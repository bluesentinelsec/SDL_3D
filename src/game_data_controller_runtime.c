/* Controller, FPS movement, brush movement, and sector door update helpers. */

#include "game_data_internal.h"

#include "slayer3d/collision.h"

#define FPS_BRUSH_VIEW_SMOOTH_SPEED 12.0f
#define FPS_BRUSH_VIEW_SMOOTH_EPSILON 0.01f

typedef struct patrol_brush_collision_context
{
    slayer3d_game_data_runtime *runtime;
    yyjson_val *collision;
    slayer3d_game_data_brush_trace_shape shape;
    slayer3d_vec3 extents;
    slayer3d_vec3 center_offset;
    unsigned int contents_mask;
    int slide_iterations;
    float contact_skin;
    float ground_probe_distance;
    float walkable_normal_y;
    const char *on_ground_property;
} patrol_brush_collision_context;

static yyjson_val *patrol_brush_collision_json(yyjson_val *component)
{
    yyjson_val *collision = obj_get(component, "collision");
    if (!yyjson_is_obj(collision))
        return NULL;
    const char *type = json_string(collision, "type", "brush");
    return SDL_strcmp(type, "brush") == 0 ? collision : NULL;
}

static slayer3d_game_data_brush_trace_shape patrol_brush_trace_shape_from_string(const char *shape)
{
    if (SDL_strcmp(shape != NULL ? shape : "", "sphere") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE;
    if (SDL_strcmp(shape != NULL ? shape : "", "aabb") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB;
    return SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
}

static bool patrol_brush_collision_context_init(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                                patrol_brush_collision_context *context)
{
    yyjson_val *collision = patrol_brush_collision_json(component);
    if (runtime == NULL || collision == NULL || context == NULL)
        return false;

    SDL_zero(*context);
    context->runtime = runtime;
    context->collision = collision;
    context->shape = patrol_brush_trace_shape_from_string(json_string(collision, "shape", "aabb"));
    context->extents = json_vec3(collision, "extents", slayer3d_vec3_make(0.35f, 0.9f, 0.35f));
    if (context->shape == SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE)
        context->extents = slayer3d_vec3_make(json_float(collision, "radius", context->extents.x), 0.0f, 0.0f);
    else if (context->shape == SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT)
        context->extents = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    context->center_offset = json_vec3(collision, "center_offset", slayer3d_vec3_make(0.0f, context->extents.y, 0.0f));
    context->contents_mask =
        brush_flags_from_json(obj_get(collision, "contents_mask"), brush_content_flag_from_string,
                              SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP);
    context->slide_iterations = SDL_clamp(json_int(collision, "slide_iterations", 4), 1, 8);
    context->contact_skin = SDL_max(json_float(collision, "contact_skin", 0.02f), 0.0f);
    context->ground_probe_distance = SDL_max(json_float(collision, "ground_probe_distance", 0.35f), 0.0f);
    context->walkable_normal_y = SDL_clamp(json_float(collision, "walkable_normal_y", 0.7f), 0.0f, 1.0f);
    context->on_ground_property = json_string(collision, "on_ground_property", "on_ground");
    return true;
}

static bool patrol_brush_trace(const patrol_brush_collision_context *context, slayer3d_vec3 start, slayer3d_vec3 end,
                               bool slide, slayer3d_game_data_brush_trace_result *out_result)
{
    if (context == NULL || out_result == NULL)
        return false;

    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.start = start;
    trace.end = end;
    trace.shape = context->shape;
    trace.extents = context->extents;
    trace.contents_mask = context->contents_mask;
    return slide ? slayer3d_game_data_slide_active_brush_worlds(context->runtime, &trace, context->slide_iterations,
                                                                out_result)
                 : slayer3d_game_data_trace_active_brush_worlds(context->runtime, &trace, out_result);
}

static void patrol_brush_publish_grounded(const patrol_brush_collision_context *context,
                                          slayer3d_registered_actor *actor, bool grounded)
{
    if (context != NULL && actor != NULL && context->on_ground_property != NULL &&
        context->on_ground_property[0] != '\0')
        slayer3d_properties_set_bool(actor->props, context->on_ground_property, grounded);
}

static void patrol_increment_animation_time(yyjson_val *component, slayer3d_registered_actor *actor,
                                            slayer3d_actor_patrol_state state, float dt)
{
    const char *property = json_string(component, "animation_time_property", NULL);
    if (component == NULL || actor == NULL || property == NULL || property[0] == '\0' || dt <= 0.0f)
        return;

    const bool animate_when_idle = json_bool(component, "animate_when_idle", false);
    if (state == SLAYER3D_ACTOR_PATROL_IDLE && !animate_when_idle)
        return;

    const float rate = json_float(component, "animation_rate", 1.0f);
    if (rate == 0.0f)
        return;
    slayer3d_properties_set_float(actor->props, property,
                                  slayer3d_properties_get_float(actor->props, property, 0.0f) + rate * dt);
}

static float patrol_yaw_from_delta(yyjson_val *component, slayer3d_vec3 delta)
{
    const char *forward_axis = json_string(component, "yaw_forward", "-z");
    if (SDL_strcmp(forward_axis != NULL ? forward_axis : "", "+z") == 0 ||
        SDL_strcmp(forward_axis != NULL ? forward_axis : "", "positive_z") == 0)
    {
        return SDL_atan2f(delta.x, delta.z);
    }
    return SDL_atan2f(delta.x, -delta.z);
}

static bool patrol_direction_to_target(const slayer3d_actor_patrol_controller *controller,
                                       const slayer3d_registered_actor *actor, slayer3d_vec3 *out_direction)
{
    if (controller == NULL || actor == NULL || out_direction == NULL || controller->waypoint_count <= 0 ||
        controller->target_waypoint < 0 || controller->target_waypoint >= controller->waypoint_count)
    {
        return false;
    }

    const slayer3d_vec3 target = controller->waypoints[controller->target_waypoint];
    const slayer3d_vec3 direction =
        slayer3d_vec3_make(target.x - actor->position.x, 0.0f, target.z - actor->position.z);
    if ((direction.x * direction.x + direction.z * direction.z) <= 0.000001f)
        return false;
    *out_direction = direction;
    return true;
}

static bool patrol_brush_move(void *userdata, const slayer3d_actor_patrol_controller *controller,
                              slayer3d_registered_actor *actor, slayer3d_vec3 desired_position,
                              slayer3d_vec3 *out_position)
{
    (void)controller;
    patrol_brush_collision_context *context = (patrol_brush_collision_context *)userdata;
    if (context == NULL || actor == NULL || out_position == NULL)
        return false;

    const slayer3d_vec3 skin_offset = slayer3d_vec3_make(0.0f, context->contact_skin, 0.0f);
    const slayer3d_vec3 start_center = slayer3d_vec3_add(actor->position, context->center_offset);
    const slayer3d_vec3 desired_center = slayer3d_vec3_add(desired_position, context->center_offset);
    slayer3d_game_data_brush_trace_result move;
    if (!patrol_brush_trace(context, slayer3d_vec3_add(start_center, skin_offset),
                            slayer3d_vec3_add(desired_center, skin_offset), true, &move) ||
        move.start_solid)
    {
        return false;
    }

    slayer3d_vec3 accepted_center =
        move.hit ? slayer3d_vec3_add(move.end_position, slayer3d_vec3_scale(move.normal, 0.01f)) : move.end_position;
    accepted_center = slayer3d_vec3_sub(accepted_center, skin_offset);
    bool grounded = false;
    if (context->ground_probe_distance > 0.0f)
    {
        const slayer3d_vec3 ground_start = slayer3d_vec3_add(accepted_center, skin_offset);
        const slayer3d_vec3 ground_end =
            slayer3d_vec3_make(ground_start.x, ground_start.y - context->ground_probe_distance, ground_start.z);
        slayer3d_game_data_brush_trace_result ground;
        if (patrol_brush_trace(context, ground_start, ground_end, false, &ground) && ground.hit &&
            !ground.start_solid && ground.normal.y >= context->walkable_normal_y)
        {
            accepted_center = slayer3d_vec3_add(slayer3d_vec3_sub(ground.end_position, skin_offset),
                                                slayer3d_vec3_scale(ground.normal, 0.01f));
            grounded = true;
        }
    }

    *out_position = slayer3d_vec3_sub(accepted_center, context->center_offset);
    patrol_brush_publish_grounded(context, actor, grounded);
    return true;
}

static bool initialize_patrol_controller(slayer3d_game_data_runtime *runtime, patrol_controller_runtime *controller,
                                         yyjson_val *component, slayer3d_registered_actor *actor)
{
    if (runtime == NULL || controller == NULL || component == NULL || actor == NULL)
        return false;

    slayer3d_actor_patrol_config config = slayer3d_actor_patrol_default_config();
    config.speed = json_float(component, "speed", config.speed);
    config.wait_time = json_float(component, "wait_time", config.wait_time);
    config.arrival_radius = json_float(component, "arrival_radius", config.arrival_radius);
    config.mode = parse_patrol_mode(json_string(component, "mode", "loop"));
    config.start_idle = json_bool(component, "start_idle", config.start_idle);
    config.signals.waypoint_reached = patrol_signal_id(runtime, component, "waypoint_reached");
    config.signals.loop_completed = patrol_signal_id(runtime, component, "loop_completed");
    config.signals.idle_started = patrol_signal_id(runtime, component, "idle_started");
    config.signals.walk_started = patrol_signal_id(runtime, component, "walk_started");

    slayer3d_actor_patrol_controller_init(&controller->controller, actor->id, actor->id, &config);
    yyjson_val *waypoints = obj_get(component, "waypoints");
    for (size_t i = 0; yyjson_is_arr(waypoints) && i < yyjson_arr_size(waypoints); ++i)
    {
        if (!slayer3d_actor_patrol_controller_add_waypoint(
                &controller->controller, json_vec3_value(yyjson_arr_get(waypoints, i), actor->position)))
        {
            return false;
        }
    }

    controller->initialized = true;
    slayer3d_actor_patrol_controller_sync_properties(&controller->controller, actor);
    return true;
}

void update_patrol_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                              slayer3d_registered_actor *actor, float dt)
{
    patrol_controller_runtime *controller =
        actor != NULL ? find_or_add_patrol_controller(runtime, actor->name, component) : NULL;
    if (controller == NULL)
        return;

    if (!controller->initialized && !initialize_patrol_controller(runtime, controller, component, actor))
        return;

    patrol_brush_collision_context collision;
    const bool use_brush_collision = patrol_brush_collision_context_init(runtime, component, &collision);
    slayer3d_actor_patrol_result result = slayer3d_actor_patrol_controller_update(
        &controller->controller, runtime_registry(runtime), runtime_bus(runtime), dt,
        use_brush_collision ? patrol_brush_move : NULL, use_brush_collision ? &collision : NULL);
    actor_set_position(actor, result.position);
    if (result.moved)
    {
        const float yaw = patrol_yaw_from_delta(component, result.movement_delta);
        slayer3d_properties_set_float(actor->props, json_string(component, "yaw_property", "yaw"), yaw);
    }
    if (json_bool(component, "face_target", false))
    {
        slayer3d_vec3 direction;
        if (patrol_direction_to_target(&controller->controller, actor, &direction))
        {
            const float yaw = patrol_yaw_from_delta(component, direction);
            slayer3d_properties_set_float(actor->props, json_string(component, "yaw_property", "yaw"), yaw);
        }
    }
    patrol_increment_animation_time(component, actor, result.state, dt);
}

static void fps_controller_publish_actor_state(const fps_controller_runtime *controller, yyjson_val *component,
                                               slayer3d_registered_actor *actor)
{
    if (controller == NULL || component == NULL || actor == NULL)
        return;
    const float cos_pitch = SDL_cosf(controller->mover.pitch);
    const slayer3d_vec3 forward =
        slayer3d_vec3_make(SDL_sinf(controller->mover.yaw) * cos_pitch, SDL_sinf(controller->mover.pitch),
                           -SDL_cosf(controller->mover.yaw) * cos_pitch);
    actor_set_position(actor, controller->mover.position);
    slayer3d_properties_set_float(actor->props, json_string(component, "yaw_property", "yaw"), controller->mover.yaw);
    slayer3d_properties_set_float(actor->props, json_string(component, "pitch_property", "pitch"),
                                  controller->mover.pitch);
    slayer3d_properties_set_vec3(actor->props, json_string(component, "forward_property", "camera_forward"), forward);
    slayer3d_properties_set_float(actor->props, json_string(component, "view_smooth_property", "view_smooth"),
                                  controller->mover.view_smooth);
    slayer3d_properties_set_float(actor->props,
                                  json_string(component, "vertical_velocity_property", "vertical_velocity"),
                                  controller->mover.vertical_velocity);
    slayer3d_properties_set_bool(actor->props, json_string(component, "on_ground_property", "on_ground"),
                                 controller->mover.on_ground);
    slayer3d_properties_set_int(actor->props, json_string(component, "sector_property", "current_sector"),
                                controller->mover.current_sector);
}

static void publish_editor_camera_actor_state(yyjson_val *component, slayer3d_registered_actor *actor, float yaw,
                                              float pitch)
{
    if (component == NULL || actor == NULL)
        return;

    const float cos_pitch = SDL_cosf(pitch);
    const slayer3d_vec3 forward =
        slayer3d_vec3_make(SDL_sinf(yaw) * cos_pitch, SDL_sinf(pitch), -SDL_cosf(yaw) * cos_pitch);
    slayer3d_properties_set_float(actor->props, json_string(component, "yaw_property", "yaw"), yaw);
    slayer3d_properties_set_float(actor->props, json_string(component, "pitch_property", "pitch"), pitch);
    slayer3d_properties_set_vec3(actor->props, json_string(component, "forward_property", "camera_forward"), forward);
}

static float editor_camera_clampf(float value, float min_value, float max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static bool editor_camera_mode_equals(const char *mode, const char *expected)
{
    return mode != NULL && expected != NULL && SDL_strcmp(mode, expected) == 0;
}

static bool editor_camera_mode_is_orthographic(const char *mode, yyjson_val *component)
{
    return editor_camera_mode_equals(mode, json_string(component, "top_mode", "orthographic_top")) ||
           editor_camera_mode_equals(mode, json_string(component, "front_mode", "orthographic_front")) ||
           editor_camera_mode_equals(mode, json_string(component, "side_mode", "orthographic_side"));
}

static const char *editor_camera_mode_for_camera(yyjson_val *component, const char *camera)
{
    if (component == NULL || camera == NULL)
        return NULL;
    const char *top_camera = json_string(component, "top_camera", NULL);
    const char *front_camera = json_string(component, "front_camera", NULL);
    const char *side_camera = json_string(component, "side_camera", NULL);
    if (top_camera != NULL && SDL_strcmp(camera, top_camera) == 0)
        return json_string(component, "top_mode", "orthographic_top");
    if (front_camera != NULL && SDL_strcmp(camera, front_camera) == 0)
        return json_string(component, "front_mode", "orthographic_front");
    if (side_camera != NULL && SDL_strcmp(camera, side_camera) == 0)
        return json_string(component, "side_mode", "orthographic_side");
    return NULL;
}

static const char *editor_camera_hovered_orthographic_mode(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                                           const slayer3d_input_manager *input)
{
    if (runtime == NULL || component == NULL || input == NULL)
        return NULL;

    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (!slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        return NULL;

    game_data_scene_world_viewport viewports[SLAYER3D_GAME_DATA_WORLD_VIEWPORT_MAX];
    int count = 0;
    if (!game_data_resolve_active_scene_world_viewports(runtime, viewports, SDL_arraysize(viewports), &count))
        return NULL;
    for (int i = 0; i < count; ++i)
    {
        const SDL_Rect rect = viewports[i].rect;
        if (mouse_x < (float)rect.x || mouse_y < (float)rect.y || mouse_x >= (float)(rect.x + rect.w) ||
            mouse_y >= (float)(rect.y + rect.h))
        {
            continue;
        }
        return editor_camera_mode_for_camera(component, viewports[i].camera);
    }

    return NULL;
}

static void editor_camera_orthographic_basis(const char *mode, yyjson_val *component, slayer3d_vec3 *out_right,
                                             slayer3d_vec3 *out_up)
{
    if (out_right == NULL || out_up == NULL)
        return;

    if (editor_camera_mode_equals(mode, json_string(component, "front_mode", "orthographic_front")))
    {
        *out_right = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        *out_up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
        return;
    }
    if (editor_camera_mode_equals(mode, json_string(component, "side_mode", "orthographic_side")))
    {
        *out_right = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
        *out_up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
        return;
    }

    *out_right = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    *out_up = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
}

static void update_editor_camera_orthographic_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                                         slayer3d_registered_actor *actor,
                                                         const slayer3d_input_manager *input, float dt,
                                                         const char *mode)
{
    if (runtime == NULL || component == NULL || actor == NULL || !editor_camera_mode_is_orthographic(mode, component))
        return;

    const float pan_left =
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "pan_left"));
    const float pan_right =
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "pan_right"));
    const float pan_up =
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "pan_up"));
    const float pan_down =
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "pan_down"));
    const char *size_key = json_string(component, "orthographic_size_key", NULL);
    const float authored_size = json_float(component, "orthographic_size", 48.0f);
    float ortho_size = scene_state_float(runtime, size_key, authored_size);
    if (ortho_size <= 0.0f)
        ortho_size = authored_size > 0.0f ? authored_size : 48.0f;

    float pan_x = pan_right - pan_left;
    float pan_y = pan_up - pan_down;
    const float pan_len_sq = pan_x * pan_x + pan_y * pan_y;
    if (pan_len_sq > 1.0f)
    {
        const float inv_len = 1.0f / SDL_sqrtf(pan_len_sq);
        pan_x *= inv_len;
        pan_y *= inv_len;
    }

    slayer3d_vec3 right;
    slayer3d_vec3 up;
    editor_camera_orthographic_basis(mode, component, &right, &up);
    const float pan_speed = json_float(component, "orthographic_pan_speed", 0.85f) * ortho_size;
    const float seconds = SDL_max(dt, 0.0f);
    const slayer3d_vec3 delta = slayer3d_vec3_scale(
        slayer3d_vec3_add(slayer3d_vec3_scale(right, pan_x), slayer3d_vec3_scale(up, pan_y)), pan_speed * seconds);
    if (slayer3d_vec3_length_squared(delta) > 0.0000001f)
        actor_set_position(actor, slayer3d_vec3_add(actor->position, delta));

    const float zoom_in =
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "zoom_in"));
    const float zoom_out =
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "zoom_out"));
    const float zoom_wheel =
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "zoom_wheel"));
    const float zoom_speed = json_float(component, "orthographic_zoom_speed", 1.75f);
    const float wheel_step = json_float(component, "orthographic_wheel_zoom_step", 0.12f);
    const float zoom_factor = 1.0f + (zoom_out - zoom_in) * zoom_speed * seconds - zoom_wheel * wheel_step;
    if (zoom_factor > 0.05f && SDL_fabsf(zoom_factor - 1.0f) > 0.0001f)
    {
        const float min_size = json_float(component, "orthographic_min_size", 2.0f);
        const float max_size = json_float(component, "orthographic_max_size", 512.0f);
        ortho_size =
            editor_camera_clampf(ortho_size * zoom_factor, SDL_min(min_size, max_size), SDL_max(min_size, max_size));
        if (runtime->scene_state != NULL && size_key != NULL && size_key[0] != '\0')
            slayer3d_properties_set_float(runtime->scene_state, size_key, ortho_size);
    }
}

static bool editor_camera_hover_or_selection_pivot(slayer3d_game_data_runtime *runtime, bool prefer_selection,
                                                   slayer3d_vec3 *out_pivot, slayer3d_bounding_box *out_bounds)
{
    if (runtime == NULL || out_pivot == NULL)
        return false;

    slayer3d_game_data_editor_selection selection;
    if (prefer_selection && slayer3d_game_data_get_active_editor_selection(runtime, &selection) && selection.has_bounds)
    {
        *out_pivot = slayer3d_vec3_scale(slayer3d_vec3_add(selection.bounds.min, selection.bounds.max), 0.5f);
        if (out_bounds != NULL)
            *out_bounds = selection.bounds;
        return true;
    }

    if (slayer3d_properties_get_bool(runtime->scene_state, "editor.hover.hit", false))
    {
        *out_pivot = slayer3d_properties_get_vec3(runtime->scene_state, "editor.hover.point",
                                                  slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        if (out_bounds != NULL)
        {
            out_bounds->min = slayer3d_properties_get_vec3(runtime->scene_state, "editor.hover.bounds_min", *out_pivot);
            out_bounds->max = slayer3d_properties_get_vec3(runtime->scene_state, "editor.hover.bounds_max", *out_pivot);
        }
        return true;
    }

    if (!slayer3d_game_data_get_active_editor_selection(runtime, &selection) || !selection.has_bounds)
        return false;

    *out_pivot = slayer3d_vec3_scale(slayer3d_vec3_add(selection.bounds.min, selection.bounds.max), 0.5f);
    if (out_bounds != NULL)
        *out_bounds = selection.bounds;
    return true;
}

void update_editor_camera_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                     slayer3d_registered_actor *actor, const slayer3d_input_manager *input, float dt)
{
    if (runtime == NULL || component == NULL || actor == NULL)
        return;

    const char *mode = scene_state_string(runtime, json_string(component, "mode_key", "editor.view.mode"), NULL);
    yyjson_val *controls_if = obj_get(component, "controls_if");
    if (controls_if != NULL && !eval_data_condition(runtime, controls_if, NULL))
    {
        runtime->editor_camera_orbit.active = false;
        runtime->editor_camera_move.active = false;
        runtime->editor_camera_move.hold_seconds = 0.0f;
        runtime->editor_camera_move.direction = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        return;
    }
    if (editor_camera_mode_is_orthographic(mode, component))
    {
        yyjson_val *orthographic_controls_if = obj_get(component, "orthographic_controls_if");
        if (orthographic_controls_if != NULL && !eval_data_condition(runtime, orthographic_controls_if, NULL))
            return;
        update_editor_camera_orthographic_controller(runtime, component, actor, input, dt, mode);
        return;
    }
    const char *hovered_orthographic_mode = editor_camera_hovered_orthographic_mode(runtime, component, input);
    if (hovered_orthographic_mode != NULL)
    {
        yyjson_val *orthographic_controls_if = obj_get(component, "orthographic_controls_if");
        if (orthographic_controls_if != NULL && !eval_data_condition(runtime, orthographic_controls_if, NULL))
            return;
        update_editor_camera_orthographic_controller(runtime, component, actor, input, dt, hovered_orthographic_mode);
        return;
    }
    if (mode != NULL && !editor_camera_mode_equals(mode, json_string(component, "flyby_mode", "flyby_3d")))
        return;

    const char *yaw_property = json_string(component, "yaw_property", "yaw");
    const char *pitch_property = json_string(component, "pitch_property", "pitch");
    float yaw = slayer3d_properties_get_float(actor->props, yaw_property, json_float(component, "spawn_yaw", 0.0f));
    float pitch =
        slayer3d_properties_get_float(actor->props, pitch_property, json_float(component, "spawn_pitch", 0.0f));

    const int look_action = fps_controller_action_id(runtime, component, "look");
    const bool look_active =
        input != NULL && (look_action < 0 || fps_controller_action_value(runtime, input, look_action) > 0.0f);
    const SDL_Keymod modifiers = SDL_GetModState();
    const bool orbit_active = (modifiers & SDL_KMOD_ALT) != 0;
    if (!look_active || !orbit_active)
        runtime->editor_camera_orbit.active = false;
    if (look_active && orbit_active && !runtime->editor_camera_orbit.active)
    {
        slayer3d_vec3 pivot = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        if (editor_camera_hover_or_selection_pivot(runtime, false, &pivot, NULL))
        {
            runtime->editor_camera_orbit.active = true;
            runtime->editor_camera_orbit.pivot = pivot;
            runtime->editor_camera_orbit.radius =
                SDL_max(slayer3d_vec3_length(slayer3d_vec3_sub(actor->position, pivot)), 0.5f);
        }
    }
    if (json_bool(component, "mouse_look", true) && look_active)
    {
        const float sensitivity = orbit_active ? json_float(component, "orbit_sensitivity",
                                                            json_float(component, "mouse_sensitivity", 0.002f))
                                               : json_float(component, "mouse_sensitivity", 0.002f);
        yaw += slayer3d_input_get_mouse_dx(input) * sensitivity;
        pitch -= slayer3d_input_get_mouse_dy(input) * sensitivity;
    }
    const float pitch_min = json_float(component, "pitch_min", -1.4f);
    const float pitch_max = json_float(component, "pitch_max", 1.4f);
    pitch = editor_camera_clampf(pitch, SDL_min(pitch_min, pitch_max), SDL_max(pitch_min, pitch_max));

    const float cos_pitch = SDL_cosf(pitch);
    const slayer3d_vec3 camera_forward =
        slayer3d_vec3_make(SDL_sinf(yaw) * cos_pitch, SDL_sinf(pitch), -SDL_cosf(yaw) * cos_pitch);
    const slayer3d_vec3 camera_right = slayer3d_vec3_make(SDL_cosf(yaw), 0.0f, SDL_sinf(yaw));
    const slayer3d_vec3 camera_up = slayer3d_vec3_normalize(slayer3d_vec3_cross(camera_right, camera_forward));

    if (runtime->editor_camera_orbit.active && look_active && orbit_active &&
        (SDL_fabsf(slayer3d_input_get_mouse_dx(input)) > 0.0f || SDL_fabsf(slayer3d_input_get_mouse_dy(input)) > 0.0f))
    {
        actor_set_position(actor,
                           slayer3d_vec3_sub(runtime->editor_camera_orbit.pivot,
                                             slayer3d_vec3_scale(camera_forward, runtime->editor_camera_orbit.radius)));
    }

    const int frame_action = fps_controller_action_id(runtime, component, "frame_selected");
    if (input != NULL && frame_action >= 0 && slayer3d_input_is_pressed(input, frame_action))
    {
        slayer3d_vec3 pivot;
        slayer3d_bounding_box bounds;
        if (editor_camera_hover_or_selection_pivot(runtime, true, &pivot, &bounds))
        {
            const slayer3d_vec3 extents = slayer3d_vec3_sub(bounds.max, bounds.min);
            const float max_extent = SDL_max(SDL_max(extents.x, extents.y), extents.z);
            const float distance = SDL_max(max_extent * 2.25f, 2.0f);
            actor_set_position(actor, slayer3d_vec3_sub(pivot, slayer3d_vec3_scale(camera_forward, distance)));
        }
    }

    const int mouse_pan_action = fps_controller_action_id(runtime, component, "mouse_pan");
    if (input != NULL && mouse_pan_action >= 0 && fps_controller_action_value(runtime, input, mouse_pan_action) > 0.0f)
    {
        const float pan_sensitivity = json_float(component, "mouse_pan_sensitivity", 0.012f);
        const slayer3d_vec3 pan_delta =
            slayer3d_vec3_scale(slayer3d_vec3_sub(slayer3d_vec3_scale(camera_right, slayer3d_input_get_mouse_dx(input)),
                                                  slayer3d_vec3_scale(camera_up, slayer3d_input_get_mouse_dy(input))),
                                pan_sensitivity);
        if (slayer3d_vec3_length_squared(pan_delta) > 0.0000001f)
            actor_set_position(actor, slayer3d_vec3_add(actor->position, pan_delta));
    }

    const float wheel_zoom =
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "zoom_wheel"));
    if (SDL_fabsf(wheel_zoom) > 0.0001f)
    {
        const float wheel_step = json_float(component, "perspective_wheel_zoom_step", 1.25f);
        const slayer3d_vec3 dolly_delta = slayer3d_vec3_scale(camera_forward, wheel_zoom * wheel_step);
        if (slayer3d_vec3_length_squared(dolly_delta) > 0.0000001f)
            actor_set_position(actor, slayer3d_vec3_add(actor->position, dolly_delta));
    }

    float forward =
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "forward")) -
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "back"));
    float side = fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "right")) -
                 fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "left"));
    float vertical = fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "up")) -
                     fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "down"));
    const float len_sq = forward * forward + side * side + vertical * vertical;
    if (len_sq > 1.0f)
    {
        const float inv_len = 1.0f / SDL_sqrtf(len_sq);
        forward *= inv_len;
        side *= inv_len;
        vertical *= inv_len;
    }

    const slayer3d_vec3 move_direction = slayer3d_vec3_make(forward, side, vertical);
    const float move_dt = SDL_max(dt, 0.0f);
    float hold_multiplier = 1.0f;
    if (len_sq > 0.000001f && move_dt > 0.0f)
    {
        const slayer3d_vec3 normalized_move_direction = slayer3d_vec3_normalize(move_direction);
        if (runtime->editor_camera_move.active &&
            slayer3d_vec3_dot(runtime->editor_camera_move.direction, normalized_move_direction) > 0.75f)
        {
            runtime->editor_camera_move.hold_seconds += move_dt;
        }
        else
        {
            runtime->editor_camera_move.active = true;
            runtime->editor_camera_move.hold_seconds = move_dt;
        }
        runtime->editor_camera_move.direction = normalized_move_direction;

        const float acceleration_delay = json_float(component, "move_acceleration_delay", 0.75f);
        const float acceleration_ramp_time = json_float(component, "move_acceleration_ramp_time", 3.0f);
        const float acceleration_max_multiplier = json_float(component, "move_acceleration_max_multiplier", 2.5f);
        if (acceleration_max_multiplier > 1.0f && runtime->editor_camera_move.hold_seconds > acceleration_delay)
        {
            const float ramp =
                acceleration_ramp_time > 0.0001f
                    ? (runtime->editor_camera_move.hold_seconds - acceleration_delay) / acceleration_ramp_time
                    : 1.0f;
            hold_multiplier = 1.0f + (acceleration_max_multiplier - 1.0f) * editor_camera_clampf(ramp, 0.0f, 1.0f);
        }
    }
    else
    {
        runtime->editor_camera_move.active = false;
        runtime->editor_camera_move.hold_seconds = 0.0f;
        runtime->editor_camera_move.direction = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    }

    const float fast =
        fps_controller_action_value(runtime, input, fps_controller_action_id(runtime, component, "fast"));
    const float speed = fast > 0.0f
                            ? json_float(component, "fast_speed", json_float(component, "move_speed", 8.0f) * 2.5f)
                            : json_float(component, "move_speed", 8.0f);
    const slayer3d_vec3 move_forward = json_bool(component, "move_forward_with_pitch", false)
                                           ? camera_forward
                                           : slayer3d_vec3_make(SDL_sinf(yaw), 0.0f, -SDL_cosf(yaw));
    const float move_step = speed * hold_multiplier * move_dt;
    slayer3d_vec3 world_move = slayer3d_vec3_add(
        slayer3d_vec3_add(slayer3d_vec3_scale(move_forward, forward), slayer3d_vec3_scale(camera_right, side)),
        slayer3d_vec3_make(0.0f, vertical, 0.0f));
    const float world_move_len_sq = slayer3d_vec3_length_squared(world_move);
    if (world_move_len_sq > 1.0f)
        world_move = slayer3d_vec3_scale(world_move, 1.0f / SDL_sqrtf(world_move_len_sq));
    const slayer3d_vec3 delta = slayer3d_vec3_scale(world_move, move_step);
    if (slayer3d_vec3_length_squared(delta) > 0.0000001f)
        actor_set_position(actor, slayer3d_vec3_add(actor->position, delta));

    publish_editor_camera_actor_state(component, actor, yaw, pitch);
}

static bool initialize_fps_controller_runtime(slayer3d_game_data_runtime *runtime, fps_controller_runtime *controller,
                                              yyjson_val *component, slayer3d_registered_actor *actor)
{
    (void)runtime;
    if (controller == NULL || component == NULL || actor == NULL)
        return false;
    if (controller->initialized)
        return true;

    slayer3d_fps_mover_config config;
    SDL_zero(config);
    config.move_speed = json_float(component, "move_speed", 12.0f);
    config.jump_velocity = json_float(component, "jump_velocity", 6.0f);
    config.gravity = json_float(component, "gravity", 14.0f);
    config.player_height = json_float(component, "player_height", 1.6f);
    config.player_radius = json_float(component, "player_radius", 0.35f);
    config.step_height = json_float(component, "step_height", 1.1f);
    config.ceiling_clearance = json_float(component, "ceiling_clearance", 0.1f);
    const float yaw = slayer3d_properties_get_float(actor->props, json_string(component, "yaw_property", "yaw"),
                                                    json_float(component, "spawn_yaw", 0.0f));
    slayer3d_fps_mover_init(&controller->mover, &config, actor->position, yaw);
    controller->mover.pitch = slayer3d_properties_get_float(
        actor->props, json_string(component, "pitch_property", "pitch"), json_float(component, "spawn_pitch", 0.0f));
    controller->initialized = true;
    fps_controller_publish_actor_state(controller, component, actor);
    return true;
}

static fps_controller_runtime *fps_controller_for_actor_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                               const slayer3d_properties *payload,
                                                               slayer3d_registered_actor **out_actor,
                                                               yyjson_val **out_component)
{
    const char *target = json_string(action, "target", NULL);
    const char *target_from_payload = json_string(action, "target_from_payload", NULL);
    if ((target == NULL || target[0] == '\0') && target_from_payload != NULL && payload != NULL)
        target = slayer3d_properties_get_string(payload, target_from_payload, NULL);

    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    yyjson_val *entity = find_entity_json(runtime, target);
    yyjson_val *component = find_component_json(entity, "controller.fps_sector");
    if (component == NULL)
        component = find_component_json(entity, "controller.fps_brush");
    fps_controller_runtime *controller =
        actor != NULL && component != NULL ? find_or_add_fps_controller(runtime, target, component) : NULL;
    if (controller == NULL || !initialize_fps_controller_runtime(runtime, controller, component, actor))
        return NULL;

    if (out_actor != NULL)
        *out_actor = actor;
    if (out_component != NULL)
        *out_component = component;
    return controller;
}

bool execute_fps_controller_launch_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                          const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = NULL;
    yyjson_val *component = NULL;
    fps_controller_runtime *controller = fps_controller_for_actor_action(runtime, action, payload, &actor, &component);
    const float vertical_velocity = json_float(action, "vertical_velocity", 0.0f);
    if (controller == NULL || vertical_velocity <= 0.0f)
        return false;

    slayer3d_fps_mover_launch(&controller->mover, vertical_velocity);
    fps_controller_publish_actor_state(controller, component, actor);
    return true;
}

bool execute_fps_controller_teleport_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = NULL;
    yyjson_val *component = NULL;
    fps_controller_runtime *controller = fps_controller_for_actor_action(runtime, action, payload, &actor, &component);
    yyjson_val *position = obj_get(action, "position");
    if (controller == NULL || !yyjson_is_arr(position) || yyjson_arr_size(position) != 3)
        return false;

    yyjson_val *yaw = obj_get(action, "yaw");
    yyjson_val *pitch = obj_get(action, "pitch");
    slayer3d_fps_mover_teleport(&controller->mover, json_vec3_value(position, actor->position), yyjson_is_num(yaw),
                                json_float(action, "yaw", controller->mover.yaw), yyjson_is_num(pitch),
                                json_float(action, "pitch", controller->mover.pitch));
    fps_controller_publish_actor_state(controller, component, actor);
    return true;
}

static slayer3d_bounding_box sector_door_closed_bounds(const slayer3d_door *door)
{
    if (door == NULL || door->panel_count <= 0)
    {
        return (slayer3d_bounding_box){
            slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
            slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
        };
    }
    slayer3d_bounding_box bounds = door->panels[0].closed_bounds;
    for (int i = 1; i < door->panel_count; ++i)
    {
        const slayer3d_bounding_box panel = door->panels[i].closed_bounds;
        bounds.min.x = SDL_min(bounds.min.x, panel.min.x);
        bounds.min.y = SDL_min(bounds.min.y, panel.min.y);
        bounds.min.z = SDL_min(bounds.min.z, panel.min.z);
        bounds.max.x = SDL_max(bounds.max.x, panel.max.x);
        bounds.max.y = SDL_max(bounds.max.y, panel.max.y);
        bounds.max.z = SDL_max(bounds.max.z, panel.max.z);
    }
    return bounds;
}

static slayer3d_vec3 sector_door_closed_center(const slayer3d_door *door)
{
    const slayer3d_bounding_box bounds = sector_door_closed_bounds(door);
    return slayer3d_vec3_make((bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f,
                              (bounds.min.z + bounds.max.z) * 0.5f);
}

float sector_door_distance_sq_xz(const slayer3d_door *door, slayer3d_vec3 point)
{
    if (door == NULL || door->panel_count <= 0)
        return 0.0f;
    const slayer3d_vec3 center = sector_door_closed_center(door);
    const float dx = center.x - point.x;
    const float dz = center.z - point.z;
    return dx * dx + dz * dz;
}

bool sector_door_is_in_front(const slayer3d_door *door, slayer3d_vec3 point, float yaw, float min_dot)
{
    if (door == NULL)
        return false;
    const slayer3d_vec3 center = sector_door_closed_center(door);
    float dx = center.x - point.x;
    float dz = center.z - point.z;
    const float length = SDL_sqrtf(dx * dx + dz * dz);
    if (length <= 0.0001f)
        return true;
    dx /= length;
    dz /= length;
    const float forward_x = SDL_sinf(yaw);
    const float forward_z = -SDL_cosf(yaw);
    return dx * forward_x + dz * forward_z >= min_dot;
}

static void resolve_fps_controller_sector_doors(slayer3d_game_data_runtime *runtime, fps_controller_runtime *controller)
{
    if (runtime == NULL || controller == NULL)
        return;

    bool resolved = false;
    for (int i = 0; i < runtime->sector_door_count; ++i)
    {
        sector_door_runtime *door = &runtime->sector_doors[i];
        if (!sector_door_in_active_scene(runtime, door))
            continue;
        resolved = slayer3d_door_resolve_cylinder(&door->door, &controller->mover.position,
                                                  controller->mover.config.player_height,
                                                  controller->mover.config.player_radius) ||
                   resolved;
    }
    if (resolved)
    {
        controller->mover.last_good_position = controller->mover.position;
        controller->mover.has_last_good = true;
    }
}

void update_fps_sector_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                  slayer3d_registered_actor *actor, const slayer3d_input_manager *input, float dt)
{
    if (runtime == NULL || component == NULL || actor == NULL)
        return;

    const sector_level_runtime *sector_level =
        find_sector_level_runtime(runtime, json_string(component, "sector_level", NULL));
    if (sector_level == NULL)
        return;

    fps_controller_runtime *controller = find_or_add_fps_controller(runtime, actor->name, component);
    if (controller == NULL)
        return;

    if (!initialize_fps_controller_runtime(runtime, controller, component, actor))
        return;

    const int forward_action = fps_controller_action_id(runtime, component, "forward");
    const int back_action = fps_controller_action_id(runtime, component, "back");
    const int left_action = fps_controller_action_id(runtime, component, "left");
    const int right_action = fps_controller_action_id(runtime, component, "right");
    const int jump_action = fps_controller_action_id(runtime, component, "jump");

    if (fps_controller_action_pressed(runtime, input, jump_action))
        slayer3d_fps_mover_jump(&controller->mover);

    const float forward = fps_controller_action_value(runtime, input, forward_action) -
                          fps_controller_action_value(runtime, input, back_action);
    const float side = fps_controller_action_value(runtime, input, right_action) -
                       fps_controller_action_value(runtime, input, left_action);
    const float fwd_x = SDL_sinf(controller->mover.yaw);
    const float fwd_z = -SDL_cosf(controller->mover.yaw);
    const float right_x = SDL_cosf(controller->mover.yaw);
    const float right_z = SDL_sinf(controller->mover.yaw);
    const slayer3d_vec2 wish = {
        fwd_x * forward + right_x * side,
        fwd_z * forward + right_z * side,
    };

    const bool mouse_look = json_bool(component, "mouse_look", true);
    const float mouse_dx = mouse_look && input != NULL ? slayer3d_input_get_mouse_dx(input) : 0.0f;
    const float mouse_dy = mouse_look && input != NULL ? slayer3d_input_get_mouse_dy(input) : 0.0f;
    slayer3d_fps_mover_update(&controller->mover, &sector_level->lightmapped, sector_level->sectors, wish, mouse_dx,
                              mouse_dy, json_float(component, "mouse_sensitivity", 0.002f), dt);
    resolve_fps_controller_sector_doors(runtime, controller);
    fps_controller_publish_actor_state(controller, component, actor);
}

static float fps_brush_clampf(float value, float min_value, float max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static void fps_brush_decay_view_smooth(slayer3d_fps_mover *mover, float dt)
{
    if (mover == NULL)
        return;

    if (SDL_fabsf(mover->view_smooth) <= FPS_BRUSH_VIEW_SMOOTH_EPSILON)
    {
        mover->view_smooth = 0.0f;
        return;
    }

    const float decay = fps_brush_clampf(FPS_BRUSH_VIEW_SMOOTH_SPEED * SDL_max(dt, 0.0f), 0.0f, 1.0f);
    mover->view_smooth -= mover->view_smooth * decay;
}

static unsigned int fps_brush_contents_mask(yyjson_val *component)
{
    return brush_flags_from_json(obj_get(component, "contents_mask"), brush_content_flag_from_string,
                                 SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP);
}

static float fps_brush_walkable_normal_y(yyjson_val *component)
{
    return SDL_clamp(json_float(component, "walkable_normal_y", 0.7f), 0.0f, 1.0f);
}

typedef struct fps_brush_diagnostics
{
    const char *collision_kind;
    slayer3d_vec3 collision_normal;
    const char *collision_brush;
    const char *collision_material;
    unsigned int collision_contents;
    unsigned int collision_surface_flags;
    slayer3d_vec3 floor_normal;
    const char *floor_brush;
    bool stepped_up;
} fps_brush_diagnostics;

static void fps_brush_diagnostics_init(fps_brush_diagnostics *diagnostics)
{
    if (diagnostics == NULL)
        return;
    SDL_zero(*diagnostics);
    diagnostics->collision_kind = "none";
    diagnostics->collision_normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    diagnostics->floor_normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static const char *fps_brush_collision_kind(slayer3d_vec3 normal, float walkable_normal_y)
{
    if (normal.y >= walkable_normal_y)
        return "floor";
    if (normal.y <= -walkable_normal_y)
        return "ceiling";
    return "wall";
}

static void fps_brush_record_collision(fps_brush_diagnostics *diagnostics,
                                       const slayer3d_game_data_brush_trace_result *result, float walkable_normal_y)
{
    if (diagnostics == NULL || result == NULL || !result->hit)
        return;
    diagnostics->collision_kind =
        result->start_solid ? "solid" : fps_brush_collision_kind(result->normal, walkable_normal_y);
    diagnostics->collision_normal = result->normal;
    diagnostics->collision_brush = result->brush_name;
    diagnostics->collision_material = result->material_name;
    diagnostics->collision_contents = result->contents;
    diagnostics->collision_surface_flags = result->surface_flags;
}

static void fps_brush_record_floor(fps_brush_diagnostics *diagnostics,
                                   const slayer3d_game_data_brush_trace_result *result)
{
    if (diagnostics == NULL || result == NULL || !result->hit)
        return;
    diagnostics->floor_normal = result->normal;
    diagnostics->floor_brush = result->brush_name;
}

static void fps_brush_publish_diagnostics(const fps_brush_diagnostics *diagnostics, yyjson_val *component,
                                          slayer3d_registered_actor *actor)
{
    if (diagnostics == NULL || component == NULL || actor == NULL)
        return;
    slayer3d_properties_set_string(actor->props,
                                   json_string(component, "brush_collision_kind_property", "brush_collision_kind"),
                                   diagnostics->collision_kind != NULL ? diagnostics->collision_kind : "none");
    slayer3d_properties_set_vec3(actor->props,
                                 json_string(component, "brush_collision_normal_property", "brush_collision_normal"),
                                 diagnostics->collision_normal);
    slayer3d_properties_set_string(actor->props,
                                   json_string(component, "brush_collision_brush_property", "brush_collision_brush"),
                                   diagnostics->collision_brush != NULL ? diagnostics->collision_brush : "");
    slayer3d_properties_set_string(
        actor->props, json_string(component, "brush_collision_material_property", "brush_collision_material"),
        diagnostics->collision_material != NULL ? diagnostics->collision_material : "");
    slayer3d_properties_set_int(actor->props,
                                json_string(component, "brush_collision_contents_property", "brush_collision_contents"),
                                (int)SDL_min(diagnostics->collision_contents, (unsigned int)SDL_MAX_SINT32));
    slayer3d_properties_set_int(
        actor->props, json_string(component, "brush_collision_surface_flags_property", "brush_collision_surface_flags"),
        (int)SDL_min(diagnostics->collision_surface_flags, (unsigned int)SDL_MAX_SINT32));
    slayer3d_properties_set_vec3(actor->props,
                                 json_string(component, "brush_floor_normal_property", "brush_floor_normal"),
                                 diagnostics->floor_normal);
    slayer3d_properties_set_string(actor->props,
                                   json_string(component, "brush_floor_brush_property", "brush_floor_brush"),
                                   diagnostics->floor_brush != NULL ? diagnostics->floor_brush : "");
    slayer3d_properties_set_bool(actor->props, json_string(component, "brush_step_up_property", "brush_step_up"),
                                 diagnostics->stepped_up);
}

static slayer3d_vec3 fps_brush_body_extents(const slayer3d_fps_mover *mover)
{
    const float skin = 0.01f;
    const float radius = SDL_max((mover != NULL ? mover->config.player_radius : 0.0f) - skin, 0.0f);
    const float height =
        SDL_max(mover != NULL ? mover->config.player_height + mover->config.ceiling_clearance : 0.0f, radius * 2.0f);
    return slayer3d_vec3_make(radius, SDL_max(height * 0.5f - skin, 0.0f), radius);
}

static slayer3d_vec3 fps_brush_eye_to_body_center(const slayer3d_fps_mover *mover, slayer3d_vec3 eye_position)
{
    const float height = mover != NULL ? mover->config.player_height : 0.0f;
    const float clearance = mover != NULL ? mover->config.ceiling_clearance : 0.0f;
    return slayer3d_vec3_make(eye_position.x, eye_position.y - height * 0.5f + clearance * 0.5f, eye_position.z);
}

static slayer3d_vec3 fps_brush_body_center_to_eye(const slayer3d_fps_mover *mover, slayer3d_vec3 body_center)
{
    const float height = mover != NULL ? mover->config.player_height : 0.0f;
    const float clearance = mover != NULL ? mover->config.ceiling_clearance : 0.0f;
    return slayer3d_vec3_make(body_center.x, body_center.y + height * 0.5f - clearance * 0.5f, body_center.z);
}

static bool fps_brush_trace_body(const slayer3d_game_data_runtime *runtime, const slayer3d_fps_mover *mover,
                                 unsigned int contents_mask, slayer3d_vec3 start, slayer3d_vec3 end,
                                 slayer3d_game_data_brush_trace_result *out_result)
{
    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.start = start;
    trace.end = end;
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB;
    trace.extents = fps_brush_body_extents(mover);
    trace.contents_mask = contents_mask;
    return slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, out_result);
}

static unsigned int fps_brush_liquid_contents_at_point(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 point)
{
    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.start = point;
    trace.end = point;
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.contents_mask = SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER | SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA;

    slayer3d_game_data_brush_trace_result result;
    if (!slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, &result) || !result.hit || !result.start_solid)
        return 0u;
    return result.contents & trace.contents_mask;
}

static slayer3d_bounding_box fps_brush_body_bounds(const slayer3d_fps_mover *mover, slayer3d_vec3 body_center)
{
    const slayer3d_vec3 extents = fps_brush_body_extents(mover);
    slayer3d_bounding_box bounds;
    bounds.min = slayer3d_vec3_make(body_center.x - extents.x, body_center.y - extents.y, body_center.z - extents.z);
    bounds.max = slayer3d_vec3_make(body_center.x + extents.x, body_center.y + extents.y, body_center.z + extents.z);
    return bounds;
}

static slayer3d_bounding_box fps_brush_translate_bounds(slayer3d_bounding_box bounds, slayer3d_vec3 translation)
{
    bounds.min = slayer3d_vec3_add(bounds.min, translation);
    bounds.max = slayer3d_vec3_add(bounds.max, translation);
    return bounds;
}

typedef struct fps_brush_liquid_overlap_context
{
    slayer3d_bounding_box body_bounds;
    unsigned int contents;
} fps_brush_liquid_overlap_context;

static bool fps_brush_accumulate_liquid_overlap(void *userdata, const slayer3d_game_data_brush_world_instance *instance)
{
    fps_brush_liquid_overlap_context *context = (fps_brush_liquid_overlap_context *)userdata;
    if (context == NULL || instance == NULL || instance->world == NULL)
        return true;

    const unsigned int liquid_mask = SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER | SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA;
    for (int i = 0; i < instance->world->brush_count; ++i)
    {
        const slayer3d_game_data_brush *brush = &instance->world->brushes[i];
        const unsigned int liquid_contents = brush->contents & liquid_mask;
        if (liquid_contents == 0u || !brush->has_bounds)
            continue;

        const slayer3d_bounding_box brush_bounds = fps_brush_translate_bounds(brush->bounds, instance->position);
        if (slayer3d_check_aabb_aabb(context->body_bounds, brush_bounds))
            context->contents |= liquid_contents;
    }
    return true;
}

static unsigned int fps_brush_liquid_contents_for_body_overlap(const slayer3d_game_data_runtime *runtime,
                                                               const slayer3d_fps_mover *mover,
                                                               slayer3d_vec3 body_center)
{
    if (runtime == NULL || mover == NULL)
        return 0u;

    fps_brush_liquid_overlap_context context;
    SDL_zero(context);
    context.body_bounds = fps_brush_body_bounds(mover, body_center);
    context.body_bounds.min.y -= 0.08f;
    (void)slayer3d_game_data_for_each_brush_world_instance(runtime, fps_brush_accumulate_liquid_overlap, &context);
    return context.contents;
}

static unsigned int fps_brush_liquid_contents_for_body(const slayer3d_game_data_runtime *runtime,
                                                       const slayer3d_fps_mover *mover, slayer3d_vec3 body_center)
{
    if (runtime == NULL || mover == NULL)
        return 0u;

    const slayer3d_vec3 extents = fps_brush_body_extents(mover);
    const slayer3d_vec3 feet = slayer3d_vec3_make(body_center.x, body_center.y - extents.y + 0.08f, body_center.z);
    const slayer3d_vec3 waist = body_center;
    const slayer3d_vec3 eye = fps_brush_body_center_to_eye(mover, body_center);

    unsigned int contents = fps_brush_liquid_contents_for_body_overlap(runtime, mover, body_center);
    contents |= fps_brush_liquid_contents_at_point(runtime, waist);
    contents |= fps_brush_liquid_contents_at_point(runtime, feet);
    contents |= fps_brush_liquid_contents_at_point(runtime, eye);
    return contents;
}

static bool fps_brush_slide_body(const slayer3d_game_data_runtime *runtime, const slayer3d_fps_mover *mover,
                                 unsigned int contents_mask, slayer3d_vec3 start, slayer3d_vec3 end,
                                 slayer3d_game_data_brush_trace_result *out_result)
{
    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.start = start;
    trace.end = end;
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB;
    trace.extents = fps_brush_body_extents(mover);
    trace.contents_mask = contents_mask;
    return slayer3d_game_data_slide_active_brush_worlds(runtime, &trace, 4, out_result);
}

bool execute_fps_controller_push_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                        const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = NULL;
    yyjson_val *component = NULL;
    fps_controller_runtime *controller = fps_controller_for_actor_action(runtime, action, payload, &actor, &component);
    yyjson_val *velocity_json = obj_get(action, "velocity");
    if (controller == NULL || actor == NULL || !yyjson_is_arr(velocity_json) || yyjson_arr_size(velocity_json) != 3)
        return false;

    slayer3d_vec3 delta = json_vec3_value(velocity_json, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    if (json_bool(action, "scale_by_dt", true))
        delta = slayer3d_vec3_scale(delta, runtime != NULL ? SDL_max(runtime->current_dt, 0.0f) : 0.0f);
    if (slayer3d_vec3_length_squared(delta) <= 0.0000001f)
        return true;

    slayer3d_fps_mover *mover = &controller->mover;
    if (component != NULL && find_brush_world_runtime(runtime, json_string(component, "brush_world", NULL)) != NULL)
    {
        const unsigned int contents_mask = fps_brush_contents_mask(component);
        slayer3d_vec3 start = fps_brush_eye_to_body_center(mover, mover->position);
        slayer3d_vec3 end = slayer3d_vec3_add(start, delta);
        slayer3d_game_data_brush_trace_result slide;
        if (fps_brush_slide_body(runtime, mover, contents_mask, start, end, &slide))
        {
            const slayer3d_vec3 accepted =
                slide.hit ? slayer3d_vec3_add(slide.end_position, slayer3d_vec3_scale(slide.normal, 0.01f))
                          : slide.end_position;
            mover->position = fps_brush_body_center_to_eye(mover, accepted);
        }
        else
        {
            mover->position = slayer3d_vec3_add(mover->position, delta);
        }
    }
    else
    {
        mover->position = slayer3d_vec3_add(mover->position, delta);
    }

    fps_controller_publish_actor_state(controller, component, actor);
    return true;
}

static bool fps_brush_snap_to_ground(const slayer3d_game_data_runtime *runtime, slayer3d_fps_mover *mover,
                                     unsigned int contents_mask, float walkable_normal_y,
                                     fps_brush_diagnostics *diagnostics, slayer3d_vec3 *body_center)
{
    if (runtime == NULL || mover == NULL || body_center == NULL)
        return false;

    const float probe_distance = SDL_max(mover->config.step_height, 0.05f) + 0.05f;
    const slayer3d_vec3 probe_end = slayer3d_vec3_make(body_center->x, body_center->y - probe_distance, body_center->z);
    slayer3d_game_data_brush_trace_result probe;
    if (!fps_brush_trace_body(runtime, mover, contents_mask, *body_center, probe_end, &probe))
        return false;
    if (!probe.hit || probe.start_solid || probe.normal.y < walkable_normal_y)
        return false;

    *body_center = slayer3d_vec3_add(probe.end_position, slayer3d_vec3_scale(probe.normal, 0.01f));
    mover->vertical_velocity = 0.0f;
    mover->on_ground = true;
    fps_brush_record_floor(diagnostics, &probe);
    return true;
}

static bool fps_brush_try_step_move(const slayer3d_game_data_runtime *runtime, slayer3d_fps_mover *mover,
                                    unsigned int contents_mask, float walkable_normal_y,
                                    fps_brush_diagnostics *diagnostics, slayer3d_vec3 start, slayer3d_vec3 end,
                                    slayer3d_vec3 *out_position)
{
    if (runtime == NULL || mover == NULL || out_position == NULL || mover->config.step_height <= 0.0f)
        return false;

    const float step_height = mover->config.step_height;
    const slayer3d_vec3 raised_start = slayer3d_vec3_make(start.x, start.y + step_height, start.z);
    const slayer3d_vec3 raised_end = slayer3d_vec3_make(end.x, end.y + step_height, end.z);
    slayer3d_game_data_brush_trace_result up_trace;
    if (!fps_brush_trace_body(runtime, mover, contents_mask, start, raised_start, &up_trace) || up_trace.hit)
        return false;

    slayer3d_game_data_brush_trace_result slide_trace;
    if (!fps_brush_slide_body(runtime, mover, contents_mask, raised_start, raised_end, &slide_trace) ||
        slide_trace.start_solid)
    {
        fps_brush_record_collision(diagnostics, &slide_trace, walkable_normal_y);
        return false;
    }
    fps_brush_record_collision(diagnostics, &slide_trace, walkable_normal_y);

    slayer3d_game_data_brush_trace_result down_trace;
    const slayer3d_vec3 down_end = slayer3d_vec3_make(
        slide_trace.end_position.x, slide_trace.end_position.y - step_height - 0.05f, slide_trace.end_position.z);
    if (!fps_brush_trace_body(runtime, mover, contents_mask, slide_trace.end_position, down_end, &down_trace) ||
        !down_trace.hit || down_trace.start_solid || down_trace.normal.y < walkable_normal_y)
    {
        return false;
    }

    *out_position = slayer3d_vec3_add(down_trace.end_position, slayer3d_vec3_scale(down_trace.normal, 0.01f));
    mover->on_ground = true;
    mover->vertical_velocity = 0.0f;
    if (diagnostics != NULL)
        diagnostics->stepped_up = true;
    fps_brush_record_floor(diagnostics, &down_trace);
    return true;
}

void update_fps_brush_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                 slayer3d_registered_actor *actor, const slayer3d_input_manager *input, float dt)
{
    if (runtime == NULL || component == NULL || actor == NULL)
        return;
    if (find_brush_world_runtime(runtime, json_string(component, "brush_world", NULL)) == NULL)
        return;

    fps_controller_runtime *controller = find_or_add_fps_controller(runtime, actor->name, component);
    if (controller == NULL || !initialize_fps_controller_runtime(runtime, controller, component, actor))
        return;

    const int forward_action = fps_controller_action_id(runtime, component, "forward");
    const int back_action = fps_controller_action_id(runtime, component, "back");
    const int left_action = fps_controller_action_id(runtime, component, "left");
    const int right_action = fps_controller_action_id(runtime, component, "right");
    const int jump_action = fps_controller_action_id(runtime, component, "jump");

    slayer3d_fps_mover *mover = &controller->mover;
    const bool mouse_look = json_bool(component, "mouse_look", true);
    const float mouse_dx = mouse_look && input != NULL ? slayer3d_input_get_mouse_dx(input) : 0.0f;
    const float mouse_dy = mouse_look && input != NULL ? slayer3d_input_get_mouse_dy(input) : 0.0f;
    const float mouse_sensitivity = json_float(component, "mouse_sensitivity", 0.002f);
    mover->yaw += mouse_dx * mouse_sensitivity;
    mover->pitch = fps_brush_clampf(mover->pitch - mouse_dy * mouse_sensitivity, -1.4f, 1.4f);
    fps_brush_decay_view_smooth(mover, dt);

    const unsigned int contents_mask = fps_brush_contents_mask(component);
    const float walkable_normal_y = fps_brush_walkable_normal_y(component);
    fps_brush_diagnostics diagnostics;
    fps_brush_diagnostics_init(&diagnostics);
    slayer3d_vec3 body_center = fps_brush_eye_to_body_center(mover, mover->position);
    const unsigned int starting_liquid_contents = fps_brush_liquid_contents_for_body(runtime, mover, body_center);
    const bool in_liquid = starting_liquid_contents != 0u;
    if (in_liquid)
        mover->on_ground = false;
    if (!in_liquid && mover->on_ground)
        (void)fps_brush_snap_to_ground(runtime, mover, contents_mask, walkable_normal_y, &diagnostics, &body_center);
    const bool smooth_ground_height_change = mover->on_ground;
    const float smooth_start_eye_y = fps_brush_body_center_to_eye(mover, body_center).y;

    if (fps_controller_action_pressed(runtime, input, jump_action))
    {
        if (in_liquid)
        {
            const float swim_up_velocity = SDL_max(json_float(component, "swim_up_velocity", 3.0f), 0.0f);
            mover->vertical_velocity = SDL_max(mover->vertical_velocity, swim_up_velocity);
        }
        else
        {
            slayer3d_fps_mover_jump(mover);
        }
    }

    float forward = fps_controller_action_value(runtime, input, forward_action) -
                    fps_controller_action_value(runtime, input, back_action);
    float side = fps_controller_action_value(runtime, input, right_action) -
                 fps_controller_action_value(runtime, input, left_action);
    const float wish_len_sq = forward * forward + side * side;
    if (wish_len_sq > 1.0f)
    {
        const float inv_len = 1.0f / SDL_sqrtf(wish_len_sq);
        forward *= inv_len;
        side *= inv_len;
    }

    const float fwd_x = SDL_sinf(mover->yaw);
    const float fwd_z = -SDL_cosf(mover->yaw);
    const float right_x = SDL_cosf(mover->yaw);
    const float right_z = SDL_sinf(mover->yaw);
    const float move_speed_scale =
        in_liquid ? SDL_max(json_float(component, "swim_move_speed_scale", 0.55f), 0.0f) : 1.0f;
    const slayer3d_vec3 horizontal_delta = slayer3d_vec3_make(
        (fwd_x * forward + right_x * side) * mover->config.move_speed * move_speed_scale * SDL_max(dt, 0.0f), 0.0f,
        (fwd_z * forward + right_z * side) * mover->config.move_speed * move_speed_scale * SDL_max(dt, 0.0f));
    if (slayer3d_vec3_length_squared(horizontal_delta) > 0.0000001f)
    {
        const slayer3d_vec3 horizontal_end = slayer3d_vec3_add(body_center, horizontal_delta);
        slayer3d_game_data_brush_trace_result slide;
        if (fps_brush_slide_body(runtime, mover, contents_mask, body_center, horizontal_end, &slide))
        {
            if (mover->on_ground && slide.hit)
            {
                slayer3d_vec3 stepped;
                if (fps_brush_try_step_move(runtime, mover, contents_mask, walkable_normal_y, &diagnostics, body_center,
                                            horizontal_end, &stepped))
                {
                    body_center = stepped;
                }
                else
                {
                    fps_brush_record_collision(&diagnostics, &slide, walkable_normal_y);
                    body_center = slayer3d_vec3_add(slide.end_position, slayer3d_vec3_scale(slide.normal, 0.01f));
                }
            }
            else
            {
                fps_brush_record_collision(&diagnostics, &slide, walkable_normal_y);
                body_center = slide.hit
                                  ? slayer3d_vec3_add(slide.end_position, slayer3d_vec3_scale(slide.normal, 0.01f))
                                  : slide.end_position;
            }
        }
    }

    const bool allow_ground_snap = !in_liquid && mover->on_ground && mover->vertical_velocity <= 0.0f;
    const float gravity_scale =
        in_liquid ? SDL_clamp(json_float(component, "swim_gravity_scale", 0.25f), 0.0f, 1.0f) : 1.0f;
    mover->vertical_velocity -= mover->config.gravity * gravity_scale * SDL_max(dt, 0.0f);
    if (in_liquid)
    {
        const float max_sink_speed = SDL_max(json_float(component, "swim_max_sink_speed", 2.0f), 0.0f);
        mover->vertical_velocity = SDL_max(mover->vertical_velocity, -max_sink_speed);
    }
    const slayer3d_vec3 vertical_end =
        slayer3d_vec3_make(body_center.x, body_center.y + mover->vertical_velocity * SDL_max(dt, 0.0f), body_center.z);
    slayer3d_game_data_brush_trace_result vertical;
    mover->on_ground = false;
    if (fps_brush_trace_body(runtime, mover, contents_mask, body_center, vertical_end, &vertical))
    {
        body_center = vertical.end_position;
        if (vertical.hit)
        {
            if (mover->vertical_velocity < 0.0f && vertical.normal.y >= walkable_normal_y)
            {
                body_center = slayer3d_vec3_add(body_center, slayer3d_vec3_scale(vertical.normal, 0.01f));
                mover->on_ground = true;
                fps_brush_record_floor(&diagnostics, &vertical);
            }
            else
            {
                fps_brush_record_collision(&diagnostics, &vertical, walkable_normal_y);
            }
            mover->vertical_velocity = 0.0f;
        }
    }
    else
    {
        body_center = vertical_end;
    }

    if (!mover->on_ground && allow_ground_snap)
        (void)fps_brush_snap_to_ground(runtime, mover, contents_mask, walkable_normal_y, &diagnostics, &body_center);

    mover->position = fps_brush_body_center_to_eye(mover, body_center);
    const unsigned int final_liquid_contents = fps_brush_liquid_contents_for_body(runtime, mover, body_center);
    const float liquid_damage = (final_liquid_contents & SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA) != 0u
                                    ? SDL_max(json_float(component, "liquid_damage_per_second", 18.0f), 0.0f)
                                    : 0.0f;
    slayer3d_properties_set_bool(actor->props, json_string(component, "in_liquid_property", "in_liquid"),
                                 final_liquid_contents != 0u);
    slayer3d_properties_set_int(actor->props, json_string(component, "liquid_contents_property", "liquid_contents"),
                                (int)SDL_min(final_liquid_contents, (unsigned int)SDL_MAX_SINT32));
    slayer3d_properties_set_float(
        actor->props, json_string(component, "liquid_damage_property", "last_damage_per_second"), liquid_damage);
    resolve_fps_controller_sector_doors(runtime, controller);
    if (smooth_ground_height_change && mover->on_ground)
    {
        const float height_delta = mover->position.y - smooth_start_eye_y;
        if (SDL_fabsf(height_delta) > FPS_BRUSH_VIEW_SMOOTH_EPSILON &&
            SDL_fabsf(height_delta) <= mover->config.step_height + 0.05f)
        {
            mover->view_smooth = fps_brush_clampf(mover->view_smooth - height_delta, -mover->config.step_height,
                                                  mover->config.step_height);
        }
    }
    mover->current_sector = -1;
    if (mover->on_ground)
    {
        mover->last_good_position = mover->position;
        mover->has_last_good = true;
    }
    fps_controller_publish_actor_state(controller, component, actor);
    fps_brush_publish_diagnostics(&diagnostics, component, actor);
}

static slayer3d_game_data_brush_trace_shape brush_velocity_shape_from_string(const char *shape)
{
    if (SDL_strcmp(shape != NULL ? shape : "", "sphere") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE;
    if (SDL_strcmp(shape != NULL ? shape : "", "aabb") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB;
    return SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
}

static slayer3d_vec3 brush_velocity_extents(yyjson_val *component, const slayer3d_registered_actor *actor,
                                            slayer3d_game_data_brush_trace_shape shape)
{
    if (shape == SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE)
    {
        const float radius =
            SDL_max(json_float(component, "radius", actor_numeric_property(actor, "radius", 0.0f)), 0.0f);
        return slayer3d_vec3_make(radius, 0.0f, 0.0f);
    }
    if (shape == SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB)
    {
        const char *extents_property = json_string(component, "extents_property", "extents");
        return json_vec3_value(obj_get(component, "extents"), actor_vec_property(actor, extents_property));
    }
    return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static unsigned int brush_velocity_contents_mask(yyjson_val *component)
{
    return brush_flags_from_json(obj_get(component, "contents_mask"), brush_content_flag_from_string,
                                 SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID |
                                     SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP);
}

static void brush_velocity_publish_impact_diagnostics(const slayer3d_game_data_brush_trace_result *result,
                                                      yyjson_val *component, slayer3d_registered_actor *actor)
{
    if (result == NULL || component == NULL || actor == NULL)
        return;
    slayer3d_properties_set_string(actor->props,
                                   json_string(component, "last_impact_brush_property", "last_impact_brush"),
                                   result->brush_name != NULL ? result->brush_name : "");
    slayer3d_properties_set_string(actor->props,
                                   json_string(component, "last_impact_world_property", "last_impact_world"),
                                   result->world_name != NULL ? result->world_name : "");
    slayer3d_properties_set_string(actor->props,
                                   json_string(component, "last_impact_material_property", "last_impact_material"),
                                   result->material_name != NULL ? result->material_name : "");
    slayer3d_properties_set_vec3(actor->props,
                                 json_string(component, "last_impact_position_property", "last_impact_position"),
                                 result->end_position);
    slayer3d_properties_set_vec3(
        actor->props, json_string(component, "last_impact_normal_property", "last_impact_normal"), result->normal);
    slayer3d_properties_set_int(actor->props,
                                json_string(component, "last_impact_contents_property", "last_impact_contents"),
                                (int)SDL_min(result->contents, (unsigned int)SDL_MAX_SINT32));
    slayer3d_properties_set_int(
        actor->props, json_string(component, "last_impact_surface_flags_property", "last_impact_surface_flags"),
        (int)SDL_min(result->surface_flags, (unsigned int)SDL_MAX_SINT32));
}

static slayer3d_properties *brush_velocity_impact_payload(const slayer3d_registered_actor *actor, slayer3d_vec3 start,
                                                          slayer3d_vec3 end, slayer3d_vec3 velocity,
                                                          const slayer3d_game_data_brush_trace_result *result)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    slayer3d_properties_set_string(payload, "actor_name", actor != NULL && actor->name != NULL ? actor->name : "");
    slayer3d_properties_set_string(payload, "moving_actor_name",
                                   actor != NULL && actor->name != NULL ? actor->name : "");
    slayer3d_properties_set_vec3(payload, "trace_start", start);
    slayer3d_properties_set_vec3(payload, "trace_end", end);
    slayer3d_properties_set_vec3(payload, "velocity", velocity);
    slayer3d_properties_set_bool(payload, "hit_brush", result != NULL && result->hit);
    slayer3d_properties_set_string(payload, "hit_brush_world",
                                   result != NULL && result->world_name != NULL ? result->world_name : "");
    slayer3d_properties_set_string(payload, "hit_brush_name",
                                   result != NULL && result->brush_name != NULL ? result->brush_name : "");
    slayer3d_properties_set_string(payload, "hit_material",
                                   result != NULL && result->material_name != NULL ? result->material_name : "");
    slayer3d_properties_set_vec3(payload, "hit_position",
                                 result != NULL ? result->end_position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(payload, "hit_normal",
                                 result != NULL ? result->normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_float(payload, "hit_fraction", result != NULL ? result->fraction : 1.0f);
    slayer3d_properties_set_float(payload, "hit_distance",
                                  result != NULL ? slayer3d_vec3_length(slayer3d_vec3_sub(result->end_position, start))
                                                 : 0.0f);
    slayer3d_properties_set_int(payload, "hit_contents",
                                result != NULL ? (int)SDL_min(result->contents, (unsigned int)SDL_MAX_SINT32) : 0);
    slayer3d_properties_set_int(payload, "hit_surface_flags",
                                result != NULL ? (int)SDL_min(result->surface_flags, (unsigned int)SDL_MAX_SINT32) : 0);
    return payload;
}

bool update_brush_velocity_motion(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                  slayer3d_registered_actor *actor, int actor_id, int pool_index, int actor_index,
                                  float dt)
{
    const char *property = json_string(component, "property", "velocity");
    const slayer3d_vec3 velocity = actor_vec_property(actor, property);
    if (actor == NULL || slayer3d_vec3_length_squared(velocity) <= 0.000001f)
        return true;

    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.start = actor->position;
    trace.end = slayer3d_vec3_make(actor->position.x + velocity.x * dt, actor->position.y + velocity.y * dt,
                                   actor->position.z + velocity.z * dt);
    trace.shape = brush_velocity_shape_from_string(json_string(component, "shape", "point"));
    trace.extents = brush_velocity_extents(component, actor, trace.shape);
    trace.contents_mask = brush_velocity_contents_mask(component);

    slayer3d_game_data_brush_trace_result result;
    const bool traced = json_bool(component, "slide", false)
                            ? slayer3d_game_data_slide_active_brush_worlds(runtime, &trace, 4, &result)
                            : slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, &result);
    if (!traced)
        return false;

    actor_set_position(actor, result.end_position);
    if (!result.hit)
        return true;

    brush_velocity_publish_impact_diagnostics(&result, component, actor);
    slayer3d_properties *payload = brush_velocity_impact_payload(actor, trace.start, trace.end, velocity, &result);
    const bool ok = execute_optional_action_array(runtime, obj_get(component, "impact_actions"), payload);
    emit_optional_signal(runtime, component, "on_impact", payload);
    slayer3d_properties_destroy(payload);
    if (!ok)
        return false;

    if (json_bool(component, "despawn_on_hit", true))
    {
        if (actor_id > 0)
            (void)actor_pool_request_despawn(runtime, &runtime->actor_pools[pool_index], actor, actor_index,
                                             json_string(component, "reason", "brush impact"));
        else
            actor->active = false;
    }
    return true;
}

void update_sector_doors(slayer3d_game_data_runtime *runtime, float dt)
{
    for (int i = 0; runtime != NULL && i < runtime->sector_door_count; ++i)
    {
        if (sector_door_in_active_scene(runtime, &runtime->sector_doors[i]))
            slayer3d_door_update(&runtime->sector_doors[i].door, dt);
    }
}

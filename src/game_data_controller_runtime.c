/* Controller, FPS movement, brush movement, and sector door update helpers. */

#include "game_data_internal.h"

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
        const float yaw = SDL_atan2f(result.movement_delta.x, -result.movement_delta.z);
        slayer3d_properties_set_float(actor->props, json_string(component, "yaw_property", "yaw"), yaw);
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

    const unsigned int contents_mask = fps_brush_contents_mask(component);
    const float walkable_normal_y = fps_brush_walkable_normal_y(component);
    fps_brush_diagnostics diagnostics;
    fps_brush_diagnostics_init(&diagnostics);
    slayer3d_vec3 body_center = fps_brush_eye_to_body_center(mover, mover->position);
    if (mover->on_ground)
        (void)fps_brush_snap_to_ground(runtime, mover, contents_mask, walkable_normal_y, &diagnostics, &body_center);

    if (fps_controller_action_pressed(runtime, input, jump_action))
        slayer3d_fps_mover_jump(mover);

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
    const slayer3d_vec3 horizontal_delta =
        slayer3d_vec3_make((fwd_x * forward + right_x * side) * mover->config.move_speed * SDL_max(dt, 0.0f), 0.0f,
                           (fwd_z * forward + right_z * side) * mover->config.move_speed * SDL_max(dt, 0.0f));
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

    const bool allow_ground_snap = mover->on_ground && mover->vertical_velocity <= 0.0f;
    mover->vertical_velocity -= mover->config.gravity * SDL_max(dt, 0.0f);
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

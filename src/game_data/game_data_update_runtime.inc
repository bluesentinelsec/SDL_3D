/* Controller, motion, sensor, and schedule update helpers.
 * Included by src/game_data.c to keep private runtime helpers in one translation unit. */

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

static void update_patrol_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                     slayer3d_registered_actor *actor, float dt)
{
    patrol_controller_runtime *controller =
        actor != NULL ? find_or_add_patrol_controller(runtime, actor->name, component) : NULL;
    if (controller == NULL)
        return;

    if (!controller->initialized && !initialize_patrol_controller(runtime, controller, component, actor))
        return;

    slayer3d_actor_patrol_result result = slayer3d_actor_patrol_controller_update(
        &controller->controller, runtime_registry(runtime), runtime_bus(runtime), dt, NULL, NULL);
    actor_set_position(actor, result.position);
    if (result.moved)
    {
        const float yaw = SDL_atan2f(result.movement_delta.x, -result.movement_delta.z);
        slayer3d_properties_set_float(actor->props, json_string(component, "yaw_property", "yaw"), yaw);
    }
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

static bool execute_fps_controller_launch_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
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

static bool execute_fps_controller_teleport_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
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

static float sector_door_distance_sq_xz(const slayer3d_door *door, slayer3d_vec3 point)
{
    if (door == NULL || door->panel_count <= 0)
        return 0.0f;
    const slayer3d_vec3 center = sector_door_closed_center(door);
    const float dx = center.x - point.x;
    const float dz = center.z - point.z;
    return dx * dx + dz * dz;
}

static bool sector_door_is_in_front(const slayer3d_door *door, slayer3d_vec3 point, float yaw, float min_dot)
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

static void update_fps_sector_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                         slayer3d_registered_actor *actor, const slayer3d_input_manager *input,
                                         float dt)
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

static void update_fps_brush_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
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

static bool update_brush_velocity_motion(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                         slayer3d_registered_actor *actor, int actor_id, int pool_index,
                                         int actor_index, float dt)
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

static void update_sector_doors(slayer3d_game_data_runtime *runtime, float dt)
{
    for (int i = 0; runtime != NULL && i < runtime->sector_door_count; ++i)
    {
        if (sector_door_in_active_scene(runtime, &runtime->sector_doors[i]))
            slayer3d_door_update(&runtime->sector_doors[i].door, dt);
    }
}

static slayer3d_properties *sector_platform_crush_payload(const sector_platform_runtime *platform,
                                                          const slayer3d_registered_actor *actor, float floor_y,
                                                          float floor_delta, float damage)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    slayer3d_properties_set_string(payload, "actor_name", actor != NULL && actor->name != NULL ? actor->name : "");
    slayer3d_properties_set_string(payload, "target_actor_name",
                                   actor != NULL && actor->name != NULL ? actor->name : "");
    slayer3d_properties_set_string(payload, "sector_platform",
                                   platform != NULL && platform->name != NULL ? platform->name : "");
    slayer3d_properties_set_string(
        payload, "sector_level",
        platform != NULL && platform->level != NULL && platform->level->name != NULL ? platform->level->name : "");
    slayer3d_properties_set_int(payload, "sector_index", platform != NULL ? platform->sector_index : -1);
    slayer3d_properties_set_float(payload, "sector_platform_floor_y", floor_y);
    slayer3d_properties_set_float(payload, "sector_platform_floor_delta", floor_delta);
    slayer3d_properties_set_float(payload, "sector_platform_crush_damage", damage);
    slayer3d_properties_set_float(payload, "amount", damage);
    return payload;
}

static bool sector_platform_apply_crush_policy(slayer3d_game_data_runtime *runtime, sector_platform_runtime *platform,
                                               float floor_y, float previous_floor_y, float dt)
{
    const float damage_per_second = SDL_max(json_float(platform->json, "crush_damage_per_second", 0.0f), 0.0f);
    yyjson_val *actions = obj_get(platform->json, "crush_actions");
    const int signal_id = action_signal_id(runtime, platform->json, "on_crush");
    if (damage_per_second <= 0.0f && !yyjson_is_arr(actions) && signal_id < 0)
        return true;

    const float floor_delta = floor_y - previous_floor_y;
    if (floor_delta <= 0.0f && !json_bool(platform->json, "crush_when_descending", false))
        return true;

    const char *tag = json_string(platform->json, "crush_actor_tag", json_string(platform->json, "actor_tag", NULL));
    sensor_actor_list actors;
    SDL_zero(actors);
    if (!collect_effect_targets(runtime, tag, &actors))
    {
        sensor_actor_list_free(&actors);
        return false;
    }

    bool ok = true;
    const float clearance = SDL_max(json_float(platform->json, "crush_clearance", 1.8f), 0.0f);
    const float damage = damage_per_second * SDL_max(dt, 0.0f);
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    for (int i = 0; i < actors.count; ++i)
    {
        slayer3d_registered_actor *actor = actors.items[i];
        if (actor_sector_index_for_sensor(platform->level, NULL, actor) != platform->sector_index)
            continue;
        if (actor->position.y > floor_y + clearance)
            continue;

        slayer3d_properties *payload = sector_platform_crush_payload(platform, actor, floor_y, floor_delta, damage);
        if (!actor_matches_target_filter(runtime, actor, NULL, platform->json, payload, tag, false))
        {
            slayer3d_properties_destroy(payload);
            continue;
        }
        if (damage > 0.0f)
            ok = apply_combat_damage_to_actor(runtime, platform->json, payload, actor, damage) && ok;
        ok = execute_optional_action_array(runtime, actions, payload) && ok;
        if (bus != NULL && signal_id >= 0)
            slayer3d_signal_emit(bus, signal_id, payload);
        slayer3d_properties_destroy(payload);
    }
    sensor_actor_list_free(&actors);
    return ok;
}

static bool update_sector_platforms(slayer3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL)
        return false;
    if (dt < 0.0f)
        dt = 0.0f;

    for (int i = 0; i < runtime->sector_platform_count; ++i)
    {
        sector_platform_runtime *platform = &runtime->sector_platforms[i];
        if (!platform->enabled || platform->cycle_seconds <= 0.0f ||
            !sector_platform_in_active_scene(runtime, platform))
            continue;

        platform->time += dt;
        while (platform->time >= platform->cycle_seconds)
            platform->time -= platform->cycle_seconds;

        const float phase = platform->time / platform->cycle_seconds;
        const float wave = (SDL_sinf(phase * SDL_PI_F * 2.0f - SDL_PI_F * 0.5f) + 1.0f) * 0.5f;
        const float floor_y = platform->min_floor_y + (platform->max_floor_y - platform->min_floor_y) * wave;
        const float previous_floor_y = platform->last_floor_y;
        const bool should_rebuild =
            !platform->has_last_floor_y || SDL_fabsf(floor_y - platform->last_floor_y) >= platform->rebuild_min_delta;

        if (should_rebuild)
        {
            slayer3d_sector_geometry geometry;
            SDL_zero(geometry);
            geometry.floor_y = floor_y;
            geometry.ceil_y = platform->ceil_y;
            geometry.floor_normal[1] = 1.0f;
            geometry.ceil_normal[1] = -1.0f;

            char error[256];
            if (!set_sector_level_geometry(platform->level, platform->sector_index, &geometry, error,
                                           (int)sizeof(error)))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D sector platform '%s' update failed: %s",
                            platform->name != NULL ? platform->name : "<unnamed>", error);
                return false;
            }
            platform->last_floor_y = floor_y;
            platform->has_last_floor_y = true;
        }
        if (!sector_platform_apply_crush_policy(runtime, platform, floor_y, previous_floor_y, dt))
            return false;
    }
    return true;
}

static void update_pickup_respawn_component(yyjson_val *component, slayer3d_registered_actor *actor, float dt)
{
    if (component == NULL || actor == NULL || actor->active)
        return;

    const char *timer_property = json_string(component, "timer_property", "pickup_respawn_remaining");
    const char *available_property = json_string(component, "available_property", "pickup_available");
    const float remaining = slayer3d_properties_get_float(actor->props, timer_property, 0.0f);
    if (remaining <= 0.0f)
        return;

    const float next = SDL_max(remaining - dt, 0.0f);
    slayer3d_properties_set_float(actor->props, timer_property, next);
    if (next <= 0.0f)
    {
        actor->active = true;
        slayer3d_properties_set_bool(actor->props, available_property, true);
    }
}

static void update_status_effect_timer_component(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                                 slayer3d_registered_actor *actor, float dt)
{
    if (runtime == NULL || component == NULL || actor == NULL || !actor->active)
        return;

    const char *property = json_string(component, "property", NULL);
    const char *duration_property = json_string(component, "duration_property", NULL);
    char duration_buffer[128];
    if (duration_property == NULL && property != NULL)
    {
        SDL_snprintf(duration_buffer, sizeof(duration_buffer), "%s_remaining", property);
        duration_property = duration_buffer;
    }
    if (property == NULL || duration_property == NULL)
        return;

    const float remaining = slayer3d_properties_get_float(actor->props, duration_property, 0.0f);
    if (remaining <= 0.0f)
        return;

    const float next = SDL_max(remaining - dt, 0.0f);
    slayer3d_properties_set_float(actor->props, duration_property, next);
    if (next > 0.0f)
        return;

    slayer3d_value expired;
    if (json_scalar_to_value(obj_get(component, "expired_value"), &expired))
        (void)set_property_from_value(actor->props, property, &expired);
    const char *active_property = json_string(component, "active_property", NULL);
    if (active_property != NULL && active_property[0] != '\0')
        slayer3d_properties_set_bool(actor->props, active_property, false);

    const int signal_id = action_signal_id(runtime, component, "on_expire");
    if (signal_id >= 0 && runtime_bus(runtime) != NULL)
    {
        slayer3d_properties *payload = slayer3d_properties_create();
        if (payload != NULL)
        {
            slayer3d_properties_set_string(payload, "actor_name", actor->name != NULL ? actor->name : "");
            slayer3d_properties_set_string(payload, "property", property);
        }
        slayer3d_signal_emit(runtime_bus(runtime), signal_id, payload);
        slayer3d_properties_destroy(payload);
    }
}

static void update_weapon_state_component(yyjson_val *component, slayer3d_registered_actor *actor, float dt)
{
    if (component == NULL || actor == NULL || !actor->active)
        return;

    const char *cooldown_property = json_string(component, "cooldown_property", NULL);
    if (cooldown_property != NULL && cooldown_property[0] != '\0')
    {
        const float cooldown_rate = json_float(component, "cooldown_rate", 1.0f);
        const float cooldown = slayer3d_properties_get_float(actor->props, cooldown_property, 0.0f);
        if (cooldown > 0.0f && cooldown_rate > 0.0f)
            slayer3d_properties_set_float(actor->props, cooldown_property,
                                          SDL_max(cooldown - cooldown_rate * dt, 0.0f));
    }

    const char *timer_property = json_string(component, "reload_timer_property", "reload_timer");
    const char *pending_property = json_string(component, "reload_pending_property", "reload_pending");
    if (!slayer3d_properties_get_bool(actor->props, pending_property, false))
        return;

    const float timer = slayer3d_properties_get_float(actor->props, timer_property, 0.0f);
    if (timer > 0.0f)
    {
        const float next = SDL_max(timer - dt, 0.0f);
        slayer3d_properties_set_float(actor->props, timer_property, next);
        if (next > 0.0f)
            return;
    }
    weapon_complete_reload(actor, component);
}

static void update_interactable_component(yyjson_val *component, slayer3d_registered_actor *actor, float dt)
{
    if (component == NULL || actor == NULL || !actor->active)
        return;
    const char *cooldown_property = json_string(component, "cooldown_property", NULL);
    if (cooldown_property == NULL || cooldown_property[0] == '\0')
        return;
    const float cooldown = slayer3d_properties_get_float(actor->props, cooldown_property, 0.0f);
    if (cooldown > 0.0f)
        slayer3d_properties_set_float(actor->props, cooldown_property, SDL_max(cooldown - dt, 0.0f));
}

static void update_control_components(slayer3d_game_data_runtime *runtime, yyjson_val *root, float dt)
{
    slayer3d_input_manager *input = runtime_input(runtime);
    yyjson_val *entities = obj_get(root, "entities");
    yyjson_val *world_bounds = obj_get(obj_get(root, "world"), "bounds");
    const slayer3d_vec3 world_min =
        json_vec3(world_bounds, "min", slayer3d_vec3_make(-100000.0f, -100000.0f, -100000.0f));
    const slayer3d_vec3 world_max = json_vec3(world_bounds, "max", slayer3d_vec3_make(100000.0f, 100000.0f, 100000.0f));

    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        yyjson_val *components = obj_get(entity, "components");
        if (actor == NULL || !actor->active || !yyjson_is_arr(components))
            continue;

        for (size_t c = 0; c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type", "");
            yyjson_val *enabled_if = obj_get(component, "enabled_if");
            if (enabled_if != NULL && !eval_data_condition(runtime, enabled_if, NULL))
            {
                continue;
            }
            if (SDL_strcmp(type, "control.axis_1d") == 0 && input != NULL)
            {
                const int negative = slayer3d_game_data_find_action(runtime, json_string(component, "negative", NULL));
                const int positive = slayer3d_game_data_find_action(runtime, json_string(component, "positive", NULL));
                if ((negative >= 0 && !slayer3d_game_data_active_scene_allows_action(runtime, negative)) ||
                    (positive >= 0 && !slayer3d_game_data_active_scene_allows_action(runtime, positive)))
                {
                    continue;
                }
                const int axis = axis_index(json_string(component, "axis", NULL));
                const float value =
                    slayer3d_input_get_value(input, positive) - slayer3d_input_get_value(input, negative);
                const float speed = slayer3d_properties_get_float(actor->props, "speed", 0.0f);
                const float half_height = slayer3d_properties_get_float(actor->props, "half_height", 0.0f);
                const float lo = vec_axis(world_min, axis) + half_height;
                const float hi = vec_axis(world_max, axis) - half_height;
                slayer3d_vec3 position = actor->position;
                set_vec_axis(&position, axis, SDL_clamp(vec_axis(position, axis) + value * speed * dt, lo, hi));
                actor_set_position(actor, position);
            }
            else if (SDL_strcmp(type, "adapter.controller") == 0)
            {
                adapter_entry *adapter = find_adapter(runtime, json_string(component, "adapter", NULL));
                if (adapter != NULL)
                {
                    slayer3d_properties *payload = slayer3d_properties_create();
                    if (payload != NULL)
                    {
                        slayer3d_properties_set_string(payload, "target_actor_name",
                                                       json_string(component, "target", ""));
                    }
                    invoke_adapter(runtime, adapter, actor, payload);
                    slayer3d_properties_destroy(payload);
                }
            }
            else if (SDL_strcmp(type, "controller.fps_sector") == 0)
            {
                update_fps_sector_controller(runtime, component, actor, input, dt);
            }
            else if (SDL_strcmp(type, "controller.fps_brush") == 0)
            {
                update_fps_brush_controller(runtime, component, actor, input, dt);
            }
            else if (SDL_strcmp(type, "weapon.projectile") == 0 && input != NULL)
            {
                const int action_id = slayer3d_game_data_find_action(runtime, json_string(component, "action", NULL));
                if (action_id >= 0 && slayer3d_game_data_active_scene_allows_action(runtime, action_id) &&
                    slayer3d_input_is_held(input, action_id))
                {
                    (void)execute_projectile_fire_action_for_actor(runtime, component, NULL, actor);
                }
            }
        }
    }
}

static void update_motion_components(slayer3d_game_data_runtime *runtime, yyjson_val *root, float dt)
{
    (void)root;
    for (int actor_id = 0; actor_id < runtime->actor_pool_count + 1; ++actor_id)
    {
        yyjson_val *entities = actor_id == 0 ? obj_get(runtime_root(runtime), "entities") : NULL;
        const int entity_count = actor_id == 0 && yyjson_is_arr(entities) ? (int)yyjson_arr_size(entities) : 0;
        const int pool_index = actor_id - 1;
        const int count = actor_id == 0 ? entity_count : runtime->actor_pools[pool_index].capacity;

        if (actor_id > 0 &&
            !actor_pool_in_scene(&runtime->actor_pools[pool_index], slayer3d_game_data_active_scene(runtime)))
            continue;

        for (int i = 0; i < count; ++i)
        {
            yyjson_val *entity =
                actor_id == 0 ? yyjson_arr_get(entities, (size_t)i) : runtime->actor_pools[pool_index].archetype_json;
            const char *entity_name =
                actor_id == 0 ? json_string(entity, "name", NULL) : runtime->actor_pools[pool_index].actor_names[i];
            if (!active_scene_has_entity_internal(runtime, entity_name))
                continue;
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
            yyjson_val *components = obj_get(entity, "components");
            if (actor == NULL || !yyjson_is_arr(components))
                continue;

            for (size_t c = 0; c < yyjson_arr_size(components); ++c)
            {
                yyjson_val *component = yyjson_arr_get(components, c);
                if (SDL_strcmp(json_string(component, "type", ""), "pickup.respawn") == 0)
                    update_pickup_respawn_component(component, actor, dt);
            }

            if (!runtime_actor_is_active(runtime, actor))
                continue;
            for (size_t c = 0; c < yyjson_arr_size(components); ++c)
            {
                yyjson_val *component = yyjson_arr_get(components, c);
                if (SDL_strcmp(json_string(component, "type", ""), "weapon.state") == 0)
                    update_weapon_state_component(component, actor, dt);
                else if (SDL_strcmp(json_string(component, "type", ""), "interactable") == 0)
                    update_interactable_component(component, actor, dt);
            }
            if (!slayer3d_properties_get_bool(actor->props, "active_motion", true))
            {
                continue;
            }

            for (size_t c = 0; c < yyjson_arr_size(components); ++c)
            {
                yyjson_val *component = yyjson_arr_get(components, c);
                const char *type = json_string(component, "type", "");

                if (SDL_strcmp(type, "motion.velocity_2d") == 0)
                {
                    const char *property = json_string(component, "property", "velocity");
                    const slayer3d_vec3 velocity = actor_vec_property(actor, property);
                    actor_set_position(actor,
                                       slayer3d_vec3_make(actor->position.x + velocity.x * dt,
                                                          actor->position.y + velocity.y * dt, actor->position.z));
                }
                else if (SDL_strcmp(type, "motion.velocity_3d") == 0)
                {
                    const char *property = json_string(component, "property", "velocity");
                    const slayer3d_vec3 velocity = actor_vec_property(actor, property);
                    actor_set_position(actor, slayer3d_vec3_make(actor->position.x + velocity.x * dt,
                                                                 actor->position.y + velocity.y * dt,
                                                                 actor->position.z + velocity.z * dt));
                }
                else if (SDL_strcmp(type, "motion.sector_velocity_3d") == 0)
                {
                    const sector_level_runtime *level =
                        find_sector_level_runtime(runtime, json_string(component, "sector_level", NULL));
                    const char *property = json_string(component, "property", "velocity");
                    const slayer3d_vec3 velocity = actor_vec_property(actor, property);
                    const float speed = slayer3d_vec3_length(velocity);
                    if (level == NULL || speed <= 0.000001f)
                        continue;

                    const slayer3d_vec3 direction = slayer3d_vec3_scale(velocity, 1.0f / speed);
                    const slayer3d_level_trace_result trace = slayer3d_level_trace_point(
                        &level->lightmapped, level->sectors, actor->position, direction, speed * dt);
                    actor_set_position(actor, trace.end_point);
                    if (trace.hit && json_bool(component, "despawn_on_hit", true))
                    {
                        if (actor_id > 0)
                            (void)actor_pool_request_despawn(runtime, &runtime->actor_pools[pool_index], actor, i,
                                                             json_string(component, "reason", "sector impact"));
                        else
                            actor->active = false;
                        break;
                    }
                }
                else if (SDL_strcmp(type, "motion.brush_velocity_3d") == 0)
                {
                    if (!update_brush_velocity_motion(runtime, component, actor, actor_id, pool_index, i, dt))
                        break;
                    if (!runtime_actor_is_active(runtime, actor))
                        break;
                }
                else if (SDL_strcmp(type, "motion.scroll_wrap") == 0)
                {
                    const int axis = axis_index(json_string(component, "axis", "x"));
                    const float speed = json_float(component, "speed", 0.0f);
                    const float min_value = json_float(component, "min", -10.0f);
                    const float max_value = json_float(component, "max", 10.0f);
                    slayer3d_vec3 position = actor->position;
                    float value = vec_axis(position, axis) + speed * dt;
                    if (speed < 0.0f && value < min_value)
                        value = max_value;
                    else if (speed > 0.0f && value > max_value)
                        value = min_value;
                    set_vec_axis(&position, axis, value);
                    actor_set_position(actor, position);
                }
                else if (SDL_strcmp(type, "motion.patrol") == 0)
                {
                    update_patrol_controller(runtime, component, actor, dt);
                }
                else if (SDL_strcmp(type, "motion.grid_agent") == 0)
                {
                    const grid_map_runtime *map = find_grid_map(runtime, json_string(component, "map", NULL));
                    if (map == NULL)
                        continue;

                    int col = slayer3d_properties_get_int(actor->props, "grid_col", INT32_MIN);
                    int row = slayer3d_properties_get_int(actor->props, "grid_row", INT32_MIN);
                    if (col == INT32_MIN || row == INT32_MIN)
                    {
                        if (!grid_map_world_to_cell(map, actor->position.x, actor->position.y, &col, &row))
                            continue;
                        slayer3d_properties_set_int(actor->props, "grid_col", col);
                        slayer3d_properties_set_int(actor->props, "grid_row", row);
                    }

                    int target_col = slayer3d_properties_get_int(actor->props, "grid_target_col", -1);
                    int target_row = slayer3d_properties_get_int(actor->props, "grid_target_row", -1);
                    int from_col = slayer3d_properties_get_int(actor->props, "grid_from_col", col);
                    int from_row = slayer3d_properties_get_int(actor->props, "grid_from_row", row);
                    float progress = slayer3d_properties_get_float(actor->props, "grid_progress", 0.0f);
                    const bool has_target =
                        target_col >= 0 && target_row >= 0 && grid_map_normalize_cell(map, &target_col, &target_row);
                    if (!has_target || (target_col == col && target_row == row && progress <= 0.0f))
                    {
                        const int queued_dx =
                            SDL_clamp(slayer3d_properties_get_int(actor->props, "grid_next_dir_x", 0), -1, 1);
                        const int queued_dy =
                            SDL_clamp(slayer3d_properties_get_int(actor->props, "grid_next_dir_y", 0), -1, 1);
                        const int current_dx =
                            SDL_clamp(slayer3d_properties_get_int(actor->props, "grid_dir_x", 0), -1, 1);
                        const int current_dy =
                            SDL_clamp(slayer3d_properties_get_int(actor->props, "grid_dir_y", 0), -1, 1);
                        int chosen_dx = 0;
                        int chosen_dy = 0;
                        int next_col = col + queued_dx;
                        int next_row = row + queued_dy;
                        if ((queued_dx != 0 || queued_dy != 0) && grid_map_is_walkable(map, next_col, next_row))
                        {
                            chosen_dx = queued_dx;
                            chosen_dy = queued_dy;
                        }
                        else
                        {
                            next_col = col + current_dx;
                            next_row = row + current_dy;
                            if ((current_dx != 0 || current_dy != 0) && grid_map_is_walkable(map, next_col, next_row))
                            {
                                chosen_dx = current_dx;
                                chosen_dy = current_dy;
                            }
                        }

                        if (chosen_dx == 0 && chosen_dy == 0)
                        {
                            slayer3d_vec3 centered;
                            if (grid_map_cell_to_world(map, col, row, &centered))
                            {
                                centered.z = actor->position.z;
                                actor_set_position(actor, centered);
                            }
                            slayer3d_properties_set_float(actor->props, "grid_progress", 0.0f);
                            slayer3d_properties_set_int(actor->props, "grid_target_col", -1);
                            slayer3d_properties_set_int(actor->props, "grid_target_row", -1);
                            continue;
                        }

                        next_col = col + chosen_dx;
                        next_row = row + chosen_dy;
                        if (!grid_map_normalize_cell(map, &next_col, &next_row))
                            continue;
                        from_col = col;
                        from_row = row;
                        target_col = next_col;
                        target_row = next_row;
                        progress = 0.0f;
                        slayer3d_properties_set_int(actor->props, "grid_dir_x", chosen_dx);
                        slayer3d_properties_set_int(actor->props, "grid_dir_y", chosen_dy);
                        slayer3d_properties_set_int(actor->props, "grid_from_col", from_col);
                        slayer3d_properties_set_int(actor->props, "grid_from_row", from_row);
                        slayer3d_properties_set_int(actor->props, "grid_target_col", target_col);
                        slayer3d_properties_set_int(actor->props, "grid_target_row", target_row);
                    }

                    const float speed =
                        slayer3d_properties_get_float(actor->props, "grid_speed", json_float(component, "speed", 1.0f));
                    progress += SDL_max(speed, 0.0f) * dt;
                    if (progress >= 1.0f)
                    {
                        col = target_col;
                        row = target_row;
                        progress = 0.0f;
                        slayer3d_properties_set_int(actor->props, "grid_col", col);
                        slayer3d_properties_set_int(actor->props, "grid_row", row);
                        slayer3d_properties_set_float(actor->props, "grid_progress", 0.0f);
                        slayer3d_properties_set_int(actor->props, "grid_target_col", -1);
                        slayer3d_properties_set_int(actor->props, "grid_target_row", -1);
                        slayer3d_vec3 centered;
                        if (grid_map_cell_to_world(map, col, row, &centered))
                        {
                            centered.z = actor->position.z;
                            actor_set_position(actor, centered);
                        }
                    }
                    else
                    {
                        slayer3d_vec3 from_position;
                        slayer3d_vec3 target_position;
                        if (grid_map_cell_to_world(map, from_col, from_row, &from_position) &&
                            grid_map_cell_to_world(map, target_col, target_row, &target_position))
                        {
                            const float z = actor->position.z;
                            actor_set_position(
                                actor, slayer3d_vec3_make(
                                           from_position.x + (target_position.x - from_position.x) * progress,
                                           from_position.y + (target_position.y - from_position.y) * progress, z));
                        }
                        slayer3d_properties_set_float(actor->props, "grid_progress", progress);
                    }
                }
                else if (SDL_strcmp(type, "motion.oscillate") == 0)
                {
                    const char *time_property = json_string(component, "time_property", "motion_time");
                    const float time = slayer3d_properties_get_float(actor->props, time_property, 0.0f) + dt;
                    slayer3d_properties_set_float(actor->props, time_property, time);

                    const slayer3d_vec3 origin = json_vec3_value(obj_get(component, "origin"), actor->position);
                    const slayer3d_vec3 amplitude =
                        json_vec3_value(obj_get(component, "amplitude"), slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
                    const float rate = json_float(component, "rate", 1.0f);
                    const float phase = json_float(component, "phase", 0.0f);
                    const float wave = SDL_sinf(time * rate + phase);
                    actor_set_position(actor,
                                       slayer3d_vec3_make(origin.x + amplitude.x * wave, origin.y + amplitude.y * wave,
                                                          origin.z + amplitude.z * wave));
                }
                else if (SDL_strcmp(type, "motion.spin") == 0)
                {
                    const char *property = json_string(component, "property", "rotation_angle");
                    const float rate = json_float(component, "rate", 1.0f);
                    float angle = slayer3d_properties_get_float(actor->props, property, 0.0f) + rate * dt;
                    const float two_pi = 6.28318530717958647692f;
                    if (angle > two_pi || angle < -two_pi)
                        angle = SDL_fmodf(angle, two_pi);
                    slayer3d_properties_set_float(actor->props, property, angle);
                }
                else if (SDL_strcmp(type, "status_effect.timer") == 0)
                {
                    update_status_effect_timer_component(runtime, component, actor, dt);
                }
                else if (SDL_strcmp(type, "lifecycle.ttl") == 0)
                {
                    const char *age_property = json_string(component, "age_property", "age");
                    const char *ttl_property = json_string(component, "ttl_property", "ttl");
                    const float ttl = ttl_property != NULL && ttl_property[0] != '\0'
                                          ? slayer3d_properties_get_float(actor->props, ttl_property,
                                                                          json_float(component, "ttl", 0.0f))
                                          : json_float(component, "ttl", 0.0f);
                    const float age = slayer3d_properties_get_float(actor->props, age_property, 0.0f) + dt;
                    slayer3d_properties_set_float(actor->props, age_property, age);
                    if (ttl > 0.0f && age >= ttl)
                    {
                        if (actor_id > 0)
                            (void)actor_pool_request_despawn(runtime, &runtime->actor_pools[pool_index], actor, i,
                                                             json_string(component, "reason", "ttl"));
                        else
                            actor->active = false;
                        break;
                    }
                }
            }
        }
    }
}

static float move_float_toward(float value, float target, float max_delta)
{
    if (max_delta < 0.0f)
        max_delta = -max_delta;
    if (value < target)
        return SDL_min(value + max_delta, target);
    if (value > target)
        return SDL_max(value - max_delta, target);
    return target;
}

bool slayer3d_game_data_update_property_effects(slayer3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL || runtime->doc == NULL)
        return false;
    if (dt < 0.0f)
        dt = 0.0f;

    bool ok = true;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;

        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        yyjson_val *components = obj_get(entity, "components");
        if (actor == NULL || !actor->active || !yyjson_is_arr(components))
            continue;

        for (size_t c = 0; c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            if (SDL_strcmp(json_string(component, "type", ""), "property.decay") != 0)
                continue;

            const char *property = json_string(component, "property", NULL);
            const slayer3d_value *existing =
                property != NULL ? slayer3d_properties_get_value(actor->props, property) : NULL;
            if (property == NULL || existing == NULL ||
                (existing->type != SLAYER3D_VALUE_FLOAT && existing->type != SLAYER3D_VALUE_INT))
            {
                ok = false;
                continue;
            }

            const char *rate_property = json_string(component, "rate_property", NULL);
            const float rate = rate_property != NULL ? slayer3d_properties_get_float(actor->props, rate_property, 0.0f)
                                                     : json_float(component, "rate", 1.0f);
            const float target = json_float(component, "target", 0.0f);
            float value = existing->type == SLAYER3D_VALUE_FLOAT ? existing->as_float : (float)existing->as_int;
            value = move_float_toward(value, target, rate * dt);
            if (obj_get(component, "min") != NULL)
                value = SDL_max(value, json_float(component, "min", value));
            if (obj_get(component, "max") != NULL)
                value = SDL_min(value, json_float(component, "max", value));

            if (existing->type == SLAYER3D_VALUE_INT)
                slayer3d_properties_set_int(actor->props, property, (int)SDL_lroundf(value));
            else
                slayer3d_properties_set_float(actor->props, property, value);
        }
    }
    return ok;
}

static void emit_sensor_signal(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                               slayer3d_registered_actor *a, slayer3d_registered_actor *b)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;

    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload != NULL)
    {
        if (a != NULL)
            slayer3d_properties_set_string(payload, "actor_name", a->name);
        if (b != NULL)
            slayer3d_properties_set_string(payload, "other_actor_name", b->name);
    }
    slayer3d_signal_emit(bus, sensor->signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static void execute_sensor_actions(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                   slayer3d_registered_actor *a, slayer3d_registered_actor *b)
{
    if (runtime == NULL || sensor == NULL || !yyjson_is_arr(sensor->actions))
        return;

    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload != NULL)
    {
        if (a != NULL)
            slayer3d_properties_set_string(payload, "actor_name", a->name);
        if (b != NULL)
            slayer3d_properties_set_string(payload, "other_actor_name", b->name);
    }

    for (size_t i = 0; i < yyjson_arr_size(sensor->actions); ++i)
        (void)execute_one_action(runtime, yyjson_arr_get(sensor->actions, i), payload);
    slayer3d_properties_destroy(payload);
}

static void emit_sensor_payload_signal(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                       const slayer3d_properties *payload)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;
    slayer3d_signal_emit(bus, sensor->signal_id, payload);
}

static void execute_sensor_payload_actions(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                           const slayer3d_properties *payload)
{
    if (runtime == NULL || sensor == NULL || !yyjson_is_arr(sensor->actions))
        return;
    (void)execute_action_array(runtime, sensor->actions, payload);
}

static bool runtime_actor_is_active(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *actor)
{
    if (runtime == NULL || actor == NULL)
        return false;
    int actor_index = -1;
    const actor_pool_runtime *pool = find_actor_pool_for_actor_const(runtime, actor->name, &actor_index);
    if (pool != NULL)
        return actor_pool_actor_is_active(pool, actor, actor_index);
    return actor->active;
}

static bool sensor_actor_list_add(sensor_actor_list *list, slayer3d_registered_actor *actor)
{
    if (list == NULL || actor == NULL)
        return false;
    if (list->count >= list->capacity)
    {
        const int new_capacity = list->capacity > 0 ? list->capacity * 2 : 8;
        slayer3d_registered_actor **items =
            (slayer3d_registered_actor **)SDL_realloc(list->items, (size_t)new_capacity * sizeof(*items));
        if (items == NULL)
            return false;
        list->items = items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = actor;
    return true;
}

static void sensor_actor_list_free(sensor_actor_list *list)
{
    if (list == NULL)
        return;
    SDL_free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool collect_sensor_endpoint_actors(slayer3d_game_data_runtime *runtime, const char *actor_name, const char *tag,
                                           sensor_actor_list *out_list)
{
    if (runtime == NULL || out_list == NULL)
        return false;

    if (actor_name != NULL)
    {
        if (!active_scene_has_entity_internal(runtime, actor_name))
            return true;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, actor_name);
        if (runtime_actor_is_active(runtime, actor))
            return sensor_actor_list_add(out_list, actor);
        return true;
    }

    if (tag == NULL || tag[0] == '\0')
        return true;

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *tags[1] = {tag};
        if (!entity_json_has_tags(entity, tags, 1))
            continue;
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (runtime_actor_is_active(runtime, actor) && !sensor_actor_list_add(out_list, actor))
            return false;
    }

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        const char *tags[1] = {tag};
        if (!entity_json_has_tags(pool->archetype_json, tags, 1) ||
            !actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
        {
            continue;
        }
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index) && !sensor_actor_list_add(out_list, actor))
                return false;
        }
    }
    return true;
}

static bool sensor_contact_name_is_transient(const char *name)
{
    return name != NULL && SDL_strncmp(name, "noise.", 6) == 0;
}

static void sensor_contact_pair_destroy(sensor_contact_pair_state *state)
{
    if (state == NULL)
        return;
    if (state->owns_actor_name)
        SDL_free((char *)state->actor_name);
    if (state->owns_other_actor_name)
        SDL_free((char *)state->other_actor_name);
    SDL_zero(*state);
}

static sensor_contact_pair_state *sensor_contact_pair_state_for(sensor_entry *sensor, const char *actor_name,
                                                                const char *other_actor_name)
{
    if (sensor == NULL || actor_name == NULL || other_actor_name == NULL)
        return NULL;
    for (int i = 0; i < sensor->contact_pair_count; ++i)
    {
        sensor_contact_pair_state *state = &sensor->contact_pairs[i];
        if (state->actor_name != NULL && state->other_actor_name != NULL &&
            SDL_strcmp(state->actor_name, actor_name) == 0 &&
            SDL_strcmp(state->other_actor_name, other_actor_name) == 0)
        {
            return state;
        }
    }

    if (sensor->contact_pair_count >= sensor->contact_pair_capacity)
    {
        const int new_capacity = sensor->contact_pair_capacity > 0 ? sensor->contact_pair_capacity * 2 : 8;
        sensor_contact_pair_state *pairs = (sensor_contact_pair_state *)SDL_realloc(
            sensor->contact_pairs, (size_t)new_capacity * sizeof(*sensor->contact_pairs));
        if (pairs == NULL)
            return NULL;
        SDL_memset(pairs + sensor->contact_pair_capacity, 0,
                   (size_t)(new_capacity - sensor->contact_pair_capacity) * sizeof(*pairs));
        sensor->contact_pairs = pairs;
        sensor->contact_pair_capacity = new_capacity;
    }

    sensor_contact_pair_state *state = &sensor->contact_pairs[sensor->contact_pair_count++];
    SDL_zero(*state);
    if (sensor_contact_name_is_transient(actor_name))
    {
        state->actor_name = SDL_strdup(actor_name);
        state->owns_actor_name = true;
    }
    else
    {
        state->actor_name = actor_name;
    }
    if (sensor_contact_name_is_transient(other_actor_name))
    {
        state->other_actor_name = SDL_strdup(other_actor_name);
        state->owns_other_actor_name = true;
    }
    else
    {
        state->other_actor_name = other_actor_name;
    }
    if ((state->owns_actor_name && state->actor_name == NULL) ||
        (state->owns_other_actor_name && state->other_actor_name == NULL))
    {
        sensor_contact_pair_destroy(state);
        --sensor->contact_pair_count;
        return NULL;
    }
    return state;
}

static bool actors_contact_2d(const slayer3d_registered_actor *a, const slayer3d_registered_actor *b)
{
    if (a == NULL || b == NULL)
        return false;

    const float a_radius = slayer3d_properties_get_float(a->props, "radius", 0.0f);
    const float b_radius = slayer3d_properties_get_float(b->props, "radius", 0.0f);
    const float a_half_width = slayer3d_properties_get_float(a->props, "half_width", 0.0f);
    const float a_half_height = slayer3d_properties_get_float(a->props, "half_height", 0.0f);
    const float b_half_width = slayer3d_properties_get_float(b->props, "half_width", 0.0f);
    const float b_half_height = slayer3d_properties_get_float(b->props, "half_height", 0.0f);

    if (a_radius > 0.0f && b_half_width > 0.0f && b_half_height > 0.0f)
    {
        const float closest_x = SDL_clamp(a->position.x, b->position.x - b_half_width, b->position.x + b_half_width);
        const float closest_y = SDL_clamp(a->position.y, b->position.y - b_half_height, b->position.y + b_half_height);
        const float dx = a->position.x - closest_x;
        const float dy = a->position.y - closest_y;
        return dx * dx + dy * dy <= a_radius * a_radius;
    }
    if (a_radius > 0.0f && b_radius > 0.0f)
    {
        const float dx = a->position.x - b->position.x;
        const float dy = a->position.y - b->position.y;
        const float radius = a_radius + b_radius;
        return dx * dx + dy * dy <= radius * radius;
    }
    if (a_half_width > 0.0f && a_half_height > 0.0f && b_half_width > 0.0f && b_half_height > 0.0f)
    {
        return SDL_fabsf(a->position.x - b->position.x) <= a_half_width + b_half_width &&
               SDL_fabsf(a->position.y - b->position.y) <= a_half_height + b_half_height;
    }
    if (a_half_width > 0.0f && a_half_height > 0.0f && b_radius > 0.0f)
    {
        return actors_contact_2d(b, a);
    }
    return false;
}

static void sensor_contact_states_begin(sensor_entry *sensor)
{
    if (sensor == NULL)
        return;
    for (int i = 0; i < sensor->contact_pair_count; ++i)
        sensor->contact_pairs[i].seen = false;
}

static void sensor_contact_states_end(sensor_entry *sensor)
{
    if (sensor == NULL)
        return;
    bool any_active = false;
    int write_index = 0;
    for (int i = 0; i < sensor->contact_pair_count; ++i)
    {
        sensor_contact_pair_state *state = &sensor->contact_pairs[i];
        if (!state->seen)
        {
            if (sensor_contact_name_is_transient(state->actor_name) ||
                sensor_contact_name_is_transient(state->other_actor_name))
            {
                sensor_contact_pair_destroy(state);
                continue;
            }
            state->active = false;
        }
        any_active = any_active || state->active;
        if (write_index != i)
            sensor->contact_pairs[write_index] = *state;
        ++write_index;
    }
    sensor->contact_pair_count = write_index;
    sensor->was_active = any_active;
}

static void update_sensor_contact_pair(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                       slayer3d_registered_actor *actor, slayer3d_registered_actor *other)
{
    if (!runtime_actor_is_active(runtime, actor) || !runtime_actor_is_active(runtime, other))
        return;

    sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, actor->name, other->name);
    if (state == NULL)
        return;

    const bool active = actors_contact_2d(actor, other);
    if (active && !actor_matches_target_filter(runtime, other, actor, sensor->json, NULL, sensor->other_tag, false))
    {
        state->active = false;
        state->seen = true;
        return;
    }
    const bool stay =
        sensor->edge != NULL && (SDL_strcmp(sensor->edge, "stay") == 0 || SDL_strcmp(sensor->edge, "overlap") == 0);
    if (active && (stay || !state->active))
    {
        emit_sensor_signal(runtime, sensor, actor, other);
        execute_sensor_actions(runtime, sensor, actor, other);
    }
    state->active = active;
    state->seen = true;
}

static void update_contact_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list actors;
    sensor_actor_list others;
    SDL_zero(actors);
    SDL_zero(others);

    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &actors) &&
        collect_sensor_endpoint_actors(runtime, sensor->other, sensor->other_tag, &others))
    {
        for (int a = 0; a < actors.count; ++a)
        {
            for (int b = 0; b < others.count; ++b)
            {
                if (actors.items[a] != others.items[b])
                    update_sensor_contact_pair(runtime, sensor, actors.items[a], others.items[b]);
            }
        }
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&actors);
    sensor_actor_list_free(&others);
}

static bool sensor_edge_is_stay(const sensor_entry *sensor)
{
    return sensor != NULL && sensor->edge != NULL &&
           (SDL_strcmp(sensor->edge, "stay") == 0 || SDL_strcmp(sensor->edge, "overlap") == 0);
}

static bool sensor_edge_is_exit(const sensor_entry *sensor)
{
    return sensor != NULL && sensor->edge != NULL && SDL_strcmp(sensor->edge, "exit") == 0;
}

static unsigned int sensor_brush_contents_mask(const sensor_entry *sensor, unsigned int fallback)
{
    return brush_flags_from_json(sensor != NULL ? obj_get(sensor->json, "contents_mask") : NULL,
                                 brush_content_flag_from_string, fallback);
}

static bool trace_active_brush_point_contents(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 point,
                                              unsigned int contents_mask,
                                              slayer3d_game_data_brush_trace_result *out_result)
{
    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.start = point;
    trace.end = point;
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.contents_mask = contents_mask;
    return slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, out_result);
}

static void brush_trace_payload_fields(slayer3d_properties *payload,
                                       const slayer3d_game_data_brush_trace_result *result)
{
    if (payload == NULL)
        return;
    const bool hit = result != NULL && result->hit;
    slayer3d_properties_set_bool(payload, "hit_brush", hit);
    slayer3d_properties_set_string(payload, "brush_world", hit && result->world_name != NULL ? result->world_name : "");
    slayer3d_properties_set_string(payload, "hit_brush_world",
                                   hit && result->world_name != NULL ? result->world_name : "");
    slayer3d_properties_set_string(payload, "brush_name", hit && result->brush_name != NULL ? result->brush_name : "");
    slayer3d_properties_set_string(payload, "hit_brush_name",
                                   hit && result->brush_name != NULL ? result->brush_name : "");
    slayer3d_properties_set_string(payload, "material",
                                   hit && result->material_name != NULL ? result->material_name : "");
    slayer3d_properties_set_string(payload, "hit_material",
                                   hit && result->material_name != NULL ? result->material_name : "");
    slayer3d_properties_set_vec3(payload, "hit_position",
                                 hit ? result->end_position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(payload, "hit_normal", hit ? result->normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_float(payload, "hit_fraction", hit ? result->fraction : 1.0f);
    slayer3d_properties_set_int(payload, "contents",
                                hit ? (int)SDL_min(result->contents, (unsigned int)SDL_MAX_SINT32) : 0);
    slayer3d_properties_set_int(payload, "hit_contents",
                                hit ? (int)SDL_min(result->contents, (unsigned int)SDL_MAX_SINT32) : 0);
    slayer3d_properties_set_int(payload, "surface_flags",
                                hit ? (int)SDL_min(result->surface_flags, (unsigned int)SDL_MAX_SINT32) : 0);
    slayer3d_properties_set_int(payload, "hit_surface_flags",
                                hit ? (int)SDL_min(result->surface_flags, (unsigned int)SDL_MAX_SINT32) : 0);
}

static slayer3d_properties *create_brush_contents_sensor_payload(const sensor_entry *sensor,
                                                                 const slayer3d_registered_actor *actor,
                                                                 const slayer3d_game_data_brush_trace_result *result)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    slayer3d_properties_set_string(payload, "actor_name", actor != NULL && actor->name != NULL ? actor->name : "");
    slayer3d_properties_set_string(payload, "sensor_name", sensor != NULL && sensor->name != NULL ? sensor->name : "");
    slayer3d_properties_set_vec3(payload, "point",
                                 actor != NULL ? actor->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    brush_trace_payload_fields(payload, result);
    return payload;
}

static void update_brush_contents_sensor_actor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                               slayer3d_registered_actor *actor)
{
    if (!runtime_actor_is_active(runtime, actor))
        return;

    slayer3d_game_data_brush_trace_result result;
    const unsigned int mask = sensor_brush_contents_mask(sensor, SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER);
    const bool traced = trace_active_brush_point_contents(runtime, actor->position, mask, &result);
    const bool active =
        traced && result.hit && result.start_solid &&
        actor_matches_target_filter(runtime, actor, NULL, sensor->json, NULL, sensor->entity_tag, false);
    sensor_contact_pair_state *state = sensor_contact_pair_state_for(
        sensor, actor != NULL ? actor->name : NULL, sensor->name != NULL ? sensor->name : "brush_contents");
    if (state == NULL)
        return;

    const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                             : sensor_edge_is_stay(sensor) ? active
                                                           : (active && !state->active);
    if (should_emit)
    {
        slayer3d_properties *payload = create_brush_contents_sensor_payload(sensor, actor, active ? &result : NULL);
        emit_sensor_payload_signal(runtime, sensor, payload);
        execute_sensor_payload_actions(runtime, sensor, payload);
        slayer3d_properties_destroy(payload);
    }
    state->active = active;
    state->seen = true;
}

static void update_brush_contents_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list actors;
    SDL_zero(actors);
    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &actors))
    {
        for (int i = 0; i < actors.count; ++i)
            update_brush_contents_sensor_actor(runtime, sensor, actors.items[i]);
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&actors);
}

static int sensor_target_sector_index(const sector_level_runtime *level, const sensor_entry *sensor)
{
    if (level == NULL || sensor == NULL)
        return -1;
    if (sensor->sector_index >= 0)
        return sensor->sector_index < level->sector_count ? sensor->sector_index : -1;
    return sector_level_find_sector_name(level, sensor->sector);
}

static int actor_sector_index_for_sensor(const sector_level_runtime *level, const sensor_entry *sensor,
                                         const slayer3d_registered_actor *actor)
{
    if (level == NULL || actor == NULL)
        return -1;

    const char *sector_property =
        sensor != NULL && sensor->sector_property != NULL ? sensor->sector_property : "current_sector";
    const slayer3d_value *current_sector = slayer3d_properties_get_value(actor->props, sector_property);
    if (current_sector != NULL && current_sector->type == SLAYER3D_VALUE_INT && current_sector->as_int >= 0 &&
        current_sector->as_int < level->sector_count)
    {
        return current_sector->as_int;
    }

    return slayer3d_level_find_sector_at(&level->lightmapped, level->sectors, actor->position.x, actor->position.z,
                                         actor->position.y);
}

static slayer3d_properties *create_sector_sensor_payload(const slayer3d_game_data_runtime *runtime,
                                                         const sensor_entry *sensor, const sector_level_runtime *level,
                                                         const slayer3d_registered_actor *actor, int sector_index)
{
    (void)sensor;
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    slayer3d_properties_set_string(payload, "actor_name", actor != NULL && actor->name != NULL ? actor->name : "");
    slayer3d_properties_set_string(payload, "sector_level", level != NULL && level->name != NULL ? level->name : "");
    slayer3d_properties_set_int(payload, "sector_index", sector_index);
    slayer3d_properties_set_string(payload, "sector_name",
                                   level != NULL && sector_index >= 0 && sector_index < level->sector_count &&
                                           level->sector_names[sector_index] != NULL
                                       ? level->sector_names[sector_index]
                                       : "");
    const slayer3d_sector *sector =
        level != NULL && sector_index >= 0 && sector_index < level->sector_count ? &level->sectors[sector_index] : NULL;
    const float damage_per_second = slayer3d_sector_damage_per_second(sector);
    slayer3d_properties_set_float(payload, "sector_damage_per_second", damage_per_second);
    slayer3d_properties_set_float(
        payload, "sector_damage_delta",
        slayer3d_sector_damage_for_delta(sector, runtime != NULL ? runtime->current_dt : 0.0f));
    slayer3d_properties_set_int(payload, "ambient_sound_id", sector != NULL ? sector->ambient_sound_id : -1);
    return payload;
}

static void emit_sector_sensor_signal(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                      const sector_level_runtime *level, slayer3d_registered_actor *actor,
                                      int sector_index)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;

    slayer3d_properties *payload = create_sector_sensor_payload(runtime, sensor, level, actor, sector_index);
    slayer3d_signal_emit(bus, sensor->signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static void execute_sector_sensor_actions(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                          const sector_level_runtime *level, slayer3d_registered_actor *actor,
                                          int sector_index)
{
    if (runtime == NULL || sensor == NULL || !yyjson_is_arr(sensor->actions))
        return;

    slayer3d_properties *payload = create_sector_sensor_payload(runtime, sensor, level, actor, sector_index);
    if (payload != NULL)
        (void)execute_action_array(runtime, sensor->actions, payload);
    slayer3d_properties_destroy(payload);
}

static void update_sector_sensor_actor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                       const sector_level_runtime *level, int target_sector,
                                       slayer3d_registered_actor *actor)
{
    if (!runtime_actor_is_active(runtime, actor))
        return;

    const int current_sector = actor_sector_index_for_sensor(level, sensor, actor);
    const bool active =
        current_sector == target_sector &&
        actor_matches_target_filter(runtime, actor, NULL, sensor->json, NULL, sensor->entity_tag, false);
    const char *sector_name = "";
    if (target_sector >= 0 && target_sector < level->sector_count && level->sector_names[target_sector] != NULL)
        sector_name = level->sector_names[target_sector];
    sensor_contact_pair_state *state =
        sensor_contact_pair_state_for(sensor, actor != NULL ? actor->name : NULL, sector_name);
    if (state == NULL)
        return;

    const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                             : sensor_edge_is_stay(sensor) ? active
                                                           : (active && !state->active);
    if (should_emit)
    {
        const int event_sector = active ? current_sector : target_sector;
        emit_sector_sensor_signal(runtime, sensor, level, actor, event_sector);
        execute_sector_sensor_actions(runtime, sensor, level, actor, event_sector);
    }
    state->active = active;
    state->seen = true;
}

static void update_sector_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    const sector_level_runtime *level = find_sector_level_runtime(runtime, sensor->sector_level);
    const int target_sector = sensor_target_sector_index(level, sensor);
    if (level == NULL || target_sector < 0)
        return;

    sensor_actor_list actors;
    SDL_zero(actors);
    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &actors))
    {
        for (int i = 0; i < actors.count; ++i)
            update_sector_sensor_actor(runtime, sensor, level, target_sector, actors.items[i]);
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&actors);
}

static bool point_in_sensor_volume(const sensor_entry *sensor, slayer3d_vec3 position)
{
    if (sensor == NULL)
        return false;
    return position.x >= sensor->volume_min.x && position.x <= sensor->volume_max.x &&
           position.y >= sensor->volume_min.y && position.y <= sensor->volume_max.y &&
           position.z >= sensor->volume_min.z && position.z <= sensor->volume_max.z;
}

static void update_volume_sensor_actor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                       slayer3d_registered_actor *actor)
{
    if (!runtime_actor_is_active(runtime, actor))
        return;

    sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, actor != NULL ? actor->name : NULL,
                                                                     sensor->name != NULL ? sensor->name : "volume");
    if (state == NULL)
        return;

    const bool active =
        point_in_sensor_volume(sensor, actor->position) &&
        actor_matches_target_filter(runtime, actor, NULL, sensor->json, NULL, sensor->entity_tag, false);
    const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                             : sensor_edge_is_stay(sensor) ? active
                                                           : (active && !state->active);
    if (should_emit)
    {
        emit_sensor_signal(runtime, sensor, actor, NULL);
        execute_sensor_actions(runtime, sensor, actor, NULL);
    }
    state->active = active;
    state->seen = true;
}

static void update_volume_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list actors;
    SDL_zero(actors);
    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &actors))
    {
        for (int i = 0; i < actors.count; ++i)
            update_volume_sensor_actor(runtime, sensor, actors.items[i]);
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&actors);
}

static bool perception_actor_in_field_of_view(const sensor_entry *sensor, const slayer3d_registered_actor *observer,
                                              const slayer3d_registered_actor *target)
{
    if (sensor == NULL || observer == NULL || target == NULL)
        return false;
    if (sensor->min_dot <= -1.0f)
        return true;

    const float yaw = slayer3d_properties_get_float(observer->props, sensor->yaw_property, 0.0f);
    const slayer3d_vec3 forward = slayer3d_vec3_make(SDL_sinf(yaw), 0.0f, -SDL_cosf(yaw));
    slayer3d_vec3 to_target =
        slayer3d_vec3_make(target->position.x - observer->position.x, 0.0f, target->position.z - observer->position.z);
    if (slayer3d_vec3_length_squared(to_target) <= 0.000001f)
        return true;
    to_target = slayer3d_vec3_normalize(to_target);
    return slayer3d_vec3_dot(forward, to_target) >= sensor->min_dot;
}

static bool perception_sensor_has_line_of_sight(const sensor_entry *sensor, const sector_level_runtime *level,
                                                const slayer3d_registered_actor *observer,
                                                const slayer3d_registered_actor *target, float *out_distance,
                                                slayer3d_vec3 *out_origin, slayer3d_vec3 *out_target)
{
    if (sensor == NULL || level == NULL || observer == NULL || target == NULL)
        return false;

    const slayer3d_vec3 origin = slayer3d_vec3_make(
        observer->position.x, observer->position.y + sensor->observer_eye_height, observer->position.z);
    const slayer3d_vec3 target_point =
        slayer3d_vec3_make(target->position.x, target->position.y + sensor->target_eye_height, target->position.z);
    const slayer3d_vec3 delta = slayer3d_vec3_sub(target_point, origin);
    const float distance = slayer3d_vec3_length(delta);
    if (distance <= 0.000001f || distance > sensor->range)
        return false;

    if (out_distance != NULL)
        *out_distance = distance;
    if (out_origin != NULL)
        *out_origin = origin;
    if (out_target != NULL)
        *out_target = target_point;

    const slayer3d_vec3 direction = slayer3d_vec3_scale(delta, 1.0f / distance);
    const slayer3d_level_trace_result trace =
        slayer3d_level_trace_point(&level->lightmapped, level->sectors, origin, direction, distance);
    return !trace.hit || SDL_clamp(trace.fraction, 0.0f, 1.0f) >= 0.995f;
}

static bool brush_perception_sensor_has_line_of_sight(const slayer3d_game_data_runtime *runtime,
                                                      const sensor_entry *sensor,
                                                      const slayer3d_registered_actor *observer,
                                                      const slayer3d_registered_actor *target, float *out_distance,
                                                      slayer3d_vec3 *out_origin, slayer3d_vec3 *out_target,
                                                      slayer3d_game_data_brush_trace_result *out_trace)
{
    if (runtime == NULL || sensor == NULL || observer == NULL || target == NULL)
        return false;
    if (out_trace != NULL)
        SDL_zero(*out_trace);

    const slayer3d_vec3 origin = slayer3d_vec3_make(
        observer->position.x, observer->position.y + sensor->observer_eye_height, observer->position.z);
    const slayer3d_vec3 target_point =
        slayer3d_vec3_make(target->position.x, target->position.y + sensor->target_eye_height, target->position.z);
    const slayer3d_vec3 delta = slayer3d_vec3_sub(target_point, origin);
    const float distance = slayer3d_vec3_length(delta);
    if (distance <= 0.000001f || distance > sensor->range)
        return false;

    if (out_distance != NULL)
        *out_distance = distance;
    if (out_origin != NULL)
        *out_origin = origin;
    if (out_target != NULL)
        *out_target = target_point;

    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.start = origin;
    trace.end = target_point;
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.contents_mask = sensor_brush_contents_mask(sensor, SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID |
                                                                 SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP);
    slayer3d_game_data_brush_trace_result result;
    if (!slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, &result))
        return false;
    if (out_trace != NULL)
        *out_trace = result;
    return !result.hit || SDL_clamp(result.fraction, 0.0f, 1.0f) >= 0.995f;
}

static slayer3d_properties *create_perception_sensor_payload(const sensor_entry *sensor,
                                                             const sector_level_runtime *level,
                                                             const slayer3d_registered_actor *observer,
                                                             const slayer3d_registered_actor *target, float distance,
                                                             slayer3d_vec3 origin, slayer3d_vec3 target_point)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    slayer3d_properties_set_string(payload, "actor_name",
                                   observer != NULL && observer->name != NULL ? observer->name : "");
    slayer3d_properties_set_string(payload, "observer_actor_name",
                                   observer != NULL && observer->name != NULL ? observer->name : "");
    slayer3d_properties_set_string(payload, "target_actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(payload, "other_actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(payload, "sector_level", level != NULL && level->name != NULL ? level->name : "");
    slayer3d_properties_set_float(payload, "distance", distance);
    slayer3d_properties_set_float(payload, "perception_distance", distance);
    slayer3d_properties_set_float(payload, "range", sensor != NULL ? sensor->range : 0.0f);
    slayer3d_properties_set_vec3(payload, "origin", origin);
    slayer3d_properties_set_vec3(payload, "target_position", target_point);
    return payload;
}

static void emit_perception_sensor_signal(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                          const sector_level_runtime *level, slayer3d_registered_actor *observer,
                                          slayer3d_registered_actor *target, float distance, slayer3d_vec3 origin,
                                          slayer3d_vec3 target_point)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;

    slayer3d_properties *payload =
        create_perception_sensor_payload(sensor, level, observer, target, distance, origin, target_point);
    slayer3d_signal_emit(bus, sensor->signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static void execute_perception_sensor_actions(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                              const sector_level_runtime *level, slayer3d_registered_actor *observer,
                                              slayer3d_registered_actor *target, float distance, slayer3d_vec3 origin,
                                              slayer3d_vec3 target_point)
{
    if (runtime == NULL || sensor == NULL || !yyjson_is_arr(sensor->actions))
        return;

    slayer3d_properties *payload =
        create_perception_sensor_payload(sensor, level, observer, target, distance, origin, target_point);
    if (payload != NULL)
        (void)execute_action_array(runtime, sensor->actions, payload);
    slayer3d_properties_destroy(payload);
}

static slayer3d_properties *create_brush_perception_sensor_payload(const sensor_entry *sensor,
                                                                   const slayer3d_registered_actor *observer,
                                                                   const slayer3d_registered_actor *target,
                                                                   float distance, slayer3d_vec3 origin,
                                                                   slayer3d_vec3 target_point, bool visible,
                                                                   const slayer3d_game_data_brush_trace_result *trace)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    slayer3d_properties_set_string(payload, "actor_name",
                                   observer != NULL && observer->name != NULL ? observer->name : "");
    slayer3d_properties_set_string(payload, "observer_actor_name",
                                   observer != NULL && observer->name != NULL ? observer->name : "");
    slayer3d_properties_set_string(payload, "target_actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(payload, "other_actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(payload, "sensor_name", sensor != NULL && sensor->name != NULL ? sensor->name : "");
    slayer3d_properties_set_bool(payload, "line_of_sight", visible);
    slayer3d_properties_set_bool(payload, "brush_line_of_sight", visible);
    slayer3d_properties_set_float(payload, "distance", distance);
    slayer3d_properties_set_float(payload, "perception_distance", distance);
    slayer3d_properties_set_float(payload, "range", sensor != NULL ? sensor->range : 0.0f);
    slayer3d_properties_set_vec3(payload, "origin", origin);
    slayer3d_properties_set_vec3(payload, "target_position", target_point);
    brush_trace_payload_fields(payload, trace);
    return payload;
}

static void update_brush_perception_sensor_pair(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                                slayer3d_registered_actor *observer, slayer3d_registered_actor *target)
{
    if (!runtime_actor_is_active(runtime, observer) || !runtime_actor_is_active(runtime, target) || observer == target)
        return;

    sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, observer->name, target->name);
    if (state == NULL)
        return;

    float distance = 0.0f;
    slayer3d_vec3 origin = observer->position;
    slayer3d_vec3 target_point = target->position;
    slayer3d_game_data_brush_trace_result trace;
    SDL_zero(trace);
    bool active = actor_matches_target_filter(runtime, target, observer, sensor->json, NULL, sensor->other_tag, true) &&
                  perception_actor_in_field_of_view(sensor, observer, target) &&
                  brush_perception_sensor_has_line_of_sight(runtime, sensor, observer, target, &distance, &origin,
                                                            &target_point, &trace);

    const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                             : sensor_edge_is_stay(sensor) ? active
                                                           : (active && !state->active);
    if (should_emit)
    {
        slayer3d_properties *payload = create_brush_perception_sensor_payload(
            sensor, observer, target, distance, origin, target_point, active, trace.hit ? &trace : NULL);
        emit_sensor_payload_signal(runtime, sensor, payload);
        execute_sensor_payload_actions(runtime, sensor, payload);
        slayer3d_properties_destroy(payload);
    }
    state->active = active;
    state->seen = true;
}

static void update_brush_perception_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list observers;
    sensor_actor_list targets;
    SDL_zero(observers);
    SDL_zero(targets);

    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &observers) &&
        collect_sensor_endpoint_actors(runtime, sensor->other, sensor->other_tag, &targets))
    {
        for (int observer_index = 0; observer_index < observers.count; ++observer_index)
        {
            for (int target_index = 0; target_index < targets.count; ++target_index)
            {
                update_brush_perception_sensor_pair(runtime, sensor, observers.items[observer_index],
                                                    targets.items[target_index]);
            }
        }
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&observers);
    sensor_actor_list_free(&targets);
}

static void update_perception_sensor_pair(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                          const sector_level_runtime *level, slayer3d_registered_actor *observer,
                                          slayer3d_registered_actor *target)
{
    if (!runtime_actor_is_active(runtime, observer) || !runtime_actor_is_active(runtime, target) || observer == target)
        return;

    sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, observer->name, target->name);
    if (state == NULL)
        return;

    float distance = 0.0f;
    slayer3d_vec3 origin = observer->position;
    slayer3d_vec3 target_point = target->position;
    bool active =
        actor_matches_target_filter(runtime, target, observer, sensor->json, NULL, sensor->other_tag, true) &&
        perception_actor_in_field_of_view(sensor, observer, target) &&
        perception_sensor_has_line_of_sight(sensor, level, observer, target, &distance, &origin, &target_point);

    const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                             : sensor_edge_is_stay(sensor) ? active
                                                           : (active && !state->active);
    if (should_emit)
    {
        emit_perception_sensor_signal(runtime, sensor, level, observer, target, distance, origin, target_point);
        execute_perception_sensor_actions(runtime, sensor, level, observer, target, distance, origin, target_point);
    }
    state->active = active;
    state->seen = true;
}

static void update_perception_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    const sector_level_runtime *level = find_sector_level_runtime(runtime, sensor->sector_level);
    if (level == NULL)
        return;

    sensor_actor_list observers;
    sensor_actor_list targets;
    SDL_zero(observers);
    SDL_zero(targets);

    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &observers) &&
        collect_sensor_endpoint_actors(runtime, sensor->other, sensor->other_tag, &targets))
    {
        for (int observer_index = 0; observer_index < observers.count; ++observer_index)
        {
            for (int target_index = 0; target_index < targets.count; ++target_index)
            {
                update_perception_sensor_pair(runtime, sensor, level, observers.items[observer_index],
                                              targets.items[target_index]);
            }
        }
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&observers);
    sensor_actor_list_free(&targets);
}

static float noise_event_audibility(const sensor_entry *sensor, const noise_event_runtime *event,
                                    const slayer3d_registered_actor *listener, float *out_distance)
{
    if (sensor == NULL || event == NULL || listener == NULL)
        return 0.0f;
    const slayer3d_vec3 delta = slayer3d_vec3_sub(event->position, listener->position);
    const float distance = slayer3d_vec3_length(delta);
    if (out_distance != NULL)
        *out_distance = distance;
    const float radius = SDL_min(sensor->range, event->radius);
    if (radius <= 0.0f || distance > radius)
        return 0.0f;
    return event->loudness * SDL_clamp(1.0f - distance / radius, 0.0f, 1.0f);
}

static slayer3d_properties *create_hearing_sensor_payload(const sensor_entry *sensor, const noise_event_runtime *event,
                                                          const slayer3d_registered_actor *listener,
                                                          const slayer3d_registered_actor *source, float distance,
                                                          float audibility)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    slayer3d_properties_set_string(payload, "actor_name",
                                   listener != NULL && listener->name != NULL ? listener->name : "");
    slayer3d_properties_set_string(payload, "listener_actor_name",
                                   listener != NULL && listener->name != NULL ? listener->name : "");
    slayer3d_properties_set_string(payload, "source_actor_name",
                                   event != NULL && event->source_actor_name != NULL ? event->source_actor_name : "");
    slayer3d_properties_set_string(payload, "target_actor_name",
                                   source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_int(payload, "noise_id", event != NULL ? (int)event->id : 0);
    slayer3d_properties_set_vec3(payload, "noise_position",
                                 event != NULL ? event->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_float(payload, "noise_radius", event != NULL ? event->radius : 0.0f);
    slayer3d_properties_set_float(payload, "noise_loudness", event != NULL ? event->loudness : 0.0f);
    slayer3d_properties_set_float(payload, "distance", distance);
    slayer3d_properties_set_float(payload, "hearing_distance", distance);
    slayer3d_properties_set_float(payload, "audibility", audibility);
    slayer3d_properties_set_float(payload, "range", sensor != NULL ? sensor->range : 0.0f);
    return payload;
}

static void emit_hearing_sensor_signal(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                       const noise_event_runtime *event, slayer3d_registered_actor *listener,
                                       slayer3d_registered_actor *source, float distance, float audibility)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;

    slayer3d_properties *payload = create_hearing_sensor_payload(sensor, event, listener, source, distance, audibility);
    slayer3d_signal_emit(bus, sensor->signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static void execute_hearing_sensor_actions(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                           const noise_event_runtime *event, slayer3d_registered_actor *listener,
                                           slayer3d_registered_actor *source, float distance, float audibility)
{
    if (runtime == NULL || sensor == NULL || !yyjson_is_arr(sensor->actions))
        return;

    slayer3d_properties *payload = create_hearing_sensor_payload(sensor, event, listener, source, distance, audibility);
    if (payload != NULL)
        (void)execute_action_array(runtime, sensor->actions, payload);
    slayer3d_properties_destroy(payload);
}

static void update_hearing_sensor_listener(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                           slayer3d_registered_actor *listener)
{
    if (!runtime_actor_is_active(runtime, listener))
        return;

    for (int event_index = 0; event_index < runtime->noise_event_count; ++event_index)
    {
        const noise_event_runtime *event = &runtime->noise_events[event_index];
        if (event->remaining_seconds <= 0.0f)
            continue;

        slayer3d_registered_actor *source = slayer3d_game_data_find_actor(runtime, event->source_actor_name);
        if (event->source_actor_name != NULL &&
            !actor_matches_target_filter(runtime, source, listener, sensor->json, NULL, sensor->other_tag, true))
        {
            continue;
        }

        float distance = 0.0f;
        const float audibility = noise_event_audibility(sensor, event, listener, &distance);
        const bool active = audibility > 0.0f;
        sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, listener->name, event->key);
        if (state == NULL)
            continue;

        const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                                 : sensor_edge_is_stay(sensor) ? active
                                                               : (active && !state->active);
        if (should_emit)
        {
            emit_hearing_sensor_signal(runtime, sensor, event, listener, source, distance, audibility);
            execute_hearing_sensor_actions(runtime, sensor, event, listener, source, distance, audibility);
        }
        state->active = active;
        state->seen = true;
    }
}

static void update_hearing_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list listeners;
    SDL_zero(listeners);

    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &listeners))
    {
        for (int i = 0; i < listeners.count; ++i)
            update_hearing_sensor_listener(runtime, sensor, listeners.items[i]);
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&listeners);
}

static void update_sensors(slayer3d_game_data_runtime *runtime)
{
    for (int i = 0; i < runtime->sensor_count; ++i)
    {
        sensor_entry *sensor = &runtime->sensors[i];
        if (sensor->type == GAME_DATA_SENSOR_CONTACT_2D)
        {
            update_contact_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_SECTOR)
        {
            update_sector_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_VOLUME)
        {
            update_volume_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_BRUSH_CONTENTS)
        {
            update_brush_contents_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_BRUSH_PERCEPTION)
        {
            update_brush_perception_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_PERCEPTION)
        {
            update_perception_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_HEARING)
        {
            update_hearing_sensor(runtime, sensor);
            continue;
        }

        if (sensor->entity != NULL && !active_scene_has_entity_internal(runtime, sensor->entity))
        {
            sensor->was_active = false;
            continue;
        }

        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, sensor->entity);
        if (sensor->type == GAME_DATA_SENSOR_INPUT_PRESSED)
        {
            const int action_id = slayer3d_game_data_find_action(runtime, sensor->action);
            if (slayer3d_input_is_pressed(runtime_input(runtime), action_id))
                emit_sensor_signal(runtime, sensor, NULL, NULL);
            continue;
        }
        if (actor == NULL)
            continue;

        if (sensor->type == GAME_DATA_SENSOR_BOUNDS_EXIT)
        {
            const int axis = axis_index(sensor->axis);
            const float value = vec_axis(actor->position, axis);
            const bool active =
                SDL_strcmp(sensor->side, "min") == 0 ? value < sensor->threshold : value > sensor->threshold;
            if (active && !sensor->was_active)
                emit_sensor_signal(runtime, sensor, actor, NULL);
            sensor->was_active = active;
        }
        else if (sensor->type == GAME_DATA_SENSOR_BOUNDS_REFLECT)
        {
            const int axis = axis_index(sensor->axis);
            const float value = vec_axis(actor->position, axis);
            if (value < sensor->min_value || value > sensor->max_value)
            {
                slayer3d_vec3 position = actor->position;
                slayer3d_vec3 velocity = actor_vec_property(actor, "velocity");
                set_vec_axis(&position, axis, SDL_clamp(value, sensor->min_value, sensor->max_value));
                set_vec_axis(&velocity, axis, -vec_axis(velocity, axis));
                actor_set_position(actor, position);
                slayer3d_properties_set_vec3(actor->props, "velocity", velocity);
                emit_sensor_signal(runtime, sensor, actor, NULL);
            }
        }
    }
}

static void update_noise_events(slayer3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL || runtime->noise_event_count <= 0)
        return;

    int write_index = 0;
    for (int read_index = 0; read_index < runtime->noise_event_count; ++read_index)
    {
        noise_event_runtime event = runtime->noise_events[read_index];
        event.remaining_seconds -= dt;
        if (event.remaining_seconds > 0.0f)
        {
            if (write_index != read_index)
                runtime->noise_events[write_index] = event;
            ++write_index;
        }
    }
    runtime->noise_event_count = write_index;
}

static float json_random_axis(slayer3d_game_data_runtime *runtime, yyjson_val *value, float fallback)
{
    if (yyjson_is_num(value))
        return (float)yyjson_get_num(value);
    if (yyjson_is_arr(value) && yyjson_arr_size(value) >= 2 && yyjson_is_num(yyjson_arr_get(value, 0)) &&
        yyjson_is_num(yyjson_arr_get(value, 1)))
    {
        const float lo = (float)yyjson_get_num(yyjson_arr_get(value, 0));
        const float hi = (float)yyjson_get_num(yyjson_arr_get(value, 1));
        return lo + (hi - lo) * game_data_random01(runtime);
    }
    return fallback;
}

static slayer3d_vec3 wave_schedule_position(slayer3d_game_data_runtime *runtime, yyjson_val *schedule,
                                            slayer3d_vec3 fallback)
{
    yyjson_val *position = obj_get(schedule, "position");
    if (yyjson_is_arr(position))
        return json_vec3_value(position, fallback);
    if (yyjson_is_obj(position))
    {
        return slayer3d_vec3_make(json_random_axis(runtime, obj_get(position, "x"), fallback.x),
                                  json_random_axis(runtime, obj_get(position, "y"), fallback.y),
                                  json_random_axis(runtime, obj_get(position, "z"), fallback.z));
    }
    return fallback;
}

static void update_wave_schedules(slayer3d_game_data_runtime *runtime, float dt)
{
    for (int i = 0; runtime != NULL && i < runtime->wave_schedule_count; ++i)
    {
        wave_schedule_entry *entry = &runtime->wave_schedules[i];
        yyjson_val *schedule = entry->schedule;
        yyjson_val *active_if = obj_get(schedule, "active_if");
        if (active_if != NULL && !eval_data_condition(runtime, active_if, NULL))
        {
            entry->initialized = false;
            entry->elapsed = 0.0f;
            continue;
        }

        const float interval = SDL_max(json_float(schedule, "interval", 1.0f), 0.001f);
        if (!entry->initialized)
        {
            entry->elapsed = -json_float(schedule, "initial_delay", interval);
            entry->initialized = true;
        }
        entry->elapsed += dt;
        if (entry->elapsed < 0.0f)
            continue;

        const char *max_active_tag = json_string(schedule, "max_active_tag", NULL);
        if (max_active_tag != NULL)
        {
            sensor_actor_list active;
            SDL_zero(active);
            (void)collect_sensor_endpoint_actors(runtime, NULL, max_active_tag, &active);
            const int max_active = json_int(schedule, "max_active", 0);
            if (max_active > 0 && active.count >= max_active)
            {
                sensor_actor_list_free(&active);
                entry->elapsed = SDL_min(entry->elapsed, interval);
                continue;
            }
            sensor_actor_list_free(&active);
        }

        while (entry->elapsed >= 0.0f)
        {
            actor_pool_runtime *pool = find_actor_pool(runtime, json_string(schedule, "pool", NULL));
            actor_pool_note_spawn_attempt(pool);
            int actor_index = -1;
            slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
            if (pool == NULL || actor == NULL || actor_index < 0)
            {
                actor_pool_note_spawn_failure(pool, "exhausted");
                break;
            }
            actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
            if (!initialize_pooled_actor(pool, actor, actor_index, true))
            {
                actor_pool_note_spawn_failure(pool, "initialize_failed");
                break;
            }
            actor_set_position(actor, wave_schedule_position(runtime, schedule, actor->position));
            apply_actor_spawn_properties(actor, obj_get(schedule, "properties"));
            yyjson_val *velocity = obj_get(schedule, "velocity");
            if (velocity != NULL)
                slayer3d_properties_set_vec3(actor->props, "velocity",
                                             json_vec3_value(velocity, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
            actor_pool_note_spawn_success(runtime, pool);
            entry->elapsed -= interval;
            if (!json_bool(schedule, "catch_up", false))
                break;
        }
    }
}

bool slayer3d_game_data_update(slayer3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL || runtime->doc == NULL)
        return false;
    if (dt < 0.0f)
        dt = 0.0f;
    runtime->current_dt = dt;

    yyjson_val *root = yyjson_doc_get_root(runtime->doc);
    actor_lifecycle_defer_begin(runtime);
    update_sector_doors(runtime, dt);
    if (!update_sector_platforms(runtime, dt))
    {
        actor_lifecycle_defer_end(runtime);
        return false;
    }
    update_control_components(runtime, root, dt);
    update_motion_components(runtime, root, dt);
    update_wave_schedules(runtime, dt);
    update_sensors(runtime);
    update_noise_events(runtime, dt);
    actor_lifecycle_defer_end(runtime);
    return true;
}

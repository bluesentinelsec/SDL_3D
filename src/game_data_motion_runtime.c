/* Sector platform, component state, and motion update helpers. */

#include "game_data_internal.h"

#include <SDL3/SDL_log.h>

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

bool update_sector_platforms(slayer3d_game_data_runtime *runtime, float dt)
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

static float approach_zero(float value, float amount)
{
    if (value > amount)
        return value - amount;
    if (value < -amount)
        return value + amount;
    return 0.0f;
}

static void update_viewmodel_bob_component(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                           slayer3d_registered_actor *actor, float dt)
{
    if (runtime == NULL || component == NULL || actor == NULL || actor->props == NULL || dt <= 0.0f)
        return;

    slayer3d_registered_actor *source = slayer3d_game_data_find_actor(runtime, json_string(component, "source", NULL));
    if (source == NULL)
        return;

    const char *previous_property = json_string(component, "previous_position_property", "viewmodel_bob_source");
    const char *phase_property = json_string(component, "phase_property", "viewmodel_bob_phase");
    const char *x_property = json_string(component, "offset_x_property", "viewmodel_bob_x");
    const char *y_property = json_string(component, "offset_y_property", "viewmodel_bob_y");
    const char *z_property = json_string(component, "offset_z_property", "viewmodel_bob_z");
    const char *pitch_property = json_string(component, "pitch_property", "viewmodel_bob_pitch");
    const char *yaw_property = json_string(component, "yaw_property", "viewmodel_bob_yaw");
    const char *roll_property = json_string(component, "roll_property", "viewmodel_bob_roll");
    if (previous_property == NULL || previous_property[0] == '\0' || phase_property == NULL ||
        phase_property[0] == '\0')
    {
        return;
    }

    const slayer3d_value *previous_value = slayer3d_properties_get_value(actor->props, previous_property);
    if (previous_value == NULL || previous_value->type != SLAYER3D_VALUE_VEC3)
    {
        slayer3d_properties_set_vec3(actor->props, previous_property, source->position);
        return;
    }

    const slayer3d_vec3 previous = previous_value->as_vec3;
    const float dx = source->position.x - previous.x;
    const float dz = source->position.z - previous.z;
    const float horizontal_speed = SDL_sqrtf(dx * dx + dz * dz) / dt;
    slayer3d_properties_set_vec3(actor->props, previous_property, source->position);

    const float min_speed = SDL_max(json_float(component, "min_speed", 0.1f), 0.0f);
    const float settle_rate = SDL_max(json_float(component, "settle_rate", 8.0f), 0.0f);
    if (horizontal_speed < min_speed)
    {
        const float settle = settle_rate * dt;
        if (x_property != NULL)
            slayer3d_properties_set_float(
                actor->props, x_property,
                approach_zero(slayer3d_properties_get_float(actor->props, x_property, 0.0f), settle));
        if (y_property != NULL)
            slayer3d_properties_set_float(
                actor->props, y_property,
                approach_zero(slayer3d_properties_get_float(actor->props, y_property, 0.0f), settle));
        if (z_property != NULL)
            slayer3d_properties_set_float(
                actor->props, z_property,
                approach_zero(slayer3d_properties_get_float(actor->props, z_property, 0.0f), settle));
        if (pitch_property != NULL)
            slayer3d_properties_set_float(
                actor->props, pitch_property,
                approach_zero(slayer3d_properties_get_float(actor->props, pitch_property, 0.0f), settle));
        if (yaw_property != NULL)
            slayer3d_properties_set_float(
                actor->props, yaw_property,
                approach_zero(slayer3d_properties_get_float(actor->props, yaw_property, 0.0f), settle));
        if (roll_property != NULL)
            slayer3d_properties_set_float(
                actor->props, roll_property,
                approach_zero(slayer3d_properties_get_float(actor->props, roll_property, 0.0f), settle));
        return;
    }

    const float frequency = SDL_max(json_float(component, "frequency", 9.0f), 0.0f);
    const float speed_scale = SDL_max(json_float(component, "speed_scale", 0.2f), 0.0f);
    float phase = slayer3d_properties_get_float(actor->props, phase_property, 0.0f) +
                  frequency * SDL_min(horizontal_speed * speed_scale, 2.5f) * dt;
    const float two_pi = 6.28318530717958647692f;
    if (phase > two_pi || phase < -two_pi)
        phase = SDL_fmodf(phase, two_pi);
    slayer3d_properties_set_float(actor->props, phase_property, phase);

    const slayer3d_vec3 offset_amplitude =
        json_vec3(component, "offset_amplitude", slayer3d_vec3_make(0.015f, 0.025f, 0.006f));
    const float pitch_amplitude = json_float(component, "pitch_amplitude", 0.008f);
    const float yaw_amplitude = json_float(component, "yaw_amplitude", 0.0f);
    const float roll_amplitude = json_float(component, "roll_amplitude", 0.012f);
    const float horizontal = SDL_sinf(phase);
    const float vertical = SDL_fabsf(SDL_sinf(phase + SDL_PI_F * 0.5f));

    if (x_property != NULL)
        slayer3d_properties_set_float(actor->props, x_property, offset_amplitude.x * horizontal);
    if (y_property != NULL)
        slayer3d_properties_set_float(actor->props, y_property, offset_amplitude.y * vertical);
    if (z_property != NULL)
        slayer3d_properties_set_float(actor->props, z_property, offset_amplitude.z * SDL_cosf(phase));
    if (pitch_property != NULL)
        slayer3d_properties_set_float(actor->props, pitch_property, pitch_amplitude * SDL_cosf(phase));
    if (yaw_property != NULL)
        slayer3d_properties_set_float(actor->props, yaw_property, yaw_amplitude * horizontal);
    if (roll_property != NULL)
        slayer3d_properties_set_float(actor->props, roll_property, roll_amplitude * horizontal);
}

void update_control_components(slayer3d_game_data_runtime *runtime, yyjson_val *root, float dt)
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

void update_motion_components(slayer3d_game_data_runtime *runtime, yyjson_val *root, float dt)
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
                else if (SDL_strcmp(type, "viewmodel.bob") == 0)
                {
                    update_viewmodel_bob_component(runtime, component, actor, dt);
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

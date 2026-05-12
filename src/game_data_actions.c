/**
 * @file game_data_actions.c
 * @brief Data-authored action dispatcher.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_log.h>

bool execute_one_action(slayer3d_game_data_runtime *runtime, yyjson_val *action, const slayer3d_properties *payload)
{
    const char *type = json_string(action, "type", "");
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    slayer3d_timer_pool *timers = runtime_timers(runtime);

    if (SDL_strcmp(type, "signal.emit") == 0)
    {
        const int signal_id = action_signal_id(runtime, action, "signal");
        if (signal_id >= 0 && bus != NULL)
            slayer3d_signal_emit(bus, signal_id, payload);
        return signal_id >= 0;
    }

    if (SDL_strcmp(type, "timer.start") == 0)
    {
        const int timer_index = find_timer_index(runtime, json_string(action, "timer", NULL));
        if (timer_index < 0 || timers == NULL)
            return false;
        const named_timer *timer = &runtime->timers[timer_index];
        return slayer3d_timer_start(timers, json_float(action, "delay", timer->delay), timer->signal_id,
                                    timer->repeating, timer->interval) != 0;
    }

    if (SDL_strcmp(type, "property.set") == 0 || SDL_strcmp(type, "property.add") == 0)
    {
        slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
        const char *key = json_string(action, "key", NULL);
        yyjson_val *value = obj_get(action, "value");
        const char *value_from_payload = json_string(action, "value_from_payload", NULL);
        const slayer3d_value *payload_value = value_from_payload != NULL && payload != NULL
                                                  ? slayer3d_properties_get_value(payload, value_from_payload)
                                                  : NULL;
        if (actor == NULL || key == NULL || (value == NULL && payload_value == NULL))
            return false;

        if (SDL_strcmp(type, "property.add") == 0)
        {
            const slayer3d_value *existing = slayer3d_properties_get_value(actor->props, key);
            if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && payload_value != NULL &&
                payload_value->type == SLAYER3D_VALUE_INT)
            {
                slayer3d_properties_set_int(actor->props, key, existing->as_int + payload_value->as_int);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && payload_value != NULL &&
                     payload_value->type == SLAYER3D_VALUE_FLOAT)
            {
                slayer3d_properties_set_float(actor->props, key, existing->as_float + payload_value->as_float);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && payload_value != NULL &&
                     payload_value->type == SLAYER3D_VALUE_INT)
            {
                slayer3d_properties_set_float(actor->props, key, existing->as_float + (float)payload_value->as_int);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && payload_value != NULL &&
                     payload_value->type == SLAYER3D_VALUE_FLOAT)
            {
                slayer3d_properties_set_float(actor->props, key, (float)existing->as_int + payload_value->as_float);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && yyjson_is_num(value))
                slayer3d_properties_set_int(actor->props, key, existing->as_int + (int)yyjson_get_int(value));
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && yyjson_is_num(value))
                slayer3d_properties_set_float(actor->props, key, existing->as_float + (float)yyjson_get_real(value));
            else
                return false;
        }
        else if (payload_value != NULL)
        {
            copy_property_value(actor->props, key, payload_value);
        }
        else
        {
            set_actor_property_from_json(actor, key, value);
        }
        return true;
    }

    if (SDL_strcmp(type, "property.snapshot") == 0)
        return snapshot_actor_properties(runtime, action);

    if (SDL_strcmp(type, "property.restore_snapshot") == 0)
        return restore_actor_property_snapshot(runtime, action);

    if (SDL_strcmp(type, "property.animate") == 0)
        return start_property_animation_from_json(runtime, action);

    if (SDL_strcmp(type, "property.reset_defaults") == 0)
        return reset_actor_properties_to_authored_defaults(runtime, action);

    if (SDL_strcmp(type, "input.reset_bindings") == 0)
        return slayer3d_game_data_reset_menu_input_bindings(runtime, json_string(action, "menu", NULL));

    if (SDL_strcmp(type, "input.apply_profile") == 0)
    {
        char error[256] = {0};
        slayer3d_input_manager *input = runtime_input(runtime);
        const char *profile = json_string(action, "profile", NULL);
        if (!slayer3d_game_data_apply_input_profile(runtime, input, profile, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data input profile apply failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        return true;
    }

    if (SDL_strcmp(type, "input.apply_active_profile") == 0)
    {
        char error[256] = {0};
        const char *profile = NULL;
        slayer3d_input_manager *input = runtime_input(runtime);
        if (!slayer3d_game_data_apply_active_input_profile(runtime, input, &profile, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data active input profile apply failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "game data active input profile applied: profile=%s",
                    profile != NULL ? profile : "<none>");
        return true;
    }

    if (SDL_strcmp(type, "input.clear_network_input_overrides") == 0)
    {
        char error[256] = {0};
        slayer3d_input_manager *input = runtime_input(runtime);
        const char *channel = json_string(action, "channel", NULL);
        if (!slayer3d_game_data_clear_network_input_overrides(runtime, channel, input, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data network input override clear failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        return true;
    }

    if (SDL_strcmp(type, "scene_state.set") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        yyjson_val *value = obj_get(action, "value");
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0' || value == NULL)
            return false;
        return set_property_from_json_with_payload(runtime->scene_state, key, value, payload);
    }

    if (SDL_strcmp(type, "scene_state.toggle") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
            return false;
        const bool current = scene_state_bool(runtime, key, json_bool(action, "default", false));
        slayer3d_properties_set_bool(runtime->scene_state, key, !current);
        return true;
    }

    if (SDL_strcmp(type, "scene_state.cycle") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        yyjson_val *values = obj_get(action, "values");
        const size_t count = yyjson_is_arr(values) ? yyjson_arr_size(values) : 0U;
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0' || count == 0U)
            return false;

        const slayer3d_value *current = slayer3d_properties_get_value(runtime->scene_state, key);
        slayer3d_value fallback;
        if (current == NULL && json_scalar_to_value(obj_get(action, "default"), &fallback))
            current = &fallback;
        size_t next = 0U;
        for (size_t i = 0; i < count; ++i)
        {
            yyjson_val *value = yyjson_arr_get(values, i);
            if (json_value_matches_property(value, current))
            {
                next = (i + 1U) % count;
                break;
            }
        }

        return set_property_from_json(runtime->scene_state, key, yyjson_arr_get(values, next));
    }

    if (SDL_strcmp(type, "network.direct_connect.start") == 0)
    {
        const char *name = json_string(action, "name", NULL);
        const char *host_key = json_string(action, "host_key", NULL);
        const char *port_key = json_string(action, "port_key", NULL);
        const char *host = runtime != NULL && host_key != NULL
                               ? slayer3d_properties_get_string(runtime->scene_state, host_key,
                                                                json_string(action, "default_host", "127.0.0.1"))
                               : json_string(action, "host", json_string(action, "default_host", "127.0.0.1"));
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const char *port_text = runtime != NULL && port_key != NULL
                                    ? slayer3d_properties_get_string(runtime->scene_state, port_key, NULL)
                                    : NULL;
        const int port = port_text != NULL ? SDL_atoi(port_text) : json_int_or_string(action, "port", default_port);
        return slayer3d_game_data_network_direct_connect_start(
            runtime, name, host, port, json_string(action, "status_key", NULL), json_string(action, "state_key", NULL),
            json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.direct_connect.cancel") == 0)
    {
        char status[256];
        const char *status_text = json_string(action, "status", "Disconnected");
        (void)format_payload_string(payload, status_text, status, sizeof(status));
        return slayer3d_game_data_network_direct_connect_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL), status);
    }

    if (SDL_strcmp(type, "network.direct_connect.observe") == 0)
    {
        return slayer3d_game_data_network_direct_connect_publish_status(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.host.start") == 0)
    {
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const int port = json_int_or_string(action, "port", default_port);
        return slayer3d_game_data_network_host_start(
            runtime, json_string(action, "name", NULL), port,
            json_string(action, "session_name", json_string(action, "advertised_name", NULL)),
            json_string(action, "status_key", NULL), json_string(action, "endpoint_key", NULL),
            json_string(action, "peer_key", NULL), json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.host.cancel") == 0)
    {
        char status[256];
        const char *status_text = json_string(action, "status", "Not hosting");
        (void)format_payload_string(payload, status_text, status, sizeof(status));
        return slayer3d_game_data_network_host_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "endpoint_key", NULL), json_string(action, "peer_key", NULL),
            json_string(action, "connected_key", NULL), status);
    }

    if (SDL_strcmp(type, "network.host.observe") == 0)
    {
        return slayer3d_game_data_network_host_publish_status(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "endpoint_key", NULL), json_string(action, "peer_key", NULL),
            json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.start") == 0 || SDL_strcmp(type, "network.discovery.refresh") == 0)
    {
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const int port = json_int_or_string(action, "port", default_port);
        const int local_port = json_int_or_string(action, "local_port", 0);
        return slayer3d_game_data_network_discovery_start(
            runtime, json_string(action, "name", NULL), json_string(action, "host", NULL), port, local_port,
            json_string(action, "collection", NULL), json_string(action, "status_key", NULL),
            json_string(action, "count_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.observe") == 0)
    {
        return slayer3d_game_data_network_discovery_update(
            runtime, json_string(action, "name", NULL),
            json_float(action, "dt", json_float(action, "update_seconds", 0.016f)),
            json_string(action, "collection", NULL), json_string(action, "status_key", NULL),
            json_string(action, "count_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.cancel") == 0)
    {
        return slayer3d_game_data_network_discovery_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "collection", NULL),
            json_string(action, "status_key", NULL), json_string(action, "count_key", NULL),
            json_string(action, "status", "Discovery canceled"));
    }

    if (SDL_strcmp(type, "network.discovery.connect_selected") == 0)
    {
        const char *index_key = json_string(action, "selected_index_key", NULL);
        const int selected_index =
            runtime != NULL && runtime->scene_state != NULL && index_key != NULL
                ? slayer3d_properties_get_int(runtime->scene_state, index_key, json_int(action, "selected_index", 0))
                : json_int(action, "selected_index", 0);
        return slayer3d_game_data_network_discovery_connect_selected(
            runtime, json_string(action, "name", NULL), json_string(action, "collection", NULL), selected_index,
            json_string(action, "direct_connect_name", NULL), json_string(action, "host_key", NULL),
            json_string(action, "port_key", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL),
            json_string(action, "connecting_status", NULL));
    }

    if (SDL_strcmp(type, "ui.animate") == 0)
        return start_ui_animation_from_json(runtime, action);

    if (SDL_strncmp(type, "audio.", 6) == 0)
        return execute_audio_action(runtime, action, payload, type);

    if (SDL_strncmp(type, "persistence.", 12) == 0)
        return execute_persistence_action(runtime, action, type);

    if (SDL_strcmp(type, "entity.set_active") == 0)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        yyjson_val *active = obj_get(action, "active");
        if (actor == NULL || !yyjson_is_bool(active))
            return false;
        actor->active = yyjson_get_bool(active);
        return true;
    }

    if (SDL_strcmp(type, "actor.spawn") == 0)
        return execute_actor_spawn_action(runtime, action, payload);

    if (SDL_strcmp(type, "actor.despawn") == 0)
        return execute_actor_despawn_action_with_payload(runtime, action, payload);

    if (SDL_strcmp(type, "actor.despawn_by_tag") == 0)
        return execute_actor_despawn_by_tag_action(runtime, action);

    if (SDL_strcmp(type, "combat.damage") == 0)
        return execute_combat_damage_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.heal") == 0)
        return execute_combat_heal_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.kill") == 0)
        return execute_combat_kill_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.revive") == 0)
        return execute_combat_revive_action(runtime, action, payload);
    if (SDL_strcmp(type, "resource.add") == 0)
        return execute_resource_amount_action(runtime, action, payload, false);
    if (SDL_strcmp(type, "resource.consume") == 0)
        return execute_resource_amount_action(runtime, action, payload, true);
    if (SDL_strcmp(type, "resource.set") == 0)
        return execute_resource_set_action(runtime, action, payload);
    if (SDL_strcmp(type, "pickup.collect") == 0)
        return execute_pickup_collect_action(runtime, action, payload);
    if (SDL_strcmp(type, "resource.station.use") == 0)
        return execute_resource_station_use_action(runtime, action, payload);
    if (SDL_strcmp(type, "status_effect.apply") == 0)
        return execute_status_effect_apply_action(runtime, action, payload);
    if (SDL_strcmp(type, "weapon.reload") == 0)
        return execute_weapon_reload_action(runtime, action, payload);
    if (SDL_strcmp(type, "weapon.hitscan") == 0)
        return execute_weapon_hitscan_action(runtime, action, payload);
    if (SDL_strcmp(type, "interaction.use") == 0)
        return execute_interaction_use_action(runtime, action, payload);
    if (SDL_strcmp(type, "effect.explosion") == 0)
        return execute_effect_explosion_action(runtime, action, payload);
    if (SDL_strcmp(type, "noise.emit") == 0)
        return execute_noise_emit_action(runtime, action, payload);

    if (SDL_strcmp(type, "projectile.fire") == 0)
        return execute_projectile_fire_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.launch") == 0 || SDL_strcmp(type, "controller.fps_sector.launch") == 0)
        return execute_fps_controller_launch_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.teleport") == 0 || SDL_strcmp(type, "controller.fps_sector.teleport") == 0)
        return execute_fps_controller_teleport_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.push") == 0)
        return execute_fps_controller_push_action(runtime, action, payload);
    if (SDL_strcmp(type, "grid.spawn_from_glyphs") == 0)
        return execute_grid_spawn_from_glyphs_action(runtime, action);
    if (SDL_strcmp(type, "grid.spawn_runs_from_glyphs") == 0)
        return execute_grid_spawn_runs_from_glyphs_action(runtime, action);
    if (SDL_strcmp(type, "grid.pickup_layer.reset") == 0)
        return execute_grid_pickup_layer_reset_action(runtime, action);

    if (SDL_strcmp(type, "sector_door.open") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "open");
    if (SDL_strcmp(type, "sector_door.close") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "close");
    if (SDL_strcmp(type, "sector_door.toggle") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "toggle");
    if (SDL_strcmp(type, "sector_door.interact") == 0)
        return execute_sector_door_interact_action(runtime, action);
    if (SDL_strcmp(type, "sector_lighting.set") == 0)
    {
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        yyjson_val *color_json = obj_get(action, "color");
        if (yyjson_is_arr(color_json) && yyjson_arr_size(color_json) == 3U)
        {
            (void)json_float_array(color_json, color, 3, color);
            color[3] = 1.0f;
        }
        else
        {
            (void)json_float_array(color_json, color, 4, color);
        }
        char error[256] = {0};
        char sector_index_text[32];
        const char *sector = json_string(action, "sector", NULL);
        yyjson_val *sector_index_value = obj_get(action, "sector_index");
        if (sector == NULL && yyjson_is_int(sector_index_value))
        {
            SDL_snprintf(sector_index_text, sizeof(sector_index_text), "%d", (int)yyjson_get_int(sector_index_value));
            sector = sector_index_text;
        }
        const bool ok = slayer3d_game_data_set_sector_lighting(runtime, json_string(action, "sector_level", NULL),
                                                               sector, json_float(action, "level", 255.0f), color,
                                                               error, (int)sizeof(error));
        if (!ok)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sector_lighting.set failed: %s",
                        error[0] != '\0' ? error : "unknown error");
        return ok;
    }

    if (SDL_strcmp(type, "transform.set_position") == 0)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        if (actor == NULL)
            return false;
        actor_set_position(actor, json_vec3(action, "position", actor->position));
        return true;
    }

    if (SDL_strcmp(type, "camera.toggle") == 0)
    {
        const char *camera = json_string(action, "camera", NULL);
        const char *fallback = json_string(action, "fallback", NULL);
        runtime->active_camera =
            runtime->active_camera != NULL && camera != NULL && SDL_strcmp(runtime->active_camera, camera) == 0
                ? fallback
                : camera;
        return runtime->active_camera != NULL;
    }

    if (SDL_strcmp(type, "camera.set") == 0)
    {
        const char *camera = json_string(action, "camera", NULL);
        if (camera == NULL)
            return false;
        runtime->active_camera = camera;
        return true;
    }

    if (SDL_strcmp(type, "scene.set") == 0)
    {
        yyjson_val *authored_payload = obj_get(action, "payload");
        if (authored_payload == NULL)
            return slayer3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL),
                                                                    payload);

        slayer3d_properties *scene_payload = properties_from_json_payload(authored_payload, payload);
        if (scene_payload == NULL)
            return false;
        const bool ok = slayer3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL),
                                                                         scene_payload);
        slayer3d_properties_destroy(scene_payload);
        return ok;
    }

    if (SDL_strcmp(type, "adapter.invoke") == 0)
    {
        const char *adapter_name = json_string(action, "adapter", NULL);
        adapter_entry *adapter = find_adapter(runtime, adapter_name);
        if (adapter == NULL)
            return false;
        const char *target_name = json_string(action, "target", NULL);
        if ((target_name == NULL || target_name[0] == '\0') && payload != NULL)
            target_name = slayer3d_properties_get_string(payload, "actor_name", NULL);
        slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, target_name);
        return invoke_adapter(runtime, adapter, target, payload);
    }

    if (SDL_strcmp(type, "branch") == 0)
    {
        yyjson_val *condition = obj_get(action, "if");
        const bool passed = eval_data_condition(runtime, condition, NULL);
        return execute_action_array(runtime, obj_get(action, passed ? "then" : "else"), payload);
    }

    return false;
}

bool execute_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions, const slayer3d_properties *payload)
{
    if (!yyjson_is_arr(actions))
        return false;

    actor_lifecycle_defer_begin(runtime);
    bool ok = true;
    for (size_t i = 0; i < yyjson_arr_size(actions); ++i)
    {
        yyjson_val *action = yyjson_arr_get(actions, i);
        if (yyjson_is_obj(action))
            ok = execute_one_action(runtime, action, payload) && ok;
    }
    actor_lifecycle_defer_end(runtime);
    return ok;
}

bool execute_optional_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                   const slayer3d_properties *payload)
{
    if (actions == NULL)
        return true;
    return execute_action_array(runtime, actions, payload);
}

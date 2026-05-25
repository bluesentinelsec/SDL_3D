/**
 * @file game_data_validation_network.c
 * @brief Network validation for JSON-authored game data.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "network_replication_schema.h"
#include "slayer3d_crypto.h"

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static bool network_is_non_empty_string(yyjson_val *object, const char *key)
{
    const char *value = json_string(object, key);
    return value != NULL && value[0] != '\0';
}

static bool require_network_string_entry(validation_context *ctx, yyjson_val *map, const char *path, const char *label,
                                         const char *name);

bool validate_network_port_value(validation_context *ctx, yyjson_val *value, const char *json_path, const char *label)
{
    if (value == NULL)
        return true;
    if (yyjson_is_int(value))
    {
        const int port = (int)yyjson_get_int(value);
        if (port > 0 && port <= 65535)
            return true;
    }
    else if (yyjson_is_str(value) && yyjson_get_str(value) != NULL && yyjson_get_str(value)[0] != '\0')
    {
        return true;
    }
    return validation_error(ctx, json_path, "%s must be a non-empty string or integer 1..65535", label);
}

static bool is_replication_direction(const char *direction, bool allow_bidirectional)
{
    return direction != NULL &&
           (SDL_strcmp(direction, "host_to_client") == 0 || SDL_strcmp(direction, "client_to_host") == 0 ||
            (allow_bidirectional && SDL_strcmp(direction, "bidirectional") == 0));
}

static bool is_replication_property_path(const char *path)
{
    if (path == NULL || path[0] == '\0' || path[0] == '.' || path[SDL_strlen(path) - 1u] == '.')
        return false;

    bool previous_dot = false;
    for (const char *p = path; *p != '\0'; ++p)
    {
        const char c = *p;
        const bool valid =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!valid)
            return false;
        if (c == '.' && previous_dot)
            return false;
        previous_dot = c == '.';
    }
    return true;
}

static void network_hash_update(slayer3d_crypto_hash32_state *state, const char *label, const char *value)
{
    static const char sep = '\0';
    static const char null_marker = '\1';
    slayer3d_crypto_hash32_update(state, label, SDL_strlen(label));
    slayer3d_crypto_hash32_update(state, &sep, 1u);
    if (value != NULL)
    {
        slayer3d_crypto_hash32_update(state, value, SDL_strlen(value));
    }
    else
    {
        slayer3d_crypto_hash32_update(state, &null_marker, 1u);
    }
    slayer3d_crypto_hash32_update(state, &sep, 1u);
}

static void network_hash_update_int(slayer3d_crypto_hash32_state *state, const char *label, Sint64 value)
{
    char buffer[32];
    SDL_snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    network_hash_update(state, label, buffer);
}

static bool validate_network_actor_fields(validation_context *ctx, yyjson_val *fields, const char *json_path)
{
    if (!yyjson_is_arr(fields) || yyjson_arr_size(fields) == 0)
        return validation_error(ctx, json_path, "network actor fields must be a non-empty array");

    name_table field_names;
    SDL_zero(field_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(fields); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *field = yyjson_arr_get(fields, i);
        slayer3d_replication_field_descriptor descriptor;
        if (!slayer3d_replication_field_descriptor_from_json(field, &descriptor))
        {
            ok = validation_error(ctx, path,
                                  "network actor field must be a built-in field string or object with path and type");
            break;
        }
        if (!is_replication_property_path(descriptor.path))
        {
            ok = validation_error(ctx, path, "network actor field path '%s' is invalid", descriptor.path);
            break;
        }
        if (slayer3d_replication_field_wire_size(descriptor.type) == 0U)
        {
            ok = validation_error(ctx, path, "unsupported network actor field type");
            break;
        }
        if (!require_unique_name(ctx, &field_names, "network actor field", descriptor.path, path))
        {
            ok = false;
            break;
        }
    }
    name_table_destroy(&field_names);
    return ok;
}

static bool validate_network_actors(validation_context *ctx, yyjson_val *actors, const char *json_path,
                                    validation_names *names)
{
    if (!yyjson_is_arr(actors) || yyjson_arr_size(actors) == 0)
        return validation_error(ctx, json_path, "network actors must be a non-empty array");

    name_table actor_names;
    SDL_zero(actor_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(actors); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *actor = yyjson_arr_get(actors, i);
        if (!yyjson_is_obj(actor))
        {
            ok = validation_error(ctx, path, "network actor entry must be an object");
            break;
        }
        const char *entity = json_string(actor, "entity");
        if (!require_ref(ctx, &names->entities, "entity", entity, path) ||
            !require_unique_name(ctx, &actor_names, "network actor", entity, path))
        {
            ok = false;
            break;
        }
        char fields_path[PATH_BUFFER_SIZE];
        format_path(fields_path, sizeof(fields_path), "%s.fields", path);
        ok = validate_network_actor_fields(ctx, obj_get(actor, "fields"), fields_path);
    }
    name_table_destroy(&actor_names);
    return ok;
}

static bool validate_network_pools(validation_context *ctx, yyjson_val *pools, const char *json_path,
                                   validation_names *names)
{
    if (!yyjson_is_arr(pools) || yyjson_arr_size(pools) == 0)
        return validation_error(ctx, json_path, "network pools must be a non-empty array");

    name_table pool_names;
    SDL_zero(pool_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(pools); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *pool = yyjson_arr_get(pools, i);
        if (!yyjson_is_obj(pool))
        {
            ok = validation_error(ctx, path, "network pool entry must be an object");
            break;
        }
        const char *pool_name = json_string(pool, "pool");
        if (!require_ref(ctx, &names->actor_pools, "actor pool", pool_name, path) ||
            !require_unique_name(ctx, &pool_names, "network pool", pool_name, path))
        {
            ok = false;
            break;
        }
        char fields_path[PATH_BUFFER_SIZE];
        format_path(fields_path, sizeof(fields_path), "%s.fields", path);
        ok = validate_network_actor_fields(ctx, obj_get(pool, "fields"), fields_path);
    }
    name_table_destroy(&pool_names);
    return ok;
}

static bool validate_network_inputs(validation_context *ctx, yyjson_val *inputs, const char *json_path,
                                    validation_names *names)
{
    if (!yyjson_is_arr(inputs) || yyjson_arr_size(inputs) == 0)
        return validation_error(ctx, json_path, "network inputs must be a non-empty array");

    name_table input_names;
    SDL_zero(input_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(inputs); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *input = yyjson_arr_get(inputs, i);
        if (!yyjson_is_obj(input))
        {
            ok = validation_error(ctx, path, "network input entry must be an object");
            break;
        }
        const char *action = json_string(input, "action");
        if (!require_ref(ctx, &names->actions, "input action", action, path) ||
            !require_unique_name(ctx, &input_names, "network input action", action, path))
        {
            ok = false;
            break;
        }
    }
    name_table_destroy(&input_names);
    return ok;
}

static bool validate_network_scene_state(validation_context *ctx, yyjson_val *network)
{
    yyjson_val *scene_state = obj_get(network, "scene_state");
    if (scene_state == NULL)
        return true;
    if (!yyjson_is_obj(scene_state))
        return validation_error(ctx, "$.network.scene_state", "network scene_state must be an object");

    yyjson_val *scope_key;
    yyjson_obj_iter scope_iter;
    yyjson_obj_iter_init(scene_state, &scope_iter);
    while ((scope_key = yyjson_obj_iter_next(&scope_iter)) != NULL)
    {
        const char *scope_name = yyjson_get_str(scope_key);
        yyjson_val *scope = yyjson_obj_iter_get_val(scope_key);
        char scope_path[PATH_BUFFER_SIZE];
        format_path(scope_path, sizeof(scope_path), "$.network.scene_state.%s",
                    scope_name != NULL ? scope_name : "<invalid>");
        if (scope_name == NULL || scope_name[0] == '\0')
            return validation_error(ctx, scope_path, "network scene_state scope must have a non-empty name");
        if (!yyjson_is_obj(scope))
            return validation_error(ctx, scope_path, "network scene_state scope must be an object");

        yyjson_val *key;
        yyjson_obj_iter key_iter;
        yyjson_obj_iter_init(scope, &key_iter);
        while ((key = yyjson_obj_iter_next(&key_iter)) != NULL)
        {
            const char *name = yyjson_get_str(key);
            yyjson_val *value = yyjson_obj_iter_get_val(key);
            char key_path[PATH_BUFFER_SIZE];
            format_path(key_path, sizeof(key_path), "%s.%s", scope_path, name != NULL ? name : "<invalid>");
            if (name == NULL || name[0] == '\0')
                return validation_error(ctx, key_path, "network scene_state key name must be non-empty");
            if (!yyjson_is_str(value) || yyjson_get_len(value) == 0)
                return validation_error(ctx, key_path, "network scene_state key value must be a non-empty string");
        }
    }

    return true;
}

static bool network_managed_runtime_enabled_json(yyjson_val *network)
{
    yyjson_val *enabled = obj_get(obj_get(obj_get(network, "session_flow"), "managed_runtime"), "enabled");
    return yyjson_is_bool(enabled) && yyjson_get_bool(enabled);
}

static bool validate_managed_network_scene_state(validation_context *ctx, yyjson_val *network)
{
    if (!network_managed_runtime_enabled_json(network))
        return true;

    yyjson_val *scene_state = obj_get(network, "scene_state");
    yyjson_val *host = obj_get(scene_state, "host");
    yyjson_val *direct_connect = obj_get(scene_state, "direct_connect");
    const char *host_keys[] = {"status", "endpoint", "peer", "connected"};
    const char *direct_connect_keys[] = {"status", "state", "connected"};

    for (size_t i = 0U; i < SDL_arraysize(host_keys); ++i)
    {
        if (!require_network_string_entry(ctx, host, "$.network.scene_state.host", "scene_state host key",
                                          host_keys[i]))
        {
            return false;
        }
    }
    for (size_t i = 0U; i < SDL_arraysize(direct_connect_keys); ++i)
    {
        if (!require_network_string_entry(ctx, direct_connect, "$.network.scene_state.direct_connect",
                                          "scene_state direct_connect key", direct_connect_keys[i]))
        {
            return false;
        }
    }

    return true;
}

static bool validate_network_session_string_map(validation_context *ctx, yyjson_val *map, const char *json_path,
                                                const char *label, const name_table *scene_names)
{
    if (map == NULL)
        return true;
    if (!yyjson_is_obj(map))
        return validation_error(ctx, json_path, "network session_flow %s must be an object", label);

    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(map, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.%s", json_path, name != NULL ? name : "<invalid>");
        if (name == NULL || name[0] == '\0')
            return validation_error(ctx, path, "network session_flow %s key must be non-empty", label);
        if (!yyjson_is_str(value) || yyjson_get_len(value) == 0)
            return validation_error(ctx, path, "network session_flow %s value must be a non-empty string", label);
        if (scene_names != NULL && !require_ref(ctx, scene_names, "scene", yyjson_get_str(value), path))
            return false;
    }

    return true;
}

static bool require_network_string_entry(validation_context *ctx, yyjson_val *map, const char *path, const char *label,
                                         const char *name)
{
    char entry_path[PATH_BUFFER_SIZE];
    format_path(entry_path, sizeof(entry_path), "%s.%s", path, name);
    if (map == NULL || !yyjson_is_obj(map) || !network_is_non_empty_string(map, name))
        return validation_error(ctx, entry_path, "managed network requires %s '%s'", label, name);
    return true;
}

static bool require_network_group_string_entry(validation_context *ctx, yyjson_val *groups, const char *path,
                                               const char *label, const char *group_name, const char *name)
{
    yyjson_val *group = obj_get(groups, group_name);
    char entry_path[PATH_BUFFER_SIZE];
    format_path(entry_path, sizeof(entry_path), "%s.%s.%s", path, group_name, name);
    if (group == NULL || !yyjson_is_obj(group) || !network_is_non_empty_string(group, name))
        return validation_error(ctx, entry_path, "managed network requires %s '%s.%s'", label, group_name, name);
    return true;
}

static bool validate_network_managed_keep_alive_scenes(validation_context *ctx, yyjson_val *managed, yyjson_val *scenes)
{
    yyjson_val *keep_alive = obj_get(managed, "keep_alive_scenes");
    if (keep_alive == NULL)
        return true;
    if (!yyjson_is_obj(keep_alive))
        return validation_error(ctx, "$.network.session_flow.managed_runtime.keep_alive_scenes",
                                "managed network keep_alive_scenes must be an object");

    yyjson_val *session_key;
    yyjson_obj_iter session_iter;
    yyjson_obj_iter_init(keep_alive, &session_iter);
    while ((session_key = yyjson_obj_iter_next(&session_iter)) != NULL)
    {
        const char *session_name = yyjson_get_str(session_key);
        yyjson_val *list = yyjson_obj_iter_get_val(session_key);
        char session_path[PATH_BUFFER_SIZE];
        format_path(session_path, sizeof(session_path), "$.network.session_flow.managed_runtime.keep_alive_scenes.%s",
                    session_name != NULL ? session_name : "<invalid>");
        if (session_name == NULL || session_name[0] == '\0')
            return validation_error(ctx, session_path, "managed network keep-alive session name must be non-empty");
        if (!yyjson_is_arr(list) || yyjson_arr_size(list) == 0)
            return validation_error(ctx, session_path, "managed network keep-alive scenes must be a non-empty array");

        for (size_t i = 0U; i < yyjson_arr_size(list); ++i)
        {
            yyjson_val *entry = yyjson_arr_get(list, i);
            const char *scene_semantic = yyjson_is_str(entry) ? yyjson_get_str(entry) : NULL;
            char entry_path[PATH_BUFFER_SIZE];
            format_path(entry_path, sizeof(entry_path), "%s[%zu]", session_path, i);
            if (scene_semantic == NULL || scene_semantic[0] == '\0')
                return validation_error(ctx, entry_path, "managed network keep-alive scene must be a non-empty string");
            if (!network_is_non_empty_string(scenes, scene_semantic))
                return validation_error(ctx, entry_path,
                                        "managed network keep-alive scene must reference session_flow.scenes");
        }
    }

    return true;
}

static bool validate_network_managed_runtime(validation_context *ctx, yyjson_val *flow)
{
    yyjson_val *managed = obj_get(flow, "managed_runtime");
    if (managed == NULL)
        return true;
    if (!yyjson_is_obj(managed))
        return validation_error(ctx, "$.network.session_flow.managed_runtime",
                                "managed network runtime must be an object");

    yyjson_val *enabled = obj_get(managed, "enabled");
    if (enabled != NULL && !yyjson_is_bool(enabled))
        return validation_error(ctx, "$.network.session_flow.managed_runtime.enabled",
                                "managed network enabled must be boolean");

    yyjson_val *ack_delay = obj_get(managed, "termination_ack_delay_seconds");
    if (ack_delay != NULL && (!yyjson_is_num(ack_delay) || yyjson_get_real(ack_delay) < 0.0))
    {
        return validation_error(ctx, "$.network.session_flow.managed_runtime.termination_ack_delay_seconds",
                                "managed network termination_ack_delay_seconds must be a non-negative number");
    }

    yyjson_val *scenes = obj_get(flow, "scenes");
    if (!validate_network_managed_keep_alive_scenes(ctx, managed, scenes))
        return false;

    if (enabled == NULL || !yyjson_get_bool(enabled))
        return true;

    yyjson_val *state_keys = obj_get(flow, "state_keys");
    yyjson_val *state_values = obj_get(flow, "state_values");
    yyjson_val *events = obj_get(flow, "events");
    const char *required_scenes[] = {"play", "host_lobby", "direct_connect", "discovery"};
    const char *required_state_keys[] = {"match_mode", "network_role", "network_flow", "match_termination_active"};
    const char *required_events[] = {"host_start_game",           "client_start_game",
                                     "client_state_before_start", "host_match_terminated",
                                     "client_match_terminated",   "host_client_disconnected",
                                     "client_connection_closed",  "network_match_termination_ack"};

    for (size_t i = 0U; i < SDL_arraysize(required_scenes); ++i)
    {
        if (!require_network_string_entry(ctx, scenes, "$.network.session_flow.scenes", "session scene",
                                          required_scenes[i]))
        {
            return false;
        }
    }
    for (size_t i = 0U; i < SDL_arraysize(required_state_keys); ++i)
    {
        if (!require_network_string_entry(ctx, state_keys, "$.network.session_flow.state_keys", "session state key",
                                          required_state_keys[i]))
        {
            return false;
        }
    }

    if (!require_network_group_string_entry(ctx, state_values, "$.network.session_flow.state_values",
                                            "session state value", "match_mode", "network") ||
        !require_network_group_string_entry(ctx, state_values, "$.network.session_flow.state_values",
                                            "session state value", "network_role", "host") ||
        !require_network_group_string_entry(ctx, state_values, "$.network.session_flow.state_values",
                                            "session state value", "network_role", "client") ||
        !require_network_group_string_entry(ctx, state_values, "$.network.session_flow.state_values",
                                            "session state value", "network_flow", "host") ||
        !require_network_group_string_entry(ctx, state_values, "$.network.session_flow.state_values",
                                            "session state value", "network_flow", "direct"))
    {
        return false;
    }

    for (size_t i = 0U; i < SDL_arraysize(required_events); ++i)
    {
        char event_path[PATH_BUFFER_SIZE];
        format_path(event_path, sizeof(event_path), "$.network.session_flow.events.%s", required_events[i]);
        if (events == NULL || !yyjson_is_obj(events) || obj_get(events, required_events[i]) == NULL)
            return validation_error(ctx, event_path, "managed network requires session flow event '%s'",
                                    required_events[i]);
    }
    if (obj_get(managed, "keep_alive_scenes") == NULL)
    {
        return validation_error(ctx, "$.network.session_flow.managed_runtime.keep_alive_scenes",
                                "managed network requires keep_alive_scenes");
    }
    if (ack_delay == NULL)
    {
        return validation_error(ctx, "$.network.session_flow.managed_runtime.termination_ack_delay_seconds",
                                "managed network requires termination_ack_delay_seconds");
    }

    return true;
}

static bool validate_network_session_flow(validation_context *ctx, yyjson_val *network, validation_names *names)
{
    yyjson_val *flow = obj_get(network, "session_flow");
    if (flow == NULL)
        return true;
    if (!yyjson_is_obj(flow))
        return validation_error(ctx, "$.network.session_flow", "network session_flow must be an object");

    if (!validate_network_session_string_map(ctx, obj_get(flow, "scenes"), "$.network.session_flow.scenes", "scenes",
                                             &names->scenes) ||
        !validate_network_session_string_map(ctx, obj_get(flow, "state_keys"), "$.network.session_flow.state_keys",
                                             "state_keys", NULL))
    {
        return false;
    }

    yyjson_val *state_values = obj_get(flow, "state_values");
    yyjson_val *messages = obj_get(flow, "messages");
    const struct
    {
        yyjson_val *root;
        const char *path;
        const char *label;
    } grouped_maps[] = {
        {state_values, "$.network.session_flow.state_values", "state_values"},
        {messages, "$.network.session_flow.messages", "messages"},
    };

    for (size_t map_index = 0; map_index < SDL_arraysize(grouped_maps); ++map_index)
    {
        if (grouped_maps[map_index].root == NULL)
            continue;
        if (!yyjson_is_obj(grouped_maps[map_index].root))
            return validation_error(ctx, grouped_maps[map_index].path, "network session_flow %s must be an object",
                                    grouped_maps[map_index].label);

        yyjson_val *group_key;
        yyjson_obj_iter group_iter;
        yyjson_obj_iter_init(grouped_maps[map_index].root, &group_iter);
        while ((group_key = yyjson_obj_iter_next(&group_iter)) != NULL)
        {
            const char *group_name = yyjson_get_str(group_key);
            yyjson_val *group = yyjson_obj_iter_get_val(group_key);
            char group_path[PATH_BUFFER_SIZE];
            format_path(group_path, sizeof(group_path), "%s.%s", grouped_maps[map_index].path,
                        group_name != NULL ? group_name : "<invalid>");
            if (group_name == NULL || group_name[0] == '\0')
                return validation_error(ctx, group_path, "network session_flow %s group must be non-empty",
                                        grouped_maps[map_index].label);
            if (!validate_network_session_string_map(ctx, group, group_path, grouped_maps[map_index].label, NULL))
                return false;
        }
    }

    yyjson_val *events = obj_get(flow, "events");
    if (events != NULL)
    {
        if (!yyjson_is_obj(events))
            return validation_error(ctx, "$.network.session_flow.events",
                                    "network session_flow events must be an object");
        yyjson_val *event_key;
        yyjson_obj_iter event_iter;
        yyjson_obj_iter_init(events, &event_iter);
        while ((event_key = yyjson_obj_iter_next(&event_iter)) != NULL)
        {
            const char *event_name = yyjson_get_str(event_key);
            yyjson_val *event = yyjson_obj_iter_get_val(event_key);
            char event_path[PATH_BUFFER_SIZE];
            format_path(event_path, sizeof(event_path), "$.network.session_flow.events.%s",
                        event_name != NULL ? event_name : "<invalid>");
            if (event_name == NULL || event_name[0] == '\0')
                return validation_error(ctx, event_path, "network session_flow event name must be non-empty");
            if (yyjson_is_arr(event))
            {
                if (!validate_action_array(ctx, event, event_path, names))
                    return false;
            }
            else if (yyjson_is_obj(event))
            {
                yyjson_val *pause = obj_get(event, "pause");
                if (pause != NULL && !yyjson_is_bool(pause))
                    return validation_error(ctx, event_path, "network session_flow event pause must be boolean");
                yyjson_val *actions = obj_get(event, "actions");
                if (actions != NULL)
                {
                    char actions_path[PATH_BUFFER_SIZE];
                    format_path(actions_path, sizeof(actions_path), "%s.actions", event_path);
                    if (!validate_action_array(ctx, actions, actions_path, names))
                        return false;
                }
            }
            else
            {
                return validation_error(ctx, event_path,
                                        "network session_flow event must be an action array or object");
            }
        }
    }

    return validate_network_managed_runtime(ctx, flow);
}

static bool validate_network_runtime_binding_map(validation_context *ctx, yyjson_val *map, const char *json_path,
                                                 const char *label, const name_table *references,
                                                 bool require_unique_values)
{
    if (map == NULL)
        return true;
    if (!yyjson_is_obj(map))
        return validation_error(ctx, json_path, "network runtime_bindings %s must be an object", label);

    name_table values = {0};
    bool ok = true;
    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(map, &iter);
    while (ok && (key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.%s", json_path, name != NULL ? name : "<invalid>");
        if (name == NULL || name[0] == '\0')
        {
            ok = validation_error(ctx, path, "network runtime_bindings %s key must be non-empty", label);
        }
        else if (!yyjson_is_str(value) || yyjson_get_len(value) == 0)
        {
            ok = validation_error(ctx, path, "network runtime_bindings %s value must be a non-empty string", label);
        }
        else if (!require_ref(ctx, references, label, yyjson_get_str(value), path))
        {
            ok = false;
        }
        else if (require_unique_values &&
                 !require_unique_name(ctx, &values, "network runtime binding value", yyjson_get_str(value), path))
        {
            ok = false;
        }
    }

    name_table_destroy(&values);
    return ok;
}

static bool validate_network_runtime_pause_binding(validation_context *ctx, yyjson_val *pause, validation_names *names)
{
    if (pause == NULL)
        return true;
    if (!yyjson_is_obj(pause))
        return validation_error(ctx, "$.network.runtime_bindings.pause",
                                "network runtime_bindings pause must be an object");

    if (!require_ref(ctx, &names->actions, "input action", json_string(pause, "action"),
                     "$.network.runtime_bindings.pause.action"))
    {
        return false;
    }

    yyjson_val *state = obj_get(pause, "state");
    if (!yyjson_is_obj(state))
        return validation_error(ctx, "$.network.runtime_bindings.pause.state",
                                "network runtime_bindings pause state must be an object");
    if (!require_ref(ctx, &names->entities, "entity", json_string(state, "actor"),
                     "$.network.runtime_bindings.pause.state.actor"))
    {
        return false;
    }
    if (!network_is_non_empty_string(state, "property"))
        return validation_error(ctx, "$.network.runtime_bindings.pause.state.property",
                                "network runtime_bindings pause state property must be a non-empty string");

    return true;
}

static bool require_network_runtime_binding(validation_context *ctx, yyjson_val *bindings, const char *section,
                                            const char *name, const char *label, const name_table *references)
{
    yyjson_val *map = obj_get(bindings, section);
    char path[PATH_BUFFER_SIZE];
    format_path(path, sizeof(path), "$.network.runtime_bindings.%s.%s", section, name);
    const char *value = json_string(map, name);
    if (value == NULL || value[0] == '\0')
        return validation_error(ctx, path, "managed network requires runtime binding '%s.%s'", section, name);
    return require_ref(ctx, references, label, value, path);
}

static bool validate_managed_network_runtime_bindings(validation_context *ctx, yyjson_val *bindings,
                                                      const name_table *replication_names,
                                                      const name_table *control_names, validation_names *names)
{
    const char *replication_bindings[] = {"state_snapshot", "client_input"};
    const char *control_bindings[] = {"start_game", "pause_request", "resume_request", "disconnect"};
    const char *action_bindings[] = {"menu_select", "camera_toggle"};
    const char *signal_bindings[] = {"lobby_start", "camera_toggle"};

    for (size_t i = 0U; i < SDL_arraysize(replication_bindings); ++i)
    {
        if (!require_network_runtime_binding(ctx, bindings, "replication", replication_bindings[i],
                                             "network replication", replication_names))
        {
            return false;
        }
    }
    for (size_t i = 0U; i < SDL_arraysize(control_bindings); ++i)
    {
        if (!require_network_runtime_binding(ctx, bindings, "controls", control_bindings[i], "network control message",
                                             control_names))
        {
            return false;
        }
    }
    for (size_t i = 0U; i < SDL_arraysize(action_bindings); ++i)
    {
        if (!require_network_runtime_binding(ctx, bindings, "actions", action_bindings[i], "input action",
                                             &names->actions))
        {
            return false;
        }
    }
    for (size_t i = 0U; i < SDL_arraysize(signal_bindings); ++i)
    {
        if (!require_network_runtime_binding(ctx, bindings, "signals", signal_bindings[i], "signal", &names->signals))
            return false;
    }
    if (obj_get(bindings, "pause") == NULL)
    {
        return validation_error(ctx, "$.network.runtime_bindings.pause",
                                "managed network requires runtime_bindings.pause");
    }

    return true;
}

static bool validate_network_runtime_bindings(validation_context *ctx, yyjson_val *network,
                                              const name_table *replication_names, const name_table *control_names,
                                              validation_names *names)
{
    yyjson_val *bindings = obj_get(network, "runtime_bindings");
    const bool managed_required = network_managed_runtime_enabled_json(network);
    if (bindings == NULL)
    {
        if (managed_required)
            return validation_error(ctx, "$.network.runtime_bindings", "managed network requires runtime_bindings");
        return true;
    }
    if (!yyjson_is_obj(bindings))
        return validation_error(ctx, "$.network.runtime_bindings", "network runtime_bindings must be an object");

    if (!validate_network_runtime_binding_map(ctx, obj_get(bindings, "replication"),
                                              "$.network.runtime_bindings.replication", "network replication",
                                              replication_names, false) ||
        !validate_network_runtime_binding_map(ctx, obj_get(bindings, "controls"), "$.network.runtime_bindings.controls",
                                              "network control message", control_names, true) ||
        !validate_network_runtime_binding_map(ctx, obj_get(bindings, "actions"), "$.network.runtime_bindings.actions",
                                              "input action", &names->actions, false) ||
        !validate_network_runtime_binding_map(ctx, obj_get(bindings, "signals"), "$.network.runtime_bindings.signals",
                                              "signal", &names->signals, false) ||
        !validate_network_runtime_pause_binding(ctx, obj_get(bindings, "pause"), names))
    {
        return false;
    }

    return !managed_required ||
           validate_managed_network_runtime_bindings(ctx, bindings, replication_names, control_names, names);
}

static bool is_network_diagnostic_level(const char *level)
{
    return level == NULL || SDL_strcmp(level, "debug") == 0 || SDL_strcmp(level, "info") == 0 ||
           SDL_strcmp(level, "warn") == 0 || SDL_strcmp(level, "warning") == 0 || SDL_strcmp(level, "error") == 0 ||
           SDL_strcmp(level, "critical") == 0;
}

static bool validate_network_diagnostics(validation_context *ctx, yyjson_val *network,
                                         const name_table *replication_names)
{
    yyjson_val *diagnostics = obj_get(network, "diagnostics");
    if (diagnostics == NULL)
        return true;
    if (!yyjson_is_obj(diagnostics))
        return validation_error(ctx, "$.network.diagnostics", "network diagnostics must be an object");

    yyjson_val *snapshots = obj_get(diagnostics, "snapshots");
    if (snapshots == NULL)
        return true;
    if (!yyjson_is_arr(snapshots))
        return validation_error(ctx, "$.network.diagnostics.snapshots",
                                "network diagnostics snapshots must be an array");

    name_table diagnostic_names = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(snapshots); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.network.diagnostics.snapshots[%zu]", i);
        yyjson_val *entry = yyjson_arr_get(snapshots, i);
        if (!yyjson_is_obj(entry))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic must be an object");
            break;
        }
        if (!require_unique_name(ctx, &diagnostic_names, "network snapshot diagnostic", json_string(entry, "name"),
                                 path) ||
            !require_ref(ctx, replication_names, "network replication", json_string(entry, "replication"), path))
        {
            ok = false;
            break;
        }
        yyjson_val *enabled = obj_get(entry, "enabled");
        if (enabled != NULL && !yyjson_is_bool(enabled))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic enabled must be boolean");
            break;
        }
        yyjson_val *include_session_state = obj_get(entry, "include_session_state");
        if (include_session_state != NULL && !yyjson_is_bool(include_session_state))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic include_session_state must be boolean");
            break;
        }
        yyjson_val *cadence = obj_get(entry, "cadence_seconds");
        if (cadence != NULL && (!yyjson_is_num(cadence) || yyjson_get_real(cadence) < 0.0))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic cadence_seconds must be non-negative");
            break;
        }
        const char *level = json_string(entry, "level");
        if (!is_network_diagnostic_level(level))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic level is unsupported");
            break;
        }
        yyjson_val *message = obj_get(entry, "message");
        if (message != NULL && (!yyjson_is_str(message) || yyjson_get_len(message) == 0))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic message must be a non-empty string");
            break;
        }
    }

    name_table_destroy(&diagnostic_names);
    return ok;
}

bool validate_network(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *network = obj_get(root, "network");
    if (network == NULL)
        return true;
    if (!yyjson_is_obj(network))
        return validation_error(ctx, "$.network", "network must be an object");

    yyjson_val *protocol = obj_get(network, "protocol");
    if (!yyjson_is_obj(protocol))
        return validation_error(ctx, "$.network.protocol", "network protocol must be an object");
    if (!network_is_non_empty_string(protocol, "id"))
        return validation_error(ctx, "$.network.protocol.id", "network protocol id must be a non-empty string");
    yyjson_val *version = obj_get(protocol, "version");
    if (!yyjson_is_int(version) || yyjson_get_sint(version) < 1)
        return validation_error(ctx, "$.network.protocol.version",
                                "network protocol version must be a positive integer");
    const char *transport = json_string(protocol, "transport");
    if (transport == NULL || SDL_strcmp(transport, "udp") != 0)
        return validation_error(ctx, "$.network.protocol.transport", "network protocol transport must be udp");
    yyjson_val *tick_rate = obj_get(protocol, "tick_rate");
    if (!yyjson_is_int(tick_rate) || yyjson_get_sint(tick_rate) <= 0)
        return validation_error(ctx, "$.network.protocol.tick_rate",
                                "network protocol tick_rate must be a positive integer");
    if (!validate_network_scene_state(ctx, network))
        return false;
    if (!validate_network_session_flow(ctx, network, names))
        return false;
    if (!validate_managed_network_scene_state(ctx, network))
        return false;

    yyjson_val *replication = obj_get(network, "replication");
    if (!yyjson_is_arr(replication) || yyjson_arr_size(replication) == 0)
        return validation_error(ctx, "$.network.replication", "network replication must be a non-empty array");

    name_table replication_names;
    SDL_zero(replication_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(replication); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.network.replication[%zu]", i);
        yyjson_val *entry = yyjson_arr_get(replication, i);
        if (!yyjson_is_obj(entry))
        {
            ok = validation_error(ctx, path, "network replication entry must be an object");
            break;
        }
        if (!require_unique_name(ctx, &replication_names, "network replication", json_string(entry, "name"), path))
        {
            ok = false;
            break;
        }
        const char *direction = json_string(entry, "direction");
        if (!is_replication_direction(direction, false))
        {
            ok = validation_error(ctx, path, "network replication direction must be host_to_client or client_to_host");
            break;
        }
        yyjson_val *rate = obj_get(entry, "rate");
        if (!yyjson_is_int(rate) || yyjson_get_sint(rate) <= 0)
        {
            ok = validation_error(ctx, path, "network replication rate must be a positive integer");
            break;
        }
        yyjson_val *actors = obj_get(entry, "actors");
        yyjson_val *pools = obj_get(entry, "pools");
        yyjson_val *inputs = obj_get(entry, "inputs");
        if (SDL_strcmp(direction, "host_to_client") == 0)
        {
            if (actors == NULL && pools == NULL)
            {
                ok = validation_error(ctx, path, "host_to_client network replication must declare actors or pools");
                break;
            }
            if (inputs != NULL)
            {
                ok = validation_error(ctx, path, "host_to_client network replication must not declare inputs");
                break;
            }
            char actors_path[PATH_BUFFER_SIZE];
            format_path(actors_path, sizeof(actors_path), "%s.actors", path);
            if (actors != NULL)
                ok = validate_network_actors(ctx, actors, actors_path, names);
            if (ok && pools != NULL)
            {
                char pools_path[PATH_BUFFER_SIZE];
                format_path(pools_path, sizeof(pools_path), "%s.pools", path);
                ok = validate_network_pools(ctx, pools, pools_path, names);
            }
        }
        else
        {
            if (inputs == NULL)
            {
                ok = validation_error(ctx, path, "client_to_host network replication must declare inputs");
                break;
            }
            if (actors != NULL)
            {
                ok = validation_error(ctx, path, "client_to_host network replication must not declare actors");
                break;
            }
            if (pools != NULL)
            {
                ok = validation_error(ctx, path, "client_to_host network replication must not declare pools");
                break;
            }
            char inputs_path[PATH_BUFFER_SIZE];
            format_path(inputs_path, sizeof(inputs_path), "%s.inputs", path);
            ok = validate_network_inputs(ctx, inputs, inputs_path, names);
        }
    }
    if (!ok)
    {
        name_table_destroy(&replication_names);
        return false;
    }

    yyjson_val *controls = obj_get(network, "control_messages");
    if (controls != NULL && !yyjson_is_arr(controls))
    {
        name_table_destroy(&replication_names);
        return validation_error(ctx, "$.network.control_messages", "network control_messages must be an array");
    }

    name_table control_names;
    SDL_zero(control_names);
    for (size_t i = 0; ok && yyjson_is_arr(controls) && i < yyjson_arr_size(controls); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.network.control_messages[%zu]", i);
        yyjson_val *control = yyjson_arr_get(controls, i);
        if (!yyjson_is_obj(control))
        {
            ok = validation_error(ctx, path, "network control message must be an object");
            break;
        }
        if (!require_unique_name(ctx, &control_names, "network control message", json_string(control, "name"), path))
        {
            ok = false;
            break;
        }
        if (!is_replication_direction(json_string(control, "direction"), true))
        {
            ok = validation_error(ctx, path,
                                  "network control message direction must be host_to_client, client_to_host, or "
                                  "bidirectional");
            break;
        }
        if (!require_ref(ctx, &names->signals, "signal", json_string(control, "signal"), path))
        {
            ok = false;
            break;
        }
    }
    if (ok)
        ok = validate_network_runtime_bindings(ctx, network, &replication_names, &control_names, names);
    if (ok)
        ok = validate_network_diagnostics(ctx, network, &replication_names);
    name_table_destroy(&control_names);
    name_table_destroy(&replication_names);
    return ok;
}

static void hash_network_actor_fields(slayer3d_crypto_hash32_state *state, yyjson_val *fields)
{
    network_hash_update_int(state, "field_count", (Sint64)yyjson_arr_size(fields));
    for (size_t i = 0; yyjson_is_arr(fields) && i < yyjson_arr_size(fields); ++i)
    {
        slayer3d_replication_field_descriptor descriptor;
        if (slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, i), &descriptor))
        {
            network_hash_update(state, "field.path", descriptor.path);
            network_hash_update(state, "field.type", slayer3d_replication_field_type_name(descriptor.type));
        }
    }
}

static Sint64 network_actor_pool_capacity(yyjson_val *root, const char *pool_name)
{
    yyjson_val *pools = obj_get(root, "actor_pools");
    for (size_t i = 0; yyjson_is_arr(pools) && i < yyjson_arr_size(pools); ++i)
    {
        yyjson_val *pool = yyjson_arr_get(pools, i);
        if (SDL_strcmp(json_string(pool, "name"), pool_name != NULL ? pool_name : "") == 0)
            return yyjson_get_sint(obj_get(pool, "capacity"));
    }
    return 0;
}

bool slayer3d_game_data_network_schema_hash(yyjson_val *root, Uint8 out_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE],
                                            bool *out_present)
{
    if (out_present != NULL)
        *out_present = false;
    if (out_hash != NULL)
        SDL_memset(out_hash, 0, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE);
    if (!yyjson_is_obj(root))
        return false;

    yyjson_val *network = obj_get(root, "network");
    if (network == NULL)
        return true;
    if (!yyjson_is_obj(network) || out_hash == NULL)
        return false;

    if (out_present != NULL)
        *out_present = true;

    slayer3d_crypto_hash32_state state;
    slayer3d_crypto_hash32_init(&state);
    network_hash_update(&state, "schema", "slayer3d.network.replication.v0");

    yyjson_val *protocol = obj_get(network, "protocol");
    network_hash_update(&state, "protocol.id", json_string(protocol, "id"));
    network_hash_update_int(&state, "protocol.version", yyjson_get_sint(obj_get(protocol, "version")));
    network_hash_update(&state, "protocol.transport", json_string(protocol, "transport"));
    network_hash_update_int(&state, "protocol.tick_rate", yyjson_get_sint(obj_get(protocol, "tick_rate")));

    yyjson_val *replication = obj_get(network, "replication");
    network_hash_update_int(&state, "replication_count", (Sint64)yyjson_arr_size(replication));
    for (size_t i = 0; yyjson_is_arr(replication) && i < yyjson_arr_size(replication); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(replication, i);
        network_hash_update(&state, "replication.name", json_string(entry, "name"));
        network_hash_update(&state, "replication.direction", json_string(entry, "direction"));
        network_hash_update_int(&state, "replication.rate", yyjson_get_sint(obj_get(entry, "rate")));

        yyjson_val *actors = obj_get(entry, "actors");
        network_hash_update_int(&state, "actor_count", (Sint64)yyjson_arr_size(actors));
        for (size_t a = 0; yyjson_is_arr(actors) && a < yyjson_arr_size(actors); ++a)
        {
            yyjson_val *actor = yyjson_arr_get(actors, a);
            network_hash_update(&state, "actor.entity", json_string(actor, "entity"));
            hash_network_actor_fields(&state, obj_get(actor, "fields"));
        }

        yyjson_val *pools = obj_get(entry, "pools");
        network_hash_update_int(&state, "pool_count", (Sint64)yyjson_arr_size(pools));
        for (size_t p = 0; yyjson_is_arr(pools) && p < yyjson_arr_size(pools); ++p)
        {
            yyjson_val *pool = yyjson_arr_get(pools, p);
            const char *pool_name = json_string(pool, "pool");
            network_hash_update(&state, "pool.name", pool_name);
            network_hash_update_int(&state, "pool.capacity", network_actor_pool_capacity(root, pool_name));
            hash_network_actor_fields(&state, obj_get(pool, "fields"));
        }

        yyjson_val *inputs = obj_get(entry, "inputs");
        network_hash_update_int(&state, "input_count", (Sint64)yyjson_arr_size(inputs));
        for (size_t input_index = 0; yyjson_is_arr(inputs) && input_index < yyjson_arr_size(inputs); ++input_index)
        {
            yyjson_val *input = yyjson_arr_get(inputs, input_index);
            network_hash_update(&state, "input.action", json_string(input, "action"));
        }
    }

    yyjson_val *controls = obj_get(network, "control_messages");
    network_hash_update_int(&state, "control_count", (Sint64)yyjson_arr_size(controls));
    for (size_t i = 0; yyjson_is_arr(controls) && i < yyjson_arr_size(controls); ++i)
    {
        yyjson_val *control = yyjson_arr_get(controls, i);
        network_hash_update(&state, "control.name", json_string(control, "name"));
        network_hash_update(&state, "control.direction", json_string(control, "direction"));
        network_hash_update(&state, "control.signal", json_string(control, "signal"));
    }

    slayer3d_crypto_hash32_final(&state, out_hash);
    return true;
}

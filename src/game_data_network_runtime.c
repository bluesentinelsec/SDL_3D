/**
 * @file game_data_network_runtime.c
 * @brief Network runtime bindings, packets, and diagnostics.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include "network_replication_schema.h"
bool slayer3d_game_data_has_network_schema(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->has_network_schema;
}

bool slayer3d_game_data_get_network_schema_hash(const slayer3d_game_data_runtime *runtime,
                                                Uint8 out_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE])
{
    if (runtime == NULL || out_hash == NULL || !runtime->has_network_schema)
        return false;

    SDL_memcpy(out_hash, runtime->network_schema_hash, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE);
    return true;
}

bool slayer3d_game_data_get_network_scene_state_key(const slayer3d_game_data_runtime *runtime, const char *scope,
                                                    const char *name, const char **out_key)
{
    if (out_key != NULL)
        *out_key = NULL;
    if (runtime == NULL || scope == NULL || scope[0] == '\0' || name == NULL || name[0] == '\0' || out_key == NULL)
    {
        return false;
    }

    yyjson_val *root = runtime_root(runtime);
    yyjson_val *scene_state = obj_get(obj_get(root, "network"), "scene_state");
    yyjson_val *group = obj_get(scene_state, scope);
    const char *key = json_string(group, name, NULL);
    if (key == NULL || key[0] == '\0')
        return false;

    *out_key = key;
    return true;
}

static yyjson_val *network_session_flow_json(const slayer3d_game_data_runtime *runtime)
{
    return obj_get(obj_get(runtime_root(runtime), "network"), "session_flow");
}

static bool game_data_get_network_session_string(const slayer3d_game_data_runtime *runtime, const char *section,
                                                 const char *name, const char **out_value)
{
    if (out_value != NULL)
        *out_value = NULL;
    if (runtime == NULL || section == NULL || section[0] == '\0' || name == NULL || name[0] == '\0' ||
        out_value == NULL)
    {
        return false;
    }

    const char *value = json_string(obj_get(network_session_flow_json(runtime), section), name, NULL);
    if (value == NULL || value[0] == '\0')
        return false;

    *out_value = value;
    return true;
}

bool slayer3d_game_data_get_network_session_scene(const slayer3d_game_data_runtime *runtime, const char *name,
                                                  const char **out_scene)
{
    return game_data_get_network_session_string(runtime, "scenes", name, out_scene);
}

bool slayer3d_game_data_get_network_session_state_key(const slayer3d_game_data_runtime *runtime, const char *name,
                                                      const char **out_key)
{
    return game_data_get_network_session_string(runtime, "state_keys", name, out_key);
}

bool slayer3d_game_data_get_network_session_state_value(const slayer3d_game_data_runtime *runtime, const char *group,
                                                        const char *name, const char **out_value)
{
    if (out_value != NULL)
        *out_value = NULL;
    if (runtime == NULL || group == NULL || group[0] == '\0' || name == NULL || name[0] == '\0' || out_value == NULL)
    {
        return false;
    }

    yyjson_val *groups = obj_get(network_session_flow_json(runtime), "state_values");
    const char *value = json_string(obj_get(groups, group), name, NULL);
    if (value == NULL || value[0] == '\0')
        return false;

    *out_value = value;
    return true;
}

bool slayer3d_game_data_get_network_session_message(const slayer3d_game_data_runtime *runtime, const char *group,
                                                    const char *name, const char **out_message)
{
    if (out_message != NULL)
        *out_message = NULL;
    if (runtime == NULL || group == NULL || group[0] == '\0' || name == NULL || name[0] == '\0' || out_message == NULL)
    {
        return false;
    }

    yyjson_val *groups = obj_get(network_session_flow_json(runtime), "messages");
    const char *value = json_string(obj_get(groups, group), name, NULL);
    if (value == NULL || value[0] == '\0')
        return false;

    *out_message = value;
    return true;
}

static yyjson_val *network_managed_runtime_json(const slayer3d_game_data_runtime *runtime)
{
    return obj_get(network_session_flow_json(runtime), "managed_runtime");
}

bool slayer3d_game_data_network_managed_runtime_enabled(const slayer3d_game_data_runtime *runtime)
{
    return json_bool(network_managed_runtime_json(runtime), "enabled", false);
}

bool slayer3d_game_data_get_network_managed_termination_ack_delay(const slayer3d_game_data_runtime *runtime,
                                                                  float *out_seconds)
{
    if (out_seconds != NULL)
        *out_seconds = 0.0f;
    if (runtime == NULL || out_seconds == NULL)
        return false;

    yyjson_val *value = obj_get(network_managed_runtime_json(runtime), "termination_ack_delay_seconds");
    if (!yyjson_is_num(value))
        return false;

    *out_seconds = SDL_max((float)yyjson_get_real(value), 0.0f);
    return true;
}

bool slayer3d_game_data_network_managed_keep_alive_scene_matches(const slayer3d_game_data_runtime *runtime,
                                                                 const char *session_name, const char *scene_name)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0' || scene_name == NULL ||
        scene_name[0] == '\0')
    {
        return false;
    }

    yyjson_val *keep_alive = obj_get(network_managed_runtime_json(runtime), "keep_alive_scenes");
    yyjson_val *scenes = obj_get(keep_alive, session_name);
    if (!yyjson_is_arr(scenes))
        return false;

    for (size_t i = 0U; i < yyjson_arr_size(scenes); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(scenes, i);
        const char *semantic = yyjson_is_str(entry) ? yyjson_get_str(entry) : NULL;
        const char *resolved_scene = NULL;
        if (semantic != NULL && slayer3d_game_data_get_network_session_scene(runtime, semantic, &resolved_scene) &&
            resolved_scene != NULL && SDL_strcmp(resolved_scene, scene_name) == 0)
        {
            return true;
        }
    }

    return false;
}

bool slayer3d_game_data_run_network_session_flow_event(slayer3d_game_data_runtime *runtime, slayer3d_game_context *ctx,
                                                       const char *name, const slayer3d_properties *payload,
                                                       char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || name == NULL || name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "network session flow event requires runtime and name");
        return false;
    }

    yyjson_val *events = obj_get(network_session_flow_json(runtime), "events");
    yyjson_val *event = obj_get(events, name);
    if (event == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network session flow event not found");
        return false;
    }

    yyjson_val *actions = event;
    if (yyjson_is_obj(event))
    {
        yyjson_val *pause = obj_get(event, "pause");
        if (pause != NULL && ctx == NULL)
        {
            set_error(error_buffer, error_buffer_size, "network session flow event pause requires a game context");
            return false;
        }
        if (pause != NULL && yyjson_is_bool(pause))
            ctx->paused = yyjson_get_bool(pause);
        actions = obj_get(event, "actions");
    }

    if (!execute_optional_action_array(runtime, actions, payload))
    {
        set_error(error_buffer, error_buffer_size, "network session flow event action failed");
        return false;
    }
    return true;
}

static yyjson_val *network_runtime_bindings_json(const slayer3d_game_data_runtime *runtime)
{
    return obj_get(obj_get(runtime_root(runtime), "network"), "runtime_bindings");
}

static bool game_data_get_network_runtime_binding(const slayer3d_game_data_runtime *runtime, const char *section,
                                                  const char *name, const char **out_value)
{
    if (out_value != NULL)
        *out_value = NULL;
    if (runtime == NULL || section == NULL || section[0] == '\0' || name == NULL || name[0] == '\0' ||
        out_value == NULL)
    {
        return false;
    }

    const char *value = json_string(obj_get(network_runtime_bindings_json(runtime), section), name, NULL);
    if (value == NULL || value[0] == '\0')
        return false;

    *out_value = value;
    return true;
}

bool slayer3d_game_data_get_network_runtime_replication(const slayer3d_game_data_runtime *runtime, const char *name,
                                                        const char **out_channel)
{
    return game_data_get_network_runtime_binding(runtime, "replication", name, out_channel);
}

bool slayer3d_game_data_get_network_runtime_control(const slayer3d_game_data_runtime *runtime, const char *name,
                                                    const char **out_control)
{
    return game_data_get_network_runtime_binding(runtime, "controls", name, out_control);
}

static bool game_data_get_network_runtime_binding_name_for_value(const slayer3d_game_data_runtime *runtime,
                                                                 const char *section, const char *value,
                                                                 const char **out_name)
{
    if (out_name != NULL)
        *out_name = NULL;
    if (runtime == NULL || section == NULL || section[0] == '\0' || value == NULL || value[0] == '\0' ||
        out_name == NULL)
    {
        return false;
    }

    yyjson_val *bindings = obj_get(network_runtime_bindings_json(runtime), section);
    if (!yyjson_is_obj(bindings))
        return false;

    yyjson_val *key = NULL;
    yyjson_val *binding_value = NULL;
    size_t index = 0U;
    size_t max = 0U;
    yyjson_obj_foreach(bindings, index, max, key, binding_value)
    {
        const char *semantic_name = yyjson_is_str(key) ? yyjson_get_str(key) : NULL;
        const char *concrete_value = yyjson_is_str(binding_value) ? yyjson_get_str(binding_value) : NULL;
        if (semantic_name != NULL && concrete_value != NULL && SDL_strcmp(concrete_value, value) == 0)
        {
            *out_name = semantic_name;
            return true;
        }
    }

    return false;
}

bool slayer3d_game_data_get_network_runtime_control_binding(const slayer3d_game_data_runtime *runtime,
                                                            const char *control_name, const char **out_binding)
{
    return game_data_get_network_runtime_binding_name_for_value(runtime, "controls", control_name, out_binding);
}

bool slayer3d_game_data_get_network_runtime_action(const slayer3d_game_data_runtime *runtime, const char *name,
                                                   int *out_action)
{
    const char *action_name = NULL;
    if (out_action != NULL)
        *out_action = -1;
    if (out_action == NULL || !game_data_get_network_runtime_binding(runtime, "actions", name, &action_name))
        return false;

    const int action_id = slayer3d_game_data_find_action(runtime, action_name);
    if (action_id < 0)
        return false;

    *out_action = action_id;
    return true;
}

bool slayer3d_game_data_get_network_runtime_signal(const slayer3d_game_data_runtime *runtime, const char *name,
                                                   int *out_signal)
{
    const char *signal_name = NULL;
    if (out_signal != NULL)
        *out_signal = -1;
    if (out_signal == NULL || !game_data_get_network_runtime_binding(runtime, "signals", name, &signal_name))
        return false;

    const int signal_id = slayer3d_game_data_find_signal(runtime, signal_name);
    if (signal_id < 0)
        return false;

    *out_signal = signal_id;
    return true;
}

static yyjson_val *haptics_policies_json(const slayer3d_game_data_runtime *runtime)
{
    return obj_get(obj_get(runtime_root(runtime), "haptics"), "policies");
}

static yyjson_val *haptics_policy_json(const slayer3d_game_data_runtime *runtime, int index)
{
    yyjson_val *policies = haptics_policies_json(runtime);
    if (!yyjson_is_arr(policies) || index < 0 || (size_t)index >= yyjson_arr_size(policies))
        return NULL;
    return yyjson_arr_get(policies, (size_t)index);
}

int slayer3d_game_data_haptics_policy_count(const slayer3d_game_data_runtime *runtime)
{
    yyjson_val *policies = haptics_policies_json(runtime);
    return yyjson_is_arr(policies) ? (int)yyjson_arr_size(policies) : 0;
}

bool slayer3d_game_data_get_haptics_policy_at(const slayer3d_game_data_runtime *runtime, int index,
                                              slayer3d_game_data_haptics_policy *out_policy)
{
    if (out_policy != NULL)
        SDL_zero(*out_policy);
    if (runtime == NULL || out_policy == NULL)
        return false;

    yyjson_val *policy = haptics_policy_json(runtime, index);
    if (!yyjson_is_obj(policy))
        return false;

    const char *signal = json_string(policy, "signal", NULL);
    out_policy->name = json_string(policy, "name", NULL);
    out_policy->signal_id = slayer3d_game_data_find_signal(runtime, signal);
    out_policy->low_frequency = json_float(policy, "low_frequency", json_float(policy, "low", 0.0f));
    out_policy->high_frequency = json_float(policy, "high_frequency", json_float(policy, "high", 0.0f));
    out_policy->duration_ms = (Uint32)SDL_max(json_int(policy, "duration_ms", 0), 0);
    return out_policy->name != NULL && out_policy->signal_id >= 0 && out_policy->duration_ms > 0U;
}

static bool haptics_payload_actor_filter_matches(const slayer3d_game_data_runtime *runtime, yyjson_val *filter,
                                                 const slayer3d_properties *payload)
{
    if (runtime == NULL || !yyjson_is_obj(filter) || payload == NULL)
        return false;
    if (!eval_data_condition(runtime, obj_get(filter, "active_if"), NULL))
        return false;

    const char *key = json_string(filter, "key", NULL);
    const char *payload_actor_name = key != NULL ? slayer3d_properties_get_string(payload, key, NULL) : NULL;
    if (payload_actor_name == NULL || payload_actor_name[0] == '\0')
        return false;

    const char *actor_name = json_string(filter, "actor", NULL);
    if (actor_name != NULL && SDL_strcmp(actor_name, payload_actor_name) == 0)
        return true;

    yyjson_val *tags = obj_get(filter, "tags");
    if (yyjson_is_arr(tags))
    {
        yyjson_val *entity = find_entity_json(runtime, payload_actor_name);
        return entity != NULL && entity_json_has_all_tags_from_json(entity, tags);
    }

    return false;
}

static bool haptics_policy_payload_matches(const slayer3d_game_data_runtime *runtime, yyjson_val *policy,
                                           const slayer3d_properties *payload)
{
    yyjson_val *filters = obj_get(policy, "payload_actor_filters");
    if (filters == NULL)
        return true;
    if (!yyjson_is_arr(filters) || yyjson_arr_size(filters) == 0)
        return false;

    for (size_t i = 0; i < yyjson_arr_size(filters); ++i)
    {
        if (haptics_payload_actor_filter_matches(runtime, yyjson_arr_get(filters, i), payload))
            return true;
    }
    return false;
}

bool slayer3d_game_data_match_haptics_policy(const slayer3d_game_data_runtime *runtime, int index, int signal_id,
                                             const slayer3d_properties *payload,
                                             slayer3d_game_data_haptics_policy *out_policy)
{
    yyjson_val *policy = haptics_policy_json(runtime, index);
    slayer3d_game_data_haptics_policy candidate;
    if (!slayer3d_game_data_get_haptics_policy_at(runtime, index, &candidate))
        return false;
    if (candidate.signal_id != signal_id)
        return false;
    if (!eval_data_condition(runtime, obj_get(policy, "enabled_if"), NULL))
        return false;
    if (!haptics_policy_payload_matches(runtime, policy, payload))
        return false;

    if (out_policy != NULL)
        *out_policy = candidate;
    return true;
}

static yyjson_val *network_runtime_pause_json(const slayer3d_game_data_runtime *runtime)
{
    return obj_get(network_runtime_bindings_json(runtime), "pause");
}

static bool network_runtime_pause_state_binding(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_registered_actor **out_actor, const char **out_property,
                                                char *error_buffer, int error_buffer_size)
{
    if (out_actor != NULL)
        *out_actor = NULL;
    if (out_property != NULL)
        *out_property = NULL;
    if (runtime == NULL || out_actor == NULL || out_property == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network pause state requires a runtime");
        return false;
    }

    yyjson_val *state = obj_get(network_runtime_pause_json(runtime), "state");
    const char *actor_name = json_string(state, "actor", NULL);
    const char *property = json_string(state, "property", NULL);
    if (actor_name == NULL || actor_name[0] == '\0' || property == NULL || property[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "network pause state binding is not authored");
        return false;
    }

    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, actor_name);
    if (actor == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "network pause actor '%s' not found", actor_name);
        return false;
    }

    *out_actor = actor;
    *out_property = property;
    return true;
}

bool slayer3d_game_data_get_network_runtime_pause_action(const slayer3d_game_data_runtime *runtime, int *out_action_id)
{
    if (out_action_id != NULL)
        *out_action_id = -1;
    if (runtime == NULL || out_action_id == NULL)
        return false;

    const char *action = json_string(network_runtime_pause_json(runtime), "action", NULL);
    if (action == NULL || action[0] == '\0')
        return false;

    const int action_id = slayer3d_game_data_find_action(runtime, action);
    if (action_id < 0)
        return false;

    *out_action_id = action_id;
    return true;
}

bool slayer3d_game_data_get_network_runtime_pause_state(const slayer3d_game_data_runtime *runtime, bool *out_paused,
                                                        char *error_buffer, int error_buffer_size)
{
    if (out_paused != NULL)
        *out_paused = false;
    if (out_paused == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network pause state output is required");
        return false;
    }

    slayer3d_registered_actor *actor = NULL;
    const char *property = NULL;
    if (!network_runtime_pause_state_binding(runtime, &actor, &property, error_buffer, error_buffer_size))
        return false;

    const slayer3d_value *value = slayer3d_properties_get_value(actor->props, property);
    if (value == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "network pause property '%s' not found", property);
        return false;
    }
    if (value->type != SLAYER3D_VALUE_BOOL)
    {
        set_errorf(error_buffer, error_buffer_size, "network pause property '%s' must be bool", property);
        return false;
    }

    *out_paused = value->as_bool;
    return true;
}

bool slayer3d_game_data_set_network_runtime_pause_state(slayer3d_game_data_runtime *runtime, bool paused,
                                                        char *error_buffer, int error_buffer_size)
{
    slayer3d_registered_actor *actor = NULL;
    const char *property = NULL;
    if (!network_runtime_pause_state_binding(runtime, &actor, &property, error_buffer, error_buffer_size))
        return false;

    const slayer3d_value *existing = slayer3d_properties_get_value(actor->props, property);
    if (existing != NULL && existing->type != SLAYER3D_VALUE_BOOL)
    {
        set_errorf(error_buffer, error_buffer_size, "network pause property '%s' must be bool", property);
        return false;
    }

    slayer3d_properties_set_bool(actor->props, property, paused);
    return true;
}

static bool game_data_append_snapshot_value(char *buffer, size_t buffer_size, size_t *offset,
                                            const game_data_snapshot_value *value)
{
    if (value == NULL)
        return false;

    switch (value->type)
    {
    case SLAYER3D_REPLICATION_FIELD_BOOL:
        return append_format(buffer, buffer_size, offset, "%s", value->value.as_bool ? "true" : "false");
    case SLAYER3D_REPLICATION_FIELD_INT32:
    case SLAYER3D_REPLICATION_FIELD_ENUM_ID:
        return append_format(buffer, buffer_size, offset, "%d", (int)value->value.as_int32);
    case SLAYER3D_REPLICATION_FIELD_FLOAT32:
        return append_format(buffer, buffer_size, offset, "%.3f", value->value.as_float32);
    case SLAYER3D_REPLICATION_FIELD_VEC2:
        return append_format(buffer, buffer_size, offset, "(%.3f,%.3f)", value->value.as_vec2.x,
                             value->value.as_vec2.y);
    case SLAYER3D_REPLICATION_FIELD_VEC3:
        return append_format(buffer, buffer_size, offset, "(%.3f,%.3f,%.3f)", value->value.as_vec3.x,
                             value->value.as_vec3.y, value->value.as_vec3.z);
    default:
        return false;
    }
}

static bool game_data_describe_network_snapshot_ex(const slayer3d_game_data_runtime *runtime,
                                                   const char *replication_name, Uint32 tick,
                                                   bool include_session_state, char *buffer, size_t buffer_size,
                                                   char *error_buffer, int error_buffer_size)
{
    if (buffer != NULL && buffer_size > 0U)
        buffer[0] = '\0';
    if (runtime == NULL || replication_name == NULL || replication_name[0] == '\0' || buffer == NULL ||
        buffer_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot describe requires runtime, channel, and buffer");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot describe requires an authored network schema");
        return false;
    }

    yyjson_val *channel = game_data_find_replication_channel_by_name(runtime, replication_name, NULL);
    if (channel == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot describe channel not found");
        return false;
    }
    if (!game_data_replication_channel_is_host_to_client(channel))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot describe channel must be host_to_client");
        return false;
    }

    size_t offset = 0U;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (!append_format(buffer, buffer_size, &offset, "tick=%u scene=%s channel=%s", (unsigned int)tick,
                       active_scene != NULL ? active_scene : "<none>", replication_name))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot describe buffer is too small");
        return false;
    }

    yyjson_val *state_keys = obj_get(network_session_flow_json(runtime), "state_keys");
    if (include_session_state && yyjson_is_obj(state_keys))
    {
        yyjson_val *state_key = NULL;
        yyjson_val *state_value_key = NULL;
        size_t state_index = 0U;
        size_t state_max = 0U;
        yyjson_obj_foreach(state_keys, state_index, state_max, state_key, state_value_key)
        {
            const char *semantic = yyjson_get_str(state_key);
            const char *key = yyjson_is_str(state_value_key) ? yyjson_get_str(state_value_key) : NULL;
            const char *value = slayer3d_properties_get_string(runtime->scene_state, key, "none");
            if (semantic != NULL && key != NULL &&
                !append_format(buffer, buffer_size, &offset, " %s=%s", semantic, value != NULL ? value : "none"))
            {
                set_error(error_buffer, error_buffer_size, "network snapshot describe buffer is too small");
                return false;
            }
        }
    }

    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t actor_index = 0U; yyjson_is_arr(actors) && actor_index < yyjson_arr_size(actors); ++actor_index)
    {
        yyjson_val *actor_schema = yyjson_arr_get(actors, actor_index);
        const char *entity_name = json_string(actor_schema, "entity", NULL);
        const slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (actor == NULL)
        {
            set_errorf(error_buffer, error_buffer_size, "network snapshot describe actor '%s' not found",
                       entity_name != NULL ? entity_name : "<null>");
            return false;
        }

        yyjson_val *fields = obj_get(actor_schema, "fields");
        for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
        {
            slayer3d_replication_field_descriptor field;
            game_data_snapshot_value value;
            if (!slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) ||
                !game_data_read_actor_replication_field(actor, &field, &value))
            {
                set_errorf(error_buffer, error_buffer_size,
                           "network snapshot describe failed to read field '%s' on actor '%s'",
                           field.path != NULL ? field.path : "<invalid>", entity_name != NULL ? entity_name : "<null>");
                return false;
            }
            if (!append_format(buffer, buffer_size, &offset, " %s.%s=", entity_name != NULL ? entity_name : "<null>",
                               field.path != NULL ? field.path : "<invalid>") ||
                !game_data_append_snapshot_value(buffer, buffer_size, &offset, &value))
            {
                set_error(error_buffer, error_buffer_size, "network snapshot describe buffer is too small");
                return false;
            }
        }
    }

    yyjson_val *pools = obj_get(channel, "pools");
    for (size_t pool_index = 0U; yyjson_is_arr(pools) && pool_index < yyjson_arr_size(pools); ++pool_index)
    {
        yyjson_val *pool_schema = yyjson_arr_get(pools, pool_index);
        const char *pool_name = json_string(pool_schema, "pool", NULL);
        const actor_pool_runtime *pool = find_actor_pool_const(runtime, pool_name);
        if (pool == NULL)
        {
            set_errorf(error_buffer, error_buffer_size, "network snapshot describe pool '%s' not found",
                       pool_name != NULL ? pool_name : "<null>");
            return false;
        }

        yyjson_val *fields = obj_get(pool_schema, "fields");
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            const char *actor_name = pool->actor_names[actor_index];
            const slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, actor_name);
            if (actor == NULL)
            {
                set_errorf(error_buffer, error_buffer_size, "network snapshot describe pooled actor '%s' not found",
                           actor_name != NULL ? actor_name : "<null>");
                return false;
            }
            for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
            {
                slayer3d_replication_field_descriptor field;
                game_data_snapshot_value value;
                if (!slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) ||
                    !game_data_read_actor_replication_field(actor, &field, &value))
                {
                    set_errorf(error_buffer, error_buffer_size,
                               "network snapshot describe failed to read field '%s' on pooled actor '%s'",
                               field.path != NULL ? field.path : "<invalid>",
                               actor_name != NULL ? actor_name : "<null>");
                    return false;
                }
                if (!append_format(buffer, buffer_size, &offset, " %s.%s=", actor_name != NULL ? actor_name : "<null>",
                                   field.path != NULL ? field.path : "<invalid>") ||
                    !game_data_append_snapshot_value(buffer, buffer_size, &offset, &value))
                {
                    set_error(error_buffer, error_buffer_size, "network snapshot describe buffer is too small");
                    return false;
                }
            }
        }
    }

    return true;
}

bool slayer3d_game_data_describe_network_snapshot(const slayer3d_game_data_runtime *runtime,
                                                  const char *replication_name, Uint32 tick, char *buffer,
                                                  size_t buffer_size, char *error_buffer, int error_buffer_size)
{
    return game_data_describe_network_snapshot_ex(runtime, replication_name, tick, true, buffer, buffer_size,
                                                  error_buffer, error_buffer_size);
}

static yyjson_val *network_diagnostics_json(const slayer3d_game_data_runtime *runtime)
{
    return obj_get(obj_get(runtime_root(runtime), "network"), "diagnostics");
}

static yyjson_val *find_network_snapshot_diagnostic_json(const slayer3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *snapshots = obj_get(network_diagnostics_json(runtime), "snapshots");
    if (name == NULL || name[0] == '\0' || !yyjson_is_arr(snapshots))
        return NULL;

    for (size_t i = 0U; i < yyjson_arr_size(snapshots); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(snapshots, i);
        const char *entry_name = json_string(entry, "name", NULL);
        if (entry_name != NULL && SDL_strcmp(entry_name, name) == 0)
            return entry;
    }
    return NULL;
}

static SDL_LogPriority network_diagnostic_log_priority(const char *level)
{
    if (level == NULL || level[0] == '\0' || SDL_strcmp(level, "info") == 0)
        return SDL_LOG_PRIORITY_INFO;
    if (SDL_strcmp(level, "debug") == 0)
        return SDL_LOG_PRIORITY_DEBUG;
    if (SDL_strcmp(level, "warn") == 0 || SDL_strcmp(level, "warning") == 0)
        return SDL_LOG_PRIORITY_WARN;
    if (SDL_strcmp(level, "error") == 0)
        return SDL_LOG_PRIORITY_ERROR;
    if (SDL_strcmp(level, "critical") == 0)
        return SDL_LOG_PRIORITY_CRITICAL;
    return SDL_LOG_PRIORITY_INFO;
}

static network_diagnostic_runtime_state *find_network_diagnostic_state(slayer3d_game_data_runtime *runtime,
                                                                       const char *name)
{
    if (runtime == NULL || name == NULL || name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->network_diagnostic_count; ++i)
    {
        if (runtime->network_diagnostics[i].name != NULL && SDL_strcmp(runtime->network_diagnostics[i].name, name) == 0)
        {
            return &runtime->network_diagnostics[i];
        }
    }
    return NULL;
}

static bool ensure_network_diagnostic_state_capacity(slayer3d_game_data_runtime *runtime, int required)
{
    if (runtime == NULL)
        return false;
    if (required <= runtime->network_diagnostic_capacity)
        return true;

    int next_capacity = runtime->network_diagnostic_capacity < 4 ? 4 : runtime->network_diagnostic_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    network_diagnostic_runtime_state *states = (network_diagnostic_runtime_state *)SDL_realloc(
        runtime->network_diagnostics, (size_t)next_capacity * sizeof(*states));
    if (states == NULL)
        return false;

    SDL_memset(states + runtime->network_diagnostic_capacity, 0,
               (size_t)(next_capacity - runtime->network_diagnostic_capacity) * sizeof(*states));
    runtime->network_diagnostics = states;
    runtime->network_diagnostic_capacity = next_capacity;
    return true;
}

static network_diagnostic_runtime_state *ensure_network_diagnostic_state(slayer3d_game_data_runtime *runtime,
                                                                         const char *name)
{
    network_diagnostic_runtime_state *state = find_network_diagnostic_state(runtime, name);
    if (state != NULL)
        return state;
    if (runtime == NULL || name == NULL || name[0] == '\0' ||
        !ensure_network_diagnostic_state_capacity(runtime, runtime->network_diagnostic_count + 1))
    {
        return NULL;
    }

    state = &runtime->network_diagnostics[runtime->network_diagnostic_count];
    state->name = SDL_strdup(name);
    if (state->name == NULL)
        return NULL;
    state->last_log_ms = 0U;
    state->logged = false;
    runtime->network_diagnostic_count++;
    return state;
}

bool slayer3d_game_data_log_network_snapshot_diagnostic(slayer3d_game_data_runtime *runtime,
                                                        const char *diagnostic_name, Uint32 tick, const char *event,
                                                        const char *extra, bool *out_logged, char *error_buffer,
                                                        int error_buffer_size)
{
    if (out_logged != NULL)
        *out_logged = false;
    if (runtime == NULL || diagnostic_name == NULL || diagnostic_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "network snapshot diagnostic requires runtime and name");
        return false;
    }

    yyjson_val *diagnostic = find_network_snapshot_diagnostic_json(runtime, diagnostic_name);
    if (diagnostic == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot diagnostic not found");
        return false;
    }
    if (!json_bool(diagnostic, "enabled", true))
        return true;

    network_diagnostic_runtime_state *state = ensure_network_diagnostic_state(runtime, diagnostic_name);
    if (state == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot diagnostic state allocation failed");
        return false;
    }

    const float cadence_seconds = SDL_max(json_float(diagnostic, "cadence_seconds", 0.0f), 0.0f);
    const Uint64 now = SDL_GetTicks();
    if (state->logged && cadence_seconds > 0.0f &&
        (double)(now - state->last_log_ms) < (double)cadence_seconds * 1000.0)
    {
        return true;
    }

    const char *replication_name = json_string(diagnostic, "replication", NULL);
    char description[4096] = {0};
    if (!game_data_describe_network_snapshot_ex(runtime, replication_name, tick,
                                                json_bool(diagnostic, "include_session_state", true), description,
                                                sizeof(description), error_buffer, error_buffer_size))
    {
        return false;
    }

    char tick_text[32];
    SDL_snprintf(tick_text, sizeof(tick_text), "%u", (unsigned int)tick);

    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot diagnostic payload allocation failed");
        return false;
    }
    slayer3d_properties_set_string(payload, "name", diagnostic_name);
    slayer3d_properties_set_string(payload, "event", event != NULL ? event : "");
    slayer3d_properties_set_string(payload, "extra", extra != NULL ? extra : "");
    slayer3d_properties_set_string(payload, "tick", tick_text);
    slayer3d_properties_set_string(payload, "description", description);

    char message[4096] = {0};
    const char *message_template = json_string(diagnostic, "message", "{event} {description} {extra}");
    const bool formatted = format_payload_string(payload, message_template, message, sizeof(message));
    slayer3d_properties_destroy(payload);
    if (!formatted)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot diagnostic message is too long");
        return false;
    }

    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION,
                   network_diagnostic_log_priority(json_string(diagnostic, "level", NULL)), "%s", message);
    state->last_log_ms = now;
    state->logged = true;
    if (out_logged != NULL)
        *out_logged = true;
    return true;
}

bool slayer3d_game_data_encode_network_snapshot(const slayer3d_game_data_runtime *runtime, const char *replication_name,
                                                Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                                char *error_buffer, int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (runtime == NULL || replication_name == NULL || replication_name[0] == '\0' || buffer == NULL ||
        buffer_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot encode requires runtime, channel, and buffer");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot encode requires an authored network schema");
        return false;
    }

    int channel_index = -1;
    yyjson_val *channel = game_data_find_replication_channel_by_name(runtime, replication_name, &channel_index);
    if (channel == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot replication channel not found");
        return false;
    }
    if (!game_data_replication_channel_is_host_to_client(channel))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot channel must be host_to_client");
        return false;
    }

    const size_t field_count = game_data_replication_channel_field_count(runtime, channel);
    if (field_count > SDL_MAX_UINT32 || channel_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot channel is too large");
        return false;
    }
    size_t required_size = 0U;
    if (!game_data_replication_channel_packet_size(runtime, channel, &required_size))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot channel contains unsupported field data");
        return false;
    }
    if (buffer_size < required_size)
    {
        set_errorf(error_buffer, error_buffer_size, "network snapshot requires %zu bytes, buffer has %zu bytes",
                   required_size, buffer_size);
        return false;
    }

    slayer3d_replication_writer writer;
    slayer3d_replication_writer_init(&writer, buffer, buffer_size);
    if (!slayer3d_replication_write_uint32(&writer, SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_MAGIC) ||
        !slayer3d_replication_write_uint32(&writer, SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_VERSION) ||
        !slayer3d_replication_write_uint32(&writer, tick) ||
        !slayer3d_replication_write_uint32(&writer, (Uint32)channel_index) ||
        !slayer3d_replication_write_bytes(&writer, runtime->network_schema_hash,
                                          SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE) ||
        !slayer3d_replication_write_uint32(&writer, (Uint32)field_count))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot buffer is too small for header");
        return false;
    }

    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t actor_index = 0U; yyjson_is_arr(actors) && actor_index < yyjson_arr_size(actors); ++actor_index)
    {
        yyjson_val *actor_schema = yyjson_arr_get(actors, actor_index);
        const char *entity_name = json_string(actor_schema, "entity", NULL);
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (actor == NULL)
        {
            set_errorf(error_buffer, error_buffer_size, "network snapshot actor '%s' not found",
                       entity_name != NULL ? entity_name : "<null>");
            return false;
        }

        yyjson_val *fields = obj_get(actor_schema, "fields");
        for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
        {
            slayer3d_replication_field_descriptor field;
            game_data_snapshot_value value;
            if (!slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) ||
                !game_data_read_actor_replication_field(actor, &field, &value))
            {
                set_errorf(error_buffer, error_buffer_size, "network snapshot failed to read field '%s' on actor '%s'",
                           field.path != NULL ? field.path : "<invalid>", entity_name != NULL ? entity_name : "<null>");
                return false;
            }
            if (!game_data_write_snapshot_value(&writer, &value))
            {
                set_error(error_buffer, error_buffer_size, "network snapshot buffer is too small for field data");
                return false;
            }
        }
    }

    yyjson_val *pools = obj_get(channel, "pools");
    for (size_t pool_index = 0U; yyjson_is_arr(pools) && pool_index < yyjson_arr_size(pools); ++pool_index)
    {
        yyjson_val *pool_schema = yyjson_arr_get(pools, pool_index);
        const char *pool_name = json_string(pool_schema, "pool", NULL);
        const actor_pool_runtime *pool = find_actor_pool_const(runtime, pool_name);
        if (pool == NULL)
        {
            set_errorf(error_buffer, error_buffer_size, "network snapshot pool '%s' not found",
                       pool_name != NULL ? pool_name : "<null>");
            return false;
        }

        yyjson_val *fields = obj_get(pool_schema, "fields");
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            const char *actor_name = pool->actor_names[actor_index];
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, actor_name);
            if (actor == NULL)
            {
                set_errorf(error_buffer, error_buffer_size, "network snapshot pooled actor '%s' not found",
                           actor_name != NULL ? actor_name : "<null>");
                return false;
            }
            for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
            {
                slayer3d_replication_field_descriptor field;
                game_data_snapshot_value value;
                if (!slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) ||
                    !game_data_read_actor_replication_field(actor, &field, &value))
                {
                    set_errorf(error_buffer, error_buffer_size,
                               "network snapshot failed to read field '%s' on pooled actor '%s'",
                               field.path != NULL ? field.path : "<invalid>",
                               actor_name != NULL ? actor_name : "<null>");
                    return false;
                }
                if (!game_data_write_snapshot_value(&writer, &value))
                {
                    set_error(error_buffer, error_buffer_size, "network snapshot buffer is too small for field data");
                    return false;
                }
            }
        }
    }

    if (out_size != NULL)
        *out_size = slayer3d_replication_writer_offset(&writer);
    return true;
}

bool slayer3d_game_data_encode_network_runtime_snapshot(const slayer3d_game_data_runtime *runtime,
                                                        const char *binding_name, Uint32 tick, void *buffer,
                                                        size_t buffer_size, size_t *out_size, char *error_buffer,
                                                        int error_buffer_size)
{
    const char *replication_name = NULL;
    if (!slayer3d_game_data_get_network_runtime_replication(runtime, binding_name, &replication_name))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime replication binding '%s' not found",
                   binding_name != NULL ? binding_name : "<null>");
        if (out_size != NULL)
            *out_size = 0U;
        return false;
    }
    return slayer3d_game_data_encode_network_snapshot(runtime, replication_name, tick, buffer, buffer_size, out_size,
                                                      error_buffer, error_buffer_size);
}

bool slayer3d_game_data_send_network_runtime_snapshot(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_network_session *session, const char *binding_name,
                                                      Uint32 tick, char *error_buffer, int error_buffer_size)
{
    Uint8 packet[SLAYER3D_NETWORK_MAX_PACKET_SIZE];
    size_t packet_size = 0U;
    if (session == NULL || !slayer3d_network_session_is_connected(session))
    {
        set_error(error_buffer, error_buffer_size, "network runtime snapshot send requires connected session");
        return false;
    }
    if (!slayer3d_game_data_encode_network_runtime_snapshot(runtime, binding_name, tick, packet, sizeof(packet),
                                                            &packet_size, error_buffer, error_buffer_size))
    {
        return false;
    }
    if (!slayer3d_network_session_send(session, packet, (int)packet_size))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime snapshot send failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

static bool game_data_network_packet_matches_replication_binding(const slayer3d_game_data_runtime *runtime,
                                                                 const char *binding_name, const void *packet,
                                                                 size_t packet_size, Uint32 expected_magic,
                                                                 Uint32 expected_version, bool host_to_client,
                                                                 char *error_buffer, int error_buffer_size)
{
    const char *expected_channel = NULL;
    if (runtime == NULL || packet == NULL || packet_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication check requires runtime and packet");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size,
                  "network runtime replication check requires an authored network schema");
        return false;
    }
    if (!slayer3d_game_data_get_network_runtime_replication(runtime, binding_name, &expected_channel))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime replication binding '%s' not found",
                   binding_name != NULL ? binding_name : "<null>");
        return false;
    }

    slayer3d_replication_reader reader;
    slayer3d_replication_reader_init(&reader, packet, packet_size);
    Uint32 magic = 0U;
    Uint32 version = 0U;
    Uint32 tick = 0U;
    Uint32 channel_index = 0U;
    Uint8 schema_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE];
    if (!slayer3d_replication_read_uint32(&reader, &magic) || !slayer3d_replication_read_uint32(&reader, &version) ||
        !slayer3d_replication_read_uint32(&reader, &tick) ||
        !slayer3d_replication_read_uint32(&reader, &channel_index) ||
        !slayer3d_replication_read_bytes(&reader, schema_hash, sizeof(schema_hash)))
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication packet is too small for header");
        return false;
    }
    if (magic != expected_magic || version != expected_version)
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication packet has unsupported header");
        return false;
    }
    if (SDL_memcmp(schema_hash, runtime->network_schema_hash, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE) != 0)
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication schema hash does not match runtime");
        return false;
    }

    yyjson_val *channel = game_data_find_replication_channel_by_index(runtime, channel_index);
    if (channel == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication channel is invalid for this runtime");
        return false;
    }
    if (host_to_client && !game_data_replication_channel_is_host_to_client(channel))
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication channel must be host_to_client");
        return false;
    }
    if (!host_to_client && !game_data_replication_channel_is_client_to_host(channel))
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication channel must be client_to_host");
        return false;
    }

    const char *actual_channel = json_string(channel, "name", NULL);
    if (actual_channel == NULL || SDL_strcmp(actual_channel, expected_channel) != 0)
    {
        set_errorf(error_buffer, error_buffer_size,
                   "network runtime replication packet channel '%s' does not match binding '%s'",
                   actual_channel != NULL ? actual_channel : "<null>", binding_name != NULL ? binding_name : "<null>");
        return false;
    }
    return true;
}

bool slayer3d_game_data_apply_network_snapshot(slayer3d_game_data_runtime *runtime, const void *packet,
                                               size_t packet_size, Uint32 *out_tick, char *error_buffer,
                                               int error_buffer_size)
{
    if (out_tick != NULL)
        *out_tick = 0U;
    if (runtime == NULL || packet == NULL || packet_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot apply requires runtime and packet");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot apply requires an authored network schema");
        return false;
    }

    slayer3d_replication_reader reader;
    slayer3d_replication_reader_init(&reader, packet, packet_size);

    Uint32 magic = 0U;
    Uint32 version = 0U;
    Uint32 tick = 0U;
    Uint32 channel_index = 0U;
    Uint8 schema_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE];
    Uint32 packet_field_count = 0U;
    if (!slayer3d_replication_read_uint32(&reader, &magic) || !slayer3d_replication_read_uint32(&reader, &version) ||
        !slayer3d_replication_read_uint32(&reader, &tick) ||
        !slayer3d_replication_read_uint32(&reader, &channel_index) ||
        !slayer3d_replication_read_bytes(&reader, schema_hash, sizeof(schema_hash)) ||
        !slayer3d_replication_read_uint32(&reader, &packet_field_count))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot packet is too small for header");
        return false;
    }
    if (magic != SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_MAGIC || version != SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_VERSION)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot packet has unsupported header");
        return false;
    }
    if (SDL_memcmp(schema_hash, runtime->network_schema_hash, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE) != 0)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot schema hash does not match runtime");
        return false;
    }

    yyjson_val *channel = game_data_find_replication_channel_by_index(runtime, channel_index);
    if (channel == NULL || !game_data_replication_channel_is_host_to_client(channel))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot channel is invalid for this runtime");
        return false;
    }

    const size_t field_count = game_data_replication_channel_field_count(runtime, channel);
    if ((Uint32)field_count != packet_field_count)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot field count does not match schema");
        return false;
    }

    game_data_snapshot_value *values =
        field_count > 0U ? (game_data_snapshot_value *)SDL_calloc(field_count, sizeof(*values)) : NULL;
    if (values == NULL && field_count > 0U)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot failed to allocate decoded field storage");
        return false;
    }

    size_t decoded_index = 0U;
    bool ok = true;
    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t actor_index = 0U; ok && yyjson_is_arr(actors) && actor_index < yyjson_arr_size(actors); ++actor_index)
    {
        yyjson_val *fields = obj_get(yyjson_arr_get(actors, actor_index), "fields");
        for (size_t field_index = 0U; ok && yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields);
             ++field_index)
        {
            slayer3d_replication_field_descriptor field;
            ok = slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) &&
                 decoded_index < field_count &&
                 game_data_read_snapshot_value(&reader, field.type, &values[decoded_index]);
            ++decoded_index;
        }
    }
    yyjson_val *pools = obj_get(channel, "pools");
    for (size_t pool_schema_index = 0U; ok && yyjson_is_arr(pools) && pool_schema_index < yyjson_arr_size(pools);
         ++pool_schema_index)
    {
        yyjson_val *pool_schema = yyjson_arr_get(pools, pool_schema_index);
        const actor_pool_runtime *pool = find_actor_pool_const(runtime, json_string(pool_schema, "pool", NULL));
        yyjson_val *fields = obj_get(pool_schema, "fields");
        if (pool == NULL)
        {
            ok = false;
            break;
        }
        for (int actor_index = 0; ok && actor_index < pool->capacity; ++actor_index)
        {
            for (size_t field_index = 0U; ok && yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields);
                 ++field_index)
            {
                slayer3d_replication_field_descriptor field;
                ok = slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) &&
                     decoded_index < field_count &&
                     game_data_read_snapshot_value(&reader, field.type, &values[decoded_index]);
                ++decoded_index;
            }
        }
    }
    if (ok && decoded_index != field_count)
        ok = false;
    if (ok && slayer3d_replication_reader_remaining(&reader) != 0U)
        ok = false;
    if (!ok)
    {
        SDL_free(values);
        set_error(error_buffer, error_buffer_size, "network snapshot field data does not match schema");
        return false;
    }

    const size_t actor_count = yyjson_is_arr(actors) ? yyjson_arr_size(actors) : 0U;
    slayer3d_registered_actor **resolved_actors =
        actor_count > 0U ? (slayer3d_registered_actor **)SDL_calloc(actor_count, sizeof(*resolved_actors)) : NULL;
    if (resolved_actors == NULL && actor_count > 0U)
    {
        SDL_free(values);
        set_error(error_buffer, error_buffer_size, "network snapshot failed to allocate actor lookup storage");
        return false;
    }

    /* Resolve every actor before applying any field so a malformed runtime cannot produce a partial snapshot apply. */
    for (size_t actor_index = 0U; actor_index < actor_count; ++actor_index)
    {
        yyjson_val *actor_schema = yyjson_arr_get(actors, actor_index);
        const char *entity_name = json_string(actor_schema, "entity", NULL);
        resolved_actors[actor_index] = slayer3d_game_data_find_actor(runtime, entity_name);
        if (resolved_actors[actor_index] == NULL)
        {
            SDL_free(resolved_actors);
            SDL_free(values);
            set_errorf(error_buffer, error_buffer_size, "network snapshot actor '%s' not found",
                       entity_name != NULL ? entity_name : "<null>");
            return false;
        }
    }

    size_t pooled_actor_count = 0U;
    for (size_t pool_schema_index = 0U; yyjson_is_arr(pools) && pool_schema_index < yyjson_arr_size(pools);
         ++pool_schema_index)
    {
        const actor_pool_runtime *pool =
            find_actor_pool_const(runtime, json_string(yyjson_arr_get(pools, pool_schema_index), "pool", NULL));
        if (pool == NULL || pool->capacity < 0)
        {
            SDL_free(resolved_actors);
            SDL_free(values);
            set_error(error_buffer, error_buffer_size, "network snapshot pool not found");
            return false;
        }
        pooled_actor_count += (size_t)pool->capacity;
    }
    slayer3d_registered_actor **resolved_pooled_actors =
        pooled_actor_count > 0U
            ? (slayer3d_registered_actor **)SDL_calloc(pooled_actor_count, sizeof(*resolved_pooled_actors))
            : NULL;
    if (resolved_pooled_actors == NULL && pooled_actor_count > 0U)
    {
        SDL_free(resolved_actors);
        SDL_free(values);
        set_error(error_buffer, error_buffer_size, "network snapshot failed to allocate pooled actor lookup storage");
        return false;
    }

    size_t resolved_pool_actor_index = 0U;
    for (size_t pool_schema_index = 0U; yyjson_is_arr(pools) && pool_schema_index < yyjson_arr_size(pools);
         ++pool_schema_index)
    {
        yyjson_val *pool_schema = yyjson_arr_get(pools, pool_schema_index);
        const char *pool_name = json_string(pool_schema, "pool", NULL);
        const actor_pool_runtime *pool = find_actor_pool_const(runtime, pool_name);
        for (int actor_index = 0; pool != NULL && actor_index < pool->capacity; ++actor_index)
        {
            const char *actor_name = pool->actor_names[actor_index];
            resolved_pooled_actors[resolved_pool_actor_index] = slayer3d_game_data_find_actor(runtime, actor_name);
            if (resolved_pooled_actors[resolved_pool_actor_index] == NULL)
            {
                SDL_free(resolved_pooled_actors);
                SDL_free(resolved_actors);
                SDL_free(values);
                set_errorf(error_buffer, error_buffer_size, "network snapshot pooled actor '%s' not found",
                           actor_name != NULL ? actor_name : "<null>");
                return false;
            }
            ++resolved_pool_actor_index;
        }
    }

    actor_lifecycle_defer_begin(runtime);
    bool apply_ok = true;
    size_t apply_index = 0U;
    for (size_t actor_index = 0U; apply_ok && actor_index < actor_count; ++actor_index)
    {
        yyjson_val *actor_schema = yyjson_arr_get(actors, actor_index);
        slayer3d_registered_actor *actor = resolved_actors[actor_index];
        yyjson_val *fields = obj_get(actor_schema, "fields");
        for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
        {
            slayer3d_replication_field_descriptor field;
            if (!slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) ||
                apply_index >= field_count ||
                !game_data_apply_actor_replication_field(runtime, actor, &field, &values[apply_index]))
            {
                set_errorf(error_buffer, error_buffer_size, "network snapshot failed to apply field '%s' on actor '%s'",
                           field.path != NULL ? field.path : "<invalid>",
                           actor_schema != NULL ? json_string(actor_schema, "entity", "<null>") : "<null>");
                apply_ok = false;
                break;
            }
            ++apply_index;
        }
    }
    size_t pooled_apply_actor_index = 0U;
    for (size_t pool_schema_index = 0U; apply_ok && yyjson_is_arr(pools) && pool_schema_index < yyjson_arr_size(pools);
         ++pool_schema_index)
    {
        yyjson_val *pool_schema = yyjson_arr_get(pools, pool_schema_index);
        const char *pool_name = json_string(pool_schema, "pool", NULL);
        const actor_pool_runtime *pool = find_actor_pool_const(runtime, pool_name);
        yyjson_val *fields = obj_get(pool_schema, "fields");
        for (int actor_index = 0; pool != NULL && actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = resolved_pooled_actors[pooled_apply_actor_index++];
            const char *actor_name = actor != NULL ? actor->name : "<null>";
            for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
            {
                slayer3d_replication_field_descriptor field;
                if (!slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) ||
                    apply_index >= field_count ||
                    !game_data_apply_actor_replication_field(runtime, actor, &field, &values[apply_index]))
                {
                    set_errorf(error_buffer, error_buffer_size,
                               "network snapshot failed to apply field '%s' on pooled actor '%s'",
                               field.path != NULL ? field.path : "<invalid>", actor_name);
                    apply_ok = false;
                    break;
                }
                ++apply_index;
            }
        }
    }
    actor_lifecycle_defer_end(runtime);
    if (!apply_ok)
    {
        SDL_free(resolved_pooled_actors);
        SDL_free(resolved_actors);
        SDL_free(values);
        return false;
    }

    SDL_free(resolved_pooled_actors);
    SDL_free(resolved_actors);
    SDL_free(values);
    if (out_tick != NULL)
        *out_tick = tick;
    return true;
}

bool slayer3d_game_data_apply_network_runtime_snapshot(slayer3d_game_data_runtime *runtime, const char *binding_name,
                                                       const void *packet, size_t packet_size, Uint32 *out_tick,
                                                       char *error_buffer, int error_buffer_size)
{
    if (!game_data_network_packet_matches_replication_binding(
            runtime, binding_name, packet, packet_size, SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_MAGIC,
            SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_VERSION, true, error_buffer, error_buffer_size))
    {
        if (out_tick != NULL)
            *out_tick = 0U;
        return false;
    }
    return slayer3d_game_data_apply_network_snapshot(runtime, packet, packet_size, out_tick, error_buffer,
                                                     error_buffer_size);
}

bool slayer3d_game_data_encode_network_input(const slayer3d_game_data_runtime *runtime, const char *replication_name,
                                             const slayer3d_input_manager *input, Uint32 tick, void *buffer,
                                             size_t buffer_size, size_t *out_size, char *error_buffer,
                                             int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (runtime == NULL || replication_name == NULL || replication_name[0] == '\0' || input == NULL || buffer == NULL ||
        buffer_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network input encode requires runtime, channel, input, and buffer");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network input encode requires an authored network schema");
        return false;
    }

    int channel_index = -1;
    yyjson_val *channel = game_data_find_replication_channel_by_name(runtime, replication_name, &channel_index);
    if (channel == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network input replication channel not found");
        return false;
    }
    if (!game_data_replication_channel_is_client_to_host(channel))
    {
        set_error(error_buffer, error_buffer_size, "network input channel must be client_to_host");
        return false;
    }

    const size_t input_count = game_data_replication_channel_input_count(channel);
    if (input_count > SDL_MAX_UINT32 || channel_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "network input channel is too large");
        return false;
    }
    size_t required_size = 0U;
    if (!game_data_replication_input_packet_size(channel, &required_size))
    {
        set_error(error_buffer, error_buffer_size, "network input channel contains unsupported field data");
        return false;
    }
    if (buffer_size < required_size)
    {
        set_errorf(error_buffer, error_buffer_size, "network input requires %zu bytes, buffer has %zu bytes",
                   required_size, buffer_size);
        return false;
    }

    slayer3d_replication_writer writer;
    slayer3d_replication_writer_init(&writer, buffer, buffer_size);
    if (!slayer3d_replication_write_uint32(&writer, SLAYER3D_GAME_DATA_NETWORK_INPUT_MAGIC) ||
        !slayer3d_replication_write_uint32(&writer, SLAYER3D_GAME_DATA_NETWORK_INPUT_VERSION) ||
        !slayer3d_replication_write_uint32(&writer, tick) ||
        !slayer3d_replication_write_uint32(&writer, (Uint32)channel_index) ||
        !slayer3d_replication_write_bytes(&writer, runtime->network_schema_hash,
                                          SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE) ||
        !slayer3d_replication_write_uint32(&writer, (Uint32)input_count))
    {
        set_error(error_buffer, error_buffer_size, "network input buffer is too small for header");
        return false;
    }

    yyjson_val *inputs = obj_get(channel, "inputs");
    for (size_t input_index = 0U; yyjson_is_arr(inputs) && input_index < yyjson_arr_size(inputs); ++input_index)
    {
        yyjson_val *input_schema = yyjson_arr_get(inputs, input_index);
        const char *action_name = game_data_replication_input_action(input_schema);
        const int action_id = game_data_replication_action_id(runtime, input_schema);
        if (action_id < 0)
        {
            set_errorf(error_buffer, error_buffer_size, "network input action '%s' not found",
                       action_name != NULL ? action_name : "<null>");
            return false;
        }

        const float value = SDL_clamp(slayer3d_input_get_value(input, action_id), -1.0f, 1.0f);
        if (!slayer3d_replication_write_field_type(&writer, SLAYER3D_REPLICATION_FIELD_FLOAT32) ||
            !slayer3d_replication_write_float32(&writer, value))
        {
            set_error(error_buffer, error_buffer_size, "network input buffer is too small for action data");
            return false;
        }
    }

    if (out_size != NULL)
        *out_size = slayer3d_replication_writer_offset(&writer);
    return true;
}

bool slayer3d_game_data_encode_network_runtime_input(const slayer3d_game_data_runtime *runtime,
                                                     const char *binding_name, const slayer3d_input_manager *input,
                                                     Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                                     char *error_buffer, int error_buffer_size)
{
    const char *replication_name = NULL;
    if (!slayer3d_game_data_get_network_runtime_replication(runtime, binding_name, &replication_name))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime replication binding '%s' not found",
                   binding_name != NULL ? binding_name : "<null>");
        if (out_size != NULL)
            *out_size = 0U;
        return false;
    }
    return slayer3d_game_data_encode_network_input(runtime, replication_name, input, tick, buffer, buffer_size,
                                                   out_size, error_buffer, error_buffer_size);
}

bool slayer3d_game_data_send_network_runtime_input(const slayer3d_game_data_runtime *runtime,
                                                   slayer3d_network_session *session, const char *binding_name,
                                                   const slayer3d_input_manager *input, Uint32 tick, char *error_buffer,
                                                   int error_buffer_size)
{
    Uint8 packet[SLAYER3D_NETWORK_MAX_PACKET_SIZE];
    size_t packet_size = 0U;
    if (session == NULL || !slayer3d_network_session_is_connected(session))
    {
        set_error(error_buffer, error_buffer_size, "network runtime input send requires connected session");
        return false;
    }
    if (!slayer3d_game_data_encode_network_runtime_input(runtime, binding_name, input, tick, packet, sizeof(packet),
                                                         &packet_size, error_buffer, error_buffer_size))
    {
        return false;
    }
    if (!slayer3d_network_session_send(session, packet, (int)packet_size))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime input send failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool slayer3d_game_data_apply_network_input(const slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                            const void *packet, size_t packet_size, Uint32 *out_tick,
                                            char *error_buffer, int error_buffer_size)
{
    if (out_tick != NULL)
        *out_tick = 0U;
    if (runtime == NULL || input == NULL || packet == NULL || packet_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network input apply requires runtime, input, and packet");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network input apply requires an authored network schema");
        return false;
    }

    slayer3d_replication_reader reader;
    slayer3d_replication_reader_init(&reader, packet, packet_size);

    Uint32 magic = 0U;
    Uint32 version = 0U;
    Uint32 tick = 0U;
    Uint32 channel_index = 0U;
    Uint8 schema_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE];
    Uint32 packet_input_count = 0U;
    if (!slayer3d_replication_read_uint32(&reader, &magic) || !slayer3d_replication_read_uint32(&reader, &version) ||
        !slayer3d_replication_read_uint32(&reader, &tick) ||
        !slayer3d_replication_read_uint32(&reader, &channel_index) ||
        !slayer3d_replication_read_bytes(&reader, schema_hash, sizeof(schema_hash)) ||
        !slayer3d_replication_read_uint32(&reader, &packet_input_count))
    {
        set_error(error_buffer, error_buffer_size, "network input packet is too small for header");
        return false;
    }
    if (magic != SLAYER3D_GAME_DATA_NETWORK_INPUT_MAGIC || version != SLAYER3D_GAME_DATA_NETWORK_INPUT_VERSION)
    {
        set_error(error_buffer, error_buffer_size, "network input packet has unsupported header");
        return false;
    }
    if (SDL_memcmp(schema_hash, runtime->network_schema_hash, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE) != 0)
    {
        set_error(error_buffer, error_buffer_size, "network input schema hash does not match runtime");
        return false;
    }

    yyjson_val *channel = game_data_find_replication_channel_by_index(runtime, channel_index);
    if (channel == NULL || !game_data_replication_channel_is_client_to_host(channel))
    {
        set_error(error_buffer, error_buffer_size, "network input channel is invalid for this runtime");
        return false;
    }

    const size_t input_count = game_data_replication_channel_input_count(channel);
    if ((Uint32)input_count != packet_input_count)
    {
        set_error(error_buffer, error_buffer_size, "network input count does not match schema");
        return false;
    }

    game_data_input_value *values =
        input_count > 0U ? (game_data_input_value *)SDL_calloc(input_count, sizeof(*values)) : NULL;
    if (values == NULL && input_count > 0U)
    {
        set_error(error_buffer, error_buffer_size, "network input failed to allocate decoded action storage");
        return false;
    }

    bool ok = true;
    yyjson_val *inputs = obj_get(channel, "inputs");
    for (size_t input_index = 0U; ok && yyjson_is_arr(inputs) && input_index < yyjson_arr_size(inputs); ++input_index)
    {
        yyjson_val *input_schema = yyjson_arr_get(inputs, input_index);
        slayer3d_replication_field_type type = SLAYER3D_REPLICATION_FIELD_BOOL;
        values[input_index].action_id = game_data_replication_action_id(runtime, input_schema);
        ok = values[input_index].action_id >= 0 && slayer3d_replication_read_field_type(&reader, &type) &&
             type == SLAYER3D_REPLICATION_FIELD_FLOAT32 &&
             slayer3d_replication_read_float32(&reader, &values[input_index].value);
        values[input_index].value = SDL_clamp(values[input_index].value, -1.0f, 1.0f);
    }
    if (ok && slayer3d_replication_reader_remaining(&reader) != 0U)
        ok = false;
    if (!ok)
    {
        SDL_free(values);
        set_error(error_buffer, error_buffer_size, "network input action data does not match schema");
        return false;
    }

    for (size_t input_index = 0U; input_index < input_count; ++input_index)
        slayer3d_input_set_action_override(input, values[input_index].action_id, values[input_index].value);

    SDL_free(values);
    if (out_tick != NULL)
        *out_tick = tick;
    return true;
}

bool slayer3d_game_data_apply_network_runtime_input(const slayer3d_game_data_runtime *runtime, const char *binding_name,
                                                    slayer3d_input_manager *input, const void *packet,
                                                    size_t packet_size, Uint32 *out_tick, char *error_buffer,
                                                    int error_buffer_size)
{
    if (!game_data_network_packet_matches_replication_binding(
            runtime, binding_name, packet, packet_size, SLAYER3D_GAME_DATA_NETWORK_INPUT_MAGIC,
            SLAYER3D_GAME_DATA_NETWORK_INPUT_VERSION, false, error_buffer, error_buffer_size))
    {
        if (out_tick != NULL)
            *out_tick = 0U;
        return false;
    }
    return slayer3d_game_data_apply_network_input(runtime, input, packet, packet_size, out_tick, error_buffer,
                                                  error_buffer_size);
}

bool slayer3d_game_data_clear_network_input_overrides(const slayer3d_game_data_runtime *runtime,
                                                      const char *replication_name, slayer3d_input_manager *input,
                                                      char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || replication_name == NULL || replication_name[0] == '\0' || input == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network input override clear requires runtime, channel, and input");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network input override clear requires an authored network schema");
        return false;
    }

    yyjson_val *channel = game_data_find_replication_channel_by_name(runtime, replication_name, NULL);
    if (channel == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network input replication channel not found");
        return false;
    }
    if (!game_data_replication_channel_is_client_to_host(channel))
    {
        set_error(error_buffer, error_buffer_size, "network input channel must be client_to_host");
        return false;
    }

    yyjson_val *inputs = obj_get(channel, "inputs");
    for (size_t input_index = 0U; yyjson_is_arr(inputs) && input_index < yyjson_arr_size(inputs); ++input_index)
    {
        yyjson_val *input_schema = yyjson_arr_get(inputs, input_index);
        const char *action_name = game_data_replication_input_action(input_schema);
        const int action_id = game_data_replication_action_id(runtime, input_schema);
        if (action_id < 0)
        {
            set_errorf(error_buffer, error_buffer_size, "network input action '%s' not found",
                       action_name != NULL ? action_name : "<null>");
            return false;
        }
        slayer3d_input_clear_action_override(input, action_id);
    }

    return true;
}

bool slayer3d_game_data_encode_network_control(const slayer3d_game_data_runtime *runtime, const char *control_name,
                                               Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                               char *error_buffer, int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (runtime == NULL || control_name == NULL || control_name[0] == '\0' || buffer == NULL || buffer_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network control encode requires runtime, control, and buffer");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network control encode requires an authored network schema");
        return false;
    }

    int control_index = -1;
    yyjson_val *control = game_data_find_network_control_by_name(runtime, control_name, &control_index);
    if (control == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network control message not found");
        return false;
    }
    if (control_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "network control message index is invalid");
        return false;
    }

    size_t required_size = 0U;
    (void)game_data_network_control_packet_size(&required_size);
    if (buffer_size < required_size)
    {
        set_errorf(error_buffer, error_buffer_size, "network control requires %zu bytes, buffer has %zu bytes",
                   required_size, buffer_size);
        return false;
    }

    slayer3d_replication_writer writer;
    slayer3d_replication_writer_init(&writer, buffer, buffer_size);
    if (!slayer3d_replication_write_uint32(&writer, SLAYER3D_GAME_DATA_NETWORK_CONTROL_MAGIC) ||
        !slayer3d_replication_write_uint32(&writer, SLAYER3D_GAME_DATA_NETWORK_CONTROL_VERSION) ||
        !slayer3d_replication_write_uint32(&writer, tick) ||
        !slayer3d_replication_write_uint32(&writer, (Uint32)control_index) ||
        !slayer3d_replication_write_bytes(&writer, runtime->network_schema_hash, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE))
    {
        set_error(error_buffer, error_buffer_size, "network control buffer is too small for packet");
        return false;
    }

    if (out_size != NULL)
        *out_size = slayer3d_replication_writer_offset(&writer);
    return true;
}

bool slayer3d_game_data_encode_network_runtime_control(const slayer3d_game_data_runtime *runtime,
                                                       const char *binding_name, Uint32 tick, void *buffer,
                                                       size_t buffer_size, size_t *out_size, char *error_buffer,
                                                       int error_buffer_size)
{
    const char *control_name = NULL;
    if (!slayer3d_game_data_get_network_runtime_control(runtime, binding_name, &control_name))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime control binding '%s' not found",
                   binding_name != NULL ? binding_name : "<null>");
        if (out_size != NULL)
            *out_size = 0U;
        return false;
    }
    return slayer3d_game_data_encode_network_control(runtime, control_name, tick, buffer, buffer_size, out_size,
                                                     error_buffer, error_buffer_size);
}

bool slayer3d_game_data_decode_network_control(const slayer3d_game_data_runtime *runtime, const void *packet,
                                               size_t packet_size, slayer3d_game_data_network_control *out_control,
                                               char *error_buffer, int error_buffer_size)
{
    if (out_control != NULL)
    {
        out_control->name = NULL;
        out_control->direction = SLAYER3D_GAME_DATA_NETWORK_DIRECTION_INVALID;
        out_control->signal_id = -1;
        out_control->tick = 0U;
    }
    if (runtime == NULL || packet == NULL || packet_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network control decode requires runtime and packet");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network control decode requires an authored network schema");
        return false;
    }

    size_t required_size = 0U;
    (void)game_data_network_control_packet_size(&required_size);
    if (packet_size != required_size)
    {
        set_errorf(error_buffer, error_buffer_size, "network control packet requires %zu bytes, packet has %zu bytes",
                   required_size, packet_size);
        return false;
    }

    slayer3d_replication_reader reader;
    slayer3d_replication_reader_init(&reader, packet, packet_size);

    Uint32 magic = 0U;
    Uint32 version = 0U;
    Uint32 tick = 0U;
    Uint32 control_index = 0U;
    Uint8 schema_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE];
    if (!slayer3d_replication_read_uint32(&reader, &magic) || !slayer3d_replication_read_uint32(&reader, &version) ||
        !slayer3d_replication_read_uint32(&reader, &tick) ||
        !slayer3d_replication_read_uint32(&reader, &control_index) ||
        !slayer3d_replication_read_bytes(&reader, schema_hash, sizeof(schema_hash)) ||
        slayer3d_replication_reader_remaining(&reader) != 0U)
    {
        set_error(error_buffer, error_buffer_size, "network control packet is malformed");
        return false;
    }
    if (magic != SLAYER3D_GAME_DATA_NETWORK_CONTROL_MAGIC || version != SLAYER3D_GAME_DATA_NETWORK_CONTROL_VERSION)
    {
        set_error(error_buffer, error_buffer_size, "network control packet has unsupported header");
        return false;
    }
    if (SDL_memcmp(schema_hash, runtime->network_schema_hash, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE) != 0)
    {
        set_error(error_buffer, error_buffer_size, "network control schema hash does not match runtime");
        return false;
    }

    yyjson_val *control = game_data_find_network_control_by_index(runtime, control_index);
    if (control == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network control message index is invalid for this runtime");
        return false;
    }

    const slayer3d_game_data_network_direction direction =
        game_data_network_direction_from_string(json_string(control, "direction", NULL));
    const int signal_id = game_data_network_control_signal_id(runtime, control);
    if (direction == SLAYER3D_GAME_DATA_NETWORK_DIRECTION_INVALID || signal_id < 0)
    {
        set_error(error_buffer, error_buffer_size, "network control message metadata is invalid for this runtime");
        return false;
    }

    if (out_control != NULL)
    {
        out_control->name = json_string(control, "name", NULL);
        out_control->direction = direction;
        out_control->signal_id = signal_id;
        out_control->tick = tick;
    }
    return true;
}

bool slayer3d_game_data_decode_network_runtime_control(const slayer3d_game_data_runtime *runtime, const void *packet,
                                                       size_t packet_size, const char **out_binding,
                                                       slayer3d_game_data_network_control *out_control,
                                                       char *error_buffer, int error_buffer_size)
{
    slayer3d_game_data_network_control control;
    if (out_binding != NULL)
        *out_binding = NULL;
    if (!slayer3d_game_data_decode_network_control(runtime, packet, packet_size, &control, error_buffer,
                                                   error_buffer_size))
    {
        return false;
    }

    const char *binding = NULL;
    if (!slayer3d_game_data_get_network_runtime_control_binding(runtime, control.name, &binding))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime control binding for '%s' not found",
                   control.name != NULL ? control.name : "<null>");
        return false;
    }

    if (out_binding != NULL)
        *out_binding = binding;
    if (out_control != NULL)
        *out_control = control;
    return true;
}

bool slayer3d_game_data_send_network_runtime_control(const slayer3d_game_data_runtime *runtime,
                                                     slayer3d_network_session *session, const char *binding_name,
                                                     Uint32 tick, char *error_buffer, int error_buffer_size)
{
    Uint8 packet[SLAYER3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE];
    size_t packet_size = 0U;
    if (session == NULL || !slayer3d_network_session_is_connected(session))
    {
        set_error(error_buffer, error_buffer_size, "network runtime control send requires connected session");
        return false;
    }
    if (!slayer3d_game_data_encode_network_runtime_control(runtime, binding_name, tick, packet, sizeof(packet),
                                                           &packet_size, error_buffer, error_buffer_size))
    {
        return false;
    }
    if (!slayer3d_network_session_send(session, packet, (int)packet_size))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime control send failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool slayer3d_game_data_apply_network_control(slayer3d_game_data_runtime *runtime, const void *packet,
                                              size_t packet_size, slayer3d_game_data_network_control *out_control,
                                              char *error_buffer, int error_buffer_size)
{
    slayer3d_game_data_network_control control;
    if (!slayer3d_game_data_decode_network_control(runtime, packet, packet_size, &control, error_buffer,
                                                   error_buffer_size))
        return false;

    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network control failed to allocate signal payload");
        return false;
    }

    slayer3d_properties_set_string(payload, "network_control", control.name != NULL ? control.name : "");
    slayer3d_properties_set_string(payload, "network_direction", game_data_network_direction_name(control.direction));
    slayer3d_properties_set_int(payload, "network_tick", (int)SDL_min(control.tick, (Uint32)SDL_MAX_SINT32));
    slayer3d_signal_emit(runtime_bus(runtime), control.signal_id, payload);
    slayer3d_properties_destroy(payload);

    if (out_control != NULL)
        *out_control = control;
    return true;
}

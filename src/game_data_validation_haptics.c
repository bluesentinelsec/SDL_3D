/**
 * @file game_data_validation_haptics.c
 * @brief Haptics JSON game data validation.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static bool is_non_empty_string(yyjson_val *object, const char *key)
{
    const char *value = json_string(object, key);
    return value != NULL && value[0] != '\0';
}

static bool validate_haptics_actor_filter(validation_context *ctx, yyjson_val *filter, const char *path,
                                          validation_names *names)
{
    if (!yyjson_is_obj(filter))
        return validation_error(ctx, path, "haptics payload actor filter must be an object");
    if (!is_non_empty_string(filter, "key"))
        return validation_error(ctx, path, "haptics payload actor filter requires a non-empty key");

    const char *actor = json_string(filter, "actor");
    yyjson_val *tags = obj_get(filter, "tags");
    if (actor == NULL && tags == NULL)
        return validation_error(ctx, path, "haptics payload actor filter requires actor or tags");
    if (actor != NULL && !require_ref(ctx, &names->entities, "entity", actor, path))
        return false;
    if (tags != NULL)
    {
        if (!yyjson_is_arr(tags) || yyjson_arr_size(tags) == 0)
            return validation_error(ctx, path, "haptics payload actor filter tags must be a non-empty array");
        for (size_t i = 0; i < yyjson_arr_size(tags); ++i)
        {
            yyjson_val *tag = yyjson_arr_get(tags, i);
            if (!yyjson_is_str(tag) || yyjson_get_len(tag) == 0)
                return validation_error(ctx, path, "haptics payload actor filter tags must be non-empty strings");
        }
    }

    char condition_path[PATH_BUFFER_SIZE];
    format_path(condition_path, sizeof(condition_path), "%s.active_if", path);
    return validate_data_condition(ctx, obj_get(filter, "active_if"), condition_path, names);
}

bool validate_haptics(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *haptics = obj_get(root, "haptics");
    if (haptics == NULL)
        return true;
    if (!yyjson_is_obj(haptics))
        return validation_error(ctx, "$.haptics", "haptics must be an object");

    yyjson_val *policies = obj_get(haptics, "policies");
    if (policies == NULL)
        return true;
    if (!yyjson_is_arr(policies))
        return validation_error(ctx, "$.haptics.policies", "haptics policies must be an array");

    name_table policy_names;
    SDL_zero(policy_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(policies); ++i)
    {
        yyjson_val *policy = yyjson_arr_get(policies, i);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.haptics.policies[%zu]", i);
        if (!yyjson_is_obj(policy))
        {
            ok = validation_error(ctx, path, "haptics policy must be an object");
            break;
        }
        if (!require_unique_name(ctx, &policy_names, "haptics policy", json_string(policy, "name"), path) ||
            !require_ref(ctx, &names->signals, "signal", json_string(policy, "signal"), path))
        {
            ok = false;
            break;
        }

        yyjson_val *low = obj_get(policy, "low_frequency");
        yyjson_val *high = obj_get(policy, "high_frequency");
        yyjson_val *duration = obj_get(policy, "duration_ms");
        if (!yyjson_is_num(low) || yyjson_get_num(low) < 0.0 || yyjson_get_num(low) > 1.0)
        {
            ok = validation_error(ctx, path, "haptics low_frequency must be a number from 0 to 1");
            break;
        }
        if (!yyjson_is_num(high) || yyjson_get_num(high) < 0.0 || yyjson_get_num(high) > 1.0)
        {
            ok = validation_error(ctx, path, "haptics high_frequency must be a number from 0 to 1");
            break;
        }
        if (!yyjson_is_int(duration) || yyjson_get_sint(duration) <= 0)
        {
            ok = validation_error(ctx, path, "haptics duration_ms must be a positive integer");
            break;
        }

        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.enabled_if", path);
        if (!validate_data_condition(ctx, obj_get(policy, "enabled_if"), condition_path, names))
        {
            ok = false;
            break;
        }

        yyjson_val *filters = obj_get(policy, "payload_actor_filters");
        if (filters == NULL)
            continue;
        if (!yyjson_is_arr(filters) || yyjson_arr_size(filters) == 0)
        {
            ok = validation_error(ctx, path, "haptics payload_actor_filters must be a non-empty array");
            break;
        }
        for (size_t filter_index = 0; filter_index < yyjson_arr_size(filters); ++filter_index)
        {
            char filter_path[PATH_BUFFER_SIZE];
            format_path(filter_path, sizeof(filter_path), "%s.payload_actor_filters[%zu]", path, filter_index);
            if (!validate_haptics_actor_filter(ctx, yyjson_arr_get(filters, filter_index), filter_path, names))
            {
                ok = false;
                break;
            }
        }
    }

    name_table_destroy(&policy_names);
    return ok;
}

/**
 * @file game_data_validation_persistence.c
 * @brief Storage and persistence JSON game data validation.
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

static bool is_storage_path_segment(const char *value)
{
    if (value == NULL || value[0] == '\0')
        return false;
    if (SDL_strcmp(value, ".") == 0 || SDL_strcmp(value, "..") == 0)
        return false;
    return SDL_strchr(value, '/') == NULL && SDL_strchr(value, '\\') == NULL && SDL_strchr(value, ':') == NULL;
}

static bool is_virtual_storage_path(const char *value)
{
    return value != NULL && (SDL_strncmp(value, "user://", 7) == 0 || SDL_strncmp(value, "cache://", 8) == 0);
}

static bool validate_storage_string(validation_context *ctx, yyjson_val *storage, const char *key,
                                    const char *json_path, bool path_segment)
{
    yyjson_val *value = obj_get(storage, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0')
        return validation_error(ctx, json_path, "storage field must be a non-empty string");
    if (path_segment && !is_storage_path_segment(yyjson_get_str(value)))
        return validation_error(ctx, json_path, "storage field must be a safe path segment");
    return true;
}

bool validate_storage(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *storage = obj_get(root, "storage");
    if (storage == NULL)
        return true;
    if (!yyjson_is_obj(storage))
        return validation_error(ctx, "$.storage", "storage must be an object");

    return validate_storage_string(ctx, storage, "organization", "$.storage.organization", true) &&
           validate_storage_string(ctx, storage, "application", "$.storage.application", true) &&
           validate_storage_string(ctx, storage, "profile", "$.storage.profile", true) &&
           validate_storage_string(ctx, storage, "user_root_override", "$.storage.user_root_override", false) &&
           validate_storage_string(ctx, storage, "cache_root_override", "$.storage.cache_root_override", false);
}

static bool validate_persistence_properties(validation_context *ctx, yyjson_val *properties, const char *json_path)
{
    if (!yyjson_is_arr(properties) || yyjson_arr_size(properties) == 0)
        return validation_error(ctx, json_path, "persistence properties must be a non-empty array");

    for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *property = yyjson_arr_get(properties, i);
        if (yyjson_is_str(property) && yyjson_get_str(property)[0] != '\0')
            continue;
        if (yyjson_is_obj(property) && is_non_empty_string(property, "key"))
            continue;
        return validation_error(ctx, path, "persistence property must be a non-empty string or object with key");
    }
    return true;
}

bool validate_persistence(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *persistence = obj_get(root, "persistence");
    if (persistence == NULL)
        return true;
    if (!yyjson_is_obj(persistence))
        return validation_error(ctx, "$.persistence", "persistence must be an object");

    yyjson_val *entries = obj_get(persistence, "entries");
    if (entries == NULL)
        return true;
    if (!yyjson_is_arr(entries))
        return validation_error(ctx, "$.persistence.entries", "persistence.entries must be an array");

    for (size_t i = 0; i < yyjson_arr_size(entries); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.persistence.entries[%zu]", i);
        yyjson_val *entry = yyjson_arr_get(entries, i);
        if (!yyjson_is_obj(entry))
            return validation_error(ctx, path, "persistence entry must be an object");
        if (!require_unique_name(ctx, &names->persistence, "persistence entry", json_string(entry, "name"), path))
            return false;

        char field_path[PATH_BUFFER_SIZE];
        format_path(field_path, sizeof(field_path), "%s.path", path);
        const char *storage_path = json_string(entry, "path");
        if (storage_path == NULL || storage_path[0] == '\0' || !is_virtual_storage_path(storage_path))
            return validation_error(ctx, field_path, "persistence path must use user:// or cache://");
        if (!require_ref(ctx, &names->entities, "entity", json_string(entry, "target"), path))
            return false;
        format_path(field_path, sizeof(field_path), "%s.properties", path);
        if (!validate_persistence_properties(ctx, obj_get(entry, "properties"), field_path))
            return false;
        yyjson_val *schema = obj_get(entry, "schema");
        if (schema != NULL && (!yyjson_is_str(schema) || yyjson_get_str(schema)[0] == '\0'))
            return validation_error(ctx, path, "persistence schema must be a non-empty string");
        yyjson_val *version = obj_get(entry, "version");
        if (version != NULL && !yyjson_is_int(version))
            return validation_error(ctx, path, "persistence version must be an integer");
        yyjson_val *condition = obj_get(entry, "enabled_if");
        if (condition != NULL)
        {
            format_path(field_path, sizeof(field_path), "%s.enabled_if", path);
            if (!validate_data_condition(ctx, condition, field_path, names))
                return false;
        }
    }
    return true;
}

/**
 * @file game_data_validation_conditions.c
 * @brief Validation helpers for authored data conditions.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL.h>

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

static bool is_compare_op(const char *op)
{
    return op != NULL && (SDL_strcmp(op, ">=") == 0 || SDL_strcmp(op, ">") == 0 || SDL_strcmp(op, "<=") == 0 ||
                          SDL_strcmp(op, "<") == 0 || SDL_strcmp(op, "==") == 0 || SDL_strcmp(op, "!=") == 0);
}

bool validate_data_condition(validation_context *ctx, yyjson_val *condition, const char *path, validation_names *names)
{
    if (condition == NULL)
        return true;
    if (!yyjson_is_obj(condition))
        return validation_error(ctx, path, "UI condition must be an object");

    const char *type = json_string(condition, "type");
    if (SDL_strcmp(type != NULL ? type : "", "always") == 0 || SDL_strcmp(type != NULL ? type : "", "app.paused") == 0)
        return true;
    if (SDL_strcmp(type != NULL ? type : "", "camera.active") == 0)
        return require_ref(ctx, &names->cameras, "camera", json_string(condition, "camera"), path);
    if (SDL_strcmp(type != NULL ? type : "", "property.compare") == 0 ||
        SDL_strcmp(type != NULL ? type : "", "property.bool") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(condition, "target"), path))
            return false;
        if (!is_non_empty_string(condition, "key"))
            return validation_error(ctx, path, "property condition requires a non-empty key");
        if (SDL_strcmp(type, "property.compare") == 0 && !is_compare_op(json_string(condition, "op")))
            return validation_error(ctx, path, "property.compare condition requires a supported comparison operator");
        if (SDL_strcmp(type, "property.compare") == 0 && obj_get(condition, "value") == NULL)
            return validation_error(ctx, path, "property.compare condition requires a value");
        return true;
    }
    if (SDL_strcmp(type != NULL ? type : "", "scene_state.compare") == 0)
    {
        if (!is_non_empty_string(condition, "key"))
            return validation_error(ctx, path, "scene_state.compare condition requires a non-empty key");
        if (!is_compare_op(json_string(condition, "op")))
            return validation_error(ctx, path,
                                    "scene_state.compare condition requires a supported comparison operator");
        if (obj_get(condition, "value") == NULL)
            return validation_error(ctx, path, "scene_state.compare condition requires a value");
        return true;
    }
    if (SDL_strcmp(type != NULL ? type : "", "payload.compare") == 0)
    {
        if (!is_non_empty_string(condition, "key"))
            return validation_error(ctx, path, "payload.compare condition requires a non-empty key");
        if (!is_compare_op(json_string(condition, "op")))
            return validation_error(ctx, path, "payload.compare condition requires a supported comparison operator");
        if (obj_get(condition, "value") == NULL)
            return validation_error(ctx, path, "payload.compare condition requires a value");
        return true;
    }
    if (SDL_strcmp(type != NULL ? type : "", "not") == 0)
    {
        char child_path[PATH_BUFFER_SIZE];
        format_path(child_path, sizeof(child_path), "%s.condition", path);
        return validate_data_condition(ctx, obj_get(condition, "condition"), child_path, names);
    }
    if (SDL_strcmp(type != NULL ? type : "", "all") == 0 || SDL_strcmp(type != NULL ? type : "", "any") == 0)
    {
        yyjson_val *conditions = obj_get(condition, "conditions");
        if (!yyjson_is_arr(conditions))
            return validation_error(ctx, path, "%s condition requires a conditions array", type);
        for (size_t i = 0; i < yyjson_arr_size(conditions); ++i)
        {
            char child_path[PATH_BUFFER_SIZE];
            format_path(child_path, sizeof(child_path), "%s.conditions[%zu]", path, i);
            if (!validate_data_condition(ctx, yyjson_arr_get(conditions, i), child_path, names))
                return false;
        }
        return true;
    }
    return validation_error(ctx, path, "unsupported condition type '%s'", type != NULL ? type : "<missing>");
}

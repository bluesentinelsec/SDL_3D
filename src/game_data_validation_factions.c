/**
 * @file game_data_validation_factions.c
 * @brief Validation helpers for authored faction relationship data.
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

static bool faction_relationship_valid(const char *value)
{
    return value != NULL && (SDL_strcmp(value, "friendly") == 0 || SDL_strcmp(value, "hostile") == 0 ||
                             SDL_strcmp(value, "neutral") == 0 || SDL_strcmp(value, "ignored") == 0);
}

bool validate_factions(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *factions = obj_get(root, "factions");
    if (factions == NULL)
        return true;
    if (!yyjson_is_obj(factions))
        return validation_error(ctx, "$.factions", "factions must be an object");
    const char *default_relationship = json_string(factions, "default_relationship");
    if (default_relationship != NULL && !faction_relationship_valid(default_relationship))
    {
        return validation_error(ctx, "$.factions",
                                "factions default_relationship must be friendly, hostile, neutral, or ignored");
    }

    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(factions, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        value = yyjson_obj_iter_get_val(key);
        if (SDL_strcmp(name != NULL ? name : "", "default_relationship") == 0)
            continue;
        if (name == NULL || name[0] == '\0')
            return validation_error(ctx, "$.factions", "faction names must be non-empty");
        if (!yyjson_is_obj(value))
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.factions.%s", name);
            return validation_error(ctx, path, "faction entries must be objects");
        }
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.factions.%s", name);
        yyjson_val *rel_key;
        yyjson_val *rel_value;
        yyjson_obj_iter rel_iter;
        yyjson_obj_iter_init(value, &rel_iter);
        while ((rel_key = yyjson_obj_iter_next(&rel_iter)) != NULL)
        {
            const char *target = yyjson_get_str(rel_key);
            rel_value = yyjson_obj_iter_get_val(rel_key);
            if (target == NULL || target[0] == '\0' || !yyjson_is_str(rel_value) ||
                !faction_relationship_valid(yyjson_get_str(rel_value)))
            {
                return validation_error(
                    ctx, path,
                    "faction relationships must map non-empty faction names to friendly, hostile, neutral, or ignored");
            }
        }
    }
    return true;
}

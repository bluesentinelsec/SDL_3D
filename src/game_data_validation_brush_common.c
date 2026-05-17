/**
 * @file game_data_validation_brush_common.c
 * @brief Shared brush-world validation helpers.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

bool brush_content_name_valid(const char *name)
{
    static const char *const names[] = {"solid", "player_clip", "projectile_clip", "trigger", "water", "lava", "sky"};
    for (size_t i = 0; name != NULL && i < SDL_arraysize(names); ++i)
    {
        if (SDL_strcmp(name, names[i]) == 0)
            return true;
    }
    return false;
}

bool brush_surface_flag_name_valid(const char *name)
{
    static const char *const names[] = {"nocollide", "slick", "ladder", "emissive", "portal_candidate"};
    for (size_t i = 0; name != NULL && i < SDL_arraysize(names); ++i)
    {
        if (SDL_strcmp(name, names[i]) == 0)
            return true;
    }
    return false;
}

bool validate_brush_string_or_string_array(validation_context *ctx, yyjson_val *value, const char *path,
                                           const char *label, bool (*name_valid)(const char *name), bool allow_empty)
{
    if (value == NULL)
        return true;
    if (yyjson_is_str(value))
    {
        const char *name = yyjson_get_str(value);
        if (name == NULL || name[0] == '\0' || (name_valid != NULL && !name_valid(name)))
            return validation_error(ctx, path, "%s value is unknown", label);
        return true;
    }
    if (!yyjson_is_arr(value))
        return validation_error(ctx, path, "%s must be a string or string array", label);
    if (!allow_empty && yyjson_arr_size(value) <= 0)
        return validation_error(ctx, path, "%s array must be non-empty", label);

    name_table names;
    SDL_zero(names);
    bool ok = true;
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        char entry_path[PATH_BUFFER_SIZE];
        format_path(entry_path, sizeof(entry_path), "%s[%zu]", path, i);
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (!yyjson_is_str(entry) || yyjson_get_str(entry) == NULL || yyjson_get_str(entry)[0] == '\0')
        {
            ok = validation_error(ctx, entry_path, "%s entries must be non-empty strings", label);
            break;
        }
        const char *name = yyjson_get_str(entry);
        if (name_valid != NULL && !name_valid(name))
        {
            ok = validation_error(ctx, entry_path, "%s value is unknown", label);
            break;
        }
        if (!require_unique_name(ctx, &names, label, name, entry_path))
        {
            ok = false;
            break;
        }
    }
    name_table_destroy(&names);
    return ok;
}

/**
 * @file game_data_validation_grid.c
 * @brief Grid map and pickup-layer JSON game data validation.
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

bool collect_grid_maps(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *maps = obj_get(root, "grid_maps");
    if (maps == NULL)
        return true;
    if (!yyjson_is_arr(maps))
        return validation_error(ctx, "$.grid_maps", "grid_maps must be an array");

    for (size_t i = 0; i < yyjson_arr_size(maps); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.grid_maps[%zu]", i);
        yyjson_val *map = yyjson_arr_get(maps, i);
        if (!yyjson_is_obj(map))
            return validation_error(ctx, path, "grid map entries must be objects");
        if (!require_unique_name(ctx, &names->grid_maps, "grid map", json_string(map, "name"), path))
            return false;
    }
    return true;
}

bool collect_grid_pickup_layers(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *layers = obj_get(root, "grid_pickup_layers");
    if (layers == NULL)
        return true;
    if (!yyjson_is_arr(layers))
        return validation_error(ctx, "$.grid_pickup_layers", "grid_pickup_layers must be an array");

    for (size_t i = 0; i < yyjson_arr_size(layers); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.grid_pickup_layers[%zu]", i);
        yyjson_val *layer = yyjson_arr_get(layers, i);
        if (!yyjson_is_obj(layer))
            return validation_error(ctx, path, "grid pickup layer entries must be objects");
        if (!require_unique_name(ctx, &names->grid_pickup_layers, "grid pickup layer", json_string(layer, "name"),
                                 path))
            return false;
    }
    return true;
}

bool is_single_byte_string(yyjson_val *value)
{
    return yyjson_is_str(value) && SDL_strlen(yyjson_get_str(value)) == 1U;
}

bool validate_grid_maps(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *maps = obj_get(root, "grid_maps");
    for (size_t i = 0; yyjson_is_arr(maps) && i < yyjson_arr_size(maps); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.grid_maps[%zu]", i);
        yyjson_val *map = yyjson_arr_get(maps, i);
        yyjson_val *rows = obj_get(map, "rows");
        if (!yyjson_is_arr(rows) || yyjson_arr_size(rows) <= 0)
            return validation_error(ctx, path, "grid map rows must be a non-empty array");
        yyjson_val *first = yyjson_arr_get(rows, 0);
        if (!yyjson_is_str(first) || SDL_strlen(yyjson_get_str(first)) <= 0)
        {
            char row_path[PATH_BUFFER_SIZE];
            format_path(row_path, sizeof(row_path), "%s.rows[0]", path);
            return validation_error(ctx, row_path, "grid map rows must be non-empty strings");
        }
        const size_t width = SDL_strlen(yyjson_get_str(first));
        for (size_t row = 0; row < yyjson_arr_size(rows); ++row)
        {
            yyjson_val *row_value = yyjson_arr_get(rows, row);
            if (!yyjson_is_str(row_value) || SDL_strlen(yyjson_get_str(row_value)) != width)
                return validation_error(ctx, path, "grid map rows must have identical widths");
        }
        yyjson_val *cell_size = obj_get(map, "cell_size");
        if (cell_size != NULL && !is_vec_array(cell_size, 2))
            return validation_error(ctx, path, "grid map cell_size must be a vec2");
        if (cell_size != NULL && (yyjson_get_num(yyjson_arr_get(cell_size, 0)) <= 0.0 ||
                                  yyjson_get_num(yyjson_arr_get(cell_size, 1)) <= 0.0))
            return validation_error(ctx, path, "grid map cell_size values must be positive");
        yyjson_val *origin = obj_get(map, "origin");
        if (origin != NULL && !is_vec_array(origin, 3))
            return validation_error(ctx, path, "grid map origin must be a vec3");
        yyjson_val *row_direction = obj_get(map, "row_direction");
        if (row_direction != NULL && !yyjson_is_num(row_direction))
            return validation_error(ctx, path, "grid map row_direction must be numeric");
        yyjson_val *walkable = obj_get(map, "walkable");
        if (!yyjson_is_arr(walkable) || yyjson_arr_size(walkable) <= 0)
            return validation_error(ctx, path, "grid map walkable must be a non-empty array");
        for (size_t glyph = 0; glyph < yyjson_arr_size(walkable); ++glyph)
        {
            if (!is_single_byte_string(yyjson_arr_get(walkable, glyph)))
                return validation_error(ctx, path, "grid map walkable entries must be single-byte glyph strings");
        }
        yyjson_val *wrap_x = obj_get(map, "wrap_x");
        yyjson_val *wrap_y = obj_get(map, "wrap_y");
        if ((wrap_x != NULL && !yyjson_is_bool(wrap_x)) || (wrap_y != NULL && !yyjson_is_bool(wrap_y)))
            return validation_error(ctx, path, "grid map wrap flags must be booleans");
    }
    return true;
}

bool validate_grid_pickup_layers(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *layers = obj_get(root, "grid_pickup_layers");
    if (layers == NULL)
        return true;
    if (!yyjson_is_arr(layers))
        return validation_error(ctx, "$.grid_pickup_layers", "grid_pickup_layers must be an array");

    for (size_t i = 0; i < yyjson_arr_size(layers); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.grid_pickup_layers[%zu]", i);
        yyjson_val *layer = yyjson_arr_get(layers, i);
        if (!yyjson_is_obj(layer))
            return validation_error(ctx, path, "grid pickup layer entries must be objects");
        if (!require_ref(ctx, &names->grid_maps, "grid map", json_string(layer, "map"), path))
            return false;
        yyjson_val *kinds = obj_get(layer, "kinds");
        if (!yyjson_is_arr(kinds) || yyjson_arr_size(kinds) <= 0)
            return validation_error(ctx, path, "grid pickup layer kinds must be a non-empty array");
        name_table glyphs;
        SDL_zero(glyphs);
        bool ok = true;
        for (size_t k = 0; ok && k < yyjson_arr_size(kinds); ++k)
        {
            char kind_path[PATH_BUFFER_SIZE];
            format_path(kind_path, sizeof(kind_path), "%s.kinds[%zu]", path, k);
            yyjson_val *kind = yyjson_arr_get(kinds, k);
            if (!yyjson_is_obj(kind))
            {
                ok = validation_error(ctx, kind_path, "grid pickup kind entries must be objects");
                break;
            }
            yyjson_val *glyph = obj_get(kind, "glyph");
            if (!is_single_byte_string(glyph))
            {
                ok = validation_error(ctx, kind_path, "grid pickup kind glyph must be a single-byte string");
                break;
            }
            if (!require_unique_name(ctx, &glyphs, "grid pickup glyph", yyjson_get_str(glyph), kind_path))
            {
                ok = false;
                break;
            }
            if (!is_non_empty_string(kind, "kind"))
            {
                ok = validation_error(ctx, kind_path, "grid pickup kind requires a non-empty kind");
                break;
            }
            yyjson_val *points = obj_get(kind, "points");
            if (points != NULL && !yyjson_is_int(points))
            {
                ok = validation_error(ctx, kind_path, "grid pickup points must be an integer");
                break;
            }
            yyjson_val *z = obj_get(kind, "z");
            if (z != NULL && !yyjson_is_num(z))
            {
                ok = validation_error(ctx, kind_path, "grid pickup z must be numeric");
                break;
            }
            yyjson_val *radius = obj_get(kind, "radius");
            if (radius != NULL && (!yyjson_is_num(radius) || yyjson_get_num(radius) <= 0.0))
            {
                ok = validation_error(ctx, kind_path, "grid pickup radius must be positive");
                break;
            }
            yyjson_val *rings = obj_get(kind, "rings");
            yyjson_val *slices = obj_get(kind, "slices");
            if ((rings != NULL && (!yyjson_is_int(rings) || yyjson_get_int(rings) < 3)) ||
                (slices != NULL && (!yyjson_is_int(slices) || yyjson_get_int(slices) < 3)))
            {
                ok = validation_error(ctx, kind_path, "grid pickup rings and slices must be integers >= 3");
                break;
            }
            yyjson_val *color = obj_get(kind, "color");
            if (color != NULL && !is_vec_array(color, 4))
            {
                ok = validation_error(ctx, kind_path, "grid pickup color must be a color vec4");
                break;
            }
            yyjson_val *lighting = obj_get(kind, "lighting");
            yyjson_val *emissive = obj_get(kind, "emissive");
            if ((lighting != NULL && !yyjson_is_bool(lighting)) || (emissive != NULL && !yyjson_is_bool(emissive)))
            {
                ok = validation_error(ctx, kind_path, "grid pickup lighting and emissive must be booleans");
                break;
            }
        }
        name_table_destroy(&glyphs);
        if (!ok)
            return false;
    }
    return true;
}

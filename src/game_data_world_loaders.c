/**
 * @file game_data_world_loaders.c
 * @brief Grid, sector, brush, door, and platform loaders for authored game data.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/door.h"
#include "slayer3d/level.h"
#include "slayer3d/math.h"

static bool load_editor_metadata(yyjson_val *editor, slayer3d_game_data_editor_metadata *out_metadata,
                                 char *error_buffer, int error_buffer_size);

bool load_grid_maps(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size)
{
    yyjson_val *maps = obj_get(root, "grid_maps");
    if (maps == NULL)
        return true;
    if (!yyjson_is_arr(maps))
    {
        set_error(error_buffer, error_buffer_size, "grid_maps must be an array");
        return false;
    }

    runtime->grid_map_count = (int)yyjson_arr_size(maps);
    runtime->grid_maps = (grid_map_runtime *)SDL_calloc((size_t)runtime->grid_map_count, sizeof(*runtime->grid_maps));
    if (runtime->grid_maps == NULL && runtime->grid_map_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate grid maps");
        return false;
    }

    for (int i = 0; i < runtime->grid_map_count; ++i)
    {
        yyjson_val *map_json = yyjson_arr_get(maps, (size_t)i);
        yyjson_val *rows = obj_get(map_json, "rows");
        const int height = yyjson_is_arr(rows) ? (int)yyjson_arr_size(rows) : 0;
        const char *first_row =
            height > 0 && yyjson_is_str(yyjson_arr_get(rows, 0)) ? yyjson_get_str(yyjson_arr_get(rows, 0)) : NULL;
        const int width = first_row != NULL ? (int)SDL_strlen(first_row) : 0;
        if (width <= 0 || height <= 0)
        {
            set_error(error_buffer, error_buffer_size, "grid map rows must be non-empty strings");
            return false;
        }

        grid_map_runtime *map = &runtime->grid_maps[i];
        map->name = SDL_strdup(json_string(map_json, "name", ""));
        map->cells = (char *)SDL_malloc((size_t)(width * height + 1));
        map->width = width;
        map->height = height;
        map->cell_width = 1.0f;
        map->cell_height = 1.0f;
        if (!json_vec2_value(obj_get(map_json, "cell_size"), 1.0f, 1.0f, &map->cell_width, &map->cell_height))
        {
            set_error(error_buffer, error_buffer_size, "grid map cell_size must be a vec2");
            return false;
        }
        map->origin = json_vec3(map_json, "origin", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        map->row_direction = json_float(map_json, "row_direction", -1.0f) < 0.0f ? -1.0f : 1.0f;
        map->wrap_x = json_bool(map_json, "wrap_x", false);
        map->wrap_y = json_bool(map_json, "wrap_y", false);
        yyjson_val *walkable = obj_get(map_json, "walkable");
        size_t walkable_count = yyjson_is_arr(walkable) ? yyjson_arr_size(walkable) : 0;
        map->walkable = (char *)SDL_malloc(walkable_count + 1u);
        if (map->name == NULL || map->cells == NULL || map->walkable == NULL)
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate grid map data");
            return false;
        }
        for (int row = 0; row < height; ++row)
        {
            yyjson_val *row_value = yyjson_arr_get(rows, (size_t)row);
            const char *row_text = yyjson_is_str(row_value) ? yyjson_get_str(row_value) : NULL;
            if (row_text == NULL || (int)SDL_strlen(row_text) != width)
            {
                set_error(error_buffer, error_buffer_size, "grid map rows must have identical widths");
                return false;
            }
            SDL_memcpy(map->cells + row * width, row_text, (size_t)width);
        }
        map->cells[width * height] = '\0';
        for (size_t w = 0; w < walkable_count; ++w)
        {
            const char *glyph =
                yyjson_is_str(yyjson_arr_get(walkable, w)) ? yyjson_get_str(yyjson_arr_get(walkable, w)) : NULL;
            map->walkable[w] = glyph != NULL ? glyph[0] : '\0';
        }
        map->walkable[walkable_count] = '\0';
    }
    return true;
}

bool load_grid_pickup_layers(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                             int error_buffer_size)
{
    yyjson_val *layers = obj_get(root, "grid_pickup_layers");
    if (layers == NULL)
        return true;
    if (!yyjson_is_arr(layers))
    {
        set_error(error_buffer, error_buffer_size, "grid_pickup_layers must be an array");
        return false;
    }

    runtime->grid_pickup_layer_count = (int)yyjson_arr_size(layers);
    runtime->grid_pickup_layers = (grid_pickup_layer_runtime *)SDL_calloc((size_t)runtime->grid_pickup_layer_count,
                                                                          sizeof(*runtime->grid_pickup_layers));
    if (runtime->grid_pickup_layers == NULL && runtime->grid_pickup_layer_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate grid pickup layers");
        return false;
    }

    for (int i = 0; i < runtime->grid_pickup_layer_count; ++i)
    {
        yyjson_val *layer_json = yyjson_arr_get(layers, (size_t)i);
        const grid_map_runtime *map = find_grid_map(runtime, json_string(layer_json, "map", NULL));
        yyjson_val *kinds = obj_get(layer_json, "kinds");
        if (map == NULL || !yyjson_is_arr(kinds) || yyjson_arr_size(kinds) <= 0)
        {
            set_error(error_buffer, error_buffer_size, "grid pickup layer requires a map and non-empty kinds");
            return false;
        }

        grid_pickup_layer_runtime *layer = &runtime->grid_pickup_layers[i];
        layer->name = SDL_strdup(json_string(layer_json, "name", ""));
        layer->map = map;
        layer->kind_count = (int)yyjson_arr_size(kinds);
        layer->kinds = (grid_pickup_kind_runtime *)SDL_calloc((size_t)layer->kind_count, sizeof(*layer->kinds));
        layer->cells = (Uint8 *)SDL_calloc((size_t)map->width * (size_t)map->height, sizeof(*layer->cells));
        if (layer->name == NULL || layer->kinds == NULL || layer->cells == NULL)
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate grid pickup layer data");
            return false;
        }

        for (int kind_index = 0; kind_index < layer->kind_count; ++kind_index)
        {
            yyjson_val *kind_json = yyjson_arr_get(kinds, (size_t)kind_index);
            const char *glyph = json_string(kind_json, "glyph", NULL);
            grid_pickup_kind_runtime *kind = &layer->kinds[kind_index];
            kind->glyph = glyph != NULL ? glyph[0] : '\0';
            kind->kind = SDL_strdup(json_string(kind_json, "kind", ""));
            kind->points = json_int(kind_json, "points", 0);
            kind->z = json_float(kind_json, "z", 0.0f);
            kind->radius = json_float(kind_json, "radius", 0.1f);
            kind->rings = SDL_max(json_int(kind_json, "rings", 8), 3);
            kind->slices = SDL_max(json_int(kind_json, "slices", 10), 3);
            kind->color = json_color(kind_json, "color", (slayer3d_color){255, 255, 255, 255});
            kind->lighting = json_bool(kind_json, "lighting", true);
            kind->emissive = json_bool(kind_json, "emissive", false);
            if (kind->kind == NULL)
            {
                set_error(error_buffer, error_buffer_size, "failed to allocate grid pickup kind data");
                return false;
            }
        }
    }
    return true;
}

static void strip_sector_level_lightmap(slayer3d_level *level)
{
    if (level == NULL)
        return;

    SDL_free(level->lightmap_pixels);
    level->lightmap_pixels = NULL;
    level->lightmap_width = 0;
    level->lightmap_height = 0;
    slayer3d_free_texture(&level->lightmap_texture);
    for (int i = 0; i < level->model.mesh_count; ++i)
    {
        SDL_free(level->model.meshes[i].lightmap_uvs);
        level->model.meshes[i].lightmap_uvs = NULL;
    }
}

static int sector_material_index(yyjson_val *materials, yyjson_val *ref, bool allow_none)
{
    if (allow_none && ref == NULL)
        return -1;
    if (yyjson_is_null(ref) && allow_none)
        return -1;
    if (yyjson_is_int(ref))
    {
        const int index = (int)yyjson_get_int(ref);
        if (allow_none && index == -1)
            return -1;
        return (index >= 0 && index < (int)yyjson_arr_size(materials)) ? index : -2;
    }
    if (yyjson_is_str(ref))
    {
        const char *name = yyjson_get_str(ref);
        if (allow_none && name != NULL && SDL_strcmp(name, "none") == 0)
            return -1;
        for (size_t i = 0; yyjson_is_arr(materials) && i < yyjson_arr_size(materials); ++i)
        {
            yyjson_val *material = yyjson_arr_get(materials, i);
            if (SDL_strcmp(json_string(material, "name", ""), name != NULL ? name : "") == 0)
                return (int)i;
        }
    }
    return -2;
}

static bool load_sector_level_materials(sector_level_runtime *level, yyjson_val *materials, char *error_buffer,
                                        int error_buffer_size)
{
    level->material_count = yyjson_is_arr(materials) ? (int)yyjson_arr_size(materials) : 0;
    level->materials = (slayer3d_level_material *)SDL_calloc((size_t)level->material_count, sizeof(*level->materials));
    if (level->materials == NULL && level->material_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate sector level materials");
        return false;
    }

    static const float default_albedo[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    for (int i = 0; i < level->material_count; ++i)
    {
        yyjson_val *material_json = yyjson_arr_get(materials, (size_t)i);
        yyjson_val *albedo = obj_get(material_json, "albedo");
        slayer3d_level_material *material = &level->materials[i];
        SDL_memcpy(material->albedo, default_albedo, sizeof(material->albedo));
        if (yyjson_is_arr(albedo) && yyjson_arr_size(albedo) >= 3)
        {
            for (int channel = 0; channel < 4 && channel < (int)yyjson_arr_size(albedo); ++channel)
            {
                yyjson_val *value = yyjson_arr_get(albedo, (size_t)channel);
                if (yyjson_is_num(value))
                    material->albedo[channel] = (float)yyjson_get_num(value);
            }
        }
        material->metallic = json_float(material_json, "metallic", 0.0f);
        material->roughness = json_float(material_json, "roughness", 1.0f);
        material->texture = json_string(material_json, "texture", NULL);
        material->tex_scale = json_float(material_json, "tex_scale", 4.0f);
    }
    return true;
}

static bool load_sector_level_sectors(sector_level_runtime *level, yyjson_val *materials, yyjson_val *sectors,
                                      char *error_buffer, int error_buffer_size)
{
    level->sector_count = yyjson_is_arr(sectors) ? (int)yyjson_arr_size(sectors) : 0;
    level->sectors = (slayer3d_sector *)SDL_calloc((size_t)level->sector_count, sizeof(*level->sectors));
    level->sector_names = (const char **)SDL_calloc((size_t)level->sector_count, sizeof(*level->sector_names));
    if ((level->sectors == NULL || level->sector_names == NULL) && level->sector_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate sector level sectors");
        return false;
    }

    for (int sector_index = 0; sector_index < level->sector_count; ++sector_index)
    {
        yyjson_val *sector_json = yyjson_arr_get(sectors, (size_t)sector_index);
        yyjson_val *points = obj_get(sector_json, "points");
        slayer3d_sector *sector = &level->sectors[sector_index];

        level->sector_names[sector_index] = json_string(sector_json, "name", NULL);
        sector->num_points = yyjson_is_arr(points) ? (int)yyjson_arr_size(points) : 0;
        for (int point_index = 0; point_index < sector->num_points; ++point_index)
        {
            yyjson_val *point = yyjson_arr_get(points, (size_t)point_index);
            json_vec2_value(point, 0.0f, 0.0f, &sector->points[point_index][0], &sector->points[point_index][1]);
        }

        sector->floor_y = json_float(sector_json, "floor_y", 0.0f);
        sector->ceil_y = json_float(sector_json, "ceil_y", sector->floor_y + 4.0f);
        sector->floor_material = sector_material_index(materials, obj_get(sector_json, "floor_material"), true);
        sector->ceil_material = sector_material_index(materials, obj_get(sector_json, "ceil_material"), true);
        sector->wall_material = sector_material_index(materials, obj_get(sector_json, "wall_material"), false);
        json_float_array(obj_get(sector_json, "floor_normal"), sector->floor_normal, 3, NULL);
        json_float_array(obj_get(sector_json, "ceil_normal"), sector->ceil_normal, 3, NULL);
        sector->ambient_sound_id = json_int(sector_json, "ambient_sound_id", 0);
        json_float_array(obj_get(sector_json, "push_velocity"), sector->push_velocity, 3, NULL);
        sector->damage_per_second = json_float(sector_json, "damage_per_second", 0.0f);
        yyjson_val *lighting = obj_get(sector_json, "lighting");
        if (yyjson_is_obj(lighting))
        {
            static const float default_lighting_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            yyjson_val *color = obj_get(lighting, "color");
            sector->has_lighting = true;
            sector->lighting_level = json_float(lighting, "level", 255.0f);
            if (yyjson_is_arr(color) && yyjson_arr_size(color) == 3U &&
                json_float_array(color, sector->lighting_color, 3, default_lighting_color))
            {
                sector->lighting_color[3] = 1.0f;
            }
            else
            {
                json_float_array(color, sector->lighting_color, 4, default_lighting_color);
            }
        }
    }
    return true;
}

static bool load_sector_level_lights(sector_level_runtime *level, yyjson_val *lights, char *error_buffer,
                                     int error_buffer_size)
{
    level->light_count = yyjson_is_arr(lights) ? (int)yyjson_arr_size(lights) : 0;
    if (level->light_count <= 0)
        return true;

    level->lights = (slayer3d_level_light *)SDL_calloc((size_t)level->light_count, sizeof(*level->lights));
    if (level->lights == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate sector level lights");
        return false;
    }

    static const float default_color[3] = {1.0f, 1.0f, 1.0f};
    for (int i = 0; i < level->light_count; ++i)
    {
        yyjson_val *light_json = yyjson_arr_get(lights, (size_t)i);
        slayer3d_level_light *light = &level->lights[i];
        json_float_array(obj_get(light_json, "position"), light->position, 3, NULL);
        json_float_array(obj_get(light_json, "color"), light->color, 3, default_color);
        light->intensity = json_float(light_json, "intensity", 1.0f);
        light->range = json_float(light_json, "range", 8.0f);
    }
    return true;
}

slayer3d_sector *copy_sectors_without_sector_lighting(const sector_level_runtime *level)
{
    if (level == NULL || level->sector_count <= 0)
        return NULL;

    slayer3d_sector *sectors = (slayer3d_sector *)SDL_malloc(sizeof(*sectors) * (size_t)level->sector_count);
    if (sectors == NULL)
        return NULL;
    SDL_memcpy(sectors, level->sectors, sizeof(*sectors) * (size_t)level->sector_count);
    for (int i = 0; i < level->sector_count; ++i)
    {
        sectors[i].has_lighting = false;
        sectors[i].lighting_level = 255.0f;
        sectors[i].lighting_color[0] = 1.0f;
        sectors[i].lighting_color[1] = 1.0f;
        sectors[i].lighting_color[2] = 1.0f;
        sectors[i].lighting_color[3] = 0.0f;
    }
    return sectors;
}

bool build_sector_level_variant_set(sector_level_runtime *level, const slayer3d_sector *sectors,
                                    slayer3d_level *lightmapped, slayer3d_level *vertex_baked, slayer3d_level *unlit,
                                    const char *label, char *error_buffer, int error_buffer_size)
{
    if (!slayer3d_build_level(sectors, level->sector_count, level->materials, level->material_count, level->lights,
                              level->light_count, lightmapped))
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' %s lightmapped build failed: %s", level->name,
                   label, SDL_GetError());
        return false;
    }
    if (!slayer3d_build_level(sectors, level->sector_count, level->materials, level->material_count, level->lights,
                              level->light_count, vertex_baked))
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' %s vertex-baked build failed: %s", level->name,
                   label, SDL_GetError());
        return false;
    }
    strip_sector_level_lightmap(vertex_baked);
    if (!slayer3d_build_level(sectors, level->sector_count, level->materials, level->material_count, NULL, 0, unlit))
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' %s unlit build failed: %s", level->name, label,
                   SDL_GetError());
        return false;
    }
    return true;
}

static bool build_sector_level_variants(sector_level_runtime *level, char *error_buffer, int error_buffer_size)
{
    if (!build_sector_level_variant_set(level, level->sectors, &level->lightmapped, &level->vertex_baked, &level->unlit,
                                        "sector-lit", error_buffer, error_buffer_size))
    {
        return false;
    }

    slayer3d_sector *unlit_sectors = copy_sectors_without_sector_lighting(level);
    if (unlit_sectors == NULL && level->sector_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate sector lighting toggle variants");
        return false;
    }
    const bool ok = build_sector_level_variant_set(
        level, unlit_sectors != NULL ? unlit_sectors : level->sectors, &level->lightmapped_without_sector_lighting,
        &level->vertex_baked_without_sector_lighting, &level->unlit_without_sector_lighting, "sector-neutral",
        error_buffer, error_buffer_size);
    SDL_free(unlit_sectors);
    return ok;
}

bool set_sector_level_geometry(sector_level_runtime *level, int sector_index, const slayer3d_sector_geometry *geometry,
                               char *error_buffer, int error_buffer_size)
{
    if (level == NULL || geometry == NULL || sector_index < 0 || sector_index >= level->sector_count)
    {
        set_error(error_buffer, error_buffer_size, "sector platform geometry target is invalid");
        return false;
    }
    if (geometry->ceil_y <= geometry->floor_y)
    {
        set_error(error_buffer, error_buffer_size, "sector platform ceiling must be above floor");
        return false;
    }

    if (!slayer3d_level_set_sector_geometry(&level->lightmapped, level->sectors, level->sector_count, sector_index,
                                            geometry, level->materials, level->material_count, level->lights,
                                            level->light_count))
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' lightmapped geometry update failed: %s",
                   level->name, SDL_GetError());
        return false;
    }
    if (!slayer3d_level_set_sector_geometry(&level->vertex_baked, level->sectors, level->sector_count, sector_index,
                                            geometry, level->materials, level->material_count, level->lights,
                                            level->light_count))
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' vertex-baked geometry update failed: %s",
                   level->name, SDL_GetError());
        return false;
    }
    if (!slayer3d_level_set_sector_geometry(&level->unlit, level->sectors, level->sector_count, sector_index, geometry,
                                            level->materials, level->material_count, NULL, 0))
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' unlit geometry update failed: %s", level->name,
                   SDL_GetError());
        return false;
    }

    slayer3d_sector *unlit_sectors = copy_sectors_without_sector_lighting(level);
    if (unlit_sectors == NULL && level->sector_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate sector lighting toggle geometry update");
        return false;
    }
    slayer3d_sector *neutral_sectors = unlit_sectors != NULL ? unlit_sectors : level->sectors;
    bool ok = true;
    if (!slayer3d_level_set_sector_geometry(&level->lightmapped_without_sector_lighting, neutral_sectors,
                                            level->sector_count, sector_index, geometry, level->materials,
                                            level->material_count, level->lights, level->light_count))
    {
        set_errorf(error_buffer, error_buffer_size,
                   "sector level '%s' sector-neutral lightmapped geometry update failed: %s", level->name,
                   SDL_GetError());
        ok = false;
    }
    if (ok && !slayer3d_level_set_sector_geometry(&level->vertex_baked_without_sector_lighting, neutral_sectors,
                                                  level->sector_count, sector_index, geometry, level->materials,
                                                  level->material_count, level->lights, level->light_count))
    {
        set_errorf(error_buffer, error_buffer_size,
                   "sector level '%s' sector-neutral vertex-baked geometry update failed: %s", level->name,
                   SDL_GetError());
        ok = false;
    }
    if (ok &&
        !slayer3d_level_set_sector_geometry(&level->unlit_without_sector_lighting, neutral_sectors, level->sector_count,
                                            sector_index, geometry, level->materials, level->material_count, NULL, 0))
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' sector-neutral unlit geometry update failed: %s",
                   level->name, SDL_GetError());
        ok = false;
    }
    SDL_free(unlit_sectors);
    if (!ok)
        return false;
    return true;
}

bool load_sector_levels(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                        int error_buffer_size)
{
    yyjson_val *levels = obj_get(root, "sector_levels");
    if (levels == NULL)
        return true;
    if (!yyjson_is_arr(levels))
    {
        set_error(error_buffer, error_buffer_size, "sector_levels must be an array");
        return false;
    }

    runtime->sector_level_count = (int)yyjson_arr_size(levels);
    runtime->sector_levels =
        (sector_level_runtime *)SDL_calloc((size_t)runtime->sector_level_count, sizeof(*runtime->sector_levels));
    if (runtime->sector_levels == NULL && runtime->sector_level_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate sector levels");
        return false;
    }

    for (int i = 0; i < runtime->sector_level_count; ++i)
    {
        yyjson_val *level_json = yyjson_arr_get(levels, (size_t)i);
        sector_level_runtime *level = &runtime->sector_levels[i];
        level->name = SDL_strdup(json_string(level_json, "name", ""));
        if (level->name == NULL)
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate sector level name");
            return false;
        }

        yyjson_val *materials = obj_get(level_json, "materials");
        yyjson_val *sectors = obj_get(level_json, "sectors");
        yyjson_val *lights = obj_get(level_json, "lights");
        if (!load_sector_level_materials(level, materials, error_buffer, error_buffer_size) ||
            !load_sector_level_sectors(level, materials, sectors, error_buffer, error_buffer_size) ||
            !load_sector_level_lights(level, lights, error_buffer, error_buffer_size) ||
            !build_sector_level_variants(level, error_buffer, error_buffer_size))
        {
            return false;
        }
    }
    return true;
}

unsigned int brush_content_flag_from_string(const char *name)
{
    if (SDL_strcmp(name != NULL ? name : "", "solid") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    if (SDL_strcmp(name != NULL ? name : "", "player_clip") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP;
    if (SDL_strcmp(name != NULL ? name : "", "projectile_clip") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP;
    if (SDL_strcmp(name != NULL ? name : "", "trigger") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER;
    if (SDL_strcmp(name != NULL ? name : "", "water") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER;
    if (SDL_strcmp(name != NULL ? name : "", "lava") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA;
    if (SDL_strcmp(name != NULL ? name : "", "sky") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY;
    return 0u;
}

static unsigned int brush_surface_flag_from_string(const char *name)
{
    if (SDL_strcmp(name != NULL ? name : "", "nocollide") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_SURFACE_NO_COLLIDE;
    if (SDL_strcmp(name != NULL ? name : "", "slick") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_SURFACE_SLICK;
    if (SDL_strcmp(name != NULL ? name : "", "ladder") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_SURFACE_LADDER;
    if (SDL_strcmp(name != NULL ? name : "", "emissive") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_SURFACE_EMISSIVE;
    if (SDL_strcmp(name != NULL ? name : "", "portal_candidate") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_SURFACE_PORTAL_CANDIDATE;
    return 0u;
}

static slayer3d_game_data_brush_visibility brush_visibility_from_string(const char *name)
{
    if (name == NULL || name[0] == '\0' || SDL_strcmp(name, "auto") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_AUTO;
    if (SDL_strcmp(name, "always") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_ALWAYS;
    if (SDL_strcmp(name, "trace") == 0)
        return SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_TRACE;
    return SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_AUTO;
}

unsigned int brush_flags_from_json(yyjson_val *value, unsigned int (*flag_from_string)(const char *name),
                                   unsigned int fallback)
{
    if (yyjson_is_str(value))
    {
        const unsigned int flag = flag_from_string(yyjson_get_str(value));
        return flag != 0u ? flag : fallback;
    }
    if (!yyjson_is_arr(value))
        return fallback;

    unsigned int flags = 0u;
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (yyjson_is_str(entry))
            flags |= flag_from_string(yyjson_get_str(entry));
    }
    return flags != 0u ? flags : fallback;
}

static int brush_material_index_from_ref(const slayer3d_game_data_brush_material *materials, int material_count,
                                         yyjson_val *ref)
{
    if (yyjson_is_int(ref))
    {
        const int index = (int)yyjson_get_int(ref);
        return index >= 0 && index < material_count ? index : -1;
    }
    if (yyjson_is_str(ref))
    {
        const char *name = yyjson_get_str(ref);
        for (int i = 0; i < material_count; ++i)
        {
            if (materials[i].name != NULL && SDL_strcmp(materials[i].name, name) == 0)
                return i;
        }
    }
    return -1;
}

static bool find_editor_brush_source_boxes(yyjson_val *root, const char *world_name, yyjson_val **out_boxes,
                                           float *out_meters_per_unit, int *out_snap_units, char *error_buffer,
                                           int error_buffer_size)
{
    if (out_boxes != NULL)
        *out_boxes = NULL;
    if (out_meters_per_unit != NULL)
        *out_meters_per_unit = 0.001f;
    if (out_snap_units != NULL)
        *out_snap_units = 1;
    yyjson_val *sources = obj_get(root, "editor_brush_sources");
    if (!yyjson_is_arr(sources))
        return true;

    for (size_t i = 0; i < yyjson_arr_size(sources); ++i)
    {
        yyjson_val *source = yyjson_arr_get(sources, i);
        const char *source_world = json_string(source, "world", NULL);
        if (source_world == NULL || world_name == NULL || SDL_strcmp(source_world, world_name) != 0)
            continue;

        yyjson_val *boxes = obj_get(source, "boxes");
        if (!yyjson_is_arr(boxes))
        {
            set_errorf(error_buffer, error_buffer_size, "editor brush source for world '%s' requires boxes array",
                       world_name);
            return false;
        }
        if (out_boxes != NULL)
            *out_boxes = boxes;
        if (out_meters_per_unit != NULL)
        {
            const float meters_per_unit = json_float(source, "meters_per_unit", 0.001f);
            *out_meters_per_unit = meters_per_unit > 0.0f ? meters_per_unit : 0.001f;
        }
        if (out_snap_units != NULL)
        {
            const int snap_units = json_int(source, "snap_units", 1);
            *out_snap_units = snap_units > 0 ? snap_units : 1;
        }
        return true;
    }
    return true;
}

bool load_brush_worlds(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size)
{
    yyjson_val *worlds = obj_get(root, "brush_worlds");
    if (worlds == NULL)
        return true;
    if (!yyjson_is_arr(worlds))
    {
        set_error(error_buffer, error_buffer_size, "brush_worlds must be an array");
        return false;
    }

    runtime->brush_world_count = (int)yyjson_arr_size(worlds);
    runtime->brush_worlds =
        (brush_world_runtime *)SDL_calloc((size_t)runtime->brush_world_count, sizeof(*runtime->brush_worlds));
    if (runtime->brush_worlds == NULL && runtime->brush_world_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate brush worlds");
        return false;
    }

    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        yyjson_val *world_json = yyjson_arr_get(worlds, (size_t)world_index);
        yyjson_val *materials_json = obj_get(world_json, "materials");
        yyjson_val *source_boxes = NULL;
        float source_meters_per_unit = 0.001f;
        int source_snap_units = 1;
        const char *world_name = json_string(world_json, "name", "");
        if (!find_editor_brush_source_boxes(root, world_name, &source_boxes, &source_meters_per_unit,
                                            &source_snap_units, error_buffer, error_buffer_size))
        {
            return false;
        }
        const bool use_source_boxes = source_boxes != NULL;
        yyjson_val *brushes_json = use_source_boxes ? NULL : obj_get(world_json, "brushes");
        slayer3d_game_data_brush_world *world = &runtime->brush_worlds[world_index].desc;

        world->name = SDL_strdup(world_name);
        world->units = SDL_strdup(json_string(world_json, "units", "meters"));
        world->meters_per_unit = json_float(world_json, "meters_per_unit", 1.0f);
        world->visibility_cell_size = json_float(world_json, "visibility_cell_size", 2.0f);
        yyjson_val *compile_json = obj_get(world_json, "compile");
        world->compile_hidden_face_culling = json_bool(compile_json, "hidden_face_culling", true);
        world->compile_chunk_cell_size_hint = json_float(compile_json, "chunk_cell_size", 0.0f);
        world->material_count = (int)yyjson_arr_size(materials_json);
        world->brush_count = use_source_boxes ? 0 : (int)yyjson_arr_size(brushes_json);
        if (world->name == NULL || world->units == NULL)
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate brush world strings");
            return false;
        }
        if (!load_editor_metadata(obj_get(world_json, "editor"), &world->editor, error_buffer, error_buffer_size))
            return false;

        slayer3d_game_data_brush_material *materials =
            (slayer3d_game_data_brush_material *)SDL_calloc((size_t)world->material_count, sizeof(*materials));
        slayer3d_game_data_brush *brushes =
            (slayer3d_game_data_brush *)SDL_calloc((size_t)world->brush_count, sizeof(*brushes));
        if ((materials == NULL && world->material_count > 0) || (brushes == NULL && world->brush_count > 0))
        {
            SDL_free(materials);
            SDL_free(brushes);
            set_error(error_buffer, error_buffer_size, "failed to allocate brush world data");
            return false;
        }
        world->materials = materials;
        world->brushes = brushes;

        for (int material_index = 0; material_index < world->material_count; ++material_index)
        {
            yyjson_val *material_json = yyjson_arr_get(materials_json, (size_t)material_index);
            slayer3d_game_data_brush_material *material = &materials[material_index];
            material->name = SDL_strdup(json_string(material_json, "name", ""));
            const char *texture = json_string(material_json, "texture", NULL);
            material->texture = texture != NULL ? SDL_strdup(texture) : NULL;
            material->albedo = json_vec4(material_json, "albedo", slayer3d_vec4_make(1.0f, 1.0f, 1.0f, 1.0f));
            material->metallic = json_float(material_json, "metallic", 0.0f);
            material->roughness = json_float(material_json, "roughness", 1.0f);
            material->emissive = json_vec3(material_json, "emissive", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            material->tex_scale = json_float(material_json, "tex_scale", 1.0f);
            if (material->name == NULL || (texture != NULL && material->texture == NULL))
            {
                set_error(error_buffer, error_buffer_size, "failed to allocate brush material strings");
                return false;
            }
            if (!load_editor_metadata(obj_get(material_json, "editor"), &material->editor, error_buffer,
                                      error_buffer_size))
                return false;
        }

        if (use_source_boxes)
        {
            if (!load_editor_brush_source_boxes(&runtime->brush_worlds[world_index], source_boxes,
                                                source_meters_per_unit, source_snap_units, error_buffer,
                                                error_buffer_size) ||
                !editor_brush_world_rebuild_from_source(&runtime->brush_worlds[world_index], error_buffer,
                                                        error_buffer_size))
            {
                return false;
            }
            continue;
        }

        for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
        {
            yyjson_val *brush_json = yyjson_arr_get(brushes_json, (size_t)brush_index);
            yyjson_val *tags_json = obj_get(brush_json, "tags");
            yyjson_val *faces_json = obj_get(brush_json, "faces");
            slayer3d_game_data_brush *brush = &brushes[brush_index];
            brush->name = SDL_strdup(json_string(brush_json, "name", ""));
            brush->contents = brush_flags_from_json(obj_get(brush_json, "contents"), brush_content_flag_from_string,
                                                    SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID);
            brush->visibility_cullable = json_bool(brush_json, "visibility_cullable", false);
            brush->visibility = brush_visibility_from_string(json_string(brush_json, "visibility", NULL));
            if (brush->visibility_cullable && obj_get(brush_json, "visibility") == NULL)
                brush->visibility = SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_TRACE;
            brush->tag_count = yyjson_is_arr(tags_json) ? (int)yyjson_arr_size(tags_json) : 0;
            brush->face_count = (int)yyjson_arr_size(faces_json);
            if (brush->name == NULL)
            {
                set_error(error_buffer, error_buffer_size, "failed to allocate brush name");
                return false;
            }
            if (!load_editor_metadata(obj_get(brush_json, "editor"), &brush->editor, error_buffer, error_buffer_size))
                return false;

            const char **tags =
                brush->tag_count > 0 ? (const char **)SDL_calloc((size_t)brush->tag_count, sizeof(*tags)) : NULL;
            slayer3d_game_data_brush_face *faces =
                (slayer3d_game_data_brush_face *)SDL_calloc((size_t)brush->face_count, sizeof(*faces));
            if ((tags == NULL && brush->tag_count > 0) || (faces == NULL && brush->face_count > 0))
            {
                SDL_free(tags);
                SDL_free(faces);
                set_error(error_buffer, error_buffer_size, "failed to allocate brush entries");
                return false;
            }
            brush->tags = tags;
            brush->faces = faces;

            for (int tag_index = 0; tag_index < brush->tag_count; ++tag_index)
            {
                tags[tag_index] = SDL_strdup(yyjson_get_str(yyjson_arr_get(tags_json, (size_t)tag_index)));
                if (tags[tag_index] == NULL)
                {
                    set_error(error_buffer, error_buffer_size, "failed to allocate brush tag");
                    return false;
                }
            }

            for (int face_index = 0; face_index < brush->face_count; ++face_index)
            {
                yyjson_val *face_json = yyjson_arr_get(faces_json, (size_t)face_index);
                yyjson_val *plane_json = obj_get(face_json, "plane");
                slayer3d_game_data_brush_face *face = &faces[face_index];
                face->normal = json_vec3(plane_json, "normal", slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
                face->distance = json_float(plane_json, "distance", 0.0f);
                face->material_index =
                    brush_material_index_from_ref(materials, world->material_count, obj_get(face_json, "material"));
                face->material_name = face->material_index >= 0 ? materials[face->material_index].name : NULL;
                yyjson_val *uv_json = obj_get(face_json, "uv");
                (void)json_vec2_value(obj_get(uv_json, "scale"), 1.0f, 1.0f, &face->uv_scale[0], &face->uv_scale[1]);
                (void)json_vec2_value(obj_get(uv_json, "offset"), 0.0f, 0.0f, &face->uv_offset[0], &face->uv_offset[1]);
                face->uv_rotation_degrees = json_float(uv_json, "rotation_degrees", 0.0f);
                face->surface_flags =
                    brush_flags_from_json(obj_get(face_json, "surface_flags"), brush_surface_flag_from_string, 0u);
                if (!load_editor_metadata(obj_get(face_json, "editor"), &face->editor, error_buffer, error_buffer_size))
                    return false;
            }
        }

        if (!rebuild_brush_world_runtime_artifacts(&runtime->brush_worlds[world_index], error_buffer,
                                                   error_buffer_size))
            return false;
    }
    return true;
}

static slayer3d_bounding_box json_bounds(yyjson_val *value, slayer3d_bounding_box fallback)
{
    if (!yyjson_is_obj(value))
        return fallback;
    return (slayer3d_bounding_box){
        json_vec3(value, "min", fallback.min),
        json_vec3(value, "max", fallback.max),
    };
}

static bool load_editor_metadata(yyjson_val *editor, slayer3d_game_data_editor_metadata *out_metadata,
                                 char *error_buffer, int error_buffer_size)
{
    if (out_metadata == NULL)
        return false;
    SDL_zero(*out_metadata);
    if (!yyjson_is_obj(editor))
        return true;

    const char *stable_id = json_string(editor, "stable_id", NULL);
    const char *display_name = json_string(editor, "display_name", NULL);
    const char *description = json_string(editor, "description", NULL);
    const char *category = json_string(editor, "category", NULL);
    const char *group = json_string(editor, "group", NULL);
    const char *prefab = json_string(editor, "prefab", NULL);
    const char *archetype = json_string(editor, "archetype", NULL);
    const char *icon = json_string(editor, "icon", NULL);
    const char *preview_asset = json_string(editor, "preview_asset", NULL);
    yyjson_val *snap = obj_get(editor, "snap");
    yyjson_val *tags = obj_get(editor, "tags");

#define DUP_EDITOR_STRING(field, source)                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((source) != NULL)                                                                                          \
        {                                                                                                              \
            (field) = SDL_strdup(source);                                                                              \
            if ((field) == NULL)                                                                                       \
            {                                                                                                          \
                set_error(error_buffer, error_buffer_size, "failed to allocate editor metadata string");               \
                goto fail;                                                                                             \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

    DUP_EDITOR_STRING(out_metadata->stable_id, stable_id);
    DUP_EDITOR_STRING(out_metadata->display_name, display_name);
    DUP_EDITOR_STRING(out_metadata->description, description);
    DUP_EDITOR_STRING(out_metadata->category, category);
    DUP_EDITOR_STRING(out_metadata->group, group);
    DUP_EDITOR_STRING(out_metadata->prefab, prefab);
    DUP_EDITOR_STRING(out_metadata->archetype, archetype);
    DUP_EDITOR_STRING(out_metadata->icon, icon);
    DUP_EDITOR_STRING(out_metadata->preview_asset, preview_asset);
#undef DUP_EDITOR_STRING

    out_metadata->tag_count = yyjson_is_arr(tags) ? (int)yyjson_arr_size(tags) : 0;
    if (out_metadata->tag_count > 0)
    {
        const char **tag_storage = (const char **)SDL_calloc((size_t)out_metadata->tag_count, sizeof(*tag_storage));
        if (tag_storage == NULL)
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate editor metadata tags");
            goto fail;
        }
        out_metadata->tags = tag_storage;
        for (int i = 0; i < out_metadata->tag_count; ++i)
        {
            tag_storage[i] = SDL_strdup(yyjson_get_str(yyjson_arr_get(tags, (size_t)i)));
            if (tag_storage[i] == NULL)
            {
                set_error(error_buffer, error_buffer_size, "failed to allocate editor metadata tag");
                goto fail;
            }
        }
    }

    yyjson_val *grid = obj_get(snap, "grid");
    out_metadata->snap_grid = slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
    if (yyjson_is_num(grid))
    {
        const float value = (float)yyjson_get_num(grid);
        out_metadata->snap_grid = slayer3d_vec3_make(value, value, value);
        out_metadata->has_snap_grid = true;
    }
    else if (yyjson_is_arr(grid) && yyjson_arr_size(grid) >= 3)
    {
        out_metadata->snap_grid = json_vec3_value(grid, out_metadata->snap_grid);
        out_metadata->has_snap_grid = true;
    }
    out_metadata->snap_rotation_degrees = json_float(snap, "rotation_degrees", 0.0f);
    out_metadata->snap_align_to_floor = json_bool(snap, "align_to_floor", false);
    return true;

fail:
    free_editor_metadata(out_metadata);
    return false;
}

void free_editor_metadata(slayer3d_game_data_editor_metadata *metadata)
{
    if (metadata == NULL)
        return;
    SDL_free((void *)metadata->stable_id);
    SDL_free((void *)metadata->display_name);
    SDL_free((void *)metadata->description);
    SDL_free((void *)metadata->category);
    SDL_free((void *)metadata->group);
    SDL_free((void *)metadata->prefab);
    SDL_free((void *)metadata->archetype);
    SDL_free((void *)metadata->icon);
    SDL_free((void *)metadata->preview_asset);
    for (int i = 0; i < metadata->tag_count; ++i)
        SDL_free((void *)metadata->tags[i]);
    SDL_free((void *)metadata->tags);
    SDL_zero(*metadata);
}

bool load_sector_doors(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size)
{
    yyjson_val *doors = obj_get(root, "sector_doors");
    if (doors == NULL)
        return true;
    if (!yyjson_is_arr(doors))
    {
        set_error(error_buffer, error_buffer_size, "sector_doors must be an array");
        return false;
    }

    runtime->sector_door_count = (int)yyjson_arr_size(doors);
    runtime->sector_doors =
        (sector_door_runtime *)SDL_calloc((size_t)runtime->sector_door_count, sizeof(*runtime->sector_doors));
    if (runtime->sector_doors == NULL && runtime->sector_door_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate sector doors");
        return false;
    }

    for (int i = 0; i < runtime->sector_door_count; ++i)
    {
        yyjson_val *door_json = yyjson_arr_get(doors, (size_t)i);
        yyjson_val *panels = obj_get(door_json, "panels");
        slayer3d_door_desc desc;
        SDL_zero(desc);
        desc.door_id = json_int(door_json, "id", i + 1);
        desc.name = json_string(door_json, "name", "");
        desc.panel_count = yyjson_is_arr(panels) ? (int)yyjson_arr_size(panels) : 0;
        desc.open_seconds = json_float(door_json, "open_seconds", 0.5f);
        desc.close_seconds = json_float(door_json, "close_seconds", 0.5f);
        desc.stay_open_seconds = json_float(door_json, "stay_open_seconds", 0.0f);
        desc.start_open = json_bool(door_json, "start_open", false);

        if (desc.panel_count < 1 || desc.panel_count > SLAYER3D_DOOR_MAX_PANELS)
        {
            set_errorf(error_buffer, error_buffer_size, "sector door '%s' must declare 1 or 2 panels", desc.name);
            return false;
        }
        for (int panel_index = 0; panel_index < desc.panel_count; ++panel_index)
        {
            yyjson_val *panel_json = yyjson_arr_get(panels, (size_t)panel_index);
            const slayer3d_bounding_box fallback = {
                slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
            };
            desc.panels[panel_index].closed_bounds = json_bounds(
                obj_get(panel_json, "bounds") != NULL ? obj_get(panel_json, "bounds") : panel_json, fallback);
            desc.panels[panel_index].open_offset =
                json_vec3(panel_json, "open_offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        }

        runtime->sector_doors[i].json = door_json;
        slayer3d_door_init(&runtime->sector_doors[i].door, &desc);
    }
    return true;
}

bool load_sector_platforms(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                           int error_buffer_size)
{
    yyjson_val *platforms = obj_get(root, "sector_platforms");
    if (platforms == NULL)
        return true;
    if (!yyjson_is_arr(platforms))
    {
        set_error(error_buffer, error_buffer_size, "sector_platforms must be an array");
        return false;
    }

    runtime->sector_platform_count = (int)yyjson_arr_size(platforms);
    runtime->sector_platforms = (sector_platform_runtime *)SDL_calloc((size_t)runtime->sector_platform_count,
                                                                      sizeof(*runtime->sector_platforms));
    if (runtime->sector_platforms == NULL && runtime->sector_platform_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate sector platforms");
        return false;
    }

    for (int i = 0; i < runtime->sector_platform_count; ++i)
    {
        yyjson_val *platform_json = yyjson_arr_get(platforms, (size_t)i);
        sector_platform_runtime *platform = &runtime->sector_platforms[i];
        platform->json = platform_json;
        platform->name = json_string(platform_json, "name", "");
        platform->level = find_sector_level_runtime_mutable(runtime, json_string(platform_json, "sector_level", NULL));
        if (platform->level == NULL)
        {
            set_errorf(error_buffer, error_buffer_size, "sector platform '%s' references an unknown sector level",
                       platform->name);
            return false;
        }

        const char *sector_name = json_string(platform_json, "sector", NULL);
        yyjson_val *sector_index_value = obj_get(platform_json, "sector_index");
        platform->sector_index =
            sector_name != NULL ? sector_level_find_sector_name(platform->level, sector_name)
                                : (yyjson_is_int(sector_index_value) ? (int)yyjson_get_int(sector_index_value) : -1);
        if (platform->sector_index < 0 || platform->sector_index >= platform->level->sector_count)
        {
            set_errorf(error_buffer, error_buffer_size, "sector platform '%s' references an unknown sector",
                       platform->name);
            return false;
        }

        const slayer3d_sector *sector = &platform->level->sectors[platform->sector_index];
        platform->min_floor_y = json_float(platform_json, "min_floor_y", sector->floor_y);
        platform->max_floor_y = json_float(platform_json, "max_floor_y", sector->floor_y);
        if (platform->min_floor_y > platform->max_floor_y)
        {
            const float temp = platform->min_floor_y;
            platform->min_floor_y = platform->max_floor_y;
            platform->max_floor_y = temp;
        }
        platform->ceil_y = json_float(platform_json, "ceil_y", sector->ceil_y);
        platform->cycle_seconds = json_float(platform_json, "cycle_seconds", 1.0f);
        platform->rebuild_min_delta = SDL_max(json_float(platform_json, "rebuild_min_delta", 0.02f), 0.0f);
        platform->last_floor_y = sector->floor_y;
        platform->has_last_floor_y = true;
        platform->enabled = json_bool(platform_json, "enabled", true);
    }
    return true;
}

/**
 * @file game_data_validation_sectors.c
 * @brief Sector world validation for JSON-authored game data.
 */

#include "game_data_validation_internal.h"

#include <float.h>

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/door.h"
#include "slayer3d/level.h"

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

bool collect_sector_levels(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *levels = obj_get(root, "sector_levels");
    if (levels == NULL)
        return true;
    if (!yyjson_is_arr(levels))
        return validation_error(ctx, "$.sector_levels", "sector_levels must be an array");

    for (size_t i = 0; i < yyjson_arr_size(levels); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_levels[%zu]", i);
        yyjson_val *level = yyjson_arr_get(levels, i);
        if (!yyjson_is_obj(level))
            return validation_error(ctx, path, "sector level entries must be objects");
        if (!require_unique_name(ctx, &names->sector_levels, "sector level", json_string(level, "name"), path))
            return false;
    }
    return true;
}

bool collect_sector_navigation(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *graphs = obj_get(root, "sector_navigation");
    for (size_t i = 0; yyjson_is_arr(graphs) && i < yyjson_arr_size(graphs); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_navigation[%zu]", i);
        yyjson_val *graph = yyjson_arr_get(graphs, i);
        if (!require_unique_name(ctx, &names->sector_navigation, "sector navigation graph", json_string(graph, "name"),
                                 path))
        {
            return false;
        }
    }
    return true;
}

bool collect_sector_doors(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *doors = obj_get(root, "sector_doors");
    if (doors == NULL)
        return true;
    if (!yyjson_is_arr(doors))
        return validation_error(ctx, "$.sector_doors", "sector_doors must be an array");

    for (size_t i = 0; i < yyjson_arr_size(doors); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_doors[%zu]", i);
        yyjson_val *door = yyjson_arr_get(doors, i);
        if (!yyjson_is_obj(door))
            return validation_error(ctx, path, "sector door entries must be objects");
        if (!require_unique_name(ctx, &names->sector_doors, "sector door", json_string(door, "name"), path))
            return false;
    }
    return true;
}

bool collect_sector_platforms(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *platforms = obj_get(root, "sector_platforms");
    if (platforms == NULL)
        return true;
    if (!yyjson_is_arr(platforms))
        return validation_error(ctx, "$.sector_platforms", "sector_platforms must be an array");

    for (size_t i = 0; i < yyjson_arr_size(platforms); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_platforms[%zu]", i);
        yyjson_val *platform = yyjson_arr_get(platforms, i);
        if (!yyjson_is_obj(platform))
            return validation_error(ctx, path, "sector platform entries must be objects");
        if (!require_unique_name(ctx, &names->sector_platforms, "sector platform", json_string(platform, "name"), path))
            return false;
    }
    return true;
}

static bool sector_level_material_ref_valid(yyjson_val *materials, const name_table *material_names, yyjson_val *ref,
                                            bool allow_none)
{
    if (allow_none && (ref == NULL || yyjson_is_null(ref)))
        return true;
    if (yyjson_is_int(ref))
    {
        const int index = (int)yyjson_get_int(ref);
        return index >= (allow_none ? -1 : 0) && index < (int)yyjson_arr_size(materials);
    }
    if (yyjson_is_str(ref))
    {
        const char *name = yyjson_get_str(ref);
        if (allow_none && SDL_strcmp(name != NULL ? name : "", "none") == 0)
            return true;
        return name_table_contains(material_names, name);
    }
    return false;
}

bool validate_sector_levels(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *levels = obj_get(root, "sector_levels");
    if (levels == NULL)
        return true;
    if (!yyjson_is_arr(levels))
        return validation_error(ctx, "$.sector_levels", "sector_levels must be an array");

    for (size_t level_index = 0; level_index < yyjson_arr_size(levels); ++level_index)
    {
        char level_path[PATH_BUFFER_SIZE];
        format_path(level_path, sizeof(level_path), "$.sector_levels[%zu]", level_index);
        yyjson_val *level = yyjson_arr_get(levels, level_index);
        yyjson_val *materials = obj_get(level, "materials");
        yyjson_val *sectors = obj_get(level, "sectors");
        yyjson_val *lights = obj_get(level, "lights");
        name_table material_names;
        name_table sector_names;
        SDL_zero(material_names);
        SDL_zero(sector_names);
        bool ok = true;

        if (!yyjson_is_obj(level))
        {
            ok = validation_error(ctx, level_path, "sector level entries must be objects");
            goto done;
        }
        if (!yyjson_is_arr(materials) || yyjson_arr_size(materials) <= 0)
        {
            ok = validation_error(ctx, level_path, "sector level materials must be a non-empty array");
            goto done;
        }
        if (!yyjson_is_arr(sectors) || yyjson_arr_size(sectors) <= 0)
        {
            ok = validation_error(ctx, level_path, "sector level sectors must be a non-empty array");
            goto done;
        }

        for (size_t material_index = 0; ok && material_index < yyjson_arr_size(materials); ++material_index)
        {
            char material_path[PATH_BUFFER_SIZE];
            format_path(material_path, sizeof(material_path), "%s.materials[%zu]", level_path, material_index);
            yyjson_val *material = yyjson_arr_get(materials, material_index);
            if (!yyjson_is_obj(material))
            {
                ok = validation_error(ctx, material_path, "sector material entries must be objects");
                break;
            }
            if (!require_unique_name(ctx, &material_names, "sector material", json_string(material, "name"),
                                     material_path))
            {
                ok = false;
                break;
            }
            yyjson_val *albedo = obj_get(material, "albedo");
            if (albedo != NULL && !is_vec_array(albedo, 3))
            {
                ok = validation_error(ctx, material_path, "sector material albedo must be a vec3 or vec4");
                break;
            }
            yyjson_val *metallic = obj_get(material, "metallic");
            yyjson_val *roughness = obj_get(material, "roughness");
            yyjson_val *tex_scale = obj_get(material, "tex_scale");
            if ((metallic != NULL && !yyjson_is_num(metallic)) || (roughness != NULL && !yyjson_is_num(roughness)) ||
                (tex_scale != NULL && !yyjson_is_num(tex_scale)))
            {
                ok = validation_error(ctx, material_path, "sector material numeric fields must be numbers");
                break;
            }
            if (tex_scale != NULL && yyjson_get_num(tex_scale) <= 0.0)
            {
                ok = validation_error(ctx, material_path, "sector material tex_scale must be positive");
                break;
            }
            const char *texture = json_string(material, "texture");
            if (texture != NULL && texture[0] == '\0')
            {
                ok = validation_error(ctx, material_path, "sector material texture must be non-empty when present");
                break;
            }
            if (texture != NULL && !asset_path_exists(ctx, texture, material_path, "sector material texture"))
            {
                ok = false;
                break;
            }
        }

        for (size_t sector_index = 0; ok && sector_index < yyjson_arr_size(sectors); ++sector_index)
        {
            char sector_path[PATH_BUFFER_SIZE];
            format_path(sector_path, sizeof(sector_path), "%s.sectors[%zu]", level_path, sector_index);
            yyjson_val *sector = yyjson_arr_get(sectors, sector_index);
            yyjson_val *points = obj_get(sector, "points");
            if (!yyjson_is_obj(sector))
            {
                ok = validation_error(ctx, sector_path, "sector entries must be objects");
                break;
            }
            const char *sector_name = json_string(sector, "name");
            if (sector_name != NULL && sector_name[0] != '\0' &&
                !require_unique_name(ctx, &sector_names, "sector", sector_name, sector_path))
            {
                ok = false;
                break;
            }
            if (!yyjson_is_arr(points) || yyjson_arr_size(points) < 3 ||
                yyjson_arr_size(points) > SLAYER3D_SECTOR_MAX_POINTS)
            {
                ok = validation_error(ctx, sector_path, "sector points must contain 3..%d vec2 entries",
                                      SLAYER3D_SECTOR_MAX_POINTS);
                break;
            }
            for (size_t point_index = 0; point_index < yyjson_arr_size(points); ++point_index)
            {
                if (!is_exact_vec_array(yyjson_arr_get(points, point_index), 2))
                {
                    ok = validation_error(ctx, sector_path, "sector points must be vec2 arrays");
                    break;
                }
            }
            if (!ok)
                break;
            yyjson_val *floor_y = obj_get(sector, "floor_y");
            yyjson_val *ceil_y = obj_get(sector, "ceil_y");
            if (!yyjson_is_num(floor_y) || !yyjson_is_num(ceil_y))
            {
                ok = validation_error(ctx, sector_path, "sector floor_y and ceil_y must be numbers");
                break;
            }
            if (yyjson_get_num(ceil_y) <= yyjson_get_num(floor_y))
            {
                ok = validation_error(ctx, sector_path, "sector ceil_y must be greater than floor_y");
                break;
            }
            if (!sector_level_material_ref_valid(materials, &material_names, obj_get(sector, "floor_material"), true) ||
                !sector_level_material_ref_valid(materials, &material_names, obj_get(sector, "ceil_material"), true) ||
                !sector_level_material_ref_valid(materials, &material_names, obj_get(sector, "wall_material"), false))
            {
                ok = validation_error(ctx, sector_path, "sector material refs must reference a declared material");
                break;
            }
            yyjson_val *floor_normal = obj_get(sector, "floor_normal");
            yyjson_val *ceil_normal = obj_get(sector, "ceil_normal");
            yyjson_val *push_velocity = obj_get(sector, "push_velocity");
            if ((floor_normal != NULL && !is_exact_vec_array(floor_normal, 3)) ||
                (ceil_normal != NULL && !is_exact_vec_array(ceil_normal, 3)) ||
                (push_velocity != NULL && !is_exact_vec_array(push_velocity, 3)))
            {
                ok = validation_error(ctx, sector_path, "sector normals and push_velocity must be vec3 arrays");
                break;
            }
            yyjson_val *ambient = obj_get(sector, "ambient_sound_id");
            if (ambient != NULL && (!yyjson_is_int(ambient) || yyjson_get_int(ambient) < 0))
            {
                ok = validation_error(ctx, sector_path, "sector ambient_sound_id must be a non-negative integer");
                break;
            }
            yyjson_val *damage = obj_get(sector, "damage_per_second");
            if (damage != NULL && (!yyjson_is_num(damage) || yyjson_get_num(damage) < 0.0))
            {
                ok = validation_error(ctx, sector_path, "sector damage_per_second must be non-negative");
                break;
            }
            yyjson_val *lighting = obj_get(sector, "lighting");
            if (lighting != NULL)
            {
                if (!yyjson_is_obj(lighting))
                {
                    ok = validation_error(ctx, sector_path, "sector lighting must be an object");
                    break;
                }
                yyjson_val *lighting_level = obj_get(lighting, "level");
                if (lighting_level != NULL && (!yyjson_is_int(lighting_level) || yyjson_get_int(lighting_level) < 0 ||
                                               yyjson_get_int(lighting_level) > 255))
                {
                    ok = validation_error(ctx, sector_path, "sector lighting level must be an integer in [0, 255]");
                    break;
                }
                yyjson_val *color = obj_get(lighting, "color");
                if (color != NULL &&
                    (!is_exact_vec3_or_vec4_array(color) || !numeric_array_values_in_range(color, 0.0, 1.0)))
                {
                    ok = validation_error(ctx, sector_path,
                                          "sector lighting color must be a vec3 or vec4 with values in [0, 1]");
                    break;
                }
            }
        }

        if (ok && lights != NULL)
        {
            if (!yyjson_is_arr(lights))
            {
                ok = validation_error(ctx, level_path, "sector level lights must be an array");
                goto done;
            }
            for (size_t light_index = 0; ok && light_index < yyjson_arr_size(lights); ++light_index)
            {
                char light_path[PATH_BUFFER_SIZE];
                format_path(light_path, sizeof(light_path), "%s.lights[%zu]", level_path, light_index);
                yyjson_val *light = yyjson_arr_get(lights, light_index);
                if (!yyjson_is_obj(light) || !is_exact_vec_array(obj_get(light, "position"), 3))
                {
                    ok = validation_error(ctx, light_path, "sector light requires position vec3");
                    break;
                }
                yyjson_val *color = obj_get(light, "color");
                if (color != NULL && !is_exact_vec_array(color, 3))
                {
                    ok = validation_error(ctx, light_path, "sector light color must be a vec3");
                    break;
                }
                yyjson_val *intensity = obj_get(light, "intensity");
                yyjson_val *range = obj_get(light, "range");
                if ((intensity != NULL && (!yyjson_is_num(intensity) || yyjson_get_num(intensity) < 0.0)) ||
                    (range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) <= 0.0)))
                {
                    ok = validation_error(ctx, light_path,
                                          "sector light intensity must be non-negative and range positive");
                    break;
                }
            }
        }

    done:
        name_table_destroy(&material_names);
        name_table_destroy(&sector_names);
        if (!ok)
            return false;
    }
    return true;
}

bool validate_sector_doors(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *doors = obj_get(root, "sector_doors");
    if (doors == NULL)
        return true;
    if (!yyjson_is_arr(doors))
        return validation_error(ctx, "$.sector_doors", "sector_doors must be an array");

    for (size_t door_index = 0; door_index < yyjson_arr_size(doors); ++door_index)
    {
        char door_path[PATH_BUFFER_SIZE];
        format_path(door_path, sizeof(door_path), "$.sector_doors[%zu]", door_index);
        yyjson_val *door = yyjson_arr_get(doors, door_index);
        if (!yyjson_is_obj(door))
            return validation_error(ctx, door_path, "sector door entries must be objects");
        const char *scene = json_string(door, "scene");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, door_path))
            return false;

        yyjson_val *id = obj_get(door, "id");
        if (id != NULL && !yyjson_is_int(id))
            return validation_error(ctx, door_path, "sector door id must be an integer");
        const char *numeric_fields[] = {"open_seconds", "close_seconds", "stay_open_seconds"};
        for (size_t n = 0; n < SDL_arraysize(numeric_fields); ++n)
        {
            yyjson_val *value = obj_get(door, numeric_fields[n]);
            if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0))
                return validation_error(ctx, door_path, "sector door timing values must be non-negative");
        }
        yyjson_val *start_open = obj_get(door, "start_open");
        if (start_open != NULL && !yyjson_is_bool(start_open))
            return validation_error(ctx, door_path, "sector door start_open must be a boolean");

        yyjson_val *panels = obj_get(door, "panels");
        if (!yyjson_is_arr(panels) || yyjson_arr_size(panels) < 1 || yyjson_arr_size(panels) > SLAYER3D_DOOR_MAX_PANELS)
        {
            return validation_error(ctx, door_path, "sector door panels must be an array with 1 or 2 entries");
        }
        for (size_t panel_index = 0; panel_index < yyjson_arr_size(panels); ++panel_index)
        {
            char panel_path[PATH_BUFFER_SIZE];
            format_path(panel_path, sizeof(panel_path), "%s.panels[%zu]", door_path, panel_index);
            yyjson_val *panel = yyjson_arr_get(panels, panel_index);
            yyjson_val *bounds = obj_get(panel, "bounds");
            yyjson_val *bounds_source = bounds != NULL ? bounds : panel;
            if (!yyjson_is_obj(panel))
                return validation_error(ctx, panel_path, "sector door panel entries must be objects");
            if (!is_exact_vec_array(obj_get(bounds_source, "min"), 3) ||
                !is_exact_vec_array(obj_get(bounds_source, "max"), 3))
            {
                return validation_error(ctx, panel_path, "sector door panel requires bounds min and max vec3 values");
            }
            if (!is_exact_vec_array(obj_get(panel, "open_offset"), 3))
                return validation_error(ctx, panel_path, "sector door panel requires open_offset vec3");
        }

        yyjson_val *render = obj_get(door, "render");
        if (render != NULL)
        {
            if (!yyjson_is_obj(render))
                return validation_error(ctx, door_path, "sector door render must be an object");
            yyjson_val *color = obj_get(render, "color");
            if (color != NULL && !is_vec_array(color, 3))
                return validation_error(ctx, door_path, "sector door render color must be a vec3 or vec4");
            yyjson_val *lighting = obj_get(render, "lighting");
            yyjson_val *emissive = obj_get(render, "emissive");
            if ((lighting != NULL && !yyjson_is_bool(lighting)) || (emissive != NULL && !yyjson_is_bool(emissive)))
                return validation_error(ctx, door_path, "sector door render lighting flags must be booleans");
            const char *texture = json_string(render, "texture");
            if (texture != NULL && !require_ref(ctx, &names->images, "image asset", texture, door_path))
                return false;
        }
    }
    return true;
}

static yyjson_val *find_sector_level_json(yyjson_val *root, const char *level_name)
{
    yyjson_val *levels = obj_get(root, "sector_levels");
    for (size_t i = 0; level_name != NULL && yyjson_is_arr(levels) && i < yyjson_arr_size(levels); ++i)
    {
        yyjson_val *level = yyjson_arr_get(levels, i);
        const char *name = json_string(level, "name");
        if (name != NULL && SDL_strcmp(name, level_name) == 0)
            return level;
    }
    return NULL;
}

bool sector_level_has_sector_name(yyjson_val *root, const char *level_name, const char *sector_name)
{
    yyjson_val *level = find_sector_level_json(root, level_name);
    yyjson_val *sectors = obj_get(level, "sectors");
    for (size_t i = 0; sector_name != NULL && yyjson_is_arr(sectors) && i < yyjson_arr_size(sectors); ++i)
    {
        const char *name = json_string(yyjson_arr_get(sectors, i), "name");
        if (name != NULL && SDL_strcmp(name, sector_name) == 0)
            return true;
    }
    return false;
}

bool sector_level_has_sector_index(yyjson_val *root, const char *level_name, int sector_index)
{
    yyjson_val *level = find_sector_level_json(root, level_name);
    yyjson_val *sectors = obj_get(level, "sectors");
    return yyjson_is_arr(sectors) && sector_index >= 0 && (size_t)sector_index < yyjson_arr_size(sectors);
}

bool validate_sector_navigation(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *graphs = obj_get(root, "sector_navigation");
    if (graphs == NULL)
        return true;
    if (!yyjson_is_arr(graphs))
        return validation_error(ctx, "$.sector_navigation", "sector_navigation must be an array");

    for (size_t graph_index = 0; graph_index < yyjson_arr_size(graphs); ++graph_index)
    {
        char graph_path[PATH_BUFFER_SIZE];
        format_path(graph_path, sizeof(graph_path), "$.sector_navigation[%zu]", graph_index);
        yyjson_val *graph = yyjson_arr_get(graphs, graph_index);
        yyjson_val *nodes = obj_get(graph, "nodes");
        yyjson_val *links = obj_get(graph, "links");
        name_table node_names;
        SDL_zero(node_names);
        bool ok = true;

        if (!yyjson_is_obj(graph))
        {
            ok = validation_error(ctx, graph_path, "sector navigation graph entries must be objects");
            goto done;
        }
        const char *level = json_string(graph, "sector_level");
        if (!require_ref(ctx, &names->sector_levels, "sector level", level, graph_path))
        {
            ok = false;
            goto done;
        }
        if (!yyjson_is_arr(nodes) || yyjson_arr_size(nodes) <= 0 || yyjson_arr_size(nodes) > 1024)
        {
            ok = validation_error(ctx, graph_path, "sector navigation nodes must be a non-empty array of <=1024 nodes");
            goto done;
        }
        if (links != NULL && !yyjson_is_arr(links))
        {
            ok = validation_error(ctx, graph_path, "sector navigation links must be an array");
            goto done;
        }

        for (size_t node_index = 0; ok && node_index < yyjson_arr_size(nodes); ++node_index)
        {
            char node_path[PATH_BUFFER_SIZE];
            format_path(node_path, sizeof(node_path), "%s.nodes[%zu]", graph_path, node_index);
            yyjson_val *node = yyjson_arr_get(nodes, node_index);
            if (!yyjson_is_obj(node))
            {
                ok = validation_error(ctx, node_path, "sector navigation nodes must be objects");
                break;
            }
            if (!require_unique_name(ctx, &node_names, "sector navigation node", json_string(node, "name"), node_path))
            {
                ok = false;
                break;
            }
            if (!is_exact_vec_array(obj_get(node, "position"), 3))
            {
                ok = validation_error(ctx, node_path, "sector navigation node position must be a vec3");
                break;
            }
            const char *sector = json_string(node, "sector");
            yyjson_val *sector_index = obj_get(node, "sector_index");
            if ((sector == NULL && sector_index == NULL) || (sector != NULL && sector_index != NULL))
            {
                ok = validation_error(ctx, node_path,
                                      "sector navigation node requires exactly one of sector or sector_index");
                break;
            }
            if (sector != NULL && !sector_level_has_sector_name(root, level, sector))
            {
                ok = validation_error(ctx, node_path, "unknown sector navigation node sector '%s'", sector);
                break;
            }
            if (sector_index != NULL &&
                (!yyjson_is_int(sector_index) ||
                 !sector_level_has_sector_index(root, level, (int)yyjson_get_int(sector_index))))
            {
                ok = validation_error(ctx, node_path, "sector navigation node sector_index is out of range");
                break;
            }
        }

        for (size_t link_index = 0; ok && yyjson_is_arr(links) && link_index < yyjson_arr_size(links); ++link_index)
        {
            char link_path[PATH_BUFFER_SIZE];
            format_path(link_path, sizeof(link_path), "%s.links[%zu]", graph_path, link_index);
            yyjson_val *link = yyjson_arr_get(links, link_index);
            if (!yyjson_is_obj(link))
            {
                ok = validation_error(ctx, link_path, "sector navigation links must be objects");
                break;
            }
            if (!require_ref(ctx, &node_names, "sector navigation node", json_string(link, "from"), link_path) ||
                !require_ref(ctx, &node_names, "sector navigation node", json_string(link, "to"), link_path))
            {
                ok = false;
                break;
            }
            yyjson_val *bidirectional = obj_get(link, "bidirectional");
            yyjson_val *cost = obj_get(link, "cost");
            if (bidirectional != NULL && !yyjson_is_bool(bidirectional))
            {
                ok = validation_error(ctx, link_path, "sector navigation link bidirectional must be a boolean");
                break;
            }
            if (cost != NULL && (!yyjson_is_num(cost) || yyjson_get_num(cost) <= 0.0))
            {
                ok = validation_error(ctx, link_path, "sector navigation link cost must be positive");
                break;
            }
        }

    done:
        name_table_destroy(&node_names);
        if (!ok)
            return false;
    }
    return true;
}

bool validate_sector_platforms(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *platforms = obj_get(root, "sector_platforms");
    if (platforms == NULL)
        return true;
    if (!yyjson_is_arr(platforms))
        return validation_error(ctx, "$.sector_platforms", "sector_platforms must be an array");

    for (size_t i = 0; i < yyjson_arr_size(platforms); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_platforms[%zu]", i);
        yyjson_val *platform = yyjson_arr_get(platforms, i);
        if (!yyjson_is_obj(platform))
            return validation_error(ctx, path, "sector platform entries must be objects");

        const char *scene = json_string(platform, "scene");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, path))
            return false;
        const char *sector_level = json_string(platform, "sector_level");
        if (!require_ref(ctx, &names->sector_levels, "sector level", sector_level, path))
            return false;

        const char *sector = json_string(platform, "sector");
        yyjson_val *sector_index = obj_get(platform, "sector_index");
        if ((sector == NULL && sector_index == NULL) || (sector != NULL && sector_index != NULL))
            return validation_error(ctx, path, "sector platform requires exactly one of sector or sector_index");
        if (sector != NULL && !sector_level_has_sector_name(root, sector_level, sector))
            return validation_error(ctx, path, "unknown sector platform sector '%s'", sector);
        if (sector_index != NULL &&
            (!yyjson_is_int(sector_index) ||
             !sector_level_has_sector_index(root, sector_level, (int)yyjson_get_int(sector_index))))
        {
            return validation_error(ctx, path, "sector platform sector_index must reference a sector in sector_level");
        }

        yyjson_val *min_floor_y = obj_get(platform, "min_floor_y");
        yyjson_val *max_floor_y = obj_get(platform, "max_floor_y");
        yyjson_val *ceil_y = obj_get(platform, "ceil_y");
        yyjson_val *cycle_seconds = obj_get(platform, "cycle_seconds");
        yyjson_val *rebuild_min_delta = obj_get(platform, "rebuild_min_delta");
        yyjson_val *crush_damage = obj_get(platform, "crush_damage_per_second");
        yyjson_val *crush_clearance = obj_get(platform, "crush_clearance");
        if (!yyjson_is_num(min_floor_y) || !yyjson_is_num(max_floor_y) || !yyjson_is_num(ceil_y))
            return validation_error(ctx, path, "sector platform requires numeric min_floor_y, max_floor_y, and ceil_y");
        const double min_y = yyjson_get_num(min_floor_y);
        const double max_y = yyjson_get_num(max_floor_y);
        const double ceiling = yyjson_get_num(ceil_y);
        if (max_y < min_y)
            return validation_error(ctx, path,
                                    "sector platform max_floor_y must be greater than or equal to min_floor_y");
        if (ceiling <= max_y)
            return validation_error(ctx, path, "sector platform ceil_y must be greater than max_floor_y");
        if (!yyjson_is_num(cycle_seconds) || yyjson_get_num(cycle_seconds) <= 0.0)
            return validation_error(ctx, path, "sector platform cycle_seconds must be positive");
        if (rebuild_min_delta != NULL && (!yyjson_is_num(rebuild_min_delta) || yyjson_get_num(rebuild_min_delta) < 0.0))
            return validation_error(ctx, path, "sector platform rebuild_min_delta must be non-negative");
        if (crush_damage != NULL && (!yyjson_is_num(crush_damage) || yyjson_get_num(crush_damage) < 0.0))
            return validation_error(ctx, path, "sector platform crush_damage_per_second must be non-negative");
        if (crush_clearance != NULL && (!yyjson_is_num(crush_clearance) || yyjson_get_num(crush_clearance) < 0.0))
            return validation_error(ctx, path, "sector platform crush_clearance must be non-negative");
        yyjson_val *enabled = obj_get(platform, "enabled");
        if (enabled != NULL && !yyjson_is_bool(enabled))
            return validation_error(ctx, path, "sector platform enabled must be a boolean");
        yyjson_val *crush_when_descending = obj_get(platform, "crush_when_descending");
        if (crush_when_descending != NULL && !yyjson_is_bool(crush_when_descending))
            return validation_error(ctx, path, "sector platform crush_when_descending must be a boolean");
        yyjson_val *deactivate_on_death = obj_get(platform, "deactivate_on_death");
        if (deactivate_on_death != NULL && !yyjson_is_bool(deactivate_on_death))
            return validation_error(ctx, path, "sector platform deactivate_on_death must be a boolean");
        yyjson_val *armor_absorb = obj_get(platform, "armor_absorb");
        if (armor_absorb != NULL &&
            (!yyjson_is_num(armor_absorb) || yyjson_get_num(armor_absorb) < 0.0 || yyjson_get_num(armor_absorb) > 1.0))
        {
            return validation_error(ctx, path, "sector platform armor_absorb must be in 0..1");
        }
        if (!validate_non_empty_string_field(ctx, platform, path, "sector platform", "crush_actor_tag") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "actor_tag") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "damage_type") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "health_property") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "max_health_property") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "armor_property") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "armor_absorb_property") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "alive_property"))
        {
            return false;
        }
        if (!validate_target_filter_fields(ctx, platform, path, "sector platform"))
            return false;
        yyjson_val *actions = obj_get(platform, "crush_actions");
        if (actions != NULL && !validate_action_array(ctx, actions, path, names))
            return false;
        if (!validate_optional_signal_field(ctx, platform, path, names, "on_crush") ||
            !validate_optional_signal_field(ctx, platform, path, names, "on_damage") ||
            !validate_optional_signal_field(ctx, platform, path, names, "on_death"))
            return false;
    }
    return true;
}

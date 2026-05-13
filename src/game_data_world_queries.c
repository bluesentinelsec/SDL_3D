/**
 * @file game_data_world_queries.c
 * @brief Scene, sector, brush, and generic world-model query helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/collision.h"
#include "slayer3d/game.h"
#include "slayer3d/math.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_timer.h>
#include <stdlib.h>

const char *slayer3d_game_data_active_scene(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return scene != NULL ? scene->name : NULL;
}

int slayer3d_game_data_scene_count(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_count : 0;
}

const char *slayer3d_game_data_scene_name_at(const slayer3d_game_data_runtime *runtime, int index)
{
    if (runtime == NULL || index < 0 || index >= runtime->scene_count)
        return NULL;
    return runtime->scenes[index].name;
}

slayer3d_properties *slayer3d_game_data_mutable_scene_state(slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_state : NULL;
}

const slayer3d_properties *slayer3d_game_data_scene_state(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_state : NULL;
}

sector_level_runtime *find_sector_level_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->sector_level_count; ++i)
    {
        sector_level_runtime *level = &runtime->sector_levels[i];
        if (level->name != NULL && SDL_strcmp(level->name, name) == 0)
            return level;
    }
    return NULL;
}

const sector_level_runtime *find_sector_level_runtime(const slayer3d_game_data_runtime *runtime, const char *name)
{
    return find_sector_level_runtime_mutable((slayer3d_game_data_runtime *)runtime, name);
}

int sector_level_find_sector_name(const sector_level_runtime *level, const char *sector_name)
{
    if (level == NULL || sector_name == NULL)
        return -1;
    for (int i = 0; i < level->sector_count; ++i)
    {
        if (level->sector_names[i] != NULL && SDL_strcmp(level->sector_names[i], sector_name) == 0)
            return i;
    }
    return -1;
}

static int sector_level_resolve_sector_index(const sector_level_runtime *level, const char *sector)
{
    if (level == NULL || sector == NULL || sector[0] == '\0')
        return -1;
    const int named_index = sector_level_find_sector_name(level, sector);
    if (named_index >= 0)
        return named_index;

    char *end = NULL;
    const long parsed = SDL_strtol(sector, &end, 10);
    if (end != NULL && *end == '\0' && parsed >= 0 && parsed < level->sector_count)
        return (int)parsed;
    return -1;
}

static float clamp01(float value)
{
    if (value <= 0.0f)
        return 0.0f;
    if (value >= 1.0f)
        return 1.0f;
    return value;
}

static void sector_lighting_rgb(const slayer3d_sector *sector, float out_rgb[3])
{
    if (out_rgb == NULL)
        return;
    out_rgb[0] = 1.0f;
    out_rgb[1] = 1.0f;
    out_rgb[2] = 1.0f;
    if (sector == NULL || !sector->has_lighting)
        return;

    const float level = SDL_clamp(sector->lighting_level, 0.0f, 255.0f) / 255.0f;
    const float influence = clamp01(sector->lighting_color[3]);
    for (int i = 0; i < 3; ++i)
    {
        const float tint = 1.0f + (clamp01(sector->lighting_color[i]) - 1.0f) * influence;
        out_rgb[i] = level * tint;
    }
}

static Uint8 color_channel_from_float(float value)
{
    value = SDL_clamp(value, 0.0f, 255.0f);
    return (Uint8)(value + 0.5f);
}

void modulate_color_by_sector_lighting(slayer3d_color *color, const slayer3d_sector *sector)
{
    if (color == NULL || sector == NULL || !sector->has_lighting)
        return;
    float lighting[3];
    sector_lighting_rgb(sector, lighting);
    color->r = color_channel_from_float((float)color->r * lighting[0]);
    color->g = color_channel_from_float((float)color->g * lighting[1]);
    color->b = color_channel_from_float((float)color->b * lighting[2]);
}

static bool rebuild_sector_level_variants_atomic(sector_level_runtime *level, char *error_buffer, int error_buffer_size)
{
    if (level == NULL)
    {
        set_error(error_buffer, error_buffer_size, "sector level is invalid");
        return false;
    }

    slayer3d_level lightmapped;
    slayer3d_level vertex_baked;
    slayer3d_level unlit;
    slayer3d_level lightmapped_without_sector_lighting;
    slayer3d_level vertex_baked_without_sector_lighting;
    slayer3d_level unlit_without_sector_lighting;
    SDL_zero(lightmapped);
    SDL_zero(vertex_baked);
    SDL_zero(unlit);
    SDL_zero(lightmapped_without_sector_lighting);
    SDL_zero(vertex_baked_without_sector_lighting);
    SDL_zero(unlit_without_sector_lighting);

    bool ok = true;
    if (!build_sector_level_variant_set(level, level->sectors, &lightmapped, &vertex_baked, &unlit, "sector-lit",
                                        error_buffer, error_buffer_size))
    {
        ok = false;
    }
    slayer3d_sector *unlit_sectors = NULL;
    if (ok)
    {
        unlit_sectors = copy_sectors_without_sector_lighting(level);
        if (unlit_sectors == NULL && level->sector_count > 0)
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate sector lighting toggle rebuild variants");
            ok = false;
        }
    }
    if (ok && !build_sector_level_variant_set(level, unlit_sectors != NULL ? unlit_sectors : level->sectors,
                                              &lightmapped_without_sector_lighting,
                                              &vertex_baked_without_sector_lighting, &unlit_without_sector_lighting,
                                              "sector-neutral", error_buffer, error_buffer_size))
    {
        ok = false;
    }
    SDL_free(unlit_sectors);

    if (!ok)
    {
        slayer3d_free_level(&lightmapped);
        slayer3d_free_level(&vertex_baked);
        slayer3d_free_level(&unlit);
        slayer3d_free_level(&lightmapped_without_sector_lighting);
        slayer3d_free_level(&vertex_baked_without_sector_lighting);
        slayer3d_free_level(&unlit_without_sector_lighting);
        return false;
    }

    slayer3d_free_level(&level->lightmapped);
    slayer3d_free_level(&level->vertex_baked);
    slayer3d_free_level(&level->unlit);
    slayer3d_free_level(&level->lightmapped_without_sector_lighting);
    slayer3d_free_level(&level->vertex_baked_without_sector_lighting);
    slayer3d_free_level(&level->unlit_without_sector_lighting);
    level->lightmapped = lightmapped;
    level->vertex_baked = vertex_baked;
    level->unlit = unlit;
    level->lightmapped_without_sector_lighting = lightmapped_without_sector_lighting;
    level->vertex_baked_without_sector_lighting = vertex_baked_without_sector_lighting;
    level->unlit_without_sector_lighting = unlit_without_sector_lighting;
    return true;
}

bool slayer3d_game_data_get_sector_lighting(const slayer3d_game_data_runtime *runtime, const char *sector_level,
                                            const char *sector, float *out_level, float out_color[4],
                                            char *error_buffer, int error_buffer_size)
{
    if (out_level != NULL)
        *out_level = 0.0f;
    if (out_color != NULL)
        SDL_memset(out_color, 0, sizeof(float) * 4U);
    const sector_level_runtime *level = find_sector_level_runtime(runtime, sector_level);
    const int sector_index = sector_level_resolve_sector_index(level, sector);
    if (level == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' not found",
                   sector_level != NULL ? sector_level : "<null>");
        return false;
    }
    if (sector_index < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "sector '%s' not found", sector != NULL ? sector : "<null>");
        return false;
    }

    const slayer3d_sector *resolved = &level->sectors[sector_index];
    if (!resolved->has_lighting)
    {
        set_errorf(error_buffer, error_buffer_size, "sector '%s' has no authored lighting", sector);
        return false;
    }
    if (out_level == NULL || out_color == NULL)
    {
        set_error(error_buffer, error_buffer_size, "sector lighting output pointers are required");
        return false;
    }
    *out_level = resolved->lighting_level;
    SDL_memcpy(out_color, resolved->lighting_color, sizeof(resolved->lighting_color));
    return true;
}

bool slayer3d_game_data_set_sector_lighting(slayer3d_game_data_runtime *runtime, const char *sector_level,
                                            const char *sector, float level, const float color[4], char *error_buffer,
                                            int error_buffer_size)
{
    sector_level_runtime *resolved_level = find_sector_level_runtime_mutable(runtime, sector_level);
    const int sector_index = sector_level_resolve_sector_index(resolved_level, sector);
    if (resolved_level == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' not found",
                   sector_level != NULL ? sector_level : "<null>");
        return false;
    }
    if (sector_index < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "sector '%s' not found", sector != NULL ? sector : "<null>");
        return false;
    }
    if (color == NULL)
    {
        set_error(error_buffer, error_buffer_size, "sector lighting color is required");
        return false;
    }

    slayer3d_sector *target = &resolved_level->sectors[sector_index];
    const bool old_has_lighting = target->has_lighting;
    const float old_level = target->lighting_level;
    float old_color[4];
    SDL_memcpy(old_color, target->lighting_color, sizeof(old_color));

    target->has_lighting = true;
    target->lighting_level = SDL_clamp(level, 0.0f, 255.0f);
    for (int i = 0; i < 4; ++i)
        target->lighting_color[i] = clamp01(color[i]);

    if (!rebuild_sector_level_variants_atomic(resolved_level, error_buffer, error_buffer_size))
    {
        target->has_lighting = old_has_lighting;
        target->lighting_level = old_level;
        SDL_memcpy(target->lighting_color, old_color, sizeof(target->lighting_color));
        (void)rebuild_sector_level_variants_atomic(resolved_level, NULL, 0);
        return false;
    }
    return true;
}

static yyjson_val *find_sector_navigation_graph(const slayer3d_game_data_runtime *runtime, const char *graph_name)
{
    yyjson_val *graphs = obj_get(runtime_root(runtime), "sector_navigation");
    for (size_t i = 0; graph_name != NULL && yyjson_is_arr(graphs) && i < yyjson_arr_size(graphs); ++i)
    {
        yyjson_val *graph = yyjson_arr_get(graphs, i);
        const char *name = json_string(graph, "name", NULL);
        if (name != NULL && SDL_strcmp(name, graph_name) == 0)
            return graph;
    }
    return NULL;
}

static const sector_level_runtime *sector_navigation_level(const slayer3d_game_data_runtime *runtime, yyjson_val *graph)
{
    return find_sector_level_runtime(runtime, json_string(graph, "sector_level", NULL));
}

static int sector_navigation_node_count(yyjson_val *graph)
{
    yyjson_val *nodes = obj_get(graph, "nodes");
    return yyjson_is_arr(nodes) ? (int)yyjson_arr_size(nodes) : 0;
}

static yyjson_val *sector_navigation_node_at(yyjson_val *graph, int index)
{
    yyjson_val *nodes = obj_get(graph, "nodes");
    return index >= 0 && yyjson_is_arr(nodes) && index < (int)yyjson_arr_size(nodes)
               ? yyjson_arr_get(nodes, (size_t)index)
               : NULL;
}

static int sector_navigation_node_sector_index(const sector_level_runtime *level, yyjson_val *node)
{
    if (level == NULL || node == NULL)
        return -1;
    const int authored_index = json_int(node, "sector_index", -1);
    if (authored_index >= 0 && authored_index < level->sector_count)
        return authored_index;
    return sector_level_find_sector_name(level, json_string(node, "sector", NULL));
}

static bool sector_navigation_read_node(const slayer3d_game_data_runtime *runtime, yyjson_val *graph, int index,
                                        slayer3d_game_data_sector_nav_node *out_node)
{
    const sector_level_runtime *level = sector_navigation_level(runtime, graph);
    yyjson_val *node = sector_navigation_node_at(graph, index);
    if (level == NULL || node == NULL || out_node == NULL)
        return false;

    out_node->name = json_string(node, "name", NULL);
    out_node->sector_index = sector_navigation_node_sector_index(level, node);
    out_node->position = json_vec3(node, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return out_node->name != NULL && out_node->sector_index >= 0;
}

static int sector_navigation_find_node_index(yyjson_val *graph, const char *node_name)
{
    const int node_count = sector_navigation_node_count(graph);
    for (int i = 0; node_name != NULL && i < node_count; ++i)
    {
        yyjson_val *node = sector_navigation_node_at(graph, i);
        const char *name = json_string(node, "name", NULL);
        if (name != NULL && SDL_strcmp(name, node_name) == 0)
            return i;
    }
    return -1;
}

static float sector_navigation_distance_squared(slayer3d_vec3 a, slayer3d_vec3 b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

static float sector_navigation_link_default_cost(const slayer3d_game_data_runtime *runtime, yyjson_val *graph, int from,
                                                 int to)
{
    slayer3d_game_data_sector_nav_node from_node;
    slayer3d_game_data_sector_nav_node to_node;
    if (!sector_navigation_read_node(runtime, graph, from, &from_node) ||
        !sector_navigation_read_node(runtime, graph, to, &to_node))
    {
        return 1.0f;
    }
    return SDL_sqrtf(sector_navigation_distance_squared(from_node.position, to_node.position));
}

static int sector_navigation_nearest_index(const slayer3d_game_data_runtime *runtime, yyjson_val *graph,
                                           slayer3d_vec3 position)
{
    const sector_level_runtime *level = sector_navigation_level(runtime, graph);
    const int node_count = sector_navigation_node_count(graph);
    if (level == NULL || node_count <= 0)
        return -1;

    const int containing_sector =
        slayer3d_level_find_sector_at(&level->lightmapped, level->sectors, position.x, position.z, position.y);
    int best_index = -1;
    float best_distance = 1.0e30f;
    for (int pass = 0; pass < 2 && best_index < 0; ++pass)
    {
        const bool same_sector_only = pass == 0 && containing_sector >= 0;
        for (int i = 0; i < node_count; ++i)
        {
            slayer3d_game_data_sector_nav_node node;
            if (!sector_navigation_read_node(runtime, graph, i, &node))
                continue;
            if (same_sector_only && node.sector_index != containing_sector)
                continue;
            const float distance = sector_navigation_distance_squared(position, node.position);
            if (best_index < 0 || distance < best_distance)
            {
                best_index = i;
                best_distance = distance;
            }
        }
    }
    return best_index;
}

bool slayer3d_game_data_sector_nav_nearest_node(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                                slayer3d_vec3 position, slayer3d_game_data_sector_nav_node *out_node)
{
    yyjson_val *graph = find_sector_navigation_graph(runtime, graph_name);
    const int index = sector_navigation_nearest_index(runtime, graph, position);
    return index >= 0 && sector_navigation_read_node(runtime, graph, index, out_node);
}

bool slayer3d_game_data_sector_nav_path(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                        slayer3d_vec3 start, slayer3d_vec3 goal,
                                        slayer3d_game_data_sector_nav_node *out_nodes, int max_nodes,
                                        int *out_node_count, float *out_cost)
{
    if (out_node_count != NULL)
        *out_node_count = 0;
    if (out_cost != NULL)
        *out_cost = 0.0f;

    yyjson_val *graph = find_sector_navigation_graph(runtime, graph_name);
    const int node_count = sector_navigation_node_count(graph);
    const int start_index = sector_navigation_nearest_index(runtime, graph, start);
    const int goal_index = sector_navigation_nearest_index(runtime, graph, goal);
    if (graph == NULL || node_count <= 0 || start_index < 0 || goal_index < 0)
        return false;

    float *distances = (float *)SDL_malloc(sizeof(float) * (size_t)node_count);
    int *previous = (int *)SDL_malloc(sizeof(int) * (size_t)node_count);
    bool *visited = (bool *)SDL_calloc((size_t)node_count, sizeof(bool));
    int *reverse_path = (int *)SDL_malloc(sizeof(int) * (size_t)node_count);
    if (distances == NULL || previous == NULL || visited == NULL || reverse_path == NULL)
    {
        SDL_free(distances);
        SDL_free(previous);
        SDL_free(visited);
        SDL_free(reverse_path);
        return false;
    }

    for (int i = 0; i < node_count; ++i)
    {
        distances[i] = 1.0e30f;
        previous[i] = -1;
    }
    distances[start_index] = 0.0f;

    for (;;)
    {
        int current = -1;
        float current_distance = 1.0e30f;
        for (int i = 0; i < node_count; ++i)
        {
            if (!visited[i] && distances[i] < current_distance)
            {
                current = i;
                current_distance = distances[i];
            }
        }
        if (current < 0 || current == goal_index)
            break;
        visited[current] = true;

        yyjson_val *links = obj_get(graph, "links");
        for (size_t link_index = 0; yyjson_is_arr(links) && link_index < yyjson_arr_size(links); ++link_index)
        {
            yyjson_val *link = yyjson_arr_get(links, link_index);
            const int from = sector_navigation_find_node_index(graph, json_string(link, "from", NULL));
            const int to = sector_navigation_find_node_index(graph, json_string(link, "to", NULL));
            const bool bidirectional = json_bool(link, "bidirectional", true);
            int neighbor = -1;
            if (from == current)
                neighbor = to;
            else if (bidirectional && to == current)
                neighbor = from;
            if (neighbor < 0 || neighbor >= node_count || visited[neighbor])
                continue;

            const float cost = SDL_max(
                json_float(link, "cost", sector_navigation_link_default_cost(runtime, graph, current, neighbor)),
                0.0001f);
            const float candidate = distances[current] + cost;
            if (candidate < distances[neighbor])
            {
                distances[neighbor] = candidate;
                previous[neighbor] = current;
            }
        }
    }

    bool ok = start_index == goal_index || previous[goal_index] >= 0;
    int path_count = 0;
    if (ok)
    {
        for (int node = goal_index; node >= 0 && path_count < node_count; node = previous[node])
            reverse_path[path_count++] = node;
        if (path_count <= 0 || reverse_path[path_count - 1] != start_index)
            ok = false;
    }

    if (ok)
    {
        if (out_node_count != NULL)
            *out_node_count = path_count;
        if (out_cost != NULL)
            *out_cost = distances[goal_index];
        if (out_nodes != NULL)
        {
            if (max_nodes < path_count)
            {
                ok = false;
            }
            else
            {
                for (int i = 0; i < path_count; ++i)
                {
                    const int source_index = reverse_path[path_count - 1 - i];
                    if (!sector_navigation_read_node(runtime, graph, source_index, &out_nodes[i]))
                    {
                        ok = false;
                        break;
                    }
                }
            }
        }
    }

    SDL_free(distances);
    SDL_free(previous);
    SDL_free(visited);
    SDL_free(reverse_path);
    return ok;
}

bool slayer3d_game_data_sector_nav_path_available(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                                  slayer3d_vec3 start, slayer3d_vec3 goal)
{
    return slayer3d_game_data_sector_nav_path(runtime, graph_name, start, goal, NULL, 0, NULL, NULL);
}

bool slayer3d_game_data_sector_nav_next_node(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                             slayer3d_vec3 start, slayer3d_vec3 goal,
                                             slayer3d_game_data_sector_nav_node *out_node)
{
    int node_count = 0;
    if (!slayer3d_game_data_sector_nav_path(runtime, graph_name, start, goal, NULL, 0, &node_count, NULL) ||
        node_count <= 0)
        return false;

    slayer3d_game_data_sector_nav_node *nodes = (slayer3d_game_data_sector_nav_node *)SDL_malloc(
        sizeof(slayer3d_game_data_sector_nav_node) * (size_t)node_count);
    if (nodes == NULL)
        return false;
    const bool ok =
        slayer3d_game_data_sector_nav_path(runtime, graph_name, start, goal, nodes, node_count, &node_count, NULL);
    if (ok && out_node != NULL)
        *out_node = nodes[node_count > 1 ? 1 : 0];
    SDL_free(nodes);
    return ok;
}

bool slayer3d_game_data_get_sector_level(const slayer3d_game_data_runtime *runtime, const char *name,
                                         slayer3d_game_data_sector_level *out_level)
{
    if (out_level != NULL)
        SDL_zero(*out_level);
    if (runtime == NULL || name == NULL || out_level == NULL)
        return false;

    const sector_level_runtime *level = find_sector_level_runtime(runtime, name);
    if (level == NULL)
        return false;

    out_level->name = level->name;
    out_level->sectors = level->sectors;
    out_level->sector_names = level->sector_names;
    out_level->sector_count = level->sector_count;
    out_level->materials = level->materials;
    out_level->material_count = level->material_count;
    out_level->lights = level->lights;
    out_level->light_count = level->light_count;
    out_level->lightmapped = &level->lightmapped;
    out_level->vertex_baked = &level->vertex_baked;
    out_level->unlit = &level->unlit;
    return true;
}

static brush_world_runtime *find_brush_world_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->brush_world_count; ++i)
    {
        brush_world_runtime *world = &runtime->brush_worlds[i];
        if (world->desc.name != NULL && SDL_strcmp(world->desc.name, name) == 0)
            return world;
    }
    return NULL;
}

const brush_world_runtime *find_brush_world_runtime(const slayer3d_game_data_runtime *runtime, const char *name)
{
    return find_brush_world_runtime_mutable((slayer3d_game_data_runtime *)runtime, name);
}

static int find_editor_brush_material_index(const brush_world_runtime *world_runtime, const char *material_name);

static bool editor_brush_world_set_source_path(brush_world_runtime *world_runtime, const char *source_path)
{
    if (world_runtime == NULL || source_path == NULL)
        return world_runtime != NULL;

    char *copy = SDL_strdup(source_path);
    if (copy == NULL)
        return false;
    SDL_free(world_runtime->editor_source_path);
    world_runtime->editor_source_path = copy;
    return true;
}

static void editor_brush_world_mark_dirty(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return;
    if (world_runtime->editor_revision < SDL_MAX_UINT64)
        world_runtime->editor_revision++;
    world_runtime->editor_dirty = true;
}

static bool editor_brush_world_mark_saved(brush_world_runtime *world_runtime, const char *source_path)
{
    if (world_runtime == NULL)
        return false;
    if (!editor_brush_world_set_source_path(world_runtime, source_path))
        return false;
    world_runtime->editor_saved_revision = world_runtime->editor_revision;
    world_runtime->editor_dirty = false;
    return true;
}

static bool editor_brush_world_name_exists(const brush_world_runtime *world_runtime, const char *brush_name)
{
    if (world_runtime == NULL || brush_name == NULL || brush_name[0] == '\0')
        return false;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->brush_count; ++i)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[i];
        if (brush->name != NULL && SDL_strcmp(brush->name, brush_name) == 0)
            return true;
    }
    return false;
}

static bool editor_brush_world_generate_brush_name(const brush_world_runtime *world_runtime, char *buffer,
                                                   size_t buffer_size)
{
    if (world_runtime == NULL || buffer == NULL || buffer_size == 0u)
        return false;
    const char *world_name = world_runtime->desc.name != NULL ? world_runtime->desc.name : "brush_world";
    for (int i = 0; i < 1000000; ++i)
    {
        SDL_snprintf(buffer, buffer_size, "%s.box.%d", world_name, i + 1);
        if (buffer[0] == '\0')
            return false;
        if (!editor_brush_world_name_exists(world_runtime, buffer))
            return true;
    }
    return false;
}

static void free_editor_runtime_brush(slayer3d_game_data_brush *brush)
{
    if (brush == NULL)
        return;
    SDL_free((void *)brush->name);
    for (int tag_index = 0; tag_index < brush->tag_count; ++tag_index)
        SDL_free((void *)brush->tags[tag_index]);
    SDL_free((void *)brush->tags);
    for (int face_index = 0; face_index < brush->face_count; ++face_index)
    {
        slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[face_index];
        free_editor_metadata(&face->editor);
    }
    SDL_free((void *)brush->faces);
    free_editor_metadata(&brush->editor);
    SDL_zero(*brush);
}

static void init_box_brush_face(slayer3d_game_data_brush_face *face, slayer3d_vec3 normal, float distance,
                                int material_index, const char *material_name)
{
    SDL_zero(*face);
    face->normal = normal;
    face->distance = distance;
    face->material_index = material_index;
    face->material_name = material_name;
    face->uv_scale[0] = 1.0f;
    face->uv_scale[1] = 1.0f;
}

static bool init_editor_box_brush(const brush_world_runtime *world_runtime,
                                  const slayer3d_game_data_create_box_brush_desc *desc, const char *brush_name,
                                  int material_index, slayer3d_game_data_brush *out_brush)
{
    if (world_runtime == NULL || desc == NULL || brush_name == NULL || out_brush == NULL || material_index < 0 ||
        material_index >= world_runtime->desc.material_count)
    {
        return false;
    }

    SDL_zero(*out_brush);
    out_brush->name = SDL_strdup(brush_name);
    out_brush->contents = desc->contents != 0u ? desc->contents : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    out_brush->face_count = 6;
    out_brush->faces = (slayer3d_game_data_brush_face *)SDL_calloc(6u, sizeof(*out_brush->faces));
    if (out_brush->name == NULL || out_brush->faces == NULL)
    {
        free_editor_runtime_brush(out_brush);
        return false;
    }

    const char *material_name = world_runtime->desc.materials[material_index].name;
    slayer3d_game_data_brush_face *faces = (slayer3d_game_data_brush_face *)out_brush->faces;
    init_box_brush_face(&faces[0], slayer3d_vec3_make(1.0f, 0.0f, 0.0f), desc->max.x, material_index, material_name);
    init_box_brush_face(&faces[1], slayer3d_vec3_make(-1.0f, 0.0f, 0.0f), -desc->min.x, material_index, material_name);
    init_box_brush_face(&faces[2], slayer3d_vec3_make(0.0f, 1.0f, 0.0f), desc->max.y, material_index, material_name);
    init_box_brush_face(&faces[3], slayer3d_vec3_make(0.0f, -1.0f, 0.0f), -desc->min.y, material_index, material_name);
    init_box_brush_face(&faces[4], slayer3d_vec3_make(0.0f, 0.0f, 1.0f), desc->max.z, material_index, material_name);
    init_box_brush_face(&faces[5], slayer3d_vec3_make(0.0f, 0.0f, -1.0f), -desc->min.z, material_index, material_name);
    return true;
}

bool slayer3d_game_data_create_box_brush(slayer3d_game_data_runtime *runtime,
                                         const slayer3d_game_data_create_box_brush_desc *desc, char *out_brush_name,
                                         size_t out_brush_name_size, char *error_buffer, int error_buffer_size)
{
    if (out_brush_name != NULL && out_brush_name_size > 0u)
        out_brush_name[0] = '\0';
    if (runtime == NULL || desc == NULL || desc->world_name == NULL || desc->world_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "box brush creation requires a brush world");
        return false;
    }
    if (desc->material_name == NULL || desc->material_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "box brush creation requires a material");
        return false;
    }
    if (!(desc->min.x < desc->max.x && desc->min.y < desc->max.y && desc->min.z < desc->max.z))
    {
        set_error(error_buffer, error_buffer_size, "box brush bounds must have min < max on all axes");
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, desc->world_name);
    if (world_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush world not found");
        return false;
    }
    const int material_index = find_editor_brush_material_index(world_runtime, desc->material_name);
    if (material_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "box brush material not found");
        return false;
    }

    char generated_name[256];
    const char *brush_name = desc->brush_name;
    if (brush_name == NULL || brush_name[0] == '\0')
    {
        if (!editor_brush_world_generate_brush_name(world_runtime, generated_name, sizeof(generated_name)))
        {
            set_error(error_buffer, error_buffer_size, "failed to generate box brush name");
            return false;
        }
        brush_name = generated_name;
    }
    else if (editor_brush_world_name_exists(world_runtime, brush_name))
    {
        set_error(error_buffer, error_buffer_size, "box brush name already exists");
        return false;
    }

    slayer3d_game_data_brush new_brush;
    if (!init_editor_box_brush(world_runtime, desc, brush_name, material_index, &new_brush))
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate box brush");
        return false;
    }

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    slayer3d_game_data_brush *old_brushes = (slayer3d_game_data_brush *)world->brushes;
    const int old_count = world->brush_count;
    slayer3d_game_data_brush *brushes =
        (slayer3d_game_data_brush *)SDL_calloc((size_t)old_count + 1u, sizeof(*brushes));
    if (brushes == NULL)
    {
        free_editor_runtime_brush(&new_brush);
        set_error(error_buffer, error_buffer_size, "failed to allocate brush world entries");
        return false;
    }
    if (old_count > 0)
        SDL_memcpy(brushes, old_brushes, sizeof(*brushes) * (size_t)old_count);
    brushes[old_count] = new_brush;
    world->brushes = brushes;
    world->brush_count = old_count + 1;

    slayer3d_model render_model;
    SDL_zero(render_model);
    const bool rebuilt = slayer3d_game_data_brush_world_build_acceleration(world) &&
                         slayer3d_game_data_brush_world_compile_render_model(world, &render_model);
    if (!rebuilt)
    {
        slayer3d_free_model(&render_model);
        world->brushes = old_brushes;
        world->brush_count = old_count;
        free_editor_runtime_brush(&brushes[old_count]);
        SDL_free(brushes);
        (void)slayer3d_game_data_brush_world_build_acceleration(world);
        set_error(error_buffer, error_buffer_size, "failed to rebuild brush world after box creation");
        return false;
    }

    SDL_free(old_brushes);
    slayer3d_free_model(&world_runtime->render_model);
    world_runtime->render_model = render_model;
    world->render_model = &world_runtime->render_model;
    editor_brush_world_mark_dirty(world_runtime);
    if (out_brush_name != NULL && out_brush_name_size > 0u)
        SDL_strlcpy(out_brush_name, brushes[old_count].name != NULL ? brushes[old_count].name : "",
                    out_brush_name_size);
    return true;
}

bool slayer3d_game_data_get_brush_world_editor_state(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                                     slayer3d_game_data_brush_world_editor_state *out_state)
{
    if (out_state != NULL)
        SDL_zero(*out_state);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL || out_state == NULL)
        return false;
    out_state->world_name = world_runtime->desc.name;
    out_state->source_path = world_runtime->editor_source_path;
    out_state->dirty = world_runtime->editor_dirty;
    out_state->revision = world_runtime->editor_revision;
    out_state->saved_revision = world_runtime->editor_saved_revision;
    return true;
}

bool slayer3d_game_data_mark_brush_world_saved(slayer3d_game_data_runtime *runtime, const char *world_name,
                                               const char *source_path, char *error_buffer, int error_buffer_size)
{
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    if (world_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush world not found");
        return false;
    }
    if (!editor_brush_world_mark_saved(world_runtime, source_path))
    {
        set_error(error_buffer, error_buffer_size, "failed to update brush world save state");
        return false;
    }
    return true;
}

static editor_player_start_runtime *find_editor_player_start_mutable(slayer3d_game_data_runtime *runtime,
                                                                     const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->editor_player_start_count; ++i)
    {
        if (runtime->editor_player_starts[i].name != NULL &&
            SDL_strcmp(runtime->editor_player_starts[i].name, name) == 0)
        {
            return &runtime->editor_player_starts[i];
        }
    }
    return NULL;
}

static const editor_player_start_runtime *find_editor_player_start(const slayer3d_game_data_runtime *runtime,
                                                                   const char *name)
{
    return find_editor_player_start_mutable((slayer3d_game_data_runtime *)runtime, name);
}

static bool runtime_scene_exists(const slayer3d_game_data_runtime *runtime, const char *scene)
{
    if (scene == NULL || scene[0] == '\0')
        return true;
    for (int i = 0; runtime != NULL && i < runtime->scene_count; ++i)
    {
        if (runtime->scenes[i].name != NULL && SDL_strcmp(runtime->scenes[i].name, scene) == 0)
            return true;
    }
    return false;
}

bool slayer3d_game_data_get_editor_player_start(const slayer3d_game_data_runtime *runtime, const char *name,
                                                slayer3d_game_data_editor_player_start *out_start)
{
    if (out_start != NULL)
        SDL_zero(*out_start);
    const editor_player_start_runtime *start = find_editor_player_start(runtime, name);
    if (start == NULL || out_start == NULL)
        return false;
    out_start->name = start->name;
    out_start->scene = start->scene;
    out_start->target = start->target;
    out_start->position = start->position;
    out_start->yaw = start->yaw;
    out_start->pitch = start->pitch;
    return true;
}

bool slayer3d_game_data_get_player_start_editor_state(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_player_start_editor_state *out_state)
{
    if (out_state != NULL)
        SDL_zero(*out_state);
    if (runtime == NULL || out_state == NULL)
        return false;
    out_state->source_path = runtime->editor_player_start_source_path;
    out_state->dirty = runtime->editor_player_start_dirty;
    out_state->revision = runtime->editor_player_start_revision;
    out_state->saved_revision = runtime->editor_player_start_saved_revision;
    out_state->count = runtime->editor_player_start_count;
    return true;
}

static bool ensure_editor_player_start_capacity(slayer3d_game_data_runtime *runtime, int required_capacity)
{
    if (runtime == NULL || required_capacity <= runtime->editor_player_start_capacity)
        return runtime != NULL;
    int capacity = runtime->editor_player_start_capacity > 0 ? runtime->editor_player_start_capacity : 4;
    while (capacity < required_capacity)
        capacity *= 2;
    editor_player_start_runtime *starts =
        (editor_player_start_runtime *)SDL_realloc(runtime->editor_player_starts, (size_t)capacity * sizeof(*starts));
    if (starts == NULL)
        return false;
    SDL_memset(starts + runtime->editor_player_start_capacity, 0,
               (size_t)(capacity - runtime->editor_player_start_capacity) * sizeof(*starts));
    runtime->editor_player_starts = starts;
    runtime->editor_player_start_capacity = capacity;
    return true;
}

static bool duplicate_optional_string(const char *value, char **out_copy)
{
    if (out_copy != NULL)
        *out_copy = NULL;
    if (out_copy == NULL)
        return false;
    if (value == NULL || value[0] == '\0')
        return true;
    *out_copy = SDL_strdup(value);
    return *out_copy != NULL;
}

static void mark_editor_player_starts_dirty(slayer3d_game_data_runtime *runtime)
{
    runtime->editor_player_start_revision++;
    runtime->editor_player_start_dirty = true;
}

bool slayer3d_game_data_place_editor_player_start(slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_place_player_start_desc *desc,
                                                  char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || desc == NULL || desc->name == NULL || desc->name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "player start placement requires a runtime and name");
        return false;
    }

    const char *scene = first_non_empty_string(desc->scene, slayer3d_game_data_active_scene(runtime), NULL);
    if (!runtime_scene_exists(runtime, scene))
    {
        set_errorf(error_buffer, error_buffer_size, "player start scene '%s' not found", scene);
        return false;
    }

    slayer3d_registered_actor *target =
        desc->target != NULL && desc->target[0] != '\0' ? slayer3d_game_data_find_actor(runtime, desc->target) : NULL;
    if (desc->target != NULL && desc->target[0] != '\0' && target == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "player start target '%s' not found", desc->target);
        return false;
    }

    slayer3d_vec3 position = desc->position;
    if (!desc->has_position)
    {
        slayer3d_game_data_editor_selection selection;
        SDL_zero(selection);
        if (slayer3d_game_data_get_active_editor_selection(runtime, &selection) && selection.hit)
            position = selection.point;
        else if (target != NULL)
            position = target->position;
        else
        {
            set_error(error_buffer, error_buffer_size, "player start placement requires a position or target");
            return false;
        }
    }

    const float yaw =
        desc->has_yaw || target == NULL ? desc->yaw : slayer3d_properties_get_float(target->props, "yaw", 0.0f);
    const float pitch =
        desc->has_pitch || target == NULL ? desc->pitch : slayer3d_properties_get_float(target->props, "pitch", 0.0f);

    char *scene_copy = NULL;
    char *target_copy = NULL;
    if (!duplicate_optional_string(scene, &scene_copy) || !duplicate_optional_string(desc->target, &target_copy))
    {
        SDL_free(scene_copy);
        SDL_free(target_copy);
        set_error(error_buffer, error_buffer_size, "failed to allocate player start fields");
        return false;
    }

    editor_player_start_runtime *entry = find_editor_player_start_mutable(runtime, desc->name);
    if (entry == NULL)
    {
        if (!ensure_editor_player_start_capacity(runtime, runtime->editor_player_start_count + 1))
        {
            SDL_free(scene_copy);
            SDL_free(target_copy);
            set_error(error_buffer, error_buffer_size, "failed to allocate player start");
            return false;
        }
        entry = &runtime->editor_player_starts[runtime->editor_player_start_count];
        entry->name = SDL_strdup(desc->name);
        if (entry->name == NULL)
        {
            SDL_free(scene_copy);
            SDL_free(target_copy);
            set_error(error_buffer, error_buffer_size, "failed to allocate player start name");
            return false;
        }
        runtime->editor_player_start_count++;
    }

    SDL_free(entry->scene);
    SDL_free(entry->target);
    entry->scene = scene_copy;
    entry->target = target_copy;
    entry->position = position;
    entry->yaw = yaw;
    entry->pitch = pitch;
    mark_editor_player_starts_dirty(runtime);

    if (desc->apply_to_target && target != NULL)
    {
        actor_set_position(target, position);
        slayer3d_properties_set_float(target->props, "yaw", yaw);
        slayer3d_properties_set_float(target->props, "pitch", pitch);
    }
    return true;
}

static bool export_add_vec3(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, slayer3d_vec3 value)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_real(doc, arr, value.x) && yyjson_mut_arr_add_real(doc, arr, value.y) &&
           yyjson_mut_arr_add_real(doc, arr, value.z) && yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_vec4(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, slayer3d_vec4 value)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_real(doc, arr, value.x) && yyjson_mut_arr_add_real(doc, arr, value.y) &&
           yyjson_mut_arr_add_real(doc, arr, value.z) && yyjson_mut_arr_add_real(doc, arr, value.w) &&
           yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_vec2_values(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, float x, float y)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_real(doc, arr, x) && yyjson_mut_arr_add_real(doc, arr, y) &&
           yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_optional_string(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, const char *value)
{
    return value == NULL || value[0] == '\0' || yyjson_mut_obj_add_strcpy(doc, obj, key, value);
}

static bool export_add_string_array(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key,
                                    const char *const *values, int count)
{
    if (count <= 0)
        return true;
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (arr == NULL)
        return false;
    for (int i = 0; i < count; ++i)
    {
        if (values[i] == NULL || !yyjson_mut_arr_add_strcpy(doc, arr, values[i]))
            return false;
    }
    return yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_editor_metadata(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                       const slayer3d_game_data_editor_metadata *metadata)
{
    if (metadata == NULL)
        return true;
    const bool has_snap =
        metadata->has_snap_grid || metadata->snap_rotation_degrees > 0.0f || metadata->snap_align_to_floor;
    if (metadata->stable_id == NULL && metadata->display_name == NULL && metadata->description == NULL &&
        metadata->category == NULL && metadata->group == NULL && metadata->prefab == NULL &&
        metadata->archetype == NULL && metadata->icon == NULL && metadata->preview_asset == NULL &&
        metadata->tag_count <= 0 && !has_snap)
    {
        return true;
    }

    yyjson_mut_val *editor = yyjson_mut_obj(doc);
    if (editor == NULL || !yyjson_mut_obj_add_val(doc, obj, "editor", editor))
        return false;
    if (!export_add_optional_string(doc, editor, "stable_id", metadata->stable_id) ||
        !export_add_optional_string(doc, editor, "display_name", metadata->display_name) ||
        !export_add_optional_string(doc, editor, "description", metadata->description) ||
        !export_add_optional_string(doc, editor, "category", metadata->category) ||
        !export_add_optional_string(doc, editor, "group", metadata->group) ||
        !export_add_optional_string(doc, editor, "prefab", metadata->prefab) ||
        !export_add_optional_string(doc, editor, "archetype", metadata->archetype) ||
        !export_add_optional_string(doc, editor, "icon", metadata->icon) ||
        !export_add_optional_string(doc, editor, "preview_asset", metadata->preview_asset) ||
        !export_add_string_array(doc, editor, "tags", metadata->tags, metadata->tag_count))
    {
        return false;
    }
    if (has_snap)
    {
        yyjson_mut_val *snap = yyjson_mut_obj(doc);
        if (snap == NULL || !yyjson_mut_obj_add_val(doc, editor, "snap", snap))
            return false;
        if (metadata->has_snap_grid && !export_add_vec3(doc, snap, "grid", metadata->snap_grid))
            return false;
        if (metadata->snap_rotation_degrees > 0.0f &&
            !yyjson_mut_obj_add_real(doc, snap, "rotation_degrees", metadata->snap_rotation_degrees))
        {
            return false;
        }
        if (metadata->snap_align_to_floor && !yyjson_mut_obj_add_bool(doc, snap, "align_to_floor", true))
            return false;
    }
    return true;
}

typedef struct brush_flag_export_entry
{
    unsigned int flag;
    const char *name;
} brush_flag_export_entry;

static bool export_add_flag_array(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, unsigned int flags,
                                  const brush_flag_export_entry *entries, size_t entry_count)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (arr == NULL)
        return false;
    for (size_t i = 0; i < entry_count; ++i)
    {
        if ((flags & entries[i].flag) != 0u && !yyjson_mut_arr_add_strcpy(doc, arr, entries[i].name))
            return false;
    }
    return yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_brush_contents(yyjson_mut_doc *doc, yyjson_mut_val *brush, unsigned int flags)
{
    static const brush_flag_export_entry entries[] = {
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID, "solid"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP, "player_clip"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP, "projectile_clip"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER, "trigger"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER, "water"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA, "lava"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY, "sky"},
    };
    return export_add_flag_array(doc, brush, "contents", flags != 0u ? flags : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID,
                                 entries, SDL_arraysize(entries));
}

static bool export_add_surface_flags(yyjson_mut_doc *doc, yyjson_mut_val *face, unsigned int flags)
{
    if (flags == 0u)
        return true;
    static const brush_flag_export_entry entries[] = {
        {SLAYER3D_GAME_DATA_BRUSH_SURFACE_NO_COLLIDE, "nocollide"},
        {SLAYER3D_GAME_DATA_BRUSH_SURFACE_SLICK, "slick"},
        {SLAYER3D_GAME_DATA_BRUSH_SURFACE_LADDER, "ladder"},
        {SLAYER3D_GAME_DATA_BRUSH_SURFACE_EMISSIVE, "emissive"},
        {SLAYER3D_GAME_DATA_BRUSH_SURFACE_PORTAL_CANDIDATE, "portal_candidate"},
    };
    return export_add_flag_array(doc, face, "surface_flags", flags, entries, SDL_arraysize(entries));
}

static bool export_add_brush_material(yyjson_mut_doc *doc, yyjson_mut_val *materials,
                                      const slayer3d_game_data_brush_material *material)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    return obj != NULL && yyjson_mut_arr_add_val(materials, obj) &&
           yyjson_mut_obj_add_strcpy(doc, obj, "name", material->name != NULL ? material->name : "") &&
           export_add_optional_string(doc, obj, "texture", material->texture) &&
           export_add_vec4(doc, obj, "albedo", material->albedo) &&
           yyjson_mut_obj_add_real(doc, obj, "metallic", material->metallic) &&
           yyjson_mut_obj_add_real(doc, obj, "roughness", material->roughness) &&
           export_add_vec3(doc, obj, "emissive", material->emissive) &&
           yyjson_mut_obj_add_real(doc, obj, "tex_scale", material->tex_scale) &&
           export_add_editor_metadata(doc, obj, &material->editor);
}

static bool export_add_brush_face(yyjson_mut_doc *doc, yyjson_mut_val *faces,
                                  const slayer3d_game_data_brush_world *world,
                                  const slayer3d_game_data_brush_face *face)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *plane = yyjson_mut_obj(doc);
    yyjson_mut_val *uv = yyjson_mut_obj(doc);
    const char *material = face->material_name;
    if (material == NULL && face->material_index >= 0 && face->material_index < world->material_count)
        material = world->materials[face->material_index].name;
    return obj != NULL && plane != NULL && uv != NULL && yyjson_mut_arr_add_val(faces, obj) &&
           yyjson_mut_obj_add_val(doc, obj, "plane", plane) && export_add_vec3(doc, plane, "normal", face->normal) &&
           yyjson_mut_obj_add_real(doc, plane, "distance", face->distance) &&
           yyjson_mut_obj_add_strcpy(doc, obj, "material", material != NULL ? material : "") &&
           yyjson_mut_obj_add_val(doc, obj, "uv", uv) &&
           export_add_vec2_values(doc, uv, "scale", face->uv_scale[0], face->uv_scale[1]) &&
           export_add_vec2_values(doc, uv, "offset", face->uv_offset[0], face->uv_offset[1]) &&
           yyjson_mut_obj_add_real(doc, uv, "rotation_degrees", face->uv_rotation_degrees) &&
           export_add_surface_flags(doc, obj, face->surface_flags) &&
           export_add_editor_metadata(doc, obj, &face->editor);
}

static bool export_add_brush(yyjson_mut_doc *doc, yyjson_mut_val *brushes, const slayer3d_game_data_brush_world *world,
                             const slayer3d_game_data_brush *brush)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *faces = yyjson_mut_arr(doc);
    if (obj == NULL || faces == NULL || !yyjson_mut_arr_add_val(brushes, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "name", brush->name != NULL ? brush->name : "") ||
        !export_add_brush_contents(doc, obj, brush->contents) ||
        !export_add_string_array(doc, obj, "tags", brush->tags, brush->tag_count) ||
        !export_add_editor_metadata(doc, obj, &brush->editor) || !yyjson_mut_obj_add_val(doc, obj, "faces", faces))
    {
        return false;
    }
    for (int i = 0; i < brush->face_count; ++i)
    {
        if (!export_add_brush_face(doc, faces, world, &brush->faces[i]))
            return false;
    }
    return true;
}

static bool export_add_brush_world(yyjson_mut_doc *doc, yyjson_mut_val *worlds,
                                   const slayer3d_game_data_brush_world *world)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *materials = yyjson_mut_arr(doc);
    yyjson_mut_val *brushes = yyjson_mut_arr(doc);
    if (obj == NULL || materials == NULL || brushes == NULL || !yyjson_mut_arr_add_val(worlds, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "name", world->name != NULL ? world->name : "") ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "units", world->units != NULL ? world->units : "meters") ||
        !yyjson_mut_obj_add_real(doc, obj, "meters_per_unit", world->meters_per_unit) ||
        !export_add_editor_metadata(doc, obj, &world->editor) ||
        !yyjson_mut_obj_add_val(doc, obj, "materials", materials) ||
        !yyjson_mut_obj_add_val(doc, obj, "brushes", brushes))
    {
        return false;
    }
    for (int i = 0; i < world->material_count; ++i)
    {
        if (!export_add_brush_material(doc, materials, &world->materials[i]))
            return false;
    }
    for (int i = 0; i < world->brush_count; ++i)
    {
        if (!export_add_brush(doc, brushes, world, &world->brushes[i]))
            return false;
    }
    return true;
}

bool slayer3d_game_data_export_brush_world_fragment_json(const slayer3d_game_data_runtime *runtime,
                                                         const char *world_name, char **out_json, size_t *out_size,
                                                         char *error_buffer, int error_buffer_size)
{
    if (out_json != NULL)
        *out_json = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || out_json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush world export requires runtime, world name, and output");
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "brush world '%s' not found", world_name);
        return false;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *worlds = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    if (doc == NULL || root == NULL || worlds == NULL)
    {
        yyjson_mut_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to allocate brush world export document");
        return false;
    }

    yyjson_mut_doc_set_root(doc, root);
    bool ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.fragment.v0") &&
              yyjson_mut_obj_add_val(doc, root, "brush_worlds", worlds) &&
              export_add_brush_world(doc, worlds, &world_runtime->desc);
    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to write brush world export JSON");
        return false;
    }

    char *copy = (char *)SDL_malloc(size + 1u);
    if (copy == NULL)
    {
        free(json);
        set_error(error_buffer, error_buffer_size, "failed to allocate brush world export JSON");
        return false;
    }
    SDL_memcpy(copy, json, size + 1u);
    free(json);
    *out_json = copy;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

static bool export_add_player_start(yyjson_mut_doc *doc, yyjson_mut_val *starts,
                                    const editor_player_start_runtime *start)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || !yyjson_mut_arr_add_val(starts, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "name", start->name != NULL ? start->name : "") ||
        !export_add_vec3(doc, obj, "position", start->position) ||
        !yyjson_mut_obj_add_real(doc, obj, "yaw", start->yaw) ||
        !yyjson_mut_obj_add_real(doc, obj, "pitch", start->pitch))
    {
        return false;
    }
    if (start->scene != NULL && !yyjson_mut_obj_add_strcpy(doc, obj, "scene", start->scene))
        return false;
    if (start->target != NULL && !yyjson_mut_obj_add_strcpy(doc, obj, "target", start->target))
        return false;
    return true;
}

bool slayer3d_game_data_export_player_starts_fragment_json(const slayer3d_game_data_runtime *runtime, char **out_json,
                                                           size_t *out_size, char *error_buffer, int error_buffer_size)
{
    if (out_json != NULL)
        *out_json = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (runtime == NULL || out_json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "player start export requires runtime and output");
        return false;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *starts = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    if (doc == NULL || root == NULL || starts == NULL)
    {
        yyjson_mut_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to allocate player start export document");
        return false;
    }

    yyjson_mut_doc_set_root(doc, root);
    bool ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.fragment.v0") &&
              yyjson_mut_obj_add_val(doc, root, "editor_player_starts", starts);
    for (int i = 0; ok && i < runtime->editor_player_start_count; ++i)
        ok = export_add_player_start(doc, starts, &runtime->editor_player_starts[i]);

    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to write player start export JSON");
        return false;
    }

    char *copy = (char *)SDL_malloc(size + 1u);
    if (copy == NULL)
    {
        free(json);
        set_error(error_buffer, error_buffer_size, "failed to allocate player start export JSON");
        return false;
    }
    SDL_memcpy(copy, json, size + 1u);
    free(json);
    *out_json = copy;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

static bool editor_save_write_all(SDL_IOStream *stream, const void *data, size_t size)
{
    const Uint8 *bytes = (const Uint8 *)data;
    size_t written = 0u;
    while (written < size)
    {
        const size_t chunk = SDL_WriteIO(stream, bytes + written, size - written);
        if (chunk == 0u)
            return false;
        written += chunk;
    }
    return true;
}

static bool editor_save_make_directory_recursive(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;

    SDL_PathInfo info;
    SDL_zero(info);
    if (SDL_GetPathInfo(path, &info))
        return info.type == SDL_PATHTYPE_DIRECTORY;

    char *copy = SDL_strdup(path);
    if (copy == NULL)
        return false;

    bool ok = true;
    for (char *p = copy + 1; *p != '\0'; ++p)
    {
        if (*p != '/' && *p != '\\')
            continue;
        const char saved = *p;
        *p = '\0';
        if (copy[0] != '\0')
        {
            SDL_zero(info);
            if (!SDL_GetPathInfo(copy, &info))
                ok = SDL_CreateDirectory(copy);
            else
                ok = info.type == SDL_PATHTYPE_DIRECTORY;
        }
        *p = saved;
        if (!ok)
            break;
    }

    if (ok)
    {
        SDL_zero(info);
        if (!SDL_GetPathInfo(copy, &info))
            ok = SDL_CreateDirectory(copy);
        else
            ok = info.type == SDL_PATHTYPE_DIRECTORY;
    }

    SDL_free(copy);
    return ok;
}

static char *editor_save_parent_directory(const char *path)
{
    if (path == NULL)
        return NULL;
    const char *slash = SDL_strrchr(path, '/');
    const char *backslash = SDL_strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
    if (slash == NULL)
        return SDL_strdup(".");
    const size_t len = (size_t)(slash - path);
    if (len == 0u)
        return SDL_strdup("/");
    char *parent = (char *)SDL_malloc(len + 1u);
    if (parent == NULL)
        return NULL;
    SDL_memcpy(parent, path, len);
    parent[len] = '\0';
    return parent;
}

bool slayer3d_game_data_save_brush_world_fragment_file(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                       const char *path, size_t *out_size, char *error_buffer,
                                                       int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0u;
    if (path == NULL || path[0] == '\0' || SDL_strstr(path, "://") != NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush world save requires a filesystem path");
        return false;
    }

    char *json = NULL;
    size_t size = 0u;
    if (!slayer3d_game_data_export_brush_world_fragment_json(runtime, world_name, &json, &size, error_buffer,
                                                             error_buffer_size))
    {
        return false;
    }

    char *parent = editor_save_parent_directory(path);
    if (parent == NULL || !editor_save_make_directory_recursive(parent))
    {
        SDL_free(parent);
        SDL_free(json);
        set_error(error_buffer, error_buffer_size, "failed to create brush world save directory");
        return false;
    }
    SDL_free(parent);

    bool ok = false;
    char temp_path[4096];
    for (int attempt = 0; attempt < 16 && !ok; ++attempt)
    {
        SDL_snprintf(temp_path, sizeof(temp_path), "%s.tmp.%llu.%d", path, (unsigned long long)SDL_GetTicksNS(),
                     attempt);
        SDL_IOStream *stream = SDL_IOFromFile(temp_path, "wb");
        if (stream == NULL)
            continue;
        ok = editor_save_write_all(stream, json, size) && SDL_FlushIO(stream);
        ok = SDL_CloseIO(stream) && ok;
        if (!ok)
        {
            SDL_RemovePath(temp_path);
            continue;
        }
        if (!SDL_RenamePath(temp_path, path))
        {
            SDL_RemovePath(path);
            ok = SDL_RenamePath(temp_path, path);
        }
        if (!ok)
            SDL_RemovePath(temp_path);
    }

    SDL_free(json);
    if (!ok)
    {
        set_error(error_buffer, error_buffer_size, "failed to save brush world fragment");
        return false;
    }
    if (!slayer3d_game_data_mark_brush_world_saved(runtime, world_name, path, error_buffer, error_buffer_size))
        return false;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

bool slayer3d_game_data_get_brush_world(const slayer3d_game_data_runtime *runtime, const char *name,
                                        slayer3d_game_data_brush_world *out_world)
{
    if (out_world != NULL)
        SDL_zero(*out_world);
    if (runtime == NULL || name == NULL || out_world == NULL)
        return false;

    const brush_world_runtime *world = find_brush_world_runtime(runtime, name);
    if (world == NULL)
        return false;

    *out_world = world->desc;
    return true;
}

bool slayer3d_game_data_trace_brush_world(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                          const slayer3d_game_data_brush_trace_desc *desc,
                                          slayer3d_game_data_brush_trace_result *out_result)
{
    if (out_result != NULL)
        *out_result = slayer3d_game_data_brush_trace_default_result(desc);
    if (runtime == NULL || world_name == NULL || desc == NULL || out_result == NULL)
        return false;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
        return false;
    return slayer3d_game_data_brush_world_trace_local_with_diagnostics(
        &world_runtime->desc, desc, true, out_result, &((slayer3d_game_data_runtime *)runtime)->brush_diagnostics);
}

typedef struct brush_scene_trace_context
{
    const slayer3d_game_data_runtime *runtime;
    const slayer3d_game_data_brush_trace_desc *world_desc;
    slayer3d_game_data_brush_trace_result closest;
    bool ok;
} brush_scene_trace_context;

static bool trace_brush_world_instance(void *userdata, const slayer3d_game_data_brush_world_instance *instance)
{
    brush_scene_trace_context *context = (brush_scene_trace_context *)userdata;
    if (context == NULL || context->runtime == NULL || context->world_desc == NULL || instance == NULL ||
        instance->world_name == NULL)
    {
        if (context != NULL)
            context->ok = false;
        return false;
    }

    slayer3d_game_data_brush_trace_desc local_desc = *context->world_desc;
    local_desc.start = slayer3d_vec3_sub(local_desc.start, instance->position);
    local_desc.end = slayer3d_vec3_sub(local_desc.end, instance->position);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(context->runtime, instance->world_name);
    if (world_runtime == NULL)
    {
        context->ok = false;
        return false;
    }
    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)context->runtime;
    ++mutable_runtime->brush_diagnostics.world_instance_count;
    if (instance->acceleration_enabled && world_runtime->desc.has_bounds)
    {
        const slayer3d_bounding_box trace_bounds = slayer3d_game_data_brush_trace_bounds(&local_desc);
        if (!slayer3d_check_aabb_aabb(trace_bounds, world_runtime->desc.bounds))
        {
            ++mutable_runtime->brush_diagnostics.world_bounds_reject_count;
            return true;
        }
    }

    slayer3d_game_data_brush_trace_result local_result;
    if (!slayer3d_game_data_brush_world_trace_local_with_diagnostics(&world_runtime->desc, &local_desc,
                                                                     instance->acceleration_enabled, &local_result,
                                                                     &mutable_runtime->brush_diagnostics))
    {
        context->ok = false;
        return false;
    }

    if (local_result.hit && (!context->closest.hit || local_result.fraction < context->closest.fraction ||
                             (local_result.start_solid && !context->closest.start_solid)))
    {
        local_result.end_position = slayer3d_vec3_add(local_result.end_position, instance->position);
        local_result.point = slayer3d_vec3_add(local_result.point, instance->position);
        local_result.world_position = instance->position;
        context->closest = local_result;
    }
    return true;
}

bool slayer3d_game_data_trace_active_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_brush_trace_desc *desc,
                                                  slayer3d_game_data_brush_trace_result *out_result)
{
    if (out_result != NULL)
        *out_result = slayer3d_game_data_brush_trace_default_result(desc);
    if (runtime == NULL || desc == NULL || out_result == NULL || !slayer3d_game_data_brush_trace_shape_valid(desc))
        return false;

    brush_scene_trace_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.world_desc = desc;
    context.closest = slayer3d_game_data_brush_trace_default_result(desc);
    context.ok = true;
    if (!slayer3d_game_data_for_each_brush_world_instance(runtime, trace_brush_world_instance, &context) || !context.ok)
        return false;

    *out_result = context.closest;
    return true;
}

bool slayer3d_game_data_get_brush_diagnostics(const slayer3d_game_data_runtime *runtime,
                                              slayer3d_game_data_brush_diagnostics *out_diagnostics)
{
    if (runtime == NULL || out_diagnostics == NULL)
        return false;
    *out_diagnostics = runtime->brush_diagnostics;
    return true;
}

void slayer3d_game_data_reset_brush_diagnostics(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    SDL_zero(runtime->brush_diagnostics);
}

void slayer3d_game_data_accumulate_brush_render_diagnostics(slayer3d_game_data_runtime *runtime,
                                                            const slayer3d_render_stats *before,
                                                            const slayer3d_render_stats *after)
{
    if (runtime == NULL || before == NULL || after == NULL)
        return;

    runtime->brush_diagnostics.render_mesh_submissions +=
        after->model_mesh_submissions >= before->model_mesh_submissions
            ? after->model_mesh_submissions - before->model_mesh_submissions
            : 0u;
    runtime->brush_diagnostics.render_mesh_culled += after->model_mesh_culled >= before->model_mesh_culled
                                                         ? after->model_mesh_culled - before->model_mesh_culled
                                                         : 0u;
    runtime->brush_diagnostics.render_mesh_draws +=
        after->model_mesh_draws >= before->model_mesh_draws ? after->model_mesh_draws - before->model_mesh_draws : 0u;
    runtime->brush_diagnostics.render_triangles_submitted +=
        after->model_triangles_submitted >= before->model_triangles_submitted
            ? after->model_triangles_submitted - before->model_triangles_submitted
            : 0u;
}

typedef struct brush_named_trace_context
{
    const slayer3d_game_data_runtime *runtime;
    const char *world_name;
} brush_named_trace_context;

static bool brush_slide_trace_named(void *userdata, const slayer3d_game_data_brush_trace_desc *desc,
                                    slayer3d_game_data_brush_trace_result *out_result)
{
    const brush_named_trace_context *context = (const brush_named_trace_context *)userdata;
    if (context == NULL)
        return false;
    return slayer3d_game_data_trace_brush_world(context->runtime, context->world_name, desc, out_result);
}

static bool brush_slide_trace_active(void *userdata, const slayer3d_game_data_brush_trace_desc *desc,
                                     slayer3d_game_data_brush_trace_result *out_result)
{
    const slayer3d_game_data_runtime *runtime = (const slayer3d_game_data_runtime *)userdata;
    return slayer3d_game_data_trace_active_brush_worlds(runtime, desc, out_result);
}

bool slayer3d_game_data_slide_brush_world(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                          const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                          slayer3d_game_data_brush_trace_result *out_result)
{
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0')
        return false;
    brush_named_trace_context context;
    context.runtime = runtime;
    context.world_name = world_name;
    return slayer3d_game_data_brush_slide_with_trace(brush_slide_trace_named, &context, desc, max_bumps, out_result);
}

bool slayer3d_game_data_slide_active_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                                  slayer3d_game_data_brush_trace_result *out_result)
{
    if (runtime == NULL)
        return false;
    return slayer3d_game_data_brush_slide_with_trace(brush_slide_trace_active, (void *)runtime, desc, max_bumps,
                                                     out_result);
}

static slayer3d_game_data_sector_level_variant sector_level_variant_from_string(const char *variant,
                                                                                const slayer3d_level **out_level,
                                                                                const sector_level_runtime *level,
                                                                                bool sector_lighting_enabled);

static unsigned int world_model_filter_or_all(unsigned int model_filter)
{
    return model_filter != 0u ? model_filter : SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_ALL;
}

static unsigned int brush_contents_mask_or_all(unsigned int contents_mask)
{
    return contents_mask != 0u ? contents_mask
                               : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP |
                                     SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP |
                                     SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER | SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER |
                                     SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA | SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY;
}

static slayer3d_bounding_box translated_bounds(slayer3d_bounding_box bounds, slayer3d_vec3 position)
{
    bounds.min = slayer3d_vec3_add(bounds.min, position);
    bounds.max = slayer3d_vec3_add(bounds.max, position);
    return bounds;
}

static bool model_bounds(const slayer3d_model *model, slayer3d_bounding_box *out_bounds)
{
    if (out_bounds != NULL)
        SDL_zero(*out_bounds);
    if (model == NULL || out_bounds == NULL)
        return false;

    bool has_bounds = false;
    for (int i = 0; i < model->mesh_count; ++i)
    {
        const slayer3d_mesh *mesh = &model->meshes[i];
        if (!mesh->has_local_bounds)
            continue;
        if (!has_bounds)
        {
            *out_bounds = mesh->local_bounds;
            has_bounds = true;
            continue;
        }
        out_bounds->min.x = SDL_min(out_bounds->min.x, mesh->local_bounds.min.x);
        out_bounds->min.y = SDL_min(out_bounds->min.y, mesh->local_bounds.min.y);
        out_bounds->min.z = SDL_min(out_bounds->min.z, mesh->local_bounds.min.z);
        out_bounds->max.x = SDL_max(out_bounds->max.x, mesh->local_bounds.max.x);
        out_bounds->max.y = SDL_max(out_bounds->max.y, mesh->local_bounds.max.y);
        out_bounds->max.z = SDL_max(out_bounds->max.z, mesh->local_bounds.max.z);
    }
    return has_bounds;
}

static void init_world_trace_result(const slayer3d_game_data_world_trace_desc *desc,
                                    slayer3d_game_data_world_trace_result *out_result)
{
    if (out_result == NULL)
        return;
    SDL_zero(*out_result);
    out_result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID;
    out_result->element_index = -1;
    out_result->face_index = -1;
    out_result->fraction = 1.0f;
    if (desc != NULL)
    {
        out_result->end_position = desc->end;
        out_result->point = desc->end;
    }
}

static void init_world_point_result(slayer3d_game_data_world_point_result *out_result)
{
    if (out_result == NULL)
        return;
    SDL_zero(*out_result);
    out_result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID;
    out_result->element_index = -1;
}

static bool world_trace_shape_valid(slayer3d_game_data_brush_trace_shape shape, slayer3d_vec3 extents)
{
    switch (shape)
    {
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT:
        return true;
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE:
        return extents.x >= 0.0f;
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB:
        return extents.x >= 0.0f && extents.y >= 0.0f && extents.z >= 0.0f;
    default:
        return false;
    }
}

static slayer3d_game_data_sector_level_variant sector_level_variant_from_string(const char *variant,
                                                                                const slayer3d_level **out_level,
                                                                                const sector_level_runtime *level,
                                                                                bool sector_lighting_enabled)
{
    if (out_level != NULL)
        *out_level = NULL;
    if (level == NULL)
        return 0;

    const char *name = variant != NULL ? variant : "lightmapped";
    if (SDL_strcmp(name, "lightmapped") == 0)
    {
        if (out_level != NULL)
            *out_level = sector_lighting_enabled ? &level->lightmapped : &level->lightmapped_without_sector_lighting;
        return SLAYER3D_GAME_DATA_SECTOR_LEVEL_LIGHTMAPPED;
    }
    if (SDL_strcmp(name, "vertex_baked") == 0)
    {
        if (out_level != NULL)
            *out_level = sector_lighting_enabled ? &level->vertex_baked : &level->vertex_baked_without_sector_lighting;
        return SLAYER3D_GAME_DATA_SECTOR_LEVEL_VERTEX_BAKED;
    }
    if (SDL_strcmp(name, "unlit") == 0)
    {
        if (out_level != NULL)
            *out_level = sector_lighting_enabled ? &level->unlit : &level->unlit_without_sector_lighting;
        return SLAYER3D_GAME_DATA_SECTOR_LEVEL_UNLIT;
    }
    return 0;
}

bool slayer3d_game_data_for_each_sector_level_instance(const slayer3d_game_data_runtime *runtime,
                                                       slayer3d_game_data_sector_level_instance_fn callback,
                                                       void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *instances = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "sector_levels");
    if (instances == NULL)
        return true;
    if (!yyjson_is_arr(instances))
        return false;

    for (size_t i = 0; i < yyjson_arr_size(instances); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(instances, i);
        const char *level_name = json_string(entry, "level", NULL);
        const char *variant_name = scene_state_string(runtime, json_string(entry, "variant_key", NULL),
                                                      json_string(entry, "variant", "lightmapped"));
        const sector_level_runtime *level_runtime = find_sector_level_runtime(runtime, level_name);
        const bool sector_lighting_enabled = scene_state_bool(runtime, json_string(entry, "sector_lighting_key", NULL),
                                                              json_bool(entry, "sector_lighting", true));
        const slayer3d_level *level = NULL;
        const slayer3d_game_data_sector_level_variant variant =
            sector_level_variant_from_string(variant_name, &level, level_runtime, sector_lighting_enabled);
        if (level_runtime == NULL || level == NULL || variant == 0)
            return false;

        slayer3d_game_data_sector_level_instance instance;
        SDL_zero(instance);
        instance.level_name = level_runtime->name;
        instance.variant_name = variant_name;
        instance.variant = variant;
        instance.level = level;
        instance.sectors = level_runtime->sectors;
        instance.sector_count = level_runtime->sector_count;
        instance.position = json_vec3(entry, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        instance.portal_culling = scene_state_bool(runtime, json_string(entry, "portal_culling_key", NULL),
                                                   json_bool(entry, "portal_culling", true));
        instance.sector_lighting_enabled = sector_lighting_enabled;
        if (!callback(userdata, &instance))
            return true;
    }
    return true;
}

bool slayer3d_game_data_for_each_brush_world_instance(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_brush_world_instance_fn callback,
                                                      void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *instances = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "brush_worlds");
    if (instances == NULL)
        return true;
    if (!yyjson_is_arr(instances))
        return false;

    for (size_t i = 0; i < yyjson_arr_size(instances); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(instances, i);
        const char *world_name = json_string(entry, "world", NULL);
        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
        if (world_runtime == NULL)
            return false;

        slayer3d_game_data_brush_world_instance instance;
        SDL_zero(instance);
        instance.world_name = world_runtime->desc.name;
        instance.world = &world_runtime->desc;
        instance.position = json_vec3(entry, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        instance.acceleration_enabled = scene_state_bool(runtime, json_string(entry, "acceleration_key", NULL),
                                                         json_bool(entry, "acceleration", true));
        instance.lighting_enabled =
            scene_state_bool(runtime, json_string(entry, "lighting_key", NULL), json_bool(entry, "lighting", true));
        instance.debug_wireframe = scene_state_bool(runtime, json_string(entry, "debug_wireframe_key", NULL),
                                                    json_bool(entry, "debug_wireframe", false));
        if (!callback(userdata, &instance))
            return true;
    }
    return true;
}

typedef struct world_model_iteration_context
{
    slayer3d_game_data_world_model_instance_fn callback;
    void *userdata;
    bool stopped;
} world_model_iteration_context;

static bool emit_sector_world_model_instance(void *userdata,
                                             const slayer3d_game_data_sector_level_instance *sector_instance)
{
    world_model_iteration_context *context = (world_model_iteration_context *)userdata;
    if (context == NULL || context->callback == NULL || sector_instance == NULL)
        return false;

    slayer3d_game_data_world_model_instance instance;
    SDL_zero(instance);
    instance.type = SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL;
    instance.name = sector_instance->level_name;
    instance.variant_name = sector_instance->variant_name;
    instance.position = sector_instance->position;
    instance.sector_level = sector_instance->level;
    instance.sectors = sector_instance->sectors;
    instance.sector_count = sector_instance->sector_count;
    slayer3d_bounding_box bounds;
    if (model_bounds(sector_instance->level != NULL ? &sector_instance->level->model : NULL, &bounds))
    {
        instance.bounds = translated_bounds(bounds, sector_instance->position);
        instance.has_bounds = true;
    }

    if (!context->callback(context->userdata, &instance))
    {
        context->stopped = true;
        return false;
    }
    return true;
}

static bool emit_brush_world_model_instance(void *userdata,
                                            const slayer3d_game_data_brush_world_instance *brush_instance)
{
    world_model_iteration_context *context = (world_model_iteration_context *)userdata;
    if (context == NULL || context->callback == NULL || brush_instance == NULL)
        return false;

    slayer3d_game_data_world_model_instance instance;
    SDL_zero(instance);
    instance.type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
    instance.name = brush_instance->world_name;
    instance.position = brush_instance->position;
    instance.brush_world = brush_instance->world;
    if (brush_instance->world != NULL && brush_instance->world->has_bounds)
    {
        instance.bounds = translated_bounds(brush_instance->world->bounds, brush_instance->position);
        instance.has_bounds = true;
    }

    if (!context->callback(context->userdata, &instance))
    {
        context->stopped = true;
        return false;
    }
    return true;
}

bool slayer3d_game_data_for_each_world_model_instance(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_world_model_instance_fn callback,
                                                      void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    world_model_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    if (!slayer3d_game_data_for_each_sector_level_instance(runtime, emit_sector_world_model_instance, &context))
        return false;
    if (context.stopped)
        return true;
    return slayer3d_game_data_for_each_brush_world_instance(runtime, emit_brush_world_model_instance, &context);
}

typedef struct world_sector_trace_context
{
    const slayer3d_game_data_runtime *runtime;
    const slayer3d_game_data_world_trace_desc *desc;
    slayer3d_game_data_world_trace_result *result;
    bool ok;
} world_sector_trace_context;

static bool trace_sector_world_instance(void *userdata, const slayer3d_game_data_sector_level_instance *instance)
{
    world_sector_trace_context *context = (world_sector_trace_context *)userdata;
    if (context == NULL || context->desc == NULL || context->result == NULL || instance == NULL ||
        instance->level == NULL || instance->sectors == NULL)
    {
        if (context != NULL)
            context->ok = false;
        return false;
    }
    if (context->desc->shape != SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT)
        return true;

    const slayer3d_vec3 local_start = slayer3d_vec3_sub(context->desc->start, instance->position);
    const slayer3d_vec3 local_end = slayer3d_vec3_sub(context->desc->end, instance->position);
    const slayer3d_vec3 direction = slayer3d_vec3_sub(local_end, local_start);
    const float distance = slayer3d_vec3_length(direction);
    if (distance <= 0.000001f)
        return true;

    const slayer3d_level_trace_result sector_hit =
        slayer3d_level_trace_point(instance->level, instance->sectors, local_start, direction, distance);
    if (!sector_hit.hit)
        return true;
    if (context->result->hit && sector_hit.fraction >= context->result->fraction)
        return true;

    context->result->hit = true;
    context->result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL;
    context->result->world_name = instance->level_name;
    context->result->world_position = instance->position;
    context->result->element_index = sector_hit.end_sector;
    context->result->face_index = -1;
    context->result->fraction = sector_hit.fraction;
    context->result->end_position = slayer3d_vec3_add(sector_hit.end_point, instance->position);
    context->result->point = context->result->end_position;
    context->result->normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const sector_level_runtime *level_runtime = find_sector_level_runtime(context->runtime, instance->level_name);
    if (level_runtime != NULL && sector_hit.end_sector >= 0 && sector_hit.end_sector < level_runtime->sector_count)
        context->result->element_name = level_runtime->sector_names[sector_hit.end_sector];
    return true;
}

bool slayer3d_game_data_trace_world_models(const slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_world_trace_desc *desc,
                                           slayer3d_game_data_world_trace_result *out_result)
{
    init_world_trace_result(desc, out_result);
    if (runtime == NULL || desc == NULL || out_result == NULL || !world_trace_shape_valid(desc->shape, desc->extents))
    {
        return false;
    }

    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)runtime;
    ++mutable_runtime->world_model_trace_count;
    const unsigned int filter = world_model_filter_or_all(desc->model_filter);

    if ((filter & SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_SECTOR_LEVELS) != 0u)
    {
        world_sector_trace_context context;
        SDL_zero(context);
        context.runtime = runtime;
        context.desc = desc;
        context.result = out_result;
        context.ok = true;
        if (!slayer3d_game_data_for_each_sector_level_instance(runtime, trace_sector_world_instance, &context) ||
            !context.ok)
        {
            return false;
        }
    }

    if ((filter & SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS) != 0u)
    {
        slayer3d_game_data_brush_trace_desc brush_desc;
        SDL_zero(brush_desc);
        brush_desc.start = desc->start;
        brush_desc.end = desc->end;
        brush_desc.shape = desc->shape;
        brush_desc.extents = desc->extents;
        brush_desc.contents_mask = brush_contents_mask_or_all(desc->contents_mask);
        slayer3d_game_data_brush_trace_result brush_result;
        if (!slayer3d_game_data_trace_active_brush_worlds(runtime, &brush_desc, &brush_result))
            return false;
        if (brush_result.hit && (!out_result->hit || brush_result.fraction < out_result->fraction ||
                                 (brush_result.start_solid && !out_result->start_solid)))
        {
            out_result->hit = true;
            out_result->start_solid = brush_result.start_solid;
            out_result->all_solid = brush_result.all_solid;
            out_result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
            out_result->world_name = brush_result.world_name;
            out_result->world_position = brush_result.world_position;
            out_result->element_name = brush_result.brush_name;
            out_result->material_name = brush_result.material_name;
            out_result->element_index = brush_result.brush_index;
            out_result->face_index = brush_result.face_index;
            out_result->fraction = brush_result.fraction;
            out_result->end_position = brush_result.end_position;
            out_result->point = brush_result.point;
            out_result->normal = brush_result.normal;
            out_result->contents = brush_result.contents;
            out_result->surface_flags = brush_result.surface_flags;
        }
    }
    return true;
}

static void init_editor_selection(slayer3d_game_data_editor_selection *selection)
{
    if (selection == NULL)
        return;
    SDL_zero(*selection);
    selection->type = SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID;
    selection->element_index = -1;
    selection->face_index = -1;
    selection->fraction = 1.0f;
}

typedef struct world_model_instance_lookup_context
{
    const char *world_name;
    slayer3d_vec3 position;
    slayer3d_game_data_world_model_type type;
    slayer3d_bounding_box bounds;
    bool has_bounds;
    bool found;
} world_model_instance_lookup_context;

static bool capture_matching_world_model_instance(void *userdata,
                                                  const slayer3d_game_data_world_model_instance *instance)
{
    world_model_instance_lookup_context *context = (world_model_instance_lookup_context *)userdata;
    if (context == NULL || context->world_name == NULL || instance == NULL || instance->name == NULL)
        return false;
    if (context->type != SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID && instance->type != context->type)
        return true;
    if (SDL_strcmp(context->world_name, instance->name) != 0)
        return true;
    if (slayer3d_vec3_length_squared(slayer3d_vec3_sub(context->position, instance->position)) > 0.000001f)
        return true;

    context->bounds = instance->bounds;
    context->has_bounds = instance->has_bounds;
    context->found = true;
    return false;
}

static bool active_world_model_bounds(const slayer3d_game_data_runtime *runtime,
                                      slayer3d_game_data_world_model_type type, const char *world_name,
                                      slayer3d_vec3 position, slayer3d_bounding_box *out_bounds)
{
    if (out_bounds != NULL)
        SDL_zero(*out_bounds);
    if (runtime == NULL || world_name == NULL || out_bounds == NULL)
        return false;

    world_model_instance_lookup_context context;
    SDL_zero(context);
    context.world_name = world_name;
    context.position = position;
    context.type = type;
    if (!slayer3d_game_data_for_each_world_model_instance(runtime, capture_matching_world_model_instance, &context))
        return false;
    if (!context.found || !context.has_bounds)
        return false;
    *out_bounds = context.bounds;
    return true;
}

static bool populate_brush_editor_selection(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_world_trace_result *trace,
                                            slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || trace == NULL || selection == NULL || trace->world_name == NULL)
        return false;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, trace->world_name);
    if (world_runtime == NULL)
        return true;

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    selection->world_editor = &world->editor;
    if (trace->element_index < 0 || trace->element_index >= world->brush_count)
        return true;

    const slayer3d_game_data_brush *brush = &world->brushes[trace->element_index];
    selection->element_editor = &brush->editor;
    if (brush->has_bounds)
    {
        selection->bounds = translated_bounds(brush->bounds, trace->world_position);
        selection->has_bounds = true;
    }

    if (trace->face_index >= 0 && trace->face_index < brush->face_count)
    {
        const slayer3d_game_data_brush_face *face = &brush->faces[trace->face_index];
        selection->face_editor = &face->editor;
        if (face->material_index >= 0 && face->material_index < world->material_count)
            selection->material_editor = &world->materials[face->material_index].editor;
    }
    return true;
}

bool slayer3d_game_data_pick_editor_world_model(const slayer3d_game_data_runtime *runtime,
                                                const slayer3d_game_data_world_trace_desc *desc,
                                                slayer3d_game_data_editor_selection *out_selection)
{
    init_editor_selection(out_selection);
    if (runtime == NULL || desc == NULL || out_selection == NULL)
        return false;

    slayer3d_game_data_world_trace_result trace;
    if (!slayer3d_game_data_trace_world_models(runtime, desc, &trace))
        return false;
    if (!trace.hit)
        return true;

    out_selection->hit = true;
    out_selection->type = trace.type;
    out_selection->world_name = trace.world_name;
    out_selection->world_position = trace.world_position;
    out_selection->element_name = trace.element_name;
    out_selection->material_name = trace.material_name;
    out_selection->element_index = trace.element_index;
    out_selection->face_index = trace.face_index;
    out_selection->fraction = trace.fraction;
    out_selection->point = trace.point;
    out_selection->normal = trace.normal;

    if (trace.type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
        return populate_brush_editor_selection(runtime, &trace, out_selection);

    if (active_world_model_bounds(runtime, trace.type, trace.world_name, trace.world_position, &out_selection->bounds))
        out_selection->has_bounds = true;
    return true;
}

typedef struct editor_debug_iteration_context
{
    slayer3d_game_data_editor_debug_primitive_fn callback;
    void *userdata;
    slayer3d_color color;
    slayer3d_game_data_editor_debug_primitive_type type;
    const char *world_name;
    const char *element_name;
    int face_index;
    bool stopped;
} editor_debug_iteration_context;

static slayer3d_color editor_debug_color_or_default(slayer3d_color color, slayer3d_color fallback)
{
    return color.a != 0 ? color : fallback;
}

static bool emit_editor_debug_line(editor_debug_iteration_context *context, slayer3d_vec3 start, slayer3d_vec3 end)
{
    if (context == NULL || context->callback == NULL)
        return false;

    slayer3d_game_data_editor_debug_primitive primitive;
    SDL_zero(primitive);
    primitive.type = context->type;
    primitive.start = start;
    primitive.end = end;
    primitive.color = context->color;
    primitive.world_name = context->world_name;
    primitive.element_name = context->element_name;
    primitive.face_index = context->face_index;
    if (!context->callback(context->userdata, &primitive))
    {
        context->stopped = true;
        return false;
    }
    return true;
}

static bool emit_editor_debug_bounds(editor_debug_iteration_context *context, slayer3d_bounding_box bounds)
{
    const slayer3d_vec3 min = bounds.min;
    const slayer3d_vec3 max = bounds.max;
    const slayer3d_vec3 corners[8] = {
        slayer3d_vec3_make(min.x, min.y, min.z), slayer3d_vec3_make(max.x, min.y, min.z),
        slayer3d_vec3_make(max.x, min.y, max.z), slayer3d_vec3_make(min.x, min.y, max.z),
        slayer3d_vec3_make(min.x, max.y, min.z), slayer3d_vec3_make(max.x, max.y, min.z),
        slayer3d_vec3_make(max.x, max.y, max.z), slayer3d_vec3_make(min.x, max.y, max.z),
    };
    static const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };

    for (int i = 0; i < 12; ++i)
    {
        if (!emit_editor_debug_line(context, corners[edges[i][0]], corners[edges[i][1]]))
            return false;
    }
    return true;
}

typedef struct editor_world_bounds_context
{
    const slayer3d_game_data_editor_debug_desc *desc;
    slayer3d_game_data_editor_debug_primitive_fn callback;
    void *userdata;
    bool stopped;
} editor_world_bounds_context;

static bool editor_command_preview_active_for_scene(const slayer3d_game_data_runtime *runtime);

static bool emit_editor_debug_world_bounds(void *userdata, const slayer3d_game_data_world_model_instance *instance)
{
    editor_world_bounds_context *bounds_context = (editor_world_bounds_context *)userdata;
    if (bounds_context == NULL || bounds_context->desc == NULL || instance == NULL || !instance->has_bounds)
        return true;

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = bounds_context->callback;
    context.userdata = bounds_context->userdata;
    context.color =
        editor_debug_color_or_default(bounds_context->desc->world_bounds_color, (slayer3d_color){80, 170, 255, 220});
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_WORLD_BOUNDS_EDGE;
    context.world_name = instance->name;
    context.face_index = -1;
    if (!emit_editor_debug_bounds(&context, instance->bounds))
    {
        bounds_context->stopped = context.stopped;
        return false;
    }
    return true;
}

bool slayer3d_game_data_for_each_editor_debug_primitive(const slayer3d_game_data_runtime *runtime,
                                                        const slayer3d_game_data_editor_debug_desc *desc,
                                                        slayer3d_game_data_editor_debug_primitive_fn callback,
                                                        void *userdata)
{
    if (runtime == NULL || desc == NULL || callback == NULL)
        return false;

    const unsigned int flags = desc->flags != 0u ? desc->flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORLD_BOUNDS) != 0u)
    {
        editor_world_bounds_context bounds_context;
        SDL_zero(bounds_context);
        bounds_context.desc = desc;
        bounds_context.callback = callback;
        bounds_context.userdata = userdata;
        if (!slayer3d_game_data_for_each_world_model_instance(runtime, emit_editor_debug_world_bounds, &bounds_context))
        {
            return false;
        }
        if (bounds_context.stopped)
            return true;
    }

    const slayer3d_game_data_editor_selection *selection = desc->selection;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_BOUNDS) != 0u && selection != NULL && selection->hit &&
        selection->has_bounds)
    {
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color =
            editor_debug_color_or_default(desc->selection_bounds_color, (slayer3d_color){255, 220, 40, 255});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_SELECTION_BOUNDS_EDGE;
        context.world_name = selection->world_name;
        context.element_name = selection->element_name;
        context.face_index = selection->face_index;
        if (!emit_editor_debug_bounds(&context, selection->bounds))
            return true;
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_TRACE_RAY) != 0u && desc->trace != NULL)
    {
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color = editor_debug_color_or_default(desc->trace_color, (slayer3d_color){255, 255, 255, 180});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_TRACE_RAY;
        context.world_name = selection != NULL ? selection->world_name : NULL;
        context.element_name = selection != NULL ? selection->element_name : NULL;
        context.face_index = selection != NULL ? selection->face_index : -1;
        if (!emit_editor_debug_line(&context, desc->trace->start, desc->trace->end))
            return true;
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_FACE_NORMAL) != 0u && selection != NULL && selection->hit &&
        slayer3d_vec3_length_squared(selection->normal) > 0.000001f)
    {
        const float length = desc->normal_length > 0.0f ? desc->normal_length : 0.75f;
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color = editor_debug_color_or_default(desc->face_normal_color, (slayer3d_color){40, 255, 120, 255});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_FACE_NORMAL;
        context.world_name = selection->world_name;
        context.element_name = selection->element_name;
        context.face_index = selection->face_index;
        if (!emit_editor_debug_line(
                &context, selection->point,
                slayer3d_vec3_add(selection->point,
                                  slayer3d_vec3_scale(slayer3d_vec3_normalize(selection->normal), length))))
        {
            return true;
        }
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_HIT_MARKER) != 0u && selection != NULL && selection->hit)
    {
        const float size = desc->hit_marker_size > 0.0f ? desc->hit_marker_size : 0.1f;
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color = editor_debug_color_or_default(desc->hit_marker_color, (slayer3d_color){255, 80, 80, 255});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_HIT_MARKER;
        context.world_name = selection->world_name;
        context.element_name = selection->element_name;
        context.face_index = selection->face_index;
        if (!emit_editor_debug_line(&context,
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(-size, 0.0f, 0.0f)),
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(size, 0.0f, 0.0f))) ||
            !emit_editor_debug_line(&context,
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(0.0f, -size, 0.0f)),
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(0.0f, size, 0.0f))) ||
            !emit_editor_debug_line(&context,
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(0.0f, 0.0f, -size)),
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(0.0f, 0.0f, size))))
        {
            return true;
        }
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_COMMAND_PREVIEW) != 0u &&
        editor_command_preview_active_for_scene(runtime) && runtime->editor_command_preview.has_bounds)
    {
        const editor_command_preview_state *preview = &runtime->editor_command_preview;
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color = editor_debug_color_or_default(desc->command_preview_color, (slayer3d_color){80, 255, 255, 220});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_COMMAND_PREVIEW_BOUNDS_EDGE;
        context.world_name = preview->world_name;
        context.element_name = preview->element_name;
        context.face_index = preview->face_index;
        if (!emit_editor_debug_bounds(&context, preview->bounds))
            return true;
    }
    return true;
}

static yyjson_val *active_editor_tooling_root(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return obj_get(scene != NULL ? scene->root : NULL, "editor");
}

static unsigned int editor_model_filter_flag_from_string(const char *value)
{
    if (SDL_strcmp(value != NULL ? value : "", "all") == 0)
        return SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_ALL;
    if (SDL_strcmp(value != NULL ? value : "", "sector_levels") == 0 ||
        SDL_strcmp(value != NULL ? value : "", "sector") == 0)
    {
        return SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_SECTOR_LEVELS;
    }
    if (SDL_strcmp(value != NULL ? value : "", "brush_worlds") == 0 ||
        SDL_strcmp(value != NULL ? value : "", "brush") == 0)
    {
        return SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS;
    }
    return 0u;
}

static unsigned int editor_model_filter_from_json(yyjson_val *value)
{
    if (yyjson_is_str(value))
        return editor_model_filter_flag_from_string(yyjson_get_str(value));
    if (!yyjson_is_arr(value))
        return SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_ALL;

    unsigned int flags = 0u;
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (yyjson_is_str(entry))
            flags |= editor_model_filter_flag_from_string(yyjson_get_str(entry));
    }
    return flags != 0u ? flags : SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_ALL;
}

static unsigned int editor_debug_flag_from_string(const char *value)
{
    if (SDL_strcmp(value != NULL ? value : "", "all") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if (SDL_strcmp(value != NULL ? value : "", "world_bounds") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORLD_BOUNDS;
    if (SDL_strcmp(value != NULL ? value : "", "selection_bounds") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_BOUNDS;
    if (SDL_strcmp(value != NULL ? value : "", "trace_ray") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_TRACE_RAY;
    if (SDL_strcmp(value != NULL ? value : "", "face_normal") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_FACE_NORMAL;
    if (SDL_strcmp(value != NULL ? value : "", "hit_marker") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_HIT_MARKER;
    if (SDL_strcmp(value != NULL ? value : "", "command_preview") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_COMMAND_PREVIEW;
    return 0u;
}

static unsigned int editor_debug_flags_from_json(yyjson_val *value)
{
    if (yyjson_is_str(value))
        return editor_debug_flag_from_string(yyjson_get_str(value));
    if (!yyjson_is_arr(value))
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;

    unsigned int flags = 0u;
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (yyjson_is_str(entry))
            flags |= editor_debug_flag_from_string(yyjson_get_str(entry));
    }
    return flags != 0u ? flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
}

static float editor_scene_state_float(const slayer3d_game_data_runtime *runtime, const char *key, float fallback)
{
    return runtime != NULL && key != NULL ? slayer3d_properties_get_float(runtime->scene_state, key, fallback)
                                          : fallback;
}

static void editor_default_viewport(const slayer3d_game_data_runtime *runtime, float *out_width, float *out_height)
{
    if (out_width == NULL || out_height == NULL)
        return;

    yyjson_val *app = obj_get(runtime_root(runtime), "app");
    yyjson_val *window = obj_get(app, "window");
    const int width =
        json_int(app, "logical_width",
                 json_int(window, "logical_width", json_int(app, "width", SLAYER3D_GAME_DEFAULT_LOGICAL_WIDTH)));
    const int height =
        json_int(app, "logical_height",
                 json_int(window, "logical_height", json_int(app, "height", SLAYER3D_GAME_DEFAULT_LOGICAL_HEIGHT)));
    *out_width = (float)SDL_max(width, 1);
    *out_height = (float)SDL_max(height, 1);
}

static bool editor_trace_screen_point(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                      float viewport_width, float viewport_height, float *out_x, float *out_y)
{
    if (out_x == NULL || out_y == NULL)
        return false;

    *out_x = viewport_width * 0.5f;
    *out_y = viewport_height * 0.5f;

    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
    {
        *out_x = mouse_x;
        *out_y = mouse_y;
    }

    if (!json_vec2_value(obj_get(trace, "screen"), *out_x, *out_y, out_x, out_y))
        return false;

    *out_x = json_float(trace, "screen_x", *out_x);
    *out_y = json_float(trace, "screen_y", *out_y);
    *out_x = editor_scene_state_float(runtime, json_string(trace, "screen_x_key", NULL), *out_x);
    *out_y = editor_scene_state_float(runtime, json_string(trace, "screen_y_key", NULL), *out_y);
    return true;
}

static bool editor_trace_viewport(const slayer3d_game_data_runtime *runtime, yyjson_val *trace, float *out_width,
                                  float *out_height)
{
    editor_default_viewport(runtime, out_width, out_height);
    if (!json_vec2_value(obj_get(trace, "viewport"), *out_width, *out_height, out_width, out_height))
        return false;
    *out_width = json_float(trace, "viewport_width", *out_width);
    *out_height = json_float(trace, "viewport_height", *out_height);
    *out_width = editor_scene_state_float(runtime, json_string(trace, "viewport_width_key", NULL), *out_width);
    *out_height = editor_scene_state_float(runtime, json_string(trace, "viewport_height_key", NULL), *out_height);
    return *out_width > 0.0f && *out_height > 0.0f;
}

static bool editor_camera_screen_trace_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                                 slayer3d_game_data_world_trace_desc *out_trace)
{
    const char *camera_name = json_string(trace, "camera", slayer3d_game_data_active_camera(runtime));
    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, camera_name, &camera))
        return false;

    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
    if (!editor_trace_viewport(runtime, trace, &viewport_width, &viewport_height))
        return false;

    float screen_x = 0.0f;
    float screen_y = 0.0f;
    if (!editor_trace_screen_point(runtime, trace, viewport_width, viewport_height, &screen_x, &screen_y))
        return false;

    const float near_distance = SDL_max(json_float(trace, "near", 0.05f), 0.0f);
    const float far_distance = SDL_max(json_float(trace, "far", 1000.0f), near_distance + 0.001f);
    return slayer3d_camera3d_screen_ray(&camera, viewport_width, viewport_height, screen_x, screen_y, near_distance,
                                        far_distance, &out_trace->start, &out_trace->end);
}

static bool editor_trace_desc_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                        slayer3d_game_data_world_trace_desc *out_trace)
{
    if (out_trace != NULL)
        SDL_zero(*out_trace);
    yyjson_val *trace = obj_get(selection, "trace");
    if (!yyjson_is_obj(trace) || out_trace == NULL)
        return false;

    out_trace->shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    const char *source = json_string(trace, "source", "world");
    if (SDL_strcmp(source, "camera_screen") == 0)
    {
        if (!editor_camera_screen_trace_from_json(runtime, trace, out_trace))
            return false;
    }
    else
    {
        out_trace->start = json_vec3(trace, "start", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        out_trace->end = json_vec3(trace, "end", out_trace->start);
    }
    out_trace->contents_mask =
        brush_flags_from_json(obj_get(trace, "contents_mask"), brush_content_flag_from_string,
                              SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP);
    out_trace->model_filter = editor_model_filter_from_json(obj_get(trace, "model_filter"));
    return true;
}

static bool editor_selection_mode_is_click(yyjson_val *selection)
{
    const char *mode = json_string(selection, "mode", "hover");
    return SDL_strcmp(mode, "click") == 0;
}

static bool editor_selection_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selection_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selection_scene, active_scene) == 0;
}

static void clear_editor_command_preview(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    SDL_zero(runtime->editor_command_preview);
    runtime->editor_command_preview.face_index = -1;
}

static void clear_editor_active_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    init_editor_selection(&runtime->editor_active_selection);
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    clear_editor_command_preview(runtime);
}

static bool editor_selection_select_requested(const slayer3d_game_data_runtime *runtime, yyjson_val *selection)
{
    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return false;

    const Uint8 button = mouse_button_from_json(json_string(selection, "select_button", "LEFT"));
    return button != 0 && slayer3d_input_get_pressed_mouse_button(input) == button;
}

static bool editor_command_preview_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || !runtime->editor_command_preview.active)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return active_scene != NULL && runtime->editor_command_preview.scene != NULL &&
           SDL_strcmp(runtime->editor_command_preview.scene, active_scene) == 0;
}

static bool editor_command_name_valid(const char *command)
{
    return command != NULL && (SDL_strcmp(command, "translate") == 0 || SDL_strcmp(command, "paint") == 0 ||
                               SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0 ||
                               SDL_strcmp(command, "delete") == 0);
}

static bool editor_command_target_name_valid(const char *target)
{
    return target != NULL && (SDL_strcmp(target, "selection") == 0 || SDL_strcmp(target, "world") == 0 ||
                              SDL_strcmp(target, "element") == 0 || SDL_strcmp(target, "face") == 0 ||
                              SDL_strcmp(target, "material") == 0);
}

static bool resolve_editor_paint_material(slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_editor_selection *selection,
                                          const char *material_name, const char **out_material_name,
                                          int *out_material_index);

static const char *editor_selection_type_name(slayer3d_game_data_world_model_type type)
{
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL)
        return "sector_level";
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
        return "brush_world";
    return "none";
}

static const char *editor_metadata_stable_id(const slayer3d_game_data_editor_metadata *metadata)
{
    return metadata != NULL && metadata->stable_id != NULL ? metadata->stable_id : "";
}

static void editor_set_string_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name,
                                     const char *value)
{
    const char *key = json_string(outputs, key_name, NULL);
    if (props != NULL && key != NULL)
        slayer3d_properties_set_string(props, key, value != NULL ? value : "");
}

static void editor_set_bool_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, bool value)
{
    const char *key = json_string(outputs, key_name, NULL);
    if (props != NULL && key != NULL)
        slayer3d_properties_set_bool(props, key, value);
}

static void editor_set_int_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, int value)
{
    const char *key = json_string(outputs, key_name, NULL);
    if (props != NULL && key != NULL)
        slayer3d_properties_set_int(props, key, value);
}

static void editor_set_float_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, float value)
{
    const char *key = json_string(outputs, key_name, NULL);
    if (props != NULL && key != NULL)
        slayer3d_properties_set_float(props, key, value);
}

static void editor_set_vec3_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name,
                                   slayer3d_vec3 value)
{
    const char *key = json_string(outputs, key_name, NULL);
    if (props != NULL && key != NULL)
        slayer3d_properties_set_vec3(props, key, value);
}

static int editor_revision_to_int(Uint64 revision)
{
    return revision > (Uint64)SDL_MAX_SINT32 ? SDL_MAX_SINT32 : (int)revision;
}

static bool publish_editor_brush_world_status(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                                              const char *world_name, const char *message, bool publish_result)
{
    if (runtime == NULL || outputs == NULL)
        return true;

    slayer3d_game_data_brush_world_editor_state state;
    const bool ok = slayer3d_game_data_get_brush_world_editor_state(runtime, world_name, &state);
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    if (publish_result)
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", ok);
        editor_set_string_output(scene_state, outputs, "message_key",
                                 ok ? (message != NULL ? message : "brush world status published")
                                    : "brush world status unavailable");
    }
    editor_set_string_output(scene_state, outputs, "world_key", ok && state.world_name != NULL ? state.world_name : "");
    editor_set_string_output(scene_state, outputs, "source_path_key",
                             ok && state.source_path != NULL ? state.source_path : "");
    editor_set_bool_output(scene_state, outputs, "dirty_key", ok && state.dirty);
    editor_set_int_output(scene_state, outputs, "revision_key", ok ? editor_revision_to_int(state.revision) : 0);
    editor_set_int_output(scene_state, outputs, "saved_revision_key",
                          ok ? editor_revision_to_int(state.saved_revision) : 0);
    return ok;
}

static void publish_editor_player_start_state(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                                              const editor_player_start_runtime *start)
{
    if (runtime == NULL || outputs == NULL)
        return;
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    slayer3d_game_data_player_start_editor_state state;
    SDL_zero(state);
    (void)slayer3d_game_data_get_player_start_editor_state(runtime, &state);
    editor_set_string_output(scene_state, outputs, "player_start_key", start != NULL ? start->name : "");
    editor_set_string_output(scene_state, outputs, "scene_key",
                             start != NULL && start->scene != NULL ? start->scene : "");
    editor_set_string_output(scene_state, outputs, "target_key",
                             start != NULL && start->target != NULL ? start->target : "");
    editor_set_vec3_output(scene_state, outputs, "position_key",
                           start != NULL ? start->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_float_output(scene_state, outputs, "yaw_key", start != NULL ? start->yaw : 0.0f);
    editor_set_float_output(scene_state, outputs, "pitch_key", start != NULL ? start->pitch : 0.0f);
    editor_set_bool_output(scene_state, outputs, "dirty_key", state.dirty);
    editor_set_int_output(scene_state, outputs, "revision_key", editor_revision_to_int(state.revision));
    editor_set_int_output(scene_state, outputs, "saved_revision_key", editor_revision_to_int(state.saved_revision));
}

bool slayer3d_game_data_export_editor_brush_world_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    char error[256];
    error[0] = '\0';
    char *json = NULL;
    size_t size = 0u;
    const bool ok = slayer3d_game_data_export_brush_world_fragment_json(runtime, world_name, &json, &size, error,
                                                                        (int)sizeof(error));
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "brush world exported")
                                : (error[0] != '\0' ? error : "brush world export failed"));
    editor_set_int_output(scene_state, outputs, "size_key", ok ? (int)size : 0);
    editor_set_string_output(scene_state, outputs, "json_key", ok && json != NULL ? json : "");
    (void)publish_editor_brush_world_status(runtime, outputs, world_name, NULL, false);
    SDL_free(json);
    return true;
}

bool slayer3d_game_data_publish_editor_brush_world_status_action(slayer3d_game_data_runtime *runtime,
                                                                 yyjson_val *action)
{
    return publish_editor_brush_world_status(runtime, obj_get(action, "outputs"), json_string(action, "world", NULL),
                                             json_string(action, "message", "brush world status published"), true);
}

bool slayer3d_game_data_create_box_brush_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_game_data_create_box_brush_desc desc;
    SDL_zero(desc);
    desc.world_name = json_string(action, "world", NULL);
    desc.brush_name = json_string(action, "name", NULL);
    desc.material_name = json_string(action, "material", NULL);
    desc.min = json_vec3(action, "min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    desc.max = json_vec3(action, "max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));

    char error[256];
    error[0] = '\0';
    char brush_name[256];
    brush_name[0] = '\0';
    const bool ok =
        slayer3d_game_data_create_box_brush(runtime, &desc, brush_name, sizeof(brush_name), error, (int)sizeof(error));
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "box brush created")
                                : (error[0] != '\0' ? error : "box brush creation failed"));
    editor_set_string_output(scene_state, outputs, "brush_key", ok ? brush_name : "");
    (void)publish_editor_brush_world_status(runtime, outputs, desc.world_name, NULL, false);
    return true;
}

bool slayer3d_game_data_place_editor_player_start_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_game_data_place_player_start_desc desc;
    SDL_zero(desc);
    desc.name = json_string(action, "name", NULL);
    desc.scene = json_string(action, "scene", NULL);
    desc.target = json_string(action, "target", NULL);
    yyjson_val *position = obj_get(action, "position");
    desc.has_position = position != NULL;
    desc.position = json_vec3(action, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    yyjson_val *yaw = obj_get(action, "yaw");
    yyjson_val *pitch = obj_get(action, "pitch");
    desc.has_yaw = yaw != NULL;
    desc.yaw = json_float(action, "yaw", 0.0f);
    desc.has_pitch = pitch != NULL;
    desc.pitch = json_float(action, "pitch", 0.0f);
    desc.apply_to_target = json_bool(action, "apply_to_target", true);

    char error[256];
    error[0] = '\0';
    const bool ok = slayer3d_game_data_place_editor_player_start(runtime, &desc, error, (int)sizeof(error));
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "player start placed")
                                : (error[0] != '\0' ? error : "player start placement failed"));
    publish_editor_player_start_state(runtime, outputs, ok ? find_editor_player_start(runtime, desc.name) : NULL);
    return true;
}

static void publish_editor_selection(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                                     const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || outputs == NULL || selection == NULL)
        return;

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "hit_key", selection->hit);
    editor_set_string_output(scene_state, outputs, "type_key", editor_selection_type_name(selection->type));
    editor_set_string_output(scene_state, outputs, "world_key", selection->hit ? selection->world_name : "");
    editor_set_string_output(scene_state, outputs, "element_key", selection->hit ? selection->element_name : "");
    editor_set_string_output(scene_state, outputs, "material_key", selection->hit ? selection->material_name : "");
    editor_set_string_output(scene_state, outputs, "world_stable_id_key",
                             selection->hit ? editor_metadata_stable_id(selection->world_editor) : "");
    editor_set_string_output(scene_state, outputs, "element_stable_id_key",
                             selection->hit ? editor_metadata_stable_id(selection->element_editor) : "");
    editor_set_string_output(scene_state, outputs, "material_stable_id_key",
                             selection->hit ? editor_metadata_stable_id(selection->material_editor) : "");
    editor_set_string_output(scene_state, outputs, "face_stable_id_key",
                             selection->hit ? editor_metadata_stable_id(selection->face_editor) : "");
    editor_set_int_output(scene_state, outputs, "element_index_key", selection->hit ? selection->element_index : -1);
    editor_set_int_output(scene_state, outputs, "face_index_key", selection->hit ? selection->face_index : -1);
    editor_set_float_output(scene_state, outputs, "fraction_key", selection->hit ? selection->fraction : 1.0f);
    editor_set_vec3_output(scene_state, outputs, "point_key",
                           selection->hit ? selection->point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_vec3_output(scene_state, outputs, "normal_key",
                           selection->hit ? selection->normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

bool slayer3d_game_data_update_active_editor_tooling(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    yyjson_val *editor = active_editor_tooling_root(runtime);
    yyjson_val *selection_json = obj_get(editor, "selection");
    yyjson_val *outputs = obj_get(selection_json, "outputs");
    if (!yyjson_is_obj(selection_json))
    {
        clear_editor_active_selection(runtime);
        return true;
    }
    if (!editor_selection_active_for_scene(runtime))
        clear_editor_active_selection(runtime);

    slayer3d_game_data_world_trace_desc trace;
    slayer3d_game_data_editor_selection hover_selection;
    init_editor_selection(&hover_selection);
    if (!editor_trace_desc_from_json(runtime, selection_json, &trace) ||
        !slayer3d_game_data_pick_editor_world_model(runtime, &trace, &hover_selection))
    {
        return false;
    }

    if (!editor_selection_mode_is_click(selection_json))
    {
        runtime->editor_active_selection = hover_selection;
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
        publish_editor_selection(runtime, outputs, &hover_selection);
        return true;
    }

    publish_editor_selection(runtime, obj_get(selection_json, "hover_outputs"), &hover_selection);
    if (editor_selection_select_requested(runtime, selection_json))
    {
        if (hover_selection.hit)
            runtime->editor_active_selection = hover_selection;
        else if (json_bool(selection_json, "clear_on_miss", true))
            init_editor_selection(&runtime->editor_active_selection);
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    }

    publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
    return true;
}

bool slayer3d_game_data_get_active_editor_selection(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_editor_selection *out_selection)
{
    if (out_selection != NULL)
        init_editor_selection(out_selection);
    if (runtime == NULL || out_selection == NULL || !editor_selection_active_for_scene(runtime) ||
        !runtime->editor_active_selection.hit)
    {
        return false;
    }
    *out_selection = runtime->editor_active_selection;
    return true;
}

bool slayer3d_game_data_clear_active_editor_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    clear_editor_active_selection(runtime);
    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
    return true;
}

slayer3d_properties *slayer3d_game_data_create_editor_selection_payload(
    const slayer3d_game_data_editor_selection *selection)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    const bool hit = selection != NULL && selection->hit;
    slayer3d_properties_set_bool(payload, "selection_hit", hit);
    slayer3d_properties_set_string(payload, "selection_type",
                                   hit ? editor_selection_type_name(selection->type) : "none");
    slayer3d_properties_set_string(payload, "selection_world",
                                   hit && selection->world_name != NULL ? selection->world_name : "");
    slayer3d_properties_set_string(payload, "selection_element",
                                   hit && selection->element_name != NULL ? selection->element_name : "");
    slayer3d_properties_set_string(payload, "selection_material",
                                   hit && selection->material_name != NULL ? selection->material_name : "");
    slayer3d_properties_set_string(payload, "selection_world_stable_id",
                                   hit ? editor_metadata_stable_id(selection->world_editor) : "");
    slayer3d_properties_set_string(payload, "selection_element_stable_id",
                                   hit ? editor_metadata_stable_id(selection->element_editor) : "");
    slayer3d_properties_set_string(payload, "selection_material_stable_id",
                                   hit ? editor_metadata_stable_id(selection->material_editor) : "");
    slayer3d_properties_set_string(payload, "selection_face_stable_id",
                                   hit ? editor_metadata_stable_id(selection->face_editor) : "");
    slayer3d_properties_set_int(payload, "selection_element_index", hit ? selection->element_index : -1);
    slayer3d_properties_set_int(payload, "selection_face_index", hit ? selection->face_index : -1);
    slayer3d_properties_set_float(payload, "selection_fraction", hit ? selection->fraction : 1.0f);
    slayer3d_properties_set_vec3(payload, "selection_world_position",
                                 hit ? selection->world_position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(payload, "selection_point",
                                 hit ? selection->point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(payload, "selection_normal",
                                 hit ? selection->normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return payload;
}

static bool editor_command_target_compatible(const slayer3d_game_data_editor_selection *selection, const char *command,
                                             const char *target, const char **out_reason)
{
    if (out_reason != NULL)
        *out_reason = NULL;
    if (selection == NULL || !selection->hit)
    {
        if (out_reason != NULL)
            *out_reason = "no active selection";
        return false;
    }
    if (SDL_strcmp(target, "world") == 0 && (selection->world_name == NULL || selection->world_name[0] == '\0'))
    {
        if (out_reason != NULL)
            *out_reason = "selection has no world target";
        return false;
    }
    if (SDL_strcmp(target, "element") == 0 && (selection->element_name == NULL || selection->element_name[0] == '\0'))
    {
        if (out_reason != NULL)
            *out_reason = "selection has no element target";
        return false;
    }
    if (SDL_strcmp(target, "face") == 0 && selection->face_index < 0)
    {
        if (out_reason != NULL)
            *out_reason = "selection has no face target";
        return false;
    }
    if (SDL_strcmp(target, "material") == 0 &&
        (selection->material_name == NULL || selection->material_name[0] == '\0'))
    {
        if (out_reason != NULL)
            *out_reason = "selection has no material target";
        return false;
    }
    if ((SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0) && SDL_strcmp(target, "face") != 0)
    {
        if (out_reason != NULL)
            *out_reason = "resize/extrude target must be face";
        return false;
    }
    if ((SDL_strcmp(command, "translate") == 0 || SDL_strcmp(command, "delete") == 0) && !selection->has_bounds)
    {
        if (out_reason != NULL)
            *out_reason = "selection has no previewable bounds";
        return false;
    }
    if ((SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0) && selection->face_index < 0)
    {
        if (out_reason != NULL)
            *out_reason = "resize/extrude preview requires a face selection";
        return false;
    }
    if (SDL_strcmp(command, "paint") == 0 && selection->face_index < 0 &&
        (selection->material_name == NULL || selection->material_name[0] == '\0'))
    {
        if (out_reason != NULL)
            *out_reason = "paint preview requires a face or material selection";
        return false;
    }
    return true;
}

static slayer3d_vec3 editor_command_preview_offset(yyjson_val *action,
                                                   const slayer3d_game_data_editor_selection *selection)
{
    yyjson_val *offset = obj_get(action, "offset");
    if (offset != NULL)
        return json_vec3(action, "offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    const float distance = json_float(action, "distance", 0.0f);
    if (distance != 0.0f && selection != NULL && slayer3d_vec3_length_squared(selection->normal) > 0.000001f)
        return slayer3d_vec3_scale(slayer3d_vec3_normalize(selection->normal), distance);
    return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static float editor_command_face_distance_delta(const char *command, slayer3d_vec3 offset,
                                                const slayer3d_game_data_editor_selection *selection)
{
    if (command == NULL || selection == NULL ||
        (SDL_strcmp(command, "resize") != 0 && SDL_strcmp(command, "extrude") != 0) ||
        slayer3d_vec3_length_squared(selection->normal) <= 0.000001f)
    {
        return 0.0f;
    }
    return slayer3d_vec3_dot(slayer3d_vec3_normalize(selection->normal), offset);
}

static slayer3d_bounding_box editor_resized_preview_bounds(slayer3d_bounding_box bounds, slayer3d_vec3 normal,
                                                           float distance)
{
    normal = slayer3d_vec3_normalize(normal);
    const float ax = SDL_fabsf(normal.x);
    const float ay = SDL_fabsf(normal.y);
    const float az = SDL_fabsf(normal.z);
    if (ax >= ay && ax >= az)
    {
        if (normal.x >= 0.0f)
            bounds.max.x += distance;
        else
            bounds.min.x -= distance;
    }
    else if (ay >= ax && ay >= az)
    {
        if (normal.y >= 0.0f)
            bounds.max.y += distance;
        else
            bounds.min.y -= distance;
    }
    else
    {
        if (normal.z >= 0.0f)
            bounds.max.z += distance;
        else
            bounds.min.z -= distance;
    }
    return bounds;
}

static void publish_editor_command_preview(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, bool valid,
                                           const char *command, const char *target, const char *message,
                                           const slayer3d_game_data_editor_selection *selection,
                                           const slayer3d_bounding_box *bounds)
{
    if (runtime == NULL || outputs == NULL)
        return;

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "active_key", valid);
    editor_set_bool_output(scene_state, outputs, "valid_key", valid);
    editor_set_string_output(scene_state, outputs, "command_key", command != NULL ? command : "");
    editor_set_string_output(scene_state, outputs, "target_key", target != NULL ? target : "");
    editor_set_string_output(scene_state, outputs, "message_key", message != NULL ? message : "");
    editor_set_string_output(scene_state, outputs, "world_key",
                             valid && selection != NULL && selection->world_name != NULL ? selection->world_name : "");
    editor_set_string_output(scene_state, outputs, "element_key",
                             valid && selection != NULL && selection->element_name != NULL ? selection->element_name
                                                                                           : "");
    editor_set_int_output(scene_state, outputs, "face_index_key",
                          valid && selection != NULL ? selection->face_index : -1);
    editor_set_vec3_output(scene_state, outputs, "bounds_min_key",
                           valid && bounds != NULL ? bounds->min : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_vec3_output(scene_state, outputs, "bounds_max_key",
                           valid && bounds != NULL ? bounds->max : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

bool slayer3d_game_data_preview_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *command = json_string(action, "command", NULL);
    const char *target = json_string(action, "target", "selection");
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL || !editor_command_name_valid(command) || !editor_command_target_name_valid(target))
        return false;

    slayer3d_game_data_editor_selection selection;
    const bool has_selection = slayer3d_game_data_get_active_editor_selection(runtime, &selection);
    const char *reason = NULL;
    const bool valid = has_selection && editor_command_target_compatible(&selection, command, target, &reason);
    if (!valid)
    {
        clear_editor_command_preview(runtime);
        publish_editor_command_preview(runtime, outputs, false, command, target, reason != NULL ? reason : "no preview",
                                       NULL, NULL);
        return true;
    }

    const slayer3d_vec3 offset = editor_command_preview_offset(action, &selection);
    const char *paint_material_name = NULL;
    int paint_material_index = -1;
    if (SDL_strcmp(command, "paint") == 0)
    {
        const char *authored_material = json_string(action, "material", NULL);
        if (!resolve_editor_paint_material(runtime, &selection, authored_material, &paint_material_name,
                                           &paint_material_index))
        {
            clear_editor_command_preview(runtime);
            publish_editor_command_preview(runtime, outputs, false, command, target, "paint preview material not found",
                                           NULL, NULL);
            return true;
        }
    }

    const bool face_resize = SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0;
    const float face_distance = editor_command_face_distance_delta(command, offset, &selection);
    slayer3d_bounding_box bounds =
        selection.has_bounds
            ? (face_resize ? editor_resized_preview_bounds(selection.bounds, selection.normal, face_distance)
                           : translated_bounds(selection.bounds, offset))
            : (slayer3d_bounding_box){selection.point, selection.point};

    editor_command_preview_state *preview = &runtime->editor_command_preview;
    SDL_zero(*preview);
    preview->active = true;
    preview->scene = slayer3d_game_data_active_scene(runtime);
    preview->command = command;
    preview->target = target;
    preview->world_name = selection.world_name;
    preview->element_name = selection.element_name;
    preview->material_name = paint_material_name != NULL ? paint_material_name : selection.material_name;
    preview->previous_material_name = selection.material_name;
    preview->face_index = selection.face_index;
    preview->material_index = paint_material_index;
    preview->previous_material_index = -1;
    if (selection.world_name != NULL && selection.material_name != NULL)
    {
        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection.world_name);
        preview->previous_material_index = find_editor_brush_material_index(world_runtime, selection.material_name);
    }
    preview->outputs = outputs;
    preview->offset = offset;
    preview->has_bounds = true;
    preview->bounds = bounds;

    slayer3d_properties *payload = slayer3d_game_data_create_editor_selection_payload(&selection);
    char message[256];
    bool formatted = false;
    if (payload != NULL)
    {
        slayer3d_properties_set_string(payload, "editor_command", command);
        slayer3d_properties_set_string(payload, "editor_command_target", target);
        formatted = format_payload_string(payload, json_string(action, "message", "preview"), message, sizeof(message));
        slayer3d_properties_destroy(payload);
    }
    publish_editor_command_preview(runtime, outputs, true, command, target,
                                   formatted ? message : json_string(action, "message", "preview"), &selection,
                                   &bounds);
    return true;
}

bool slayer3d_game_data_clear_editor_command_preview(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL)
        return false;
    clear_editor_command_preview(runtime);
    publish_editor_command_preview(runtime, obj_get(action, "outputs"), false, "", "",
                                   json_string(action, "message", "preview cleared"), NULL, NULL);
    return true;
}

static int editor_command_history_undo_count(const editor_command_history_state *history)
{
    return history != NULL ? history->cursor : 0;
}

static int editor_command_history_redo_count(const editor_command_history_state *history)
{
    return history != NULL ? history->count - history->cursor : 0;
}

static slayer3d_properties *create_editor_transaction_payload(const slayer3d_game_data_runtime *runtime,
                                                              const char *event, bool valid,
                                                              const editor_command_transaction_entry *entry,
                                                              const char *message)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    const editor_command_history_state *history = runtime != NULL ? &runtime->editor_command_history : NULL;
    slayer3d_properties_set_bool(payload, "editor_transaction_valid", valid);
    slayer3d_properties_set_string(payload, "editor_transaction_event", event != NULL ? event : "");
    slayer3d_properties_set_string(payload, "editor_transaction_message", message != NULL ? message : "");
    slayer3d_properties_set_int(payload, "editor_transaction_undo_count", editor_command_history_undo_count(history));
    slayer3d_properties_set_int(payload, "editor_transaction_redo_count", editor_command_history_redo_count(history));

    if (entry != NULL)
    {
        char id_text[32];
        SDL_snprintf(id_text, sizeof(id_text), "%d", entry->id);
        slayer3d_properties_set_int(payload, "editor_transaction_id", entry->id);
        slayer3d_properties_set_string(payload, "editor_transaction_id_text", id_text);
        slayer3d_properties_set_string(payload, "editor_command", entry->command != NULL ? entry->command : "");
        slayer3d_properties_set_string(payload, "editor_command_target", entry->target != NULL ? entry->target : "");
        slayer3d_properties_set_string(payload, "editor_transaction_scene", entry->scene != NULL ? entry->scene : "");
        slayer3d_properties_set_string(payload, "editor_transaction_world",
                                       entry->world_name != NULL ? entry->world_name : "");
        slayer3d_properties_set_string(payload, "editor_transaction_element",
                                       entry->element_name != NULL ? entry->element_name : "");
        slayer3d_properties_set_string(payload, "editor_transaction_material",
                                       entry->material_name != NULL ? entry->material_name : "");
        slayer3d_properties_set_string(payload, "editor_transaction_previous_material",
                                       entry->previous_material_name != NULL ? entry->previous_material_name : "");
        slayer3d_properties_set_int(payload, "editor_transaction_face_index", entry->face_index);
        slayer3d_properties_set_vec3(payload, "editor_transaction_offset", entry->offset);
        slayer3d_properties_set_vec3(payload, "editor_transaction_bounds_min",
                                     entry->has_bounds ? entry->bounds.min : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(payload, "editor_transaction_bounds_max",
                                     entry->has_bounds ? entry->bounds.max : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    }
    else
    {
        slayer3d_properties_set_int(payload, "editor_transaction_id", -1);
        slayer3d_properties_set_string(payload, "editor_transaction_id_text", "");
        slayer3d_properties_set_string(payload, "editor_command", "");
        slayer3d_properties_set_string(payload, "editor_command_target", "");
        slayer3d_properties_set_string(payload, "editor_transaction_scene", "");
        slayer3d_properties_set_string(payload, "editor_transaction_world", "");
        slayer3d_properties_set_string(payload, "editor_transaction_element", "");
        slayer3d_properties_set_string(payload, "editor_transaction_material", "");
        slayer3d_properties_set_string(payload, "editor_transaction_previous_material", "");
        slayer3d_properties_set_int(payload, "editor_transaction_face_index", -1);
        slayer3d_properties_set_vec3(payload, "editor_transaction_offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(payload, "editor_transaction_bounds_min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(payload, "editor_transaction_bounds_max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    }
    return payload;
}

static void publish_editor_transaction(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, const char *event,
                                       bool valid, const editor_command_transaction_entry *entry, const char *message)
{
    if (runtime == NULL || outputs == NULL)
        return;

    const editor_command_history_state *history = &runtime->editor_command_history;
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", valid);
    editor_set_string_output(scene_state, outputs, "event_key", event != NULL ? event : "");
    editor_set_string_output(scene_state, outputs, "message_key", message != NULL ? message : "");
    editor_set_int_output(scene_state, outputs, "transaction_id_key", valid && entry != NULL ? entry->id : -1);
    editor_set_int_output(scene_state, outputs, "undo_count_key", editor_command_history_undo_count(history));
    editor_set_int_output(scene_state, outputs, "redo_count_key", editor_command_history_redo_count(history));
    editor_set_string_output(scene_state, outputs, "command_key",
                             valid && entry != NULL && entry->command != NULL ? entry->command : "");
    editor_set_string_output(scene_state, outputs, "target_key",
                             valid && entry != NULL && entry->target != NULL ? entry->target : "");
    editor_set_string_output(scene_state, outputs, "world_key",
                             valid && entry != NULL && entry->world_name != NULL ? entry->world_name : "");
    editor_set_string_output(scene_state, outputs, "element_key",
                             valid && entry != NULL && entry->element_name != NULL ? entry->element_name : "");
    editor_set_int_output(scene_state, outputs, "face_index_key", valid && entry != NULL ? entry->face_index : -1);
    editor_set_vec3_output(scene_state, outputs, "bounds_min_key",
                           valid && entry != NULL && entry->has_bounds ? entry->bounds.min
                                                                       : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_vec3_output(scene_state, outputs, "bounds_max_key",
                           valid && entry != NULL && entry->has_bounds ? entry->bounds.max
                                                                       : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    (void)publish_editor_brush_world_status(runtime, outputs, valid && entry != NULL ? entry->world_name : NULL, NULL,
                                            false);
}

static bool run_editor_transaction_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                                const char *event, bool valid,
                                                const editor_command_transaction_entry *entry, const char *message)
{
    if (actions == NULL)
        return true;
    slayer3d_properties *payload = create_editor_transaction_payload(runtime, event, valid, entry, message);
    if (payload == NULL)
        return false;
    const bool ok = execute_optional_action_array(runtime, actions, payload);
    slayer3d_properties_destroy(payload);
    return ok;
}

static void format_editor_transaction_message(const slayer3d_game_data_runtime *runtime, const char *event, bool valid,
                                              const editor_command_transaction_entry *entry, const char *format,
                                              char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    SDL_strlcpy(buffer, format != NULL ? format : "", buffer_size);
    slayer3d_properties *payload = create_editor_transaction_payload(runtime, event, valid, entry, buffer);
    if (payload != NULL)
    {
        (void)format_payload_string(payload, format != NULL ? format : "", buffer, buffer_size);
        slayer3d_properties_destroy(payload);
    }
}

static editor_command_transaction_entry *editor_command_history_append(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return NULL;
    editor_command_history_state *history = &runtime->editor_command_history;
    if (history->cursor < history->count)
        history->count = history->cursor;
    if (history->count >= SLAYER3D_EDITOR_COMMAND_HISTORY_CAPACITY)
    {
        SDL_memmove(&history->entries[0], &history->entries[1],
                    sizeof(history->entries[0]) * (SLAYER3D_EDITOR_COMMAND_HISTORY_CAPACITY - 1));
        history->count = SLAYER3D_EDITOR_COMMAND_HISTORY_CAPACITY - 1;
        history->cursor = history->count;
    }

    editor_command_transaction_entry *entry = &history->entries[history->count];
    SDL_zero(*entry);
    entry->id = ++history->next_id;
    entry->face_index = -1;
    entry->material_index = -1;
    entry->previous_material_index = -1;
    history->count++;
    history->cursor = history->count;
    return entry;
}

static slayer3d_game_data_brush *find_editor_mutable_brush(brush_world_runtime *world_runtime, const char *brush_name)
{
    if (world_runtime == NULL || brush_name == NULL || brush_name[0] == '\0')
        return NULL;
    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->brush_count; ++i)
    {
        slayer3d_game_data_brush *brush = (slayer3d_game_data_brush *)&world->brushes[i];
        if (brush->name != NULL && SDL_strcmp(brush->name, brush_name) == 0)
            return brush;
    }
    return NULL;
}

static int find_editor_brush_material_index(const brush_world_runtime *world_runtime, const char *material_name)
{
    if (world_runtime == NULL || material_name == NULL || material_name[0] == '\0')
        return -1;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->material_count; ++i)
    {
        if (world->materials[i].name != NULL && SDL_strcmp(world->materials[i].name, material_name) == 0)
            return i;
    }
    return -1;
}

static bool rebuild_editor_brush_world(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return false;
    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (!slayer3d_game_data_brush_world_build_acceleration(world))
        return false;

    slayer3d_model render_model;
    SDL_zero(render_model);
    if (!slayer3d_game_data_brush_world_compile_render_model(world, &render_model))
    {
        slayer3d_free_model(&render_model);
        return false;
    }

    slayer3d_free_model(&world_runtime->render_model);
    world_runtime->render_model = render_model;
    world->render_model = &world_runtime->render_model;
    return true;
}

static bool editor_brush_bounds_valid(const slayer3d_game_data_brush *brush)
{
    if (brush == NULL || !brush->has_bounds)
        return false;
    const float epsilon = 0.0001f;
    return brush->bounds.max.x - brush->bounds.min.x > epsilon && brush->bounds.max.y - brush->bounds.min.y > epsilon &&
           brush->bounds.max.z - brush->bounds.min.z > epsilon;
}

static bool resize_editor_brush_face_plane(slayer3d_game_data_brush *brush, int face_index, float distance)
{
    if (brush == NULL || face_index < 0 || face_index >= brush->face_count)
        return false;
    slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[face_index];
    face->distance += distance;
    return true;
}

bool slayer3d_game_data_resize_brush_face(slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_resize_brush_face_desc *desc, char *error_buffer,
                                          int error_buffer_size)
{
    if (runtime == NULL || desc == NULL || desc->world_name == NULL || desc->world_name[0] == '\0' ||
        desc->brush_name == NULL || desc->brush_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "invalid brush face resize request");
        return false;
    }
    if (SDL_fabsf(desc->distance) <= 0.0000001f)
        return true;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, desc->world_name);
    slayer3d_game_data_brush *brush = find_editor_mutable_brush(world_runtime, desc->brush_name);
    if (brush == NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush not found");
        return false;
    }
    if (desc->face_index < 0 || desc->face_index >= brush->face_count)
    {
        set_error(error_buffer, error_buffer_size, "brush face index out of range");
        return false;
    }

    if (!resize_editor_brush_face_plane(brush, desc->face_index, desc->distance))
    {
        set_error(error_buffer, error_buffer_size, "failed to resize brush face");
        return false;
    }

    if (rebuild_editor_brush_world(world_runtime) && editor_brush_bounds_valid(brush))
    {
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    (void)resize_editor_brush_face_plane(brush, desc->face_index, -desc->distance);
    (void)rebuild_editor_brush_world(world_runtime);
    set_error(error_buffer, error_buffer_size, "brush face resize would create invalid geometry");
    return false;
}

static bool resolve_editor_paint_material(slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_editor_selection *selection,
                                          const char *material_name, const char **out_material_name,
                                          int *out_material_index)
{
    if (out_material_name != NULL)
        *out_material_name = NULL;
    if (out_material_index != NULL)
        *out_material_index = -1;
    if (runtime == NULL || selection == NULL || selection->world_name == NULL || material_name == NULL ||
        material_name[0] == '\0')
    {
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    const int material_index = find_editor_brush_material_index(world_runtime, material_name);
    if (material_index < 0)
        return false;
    if (out_material_name != NULL)
        *out_material_name = world_runtime->desc.materials[material_index].name;
    if (out_material_index != NULL)
        *out_material_index = material_index;
    return true;
}

static void translate_editor_brush_planes(slayer3d_game_data_brush *brush, slayer3d_vec3 offset)
{
    if (brush == NULL)
        return;
    for (int i = 0; i < brush->face_count; ++i)
    {
        slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[i];
        face->distance += slayer3d_vec3_dot(face->normal, offset);
    }
}

static bool apply_editor_brush_translate(slayer3d_game_data_runtime *runtime,
                                         const editor_command_transaction_entry *entry, slayer3d_vec3 offset)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL)
        return false;
    if (slayer3d_vec3_length_squared(offset) <= 0.0000001f)
        return true;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    slayer3d_game_data_brush *brush = find_editor_mutable_brush(world_runtime, entry->element_name);
    if (brush == NULL)
        return false;

    translate_editor_brush_planes(brush, offset);
    if (rebuild_editor_brush_world(world_runtime))
    {
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    translate_editor_brush_planes(brush, slayer3d_vec3_scale(offset, -1.0f));
    (void)rebuild_editor_brush_world(world_runtime);
    return false;
}

static bool set_editor_brush_face_material(brush_world_runtime *world_runtime, slayer3d_game_data_brush *brush,
                                           int face_index, int material_index)
{
    if (world_runtime == NULL || brush == NULL || face_index < 0 || face_index >= brush->face_count ||
        material_index < 0 || material_index >= world_runtime->desc.material_count)
    {
        return false;
    }
    slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[face_index];
    face->material_index = material_index;
    face->material_name = world_runtime->desc.materials[material_index].name;
    return true;
}

static bool apply_editor_brush_paint(slayer3d_game_data_runtime *runtime, const editor_command_transaction_entry *entry,
                                     bool forward)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL ||
        entry->face_index < 0)
    {
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    slayer3d_game_data_brush *brush = find_editor_mutable_brush(world_runtime, entry->element_name);
    const int material_index = forward ? entry->material_index : entry->previous_material_index;
    const int rollback_index = forward ? entry->previous_material_index : entry->material_index;
    if (!set_editor_brush_face_material(world_runtime, brush, entry->face_index, material_index))
        return false;
    if (rebuild_editor_brush_world(world_runtime))
    {
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    (void)set_editor_brush_face_material(world_runtime, brush, entry->face_index, rollback_index);
    (void)rebuild_editor_brush_world(world_runtime);
    return false;
}

static bool apply_editor_brush_face_resize(slayer3d_game_data_runtime *runtime,
                                           const editor_command_transaction_entry *entry, bool forward)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL ||
        entry->face_index < 0)
    {
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    slayer3d_game_data_brush *brush = find_editor_mutable_brush(world_runtime, entry->element_name);
    if (brush == NULL || entry->face_index >= brush->face_count)
        return false;

    const slayer3d_game_data_brush_face *face = &brush->faces[entry->face_index];
    const float distance = slayer3d_vec3_dot(slayer3d_vec3_normalize(face->normal),
                                             forward ? entry->offset : slayer3d_vec3_scale(entry->offset, -1.0f));
    slayer3d_game_data_resize_brush_face_desc desc;
    SDL_zero(desc);
    desc.world_name = entry->world_name;
    desc.brush_name = entry->element_name;
    desc.face_index = entry->face_index;
    desc.distance = distance;
    return slayer3d_game_data_resize_brush_face(runtime, &desc, NULL, 0);
}

static void translate_active_editor_selection_for_transaction(slayer3d_game_data_runtime *runtime,
                                                              const editor_command_transaction_entry *entry,
                                                              slayer3d_vec3 offset)
{
    if (runtime == NULL || entry == NULL || !runtime->editor_active_selection.hit)
        return;
    slayer3d_game_data_editor_selection *selection = &runtime->editor_active_selection;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || entry->scene == NULL || SDL_strcmp(active_scene, entry->scene) != 0 ||
        selection->world_name == NULL || entry->world_name == NULL ||
        SDL_strcmp(selection->world_name, entry->world_name) != 0 || selection->element_name == NULL ||
        entry->element_name == NULL || SDL_strcmp(selection->element_name, entry->element_name) != 0)
    {
        return;
    }

    selection->point = slayer3d_vec3_add(selection->point, offset);
    selection->world_position = slayer3d_vec3_add(selection->world_position, offset);
    if (selection->has_bounds)
        selection->bounds = translated_bounds(selection->bounds, offset);

    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), selection);
}

static void resize_active_editor_selection_for_transaction(slayer3d_game_data_runtime *runtime,
                                                           const editor_command_transaction_entry *entry, bool forward)
{
    if (runtime == NULL || entry == NULL || !runtime->editor_active_selection.hit)
        return;
    slayer3d_game_data_editor_selection *selection = &runtime->editor_active_selection;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || entry->scene == NULL || SDL_strcmp(active_scene, entry->scene) != 0 ||
        selection->world_name == NULL || entry->world_name == NULL ||
        SDL_strcmp(selection->world_name, entry->world_name) != 0 || selection->element_name == NULL ||
        entry->element_name == NULL || SDL_strcmp(selection->element_name, entry->element_name) != 0 ||
        selection->face_index != entry->face_index)
    {
        return;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    const slayer3d_game_data_brush *brush = NULL;
    if (world_runtime != NULL)
    {
        const slayer3d_game_data_brush_world *world = &world_runtime->desc;
        for (int i = 0; i < world->brush_count; ++i)
        {
            if (world->brushes[i].name != NULL && SDL_strcmp(world->brushes[i].name, entry->element_name) == 0)
            {
                brush = &world->brushes[i];
                break;
            }
        }
    }

    const slayer3d_vec3 offset = forward ? entry->offset : slayer3d_vec3_scale(entry->offset, -1.0f);
    selection->point = slayer3d_vec3_add(selection->point, offset);
    selection->world_position = slayer3d_vec3_add(selection->world_position, offset);
    if (brush != NULL && brush->has_bounds)
        selection->bounds = brush->bounds;
    else if (selection->has_bounds)
        selection->bounds = editor_resized_preview_bounds(selection->bounds, selection->normal,
                                                          slayer3d_vec3_dot(selection->normal, offset));
    selection->has_bounds = brush != NULL ? brush->has_bounds : selection->has_bounds;

    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), selection);
}

static void update_active_editor_selection_material_for_transaction(slayer3d_game_data_runtime *runtime,
                                                                    const editor_command_transaction_entry *entry,
                                                                    bool forward)
{
    if (runtime == NULL || entry == NULL || !runtime->editor_active_selection.hit)
        return;
    slayer3d_game_data_editor_selection *selection = &runtime->editor_active_selection;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || entry->scene == NULL || SDL_strcmp(active_scene, entry->scene) != 0 ||
        selection->world_name == NULL || entry->world_name == NULL ||
        SDL_strcmp(selection->world_name, entry->world_name) != 0 || selection->element_name == NULL ||
        entry->element_name == NULL || SDL_strcmp(selection->element_name, entry->element_name) != 0 ||
        selection->face_index != entry->face_index)
    {
        return;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    const int material_index = forward ? entry->material_index : entry->previous_material_index;
    selection->material_name = forward ? entry->material_name : entry->previous_material_name;
    selection->material_editor =
        world_runtime != NULL && material_index >= 0 && material_index < world_runtime->desc.material_count
            ? &world_runtime->desc.materials[material_index].editor
            : NULL;

    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), selection);
}

static bool editor_transaction_has_brush_mutation(const editor_command_transaction_entry *entry)
{
    if (entry == NULL || entry->command == NULL || entry->target == NULL || entry->world_name == NULL ||
        entry->world_name[0] == '\0' || entry->element_name == NULL || entry->element_name[0] == '\0')
    {
        return false;
    }
    return (SDL_strcmp(entry->command, "translate") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "paint") == 0 && SDL_strcmp(entry->target, "face") == 0) ||
           ((SDL_strcmp(entry->command, "resize") == 0 || SDL_strcmp(entry->command, "extrude") == 0) &&
            SDL_strcmp(entry->target, "face") == 0);
}

static bool apply_editor_transaction_mutation(slayer3d_game_data_runtime *runtime,
                                              const editor_command_transaction_entry *entry, bool forward)
{
    if (!editor_transaction_has_brush_mutation(entry))
        return true;
    if (SDL_strcmp(entry->command, "paint") == 0)
    {
        if (!apply_editor_brush_paint(runtime, entry, forward))
            return false;
        update_active_editor_selection_material_for_transaction(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "resize") == 0 || SDL_strcmp(entry->command, "extrude") == 0)
    {
        if (!apply_editor_brush_face_resize(runtime, entry, forward))
            return false;
        resize_active_editor_selection_for_transaction(runtime, entry, forward);
        return true;
    }

    const slayer3d_vec3 offset = forward ? entry->offset : slayer3d_vec3_scale(entry->offset, -1.0f);
    if (!apply_editor_brush_translate(runtime, entry, offset))
        return false;
    translate_active_editor_selection_for_transaction(runtime, entry, offset);
    return true;
}

bool slayer3d_game_data_commit_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              const slayer3d_properties *payload)
{
    (void)payload;
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL)
        return false;

    if (!editor_command_preview_active_for_scene(runtime))
    {
        const char *message = json_string(action, "invalid_message", "no active editor command preview");
        publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, message);
    }

    const editor_command_preview_state *preview = &runtime->editor_command_preview;
    editor_command_transaction_entry *entry = editor_command_history_append(runtime);
    if (entry == NULL)
        return false;

    entry->scene = preview->scene;
    entry->command = preview->command;
    entry->target = preview->target;
    entry->world_name = preview->world_name;
    entry->element_name = preview->element_name;
    entry->material_name = preview->material_name;
    entry->previous_material_name = preview->previous_material_name;
    entry->face_index = preview->face_index;
    entry->material_index = preview->material_index;
    entry->previous_material_index = preview->previous_material_index;
    entry->offset = preview->offset;
    entry->has_bounds = preview->has_bounds;
    entry->bounds = preview->bounds;
    format_editor_transaction_message(runtime, "commit", true, entry,
                                      json_string(action, "message", "committed {editor_command}"), entry->message,
                                      sizeof(entry->message));

    if (!apply_editor_transaction_mutation(runtime, entry, true))
    {
        editor_command_history_state *history = &runtime->editor_command_history;
        history->count--;
        history->cursor = history->count;
        const char *message = json_string(action, "invalid_message", "editor command mutation failed");
        publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, message);
    }

    publish_editor_command_preview(runtime, preview->outputs, false, "", "", "preview committed", NULL, NULL);
    clear_editor_command_preview(runtime);
    publish_editor_transaction(runtime, outputs, "commit", true, entry, entry->message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "commit", true, entry,
                                               entry->message);
}

bool slayer3d_game_data_undo_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload)
{
    (void)payload;
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL)
        return false;
    editor_command_history_state *history = &runtime->editor_command_history;
    if (history->cursor <= 0)
    {
        const char *message = json_string(action, "invalid_message", "nothing to undo");
        publish_editor_transaction(runtime, outputs, "undo", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "undo", false, NULL, message);
    }

    editor_command_transaction_entry *entry = &history->entries[history->cursor - 1];
    if (!apply_editor_transaction_mutation(runtime, entry, false))
    {
        const char *message = json_string(action, "invalid_message", "editor command undo failed");
        publish_editor_transaction(runtime, outputs, "undo", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "undo", false, NULL, message);
    }
    history->cursor--;
    char message[128];
    format_editor_transaction_message(runtime, "undo", true, entry,
                                      json_string(action, "message", "undo {editor_command}"), message,
                                      sizeof(message));
    publish_editor_transaction(runtime, outputs, "undo", true, entry, message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "undo", true, entry, message);
}

bool slayer3d_game_data_redo_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload)
{
    (void)payload;
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL)
        return false;
    editor_command_history_state *history = &runtime->editor_command_history;
    if (history->cursor >= history->count)
    {
        const char *message = json_string(action, "invalid_message", "nothing to redo");
        publish_editor_transaction(runtime, outputs, "redo", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "redo", false, NULL, message);
    }

    editor_command_transaction_entry *entry = &history->entries[history->cursor];
    if (!apply_editor_transaction_mutation(runtime, entry, true))
    {
        const char *message = json_string(action, "invalid_message", "editor command redo failed");
        publish_editor_transaction(runtime, outputs, "redo", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "redo", false, NULL, message);
    }
    history->cursor++;
    char message[128];
    format_editor_transaction_message(runtime, "redo", true, entry,
                                      json_string(action, "message", "redo {editor_command}"), message,
                                      sizeof(message));
    publish_editor_transaction(runtime, outputs, "redo", true, entry, message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "redo", true, entry, message);
}

static bool active_editor_debug_desc_from_json(const slayer3d_game_data_runtime *runtime,
                                               slayer3d_game_data_editor_debug_desc *out_desc,
                                               slayer3d_game_data_world_trace_desc *out_trace,
                                               slayer3d_game_data_editor_selection *out_selection)
{
    if (runtime == NULL)
        return false;
    yyjson_val *editor = active_editor_tooling_root(runtime);
    yyjson_val *overlay = obj_get(editor, "debug_overlay");
    if (!yyjson_is_obj(overlay) || !json_bool(overlay, "enabled", true) || out_desc == NULL)
        return false;

    SDL_zero(*out_desc);
    out_desc->flags = editor_debug_flags_from_json(obj_get(overlay, "flags"));
    out_desc->world_bounds_color = json_color(overlay, "world_bounds_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->selection_bounds_color = json_color(overlay, "selection_bounds_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->trace_color = json_color(overlay, "trace_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->face_normal_color = json_color(overlay, "face_normal_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->hit_marker_color = json_color(overlay, "hit_marker_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->command_preview_color = json_color(overlay, "command_preview_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->normal_length = json_float(overlay, "normal_length", 0.75f);
    out_desc->hit_marker_size = json_float(overlay, "hit_marker_size", 0.1f);

    yyjson_val *selection_json = obj_get(editor, "selection");
    if (editor_trace_desc_from_json(runtime, selection_json, out_trace))
    {
        out_desc->trace = out_trace;
        if (out_selection != NULL && editor_selection_mode_is_click(selection_json) &&
            editor_selection_active_for_scene(runtime) && runtime->editor_active_selection.hit)
        {
            *out_selection = runtime->editor_active_selection;
            out_desc->selection = out_selection;
        }
        else if (out_selection != NULL && slayer3d_game_data_pick_editor_world_model(runtime, out_trace, out_selection))
        {
            out_desc->selection = out_selection;
        }
    }
    return true;
}

bool slayer3d_game_data_for_each_active_editor_debug_primitive(const slayer3d_game_data_runtime *runtime,
                                                               slayer3d_game_data_editor_debug_primitive_fn callback,
                                                               void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    slayer3d_game_data_editor_debug_desc desc;
    slayer3d_game_data_world_trace_desc trace;
    slayer3d_game_data_editor_selection selection;
    if (!active_editor_debug_desc_from_json(runtime, &desc, &trace, &selection))
        return true;
    return slayer3d_game_data_for_each_editor_debug_primitive(runtime, &desc, callback, userdata);
}

bool slayer3d_game_data_query_world_model_point(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 point,
                                                unsigned int model_filter, unsigned int brush_contents_mask,
                                                slayer3d_game_data_world_point_result *out_result)
{
    init_world_point_result(out_result);
    if (runtime == NULL || out_result == NULL)
        return false;

    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)runtime;
    ++mutable_runtime->world_model_point_query_count;
    const unsigned int filter = world_model_filter_or_all(model_filter);
    if ((filter & SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_SECTOR_LEVELS) != 0u)
    {
        const scene_entry *scene = active_scene_entry_const(runtime);
        yyjson_val *instances = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "sector_levels");
        for (size_t i = 0; yyjson_is_arr(instances) && i < yyjson_arr_size(instances); ++i)
        {
            yyjson_val *entry = yyjson_arr_get(instances, i);
            const char *level_name = json_string(entry, "level", NULL);
            const sector_level_runtime *level_runtime = find_sector_level_runtime(runtime, level_name);
            if (level_runtime == NULL)
                return false;
            const bool sector_lighting_enabled = scene_state_bool(
                runtime, json_string(entry, "sector_lighting_key", NULL), json_bool(entry, "sector_lighting", true));
            const slayer3d_level *level = NULL;
            const char *variant_name = scene_state_string(runtime, json_string(entry, "variant_key", NULL),
                                                          json_string(entry, "variant", "lightmapped"));
            if (sector_level_variant_from_string(variant_name, &level, level_runtime, sector_lighting_enabled) == 0 ||
                level == NULL)
            {
                return false;
            }
            const slayer3d_vec3 position = json_vec3(entry, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            const slayer3d_vec3 local_point = slayer3d_vec3_sub(point, position);
            const int sector_index = slayer3d_level_find_sector_at(level, level_runtime->sectors, local_point.x,
                                                                   local_point.z, local_point.y);
            if (sector_index >= 0 && sector_index < level_runtime->sector_count)
            {
                out_result->inside = true;
                out_result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL;
                out_result->world_name = level_runtime->name;
                out_result->world_position = position;
                out_result->element_name = level_runtime->sector_names[sector_index];
                out_result->element_index = sector_index;
                return true;
            }
        }
    }

    if ((filter & SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS) != 0u)
    {
        slayer3d_game_data_brush_trace_desc trace;
        SDL_zero(trace);
        trace.start = point;
        trace.end = point;
        trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
        trace.contents_mask = brush_contents_mask_or_all(brush_contents_mask);
        slayer3d_game_data_brush_trace_result result;
        if (!slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, &result))
            return false;
        if (result.hit && result.start_solid)
        {
            out_result->inside = true;
            out_result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
            out_result->world_name = result.world_name;
            out_result->world_position = result.world_position;
            out_result->element_name = result.brush_name;
            out_result->element_index = result.brush_index;
            out_result->contents = result.contents;
            return true;
        }
    }

    return true;
}

typedef struct world_model_diagnostics_count_context
{
    Uint64 sector_count;
    Uint64 brush_count;
} world_model_diagnostics_count_context;

static bool count_sector_world_instance(void *userdata, const slayer3d_game_data_sector_level_instance *instance)
{
    (void)instance;
    world_model_diagnostics_count_context *context = (world_model_diagnostics_count_context *)userdata;
    if (context != NULL)
        ++context->sector_count;
    return true;
}

static bool count_brush_world_instance(void *userdata, const slayer3d_game_data_brush_world_instance *instance)
{
    (void)instance;
    world_model_diagnostics_count_context *context = (world_model_diagnostics_count_context *)userdata;
    if (context != NULL)
        ++context->brush_count;
    return true;
}

bool slayer3d_game_data_get_world_model_diagnostics(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_world_model_diagnostics *out_diagnostics)
{
    if (runtime == NULL || out_diagnostics == NULL)
        return false;

    SDL_zero(*out_diagnostics);
    world_model_diagnostics_count_context context;
    SDL_zero(context);
    if (!slayer3d_game_data_for_each_sector_level_instance(runtime, count_sector_world_instance, &context) ||
        !slayer3d_game_data_for_each_brush_world_instance(runtime, count_brush_world_instance, &context))
    {
        return false;
    }
    out_diagnostics->active_sector_level_instances = context.sector_count;
    out_diagnostics->active_brush_world_instances = context.brush_count;
    out_diagnostics->world_trace_count = runtime->world_model_trace_count;
    out_diagnostics->point_query_count = runtime->world_model_point_query_count;
    out_diagnostics->brush = runtime->brush_diagnostics;
    return true;
}

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

const brush_world_runtime *find_brush_world_runtime(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->brush_world_count; ++i)
    {
        const brush_world_runtime *world = &runtime->brush_worlds[i];
        if (world->desc.name != NULL && SDL_strcmp(world->desc.name, name) == 0)
            return world;
    }
    return NULL;
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
                               SDL_strcmp(command, "extrude") == 0 || SDL_strcmp(command, "delete") == 0);
}

static bool editor_command_target_name_valid(const char *target)
{
    return target != NULL && (SDL_strcmp(target, "selection") == 0 || SDL_strcmp(target, "world") == 0 ||
                              SDL_strcmp(target, "element") == 0 || SDL_strcmp(target, "face") == 0 ||
                              SDL_strcmp(target, "material") == 0);
}

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
    if ((SDL_strcmp(command, "translate") == 0 || SDL_strcmp(command, "delete") == 0) && !selection->has_bounds)
    {
        if (out_reason != NULL)
            *out_reason = "selection has no previewable bounds";
        return false;
    }
    if (SDL_strcmp(command, "extrude") == 0 && selection->face_index < 0)
    {
        if (out_reason != NULL)
            *out_reason = "extrude preview requires a face selection";
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
    slayer3d_bounding_box bounds = selection.has_bounds ? translated_bounds(selection.bounds, offset)
                                                        : (slayer3d_bounding_box){selection.point, selection.point};

    editor_command_preview_state *preview = &runtime->editor_command_preview;
    SDL_zero(*preview);
    preview->active = true;
    preview->scene = slayer3d_game_data_active_scene(runtime);
    preview->command = command;
    preview->target = target;
    preview->world_name = selection.world_name;
    preview->element_name = selection.element_name;
    preview->material_name = selection.material_name;
    preview->face_index = selection.face_index;
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

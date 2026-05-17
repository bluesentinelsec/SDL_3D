/**
 * @file game_data_world_navigation.c
 * @brief Authored sector navigation graph query helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/math.h"

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

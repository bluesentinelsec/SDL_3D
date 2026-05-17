/**
 * @file game_data_world_queries.c
 * @brief Scene, sector, brush, and generic world-model query helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
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

brush_world_runtime *find_brush_world_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name)
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

static void free_brush_world_visibility_models(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return;
    for (int i = 0; i < world_runtime->artifacts.brush_render_model_count; ++i)
        slayer3d_free_model(&world_runtime->artifacts.brush_render_models[i]);
    SDL_free(world_runtime->artifacts.brush_render_models);
    world_runtime->artifacts.brush_render_models = NULL;
    world_runtime->artifacts.brush_render_model_count = 0;
    for (int i = 0; i < world_runtime->artifacts.chunk_render_model_count; ++i)
        slayer3d_free_model(&world_runtime->artifacts.chunk_render_models[i]);
    SDL_free(world_runtime->artifacts.chunk_render_models);
    world_runtime->artifacts.chunk_render_models = NULL;
    world_runtime->artifacts.chunk_render_model_count = 0;
}

void free_brush_world_visibility_grid(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return;
    SDL_free(world_runtime->artifacts.visibility_grid_solid);
    for (int i = 0; i < SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS; ++i)
    {
        SDL_free(world_runtime->artifacts.visibility_grid_visible_cache[i]);
        world_runtime->artifacts.visibility_grid_visible_cache[i] = NULL;
        world_runtime->artifacts.visibility_grid_visible_cache_start[i] = -1;
        world_runtime->artifacts.visibility_grid_visible_cache_tick[i] = 0u;
    }
    world_runtime->artifacts.visibility_grid_solid = NULL;
    world_runtime->artifacts.visibility_grid_visible_cache_clock = 0u;
    world_runtime->artifacts.visibility_cell_size = 0.0f;
    world_runtime->artifacts.visibility_grid_dim_x = 0;
    world_runtime->artifacts.visibility_grid_dim_y = 0;
    world_runtime->artifacts.visibility_grid_dim_z = 0;
    world_runtime->artifacts.visibility_grid_cell_count = 0;
    SDL_zero(world_runtime->artifacts.visibility_grid_bounds);
}

static bool brush_point_inside_contents(const slayer3d_game_data_brush *brush, slayer3d_vec3 point,
                                        unsigned int contents_mask)
{
    if (brush == NULL || (brush->contents & contents_mask) == 0u)
        return false;
    const float epsilon = 0.0005f;
    for (int face_index = 0; face_index < brush->face_count; ++face_index)
    {
        const slayer3d_game_data_brush_face *face = &brush->faces[face_index];
        if ((face->surface_flags & SLAYER3D_GAME_DATA_BRUSH_SURFACE_NO_COLLIDE) != 0u)
            continue;
        const float len = slayer3d_vec3_length(face->normal);
        if (len <= 0.000001f)
            continue;
        const slayer3d_vec3 normal = slayer3d_vec3_scale(face->normal, 1.0f / len);
        const float distance = face->distance / len;
        if (slayer3d_vec3_dot(normal, point) - distance > epsilon)
            return false;
    }
    return true;
}

static bool brush_world_point_inside_contents(const slayer3d_game_data_brush_world *world, slayer3d_vec3 point,
                                              unsigned int contents_mask)
{
    if (world == NULL)
        return false;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (brush->has_bounds &&
            (point.x < brush->bounds.min.x || point.x > brush->bounds.max.x || point.y < brush->bounds.min.y ||
             point.y > brush->bounds.max.y || point.z < brush->bounds.min.z || point.z > brush->bounds.max.z))
        {
            continue;
        }
        if (brush_point_inside_contents(brush, point, contents_mask))
            return true;
    }
    return false;
}

static int visibility_grid_index(int x, int y, int z, int dim_x, int dim_y)
{
    return x + y * dim_x + z * dim_x * dim_y;
}

bool compile_brush_world_visibility_grid(brush_world_runtime *world_runtime)
{
    static const int max_cells = 524288;
    static const float min_cell_size = 0.25f;
    static const float max_cell_size = 64.0f;
    free_brush_world_visibility_grid(world_runtime);
    if (world_runtime == NULL)
        return false;

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (world == NULL || !world->has_bounds || world->brush_count <= 0)
        return true;
    const float cell_size = SDL_clamp(world->visibility_cell_size > 0.0f ? world->visibility_cell_size : 2.0f,
                                      min_cell_size, max_cell_size);
    slayer3d_bounding_box bounds = world->bounds;
    bounds.min.x -= cell_size;
    bounds.min.y -= cell_size;
    bounds.min.z -= cell_size;
    bounds.max.x += cell_size;
    bounds.max.y += cell_size;
    bounds.max.z += cell_size;

    const int dim_x = SDL_max(1, (int)SDL_ceilf((bounds.max.x - bounds.min.x) / cell_size));
    const int dim_y = SDL_max(1, (int)SDL_ceilf((bounds.max.y - bounds.min.y) / cell_size));
    const int dim_z = SDL_max(1, (int)SDL_ceilf((bounds.max.z - bounds.min.z) / cell_size));
    if (dim_x <= 0 || dim_y <= 0 || dim_z <= 0 || dim_x > max_cells / SDL_max(dim_y, 1) / SDL_max(dim_z, 1))
        return true;
    const int cell_count = dim_x * dim_y * dim_z;
    if (cell_count <= 0 || cell_count > max_cells)
        return true;

    Uint8 *solid = (Uint8 *)SDL_calloc((size_t)cell_count, sizeof(*solid));
    if (solid == NULL)
        return false;

    const unsigned int blocker_mask =
        SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP;
    for (int z = 0; z < dim_z; ++z)
    {
        for (int y = 0; y < dim_y; ++y)
        {
            for (int x = 0; x < dim_x; ++x)
            {
                const slayer3d_vec3 point = slayer3d_vec3_make(bounds.min.x + ((float)x + 0.5f) * cell_size,
                                                               bounds.min.y + ((float)y + 0.5f) * cell_size,
                                                               bounds.min.z + ((float)z + 0.5f) * cell_size);
                if (brush_world_point_inside_contents(world, point, blocker_mask))
                    solid[visibility_grid_index(x, y, z, dim_x, dim_y)] = 1u;
            }
        }
    }

    world_runtime->artifacts.visibility_grid_bounds = bounds;
    world_runtime->artifacts.visibility_cell_size = cell_size;
    world_runtime->artifacts.visibility_grid_dim_x = dim_x;
    world_runtime->artifacts.visibility_grid_dim_y = dim_y;
    world_runtime->artifacts.visibility_grid_dim_z = dim_z;
    world_runtime->artifacts.visibility_grid_cell_count = cell_count;
    world_runtime->artifacts.visibility_grid_solid = solid;
    for (int i = 0; i < SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS; ++i)
        world_runtime->artifacts.visibility_grid_visible_cache_start[i] = -1;
    return true;
}

static bool compile_brush_world_visibility_models(brush_world_runtime *world_runtime, slayer3d_model **out_models,
                                                  int *out_model_count)
{
    if (out_models != NULL)
        *out_models = NULL;
    if (out_model_count != NULL)
        *out_model_count = 0;
    if (world_runtime == NULL || out_models == NULL || out_model_count == NULL)
        return false;

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (world->brush_count <= 0)
        return true;
    slayer3d_model *models = (slayer3d_model *)SDL_calloc((size_t)world->brush_count, sizeof(*models));
    if (models == NULL)
        return false;
    if (!slayer3d_game_data_brush_world_compile_brush_render_models(world, models, world->brush_count))
    {
        SDL_free(models);
        return false;
    }
    *out_models = models;
    *out_model_count = world->brush_count;
    return true;
}

static bool compile_brush_world_chunk_models(brush_world_runtime *world_runtime, slayer3d_model **out_models,
                                             int *out_model_count)
{
    if (out_models != NULL)
        *out_models = NULL;
    if (out_model_count != NULL)
        *out_model_count = 0;
    if (world_runtime == NULL || out_models == NULL || out_model_count == NULL)
        return false;

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (world->compile_chunk_count <= 0)
        return true;
    slayer3d_model *models = (slayer3d_model *)SDL_calloc((size_t)world->compile_chunk_count, sizeof(*models));
    if (models == NULL)
        return false;
    if (!slayer3d_game_data_brush_world_compile_chunk_render_models(world, models, world->compile_chunk_count))
    {
        SDL_free(models);
        return false;
    }
    *out_models = models;
    *out_model_count = world->compile_chunk_count;
    return true;
}

static void free_brush_world_model_array(slayer3d_model *models, int model_count)
{
    if (models == NULL)
        return;
    for (int i = 0; i < model_count; ++i)
        slayer3d_free_model(&models[i]);
    SDL_free(models);
}

bool rebuild_brush_world_runtime_artifacts(brush_world_runtime *world_runtime, char *error_buffer,
                                           int error_buffer_size)
{
    if (world_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "missing brush world runtime");
        return false;
    }

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    const slayer3d_model *old_render_model = world->render_model;
    char compile_error[256] = {0};
    if (!slayer3d_game_data_brush_world_build_acceleration_checked(world, compile_error, sizeof(compile_error)))
    {
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' acceleration data: %s",
                   world->name != NULL ? world->name : "<unnamed>",
                   compile_error[0] != '\0' ? compile_error : "unknown compile error");
        return false;
    }

    slayer3d_model render_model;
    slayer3d_model *brush_render_models = NULL;
    slayer3d_model *chunk_render_models = NULL;
    int brush_render_model_count = 0;
    int chunk_render_model_count = 0;
    SDL_zero(render_model);

    brush_world_runtime staged_grid;
    SDL_zero(staged_grid);
    staged_grid.desc = *world;

    bool ok = slayer3d_game_data_brush_world_compile_render_model(world, &render_model);
    if (!ok)
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' render mesh",
                   world->name != NULL ? world->name : "<unnamed>");
    if (ok && !compile_brush_world_visibility_models(world_runtime, &brush_render_models, &brush_render_model_count))
    {
        ok = false;
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' visibility meshes",
                   world->name != NULL ? world->name : "<unnamed>");
    }
    if (ok && !compile_brush_world_visibility_grid(&staged_grid))
    {
        ok = false;
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' visibility grid",
                   world->name != NULL ? world->name : "<unnamed>");
    }
    if (ok && !slayer3d_game_data_brush_world_build_compile_chunks(world))
    {
        ok = false;
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' spatial chunks",
                   world->name != NULL ? world->name : "<unnamed>");
    }
    if (ok && !compile_brush_world_chunk_models(world_runtime, &chunk_render_models, &chunk_render_model_count))
    {
        ok = false;
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' chunk meshes",
                   world->name != NULL ? world->name : "<unnamed>");
    }

    if (!ok)
    {
        slayer3d_free_model(&render_model);
        free_brush_world_model_array(brush_render_models, brush_render_model_count);
        free_brush_world_model_array(chunk_render_models, chunk_render_model_count);
        free_brush_world_visibility_grid(&staged_grid);
        world->render_model = old_render_model;
        return false;
    }

    slayer3d_free_model(&world_runtime->artifacts.render_model);
    free_brush_world_visibility_models(world_runtime);
    free_brush_world_visibility_grid(world_runtime);

    world_runtime->artifacts.render_model = render_model;
    world_runtime->artifacts.brush_render_models = brush_render_models;
    world_runtime->artifacts.brush_render_model_count = brush_render_model_count;
    world_runtime->artifacts.chunk_render_models = chunk_render_models;
    world_runtime->artifacts.chunk_render_model_count = chunk_render_model_count;
    world_runtime->artifacts.visibility_grid_bounds = staged_grid.artifacts.visibility_grid_bounds;
    world_runtime->artifacts.visibility_cell_size = staged_grid.artifacts.visibility_cell_size;
    world_runtime->artifacts.visibility_grid_dim_x = staged_grid.artifacts.visibility_grid_dim_x;
    world_runtime->artifacts.visibility_grid_dim_y = staged_grid.artifacts.visibility_grid_dim_y;
    world_runtime->artifacts.visibility_grid_dim_z = staged_grid.artifacts.visibility_grid_dim_z;
    world_runtime->artifacts.visibility_grid_cell_count = staged_grid.artifacts.visibility_grid_cell_count;
    world_runtime->artifacts.visibility_grid_solid = staged_grid.artifacts.visibility_grid_solid;
    for (int i = 0; i < SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS; ++i)
        world_runtime->artifacts.visibility_grid_visible_cache_start[i] = -1;

    world->render_model = &world_runtime->artifacts.render_model;
    world->compile_artifact_hash = slayer3d_game_data_brush_world_compute_compile_artifact_hash(world);
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

slayer3d_bounding_box translated_bounds(slayer3d_bounding_box bounds, slayer3d_vec3 position)
{
    bounds.min = slayer3d_vec3_add(bounds.min, position);
    bounds.max = slayer3d_vec3_add(bounds.max, position);
    return bounds;
}

bool model_bounds(const slayer3d_model *model, slayer3d_bounding_box *out_bounds)
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

slayer3d_game_data_sector_level_variant sector_level_variant_from_string(const char *variant,
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

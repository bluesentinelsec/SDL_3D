/* Grid-map and pickup-layer runtime helpers. Included by src/game_data.c to preserve internal linkage. */

static bool grid_map_normalize_cell(const grid_map_runtime *map, int *col, int *row)
{
    if (map == NULL || col == NULL || row == NULL || map->width <= 0 || map->height <= 0)
        return false;
    if (*col < 0 || *col >= map->width)
    {
        if (!map->wrap_x)
            return false;
        *col = (*col % map->width + map->width) % map->width;
    }
    if (*row < 0 || *row >= map->height)
    {
        if (!map->wrap_y)
            return false;
        *row = (*row % map->height + map->height) % map->height;
    }
    return true;
}

static char grid_map_cell(const grid_map_runtime *map, int col, int row)
{
    if (!grid_map_normalize_cell(map, &col, &row))
        return '\0';
    return map->cells[row * map->width + col];
}

static bool grid_map_is_walkable(const grid_map_runtime *map, int col, int row)
{
    const char cell = grid_map_cell(map, col, row);
    if (cell == '\0' || map == NULL || map->walkable == NULL)
        return false;
    for (const char *cursor = map->walkable; *cursor != '\0'; ++cursor)
    {
        if (*cursor == cell)
            return true;
    }
    return false;
}

static bool grid_map_cell_to_world(const grid_map_runtime *map, int col, int row, slayer3d_vec3 *out_position)
{
    if (map == NULL || out_position == NULL || !grid_map_normalize_cell(map, &col, &row))
        return false;
    *out_position =
        slayer3d_vec3_make(map->origin.x + (float)col * map->cell_width,
                           map->origin.y + (float)row * map->cell_height * map->row_direction, map->origin.z);
    return true;
}

static int grid_round_to_int(float value)
{
    return value >= 0.0f ? (int)(value + 0.5f) : (int)(value - 0.5f);
}

static bool grid_map_world_to_cell(const grid_map_runtime *map, float x, float y, int *out_col, int *out_row)
{
    if (map == NULL || out_col == NULL || out_row == NULL || map->cell_width <= 0.0f || map->cell_height <= 0.0f ||
        map->row_direction == 0.0f)
    {
        return false;
    }
    int col = grid_round_to_int((x - map->origin.x) / map->cell_width);
    int row = grid_round_to_int((y - map->origin.y) / (map->cell_height * map->row_direction));
    if (!grid_map_normalize_cell(map, &col, &row))
        return false;
    *out_col = col;
    *out_row = row;
    return true;
}

const grid_map_runtime *find_grid_map(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->grid_map_count; ++i)
    {
        if (runtime->grid_maps[i].name != NULL && SDL_strcmp(runtime->grid_maps[i].name, name) == 0)
            return &runtime->grid_maps[i];
    }
    return NULL;
}

static grid_actor_index *find_grid_actor_index(slayer3d_game_data_runtime *runtime, const char *map_name,
                                               const char *pool_name)
{
    if (runtime == NULL || map_name == NULL || pool_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->grid_actor_index_count; ++i)
    {
        grid_actor_index *index = &runtime->grid_actor_indices[i];
        if (index->map != NULL && index->pool != NULL && SDL_strcmp(index->map, map_name) == 0 &&
            SDL_strcmp(index->pool, pool_name) == 0)
        {
            return index;
        }
    }
    return NULL;
}

static grid_actor_index *get_or_create_grid_actor_index(slayer3d_game_data_runtime *runtime,
                                                        const grid_map_runtime *map, const char *pool_name)
{
    grid_actor_index *existing = find_grid_actor_index(runtime, map != NULL ? map->name : NULL, pool_name);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || map == NULL || map->name == NULL || pool_name == NULL || pool_name[0] == '\0' ||
        map->width <= 0 || map->height <= 0)
    {
        return NULL;
    }

    if (runtime->grid_actor_index_count >= runtime->grid_actor_index_capacity)
    {
        const int next_capacity = runtime->grid_actor_index_capacity > 0 ? runtime->grid_actor_index_capacity * 2 : 4;
        grid_actor_index *next = (grid_actor_index *)SDL_realloc(
            runtime->grid_actor_indices, (size_t)next_capacity * sizeof(*runtime->grid_actor_indices));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->grid_actor_index_capacity, 0,
                   (size_t)(next_capacity - runtime->grid_actor_index_capacity) * sizeof(*runtime->grid_actor_indices));
        runtime->grid_actor_indices = next;
        runtime->grid_actor_index_capacity = next_capacity;
    }

    grid_actor_index *index = &runtime->grid_actor_indices[runtime->grid_actor_index_count];
    SDL_zero(*index);
    index->map = SDL_strdup(map->name);
    index->pool = SDL_strdup(pool_name);
    index->width = map->width;
    index->height = map->height;
    index->actors =
        (slayer3d_registered_actor **)SDL_calloc((size_t)map->width * (size_t)map->height, sizeof(*index->actors));
    if (index->map == NULL || index->pool == NULL || index->actors == NULL)
    {
        SDL_free(index->map);
        SDL_free(index->pool);
        SDL_free(index->actors);
        SDL_zero(*index);
        return NULL;
    }
    ++runtime->grid_actor_index_count;
    return index;
}

static void grid_actor_index_clear(slayer3d_game_data_runtime *runtime, const grid_map_runtime *map,
                                   const char *pool_name)
{
    grid_actor_index *index = find_grid_actor_index(runtime, map != NULL ? map->name : NULL, pool_name);
    if (index == NULL || index->actors == NULL || index->width <= 0 || index->height <= 0)
        return;
    SDL_memset(index->actors, 0, (size_t)index->width * (size_t)index->height * sizeof(*index->actors));
}

static bool grid_actor_index_register(slayer3d_game_data_runtime *runtime, const grid_map_runtime *map,
                                      const char *pool_name, slayer3d_registered_actor *actor, int col, int row)
{
    if (actor == NULL || !grid_map_normalize_cell(map, &col, &row))
        return false;
    grid_actor_index *index = get_or_create_grid_actor_index(runtime, map, pool_name);
    if (index == NULL || index->actors == NULL || index->width != map->width || index->height != map->height)
        return false;
    index->actors[row * index->width + col] = actor;
    return true;
}

static slayer3d_registered_actor *grid_actor_index_find(slayer3d_game_data_runtime *runtime, const char *map_name,
                                                        const char *pool_name, int col, int row)
{
    const grid_map_runtime *map = find_grid_map(runtime, map_name);
    if (!grid_map_normalize_cell(map, &col, &row))
        return NULL;
    grid_actor_index *index = find_grid_actor_index(runtime, map_name, pool_name);
    if (index == NULL || index->actors == NULL || index->width != map->width || index->height != map->height)
        return NULL;

    slayer3d_registered_actor *actor = index->actors[row * index->width + col];
    if (!runtime_actor_is_active(runtime, actor))
        return NULL;
    const char *actor_map = slayer3d_properties_get_string(actor->props, "grid_map", NULL);
    const int actor_col = slayer3d_properties_get_int(actor->props, "grid_col", -1);
    const int actor_row = slayer3d_properties_get_int(actor->props, "grid_row", -1);
    const int start_col = slayer3d_properties_get_int(actor->props, "grid_run_start_col", actor_col);
    const int start_row = slayer3d_properties_get_int(actor->props, "grid_run_start_row", actor_row);
    const int end_col = slayer3d_properties_get_int(actor->props, "grid_run_end_col", actor_col);
    const int end_row = slayer3d_properties_get_int(actor->props, "grid_run_end_row", actor_row);
    if (actor_map == NULL || SDL_strcmp(actor_map, map_name) != 0 || col < SDL_min(start_col, end_col) ||
        col > SDL_max(start_col, end_col) || row < SDL_min(start_row, end_row) || row > SDL_max(start_row, end_row))
    {
        return NULL;
    }
    return actor;
}

static grid_pickup_layer_runtime *find_grid_pickup_layer(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->grid_pickup_layer_count; ++i)
    {
        if (runtime->grid_pickup_layers[i].name != NULL && SDL_strcmp(runtime->grid_pickup_layers[i].name, name) == 0)
            return &runtime->grid_pickup_layers[i];
    }
    return NULL;
}

static bool grid_pickup_layer_reset(slayer3d_game_data_runtime *runtime, grid_pickup_layer_runtime *layer)
{
    (void)runtime;
    if (layer == NULL || layer->map == NULL || layer->cells == NULL)
        return false;
    const int count = layer->map->width * layer->map->height;
    SDL_memset(layer->cells, 0, (size_t)count * sizeof(*layer->cells));
    layer->active_count = 0;
    for (int row = 0; row < layer->map->height; ++row)
    {
        for (int col = 0; col < layer->map->width; ++col)
        {
            const char glyph = grid_map_cell(layer->map, col, row);
            for (int kind_index = 0; kind_index < layer->kind_count; ++kind_index)
            {
                if (layer->kinds[kind_index].glyph != glyph)
                    continue;
                layer->cells[row * layer->map->width + col] = (Uint8)(kind_index + 1);
                layer->active_count++;
                break;
            }
        }
    }
    return true;
}

static bool grid_pickup_layer_collect_at(slayer3d_game_data_runtime *runtime, grid_pickup_layer_runtime *layer, int col,
                                         int row, grid_pickup_kind_runtime *out_kind)
{
    (void)runtime;
    if (out_kind != NULL)
        SDL_zero(*out_kind);
    if (layer == NULL || layer->map == NULL || layer->cells == NULL || !grid_map_normalize_cell(layer->map, &col, &row))
        return false;
    Uint8 *cell = &layer->cells[row * layer->map->width + col];
    if (*cell == 0 || *cell > (Uint8)layer->kind_count)
        return false;
    if (out_kind != NULL)
        *out_kind = layer->kinds[*cell - 1];
    *cell = 0;
    if (layer->active_count > 0)
        layer->active_count--;
    return true;
}

static bool grid_map_next_step(const grid_map_runtime *map, int start_col, int start_row, int goal_col, int goal_row,
                               int *out_col, int *out_row)
{
    if (out_col != NULL)
        *out_col = start_col;
    if (out_row != NULL)
        *out_row = start_row;
    if (map == NULL || out_col == NULL || out_row == NULL || !grid_map_normalize_cell(map, &start_col, &start_row) ||
        !grid_map_normalize_cell(map, &goal_col, &goal_row) || !grid_map_is_walkable(map, start_col, start_row) ||
        !grid_map_is_walkable(map, goal_col, goal_row))
    {
        return false;
    }
    if (start_col == goal_col && start_row == goal_row)
        return true;

    const int count = map->width * map->height;
    bool *visited = (bool *)SDL_calloc((size_t)count, sizeof(*visited));
    int *previous = (int *)SDL_malloc((size_t)count * sizeof(*previous));
    int *queue = (int *)SDL_malloc((size_t)count * sizeof(*queue));
    if (visited == NULL || previous == NULL || queue == NULL)
    {
        SDL_free(visited);
        SDL_free(previous);
        SDL_free(queue);
        return false;
    }
    for (int i = 0; i < count; ++i)
        previous[i] = -1;

    const int start = start_row * map->width + start_col;
    const int goal = goal_row * map->width + goal_col;
    int read_index = 0;
    int write_index = 0;
    queue[write_index++] = start;
    visited[start] = true;

    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, -1}, {0, 1}};
    while (read_index < write_index && !visited[goal])
    {
        const int current = queue[read_index++];
        const int col = current % map->width;
        const int row = current / map->width;
        for (size_t d = 0; d < SDL_arraysize(dirs); ++d)
        {
            int next_col = col + dirs[d][0];
            int next_row = row + dirs[d][1];
            if (!grid_map_normalize_cell(map, &next_col, &next_row) || !grid_map_is_walkable(map, next_col, next_row))
            {
                continue;
            }
            const int next = next_row * map->width + next_col;
            if (visited[next])
                continue;
            visited[next] = true;
            previous[next] = current;
            queue[write_index++] = next;
            if (next == goal)
                break;
        }
    }

    bool found = visited[goal];
    if (found)
    {
        int step = goal;
        while (previous[step] >= 0 && previous[step] != start)
            step = previous[step];
        *out_col = step % map->width;
        *out_row = step / map->width;
    }

    SDL_free(visited);
    SDL_free(previous);
    SDL_free(queue);
    return found;
}

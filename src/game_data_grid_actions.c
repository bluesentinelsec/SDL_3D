/* Grid spawn and pickup action helpers. */

#include "game_data_internal.h"

static bool grid_spawn_rule_matches(yyjson_val *rule, char glyph)
{
    const char *rule_glyph = json_string(rule, "glyph", NULL);
    return rule_glyph != NULL && rule_glyph[0] == glyph && rule_glyph[1] == '\0';
}

bool execute_grid_spawn_from_glyphs_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const grid_map_runtime *map = find_grid_map(runtime, json_string(action, "map", NULL));
    yyjson_val *spawns = obj_get(action, "spawns");
    if (runtime == NULL || map == NULL || !yyjson_is_arr(spawns))
        return false;

    bool ok = true;
    int spawned_count = 0;
    for (size_t i = 0; i < yyjson_arr_size(spawns); ++i)
    {
        yyjson_val *rule = yyjson_arr_get(spawns, i);
        grid_actor_index_clear(runtime, map, json_string(rule, "pool", NULL));
    }
    for (int row = 0; row < map->height; ++row)
    {
        for (int col = 0; col < map->width; ++col)
        {
            const char glyph = grid_map_cell(map, col, row);
            yyjson_val *rule = NULL;
            for (size_t i = 0; i < yyjson_arr_size(spawns); ++i)
            {
                yyjson_val *candidate = yyjson_arr_get(spawns, i);
                if (grid_spawn_rule_matches(candidate, glyph))
                {
                    rule = candidate;
                    break;
                }
            }
            if (rule == NULL)
                continue;

            actor_pool_runtime *pool = find_actor_pool(runtime, json_string(rule, "pool", NULL));
            actor_pool_note_spawn_attempt(pool);
            int actor_index = -1;
            slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
            if (pool == NULL || actor == NULL || actor_index < 0)
            {
                actor_pool_note_spawn_failure(pool, "exhausted");
                ok = false;
                continue;
            }

            actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
            if (!initialize_pooled_actor(pool, actor, actor_index, true))
            {
                actor_pool_note_spawn_failure(pool, "initialize_failed");
                ok = false;
                continue;
            }
            if (pool->spawn_generations != NULL)
            {
                pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
                slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                            (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
            }

            slayer3d_vec3 position;
            if (!grid_map_cell_to_world(map, col, row, &position))
            {
                actor_pool_note_spawn_failure(pool, "invalid_cell");
                ok = false;
                continue;
            }
            position.z = json_float(rule, "z", json_float(action, "z", position.z));
            actor_set_position(actor, position);
            slayer3d_properties_set_string(actor->props, "grid_map", map->name);
            slayer3d_properties_set_int(actor->props, "grid_col", col);
            slayer3d_properties_set_int(actor->props, "grid_row", row);
            char glyph_text[2] = {glyph, '\0'};
            slayer3d_properties_set_string(actor->props, "grid_glyph", glyph_text);
            apply_actor_spawn_properties(actor, obj_get(action, "properties"));
            apply_actor_spawn_properties(actor, obj_get(rule, "properties"));
            if (!grid_actor_index_register(runtime, map, pool != NULL ? pool->name : NULL, actor, col, row))
                ok = false;
            actor_pool_note_spawn_success(runtime, pool);
            spawned_count++;
        }
    }

    const char *count_key = json_string(action, "output_count_key", NULL);
    if (count_key != NULL && runtime->scene_state != NULL)
        slayer3d_properties_set_int(runtime->scene_state, count_key, spawned_count);
    return ok;
}

static bool grid_run_axis_is_y(yyjson_val *action, yyjson_val *rule)
{
    const char *axis = json_string(rule, "axis", json_string(action, "axis", "x"));
    return axis != NULL &&
           (SDL_strcmp(axis, "y") == 0 || SDL_strcmp(axis, "vertical") == 0 || SDL_strcmp(axis, "column") == 0);
}

static bool spawn_grid_run_actor(slayer3d_game_data_runtime *runtime, const grid_map_runtime *map, yyjson_val *action,
                                 yyjson_val *rule, int start_col, int start_row, int end_col, int end_row)
{
    actor_pool_runtime *pool = find_actor_pool(runtime, json_string(rule, "pool", NULL));
    actor_pool_note_spawn_attempt(pool);
    int actor_index = -1;
    slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
    if (pool == NULL || actor == NULL || actor_index < 0)
    {
        actor_pool_note_spawn_failure(pool, "exhausted");
        return false;
    }

    actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
    if (!initialize_pooled_actor(pool, actor, actor_index, true))
    {
        actor_pool_note_spawn_failure(pool, "initialize_failed");
        return false;
    }
    if (pool->spawn_generations != NULL)
    {
        pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
        slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                    (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
    }

    slayer3d_vec3 start_position;
    slayer3d_vec3 end_position;
    if (!grid_map_cell_to_world(map, start_col, start_row, &start_position) ||
        !grid_map_cell_to_world(map, end_col, end_row, &end_position))
    {
        actor_pool_note_spawn_failure(pool, "invalid_cell");
        return false;
    }

    slayer3d_vec3 position =
        slayer3d_vec3_make((start_position.x + end_position.x) * 0.5f, (start_position.y + end_position.y) * 0.5f,
                           (start_position.z + end_position.z) * 0.5f);
    position.z = json_float(rule, "z", json_float(action, "z", position.z));
    actor_set_position(actor, position);

    const bool axis_y = grid_run_axis_is_y(action, rule);
    const int run_length = axis_y ? SDL_abs(end_row - start_row) + 1 : SDL_abs(end_col - start_col) + 1;
    const float depth = json_float(rule, "depth", json_float(action, "depth", 0.25f));
    const float inset = SDL_max(0.0f, json_float(rule, "inset", json_float(action, "inset", 0.0f)));
    const float run_width = axis_y ? SDL_max(0.001f, map->cell_width - inset * 2.0f)
                                   : SDL_max(0.001f, map->cell_width * (float)run_length - inset * 2.0f);
    const float run_height = axis_y ? SDL_max(0.001f, map->cell_height * (float)run_length - inset * 2.0f)
                                    : SDL_max(0.001f, map->cell_height - inset * 2.0f);
    const slayer3d_vec3 run_size =
        json_vec3(rule, "size", json_vec3(action, "size", slayer3d_vec3_make(run_width, run_height, depth)));

    char glyph_text[2] = {grid_map_cell(map, start_col, start_row), '\0'};
    slayer3d_properties_set_string(actor->props, "grid_map", map->name);
    slayer3d_properties_set_int(actor->props, "grid_col", start_col);
    slayer3d_properties_set_int(actor->props, "grid_row", start_row);
    slayer3d_properties_set_string(actor->props, "grid_glyph", glyph_text);
    slayer3d_properties_set_string(actor->props, "grid_run_axis", axis_y ? "y" : "x");
    slayer3d_properties_set_int(actor->props, "grid_run_start_col", start_col);
    slayer3d_properties_set_int(actor->props, "grid_run_start_row", start_row);
    slayer3d_properties_set_int(actor->props, "grid_run_end_col", end_col);
    slayer3d_properties_set_int(actor->props, "grid_run_end_row", end_row);
    slayer3d_properties_set_int(actor->props, "grid_run_length", run_length);
    slayer3d_properties_set_vec3(actor->props, "grid_run_size", run_size);
    apply_actor_spawn_properties(actor, obj_get(action, "properties"));
    apply_actor_spawn_properties(actor, obj_get(rule, "properties"));

    for (int row = SDL_min(start_row, end_row); row <= SDL_max(start_row, end_row); ++row)
    {
        for (int col = SDL_min(start_col, end_col); col <= SDL_max(start_col, end_col); ++col)
            (void)grid_actor_index_register(runtime, map, pool->name, actor, col, row);
    }
    actor_pool_note_spawn_success(runtime, pool);
    return true;
}

bool execute_grid_spawn_runs_from_glyphs_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const grid_map_runtime *map = find_grid_map(runtime, json_string(action, "map", NULL));
    yyjson_val *spawns = obj_get(action, "spawns");
    if (runtime == NULL || map == NULL || !yyjson_is_arr(spawns))
        return false;

    bool ok = true;
    int spawned_count = 0;
    for (size_t i = 0; i < yyjson_arr_size(spawns); ++i)
    {
        yyjson_val *rule = yyjson_arr_get(spawns, i);
        grid_actor_index_clear(runtime, map, json_string(rule, "pool", NULL));
    }

    for (size_t rule_index = 0; rule_index < yyjson_arr_size(spawns); ++rule_index)
    {
        yyjson_val *rule = yyjson_arr_get(spawns, rule_index);
        const bool axis_y = grid_run_axis_is_y(action, rule);
        if (axis_y)
        {
            for (int col = 0; col < map->width; ++col)
            {
                int row = 0;
                while (row < map->height)
                {
                    if (!grid_spawn_rule_matches(rule, grid_map_cell(map, col, row)))
                    {
                        ++row;
                        continue;
                    }
                    const int start_row = row;
                    while (row + 1 < map->height && grid_spawn_rule_matches(rule, grid_map_cell(map, col, row + 1)))
                        ++row;
                    if (spawn_grid_run_actor(runtime, map, action, rule, col, start_row, col, row))
                        ++spawned_count;
                    else
                        ok = false;
                    ++row;
                }
            }
        }
        else
        {
            for (int row = 0; row < map->height; ++row)
            {
                int col = 0;
                while (col < map->width)
                {
                    if (!grid_spawn_rule_matches(rule, grid_map_cell(map, col, row)))
                    {
                        ++col;
                        continue;
                    }
                    const int start_col = col;
                    while (col + 1 < map->width && grid_spawn_rule_matches(rule, grid_map_cell(map, col + 1, row)))
                        ++col;
                    if (spawn_grid_run_actor(runtime, map, action, rule, start_col, row, col, row))
                        ++spawned_count;
                    else
                        ok = false;
                    ++col;
                }
            }
        }
    }

    const char *count_key = json_string(action, "output_count_key", NULL);
    if (count_key != NULL && runtime->scene_state != NULL)
        slayer3d_properties_set_int(runtime->scene_state, count_key, spawned_count);
    return ok;
}

bool execute_grid_pickup_layer_reset_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    grid_pickup_layer_runtime *layer = find_grid_pickup_layer(runtime, json_string(action, "layer", NULL));
    if (layer == NULL || !grid_pickup_layer_reset(runtime, layer))
        return false;

    const char *count_key = json_string(action, "output_count_key", NULL);
    if (count_key != NULL && runtime != NULL && runtime->scene_state != NULL)
        slayer3d_properties_set_int(runtime->scene_state, count_key, layer->active_count);
    return true;
}

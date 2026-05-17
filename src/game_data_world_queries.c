/**
 * @file game_data_world_queries.c
 * @brief Scene, sector, and brush-world access helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

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

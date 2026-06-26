/**
 * @file game_presentation_cache_lifecycle.c
 * @brief Lifecycle helpers for presentation caches.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/font.h"
#include "slayer3d/model.h"
#include "slayer3d/sprite_asset.h"
#include "slayer3d/texture.h"

void slayer3d_game_data_font_cache_init(slayer3d_game_data_font_cache *cache, const char *media_dir)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->media_dir = media_dir;
}

void slayer3d_game_data_font_cache_free(slayer3d_game_data_font_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        slayer3d_free_font(&cache->fonts[i]);
        SDL_free((char *)cache->font_ids[i]);
    }
    SDL_free(cache->fonts);
    SDL_free(cache->font_ids);
    SDL_zero(*cache);
}

void slayer3d_game_data_image_cache_init(slayer3d_game_data_image_cache *cache, slayer3d_asset_resolver *assets)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->assets = assets;
}

void slayer3d_game_data_image_cache_free(slayer3d_game_data_image_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].loaded)
            slayer3d_free_texture(&cache->entries[i].texture);
        SDL_free(cache->entries[i].image_id);
        SDL_free(cache->entries[i].source_path);
        SDL_free(cache->entries[i].shader_vertex_source);
        SDL_free(cache->entries[i].shader_fragment_source);
    }
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

void slayer3d_game_data_sprite_cache_init(slayer3d_game_data_sprite_cache *cache, slayer3d_asset_resolver *assets)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->assets = assets;
}

void slayer3d_game_data_sprite_cache_free(slayer3d_game_data_sprite_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].loaded)
            slayer3d_sprite_asset_free(&cache->entries[i].sprite);
        SDL_free((char *)cache->entries[i].sprite_id);
    }
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

void slayer3d_game_data_model_cache_init(slayer3d_game_data_model_cache *cache, slayer3d_asset_resolver *assets)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->assets = assets;
}

void slayer3d_game_data_model_cache_free(slayer3d_game_data_model_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].loaded)
            slayer3d_free_model(&cache->entries[i].model);
        SDL_free((char *)cache->entries[i].model_id);
    }
    for (int i = 0; i < cache->pose_capacity; ++i)
        SDL_free(cache->pose_entries[i].joint_matrices);
    SDL_free(cache->pose_entries);
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

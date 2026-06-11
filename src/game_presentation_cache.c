/**
 * @file game_presentation_cache.c
 * @brief Presentation asset cache loading and animation pose cache helpers.
 */

#include "game_presentation_internal.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/animation.h"
#include "slayer3d/image.h"

#include "game_data_internal.h"
#include "render_context_internal.h"

static bool ensure_font_cache_capacity(slayer3d_game_data_font_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_font *new_fonts = (slayer3d_font *)SDL_calloc((size_t)next_capacity, sizeof(*new_fonts));
    if (new_fonts == NULL)
        return false;

    const char **new_font_ids = (const char **)SDL_calloc((size_t)next_capacity, sizeof(*new_font_ids));
    if (new_font_ids == NULL)
    {
        SDL_free(new_fonts);
        return false;
    }

    for (int i = 0; i < cache->count; ++i)
    {
        new_fonts[i] = cache->fonts[i];
        new_font_ids[i] = cache->font_ids[i];
    }

    SDL_free(cache->fonts);
    SDL_free(cache->font_ids);
    cache->fonts = new_fonts;
    cache->font_ids = new_font_ids;
    cache->capacity = next_capacity;
    return true;
}

static bool ensure_image_cache_capacity(slayer3d_game_data_image_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_image_cache_entry *entries =
        (slayer3d_game_data_image_cache_entry *)SDL_realloc(cache->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static bool ensure_sprite_cache_capacity(slayer3d_game_data_sprite_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_sprite_cache_entry *entries =
        (slayer3d_game_data_sprite_cache_entry *)SDL_realloc(cache->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static bool ensure_model_cache_capacity(slayer3d_game_data_model_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_model_cache_entry *entries =
        (slayer3d_game_data_model_cache_entry *)SDL_realloc(cache->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static bool ensure_model_pose_cache_capacity(slayer3d_game_data_model_cache *cache, int required)
{
    if (cache == NULL || required <= cache->pose_capacity)
        return cache != NULL;

    int next_capacity = cache->pose_capacity < 16 ? 16 : cache->pose_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_model_pose_cache_entry *entries = (slayer3d_game_data_model_pose_cache_entry *)SDL_realloc(
        cache->pose_entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->pose_capacity, 0, (size_t)(next_capacity - cache->pose_capacity) * sizeof(*entries));
    cache->pose_entries = entries;
    cache->pose_capacity = next_capacity;
    return true;
}

bool slayer3d_game_data_ensure_mesh_primitive_cache_capacity(slayer3d_game_data_mesh_primitive_cache *cache,
                                                             int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 16 ? 16 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_mesh_primitive_cache_entry *entries =
        (slayer3d_game_data_mesh_primitive_cache_entry *)SDL_realloc(cache->entries,
                                                                     (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

slayer3d_font *slayer3d_game_data_find_or_load_font(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_font_cache *cache, const char *font_id)
{
    if (runtime == NULL || cache == NULL || font_id == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->font_ids[i] != NULL && SDL_strcmp(cache->font_ids[i], font_id) == 0)
            return &cache->fonts[i];
    }

    if (!ensure_font_cache_capacity(cache, cache->count + 1))
        return NULL;

    slayer3d_game_data_font_asset font;
    if (!slayer3d_game_data_get_font_asset(runtime, font_id, &font))
        return NULL;

    slayer3d_font *slot = &cache->fonts[cache->count];
    bool loaded = false;
    if (font.builtin)
        loaded = slayer3d_load_builtin_font(cache->media_dir, font.builtin_id, font.size, slot);
    else if (font.path != NULL)
        loaded = slayer3d_load_font(font.path, font.size, slot);
    if (!loaded)
        return NULL;

    cache->font_ids[cache->count] = font_id;
    cache->count++;
    return slot;
}

bool slayer3d_game_data_prepare_direct_image_texture(slayer3d_asset_resolver *assets,
                                                     const slayer3d_game_data_image_asset *asset,
                                                     slayer3d_texture2d *out_texture)
{
    if (assets == NULL || asset == NULL || asset->path == NULL || out_texture == NULL)
        return false;

    SDL_zero(*out_texture);
    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char error[256];
    if (!slayer3d_asset_resolver_read_file(assets, asset->path, &buffer, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read UI image asset %s: %s", asset->path, error);
        return false;
    }

    slayer3d_image image;
    SDL_zero(image);
    const bool decoded = slayer3d_load_image_from_memory(buffer.data, buffer.size, &image);
    slayer3d_asset_buffer_free(&buffer);
    if (!decoded)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to decode UI image asset %s", asset->path);
        return false;
    }

    const bool loaded = slayer3d_create_texture_from_image(&image, out_texture);
    slayer3d_free_image(&image);
    if (!loaded)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture for UI image asset %s", asset->path);
        return false;
    }
    return true;
}

slayer3d_game_data_image_cache_entry *slayer3d_game_data_image_cache_insert_prepared_texture(
    slayer3d_game_data_image_cache *cache, const char *image_id, slayer3d_texture2d *texture)
{
    if (cache == NULL || image_id == NULL || texture == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].image_id != NULL && SDL_strcmp(cache->entries[i].image_id, image_id) == 0)
        {
            if (cache->entries[i].loaded)
            {
                slayer3d_free_texture(texture);
                return &cache->entries[i];
            }
            return NULL;
        }
    }

    if (!ensure_image_cache_capacity(cache, cache->count + 1))
        return NULL;

    slayer3d_game_data_image_cache_entry *entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->image_id = image_id;
    entry->effect = NULL;
    entry->effect_delay = 0.0f;
    entry->effect_duration = 1.0f;
    entry->shader_vertex_source = NULL;
    entry->shader_fragment_source = NULL;
    entry->texture = *texture;
    SDL_zero(*texture);
    entry->loaded = true;
    ++cache->count;
    return entry;
}

slayer3d_game_data_image_cache_entry *slayer3d_game_data_find_or_load_image_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_image_cache *cache, const char *image_id)
{
    if (runtime == NULL || cache == NULL || cache->assets == NULL || image_id == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].image_id != NULL && SDL_strcmp(cache->entries[i].image_id, image_id) == 0)
            return cache->entries[i].loaded ? &cache->entries[i] : NULL;
    }

    if (!ensure_image_cache_capacity(cache, cache->count + 1))
        return NULL;

    slayer3d_game_data_image_asset asset;
    if (!slayer3d_game_data_get_image_asset(runtime, image_id, &asset))
        return NULL;

    if (asset.sprite != NULL)
    {
        slayer3d_sprite_asset_runtime sprite;
        SDL_zero(sprite);
        char error[256];
        if (!slayer3d_game_data_load_sprite_asset(runtime, asset.sprite, &sprite, error, (int)sizeof(error)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load sprite-backed UI image %s: %s", asset.sprite,
                         error);
            return NULL;
        }

        if (sprite.base_texture_count <= 0 || sprite.base_textures == NULL || sprite.base_textures[0].pixels == NULL ||
            sprite.base_textures[0].width <= 0 || sprite.base_textures[0].height <= 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Sprite-backed UI image %s has no base texture", asset.sprite);
            slayer3d_sprite_asset_free(&sprite);
            return NULL;
        }

        slayer3d_image image;
        SDL_zero(image);
        image.pixels = sprite.base_textures[0].pixels;
        image.width = sprite.base_textures[0].width;
        image.height = sprite.base_textures[0].height;

        slayer3d_game_data_image_cache_entry *entry = &cache->entries[cache->count];
        SDL_zero(*entry);
        entry->image_id = asset.id;
        entry->effect = sprite.effect;
        entry->effect_delay = sprite.effect_delay;
        entry->effect_duration = sprite.effect_duration;
        if (sprite.shader_vertex_source != NULL)
            entry->shader_vertex_source = SDL_strdup(sprite.shader_vertex_source);
        if (sprite.shader_fragment_source != NULL)
            entry->shader_fragment_source = SDL_strdup(sprite.shader_fragment_source);
        if ((sprite.shader_vertex_source != NULL && entry->shader_vertex_source == NULL) ||
            (sprite.shader_fragment_source != NULL && entry->shader_fragment_source == NULL))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to copy sprite-backed UI image shader source %s",
                         asset.sprite);
            SDL_free(entry->shader_vertex_source);
            SDL_free(entry->shader_fragment_source);
            slayer3d_sprite_asset_free(&sprite);
            return NULL;
        }
        entry->loaded = slayer3d_create_texture_from_image(&image, &entry->texture);
        slayer3d_sprite_asset_free(&sprite);
        if (!entry->loaded)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture for sprite-backed UI image %s",
                         asset.sprite);
            SDL_free(entry->shader_vertex_source);
            SDL_free(entry->shader_fragment_source);
            SDL_zero(*entry);
            return NULL;
        }

        ++cache->count;
        return entry;
    }

    if (asset.path == NULL)
        return NULL;

    slayer3d_texture2d texture;
    SDL_zero(texture);
    if (!slayer3d_game_data_prepare_direct_image_texture(cache->assets, &asset, &texture))
        return NULL;
    slayer3d_game_data_image_cache_entry *entry =
        slayer3d_game_data_image_cache_insert_prepared_texture(cache, asset.id, &texture);
    if (entry == NULL)
        slayer3d_free_texture(&texture);
    return entry;
}

slayer3d_game_data_sprite_cache_entry *slayer3d_game_data_find_or_load_sprite_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_sprite_cache *cache, const char *sprite_id)
{
    if (runtime == NULL || cache == NULL || cache->assets == NULL || sprite_id == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].sprite_id != NULL && SDL_strcmp(cache->entries[i].sprite_id, sprite_id) == 0)
            return cache->entries[i].loaded ? &cache->entries[i] : NULL;
    }

    if (!ensure_sprite_cache_capacity(cache, cache->count + 1))
        return NULL;

    slayer3d_game_data_sprite_cache_entry *entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->sprite_id = sprite_id;

    char error[256];
    if (!slayer3d_game_data_load_sprite_asset(runtime, sprite_id, &entry->sprite, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load world sprite asset %s: %s", sprite_id, error);
        SDL_zero(entry);
        return NULL;
    }

    entry->loaded = true;
    ++cache->count;
    return entry;
}

slayer3d_game_data_model_cache_entry *slayer3d_game_data_find_or_load_model_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_model_cache *cache, const char *model_id)
{
    if (runtime == NULL || cache == NULL || cache->assets == NULL || model_id == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].model_id != NULL && SDL_strcmp(cache->entries[i].model_id, model_id) == 0)
            return cache->entries[i].loaded ? &cache->entries[i] : NULL;
    }

    if (!ensure_model_cache_capacity(cache, cache->count + 1))
        return NULL;

    slayer3d_game_data_model_asset asset;
    if (!slayer3d_game_data_get_model_asset(runtime, model_id, &asset))
        return NULL;

    char error[256];
    char *filesystem_path = NULL;
    if (!slayer3d_asset_resolver_resolve_file_path(cache->assets, asset.path, &filesystem_path, error,
                                                   (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to resolve model asset %s (%s): %s. Model assets currently require a directory mount.",
                     model_id, asset.path != NULL ? asset.path : "<null>", error);
        return NULL;
    }

    slayer3d_game_data_model_cache_entry *entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->model_id = model_id;
    entry->loaded = slayer3d_load_model_from_file(filesystem_path, &entry->model);
    slayer3d_asset_resolver_free_path(filesystem_path);
    if (!entry->loaded)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load model asset %s: %s", model_id, SDL_GetError());
        SDL_zero(entry);
        return NULL;
    }

    ++cache->count;
    return entry;
}

void slayer3d_game_data_model_cache_begin_pose_frame(slayer3d_game_data_model_cache *cache)
{
    if (cache != NULL)
        cache->pose_count = 0;
}

static const slayer3d_mat4 *model_pose_cache_evaluate_clip(slayer3d_game_data_model_cache *cache,
                                                           slayer3d_render_context *renderer,
                                                           const slayer3d_model *model,
                                                           const slayer3d_animation_clip *clip, int animation_clip,
                                                           float animation_time, int *out_joint_count)
{
    if (out_joint_count != NULL)
        *out_joint_count = 0;
    if (cache == NULL || model == NULL || model->skeleton == NULL || clip == NULL || out_joint_count == NULL)
        return NULL;

    const int joint_count = model->skeleton->joint_count;
    if (joint_count <= 0)
        return NULL;

    for (int i = 0; i < cache->pose_count; ++i)
    {
        slayer3d_game_data_model_pose_cache_entry *entry = &cache->pose_entries[i];
        if (entry->model == model && entry->animation_clip == animation_clip &&
            entry->animation_time == animation_time && entry->joint_count == joint_count)
        {
            *out_joint_count = joint_count;
            if (renderer != NULL)
                renderer->stats.animation_pose_cache_hits += 1u;
            return entry->joint_matrices;
        }
    }

    if (!ensure_model_pose_cache_capacity(cache, cache->pose_count + 1))
        return NULL;

    slayer3d_game_data_model_pose_cache_entry *entry = &cache->pose_entries[cache->pose_count];
    if (entry->joint_capacity < joint_count)
    {
        slayer3d_mat4 *matrices =
            (slayer3d_mat4 *)SDL_realloc(entry->joint_matrices, (size_t)joint_count * sizeof(*matrices));
        if (matrices == NULL)
            return NULL;
        entry->joint_matrices = matrices;
        entry->joint_capacity = joint_count;
    }

    if (!slayer3d_evaluate_animation(model->skeleton, clip, animation_time, entry->joint_matrices))
        return NULL;

    entry->model = model;
    entry->animation_clip = animation_clip;
    entry->animation_time = animation_time;
    entry->joint_count = joint_count;
    ++cache->pose_count;
    *out_joint_count = joint_count;
    if (renderer != NULL)
        renderer->stats.animation_pose_evaluations += 1u;
    return entry->joint_matrices;
}

const slayer3d_mat4 *slayer3d_game_data_model_cache_evaluate_pose(slayer3d_game_data_model_cache *cache,
                                                                  slayer3d_render_context *renderer,
                                                                  const slayer3d_model *model, int animation_clip,
                                                                  float animation_time, bool loop, int *out_joint_count)
{
    if (out_joint_count != NULL)
        *out_joint_count = 0;
    if (cache == NULL || model == NULL || model->skeleton == NULL || model->animations == NULL ||
        model->animation_count <= 0 || animation_clip < 0 || out_joint_count == NULL)
    {
        return NULL;
    }

    int clip_index = animation_clip;
    if (clip_index >= model->animation_count)
        clip_index = 0;
    const slayer3d_animation_clip *clip = &model->animations[clip_index];
    if (loop && clip->duration > 0.0f)
    {
        animation_time = SDL_fmodf(animation_time, clip->duration);
        if (animation_time < 0.0f)
            animation_time += clip->duration;
    }
    return model_pose_cache_evaluate_clip(cache, renderer, model, clip, clip_index, animation_time, out_joint_count);
}

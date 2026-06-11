/**
 * @file game_presentation_asset_warmup.c
 * @brief Budgeted presentation asset warmup queue.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "game_presentation_internal.h"
#include "render_context_internal.h"
#include "texture_internal.h"

static bool warmup_entry_matches(const slayer3d_game_data_asset_warmup_entry *entry,
                                 slayer3d_game_data_asset_warmup_kind kind, const char *source_path, const char *id)
{
    if (entry == NULL || entry->kind != kind)
        return false;
    const char *entry_source = entry->source_path != NULL ? entry->source_path : "";
    const char *request_source = source_path != NULL ? source_path : "";
    return entry->id != NULL && id != NULL && SDL_strcmp(entry->id, id) == 0 &&
           SDL_strcmp(entry_source, request_source) == 0;
}

static bool ensure_warmup_queue_capacity(slayer3d_game_data_asset_warmup_queue *queue, int required)
{
    if (queue == NULL || required <= queue->capacity)
        return queue != NULL;

    int next_capacity = queue->capacity < 16 ? 16 : queue->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_asset_warmup_entry *entries =
        (slayer3d_game_data_asset_warmup_entry *)SDL_realloc(queue->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return SDL_OutOfMemory();

    SDL_memset(entries + queue->capacity, 0, (size_t)(next_capacity - queue->capacity) * sizeof(*entries));
    queue->entries = entries;
    queue->capacity = next_capacity;
    return true;
}

static bool request_warmup_asset(slayer3d_game_data_asset_warmup_queue *queue,
                                 slayer3d_game_data_asset_warmup_kind kind, const char *source_path, const char *id)
{
    if (queue == NULL || id == NULL || id[0] == '\0')
        return false;

    for (int i = 0; i < queue->count; ++i)
    {
        if (warmup_entry_matches(&queue->entries[i], kind, source_path, id))
            return true;
    }

    if (!ensure_warmup_queue_capacity(queue, queue->count + 1))
        return false;

    slayer3d_game_data_asset_warmup_entry *entry = &queue->entries[queue->count];
    SDL_zero(*entry);
    entry->kind = kind;
    entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED;
    if (source_path != NULL && source_path[0] != '\0')
    {
        entry->source_path = SDL_strdup(source_path);
        if (entry->source_path == NULL)
            return SDL_OutOfMemory();
    }
    entry->id = SDL_strdup(id);
    if (entry->id == NULL)
    {
        SDL_free(entry->source_path);
        SDL_zero(*entry);
        return SDL_OutOfMemory();
    }
    queue->count++;
    return true;
}

void slayer3d_game_data_asset_warmup_queue_init(slayer3d_game_data_asset_warmup_queue *queue, int max_jobs_per_frame)
{
    if (queue == NULL)
        return;
    SDL_zero(*queue);
    queue->max_jobs_per_frame = max_jobs_per_frame > 0 ? max_jobs_per_frame : 1;
}

void slayer3d_game_data_asset_warmup_queue_free(slayer3d_game_data_asset_warmup_queue *queue)
{
    if (queue == NULL)
        return;
    for (int i = 0; i < queue->count; ++i)
    {
        SDL_free(queue->entries[i].source_path);
        SDL_free(queue->entries[i].id);
    }
    SDL_free(queue->entries);
    SDL_free(queue->requested_scene);
    SDL_zero(*queue);
}

bool slayer3d_game_data_asset_warmup_request_ui_image(slayer3d_game_data_asset_warmup_queue *queue,
                                                      const char *image_id)
{
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE, NULL, image_id);
}

bool slayer3d_game_data_asset_warmup_request_texture(slayer3d_game_data_asset_warmup_queue *queue,
                                                     const char *source_path, const char *texture_path)
{
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_TEXTURE, source_path, texture_path);
}

bool slayer3d_game_data_asset_warmup_request_sprite(slayer3d_game_data_asset_warmup_queue *queue, const char *sprite_id)
{
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_SPRITE, NULL, sprite_id);
}

bool slayer3d_game_data_asset_warmup_request_model(slayer3d_game_data_asset_warmup_queue *queue, const char *model_id)
{
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_MODEL, NULL, model_id);
}

void slayer3d_game_data_asset_warmup_queue_stats(const slayer3d_game_data_asset_warmup_queue *queue,
                                                 slayer3d_game_data_asset_warmup_stats *out_stats)
{
    if (out_stats == NULL)
        return;
    SDL_zero(*out_stats);
    if (queue == NULL)
        return;
    out_stats->total = queue->count;
    for (int i = 0; i < queue->count; ++i)
    {
        switch (queue->entries[i].state)
        {
        case SLAYER3D_GAME_DATA_ASSET_WARMUP_READY:
            out_stats->ready++;
            break;
        case SLAYER3D_GAME_DATA_ASSET_WARMUP_FAILED:
            out_stats->failed++;
            break;
        case SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED:
        default:
            out_stats->queued++;
            break;
        }
    }
}

static bool service_warmup_entry(slayer3d_game_data_asset_warmup_entry *entry,
                                 const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                 slayer3d_game_data_image_cache *image_cache,
                                 slayer3d_game_data_sprite_cache *sprite_cache,
                                 slayer3d_game_data_model_cache *model_cache, slayer3d_asset_resolver *assets)
{
    if (entry == NULL || entry->id == NULL)
        return false;

    switch (entry->kind)
    {
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE:
        return slayer3d_game_data_find_or_load_image_entry(runtime, image_cache, entry->id) != NULL;
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_TEXTURE: {
        if (renderer == NULL)
            return false;
        const slayer3d_texture2d *texture = NULL;
        return slayer3d_texture_cache_get_or_load_asset(&renderer->texture_cache, assets, entry->source_path, entry->id,
                                                        &texture);
    }
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_SPRITE:
        return slayer3d_game_data_find_or_load_sprite_entry(runtime, sprite_cache, entry->id) != NULL;
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_MODEL:
        return slayer3d_game_data_find_or_load_model_entry(runtime, model_cache, entry->id) != NULL;
    default:
        return false;
    }
}

static const char *warmup_kind_name(slayer3d_game_data_asset_warmup_kind kind)
{
    switch (kind)
    {
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_TEXTURE:
        return "texture";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_MODEL:
        return "model";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_SPRITE:
        return "sprite";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE:
    default:
        return "image";
    }
}

int slayer3d_game_data_asset_warmup_queue_service(slayer3d_game_data_asset_warmup_queue *queue,
                                                  const slayer3d_game_data_runtime *runtime,
                                                  slayer3d_render_context *renderer,
                                                  slayer3d_game_data_image_cache *image_cache,
                                                  slayer3d_game_data_sprite_cache *sprite_cache,
                                                  slayer3d_game_data_model_cache *model_cache,
                                                  slayer3d_asset_resolver *assets, int max_jobs)
{
    if (queue == NULL || runtime == NULL)
        return 0;

    int budget = max_jobs > 0 ? max_jobs : queue->max_jobs_per_frame;
    if (budget <= 0)
        budget = 1;

    int serviced = 0;
    for (int i = 0; i < queue->count && serviced < budget; ++i)
    {
        slayer3d_game_data_asset_warmup_entry *entry = &queue->entries[i];
        if (entry->state != SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED)
            continue;

        const bool ok = service_warmup_entry(entry, runtime, renderer, image_cache, sprite_cache, model_cache, assets);
        entry->state = ok ? SLAYER3D_GAME_DATA_ASSET_WARMUP_READY : SLAYER3D_GAME_DATA_ASSET_WARMUP_FAILED;
        if (!ok)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to warm %s asset %s: %s", warmup_kind_name(entry->kind),
                        entry->id, SDL_GetError());
        }
        serviced++;
    }
    return serviced;
}

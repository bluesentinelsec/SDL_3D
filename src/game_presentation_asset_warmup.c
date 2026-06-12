/**
 * @file game_presentation_asset_warmup.c
 * @brief Budgeted presentation asset warmup queue.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include "game_presentation_internal.h"
#include "render_context_internal.h"
#include "texture_internal.h"

typedef struct asset_warmup_prepared_texture
{
    char *resolved_path;
    slayer3d_texture2d texture;
} asset_warmup_prepared_texture;

typedef struct asset_warmup_prepared_image
{
    slayer3d_texture2d texture;
    const char *effect;
    float effect_delay;
    float effect_duration;
    char *shader_vertex_source;
    char *shader_fragment_source;
} asset_warmup_prepared_image;

typedef struct asset_warmup_prepared_sprite
{
    slayer3d_sprite_asset_runtime sprite;
} asset_warmup_prepared_sprite;

typedef struct asset_warmup_prepared_model
{
    slayer3d_model model;
} asset_warmup_prepared_model;

typedef struct asset_warmup_worker_state
{
    SDL_Mutex *mutex;
    SDL_Condition *condition;
    SDL_Thread **threads;
    int thread_count;
    bool stopping;
    const slayer3d_game_data_runtime *runtime;
    slayer3d_asset_resolver *assets;
    slayer3d_game_data_asset_warmup_queue *queue;
} asset_warmup_worker_state;

static const char *warmup_kind_name(slayer3d_game_data_asset_warmup_kind kind);

static Uint64 warmup_now_counter(void)
{
    return SDL_GetPerformanceCounter();
}

static float warmup_elapsed_ms(Uint64 start_counter, Uint64 end_counter)
{
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    if (frequency == 0 || start_counter == 0 || end_counter < start_counter)
        return 0.0f;
    return (float)(((double)(end_counter - start_counter) * 1000.0) / (double)frequency);
}

static void record_warmup_activity(slayer3d_game_data_asset_warmup_queue *queue, Uint64 counter)
{
    if (queue == NULL || counter == 0)
        return;
    if (queue->first_request_counter == 0)
        queue->first_request_counter = counter;
    queue->last_activity_counter = counter;
}

static void free_prepared_texture(asset_warmup_prepared_texture *prepared)
{
    if (prepared == NULL)
        return;
    SDL_free(prepared->resolved_path);
    slayer3d_free_texture(&prepared->texture);
    SDL_free(prepared);
}

static void free_prepared_image(asset_warmup_prepared_image *prepared)
{
    if (prepared == NULL)
        return;
    slayer3d_free_texture(&prepared->texture);
    SDL_free(prepared->shader_vertex_source);
    SDL_free(prepared->shader_fragment_source);
    SDL_free(prepared);
}

static void free_prepared_sprite(asset_warmup_prepared_sprite *prepared)
{
    if (prepared == NULL)
        return;
    slayer3d_sprite_asset_free(&prepared->sprite);
    SDL_free(prepared);
}

static void free_prepared_model(asset_warmup_prepared_model *prepared)
{
    if (prepared == NULL)
        return;
    slayer3d_free_model(&prepared->model);
    SDL_free(prepared);
}

static void free_warmup_entry_prepared(slayer3d_game_data_asset_warmup_entry *entry)
{
    if (entry == NULL || entry->prepared == NULL)
        return;
    if (entry->kind == SLAYER3D_GAME_DATA_ASSET_WARMUP_TEXTURE)
        free_prepared_texture((asset_warmup_prepared_texture *)entry->prepared);
    else if (entry->kind == SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE)
        free_prepared_image((asset_warmup_prepared_image *)entry->prepared);
    else if (entry->kind == SLAYER3D_GAME_DATA_ASSET_WARMUP_SPRITE)
        free_prepared_sprite((asset_warmup_prepared_sprite *)entry->prepared);
    else if (entry->kind == SLAYER3D_GAME_DATA_ASSET_WARMUP_MODEL)
        free_prepared_model((asset_warmup_prepared_model *)entry->prepared);
    entry->prepared = NULL;
}

static asset_warmup_worker_state *queue_worker_state(const slayer3d_game_data_asset_warmup_queue *queue)
{
    return queue != NULL ? (asset_warmup_worker_state *)queue->worker_state : NULL;
}

static void queue_lock(slayer3d_game_data_asset_warmup_queue *queue)
{
    asset_warmup_worker_state *worker_state = queue_worker_state(queue);
    if (worker_state != NULL && worker_state->mutex != NULL)
        SDL_LockMutex(worker_state->mutex);
}

static void queue_unlock(slayer3d_game_data_asset_warmup_queue *queue)
{
    asset_warmup_worker_state *worker_state = queue_worker_state(queue);
    if (worker_state != NULL && worker_state->mutex != NULL)
        SDL_UnlockMutex(worker_state->mutex);
}

static void queue_signal_workers(slayer3d_game_data_asset_warmup_queue *queue)
{
    asset_warmup_worker_state *worker_state = queue_worker_state(queue);
    if (worker_state != NULL && worker_state->condition != NULL)
        SDL_SignalCondition(worker_state->condition);
}

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

static void bump_warmup_entry_generation(slayer3d_game_data_asset_warmup_entry *entry)
{
    if (entry == NULL)
        return;
    entry->generation++;
    if (entry->generation == 0)
        entry->generation = 1;
}

static bool request_warmup_asset(slayer3d_game_data_asset_warmup_queue *queue,
                                 slayer3d_game_data_asset_warmup_kind kind, const char *source_path, const char *id,
                                 const slayer3d_game_data_render_primitive *mesh_primitive)
{
    if (queue == NULL || id == NULL || id[0] == '\0')
        return false;

    const Uint64 now_counter = warmup_now_counter();
    queue_lock(queue);
    for (int i = 0; i < queue->count; ++i)
    {
        if (warmup_entry_matches(&queue->entries[i], kind, source_path, id))
        {
            if (queue->entries[i].state == SLAYER3D_GAME_DATA_ASSET_WARMUP_CANCELED)
            {
                free_warmup_entry_prepared(&queue->entries[i]);
                bump_warmup_entry_generation(&queue->entries[i]);
                queue->entries[i].state = SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED;
                if (mesh_primitive != NULL)
                    queue->entries[i].mesh_primitive = *mesh_primitive;
                record_warmup_activity(queue, now_counter);
                queue_signal_workers(queue);
            }
            queue_unlock(queue);
            return true;
        }
    }

    if (!ensure_warmup_queue_capacity(queue, queue->count + 1))
    {
        queue_unlock(queue);
        return false;
    }

    slayer3d_game_data_asset_warmup_entry *entry = &queue->entries[queue->count];
    SDL_zero(*entry);
    entry->kind = kind;
    entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED;
    entry->generation = 1;
    if (mesh_primitive != NULL)
        entry->mesh_primitive = *mesh_primitive;
    if (source_path != NULL && source_path[0] != '\0')
    {
        entry->source_path = SDL_strdup(source_path);
        if (entry->source_path == NULL)
        {
            queue_unlock(queue);
            return SDL_OutOfMemory();
        }
    }
    entry->id = SDL_strdup(id);
    if (entry->id == NULL)
    {
        SDL_free(entry->source_path);
        SDL_zero(*entry);
        queue_unlock(queue);
        return SDL_OutOfMemory();
    }
    record_warmup_activity(queue, now_counter);
    queue->count++;
    queue_signal_workers(queue);
    queue_unlock(queue);
    return true;
}

static bool worker_can_prepare_entry(const asset_warmup_worker_state *worker_state,
                                     const slayer3d_game_data_asset_warmup_entry *entry)
{
    if (worker_state == NULL || entry == NULL || entry->id == NULL)
        return false;
    if (entry->kind == SLAYER3D_GAME_DATA_ASSET_WARMUP_TEXTURE)
        return true;
    if (entry->kind == SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE && worker_state->runtime != NULL)
    {
        slayer3d_game_data_image_asset asset;
        return slayer3d_game_data_get_image_asset(worker_state->runtime, entry->id, &asset) &&
               (asset.path != NULL || asset.sprite != NULL);
    }
    if (entry->kind == SLAYER3D_GAME_DATA_ASSET_WARMUP_SPRITE && worker_state->runtime != NULL)
        return true;
    if (entry->kind == SLAYER3D_GAME_DATA_ASSET_WARMUP_MODEL && worker_state->runtime != NULL &&
        worker_state->assets != NULL)
        return true;
    return false;
}

static int find_queued_worker_index(const asset_warmup_worker_state *worker_state)
{
    const slayer3d_game_data_asset_warmup_queue *queue = worker_state != NULL ? worker_state->queue : NULL;
    if (queue == NULL)
        return -1;
    for (int i = 0; i < queue->count; ++i)
    {
        const slayer3d_game_data_asset_warmup_entry *entry = &queue->entries[i];
        if (entry->state == SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED && worker_can_prepare_entry(worker_state, entry))
            return i;
    }
    return -1;
}

static int find_loading_worker_index(const slayer3d_game_data_asset_warmup_queue *queue,
                                     slayer3d_game_data_asset_warmup_kind kind, const char *source_path, const char *id,
                                     unsigned int generation)
{
    if (queue == NULL)
        return -1;
    for (int i = 0; i < queue->count; ++i)
    {
        const slayer3d_game_data_asset_warmup_entry *entry = &queue->entries[i];
        if (entry->state == SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING && entry->generation == generation &&
            warmup_entry_matches(entry, kind, source_path, id))
            return i;
    }
    return -1;
}

static bool copy_warmup_entry_request(const slayer3d_game_data_asset_warmup_entry *entry, char **out_source_path,
                                      char **out_id)
{
    *out_source_path = NULL;
    *out_id = NULL;
    if (entry == NULL || entry->id == NULL)
        return false;
    if (entry->source_path != NULL)
    {
        *out_source_path = SDL_strdup(entry->source_path);
        if (*out_source_path == NULL)
            return SDL_OutOfMemory();
    }
    *out_id = SDL_strdup(entry->id);
    if (*out_id == NULL)
    {
        SDL_free(*out_source_path);
        *out_source_path = NULL;
        return SDL_OutOfMemory();
    }
    return true;
}

static bool prepare_texture_request(asset_warmup_worker_state *worker_state, const char *source_path, const char *id,
                                    asset_warmup_prepared_texture **out_prepared)
{
    asset_warmup_prepared_texture *prepared = NULL;

    if (worker_state == NULL || id == NULL || out_prepared == NULL)
        return false;

    *out_prepared = NULL;
    prepared = (asset_warmup_prepared_texture *)SDL_calloc(1, sizeof(*prepared));
    if (prepared == NULL)
        return SDL_OutOfMemory();

    if (!slayer3d_texture_cache_prepare_asset(worker_state->assets, source_path, id, &prepared->resolved_path,
                                              &prepared->texture))
    {
        free_prepared_texture(prepared);
        return false;
    }

    *out_prepared = prepared;
    return true;
}

static bool prepare_image_request(asset_warmup_worker_state *worker_state, const char *id,
                                  asset_warmup_prepared_image **out_prepared)
{
    asset_warmup_prepared_image *prepared = NULL;

    if (worker_state == NULL || worker_state->runtime == NULL || id == NULL || out_prepared == NULL)
        return false;

    *out_prepared = NULL;
    slayer3d_game_data_image_asset asset;
    if (!slayer3d_game_data_get_image_asset(worker_state->runtime, id, &asset))
        return false;

    prepared = (asset_warmup_prepared_image *)SDL_calloc(1, sizeof(*prepared));
    if (prepared == NULL)
        return SDL_OutOfMemory();
    prepared->effect_duration = 1.0f;

    if (asset.sprite != NULL)
    {
        if (!slayer3d_game_data_prepare_sprite_backed_image_texture(
                worker_state->runtime, &asset, &prepared->texture, &prepared->effect, &prepared->effect_delay,
                &prepared->effect_duration, &prepared->shader_vertex_source, &prepared->shader_fragment_source))
        {
            free_prepared_image(prepared);
            return false;
        }
    }
    else if (asset.path != NULL)
    {
        if (!slayer3d_game_data_prepare_direct_image_texture(worker_state->assets, &asset, &prepared->texture))
        {
            free_prepared_image(prepared);
            return false;
        }
    }
    else
    {
        free_prepared_image(prepared);
        return false;
    }

    *out_prepared = prepared;
    return true;
}

static bool prepare_sprite_request(asset_warmup_worker_state *worker_state, const char *id,
                                   asset_warmup_prepared_sprite **out_prepared)
{
    asset_warmup_prepared_sprite *prepared = NULL;
    char error[256];

    if (worker_state == NULL || worker_state->runtime == NULL || id == NULL || out_prepared == NULL)
        return false;

    *out_prepared = NULL;
    prepared = (asset_warmup_prepared_sprite *)SDL_calloc(1, sizeof(*prepared));
    if (prepared == NULL)
        return SDL_OutOfMemory();

    if (!slayer3d_game_data_load_sprite_asset(worker_state->runtime, id, &prepared->sprite, error, (int)sizeof(error)))
    {
        SDL_SetError("%s", error);
        free_prepared_sprite(prepared);
        return false;
    }

    *out_prepared = prepared;
    return true;
}

static bool prepare_model_request(asset_warmup_worker_state *worker_state, const char *id,
                                  asset_warmup_prepared_model **out_prepared)
{
    asset_warmup_prepared_model *prepared = NULL;
    slayer3d_game_data_model_asset asset;
    char error[256];
    char *filesystem_path = NULL;

    if (worker_state == NULL || worker_state->runtime == NULL || worker_state->assets == NULL || id == NULL ||
        out_prepared == NULL)
        return false;

    *out_prepared = NULL;
    if (!slayer3d_game_data_get_model_asset(worker_state->runtime, id, &asset))
        return SDL_SetError("model asset not found: %s", id);

    if (!slayer3d_asset_resolver_resolve_file_path(worker_state->assets, asset.path, &filesystem_path, error,
                                                   (int)sizeof(error)))
        return SDL_SetError("%s", error);

    prepared = (asset_warmup_prepared_model *)SDL_calloc(1, sizeof(*prepared));
    if (prepared == NULL)
    {
        slayer3d_asset_resolver_free_path(filesystem_path);
        return SDL_OutOfMemory();
    }

    if (!slayer3d_load_model_from_file(filesystem_path, &prepared->model))
    {
        slayer3d_asset_resolver_free_path(filesystem_path);
        free_prepared_model(prepared);
        return false;
    }

    slayer3d_asset_resolver_free_path(filesystem_path);
    *out_prepared = prepared;
    return true;
}

static bool prepare_worker_request(asset_warmup_worker_state *worker_state, slayer3d_game_data_asset_warmup_kind kind,
                                   const char *source_path, const char *id, void **out_prepared)
{
    if (out_prepared == NULL)
        return false;
    *out_prepared = NULL;

    switch (kind)
    {
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_TEXTURE:
        return prepare_texture_request(worker_state, source_path, id, (asset_warmup_prepared_texture **)out_prepared);
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE:
        return prepare_image_request(worker_state, id, (asset_warmup_prepared_image **)out_prepared);
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_SPRITE:
        return prepare_sprite_request(worker_state, id, (asset_warmup_prepared_sprite **)out_prepared);
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_MODEL:
        return prepare_model_request(worker_state, id, (asset_warmup_prepared_model **)out_prepared);
    default:
        return false;
    }
}

static int SDLCALL warmup_worker_main(void *userdata)
{
    asset_warmup_worker_state *worker_state = (asset_warmup_worker_state *)userdata;
    if (worker_state == NULL || worker_state->queue == NULL || worker_state->mutex == NULL ||
        worker_state->condition == NULL)
        return 0;

    for (;;)
    {
        slayer3d_game_data_asset_warmup_kind kind = 0;
        char *source_path = NULL;
        char *id = NULL;
        unsigned int generation = 0;

        SDL_LockMutex(worker_state->mutex);
        while (!worker_state->stopping)
        {
            const int index = find_queued_worker_index(worker_state);
            if (index >= 0)
            {
                slayer3d_game_data_asset_warmup_entry *entry = &worker_state->queue->entries[index];
                if (copy_warmup_entry_request(entry, &source_path, &id))
                {
                    kind = entry->kind;
                    generation = entry->generation;
                    entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING;
                    record_warmup_activity(worker_state->queue, warmup_now_counter());
                }
                else
                {
                    entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_FAILED;
                    record_warmup_activity(worker_state->queue, warmup_now_counter());
                }
                break;
            }
            SDL_WaitCondition(worker_state->condition, worker_state->mutex);
        }
        const bool stopping = worker_state->stopping;
        SDL_UnlockMutex(worker_state->mutex);

        if (stopping)
        {
            SDL_free(source_path);
            SDL_free(id);
            break;
        }
        if (id == NULL)
            continue;

        void *prepared = NULL;
        const bool prepared_ok = prepare_worker_request(worker_state, kind, source_path, id, &prepared);

        SDL_LockMutex(worker_state->mutex);
        const int index = find_loading_worker_index(worker_state->queue, kind, source_path, id, generation);
        if (index >= 0)
        {
            slayer3d_game_data_asset_warmup_entry *entry = &worker_state->queue->entries[index];
            if (prepared_ok && prepared != NULL)
            {
                free_warmup_entry_prepared(entry);
                entry->prepared = prepared;
                prepared = NULL;
                entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_READY_FOR_FINALIZE;
                record_warmup_activity(worker_state->queue, warmup_now_counter());
            }
            else
            {
                entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_FAILED;
                record_warmup_activity(worker_state->queue, warmup_now_counter());
            }
        }
        SDL_UnlockMutex(worker_state->mutex);

        if (!prepared_ok)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to prepare %s asset %s: %s", warmup_kind_name(kind), id,
                        SDL_GetError());
        }
        slayer3d_game_data_asset_warmup_entry cleanup_entry;
        SDL_zero(cleanup_entry);
        cleanup_entry.kind = kind;
        cleanup_entry.prepared = prepared;
        free_warmup_entry_prepared(&cleanup_entry);
        SDL_free(source_path);
        SDL_free(id);
    }

    return 0;
}

void slayer3d_game_data_asset_warmup_queue_init(slayer3d_game_data_asset_warmup_queue *queue, int max_jobs_per_frame)
{
    if (queue == NULL)
        return;
    SDL_zero(*queue);
    queue->max_jobs_per_frame = max_jobs_per_frame > 0 ? max_jobs_per_frame : 1;
}

bool slayer3d_game_data_asset_warmup_queue_start_workers(slayer3d_game_data_asset_warmup_queue *queue,
                                                         const slayer3d_game_data_runtime *runtime,
                                                         slayer3d_asset_resolver *assets, int worker_count)
{
    if (queue == NULL || assets == NULL)
        return false;
    if (queue->worker_state != NULL)
        return true;
    if (worker_count <= 0)
        worker_count = 1;
#ifdef __EMSCRIPTEN__
    (void)worker_count;
    return false;
#else
    asset_warmup_worker_state *worker_state = (asset_warmup_worker_state *)SDL_calloc(1, sizeof(*worker_state));
    if (worker_state == NULL)
        return SDL_OutOfMemory();

    worker_state->mutex = SDL_CreateMutex();
    worker_state->condition = SDL_CreateCondition();
    worker_state->threads = (SDL_Thread **)SDL_calloc((size_t)worker_count, sizeof(*worker_state->threads));
    if (worker_state->mutex == NULL || worker_state->condition == NULL || worker_state->threads == NULL)
    {
        if (worker_state->mutex != NULL)
            SDL_DestroyMutex(worker_state->mutex);
        if (worker_state->condition != NULL)
            SDL_DestroyCondition(worker_state->condition);
        SDL_free(worker_state->threads);
        SDL_free(worker_state);
        return false;
    }

    worker_state->queue = queue;
    worker_state->runtime = runtime;
    worker_state->assets = assets;
    worker_state->thread_count = worker_count;
    queue->worker_state = worker_state;

    for (int i = 0; i < worker_count; ++i)
    {
        char name[32];
        (void)SDL_snprintf(name, sizeof(name), "asset-warmup-%d", i);
        worker_state->threads[i] = SDL_CreateThread(warmup_worker_main, name, worker_state);
        if (worker_state->threads[i] == NULL)
        {
            slayer3d_game_data_asset_warmup_queue_stop_workers(queue);
            return false;
        }
    }

    queue_signal_workers(queue);
    return true;
#endif
}

void slayer3d_game_data_asset_warmup_queue_stop_workers(slayer3d_game_data_asset_warmup_queue *queue)
{
    asset_warmup_worker_state *worker_state = queue_worker_state(queue);
    if (worker_state == NULL)
        return;

    SDL_LockMutex(worker_state->mutex);
    worker_state->stopping = true;
    SDL_BroadcastCondition(worker_state->condition);
    SDL_UnlockMutex(worker_state->mutex);

    for (int i = 0; i < worker_state->thread_count; ++i)
    {
        if (worker_state->threads[i] != NULL)
            SDL_WaitThread(worker_state->threads[i], NULL);
    }

    SDL_DestroyCondition(worker_state->condition);
    SDL_DestroyMutex(worker_state->mutex);
    SDL_free(worker_state->threads);
    SDL_free(worker_state);
    queue->worker_state = NULL;
}

void slayer3d_game_data_asset_warmup_queue_free(slayer3d_game_data_asset_warmup_queue *queue)
{
    if (queue == NULL)
        return;
    slayer3d_game_data_asset_warmup_queue_stop_workers(queue);
    for (int i = 0; i < queue->count; ++i)
    {
        free_warmup_entry_prepared(&queue->entries[i]);
        SDL_free(queue->entries[i].source_path);
        SDL_free(queue->entries[i].id);
    }
    SDL_free(queue->entries);
    SDL_free(queue->requested_scene);
    SDL_zero(*queue);
}

int slayer3d_game_data_asset_warmup_queue_cancel_pending(slayer3d_game_data_asset_warmup_queue *queue)
{
    if (queue == NULL)
        return 0;

    int canceled = 0;
    const Uint64 now_counter = warmup_now_counter();
    queue_lock(queue);
    for (int i = 0; i < queue->count; ++i)
    {
        slayer3d_game_data_asset_warmup_entry *entry = &queue->entries[i];
        if (entry->state != SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED &&
            entry->state != SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING &&
            entry->state != SLAYER3D_GAME_DATA_ASSET_WARMUP_READY_FOR_FINALIZE)
        {
            continue;
        }

        free_warmup_entry_prepared(entry);
        bump_warmup_entry_generation(entry);
        entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_CANCELED;
        record_warmup_activity(queue, now_counter);
        canceled++;
    }
    queue_unlock(queue);
    return canceled;
}

bool slayer3d_game_data_asset_warmup_request_ui_image(slayer3d_game_data_asset_warmup_queue *queue,
                                                      const char *image_id)
{
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE, NULL, image_id, NULL);
}

bool slayer3d_game_data_asset_warmup_request_font(slayer3d_game_data_asset_warmup_queue *queue, const char *font_id)
{
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_FONT, NULL, font_id, NULL);
}

bool slayer3d_game_data_asset_warmup_request_texture(slayer3d_game_data_asset_warmup_queue *queue,
                                                     const char *source_path, const char *texture_path)
{
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_TEXTURE, source_path, texture_path, NULL);
}

bool slayer3d_game_data_asset_warmup_request_sprite(slayer3d_game_data_asset_warmup_queue *queue, const char *sprite_id)
{
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_SPRITE, NULL, sprite_id, NULL);
}

bool slayer3d_game_data_asset_warmup_request_model(slayer3d_game_data_asset_warmup_queue *queue, const char *model_id)
{
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_MODEL, NULL, model_id, NULL);
}

bool slayer3d_game_data_asset_warmup_request_audio_file(slayer3d_game_data_asset_warmup_queue *queue,
                                                        const char *audio_path)
{
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_AUDIO_FILE, NULL, audio_path, NULL);
}

bool slayer3d_game_data_asset_warmup_request_mesh_primitive(slayer3d_game_data_asset_warmup_queue *queue,
                                                            const slayer3d_game_data_render_primitive *primitive)
{
    if (queue == NULL || primitive == NULL)
        return false;
    if (!slayer3d_game_data_mesh_primitive_cacheable(primitive))
        return true;

    char key[256];
    if (!slayer3d_game_data_mesh_primitive_warmup_key(primitive, key, (int)sizeof(key)))
        return false;
    return request_warmup_asset(queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_MESH_PRIMITIVE, NULL, key, primitive);
}

void slayer3d_game_data_asset_warmup_queue_stats(const slayer3d_game_data_asset_warmup_queue *queue,
                                                 slayer3d_game_data_asset_warmup_stats *out_stats)
{
    if (out_stats == NULL)
        return;
    SDL_zero(*out_stats);
    if (queue == NULL)
        return;
    queue_lock((slayer3d_game_data_asset_warmup_queue *)queue);
    out_stats->total = queue->count;
    asset_warmup_worker_state *worker_state = queue_worker_state(queue);
    out_stats->worker_threads = worker_state != NULL ? worker_state->thread_count : 0;
    out_stats->service_calls = queue->service_calls;
    out_stats->service_jobs = queue->service_jobs;
    out_stats->service_last_ms = queue->service_last_ms;
    out_stats->service_total_ms = queue->service_total_ms;
    out_stats->service_max_ms = queue->service_max_ms;
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
        case SLAYER3D_GAME_DATA_ASSET_WARMUP_CANCELED:
            out_stats->canceled++;
            break;
        case SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING:
            out_stats->loading++;
            break;
        case SLAYER3D_GAME_DATA_ASSET_WARMUP_READY_FOR_FINALIZE:
            out_stats->ready_for_finalize++;
            break;
        case SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED:
        default:
            out_stats->queued++;
            break;
        }
    }
    out_stats->pending = out_stats->queued + out_stats->loading + out_stats->ready_for_finalize;
    out_stats->completed = out_stats->ready + out_stats->failed + out_stats->canceled;
    out_stats->progress = out_stats->total > 0 ? (float)out_stats->completed / (float)out_stats->total : 1.0f;
    const Uint64 elapsed_end_counter = out_stats->pending > 0 ? warmup_now_counter() : queue->last_activity_counter;
    out_stats->elapsed_ms = warmup_elapsed_ms(queue->first_request_counter, elapsed_end_counter);
    queue_unlock((slayer3d_game_data_asset_warmup_queue *)queue);
}

bool slayer3d_game_data_asset_warmup_request_state(const slayer3d_game_data_asset_warmup_queue *queue,
                                                   slayer3d_game_data_asset_warmup_kind kind, const char *source_path,
                                                   const char *id, slayer3d_game_data_asset_warmup_state *out_state)
{
    if (out_state != NULL)
        *out_state = SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED;
    if (queue == NULL || id == NULL || out_state == NULL)
        return false;

    bool found = false;
    queue_lock((slayer3d_game_data_asset_warmup_queue *)queue);
    for (int i = 0; i < queue->count; ++i)
    {
        if (!warmup_entry_matches(&queue->entries[i], kind, source_path, id))
            continue;
        *out_state = queue->entries[i].state;
        found = true;
        break;
    }
    queue_unlock((slayer3d_game_data_asset_warmup_queue *)queue);
    return found;
}

static bool service_warmup_entry(slayer3d_game_data_asset_warmup_entry *entry,
                                 const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                 slayer3d_game_data_font_cache *font_cache, slayer3d_game_data_image_cache *image_cache,
                                 slayer3d_game_data_sprite_cache *sprite_cache,
                                 slayer3d_game_data_model_cache *model_cache,
                                 slayer3d_game_data_mesh_primitive_cache *mesh_primitive_cache,
                                 slayer3d_asset_resolver *assets)
{
    if (entry == NULL || entry->id == NULL)
        return false;

    switch (entry->kind)
    {
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_FONT:
        return slayer3d_game_data_find_or_load_font(runtime, font_cache, entry->id) != NULL;
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
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_MESH_PRIMITIVE:
        return slayer3d_game_data_find_or_build_mesh_primitive(mesh_primitive_cache, &entry->mesh_primitive) != NULL;
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_AUDIO_FILE: {
        char resolved_path[4096];
        return slayer3d_game_data_prepare_audio_file((slayer3d_game_data_runtime *)runtime, entry->id, resolved_path,
                                                     (int)sizeof(resolved_path));
    }
    default:
        return false;
    }
}

static bool finalize_prepared_warmup_entry(slayer3d_game_data_asset_warmup_entry *entry,
                                           const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                           slayer3d_game_data_font_cache *font_cache,
                                           slayer3d_game_data_image_cache *image_cache,
                                           slayer3d_game_data_sprite_cache *sprite_cache,
                                           slayer3d_game_data_model_cache *model_cache, void *prepared_payload)
{
    (void)font_cache;
    if (entry == NULL || prepared_payload == NULL)
        return false;

    switch (entry->kind)
    {
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_TEXTURE: {
        if (renderer == NULL)
            return false;
        asset_warmup_prepared_texture *prepared = (asset_warmup_prepared_texture *)prepared_payload;
        const slayer3d_texture2d *texture = NULL;
        const bool ok = slayer3d_texture_cache_insert_prepared(&renderer->texture_cache, prepared->resolved_path,
                                                               &prepared->texture, &texture);
        if (ok)
        {
            prepared->resolved_path = NULL;
            SDL_zero(prepared->texture);
        }
        return ok;
    }
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE: {
        if (runtime == NULL || image_cache == NULL)
            return false;
        slayer3d_game_data_image_asset asset;
        if (!slayer3d_game_data_get_image_asset(runtime, entry->id, &asset))
            return false;
        asset_warmup_prepared_image *prepared = (asset_warmup_prepared_image *)prepared_payload;
        slayer3d_game_data_image_cache_entry *cache_entry = slayer3d_game_data_image_cache_insert_prepared(
            image_cache, asset.id, &prepared->texture, prepared->effect, prepared->effect_delay,
            prepared->effect_duration, &prepared->shader_vertex_source, &prepared->shader_fragment_source);
        return cache_entry != NULL;
    }
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_SPRITE: {
        if (runtime == NULL || sprite_cache == NULL)
            return false;
        slayer3d_game_data_sprite_asset asset;
        if (!slayer3d_game_data_get_sprite_asset(runtime, entry->id, &asset))
            return false;
        asset_warmup_prepared_sprite *prepared = (asset_warmup_prepared_sprite *)prepared_payload;
        slayer3d_game_data_sprite_cache_entry *cache_entry =
            slayer3d_game_data_sprite_cache_insert_prepared(sprite_cache, asset.id, &prepared->sprite);
        return cache_entry != NULL;
    }
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_MODEL: {
        if (runtime == NULL || model_cache == NULL)
            return false;
        slayer3d_game_data_model_asset asset;
        if (!slayer3d_game_data_get_model_asset(runtime, entry->id, &asset))
            return false;
        asset_warmup_prepared_model *prepared = (asset_warmup_prepared_model *)prepared_payload;
        slayer3d_game_data_model_cache_entry *cache_entry =
            slayer3d_game_data_model_cache_insert_prepared(model_cache, asset.id, &prepared->model);
        return cache_entry != NULL;
    }
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
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_AUDIO_FILE:
        return "audio";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_FONT:
        return "font";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_MESH_PRIMITIVE:
        return "mesh primitive";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE:
    default:
        return "image";
    }
}

int slayer3d_game_data_asset_warmup_queue_service(
    slayer3d_game_data_asset_warmup_queue *queue, const slayer3d_game_data_runtime *runtime,
    slayer3d_render_context *renderer, slayer3d_game_data_font_cache *font_cache,
    slayer3d_game_data_image_cache *image_cache, slayer3d_game_data_sprite_cache *sprite_cache,
    slayer3d_game_data_model_cache *model_cache, slayer3d_game_data_mesh_primitive_cache *mesh_primitive_cache,
    slayer3d_asset_resolver *assets, int max_jobs)
{
    if (queue == NULL || runtime == NULL)
        return 0;

    int budget = max_jobs > 0 ? max_jobs : queue->max_jobs_per_frame;
    if (budget <= 0)
        budget = 1;

    const Uint64 service_start_counter = warmup_now_counter();
    int serviced = 0;
    while (serviced < budget)
    {
        enum
        {
            SERVICE_NONE,
            SERVICE_SYNC,
            SERVICE_FINALIZE
        } service_mode = SERVICE_NONE;
        slayer3d_game_data_asset_warmup_entry work_entry;
        void *prepared = NULL;
        int work_index = -1;
        SDL_zero(work_entry);

        queue_lock(queue);
        for (int i = 0; i < queue->count; ++i)
        {
            slayer3d_game_data_asset_warmup_entry *entry = &queue->entries[i];
            if (entry->state == SLAYER3D_GAME_DATA_ASSET_WARMUP_READY_FOR_FINALIZE)
            {
                work_index = i;
                work_entry.kind = entry->kind;
                if (copy_warmup_entry_request(entry, &work_entry.source_path, &work_entry.id))
                {
                    work_entry.generation = entry->generation;
                    work_entry.mesh_primitive = entry->mesh_primitive;
                    prepared = entry->prepared;
                    entry->prepared = NULL;
                    entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING;
                    record_warmup_activity(queue, warmup_now_counter());
                    service_mode = SERVICE_FINALIZE;
                }
                else
                {
                    entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_FAILED;
                    record_warmup_activity(queue, warmup_now_counter());
                }
                break;
            }
            if (entry->state != SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED)
                continue;
            if (worker_can_prepare_entry(queue_worker_state(queue), entry))
                continue;
            work_index = i;
            work_entry.kind = entry->kind;
            if (copy_warmup_entry_request(entry, &work_entry.source_path, &work_entry.id))
            {
                work_entry.generation = entry->generation;
                work_entry.mesh_primitive = entry->mesh_primitive;
                entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING;
                record_warmup_activity(queue, warmup_now_counter());
                service_mode = SERVICE_SYNC;
            }
            else
            {
                entry->state = SLAYER3D_GAME_DATA_ASSET_WARMUP_FAILED;
                record_warmup_activity(queue, warmup_now_counter());
            }
            break;
        }
        queue_unlock(queue);

        if (service_mode == SERVICE_NONE)
            break;

        const bool ok = service_mode == SERVICE_FINALIZE
                            ? finalize_prepared_warmup_entry(&work_entry, runtime, renderer, font_cache, image_cache,
                                                             sprite_cache, model_cache, prepared)
                            : service_warmup_entry(&work_entry, runtime, renderer, font_cache, image_cache,
                                                   sprite_cache, model_cache, mesh_primitive_cache, assets);

        queue_lock(queue);
        if (work_index >= 0 && work_index < queue->count &&
            queue->entries[work_index].state == SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING &&
            queue->entries[work_index].generation == work_entry.generation &&
            warmup_entry_matches(&queue->entries[work_index], work_entry.kind, work_entry.source_path, work_entry.id))
        {
            queue->entries[work_index].state =
                ok ? SLAYER3D_GAME_DATA_ASSET_WARMUP_READY : SLAYER3D_GAME_DATA_ASSET_WARMUP_FAILED;
            record_warmup_activity(queue, warmup_now_counter());
        }
        queue_unlock(queue);
        if (!ok)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to warm %s asset %s: %s",
                        warmup_kind_name(work_entry.kind), work_entry.id != NULL ? work_entry.id : "<null>",
                        SDL_GetError());
        }
        work_entry.prepared = prepared;
        free_warmup_entry_prepared(&work_entry);
        SDL_free(work_entry.source_path);
        SDL_free(work_entry.id);
        serviced++;
    }
    if (serviced > 0)
    {
        const float service_ms = warmup_elapsed_ms(service_start_counter, warmup_now_counter());
        queue_lock(queue);
        queue->service_calls++;
        queue->service_jobs += serviced;
        queue->service_last_ms = service_ms;
        queue->service_total_ms += service_ms;
        if (service_ms > queue->service_max_ms)
            queue->service_max_ms = service_ms;
        queue_unlock(queue);
    }
    return serviced;
}

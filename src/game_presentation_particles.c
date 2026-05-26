/**
 * @file game_presentation_particles.c
 * @brief Particle cache updates and filtered particle rendering.
 */

#include "game_presentation_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/effects.h"
#include "slayer3d/lighting.h"

#include "game_data_internal.h"

typedef struct particle_update_context
{
    slayer3d_game_data_particle_cache *cache;
    float dt;
    bool ok;
} particle_update_context;

static bool ensure_particle_cache_capacity(slayer3d_game_data_particle_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_particle_cache_entry *entries = (slayer3d_game_data_particle_cache_entry *)SDL_realloc(
        cache->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static slayer3d_game_data_particle_cache_entry *find_particle_entry(slayer3d_game_data_particle_cache *cache,
                                                                    const char *entity_name)
{
    if (cache == NULL || entity_name == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].entity_name != NULL && SDL_strcmp(cache->entries[i].entity_name, entity_name) == 0)
            return &cache->entries[i];
    }
    return NULL;
}

static slayer3d_game_data_particle_cache_entry *find_or_create_particle_entry(
    slayer3d_game_data_particle_cache *cache, const slayer3d_game_data_particle_emitter *emitter)
{
    if (cache == NULL || emitter == NULL || emitter->entity_name == NULL)
        return NULL;

    slayer3d_game_data_particle_cache_entry *entry = find_particle_entry(cache, emitter->entity_name);
    if (entry != NULL)
        return entry;

    if (!ensure_particle_cache_capacity(cache, cache->count + 1))
        return NULL;

    entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->entity_name = emitter->entity_name;
    entry->emitter = slayer3d_create_particle_emitter(&emitter->config);
    if (entry->emitter == NULL)
        return NULL;

    ++cache->count;
    return entry;
}

static bool update_particle(void *userdata, const slayer3d_game_data_particle_emitter *emitter)
{
    particle_update_context *context = (particle_update_context *)userdata;
    if (context == NULL || context->cache == NULL || emitter == NULL)
        return false;

    slayer3d_game_data_particle_cache_entry *entry = find_or_create_particle_entry(context->cache, emitter);
    if (entry == NULL)
    {
        context->ok = false;
        return true;
    }

    if (!slayer3d_particle_emitter_set_config(entry->emitter, &emitter->config))
    {
        context->ok = false;
        return true;
    }
    entry->view_space = emitter->view_space;
    entry->draw_emissive = emitter->draw_emissive;
    entry->visible = true;
    slayer3d_particle_emitter_update(entry->emitter, context->dt);
    return true;
}

void slayer3d_game_data_particle_cache_init(slayer3d_game_data_particle_cache *cache)
{
    if (cache != NULL)
        SDL_zero(*cache);
}

void slayer3d_game_data_particle_cache_free(slayer3d_game_data_particle_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
        slayer3d_destroy_particle_emitter(cache->entries[i].emitter);
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

bool slayer3d_game_data_update_particles(const slayer3d_game_data_runtime *runtime,
                                         slayer3d_game_data_particle_cache *cache, float dt)
{
    if (runtime == NULL || cache == NULL)
        return false;

    for (int i = 0; i < cache->count; ++i)
        cache->entries[i].visible = false;

    particle_update_context context;
    SDL_zero(context);
    context.cache = cache;
    context.dt = dt;
    context.ok = true;

    return slayer3d_game_data_for_each_particle_emitter(runtime, update_particle, &context) && context.ok;
}

bool slayer3d_game_data_draw_particles_filtered(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_render_context *renderer,
                                                slayer3d_game_data_particle_cache *cache, bool draw_world_space,
                                                bool draw_view_space)
{
    if (runtime == NULL || renderer == NULL || cache == NULL)
        return false;

    for (int i = 0; i < cache->count; ++i)
    {
        slayer3d_game_data_particle_cache_entry *entry = &cache->entries[i];
        if (!entry->visible || entry->emitter == NULL || (entry->view_space && !draw_view_space) ||
            (!entry->view_space && !draw_world_space) ||
            !slayer3d_game_data_active_scene_has_entity(runtime, entry->entity_name))
        {
            continue;
        }

        slayer3d_set_emissive(renderer, entry->draw_emissive.x, entry->draw_emissive.y, entry->draw_emissive.z);
        slayer3d_draw_particles(renderer, entry->emitter);
        slayer3d_set_emissive(renderer, 0.0f, 0.0f, 0.0f);
    }
    return true;
}

bool slayer3d_game_data_draw_particles(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                       slayer3d_game_data_particle_cache *cache)
{
    return slayer3d_game_data_draw_particles_filtered(runtime, renderer, cache, true, true);
}

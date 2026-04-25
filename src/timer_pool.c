/**
 * @file timer_pool.c
 * @brief Timer pool implementation — flat dynamic array of countdown timers.
 */

#include "sdl3d/timer_pool.h"

#include <SDL3/SDL_stdinc.h>

/* ================================================================== */
/* Internal types                                                     */
/* ================================================================== */

typedef struct timer_entry
{
    int id;
    int signal_id;
    float remaining;
    float interval;
    bool repeating;
    bool active;
} timer_entry;

struct sdl3d_timer_pool
{
    timer_entry *timers;
    int count;
    int capacity;
    int next_id;
};

/* ================================================================== */
/* Internal helpers                                                   */
/* ================================================================== */

static bool ensure_capacity(sdl3d_timer_pool *pool)
{
    if (pool->count < pool->capacity)
        return true;
    int new_cap = pool->capacity < 8 ? 8 : pool->capacity * 2;
    timer_entry *buf = (timer_entry *)SDL_realloc(pool->timers, (size_t)new_cap * sizeof(timer_entry));
    if (buf == NULL)
        return false;
    pool->timers = buf;
    pool->capacity = new_cap;
    return true;
}

/* ================================================================== */
/* Lifecycle                                                          */
/* ================================================================== */

sdl3d_timer_pool *sdl3d_timer_pool_create(void)
{
    sdl3d_timer_pool *pool = (sdl3d_timer_pool *)SDL_calloc(1, sizeof(sdl3d_timer_pool));
    if (pool != NULL)
        pool->next_id = 1;
    return pool;
}

void sdl3d_timer_pool_destroy(sdl3d_timer_pool *pool)
{
    if (pool == NULL)
        return;
    SDL_free(pool->timers);
    SDL_free(pool);
}

/* ================================================================== */
/* Timer management                                                   */
/* ================================================================== */

int sdl3d_timer_start(sdl3d_timer_pool *pool, float delay, int signal_id, bool repeating, float interval)
{
    if (pool == NULL || delay <= 0.0f)
        return 0;
    if (repeating && interval <= 0.0f)
        return 0;

    if (!ensure_capacity(pool))
        return 0;

    int id = pool->next_id++;
    timer_entry *t = &pool->timers[pool->count++];
    t->id = id;
    t->signal_id = signal_id;
    t->remaining = delay;
    t->interval = interval;
    t->repeating = repeating;
    t->active = true;
    return id;
}

void sdl3d_timer_cancel(sdl3d_timer_pool *pool, int timer_id)
{
    if (pool == NULL || timer_id <= 0)
        return;
    for (int i = 0; i < pool->count; ++i)
    {
        if (pool->timers[i].active && pool->timers[i].id == timer_id)
        {
            pool->timers[i].active = false;
            return;
        }
    }
}

/* ================================================================== */
/* Per-frame update                                                   */
/* ================================================================== */

void sdl3d_timer_pool_update(sdl3d_timer_pool *pool, sdl3d_signal_bus *bus, float dt)
{
    if (pool == NULL || bus == NULL || dt < 0.0f)
        return;

    for (int i = 0; i < pool->count; ++i)
    {
        timer_entry *t = &pool->timers[i];
        if (!t->active)
            continue;

        t->remaining -= dt;
        if (t->remaining <= 0.0f)
        {
            sdl3d_signal_emit(bus, t->signal_id, NULL);
            if (t->repeating)
            {
                t->remaining += t->interval;
                /* Guard against spiral-of-death: if remaining is still
                 * negative after one reset, clamp to the interval. This
                 * means at most one firing per update call. */
                if (t->remaining <= 0.0f)
                    t->remaining = t->interval;
            }
            else
            {
                t->active = false;
            }
        }
    }

    /* Compact inactive timers. */
    int write = 0;
    for (int read = 0; read < pool->count; ++read)
    {
        if (pool->timers[read].active)
        {
            if (write != read)
                pool->timers[write] = pool->timers[read];
            write++;
        }
    }
    pool->count = write;
}

/* ================================================================== */
/* Query                                                              */
/* ================================================================== */

int sdl3d_timer_pool_active_count(const sdl3d_timer_pool *pool)
{
    if (pool == NULL)
        return 0;
    int count = 0;
    for (int i = 0; i < pool->count; ++i)
    {
        if (pool->timers[i].active)
            count++;
    }
    return count;
}

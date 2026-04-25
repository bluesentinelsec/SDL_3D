/*
 * SDL3D time system — frame timing, time scaling, fixed timestep.
 */

#include "sdl3d/time.h"

#include <SDL3/SDL_timer.h>

/* ------------------------------------------------------------------ */
/* Internal state                                                      */
/* ------------------------------------------------------------------ */

#define SDL3D_TIME_MAX_FIXED_STEPS 8
#define SDL3D_TIME_DEFAULT_FIXED_DT (1.0f / 60.0f)
#define SDL3D_TIME_MAX_FRAME_DT 0.25f /* clamp to avoid spiral of death */

static struct
{
    Uint64 last_ticks;
    float delta_time;     /* unscaled */
    float scaled_dt;      /* scaled */
    float elapsed_game;   /* accumulated scaled time */
    float elapsed_real;   /* accumulated real time */
    float time_scale;     /* multiplier */
    float fixed_dt;       /* fixed timestep interval */
    float fixed_accum;    /* accumulator for fixed steps */
    int fixed_step_count; /* steps this frame */
    float fixed_alpha;    /* interpolation factor */
    bool initialized;
} s_time = {
    .time_scale = 1.0f,
    .fixed_dt = SDL3D_TIME_DEFAULT_FIXED_DT,
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void sdl3d_time_update(void)
{
    Uint64 now = SDL_GetPerformanceCounter();

    if (!s_time.initialized)
    {
        s_time.last_ticks = now;
        s_time.initialized = true;
        s_time.delta_time = 0.0f;
        s_time.scaled_dt = 0.0f;
        s_time.fixed_step_count = 0;
        s_time.fixed_alpha = 0.0f;
        return;
    }

    float raw_dt = (float)(now - s_time.last_ticks) / (float)SDL_GetPerformanceFrequency();
    s_time.last_ticks = now;

    /* Clamp to avoid spiral of death after breakpoints or long stalls. */
    if (raw_dt > SDL3D_TIME_MAX_FRAME_DT)
        raw_dt = SDL3D_TIME_MAX_FRAME_DT;
    if (raw_dt < 0.0f)
        raw_dt = 0.0f;

    s_time.delta_time = raw_dt;
    s_time.scaled_dt = raw_dt * s_time.time_scale;
    s_time.elapsed_real += raw_dt;
    s_time.elapsed_game += s_time.scaled_dt;

    /* Fixed timestep accumulator. */
    s_time.fixed_accum += s_time.scaled_dt;
    s_time.fixed_step_count = 0;

    if (s_time.fixed_dt > 0.0f)
    {
        while (s_time.fixed_accum >= s_time.fixed_dt && s_time.fixed_step_count < SDL3D_TIME_MAX_FIXED_STEPS)
        {
            s_time.fixed_accum -= s_time.fixed_dt;
            s_time.fixed_step_count++;
        }

        /* Clamp leftover to prevent unbounded accumulation. */
        if (s_time.fixed_accum > s_time.fixed_dt)
            s_time.fixed_accum = s_time.fixed_dt;

        s_time.fixed_alpha = s_time.fixed_accum / s_time.fixed_dt;
    }
    else
    {
        s_time.fixed_alpha = 0.0f;
    }
}

float sdl3d_time_get_delta_time(void)
{
    return s_time.scaled_dt;
}

float sdl3d_time_get_unscaled_delta_time(void)
{
    return s_time.delta_time;
}

float sdl3d_time_get_time(void)
{
    return s_time.elapsed_game;
}

float sdl3d_time_get_real_time(void)
{
    return s_time.elapsed_real;
}

void sdl3d_time_set_scale(float scale)
{
    s_time.time_scale = (scale < 0.0f) ? 0.0f : scale;
}

float sdl3d_time_get_scale(void)
{
    return s_time.time_scale;
}

void sdl3d_time_set_fixed_delta_time(float dt)
{
    if (dt > 0.0f)
        s_time.fixed_dt = dt;
}

float sdl3d_time_get_fixed_delta_time(void)
{
    return s_time.fixed_dt;
}

int sdl3d_time_get_fixed_step_count(void)
{
    return s_time.fixed_step_count;
}

float sdl3d_time_get_fixed_interpolation(void)
{
    return s_time.fixed_alpha;
}

void sdl3d_time_reset(void)
{
    s_time.last_ticks = SDL_GetPerformanceCounter();
    s_time.delta_time = 0.0f;
    s_time.scaled_dt = 0.0f;
    s_time.elapsed_game = 0.0f;
    s_time.elapsed_real = 0.0f;
    s_time.time_scale = 1.0f;
    s_time.fixed_dt = SDL3D_TIME_DEFAULT_FIXED_DT;
    s_time.fixed_accum = 0.0f;
    s_time.fixed_step_count = 0;
    s_time.fixed_alpha = 0.0f;
    s_time.initialized = false;
}

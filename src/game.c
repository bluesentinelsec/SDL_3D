#define SDL_MAIN_HANDLED
#include "sdl3d/game.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/time.h"

#define SDL3D_GAME_DEFAULT_WIDTH 1280
#define SDL3D_GAME_DEFAULT_HEIGHT 720
#define SDL3D_GAME_DEFAULT_FIXED_DT (1.0f / 60.0f)
#define SDL3D_GAME_DEFAULT_MAX_TICKS 8

static float sdl3d_game_fixed_dt(const sdl3d_game_config *config)
{
    return (config != NULL && config->tick_rate > 0.0f) ? config->tick_rate : SDL3D_GAME_DEFAULT_FIXED_DT;
}

static int sdl3d_game_max_ticks_per_frame(const sdl3d_game_config *config)
{
    return (config != NULL && config->max_ticks_per_frame > 0) ? config->max_ticks_per_frame
                                                               : SDL3D_GAME_DEFAULT_MAX_TICKS;
}

static bool sdl3d_game_create_context(const sdl3d_game_config *config, sdl3d_game_context *ctx)
{
    sdl3d_window_config window_config;

    sdl3d_init_window_config(&window_config);
    window_config.width = (config != NULL && config->width > 0) ? config->width : SDL3D_GAME_DEFAULT_WIDTH;
    window_config.height = (config != NULL && config->height > 0) ? config->height : SDL3D_GAME_DEFAULT_HEIGHT;
    window_config.title = (config != NULL && config->title != NULL) ? config->title : "SDL3D";
    window_config.backend = (config != NULL) ? config->backend : SDL3D_BACKEND_AUTO;

    if (!sdl3d_create_window(&window_config, &ctx->window, &ctx->renderer))
    {
        return false;
    }

    ctx->registry = sdl3d_actor_registry_create();
    ctx->bus = sdl3d_signal_bus_create();
    ctx->timers = sdl3d_timer_pool_create();
    ctx->input = sdl3d_input_create();
    if (!sdl3d_audio_create(&ctx->audio))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio disabled: %s", SDL_GetError());
        SDL_ClearError();
        ctx->audio = NULL;
    }

    if (ctx->registry == NULL || ctx->bus == NULL || ctx->timers == NULL || ctx->input == NULL)
    {
        SDL_OutOfMemory();
        return false;
    }

    return true;
}

static void sdl3d_game_cleanup_context(sdl3d_game_context *ctx)
{
    sdl3d_timer_pool_destroy(ctx->timers);
    sdl3d_signal_bus_destroy(ctx->bus);
    sdl3d_actor_registry_destroy(ctx->registry);
    sdl3d_input_destroy(ctx->input);
    sdl3d_audio_destroy(ctx->audio);
    sdl3d_destroy_window(ctx->window, ctx->renderer);
    SDL_zero(*ctx);
}

static float sdl3d_game_frame_delta(Uint64 now, Uint64 last)
{
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    if (frequency == 0 || now < last)
    {
        return 0.0f;
    }

    return (float)(now - last) / (float)frequency;
}

int sdl3d_run_game(const sdl3d_game_config *config, const sdl3d_game_callbacks *callbacks, void *userdata)
{
    sdl3d_game_context ctx;
    const float fixed_dt = sdl3d_game_fixed_dt(config);
    const int max_ticks_per_frame = sdl3d_game_max_ticks_per_frame(config);
    float accumulator = 0.0f;
    Uint64 last_counter = 0;
    int result = 0;

    SDL_zero(ctx);
    SDL_SetMainReady();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        return 1;
    }

    if (!sdl3d_game_create_context(config, &ctx))
    {
        sdl3d_game_cleanup_context(&ctx);
        SDL_Quit();
        return 1;
    }

    sdl3d_time_reset();

    if (callbacks != NULL && callbacks->init != NULL && !callbacks->init(&ctx, userdata))
    {
        sdl3d_game_cleanup_context(&ctx);
        SDL_Quit();
        return 1;
    }

    last_counter = SDL_GetPerformanceCounter();

    while (!ctx.quit_requested)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            sdl3d_input_process_event(ctx.input, &event);

            if (event.type == SDL_EVENT_QUIT)
            {
                ctx.quit_requested = true;
                break;
            }

            if (callbacks != NULL && callbacks->event != NULL && !callbacks->event(&ctx, userdata, &event))
            {
                ctx.quit_requested = true;
                break;
            }
        }

        if (ctx.quit_requested)
        {
            break;
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        const float frame_dt = sdl3d_game_frame_delta(now, last_counter);
        int ticks_this_frame = 0;
        last_counter = now;

        ctx.real_time += frame_dt;
        sdl3d_time_update();
        sdl3d_audio_update(ctx.audio, frame_dt);

        if (ctx.paused)
        {
            sdl3d_input_update(ctx.input, ctx.tick_count);
            if (callbacks != NULL && callbacks->pause_tick != NULL)
            {
                callbacks->pause_tick(&ctx, userdata, frame_dt);
            }
        }
        else
        {
            accumulator += frame_dt;

            while (!ctx.quit_requested && !ctx.paused && accumulator >= fixed_dt &&
                   ticks_this_frame < max_ticks_per_frame)
            {
                sdl3d_input_update(ctx.input, ctx.tick_count);
                sdl3d_timer_pool_update(ctx.timers, ctx.bus, fixed_dt);

                if (callbacks != NULL && callbacks->tick != NULL)
                {
                    callbacks->tick(&ctx, userdata, fixed_dt);
                }

                ctx.time += fixed_dt;
                ctx.tick_count++;
                accumulator -= fixed_dt;
                ticks_this_frame++;
            }

            if (ticks_this_frame == max_ticks_per_frame && accumulator >= fixed_dt)
            {
                accumulator = SDL_fmodf(accumulator, fixed_dt);
            }
        }

        if (ctx.quit_requested)
        {
            break;
        }

        if (callbacks != NULL && callbacks->render != NULL)
        {
            float alpha = ctx.paused ? 0.0f : accumulator / fixed_dt;
            if (alpha > 1.0f)
            {
                alpha = 1.0f;
            }
            callbacks->render(&ctx, userdata, alpha);
        }

        if (!sdl3d_present_render_context(ctx.renderer))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL3D present failed: %s", SDL_GetError());
            result = 1;
            ctx.quit_requested = true;
        }
    }

    if (callbacks != NULL && callbacks->shutdown != NULL)
    {
        callbacks->shutdown(&ctx, userdata);
    }

    sdl3d_game_cleanup_context(&ctx);
    SDL_Quit();
    return result;
}

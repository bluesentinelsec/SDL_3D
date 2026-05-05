#define SDL_MAIN_HANDLED
#include "sdl3d/game.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/logic.h"
#include "sdl3d/time.h"

#define SDL3D_GAME_DEFAULT_WIDTH 1280
#define SDL3D_GAME_DEFAULT_HEIGHT 720
#define SDL3D_GAME_DEFAULT_FIXED_DT (1.0f / 60.0f)
#define SDL3D_GAME_DEFAULT_MAX_TICKS 8

#if defined(SDL3D_PRODUCTION_BUILD)
#define SDL3D_GAME_DEFAULT_WINDOW_MODE SDL3D_WINDOW_MODE_FULLSCREEN_BORDERLESS
#else
#define SDL3D_GAME_DEFAULT_WINDOW_MODE SDL3D_WINDOW_MODE_WINDOWED
#endif

#if defined(__ANDROID__) || defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) ||                                 \
    defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__)
#define SDL3D_GAME_ENABLE_GLOBAL_TEXT_INPUT 0
#else
#define SDL3D_GAME_ENABLE_GLOBAL_TEXT_INPUT 1
#endif

struct sdl3d_game_session
{
    sdl3d_actor_registry *registry;
    sdl3d_signal_bus *bus;
    sdl3d_timer_pool *timers;
    sdl3d_logic_world *logic;
    sdl3d_input_manager *input;
    sdl3d_audio_engine *audio;
    void *world;
    char *profile_name;
    unsigned int owned_services;
    float time;
    int tick_count;
};

static bool session_owns_service(const sdl3d_game_session_desc *desc, unsigned int service)
{
    return desc != NULL && (desc->create_services & service) != 0;
}

static bool session_has_ambiguous_ownership(const sdl3d_game_session_desc *desc)
{
    if (desc == NULL)
    {
        return false;
    }

    return (session_owns_service(desc, SDL3D_GAME_SESSION_SERVICE_REGISTRY) && desc->registry != NULL) ||
           (session_owns_service(desc, SDL3D_GAME_SESSION_SERVICE_SIGNAL_BUS) && desc->bus != NULL) ||
           (session_owns_service(desc, SDL3D_GAME_SESSION_SERVICE_TIMER_POOL) && desc->timers != NULL) ||
           (session_owns_service(desc, SDL3D_GAME_SESSION_SERVICE_LOGIC_WORLD) && desc->logic != NULL) ||
           (session_owns_service(desc, SDL3D_GAME_SESSION_SERVICE_INPUT) && desc->input != NULL) ||
           (session_owns_service(desc, SDL3D_GAME_SESSION_SERVICE_AUDIO) && desc->audio != NULL);
}

static bool session_copy_profile_name(sdl3d_game_session *session, const char *profile_name)
{
    if (profile_name == NULL)
    {
        return true;
    }

    session->profile_name = SDL_strdup(profile_name);
    if (session->profile_name == NULL)
    {
        SDL_OutOfMemory();
        return false;
    }

    return true;
}

static void session_configure_logic_targets(sdl3d_game_session *session)
{
    if (session == NULL || session->logic == NULL || session->registry == NULL)
    {
        return;
    }

    sdl3d_logic_target_context context;
    SDL_zero(context);
    context.registry = session->registry;
    sdl3d_logic_world_set_target_context(session->logic, &context);
}

void sdl3d_game_session_desc_init(sdl3d_game_session_desc *desc)
{
    if (desc == NULL)
    {
        return;
    }

    SDL_zero(*desc);
    desc->create_services = SDL3D_GAME_SESSION_SERVICE_CORE;
}

bool sdl3d_game_session_create(const sdl3d_game_session_desc *desc, sdl3d_game_session **out_session)
{
    sdl3d_game_session_desc defaults;
    const sdl3d_game_session_desc *effective = desc;
    sdl3d_game_session *session = NULL;

    if (out_session == NULL)
    {
        return false;
    }
    *out_session = NULL;

    if (effective == NULL)
    {
        sdl3d_game_session_desc_init(&defaults);
        effective = &defaults;
    }

    if (session_has_ambiguous_ownership(effective))
    {
        SDL_SetError("Game session service cannot be both borrowed and created.");
        return false;
    }

    session = (sdl3d_game_session *)SDL_calloc(1, sizeof(*session));
    if (session == NULL)
    {
        SDL_OutOfMemory();
        return false;
    }

    session->owned_services = effective->create_services;
    session->registry = effective->registry;
    session->bus = effective->bus;
    session->timers = effective->timers;
    session->logic = effective->logic;
    session->input = effective->input;
    session->audio = effective->audio;
    session->world = effective->world;

    if (!session_copy_profile_name(session, effective->profile_name))
    {
        sdl3d_game_session_destroy(session);
        return false;
    }

    if (session_owns_service(effective, SDL3D_GAME_SESSION_SERVICE_REGISTRY))
    {
        session->registry = sdl3d_actor_registry_create();
        if (session->registry == NULL)
        {
            sdl3d_game_session_destroy(session);
            return false;
        }
    }

    if (session_owns_service(effective, SDL3D_GAME_SESSION_SERVICE_SIGNAL_BUS))
    {
        session->bus = sdl3d_signal_bus_create();
        if (session->bus == NULL)
        {
            sdl3d_game_session_destroy(session);
            return false;
        }
    }

    if (session_owns_service(effective, SDL3D_GAME_SESSION_SERVICE_TIMER_POOL))
    {
        session->timers = sdl3d_timer_pool_create();
        if (session->timers == NULL)
        {
            sdl3d_game_session_destroy(session);
            return false;
        }
    }

    if (session_owns_service(effective, SDL3D_GAME_SESSION_SERVICE_INPUT))
    {
        session->input = sdl3d_input_create();
        if (session->input == NULL)
        {
            sdl3d_game_session_destroy(session);
            return false;
        }
        sdl3d_input_bind_ui_defaults(session->input);
    }

    if (session_owns_service(effective, SDL3D_GAME_SESSION_SERVICE_AUDIO))
    {
        if (!sdl3d_audio_create(&session->audio))
        {
            if (effective->optional_audio)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio disabled: %s", SDL_GetError());
                SDL_ClearError();
                session->owned_services &= ~(unsigned int)SDL3D_GAME_SESSION_SERVICE_AUDIO;
            }
            else
            {
                sdl3d_game_session_destroy(session);
                return false;
            }
        }
    }

    if (session_owns_service(effective, SDL3D_GAME_SESSION_SERVICE_LOGIC_WORLD))
    {
        if (session->bus == NULL)
        {
            SDL_SetError("Game session logic world requires a signal bus.");
            sdl3d_game_session_destroy(session);
            return false;
        }

        session->logic = sdl3d_logic_world_create(session->bus, session->timers);
        if (session->logic == NULL)
        {
            sdl3d_game_session_destroy(session);
            return false;
        }
    }

    session_configure_logic_targets(session);
    *out_session = session;
    return true;
}

void sdl3d_game_session_destroy(sdl3d_game_session *session)
{
    if (session == NULL)
    {
        return;
    }

    if ((session->owned_services & SDL3D_GAME_SESSION_SERVICE_LOGIC_WORLD) != 0)
    {
        sdl3d_logic_world_destroy(session->logic);
    }
    if ((session->owned_services & SDL3D_GAME_SESSION_SERVICE_AUDIO) != 0)
    {
        sdl3d_audio_destroy(session->audio);
    }
    if ((session->owned_services & SDL3D_GAME_SESSION_SERVICE_INPUT) != 0)
    {
        sdl3d_input_destroy(session->input);
    }
    if ((session->owned_services & SDL3D_GAME_SESSION_SERVICE_TIMER_POOL) != 0)
    {
        sdl3d_timer_pool_destroy(session->timers);
    }
    if ((session->owned_services & SDL3D_GAME_SESSION_SERVICE_SIGNAL_BUS) != 0)
    {
        sdl3d_signal_bus_destroy(session->bus);
    }
    if ((session->owned_services & SDL3D_GAME_SESSION_SERVICE_REGISTRY) != 0)
    {
        sdl3d_actor_registry_destroy(session->registry);
    }

    SDL_free(session->profile_name);
    SDL_free(session);
}

bool sdl3d_game_session_begin_frame(sdl3d_game_session *session, float real_dt)
{
    if (session == NULL)
    {
        return false;
    }

    sdl3d_audio_update(session->audio, real_dt > 0.0f ? real_dt : 0.0f);
    return true;
}

bool sdl3d_game_session_update_input(sdl3d_game_session *session)
{
    if (session == NULL || session->input == NULL)
    {
        return false;
    }

    sdl3d_input_update(session->input, session->tick_count);
    return true;
}

bool sdl3d_game_session_begin_tick(sdl3d_game_session *session, float dt)
{
    if (session == NULL)
    {
        return false;
    }

    const float clamped_dt = dt > 0.0f ? dt : 0.0f;
    sdl3d_game_session_update_input(session);
    sdl3d_timer_pool_update(session->timers, session->bus, clamped_dt);
    return true;
}

bool sdl3d_game_session_end_tick(sdl3d_game_session *session, float dt)
{
    if (session == NULL)
    {
        return false;
    }

    const float clamped_dt = dt > 0.0f ? dt : 0.0f;
    session->time += clamped_dt;
    session->tick_count++;
    return true;
}

bool sdl3d_game_session_tick(sdl3d_game_session *session, float dt)
{
    if (!sdl3d_game_session_begin_tick(session, dt))
    {
        return false;
    }
    return sdl3d_game_session_end_tick(session, dt);
}

sdl3d_actor_registry *sdl3d_game_session_get_registry(const sdl3d_game_session *session)
{
    return session != NULL ? session->registry : NULL;
}

sdl3d_signal_bus *sdl3d_game_session_get_signal_bus(const sdl3d_game_session *session)
{
    return session != NULL ? session->bus : NULL;
}

sdl3d_timer_pool *sdl3d_game_session_get_timer_pool(const sdl3d_game_session *session)
{
    return session != NULL ? session->timers : NULL;
}

sdl3d_logic_world *sdl3d_game_session_get_logic_world(const sdl3d_game_session *session)
{
    return session != NULL ? session->logic : NULL;
}

sdl3d_input_manager *sdl3d_game_session_get_input(const sdl3d_game_session *session)
{
    return session != NULL ? session->input : NULL;
}

sdl3d_audio_engine *sdl3d_game_session_get_audio(const sdl3d_game_session *session)
{
    return session != NULL ? session->audio : NULL;
}

void *sdl3d_game_session_get_world(const sdl3d_game_session *session)
{
    return session != NULL ? session->world : NULL;
}

const char *sdl3d_game_session_get_profile_name(const sdl3d_game_session *session)
{
    return session != NULL ? session->profile_name : NULL;
}

float sdl3d_game_session_get_time(const sdl3d_game_session *session)
{
    return session != NULL ? session->time : 0.0f;
}

int sdl3d_game_session_get_tick_count(const sdl3d_game_session *session)
{
    return session != NULL ? session->tick_count : 0;
}

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
    sdl3d_game_session_desc session_desc;
    const int logical_width =
        (config != NULL && config->logical_width > 0) ? config->logical_width : SDL3D_GAME_DEFAULT_WIDTH;
    const int logical_height =
        (config != NULL && config->logical_height > 0) ? config->logical_height : SDL3D_GAME_DEFAULT_HEIGHT;

    sdl3d_init_window_config(&window_config);
    window_config.width = (config != NULL && config->width > 0) ? config->width : logical_width;
    window_config.height = (config != NULL && config->height > 0) ? config->height : logical_height;
    window_config.logical_width = logical_width;
    window_config.logical_height = logical_height;
    window_config.title = (config != NULL && config->title != NULL) ? config->title : "SDL3D";
    window_config.icon_path = config != NULL ? config->icon_path : NULL;
    window_config.backend = (config != NULL) ? config->backend : SDL3D_BACKEND_AUTO;
    window_config.display_mode = (config != NULL && config->display_mode != SDL3D_WINDOW_MODE_DEFAULT)
                                     ? config->display_mode
                                     : SDL3D_GAME_DEFAULT_WINDOW_MODE;
    if (config != NULL && config->vsync != 0)
        window_config.vsync = config->vsync > 0;
    if (config != NULL && config->maximized != 0)
        window_config.maximized = config->maximized > 0;
    else
        window_config.maximized = true;

    if (!sdl3d_create_window(&window_config, &ctx->window, &ctx->renderer))
    {
        return false;
    }

    sdl3d_game_session_desc_init(&session_desc);
    if (config != NULL && config->enable_audio)
    {
        session_desc.create_services |= SDL3D_GAME_SESSION_SERVICE_AUDIO;
        session_desc.optional_audio = true;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio requested by game config");
    }
    else
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio not requested by game config");
    }
    if (!sdl3d_game_session_create(&session_desc, &ctx->session))
    {
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio engine %s",
                sdl3d_game_session_get_audio(ctx->session) != NULL ? "created" : "unavailable");

    return true;
}

static void sdl3d_game_cleanup_context(sdl3d_game_context *ctx)
{
    sdl3d_game_session_destroy(ctx->session);
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
    const bool profile_frames = SDL_getenv("SDL3D_PROFILE_FRAMES") != NULL;
    float accumulator = 0.0f;
    Uint64 last_counter = 0;
    Uint64 profile_last_counter = 0;
    double profile_poll_ms = 0.0;
    double profile_tick_ms = 0.0;
    double profile_render_ms = 0.0;
    double profile_present_ms = 0.0;
    double profile_frame_ms = 0.0;
    int profile_frames_count = 0;
    int profile_ticks_count = 0;
    int profile_max_ticks = 0;
    int result = 0;

    SDL_zero(ctx);
    SDL_SetMainReady();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC))
    {
        return 1;
    }

    if (!sdl3d_game_create_context(config, &ctx))
    {
        sdl3d_game_cleanup_context(&ctx);
        SDL_Quit();
        return 1;
    }
#if SDL3D_GAME_ENABLE_GLOBAL_TEXT_INPUT
    SDL_StartTextInput(ctx.window);
#endif

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
        const Uint64 frame_start_counter = SDL_GetPerformanceCounter();
        Uint64 poll_start_counter = frame_start_counter;
        Uint64 poll_end_counter;
        Uint64 tick_start_counter;
        Uint64 tick_end_counter;
        Uint64 render_start_counter;
        Uint64 render_end_counter;
        Uint64 present_start_counter;
        Uint64 present_end_counter;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ctx.input_event_consumed = false;

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

            if (!ctx.input_event_consumed)
            {
                sdl3d_input_process_event(sdl3d_game_session_get_input(ctx.session), &event);
            }
        }
        poll_end_counter = SDL_GetPerformanceCounter();

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
        sdl3d_game_session_begin_frame(ctx.session, frame_dt);

        if (ctx.paused)
        {
            tick_start_counter = SDL_GetPerformanceCounter();
            sdl3d_game_session_update_input(ctx.session);
            if (callbacks != NULL && callbacks->pause_tick != NULL)
            {
                callbacks->pause_tick(&ctx, userdata, frame_dt);
            }
            tick_end_counter = SDL_GetPerformanceCounter();
        }
        else
        {
            tick_start_counter = SDL_GetPerformanceCounter();
            accumulator += frame_dt;

            while (!ctx.quit_requested && !ctx.paused && accumulator >= fixed_dt &&
                   ticks_this_frame < max_ticks_per_frame)
            {
                sdl3d_game_session_begin_tick(ctx.session, fixed_dt);

                if (callbacks != NULL && callbacks->tick != NULL)
                {
                    callbacks->tick(&ctx, userdata, fixed_dt);
                }

                sdl3d_game_session_end_tick(ctx.session, fixed_dt);
                accumulator -= fixed_dt;
                ticks_this_frame++;
            }

            if (ticks_this_frame == max_ticks_per_frame && accumulator >= fixed_dt)
            {
                accumulator = SDL_fmodf(accumulator, fixed_dt);
            }
            tick_end_counter = SDL_GetPerformanceCounter();
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
            render_start_counter = SDL_GetPerformanceCounter();
            callbacks->render(&ctx, userdata, alpha);
            render_end_counter = SDL_GetPerformanceCounter();
        }
        else
        {
            render_start_counter = SDL_GetPerformanceCounter();
            render_end_counter = render_start_counter;
        }

        present_start_counter = SDL_GetPerformanceCounter();
        if (!sdl3d_present_render_context(ctx.renderer))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL3D present failed: %s", SDL_GetError());
            result = 1;
            ctx.quit_requested = true;
        }
        present_end_counter = SDL_GetPerformanceCounter();

        if (profile_frames)
        {
            const double inv_freq_ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
            profile_poll_ms += (double)(poll_end_counter - poll_start_counter) * inv_freq_ms;
            profile_tick_ms += (double)(tick_end_counter - tick_start_counter) * inv_freq_ms;
            profile_render_ms += (double)(render_end_counter - render_start_counter) * inv_freq_ms;
            profile_present_ms += (double)(present_end_counter - present_start_counter) * inv_freq_ms;
            profile_frame_ms += (double)(present_end_counter - frame_start_counter) * inv_freq_ms;
            profile_frames_count++;
            profile_ticks_count += ticks_this_frame;
            if (ticks_this_frame > profile_max_ticks)
            {
                profile_max_ticks = ticks_this_frame;
            }

            if (profile_last_counter == 0)
            {
                profile_last_counter = present_end_counter;
            }
            else if ((double)(present_end_counter - profile_last_counter) / (double)SDL_GetPerformanceFrequency() >=
                     1.0)
            {
                const double frames = profile_frames_count > 0 ? (double)profile_frames_count : 1.0;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "SDL3D profile: fps=%.1f frame=%.2fms poll=%.2f tick=%.2f render=%.2f present=%.2f "
                            "ticks/frame=%.2f max_ticks=%d",
                            frames * (double)SDL_GetPerformanceFrequency() /
                                (double)(present_end_counter - profile_last_counter),
                            profile_frame_ms / frames, profile_poll_ms / frames, profile_tick_ms / frames,
                            profile_render_ms / frames, profile_present_ms / frames, profile_ticks_count / frames,
                            profile_max_ticks);
                profile_last_counter = present_end_counter;
                profile_poll_ms = 0.0;
                profile_tick_ms = 0.0;
                profile_render_ms = 0.0;
                profile_present_ms = 0.0;
                profile_frame_ms = 0.0;
                profile_frames_count = 0;
                profile_ticks_count = 0;
                profile_max_ticks = 0;
            }
        }
    }

    if (callbacks != NULL && callbacks->shutdown != NULL)
    {
        callbacks->shutdown(&ctx, userdata);
    }

#if SDL3D_GAME_ENABLE_GLOBAL_TEXT_INPUT
    SDL_StopTextInput(ctx.window);
#endif
    sdl3d_game_cleanup_context(&ctx);
    SDL_Quit();
    return result;
}

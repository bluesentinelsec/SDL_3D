/*
 * Doom-style level demo using the SDL3D managed game loop.
 *
 * Gameplay stays in subsystem modules. The managed loop owns SDL init,
 * event polling, fixed-timestep ticking, presentation, and engine cleanup.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/font.h"
#include "sdl3d/game.h"
#include "sdl3d/sdl3d.h"
#include "sdl3d/ui.h"

#include "backend.h"
#include "entities.h"
#include "level_data.h"
#include "player.h"
#include "renderer.h"

#define SIG_FADE_OUT_DONE 2001

typedef struct doom_state
{
    level_data level;
    entities ent;
    player_state player;
    render_state render;
    sdl3d_transition transition;
    sdl3d_font debug_font;
    sdl3d_ui_context *ui;
    sdl3d_demo_player *demo_player;
    sdl3d_backend current_backend;
    doom_render_profile render_profile;
    int action_pause;
    int action_menu;
    int action_toggle_lighting;
    int action_toggle_lightmaps;
    int action_toggle_debug_stats;
    int action_toggle_portal_culling;
    int action_switch_backend;
    int action_render_profile[DOOM_RENDER_PROFILE_COUNT];
    float pause_flash_timer;
    bool has_font;
    bool level_ready;
    bool entities_ready;
    bool quit_pending;
} doom_state;

static sdl3d_actor_registry *ctx_registry(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_registry(ctx != NULL ? ctx->session : NULL);
}

static sdl3d_signal_bus *ctx_bus(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_signal_bus(ctx != NULL ? ctx->session : NULL);
}

static sdl3d_input_manager *ctx_input(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_input(ctx != NULL ? ctx->session : NULL);
}

static void doom_state_cleanup(doom_state *state)
{
    render_state_free(&state->render);
    if (state->entities_ready)
    {
        entities_free(&state->ent);
        state->entities_ready = false;
    }
    if (state->level_ready)
    {
        level_data_free(&state->level);
        state->level_ready = false;
    }
    sdl3d_ui_destroy(state->ui);
    state->ui = NULL;
    sdl3d_demo_playback_free(state->demo_player);
    state->demo_player = NULL;
    if (state->has_font)
    {
        sdl3d_free_font(&state->debug_font);
        state->has_font = false;
    }
}

static void stop_demo_playback(sdl3d_game_context *ctx, doom_state *state)
{
    if (state->demo_player == NULL)
    {
        return;
    }

    if (ctx != NULL && ctx_input(ctx) != NULL)
    {
        sdl3d_demo_playback_stop(ctx_input(ctx));
    }
    sdl3d_demo_playback_free(state->demo_player);
    state->demo_player = NULL;
}

static void apply_window_defaults(sdl3d_game_context *ctx, doom_state *state)
{
    backend_apply_defaults(ctx->renderer, state->render_profile);
    SDL_SetWindowRelativeMouseMode(ctx->window, true);
}

static void on_fade_out_done(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    (void)signal_id;
    (void)payload;
    sdl3d_game_context *ctx = (sdl3d_game_context *)userdata;
    ctx->quit_requested = true;
}

static void start_quit_fade(sdl3d_game_context *ctx, doom_state *state)
{
    if (state->quit_pending)
    {
        return;
    }

    state->quit_pending = true;
    sdl3d_transition_start(&state->transition, SDL3D_TRANSITION_FADE, SDL3D_TRANSITION_OUT, (sdl3d_color){0, 0, 0, 255},
                           0.5f, SIG_FADE_OUT_DONE);
}

static void toggle_pause(sdl3d_game_context *ctx, doom_state *state)
{
    if (state->quit_pending)
    {
        return;
    }

    ctx->paused = !ctx->paused;
    state->pause_flash_timer = 0.0f;
}

static int bind_doom_key_action(sdl3d_input_manager *input, const char *name, SDL_Scancode key)
{
    int action = sdl3d_input_register_action(input, name);
    sdl3d_input_bind_key(input, action, key);
    return action;
}

static void bind_doom_actions(sdl3d_input_manager *input, doom_state *state)
{
    static const SDL_Scancode profile_keys[DOOM_RENDER_PROFILE_COUNT] = {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
    };
    sdl3d_input_bind_fps_defaults(input);
    state->action_pause = sdl3d_input_find_action(input, "pause");
    state->action_menu = sdl3d_input_find_action(input, "menu");
    state->action_toggle_lighting = bind_doom_key_action(input, "toggle_lighting", SDL_SCANCODE_L);
    state->action_toggle_lightmaps = bind_doom_key_action(input, "toggle_lightmaps", SDL_SCANCODE_M);
    state->action_toggle_debug_stats = bind_doom_key_action(input, "toggle_debug_stats", SDL_SCANCODE_F1);
    state->action_toggle_portal_culling = bind_doom_key_action(input, "toggle_portal_culling", SDL_SCANCODE_F2);
    state->action_switch_backend = bind_doom_key_action(input, "switch_backend", SDL_SCANCODE_TAB);
    for (int i = 0; i < DOOM_RENDER_PROFILE_COUNT; ++i)
    {
        char action_name[SDL3D_INPUT_ACTION_NAME_MAX];
        SDL_snprintf(action_name, sizeof(action_name), "render_profile_%d", i + 1);
        state->action_render_profile[i] = bind_doom_key_action(input, action_name, profile_keys[i]);
    }
}

static bool game_init(sdl3d_game_context *ctx, void *userdata)
{
    doom_state *state = (doom_state *)userdata;

    state->current_backend = sdl3d_get_render_context_backend(ctx->renderer);
    state->render_profile = DOOM_RENDER_PROFILE_MODERN;
    bind_doom_actions(ctx_input(ctx), state);
    apply_window_defaults(ctx, state);

    if (sdl3d_signal_connect(ctx_bus(ctx), SIG_FADE_OUT_DONE, on_fade_out_done, ctx) == 0)
    {
        return false;
    }
    state->has_font = sdl3d_load_font(SDL3D_MEDIA_DIR "/fonts/Roboto.ttf", 40.0f, &state->debug_font);
    if (!sdl3d_ui_create(state->has_font ? &state->debug_font : NULL, &state->ui))
    {
        doom_state_cleanup(state);
        return false;
    }

    if (!level_data_init(&state->level))
    {
        doom_state_cleanup(state);
        return false;
    }
    state->level_ready = true;

    if (!entities_init(&state->ent, &state->level.unlit, ctx_registry(ctx), ctx_bus(ctx)))
    {
        entities_free(&state->ent);
        doom_state_cleanup(state);
        return false;
    }
    state->entities_ready = true;

    player_init(&state->player, ctx_input(ctx));
    render_state_init(&state->render);
    sdl3d_transition_start(&state->transition, SDL3D_TRANSITION_FADE, SDL3D_TRANSITION_IN, (sdl3d_color){0, 0, 0, 255},
                           1.0f, -1);

    state->demo_player = sdl3d_demo_playback_load("attract.dem");
    if (state->demo_player != NULL)
    {
        sdl3d_demo_playback_start(ctx_input(ctx), state->demo_player);
    }
    return true;
}

static bool switch_demo_backend(sdl3d_game_context *ctx, doom_state *state)
{
    sdl3d_backend next =
        (state->current_backend == SDL3D_BACKEND_OPENGL) ? SDL3D_BACKEND_SOFTWARE : SDL3D_BACKEND_OPENGL;

    if (sdl3d_switch_backend(&ctx->window, &ctx->renderer, next))
    {
        state->current_backend = next;
        apply_window_defaults(ctx, state);
        return true;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Backend switch failed: %s", SDL_GetError());
    if (!sdl3d_switch_backend(&ctx->window, &ctx->renderer, state->current_backend))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not restore previous backend: %s", SDL_GetError());
        return false;
    }

    apply_window_defaults(ctx, state);
    return true;
}

static bool game_event(sdl3d_game_context *ctx, void *userdata, const SDL_Event *event)
{
    doom_state *state = (doom_state *)userdata;

    if (state->demo_player != NULL)
    {
        switch (event->type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            stop_demo_playback(ctx, state);
            break;
        default:
            break;
        }
    }

    sdl3d_ui_process_event(state->ui, event);
    return true;
}

static void apply_doom_debug_actions(sdl3d_game_context *ctx, doom_state *state)
{
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->action_toggle_lighting))
    {
        state->level.use_lit = !state->level.use_lit;
    }
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->action_toggle_lightmaps))
    {
        state->level.use_lightmaps = !state->level.use_lightmaps;
    }
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->action_toggle_debug_stats))
    {
        state->render.show_debug = !state->render.show_debug;
    }
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->action_toggle_portal_culling))
    {
        state->render.portal_culling = !state->render.portal_culling;
    }
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->action_switch_backend) && !switch_demo_backend(ctx, state))
    {
        ctx->quit_requested = true;
    }
}

static void apply_doom_profile_actions(sdl3d_game_context *ctx, doom_state *state)
{
    for (int i = 0; i < DOOM_RENDER_PROFILE_COUNT; ++i)
    {
        if (!sdl3d_input_is_pressed(ctx_input(ctx), state->action_render_profile[i]))
        {
            continue;
        }

        doom_render_profile next = (doom_render_profile)i;
        if (backend_apply_profile(ctx->renderer, next))
        {
            state->render_profile = next;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Render profile: %s", backend_profile_name(next));
        }
        else
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Render profile switch failed: %s", SDL_GetError());
        }
    }
}

typedef struct doom_tick_profile
{
    bool enabled;
    Uint64 last_counter;
    double actions_ms;
    double transition_ms;
    double entities_ms;
    double player_ms;
    double sensors_ms;
    double frame_ms;
    int ticks;
} doom_tick_profile;

static doom_tick_profile g_tick_profile;

static bool doom_tick_profile_enabled(void)
{
    if (!g_tick_profile.enabled)
    {
        g_tick_profile.enabled = SDL_getenv("DOOM_PROFILE_TICK") != NULL;
    }
    return g_tick_profile.enabled;
}

static double doom_profile_elapsed_ms(Uint64 start, Uint64 end)
{
    return (double)(end - start) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

static void doom_tick_profile_record(double actions_ms, double transition_ms, double entities_ms, double player_ms,
                                     double sensors_ms, double frame_ms)
{
    const Uint64 now = SDL_GetPerformanceCounter();
    g_tick_profile.actions_ms += actions_ms;
    g_tick_profile.transition_ms += transition_ms;
    g_tick_profile.entities_ms += entities_ms;
    g_tick_profile.player_ms += player_ms;
    g_tick_profile.sensors_ms += sensors_ms;
    g_tick_profile.frame_ms += frame_ms;
    g_tick_profile.ticks++;

    if (g_tick_profile.last_counter == 0)
    {
        g_tick_profile.last_counter = now;
        return;
    }
    if ((double)(now - g_tick_profile.last_counter) / (double)SDL_GetPerformanceFrequency() < 1.0)
    {
        return;
    }

    const double ticks = g_tick_profile.ticks > 0 ? (double)g_tick_profile.ticks : 1.0;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Doom tick profile: tick=%.2fms actions=%.2f transition=%.2f entities=%.2f player=%.2f "
                "sensors=%.2f ticks=%d",
                g_tick_profile.frame_ms / ticks, g_tick_profile.actions_ms / ticks,
                g_tick_profile.transition_ms / ticks, g_tick_profile.entities_ms / ticks,
                g_tick_profile.player_ms / ticks, g_tick_profile.sensors_ms / ticks, g_tick_profile.ticks);
    g_tick_profile.last_counter = now;
    g_tick_profile.actions_ms = 0.0;
    g_tick_profile.transition_ms = 0.0;
    g_tick_profile.entities_ms = 0.0;
    g_tick_profile.player_ms = 0.0;
    g_tick_profile.sensors_ms = 0.0;
    g_tick_profile.frame_ms = 0.0;
    g_tick_profile.ticks = 0;
}

static void game_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    doom_state *state = (doom_state *)userdata;
    const bool profile_tick = doom_tick_profile_enabled();
    Uint64 tick_start = 0;
    Uint64 section_start = 0;
    double actions_ms = 0.0;
    double transition_ms = 0.0;
    double entities_ms = 0.0;
    double player_ms = 0.0;
    double sensors_ms = 0.0;

    if (profile_tick)
    {
        tick_start = SDL_GetPerformanceCounter();
        section_start = tick_start;
    }

    apply_doom_profile_actions(ctx, state);

    if (sdl3d_input_is_pressed(ctx_input(ctx), state->action_pause))
    {
        toggle_pause(ctx, state);
        return;
    }

    apply_doom_debug_actions(ctx, state);

    if (state->demo_player != NULL && sdl3d_demo_playback_finished(state->demo_player))
    {
        stop_demo_playback(ctx, state);
    }
    if (profile_tick)
    {
        Uint64 now = SDL_GetPerformanceCounter();
        actions_ms = doom_profile_elapsed_ms(section_start, now);
        section_start = now;
    }

    sdl3d_transition_update(&state->transition, ctx_bus(ctx), dt);
    if (profile_tick)
    {
        Uint64 now = SDL_GetPerformanceCounter();
        transition_ms = doom_profile_elapsed_ms(section_start, now);
        section_start = now;
    }
    entities_update(&state->ent, &state->level.unlit, dt, state->player.mover.position);
    if (profile_tick)
    {
        Uint64 now = SDL_GetPerformanceCounter();
        entities_ms = doom_profile_elapsed_ms(section_start, now);
        section_start = now;
    }
    if (!player_update(&state->player, ctx_input(ctx), &state->level.unlit, g_sectors, dt))
    {
        start_quit_fade(ctx, state);
    }
    else
    {
        if (profile_tick)
        {
            Uint64 now = SDL_GetPerformanceCounter();
            player_ms = doom_profile_elapsed_ms(section_start, now);
            section_start = now;
        }
        player_apply_sector_damage(&state->player, g_sectors, g_sector_count, dt);
        if (profile_tick)
        {
            Uint64 now = SDL_GetPerformanceCounter();
            sensors_ms += doom_profile_elapsed_ms(section_start, now);
            section_start = now;
        }
    }

    if (profile_tick)
    {
        Uint64 now = SDL_GetPerformanceCounter();
        sensors_ms += doom_profile_elapsed_ms(section_start, now);
        doom_tick_profile_record(actions_ms, transition_ms, entities_ms, player_ms, sensors_ms,
                                 doom_profile_elapsed_ms(tick_start, now));
    }
}

static void game_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    doom_state *state = (doom_state *)userdata;

    apply_doom_profile_actions(ctx, state);

    if (sdl3d_input_is_pressed(ctx_input(ctx), state->action_pause))
    {
        toggle_pause(ctx, state);
        return;
    }
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->action_menu))
    {
        start_quit_fade(ctx, state);
    }

    state->pause_flash_timer += real_dt * 2.0f;
    while (state->pause_flash_timer >= 1.0f)
    {
        state->pause_flash_timer -= 1.0f;
    }

    if (state->quit_pending)
    {
        sdl3d_transition_update(&state->transition, ctx_bus(ctx), real_dt);
    }
}

static void draw_pause_overlay(sdl3d_game_context *ctx, doom_state *state)
{
    const int width = sdl3d_get_render_context_width(ctx->renderer);
    const int height = sdl3d_get_render_context_height(ctx->renderer);

    if (width <= 0 || height <= 0)
    {
        return;
    }

    sdl3d_draw_rect_overlay(ctx->renderer, 0.0f, 0.0f, (float)width, (float)height, (sdl3d_color){0, 0, 0, 128});

    if (state->pause_flash_timer >= 0.5f || !state->has_font)
    {
        return;
    }

    float text_width = 0.0f;
    float text_height = 0.0f;
    sdl3d_measure_text(&state->debug_font, "PAUSED", &text_width, &text_height);
    const float text_x = ((float)width - text_width) * 0.5f;
    const float text_y = ((float)height - text_height) * 0.5f;
    sdl3d_draw_text_overlay(ctx->renderer, &state->debug_font, "PAUSED", text_x, text_y,
                            (sdl3d_color){255, 255, 255, 255});
}

static void game_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    (void)alpha;
    doom_state *state = (doom_state *)userdata;
    const float frame_dt = sdl3d_time_get_unscaled_delta_time();

    render_draw_frame(&state->render, ctx->renderer, state->has_font ? &state->debug_font : NULL, state->ui,
                      &state->level, &state->ent, &state->player, WINDOW_W, WINDOW_H, frame_dt,
                      backend_profile_name(state->render_profile));
    sdl3d_transition_draw(&state->transition, ctx->renderer);
    if (ctx->paused && !state->quit_pending)
    {
        draw_pause_overlay(ctx, state);
    }
}

static void game_shutdown(sdl3d_game_context *ctx, void *userdata)
{
    doom_state *state = (doom_state *)userdata;

    stop_demo_playback(ctx, state);
    doom_state_cleanup(state);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    doom_state state = {0};
    sdl3d_game_config config = {0};
    config.title = "SDL3D - Doom Level";
    config.width = WINDOW_W;
    config.height = WINDOW_H;
    config.backend = SDL3D_BACKEND_OPENGL;
    config.tick_rate = 1.0f / 60.0f;
    config.enable_audio = true;

    sdl3d_game_callbacks callbacks = {0};
    callbacks.init = game_init;
    callbacks.event = game_event;
    callbacks.tick = game_tick;
    callbacks.pause_tick = game_pause_tick;
    callbacks.render = game_render;
    callbacks.shutdown = game_shutdown;

    return sdl3d_run_game(&config, &callbacks, &state);
}

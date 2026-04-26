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
    int action_pause;
    int action_menu;
    int action_toggle_lighting;
    int action_toggle_lightmaps;
    int action_toggle_debug_stats;
    int action_toggle_portal_culling;
    int action_switch_backend;
    float pause_flash_timer;
    bool has_font;
    bool level_ready;
    bool entities_ready;
    bool quit_pending;
} doom_state;

static void doom_state_cleanup(doom_state *state)
{
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

    if (ctx != NULL && ctx->input != NULL)
    {
        sdl3d_demo_playback_stop(ctx->input);
    }
    sdl3d_demo_playback_free(state->demo_player);
    state->demo_player = NULL;
}

static void apply_window_defaults(sdl3d_game_context *ctx)
{
    backend_apply_defaults(ctx->renderer);
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
    sdl3d_input_bind_fps_defaults(input);
    state->action_pause = sdl3d_input_find_action(input, "pause");
    state->action_menu = sdl3d_input_find_action(input, "menu");
    state->action_toggle_lighting = bind_doom_key_action(input, "toggle_lighting", SDL_SCANCODE_L);
    state->action_toggle_lightmaps = bind_doom_key_action(input, "toggle_lightmaps", SDL_SCANCODE_M);
    state->action_toggle_debug_stats = bind_doom_key_action(input, "toggle_debug_stats", SDL_SCANCODE_F1);
    state->action_toggle_portal_culling = bind_doom_key_action(input, "toggle_portal_culling", SDL_SCANCODE_F2);
    state->action_switch_backend = bind_doom_key_action(input, "switch_backend", SDL_SCANCODE_TAB);
}

static bool game_init(sdl3d_game_context *ctx, void *userdata)
{
    doom_state *state = (doom_state *)userdata;

    state->current_backend = sdl3d_get_render_context_backend(ctx->renderer);
    bind_doom_actions(ctx->input, state);
    apply_window_defaults(ctx);

    if (sdl3d_signal_connect(ctx->bus, SIG_FADE_OUT_DONE, on_fade_out_done, ctx) == 0)
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

    if (!entities_init(&state->ent, ctx->registry, ctx->bus))
    {
        entities_free(&state->ent);
        doom_state_cleanup(state);
        return false;
    }
    state->entities_ready = true;

    player_init(&state->player, ctx->input);
    render_state_init(&state->render);
    sdl3d_transition_start(&state->transition, SDL3D_TRANSITION_FADE, SDL3D_TRANSITION_IN, (sdl3d_color){0, 0, 0, 255},
                           1.0f, -1);

    state->demo_player = sdl3d_demo_playback_load("attract.dem");
    if (state->demo_player != NULL)
    {
        sdl3d_demo_playback_start(ctx->input, state->demo_player);
    }
    return true;
}

static bool switch_demo_backend(sdl3d_game_context *ctx, doom_state *state)
{
    sdl3d_backend next =
        (state->current_backend == SDL3D_BACKEND_SDLGPU) ? SDL3D_BACKEND_SOFTWARE : SDL3D_BACKEND_SDLGPU;

    if (sdl3d_switch_backend(&ctx->window, &ctx->renderer, next))
    {
        state->current_backend = next;
        apply_window_defaults(ctx);
        return true;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Backend switch failed: %s", SDL_GetError());
    if (!sdl3d_switch_backend(&ctx->window, &ctx->renderer, state->current_backend))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not restore previous backend: %s", SDL_GetError());
        return false;
    }

    apply_window_defaults(ctx);
    return true;
}

static bool game_event(sdl3d_game_context *ctx, void *userdata, const SDL_Event *event)
{
    (void)ctx;
    doom_state *state = (doom_state *)userdata;

    sdl3d_ui_process_event(state->ui, event);
    return true;
}

static void apply_doom_debug_actions(sdl3d_game_context *ctx, doom_state *state)
{
    if (sdl3d_input_is_pressed(ctx->input, state->action_toggle_lighting))
    {
        state->level.use_lit = !state->level.use_lit;
    }
    if (sdl3d_input_is_pressed(ctx->input, state->action_toggle_lightmaps))
    {
        state->level.use_lightmaps = !state->level.use_lightmaps;
    }
    if (sdl3d_input_is_pressed(ctx->input, state->action_toggle_debug_stats))
    {
        state->render.show_debug = !state->render.show_debug;
    }
    if (sdl3d_input_is_pressed(ctx->input, state->action_toggle_portal_culling))
    {
        state->render.portal_culling = !state->render.portal_culling;
    }
    if (sdl3d_input_is_pressed(ctx->input, state->action_switch_backend) && !switch_demo_backend(ctx, state))
    {
        ctx->quit_requested = true;
    }
}

static void game_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    doom_state *state = (doom_state *)userdata;

    if (sdl3d_input_is_pressed(ctx->input, state->action_pause))
    {
        toggle_pause(ctx, state);
        return;
    }

    apply_doom_debug_actions(ctx, state);

    if (state->demo_player != NULL && sdl3d_demo_playback_finished(state->demo_player))
    {
        stop_demo_playback(ctx, state);
    }

    sdl3d_transition_update(&state->transition, ctx->bus, dt);
    entities_update(&state->ent, dt, state->player.mover.position);
    if (!player_update(&state->player, ctx->input, &state->level.unlit, g_sectors, dt))
    {
        start_quit_fade(ctx, state);
    }
}

static void game_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    doom_state *state = (doom_state *)userdata;

    if (sdl3d_input_is_pressed(ctx->input, state->action_pause))
    {
        toggle_pause(ctx, state);
        return;
    }
    if (sdl3d_input_is_pressed(ctx->input, state->action_menu))
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
        sdl3d_transition_update(&state->transition, ctx->bus, real_dt);
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
                      &state->level, &state->ent, &state->player, WINDOW_W, WINDOW_H, frame_dt);
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
    config.backend = SDL3D_BACKEND_SDLGPU;
    config.tick_rate = 1.0f / 60.0f;

    sdl3d_game_callbacks callbacks = {0};
    callbacks.init = game_init;
    callbacks.event = game_event;
    callbacks.tick = game_tick;
    callbacks.pause_tick = game_pause_tick;
    callbacks.render = game_render;
    callbacks.shutdown = game_shutdown;

    return sdl3d_run_game(&config, &callbacks, &state);
}

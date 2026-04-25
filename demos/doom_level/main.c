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

typedef struct doom_state
{
    level_data level;
    entities ent;
    player_state player;
    render_state render;
    sdl3d_font debug_font;
    sdl3d_ui_context *ui;
    sdl3d_backend current_backend;
    bool has_font;
    bool level_ready;
    bool entities_ready;
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
    if (state->has_font)
    {
        sdl3d_free_font(&state->debug_font);
        state->has_font = false;
    }
}

static void apply_window_defaults(sdl3d_game_context *ctx)
{
    backend_apply_defaults(ctx->renderer);
    SDL_SetWindowRelativeMouseMode(ctx->window, true);
}

static bool game_init(sdl3d_game_context *ctx, void *userdata)
{
    doom_state *state = (doom_state *)userdata;

    state->current_backend = sdl3d_get_render_context_backend(ctx->renderer);
    apply_window_defaults(ctx);

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

    player_init(&state->player);
    render_state_init(&state->render);
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
    doom_state *state = (doom_state *)userdata;

    sdl3d_ui_process_event(state->ui, event);

    if (!player_handle_event(&state->player, event))
    {
        return false;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_L)
    {
        state->level.use_lit = !state->level.use_lit;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_M)
    {
        state->level.use_lightmaps = !state->level.use_lightmaps;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_F1)
    {
        state->render.show_debug = !state->render.show_debug;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_F2)
    {
        state->render.portal_culling = !state->render.portal_culling;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_TAB)
    {
        return switch_demo_backend(ctx, state);
    }

    return true;
}

static void game_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    (void)ctx;
    doom_state *state = (doom_state *)userdata;

    entities_update(&state->ent, dt, state->player.mover.position);
    player_update(&state->player, &state->level.unlit, g_sectors, dt);
}

static void game_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    (void)alpha;
    doom_state *state = (doom_state *)userdata;
    const float frame_dt = sdl3d_time_get_unscaled_delta_time();

    render_draw_frame(&state->render, ctx->renderer, state->has_font ? &state->debug_font : NULL, state->ui,
                      &state->level, &state->ent, &state->player, WINDOW_W, WINDOW_H, frame_dt);
}

static void game_shutdown(sdl3d_game_context *ctx, void *userdata)
{
    (void)ctx;
    doom_state *state = (doom_state *)userdata;

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
    callbacks.render = game_render;
    callbacks.shutdown = game_shutdown;

    return sdl3d_run_game(&config, &callbacks, &state);
}

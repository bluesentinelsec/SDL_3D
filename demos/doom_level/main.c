/*
 * Doom-style level demo — thin game loop shell.
 *
 * All logic is split into subsystems:
 *   backend.h   — window/backend creation
 *   level_data.h — sector geometry, materials, lights
 *   entities.h  — sprites, 3D models, textures
 *   player.h    — FPS mover, input, projectile
 *   renderer.h  — per-frame drawing, visibility, HUD
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/font.h"
#include "sdl3d/sdl3d.h"
#include "sdl3d/ui.h"

#include "backend.h"
#include "entities.h"
#include "level_data.h"
#include "player.h"
#include "renderer.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;

    /* Backend */
    SDL_Window *win = NULL;
    sdl3d_render_context *ctx = NULL;
    sdl3d_backend current_backend = SDL3D_BACKEND_SDLGPU;
    if (!backend_create(&win, &ctx, current_backend))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Backend init failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    backend_apply_defaults(ctx);

    /* Font + UI */
    sdl3d_font debug_font;
    bool has_font = sdl3d_load_font(SDL3D_MEDIA_DIR "/fonts/Roboto.ttf", 40.0f, &debug_font);
    sdl3d_ui_context *ui = NULL;
    sdl3d_ui_create(has_font ? &debug_font : NULL, &ui);

    /* Subsystems */
    level_data ld;
    if (!level_data_init(&ld))
    {
        sdl3d_destroy_render_context(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    entities ent;
    if (!entities_init(&ent))
    {
        level_data_free(&ld);
        sdl3d_destroy_render_context(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    player_state player;
    player_init(&player);

    render_state rs;
    render_state_init(&rs);

    /* Game loop */
    bool running = true;
    Uint64 last = SDL_GetPerformanceCounter();

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            sdl3d_ui_process_event(ui, &ev);

            if (!player_handle_event(&player, &ev))
                running = false;

            /* Level lighting toggles */
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_L)
                ld.use_lit = !ld.use_lit;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_M)
                ld.use_lightmaps = !ld.use_lightmaps;

            /* Render toggles */
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_F1)
                rs.show_debug = !rs.show_debug;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_F2)
                rs.portal_culling = !rs.portal_culling;

            /* Backend hot-switch */
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_TAB)
            {
                sdl3d_backend next =
                    (current_backend == SDL3D_BACKEND_SDLGPU) ? SDL3D_BACKEND_SOFTWARE : SDL3D_BACKEND_SDLGPU;
                sdl3d_destroy_render_context(ctx);
                SDL_DestroyWindow(win);
                ctx = NULL;
                win = NULL;
                if (!backend_create(&win, &ctx, next))
                {
                    backend_create(&win, &ctx, current_backend);
                }
                else
                {
                    current_backend = next;
                }
                backend_apply_defaults(ctx);
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;

        entities_update(&ent, dt, player.mover.position);
        player_update(&player, &ld.unlit, g_sectors, dt);

        render_draw_frame(&rs, ctx, has_font ? &debug_font : NULL, ui, &ld, &ent, &player, WINDOW_W, WINDOW_H, dt);
    }

    /* Cleanup */
    entities_free(&ent);
    level_data_free(&ld);
    if (has_font)
        sdl3d_free_font(&debug_font);
    sdl3d_ui_destroy(ui);
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

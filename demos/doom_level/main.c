/*
 * Doom-style level demo — sector-based level builder.
 * 6 connected rooms built from 2D floor plans.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/level.h"
#include "sdl3d/lighting.h"
#include "sdl3d/sdl3d.h"

#include <math.h>

#define WINDOW_W 1280
#define WINDOW_H 720
#define MOVE_SPEED 4.0f
#define MOUSE_SENS 0.002f

int main(int argc, char *argv[])
{
    SDL_Window *win = NULL;
    sdl3d_render_context *ctx = NULL;
    sdl3d_render_context_config cfg;

    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    win =
        SDL_CreateWindow("SDL3D \xe2\x80\x94 Doom Level", WINDOW_W, WINDOW_H, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win)
        return 1;

    sdl3d_init_render_context_config(&cfg);
    cfg.backend = SDL3D_BACKEND_SDLGPU;
    cfg.allow_backend_fallback = false;
    cfg.logical_width = WINDOW_W;
    cfg.logical_height = WINDOW_H;
    cfg.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    if (!sdl3d_create_render_context(win, NULL, &cfg, &ctx))
    {
        SDL_Log("Render context failed: %s", SDL_GetError());
        return 1;
    }

    SDL_SetWindowRelativeMouseMode(win, true);

    sdl3d_set_bloom_enabled(ctx, false);
    sdl3d_set_ssao_enabled(ctx, false);
    sdl3d_set_point_shadows_enabled(ctx, false);
    sdl3d_set_backface_culling_enabled(ctx, false);

    /* ---- Material palette ---- */
    sdl3d_level_material mats[] = {
        {{0.25f, 0.25f, 0.28f, 1.0f}, 0.0f, 0.9f}, /* 0: dark stone floor */
        {{0.35f, 0.33f, 0.30f, 1.0f}, 0.0f, 0.8f}, /* 1: concrete ceiling */
        {{0.40f, 0.30f, 0.25f, 1.0f}, 0.0f, 0.7f}, /* 2: brick walls */
        {{0.15f, 0.30f, 0.15f, 1.0f}, 0.0f, 0.9f}, /* 3: nukage floor (green) */
        {{0.50f, 0.45f, 0.40f, 1.0f}, 0.0f, 0.6f}, /* 4: light brown walls */
        {{0.20f, 0.18f, 0.22f, 1.0f}, 0.0f, 0.9f}, /* 5: dark floor */
    };

    /* ---- Sector definitions (E1M1-inspired) ---- */
    /*
     * Layout (top-down, Z increases downward):
     *
     *   [0] Start Room (10x8)
     *        |  (doorway 3..7)
     *   [1] Corridor (4x8)
     *        |  (doorway 3..7)
     *   [2] Nukage Room (12x10) ---[3] Side Passage (6x4)---[4] Outdoor Area (12x10)
     *                                                              |
     *                                                         [5] Exit Room (8x6)
     */
    sdl3d_sector sectors[] = {
        /* 0: Starting room */
        {{{0, 0}, {10, 0}, {10, 8}, {0, 8}}, 4, 0.0f, 4.0f, 0, 1, 2},
        /* 1: Corridor */
        {{{3, 8}, {7, 8}, {7, 16}, {3, 16}}, 4, 0.0f, 3.5f, 5, 1, 4},
        /* 2: Nukage room (lower floor) */
        {{{-2, 16}, {10, 16}, {10, 26}, {-2, 26}}, 4, -0.5f, 4.5f, 3, 1, 2},
        /* 3: Side passage */
        {{{10, 18}, {16, 18}, {16, 22}, {10, 22}}, 4, 0.0f, 3.5f, 0, 1, 4},
        /* 4: Outdoor area */
        {{{16, 14}, {28, 14}, {28, 26}, {16, 26}}, 4, 0.0f, 8.0f, 0, 1, 2},
        /* 5: Exit room */
        {{{20, 26}, {28, 26}, {28, 32}, {20, 32}}, 4, 0.0f, 3.0f, 5, 1, 4},
    };

    sdl3d_level_light lights[] = {
        {{5, 3.5f, 4}, {1.0f, 0.85f, 0.6f}, 5.0f, 12.0f},  /* Start room — warm */
        {{5, 3.0f, 12}, {1.0f, 0.7f, 0.3f}, 3.0f, 8.0f},   /* Corridor — amber */
        {{4, 1.0f, 21}, {0.2f, 1.0f, 0.2f}, 4.0f, 14.0f},  /* Nukage — green */
        {{22, 7.0f, 20}, {0.4f, 0.5f, 0.8f}, 6.0f, 18.0f}, /* Outdoor — moonlight */
        {{24, 2.5f, 29}, {1.0f, 0.15f, 0.1f}, 4.0f, 8.0f}, /* Exit — red warning */
    };

    /* Build two versions: baked lighting and raw materials. */
    sdl3d_level level_lit, level_unlit;
    if (!sdl3d_build_level(sectors, 6, mats, 6, lights, 5, &level_lit))
    {
        SDL_Log("Level build failed: %s", SDL_GetError());
        return 1;
    }
    if (!sdl3d_build_level(sectors, 6, mats, 6, NULL, 0, &level_unlit))
    {
        SDL_Log("Level build failed: %s", SDL_GetError());
        return 1;
    }
    bool use_baked = true;

    /* ---- Lighting ---- */
    /* Start simple: unlit to verify geometry, then add lighting. */

    /* Player */
    float px = 5, py = 1.6f, pz = 4;
    float yaw = 3.14159f, pitch = 0;
    bool mouse_init = false;

    bool running = true;
    Uint64 last = SDL_GetPerformanceCounter();

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
                running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_ESCAPE)
                running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_L)
                use_baked = !use_baked;
            if (ev.type == SDL_EVENT_MOUSE_MOTION && mouse_init)
            {
                yaw += ev.motion.xrel * MOUSE_SENS;
                pitch -= ev.motion.yrel * MOUSE_SENS;
                if (pitch > 1.4f)
                    pitch = 1.4f;
                if (pitch < -1.4f)
                    pitch = -1.4f;
            }
            if (ev.type == SDL_EVENT_MOUSE_MOTION)
                mouse_init = true;
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;

        float fx = sinf(yaw) * cosf(pitch);
        float fz = -cosf(yaw) * cosf(pitch);
        float rx = cosf(yaw);
        float rz = sinf(yaw);

        const Uint8 *keys = (const Uint8 *)SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_W])
        {
            px += fx * MOVE_SPEED * dt;
            pz += fz * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_S])
        {
            px -= fx * MOVE_SPEED * dt;
            pz -= fz * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_A])
        {
            px -= rx * MOVE_SPEED * dt;
            pz -= rz * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_D])
        {
            px += rx * MOVE_SPEED * dt;
            pz += rz * MOVE_SPEED * dt;
        }

        sdl3d_camera3d cam;
        cam.position = sdl3d_vec3_make(px, py, pz);
        cam.target = sdl3d_vec3_make(px + fx, py + sinf(pitch), pz + fz);
        cam.up = sdl3d_vec3_make(0, 1, 0);
        cam.fovy = 75.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        sdl3d_clear_render_context(ctx, (sdl3d_color){100, 150, 200, 255});
        sdl3d_begin_mode_3d(ctx, cam);

        sdl3d_draw_model(ctx, use_baked ? &level_lit.model : &level_unlit.model, sdl3d_vec3_make(0, 0, 0), 1.0f,
                         (sdl3d_color){255, 255, 255, 255});

        sdl3d_end_mode_3d(ctx);
        sdl3d_present_render_context(ctx);
    }

    sdl3d_free_level(&level_lit);
    sdl3d_free_level(&level_unlit);
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

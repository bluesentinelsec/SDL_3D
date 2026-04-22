/*
 * Bathroom demo — first-person walkthrough of an interior scene.
 * Tests: large node hierarchy, multi-material textures, interior lighting.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/collision.h"
#include "sdl3d/lighting.h"
#include "sdl3d/sdl3d.h"

#include <math.h>

#define WINDOW_W 1280
#define WINDOW_H 720
#define MOVE_SPEED 3.0f
#define MOUSE_SENS 0.002f
#define EYE_HEIGHT 1.6f

int main(int argc, char *argv[])
{
    SDL_Window *win = NULL;
    sdl3d_render_context *ctx = NULL;
    sdl3d_render_context_config cfg;
    sdl3d_model model;
    bool has_model = false;

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

    win = SDL_CreateWindow("SDL3D \xe2\x80\x94 Bathroom Interior", WINDOW_W, WINDOW_H,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

    SDL_memset(&model, 0, sizeof(model));
    if (sdl3d_load_model_from_file(SDL3D_MEDIA_DIR "/bathroom/scene.gltf", &model))
    {
        has_model = true;
        SDL_Log("Bathroom loaded: %d meshes, %d materials, %d nodes", model.mesh_count, model.material_count,
                model.node_count);
    }
    else
    {
        SDL_Log("Failed to load bathroom: %s", SDL_GetError());
    }

    /* Pitch black — only the flashlight illuminates */
    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG);
    sdl3d_set_ambient_light(ctx, 0.0f, 0.0f, 0.0f);
    sdl3d_set_bloom_enabled(ctx, true);
    sdl3d_set_ssao_enabled(ctx, true);
    sdl3d_set_point_shadows_enabled(ctx, true);

    /* Load HDRI environment map for IBL */
    if (!sdl3d_load_environment_map(ctx, SDL3D_MEDIA_DIR "/environments/warm_bar_4k.hdr"))
    {
        SDL_Log("IBL not available: %s", SDL_GetError());
    }

    /* Player state */
    float px = 2.0f, py = EYE_HEIGHT, pz = 1.0f;
    float yaw = 3.14159f, pitch = 0.0f;
    bool mouse_initialized = false;

    float game_time = 0.0f;
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
            if (ev.type == SDL_EVENT_MOUSE_MOTION && mouse_initialized)
            {
                yaw += ev.motion.xrel * MOUSE_SENS;
                pitch -= ev.motion.yrel * MOUSE_SENS;
                if (pitch > 1.4f)
                    pitch = 1.4f;
                if (pitch < -1.4f)
                    pitch = -1.4f;
            }
            if (ev.type == SDL_EVENT_MOUSE_MOTION)
                mouse_initialized = true;
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                /* Read depth at screen center to find exact surface distance */
                sdl3d_color center_pixel;
                int cx = WINDOW_W / 2, cy = WINDOW_H / 2;
                float cfx = sinf(yaw) * cosf(pitch);
                float cfy = sinf(pitch);
                float cfz = -cosf(yaw) * cosf(pitch);
                /* Print at several distances — pick the one on the surface */
                SDL_Log("Camera: (%.2f, %.2f, %.2f)", px, py, pz);
                for (float d = 0.5f; d <= 5.0f; d += 0.5f)
                {
                    SDL_Log("  %.1fm: sdl3d_vec3_make(%.2ff, %.2ff, %.2ff)", d, px + cfx * d, py + cfy * d,
                            pz + cfz * d);
                }
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;
        game_time += dt;

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
        if (keys[SDL_SCANCODE_UP])
            py += MOVE_SPEED * dt;
        if (keys[SDL_SCANCODE_DOWN])
            py -= MOVE_SPEED * dt;

        sdl3d_camera3d cam;
        cam.position = sdl3d_vec3_make(px, py, pz);
        cam.target = sdl3d_vec3_make(px + fx, py + sinf(pitch), pz + fz);
        cam.up = sdl3d_vec3_make(0, 1, 0);
        cam.fovy = 75.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        sdl3d_clear_render_context(ctx, (sdl3d_color){5, 5, 8, 255});
        sdl3d_begin_mode_3d(ctx, cam);

        /* Single flashlight — nothing else */
        sdl3d_clear_lights(ctx);
        sdl3d_set_ambient_light(ctx, 0.0f, 0.0f, 0.0f);

        sdl3d_light flashlight = {0};
        flashlight.type = SDL3D_LIGHT_POINT;
        flashlight.position = sdl3d_vec3_make(px, py - 0.2f, pz);
        flashlight.color[0] = 1.0f;
        flashlight.color[1] = 0.9f;
        flashlight.color[2] = 0.7f;
        flashlight.intensity = 2.0f;
        flashlight.range = 3.5f;
        sdl3d_add_light(ctx, &flashlight);

        /* Candle — warm flicker */
        float flicker = 1.0f + 0.3f * sinf(game_time * 12.0f) + 0.15f * sinf(game_time * 23.0f);
        sdl3d_light candle = {0};
        candle.type = SDL3D_LIGHT_POINT;
        candle.position = sdl3d_vec3_make(0.67f, 1.63f, -2.68f);
        candle.color[0] = 1.0f;
        candle.color[1] = 0.6f;
        candle.color[2] = 0.15f;
        candle.intensity = 4.0f * flicker;
        candle.range = 2.0f;
        sdl3d_add_light(ctx, &candle);

        if (has_model)
        {
            sdl3d_draw_model(ctx, &model, sdl3d_vec3_make(0, 0, 0), 1.0f, (sdl3d_color){255, 255, 255, 255});
        }

        /* Crosshair markers along look direction — shows where each distance lands */
        sdl3d_set_emissive(ctx, 5.0f, 5.0f, 5.0f);
        for (float d = 0.5f; d <= 5.0f; d += 0.5f)
        {
            float mx = px + fx * d;
            float my = py + sinf(pitch) * d;
            float mz = pz + fz * d;
            /* Alternate colors: white at 0.5m intervals, red at 1m */
            float r = (int)(d * 2) % 2 == 0 ? 5.0f : 0.0f;
            float g = (int)(d * 2) % 2 == 0 ? 0.0f : 5.0f;
            sdl3d_set_emissive(ctx, r, g, 0.0f);
            float sz = 0.01f;
            sdl3d_draw_cube(ctx, sdl3d_vec3_make(mx, my, mz), sdl3d_vec3_make(sz, sz, sz),
                            (sdl3d_color){255, 255, 255, 255});
        }
        sdl3d_set_emissive(ctx, 0.0f, 0.0f, 0.0f);

        sdl3d_end_mode_3d(ctx);
        sdl3d_present_render_context(ctx);
    }

    sdl3d_free_model(&model);
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

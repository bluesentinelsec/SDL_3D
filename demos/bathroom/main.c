/*
 * Bathroom demo — interior scene with 106 meshes and 11 PBR materials.
 * Tests: large node hierarchy, multi-material texturing, interior lighting.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/lighting.h"
#include "sdl3d/sdl3d.h"

#include <math.h>

#define WINDOW_W 1280
#define WINDOW_H 720

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

    /* Interior lighting */
    sdl3d_set_ambient_light(ctx, 0.3f, 0.3f, 0.35f);

    sdl3d_light ceiling = {0};
    ceiling.type = SDL3D_LIGHT_POINT;
    ceiling.position = sdl3d_vec3_make(0.0f, 3.0f, 0.0f);
    ceiling.color[0] = 1.0f;
    ceiling.color[1] = 0.95f;
    ceiling.color[2] = 0.85f;
    ceiling.intensity = 8.0f;
    ceiling.range = 15.0f;
    sdl3d_add_light(ctx, &ceiling);

    sdl3d_light window_light = {0};
    window_light.type = SDL3D_LIGHT_POINT;
    window_light.position = sdl3d_vec3_make(3.0f, 2.0f, 0.0f);
    window_light.color[0] = 0.7f;
    window_light.color[1] = 0.8f;
    window_light.color[2] = 1.0f;
    window_light.intensity = 5.0f;
    window_light.range = 12.0f;
    sdl3d_add_light(ctx, &window_light);

    /* Orbit camera */
    float orbit_angle = 0.0f;
    float orbit_radius = 6.0f;
    float orbit_height = 2.0f;
    bool auto_orbit = true;
    float pan_x = 0.0f, pan_y = 0.0f;

    bool running = true;
    Uint64 last = SDL_GetPerformanceCounter();

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
                running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN)
            {
                if (ev.key.scancode == SDL_SCANCODE_ESCAPE)
                    running = false;
                if (ev.key.scancode == SDL_SCANCODE_SPACE)
                    auto_orbit = !auto_orbit;
            }
            if (ev.type == SDL_EVENT_MOUSE_WHEEL)
            {
                orbit_radius -= ev.wheel.y * 0.5f;
                if (orbit_radius < 1.0f)
                    orbit_radius = 1.0f;
                if (orbit_radius > 30.0f)
                    orbit_radius = 30.0f;
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;

        const Uint8 *keys = (const Uint8 *)SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_LEFT])
            orbit_angle -= 1.5f * dt;
        if (keys[SDL_SCANCODE_RIGHT])
            orbit_angle += 1.5f * dt;
        if (keys[SDL_SCANCODE_UP])
            orbit_height += 2.0f * dt;
        if (keys[SDL_SCANCODE_DOWN])
            orbit_height -= 2.0f * dt;
        if (keys[SDL_SCANCODE_W])
            pan_y += 2.0f * dt;
        if (keys[SDL_SCANCODE_S])
            pan_y -= 2.0f * dt;
        if (keys[SDL_SCANCODE_A])
            pan_x -= 2.0f * dt;
        if (keys[SDL_SCANCODE_D])
            pan_x += 2.0f * dt;
        if (keys[SDL_SCANCODE_E])
            orbit_radius -= 2.0f * dt;
        if (keys[SDL_SCANCODE_Q])
            orbit_radius += 2.0f * dt;
        if (orbit_radius < 0.5f)
            orbit_radius = 0.5f;

        if (auto_orbit)
            orbit_angle += 0.3f * dt;

        sdl3d_camera3d cam;
        float cx = sinf(orbit_angle) * orbit_radius;
        float cz = cosf(orbit_angle) * orbit_radius;
        float right_x = cosf(orbit_angle);
        float right_z = -sinf(orbit_angle);
        float ox = pan_x * right_x;
        float oz = pan_x * right_z;
        cam.position = sdl3d_vec3_make(cx + ox, orbit_height + pan_y, cz + oz);
        cam.target = sdl3d_vec3_make(ox, 1.0f + pan_y, oz);
        cam.up = sdl3d_vec3_make(0, 1, 0);
        cam.fovy = 60.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        sdl3d_clear_render_context(ctx, (sdl3d_color){40, 45, 55, 255});
        sdl3d_begin_mode_3d(ctx, cam);

        if (has_model)
        {
            sdl3d_draw_model(ctx, &model, sdl3d_vec3_make(0, 0, 0), 1.0f, (sdl3d_color){255, 255, 255, 255});
        }

        sdl3d_end_mode_3d(ctx);
        sdl3d_present_render_context(ctx);
    }

    sdl3d_free_model(&model);
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

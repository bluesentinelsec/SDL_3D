/*
 * Black Dragon demo — skinned model with idle animation.
 * Tests: skeleton, animation evaluation, skinned drawing, textures.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/animation.h"
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
    sdl3d_mat4 *joint_matrices = NULL;
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

    win = SDL_CreateWindow("SDL3D \xe2\x80\x94 Black Dragon", WINDOW_W, WINDOW_H,
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
    if (sdl3d_load_model_from_file(SDL3D_MEDIA_DIR "/black_dragon/scene.gltf", &model))
    {
        has_model = true;
        SDL_Log("Dragon loaded: %d meshes, %d materials, %d nodes", model.mesh_count, model.material_count,
                model.node_count);
        if (model.skeleton)
        {
            SDL_Log("  Skeleton: %d joints", model.skeleton->joint_count);
            joint_matrices = (sdl3d_mat4 *)SDL_calloc((size_t)model.skeleton->joint_count, sizeof(sdl3d_mat4));
        }
        /* Boost dark material albedo factors so the dragon is visible,
         * but preserve relative color differences between materials. */
        for (int i = 0; i < model.material_count; ++i)
        {
            for (int c = 0; c < 3; ++c)
            {
                model.materials[i].albedo[c] = fminf(model.materials[i].albedo[c] * 3.0f, 1.0f);
            }
            model.materials[i].albedo[3] = 1.0f;
        }
        if (model.animation_count > 0)
        {
            SDL_Log("  Animation: '%s' (%.1fs, %d channels)", model.animations[0].name, model.animations[0].duration,
                    model.animations[0].channel_count);
        }
    }
    else
    {
        SDL_Log("Failed to load dragon: %s", SDL_GetError());
    }

    /* Lighting */
    sdl3d_set_ambient_light(ctx, 0.25f, 0.25f, 0.3f);

    sdl3d_light key = {0};
    key.type = SDL3D_LIGHT_POINT;
    key.position = sdl3d_vec3_make(-5.0f, 8.0f, 6.0f);
    key.color[0] = 1.0f;
    key.color[1] = 0.95f;
    key.color[2] = 0.9f;
    key.intensity = 30.0f;
    key.range = 40.0f;
    sdl3d_add_light(ctx, &key);

    sdl3d_light fill = {0};
    fill.type = SDL3D_LIGHT_POINT;
    fill.position = sdl3d_vec3_make(5.0f, 4.0f, -4.0f);
    fill.color[0] = 0.6f;
    fill.color[1] = 0.7f;
    fill.color[2] = 1.0f;
    fill.intensity = 15.0f;
    fill.range = 30.0f;
    sdl3d_add_light(ctx, &fill);

    sdl3d_light rim = {0};
    rim.type = SDL3D_LIGHT_POINT;
    rim.position = sdl3d_vec3_make(0.0f, 2.0f, -6.0f);
    rim.color[0] = 1.0f;
    rim.color[1] = 0.9f;
    rim.color[2] = 0.8f;
    rim.intensity = 12.0f;
    rim.range = 30.0f;
    sdl3d_add_light(ctx, &rim);
    sdl3d_add_light(ctx, &fill);

    /* Orbit camera */
    float orbit_angle = 0.0f;
    float orbit_radius = 20.0f;
    float orbit_height = 5.0f;
    bool auto_orbit = true;
    float pan_x = 0.0f, pan_y = 0.0f;
    float anim_time = 0.0f;
    bool anim_playing = true;

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
                if (ev.key.scancode == SDL_SCANCODE_P)
                    anim_playing = !anim_playing;
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

        /* Advance animation */
        if (anim_playing && has_model && model.animation_count > 0 && joint_matrices)
        {
            anim_time += dt;
            if (anim_time > model.animations[0].duration)
                anim_time -= model.animations[0].duration;
            sdl3d_evaluate_animation(model.skeleton, &model.animations[0], anim_time, joint_matrices);
        }

        sdl3d_camera3d cam;
        float cx = sinf(orbit_angle) * orbit_radius;
        float cz = cosf(orbit_angle) * orbit_radius;
        float right_x = cosf(orbit_angle);
        float right_z = -sinf(orbit_angle);
        float ox = pan_x * right_x;
        float oz = pan_x * right_z;
        cam.position = sdl3d_vec3_make(cx + ox, orbit_height + pan_y, cz + oz);
        cam.target = sdl3d_vec3_make(ox, pan_y, oz);
        cam.up = sdl3d_vec3_make(0, 1, 0);
        cam.fovy = 45.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        sdl3d_clear_render_context(ctx, (sdl3d_color){100, 110, 130, 255});
        sdl3d_begin_mode_3d(ctx, cam);

        if (has_model)
        {
            if (joint_matrices)
            {
                sdl3d_draw_model_skinned(ctx, &model, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(0, 1, 0), 0.0f,
                                         sdl3d_vec3_make(1, 1, 1), (sdl3d_color){255, 255, 255, 255}, joint_matrices);
            }
            else
            {
                sdl3d_draw_model(ctx, &model, sdl3d_vec3_make(0, 0, 0), 1.0f, (sdl3d_color){255, 255, 255, 255});
            }
        }

        sdl3d_end_mode_3d(ctx);
        sdl3d_present_render_context(ctx);
    }

    SDL_free(joint_matrices);
    sdl3d_free_model(&model);
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

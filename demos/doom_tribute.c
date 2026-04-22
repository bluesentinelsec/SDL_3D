/*
 * SDL3D Doom E1M1-Inspired Demo — Indoor/outdoor FPS scene.
 *
 * Controls:
 *   WASD        — move
 *   Mouse       — look
 *   1-5         — render profiles
 *   Tab         — toggle backend
 *   6           — toggle bloom
 *   7           — toggle SSAO
 *   8           — toggle fog
 *   ESC         — quit
 */

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/effects.h"
#include "sdl3d/lighting.h"
#include "sdl3d/sdl3d.h"

#include <math.h>
#include <stdio.h>

#define WINDOW_W 1920
#define WINDOW_H 1080
#define MOVE_SPEED 4.0f
#define MOUSE_SENS 0.002f
#define EYE_HEIGHT 1.6f

typedef struct
{
    float x, y, z;
    float yaw, pitch;
} player_t;

/* ------------------------------------------------------------------ */
/* Scene geometry                                                      */
/* ------------------------------------------------------------------ */

static void draw_doom_scene(sdl3d_render_context *ctx)
{
    sdl3d_color dark_floor = {50, 50, 45, 255};
    sdl3d_color dark_ceil = {150, 30, 30, 255};
    sdl3d_color wall_grey = {80, 80, 75, 255};
    sdl3d_color wall_brown = {90, 70, 50, 255};
    sdl3d_color wall_dark = {55, 50, 50, 255};
    sdl3d_color outdoor_floor = {45, 65, 35, 255};
    sdl3d_color nukage = {30, 120, 30, 200};
    sdl3d_color metal = {70, 75, 80, 255};

    /* ROOM 1: spawn (x:-5..5, z:-4..4) */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, 0, 0), (sdl3d_vec2){10, 8}, dark_floor);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 4, 0), sdl3d_vec3_make(10, 0.2f, 8), dark_ceil);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-5, 2, 0), sdl3d_vec3_make(0.3f, 4, 8), wall_grey);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(5, 2, 0), sdl3d_vec3_make(0.3f, 4, 8), wall_grey);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 2, -4), sdl3d_vec3_make(10, 4, 0.3f), wall_grey);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-3.5f, 2, 4), sdl3d_vec3_make(3, 4, 0.3f), wall_grey);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(3.5f, 2, 4), sdl3d_vec3_make(3, 4, 0.3f), wall_grey);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 3.5f, 4), sdl3d_vec3_make(4, 1, 0.3f), wall_grey);
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(-3, 2, -2), 0.4f, 0.4f, 4, 8, wall_brown);
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(3, 2, -2), 0.4f, 0.4f, 4, 8, wall_brown);

    /* CORRIDOR 1 (x:-2..2, z:4..12) */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, 0, 8), (sdl3d_vec2){4, 8}, dark_floor);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 3.5f, 8), sdl3d_vec3_make(4, 0.2f, 8), dark_ceil);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-2, 1.75f, 8), sdl3d_vec3_make(0.3f, 3.5f, 8), wall_dark);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(2, 1.75f, 8), sdl3d_vec3_make(0.3f, 3.5f, 8), wall_dark);
    /* Fill above corridor where it meets room 1 (step from 4 to 3.5) */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 3.85f, 4), sdl3d_vec3_make(4, 0.5f, 0.3f), wall_grey);
    /* Fill above corridor where it meets room 2 (step from 3.5 to 4.5) */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 4.0f, 12), sdl3d_vec3_make(4, 1.0f, 0.3f), wall_brown);

    /* ROOM 2: nukage (x:-6..6, z:12..22) */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, -0.5f, 17), (sdl3d_vec2){12, 10}, dark_floor);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 4.5f, 17), sdl3d_vec3_make(12, 0.2f, 10), dark_ceil);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-6, 2, 17), sdl3d_vec3_make(0.3f, 5, 10), wall_brown);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(6, 2, 13.5f), sdl3d_vec3_make(0.3f, 5, 3), wall_brown);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(6, 2, 20.5f), sdl3d_vec3_make(0.3f, 5, 3), wall_brown);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(6, 4, 17), sdl3d_vec3_make(0.3f, 1, 4), wall_brown);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-4, 2, 12), sdl3d_vec3_make(4, 5, 0.3f), wall_brown);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(4, 2, 12), sdl3d_vec3_make(4, 5, 0.3f), wall_brown);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 2, 22), sdl3d_vec3_make(12, 5, 0.3f), wall_brown);
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, -0.3f, 17), (sdl3d_vec2){6, 4}, nukage);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-4, 0.1f, 17), sdl3d_vec3_make(1.5f, 0.5f, 8), metal);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(4, 0.1f, 17), sdl3d_vec3_make(1.5f, 0.5f, 8), metal);

    /* CORRIDOR 2 east (x:6..12, z:15..19) */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(9, 0, 17), (sdl3d_vec2){6, 4}, dark_floor);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(9, 3.5f, 17), sdl3d_vec3_make(6, 0.2f, 4), dark_ceil);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(9, 1.75f, 15), sdl3d_vec3_make(6, 3.5f, 0.3f), wall_dark);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(9, 1.75f, 19), sdl3d_vec3_make(6, 3.5f, 0.3f), wall_dark);

    /* OUTDOOR (x:12..24, z:12..22, NO ceiling — sky visible) */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(18, 0, 17), (sdl3d_vec2){12, 10}, outdoor_floor);
    /* East wall */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(24, 2, 17), sdl3d_vec3_make(0.3f, 4, 10), wall_dark);
    /* South wall */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(18, 2, 12), sdl3d_vec3_make(12, 4, 0.3f), wall_dark);
    /* West wall — solid except corridor 2 opening (z:15..19) */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(12, 2, 13.5f), sdl3d_vec3_make(0.3f, 4, 3), wall_dark);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(12, 2, 20.5f), sdl3d_vec3_make(0.3f, 4, 3), wall_dark);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(12, 3.5f, 17), sdl3d_vec3_make(0.3f, 1, 4), wall_dark);
    /* North wall with door to exit room (gap x:16..20) */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(14.5f, 2, 21.8f), sdl3d_vec3_make(3, 4, 0.3f), wall_dark);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(21.5f, 2, 21.8f), sdl3d_vec3_make(3, 4, 0.3f), wall_dark);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(18, 3.5f, 21.8f), sdl3d_vec3_make(4, 1, 0.3f), wall_dark);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(16, 0.5f, 14), sdl3d_vec3_make(1, 1, 1), wall_brown);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(17, 0.5f, 15), sdl3d_vec3_make(1, 1, 1), wall_brown);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(16.5f, 1.5f, 14.5f), sdl3d_vec3_make(1, 1, 1), wall_brown);
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(20, 0.6f, 15), 0.4f, 0.4f, 1.2f, 8, (sdl3d_color){60, 90, 60, 255});
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(21, 0.6f, 19), 0.4f, 0.4f, 1.2f, 8, (sdl3d_color){60, 90, 60, 255});

    /* ROOM 3: exit (x:14..22, z:22..28, enclosed) */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(18, 0, 25), (sdl3d_vec2){8, 6}, dark_floor);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(18, 3, 25), sdl3d_vec3_make(8, 0.2f, 6), dark_ceil);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(14, 1.5f, 25), sdl3d_vec3_make(0.3f, 3, 6), wall_grey);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(22, 1.5f, 25), sdl3d_vec3_make(0.3f, 3, 6), wall_grey);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(18, 1.5f, 28), sdl3d_vec3_make(8, 3, 0.3f), wall_grey);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(15, 1.5f, 22.2f), sdl3d_vec3_make(2, 3, 0.3f), wall_grey);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(21, 1.5f, 22.2f), sdl3d_vec3_make(2, 3, 0.3f), wall_grey);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(18, 2.8f, 22.2f), sdl3d_vec3_make(4, 0.4f, 0.3f), wall_grey);
}

static bool create_backend(SDL_Window **out_win, SDL_Renderer **out_ren, sdl3d_render_context **out_ctx,
                           sdl3d_backend backend)
{
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    SDL_Window *w;
    SDL_Renderer *r = NULL;
    sdl3d_render_context *c = NULL;
    sdl3d_render_context_config cfg;

    if (backend == SDL3D_BACKEND_SDLGPU)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
        flags |= SDL_WINDOW_OPENGL;
    }

    w = SDL_CreateWindow("SDL3D - Doom E1M1 Tribute", WINDOW_W, WINDOW_H, flags);
    if (!w)
        return false;

    if (backend != SDL3D_BACKEND_SDLGPU)
    {
        r = SDL_CreateRenderer(w, NULL);
        if (!r)
        {
            SDL_DestroyWindow(w);
            return false;
        }
    }

    sdl3d_init_render_context_config(&cfg);
    cfg.backend = backend;
    cfg.allow_backend_fallback = false;
    cfg.logical_width = WINDOW_W;
    cfg.logical_height = WINDOW_H;
    cfg.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    if (!sdl3d_create_render_context(w, r, &cfg, &c))
    {
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(w);
        return false;
    }

    SDL_SetWindowRelativeMouseMode(w, true);
    *out_win = w;
    *out_ren = r;
    *out_ctx = c;
    return true;
}

static void setup_lighting(sdl3d_render_context *ctx)
{
    sdl3d_set_ambient_light(ctx, 0.03f, 0.03f, 0.04f);

    /* Dim overhead in starting room */
    sdl3d_light l = {0};
    l.type = SDL3D_LIGHT_POINT;
    l.position = sdl3d_vec3_make(0, 3.5f, 0);
    l.color[0] = 1.0f;
    l.color[1] = 0.85f;
    l.color[2] = 0.6f;
    l.intensity = 3.0f;
    l.range = 12.0f;
    sdl3d_add_light(ctx, &l);

    /* Corridor light — flickering amber */
    l.position = sdl3d_vec3_make(0, 3.0f, 9);
    l.color[0] = 1.0f;
    l.color[1] = 0.7f;
    l.color[2] = 0.3f;
    l.intensity = 2.5f;
    l.range = 10.0f;
    sdl3d_add_light(ctx, &l);

    /* Nukage room — green glow from the pool */
    l.position = sdl3d_vec3_make(0, 1.0f, 18);
    l.color[0] = 0.2f;
    l.color[1] = 1.0f;
    l.color[2] = 0.2f;
    l.intensity = 4.0f;
    l.range = 14.0f;
    sdl3d_add_light(ctx, &l);

    /* Exit room — red warning light */
    l.position = sdl3d_vec3_make(18, 2.5f, 25);
    l.color[0] = 1.0f;
    l.color[1] = 0.15f;
    l.color[2] = 0.1f;
    l.intensity = 3.5f;
    l.range = 10.0f;
    sdl3d_add_light(ctx, &l);

    /* Outdoor — cool moonlight from above */
    l.position = sdl3d_vec3_make(18, 5.0f, 17);
    l.color[0] = 0.4f;
    l.color[1] = 0.5f;
    l.color[2] = 0.8f;
    l.intensity = 2.0f;
    l.range = 18.0f;
    sdl3d_add_light(ctx, &l);
    l.color[0] = 0.4f;
    l.color[1] = 0.5f;
    l.color[2] = 0.8f;
    l.intensity = 2.0f;
    l.range = 18.0f;
    sdl3d_add_light(ctx, &l);

    /* Directional moonlight for outdoor */
    sdl3d_light sun = {0};
    sun.type = SDL3D_LIGHT_DIRECTIONAL;
    sun.direction = sdl3d_vec3_make(0.3f, -0.9f, -0.2f);
    sun.color[0] = 0.2f;
    sun.color[1] = 0.25f;
    sun.color[2] = 0.4f;
    sun.intensity = 0.3f;
    sdl3d_add_light(ctx, &sun);
}

int main(int argc, char *argv[])
{
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    sdl3d_render_context *ctx = NULL;
    player_t player = {0, EYE_HEIGHT, -3, 3.14159f, 0};
    sdl3d_fog fog;
    bool running = true;
    bool mouse_initialized = false;
    bool show_bloom = true;
    bool show_ssao = false;
    bool show_fog = true;
    Uint64 last_time;
    float game_time = 0.0f;
    int current_profile = 0;
    sdl3d_backend current_backend = SDL3D_BACKEND_SDLGPU;
    const char *profile_names[] = {"Modern", "PS1", "N64", "DOS", "SNES"};
    sdl3d_render_profile (*profile_fns[])(void) = {sdl3d_profile_modern, sdl3d_profile_ps1, sdl3d_profile_n64,
                                                   sdl3d_profile_dos, sdl3d_profile_snes};

    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;

    if (!create_backend(&win, &ren, &ctx, current_backend))
    {
        fprintf(stderr, "Backend init failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    setup_lighting(ctx);
    sdl3d_set_ssao_enabled(ctx, false);
    sdl3d_set_point_shadows_enabled(ctx, false);

    SDL_zerop(&fog);
    fog.mode = SDL3D_FOG_EXP2;
    fog.color[0] = 0.0f;
    fog.color[1] = 0.0f;
    fog.color[2] = 0.0f;
    fog.density = 0.04f;
    sdl3d_set_fog(ctx, &fog);

    /* sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 30.0f); */

    {
        sdl3d_render_profile p = sdl3d_profile_modern();
        sdl3d_set_render_profile(ctx, &p);
    }

    last_time = SDL_GetPerformanceCounter();

    while (running)
    {
        SDL_Event ev;
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last_time) / (float)SDL_GetPerformanceFrequency();
        const bool *keys;
        float fx, fz, rx, rz;
        sdl3d_camera3d cam;
        sdl3d_color sky = {0, 0, 0, 255};

        last_time = now;
        if (dt > 0.1f)
            dt = 0.1f;
        game_time += dt;

        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
                running = false;
            else if (ev.type == SDL_EVENT_KEY_DOWN)
            {
                switch (ev.key.key)
                {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_1:
                case SDLK_2:
                case SDLK_3:
                case SDLK_4:
                case SDLK_5: {
                    current_profile = ev.key.key - SDLK_1;
                    sdl3d_render_profile p = profile_fns[current_profile]();
                    sdl3d_set_render_profile(ctx, &p);
                    fprintf(stderr, "Profile: %s\n", profile_names[current_profile]);
                }
                break;
                case SDLK_6:
                    show_bloom = !show_bloom;
                    sdl3d_set_bloom_enabled(ctx, show_bloom);
                    fprintf(stderr, "Bloom: %s\n", show_bloom ? "ON" : "OFF");
                    break;
                case SDLK_7:
                    show_ssao = !show_ssao;
                    sdl3d_set_ssao_enabled(ctx, show_ssao);
                    fprintf(stderr, "SSAO: %s\n", show_ssao ? "ON" : "OFF");
                    break;
                case SDLK_8:
                    show_fog = !show_fog;
                    fprintf(stderr, "Fog: %s\n", show_fog ? "ON" : "OFF");
                    break;
                default:
                    break;
                }
            }
            else if (ev.type == SDL_EVENT_MOUSE_MOTION)
            {
                if (!mouse_initialized)
                {
                    mouse_initialized = true;
                }
                else
                {
                    player.yaw += ev.motion.xrel * MOUSE_SENS;
                    player.pitch -= ev.motion.yrel * MOUSE_SENS;
                    if (player.pitch > 1.4f)
                        player.pitch = 1.4f;
                    if (player.pitch < -1.4f)
                        player.pitch = -1.4f;
                }
            }
        }

        keys = SDL_GetKeyboardState(NULL);
        fx = sinf(player.yaw);
        fz = -cosf(player.yaw);
        rx = cosf(player.yaw);
        rz = sinf(player.yaw);

        if (keys[SDL_SCANCODE_W])
        {
            player.x += fx * MOVE_SPEED * dt;
            player.z += fz * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_S])
        {
            player.x -= fx * MOVE_SPEED * dt;
            player.z -= fz * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_A])
        {
            player.x -= rx * MOVE_SPEED * dt;
            player.z -= rz * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_D])
        {
            player.x += rx * MOVE_SPEED * dt;
            player.z += rz * MOVE_SPEED * dt;
        }

        if (show_fog)
            sdl3d_set_fog(ctx, &fog);
        else
            sdl3d_clear_fog(ctx);

        cam.position = sdl3d_vec3_make(player.x, player.y, player.z);
        cam.target = sdl3d_vec3_make(player.x + sinf(player.yaw) * cosf(player.pitch), player.y + sinf(player.pitch),
                                     player.z - cosf(player.yaw) * cosf(player.pitch));
        cam.up = sdl3d_vec3_make(0, 1, 0);
        cam.fovy = 75.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        sdl3d_clear_render_context(ctx, sky);
        sdl3d_set_backface_culling_enabled(ctx, true);
        sdl3d_begin_mode_3d(ctx, cam);

        sdl3d_draw_skybox_gradient(ctx, (sdl3d_color){0, 0, 0, 255}, (sdl3d_color){0, 0, 0, 255});
        draw_doom_scene(ctx);

        /* Nukage glow emissive */
        sdl3d_set_emissive(ctx, 0.0f, 2.0f + sinf(game_time * 2.0f) * 0.5f, 0.0f);
        sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, -0.25f, 18), (sdl3d_vec2){5.5f, 3.5f},
                         (sdl3d_color){20, 200, 20, 220});
        sdl3d_set_emissive(ctx, 0.0f, 0.0f, 0.0f);

        /* Red warning light pulse in exit room */
        sdl3d_set_emissive(ctx, 3.0f + 2.0f * sinf(game_time * 4.0f), 0.0f, 0.0f);
        sdl3d_draw_sphere(ctx, sdl3d_vec3_make(18, 2.8f, 25), 0.15f, 6, 6, (sdl3d_color){255, 30, 20, 255});
        sdl3d_set_emissive(ctx, 0.0f, 0.0f, 0.0f);

        sdl3d_end_mode_3d(ctx);
        sdl3d_present_render_context(ctx);
    }

    sdl3d_destroy_render_context(ctx);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

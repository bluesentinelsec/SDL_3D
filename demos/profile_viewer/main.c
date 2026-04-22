/*
 * SDL3D render profile demo.
 * Press 1-5 or SPACE to cycle: Modern, PS1, N64, DOS, SNES.
 * ESC to quit.
 */

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/lighting.h"
#include "sdl3d/sdl3d.h"

#include <math.h>
#include <stdio.h>

#define RENDER_W 320
#define RENDER_H 240

typedef struct
{
    const char *name;
    sdl3d_render_profile (*fn)(void);
} profile_entry;

static const profile_entry profiles[] = {
    {"Modern (Phong + ACES)", sdl3d_profile_modern}, {"PS1 (Gouraud + Affine + Snap)", sdl3d_profile_ps1},
    {"N64 (Gouraud + Bilinear)", sdl3d_profile_n64}, {"DOS (Gouraud + 256-color dither)", sdl3d_profile_dos},
    {"SNES (Flat + Affine)", sdl3d_profile_snes},
};
static const int profile_count = 5;

static void draw_scene(sdl3d_render_context *ctx, float angle)
{
    sdl3d_camera3d cam;
    sdl3d_color sky = {150, 180, 210, 255};
    sdl3d_color ground = {80, 130, 60, 255};
    sdl3d_color brown = {120, 80, 40, 255};
    sdl3d_color green = {40, 140, 40, 255};
    sdl3d_color gray = {100, 100, 105, 255};
    sdl3d_color flame = {255, 180, 50, 255};
    sdl3d_color stone = {80, 75, 70, 255};
    sdl3d_color mountain = {90, 85, 80, 255};
    sdl3d_vec2 plane_sz = {20.0f, 20.0f};
    int i;

    cam.position = sdl3d_vec3_make(cosf(angle) * 10.0f, 5.0f, sinf(angle) * 10.0f);
    cam.target = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 55.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, sky);
    sdl3d_set_backface_culling_enabled(ctx, true);
    sdl3d_begin_mode_3d(ctx, cam);

    sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, 0, 0), plane_sz, ground);

    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(-3, 1, -4), 0.2f, 0.2f, 2, 8, brown);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(-3, 2.5f, -4), 1.2f, 8, 8, green);
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(4, 1.2f, -2), 0.25f, 0.25f, 2.4f, 8, brown);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(4, 3, -2), 1.5f, 8, 8, green);
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(-5, 0.8f, 3), 0.15f, 0.15f, 1.6f, 8, brown);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(-5, 2, 3), 1.0f, 8, 8, green);
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(2, 1, 5), 0.2f, 0.2f, 2, 8, brown);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(2, 2.5f, 5), 1.3f, 8, 8, green);

    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(1.5f, 0.2f, -1), 0.4f, 6, 6, gray);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(-2, 0.15f, 1.5f), 0.3f, 6, 6, gray);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(0.5f, 0.25f, 2.5f), 0.5f, 6, 6, gray);

    for (i = 0; i < 8; ++i)
    {
        float a = (float)i * 3.14159f * 2.0f / 8.0f;
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(cosf(a) * 0.6f, 0.1f, sinf(a) * 0.6f),
                        sdl3d_vec3_make(0.15f, 0.15f, 0.15f), stone);
    }
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(0, 0.3f, 0), 0.15f, 6, 6, flame);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(0, -3, -15), 8.0f, 12, 12, mountain);

    sdl3d_end_mode_3d(ctx);
}

int main(int argc, char *argv[])
{
    SDL_Window *win;
    SDL_Renderer *ren;
    sdl3d_render_context *ctx;
    sdl3d_render_context_config cfg;
    sdl3d_light sun, fire;
    sdl3d_fog fog;
    int current = 0;
    float angle = 0.0f;
    bool running = true;
    char title[128];

    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        return 1;
    }

    win = SDL_CreateWindow("SDL3D Profiles", 800, 600, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, NULL);
    sdl3d_init_render_context_config(&cfg);
    cfg.logical_width = RENDER_W;
    cfg.logical_height = RENDER_H;
    cfg.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;
    sdl3d_create_render_context(win, ren, &cfg, &ctx);

    /* Lighting. */
    SDL_zerop(&sun);
    sun.type = SDL3D_LIGHT_DIRECTIONAL;
    sun.direction = sdl3d_vec3_make(0.4f, -0.8f, -0.3f);
    sun.color[0] = 1.0f;
    sun.color[1] = 0.95f;
    sun.color[2] = 0.8f;
    sun.intensity = 2.5f;
    sdl3d_add_light(ctx, &sun);

    SDL_zerop(&fire);
    fire.type = SDL3D_LIGHT_POINT;
    fire.position = sdl3d_vec3_make(0, 0.5f, 0);
    fire.color[0] = 1.0f;
    fire.color[1] = 0.6f;
    fire.color[2] = 0.2f;
    fire.intensity = 3.0f;
    fire.range = 8.0f;
    sdl3d_add_light(ctx, &fire);

    /* Fog. */
    SDL_zerop(&fog);
    fog.mode = SDL3D_FOG_EXP2;
    fog.color[0] = 0.6f;
    fog.color[1] = 0.7f;
    fog.color[2] = 0.8f;
    fog.density = 0.04f;
    sdl3d_set_fog(ctx, &fog);

    /* Start with Modern profile. */
    {
        sdl3d_render_profile p = profiles[current].fn();
        sdl3d_set_render_profile(ctx, &p);
    }

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN)
            {
                int next = -1;
                switch (ev.key.key)
                {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_SPACE:
                    next = (current + 1) % profile_count;
                    break;
                case SDLK_1:
                    next = 0;
                    break;
                case SDLK_2:
                    next = 1;
                    break;
                case SDLK_3:
                    next = 2;
                    break;
                case SDLK_4:
                    next = 3;
                    break;
                case SDLK_5:
                    next = 4;
                    break;
                default:
                    break;
                }
                if (next >= 0 && next != current)
                {
                    current = next;
                    sdl3d_render_profile p = profiles[current].fn();
                    sdl3d_set_render_profile(ctx, &p);
                    SDL_snprintf(title, sizeof(title), "SDL3D Profiles - [%d] %s", current + 1, profiles[current].name);
                    SDL_SetWindowTitle(win, title);
                    fprintf(stderr, "Profile: %s\n", profiles[current].name);
                }
            }
        }

        angle += 0.005f;
        draw_scene(ctx, angle);
        sdl3d_present_render_context(ctx);
    }

    sdl3d_destroy_render_context(ctx);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

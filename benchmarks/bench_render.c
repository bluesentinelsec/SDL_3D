/*
 * Headless rendering benchmark for profiling the software 3D pipeline.
 *
 * Renders a fixed scene (ground plane, shapes with PBR lighting, fog,
 * tonemapping) for a configurable number of frames without presenting
 * to a window. Designed to run under perf or other profilers in CI.
 *
 * Usage: slayer3d_bench [frame_count]   (default: 60)
 */

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "slayer3d/lighting.h"
#include "slayer3d/slayer3d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Fog and tonemap may not be available until M4-4/5/6 lands. */
#ifdef SLAYER3D_FOG_LINEAR
#define BENCH_HAS_FOG 1
#else
#define BENCH_HAS_FOG 0
#endif

#ifdef SLAYER3D_TONEMAP_REINHARD
#define BENCH_HAS_TONEMAP 1
#else
#define BENCH_HAS_TONEMAP 0
#endif

#define BENCH_WIDTH 320
#define BENCH_HEIGHT 240

static void draw_scene(slayer3d_render_context *ctx, float angle)
{
    slayer3d_camera3d camera;
    slayer3d_color sky = {150, 180, 210, 255};
    slayer3d_color ground = {80, 130, 60, 255};
    slayer3d_color brown = {120, 80, 40, 255};
    slayer3d_color green = {40, 140, 40, 255};
    slayer3d_color gray = {100, 100, 105, 255};
    slayer3d_color flame = {255, 180, 50, 255};
    slayer3d_vec2 plane_size = {20.0f, 20.0f};
    int i;

    camera.position = slayer3d_vec3_make(cosf(angle) * 10.0f, 5.0f, sinf(angle) * 10.0f);
    camera.target = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    camera.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    camera.fovy = 55.0f;
    camera.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, sky);
    slayer3d_set_backface_culling_enabled(ctx, true);
    slayer3d_begin_mode_3d(ctx, camera);

    /* Ground. */
    slayer3d_draw_plane(ctx, slayer3d_vec3_make(0, 0, 0), plane_size, ground);

    /* Trees: trunk + canopy. */
    slayer3d_draw_cylinder(ctx, slayer3d_vec3_make(-3, 1.0f, -4), 0.2f, 0.2f, 2.0f, 8, brown);
    slayer3d_draw_sphere(ctx, slayer3d_vec3_make(-3, 2.5f, -4), 1.2f, 8, 8, green);
    slayer3d_draw_cylinder(ctx, slayer3d_vec3_make(4, 1.2f, -2), 0.25f, 0.25f, 2.4f, 8, brown);
    slayer3d_draw_sphere(ctx, slayer3d_vec3_make(4, 3.0f, -2), 1.5f, 8, 8, green);
    slayer3d_draw_cylinder(ctx, slayer3d_vec3_make(-5, 0.8f, 3), 0.15f, 0.15f, 1.6f, 8, brown);
    slayer3d_draw_sphere(ctx, slayer3d_vec3_make(-5, 2.0f, 3), 1.0f, 8, 8, green);
    slayer3d_draw_cylinder(ctx, slayer3d_vec3_make(2, 1.0f, 5), 0.2f, 0.2f, 2.0f, 8, brown);
    slayer3d_draw_sphere(ctx, slayer3d_vec3_make(2, 2.5f, 5), 1.3f, 8, 8, green);

    /* Rocks. */
    slayer3d_draw_sphere(ctx, slayer3d_vec3_make(1.5f, 0.2f, -1), 0.4f, 6, 6, gray);
    slayer3d_draw_sphere(ctx, slayer3d_vec3_make(-2, 0.15f, 1.5f), 0.3f, 6, 6, gray);
    slayer3d_draw_sphere(ctx, slayer3d_vec3_make(0.5f, 0.25f, 2.5f), 0.5f, 6, 6, gray);

    /* Campfire ring. */
    for (i = 0; i < 8; ++i)
    {
        float a = (float)i * 3.14159f * 2.0f / 8.0f;
        slayer3d_color stone = {80, 75, 70, 255};
        slayer3d_draw_cube(ctx, slayer3d_vec3_make(cosf(a) * 0.6f, 0.1f, sinf(a) * 0.6f),
                           slayer3d_vec3_make(0.15f, 0.15f, 0.15f), stone);
    }
    slayer3d_draw_sphere(ctx, slayer3d_vec3_make(0, 0.3f, 0), 0.15f, 6, 6, flame);

    /* Distant mountain. */
    slayer3d_draw_sphere(ctx, slayer3d_vec3_make(0, -3, -15), 8.0f, 12, 12, gray);

    slayer3d_end_mode_3d(ctx);
}

int main(int argc, char *argv[])
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    slayer3d_render_context *ctx = NULL;
    slayer3d_render_context_config config;
    slayer3d_light sun, campfire;
#if BENCH_HAS_FOG
    slayer3d_fog fog;
#else
    int fog;
#endif
    int frame_count = 60;
    int frame;
    Uint64 start_ticks, end_ticks;
    float elapsed_ms;

    if (argc > 1)
    {
        frame_count = atoi(argv[1]);
        if (frame_count <= 0)
        {
            frame_count = 60;
        }
    }

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("bench", BENCH_WIDTH, BENCH_HEIGHT, SDL_WINDOW_HIDDEN);
    if (!window)
    {
        fprintf(stderr, "Window: %s\n", SDL_GetError());
        return 1;
    }

    renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer)
    {
        fprintf(stderr, "Renderer: %s\n", SDL_GetError());
        return 1;
    }

    slayer3d_init_render_context_config(&config);
    if (!slayer3d_create_render_context(window, renderer, &config, &ctx))
    {
        fprintf(stderr, "Context: %s\n", SDL_GetError());
        return 1;
    }

    /* Lighting. */
    slayer3d_set_lighting_enabled(ctx, true);
    slayer3d_set_ambient_light(ctx, 0.15f, 0.18f, 0.25f);

    SDL_zerop(&sun);
    sun.type = SLAYER3D_LIGHT_DIRECTIONAL;
    sun.direction = slayer3d_vec3_make(0.4f, -0.8f, -0.3f);
    sun.color[0] = 1.0f;
    sun.color[1] = 0.95f;
    sun.color[2] = 0.8f;
    sun.intensity = 2.5f;
    slayer3d_add_light(ctx, &sun);

    SDL_zerop(&campfire);
    campfire.type = SLAYER3D_LIGHT_POINT;
    campfire.position = slayer3d_vec3_make(0, 0.5f, 0);
    campfire.color[0] = 1.0f;
    campfire.color[1] = 0.6f;
    campfire.color[2] = 0.2f;
    campfire.intensity = 3.0f;
    campfire.range = 8.0f;
    slayer3d_add_light(ctx, &campfire);

    /* Fog + tonemap. */
#if BENCH_HAS_FOG
    SDL_zerop(&fog);
    fog.mode = SLAYER3D_FOG_EXP2;
    fog.color[0] = 0.6f;
    fog.color[1] = 0.7f;
    fog.color[2] = 0.8f;
    fog.density = 0.04f;
    slayer3d_set_fog(ctx, &fog);
#else
    (void)fog;
#endif
#if BENCH_HAS_TONEMAP
    slayer3d_set_tonemap_mode(ctx, SLAYER3D_TONEMAP_ACES);
#endif

    /* Benchmark loop. */
    fprintf(stderr, "Rendering %d frames at %dx%d...\n", frame_count, BENCH_WIDTH, BENCH_HEIGHT);
    start_ticks = SDL_GetPerformanceCounter();

    for (frame = 0; frame < frame_count; ++frame)
    {
        draw_scene(ctx, (float)frame * 0.05f);
        /* No present — headless. */
    }

    end_ticks = SDL_GetPerformanceCounter();
    elapsed_ms = (float)(end_ticks - start_ticks) / (float)SDL_GetPerformanceFrequency() * 1000.0f;

    fprintf(stderr, "Done: %.1f ms total, %.2f ms/frame, %.1f FPS\n", elapsed_ms, elapsed_ms / (float)frame_count,
            (float)frame_count / (elapsed_ms / 1000.0f));

    slayer3d_destroy_render_context(ctx);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

/*
 * SDL3D 1080p Showcase — Demonstrates the full feature set at native HD.
 *
 * Controls:
 *   WASD/Arrow keys — move
 *   Mouse           — look
 *   1-5             — render profiles (Modern/PS1/N64/DOS/SNES)
 *   Tab             — toggle backend (Software ↔ OpenGL)
 *   Space           — jump
 *   ESC             — quit
 */

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/effects.h"
#include "sdl3d/lighting.h"
#include "sdl3d/sdl3d.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef SDL3D_DEMO_ASSETS_DIR
#define SDL3D_DEMO_ASSETS_DIR "demos/assets"
#endif

#define WINDOW_W 1920
#define WINDOW_H 1080
#define MOVE_SPEED 5.0f
#define MOUSE_SENS 0.002f
#define GRAVITY 18.0f
#define JUMP_VEL 7.0f
#define EYE_HEIGHT 1.7f

typedef struct
{
    float x, y, z;
    float yaw, pitch;
    float vy;
    bool on_ground;
} player_t;

/* ------------------------------------------------------------------ */
/* Scene geometry                                                      */
/* ------------------------------------------------------------------ */

static void draw_building(sdl3d_render_context *ctx, float x, float z, float w, float h, float d, sdl3d_color color)
{
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(x, h * 0.5f, z), sdl3d_vec3_make(w, h, d), color);
}

static void draw_tree(sdl3d_render_context *ctx, float x, float z)
{
    sdl3d_color brown = {100, 70, 40, 255};
    sdl3d_color green = {30, 130, 30, 255};
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(x, 1.5f, z), 0.12f, 0.18f, 3.0f, 8, brown);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(x, 3.8f, z), 1.4f, 10, 10, green);
}

static void draw_lamp(sdl3d_render_context *ctx, float x, float z)
{
    sdl3d_color dark = {50, 50, 55, 255};
    sdl3d_color glow = {255, 240, 190, 255};
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(x, 1.75f, z), 0.04f, 0.06f, 3.5f, 6, dark);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(x, 3.7f, z), 0.22f, 8, 8, glow);
}

static void draw_scene(sdl3d_render_context *ctx)
{
    sdl3d_color ground = {65, 115, 45, 255};
    sdl3d_color path_col = {145, 135, 115, 255};
    sdl3d_color wall = {165, 155, 145, 255};
    sdl3d_color roof = {145, 55, 45, 255};
    sdl3d_color blue_wall = {95, 115, 155, 255};
    sdl3d_color stone = {85, 80, 75, 255};
    sdl3d_color water = {35, 75, 135, 180};
    sdl3d_color red_brick = {140, 65, 55, 255};

    /* Ground */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, 0, 0), (sdl3d_vec2){80, 80}, ground);

    /* Stone path */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0.01f, 0), sdl3d_vec3_make(2.5f, 0.02f, 40), path_col);

    /* Buildings */
    draw_building(ctx, -8, -6, 5, 6, 5, wall);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-8, 6.8f, -6), sdl3d_vec3_make(5.5f, 2.0f, 5.5f), roof);

    draw_building(ctx, 8, -4, 4, 4.5f, 6, blue_wall);
    draw_building(ctx, 8, 10, 6, 7, 5, wall);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(8, 7.8f, 10), sdl3d_vec3_make(6.5f, 2.0f, 5.5f), roof);

    draw_building(ctx, -9, 12, 4, 5, 4, red_brick);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-9, 5.8f, 12), sdl3d_vec3_make(4.5f, 1.8f, 4.5f), roof);

    draw_building(ctx, 14, -10, 3, 3, 3, blue_wall);
    draw_building(ctx, -14, -8, 5, 8, 4, wall);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-14, 8.8f, -8), sdl3d_vec3_make(5.5f, 2.0f, 4.5f), roof);

    /* Stone wall */
    for (int i = -14; i <= 14; ++i)
    {
        sdl3d_draw_cube(ctx, sdl3d_vec3_make((float)i * 1.2f, 0.5f, -20), sdl3d_vec3_make(1.0f, 1.0f, 0.4f), stone);
    }

    /* Trees */
    draw_tree(ctx, -4, 4);
    draw_tree(ctx, 4, -9);
    draw_tree(ctx, -12, -12);
    draw_tree(ctx, 12, 6);
    draw_tree(ctx, -5, 18);
    draw_tree(ctx, 10, -14);
    draw_tree(ctx, -15, 8);
    draw_tree(ctx, 15, 14);
    draw_tree(ctx, 0, -14);
    draw_tree(ctx, 6, 20);
    draw_tree(ctx, -18, 0);
    draw_tree(ctx, 18, -5);

    /* Street lamps */
    draw_lamp(ctx, -1.8f, -6);
    draw_lamp(ctx, 1.8f, 0);
    draw_lamp(ctx, -1.8f, 6);
    draw_lamp(ctx, 1.8f, 14);
    draw_lamp(ctx, -1.8f, -14);

    /* Pond */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(12, 0.05f, -9), (sdl3d_vec2){5, 5}, water);

    /* Rocks */
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(10.2f, 0.25f, -7.2f), 0.35f, 5, 5, stone);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(13.8f, 0.3f, -10.5f), 0.4f, 5, 5, stone);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(11, 0.2f, -11.5f), 0.28f, 5, 5, stone);

    /* Decorative columns */
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(-3, 1.5f, -8), 0.25f, 0.3f, 3.0f, 10, stone);
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(3, 1.5f, -8), 0.25f, 0.3f, 3.0f, 10, stone);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(-3, 3.1f, -8), 0.35f, 6, 6, stone);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(3, 3.1f, -8), 0.35f, 6, 6, stone);
}

/* ------------------------------------------------------------------ */
/* Backend management                                                  */
/* ------------------------------------------------------------------ */

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

    w = SDL_CreateWindow("SDL3D 1080p Showcase", WINDOW_W, WINDOW_H, flags);
    if (w == NULL)
    {
        return false;
    }

    if (backend != SDL3D_BACKEND_SDLGPU)
    {
        r = SDL_CreateRenderer(w, NULL);
        if (r == NULL)
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

static void destroy_backend(SDL_Window **win, SDL_Renderer **ren, sdl3d_render_context **ctx)
{
    sdl3d_destroy_render_context(*ctx);
    *ctx = NULL;
    SDL_DestroyRenderer(*ren);
    *ren = NULL;
    SDL_DestroyWindow(*win);
    *win = NULL;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    sdl3d_render_context *ctx = NULL;
    player_t player = {0, EYE_HEIGHT, 12, 0, 0, 0, true};
    sdl3d_light sun, lamp[5];
    sdl3d_fog fog;
    sdl3d_post_process_config post;
    sdl3d_particle_emitter *fire_em;
    sdl3d_particle_emitter *smoke_em;
    sdl3d_particle_config fire_cfg, smoke_cfg;
    sdl3d_model box_model, duck_model, man_model;
    bool has_box = false, has_duck = false, has_man = false;
    sdl3d_mat4 *man_joints = NULL;
    float man_anim_time = 0.0f;
    float man_walk_pos = 0.0f;
    float man_walk_dir = 1.0f;
    bool running = true;
    bool show_neon = true;
    bool show_bloom = true;
    bool show_ssao = true;
    bool show_fog = true;
    bool mouse_initialized = false;
    Uint64 last_time;
    int current_profile = 0;
    sdl3d_backend current_backend = SDL3D_BACKEND_SDLGPU;
    const char *profile_names[] = {"Modern", "PS1", "N64", "DOS", "SNES"};
    sdl3d_render_profile (*profile_fns[])(void) = {sdl3d_profile_modern, sdl3d_profile_ps1, sdl3d_profile_n64,
                                                   sdl3d_profile_dos, sdl3d_profile_snes};

    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        return 1;
    }

    if (!create_backend(&win, &ren, &ctx, current_backend))
    {
        fprintf(stderr, "Backend init failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* --- Lighting (nighttime) --- */
    sdl3d_set_ambient_light(ctx, 0.04f, 0.04f, 0.06f);

    SDL_zerop(&sun);
    sun.type = SDL3D_LIGHT_DIRECTIONAL;
    sun.direction = sdl3d_vec3_make(0.3f, -0.9f, -0.3f);
    sun.color[0] = 0.3f;
    sun.color[1] = 0.35f;
    sun.color[2] = 0.5f;
    sun.intensity = 0.25f;
    sdl3d_add_light(ctx, &sun);

    {
        float lamp_x[] = {0.0f, -15.0f, 15.0f, -10.0f, 10.0f};
        float lamp_z[] = {0.0f, -15.0f, -15.0f, 15.0f, 15.0f};
        float lamp_r[] = {1.0f, 0.2f, 0.1f, 1.0f, 0.5f};
        float lamp_g[] = {0.9f, 0.6f, 0.4f, 0.3f, 1.0f};
        float lamp_b[] = {0.3f, 1.0f, 1.0f, 0.1f, 0.2f};
        for (int i = 0; i < 5; ++i)
        {
            SDL_zerop(&lamp[i]);
            lamp[i].type = SDL3D_LIGHT_POINT;
            lamp[i].position = sdl3d_vec3_make(lamp_x[i], 6.0f, lamp_z[i]);
            lamp[i].color[0] = lamp_r[i];
            lamp[i].color[1] = lamp_g[i];
            lamp[i].color[2] = lamp_b[i];
            lamp[i].intensity = 5.0f;
            lamp[i].range = 20.0f;
            sdl3d_add_light(ctx, &lamp[i]);
        }
    }

    /* --- Fog (nighttime) --- */
    SDL_zerop(&fog);
    fog.mode = SDL3D_FOG_EXP2;
    fog.color[0] = 0.03f;
    fog.color[1] = 0.03f;
    fog.color[2] = 0.06f;
    fog.density = 0.025f;
    sdl3d_set_fog(ctx, &fog);

    /* --- Shadows (moonlight casts shadows) --- */
    sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 30.0f);

    SDL_zerop(&post);
    post.effects = SDL3D_POST_BLOOM | SDL3D_POST_VIGNETTE;
    post.bloom_threshold = 0.8f;
    post.bloom_intensity = 0.2f;
    post.vignette_intensity = 0.4f;

    /* --- Particles --- */
    SDL_zerop(&fire_cfg);
    fire_cfg.position = sdl3d_vec3_make(0, 0.2f, 0);
    fire_cfg.direction = sdl3d_vec3_make(0, 1, 0);
    fire_cfg.spread = 0.25f;
    fire_cfg.speed_min = 0.6f;
    fire_cfg.speed_max = 1.8f;
    fire_cfg.lifetime_min = 0.3f;
    fire_cfg.lifetime_max = 0.7f;
    fire_cfg.size_start = 0.1f;
    fire_cfg.size_end = 0.02f;
    fire_cfg.color_start = (sdl3d_color){255, 200, 50, 255};
    fire_cfg.color_end = (sdl3d_color){255, 40, 0, 0};
    fire_cfg.gravity = -1.5f;
    fire_cfg.max_particles = 150;
    fire_cfg.emit_rate = 50.0f;
    fire_em = sdl3d_create_particle_emitter(&fire_cfg);

    SDL_zerop(&smoke_cfg);
    smoke_cfg.position = sdl3d_vec3_make(0, 0.9f, 0);
    smoke_cfg.direction = sdl3d_vec3_make(0, 1, 0);
    smoke_cfg.spread = 0.4f;
    smoke_cfg.speed_min = 0.2f;
    smoke_cfg.speed_max = 0.6f;
    smoke_cfg.lifetime_min = 1.2f;
    smoke_cfg.lifetime_max = 2.8f;
    smoke_cfg.size_start = 0.06f;
    smoke_cfg.size_end = 0.18f;
    smoke_cfg.color_start = (sdl3d_color){90, 90, 90, 160};
    smoke_cfg.color_end = (sdl3d_color){60, 60, 60, 0};
    smoke_cfg.gravity = -0.4f;
    smoke_cfg.max_particles = 80;
    smoke_cfg.emit_rate = 15.0f;
    smoke_em = sdl3d_create_particle_emitter(&smoke_cfg);

    /* --- Load models --- */
    {
        char path[512];
        SDL_snprintf(path, sizeof(path), "%s/BoxTextured.glb", SDL3D_DEMO_ASSETS_DIR);
        if (sdl3d_load_model_from_file(path, &box_model))
        {
            has_box = true;
        }
        SDL_snprintf(path, sizeof(path), "%s/Duck.glb", SDL3D_DEMO_ASSETS_DIR);
        if (sdl3d_load_model_from_file(path, &duck_model))
        {
            has_duck = true;
        }
        SDL_snprintf(path, sizeof(path), "%s/CesiumMan.glb", SDL3D_DEMO_ASSETS_DIR);
        if (sdl3d_load_model_from_file(path, &man_model))
        {
            has_man = true;
            if (man_model.skeleton != NULL)
            {
                man_joints = (sdl3d_mat4 *)SDL_calloc((size_t)man_model.skeleton->joint_count, sizeof(sdl3d_mat4));
            }
        }
    }

    /* Default profile */
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
        sdl3d_color sky = {8, 10, 20, 255};

        last_time = now;
        if (dt > 0.1f)
        {
            dt = 0.1f;
        }

        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            else if (ev.type == SDL_EVENT_KEY_DOWN)
            {
                int next = -1;
                switch (ev.key.key)
                {
                case SDLK_ESCAPE:
                    running = false;
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
                case SDLK_SPACE:
                    if (player.on_ground)
                    {
                        player.vy = JUMP_VEL;
                        player.on_ground = false;
                    }
                    break;
                case SDLK_6:
                    show_neon = !show_neon;
                    fprintf(stderr, "Neon: %s\n", show_neon ? "ON" : "OFF");
                    break;
                case SDLK_7:
                    show_bloom = !show_bloom;
                    sdl3d_set_bloom_enabled(ctx, show_bloom);
                    fprintf(stderr, "Bloom: %s\n", show_bloom ? "ON" : "OFF");
                    break;
                case SDLK_8:
                    show_ssao = !show_ssao;
                    sdl3d_set_ssao_enabled(ctx, show_ssao);
                    fprintf(stderr, "SSAO: %s\n", show_ssao ? "ON" : "OFF");
                    break;
                case SDLK_9:
                    show_fog = !show_fog;
                    fprintf(stderr, "Fog: %s\n", show_fog ? "ON" : "OFF");
                    break;
                case SDLK_TAB: {
                    sdl3d_backend nb =
                        (current_backend == SDL3D_BACKEND_SOFTWARE) ? SDL3D_BACKEND_SDLGPU : SDL3D_BACKEND_SOFTWARE;
                    destroy_backend(&win, &ren, &ctx);
                    if (!create_backend(&win, &ren, &ctx, nb))
                    {
                        create_backend(&win, &ren, &ctx, SDL3D_BACKEND_SOFTWARE);
                        nb = SDL3D_BACKEND_SOFTWARE;
                    }
                    current_backend = nb;
                    sdl3d_set_ambient_light(ctx, 0.04f, 0.04f, 0.06f);
                    sdl3d_add_light(ctx, &sun);
                    for (int i = 0; i < 5; ++i)
                    {
                        sdl3d_add_light(ctx, &lamp[i]);
                    }
                    sdl3d_set_fog(ctx, &fog);
                    sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 30.0f);
                    sdl3d_set_bloom_enabled(ctx, show_bloom);
                    sdl3d_set_ssao_enabled(ctx, show_ssao);
                    {
                        sdl3d_render_profile p = profile_fns[current_profile]();
                        sdl3d_set_render_profile(ctx, &p);
                    }
                    fprintf(stderr, "Backend: %s\n", current_backend == SDL3D_BACKEND_SOFTWARE ? "Software" : "OpenGL");
                }
                break;
                default:
                    break;
                }
                if (next >= 0)
                {
                    current_profile = next;
                    sdl3d_render_profile p = profile_fns[current_profile]();
                    sdl3d_set_render_profile(ctx, &p);
                    fprintf(stderr, "Profile: %s\n", profile_names[current_profile]);
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
                    {
                        player.pitch = 1.4f;
                    }
                    if (player.pitch < -1.4f)
                    {
                        player.pitch = -1.4f;
                    }
                }
            }
        }

        /* Movement */
        keys = SDL_GetKeyboardState(NULL);
        fx = sinf(player.yaw);
        fz = -cosf(player.yaw);
        rx = cosf(player.yaw);
        rz = sinf(player.yaw);

        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
        {
            player.x += fx * MOVE_SPEED * dt;
            player.z += fz * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
        {
            player.x -= fx * MOVE_SPEED * dt;
            player.z -= fz * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
        {
            player.x -= rx * MOVE_SPEED * dt;
            player.z -= rz * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
        {
            player.x += rx * MOVE_SPEED * dt;
            player.z += rz * MOVE_SPEED * dt;
        }

        /* Gravity */
        player.vy -= GRAVITY * dt;
        player.y += player.vy * dt;
        if (player.y < EYE_HEIGHT)
        {
            player.y = EYE_HEIGHT;
            player.vy = 0;
            player.on_ground = true;
        }

        /* Update particles */
        sdl3d_particle_emitter_update(fire_em, dt);
        sdl3d_particle_emitter_update(smoke_em, dt);

        /* Animate CesiumMan */
        if (has_man && man_model.animation_count > 0 && man_model.skeleton != NULL && man_joints != NULL)
        {
            man_anim_time += dt;
            if (man_model.animations[0].duration > 0.0f)
            {
                man_anim_time = fmodf(man_anim_time, man_model.animations[0].duration);
            }
            sdl3d_evaluate_animation(man_model.skeleton, &man_model.animations[0], man_anim_time, man_joints);
            /* Walk back and forth along Z axis. */
            man_walk_pos += man_walk_dir * 2.0f * dt;
            if (man_walk_pos > 8.0f)
            {
                man_walk_dir = -1.0f;
            }
            else if (man_walk_pos < -8.0f)
            {
                man_walk_dir = 1.0f;
            }
        }

        /* Camera */
        cam.position = sdl3d_vec3_make(player.x, player.y, player.z);
        cam.target = sdl3d_vec3_make(player.x + sinf(player.yaw) * cosf(player.pitch), player.y + sinf(player.pitch),
                                     player.z - cosf(player.yaw) * cosf(player.pitch));
        cam.up = sdl3d_vec3_make(0, 1, 0);
        cam.fovy = 65.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        /* Shadow pass is now automatic via the deferred draw list. */

        /* Render */
        sdl3d_clear_render_context(ctx, sky);
        sdl3d_set_backface_culling_enabled(ctx, true);

        if (show_fog)
        {
            sdl3d_set_fog(ctx, &fog);
        }
        else
        {
            sdl3d_clear_fog(ctx);
        }

        sdl3d_begin_mode_3d(ctx, cam);

        sdl3d_draw_skybox_gradient(ctx, (sdl3d_color){5, 8, 18, 255}, (sdl3d_color){12, 15, 30, 255});
        draw_scene(ctx);

        /* Neon signs */
        if (show_neon)
        {
            /* Green neon on front face of building at (-8, -6), face at z=-3.5 */
            sdl3d_set_emissive(ctx, 0.0f, 8.0f, 1.5f);
            sdl3d_draw_cube(ctx, sdl3d_vec3_make(-8.0f, 4.0f, -3.4f), sdl3d_vec3_make(3.0f, 1.2f, 0.15f),
                            (sdl3d_color){0, 255, 80, 255});

            /* Pink neon on front face of building at (8, -4), face at z=-1 */
            sdl3d_set_emissive(ctx, 8.0f, 0.5f, 5.0f);
            sdl3d_draw_cube(ctx, sdl3d_vec3_make(8.0f, 3.0f, -0.9f), sdl3d_vec3_make(2.5f, 0.8f, 0.15f),
                            (sdl3d_color){255, 50, 220, 255});

            sdl3d_set_emissive(ctx, 0.0f, 0.0f, 0.0f);
        }

        /* Models */
        if (has_box)
        {
            sdl3d_draw_model(ctx, &box_model, sdl3d_vec3_make(-2.5f, 0.5f, -2), 1.0f,
                             (sdl3d_color){255, 255, 255, 255});
            sdl3d_draw_model(ctx, &box_model, sdl3d_vec3_make(-3.0f, 0.5f, -1), 0.8f,
                             (sdl3d_color){255, 255, 255, 255});
            sdl3d_draw_model(ctx, &box_model, sdl3d_vec3_make(-2.2f, 1.3f, -1.5f), 0.5f,
                             (sdl3d_color){255, 255, 255, 255});
        }
        if (has_duck)
        {
            sdl3d_draw_model_ex(ctx, &duck_model, sdl3d_vec3_make(12, 0.1f, -8), sdl3d_vec3_make(0, 1, 0), 1.5f,
                                sdl3d_vec3_make(0.005f, 0.005f, 0.005f), (sdl3d_color){255, 255, 255, 255});
        }
        if (has_man)
        {
            sdl3d_draw_model_skinned(ctx, &man_model, sdl3d_vec3_make(-5, 0.0f, man_walk_pos), sdl3d_vec3_make(1, 0, 0),
                                     sdl3d_degrees_to_radians(-90.0f), sdl3d_vec3_make(1.5f, 1.5f, 1.5f),
                                     (sdl3d_color){255, 255, 255, 255}, man_joints);
        }

        sdl3d_draw_particles(ctx, fire_em);
        sdl3d_draw_particles(ctx, smoke_em);

        sdl3d_end_mode_3d(ctx);
        sdl3d_apply_post_process(ctx, &post);
        sdl3d_present_render_context(ctx);
    }

    /* Cleanup */
    sdl3d_destroy_particle_emitter(fire_em);
    sdl3d_destroy_particle_emitter(smoke_em);
    if (has_box)
    {
        sdl3d_free_model(&box_model);
    }
    if (has_duck)
    {
        sdl3d_free_model(&duck_model);
    }
    if (has_man)
    {
        sdl3d_free_model(&man_model);
    }
    SDL_free(man_joints);
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

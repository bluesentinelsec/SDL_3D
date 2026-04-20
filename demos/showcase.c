/*
 * SDL3D Showcase Demo — First-person walkthrough demonstrating all
 * software 3D renderer features.
 *
 * Features demonstrated:
 * - First-person camera with WASD + mouse look
 * - PBR lighting: directional sun + point lights
 * - Fog (exponential)
 * - Tonemapping (ACES)
 * - Skybox gradient
 * - Particle effects (fire, smoke)
 * - Post-process (bloom + vignette)
 * - Render profile switching (1-5 keys)
 * - Shape primitives as scene geometry
 * - Collision detection (ground plane)
 *
 * Controls:
 *   WASD      — move
 *   Mouse     — look
 *   1-5       — render profiles (Modern/PS1/N64/DOS/SNES)
 *   Tab       — toggle backend (Software ↔ OpenGL)
 *   Space     — jump
 *   ESC       — quit
 *
 * Build:
 *   cmake -DSDL3D_BUILD_DEMOS=ON
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

#define RENDER_W 400
#define RENDER_H 300
#define MOVE_SPEED 4.0f
#define MOUSE_SENS 0.002f
#define GRAVITY 15.0f
#define JUMP_VEL 6.0f
#define EYE_HEIGHT 1.7f

typedef struct
{
    float x, y, z;
    float yaw, pitch;
    float vy;
    bool on_ground;
} player_t;

static void draw_building(sdl3d_render_context *ctx, float x, float z, float w, float h, float d, sdl3d_color color)
{
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(x, h * 0.5f, z), sdl3d_vec3_make(w, h, d), color);
}

static void draw_tree(sdl3d_render_context *ctx, float x, float z)
{
    sdl3d_color brown = {100, 70, 40, 255};
    sdl3d_color green = {30, 120, 30, 255};
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(x, 1.5f, z), 0.15f, 0.15f, 3.0f, 6, brown);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(x, 3.5f, z), 1.2f, 8, 8, green);
}

static void draw_lamp(sdl3d_render_context *ctx, float x, float z)
{
    sdl3d_color dark = {60, 60, 60, 255};
    sdl3d_color glow = {255, 240, 200, 255};
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(x, 1.5f, z), 0.05f, 0.05f, 3.0f, 6, dark);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(x, 3.2f, z), 0.2f, 6, 6, glow);
}

static void draw_scene(sdl3d_render_context *ctx)
{
    sdl3d_color ground = {70, 110, 50, 255};
    sdl3d_color path_color = {140, 130, 110, 255};
    sdl3d_color wall = {160, 150, 140, 255};
    sdl3d_color roof = {140, 60, 50, 255};
    sdl3d_color blue_wall = {100, 120, 160, 255};
    sdl3d_color stone = {90, 85, 80, 255};
    sdl3d_color water = {40, 80, 140, 200};

    /* Ground. */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, 0, 0), (sdl3d_vec2){60, 60}, ground);

    /* Stone path. */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0.01f, 0), sdl3d_vec3_make(2, 0.02f, 30), path_color);

    /* Buildings. */
    draw_building(ctx, -6, -5, 4, 5, 4, wall);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-6, 5.5f, -5), sdl3d_vec3_make(4.5f, 1.5f, 4.5f), roof);

    draw_building(ctx, 6, -3, 3, 3.5f, 5, blue_wall);
    draw_building(ctx, 6, 8, 5, 6, 4, wall);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(6, 6.5f, 8), sdl3d_vec3_make(5.5f, 1.5f, 4.5f), roof);

    draw_building(ctx, -7, 10, 3, 4, 3, blue_wall);

    /* Stone wall / fence. */
    for (int i = -10; i <= 10; ++i)
    {
        sdl3d_draw_cube(ctx, sdl3d_vec3_make((float)i * 1.2f, 0.4f, -15), sdl3d_vec3_make(1.0f, 0.8f, 0.3f), stone);
    }

    /* Trees. */
    draw_tree(ctx, -3, 3);
    draw_tree(ctx, 3, -8);
    draw_tree(ctx, -10, -10);
    draw_tree(ctx, 10, 5);
    draw_tree(ctx, -4, 15);
    draw_tree(ctx, 8, -12);
    draw_tree(ctx, -12, 7);
    draw_tree(ctx, 12, 12);

    /* Street lamps. */
    draw_lamp(ctx, -1.5f, -5);
    draw_lamp(ctx, 1.5f, 5);
    draw_lamp(ctx, -1.5f, 12);

    /* Pond. */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(10, 0.05f, -8), (sdl3d_vec2){4, 4}, water);

    /* Rocks around pond. */
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(8.5f, 0.2f, -6.5f), 0.3f, 5, 5, stone);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(11.5f, 0.25f, -9), 0.35f, 5, 5, stone);
    sdl3d_draw_sphere(ctx, sdl3d_vec3_make(9, 0.15f, -10), 0.25f, 5, 5, stone);
}

int main(int argc, char *argv[])
{
    SDL_Window *win;
    SDL_Renderer *ren;
    sdl3d_render_context *ctx;
    sdl3d_render_context_config cfg;
    player_t player = {0, EYE_HEIGHT, 10, 0, 0, 0, true};
    sdl3d_particle_emitter *fire_emitter;
    sdl3d_particle_emitter *smoke_emitter;
    sdl3d_particle_config fire_cfg, smoke_cfg;
    sdl3d_light sun, lamp1, lamp2, lamp3;
    sdl3d_fog fog;
    sdl3d_post_process_config post;
    bool running = true;
    Uint64 last_time;
    int current_profile = 0;
    sdl3d_backend current_backend = SDL3D_BACKEND_SOFTWARE;
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

    /* Set GL attributes before window creation for OpenGL support. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    win = SDL_CreateWindow("SDL3D Showcase", 960, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    ren = SDL_CreateRenderer(win, NULL);
    sdl3d_init_render_context_config(&cfg);
    cfg.logical_width = RENDER_W;
    cfg.logical_height = RENDER_H;
    cfg.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;
    sdl3d_create_render_context(win, ren, &cfg, &ctx);

    SDL_SetWindowRelativeMouseMode(win, true);

    /* Lighting. */
    SDL_zerop(&sun);
    sun.type = SDL3D_LIGHT_DIRECTIONAL;
    sun.direction = sdl3d_vec3_make(0.3f, -0.7f, -0.5f);
    sun.color[0] = 1.0f;
    sun.color[1] = 0.95f;
    sun.color[2] = 0.85f;
    sun.intensity = 1.2f;
    sdl3d_add_light(ctx, &sun);

    SDL_zerop(&lamp1);
    lamp1.type = SDL3D_LIGHT_POINT;
    lamp1.position = sdl3d_vec3_make(-1.5f, 3.0f, -5);
    lamp1.color[0] = 1.0f;
    lamp1.color[1] = 0.9f;
    lamp1.color[2] = 0.7f;
    lamp1.intensity = 2.5f;
    lamp1.range = 10.0f;
    sdl3d_add_light(ctx, &lamp1);

    SDL_zerop(&lamp2);
    lamp2.type = SDL3D_LIGHT_POINT;
    lamp2.position = sdl3d_vec3_make(1.5f, 3.0f, 5);
    lamp2.color[0] = 1.0f;
    lamp2.color[1] = 0.9f;
    lamp2.color[2] = 0.7f;
    lamp2.intensity = 2.5f;
    lamp2.range = 10.0f;
    sdl3d_add_light(ctx, &lamp2);

    SDL_zerop(&lamp3);
    lamp3.type = SDL3D_LIGHT_POINT;
    lamp3.position = sdl3d_vec3_make(-1.5f, 3.0f, 12);
    lamp3.color[0] = 1.0f;
    lamp3.color[1] = 0.9f;
    lamp3.color[2] = 0.7f;
    lamp3.intensity = 2.5f;
    lamp3.range = 10.0f;
    sdl3d_add_light(ctx, &lamp3);

    /* Fog. */
    SDL_zerop(&fog);
    fog.mode = SDL3D_FOG_EXP2;
    fog.color[0] = 0.55f;
    fog.color[1] = 0.65f;
    fog.color[2] = 0.75f;
    fog.density = 0.025f;
    sdl3d_set_fog(ctx, &fog);

    /* Post-process. */
    SDL_zerop(&post);
    post.effects = SDL3D_POST_BLOOM | SDL3D_POST_VIGNETTE;
    post.bloom_threshold = 0.7f;
    post.bloom_intensity = 0.25f;
    post.vignette_intensity = 0.6f;

    /* Fire particles. */
    SDL_zerop(&fire_cfg);
    fire_cfg.position = sdl3d_vec3_make(0, 0.2f, 0);
    fire_cfg.direction = sdl3d_vec3_make(0, 1, 0);
    fire_cfg.spread = 0.3f;
    fire_cfg.speed_min = 0.5f;
    fire_cfg.speed_max = 1.5f;
    fire_cfg.lifetime_min = 0.3f;
    fire_cfg.lifetime_max = 0.8f;
    fire_cfg.size_start = 0.08f;
    fire_cfg.size_end = 0.02f;
    fire_cfg.color_start = (sdl3d_color){255, 200, 50, 255};
    fire_cfg.color_end = (sdl3d_color){255, 50, 0, 0};
    fire_cfg.gravity = -1.0f;
    fire_cfg.max_particles = 100;
    fire_cfg.emit_rate = 40.0f;
    fire_emitter = sdl3d_create_particle_emitter(&fire_cfg);

    /* Smoke particles. */
    SDL_zerop(&smoke_cfg);
    smoke_cfg.position = sdl3d_vec3_make(0, 0.8f, 0);
    smoke_cfg.direction = sdl3d_vec3_make(0, 1, 0);
    smoke_cfg.spread = 0.5f;
    smoke_cfg.speed_min = 0.3f;
    smoke_cfg.speed_max = 0.8f;
    smoke_cfg.lifetime_min = 1.0f;
    smoke_cfg.lifetime_max = 2.5f;
    smoke_cfg.size_start = 0.05f;
    smoke_cfg.size_end = 0.15f;
    smoke_cfg.color_start = (sdl3d_color){80, 80, 80, 180};
    smoke_cfg.color_end = (sdl3d_color){60, 60, 60, 0};
    smoke_cfg.gravity = -0.5f;
    smoke_cfg.max_particles = 60;
    smoke_cfg.emit_rate = 12.0f;
    smoke_emitter = sdl3d_create_particle_emitter(&smoke_cfg);

    /* Load glTF models. */
    sdl3d_model box_model, duck_model, man_model;
    bool has_box = false, has_duck = false, has_man = false;
    sdl3d_mat4 *man_joint_matrices = NULL;
    float man_anim_time = 0.0f;
    {
        char path[512];
        SDL_snprintf(path, sizeof(path), "%s/BoxTextured.glb", SDL3D_DEMO_ASSETS_DIR);
        if (sdl3d_load_model_from_file(path, &box_model))
        {
            has_box = true;
            fprintf(stderr, "Loaded: BoxTextured.glb (%d meshes, %d materials)\n", box_model.mesh_count,
                    box_model.material_count);
        }
        else
        {
            fprintf(stderr, "Could not load BoxTextured.glb: %s\n", SDL_GetError());
        }

        SDL_snprintf(path, sizeof(path), "%s/Duck.glb", SDL3D_DEMO_ASSETS_DIR);
        if (sdl3d_load_model_from_file(path, &duck_model))
        {
            has_duck = true;
            fprintf(stderr, "Loaded: Duck.glb (%d meshes, %d materials)\n", duck_model.mesh_count,
                    duck_model.material_count);
        }
        else
        {
            fprintf(stderr, "Could not load Duck.glb: %s\n", SDL_GetError());
        }

        SDL_snprintf(path, sizeof(path), "%s/CesiumMan.glb", SDL3D_DEMO_ASSETS_DIR);
        if (sdl3d_load_model_from_file(path, &man_model))
        {
            has_man = true;
            fprintf(stderr, "Loaded: CesiumMan.glb (%d meshes, %d materials, %d animations)\n", man_model.mesh_count,
                    man_model.material_count, man_model.animation_count);
            if (man_model.skeleton != NULL)
            {
                man_joint_matrices =
                    (sdl3d_mat4 *)SDL_calloc((size_t)man_model.skeleton->joint_count, sizeof(sdl3d_mat4));
            }
        }
        else
        {
            fprintf(stderr, "Could not load CesiumMan.glb: %s\n", SDL_GetError());
        }
    }

    /* Default profile. */
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
        float forward_x, forward_z, right_x, right_z;
        sdl3d_camera3d cam;
        sdl3d_color sky = {140, 175, 210, 255};

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
            if (ev.type == SDL_EVENT_KEY_DOWN)
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
                case SDLK_TAB: {
                    /* Toggle backend: destroy and recreate render context. */
                    sdl3d_backend new_backend =
                        (current_backend == SDL3D_BACKEND_SOFTWARE) ? SDL3D_BACKEND_SDLGPU : SDL3D_BACKEND_SOFTWARE;
                    sdl3d_destroy_render_context(ctx);
                    ctx = NULL;
                    sdl3d_render_context_config new_cfg;
                    sdl3d_init_render_context_config(&new_cfg);
                    new_cfg.backend = new_backend;
                    new_cfg.logical_width = RENDER_W;
                    new_cfg.logical_height = RENDER_H;
                    new_cfg.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;
                    if (sdl3d_create_render_context(win, ren, &new_cfg, &ctx))
                    {
                        current_backend = new_backend;
                        /* Re-apply lighting and fog. */
                        sdl3d_add_light(ctx, &sun);
                        sdl3d_add_light(ctx, &lamp1);
                        sdl3d_add_light(ctx, &lamp2);
                        sdl3d_add_light(ctx, &lamp3);
                        sdl3d_set_fog(ctx, &fog);
                        {
                            sdl3d_render_profile p = profile_fns[current_profile]();
                            sdl3d_set_render_profile(ctx, &p);
                        }
                        fprintf(stderr, "Backend: %s\n",
                                current_backend == SDL3D_BACKEND_SOFTWARE ? "Software" : "OpenGL");
                    }
                    else
                    {
                        fprintf(stderr, "Backend switch failed: %s\n", SDL_GetError());
                        /* Fall back to software. */
                        new_cfg.backend = SDL3D_BACKEND_SOFTWARE;
                        sdl3d_create_render_context(win, ren, &new_cfg, &ctx);
                        current_backend = SDL3D_BACKEND_SOFTWARE;
                        sdl3d_add_light(ctx, &sun);
                        sdl3d_add_light(ctx, &lamp1);
                        sdl3d_add_light(ctx, &lamp2);
                        sdl3d_add_light(ctx, &lamp3);
                        sdl3d_set_fog(ctx, &fog);
                        {
                            sdl3d_render_profile p = profile_fns[current_profile]();
                            sdl3d_set_render_profile(ctx, &p);
                        }
                    }
                }
                break;
                default:
                    break;
                }
                if (next >= 0)
                {
                    current_profile = next;
                    {
                        sdl3d_render_profile p = profile_fns[current_profile]();
                        sdl3d_set_render_profile(ctx, &p);
                    }
                    fprintf(stderr, "Profile: %s\n", profile_names[current_profile]);
                }
            }
            if (ev.type == SDL_EVENT_MOUSE_MOTION)
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

        /* Movement. */
        keys = SDL_GetKeyboardState(NULL);
        forward_x = sinf(player.yaw);
        forward_z = -cosf(player.yaw);
        right_x = cosf(player.yaw);
        right_z = sinf(player.yaw);

        if (keys[SDL_SCANCODE_W])
        {
            player.x += forward_x * MOVE_SPEED * dt;
            player.z += forward_z * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_S])
        {
            player.x -= forward_x * MOVE_SPEED * dt;
            player.z -= forward_z * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_A])
        {
            player.x -= right_x * MOVE_SPEED * dt;
            player.z -= right_z * MOVE_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_D])
        {
            player.x += right_x * MOVE_SPEED * dt;
            player.z += right_z * MOVE_SPEED * dt;
        }

        /* Gravity. */
        player.vy -= GRAVITY * dt;
        player.y += player.vy * dt;
        if (player.y < EYE_HEIGHT)
        {
            player.y = EYE_HEIGHT;
            player.vy = 0;
            player.on_ground = true;
        }

        /* Update particles. */
        sdl3d_particle_emitter_update(fire_emitter, dt);
        sdl3d_particle_emitter_update(smoke_emitter, dt);

        /* Advance CesiumMan animation. */
        if (has_man && man_model.animation_count > 0 && man_model.skeleton != NULL && man_joint_matrices != NULL)
        {
            man_anim_time += dt;
            if (man_model.animations[0].duration > 0.0f)
            {
                man_anim_time = fmodf(man_anim_time, man_model.animations[0].duration);
            }
            sdl3d_evaluate_animation(man_model.skeleton, &man_model.animations[0], man_anim_time, man_joint_matrices);
        }

        /* Camera. */
        cam.position = sdl3d_vec3_make(player.x, player.y, player.z);
        cam.target = sdl3d_vec3_make(player.x + sinf(player.yaw) * cosf(player.pitch), player.y + sinf(player.pitch),
                                     player.z - cosf(player.yaw) * cosf(player.pitch));
        cam.up = sdl3d_vec3_make(0, 1, 0);
        cam.fovy = 70.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        /* Render. */
        sdl3d_clear_render_context(ctx, sky);
        sdl3d_set_backface_culling_enabled(ctx, true);
        sdl3d_begin_mode_3d(ctx, cam);

        sdl3d_draw_skybox_gradient(ctx, (sdl3d_color){100, 150, 220, 255}, (sdl3d_color){200, 210, 220, 255});

        draw_scene(ctx);

        /* Draw loaded models placed around the scene. */
        if (has_box)
        {
            /* Textured crates on the path. */
            sdl3d_draw_model(ctx, &box_model, sdl3d_vec3_make(-2, 0.5f, -2), 1.0f, (sdl3d_color){255, 255, 255, 255});
            sdl3d_draw_model(ctx, &box_model, sdl3d_vec3_make(-2.5f, 0.5f, -1), 0.7f,
                             (sdl3d_color){255, 255, 255, 255});
            sdl3d_draw_model(ctx, &box_model, sdl3d_vec3_make(-1.8f, 1.2f, -1.5f), 0.5f,
                             (sdl3d_color){255, 255, 255, 255});
        }
        if (has_duck)
        {
            /* Duck by the pond. */
            sdl3d_draw_model_ex(ctx, &duck_model, sdl3d_vec3_make(10, 0.1f, -7), sdl3d_vec3_make(0, 1, 0), 2.0f,
                                sdl3d_vec3_make(0.005f, 0.005f, 0.005f), (sdl3d_color){255, 255, 255, 255});
        }
        if (has_man)
        {
            sdl3d_draw_model_skinned(ctx, &man_model, sdl3d_vec3_make(-4, 1.0f, 5), sdl3d_vec3_make(1, 0, 0),
                                     sdl3d_degrees_to_radians(-90.0f), sdl3d_vec3_make(1.5f, 1.5f, 1.5f),
                                     (sdl3d_color){255, 255, 255, 255}, man_joint_matrices);
        }

        sdl3d_draw_particles(ctx, fire_emitter);
        sdl3d_draw_particles(ctx, smoke_emitter);

        sdl3d_end_mode_3d(ctx);

        sdl3d_apply_post_process(ctx, &post);
        sdl3d_present_render_context(ctx);
    }

    sdl3d_destroy_particle_emitter(fire_emitter);
    sdl3d_destroy_particle_emitter(smoke_emitter);
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
    SDL_free(man_joint_matrices);
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

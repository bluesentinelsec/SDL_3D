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

/* Wall flags for draw_room — skip walls where rooms connect. */
#define WALL_WEST 1
#define WALL_EAST 2
#define WALL_SOUTH 4
#define WALL_NORTH 8
#define WALL_ALL (WALL_WEST | WALL_EAST | WALL_SOUTH | WALL_NORTH)

static void draw_room(sdl3d_render_context *ctx, float x, float z, float w, float d, float floor_y, float ceil_y,
                      sdl3d_color floor_col, sdl3d_color wall_col, sdl3d_color ceil_col, bool has_ceiling, int walls)
{
    float hw = w * 0.5f, hd = d * 0.5f;
    float wall_h = ceil_y - floor_y;
    float wall_cy = floor_y + wall_h * 0.5f;
    float t = 0.3f;
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(x, floor_y - 0.01f, z), (sdl3d_vec2){w + t, d + t}, floor_col);
    if (has_ceiling)
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(x, ceil_y, z), sdl3d_vec3_make(w + t, 0.2f, d + t), ceil_col);
    if (walls & WALL_WEST)
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(x - hw, wall_cy, z), sdl3d_vec3_make(t, wall_h, d), wall_col);
    if (walls & WALL_EAST)
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(x + hw, wall_cy, z), sdl3d_vec3_make(t, wall_h, d), wall_col);
    if (walls & WALL_SOUTH)
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(x, wall_cy, z - hd), sdl3d_vec3_make(w, wall_h, t), wall_col);
    if (walls & WALL_NORTH)
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(x, wall_cy, z + hd), sdl3d_vec3_make(w, wall_h, t), wall_col);
}

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

    /* Each room is a solid box. Doorways are where walls overlap
     * between adjacent rooms — the thicker room wall hides the
     * thinner corridor wall, creating a natural opening. */

    /* Room 1 → corridor 1: skip north wall of room 1, skip south wall of corridor */
    draw_room(ctx, 0, 0, 10, 8, 0, 4, dark_floor, wall_grey, dark_ceil, true, WALL_WEST | WALL_EAST | WALL_SOUTH);
    draw_room(ctx, 0, 8, 4, 8, 0, 3.5f, dark_floor, wall_dark, dark_ceil, true, WALL_WEST | WALL_EAST);
    /* Room 2: skip south (corridor 1 connects), skip east (corridor 2 connects) */
    draw_room(ctx, 0, 17, 12, 10, -0.5f, 4.5f, dark_floor, wall_brown, dark_ceil, true, WALL_WEST | WALL_NORTH);
    /* Corridor 2: skip west (room 2 connects), skip east (outdoor connects) */
    draw_room(ctx, 9, 17, 6, 4, 0, 3.5f, dark_floor, wall_dark, dark_ceil, true, WALL_SOUTH | WALL_NORTH);
    /* Outdoor: skip west (corridor 2 connects), skip north (room 3 connects) */
    draw_room(ctx, 18, 17, 12, 10, 0, 4, outdoor_floor, wall_dark, dark_ceil, false, WALL_EAST | WALL_SOUTH);
    /* Room 3: skip south (outdoor connects) */
    draw_room(ctx, 18, 25, 8, 6, 0, 3, dark_floor, wall_grey, dark_ceil, true, WALL_WEST | WALL_EAST | WALL_NORTH);

    /* Pillars */
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(-3, 2, -2), 0.4f, 0.4f, 4, 8, wall_brown);
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(3, 2, -2), 0.4f, 0.4f, 4, 8, wall_brown);

    /* Nukage pool + walkways */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, -0.3f, 17), (sdl3d_vec2){6, 4}, nukage);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-4, 0.1f, 17), sdl3d_vec3_make(1.5f, 0.5f, 8), metal);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(4, 0.1f, 17), sdl3d_vec3_make(1.5f, 0.5f, 8), metal);

    /* Outdoor crates + barrels */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(16, 0.5f, 14), sdl3d_vec3_make(1, 1, 1), wall_brown);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(17, 0.5f, 15), sdl3d_vec3_make(1, 1, 1), wall_brown);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(16.5f, 1.5f, 14.5f), sdl3d_vec3_make(1, 1, 1), wall_brown);
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(20, 0.6f, 15), 0.4f, 0.4f, 1.2f, 8, (sdl3d_color){60, 90, 60, 255});
    sdl3d_draw_cylinder(ctx, sdl3d_vec3_make(21, 0.6f, 19), 0.4f, 0.4f, 1.2f, 8, (sdl3d_color){60, 90, 60, 255});
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

static void on_zfight(const char *message, void *userdata)
{
    (void)userdata;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL3D", message, NULL);
    SDL_assert_always(0 && "SDL3D: Z-fighting detected.");
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
    sdl3d_model level_model;
    bool has_level = false;
    bool mouse_initialized = false;
    bool show_bloom = true;
    bool show_ssao = false;
    bool show_fog = false;
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

    /* Load level geometry as a glTF model — watertight, no seams. */
    if (sdl3d_load_model_from_file(SDL3D_DEMO_ASSETS_DIR "/doom_level.gltf", &level_model))
    {
        has_level = true;
        fprintf(stderr, "Loaded doom_level.gltf (%d meshes)\n", level_model.mesh_count);
    }
    else
    {
        fprintf(stderr, "Could not load doom_level.gltf: %s\n", SDL_GetError());
    }
    sdl3d_set_zfight_callback(ctx, on_zfight, NULL);
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
        sdl3d_color sky = {255, 255, 0, 255};

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
            else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT)
            {
                float look_x = sinf(player.yaw) * cosf(player.pitch);
                float look_y = sinf(player.pitch);
                float look_z = -cosf(player.yaw) * cosf(player.pitch);
                fprintf(stderr, "CLICK pos=(%.2f, %.2f, %.2f) look=(%.2f, %.2f, %.2f) yaw=%.2f pitch=%.2f\n", player.x,
                        player.y, player.z, look_x, look_y, look_z, player.yaw, player.pitch);
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
        sdl3d_set_backface_culling_enabled(ctx, false);
        sdl3d_begin_mode_3d(ctx, cam);

        /* Level geometry — culling disabled for interior-facing glTF mesh */
        if (has_level)
        {
            sdl3d_draw_model(ctx, &level_model, sdl3d_vec3_make(0, 0, 0), 1.0f, (sdl3d_color){255, 255, 255, 255});
        }

        /* Nukage glow emissive */
        sdl3d_set_emissive(ctx, 0.0f, 2.0f + sinf(game_time * 2.0f) * 0.5f, 0.0f);
        sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, -0.25f, 18), (sdl3d_vec2){5.5f, 3.5f},
                         (sdl3d_color){20, 200, 20, 220});
        sdl3d_set_emissive(ctx, 0.0f, 0.0f, 0.0f);

        /* Red warning light pulse in exit room */
        sdl3d_set_emissive(ctx, 3.0f + 2.0f * sinf(game_time * 4.0f), 0.0f, 0.0f);
        sdl3d_draw_sphere(ctx, sdl3d_vec3_make(18, 2.8f, 25), 0.15f, 6, 6, (sdl3d_color){255, 30, 20, 255});
        sdl3d_set_emissive(ctx, 0.0f, 0.0f, 0.0f);

        /* Crosshair: small bright cube 0.5 units in front of camera */
        {
            float cx = player.x + sinf(player.yaw) * cosf(player.pitch) * 0.5f;
            float cy = player.y + sinf(player.pitch) * 0.5f;
            float cz = player.z - cosf(player.yaw) * cosf(player.pitch) * 0.5f;
            sdl3d_set_emissive(ctx, 10.0f, 10.0f, 10.0f);
            sdl3d_draw_cube(ctx, sdl3d_vec3_make(cx, cy, cz), sdl3d_vec3_make(0.005f, 0.005f, 0.005f),
                            (sdl3d_color){255, 255, 255, 255});
            sdl3d_set_emissive(ctx, 0.0f, 0.0f, 0.0f);
        }

        sdl3d_end_mode_3d(ctx);
        sdl3d_present_render_context(ctx);
    }

    if (has_level)
        sdl3d_free_model(&level_model);
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

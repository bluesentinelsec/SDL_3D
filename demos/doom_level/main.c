/*
 * Doom-style level demo — sector-based level builder.
 * Expanded E1M1-scale-ish layout with multiple wings and color accents.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/font.h"
#include "sdl3d/level.h"
#include "sdl3d/lighting.h"
#include "sdl3d/sdl3d.h"

#define WINDOW_W 1280
#define WINDOW_H 720
#define SOFTWARE_W (WINDOW_W / 2)
#define SOFTWARE_H (WINDOW_H / 2)
#define MOVE_SPEED 12.0f
#define MOUSE_SENS 0.002f
#define PROJ_SPEED 20.0f
#define PROJ_LIFETIME 3.0f
#define ROCKET_LIGHT_R 1.0f
#define ROCKET_LIGHT_G 0.6f
#define ROCKET_LIGHT_B 0.2f
#define ROCKET_LIGHT_INTENSITY 4.0f
#define ROCKET_LIGHT_RANGE 4.0f

typedef struct demo_sprite_actor
{
    const sdl3d_texture2d *texture;
    sdl3d_vec3 position;
    sdl3d_vec2 size;
    bool bob;
    float bob_amplitude;
    float bob_speed;
} demo_sprite_actor;

typedef struct demo_actor_draw
{
    const demo_sprite_actor *actor;
    sdl3d_vec3 position;
    sdl3d_color tint;
    float distance_sq;
} demo_actor_draw;

static bool point_in_sector_xz(const sdl3d_sector *sector, float x, float z)
{
    bool inside = false;
    for (int i = 0, j = sector->num_points - 1; i < sector->num_points; j = i++)
    {
        float xi = sector->points[i][0];
        float zi = sector->points[i][1];
        float xj = sector->points[j][0];
        float zj = sector->points[j][1];
        bool crosses = ((zi > z) != (zj > z));
        if (!crosses)
            continue;
        float intersect_x = (xj - xi) * (z - zi) / (zj - zi) + xi;
        if (x < intersect_x)
            inside = !inside;
    }
    return inside;
}

static bool point_in_any_sector(const sdl3d_sector *sectors, int sector_count, float x, float y, float z)
{
    for (int i = 0; i < sector_count; i++)
    {
        const sdl3d_sector *sector = &sectors[i];
        if (y < sector->floor_y || y > sector->ceil_y)
            continue;
        if (point_in_sector_xz(sector, x, z))
            return true;
    }
    return false;
}

static void advance_projectile(const sdl3d_sector *sectors, int sector_count, float dt, bool *proj_active,
                               float *proj_x, float *proj_y, float *proj_z, float proj_dx, float proj_dy, float proj_dz,
                               float *proj_life)
{
    if (!*proj_active)
        return;

    float travel = PROJ_SPEED * dt;
    int steps = (int)SDL_ceilf(travel / 0.25f);
    if (steps < 1)
        steps = 1;
    float step_dist = travel / (float)steps;

    for (int i = 0; i < steps; i++)
    {
        float next_x = *proj_x + proj_dx * step_dist;
        float next_y = *proj_y + proj_dy * step_dist;
        float next_z = *proj_z + proj_dz * step_dist;
        if (!point_in_any_sector(sectors, sector_count, next_x, next_y, next_z))
        {
            *proj_active = false;
            return;
        }
        *proj_x = next_x;
        *proj_y = next_y;
        *proj_z = next_z;
    }

    *proj_life -= dt;
    if (*proj_life <= 0.0f)
        *proj_active = false;
}

static sdl3d_color sample_actor_tint(const sdl3d_level_light *lights, int light_count, sdl3d_vec3 position,
                                     bool proj_active, float proj_x, float proj_y, float proj_z)
{
    float r = 0.12f;
    float g = 0.12f;
    float b = 0.14f;

    for (int i = 0; i < light_count; ++i)
    {
        float dx = lights[i].position[0] - position.x;
        float dy = lights[i].position[1] - position.y;
        float dz = lights[i].position[2] - position.z;
        float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist >= lights[i].range || dist <= 0.0001f)
        {
            continue;
        }

        float t = 1.0f - (dist / lights[i].range);
        float atten = t * t;
        float scale = lights[i].intensity * atten * 0.28f;
        r += lights[i].color[0] * scale;
        g += lights[i].color[1] * scale;
        b += lights[i].color[2] * scale;
    }

    if (proj_active)
    {
        float dx = proj_x - position.x;
        float dy = proj_y - position.y;
        float dz = proj_z - position.z;
        float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist < ROCKET_LIGHT_RANGE && dist > 0.0001f)
        {
            float t = 1.0f - (dist / ROCKET_LIGHT_RANGE);
            float atten = t * t;
            float scale = ROCKET_LIGHT_INTENSITY * atten * 0.35f;
            r += ROCKET_LIGHT_R * scale;
            g += ROCKET_LIGHT_G * scale;
            b += ROCKET_LIGHT_B * scale;
        }
    }

    if (r > 1.0f)
        r = 1.0f;
    if (g > 1.0f)
        g = 1.0f;
    if (b > 1.0f)
        b = 1.0f;

    return (sdl3d_color){(Uint8)(r * 255.0f), (Uint8)(g * 255.0f), (Uint8)(b * 255.0f), 255};
}

static int compare_actor_draws(const void *lhs, const void *rhs)
{
    const demo_actor_draw *a = (const demo_actor_draw *)lhs;
    const demo_actor_draw *b = (const demo_actor_draw *)rhs;
    if (a->distance_sq < b->distance_sq)
        return 1;
    if (a->distance_sq > b->distance_sq)
        return -1;
    return 0;
}

static void strip_level_lightmap(sdl3d_level *level)
{
    SDL_free(level->lightmap_pixels);
    level->lightmap_pixels = NULL;
    level->lightmap_width = 0;
    level->lightmap_height = 0;
    sdl3d_free_texture(&level->lightmap_texture);

    for (int i = 0; i < level->model.mesh_count; ++i)
    {
        SDL_free(level->model.meshes[i].lightmap_uvs);
        level->model.meshes[i].lightmap_uvs = NULL;
    }
}

static bool create_backend(SDL_Window **out_win, SDL_Renderer **out_ren, sdl3d_render_context **out_ctx,
                           sdl3d_backend backend)
{
    const int logical_w = (backend == SDL3D_BACKEND_SOFTWARE) ? SOFTWARE_W : WINDOW_W;
    const int logical_h = (backend == SDL3D_BACKEND_SOFTWARE) ? SOFTWARE_H : WINDOW_H;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (backend == SDL3D_BACKEND_SDLGPU)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        flags |= SDL_WINDOW_OPENGL;
    }

    SDL_Window *w = SDL_CreateWindow("SDL3D \xe2\x80\x94 Doom Level", WINDOW_W, WINDOW_H, flags);
    if (!w)
        return false;

    SDL_Renderer *r = NULL;
    if (backend != SDL3D_BACKEND_SDLGPU)
    {
        r = SDL_CreateRenderer(w, NULL);
        if (!r)
        {
            SDL_DestroyWindow(w);
            return false;
        }
    }

    sdl3d_render_context_config cfg;
    sdl3d_init_render_context_config(&cfg);
    cfg.backend = backend;
    cfg.allow_backend_fallback = false;
    cfg.logical_width = logical_w;
    cfg.logical_height = logical_h;
    cfg.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    sdl3d_render_context *c = NULL;
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

int main(int argc, char *argv[])
{
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    sdl3d_render_context *ctx = NULL;
    sdl3d_texture2d enemy_tex = {0};
    sdl3d_texture2d health_tex = {0};
    sdl3d_texture2d sky_px = {0};
    sdl3d_texture2d sky_nx = {0};
    sdl3d_texture2d sky_py = {0};
    sdl3d_texture2d sky_ny = {0};
    sdl3d_texture2d sky_pz = {0};
    sdl3d_texture2d sky_nz = {0};

    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;

    sdl3d_backend current_backend = SDL3D_BACKEND_SDLGPU;
    if (!create_backend(&win, &ren, &ctx, current_backend))
    {
        SDL_Log("Backend init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Log("doom_level MOVE_SPEED=%.2f backend=%d render=%dx%d", MOVE_SPEED, (int)current_backend,
            sdl3d_get_render_context_width(ctx), sdl3d_get_render_context_height(ctx));

    sdl3d_set_bloom_enabled(ctx, true);
    sdl3d_set_ssao_enabled(ctx, false);
    sdl3d_set_point_shadows_enabled(ctx, false);
    sdl3d_set_backface_culling_enabled(ctx, true);
    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG);

    /* Load debug font. */
    sdl3d_font debug_font;
    bool has_font = sdl3d_load_font(SDL3D_MEDIA_DIR "/fonts/Roboto.ttf", 20.0f, &debug_font);
    if (!has_font)
    {
        SDL_Log("Font load failed: %s", SDL_GetError());
    }

    if (!sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/sprites/enemy.png", &enemy_tex) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/sprites/health-pack.png", &health_tex))
    {
        SDL_Log("Sprite load failed: %s", SDL_GetError());
        sdl3d_destroy_render_context(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    sdl3d_set_texture_filter(&enemy_tex, SDL3D_TEXTURE_FILTER_NEAREST);
    sdl3d_set_texture_filter(&health_tex, SDL3D_TEXTURE_FILTER_NEAREST);
    sdl3d_set_texture_wrap(&enemy_tex, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
    sdl3d_set_texture_wrap(&health_tex, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);

    if (!sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/px.png", &sky_px) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/nx.png", &sky_nx) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/py.png", &sky_py) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/ny.png", &sky_ny) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/pz.png", &sky_pz) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/nz.png", &sky_nz))
    {
        SDL_Log("Skybox load failed: %s", SDL_GetError());
        sdl3d_free_texture(&enemy_tex);
        sdl3d_free_texture(&health_tex);
        sdl3d_free_texture(&sky_px);
        sdl3d_free_texture(&sky_nx);
        sdl3d_free_texture(&sky_py);
        sdl3d_free_texture(&sky_ny);
        sdl3d_free_texture(&sky_pz);
        sdl3d_free_texture(&sky_nz);
        sdl3d_destroy_render_context(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    sdl3d_set_texture_wrap(&sky_px, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
    sdl3d_set_texture_wrap(&sky_nx, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
    sdl3d_set_texture_wrap(&sky_py, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
    sdl3d_set_texture_wrap(&sky_ny, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
    sdl3d_set_texture_wrap(&sky_pz, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
    sdl3d_set_texture_wrap(&sky_nz, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);

    /* ---- Material palette ---- */
    sdl3d_level_material mats[] = {
        {{1, 1, 1, 1}, 0, 0.9f, SDL3D_MEDIA_DIR "/textures/rock_floor.jpg", 4},    /* 0: rock floor */
        {{1, 1, 1, 1}, 0, 0.8f, SDL3D_MEDIA_DIR "/textures/ceiling_metal.jpg", 4}, /* 1: metal ceiling */
        {{1, 1, 1, 1}, 0, 0.7f, SDL3D_MEDIA_DIR "/textures/wall_metal.jpg", 4},    /* 2: metal walls */
        {{1, 1, 1, 1}, 0, 0.9f, SDL3D_MEDIA_DIR "/textures/lava.jpg", 4},          /* 3: lava */
        {{1, 1, 1, 1}, 0, 0.6f, SDL3D_MEDIA_DIR "/textures/wall_metal.jpg", 4},    /* 4: metal walls alt */
        {{1, 1, 1, 1}, 0, 0.9f, SDL3D_MEDIA_DIR "/textures/rock_floor.jpg", 4},    /* 5: rock floor alt */
    };

    /* ---- Sector definitions (expanded E1M1-inspired layout) ---- */
    /*
     * Layout (top-down, Z increases downward):
     *
     *   [7] Upper Hall -------- [8] Computer Core
     *      |                          |
     *   [0] Start Room                [9] Security Bend
     *      |                               |
     *   [1] South Corridor                 |
     *      |                               |
     *   [2] Nukage Basin -- [3] East Passage -- [4] Courtyard -- [10] Storage -- [12] Secret Annex
     *      |                                       |
     *   [6] West Alcove                           [5] Exit Room -- [11] Reactor Hall -- [13] Exterior Yard
     */
    sdl3d_sector sectors[] = {
        /* 0: Starting room */
        {{{0, 0}, {10, 0}, {10, 8}, {0, 8}}, 4, 0.0f, 4.0f, 0, 1, 2},
        /* 1: South corridor */
        {{{3, 8}, {7, 8}, {7, 16}, {3, 16}}, 4, 0.0f, 3.5f, 5, 1, 4},
        /* 2: Nukage basin */
        {{{-2, 16}, {10, 16}, {10, 26}, {-2, 26}}, 4, -0.5f, 4.5f, 3, 1, 2},
        /* 3: East passage */
        {{{10, 18}, {16, 18}, {16, 22}, {10, 22}}, 4, 0.0f, 3.5f, 0, 1, 4},
        /* 4: Courtyard (open roof) */
        {{{16, 14}, {28, 14}, {28, 26}, {16, 26}}, 4, 0.0f, 8.0f, 0, -1, 2},
        /* 5: Exit room */
        {{{20, 26}, {28, 26}, {28, 32}, {20, 32}}, 4, 0.0f, 3.0f, 5, 1, 4},
        /* 6: West alcove off nukage */
        {{{-10, 18}, {-2, 18}, {-2, 24}, {-10, 24}}, 4, -0.5f, 3.5f, 5, 1, 4},
        /* 7: Upper hall off start */
        {{{10, 2}, {18, 2}, {18, 6}, {10, 6}}, 4, 0.0f, 3.5f, 5, 1, 4},
        /* 8: Computer core */
        {{{18, 0}, {30, 0}, {30, 8}, {18, 8}}, 4, 0.0f, 4.0f, 0, 1, 2},
        /* 9: Security bend linking core to courtyard */
        {{{22, 8}, {26, 8}, {26, 14}, {22, 14}}, 4, 0.0f, 3.2f, 5, 1, 4},
        /* 10: Storage annex */
        {{{28, 12}, {38, 12}, {38, 24}, {28, 24}}, 4, 0.0f, 4.0f, 0, 1, 2},
        /* 11: Reactor hall beyond exit */
        {{{18, 32}, {30, 32}, {30, 44}, {18, 44}}, 4, 0.0f, 5.5f, 5, 1, 4},
        /* 12: Secret annex behind storage */
        {{{32, 24}, {40, 24}, {40, 30}, {32, 30}}, 4, 0.0f, 3.0f, 0, 1, 2},
        /* 13: Exterior yard (large open-air space for skybox side visibility) */
        {{{-6, 44}, {54, 44}, {54, 104}, {-6, 104}}, 4, 0.0f, 12.0f, 5, -1, 2},
    };

    sdl3d_level_light lights[] = {
        {{5, 3.5f, 4}, {1.0f, 0.82f, 0.58f}, 2.4f, 9.5f},     /* Start room — warm tungsten */
        {{5, 3.0f, 12}, {1.0f, 0.68f, 0.28f}, 1.4f, 7.0f},    /* South corridor — amber */
        {{4, 1.0f, 21}, {1.0f, 0.16f, 0.10f}, 3.1f, 12.0f},   /* Nukage basin — red */
        {{-6, 1.8f, 21}, {0.25f, 0.95f, 0.35f}, 2.4f, 8.5f},  /* West alcove — toxic green */
        {{14, 2.7f, 4}, {0.7f, 0.8f, 1.0f}, 1.3f, 7.0f},      /* Upper hall — cold white */
        {{24, 3.2f, 4}, {0.15f, 0.85f, 1.0f}, 2.4f, 10.5f},   /* Computer core — cyan */
        {{24, 2.6f, 11}, {1.0f, 0.9f, 0.45f}, 1.4f, 6.0f},    /* Security bend — sodium */
        {{22, 7.0f, 20}, {0.36f, 0.46f, 0.78f}, 2.3f, 14.0f}, /* Courtyard — moonlight */
        {{33, 3.0f, 18}, {0.9f, 0.35f, 1.0f}, 1.9f, 10.0f},   /* Storage — magenta */
        {{24, 2.5f, 29}, {0.2f, 1.0f, 0.2f}, 3.2f, 8.0f},     /* Exit — green */
        {{24, 3.8f, 38}, {0.45f, 0.35f, 1.0f}, 2.2f, 11.0f},  /* Reactor hall — violet */
        {{36, 2.2f, 27}, {1.0f, 0.55f, 0.22f}, 1.5f, 7.0f},   /* Secret annex — orange */
        {{24, 8.5f, 74}, {0.32f, 0.38f, 0.7f}, 2.4f, 32.0f},  /* Exterior yard — night sky fill */
    };
    const int sector_count = (int)SDL_arraysize(sectors);
    const int light_count = (int)SDL_arraysize(lights);

    /* Build three versions: lightmapped, vertex-baked fallback, and unlit. */
    sdl3d_level level_lightmapped, level_vertex_baked, level_unlit;
    if (!sdl3d_build_level(sectors, sector_count, mats, 6, lights, light_count, &level_lightmapped))
    {
        SDL_Log("Level build failed: %s", SDL_GetError());
        sdl3d_free_texture(&enemy_tex);
        sdl3d_free_texture(&health_tex);
        sdl3d_free_texture(&sky_px);
        sdl3d_free_texture(&sky_nx);
        sdl3d_free_texture(&sky_py);
        sdl3d_free_texture(&sky_ny);
        sdl3d_free_texture(&sky_pz);
        sdl3d_free_texture(&sky_nz);
        sdl3d_destroy_render_context(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    if (!sdl3d_build_level(sectors, sector_count, mats, 6, lights, light_count, &level_vertex_baked))
    {
        SDL_Log("Level build failed: %s", SDL_GetError());
        sdl3d_free_level(&level_lightmapped);
        sdl3d_free_texture(&enemy_tex);
        sdl3d_free_texture(&health_tex);
        sdl3d_free_texture(&sky_px);
        sdl3d_free_texture(&sky_nx);
        sdl3d_free_texture(&sky_py);
        sdl3d_free_texture(&sky_ny);
        sdl3d_free_texture(&sky_pz);
        sdl3d_free_texture(&sky_nz);
        sdl3d_destroy_render_context(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    strip_level_lightmap(&level_vertex_baked);
    if (!sdl3d_build_level(sectors, sector_count, mats, 6, NULL, 0, &level_unlit))
    {
        SDL_Log("Level build failed: %s", SDL_GetError());
        sdl3d_free_level(&level_lightmapped);
        sdl3d_free_level(&level_vertex_baked);
        sdl3d_free_texture(&enemy_tex);
        sdl3d_free_texture(&health_tex);
        sdl3d_free_texture(&sky_px);
        sdl3d_free_texture(&sky_nx);
        sdl3d_free_texture(&sky_py);
        sdl3d_free_texture(&sky_ny);
        sdl3d_free_texture(&sky_pz);
        sdl3d_free_texture(&sky_nz);
        sdl3d_destroy_render_context(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    bool use_lit_world = true;
    bool use_lightmaps = true;

    demo_sprite_actor actors[] = {
        {&enemy_tex, sdl3d_vec3_make(5.8f, 0.0f, 6.8f), (sdl3d_vec2){1.7f, 2.6f}, true, 0.10f, 7.0f},
        {&health_tex, sdl3d_vec3_make(5.0f, 0.25f, 10.8f), (sdl3d_vec2){1.0f, 1.0f}, true, 0.12f, 1.8f},
        {&enemy_tex, sdl3d_vec3_make(4.2f, -0.5f, 21.5f), (sdl3d_vec2){1.6f, 2.4f}, true, 0.08f, 6.0f},
        {&health_tex, sdl3d_vec3_make(24.0f, 0.25f, 4.5f), (sdl3d_vec2){1.0f, 1.0f}, true, 0.15f, 1.5f},
        {&enemy_tex, sdl3d_vec3_make(24.0f, 0.0f, 19.0f), (sdl3d_vec2){1.8f, 2.8f}, true, 0.09f, 6.5f},
        {&health_tex, sdl3d_vec3_make(24.0f, 0.25f, 28.5f), (sdl3d_vec2){1.0f, 1.0f}, true, 0.12f, 2.1f},
        {&enemy_tex, sdl3d_vec3_make(24.0f, 0.0f, 37.5f), (sdl3d_vec2){1.7f, 2.6f}, true, 0.09f, 5.8f},
        {&health_tex, sdl3d_vec3_make(35.5f, 0.25f, 27.0f), (sdl3d_vec2){1.0f, 1.0f}, true, 0.1f, 1.7f},
        {&enemy_tex, sdl3d_vec3_make(24.0f, 0.0f, 72.0f), (sdl3d_vec2){1.8f, 2.8f}, true, 0.11f, 5.5f},
        {&health_tex, sdl3d_vec3_make(10.0f, 0.25f, 84.0f), (sdl3d_vec2){1.0f, 1.0f}, true, 0.14f, 1.6f},
    };
    demo_actor_draw actor_draws[SDL_arraysize(actors)];
    float elapsed = 0.0f;
    sdl3d_skybox_textured skybox = {&sky_px, &sky_nx, &sky_py, &sky_ny, &sky_pz, &sky_nz, 350.0f};

    SDL_Log("Portals detected: %d", level_lightmapped.portal_count);
    for (int i = 0; i < level_lightmapped.portal_count; i++)
    {
        const sdl3d_level_portal *p = &level_lightmapped.portals[i];
        SDL_Log("  portal %d: sector %d <-> %d  x[%.1f,%.1f] z[%.1f,%.1f] y[%.2f,%.2f]", i, p->sector_a, p->sector_b,
                p->min_x, p->max_x, p->min_z, p->max_z, p->floor_y, p->ceil_y);
    }

    /* Visibility state. */
    bool sector_visible[SDL_arraysize(sectors)];
    sdl3d_visibility_result vis;
    vis.sector_visible = sector_visible;
    vis.visible_count = 0;
    bool show_debug = false;
    bool portal_culling = true;

    /* Player */
    float px = 5, py = 1.6f, pz = 4;
    float yaw = 3.14159f, pitch = 0;
    bool mouse_init = false;

    /* Projectile. */
    bool proj_active = false;
    float proj_x = 0, proj_y = 0, proj_z = 0;
    float proj_dx = 0, proj_dy = 0, proj_dz = 0;
    float proj_life = 0;

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
            {
                use_lit_world = !use_lit_world;
                SDL_Log("World lighting: %s", use_lit_world ? (use_lightmaps ? "LIGHTMAP" : "VERTEX-BAKED") : "UNLIT");
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_M)
            {
                use_lightmaps = !use_lightmaps;
                SDL_Log("Lightmap mode: %s", use_lightmaps ? "LIGHTMAP" : "VERTEX-BAKED");
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_F1)
            {
                show_debug = !show_debug;
                SDL_Log("Debug stats: %s", show_debug ? "ON" : "OFF");
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_F2)
            {
                portal_culling = !portal_culling;
                SDL_Log("Portal culling: %s", portal_culling ? "ON" : "OFF");
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_TAB)
            {
                sdl3d_backend next =
                    (current_backend == SDL3D_BACKEND_SDLGPU) ? SDL3D_BACKEND_SOFTWARE : SDL3D_BACKEND_SDLGPU;
                sdl3d_destroy_render_context(ctx);
                if (ren)
                    SDL_DestroyRenderer(ren);
                SDL_DestroyWindow(win);
                ctx = NULL;
                ren = NULL;
                win = NULL;
                if (create_backend(&win, &ren, &ctx, next))
                {
                    current_backend = next;
                    sdl3d_set_bloom_enabled(ctx, current_backend == SDL3D_BACKEND_SDLGPU);
                    sdl3d_set_ssao_enabled(ctx, false);
                    sdl3d_set_point_shadows_enabled(ctx, false);
                    sdl3d_set_backface_culling_enabled(ctx, true);
                    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG);
                    SDL_Log("Switched to %s backend render=%dx%d",
                            current_backend == SDL3D_BACKEND_SDLGPU ? "GL" : "SOFTWARE",
                            sdl3d_get_render_context_width(ctx), sdl3d_get_render_context_height(ctx));
                }
                else
                {
                    SDL_Log("Backend switch failed: %s — reverting", SDL_GetError());
                    create_backend(&win, &ren, &ctx, current_backend);
                    sdl3d_set_bloom_enabled(ctx, current_backend == SDL3D_BACKEND_SDLGPU);
                    sdl3d_set_ssao_enabled(ctx, false);
                    sdl3d_set_point_shadows_enabled(ctx, false);
                    sdl3d_set_backface_culling_enabled(ctx, true);
                    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG);
                }
            }
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
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT)
            {
                proj_active = true;
                proj_x = px;
                proj_y = py;
                proj_z = pz;
                proj_dx = SDL_sinf(yaw) * SDL_cosf(pitch);
                proj_dy = SDL_sinf(pitch);
                proj_dz = -SDL_cosf(yaw) * SDL_cosf(pitch);
                proj_life = PROJ_LIFETIME;
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;
        elapsed += dt;

        /* Look direction (for camera target). */
        float fx = SDL_sinf(yaw) * SDL_cosf(pitch);
        float fz = -SDL_cosf(yaw) * SDL_cosf(pitch);

        /* Doom-style movement: accumulate wish direction on XZ plane,
         * normalize so diagonal isn't faster, constant speed regardless of pitch. */
        float fwd_x = SDL_sinf(yaw);
        float fwd_z = -SDL_cosf(yaw);
        float right_x = SDL_cosf(yaw);
        float right_z = SDL_sinf(yaw);
        float wish_x = 0, wish_z = 0;

        const Uint8 *keys = (const Uint8 *)SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_W])
        {
            wish_x += fwd_x;
            wish_z += fwd_z;
        }
        if (keys[SDL_SCANCODE_S])
        {
            wish_x -= fwd_x;
            wish_z -= fwd_z;
        }
        if (keys[SDL_SCANCODE_A])
        {
            wish_x -= right_x;
            wish_z -= right_z;
        }
        if (keys[SDL_SCANCODE_D])
        {
            wish_x += right_x;
            wish_z += right_z;
        }

        /* Normalize wish direction. */
        float wish_len = SDL_sqrtf(wish_x * wish_x + wish_z * wish_z);
        if (wish_len > 0.001f)
        {
            wish_x /= wish_len;
            wish_z /= wish_len;
            px += wish_x * MOVE_SPEED * dt;
            pz += wish_z * MOVE_SPEED * dt;
        }

        sdl3d_camera3d cam;
        cam.position = sdl3d_vec3_make(px, py, pz);
        cam.target = sdl3d_vec3_make(px + fx, py + SDL_sinf(pitch), pz + fz);
        cam.up = sdl3d_vec3_make(0, 1, 0);
        cam.fovy = 75.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        /* Update projectile. */
        advance_projectile(sectors, sector_count, dt, &proj_active, &proj_x, &proj_y, &proj_z, proj_dx, proj_dy,
                           proj_dz, &proj_life);

        /* Compute portal visibility. */
        sdl3d_level *active_level =
            use_lit_world ? (use_lightmaps ? &level_lightmapped : &level_vertex_baked) : &level_unlit;
        int current_sector = sdl3d_level_find_sector(active_level, sectors, px, pz);
        sdl3d_vec3 cam_dir = sdl3d_vec3_make(fx, SDL_sinf(pitch), fz);

        sdl3d_clear_lights(ctx);
        if (proj_active)
        {
            sdl3d_light rocket = {0};
            rocket.type = SDL3D_LIGHT_POINT;
            rocket.position = sdl3d_vec3_make(proj_x, proj_y, proj_z);
            rocket.color[0] = ROCKET_LIGHT_R;
            rocket.color[1] = ROCKET_LIGHT_G;
            rocket.color[2] = ROCKET_LIGHT_B;
            rocket.intensity = ROCKET_LIGHT_INTENSITY;
            rocket.range = ROCKET_LIGHT_RANGE;
            sdl3d_add_light(ctx, &rocket);
        }

        sdl3d_clear_render_context(ctx, (sdl3d_color){10, 10, 15, 255});

        sdl3d_begin_mode_3d(ctx, cam);
        sdl3d_draw_skybox_textured(ctx, &skybox);

        /* Compute visibility using cached frustum planes from begin_mode_3d.
         * We pass the frustum planes through to the visibility system. */
        if (portal_culling)
        {
            sdl3d_mat4 vview, vproj;
            sdl3d_camera3d_compute_matrices(&cam, WINDOW_W, WINDOW_H, 0.01f, 1000.0f, &vview, &vproj);
            sdl3d_mat4 vp = sdl3d_mat4_multiply(vproj, vview);
            const float *m = vp.m;

            /* Gribb/Hartmann frustum plane extraction: left, right, bottom, top, near, far. */
            float fp[6][4];
            float raw[6][4] = {
                {m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]},
                {m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]},
                {m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]},
                {m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]},
                {m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]},
                {m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]},
            };
            for (int i = 0; i < 6; i++)
            {
                float len = SDL_sqrtf(raw[i][0] * raw[i][0] + raw[i][1] * raw[i][1] + raw[i][2] * raw[i][2]);
                if (len > 0.000001f)
                {
                    fp[i][0] = raw[i][0] / len;
                    fp[i][1] = raw[i][1] / len;
                    fp[i][2] = raw[i][2] / len;
                    fp[i][3] = raw[i][3] / len;
                }
                else
                {
                    fp[i][0] = fp[i][1] = fp[i][2] = 0.0f;
                    fp[i][3] = raw[i][3];
                }
            }

            sdl3d_level_compute_visibility(active_level, current_sector, cam.position, cam_dir, fp, &vis);
        }
        else
        {
            for (int i = 0; i < sector_count; i++)
                sector_visible[i] = true;
            vis.visible_count = sector_count;
        }

        /* Count visible mesh chunks for debug stats. */
        int visible_meshes = 0;
        if (portal_culling)
        {
            for (int i = 0; i < active_level->model.mesh_count; i++)
            {
                int sid = active_level->mesh_sector_ids[i];
                if (sid >= 0 && sid < sector_count && vis.sector_visible[sid])
                    visible_meshes++;
            }
        }
        else
        {
            visible_meshes = active_level->model.mesh_count;
        }

        sdl3d_draw_level(ctx, active_level, portal_culling ? &vis : NULL, (sdl3d_color){255, 255, 255, 255});

        {
            int actor_draw_count = 0;
            for (int i = 0; i < (int)SDL_arraysize(actors); ++i)
            {
                sdl3d_vec3 actor_pos = actors[i].position;
                if (actors[i].bob)
                {
                    actor_pos.y += SDL_sinf(elapsed * actors[i].bob_speed + (float)i) * actors[i].bob_amplitude;
                }

                if (portal_culling)
                {
                    int actor_sector = sdl3d_level_find_sector(active_level, sectors, actor_pos.x, actor_pos.z);
                    if (actor_sector >= 0 && actor_sector < sector_count && !vis.sector_visible[actor_sector])
                    {
                        continue;
                    }
                }

                actor_draws[actor_draw_count].actor = &actors[i];
                actor_draws[actor_draw_count].position = actor_pos;
                actor_draws[actor_draw_count].tint =
                    sample_actor_tint(lights, light_count, actor_pos, proj_active, proj_x, proj_y, proj_z);
                {
                    float dx = actor_pos.x - px;
                    float dy = actor_pos.y - py;
                    float dz = actor_pos.z - pz;
                    actor_draws[actor_draw_count].distance_sq = dx * dx + dy * dy + dz * dz;
                }
                actor_draw_count++;
            }

            SDL_qsort(actor_draws, (size_t)actor_draw_count, sizeof(actor_draws[0]), compare_actor_draws);

            for (int i = 0; i < actor_draw_count; ++i)
            {
                const demo_actor_draw *draw = &actor_draws[i];
                sdl3d_draw_billboard(ctx, draw->actor->texture, draw->position, draw->actor->size, draw->tint);
            }
        }

        /* Projectile sphere. */
        if (proj_active)
        {
            sdl3d_set_emissive(ctx, 5.0f, 3.0f, 1.0f);
            sdl3d_draw_sphere(ctx, sdl3d_vec3_make(proj_x, proj_y, proj_z), 0.1f, 8, 8,
                              (sdl3d_color){255, 200, 100, 255});
            sdl3d_set_emissive(ctx, 0, 0, 0);
        }

        /* Crosshair. */
        {
            float chx = px + fx * 0.4f;
            float chy = py + SDL_sinf(pitch) * 0.4f;
            float chz = pz + fz * 0.4f;
            sdl3d_set_emissive(ctx, 8, 8, 8);
            sdl3d_draw_cube(ctx, sdl3d_vec3_make(chx, chy, chz), sdl3d_vec3_make(0.003f, 0.003f, 0.003f),
                            (sdl3d_color){255, 255, 255, 255});
            sdl3d_set_emissive(ctx, 0, 0, 0);
        }

        sdl3d_end_mode_3d(ctx);

        /* Draw debug text overlay. */
        if (has_font)
        {
            char fps_buf[64];
            SDL_snprintf(fps_buf, sizeof(fps_buf), "FPS: %.0f", dt > 0 ? 1.0f / dt : 0.0f);
            sdl3d_draw_text(ctx, &debug_font, fps_buf, 10, 10, (sdl3d_color){255, 255, 255, 255});
        }

        sdl3d_present_render_context(ctx);

        /* Debug stats to log. */
        if (show_debug)
        {
            SDL_Log("[VIS] sector=%d  visible=%d/%d sectors  meshes=%d/%d  portals=%d  culling=%s", current_sector,
                    vis.visible_count, sector_count, visible_meshes, active_level->model.mesh_count,
                    active_level->portal_count, portal_culling ? "ON" : "OFF");
        }
    }

    sdl3d_free_level(&level_lightmapped);
    sdl3d_free_level(&level_vertex_baked);
    sdl3d_free_level(&level_unlit);
    sdl3d_free_texture(&enemy_tex);
    sdl3d_free_texture(&health_tex);
    sdl3d_free_texture(&sky_px);
    sdl3d_free_texture(&sky_nx);
    sdl3d_free_texture(&sky_py);
    sdl3d_free_texture(&sky_ny);
    sdl3d_free_texture(&sky_pz);
    sdl3d_free_texture(&sky_nz);
    if (has_font)
        sdl3d_free_font(&debug_font);
    sdl3d_destroy_render_context(ctx);
    if (ren)
        SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

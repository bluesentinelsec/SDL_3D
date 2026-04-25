/*
 * Doom-style level demo — sector-based level builder.
 * Expanded E1M1-scale-ish layout with multiple wings and color accents.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/animation.h"
#include "sdl3d/font.h"
#include "sdl3d/level.h"
#include "sdl3d/lighting.h"
#include "sdl3d/model.h"
#include "sdl3d/scene.h"
#include "sdl3d/sdl3d.h"
#include "sdl3d/ui.h"

#define WINDOW_W 1280
#define WINDOW_H 720
#define SOFTWARE_W (WINDOW_W / 2)
#define SOFTWARE_H (WINDOW_H / 2)
#define MOVE_SPEED 12.0f
#define MOUSE_SENS 0.002f
#define JUMP_VELOCITY 6.0f
#define GRAVITY 14.0f
#define PLAYER_HEIGHT 1.6f
#define PLAYER_RADIUS 0.35f
#define PLAYER_STEP_HEIGHT 1.1f
#define PLAYER_CEILING_CLEARANCE 0.1f
#define PLAYER_SPAWN_X 5.0f
#define PLAYER_SPAWN_Z 4.0f
#define PLAYER_SPAWN_YAW 3.14159f
#define PROJ_SPEED 20.0f
#define PROJ_LIFETIME 3.0f
#define ROCKET_LIGHT_R 1.0f
#define ROCKET_LIGHT_G 0.6f
#define ROCKET_LIGHT_B 0.2f
#define ROCKET_LIGHT_INTENSITY 4.0f
#define ROCKET_LIGHT_RANGE 4.0f
#define DEMO_SPRITE_ROTATION_COUNT 8

static const float DEMO_PI = 3.14159265358979323846f;

typedef enum demo_sprite_rotation
{
    DEMO_SPRITE_SOUTH = 0,
    DEMO_SPRITE_SOUTH_EAST = 1,
    DEMO_SPRITE_EAST = 2,
    DEMO_SPRITE_NORTH_EAST = 3,
    DEMO_SPRITE_NORTH = 4,
    DEMO_SPRITE_NORTH_WEST = 5,
    DEMO_SPRITE_WEST = 6,
    DEMO_SPRITE_SOUTH_WEST = 7
} demo_sprite_rotation;

typedef struct demo_sprite_rotation_set
{
    const sdl3d_texture2d *frames[DEMO_SPRITE_ROTATION_COUNT];
} demo_sprite_rotation_set;

typedef struct demo_sprite_actor
{
    const sdl3d_texture2d *texture;
    const demo_sprite_rotation_set *rotations;
    sdl3d_vec3 position;
    sdl3d_vec2 size;
    bool bob;
    float bob_amplitude;
    float bob_speed;
} demo_sprite_actor;

typedef struct demo_actor_draw
{
    const demo_sprite_actor *actor;
    const sdl3d_texture2d *texture;
    sdl3d_vec3 position;
    sdl3d_color tint;
    float distance_sq;
} demo_actor_draw;

static void configure_sprite_texture(sdl3d_texture2d *texture)
{
    sdl3d_set_texture_filter(texture, SDL3D_TEXTURE_FILTER_NEAREST);
    sdl3d_set_texture_wrap(texture, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
}

static void free_texture_array(sdl3d_texture2d *textures, int count)
{
    for (int i = 0; i < count; ++i)
    {
        sdl3d_free_texture(&textures[i]);
    }
}

static const sdl3d_texture2d *select_actor_texture(const demo_sprite_actor *actor, sdl3d_vec3 actor_pos, float cam_x,
                                                   float cam_z)
{
    if (actor->rotations == NULL)
    {
        return actor->texture;
    }

    {
        /* Frame names follow the "view from that direction" convention:
         *   south.png  — what the camera sees when standing south of the actor (actor's front)
         *   north.png  — camera north of the actor (actor's back)
         *   east/west  — side profiles
         *
         * atan2(dx, dz) measures the camera's bearing from the actor,
         * CW from +Z (north). Enum order is S, SE, E, NE, N, NW, W, SW
         * — CCW from south. The (π - angle) shift aligns camera-south
         * with index 0 so a viewer circling the actor sees the front,
         * sides, and back in turn. */
        const float dx = cam_x - actor_pos.x;
        const float dz = cam_z - actor_pos.z;
        const float angle = SDL_atan2f(dx, dz);
        const float octant = (DEMO_PI - angle) / (DEMO_PI * 0.25f);
        int index = (int)SDL_floorf(octant + 0.5f) % DEMO_SPRITE_ROTATION_COUNT;
        if (index < 0)
        {
            index += DEMO_SPRITE_ROTATION_COUNT;
        }
        const sdl3d_texture2d *texture = actor->rotations->frames[index];
        return texture != NULL ? texture : actor->texture;
    }
}

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

static int find_sector_at(const sdl3d_sector *sectors, int sector_count, float x, float z, float feet_y)
{
    int best = -1;
    float best_floor = -1000000.0f;

    for (int i = 0; i < sector_count; ++i)
    {
        const sdl3d_sector *sector = &sectors[i];
        if (!point_in_sector_xz(sector, x, z))
            continue;
        if (sector->floor_y > feet_y || feet_y >= sector->ceil_y)
            continue;
        if (sector->floor_y > best_floor)
        {
            best = i;
            best_floor = sector->floor_y;
        }
    }

    return best;
}

static int find_walkable_sector_at(const sdl3d_sector *sectors, int sector_count, float x, float z, float feet_y,
                                   float max_step_up)
{
    int best = -1;
    float best_floor = -1000000.0f;

    for (int i = 0; i < sector_count; ++i)
    {
        const sdl3d_sector *sector = &sectors[i];
        if (!point_in_sector_xz(sector, x, z))
            continue;
        if (sector->floor_y > feet_y + max_step_up)
            continue;
        if (sector->ceil_y - sector->floor_y < PLAYER_HEIGHT + PLAYER_CEILING_CLEARANCE)
            continue;
        if (sector->floor_y > best_floor)
        {
            best = i;
            best_floor = sector->floor_y;
        }
    }

    return best;
}

static int find_support_sector_at(const sdl3d_sector *sectors, int sector_count, float x, float z, float feet_y)
{
    int best = -1;
    float best_floor = -1000000.0f;

    for (int i = 0; i < sector_count; ++i)
    {
        const sdl3d_sector *sector = &sectors[i];
        if (!point_in_sector_xz(sector, x, z))
            continue;
        if (sector->floor_y > feet_y)
            continue;
        if (sector->ceil_y - sector->floor_y < PLAYER_HEIGHT + PLAYER_CEILING_CLEARANCE)
            continue;
        if (sector->floor_y > best_floor)
        {
            best = i;
            best_floor = sector->floor_y;
        }
    }

    return best;
}

static bool position_is_walkable(const sdl3d_sector *sectors, int sector_count, float x, float z, float feet_y,
                                 float max_step_up, int *out_sector)
{
    static const float sample_dirs[8][2] = {
        {1.0f, 0.0f},       {-1.0f, 0.0f},       {0.0f, 1.0f},        {0.0f, -1.0f},
        {0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, 0.7071f}, {-0.7071f, -0.7071f},
    };
    int center_sector = find_walkable_sector_at(sectors, sector_count, x, z, feet_y, max_step_up);
    float target_floor;

    if (center_sector < 0)
        return false;

    target_floor = sectors[center_sector].floor_y;
    for (int i = 0; i < 8; ++i)
    {
        float sx = x + sample_dirs[i][0] * PLAYER_RADIUS;
        float sz = z + sample_dirs[i][1] * PLAYER_RADIUS;
        int sample_sector = find_walkable_sector_at(sectors, sector_count, sx, sz, feet_y, max_step_up);
        if (sample_sector < 0)
            return false;
        if (SDL_fabsf(sectors[sample_sector].floor_y - target_floor) > max_step_up)
            return false;
    }

    if (out_sector != NULL)
        *out_sector = center_sector;
    return true;
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
    /* Ambient floor: the sprite art is already dark (black skeletal robot
     * on transparent background), so tinting further with ~12% ambient
     * kills readability anywhere without a nearby light. Lifting the
     * floor to ~70% keeps the art recognizable while still letting
     * colored point lights push it toward their hue up close. */
    float r = 0.7f;
    float g = 0.7f;
    float b = 0.72f;

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

static bool create_backend(SDL_Window **out_win, sdl3d_render_context **out_ctx, sdl3d_backend backend)
{
    sdl3d_window_config wcfg;
    sdl3d_init_window_config(&wcfg);
    wcfg.width = (backend == SDL3D_BACKEND_SOFTWARE) ? SOFTWARE_W : WINDOW_W;
    wcfg.height = (backend == SDL3D_BACKEND_SOFTWARE) ? SOFTWARE_H : WINDOW_H;
    wcfg.title = "SDL3D \xe2\x80\x94 Doom Level";
    wcfg.backend = backend;
    wcfg.allow_backend_fallback = false;

    if (!sdl3d_create_window(&wcfg, out_win, out_ctx))
        return false;

    SDL_SetWindowRelativeMouseMode(*out_win, true);
    return true;
}

static void reset_demo_state(float *px, float *py, float *pz, float *yaw, float *pitch, float *vy, bool *on_ground,
                             bool *proj_active, float *proj_life)
{
    *px = PLAYER_SPAWN_X;
    *py = PLAYER_HEIGHT;
    *pz = PLAYER_SPAWN_Z;
    *yaw = PLAYER_SPAWN_YAW;
    *pitch = 0.0f;
    *vy = 0.0f;
    *on_ground = true;
    *proj_active = false;
    *proj_life = 0.0f;
}

int main(int argc, char *argv[])
{
    SDL_Window *win = NULL;
    sdl3d_render_context *ctx = NULL;
    sdl3d_texture2d enemy_rot_tex[DEMO_SPRITE_ROTATION_COUNT] = {0};
    sdl3d_texture2d health_tex = {0};
    sdl3d_texture2d crate_tex = {0};
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
    if (!create_backend(&win, &ctx, current_backend))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Backend init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "doom_level MOVE_SPEED=%.2f backend=%d render=%dx%d", MOVE_SPEED,
                (int)current_backend, sdl3d_get_render_context_width(ctx), sdl3d_get_render_context_height(ctx));

    sdl3d_set_bloom_enabled(ctx, true);
    sdl3d_set_ssao_enabled(ctx, false);
    sdl3d_set_point_shadows_enabled(ctx, false);
    sdl3d_set_backface_culling_enabled(ctx, true);
    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG);

    /* Load debug font. */
    sdl3d_font debug_font;
    bool has_font = sdl3d_load_font(SDL3D_MEDIA_DIR "/fonts/Roboto.ttf", 40.0f, &debug_font);
    if (!has_font)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Font load FAILED: %s", SDL_GetError());
    }
    else
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Font loaded: size=%.0f atlas=%dx%d ascent=%.1f descent=%.1f",
                     debug_font.size, debug_font.atlas_w, debug_font.atlas_h, debug_font.ascent, debug_font.descent);
    }

    /* UI context — demoing the new immediate-mode UI path with a label
     * widget overlaid on the 3D scene. */
    sdl3d_ui_context *ui = NULL;
    sdl3d_ui_create(has_font ? &debug_font : NULL, &ui);

    const char *enemy_rotation_paths[DEMO_SPRITE_ROTATION_COUNT] = {
        SDL3D_MEDIA_DIR "/sprites/skeletal_robot/south.png", SDL3D_MEDIA_DIR "/sprites/skeletal_robot/south-east.png",
        SDL3D_MEDIA_DIR "/sprites/skeletal_robot/east.png",  SDL3D_MEDIA_DIR "/sprites/skeletal_robot/north-east.png",
        SDL3D_MEDIA_DIR "/sprites/skeletal_robot/north.png", SDL3D_MEDIA_DIR "/sprites/skeletal_robot/north-west.png",
        SDL3D_MEDIA_DIR "/sprites/skeletal_robot/west.png",  SDL3D_MEDIA_DIR "/sprites/skeletal_robot/south-west.png",
    };
    bool enemy_sprites_ok = true;

    for (int i = 0; i < DEMO_SPRITE_ROTATION_COUNT; ++i)
    {
        if (!sdl3d_load_texture_from_file(enemy_rotation_paths[i], &enemy_rot_tex[i]))
        {
            enemy_sprites_ok = false;
            break;
        }
        configure_sprite_texture(&enemy_rot_tex[i]);
    }

    if (!enemy_sprites_ok || !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/sprites/health-pack.png", &health_tex))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Sprite load failed: %s", SDL_GetError());
        free_texture_array(enemy_rot_tex, DEMO_SPRITE_ROTATION_COUNT);
        sdl3d_free_texture(&health_tex);
        sdl3d_destroy_render_context(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    configure_sprite_texture(&health_tex);

    if (!sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/textures/radioactive-crate.png", &crate_tex))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Crate texture load failed: %s", SDL_GetError());

    if (!sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/px.png", &sky_px) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/nx.png", &sky_nx) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/py.png", &sky_py) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/ny.png", &sky_ny) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/pz.png", &sky_pz) ||
        !sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/skyboxes/sky_17/nz.png", &sky_nz))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Skybox load failed: %s", SDL_GetError());
        free_texture_array(enemy_rot_tex, DEMO_SPRITE_ROTATION_COUNT);
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
        /* 10: Storage annex (narrowed to make room for stairwell) */
        {{{28, 12}, {34, 12}, {34, 24}, {28, 24}}, 4, 0.0f, 4.0f, 0, 1, 2},
        /* 11: Reactor hall beyond exit */
        {{{18, 32}, {30, 32}, {30, 44}, {18, 44}}, 4, 0.0f, 5.5f, 5, 1, 4},
        /* 12: Secret annex behind storage */
        {{{32, 24}, {40, 24}, {40, 30}, {32, 30}}, 4, 0.0f, 3.0f, 0, 1, 2},
        /* 13: Exterior yard (large open-air space for skybox side visibility) */
        {{{-6, 44}, {54, 44}, {54, 104}, {-6, 104}}, 4, 0.0f, 12.0f, 5, -1, 2},

        /* ---- Stairwell (sectors 14-20): wide stairs east of storage ---- */
        /* Stairs run north (z=24) to south (z=12), rising as you go south.
         * Upper room sits above storage with inset walls to avoid false portals. */
        /* 14: Stair entry (ground floor, connects to storage at x=34, z=20-24) */
        {{{34, 20}, {40, 20}, {40, 24}, {34, 24}}, 4, 0.0f, 10.0f, 0, -1, 4},
        /* 15: Stair step 1 */
        {{{34, 18}, {40, 18}, {40, 20}, {34, 20}}, 4, 1.0f, 10.0f, 0, -1, 4},
        /* 16: Stair step 2 */
        {{{34, 16}, {40, 16}, {40, 18}, {34, 18}}, 4, 2.0f, 10.0f, 0, -1, 4},
        /* 17: Stair step 3 */
        {{{34, 14}, {40, 14}, {40, 16}, {34, 16}}, 4, 3.0f, 10.0f, 0, -1, 4},
        /* 18: Stair step 4 */
        {{{34, 12}, {40, 12}, {40, 14}, {34, 14}}, 4, 4.0f, 10.0f, 0, -1, 4},
        /* 19: Stair top landing */
        {{{34, 10}, {40, 10}, {40, 12}, {34, 12}}, 4, 5.0f, 10.0f, 0, -1, 4},
        /* 20: Upper room (3rd floor, connects to top landing at z=12, x=34-40) */
        {{{29, 4}, {40, 4}, {40, 10}, {29, 10}}, 4, 6.0f, 10.0f, 3, 1, 5},
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
        {{37, 2.5f, 22}, {1.0f, 0.8f, 0.5f}, 1.5f, 8.0f},     /* Stair entry — warm */
        {{37, 5.0f, 15}, {0.8f, 0.6f, 1.0f}, 1.8f, 10.0f},    /* Mid-stairs — purple */
        {{34, 8.5f, 7}, {1.0f, 0.95f, 0.8f}, 3.0f, 16.0f},    /* Upper room — bright warm */
    };
    const int sector_count = (int)SDL_arraysize(sectors);
    const int light_count = (int)SDL_arraysize(lights);

    /* Build three versions: lightmapped, vertex-baked fallback, and unlit. */
    sdl3d_level level_lightmapped, level_vertex_baked, level_unlit;
    if (!sdl3d_build_level(sectors, sector_count, mats, 6, lights, light_count, &level_lightmapped))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Level build failed: %s", SDL_GetError());
        free_texture_array(enemy_rot_tex, DEMO_SPRITE_ROTATION_COUNT);
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Level build failed: %s", SDL_GetError());
        sdl3d_free_level(&level_lightmapped);
        free_texture_array(enemy_rot_tex, DEMO_SPRITE_ROTATION_COUNT);
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Level build failed: %s", SDL_GetError());
        sdl3d_free_level(&level_lightmapped);
        sdl3d_free_level(&level_vertex_baked);
        free_texture_array(enemy_rot_tex, DEMO_SPRITE_ROTATION_COUNT);
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
    demo_sprite_rotation_set enemy_rotations = {{
        &enemy_rot_tex[DEMO_SPRITE_SOUTH],
        &enemy_rot_tex[DEMO_SPRITE_SOUTH_EAST],
        &enemy_rot_tex[DEMO_SPRITE_EAST],
        &enemy_rot_tex[DEMO_SPRITE_NORTH_EAST],
        &enemy_rot_tex[DEMO_SPRITE_NORTH],
        &enemy_rot_tex[DEMO_SPRITE_NORTH_WEST],
        &enemy_rot_tex[DEMO_SPRITE_WEST],
        &enemy_rot_tex[DEMO_SPRITE_SOUTH_WEST],
    }};

    /* Enemy sprites are anchored at bottom-center (feet = position.y),
     * but the AI-generated art has a chunk of transparent padding under
     * the robot's feet, so rendering at sector floor_y leaves them
     * visually floating. Dropping each enemy's y by ~1.05 units plants
     * the boots on the floor and keeps 2×-scale figures under the low
     * 3–4m ceilings in the start/south-corridor sectors without
     * clipping. */
    demo_sprite_actor actors[] = {
        {enemy_rotations.frames[DEMO_SPRITE_SOUTH], &enemy_rotations, sdl3d_vec3_make(5.8f, -1.05f, 6.8f),
         (sdl3d_vec2){3.4f, 5.2f}, true, 0.10f, 7.0f},
        {&health_tex, NULL, sdl3d_vec3_make(5.0f, 0.25f, 10.8f), (sdl3d_vec2){1.0f, 1.0f}, true, 0.12f, 1.8f},
        {enemy_rotations.frames[DEMO_SPRITE_SOUTH], &enemy_rotations, sdl3d_vec3_make(4.2f, -1.4f, 21.5f),
         (sdl3d_vec2){3.2f, 4.8f}, true, 0.08f, 6.0f},
        {&health_tex, NULL, sdl3d_vec3_make(24.0f, 0.25f, 4.5f), (sdl3d_vec2){1.0f, 1.0f}, true, 0.15f, 1.5f},
        {enemy_rotations.frames[DEMO_SPRITE_SOUTH], &enemy_rotations, sdl3d_vec3_make(24.0f, -1.05f, 19.0f),
         (sdl3d_vec2){3.6f, 5.6f}, true, 0.09f, 6.5f},
        {&health_tex, NULL, sdl3d_vec3_make(24.0f, 0.25f, 28.5f), (sdl3d_vec2){1.0f, 1.0f}, true, 0.12f, 2.1f},
        {enemy_rotations.frames[DEMO_SPRITE_SOUTH], &enemy_rotations, sdl3d_vec3_make(24.0f, -1.05f, 37.5f),
         (sdl3d_vec2){3.4f, 5.2f}, true, 0.09f, 5.8f},
        {&health_tex, NULL, sdl3d_vec3_make(35.5f, 0.25f, 27.0f), (sdl3d_vec2){1.0f, 1.0f}, true, 0.1f, 1.7f},
        {enemy_rotations.frames[DEMO_SPRITE_SOUTH], &enemy_rotations, sdl3d_vec3_make(24.0f, -1.05f, 72.0f),
         (sdl3d_vec2){3.6f, 5.6f}, true, 0.11f, 5.5f},
        {&health_tex, NULL, sdl3d_vec3_make(10.0f, 0.25f, 84.0f), (sdl3d_vec2){1.0f, 1.0f}, true, 0.14f, 1.6f},
    };
    demo_actor_draw actor_draws[SDL_arraysize(actors)];
    float elapsed = 0.0f;
    sdl3d_skybox_textured skybox = {&sky_px, &sky_nx, &sky_py, &sky_ny, &sky_pz, &sky_nz, 350.0f};

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Portals detected: %d", level_lightmapped.portal_count);
    for (int i = 0; i < level_lightmapped.portal_count; i++)
    {
        const sdl3d_level_portal *p = &level_lightmapped.portals[i];
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "  portal %d: sector %d <-> %d  x[%.1f,%.1f] z[%.1f,%.1f] y[%.2f,%.2f]", i, p->sector_a,
                     p->sector_b, p->min_x, p->max_x, p->min_z, p->max_z, p->floor_y, p->ceil_y);
    }

    /* Visibility state. */
    bool sector_visible[SDL_arraysize(sectors)];
    sdl3d_visibility_result vis;
    vis.sector_visible = sector_visible;
    vis.visible_count = 0;
    bool show_debug = false;
    bool portal_culling = true;

    /* Player */
    float px = PLAYER_SPAWN_X, py = PLAYER_HEIGHT, pz = PLAYER_SPAWN_Z;
    float yaw = PLAYER_SPAWN_YAW, pitch = 0;
    float vy = 0;          /* vertical velocity for jump */
    float view_smooth = 0; /* Quake-style view smoothing for stairs */
    bool on_ground = true;
    bool mouse_init = false;

    /* Projectile. */
    bool proj_active = false;
    float proj_x = 0, proj_y = 0, proj_z = 0;
    float proj_dx = 0, proj_dy = 0, proj_dz = 0;
    float proj_life = 0;

    /* Load 3D models and create scene actors. */
    sdl3d_model robot_model = {0};
    bool has_robot = sdl3d_load_model_from_file(SDL3D_MEDIA_DIR "/simple_robot/simple_robot.glb", &robot_model);
    if (!has_robot)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Robot model load failed: %s", SDL_GetError());

    sdl3d_model dragon_model = {0};
    bool has_dragon = sdl3d_load_model_from_file(SDL3D_MEDIA_DIR "/black_dragon/scene.gltf", &dragon_model);
    if (!has_dragon)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Dragon model load failed: %s", SDL_GetError());
    else
    {
        /* Boost dark material albedo so the dragon is visible under level lighting. */
        for (int i = 0; i < dragon_model.material_count; ++i)
        {
            for (int c = 0; c < 3; ++c)
                dragon_model.materials[i].albedo[c] = SDL_min(dragon_model.materials[i].albedo[c] * 3.0f, 1.0f);
            dragon_model.materials[i].albedo[3] = 1.0f;
        }
    }

    sdl3d_scene *scene = sdl3d_create_scene();

    /* Place robot actors in the level. */
    if (has_robot && scene)
    {
        sdl3d_actor *r1 = sdl3d_scene_add_actor(scene, &robot_model);
        sdl3d_actor_set_position(r1, sdl3d_vec3_make(5.0f, 0.0f, 12.0f));
        sdl3d_actor_set_scale(r1, sdl3d_vec3_make(0.8f, 0.8f, 0.8f));

        sdl3d_actor *r2 = sdl3d_scene_add_actor(scene, &robot_model);
        sdl3d_actor_set_position(r2, sdl3d_vec3_make(24.0f, 0.0f, 20.0f));
        sdl3d_actor_set_scale(r2, sdl3d_vec3_make(0.8f, 0.8f, 0.8f));
        sdl3d_actor_set_tint(r2, (sdl3d_color){255, 180, 180, 255});
    }

    /* Place animated dragon in the exterior yard. */
    if (has_dragon && scene)
    {
        sdl3d_actor *dragon = sdl3d_scene_add_actor(scene, &dragon_model);
        sdl3d_actor_set_position(dragon, sdl3d_vec3_make(24.0f, 0.0f, 74.0f));
        sdl3d_actor_set_scale(dragon, sdl3d_vec3_make(2.0f, 2.0f, 2.0f));
        if (dragon_model.animation_count > 0)
            sdl3d_actor_play_animation(dragon, 0, true);
    }

    bool running = true;
    Uint64 last = SDL_GetPerformanceCounter();

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            sdl3d_ui_process_event(ui, &ev);
            if (ev.type == SDL_EVENT_QUIT)
                running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_ESCAPE)
                running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_L)
            {
                use_lit_world = !use_lit_world;
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "World lighting: %s",
                             use_lit_world ? (use_lightmaps ? "LIGHTMAP" : "VERTEX-BAKED") : "UNLIT");
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_M)
            {
                use_lightmaps = !use_lightmaps;
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Lightmap mode: %s",
                             use_lightmaps ? "LIGHTMAP" : "VERTEX-BAKED");
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_F1)
            {
                show_debug = !show_debug;
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Debug stats: %s", show_debug ? "ON" : "OFF");
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_F2)
            {
                portal_culling = !portal_culling;
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Portal culling: %s", portal_culling ? "ON" : "OFF");
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_SPACE && on_ground)
            {
                vy = JUMP_VELOCITY;
                on_ground = false;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN &&
                (ev.key.scancode == SDL_SCANCODE_BACKSPACE || ev.key.scancode == SDL_SCANCODE_DELETE))
            {
                reset_demo_state(&px, &py, &pz, &yaw, &pitch, &vy, &on_ground, &proj_active, &proj_life);
                view_smooth = 0;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_TAB)
            {
                sdl3d_backend next =
                    (current_backend == SDL3D_BACKEND_SDLGPU) ? SDL3D_BACKEND_SOFTWARE : SDL3D_BACKEND_SDLGPU;
                sdl3d_destroy_render_context(ctx);
                SDL_DestroyWindow(win);
                ctx = NULL;
                win = NULL;
                if (create_backend(&win, &ctx, next))
                {
                    current_backend = next;
                    sdl3d_set_bloom_enabled(ctx, sdl3d_is_feature_available(ctx, SDL3D_FEATURE_BLOOM));
                    sdl3d_set_ssao_enabled(ctx, false);
                    sdl3d_set_point_shadows_enabled(ctx, false);
                    sdl3d_set_backface_culling_enabled(ctx, true);
                    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG);
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Switched to %s backend render=%dx%d",
                                sdl3d_get_backend_name(current_backend), sdl3d_get_render_context_width(ctx),
                                sdl3d_get_render_context_height(ctx));
                }
                else
                {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Backend switch failed: %s — reverting", SDL_GetError());
                    create_backend(&win, &ctx, current_backend);
                    sdl3d_set_bloom_enabled(ctx, sdl3d_is_feature_available(ctx, SDL3D_FEATURE_BLOOM));
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

        /* Advance all actor animations. */
        if (scene)
        {
            int ac = sdl3d_scene_get_actor_count(scene);
            for (int i = 0; i < ac; i++)
            {
                sdl3d_actor *a = sdl3d_scene_get_actor_at(scene, i);
                if (a)
                    sdl3d_actor_advance_animation(a, dt);
            }
        }

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

        /* Quake-style view smoothing: decay the offset each frame. */
        {
            float smooth_speed = 12.0f; /* higher = snappier */
            if (view_smooth > 0.01f)
                view_smooth -= view_smooth * smooth_speed * dt;
            else if (view_smooth < -0.01f)
                view_smooth -= view_smooth * smooth_speed * dt;
            else
                view_smooth = 0.0f;
        }

        float py_before_collision = py;

        /* Normalize wish direction, then apply Doom-style sliding collision
         * against sector boundaries. */
        {
            float feet_y = py - PLAYER_HEIGHT;
            int current_sector = find_sector_at(sectors, sector_count, px, pz, feet_y);
            float current_floor = feet_y;

            if (current_sector < 0)
            {
                current_sector = find_walkable_sector_at(sectors, sector_count, px, pz, feet_y, PLAYER_STEP_HEIGHT);
            }
            if (current_sector >= 0)
            {
                current_floor = sectors[current_sector].floor_y;
            }

            {
                float wish_len = SDL_sqrtf(wish_x * wish_x + wish_z * wish_z);
                if (wish_len > 0.001f)
                {
                    float move_x;
                    float move_z;
                    int candidate_sector = -1;

                    wish_x /= wish_len;
                    wish_z /= wish_len;
                    move_x = wish_x * MOVE_SPEED * dt;
                    move_z = wish_z * MOVE_SPEED * dt;

                    if (position_is_walkable(sectors, sector_count, px + move_x, pz + move_z, feet_y,
                                             PLAYER_STEP_HEIGHT, &candidate_sector))
                    {
                        px += move_x;
                        pz += move_z;
                        current_sector = candidate_sector;
                    }
                    else
                    {
                        if (position_is_walkable(sectors, sector_count, px + move_x, pz, feet_y, PLAYER_STEP_HEIGHT,
                                                 &candidate_sector))
                        {
                            px += move_x;
                            current_sector = candidate_sector;
                        }
                        else
                        {
                            if (position_is_walkable(sectors, sector_count, px, pz + move_z, feet_y, PLAYER_STEP_HEIGHT,
                                                     &candidate_sector))
                            {
                                pz += move_z;
                                current_sector = candidate_sector;
                            }
                        }
                    }
                }
            }

            feet_y = py - PLAYER_HEIGHT;
            current_sector = find_sector_at(sectors, sector_count, px, pz, feet_y);
            if (current_sector < 0)
            {
                current_sector = find_walkable_sector_at(sectors, sector_count, px, pz, feet_y, PLAYER_STEP_HEIGHT);
            }

            if (on_ground)
            {
                if (current_sector >= 0)
                {
                    const float target_floor = sectors[current_sector].floor_y;
                    if (target_floor < current_floor - PLAYER_STEP_HEIGHT)
                    {
                        on_ground = false;
                    }
                    else
                    {
                        py = target_floor + PLAYER_HEIGHT;
                    }
                }
                else
                {
                    on_ground = false;
                }
            }
        }

        /* Jump / gravity. */
        if (!on_ground)
        {
            float prev_head_y = py;
            float prev_feet_y = py - PLAYER_HEIGHT;
            float feet_y;
            int containing_sector;
            int support_sector;

            vy -= GRAVITY * dt;
            py += vy * dt;
            feet_y = py - PLAYER_HEIGHT;
            containing_sector = find_sector_at(sectors, sector_count, px, pz, SDL_max(prev_feet_y, feet_y));
            support_sector = find_support_sector_at(sectors, sector_count, px, pz,
                                                    SDL_max(prev_feet_y, feet_y) + PLAYER_STEP_HEIGHT);

            if (containing_sector >= 0)
            {
                float ceiling_y = sectors[containing_sector].ceil_y - PLAYER_CEILING_CLEARANCE;
                if (prev_head_y <= ceiling_y && py > ceiling_y)
                {
                    py = ceiling_y;
                    feet_y = py - PLAYER_HEIGHT;
                    if (vy > 0.0f)
                        vy = 0.0f;
                }
            }

            if (support_sector >= 0 && vy <= 0.0f)
            {
                float floor_y = sectors[support_sector].floor_y;
                if (prev_feet_y >= floor_y && feet_y <= floor_y)
                {
                    py = floor_y + PLAYER_HEIGHT;
                    vy = 0.0f;
                    on_ground = true;
                }
            }

            if (!on_ground && py < -20.0f)
            {
                reset_demo_state(&px, &py, &pz, &yaw, &pitch, &vy, &on_ground, &proj_active, &proj_life);
                view_smooth = 0;
            }
        }

        {
            float feet_y = py - PLAYER_HEIGHT;
            int containing_sector = find_sector_at(sectors, sector_count, px, pz, feet_y);
            if (containing_sector >= 0)
            {
                float ceiling_y = sectors[containing_sector].ceil_y - PLAYER_CEILING_CLEARANCE;
                if (py > ceiling_y)
                {
                    py = ceiling_y;
                    if (vy > 0.0f)
                        vy = 0.0f;
                }
            }
        }

        /* Accumulate view smooth offset from floor snaps (not jumps). */
        {
            float py_delta = py - py_before_collision;
            if (on_ground && SDL_fabsf(py_delta) > 0.01f)
                view_smooth -= py_delta;
        }

        sdl3d_camera3d cam;
        float eye_y = py + view_smooth;
        cam.position = sdl3d_vec3_make(px, eye_y, pz);
        cam.target = sdl3d_vec3_make(px + fx, eye_y + SDL_sinf(pitch), pz + fz);
        cam.up = sdl3d_vec3_make(0, 1, 0);
        cam.fovy = 75.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        /* Update projectile. */
        advance_projectile(sectors, sector_count, dt, &proj_active, &proj_x, &proj_y, &proj_z, proj_dx, proj_dy,
                           proj_dz, &proj_life);

        /* Compute portal visibility. */
        sdl3d_level *active_level =
            use_lit_world ? (use_lightmaps ? &level_lightmapped : &level_vertex_baked) : &level_unlit;
        int current_sector = find_sector_at(sectors, sector_count, px, pz, py - PLAYER_HEIGHT);
        if (current_sector < 0)
        {
            current_sector =
                find_walkable_sector_at(sectors, sector_count, px, pz, py - PLAYER_HEIGHT, PLAYER_STEP_HEIGHT);
        }
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

        /* Draw 3D scene actors (robot models). Refresh each actor's sector
         * from its current world XZ so sdl3d_draw_scene_with_visibility can
         * skip actors in unreachable rooms before any frustum work runs. */
        if (scene)
        {
            int actor_count = sdl3d_scene_get_actor_count(scene);
            for (int i = 0; i < actor_count; ++i)
            {
                sdl3d_actor *a = sdl3d_scene_get_actor_at(scene, i);
                if (!a)
                    continue;
                sdl3d_vec3 ap = sdl3d_actor_get_position(a);
                int sid = sdl3d_level_find_sector(active_level, sectors, ap.x, ap.z);
                sdl3d_actor_set_sector(a, sid);
            }
            sdl3d_draw_scene_with_visibility(ctx, scene, portal_culling ? &vis : NULL);
        }

        /* Crate props — simple textured cubes placed around the level. */
        {
            static const sdl3d_vec3 crate_positions[] = {
                {3.0f, 0.5f, 5.0f},   {7.0f, 0.5f, 5.0f},   {3.0f, 0.5f, 10.0f},
                {7.0f, 0.5f, 10.0f},  {14.0f, 0.5f, 20.0f}, {14.0f, 1.5f, 20.0f}, /* stacked */
                {22.0f, 0.5f, 16.0f},
            };
            sdl3d_color crate_tint = {255, 255, 255, 255};
            const sdl3d_texture2d *tex = crate_tex.pixels ? &crate_tex : NULL;
            for (int i = 0; i < (int)SDL_arraysize(crate_positions); i++)
            {
                sdl3d_draw_cube_textured(ctx, crate_positions[i], sdl3d_vec3_make(1.0f, 1.0f, 1.0f),
                                         sdl3d_vec3_make(0, 1, 0), 0.0f, tex, crate_tint);
            }
        }

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
                actor_draws[actor_draw_count].texture = select_actor_texture(&actors[i], actor_pos, px, pz);
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
                sdl3d_draw_billboard(ctx, draw->texture, draw->position, draw->actor->size, draw->tint);
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
            float chy = eye_y + SDL_sinf(pitch) * 0.4f;
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
            sdl3d_draw_fps(ctx, &debug_font, dt);
        }

        /* UI pass: demo of the new immediate-mode widget system. Labels
         * are composed each frame and rendered on top of the 3D scene. */
        if (ui && has_font)
        {
            sdl3d_ui_begin_frame(ui, sdl3d_get_render_context_width(ctx), sdl3d_get_render_context_height(ctx));
            sdl3d_ui_label(ui, 10.0f, 60.0f, "SDL3D UI - Phase 1");
            sdl3d_ui_labelf(ui, 10.0f, 100.0f, "sector=%d  visible=%d/%d", current_sector, vis.visible_count,
                            sector_count);
            sdl3d_ui_labelf(ui, 10.0f, 140.0f, "pos %.1f, %.1f, %.1f", px, py, pz);
            sdl3d_ui_end_frame(ui);
            sdl3d_ui_render(ui, ctx);
        }

        sdl3d_present_render_context(ctx);

        /* Debug stats to log. */
        if (show_debug)
        {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                         "[VIS] sector=%d  visible=%d/%d sectors  meshes=%d/%d  portals=%d  culling=%s", current_sector,
                         vis.visible_count, sector_count, visible_meshes, active_level->model.mesh_count,
                         active_level->portal_count, portal_culling ? "ON" : "OFF");
        }
    }

    sdl3d_free_level(&level_lightmapped);
    sdl3d_free_level(&level_vertex_baked);
    sdl3d_free_level(&level_unlit);
    free_texture_array(enemy_rot_tex, DEMO_SPRITE_ROTATION_COUNT);
    sdl3d_free_texture(&health_tex);
    sdl3d_free_texture(&crate_tex);
    sdl3d_free_texture(&sky_px);
    sdl3d_free_texture(&sky_nx);
    sdl3d_free_texture(&sky_py);
    sdl3d_free_texture(&sky_ny);
    sdl3d_free_texture(&sky_pz);
    sdl3d_free_texture(&sky_nz);
    if (has_font)
        sdl3d_free_font(&debug_font);
    sdl3d_ui_destroy(ui);
    sdl3d_destroy_scene(scene);
    if (has_robot)
        sdl3d_free_model(&robot_model);
    if (has_dragon)
        sdl3d_free_model(&dragon_model);
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

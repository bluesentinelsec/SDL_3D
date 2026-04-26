/* Per-frame rendering: visibility, level, entities, HUD. */
#include "renderer.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/drawing3d.h"
#include "sdl3d/fps_mover.h"
#include "sdl3d/lighting.h"
#include "sdl3d/sdl3d.h"

#define ROCKET_LIGHT_R 1.0f
#define ROCKET_LIGHT_G 0.6f
#define ROCKET_LIGHT_B 0.2f
#define ROCKET_LIGHT_INTENSITY 4.0f
#define ROCKET_LIGHT_RANGE 4.0f

static sdl3d_color sample_actor_tint(const sdl3d_level_light *lights, int light_count, sdl3d_vec3 position)
{
    float r = 0.7f, g = 0.7f, b = 0.72f;

    for (int i = 0; i < light_count; ++i)
    {
        float dx = lights[i].position[0] - position.x;
        float dy = lights[i].position[1] - position.y;
        float dz = lights[i].position[2] - position.z;
        float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist >= lights[i].range || dist <= 0.0001f)
            continue;
        float t = 1.0f - (dist / lights[i].range);
        float atten = t * t;
        float scale = lights[i].intensity * atten * 0.28f;
        r += lights[i].color[0] * scale;
        g += lights[i].color[1] * scale;
        b += lights[i].color[2] * scale;
    }

    if (r > 1.0f)
        r = 1.0f;
    if (g > 1.0f)
        g = 1.0f;
    if (b > 1.0f)
        b = 1.0f;
    return (sdl3d_color){(Uint8)(r * 255.0f), (Uint8)(g * 255.0f), (Uint8)(b * 255.0f), 255};
}

static sdl3d_vec3 camera_forward(const sdl3d_camera3d *camera)
{
    if (camera == NULL)
    {
        return sdl3d_vec3_make(0.0f, 0.0f, -1.0f);
    }

    sdl3d_vec3 forward = sdl3d_vec3_sub(camera->target, camera->position);
    if (sdl3d_vec3_length_squared(forward) <= 0.000001f)
    {
        return sdl3d_vec3_make(0.0f, 0.0f, -1.0f);
    }
    return sdl3d_vec3_normalize(forward);
}

static sdl3d_vec3 bounds_center(sdl3d_bounding_box bounds)
{
    return sdl3d_vec3_make((bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f,
                           (bounds.min.z + bounds.max.z) * 0.5f);
}

void render_state_init(render_state *rs)
{
    SDL_zerop(rs);
    rs->portal_culling = true;
}

void render_state_free(render_state *rs)
{
    if (rs == NULL)
    {
        return;
    }

    SDL_free(rs->sector_visible);
    SDL_zerop(rs);
}

static bool render_state_ensure_sector_capacity(render_state *rs, int sector_count)
{
    if (rs == NULL || sector_count <= 0)
    {
        return false;
    }

    if (rs->sector_visible_capacity < sector_count)
    {
        bool *sector_visible = SDL_realloc(rs->sector_visible, (size_t)sector_count * sizeof(*sector_visible));
        if (sector_visible == NULL)
        {
            return SDL_OutOfMemory();
        }
        rs->sector_visible = sector_visible;
        rs->sector_visible_capacity = sector_count;
    }

    rs->vis.sector_visible = rs->sector_visible;
    return true;
}

void render_draw_frame(render_state *rs, sdl3d_render_context *ctx, const sdl3d_font *font, sdl3d_ui_context *ui,
                       level_data *ld, entities *ent, const doom_hazard_particles *hazards,
                       const doom_surveillance_camera *surveillance, const player_state *player, int backbuffer_w,
                       int backbuffer_h, float dt, const char *render_profile_name, bool ambient_feedback_active,
                       bool teleport_feedback_active)
{
    const sdl3d_fps_mover *mover = &player->mover;
    sdl3d_level *active = level_data_active(ld);
    const sdl3d_camera3d player_cam = sdl3d_fps_mover_camera(mover, 75.0f);
    const sdl3d_camera3d *surveillance_cam = doom_surveillance_active_camera(surveillance);
    sdl3d_camera3d cam = surveillance_cam != NULL ? *surveillance_cam : player_cam;
    const int sector_count = active->sector_count;

    float px = mover->position.x, py = mover->position.y;
    float pz = mover->position.z;
    const sdl3d_vec3 forward = camera_forward(&cam);

    int current_sector = sdl3d_level_find_sector_at(&ld->unlit, g_sectors, px, pz, py - PLAYER_HEIGHT);
    if (current_sector < 0)
        current_sector = sdl3d_level_find_walkable_sector(&ld->unlit, g_sectors, px, pz, py - PLAYER_HEIGHT,
                                                          PLAYER_STEP_HEIGHT, PLAYER_MIN_HEADROOM);

    /* Dynamic lights. */
    sdl3d_clear_lights(ctx);
    if (player->proj_active)
    {
        sdl3d_light rocket = {0};
        rocket.type = SDL3D_LIGHT_POINT;
        rocket.position = sdl3d_vec3_make(player->proj_x, player->proj_y, player->proj_z);
        rocket.color[0] = ROCKET_LIGHT_R;
        rocket.color[1] = ROCKET_LIGHT_G;
        rocket.color[2] = ROCKET_LIGHT_B;
        rocket.intensity = ROCKET_LIGHT_INTENSITY;
        rocket.range = ROCKET_LIGHT_RANGE;
        sdl3d_add_light(ctx, &rocket);
    }

    sdl3d_clear_render_context(ctx, (sdl3d_color){10, 10, 15, 255});
    sdl3d_begin_mode_3d(ctx, cam);

    /* Skybox */
    sdl3d_skybox_textured skybox = {&ent->sky[0], &ent->sky[1], &ent->sky[2], &ent->sky[3],
                                    &ent->sky[4], &ent->sky[5], 350.0f};
    sdl3d_draw_skybox_textured(ctx, &skybox);

    /* Visibility */
    if (rs->portal_culling && !render_state_ensure_sector_capacity(rs, sector_count))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Portal visibility allocation failed: %s", SDL_GetError());
        rs->portal_culling = false;
    }

    if (rs->portal_culling)
    {
        sdl3d_level_compute_visibility_from_camera(active, g_sectors, &cam, backbuffer_w, backbuffer_h, 0.01f, 1000.0f,
                                                   &rs->vis);
    }
    else
    {
        for (int i = 0; i < sector_count && rs->sector_visible != NULL; i++)
            rs->sector_visible[i] = true;
        rs->vis.visible_count = sector_count;
    }

    /* Level geometry */
    sdl3d_draw_level(ctx, active, rs->portal_culling ? &rs->vis : NULL, (sdl3d_color){255, 255, 255, 255});

    /* 3D scene actors */
    if (ent->scene)
    {
        int ac = sdl3d_scene_get_actor_count(ent->scene);
        for (int i = 0; i < ac; ++i)
        {
            sdl3d_actor *a = sdl3d_scene_get_actor_at(ent->scene, i);
            if (!a)
                continue;
            sdl3d_vec3 ap = sdl3d_actor_get_position(a);
            sdl3d_actor_set_sector(a, sdl3d_level_find_sector(active, g_sectors, ap.x, ap.z));
        }
        sdl3d_draw_scene_with_visibility(ctx, ent->scene, rs->portal_culling ? &rs->vis : NULL);
    }

    /* Crate props */
    {
        static const sdl3d_vec3 crate_positions[] = {
            {3.0f, 0.5f, 5.0f},   {7.0f, 0.5f, 5.0f},   {3.0f, 0.5f, 10.0f},  {7.0f, 0.5f, 10.0f},
            {14.0f, 0.5f, 20.0f}, {14.0f, 1.5f, 20.0f}, {22.0f, 0.5f, 16.0f},
        };
        const sdl3d_texture2d *tex = ent->crate_tex.pixels ? &ent->crate_tex : NULL;
        for (int i = 0; i < (int)SDL_arraysize(crate_positions); i++)
            sdl3d_draw_cube_textured(ctx, crate_positions[i], sdl3d_vec3_make(1.0f, 1.0f, 1.0f),
                                     sdl3d_vec3_make(0, 1, 0), 0.0f, tex, (sdl3d_color){255, 255, 255, 255});
    }

    /* Ambient-zone feedback beacon. */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(73.0f, 3.0f, 63.0f), sdl3d_vec3_make(0.7f, 0.7f, 0.7f),
                    ambient_feedback_active ? (sdl3d_color){220, 40, 30, 255} : (sdl3d_color){40, 210, 70, 255});

    /* Dragon-room teleporter source pad and destination beacon. */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(24.0f, 0.05f, 88.0f), sdl3d_vec3_make(3.0f, 0.1f, 3.0f),
                    teleport_feedback_active ? (sdl3d_color){255, 210, 60, 255} : (sdl3d_color){40, 190, 230, 255});
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(72.0f, 2.85f, 63.0f), sdl3d_vec3_make(0.8f, 0.7f, 0.8f),
                    teleport_feedback_active ? (sdl3d_color){255, 210, 60, 255} : (sdl3d_color){80, 120, 255, 255});

    /* Conveyor direction marker in the dragon room. */
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(19.0f, 0.04f, 51.0f), sdl3d_vec3_make(21.0f, 0.08f, 12.0f),
                    (sdl3d_color){30, 35, 40, 255});
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(19.0f, 0.12f, 51.0f), sdl3d_vec3_make(13.0f, 0.08f, 0.8f),
                    (sdl3d_color){70, 210, 230, 255});
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(26.0f, 0.13f, 49.4f), sdl3d_vec3_make(3.0f, 0.1f, 0.8f),
                    (sdl3d_color){70, 210, 230, 255});
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(26.0f, 0.13f, 52.6f), sdl3d_vec3_make(3.0f, 0.1f, 0.8f),
                    (sdl3d_color){70, 210, 230, 255});

    /* Surveillance camera button in the dragon room. */
    if (surveillance != NULL)
    {
        const sdl3d_vec3 c = bounds_center(surveillance->button_bounds);
        const bool active_button = doom_surveillance_is_active(surveillance);
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(c.x, 0.18f, c.z), sdl3d_vec3_make(1.4f, 0.25f, 1.4f),
                        (sdl3d_color){45, 48, 55, 255});
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(c.x, 0.42f, c.z), sdl3d_vec3_make(0.9f, 0.25f, 0.9f),
                        active_button ? (sdl3d_color){45, 235, 90, 255} : (sdl3d_color){235, 45, 45, 255});
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(c.x, 1.35f, c.z), sdl3d_vec3_make(0.18f, 1.6f, 0.18f),
                        (sdl3d_color){50, 60, 75, 255});
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(c.x, 2.2f, c.z), sdl3d_vec3_make(0.8f, 0.45f, 0.45f),
                        active_button ? (sdl3d_color){40, 180, 90, 255} : (sdl3d_color){70, 90, 120, 255});
    }

    /* Sprites */
    {
        for (int i = 0; i < ent->sprites.count; ++i)
        {
            sdl3d_sprite_actor *sa = &ent->sprites.actors[i];
            sdl3d_vec3 light_sample_position = sa->position;
            light_sample_position.y += sa->size.y * 0.5f;
            sa->tint = sample_actor_tint(g_lights, g_light_count, light_sample_position);
            sa->sector_id =
                rs->portal_culling ? sdl3d_level_find_sector(active, g_sectors, sa->position.x, sa->position.z) : -1;
        }
        sdl3d_sprite_scene_draw(&ent->sprites, ctx, cam.position, rs->portal_culling ? &rs->vis : NULL);
    }

    /* Sector hazard particles */
    if (!doom_hazard_particles_draw(hazards, ctx))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Hazard particle draw failed: %s", SDL_GetError());
        SDL_ClearError();
    }

    /* Projectile */
    if (player->proj_active)
    {
        sdl3d_set_emissive(ctx, 5.0f, 3.0f, 1.0f);
        sdl3d_draw_sphere(ctx, sdl3d_vec3_make(player->proj_x, player->proj_y, player->proj_z), 0.1f, 8, 8,
                          (sdl3d_color){255, 200, 100, 255});
        sdl3d_set_emissive(ctx, 0, 0, 0);
    }

    /* Crosshair */
    {
        float chx = cam.position.x + forward.x * 0.4f;
        float chy = cam.position.y + forward.y * 0.4f;
        float chz = cam.position.z + forward.z * 0.4f;
        sdl3d_set_emissive(ctx, 8, 8, 8);
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(chx, chy, chz), sdl3d_vec3_make(0.003f, 0.003f, 0.003f),
                        (sdl3d_color){255, 255, 255, 255});
        sdl3d_set_emissive(ctx, 0, 0, 0);
    }

    sdl3d_end_mode_3d(ctx);

    /* HUD */
    if (font)
        sdl3d_draw_fps(ctx, font, dt);

    if (ui && font)
    {
        sdl3d_ui_begin_frame(ui, sdl3d_get_render_context_width(ctx), sdl3d_get_render_context_height(ctx));
        sdl3d_ui_label(ui, 10.0f, 60.0f, "SDL3D UI - Phase 1");
        sdl3d_ui_labelf(ui, 10.0f, 100.0f, "sector=%d  visible=%d/%d", current_sector, rs->vis.visible_count,
                        sector_count);
        sdl3d_ui_labelf(ui, 10.0f, 140.0f, "pos %.1f, %.1f, %.1f", px, py, pz);
        sdl3d_ui_labelf(ui, 10.0f, 180.0f, "profile=%s", render_profile_name ? render_profile_name : "Modern");
        if (doom_surveillance_is_active(surveillance))
            sdl3d_ui_label(ui, 10.0f, 220.0f, "SURVEILLANCE VIEW");
        sdl3d_ui_end_frame(ui);
        sdl3d_ui_render(ui, ctx);
    }

    if (rs->show_debug)
    {
        int visible_meshes = 0;
        if (rs->portal_culling)
        {
            for (int i = 0; i < active->model.mesh_count; i++)
            {
                int sid = active->mesh_sector_ids[i];
                if (sid >= 0 && sid < sector_count && rs->sector_visible != NULL && rs->sector_visible[sid])
                    visible_meshes++;
            }
        }
        else
        {
            visible_meshes = active->model.mesh_count;
        }
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "[VIS] sector=%d  visible=%d/%d sectors  meshes=%d/%d  portals=%d  culling=%s", current_sector,
                     rs->vis.visible_count, sector_count, visible_meshes, active->model.mesh_count,
                     active->portal_count, rs->portal_culling ? "ON" : "OFF");
    }
}

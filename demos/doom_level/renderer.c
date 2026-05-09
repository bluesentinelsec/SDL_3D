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
                       level_data *ld, entities *ent, const doom_hazard_particles *hazards, const doom_doors *doors,
                       const doom_surveillance_camera *surveillance, const player_state *player, int backbuffer_w,
                       int backbuffer_h, float dt, const char *render_profile_name)
{
    const sdl3d_fps_mover *mover = &player->mover;
    sdl3d_level *active = level_data_active(ld);
    const sdl3d_camera3d player_cam = sdl3d_fps_mover_camera(mover, 75.0f);
    const sdl3d_camera3d *surveillance_cam = doom_surveillance_active_camera(surveillance);
    sdl3d_camera3d cam = surveillance_cam != NULL ? *surveillance_cam : player_cam;
    const int sector_count = active->sector_count;

    float px = mover->position.x, py = mover->position.y;
    float pz = mover->position.z;

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

    /* Runtime doors are dynamic gameplay geometry, drawn after the static level mesh. */
    doom_doors_draw(doors, ctx);

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

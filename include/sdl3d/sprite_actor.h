/*
 * Doom-style billboard sprite actors with 8-direction rotation sets,
 * portal-based visibility culling, and depth-sorted drawing.
 *
 * Usage:
 *   sdl3d_sprite_scene sprites;
 *   sdl3d_sprite_scene_init(&sprites);
 *   sdl3d_sprite_actor *e = sdl3d_sprite_scene_add(&sprites);
 *   e->position = sdl3d_vec3_make(5, 0, 10);
 *   e->size     = (sdl3d_vec2){3.4f, 5.2f};
 *   e->texture  = &enemy_tex;
 *   e->rotations = &enemy_rot;
 *   e->tint     = (sdl3d_color){255,255,255,255};
 *
 *   // each frame:
 *   sdl3d_sprite_scene_update(&sprites, dt);
 *   sdl3d_sprite_scene_draw(&sprites, ctx, cam_pos, &vis);
 *
 *   sdl3d_sprite_scene_free(&sprites);
 *
 * Tinting is the caller's responsibility — set each actor's tint
 * before drawing. Use sdl3d_level_sample_light from level.h as a
 * building block for point-light contribution.
 */

#ifndef SDL3D_SPRITE_ACTOR_H
#define SDL3D_SPRITE_ACTOR_H

#include <stdbool.h>

#include "sdl3d/level.h"
#include "sdl3d/render_context.h"
#include "sdl3d/texture.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SDL3D_SPRITE_ROTATION_COUNT 8

    /*
     * 8-direction rotation set. Frame order follows the Doom convention:
     *   [0] S, [1] SE, [2] E, [3] NE, [4] N, [5] NW, [6] W, [7] SW
     * NULL entries fall back to the actor's single texture.
     */
    typedef struct sdl3d_sprite_rotation_set
    {
        const sdl3d_texture2d *frames[SDL3D_SPRITE_ROTATION_COUNT];
    } sdl3d_sprite_rotation_set;

    typedef struct sdl3d_sprite_actor
    {
        sdl3d_vec3 position;
        sdl3d_vec2 size;
        const sdl3d_texture2d *texture;             /* single-frame fallback     */
        const sdl3d_sprite_rotation_set *rotations; /* NULL = no rotation      */
        sdl3d_color tint;
        bool visible;
        int sector_id; /* for portal culling, -1 = unassigned */

        /* Bobbing animation. Set bob_amplitude > 0 to enable. */
        float bob_amplitude;
        float bob_speed;
        float bob_phase; /* internal accumulator, advanced by _update */
    } sdl3d_sprite_actor;

    typedef struct sdl3d_sprite_scene
    {
        sdl3d_sprite_actor *actors;
        int count;
        int capacity;
    } sdl3d_sprite_scene;

    void sdl3d_sprite_scene_init(sdl3d_sprite_scene *scene);
    void sdl3d_sprite_scene_free(sdl3d_sprite_scene *scene);

    /*
     * Add a sprite actor to the scene. Returns a pointer to the new actor
     * (valid until the next add or remove). The actor is zero-initialized
     * with visible = true and sector_id = -1.
     */
    sdl3d_sprite_actor *sdl3d_sprite_scene_add(sdl3d_sprite_scene *scene);

    /* Remove the actor at the given index. Swaps with the last element. */
    void sdl3d_sprite_scene_remove(sdl3d_sprite_scene *scene, int index);

    /* Advance bobbing phase for all actors. */
    void sdl3d_sprite_scene_update(sdl3d_sprite_scene *scene, float dt);

    /*
     * Select the correct rotation frame for a sprite given the camera's
     * XZ position. Returns the actor's single texture if rotations is NULL
     * or the selected frame is NULL.
     */
    const sdl3d_texture2d *sdl3d_sprite_select_texture(const sdl3d_sprite_actor *actor, float cam_x, float cam_z);

    /*
     * Draw all visible sprites: portal cull via vis (optional), select
     * rotation frame, depth sort back-to-front, draw billboards.
     *
     * The caller is responsible for setting each actor's tint before
     * calling this function. Pass vis = NULL to skip portal culling.
     */
    void sdl3d_sprite_scene_draw(sdl3d_sprite_scene *scene, sdl3d_render_context *context, sdl3d_vec3 camera_position,
                                 const sdl3d_visibility_result *vis);

#ifdef __cplusplus
}
#endif

#endif

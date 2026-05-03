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

    /**
     * @brief 8-direction rotation set for a billboard sprite.
     *
     * Frame order follows the Doom convention:
     * [0] S, [1] SE, [2] E, [3] NE, [4] N, [5] NW, [6] W, [7] SW.
     * NULL entries fall back to the actor's single texture.
     */
    typedef struct sdl3d_sprite_rotation_set
    {
        const sdl3d_texture2d *frames[SDL3D_SPRITE_ROTATION_COUNT];
    } sdl3d_sprite_rotation_set;

    /**
     * @brief Doom-style billboard sprite actor.
     *
     * The actor can be a single texture, an 8-direction rotation set, or a
     * timed sequence of 8-direction rotation sets. The position is the
     * logical ground-contact point for floor snapping and gameplay queries.
     */
    typedef struct sdl3d_sprite_actor
    {
        sdl3d_vec3 position;
        sdl3d_vec2 size;
        const sdl3d_texture2d *texture;                    /**< Single-frame fallback texture. */
        const sdl3d_sprite_rotation_set *rotations;        /**< Base rotation set, or NULL for a single texture. */
        const sdl3d_sprite_rotation_set *animation_frames; /**< Contiguous array of animation frame rotations. */
        int animation_frame_count;                         /**< Number of entries in animation_frames. */
        float animation_fps;                               /**< Animation playback rate in frames per second. */
        bool animation_loop;                               /**< True to wrap after the last frame. */
        float animation_time;                              /**< Current animation playback time in seconds. */
        bool lighting;                      /**< True when the sprite participates in dynamic lighting. */
        bool emissive;                      /**< True when the sprite is emissive. */
        const char *shader_vertex_source;   /**< Optional custom sprite shader vertex source. */
        const char *shader_fragment_source; /**< Optional custom sprite shader fragment source. */
        sdl3d_color tint;
        bool visible;
        int sector_id;              /**< Portal culling sector, or -1 when unassigned. */
        float visual_ground_offset; /**< World units between the quad bottom and the visible feet/contact point. */
        float facing_yaw;           /**< World yaw, in radians, for the actor side represented by rotation frame 0. */

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

    /**
     * @brief Start a timed rotation-set animation on a sprite actor.
     *
     * @param actor Actor to configure. NULL is ignored.
     * @param frames Contiguous array of rotation sets. The caller owns this
     * storage and must keep it alive while the animation is active.
     * @param frame_count Number of rotation sets in frames.
     * @param fps Playback rate in frames per second. Values <= 0 disable the animation.
     * @param loop True to wrap to frame 0 after the last frame; false to hold the last frame.
     *
     * The actor's base rotations and texture remain as fallbacks for missing
     * frame textures and for when the animation is stopped.
     */
    void sdl3d_sprite_actor_play_animation(sdl3d_sprite_actor *actor, const sdl3d_sprite_rotation_set *frames,
                                           int frame_count, float fps, bool loop);

    /**
     * @brief Stop a timed sprite animation and return to the actor's base rotation set.
     *
     * Safe to call with NULL. The actor's base texture and rotations are not modified.
     */
    void sdl3d_sprite_actor_stop_animation(sdl3d_sprite_actor *actor);

    /**
     * @brief Return the currently selected animation frame index.
     *
     * Returns 0 when actor is NULL or no timed animation is active.
     */
    int sdl3d_sprite_actor_current_animation_frame(const sdl3d_sprite_actor *actor);

    /**
     * @brief Return the render-space billboard pivot for a sprite actor.
     *
     * The returned point starts at actor->position, subtracts
     * visual_ground_offset on Y so transparent padding below the feet can sit
     * below the logical ground contact point, then applies bobbing. NULL
     * returns {0, 0, 0}.
     */
    sdl3d_vec3 sdl3d_sprite_actor_draw_position(const sdl3d_sprite_actor *actor);

    /**
     * @brief Set a sprite actor's facing yaw in radians.
     *
     * Yaw uses the same XZ convention as sdl3d_sprite_select_texture: 0 means
     * rotation frame 0 faces world -Z, pi/2 faces world +X, pi faces world
     * +Z, and -pi/2 faces world -X. This preserves legacy behavior for actors
     * whose facing_yaw is left at its zero-initialized default.
     */
    void sdl3d_sprite_actor_set_facing_yaw(sdl3d_sprite_actor *actor, float yaw_radians);

    /**
     * @brief Set a sprite actor's facing yaw from a world-space XZ direction.
     *
     * Safe with NULL actors. Very small direction vectors are ignored so callers
     * can pass movement deltas without special-casing idle frames.
     */
    void sdl3d_sprite_actor_set_facing_direction(sdl3d_sprite_actor *actor, float direction_x, float direction_z);

    /**
     * @brief Test whether a sprite actor can stand at a target XZ point.
     *
     * Uses sdl3d_level_find_walkable_sector() with the actor's current Y as
     * the feet height, so step_height controls how far the actor can step up
     * in one move. actor_height is the gameplay/collision height, not
     * necessarily the visual billboard height. If out_floor_y is non-NULL,
     * it receives the walkable floor height at the target.
     */
    bool sdl3d_sprite_actor_can_stand_at(const sdl3d_sprite_actor *actor, const sdl3d_level *level,
                                         const sdl3d_sector *sectors, float target_x, float target_z, float step_height,
                                         float actor_height, float *out_floor_y);

    /**
     * @brief Snap a sprite actor to the highest support floor under its XZ point.
     *
     * Uses the actor's current Y plus step_height as the support probe height,
     * which lets callers snap actors onto nearby stairs and slopes without
     * teleporting through floors above them. actor_height is the gameplay
     * collision height used for headroom checks. Returns false if no support
     * sector is available.
     */
    bool sdl3d_sprite_actor_snap_to_ground(sdl3d_sprite_actor *actor, const sdl3d_level *level,
                                           const sdl3d_sector *sectors, float step_height, float actor_height);

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

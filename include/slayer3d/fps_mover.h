/*
 * First-person movement controller for sector-based levels.
 *
 * Encapsulates reusable sector-world first-person physics: gravity, jumping,
 * stair stepping, wall sliding, ceiling collision, substepped vertical
 * integration (so a single fast frame cannot skip past a thin stair),
 * ground-trace rescue, and a last-known-good position fallback.
 *
 * The caller owns the game loop and the input system. Per frame:
 *   - poll input, accumulate mouse_dx / mouse_dy and a wish direction
 *     in world XZ space (already rotated by the player's facing);
 *   - call slayer3d_fps_mover_update;
 *   - build a camera with slayer3d_fps_mover_camera and render.
 *
 * Use slayer3d_fps_mover_jump from the input handler when the jump key
 * fires; the mover ignores the call if it is not on the ground.
 */

#ifndef SLAYER3D_FPS_MOVER_H
#define SLAYER3D_FPS_MOVER_H

#include <stdbool.h>

#include "slayer3d/camera.h"
#include "slayer3d/level.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct slayer3d_fps_mover_config
    {
        float move_speed;        /* horizontal units / second                   */
        float jump_velocity;     /* upward velocity applied on jump             */
        float gravity;           /* downward acceleration (positive number)     */
        float player_height;     /* eye to feet                                 */
        float player_radius;     /* cylinder radius for sliding collision tests */
        float step_height;       /* maximum stair step the player can climb    */
        float ceiling_clearance; /* required gap between head and ceiling       */
    } slayer3d_fps_mover_config;

    typedef struct slayer3d_fps_mover
    {
        /* Public read/write — position and orientation. Treat as the eye
         * position; feet are at position.y - config.player_height. */
        slayer3d_vec3 position;
        float yaw;
        float pitch;

        /* Public read — physics state. */
        bool on_ground;
        float vertical_velocity;
        float view_smooth; /* Quake-style stair smoothing offset, eye-space */
        int current_sector;

        /* Internal. */
        slayer3d_fps_mover_config config;
        slayer3d_vec3 last_good_position;
        bool has_last_good;
    } slayer3d_fps_mover;

    /*
     * Initialize a mover with the given configuration. Spawn position is
     * the eye position (feet land at spawn_position.y - player_height).
     */
    void slayer3d_fps_mover_init(slayer3d_fps_mover *mover, const slayer3d_fps_mover_config *config,
                                 slayer3d_vec3 spawn_position, float spawn_yaw);

    /*
     * Advance the mover by dt seconds.
     *
     * wish_dir is the desired XZ movement direction in world space — the
     * caller is responsible for rotating WASD input by the player's yaw
     * before passing it in. Magnitudes greater than 1 are clamped.
     *
     * mouse_dx / mouse_dy are raw event deltas (typically xrel / yrel
     * from SDL_EVENT_MOUSE_MOTION); they are scaled by mouse_sensitivity
     * before being applied to yaw / pitch.
     */
    void slayer3d_fps_mover_update(slayer3d_fps_mover *mover, const slayer3d_level *level,
                                   const slayer3d_sector *sectors, slayer3d_vec2 wish_dir, float mouse_dx,
                                   float mouse_dy, float mouse_sensitivity, float dt);

    /*
     * Apply jump_velocity if the mover is currently on the ground.
     * No-op otherwise. Safe with NULL.
     */
    void slayer3d_fps_mover_jump(slayer3d_fps_mover *mover);

    /**
     * @brief Apply an upward launch velocity to a first-person mover.
     *
     * This is useful for jump pads, wind vents, bounce pads, or scripted
     * launchers. Values less than or equal to zero are ignored. Unlike
     * slayer3d_fps_mover_jump(), launching does not require the mover to be on
     * the ground.
     */
    void slayer3d_fps_mover_launch(slayer3d_fps_mover *mover, float vertical_velocity);

    /**
     * @brief Instantly move a first-person mover to a new eye position.
     *
     * Teleporting resets vertical velocity, stair smoothing, sector cache, and
     * last-known-good state so later collision recovery cannot snap the player
     * back to the pre-teleport location. Pass false for set_yaw or set_pitch to
     * preserve the current facing on that axis.
     */
    void slayer3d_fps_mover_teleport(slayer3d_fps_mover *mover, slayer3d_vec3 eye_position, bool set_yaw, float yaw,
                                     bool set_pitch, float pitch);

    /*
     * Build a camera looking along the mover's facing. The camera position
     * includes the view-smooth offset so stair transitions appear smooth.
     */
    slayer3d_camera3d slayer3d_fps_mover_camera(const slayer3d_fps_mover *mover, float fov);

#ifdef __cplusplus
}
#endif

#endif

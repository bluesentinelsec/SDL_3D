/*
 * First-person movement controller for sector-based levels.
 *
 * Encapsulates the physics that ship in the doom_level demo: gravity,
 * jumping, stair stepping, wall sliding, ceiling collision, substepped
 * vertical integration (so a single fast frame cannot skip past a thin
 * stair), ground-trace rescue, and a last-known-good position fallback.
 *
 * The caller owns the game loop and the input system. Per frame:
 *   - poll input, accumulate mouse_dx / mouse_dy and a wish direction
 *     in world XZ space (already rotated by the player's facing);
 *   - call sdl3d_fps_mover_update;
 *   - build a camera with sdl3d_fps_mover_camera and render.
 *
 * Use sdl3d_fps_mover_jump from the input handler when the jump key
 * fires; the mover ignores the call if it is not on the ground.
 */

#ifndef SDL3D_FPS_MOVER_H
#define SDL3D_FPS_MOVER_H

#include <stdbool.h>

#include "sdl3d/camera.h"
#include "sdl3d/level.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct sdl3d_fps_mover_config
    {
        float move_speed;        /* horizontal units / second                   */
        float jump_velocity;     /* upward velocity applied on jump             */
        float gravity;           /* downward acceleration (positive number)     */
        float player_height;     /* eye to feet                                 */
        float player_radius;     /* cylinder radius for sliding collision tests */
        float step_height;       /* maximum stair step the player can climb    */
        float ceiling_clearance; /* required gap between head and ceiling       */
    } sdl3d_fps_mover_config;

    typedef struct sdl3d_fps_mover
    {
        /* Public read/write — position and orientation. Treat as the eye
         * position; feet are at position.y - config.player_height. */
        sdl3d_vec3 position;
        float yaw;
        float pitch;

        /* Public read — physics state. */
        bool on_ground;
        float vertical_velocity;
        float view_smooth; /* Quake-style stair smoothing offset, eye-space */
        int current_sector;

        /* Internal. */
        sdl3d_fps_mover_config config;
        sdl3d_vec3 last_good_position;
        bool has_last_good;
    } sdl3d_fps_mover;

    /*
     * Initialize a mover with the given configuration. Spawn position is
     * the eye position (feet land at spawn_position.y - player_height).
     */
    void sdl3d_fps_mover_init(sdl3d_fps_mover *mover, const sdl3d_fps_mover_config *config, sdl3d_vec3 spawn_position,
                              float spawn_yaw);

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
    void sdl3d_fps_mover_update(sdl3d_fps_mover *mover, const sdl3d_level *level, const sdl3d_sector *sectors,
                                sdl3d_vec2 wish_dir, float mouse_dx, float mouse_dy, float mouse_sensitivity, float dt);

    /*
     * Apply jump_velocity if the mover is currently on the ground.
     * No-op otherwise. Safe with NULL.
     */
    void sdl3d_fps_mover_jump(sdl3d_fps_mover *mover);

    /*
     * Build a camera looking along the mover's facing. The camera position
     * includes the view-smooth offset so stair transitions appear smooth.
     */
    sdl3d_camera3d sdl3d_fps_mover_camera(const sdl3d_fps_mover *mover, float fovy);

#ifdef __cplusplus
}
#endif

#endif

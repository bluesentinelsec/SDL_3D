/**
 * @file time.h
 * @brief Frame timing, time scaling, and fixed-timestep utilities.
 *
 * Provides frame-rate-independent delta time, a global time scale for
 * pause/slow-motion, and a fixed-timestep accumulator for deterministic
 * physics. Call sdl3d_time_update() once per frame before any game logic.
 *
 * Usage:
 * @code
 *   // Game loop
 *   while (running) {
 *       sdl3d_time_update();
 *
 *       // Fixed-rate physics (deterministic)
 *       for (int i = 0; i < sdl3d_time_get_fixed_step_count(); i++)
 *           update_physics(sdl3d_time_get_fixed_delta_time());
 *
 *       // Variable-rate rendering
 *       float alpha = sdl3d_time_get_fixed_interpolation();
 *       render(alpha);
 *   }
 * @endcode
 */

#ifndef SDL3D_TIME_H
#define SDL3D_TIME_H

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Sample the clock and compute delta times for this frame.
     *
     * Must be called exactly once per frame, before any game logic that
     * reads delta time. Internally advances the fixed-timestep accumulator.
     */
    void sdl3d_time_update(void);

    /**
     * @brief Get the scaled delta time for this frame, in seconds.
     *
     * This is the real elapsed time since the last sdl3d_time_update(),
     * multiplied by the current time scale. Use this for gameplay logic,
     * movement, and animation.
     *
     * @return Scaled delta time in seconds.
     */
    float sdl3d_time_get_delta_time(void);

    /**
     * @brief Get the real (unscaled) delta time for this frame, in seconds.
     *
     * Unaffected by time scale. Use this for UI updates, input smoothing,
     * and anything that should run at real-world speed regardless of
     * pause or slow-motion state.
     *
     * @return Unscaled delta time in seconds.
     */
    float sdl3d_time_get_unscaled_delta_time(void);

    /**
     * @brief Get the total elapsed game time in seconds.
     *
     * Accumulated from scaled delta times. Pauses when time_scale is 0.
     *
     * @return Total game time in seconds since the first sdl3d_time_update().
     */
    float sdl3d_time_get_time(void);

    /**
     * @brief Get the total elapsed real time in seconds.
     *
     * Accumulated from unscaled delta times. Always advances.
     *
     * @return Total real time in seconds since the first sdl3d_time_update().
     */
    float sdl3d_time_get_real_time(void);

    /**
     * @brief Set the global time scale multiplier.
     *
     * - 0.0 = paused (game time stops, real time continues)
     * - 0.5 = half speed
     * - 1.0 = normal (default)
     * - 2.0 = double speed
     *
     * Negative values are clamped to 0.
     *
     * @param scale The time scale multiplier.
     */
    void sdl3d_time_set_scale(float scale);

    /**
     * @brief Get the current time scale multiplier.
     *
     * @return The current time scale (default 1.0).
     */
    float sdl3d_time_get_scale(void);

    /**
     * @brief Set the fixed timestep interval for physics updates.
     *
     * Default is 1/60 (0.01666...). The fixed-timestep accumulator
     * consumes time in chunks of this size each frame.
     *
     * @param dt Fixed delta time in seconds. Must be > 0.
     */
    void sdl3d_time_set_fixed_delta_time(float dt);

    /**
     * @brief Get the fixed timestep interval.
     *
     * @return Fixed delta time in seconds (default 1/60).
     */
    float sdl3d_time_get_fixed_delta_time(void);

    /**
     * @brief Get the number of fixed-timestep iterations for this frame.
     *
     * After sdl3d_time_update(), this returns how many fixed-step
     * physics iterations should run. Typically 0 or 1, but can be
     * higher if the frame took longer than the fixed interval.
     *
     * Capped at 8 to prevent spiral-of-death on long frames.
     *
     * @return Number of fixed steps to execute this frame.
     */
    int sdl3d_time_get_fixed_step_count(void);

    /**
     * @brief Get the interpolation factor for rendering between fixed steps.
     *
     * Returns a value in [0, 1] representing how far the current frame
     * is between the last fixed step and the next. Use this to
     * interpolate visual positions between the previous and current
     * physics state for smooth rendering.
     *
     * @return Interpolation alpha in [0, 1].
     */
    float sdl3d_time_get_fixed_interpolation(void);

    /**
     * @brief Reset all time state to initial values.
     *
     * Resets elapsed times to 0, time scale to 1.0, and clears the
     * fixed-timestep accumulator. Useful when restarting a level.
     */
    void sdl3d_time_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_TIME_H */

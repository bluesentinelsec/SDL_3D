/**
 * @file timer_pool.h
 * @brief Deferred and repeating signal emission via countdown timers.
 *
 * A timer counts down by caller-provided dt each frame. When it
 * expires, it emits a signal via the signal bus. One-shot timers
 * become inactive after firing. Repeating timers reset and fire
 * again at the specified interval.
 *
 * Timers emit signals, not callbacks. This keeps them in the same
 * composable signal/action framework as the rest of the gameplay
 * mechanics system.
 *
 * Usage:
 * @code
 *   sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
 *
 *   // 30-second countdown → emit SIG_UNLOCK when it expires.
 *   int id = sdl3d_timer_start(pool, 30.0f, SIG_UNLOCK, false, 0);
 *
 *   // Each frame:
 *   sdl3d_timer_pool_update(pool, bus, dt);
 *
 *   // Cancel if needed:
 *   sdl3d_timer_cancel(pool, id);
 *
 *   sdl3d_timer_pool_destroy(pool);
 * @endcode
 */

#ifndef SDL3D_TIMER_POOL_H
#define SDL3D_TIMER_POOL_H

#include <stdbool.h>

#include "sdl3d/signal_bus.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque timer pool handle. */
    typedef struct sdl3d_timer_pool sdl3d_timer_pool;

    /* ================================================================== */
    /* Lifecycle                                                          */
    /* ================================================================== */

    /**
     * @brief Create an empty timer pool.
     * @return A new timer pool, or NULL on allocation failure.
     */
    sdl3d_timer_pool *sdl3d_timer_pool_create(void);

    /**
     * @brief Destroy a timer pool and free all timers.
     *
     * Active timers are silently cancelled. Safe to call with NULL.
     */
    void sdl3d_timer_pool_destroy(sdl3d_timer_pool *pool);

    /* ================================================================== */
    /* Timer management                                                   */
    /* ================================================================== */

    /**
     * @brief Start a new timer.
     *
     * The timer counts down from @p delay seconds. When it reaches zero,
     * it emits @p signal_id via the bus passed to sdl3d_timer_pool_update.
     *
     * @param pool      The timer pool.
     * @param delay     Initial countdown in seconds. Must be > 0.
     * @param signal_id Signal to emit on expiry.
     * @param repeating If true, the timer resets to @p interval after
     *                  firing and fires again. If false, the timer
     *                  becomes inactive after one firing.
     * @param interval  Reset interval for repeating timers (seconds).
     *                  Ignored for one-shot timers. Must be > 0 for
     *                  repeating timers.
     * @return A timer ID (>= 1) for cancellation, or 0 on failure.
     */
    int sdl3d_timer_start(sdl3d_timer_pool *pool, float delay, int signal_id, bool repeating, float interval);

    /**
     * @brief Cancel an active timer.
     *
     * The timer will not fire. No-op if the ID is invalid or the timer
     * has already fired/been cancelled.
     */
    void sdl3d_timer_cancel(sdl3d_timer_pool *pool, int timer_id);

    /* ================================================================== */
    /* Per-frame update                                                   */
    /* ================================================================== */

    /**
     * @brief Tick all active timers by dt seconds.
     *
     * Expired timers emit their signal via @p bus. One-shot timers
     * become inactive. Repeating timers reset their countdown to
     * their interval.
     *
     * Uses caller-provided dt, not sdl3d_time_get_delta_time(). This
     * lets the caller decide whether timers respect time scale (game
     * time) or run at real time.
     *
     * @param pool The timer pool.
     * @param bus  Signal bus for emission. Must not be NULL.
     * @param dt   Elapsed time in seconds. Must be >= 0.
     */
    void sdl3d_timer_pool_update(sdl3d_timer_pool *pool, sdl3d_signal_bus *bus, float dt);

    /* ================================================================== */
    /* Query                                                              */
    /* ================================================================== */

    /**
     * @brief Get the number of active timers in the pool.
     */
    int sdl3d_timer_pool_active_count(const sdl3d_timer_pool *pool);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_TIMER_POOL_H */

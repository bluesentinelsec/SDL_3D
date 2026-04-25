/**
 * @file game.h
 * @brief Managed SDL3D game loop with fixed-timestep simulation.
 *
 * The managed game loop owns SDL initialization, window and render-context
 * creation, SDL event polling, fixed-rate simulation ticks, frame presentation,
 * and engine subsystem cleanup. Games provide callbacks and caller-owned
 * userdata; SDL3D owns the outer loop.
 */

#ifndef SDL3D_GAME_H
#define SDL3D_GAME_H

#include <stdbool.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include "sdl3d/actor_registry.h"
#include "sdl3d/render_context.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/timer_pool.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Engine-owned state passed to every managed-loop callback.
     *
     * The game may read these fields and may set quit_requested to request
     * shutdown. The loop creates the window, render context, registry, signal
     * bus, and timer pool before init, then destroys them after shutdown.
     */
    typedef struct sdl3d_game_context
    {
        SDL_Window *window;             /**< SDL window owned by the managed loop. */
        sdl3d_render_context *renderer; /**< SDL3D render context owned by the managed loop. */
        sdl3d_actor_registry *registry; /**< Actor registry owned by the managed loop. */
        sdl3d_signal_bus *bus;          /**< Signal bus owned by the managed loop. */
        sdl3d_timer_pool *timers;       /**< Timer pool owned by the managed loop. */
        float time;                     /**< Simulated game time advanced by fixed ticks. */
        float real_time;                /**< Wall-clock time accumulated by rendered frames. */
        int tick_count;                 /**< Total fixed ticks executed since startup. */
        bool quit_requested;            /**< Set true from any callback to leave the loop. */
    } sdl3d_game_context;

    /**
     * @brief Callback table for sdl3d_run_game.
     *
     * Every callback receives the engine-owned context plus the userdata passed
     * to sdl3d_run_game. Any callback may be NULL and is treated as a no-op.
     */
    typedef struct sdl3d_game_callbacks
    {
        /**
         * @brief Called once after engine subsystems are created.
         *
         * Load assets, create actors, initialize UI, and build level state here.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to sdl3d_run_game.
         * @return true to enter the game loop, false to abort startup.
         */
        bool (*init)(sdl3d_game_context *ctx, void *userdata);

        /**
         * @brief Called for each non-quit SDL event.
         *
         * SDL_EVENT_QUIT is handled by the managed loop and always requests
         * shutdown. Return false from this callback to request shutdown for
         * game-specific events such as Escape.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to sdl3d_run_game.
         * @param event    SDL event for game-specific handling.
         * @return true to continue, false to request shutdown.
         */
        bool (*event)(sdl3d_game_context *ctx, void *userdata, const SDL_Event *event);

        /**
         * @brief Called at the fixed simulation timestep.
         *
         * The dt value is always config.tick_rate seconds, or 1/60 by default.
         * The managed loop updates ctx->timers before this callback. It does not
         * call sdl3d_actor_registry_update automatically because trigger tests
         * need a game-defined point such as the player position.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to sdl3d_run_game.
         * @param dt       Fixed timestep in seconds.
         */
        void (*tick)(sdl3d_game_context *ctx, void *userdata, float dt);

        /**
         * @brief Called once per rendered frame after fixed ticks.
         *
         * The alpha value is the interpolation fraction in [0, 1] between the
         * last completed tick and the next tick. The game owns clearing and all
         * drawing. The managed loop presents ctx->renderer after this callback
         * returns, so render callbacks should not call sdl3d_present_render_context.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to sdl3d_run_game.
         * @param alpha    Render interpolation factor in the range [0, 1].
         */
        void (*render)(sdl3d_game_context *ctx, void *userdata, float alpha);

        /**
         * @brief Called once after the loop exits and before engine cleanup.
         *
         * Free caller-owned assets, UI, levels, and gameplay state here. Engine
         * subsystems in ctx are still valid for the duration of this callback.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to sdl3d_run_game.
         */
        void (*shutdown)(sdl3d_game_context *ctx, void *userdata);
    } sdl3d_game_callbacks;

    /**
     * @brief Configuration for sdl3d_run_game.
     *
     * Zero-initialization selects sensible defaults.
     */
    typedef struct sdl3d_game_config
    {
        const char *title;       /**< Window title, or "SDL3D" when NULL. */
        int width;               /**< Window width in pixels, or 1280 when <= 0. */
        int height;              /**< Window height in pixels, or 720 when <= 0. */
        sdl3d_backend backend;   /**< Requested backend, or SDL3D_BACKEND_AUTO when zero. */
        float tick_rate;         /**< Fixed timestep in seconds, or 1/60 when <= 0. */
        int max_ticks_per_frame; /**< Catch-up tick cap per rendered frame, or 8 when <= 0. */
    } sdl3d_game_config;

    /**
     * @brief Run a managed SDL3D game loop.
     *
     * Initializes SDL video, creates the window, render context, actor registry,
     * signal bus, and timer pool, then runs a fixed-timestep simulation loop
     * until quit is requested. The loop updates the timer pool before each tick,
     * calls sdl3d_time_update once per rendered frame, and presents the render
     * context after each render callback.
     *
     * @param config    Optional configuration. NULL selects all defaults.
     * @param callbacks Optional callback table. NULL treats all callbacks as no-ops.
     * @param userdata  Caller-owned pointer passed to every callback.
     * @return 0 on normal shutdown, 1 on startup or runtime failure.
     */
    int sdl3d_run_game(const sdl3d_game_config *config, const sdl3d_game_callbacks *callbacks, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_GAME_H */

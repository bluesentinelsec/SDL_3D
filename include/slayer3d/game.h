/**
 * @file game.h
 * @brief Generic game sessions and managed SLAYER3D game loop.
 *
 * Game sessions group genre-neutral runtime services such as actors, signals,
 * timers, logic, input, and optional audio. The managed game loop owns SDL
 * initialization, window and render-context creation, SDL event polling,
 * fixed-rate simulation ticks, frame presentation, and engine subsystem
 * cleanup. Games provide callbacks and caller-owned userdata; SLAYER3D owns the
 * outer loop.
 */

#ifndef SLAYER3D_GAME_H
#define SLAYER3D_GAME_H

#include <stdbool.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include "slayer3d/actor_registry.h"
#include "slayer3d/audio.h"
#include "slayer3d/input.h"
#include "slayer3d/render_context.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/timer_pool.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Default authored virtual resolution for SLAYER3D games.
 *
 * The managed loop presents this logical resolution with letterboxing by
 * default so games can scale to user displays while preserving aspect ratio.
 */
#define SLAYER3D_GAME_DEFAULT_LOGICAL_WIDTH 1280

/** @brief Default authored virtual height for SLAYER3D games. */
#define SLAYER3D_GAME_DEFAULT_LOGICAL_HEIGHT 720

    /** @brief Opaque game-session container for genre-neutral runtime services. */
    typedef struct slayer3d_game_session slayer3d_game_session;

    /** @brief Opaque logic-world handle; see logic.h for the full API. */
    typedef struct slayer3d_logic_world slayer3d_logic_world;

    /**
     * @brief Runtime services a game session can create and own.
     *
     * These flags are used by slayer3d_game_session_desc::create_services. Services
     * not listed there may still be supplied as borrowed pointers in the desc.
     */
    typedef enum slayer3d_game_session_service
    {
        SLAYER3D_GAME_SESSION_SERVICE_NONE = 0,              /**< Create no services. */
        SLAYER3D_GAME_SESSION_SERVICE_REGISTRY = 1u << 0,    /**< Create an actor registry. */
        SLAYER3D_GAME_SESSION_SERVICE_SIGNAL_BUS = 1u << 1,  /**< Create a signal bus. */
        SLAYER3D_GAME_SESSION_SERVICE_TIMER_POOL = 1u << 2,  /**< Create a timer pool. */
        SLAYER3D_GAME_SESSION_SERVICE_LOGIC_WORLD = 1u << 3, /**< Create a logic world. */
        SLAYER3D_GAME_SESSION_SERVICE_INPUT = 1u << 4,       /**< Create an input manager. */
        SLAYER3D_GAME_SESSION_SERVICE_AUDIO = 1u << 5,       /**< Create an audio engine. */
        SLAYER3D_GAME_SESSION_SERVICE_CORE =
            SLAYER3D_GAME_SESSION_SERVICE_REGISTRY | SLAYER3D_GAME_SESSION_SERVICE_SIGNAL_BUS |
            SLAYER3D_GAME_SESSION_SERVICE_TIMER_POOL | SLAYER3D_GAME_SESSION_SERVICE_LOGIC_WORLD |
            SLAYER3D_GAME_SESSION_SERVICE_INPUT /**< Default non-rendering gameplay services. */
    } slayer3d_game_session_service;

    /**
     * @brief Creation descriptor for a generic game session.
     *
     * The session creates and owns services listed in @p create_services.
     * Service pointers supplied here but not listed in @p create_services are
     * borrowed and will not be destroyed with the session. Supplying both a
     * borrowed pointer and a create flag for the same service is invalid because
     * ownership would be ambiguous.
     *
     * The world pointer is intentionally opaque. It can reference a fixed
     * playfield, tile map, sector level, room graph, 3D scene, or caller-owned
     * game state without forcing one world representation into the core engine.
     */
    typedef struct slayer3d_game_session_desc
    {
        unsigned int create_services;      /**< Bitmask of slayer3d_game_session_service values to create and own. */
        slayer3d_actor_registry *registry; /**< Borrowed registry when REGISTRY is not created. */
        slayer3d_signal_bus *bus;          /**< Borrowed bus when SIGNAL_BUS is not created. */
        slayer3d_timer_pool *timers;       /**< Borrowed timers when TIMER_POOL is not created. */
        slayer3d_logic_world *logic;       /**< Borrowed logic world when LOGIC_WORLD is not created. */
        slayer3d_input_manager *input;     /**< Borrowed input manager when INPUT is not created. */
        slayer3d_audio_engine *audio;      /**< Borrowed audio engine when AUDIO is not created. */
        void *world;                       /**< Borrowed optional world representation. */
        const char *profile_name;          /**< Optional profile name copied into the session. */
        bool optional_audio; /**< If true, AUDIO creation failures leave audio NULL instead of failing creation. */
    } slayer3d_game_session_desc;

    /**
     * @brief Initialize a session descriptor with default core services.
     *
     * The default creates the registry, signal bus, timer pool, logic world,
     * and input manager. Audio is not created by default because many tests,
     * tools, and server-like loops do not have a stable platform audio device.
     */
    void slayer3d_game_session_desc_init(slayer3d_game_session_desc *desc);

    /**
     * @brief Create a generic runtime session.
     *
     * Passing NULL for @p desc uses slayer3d_game_session_desc_init() defaults.
     * Passing a zero-initialized descriptor creates no owned services and only
     * stores borrowed pointers supplied by the caller.
     *
     * When the session creates a logic world, a signal bus must be available
     * either through created or borrowed services. A timer pool is optional for
     * logic; timer actions become no-ops when no pool exists.
     *
     * @return true on success. On failure @p out_session is set to NULL.
     */
    bool slayer3d_game_session_create(const slayer3d_game_session_desc *desc, slayer3d_game_session **out_session);

    /**
     * @brief Destroy a game session and any services it owns.
     *
     * Borrowed services and the opaque world pointer are not destroyed. Safe to
     * call with NULL.
     */
    void slayer3d_game_session_destroy(slayer3d_game_session *session);

    /**
     * @brief Advance generic per-frame services.
     *
     * This updates real-time services such as audio fades. It does not advance
     * gameplay timers, input snapshots, simulation time, or tick counters.
     * Missing optional services are skipped. Negative real_dt is clamped to
     * zero.
     *
     * @return true when @p session is valid, false otherwise.
     */
    bool slayer3d_game_session_begin_frame(slayer3d_game_session *session, float real_dt);

    /**
     * @brief Refresh the input snapshot for the current session tick.
     *
     * Managed loops call this while paused so pause menus and unpause actions
     * can consume fresh input without advancing gameplay timers. Custom loops
     * may also call it before rendering when no fixed tick occurs.
     *
     * @return true when @p session is valid and has an input manager.
     */
    bool slayer3d_game_session_update_input(slayer3d_game_session *session);

    /**
     * @brief Begin one fixed simulation tick.
     *
     * This refreshes the input snapshot for the current tick and advances the
     * timer pool, which may emit signals through the session signal bus. It does
     * not increment session time or tick count; call
     * slayer3d_game_session_end_tick() after game-specific simulation callbacks
     * complete.
     *
     * Missing optional services are skipped. Negative dt is clamped to zero.
     *
     * @return true when @p session is valid, false otherwise.
     */
    bool slayer3d_game_session_begin_tick(slayer3d_game_session *session, float dt);

    /**
     * @brief Complete one fixed simulation tick.
     *
     * This advances session simulation time and increments the tick counter.
     * Negative dt is clamped to zero.
     *
     * @return true when @p session is valid, false otherwise.
     */
    bool slayer3d_game_session_end_tick(slayer3d_game_session *session, float dt);

    /**
     * @brief Advance one full fixed tick without caller simulation callbacks.
     *
     * This is a convenience wrapper around begin_tick and end_tick for tools,
     * tests, or simple loops that do not need to run game code between generic
     * timer/input work and tick-counter advancement.
     *
     * @return true when @p session is valid, false otherwise.
     */
    bool slayer3d_game_session_tick(slayer3d_game_session *session, float dt);

    /** @brief Return the session's actor registry, or NULL. */
    slayer3d_actor_registry *slayer3d_game_session_get_registry(const slayer3d_game_session *session);

    /** @brief Return the session's signal bus, or NULL. */
    slayer3d_signal_bus *slayer3d_game_session_get_signal_bus(const slayer3d_game_session *session);

    /** @brief Return the session's timer pool, or NULL. */
    slayer3d_timer_pool *slayer3d_game_session_get_timer_pool(const slayer3d_game_session *session);

    /** @brief Return the session's logic world, or NULL. */
    slayer3d_logic_world *slayer3d_game_session_get_logic_world(const slayer3d_game_session *session);

    /** @brief Return the session's input manager, or NULL. */
    slayer3d_input_manager *slayer3d_game_session_get_input(const slayer3d_game_session *session);

    /** @brief Return the session's audio engine, or NULL. */
    slayer3d_audio_engine *slayer3d_game_session_get_audio(const slayer3d_game_session *session);

    /** @brief Return the borrowed world pointer, or NULL. */
    void *slayer3d_game_session_get_world(const slayer3d_game_session *session);

    /** @brief Return the copied profile name, or NULL. */
    const char *slayer3d_game_session_get_profile_name(const slayer3d_game_session *session);

    /** @brief Return accumulated session simulation time in seconds. */
    float slayer3d_game_session_get_time(const slayer3d_game_session *session);

    /** @brief Return the number of session updates completed. */
    int slayer3d_game_session_get_tick_count(const slayer3d_game_session *session);

    /**
     * @brief Engine-owned state passed to every managed-loop callback.
     *
     * The game may read these fields and may set quit_requested to request
     * shutdown. The loop creates the window, render context, and game session
     * before init, then destroys them after shutdown. Runtime services such as
     * actors, signals, timers, logic, input, and optional audio live in
     * @p session.
     */
    typedef struct slayer3d_game_context
    {
        slayer3d_game_session *session;    /**< Managed game session owned by the loop. */
        SDL_Window *window;                /**< SDL window owned by the managed loop. */
        slayer3d_render_context *renderer; /**< SLAYER3D render context owned by the managed loop. */
        float real_time;                   /**< Wall-clock time accumulated by rendered frames. */
        bool paused;                       /**< When true, fixed ticks and timer pools are frozen. */
        bool input_event_consumed; /**< Set true by the event callback to skip raw input delivery for one event. */
        bool quit_requested;       /**< Set true from any callback to leave the loop. */
    } slayer3d_game_context;

    /**
     * @brief Callback table for slayer3d_run_game.
     *
     * Every callback receives the engine-owned context plus the userdata passed
     * to slayer3d_run_game. Any callback may be NULL and is treated as a no-op.
     */
    typedef struct slayer3d_game_callbacks
    {
        /**
         * @brief Called once after engine subsystems are created.
         *
         * Load assets, create actors, initialize UI, and build level state here.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to slayer3d_run_game.
         * @return true to enter the game loop, false to abort startup.
         */
        bool (*init)(slayer3d_game_context *ctx, void *userdata);

        /**
         * @brief Called for each non-quit SDL event.
         *
         * SDL_EVENT_QUIT is handled by the managed loop and always requests
         * shutdown. Set ctx->input_event_consumed to true to prevent the raw
         * event from reaching the session input manager. Return false from
         * this callback to request shutdown for game-specific events such as
         * Escape.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to slayer3d_run_game.
         * @param event    SDL event for game-specific handling.
         * @return true to continue, false to request shutdown.
         */
        bool (*event)(slayer3d_game_context *ctx, void *userdata, const SDL_Event *event);

        /**
         * @brief Called at the fixed simulation timestep.
         *
         * The dt value is always config.tick_rate seconds, or 1/60 by default.
         * The managed loop updates session input before each fixed tick so
         * pressed/released edges stay buffered until gameplay consumes them. It
         * updates session timers before each tick. It does not call
         * slayer3d_actor_registry_update automatically
         * because trigger tests need a game-defined point such as the player
         * position.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to slayer3d_run_game.
         * @param dt       Fixed timestep in seconds.
         */
        void (*tick)(slayer3d_game_context *ctx, void *userdata, float dt);

        /**
         * @brief Called once per rendered frame while paused.
         *
         * Use this for pause-screen animation, menu transitions, and other
         * visual-only work that should continue while simulation is frozen.
         * The real_dt value is wall-clock time in seconds and is not affected
         * by the fixed simulation timestep.
         *
         * While paused, the managed loop does not call tick and does not
         * advance timer pools. SDL events are still processed and session input is
         * updated before this callback so games can drive pause menus and
         * unpause through action bindings.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to slayer3d_run_game.
         * @param real_dt  Wall-clock delta time for the rendered frame.
         */
        void (*pause_tick)(slayer3d_game_context *ctx, void *userdata, float real_dt);

        /**
         * @brief Called once per rendered frame after fixed ticks.
         *
         * The alpha value is the interpolation fraction in [0, 1] between the
         * last completed tick and the next tick. The game owns clearing and all
         * drawing. The managed loop presents ctx->renderer after this callback
         * returns, so render callbacks should not call slayer3d_present_render_context.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to slayer3d_run_game.
         * @param alpha    Render interpolation factor in the range [0, 1].
         */
        void (*render)(slayer3d_game_context *ctx, void *userdata, float alpha);

        /**
         * @brief Called once after the loop exits and before engine cleanup.
         *
         * Free caller-owned assets, UI, levels, and gameplay state here. Engine
         * subsystems in ctx are still valid for the duration of this callback.
         *
         * @param ctx      Managed-loop context.
         * @param userdata Caller-owned pointer passed to slayer3d_run_game.
         */
        void (*shutdown)(slayer3d_game_context *ctx, void *userdata);
    } slayer3d_game_callbacks;

    /**
     * @brief Configuration for slayer3d_run_game.
     *
     * Zero-initialization selects sensible defaults. Audio is opt-in and
     * optional in the managed loop because many automated tests, tools, and
     * dedicated simulation loops run without a stable platform audio device.
     */
    typedef struct slayer3d_game_config
    {
        const char *title;  /**< Window title, or "SLAYER3D" when NULL. */
        int width;          /**< Initial window width in pixels, or logical width when <= 0. */
        int height;         /**< Initial window height in pixels, or logical height when <= 0. */
        int logical_width;  /**< Virtual render width, or 1280 when <= 0. */
        int logical_height; /**< Virtual render height, or 720 when <= 0. */
        slayer3d_logical_size_policy logical_size_policy; /**< Fixed or window-filling logical canvas. */
        const char *icon_path;                            /**< Optional filesystem path to a window icon image. */
        slayer3d_backend backend;                         /**< Requested backend, or SLAYER3D_BACKEND_AUTO when zero. */
        slayer3d_window_mode display_mode;    /**< Window presentation mode, or SLAYER3D default when zero. */
        int vsync;                            /**< >0 enables vsync, <0 disables vsync, 0 uses the SLAYER3D default. */
        int maximized;                        /**< >0 maximizes, <0 keeps window normal, 0 uses the SLAYER3D default. */
        int high_pixel_density;               /**< >0 requests high-DPI backing, <0 disables it, 0 uses default. */
        float tick_rate;                      /**< Fixed timestep in seconds, or 1/60 when <= 0. */
        int max_ticks_per_frame;              /**< Catch-up tick cap per rendered frame, or 8 when <= 0. */
        bool dynamic_world_render_scale;      /**< Enable adaptive 3D render resolution when true. */
        float dynamic_world_render_min_scale; /**< Minimum adaptive 3D render scale, or 0.5 when <= 0. */
        float dynamic_world_render_max_scale; /**< Maximum adaptive 3D render scale, or 1.0 when <= 0. */
        float dynamic_world_render_target_fps; /**< Adaptive render target FPS, or 60 when <= 0. */
        bool enable_audio;                     /**< When true, create session audio before init when available. */

        /**
         * @brief Hand per-frame control to the browser on Emscripten builds.
         *
         * When true, browser builds register the frame callback with
         * emscripten_set_main_loop_arg and slayer3d_run_game does not return;
         * shutdown and engine cleanup run from the callback once quit is
         * requested. Interactive browser apps should enable this so the page
         * stays responsive. When false, browser builds run the same blocking
         * loop as native, which freezes the tab until quit is requested and
         * is only appropriate for bounded runs such as automated tests.
         * Native builds ignore this field and always block until quit.
         */
        bool browser_main_loop;
    } slayer3d_game_config;

    /**
     * @brief Run a managed SLAYER3D game loop.
     *
     * Initializes SDL video, creates the window, render context, and a game
     * session with core services. It then runs a fixed-timestep simulation loop
     * until quit is requested. The loop processes SDL events through the
     * session input manager, updates input before pause callbacks and before
     * each fixed tick, updates timers before each fixed tick, calls
     * slayer3d_time_update once per rendered frame, updates audio once per rendered
     * frame when present, and presents the render context after each render
     * callback.
     *
     * @param config    Optional configuration. NULL selects all defaults.
     * @param callbacks Optional callback table. NULL treats all callbacks as no-ops.
     * @param userdata  Caller-owned pointer passed to every callback.
     * @return 0 on normal shutdown, 1 on startup or runtime failure.
     */
    int slayer3d_run_game(const slayer3d_game_config *config, const slayer3d_game_callbacks *callbacks, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_H */

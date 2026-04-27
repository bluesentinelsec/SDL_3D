/**
 * @file game_data.h
 * @brief JSON-authored game data runtime.
 *
 * The game data runtime loads an SDL3D game JSON file and instantiates its
 * generic composition primitives into an existing game session: actors, input
 * actions, signals, timers, sensors, and signal-to-action bindings.
 *
 * Game-specific behavior stays behind named adapters. JSON chooses where an
 * adapter is invoked and can bind it to a Lua function loaded from a script
 * next to the data file. Game code may also register native C callbacks for
 * adapters that need host integration or optimized native code.
 */

#ifndef SDL3D_GAME_DATA_H
#define SDL3D_GAME_DATA_H

#include <stdbool.h>

#include "sdl3d/actor_registry.h"
#include "sdl3d/game.h"
#include "sdl3d/properties.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct sdl3d_game_data_runtime sdl3d_game_data_runtime;

    /** @brief Authored game data diagnostic severity. */
    typedef enum sdl3d_game_data_diagnostic_severity
    {
        /** @brief Non-fatal issue that authors should review. */
        SDL3D_GAME_DATA_DIAGNOSTIC_WARNING = 1,
        /** @brief Fatal issue that prevents the data from loading. */
        SDL3D_GAME_DATA_DIAGNOSTIC_ERROR = 2,
    } sdl3d_game_data_diagnostic_severity;

    /**
     * @brief Callback for authored game data validation diagnostics.
     *
     * @p json_path is a best-effort JSON path to the authored object or field
     * that produced the diagnostic. @p message is a human-readable description
     * intended to be actionable without stepping through engine code.
     */
    typedef void (*sdl3d_game_data_diagnostic_fn)(void *userdata, sdl3d_game_data_diagnostic_severity severity,
                                                  const char *json_path, const char *message);

    /**
     * @brief Options controlling authored game data validation.
     */
    typedef struct sdl3d_game_data_validation_options
    {
        /** @brief Optional diagnostic callback. */
        sdl3d_game_data_diagnostic_fn diagnostic;
        /** @brief User pointer passed to @p diagnostic. */
        void *userdata;
        /** @brief When true, warnings also make validation fail. */
        bool treat_warnings_as_errors;
    } sdl3d_game_data_validation_options;

    /**
     * @brief Named game-specific callback invoked by JSON actions/components.
     *
     * @p adapter_name is the authored adapter name. @p target is the resolved
     * target actor when the JSON supplied one, otherwise NULL. @p payload is the
     * signal payload that caused the invocation for action adapters. Component
     * adapters receive a small authored payload, such as target_actor_name for
     * controller components.
     *
     * @return true when the adapter recognized and applied the request.
     */
    typedef bool (*sdl3d_game_data_adapter_fn)(void *userdata, sdl3d_game_data_runtime *runtime,
                                               const char *adapter_name, sdl3d_registered_actor *target,
                                               const sdl3d_properties *payload);

    /**
     * @brief Load a JSON game data file into a session.
     *
     * The session must provide an actor registry, signal bus, timer pool, and
     * input manager when the corresponding JSON sections are used. The runtime
     * owns the parsed JSON document and any signal bindings it installs; destroy
     * it before destroying the session services.
     *
     * @param path JSON file path.
     * @param session Target session whose services receive the authored data.
     * @param out_runtime Receives the created runtime on success.
     * @param error_buffer Optional buffer for a human-readable error.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true on success.
     */
    bool sdl3d_game_data_load_file(const char *path, sdl3d_game_session *session, sdl3d_game_data_runtime **out_runtime,
                                   char *error_buffer, int error_buffer_size);

    /**
     * @brief Validate a JSON game data file without instantiating runtime state.
     *
     * Validation checks schema, authored names, references, supported generic
     * logic primitives, script manifest structure, dependency cycles, and script
     * file existence. It can emit warnings for suspicious but non-fatal data,
     * such as unused adapters or unsupported component types.
     *
     * @param path JSON file path.
     * @param options Optional validation options and diagnostic callback.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when no fatal validation error was found.
     */
    bool sdl3d_game_data_validate_file(const char *path, const sdl3d_game_data_validation_options *options,
                                       char *error_buffer, int error_buffer_size);

    /**
     * @brief Destroy a loaded game data runtime.
     *
     * Disconnects installed signal handlers and frees the parsed document.
     * Session services and registered actors are not destroyed.
     */
    void sdl3d_game_data_destroy(sdl3d_game_data_runtime *runtime);

    /**
     * @brief Register a named native game-specific adapter callback.
     *
     * Re-registering a name replaces the callback and userdata. If the JSON file
     * declared a Lua function for the same adapter, the native callback becomes
     * the active implementation. The adapter name is copied by the runtime.
     */
    bool sdl3d_game_data_register_adapter(sdl3d_game_data_runtime *runtime, const char *name,
                                          sdl3d_game_data_adapter_fn callback, void *userdata);

    /**
     * @brief Advance JSON-authored controllers, motion, and sensors by one tick.
     *
     * Call after input is refreshed and before rendering. This updates generic
     * control/motion components, invokes controller adapters, evaluates sensors,
     * and emits any authored signals.
     */
    bool sdl3d_game_data_update(sdl3d_game_data_runtime *runtime, float dt);

    /** @brief Find an authored signal id by name, or -1 when missing. */
    int sdl3d_game_data_find_signal(const sdl3d_game_data_runtime *runtime, const char *name);

    /** @brief Find an authored input action id by name, or -1 when missing. */
    int sdl3d_game_data_find_action(const sdl3d_game_data_runtime *runtime, const char *name);

    /** @brief Find an authored actor by name in the runtime's session registry. */
    sdl3d_registered_actor *sdl3d_game_data_find_actor(const sdl3d_game_data_runtime *runtime, const char *name);

    /**
     * @brief Return the currently active authored camera name.
     *
     * The returned pointer is owned by the parsed JSON document and remains
     * valid until the runtime is destroyed.
     */
    const char *sdl3d_game_data_active_camera(const sdl3d_game_data_runtime *runtime);

    /**
     * @brief Return the dt currently being processed by sdl3d_game_data_update().
     *
     * Adapter callbacks can use this for per-frame controller behavior. Outside
     * an update call, this returns the most recent non-negative update dt.
     */
    float sdl3d_game_data_delta_time(const sdl3d_game_data_runtime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_GAME_DATA_H */

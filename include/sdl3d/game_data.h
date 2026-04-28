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
#include "sdl3d/asset.h"
#include "sdl3d/camera.h"
#include "sdl3d/effects.h"
#include "sdl3d/game.h"
#include "sdl3d/lighting.h"
#include "sdl3d/properties.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct sdl3d_game_data_runtime sdl3d_game_data_runtime;

    /** @brief Authored render primitive kind. */
    typedef enum sdl3d_game_data_render_primitive_type
    {
        /** @brief Axis-aligned cube/box primitive. */
        SDL3D_GAME_DATA_RENDER_CUBE = 1,
        /** @brief Sphere primitive. */
        SDL3D_GAME_DATA_RENDER_SPHERE = 2,
    } sdl3d_game_data_render_primitive_type;

    /**
     * @brief Read-only descriptor for an authored render primitive component.
     *
     * This is an engine-data description, not a draw call. Renderers or demos
     * may interpret these descriptors to draw simple generic primitives while
     * preserving renderer ownership outside the game data runtime.
     */
    typedef struct sdl3d_game_data_render_primitive
    {
        /** @brief Name of the entity that owns the component. */
        const char *entity_name;
        /** @brief Primitive type declared by the component. */
        sdl3d_game_data_render_primitive_type type;
        /** @brief Current world-space position from the owning actor plus optional component offset. */
        sdl3d_vec3 position;
        /** @brief Cube size for SDL3D_GAME_DATA_RENDER_CUBE. */
        sdl3d_vec3 size;
        /** @brief Sphere radius for SDL3D_GAME_DATA_RENDER_SPHERE. */
        float radius;
        /** @brief Sphere longitudinal slices for SDL3D_GAME_DATA_RENDER_SPHERE. */
        int slices;
        /** @brief Sphere latitudinal rings for SDL3D_GAME_DATA_RENDER_SPHERE. */
        int rings;
        /** @brief Authored tint color. */
        sdl3d_color color;
        /** @brief Whether the primitive should be treated as emissive by the caller. */
        bool emissive;
    } sdl3d_game_data_render_primitive;

    /**
     * @brief Callback for iterating authored render primitives.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*sdl3d_game_data_render_primitive_fn)(void *userdata,
                                                        const sdl3d_game_data_render_primitive *primitive);

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
     * @brief Load a JSON game data asset through a resolver.
     *
     * This is the preferred loading entry point for games that may ship data in
     * source directories, packed archives, or embedded packs. Script paths in
     * the JSON are resolved relative to @p asset_path through the same resolver.
     *
     * @param assets Resolver containing the JSON asset and referenced scripts.
     * @param asset_path Virtual path, such as asset://pong.game.json.
     * @param session Target session whose services receive the authored data.
     * @param out_runtime Receives the created runtime on success.
     * @param error_buffer Optional buffer for a human-readable error.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true on success.
     */
    bool sdl3d_game_data_load_asset(sdl3d_asset_resolver *assets, const char *asset_path, sdl3d_game_session *session,
                                    sdl3d_game_data_runtime **out_runtime, char *error_buffer, int error_buffer_size);

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
     * @brief Validate a JSON game data asset through a resolver.
     *
     * Validation reads the JSON and referenced script files from @p assets, so
     * authored data can be checked the same way whether it comes from a source
     * tree, packed archive, or embedded pack.
     *
     * @param assets Resolver containing the JSON asset and referenced scripts.
     * @param asset_path Virtual path, such as asset://pong.game.json.
     * @param options Optional validation options and diagnostic callback.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when no fatal validation error was found.
     */
    bool sdl3d_game_data_validate_asset(sdl3d_asset_resolver *assets, const char *asset_path,
                                        const sdl3d_game_data_validation_options *options, char *error_buffer,
                                        int error_buffer_size);

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
     * @brief Reload Lua scripts and rebind Lua adapters atomically.
     *
     * This development-time API reloads the runtime's script manifest through
     * @p assets, resolves all authored Lua adapter functions in a fresh Lua
     * state, and commits the new state only after the full reload succeeds.
     * When a script has a syntax error, returns the wrong type, is missing, or
     * no longer contains a referenced adapter function, the existing scripts and
     * adapter bindings remain active.
     *
     * Native adapters registered with sdl3d_game_data_register_adapter() remain
     * active and are not replaced by reloaded Lua functions.
     *
     * @param runtime Loaded game data runtime.
     * @param assets Resolver containing the updated script assets.
     * @param error_buffer Optional buffer for a human-readable error.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when scripts were reloaded and committed, or when the runtime
     * has no scripts to reload.
     */
    bool sdl3d_game_data_reload_scripts(sdl3d_game_data_runtime *runtime, sdl3d_asset_resolver *assets,
                                        char *error_buffer, int error_buffer_size);

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
     * @brief Read an authored non-adapter camera by name.
     *
     * Adapter cameras are game-specific and return false here because their
     * final pose is computed by game code or script. Orthographic cameras use
     * `size` as sdl3d_camera3d::fovy; perspective cameras use `fovy`.
     */
    bool sdl3d_game_data_get_camera(const sdl3d_game_data_runtime *runtime, const char *name,
                                    sdl3d_camera3d *out_camera);

    /**
     * @brief Read a numeric custom property from an authored camera.
     *
     * This lets games keep camera tuning data in JSON even when the camera pose
     * itself is adapter-driven.
     */
    bool sdl3d_game_data_get_camera_float(const sdl3d_game_data_runtime *runtime, const char *camera_name,
                                          const char *property_name, float *out_value);

    /** @brief Return the number of authored world lights. */
    int sdl3d_game_data_world_light_count(const sdl3d_game_data_runtime *runtime);

    /**
     * @brief Read the authored world ambient light color.
     *
     * Values are linear RGB in the same range expected by
     * sdl3d_set_ambient_light().
     */
    bool sdl3d_game_data_get_world_ambient_light(const sdl3d_game_data_runtime *runtime, float out_rgb[3]);

    /**
     * @brief Read an authored world light by zero-based index.
     *
     * The returned light is suitable for passing to sdl3d_add_light().
     */
    bool sdl3d_game_data_get_world_light(const sdl3d_game_data_runtime *runtime, int index, sdl3d_light *out_light);

    /**
     * @brief Iterate active authored render primitive components.
     *
     * Components currently supported by this iterator are `render.cube` and
     * `render.sphere`. Iteration skips inactive actors.
     */
    bool sdl3d_game_data_for_each_render_primitive(const sdl3d_game_data_runtime *runtime,
                                                   sdl3d_game_data_render_primitive_fn callback, void *userdata);

    /**
     * @brief Read an authored particle emitter component from an entity.
     *
     * The returned config is ready for sdl3d_create_particle_emitter(). Texture
     * references are intentionally not resolved here yet, so config.texture is
     * always NULL.
     */
    bool sdl3d_game_data_get_particle_emitter(const sdl3d_game_data_runtime *runtime, const char *entity_name,
                                              sdl3d_particle_config *out_config);

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

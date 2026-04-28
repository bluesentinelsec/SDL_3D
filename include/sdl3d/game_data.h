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
#include "sdl3d/font.h"
#include "sdl3d/game.h"
#include "sdl3d/lighting.h"
#include "sdl3d/properties.h"
#include "sdl3d/transition.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct sdl3d_game_data_runtime sdl3d_game_data_runtime;

    /** @brief Authored application lifecycle hooks. */
    typedef struct sdl3d_game_data_app_control
    {
        /** @brief Signal emitted by the host after game data has loaded, or -1. */
        int start_signal_id;
        /** @brief Input action that requests app quit, or -1. */
        int quit_action_id;
        /** @brief Input action that requests pause/unpause, or -1. */
        int pause_action_id;
        /** @brief Transition name to play at startup, or NULL. */
        const char *startup_transition;
        /** @brief Transition name to play before quit, or NULL. */
        const char *quit_transition;
        /** @brief Signal that means the app should quit immediately, or -1. */
        int quit_signal_id;
    } sdl3d_game_data_app_control;

    /** @brief Authored font asset descriptor. */
    typedef struct sdl3d_game_data_font_asset
    {
        /** @brief Stable asset id, such as `font.hud`. */
        const char *id;
        /** @brief Built-in font id when @p builtin is true. */
        sdl3d_builtin_font builtin_id;
        /** @brief True when this asset refers to an SDL3D built-in font. */
        bool builtin;
        /** @brief External font path when @p builtin is false, or NULL. */
        const char *path;
        /** @brief Requested font pixel size. */
        float size;
    } sdl3d_game_data_font_asset;

    /**
     * @brief Runtime metrics used when evaluating data-authored UI bindings.
     *
     * Games provide this small host-state snapshot each frame. The game data
     * runtime combines it with actor properties and active camera state to
     * resolve UI visibility and text content without game-specific string maps.
     */
    typedef struct sdl3d_game_data_ui_metrics
    {
        /** @brief Whether the managed loop is currently paused. */
        bool paused;
        /** @brief Most recently sampled frames per second. */
        float fps;
        /** @brief Number of rendered frames. */
        Uint64 frame;
    } sdl3d_game_data_ui_metrics;

    /** @brief Optional render evaluation inputs for dynamic visual effects. */
    typedef struct sdl3d_game_data_render_eval
    {
        /** @brief Elapsed presentation time in seconds, used by pulse effects. */
        float time;
    } sdl3d_game_data_render_eval;

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
        /** @brief Evaluated emissive RGB contribution. */
        sdl3d_vec3 emissive_color;
    } sdl3d_game_data_render_primitive;

    /**
     * @brief Callback for iterating authored render primitives.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*sdl3d_game_data_render_primitive_fn)(void *userdata,
                                                        const sdl3d_game_data_render_primitive *primitive);

    /** @brief Authored render setup that can be applied to a render context. */
    typedef struct sdl3d_game_data_render_settings
    {
        /** @brief Clear color for the frame. */
        sdl3d_color clear_color;
        /** @brief Whether 3D lighting should be enabled. */
        bool lighting_enabled;
        /** @brief Whether bloom post-processing should be enabled. */
        bool bloom_enabled;
        /** @brief Whether SSAO post-processing should be enabled. */
        bool ssao_enabled;
        /** @brief Tonemap operator for lit rendering. */
        sdl3d_tonemap_mode tonemap;
    } sdl3d_game_data_render_settings;

    /** @brief Authored transition effect descriptor. */
    typedef struct sdl3d_game_data_transition_desc
    {
        /** @brief Transition effect type. */
        sdl3d_transition_type type;
        /** @brief Transition direction. */
        sdl3d_transition_direction direction;
        /** @brief Transition color. */
        sdl3d_color color;
        /** @brief Duration in seconds. */
        float duration;
        /** @brief Signal emitted on completion, or -1. */
        int done_signal_id;
    } sdl3d_game_data_transition_desc;

    /** @brief Authored UI text descriptor. */
    typedef struct sdl3d_game_data_ui_text
    {
        /** @brief Stable UI item name. */
        const char *name;
        /** @brief Font asset id. */
        const char *font;
        /** @brief Literal text, or NULL when @p format is used. */
        const char *text;
        /** @brief Format string interpreted by the caller. */
        const char *format;
        /** @brief Caller-defined source key for dynamic text. */
        const char *source;
        /** @brief Caller-defined visibility key. */
        const char *visible;
        /** @brief Horizontal position. For centered text, this is a normalized y-independent coordinate. */
        float x;
        /** @brief Vertical position. */
        float y;
        /** @brief Whether x/y are normalized to the current render size. */
        bool normalized;
        /** @brief Whether the text should be horizontally centered by the caller. */
        bool centered;
        /** @brief Whether alpha should pulse while visible. */
        bool pulse_alpha;
        /** @brief Text color. */
        sdl3d_color color;
    } sdl3d_game_data_ui_text;

    /**
     * @brief Callback for iterating authored UI text descriptors.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*sdl3d_game_data_ui_text_fn)(void *userdata, const sdl3d_game_data_ui_text *text);

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
     * @brief Read the managed-loop config authored in a JSON game data asset.
     *
     * This lightweight reader is intended for startup, before a managed loop
     * creates a window or game session. Missing fields keep the values already
     * present in @p out_config, so callers can initialize defaults first. When
     * an authored title is present, it is copied into @p title_buffer and
     * out_config->title points at that buffer.
     */
    bool sdl3d_game_data_load_app_config_asset(sdl3d_asset_resolver *assets, const char *asset_path,
                                               sdl3d_game_config *out_config, char *title_buffer, int title_buffer_size,
                                               char *error_buffer, int error_buffer_size);

    /**
     * @brief Read the managed-loop config authored in a JSON game data file.
     *
     * Missing fields keep the values already present in @p out_config, so
     * callers can initialize defaults first. When an authored title is present,
     * it is copied into @p title_buffer and out_config->title points at that
     * buffer.
     */
    bool sdl3d_game_data_load_app_config_file(const char *path, sdl3d_game_config *out_config, char *title_buffer,
                                              int title_buffer_size, char *error_buffer, int error_buffer_size);

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

    /** @brief Find the first authored actor whose entity data contains @p tag. */
    sdl3d_registered_actor *sdl3d_game_data_find_actor_with_tag(const sdl3d_game_data_runtime *runtime,
                                                                const char *tag);

    /**
     * @brief Find the first authored actor whose entity data contains every tag.
     *
     * Tags are matched against the entity's `tags` array in the loaded JSON
     * document. This lets game code request roles like `{"paddle", "player"}`
     * without depending on exact entity names.
     */
    sdl3d_registered_actor *sdl3d_game_data_find_actor_with_tags(const sdl3d_game_data_runtime *runtime,
                                                                 const char *const *tags, int tag_count);

    /**
     * @brief Read data-authored application lifecycle hooks.
     *
     * Missing fields return neutral values: signal/action ids are -1 and
     * transition names are NULL.
     */
    bool sdl3d_game_data_get_app_control(const sdl3d_game_data_runtime *runtime,
                                         sdl3d_game_data_app_control *out_control);

    /** @brief Read a font asset descriptor by id from `assets.fonts`. */
    bool sdl3d_game_data_get_font_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                        sdl3d_game_data_font_asset *out_font);

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
     * @brief Iterate active authored render primitives with dynamic effects evaluated.
     *
     * This applies generic `effects` authored on render primitive components,
     * such as property-driven flash colors, size offsets, and time-driven
     * pulses. Passing NULL for @p eval uses a zeroed evaluation context.
     */
    bool sdl3d_game_data_for_each_render_primitive_evaluated(const sdl3d_game_data_runtime *runtime,
                                                             const sdl3d_game_data_render_eval *eval,
                                                             sdl3d_game_data_render_primitive_fn callback,
                                                             void *userdata);

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
     * @brief Read optional draw-time emissive color for a particle emitter entity.
     *
     * The color is read from the emitter component's `draw_emissive` field and
     * defaults to zero when not authored.
     */
    bool sdl3d_game_data_get_particle_emitter_draw_emissive(const sdl3d_game_data_runtime *runtime,
                                                            const char *entity_name, sdl3d_vec3 *out_rgb);

    /**
     * @brief Read authored render setup.
     *
     * Missing fields produce conservative defaults: black clear color, lighting
     * enabled, bloom/SSAO enabled, and ACES tonemapping.
     */
    bool sdl3d_game_data_get_render_settings(const sdl3d_game_data_runtime *runtime,
                                             sdl3d_game_data_render_settings *out_settings);

    /**
     * @brief Read a named authored transition descriptor.
     *
     * @p name is looked up under the top-level `transitions` object.
     */
    bool sdl3d_game_data_get_transition(const sdl3d_game_data_runtime *runtime, const char *name,
                                        sdl3d_game_data_transition_desc *out_transition);

    /** @brief Iterate authored UI text descriptors. */
    bool sdl3d_game_data_for_each_ui_text(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_ui_text_fn callback,
                                          void *userdata);

    /**
     * @brief Evaluate a UI text descriptor's authored visibility condition.
     *
     * Supports camera-active checks, app pause checks, actor property
     * comparisons, and boolean all/any/not composition. Descriptors without a
     * condition are visible.
     */
    bool sdl3d_game_data_ui_text_is_visible(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_text *text,
                                            const sdl3d_game_data_ui_metrics *metrics);

    /**
     * @brief Resolve UI text content from data-authored bindings.
     *
     * Literal `text` entries are copied directly. Entries with `bindings`
     * resolve engine metrics and actor properties, then format them using the
     * descriptor's `format` string.
     */
    bool sdl3d_game_data_format_ui_text(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_text *text,
                                        const sdl3d_game_data_ui_metrics *metrics, char *buffer, size_t buffer_size);

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

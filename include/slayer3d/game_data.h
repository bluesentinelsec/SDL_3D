/**
 * @file game_data.h
 * @brief JSON-authored game data runtime.
 *
 * The game data runtime loads an SLAYER3D game JSON file and instantiates its
 * generic composition primitives into an existing game session: actors, input
 * actions, signals, timers, sensors, and signal-to-action bindings.
 *
 * Game-specific behavior stays behind named adapters. JSON chooses where an
 * adapter is invoked and can bind it to a Lua function loaded from a script
 * next to the data file. Game code may also register native C callbacks for
 * adapters that need host integration or optimized native code.
 */

#ifndef SLAYER3D_GAME_DATA_H
#define SLAYER3D_GAME_DATA_H

#include <stdbool.h>

#include "slayer3d/actor_registry.h"
#include "slayer3d/asset.h"
#include "slayer3d/camera.h"
#include "slayer3d/effects.h"
#include "slayer3d/font.h"
#include "slayer3d/game.h"
#include "slayer3d/game_data_app.h"
#include "slayer3d/game_data_assets.h"
#include "slayer3d/game_data_brush.h"
#include "slayer3d/game_data_defaults.h"
#include "slayer3d/game_data_editor.h"
#include "slayer3d/game_data_editor_metadata.h"
#include "slayer3d/game_data_editor_runtime.h"
#include "slayer3d/game_data_network.h"
#include "slayer3d/game_data_network_runtime.h"
#include "slayer3d/game_data_render.h"
#include "slayer3d/game_data_scene.h"
#include "slayer3d/game_data_ui.h"
#include "slayer3d/game_data_world.h"
#include "slayer3d/game_data_world_model.h"
#include "slayer3d/level.h"
#include "slayer3d/lighting.h"
#include "slayer3d/model.h"
#include "slayer3d/network.h"
#include "slayer3d/network_replication.h"
#include "slayer3d/properties.h"
#include "slayer3d/render_context.h"
#include "slayer3d/sprite_asset.h"
#include "slayer3d/storage.h"
#include "slayer3d/transition.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

    /**
     * @brief Callback for active authored brush world instances.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_brush_world_instance_fn)(void *userdata,
                                                               const slayer3d_game_data_brush_world_instance *instance);

    /**
     * @brief Callback for active authored world model instances.
     *
     * Return false to stop iteration early. The descriptor and all nested
     * pointers are valid only for the duration of the callback; strings and
     * native world pointers are runtime-owned.
     */
    typedef bool (*slayer3d_game_data_world_model_instance_fn)(void *userdata,
                                                               const slayer3d_game_data_world_model_instance *instance);

    /**
     * @brief Persistent state bag shared across authored scene changes.
     *
     * Scene-transition payloads are transient and exist only while the target
     * scene's enter signal is emitted. This runtime-owned bag is the durable
     * handoff point for data that should survive after the transition, such as
     * selected character, level index, difficulty, or inventory snapshot ids.
     *
     * The returned pointer is owned by @p runtime and remains valid until the
     * runtime is destroyed. Callers may mutate it with the normal
     * slayer3d_properties setters.
     */
    slayer3d_properties *slayer3d_game_data_mutable_scene_state(slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read the persistent scene-state bag.
     *
     * @see slayer3d_game_data_mutable_scene_state
     */
    const slayer3d_properties *slayer3d_game_data_scene_state(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Look up a JSON-authored sector level by name.
     *
     * This exposes loaded and built sector-world data to renderer,
     * controller, sensor, and editor systems without making callers parse
     * JSON. The returned pointers are runtime-owned.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored sector level name.
     * @param out_level Receives runtime-owned sector level pointers.
     * @return true when @p name resolves to an authored sector level.
     */
    bool slayer3d_game_data_get_sector_level(const slayer3d_game_data_runtime *runtime, const char *name,
                                             slayer3d_game_data_sector_level *out_level);

    /**
     * @brief Look up a JSON-authored brush world by name.
     *
     * This exposes loaded native brush-world data to renderer, collision,
     * controller, sensor, and editor systems without making callers parse JSON.
     * The returned pointers are runtime-owned.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored brush world name.
     * @param out_world Receives runtime-owned brush world pointers.
     * @return true when @p name resolves to an authored brush world.
     */
    bool slayer3d_game_data_get_brush_world(const slayer3d_game_data_runtime *runtime, const char *name,
                                            slayer3d_game_data_brush_world *out_world);

    /**
     * @brief Trace a point, sphere, or AABB through one named brush world.
     *
     * Coordinates are local to the named brush world. Use
     * slayer3d_game_data_trace_active_brush_worlds() for active-scene
     * world-space queries that honor `world.brush_worlds` placement.
     *
     * The function tests only brushes whose `contents` overlap
     * `desc.contents_mask`. It returns true for a valid query regardless of
     * whether a hit occurred; inspect `out_result.hit`.
     */
    bool slayer3d_game_data_trace_brush_world(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                              const slayer3d_game_data_brush_trace_desc *desc,
                                              slayer3d_game_data_brush_trace_result *out_result);

    /**
     * @brief Trace a point, sphere, or AABB through brush worlds in the active scene.
     *
     * Input and output positions are world-space. The closest hit across all
     * active-scene brush world instances is returned.
     */
    bool slayer3d_game_data_trace_active_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                                      const slayer3d_game_data_brush_trace_desc *desc,
                                                      slayer3d_game_data_brush_trace_result *out_result);

    /**
     * @brief Move through one named brush world while sliding along hit planes.
     *
     * This is a thin deterministic helper over brush traces. It repeatedly
     * traces the requested movement, projects the remaining motion along the
     * blocking plane, and returns the final non-penetrating end position in
     * `out_result.end_position`. The first blocking hit metadata is preserved.
     */
    bool slayer3d_game_data_slide_brush_world(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                              const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                              slayer3d_game_data_brush_trace_result *out_result);

    /**
     * @brief Active-scene world-space variant of slayer3d_game_data_slide_brush_world().
     */
    bool slayer3d_game_data_slide_active_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                                      const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                                      slayer3d_game_data_brush_trace_result *out_result);

    /**
     * @brief Copy accumulated brush-world trace diagnostics.
     *
     * Diagnostics are cumulative until reset and are intended for tests,
     * debug UI, and future editor instrumentation.
     */
    bool slayer3d_game_data_get_brush_diagnostics(const slayer3d_game_data_runtime *runtime,
                                                  slayer3d_game_data_brush_diagnostics *out_diagnostics);

    /** @brief Reset accumulated brush-world trace diagnostics to zero. */
    void slayer3d_game_data_reset_brush_diagnostics(slayer3d_game_data_runtime *runtime);

    /** @brief Add render-context stat deltas to accumulated brush diagnostics. */
    void slayer3d_game_data_accumulate_brush_render_diagnostics(slayer3d_game_data_runtime *runtime,
                                                                const slayer3d_render_stats *before,
                                                                const slayer3d_render_stats *after);

    /**
     * @brief Find the nearest authored navigation node in a sector navigation graph.
     *
     * If @p position is inside the graph's sector level, nodes in that sector
     * are preferred. If no same-sector node exists, the nearest node in the
     * graph is returned. The returned node name is runtime-owned.
     */
    bool slayer3d_game_data_sector_nav_nearest_node(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                                    slayer3d_vec3 position,
                                                    slayer3d_game_data_sector_nav_node *out_node);

    /**
     * @brief Resolve an authored sector navigation path between two world positions.
     *
     * The start and goal positions are first anchored to nearest nodes in the
     * graph, then Dijkstra search is run across authored links. @p out_nodes
     * may be NULL when the caller only needs @p out_node_count or @p out_cost.
     * When @p out_nodes is non-NULL, @p max_nodes must be large enough for the
     * full path or the function returns false.
     */
    bool slayer3d_game_data_sector_nav_path(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                            slayer3d_vec3 start, slayer3d_vec3 goal,
                                            slayer3d_game_data_sector_nav_node *out_nodes, int max_nodes,
                                            int *out_node_count, float *out_cost);

    /**
     * @brief Return whether an authored sector navigation path exists between two positions.
     */
    bool slayer3d_game_data_sector_nav_path_available(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                                      slayer3d_vec3 start, slayer3d_vec3 goal);

    /**
     * @brief Resolve the next node after the start anchor on a sector navigation path.
     *
     * If the start and goal anchor to the same node, that node is returned.
     */
    bool slayer3d_game_data_sector_nav_next_node(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                                 slayer3d_vec3 start, slayer3d_vec3 goal,
                                                 slayer3d_game_data_sector_nav_node *out_node);

    /**
     * @brief Read authored or runtime-edited sector-local lighting.
     *
     * @p sector may be either a sector name or a decimal sector index. Returns
     * false when the level/sector cannot be resolved, when the sector has no
     * authored lighting, or when the output pointers are invalid. @p out_color
     * receives RGB tint plus alpha/influence in [0, 1].
     */
    bool slayer3d_game_data_get_sector_lighting(const slayer3d_game_data_runtime *runtime, const char *sector_level,
                                                const char *sector, float *out_level, float out_color[4],
                                                char *error_buffer, int error_buffer_size);

    /**
     * @brief Set sector-local lighting and rebuild the level's render variants.
     *
     * @p sector may be either a sector name or a decimal sector index. @p level
     * is clamped to [0, 255]. @p color is clamped per channel to [0, 1] and
     * uses color[3] as tint influence, not transparency. The update is atomic:
     * if rebuilding fails, the previous runtime level data remains active.
     */
    bool slayer3d_game_data_set_sector_lighting(slayer3d_game_data_runtime *runtime, const char *sector_level,
                                                const char *sector, float level, const float color[4],
                                                char *error_buffer, int error_buffer_size);

    /**
     * @brief Iterate sector levels declared by the active scene.
     *
     * The active scene owns placement and variant selection through
     * `world.sector_levels`. This helper is renderer-agnostic so tests,
     * editors, and custom hosts can inspect the same resolved instances that
     * the generic presentation layer draws.
     */
    bool slayer3d_game_data_for_each_sector_level_instance(const slayer3d_game_data_runtime *runtime,
                                                           slayer3d_game_data_sector_level_instance_fn callback,
                                                           void *userdata);

    /**
     * @brief Iterate brush worlds declared by the active scene.
     *
     * The active scene owns placement and debug policy through
     * `world.brush_worlds`. This helper is renderer-agnostic so tests,
     * editors, and future brush render/collision systems can inspect the same
     * resolved instances.
     */
    bool slayer3d_game_data_for_each_brush_world_instance(const slayer3d_game_data_runtime *runtime,
                                                          slayer3d_game_data_brush_world_instance_fn callback,
                                                          void *userdata);

    /**
     * @brief Iterate all active scene world model instances through a common descriptor.
     *
     * This is the editor/tooling-facing enumeration layer over sector and
     * brush worlds. It allows tools to inspect placement, bounds, and stable
     * world references without branching over the authored scene JSON shape.
     */
    bool slayer3d_game_data_for_each_world_model_instance(const slayer3d_game_data_runtime *runtime,
                                                          slayer3d_game_data_world_model_instance_fn callback,
                                                          void *userdata);

    /**
     * @brief Trace through active scene world model instances.
     *
     * Brush worlds use the existing brush trace implementation. Sector levels
     * currently support point traces that detect exiting sector volume; shaped
     * traces are ignored for sector models and still evaluated against brush
     * models when included by @p desc.model_filter.
     */
    bool slayer3d_game_data_trace_world_models(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_world_trace_desc *desc,
                                               slayer3d_game_data_world_trace_result *out_result);

    /** @brief Authored game data diagnostic severity. */
    typedef enum slayer3d_game_data_diagnostic_severity
    {
        /** @brief Non-fatal issue that authors should review. */
        SLAYER3D_GAME_DATA_DIAGNOSTIC_WARNING = 1,
        /** @brief Fatal issue that prevents the data from loading. */
        SLAYER3D_GAME_DATA_DIAGNOSTIC_ERROR = 2,
    } slayer3d_game_data_diagnostic_severity;

    /**
     * @brief Callback for authored game data validation diagnostics.
     *
     * @p json_path is a best-effort JSON path to the authored object or field
     * that produced the diagnostic. @p message is a human-readable description
     * intended to be actionable without stepping through engine code.
     */
    typedef void (*slayer3d_game_data_diagnostic_fn)(void *userdata, slayer3d_game_data_diagnostic_severity severity,
                                                     const char *json_path, const char *message);

    /**
     * @brief Options controlling authored game data validation.
     */
    typedef struct slayer3d_game_data_validation_options
    {
        /** @brief Optional diagnostic callback. */
        slayer3d_game_data_diagnostic_fn diagnostic;
        /** @brief User pointer passed to @p diagnostic. */
        void *userdata;
        /** @brief When true, warnings also make validation fail. */
        bool treat_warnings_as_errors;
    } slayer3d_game_data_validation_options;

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
    typedef bool (*slayer3d_game_data_adapter_fn)(void *userdata, slayer3d_game_data_runtime *runtime,
                                                  const char *adapter_name, slayer3d_registered_actor *target,
                                                  const slayer3d_properties *payload);

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
    bool slayer3d_game_data_load_file(const char *path, slayer3d_game_session *session,
                                      slayer3d_game_data_runtime **out_runtime, char *error_buffer,
                                      int error_buffer_size);

    /**
     * @brief Optional game-data load-time overrides.
     *
     * Hosts normally load the authored `scenes.initial` scene and emit that
     * scene's enter signal. Development tools and editors can provide
     * `initial_scene_override` to enter a different scene before any enter
     * signal fires. `initial_scene_state` is copied into the persistent
     * scene-state bag before the first enter signal; `initial_scene_payload`
     * is passed only to that initial scene-enter signal. `initial_player_start`
     * applies an editor-authored player start before camera setup and the first
     * enter signal; when it has a scene and no explicit scene override is set,
     * that scene becomes the initial scene.
     */
    typedef struct slayer3d_game_data_load_options
    {
        /** @brief Game session that receives authored signals, timers, and input bindings. Required. */
        slayer3d_game_session *session;
        /** @brief Optional authored scene name to enter instead of `scenes.initial`. */
        const char *initial_scene_override;
        /** @brief Optional persistent scene-state values copied before first scene enter. */
        const slayer3d_properties *initial_scene_state;
        /** @brief Optional transient payload passed to the first scene-enter signal. */
        const slayer3d_properties *initial_scene_payload;
        /** @brief Optional editor player start to apply for direct test-run workflows. */
        const char *initial_player_start;
    } slayer3d_game_data_load_options;

    /**
     * @brief Load a JSON game data asset through a resolver.
     *
     * This is the preferred loading entry point for games that may ship data in
     * source directories, packed archives, or embedded packs. Script paths in
     * the JSON are resolved relative to @p asset_path through the same resolver.
     * The runtime borrows @p assets for later runtime asset actions, so callers
     * must keep the resolver alive until the runtime is destroyed.
     *
     * @param assets Resolver containing the JSON asset and referenced scripts.
     * @param asset_path Virtual path, such as asset://pong.game.json.
     * @param session Target session whose services receive the authored data.
     * @param out_runtime Receives the created runtime on success.
     * @param error_buffer Optional buffer for a human-readable error.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true on success.
     */
    bool slayer3d_game_data_load_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                       slayer3d_game_session *session, slayer3d_game_data_runtime **out_runtime,
                                       char *error_buffer, int error_buffer_size);

    /**
     * @brief Load a JSON game data asset through a resolver with load-time overrides.
     *
     * This uses the same resolver and ownership rules as
     * @ref slayer3d_game_data_load_asset, with the additional ability to choose
     * the first active scene and seed scene state before any scene-enter signal
     * runs.
     */
    bool slayer3d_game_data_load_asset_with_options(slayer3d_asset_resolver *assets, const char *asset_path,
                                                    const slayer3d_game_data_load_options *options,
                                                    slayer3d_game_data_runtime **out_runtime, char *error_buffer,
                                                    int error_buffer_size);

    /**
     * @brief Read the managed-loop config authored in a JSON game data asset.
     *
     * This lightweight reader is intended for startup, before a managed loop
     * creates a window or game session. Missing fields keep the values already
     * present in @p out_config, so callers can initialize defaults first. When
     * an authored title is present, it is copied into @p title_buffer and
     * out_config->title points at that buffer.
     */
    bool slayer3d_game_data_load_app_config_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                                  slayer3d_game_config *out_config, char *title_buffer,
                                                  int title_buffer_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Read the managed-loop config authored in a JSON game data file.
     *
     * Missing fields keep the values already present in @p out_config, so
     * callers can initialize defaults first. When an authored title is present,
     * it is copied into @p title_buffer and out_config->title points at that
     * buffer.
     */
    bool slayer3d_game_data_load_app_config_file(const char *path, slayer3d_game_config *out_config, char *title_buffer,
                                                 int title_buffer_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Get the writable storage identity authored by the game data.
     *
     * The returned config is suitable for slayer3d_storage_create(). String
     * pointers are owned by @p runtime and remain valid until
     * slayer3d_game_data_destroy(). If the JSON omits the storage block, SLAYER3D
     * derives conservative defaults from metadata/app fields and finally falls
     * back to slayer3d_storage_config_init() defaults.
     *
     * @param runtime Loaded game data runtime.
     * @param out_config Receives the resolved storage configuration.
     * @return true when @p out_config was filled.
     */
    bool slayer3d_game_data_get_storage_config(const slayer3d_game_data_runtime *runtime,
                                               slayer3d_storage_config *out_config);

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
    bool slayer3d_game_data_validate_file(const char *path, const slayer3d_game_data_validation_options *options,
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
    bool slayer3d_game_data_validate_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                           const slayer3d_game_data_validation_options *options, char *error_buffer,
                                           int error_buffer_size);

    /**
     * @brief Destroy a loaded game data runtime.
     *
     * Disconnects installed signal handlers and frees the parsed document.
     * Session services and registered actors are not destroyed.
     */
    void slayer3d_game_data_destroy(slayer3d_game_data_runtime *runtime);

    /**
     * @brief Register a named native game-specific adapter callback.
     *
     * Re-registering a name replaces the callback and userdata. If the JSON file
     * declared a Lua function for the same adapter, the native callback becomes
     * the active implementation. The adapter name is copied by the runtime.
     */
    bool slayer3d_game_data_register_adapter(slayer3d_game_data_runtime *runtime, const char *name,
                                             slayer3d_game_data_adapter_fn callback, void *userdata);

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
     * Native adapters registered with slayer3d_game_data_register_adapter() remain
     * active and are not replaced by reloaded Lua functions.
     *
     * @param runtime Loaded game data runtime.
     * @param assets Resolver containing the updated script assets.
     * @param error_buffer Optional buffer for a human-readable error.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when scripts were reloaded and committed, or when the runtime
     * has no scripts to reload.
     */
    bool slayer3d_game_data_reload_scripts(slayer3d_game_data_runtime *runtime, slayer3d_asset_resolver *assets,
                                           char *error_buffer, int error_buffer_size);

    /**
     * @brief Advance JSON-authored controllers, motion, and sensors by one tick.
     *
     * Call after input is refreshed and before rendering. This updates generic
     * control/motion components, invokes controller adapters, evaluates sensors,
     * and emits any authored signals.
     */
    bool slayer3d_game_data_update(slayer3d_game_data_runtime *runtime, float dt);

    /** @brief Find an authored signal id by name, or -1 when missing. */
    int slayer3d_game_data_find_signal(const slayer3d_game_data_runtime *runtime, const char *name);

    /** @brief Find an authored input action id by name, or -1 when missing. */
    int slayer3d_game_data_find_action(const slayer3d_game_data_runtime *runtime, const char *name);

    /** @brief Find an authored actor by name in the runtime's session registry. */
    slayer3d_registered_actor *slayer3d_game_data_find_actor(const slayer3d_game_data_runtime *runtime,
                                                             const char *name);

    /** @brief Find the first authored actor whose entity data contains @p tag. */
    slayer3d_registered_actor *slayer3d_game_data_find_actor_with_tag(const slayer3d_game_data_runtime *runtime,
                                                                      const char *tag);

    /**
     * @brief Find the first authored actor whose entity data contains every tag.
     *
     * Tags are matched against the entity's `tags` array in the loaded JSON
     * document. This lets game code request roles like `{"paddle", "player"}`
     * without depending on exact entity names.
     */
    slayer3d_registered_actor *slayer3d_game_data_find_actor_with_tags(const slayer3d_game_data_runtime *runtime,
                                                                       const char *const *tags, int tag_count);

    /**
     * @brief Read data-authored application lifecycle hooks.
     *
     * Missing fields return neutral values: signal/action ids are -1 and
     * transition names are NULL.
     */
    bool slayer3d_game_data_get_app_control(const slayer3d_game_data_runtime *runtime,
                                            slayer3d_game_data_app_control *out_control);

    /**
     * @brief Return whether an authored signal should apply live window settings.
     *
     * Games declare these signals under `app.window.apply_signal` or
     * `app.window.apply_signals`. This lets reusable menu controls apply display
     * mode, renderer, and V-sync changes immediately without hard-coding menu
     * names in the host.
     */
    bool slayer3d_game_data_app_signal_applies_window_settings(const slayer3d_game_data_runtime *runtime,
                                                               int signal_id);

    /**
     * @brief Evaluate the data-authored app pause condition.
     *
     * Returns true when `app.pause.allowed_if` is absent. When present, the
     * condition uses the same generic condition language as UI visibility:
     * actor property comparisons, app pause checks, camera checks, and
     * all/any/not composition. @p metrics may be NULL when the condition does
     * not refer to app metrics.
     */
    bool slayer3d_game_data_app_pause_allowed(const slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_ui_metrics *metrics);

    /**
     * @brief Advance data-authored presentation clocks.
     *
     * Presentation clocks are generic data-driven oscillators/counters used by
     * UI, lights, and other render-facing effects. Authored clocks may write
     * into actor properties so scripts, UI bindings, and render evaluation can
     * share the same source of truth.
     */
    bool slayer3d_game_data_update_presentation_clocks(slayer3d_game_data_runtime *runtime, float dt, bool paused,
                                                       bool pause_entered);

    /**
     * @brief Advance the active scene's authored input-activity controller.
     *
     * Scenes may author an `activity` object to drive reusable attract-mode,
     * kiosk, title-screen, or cutscene overlays. The controller tracks the
     * active scene, detects input activity, emits `on_enter`, `on_idle`,
     * `on_active`, and `periodic` action lists, and keeps behavior in game
     * data instead of host glue.
     *
     * Supported activity input modes are `any`, `action`, and `disabled`.
     * Periodic entries may reset idle time so data can temporarily reveal UI
     * prompts before allowing them to fade away again.
     *
     * @param runtime Loaded game data runtime.
     * @param input Current input manager, or NULL when input activity should be ignored.
     * @param dt Delta time in seconds.
     * @return true when authored activity actions completed successfully.
     */
    bool slayer3d_game_data_update_scene_activity(slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_input_manager *input, float dt);

    /**
     * @brief Return whether the active scene activity should consume wake input.
     *
     * This is a query-only helper for app-flow/menu controllers. When a scene
     * has entered its authored idle state and matching input is pressed, data
     * may request that the current input be used only to wake the scene's
     * activity controller. The next scene-activity update will run `on_active`.
     *
     * @param runtime Loaded game data runtime.
     * @param input Current input manager.
     * @param out_block_menus Optional output set when menu input should be blocked.
     * @param out_block_scene_shortcuts Optional output set when scene shortcuts should be blocked.
     * @return true when the matching wake input should be consumed for this frame.
     */
    bool slayer3d_game_data_scene_activity_consumes_wake_input(const slayer3d_game_data_runtime *runtime,
                                                               const slayer3d_input_manager *input,
                                                               bool *out_block_menus, bool *out_block_scene_shortcuts);

    /**
     * @brief Read the authored UI pulse phase.
     *
     * Returns @p fallback when no `presentation.ui_pulse_clock` is authored or
     * the named clock has no current value.
     */
    float slayer3d_game_data_ui_pulse_phase(const slayer3d_game_data_runtime *runtime, float fallback);

    /**
     * @brief Read authored FPS metric sample duration in seconds.
     */
    float slayer3d_game_data_fps_sample_seconds(const slayer3d_game_data_runtime *runtime, float fallback);

    /** @brief Read a font asset descriptor by id from `assets.fonts`. */
    bool slayer3d_game_data_get_font_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                           slayer3d_game_data_font_asset *out_font);

    /** @brief Read an image asset descriptor by id from `assets.images`. */
    bool slayer3d_game_data_get_image_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_image_asset *out_image);

    /** @brief Read a 3D model asset descriptor by id from `assets.models`. */
    bool slayer3d_game_data_get_model_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_model_asset *out_model);

    /** @brief Read a sound-effect asset descriptor by id from `assets.sounds`. */
    bool slayer3d_game_data_get_sound_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_sound_asset *out_sound);

    /** @brief Read a music asset descriptor by id from `assets.music`. */
    bool slayer3d_game_data_get_music_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_music_asset *out_music);

    /** @brief Read an ambient-zone asset descriptor by id from `assets.ambient`. */
    bool slayer3d_game_data_get_ambient_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                              slayer3d_game_data_ambient_asset *out_ambient);

    /** @brief Read a sprite asset descriptor by id from `assets.sprites`. */
    bool slayer3d_game_data_get_sprite_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                             slayer3d_game_data_sprite_asset *out_sprite);

    /**
     * @brief Load a sprite asset by id from `assets.sprites`.
     *
     * The runtime looks up the authored sprite descriptor, resolves the
     * source image through the game's asset resolver, and builds billboard
     * textures plus rotation sets ready for sprite actors.
     */
    bool slayer3d_game_data_load_sprite_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                              slayer3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                              int error_buffer_size);

    /**
     * @brief Resolve an authored audio path to a filesystem path usable by audio backends.
     *
     * Resolver-backed assets such as `asset://audio/title.ogg` are materialized
     * into the runtime's `cache://audio` storage root. Plain filesystem paths
     * are resolved relative to the loaded game data file and returned without
     * copying. The returned path is copied into @p out_path and remains valid
     * independently of the runtime.
     *
     * @param runtime Loaded game data runtime.
     * @param path Authored audio path or asset URI.
     * @param out_path Buffer that receives the filesystem path.
     * @param out_path_size Size of @p out_path in bytes.
     * @return true when the path was resolved and copied.
     */
    bool slayer3d_game_data_prepare_audio_file(slayer3d_game_data_runtime *runtime, const char *path, char *out_path,
                                               int out_path_size);

    /**
     * @brief Return the currently active authored camera name.
     *
     * The returned pointer is owned by the parsed JSON document and remains
     * valid until the runtime is destroyed.
     */
    const char *slayer3d_game_data_active_camera(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read an authored non-adapter camera by name.
     *
     * Adapter cameras are game-specific and return false here because their
     * final pose is computed by game code or script. Orthographic cameras use
     * `size` as slayer3d_camera3d::fovy; perspective cameras use `fov` or the
     * legacy `fovy` field with an optional `fov_axis`.
     */
    bool slayer3d_game_data_get_camera(const slayer3d_game_data_runtime *runtime, const char *name,
                                       slayer3d_camera3d *out_camera);

    /**
     * @brief Read a numeric custom property from an authored camera.
     *
     * This lets games keep camera tuning data in JSON even when the camera pose
     * itself is adapter-driven.
     */
    bool slayer3d_game_data_get_camera_float(const slayer3d_game_data_runtime *runtime, const char *camera_name,
                                             const char *property_name, float *out_value);

    /**
     * @brief Read the authored world unit convention.
     *
     * Games may omit these fields; SLAYER3D then reports the engine default:
     * units="meters" and meters_per_unit=1.0. This is metadata for tools,
     * editors, physics tuning, and documentation. The renderer interprets all
     * authored positions consistently as world units.
     */
    bool slayer3d_game_data_get_world_units(const slayer3d_game_data_runtime *runtime, const char **out_units,
                                            float *out_meters_per_unit);

    /** @brief Return the number of authored world lights. */
    int slayer3d_game_data_world_light_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read the authored world ambient light color.
     *
     * Values are linear RGB in the same range expected by
     * slayer3d_set_ambient_light().
     */
    bool slayer3d_game_data_get_world_ambient_light(const slayer3d_game_data_runtime *runtime, float out_rgb[3]);

    /**
     * @brief Read an authored world light by zero-based index.
     *
     * The returned light is suitable for passing to slayer3d_add_light(). Lights
     * may target one entity with `target_entity`, or the first active-scene
     * entity in an ordered `target_entities` fallback list.
     */
    bool slayer3d_game_data_get_world_light(const slayer3d_game_data_runtime *runtime, int index,
                                            slayer3d_light *out_light);

    /**
     * @brief Read an authored world light with generic visual effects evaluated.
     *
     * Supported light effects include `pulse`, `color_cycle`, and `flash`,
     * allowing data to drive color blends, intensity changes, and range changes
     * over time or from actor properties. Passing NULL for @p eval uses a zeroed
     * evaluation context.
     */
    bool slayer3d_game_data_get_world_light_evaluated(const slayer3d_game_data_runtime *runtime, int index,
                                                      const slayer3d_game_data_render_eval *eval,
                                                      slayer3d_light *out_light);

    /**
     * @brief Iterate active authored render primitive components.
     *
     * Components currently supported by this iterator include `render.cube`,
     * `render.sphere`, `render.mesh_primitive`, `render.composite`,
     * `render.sprite`, and `render.model`. Iteration skips inactive actors.
     */
    bool slayer3d_game_data_for_each_render_primitive(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_render_primitive_fn callback, void *userdata);

    /**
     * @brief Iterate active authored render primitives with dynamic effects evaluated.
     *
     * This applies generic `effects` authored on render primitive components,
     * such as property-driven flash colors, size offsets, and time-driven
     * pulses. Passing NULL for @p eval uses a zeroed evaluation context.
     */
    bool slayer3d_game_data_for_each_render_primitive_evaluated(const slayer3d_game_data_runtime *runtime,
                                                                const slayer3d_game_data_render_eval *eval,
                                                                slayer3d_game_data_render_primitive_fn callback,
                                                                void *userdata);

    /**
     * @brief Read an authored particle emitter component from an entity.
     *
     * The returned config is ready for slayer3d_create_particle_emitter(). Texture
     * references are intentionally not resolved here yet, so config.texture is
     * always NULL.
     */
    bool slayer3d_game_data_get_particle_emitter(const slayer3d_game_data_runtime *runtime, const char *entity_name,
                                                 slayer3d_particle_config *out_config);

    /**
     * @brief Read optional draw-time emissive color for a particle emitter entity.
     *
     * The color is read from the emitter component's `draw_emissive` field and
     * defaults to zero when not authored.
     */
    bool slayer3d_game_data_get_particle_emitter_draw_emissive(const slayer3d_game_data_runtime *runtime,
                                                               const char *entity_name, slayer3d_vec3 *out_rgb);

    /**
     * @brief Iterate active authored particle emitter components.
     *
     * Iteration skips inactive actors and entities not included by the active
     * scene. The descriptor's config is ready for slayer3d_create_particle_emitter().
     */
    bool slayer3d_game_data_for_each_particle_emitter(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_particle_emitter_fn callback, void *userdata);

    /**
     * @brief Read authored render setup.
     *
     * Missing fields produce conservative defaults: black clear color, lighting
     * enabled, bloom/SSAO enabled, and ACES tonemapping.
     */
    bool slayer3d_game_data_get_render_settings(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_game_data_render_settings *out_settings);

    /**
     * @brief Read a named authored transition descriptor.
     *
     * @p name is looked up under the top-level `transitions` object.
     */
    bool slayer3d_game_data_get_transition(const slayer3d_game_data_runtime *runtime, const char *name,
                                           slayer3d_game_data_transition_desc *out_transition);

    /**
     * @brief Return the active scene name.
     *
     * Scene names come from the top-level `scenes.initial` field and referenced
     * scene files. Returns NULL when the game does not author scenes.
     */
    const char *slayer3d_game_data_active_scene(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Return the number of authored scenes loaded by the runtime.
     *
     * Games without a `scenes.files` manifest return 0. Scene order matches
     * the authored manifest order and is stable for the lifetime of the
     * runtime.
     */
    int slayer3d_game_data_scene_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Return an authored scene name by manifest index.
     *
     * @param index Zero-based scene index in the loaded manifest.
     * @return Runtime-owned scene name, or NULL when @p index is out of range.
     */
    const char *slayer3d_game_data_scene_name_at(const slayer3d_game_data_runtime *runtime, int index);

    /**
     * @brief Switch to an authored scene by name.
     *
     * The new scene's `on_enter_signal`, when present, is emitted after the
     * active scene changes. The enter payload always includes `from_scene` and
     * `to_scene` string keys. Returns false when @p scene_name is unknown.
     */
    bool slayer3d_game_data_set_active_scene(slayer3d_game_data_runtime *runtime, const char *scene_name);

    /**
     * @brief Switch to an authored scene and pass state to its enter signal.
     *
     * @p payload is copied into a transient enter payload and forwarded to the
     * target scene's `on_enter_signal`; the caller keeps ownership of @p
     * payload. The runtime also writes `from_scene` and `to_scene`, overriding
     * same-named keys in @p payload so every scene-enter observer receives
     * reliable transition context.
     *
     * Use slayer3d_game_data_mutable_scene_state() for data that must persist
     * after enter-signal processing.
     */
    bool slayer3d_game_data_set_active_scene_with_payload(slayer3d_game_data_runtime *runtime, const char *scene_name,
                                                          const slayer3d_properties *payload);

    /**
     * @brief Return whether the active scene should advance gameplay systems.
     *
     * Scenes default to updating gameplay when they do not specify
     * `updates_game`. Games without authored scenes return true.
     */
    bool slayer3d_game_data_active_scene_updates_game(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Return whether an authored update phase should run for the active scene.
     *
     * @p phase is an authored phase name such as `simulation`,
     * `property_effects`, `particles`, or `presentation`. Scene-level
     * `update_phases` entries override top-level entries. Missing phases use
     * conservative defaults: simulation follows `updates_game` and does not run
     * while paused; presentation/property effects/particles run in both paused
     * and unpaused frames.
     */
    bool slayer3d_game_data_active_scene_update_phase(const slayer3d_game_data_runtime *runtime, const char *phase,
                                                      bool paused);

    /**
     * @brief Return whether the active scene should render the authored world.
     *
     * Scenes default to rendering the world when they do not specify
     * `renders_world`. Games without authored scenes return true.
     */
    bool slayer3d_game_data_active_scene_renders_world(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read the active scene's optional skybox descriptor.
     *
     * Scene skyboxes are authored under `world.skybox` and reference image
     * assets by id. Returns false when the active scene has no skybox.
     */
    bool slayer3d_game_data_get_active_scene_skybox(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_scene_skybox *out_skybox);

    /**
     * @brief Return whether an entity belongs to the active scene.
     *
     * Scenes that omit an `entities` list include all loaded entities for
     * backward compatibility. Scenes with an empty list include no entities.
     */
    bool slayer3d_game_data_active_scene_has_entity(const slayer3d_game_data_runtime *runtime, const char *entity_name);

    /**
     * @brief Return whether the active scene allows an input action.
     *
     * If a scene omits `input.actions`, all actions are allowed. When present,
     * the action must be listed there or in top-level
     * `app.input_policy.global_actions`.
     */
    bool slayer3d_game_data_active_scene_allows_action(const slayer3d_game_data_runtime *runtime, int action_id);

    /**
     * @brief Return whether the active scene requests relative mouse capture.
     *
     * Scenes may author `input.mouse_capture` as `never`, `unpaused`, or
     * `always`, plus an optional `input.mouse_capture_if` condition. Missing
     * policy defaults to `never`. The @p paused argument lets generic hosts
     * release the cursor while an authored pause/menu overlay is active.
     */
    bool slayer3d_game_data_active_scene_mouse_capture(const slayer3d_game_data_runtime *runtime, bool paused);

    /**
     * @brief Read authored scene transition policy.
     *
     * Missing fields use stable defaults: same-scene requests and interrupting
     * active transitions are rejected, and accepted scene requests reset menu
     * input arming.
     */
    bool slayer3d_game_data_get_scene_transition_policy(const slayer3d_game_data_runtime *runtime,
                                                        slayer3d_game_data_scene_transition_policy *out_policy);

    /**
     * @brief Read the transition descriptor attached to a scene phase.
     *
     * @p phase is commonly `enter` or `exit` and is looked up under the scene's
     * `transitions` object. Returns false when the scene or phase is missing.
     */
    bool slayer3d_game_data_get_scene_transition(const slayer3d_game_data_runtime *runtime, const char *scene_name,
                                                 const char *phase, slayer3d_game_data_transition_desc *out_transition);

    /**
     * @brief Read the active scene's primary menu, if any.
     *
     * The first menu whose optional `active_if` condition passes is considered
     * active. Conditions that depend on frame metrics, such as `app.paused`,
     * evaluate as false through this convenience wrapper.
     */
    bool slayer3d_game_data_get_active_menu(const slayer3d_game_data_runtime *runtime,
                                            slayer3d_game_data_menu *out_menu);

    /**
     * @brief Read the active scene menu using current frame metrics.
     *
     * This variant lets authored menu `active_if` conditions depend on app
     * pause state, camera state, actor properties, or other metrics-backed UI
     * conditions. It returns false when no active scene menu is eligible.
     */
    bool slayer3d_game_data_get_active_menu_for_metrics(const slayer3d_game_data_runtime *runtime,
                                                        const slayer3d_game_data_ui_metrics *metrics,
                                                        slayer3d_game_data_menu *out_menu);

    /**
     * @brief Move a menu selection by @p delta with wrap-around.
     *
     * Positive values move down, negative values move up. Returns false when
     * the menu is unknown or contains no items.
     */
    bool slayer3d_game_data_menu_move(slayer3d_game_data_runtime *runtime, const char *menu_name, int delta);

    /**
     * @brief Publish side effects for the currently selected menu item without moving selection.
     *
     * Dynamic-list rows may author selected-index or selected-value scene-state
     * outputs. This helper refreshes those outputs for the current highlighted
     * item. It does not emit signals, select the item, or change scenes.
     */
    bool slayer3d_game_data_publish_menu_selection(slayer3d_game_data_runtime *runtime, const char *menu_name);

    /**
     * @brief Remove every row from a runtime collection.
     *
     * Runtime collections are named, host-populated row sets that authored UI
     * can read through dynamic-list menu sources. Clearing an unknown
     * collection is a successful no-op.
     */
    bool slayer3d_game_data_runtime_collection_clear(slayer3d_game_data_runtime *runtime, const char *collection_name);

    /**
     * @brief Return the number of rows currently published in a runtime collection.
     *
     * Unknown collections have a count of zero.
     */
    int slayer3d_game_data_runtime_collection_count(const slayer3d_game_data_runtime *runtime,
                                                    const char *collection_name);

    /**
     * @brief Publish a string field on one runtime collection row.
     *
     * Rows are zero-based and created on demand. Host systems should publish
     * contiguous rows and then clear the collection before republishing a
     * shorter result set.
     */
    bool slayer3d_game_data_runtime_collection_set_string(slayer3d_game_data_runtime *runtime,
                                                          const char *collection_name, int row_index,
                                                          const char *field_name, const char *value);

    /** @brief Publish an integer field on one runtime collection row. */
    bool slayer3d_game_data_runtime_collection_set_int(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                       int row_index, const char *field_name, int value);

    /** @brief Publish a floating-point field on one runtime collection row. */
    bool slayer3d_game_data_runtime_collection_set_float(slayer3d_game_data_runtime *runtime,
                                                         const char *collection_name, int row_index,
                                                         const char *field_name, float value);

    /** @brief Publish a boolean field on one runtime collection row. */
    bool slayer3d_game_data_runtime_collection_set_bool(slayer3d_game_data_runtime *runtime,
                                                        const char *collection_name, int row_index,
                                                        const char *field_name, bool value);

    /**
     * @brief Start or replace a named runtime-owned UDP direct-connect client session.
     *
     * The session is owned by @p runtime and remains valid until canceled,
     * replaced, or the runtime is destroyed. @p status_key, @p state_key, and
     * @p connected_key are optional scene-state keys updated after creation.
     */
    bool slayer3d_game_data_network_direct_connect_start(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                         const char *host, int port, const char *status_key,
                                                         const char *state_key, const char *connected_key);

    /**
     * @brief Cancel and destroy a named runtime-owned direct-connect session.
     *
     * Canceling an unknown session is a successful no-op.
     */
    bool slayer3d_game_data_network_direct_connect_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                          const char *status_key, const char *state_key,
                                                          const char *connected_key, const char *status);

    /**
     * @brief Publish a named runtime-owned direct-connect session's status into scene state.
     */
    bool slayer3d_game_data_network_direct_connect_publish_status(slayer3d_game_data_runtime *runtime,
                                                                  const char *session_name, const char *status_key,
                                                                  const char *state_key, const char *connected_key);

    /**
     * @brief Return a runtime-owned direct-connect session, or NULL when absent.
     *
     * The caller must not destroy the returned pointer.
     */
    slayer3d_network_session *slayer3d_game_data_get_network_direct_connect_session(slayer3d_game_data_runtime *runtime,
                                                                                    const char *session_name);

    /**
     * @brief Start or keep a named runtime-owned UDP host session alive.
     *
     * Host sessions listen for exactly one client. The session is owned by
     * @p runtime and remains valid until canceled, replaced, or the runtime is
     * destroyed. Optional scene-state keys publish human-readable status,
     * endpoint, peer label, and connected state.
     */
    bool slayer3d_game_data_network_host_start(slayer3d_game_data_runtime *runtime, const char *session_name, int port,
                                               const char *advertised_name, const char *status_key,
                                               const char *endpoint_key, const char *peer_key,
                                               const char *connected_key);

    /**
     * @brief Cancel and destroy a named runtime-owned host session.
     *
     * Canceling an unknown host session is a successful no-op.
     */
    bool slayer3d_game_data_network_host_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                const char *status_key, const char *endpoint_key, const char *peer_key,
                                                const char *connected_key, const char *status);

    /**
     * @brief Publish a named runtime-owned host session's status into scene state.
     */
    bool slayer3d_game_data_network_host_publish_status(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                        const char *status_key, const char *endpoint_key,
                                                        const char *peer_key, const char *connected_key);

    /**
     * @brief Return a runtime-owned host session, or NULL when absent.
     *
     * The caller must not destroy the returned pointer.
     */
    slayer3d_network_session *slayer3d_game_data_get_network_host_session(slayer3d_game_data_runtime *runtime,
                                                                          const char *session_name);

    /**
     * @brief Start or refresh a named runtime-owned LAN discovery scanner.
     *
     * Results are published to @p collection_name when provided. Each row
     * contains `label`, `name`, `host`, `port`, `status`, and `endpoint`
     * fields. @p status_key and @p count_key are optional scene-state outputs.
     */
    bool slayer3d_game_data_network_discovery_start(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                    const char *host, int port, int local_port,
                                                    const char *collection_name, const char *status_key,
                                                    const char *count_key);

    /**
     * @brief Advance a named discovery scanner and republish results.
     */
    bool slayer3d_game_data_network_discovery_update(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                     float dt, const char *collection_name, const char *status_key,
                                                     const char *count_key);

    /**
     * @brief Cancel and destroy a named discovery scanner.
     *
     * Canceling an unknown scanner is a successful no-op.
     */
    bool slayer3d_game_data_network_discovery_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                     const char *collection_name, const char *status_key,
                                                     const char *count_key, const char *status);

    /**
     * @brief Connect a direct-connect session to one row from a discovery collection.
     *
     * The selected collection row must contain `host` and `port` fields.
     */
    bool slayer3d_game_data_network_discovery_connect_selected(
        slayer3d_game_data_runtime *runtime, const char *discovery_name, const char *collection_name,
        int selected_index, const char *direct_connect_name, const char *host_key, const char *port_key,
        const char *status_key, const char *state_key, const char *connected_key, const char *connecting_status);

    /**
     * @brief Read one item from an authored menu.
     *
     * @p index is zero based. Static returned strings remain owned by the
     * runtime. Dynamic-list label/value strings are copied into storage fields
     * on @p out_item and remain valid until @p out_item is overwritten.
     */
    bool slayer3d_game_data_get_menu_item(const slayer3d_game_data_runtime *runtime, const char *menu_name, int index,
                                          slayer3d_game_data_menu_item *out_item);

    /**
     * @brief Apply the generic control behavior authored on a menu item.
     *
     * Toggle controls flip boolean properties, choice controls advance by one
     * authored choice, and range controls increase by one authored step.
     * Returns false when @p item is not a control or its target cannot be
     * resolved.
     */
    bool slayer3d_game_data_apply_menu_item_control(slayer3d_game_data_runtime *runtime,
                                                    const slayer3d_game_data_menu_item *item);

    /**
     * @brief Adjust the generic control behavior authored on a menu item.
     *
     * @p direction should be positive to increase/advance or negative to
     * decrease/rewind. Choice controls wrap across authored choices; range
     * controls clamp to their authored min/max and preserve integer properties
     * when authored with `value_type: "int"`. Toggle controls ignore direction
     * and flip the current boolean value.
     *
     * @return true when a control value changed.
     */
    bool slayer3d_game_data_adjust_menu_item_control(slayer3d_game_data_runtime *runtime,
                                                     const slayer3d_game_data_menu_item *item, int direction);

    /**
     * @brief Start capture mode for an input-binding menu item.
     *
     * The menu item must author a `control` with `type: "input_binding"`.
     * While capture is active, callers should pass input snapshots to
     * slayer3d_game_data_update_menu_input_binding_capture() before normal menu
     * navigation.
     */
    bool slayer3d_game_data_start_menu_input_binding_capture(slayer3d_game_data_runtime *runtime, const char *menu_name,
                                                             int item_index);

    /** @brief Return true while a binding menu item is waiting for an input. */
    bool slayer3d_game_data_menu_input_binding_capture_active(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Advance active binding capture from current input.
     *
     * The runtime reads the first keyboard scancode or gamepad button captured
     * by slayer3d_input_update(), depending on the menu item's authored device.
     * Successful captures immediately update the live input manager for every
     * action authored by the menu item.
     */
    slayer3d_game_data_input_binding_capture_status slayer3d_game_data_update_menu_input_binding_capture(
        slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input);

    /**
     * @brief Reset all binding controls in a menu to their authored defaults.
     */
    bool slayer3d_game_data_reset_menu_input_bindings(slayer3d_game_data_runtime *runtime, const char *menu_name);

    /**
     * @brief Start text capture for an authored menu text control.
     *
     * The menu item must author a `control` with `type: "text"`. While active,
     * callers should call slayer3d_game_data_update_menu_text_entry_capture()
     * before normal menu navigation so editing keys are consumed locally.
     */
    bool slayer3d_game_data_start_menu_text_entry_capture(slayer3d_game_data_runtime *runtime, const char *menu_name,
                                                          int item_index);

    /** @brief Return true while a menu text-entry capture is active. */
    bool slayer3d_game_data_menu_text_entry_capture_active(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Advance active text-entry capture from current input.
     *
     * SDL text-input payloads append to the bound string. Backspace and Delete
     * remove the previous UTF-8 codepoint. The menu's select action or Return
     * submits; the menu's back action or the authored cancel key cancels and
     * restores the original value.
     */
    slayer3d_game_data_text_entry_capture_status slayer3d_game_data_update_menu_text_entry_capture(
        slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input);

    /**
     * @brief Device-count state for active input profile refresh.
     *
     * Initialize with slayer3d_game_data_input_profile_refresh_state_init() before
     * the first refresh. Callers may reset the state when entering a new scene
     * if they want to force one active-profile application on the next frame.
     */
    typedef struct slayer3d_game_data_input_profile_refresh_state
    {
        /** @brief Last observed gamepad count. */
        int gamepad_count;
        /** @brief Whether gamepad_count has been sampled at least once. */
        bool initialized;
    } slayer3d_game_data_input_profile_refresh_state;

    /**
     * @brief Initialize active input profile refresh state.
     */
    void slayer3d_game_data_input_profile_refresh_state_init(slayer3d_game_data_input_profile_refresh_state *state);

    /**
     * @brief Apply one authored input profile to an input manager.
     *
     * Profiles are authored under `input.profiles`. Applying a profile first
     * unbinds every action listed in its `unbind` array, then applies either
     * raw keyboard, mouse, or gamepad bindings or reusable
     * `input.device_assignment_sets` assignments.
     *
     * @param runtime Runtime containing authored profile data and action ids.
     * @param input Input manager to mutate.
     * @param profile_name Authored profile name.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the profile exists and all authored bindings were applied.
     */
    bool slayer3d_game_data_apply_input_profile(slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                                const char *profile_name, char *error_buffer, int error_buffer_size);

    /**
     * @brief Apply the first input profile whose authored conditions match.
     *
     * Profiles are evaluated in authored order. `active_if` uses the same
     * condition language as scene UI/menu rules, and optional `min_gamepads`
     * / `max_gamepads` gates use the current input manager device count.
     *
     * @param runtime Runtime containing authored profile data and action ids.
     * @param input Input manager to mutate.
     * @param out_profile_name Receives the applied runtime-owned profile name, if non-NULL.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when a matching profile was found and applied.
     */
    bool slayer3d_game_data_apply_active_input_profile(slayer3d_game_data_runtime *runtime,
                                                       slayer3d_input_manager *input, const char **out_profile_name,
                                                       char *error_buffer, int error_buffer_size);

    /**
     * @brief Return the first input profile whose authored conditions match.
     *
     * This is a side-effect-free query for hosts and generic runtimes that
     * need to know whether automatic input-profile refresh is currently
     * applicable. It uses the same authored-order, `active_if`, and gamepad
     * gate rules as @ref slayer3d_game_data_apply_active_input_profile.
     *
     * @param runtime Runtime containing authored profile data.
     * @param input Input manager used for live gamepad-count gates.
     * @param out_profile_name Receives the matching runtime-owned profile name, if non-NULL.
     * @return true when a profile currently matches.
     */
    bool slayer3d_game_data_get_active_input_profile_name(const slayer3d_game_data_runtime *runtime,
                                                          const slayer3d_input_manager *input,
                                                          const char **out_profile_name);

    /**
     * @brief Apply the active input profile when connected gamepad count changes.
     *
     * This helper centralizes the common hotplug policy for data-authored input
     * profiles. It applies the active profile on first use, then applies again
     * only when slayer3d_input_gamepad_count() changes. Scene changes that should
     * always rebind controls should still use authored `input.apply_active_profile`
     * actions on scene entry.
     *
     * @param runtime Runtime containing authored profile data and action ids.
     * @param input Input manager to inspect and mutate.
     * @param state Persistent refresh state owned by the caller.
     * @param out_profile_name Receives the applied runtime-owned profile name, if non-NULL and applied.
     * @param out_applied Receives whether a profile was applied this call, if non-NULL.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when no refresh was needed or the matching profile was applied.
     */
    bool slayer3d_game_data_apply_active_input_profile_on_device_change(
        slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
        slayer3d_game_data_input_profile_refresh_state *state, const char **out_profile_name, bool *out_applied,
        char *error_buffer, int error_buffer_size);

    /**
     * @brief Return the number of scene shortcuts authored under `app.scene_shortcuts`.
     */
    int slayer3d_game_data_scene_shortcut_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read an authored scene shortcut by index.
     *
     * Invalid or unresolved shortcut entries still return their authored names,
     * but use action id -1. This lets validators and hosts report useful
     * diagnostics without failing at runtime.
     */
    bool slayer3d_game_data_scene_shortcut_at(const slayer3d_game_data_runtime *runtime, int index,
                                              slayer3d_game_data_scene_shortcut *out_shortcut);

    /**
     * @brief Return whether the active menu has no held navigation actions.
     *
     * This lets hosts arm menu input after scene entry. Waiting for idle input
     * prevents a key or gamepad button held while launching or switching scenes
     * from immediately activating the new scene's default menu item.
     *
     * Scenes without an active menu return true. A NULL input manager returns
     * false when a menu exists because the runtime cannot prove the menu is idle.
     */
    bool slayer3d_game_data_active_menu_input_is_idle(const slayer3d_game_data_runtime *runtime,
                                                      const slayer3d_input_manager *input);

    /**
     * @brief Iterate authored UI text descriptors.
     *
     * This is equivalent to slayer3d_game_data_for_each_ui_text_for_metrics()
     * with a NULL metrics pointer. Use the metrics-aware iterator when menu
     * presenters or authored visibility conditions depend on frame state such
     * as pause, FPS, or active scene transition state.
     */
    bool slayer3d_game_data_for_each_ui_text(const slayer3d_game_data_runtime *runtime,
                                             slayer3d_game_data_ui_text_fn callback, void *userdata);

    /**
     * @brief Iterate authored UI text descriptors using current frame metrics.
     *
     * Iteration includes global `ui.text`, active-scene `ui.text`, and menu
     * presenters from global and active-scene `ui.menus`. Conditions on
     * generated menu presenters are evaluated with `metrics`, so app-state
     * dependent menus such as pause overlays can be rendered correctly.
     */
    bool slayer3d_game_data_for_each_ui_text_for_metrics(const slayer3d_game_data_runtime *runtime,
                                                         const slayer3d_game_data_ui_metrics *metrics,
                                                         slayer3d_game_data_ui_text_fn callback, void *userdata);

    /**
     * @brief Iterate authored UI images visible to the active scene.
     *
     * Iteration includes global `ui.images` followed by active-scene
     * `ui.images`. Visibility is evaluated separately by
     * slayer3d_game_data_ui_image_is_visible().
     */
    bool slayer3d_game_data_for_each_ui_image(const slayer3d_game_data_runtime *runtime,
                                              slayer3d_game_data_ui_image_fn callback, void *userdata);

    /**
     * @brief Iterate authored UI rectangles visible to the active scene.
     *
     * Iteration includes global `ui.rects` followed by active-scene
     * `ui.rects`. Visibility is evaluated separately by
     * slayer3d_game_data_ui_rect_is_visible().
     */
    bool slayer3d_game_data_for_each_ui_rect(const slayer3d_game_data_runtime *runtime,
                                             slayer3d_game_data_ui_rect_fn callback, void *userdata);

    /**
     * @brief Initialize runtime UI state to identity values.
     *
     * The initialized state has no override flags, zero offset, scale 1, alpha
     * 1, and white tint.
     */
    void slayer3d_game_data_ui_state_init(slayer3d_game_data_ui_state *state);

    /**
     * @brief Store runtime state for a named authored UI item.
     *
     * The runtime copies @p state and owns the name key internally. State
     * remains active until replaced, cleared by name, or all UI state is
     * cleared.
     */
    bool slayer3d_game_data_set_ui_state(slayer3d_game_data_runtime *runtime, const char *name,
                                         const slayer3d_game_data_ui_state *state);

    /**
     * @brief Read runtime state for a named authored UI item.
     *
     * Returns false when no state exists for @p name. @p out_state is
     * initialized to identity values before lookup.
     */
    bool slayer3d_game_data_get_ui_state(const slayer3d_game_data_runtime *runtime, const char *name,
                                         slayer3d_game_data_ui_state *out_state);

    /** @brief Clear runtime state for one named UI item. */
    bool slayer3d_game_data_clear_ui_state(slayer3d_game_data_runtime *runtime, const char *name);

    /** @brief Clear all runtime UI item state. */
    void slayer3d_game_data_clear_ui_states(slayer3d_game_data_runtime *runtime);

    /**
     * @brief Resolve authored text plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions
     * and runtime overrides. @p out_text may alias @p text.
     */
    bool slayer3d_game_data_resolve_ui_text(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_ui_text *text,
                                            const slayer3d_game_data_ui_metrics *metrics,
                                            slayer3d_game_data_ui_text *out_text, bool *out_visible);

    /**
     * @brief Resolve authored image plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions
     * and runtime overrides. @p out_image may alias @p image.
     */
    bool slayer3d_game_data_resolve_ui_image(const slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_ui_image *image,
                                             const slayer3d_game_data_ui_metrics *metrics,
                                             slayer3d_game_data_ui_image *out_image, bool *out_visible);

    /**
     * @brief Resolve authored rectangle plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions,
     * property-driven alpha, and runtime overrides. @p out_rect may alias
     * @p rect.
     */
    bool slayer3d_game_data_resolve_ui_rect(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_ui_rect *rect,
                                            const slayer3d_game_data_ui_metrics *metrics,
                                            slayer3d_game_data_ui_rect *out_rect, bool *out_visible);

    /**
     * @brief Evaluate a UI text descriptor's authored visibility condition.
     *
     * Supports camera-active checks, app pause checks, actor property
     * comparisons, and boolean all/any/not composition. Descriptors without a
     * condition are visible.
     */
    bool slayer3d_game_data_ui_text_is_visible(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_ui_text *text,
                                               const slayer3d_game_data_ui_metrics *metrics);

    /** @brief Evaluate a UI image's authored `visible_if` condition. */
    bool slayer3d_game_data_ui_image_is_visible(const slayer3d_game_data_runtime *runtime,
                                                const slayer3d_game_data_ui_image *image,
                                                const slayer3d_game_data_ui_metrics *metrics);

    /**
     * @brief Evaluate a UI rectangle descriptor's authored visibility condition.
     */
    bool slayer3d_game_data_ui_rect_is_visible(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_ui_rect *rect,
                                               const slayer3d_game_data_ui_metrics *metrics);

    /**
     * @brief Read the active scene's authored skip policy.
     *
     * The runtime first checks `scene.skip_policy`, then
     * `scene.timeline.skip_policy`. Returns false when no enabled policy is
     * authored for the active scene. Missing fields use conservative defaults:
     * any input, transition preservation enabled, and input consumption enabled.
     */
    bool slayer3d_game_data_get_active_skip_policy(const slayer3d_game_data_runtime *runtime,
                                                   slayer3d_game_data_skip_policy *out_policy);

    /**
     * @brief Read the active scene's authored timeline interaction policy.
     *
     * Returns false when the active scene has no autoplaying `timeline` object.
     * Missing policy fields default to false so timelines remain interactive
     * unless the scene author explicitly blocks menus or scene shortcuts.
     */
    bool slayer3d_game_data_get_active_timeline_policy(const slayer3d_game_data_runtime *runtime,
                                                       slayer3d_game_data_timeline_policy *out_policy);

    /**
     * @brief Initialize reusable timeline state.
     *
     * Safe to call with NULL.
     */
    void slayer3d_game_data_timeline_state_init(slayer3d_game_data_timeline_state *state);

    /**
     * @brief Advance the active scene's authored autoplay timeline.
     *
     * Timeline events are one-shot and must be authored in non-decreasing time
     * order. This helper executes generic actions that are safe to apply inside
     * the data runtime (`signal.emit`, `property.set`, etc.). `scene.request`
     * is reported in @p out_result so hosts can route the request through their
     * own transition flow instead of forcing an immediate scene switch.
     *
     * @param runtime Loaded game data runtime.
     * @param state Persistent timeline state owned by the host.
     * @param dt Delta time in seconds.
     * @param out_result Optional update result.
     * @return true when the timeline update completed without an execution error.
     */
    bool slayer3d_game_data_update_timeline(slayer3d_game_data_runtime *runtime,
                                            slayer3d_game_data_timeline_state *state, float dt,
                                            slayer3d_game_data_timeline_update_result *out_result);

    /**
     * @brief Advance data-authored runtime animations.
     *
     * Animations are started by generic actions such as `ui.animate` and
     * `property.animate`. This function advances active tweens, applies eased
     * values to UI runtime state or actor properties, emits completion signals
     * for one-shot animations, and clears scene-scoped animation state when the
     * active scene changes.
     *
     * @param runtime Runtime created by slayer3d_game_data_load_file().
     * @param dt Delta time in seconds.
     * @return true when active animations were advanced successfully.
     */
    bool slayer3d_game_data_update_animations(slayer3d_game_data_runtime *runtime, float dt);

    /**
     * @brief Resolve UI text content from data-authored bindings.
     *
     * Literal `text` entries are copied directly. Entries with `bindings`
     * resolve engine metrics, brush diagnostics, scene state, and actor
     * properties, then format them using the descriptor's `format` string.
     */
    bool slayer3d_game_data_format_ui_text(const slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_ui_text *text,
                                           const slayer3d_game_data_ui_metrics *metrics, char *buffer,
                                           size_t buffer_size);

    /**
     * @brief Advance data-authored property animation components.
     *
     * Currently supports `property.decay` components, which move a numeric
     * actor property toward a target value at an authored rate. This is useful
     * for reusable presentation state such as flashes, glow weights, and other
     * transient values without per-game C code.
     */
    bool slayer3d_game_data_update_property_effects(slayer3d_game_data_runtime *runtime, float dt);

    /**
     * @brief Return the dt currently being processed by slayer3d_game_data_update().
     *
     * Adapter callbacks can use this for per-frame controller behavior. Outside
     * an update call, this returns the most recent non-negative update dt.
     */
    float slayer3d_game_data_delta_time(const slayer3d_game_data_runtime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_DATA_H */

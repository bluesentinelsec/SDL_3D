/**
 * @file game_data_runtime.h
 * @brief Core runtime lifecycle and lookup APIs for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_RUNTIME_H
#define SLAYER3D_GAME_DATA_RUNTIME_H

#include <stdbool.h>

#include "slayer3d/actor_registry.h"
#include "slayer3d/asset.h"
#include "slayer3d/game.h"
#include "slayer3d/properties.h"
#include "slayer3d/storage.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

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
        /** @brief Optional filesystem base directory for authored tools that enumerate loose source assets. */
        const char *source_base_dir;
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

#ifdef __cplusplus
}
#endif

#endif

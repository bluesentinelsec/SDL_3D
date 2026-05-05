/**
 * @file data_game.h
 * @brief Generic runtime for JSON/Lua-authored SDL3D games.
 *
 * The data-game runtime owns the common lifecycle needed by games authored
 * through SDL3D game data: asset mounting, game JSON loading, app-flow/frame
 * state, presentation caches, haptics policy wiring, input-profile hotplug
 * state, update, render, and shutdown. Game-specific hosts should prefer this
 * API over duplicating lifecycle code.
 */

#ifndef SDL3D_DATA_GAME_H
#define SDL3D_DATA_GAME_H

#include <stdbool.h>

#include "sdl3d/asset.h"
#include "sdl3d/game.h"
#include "sdl3d/game_data.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime for one loaded data-authored game package. */
    typedef struct sdl3d_data_game_runtime sdl3d_data_game_runtime;

    /**
     * @brief Callback used to mount game assets into a new resolver.
     *
     * The runtime creates the resolver and then calls this function before
     * loading @ref sdl3d_data_game_runtime_desc::data_asset_path. The callback
     * should mount a directory, pack file, embedded pack, or any combination of
     * asset roots needed by the game package.
     *
     * @param assets Runtime-owned resolver to populate.
     * @param userdata Caller-provided pointer from the runtime descriptor.
     * @param error_buffer Optional buffer for a failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true on success, false on mount failure.
     */
    typedef bool (*sdl3d_data_game_mount_assets_fn)(sdl3d_asset_resolver *assets, void *userdata, char *error_buffer,
                                                    int error_buffer_size);

    /** @brief Creation descriptor for a generic data-game runtime. */
    typedef struct sdl3d_data_game_runtime_desc
    {
        /** @brief Session containing input, signals, timers, logic, and optional audio. Required. */
        sdl3d_game_session *session;
        /** @brief Asset path to the root game JSON, such as `asset://pong.game.json`. Required. */
        const char *data_asset_path;
        /** @brief Media directory used by built-in font loading. Optional. */
        const char *media_dir;
        /** @brief Optional callback that mounts game assets into the runtime resolver. */
        sdl3d_data_game_mount_assets_fn mount_assets;
        /** @brief Userdata passed to @ref mount_assets. */
        void *mount_userdata;
    } sdl3d_data_game_runtime_desc;

    /**
     * @brief Initialize a runtime descriptor with NULL/default fields.
     */
    void sdl3d_data_game_runtime_desc_init(sdl3d_data_game_runtime_desc *desc);

    /**
     * @brief Create and start a generic data-game runtime.
     *
     * This creates an asset resolver, mounts assets, loads the authored game
     * data into @p desc->session, initializes presentation caches, starts the
     * authored app flow, and connects authored haptics policies.
     *
     * @param desc Runtime descriptor.
     * @param out_runtime Receives the created runtime on success.
     * @param error_buffer Optional buffer for a failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true on success, false on startup failure.
     */
    bool sdl3d_data_game_runtime_create(const sdl3d_data_game_runtime_desc *desc, sdl3d_data_game_runtime **out_runtime,
                                        char *error_buffer, int error_buffer_size);

    /**
     * @brief Destroy a data-game runtime and all resources it owns.
     *
     * Safe to call with NULL.
     */
    void sdl3d_data_game_runtime_destroy(sdl3d_data_game_runtime *runtime);

    /** @brief Return the runtime-owned asset resolver, or NULL. */
    sdl3d_asset_resolver *sdl3d_data_game_runtime_assets(const sdl3d_data_game_runtime *runtime);

    /** @brief Return the loaded game-data runtime, or NULL. */
    sdl3d_game_data_runtime *sdl3d_data_game_runtime_data(const sdl3d_data_game_runtime *runtime);

    /**
     * @brief Refresh authored input profiles when device count changes.
     *
     * This is a thin runtime-owned wrapper around
     * sdl3d_game_data_apply_active_input_profile_on_device_change(). It stores
     * the refresh state inside the data-game runtime so hosts do not need
     * per-game bookkeeping.
     */
    bool sdl3d_data_game_runtime_refresh_input_profile_on_device_change(sdl3d_data_game_runtime *runtime,
                                                                        sdl3d_input_manager *input,
                                                                        const char **out_profile_name,
                                                                        bool *out_applied, char *error_buffer,
                                                                        int error_buffer_size);

    /**
     * @brief Advance authored frame/app-flow/presentation systems.
     *
     * This does not update the outer SDL3D game session tick counters; callers
     * should invoke it from their managed-loop tick or pause_tick callback.
     */
    bool sdl3d_data_game_runtime_update_frame(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx, float dt);

    /**
     * @brief Draw the active authored game-data frame.
     *
     * Records render metrics and draws world primitives, particles, UI, images,
     * and transitions through the runtime-owned caches.
     */
    void sdl3d_data_game_runtime_render(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_DATA_GAME_H */

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
        /**
         * @brief Enable authored host/direct-connect network orchestration.
         *
         * When true, the runtime consumes the standard authored network
         * semantics from `network.session_flow`, `network.runtime_bindings`,
         * and `network.scene_state` to keep host/direct-connect sessions
         * updated, send/receive runtime-bound replication packets, handle
         * start/pause/disconnect controls, publish status, and run authored
         * session-flow events. Local-only games can leave this false.
         */
        bool enable_managed_network;
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
     * @brief Runtime-binding names used by the generic network packet loop.
     *
     * Each field is a semantic key from `network.runtime_bindings`, not a
     * concrete replication channel or control-message name. Games can author
     * different schema names while reusing the same host/client loop.
     */
    typedef struct sdl3d_data_game_network_bindings
    {
        /** @brief Host-to-client snapshot replication binding, or NULL if unused. */
        const char *state_snapshot;
        /** @brief Client-to-host input replication binding, or NULL if unused. */
        const char *client_input;
        /** @brief Host-to-client start-game control binding, or NULL if unused. */
        const char *start_game;
        /** @brief Client-to-host pause request control binding, or NULL if unused. */
        const char *pause_request;
        /** @brief Client-to-host resume request control binding, or NULL if unused. */
        const char *resume_request;
        /** @brief Bidirectional graceful disconnect control binding, or NULL if unused. */
        const char *disconnect;
    } sdl3d_data_game_network_bindings;

    /**
     * @brief Summary of one generic network packet-loop update.
     */
    typedef struct sdl3d_data_game_network_loop_result
    {
        /** @brief Current transport state after the update, if a session exists. */
        sdl3d_network_state session_state;
        /** @brief Number of user packets consumed from the session queue. */
        int packets_received;
        /** @brief Last decoded packet/control tick, or zero when none was decoded. */
        Uint32 last_tick;
        /** @brief True when a start-game control was received. */
        bool received_start_game;
        /** @brief True when a graceful disconnect control was received. */
        bool received_disconnect;
        /** @brief True when a pause request control was received and applied to @p ctx. */
        bool received_pause_request;
        /** @brief True when a resume request control was received and applied to @p ctx. */
        bool received_resume_request;
        /** @brief True when a client input packet was applied. */
        bool applied_input;
        /** @brief True when an authoritative snapshot packet was applied. */
        bool applied_snapshot;
        /** @brief True when the client sent an input packet. */
        bool sent_input;
        /** @brief True when the host sent an authoritative snapshot packet. */
        bool sent_snapshot;
        /** @brief True when the client sent a pause request control. */
        bool sent_pause_request;
        /** @brief True when the client sent a resume request control. */
        bool sent_resume_request;
    } sdl3d_data_game_network_loop_result;

    /**
     * @brief Advance a runtime-owned host session and consume client packets.
     *
     * The function updates the named `network.host` session, decodes authored
     * runtime control packets, applies client input packets when @p playing is
     * true, and mirrors pause/resume requests into @p ctx. Scene transitions
     * remain caller-owned; callers inspect @p out_result to decide what to do.
     */
    bool sdl3d_data_game_runtime_update_network_host_session(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx,
                                                             const char *session_name,
                                                             const sdl3d_data_game_network_bindings *bindings,
                                                             bool playing, float dt,
                                                             sdl3d_data_game_network_loop_result *out_result,
                                                             char *error_buffer, int error_buffer_size);

    /**
     * @brief Send one authoritative host snapshot over a runtime-owned host session.
     *
     * Before encoding, this writes @p ctx->paused into the authored network
     * pause property so clients can mirror host pause state through snapshots.
     */
    bool sdl3d_data_game_runtime_publish_network_host_snapshot(sdl3d_data_game_runtime *runtime,
                                                               sdl3d_game_context *ctx, const char *session_name,
                                                               const sdl3d_data_game_network_bindings *bindings,
                                                               sdl3d_data_game_network_loop_result *out_result,
                                                               char *error_buffer, int error_buffer_size);

    /**
     * @brief Advance a runtime-owned direct-connect client session.
     *
     * The function updates the named direct-connect session, consumes
     * host-to-client control/snapshot packets, mirrors snapshot pause state
     * into @p ctx, and when @p playing is true sends client input packets. If
     * @p allow_pause_requests is true, a pressed authored pause action sends
     * pause/resume request controls based on @p ctx->paused.
     */
    bool sdl3d_data_game_runtime_update_network_client_session(sdl3d_data_game_runtime *runtime,
                                                               sdl3d_game_context *ctx, const char *session_name,
                                                               const sdl3d_data_game_network_bindings *bindings,
                                                               bool playing, bool allow_pause_requests, float dt,
                                                               sdl3d_data_game_network_loop_result *out_result,
                                                               char *error_buffer, int error_buffer_size);

    /**
     * @brief Advance authored frame/app-flow/presentation systems.
     *
     * This does not update the outer SDL3D game session tick counters; callers
     * should invoke it from their managed-loop tick or pause_tick callback.
     * The runtime also refreshes the active authored input profile when the
     * connected gamepad count changes and the current scene state has a
     * matching profile.
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

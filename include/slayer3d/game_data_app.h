/**
 * @file game_data_app.h
 * @brief Authored application lifecycle and haptics descriptors.
 */

#ifndef SLAYER3D_GAME_DATA_APP_H
#define SLAYER3D_GAME_DATA_APP_H

#include <SDL3/SDL_stdinc.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Authored application lifecycle hooks. */
    typedef struct slayer3d_game_data_app_control
    {
        /** @brief Signal emitted by the host after game data has loaded, or -1. */
        int start_signal_id;
        /** @brief Input action that requests app quit, or -1. */
        int quit_action_id;
        /** @brief Signal that requests the normal app quit flow, or -1. */
        int quit_request_signal_id;
        /** @brief Input action that requests pause/unpause, or -1. */
        int pause_action_id;
        /** @brief Transition name to play at startup, or NULL. */
        const char *startup_transition;
        /** @brief Transition name to play before quit, or NULL. */
        const char *quit_transition;
        /** @brief Signal that means the app should quit immediately, or -1. */
        int quit_signal_id;
        /** @brief Signal that applies live window settings, or -1. */
        int window_apply_signal_id;
        /** @brief Actor containing authored window setting properties, or NULL. */
        const char *window_settings_target;
        /** @brief Property key containing display mode, or NULL. */
        const char *window_display_mode_key;
        /** @brief Property key containing renderer/backend, or NULL. */
        const char *window_renderer_key;
        /** @brief Property key containing V-sync, or NULL. */
        const char *window_vsync_key;
    } slayer3d_game_data_app_control;

    /** @brief Authored haptics/rumble policy selected by a signal payload. */
    typedef struct slayer3d_game_data_haptics_policy
    {
        /** @brief Stable authored policy name, owned by the runtime. */
        const char *name;
        /** @brief Signal that triggers the policy, or -1. */
        int signal_id;
        /** @brief Low-frequency rumble intensity in the range [0, 1]. */
        float low_frequency;
        /** @brief High-frequency rumble intensity in the range [0, 1]. */
        float high_frequency;
        /** @brief Rumble duration in milliseconds. */
        Uint32 duration_ms;
    } slayer3d_game_data_haptics_policy;

#ifdef __cplusplus
}
#endif

#endif

/**
 * @file game_data_input_runtime.h
 * @brief Runtime input-profile helpers for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_INPUT_RUNTIME_H
#define SLAYER3D_GAME_DATA_INPUT_RUNTIME_H

#include <stdbool.h>

#include "slayer3d/input.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

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

#ifdef __cplusplus
}
#endif

#endif

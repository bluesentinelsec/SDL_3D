/**
 * @file game_data_menu_runtime.h
 * @brief Runtime menu helpers for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_MENU_RUNTIME_H
#define SLAYER3D_GAME_DATA_MENU_RUNTIME_H

#include <stdbool.h>

#include "slayer3d/game_data_render.h"
#include "slayer3d/game_data_scene.h"
#include "slayer3d/game_data_ui.h"
#include "slayer3d/input.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

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

#ifdef __cplusplus
}
#endif

#endif

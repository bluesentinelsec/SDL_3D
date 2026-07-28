/**
 * @file input.h
 * @brief Action-based input abstraction and demo input recording.
 *
 * SLAYER3D input maps physical devices to named actions. Gameplay code reads
 * action snapshots instead of raw SDL keyboard, mouse, or gamepad state. The
 * same snapshot format can be recorded and played back for deterministic
 * Quake-style demos.
 */

#ifndef SLAYER3D_INPUT_H
#define SLAYER3D_INPUT_H

#include <stdbool.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SLAYER3D_INPUT_MAX_ACTIONS 64
#define SLAYER3D_INPUT_MAX_BINDINGS 128
#define SLAYER3D_INPUT_ACTION_NAME_MAX 64
#define SLAYER3D_INPUT_MAX_GAMEPADS 4
#define SLAYER3D_INPUT_TEXT_MAX 128

    /**
     * @brief Physical input source kind for an action binding.
     */
    typedef enum slayer3d_input_source
    {
        SLAYER3D_INPUT_KEYBOARD = 0,
        SLAYER3D_INPUT_MOUSE_BUTTON,
        SLAYER3D_INPUT_MOUSE_AXIS,
        SLAYER3D_INPUT_GAMEPAD_BUTTON,
        SLAYER3D_INPUT_GAMEPAD_AXIS,
    } slayer3d_input_source;

    /**
     * @brief Mouse axes exposed as action bindings.
     */
    typedef enum slayer3d_mouse_axis
    {
        SLAYER3D_MOUSE_AXIS_X = 0,   /**< Horizontal mouse motion delta. */
        SLAYER3D_MOUSE_AXIS_Y,       /**< Vertical mouse motion delta. */
        SLAYER3D_MOUSE_AXIS_WHEEL,   /**< Vertical scroll-wheel delta. */
        SLAYER3D_MOUSE_AXIS_WHEEL_X, /**< Horizontal scroll-wheel delta. */
    } slayer3d_mouse_axis;

    /**
     * @brief Optional keyboard modifier mask for keyboard bindings.
     */
    typedef enum slayer3d_input_modifier
    {
        SLAYER3D_INPUT_MOD_NONE = 0,
        SLAYER3D_INPUT_MOD_SHIFT = 1 << 0,
        SLAYER3D_INPUT_MOD_CTRL = 1 << 1,
        SLAYER3D_INPUT_MOD_ALT = 1 << 2,
        SLAYER3D_INPUT_MOD_GUI = 1 << 3,
#if defined(__APPLE__)
        /** @brief Platform-native command modifier: GUI/Command on Apple platforms. */
        SLAYER3D_INPUT_MOD_PRIMARY = SLAYER3D_INPUT_MOD_GUI,
#else
    /** @brief Platform-native command modifier: Control on non-Apple platforms. */
    SLAYER3D_INPUT_MOD_PRIMARY = SLAYER3D_INPUT_MOD_CTRL,
#endif
    } slayer3d_input_modifier;

    /**
     * @brief A physical input mapped to a registered action.
     */
    typedef struct slayer3d_input_binding
    {
        int action_id;                /**< Registered action ID. */
        slayer3d_input_source source; /**< Source device/control kind. */
        int gamepad_index;            /**< Specific gamepad slot, or -1 for any connected slot. */
        int required_modifiers;       /**< slayer3d_input_modifier mask for keyboard bindings. */
        int excluded_modifiers;       /**< slayer3d_input_modifier mask that must be absent for keyboard bindings. */
        union {
            SDL_Scancode scancode;            /**< Keyboard scancode. */
            Uint8 mouse_button;               /**< SDL mouse button number. */
            slayer3d_mouse_axis mouse_axis;   /**< Mouse axis. */
            SDL_GamepadButton gamepad_button; /**< SDL gamepad button. */
            SDL_GamepadAxis gamepad_axis;     /**< SDL gamepad axis. */
        };
        float scale; /**< Input multiplier; use -1 to invert axes. */
    } slayer3d_input_binding;

    /**
     * @brief Per-action state for one input tick.
     */
    typedef struct slayer3d_action_state
    {
        bool pressed;  /**< True on the tick the action first becomes active. */
        bool released; /**< True on the tick the action becomes inactive. */
        bool held;     /**< True while the action is active. */
        float value;   /**< Analog value in [-1, 1], or 0/1 for digital input. */
    } slayer3d_action_state;

    /**
     * @brief Fixed-size input state captured for one simulation tick.
     */
    typedef struct slayer3d_input_snapshot
    {
        slayer3d_action_state actions[SLAYER3D_INPUT_MAX_ACTIONS];
        float mouse_dx;      /**< Raw horizontal mouse motion accumulated this tick. */
        float mouse_dy;      /**< Raw vertical mouse motion accumulated this tick. */
        float mouse_wheel_x; /**< Raw horizontal mouse wheel delta accumulated this tick. */
        float mouse_wheel_y; /**< Raw vertical mouse wheel delta accumulated this tick. */
        int tick;            /**< Simulation tick number for demo synchronization. */
        bool any_pressed;    /**< True when any key, mouse button, gamepad button, or action was pressed. */
    } slayer3d_input_snapshot;

    /** @brief Opaque action input manager. */
    typedef struct slayer3d_input_manager slayer3d_input_manager;

    /** @brief Opaque input demo recorder. */
    typedef struct slayer3d_demo_recorder slayer3d_demo_recorder;

    /** @brief Opaque input demo playback object. */
    typedef struct slayer3d_demo_player slayer3d_demo_player;

    /* ================================================================== */
    /* Lifecycle                                                          */
    /* ================================================================== */

    /**
     * @brief Create an input manager.
     * @return A new manager, or NULL on allocation failure.
     */
    slayer3d_input_manager *slayer3d_input_create(void);

    /**
     * @brief Destroy an input manager.
     *
     * Active demo playback is detached but not freed; callers own demo players.
     * Safe to call with NULL.
     */
    void slayer3d_input_destroy(slayer3d_input_manager *input);

    /* ================================================================== */
    /* Action registration                                                 */
    /* ================================================================== */

    /**
     * @brief Register a named action.
     *
     * Re-registering an existing action returns its existing ID.
     *
     * @return A 0-based action ID, or -1 if invalid, too long, or full.
     */
    int slayer3d_input_register_action(slayer3d_input_manager *input, const char *name);

    /**
     * @brief Find a registered action by name.
     * @return The action ID, or -1 if not found.
     */
    int slayer3d_input_find_action(const slayer3d_input_manager *input, const char *name);

    /* ================================================================== */
    /* Bindings                                                           */
    /* ================================================================== */

    /** @brief Bind a key to an action. */
    void slayer3d_input_bind_key(slayer3d_input_manager *input, int action_id, SDL_Scancode key);

    /** @brief Bind a key plus required modifiers to an action. */
    void slayer3d_input_bind_key_mod(slayer3d_input_manager *input, int action_id, SDL_Scancode key,
                                     int required_modifiers);

    /** @brief Bind a key with required and excluded modifier masks. */
    void slayer3d_input_bind_key_mod_mask(slayer3d_input_manager *input, int action_id, SDL_Scancode key,
                                          int required_modifiers, int excluded_modifiers);

    /** @brief Bind a mouse button to an action. */
    void slayer3d_input_bind_mouse_button(slayer3d_input_manager *input, int action_id, Uint8 button);

    /**
     * @brief Bind a mouse motion or wheel axis to an action.
     *
     * If the same physical axis is bound to another action with an opposite
     * scale, each binding reports only its positive half. This supports
     * directional actions such as look_left/look_right on one axis.
     */
    void slayer3d_input_bind_mouse_axis(slayer3d_input_manager *input, int action_id, slayer3d_mouse_axis axis,
                                        float scale);

    /** @brief Bind a gamepad button to an action. */
    void slayer3d_input_bind_gamepad_button(slayer3d_input_manager *input, int action_id, SDL_GamepadButton button);

    /**
     * @brief Bind a gamepad button to an action for one specific slot.
     *
     * Pass a slot in [0, SLAYER3D_INPUT_MAX_GAMEPADS) to require one controller,
     * or -1 to match any connected controller.
     */
    void slayer3d_input_bind_gamepad_button_at(slayer3d_input_manager *input, int action_id, int gamepad_index,
                                               SDL_GamepadButton button);

    /**
     * @brief Bind a gamepad axis to an action.
     *
     * If the same physical axis is bound to another action with an opposite
     * scale, each binding reports only its positive half. This supports
     * directional actions such as move_forward/move_back on one stick axis.
     */
    void slayer3d_input_bind_gamepad_axis(slayer3d_input_manager *input, int action_id, SDL_GamepadAxis axis,
                                          float scale);

    /**
     * @brief Bind a gamepad axis to an action for one specific slot.
     *
     * Pass a slot in [0, SLAYER3D_INPUT_MAX_GAMEPADS) to require one controller,
     * or -1 to match any connected controller.
     */
    void slayer3d_input_bind_gamepad_axis_at(slayer3d_input_manager *input, int action_id, int gamepad_index,
                                             SDL_GamepadAxis axis, float scale);

    /** @brief Remove all bindings for an action. */
    void slayer3d_input_unbind_action(slayer3d_input_manager *input, int action_id);

    /**
     * @brief Override an action's runtime value for the next input update.
     *
     * When an override is active, the injected value replaces live bindings
     * for that action until it is cleared. This is intended for gameplay
     * systems that need to feed authored actions from non-physical sources
     * such as network packets, scripted AI, or deterministic playback.
     *
     * Passing a value in [-1, 1] is recommended. Digital actions usually use
     * 0 or 1.
     */
    void slayer3d_input_set_action_override(slayer3d_input_manager *input, int action_id, float value);

    /** @brief Clear one action override and resume live bindings for that action. */
    void slayer3d_input_clear_action_override(slayer3d_input_manager *input, int action_id);

    /** @brief Clear all action overrides and resume live bindings. */
    void slayer3d_input_clear_action_overrides(slayer3d_input_manager *input);

    /* ================================================================== */
    /* Gamepad state                                                      */
    /* ================================================================== */

    /**
     * @brief Return the number of connected gamepad slots.
     *
     * SLAYER3D tracks up to four simultaneous gamepads. Slots remain stable for
     * the lifetime of a connected controller and are reused when controllers
     * disconnect and new ones are added.
     */
    int slayer3d_input_gamepad_count(const slayer3d_input_manager *input);

    /**
     * @brief Return true if a gamepad slot is connected.
     *
     * @param gamepad_index Slot index in [0, SLAYER3D_INPUT_MAX_GAMEPADS).
     */
    bool slayer3d_input_gamepad_is_connected(const slayer3d_input_manager *input, int gamepad_index);

    /**
     * @brief Return the SDL joystick instance id for a gamepad slot.
     *
     * Returns 0 when the slot is invalid or disconnected.
     */
    SDL_JoystickID slayer3d_input_gamepad_id_at(const slayer3d_input_manager *input, int gamepad_index);

    /**
     * @brief Return true when a gamepad button is held for a given slot.
     *
     * This is a direct state query, separate from the action system's
     * pressed/released edge tracking.
     */
    bool slayer3d_input_is_gamepad_button_held(const slayer3d_input_manager *input, int gamepad_index,
                                               SDL_GamepadButton button);

    /** @brief Return the normalized axis value for one gamepad slot. */
    float slayer3d_input_get_gamepad_axis(const slayer3d_input_manager *input, int gamepad_index, SDL_GamepadAxis axis);

    /** @brief Return the left stick vector for one gamepad slot. */
    slayer3d_vec2 slayer3d_input_get_gamepad_left_stick(const slayer3d_input_manager *input, int gamepad_index);

    /** @brief Return the right stick vector for one gamepad slot. */
    slayer3d_vec2 slayer3d_input_get_gamepad_right_stick(const slayer3d_input_manager *input, int gamepad_index);

    /** @brief Return true when the left stick press is held for one gamepad slot. */
    bool slayer3d_input_is_gamepad_left_stick_pressed(const slayer3d_input_manager *input, int gamepad_index);

    /** @brief Return true when the right stick press is held for one gamepad slot. */
    bool slayer3d_input_is_gamepad_right_stick_pressed(const slayer3d_input_manager *input, int gamepad_index);

    /** @brief Return true when any face button is held for one gamepad slot. */
    bool slayer3d_input_is_gamepad_face_button_pressed(const slayer3d_input_manager *input, int gamepad_index);

    /** @brief Return true when Start is held for one gamepad slot. */
    bool slayer3d_input_is_gamepad_start_pressed(const slayer3d_input_manager *input, int gamepad_index);

    /** @brief Return true when Select/Back is held for one gamepad slot. */
    bool slayer3d_input_is_gamepad_select_pressed(const slayer3d_input_manager *input, int gamepad_index);

    /**
     * @brief Start rumble on one connected gamepad slot.
     *
     * The strength values are normalized in [0, 1]. Returns false when the
     * slot is invalid, disconnected, or rumble is unavailable.
     */
    bool slayer3d_input_rumble_gamepad(slayer3d_input_manager *input, int gamepad_index, float low_frequency_rumble,
                                       float high_frequency_rumble, Uint32 duration_ms);

    /**
     * @brief Start rumble on every connected gamepad slot.
     *
     * Returns true when at least one controller accepted rumble. If no
     * controllers are connected, or none support rumble, the call returns
     * false.
     */
    bool slayer3d_input_rumble_all_gamepads(slayer3d_input_manager *input, float low_frequency_rumble,
                                            float high_frequency_rumble, Uint32 duration_ms);

    /* ================================================================== */
    /* Per-frame processing                                                */
    /* ================================================================== */

    /**
     * @brief Process one SDL event.
     *
     * Call this before gameplay event handling so the input manager can track
     * key/button edges, mouse deltas, wheel deltas, and hot-plugged gamepad
     * slots.
     */
    void slayer3d_input_process_event(slayer3d_input_manager *input, const SDL_Event *event);

    /**
     * @brief Build the current action snapshot.
     *
     * If demo playback is active, this returns the next recorded snapshot.
     * Otherwise it evaluates live bindings, records the snapshot if a recorder
     * is active, resets transient accumulators, and returns a pointer valid
     * until the next update. The tick argument is copied into the snapshot for
     * synchronization with the caller's simulation timeline.
     */
    const slayer3d_input_snapshot *slayer3d_input_update(slayer3d_input_manager *input, int tick);

    /**
     * @brief Discard pending mouse motion around a relative-capture transition.
     *
     * This is intended for relative mouse capture transitions, where the
     * platform may report synthetic cursor recenter/focus motion that should
     * not be interpreted as player look input. Motion is ignored until the
     * next input snapshot; if no motion arrives before that snapshot, the next
     * motion event is also ignored to handle delayed platform artifacts. Safe
     * to call with NULL.
     */
    void slayer3d_input_discard_mouse_motion(slayer3d_input_manager *input);

    /* ================================================================== */
    /* Queries                                                            */
    /* ================================================================== */

    /** @brief Return true if the action was pressed on the current tick. */
    bool slayer3d_input_is_pressed(const slayer3d_input_manager *input, int action_id);

    /** @brief Return true if the action was released on the current tick. */
    bool slayer3d_input_is_released(const slayer3d_input_manager *input, int action_id);

    /** @brief Return true if the action is held on the current tick. */
    bool slayer3d_input_is_held(const slayer3d_input_manager *input, int action_id);

    /** @brief Return the action's current analog value. */
    float slayer3d_input_get_value(const slayer3d_input_manager *input, int action_id);

    /**
     * @brief Return the first keyboard scancode pressed during the current input tick.
     *
     * The value is captured by slayer3d_input_update() before transient key
     * edges are cleared. Returns SDL_SCANCODE_UNKNOWN when no key was pressed
     * or live keyboard input is unavailable, such as during demo playback.
     */
    SDL_Scancode slayer3d_input_get_pressed_scancode(const slayer3d_input_manager *input);

    /**
     * @brief Return the first gamepad button pressed during the current input tick.
     *
     * The value is captured by slayer3d_input_update() before transient button
     * edges are cleared. Returns SDL_GAMEPAD_BUTTON_INVALID when no gamepad
     * button was pressed or live gamepad input is unavailable, such as during
     * demo playback.
     */
    SDL_GamepadButton slayer3d_input_get_pressed_gamepad_button(const slayer3d_input_manager *input);

    /**
     * @brief Return the first mouse button pressed during the current input tick.
     *
     * The value is captured by slayer3d_input_update() before transient button
     * edges are cleared. Returns 0 when no mouse button was pressed or live
     * mouse input is unavailable, such as during demo playback.
     */
    Uint8 slayer3d_input_get_pressed_mouse_button(const slayer3d_input_manager *input);

    /** @brief Return true if the scancode was pressed during the current input tick. */
    bool slayer3d_input_is_scancode_pressed(const slayer3d_input_manager *input, SDL_Scancode scancode);

    /** @brief Return true if the mouse button is currently held down. */
    bool slayer3d_input_is_mouse_button_down(const slayer3d_input_manager *input, Uint8 button);

    /** @brief Return true if the mouse button was pressed during the current input tick. */
    bool slayer3d_input_is_mouse_button_pressed(const slayer3d_input_manager *input, Uint8 button);

    /** @brief Return true if the mouse button was released during the current input tick. */
    bool slayer3d_input_is_mouse_button_released(const slayer3d_input_manager *input, Uint8 button);

    /**
     * @brief Return true if any button-like input or action was pressed this tick.
     *
     * This supports generic flows such as splash screens where "press anything
     * to continue" should work without binding every possible key to a
     * game-specific action.
     */
    bool slayer3d_input_any_pressed(const slayer3d_input_manager *input);

    /** @brief Return raw horizontal mouse motion for the current tick. */
    float slayer3d_input_get_mouse_dx(const slayer3d_input_manager *input);

    /** @brief Return raw vertical mouse motion for the current tick. */
    float slayer3d_input_get_mouse_dy(const slayer3d_input_manager *input);

    /** @brief Return raw horizontal mouse wheel motion for the current tick. */
    float slayer3d_input_get_mouse_wheel_x(const slayer3d_input_manager *input);

    /** @brief Return raw vertical mouse wheel motion for the current tick. */
    float slayer3d_input_get_mouse_wheel_y(const slayer3d_input_manager *input);

    /**
     * @brief Map subsequent absolute SDL mouse positions into logical space.
     *
     * SDL mouse events report window-space coordinates. The managed game loop
     * sets this to the current letterbox transform so callers that use
     * absolute mouse positions, such as UI and editor picking, receive
     * coordinates in the authored logical viewport. Relative mouse deltas are
     * not transformed.
     *
     * logical_x = (window_x - offset_x) * scale_x
     * logical_y = (window_y - offset_y) * scale_y
     *
     * Pass scale 1 and offset 0 to restore identity mapping.
     */
    void slayer3d_input_set_mouse_position_transform(slayer3d_input_manager *input, float scale_x, float scale_y,
                                                     float offset_x, float offset_y);

    /**
     * @brief Return the latest absolute mouse position observed from SDL events.
     *
     * Coordinates are transformed by
     * slayer3d_input_set_mouse_position_transform. Returns false when no
     * absolute mouse position has been observed yet or live mouse input is
     * unavailable, such as during demo playback.
     */
    bool slayer3d_input_get_mouse_position(const slayer3d_input_manager *input, float *out_x, float *out_y);

    /**
     * @brief Build a 2D axis pair from four action IDs.
     *
     * The result is (positive_x - negative_x, positive_y - negative_y).
     */
    slayer3d_vec2 slayer3d_input_get_axis_pair(const slayer3d_input_manager *input, int negative_x_action,
                                               int positive_x_action, int negative_y_action, int positive_y_action);

    /** @brief Return the current snapshot, or NULL for a NULL manager. */
    const slayer3d_input_snapshot *slayer3d_input_get_snapshot(const slayer3d_input_manager *input);

    /**
     * @brief Return UTF-8 text entered during the current input tick.
     *
     * This contains SDL text-input event payloads accumulated before the most
     * recent slayer3d_input_update() call. The pointer is owned by @p input and
     * remains valid until the next update. It is an empty string when no text
     * was entered or during demo playback.
     */
    const char *slayer3d_input_get_text_input(const slayer3d_input_manager *input);

    /* ================================================================== */
    /* Default bindings                                                    */
    /* ================================================================== */

    /**
     * @brief Register and bind the standard first-person action set.
     *
     * Registers movement, look, jump, fire, interaction, menu, and pause
     * actions. The pause action is bound to Return, keypad Return, P, and the
     * gamepad Start button so games can provide a conventional pause toggle on
     * keyboard and controller.
     */
    void slayer3d_input_bind_fps_defaults(slayer3d_input_manager *input);

    /** @brief Register and bind the standard UI navigation action set. */
    void slayer3d_input_bind_ui_defaults(slayer3d_input_manager *input);

    /* ================================================================== */
    /* Deadzone                                                           */
    /* ================================================================== */

    /**
     * @brief Set gamepad axis deadzone.
     *
     * Values are clamped to [0, 1]. Default is 0.15.
     */
    void slayer3d_input_set_deadzone(slayer3d_input_manager *input, float deadzone);

    /* ================================================================== */
    /* Demo recording and playback                                         */
    /* ================================================================== */

    /**
     * @brief Start recording input snapshots from an input manager.
     *
     * One recorder may be active per manager. The recorder grows internally as
     * snapshots are appended during slayer3d_input_update.
     */
    slayer3d_demo_recorder *slayer3d_demo_record_start(slayer3d_input_manager *input);

    /** @brief Stop recording. The recorder remains valid for saving/freeing. */
    void slayer3d_demo_record_stop(slayer3d_demo_recorder *recorder);

    /**
     * @brief Save a recorded demo to disk.
     *
     * Demo files use an explicit little-endian binary format with fixed-size
     * integer fields, IEEE-754 float32 values, and per-action flag bytes. The
     * file format does not depend on host struct padding, bool size, or native
     * endianness.
     */
    bool slayer3d_demo_save(const slayer3d_demo_recorder *recorder, const char *path, float tick_rate);

    /** @brief Return the number of snapshots in a recorder. */
    Uint32 slayer3d_demo_record_count(const slayer3d_demo_recorder *recorder);

    /** @brief Return a recorded snapshot by index, or NULL if out of range. */
    const slayer3d_input_snapshot *slayer3d_demo_record_snapshot(const slayer3d_demo_recorder *recorder, Uint32 index);

    /** @brief Free a recorder and its snapshot buffer. */
    void slayer3d_demo_record_free(slayer3d_demo_recorder *recorder);

    /** @brief Load a portable SLAYER3D demo file for playback. */
    slayer3d_demo_player *slayer3d_demo_playback_load(const char *path);

    /** @brief Start feeding recorded snapshots into an input manager. */
    void slayer3d_demo_playback_start(slayer3d_input_manager *input, slayer3d_demo_player *player);

    /** @brief Stop demo playback and resume live input. */
    void slayer3d_demo_playback_stop(slayer3d_input_manager *input);

    /** @brief Return true when playback has consumed every snapshot. */
    bool slayer3d_demo_playback_finished(const slayer3d_demo_player *player);

    /** @brief Return the demo tick rate stored in the file header. */
    float slayer3d_demo_playback_tick_rate(const slayer3d_demo_player *player);

    /** @brief Return the total snapshots loaded for playback. */
    Uint32 slayer3d_demo_playback_count(const slayer3d_demo_player *player);

    /** @brief Free a demo playback object. */
    void slayer3d_demo_playback_free(slayer3d_demo_player *player);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_INPUT_H */

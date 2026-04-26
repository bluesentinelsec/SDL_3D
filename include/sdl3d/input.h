/**
 * @file input.h
 * @brief Action-based input abstraction and demo input recording.
 *
 * SDL3D input maps physical devices to named actions. Gameplay code reads
 * action snapshots instead of raw SDL keyboard, mouse, or gamepad state. The
 * same snapshot format can be recorded and played back for deterministic
 * Quake-style demos.
 */

#ifndef SDL3D_INPUT_H
#define SDL3D_INPUT_H

#include <stdbool.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SDL3D_INPUT_MAX_ACTIONS 64
#define SDL3D_INPUT_MAX_BINDINGS 128
#define SDL3D_INPUT_ACTION_NAME_MAX 32

    /**
     * @brief Physical input source kind for an action binding.
     */
    typedef enum sdl3d_input_source
    {
        SDL3D_INPUT_KEYBOARD = 0,
        SDL3D_INPUT_MOUSE_BUTTON,
        SDL3D_INPUT_MOUSE_AXIS,
        SDL3D_INPUT_GAMEPAD_BUTTON,
        SDL3D_INPUT_GAMEPAD_AXIS,
    } sdl3d_input_source;

    /**
     * @brief Mouse axes exposed as action bindings.
     */
    typedef enum sdl3d_mouse_axis
    {
        SDL3D_MOUSE_AXIS_X = 0,   /**< Horizontal mouse motion delta. */
        SDL3D_MOUSE_AXIS_Y,       /**< Vertical mouse motion delta. */
        SDL3D_MOUSE_AXIS_WHEEL,   /**< Vertical scroll-wheel delta. */
        SDL3D_MOUSE_AXIS_WHEEL_X, /**< Horizontal scroll-wheel delta. */
    } sdl3d_mouse_axis;

    /**
     * @brief Optional keyboard modifier mask for keyboard bindings.
     */
    typedef enum sdl3d_input_modifier
    {
        SDL3D_INPUT_MOD_NONE = 0,
        SDL3D_INPUT_MOD_SHIFT = 1 << 0,
        SDL3D_INPUT_MOD_CTRL = 1 << 1,
        SDL3D_INPUT_MOD_ALT = 1 << 2,
        SDL3D_INPUT_MOD_GUI = 1 << 3,
    } sdl3d_input_modifier;

    /**
     * @brief A physical input mapped to a registered action.
     */
    typedef struct sdl3d_input_binding
    {
        int action_id;             /**< Registered action ID. */
        sdl3d_input_source source; /**< Source device/control kind. */
        int required_modifiers;    /**< sdl3d_input_modifier mask for keyboard bindings. */
        int excluded_modifiers;    /**< sdl3d_input_modifier mask that must be absent for keyboard bindings. */
        union {
            SDL_Scancode scancode;            /**< Keyboard scancode. */
            Uint8 mouse_button;               /**< SDL mouse button number. */
            sdl3d_mouse_axis mouse_axis;      /**< Mouse axis. */
            SDL_GamepadButton gamepad_button; /**< SDL gamepad button. */
            SDL_GamepadAxis gamepad_axis;     /**< SDL gamepad axis. */
        };
        float scale; /**< Input multiplier; use -1 to invert axes. */
    } sdl3d_input_binding;

    /**
     * @brief Per-action state for one input tick.
     */
    typedef struct sdl3d_action_state
    {
        bool pressed;  /**< True on the tick the action first becomes active. */
        bool released; /**< True on the tick the action becomes inactive. */
        bool held;     /**< True while the action is active. */
        float value;   /**< Analog value in [-1, 1], or 0/1 for digital input. */
    } sdl3d_action_state;

    /**
     * @brief Fixed-size input state captured for one simulation tick.
     */
    typedef struct sdl3d_input_snapshot
    {
        sdl3d_action_state actions[SDL3D_INPUT_MAX_ACTIONS];
        float mouse_dx; /**< Raw horizontal mouse motion accumulated this tick. */
        float mouse_dy; /**< Raw vertical mouse motion accumulated this tick. */
        int tick;       /**< Simulation tick number for demo synchronization. */
    } sdl3d_input_snapshot;

    /** @brief Opaque action input manager. */
    typedef struct sdl3d_input_manager sdl3d_input_manager;

    /** @brief Opaque input demo recorder. */
    typedef struct sdl3d_demo_recorder sdl3d_demo_recorder;

    /** @brief Opaque input demo playback object. */
    typedef struct sdl3d_demo_player sdl3d_demo_player;

    /* ================================================================== */
    /* Lifecycle                                                          */
    /* ================================================================== */

    /**
     * @brief Create an input manager.
     * @return A new manager, or NULL on allocation failure.
     */
    sdl3d_input_manager *sdl3d_input_create(void);

    /**
     * @brief Destroy an input manager.
     *
     * Active demo playback is detached but not freed; callers own demo players.
     * Safe to call with NULL.
     */
    void sdl3d_input_destroy(sdl3d_input_manager *input);

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
    int sdl3d_input_register_action(sdl3d_input_manager *input, const char *name);

    /**
     * @brief Find a registered action by name.
     * @return The action ID, or -1 if not found.
     */
    int sdl3d_input_find_action(const sdl3d_input_manager *input, const char *name);

    /* ================================================================== */
    /* Bindings                                                           */
    /* ================================================================== */

    /** @brief Bind a key to an action. */
    void sdl3d_input_bind_key(sdl3d_input_manager *input, int action_id, SDL_Scancode key);

    /** @brief Bind a key plus required modifiers to an action. */
    void sdl3d_input_bind_key_mod(sdl3d_input_manager *input, int action_id, SDL_Scancode key, int required_modifiers);

    /** @brief Bind a key with required and excluded modifier masks. */
    void sdl3d_input_bind_key_mod_mask(sdl3d_input_manager *input, int action_id, SDL_Scancode key,
                                       int required_modifiers, int excluded_modifiers);

    /** @brief Bind a mouse button to an action. */
    void sdl3d_input_bind_mouse_button(sdl3d_input_manager *input, int action_id, Uint8 button);

    /**
     * @brief Bind a mouse motion or wheel axis to an action.
     *
     * If the same physical axis is bound to another action with an opposite
     * scale, each binding reports only its positive half. This supports
     * directional actions such as look_left/look_right on one axis.
     */
    void sdl3d_input_bind_mouse_axis(sdl3d_input_manager *input, int action_id, sdl3d_mouse_axis axis, float scale);

    /** @brief Bind a gamepad button to an action. */
    void sdl3d_input_bind_gamepad_button(sdl3d_input_manager *input, int action_id, SDL_GamepadButton button);

    /**
     * @brief Bind a gamepad axis to an action.
     *
     * If the same physical axis is bound to another action with an opposite
     * scale, each binding reports only its positive half. This supports
     * directional actions such as move_forward/move_back on one stick axis.
     */
    void sdl3d_input_bind_gamepad_axis(sdl3d_input_manager *input, int action_id, SDL_GamepadAxis axis, float scale);

    /** @brief Remove all bindings for an action. */
    void sdl3d_input_unbind_action(sdl3d_input_manager *input, int action_id);

    /* ================================================================== */
    /* Per-frame processing                                                */
    /* ================================================================== */

    /**
     * @brief Process one SDL event.
     *
     * Call this before gameplay event handling so the input manager can track
     * key/button edges, mouse deltas, wheel deltas, and gamepad hotplug.
     */
    void sdl3d_input_process_event(sdl3d_input_manager *input, const SDL_Event *event);

    /**
     * @brief Build the current action snapshot.
     *
     * If demo playback is active, this returns the next recorded snapshot.
     * Otherwise it evaluates live bindings, records the snapshot if a recorder
     * is active, resets transient accumulators, and returns a pointer valid
     * until the next update. The tick argument is copied into the snapshot for
     * synchronization with the caller's simulation timeline.
     */
    const sdl3d_input_snapshot *sdl3d_input_update(sdl3d_input_manager *input, int tick);

    /* ================================================================== */
    /* Queries                                                            */
    /* ================================================================== */

    /** @brief Return true if the action was pressed on the current tick. */
    bool sdl3d_input_is_pressed(const sdl3d_input_manager *input, int action_id);

    /** @brief Return true if the action was released on the current tick. */
    bool sdl3d_input_is_released(const sdl3d_input_manager *input, int action_id);

    /** @brief Return true if the action is held on the current tick. */
    bool sdl3d_input_is_held(const sdl3d_input_manager *input, int action_id);

    /** @brief Return the action's current analog value. */
    float sdl3d_input_get_value(const sdl3d_input_manager *input, int action_id);

    /** @brief Return raw horizontal mouse motion for the current tick. */
    float sdl3d_input_get_mouse_dx(const sdl3d_input_manager *input);

    /** @brief Return raw vertical mouse motion for the current tick. */
    float sdl3d_input_get_mouse_dy(const sdl3d_input_manager *input);

    /**
     * @brief Build a 2D axis pair from four action IDs.
     *
     * The result is (positive_x - negative_x, positive_y - negative_y).
     */
    sdl3d_vec2 sdl3d_input_get_axis_pair(const sdl3d_input_manager *input, int negative_x_action, int positive_x_action,
                                         int negative_y_action, int positive_y_action);

    /** @brief Return the current snapshot, or NULL for a NULL manager. */
    const sdl3d_input_snapshot *sdl3d_input_get_snapshot(const sdl3d_input_manager *input);

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
    void sdl3d_input_bind_fps_defaults(sdl3d_input_manager *input);

    /** @brief Register and bind the standard UI navigation action set. */
    void sdl3d_input_bind_ui_defaults(sdl3d_input_manager *input);

    /* ================================================================== */
    /* Deadzone                                                           */
    /* ================================================================== */

    /**
     * @brief Set gamepad axis deadzone.
     *
     * Values are clamped to [0, 1]. Default is 0.15.
     */
    void sdl3d_input_set_deadzone(sdl3d_input_manager *input, float deadzone);

    /* ================================================================== */
    /* Demo recording and playback                                         */
    /* ================================================================== */

    /**
     * @brief Start recording input snapshots from an input manager.
     *
     * One recorder may be active per manager. The recorder grows internally as
     * snapshots are appended during sdl3d_input_update.
     */
    sdl3d_demo_recorder *sdl3d_demo_record_start(sdl3d_input_manager *input);

    /** @brief Stop recording. The recorder remains valid for saving/freeing. */
    void sdl3d_demo_record_stop(sdl3d_demo_recorder *recorder);

    /**
     * @brief Save a recorded demo to disk.
     *
     * Demo files use an explicit little-endian binary format with fixed-size
     * integer fields, IEEE-754 float32 values, and per-action flag bytes. The
     * file format does not depend on host struct padding, bool size, or native
     * endianness.
     */
    bool sdl3d_demo_save(const sdl3d_demo_recorder *recorder, const char *path, float tick_rate);

    /** @brief Return the number of snapshots in a recorder. */
    Uint32 sdl3d_demo_record_count(const sdl3d_demo_recorder *recorder);

    /** @brief Return a recorded snapshot by index, or NULL if out of range. */
    const sdl3d_input_snapshot *sdl3d_demo_record_snapshot(const sdl3d_demo_recorder *recorder, Uint32 index);

    /** @brief Free a recorder and its snapshot buffer. */
    void sdl3d_demo_record_free(sdl3d_demo_recorder *recorder);

    /** @brief Load a portable SDL3D demo file for playback. */
    sdl3d_demo_player *sdl3d_demo_playback_load(const char *path);

    /** @brief Start feeding recorded snapshots into an input manager. */
    void sdl3d_demo_playback_start(sdl3d_input_manager *input, sdl3d_demo_player *player);

    /** @brief Stop demo playback and resume live input. */
    void sdl3d_demo_playback_stop(sdl3d_input_manager *input);

    /** @brief Return true when playback has consumed every snapshot. */
    bool sdl3d_demo_playback_finished(const sdl3d_demo_player *player);

    /** @brief Return the demo tick rate stored in the file header. */
    float sdl3d_demo_playback_tick_rate(const sdl3d_demo_player *player);

    /** @brief Return the total snapshots loaded for playback. */
    Uint32 sdl3d_demo_playback_count(const sdl3d_demo_player *player);

    /** @brief Free a demo playback object. */
    void sdl3d_demo_playback_free(sdl3d_demo_player *player);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_INPUT_H */

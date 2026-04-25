/**
 * @file action.h
 * @brief Data-driven responses to signals — the effect side of gameplay mechanics.
 *
 * An action is a response to a signal. Actions are data, not code: a
 * designer selects an action type, fills in parameters, and wires it to
 * a signal. This is what makes actions serializable and editable in the
 * future level editor.
 *
 * Supported action types:
 *
 * - **Set property:** write a value to a property bag.
 * - **Emit signal:** emit another signal (cascading / chaining).
 * - **Start timer:** start a deferred or repeating timer (Phase 5).
 * - **Log:** debug-print a message.
 *
 * Actions are plain structs. They do not own the resources they
 * reference (property bags, strings). The caller is responsible for
 * ensuring referenced resources outlive the action.
 *
 * Usage:
 * @code
 *   // Lock a door when an alarm fires.
 *   sdl3d_action lock_door = {0};
 *   lock_door.type = SDL3D_ACTION_SET_PROPERTY;
 *   lock_door.set_property.target = door_props;
 *   lock_door.set_property.key = "locked";
 *   lock_door.set_property.value.type = SDL3D_VALUE_BOOL;
 *   lock_door.set_property.value.as_bool = true;
 *
 *   sdl3d_action_execute(&lock_door, bus, NULL);
 * @endcode
 */

#ifndef SDL3D_ACTION_H
#define SDL3D_ACTION_H

#include <stdbool.h>

#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Forward declaration — timer_pool is Phase 5. Actions that start
     * timers are no-ops when the pool pointer is NULL, so Phase 4 can
     * ship independently. */
    typedef struct sdl3d_timer_pool sdl3d_timer_pool;

    /** @brief Action types. */
    typedef enum sdl3d_action_type
    {
        SDL3D_ACTION_SET_PROPERTY, /**< Write a value to a property bag. */
        SDL3D_ACTION_EMIT_SIGNAL,  /**< Emit another signal via the bus. */
        SDL3D_ACTION_START_TIMER,  /**< Start a timer (requires Phase 5). */
        SDL3D_ACTION_LOG,          /**< Log a debug message via SDL_Log. */
    } sdl3d_action_type;

    /**
     * @brief A data-driven action — a response to a signal.
     *
     * Actions are plain value types. Zero-initialize and set the fields
     * you need. The action does not own any of the pointers it holds;
     * the caller must ensure they remain valid for the action's lifetime.
     */
    typedef struct sdl3d_action
    {
        sdl3d_action_type type;

        union {
            /**
             * @brief Set a property on a target property bag.
             *
             * The key string is NOT copied — it must remain valid for the
             * lifetime of the action. For string values, the property bag
             * copies the string on set, so the value's as_string pointer
             * only needs to be valid at execution time.
             */
            struct
            {
                sdl3d_properties *target;
                const char *key;
                sdl3d_value value;
            } set_property;

            /** @brief Emit another signal, enabling cascading mechanics. */
            struct
            {
                int signal_id;
            } emit_signal;

            /**
             * @brief Start a timer that emits a signal after a delay.
             *
             * No-op if the timer_pool argument to sdl3d_action_execute
             * is NULL (Phase 5 not yet integrated).
             */
            struct
            {
                float delay;
                int signal_id;
                bool repeating;
                float interval;
            } start_timer;

            /**
             * @brief Log a debug message via SDL_LogInfo.
             *
             * The message string is NOT copied — it must remain valid
             * for the lifetime of the action.
             */
            struct
            {
                const char *message;
            } log;
        };
    } sdl3d_action;

    /**
     * @brief Execute an action.
     *
     * Performs the effect described by the action struct:
     * - SET_PROPERTY: writes the value to the target property bag.
     * - EMIT_SIGNAL: emits the signal via the bus.
     * - START_TIMER: starts a timer via the pool (no-op if pool is NULL).
     * - LOG: logs the message via SDL_LogInfo.
     *
     * @param action     The action to execute. Must not be NULL.
     * @param bus        Signal bus for EMIT_SIGNAL actions. May be NULL
     *                   if the action does not emit signals.
     * @param timer_pool Timer pool for START_TIMER actions. May be NULL
     *                   (the action becomes a no-op).
     */
    void sdl3d_action_execute(const sdl3d_action *action, sdl3d_signal_bus *bus, sdl3d_timer_pool *timer_pool);

    /* ================================================================== */
    /* Convenience constructors                                          */
    /* ================================================================== */

    /**
     * @brief Create a set-property action for a boolean value.
     *
     * Convenience for the common case of toggling a flag.
     */
    sdl3d_action sdl3d_action_make_set_bool(sdl3d_properties *target, const char *key, bool value);

    /**
     * @brief Create a set-property action for a float value.
     */
    sdl3d_action sdl3d_action_make_set_float(sdl3d_properties *target, const char *key, float value);

    /**
     * @brief Create a set-property action for an int value.
     */
    sdl3d_action sdl3d_action_make_set_int(sdl3d_properties *target, const char *key, int value);

    /**
     * @brief Create an emit-signal action.
     */
    sdl3d_action sdl3d_action_make_emit_signal(int signal_id);

    /**
     * @brief Create a log action.
     */
    sdl3d_action sdl3d_action_make_log(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_ACTION_H */

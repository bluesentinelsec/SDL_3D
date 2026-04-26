/**
 * @file logic.h
 * @brief Signal-to-action binding runtime for designer-composable gameplay.
 *
 * The logic world is the orchestration layer above the existing signal bus,
 * action system, and timer pool. It does not replace those systems: it owns a
 * set of bindings that say "when this signal is emitted, execute this action".
 *
 * This lets gameplay be expressed as data:
 * - sensors and triggers emit signals,
 * - logic bindings execute ordered actions,
 * - actions mutate properties, emit more signals, or start timers.
 *
 * The logic world does not own the signal bus or timer pool passed at
 * creation. They must remain valid for the lifetime of the logic world.
 */

#ifndef SDL3D_LOGIC_H
#define SDL3D_LOGIC_H

#include <stdbool.h>

#include "sdl3d/action.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/timer_pool.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque signal-to-action binding runtime. */
    typedef struct sdl3d_logic_world sdl3d_logic_world;

    /**
     * @brief Create a logic world that listens to a signal bus.
     *
     * The logic world references @p bus and @p timers but does not own either
     * object. @p bus is required because bindings are implemented as signal
     * handlers. @p timers may be NULL; START_TIMER actions then become no-ops,
     * matching sdl3d_action_execute semantics.
     *
     * @param bus Signal bus used for binding dispatch. Must not be NULL.
     * @param timers Optional timer pool used by START_TIMER actions.
     * @return A new logic world, or NULL on invalid input or allocation failure.
     */
    sdl3d_logic_world *sdl3d_logic_world_create(sdl3d_signal_bus *bus, sdl3d_timer_pool *timers);

    /**
     * @brief Destroy a logic world and disconnect all of its bindings.
     *
     * The referenced signal bus and timer pool are not destroyed. Safe to call
     * with NULL. Destroying a logic world from inside an active signal emission
     * follows the same lifetime restrictions as destroying user data for any
     * signal handler: callers should avoid doing so while the bus is emitting.
     */
    void sdl3d_logic_world_destroy(sdl3d_logic_world *world);

    /**
     * @brief Bind one action to a signal.
     *
     * The action is copied by value. Pointer fields inside the action, such as
     * property bags and strings, are still caller-owned and must remain valid
     * for as long as the binding can execute.
     *
     * Bindings execute in the same deterministic order as signal-bus
     * connections: the order in which they were successfully added, interleaved
     * with any other handlers connected directly to the same signal.
     *
     * @param world Logic world that owns the binding.
     * @param signal_id Signal that should execute the action.
     * @param action Action to copy into the binding. Must not be NULL.
     * @return A binding id greater than zero, or zero on failure.
     */
    int sdl3d_logic_world_bind_action(sdl3d_logic_world *world, int signal_id, const sdl3d_action *action);

    /**
     * @brief Remove a binding by id.
     *
     * Disconnects the binding from the signal bus. No-op if @p binding_id is
     * invalid or has already been removed.
     */
    void sdl3d_logic_world_unbind_action(sdl3d_logic_world *world, int binding_id);

    /**
     * @brief Enable or disable an existing binding.
     *
     * Disabled bindings remain connected and keep their deterministic order, but
     * they skip action execution while disabled.
     *
     * @return true if the binding was found, false otherwise.
     */
    bool sdl3d_logic_world_set_binding_enabled(sdl3d_logic_world *world, int binding_id, bool enabled);

    /**
     * @brief Return whether a binding is currently enabled.
     *
     * @return false when the world or binding id is invalid.
     */
    bool sdl3d_logic_world_binding_enabled(const sdl3d_logic_world *world, int binding_id);

    /**
     * @brief Return the number of live bindings in the logic world.
     */
    int sdl3d_logic_world_binding_count(const sdl3d_logic_world *world);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_LOGIC_H */

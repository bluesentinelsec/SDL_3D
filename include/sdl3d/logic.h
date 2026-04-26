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
#include "sdl3d/actor_registry.h"
#include "sdl3d/level.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/timer_pool.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque signal-to-action binding runtime. */
    typedef struct sdl3d_logic_world sdl3d_logic_world;

    /**
     * @brief Kind of authored target reference.
     *
     * Target references are stable, serializable handles that can be resolved
     * at runtime against the logic world's target context. Actor references
     * reuse the existing actor registry. Sector references use level sector
     * indices directly or names registered with sdl3d_logic_world_set_sector_name().
     */
    typedef enum sdl3d_logic_target_kind
    {
        SDL3D_LOGIC_TARGET_NONE = 0,         /**< Invalid/no target. */
        SDL3D_LOGIC_TARGET_ACTOR_ID = 1,     /**< Actor registry id. */
        SDL3D_LOGIC_TARGET_ACTOR_NAME = 2,   /**< Actor registry name. */
        SDL3D_LOGIC_TARGET_SECTOR_INDEX = 3, /**< Sector index in the active level. */
        SDL3D_LOGIC_TARGET_SECTOR_NAME = 4,  /**< Name registered for a sector index. */
    } sdl3d_logic_target_kind;

    /**
     * @brief Authored target reference.
     *
     * Name pointers are not copied into the reference. They must remain valid
     * until resolution, or come from persistent level/editor data.
     */
    typedef struct sdl3d_logic_target_ref
    {
        sdl3d_logic_target_kind kind;
        union {
            int actor_id;            /**< Used by SDL3D_LOGIC_TARGET_ACTOR_ID. */
            const char *actor_name;  /**< Used by SDL3D_LOGIC_TARGET_ACTOR_NAME. */
            int sector_index;        /**< Used by SDL3D_LOGIC_TARGET_SECTOR_INDEX. */
            const char *sector_name; /**< Used by SDL3D_LOGIC_TARGET_SECTOR_NAME. */
        };
    } sdl3d_logic_target_ref;

    /**
     * @brief Mutable target data available to a logic world.
     *
     * The logic world copies this struct by value and never owns the pointers.
     * The caller must keep pointed-to objects valid while resolving targets.
     */
    typedef struct sdl3d_logic_target_context
    {
        sdl3d_actor_registry *registry; /**< Actor registry for actor references. May be NULL. */
        sdl3d_level *level;             /**< Active level for sector references. May be NULL. */
        sdl3d_sector *sectors;          /**< Mutable sector definitions matching @p level. May be NULL. */
        int sector_count;               /**< Number of entries in @p sectors. */
    } sdl3d_logic_target_context;

    /**
     * @brief Runtime result of resolving a target reference.
     *
     * Returned pointers are borrowed. Actor pointers remain valid until the
     * registry is mutated. Sector pointers remain valid while the caller-owned
     * sector array remains alive and unmoved.
     */
    typedef struct sdl3d_logic_resolved_target
    {
        sdl3d_logic_target_kind kind;
        union {
            sdl3d_registered_actor *actor; /**< Resolved actor for actor references. */
            struct
            {
                sdl3d_level *level;   /**< Level supplied in the target context. */
                sdl3d_sector *sector; /**< Resolved sector definition. */
                int sector_index;     /**< Resolved sector index. */
            } sector;
        };
    } sdl3d_logic_resolved_target;

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

    /* ================================================================== */
    /* Target references                                                  */
    /* ================================================================== */

    /** @brief Create an actor-id target reference. */
    sdl3d_logic_target_ref sdl3d_logic_target_actor_id(int actor_id);

    /**
     * @brief Create an actor-name target reference.
     *
     * The name pointer is not copied into the reference.
     */
    sdl3d_logic_target_ref sdl3d_logic_target_actor_name(const char *actor_name);

    /** @brief Create a sector-index target reference. */
    sdl3d_logic_target_ref sdl3d_logic_target_sector_index(int sector_index);

    /**
     * @brief Create a sector-name target reference.
     *
     * The name pointer is not copied into the reference. Register sector names
     * on the world with sdl3d_logic_world_set_sector_name().
     */
    sdl3d_logic_target_ref sdl3d_logic_target_sector_name(const char *sector_name);

    /**
     * @brief Set or clear the target context used for resolution.
     *
     * Passing NULL clears the context. The context pointers are borrowed and
     * must remain valid while target resolution is used.
     */
    void sdl3d_logic_world_set_target_context(sdl3d_logic_world *world, const sdl3d_logic_target_context *context);

    /**
     * @brief Return the current copied target context, or NULL.
     */
    const sdl3d_logic_target_context *sdl3d_logic_world_get_target_context(const sdl3d_logic_world *world);

    /**
     * @brief Assign a stable editor-facing name to a sector index.
     *
     * The name is copied by the logic world. Names are unique: assigning an
     * existing name to a new index moves that name, and assigning a new name to
     * an existing index replaces the previous name for that index.
     *
     * @return true on success, false on invalid input or allocation failure.
     */
    bool sdl3d_logic_world_set_sector_name(sdl3d_logic_world *world, int sector_index, const char *name);

    /**
     * @brief Remove all sector name aliases from the logic world.
     */
    void sdl3d_logic_world_clear_sector_names(sdl3d_logic_world *world);

    /**
     * @brief Find a sector index by registered sector name.
     *
     * @return The sector index, or -1 if the name is missing or invalid.
     */
    int sdl3d_logic_world_find_sector_index(const sdl3d_logic_world *world, const char *name);

    /**
     * @brief Resolve an authored target reference against the world context.
     *
     * On failure, @p out_target is reset to SDL3D_LOGIC_TARGET_NONE when
     * non-NULL. Resolution fails when required context is missing, the target
     * does not exist, or a sector index is outside the current sector count.
     */
    bool sdl3d_logic_world_resolve_target(const sdl3d_logic_world *world, const sdl3d_logic_target_ref *ref,
                                          sdl3d_logic_resolved_target *out_target);

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

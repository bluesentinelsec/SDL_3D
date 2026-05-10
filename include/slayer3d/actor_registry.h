/**
 * @file actor_registry.h
 * @brief Unified game object table with properties and triggers.
 *
 * The actor registry is the central table of all game objects: players,
 * enemies, doors, sensors, pickups, lights. Each registered actor has a
 * unique ID, a human-readable name, a property bag for arbitrary state,
 * a world position, and optional trigger attachments.
 *
 * Visual representation (3D model actors, sprite actors) is managed by
 * the existing slayer3d_scene and slayer3d_sprite_scene systems. The registry
 * does not own or draw visuals — it owns identity, state, and behavior.
 *
 * The registry is the data model the future level editor will
 * serialize and deserialize.
 *
 * Usage:
 * @code
 *   slayer3d_actor_registry *reg = slayer3d_actor_registry_create();
 *
 *   slayer3d_registered_actor *door = slayer3d_actor_registry_add(reg, "door_1");
 *   slayer3d_properties_set_bool(door->props, "locked", false);
 *   door->position = slayer3d_vec3_make(10, 0, 15);
 *
 *   slayer3d_registered_actor *sensor = slayer3d_actor_registry_add(reg, "sensor_1");
 *   sensor->position = slayer3d_vec3_make(10, 1.5, 15);
 *   sensor->triggers[0] = (slayer3d_trigger){...};
 *   sensor->trigger_count = 1;
 *
 *   // Each frame:
 *   slayer3d_actor_registry_update(reg, bus, player_position);
 *
 *   slayer3d_actor_registry_destroy(reg);
 * @endcode
 */

#ifndef SLAYER3D_ACTOR_REGISTRY_H
#define SLAYER3D_ACTOR_REGISTRY_H

#include <stdbool.h>

#include "slayer3d/properties.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/trigger.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SLAYER3D_ACTOR_MAX_TRIGGERS 8

    /**
     * @brief A registered actor — a game object with identity, state, and behavior.
     *
     * The registry owns the actor and its property bag. The caller may
     * read and write all fields directly. Trigger attachments are inline
     * (no heap allocation) with a fixed maximum per actor.
     */
    typedef struct slayer3d_registered_actor
    {
        int id;                     /**< Unique ID assigned by the registry. */
        const char *name;           /**< Human-readable name (owned copy). */
        slayer3d_properties *props; /**< Per-actor state (owned by registry). */
        slayer3d_vec3 position;     /**< World-space position. */
        int sector_id;              /**< Sector for portal culling, -1 = none. */
        bool active;                /**< Inactive actors are skipped during update. */

        /** @brief Inline trigger array. Set trigger_count to activate. */
        slayer3d_trigger triggers[SLAYER3D_ACTOR_MAX_TRIGGERS];
        int trigger_count;
    } slayer3d_registered_actor;

    /** @brief Opaque actor registry handle. */
    typedef struct slayer3d_actor_registry slayer3d_actor_registry;

    /* ================================================================== */
    /* Lifecycle                                                          */
    /* ================================================================== */

    /**
     * @brief Create an empty actor registry.
     * @return A new registry, or NULL on allocation failure.
     */
    slayer3d_actor_registry *slayer3d_actor_registry_create(void);

    /**
     * @brief Destroy a registry and all registered actors.
     *
     * All property bags are destroyed. Safe to call with NULL.
     */
    void slayer3d_actor_registry_destroy(slayer3d_actor_registry *reg);

    /* ================================================================== */
    /* Actor management                                                   */
    /* ================================================================== */

    /**
     * @brief Register a new actor with the given name.
     *
     * The name is copied internally. A fresh property bag is created.
     * The actor starts active with position (0,0,0) and sector_id -1.
     *
     * @return Pointer to the new actor (valid until the next remove or
     *         destroy), or NULL on failure.
     */
    slayer3d_registered_actor *slayer3d_actor_registry_add(slayer3d_actor_registry *reg, const char *name);

    /**
     * @brief Remove and free a registered actor by ID.
     *
     * The actor's property bag is destroyed. No-op if the ID is invalid.
     */
    void slayer3d_actor_registry_remove(slayer3d_actor_registry *reg, int actor_id);

    /**
     * @brief Find a registered actor by name.
     *
     * Returns the first actor with a matching name, or NULL if not found.
     * O(n) scan — use sparingly outside initialization.
     */
    slayer3d_registered_actor *slayer3d_actor_registry_find(const slayer3d_actor_registry *reg, const char *name);

    /**
     * @brief Get a registered actor by ID.
     *
     * Returns NULL if the ID is invalid or the actor has been removed.
     */
    slayer3d_registered_actor *slayer3d_actor_registry_get(const slayer3d_actor_registry *reg, int actor_id);

    /**
     * @brief Get the number of active actors in the registry.
     */
    int slayer3d_actor_registry_count(const slayer3d_actor_registry *reg);

    /* ================================================================== */
    /* Per-frame update                                                   */
    /* ================================================================== */

    /**
     * @brief Evaluate all triggers on all active actors.
     *
     * For each active actor with triggers:
     * - Spatial triggers are tested against @p test_point (typically the
     *   player position).
     * - Property triggers are tested against their source property bag.
     * - All triggers are evaluated for edge detection and signal emission.
     *
     * Signal triggers are NOT automatically wired by this function —
     * the caller must connect a handler to the listened signal that
     * calls slayer3d_trigger_activate_signal on the appropriate trigger.
     *
     * @param reg        The actor registry.
     * @param bus        Signal bus for trigger emission.
     * @param test_point World-space point for spatial trigger tests.
     */
    void slayer3d_actor_registry_update(slayer3d_actor_registry *reg, slayer3d_signal_bus *bus,
                                        slayer3d_vec3 test_point);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_ACTOR_REGISTRY_H */

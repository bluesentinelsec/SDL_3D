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
 * the existing sdl3d_scene and sdl3d_sprite_scene systems. The registry
 * does not own or draw visuals — it owns identity, state, and behavior.
 *
 * The registry is the data model the future level editor will
 * serialize and deserialize.
 *
 * Usage:
 * @code
 *   sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
 *
 *   sdl3d_registered_actor *door = sdl3d_actor_registry_add(reg, "door_1");
 *   sdl3d_properties_set_bool(door->props, "locked", false);
 *   door->position = sdl3d_vec3_make(10, 0, 15);
 *
 *   sdl3d_registered_actor *sensor = sdl3d_actor_registry_add(reg, "sensor_1");
 *   sensor->position = sdl3d_vec3_make(10, 1.5, 15);
 *   sensor->triggers[0] = (sdl3d_trigger){...};
 *   sensor->trigger_count = 1;
 *
 *   // Each frame:
 *   sdl3d_actor_registry_update(reg, bus, player_position);
 *
 *   sdl3d_actor_registry_destroy(reg);
 * @endcode
 */

#ifndef SDL3D_ACTOR_REGISTRY_H
#define SDL3D_ACTOR_REGISTRY_H

#include <stdbool.h>

#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/trigger.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SDL3D_ACTOR_MAX_TRIGGERS 8

    /**
     * @brief A registered actor — a game object with identity, state, and behavior.
     *
     * The registry owns the actor and its property bag. The caller may
     * read and write all fields directly. Trigger attachments are inline
     * (no heap allocation) with a fixed maximum per actor.
     */
    typedef struct sdl3d_registered_actor
    {
        int id;                  /**< Unique ID assigned by the registry. */
        const char *name;        /**< Human-readable name (owned copy). */
        sdl3d_properties *props; /**< Per-actor state (owned by registry). */
        sdl3d_vec3 position;     /**< World-space position. */
        int sector_id;           /**< Sector for portal culling, -1 = none. */
        bool active;             /**< Inactive actors are skipped during update. */

        /** @brief Inline trigger array. Set trigger_count to activate. */
        sdl3d_trigger triggers[SDL3D_ACTOR_MAX_TRIGGERS];
        int trigger_count;
    } sdl3d_registered_actor;

    /** @brief Opaque actor registry handle. */
    typedef struct sdl3d_actor_registry sdl3d_actor_registry;

    /* ================================================================== */
    /* Lifecycle                                                          */
    /* ================================================================== */

    /**
     * @brief Create an empty actor registry.
     * @return A new registry, or NULL on allocation failure.
     */
    sdl3d_actor_registry *sdl3d_actor_registry_create(void);

    /**
     * @brief Destroy a registry and all registered actors.
     *
     * All property bags are destroyed. Safe to call with NULL.
     */
    void sdl3d_actor_registry_destroy(sdl3d_actor_registry *reg);

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
    sdl3d_registered_actor *sdl3d_actor_registry_add(sdl3d_actor_registry *reg, const char *name);

    /**
     * @brief Remove and free a registered actor by ID.
     *
     * The actor's property bag is destroyed. No-op if the ID is invalid.
     */
    void sdl3d_actor_registry_remove(sdl3d_actor_registry *reg, int actor_id);

    /**
     * @brief Find a registered actor by name.
     *
     * Returns the first actor with a matching name, or NULL if not found.
     * O(n) scan — use sparingly outside initialization.
     */
    sdl3d_registered_actor *sdl3d_actor_registry_find(const sdl3d_actor_registry *reg, const char *name);

    /**
     * @brief Get a registered actor by ID.
     *
     * Returns NULL if the ID is invalid or the actor has been removed.
     */
    sdl3d_registered_actor *sdl3d_actor_registry_get(const sdl3d_actor_registry *reg, int actor_id);

    /**
     * @brief Get the number of active actors in the registry.
     */
    int sdl3d_actor_registry_count(const sdl3d_actor_registry *reg);

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
     * calls sdl3d_trigger_activate_signal on the appropriate trigger.
     *
     * @param reg        The actor registry.
     * @param bus        Signal bus for trigger emission.
     * @param test_point World-space point for spatial trigger tests.
     */
    void sdl3d_actor_registry_update(sdl3d_actor_registry *reg, sdl3d_signal_bus *bus, sdl3d_vec3 test_point);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_ACTOR_REGISTRY_H */

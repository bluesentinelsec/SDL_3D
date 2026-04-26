/**
 * @file teleporter.h
 * @brief Signal-driven teleport trigger volumes.
 *
 * Teleporters combine a source volume with a destination transform. They do
 * not directly move a player or actor; instead they emit a teleport signal
 * with a property payload. Game code receives the signal and applies the
 * destination to the appropriate object. This keeps teleport detection,
 * signaling, and game-specific movement ownership separate.
 */

#ifndef SDL3D_TELEPORTER_H
#define SDL3D_TELEPORTER_H

#include <stdbool.h>

#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Default signal emitted when a teleporter is entered. */
#define SDL3D_SIGNAL_TELEPORT 0x54454C45

    /**
     * @brief Destination transform carried by a teleport signal.
     *
     * Position is always meaningful. Yaw and pitch are optional so a teleporter
     * can either force facing, preserve current facing, or only override one
     * axis.
     */
    typedef struct sdl3d_teleport_destination
    {
        sdl3d_vec3 position; /**< Caller-defined destination position. */
        float yaw;           /**< Destination yaw in radians when use_yaw is true. */
        float pitch;         /**< Destination pitch in radians when use_pitch is true. */
        bool use_yaw;        /**< Whether yaw should replace the current yaw. */
        bool use_pitch;      /**< Whether pitch should replace the current pitch. */
    } sdl3d_teleport_destination;

    /**
     * @brief Teleporter trigger volume.
     *
     * Initialize with sdl3d_teleporter_init(), then call
     * sdl3d_teleporter_update() once per simulation tick using the point that
     * represents the object being teleported. The teleporter emits only on the
     * enter edge of source_bounds, not every frame while the point remains
     * inside.
     */
    typedef struct sdl3d_teleporter
    {
        sdl3d_bounding_box source_bounds;       /**< World-space source volume. */
        sdl3d_teleport_destination destination; /**< Payload destination. */
        int teleporter_id;                      /**< Stable game/editor id included in the payload. */
        int signal_id;                          /**< Signal to emit, or SDL3D_SIGNAL_TELEPORT by default. */
        float cooldown_seconds;                 /**< Minimum seconds before this teleporter can fire again. */
        float cooldown_remaining;               /**< Runtime cooldown state. */
        bool enabled;                           /**< Disabled teleporters never emit. */
        bool was_inside;                        /**< Runtime enter-edge state. */
    } sdl3d_teleporter;

    /**
     * @brief Initialize a teleporter with source bounds and a destination.
     *
     * The teleporter starts enabled, emits SDL3D_SIGNAL_TELEPORT, and uses a
     * short default cooldown suitable for paired or nearby teleport volumes.
     */
    void sdl3d_teleporter_init(sdl3d_teleporter *teleporter, int teleporter_id, sdl3d_bounding_box source_bounds,
                               sdl3d_teleport_destination destination);

    /**
     * @brief Update a teleporter and emit when the sample point enters it.
     *
     * The emitted payload contains:
     * - teleporter_id: int
     * - destination: vec3
     * - destination_yaw: float
     * - destination_pitch: float
     * - use_yaw: bool
     * - use_pitch: bool
     *
     * @param teleporter Teleporter state.
     * @param sample_position World-space point to test.
     * @param dt Simulation delta used to reduce cooldown. Negative values are
     *        treated as zero.
     * @param bus Signal bus that receives the teleport signal.
     * @return true if a signal was emitted, false otherwise.
     */
    bool sdl3d_teleporter_update(sdl3d_teleporter *teleporter, sdl3d_vec3 sample_position, float dt,
                                 sdl3d_signal_bus *bus);

    /**
     * @brief Reset enter-edge and cooldown state without changing configuration.
     */
    void sdl3d_teleporter_reset(sdl3d_teleporter *teleporter);

    /**
     * @brief Decode a teleport signal payload into a destination transform.
     *
     * @return true when the payload contains a destination vector, false when
     *         payload or out_destination is NULL or the required key is absent.
     */
    bool sdl3d_teleport_destination_from_payload(const sdl3d_properties *payload,
                                                 sdl3d_teleport_destination *out_destination);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_TELEPORTER_H */

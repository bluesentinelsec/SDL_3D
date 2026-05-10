/**
 * @file door.h
 * @brief Runtime sliding door primitive for interactive level geometry.
 *
 * Doors are authored as one or two axis-aligned panels. Each panel has closed
 * bounds and an open offset, which lets a single primitive express vertical
 * lifts, horizontal slides, and split doors. The door API owns animation state
 * only; rendering, sounds, and game-specific activation remain caller-owned.
 */

#ifndef SLAYER3D_DOOR_H
#define SLAYER3D_DOOR_H

#include <stdbool.h>

#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SLAYER3D_DOOR_MAX_PANELS 2

    /**
     * @brief Runtime state of a sliding door.
     */
    typedef enum slayer3d_door_state
    {
        SLAYER3D_DOOR_CLOSED = 0,  /**< Door panels are fully closed. */
        SLAYER3D_DOOR_OPENING = 1, /**< Door panels are moving toward their open offsets. */
        SLAYER3D_DOOR_OPEN = 2,    /**< Door panels are fully open. */
        SLAYER3D_DOOR_CLOSING = 3, /**< Door panels are moving back to their closed bounds. */
    } slayer3d_door_state;

    /**
     * @brief Authored movement for one physical door panel.
     */
    typedef struct slayer3d_door_panel_desc
    {
        slayer3d_bounding_box closed_bounds; /**< World-space AABB when the panel is closed. */
        slayer3d_vec3 open_offset;           /**< World-space translation applied when fully open. */
    } slayer3d_door_panel_desc;

    /**
     * @brief Door initialization data.
     *
     * String pointers are borrowed and must remain valid for the door's
     * lifetime. Set stay_open_seconds to 0 or less for a door that stays open
     * until closed by gameplay code.
     */
    typedef struct slayer3d_door_desc
    {
        int door_id;                                               /**< Stable game/editor id. */
        const char *name;                                          /**< Optional stable name. */
        int panel_count;                                           /**< Number of panels, clamped to 1..2. */
        slayer3d_door_panel_desc panels[SLAYER3D_DOOR_MAX_PANELS]; /**< Panel movement definitions. */
        float open_seconds;                                        /**< Seconds needed to open; <= 0 opens instantly. */
        float close_seconds;     /**< Seconds needed to close; <= 0 closes instantly. */
        float stay_open_seconds; /**< Auto-close delay; <= 0 disables auto-close. */
        bool start_open;         /**< Whether the door starts fully open. */
    } slayer3d_door_desc;

    /**
     * @brief Runtime door instance.
     *
     * This is a plain value type. Callers may store it inline in level or actor
     * data. Fields are public for serialization/debugging, but normal gameplay
     * should prefer the helper functions below.
     */
    typedef struct slayer3d_door
    {
        int door_id;                                               /**< Stable game/editor id. */
        const char *name;                                          /**< Optional borrowed name. */
        int panel_count;                                           /**< Active panel count. */
        slayer3d_door_panel_desc panels[SLAYER3D_DOOR_MAX_PANELS]; /**< Authored panel movement. */
        float open_seconds;                                        /**< Opening duration in seconds. */
        float close_seconds;                                       /**< Closing duration in seconds. */
        float stay_open_seconds;                                   /**< Auto-close delay; <= 0 disables it. */
        float open_fraction;                                       /**< 0 closed, 1 open. */
        float hold_timer;                                          /**< Remaining auto-close hold time. */
        slayer3d_door_state state;                                 /**< Current runtime state. */
        bool enabled; /**< Disabled doors ignore open/close/toggle commands. */
    } slayer3d_door;

    /**
     * @brief Initialize a runtime door from authored data.
     *
     * Invalid input leaves @p door zeroed and disabled.
     */
    void slayer3d_door_init(slayer3d_door *door, const slayer3d_door_desc *desc);

    /** @brief Begin opening a door, or keep it open/opening. */
    bool slayer3d_door_open(slayer3d_door *door);

    /** @brief Begin closing a door, or keep it closed/closing. */
    bool slayer3d_door_close(slayer3d_door *door);

    /** @brief Open a closed/closing door or close an open/opening door. */
    bool slayer3d_door_toggle(slayer3d_door *door);

    /**
     * @brief Set the door's auto-close delay.
     *
     * Values less than or equal to zero disable auto-close. If the door is
     * already fully open, its current hold timer is reset to the new delay.
     */
    void slayer3d_door_set_auto_close_delay(slayer3d_door *door, float stay_open_seconds);

    /** @brief Advance door animation by @p dt seconds. */
    void slayer3d_door_update(slayer3d_door *door, float dt);

    /** @brief Return the door's current state. */
    slayer3d_door_state slayer3d_door_get_state(const slayer3d_door *door);

    /** @brief Return the door's current open fraction in the inclusive range [0, 1]. */
    float slayer3d_door_get_open_fraction(const slayer3d_door *door);

    /** @brief Return the number of active panels in a door. */
    int slayer3d_door_panel_count(const slayer3d_door *door);

    /**
     * @brief Get a panel's current world-space bounds.
     *
     * @return true when @p panel_index is valid and @p out_bounds was written.
     */
    bool slayer3d_door_get_panel_bounds(const slayer3d_door *door, int panel_index, slayer3d_bounding_box *out_bounds);

    /**
     * @brief Return whether a point is close enough to interact with a door.
     *
     * Distance is measured in the horizontal X/Z plane against the door's
     * closed footprint, so an open door remains interactable at its doorway.
     */
    bool slayer3d_door_point_in_interaction_range(const slayer3d_door *door, slayer3d_vec3 point, float range);

    /**
     * @brief Test whether a vertical cylinder overlaps any current door panel.
     *
     * The cylinder is described by an eye position, total height, and horizontal
     * radius. This matches typical FPS mover state where position is camera/eye
     * height and the feet are at eye_y - height.
     */
    bool slayer3d_door_intersects_cylinder(const slayer3d_door *door, slayer3d_vec3 eye_position, float height,
                                           float radius);

    /**
     * @brief Push a vertical cylinder out of current door panel bounds.
     *
     * This is intended as a post-move dynamic obstacle resolver. It adjusts
     * only X/Z position and leaves vertical movement to the caller's mover.
     *
     * @return true when @p eye_position was changed.
     */
    bool slayer3d_door_resolve_cylinder(const slayer3d_door *door, slayer3d_vec3 *eye_position, float height,
                                        float radius);

#ifdef __cplusplus
}
#endif

#endif

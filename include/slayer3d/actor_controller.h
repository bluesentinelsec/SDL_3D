/**
 * @file actor_controller.h
 * @brief Reusable actor controller primitives for gameplay logic.
 *
 * Actor controllers update registered actor state without owning rendering.
 * This keeps gameplay state in slayer3d_actor_registry while allowing callers to
 * adapt visuals, physics, and game-specific movement through narrow callbacks.
 */

#ifndef SLAYER3D_ACTOR_CONTROLLER_H
#define SLAYER3D_ACTOR_CONTROLLER_H

#include <stdbool.h>

#include "slayer3d/actor_registry.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SLAYER3D_ACTOR_PATROL_MAX_WAYPOINTS 16

    typedef struct slayer3d_actor_patrol_controller slayer3d_actor_patrol_controller;

    /** @brief Patrol path traversal mode. */
    typedef enum slayer3d_actor_patrol_mode
    {
        SLAYER3D_ACTOR_PATROL_LOOP = 0,     /**< After the last waypoint, continue at waypoint 0. */
        SLAYER3D_ACTOR_PATROL_PING_PONG = 1 /**< Reverse direction at each endpoint. */
    } slayer3d_actor_patrol_mode;

    /** @brief Runtime patrol state exposed through actor properties. */
    typedef enum slayer3d_actor_patrol_state
    {
        SLAYER3D_ACTOR_PATROL_IDLE = 0, /**< Actor is waiting before the next movement segment. */
        SLAYER3D_ACTOR_PATROL_WALK = 1  /**< Actor is moving toward target_waypoint. */
    } slayer3d_actor_patrol_state;

    /**
     * @brief Signal ids emitted by a patrol controller.
     *
     * Signal ids <= 0 are treated as disabled. Payloads include
     * controller_id, actor_id, actor_name, state, target_waypoint, and
     * waypoint_index when a waypoint is involved.
     */
    typedef struct slayer3d_actor_patrol_signals
    {
        int waypoint_reached; /**< Emitted after reaching a waypoint. */
        int loop_completed;   /**< Emitted after completing a full patrol cycle. */
        int idle_started;     /**< Emitted after transitioning to idle. */
        int walk_started;     /**< Emitted after transitioning to walk. */
    } slayer3d_actor_patrol_signals;

    /**
     * @brief Authored patrol configuration.
     */
    typedef struct slayer3d_actor_patrol_config
    {
        float speed;                           /**< Movement speed in world units per second. */
        float wait_time;                       /**< Idle wait before each movement segment, in seconds. */
        float arrival_radius;                  /**< Distance at which a waypoint is considered reached. */
        slayer3d_actor_patrol_mode mode;       /**< Loop or ping-pong traversal. */
        bool start_idle;                       /**< True to start in idle before first movement. */
        slayer3d_actor_patrol_signals signals; /**< Optional signal ids for controller events. */
    } slayer3d_actor_patrol_config;

    /**
     * @brief Stateful patrol controller for a registered actor.
     *
     * The controller owns no actor, registry, or signal bus. The actor_id is
     * resolved through the registry on each update, so the controller remains
     * stable across registry pointer changes as long as the actor id exists.
     */
    struct slayer3d_actor_patrol_controller
    {
        int controller_id; /**< Stable editor/game id included in signal payloads. */
        int actor_id;      /**< Registered actor id controlled by this patrol. */
        bool enabled;      /**< Disabled controllers keep state but do not move or emit. */

        slayer3d_vec3 waypoints[SLAYER3D_ACTOR_PATROL_MAX_WAYPOINTS]; /**< Authored path waypoints. */
        int waypoint_count;                                           /**< Number of configured waypoints. */
        int target_waypoint;                                          /**< Current waypoint index being approached. */
        int direction;                                                /**< Traversal direction, normally +1 or -1. */

        float speed;          /**< Movement speed in world units per second. */
        float wait_time;      /**< Idle wait before walking. */
        float arrival_radius; /**< Distance at which a waypoint is reached. */
        float wait_remaining; /**< Runtime idle countdown. */

        slayer3d_actor_patrol_mode mode;       /**< Path traversal mode. */
        slayer3d_actor_patrol_state state;     /**< Current patrol state. */
        slayer3d_actor_patrol_signals signals; /**< Optional signal ids. */
        bool has_left_start;                   /**< Internal cycle-completion tracking. */
    };

    /**
     * @brief Result returned from one patrol update.
     */
    typedef struct slayer3d_actor_patrol_result
    {
        bool updated;                      /**< True when a valid actor/controller was processed. */
        bool moved;                        /**< True when actor position changed. */
        bool state_changed;                /**< True when idle/walk state changed. */
        bool waypoint_reached;             /**< True when a waypoint was reached this update. */
        bool loop_completed;               /**< True when a full patrol cycle completed. */
        int reached_waypoint;              /**< Reached waypoint index, or -1 when none. */
        slayer3d_actor_patrol_state state; /**< Controller state after the update. */
        slayer3d_vec3 previous_position;   /**< Actor position before movement. */
        slayer3d_vec3 position;            /**< Actor position after movement. */
        slayer3d_vec3 movement_delta;      /**< position - previous_position. */
    } slayer3d_actor_patrol_result;

    /**
     * @brief Optional movement adapter for patrol controllers.
     *
     * The controller computes desired_position. The adapter may validate it
     * against game geometry, adjust floor height, or reject movement. Returning
     * false keeps the actor at its current position and does not mark the
     * waypoint reached.
     */
    typedef bool (*slayer3d_actor_patrol_move_fn)(void *userdata, const slayer3d_actor_patrol_controller *controller,
                                                  slayer3d_registered_actor *actor, slayer3d_vec3 desired_position,
                                                  slayer3d_vec3 *out_position);

    /**
     * @brief Return a conservative default patrol configuration.
     */
    slayer3d_actor_patrol_config slayer3d_actor_patrol_default_config(void);

    /**
     * @brief Initialize a patrol controller.
     *
     * The controller starts with no waypoints. Add at least two waypoints
     * before update for movement to occur. Passing NULL for config uses
     * slayer3d_actor_patrol_default_config().
     */
    void slayer3d_actor_patrol_controller_init(slayer3d_actor_patrol_controller *controller, int controller_id,
                                               int actor_id, const slayer3d_actor_patrol_config *config);

    /**
     * @brief Remove all waypoints and reset target state.
     */
    void slayer3d_actor_patrol_controller_clear_waypoints(slayer3d_actor_patrol_controller *controller);

    /**
     * @brief Add a waypoint to the patrol path.
     * @return true on success, false if the controller is NULL or full.
     */
    bool slayer3d_actor_patrol_controller_add_waypoint(slayer3d_actor_patrol_controller *controller,
                                                       slayer3d_vec3 waypoint);

    /**
     * @brief Restart runtime state without changing configuration or waypoints.
     */
    void slayer3d_actor_patrol_controller_reset(slayer3d_actor_patrol_controller *controller, bool start_idle);

    /**
     * @brief Enable or disable the patrol controller.
     */
    void slayer3d_actor_patrol_controller_set_enabled(slayer3d_actor_patrol_controller *controller, bool enabled);

    /**
     * @brief Return a stable lowercase name for a patrol state.
     */
    const char *slayer3d_actor_patrol_state_name(slayer3d_actor_patrol_state state);

    /**
     * @brief Update actor properties that expose patrol state.
     *
     * Exposed keys are: state, target_waypoint, patrol_enabled, enabled,
     * alerted, patrol_mode, patrol_speed, patrol_wait_time, and
     * patrol_wait_remaining. alerted is initialized to false only if the actor
     * does not already define it.
     */
    void slayer3d_actor_patrol_controller_sync_properties(const slayer3d_actor_patrol_controller *controller,
                                                          slayer3d_registered_actor *actor);

    /**
     * @brief Apply actor-authored patrol properties to a controller.
     *
     * This lets logic actions drive patrol behavior by setting properties on
     * the controlled actor. Recognized properties are:
     * - patrol_enabled: bool
     * - patrol_speed: positive float
     * - patrol_wait_time: non-negative float
     *
     * Missing properties leave the current controller values unchanged.
     */
    void slayer3d_actor_patrol_controller_apply_actor_properties(slayer3d_actor_patrol_controller *controller,
                                                                 const slayer3d_registered_actor *actor);

    /**
     * @brief Advance a patrol controller and update its registered actor.
     *
     * The registry and actor are not owned. The signal bus is optional; when
     * NULL, state and movement still update but no controller signals emit.
     */
    slayer3d_actor_patrol_result slayer3d_actor_patrol_controller_update(slayer3d_actor_patrol_controller *controller,
                                                                         slayer3d_actor_registry *registry,
                                                                         slayer3d_signal_bus *bus, float dt,
                                                                         slayer3d_actor_patrol_move_fn move_fn,
                                                                         void *move_userdata);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_ACTOR_CONTROLLER_H */

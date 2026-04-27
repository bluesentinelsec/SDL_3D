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
#include "sdl3d/camera.h"
#include "sdl3d/level.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/teleporter.h"
#include "sdl3d/timer_pool.h"
#include "sdl3d/trigger.h"

#define SDL3D_LOGIC_MAX_ENTITY_OUTPUTS 8

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
     * @brief Target-aware action type for designer-authored gameplay logic.
     *
     * These actions extend the lower-level sdl3d_action vocabulary with effects
     * that need logic-world target resolution. Existing sdl3d_action values are
     * still supported through SDL3D_LOGIC_ACTION_CORE.
     */
    typedef enum sdl3d_logic_action_type
    {
        SDL3D_LOGIC_ACTION_NONE = 0,                 /**< Invalid/no action. */
        SDL3D_LOGIC_ACTION_CORE = 1,                 /**< Execute an embedded sdl3d_action. */
        SDL3D_LOGIC_ACTION_SET_ACTOR_ACTIVE = 2,     /**< Set a resolved actor's active flag. */
        SDL3D_LOGIC_ACTION_TOGGLE_ACTOR_ACTIVE = 3,  /**< Toggle a resolved actor's active flag. */
        SDL3D_LOGIC_ACTION_SET_ACTOR_PROPERTY = 4,   /**< Set a property on a resolved actor. */
        SDL3D_LOGIC_ACTION_SET_SECTOR_PUSH = 5,      /**< Set a sector push/current velocity. */
        SDL3D_LOGIC_ACTION_SET_SECTOR_DAMAGE = 6,    /**< Set non-negative sector damage per second. */
        SDL3D_LOGIC_ACTION_SET_SECTOR_AMBIENT = 7,   /**< Set a non-negative ambient sound id. */
        SDL3D_LOGIC_ACTION_TELEPORT_PLAYER = 8,      /**< Request a game-owned player teleport. */
        SDL3D_LOGIC_ACTION_SET_ACTIVE_CAMERA = 9,    /**< Request a game-owned active camera override. */
        SDL3D_LOGIC_ACTION_RESTORE_CAMERA = 10,      /**< Request restoration of the game-owned camera. */
        SDL3D_LOGIC_ACTION_SET_AMBIENT = 11,         /**< Request a game-owned ambient audio transition. */
        SDL3D_LOGIC_ACTION_TRIGGER_FEEDBACK = 12,    /**< Request a named game-owned feedback cue. */
        SDL3D_LOGIC_ACTION_DOOR_COMMAND = 13,        /**< Request a game-owned door command. */
        SDL3D_LOGIC_ACTION_SET_SECTOR_GEOMETRY = 14, /**< Request a game-owned sector geometry change. */
        SDL3D_LOGIC_ACTION_LAUNCH_PLAYER = 15        /**< Request a game-owned player launch impulse. */
    } sdl3d_logic_action_type;

    /**
     * @brief Command issued to a game-owned door.
     */
    typedef enum sdl3d_logic_door_command
    {
        SDL3D_LOGIC_DOOR_OPEN = 0,   /**< Open the target door. */
        SDL3D_LOGIC_DOOR_CLOSE = 1,  /**< Close the target door. */
        SDL3D_LOGIC_DOOR_TOGGLE = 2, /**< Toggle the target door. */
    } sdl3d_logic_door_command;

    /**
     * @brief Callback used by logic actions that request a player teleport.
     *
     * The logic world does not own player movement state, so games provide this
     * adapter when they want designer-authored signals to move the player. The
     * payload pointer is borrowed from the signal emission that triggered the
     * action and may be NULL for immediate/static execution.
     *
     * @return true when the game accepted and applied the teleport.
     */
    typedef bool (*sdl3d_logic_teleport_player_fn)(void *userdata, const sdl3d_teleport_destination *destination,
                                                   const sdl3d_properties *payload);

    /**
     * @brief Callback used by logic actions that request a camera override.
     *
     * @p camera_name may be NULL. @p camera points at the camera copied into
     * the action. The game decides how that override affects rendering.
     *
     * @return true when the game accepted and applied the camera override.
     */
    typedef bool (*sdl3d_logic_set_active_camera_fn)(void *userdata, const char *camera_name,
                                                     const sdl3d_camera3d *camera, const sdl3d_properties *payload);

    /**
     * @brief Callback used by logic actions that request camera restoration.
     *
     * @return true when the game accepted and restored normal camera behavior.
     */
    typedef bool (*sdl3d_logic_restore_camera_fn)(void *userdata, const sdl3d_properties *payload);

    /**
     * @brief Callback used by logic actions that request an ambient transition.
     *
     * The logic world does not own audio zone tables, so games provide this
     * adapter to map an ambient id to their own ambient definitions.
     *
     * @return true when the game accepted the ambient request.
     */
    typedef bool (*sdl3d_logic_set_ambient_fn)(void *userdata, int ambient_id, float fade_seconds,
                                               const sdl3d_properties *payload);

    /**
     * @brief Callback used by logic actions that request a named feedback cue.
     *
     * Feedback cues are intentionally game-owned because they may be UI flashes,
     * material changes, lights, particles, sounds, or any combination of those.
     *
     * @return true when the game accepted the feedback request.
     */
    typedef bool (*sdl3d_logic_trigger_feedback_fn)(void *userdata, const char *feedback_name, float duration_seconds,
                                                    const sdl3d_properties *payload);

    /**
     * @brief Callback used by logic actions that request a door command.
     *
     * Door storage, collision, rendering, and sound are game-owned. Logic
     * actions identify doors by id and/or name and request one command. An
     * @p auto_close_seconds value below zero means "preserve the door's
     * current authored policy", zero means "stay open", and positive values
     * request an automatic close after that many seconds.
     *
     * @return true when the game accepted the door command.
     */
    typedef bool (*sdl3d_logic_door_command_fn)(void *userdata, const char *door_name, int door_id,
                                                sdl3d_logic_door_command command, float auto_close_seconds,
                                                const sdl3d_properties *payload);

    /**
     * @brief Callback used by logic actions that request runtime sector geometry.
     *
     * The logic world can resolve a target sector, but games own the concrete
     * level variants, material palettes, lighting data, and transactional mesh
     * rebuild policy. This adapter lets authored logic drive lifts, crushers,
     * bridges, and similar moving sector primitives.
     *
     * @return true when the game accepted and committed the geometry change.
     */
    typedef bool (*sdl3d_logic_set_sector_geometry_fn)(void *userdata, int sector_index,
                                                       const sdl3d_sector_geometry *geometry,
                                                       const sdl3d_properties *payload);

    /**
     * @brief Callback used by logic actions that launch the player.
     *
     * The velocity is authored in world units per second. Games decide how to
     * map it onto their movement model; an FPS controller may apply only the Y
     * component, while another game may use the full vector.
     *
     * @return true when the game accepted the launch request.
     */
    typedef bool (*sdl3d_logic_launch_player_fn)(void *userdata, sdl3d_vec3 velocity, const sdl3d_properties *payload);

    /**
     * @brief Game-owned operations exposed to generic logic actions.
     *
     * These callbacks keep the logic world open for game-specific effects
     * without coupling it to a particular player controller, render camera, UI,
     * inventory, or health model. The struct is copied by value into the world;
     * userdata remains caller-owned.
     */
    typedef struct sdl3d_logic_game_adapters
    {
        void *userdata;                                         /**< Caller-owned data passed to every callback. */
        sdl3d_logic_teleport_player_fn teleport_player;         /**< Optional player teleport adapter. */
        sdl3d_logic_set_active_camera_fn set_active_camera;     /**< Optional camera override adapter. */
        sdl3d_logic_restore_camera_fn restore_camera;           /**< Optional camera restore adapter. */
        sdl3d_logic_set_ambient_fn set_ambient;                 /**< Optional ambient audio adapter. */
        sdl3d_logic_trigger_feedback_fn trigger_feedback;       /**< Optional named feedback adapter. */
        sdl3d_logic_door_command_fn door_command;               /**< Optional door command adapter. */
        sdl3d_logic_set_sector_geometry_fn set_sector_geometry; /**< Optional sector geometry adapter. */
        sdl3d_logic_launch_player_fn launch_player;             /**< Optional player launch adapter. */
    } sdl3d_logic_game_adapters;

    /**
     * @brief Target-aware action executed by a logic world.
     *
     * Like sdl3d_action, this is a plain value type. It owns no pointers:
     * target names, property keys, and string property values must remain valid
     * until execution. Property bags copy string values when the action runs.
     */
    typedef struct sdl3d_logic_action
    {
        sdl3d_logic_action_type type;

        union {
            /** @brief Embedded low-level action. */
            sdl3d_action core;

            /** @brief Set or toggle actor active state. */
            struct
            {
                sdl3d_logic_target_ref target;
                bool active;
            } actor_active;

            /** @brief Set a property on a resolved actor's property bag. */
            struct
            {
                sdl3d_logic_target_ref target;
                const char *key;
                sdl3d_value value;
            } actor_property;

            /** @brief Set world-space sector push/current velocity. */
            struct
            {
                sdl3d_logic_target_ref target;
                sdl3d_vec3 velocity;
            } sector_push;

            /** @brief Set sector damage per second. Negative values clamp to zero. */
            struct
            {
                sdl3d_logic_target_ref target;
                float damage_per_second;
            } sector_damage;

            /** @brief Set sector ambient sound id. Negative values clamp to zero. */
            struct
            {
                sdl3d_logic_target_ref target;
                int ambient_sound_id;
            } sector_ambient;

            /** @brief Request a game-owned player teleport. */
            struct
            {
                sdl3d_teleport_destination destination;
                bool use_signal_payload;
            } teleport_player;

            /** @brief Request a game-owned camera override. */
            struct
            {
                const char *camera_name;
                sdl3d_camera3d camera;
            } camera;

            /** @brief Request a game-owned ambient transition. */
            struct
            {
                const char *payload_key;
                int ambient_id;
                float fade_seconds;
                bool use_signal_payload;
            } ambient;

            /** @brief Request a named game-owned feedback cue. */
            struct
            {
                const char *feedback_name;
                float duration_seconds;
            } feedback;

            /** @brief Request a game-owned door command. */
            struct
            {
                const char *door_name;
                int door_id;
                sdl3d_logic_door_command command;
                float auto_close_seconds;
                bool use_signal_payload;
            } door;

            /** @brief Request a game-owned sector geometry update. */
            struct
            {
                sdl3d_logic_target_ref target;
                sdl3d_sector_geometry geometry;
            } sector_geometry;

            /** @brief Request a game-owned player launch impulse. */
            struct
            {
                sdl3d_vec3 velocity;
            } launch_player;
        };
    } sdl3d_logic_action;

    /**
     * @brief Sensor event emitted by logic sensors.
     */
    typedef enum sdl3d_logic_sensor_event
    {
        SDL3D_LOGIC_SENSOR_EVENT_NONE = 0,  /**< No signal was emitted. */
        SDL3D_LOGIC_SENSOR_EVENT_ENTER = 1, /**< Sensor condition became true. */
        SDL3D_LOGIC_SENSOR_EVENT_EXIT = 2,  /**< Sensor condition became false. */
        SDL3D_LOGIC_SENSOR_EVENT_LEVEL = 3, /**< Sensor condition is true for this update. */
    } sdl3d_logic_sensor_event;

    /**
     * @brief Result returned by a sensor update.
     */
    typedef struct sdl3d_logic_sensor_result
    {
        bool emitted;                   /**< Whether a signal was emitted. */
        bool active;                    /**< Sensor condition after the update. */
        sdl3d_logic_sensor_event event; /**< Event kind emitted, or NONE. */
    } sdl3d_logic_sensor_result;

    /**
     * @brief AABB contact sensor for point-in-volume mechanics.
     *
     * This is a reusable building block for pressure plates, surveillance
     * buttons, proximity boxes, item pickup zones, and trigger volumes.
     */
    typedef struct sdl3d_logic_contact_sensor
    {
        int sensor_id;             /**< Stable editor/game id included in payloads. */
        sdl3d_bounding_box bounds; /**< World-space sensor volume. */
        int signal_id;             /**< Signal emitted when edge rules match. */
        sdl3d_trigger_edge edge;   /**< Enter/exit/both/level emission mode. */
        bool enabled;              /**< Disabled sensors never emit. */
        bool was_inside;           /**< Previous contact state, managed by update/reset. */
    } sdl3d_logic_contact_sensor;

    /**
     * @brief Sector occupancy sensor for sector-specific enter/exit/level logic.
     *
     * The sector target can be a sector index or a sector name registered on the
     * logic world. The sample point is treated as a world-space point; pass an
     * actor's feet position when floor occupancy matters.
     */
    typedef struct sdl3d_logic_sector_sensor
    {
        int sensor_id;                 /**< Stable editor/game id included in payloads. */
        sdl3d_logic_target_ref sector; /**< Sector index/name target. */
        int signal_id;                 /**< Signal emitted when edge rules match. */
        sdl3d_trigger_edge edge;       /**< Enter/exit/both/level emission mode. */
        bool enabled;                  /**< Disabled sensors never emit. */
        bool was_inside;               /**< Previous occupancy state, managed by update/reset. */
    } sdl3d_logic_sector_sensor;

    /**
     * @brief Actor proximity sensor for distance-based mechanics.
     *
     * The actor target resolves through the logic world's actor registry. The
     * sensor is active when the sample point is within @p radius of the actor's
     * registered world position.
     */
    typedef struct sdl3d_logic_proximity_sensor
    {
        int sensor_id;                /**< Stable editor/game id included in payloads. */
        sdl3d_logic_target_ref actor; /**< Actor id/name target. */
        float radius;                 /**< Non-negative trigger radius in world units. */
        int signal_id;                /**< Signal emitted when edge rules match. */
        sdl3d_trigger_edge edge;      /**< Enter/exit/both/level emission mode. */
        bool enabled;                 /**< Disabled sensors never emit. */
        bool was_inside;              /**< Previous proximity state, managed by update/reset. */
    } sdl3d_logic_proximity_sensor;

    /**
     * @brief Result returned by a logic entity activation.
     */
    typedef struct sdl3d_logic_entity_result
    {
        bool emitted;     /**< Whether the entity emitted a signal. */
        int signal_id;    /**< Last emitted signal, or 0 when none. */
        int output_index; /**< Output slot selected, or -1 when none. */
    } sdl3d_logic_entity_result;

    /**
     * @brief Relay entity that emits each configured output in order.
     *
     * Relays are useful for fan-out composition: one input signal can trigger
     * several downstream signals/actions without a bespoke callback.
     */
    typedef struct sdl3d_logic_relay
    {
        int entity_id;                               /**< Stable editor/game id included in generated payloads. */
        int outputs[SDL3D_LOGIC_MAX_ENTITY_OUTPUTS]; /**< Signals emitted by activation. */
        int output_count;                            /**< Number of active entries in @p outputs. */
        bool enabled;                                /**< Disabled relays emit nothing. */
    } sdl3d_logic_relay;

    /**
     * @brief Toggle entity that flips boolean state and emits on/off signals.
     */
    typedef struct sdl3d_logic_toggle
    {
        int entity_id;  /**< Stable editor/game id included in payloads. */
        int on_signal;  /**< Signal emitted when state becomes true. */
        int off_signal; /**< Signal emitted when state becomes false. */
        bool state;     /**< Current toggle state. */
        bool enabled;   /**< Disabled toggles ignore activation. */
    } sdl3d_logic_toggle;

    /**
     * @brief Counter entity that emits when a threshold is reached.
     */
    typedef struct sdl3d_logic_counter
    {
        int entity_id;      /**< Stable editor/game id included in payloads. */
        int threshold;      /**< Activations required to emit. Values less than 1 clamp to 1. */
        int count;          /**< Current activation count. */
        int output_signal;  /**< Signal emitted at the threshold. */
        bool reset_on_fire; /**< If true, count resets after each emission. */
        bool fired;         /**< True after a non-resetting counter has emitted. */
        bool enabled;       /**< Disabled counters ignore activation. */
    } sdl3d_logic_counter;

    /**
     * @brief Branch entity that compares a property or payload value and emits true/false.
     *
     * The property bag, key pointer, and string value pointer are borrowed and
     * must remain valid until activation. Payload branches inspect the payload
     * passed to sdl3d_logic_branch_activate_with_payload().
     */
    typedef struct sdl3d_logic_branch
    {
        int entity_id;                 /**< Stable editor/game id included in payloads. */
        const sdl3d_properties *props; /**< Property bag to inspect. */
        const char *key;               /**< Property key to compare. */
        sdl3d_value expected;          /**< Expected value. String pointer is borrowed. */
        int true_signal;               /**< Signal emitted when comparison succeeds. */
        int false_signal;              /**< Signal emitted when comparison fails. */
        bool enabled;                  /**< Disabled branches ignore activation. */
        bool use_activation_payload;   /**< If true, compare the activation payload instead of @p props. */
    } sdl3d_logic_branch;

    /**
     * @brief Deterministic random selector that emits one configured output.
     *
     * Uses an internal deterministic PRNG state so seeded selectors are stable
     * in tests, demos, and recorded playback.
     */
    typedef struct sdl3d_logic_random
    {
        int entity_id;                               /**< Stable editor/game id included in payloads. */
        int outputs[SDL3D_LOGIC_MAX_ENTITY_OUTPUTS]; /**< Candidate output signals. */
        int output_count;                            /**< Number of active entries in @p outputs. */
        unsigned int state;                          /**< Deterministic PRNG state. */
        bool enabled;                                /**< Disabled selectors emit nothing. */
    } sdl3d_logic_random;

    /**
     * @brief Sequence entity that emits outputs in authored order.
     */
    typedef struct sdl3d_logic_sequence
    {
        int entity_id;                               /**< Stable editor/game id included in payloads. */
        int outputs[SDL3D_LOGIC_MAX_ENTITY_OUTPUTS]; /**< Ordered output signals. */
        int output_count;                            /**< Number of active entries in @p outputs. */
        int next_index;                              /**< Next output slot to emit. */
        bool loop;                                   /**< If true, wraps to the first output after the last. */
        bool enabled;                                /**< Disabled sequences emit nothing. */
    } sdl3d_logic_sequence;

    /**
     * @brief Once gate that passes only the first activation until reset.
     */
    typedef struct sdl3d_logic_once
    {
        int entity_id;     /**< Stable editor/game id included in payloads. */
        int output_signal; /**< Signal emitted on the first activation. */
        bool fired;        /**< True after the first successful activation. */
        bool enabled;      /**< Disabled gates emit nothing. */
    } sdl3d_logic_once;

    /**
     * @brief Stateful timer entity that emits when its countdown expires.
     *
     * This is intended for authored logic graphs that need a timer as an
     * inspectable entity. It emits at most once per update call, matching the
     * timer pool's spiral-of-death guard behavior.
     */
    typedef struct sdl3d_logic_timer
    {
        int entity_id;     /**< Stable editor/game id included in payloads. */
        float delay;       /**< Initial delay in seconds. Must be > 0 to start. */
        float remaining;   /**< Remaining countdown while active. */
        int output_signal; /**< Signal emitted when the timer expires. */
        bool repeating;    /**< Whether the timer repeats. */
        float interval;    /**< Repeat interval in seconds. Must be > 0 when repeating. */
        bool active;       /**< Whether the timer is currently counting down. */
        bool enabled;      /**< Disabled timers cannot be started or updated. */
    } sdl3d_logic_timer;

    /**
     * @brief Oscillating sector platform that drives runtime sector geometry.
     *
     * This entity owns the timing for moving floors, simple lifts, bobbing
     * platforms, and similar sector-based mechanics. It produces
     * SDL3D_LOGIC_ACTION_SET_SECTOR_GEOMETRY actions when the floor has moved
     * far enough to merit a mesh rebuild.
     */
    typedef struct sdl3d_logic_sector_platform
    {
        int entity_id;                 /**< Stable editor/game id. */
        sdl3d_logic_target_ref sector; /**< Sector moved by this platform. */
        float min_floor_y;             /**< Lowest floor height. */
        float max_floor_y;             /**< Highest floor height. */
        float ceil_y;                  /**< Ceiling height to preserve while moving. */
        float cycle_seconds;           /**< Full oscillation period; must be > 0. */
        float rebuild_min_delta;       /**< Minimum floor delta before issuing an update. */
        float time;                    /**< Current phase time in seconds. */
        float last_floor_y;            /**< Last floor height successfully requested. */
        bool has_last_floor_y;         /**< Whether last_floor_y is initialized. */
        bool enabled;                  /**< Disabled platforms do not update. */
    } sdl3d_logic_sector_platform;

    /**
     * @brief Result returned by a sector platform update.
     */
    typedef struct sdl3d_logic_sector_platform_result
    {
        bool attempted; /**< Whether the platform attempted a sector geometry action. */
        bool applied;   /**< Whether the action was accepted by the logic world. */
        float floor_y;  /**< Floor height computed for this update. */
    } sdl3d_logic_sector_platform_result;

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
     * @brief Set callbacks for game-owned logic actions.
     *
     * Passing NULL clears all adapters. The world copies the struct by value and
     * never owns @p adapters->userdata.
     */
    void sdl3d_logic_world_set_game_adapters(sdl3d_logic_world *world, const sdl3d_logic_game_adapters *adapters);

    /**
     * @brief Return the currently configured game adapters, or NULL.
     */
    const sdl3d_logic_game_adapters *sdl3d_logic_world_get_game_adapters(const sdl3d_logic_world *world);

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

    /* ================================================================== */
    /* Target-aware actions                                               */
    /* ================================================================== */

    /** @brief Wrap an existing low-level sdl3d_action as a logic action. */
    sdl3d_logic_action sdl3d_logic_action_make_core(sdl3d_action action);

    /** @brief Create an action that sets a resolved actor's active flag. */
    sdl3d_logic_action sdl3d_logic_action_make_set_actor_active(sdl3d_logic_target_ref target, bool active);

    /** @brief Create an action that toggles a resolved actor's active flag. */
    sdl3d_logic_action sdl3d_logic_action_make_toggle_actor_active(sdl3d_logic_target_ref target);

    /**
     * @brief Create an action that sets a property on a resolved actor.
     *
     * The key pointer and any string value pointer are not copied into the
     * action; they must remain valid until execution.
     */
    sdl3d_logic_action sdl3d_logic_action_make_set_actor_property(sdl3d_logic_target_ref target, const char *key,
                                                                  sdl3d_value value);

    /** @brief Create an action that sets a sector's push/current velocity. */
    sdl3d_logic_action sdl3d_logic_action_make_set_sector_push(sdl3d_logic_target_ref target, sdl3d_vec3 velocity);

    /**
     * @brief Create an action that sets sector damage per second.
     *
     * Negative values are clamped to zero during execution.
     */
    sdl3d_logic_action sdl3d_logic_action_make_set_sector_damage(sdl3d_logic_target_ref target,
                                                                 float damage_per_second);

    /**
     * @brief Create an action that sets a sector ambient sound id.
     *
     * Negative values are clamped to zero during execution.
     */
    sdl3d_logic_action sdl3d_logic_action_make_set_sector_ambient(sdl3d_logic_target_ref target, int ambient_sound_id);

    /**
     * @brief Create an action that teleports the player to a fixed destination.
     *
     * Execution requires a teleport_player game adapter on the logic world.
     */
    sdl3d_logic_action sdl3d_logic_action_make_teleport_player(sdl3d_teleport_destination destination);

    /**
     * @brief Create an action that teleports the player from the signal payload.
     *
     * The action decodes the payload with sdl3d_teleport_destination_from_payload().
     * Execution fails if no payload or no teleport_player adapter is available.
     */
    sdl3d_logic_action sdl3d_logic_action_make_teleport_player_from_payload(void);

    /**
     * @brief Create an action that requests a game-owned camera override.
     *
     * The camera value is copied into the action. The camera_name pointer is
     * borrowed and must remain valid while the action can execute.
     */
    sdl3d_logic_action sdl3d_logic_action_make_set_active_camera(const char *camera_name, const sdl3d_camera3d *camera);

    /**
     * @brief Create an action that requests restoration of normal camera behavior.
     */
    sdl3d_logic_action sdl3d_logic_action_make_restore_camera(void);

    /**
     * @brief Create an action that requests a fixed ambient id.
     *
     * Execution requires a set_ambient game adapter on the logic world.
     */
    sdl3d_logic_action sdl3d_logic_action_make_set_ambient(int ambient_id, float fade_seconds);

    /**
     * @brief Create an action that requests an ambient id from the signal payload.
     *
     * The action reads @p payload_key as an int. Missing payloads or missing
     * keys cause execution to fail rather than silently choosing a fallback.
     * Execution requires a set_ambient game adapter on the logic world.
     */
    sdl3d_logic_action sdl3d_logic_action_make_set_ambient_from_payload(const char *payload_key, float fade_seconds);

    /**
     * @brief Create an action that requests a named game-owned feedback cue.
     *
     * The feedback_name pointer is borrowed and must remain valid while the
     * action can execute. Negative durations are clamped to zero during
     * execution.
     */
    sdl3d_logic_action sdl3d_logic_action_make_trigger_feedback(const char *feedback_name, float duration_seconds);

    /**
     * @brief Create an action that requests a command on a fixed door target.
     *
     * Execution requires a door_command game adapter. The door_name pointer is
     * borrowed and may be NULL when door_id is sufficient for the game.
     */
    sdl3d_logic_action sdl3d_logic_action_make_door_command(const char *door_name, int door_id,
                                                            sdl3d_logic_door_command command);

    /**
     * @brief Create a door command with explicit auto-close behavior.
     *
     * Set @p auto_close_seconds below zero to preserve the target door's
     * authored setting, to zero for stay-open behavior, or above zero to close
     * automatically after that delay once the door is fully open.
     */
    sdl3d_logic_action sdl3d_logic_action_make_door_command_ex(const char *door_name, int door_id,
                                                               sdl3d_logic_door_command command,
                                                               float auto_close_seconds);

    /**
     * @brief Create an action that requests a door command from the signal payload.
     *
     * The action reads optional payload keys "door_id" and "door_name" and
     * passes them to the door_command game adapter. At least one identifier
     * should be present for game-owned lookup to succeed.
     */
    sdl3d_logic_action sdl3d_logic_action_make_door_command_from_payload(sdl3d_logic_door_command command);

    /**
     * @brief Create a payload-driven door command with explicit auto-close behavior.
     *
     * Payload keys "door_id" and "door_name" select the door. The
     * auto-close delay follows the same semantics as
     * sdl3d_logic_action_make_door_command_ex().
     */
    sdl3d_logic_action sdl3d_logic_action_make_door_command_from_payload_ex(sdl3d_logic_door_command command,
                                                                            float auto_close_seconds);

    /**
     * @brief Create an action that requests runtime geometry for a target sector.
     *
     * Execution resolves @p target as a sector and then calls the
     * set_sector_geometry game adapter. This keeps mesh rebuild ownership in
     * the game while making moving floors, ceilings, lifts, bridges, and
     * crushers designer-authorable through logic.
     */
    sdl3d_logic_action sdl3d_logic_action_make_set_sector_geometry(sdl3d_logic_target_ref target,
                                                                   sdl3d_sector_geometry geometry);

    /**
     * @brief Create an action that requests a player launch velocity.
     *
     * Execution requires a launch_player game adapter. The meaning of X/Z
     * components is game-defined; FPS games may choose to honor only Y.
     */
    sdl3d_logic_action sdl3d_logic_action_make_launch_player(sdl3d_vec3 velocity);

    /**
     * @brief Execute a target-aware action immediately.
     *
     * Core actions execute through sdl3d_action_execute using the world's signal
     * bus and timer pool. Target-aware actions resolve their target against the
     * world's current target context.
     *
     * @return true when the action was valid and applied, false when the action
     *         or target was invalid.
     */
    bool sdl3d_logic_world_execute_action(sdl3d_logic_world *world, const sdl3d_logic_action *action);

    /**
     * @brief Execute a target-aware action with an optional signal payload.
     *
     * This is the immediate-execution equivalent of a signal binding. It is
     * mainly useful for actions that consume event payloads, such as
     * SDL3D_LOGIC_ACTION_TELEPORT_PLAYER actions created with
     * sdl3d_logic_action_make_teleport_player_from_payload().
     *
     * @return true when the action was valid and applied, false otherwise.
     */
    bool sdl3d_logic_world_execute_action_with_payload(sdl3d_logic_world *world, const sdl3d_logic_action *action,
                                                       const sdl3d_properties *payload);

    /* ================================================================== */
    /* Sensors                                                            */
    /* ================================================================== */

    /**
     * @brief Initialize an AABB contact sensor.
     */
    void sdl3d_logic_contact_sensor_init(sdl3d_logic_contact_sensor *sensor, int sensor_id, sdl3d_bounding_box bounds,
                                         int signal_id, sdl3d_trigger_edge edge);

    /**
     * @brief Update a contact sensor and emit through @p world when needed.
     *
     * @p world may be NULL when the caller only needs the returned active
     * state and does not need signal emission.
     *
     * Payload keys: sensor_id, event, inside, sample_position.
     */
    sdl3d_logic_sensor_result sdl3d_logic_contact_sensor_update(sdl3d_logic_contact_sensor *sensor,
                                                                sdl3d_logic_world *world, sdl3d_vec3 sample_position);

    /**
     * @brief Reset a contact sensor's edge state.
     */
    void sdl3d_logic_contact_sensor_reset(sdl3d_logic_contact_sensor *sensor);

    /**
     * @brief Initialize a sector occupancy sensor.
     */
    void sdl3d_logic_sector_sensor_init(sdl3d_logic_sector_sensor *sensor, int sensor_id, sdl3d_logic_target_ref sector,
                                        int signal_id, sdl3d_trigger_edge edge);

    /**
     * @brief Update a sector sensor and emit through @p world when needed.
     *
     * Payload keys: sensor_id, event, inside, sample_position, sector_index.
     */
    sdl3d_logic_sensor_result sdl3d_logic_sector_sensor_update(sdl3d_logic_sector_sensor *sensor,
                                                               sdl3d_logic_world *world, sdl3d_vec3 sample_position);

    /**
     * @brief Reset a sector sensor's edge state.
     */
    void sdl3d_logic_sector_sensor_reset(sdl3d_logic_sector_sensor *sensor);

    /**
     * @brief Initialize an actor proximity sensor.
     */
    void sdl3d_logic_proximity_sensor_init(sdl3d_logic_proximity_sensor *sensor, int sensor_id,
                                           sdl3d_logic_target_ref actor, float radius, int signal_id,
                                           sdl3d_trigger_edge edge);

    /**
     * @brief Update a proximity sensor and emit through @p world when needed.
     *
     * Payload keys: sensor_id, event, inside, sample_position, actor_id,
     * actor_name, distance.
     */
    sdl3d_logic_sensor_result sdl3d_logic_proximity_sensor_update(sdl3d_logic_proximity_sensor *sensor,
                                                                  sdl3d_logic_world *world, sdl3d_vec3 sample_position);

    /**
     * @brief Reset a proximity sensor's edge state.
     */
    void sdl3d_logic_proximity_sensor_reset(sdl3d_logic_proximity_sensor *sensor);

    /* ================================================================== */
    /* Logic entities                                                     */
    /* ================================================================== */

    /**
     * @brief Emit a signal through a logic world's signal bus.
     *
     * This is a small convenience for gameplay code that owns a logic world
     * but should not need direct access to the underlying signal bus pointer.
     *
     * @return true when the signal was emitted, false on invalid input.
     */
    bool sdl3d_logic_world_emit_signal(sdl3d_logic_world *world, int signal_id, const sdl3d_properties *payload);

    /** @brief Initialize an empty relay entity. */
    void sdl3d_logic_relay_init(sdl3d_logic_relay *relay, int entity_id);

    /**
     * @brief Add an output signal to a relay.
     * @return true on success, false if the relay is NULL or full.
     */
    bool sdl3d_logic_relay_add_output(sdl3d_logic_relay *relay, int signal_id);

    /**
     * @brief Activate a relay, emitting each output in configured order.
     *
     * Relays forward @p payload unchanged to each output. The returned result
     * reports the last signal emitted.
     */
    sdl3d_logic_entity_result sdl3d_logic_relay_activate(sdl3d_logic_world *world, sdl3d_logic_relay *relay,
                                                         const sdl3d_properties *payload);

    /** @brief Initialize a toggle entity. */
    void sdl3d_logic_toggle_init(sdl3d_logic_toggle *toggle, int entity_id, bool initial_state, int on_signal,
                                 int off_signal);

    /** @brief Activate a toggle, flipping state and emitting on/off. */
    sdl3d_logic_entity_result sdl3d_logic_toggle_activate(sdl3d_logic_world *world, sdl3d_logic_toggle *toggle);

    /** @brief Reset a toggle to an explicit state without emitting. */
    void sdl3d_logic_toggle_reset(sdl3d_logic_toggle *toggle, bool state);

    /** @brief Initialize a counter entity. */
    void sdl3d_logic_counter_init(sdl3d_logic_counter *counter, int entity_id, int threshold, int output_signal,
                                  bool reset_on_fire);

    /** @brief Activate a counter and emit when its threshold is reached. */
    sdl3d_logic_entity_result sdl3d_logic_counter_activate(sdl3d_logic_world *world, sdl3d_logic_counter *counter);

    /** @brief Reset a counter to zero and clear its fired state. */
    void sdl3d_logic_counter_reset(sdl3d_logic_counter *counter);

    /** @brief Initialize a property branch entity. */
    void sdl3d_logic_branch_init(sdl3d_logic_branch *branch, int entity_id, const sdl3d_properties *props,
                                 const char *key, sdl3d_value expected, int true_signal, int false_signal);

    /** @brief Initialize a branch entity that compares the activation payload. */
    void sdl3d_logic_branch_init_payload(sdl3d_logic_branch *branch, int entity_id, const char *key,
                                         sdl3d_value expected, int true_signal, int false_signal);

    /** @brief Activate a branch and emit true_signal or false_signal. */
    sdl3d_logic_entity_result sdl3d_logic_branch_activate(sdl3d_logic_world *world, sdl3d_logic_branch *branch);

    /** @brief Activate a branch with an optional signal payload. */
    sdl3d_logic_entity_result sdl3d_logic_branch_activate_with_payload(sdl3d_logic_world *world,
                                                                       sdl3d_logic_branch *branch,
                                                                       const sdl3d_properties *payload);

    /** @brief Initialize a deterministic random selector. */
    void sdl3d_logic_random_init(sdl3d_logic_random *random, int entity_id, unsigned int seed);

    /**
     * @brief Add a candidate output signal to a random selector.
     * @return true on success, false if the selector is NULL or full.
     */
    bool sdl3d_logic_random_add_output(sdl3d_logic_random *random, int signal_id);

    /** @brief Activate a random selector and emit one candidate output. */
    sdl3d_logic_entity_result sdl3d_logic_random_activate(sdl3d_logic_world *world, sdl3d_logic_random *random);

    /** @brief Reset a random selector's deterministic PRNG seed. */
    void sdl3d_logic_random_reset(sdl3d_logic_random *random, unsigned int seed);

    /** @brief Initialize an ordered sequence entity. */
    void sdl3d_logic_sequence_init(sdl3d_logic_sequence *sequence, int entity_id, bool loop);

    /**
     * @brief Add an output signal to a sequence.
     * @return true on success, false if the sequence is NULL or full.
     */
    bool sdl3d_logic_sequence_add_output(sdl3d_logic_sequence *sequence, int signal_id);

    /** @brief Activate a sequence and emit its next output. */
    sdl3d_logic_entity_result sdl3d_logic_sequence_activate(sdl3d_logic_world *world, sdl3d_logic_sequence *sequence);

    /** @brief Reset a sequence to its first output without emitting. */
    void sdl3d_logic_sequence_reset(sdl3d_logic_sequence *sequence);

    /** @brief Initialize a once gate. */
    void sdl3d_logic_once_init(sdl3d_logic_once *once, int entity_id, int output_signal);

    /** @brief Activate a once gate. Emits only the first time until reset. */
    sdl3d_logic_entity_result sdl3d_logic_once_activate(sdl3d_logic_world *world, sdl3d_logic_once *once);

    /** @brief Reset a once gate so it can emit again. */
    void sdl3d_logic_once_reset(sdl3d_logic_once *once);

    /** @brief Initialize a timer entity. */
    void sdl3d_logic_timer_init(sdl3d_logic_timer *timer, int entity_id, float delay, int output_signal, bool repeating,
                                float interval);

    /**
     * @brief Start or restart a timer entity.
     * @return true when the timer was started, false on invalid input.
     */
    bool sdl3d_logic_timer_start(sdl3d_logic_timer *timer);

    /**
     * @brief Update a timer entity by elapsed seconds and emit on expiry.
     */
    sdl3d_logic_entity_result sdl3d_logic_timer_update(sdl3d_logic_world *world, sdl3d_logic_timer *timer, float dt);

    /** @brief Stop an active timer entity without emitting. */
    void sdl3d_logic_timer_stop(sdl3d_logic_timer *timer);

    /** @brief Return whether the timer entity is currently counting down. */
    bool sdl3d_logic_timer_active(const sdl3d_logic_timer *timer);

    /**
     * @brief Initialize an oscillating sector platform.
     *
     * The platform uses a smooth sine cycle: it starts at @p min_floor_y,
     * reaches @p max_floor_y halfway through the cycle, then returns. Callers
     * can seed time or last_floor_y after initialization when restoring saved
     * state or matching an existing sector.
     */
    void sdl3d_logic_sector_platform_init(sdl3d_logic_sector_platform *platform, int entity_id,
                                          sdl3d_logic_target_ref sector, float min_floor_y, float max_floor_y,
                                          float ceil_y, float cycle_seconds, float rebuild_min_delta);

    /**
     * @brief Update an oscillating sector platform.
     *
     * When enough floor motion has accumulated, this executes a sector geometry
     * action through @p world. The return value distinguishes no-op frames from
     * attempted updates and failed adapter calls.
     */
    sdl3d_logic_sector_platform_result sdl3d_logic_sector_platform_update(sdl3d_logic_world *world,
                                                                          sdl3d_logic_sector_platform *platform,
                                                                          float dt);

    /**
     * @brief Bind a relay entity to activate when a signal is emitted.
     *
     * The entity pointer is borrowed and must remain valid while the binding is
     * live. Entity bindings execute in signal connection order.
     *
     * @return A binding id greater than zero, or zero on failure.
     */
    int sdl3d_logic_world_bind_relay(sdl3d_logic_world *world, int signal_id, sdl3d_logic_relay *relay);

    /** @brief Bind a toggle entity to activate when a signal is emitted. */
    int sdl3d_logic_world_bind_toggle(sdl3d_logic_world *world, int signal_id, sdl3d_logic_toggle *toggle);

    /** @brief Bind a counter entity to activate when a signal is emitted. */
    int sdl3d_logic_world_bind_counter(sdl3d_logic_world *world, int signal_id, sdl3d_logic_counter *counter);

    /** @brief Bind a branch entity to activate when a signal is emitted. */
    int sdl3d_logic_world_bind_branch(sdl3d_logic_world *world, int signal_id, sdl3d_logic_branch *branch);

    /** @brief Bind a random selector entity to activate when a signal is emitted. */
    int sdl3d_logic_world_bind_random(sdl3d_logic_world *world, int signal_id, sdl3d_logic_random *random);

    /** @brief Bind a sequence entity to activate when a signal is emitted. */
    int sdl3d_logic_world_bind_sequence(sdl3d_logic_world *world, int signal_id, sdl3d_logic_sequence *sequence);

    /** @brief Bind a once gate entity to activate when a signal is emitted. */
    int sdl3d_logic_world_bind_once(sdl3d_logic_world *world, int signal_id, sdl3d_logic_once *once);

    /**
     * @brief Remove an entity binding by id.
     *
     * Disconnects the binding from the signal bus. No-op if @p binding_id is
     * invalid or has already been removed.
     */
    void sdl3d_logic_world_unbind_entity(sdl3d_logic_world *world, int binding_id);

    /**
     * @brief Enable or disable an entity binding.
     *
     * Disabled entity bindings remain connected and keep their deterministic
     * order, but they skip entity activation while disabled.
     */
    bool sdl3d_logic_world_set_entity_binding_enabled(sdl3d_logic_world *world, int binding_id, bool enabled);

    /**
     * @brief Return whether an entity binding is currently enabled.
     *
     * @return false when the world or binding id is invalid.
     */
    bool sdl3d_logic_world_entity_binding_enabled(const sdl3d_logic_world *world, int binding_id);

    /**
     * @brief Return the number of live entity bindings in the logic world.
     */
    int sdl3d_logic_world_entity_binding_count(const sdl3d_logic_world *world);

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
     * @brief Bind one target-aware logic action to a signal.
     *
     * The action is copied by value. Pointers stored inside the action are not
     * copied and must remain valid while the binding can execute.
     *
     * @return A binding id greater than zero, or zero on failure.
     */
    int sdl3d_logic_world_bind_logic_action(sdl3d_logic_world *world, int signal_id, const sdl3d_logic_action *action);

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

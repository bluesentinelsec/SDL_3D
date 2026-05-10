/**
 * @file trigger.h
 * @brief Condition detectors that emit signals when criteria are met.
 *
 * A trigger is a condition that, when met, emits a signal via the signal
 * bus. Triggers only detect — they never act. This separation is what
 * makes the system composable: the response to a trigger is defined
 * elsewhere by connecting handlers to the emitted signal.
 *
 * Three trigger types are supported:
 *
 * - **Spatial:** a point enters or exits a bounding box (motion sensor).
 * - **Property:** a numeric property crosses a threshold (health <= 0).
 * - **Signal:** fires in response to another signal (chaining).
 *
 * Edge detection is built in. Most game triggers are edge-triggered
 * ("fire once when the player enters the zone"), not level-triggered
 * ("fire every frame while inside"). All four modes are supported.
 *
 * Triggers are plain structs, not heap-allocated opaque types. This
 * makes them embeddable in entity structs and trivially serializable
 * for the future level editor.
 *
 * Usage:
 * @code
 *   // Spatial trigger: emit SIG_ALARM when a point enters the zone.
 *   slayer3d_trigger sensor = {0};
 *   sensor.type = SLAYER3D_TRIGGER_SPATIAL;
 *   sensor.edge = SLAYER3D_TRIGGER_EDGE_ENTER;
 *   sensor.emit_signal_id = SIG_ALARM;
 *   sensor.enabled = true;
 *   sensor.spatial.zone = (slayer3d_bounding_box){{8,0,13}, {12,3,17}};
 *
 *   // Each frame:
 *   slayer3d_trigger_test_spatial(&sensor, player_position);
 *   slayer3d_trigger_evaluate(&sensor, bus);
 * @endcode
 */

#ifndef SLAYER3D_TRIGGER_H
#define SLAYER3D_TRIGGER_H

#include <stdbool.h>

#include "slayer3d/properties.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Trigger condition type. */
    typedef enum slayer3d_trigger_type
    {
        SLAYER3D_TRIGGER_SPATIAL,  /**< Point enters/exits a bounding box. */
        SLAYER3D_TRIGGER_PROPERTY, /**< A numeric property crosses a threshold. */
        SLAYER3D_TRIGGER_SIGNAL,   /**< Fires in response to another signal. */
    } slayer3d_trigger_type;

    /**
     * @brief Edge detection mode for trigger firing.
     *
     * Controls when the trigger's signal is emitted relative to the
     * condition transitioning between true and false.
     */
    typedef enum slayer3d_trigger_edge
    {
        SLAYER3D_TRIGGER_EDGE_ENTER, /**< Fire once when condition becomes true. */
        SLAYER3D_TRIGGER_EDGE_EXIT,  /**< Fire once when condition becomes false. */
        SLAYER3D_TRIGGER_EDGE_BOTH,  /**< Fire on both enter and exit transitions. */
        SLAYER3D_TRIGGER_LEVEL,      /**< Fire every evaluation while condition is true. */
    } slayer3d_trigger_edge;

    /** @brief Comparison operators for property triggers. */
    typedef enum slayer3d_compare_op
    {
        SLAYER3D_CMP_EQ, /**< Equal. */
        SLAYER3D_CMP_NE, /**< Not equal. */
        SLAYER3D_CMP_LT, /**< Less than. */
        SLAYER3D_CMP_LE, /**< Less than or equal. */
        SLAYER3D_CMP_GT, /**< Greater than. */
        SLAYER3D_CMP_GE, /**< Greater than or equal. */
    } slayer3d_compare_op;

    /**
     * @brief A trigger — a condition that emits a signal when met.
     *
     * Triggers are plain value types. Zero-initialize and set the fields
     * you need. The `enabled` field must be set to true for the trigger
     * to fire. The `was_active` and `active` fields are managed
     * internally by the test and evaluate functions.
     */
    typedef struct slayer3d_trigger
    {
        slayer3d_trigger_type type;
        slayer3d_trigger_edge edge;
        int emit_signal_id; /**< Signal emitted when the trigger fires. */
        bool enabled;

        /* Edge-detection state (internal, managed by test/evaluate). */
        bool was_active; /**< Condition state from the previous evaluation. */
        bool active;     /**< Condition state from the current evaluation. */

        union {
            /** @brief Spatial trigger: point-in-AABB test. */
            struct
            {
                slayer3d_bounding_box zone;
            } spatial;

            /**
             * @brief Property trigger: numeric comparison.
             *
             * Reads a float property from `source` and compares it against
             * `threshold` using `op`. For int properties, the value is
             * read as float via get_float (returns fallback on type
             * mismatch, so use float properties for thresholds). For bool
             * properties, use get_float with a threshold of 0.5 and
             * SLAYER3D_CMP_GE to test truthiness.
             */
            struct
            {
                const slayer3d_properties *source;
                const char *key; /**< Property key to read. Not copied. */
                slayer3d_compare_op op;
                float threshold;
            } property;

            /** @brief Signal trigger: fires when a specific signal is received. */
            struct
            {
                int listen_signal_id;
            } signal;
        };
    } slayer3d_trigger;

    /* ================================================================== */
    /* Condition testing                                                  */
    /* ================================================================== */

    /**
     * @brief Test a spatial trigger against a world-space point.
     *
     * Sets `trigger->active` to true if the point is inside the zone,
     * false otherwise. Does nothing if trigger is NULL, disabled, or
     * not of type SLAYER3D_TRIGGER_SPATIAL.
     */
    void slayer3d_trigger_test_spatial(slayer3d_trigger *trigger, slayer3d_vec3 point);

    /**
     * @brief Test a property trigger against its source property bag.
     *
     * Reads the float value at `trigger->property.key` from
     * `trigger->property.source` and compares it against the threshold.
     * Sets `trigger->active` accordingly. Does nothing if trigger is
     * NULL, disabled, or not of type SLAYER3D_TRIGGER_PROPERTY.
     */
    void slayer3d_trigger_test_property(slayer3d_trigger *trigger);

    /**
     * @brief Mark a signal trigger as active.
     *
     * Called when the listened signal is received. Sets `trigger->active`
     * to true. The trigger will fire on the next evaluate call. Does
     * nothing if trigger is NULL, disabled, or not of type
     * SLAYER3D_TRIGGER_SIGNAL.
     */
    void slayer3d_trigger_activate_signal(slayer3d_trigger *trigger);

    /* ================================================================== */
    /* Evaluation                                                        */
    /* ================================================================== */

    /**
     * @brief Evaluate a trigger and emit its signal if the edge condition is met.
     *
     * Compares `active` against `was_active` to detect transitions:
     * - EDGE_ENTER: emits when active transitions from false to true.
     * - EDGE_EXIT: emits when active transitions from true to false.
     * - EDGE_BOTH: emits on either transition.
     * - LEVEL: emits every call while active is true.
     *
     * After evaluation, `was_active` is updated to `active`, and for
     * signal triggers, `active` is reset to false (signal triggers are
     * pulse-based: they activate for one evaluation cycle per received
     * signal).
     *
     * Does nothing if trigger is NULL, disabled, or bus is NULL.
     */
    void slayer3d_trigger_evaluate(slayer3d_trigger *trigger, slayer3d_signal_bus *bus);

    /**
     * @brief Reset a trigger's edge-detection state.
     *
     * Sets both `was_active` and `active` to false. Useful when
     * re-enabling a trigger or resetting a level.
     */
    void slayer3d_trigger_reset(slayer3d_trigger *trigger);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_TRIGGER_H */

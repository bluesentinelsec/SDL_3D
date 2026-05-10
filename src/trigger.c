/**
 * @file trigger.c
 * @brief Trigger implementation — condition detection and edge-based emission.
 */

#include "slayer3d/trigger.h"

/* ================================================================== */
/* Spatial: point-in-AABB                                             */
/* ================================================================== */

void slayer3d_trigger_test_spatial(slayer3d_trigger *trigger, slayer3d_vec3 point)
{
    if (trigger == NULL || !trigger->enabled || trigger->type != SLAYER3D_TRIGGER_SPATIAL)
        return;

    const slayer3d_bounding_box *z = &trigger->spatial.zone;
    trigger->active = (point.x >= z->min.x && point.x <= z->max.x && point.y >= z->min.y && point.y <= z->max.y &&
                       point.z >= z->min.z && point.z <= z->max.z);
}

/* ================================================================== */
/* Property: numeric comparison                                       */
/* ================================================================== */

static bool compare_float(float value, slayer3d_compare_op op, float threshold)
{
    switch (op)
    {
    case SLAYER3D_CMP_EQ:
        return value == threshold;
    case SLAYER3D_CMP_NE:
        return value != threshold;
    case SLAYER3D_CMP_LT:
        return value < threshold;
    case SLAYER3D_CMP_LE:
        return value <= threshold;
    case SLAYER3D_CMP_GT:
        return value > threshold;
    case SLAYER3D_CMP_GE:
        return value >= threshold;
    }
    return false;
}

void slayer3d_trigger_test_property(slayer3d_trigger *trigger)
{
    if (trigger == NULL || !trigger->enabled || trigger->type != SLAYER3D_TRIGGER_PROPERTY)
        return;

    if (trigger->property.source == NULL || trigger->property.key == NULL)
    {
        trigger->active = false;
        return;
    }

    /*
     * Read the property as float. If the key does not exist or is not a
     * float, get_float returns a NaN-like fallback that will fail most
     * comparisons, which is the correct behavior (missing property =
     * condition not met). We use a fallback that makes all comparisons
     * return false for the "missing" case.
     */
    float value =
        slayer3d_properties_get_float(trigger->property.source, trigger->property.key, trigger->property.threshold);

    /*
     * If the key doesn't exist at all, the fallback equals the threshold.
     * For EQ that would incorrectly return true. Check existence explicitly.
     */
    if (!slayer3d_properties_has(trigger->property.source, trigger->property.key))
    {
        trigger->active = false;
        return;
    }

    trigger->active = compare_float(value, trigger->property.op, trigger->property.threshold);
}

/* ================================================================== */
/* Signal: pulse activation                                           */
/* ================================================================== */

void slayer3d_trigger_activate_signal(slayer3d_trigger *trigger)
{
    if (trigger == NULL || !trigger->enabled || trigger->type != SLAYER3D_TRIGGER_SIGNAL)
        return;
    trigger->active = true;
}

/* ================================================================== */
/* Evaluation: edge detection and emission                            */
/* ================================================================== */

void slayer3d_trigger_evaluate(slayer3d_trigger *trigger, slayer3d_signal_bus *bus)
{
    if (trigger == NULL || !trigger->enabled || bus == NULL)
        return;

    bool should_fire = false;

    switch (trigger->edge)
    {
    case SLAYER3D_TRIGGER_EDGE_ENTER:
        should_fire = trigger->active && !trigger->was_active;
        break;
    case SLAYER3D_TRIGGER_EDGE_EXIT:
        should_fire = !trigger->active && trigger->was_active;
        break;
    case SLAYER3D_TRIGGER_EDGE_BOTH:
        should_fire = trigger->active != trigger->was_active;
        break;
    case SLAYER3D_TRIGGER_LEVEL:
        should_fire = trigger->active;
        break;
    }

    if (should_fire)
        slayer3d_signal_emit(bus, trigger->emit_signal_id, NULL);

    trigger->was_active = trigger->active;

    /* Signal triggers are pulse-based: reset active after evaluation so
     * they require a fresh activation each cycle. */
    if (trigger->type == SLAYER3D_TRIGGER_SIGNAL)
        trigger->active = false;
}

/* ================================================================== */
/* Reset                                                              */
/* ================================================================== */

void slayer3d_trigger_reset(slayer3d_trigger *trigger)
{
    if (trigger == NULL)
        return;
    trigger->was_active = false;
    trigger->active = false;
}

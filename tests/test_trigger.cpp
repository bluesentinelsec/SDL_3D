/**
 * @file test_trigger.cpp
 * @brief Unit tests for slayer3d_trigger — condition detection and emission.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "slayer3d/math.h"
#include "slayer3d/properties.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/trigger.h"
}

/* ================================================================== */
/* Test helpers                                                       */
/* ================================================================== */

static int g_fire_count;
static int g_last_signal;

static void reset_globals()
{
    g_fire_count = 0;
    g_last_signal = -1;
}

static void fire_counter(void *ud, int signal_id, const slayer3d_properties *payload)
{
    (void)ud;
    (void)payload;
    g_fire_count++;
    g_last_signal = signal_id;
}

static slayer3d_trigger make_spatial_trigger(slayer3d_bounding_box zone, slayer3d_trigger_edge edge, int signal_id)
{
    slayer3d_trigger t{};
    t.type = SLAYER3D_TRIGGER_SPATIAL;
    t.edge = edge;
    t.emit_signal_id = signal_id;
    t.enabled = true;
    t.spatial.zone = zone;
    return t;
}

static slayer3d_bounding_box make_box(float x0, float y0, float z0, float x1, float y1, float z1)
{
    return slayer3d_bounding_box{{x0, y0, z0}, {x1, y1, z1}};
}

/* ================================================================== */
/* Spatial trigger — edge enter                                       */
/* ================================================================== */

TEST(TriggerSpatial, EnterFiresOnTransitionIntoZone)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 1, fire_counter, NULL);

    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_EDGE_ENTER, 1);

    /* Outside → evaluate: no fire. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(-1, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 0);

    /* Enter zone → fire. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    /* Stay inside → no additional fire (edge, not level). */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(6, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    slayer3d_signal_bus_destroy(bus);
}

TEST(TriggerSpatial, EnterDoesNotFireOnExit)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 1, fire_counter, NULL);

    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_EDGE_ENTER, 1);

    /* Enter. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    /* Exit. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(-1, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1); /* No fire on exit. */

    slayer3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Spatial trigger — edge exit                                        */
/* ================================================================== */

TEST(TriggerSpatial, ExitFiresOnTransitionOutOfZone)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 2, fire_counter, NULL);

    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_EDGE_EXIT, 2);

    /* Start inside. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 0);

    /* Leave zone → fire. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(-1, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);
    EXPECT_EQ(g_last_signal, 2);

    slayer3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Spatial trigger — edge both                                        */
/* ================================================================== */

TEST(TriggerSpatial, BothFiresOnEnterAndExit)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 3, fire_counter, NULL);

    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_EDGE_BOTH, 3);

    /* Enter. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    /* Exit. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(-1, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 2);

    slayer3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Spatial trigger — level (fires every frame while inside)           */
/* ================================================================== */

TEST(TriggerSpatial, LevelFiresEveryFrameWhileInside)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 4, fire_counter, NULL);

    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_LEVEL, 4);

    /* Inside: fires each frame. */
    for (int i = 0; i < 3; i++)
    {
        slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
        slayer3d_trigger_evaluate(&t, bus);
    }
    EXPECT_EQ(g_fire_count, 3);

    /* Outside: stops firing. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(-1, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 3);

    slayer3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Spatial trigger — boundary (point on edge is inside)               */
/* ================================================================== */

TEST(TriggerSpatial, PointOnBoundaryIsInside)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 1, fire_counter, NULL);

    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_EDGE_ENTER, 1);

    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(0, 0, 0));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    slayer3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Property trigger                                                   */
/* ================================================================== */

TEST(TriggerProperty, FiresWhenThresholdCrossed)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 10, fire_counter, NULL);

    slayer3d_properties *props = slayer3d_properties_create();
    slayer3d_properties_set_float(props, "health", 100.0f);

    slayer3d_trigger t{};
    t.type = SLAYER3D_TRIGGER_PROPERTY;
    t.edge = SLAYER3D_TRIGGER_EDGE_ENTER;
    t.emit_signal_id = 10;
    t.enabled = true;
    t.property.source = props;
    t.property.key = "health";
    t.property.op = SLAYER3D_CMP_LE;
    t.property.threshold = 0.0f;

    /* health=100, condition false. */
    slayer3d_trigger_test_property(&t);
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 0);

    /* health=0, condition becomes true → fire. */
    slayer3d_properties_set_float(props, "health", 0.0f);
    slayer3d_trigger_test_property(&t);
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    /* Still 0, no re-fire (edge enter). */
    slayer3d_trigger_test_property(&t);
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    slayer3d_properties_destroy(props);
    slayer3d_signal_bus_destroy(bus);
}

TEST(TriggerProperty, AllComparisonOps)
{
    slayer3d_properties *props = slayer3d_properties_create();
    slayer3d_properties_set_float(props, "x", 5.0f);

    slayer3d_trigger t{};
    t.type = SLAYER3D_TRIGGER_PROPERTY;
    t.edge = SLAYER3D_TRIGGER_LEVEL;
    t.enabled = true;
    t.property.source = props;
    t.property.key = "x";

    /* EQ: 5 == 5 → true */
    t.property.op = SLAYER3D_CMP_EQ;
    t.property.threshold = 5.0f;
    slayer3d_trigger_test_property(&t);
    EXPECT_TRUE(t.active);

    /* NE: 5 != 5 → false */
    t.property.op = SLAYER3D_CMP_NE;
    slayer3d_trigger_test_property(&t);
    EXPECT_FALSE(t.active);

    /* LT: 5 < 10 → true */
    t.property.op = SLAYER3D_CMP_LT;
    t.property.threshold = 10.0f;
    slayer3d_trigger_test_property(&t);
    EXPECT_TRUE(t.active);

    /* LE: 5 <= 5 → true */
    t.property.op = SLAYER3D_CMP_LE;
    t.property.threshold = 5.0f;
    slayer3d_trigger_test_property(&t);
    EXPECT_TRUE(t.active);

    /* GT: 5 > 3 → true */
    t.property.op = SLAYER3D_CMP_GT;
    t.property.threshold = 3.0f;
    slayer3d_trigger_test_property(&t);
    EXPECT_TRUE(t.active);

    /* GE: 5 >= 6 → false */
    t.property.op = SLAYER3D_CMP_GE;
    t.property.threshold = 6.0f;
    slayer3d_trigger_test_property(&t);
    EXPECT_FALSE(t.active);

    slayer3d_properties_destroy(props);
}

TEST(TriggerProperty, MissingKeyIsInactive)
{
    slayer3d_properties *props = slayer3d_properties_create();

    slayer3d_trigger t{};
    t.type = SLAYER3D_TRIGGER_PROPERTY;
    t.edge = SLAYER3D_TRIGGER_LEVEL;
    t.enabled = true;
    t.property.source = props;
    t.property.key = "nonexistent";
    t.property.op = SLAYER3D_CMP_EQ;
    t.property.threshold = 0.0f;

    slayer3d_trigger_test_property(&t);
    EXPECT_FALSE(t.active);

    slayer3d_properties_destroy(props);
}

TEST(TriggerProperty, NullSourceIsInactive)
{
    slayer3d_trigger t{};
    t.type = SLAYER3D_TRIGGER_PROPERTY;
    t.edge = SLAYER3D_TRIGGER_LEVEL;
    t.enabled = true;
    t.property.source = NULL;
    t.property.key = "x";
    t.property.op = SLAYER3D_CMP_EQ;
    t.property.threshold = 0.0f;

    slayer3d_trigger_test_property(&t);
    EXPECT_FALSE(t.active);
}

/* ================================================================== */
/* Signal trigger                                                     */
/* ================================================================== */

TEST(TriggerSignal, ActivateAndEvaluate)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 20, fire_counter, NULL);

    slayer3d_trigger t{};
    t.type = SLAYER3D_TRIGGER_SIGNAL;
    t.edge = SLAYER3D_TRIGGER_EDGE_ENTER;
    t.emit_signal_id = 20;
    t.enabled = true;
    t.signal.listen_signal_id = 10;

    /* Not activated → no fire. */
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 0);

    /* Activate (simulating signal reception) → fire on evaluate. */
    slayer3d_trigger_activate_signal(&t);
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    /* Auto-reset: next evaluate without activation → no fire. */
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    slayer3d_signal_bus_destroy(bus);
}

TEST(TriggerSignal, PulseResetsAfterEvaluate)
{
    slayer3d_trigger t{};
    t.type = SLAYER3D_TRIGGER_SIGNAL;
    t.edge = SLAYER3D_TRIGGER_LEVEL;
    t.emit_signal_id = 1;
    t.enabled = true;

    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    reset_globals();
    slayer3d_signal_connect(bus, 1, fire_counter, NULL);

    /* Activate and evaluate: fires. */
    slayer3d_trigger_activate_signal(&t);
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    /* Without re-activation: does not fire (pulse reset). */
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    slayer3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Disabled trigger                                                   */
/* ================================================================== */

TEST(TriggerDisabled, DisabledTriggerDoesNotFire)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 1, fire_counter, NULL);

    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_EDGE_ENTER, 1);
    t.enabled = false;

    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 0);

    slayer3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Reset                                                              */
/* ================================================================== */

TEST(TriggerReset, ResetClearsState)
{
    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_EDGE_ENTER, 1);
    t.active = true;
    t.was_active = true;

    slayer3d_trigger_reset(&t);
    EXPECT_FALSE(t.active);
    EXPECT_FALSE(t.was_active);
}

TEST(TriggerReset, ResetAllowsRefire)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 1, fire_counter, NULL);

    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_EDGE_ENTER, 1);

    /* Enter → fire. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    /* Reset, then enter again → fires again. */
    slayer3d_trigger_reset(&t);
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 2);

    slayer3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Re-entry after exit                                                */
/* ================================================================== */

TEST(TriggerSpatial, ReentryAfterExitFiresAgain)
{
    reset_globals();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_signal_connect(bus, 1, fire_counter, NULL);

    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_EDGE_ENTER, 1);

    /* Enter. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    /* Exit. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(-1, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 1);

    /* Re-enter → fires again. */
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
    slayer3d_trigger_evaluate(&t, bus);
    EXPECT_EQ(g_fire_count, 2);

    slayer3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* NULL safety                                                        */
/* ================================================================== */

TEST(TriggerNull, NullTriggerIsSafe)
{
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_trigger_test_spatial(NULL, slayer3d_vec3_make(0, 0, 0));
    slayer3d_trigger_test_property(NULL);
    slayer3d_trigger_activate_signal(NULL);
    slayer3d_trigger_evaluate(NULL, bus);
    slayer3d_trigger_reset(NULL);
    slayer3d_signal_bus_destroy(bus);
}

TEST(TriggerNull, NullBusDoesNotCrash)
{
    slayer3d_trigger t = make_spatial_trigger(make_box(0, 0, 0, 10, 10, 10), SLAYER3D_TRIGGER_EDGE_ENTER, 1);
    slayer3d_trigger_test_spatial(&t, slayer3d_vec3_make(5, 5, 5));
    slayer3d_trigger_evaluate(&t, NULL); /* Should not crash. */
}

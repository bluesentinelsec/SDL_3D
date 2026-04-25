/**
 * @file test_action.cpp
 * @brief Unit tests for sdl3d_action — data-driven gameplay effects.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/action.h"
#include "sdl3d/math.h"
#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"
}

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

static int g_signal_count;
static int g_last_signal;

static void reset_globals()
{
    g_signal_count = 0;
    g_last_signal = -1;
}

static void signal_counter(void *ud, int signal_id, const sdl3d_properties *payload)
{
    (void)ud;
    (void)payload;
    g_signal_count++;
    g_last_signal = signal_id;
}

/* ================================================================== */
/* SET_PROPERTY — all value types                                     */
/* ================================================================== */

TEST(Action, SetPropertyBool)
{
    sdl3d_properties *props = sdl3d_properties_create();
    sdl3d_action a = sdl3d_action_make_set_bool(props, "locked", true);

    sdl3d_action_execute(&a, NULL, NULL);
    EXPECT_TRUE(sdl3d_properties_get_bool(props, "locked", false));

    sdl3d_properties_destroy(props);
}

TEST(Action, SetPropertyFloat)
{
    sdl3d_properties *props = sdl3d_properties_create();
    sdl3d_action a = sdl3d_action_make_set_float(props, "speed", 12.5f);

    sdl3d_action_execute(&a, NULL, NULL);
    EXPECT_FLOAT_EQ(sdl3d_properties_get_float(props, "speed", 0.0f), 12.5f);

    sdl3d_properties_destroy(props);
}

TEST(Action, SetPropertyInt)
{
    sdl3d_properties *props = sdl3d_properties_create();
    sdl3d_action a = sdl3d_action_make_set_int(props, "ammo", 50);

    sdl3d_action_execute(&a, NULL, NULL);
    EXPECT_EQ(sdl3d_properties_get_int(props, "ammo", 0), 50);

    sdl3d_properties_destroy(props);
}

TEST(Action, SetPropertyVec3)
{
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action a{};
    a.type = SDL3D_ACTION_SET_PROPERTY;
    a.set_property.target = props;
    a.set_property.key = "origin";
    a.set_property.value.type = SDL3D_VALUE_VEC3;
    a.set_property.value.as_vec3 = sdl3d_vec3_make(1.0f, 2.0f, 3.0f);

    sdl3d_action_execute(&a, NULL, NULL);
    sdl3d_vec3 v = sdl3d_properties_get_vec3(props, "origin", sdl3d_vec3_make(0, 0, 0));
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);

    sdl3d_properties_destroy(props);
}

TEST(Action, SetPropertyString)
{
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action a{};
    a.type = SDL3D_ACTION_SET_PROPERTY;
    a.set_property.target = props;
    a.set_property.key = "classname";
    a.set_property.value.type = SDL3D_VALUE_STRING;
    a.set_property.value.as_string = (char *)"func_door";

    sdl3d_action_execute(&a, NULL, NULL);
    EXPECT_STREQ(sdl3d_properties_get_string(props, "classname", ""), "func_door");

    sdl3d_properties_destroy(props);
}

TEST(Action, SetPropertyColor)
{
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action a{};
    a.type = SDL3D_ACTION_SET_PROPERTY;
    a.set_property.target = props;
    a.set_property.key = "tint";
    a.set_property.value.type = SDL3D_VALUE_COLOR;
    a.set_property.value.as_color = (sdl3d_color){255, 0, 0, 255};

    sdl3d_action_execute(&a, NULL, NULL);
    sdl3d_color c = sdl3d_properties_get_color(props, "tint", (sdl3d_color){0, 0, 0, 0});
    EXPECT_EQ(c.r, 255);
    EXPECT_EQ(c.g, 0);

    sdl3d_properties_destroy(props);
}

TEST(Action, SetPropertyOverwritesExisting)
{
    sdl3d_properties *props = sdl3d_properties_create();
    sdl3d_properties_set_int(props, "health", 100);

    sdl3d_action a = sdl3d_action_make_set_int(props, "health", 0);
    sdl3d_action_execute(&a, NULL, NULL);
    EXPECT_EQ(sdl3d_properties_get_int(props, "health", -1), 0);

    sdl3d_properties_destroy(props);
}

TEST(Action, SetPropertyNullTargetIsNoOp)
{
    sdl3d_action a = sdl3d_action_make_set_bool(NULL, "x", true);
    sdl3d_action_execute(&a, NULL, NULL); /* Should not crash. */
}

/* ================================================================== */
/* EMIT_SIGNAL                                                        */
/* ================================================================== */

TEST(Action, EmitSignal)
{
    reset_globals();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 42, signal_counter, NULL);

    sdl3d_action a = sdl3d_action_make_emit_signal(42);
    sdl3d_action_execute(&a, bus, NULL);

    EXPECT_EQ(g_signal_count, 1);
    EXPECT_EQ(g_last_signal, 42);

    sdl3d_signal_bus_destroy(bus);
}

TEST(Action, EmitSignalNullBusIsNoOp)
{
    sdl3d_action a = sdl3d_action_make_emit_signal(1);
    sdl3d_action_execute(&a, NULL, NULL); /* Should not crash. */
}

TEST(Action, EmitSignalCascade)
{
    reset_globals();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();

    /* Signal 1 handler emits signal 2 via an action. */
    struct cascade_ctx
    {
        sdl3d_signal_bus *bus;
        int count;
    };
    static struct cascade_ctx ctx;
    ctx.bus = bus;
    ctx.count = 0;

    auto cascade_handler = [](void *ud, int sig, const sdl3d_properties *p) {
        (void)sig;
        (void)p;
        struct cascade_ctx *c = (struct cascade_ctx *)ud;
        sdl3d_action a2 = sdl3d_action_make_emit_signal(2);
        sdl3d_action_execute(&a2, c->bus, NULL);
    };

    sdl3d_signal_connect(bus, 1, cascade_handler, &ctx);
    sdl3d_signal_connect(bus, 2, signal_counter, NULL);

    sdl3d_action a = sdl3d_action_make_emit_signal(1);
    sdl3d_action_execute(&a, bus, NULL);

    EXPECT_EQ(g_signal_count, 1);
    EXPECT_EQ(g_last_signal, 2);

    sdl3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* START_TIMER (no-op without Phase 5)                                */
/* ================================================================== */

TEST(Action, StartTimerWithNullPoolIsNoOp)
{
    sdl3d_action a{};
    a.type = SDL3D_ACTION_START_TIMER;
    a.start_timer.delay = 5.0f;
    a.start_timer.signal_id = 99;
    a.start_timer.repeating = false;

    sdl3d_action_execute(&a, NULL, NULL); /* Should not crash. */
}

/* ================================================================== */
/* LOG                                                                */
/* ================================================================== */

TEST(Action, LogDoesNotCrash)
{
    sdl3d_action a = sdl3d_action_make_log("test message");
    sdl3d_action_execute(&a, NULL, NULL);
}

TEST(Action, LogNullMessageDoesNotCrash)
{
    sdl3d_action a = sdl3d_action_make_log(NULL);
    sdl3d_action_execute(&a, NULL, NULL);
}

/* ================================================================== */
/* NULL action                                                        */
/* ================================================================== */

TEST(Action, NullActionIsSafe)
{
    sdl3d_action_execute(NULL, NULL, NULL);
}

/* ================================================================== */
/* Convenience constructors produce correct types                     */
/* ================================================================== */

TEST(Action, MakeSetBoolHasCorrectType)
{
    sdl3d_action a = sdl3d_action_make_set_bool(NULL, "x", true);
    EXPECT_EQ(a.type, SDL3D_ACTION_SET_PROPERTY);
    EXPECT_EQ(a.set_property.value.type, SDL3D_VALUE_BOOL);
    EXPECT_TRUE(a.set_property.value.as_bool);
}

TEST(Action, MakeSetFloatHasCorrectType)
{
    sdl3d_action a = sdl3d_action_make_set_float(NULL, "x", 3.14f);
    EXPECT_EQ(a.type, SDL3D_ACTION_SET_PROPERTY);
    EXPECT_EQ(a.set_property.value.type, SDL3D_VALUE_FLOAT);
    EXPECT_FLOAT_EQ(a.set_property.value.as_float, 3.14f);
}

TEST(Action, MakeSetIntHasCorrectType)
{
    sdl3d_action a = sdl3d_action_make_set_int(NULL, "x", 42);
    EXPECT_EQ(a.type, SDL3D_ACTION_SET_PROPERTY);
    EXPECT_EQ(a.set_property.value.type, SDL3D_VALUE_INT);
    EXPECT_EQ(a.set_property.value.as_int, 42);
}

TEST(Action, MakeEmitSignalHasCorrectType)
{
    sdl3d_action a = sdl3d_action_make_emit_signal(7);
    EXPECT_EQ(a.type, SDL3D_ACTION_EMIT_SIGNAL);
    EXPECT_EQ(a.emit_signal.signal_id, 7);
}

TEST(Action, MakeLogHasCorrectType)
{
    sdl3d_action a = sdl3d_action_make_log("hello");
    EXPECT_EQ(a.type, SDL3D_ACTION_LOG);
    EXPECT_STREQ(a.log.message, "hello");
}

/* ================================================================== */
/* Integration: trigger → signal → action → property change           */
/* ================================================================== */

TEST(ActionIntegration, TriggerToSignalToPropertyChange)
{
    sdl3d_properties *door_props = sdl3d_properties_create();
    sdl3d_properties_set_bool(door_props, "locked", false);

    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();

    /* When signal 1 fires, lock the door. */
    struct action_ctx
    {
        sdl3d_action action;
    };
    static struct action_ctx actx;
    actx.action = sdl3d_action_make_set_bool(door_props, "locked", true);

    auto action_handler = [](void *ud, int sig, const sdl3d_properties *p) {
        (void)sig;
        (void)p;
        struct action_ctx *c = (struct action_ctx *)ud;
        sdl3d_action_execute(&c->action, NULL, NULL);
    };

    sdl3d_signal_connect(bus, 1, action_handler, &actx);

    /* Emit signal 1 → handler executes action → door.locked = true. */
    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_TRUE(sdl3d_properties_get_bool(door_props, "locked", false));

    sdl3d_properties_destroy(door_props);
    sdl3d_signal_bus_destroy(bus);
}

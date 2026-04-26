/**
 * @file test_logic.cpp
 * @brief Unit tests for sdl3d_logic_world signal-to-action bindings.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/logic.h"
#include "sdl3d/properties.h"
}

TEST(LogicWorld, CreateAndDestroy)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_timer_pool *timers = sdl3d_timer_pool_create();

    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, timers);
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(sdl3d_logic_world_binding_count(world), 0);

    sdl3d_logic_world_destroy(world);
    sdl3d_timer_pool_destroy(timers);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorld, CreateRequiresBus)
{
    sdl3d_timer_pool *timers = sdl3d_timer_pool_create();
    EXPECT_EQ(sdl3d_logic_world_create(nullptr, timers), nullptr);
    sdl3d_timer_pool_destroy(timers);
}

TEST(LogicWorld, DestroyNullIsSafe)
{
    sdl3d_logic_world_destroy(nullptr);
}

TEST(LogicWorld, BoundActionExecutesWhenSignalIsEmitted)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action action = sdl3d_action_make_set_bool(props, "opened", true);
    int binding_id = sdl3d_logic_world_bind_action(world, 100, &action);
    EXPECT_GT(binding_id, 0);
    EXPECT_EQ(sdl3d_logic_world_binding_count(world), 1);

    sdl3d_signal_emit(bus, 100, nullptr);
    EXPECT_TRUE(sdl3d_properties_get_bool(props, "opened", false));

    sdl3d_properties_destroy(props);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorld, BoundActionIgnoresDifferentSignals)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action action = sdl3d_action_make_set_bool(props, "opened", true);
    ASSERT_GT(sdl3d_logic_world_bind_action(world, 100, &action), 0);

    sdl3d_signal_emit(bus, 101, nullptr);
    EXPECT_FALSE(sdl3d_properties_get_bool(props, "opened", false));

    sdl3d_properties_destroy(props);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorld, BindingsExecuteInSignalConnectionOrder)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action first = sdl3d_action_make_set_int(props, "value", 1);
    sdl3d_action second = sdl3d_action_make_set_int(props, "value", 2);

    ASSERT_GT(sdl3d_logic_world_bind_action(world, 7, &first), 0);
    ASSERT_GT(sdl3d_logic_world_bind_action(world, 7, &second), 0);

    sdl3d_signal_emit(bus, 7, nullptr);
    EXPECT_EQ(sdl3d_properties_get_int(props, "value", 0), 2);

    sdl3d_properties_destroy(props);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorld, DisabledBindingDoesNotExecuteAndCanBeReenabled)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action action = sdl3d_action_make_set_bool(props, "armed", true);
    int binding_id = sdl3d_logic_world_bind_action(world, 42, &action);
    ASSERT_GT(binding_id, 0);

    EXPECT_TRUE(sdl3d_logic_world_set_binding_enabled(world, binding_id, false));
    EXPECT_FALSE(sdl3d_logic_world_binding_enabled(world, binding_id));
    sdl3d_signal_emit(bus, 42, nullptr);
    EXPECT_FALSE(sdl3d_properties_get_bool(props, "armed", false));

    EXPECT_TRUE(sdl3d_logic_world_set_binding_enabled(world, binding_id, true));
    EXPECT_TRUE(sdl3d_logic_world_binding_enabled(world, binding_id));
    sdl3d_signal_emit(bus, 42, nullptr);
    EXPECT_TRUE(sdl3d_properties_get_bool(props, "armed", false));

    sdl3d_properties_destroy(props);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorld, UnbindRemovesBindingAndDisconnectsHandler)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action action = sdl3d_action_make_set_bool(props, "active", true);
    int binding_id = sdl3d_logic_world_bind_action(world, 12, &action);
    ASSERT_GT(binding_id, 0);
    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 1);

    sdl3d_logic_world_unbind_action(world, binding_id);
    EXPECT_EQ(sdl3d_logic_world_binding_count(world), 0);
    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 0);

    sdl3d_signal_emit(bus, 12, nullptr);
    EXPECT_FALSE(sdl3d_properties_get_bool(props, "active", false));

    sdl3d_properties_destroy(props);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorld, EmitSignalActionCanChainToAnotherBinding)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action emit_second = sdl3d_action_make_emit_signal(2);
    sdl3d_action set_done = sdl3d_action_make_set_bool(props, "done", true);

    ASSERT_GT(sdl3d_logic_world_bind_action(world, 1, &emit_second), 0);
    ASSERT_GT(sdl3d_logic_world_bind_action(world, 2, &set_done), 0);

    sdl3d_signal_emit(bus, 1, nullptr);
    EXPECT_TRUE(sdl3d_properties_get_bool(props, "done", false));

    sdl3d_properties_destroy(props);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorld, StartTimerActionUsesWorldTimerPool)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_timer_pool *timers = sdl3d_timer_pool_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, timers);
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action start_timer{};
    start_timer.type = SDL3D_ACTION_START_TIMER;
    start_timer.start_timer.delay = 0.5f;
    start_timer.start_timer.signal_id = 2;
    start_timer.start_timer.repeating = false;
    start_timer.start_timer.interval = 0.0f;

    sdl3d_action set_done = sdl3d_action_make_set_bool(props, "done", true);

    ASSERT_GT(sdl3d_logic_world_bind_action(world, 1, &start_timer), 0);
    ASSERT_GT(sdl3d_logic_world_bind_action(world, 2, &set_done), 0);

    sdl3d_signal_emit(bus, 1, nullptr);
    EXPECT_EQ(sdl3d_timer_pool_active_count(timers), 1);

    sdl3d_timer_pool_update(timers, bus, 0.25f);
    EXPECT_FALSE(sdl3d_properties_get_bool(props, "done", false));

    sdl3d_timer_pool_update(timers, bus, 0.25f);
    EXPECT_TRUE(sdl3d_properties_get_bool(props, "done", false));
    EXPECT_EQ(sdl3d_timer_pool_active_count(timers), 0);

    sdl3d_properties_destroy(props);
    sdl3d_logic_world_destroy(world);
    sdl3d_timer_pool_destroy(timers);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorld, DestroyDisconnectsBindingsButLeavesBusAlive)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action action = sdl3d_action_make_set_bool(props, "active", true);
    ASSERT_GT(sdl3d_logic_world_bind_action(world, 5, &action), 0);
    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 1);

    sdl3d_logic_world_destroy(world);
    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 0);

    sdl3d_signal_emit(bus, 5, nullptr);
    EXPECT_FALSE(sdl3d_properties_get_bool(props, "active", false));

    sdl3d_properties_destroy(props);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorld, InvalidOperationsAreSafe)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    sdl3d_action action = sdl3d_action_make_log("safe");

    EXPECT_EQ(sdl3d_logic_world_bind_action(nullptr, 1, &action), 0);
    EXPECT_EQ(sdl3d_logic_world_bind_action(world, 1, nullptr), 0);
    EXPECT_FALSE(sdl3d_logic_world_set_binding_enabled(nullptr, 1, true));
    EXPECT_FALSE(sdl3d_logic_world_set_binding_enabled(world, 999, true));
    EXPECT_FALSE(sdl3d_logic_world_binding_enabled(nullptr, 1));
    EXPECT_FALSE(sdl3d_logic_world_binding_enabled(world, 999));
    EXPECT_EQ(sdl3d_logic_world_binding_count(nullptr), 0);

    sdl3d_logic_world_unbind_action(nullptr, 1);
    sdl3d_logic_world_unbind_action(world, 999);
    sdl3d_logic_world_unbind_action(world, 0);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

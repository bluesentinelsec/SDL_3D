/**
 * @file test_logic.cpp
 * @brief Unit tests for sdl3d_logic_world signal-to-action bindings.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/actor_registry.h"
#include "sdl3d/level.h"
#include "sdl3d/logic.h"
#include "sdl3d/properties.h"
}

static sdl3d_logic_world *make_logic_world(sdl3d_signal_bus **out_bus)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    if (out_bus != nullptr)
        *out_bus = bus;
    return sdl3d_logic_world_create(bus, nullptr);
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

TEST(LogicWorldTargets, TargetContextCanBeSetAndCleared)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();

    sdl3d_logic_target_context context{};
    context.registry = registry;
    sdl3d_logic_world_set_target_context(world, &context);

    const sdl3d_logic_target_context *stored = sdl3d_logic_world_get_target_context(world);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->registry, registry);

    sdl3d_logic_world_set_target_context(world, nullptr);
    EXPECT_EQ(sdl3d_logic_world_get_target_context(world), nullptr);

    sdl3d_actor_registry_destroy(registry);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldTargets, ResolveActorById)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();
    sdl3d_registered_actor *actor = sdl3d_actor_registry_add(registry, "door_1");
    ASSERT_NE(actor, nullptr);

    sdl3d_logic_target_context context{};
    context.registry = registry;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_target_ref ref = sdl3d_logic_target_actor_id(actor->id);
    sdl3d_logic_resolved_target resolved{};
    ASSERT_TRUE(sdl3d_logic_world_resolve_target(world, &ref, &resolved));
    EXPECT_EQ(resolved.kind, SDL3D_LOGIC_TARGET_ACTOR_ID);
    EXPECT_EQ(resolved.actor, actor);

    sdl3d_actor_registry_destroy(registry);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldTargets, ResolveActorByName)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();
    sdl3d_registered_actor *actor = sdl3d_actor_registry_add(registry, "lift_button");
    ASSERT_NE(actor, nullptr);

    sdl3d_logic_target_context context{};
    context.registry = registry;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_target_ref ref = sdl3d_logic_target_actor_name("lift_button");
    sdl3d_logic_resolved_target resolved{};
    ASSERT_TRUE(sdl3d_logic_world_resolve_target(world, &ref, &resolved));
    EXPECT_EQ(resolved.kind, SDL3D_LOGIC_TARGET_ACTOR_NAME);
    EXPECT_EQ(resolved.actor, actor);

    sdl3d_actor_registry_destroy(registry);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldTargets, MissingActorResolutionFailsAndClearsOutput)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();

    sdl3d_logic_target_context context{};
    context.registry = registry;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_target_ref ref = sdl3d_logic_target_actor_name("missing");
    sdl3d_logic_resolved_target resolved{};
    resolved.kind = SDL3D_LOGIC_TARGET_ACTOR_NAME;
    resolved.actor = (sdl3d_registered_actor *)0x1;

    EXPECT_FALSE(sdl3d_logic_world_resolve_target(world, &ref, &resolved));
    EXPECT_EQ(resolved.kind, SDL3D_LOGIC_TARGET_NONE);
    EXPECT_EQ(resolved.actor, nullptr);

    sdl3d_actor_registry_destroy(registry);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldTargets, ResolveSectorByIndex)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_level level{};
    sdl3d_sector sectors[2]{};
    level.sector_count = 2;
    sectors[1].damage_per_second = 12.0f;

    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = sectors;
    context.sector_count = 2;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_target_ref ref = sdl3d_logic_target_sector_index(1);
    sdl3d_logic_resolved_target resolved{};
    ASSERT_TRUE(sdl3d_logic_world_resolve_target(world, &ref, &resolved));
    EXPECT_EQ(resolved.kind, SDL3D_LOGIC_TARGET_SECTOR_INDEX);
    EXPECT_EQ(resolved.sector.level, &level);
    EXPECT_EQ(resolved.sector.sector, &sectors[1]);
    EXPECT_EQ(resolved.sector.sector_index, 1);
    EXPECT_FLOAT_EQ(resolved.sector.sector->damage_per_second, 12.0f);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldTargets, InvalidSectorIndexFails)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_level level{};
    sdl3d_sector sectors[1]{};
    level.sector_count = 1;

    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = sectors;
    context.sector_count = 1;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_target_ref negative = sdl3d_logic_target_sector_index(-1);
    sdl3d_logic_target_ref too_high = sdl3d_logic_target_sector_index(1);
    sdl3d_logic_resolved_target resolved{};

    EXPECT_FALSE(sdl3d_logic_world_resolve_target(world, &negative, &resolved));
    EXPECT_FALSE(sdl3d_logic_world_resolve_target(world, &too_high, &resolved));

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldTargets, ResolveSectorByRegisteredName)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_level level{};
    sdl3d_sector sectors[3]{};
    level.sector_count = 3;
    sectors[2].push_velocity[0] = 4.0f;

    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = sectors;
    context.sector_count = 3;
    sdl3d_logic_world_set_target_context(world, &context);

    ASSERT_TRUE(sdl3d_logic_world_set_sector_name(world, 2, "dragon_conveyor"));
    EXPECT_EQ(sdl3d_logic_world_find_sector_index(world, "dragon_conveyor"), 2);

    sdl3d_logic_target_ref ref = sdl3d_logic_target_sector_name("dragon_conveyor");
    sdl3d_logic_resolved_target resolved{};
    ASSERT_TRUE(sdl3d_logic_world_resolve_target(world, &ref, &resolved));
    EXPECT_EQ(resolved.kind, SDL3D_LOGIC_TARGET_SECTOR_NAME);
    EXPECT_EQ(resolved.sector.sector, &sectors[2]);
    EXPECT_EQ(resolved.sector.sector_index, 2);
    EXPECT_FLOAT_EQ(resolved.sector.sector->push_velocity[0], 4.0f);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldTargets, SectorNamesAreUniqueByNameAndIndex)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);

    ASSERT_TRUE(sdl3d_logic_world_set_sector_name(world, 1, "lift"));
    ASSERT_TRUE(sdl3d_logic_world_set_sector_name(world, 2, "lift"));
    EXPECT_EQ(sdl3d_logic_world_find_sector_index(world, "lift"), 2);

    ASSERT_TRUE(sdl3d_logic_world_set_sector_name(world, 2, "platform"));
    EXPECT_EQ(sdl3d_logic_world_find_sector_index(world, "lift"), -1);
    EXPECT_EQ(sdl3d_logic_world_find_sector_index(world, "platform"), 2);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldTargets, ClearSectorNamesRemovesAliases)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);

    ASSERT_TRUE(sdl3d_logic_world_set_sector_name(world, 0, "nukage"));
    EXPECT_EQ(sdl3d_logic_world_find_sector_index(world, "nukage"), 0);

    sdl3d_logic_world_clear_sector_names(world);
    EXPECT_EQ(sdl3d_logic_world_find_sector_index(world, "nukage"), -1);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldTargets, InvalidTargetOperationsAreSafe)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_logic_target_ref ref = sdl3d_logic_target_actor_id(1);
    sdl3d_logic_resolved_target resolved{};

    sdl3d_logic_world_set_target_context(nullptr, nullptr);
    EXPECT_EQ(sdl3d_logic_world_get_target_context(nullptr), nullptr);
    EXPECT_FALSE(sdl3d_logic_world_set_sector_name(nullptr, 0, "sector"));
    EXPECT_FALSE(sdl3d_logic_world_set_sector_name(world, -1, "sector"));
    EXPECT_FALSE(sdl3d_logic_world_set_sector_name(world, 0, nullptr));
    EXPECT_FALSE(sdl3d_logic_world_set_sector_name(world, 0, ""));
    sdl3d_logic_world_clear_sector_names(nullptr);
    EXPECT_EQ(sdl3d_logic_world_find_sector_index(nullptr, "sector"), -1);
    EXPECT_EQ(sdl3d_logic_world_find_sector_index(world, nullptr), -1);
    EXPECT_EQ(sdl3d_logic_world_find_sector_index(world, ""), -1);

    EXPECT_FALSE(sdl3d_logic_world_resolve_target(nullptr, &ref, &resolved));
    EXPECT_FALSE(sdl3d_logic_world_resolve_target(world, nullptr, &resolved));
    EXPECT_FALSE(sdl3d_logic_world_resolve_target(world, &ref, nullptr));

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

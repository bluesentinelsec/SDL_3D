/**
 * @file test_logic.cpp
 * @brief Unit tests for sdl3d_logic_world signal-to-action bindings.
 */

#include <gtest/gtest.h>

#include <SDL3/SDL_stdinc.h>

extern "C"
{
#include "sdl3d/actor_registry.h"
#include "sdl3d/level.h"
#include "sdl3d/logic.h"
#include "sdl3d/math.h"
#include "sdl3d/properties.h"
}

static sdl3d_logic_world *make_logic_world(sdl3d_signal_bus **out_bus)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    if (out_bus != nullptr)
        *out_bus = bus;
    return sdl3d_logic_world_create(bus, nullptr);
}

struct logic_signal_capture
{
    int count = 0;
    int signal_id = 0;
    int sensor_id = -1;
    int event = 0;
    int sector_index = -1;
    int actor_id = -1;
    bool inside = false;
    float distance = -1.0f;
    sdl3d_vec3 sample_position = {};
    char actor_name[64] = {};
};

static void capture_logic_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    logic_signal_capture *capture = (logic_signal_capture *)userdata;
    capture->count++;
    capture->signal_id = signal_id;
    capture->sensor_id = sdl3d_properties_get_int(payload, "sensor_id", -1);
    capture->event = sdl3d_properties_get_int(payload, "event", 0);
    capture->inside = sdl3d_properties_get_bool(payload, "inside", false);
    capture->sector_index = sdl3d_properties_get_int(payload, "sector_index", -1);
    capture->actor_id = sdl3d_properties_get_int(payload, "actor_id", -1);
    capture->distance = sdl3d_properties_get_float(payload, "distance", -1.0f);
    capture->sample_position = sdl3d_properties_get_vec3(payload, "sample_position", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    const char *actor_name = sdl3d_properties_get_string(payload, "actor_name", "");
    SDL_snprintf(capture->actor_name, sizeof(capture->actor_name), "%s", actor_name);
}

static sdl3d_sector make_square_sector(float min_x, float min_z, float max_x, float max_z)
{
    sdl3d_sector sector{};
    sector.points[0][0] = min_x;
    sector.points[0][1] = min_z;
    sector.points[1][0] = max_x;
    sector.points[1][1] = min_z;
    sector.points[2][0] = max_x;
    sector.points[2][1] = max_z;
    sector.points[3][0] = min_x;
    sector.points[3][1] = max_z;
    sector.num_points = 4;
    sector.floor_y = 0.0f;
    sector.ceil_y = 3.0f;
    return sector;
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

TEST(LogicWorldActions, ExecuteCoreActionUsesExistingActionSystem)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_properties *props = sdl3d_properties_create();

    sdl3d_action core = sdl3d_action_make_set_bool(props, "opened", true);
    sdl3d_logic_action action = sdl3d_logic_action_make_core(core);

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &action));
    EXPECT_TRUE(sdl3d_properties_get_bool(props, "opened", false));

    sdl3d_properties_destroy(props);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, BindLogicActionSetsActorActive)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();
    sdl3d_registered_actor *actor = sdl3d_actor_registry_add(registry, "robot_1");
    ASSERT_NE(actor, nullptr);
    actor->active = false;

    sdl3d_logic_target_context context{};
    context.registry = registry;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_action action =
        sdl3d_logic_action_make_set_actor_active(sdl3d_logic_target_actor_name("robot_1"), true);
    ASSERT_GT(sdl3d_logic_world_bind_logic_action(world, 77, &action), 0);

    sdl3d_signal_emit(bus, 77, nullptr);
    EXPECT_TRUE(actor->active);

    sdl3d_actor_registry_destroy(registry);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, ToggleActorActive)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();
    sdl3d_registered_actor *actor = sdl3d_actor_registry_add(registry, "door");
    ASSERT_NE(actor, nullptr);
    actor->active = true;

    sdl3d_logic_target_context context{};
    context.registry = registry;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_action action = sdl3d_logic_action_make_toggle_actor_active(sdl3d_logic_target_actor_id(actor->id));

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &action));
    EXPECT_FALSE(actor->active);
    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &action));
    EXPECT_TRUE(actor->active);

    sdl3d_actor_registry_destroy(registry);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, SetActorProperty)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();
    sdl3d_registered_actor *actor = sdl3d_actor_registry_add(registry, "alert_light");
    ASSERT_NE(actor, nullptr);

    sdl3d_logic_target_context context{};
    context.registry = registry;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_value color{};
    color.type = SDL3D_VALUE_STRING;
    color.as_string = (char *)"red";
    sdl3d_logic_action action =
        sdl3d_logic_action_make_set_actor_property(sdl3d_logic_target_actor_name("alert_light"), "state", color);

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &action));
    EXPECT_STREQ(sdl3d_properties_get_string(actor->props, "state", ""), "red");

    sdl3d_actor_registry_destroy(registry);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, SetSectorPushByName)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_level level{};
    sdl3d_sector sectors[2]{};
    level.sector_count = 2;

    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = sectors;
    context.sector_count = 2;
    sdl3d_logic_world_set_target_context(world, &context);
    ASSERT_TRUE(sdl3d_logic_world_set_sector_name(world, 1, "conveyor"));

    sdl3d_logic_action action = sdl3d_logic_action_make_set_sector_push(sdl3d_logic_target_sector_name("conveyor"),
                                                                        sdl3d_vec3_make(3.0f, 0.0f, -1.5f));

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &action));
    EXPECT_FLOAT_EQ(sectors[1].push_velocity[0], 3.0f);
    EXPECT_FLOAT_EQ(sectors[1].push_velocity[1], 0.0f);
    EXPECT_FLOAT_EQ(sectors[1].push_velocity[2], -1.5f);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, SetSectorDamageClampsNegativeToZero)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_level level{};
    sdl3d_sector sectors[1]{};
    level.sector_count = 1;
    sectors[0].damage_per_second = 9.0f;

    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = sectors;
    context.sector_count = 1;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_action action = sdl3d_logic_action_make_set_sector_damage(sdl3d_logic_target_sector_index(0), -5.0f);

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &action));
    EXPECT_FLOAT_EQ(sectors[0].damage_per_second, 0.0f);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, SetSectorAmbientClampsNegativeToZero)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_level level{};
    sdl3d_sector sectors[1]{};
    level.sector_count = 1;
    sectors[0].ambient_sound_id = 4;

    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = sectors;
    context.sector_count = 1;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_action action = sdl3d_logic_action_make_set_sector_ambient(sdl3d_logic_target_sector_index(0), -1);

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &action));
    EXPECT_EQ(sectors[0].ambient_sound_id, 0);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, WrongTargetKindFailsWithoutMutation)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();
    sdl3d_registered_actor *actor = sdl3d_actor_registry_add(registry, "actor");
    ASSERT_NE(actor, nullptr);
    sdl3d_level level{};
    sdl3d_sector sectors[1]{};
    level.sector_count = 1;
    sectors[0].damage_per_second = 7.0f;

    sdl3d_logic_target_context context{};
    context.registry = registry;
    context.level = &level;
    context.sectors = sectors;
    context.sector_count = 1;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_action actor_action =
        sdl3d_logic_action_make_set_actor_active(sdl3d_logic_target_sector_index(0), false);
    sdl3d_logic_action sector_action =
        sdl3d_logic_action_make_set_sector_damage(sdl3d_logic_target_actor_id(actor->id), 12.0f);

    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &actor_action));
    EXPECT_TRUE(actor->active);
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &sector_action));
    EXPECT_FLOAT_EQ(sectors[0].damage_per_second, 7.0f);

    sdl3d_actor_registry_destroy(registry);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, InvalidLogicActionsAreRejected)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_logic_action none{};

    EXPECT_FALSE(sdl3d_logic_world_execute_action(nullptr, &none));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, nullptr));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &none));
    EXPECT_EQ(sdl3d_logic_world_bind_logic_action(nullptr, 1, &none), 0);
    EXPECT_EQ(sdl3d_logic_world_bind_logic_action(world, 1, nullptr), 0);
    EXPECT_EQ(sdl3d_logic_world_bind_logic_action(world, 1, &none), 0);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldSensors, ContactSensorEnterEmitsPayload)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 200, capture_logic_signal, &capture), 0);

    sdl3d_logic_contact_sensor sensor{};
    sdl3d_logic_contact_sensor_init(
        &sensor, 7, sdl3d_bounding_box{sdl3d_vec3_make(0.0f, 0.0f, 0.0f), sdl3d_vec3_make(2.0f, 2.0f, 2.0f)}, 200,
        SDL3D_TRIGGER_EDGE_ENTER);

    sdl3d_logic_sensor_result outside =
        sdl3d_logic_contact_sensor_update(&sensor, world, sdl3d_vec3_make(-1.0f, 1.0f, 1.0f));
    EXPECT_FALSE(outside.active);
    EXPECT_FALSE(outside.emitted);
    EXPECT_EQ(capture.count, 0);

    sdl3d_logic_sensor_result inside =
        sdl3d_logic_contact_sensor_update(&sensor, world, sdl3d_vec3_make(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(inside.active);
    EXPECT_TRUE(inside.emitted);
    EXPECT_EQ(inside.event, SDL3D_LOGIC_SENSOR_EVENT_ENTER);
    ASSERT_EQ(capture.count, 1);
    EXPECT_EQ(capture.signal_id, 200);
    EXPECT_EQ(capture.sensor_id, 7);
    EXPECT_EQ(capture.event, SDL3D_LOGIC_SENSOR_EVENT_ENTER);
    EXPECT_TRUE(capture.inside);
    EXPECT_FLOAT_EQ(capture.sample_position.x, 1.0f);
    EXPECT_FLOAT_EQ(capture.sample_position.y, 1.0f);
    EXPECT_FLOAT_EQ(capture.sample_position.z, 1.0f);

    sdl3d_logic_sensor_result still_inside =
        sdl3d_logic_contact_sensor_update(&sensor, world, sdl3d_vec3_make(1.5f, 1.0f, 1.0f));
    EXPECT_TRUE(still_inside.active);
    EXPECT_FALSE(still_inside.emitted);
    EXPECT_EQ(capture.count, 1);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldSensors, ContactSensorBothEmitsExit)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 201, capture_logic_signal, &capture), 0);

    sdl3d_logic_contact_sensor sensor{};
    sdl3d_logic_contact_sensor_init(
        &sensor, 8, sdl3d_bounding_box{sdl3d_vec3_make(0.0f, 0.0f, 0.0f), sdl3d_vec3_make(2.0f, 2.0f, 2.0f)}, 201,
        SDL3D_TRIGGER_EDGE_BOTH);

    EXPECT_TRUE(sdl3d_logic_contact_sensor_update(&sensor, world, sdl3d_vec3_make(1.0f, 1.0f, 1.0f)).emitted);
    sdl3d_logic_sensor_result exit =
        sdl3d_logic_contact_sensor_update(&sensor, world, sdl3d_vec3_make(3.0f, 1.0f, 1.0f));

    EXPECT_FALSE(exit.active);
    EXPECT_TRUE(exit.emitted);
    EXPECT_EQ(exit.event, SDL3D_LOGIC_SENSOR_EVENT_EXIT);
    ASSERT_EQ(capture.count, 2);
    EXPECT_EQ(capture.sensor_id, 8);
    EXPECT_EQ(capture.event, SDL3D_LOGIC_SENSOR_EVENT_EXIT);
    EXPECT_FALSE(capture.inside);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldSensors, ContactSensorLevelEmitsEveryInsideUpdate)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 202, capture_logic_signal, &capture), 0);

    sdl3d_logic_contact_sensor sensor{};
    sdl3d_logic_contact_sensor_init(
        &sensor, 9, sdl3d_bounding_box{sdl3d_vec3_make(0.0f, 0.0f, 0.0f), sdl3d_vec3_make(2.0f, 2.0f, 2.0f)}, 202,
        SDL3D_TRIGGER_LEVEL);

    sdl3d_logic_sensor_result first =
        sdl3d_logic_contact_sensor_update(&sensor, world, sdl3d_vec3_make(1.0f, 1.0f, 1.0f));
    sdl3d_logic_sensor_result second =
        sdl3d_logic_contact_sensor_update(&sensor, world, sdl3d_vec3_make(1.5f, 1.0f, 1.0f));

    EXPECT_TRUE(first.emitted);
    EXPECT_TRUE(second.emitted);
    EXPECT_EQ(first.event, SDL3D_LOGIC_SENSOR_EVENT_LEVEL);
    EXPECT_EQ(second.event, SDL3D_LOGIC_SENSOR_EVENT_LEVEL);
    EXPECT_EQ(capture.count, 2);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldSensors, ContactSensorResetAllowsEnterAgain)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 203, capture_logic_signal, &capture), 0);

    sdl3d_logic_contact_sensor sensor{};
    sdl3d_logic_contact_sensor_init(
        &sensor, 10, sdl3d_bounding_box{sdl3d_vec3_make(0.0f, 0.0f, 0.0f), sdl3d_vec3_make(2.0f, 2.0f, 2.0f)}, 203,
        SDL3D_TRIGGER_EDGE_ENTER);

    EXPECT_TRUE(sdl3d_logic_contact_sensor_update(&sensor, world, sdl3d_vec3_make(1.0f, 1.0f, 1.0f)).emitted);
    EXPECT_FALSE(sdl3d_logic_contact_sensor_update(&sensor, world, sdl3d_vec3_make(1.0f, 1.0f, 1.0f)).emitted);

    sdl3d_logic_contact_sensor_reset(&sensor);
    EXPECT_TRUE(sdl3d_logic_contact_sensor_update(&sensor, world, sdl3d_vec3_make(1.0f, 1.0f, 1.0f)).emitted);
    EXPECT_EQ(capture.count, 2);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldSensors, ContactSensorCanEvaluateWithoutWorld)
{
    sdl3d_logic_contact_sensor sensor{};
    sdl3d_logic_contact_sensor_init(
        &sensor, 17, sdl3d_bounding_box{sdl3d_vec3_make(0.0f, 0.0f, 0.0f), sdl3d_vec3_make(2.0f, 2.0f, 2.0f)}, 204,
        SDL3D_TRIGGER_EDGE_ENTER);

    sdl3d_logic_sensor_result result =
        sdl3d_logic_contact_sensor_update(&sensor, nullptr, sdl3d_vec3_make(1.0f, 1.0f, 1.0f));

    EXPECT_TRUE(result.active);
    EXPECT_FALSE(result.emitted);
    EXPECT_EQ(result.event, SDL3D_LOGIC_SENSOR_EVENT_NONE);
    EXPECT_TRUE(sensor.was_inside);
}

TEST(LogicWorldSensors, SectorSensorEnterByName)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 210, capture_logic_signal, &capture), 0);

    sdl3d_level level{};
    sdl3d_sector sectors[2]{};
    level.sector_count = 2;
    sectors[0] = make_square_sector(0.0f, 0.0f, 10.0f, 10.0f);
    sectors[1] = make_square_sector(10.0f, 0.0f, 20.0f, 10.0f);

    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = sectors;
    context.sector_count = 2;
    sdl3d_logic_world_set_target_context(world, &context);
    ASSERT_TRUE(sdl3d_logic_world_set_sector_name(world, 1, "target_room"));

    sdl3d_logic_sector_sensor sensor{};
    sdl3d_logic_sector_sensor_init(&sensor, 11, sdl3d_logic_target_sector_name("target_room"), 210,
                                   SDL3D_TRIGGER_EDGE_ENTER);

    sdl3d_logic_sensor_result result =
        sdl3d_logic_sector_sensor_update(&sensor, world, sdl3d_vec3_make(15.0f, 1.0f, 5.0f));

    EXPECT_TRUE(result.active);
    EXPECT_TRUE(result.emitted);
    EXPECT_EQ(result.event, SDL3D_LOGIC_SENSOR_EVENT_ENTER);
    ASSERT_EQ(capture.count, 1);
    EXPECT_EQ(capture.sensor_id, 11);
    EXPECT_EQ(capture.sector_index, 1);
    EXPECT_TRUE(capture.inside);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldSensors, SectorSensorExitReportsTargetSector)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 211, capture_logic_signal, &capture), 0);

    sdl3d_level level{};
    sdl3d_sector sectors[2]{};
    level.sector_count = 2;
    sectors[0] = make_square_sector(0.0f, 0.0f, 10.0f, 10.0f);
    sectors[1] = make_square_sector(10.0f, 0.0f, 20.0f, 10.0f);

    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = sectors;
    context.sector_count = 2;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_sector_sensor sensor{};
    sdl3d_logic_sector_sensor_init(&sensor, 12, sdl3d_logic_target_sector_index(1), 211, SDL3D_TRIGGER_EDGE_BOTH);

    EXPECT_TRUE(sdl3d_logic_sector_sensor_update(&sensor, world, sdl3d_vec3_make(15.0f, 1.0f, 5.0f)).emitted);
    sdl3d_logic_sensor_result exit =
        sdl3d_logic_sector_sensor_update(&sensor, world, sdl3d_vec3_make(5.0f, 1.0f, 5.0f));

    EXPECT_FALSE(exit.active);
    EXPECT_TRUE(exit.emitted);
    EXPECT_EQ(exit.event, SDL3D_LOGIC_SENSOR_EVENT_EXIT);
    ASSERT_EQ(capture.count, 2);
    EXPECT_EQ(capture.sensor_id, 12);
    EXPECT_EQ(capture.sector_index, 1);
    EXPECT_FALSE(capture.inside);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldSensors, ProximitySensorEnterExitWithActorPayload)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 220, capture_logic_signal, &capture), 0);

    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();
    sdl3d_registered_actor *actor = sdl3d_actor_registry_add(registry, "robot");
    ASSERT_NE(actor, nullptr);
    actor->position = sdl3d_vec3_make(10.0f, 0.0f, 0.0f);

    sdl3d_logic_target_context context{};
    context.registry = registry;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_proximity_sensor sensor{};
    sdl3d_logic_proximity_sensor_init(&sensor, 13, sdl3d_logic_target_actor_name("robot"), 3.0f, 220,
                                      SDL3D_TRIGGER_EDGE_BOTH);

    EXPECT_FALSE(sdl3d_logic_proximity_sensor_update(&sensor, world, sdl3d_vec3_make(20.0f, 0.0f, 0.0f)).emitted);
    sdl3d_logic_sensor_result enter =
        sdl3d_logic_proximity_sensor_update(&sensor, world, sdl3d_vec3_make(12.0f, 0.0f, 0.0f));
    EXPECT_TRUE(enter.active);
    EXPECT_TRUE(enter.emitted);
    EXPECT_EQ(enter.event, SDL3D_LOGIC_SENSOR_EVENT_ENTER);
    ASSERT_EQ(capture.count, 1);
    EXPECT_EQ(capture.actor_id, actor->id);
    EXPECT_STREQ(capture.actor_name, "robot");
    EXPECT_FLOAT_EQ(capture.distance, 2.0f);

    sdl3d_logic_sensor_result exit =
        sdl3d_logic_proximity_sensor_update(&sensor, world, sdl3d_vec3_make(20.0f, 0.0f, 0.0f));
    EXPECT_FALSE(exit.active);
    EXPECT_TRUE(exit.emitted);
    EXPECT_EQ(exit.event, SDL3D_LOGIC_SENSOR_EVENT_EXIT);
    ASSERT_EQ(capture.count, 2);
    EXPECT_FALSE(capture.inside);
    EXPECT_EQ(capture.actor_id, actor->id);

    sdl3d_actor_registry_destroy(registry);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldSensors, InvalidSensorsAreSafe)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);

    sdl3d_logic_contact_sensor contact{};
    sdl3d_logic_contact_sensor_init(
        &contact, 14, sdl3d_bounding_box{sdl3d_vec3_make(0.0f, 0.0f, 0.0f), sdl3d_vec3_make(1.0f, 1.0f, 1.0f)}, 230,
        SDL3D_TRIGGER_EDGE_ENTER);

    EXPECT_FALSE(sdl3d_logic_contact_sensor_update(&contact, nullptr, sdl3d_vec3_make(0.5f, 0.5f, 0.5f)).emitted);
    EXPECT_TRUE(contact.was_inside);
    EXPECT_FALSE(sdl3d_logic_contact_sensor_update(nullptr, world, sdl3d_vec3_make(0.5f, 0.5f, 0.5f)).emitted);
    sdl3d_logic_contact_sensor_reset(nullptr);

    contact.enabled = false;
    sdl3d_logic_sensor_result disabled =
        sdl3d_logic_contact_sensor_update(&contact, world, sdl3d_vec3_make(0.5f, 0.5f, 0.5f));
    EXPECT_FALSE(disabled.active);
    EXPECT_FALSE(disabled.emitted);

    sdl3d_logic_sector_sensor sector{};
    sdl3d_logic_sector_sensor_init(&sector, 15, sdl3d_logic_target_sector_name("missing"), 231,
                                   SDL3D_TRIGGER_EDGE_ENTER);
    EXPECT_FALSE(sdl3d_logic_sector_sensor_update(&sector, world, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)).emitted);
    sdl3d_logic_sector_sensor_reset(nullptr);

    sdl3d_logic_proximity_sensor proximity{};
    sdl3d_logic_proximity_sensor_init(&proximity, 16, sdl3d_logic_target_actor_name("missing"), 1.0f, 232,
                                      SDL3D_TRIGGER_EDGE_ENTER);
    EXPECT_FALSE(sdl3d_logic_proximity_sensor_update(&proximity, world, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)).emitted);
    sdl3d_logic_proximity_sensor_reset(nullptr);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

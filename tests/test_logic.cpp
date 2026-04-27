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
    int entity_id = -1;
    int output_index = -1;
    int count_value = -1;
    int threshold = -1;
    bool inside = false;
    bool state = false;
    bool matched = false;
    float distance = -1.0f;
    float damage_per_second = 0.0f;
    float damage_amount = 0.0f;
    float dt = 0.0f;
    sdl3d_vec3 sample_position = {};
    char actor_name[64] = {};
    char entity_type[32] = {};
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
    capture->entity_id = sdl3d_properties_get_int(payload, "entity_id", -1);
    capture->output_index = sdl3d_properties_get_int(payload, "output_index", -1);
    capture->count_value = sdl3d_properties_get_int(payload, "count", -1);
    capture->threshold = sdl3d_properties_get_int(payload, "threshold", -1);
    capture->distance = sdl3d_properties_get_float(payload, "distance", -1.0f);
    capture->damage_per_second = sdl3d_properties_get_float(payload, "damage_per_second", 0.0f);
    capture->damage_amount = sdl3d_properties_get_float(payload, "damage_amount", 0.0f);
    capture->dt = sdl3d_properties_get_float(payload, "dt", 0.0f);
    capture->state = sdl3d_properties_get_bool(payload, "state", false);
    capture->matched = sdl3d_properties_get_bool(payload, "matched", false);
    capture->sample_position = sdl3d_properties_get_vec3(payload, "sample_position", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    const char *actor_name = sdl3d_properties_get_string(payload, "actor_name", "");
    SDL_snprintf(capture->actor_name, sizeof(capture->actor_name), "%s", actor_name);
    const char *entity_type = sdl3d_properties_get_string(payload, "entity_type", "");
    SDL_snprintf(capture->entity_type, sizeof(capture->entity_type), "%s", entity_type);
}

struct logic_adapter_capture
{
    int teleport_count = 0;
    int set_camera_count = 0;
    int restore_camera_count = 0;
    int set_ambient_count = 0;
    int trigger_feedback_count = 0;
    int door_command_count = 0;
    int set_sector_geometry_count = 0;
    int launch_player_count = 0;
    int set_effect_active_count = 0;
    int last_teleporter_id = -1;
    int last_ambient_id = -1;
    int last_door_id = -1;
    int last_sector_index = -1;
    float last_ambient_fade = -1.0f;
    float last_feedback_duration = -1.0f;
    float last_auto_close_seconds = -2.0f;
    char last_camera_name[64] = {};
    char last_feedback_name[64] = {};
    char last_door_name[64] = {};
    sdl3d_logic_door_command last_door_command = SDL3D_LOGIC_DOOR_OPEN;
    sdl3d_teleport_destination last_destination = {};
    sdl3d_camera3d last_camera = {};
    sdl3d_sector_geometry last_geometry = {};
    sdl3d_vec3 last_launch_velocity = {};
    bool last_effect_active = false;
    char last_effect_name[64] = {};
};

static bool capture_teleport_player(void *userdata, const sdl3d_teleport_destination *destination,
                                    const sdl3d_properties *payload)
{
    logic_adapter_capture *capture = (logic_adapter_capture *)userdata;
    if (capture == nullptr || destination == nullptr)
    {
        return false;
    }

    capture->teleport_count++;
    capture->last_destination = *destination;
    capture->last_teleporter_id = sdl3d_properties_get_int(payload, "teleporter_id", -1);
    return true;
}

static bool capture_set_active_camera(void *userdata, const char *camera_name, const sdl3d_camera3d *camera,
                                      const sdl3d_properties *payload)
{
    (void)payload;
    logic_adapter_capture *capture = (logic_adapter_capture *)userdata;
    if (capture == nullptr || camera == nullptr)
    {
        return false;
    }

    capture->set_camera_count++;
    capture->last_camera = *camera;
    SDL_snprintf(capture->last_camera_name, sizeof(capture->last_camera_name), "%s",
                 camera_name != nullptr ? camera_name : "");
    return true;
}

static bool capture_restore_camera(void *userdata, const sdl3d_properties *payload)
{
    (void)payload;
    logic_adapter_capture *capture = (logic_adapter_capture *)userdata;
    if (capture == nullptr)
    {
        return false;
    }

    capture->restore_camera_count++;
    return true;
}

static bool capture_set_ambient(void *userdata, int ambient_id, float fade_seconds, const sdl3d_properties *payload)
{
    (void)payload;
    logic_adapter_capture *capture = (logic_adapter_capture *)userdata;
    if (capture == nullptr)
    {
        return false;
    }

    capture->set_ambient_count++;
    capture->last_ambient_id = ambient_id;
    capture->last_ambient_fade = fade_seconds;
    return true;
}

static bool capture_trigger_feedback(void *userdata, const char *feedback_name, float duration_seconds,
                                     const sdl3d_properties *payload)
{
    (void)payload;
    logic_adapter_capture *capture = (logic_adapter_capture *)userdata;
    if (capture == nullptr || feedback_name == nullptr)
    {
        return false;
    }

    capture->trigger_feedback_count++;
    capture->last_feedback_duration = duration_seconds;
    SDL_snprintf(capture->last_feedback_name, sizeof(capture->last_feedback_name), "%s", feedback_name);
    return true;
}

static bool capture_door_command(void *userdata, const char *door_name, int door_id, sdl3d_logic_door_command command,
                                 float auto_close_seconds, const sdl3d_properties *payload)
{
    (void)payload;
    logic_adapter_capture *capture = (logic_adapter_capture *)userdata;
    if (capture == nullptr)
    {
        return false;
    }

    capture->door_command_count++;
    capture->last_door_id = door_id;
    capture->last_door_command = command;
    capture->last_auto_close_seconds = auto_close_seconds;
    SDL_snprintf(capture->last_door_name, sizeof(capture->last_door_name), "%s", door_name != nullptr ? door_name : "");
    return true;
}

static bool capture_set_sector_geometry(void *userdata, int sector_index, const sdl3d_sector_geometry *geometry,
                                        const sdl3d_properties *payload)
{
    (void)payload;
    logic_adapter_capture *capture = (logic_adapter_capture *)userdata;
    if (capture == nullptr || geometry == nullptr)
    {
        return false;
    }

    capture->set_sector_geometry_count++;
    capture->last_sector_index = sector_index;
    capture->last_geometry = *geometry;
    return true;
}

static bool capture_launch_player(void *userdata, sdl3d_vec3 velocity, const sdl3d_properties *payload)
{
    (void)payload;
    logic_adapter_capture *capture = (logic_adapter_capture *)userdata;
    if (capture == nullptr)
    {
        return false;
    }

    capture->launch_player_count++;
    capture->last_launch_velocity = velocity;
    return true;
}

static bool capture_set_effect_active(void *userdata, const char *effect_name, bool active,
                                      const sdl3d_properties *payload)
{
    (void)payload;
    logic_adapter_capture *capture = (logic_adapter_capture *)userdata;
    if (capture == nullptr || effect_name == nullptr)
    {
        return false;
    }

    capture->set_effect_active_count++;
    capture->last_effect_active = active;
    SDL_snprintf(capture->last_effect_name, sizeof(capture->last_effect_name), "%s", effect_name);
    return true;
}

struct ordered_signal_capture
{
    int count = 0;
    int signals[16] = {};
    int entity_ids[16] = {};
    int payload_value[16] = {};
};

static void capture_ordered_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    ordered_signal_capture *capture = (ordered_signal_capture *)userdata;
    if (capture->count >= (int)SDL_arraysize(capture->signals))
        return;

    const int index = capture->count++;
    capture->signals[index] = signal_id;
    capture->entity_ids[index] = sdl3d_properties_get_int(payload, "entity_id", -1);
    capture->payload_value[index] = sdl3d_properties_get_int(payload, "payload_value", -1);
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

TEST(LogicWorld, GameAdaptersCanBeSetAndCleared)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_adapter_capture capture{};

    EXPECT_EQ(sdl3d_logic_world_get_game_adapters(world), nullptr);
    sdl3d_logic_world_set_game_adapters(nullptr, nullptr);

    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.teleport_player = capture_teleport_player;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    const sdl3d_logic_game_adapters *stored = sdl3d_logic_world_get_game_adapters(world);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->userdata, &capture);
    EXPECT_EQ(stored->teleport_player, capture_teleport_player);

    sdl3d_logic_world_set_game_adapters(world, nullptr);
    EXPECT_EQ(sdl3d_logic_world_get_game_adapters(world), nullptr);
    EXPECT_EQ(sdl3d_logic_world_get_game_adapters(nullptr), nullptr);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
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

TEST(LogicWorldActions, TeleportPlayerFromPayloadForwardsSignalPayload)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.teleport_player = capture_teleport_player;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_logic_action action = sdl3d_logic_action_make_teleport_player_from_payload();
    ASSERT_GT(sdl3d_logic_world_bind_logic_action(world, 81, &action), 0);

    sdl3d_properties *payload = sdl3d_properties_create();
    sdl3d_properties_set_int(payload, "teleporter_id", 17);
    sdl3d_properties_set_vec3(payload, "destination", sdl3d_vec3_make(4.0f, 5.0f, 6.0f));
    sdl3d_properties_set_float(payload, "destination_yaw", 1.25f);
    sdl3d_properties_set_float(payload, "destination_pitch", -0.125f);
    sdl3d_properties_set_bool(payload, "use_yaw", true);
    sdl3d_properties_set_bool(payload, "use_pitch", true);

    sdl3d_signal_emit(bus, 81, payload);

    EXPECT_EQ(capture.teleport_count, 1);
    EXPECT_EQ(capture.last_teleporter_id, 17);
    EXPECT_FLOAT_EQ(capture.last_destination.position.x, 4.0f);
    EXPECT_FLOAT_EQ(capture.last_destination.position.y, 5.0f);
    EXPECT_FLOAT_EQ(capture.last_destination.position.z, 6.0f);
    EXPECT_FLOAT_EQ(capture.last_destination.yaw, 1.25f);
    EXPECT_FLOAT_EQ(capture.last_destination.pitch, -0.125f);
    EXPECT_TRUE(capture.last_destination.use_yaw);
    EXPECT_TRUE(capture.last_destination.use_pitch);

    sdl3d_properties_destroy(payload);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, StaticTeleportPlayerUsesConfiguredDestination)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.teleport_player = capture_teleport_player;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_teleport_destination destination{};
    destination.position = sdl3d_vec3_make(-2.0f, 3.0f, 8.0f);
    destination.yaw = 0.5f;
    destination.use_yaw = true;

    sdl3d_logic_action action = sdl3d_logic_action_make_teleport_player(destination);
    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &action));

    EXPECT_EQ(capture.teleport_count, 1);
    EXPECT_FLOAT_EQ(capture.last_destination.position.x, -2.0f);
    EXPECT_FLOAT_EQ(capture.last_destination.position.y, 3.0f);
    EXPECT_FLOAT_EQ(capture.last_destination.position.z, 8.0f);
    EXPECT_FLOAT_EQ(capture.last_destination.yaw, 0.5f);
    EXPECT_TRUE(capture.last_destination.use_yaw);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, TeleportPlayerRejectsMissingAdapterOrPayload)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_logic_action action = sdl3d_logic_action_make_teleport_player_from_payload();

    EXPECT_FALSE(sdl3d_logic_world_execute_action_with_payload(world, &action, nullptr));

    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.teleport_player = capture_teleport_player;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    EXPECT_FALSE(sdl3d_logic_world_execute_action_with_payload(world, &action, nullptr));
    EXPECT_EQ(capture.teleport_count, 0);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, CameraActionsUseGameAdaptersInSignalOrder)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.set_active_camera = capture_set_active_camera;
    adapters.restore_camera = capture_restore_camera;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_camera3d camera{};
    camera.position = sdl3d_vec3_make(1.0f, 2.0f, 3.0f);
    camera.target = sdl3d_vec3_make(4.0f, 5.0f, 6.0f);
    camera.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    camera.fovy = 75.0f;
    camera.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_logic_action set_camera = sdl3d_logic_action_make_set_active_camera("security", &camera);
    sdl3d_logic_action restore_camera = sdl3d_logic_action_make_restore_camera();
    ASSERT_GT(sdl3d_logic_world_bind_logic_action(world, 91, &set_camera), 0);
    ASSERT_GT(sdl3d_logic_world_bind_logic_action(world, 91, &restore_camera), 0);

    sdl3d_signal_emit(bus, 91, nullptr);

    EXPECT_EQ(capture.set_camera_count, 1);
    EXPECT_EQ(capture.restore_camera_count, 1);
    EXPECT_STREQ(capture.last_camera_name, "security");
    EXPECT_FLOAT_EQ(capture.last_camera.position.x, 1.0f);
    EXPECT_FLOAT_EQ(capture.last_camera.position.y, 2.0f);
    EXPECT_FLOAT_EQ(capture.last_camera.position.z, 3.0f);
    EXPECT_FLOAT_EQ(capture.last_camera.fovy, 75.0f);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, CameraActionsRejectMissingAdapters)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_camera3d camera{};
    sdl3d_logic_action set_camera = sdl3d_logic_action_make_set_active_camera("missing", &camera);
    sdl3d_logic_action restore_camera = sdl3d_logic_action_make_restore_camera();

    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &set_camera));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &restore_camera));

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, SetAmbientFromPayloadUsesGameAdapter)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.set_ambient = capture_set_ambient;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_logic_action action = sdl3d_logic_action_make_set_ambient_from_payload("ambient_sound_id", 1.25f);
    ASSERT_GT(sdl3d_logic_world_bind_logic_action(world, 92, &action), 0);

    sdl3d_properties *payload = sdl3d_properties_create();
    sdl3d_properties_set_int(payload, "ambient_sound_id", 7);
    sdl3d_signal_emit(bus, 92, payload);

    EXPECT_EQ(capture.set_ambient_count, 1);
    EXPECT_EQ(capture.last_ambient_id, 7);
    EXPECT_FLOAT_EQ(capture.last_ambient_fade, 1.25f);

    sdl3d_properties_destroy(payload);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, StaticAmbientAndFeedbackActionsUseGameAdapters)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.set_ambient = capture_set_ambient;
    adapters.trigger_feedback = capture_trigger_feedback;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_logic_action ambient = sdl3d_logic_action_make_set_ambient(3, -1.0f);
    sdl3d_logic_action feedback = sdl3d_logic_action_make_trigger_feedback("ambient_zone", -2.0f);

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &ambient));
    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &feedback));
    EXPECT_EQ(capture.set_ambient_count, 1);
    EXPECT_EQ(capture.last_ambient_id, 3);
    EXPECT_FLOAT_EQ(capture.last_ambient_fade, 0.0f);
    EXPECT_EQ(capture.trigger_feedback_count, 1);
    EXPECT_STREQ(capture.last_feedback_name, "ambient_zone");
    EXPECT_FLOAT_EQ(capture.last_feedback_duration, 0.0f);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, AmbientAndFeedbackRejectMissingInputs)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_logic_action ambient = sdl3d_logic_action_make_set_ambient_from_payload("ambient_sound_id", 1.0f);
    sdl3d_logic_action feedback = sdl3d_logic_action_make_trigger_feedback(nullptr, 1.0f);

    EXPECT_FALSE(sdl3d_logic_world_execute_action_with_payload(world, &ambient, nullptr));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &feedback));

    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.set_ambient = capture_set_ambient;
    adapters.trigger_feedback = capture_trigger_feedback;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_properties *payload = sdl3d_properties_create();
    EXPECT_FALSE(sdl3d_logic_world_execute_action_with_payload(world, &ambient, payload));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &feedback));
    EXPECT_EQ(capture.set_ambient_count, 0);
    EXPECT_EQ(capture.trigger_feedback_count, 0);

    sdl3d_properties_destroy(payload);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, DoorCommandFromPayloadUsesGameAdapter)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.door_command = capture_door_command;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_logic_action action = sdl3d_logic_action_make_door_command_from_payload(SDL3D_LOGIC_DOOR_OPEN);
    ASSERT_GT(sdl3d_logic_world_bind_logic_action(world, 93, &action), 0);

    sdl3d_properties *payload = sdl3d_properties_create();
    sdl3d_properties_set_int(payload, "door_id", 12);
    sdl3d_properties_set_string(payload, "door_name", "nukage_east");
    sdl3d_signal_emit(bus, 93, payload);

    EXPECT_EQ(capture.door_command_count, 1);
    EXPECT_EQ(capture.last_door_id, 12);
    EXPECT_EQ(capture.last_door_command, SDL3D_LOGIC_DOOR_OPEN);
    EXPECT_FLOAT_EQ(capture.last_auto_close_seconds, -1.0f);
    EXPECT_STREQ(capture.last_door_name, "nukage_east");

    sdl3d_properties_destroy(payload);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, DoorCommandFromPayloadCanSetAutoCloseDelay)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.door_command = capture_door_command;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_logic_action action = sdl3d_logic_action_make_door_command_from_payload_ex(SDL3D_LOGIC_DOOR_OPEN, 5.0f);
    sdl3d_properties *payload = sdl3d_properties_create();
    sdl3d_properties_set_int(payload, "door_id", 12);

    EXPECT_TRUE(sdl3d_logic_world_execute_action_with_payload(world, &action, payload));
    EXPECT_EQ(capture.door_command_count, 1);
    EXPECT_EQ(capture.last_door_id, 12);
    EXPECT_EQ(capture.last_door_command, SDL3D_LOGIC_DOOR_OPEN);
    EXPECT_FLOAT_EQ(capture.last_auto_close_seconds, 5.0f);

    sdl3d_properties_destroy(payload);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, StaticDoorCommandAndSectorGeometryUseGameAdapters)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_level level{};
    level.sector_count = 1;
    sdl3d_sector sector = make_square_sector(0.0f, 0.0f, 2.0f, 2.0f);
    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = &sector;
    context.sector_count = 1;
    sdl3d_logic_world_set_target_context(world, &context);

    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.door_command = capture_door_command;
    adapters.set_sector_geometry = capture_set_sector_geometry;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_sector_geometry geometry{};
    geometry.floor_y = 2.5f;
    geometry.ceil_y = 9.0f;
    geometry.floor_normal[1] = 1.0f;
    geometry.ceil_normal[1] = -1.0f;

    sdl3d_logic_action door = sdl3d_logic_action_make_door_command_ex("nukage_north", 3, SDL3D_LOGIC_DOOR_TOGGLE, 5.0f);
    sdl3d_logic_action lift = sdl3d_logic_action_make_set_sector_geometry(sdl3d_logic_target_sector_index(0), geometry);

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &door));
    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &lift));
    EXPECT_EQ(capture.door_command_count, 1);
    EXPECT_EQ(capture.last_door_id, 3);
    EXPECT_EQ(capture.last_door_command, SDL3D_LOGIC_DOOR_TOGGLE);
    EXPECT_FLOAT_EQ(capture.last_auto_close_seconds, 5.0f);
    EXPECT_STREQ(capture.last_door_name, "nukage_north");
    EXPECT_EQ(capture.set_sector_geometry_count, 1);
    EXPECT_EQ(capture.last_sector_index, 0);
    EXPECT_FLOAT_EQ(capture.last_geometry.floor_y, 2.5f);
    EXPECT_FLOAT_EQ(capture.last_geometry.ceil_y, 9.0f);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, LaunchPlayerUsesGameAdapter)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.launch_player = capture_launch_player;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_logic_action launch = sdl3d_logic_action_make_launch_player(sdl3d_vec3_make(0.0f, 14.0f, 0.0f));

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &launch));
    EXPECT_EQ(capture.launch_player_count, 1);
    EXPECT_FLOAT_EQ(capture.last_launch_velocity.x, 0.0f);
    EXPECT_FLOAT_EQ(capture.last_launch_velocity.y, 14.0f);
    EXPECT_FLOAT_EQ(capture.last_launch_velocity.z, 0.0f);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, SetEffectActiveUsesGameAdapter)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.set_effect_active = capture_set_effect_active;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_logic_action enable = sdl3d_logic_action_make_set_effect_active("nukage_particles", true);
    sdl3d_logic_action disable = sdl3d_logic_action_make_set_effect_active("nukage_particles", false);

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &enable));
    EXPECT_EQ(capture.set_effect_active_count, 1);
    EXPECT_STREQ(capture.last_effect_name, "nukage_particles");
    EXPECT_TRUE(capture.last_effect_active);

    EXPECT_TRUE(sdl3d_logic_world_execute_action(world, &disable));
    EXPECT_EQ(capture.set_effect_active_count, 2);
    EXPECT_FALSE(capture.last_effect_active);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldActions, DoorSectorGeometryLaunchAndEffectRejectMissingInputs)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_logic_action door = sdl3d_logic_action_make_door_command_from_payload(SDL3D_LOGIC_DOOR_OPEN);
    sdl3d_logic_action lift =
        sdl3d_logic_action_make_set_sector_geometry(sdl3d_logic_target_sector_index(0), sdl3d_sector_geometry{});
    sdl3d_logic_action launch = sdl3d_logic_action_make_launch_player(sdl3d_vec3_make(0.0f, 12.0f, 0.0f));
    sdl3d_logic_action effect = sdl3d_logic_action_make_set_effect_active("nukage_particles", true);
    sdl3d_logic_action unnamed_effect = sdl3d_logic_action_make_set_effect_active(nullptr, true);

    EXPECT_FALSE(sdl3d_logic_world_execute_action_with_payload(world, &door, nullptr));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &lift));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &launch));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &effect));

    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.door_command = capture_door_command;
    adapters.set_sector_geometry = capture_set_sector_geometry;
    adapters.set_effect_active = capture_set_effect_active;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_properties *payload = sdl3d_properties_create();
    EXPECT_FALSE(sdl3d_logic_world_execute_action_with_payload(world, &door, payload));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &lift));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &launch));
    EXPECT_FALSE(sdl3d_logic_world_execute_action(world, &unnamed_effect));
    EXPECT_EQ(capture.door_command_count, 0);
    EXPECT_EQ(capture.set_sector_geometry_count, 0);
    EXPECT_EQ(capture.launch_player_count, 0);
    EXPECT_EQ(capture.set_effect_active_count, 0);

    sdl3d_properties_destroy(payload);
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

TEST(LogicWorldSensors, SectorDamageSensorEmitsDamagePayload)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 230, capture_logic_signal, &capture), 0);

    sdl3d_level level{};
    sdl3d_sector sectors[2]{};
    level.sector_count = 2;
    sectors[0] = make_square_sector(0.0f, 0.0f, 10.0f, 10.0f);
    sectors[1] = make_square_sector(10.0f, 0.0f, 20.0f, 10.0f);
    sectors[1].damage_per_second = 12.0f;

    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = sectors;
    context.sector_count = 2;
    sdl3d_logic_world_set_target_context(world, &context);

    sdl3d_logic_sector_damage_sensor sensor{};
    sdl3d_logic_sector_damage_sensor_init(&sensor, 31, 230, 1.0f);

    EXPECT_FALSE(
        sdl3d_logic_sector_damage_sensor_update(&sensor, world, sdl3d_vec3_make(5.0f, 1.0f, 5.0f), 0.25f).emitted);
    sdl3d_logic_sensor_result damaged =
        sdl3d_logic_sector_damage_sensor_update(&sensor, world, sdl3d_vec3_make(15.0f, 1.0f, 5.0f), 0.25f);

    EXPECT_TRUE(damaged.active);
    EXPECT_TRUE(damaged.emitted);
    EXPECT_EQ(damaged.event, SDL3D_LOGIC_SENSOR_EVENT_LEVEL);
    ASSERT_EQ(capture.count, 1);
    EXPECT_EQ(capture.sensor_id, 31);
    EXPECT_EQ(capture.sector_index, 1);
    EXPECT_TRUE(capture.inside);
    EXPECT_FLOAT_EQ(capture.damage_per_second, 12.0f);
    EXPECT_FLOAT_EQ(capture.damage_amount, 3.0f);
    EXPECT_FLOAT_EQ(capture.dt, 0.25f);

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

    sdl3d_logic_sector_damage_sensor damage{};
    sdl3d_logic_sector_damage_sensor_init(&damage, 17, 233, -1.0f);
    EXPECT_FALSE(
        sdl3d_logic_sector_damage_sensor_update(nullptr, world, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), 0.1f).emitted);
    EXPECT_FALSE(
        sdl3d_logic_sector_damage_sensor_update(&damage, nullptr, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), 0.1f).emitted);
    EXPECT_FALSE(
        sdl3d_logic_sector_damage_sensor_update(&damage, world, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), 0.0f).emitted);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, RelayEmitsOutputsInOrderAndForwardsPayload)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    ordered_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 300, capture_ordered_signal, &capture), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 301, capture_ordered_signal, &capture), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 302, capture_ordered_signal, &capture), 0);

    sdl3d_logic_relay relay{};
    sdl3d_logic_relay_init(&relay, 21);
    ASSERT_TRUE(sdl3d_logic_relay_add_output(&relay, 300));
    ASSERT_TRUE(sdl3d_logic_relay_add_output(&relay, 301));
    ASSERT_TRUE(sdl3d_logic_relay_add_output(&relay, 302));

    sdl3d_properties *payload = sdl3d_properties_create();
    sdl3d_properties_set_int(payload, "payload_value", 99);
    sdl3d_logic_entity_result result = sdl3d_logic_relay_activate(world, &relay, payload);

    EXPECT_TRUE(result.emitted);
    EXPECT_EQ(result.signal_id, 302);
    EXPECT_EQ(result.output_index, 2);
    ASSERT_EQ(capture.count, 3);
    EXPECT_EQ(capture.signals[0], 300);
    EXPECT_EQ(capture.signals[1], 301);
    EXPECT_EQ(capture.signals[2], 302);
    EXPECT_EQ(capture.payload_value[0], 99);
    EXPECT_EQ(capture.payload_value[1], 99);
    EXPECT_EQ(capture.payload_value[2], 99);

    sdl3d_properties_destroy(payload);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, ToggleFlipsAndEmitsStatePayload)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 310, capture_logic_signal, &capture), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 311, capture_logic_signal, &capture), 0);

    sdl3d_logic_toggle toggle{};
    sdl3d_logic_toggle_init(&toggle, 22, false, 310, 311);

    sdl3d_logic_entity_result on = sdl3d_logic_toggle_activate(world, &toggle);
    EXPECT_TRUE(on.emitted);
    EXPECT_EQ(on.signal_id, 310);
    EXPECT_TRUE(toggle.state);
    EXPECT_EQ(capture.entity_id, 22);
    EXPECT_STREQ(capture.entity_type, "toggle");
    EXPECT_TRUE(capture.state);

    sdl3d_logic_entity_result off = sdl3d_logic_toggle_activate(world, &toggle);
    EXPECT_TRUE(off.emitted);
    EXPECT_EQ(off.signal_id, 311);
    EXPECT_FALSE(toggle.state);
    EXPECT_FALSE(capture.state);

    sdl3d_logic_toggle_reset(&toggle, true);
    EXPECT_TRUE(toggle.state);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, CounterEmitsAtThresholdAndCanResetOnFire)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 320, capture_logic_signal, &capture), 0);

    sdl3d_logic_counter counter{};
    sdl3d_logic_counter_init(&counter, 23, 3, 320, true);

    EXPECT_FALSE(sdl3d_logic_counter_activate(world, &counter).emitted);
    EXPECT_FALSE(sdl3d_logic_counter_activate(world, &counter).emitted);
    sdl3d_logic_entity_result fired = sdl3d_logic_counter_activate(world, &counter);
    EXPECT_TRUE(fired.emitted);
    EXPECT_EQ(fired.signal_id, 320);
    EXPECT_EQ(counter.count, 0);
    EXPECT_EQ(capture.entity_id, 23);
    EXPECT_STREQ(capture.entity_type, "counter");
    EXPECT_EQ(capture.count_value, 3);
    EXPECT_EQ(capture.threshold, 3);

    EXPECT_FALSE(sdl3d_logic_counter_activate(world, &counter).emitted);
    EXPECT_FALSE(sdl3d_logic_counter_activate(world, &counter).emitted);
    EXPECT_TRUE(sdl3d_logic_counter_activate(world, &counter).emitted);
    EXPECT_EQ(capture.count, 2);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, NonResettingCounterFiresOnceUntilReset)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 321, capture_logic_signal, &capture), 0);

    sdl3d_logic_counter counter{};
    sdl3d_logic_counter_init(&counter, 24, 2, 321, false);

    EXPECT_FALSE(sdl3d_logic_counter_activate(world, &counter).emitted);
    EXPECT_TRUE(sdl3d_logic_counter_activate(world, &counter).emitted);
    EXPECT_FALSE(sdl3d_logic_counter_activate(world, &counter).emitted);
    EXPECT_EQ(capture.count, 1);
    EXPECT_TRUE(counter.fired);

    sdl3d_logic_counter_reset(&counter);
    EXPECT_FALSE(counter.fired);
    EXPECT_FALSE(sdl3d_logic_counter_activate(world, &counter).emitted);
    EXPECT_TRUE(sdl3d_logic_counter_activate(world, &counter).emitted);
    EXPECT_EQ(capture.count, 2);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, BranchComparesPropertyValues)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 330, capture_logic_signal, &capture), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 331, capture_logic_signal, &capture), 0);

    sdl3d_properties *props = sdl3d_properties_create();
    sdl3d_properties_set_bool(props, "locked", true);
    sdl3d_value expected{};
    expected.type = SDL3D_VALUE_BOOL;
    expected.as_bool = true;

    sdl3d_logic_branch branch{};
    sdl3d_logic_branch_init(&branch, 25, props, "locked", expected, 330, 331);

    EXPECT_TRUE(sdl3d_logic_branch_activate(world, &branch).emitted);
    EXPECT_EQ(capture.signal_id, 330);
    EXPECT_TRUE(capture.matched);

    sdl3d_properties_set_bool(props, "locked", false);
    EXPECT_TRUE(sdl3d_logic_branch_activate(world, &branch).emitted);
    EXPECT_EQ(capture.signal_id, 331);
    EXPECT_FALSE(capture.matched);

    sdl3d_properties_destroy(props);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, PayloadBranchComparesActivationPayload)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 332, capture_logic_signal, &capture), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 333, capture_logic_signal, &capture), 0);

    sdl3d_value expected{};
    expected.type = SDL3D_VALUE_INT;
    expected.as_int = 11;
    sdl3d_logic_branch branch{};
    sdl3d_logic_branch_init_payload(&branch, 26, "ambient_sound_id", expected, 332, 333);

    sdl3d_properties *payload = sdl3d_properties_create();
    sdl3d_properties_set_int(payload, "ambient_sound_id", 11);
    EXPECT_TRUE(sdl3d_logic_branch_activate_with_payload(world, &branch, payload).emitted);
    EXPECT_EQ(capture.signal_id, 332);
    EXPECT_TRUE(capture.matched);

    sdl3d_properties_set_int(payload, "ambient_sound_id", 0);
    EXPECT_TRUE(sdl3d_logic_branch_activate_with_payload(world, &branch, payload).emitted);
    EXPECT_EQ(capture.signal_id, 333);
    EXPECT_FALSE(capture.matched);

    ASSERT_GT(sdl3d_logic_world_bind_branch(world, 334, &branch), 0);
    sdl3d_properties_set_int(payload, "ambient_sound_id", 11);
    sdl3d_signal_emit(bus, 334, payload);
    EXPECT_EQ(capture.signal_id, 332);
    EXPECT_TRUE(capture.matched);

    sdl3d_properties_destroy(payload);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, RandomSelectorIsDeterministicAfterReset)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    ordered_signal_capture first{};
    ASSERT_GT(sdl3d_signal_connect(bus, 340, capture_ordered_signal, &first), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 341, capture_ordered_signal, &first), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 342, capture_ordered_signal, &first), 0);

    sdl3d_logic_random selector{};
    sdl3d_logic_random_init(&selector, 26, 123);
    ASSERT_TRUE(sdl3d_logic_random_add_output(&selector, 340));
    ASSERT_TRUE(sdl3d_logic_random_add_output(&selector, 341));
    ASSERT_TRUE(sdl3d_logic_random_add_output(&selector, 342));

    int expected[5] = {};
    for (int i = 0; i < 5; ++i)
    {
        sdl3d_logic_entity_result result = sdl3d_logic_random_activate(world, &selector);
        ASSERT_TRUE(result.emitted);
        expected[i] = result.signal_id;
    }

    sdl3d_logic_random_reset(&selector, 123);
    for (int i = 0; i < 5; ++i)
    {
        sdl3d_logic_entity_result result = sdl3d_logic_random_activate(world, &selector);
        ASSERT_TRUE(result.emitted);
        EXPECT_EQ(result.signal_id, expected[i]);
    }

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, SequenceEmitsInOrderThenStopsUntilReset)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    ordered_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 350, capture_ordered_signal, &capture), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 351, capture_ordered_signal, &capture), 0);

    sdl3d_logic_sequence sequence{};
    sdl3d_logic_sequence_init(&sequence, 27, false);
    ASSERT_TRUE(sdl3d_logic_sequence_add_output(&sequence, 350));
    ASSERT_TRUE(sdl3d_logic_sequence_add_output(&sequence, 351));

    EXPECT_EQ(sdl3d_logic_sequence_activate(world, &sequence).signal_id, 350);
    EXPECT_EQ(sdl3d_logic_sequence_activate(world, &sequence).signal_id, 351);
    EXPECT_FALSE(sdl3d_logic_sequence_activate(world, &sequence).emitted);
    ASSERT_EQ(capture.count, 2);
    EXPECT_EQ(capture.signals[0], 350);
    EXPECT_EQ(capture.signals[1], 351);

    sdl3d_logic_sequence_reset(&sequence);
    EXPECT_EQ(sdl3d_logic_sequence_activate(world, &sequence).signal_id, 350);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, LoopingSequenceWraps)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    ordered_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 352, capture_ordered_signal, &capture), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 353, capture_ordered_signal, &capture), 0);

    sdl3d_logic_sequence sequence{};
    sdl3d_logic_sequence_init(&sequence, 28, true);
    ASSERT_TRUE(sdl3d_logic_sequence_add_output(&sequence, 352));
    ASSERT_TRUE(sdl3d_logic_sequence_add_output(&sequence, 353));

    EXPECT_EQ(sdl3d_logic_sequence_activate(world, &sequence).signal_id, 352);
    EXPECT_EQ(sdl3d_logic_sequence_activate(world, &sequence).signal_id, 353);
    EXPECT_EQ(sdl3d_logic_sequence_activate(world, &sequence).signal_id, 352);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, OnceGatePassesFirstActivationUntilReset)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 360, capture_logic_signal, &capture), 0);

    sdl3d_logic_once once{};
    sdl3d_logic_once_init(&once, 29, 360);

    EXPECT_TRUE(sdl3d_logic_once_activate(world, &once).emitted);
    EXPECT_FALSE(sdl3d_logic_once_activate(world, &once).emitted);
    EXPECT_EQ(capture.count, 1);
    EXPECT_TRUE(once.fired);

    sdl3d_logic_once_reset(&once);
    EXPECT_TRUE(sdl3d_logic_once_activate(world, &once).emitted);
    EXPECT_EQ(capture.count, 2);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, TimerEntityUpdatesAndStopsAccurately)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 370, capture_logic_signal, &capture), 0);

    sdl3d_logic_timer timer{};
    sdl3d_logic_timer_init(&timer, 30, 0.5f, 370, false, 0.0f);
    EXPECT_TRUE(sdl3d_logic_timer_start(&timer));
    EXPECT_TRUE(sdl3d_logic_timer_active(&timer));

    EXPECT_FALSE(sdl3d_logic_timer_update(world, &timer, 0.49f).emitted);
    EXPECT_EQ(capture.count, 0);
    sdl3d_logic_entity_result fired = sdl3d_logic_timer_update(world, &timer, 0.01f);
    EXPECT_TRUE(fired.emitted);
    EXPECT_FALSE(sdl3d_logic_timer_active(&timer));
    EXPECT_EQ(capture.count, 1);
    EXPECT_EQ(capture.entity_id, 30);
    EXPECT_STREQ(capture.entity_type, "timer");

    EXPECT_TRUE(sdl3d_logic_timer_start(&timer));
    sdl3d_logic_timer_stop(&timer);
    EXPECT_FALSE(sdl3d_logic_timer_active(&timer));
    EXPECT_FALSE(sdl3d_logic_timer_update(world, &timer, 0.5f).emitted);
    EXPECT_EQ(capture.count, 1);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, RepeatingTimerEmitsAtMostOncePerUpdateAndStaysActive)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 371, capture_logic_signal, &capture), 0);

    sdl3d_logic_timer timer{};
    sdl3d_logic_timer_init(&timer, 31, 0.25f, 371, true, 0.25f);
    ASSERT_TRUE(sdl3d_logic_timer_start(&timer));

    EXPECT_TRUE(sdl3d_logic_timer_update(world, &timer, 1.0f).emitted);
    EXPECT_TRUE(sdl3d_logic_timer_active(&timer));
    EXPECT_EQ(capture.count, 1);
    EXPECT_FLOAT_EQ(timer.remaining, 0.25f);

    EXPECT_TRUE(sdl3d_logic_timer_update(world, &timer, 0.25f).emitted);
    EXPECT_EQ(capture.count, 2);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, SectorPlatformDrivesSectorGeometryThroughAdapter)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_level level{};
    level.sector_count = 1;
    sdl3d_sector sector = make_square_sector(0.0f, 0.0f, 2.0f, 2.0f);
    sdl3d_logic_target_context context{};
    context.level = &level;
    context.sectors = &sector;
    context.sector_count = 1;
    sdl3d_logic_world_set_target_context(world, &context);

    logic_adapter_capture capture{};
    sdl3d_logic_game_adapters adapters{};
    adapters.userdata = &capture;
    adapters.set_sector_geometry = capture_set_sector_geometry;
    sdl3d_logic_world_set_game_adapters(world, &adapters);

    sdl3d_logic_sector_platform platform{};
    sdl3d_logic_sector_platform_init(&platform, 40, sdl3d_logic_target_sector_index(0), 0.0f, 2.0f, 6.0f, 4.0f, 0.01f);

    sdl3d_logic_sector_platform_result result = sdl3d_logic_sector_platform_update(world, &platform, 1.0f);
    EXPECT_TRUE(result.attempted);
    EXPECT_TRUE(result.applied);
    EXPECT_EQ(capture.set_sector_geometry_count, 1);
    EXPECT_EQ(capture.last_sector_index, 0);
    EXPECT_FLOAT_EQ(capture.last_geometry.floor_y, 1.0f);
    EXPECT_FLOAT_EQ(capture.last_geometry.ceil_y, 6.0f);

    result = sdl3d_logic_sector_platform_update(world, &platform, 0.001f);
    EXPECT_FALSE(result.attempted);
    EXPECT_EQ(capture.set_sector_geometry_count, 1);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, SectorPlatformReportsFailedGeometryAdapter)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    sdl3d_logic_sector_platform platform{};
    sdl3d_logic_sector_platform_init(&platform, 41, sdl3d_logic_target_sector_index(0), 0.0f, 2.0f, 6.0f, 4.0f, 0.01f);

    sdl3d_logic_sector_platform_result result = sdl3d_logic_sector_platform_update(world, &platform, 1.0f);
    EXPECT_TRUE(result.attempted);
    EXPECT_FALSE(result.applied);
    EXPECT_FLOAT_EQ(platform.last_floor_y, 0.0f);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, EntityBindingActivatesRelayFromSignal)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    ordered_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 401, capture_ordered_signal, &capture), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 402, capture_ordered_signal, &capture), 0);

    sdl3d_logic_relay relay{};
    sdl3d_logic_relay_init(&relay, 32);
    ASSERT_TRUE(sdl3d_logic_relay_add_output(&relay, 401));
    ASSERT_TRUE(sdl3d_logic_relay_add_output(&relay, 402));
    int binding_id = sdl3d_logic_world_bind_relay(world, 400, &relay);
    ASSERT_GT(binding_id, 0);
    EXPECT_EQ(sdl3d_logic_world_entity_binding_count(world), 1);
    EXPECT_TRUE(sdl3d_logic_world_entity_binding_enabled(world, binding_id));

    sdl3d_properties *payload = sdl3d_properties_create();
    sdl3d_properties_set_int(payload, "payload_value", 123);
    sdl3d_signal_emit(bus, 400, payload);

    ASSERT_EQ(capture.count, 2);
    EXPECT_EQ(capture.signals[0], 401);
    EXPECT_EQ(capture.signals[1], 402);
    EXPECT_EQ(capture.payload_value[0], 123);
    EXPECT_EQ(capture.payload_value[1], 123);

    sdl3d_properties_destroy(payload);
    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, EntityBindingCanBeDisabledAndUnbound)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);
    logic_signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 411, capture_logic_signal, &capture), 0);

    sdl3d_logic_toggle toggle{};
    sdl3d_logic_toggle_init(&toggle, 33, false, 411, 412);
    int binding_id = sdl3d_logic_world_bind_toggle(world, 410, &toggle);
    ASSERT_GT(binding_id, 0);

    EXPECT_TRUE(sdl3d_logic_world_set_entity_binding_enabled(world, binding_id, false));
    EXPECT_FALSE(sdl3d_logic_world_entity_binding_enabled(world, binding_id));
    sdl3d_signal_emit(bus, 410, nullptr);
    EXPECT_FALSE(toggle.state);
    EXPECT_EQ(capture.count, 0);

    EXPECT_TRUE(sdl3d_logic_world_set_entity_binding_enabled(world, binding_id, true));
    sdl3d_signal_emit(bus, 410, nullptr);
    EXPECT_TRUE(toggle.state);
    EXPECT_EQ(capture.count, 1);

    sdl3d_logic_world_unbind_entity(world, binding_id);
    EXPECT_EQ(sdl3d_logic_world_entity_binding_count(world), 0);
    sdl3d_signal_emit(bus, 410, nullptr);
    EXPECT_TRUE(toggle.state);
    EXPECT_EQ(capture.count, 1);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(LogicWorldEntities, InvalidEntityOperationsAreSafe)
{
    sdl3d_signal_bus *bus = nullptr;
    sdl3d_logic_world *world = make_logic_world(&bus);

    EXPECT_FALSE(sdl3d_logic_world_emit_signal(nullptr, 1, nullptr));
    EXPECT_FALSE(sdl3d_logic_relay_add_output(nullptr, 1));
    EXPECT_FALSE(sdl3d_logic_relay_activate(world, nullptr, nullptr).emitted);
    EXPECT_FALSE(sdl3d_logic_toggle_activate(world, nullptr).emitted);
    EXPECT_FALSE(sdl3d_logic_counter_activate(world, nullptr).emitted);
    EXPECT_FALSE(sdl3d_logic_branch_activate(world, nullptr).emitted);
    EXPECT_FALSE(sdl3d_logic_random_add_output(nullptr, 1));
    EXPECT_FALSE(sdl3d_logic_random_activate(world, nullptr).emitted);
    EXPECT_FALSE(sdl3d_logic_sequence_add_output(nullptr, 1));
    EXPECT_FALSE(sdl3d_logic_sequence_activate(world, nullptr).emitted);
    EXPECT_FALSE(sdl3d_logic_once_activate(world, nullptr).emitted);
    EXPECT_FALSE(sdl3d_logic_timer_start(nullptr));
    EXPECT_FALSE(sdl3d_logic_timer_update(world, nullptr, 1.0f).emitted);
    sdl3d_logic_timer_stop(nullptr);
    EXPECT_FALSE(sdl3d_logic_timer_active(nullptr));
    EXPECT_EQ(sdl3d_logic_world_bind_relay(nullptr, 1, nullptr), 0);
    EXPECT_EQ(sdl3d_logic_world_bind_relay(world, 1, nullptr), 0);
    EXPECT_EQ(sdl3d_logic_world_bind_toggle(world, 1, nullptr), 0);
    EXPECT_EQ(sdl3d_logic_world_bind_counter(world, 1, nullptr), 0);
    EXPECT_EQ(sdl3d_logic_world_bind_branch(world, 1, nullptr), 0);
    EXPECT_EQ(sdl3d_logic_world_bind_random(world, 1, nullptr), 0);
    EXPECT_EQ(sdl3d_logic_world_bind_sequence(world, 1, nullptr), 0);
    EXPECT_EQ(sdl3d_logic_world_bind_once(world, 1, nullptr), 0);
    sdl3d_logic_world_unbind_entity(nullptr, 1);
    sdl3d_logic_world_unbind_entity(world, 999);
    EXPECT_FALSE(sdl3d_logic_world_set_entity_binding_enabled(nullptr, 1, true));
    EXPECT_FALSE(sdl3d_logic_world_set_entity_binding_enabled(world, 999, true));
    EXPECT_FALSE(sdl3d_logic_world_entity_binding_enabled(nullptr, 1));
    EXPECT_FALSE(sdl3d_logic_world_entity_binding_enabled(world, 999));
    EXPECT_EQ(sdl3d_logic_world_entity_binding_count(nullptr), 0);

    sdl3d_logic_relay relay{};
    sdl3d_logic_relay_init(&relay, 31);
    for (int i = 0; i < SDL3D_LOGIC_MAX_ENTITY_OUTPUTS; ++i)
        ASSERT_TRUE(sdl3d_logic_relay_add_output(&relay, i + 1));
    EXPECT_FALSE(sdl3d_logic_relay_add_output(&relay, 99));

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

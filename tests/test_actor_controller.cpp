#include <gtest/gtest.h>

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/actor_controller.h"
#include "slayer3d/math.h"
#include "slayer3d/properties.h"

struct patrol_signal_capture
{
    int count = 0;
    int last_signal = 0;
    int controller_id = 0;
    int actor_id = 0;
    int waypoint_index = -1;
    int target_waypoint = -1;
    char actor_name[64] = {};
    char state[32] = {};
};

static void capture_patrol_signal(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    patrol_signal_capture *capture = (patrol_signal_capture *)userdata;
    capture->count++;
    capture->last_signal = signal_id;
    capture->controller_id = slayer3d_properties_get_int(payload, "controller_id", 0);
    capture->actor_id = slayer3d_properties_get_int(payload, "actor_id", 0);
    capture->waypoint_index = slayer3d_properties_get_int(payload, "waypoint_index", -1);
    capture->target_waypoint = slayer3d_properties_get_int(payload, "target_waypoint", -1);
    SDL_snprintf(capture->actor_name, sizeof(capture->actor_name), "%s",
                 slayer3d_properties_get_string(payload, "actor_name", ""));
    SDL_snprintf(capture->state, sizeof(capture->state), "%s", slayer3d_properties_get_string(payload, "state", ""));
}

static slayer3d_registered_actor *add_actor(slayer3d_actor_registry *registry)
{
    slayer3d_registered_actor *actor = slayer3d_actor_registry_add(registry, "robot");
    if (actor != nullptr)
        actor->position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    return actor;
}

TEST(ActorPatrolController, DefaultConfigIsConservative)
{
    slayer3d_actor_patrol_config config = slayer3d_actor_patrol_default_config();

    EXPECT_GT(config.speed, 0.0f);
    EXPECT_GE(config.wait_time, 0.0f);
    EXPECT_GT(config.arrival_radius, 0.0f);
    EXPECT_EQ(config.mode, SLAYER3D_ACTOR_PATROL_LOOP);
    EXPECT_TRUE(config.start_idle);
}

TEST(ActorPatrolController, MovesRegisteredActorTowardWaypoint)
{
    slayer3d_actor_registry *registry = slayer3d_actor_registry_create();
    ASSERT_NE(registry, nullptr);
    slayer3d_registered_actor *actor = add_actor(registry);
    ASSERT_NE(actor, nullptr);

    slayer3d_actor_patrol_config config = slayer3d_actor_patrol_default_config();
    config.speed = 2.0f;
    config.wait_time = 0.0f;
    config.start_idle = false;

    slayer3d_actor_patrol_controller controller{};
    slayer3d_actor_patrol_controller_init(&controller, 7, actor->id, &config);
    ASSERT_TRUE(slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
    ASSERT_TRUE(slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(4.0f, 0.0f, 0.0f)));

    slayer3d_actor_patrol_result result =
        slayer3d_actor_patrol_controller_update(&controller, registry, nullptr, 0.5f, nullptr, nullptr);

    EXPECT_TRUE(result.updated);
    EXPECT_TRUE(result.moved);
    EXPECT_FLOAT_EQ(actor->position.x, 1.0f);
    EXPECT_FLOAT_EQ(actor->position.z, 0.0f);
    EXPECT_STREQ(slayer3d_properties_get_string(actor->props, "state", ""), "walk");
    EXPECT_EQ(slayer3d_properties_get_int(actor->props, "target_waypoint", -1), 1);
    EXPECT_TRUE(slayer3d_properties_get_bool(actor->props, "enabled", false));
    EXPECT_TRUE(slayer3d_properties_get_bool(actor->props, "patrol_enabled", false));
    EXPECT_FALSE(slayer3d_properties_get_bool(actor->props, "alerted", true));

    slayer3d_actor_registry_destroy(registry);
}

TEST(ActorPatrolController, ActorPropertiesCanDrivePatrolRuntimeConfig)
{
    slayer3d_actor_registry *registry = slayer3d_actor_registry_create();
    ASSERT_NE(registry, nullptr);
    slayer3d_registered_actor *actor = add_actor(registry);
    ASSERT_NE(actor, nullptr);

    slayer3d_actor_patrol_config config = slayer3d_actor_patrol_default_config();
    config.start_idle = false;
    config.speed = 1.0f;

    slayer3d_actor_patrol_controller controller{};
    slayer3d_actor_patrol_controller_init(&controller, 14, actor->id, &config);
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(10.0f, 0.0f, 0.0f));

    slayer3d_properties_set_bool(actor->props, "patrol_enabled", false);
    slayer3d_actor_patrol_result disabled =
        slayer3d_actor_patrol_controller_update(&controller, registry, nullptr, 1.0f, nullptr, nullptr);
    EXPECT_TRUE(disabled.updated);
    EXPECT_FALSE(disabled.moved);
    EXPECT_FALSE(controller.enabled);
    EXPECT_FLOAT_EQ(actor->position.x, 0.0f);

    slayer3d_properties_set_bool(actor->props, "patrol_enabled", true);
    slayer3d_properties_set_float(actor->props, "patrol_speed", 4.0f);
    slayer3d_properties_set_float(actor->props, "patrol_wait_time", 0.75f);
    slayer3d_actor_patrol_result moved =
        slayer3d_actor_patrol_controller_update(&controller, registry, nullptr, 0.5f, nullptr, nullptr);
    EXPECT_TRUE(moved.moved);
    EXPECT_TRUE(controller.enabled);
    EXPECT_FLOAT_EQ(controller.speed, 4.0f);
    EXPECT_FLOAT_EQ(controller.wait_time, 0.75f);
    EXPECT_FLOAT_EQ(actor->position.x, 2.0f);

    slayer3d_actor_registry_destroy(registry);
}

TEST(ActorPatrolController, IdleTransitionsToWalkAndEmitsSignal)
{
    slayer3d_actor_registry *registry = slayer3d_actor_registry_create();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    ASSERT_NE(registry, nullptr);
    ASSERT_NE(bus, nullptr);
    slayer3d_registered_actor *actor = add_actor(registry);
    ASSERT_NE(actor, nullptr);

    patrol_signal_capture capture{};
    ASSERT_GT(slayer3d_signal_connect(bus, 103, capture_patrol_signal, &capture), 0);

    slayer3d_actor_patrol_config config = slayer3d_actor_patrol_default_config();
    config.wait_time = 0.25f;
    config.signals.walk_started = 103;

    slayer3d_actor_patrol_controller controller{};
    slayer3d_actor_patrol_controller_init(&controller, 8, actor->id, &config);
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(1.0f, 0.0f, 0.0f));

    EXPECT_EQ(controller.state, SLAYER3D_ACTOR_PATROL_IDLE);
    EXPECT_FALSE(
        slayer3d_actor_patrol_controller_update(&controller, registry, bus, 0.1f, nullptr, nullptr).state_changed);
    slayer3d_actor_patrol_result result =
        slayer3d_actor_patrol_controller_update(&controller, registry, bus, 0.15f, nullptr, nullptr);

    EXPECT_TRUE(result.state_changed);
    EXPECT_EQ(controller.state, SLAYER3D_ACTOR_PATROL_WALK);
    EXPECT_EQ(capture.count, 1);
    EXPECT_EQ(capture.last_signal, 103);
    EXPECT_EQ(capture.controller_id, 8);
    EXPECT_EQ(capture.actor_id, actor->id);
    EXPECT_STREQ(capture.actor_name, "robot");
    EXPECT_STREQ(capture.state, "walk");

    slayer3d_signal_bus_destroy(bus);
    slayer3d_actor_registry_destroy(registry);
}

TEST(ActorPatrolController, ReachingWaypointEmitsAndIdles)
{
    slayer3d_actor_registry *registry = slayer3d_actor_registry_create();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    ASSERT_NE(registry, nullptr);
    ASSERT_NE(bus, nullptr);
    slayer3d_registered_actor *actor = add_actor(registry);
    ASSERT_NE(actor, nullptr);

    patrol_signal_capture reached{};
    patrol_signal_capture idle{};
    ASSERT_GT(slayer3d_signal_connect(bus, 111, capture_patrol_signal, &reached), 0);
    ASSERT_GT(slayer3d_signal_connect(bus, 112, capture_patrol_signal, &idle), 0);

    slayer3d_actor_patrol_config config = slayer3d_actor_patrol_default_config();
    config.speed = 10.0f;
    config.wait_time = 0.5f;
    config.start_idle = false;
    config.signals.waypoint_reached = 111;
    config.signals.idle_started = 112;

    slayer3d_actor_patrol_controller controller{};
    slayer3d_actor_patrol_controller_init(&controller, 9, actor->id, &config);
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(1.0f, 0.0f, 0.0f));

    slayer3d_actor_patrol_result result =
        slayer3d_actor_patrol_controller_update(&controller, registry, bus, 0.2f, nullptr, nullptr);

    EXPECT_TRUE(result.waypoint_reached);
    EXPECT_EQ(result.reached_waypoint, 1);
    EXPECT_EQ(controller.state, SLAYER3D_ACTOR_PATROL_IDLE);
    EXPECT_EQ(controller.target_waypoint, 0);
    EXPECT_EQ(reached.count, 1);
    EXPECT_EQ(reached.waypoint_index, 1);
    EXPECT_EQ(idle.count, 1);
    EXPECT_STREQ(slayer3d_properties_get_string(actor->props, "state", ""), "idle");
    EXPECT_FLOAT_EQ(slayer3d_properties_get_float(actor->props, "patrol_wait_remaining", 0.0f), 0.5f);

    slayer3d_signal_bus_destroy(bus);
    slayer3d_actor_registry_destroy(registry);
}

TEST(ActorPatrolController, LoopCompletionEmitsAfterReturningToStart)
{
    slayer3d_actor_registry *registry = slayer3d_actor_registry_create();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_registered_actor *actor = add_actor(registry);
    ASSERT_NE(actor, nullptr);

    patrol_signal_capture loop{};
    ASSERT_GT(slayer3d_signal_connect(bus, 121, capture_patrol_signal, &loop), 0);

    slayer3d_actor_patrol_config config = slayer3d_actor_patrol_default_config();
    config.speed = 10.0f;
    config.wait_time = 0.0f;
    config.start_idle = false;
    config.signals.loop_completed = 121;

    slayer3d_actor_patrol_controller controller{};
    slayer3d_actor_patrol_controller_init(&controller, 10, actor->id, &config);
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(1.0f, 0.0f, 0.0f));

    EXPECT_TRUE(
        slayer3d_actor_patrol_controller_update(&controller, registry, bus, 0.2f, nullptr, nullptr).waypoint_reached);
    EXPECT_EQ(loop.count, 0);
    EXPECT_TRUE(
        slayer3d_actor_patrol_controller_update(&controller, registry, bus, 0.0f, nullptr, nullptr).state_changed);
    slayer3d_actor_patrol_result returned =
        slayer3d_actor_patrol_controller_update(&controller, registry, bus, 0.2f, nullptr, nullptr);

    EXPECT_TRUE(returned.loop_completed);
    EXPECT_EQ(loop.count, 1);
    EXPECT_EQ(loop.waypoint_index, 0);

    slayer3d_signal_bus_destroy(bus);
    slayer3d_actor_registry_destroy(registry);
}

TEST(ActorPatrolController, PingPongReversesAtEndpoints)
{
    slayer3d_actor_registry *registry = slayer3d_actor_registry_create();
    slayer3d_registered_actor *actor = add_actor(registry);
    ASSERT_NE(actor, nullptr);

    slayer3d_actor_patrol_config config = slayer3d_actor_patrol_default_config();
    config.mode = SLAYER3D_ACTOR_PATROL_PING_PONG;
    config.speed = 10.0f;
    config.start_idle = false;

    slayer3d_actor_patrol_controller controller{};
    slayer3d_actor_patrol_controller_init(&controller, 11, actor->id, &config);
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(1.0f, 0.0f, 0.0f));
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(2.0f, 0.0f, 0.0f));

    slayer3d_actor_patrol_controller_update(&controller, registry, nullptr, 0.2f, nullptr, nullptr);
    EXPECT_EQ(controller.target_waypoint, 2);
    slayer3d_actor_patrol_controller_update(&controller, registry, nullptr, 0.0f, nullptr, nullptr);
    slayer3d_actor_patrol_controller_update(&controller, registry, nullptr, 0.2f, nullptr, nullptr);
    EXPECT_EQ(controller.direction, -1);
    EXPECT_EQ(controller.target_waypoint, 1);

    slayer3d_actor_registry_destroy(registry);
}

struct move_adapter_context
{
    bool allow = true;
    float floor_y = 3.0f;
};

static bool test_move_adapter(void *userdata, const slayer3d_actor_patrol_controller *controller,
                              slayer3d_registered_actor *actor, slayer3d_vec3 desired_position,
                              slayer3d_vec3 *out_position)
{
    (void)controller;
    (void)actor;
    move_adapter_context *context = (move_adapter_context *)userdata;
    if (!context->allow)
        return false;
    *out_position = desired_position;
    out_position->y = context->floor_y;
    return true;
}

TEST(ActorPatrolController, MovementAdapterCanAdjustOrRejectMovement)
{
    slayer3d_actor_registry *registry = slayer3d_actor_registry_create();
    slayer3d_registered_actor *actor = add_actor(registry);
    ASSERT_NE(actor, nullptr);

    slayer3d_actor_patrol_config config = slayer3d_actor_patrol_default_config();
    config.start_idle = false;
    slayer3d_actor_patrol_controller controller{};
    slayer3d_actor_patrol_controller_init(&controller, 12, actor->id, &config);
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(4.0f, 0.0f, 0.0f));

    move_adapter_context context{};
    slayer3d_actor_patrol_controller_update(&controller, registry, nullptr, 1.0f, test_move_adapter, &context);
    EXPECT_FLOAT_EQ(actor->position.x, 1.0f);
    EXPECT_FLOAT_EQ(actor->position.y, 3.0f);

    context.allow = false;
    slayer3d_actor_patrol_controller_update(&controller, registry, nullptr, 1.0f, test_move_adapter, &context);
    EXPECT_FLOAT_EQ(actor->position.x, 1.0f);

    slayer3d_actor_registry_destroy(registry);
}

TEST(ActorPatrolController, DisabledAndInvalidControllersAreSafe)
{
    EXPECT_STREQ(slayer3d_actor_patrol_state_name(SLAYER3D_ACTOR_PATROL_IDLE), "idle");
    EXPECT_STREQ(slayer3d_actor_patrol_state_name(SLAYER3D_ACTOR_PATROL_WALK), "walk");

    slayer3d_actor_patrol_controller_init(nullptr, 1, 1, nullptr);
    slayer3d_actor_patrol_controller_clear_waypoints(nullptr);
    EXPECT_FALSE(slayer3d_actor_patrol_controller_add_waypoint(nullptr, slayer3d_vec3_make(0, 0, 0)));
    slayer3d_actor_patrol_controller_reset(nullptr, true);
    slayer3d_actor_patrol_controller_set_enabled(nullptr, false);
    slayer3d_actor_patrol_controller_sync_properties(nullptr, nullptr);

    slayer3d_actor_registry *registry = slayer3d_actor_registry_create();
    slayer3d_registered_actor *actor = add_actor(registry);
    ASSERT_NE(actor, nullptr);

    slayer3d_actor_patrol_controller controller{};
    slayer3d_actor_patrol_controller_init(&controller, 13, actor->id, nullptr);
    slayer3d_actor_patrol_controller_set_enabled(&controller, false);
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_actor_patrol_controller_add_waypoint(&controller, slayer3d_vec3_make(2.0f, 0.0f, 0.0f));

    slayer3d_actor_patrol_result disabled =
        slayer3d_actor_patrol_controller_update(&controller, registry, nullptr, 1.0f, nullptr, nullptr);
    EXPECT_TRUE(disabled.updated);
    EXPECT_FALSE(disabled.moved);
    EXPECT_FLOAT_EQ(actor->position.x, 0.0f);
    EXPECT_FALSE(slayer3d_properties_get_bool(actor->props, "enabled", true));

    slayer3d_actor_patrol_result missing =
        slayer3d_actor_patrol_controller_update(&controller, nullptr, nullptr, 1.0f, nullptr, nullptr);
    EXPECT_FALSE(missing.updated);

    slayer3d_actor_registry_destroy(registry);
}

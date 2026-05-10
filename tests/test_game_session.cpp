#include <gtest/gtest.h>

extern "C"
{
#include "slayer3d/game.h"
#include "slayer3d/logic.h"
#include "slayer3d/properties.h"
}

namespace
{
constexpr int kTimerSignal = 991;

struct SessionSignalCapture
{
    slayer3d_game_session *session = nullptr;
    int count = 0;
    int tick_seen = -1;
};

void capture_session_signal(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    (void)payload;
    auto *capture = static_cast<SessionSignalCapture *>(userdata);
    if (capture != nullptr && signal_id == kTimerSignal)
    {
        capture->count++;
        capture->tick_seen = slayer3d_game_session_get_tick_count(capture->session);
    }
}
} // namespace

TEST(GameSession, DefaultDescriptorCreatesCoreServices)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    ASSERT_NE(session, nullptr);

    EXPECT_NE(slayer3d_game_session_get_registry(session), nullptr);
    EXPECT_NE(slayer3d_game_session_get_signal_bus(session), nullptr);
    EXPECT_NE(slayer3d_game_session_get_timer_pool(session), nullptr);
    EXPECT_NE(slayer3d_game_session_get_logic_world(session), nullptr);
    EXPECT_NE(slayer3d_game_session_get_input(session), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_audio(session), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_world(session), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_profile_name(session), nullptr);

    slayer3d_registered_actor *actor =
        slayer3d_actor_registry_add(slayer3d_game_session_get_registry(session), "entity.ball");
    ASSERT_NE(actor, nullptr);

    slayer3d_logic_resolved_target resolved{};
    slayer3d_logic_target_ref target = slayer3d_logic_target_actor_name("entity.ball");
    EXPECT_TRUE(
        slayer3d_logic_world_resolve_target(slayer3d_game_session_get_logic_world(session), &target, &resolved));
    EXPECT_EQ(resolved.actor, actor);

    slayer3d_game_session_destroy(session);
}

TEST(GameSession, DescriptorInitCreatesCoreAndCopiesProfile)
{
    int world_marker = 42;
    slayer3d_game_session_desc desc{};
    slayer3d_game_session_desc_init(&desc);
    desc.world = &world_marker;
    desc.profile_name = "fixed-screen-arcade";

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(&desc, &session));

    EXPECT_EQ(slayer3d_game_session_get_world(session), &world_marker);
    ASSERT_NE(slayer3d_game_session_get_profile_name(session), nullptr);
    EXPECT_STREQ(slayer3d_game_session_get_profile_name(session), "fixed-screen-arcade");

    slayer3d_game_session_destroy(session);
}

TEST(GameSession, BorrowedServicesAreNotDestroyedWithSession)
{
    slayer3d_actor_registry *registry = slayer3d_actor_registry_create();
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    slayer3d_timer_pool *timers = slayer3d_timer_pool_create();
    slayer3d_input_manager *input = slayer3d_input_create();
    ASSERT_NE(registry, nullptr);
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(timers, nullptr);
    ASSERT_NE(input, nullptr);

    slayer3d_game_session_desc desc{};
    desc.registry = registry;
    desc.bus = bus;
    desc.timers = timers;
    desc.input = input;

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(&desc, &session));
    EXPECT_EQ(slayer3d_game_session_get_registry(session), registry);
    EXPECT_EQ(slayer3d_game_session_get_signal_bus(session), bus);
    EXPECT_EQ(slayer3d_game_session_get_timer_pool(session), timers);
    EXPECT_EQ(slayer3d_game_session_get_input(session), input);
    EXPECT_EQ(slayer3d_game_session_get_logic_world(session), nullptr);

    slayer3d_game_session_destroy(session);

    EXPECT_NE(slayer3d_actor_registry_add(registry, "entity.after_session_destroy"), nullptr);
    slayer3d_input_destroy(input);
    slayer3d_timer_pool_destroy(timers);
    slayer3d_signal_bus_destroy(bus);
    slayer3d_actor_registry_destroy(registry);
}

TEST(GameSession, RejectsAmbiguousOwnership)
{
    slayer3d_actor_registry *registry = slayer3d_actor_registry_create();
    ASSERT_NE(registry, nullptr);

    slayer3d_game_session_desc desc{};
    desc.create_services = SLAYER3D_GAME_SESSION_SERVICE_REGISTRY;
    desc.registry = registry;

    slayer3d_game_session *session = nullptr;
    EXPECT_FALSE(slayer3d_game_session_create(&desc, &session));
    EXPECT_EQ(session, nullptr);

    slayer3d_actor_registry_destroy(registry);
}

TEST(GameSession, LogicCreationRequiresSignalBus)
{
    slayer3d_game_session_desc desc{};
    desc.create_services = SLAYER3D_GAME_SESSION_SERVICE_LOGIC_WORLD;

    slayer3d_game_session *session = nullptr;
    EXPECT_FALSE(slayer3d_game_session_create(&desc, &session));
    EXPECT_EQ(session, nullptr);
}

TEST(GameSession, UpdateOrderRunsTimersBeforeTickCounterAdvances)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    SessionSignalCapture capture{};
    capture.session = session;
    ASSERT_GT(slayer3d_signal_connect(slayer3d_game_session_get_signal_bus(session), kTimerSignal,
                                      capture_session_signal, &capture),
              0);
    ASSERT_GT(slayer3d_timer_start(slayer3d_game_session_get_timer_pool(session), 0.25f, kTimerSignal, false, 0.0f), 0);

    EXPECT_TRUE(slayer3d_game_session_begin_tick(session, 0.25f));
    EXPECT_EQ(capture.count, 1);
    EXPECT_EQ(capture.tick_seen, 0);
    EXPECT_TRUE(slayer3d_game_session_end_tick(session, 0.25f));
    EXPECT_EQ(slayer3d_game_session_get_tick_count(session), 1);
    EXPECT_FLOAT_EQ(slayer3d_game_session_get_time(session), 0.25f);

    slayer3d_game_session_destroy(session);
}

TEST(GameSession, EmptySessionUpdatesTimeWithoutOptionalServices)
{
    slayer3d_game_session_desc desc{};
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(&desc, &session));

    EXPECT_EQ(slayer3d_game_session_get_registry(session), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_signal_bus(session), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_timer_pool(session), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_logic_world(session), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_input(session), nullptr);

    EXPECT_TRUE(slayer3d_game_session_begin_frame(session, 0.5f));
    EXPECT_TRUE(slayer3d_game_session_tick(session, 0.5f));
    EXPECT_TRUE(slayer3d_game_session_tick(session, -1.0f));
    EXPECT_EQ(slayer3d_game_session_get_tick_count(session), 2);
    EXPECT_FLOAT_EQ(slayer3d_game_session_get_time(session), 0.5f);

    slayer3d_game_session_destroy(session);
}

TEST(GameSession, NullSafety)
{
    EXPECT_FALSE(slayer3d_game_session_create(nullptr, nullptr));
    EXPECT_FALSE(slayer3d_game_session_begin_frame(nullptr, 1.0f));
    EXPECT_FALSE(slayer3d_game_session_update_input(nullptr));
    EXPECT_FALSE(slayer3d_game_session_begin_tick(nullptr, 1.0f));
    EXPECT_FALSE(slayer3d_game_session_end_tick(nullptr, 1.0f));
    EXPECT_FALSE(slayer3d_game_session_tick(nullptr, 1.0f));
    EXPECT_EQ(slayer3d_game_session_get_registry(nullptr), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_signal_bus(nullptr), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_timer_pool(nullptr), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_logic_world(nullptr), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_input(nullptr), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_audio(nullptr), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_world(nullptr), nullptr);
    EXPECT_EQ(slayer3d_game_session_get_profile_name(nullptr), nullptr);
    EXPECT_FLOAT_EQ(slayer3d_game_session_get_time(nullptr), 0.0f);
    EXPECT_EQ(slayer3d_game_session_get_tick_count(nullptr), 0);
    slayer3d_game_session_destroy(nullptr);
}

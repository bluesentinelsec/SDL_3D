#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/game.h"
#include "sdl3d/logic.h"
#include "sdl3d/properties.h"
}

namespace
{
constexpr int kTimerSignal = 991;

struct SessionSignalCapture
{
    sdl3d_game_session *session = nullptr;
    int count = 0;
    int tick_seen = -1;
};

void capture_session_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    (void)payload;
    auto *capture = static_cast<SessionSignalCapture *>(userdata);
    if (capture != nullptr && signal_id == kTimerSignal)
    {
        capture->count++;
        capture->tick_seen = sdl3d_game_session_get_tick_count(capture->session);
    }
}
} // namespace

TEST(GameSession, DefaultDescriptorCreatesCoreServices)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    ASSERT_NE(session, nullptr);

    EXPECT_NE(sdl3d_game_session_get_registry(session), nullptr);
    EXPECT_NE(sdl3d_game_session_get_signal_bus(session), nullptr);
    EXPECT_NE(sdl3d_game_session_get_timer_pool(session), nullptr);
    EXPECT_NE(sdl3d_game_session_get_logic_world(session), nullptr);
    EXPECT_NE(sdl3d_game_session_get_input(session), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_audio(session), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_world(session), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_profile_name(session), nullptr);

    sdl3d_registered_actor *actor = sdl3d_actor_registry_add(sdl3d_game_session_get_registry(session), "entity.ball");
    ASSERT_NE(actor, nullptr);

    sdl3d_logic_resolved_target resolved{};
    sdl3d_logic_target_ref target = sdl3d_logic_target_actor_name("entity.ball");
    EXPECT_TRUE(sdl3d_logic_world_resolve_target(sdl3d_game_session_get_logic_world(session), &target, &resolved));
    EXPECT_EQ(resolved.actor, actor);

    sdl3d_game_session_destroy(session);
}

TEST(GameSession, DescriptorInitCreatesCoreAndCopiesProfile)
{
    int world_marker = 42;
    sdl3d_game_session_desc desc{};
    sdl3d_game_session_desc_init(&desc);
    desc.world = &world_marker;
    desc.profile_name = "fixed-screen-arcade";

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(&desc, &session));

    EXPECT_EQ(sdl3d_game_session_get_world(session), &world_marker);
    ASSERT_NE(sdl3d_game_session_get_profile_name(session), nullptr);
    EXPECT_STREQ(sdl3d_game_session_get_profile_name(session), "fixed-screen-arcade");

    sdl3d_game_session_destroy(session);
}

TEST(GameSession, BorrowedServicesAreNotDestroyedWithSession)
{
    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_timer_pool *timers = sdl3d_timer_pool_create();
    sdl3d_input_manager *input = sdl3d_input_create();
    ASSERT_NE(registry, nullptr);
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(timers, nullptr);
    ASSERT_NE(input, nullptr);

    sdl3d_game_session_desc desc{};
    desc.registry = registry;
    desc.bus = bus;
    desc.timers = timers;
    desc.input = input;

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(&desc, &session));
    EXPECT_EQ(sdl3d_game_session_get_registry(session), registry);
    EXPECT_EQ(sdl3d_game_session_get_signal_bus(session), bus);
    EXPECT_EQ(sdl3d_game_session_get_timer_pool(session), timers);
    EXPECT_EQ(sdl3d_game_session_get_input(session), input);
    EXPECT_EQ(sdl3d_game_session_get_logic_world(session), nullptr);

    sdl3d_game_session_destroy(session);

    EXPECT_NE(sdl3d_actor_registry_add(registry, "entity.after_session_destroy"), nullptr);
    sdl3d_input_destroy(input);
    sdl3d_timer_pool_destroy(timers);
    sdl3d_signal_bus_destroy(bus);
    sdl3d_actor_registry_destroy(registry);
}

TEST(GameSession, RejectsAmbiguousOwnership)
{
    sdl3d_actor_registry *registry = sdl3d_actor_registry_create();
    ASSERT_NE(registry, nullptr);

    sdl3d_game_session_desc desc{};
    desc.create_services = SDL3D_GAME_SESSION_SERVICE_REGISTRY;
    desc.registry = registry;

    sdl3d_game_session *session = nullptr;
    EXPECT_FALSE(sdl3d_game_session_create(&desc, &session));
    EXPECT_EQ(session, nullptr);

    sdl3d_actor_registry_destroy(registry);
}

TEST(GameSession, LogicCreationRequiresSignalBus)
{
    sdl3d_game_session_desc desc{};
    desc.create_services = SDL3D_GAME_SESSION_SERVICE_LOGIC_WORLD;

    sdl3d_game_session *session = nullptr;
    EXPECT_FALSE(sdl3d_game_session_create(&desc, &session));
    EXPECT_EQ(session, nullptr);
}

TEST(GameSession, UpdateOrderRunsTimersBeforeTickCounterAdvances)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    SessionSignalCapture capture{};
    capture.session = session;
    ASSERT_GT(sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(session), kTimerSignal, capture_session_signal,
                                   &capture),
              0);
    ASSERT_GT(sdl3d_timer_start(sdl3d_game_session_get_timer_pool(session), 0.25f, kTimerSignal, false, 0.0f), 0);

    EXPECT_TRUE(sdl3d_game_session_begin_tick(session, 0.25f));
    EXPECT_EQ(capture.count, 1);
    EXPECT_EQ(capture.tick_seen, 0);
    EXPECT_TRUE(sdl3d_game_session_end_tick(session, 0.25f));
    EXPECT_EQ(sdl3d_game_session_get_tick_count(session), 1);
    EXPECT_FLOAT_EQ(sdl3d_game_session_get_time(session), 0.25f);

    sdl3d_game_session_destroy(session);
}

TEST(GameSession, EmptySessionUpdatesTimeWithoutOptionalServices)
{
    sdl3d_game_session_desc desc{};
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(&desc, &session));

    EXPECT_EQ(sdl3d_game_session_get_registry(session), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_signal_bus(session), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_timer_pool(session), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_logic_world(session), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_input(session), nullptr);

    EXPECT_TRUE(sdl3d_game_session_begin_frame(session, 0.5f));
    EXPECT_TRUE(sdl3d_game_session_tick(session, 0.5f));
    EXPECT_TRUE(sdl3d_game_session_tick(session, -1.0f));
    EXPECT_EQ(sdl3d_game_session_get_tick_count(session), 2);
    EXPECT_FLOAT_EQ(sdl3d_game_session_get_time(session), 0.5f);

    sdl3d_game_session_destroy(session);
}

TEST(GameSession, NullSafety)
{
    EXPECT_FALSE(sdl3d_game_session_create(nullptr, nullptr));
    EXPECT_FALSE(sdl3d_game_session_begin_frame(nullptr, 1.0f));
    EXPECT_FALSE(sdl3d_game_session_update_input(nullptr));
    EXPECT_FALSE(sdl3d_game_session_begin_tick(nullptr, 1.0f));
    EXPECT_FALSE(sdl3d_game_session_end_tick(nullptr, 1.0f));
    EXPECT_FALSE(sdl3d_game_session_tick(nullptr, 1.0f));
    EXPECT_EQ(sdl3d_game_session_get_registry(nullptr), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_signal_bus(nullptr), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_timer_pool(nullptr), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_logic_world(nullptr), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_input(nullptr), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_audio(nullptr), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_world(nullptr), nullptr);
    EXPECT_EQ(sdl3d_game_session_get_profile_name(nullptr), nullptr);
    EXPECT_FLOAT_EQ(sdl3d_game_session_get_time(nullptr), 0.0f);
    EXPECT_EQ(sdl3d_game_session_get_tick_count(nullptr), 0);
    sdl3d_game_session_destroy(nullptr);
}

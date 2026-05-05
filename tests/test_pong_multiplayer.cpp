#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

extern "C"
{
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/game.h"
#include "sdl3d/game_data.h"
#include "sdl3d/network.h"
#include "sdl3d/properties.h"
}

namespace
{
enum PongNetworkMessageKind : Uint8
{
    PONG_NETWORK_MESSAGE_START_GAME,
    PONG_NETWORK_MESSAGE_PAUSE_REQUEST,
    PONG_NETWORK_MESSAGE_RESUME_REQUEST,
    PONG_NETWORK_MESSAGE_DISCONNECT,
};

constexpr const char *PONG_NETWORK_BINDING_STATE_SNAPSHOT = "state_snapshot";
constexpr const char *PONG_NETWORK_BINDING_CLIENT_INPUT = "client_input";
constexpr const char *PONG_NETWORK_BINDING_START_GAME = "start_game";
constexpr const char *PONG_NETWORK_BINDING_PAUSE_REQUEST = "pause_request";
constexpr const char *PONG_NETWORK_BINDING_RESUME_REQUEST = "resume_request";
constexpr const char *PONG_NETWORK_BINDING_DISCONNECT = "disconnect";

static sdl3d_game_session *create_session(bool include_audio = false)
{
    sdl3d_game_session_desc desc{};
    sdl3d_game_session_desc_init(&desc);
    if (include_audio)
    {
        desc.create_services |= SDL3D_GAME_SESSION_SERVICE_AUDIO;
        desc.optional_audio = true;
    }
    sdl3d_game_session *session = nullptr;
    EXPECT_TRUE(sdl3d_game_session_create(&desc, &session));
    return session;
}

static bool load_pong_runtime(sdl3d_game_session *session, sdl3d_game_data_runtime **out_runtime)
{
    char error[256]{};
    EXPECT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, out_runtime, error, sizeof(error))) << error;
    return *out_runtime != nullptr;
}

static void set_multiplayer_scene_state(sdl3d_game_data_runtime *runtime, const char *match_mode,
                                        const char *network_role, const char *network_flow)
{
    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_string(scene_state, "match_mode", match_mode != nullptr ? match_mode : "");
    sdl3d_properties_set_string(scene_state, "network_role", network_role != nullptr ? network_role : "");
    sdl3d_properties_set_string(scene_state, "network_flow", network_flow != nullptr ? network_flow : "");
}

static bool enter_multiplayer_play_scene(sdl3d_game_data_runtime *runtime, const char *match_mode,
                                         const char *network_role, const char *network_flow)
{
    sdl3d_properties *payload = sdl3d_properties_create();
    if (payload == nullptr)
    {
        return false;
    }

    if (match_mode != nullptr)
    {
        sdl3d_properties_set_string(payload, "match_mode", match_mode);
    }
    if (network_role != nullptr)
    {
        sdl3d_properties_set_string(payload, "network_role", network_role);
    }
    if (network_flow != nullptr)
    {
        sdl3d_properties_set_string(payload, "network_flow", network_flow);
    }

    const bool ok = sdl3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload);
    sdl3d_properties_destroy(payload);
    return ok;
}

static bool wait_for_network_pair(sdl3d_network_session *host, sdl3d_network_session *client)
{
    for (int i = 0; i < 1200; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client, 0.01f));
        if (sdl3d_network_session_is_connected(host) && sdl3d_network_session_is_connected(client))
        {
            return true;
        }
        if (sdl3d_network_session_state(client) == SDL3D_NETWORK_STATE_REJECTED ||
            sdl3d_network_session_state(client) == SDL3D_NETWORK_STATE_TIMED_OUT ||
            sdl3d_network_session_state(client) == SDL3D_NETWORK_STATE_ERROR)
        {
            return false;
        }
    }
    return false;
}

static bool send_client_input_packet(sdl3d_game_data_runtime *runtime, sdl3d_game_session *session,
                                     sdl3d_network_session *net_session)
{
    const sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    const sdl3d_input_snapshot *snapshot = sdl3d_input_get_snapshot(input);
    Uint8 packet[128];
    size_t packet_size = 0U;
    char error[160]{};
    const char *client_input_channel = nullptr;

    return sdl3d_game_data_get_network_runtime_replication(runtime, PONG_NETWORK_BINDING_CLIENT_INPUT,
                                                           &client_input_channel) &&
           sdl3d_game_data_encode_network_input(runtime, client_input_channel, input,
                                                (Uint32)SDL_max(snapshot != nullptr ? snapshot->tick : 0, 0), packet,
                                                sizeof(packet), &packet_size, error, sizeof(error)) &&
           sdl3d_network_session_send(net_session, packet, (int)packet_size);
}

static bool process_host_input_packet(sdl3d_game_data_runtime *runtime, sdl3d_game_session *session,
                                      const Uint8 *packet, int packet_size)
{
    Uint32 tick = 0U;
    char error[160]{};

    if (runtime == nullptr || session == nullptr || packet == nullptr || packet_size <= 0)
    {
        return false;
    }

    return sdl3d_game_data_apply_network_input(runtime, sdl3d_game_session_get_input(session), packet,
                                               (size_t)packet_size, &tick, error, sizeof(error));
}

static const char *control_binding_for_kind(PongNetworkMessageKind kind)
{
    switch (kind)
    {
    case PONG_NETWORK_MESSAGE_START_GAME:
        return PONG_NETWORK_BINDING_START_GAME;
    case PONG_NETWORK_MESSAGE_PAUSE_REQUEST:
        return PONG_NETWORK_BINDING_PAUSE_REQUEST;
    case PONG_NETWORK_MESSAGE_RESUME_REQUEST:
        return PONG_NETWORK_BINDING_RESUME_REQUEST;
    case PONG_NETWORK_MESSAGE_DISCONNECT:
        return PONG_NETWORK_BINDING_DISCONNECT;
    default:
        return nullptr;
    }
}

static const char *control_name_for_kind(sdl3d_game_data_runtime *runtime, PongNetworkMessageKind kind)
{
    const char *control_name = nullptr;
    const char *binding = control_binding_for_kind(kind);
    return binding != nullptr && sdl3d_game_data_get_network_runtime_control(runtime, binding, &control_name)
               ? control_name
               : nullptr;
}

static bool send_control_packet(sdl3d_game_data_runtime *runtime, sdl3d_network_session *net_session,
                                PongNetworkMessageKind kind)
{
    Uint8 packet[SDL3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE];
    size_t packet_size = 0U;
    char error[160]{};
    const char *control_name = control_name_for_kind(runtime, kind);

    return control_name != nullptr &&
           sdl3d_game_data_encode_network_control(runtime, control_name, 1234U, packet, sizeof(packet), &packet_size,
                                                  error, sizeof(error)) &&
           sdl3d_network_session_send(net_session, packet, (int)packet_size);
}

static bool read_control_packet(sdl3d_game_data_runtime *runtime, const Uint8 *packet, int packet_size,
                                PongNetworkMessageKind expected)
{
    sdl3d_game_data_network_control control{};
    char error[160]{};
    const char *expected_name = control_name_for_kind(runtime, expected);

    return runtime != nullptr && packet != nullptr && expected_name != nullptr &&
           sdl3d_game_data_decode_network_control(runtime, packet, (size_t)packet_size, &control, error,
                                                  sizeof(error)) &&
           SDL_strcmp(control.name, expected_name) == 0 && control.tick == 1234U;
}

static bool send_host_state_packet(sdl3d_game_data_runtime *runtime, sdl3d_game_session *session,
                                   sdl3d_network_session *net_session, bool paused)
{
    sdl3d_registered_actor *match = sdl3d_game_data_find_actor(runtime, "entity.match");
    const sdl3d_input_snapshot *snapshot = sdl3d_input_get_snapshot(sdl3d_game_session_get_input(session));
    Uint8 packet[SDL3D_NETWORK_MAX_PACKET_SIZE];
    size_t packet_size = 0U;
    char error[160]{};
    const char *state_channel = nullptr;

    if (runtime == nullptr || session == nullptr || net_session == nullptr || match == nullptr ||
        !sdl3d_game_data_get_network_runtime_replication(runtime, PONG_NETWORK_BINDING_STATE_SNAPSHOT, &state_channel))
    {
        return false;
    }

    sdl3d_properties_set_bool(match->props, "paused", paused);
    return sdl3d_game_data_encode_network_snapshot(runtime, state_channel,
                                                   (Uint32)SDL_max(snapshot != nullptr ? snapshot->tick : 0, 0), packet,
                                                   sizeof(packet), &packet_size, error, sizeof(error)) &&
           sdl3d_network_session_send(net_session, packet, (int)packet_size);
}

static bool process_client_state_packet(sdl3d_game_data_runtime *runtime, const Uint8 *packet, int packet_size,
                                        bool *out_paused)
{
    Uint32 tick = 0U;
    sdl3d_registered_actor *match = nullptr;
    char error[160]{};

    if (runtime == nullptr || packet == nullptr || packet_size <= 0)
    {
        return false;
    }

    if (!sdl3d_game_data_apply_network_snapshot(runtime, packet, (size_t)packet_size, &tick, error, sizeof(error)))
    {
        return false;
    }

    (void)tick;
    match = sdl3d_game_data_find_actor(runtime, "entity.match");
    if (match == nullptr)
    {
        return false;
    }

    if (out_paused != nullptr)
    {
        *out_paused = sdl3d_properties_get_bool(match->props, "paused", false);
    }
    return true;
}

class PongHeadlessMultiplayerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        host_session = create_session(false);
        client_session = create_session(false);
        ASSERT_NE(host_session, nullptr);
        ASSERT_NE(client_session, nullptr);

        ASSERT_TRUE(load_pong_runtime(host_session, &host_runtime));
        ASSERT_TRUE(load_pong_runtime(client_session, &client_runtime));

        ASSERT_TRUE(enter_multiplayer_play_scene(host_runtime, "lan", "host", "host"));
        ASSERT_TRUE(enter_multiplayer_play_scene(client_runtime, "lan", "client", "direct"));

        set_multiplayer_scene_state(host_runtime, "lan", "host", "host");
        set_multiplayer_scene_state(client_runtime, "lan", "client", "direct");

        const ::testing::TestInfo *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string test_name =
            test_info != nullptr ? std::string(test_info->test_suite_name()) + "." + test_info->name() : "pong";
        network_port = (Uint16)(30000U + (Uint32)(std::hash<std::string>{}(test_name) % 20000U));

        sdl3d_network_session_desc host_desc{};
        sdl3d_network_session_desc_init(&host_desc);
        host_desc.role = SDL3D_NETWORK_ROLE_HOST;
        host_desc.port = network_port;
        host_desc.handshake_timeout = 2.0f;
        host_desc.idle_timeout = 2.0f;
        ASSERT_TRUE(sdl3d_network_session_create(&host_desc, &host_network));

        sdl3d_network_session_desc client_desc{};
        sdl3d_network_session_desc_init(&client_desc);
        client_desc.role = SDL3D_NETWORK_ROLE_CLIENT;
        client_desc.host = "127.0.0.1";
        client_desc.port = network_port;
        client_desc.handshake_timeout = 2.0f;
        client_desc.idle_timeout = 2.0f;
        ASSERT_TRUE(sdl3d_network_session_create(&client_desc, &client_network));

        ASSERT_TRUE(wait_for_network_pair(host_network, client_network));

        p1_up = sdl3d_game_data_find_action(host_runtime, "action.paddle.up");
        p1_down = sdl3d_game_data_find_action(host_runtime, "action.paddle.down");
        p2_up = sdl3d_game_data_find_action(client_runtime, "action.paddle.local.up");
        p2_down = sdl3d_game_data_find_action(client_runtime, "action.paddle.local.down");
        ASSERT_GE(p1_up, 0);
        ASSERT_GE(p1_down, 0);
        ASSERT_GE(p2_up, 0);
        ASSERT_GE(p2_down, 0);
    }

    void TearDown() override
    {
        sdl3d_network_session_destroy(client_network);
        sdl3d_network_session_destroy(host_network);
        sdl3d_game_data_destroy(client_runtime);
        sdl3d_game_data_destroy(host_runtime);
        sdl3d_game_session_destroy(client_session);
        sdl3d_game_session_destroy(host_session);
    }

    sdl3d_game_session *host_session = nullptr;
    sdl3d_game_session *client_session = nullptr;
    sdl3d_game_data_runtime *host_runtime = nullptr;
    sdl3d_game_data_runtime *client_runtime = nullptr;
    sdl3d_network_session *host_network = nullptr;
    sdl3d_network_session *client_network = nullptr;
    Uint16 network_port = SDL3D_NETWORK_DEFAULT_PORT;
    int p1_up = -1;
    int p1_down = -1;
    int p2_up = -1;
    int p2_down = -1;
};

TEST_F(PongHeadlessMultiplayerTest, HostAppliesRemoteInputAndClientReceivesAuthoritativeState)
{
    sdl3d_registered_actor *host_cpu = sdl3d_game_data_find_actor(host_runtime, "entity.paddle.cpu");
    sdl3d_registered_actor *client_cpu = sdl3d_game_data_find_actor(client_runtime, "entity.paddle.cpu");
    ASSERT_NE(host_cpu, nullptr);
    ASSERT_NE(client_cpu, nullptr);

    const float initial_host_cpu_y = host_cpu->position.y;
    const float initial_client_cpu_y = client_cpu->position.y;
    const sdl3d_vec3 initial_client_ball_position = sdl3d_game_data_find_actor(client_runtime, "entity.ball")->position;

    ASSERT_TRUE(sdl3d_game_session_tick(host_session, 0.25f));
    ASSERT_TRUE(sdl3d_game_data_update(host_runtime, 0.25f));
    ASSERT_TRUE(sdl3d_game_session_tick(client_session, 0.25f));
    ASSERT_TRUE(sdl3d_game_data_update(client_runtime, 0.25f));

    EXPECT_FLOAT_EQ(host_cpu->position.y, initial_host_cpu_y);
    EXPECT_FLOAT_EQ(client_cpu->position.y, initial_client_cpu_y);
    EXPECT_FLOAT_EQ(sdl3d_game_data_find_actor(client_runtime, "entity.ball")->position.x,
                    initial_client_ball_position.x);
    EXPECT_FLOAT_EQ(sdl3d_game_data_find_actor(client_runtime, "entity.ball")->position.y,
                    initial_client_ball_position.y);

    sdl3d_input_set_action_override(sdl3d_game_session_get_input(client_session), p2_up, 1.0f);
    sdl3d_input_set_action_override(sdl3d_game_session_get_input(client_session), p2_down, 0.0f);
    ASSERT_TRUE(sdl3d_game_session_tick(client_session, 0.016f));

    ASSERT_TRUE(send_client_input_packet(client_runtime, client_session, client_network));

    std::array<Uint8, SDL3D_NETWORK_MAX_PACKET_SIZE> packet{};
    int received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client_network, 0.01f));
        received = sdl3d_network_session_receive(host_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(process_host_input_packet(host_runtime, host_session, packet.data(), received));

    ASSERT_TRUE(sdl3d_game_session_tick(host_session, 0.25f));
    ASSERT_TRUE(sdl3d_game_data_update(host_runtime, 0.25f));

    EXPECT_NE(host_cpu->position.y, initial_host_cpu_y);

    ASSERT_TRUE(send_host_state_packet(host_runtime, host_session, host_network, true));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client_network, 0.01f));
        received = sdl3d_network_session_receive(client_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    bool client_paused = false;
    ASSERT_TRUE(process_client_state_packet(client_runtime, packet.data(), received, &client_paused));

    EXPECT_NEAR(client_cpu->position.y, host_cpu->position.y, 0.0001f);
    EXPECT_TRUE(client_paused);
    EXPECT_EQ(
        sdl3d_properties_get_int(sdl3d_game_data_find_actor(client_runtime, "entity.match")->props, "winner_id", -1),
        sdl3d_properties_get_int(sdl3d_game_data_find_actor(host_runtime, "entity.match")->props, "winner_id", -2));
}

TEST_F(PongHeadlessMultiplayerTest, ControlPacketsCarryPauseResumeAndDisconnect)
{
    std::array<Uint8, SDL3D_NETWORK_MAX_PACKET_SIZE> packet{};

    ASSERT_TRUE(send_control_packet(client_runtime, client_network, PONG_NETWORK_MESSAGE_PAUSE_REQUEST));
    int received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client_network, 0.01f));
        received = sdl3d_network_session_receive(host_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(host_runtime, packet.data(), received, PONG_NETWORK_MESSAGE_PAUSE_REQUEST));

    ASSERT_TRUE(send_control_packet(host_runtime, host_network, PONG_NETWORK_MESSAGE_RESUME_REQUEST));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client_network, 0.01f));
        received = sdl3d_network_session_receive(client_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(client_runtime, packet.data(), received, PONG_NETWORK_MESSAGE_RESUME_REQUEST));

    ASSERT_TRUE(send_control_packet(client_runtime, client_network, PONG_NETWORK_MESSAGE_DISCONNECT));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client_network, 0.01f));
        received = sdl3d_network_session_receive(host_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(host_runtime, packet.data(), received, PONG_NETWORK_MESSAGE_DISCONNECT));

    ASSERT_TRUE(send_control_packet(host_runtime, host_network, PONG_NETWORK_MESSAGE_DISCONNECT));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client_network, 0.01f));
        received = sdl3d_network_session_receive(client_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(client_runtime, packet.data(), received, PONG_NETWORK_MESSAGE_DISCONNECT));
}

TEST_F(PongHeadlessMultiplayerTest, NetworkPauseMenuOmitsOptions)
{
    sdl3d_game_data_ui_metrics metrics{};
    metrics.paused = true;

    sdl3d_game_data_menu menu{};
    sdl3d_game_data_menu_item item{};

    set_multiplayer_scene_state(host_runtime, "single", "client", "direct");
    ASSERT_TRUE(sdl3d_game_data_get_active_menu_for_metrics(host_runtime, &metrics, &menu));
    ASSERT_STREQ(menu.name, "menu.pause");
    ASSERT_EQ(menu.item_count, 3);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(host_runtime, menu.name, 1, &item));
    ASSERT_STREQ(item.label, "Options");

    set_multiplayer_scene_state(host_runtime, "local", "host", "host");
    ASSERT_TRUE(sdl3d_game_data_get_active_menu_for_metrics(host_runtime, &metrics, &menu));
    ASSERT_STREQ(menu.name, "menu.pause");
    ASSERT_EQ(menu.item_count, 3);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(host_runtime, menu.name, 1, &item));
    ASSERT_STREQ(item.label, "Options");

    set_multiplayer_scene_state(host_runtime, "lan", "host", "host");
    ASSERT_TRUE(sdl3d_game_data_get_active_menu_for_metrics(host_runtime, &metrics, &menu));
    ASSERT_STREQ(menu.name, "menu.pause.network");
    ASSERT_EQ(menu.item_count, 2);

    ASSERT_TRUE(sdl3d_game_data_get_menu_item(host_runtime, menu.name, 0, &item));
    ASSERT_STREQ(item.label, "Resume");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(host_runtime, menu.name, 1, &item));
    ASSERT_STREQ(item.label, "Title");

    set_multiplayer_scene_state(host_runtime, "single", "none", "none");
    ASSERT_TRUE(sdl3d_game_data_get_active_menu_for_metrics(host_runtime, &metrics, &menu));
    ASSERT_STREQ(menu.name, "menu.pause");
    ASSERT_EQ(menu.item_count, 3);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(host_runtime, menu.name, 1, &item));
    ASSERT_STREQ(item.label, "Options");
}

TEST_F(PongHeadlessMultiplayerTest, OnlyLanClientDisablesLocalSimulation)
{
    set_multiplayer_scene_state(host_runtime, "single", "client", "direct");
    EXPECT_TRUE(sdl3d_game_data_active_scene_update_phase(host_runtime, "simulation", false));

    set_multiplayer_scene_state(host_runtime, "local", "client", "direct");
    EXPECT_TRUE(sdl3d_game_data_active_scene_update_phase(host_runtime, "simulation", false));

    set_multiplayer_scene_state(host_runtime, "lan", "host", "host");
    EXPECT_TRUE(sdl3d_game_data_active_scene_update_phase(host_runtime, "simulation", false));

    set_multiplayer_scene_state(host_runtime, "lan", "client", "direct");
    EXPECT_FALSE(sdl3d_game_data_active_scene_update_phase(host_runtime, "simulation", false));
}

} // namespace

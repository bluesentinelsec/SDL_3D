#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

extern "C"
{
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/game.h"
#include "slayer3d/game_data.h"
#include "slayer3d/network.h"
#include "slayer3d/properties.h"
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

static std::filesystem::path demo_data_path(const char *demo_name, const char *data_file)
{
    return std::filesystem::path(SLAYER3D_DEMOS_ROOT) / demo_name / "data" / data_file;
}

static std::filesystem::path pong_data_path()
{
    return demo_data_path("pong", "pong.game.json");
}

static slayer3d_game_session *create_session(bool include_audio = false)
{
    slayer3d_game_session_desc desc{};
    slayer3d_game_session_desc_init(&desc);
    if (include_audio)
    {
        desc.create_services |= SLAYER3D_GAME_SESSION_SERVICE_AUDIO;
        desc.optional_audio = true;
    }
    slayer3d_game_session *session = nullptr;
    EXPECT_TRUE(slayer3d_game_session_create(&desc, &session));
    return session;
}

static bool load_pong_runtime(slayer3d_game_session *session, slayer3d_game_data_runtime **out_runtime)
{
    char error[256]{};
    EXPECT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, out_runtime, error, sizeof(error)))
        << error;
    return *out_runtime != nullptr;
}

static void set_multiplayer_scene_state(slayer3d_game_data_runtime *runtime, const char *match_mode,
                                        const char *network_role, const char *network_flow)
{
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_string(scene_state, "match_mode", match_mode != nullptr ? match_mode : "");
    slayer3d_properties_set_string(scene_state, "network_role", network_role != nullptr ? network_role : "");
    slayer3d_properties_set_string(scene_state, "network_flow", network_flow != nullptr ? network_flow : "");
}

static bool enter_multiplayer_play_scene(slayer3d_game_data_runtime *runtime, const char *match_mode,
                                         const char *network_role, const char *network_flow)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == nullptr)
    {
        return false;
    }

    if (match_mode != nullptr)
    {
        slayer3d_properties_set_string(payload, "match_mode", match_mode);
    }
    if (network_role != nullptr)
    {
        slayer3d_properties_set_string(payload, "network_role", network_role);
    }
    if (network_flow != nullptr)
    {
        slayer3d_properties_set_string(payload, "network_flow", network_flow);
    }

    const bool ok = slayer3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload);
    slayer3d_properties_destroy(payload);
    return ok;
}

static bool wait_for_network_pair(slayer3d_network_session *host, slayer3d_network_session *client)
{
    for (int i = 0; i < 1200; ++i)
    {
        EXPECT_TRUE(slayer3d_network_session_update(host, 0.01f));
        EXPECT_TRUE(slayer3d_network_session_update(client, 0.01f));
        if (slayer3d_network_session_is_connected(host) && slayer3d_network_session_is_connected(client))
        {
            return true;
        }
        if (slayer3d_network_session_state(client) == SLAYER3D_NETWORK_STATE_REJECTED ||
            slayer3d_network_session_state(client) == SLAYER3D_NETWORK_STATE_TIMED_OUT ||
            slayer3d_network_session_state(client) == SLAYER3D_NETWORK_STATE_ERROR)
        {
            return false;
        }
    }
    return false;
}

static bool send_client_input_packet(slayer3d_game_data_runtime *runtime, slayer3d_game_session *session,
                                     slayer3d_network_session *net_session)
{
    const slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    const slayer3d_input_snapshot *snapshot = slayer3d_input_get_snapshot(input);
    Uint8 packet[128];
    size_t packet_size = 0U;
    char error[160]{};
    const char *client_input_channel = nullptr;

    return slayer3d_game_data_get_network_runtime_replication(runtime, PONG_NETWORK_BINDING_CLIENT_INPUT,
                                                              &client_input_channel) &&
           slayer3d_game_data_encode_network_input(runtime, client_input_channel, input,
                                                   (Uint32)SDL_max(snapshot != nullptr ? snapshot->tick : 0, 0), packet,
                                                   sizeof(packet), &packet_size, error, sizeof(error)) &&
           slayer3d_network_session_send(net_session, packet, (int)packet_size);
}

static bool process_host_input_packet(slayer3d_game_data_runtime *runtime, slayer3d_game_session *session,
                                      const Uint8 *packet, int packet_size)
{
    Uint32 tick = 0U;
    char error[160]{};

    if (runtime == nullptr || session == nullptr || packet == nullptr || packet_size <= 0)
    {
        return false;
    }

    return slayer3d_game_data_apply_network_input(runtime, slayer3d_game_session_get_input(session), packet,
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

static const char *control_name_for_kind(slayer3d_game_data_runtime *runtime, PongNetworkMessageKind kind)
{
    const char *control_name = nullptr;
    const char *binding = control_binding_for_kind(kind);
    return binding != nullptr && slayer3d_game_data_get_network_runtime_control(runtime, binding, &control_name)
               ? control_name
               : nullptr;
}

static bool send_control_packet(slayer3d_game_data_runtime *runtime, slayer3d_network_session *net_session,
                                PongNetworkMessageKind kind)
{
    Uint8 packet[SLAYER3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE];
    size_t packet_size = 0U;
    char error[160]{};
    const char *control_name = control_name_for_kind(runtime, kind);

    return control_name != nullptr &&
           slayer3d_game_data_encode_network_control(runtime, control_name, 1234U, packet, sizeof(packet), &packet_size,
                                                     error, sizeof(error)) &&
           slayer3d_network_session_send(net_session, packet, (int)packet_size);
}

static bool read_control_packet(slayer3d_game_data_runtime *runtime, const Uint8 *packet, int packet_size,
                                PongNetworkMessageKind expected)
{
    slayer3d_game_data_network_control control{};
    char error[160]{};
    const char *expected_name = control_name_for_kind(runtime, expected);

    return runtime != nullptr && packet != nullptr && expected_name != nullptr &&
           slayer3d_game_data_decode_network_control(runtime, packet, (size_t)packet_size, &control, error,
                                                     sizeof(error)) &&
           SDL_strcmp(control.name, expected_name) == 0 && control.tick == 1234U;
}

static bool send_host_state_packet(slayer3d_game_data_runtime *runtime, slayer3d_game_session *session,
                                   slayer3d_network_session *net_session, bool paused)
{
    const slayer3d_input_snapshot *snapshot = slayer3d_input_get_snapshot(slayer3d_game_session_get_input(session));
    Uint8 packet[SLAYER3D_NETWORK_MAX_PACKET_SIZE];
    size_t packet_size = 0U;
    char error[160]{};
    const char *state_channel = nullptr;

    if (runtime == nullptr || session == nullptr || net_session == nullptr ||
        !slayer3d_game_data_get_network_runtime_replication(runtime, PONG_NETWORK_BINDING_STATE_SNAPSHOT,
                                                            &state_channel))
    {
        return false;
    }

    if (!slayer3d_game_data_set_network_runtime_pause_state(runtime, paused, error, sizeof(error)))
    {
        return false;
    }
    return slayer3d_game_data_encode_network_snapshot(runtime, state_channel,
                                                      (Uint32)SDL_max(snapshot != nullptr ? snapshot->tick : 0, 0),
                                                      packet, sizeof(packet), &packet_size, error, sizeof(error)) &&
           slayer3d_network_session_send(net_session, packet, (int)packet_size);
}

static bool process_client_state_packet(slayer3d_game_data_runtime *runtime, const Uint8 *packet, int packet_size,
                                        bool *out_paused)
{
    Uint32 tick = 0U;
    char error[160]{};

    if (runtime == nullptr || packet == nullptr || packet_size <= 0)
    {
        return false;
    }

    if (!slayer3d_game_data_apply_network_snapshot(runtime, packet, (size_t)packet_size, &tick, error, sizeof(error)))
    {
        return false;
    }

    (void)tick;
    if (out_paused != nullptr)
    {
        return slayer3d_game_data_get_network_runtime_pause_state(runtime, out_paused, error, sizeof(error));
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

        slayer3d_network_session_desc host_desc{};
        slayer3d_network_session_desc_init(&host_desc);
        host_desc.role = SLAYER3D_NETWORK_ROLE_HOST;
        host_desc.port = network_port;
        host_desc.handshake_timeout = 2.0f;
        host_desc.idle_timeout = 2.0f;
        ASSERT_TRUE(slayer3d_network_session_create(&host_desc, &host_network));

        slayer3d_network_session_desc client_desc{};
        slayer3d_network_session_desc_init(&client_desc);
        client_desc.role = SLAYER3D_NETWORK_ROLE_CLIENT;
        client_desc.host = "127.0.0.1";
        client_desc.port = network_port;
        client_desc.handshake_timeout = 2.0f;
        client_desc.idle_timeout = 2.0f;
        ASSERT_TRUE(slayer3d_network_session_create(&client_desc, &client_network));

        ASSERT_TRUE(wait_for_network_pair(host_network, client_network));

        p1_up = slayer3d_game_data_find_action(host_runtime, "action.paddle.up");
        p1_down = slayer3d_game_data_find_action(host_runtime, "action.paddle.down");
        p2_up = slayer3d_game_data_find_action(client_runtime, "action.paddle.local.up");
        p2_down = slayer3d_game_data_find_action(client_runtime, "action.paddle.local.down");
        ASSERT_GE(p1_up, 0);
        ASSERT_GE(p1_down, 0);
        ASSERT_GE(p2_up, 0);
        ASSERT_GE(p2_down, 0);
    }

    void TearDown() override
    {
        slayer3d_network_session_destroy(client_network);
        slayer3d_network_session_destroy(host_network);
        slayer3d_game_data_destroy(client_runtime);
        slayer3d_game_data_destroy(host_runtime);
        slayer3d_game_session_destroy(client_session);
        slayer3d_game_session_destroy(host_session);
    }

    slayer3d_game_session *host_session = nullptr;
    slayer3d_game_session *client_session = nullptr;
    slayer3d_game_data_runtime *host_runtime = nullptr;
    slayer3d_game_data_runtime *client_runtime = nullptr;
    slayer3d_network_session *host_network = nullptr;
    slayer3d_network_session *client_network = nullptr;
    Uint16 network_port = SLAYER3D_NETWORK_DEFAULT_PORT;
    int p1_up = -1;
    int p1_down = -1;
    int p2_up = -1;
    int p2_down = -1;
};

TEST_F(PongHeadlessMultiplayerTest, HostAppliesRemoteInputAndClientReceivesAuthoritativeState)
{
    slayer3d_registered_actor *host_cpu = slayer3d_game_data_find_actor(host_runtime, "entity.paddle.cpu");
    slayer3d_registered_actor *client_cpu = slayer3d_game_data_find_actor(client_runtime, "entity.paddle.cpu");
    ASSERT_NE(host_cpu, nullptr);
    ASSERT_NE(client_cpu, nullptr);

    const float initial_host_cpu_y = host_cpu->position.y;
    const float initial_client_cpu_y = client_cpu->position.y;
    const slayer3d_vec3 initial_client_ball_position =
        slayer3d_game_data_find_actor(client_runtime, "entity.ball")->position;

    ASSERT_TRUE(slayer3d_game_session_tick(host_session, 0.25f));
    ASSERT_TRUE(slayer3d_game_data_update(host_runtime, 0.25f));
    ASSERT_TRUE(slayer3d_game_session_tick(client_session, 0.25f));
    ASSERT_TRUE(slayer3d_game_data_update(client_runtime, 0.25f));

    EXPECT_FLOAT_EQ(host_cpu->position.y, initial_host_cpu_y);
    EXPECT_FLOAT_EQ(client_cpu->position.y, initial_client_cpu_y);
    EXPECT_FLOAT_EQ(slayer3d_game_data_find_actor(client_runtime, "entity.ball")->position.x,
                    initial_client_ball_position.x);
    EXPECT_FLOAT_EQ(slayer3d_game_data_find_actor(client_runtime, "entity.ball")->position.y,
                    initial_client_ball_position.y);

    slayer3d_input_set_action_override(slayer3d_game_session_get_input(client_session), p2_up, 1.0f);
    slayer3d_input_set_action_override(slayer3d_game_session_get_input(client_session), p2_down, 0.0f);
    ASSERT_TRUE(slayer3d_game_session_tick(client_session, 0.016f));

    ASSERT_TRUE(send_client_input_packet(client_runtime, client_session, client_network));

    std::array<Uint8, SLAYER3D_NETWORK_MAX_PACKET_SIZE> packet{};
    int received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(slayer3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(slayer3d_network_session_update(client_network, 0.01f));
        received = slayer3d_network_session_receive(host_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(process_host_input_packet(host_runtime, host_session, packet.data(), received));

    ASSERT_TRUE(slayer3d_game_session_tick(host_session, 0.25f));
    ASSERT_TRUE(slayer3d_game_data_update(host_runtime, 0.25f));

    EXPECT_NE(host_cpu->position.y, initial_host_cpu_y);

    ASSERT_TRUE(send_host_state_packet(host_runtime, host_session, host_network, true));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(slayer3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(slayer3d_network_session_update(client_network, 0.01f));
        received = slayer3d_network_session_receive(client_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    bool client_paused = false;
    ASSERT_TRUE(process_client_state_packet(client_runtime, packet.data(), received, &client_paused));

    EXPECT_NEAR(client_cpu->position.y, host_cpu->position.y, 0.0001f);
    EXPECT_TRUE(client_paused);
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_find_actor(client_runtime, "entity.match")->props,
                                          "winner_id", -1),
              slayer3d_properties_get_int(slayer3d_game_data_find_actor(host_runtime, "entity.match")->props,
                                          "winner_id", -2));
}

TEST_F(PongHeadlessMultiplayerTest, ControlPacketsCarryPauseResumeAndDisconnect)
{
    std::array<Uint8, SLAYER3D_NETWORK_MAX_PACKET_SIZE> packet{};

    ASSERT_TRUE(send_control_packet(client_runtime, client_network, PONG_NETWORK_MESSAGE_PAUSE_REQUEST));
    int received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(slayer3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(slayer3d_network_session_update(client_network, 0.01f));
        received = slayer3d_network_session_receive(host_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(host_runtime, packet.data(), received, PONG_NETWORK_MESSAGE_PAUSE_REQUEST));

    ASSERT_TRUE(send_control_packet(host_runtime, host_network, PONG_NETWORK_MESSAGE_RESUME_REQUEST));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(slayer3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(slayer3d_network_session_update(client_network, 0.01f));
        received = slayer3d_network_session_receive(client_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(client_runtime, packet.data(), received, PONG_NETWORK_MESSAGE_RESUME_REQUEST));

    ASSERT_TRUE(send_control_packet(client_runtime, client_network, PONG_NETWORK_MESSAGE_DISCONNECT));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(slayer3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(slayer3d_network_session_update(client_network, 0.01f));
        received = slayer3d_network_session_receive(host_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(host_runtime, packet.data(), received, PONG_NETWORK_MESSAGE_DISCONNECT));

    ASSERT_TRUE(send_control_packet(host_runtime, host_network, PONG_NETWORK_MESSAGE_DISCONNECT));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(slayer3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(slayer3d_network_session_update(client_network, 0.01f));
        received = slayer3d_network_session_receive(client_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(client_runtime, packet.data(), received, PONG_NETWORK_MESSAGE_DISCONNECT));
}

TEST_F(PongHeadlessMultiplayerTest, NetworkPauseMenuOmitsOptions)
{
    slayer3d_game_data_ui_metrics metrics{};
    metrics.paused = true;

    slayer3d_game_data_menu menu{};
    slayer3d_game_data_menu_item item{};

    set_multiplayer_scene_state(host_runtime, "single", "client", "direct");
    ASSERT_TRUE(slayer3d_game_data_get_active_menu_for_metrics(host_runtime, &metrics, &menu));
    ASSERT_STREQ(menu.name, "menu.pause");
    ASSERT_EQ(menu.item_count, 3);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(host_runtime, menu.name, 1, &item));
    ASSERT_STREQ(item.label, "Options");

    set_multiplayer_scene_state(host_runtime, "local", "host", "host");
    ASSERT_TRUE(slayer3d_game_data_get_active_menu_for_metrics(host_runtime, &metrics, &menu));
    ASSERT_STREQ(menu.name, "menu.pause");
    ASSERT_EQ(menu.item_count, 3);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(host_runtime, menu.name, 1, &item));
    ASSERT_STREQ(item.label, "Options");

    set_multiplayer_scene_state(host_runtime, "lan", "host", "host");
    ASSERT_TRUE(slayer3d_game_data_get_active_menu_for_metrics(host_runtime, &metrics, &menu));
    ASSERT_STREQ(menu.name, "menu.pause.network");
    ASSERT_EQ(menu.item_count, 2);

    ASSERT_TRUE(slayer3d_game_data_get_menu_item(host_runtime, menu.name, 0, &item));
    ASSERT_STREQ(item.label, "Resume");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(host_runtime, menu.name, 1, &item));
    ASSERT_STREQ(item.label, "Title");

    set_multiplayer_scene_state(host_runtime, "single", "none", "none");
    ASSERT_TRUE(slayer3d_game_data_get_active_menu_for_metrics(host_runtime, &metrics, &menu));
    ASSERT_STREQ(menu.name, "menu.pause");
    ASSERT_EQ(menu.item_count, 3);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(host_runtime, menu.name, 1, &item));
    ASSERT_STREQ(item.label, "Options");
}

TEST_F(PongHeadlessMultiplayerTest, OnlyLanClientDisablesLocalSimulation)
{
    set_multiplayer_scene_state(host_runtime, "single", "client", "direct");
    EXPECT_TRUE(slayer3d_game_data_active_scene_update_phase(host_runtime, "simulation", false));

    set_multiplayer_scene_state(host_runtime, "local", "client", "direct");
    EXPECT_TRUE(slayer3d_game_data_active_scene_update_phase(host_runtime, "simulation", false));

    set_multiplayer_scene_state(host_runtime, "lan", "host", "host");
    EXPECT_TRUE(slayer3d_game_data_active_scene_update_phase(host_runtime, "simulation", false));

    set_multiplayer_scene_state(host_runtime, "lan", "client", "direct");
    EXPECT_FALSE(slayer3d_game_data_active_scene_update_phase(host_runtime, "simulation", false));
}

} // namespace

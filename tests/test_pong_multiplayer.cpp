#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

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
constexpr Uint32 kPongNetworkPacketMagic = 0x474E4F50u;
constexpr Uint8 kPongNetworkPacketVersion = 1U;
constexpr size_t kPongNetworkControlPacketSize = 12U;
constexpr size_t kPongNetworkStatePacketSize = 112U;

enum PongNetworkMessageKind : Uint8
{
    PONG_NETWORK_MESSAGE_INPUT = 1,
    PONG_NETWORK_MESSAGE_STATE = 2,
    PONG_NETWORK_MESSAGE_START_GAME = 3,
    PONG_NETWORK_MESSAGE_PAUSE_REQUEST = 4,
    PONG_NETWORK_MESSAGE_RESUME_REQUEST = 5,
    PONG_NETWORK_MESSAGE_DISCONNECT = 6,
};

static bool write_u8(Uint8 **cursor, Uint8 *end, Uint8 value)
{
    if (cursor == nullptr || *cursor == nullptr || end == nullptr || *cursor >= end)
    {
        return false;
    }
    **cursor = value;
    ++(*cursor);
    return true;
}

static bool write_u32(Uint8 **cursor, Uint8 *end, Uint32 value)
{
    Uint8 bytes[4];
    bytes[0] = (Uint8)(value & 0xffU);
    bytes[1] = (Uint8)((value >> 8U) & 0xffU);
    bytes[2] = (Uint8)((value >> 16U) & 0xffU);
    bytes[3] = (Uint8)((value >> 24U) & 0xffU);
    if (cursor == nullptr || *cursor == nullptr || end == nullptr || (size_t)(end - *cursor) < sizeof(bytes))
    {
        return false;
    }
    SDL_memcpy(*cursor, bytes, sizeof(bytes));
    *cursor += sizeof(bytes);
    return true;
}

static bool write_i32(Uint8 **cursor, Uint8 *end, int value)
{
    return write_u32(cursor, end, (Uint32)(Sint32)value);
}

static bool write_f32(Uint8 **cursor, Uint8 *end, float value)
{
    Uint32 bits = 0U;
    SDL_memcpy(&bits, &value, sizeof(bits));
    return write_u32(cursor, end, bits);
}

static bool write_vec2(Uint8 **cursor, Uint8 *end, sdl3d_vec2 value)
{
    return write_f32(cursor, end, value.x) && write_f32(cursor, end, value.y);
}

static bool write_vec3(Uint8 **cursor, Uint8 *end, sdl3d_vec3 value)
{
    return write_f32(cursor, end, value.x) && write_f32(cursor, end, value.y) && write_f32(cursor, end, value.z);
}

static bool read_u8(const Uint8 **cursor, const Uint8 *end, Uint8 *out_value)
{
    if (cursor == nullptr || *cursor == nullptr || out_value == nullptr || *cursor >= end)
    {
        return false;
    }
    *out_value = **cursor;
    ++(*cursor);
    return true;
}

static bool read_u32(const Uint8 **cursor, const Uint8 *end, Uint32 *out_value)
{
    if (cursor == nullptr || *cursor == nullptr || out_value == nullptr || (size_t)(end - *cursor) < 4)
    {
        return false;
    }

    const Uint8 *src = *cursor;
    *out_value = (Uint32)src[0] | ((Uint32)src[1] << 8U) | ((Uint32)src[2] << 16U) | ((Uint32)src[3] << 24U);
    *cursor += 4;
    return true;
}

static bool read_i32(const Uint8 **cursor, const Uint8 *end, int *out_value)
{
    Uint32 value = 0U;
    if (!read_u32(cursor, end, &value))
    {
        return false;
    }
    *out_value = (int)(Sint32)value;
    return true;
}

static bool read_f32(const Uint8 **cursor, const Uint8 *end, float *out_value)
{
    Uint32 bits = 0U;
    if (!read_u32(cursor, end, &bits))
    {
        return false;
    }
    SDL_memcpy(out_value, &bits, sizeof(bits));
    return true;
}

static bool read_vec3(const Uint8 **cursor, const Uint8 *end, sdl3d_vec3 *out_value)
{
    return read_f32(cursor, end, &out_value->x) && read_f32(cursor, end, &out_value->y) &&
           read_f32(cursor, end, &out_value->z);
}

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

    return sdl3d_game_data_encode_network_input(runtime, "client_input", input,
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

static bool send_control_packet(sdl3d_network_session *net_session, PongNetworkMessageKind kind)
{
    Uint8 packet[32];
    Uint8 *cursor = packet;
    Uint8 *end = packet + sizeof(packet);

    return write_u32(&cursor, end, kPongNetworkPacketMagic) && write_u8(&cursor, end, kPongNetworkPacketVersion) &&
           write_u8(&cursor, end, kind) && write_u8(&cursor, end, 0U) && write_u8(&cursor, end, 0U) &&
           write_u32(&cursor, end, 1234U) && (size_t)(cursor - packet) == kPongNetworkControlPacketSize &&
           sdl3d_network_session_send(net_session, packet, (int)(cursor - packet));
}

static bool read_control_packet(const Uint8 *packet, int packet_size, PongNetworkMessageKind expected)
{
    const Uint8 *cursor = packet;
    const Uint8 *end = packet + packet_size;
    Uint32 magic = 0U;
    Uint8 version = 0U;
    Uint8 kind = 0U;
    Uint8 reserved = 0U;
    Uint32 tick = 0U;

    return packet != nullptr && (size_t)packet_size == kPongNetworkControlPacketSize &&
           read_u32(&cursor, end, &magic) && magic == kPongNetworkPacketMagic && read_u8(&cursor, end, &version) &&
           version == kPongNetworkPacketVersion && read_u8(&cursor, end, &kind) && kind == (Uint8)expected &&
           read_u8(&cursor, end, &reserved) && read_u8(&cursor, end, &reserved) && read_u32(&cursor, end, &tick) &&
           tick == 1234U && cursor == end;
}

static bool send_host_state_packet(sdl3d_game_data_runtime *runtime, sdl3d_game_session *session,
                                   sdl3d_network_session *net_session, bool paused)
{
    const sdl3d_registered_actor *player = sdl3d_game_data_find_actor(runtime, "entity.paddle.player");
    const sdl3d_registered_actor *cpu = sdl3d_game_data_find_actor(runtime, "entity.paddle.cpu");
    const sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball");
    const sdl3d_registered_actor *match = sdl3d_game_data_find_actor(runtime, "entity.match");
    const sdl3d_registered_actor *presentation = sdl3d_game_data_find_actor(runtime, "entity.presentation");
    const sdl3d_registered_actor *score_player_actor = sdl3d_game_data_find_actor(runtime, "entity.score.player");
    const sdl3d_registered_actor *score_cpu_actor = sdl3d_game_data_find_actor(runtime, "entity.score.cpu");
    const sdl3d_input_snapshot *snapshot = sdl3d_input_get_snapshot(sdl3d_game_session_get_input(session));
    Uint8 packet[160];
    Uint8 *cursor = packet;
    Uint8 *end = packet + sizeof(packet);
    const int p1_up = sdl3d_game_data_find_action(runtime, "action.paddle.up");
    const int p1_down = sdl3d_game_data_find_action(runtime, "action.paddle.down");
    const float p1_up_value = snapshot != nullptr && p1_up >= 0 ? snapshot->actions[p1_up].value : 0.0f;
    const float p1_down_value = snapshot != nullptr && p1_down >= 0 ? snapshot->actions[p1_down].value : 0.0f;

    if (player == nullptr || cpu == nullptr || ball == nullptr || match == nullptr || presentation == nullptr ||
        score_player_actor == nullptr || score_cpu_actor == nullptr)
    {
        return false;
    }

    return write_u32(&cursor, end, kPongNetworkPacketMagic) && write_u8(&cursor, end, kPongNetworkPacketVersion) &&
           write_u8(&cursor, end, PONG_NETWORK_MESSAGE_STATE) && write_u8(&cursor, end, 0U) &&
           write_u8(&cursor, end, 0U) &&
           write_u32(&cursor, end, (Uint32)SDL_max(snapshot != nullptr ? snapshot->tick : 0, 0)) &&
           write_f32(&cursor, end, p1_up_value) && write_f32(&cursor, end, p1_down_value) &&
           write_vec3(&cursor, end, player->position) && write_vec3(&cursor, end, cpu->position) &&
           write_vec3(&cursor, end, ball->position) &&
           write_vec3(&cursor, end,
                      sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f))) &&
           write_f32(&cursor, end, sdl3d_properties_get_float(presentation->props, "border_flash", 0.0f)) &&
           write_f32(&cursor, end, sdl3d_properties_get_float(presentation->props, "paddle_flash", 0.0f)) &&
           write_i32(&cursor, end, sdl3d_properties_get_int(score_player_actor->props, "value", 0)) &&
           write_i32(&cursor, end, sdl3d_properties_get_int(score_cpu_actor->props, "value", 0)) &&
           write_i32(&cursor, end, paused ? 1 : 0) &&
           write_i32(&cursor, end, sdl3d_properties_get_bool(match->props, "finished", false) ? 1 : 0) &&
           write_i32(&cursor, end,
                     SDL_strcmp(sdl3d_properties_get_string(match->props, "winner", "none"), "player") == 0 ? 1
                     : SDL_strcmp(sdl3d_properties_get_string(match->props, "winner", "none"), "cpu") == 0  ? 2
                                                                                                            : 0) &&
           write_i32(&cursor, end, sdl3d_properties_get_bool(ball->props, "active_motion", false) ? 1 : 0) &&
           write_i32(&cursor, end, sdl3d_properties_get_bool(ball->props, "has_last_reflect_y", false) ? 1 : 0) &&
           write_f32(&cursor, end, sdl3d_properties_get_float(ball->props, "last_reflect_y", 0.0f)) &&
           write_i32(&cursor, end, sdl3d_properties_get_int(ball->props, "stagnant_reflect_count", 0)) &&
           (size_t)(cursor - packet) == kPongNetworkStatePacketSize &&
           sdl3d_network_session_send(net_session, packet, (int)(cursor - packet));
}

static bool process_client_state_packet(sdl3d_game_data_runtime *runtime, const Uint8 *packet, int packet_size,
                                        bool *out_paused)
{
    const Uint8 *cursor = packet;
    const Uint8 *end = packet + packet_size;
    Uint32 magic = 0U;
    Uint8 version = 0U;
    Uint8 kind = 0U;
    Uint8 reserved = 0U;
    Uint32 tick = 0U;
    float p1_up = 0.0f;
    float p1_down = 0.0f;
    sdl3d_vec3 player_position{};
    sdl3d_vec3 cpu_position{};
    sdl3d_vec3 ball_position{};
    sdl3d_vec3 ball_velocity{};
    float border_flash = 0.0f;
    float paddle_flash = 0.0f;
    int score_player = 0;
    int score_cpu = 0;
    int paused = 0;
    int finished = 0;
    int winner = 0;
    int active_motion = 0;
    int has_last_reflect_y = 0;
    float last_reflect_y = 0.0f;
    int stagnant_reflect_count = 0;
    sdl3d_registered_actor *player = nullptr;
    sdl3d_registered_actor *cpu = nullptr;
    sdl3d_registered_actor *ball = nullptr;
    sdl3d_registered_actor *match = nullptr;
    sdl3d_registered_actor *presentation = nullptr;
    sdl3d_registered_actor *score_player_actor = nullptr;
    sdl3d_registered_actor *score_cpu_actor = nullptr;

    if (runtime == nullptr || packet == nullptr || packet_size < 12)
    {
        return false;
    }

    if (!read_u32(&cursor, end, &magic) || magic != kPongNetworkPacketMagic || !read_u8(&cursor, end, &version) ||
        version != kPongNetworkPacketVersion || !read_u8(&cursor, end, &kind) ||
        kind != (Uint8)PONG_NETWORK_MESSAGE_STATE || !read_u8(&cursor, end, &reserved) ||
        !read_u8(&cursor, end, &reserved) || !read_u32(&cursor, end, &tick) || !read_f32(&cursor, end, &p1_up) ||
        !read_f32(&cursor, end, &p1_down) || !read_vec3(&cursor, end, &player_position) ||
        !read_vec3(&cursor, end, &cpu_position) || !read_vec3(&cursor, end, &ball_position) ||
        !read_vec3(&cursor, end, &ball_velocity) || !read_f32(&cursor, end, &border_flash) ||
        !read_f32(&cursor, end, &paddle_flash) || !read_i32(&cursor, end, &score_player) ||
        !read_i32(&cursor, end, &score_cpu) || !read_i32(&cursor, end, &paused) || !read_i32(&cursor, end, &finished) ||
        !read_i32(&cursor, end, &winner) || !read_i32(&cursor, end, &active_motion) ||
        !read_i32(&cursor, end, &has_last_reflect_y) || !read_f32(&cursor, end, &last_reflect_y) ||
        !read_i32(&cursor, end, &stagnant_reflect_count))
    {
        return false;
    }

    if ((size_t)packet_size != kPongNetworkStatePacketSize)
    {
        return false;
    }

    (void)tick;
    player = sdl3d_game_data_find_actor(runtime, "entity.paddle.player");
    cpu = sdl3d_game_data_find_actor(runtime, "entity.paddle.cpu");
    ball = sdl3d_game_data_find_actor(runtime, "entity.ball");
    match = sdl3d_game_data_find_actor(runtime, "entity.match");
    presentation = sdl3d_game_data_find_actor(runtime, "entity.presentation");
    score_player_actor = sdl3d_game_data_find_actor(runtime, "entity.score.player");
    score_cpu_actor = sdl3d_game_data_find_actor(runtime, "entity.score.cpu");

    if (player == nullptr || cpu == nullptr || ball == nullptr || match == nullptr || presentation == nullptr ||
        score_player_actor == nullptr || score_cpu_actor == nullptr)
    {
        return false;
    }

    player->position = player_position;
    cpu->position = cpu_position;
    ball->position = ball_position;
    sdl3d_properties_set_vec3(ball->props, "velocity", ball_velocity);
    sdl3d_properties_set_bool(ball->props, "active_motion", active_motion != 0);
    sdl3d_properties_set_bool(ball->props, "has_last_reflect_y", has_last_reflect_y != 0);
    sdl3d_properties_set_float(ball->props, "last_reflect_y", last_reflect_y);
    sdl3d_properties_set_int(ball->props, "stagnant_reflect_count", stagnant_reflect_count);
    sdl3d_properties_set_int(score_player_actor->props, "value", score_player);
    sdl3d_properties_set_int(score_cpu_actor->props, "value", score_cpu);
    if (out_paused != nullptr)
    {
        *out_paused = paused != 0;
    }
    sdl3d_properties_set_bool(match->props, "finished", finished != 0);
    sdl3d_properties_set_string(match->props, "winner", winner == 1 ? "player" : winner == 2 ? "cpu" : "none");
    sdl3d_properties_set_float(presentation->props, "border_flash", border_flash);
    sdl3d_properties_set_float(presentation->props, "paddle_flash", paddle_flash);
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

        sdl3d_network_session_desc host_desc{};
        sdl3d_network_session_desc_init(&host_desc);
        host_desc.role = SDL3D_NETWORK_ROLE_HOST;
        host_desc.port = SDL3D_NETWORK_DEFAULT_PORT;
        host_desc.handshake_timeout = 2.0f;
        host_desc.idle_timeout = 2.0f;
        ASSERT_TRUE(sdl3d_network_session_create(&host_desc, &host_network));

        sdl3d_network_session_desc client_desc{};
        sdl3d_network_session_desc_init(&client_desc);
        client_desc.role = SDL3D_NETWORK_ROLE_CLIENT;
        client_desc.host = "127.0.0.1";
        client_desc.port = SDL3D_NETWORK_DEFAULT_PORT;
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
    EXPECT_STREQ(
        sdl3d_properties_get_string(sdl3d_game_data_find_actor(client_runtime, "entity.match")->props, "winner",
                                    "none"),
        sdl3d_properties_get_string(sdl3d_game_data_find_actor(host_runtime, "entity.match")->props, "winner", "none"));
}

TEST_F(PongHeadlessMultiplayerTest, ControlPacketsCarryPauseResumeAndDisconnect)
{
    std::array<Uint8, SDL3D_NETWORK_MAX_PACKET_SIZE> packet{};

    ASSERT_TRUE(send_control_packet(client_network, PONG_NETWORK_MESSAGE_PAUSE_REQUEST));
    int received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client_network, 0.01f));
        received = sdl3d_network_session_receive(host_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(packet.data(), received, PONG_NETWORK_MESSAGE_PAUSE_REQUEST));

    ASSERT_TRUE(send_control_packet(host_network, PONG_NETWORK_MESSAGE_RESUME_REQUEST));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client_network, 0.01f));
        received = sdl3d_network_session_receive(client_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(packet.data(), received, PONG_NETWORK_MESSAGE_RESUME_REQUEST));

    ASSERT_TRUE(send_control_packet(client_network, PONG_NETWORK_MESSAGE_DISCONNECT));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client_network, 0.01f));
        received = sdl3d_network_session_receive(host_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(packet.data(), received, PONG_NETWORK_MESSAGE_DISCONNECT));

    ASSERT_TRUE(send_control_packet(host_network, PONG_NETWORK_MESSAGE_DISCONNECT));
    received = 0;
    for (int i = 0; i < 120 && received <= 0; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(host_network, 0.01f));
        EXPECT_TRUE(sdl3d_network_session_update(client_network, 0.01f));
        received = sdl3d_network_session_receive(client_network, packet.data(), (int)packet.size());
    }
    ASSERT_GT(received, 0);
    ASSERT_TRUE(read_control_packet(packet.data(), received, PONG_NETWORK_MESSAGE_DISCONNECT));
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

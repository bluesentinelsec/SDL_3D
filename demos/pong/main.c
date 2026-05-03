#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <stdbool.h>
#include <string.h>

#include "sdl3d/asset.h"
#include "sdl3d/font.h"
#include "sdl3d/game.h"
#include "sdl3d/game_data.h"
#include "sdl3d/game_presentation.h"
#include "sdl3d/network.h"
#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/ui.h"

#if SDL3D_PONG_EMBEDDED_ASSETS
#include "sdl3d_pong_assets.h"
#endif

typedef struct pong_state
{
    sdl3d_asset_resolver *assets;
    sdl3d_game_data_runtime *data;
    sdl3d_game_data_font_cache font_cache;
    sdl3d_game_data_image_cache image_cache;
    sdl3d_game_data_particle_cache particle_cache;
    sdl3d_game_data_app_flow app_flow;
    sdl3d_game_data_frame_state frame_state;
    sdl3d_input_manager *input;
    sdl3d_font direct_connect_font;
    sdl3d_ui_context *direct_connect_ui;
    sdl3d_ui_context *discovery_ui;
    sdl3d_network_session *host_session;
    sdl3d_network_session *direct_connect_session;
    sdl3d_network_discovery_session *discovery_session;
    char host_status[SDL3D_NETWORK_MAX_STATUS_LENGTH];
    char host_endpoint[SDL3D_NETWORK_MAX_STATUS_LENGTH];
    char direct_connect_host[SDL3D_NETWORK_MAX_HOST_LENGTH];
    char direct_connect_port[16];
    char direct_connect_status[SDL3D_NETWORK_MAX_STATUS_LENGTH];
    int paddle_hit_connection;
    int vibration_connection;
    int lobby_start_connection;
    int host_start_signal_id;
    int camera_toggle_signal_id;
    int ball_hit_signal_id;
    int vibration_signal_id;
    int discovery_refresh_signal_id;
    int discovery_refresh_connection;
    int local_input_gamepad_count;
    int discovery_selected_index;
} pong_state;

typedef enum pong_network_message_kind
{
    PONG_NETWORK_MESSAGE_INPUT = 1,
    PONG_NETWORK_MESSAGE_STATE = 2,
    PONG_NETWORK_MESSAGE_START_GAME = 3,
} pong_network_message_kind;

static const Uint32 PONG_NETWORK_PACKET_MAGIC = 0x474E4F50u; /* "PONG" little-endian */
static const Uint8 PONG_NETWORK_PACKET_VERSION = 1U;
static const size_t PONG_NETWORK_INPUT_PACKET_SIZE = 20U;
static const size_t PONG_NETWORK_STATE_PACKET_SIZE = 108U;

static Uint64 pong_log_timestamp_ms(void)
{
    return SDL_GetTicks();
}

static void pong_log_multiplayer_state(const char *prefix, const pong_state *state, Uint32 packet_tick,
                                       const char *extra)
{
    const sdl3d_properties *scene_state =
        state != NULL && state->data != NULL ? sdl3d_game_data_scene_state(state->data) : NULL;
    const sdl3d_registered_actor *player =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_actor(state->data, "entity.paddle.player") : NULL;
    const sdl3d_registered_actor *cpu =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_actor(state->data, "entity.paddle.cpu") : NULL;
    const sdl3d_registered_actor *ball =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_actor(state->data, "entity.ball") : NULL;
    const sdl3d_registered_actor *match =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_actor(state->data, "entity.match") : NULL;
    const sdl3d_registered_actor *score_player_actor =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_actor(state->data, "entity.score.player") : NULL;
    const sdl3d_registered_actor *score_cpu_actor =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_actor(state->data, "entity.score.cpu") : NULL;
    const sdl3d_vec3 player_position = player != NULL ? player->position : sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    const sdl3d_vec3 cpu_position = cpu != NULL ? cpu->position : sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    const sdl3d_vec3 ball_position = ball != NULL ? ball->position : sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    const sdl3d_vec3 ball_velocity =
        ball != NULL ? sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f))
                     : sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    const int score_player =
        score_player_actor != NULL ? sdl3d_properties_get_int(score_player_actor->props, "value", 0) : 0;
    const int score_cpu = score_cpu_actor != NULL ? sdl3d_properties_get_int(score_cpu_actor->props, "value", 0) : 0;
    const bool finished = match != NULL && sdl3d_properties_get_bool(match->props, "finished", false);
    const char *winner = match != NULL ? sdl3d_properties_get_string(match->props, "winner", "none") : "none";
    const char *match_mode =
        scene_state != NULL ? sdl3d_properties_get_string(scene_state, "match_mode", "none") : "none";
    const char *network_role =
        scene_state != NULL ? sdl3d_properties_get_string(scene_state, "network_role", "none") : "none";
    const char *network_flow =
        scene_state != NULL ? sdl3d_properties_get_string(scene_state, "network_flow", "none") : "none";

    SDL_LogInfo(
        SDL_LOG_CATEGORY_APPLICATION,
        "[%llu ms] Pong %s tick=%u scene=%s match_mode=%s network_role=%s network_flow=%s "
        "scores=%d:%d finished=%d winner=%s player=(%.2f,%.2f,%.2f) cpu=(%.2f,%.2f,%.2f) "
        "ball=(%.2f,%.2f,%.2f) vel=(%.2f,%.2f)%s%s",
        (unsigned long long)pong_log_timestamp_ms(), prefix != NULL ? prefix : "state", (unsigned int)packet_tick,
        state != NULL && state->data != NULL && sdl3d_game_data_active_scene(state->data) != NULL
            ? sdl3d_game_data_active_scene(state->data)
            : "<none>",
        match_mode != NULL ? match_mode : "none", network_role != NULL ? network_role : "none",
        network_flow != NULL ? network_flow : "none", score_player, score_cpu, finished ? 1 : 0,
        winner != NULL ? winner : "none", player_position.x, player_position.y, player_position.z, cpu_position.x,
        cpu_position.y, cpu_position.z, ball_position.x, ball_position.y, ball_position.z, ball_velocity.x,
        ball_velocity.y, extra != NULL && extra[0] != '\0' ? " " : "", extra != NULL ? extra : "");
}

static bool pong_network_write_u8(Uint8 **cursor, Uint8 *end, Uint8 value)
{
    if (cursor == NULL || *cursor == NULL || end == NULL || *cursor >= end)
    {
        return false;
    }

    **cursor = value;
    (*cursor)++;
    return true;
}

static bool pong_network_write_u32(Uint8 **cursor, Uint8 *end, Uint32 value)
{
    Uint8 bytes[4];
    bytes[0] = (Uint8)(value & 0xffU);
    bytes[1] = (Uint8)((value >> 8U) & 0xffU);
    bytes[2] = (Uint8)((value >> 16U) & 0xffU);
    bytes[3] = (Uint8)((value >> 24U) & 0xffU);
    if (cursor == NULL || *cursor == NULL || end == NULL || (size_t)(end - *cursor) < sizeof(bytes))
    {
        return false;
    }

    SDL_memcpy(*cursor, bytes, sizeof(bytes));
    *cursor += sizeof(bytes);
    return true;
}

static bool pong_network_write_i32(Uint8 **cursor, Uint8 *end, int value)
{
    return pong_network_write_u32(cursor, end, (Uint32)(Sint32)value);
}

static bool pong_network_write_f32(Uint8 **cursor, Uint8 *end, float value)
{
    Uint32 bits = 0U;
    SDL_memcpy(&bits, &value, sizeof(bits));
    return pong_network_write_u32(cursor, end, bits);
}

static bool pong_network_write_vec2(Uint8 **cursor, Uint8 *end, sdl3d_vec2 value)
{
    return pong_network_write_f32(cursor, end, value.x) && pong_network_write_f32(cursor, end, value.y);
}

static bool pong_network_write_vec3(Uint8 **cursor, Uint8 *end, sdl3d_vec3 value)
{
    return pong_network_write_f32(cursor, end, value.x) && pong_network_write_f32(cursor, end, value.y) &&
           pong_network_write_f32(cursor, end, value.z);
}

static bool pong_network_read_u8(const Uint8 **cursor, const Uint8 *end, Uint8 *out_value)
{
    if (cursor == NULL || *cursor == NULL || out_value == NULL || *cursor >= end)
    {
        return false;
    }

    *out_value = **cursor;
    (*cursor)++;
    return true;
}

static bool pong_network_read_u32(const Uint8 **cursor, const Uint8 *end, Uint32 *out_value)
{
    Uint8 bytes[4];
    if (cursor == NULL || *cursor == NULL || out_value == NULL || (size_t)(end - *cursor) < sizeof(bytes))
    {
        return false;
    }

    SDL_memcpy(bytes, *cursor, sizeof(bytes));
    *cursor += sizeof(bytes);
    *out_value = (Uint32)bytes[0] | ((Uint32)bytes[1] << 8U) | ((Uint32)bytes[2] << 16U) | ((Uint32)bytes[3] << 24U);
    return true;
}

static bool pong_network_read_i32(const Uint8 **cursor, const Uint8 *end, int *out_value)
{
    Uint32 value = 0U;
    if (!pong_network_read_u32(cursor, end, &value) || out_value == NULL)
    {
        return false;
    }

    *out_value = (int)(Sint32)value;
    return true;
}

static bool pong_network_read_f32(const Uint8 **cursor, const Uint8 *end, float *out_value)
{
    Uint32 bits = 0U;
    if (!pong_network_read_u32(cursor, end, &bits) || out_value == NULL)
    {
        return false;
    }

    SDL_memcpy(out_value, &bits, sizeof(bits));
    return true;
}

static bool pong_network_read_vec2(const Uint8 **cursor, const Uint8 *end, sdl3d_vec2 *out_value)
{
    return out_value != NULL && pong_network_read_f32(cursor, end, &out_value->x) &&
           pong_network_read_f32(cursor, end, &out_value->y);
}

static bool pong_network_read_vec3(const Uint8 **cursor, const Uint8 *end, sdl3d_vec3 *out_value)
{
    return out_value != NULL && pong_network_read_f32(cursor, end, &out_value->x) &&
           pong_network_read_f32(cursor, end, &out_value->y) && pong_network_read_f32(cursor, end, &out_value->z);
}

static const char *active_scene_name(const pong_state *state)
{
    return state != NULL && state->data != NULL ? sdl3d_game_data_active_scene(state->data) : NULL;
}

static bool is_direct_connect_scene(const pong_state *state)
{
    const char *active_scene = active_scene_name(state);
    return active_scene != NULL && SDL_strcmp(active_scene, "scene.multiplayer.direct_connect") == 0;
}

static bool is_multiplayer_lobby_scene(const pong_state *state)
{
    const char *active_scene = active_scene_name(state);
    return active_scene != NULL && SDL_strcmp(active_scene, "scene.multiplayer.lobby") == 0;
}

static bool is_multiplayer_discovery_scene(const pong_state *state)
{
    const char *active_scene = active_scene_name(state);
    return active_scene != NULL && SDL_strcmp(active_scene, "scene.multiplayer.discovery") == 0;
}

static bool is_multiplayer_play_scene(const pong_state *state)
{
    const char *active_scene = active_scene_name(state);
    return active_scene != NULL && SDL_strcmp(active_scene, "scene.play") == 0;
}

static bool is_local_match_mode(const pong_state *state)
{
    const sdl3d_properties *scene_state =
        state != NULL && state->data != NULL ? sdl3d_game_data_scene_state(state->data) : NULL;
    const char *match_mode = scene_state != NULL ? sdl3d_properties_get_string(scene_state, "match_mode", NULL) : NULL;
    return match_mode != NULL && SDL_strcmp(match_mode, "local") == 0;
}

static bool is_network_flow_host(const pong_state *state)
{
    const sdl3d_properties *scene_state =
        state != NULL && state->data != NULL ? sdl3d_game_data_scene_state(state->data) : NULL;
    const char *network_flow =
        scene_state != NULL ? sdl3d_properties_get_string(scene_state, "network_flow", NULL) : NULL;
    return network_flow != NULL && SDL_strcmp(network_flow, "host") == 0;
}

static bool is_network_flow_direct(const pong_state *state)
{
    const sdl3d_properties *scene_state =
        state != NULL && state->data != NULL ? sdl3d_game_data_scene_state(state->data) : NULL;
    const char *network_flow =
        scene_state != NULL ? sdl3d_properties_get_string(scene_state, "network_flow", NULL) : NULL;
    return network_flow != NULL && SDL_strcmp(network_flow, "direct") == 0;
}

static const char *network_role_name(const pong_state *state)
{
    const sdl3d_properties *scene_state =
        state != NULL && state->data != NULL ? sdl3d_game_data_scene_state(state->data) : NULL;
    return scene_state != NULL ? sdl3d_properties_get_string(scene_state, "network_role", "none") : "none";
}

static bool is_network_role_host(const pong_state *state)
{
    return SDL_strcmp(network_role_name(state), "host") == 0;
}

static bool is_network_role_client(const pong_state *state)
{
    return SDL_strcmp(network_role_name(state), "client") == 0;
}

static bool pong_network_packet_is_kind(const Uint8 *packet, int packet_size, pong_network_message_kind kind)
{
    return packet != NULL && packet_size >= 6 && packet[0] == 'P' && packet[1] == 'O' && packet[2] == 'N' &&
           packet[3] == 'G' && packet[4] == PONG_NETWORK_PACKET_VERSION && packet[5] == (Uint8)kind;
}

static const char *scene_payload_string(const sdl3d_properties *payload, const char *key)
{
    return payload != NULL ? sdl3d_properties_get_string(payload, key, NULL) : NULL;
}

static const char *scene_context_string(const sdl3d_properties *payload, const sdl3d_properties *scene_state,
                                        const char *key, const char *fallback)
{
    const char *value = scene_payload_string(payload, key);
    if (value != NULL)
    {
        return value;
    }
    return scene_state != NULL ? sdl3d_properties_get_string(scene_state, key, fallback) : fallback;
}

static bool enter_multiplayer_play_scene(pong_state *state, const char *match_mode, const char *network_role,
                                         const char *network_flow)
{
    sdl3d_properties *payload = NULL;
    bool ok = false;

    if (state == NULL || state->data == NULL)
    {
        return false;
    }

    payload = sdl3d_properties_create();
    if (payload == NULL)
    {
        return false;
    }

    if (match_mode != NULL)
    {
        sdl3d_properties_set_string(payload, "match_mode", match_mode);
    }
    if (network_role != NULL)
    {
        sdl3d_properties_set_string(payload, "network_role", network_role);
    }
    if (network_flow != NULL)
    {
        sdl3d_properties_set_string(payload, "network_flow", network_flow);
    }

    ok = sdl3d_game_data_set_active_scene_with_payload(state->data, "scene.play", payload);
    sdl3d_properties_destroy(payload);
    return ok;
}

static void clear_network_action_overrides(pong_state *state)
{
    if (state == NULL || state->input == NULL || state->data == NULL)
    {
        return;
    }

    const int p1_up = sdl3d_game_data_find_action(state->data, "action.paddle.up");
    const int p1_down = sdl3d_game_data_find_action(state->data, "action.paddle.down");
    const int p2_up = sdl3d_game_data_find_action(state->data, "action.paddle.local.up");
    const int p2_down = sdl3d_game_data_find_action(state->data, "action.paddle.local.down");
    if (p1_up >= 0)
    {
        sdl3d_input_clear_action_override(state->input, p1_up);
    }
    if (p1_down >= 0)
    {
        sdl3d_input_clear_action_override(state->input, p1_down);
    }
    if (p2_up >= 0)
    {
        sdl3d_input_clear_action_override(state->input, p2_up);
    }
    if (p2_down >= 0)
    {
        sdl3d_input_clear_action_override(state->input, p2_down);
    }
}

static void set_network_action_override(pong_state *state, const char *action_name, float value)
{
    const int action_id =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_action(state->data, action_name) : -1;
    if (state == NULL || state->input == NULL || action_id < 0)
    {
        return;
    }

    sdl3d_input_set_action_override(state->input, action_id, value);
}

static void update_host_session_scene_state(pong_state *state)
{
    if (state == NULL || state->data == NULL)
    {
        return;
    }

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(state->data);
    if (scene_state == NULL)
    {
        return;
    }

    const char *status =
        state->host_session != NULL ? sdl3d_network_session_status(state->host_session) : state->host_status;
    const Uint16 port =
        state->host_session != NULL ? sdl3d_network_session_port(state->host_session) : SDL3D_NETWORK_DEFAULT_PORT;
    char client_label[SDL3D_NETWORK_MAX_HOST_LENGTH + 48];
    char peer_host[SDL3D_NETWORK_MAX_HOST_LENGTH];
    Uint16 peer_port = 0;
    const bool client_connected =
        state->host_session != NULL && sdl3d_network_session_is_connected(state->host_session);
    SDL_snprintf(state->host_status, sizeof(state->host_status), "%s",
                 status != NULL && status[0] != '\0' ? status : "Not hosting");
    SDL_snprintf(state->host_endpoint, sizeof(state->host_endpoint), "UDP %u", (unsigned int)port);
    SDL_snprintf(client_label, sizeof(client_label), "Waiting for client");
    SDL_zero(peer_host);
    if (client_connected &&
        sdl3d_network_session_get_peer_endpoint(state->host_session, peer_host, (int)sizeof(peer_host), &peer_port))
    {
        SDL_snprintf(client_label, sizeof(client_label), "Client 1 - %s:%u", peer_host, (unsigned int)peer_port);
    }
    else if (client_connected)
    {
        SDL_snprintf(client_label, sizeof(client_label), "Client 1 - Connected");
    }

    sdl3d_properties_set_string(scene_state, "multiplayer_host_status", state->host_status);
    sdl3d_properties_set_string(scene_state, "multiplayer_host_endpoint", state->host_endpoint);
    sdl3d_properties_set_string(scene_state, "multiplayer_host_client", client_label);
    sdl3d_properties_set_bool(scene_state, "multiplayer_host_connected", client_connected);
}

static void destroy_host_session(pong_state *state)
{
    if (state != NULL && state->host_session != NULL)
    {
        sdl3d_network_session_destroy(state->host_session);
        state->host_session = NULL;
    }

    if (state != NULL)
    {
        SDL_snprintf(state->host_status, sizeof(state->host_status), "Not hosting");
        SDL_snprintf(state->host_endpoint, sizeof(state->host_endpoint), "UDP %u",
                     (unsigned int)SDL3D_NETWORK_DEFAULT_PORT);
        update_host_session_scene_state(state);
    }
}

static void bind_local_play_controls(pong_state *state)
{
    if (state == NULL || state->data == NULL || state->input == NULL)
    {
        return;
    }

    const int gamepad_count = sdl3d_input_gamepad_count(state->input);
    const int p1_up = sdl3d_game_data_find_action(state->data, "action.paddle.up");
    const int p1_down = sdl3d_game_data_find_action(state->data, "action.paddle.down");
    const int p2_up = sdl3d_game_data_find_action(state->data, "action.paddle.local.up");
    const int p2_down = sdl3d_game_data_find_action(state->data, "action.paddle.local.down");

    if (p1_up < 0 || p1_down < 0 || p2_up < 0 || p2_down < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Pong local input rebinding skipped: required paddle actions were not registered");
        return;
    }

    sdl3d_input_unbind_action(state->input, p1_up);
    sdl3d_input_unbind_action(state->input, p1_down);
    sdl3d_input_unbind_action(state->input, p2_up);
    sdl3d_input_unbind_action(state->input, p2_down);

    sdl3d_input_bind_key(state->input, p1_up, SDL_SCANCODE_UP);
    sdl3d_input_bind_key(state->input, p1_down, SDL_SCANCODE_DOWN);

    if (gamepad_count >= 2)
    {
        sdl3d_input_bind_gamepad_button_at(state->input, p1_up, 0, SDL_GAMEPAD_BUTTON_DPAD_UP);
        sdl3d_input_bind_gamepad_button_at(state->input, p1_down, 0, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        sdl3d_input_bind_gamepad_axis_at(state->input, p1_up, 0, SDL_GAMEPAD_AXIS_LEFTY, -1.0f);
        sdl3d_input_bind_gamepad_axis_at(state->input, p1_down, 0, SDL_GAMEPAD_AXIS_LEFTY, 1.0f);

        sdl3d_input_bind_gamepad_button_at(state->input, p2_up, 1, SDL_GAMEPAD_BUTTON_DPAD_UP);
        sdl3d_input_bind_gamepad_button_at(state->input, p2_down, 1, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        sdl3d_input_bind_gamepad_axis_at(state->input, p2_up, 1, SDL_GAMEPAD_AXIS_LEFTY, -1.0f);
        sdl3d_input_bind_gamepad_axis_at(state->input, p2_down, 1, SDL_GAMEPAD_AXIS_LEFTY, 1.0f);
    }
    else if (gamepad_count == 1)
    {
        sdl3d_input_bind_gamepad_button_at(state->input, p2_up, 0, SDL_GAMEPAD_BUTTON_DPAD_UP);
        sdl3d_input_bind_gamepad_button_at(state->input, p2_down, 0, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        sdl3d_input_bind_gamepad_axis_at(state->input, p2_up, 0, SDL_GAMEPAD_AXIS_LEFTY, -1.0f);
        sdl3d_input_bind_gamepad_axis_at(state->input, p2_down, 0, SDL_GAMEPAD_AXIS_LEFTY, 1.0f);
    }

    state->local_input_gamepad_count = gamepad_count;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong local input configured: gamepads=%d", gamepad_count);
}

static void bind_network_play_controls(pong_state *state, const char *up_action_name, const char *down_action_name)
{
    if (state == NULL || state->data == NULL || state->input == NULL)
    {
        return;
    }

    const int up_action = sdl3d_game_data_find_action(state->data, up_action_name);
    const int down_action = sdl3d_game_data_find_action(state->data, down_action_name);
    if (up_action < 0 || down_action < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Pong network input rebinding skipped: required paddle actions were not registered");
        return;
    }

    sdl3d_input_unbind_action(state->input, up_action);
    sdl3d_input_unbind_action(state->input, down_action);
    sdl3d_input_bind_key(state->input, up_action, SDL_SCANCODE_UP);
    sdl3d_input_bind_key(state->input, down_action, SDL_SCANCODE_DOWN);

    if (sdl3d_input_gamepad_count(state->input) > 0)
    {
        sdl3d_input_bind_gamepad_button_at(state->input, up_action, 0, SDL_GAMEPAD_BUTTON_DPAD_UP);
        sdl3d_input_bind_gamepad_button_at(state->input, down_action, 0, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        sdl3d_input_bind_gamepad_axis_at(state->input, up_action, 0, SDL_GAMEPAD_AXIS_LEFTY, -1.0f);
        sdl3d_input_bind_gamepad_axis_at(state->input, down_action, 0, SDL_GAMEPAD_AXIS_LEFTY, 1.0f);
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong network input configured: role=%s actions=%s/%s gamepads=%d",
                network_role_name(state), up_action_name, down_action_name, sdl3d_input_gamepad_count(state->input));
}

static bool configure_play_input(void *userdata, sdl3d_game_data_runtime *runtime, const char *adapter_name,
                                 sdl3d_registered_actor *target, const sdl3d_properties *payload)
{
    pong_state *state = (pong_state *)userdata;
    (void)runtime;
    (void)adapter_name;
    (void)target;

    if (state == NULL || state->data == NULL || state->input == NULL)
    {
        return false;
    }

    const sdl3d_properties *scene_state = sdl3d_game_data_scene_state(state->data);
    sdl3d_properties *mutable_scene_state = sdl3d_game_data_mutable_scene_state(state->data);
    const char *match_mode = scene_context_string(payload, scene_state, "match_mode", "single");
    const char *network_role = scene_context_string(payload, scene_state, "network_role", "none");
    const char *network_flow = scene_context_string(payload, scene_state, "network_flow", "none");

    clear_network_action_overrides(state);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong play input context: match_mode=%s network_role=%s network_flow=%s",
                match_mode != NULL ? match_mode : "none", network_role != NULL ? network_role : "none",
                network_flow != NULL ? network_flow : "none");

    if (mutable_scene_state != NULL)
    {
        sdl3d_properties_set_string(mutable_scene_state, "match_mode", match_mode != NULL ? match_mode : "single");
        sdl3d_properties_set_string(mutable_scene_state, "network_role", network_role != NULL ? network_role : "none");
        sdl3d_properties_set_string(mutable_scene_state, "network_flow", network_flow != NULL ? network_flow : "none");
    }

    if (SDL_strcmp(match_mode != NULL ? match_mode : "single", "local") == 0)
    {
        bind_local_play_controls(state);
    }
    else if (SDL_strcmp(network_role != NULL ? network_role : "none", "host") == 0)
    {
        bind_network_play_controls(state, "action.paddle.up", "action.paddle.down");
        const int p2_up = sdl3d_game_data_find_action(state->data, "action.paddle.local.up");
        const int p2_down = sdl3d_game_data_find_action(state->data, "action.paddle.local.down");
        if (p2_up >= 0)
        {
            sdl3d_input_unbind_action(state->input, p2_up);
        }
        if (p2_down >= 0)
        {
            sdl3d_input_unbind_action(state->input, p2_down);
        }
    }
    else if (SDL_strcmp(network_role != NULL ? network_role : "none", "client") == 0)
    {
        const int p1_up = sdl3d_game_data_find_action(state->data, "action.paddle.up");
        const int p1_down = sdl3d_game_data_find_action(state->data, "action.paddle.down");
        if (p1_up >= 0)
        {
            sdl3d_input_unbind_action(state->input, p1_up);
        }
        if (p1_down >= 0)
        {
            sdl3d_input_unbind_action(state->input, p1_down);
        }

        bind_network_play_controls(state, "action.paddle.local.up", "action.paddle.local.down");
    }
    else
    {
        const int p1_up = sdl3d_game_data_find_action(state->data, "action.paddle.up");
        const int p1_down = sdl3d_game_data_find_action(state->data, "action.paddle.down");
        if (p1_up < 0 || p1_down < 0)
        {
            return false;
        }

        sdl3d_input_unbind_action(state->input, p1_up);
        sdl3d_input_unbind_action(state->input, p1_down);
        sdl3d_input_bind_key(state->input, p1_up, SDL_SCANCODE_UP);
        sdl3d_input_bind_key(state->input, p1_down, SDL_SCANCODE_DOWN);
        sdl3d_input_bind_gamepad_button(state->input, p1_up, SDL_GAMEPAD_BUTTON_DPAD_UP);
        sdl3d_input_bind_gamepad_button(state->input, p1_down, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        sdl3d_input_bind_gamepad_axis(state->input, p1_up, SDL_GAMEPAD_AXIS_LEFTY, -1.0f);
        sdl3d_input_bind_gamepad_axis(state->input, p1_down, SDL_GAMEPAD_AXIS_LEFTY, 1.0f);

        const int p2_up = sdl3d_game_data_find_action(state->data, "action.paddle.local.up");
        const int p2_down = sdl3d_game_data_find_action(state->data, "action.paddle.local.down");
        if (p2_up >= 0)
            sdl3d_input_unbind_action(state->input, p2_up);
        if (p2_down >= 0)
            sdl3d_input_unbind_action(state->input, p2_down);
        state->local_input_gamepad_count = sdl3d_input_gamepad_count(state->input);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong single-player input configured: gamepads=%d",
                    state->local_input_gamepad_count);
    }

    return true;
}

static void destroy_direct_connect_session(pong_state *state)
{
    if (state != NULL && state->direct_connect_session != NULL)
    {
        sdl3d_network_session_destroy(state->direct_connect_session);
        state->direct_connect_session = NULL;
    }
}

static bool start_host_session(pong_state *state)
{
    sdl3d_network_session_desc desc;

    if (state == NULL)
    {
        return false;
    }

    if (state->host_session != NULL)
    {
        return true;
    }

    sdl3d_network_session_desc_init(&desc);
    desc.role = SDL3D_NETWORK_ROLE_HOST;
    desc.host = NULL;
    desc.port = SDL3D_NETWORK_DEFAULT_PORT;
    desc.local_port = 0;
    desc.handshake_timeout = 5.0f;
    desc.idle_timeout = 10.0f;
    desc.session_name = "Pong";

    if (!sdl3d_network_session_create(&desc, &state->host_session))
    {
        SDL_snprintf(state->host_status, sizeof(state->host_status), "%s", SDL_GetError());
        SDL_snprintf(state->host_endpoint, sizeof(state->host_endpoint), "UDP %u",
                     (unsigned int)SDL3D_NETWORK_DEFAULT_PORT);
        update_host_session_scene_state(state);
        return false;
    }

    update_host_session_scene_state(state);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong host lobby session active: %s", state->host_endpoint);
    return true;
}

static void reset_direct_connect_defaults(pong_state *state)
{
    if (state == NULL)
    {
        return;
    }

    SDL_strlcpy(state->direct_connect_host, "127.0.0.1", sizeof(state->direct_connect_host));
    SDL_snprintf(state->direct_connect_port, sizeof(state->direct_connect_port), "%u",
                 (unsigned int)SDL3D_NETWORK_DEFAULT_PORT);
    SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "Ready to connect");
}

static bool send_host_start_packet(pong_state *state)
{
    Uint8 command = (Uint8)PONG_NETWORK_MESSAGE_START_GAME;

    if (state == NULL)
    {
        return false;
    }

    if (state->host_session == NULL || !sdl3d_network_session_is_connected(state->host_session))
    {
        SDL_snprintf(state->host_status, sizeof(state->host_status), "Waiting for client");
        update_host_session_scene_state(state);
        return false;
    }

    if (!sdl3d_network_session_send(state->host_session, &command, 1))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host start packet send failed: %s", SDL_GetError());
        SDL_snprintf(state->host_status, sizeof(state->host_status), "%s", SDL_GetError());
        update_host_session_scene_state(state);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong host requested multiplayer start");
    return true;
}

static bool start_selected_lobby_match(pong_state *state)
{
    if (state == NULL)
    {
        return false;
    }

    if (state->host_session == NULL || !sdl3d_network_session_is_connected(state->host_session))
    {
        SDL_snprintf(state->host_status, sizeof(state->host_status), "Waiting for client");
        update_host_session_scene_state(state);
        return false;
    }

    if (!send_host_start_packet(state))
    {
        return false;
    }

    clear_network_action_overrides(state);
    if (!enter_multiplayer_play_scene(state, "lan", "host", "host"))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host failed to enter multiplayer play scene: %s",
                    SDL_GetError());
        return false;
    }

    return true;
}

static bool send_client_input_packet(pong_state *state)
{
    Uint8 packet[32];
    Uint8 *cursor = packet;
    Uint8 *end = packet + sizeof(packet);
    const int p2_up = sdl3d_game_data_find_action(state != NULL ? state->data : NULL, "action.paddle.local.up");
    const int p2_down = sdl3d_game_data_find_action(state != NULL ? state->data : NULL, "action.paddle.local.down");

    if (state == NULL || state->direct_connect_session == NULL || state->input == NULL || p2_up < 0 || p2_down < 0 ||
        !sdl3d_network_session_is_connected(state->direct_connect_session))
    {
        return false;
    }

    const sdl3d_input_snapshot *snapshot = sdl3d_input_get_snapshot(state->input);
    const float up_value = snapshot != NULL ? snapshot->actions[p2_up].value : 0.0f;
    const float down_value = snapshot != NULL ? snapshot->actions[p2_down].value : 0.0f;

    if (!pong_network_write_u32(&cursor, end, PONG_NETWORK_PACKET_MAGIC) ||
        !pong_network_write_u8(&cursor, end, PONG_NETWORK_PACKET_VERSION) ||
        !pong_network_write_u8(&cursor, end, (Uint8)PONG_NETWORK_MESSAGE_INPUT) ||
        !pong_network_write_u8(&cursor, end, 0U) || !pong_network_write_u8(&cursor, end, 0U) ||
        !pong_network_write_u32(&cursor, end, snapshot != NULL ? (Uint32)SDL_max(snapshot->tick, 0) : 0U) ||
        !pong_network_write_f32(&cursor, end, up_value) || !pong_network_write_f32(&cursor, end, down_value))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong client input packet encode failed");
        return false;
    }

    if ((size_t)(cursor - packet) != PONG_NETWORK_INPUT_PACKET_SIZE)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong client input packet size mismatch: expected=%zu actual=%zu",
                    PONG_NETWORK_INPUT_PACKET_SIZE, (size_t)(cursor - packet));
        return false;
    }

    if (!sdl3d_network_session_send(state->direct_connect_session, packet, (int)(cursor - packet)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong client input packet send failed: %s", SDL_GetError());
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong client->host input tick=%u up=%.3f down=%.3f scene=%s",
                (unsigned long long)pong_log_timestamp_ms(),
                snapshot != NULL ? (unsigned int)SDL_max(snapshot->tick, 0) : 0U, up_value, down_value,
                state != NULL && state->data != NULL && sdl3d_game_data_active_scene(state->data) != NULL
                    ? sdl3d_game_data_active_scene(state->data)
                    : "<none>");
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[%llu ms] Pong client input packet queued: role=%s flow=%s p2_up=%.3f p2_down=%.3f",
                (unsigned long long)pong_log_timestamp_ms(), network_role_name(state),
                state != NULL && state->data != NULL && sdl3d_game_data_scene_state(state->data) != NULL
                    ? sdl3d_properties_get_string(sdl3d_game_data_scene_state(state->data), "network_flow", "none")
                    : "none",
                up_value, down_value);
    return true;
}

static bool apply_play_state_snapshot(pong_state *state, Uint32 packet_tick, const Uint8 **cursor, const Uint8 *end)
{
    const Uint8 *packet_begin = cursor != NULL ? *cursor : NULL;
    sdl3d_registered_actor *player = NULL;
    sdl3d_registered_actor *cpu = NULL;
    sdl3d_registered_actor *ball = NULL;
    sdl3d_registered_actor *match = NULL;
    sdl3d_registered_actor *presentation = NULL;
    sdl3d_registered_actor *score_player_actor = NULL;
    sdl3d_registered_actor *score_cpu_actor = NULL;
    sdl3d_vec3 player_position;
    sdl3d_vec3 cpu_position;
    sdl3d_vec3 ball_position;
    sdl3d_vec3 ball_velocity;
    float border_flash = 0.0f;
    float paddle_flash = 0.0f;
    float last_reflect_y = 0.0f;
    int score_player = 0;
    int score_cpu = 0;
    int finished = 0;
    int winner = 0;
    int active_motion = 0;
    int has_last_reflect_y = 0;
    int stagnant_reflect_count = 0;

    if (state == NULL || state->data == NULL || cursor == NULL || *cursor == NULL)
    {
        return false;
    }

    player = sdl3d_game_data_find_actor(state->data, "entity.paddle.player");
    cpu = sdl3d_game_data_find_actor(state->data, "entity.paddle.cpu");
    ball = sdl3d_game_data_find_actor(state->data, "entity.ball");
    match = sdl3d_game_data_find_actor(state->data, "entity.match");
    presentation = sdl3d_game_data_find_actor(state->data, "entity.presentation");
    score_player_actor = sdl3d_game_data_find_actor(state->data, "entity.score.player");
    score_cpu_actor = sdl3d_game_data_find_actor(state->data, "entity.score.cpu");
    if (player == NULL || cpu == NULL || ball == NULL || match == NULL || presentation == NULL ||
        score_player_actor == NULL || score_cpu_actor == NULL ||
        !pong_network_read_vec3(cursor, end, &player_position) || !pong_network_read_vec3(cursor, end, &cpu_position) ||
        !pong_network_read_vec3(cursor, end, &ball_position) || !pong_network_read_vec3(cursor, end, &ball_velocity) ||
        !pong_network_read_f32(cursor, end, &border_flash) || !pong_network_read_f32(cursor, end, &paddle_flash) ||
        !pong_network_read_i32(cursor, end, &score_player) || !pong_network_read_i32(cursor, end, &score_cpu) ||
        !pong_network_read_i32(cursor, end, &finished) || !pong_network_read_i32(cursor, end, &winner) ||
        !pong_network_read_i32(cursor, end, &active_motion) ||
        !pong_network_read_i32(cursor, end, &has_last_reflect_y) ||
        !pong_network_read_f32(cursor, end, &last_reflect_y) ||
        !pong_network_read_i32(cursor, end, &stagnant_reflect_count))
    {
        return false;
    }

    if (packet_begin == NULL || *cursor != end)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong client state packet size mismatch: expected=%zu actual=%zu",
                    PONG_NETWORK_STATE_PACKET_SIZE, packet_begin != NULL ? (size_t)(end - packet_begin) : 0U);
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
    sdl3d_properties_set_bool(match->props, "finished", finished != 0);
    sdl3d_properties_set_string(match->props, "winner", winner == 1 ? "player" : winner == 2 ? "cpu" : "none");
    sdl3d_properties_set_float(presentation->props, "border_flash", border_flash);
    sdl3d_properties_set_float(presentation->props, "paddle_flash", paddle_flash);
    pong_log_multiplayer_state("client<-host state", state, packet_tick, "applied");
    return true;
}

static bool process_host_input_packet(pong_state *state, const Uint8 *packet, int packet_size)
{
    const Uint8 *cursor = packet;
    const Uint8 *end = packet + packet_size;
    Uint32 magic = 0U;
    Uint8 version = 0U;
    Uint8 kind = 0U;
    Uint8 reserved = 0U;
    Uint32 tick = 0U;
    float up_value = 0.0f;
    float down_value = 0.0f;

    if (state == NULL || packet == NULL || packet_size < 12)
    {
        return false;
    }

    if (!pong_network_read_u32(&cursor, end, &magic) || magic != PONG_NETWORK_PACKET_MAGIC ||
        !pong_network_read_u8(&cursor, end, &version) || version != PONG_NETWORK_PACKET_VERSION ||
        !pong_network_read_u8(&cursor, end, &kind) || kind != (Uint8)PONG_NETWORK_MESSAGE_INPUT ||
        !pong_network_read_u8(&cursor, end, &reserved) || !pong_network_read_u8(&cursor, end, &reserved) ||
        !pong_network_read_u32(&cursor, end, &tick) || !pong_network_read_f32(&cursor, end, &up_value) ||
        !pong_network_read_f32(&cursor, end, &down_value))
    {
        return false;
    }

    if ((size_t)packet_size != PONG_NETWORK_INPUT_PACKET_SIZE)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host input packet size mismatch: expected=%zu actual=%d",
                    PONG_NETWORK_INPUT_PACKET_SIZE, packet_size);
        return false;
    }

    (void)tick;
    set_network_action_override(state, "action.paddle.local.up", up_value);
    set_network_action_override(state, "action.paddle.local.down", down_value);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong host<-client input tick=%u up=%.3f down=%.3f scene=%s",
                (unsigned long long)pong_log_timestamp_ms(), (unsigned int)tick, up_value, down_value,
                state != NULL && state->data != NULL && sdl3d_game_data_active_scene(state->data) != NULL
                    ? sdl3d_game_data_active_scene(state->data)
                    : "<none>");
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[%llu ms] Pong host applied remote paddle input: p2_up=%.3f p2_down=%.3f",
                (unsigned long long)pong_log_timestamp_ms(), up_value, down_value);
    return true;
}

static bool process_client_state_packet(pong_state *state, const Uint8 *packet, int packet_size)
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
    bool ok = false;

    if (state == NULL || packet == NULL || packet_size < 12)
    {
        return false;
    }

    if (!pong_network_read_u32(&cursor, end, &magic) || magic != PONG_NETWORK_PACKET_MAGIC ||
        !pong_network_read_u8(&cursor, end, &version) || version != PONG_NETWORK_PACKET_VERSION ||
        !pong_network_read_u8(&cursor, end, &kind) || kind != (Uint8)PONG_NETWORK_MESSAGE_STATE ||
        !pong_network_read_u8(&cursor, end, &reserved) || !pong_network_read_u8(&cursor, end, &reserved) ||
        !pong_network_read_u32(&cursor, end, &tick) || !pong_network_read_f32(&cursor, end, &p1_up) ||
        !pong_network_read_f32(&cursor, end, &p1_down))
    {
        return false;
    }

    if ((size_t)packet_size != PONG_NETWORK_STATE_PACKET_SIZE)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong client state packet size mismatch: expected=%zu actual=%d",
                    PONG_NETWORK_STATE_PACKET_SIZE, packet_size);
        return false;
    }

    set_network_action_override(state, "action.paddle.up", p1_up);
    set_network_action_override(state, "action.paddle.down", p1_down);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[%llu ms] Pong client received state packet tick=%u p1_up=%.3f p1_down=%.3f",
                (unsigned long long)pong_log_timestamp_ms(), (unsigned int)tick, p1_up, p1_down);
    ok = apply_play_state_snapshot(state, tick, &cursor, end);
    return ok;
}

static bool send_host_state_packet(pong_state *state)
{
    Uint8 packet[160];
    Uint8 *cursor = packet;
    Uint8 *end = packet + sizeof(packet);
    const sdl3d_registered_actor *player = NULL;
    const sdl3d_registered_actor *cpu = NULL;
    const sdl3d_registered_actor *ball = NULL;
    const sdl3d_registered_actor *match = NULL;
    const sdl3d_registered_actor *presentation = NULL;
    const sdl3d_registered_actor *score_player_actor = NULL;
    const sdl3d_registered_actor *score_cpu_actor = NULL;
    const sdl3d_input_snapshot *snapshot = NULL;
    const int p1_up_action =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_action(state->data, "action.paddle.up") : -1;
    const int p1_down_action =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_action(state->data, "action.paddle.down") : -1;
    const float p1_up = state != NULL && state->input != NULL && p1_up_action >= 0
                            ? sdl3d_input_get_value(state->input, p1_up_action)
                            : 0.0f;
    const float p1_down = state != NULL && state->input != NULL && p1_down_action >= 0
                              ? sdl3d_input_get_value(state->input, p1_down_action)
                              : 0.0f;

    if (state == NULL || state->host_session == NULL || !sdl3d_network_session_is_connected(state->host_session) ||
        state->data == NULL || state->input == NULL)
    {
        return false;
    }

    player = sdl3d_game_data_find_actor(state->data, "entity.paddle.player");
    cpu = sdl3d_game_data_find_actor(state->data, "entity.paddle.cpu");
    ball = sdl3d_game_data_find_actor(state->data, "entity.ball");
    match = sdl3d_game_data_find_actor(state->data, "entity.match");
    presentation = sdl3d_game_data_find_actor(state->data, "entity.presentation");
    score_player_actor = sdl3d_game_data_find_actor(state->data, "entity.score.player");
    score_cpu_actor = sdl3d_game_data_find_actor(state->data, "entity.score.cpu");
    snapshot = sdl3d_input_get_snapshot(state->input);
    if (player == NULL || cpu == NULL || ball == NULL || match == NULL || presentation == NULL ||
        score_player_actor == NULL || score_cpu_actor == NULL || snapshot == NULL ||
        !pong_network_write_u32(&cursor, end, PONG_NETWORK_PACKET_MAGIC) ||
        !pong_network_write_u8(&cursor, end, PONG_NETWORK_PACKET_VERSION) ||
        !pong_network_write_u8(&cursor, end, (Uint8)PONG_NETWORK_MESSAGE_STATE) ||
        !pong_network_write_u8(&cursor, end, 0U) || !pong_network_write_u8(&cursor, end, 0U) ||
        !pong_network_write_u32(&cursor, end, (Uint32)SDL_max(snapshot->tick, 0)) ||
        !pong_network_write_f32(&cursor, end, p1_up) || !pong_network_write_f32(&cursor, end, p1_down) ||
        !pong_network_write_vec3(&cursor, end, player->position) ||
        !pong_network_write_vec3(&cursor, end, cpu->position) ||
        !pong_network_write_vec3(&cursor, end, ball->position) ||
        !pong_network_write_vec3(
            &cursor, end, sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f))) ||
        !pong_network_write_f32(&cursor, end, sdl3d_properties_get_float(presentation->props, "border_flash", 0.0f)) ||
        !pong_network_write_f32(&cursor, end, sdl3d_properties_get_float(presentation->props, "paddle_flash", 0.0f)) ||
        !pong_network_write_i32(&cursor, end, sdl3d_properties_get_int(score_player_actor->props, "value", 0)) ||
        !pong_network_write_i32(&cursor, end, sdl3d_properties_get_int(score_cpu_actor->props, "value", 0)) ||
        !pong_network_write_i32(&cursor, end, sdl3d_properties_get_bool(match->props, "finished", false) ? 1 : 0) ||
        !pong_network_write_i32(
            &cursor, end,
            SDL_strcmp(sdl3d_properties_get_string(match->props, "winner", "none"), "player") == 0 ? 1
            : SDL_strcmp(sdl3d_properties_get_string(match->props, "winner", "none"), "cpu") == 0  ? 2
                                                                                                   : 0) ||
        !pong_network_write_i32(&cursor, end, sdl3d_properties_get_bool(ball->props, "active_motion", false) ? 1 : 0) ||
        !pong_network_write_i32(&cursor, end,
                                sdl3d_properties_get_bool(ball->props, "has_last_reflect_y", false) ? 1 : 0) ||
        !pong_network_write_f32(&cursor, end, sdl3d_properties_get_float(ball->props, "last_reflect_y", 0.0f)) ||
        !pong_network_write_i32(&cursor, end, sdl3d_properties_get_int(ball->props, "stagnant_reflect_count", 0)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host state packet encode failed");
        return false;
    }

    if ((size_t)(cursor - packet) != PONG_NETWORK_STATE_PACKET_SIZE)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host state packet size mismatch: expected=%zu actual=%zu",
                    PONG_NETWORK_STATE_PACKET_SIZE, (size_t)(cursor - packet));
        return false;
    }

    if (!sdl3d_network_session_send(state->host_session, packet, (int)(cursor - packet)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host state packet send failed: %s", SDL_GetError());
        return false;
    }

    pong_log_multiplayer_state("host->client state", state, snapshot != NULL ? (Uint32)SDL_max(snapshot->tick, 0) : 0U,
                               "sent");
    return true;
}

static bool start_direct_connect_session(pong_state *state)
{
    sdl3d_network_session_desc desc;
    int port = 0;

    if (state == NULL)
    {
        return false;
    }

    port = SDL_atoi(state->direct_connect_port);
    if (port <= 0 || port > 65535)
    {
        SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "Invalid port");
        return false;
    }

    destroy_direct_connect_session(state);

    sdl3d_network_session_desc_init(&desc);
    desc.role = SDL3D_NETWORK_ROLE_CLIENT;
    desc.host = state->direct_connect_host;
    desc.port = (Uint16)port;
    desc.local_port = 0;
    desc.handshake_timeout = 5.0f;
    desc.idle_timeout = 10.0f;

    if (!sdl3d_network_session_create(&desc, &state->direct_connect_session))
    {
        SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "%s", SDL_GetError());
        return false;
    }

    SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "%s",
                 sdl3d_network_session_status(state->direct_connect_session) != NULL
                     ? sdl3d_network_session_status(state->direct_connect_session)
                     : "Connecting");
    return true;
}

static void update_direct_connect_session_status(pong_state *state)
{
    if (state == NULL)
    {
        return;
    }

    if (state->direct_connect_session != NULL)
    {
        Uint8 packet[SDL3D_NETWORK_MAX_PACKET_SIZE];
        int packet_size = 0;
        const char *status = sdl3d_network_session_status(state->direct_connect_session);
        SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "%s",
                     status != NULL && status[0] != '\0' ? status : "Connecting");

        while ((packet_size =
                    sdl3d_network_session_receive(state->direct_connect_session, packet, (int)sizeof(packet))) > 0)
        {
            if (packet_size >= 1 && packet[0] == (Uint8)PONG_NETWORK_MESSAGE_START_GAME)
            {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong client received multiplayer start request");
                clear_network_action_overrides(state);
                if (!enter_multiplayer_play_scene(state, "lan", "client", "direct"))
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong client failed to enter multiplayer play scene: %s",
                                SDL_GetError());
                }
                continue;
            }
            if (pong_network_packet_is_kind(packet, packet_size, PONG_NETWORK_MESSAGE_STATE))
            {
                if (!is_multiplayer_play_scene(state))
                {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Pong client received authoritative state before start command; entering play scene");
                    clear_network_action_overrides(state);
                    if (!enter_multiplayer_play_scene(state, "lan", "client", "direct"))
                    {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "Pong client failed to enter multiplayer play scene from state packet: %s",
                                    SDL_GetError());
                        continue;
                    }
                }
                if (process_client_state_packet(state, packet, packet_size))
                {
                    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Pong client applied authoritative state packet");
                }
            }
        }

        const sdl3d_network_state session_state = sdl3d_network_session_state(state->direct_connect_session);
        if (session_state == SDL3D_NETWORK_STATE_REJECTED || session_state == SDL3D_NETWORK_STATE_TIMED_OUT ||
            session_state == SDL3D_NETWORK_STATE_ERROR)
        {
            destroy_direct_connect_session(state);
        }
    }
}

static void destroy_discovery_session(pong_state *state)
{
    if (state != NULL && state->discovery_session != NULL)
    {
        sdl3d_network_discovery_session_destroy(state->discovery_session);
        state->discovery_session = NULL;
    }
}

static bool start_discovery_session(pong_state *state)
{
    sdl3d_network_discovery_session_desc desc;

    if (state == NULL)
    {
        return false;
    }

    if (state->discovery_session != NULL)
    {
        state->discovery_selected_index = 0;
        return sdl3d_network_discovery_session_refresh(state->discovery_session);
    }

    sdl3d_network_discovery_session_desc_init(&desc);
    desc.host = NULL;
    desc.port = SDL3D_NETWORK_DEFAULT_PORT;
    desc.local_port = 0;

    if (!sdl3d_network_discovery_session_create(&desc, &state->discovery_session))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong LAN discovery create failed: %s", SDL_GetError());
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong LAN discovery scanner ready for UDP %u",
                (unsigned int)SDL3D_NETWORK_DEFAULT_PORT);
    state->discovery_selected_index = 0;
    return sdl3d_network_discovery_session_refresh(state->discovery_session);
}

static void update_discovery_scene_state(pong_state *state)
{
    sdl3d_properties *scene_state = NULL;
    const int result_count = state != NULL && state->discovery_session != NULL
                                 ? sdl3d_network_discovery_session_result_count(state->discovery_session)
                                 : 0;

    if (state == NULL || state->data == NULL)
    {
        return;
    }

    scene_state = sdl3d_game_data_mutable_scene_state(state->data);
    if (scene_state == NULL)
    {
        return;
    }

    sdl3d_properties_set_int(scene_state, "multiplayer_discovery_count", result_count);
    sdl3d_properties_set_int(scene_state, "multiplayer_discovery_selected", state->discovery_selected_index);
    sdl3d_properties_set_string(
        scene_state, "multiplayer_discovery_status",
        state->discovery_session != NULL ? sdl3d_network_discovery_session_status(state->discovery_session) : "Idle");
    for (int i = 0; i < SDL3D_NETWORK_MAX_DISCOVERY_RESULTS; ++i)
    {
        const char *key = NULL;
        sdl3d_network_discovery_result result;
        char label[SDL3D_NETWORK_MAX_STATUS_LENGTH + SDL3D_NETWORK_MAX_HOST_LENGTH + 32];
        SDL_zero(result);
        label[0] = '\0';
        switch (i)
        {
        case 0:
            key = "session_0";
            break;
        case 1:
            key = "session_1";
            break;
        case 2:
            key = "session_2";
            break;
        case 3:
            key = "session_3";
            break;
        case 4:
            key = "session_4";
            break;
        case 5:
            key = "session_5";
            break;
        case 6:
            key = "session_6";
            break;
        case 7:
            key = "session_7";
            break;
        default:
            break;
        }

        if (key == NULL)
        {
            continue;
        }

        if (i < result_count && sdl3d_network_discovery_session_get_result(state->discovery_session, i, &result))
        {
            SDL_snprintf(label, sizeof(label), "%s  %s:%u%s%s",
                         result.session_name[0] != '\0' ? result.session_name : "SDL3D Session", result.host,
                         (unsigned int)result.port, result.status[0] != '\0' ? "  " : "",
                         result.status[0] != '\0' ? result.status : "");
        }
        sdl3d_properties_set_string(scene_state, key, label);
    }
}

static bool refresh_discovery_session(pong_state *state)
{
    if (state == NULL)
    {
        return false;
    }

    if (!start_discovery_session(state))
    {
        return false;
    }

    update_discovery_scene_state(state);
    return true;
}

static void handle_discovery_result_selection(pong_state *state, int index)
{
    sdl3d_network_discovery_result result;
    char port_buffer[16];

    if (state == NULL || state->discovery_session == NULL)
    {
        return;
    }

    SDL_zero(result);
    if (!sdl3d_network_discovery_session_get_result(state->discovery_session, index, &result))
    {
        return;
    }

    SDL_strlcpy(state->direct_connect_host, result.host, sizeof(state->direct_connect_host));
    SDL_snprintf(port_buffer, sizeof(port_buffer), "%u", (unsigned int)result.port);
    SDL_strlcpy(state->direct_connect_port, port_buffer, sizeof(state->direct_connect_port));
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong LAN discovery selected result %d: %s:%u session=%s", index,
                result.host, (unsigned int)result.port,
                result.session_name[0] != '\0' ? result.session_name : "SDL3D Session");
    destroy_discovery_session(state);
    if (start_direct_connect_session(state))
    {
        (void)sdl3d_game_data_set_active_scene(state->data, "scene.multiplayer.direct_connect");
    }
}

static void emit_pong_signal_by_name(sdl3d_game_context *ctx, pong_state *state, const char *signal_name)
{
    const int signal_id =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_signal(state->data, signal_name) : -1;
    if (ctx == NULL || state == NULL || signal_id < 0)
    {
        return;
    }
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(ctx->session), signal_id, NULL);
}

static void update_discovery_controls(sdl3d_game_context *ctx, pong_state *state)
{
    if (ctx == NULL || state == NULL || state->data == NULL || state->input == NULL ||
        !is_multiplayer_discovery_scene(state))
    {
        return;
    }

    const int result_count =
        state->discovery_session != NULL ? sdl3d_network_discovery_session_result_count(state->discovery_session) : 0;
    if (result_count <= 0)
    {
        state->discovery_selected_index = 0;
    }
    else
    {
        state->discovery_selected_index = SDL_clamp(state->discovery_selected_index, 0, result_count - 1);
    }

    const int up_action = sdl3d_game_data_find_action(state->data, "action.menu.up");
    const int down_action = sdl3d_game_data_find_action(state->data, "action.menu.down");
    const int select_action = sdl3d_game_data_find_action(state->data, "action.menu.select");
    const int back_action = sdl3d_game_data_find_action(state->data, "action.menu.back");

    if (result_count > 1 && up_action >= 0 && sdl3d_input_is_pressed(state->input, up_action))
    {
        state->discovery_selected_index = (state->discovery_selected_index + result_count - 1) % result_count;
        emit_pong_signal_by_name(ctx, state, "signal.ui.menu.move");
    }
    if (result_count > 1 && down_action >= 0 && sdl3d_input_is_pressed(state->input, down_action))
    {
        state->discovery_selected_index = (state->discovery_selected_index + 1) % result_count;
        emit_pong_signal_by_name(ctx, state, "signal.ui.menu.move");
    }
    if (result_count > 0 && select_action >= 0 && sdl3d_input_is_pressed(state->input, select_action))
    {
        emit_pong_signal_by_name(ctx, state, "signal.ui.menu.select");
        handle_discovery_result_selection(state, state->discovery_selected_index);
        return;
    }
    if (back_action >= 0 && sdl3d_input_is_pressed(state->input, back_action))
    {
        emit_pong_signal_by_name(ctx, state, "signal.ui.menu.select");
        destroy_discovery_session(state);
        (void)sdl3d_game_data_set_active_scene(state->data, "scene.multiplayer.join");
    }
}

static void draw_discovery_overlay(sdl3d_game_context *ctx, pong_state *state)
{
    const int result_count = state != NULL && state->discovery_session != NULL
                                 ? sdl3d_network_discovery_session_result_count(state->discovery_session)
                                 : 0;
    float panel_w;
    float panel_h;
    float panel_x;
    float panel_y;

    if (ctx == NULL || state == NULL || state->discovery_ui == NULL)
    {
        return;
    }

    panel_w = 820.0f;
    panel_h = 500.0f;
    panel_x = ((float)sdl3d_get_render_context_width(ctx->renderer) - panel_w) * 0.5f;
    panel_y = ((float)sdl3d_get_render_context_height(ctx->renderer) - panel_h) * 0.5f;

    sdl3d_ui_begin_panel(state->discovery_ui, panel_x, panel_y, panel_w, panel_h);
    sdl3d_ui_begin_vbox(state->discovery_ui, panel_x + 28.0f, panel_y + 24.0f, panel_w - 56.0f, panel_h - 48.0f);
    sdl3d_ui_layout_label(state->discovery_ui, "DISCOVER LOCAL MATCHES");
    sdl3d_ui_separator(state->discovery_ui);

    if (state->discovery_session != NULL)
    {
        const char *status = sdl3d_network_discovery_session_status(state->discovery_session);
        if (status != NULL && status[0] != '\0')
        {
            sdl3d_ui_layout_label(state->discovery_ui, status);
        }
    }

    for (int i = 0; i < result_count; ++i)
    {
        sdl3d_network_discovery_result result;
        char label[SDL3D_NETWORK_MAX_STATUS_LENGTH + SDL3D_NETWORK_MAX_HOST_LENGTH + 32];
        char button_label[sizeof(label) + 32];
        if (!sdl3d_network_discovery_session_get_result(state->discovery_session, i, &result))
        {
            continue;
        }

        SDL_snprintf(label, sizeof(label), "%s  %s:%u%s%s",
                     result.session_name[0] != '\0' ? result.session_name : "SDL3D Session", result.host,
                     (unsigned int)result.port, result.status[0] != '\0' ? "  " : "",
                     result.status[0] != '\0' ? result.status : "");
        SDL_snprintf(button_label, sizeof(button_label), "%s%s##discovery_%d",
                     i == state->discovery_selected_index ? "> " : "  ", label, i);
        if (sdl3d_ui_layout_button(state->discovery_ui, button_label))
        {
            handle_discovery_result_selection(state, i);
            break;
        }
    }

    if (result_count == 0)
    {
        sdl3d_ui_layout_label(state->discovery_ui, "Searching local network...");
    }
    else
    {
        sdl3d_ui_layout_label(state->discovery_ui, "Press Enter / A to join the selected match.");
    }

    sdl3d_ui_separator(state->discovery_ui);
    if (sdl3d_ui_layout_button(state->discovery_ui, "Back"))
    {
        destroy_discovery_session(state);
        (void)sdl3d_game_data_set_active_scene(state->data, "scene.multiplayer.join");
    }

    sdl3d_ui_end_vbox(state->discovery_ui);
    sdl3d_ui_end_panel(state->discovery_ui);
}

static void update_host_session_status(pong_state *state, float dt)
{
    if (state == NULL)
    {
        return;
    }

    if (state->host_session != NULL)
    {
        if (!sdl3d_network_session_update(state->host_session, dt))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host session update failed: %s", SDL_GetError());
        }

        Uint8 packet[SDL3D_NETWORK_MAX_PACKET_SIZE];
        int packet_size = 0;
        while ((packet_size = sdl3d_network_session_receive(state->host_session, packet, (int)sizeof(packet))) > 0)
        {
            if (packet_size >= 12 && is_multiplayer_play_scene(state) && packet[0] == 'P' && packet[1] == 'O' &&
                packet[2] == 'N' && packet[3] == 'G')
            {
                if (process_host_input_packet(state, packet, packet_size))
                {
                    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Pong host applied remote input packet");
                }
            }
        }

        update_host_session_scene_state(state);

        const char *active_scene = active_scene_name(state);
        if (active_scene == NULL || (!is_multiplayer_lobby_scene(state) && !is_multiplayer_play_scene(state)))
        {
            destroy_host_session(state);
        }
        else if (is_multiplayer_play_scene(state) && state->host_session != NULL &&
                 sdl3d_network_session_state(state->host_session) != SDL3D_NETWORK_STATE_CONNECTED)
        {
            clear_network_action_overrides(state);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong host client disconnected, returning to lobby");
            (void)sdl3d_game_data_set_active_scene(state->data, "scene.multiplayer.lobby");
        }
        return;
    }

    if (is_multiplayer_lobby_scene(state) && is_network_flow_host(state))
    {
        (void)start_host_session(state);
    }
}

static void update_multiplayer_sessions(pong_state *state, float dt)
{
    if (state == NULL)
    {
        return;
    }

    update_host_session_status(state, dt);

    if (state->host_session != NULL)
    {
        update_host_session_scene_state(state);
    }

    if (is_multiplayer_discovery_scene(state))
    {
        if (state->discovery_session == NULL)
        {
            (void)refresh_discovery_session(state);
        }
        else
        {
            if (!sdl3d_network_discovery_session_update(state->discovery_session, dt))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong LAN discovery update failed: %s", SDL_GetError());
            }
            update_discovery_scene_state(state);
        }
    }
    else if (state->discovery_session != NULL)
    {
        destroy_discovery_session(state);
    }

    if (state->direct_connect_session != NULL)
    {
        if (!sdl3d_network_session_update(state->direct_connect_session, dt))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong direct-connect session update failed: %s", SDL_GetError());
        }

        update_direct_connect_session_status(state);

        const bool keep_direct_connect_session = is_direct_connect_scene(state) || is_multiplayer_play_scene(state);
        if (!keep_direct_connect_session)
        {
            destroy_direct_connect_session(state);
        }
    }

    if (state->direct_connect_session != NULL && is_multiplayer_play_scene(state) && is_network_role_client(state))
    {
        (void)send_client_input_packet(state);
    }
}

static void publish_multiplayer_state(pong_state *state)
{
    if (state == NULL || state->host_session == NULL || !is_multiplayer_play_scene(state) ||
        !is_network_role_host(state))
    {
        return;
    }

    if (!send_host_state_packet(state))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host failed to publish multiplayer state");
    }
}

static void update_network_client_input_sensors(sdl3d_game_context *ctx, pong_state *state)
{
    const int camera_toggle_action =
        sdl3d_game_data_find_action(state != NULL ? state->data : NULL, "action.camera.ball.toggle");

    if (ctx == NULL || state == NULL || state->input == NULL || state->data == NULL ||
        state->camera_toggle_signal_id < 0 || camera_toggle_action < 0 || !is_multiplayer_play_scene(state) ||
        !is_network_role_client(state))
    {
        return;
    }

    if (sdl3d_input_is_pressed(state->input, camera_toggle_action))
    {
        sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(ctx->session), state->camera_toggle_signal_id, NULL);
    }
}

static void draw_direct_connect_overlay(sdl3d_game_context *ctx, pong_state *state)
{
    if (ctx == NULL || state == NULL || state->direct_connect_ui == NULL)
    {
        return;
    }

    const int screen_w = sdl3d_get_render_context_width(ctx->renderer);
    const int screen_h = sdl3d_get_render_context_height(ctx->renderer);
    const float panel_w = (float)screen_w * 0.46f;
    const float panel_h = (float)screen_h * 0.44f;
    const float panel_x = ((float)screen_w - panel_w) * 0.5f;
    const float panel_y = ((float)screen_h - panel_h) * 0.5f;

    sdl3d_ui_begin_panel(state->direct_connect_ui, panel_x, panel_y, panel_w, panel_h);
    sdl3d_ui_begin_vbox(state->direct_connect_ui, panel_x + 28.0f, panel_y + 24.0f, panel_w - 56.0f, panel_h - 48.0f);

    sdl3d_ui_layout_label(state->direct_connect_ui, "JOIN MATCH");
    sdl3d_ui_layout_label(state->direct_connect_ui, "Host");
    (void)sdl3d_ui_layout_text_field(state->direct_connect_ui, state->direct_connect_host,
                                     (int)sizeof(state->direct_connect_host));
    sdl3d_ui_layout_label(state->direct_connect_ui, "Port");
    (void)sdl3d_ui_layout_text_field(state->direct_connect_ui, state->direct_connect_port,
                                     (int)sizeof(state->direct_connect_port));
    sdl3d_ui_separator(state->direct_connect_ui);

    if (sdl3d_ui_layout_button(state->direct_connect_ui,
                               state->direct_connect_session != NULL ? "Disconnect" : "Connect"))
    {
        if (state->direct_connect_session != NULL)
        {
            destroy_direct_connect_session(state);
            SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "Disconnected");
        }
        else
        {
            (void)start_direct_connect_session(state);
        }
    }

    if (sdl3d_ui_layout_button(state->direct_connect_ui, "Back"))
    {
        destroy_direct_connect_session(state);
        SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "Returning to Join Match");
        (void)sdl3d_game_data_set_active_scene(state->data, "scene.multiplayer.join");
    }

    sdl3d_ui_layout_label(state->direct_connect_ui, "Status");
    sdl3d_ui_layout_label(state->direct_connect_ui, state->direct_connect_status);

    sdl3d_ui_end_vbox(state->direct_connect_ui);
    sdl3d_ui_end_panel(state->direct_connect_ui);
}

static void refresh_local_play_input(pong_state *state)
{
    const char *active_scene = state != NULL && state->data != NULL ? sdl3d_game_data_active_scene(state->data) : NULL;
    if (state == NULL || state->input == NULL || active_scene == NULL || SDL_strcmp(active_scene, "scene.play") != 0 ||
        !is_local_match_mode(state))
    {
        return;
    }

    const int gamepad_count = sdl3d_input_gamepad_count(state->input);
    if (gamepad_count != state->local_input_gamepad_count)
    {
        bind_local_play_controls(state);
    }
}

static void on_gamepad_feedback(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    pong_state *state = (pong_state *)userdata;
    const sdl3d_registered_actor *settings =
        state != NULL ? sdl3d_game_data_find_actor(state->data, "entity.settings") : NULL;
    const bool vibration_enabled = settings != NULL && sdl3d_properties_get_bool(settings->props, "vibration", false);
    const char *other_actor_name =
        payload != NULL ? sdl3d_properties_get_string(payload, "other_actor_name", NULL) : NULL;

    if (state == NULL || state->input == NULL)
    {
        return;
    }

    if (signal_id == state->vibration_signal_id)
    {
        if (!sdl3d_input_rumble_all_gamepads(state->input, 0.30f, 0.70f, 100))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Pong vibration feedback requested but no rumble-capable gamepad accepted it");
        }
        return;
    }

    if (!vibration_enabled)
    {
        return;
    }

    if (signal_id == state->ball_hit_signal_id && other_actor_name != NULL &&
        (SDL_strcmp(other_actor_name, "entity.paddle.player") == 0 ||
         (is_local_match_mode(state) && SDL_strcmp(other_actor_name, "entity.paddle.cpu") == 0)))
    {
        if (!sdl3d_input_rumble_all_gamepads(state->input, 0.45f, 0.75f, 120))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Pong paddle hit rumble requested but no rumble-capable gamepad accepted it");
        }
    }
}

static void on_multiplayer_lobby_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    pong_state *state = (pong_state *)userdata;
    (void)payload;

    if (state == NULL || signal_id != state->host_start_signal_id)
    {
        return;
    }

    (void)start_selected_lobby_match(state);
}

static void on_multiplayer_discovery_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    pong_state *state = (pong_state *)userdata;
    (void)payload;

    if (state == NULL || signal_id != state->discovery_refresh_signal_id)
    {
        return;
    }

    (void)refresh_discovery_session(state);
}

static bool mount_pong_assets(sdl3d_asset_resolver *assets, char *error, int error_size)
{
#if SDL3D_PONG_EMBEDDED_ASSETS
    return sdl3d_asset_resolver_mount_memory_pack(assets, sdl3d_pong_assets, sdl3d_pong_assets_size, "pong.embedded",
                                                  error, error_size);
#elif defined(SDL3D_PONG_PACK_PATH)
    return sdl3d_asset_resolver_mount_pack_file(assets, SDL3D_PONG_PACK_PATH, error, error_size);
#else
    return sdl3d_asset_resolver_mount_directory(assets, SDL3D_PONG_DATA_DIR, error, error_size);
#endif
}

static bool init_game_data(sdl3d_game_context *ctx, pong_state *state)
{
    state->assets = sdl3d_asset_resolver_create();
    if (state->assets == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong asset resolver allocation failed");
        return false;
    }

    char error[512];
    bool assets_ready = mount_pong_assets(state->assets, error, (int)sizeof(error));
    if (!assets_ready)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong asset mount failed: %s", error);
        sdl3d_asset_resolver_destroy(state->assets);
        state->assets = NULL;
        return false;
    }

    if (!sdl3d_game_data_load_asset(state->assets, SDL3D_PONG_DATA_ASSET_PATH, ctx->session, &state->data, error,
                                    (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong data load failed: %s", error);
        sdl3d_asset_resolver_destroy(state->assets);
        state->assets = NULL;
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong data loaded: asset=%s active_scene=%s", SDL3D_PONG_DATA_ASSET_PATH,
                sdl3d_game_data_active_scene(state->data) != NULL ? sdl3d_game_data_active_scene(state->data)
                                                                  : "<none>");

    if (!sdl3d_game_data_register_adapter(state->data, "adapter.pong.configure_play_input", configure_play_input,
                                          state))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong play input adapter registration failed");
        sdl3d_game_data_destroy(state->data);
        state->data = NULL;
        sdl3d_asset_resolver_destroy(state->assets);
        state->assets = NULL;
        return false;
    }
    return true;
}

static bool pong_init(sdl3d_game_context *ctx, void *userdata)
{
    pong_state *state = (pong_state *)userdata;
    SDL_zero(*state);
    sdl3d_game_data_font_cache_init(&state->font_cache, SDL3D_MEDIA_DIR);
    sdl3d_game_data_particle_cache_init(&state->particle_cache);
    sdl3d_game_data_app_flow_init(&state->app_flow);
    sdl3d_game_data_frame_state_init(&state->frame_state);

    if (!init_game_data(ctx, state))
    {
        return false;
    }
    if (!sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, SDL3D_BUILTIN_FONT_INTER, 16.0f, &state->direct_connect_font))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong direct-connect font load failed: %s", SDL_GetError());
        sdl3d_game_data_destroy(state->data);
        state->data = NULL;
        sdl3d_asset_resolver_destroy(state->assets);
        state->assets = NULL;
        return false;
    }
    if (!sdl3d_ui_create(&state->direct_connect_font, &state->direct_connect_ui))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong direct-connect UI create failed: %s", SDL_GetError());
        sdl3d_free_font(&state->direct_connect_font);
        sdl3d_game_data_destroy(state->data);
        state->data = NULL;
        sdl3d_asset_resolver_destroy(state->assets);
        state->assets = NULL;
        return false;
    }
    if (!sdl3d_ui_create(&state->direct_connect_font, &state->discovery_ui))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong discovery UI create failed: %s", SDL_GetError());
        sdl3d_ui_destroy(state->direct_connect_ui);
        state->direct_connect_ui = NULL;
        sdl3d_free_font(&state->direct_connect_font);
        sdl3d_game_data_destroy(state->data);
        state->data = NULL;
        sdl3d_asset_resolver_destroy(state->assets);
        state->assets = NULL;
        return false;
    }
    reset_direct_connect_defaults(state);
    if (ctx != NULL && ctx->window != NULL)
    {
        SDL_StartTextInput(ctx->window);
    }
    if (state->direct_connect_ui != NULL && ctx != NULL && ctx->renderer != NULL && ctx->window != NULL)
    {
        sdl3d_ui_begin_frame_ex(state->direct_connect_ui, ctx->renderer, ctx->window);
    }
    if (state->discovery_ui != NULL && ctx != NULL && ctx->renderer != NULL && ctx->window != NULL)
    {
        sdl3d_ui_begin_frame_ex(state->discovery_ui, ctx->renderer, ctx->window);
    }
    sdl3d_game_data_image_cache_init(&state->image_cache, state->assets);
    if (!sdl3d_game_data_app_flow_start(&state->app_flow, state->data))
    {
        sdl3d_game_data_image_cache_free(&state->image_cache);
        if (state->discovery_ui != NULL)
        {
            sdl3d_ui_destroy(state->discovery_ui);
            state->discovery_ui = NULL;
        }
        if (state->direct_connect_ui != NULL)
        {
            sdl3d_ui_destroy(state->direct_connect_ui);
            state->direct_connect_ui = NULL;
        }
        sdl3d_free_font(&state->direct_connect_font);
        if (ctx != NULL && ctx->window != NULL)
        {
            SDL_StopTextInput(ctx->window);
        }
        sdl3d_game_data_destroy(state->data);
        state->data = NULL;
        sdl3d_asset_resolver_destroy(state->assets);
        state->assets = NULL;
        return false;
    }

    state->input = sdl3d_game_session_get_input(ctx->session);
    const int gamepad_count = state->input != NULL ? sdl3d_input_gamepad_count(state->input) : 0;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong gamepad count at init: %d", gamepad_count);
    for (int i = 0; i < gamepad_count; ++i)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong gamepad slot: slot=%d id=%d connected=%d", i,
                    sdl3d_input_gamepad_id_at(state->input, i), sdl3d_input_gamepad_is_connected(state->input, i));
    }
    state->paddle_hit_connection = 0;
    state->vibration_connection = 0;
    state->lobby_start_connection = 0;
    state->discovery_refresh_connection = 0;
    state->host_start_signal_id = sdl3d_game_data_find_signal(state->data, "signal.multiplayer.lobby.start");
    state->camera_toggle_signal_id = sdl3d_game_data_find_signal(state->data, "signal.camera.ball.toggle");
    state->ball_hit_signal_id = sdl3d_game_data_find_signal(state->data, "signal.ball.hit_paddle");
    state->vibration_signal_id = sdl3d_game_data_find_signal(state->data, "signal.settings.vibration");
    state->discovery_refresh_signal_id =
        sdl3d_game_data_find_signal(state->data, "signal.multiplayer.discovery.refresh");
    state->local_input_gamepad_count = -1;
    SDL_snprintf(state->host_status, sizeof(state->host_status), "Not hosting");
    SDL_snprintf(state->host_endpoint, sizeof(state->host_endpoint), "UDP %u",
                 (unsigned int)SDL3D_NETWORK_DEFAULT_PORT);
    update_host_session_scene_state(state);
    if (state->input != NULL)
    {
        if (state->ball_hit_signal_id >= 0)
        {
            state->paddle_hit_connection = sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(ctx->session),
                                                                state->ball_hit_signal_id, on_gamepad_feedback, state);
        }
        if (state->vibration_signal_id >= 0)
        {
            state->vibration_connection = sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(ctx->session),
                                                               state->vibration_signal_id, on_gamepad_feedback, state);
        }
        if (state->host_start_signal_id >= 0)
        {
            state->lobby_start_connection =
                sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(ctx->session), state->host_start_signal_id,
                                     on_multiplayer_lobby_signal, state);
        }
        if (state->discovery_refresh_signal_id >= 0)
        {
            state->discovery_refresh_connection =
                sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(ctx->session),
                                     state->discovery_refresh_signal_id, on_multiplayer_discovery_signal, state);
        }
    }
    if (is_multiplayer_discovery_scene(state))
    {
        (void)refresh_discovery_session(state);
    }
    return true;
}

static bool pong_handle_event(sdl3d_game_context *ctx, void *userdata, const SDL_Event *event)
{
    pong_state *state = (pong_state *)userdata;
    if (state != NULL && is_direct_connect_scene(state) && state->direct_connect_ui != NULL && event != NULL)
    {
        const bool mouse_event = event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                 event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_MOUSE_WHEEL;
        const bool key_event =
            event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP || event->type == SDL_EVENT_TEXT_INPUT;

        (void)sdl3d_ui_process_event(state->direct_connect_ui, event);

        if (mouse_event || key_event)
        {
            ctx->input_event_consumed = true;
        }
    }
    if (state != NULL && is_multiplayer_discovery_scene(state) && state->discovery_ui != NULL && event != NULL)
    {
        const bool mouse_event = event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                 event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_MOUSE_WHEEL;

        if (mouse_event)
        {
            (void)sdl3d_ui_process_event(state->discovery_ui, event);
            ctx->input_event_consumed = true;
        }
    }
    (void)ctx;
    return true;
}

static void pong_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    pong_state *state = (pong_state *)userdata;
    refresh_local_play_input(state);
    update_multiplayer_sessions(state, dt);
    update_discovery_controls(ctx, state);
    update_network_client_input_sensors(ctx, state);

    const sdl3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                     .runtime = state->data,
                                                     .app_flow = &state->app_flow,
                                                     .particle_cache = &state->particle_cache,
                                                     .dt = dt};
    (void)sdl3d_game_data_update_frame(&state->frame_state, &frame);
    publish_multiplayer_state(state);
}

static void pong_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    pong_state *state = (pong_state *)userdata;
    refresh_local_play_input(state);
    update_multiplayer_sessions(state, real_dt);
    update_discovery_controls(ctx, state);
    update_network_client_input_sensors(ctx, state);

    const sdl3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                     .runtime = state->data,
                                                     .app_flow = &state->app_flow,
                                                     .particle_cache = &state->particle_cache,
                                                     .dt = real_dt};
    (void)sdl3d_game_data_update_frame(&state->frame_state, &frame);
    publish_multiplayer_state(state);
}

static void pong_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    pong_state *state = (pong_state *)userdata;
    (void)alpha;

    sdl3d_game_data_frame_state_record_render(&state->frame_state, ctx, state->data);

    sdl3d_game_data_frame_desc frame;
    SDL_zero(frame);
    frame.runtime = state->data;
    frame.renderer = ctx->renderer;
    frame.font_cache = &state->font_cache;
    frame.image_cache = &state->image_cache;
    frame.particle_cache = &state->particle_cache;
    frame.app_flow = &state->app_flow;
    frame.metrics = &state->frame_state.metrics;
    frame.render_eval = &state->frame_state.render_eval;
    frame.pulse_phase = state->frame_state.ui_pulse_phase;
    sdl3d_game_data_draw_frame(&frame);

    if (is_direct_connect_scene(state))
    {
        draw_direct_connect_overlay(ctx, state);
    }
    if (is_multiplayer_discovery_scene(state))
    {
        draw_discovery_overlay(ctx, state);
    }
    if (state->direct_connect_ui != NULL)
    {
        sdl3d_ui_end_frame(state->direct_connect_ui);
        sdl3d_ui_render(state->direct_connect_ui, ctx->renderer);
        sdl3d_ui_begin_frame_ex(state->direct_connect_ui, ctx->renderer, ctx->window);
    }
    if (state->discovery_ui != NULL)
    {
        sdl3d_ui_end_frame(state->discovery_ui);
        sdl3d_ui_render(state->discovery_ui, ctx->renderer);
        sdl3d_ui_begin_frame_ex(state->discovery_ui, ctx->renderer, ctx->window);
    }
}

static void pong_shutdown(sdl3d_game_context *ctx, void *userdata)
{
    pong_state *state = (pong_state *)userdata;
    (void)ctx;

    sdl3d_game_data_particle_cache_free(&state->particle_cache);
    sdl3d_game_data_image_cache_free(&state->image_cache);
    sdl3d_game_data_font_cache_free(&state->font_cache);
    if (state->paddle_hit_connection > 0)
    {
        sdl3d_signal_disconnect(sdl3d_game_session_get_signal_bus(ctx->session), state->paddle_hit_connection);
        state->paddle_hit_connection = 0;
    }
    if (state->vibration_connection > 0)
    {
        sdl3d_signal_disconnect(sdl3d_game_session_get_signal_bus(ctx->session), state->vibration_connection);
        state->vibration_connection = 0;
    }
    if (state->lobby_start_connection > 0)
    {
        sdl3d_signal_disconnect(sdl3d_game_session_get_signal_bus(ctx->session), state->lobby_start_connection);
        state->lobby_start_connection = 0;
    }
    if (state->discovery_refresh_connection > 0)
    {
        sdl3d_signal_disconnect(sdl3d_game_session_get_signal_bus(ctx->session), state->discovery_refresh_connection);
        state->discovery_refresh_connection = 0;
    }
    destroy_discovery_session(state);
    if (state->discovery_ui != NULL)
    {
        sdl3d_ui_destroy(state->discovery_ui);
        state->discovery_ui = NULL;
    }
    if (state->direct_connect_ui != NULL)
    {
        sdl3d_ui_destroy(state->direct_connect_ui);
        state->direct_connect_ui = NULL;
    }
    sdl3d_free_font(&state->direct_connect_font);
    destroy_host_session(state);
    destroy_direct_connect_session(state);
    if (ctx != NULL && ctx->window != NULL)
    {
        SDL_StopTextInput(ctx->window);
    }
    state->input = NULL;
    sdl3d_game_data_destroy(state->data);
    state->data = NULL;
    sdl3d_asset_resolver_destroy(state->assets);
    state->assets = NULL;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    pong_state state;
    sdl3d_game_config config;
    SDL_zero(config);
    char title[128] = "SDL3D";
    char error[512];
    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    if (assets == NULL || !mount_pong_assets(assets, error, (int)sizeof(error)) ||
        !sdl3d_game_data_load_app_config_asset(assets, SDL3D_PONG_DATA_ASSET_PATH, &config, title, (int)sizeof(title),
                                               error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong app config load failed: %s", assets != NULL ? error : "");
        sdl3d_asset_resolver_destroy(assets);
        return 1;
    }
    sdl3d_asset_resolver_destroy(assets);

    sdl3d_game_callbacks callbacks;
    SDL_zero(callbacks);
    callbacks.init = pong_init;
    callbacks.event = pong_handle_event;
    callbacks.tick = pong_tick;
    callbacks.pause_tick = pong_pause_tick;
    callbacks.render = pong_render;
    callbacks.shutdown = pong_shutdown;

    return sdl3d_run_game(&config, &callbacks, &state);
}

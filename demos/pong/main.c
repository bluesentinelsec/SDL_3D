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
    bool network_match_termination_active;
    float network_match_termination_timer;
    char network_match_termination_reason[96];
} pong_state;

typedef enum pong_network_message_kind
{
    PONG_NETWORK_MESSAGE_START_GAME,
    PONG_NETWORK_MESSAGE_PAUSE_REQUEST,
    PONG_NETWORK_MESSAGE_RESUME_REQUEST,
    PONG_NETWORK_MESSAGE_DISCONNECT,
} pong_network_message_kind;

static bool send_network_control_packet(pong_state *state, sdl3d_network_session *session,
                                        pong_network_message_kind kind, const char *label);
static bool send_network_control_packet_repeated(pong_state *state, sdl3d_network_session *session,
                                                 pong_network_message_kind kind, const char *label, int count);

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

static bool is_network_match_mode(const pong_state *state)
{
    const sdl3d_properties *scene_state =
        state != NULL && state->data != NULL ? sdl3d_game_data_scene_state(state->data) : NULL;
    const char *match_mode = scene_state != NULL ? sdl3d_properties_get_string(scene_state, "match_mode", NULL) : NULL;
    return match_mode != NULL && SDL_strcmp(match_mode, "lan") == 0;
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
    return is_network_match_mode(state) && SDL_strcmp(network_role_name(state), "host") == 0;
}

static bool is_network_role_client(const pong_state *state)
{
    return is_network_match_mode(state) && SDL_strcmp(network_role_name(state), "client") == 0;
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
    char error[128] = {0};
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
    if (!sdl3d_game_data_clear_network_input_overrides(state->data, "client_input", state->input, error,
                                                       (int)sizeof(error)))
    {
        if (p2_up >= 0)
        {
            sdl3d_input_clear_action_override(state->input, p2_up);
        }
        if (p2_down >= 0)
        {
            sdl3d_input_clear_action_override(state->input, p2_down);
        }
    }
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

static void destroy_host_session_internal(pong_state *state, bool notify_peer)
{
    if (state != NULL && state->host_session != NULL)
    {
        if (notify_peer)
        {
            (void)send_network_control_packet_repeated(state, state->host_session, PONG_NETWORK_MESSAGE_DISCONNECT,
                                                       "host disconnect", 5);
        }
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

static void destroy_host_session(pong_state *state)
{
    destroy_host_session_internal(state, true);
}

static bool apply_active_play_input_profile(pong_state *state)
{
    char error[192];
    const char *profile_name = NULL;
    if (state == NULL || state->data == NULL || state->input == NULL)
    {
        return false;
    }

    error[0] = '\0';
    if (!sdl3d_game_data_apply_active_input_profile(state->data, state->input, &profile_name, error, sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong input profile apply failed: %s",
                    error[0] != '\0' ? error : "unknown error");
        return false;
    }

    state->local_input_gamepad_count = sdl3d_input_gamepad_count(state->input);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong input profile applied: profile=%s role=%s gamepads=%d",
                profile_name != NULL ? profile_name : "<none>", network_role_name(state),
                state->local_input_gamepad_count);
    return true;
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
    const char *requested_match_mode = scene_context_string(payload, scene_state, "match_mode", "single");
    const bool requested_lan = requested_match_mode != NULL && SDL_strcmp(requested_match_mode, "lan") == 0;
    const char *requested_network_role =
        requested_lan ? scene_context_string(payload, scene_state, "network_role", "none") : "none";
    const char *requested_network_flow =
        requested_lan ? scene_context_string(payload, scene_state, "network_flow", "none") : "none";
    const bool valid_network_role =
        requested_network_role != NULL &&
        (SDL_strcmp(requested_network_role, "host") == 0 || SDL_strcmp(requested_network_role, "client") == 0);
    const char *match_mode = requested_lan && !valid_network_role ? "single" : requested_match_mode;
    const char *network_role = requested_lan && valid_network_role ? requested_network_role : "none";
    const char *network_flow = requested_lan && valid_network_role ? requested_network_flow : "none";

    clear_network_action_overrides(state);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Pong play input context: requested_match_mode=%s match_mode=%s network_role=%s network_flow=%s",
                requested_match_mode != NULL ? requested_match_mode : "none", match_mode != NULL ? match_mode : "none",
                network_role != NULL ? network_role : "none", network_flow != NULL ? network_flow : "none");

    if (mutable_scene_state != NULL)
    {
        sdl3d_properties_set_string(mutable_scene_state, "match_mode", match_mode != NULL ? match_mode : "single");
        sdl3d_properties_set_string(mutable_scene_state, "network_role", network_role != NULL ? network_role : "none");
        sdl3d_properties_set_string(mutable_scene_state, "network_flow", network_flow != NULL ? network_flow : "none");
    }

    return apply_active_play_input_profile(state);
}

static void destroy_direct_connect_session_internal(pong_state *state, bool notify_peer)
{
    if (state != NULL && state->direct_connect_session != NULL)
    {
        if (notify_peer)
        {
            (void)send_network_control_packet_repeated(state, state->direct_connect_session,
                                                       PONG_NETWORK_MESSAGE_DISCONNECT, "client disconnect", 5);
        }
        sdl3d_network_session_destroy(state->direct_connect_session);
        state->direct_connect_session = NULL;
    }
}

static void destroy_direct_connect_session(pong_state *state)
{
    destroy_direct_connect_session_internal(state, true);
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

static const char *network_control_name_for_kind(pong_network_message_kind kind)
{
    switch (kind)
    {
    case PONG_NETWORK_MESSAGE_START_GAME:
        return "start_game";
    case PONG_NETWORK_MESSAGE_PAUSE_REQUEST:
        return "pause_request";
    case PONG_NETWORK_MESSAGE_RESUME_REQUEST:
        return "resume_request";
    case PONG_NETWORK_MESSAGE_DISCONNECT:
        return "disconnect";
    default:
        return NULL;
    }
}

static bool network_control_kind_from_name(const char *name, pong_network_message_kind *out_kind)
{
    if (name == NULL)
    {
        return false;
    }
    if (SDL_strcmp(name, "start_game") == 0)
    {
        if (out_kind != NULL)
            *out_kind = PONG_NETWORK_MESSAGE_START_GAME;
        return true;
    }
    if (SDL_strcmp(name, "pause_request") == 0)
    {
        if (out_kind != NULL)
            *out_kind = PONG_NETWORK_MESSAGE_PAUSE_REQUEST;
        return true;
    }
    if (SDL_strcmp(name, "resume_request") == 0)
    {
        if (out_kind != NULL)
            *out_kind = PONG_NETWORK_MESSAGE_RESUME_REQUEST;
        return true;
    }
    if (SDL_strcmp(name, "disconnect") == 0)
    {
        if (out_kind != NULL)
            *out_kind = PONG_NETWORK_MESSAGE_DISCONNECT;
        return true;
    }
    return false;
}

static bool send_network_control_packet(pong_state *state, sdl3d_network_session *session,
                                        pong_network_message_kind kind, const char *label)
{
    Uint8 packet[SDL3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE];
    size_t packet_size = 0U;
    char error[160] = {0};
    const Uint32 tick = (Uint32)SDL_GetTicks();
    const char *control_name = network_control_name_for_kind(kind);

    if (state == NULL || state->data == NULL || session == NULL || !sdl3d_network_session_is_connected(session) ||
        control_name == NULL)
    {
        return false;
    }

    if (!sdl3d_game_data_encode_network_control(state->data, control_name, tick, packet, sizeof(packet), &packet_size,
                                                error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong network control packet encode failed: %s", error);
        return false;
    }

    if (!sdl3d_network_session_send(session, packet, (int)packet_size))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong network control packet send failed: %s", SDL_GetError());
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong sent network control: %s kind=%u",
                (unsigned long long)pong_log_timestamp_ms(), label != NULL ? label : "control", (unsigned int)kind);
    return true;
}

static bool send_network_control_packet_repeated(pong_state *state, sdl3d_network_session *session,
                                                 pong_network_message_kind kind, const char *label, int count)
{
    bool sent_any = false;
    const int attempts = SDL_max(count, 1);

    for (int i = 0; i < attempts; ++i)
    {
        if (send_network_control_packet(state, session, kind, label))
        {
            sent_any = true;
        }
        if (session != NULL)
        {
            (void)sdl3d_network_session_update(session, 0.0f);
        }
    }

    return sent_any;
}

static bool read_network_control_packet(pong_state *state, const Uint8 *packet, int packet_size,
                                        pong_network_message_kind expected, Uint32 *out_tick)
{
    sdl3d_game_data_network_control control;
    pong_network_message_kind actual = 0;
    char error[160] = {0};

    if (state == NULL || state->data == NULL || packet == NULL || packet_size <= 0)
    {
        return false;
    }

    if (!sdl3d_game_data_decode_network_control(state->data, packet, (size_t)packet_size, &control, error,
                                                (int)sizeof(error)) ||
        !network_control_kind_from_name(control.name, &actual) || actual != expected)
    {
        return false;
    }

    if (out_tick != NULL)
    {
        *out_tick = control.tick;
    }
    return true;
}

static bool send_host_start_packet(pong_state *state)
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

    if (!send_network_control_packet(state, state->host_session, PONG_NETWORK_MESSAGE_START_GAME, "start game"))
    {
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
    Uint8 packet[128];
    size_t packet_size = 0U;
    char error[160] = {0};
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
    const Uint32 tick = snapshot != NULL ? (Uint32)SDL_max(snapshot->tick, 0) : 0U;

    if (!sdl3d_game_data_encode_network_input(state->data, "client_input", state->input, tick, packet, sizeof(packet),
                                              &packet_size, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong client input packet encode failed: %s", error);
        return false;
    }

    if (!sdl3d_network_session_send(state->direct_connect_session, packet, (int)packet_size))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong client input packet send failed: %s", SDL_GetError());
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong client->host input tick=%u up=%.3f down=%.3f scene=%s",
                (unsigned long long)pong_log_timestamp_ms(), (unsigned int)tick, up_value, down_value,
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

static void send_client_pause_request_if_pressed(sdl3d_game_context *ctx, pong_state *state)
{
    const int pause_action =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_action(state->data, "action.pause") : -1;
    pong_network_message_kind request_kind;

    if (ctx == NULL || state == NULL || state->input == NULL || state->direct_connect_session == NULL ||
        pause_action < 0 || !is_multiplayer_play_scene(state) || !is_network_role_client(state) ||
        !sdl3d_network_session_is_connected(state->direct_connect_session) ||
        !sdl3d_input_is_pressed(state->input, pause_action))
    {
        return;
    }

    request_kind = ctx->paused ? PONG_NETWORK_MESSAGE_RESUME_REQUEST : PONG_NETWORK_MESSAGE_PAUSE_REQUEST;
    (void)send_network_control_packet(state, state->direct_connect_session, request_kind,
                                      request_kind == PONG_NETWORK_MESSAGE_PAUSE_REQUEST ? "pause request"
                                                                                         : "resume request");
}

static bool sync_match_pause_from_context(sdl3d_game_context *ctx, pong_state *state)
{
    sdl3d_registered_actor *match =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_actor(state->data, "entity.match") : NULL;
    if (ctx == NULL || match == NULL)
    {
        return false;
    }

    sdl3d_properties_set_bool(match->props, "paused", ctx->paused);
    return true;
}

static bool sync_context_pause_from_match(sdl3d_game_context *ctx, pong_state *state)
{
    const sdl3d_registered_actor *match =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_actor(state->data, "entity.match") : NULL;
    const bool paused = match != NULL && sdl3d_properties_get_bool(match->props, "paused", false);
    if (ctx == NULL || match == NULL)
    {
        return false;
    }

    if (ctx->paused != paused)
    {
        ctx->paused = paused;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong client mirrored host pause state: paused=%d",
                    (unsigned long long)pong_log_timestamp_ms(), ctx->paused ? 1 : 0);
    }
    return true;
}

static bool process_host_input_packet(pong_state *state, const Uint8 *packet, int packet_size)
{
    Uint32 tick = 0U;
    char error[160] = {0};

    if (state == NULL || state->data == NULL || state->input == NULL || packet == NULL || packet_size <= 0)
    {
        return false;
    }

    if (!sdl3d_game_data_apply_network_input(state->data, state->input, packet, (size_t)packet_size, &tick, error,
                                             (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host input packet apply failed: %s", error);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong host<-client input tick=%u scene=%s",
                (unsigned long long)pong_log_timestamp_ms(), (unsigned int)tick,
                state != NULL && state->data != NULL && sdl3d_game_data_active_scene(state->data) != NULL
                    ? sdl3d_game_data_active_scene(state->data)
                    : "<none>");
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong host queued remote paddle input overrides",
                (unsigned long long)pong_log_timestamp_ms());
    return true;
}

static bool process_client_state_packet(sdl3d_game_context *ctx, pong_state *state, const Uint8 *packet,
                                        int packet_size)
{
    Uint32 tick = 0U;
    char error[160] = {0};

    if (state == NULL || state->data == NULL || packet == NULL || packet_size <= 0)
    {
        return false;
    }

    if (!sdl3d_game_data_apply_network_snapshot(state->data, packet, (size_t)packet_size, &tick, error,
                                                (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong client state packet apply failed: %s", error);
        return false;
    }

    (void)sync_context_pause_from_match(ctx, state);
    pong_log_multiplayer_state("client<-host state", state, tick, "applied");
    return true;
}

static void begin_network_match_termination(sdl3d_game_context *ctx, pong_state *state, bool local_host,
                                            const char *reason)
{
    if (state == NULL || state->data == NULL)
    {
        return;
    }

    clear_network_action_overrides(state);
    if (ctx != NULL)
    {
        ctx->paused = true;
    }

    SDL_snprintf(state->network_match_termination_reason, sizeof(state->network_match_termination_reason), "%s",
                 reason != NULL && reason[0] != '\0' ? reason : "Peer disconnected");
    state->network_match_termination_timer = 0.0f;
    state->network_match_termination_active = true;

    if (local_host)
    {
        destroy_host_session_internal(state, false);
        SDL_snprintf(state->host_status, sizeof(state->host_status), "%s",
                     reason != NULL && reason[0] != '\0' ? reason : "Client disconnected");
        update_host_session_scene_state(state);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong host match terminated: %s", state->host_status);
    }
    else
    {
        SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "%s",
                     reason != NULL && reason[0] != '\0' ? reason : "Host disconnected");
        destroy_direct_connect_session_internal(state, false);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong client match terminated: %s", state->direct_connect_status);
    }
}

static void return_to_multiplayer_after_disconnect(sdl3d_game_context *ctx, pong_state *state, bool local_host,
                                                   const char *reason)
{
    if (state == NULL || state->data == NULL)
    {
        return;
    }

    if (is_multiplayer_play_scene(state))
    {
        begin_network_match_termination(ctx, state, local_host, reason);
        return;
    }

    clear_network_action_overrides(state);
    if (ctx != NULL)
    {
        ctx->paused = false;
    }

    if (local_host)
    {
        SDL_snprintf(state->host_status, sizeof(state->host_status), "%s",
                     reason != NULL && reason[0] != '\0' ? reason : "Client disconnected");
        destroy_host_session_internal(state, false);
        update_host_session_scene_state(state);
        (void)sdl3d_game_data_set_active_scene(state->data, "scene.multiplayer.lobby");
    }
    else
    {
        SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "%s",
                     reason != NULL && reason[0] != '\0' ? reason : "Host disconnected");
        destroy_direct_connect_session_internal(state, false);
        (void)sdl3d_game_data_set_active_scene(state->data, "scene.multiplayer.join");
    }
}

static bool process_decoded_network_control_packet(sdl3d_game_context *ctx, pong_state *state,
                                                   pong_network_message_kind kind, Uint32 tick)
{
    switch (kind)
    {
    case PONG_NETWORK_MESSAGE_PAUSE_REQUEST:
        if (ctx != NULL && is_network_role_host(state))
        {
            ctx->paused = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong host accepted peer pause request tick=%u",
                        (unsigned long long)pong_log_timestamp_ms(), (unsigned int)tick);
        }
        return true;
    case PONG_NETWORK_MESSAGE_RESUME_REQUEST:
        if (ctx != NULL && is_network_role_host(state))
        {
            ctx->paused = false;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong host accepted peer resume request tick=%u",
                        (unsigned long long)pong_log_timestamp_ms(), (unsigned int)tick);
        }
        return true;
    case PONG_NETWORK_MESSAGE_DISCONNECT:
        return_to_multiplayer_after_disconnect(ctx, state, is_network_role_host(state), "Peer disconnected");
        return true;
    default:
        return false;
    }
}

static bool send_host_state_packet(sdl3d_game_context *ctx, pong_state *state)
{
    const sdl3d_input_snapshot *snapshot = NULL;
    Uint8 packet[SDL3D_NETWORK_MAX_PACKET_SIZE];
    size_t packet_size = 0U;
    char error[160] = {0};

    if (state == NULL || state->host_session == NULL || !sdl3d_network_session_is_connected(state->host_session) ||
        state->data == NULL || state->input == NULL)
    {
        return false;
    }

    snapshot = sdl3d_input_get_snapshot(state->input);
    if (snapshot == NULL || !sync_match_pause_from_context(ctx, state) ||
        !sdl3d_game_data_encode_network_snapshot(state->data, "play_state", (Uint32)SDL_max(snapshot->tick, 0), packet,
                                                 sizeof(packet), &packet_size, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host state packet encode failed: %s", error);
        return false;
    }

    if (!sdl3d_network_session_send(state->host_session, packet, (int)packet_size))
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

static void update_direct_connect_session_status(sdl3d_game_context *ctx, pong_state *state)
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
            if (read_network_control_packet(state, packet, packet_size, PONG_NETWORK_MESSAGE_START_GAME, NULL))
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
            Uint32 tick = 0U;
            if (read_network_control_packet(state, packet, packet_size, PONG_NETWORK_MESSAGE_DISCONNECT, &tick))
            {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong client received host disconnect tick=%u",
                            (unsigned long long)pong_log_timestamp_ms(), (unsigned int)tick);
                return_to_multiplayer_after_disconnect(ctx, state, false, "Host exited");
                break;
            }
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
            if (process_client_state_packet(ctx, state, packet, packet_size))
            {
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Pong client applied authoritative state packet");
            }
        }

        if (state->direct_connect_session == NULL)
        {
            return;
        }

        const sdl3d_network_state session_state = sdl3d_network_session_state(state->direct_connect_session);
        if (session_state == SDL3D_NETWORK_STATE_REJECTED || session_state == SDL3D_NETWORK_STATE_TIMED_OUT ||
            session_state == SDL3D_NETWORK_STATE_ERROR)
        {
            const bool was_playing = is_multiplayer_play_scene(state);
            const char *reason = session_state == SDL3D_NETWORK_STATE_TIMED_OUT  ? "Host timed out"
                                 : session_state == SDL3D_NETWORK_STATE_REJECTED ? "Host rejected connection"
                                                                                 : "Host connection error";
            if (was_playing)
            {
                return_to_multiplayer_after_disconnect(ctx, state, false, reason);
            }
            else
            {
                SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "%s", reason);
                destroy_direct_connect_session_internal(state, false);
            }
        }
    }
}

static void update_network_match_termination(sdl3d_game_context *ctx, pong_state *state, float dt)
{
    const int select_action =
        state != NULL && state->data != NULL ? sdl3d_game_data_find_action(state->data, "action.menu.select") : -1;

    if (state == NULL || !state->network_match_termination_active)
    {
        return;
    }

    if (ctx != NULL)
    {
        ctx->paused = true;
    }

    state->network_match_termination_timer += SDL_max(dt, 0.0f);
    if (state->network_match_termination_timer < 0.25f)
    {
        return;
    }

    if (state->input == NULL || select_action < 0 || !sdl3d_input_is_pressed(state->input, select_action))
    {
        return;
    }

    state->network_match_termination_active = false;
    state->network_match_termination_timer = 0.0f;
    state->network_match_termination_reason[0] = '\0';
    if (ctx != NULL)
    {
        ctx->paused = false;
    }
    if (state->data != NULL)
    {
        (void)sdl3d_game_data_set_active_scene(state->data, "scene.title");
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

static bool start_discovered_match_connection(pong_state *state, int index)
{
    sdl3d_network_discovery_result result;
    char port_buffer[16];

    if (state == NULL || state->discovery_session == NULL)
    {
        return false;
    }

    SDL_zero(result);
    if (!sdl3d_network_discovery_session_get_result(state->discovery_session, index, &result))
    {
        return false;
    }

    SDL_strlcpy(state->direct_connect_host, result.host, sizeof(state->direct_connect_host));
    SDL_snprintf(port_buffer, sizeof(port_buffer), "%u", (unsigned int)result.port);
    SDL_strlcpy(state->direct_connect_port, port_buffer, sizeof(state->direct_connect_port));
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong LAN discovery auto-connecting to result %d: %s:%u session=%s",
                index, result.host, (unsigned int)result.port,
                result.session_name[0] != '\0' ? result.session_name : "SDL3D Session");
    destroy_discovery_session(state);
    if (!start_direct_connect_session(state))
    {
        return false;
    }
    SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "Match found. Connecting...");
    return true;
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

    const int back_action = sdl3d_game_data_find_action(state->data, "action.menu.back");

    if (back_action >= 0 && sdl3d_input_is_pressed(state->input, back_action))
    {
        emit_pong_signal_by_name(ctx, state, "signal.ui.menu.select");
        destroy_discovery_session(state);
        destroy_direct_connect_session(state);
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
    else if (state->direct_connect_session != NULL)
    {
        if (sdl3d_network_session_is_connected(state->direct_connect_session))
        {
            sdl3d_ui_layout_label(state->discovery_ui, "Connected. Waiting for host to start...");
        }
        else
        {
            sdl3d_ui_layout_label(state->discovery_ui, state->direct_connect_status);
        }
    }

    if (state->direct_connect_session != NULL)
    {
        sdl3d_ui_layout_label(state->discovery_ui, "The host will start the match.");
    }
    else if (result_count == 0)
    {
        sdl3d_ui_layout_label(state->discovery_ui, "Searching local network...");
    }
    else
    {
        sdl3d_ui_layout_label(state->discovery_ui, "Match found. Connecting...");
    }

    sdl3d_ui_separator(state->discovery_ui);
    if (sdl3d_ui_layout_button(state->discovery_ui, "Back"))
    {
        destroy_discovery_session(state);
        destroy_direct_connect_session(state);
        (void)sdl3d_game_data_set_active_scene(state->data, "scene.multiplayer.join");
    }

    sdl3d_ui_end_vbox(state->discovery_ui);
    sdl3d_ui_end_panel(state->discovery_ui);
}

static void update_host_session_status(sdl3d_game_context *ctx, pong_state *state, float dt)
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
            Uint32 tick = 0U;
            if (read_network_control_packet(state, packet, packet_size, PONG_NETWORK_MESSAGE_DISCONNECT, &tick))
            {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong host received client disconnect tick=%u",
                            (unsigned long long)pong_log_timestamp_ms(), (unsigned int)tick);
                return_to_multiplayer_after_disconnect(ctx, state, true, "Client exited");
                break;
            }
            if (is_multiplayer_play_scene(state) &&
                read_network_control_packet(state, packet, packet_size, PONG_NETWORK_MESSAGE_PAUSE_REQUEST, &tick))
            {
                (void)process_decoded_network_control_packet(ctx, state, PONG_NETWORK_MESSAGE_PAUSE_REQUEST, tick);
                continue;
            }
            if (is_multiplayer_play_scene(state) &&
                read_network_control_packet(state, packet, packet_size, PONG_NETWORK_MESSAGE_RESUME_REQUEST, &tick))
            {
                (void)process_decoded_network_control_packet(ctx, state, PONG_NETWORK_MESSAGE_RESUME_REQUEST, tick);
                continue;
            }
            if (is_multiplayer_play_scene(state))
            {
                if (process_host_input_packet(state, packet, packet_size))
                {
                    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Pong host applied remote input packet");
                }
            }
        }

        update_host_session_scene_state(state);

        const char *active_scene = active_scene_name(state);
        const bool keep_host_session =
            is_multiplayer_lobby_scene(state) || (is_multiplayer_play_scene(state) && is_network_role_host(state));
        if (active_scene == NULL || !keep_host_session)
        {
            destroy_host_session(state);
        }
        else if (is_multiplayer_play_scene(state) && state->host_session != NULL &&
                 sdl3d_network_session_state(state->host_session) != SDL3D_NETWORK_STATE_CONNECTED)
        {
            begin_network_match_termination(ctx, state, true, "Client timed out");
        }
        return;
    }

    if (is_multiplayer_lobby_scene(state) && is_network_flow_host(state))
    {
        (void)start_host_session(state);
    }
}

static void update_multiplayer_sessions(sdl3d_game_context *ctx, pong_state *state, float dt)
{
    if (state == NULL)
    {
        return;
    }

    update_host_session_status(ctx, state, dt);

    if (state->host_session != NULL)
    {
        update_host_session_scene_state(state);
    }

    if (is_multiplayer_discovery_scene(state) && state->direct_connect_session == NULL)
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
            if (sdl3d_network_discovery_session_result_count(state->discovery_session) > 0)
            {
                (void)start_discovered_match_connection(state, 0);
            }
        }
    }
    else if (state->discovery_session != NULL)
    {
        destroy_discovery_session(state);
    }

    if (state->direct_connect_session != NULL)
    {
        const bool keep_direct_connect_session = is_direct_connect_scene(state) ||
                                                 is_multiplayer_discovery_scene(state) ||
                                                 (is_multiplayer_play_scene(state) && is_network_role_client(state));
        if (!keep_direct_connect_session)
        {
            destroy_direct_connect_session(state);
            return;
        }

        if (!sdl3d_network_session_update(state->direct_connect_session, dt))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong direct-connect session update failed: %s", SDL_GetError());
        }

        update_direct_connect_session_status(ctx, state);
    }

    if (state->direct_connect_session != NULL && is_multiplayer_play_scene(state) && is_network_role_client(state))
    {
        send_client_pause_request_if_pressed(ctx, state);
        (void)send_client_input_packet(state);
    }
}

static void publish_multiplayer_state(sdl3d_game_context *ctx, pong_state *state)
{
    if (state == NULL || state->host_session == NULL || !is_multiplayer_play_scene(state) ||
        !is_network_role_host(state))
    {
        return;
    }

    if (!send_host_state_packet(ctx, state))
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

static void draw_network_match_termination_overlay(sdl3d_game_context *ctx, pong_state *state)
{
    char message[192];
    float panel_w;
    float panel_h;
    float panel_x;
    float panel_y;

    if (ctx == NULL || state == NULL || state->direct_connect_ui == NULL || !state->network_match_termination_active)
    {
        return;
    }

    SDL_snprintf(message, sizeof(message), "Match terminated: %s - Press Enter to return to title screen.",
                 state->network_match_termination_reason[0] != '\0' ? state->network_match_termination_reason
                                                                    : "Peer disconnected");

    panel_w = 880.0f;
    panel_h = 150.0f;
    panel_x = ((float)sdl3d_get_render_context_width(ctx->renderer) - panel_w) * 0.5f;
    panel_y = ((float)sdl3d_get_render_context_height(ctx->renderer) - panel_h) * 0.5f;

    sdl3d_ui_begin_panel(state->direct_connect_ui, panel_x, panel_y, panel_w, panel_h);
    sdl3d_ui_begin_vbox(state->direct_connect_ui, panel_x + 28.0f, panel_y + 34.0f, panel_w - 56.0f, panel_h - 68.0f);
    sdl3d_ui_layout_label(state->direct_connect_ui, message);
    sdl3d_ui_end_vbox(state->direct_connect_ui);
    sdl3d_ui_end_panel(state->direct_connect_ui);
}

static void refresh_local_play_input(pong_state *state)
{
    const char *active_scene = state != NULL && state->data != NULL ? sdl3d_game_data_active_scene(state->data) : NULL;
    if (state == NULL || state->input == NULL || active_scene == NULL || SDL_strcmp(active_scene, "scene.play") != 0)
    {
        return;
    }

    const int gamepad_count = sdl3d_input_gamepad_count(state->input);
    if (gamepad_count != state->local_input_gamepad_count)
    {
        (void)apply_active_play_input_profile(state);
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

    if (state->direct_connect_session == NULL)
    {
        (void)refresh_discovery_session(state);
    }
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
    update_network_match_termination(ctx, state, dt);
    update_multiplayer_sessions(ctx, state, dt);
    update_discovery_controls(ctx, state);
    update_network_client_input_sensors(ctx, state);

    const sdl3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                     .runtime = state->data,
                                                     .app_flow = &state->app_flow,
                                                     .particle_cache = &state->particle_cache,
                                                     .dt = dt};
    (void)sdl3d_game_data_update_frame(&state->frame_state, &frame);
    publish_multiplayer_state(ctx, state);
}

static void pong_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    pong_state *state = (pong_state *)userdata;
    refresh_local_play_input(state);
    update_network_match_termination(ctx, state, real_dt);
    update_multiplayer_sessions(ctx, state, real_dt);
    update_discovery_controls(ctx, state);
    update_network_client_input_sensors(ctx, state);

    const sdl3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                     .runtime = state->data,
                                                     .app_flow = &state->app_flow,
                                                     .particle_cache = &state->particle_cache,
                                                     .dt = real_dt};
    (void)sdl3d_game_data_update_frame(&state->frame_state, &frame);
    publish_multiplayer_state(ctx, state);
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
    draw_network_match_termination_overlay(ctx, state);
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

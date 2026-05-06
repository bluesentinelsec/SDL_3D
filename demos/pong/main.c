#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <stdbool.h>
#include <string.h>

#include "sdl3d/asset.h"
#include "sdl3d/data_game.h"
#include "sdl3d/game.h"
#include "sdl3d/game_data.h"
#include "sdl3d/network.h"
#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"

#if SDL3D_PONG_EMBEDDED_ASSETS
#include "sdl3d_pong_assets.h"
#endif

typedef struct pong_state
{
    sdl3d_data_game_runtime *runtime;
    sdl3d_game_data_runtime *data;
    sdl3d_input_manager *input;
    sdl3d_network_session *host_session;
    sdl3d_network_session *direct_connect_session;
    char host_status[SDL3D_NETWORK_MAX_STATUS_LENGTH];
    char host_endpoint[SDL3D_NETWORK_MAX_STATUS_LENGTH];
    char direct_connect_host[SDL3D_NETWORK_MAX_HOST_LENGTH];
    char direct_connect_port[16];
    char direct_connect_status[SDL3D_NETWORK_MAX_STATUS_LENGTH];
    int lobby_start_connection;
    int host_start_signal_id;
    int camera_toggle_signal_id;
    float network_match_termination_timer;
} pong_state;

static bool send_network_control_packet(pong_state *state, sdl3d_network_session *session, const char *binding,
                                        const char *label);
static bool send_network_control_packet_repeated(pong_state *state, sdl3d_network_session *session, const char *binding,
                                                 const char *label, int count);

static const char PONG_NETWORK_BINDING_STATE_SNAPSHOT[] = "state_snapshot";
static const char PONG_NETWORK_BINDING_CLIENT_INPUT[] = "client_input";
static const char PONG_NETWORK_BINDING_START_GAME[] = "start_game";
static const char PONG_NETWORK_BINDING_PAUSE_REQUEST[] = "pause_request";
static const char PONG_NETWORK_BINDING_RESUME_REQUEST[] = "resume_request";
static const char PONG_NETWORK_BINDING_DISCONNECT[] = "disconnect";
static const char PONG_NETWORK_BINDING_CLIENT_UP[] = "client_up";
static const char PONG_NETWORK_BINDING_CLIENT_DOWN[] = "client_down";
static const char PONG_NETWORK_BINDING_MENU_SELECT[] = "menu_select";
static const char PONG_NETWORK_BINDING_CAMERA_TOGGLE[] = "camera_toggle";
static const char PONG_NETWORK_BINDING_LOBBY_START[] = "lobby_start";
static const char PONG_NETWORK_DIAGNOSTIC_MULTIPLAYER_STATE[] = "multiplayer_state";
static const char PONG_HOST_SESSION[] = "host";
static const char PONG_DIRECT_CONNECT_SESSION[] = "direct_connect";
static const char PONG_DIRECT_CONNECT_HOST_KEY[] = "direct_connect_host";
static const char PONG_DIRECT_CONNECT_PORT_KEY[] = "direct_connect_port";
static const char PONG_DIRECT_CONNECT_STATUS_KEY[] = "direct_connect_status";
static const char PONG_DIRECT_CONNECT_STATE_KEY[] = "direct_connect_state";
static const char PONG_DIRECT_CONNECT_CONNECTED_KEY[] = "direct_connect_connected";

static sdl3d_data_game_network_bindings pong_network_bindings(void)
{
    sdl3d_data_game_network_bindings bindings;
    SDL_zero(bindings);
    bindings.state_snapshot = PONG_NETWORK_BINDING_STATE_SNAPSHOT;
    bindings.client_input = PONG_NETWORK_BINDING_CLIENT_INPUT;
    bindings.start_game = PONG_NETWORK_BINDING_START_GAME;
    bindings.pause_request = PONG_NETWORK_BINDING_PAUSE_REQUEST;
    bindings.resume_request = PONG_NETWORK_BINDING_RESUME_REQUEST;
    bindings.disconnect = PONG_NETWORK_BINDING_DISCONNECT;
    return bindings;
}

static Uint64 pong_log_timestamp_ms(void)
{
    return SDL_GetTicks();
}

static void log_network_snapshot_diagnostic(pong_state *state, Uint32 packet_tick, const char *event, const char *extra)
{
    char error[256] = {0};
    if (state == NULL || state->data == NULL ||
        !sdl3d_game_data_log_network_snapshot_diagnostic(state->data, PONG_NETWORK_DIAGNOSTIC_MULTIPLAYER_STATE,
                                                         packet_tick, event, extra, NULL, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "network snapshot diagnostic failed: %s",
                    error[0] != '\0' ? error : "runtime unavailable");
    }
}

static const char *active_scene_name(const pong_state *state)
{
    return state != NULL && state->data != NULL ? sdl3d_game_data_active_scene(state->data) : NULL;
}

static const char *network_session_scene(const pong_state *state, const char *name, const char *fallback)
{
    const char *scene = NULL;
    if (state != NULL && state->data != NULL && sdl3d_game_data_get_network_session_scene(state->data, name, &scene))
    {
        return scene;
    }
    return fallback;
}

static const char *network_session_state_key(const pong_state *state, const char *name, const char *fallback)
{
    const char *key = NULL;
    if (state != NULL && state->data != NULL && sdl3d_game_data_get_network_session_state_key(state->data, name, &key))
    {
        return key;
    }
    return fallback;
}

static const char *network_session_state_value(const pong_state *state, const char *group, const char *name,
                                               const char *fallback)
{
    const char *value = NULL;
    if (state != NULL && state->data != NULL &&
        sdl3d_game_data_get_network_session_state_value(state->data, group, name, &value))
    {
        return value;
    }
    return fallback;
}

static const char *network_session_message(const pong_state *state, const char *group, const char *name,
                                           const char *fallback)
{
    const char *message = NULL;
    if (state != NULL && state->data != NULL &&
        sdl3d_game_data_get_network_session_message(state->data, group, name, &message))
    {
        return message;
    }
    return fallback;
}

static int network_runtime_action_id(const pong_state *state, const char *name)
{
    int action_id = -1;
    if (state != NULL && state->data != NULL &&
        sdl3d_game_data_get_network_runtime_action(state->data, name, &action_id))
    {
        return action_id;
    }
    return -1;
}

static int network_runtime_signal_id(const pong_state *state, const char *name)
{
    int signal_id = -1;
    if (state != NULL && state->data != NULL &&
        sdl3d_game_data_get_network_runtime_signal(state->data, name, &signal_id))
    {
        return signal_id;
    }
    return -1;
}

static const char *network_disconnect_reason(const pong_state *state, const char *name, const char *fallback)
{
    return network_session_message(state, "disconnect_reasons", name, fallback);
}

static bool run_network_flow_event(sdl3d_game_context *ctx, pong_state *state, const char *event_name,
                                   const char *reason)
{
    char error[192] = {0};
    sdl3d_properties *payload = NULL;
    bool ok = false;

    if (state == NULL || state->data == NULL || event_name == NULL || event_name[0] == '\0')
    {
        return false;
    }

    payload = sdl3d_properties_create();
    if (payload == NULL)
    {
        return false;
    }
    sdl3d_properties_set_string(payload, "event", event_name);
    sdl3d_properties_set_string(payload, "reason", reason != NULL && reason[0] != '\0' ? reason : "");

    ok = sdl3d_game_data_run_network_session_flow_event(state->data, ctx, event_name, payload, error,
                                                        (int)sizeof(error));
    sdl3d_properties_destroy(payload);
    if (!ok)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong network flow event '%s' failed: %s", event_name,
                    error[0] != '\0' ? error : "unknown error");
    }
    return ok;
}

static const char *network_session_state_string(const pong_state *state, const char *key_name, const char *fallback)
{
    const sdl3d_properties *scene_state =
        state != NULL && state->data != NULL ? sdl3d_game_data_scene_state(state->data) : NULL;
    const char *key = network_session_state_key(state, key_name, key_name);
    return scene_state != NULL ? sdl3d_properties_get_string(scene_state, key, fallback) : fallback;
}

static bool active_scene_is(const pong_state *state, const char *scene_name, const char *fallback)
{
    const char *active_scene = active_scene_name(state);
    const char *expected_scene = network_session_scene(state, scene_name, fallback);
    return active_scene != NULL && expected_scene != NULL && SDL_strcmp(active_scene, expected_scene) == 0;
}

static bool is_direct_connect_scene(const pong_state *state)
{
    return active_scene_is(state, "direct_connect", "scene.multiplayer.direct_connect");
}

static bool is_multiplayer_lobby_scene(const pong_state *state)
{
    return active_scene_is(state, "host_lobby", "scene.multiplayer.lobby");
}

static bool is_multiplayer_discovery_scene(const pong_state *state)
{
    return active_scene_is(state, "discovery", "scene.multiplayer.discovery");
}

static bool is_multiplayer_play_scene(const pong_state *state)
{
    return active_scene_is(state, "play", "scene.play");
}

static bool is_local_match_mode(const pong_state *state)
{
    const char *match_mode = network_session_state_string(state, "match_mode", NULL);
    return match_mode != NULL &&
           SDL_strcmp(match_mode, network_session_state_value(state, "match_mode", "local", "local")) == 0;
}

static bool is_network_match_mode(const pong_state *state)
{
    const char *match_mode = network_session_state_string(state, "match_mode", NULL);
    return match_mode != NULL &&
           SDL_strcmp(match_mode, network_session_state_value(state, "match_mode", "network", "lan")) == 0;
}

static bool is_network_flow_host(const pong_state *state)
{
    const char *network_flow = network_session_state_string(state, "network_flow", NULL);
    return network_flow != NULL &&
           SDL_strcmp(network_flow, network_session_state_value(state, "network_flow", "host", "host")) == 0;
}

static bool is_network_flow_direct(const pong_state *state)
{
    const char *network_flow = network_session_state_string(state, "network_flow", NULL);
    return network_flow != NULL &&
           SDL_strcmp(network_flow, network_session_state_value(state, "network_flow", "direct", "direct")) == 0;
}

static const char *network_role_name(const pong_state *state)
{
    return network_session_state_string(state, "network_role", "none");
}

static bool is_network_role_host(const pong_state *state)
{
    return is_network_match_mode(state) &&
           SDL_strcmp(network_role_name(state), network_session_state_value(state, "network_role", "host", "host")) ==
               0;
}

static bool is_network_role_client(const pong_state *state)
{
    return is_network_match_mode(state) &&
           SDL_strcmp(network_role_name(state),
                      network_session_state_value(state, "network_role", "client", "client")) == 0;
}

static const char *network_scene_state_key(const pong_state *state, const char *scope, const char *name,
                                           const char *fallback)
{
    const char *key = NULL;
    if (state != NULL && state->data != NULL &&
        sdl3d_game_data_get_network_scene_state_key(state->data, scope, name, &key))
    {
        return key;
    }
    return fallback;
}

static void clear_network_action_overrides(pong_state *state)
{
    char error[160] = {0};
    const char *client_input_channel = NULL;
    if (state == NULL || state->input == NULL || state->data == NULL)
    {
        return;
    }

    if (!sdl3d_game_data_get_network_runtime_replication(state->data, PONG_NETWORK_BINDING_CLIENT_INPUT,
                                                         &client_input_channel))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong network binding missing: replication.%s",
                    PONG_NETWORK_BINDING_CLIENT_INPUT);
        return;
    }
    if (!sdl3d_game_data_clear_network_input_overrides(state->data, client_input_channel, state->input, error,
                                                       (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong network input override clear failed: %s",
                    error[0] != '\0' ? error : "unknown error");
    }
}

static void update_host_session_scene_state(pong_state *state)
{
    if (state == NULL || state->data == NULL)
    {
        return;
    }

    state->host_session = sdl3d_game_data_get_network_host_session(state->data, PONG_HOST_SESSION);
    (void)sdl3d_game_data_network_host_publish_status(
        state->data, PONG_HOST_SESSION, network_scene_state_key(state, "host", "status", "multiplayer_host_status"),
        network_scene_state_key(state, "host", "endpoint", "multiplayer_host_endpoint"),
        network_scene_state_key(state, "host", "peer", "multiplayer_host_client"),
        network_scene_state_key(state, "host", "connected", "multiplayer_host_connected"));
}

static void destroy_host_session_internal(pong_state *state, bool notify_peer)
{
    if (state != NULL && state->host_session != NULL)
    {
        if (notify_peer)
        {
            (void)send_network_control_packet_repeated(state, state->host_session, PONG_NETWORK_BINDING_DISCONNECT,
                                                       "host disconnect", 5);
        }
        (void)sdl3d_game_data_network_host_cancel(
            state->data, PONG_HOST_SESSION, network_scene_state_key(state, "host", "status", "multiplayer_host_status"),
            network_scene_state_key(state, "host", "endpoint", "multiplayer_host_endpoint"),
            network_scene_state_key(state, "host", "peer", "multiplayer_host_client"),
            network_scene_state_key(state, "host", "connected", "multiplayer_host_connected"), "Not hosting");
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

static void destroy_direct_connect_session_internal(pong_state *state, bool notify_peer)
{
    if (state != NULL)
    {
        state->direct_connect_session =
            state->data != NULL
                ? sdl3d_game_data_get_network_direct_connect_session(state->data, PONG_DIRECT_CONNECT_SESSION)
                : state->direct_connect_session;
    }
    if (state != NULL && state->direct_connect_session != NULL)
    {
        if (notify_peer)
        {
            (void)send_network_control_packet_repeated(state, state->direct_connect_session,
                                                       PONG_NETWORK_BINDING_DISCONNECT, "client disconnect", 5);
        }
        (void)sdl3d_game_data_network_direct_connect_cancel(
            state->data, PONG_DIRECT_CONNECT_SESSION, PONG_DIRECT_CONNECT_STATUS_KEY, PONG_DIRECT_CONNECT_STATE_KEY,
            PONG_DIRECT_CONNECT_CONNECTED_KEY, "Disconnected");
        state->direct_connect_session = NULL;
    }
}

static void destroy_direct_connect_session(pong_state *state)
{
    destroy_direct_connect_session_internal(state, true);
}

static bool start_host_session(pong_state *state)
{
    if (state == NULL)
    {
        return false;
    }

    if (state->host_session != NULL)
    {
        return true;
    }

    if (!sdl3d_game_data_network_host_start(
            state->data, PONG_HOST_SESSION, SDL3D_NETWORK_DEFAULT_PORT, "SDL3D Pong",
            network_scene_state_key(state, "host", "status", "multiplayer_host_status"),
            network_scene_state_key(state, "host", "endpoint", "multiplayer_host_endpoint"),
            network_scene_state_key(state, "host", "peer", "multiplayer_host_client"),
            network_scene_state_key(state, "host", "connected", "multiplayer_host_connected")))
    {
        SDL_snprintf(state->host_status, sizeof(state->host_status), "%s", SDL_GetError());
        SDL_snprintf(state->host_endpoint, sizeof(state->host_endpoint), "UDP %u",
                     (unsigned int)SDL3D_NETWORK_DEFAULT_PORT);
        update_host_session_scene_state(state);
        return false;
    }

    update_host_session_scene_state(state);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong host lobby session active");
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
    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(state->data);
    if (scene_state != NULL)
    {
        sdl3d_properties_set_string(scene_state, PONG_DIRECT_CONNECT_HOST_KEY, state->direct_connect_host);
        sdl3d_properties_set_string(scene_state, PONG_DIRECT_CONNECT_PORT_KEY, state->direct_connect_port);
        sdl3d_properties_set_string(scene_state, PONG_DIRECT_CONNECT_STATUS_KEY, state->direct_connect_status);
        sdl3d_properties_set_string(scene_state, PONG_DIRECT_CONNECT_STATE_KEY, "disconnected");
        sdl3d_properties_set_bool(scene_state, PONG_DIRECT_CONNECT_CONNECTED_KEY, false);
    }
}

static bool send_network_control_packet(pong_state *state, sdl3d_network_session *session, const char *binding,
                                        const char *label)
{
    char error[160] = {0};
    const Uint32 tick = (Uint32)SDL_GetTicks();
    if (state == NULL || state->data == NULL || session == NULL || binding == NULL || binding[0] == '\0')
    {
        return false;
    }

    if (!sdl3d_game_data_send_network_runtime_control(state->data, session, binding, tick, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong network control packet send failed: %s", error);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong sent network control: %s binding=%s",
                (unsigned long long)pong_log_timestamp_ms(), label != NULL ? label : "control", binding);
    return true;
}

static bool send_network_control_packet_repeated(pong_state *state, sdl3d_network_session *session, const char *binding,
                                                 const char *label, int count)
{
    bool sent_any = false;
    const int attempts = SDL_max(count, 1);

    for (int i = 0; i < attempts; ++i)
    {
        if (send_network_control_packet(state, session, binding, label))
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

    if (!send_network_control_packet(state, state->host_session, PONG_NETWORK_BINDING_START_GAME, "start game"))
    {
        SDL_snprintf(state->host_status, sizeof(state->host_status), "%s", SDL_GetError());
        update_host_session_scene_state(state);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong host requested multiplayer start");
    return true;
}

static bool start_selected_lobby_match(sdl3d_game_context *ctx, pong_state *state)
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

    return run_network_flow_event(ctx, state, "host_start_game", NULL);
}

static void run_network_disconnect_flow(sdl3d_game_context *ctx, pong_state *state, bool local_host, const char *reason)
{
    const char *event_name = NULL;
    if (state == NULL || state->data == NULL)
    {
        return;
    }

    if (is_multiplayer_play_scene(state))
    {
        state->network_match_termination_timer = 0.0f;
        event_name = local_host ? "host_match_terminated" : "client_match_terminated";
    }
    else
    {
        event_name = local_host ? "host_client_disconnected" : "client_connection_closed";
    }

    (void)run_network_flow_event(ctx, state, event_name,
                                 reason != NULL && reason[0] != '\0'
                                     ? reason
                                     : network_disconnect_reason(state, "peer_disconnected", "Peer disconnected"));
}

static bool start_direct_connect_session(pong_state *state)
{
    int port = 0;

    if (state == NULL || state->data == NULL)
    {
        return false;
    }

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(state->data);
    const char *host = scene_state != NULL
                           ? sdl3d_properties_get_string(scene_state, PONG_DIRECT_CONNECT_HOST_KEY, "127.0.0.1")
                           : "127.0.0.1";
    const char *port_text =
        scene_state != NULL ? sdl3d_properties_get_string(scene_state, PONG_DIRECT_CONNECT_PORT_KEY, NULL) : NULL;
    port = port_text != NULL ? SDL_atoi(port_text) : SDL3D_NETWORK_DEFAULT_PORT;
    if (!sdl3d_game_data_network_direct_connect_start(state->data, PONG_DIRECT_CONNECT_SESSION, host, port,
                                                      PONG_DIRECT_CONNECT_STATUS_KEY, PONG_DIRECT_CONNECT_STATE_KEY,
                                                      PONG_DIRECT_CONNECT_CONNECTED_KEY))
    {
        const char *status =
            scene_state != NULL
                ? sdl3d_properties_get_string(scene_state, PONG_DIRECT_CONNECT_STATUS_KEY, SDL_GetError())
                : SDL_GetError();
        SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "%s",
                     status != NULL && status[0] != '\0' ? status : "Connect failed");
        return false;
    }

    state->direct_connect_session =
        sdl3d_game_data_get_network_direct_connect_session(state->data, PONG_DIRECT_CONNECT_SESSION);
    const char *status = scene_state != NULL
                             ? sdl3d_properties_get_string(scene_state, PONG_DIRECT_CONNECT_STATUS_KEY, "Connecting")
                             : "Connecting";
    SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "%s", status);
    return true;
}

static void update_direct_connect_session_status(sdl3d_game_context *ctx, pong_state *state, float dt)
{
    if (state == NULL)
    {
        return;
    }

    state->direct_connect_session =
        state->data != NULL
            ? sdl3d_game_data_get_network_direct_connect_session(state->data, PONG_DIRECT_CONNECT_SESSION)
            : state->direct_connect_session;
    if (state->direct_connect_session != NULL)
    {
        const bool was_playing = is_multiplayer_play_scene(state);
        const bool playing = was_playing && is_network_role_client(state);
        const sdl3d_data_game_network_bindings bindings = pong_network_bindings();
        sdl3d_data_game_network_loop_result result;
        char error[192] = {0};
        const char *status = sdl3d_network_session_status(state->direct_connect_session);
        SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "%s",
                     status != NULL && status[0] != '\0' ? status : "Connecting");
        (void)sdl3d_game_data_network_direct_connect_publish_status(
            state->data, PONG_DIRECT_CONNECT_SESSION, PONG_DIRECT_CONNECT_STATUS_KEY, PONG_DIRECT_CONNECT_STATE_KEY,
            PONG_DIRECT_CONNECT_CONNECTED_KEY);

        if (!sdl3d_data_game_runtime_update_network_client_session(state->runtime, ctx, PONG_DIRECT_CONNECT_SESSION,
                                                                   &bindings, playing, true, dt, &result, error,
                                                                   (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong direct-connect network update failed: %s",
                        error[0] != '\0' ? error : "unknown error");
        }

        if (result.received_start_game)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong client received multiplayer start request");
            (void)run_network_flow_event(ctx, state, "client_start_game", NULL);
        }
        if (result.received_disconnect)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong client received host disconnect tick=%u",
                        (unsigned long long)pong_log_timestamp_ms(), (unsigned int)result.last_tick);
            run_network_disconnect_flow(ctx, state, false,
                                        network_disconnect_reason(state, "host_exited", "Host exited"));
            return;
        }
        if (result.applied_snapshot)
        {
            if (!was_playing)
            {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Pong client received authoritative state before start command; entering play scene");
                (void)run_network_flow_event(ctx, state, "client_state_before_start", NULL);
            }
            log_network_snapshot_diagnostic(state, result.last_tick, "client_snapshot_applied", "applied");
        }

        if (state->direct_connect_session == NULL)
        {
            return;
        }

        const sdl3d_network_state session_state = result.session_state;
        if (session_state == SDL3D_NETWORK_STATE_REJECTED || session_state == SDL3D_NETWORK_STATE_TIMED_OUT ||
            session_state == SDL3D_NETWORK_STATE_ERROR)
        {
            const char *reason = session_state == SDL3D_NETWORK_STATE_TIMED_OUT
                                     ? network_disconnect_reason(state, "host_timed_out", "Host timed out")
                                 : session_state == SDL3D_NETWORK_STATE_REJECTED
                                     ? network_disconnect_reason(state, "host_rejected", "Host rejected connection")
                                     : network_disconnect_reason(state, "host_error", "Host connection error");
            if (was_playing)
            {
                run_network_disconnect_flow(ctx, state, false, reason);
            }
            else
            {
                (void)run_network_flow_event(ctx, state, "client_connection_closed", reason);
            }
        }
    }
}

static void update_network_match_termination(sdl3d_game_context *ctx, pong_state *state, float dt)
{
    const int select_action = network_runtime_action_id(state, PONG_NETWORK_BINDING_MENU_SELECT);
    const sdl3d_properties *scene_state =
        state != NULL && state->data != NULL ? sdl3d_game_data_scene_state(state->data) : NULL;
    const bool active = scene_state != NULL
                            ? sdl3d_properties_get_bool(scene_state,
                                                        network_session_state_key(state, "match_termination_active",
                                                                                  "network_match_termination_active"),
                                                        false)
                            : false;

    if (state == NULL || !active)
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

    state->network_match_termination_timer = 0.0f;
    (void)run_network_flow_event(ctx, state, "network_match_termination_ack", NULL);
}

static void update_host_session_status(sdl3d_game_context *ctx, pong_state *state, float dt)
{
    if (state == NULL)
    {
        return;
    }

    if (state->host_session != NULL)
    {
        const sdl3d_data_game_network_bindings bindings = pong_network_bindings();
        sdl3d_data_game_network_loop_result result;
        char error[192] = {0};
        if (!sdl3d_data_game_runtime_update_network_host_session(state->runtime, ctx, PONG_HOST_SESSION, &bindings,
                                                                 is_multiplayer_play_scene(state), dt, &result, error,
                                                                 (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host session update failed: %s",
                        error[0] != '\0' ? error : SDL_GetError());
        }
        if (result.received_disconnect)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong host received client disconnect tick=%u",
                        (unsigned long long)pong_log_timestamp_ms(), (unsigned int)result.last_tick);
            run_network_disconnect_flow(ctx, state, true,
                                        network_disconnect_reason(state, "client_exited", "Client exited"));
        }
        if (result.received_pause_request)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong host accepted peer pause request tick=%u",
                        (unsigned long long)pong_log_timestamp_ms(), (unsigned int)result.last_tick);
        }
        if (result.received_resume_request)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[%llu ms] Pong host accepted peer resume request tick=%u",
                        (unsigned long long)pong_log_timestamp_ms(), (unsigned int)result.last_tick);
        }
        if (result.applied_input)
        {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Pong host applied remote input packet");
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
                 result.session_state != SDL3D_NETWORK_STATE_CONNECTED)
        {
            run_network_disconnect_flow(ctx, state, true,
                                        network_disconnect_reason(state, "client_timed_out", "Client timed out"));
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
    state->direct_connect_session =
        state->data != NULL
            ? sdl3d_game_data_get_network_direct_connect_session(state->data, PONG_DIRECT_CONNECT_SESSION)
            : state->direct_connect_session;
    state->host_session = state->data != NULL ? sdl3d_game_data_get_network_host_session(state->data, PONG_HOST_SESSION)
                                              : state->host_session;

    update_host_session_status(ctx, state, dt);

    if (state->host_session != NULL)
    {
        update_host_session_scene_state(state);
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

        update_direct_connect_session_status(ctx, state, dt);
    }
}

static void publish_multiplayer_state(sdl3d_game_context *ctx, pong_state *state)
{
    if (state == NULL || state->host_session == NULL || !is_multiplayer_play_scene(state) ||
        !is_network_role_host(state))
    {
        return;
    }

    const sdl3d_data_game_network_bindings bindings = pong_network_bindings();
    sdl3d_data_game_network_loop_result result;
    char error[192] = {0};
    if (!sdl3d_data_game_runtime_publish_network_host_snapshot(state->runtime, ctx, PONG_HOST_SESSION, &bindings,
                                                               &result, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host failed to publish multiplayer state: %s",
                    error[0] != '\0' ? error : "unknown error");
        return;
    }
    log_network_snapshot_diagnostic(state, result.last_tick, "host_snapshot_sent", "sent");
}

static void update_network_client_input_sensors(sdl3d_game_context *ctx, pong_state *state)
{
    const int camera_toggle_action = network_runtime_action_id(state, PONG_NETWORK_BINDING_CAMERA_TOGGLE);

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

static void refresh_local_play_input(pong_state *state)
{
    const char *active_scene = state != NULL && state->data != NULL ? sdl3d_game_data_active_scene(state->data) : NULL;
    if (state == NULL || state->input == NULL || active_scene == NULL ||
        SDL_strcmp(active_scene, network_session_scene(state, "play", "scene.play")) != 0)
    {
        return;
    }

    char error[256] = {0};
    const char *profile_name = NULL;
    bool applied = false;
    if (!sdl3d_data_game_runtime_refresh_input_profile_on_device_change(state->runtime, state->input, &profile_name,
                                                                        &applied, error, sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong input profile hotplug refresh failed: %s",
                    error[0] != '\0' ? error : "unknown error");
    }
    else if (applied)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong input profile refreshed: profile=%s role=%s",
                    profile_name != NULL ? profile_name : "<none>", network_role_name(state));
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

    (void)start_selected_lobby_match(NULL, state);
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

static bool mount_pong_assets_for_runtime(sdl3d_asset_resolver *assets, void *userdata, char *error, int error_size)
{
    (void)userdata;
    return mount_pong_assets(assets, error, error_size);
}

static bool init_game_data(sdl3d_game_context *ctx, pong_state *state)
{
    sdl3d_data_game_runtime_desc desc;
    char error[512] = {0};

    if (ctx == NULL || state == NULL)
    {
        return false;
    }

    sdl3d_data_game_runtime_desc_init(&desc);
    desc.session = ctx->session;
    desc.data_asset_path = SDL3D_PONG_DATA_ASSET_PATH;
    desc.media_dir = SDL3D_MEDIA_DIR;
    desc.mount_assets = mount_pong_assets_for_runtime;

    if (!sdl3d_data_game_runtime_create(&desc, &state->runtime, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong data load failed: %s", error);
        return false;
    }

    state->data = sdl3d_data_game_runtime_data(state->runtime);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong data loaded: asset=%s active_scene=%s", SDL3D_PONG_DATA_ASSET_PATH,
                sdl3d_game_data_active_scene(state->data) != NULL ? sdl3d_game_data_active_scene(state->data)
                                                                  : "<none>");

    return true;
}

static bool pong_init(sdl3d_game_context *ctx, void *userdata)
{
    pong_state *state = (pong_state *)userdata;
    SDL_zero(*state);

    if (!init_game_data(ctx, state))
    {
        return false;
    }
    reset_direct_connect_defaults(state);

    state->input = sdl3d_game_session_get_input(ctx->session);
    const int gamepad_count = state->input != NULL ? sdl3d_input_gamepad_count(state->input) : 0;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong gamepad count at init: %d", gamepad_count);
    for (int i = 0; i < gamepad_count; ++i)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Pong gamepad slot: slot=%d id=%d connected=%d", i,
                    sdl3d_input_gamepad_id_at(state->input, i), sdl3d_input_gamepad_is_connected(state->input, i));
    }
    state->lobby_start_connection = 0;
    state->host_start_signal_id = network_runtime_signal_id(state, PONG_NETWORK_BINDING_LOBBY_START);
    state->camera_toggle_signal_id = network_runtime_signal_id(state, PONG_NETWORK_BINDING_CAMERA_TOGGLE);
    SDL_snprintf(state->host_status, sizeof(state->host_status), "Not hosting");
    SDL_snprintf(state->host_endpoint, sizeof(state->host_endpoint), "UDP %u",
                 (unsigned int)SDL3D_NETWORK_DEFAULT_PORT);
    update_host_session_scene_state(state);
    if (state->input != NULL)
    {
        if (state->host_start_signal_id >= 0)
        {
            state->lobby_start_connection =
                sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(ctx->session), state->host_start_signal_id,
                                     on_multiplayer_lobby_signal, state);
        }
    }
    return true;
}

static void pong_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    pong_state *state = (pong_state *)userdata;
    refresh_local_play_input(state);
    update_network_match_termination(ctx, state, dt);
    update_multiplayer_sessions(ctx, state, dt);
    update_network_client_input_sensors(ctx, state);
    (void)sdl3d_data_game_runtime_update_frame(state->runtime, ctx, dt);
    publish_multiplayer_state(ctx, state);
}

static void pong_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    pong_state *state = (pong_state *)userdata;
    refresh_local_play_input(state);
    update_network_match_termination(ctx, state, real_dt);
    update_multiplayer_sessions(ctx, state, real_dt);
    update_network_client_input_sensors(ctx, state);
    (void)sdl3d_data_game_runtime_update_frame(state->runtime, ctx, real_dt);
    publish_multiplayer_state(ctx, state);
}

static void pong_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    pong_state *state = (pong_state *)userdata;
    (void)alpha;
    sdl3d_data_game_runtime_render(state->runtime, ctx);
}

static void pong_shutdown(sdl3d_game_context *ctx, void *userdata)
{
    pong_state *state = (pong_state *)userdata;

    if (state->lobby_start_connection > 0)
    {
        sdl3d_signal_disconnect(sdl3d_game_session_get_signal_bus(ctx->session), state->lobby_start_connection);
        state->lobby_start_connection = 0;
    }
    destroy_host_session(state);
    destroy_direct_connect_session(state);
    state->input = NULL;
    sdl3d_data_game_runtime_destroy(state->runtime);
    state->runtime = NULL;
    state->data = NULL;
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
    callbacks.tick = pong_tick;
    callbacks.pause_tick = pong_pause_tick;
    callbacks.render = pong_render;
    callbacks.shutdown = pong_shutdown;

    return sdl3d_run_game(&config, &callbacks, &state);
}

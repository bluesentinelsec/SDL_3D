#include "sdl3d/data_game.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include "sdl3d/game_presentation.h"
#include "sdl3d/input.h"
#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"

struct sdl3d_data_game_runtime
{
    sdl3d_asset_resolver *assets;
    sdl3d_game_data_runtime *data;
    sdl3d_game_data_font_cache font_cache;
    sdl3d_game_data_image_cache image_cache;
    sdl3d_game_data_particle_cache particle_cache;
    sdl3d_game_data_app_flow app_flow;
    sdl3d_game_data_frame_state frame_state;
    sdl3d_game_data_input_profile_refresh_state input_profile_refresh;
    sdl3d_game_session *session;
    int *haptics_signal_ids;
    int *haptics_connections;
    int haptics_connection_count;
    bool managed_network_enabled;
    int managed_network_lobby_start_signal_id;
    int managed_network_lobby_start_connection;
    int managed_network_camera_toggle_signal_id;
    bool managed_network_lobby_start_requested;
    float managed_network_termination_timer;
};

static const char SDL3D_MANAGED_NETWORK_HOST_SESSION[] = "host";
static const char SDL3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION[] = "direct_connect";
static const char SDL3D_MANAGED_NETWORK_BINDING_STATE_SNAPSHOT[] = "state_snapshot";
static const char SDL3D_MANAGED_NETWORK_BINDING_CLIENT_INPUT[] = "client_input";
static const char SDL3D_MANAGED_NETWORK_BINDING_START_GAME[] = "start_game";
static const char SDL3D_MANAGED_NETWORK_BINDING_PAUSE_REQUEST[] = "pause_request";
static const char SDL3D_MANAGED_NETWORK_BINDING_RESUME_REQUEST[] = "resume_request";
static const char SDL3D_MANAGED_NETWORK_BINDING_DISCONNECT[] = "disconnect";
static const char SDL3D_MANAGED_NETWORK_BINDING_MENU_SELECT[] = "menu_select";
static const char SDL3D_MANAGED_NETWORK_BINDING_CAMERA_TOGGLE[] = "camera_toggle";
static const char SDL3D_MANAGED_NETWORK_BINDING_LOBBY_START[] = "lobby_start";
static const char SDL3D_MANAGED_NETWORK_DIAGNOSTIC_SNAPSHOT[] = "multiplayer_state";

static void set_error(char *error_buffer, int error_buffer_size, const char *message)
{
    if (error_buffer != NULL && error_buffer_size > 0)
    {
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s", message != NULL ? message : "");
    }
}

static void set_errorf(char *error_buffer, int error_buffer_size, const char *fmt, const char *value)
{
    if (error_buffer != NULL && error_buffer_size > 0)
    {
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, fmt != NULL ? fmt : "%s", value != NULL ? value : "");
    }
}

static void network_loop_result_init(sdl3d_data_game_network_loop_result *result, sdl3d_network_session *session)
{
    if (result == NULL)
    {
        return;
    }

    SDL_zero(*result);
    result->session_state = session != NULL ? sdl3d_network_session_state(session) : SDL3D_NETWORK_STATE_DISCONNECTED;
}

static Uint32 data_game_input_tick(const sdl3d_data_game_runtime *runtime)
{
    sdl3d_input_manager *input =
        runtime != NULL && runtime->session != NULL ? sdl3d_game_session_get_input(runtime->session) : NULL;
    const sdl3d_input_snapshot *snapshot = input != NULL ? sdl3d_input_get_snapshot(input) : NULL;
    return snapshot != NULL ? (Uint32)SDL_max(snapshot->tick, 0) : (Uint32)SDL_GetTicks();
}

static bool data_game_binding_matches(const char *actual, const char *expected)
{
    return actual != NULL && expected != NULL && expected[0] != '\0' && SDL_strcmp(actual, expected) == 0;
}

static bool data_game_decode_runtime_control(sdl3d_data_game_runtime *runtime, const Uint8 *packet, int packet_size,
                                             const char **out_binding, sdl3d_game_data_network_control *out_control)
{
    char error[160] = {0};
    if (out_binding != NULL)
        *out_binding = NULL;
    if (runtime == NULL || runtime->data == NULL || packet == NULL || packet_size <= 0)
    {
        return false;
    }

    return sdl3d_game_data_decode_network_runtime_control(runtime->data, packet, (size_t)packet_size, out_binding,
                                                          out_control, error, (int)sizeof(error));
}

static bool data_game_send_runtime_control(sdl3d_data_game_runtime *runtime, sdl3d_network_session *session,
                                           const char *binding_name, Uint32 tick, char *error_buffer,
                                           int error_buffer_size)
{
    if (runtime == NULL || runtime->data == NULL || session == NULL || binding_name == NULL || binding_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "network control send requires runtime, session, and binding");
        return false;
    }

    return sdl3d_game_data_send_network_runtime_control(runtime->data, session, binding_name, tick, error_buffer,
                                                        error_buffer_size);
}

static bool data_game_sync_network_pause_from_context(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx,
                                                      char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || runtime->data == NULL || ctx == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network pause sync requires runtime and context");
        return false;
    }

    return sdl3d_game_data_set_network_runtime_pause_state(runtime->data, ctx->paused, error_buffer, error_buffer_size);
}

static bool data_game_sync_context_pause_from_network(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx,
                                                      char *error_buffer, int error_buffer_size)
{
    bool paused = false;
    if (runtime == NULL || runtime->data == NULL || ctx == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network pause sync requires runtime and context");
        return false;
    }
    if (!sdl3d_game_data_get_network_runtime_pause_state(runtime->data, &paused, error_buffer, error_buffer_size))
    {
        return false;
    }
    ctx->paused = paused;
    return true;
}

static bool haptics_signal_connected(const sdl3d_data_game_runtime *runtime, int signal_id)
{
    if (runtime == NULL || signal_id < 0)
    {
        return true;
    }

    for (int i = 0; i < runtime->haptics_connection_count; ++i)
    {
        if (runtime->haptics_signal_ids != NULL && runtime->haptics_signal_ids[i] == signal_id)
        {
            return true;
        }
    }
    return false;
}

static void on_haptics_policy_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    sdl3d_data_game_runtime *runtime = (sdl3d_data_game_runtime *)userdata;
    sdl3d_input_manager *input =
        runtime != NULL && runtime->session != NULL ? sdl3d_game_session_get_input(runtime->session) : NULL;
    if (runtime == NULL || runtime->data == NULL || input == NULL)
    {
        return;
    }

    const int policy_count = sdl3d_game_data_haptics_policy_count(runtime->data);
    for (int i = 0; i < policy_count; ++i)
    {
        sdl3d_game_data_haptics_policy policy;
        if (!sdl3d_game_data_match_haptics_policy(runtime->data, i, signal_id, payload, &policy))
        {
            continue;
        }
        if (!sdl3d_input_rumble_all_gamepads(input, policy.low_frequency, policy.high_frequency, policy.duration_ms))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SDL3D haptics policy '%s' requested rumble but no gamepad accepted it",
                        policy.name != NULL ? policy.name : "<unnamed>");
        }
    }
}

static bool connect_haptics_policies(sdl3d_data_game_runtime *runtime)
{
    if (runtime == NULL || runtime->data == NULL || runtime->session == NULL)
    {
        return true;
    }

    sdl3d_signal_bus *bus = sdl3d_game_session_get_signal_bus(runtime->session);
    if (bus == NULL || sdl3d_game_session_get_input(runtime->session) == NULL)
    {
        return true;
    }

    const int policy_count = sdl3d_game_data_haptics_policy_count(runtime->data);
    if (policy_count <= 0)
    {
        return true;
    }

    runtime->haptics_signal_ids = (int *)SDL_calloc((size_t)policy_count, sizeof(*runtime->haptics_signal_ids));
    runtime->haptics_connections = (int *)SDL_calloc((size_t)policy_count, sizeof(*runtime->haptics_connections));
    if (runtime->haptics_signal_ids == NULL || runtime->haptics_connections == NULL)
    {
        SDL_free(runtime->haptics_signal_ids);
        SDL_free(runtime->haptics_connections);
        runtime->haptics_signal_ids = NULL;
        runtime->haptics_connections = NULL;
        SDL_OutOfMemory();
        return false;
    }

    for (int i = 0; i < policy_count; ++i)
    {
        sdl3d_game_data_haptics_policy policy;
        if (!sdl3d_game_data_get_haptics_policy_at(runtime->data, i, &policy) ||
            haptics_signal_connected(runtime, policy.signal_id))
        {
            continue;
        }

        const int connection = sdl3d_signal_connect(bus, policy.signal_id, on_haptics_policy_signal, runtime);
        if (connection > 0)
        {
            runtime->haptics_signal_ids[runtime->haptics_connection_count] = policy.signal_id;
            runtime->haptics_connections[runtime->haptics_connection_count] = connection;
            ++runtime->haptics_connection_count;
        }
    }
    return true;
}

static void disconnect_haptics_policies(sdl3d_data_game_runtime *runtime)
{
    if (runtime == NULL || runtime->session == NULL)
    {
        return;
    }

    sdl3d_signal_bus *bus = sdl3d_game_session_get_signal_bus(runtime->session);
    if (bus != NULL)
    {
        for (int i = 0; i < runtime->haptics_connection_count; ++i)
        {
            if (runtime->haptics_connections != NULL && runtime->haptics_connections[i] > 0)
            {
                sdl3d_signal_disconnect(bus, runtime->haptics_connections[i]);
            }
        }
    }
    SDL_free(runtime->haptics_signal_ids);
    SDL_free(runtime->haptics_connections);
    runtime->haptics_signal_ids = NULL;
    runtime->haptics_connections = NULL;
    runtime->haptics_connection_count = 0;
}

static sdl3d_data_game_network_bindings managed_network_bindings(void)
{
    sdl3d_data_game_network_bindings bindings;
    SDL_zero(bindings);
    bindings.state_snapshot = SDL3D_MANAGED_NETWORK_BINDING_STATE_SNAPSHOT;
    bindings.client_input = SDL3D_MANAGED_NETWORK_BINDING_CLIENT_INPUT;
    bindings.start_game = SDL3D_MANAGED_NETWORK_BINDING_START_GAME;
    bindings.pause_request = SDL3D_MANAGED_NETWORK_BINDING_PAUSE_REQUEST;
    bindings.resume_request = SDL3D_MANAGED_NETWORK_BINDING_RESUME_REQUEST;
    bindings.disconnect = SDL3D_MANAGED_NETWORK_BINDING_DISCONNECT;
    return bindings;
}

static const char *managed_network_session_scene(const sdl3d_data_game_runtime *runtime, const char *name,
                                                 const char *fallback)
{
    const char *scene = NULL;
    if (runtime != NULL && runtime->data != NULL &&
        sdl3d_game_data_get_network_session_scene(runtime->data, name, &scene))
    {
        return scene;
    }
    return fallback;
}

static const char *managed_network_session_state_key(const sdl3d_data_game_runtime *runtime, const char *name,
                                                     const char *fallback)
{
    const char *key = NULL;
    if (runtime != NULL && runtime->data != NULL &&
        sdl3d_game_data_get_network_session_state_key(runtime->data, name, &key))
    {
        return key;
    }
    return fallback;
}

static const char *managed_network_session_state_value(const sdl3d_data_game_runtime *runtime, const char *group,
                                                       const char *name, const char *fallback)
{
    const char *value = NULL;
    if (runtime != NULL && runtime->data != NULL &&
        sdl3d_game_data_get_network_session_state_value(runtime->data, group, name, &value))
    {
        return value;
    }
    return fallback;
}

static const char *managed_network_message(const sdl3d_data_game_runtime *runtime, const char *group, const char *name,
                                           const char *fallback)
{
    const char *message = NULL;
    if (runtime != NULL && runtime->data != NULL &&
        sdl3d_game_data_get_network_session_message(runtime->data, group, name, &message))
    {
        return message;
    }
    return fallback;
}

static const char *managed_network_scene_state_key(const sdl3d_data_game_runtime *runtime, const char *scope,
                                                   const char *name, const char *fallback)
{
    const char *key = NULL;
    if (runtime != NULL && runtime->data != NULL &&
        sdl3d_game_data_get_network_scene_state_key(runtime->data, scope, name, &key))
    {
        return key;
    }
    return fallback;
}

static const char *managed_network_scene_state_string(const sdl3d_data_game_runtime *runtime, const char *key_name,
                                                      const char *fallback)
{
    const sdl3d_properties *scene_state =
        runtime != NULL && runtime->data != NULL ? sdl3d_game_data_scene_state(runtime->data) : NULL;
    const char *key = managed_network_session_state_key(runtime, key_name, key_name);
    return scene_state != NULL ? sdl3d_properties_get_string(scene_state, key, fallback) : fallback;
}

static bool managed_network_active_scene_is(const sdl3d_data_game_runtime *runtime, const char *scene_name,
                                            const char *fallback)
{
    const char *active_scene =
        runtime != NULL && runtime->data != NULL ? sdl3d_game_data_active_scene(runtime->data) : NULL;
    const char *expected_scene = managed_network_session_scene(runtime, scene_name, fallback);
    return active_scene != NULL && expected_scene != NULL && SDL_strcmp(active_scene, expected_scene) == 0;
}

static bool managed_network_is_play_scene(const sdl3d_data_game_runtime *runtime)
{
    return managed_network_active_scene_is(runtime, "play", "scene.play");
}

static bool managed_network_keep_alive_scene_matches(const sdl3d_data_game_runtime *runtime, const char *session_name)
{
    const char *active_scene =
        runtime != NULL && runtime->data != NULL ? sdl3d_game_data_active_scene(runtime->data) : NULL;
    return sdl3d_game_data_network_managed_keep_alive_scene_matches(runtime != NULL ? runtime->data : NULL,
                                                                    session_name, active_scene);
}

static bool managed_network_is_network_match(const sdl3d_data_game_runtime *runtime)
{
    const char *match_mode = managed_network_scene_state_string(runtime, "match_mode", NULL);
    return match_mode != NULL &&
           SDL_strcmp(match_mode, managed_network_session_state_value(runtime, "match_mode", "network", "lan")) == 0;
}

static bool managed_network_is_role_host(const sdl3d_data_game_runtime *runtime)
{
    const char *network_role = managed_network_scene_state_string(runtime, "network_role", "none");
    return managed_network_is_network_match(runtime) &&
           SDL_strcmp(network_role, managed_network_session_state_value(runtime, "network_role", "host", "host")) == 0;
}

static bool managed_network_is_role_client(const sdl3d_data_game_runtime *runtime)
{
    const char *network_role = managed_network_scene_state_string(runtime, "network_role", "none");
    return managed_network_is_network_match(runtime) &&
           SDL_strcmp(network_role, managed_network_session_state_value(runtime, "network_role", "client", "client")) ==
               0;
}

static int managed_network_action_id(const sdl3d_data_game_runtime *runtime, const char *binding_name)
{
    int action_id = -1;
    if (runtime != NULL && runtime->data != NULL &&
        sdl3d_game_data_get_network_runtime_action(runtime->data, binding_name, &action_id))
    {
        return action_id;
    }
    return -1;
}

static bool managed_network_run_flow_event(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx,
                                           const char *event_name, const char *reason)
{
    char error[192] = {0};
    sdl3d_properties *payload = NULL;
    bool ok = false;

    if (runtime == NULL || runtime->data == NULL || event_name == NULL || event_name[0] == '\0')
        return false;

    payload = sdl3d_properties_create();
    if (payload == NULL)
        return false;
    sdl3d_properties_set_string(payload, "event", event_name);
    sdl3d_properties_set_string(payload, "reason", reason != NULL ? reason : "");

    ok = sdl3d_game_data_run_network_session_flow_event(runtime->data, ctx, event_name, payload, error,
                                                        (int)sizeof(error));
    sdl3d_properties_destroy(payload);
    if (!ok)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D managed network event '%s' failed: %s", event_name,
                    error[0] != '\0' ? error : "unknown error");
    }
    return ok;
}

static bool managed_network_send_control(sdl3d_data_game_runtime *runtime, sdl3d_network_session *session,
                                         const char *binding_name, const char *label)
{
    char error[160] = {0};
    const Uint32 tick = data_game_input_tick(runtime);
    if (!data_game_send_runtime_control(runtime, session, binding_name, tick, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D managed network control send failed: %s",
                    error[0] != '\0' ? error : "unknown error");
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D managed network sent control: %s binding=%s",
                label != NULL ? label : "control", binding_name != NULL ? binding_name : "<null>");
    return true;
}

static bool managed_network_send_control_repeated(sdl3d_data_game_runtime *runtime, sdl3d_network_session *session,
                                                  const char *binding_name, const char *label, int count)
{
    bool sent_any = false;
    const int attempts = SDL_max(count, 1);
    for (int i = 0; i < attempts; ++i)
    {
        if (managed_network_send_control(runtime, session, binding_name, label))
            sent_any = true;
        if (session != NULL)
            (void)sdl3d_network_session_update(session, 0.0f);
    }
    return sent_any;
}

static void managed_network_publish_host_status(sdl3d_data_game_runtime *runtime)
{
    if (runtime == NULL || runtime->data == NULL)
        return;

    (void)sdl3d_game_data_network_host_publish_status(
        runtime->data, SDL3D_MANAGED_NETWORK_HOST_SESSION,
        managed_network_scene_state_key(runtime, "host", "status", "multiplayer_host_status"),
        managed_network_scene_state_key(runtime, "host", "endpoint", "multiplayer_host_endpoint"),
        managed_network_scene_state_key(runtime, "host", "peer", "multiplayer_host_client"),
        managed_network_scene_state_key(runtime, "host", "connected", "multiplayer_host_connected"));
}

static void managed_network_cancel_host(sdl3d_data_game_runtime *runtime, bool notify_peer, const char *status)
{
    sdl3d_network_session *session =
        runtime != NULL && runtime->data != NULL
            ? sdl3d_game_data_get_network_host_session(runtime->data, SDL3D_MANAGED_NETWORK_HOST_SESSION)
            : NULL;
    if (runtime == NULL || runtime->data == NULL)
        return;
    if (session != NULL && notify_peer)
    {
        (void)managed_network_send_control_repeated(runtime, session, SDL3D_MANAGED_NETWORK_BINDING_DISCONNECT,
                                                    "host disconnect", 5);
    }
    (void)sdl3d_game_data_network_host_cancel(
        runtime->data, SDL3D_MANAGED_NETWORK_HOST_SESSION,
        managed_network_scene_state_key(runtime, "host", "status", "multiplayer_host_status"),
        managed_network_scene_state_key(runtime, "host", "endpoint", "multiplayer_host_endpoint"),
        managed_network_scene_state_key(runtime, "host", "peer", "multiplayer_host_client"),
        managed_network_scene_state_key(runtime, "host", "connected", "multiplayer_host_connected"),
        status != NULL ? status : "Not hosting");
}

static void managed_network_publish_direct_connect_status(sdl3d_data_game_runtime *runtime)
{
    if (runtime == NULL || runtime->data == NULL)
        return;

    (void)sdl3d_game_data_network_direct_connect_publish_status(
        runtime->data, SDL3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION,
        managed_network_scene_state_key(runtime, "direct_connect", "status", "direct_connect_status"),
        managed_network_scene_state_key(runtime, "direct_connect", "state", "direct_connect_state"),
        managed_network_scene_state_key(runtime, "direct_connect", "connected", "direct_connect_connected"));
}

static void managed_network_cancel_direct_connect(sdl3d_data_game_runtime *runtime, bool notify_peer,
                                                  const char *status)
{
    sdl3d_network_session *session = runtime != NULL && runtime->data != NULL
                                         ? sdl3d_game_data_get_network_direct_connect_session(
                                               runtime->data, SDL3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION)
                                         : NULL;
    if (runtime == NULL || runtime->data == NULL)
        return;
    if (session != NULL && notify_peer)
    {
        (void)managed_network_send_control_repeated(runtime, session, SDL3D_MANAGED_NETWORK_BINDING_DISCONNECT,
                                                    "client disconnect", 5);
    }
    (void)sdl3d_game_data_network_direct_connect_cancel(
        runtime->data, SDL3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION,
        managed_network_scene_state_key(runtime, "direct_connect", "status", "direct_connect_status"),
        managed_network_scene_state_key(runtime, "direct_connect", "state", "direct_connect_state"),
        managed_network_scene_state_key(runtime, "direct_connect", "connected", "direct_connect_connected"),
        status != NULL ? status : "Disconnected");
}

static void managed_network_disconnect_flow(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx, bool local_host,
                                            const char *reason)
{
    const char *event_name = NULL;
    if (runtime == NULL || runtime->data == NULL)
        return;

    if (managed_network_is_play_scene(runtime))
    {
        runtime->managed_network_termination_timer = 0.0f;
        event_name = local_host ? "host_match_terminated" : "client_match_terminated";
    }
    else
    {
        event_name = local_host ? "host_client_disconnected" : "client_connection_closed";
    }

    (void)managed_network_run_flow_event(
        runtime, ctx, event_name,
        reason != NULL && reason[0] != '\0'
            ? reason
            : managed_network_message(runtime, "disconnect_reasons", "peer_disconnected", "Peer disconnected"));
}

static void managed_network_lobby_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    sdl3d_data_game_runtime *runtime = (sdl3d_data_game_runtime *)userdata;
    (void)payload;
    if (runtime == NULL || signal_id != runtime->managed_network_lobby_start_signal_id)
        return;
    runtime->managed_network_lobby_start_requested = true;
}

static bool connect_managed_network(sdl3d_data_game_runtime *runtime)
{
    if (runtime == NULL || runtime->data == NULL || runtime->session == NULL || !runtime->managed_network_enabled)
        return true;

    sdl3d_signal_bus *bus = sdl3d_game_session_get_signal_bus(runtime->session);
    if (bus == NULL)
        return true;

    runtime->managed_network_lobby_start_signal_id = -1;
    runtime->managed_network_camera_toggle_signal_id = -1;
    (void)sdl3d_game_data_get_network_runtime_signal(runtime->data, SDL3D_MANAGED_NETWORK_BINDING_LOBBY_START,
                                                     &runtime->managed_network_lobby_start_signal_id);
    (void)sdl3d_game_data_get_network_runtime_signal(runtime->data, SDL3D_MANAGED_NETWORK_BINDING_CAMERA_TOGGLE,
                                                     &runtime->managed_network_camera_toggle_signal_id);

    if (runtime->managed_network_lobby_start_signal_id >= 0)
    {
        runtime->managed_network_lobby_start_connection = sdl3d_signal_connect(
            bus, runtime->managed_network_lobby_start_signal_id, managed_network_lobby_signal, runtime);
        if (runtime->managed_network_lobby_start_connection == 0)
            return false;
    }
    return true;
}

static void disconnect_managed_network(sdl3d_data_game_runtime *runtime)
{
    if (runtime == NULL)
        return;

    if (runtime->managed_network_enabled && runtime->data != NULL)
    {
        if (sdl3d_game_data_get_network_host_session(runtime->data, SDL3D_MANAGED_NETWORK_HOST_SESSION) != NULL)
            managed_network_cancel_host(runtime, true, "Not hosting");
        if (sdl3d_game_data_get_network_direct_connect_session(runtime->data,
                                                               SDL3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION) != NULL)
        {
            managed_network_cancel_direct_connect(runtime, true, "Disconnected");
        }
    }

    if (runtime->session != NULL && runtime->managed_network_lobby_start_connection > 0)
    {
        sdl3d_signal_disconnect(sdl3d_game_session_get_signal_bus(runtime->session),
                                runtime->managed_network_lobby_start_connection);
    }
    runtime->managed_network_lobby_start_connection = 0;
    runtime->managed_network_lobby_start_signal_id = -1;
    runtime->managed_network_camera_toggle_signal_id = -1;
    runtime->managed_network_lobby_start_requested = false;
}

static void managed_network_update_termination_ack(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx, float dt)
{
    const sdl3d_properties *scene_state =
        runtime != NULL && runtime->data != NULL ? sdl3d_game_data_scene_state(runtime->data) : NULL;
    const char *active_key =
        managed_network_session_state_key(runtime, "match_termination_active", "network_match_termination_active");
    const bool active = scene_state != NULL ? sdl3d_properties_get_bool(scene_state, active_key, false) : false;

    if (runtime == NULL || !active)
    {
        if (runtime != NULL)
            runtime->managed_network_termination_timer = 0.0f;
        return;
    }

    if (ctx != NULL)
        ctx->paused = true;

    runtime->managed_network_termination_timer += SDL_max(dt, 0.0f);
    float acknowledge_delay = 3.0f;
    (void)sdl3d_game_data_get_network_managed_termination_ack_delay(runtime->data, &acknowledge_delay);
    if (runtime->managed_network_termination_timer < acknowledge_delay)
        return;

    sdl3d_input_manager *input = runtime->session != NULL ? sdl3d_game_session_get_input(runtime->session) : NULL;
    const int select_action = managed_network_action_id(runtime, SDL3D_MANAGED_NETWORK_BINDING_MENU_SELECT);
    if (input == NULL || select_action < 0 || !sdl3d_input_is_pressed(input, select_action))
        return;

    runtime->managed_network_termination_timer = 0.0f;
    (void)managed_network_run_flow_event(runtime, ctx, "network_match_termination_ack", NULL);
}

static void managed_network_process_lobby_start(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx)
{
    if (runtime == NULL || runtime->data == NULL || !runtime->managed_network_lobby_start_requested)
        return;

    runtime->managed_network_lobby_start_requested = false;
    sdl3d_network_session *session =
        sdl3d_game_data_get_network_host_session(runtime->data, SDL3D_MANAGED_NETWORK_HOST_SESSION);
    if (session == NULL || !sdl3d_network_session_is_connected(session))
    {
        managed_network_publish_host_status(runtime);
        return;
    }

    if (managed_network_send_control(runtime, session, SDL3D_MANAGED_NETWORK_BINDING_START_GAME, "start game"))
    {
        (void)managed_network_run_flow_event(runtime, ctx, "host_start_game", NULL);
    }
}

static void managed_network_update_host(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx, float dt)
{
    if (runtime == NULL || runtime->data == NULL)
        return;

    sdl3d_network_session *session =
        sdl3d_game_data_get_network_host_session(runtime->data, SDL3D_MANAGED_NETWORK_HOST_SESSION);
    if (session == NULL)
        return;

    const sdl3d_data_game_network_bindings bindings = managed_network_bindings();
    sdl3d_data_game_network_loop_result result;
    char error[192] = {0};
    if (!sdl3d_data_game_runtime_update_network_host_session(runtime, ctx, SDL3D_MANAGED_NETWORK_HOST_SESSION,
                                                             &bindings, managed_network_is_play_scene(runtime), dt,
                                                             &result, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D managed host session update failed: %s",
                    error[0] != '\0' ? error : SDL_GetError());
    }

    if (result.received_disconnect)
    {
        managed_network_disconnect_flow(
            runtime, ctx, true,
            managed_network_message(runtime, "disconnect_reasons", "client_exited", "Client exited"));
    }
    else if (managed_network_is_play_scene(runtime) && result.session_state != SDL3D_NETWORK_STATE_CONNECTED)
    {
        managed_network_disconnect_flow(
            runtime, ctx, true,
            managed_network_message(runtime, "disconnect_reasons", "client_timed_out", "Client timed out"));
    }

    managed_network_publish_host_status(runtime);

    session = sdl3d_game_data_get_network_host_session(runtime->data, SDL3D_MANAGED_NETWORK_HOST_SESSION);
    if (session == NULL)
        return;

    const bool keep_host_session =
        managed_network_keep_alive_scene_matches(runtime, SDL3D_MANAGED_NETWORK_HOST_SESSION) &&
        (!managed_network_is_play_scene(runtime) || managed_network_is_role_host(runtime));
    if (!keep_host_session)
        managed_network_cancel_host(runtime, true, "Not hosting");
}

static void managed_network_update_direct_connect(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx, float dt)
{
    if (runtime == NULL || runtime->data == NULL)
        return;

    sdl3d_network_session *session =
        sdl3d_game_data_get_network_direct_connect_session(runtime->data, SDL3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION);
    if (session == NULL)
        return;

    const bool was_playing = managed_network_is_play_scene(runtime);
    const bool playing = was_playing && managed_network_is_role_client(runtime);
    const bool keep_direct_connect_session =
        managed_network_keep_alive_scene_matches(runtime, SDL3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION) &&
        (!managed_network_is_play_scene(runtime) || managed_network_is_role_client(runtime));
    if (!keep_direct_connect_session)
    {
        managed_network_cancel_direct_connect(runtime, true, "Disconnected");
        return;
    }

    managed_network_publish_direct_connect_status(runtime);

    const sdl3d_data_game_network_bindings bindings = managed_network_bindings();
    sdl3d_data_game_network_loop_result result;
    char error[192] = {0};
    if (!sdl3d_data_game_runtime_update_network_client_session(runtime, ctx,
                                                               SDL3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION, &bindings,
                                                               playing, true, dt, &result, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D managed direct-connect update failed: %s",
                    error[0] != '\0' ? error : "unknown error");
    }

    if (result.received_start_game)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D managed client received start-game control");
        (void)managed_network_run_flow_event(runtime, ctx, "client_start_game", NULL);
    }
    if (result.received_disconnect)
    {
        managed_network_disconnect_flow(
            runtime, ctx, false, managed_network_message(runtime, "disconnect_reasons", "host_exited", "Host exited"));
        return;
    }
    if (result.applied_snapshot)
    {
        if (!was_playing)
            (void)managed_network_run_flow_event(runtime, ctx, "client_state_before_start", NULL);
        (void)sdl3d_game_data_log_network_snapshot_diagnostic(runtime->data, SDL3D_MANAGED_NETWORK_DIAGNOSTIC_SNAPSHOT,
                                                              result.last_tick, "client_snapshot_applied", "applied",
                                                              NULL, error, (int)sizeof(error));
    }

    session =
        sdl3d_game_data_get_network_direct_connect_session(runtime->data, SDL3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION);
    if (session == NULL)
        return;

    const sdl3d_network_state state = result.session_state;
    if (state == SDL3D_NETWORK_STATE_REJECTED || state == SDL3D_NETWORK_STATE_TIMED_OUT ||
        state == SDL3D_NETWORK_STATE_ERROR)
    {
        const char *reason =
            state == SDL3D_NETWORK_STATE_TIMED_OUT
                ? managed_network_message(runtime, "disconnect_reasons", "host_timed_out", "Host timed out")
            : state == SDL3D_NETWORK_STATE_REJECTED
                ? managed_network_message(runtime, "disconnect_reasons", "host_rejected", "Host rejected connection")
                : managed_network_message(runtime, "disconnect_reasons", "host_error", "Host connection error");
        if (was_playing)
            managed_network_disconnect_flow(runtime, ctx, false, reason);
        else
            (void)managed_network_run_flow_event(runtime, ctx, "client_connection_closed", reason);
    }
}

static void managed_network_update_client_sensors(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx)
{
    if (runtime == NULL || runtime->data == NULL || ctx == NULL || !managed_network_is_play_scene(runtime) ||
        !managed_network_is_role_client(runtime) || runtime->managed_network_camera_toggle_signal_id < 0)
    {
        return;
    }

    sdl3d_input_manager *input = runtime->session != NULL ? sdl3d_game_session_get_input(runtime->session) : NULL;
    const int camera_toggle_action = managed_network_action_id(runtime, SDL3D_MANAGED_NETWORK_BINDING_CAMERA_TOGGLE);
    if (input != NULL && camera_toggle_action >= 0 && sdl3d_input_is_pressed(input, camera_toggle_action))
    {
        sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(ctx->session),
                          runtime->managed_network_camera_toggle_signal_id, NULL);
    }
}

static void managed_network_update_before_frame(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx, float dt)
{
    if (runtime == NULL || !runtime->managed_network_enabled)
        return;

    managed_network_process_lobby_start(runtime, ctx);
    managed_network_update_termination_ack(runtime, ctx, dt);
    managed_network_update_host(runtime, ctx, dt);
    managed_network_update_direct_connect(runtime, ctx, dt);
    managed_network_update_client_sensors(runtime, ctx);
}

static void managed_network_update_after_frame(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx)
{
    if (runtime == NULL || runtime->data == NULL || !runtime->managed_network_enabled)
        return;

    sdl3d_network_session *session =
        sdl3d_game_data_get_network_host_session(runtime->data, SDL3D_MANAGED_NETWORK_HOST_SESSION);
    if (session == NULL || !sdl3d_network_session_is_connected(session) || !managed_network_is_play_scene(runtime) ||
        !managed_network_is_role_host(runtime))
    {
        return;
    }

    const sdl3d_data_game_network_bindings bindings = managed_network_bindings();
    sdl3d_data_game_network_loop_result result;
    char error[192] = {0};
    if (!sdl3d_data_game_runtime_publish_network_host_snapshot(runtime, ctx, SDL3D_MANAGED_NETWORK_HOST_SESSION,
                                                               &bindings, &result, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D managed host snapshot publish failed: %s",
                    error[0] != '\0' ? error : "unknown error");
        return;
    }
    (void)sdl3d_game_data_log_network_snapshot_diagnostic(runtime->data, SDL3D_MANAGED_NETWORK_DIAGNOSTIC_SNAPSHOT,
                                                          result.last_tick, "host_snapshot_sent", "sent", NULL, error,
                                                          (int)sizeof(error));
}

void sdl3d_data_game_runtime_desc_init(sdl3d_data_game_runtime_desc *desc)
{
    if (desc == NULL)
    {
        return;
    }
    SDL_zero(*desc);
}

bool sdl3d_data_game_runtime_create(const sdl3d_data_game_runtime_desc *desc, sdl3d_data_game_runtime **out_runtime,
                                    char *error_buffer, int error_buffer_size)
{
    char load_error[512] = {0};
    sdl3d_data_game_runtime *runtime = NULL;

    if (out_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "data-game runtime create requires out_runtime");
        return false;
    }
    *out_runtime = NULL;

    if (desc == NULL || desc->session == NULL || desc->data_asset_path == NULL || desc->data_asset_path[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "data-game runtime requires session and data_asset_path");
        return false;
    }

    runtime = (sdl3d_data_game_runtime *)SDL_calloc(1, sizeof(*runtime));
    if (runtime == NULL)
    {
        SDL_OutOfMemory();
        set_error(error_buffer, error_buffer_size, SDL_GetError());
        return false;
    }
    runtime->session = desc->session;
    runtime->managed_network_enabled = desc->enable_managed_network;
    runtime->managed_network_lobby_start_signal_id = -1;
    runtime->managed_network_camera_toggle_signal_id = -1;
    sdl3d_game_data_font_cache_init(&runtime->font_cache, desc->media_dir);
    sdl3d_game_data_particle_cache_init(&runtime->particle_cache);
    sdl3d_game_data_app_flow_init(&runtime->app_flow);
    sdl3d_game_data_frame_state_init(&runtime->frame_state);
    sdl3d_game_data_input_profile_refresh_state_init(&runtime->input_profile_refresh);

    runtime->assets = sdl3d_asset_resolver_create();
    if (runtime->assets == NULL)
    {
        set_error(error_buffer, error_buffer_size, SDL_GetError());
        sdl3d_data_game_runtime_destroy(runtime);
        return false;
    }

    if (desc->mount_assets != NULL &&
        !desc->mount_assets(runtime->assets, desc->mount_userdata, load_error, (int)sizeof(load_error)))
    {
        set_error(error_buffer, error_buffer_size, load_error[0] != '\0' ? load_error : "asset mount failed");
        sdl3d_data_game_runtime_destroy(runtime);
        return false;
    }

    if (!sdl3d_game_data_load_asset(runtime->assets, desc->data_asset_path, desc->session, &runtime->data, load_error,
                                    (int)sizeof(load_error)))
    {
        set_error(error_buffer, error_buffer_size, load_error[0] != '\0' ? load_error : "game data load failed");
        sdl3d_data_game_runtime_destroy(runtime);
        return false;
    }
    runtime->managed_network_enabled =
        runtime->managed_network_enabled && sdl3d_game_data_network_managed_runtime_enabled(runtime->data);

    sdl3d_game_data_image_cache_init(&runtime->image_cache, runtime->assets);
    if (!sdl3d_game_data_app_flow_start(&runtime->app_flow, runtime->data))
    {
        set_error(error_buffer, error_buffer_size, SDL_GetError());
        sdl3d_data_game_runtime_destroy(runtime);
        return false;
    }
    if (!connect_haptics_policies(runtime))
    {
        set_error(error_buffer, error_buffer_size, SDL_GetError());
        sdl3d_data_game_runtime_destroy(runtime);
        return false;
    }
    if (!connect_managed_network(runtime))
    {
        set_error(error_buffer, error_buffer_size, SDL_GetError());
        sdl3d_data_game_runtime_destroy(runtime);
        return false;
    }

    *out_runtime = runtime;
    return true;
}

void sdl3d_data_game_runtime_destroy(sdl3d_data_game_runtime *runtime)
{
    if (runtime == NULL)
    {
        return;
    }

    disconnect_managed_network(runtime);
    disconnect_haptics_policies(runtime);
    sdl3d_game_data_particle_cache_free(&runtime->particle_cache);
    sdl3d_game_data_image_cache_free(&runtime->image_cache);
    sdl3d_game_data_font_cache_free(&runtime->font_cache);
    sdl3d_game_data_destroy(runtime->data);
    sdl3d_asset_resolver_destroy(runtime->assets);
    SDL_free(runtime);
}

sdl3d_asset_resolver *sdl3d_data_game_runtime_assets(const sdl3d_data_game_runtime *runtime)
{
    return runtime != NULL ? runtime->assets : NULL;
}

sdl3d_game_data_runtime *sdl3d_data_game_runtime_data(const sdl3d_data_game_runtime *runtime)
{
    return runtime != NULL ? runtime->data : NULL;
}

bool sdl3d_data_game_runtime_refresh_input_profile_on_device_change(sdl3d_data_game_runtime *runtime,
                                                                    sdl3d_input_manager *input,
                                                                    const char **out_profile_name, bool *out_applied,
                                                                    char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || runtime->data == NULL)
    {
        set_error(error_buffer, error_buffer_size, "input profile refresh requires data-game runtime");
        return false;
    }
    return sdl3d_game_data_apply_active_input_profile_on_device_change(
        runtime->data, input, &runtime->input_profile_refresh, out_profile_name, out_applied, error_buffer,
        error_buffer_size);
}

bool sdl3d_data_game_runtime_update_network_host_session(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx,
                                                         const char *session_name,
                                                         const sdl3d_data_game_network_bindings *bindings, bool playing,
                                                         float dt, sdl3d_data_game_network_loop_result *out_result,
                                                         char *error_buffer, int error_buffer_size)
{
    Uint8 packet[SDL3D_NETWORK_MAX_PACKET_SIZE];
    sdl3d_network_session *session = runtime != NULL && runtime->data != NULL
                                         ? sdl3d_game_data_get_network_host_session(runtime->data, session_name)
                                         : NULL;
    network_loop_result_init(out_result, session);
    if (runtime == NULL || runtime->data == NULL || bindings == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network host update requires runtime and bindings");
        return false;
    }
    if (session == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "network host session '%s' not found",
                   session_name != NULL ? session_name : "<null>");
        return false;
    }

    if (!sdl3d_network_session_update(session, dt))
    {
        set_errorf(error_buffer, error_buffer_size, "network host session update failed: %s", SDL_GetError());
        network_loop_result_init(out_result, session);
        return false;
    }

    int packet_size = 0;
    while ((packet_size = sdl3d_network_session_receive(session, packet, (int)sizeof(packet))) > 0)
    {
        if (out_result != NULL)
            ++out_result->packets_received;

        const char *control_binding = NULL;
        sdl3d_game_data_network_control control;
        if (data_game_decode_runtime_control(runtime, packet, packet_size, &control_binding, &control))
        {
            if (out_result != NULL)
                out_result->last_tick = control.tick;
            if (data_game_binding_matches(control_binding, bindings->disconnect))
            {
                if (out_result != NULL)
                    out_result->received_disconnect = true;
                continue;
            }
            if (playing && data_game_binding_matches(control_binding, bindings->pause_request))
            {
                if (ctx != NULL)
                    ctx->paused = true;
                if (out_result != NULL)
                    out_result->received_pause_request = true;
                continue;
            }
            if (playing && data_game_binding_matches(control_binding, bindings->resume_request))
            {
                if (ctx != NULL)
                    ctx->paused = false;
                if (out_result != NULL)
                    out_result->received_resume_request = true;
                continue;
            }
            continue;
        }

        if (playing && bindings->client_input != NULL && bindings->client_input[0] != '\0')
        {
            Uint32 tick = 0U;
            char apply_error[160] = {0};
            sdl3d_input_manager *input =
                runtime->session != NULL ? sdl3d_game_session_get_input(runtime->session) : NULL;
            if (sdl3d_game_data_apply_network_runtime_input(runtime->data, bindings->client_input, input, packet,
                                                            (size_t)packet_size, &tick, apply_error,
                                                            (int)sizeof(apply_error)))
            {
                if (out_result != NULL)
                {
                    out_result->applied_input = true;
                    out_result->last_tick = tick;
                }
            }
        }
    }

    if (out_result != NULL)
        out_result->session_state = sdl3d_network_session_state(session);
    return true;
}

bool sdl3d_data_game_runtime_publish_network_host_snapshot(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx,
                                                           const char *session_name,
                                                           const sdl3d_data_game_network_bindings *bindings,
                                                           sdl3d_data_game_network_loop_result *out_result,
                                                           char *error_buffer, int error_buffer_size)
{
    sdl3d_network_session *session = runtime != NULL && runtime->data != NULL
                                         ? sdl3d_game_data_get_network_host_session(runtime->data, session_name)
                                         : NULL;
    network_loop_result_init(out_result, session);
    if (runtime == NULL || runtime->data == NULL || bindings == NULL || bindings->state_snapshot == NULL ||
        bindings->state_snapshot[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "network host snapshot publish requires runtime and binding");
        return false;
    }
    if (session == NULL || !sdl3d_network_session_is_connected(session))
    {
        set_error(error_buffer, error_buffer_size, "network host snapshot publish requires connected host session");
        return false;
    }
    if (!data_game_sync_network_pause_from_context(runtime, ctx, error_buffer, error_buffer_size))
    {
        return false;
    }

    const Uint32 tick = data_game_input_tick(runtime);
    if (!sdl3d_game_data_send_network_runtime_snapshot(runtime->data, session, bindings->state_snapshot, tick,
                                                       error_buffer, error_buffer_size))
    {
        return false;
    }
    if (out_result != NULL)
    {
        out_result->sent_snapshot = true;
        out_result->last_tick = tick;
        out_result->session_state = sdl3d_network_session_state(session);
    }
    return true;
}

bool sdl3d_data_game_runtime_update_network_client_session(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx,
                                                           const char *session_name,
                                                           const sdl3d_data_game_network_bindings *bindings,
                                                           bool playing, bool allow_pause_requests, float dt,
                                                           sdl3d_data_game_network_loop_result *out_result,
                                                           char *error_buffer, int error_buffer_size)
{
    Uint8 packet[SDL3D_NETWORK_MAX_PACKET_SIZE];
    sdl3d_network_session *session =
        runtime != NULL && runtime->data != NULL
            ? sdl3d_game_data_get_network_direct_connect_session(runtime->data, session_name)
            : NULL;
    network_loop_result_init(out_result, session);
    if (runtime == NULL || runtime->data == NULL || bindings == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network client update requires runtime and bindings");
        return false;
    }
    if (session == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "network client session '%s' not found",
                   session_name != NULL ? session_name : "<null>");
        return false;
    }

    if (!sdl3d_network_session_update(session, dt))
    {
        set_errorf(error_buffer, error_buffer_size, "network client session update failed: %s", SDL_GetError());
        network_loop_result_init(out_result, session);
        return false;
    }

    int packet_size = 0;
    while ((packet_size = sdl3d_network_session_receive(session, packet, (int)sizeof(packet))) > 0)
    {
        if (out_result != NULL)
            ++out_result->packets_received;

        const char *control_binding = NULL;
        sdl3d_game_data_network_control control;
        if (data_game_decode_runtime_control(runtime, packet, packet_size, &control_binding, &control))
        {
            if (out_result != NULL)
                out_result->last_tick = control.tick;
            if (data_game_binding_matches(control_binding, bindings->start_game))
            {
                if (out_result != NULL)
                    out_result->received_start_game = true;
                continue;
            }
            if (data_game_binding_matches(control_binding, bindings->disconnect))
            {
                if (out_result != NULL)
                    out_result->received_disconnect = true;
                continue;
            }
            if (data_game_binding_matches(control_binding, bindings->pause_request))
            {
                if (ctx != NULL)
                    ctx->paused = true;
                if (out_result != NULL)
                    out_result->received_pause_request = true;
                continue;
            }
            if (data_game_binding_matches(control_binding, bindings->resume_request))
            {
                if (ctx != NULL)
                    ctx->paused = false;
                if (out_result != NULL)
                    out_result->received_resume_request = true;
                continue;
            }
            continue;
        }

        if (bindings->state_snapshot != NULL && bindings->state_snapshot[0] != '\0')
        {
            Uint32 tick = 0U;
            char apply_error[160] = {0};
            if (sdl3d_game_data_apply_network_runtime_snapshot(runtime->data, bindings->state_snapshot, packet,
                                                               (size_t)packet_size, &tick, apply_error,
                                                               (int)sizeof(apply_error)))
            {
                (void)data_game_sync_context_pause_from_network(runtime, ctx, apply_error, (int)sizeof(apply_error));
                if (out_result != NULL)
                {
                    out_result->applied_snapshot = true;
                    out_result->last_tick = tick;
                }
            }
        }
    }

    if (playing && sdl3d_network_session_is_connected(session))
    {
        sdl3d_input_manager *input = runtime->session != NULL ? sdl3d_game_session_get_input(runtime->session) : NULL;
        const Uint32 tick = data_game_input_tick(runtime);
        if (allow_pause_requests && ctx != NULL)
        {
            int pause_action = -1;
            if (sdl3d_game_data_get_network_runtime_pause_action(runtime->data, &pause_action) && input != NULL &&
                sdl3d_input_is_pressed(input, pause_action))
            {
                const bool want_resume = ctx->paused;
                const char *control_binding = want_resume ? bindings->resume_request : bindings->pause_request;
                if (control_binding == NULL || control_binding[0] == '\0')
                {
                    set_error(error_buffer, error_buffer_size, "network client pause request requires control binding");
                    return false;
                }
                if (!data_game_send_runtime_control(runtime, session, control_binding, tick, error_buffer,
                                                    error_buffer_size))
                {
                    return false;
                }
                if (out_result != NULL)
                {
                    out_result->sent_pause_request = !want_resume;
                    out_result->sent_resume_request = want_resume;
                    out_result->last_tick = tick;
                }
            }
        }

        if (bindings->client_input != NULL && bindings->client_input[0] != '\0' && input != NULL)
        {
            if (!sdl3d_game_data_send_network_runtime_input(runtime->data, session, bindings->client_input, input, tick,
                                                            error_buffer, error_buffer_size))
            {
                return false;
            }
            if (out_result != NULL)
            {
                out_result->sent_input = true;
                out_result->last_tick = tick;
            }
        }
    }

    if (out_result != NULL)
        out_result->session_state = sdl3d_network_session_state(session);
    return true;
}

static bool refresh_active_input_profile_if_available(sdl3d_data_game_runtime *runtime)
{
    sdl3d_input_manager *input =
        runtime != NULL && runtime->session != NULL ? sdl3d_game_session_get_input(runtime->session) : NULL;
    if (runtime == NULL || runtime->data == NULL || input == NULL)
    {
        return true;
    }

    if (!sdl3d_game_data_get_active_input_profile_name(runtime->data, input, NULL))
    {
        sdl3d_game_data_input_profile_refresh_state_init(&runtime->input_profile_refresh);
        return true;
    }

    char error[256] = "";
    const char *profile_name = NULL;
    bool applied = false;
    if (!sdl3d_data_game_runtime_refresh_input_profile_on_device_change(runtime, input, &profile_name, &applied, error,
                                                                        (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D input profile hotplug refresh failed: %s",
                    error[0] != '\0' ? error : "unknown error");
        return false;
    }
    return true;
}

bool sdl3d_data_game_runtime_update_frame(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx, float dt)
{
    if (runtime == NULL || runtime->data == NULL)
    {
        return false;
    }

    if (!refresh_active_input_profile_if_available(runtime))
        return false;

    managed_network_update_before_frame(runtime, ctx, dt);

    const sdl3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                     .runtime = runtime->data,
                                                     .app_flow = &runtime->app_flow,
                                                     .particle_cache = &runtime->particle_cache,
                                                     .dt = dt};
    if (!sdl3d_game_data_update_frame(&runtime->frame_state, &frame))
        return false;

    managed_network_update_after_frame(runtime, ctx);
    return true;
}

void sdl3d_data_game_runtime_render(sdl3d_data_game_runtime *runtime, sdl3d_game_context *ctx)
{
    if (runtime == NULL || runtime->data == NULL || ctx == NULL)
    {
        return;
    }

    sdl3d_game_data_frame_state_record_render(&runtime->frame_state, ctx, runtime->data);

    sdl3d_game_data_frame_desc frame;
    SDL_zero(frame);
    frame.runtime = runtime->data;
    frame.renderer = ctx->renderer;
    frame.font_cache = &runtime->font_cache;
    frame.image_cache = &runtime->image_cache;
    frame.particle_cache = &runtime->particle_cache;
    frame.app_flow = &runtime->app_flow;
    frame.metrics = &runtime->frame_state.metrics;
    frame.render_eval = &runtime->frame_state.render_eval;
    frame.pulse_phase = runtime->frame_state.ui_pulse_phase;
    sdl3d_game_data_draw_frame(&frame);
}

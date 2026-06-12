#include "slayer3d/data_game.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include "slayer3d/game_presentation.h"
#include "slayer3d/input.h"
#include "slayer3d/properties.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/transition.h"

struct slayer3d_data_game_runtime
{
    slayer3d_asset_resolver *assets;
    slayer3d_game_data_runtime *data;
    slayer3d_game_data_font_cache font_cache;
    slayer3d_game_data_image_cache image_cache;
    slayer3d_game_data_particle_cache particle_cache;
    slayer3d_game_data_sprite_cache sprite_cache;
    slayer3d_game_data_model_cache model_cache;
    slayer3d_game_data_mesh_primitive_cache mesh_primitive_cache;
    slayer3d_game_data_asset_warmup_queue asset_warmup;
    slayer3d_game_data_app_flow app_flow;
    slayer3d_game_data_frame_state frame_state;
    slayer3d_game_data_input_profile_refresh_state input_profile_refresh;
    slayer3d_game_session *session;
    int *haptics_signal_ids;
    int *haptics_connections;
    int haptics_connection_count;
    bool managed_network_enabled;
    int managed_network_lobby_start_signal_id;
    int managed_network_lobby_start_connection;
    int managed_network_camera_toggle_signal_id;
    bool managed_network_lobby_start_requested;
    float managed_network_termination_timer;
    bool mouse_capture_applied;
    bool mouse_capture_enabled;
};

static const char SLAYER3D_MANAGED_NETWORK_HOST_SESSION[] = "host";
static const char SLAYER3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION[] = "direct_connect";
static const char SLAYER3D_MANAGED_NETWORK_BINDING_STATE_SNAPSHOT[] = "state_snapshot";
static const char SLAYER3D_MANAGED_NETWORK_BINDING_CLIENT_INPUT[] = "client_input";
static const char SLAYER3D_MANAGED_NETWORK_BINDING_START_GAME[] = "start_game";
static const char SLAYER3D_MANAGED_NETWORK_BINDING_PAUSE_REQUEST[] = "pause_request";
static const char SLAYER3D_MANAGED_NETWORK_BINDING_RESUME_REQUEST[] = "resume_request";
static const char SLAYER3D_MANAGED_NETWORK_BINDING_DISCONNECT[] = "disconnect";
static const char SLAYER3D_MANAGED_NETWORK_BINDING_MENU_SELECT[] = "menu_select";
static const char SLAYER3D_MANAGED_NETWORK_BINDING_CAMERA_TOGGLE[] = "camera_toggle";
static const char SLAYER3D_MANAGED_NETWORK_BINDING_LOBBY_START[] = "lobby_start";
static const char SLAYER3D_MANAGED_NETWORK_DIAGNOSTIC_SNAPSHOT[] = "multiplayer_state";

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

static void network_loop_result_init(slayer3d_data_game_network_loop_result *result, slayer3d_network_session *session)
{
    if (result == NULL)
    {
        return;
    }

    SDL_zero(*result);
    result->session_state =
        session != NULL ? slayer3d_network_session_state(session) : SLAYER3D_NETWORK_STATE_DISCONNECTED;
}

static Uint32 data_game_input_tick(const slayer3d_data_game_runtime *runtime)
{
    slayer3d_input_manager *input =
        runtime != NULL && runtime->session != NULL ? slayer3d_game_session_get_input(runtime->session) : NULL;
    const slayer3d_input_snapshot *snapshot = input != NULL ? slayer3d_input_get_snapshot(input) : NULL;
    return snapshot != NULL ? (Uint32)SDL_max(snapshot->tick, 0) : (Uint32)SDL_GetTicks();
}

static float data_game_elapsed_seconds(Uint64 start_counter, Uint64 end_counter)
{
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    if (frequency == 0 || end_counter < start_counter)
        return 0.0f;
    return (float)((double)(end_counter - start_counter) / (double)frequency);
}

static bool data_game_format_scene_state_key(char *buffer, size_t buffer_size, const char *prefix, const char *field)
{
    if (buffer == NULL || buffer_size == 0 || field == NULL || field[0] == '\0')
        return false;
    const char *base = prefix != NULL && prefix[0] != '\0' ? prefix : "asset_warmup";
    const int written = SDL_snprintf(buffer, buffer_size, "%s.%s", base, field);
    return written > 0 && (size_t)written < buffer_size;
}

static void data_game_set_warmup_int(slayer3d_properties *state, const char *prefix, const char *field, int value)
{
    char key[128];
    if (data_game_format_scene_state_key(key, sizeof(key), prefix, field))
        slayer3d_properties_set_int(state, key, value);
}

static void data_game_set_warmup_float(slayer3d_properties *state, const char *prefix, const char *field, float value)
{
    char key[128];
    if (data_game_format_scene_state_key(key, sizeof(key), prefix, field))
        slayer3d_properties_set_float(state, key, value);
}

static void data_game_set_warmup_bool(slayer3d_properties *state, const char *prefix, const char *field, bool value)
{
    char key[128];
    if (data_game_format_scene_state_key(key, sizeof(key), prefix, field))
        slayer3d_properties_set_bool(state, key, value);
}

static void data_game_set_warmup_string(slayer3d_properties *state, const char *prefix, const char *field,
                                        const char *value)
{
    char key[128];
    if (data_game_format_scene_state_key(key, sizeof(key), prefix, field))
        slayer3d_properties_set_string(state, key, value != NULL ? value : "");
}

static const char *data_game_warmup_state_name(slayer3d_game_data_asset_warmup_state state)
{
    switch (state)
    {
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING:
        return "loading";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_READY_FOR_FINALIZE:
        return "ready_for_finalize";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_READY:
        return "ready";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_FAILED:
        return "failed";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_CANCELED:
        return "canceled";
    case SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED:
    default:
        return "queued";
    }
}

typedef struct data_game_publish_ui_image_warmup_context
{
    const slayer3d_game_data_asset_warmup_queue *queue;
    slayer3d_properties *state;
    const char *prefix;
} data_game_publish_ui_image_warmup_context;

static bool data_game_publish_ui_image_warmup(void *userdata, const slayer3d_game_data_ui_image *image)
{
    data_game_publish_ui_image_warmup_context *context = (data_game_publish_ui_image_warmup_context *)userdata;
    if (context == NULL || context->queue == NULL || context->state == NULL || image == NULL || image->image == NULL ||
        image->image[0] == '\0')
    {
        return true;
    }

    slayer3d_game_data_asset_warmup_state state;
    if (!slayer3d_game_data_asset_warmup_request_state(context->queue, SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE, NULL,
                                                       image->image, &state))
    {
        return true;
    }

    char field[128];
    int written = SDL_snprintf(field, sizeof(field), "ui_image.%s.status", image->image);
    if (written > 0 && (size_t)written < sizeof(field))
        data_game_set_warmup_string(context->state, context->prefix, field, data_game_warmup_state_name(state));
    written = SDL_snprintf(field, sizeof(field), "ui_image.%s.pending", image->image);
    if (written > 0 && (size_t)written < sizeof(field))
        data_game_set_warmup_bool(context->state, context->prefix, field,
                                  state == SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED ||
                                      state == SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING ||
                                      state == SLAYER3D_GAME_DATA_ASSET_WARMUP_READY_FOR_FINALIZE);
    written = SDL_snprintf(field, sizeof(field), "ui_image.%s.ready", image->image);
    if (written > 0 && (size_t)written < sizeof(field))
        data_game_set_warmup_bool(context->state, context->prefix, field,
                                  state == SLAYER3D_GAME_DATA_ASSET_WARMUP_READY);
    written = SDL_snprintf(field, sizeof(field), "ui_image.%s.failed", image->image);
    if (written > 0 && (size_t)written < sizeof(field))
        data_game_set_warmup_bool(context->state, context->prefix, field,
                                  state == SLAYER3D_GAME_DATA_ASSET_WARMUP_FAILED);
    return true;
}

static void data_game_release_mouse_capture(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx)
{
    if (runtime == NULL || ctx == NULL || ctx->window == NULL || !runtime->mouse_capture_applied ||
        !runtime->mouse_capture_enabled)
    {
        return;
    }

    if (SDL_SetWindowRelativeMouseMode(ctx->window, false))
    {
        float dx = 0.0f;
        float dy = 0.0f;
        (void)SDL_GetRelativeMouseState(&dx, &dy);
        slayer3d_input_discard_mouse_motion(slayer3d_game_session_get_input(ctx->session));
    }
    runtime->mouse_capture_enabled = false;
}

static void data_game_apply_scene_mouse_capture(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx)
{
    if (runtime == NULL || runtime->data == NULL || ctx == NULL || ctx->window == NULL)
        return;

    const bool capture = slayer3d_game_data_active_scene_mouse_capture(runtime->data, ctx->paused);
    if (runtime->mouse_capture_applied && runtime->mouse_capture_enabled == capture)
        return;

    if (SDL_SetWindowRelativeMouseMode(ctx->window, capture))
    {
        float dx = 0.0f;
        float dy = 0.0f;
        (void)SDL_GetRelativeMouseState(&dx, &dy);
        slayer3d_input_discard_mouse_motion(slayer3d_game_session_get_input(ctx->session));
    }
    runtime->mouse_capture_applied = true;
    runtime->mouse_capture_enabled = capture;
}

static bool data_game_binding_matches(const char *actual, const char *expected)
{
    return actual != NULL && expected != NULL && expected[0] != '\0' && SDL_strcmp(actual, expected) == 0;
}

static bool data_game_decode_runtime_control(slayer3d_data_game_runtime *runtime, const Uint8 *packet, int packet_size,
                                             const char **out_binding, slayer3d_game_data_network_control *out_control)
{
    char error[160] = {0};
    if (out_binding != NULL)
        *out_binding = NULL;
    if (runtime == NULL || runtime->data == NULL || packet == NULL || packet_size <= 0)
    {
        return false;
    }

    return slayer3d_game_data_decode_network_runtime_control(runtime->data, packet, (size_t)packet_size, out_binding,
                                                             out_control, error, (int)sizeof(error));
}

static bool data_game_send_runtime_control(slayer3d_data_game_runtime *runtime, slayer3d_network_session *session,
                                           const char *binding_name, Uint32 tick, char *error_buffer,
                                           int error_buffer_size)
{
    if (runtime == NULL || runtime->data == NULL || session == NULL || binding_name == NULL || binding_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "network control send requires runtime, session, and binding");
        return false;
    }

    return slayer3d_game_data_send_network_runtime_control(runtime->data, session, binding_name, tick, error_buffer,
                                                           error_buffer_size);
}

static bool data_game_sync_network_pause_from_context(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx,
                                                      char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || runtime->data == NULL || ctx == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network pause sync requires runtime and context");
        return false;
    }

    return slayer3d_game_data_set_network_runtime_pause_state(runtime->data, ctx->paused, error_buffer,
                                                              error_buffer_size);
}

static bool data_game_sync_context_pause_from_network(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx,
                                                      char *error_buffer, int error_buffer_size)
{
    bool paused = false;
    if (runtime == NULL || runtime->data == NULL || ctx == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network pause sync requires runtime and context");
        return false;
    }
    if (!slayer3d_game_data_get_network_runtime_pause_state(runtime->data, &paused, error_buffer, error_buffer_size))
    {
        return false;
    }
    ctx->paused = paused;
    return true;
}

static bool haptics_signal_connected(const slayer3d_data_game_runtime *runtime, int signal_id)
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

static void on_haptics_policy_signal(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    slayer3d_data_game_runtime *runtime = (slayer3d_data_game_runtime *)userdata;
    slayer3d_input_manager *input =
        runtime != NULL && runtime->session != NULL ? slayer3d_game_session_get_input(runtime->session) : NULL;
    if (runtime == NULL || runtime->data == NULL || input == NULL)
    {
        return;
    }

    const int policy_count = slayer3d_game_data_haptics_policy_count(runtime->data);
    for (int i = 0; i < policy_count; ++i)
    {
        slayer3d_game_data_haptics_policy policy;
        if (!slayer3d_game_data_match_haptics_policy(runtime->data, i, signal_id, payload, &policy))
        {
            continue;
        }
        if (!slayer3d_input_rumble_all_gamepads(input, policy.low_frequency, policy.high_frequency, policy.duration_ms))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SLAYER3D haptics policy '%s' requested rumble but no gamepad accepted it",
                        policy.name != NULL ? policy.name : "<unnamed>");
        }
    }
}

static bool connect_haptics_policies(slayer3d_data_game_runtime *runtime)
{
    if (runtime == NULL || runtime->data == NULL || runtime->session == NULL)
    {
        return true;
    }

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(runtime->session);
    if (bus == NULL || slayer3d_game_session_get_input(runtime->session) == NULL)
    {
        return true;
    }

    const int policy_count = slayer3d_game_data_haptics_policy_count(runtime->data);
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
        slayer3d_game_data_haptics_policy policy;
        if (!slayer3d_game_data_get_haptics_policy_at(runtime->data, i, &policy) ||
            haptics_signal_connected(runtime, policy.signal_id))
        {
            continue;
        }

        const int connection = slayer3d_signal_connect(bus, policy.signal_id, on_haptics_policy_signal, runtime);
        if (connection > 0)
        {
            runtime->haptics_signal_ids[runtime->haptics_connection_count] = policy.signal_id;
            runtime->haptics_connections[runtime->haptics_connection_count] = connection;
            ++runtime->haptics_connection_count;
        }
    }
    return true;
}

static void disconnect_haptics_policies(slayer3d_data_game_runtime *runtime)
{
    if (runtime == NULL || runtime->session == NULL)
    {
        return;
    }

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(runtime->session);
    if (bus != NULL)
    {
        for (int i = 0; i < runtime->haptics_connection_count; ++i)
        {
            if (runtime->haptics_connections != NULL && runtime->haptics_connections[i] > 0)
            {
                slayer3d_signal_disconnect(bus, runtime->haptics_connections[i]);
            }
        }
    }
    SDL_free(runtime->haptics_signal_ids);
    SDL_free(runtime->haptics_connections);
    runtime->haptics_signal_ids = NULL;
    runtime->haptics_connections = NULL;
    runtime->haptics_connection_count = 0;
}

static slayer3d_data_game_network_bindings managed_network_bindings(void)
{
    slayer3d_data_game_network_bindings bindings;
    SDL_zero(bindings);
    bindings.state_snapshot = SLAYER3D_MANAGED_NETWORK_BINDING_STATE_SNAPSHOT;
    bindings.client_input = SLAYER3D_MANAGED_NETWORK_BINDING_CLIENT_INPUT;
    bindings.start_game = SLAYER3D_MANAGED_NETWORK_BINDING_START_GAME;
    bindings.pause_request = SLAYER3D_MANAGED_NETWORK_BINDING_PAUSE_REQUEST;
    bindings.resume_request = SLAYER3D_MANAGED_NETWORK_BINDING_RESUME_REQUEST;
    bindings.disconnect = SLAYER3D_MANAGED_NETWORK_BINDING_DISCONNECT;
    return bindings;
}

static const char *managed_network_session_scene(const slayer3d_data_game_runtime *runtime, const char *name,
                                                 const char *fallback)
{
    const char *scene = NULL;
    if (runtime != NULL && runtime->data != NULL &&
        slayer3d_game_data_get_network_session_scene(runtime->data, name, &scene))
    {
        return scene;
    }
    return fallback;
}

static const char *managed_network_session_state_key(const slayer3d_data_game_runtime *runtime, const char *name,
                                                     const char *fallback)
{
    const char *key = NULL;
    if (runtime != NULL && runtime->data != NULL &&
        slayer3d_game_data_get_network_session_state_key(runtime->data, name, &key))
    {
        return key;
    }
    return fallback;
}

static const char *managed_network_session_state_value(const slayer3d_data_game_runtime *runtime, const char *group,
                                                       const char *name, const char *fallback)
{
    const char *value = NULL;
    if (runtime != NULL && runtime->data != NULL &&
        slayer3d_game_data_get_network_session_state_value(runtime->data, group, name, &value))
    {
        return value;
    }
    return fallback;
}

static const char *managed_network_message(const slayer3d_data_game_runtime *runtime, const char *group,
                                           const char *name, const char *fallback)
{
    const char *message = NULL;
    if (runtime != NULL && runtime->data != NULL &&
        slayer3d_game_data_get_network_session_message(runtime->data, group, name, &message))
    {
        return message;
    }
    return fallback;
}

static const char *managed_network_scene_state_key(const slayer3d_data_game_runtime *runtime, const char *scope,
                                                   const char *name, const char *fallback)
{
    const char *key = NULL;
    if (runtime != NULL && runtime->data != NULL &&
        slayer3d_game_data_get_network_scene_state_key(runtime->data, scope, name, &key))
    {
        return key;
    }
    return fallback;
}

static const char *managed_network_scene_state_string(const slayer3d_data_game_runtime *runtime, const char *key_name,
                                                      const char *fallback)
{
    const slayer3d_properties *scene_state =
        runtime != NULL && runtime->data != NULL ? slayer3d_game_data_scene_state(runtime->data) : NULL;
    const char *key = managed_network_session_state_key(runtime, key_name, NULL);
    return scene_state != NULL && key != NULL ? slayer3d_properties_get_string(scene_state, key, fallback) : fallback;
}

static bool managed_network_active_scene_is(const slayer3d_data_game_runtime *runtime, const char *scene_name,
                                            const char *fallback)
{
    const char *active_scene =
        runtime != NULL && runtime->data != NULL ? slayer3d_game_data_active_scene(runtime->data) : NULL;
    const char *expected_scene = managed_network_session_scene(runtime, scene_name, fallback);
    return active_scene != NULL && expected_scene != NULL && SDL_strcmp(active_scene, expected_scene) == 0;
}

static bool managed_network_is_play_scene(const slayer3d_data_game_runtime *runtime)
{
    return managed_network_active_scene_is(runtime, "play", NULL);
}

static bool managed_network_keep_alive_scene_matches(const slayer3d_data_game_runtime *runtime,
                                                     const char *session_name)
{
    const char *active_scene =
        runtime != NULL && runtime->data != NULL ? slayer3d_game_data_active_scene(runtime->data) : NULL;
    return slayer3d_game_data_network_managed_keep_alive_scene_matches(runtime != NULL ? runtime->data : NULL,
                                                                       session_name, active_scene);
}

static bool managed_network_is_network_match(const slayer3d_data_game_runtime *runtime)
{
    const char *match_mode = managed_network_scene_state_string(runtime, "match_mode", NULL);
    const char *network_value = managed_network_session_state_value(runtime, "match_mode", "network", NULL);
    return match_mode != NULL && network_value != NULL && SDL_strcmp(match_mode, network_value) == 0;
}

static bool managed_network_is_role_host(const slayer3d_data_game_runtime *runtime)
{
    const char *network_role = managed_network_scene_state_string(runtime, "network_role", NULL);
    const char *host_value = managed_network_session_state_value(runtime, "network_role", "host", NULL);
    return managed_network_is_network_match(runtime) && network_role != NULL && host_value != NULL &&
           SDL_strcmp(network_role, host_value) == 0;
}

static bool managed_network_is_role_client(const slayer3d_data_game_runtime *runtime)
{
    const char *network_role = managed_network_scene_state_string(runtime, "network_role", NULL);
    const char *client_value = managed_network_session_state_value(runtime, "network_role", "client", NULL);
    return managed_network_is_network_match(runtime) && network_role != NULL && client_value != NULL &&
           SDL_strcmp(network_role, client_value) == 0;
}

static int managed_network_action_id(const slayer3d_data_game_runtime *runtime, const char *binding_name)
{
    int action_id = -1;
    if (runtime != NULL && runtime->data != NULL &&
        slayer3d_game_data_get_network_runtime_action(runtime->data, binding_name, &action_id))
    {
        return action_id;
    }
    return -1;
}

static bool managed_network_run_flow_event(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx,
                                           const char *event_name, const char *reason)
{
    char error[192] = {0};
    slayer3d_properties *payload = NULL;
    bool ok = false;

    if (runtime == NULL || runtime->data == NULL || event_name == NULL || event_name[0] == '\0')
        return false;

    payload = slayer3d_properties_create();
    if (payload == NULL)
        return false;
    slayer3d_properties_set_string(payload, "event", event_name);
    slayer3d_properties_set_string(payload, "reason", reason != NULL ? reason : "");

    ok = slayer3d_game_data_run_network_session_flow_event(runtime->data, ctx, event_name, payload, error,
                                                           (int)sizeof(error));
    slayer3d_properties_destroy(payload);
    if (!ok)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D managed network event '%s' failed: %s", event_name,
                    error[0] != '\0' ? error : "unknown error");
    }
    return ok;
}

static bool managed_network_send_control(slayer3d_data_game_runtime *runtime, slayer3d_network_session *session,
                                         const char *binding_name, const char *label)
{
    char error[160] = {0};
    const Uint32 tick = data_game_input_tick(runtime);
    if (!data_game_send_runtime_control(runtime, session, binding_name, tick, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D managed network control send failed: %s",
                    error[0] != '\0' ? error : "unknown error");
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D managed network sent control: %s binding=%s",
                label != NULL ? label : "control", binding_name != NULL ? binding_name : "<null>");
    return true;
}

static bool managed_network_send_control_repeated(slayer3d_data_game_runtime *runtime,
                                                  slayer3d_network_session *session, const char *binding_name,
                                                  const char *label, int count)
{
    bool sent_any = false;
    const int attempts = SDL_max(count, 1);
    for (int i = 0; i < attempts; ++i)
    {
        if (managed_network_send_control(runtime, session, binding_name, label))
            sent_any = true;
        if (session != NULL)
            (void)slayer3d_network_session_update(session, 0.0f);
    }
    return sent_any;
}

static void managed_network_publish_host_status(slayer3d_data_game_runtime *runtime)
{
    if (runtime == NULL || runtime->data == NULL)
        return;

    (void)slayer3d_game_data_network_host_publish_status(
        runtime->data, SLAYER3D_MANAGED_NETWORK_HOST_SESSION,
        managed_network_scene_state_key(runtime, "host", "status", NULL),
        managed_network_scene_state_key(runtime, "host", "endpoint", NULL),
        managed_network_scene_state_key(runtime, "host", "peer", NULL),
        managed_network_scene_state_key(runtime, "host", "connected", NULL));
}

static void managed_network_cancel_host(slayer3d_data_game_runtime *runtime, bool notify_peer, const char *status)
{
    slayer3d_network_session *session =
        runtime != NULL && runtime->data != NULL
            ? slayer3d_game_data_get_network_host_session(runtime->data, SLAYER3D_MANAGED_NETWORK_HOST_SESSION)
            : NULL;
    if (runtime == NULL || runtime->data == NULL)
        return;
    if (session != NULL && notify_peer && slayer3d_network_session_is_connected(session))
    {
        (void)managed_network_send_control_repeated(runtime, session, SLAYER3D_MANAGED_NETWORK_BINDING_DISCONNECT,
                                                    "host disconnect", 5);
    }
    (void)slayer3d_game_data_network_host_cancel(runtime->data, SLAYER3D_MANAGED_NETWORK_HOST_SESSION,
                                                 managed_network_scene_state_key(runtime, "host", "status", NULL),
                                                 managed_network_scene_state_key(runtime, "host", "endpoint", NULL),
                                                 managed_network_scene_state_key(runtime, "host", "peer", NULL),
                                                 managed_network_scene_state_key(runtime, "host", "connected", NULL),
                                                 status != NULL ? status : "Not hosting");
}

static void managed_network_publish_direct_connect_status(slayer3d_data_game_runtime *runtime)
{
    if (runtime == NULL || runtime->data == NULL)
        return;

    (void)slayer3d_game_data_network_direct_connect_publish_status(
        runtime->data, SLAYER3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION,
        managed_network_scene_state_key(runtime, "direct_connect", "status", NULL),
        managed_network_scene_state_key(runtime, "direct_connect", "state", NULL),
        managed_network_scene_state_key(runtime, "direct_connect", "connected", NULL));
}

static void managed_network_cancel_direct_connect(slayer3d_data_game_runtime *runtime, bool notify_peer,
                                                  const char *status)
{
    slayer3d_network_session *session = runtime != NULL && runtime->data != NULL
                                            ? slayer3d_game_data_get_network_direct_connect_session(
                                                  runtime->data, SLAYER3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION)
                                            : NULL;
    if (runtime == NULL || runtime->data == NULL)
        return;
    if (session != NULL && notify_peer && slayer3d_network_session_is_connected(session))
    {
        (void)managed_network_send_control_repeated(runtime, session, SLAYER3D_MANAGED_NETWORK_BINDING_DISCONNECT,
                                                    "client disconnect", 5);
    }
    (void)slayer3d_game_data_network_direct_connect_cancel(
        runtime->data, SLAYER3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION,
        managed_network_scene_state_key(runtime, "direct_connect", "status", NULL),
        managed_network_scene_state_key(runtime, "direct_connect", "state", NULL),
        managed_network_scene_state_key(runtime, "direct_connect", "connected", NULL),
        status != NULL ? status : "Disconnected");
}

static void managed_network_disconnect_flow(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx,
                                            bool local_host, const char *reason)
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

static void managed_network_lobby_signal(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    slayer3d_data_game_runtime *runtime = (slayer3d_data_game_runtime *)userdata;
    (void)payload;
    if (runtime == NULL || signal_id != runtime->managed_network_lobby_start_signal_id)
        return;
    runtime->managed_network_lobby_start_requested = true;
}

static bool connect_managed_network(slayer3d_data_game_runtime *runtime)
{
    if (runtime == NULL || runtime->data == NULL || runtime->session == NULL || !runtime->managed_network_enabled)
        return true;

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(runtime->session);
    if (bus == NULL)
        return true;

    runtime->managed_network_lobby_start_signal_id = -1;
    runtime->managed_network_camera_toggle_signal_id = -1;
    (void)slayer3d_game_data_get_network_runtime_signal(runtime->data, SLAYER3D_MANAGED_NETWORK_BINDING_LOBBY_START,
                                                        &runtime->managed_network_lobby_start_signal_id);
    (void)slayer3d_game_data_get_network_runtime_signal(runtime->data, SLAYER3D_MANAGED_NETWORK_BINDING_CAMERA_TOGGLE,
                                                        &runtime->managed_network_camera_toggle_signal_id);

    if (runtime->managed_network_lobby_start_signal_id >= 0)
    {
        runtime->managed_network_lobby_start_connection = slayer3d_signal_connect(
            bus, runtime->managed_network_lobby_start_signal_id, managed_network_lobby_signal, runtime);
        if (runtime->managed_network_lobby_start_connection == 0)
            return false;
    }
    return true;
}

static void disconnect_managed_network(slayer3d_data_game_runtime *runtime)
{
    if (runtime == NULL)
        return;

    if (runtime->managed_network_enabled && runtime->data != NULL)
    {
        if (slayer3d_game_data_get_network_host_session(runtime->data, SLAYER3D_MANAGED_NETWORK_HOST_SESSION) != NULL)
            managed_network_cancel_host(runtime, true, "Not hosting");
        if (slayer3d_game_data_get_network_direct_connect_session(
                runtime->data, SLAYER3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION) != NULL)
        {
            managed_network_cancel_direct_connect(runtime, true, "Disconnected");
        }
    }

    if (runtime->session != NULL && runtime->managed_network_lobby_start_connection > 0)
    {
        slayer3d_signal_disconnect(slayer3d_game_session_get_signal_bus(runtime->session),
                                   runtime->managed_network_lobby_start_connection);
    }
    runtime->managed_network_lobby_start_connection = 0;
    runtime->managed_network_lobby_start_signal_id = -1;
    runtime->managed_network_camera_toggle_signal_id = -1;
    runtime->managed_network_lobby_start_requested = false;
}

static void managed_network_update_termination_ack(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx,
                                                   float dt)
{
    const slayer3d_properties *scene_state =
        runtime != NULL && runtime->data != NULL ? slayer3d_game_data_scene_state(runtime->data) : NULL;
    const char *active_key = managed_network_session_state_key(runtime, "match_termination_active", NULL);
    const bool active = scene_state != NULL && active_key != NULL
                            ? slayer3d_properties_get_bool(scene_state, active_key, false)
                            : false;

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
    (void)slayer3d_game_data_get_network_managed_termination_ack_delay(runtime->data, &acknowledge_delay);
    if (runtime->managed_network_termination_timer < acknowledge_delay)
        return;

    slayer3d_input_manager *input = runtime->session != NULL ? slayer3d_game_session_get_input(runtime->session) : NULL;
    const int select_action = managed_network_action_id(runtime, SLAYER3D_MANAGED_NETWORK_BINDING_MENU_SELECT);
    if (input == NULL || select_action < 0 || !slayer3d_input_is_pressed(input, select_action))
        return;

    runtime->managed_network_termination_timer = 0.0f;
    (void)managed_network_run_flow_event(runtime, ctx, "network_match_termination_ack", NULL);
}

static void managed_network_process_lobby_start(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx)
{
    if (runtime == NULL || runtime->data == NULL || !runtime->managed_network_lobby_start_requested)
        return;

    runtime->managed_network_lobby_start_requested = false;
    slayer3d_network_session *session =
        slayer3d_game_data_get_network_host_session(runtime->data, SLAYER3D_MANAGED_NETWORK_HOST_SESSION);
    if (session == NULL || !slayer3d_network_session_is_connected(session))
    {
        managed_network_publish_host_status(runtime);
        return;
    }

    if (managed_network_send_control(runtime, session, SLAYER3D_MANAGED_NETWORK_BINDING_START_GAME, "start game"))
    {
        (void)managed_network_run_flow_event(runtime, ctx, "host_start_game", NULL);
    }
}

static void managed_network_update_host(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx, float dt)
{
    if (runtime == NULL || runtime->data == NULL)
        return;

    slayer3d_network_session *session =
        slayer3d_game_data_get_network_host_session(runtime->data, SLAYER3D_MANAGED_NETWORK_HOST_SESSION);
    if (session == NULL)
        return;

    const slayer3d_data_game_network_bindings bindings = managed_network_bindings();
    slayer3d_data_game_network_loop_result result;
    char error[192] = {0};
    if (!slayer3d_data_game_runtime_update_network_host_session(runtime, ctx, SLAYER3D_MANAGED_NETWORK_HOST_SESSION,
                                                                &bindings, managed_network_is_play_scene(runtime), dt,
                                                                &result, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D managed host session update failed: %s",
                    error[0] != '\0' ? error : SDL_GetError());
    }

    if (result.received_disconnect)
    {
        managed_network_disconnect_flow(
            runtime, ctx, true,
            managed_network_message(runtime, "disconnect_reasons", "client_exited", "Client exited"));
    }
    else if (managed_network_is_play_scene(runtime) && result.session_state != SLAYER3D_NETWORK_STATE_CONNECTED)
    {
        managed_network_disconnect_flow(
            runtime, ctx, true,
            managed_network_message(runtime, "disconnect_reasons", "client_timed_out", "Client timed out"));
    }

    managed_network_publish_host_status(runtime);

    session = slayer3d_game_data_get_network_host_session(runtime->data, SLAYER3D_MANAGED_NETWORK_HOST_SESSION);
    if (session == NULL)
        return;

    const bool keep_host_session =
        managed_network_keep_alive_scene_matches(runtime, SLAYER3D_MANAGED_NETWORK_HOST_SESSION) &&
        (!managed_network_is_play_scene(runtime) || managed_network_is_role_host(runtime));
    if (!keep_host_session)
        managed_network_cancel_host(runtime, true, "Not hosting");
}

static void managed_network_update_direct_connect(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx,
                                                  float dt)
{
    if (runtime == NULL || runtime->data == NULL)
        return;

    slayer3d_network_session *session = slayer3d_game_data_get_network_direct_connect_session(
        runtime->data, SLAYER3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION);
    if (session == NULL)
        return;

    const bool was_playing = managed_network_is_play_scene(runtime);
    const bool playing = was_playing && managed_network_is_role_client(runtime);
    const bool keep_direct_connect_session =
        managed_network_keep_alive_scene_matches(runtime, SLAYER3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION) &&
        (!managed_network_is_play_scene(runtime) || managed_network_is_role_client(runtime));
    if (!keep_direct_connect_session)
    {
        managed_network_cancel_direct_connect(runtime, true, "Disconnected");
        return;
    }

    managed_network_publish_direct_connect_status(runtime);

    const slayer3d_data_game_network_bindings bindings = managed_network_bindings();
    slayer3d_data_game_network_loop_result result;
    char error[192] = {0};
    if (!slayer3d_data_game_runtime_update_network_client_session(
            runtime, ctx, SLAYER3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION, &bindings, playing, true, dt, &result, error,
            (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D managed direct-connect update failed: %s",
                    error[0] != '\0' ? error : "unknown error");
    }

    if (result.received_start_game)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D managed client received start-game control");
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
        (void)slayer3d_game_data_log_network_snapshot_diagnostic(
            runtime->data, SLAYER3D_MANAGED_NETWORK_DIAGNOSTIC_SNAPSHOT, result.last_tick, "client_snapshot_applied",
            "applied", NULL, error, (int)sizeof(error));
    }

    session = slayer3d_game_data_get_network_direct_connect_session(runtime->data,
                                                                    SLAYER3D_MANAGED_NETWORK_DIRECT_CONNECT_SESSION);
    if (session == NULL)
        return;

    const slayer3d_network_state state = result.session_state;
    if (state == SLAYER3D_NETWORK_STATE_REJECTED || state == SLAYER3D_NETWORK_STATE_TIMED_OUT ||
        state == SLAYER3D_NETWORK_STATE_ERROR)
    {
        const char *reason =
            state == SLAYER3D_NETWORK_STATE_TIMED_OUT
                ? managed_network_message(runtime, "disconnect_reasons", "host_timed_out", "Host timed out")
            : state == SLAYER3D_NETWORK_STATE_REJECTED
                ? managed_network_message(runtime, "disconnect_reasons", "host_rejected", "Host rejected connection")
                : managed_network_message(runtime, "disconnect_reasons", "host_error", "Host connection error");
        if (was_playing)
            managed_network_disconnect_flow(runtime, ctx, false, reason);
        else
            (void)managed_network_run_flow_event(runtime, ctx, "client_connection_closed", reason);
    }
}

static void managed_network_update_client_sensors(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx)
{
    if (runtime == NULL || runtime->data == NULL || ctx == NULL || !managed_network_is_play_scene(runtime) ||
        !managed_network_is_role_client(runtime) || runtime->managed_network_camera_toggle_signal_id < 0)
    {
        return;
    }

    slayer3d_input_manager *input = runtime->session != NULL ? slayer3d_game_session_get_input(runtime->session) : NULL;
    const int camera_toggle_action = managed_network_action_id(runtime, SLAYER3D_MANAGED_NETWORK_BINDING_CAMERA_TOGGLE);
    if (input != NULL && camera_toggle_action >= 0 && slayer3d_input_is_pressed(input, camera_toggle_action))
    {
        slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(ctx->session),
                             runtime->managed_network_camera_toggle_signal_id, NULL);
    }
}

static void managed_network_update_before_frame(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx,
                                                float dt)
{
    if (runtime == NULL || !runtime->managed_network_enabled)
        return;

    managed_network_process_lobby_start(runtime, ctx);
    managed_network_update_termination_ack(runtime, ctx, dt);
    managed_network_update_host(runtime, ctx, dt);
    managed_network_update_direct_connect(runtime, ctx, dt);
    managed_network_update_client_sensors(runtime, ctx);
}

static void managed_network_update_after_frame(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx)
{
    if (runtime == NULL || runtime->data == NULL || !runtime->managed_network_enabled)
        return;

    slayer3d_network_session *session =
        slayer3d_game_data_get_network_host_session(runtime->data, SLAYER3D_MANAGED_NETWORK_HOST_SESSION);
    if (session == NULL || !slayer3d_network_session_is_connected(session) || !managed_network_is_play_scene(runtime) ||
        !managed_network_is_role_host(runtime))
    {
        return;
    }

    const slayer3d_data_game_network_bindings bindings = managed_network_bindings();
    slayer3d_data_game_network_loop_result result;
    char error[192] = {0};
    if (!slayer3d_data_game_runtime_publish_network_host_snapshot(runtime, ctx, SLAYER3D_MANAGED_NETWORK_HOST_SESSION,
                                                                  &bindings, &result, error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D managed host snapshot publish failed: %s",
                    error[0] != '\0' ? error : "unknown error");
        return;
    }
    (void)slayer3d_game_data_log_network_snapshot_diagnostic(
        runtime->data, SLAYER3D_MANAGED_NETWORK_DIAGNOSTIC_SNAPSHOT, result.last_tick, "host_snapshot_sent", "sent",
        NULL, error, (int)sizeof(error));
}

void slayer3d_data_game_runtime_desc_init(slayer3d_data_game_runtime_desc *desc)
{
    if (desc == NULL)
    {
        return;
    }
    SDL_zero(*desc);
}

bool slayer3d_data_game_runtime_create(const slayer3d_data_game_runtime_desc *desc,
                                       slayer3d_data_game_runtime **out_runtime, char *error_buffer,
                                       int error_buffer_size)
{
    char load_error[512] = {0};
    slayer3d_data_game_runtime *runtime = NULL;

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

    runtime = (slayer3d_data_game_runtime *)SDL_calloc(1, sizeof(*runtime));
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
    slayer3d_game_data_font_cache_init(&runtime->font_cache, desc->media_dir);
    slayer3d_game_data_particle_cache_init(&runtime->particle_cache);
    slayer3d_game_data_app_flow_init(&runtime->app_flow);
    slayer3d_game_data_frame_state_init(&runtime->frame_state);
    slayer3d_game_data_input_profile_refresh_state_init(&runtime->input_profile_refresh);

    runtime->assets = slayer3d_asset_resolver_create();
    if (runtime->assets == NULL)
    {
        set_error(error_buffer, error_buffer_size, SDL_GetError());
        slayer3d_data_game_runtime_destroy(runtime);
        return false;
    }

    if (desc->mount_assets != NULL &&
        !desc->mount_assets(runtime->assets, desc->mount_userdata, load_error, (int)sizeof(load_error)))
    {
        set_error(error_buffer, error_buffer_size, load_error[0] != '\0' ? load_error : "asset mount failed");
        slayer3d_data_game_runtime_destroy(runtime);
        return false;
    }

    slayer3d_game_data_load_options load_options;
    SDL_zero(load_options);
    load_options.session = desc->session;
    load_options.initial_scene_override = desc->initial_scene_override;
    load_options.initial_scene_state = desc->initial_scene_state;
    load_options.initial_scene_payload = desc->initial_scene_payload;
    load_options.initial_player_start = desc->initial_player_start;
    if (!slayer3d_game_data_load_asset_with_options(runtime->assets, desc->data_asset_path, &load_options,
                                                    &runtime->data, load_error, (int)sizeof(load_error)))
    {
        set_error(error_buffer, error_buffer_size, load_error[0] != '\0' ? load_error : "game data load failed");
        slayer3d_data_game_runtime_destroy(runtime);
        return false;
    }
    runtime->managed_network_enabled =
        runtime->managed_network_enabled && slayer3d_game_data_network_managed_runtime_enabled(runtime->data);

    slayer3d_game_data_image_cache_init(&runtime->image_cache, runtime->assets);
    slayer3d_game_data_sprite_cache_init(&runtime->sprite_cache, runtime->assets);
    slayer3d_game_data_model_cache_init(&runtime->model_cache, runtime->assets);
    slayer3d_game_data_mesh_primitive_cache_init(&runtime->mesh_primitive_cache);
    slayer3d_game_data_asset_warmup_queue_init(&runtime->asset_warmup, 1);
    (void)slayer3d_game_data_asset_warmup_queue_start_workers(&runtime->asset_warmup, runtime->data, runtime->assets,
                                                              1);
    if (!slayer3d_game_data_app_flow_start(&runtime->app_flow, runtime->data))
    {
        set_error(error_buffer, error_buffer_size, SDL_GetError());
        slayer3d_data_game_runtime_destroy(runtime);
        return false;
    }
    if (desc->skip_app_flow_startup)
        slayer3d_transition_reset(&runtime->app_flow.transition);
    if (!connect_haptics_policies(runtime))
    {
        set_error(error_buffer, error_buffer_size, SDL_GetError());
        slayer3d_data_game_runtime_destroy(runtime);
        return false;
    }
    if (!connect_managed_network(runtime))
    {
        set_error(error_buffer, error_buffer_size, SDL_GetError());
        slayer3d_data_game_runtime_destroy(runtime);
        return false;
    }

    *out_runtime = runtime;
    return true;
}

void slayer3d_data_game_runtime_destroy(slayer3d_data_game_runtime *runtime)
{
    if (runtime == NULL)
    {
        return;
    }

    disconnect_managed_network(runtime);
    disconnect_haptics_policies(runtime);
    slayer3d_game_data_particle_cache_free(&runtime->particle_cache);
    slayer3d_game_data_mesh_primitive_cache_free(&runtime->mesh_primitive_cache);
    slayer3d_game_data_asset_warmup_queue_free(&runtime->asset_warmup);
    slayer3d_game_data_model_cache_free(&runtime->model_cache);
    slayer3d_game_data_sprite_cache_free(&runtime->sprite_cache);
    slayer3d_game_data_image_cache_free(&runtime->image_cache);
    slayer3d_game_data_font_cache_free(&runtime->font_cache);
    slayer3d_game_data_destroy(runtime->data);
    slayer3d_asset_resolver_destroy(runtime->assets);
    SDL_free(runtime);
}

void slayer3d_data_game_runtime_release_mouse_capture(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx)
{
    data_game_release_mouse_capture(runtime, ctx);
}

void slayer3d_data_game_runtime_apply_mouse_capture(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx)
{
    data_game_apply_scene_mouse_capture(runtime, ctx);
}

slayer3d_asset_resolver *slayer3d_data_game_runtime_assets(const slayer3d_data_game_runtime *runtime)
{
    return runtime != NULL ? runtime->assets : NULL;
}

slayer3d_game_data_runtime *slayer3d_data_game_runtime_data(const slayer3d_data_game_runtime *runtime)
{
    return runtime != NULL ? runtime->data : NULL;
}

bool slayer3d_data_game_runtime_asset_warmup_stats(const slayer3d_data_game_runtime *runtime,
                                                   slayer3d_game_data_asset_warmup_stats *out_stats)
{
    if (out_stats == NULL)
        return false;
    SDL_zero(*out_stats);
    if (runtime == NULL)
        return false;
    slayer3d_game_data_asset_warmup_queue_stats(&runtime->asset_warmup, out_stats);
    return true;
}

bool slayer3d_data_game_runtime_publish_asset_warmup_stats(slayer3d_data_game_runtime *runtime, const char *prefix)
{
    if (runtime == NULL || runtime->data == NULL)
        return false;
    slayer3d_properties *state = slayer3d_game_data_mutable_scene_state(runtime->data);
    if (state == NULL)
        return false;

    slayer3d_game_data_asset_warmup_stats stats;
    SDL_zero(stats);
    slayer3d_game_data_asset_warmup_queue_stats(&runtime->asset_warmup, &stats);
    data_game_set_warmup_int(state, prefix, "total", stats.total);
    data_game_set_warmup_int(state, prefix, "queued", stats.queued);
    data_game_set_warmup_int(state, prefix, "loading", stats.loading);
    data_game_set_warmup_int(state, prefix, "ready_for_finalize", stats.ready_for_finalize);
    data_game_set_warmup_int(state, prefix, "pending", stats.pending);
    data_game_set_warmup_int(state, prefix, "ready", stats.ready);
    data_game_set_warmup_int(state, prefix, "failed", stats.failed);
    data_game_set_warmup_int(state, prefix, "canceled", stats.canceled);
    data_game_set_warmup_int(state, prefix, "completed", stats.completed);
    data_game_set_warmup_float(state, prefix, "progress", stats.progress);
    data_game_set_warmup_bool(state, prefix, "active", stats.pending > 0);
    data_game_set_warmup_bool(state, prefix, "complete", stats.pending == 0);

    const char *status = "idle";
    if (stats.failed > 0)
        status = "failed";
    else if (stats.pending > 0)
        status = "loading";
    else if (stats.total > 0)
        status = "ready";
    data_game_set_warmup_string(state, prefix, "status", status);

    data_game_publish_ui_image_warmup_context image_context;
    SDL_zero(image_context);
    image_context.queue = &runtime->asset_warmup;
    image_context.state = state;
    image_context.prefix = prefix;
    (void)slayer3d_game_data_for_each_ui_image(runtime->data, data_game_publish_ui_image_warmup, &image_context);
    return true;
}

bool slayer3d_data_game_runtime_refresh_input_profile_on_device_change(slayer3d_data_game_runtime *runtime,
                                                                       slayer3d_input_manager *input,
                                                                       const char **out_profile_name, bool *out_applied,
                                                                       char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || runtime->data == NULL)
    {
        set_error(error_buffer, error_buffer_size, "input profile refresh requires data-game runtime");
        return false;
    }
    return slayer3d_game_data_apply_active_input_profile_on_device_change(
        runtime->data, input, &runtime->input_profile_refresh, out_profile_name, out_applied, error_buffer,
        error_buffer_size);
}

bool slayer3d_data_game_runtime_update_network_host_session(slayer3d_data_game_runtime *runtime,
                                                            slayer3d_game_context *ctx, const char *session_name,
                                                            const slayer3d_data_game_network_bindings *bindings,
                                                            bool playing, float dt,
                                                            slayer3d_data_game_network_loop_result *out_result,
                                                            char *error_buffer, int error_buffer_size)
{
    Uint8 packet[SLAYER3D_NETWORK_MAX_PACKET_SIZE];
    slayer3d_network_session *session = runtime != NULL && runtime->data != NULL
                                            ? slayer3d_game_data_get_network_host_session(runtime->data, session_name)
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

    if (!slayer3d_network_session_update(session, dt))
    {
        set_errorf(error_buffer, error_buffer_size, "network host session update failed: %s", SDL_GetError());
        network_loop_result_init(out_result, session);
        return false;
    }

    int packet_size = 0;
    while ((packet_size = slayer3d_network_session_receive(session, packet, (int)sizeof(packet))) > 0)
    {
        if (out_result != NULL)
            ++out_result->packets_received;

        const char *control_binding = NULL;
        slayer3d_game_data_network_control control;
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
            slayer3d_input_manager *input =
                runtime->session != NULL ? slayer3d_game_session_get_input(runtime->session) : NULL;
            if (slayer3d_game_data_apply_network_runtime_input(runtime->data, bindings->client_input, input, packet,
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
        out_result->session_state = slayer3d_network_session_state(session);
    return true;
}

bool slayer3d_data_game_runtime_publish_network_host_snapshot(slayer3d_data_game_runtime *runtime,
                                                              slayer3d_game_context *ctx, const char *session_name,
                                                              const slayer3d_data_game_network_bindings *bindings,
                                                              slayer3d_data_game_network_loop_result *out_result,
                                                              char *error_buffer, int error_buffer_size)
{
    slayer3d_network_session *session = runtime != NULL && runtime->data != NULL
                                            ? slayer3d_game_data_get_network_host_session(runtime->data, session_name)
                                            : NULL;
    network_loop_result_init(out_result, session);
    if (runtime == NULL || runtime->data == NULL || bindings == NULL || bindings->state_snapshot == NULL ||
        bindings->state_snapshot[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "network host snapshot publish requires runtime and binding");
        return false;
    }
    if (session == NULL || !slayer3d_network_session_is_connected(session))
    {
        set_error(error_buffer, error_buffer_size, "network host snapshot publish requires connected host session");
        return false;
    }
    if (!data_game_sync_network_pause_from_context(runtime, ctx, error_buffer, error_buffer_size))
    {
        return false;
    }

    const Uint32 tick = data_game_input_tick(runtime);
    if (!slayer3d_game_data_send_network_runtime_snapshot(runtime->data, session, bindings->state_snapshot, tick,
                                                          error_buffer, error_buffer_size))
    {
        return false;
    }
    if (out_result != NULL)
    {
        out_result->sent_snapshot = true;
        out_result->last_tick = tick;
        out_result->session_state = slayer3d_network_session_state(session);
    }
    return true;
}

bool slayer3d_data_game_runtime_update_network_client_session(slayer3d_data_game_runtime *runtime,
                                                              slayer3d_game_context *ctx, const char *session_name,
                                                              const slayer3d_data_game_network_bindings *bindings,
                                                              bool playing, bool allow_pause_requests, float dt,
                                                              slayer3d_data_game_network_loop_result *out_result,
                                                              char *error_buffer, int error_buffer_size)
{
    Uint8 packet[SLAYER3D_NETWORK_MAX_PACKET_SIZE];
    slayer3d_network_session *session =
        runtime != NULL && runtime->data != NULL
            ? slayer3d_game_data_get_network_direct_connect_session(runtime->data, session_name)
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

    if (!slayer3d_network_session_update(session, dt))
    {
        set_errorf(error_buffer, error_buffer_size, "network client session update failed: %s", SDL_GetError());
        network_loop_result_init(out_result, session);
        return false;
    }

    int packet_size = 0;
    while ((packet_size = slayer3d_network_session_receive(session, packet, (int)sizeof(packet))) > 0)
    {
        if (out_result != NULL)
            ++out_result->packets_received;

        const char *control_binding = NULL;
        slayer3d_game_data_network_control control;
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
            if (slayer3d_game_data_apply_network_runtime_snapshot(runtime->data, bindings->state_snapshot, packet,
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

    if (playing && slayer3d_network_session_is_connected(session))
    {
        slayer3d_input_manager *input =
            runtime->session != NULL ? slayer3d_game_session_get_input(runtime->session) : NULL;
        const Uint32 tick = data_game_input_tick(runtime);
        if (allow_pause_requests && ctx != NULL)
        {
            int pause_action = -1;
            if (slayer3d_game_data_get_network_runtime_pause_action(runtime->data, &pause_action) && input != NULL &&
                slayer3d_input_is_pressed(input, pause_action))
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
            if (!slayer3d_game_data_send_network_runtime_input(runtime->data, session, bindings->client_input, input,
                                                               tick, error_buffer, error_buffer_size))
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
        out_result->session_state = slayer3d_network_session_state(session);
    return true;
}

static bool refresh_active_input_profile_if_available(slayer3d_data_game_runtime *runtime)
{
    slayer3d_input_manager *input =
        runtime != NULL && runtime->session != NULL ? slayer3d_game_session_get_input(runtime->session) : NULL;
    if (runtime == NULL || runtime->data == NULL || input == NULL)
    {
        return true;
    }

    if (!slayer3d_game_data_get_active_input_profile_name(runtime->data, input, NULL))
    {
        slayer3d_game_data_input_profile_refresh_state_init(&runtime->input_profile_refresh);
        return true;
    }

    char error[256] = "";
    const char *profile_name = NULL;
    bool applied = false;
    if (!slayer3d_data_game_runtime_refresh_input_profile_on_device_change(runtime, input, &profile_name, &applied,
                                                                           error, (int)sizeof(error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D input profile hotplug refresh failed: %s",
                    error[0] != '\0' ? error : "unknown error");
        return false;
    }
    return true;
}

bool slayer3d_data_game_runtime_update_frame(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx, float dt)
{
    const Uint64 start_counter = SDL_GetPerformanceCounter();
    if (runtime == NULL || runtime->data == NULL)
    {
        return false;
    }

    if (!refresh_active_input_profile_if_available(runtime))
        return false;

    data_game_apply_scene_mouse_capture(runtime, ctx);
    managed_network_update_before_frame(runtime, ctx, dt);

    const slayer3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                        .runtime = runtime->data,
                                                        .app_flow = &runtime->app_flow,
                                                        .particle_cache = &runtime->particle_cache,
                                                        .dt = dt};
    if (!slayer3d_game_data_update_frame(&runtime->frame_state, &frame))
        return false;

    managed_network_update_after_frame(runtime, ctx);
    data_game_apply_scene_mouse_capture(runtime, ctx);
    slayer3d_game_data_frame_state_record_update_cpu_time(
        &runtime->frame_state, data_game_elapsed_seconds(start_counter, SDL_GetPerformanceCounter()));
    return true;
}

void slayer3d_data_game_runtime_render(slayer3d_data_game_runtime *runtime, slayer3d_game_context *ctx)
{
    if (runtime == NULL || runtime->data == NULL || ctx == NULL)
    {
        return;
    }

    slayer3d_game_data_frame_state_record_render(&runtime->frame_state, ctx, runtime->data);
    (void)slayer3d_data_game_runtime_publish_asset_warmup_stats(runtime, NULL);

    slayer3d_game_data_frame_desc frame;
    SDL_zero(frame);
    frame.runtime = runtime->data;
    frame.renderer = ctx->renderer;
    frame.font_cache = &runtime->font_cache;
    frame.image_cache = &runtime->image_cache;
    frame.particle_cache = &runtime->particle_cache;
    frame.sprite_cache = &runtime->sprite_cache;
    frame.model_cache = &runtime->model_cache;
    frame.mesh_primitive_cache = &runtime->mesh_primitive_cache;
    frame.asset_warmup = &runtime->asset_warmup;
    frame.app_flow = &runtime->app_flow;
    frame.metrics = &runtime->frame_state.metrics;
    frame.render_eval = &runtime->frame_state.render_eval;
    frame.pulse_phase = runtime->frame_state.ui_pulse_phase;
    const Uint64 start_counter = SDL_GetPerformanceCounter();
    slayer3d_game_data_draw_frame(&frame);
    (void)slayer3d_data_game_runtime_publish_asset_warmup_stats(runtime, NULL);
    slayer3d_game_data_frame_state_record_render_cpu_time(
        &runtime->frame_state, data_game_elapsed_seconds(start_counter, SDL_GetPerformanceCounter()));
}

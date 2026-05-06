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
};

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

    *out_runtime = runtime;
    return true;
}

void sdl3d_data_game_runtime_destroy(sdl3d_data_game_runtime *runtime)
{
    if (runtime == NULL)
    {
        return;
    }

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

    const sdl3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                     .runtime = runtime->data,
                                                     .app_flow = &runtime->app_flow,
                                                     .particle_cache = &runtime->particle_cache,
                                                     .dt = dt};
    return sdl3d_game_data_update_frame(&runtime->frame_state, &frame);
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

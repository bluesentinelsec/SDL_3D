#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include <stdbool.h>
#include <string.h>

#include "sdl3d/asset.h"
#include "sdl3d/font.h"
#include "sdl3d/game.h"
#include "sdl3d/game_data.h"
#include "sdl3d/game_presentation.h"
#include "sdl3d/network.h"
#include "sdl3d/properties.h"
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
    sdl3d_network_session *host_session;
    sdl3d_network_session *direct_connect_session;
    char host_status[SDL3D_NETWORK_MAX_STATUS_LENGTH];
    char host_endpoint[SDL3D_NETWORK_MAX_STATUS_LENGTH];
    char direct_connect_host[SDL3D_NETWORK_MAX_HOST_LENGTH];
    char direct_connect_port[16];
    char direct_connect_status[SDL3D_NETWORK_MAX_STATUS_LENGTH];
    int paddle_hit_connection;
    int vibration_connection;
    int lobby_start_connection;
    int host_start_signal_id;
    int ball_hit_signal_id;
    int vibration_signal_id;
    int local_input_gamepad_count;
} pong_state;

typedef enum pong_network_message_kind
{
    PONG_NETWORK_MESSAGE_START_GAME = 1,
} pong_network_message_kind;

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
    SDL_snprintf(state->host_status, sizeof(state->host_status), "%s",
                 status != NULL && status[0] != '\0' ? status : "Not hosting");
    SDL_snprintf(state->host_endpoint, sizeof(state->host_endpoint), "UDP %u", (unsigned int)port);

    sdl3d_properties_set_string(scene_state, "multiplayer_host_status", state->host_status);
    sdl3d_properties_set_string(scene_state, "multiplayer_host_endpoint", state->host_endpoint);
    sdl3d_properties_set_string(scene_state, "multiplayer_host_client",
                                state->host_session != NULL && sdl3d_network_session_is_connected(state->host_session)
                                    ? "Client connected"
                                    : "Waiting for client");
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

static bool configure_play_input(void *userdata, sdl3d_game_data_runtime *runtime, const char *adapter_name,
                                 sdl3d_registered_actor *target, const sdl3d_properties *payload)
{
    pong_state *state = (pong_state *)userdata;
    (void)runtime;
    (void)adapter_name;
    (void)target;
    (void)payload;

    if (state == NULL || state->data == NULL || state->input == NULL)
    {
        return false;
    }

    if (is_local_match_mode(state))
    {
        bind_local_play_controls(state);
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
    SDL_snprintf(state->direct_connect_status, sizeof(state->direct_connect_status), "Enter host and port");
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
                if (state->data != NULL)
                {
                    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(state->data);
                    if (scene_state != NULL)
                    {
                        sdl3d_properties_set_string(scene_state, "match_mode", "lan");
                    }
                }
                (void)sdl3d_game_data_set_active_scene(state->data, "scene.play");
                break;
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

        update_host_session_scene_state(state);

        const char *active_scene = active_scene_name(state);
        if (active_scene == NULL || (!is_multiplayer_lobby_scene(state) && !is_multiplayer_play_scene(state)))
        {
            destroy_host_session(state);
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

    sdl3d_ui_layout_label(state->direct_connect_ui, "DIRECT CONNECT");
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

    sdl3d_ui_layout_label(state->direct_connect_ui, "Status");
    sdl3d_ui_layout_label(state->direct_connect_ui, state->direct_connect_status);
    sdl3d_ui_separator(state->direct_connect_ui);
    sdl3d_ui_layout_label(state->direct_connect_ui, "Back returns to the LAN menu.");

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

    if (state->host_session == NULL || !sdl3d_network_session_is_connected(state->host_session))
    {
        SDL_snprintf(state->host_status, sizeof(state->host_status), "Waiting for client");
        update_host_session_scene_state(state);
        return;
    }

    if (!send_host_start_packet(state))
    {
        return;
    }

    if (state->data != NULL)
    {
        sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(state->data);
        if (scene_state != NULL)
        {
            sdl3d_properties_set_string(scene_state, "match_mode", "lan");
        }
    }

    if (!sdl3d_game_data_set_active_scene(state->data, "scene.play"))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong host failed to enter multiplayer play scene: %s",
                    SDL_GetError());
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
    reset_direct_connect_defaults(state);
    if (ctx != NULL && ctx->window != NULL)
    {
        SDL_StartTextInput(ctx->window);
    }
    if (state->direct_connect_ui != NULL && ctx != NULL && ctx->renderer != NULL && ctx->window != NULL)
    {
        sdl3d_ui_begin_frame_ex(state->direct_connect_ui, ctx->renderer, ctx->window);
    }
    sdl3d_game_data_image_cache_init(&state->image_cache, state->assets);
    if (!sdl3d_game_data_app_flow_start(&state->app_flow, state->data))
    {
        sdl3d_game_data_image_cache_free(&state->image_cache);
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
    state->host_start_signal_id = sdl3d_game_data_find_signal(state->data, "signal.multiplayer.lobby.start");
    state->ball_hit_signal_id = sdl3d_game_data_find_signal(state->data, "signal.ball.hit_paddle");
    state->vibration_signal_id = sdl3d_game_data_find_signal(state->data, "signal.settings.vibration");
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
    }
    return true;
}

static bool pong_handle_event(sdl3d_game_context *ctx, void *userdata, const SDL_Event *event)
{
    pong_state *state = (pong_state *)userdata;
    if (state != NULL && is_direct_connect_scene(state) && state->direct_connect_ui != NULL && event != NULL)
    {
        (void)sdl3d_ui_process_event(state->direct_connect_ui, event);
    }
    (void)ctx;
    return true;
}

static void pong_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    pong_state *state = (pong_state *)userdata;
    refresh_local_play_input(state);

    const sdl3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                     .runtime = state->data,
                                                     .app_flow = &state->app_flow,
                                                     .particle_cache = &state->particle_cache,
                                                     .dt = dt};
    (void)sdl3d_game_data_update_frame(&state->frame_state, &frame);

    update_multiplayer_sessions(state, dt);
}

static void pong_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    pong_state *state = (pong_state *)userdata;
    refresh_local_play_input(state);

    const sdl3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                     .runtime = state->data,
                                                     .app_flow = &state->app_flow,
                                                     .particle_cache = &state->particle_cache,
                                                     .dt = real_dt};
    (void)sdl3d_game_data_update_frame(&state->frame_state, &frame);

    update_multiplayer_sessions(state, real_dt);
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

    if (state->direct_connect_ui != NULL)
    {
        sdl3d_ui_end_frame(state->direct_connect_ui);
        sdl3d_ui_render(state->direct_connect_ui, ctx->renderer);
        sdl3d_ui_begin_frame_ex(state->direct_connect_ui, ctx->renderer, ctx->window);
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

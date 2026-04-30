#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include <stdbool.h>
#include <string.h>

#include "sdl3d/asset.h"
#include "sdl3d/game.h"
#include "sdl3d/game_data.h"
#include "sdl3d/game_presentation.h"
#include "sdl3d/properties.h"

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
    int paddle_hit_connection;
    int vibration_connection;
    int ball_hit_signal_id;
    int vibration_signal_id;
} pong_state;

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
        SDL_strcmp(other_actor_name, "entity.paddle.player") == 0)
    {
        if (!sdl3d_input_rumble_all_gamepads(state->input, 0.45f, 0.75f, 120))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Pong paddle hit rumble requested but no rumble-capable gamepad accepted it");
        }
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
    sdl3d_game_data_image_cache_init(&state->image_cache, state->assets);
    if (!sdl3d_game_data_app_flow_start(&state->app_flow, state->data))
    {
        sdl3d_game_data_image_cache_free(&state->image_cache);
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
    state->ball_hit_signal_id = sdl3d_game_data_find_signal(state->data, "signal.ball.hit_paddle");
    state->vibration_signal_id = sdl3d_game_data_find_signal(state->data, "signal.settings.vibration");
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
    }
    return true;
}

static bool pong_handle_event(sdl3d_game_context *ctx, void *userdata, const SDL_Event *event)
{
    (void)ctx;
    (void)userdata;
    (void)event;
    return true;
}

static void pong_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    pong_state *state = (pong_state *)userdata;

    const sdl3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                     .runtime = state->data,
                                                     .app_flow = &state->app_flow,
                                                     .particle_cache = &state->particle_cache,
                                                     .dt = dt};
    (void)sdl3d_game_data_update_frame(&state->frame_state, &frame);
}

static void pong_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    pong_state *state = (pong_state *)userdata;

    const sdl3d_game_data_update_frame_desc frame = {.ctx = ctx,
                                                     .runtime = state->data,
                                                     .app_flow = &state->app_flow,
                                                     .particle_cache = &state->particle_cache,
                                                     .dt = real_dt};
    (void)sdl3d_game_data_update_frame(&state->frame_state, &frame);
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

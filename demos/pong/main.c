#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include <stdbool.h>

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
    sdl3d_game_data_runtime *data;
    sdl3d_game_data_font_cache font_cache;
    sdl3d_game_data_particle_cache particle_cache;
    sdl3d_game_data_app_flow app_flow;
    float time;
    float pause_flash;
    float last_render_time;
    float fps_sample_time;
    float displayed_fps;
    int fps_sample_frames;
    Uint64 rendered_frames;
} pong_state;

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

static sdl3d_registered_actor *actor_with_tags(const pong_state *state, const char *tag0, const char *tag1)
{
    const char *tags[2] = {tag0, tag1};
    return tag1 != NULL ? sdl3d_game_data_find_actor_with_tags(state->data, tags, 2)
                        : sdl3d_game_data_find_actor_with_tag(state->data, tag0);
}

static bool match_is_finished(const pong_state *state)
{
    sdl3d_registered_actor *match = actor_with_tags(state, "state", "match");
    return match != NULL && sdl3d_properties_get_bool(match->props, "finished", false);
}

static float presentation_float(const pong_state *state, const char *key, float fallback)
{
    sdl3d_registered_actor *presentation = actor_with_tags(state, "state", "presentation");
    return presentation != NULL ? sdl3d_properties_get_float(presentation->props, key, fallback) : fallback;
}

static bool init_game_data(sdl3d_game_context *ctx, pong_state *state)
{
    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    if (assets == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong asset resolver allocation failed");
        return false;
    }

    char error[512];
    bool assets_ready = mount_pong_assets(assets, error, (int)sizeof(error));
    if (!assets_ready)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong asset mount failed: %s", error);
        sdl3d_asset_resolver_destroy(assets);
        return false;
    }

    if (!sdl3d_game_data_load_asset(assets, SDL3D_PONG_DATA_ASSET_PATH, ctx->session, &state->data, error,
                                    (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pong data load failed: %s", error);
        sdl3d_asset_resolver_destroy(assets);
        return false;
    }
    sdl3d_asset_resolver_destroy(assets);

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

    if (!init_game_data(ctx, state))
    {
        return false;
    }
    if (!sdl3d_game_data_app_flow_start(&state->app_flow, state->data))
    {
        return false;
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

static void update_visual_effects(pong_state *state, float dt)
{
    state->time += dt;
    sdl3d_game_data_update_property_effects(state->data, dt);
    sdl3d_game_data_update_particles(state->data, &state->particle_cache, dt);
}

static void pong_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    pong_state *state = (pong_state *)userdata;

    const bool was_paused = ctx->paused;
    sdl3d_game_data_app_flow_update(&state->app_flow, ctx, state->data, !match_is_finished(state), dt);
    if (!was_paused && ctx->paused)
        state->pause_flash = 0.0f;

    update_visual_effects(state, dt);

    if (!sdl3d_game_data_app_flow_quit_pending(&state->app_flow) && !ctx->paused &&
        sdl3d_game_data_active_scene_updates_game(state->data))
    {
        sdl3d_game_data_update(state->data, dt);
    }
}

static void pong_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    pong_state *state = (pong_state *)userdata;

    sdl3d_game_data_app_flow_update(&state->app_flow, ctx, state->data, true, real_dt);
    if (!ctx->paused)
        return;

    state->pause_flash += real_dt * presentation_float(state, "pause_flash_speed", 3.0f);
    while (state->pause_flash >= 1.0f)
    {
        state->pause_flash -= 1.0f;
    }
    update_visual_effects(state, real_dt);
}

static void pong_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    pong_state *state = (pong_state *)userdata;
    (void)alpha;

    if (state->rendered_frames > 0)
    {
        const float frame_dt = ctx->real_time - state->last_render_time;
        if (frame_dt > 0.0f)
        {
            state->fps_sample_time += frame_dt;
            ++state->fps_sample_frames;
            if (state->fps_sample_time >= 0.25f)
            {
                state->displayed_fps = (float)state->fps_sample_frames / state->fps_sample_time;
                state->fps_sample_time = 0.0f;
                state->fps_sample_frames = 0;
            }
        }
    }
    state->last_render_time = ctx->real_time;
    ++state->rendered_frames;

    sdl3d_game_data_ui_metrics metrics;
    SDL_zero(metrics);
    metrics.paused = ctx->paused;
    metrics.fps = state->displayed_fps;
    metrics.frame = state->rendered_frames;

    const sdl3d_game_data_render_eval render_eval = {.time = state->time};
    sdl3d_game_data_frame_desc frame;
    SDL_zero(frame);
    frame.runtime = state->data;
    frame.renderer = ctx->renderer;
    frame.font_cache = &state->font_cache;
    frame.particle_cache = &state->particle_cache;
    frame.app_flow = &state->app_flow;
    frame.metrics = &metrics;
    frame.render_eval = &render_eval;
    frame.pulse_phase = state->pause_flash;
    sdl3d_game_data_draw_frame(&frame);
}

static void pong_shutdown(sdl3d_game_context *ctx, void *userdata)
{
    pong_state *state = (pong_state *)userdata;
    (void)ctx;

    sdl3d_game_data_particle_cache_free(&state->particle_cache);
    sdl3d_game_data_font_cache_free(&state->font_cache);
    sdl3d_game_data_destroy(state->data);
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
    callbacks.event = pong_handle_event;
    callbacks.tick = pong_tick;
    callbacks.pause_tick = pong_pause_tick;
    callbacks.render = pong_render;
    callbacks.shutdown = pong_shutdown;

    return sdl3d_run_game(&config, &callbacks, &state);
}

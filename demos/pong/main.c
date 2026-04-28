#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include <stdbool.h>

#include "sdl3d/asset.h"
#include "sdl3d/camera.h"
#include "sdl3d/drawing3d.h"
#include "sdl3d/effects.h"
#include "sdl3d/font.h"
#include "sdl3d/game.h"
#include "sdl3d/game_data.h"
#include "sdl3d/input.h"
#include "sdl3d/lighting.h"
#include "sdl3d/math.h"
#include "sdl3d/properties.h"
#include "sdl3d/render_context.h"
#include "sdl3d/shapes.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/transition.h"

#if SDL3D_PONG_EMBEDDED_ASSETS
#include "sdl3d_pong_assets.h"
#endif

typedef struct pong_state
{
    sdl3d_game_data_runtime *data;
    sdl3d_font font;
    bool has_font;
    sdl3d_particle_emitter *ambient_particles;
    const char *ambient_particles_entity;
    sdl3d_vec3 ambient_particles_emissive;
    sdl3d_transition transition;
    sdl3d_game_data_app_control app;
    float time;
    float pause_flash;
    float border_flash;
    float paddle_flash;
    float last_render_time;
    float fps_sample_time;
    float displayed_fps;
    int fps_sample_frames;
    Uint64 rendered_frames;
    bool ball_active;
    bool match_finished;
    bool quit_pending;
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

static sdl3d_input_manager *ctx_input(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_input(ctx->session);
}

static sdl3d_signal_bus *ctx_bus(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_signal_bus(ctx->session);
}

static float fade_down(float value, float dt, float speed)
{
    value -= dt * speed;
    return value > 0.0f ? value : 0.0f;
}

static void start_quit_fade(sdl3d_game_context *ctx, pong_state *state)
{
    if (state->quit_pending)
    {
        return;
    }

    state->quit_pending = true;
    sdl3d_game_data_transition_desc transition;
    if (state->app.quit_transition == NULL ||
        !sdl3d_game_data_get_transition(state->data, state->app.quit_transition, &transition))
    {
        ctx->quit_requested = true;
        return;
    }
    sdl3d_transition_start(&state->transition, transition.type, transition.direction, transition.color,
                           transition.duration, transition.done_signal_id);
}

static void on_quit_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    sdl3d_game_context *ctx = (sdl3d_game_context *)userdata;
    (void)signal_id;
    (void)payload;
    ctx->quit_requested = true;
}

static sdl3d_registered_actor *actor_with_tags(const pong_state *state, const char *tag0, const char *tag1)
{
    const char *tags[2] = {tag0, tag1};
    return tag1 != NULL ? sdl3d_game_data_find_actor_with_tags(state->data, tags, 2)
                        : sdl3d_game_data_find_actor_with_tag(state->data, tag0);
}

static void sync_state_from_data(pong_state *state)
{
    sdl3d_registered_actor *ball = actor_with_tags(state, "ball", NULL);
    sdl3d_registered_actor *match = actor_with_tags(state, "state", "match");
    sdl3d_registered_actor *presentation = actor_with_tags(state, "state", "presentation");

    if (ball != NULL)
    {
        state->ball_active = sdl3d_properties_get_bool(ball->props, "active_motion", false);
    }
    if (match != NULL)
    {
        state->match_finished = sdl3d_properties_get_bool(match->props, "finished", false);
    }
    if (presentation != NULL)
    {
        state->border_flash = sdl3d_properties_get_float(presentation->props, "border_flash", 0.0f);
        state->paddle_flash = sdl3d_properties_get_float(presentation->props, "paddle_flash", 0.0f);
    }
}

static sdl3d_registered_actor *presentation_actor(const pong_state *state)
{
    return actor_with_tags(state, "state", "presentation");
}

static float presentation_float(const pong_state *state, const char *key, float fallback)
{
    sdl3d_registered_actor *presentation = presentation_actor(state);
    return presentation != NULL ? sdl3d_properties_get_float(presentation->props, key, fallback) : fallback;
}

static bool create_particles(pong_state *state)
{
    sdl3d_registered_actor *particles = actor_with_tags(state, "effect", "particles");
    if (particles == NULL)
    {
        SDL_SetError("missing effect particles entity");
        return false;
    }

    state->ambient_particles_entity = particles->name;
    sdl3d_particle_config config;
    if (!sdl3d_game_data_get_particle_emitter(state->data, state->ambient_particles_entity, &config))
    {
        SDL_SetError("missing particles.emitter component");
        return false;
    }

    sdl3d_game_data_get_particle_emitter_draw_emissive(state->data, state->ambient_particles_entity,
                                                       &state->ambient_particles_emissive);
    state->ambient_particles = sdl3d_create_particle_emitter(&config);
    return state->ambient_particles != NULL;
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

    sdl3d_game_data_get_app_control(state->data, &state->app);

    sync_state_from_data(state);
    if (state->app.start_signal_id >= 0)
    {
        sdl3d_signal_emit(ctx_bus(ctx), state->app.start_signal_id, NULL);
    }
    return true;
}

typedef struct font_capture
{
    const char *font_id;
} font_capture;

static bool capture_first_ui_font(void *userdata, const sdl3d_game_data_ui_text *text)
{
    font_capture *capture = (font_capture *)userdata;
    if (capture->font_id == NULL && text->font != NULL)
        capture->font_id = text->font;
    return capture->font_id == NULL;
}

static bool load_authored_font(pong_state *state)
{
    font_capture capture;
    SDL_zero(capture);
    sdl3d_game_data_for_each_ui_text(state->data, capture_first_ui_font, &capture);
    if (capture.font_id == NULL)
        return false;

    sdl3d_game_data_font_asset font;
    if (!sdl3d_game_data_get_font_asset(state->data, capture.font_id, &font))
        return false;
    if (font.builtin)
        return sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, font.builtin_id, font.size, &state->font);

    SDL_SetError("Pong demo currently supports built-in authored fonts only");
    return false;
}

static bool pong_init(sdl3d_game_context *ctx, void *userdata)
{
    pong_state *state = (pong_state *)userdata;
    SDL_zero(*state);

    if (!init_game_data(ctx, state))
    {
        return false;
    }
    if (state->app.quit_signal_id < 0 ||
        sdl3d_signal_connect(ctx_bus(ctx), state->app.quit_signal_id, on_quit_signal, ctx) == 0)
    {
        return false;
    }

    state->has_font = load_authored_font(state);
    if (!state->has_font)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong font load failed: %s", SDL_GetError());
    }

    if (!create_particles(state))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong particles disabled: %s", SDL_GetError());
    }

    sdl3d_game_data_render_settings render_settings;
    sdl3d_game_data_get_render_settings(state->data, &render_settings);
    sdl3d_set_lighting_enabled(ctx->renderer, render_settings.lighting_enabled);
    sdl3d_set_bloom_enabled(ctx->renderer, render_settings.bloom_enabled);
    sdl3d_set_ssao_enabled(ctx->renderer, render_settings.ssao_enabled);
    sdl3d_set_tonemap_mode(ctx->renderer, render_settings.tonemap);

    sdl3d_game_data_transition_desc transition;
    if (state->app.startup_transition != NULL &&
        sdl3d_game_data_get_transition(state->data, state->app.startup_transition, &transition))
    {
        sdl3d_transition_start(&state->transition, transition.type, transition.direction, transition.color,
                               transition.duration, transition.done_signal_id);
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
    state->border_flash = fade_down(state->border_flash, dt, presentation_float(state, "border_flash_decay", 2.8f));
    state->paddle_flash = fade_down(state->paddle_flash, dt, presentation_float(state, "paddle_flash_decay", 4.0f));
    sdl3d_registered_actor *presentation = presentation_actor(state);
    if (presentation != NULL)
    {
        sdl3d_properties_set_float(presentation->props, "border_flash", state->border_flash);
        sdl3d_properties_set_float(presentation->props, "paddle_flash", state->paddle_flash);
    }
    if (state->ambient_particles != NULL)
    {
        sdl3d_particle_emitter_update(state->ambient_particles, dt);
    }
}

static void consume_common_actions(sdl3d_game_context *ctx, pong_state *state)
{
    if (state->app.quit_action_id >= 0 && sdl3d_input_is_pressed(ctx_input(ctx), state->app.quit_action_id))
    {
        start_quit_fade(ctx, state);
    }
}

static void pong_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    pong_state *state = (pong_state *)userdata;

    consume_common_actions(ctx, state);
    if (state->app.pause_action_id >= 0 && sdl3d_input_is_pressed(ctx_input(ctx), state->app.pause_action_id) &&
        !state->match_finished)
    {
        ctx->paused = true;
        state->pause_flash = 0.0f;
    }
    update_visual_effects(state, dt);
    sdl3d_transition_update(&state->transition, ctx_bus(ctx), dt);

    if (!state->quit_pending)
    {
        sdl3d_game_data_update(state->data, dt);
    }

    sync_state_from_data(state);
}

static void pong_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    pong_state *state = (pong_state *)userdata;

    consume_common_actions(ctx, state);
    if (state->app.pause_action_id >= 0 && sdl3d_input_is_pressed(ctx_input(ctx), state->app.pause_action_id))
    {
        ctx->paused = false;
        return;
    }

    state->pause_flash += real_dt * presentation_float(state, "pause_flash_speed", 3.0f);
    while (state->pause_flash >= 1.0f)
    {
        state->pause_flash -= 1.0f;
    }
    update_visual_effects(state, real_dt);
    if (state->quit_pending)
    {
        sdl3d_transition_update(&state->transition, ctx_bus(ctx), real_dt);
    }
}

static void configure_lights(sdl3d_render_context *renderer, const pong_state *state)
{
    float ambient[3] = {0.015f, 0.018f, 0.026f};
    sdl3d_game_data_get_world_ambient_light(state->data, ambient);

    sdl3d_clear_lights(renderer);
    sdl3d_set_ambient_light(renderer, ambient[0], ambient[1], ambient[2]);

    const int light_count = sdl3d_game_data_world_light_count(state->data);
    for (int i = 0; i < light_count; ++i)
    {
        sdl3d_light light;
        if (sdl3d_game_data_get_world_light(state->data, i, &light))
            sdl3d_add_light(renderer, &light);
    }
}

typedef struct primitive_draw_context
{
    sdl3d_render_context *renderer;
    const pong_state *state;
} primitive_draw_context;

static void draw_emissive_render_primitive(sdl3d_render_context *renderer,
                                           const sdl3d_game_data_render_primitive *primitive, sdl3d_color color,
                                           sdl3d_vec3 size, float radius, float emissive_r, float emissive_g,
                                           float emissive_b)
{
    sdl3d_set_emissive(renderer, emissive_r, emissive_g, emissive_b);
    if (primitive->type == SDL3D_GAME_DATA_RENDER_CUBE)
        sdl3d_draw_cube(renderer, primitive->position, size, color);
    else if (primitive->type == SDL3D_GAME_DATA_RENDER_SPHERE)
        sdl3d_draw_sphere(renderer, primitive->position, radius, primitive->slices, primitive->rings, color);
    sdl3d_set_emissive(renderer, 0.0f, 0.0f, 0.0f);
}

static bool draw_authored_primitive(void *userdata, const sdl3d_game_data_render_primitive *primitive)
{
    primitive_draw_context *context = (primitive_draw_context *)userdata;
    (void)context->state;
    draw_emissive_render_primitive(context->renderer, primitive, primitive->color, primitive->size, primitive->radius,
                                   primitive->emissive_color.x, primitive->emissive_color.y,
                                   primitive->emissive_color.z);
    return true;
}

typedef struct ui_draw_context
{
    sdl3d_game_context *ctx;
    const pong_state *state;
} ui_draw_context;

static bool draw_authored_ui_text(void *userdata, const sdl3d_game_data_ui_text *text)
{
    ui_draw_context *draw = (ui_draw_context *)userdata;
    sdl3d_game_data_ui_metrics metrics;
    SDL_zero(metrics);
    metrics.paused = draw->ctx->paused;
    metrics.fps = draw->state->displayed_fps;
    metrics.frame = draw->state->rendered_frames;

    if (!sdl3d_game_data_ui_text_is_visible(draw->state->data, text, &metrics))
        return true;

    char content[128];
    if (!sdl3d_game_data_format_ui_text(draw->state->data, text, &metrics, content, sizeof(content)))
        return true;

    sdl3d_color color = text->color;
    if (text->pulse_alpha)
    {
        const float pulse = 0.5f + 0.5f * SDL_sinf(draw->state->pause_flash * SDL_PI_F * 2.0f);
        color.a = (Uint8)(120.0f + pulse * 135.0f);
    }

    const int width = sdl3d_get_render_context_width(draw->ctx->renderer);
    const int height = sdl3d_get_render_context_height(draw->ctx->renderer);
    float x = text->normalized ? text->x * (float)width : text->x;
    const float y = text->normalized ? text->y * (float)height : text->y;
    if (text->centered)
    {
        float text_w = 0.0f;
        float text_h = 0.0f;
        sdl3d_measure_text(&draw->state->font, content, &text_w, &text_h);
        x -= text_w * 0.5f;
    }

    sdl3d_draw_text_overlay(draw->ctx->renderer, &draw->state->font, content, x, y, color);
    return true;
}

static void draw_hud(sdl3d_game_context *ctx, const pong_state *state)
{
    if (!state->has_font)
    {
        return;
    }

    ui_draw_context draw = {ctx, state};
    sdl3d_game_data_for_each_ui_text(state->data, draw_authored_ui_text, &draw);
}

static sdl3d_camera3d make_active_camera(const pong_state *state)
{
    sdl3d_camera3d camera;
    const char *active_camera = sdl3d_game_data_active_camera(state->data);
    if (active_camera != NULL && sdl3d_game_data_get_camera(state->data, active_camera, &camera))
        return camera;

    SDL_zero(camera);
    camera.position = sdl3d_vec3_make(0.0f, 0.0f, 16.0f);
    camera.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    camera.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    camera.fovy = 11.4f;
    camera.projection = SDL3D_CAMERA_ORTHOGRAPHIC;
    return camera;
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

    sdl3d_game_data_render_settings render_settings;
    sdl3d_game_data_get_render_settings(state->data, &render_settings);
    sdl3d_clear_render_context(ctx->renderer, render_settings.clear_color);
    configure_lights(ctx->renderer, state);

    const sdl3d_camera3d camera = make_active_camera(state);

    if (sdl3d_begin_mode_3d(ctx->renderer, camera))
    {
        if (state->ambient_particles != NULL)
        {
            sdl3d_set_emissive(ctx->renderer, state->ambient_particles_emissive.x, state->ambient_particles_emissive.y,
                               state->ambient_particles_emissive.z);
            sdl3d_draw_particles(ctx->renderer, state->ambient_particles);
            sdl3d_set_emissive(ctx->renderer, 0.0f, 0.0f, 0.0f);
        }
        primitive_draw_context draw_context = {ctx->renderer, state};
        const sdl3d_game_data_render_eval render_eval = {.time = state->time};
        sdl3d_game_data_for_each_render_primitive_evaluated(state->data, &render_eval, draw_authored_primitive,
                                                            &draw_context);
        sdl3d_end_mode_3d(ctx->renderer);
    }

    draw_hud(ctx, state);
    sdl3d_transition_draw(&state->transition, ctx->renderer);
}

static void pong_shutdown(sdl3d_game_context *ctx, void *userdata)
{
    pong_state *state = (pong_state *)userdata;
    (void)ctx;

    sdl3d_destroy_particle_emitter(state->ambient_particles);
    state->ambient_particles = NULL;
    sdl3d_game_data_destroy(state->data);
    state->data = NULL;
    if (state->has_font)
    {
        sdl3d_free_font(&state->font);
        state->has_font = false;
    }
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

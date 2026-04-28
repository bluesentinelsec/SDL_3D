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

#define PONG_WINDOW_WIDTH 1280
#define PONG_WINDOW_HEIGHT 720
#define PONG_TICK_RATE (1.0f / 120.0f)

enum
{
    SIG_PONG_FADE_OUT_DONE = 9000
};

typedef enum pong_winner
{
    PONG_WINNER_NONE = 0,
    PONG_WINNER_PLAYER,
    PONG_WINNER_CPU,
} pong_winner;

typedef struct pong_actions
{
    int pause;
    int exit;
} pong_actions;

typedef struct pong_state
{
    sdl3d_game_data_runtime *data;
    pong_actions actions;
    sdl3d_font font;
    bool has_font;
    sdl3d_particle_emitter *ambient_particles;
    sdl3d_transition transition;
    float time;
    float pause_flash;
    float border_flash;
    float paddle_flash;
    float last_render_time;
    float fps_sample_time;
    float displayed_fps;
    int fps_sample_frames;
    Uint64 rendered_frames;
    int signal_game_start;
    int player_score;
    int cpu_score;
    float player_y;
    float cpu_y;
    sdl3d_vec2 ball_position;
    sdl3d_vec2 ball_velocity;
    float ball_z;
    float ball_radius;
    bool ball_active;
    bool match_finished;
    pong_winner winner;
    bool quit_pending;
} pong_state;

static sdl3d_input_manager *ctx_input(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_input(ctx->session);
}

static sdl3d_signal_bus *ctx_bus(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_signal_bus(ctx->session);
}

static float clampf(float value, float lo, float hi)
{
    if (value < lo)
    {
        return lo;
    }
    if (value > hi)
    {
        return hi;
    }
    return value;
}

static float fade_down(float value, float dt, float speed)
{
    value -= dt * speed;
    return value > 0.0f ? value : 0.0f;
}

static sdl3d_color color_lerp(sdl3d_color a, sdl3d_color b, float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return (sdl3d_color){
        (Uint8)((float)a.r + ((float)b.r - (float)a.r) * t),
        (Uint8)((float)a.g + ((float)b.g - (float)a.g) * t),
        (Uint8)((float)a.b + ((float)b.b - (float)a.b) * t),
        (Uint8)((float)a.a + ((float)b.a - (float)a.a) * t),
    };
}

static float vec2_length(sdl3d_vec2 value)
{
    return SDL_sqrtf(value.x * value.x + value.y * value.y);
}

static sdl3d_vec2 make_vec2(float x, float y)
{
    sdl3d_vec2 value;
    value.x = x;
    value.y = y;
    return value;
}

static sdl3d_vec2 ball_camera_forward(const pong_state *state)
{
    sdl3d_vec2 forward = state->ball_velocity;
    float len = vec2_length(forward);
    if (len < 0.001f)
    {
        forward.x = 1.0f;
        forward.y = 0.0f;
        return forward;
    }

    forward.x /= len;
    forward.y /= len;
    return forward;
}

static bool ball_camera_is_active(const pong_state *state)
{
    const char *camera = sdl3d_game_data_active_camera(state->data);
    return camera != NULL && SDL_strcmp(camera, "camera.ball_chase") == 0;
}

static void start_quit_fade(sdl3d_game_context *ctx, pong_state *state)
{
    if (state->quit_pending)
    {
        return;
    }

    state->quit_pending = true;
    sdl3d_transition_start(&state->transition, SDL3D_TRANSITION_FADE, SDL3D_TRANSITION_OUT, (sdl3d_color){0, 0, 0, 255},
                           0.45f, SIG_PONG_FADE_OUT_DONE);
    (void)ctx;
}

static void on_fade_out_done(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    sdl3d_game_context *ctx = (sdl3d_game_context *)userdata;
    (void)signal_id;
    (void)payload;
    ctx->quit_requested = true;
}

static void sync_state_from_data(pong_state *state)
{
    sdl3d_registered_actor *player = sdl3d_game_data_find_actor(state->data, "entity.paddle.player");
    sdl3d_registered_actor *cpu = sdl3d_game_data_find_actor(state->data, "entity.paddle.cpu");
    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(state->data, "entity.ball");
    sdl3d_registered_actor *player_score = sdl3d_game_data_find_actor(state->data, "entity.score.player");
    sdl3d_registered_actor *cpu_score = sdl3d_game_data_find_actor(state->data, "entity.score.cpu");
    sdl3d_registered_actor *match = sdl3d_game_data_find_actor(state->data, "entity.match");
    sdl3d_registered_actor *presentation = sdl3d_game_data_find_actor(state->data, "entity.presentation");

    if (player != NULL)
    {
        state->player_y = player->position.y;
    }
    if (cpu != NULL)
    {
        state->cpu_y = cpu->position.y;
    }
    if (ball != NULL)
    {
        const sdl3d_vec3 velocity =
            sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
        state->ball_position = make_vec2(ball->position.x, ball->position.y);
        state->ball_velocity = make_vec2(velocity.x, velocity.y);
        state->ball_z = ball->position.z;
        state->ball_radius = sdl3d_properties_get_float(ball->props, "radius", 0.22f);
        state->ball_active = sdl3d_properties_get_bool(ball->props, "active_motion", false);
    }
    if (player_score != NULL)
    {
        state->player_score = sdl3d_properties_get_int(player_score->props, "value", 0);
    }
    if (cpu_score != NULL)
    {
        state->cpu_score = sdl3d_properties_get_int(cpu_score->props, "value", 0);
    }
    if (match != NULL)
    {
        const char *winner = sdl3d_properties_get_string(match->props, "winner", "none");
        state->match_finished = sdl3d_properties_get_bool(match->props, "finished", false);
        if (winner != NULL && SDL_strcmp(winner, "player") == 0)
            state->winner = PONG_WINNER_PLAYER;
        else if (winner != NULL && SDL_strcmp(winner, "cpu") == 0)
            state->winner = PONG_WINNER_CPU;
        else
            state->winner = PONG_WINNER_NONE;
    }
    if (presentation != NULL)
    {
        state->border_flash = sdl3d_properties_get_float(presentation->props, "border_flash", 0.0f);
        state->paddle_flash = sdl3d_properties_get_float(presentation->props, "paddle_flash", 0.0f);
    }
}

static sdl3d_registered_actor *presentation_actor(const pong_state *state)
{
    return sdl3d_game_data_find_actor(state->data, "entity.presentation");
}

static float presentation_float(const pong_state *state, const char *key, float fallback)
{
    sdl3d_registered_actor *presentation = presentation_actor(state);
    return presentation != NULL ? sdl3d_properties_get_float(presentation->props, key, fallback) : fallback;
}

static const char *presentation_string(const pong_state *state, const char *key, const char *fallback)
{
    sdl3d_registered_actor *presentation = presentation_actor(state);
    const char *value =
        presentation != NULL ? sdl3d_properties_get_string(presentation->props, key, fallback) : fallback;
    return value != NULL ? value : fallback;
}

static bool create_particles(pong_state *state)
{
    sdl3d_particle_config config;
    if (!sdl3d_game_data_get_particle_emitter(state->data, "entity.effect.ambient_particles", &config))
    {
        SDL_SetError("missing entity.effect.ambient_particles particles.emitter component");
        return false;
    }

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
#if SDL3D_PONG_EMBEDDED_ASSETS
    bool assets_ready = sdl3d_asset_resolver_mount_memory_pack(assets, sdl3d_pong_assets, sdl3d_pong_assets_size,
                                                               "pong.embedded", error, (int)sizeof(error));
#elif defined(SDL3D_PONG_PACK_PATH)
    bool assets_ready = sdl3d_asset_resolver_mount_pack_file(assets, SDL3D_PONG_PACK_PATH, error, (int)sizeof(error));
#else
    bool assets_ready = sdl3d_asset_resolver_mount_directory(assets, SDL3D_PONG_DATA_DIR, error, (int)sizeof(error));
#endif
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

    state->actions.pause = sdl3d_game_data_find_action(state->data, "action.pause");
    state->actions.exit = sdl3d_game_data_find_action(state->data, "action.exit");
    state->signal_game_start = sdl3d_game_data_find_signal(state->data, "signal.game.start");

    sync_state_from_data(state);
    sdl3d_signal_emit(ctx_bus(ctx), state->signal_game_start, NULL);
    return true;
}

static bool pong_init(sdl3d_game_context *ctx, void *userdata)
{
    pong_state *state = (pong_state *)userdata;
    SDL_zero(*state);

    if (sdl3d_signal_connect(ctx_bus(ctx), SIG_PONG_FADE_OUT_DONE, on_fade_out_done, ctx) == 0)
    {
        return false;
    }

    if (!init_game_data(ctx, state))
    {
        return false;
    }

    state->has_font = sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, SDL3D_BUILTIN_FONT_INTER, 34.0f, &state->font);
    if (!state->has_font)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong font load failed: %s", SDL_GetError());
    }

    if (!create_particles(state))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Pong particles disabled: %s", SDL_GetError());
    }

    sdl3d_set_lighting_enabled(ctx->renderer, true);
    sdl3d_set_ambient_light(ctx->renderer, 0.015f, 0.018f, 0.025f);
    sdl3d_set_bloom_enabled(ctx->renderer, true);
    sdl3d_set_ssao_enabled(ctx->renderer, true);
    sdl3d_set_tonemap_mode(ctx->renderer, SDL3D_TONEMAP_ACES);

    sdl3d_transition_start(&state->transition, SDL3D_TRANSITION_FADE, SDL3D_TRANSITION_IN, (sdl3d_color){0, 0, 0, 255},
                           0.7f, -1);
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
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->actions.exit))
    {
        start_quit_fade(ctx, state);
    }
}

static void pong_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    pong_state *state = (pong_state *)userdata;

    consume_common_actions(ctx, state);
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->actions.pause) && !state->match_finished)
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
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->actions.pause))
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

static bool string_starts_with(const char *value, const char *prefix)
{
    return value != NULL && prefix != NULL && SDL_strncmp(value, prefix, SDL_strlen(prefix)) == 0;
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
    const pong_state *state = context->state;
    sdl3d_color color = primitive->color;
    sdl3d_vec3 size = primitive->size;
    float radius = primitive->radius;
    float emissive_r = 0.0f;
    float emissive_g = 0.0f;
    float emissive_b = 0.0f;

    if (string_starts_with(primitive->entity_name, "entity.field.border."))
    {
        const float flash = clampf(state->border_flash, 0.0f, 1.0f);
        color = color_lerp(color, (sdl3d_color){210, 235, 255, 255}, flash);
        if (size.x < size.y)
            size.x += 0.06f * flash;
        else
            size.y += 0.06f * flash;
        emissive_r = flash * 0.55f;
        emissive_g = flash * 0.70f;
        emissive_b = flash;
    }
    else if (string_starts_with(primitive->entity_name, "entity.field.center_ticks"))
    {
        emissive_r = 0.10f;
        emissive_g = 0.12f;
        emissive_b = 0.16f;
    }
    else if (string_starts_with(primitive->entity_name, "entity.paddle."))
    {
        const float flash = clampf(state->paddle_flash, 0.0f, 1.0f);
        color = color_lerp(color, (sdl3d_color){255, 255, 255, 255}, flash);
        emissive_r = flash * 0.35f;
        emissive_g = flash * 0.42f;
        emissive_b = flash * 0.50f;
    }
    else if (SDL_strcmp(primitive->entity_name, "entity.ball") == 0)
    {
        const float pulse = 0.5f + 0.5f * SDL_sinf(state->time * 9.0f);
        color = color_lerp(color, (sdl3d_color){255, 245, 156, 255}, pulse);
        emissive_r = 0.75f + pulse * 0.65f;
        emissive_g = 0.48f + pulse * 0.30f;
        emissive_b = 0.08f;
    }
    else if (primitive->emissive)
    {
        emissive_r = 0.2f;
        emissive_g = 0.2f;
        emissive_b = 0.2f;
    }

    draw_emissive_render_primitive(context->renderer, primitive, color, size, radius, emissive_r, emissive_g,
                                   emissive_b);
    return true;
}

static void draw_centered_text(sdl3d_render_context *renderer, const sdl3d_font *font, const char *text, float y,
                               sdl3d_color color)
{
    float text_w = 0.0f;
    float text_h = 0.0f;
    sdl3d_measure_text(font, text, &text_w, &text_h);
    const int width = sdl3d_get_render_context_width(renderer);
    sdl3d_draw_text_overlay(renderer, font, text, ((float)width - text_w) * 0.5f, y, color);
}

static void draw_hud(sdl3d_game_context *ctx, const pong_state *state)
{
    if (!state->has_font)
    {
        return;
    }

    const int width = sdl3d_get_render_context_width(ctx->renderer);
    const int height = sdl3d_get_render_context_height(ctx->renderer);
    sdl3d_draw_textf_overlay(ctx->renderer, &state->font, 18.0f, 16.0f, (sdl3d_color){170, 190, 220, 230},
                             "FPS %.0f  Frame %" SDL_PRIu64, state->displayed_fps, state->rendered_frames);
    if (ball_camera_is_active(state))
    {
        sdl3d_draw_text_overlay(ctx->renderer, &state->font,
                                presentation_string(state, "ball_camera_label", "BALL CAM"), 18.0f, 56.0f,
                                (sdl3d_color){255, 222, 140, 235});
    }
    sdl3d_draw_textf_overlay(ctx->renderer, &state->font, (float)width * 0.5f - 94.0f, 22.0f,
                             (sdl3d_color){235, 242, 255, 255}, "%02d   %02d", state->player_score, state->cpu_score);

    if (state->match_finished)
    {
        const char *winner = state->winner == PONG_WINNER_PLAYER
                                 ? presentation_string(state, "player_win_text", "You win!")
                                 : presentation_string(state, "cpu_win_text", "You lose!");
        draw_centered_text(ctx->renderer, &state->font, winner, (float)height * 0.40f,
                           (sdl3d_color){255, 255, 255, 255});
        draw_centered_text(ctx->renderer, &state->font,
                           presentation_string(state, "restart_text", "Press enter to play again"),
                           (float)height * 0.49f, (sdl3d_color){200, 220, 255, 235});
    }
    else if (ctx->paused)
    {
        const float pulse = 0.5f + 0.5f * SDL_sinf(state->pause_flash * SDL_PI_F * 2.0f);
        const Uint8 alpha = (Uint8)(120.0f + pulse * 135.0f);
        draw_centered_text(ctx->renderer, &state->font, presentation_string(state, "pause_text", "PAUSED"),
                           (float)height * 0.44f, (sdl3d_color){245, 248, 255, alpha});
    }
    else if (!state->ball_active)
    {
        draw_centered_text(ctx->renderer, &state->font, presentation_string(state, "ready_text", "Get ready"),
                           (float)height * 0.44f, (sdl3d_color){210, 225, 255, 190});
    }
}

static float camera_float_or(const pong_state *state, const char *camera_name, const char *property_name,
                             float fallback)
{
    float value = 0.0f;
    return sdl3d_game_data_get_camera_float(state->data, camera_name, property_name, &value) ? value : fallback;
}

static sdl3d_camera3d make_ball_camera(const pong_state *state)
{
    const char *camera_name = "camera.ball_chase";
    const sdl3d_vec2 forward = ball_camera_forward(state);
    const float ball_z_offset = camera_float_or(state, camera_name, "ball_z_offset", state->ball_radius);
    const sdl3d_vec3 ball =
        sdl3d_vec3_make(state->ball_position.x, state->ball_position.y, state->ball_z + ball_z_offset);
    const float chase_distance = camera_float_or(state, camera_name, "chase_distance", 2.6f);
    const float camera_height = camera_float_or(state, camera_name, "height", 1.35f);
    const float lookahead = camera_float_or(state, camera_name, "lookahead", 4.4f);
    const float target_z_offset = camera_float_or(state, camera_name, "target_z_offset", -0.05f);

    sdl3d_camera3d camera;
    SDL_zero(camera);
    camera.position = sdl3d_vec3_make(ball.x - forward.x * chase_distance, ball.y - forward.y * chase_distance,
                                      ball.z + camera_height);
    camera.target =
        sdl3d_vec3_make(ball.x + forward.x * lookahead, ball.y + forward.y * lookahead, ball.z + target_z_offset);
    camera.up = sdl3d_vec3_make(0.0f, 0.0f, 1.0f);
    camera.fovy = camera_float_or(state, camera_name, "fovy", 68.0f);
    camera.projection = SDL3D_CAMERA_PERSPECTIVE;
    return camera;
}

static sdl3d_camera3d make_active_camera(const pong_state *state)
{
    if (ball_camera_is_active(state))
        return make_ball_camera(state);

    sdl3d_camera3d camera;
    if (sdl3d_game_data_get_camera(state->data, "camera.overhead", &camera))
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

    sdl3d_clear_render_context(ctx->renderer, (sdl3d_color){3, 4, 8, 255});
    configure_lights(ctx->renderer, state);

    const sdl3d_camera3d camera = make_active_camera(state);

    if (sdl3d_begin_mode_3d(ctx->renderer, camera))
    {
        if (state->ambient_particles != NULL)
        {
            sdl3d_set_emissive(ctx->renderer, 0.8f, 0.9f, 1.0f);
            sdl3d_draw_particles(ctx->renderer, state->ambient_particles);
            sdl3d_set_emissive(ctx->renderer, 0.0f, 0.0f, 0.0f);
        }
        primitive_draw_context draw_context = {ctx->renderer, state};
        sdl3d_game_data_for_each_render_primitive(state->data, draw_authored_primitive, &draw_context);
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
    config.title = "SDL3D Pong";
    config.width = PONG_WINDOW_WIDTH;
    config.height = PONG_WINDOW_HEIGHT;
    config.backend = SDL3D_BACKEND_AUTO;
    config.tick_rate = PONG_TICK_RATE;
    config.max_ticks_per_frame = 12;

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

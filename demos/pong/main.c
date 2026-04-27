#include "pong_rules.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <stdbool.h>

#include "sdl3d/actor_registry.h"
#include "sdl3d/camera.h"
#include "sdl3d/drawing3d.h"
#include "sdl3d/effects.h"
#include "sdl3d/font.h"
#include "sdl3d/game.h"
#include "sdl3d/input.h"
#include "sdl3d/lighting.h"
#include "sdl3d/properties.h"
#include "sdl3d/render_context.h"
#include "sdl3d/shapes.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/timer_pool.h"
#include "sdl3d/transition.h"

#define PONG_WINDOW_WIDTH 1280
#define PONG_WINDOW_HEIGHT 720
#define PONG_TICK_RATE (1.0f / 120.0f)
#define PONG_FIELD_DEPTH -0.45f
#define PONG_PADDLE_DEPTH 0.28f
#define PONG_BALL_Z 0.12f
#define PONG_SERVE_DELAY 1.0f

enum
{
    SIG_PONG_SERVE = 9000,
    SIG_PONG_FADE_OUT_DONE
};

typedef struct pong_actor_ids
{
    int player_paddle;
    int cpu_paddle;
    int ball;
    int playfield;
} pong_actor_ids;

typedef struct pong_actions
{
    int up;
    int down;
    int pause;
    int exit;
    int restart;
    int toggle_ball_camera;
} pong_actions;

typedef struct pong_state
{
    pong_rules_state rules;
    pong_actions actions;
    pong_actor_ids actors;
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
    int serve_timer_id;
    bool quit_pending;
    bool ball_camera_enabled;
} pong_state;

static sdl3d_input_manager *ctx_input(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_input(ctx->session);
}

static sdl3d_signal_bus *ctx_bus(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_signal_bus(ctx->session);
}

static sdl3d_timer_pool *ctx_timers(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_timer_pool(ctx->session);
}

static sdl3d_actor_registry *ctx_registry(const sdl3d_game_context *ctx)
{
    return sdl3d_game_session_get_registry(ctx->session);
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

static sdl3d_vec2 ball_camera_forward(const pong_rules_state *rules)
{
    sdl3d_vec2 forward = rules->ball_velocity;
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

static void schedule_serve(sdl3d_game_context *ctx, pong_state *state)
{
    if (state->rules.match_finished)
    {
        return;
    }

    if (state->serve_timer_id != 0)
    {
        sdl3d_timer_cancel(ctx_timers(ctx), state->serve_timer_id);
    }
    state->serve_timer_id = sdl3d_timer_start(ctx_timers(ctx), PONG_SERVE_DELAY, SIG_PONG_SERVE, false, 0.0f);
}

static void start_quit_fade(sdl3d_game_context *ctx, pong_state *state)
{
    if (state->quit_pending)
    {
        return;
    }

    state->quit_pending = true;
    sdl3d_transition_start(&state->transition, SDL3D_TRANSITION_FADE, SDL3D_TRANSITION_OUT,
                           (sdl3d_color){0, 0, 0, 255}, 0.45f, SIG_PONG_FADE_OUT_DONE);
    (void)ctx;
}

static void on_serve(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    pong_state *state = (pong_state *)userdata;
    (void)signal_id;
    (void)payload;

    state->serve_timer_id = 0;
    pong_rules_serve_random(&state->rules, 0);
}

static void on_fade_out_done(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    sdl3d_game_context *ctx = (sdl3d_game_context *)userdata;
    (void)signal_id;
    (void)payload;
    ctx->quit_requested = true;
}

static int bind_key_action(sdl3d_input_manager *input, const char *name, SDL_Scancode first, SDL_Scancode second)
{
    const int action = sdl3d_input_register_action(input, name);
    sdl3d_input_bind_key(input, action, first);
    if (second != SDL_SCANCODE_UNKNOWN)
    {
        sdl3d_input_bind_key(input, action, second);
    }
    return action;
}

static void bind_actions(sdl3d_input_manager *input, pong_state *state)
{
    state->actions.up = bind_key_action(input, "pong_up", SDL_SCANCODE_W, SDL_SCANCODE_UP);
    sdl3d_input_bind_gamepad_axis(input, state->actions.up, SDL_GAMEPAD_AXIS_LEFTY, -1.0f);

    state->actions.down = bind_key_action(input, "pong_down", SDL_SCANCODE_S, SDL_SCANCODE_DOWN);
    sdl3d_input_bind_gamepad_axis(input, state->actions.down, SDL_GAMEPAD_AXIS_LEFTY, 1.0f);

    state->actions.pause = bind_key_action(input, "pong_pause", SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER);
    sdl3d_input_bind_key(input, state->actions.pause, SDL_SCANCODE_P);
    sdl3d_input_bind_gamepad_button(input, state->actions.pause, SDL_GAMEPAD_BUTTON_START);

    state->actions.exit = bind_key_action(input, "pong_exit", SDL_SCANCODE_ESCAPE, SDL_SCANCODE_UNKNOWN);
    sdl3d_input_bind_gamepad_button(input, state->actions.exit, SDL_GAMEPAD_BUTTON_BACK);

    state->actions.restart = sdl3d_input_register_action(input, "pong_restart");
    sdl3d_input_bind_key(input, state->actions.restart, SDL_SCANCODE_RETURN);
    sdl3d_input_bind_key(input, state->actions.restart, SDL_SCANCODE_KP_ENTER);
    sdl3d_input_bind_gamepad_button(input, state->actions.restart, SDL_GAMEPAD_BUTTON_SOUTH);

    state->actions.toggle_ball_camera =
        bind_key_action(input, "pong_toggle_ball_camera", SDL_SCANCODE_B, SDL_SCANCODE_UNKNOWN);
}

static bool register_actor(sdl3d_actor_registry *registry, const char *name, int *out_id, const char *classname,
                           sdl3d_vec3 position)
{
    sdl3d_registered_actor *actor = sdl3d_actor_registry_add(registry, name);
    if (actor == NULL)
    {
        return false;
    }

    actor->position = position;
    sdl3d_properties_set_string(actor->props, "classname", classname);
    sdl3d_properties_set_vec3(actor->props, "origin", position);
    *out_id = actor->id;
    return true;
}

static bool create_actors(sdl3d_game_context *ctx, pong_state *state)
{
    sdl3d_actor_registry *registry = ctx_registry(ctx);
    const pong_rules_config *config = &state->rules.config;

    return register_actor(registry, "player_paddle", &state->actors.player_paddle, "pong_paddle",
                          sdl3d_vec3_make(-config->paddle_x, 0.0f, 0.0f)) &&
           register_actor(registry, "cpu_paddle", &state->actors.cpu_paddle, "pong_paddle",
                          sdl3d_vec3_make(config->paddle_x, 0.0f, 0.0f)) &&
           register_actor(registry, "ball", &state->actors.ball, "pong_ball",
                          sdl3d_vec3_make(0.0f, 0.0f, PONG_BALL_Z)) &&
           register_actor(registry, "playfield", &state->actors.playfield, "pong_playfield",
                          sdl3d_vec3_make(0.0f, 0.0f, PONG_FIELD_DEPTH));
}

static void sync_actor_positions(sdl3d_game_context *ctx, const pong_state *state)
{
    sdl3d_actor_registry *registry = ctx_registry(ctx);
    sdl3d_registered_actor *actor = sdl3d_actor_registry_get(registry, state->actors.player_paddle);
    if (actor != NULL)
    {
        actor->position = sdl3d_vec3_make(-state->rules.config.paddle_x, state->rules.player_y, 0.0f);
        sdl3d_properties_set_vec3(actor->props, "origin", actor->position);
    }

    actor = sdl3d_actor_registry_get(registry, state->actors.cpu_paddle);
    if (actor != NULL)
    {
        actor->position = sdl3d_vec3_make(state->rules.config.paddle_x, state->rules.cpu_y, 0.0f);
        sdl3d_properties_set_vec3(actor->props, "origin", actor->position);
    }

    actor = sdl3d_actor_registry_get(registry, state->actors.ball);
    if (actor != NULL)
    {
        actor->position = sdl3d_vec3_make(state->rules.ball_position.x, state->rules.ball_position.y, PONG_BALL_Z);
        sdl3d_properties_set_vec3(actor->props, "origin", actor->position);
    }
}

static bool create_particles(pong_state *state)
{
    sdl3d_particle_config config;
    SDL_zero(config);
    config.position = sdl3d_vec3_make(0.0f, 0.0f, 0.35f);
    config.direction = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    config.spread = 1.1f;
    config.speed_min = 0.08f;
    config.speed_max = 0.45f;
    config.lifetime_min = 2.5f;
    config.lifetime_max = 5.0f;
    config.size_start = 0.035f;
    config.size_end = 0.012f;
    config.color_start = (sdl3d_color){220, 235, 255, 105};
    config.color_end = (sdl3d_color){255, 255, 255, 0};
    config.gravity = 0.0f;
    config.max_particles = 360;
    config.emit_rate = 95.0f;
    config.shape = SDL3D_PARTICLE_EMITTER_BOX;
    config.extents = sdl3d_vec3_make(8.8f, 4.8f, 0.18f);
    config.emissive_intensity = 1.35f;
    config.camera_facing = true;
    config.depth_test = true;
    config.random_seed = 0x1234567u;

    state->ambient_particles = sdl3d_create_particle_emitter(&config);
    return state->ambient_particles != NULL;
}

static bool pong_init(sdl3d_game_context *ctx, void *userdata)
{
    pong_state *state = (pong_state *)userdata;
    SDL_zero(*state);

    pong_rules_init(&state->rules, NULL, SDL_GetTicks());
    bind_actions(ctx_input(ctx), state);

    if (sdl3d_signal_connect(ctx_bus(ctx), SIG_PONG_SERVE, on_serve, state) == 0 ||
        sdl3d_signal_connect(ctx_bus(ctx), SIG_PONG_FADE_OUT_DONE, on_fade_out_done, ctx) == 0)
    {
        return false;
    }

    if (!create_actors(ctx, state))
    {
        return false;
    }
    sync_actor_positions(ctx, state);

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

    sdl3d_transition_start(&state->transition, SDL3D_TRANSITION_FADE, SDL3D_TRANSITION_IN,
                           (sdl3d_color){0, 0, 0, 255}, 0.7f, -1);
    schedule_serve(ctx, state);
    return true;
}

static bool pong_handle_event(sdl3d_game_context *ctx, void *userdata, const SDL_Event *event)
{
    (void)ctx;
    (void)userdata;
    (void)event;
    return true;
}

static void reset_match(sdl3d_game_context *ctx, pong_state *state)
{
    pong_rules_reset_match(&state->rules);
    state->border_flash = 0.0f;
    state->paddle_flash = 0.0f;
    schedule_serve(ctx, state);
}

static void update_visual_effects(pong_state *state, float dt)
{
    state->time += dt;
    state->border_flash = fade_down(state->border_flash, dt, 2.8f);
    state->paddle_flash = fade_down(state->paddle_flash, dt, 4.0f);
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
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->actions.toggle_ball_camera))
    {
        state->ball_camera_enabled = !state->ball_camera_enabled;
    }
}

static void pong_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    pong_state *state = (pong_state *)userdata;

    consume_common_actions(ctx, state);
    if (sdl3d_input_is_pressed(ctx_input(ctx), state->actions.pause) && !state->rules.match_finished)
    {
        ctx->paused = true;
        state->pause_flash = 0.0f;
    }
    if (state->rules.match_finished && sdl3d_input_is_pressed(ctx_input(ctx), state->actions.restart))
    {
        reset_match(ctx, state);
    }

    update_visual_effects(state, dt);
    sdl3d_transition_update(&state->transition, ctx_bus(ctx), dt);

    if (!state->rules.match_finished && !state->quit_pending)
    {
        const sdl3d_vec2 axis =
            sdl3d_input_get_axis_pair(ctx_input(ctx), state->actions.down, state->actions.up, -1, -1);
        pong_rules_input input = {.player_axis = axis.x};
        const pong_rules_step_result result = pong_rules_step(&state->rules, &input, dt);
        if ((result.events & PONG_EVENT_WALL_BOUNCE) != 0)
        {
            state->border_flash = 1.0f;
        }
        if ((result.events & PONG_EVENT_PADDLE_BOUNCE) != 0)
        {
            state->border_flash = 1.0f;
            state->paddle_flash = 1.0f;
        }
        if ((result.events & (PONG_EVENT_PLAYER_SCORED | PONG_EVENT_CPU_SCORED)) != 0 &&
            (result.events & PONG_EVENT_MATCH_FINISHED) == 0)
        {
            schedule_serve(ctx, state);
        }
    }

    sync_actor_positions(ctx, state);
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

    state->pause_flash += real_dt * 3.0f;
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

static void add_point_light(sdl3d_render_context *renderer, sdl3d_vec3 position, float r, float g, float b,
                            float intensity, float range)
{
    sdl3d_light light;
    SDL_zero(light);
    light.type = SDL3D_LIGHT_POINT;
    light.position = position;
    light.color[0] = r;
    light.color[1] = g;
    light.color[2] = b;
    light.intensity = intensity;
    light.range = range;
    sdl3d_add_light(renderer, &light);
}

static void configure_lights(sdl3d_render_context *renderer, const pong_state *state)
{
    const float pulse = 0.5f + 0.5f * SDL_sinf(state->time * 7.0f);
    sdl3d_clear_lights(renderer);
    sdl3d_set_ambient_light(renderer, 0.015f, 0.018f, 0.026f);
    add_point_light(renderer, sdl3d_vec3_make(-7.0f, 4.4f, 3.2f), 1.0f, 0.10f, 0.08f, 2.8f, 12.0f);
    add_point_light(renderer, sdl3d_vec3_make(7.0f, 4.4f, 3.2f), 0.08f, 1.0f, 0.18f, 2.7f, 12.0f);
    add_point_light(renderer, sdl3d_vec3_make(0.0f, -4.2f, 3.5f), 0.14f, 0.34f, 1.0f, 2.9f, 12.0f);
    add_point_light(renderer,
                    sdl3d_vec3_make(state->rules.ball_position.x, state->rules.ball_position.y, PONG_BALL_Z + 1.2f),
                    1.0f, 0.72f + 0.28f * pulse, 0.22f, 2.1f + pulse * 2.0f, 5.0f);
}

static void draw_playfield(sdl3d_render_context *renderer, const pong_state *state)
{
    const pong_rules_config *config = &state->rules.config;
    const float flash = clampf(state->border_flash, 0.0f, 1.0f);
    const sdl3d_color base = (sdl3d_color){42, 48, 62, 255};
    const sdl3d_color pulse = (sdl3d_color){210, 235, 255, 255};
    const sdl3d_color border = color_lerp((sdl3d_color){62, 72, 92, 255}, pulse, flash);
    const float border_size = 0.12f + 0.06f * flash;

    sdl3d_set_emissive(renderer, 0.0f, 0.0f, 0.0f);
    sdl3d_draw_cube(renderer, sdl3d_vec3_make(0.0f, 0.0f, PONG_FIELD_DEPTH),
                    sdl3d_vec3_make(config->half_width * 2.0f + 0.8f, config->half_height * 2.0f + 0.8f, 0.10f),
                    base);
    sdl3d_set_emissive(renderer, flash * 0.55f, flash * 0.7f, flash);
    sdl3d_draw_cube(renderer, sdl3d_vec3_make(0.0f, config->half_height + border_size, 0.0f),
                    sdl3d_vec3_make(config->half_width * 2.0f + 0.35f, border_size, 0.18f), border);
    sdl3d_draw_cube(renderer, sdl3d_vec3_make(0.0f, -config->half_height - border_size, 0.0f),
                    sdl3d_vec3_make(config->half_width * 2.0f + 0.35f, border_size, 0.18f), border);
    sdl3d_draw_cube(renderer, sdl3d_vec3_make(-config->half_width - border_size, 0.0f, 0.0f),
                    sdl3d_vec3_make(border_size, config->half_height * 2.0f + 0.35f, 0.18f), border);
    sdl3d_draw_cube(renderer, sdl3d_vec3_make(config->half_width + border_size, 0.0f, 0.0f),
                    sdl3d_vec3_make(border_size, config->half_height * 2.0f + 0.35f, 0.18f), border);

    sdl3d_set_emissive(renderer, 0.10f, 0.12f, 0.16f);
    for (int i = -4; i <= 4; ++i)
    {
        sdl3d_draw_cube(renderer, sdl3d_vec3_make(0.0f, (float)i, 0.02f), sdl3d_vec3_make(0.055f, 0.42f, 0.08f),
                        (sdl3d_color){115, 132, 162, 180});
    }
    sdl3d_set_emissive(renderer, 0.0f, 0.0f, 0.0f);
}

static void draw_game_objects(sdl3d_render_context *renderer, const pong_state *state)
{
    const pong_rules_config *config = &state->rules.config;
    const float paddle_flash = clampf(state->paddle_flash, 0.0f, 1.0f);
    const sdl3d_color player_color =
        color_lerp((sdl3d_color){205, 230, 255, 255}, (sdl3d_color){255, 255, 255, 255}, paddle_flash);
    const sdl3d_color cpu_color =
        color_lerp((sdl3d_color){195, 255, 212, 255}, (sdl3d_color){255, 255, 255, 255}, paddle_flash);
    const float ball_pulse = 0.5f + 0.5f * SDL_sinf(state->time * 9.0f);
    const sdl3d_color ball_color =
        color_lerp((sdl3d_color){255, 184, 82, 255}, (sdl3d_color){255, 245, 156, 255}, ball_pulse);

    sdl3d_set_emissive(renderer, paddle_flash * 0.35f, paddle_flash * 0.42f, paddle_flash * 0.50f);
    sdl3d_draw_cube(renderer, sdl3d_vec3_make(-config->paddle_x, state->rules.player_y, 0.0f),
                    sdl3d_vec3_make(config->paddle_half_width * 2.0f, config->paddle_half_height * 2.0f,
                                    PONG_PADDLE_DEPTH),
                    player_color);
    sdl3d_draw_cube(renderer, sdl3d_vec3_make(config->paddle_x, state->rules.cpu_y, 0.0f),
                    sdl3d_vec3_make(config->paddle_half_width * 2.0f, config->paddle_half_height * 2.0f,
                                    PONG_PADDLE_DEPTH),
                    cpu_color);

    sdl3d_set_emissive(renderer, 0.75f + ball_pulse * 0.65f, 0.48f + ball_pulse * 0.30f, 0.08f);
    sdl3d_draw_sphere(renderer,
                      sdl3d_vec3_make(state->rules.ball_position.x, state->rules.ball_position.y, PONG_BALL_Z),
                      config->ball_radius, 12, 18, ball_color);
    sdl3d_set_emissive(renderer, 0.0f, 0.0f, 0.0f);
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
    if (state->ball_camera_enabled)
    {
        sdl3d_draw_text_overlay(ctx->renderer, &state->font, "BALL CAM", 18.0f, 56.0f,
                                (sdl3d_color){255, 222, 140, 235});
    }
    sdl3d_draw_textf_overlay(ctx->renderer, &state->font, (float)width * 0.5f - 94.0f, 22.0f,
                             (sdl3d_color){235, 242, 255, 255}, "%02d   %02d", state->rules.player_score,
                             state->rules.cpu_score);

    if (state->rules.match_finished)
    {
        const char *winner = state->rules.winner == PONG_WINNER_PLAYER ? "You win!" : "You lose!";
        draw_centered_text(ctx->renderer, &state->font, winner, (float)height * 0.40f,
                           (sdl3d_color){255, 255, 255, 255});
        draw_centered_text(ctx->renderer, &state->font, "Press enter to play again", (float)height * 0.49f,
                           (sdl3d_color){200, 220, 255, 235});
    }
    else if (ctx->paused)
    {
        const float pulse = 0.5f + 0.5f * SDL_sinf(state->pause_flash * SDL_PI_F * 2.0f);
        const Uint8 alpha = (Uint8)(120.0f + pulse * 135.0f);
        draw_centered_text(ctx->renderer, &state->font, "PAUSED", (float)height * 0.44f,
                           (sdl3d_color){245, 248, 255, alpha});
    }
    else if (pong_rules_is_waiting_for_serve(&state->rules))
    {
        draw_centered_text(ctx->renderer, &state->font, "Get ready", (float)height * 0.44f,
                           (sdl3d_color){210, 225, 255, 190});
    }
}

static sdl3d_camera3d make_overhead_camera(void)
{
    sdl3d_camera3d camera;
    SDL_zero(camera);
    camera.position = sdl3d_vec3_make(0.0f, 0.0f, 16.0f);
    camera.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    camera.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    camera.fovy = 11.4f;
    camera.projection = SDL3D_CAMERA_ORTHOGRAPHIC;
    return camera;
}

static sdl3d_camera3d make_ball_camera(const pong_state *state)
{
    const sdl3d_vec2 forward = ball_camera_forward(&state->rules);
    const sdl3d_vec3 ball =
        sdl3d_vec3_make(state->rules.ball_position.x, state->rules.ball_position.y, PONG_BALL_Z + 0.22f);
    const float chase_distance = 2.6f;
    const float camera_height = 1.35f;
    const float lookahead = 4.4f;

    sdl3d_camera3d camera;
    SDL_zero(camera);
    camera.position = sdl3d_vec3_make(ball.x - forward.x * chase_distance, ball.y - forward.y * chase_distance,
                                      ball.z + camera_height);
    camera.target =
        sdl3d_vec3_make(ball.x + forward.x * lookahead, ball.y + forward.y * lookahead, ball.z - 0.05f);
    camera.up = sdl3d_vec3_make(0.0f, 0.0f, 1.0f);
    camera.fovy = 68.0f;
    camera.projection = SDL3D_CAMERA_PERSPECTIVE;
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

    const sdl3d_camera3d camera = state->ball_camera_enabled ? make_ball_camera(state) : make_overhead_camera();

    if (sdl3d_begin_mode_3d(ctx->renderer, camera))
    {
        draw_playfield(ctx->renderer, state);
        if (state->ambient_particles != NULL)
        {
            sdl3d_set_emissive(ctx->renderer, 0.8f, 0.9f, 1.0f);
            sdl3d_draw_particles(ctx->renderer, state->ambient_particles);
            sdl3d_set_emissive(ctx->renderer, 0.0f, 0.0f, 0.0f);
        }
        draw_game_objects(ctx->renderer, state);
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

#include "pong_rules.h"

#include <SDL3/SDL_stdinc.h>

#define PONG_DEFAULT_SEED 0xC0FFEEu
#define PONG_MAX_STEP_DT 0.05f
#define PONG_MAX_SERVE_ANGLE 0.52f
#define PONG_MAX_BOUNCE_ANGLE 1.05f

static float pong_clampf(float value, float lo, float hi)
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

static float pong_absf(float value)
{
    return value < 0.0f ? -value : value;
}

static float pong_signf(float value)
{
    return value < 0.0f ? -1.0f : 1.0f;
}

static float pong_vec2_length(sdl3d_vec2 value)
{
    return SDL_sqrtf(value.x * value.x + value.y * value.y);
}

static sdl3d_vec2 pong_vec2_make(float x, float y)
{
    sdl3d_vec2 value;
    value.x = x;
    value.y = y;
    return value;
}

static uint32_t pong_random_u32(pong_rules_state *state)
{
    uint32_t x = state->rng_state != 0u ? state->rng_state : PONG_DEFAULT_SEED;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state->rng_state = x != 0u ? x : PONG_DEFAULT_SEED;
    return state->rng_state;
}

static float pong_random_01(pong_rules_state *state)
{
    return (float)(pong_random_u32(state) >> 8) * (1.0f / 16777216.0f);
}

static void pong_move_toward(float *value, float target, float max_delta)
{
    const float delta = target - *value;
    if (pong_absf(delta) <= max_delta)
    {
        *value = target;
        return;
    }
    *value += pong_signf(delta) * max_delta;
}

static float pong_clamp_paddle_y(const pong_rules_state *state, float y)
{
    const float limit = state->config.half_height - state->config.paddle_half_height;
    return pong_clampf(y, -limit, limit);
}

static void pong_set_ball_speed(pong_rules_state *state, float dir_x, float normalized_impact, float speed)
{
    const float impact = pong_clampf(normalized_impact, -1.0f, 1.0f);
    const float angle = impact * PONG_MAX_BOUNCE_ANGLE;
    const float x = SDL_cosf(angle) * (dir_x < 0.0f ? -1.0f : 1.0f);
    const float y = SDL_sinf(angle);
    state->ball_velocity = pong_vec2_make(x * speed, y * speed);
}

static bool pong_overlap_paddle(const pong_rules_state *state, float paddle_x, float paddle_y)
{
    const float ball_min_x = state->ball_position.x - state->config.ball_radius;
    const float ball_max_x = state->ball_position.x + state->config.ball_radius;
    const float paddle_min_x = paddle_x - state->config.paddle_half_width;
    const float paddle_max_x = paddle_x + state->config.paddle_half_width;
    const float ball_min_y = state->ball_position.y - state->config.ball_radius;
    const float ball_max_y = state->ball_position.y + state->config.ball_radius;
    const float paddle_min_y = paddle_y - state->config.paddle_half_height;
    const float paddle_max_y = paddle_y + state->config.paddle_half_height;

    return ball_max_x >= paddle_min_x && ball_min_x <= paddle_max_x && ball_max_y >= paddle_min_y &&
           ball_min_y <= paddle_max_y;
}

static void pong_score_for(pong_rules_state *state, bool player_scored, pong_rules_step_result *result)
{
    if (player_scored)
    {
        ++state->player_score;
        result->events |= PONG_EVENT_PLAYER_SCORED;
    }
    else
    {
        ++state->cpu_score;
        result->events |= PONG_EVENT_CPU_SCORED;
    }

    if (state->player_score >= state->config.winning_score || state->cpu_score >= state->config.winning_score)
    {
        state->match_finished = true;
        state->winner = state->player_score >= state->config.winning_score ? PONG_WINNER_PLAYER : PONG_WINNER_CPU;
        result->events |= PONG_EVENT_MATCH_FINISHED;
        result->winner = state->winner;
    }

    pong_rules_reset_round(state);
}

pong_rules_config pong_rules_default_config(void)
{
    pong_rules_config config;
    SDL_zero(config);
    config.half_width = 9.0f;
    config.half_height = 5.0f;
    config.paddle_x = 8.0f;
    config.paddle_half_width = 0.18f;
    config.paddle_half_height = 0.95f;
    config.paddle_speed = 7.5f;
    config.cpu_speed = 5.5f;
    config.ball_radius = 0.22f;
    config.ball_speed = 5.6f;
    config.ball_speedup_per_hit = 0.28f;
    config.max_ball_speed = 10.0f;
    config.winning_score = 10;
    return config;
}

void pong_rules_init(pong_rules_state *state, const pong_rules_config *config, uint32_t seed)
{
    if (state == NULL)
    {
        return;
    }

    SDL_zero(*state);
    state->config = config != NULL ? *config : pong_rules_default_config();
    state->rng_state = seed != 0u ? seed : PONG_DEFAULT_SEED;
    pong_rules_reset_match(state);
}

void pong_rules_reset_match(pong_rules_state *state)
{
    if (state == NULL)
    {
        return;
    }

    state->player_score = 0;
    state->cpu_score = 0;
    state->player_y = 0.0f;
    state->cpu_y = 0.0f;
    state->match_finished = false;
    state->winner = PONG_WINNER_NONE;
    pong_rules_reset_round(state);
}

void pong_rules_reset_round(pong_rules_state *state)
{
    if (state == NULL)
    {
        return;
    }

    state->ball_position = pong_vec2_make(0.0f, 0.0f);
    state->ball_velocity = pong_vec2_make(0.0f, 0.0f);
    state->ball_active = false;
}

void pong_rules_serve_random(pong_rules_state *state, int direction)
{
    if (state == NULL || state->match_finished)
    {
        return;
    }

    int dir = direction;
    if (dir == 0)
    {
        dir = pong_random_01(state) < 0.5f ? -1 : 1;
    }
    dir = dir < 0 ? -1 : 1;

    const float hint = pong_random_01(state) * 2.0f - 1.0f;
    const float angle = hint * PONG_MAX_SERVE_ANGLE;
    state->ball_position = pong_vec2_make(0.0f, 0.0f);
    state->ball_velocity = pong_vec2_make(SDL_cosf(angle) * (float)dir * state->config.ball_speed,
                                          SDL_sinf(angle) * state->config.ball_speed);
    state->ball_active = true;
}

pong_rules_step_result pong_rules_step(pong_rules_state *state, const pong_rules_input *input, float dt)
{
    pong_rules_step_result result;
    SDL_zero(result);

    if (state == NULL)
    {
        return result;
    }

    result.winner = state->winner;
    result.player_score = state->player_score;
    result.cpu_score = state->cpu_score;

    if (dt <= 0.0f)
    {
        return result;
    }
    if (dt > PONG_MAX_STEP_DT)
    {
        dt = PONG_MAX_STEP_DT;
    }

    const float player_axis = input != NULL ? pong_clampf(input->player_axis, -1.0f, 1.0f) : 0.0f;
    state->player_y = pong_clamp_paddle_y(state, state->player_y + player_axis * state->config.paddle_speed * dt);

    const float cpu_target = state->ball_active ? state->ball_position.y : 0.0f;
    pong_move_toward(&state->cpu_y, cpu_target, state->config.cpu_speed * dt);
    state->cpu_y = pong_clamp_paddle_y(state, state->cpu_y);

    if (!state->ball_active || state->match_finished)
    {
        return result;
    }

    state->ball_position.x += state->ball_velocity.x * dt;
    state->ball_position.y += state->ball_velocity.y * dt;

    const float top = state->config.half_height - state->config.ball_radius;
    if (state->ball_position.y > top)
    {
        state->ball_position.y = top;
        state->ball_velocity.y = -pong_absf(state->ball_velocity.y);
        result.events |= PONG_EVENT_WALL_BOUNCE;
    }
    else if (state->ball_position.y < -top)
    {
        state->ball_position.y = -top;
        state->ball_velocity.y = pong_absf(state->ball_velocity.y);
        result.events |= PONG_EVENT_WALL_BOUNCE;
    }

    const float left_paddle_x = -state->config.paddle_x;
    const float right_paddle_x = state->config.paddle_x;
    if (state->ball_velocity.x < 0.0f && pong_overlap_paddle(state, left_paddle_x, state->player_y))
    {
        const float impact = (state->ball_position.y - state->player_y) / state->config.paddle_half_height;
        const float speed = SDL_min(pong_vec2_length(state->ball_velocity) + state->config.ball_speedup_per_hit,
                                    state->config.max_ball_speed);
        state->ball_position.x = left_paddle_x + state->config.paddle_half_width + state->config.ball_radius;
        pong_set_ball_speed(state, 1.0f, impact, speed);
        result.events |= PONG_EVENT_PADDLE_BOUNCE;
    }
    else if (state->ball_velocity.x > 0.0f && pong_overlap_paddle(state, right_paddle_x, state->cpu_y))
    {
        const float impact = (state->ball_position.y - state->cpu_y) / state->config.paddle_half_height;
        const float speed = SDL_min(pong_vec2_length(state->ball_velocity) + state->config.ball_speedup_per_hit,
                                    state->config.max_ball_speed);
        state->ball_position.x = right_paddle_x - state->config.paddle_half_width - state->config.ball_radius;
        pong_set_ball_speed(state, -1.0f, impact, speed);
        result.events |= PONG_EVENT_PADDLE_BOUNCE;
    }

    if (state->ball_position.x < -state->config.half_width - state->config.ball_radius)
    {
        pong_score_for(state, false, &result);
    }
    else if (state->ball_position.x > state->config.half_width + state->config.ball_radius)
    {
        pong_score_for(state, true, &result);
    }

    result.winner = state->winner;
    result.player_score = state->player_score;
    result.cpu_score = state->cpu_score;
    return result;
}

bool pong_rules_is_waiting_for_serve(const pong_rules_state *state)
{
    return state != NULL && !state->ball_active && !state->match_finished;
}

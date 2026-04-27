#include <gtest/gtest.h>

extern "C"
{
#include "pong_rules.h"
}

static float absf(float value)
{
    return value < 0.0f ? -value : value;
}

TEST(PongRules, InitializesCenteredInactiveMatch)
{
    pong_rules_state state{};
    pong_rules_init(&state, nullptr, 1234u);

    EXPECT_EQ(state.player_score, 0);
    EXPECT_EQ(state.cpu_score, 0);
    EXPECT_FLOAT_EQ(state.player_y, 0.0f);
    EXPECT_FLOAT_EQ(state.cpu_y, 0.0f);
    EXPECT_FLOAT_EQ(state.ball_position.x, 0.0f);
    EXPECT_FLOAT_EQ(state.ball_position.y, 0.0f);
    EXPECT_FALSE(state.ball_active);
    EXPECT_FALSE(state.match_finished);
    EXPECT_EQ(state.winner, PONG_WINNER_NONE);
    EXPECT_TRUE(pong_rules_is_waiting_for_serve(&state));
}

TEST(PongRules, RandomServeAvoidsExtremeAngles)
{
    pong_rules_state state{};
    pong_rules_init(&state, nullptr, 42u);

    pong_rules_serve_random(&state, 1);

    ASSERT_TRUE(state.ball_active);
    EXPECT_GT(state.ball_velocity.x, 0.0f);
    const float slope = absf(state.ball_velocity.y / state.ball_velocity.x);
    EXPECT_LT(slope, 0.7f);
}

TEST(PongRules, PaddleInputMovesAndClampsPlayer)
{
    pong_rules_state state{};
    pong_rules_config config = pong_rules_default_config();
    pong_rules_init(&state, &config, 1u);

    pong_rules_input input{};
    input.player_axis = 1.0f;
    for (int i = 0; i < 64; ++i)
    {
        pong_rules_step(&state, &input, 0.05f);
    }

    EXPECT_LE(state.player_y, config.half_height - config.paddle_half_height);
    EXPECT_FLOAT_EQ(state.player_y, config.half_height - config.paddle_half_height);
}

TEST(PongRules, BallReflectsFromTopAndBottomBounds)
{
    pong_rules_state state{};
    pong_rules_config config = pong_rules_default_config();
    pong_rules_init(&state, &config, 1u);
    state.ball_active = true;
    state.ball_position.y = config.half_height - config.ball_radius - 0.01f;
    state.ball_velocity.y = 5.0f;

    const pong_rules_step_result result = pong_rules_step(&state, nullptr, 0.02f);

    EXPECT_NE(result.events & PONG_EVENT_WALL_BOUNCE, 0);
    EXPECT_LT(state.ball_velocity.y, 0.0f);
    EXPECT_LE(state.ball_position.y, config.half_height - config.ball_radius);
}

TEST(PongRules, PlayerPaddleReflectsBallToTheRight)
{
    pong_rules_state state{};
    pong_rules_config config = pong_rules_default_config();
    pong_rules_init(&state, &config, 1u);
    state.ball_active = true;
    state.player_y = 0.0f;
    state.ball_position.x = -config.paddle_x + config.paddle_half_width + config.ball_radius - 0.01f;
    state.ball_position.y = 0.45f;
    state.ball_velocity.x = -config.ball_speed;
    state.ball_velocity.y = 0.0f;

    const pong_rules_step_result result = pong_rules_step(&state, nullptr, 0.01f);

    EXPECT_NE(result.events & PONG_EVENT_PADDLE_BOUNCE, 0);
    EXPECT_GT(state.ball_velocity.x, 0.0f);
    EXPECT_GT(state.ball_velocity.y, 0.0f);
}

TEST(PongRules, CpuScoresWhenBallLeavesLeftSide)
{
    pong_rules_state state{};
    pong_rules_config config = pong_rules_default_config();
    pong_rules_init(&state, &config, 1u);
    state.ball_active = true;
    state.ball_position.x = -config.half_width - config.ball_radius - 0.1f;
    state.ball_velocity.x = -config.ball_speed;

    const pong_rules_step_result result = pong_rules_step(&state, nullptr, 0.01f);

    EXPECT_NE(result.events & PONG_EVENT_CPU_SCORED, 0);
    EXPECT_EQ(result.cpu_score, 1);
    EXPECT_FALSE(state.ball_active);
    EXPECT_TRUE(pong_rules_is_waiting_for_serve(&state));
}

TEST(PongRules, MatchFinishesAtWinningScore)
{
    pong_rules_state state{};
    pong_rules_config config = pong_rules_default_config();
    config.winning_score = 2;
    pong_rules_init(&state, &config, 1u);
    state.player_score = 1;
    state.ball_active = true;
    state.ball_position.x = config.half_width + config.ball_radius + 0.1f;
    state.ball_velocity.x = config.ball_speed;

    const pong_rules_step_result result = pong_rules_step(&state, nullptr, 0.01f);

    EXPECT_NE(result.events & PONG_EVENT_PLAYER_SCORED, 0);
    EXPECT_NE(result.events & PONG_EVENT_MATCH_FINISHED, 0);
    EXPECT_TRUE(state.match_finished);
    EXPECT_EQ(state.winner, PONG_WINNER_PLAYER);
    EXPECT_FALSE(pong_rules_is_waiting_for_serve(&state));
}

TEST(PongRules, CpuPaddleTracksBall)
{
    pong_rules_state state{};
    pong_rules_init(&state, nullptr, 1u);
    state.ball_active = true;
    state.ball_position.y = 4.0f;

    pong_rules_step(&state, nullptr, 0.05f);

    EXPECT_GT(state.cpu_y, 0.0f);
}

#include <gtest/gtest.h>

extern "C"
{
#include "slayer3d/time.h"
#include <SDL3/SDL.h>
}

class SLAYER3DTimeTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        SDL_Init(0); /* needed for SDL_GetPerformanceCounter */
        slayer3d_time_reset();
    }
    void TearDown() override
    {
        slayer3d_time_reset();
    }
};

TEST_F(SLAYER3DTimeTest, InitialStateIsZero)
{
    /* After reset, all times should be zero. */
    EXPECT_FLOAT_EQ(slayer3d_time_get_delta_time(), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_unscaled_delta_time(), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_time(), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_real_time(), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_scale(), 1.0f);
    EXPECT_EQ(slayer3d_time_get_fixed_step_count(), 0);
    EXPECT_FLOAT_EQ(slayer3d_time_get_fixed_interpolation(), 0.0f);
}

TEST_F(SLAYER3DTimeTest, DefaultFixedDeltaTime)
{
    float fdt = slayer3d_time_get_fixed_delta_time();
    EXPECT_NEAR(fdt, 1.0f / 60.0f, 0.0001f);
}

TEST_F(SLAYER3DTimeTest, SetFixedDeltaTime)
{
    slayer3d_time_set_fixed_delta_time(1.0f / 30.0f);
    EXPECT_NEAR(slayer3d_time_get_fixed_delta_time(), 1.0f / 30.0f, 0.0001f);

    /* Reject non-positive values. */
    slayer3d_time_set_fixed_delta_time(0.0f);
    EXPECT_NEAR(slayer3d_time_get_fixed_delta_time(), 1.0f / 30.0f, 0.0001f);

    slayer3d_time_set_fixed_delta_time(-1.0f);
    EXPECT_NEAR(slayer3d_time_get_fixed_delta_time(), 1.0f / 30.0f, 0.0001f);
}

TEST_F(SLAYER3DTimeTest, SetTimeScale)
{
    slayer3d_time_set_scale(2.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_scale(), 2.0f);

    slayer3d_time_set_scale(0.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_scale(), 0.0f);

    /* Negative clamped to 0. */
    slayer3d_time_set_scale(-5.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_scale(), 0.0f);
}

TEST_F(SLAYER3DTimeTest, UpdateProducesDeltaTime)
{
    slayer3d_time_update(); /* first call initializes */
    SDL_Delay(16);          /* ~1 frame at 60fps */
    slayer3d_time_update();

    float dt = slayer3d_time_get_unscaled_delta_time();
    EXPECT_GT(dt, 0.005f); /* at least 5ms */
    EXPECT_LT(dt, 0.25f);  /* less than max clamp */

    float scaled = slayer3d_time_get_delta_time();
    EXPECT_NEAR(scaled, dt, 0.0001f); /* scale is 1.0 */
}

TEST_F(SLAYER3DTimeTest, TimeScaleAffectsScaledDeltaOnly)
{
    slayer3d_time_update();
    slayer3d_time_set_scale(0.5f);
    SDL_Delay(16);
    slayer3d_time_update();

    float unscaled = slayer3d_time_get_unscaled_delta_time();
    float scaled = slayer3d_time_get_delta_time();
    EXPECT_NEAR(scaled, unscaled * 0.5f, 0.001f);
}

TEST_F(SLAYER3DTimeTest, PauseStopsGameTime)
{
    slayer3d_time_update();
    slayer3d_time_set_scale(0.0f);
    SDL_Delay(16);
    slayer3d_time_update();

    EXPECT_FLOAT_EQ(slayer3d_time_get_delta_time(), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_time(), 0.0f);
    EXPECT_GT(slayer3d_time_get_real_time(), 0.0f);
    EXPECT_GT(slayer3d_time_get_unscaled_delta_time(), 0.0f);
}

TEST_F(SLAYER3DTimeTest, ElapsedTimeAccumulates)
{
    slayer3d_time_update();
    SDL_Delay(10);
    slayer3d_time_update();
    float t1 = slayer3d_time_get_time();
    float r1 = slayer3d_time_get_real_time();
    EXPECT_GT(t1, 0.0f);
    EXPECT_GT(r1, 0.0f);

    SDL_Delay(10);
    slayer3d_time_update();
    EXPECT_GT(slayer3d_time_get_time(), t1);
    EXPECT_GT(slayer3d_time_get_real_time(), r1);
}

TEST_F(SLAYER3DTimeTest, FixedStepCountIsReasonable)
{
    slayer3d_time_set_fixed_delta_time(1.0f / 60.0f);
    slayer3d_time_update();
    SDL_Delay(20); /* ~1.2 fixed steps */
    slayer3d_time_update();

    int steps = slayer3d_time_get_fixed_step_count();
    EXPECT_GE(steps, 0);
    EXPECT_LE(steps, 8); /* capped */
}

TEST_F(SLAYER3DTimeTest, FixedStepCountCappedAt8)
{
    /* Simulate a very long frame by using a large fixed dt denominator. */
    slayer3d_time_set_fixed_delta_time(0.001f); /* 1ms steps */
    slayer3d_time_update();
    SDL_Delay(50); /* 50ms = 50 steps, but capped at 8 */
    slayer3d_time_update();

    EXPECT_LE(slayer3d_time_get_fixed_step_count(), 8);
}

TEST_F(SLAYER3DTimeTest, FixedInterpolationInRange)
{
    slayer3d_time_update();
    SDL_Delay(10);
    slayer3d_time_update();

    float alpha = slayer3d_time_get_fixed_interpolation();
    EXPECT_GE(alpha, 0.0f);
    EXPECT_LE(alpha, 1.0f);
}

TEST_F(SLAYER3DTimeTest, ResetClearsEverything)
{
    slayer3d_time_update();
    SDL_Delay(10);
    slayer3d_time_update();
    slayer3d_time_set_scale(3.0f);

    slayer3d_time_reset();

    EXPECT_FLOAT_EQ(slayer3d_time_get_time(), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_real_time(), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_scale(), 1.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_delta_time(), 0.0f);
    EXPECT_EQ(slayer3d_time_get_fixed_step_count(), 0);
}

TEST_F(SLAYER3DTimeTest, FirstUpdateProducesZeroDelta)
{
    /* The very first update should produce dt=0 (no previous frame). */
    slayer3d_time_update();
    EXPECT_FLOAT_EQ(slayer3d_time_get_delta_time(), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_time_get_unscaled_delta_time(), 0.0f);
}

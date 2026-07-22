#include <gtest/gtest.h>

extern "C"
{
#include "game_frame_profile_internal.h"
}

TEST(GameFrameProfileTest, SummarizesFrameDistributionAndSlowestStageBreakdown)
{
    slayer3d_game_frame_profile profile;
    slayer3d_game_frame_profile_reset(&profile, 1000.0 / 60.0);

    slayer3d_game_frame_profile_record(&profile, {10.0, 1.0, 2.0, 3.0, 4.0, 1});
    slayer3d_game_frame_profile_record(&profile, {20.0, 2.0, 3.0, 4.0, 11.0, 1});
    slayer3d_game_frame_profile_record(&profile, {30.0, 3.0, 4.0, 5.0, 18.0, 2});
    slayer3d_game_frame_profile_record(&profile, {40.0, 4.0, 5.0, 6.0, 25.0, 3});

    slayer3d_game_frame_profile_summary summary;
    ASSERT_TRUE(slayer3d_game_frame_profile_summarize(&profile, &summary));
    EXPECT_DOUBLE_EQ(summary.frame_average_ms, 25.0);
    EXPECT_DOUBLE_EQ(summary.frame_p50_ms, 20.0);
    EXPECT_DOUBLE_EQ(summary.frame_p95_ms, 40.0);
    EXPECT_DOUBLE_EQ(summary.frame_p99_ms, 40.0);
    EXPECT_DOUBLE_EQ(summary.slowest.frame_ms, 40.0);
    EXPECT_DOUBLE_EQ(summary.slowest.present_ms, 25.0);
    EXPECT_EQ(summary.missed_budget_count, 3);
    EXPECT_EQ(summary.hitch_33ms_count, 1);
    EXPECT_EQ(summary.hitch_50ms_count, 0);
    EXPECT_DOUBLE_EQ(summary.ticks_per_frame, 1.75);
    EXPECT_EQ(summary.max_ticks, 3);
    EXPECT_EQ(summary.sampled_frame_count, 4);
    EXPECT_EQ(summary.frame_count, 4);
}

TEST(GameFrameProfileTest, RetainsAggregateCountsWhenSampleCapacityIsExceeded)
{
    slayer3d_game_frame_profile profile;
    slayer3d_game_frame_profile_reset(&profile, 16.0);
    for (int i = 0; i < SLAYER3D_GAME_FRAME_PROFILE_SAMPLE_CAPACITY + 3; ++i)
        slayer3d_game_frame_profile_record(&profile, {20.0, 1.0, 2.0, 3.0, 14.0, 1});

    slayer3d_game_frame_profile_summary summary;
    ASSERT_TRUE(slayer3d_game_frame_profile_summarize(&profile, &summary));
    EXPECT_EQ(summary.sampled_frame_count, SLAYER3D_GAME_FRAME_PROFILE_SAMPLE_CAPACITY);
    EXPECT_EQ(summary.frame_count, SLAYER3D_GAME_FRAME_PROFILE_SAMPLE_CAPACITY + 3);
    EXPECT_EQ(summary.missed_budget_count, summary.frame_count);
    EXPECT_DOUBLE_EQ(summary.frame_average_ms, 20.0);
}

TEST(GameFrameProfileTest, RejectsEmptyProfiles)
{
    slayer3d_game_frame_profile profile;
    slayer3d_game_frame_profile_reset(&profile, 0.0);
    slayer3d_game_frame_profile_summary summary;
    EXPECT_FALSE(slayer3d_game_frame_profile_summarize(&profile, &summary));
    EXPECT_NEAR(profile.budget_ms, 1000.0 / 60.0, 0.000001);
}

/*
 * Unit tests for sdl3d_fps_mover. The mover is exercised against a
 * minimal level (a single flat sector or two stacked sectors) so the
 * physics paths can be observed without a real renderer.
 */

#include <gtest/gtest.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

extern "C"
{
#include "sdl3d/fps_mover.h"
#include "sdl3d/level.h"
#include "sdl3d/math.h"
}

namespace
{
sdl3d_sector MakeSquareSector(float min_x, float min_z, float max_x, float max_z, float floor_y, float ceil_y)
{
    sdl3d_sector s{};
    s.points[0][0] = min_x;
    s.points[0][1] = min_z;
    s.points[1][0] = max_x;
    s.points[1][1] = min_z;
    s.points[2][0] = max_x;
    s.points[2][1] = max_z;
    s.points[3][0] = min_x;
    s.points[3][1] = max_z;
    s.num_points = 4;
    s.floor_y = floor_y;
    s.ceil_y = ceil_y;
    s.floor_material = 0;
    s.ceil_material = 1;
    s.wall_material = 2;
    return s;
}

sdl3d_sector MakeRampSector(float min_x, float min_z, float max_x, float max_z, float center_floor_y, float ceil_y,
                            float slope_x)
{
    sdl3d_sector s = MakeSquareSector(min_x, min_z, max_x, max_z, center_floor_y, ceil_y);
    const float len = SDL_sqrtf(slope_x * slope_x + 1.0f);
    s.floor_normal[0] = -slope_x / len;
    s.floor_normal[1] = 1.0f / len;
    s.floor_normal[2] = 0.0f;
    return s;
}

sdl3d_level_material MakeMaterial()
{
    sdl3d_level_material m{};
    m.albedo[0] = m.albedo[1] = m.albedo[2] = m.albedo[3] = 1.0f;
    m.roughness = 1.0f;
    m.tex_scale = 4.0f;
    return m;
}

sdl3d_fps_mover_config DefaultConfig()
{
    sdl3d_fps_mover_config c{};
    c.move_speed = 12.0f;
    c.jump_velocity = 6.0f;
    c.gravity = 14.0f;
    c.player_height = 1.6f;
    c.player_radius = 0.35f;
    c.step_height = 1.1f;
    c.ceiling_clearance = 0.1f;
    return c;
}

class FpsMoverFixture : public ::testing::Test
{
  protected:
    sdl3d_level level{};
    std::vector<sdl3d_sector> sectors;

    void BuildFlat()
    {
        sectors = {MakeSquareSector(-10, -10, 10, 10, 0.0f, 5.0f)};
        const sdl3d_level_material mats[] = {MakeMaterial(), MakeMaterial(), MakeMaterial()};
        ASSERT_TRUE(sdl3d_build_level(sectors.data(), (int)sectors.size(), mats, 3, nullptr, 0, &level))
            << SDL_GetError();
    }

    void BuildStair()
    {
        /* Two sectors side by side, the second one a step up. */
        sectors = {MakeSquareSector(-10, -10, 0, 10, 0.0f, 5.0f), MakeSquareSector(0, -10, 10, 10, 1.0f, 6.0f)};
        const sdl3d_level_material mats[] = {MakeMaterial(), MakeMaterial(), MakeMaterial()};
        ASSERT_TRUE(sdl3d_build_level(sectors.data(), (int)sectors.size(), mats, 3, nullptr, 0, &level))
            << SDL_GetError();
    }

    void BuildWalkableRamp()
    {
        sectors = {MakeRampSector(-5, -5, 5, 5, 1.0f, 6.0f, 0.2f)};
        const sdl3d_level_material mats[] = {MakeMaterial(), MakeMaterial(), MakeMaterial()};
        ASSERT_TRUE(sdl3d_build_level(sectors.data(), (int)sectors.size(), mats, 3, nullptr, 0, &level))
            << SDL_GetError();
    }

    void BuildSteepRamp()
    {
        sectors = {MakeSquareSector(-10, -5, 0, 5, 0.0f, 5.0f), MakeRampSector(0, -5, 10, 5, 5.5f, 14.0f, 1.1f)};
        const sdl3d_level_material mats[] = {MakeMaterial(), MakeMaterial(), MakeMaterial()};
        ASSERT_TRUE(sdl3d_build_level(sectors.data(), (int)sectors.size(), mats, 3, nullptr, 0, &level))
            << SDL_GetError();
    }

    void BuildRaisedPlatform()
    {
        sectors = {MakeSquareSector(-10, -10, 0, 10, 0.0f, 8.0f), MakeSquareSector(0, -5, 4, 5, 2.5f, 10.0f)};
        const sdl3d_level_material mats[] = {MakeMaterial(), MakeMaterial(), MakeMaterial()};
        ASSERT_TRUE(sdl3d_build_level(sectors.data(), (int)sectors.size(), mats, 3, nullptr, 0, &level))
            << SDL_GetError();
    }

    void BuildConveyor(float push_x, float push_z)
    {
        sectors = {MakeSquareSector(-50, -10, 50, 10, 0.0f, 5.0f)};
        sectors[0].push_velocity[0] = push_x;
        sectors[0].push_velocity[2] = push_z;
        const sdl3d_level_material mats[] = {MakeMaterial(), MakeMaterial(), MakeMaterial()};
        ASSERT_TRUE(sdl3d_build_level(sectors.data(), (int)sectors.size(), mats, 3, nullptr, 0, &level))
            << SDL_GetError();
    }

    void TearDown() override
    {
        sdl3d_free_level(&level);
    }
};

} // namespace

TEST_F(FpsMoverFixture, InitSetsSpawnAndDefaults)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(1, 1.6f, 2), 0.5f);

    EXPECT_FLOAT_EQ(m.position.x, 1.0f);
    EXPECT_FLOAT_EQ(m.position.y, 1.6f);
    EXPECT_FLOAT_EQ(m.position.z, 2.0f);
    EXPECT_FLOAT_EQ(m.yaw, 0.5f);
    EXPECT_FLOAT_EQ(m.pitch, 0.0f);
    EXPECT_TRUE(m.on_ground);
    EXPECT_FLOAT_EQ(m.vertical_velocity, 0.0f);
    EXPECT_FLOAT_EQ(m.view_smooth, 0.0f);
}

TEST_F(FpsMoverFixture, JumpRequiresOnGround)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(0, 1.6f, 0), 0);

    sdl3d_fps_mover_jump(&m);
    EXPECT_FLOAT_EQ(m.vertical_velocity, cfg.jump_velocity);
    EXPECT_FALSE(m.on_ground);

    /* Second jump in mid-air is a no-op. */
    sdl3d_fps_mover_jump(&m);
    EXPECT_FLOAT_EQ(m.vertical_velocity, cfg.jump_velocity);
}

TEST_F(FpsMoverFixture, JumpRisesAndFallsBackToFloor)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(0, 1.6f, 0), 0);

    sdl3d_fps_mover_jump(&m);

    /* Simulate ~2 seconds of physics; player should be back on the ground. */
    sdl3d_vec2 zero{0, 0};
    for (int i = 0; i < 200; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), zero, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_TRUE(m.on_ground);
    EXPECT_NEAR(m.position.y, 1.6f, 0.01f);
    EXPECT_FLOAT_EQ(m.vertical_velocity, 0.0f);
}

TEST_F(FpsMoverFixture, MouseDeltaUpdatesYawAndClampsPitch)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(0, 1.6f, 0), 0);

    sdl3d_vec2 zero{0, 0};
    /* Yaw moves with mouse_dx. */
    sdl3d_fps_mover_update(&m, &level, sectors.data(), zero, 100.0f, 0.0f, 0.002f, 1.0f / 60.0f);
    EXPECT_NEAR(m.yaw, 0.2f, 1e-4f);

    /* Pitch saturates at the internal limit. */
    sdl3d_fps_mover_update(&m, &level, sectors.data(), zero, 0.0f, -100000.0f, 0.002f, 1.0f / 60.0f);
    EXPECT_GT(m.pitch, 1.39f);
    EXPECT_LT(m.pitch, 1.41f);
}

TEST_F(FpsMoverFixture, WishDirectionAdvancesPosition)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(0, 1.6f, 0), 0);

    sdl3d_vec2 forward{1.0f, 0.0f};
    /* 1 second @ 12 u/s wish in +X direction. */
    for (int i = 0; i < 60; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), forward, 0, 0, 0.002f, 1.0f / 60.0f);
    }
    EXPECT_GT(m.position.x, 5.0f);
    EXPECT_LT(m.position.x, 13.0f);
    EXPECT_NEAR(m.position.z, 0.0f, 0.01f);
}

TEST_F(FpsMoverFixture, SectorPushVelocityMovesPlayerWithoutInput)
{
    BuildConveyor(8.0f, 0.0f);
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(0.0f, cfg.player_height, 0.0f), 0);

    sdl3d_vec2 zero{0.0f, 0.0f};
    for (int i = 0; i < 60; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), zero, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_GT(m.position.x, 7.5f);
    EXPECT_LT(m.position.x, 8.5f);
    EXPECT_NEAR(m.position.z, 0.0f, 0.01f);
    EXPECT_TRUE(m.on_ground);
}

TEST_F(FpsMoverFixture, SectorPushVelocityCombinesWithInput)
{
    BuildConveyor(8.0f, 0.0f);
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_vec2 with_conveyor{1.0f, 0.0f};
    sdl3d_vec2 against_conveyor{-1.0f, 0.0f};

    sdl3d_fps_mover with;
    sdl3d_fps_mover_init(&with, &cfg, sdl3d_vec3_make(0.0f, cfg.player_height, 0.0f), 0);
    for (int i = 0; i < 60; ++i)
    {
        sdl3d_fps_mover_update(&with, &level, sectors.data(), with_conveyor, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    sdl3d_fps_mover against;
    sdl3d_fps_mover_init(&against, &cfg, sdl3d_vec3_make(0.0f, cfg.player_height, 0.0f), 0);
    for (int i = 0; i < 60; ++i)
    {
        sdl3d_fps_mover_update(&against, &level, sectors.data(), against_conveyor, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_GT(with.position.x, 19.0f);
    EXPECT_LT(against.position.x, -3.0f);
    EXPECT_LT(SDL_fabsf(against.position.x), with.position.x * 0.5f);
}

TEST_F(FpsMoverFixture, SolidWallMaintainsPlayerRadius)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(8.0f, cfg.player_height, 0.0f), 0);

    sdl3d_vec2 into_wall{1.0f, 0.0f};
    for (int i = 0; i < 60; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), into_wall, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_LE(m.position.x, 10.0f - cfg.player_radius + 0.01f);
    EXPECT_NEAR(m.position.z, 0.0f, 0.01f);
    EXPECT_TRUE(m.on_ground);
}

TEST_F(FpsMoverFixture, SolidWallSlidesOnlyAlongWallPlane)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(9.5f, cfg.player_height, 0.0f), 0);

    sdl3d_vec2 diagonal_into_wall{1.0f, 1.0f};
    for (int i = 0; i < 20; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), diagonal_into_wall, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_LE(m.position.x, 10.0f - cfg.player_radius + 0.01f);
    EXPECT_GT(m.position.z, 0.5f);
    EXPECT_TRUE(m.on_ground);
}

TEST_F(FpsMoverFixture, SolidWallAllowsParallelMotionAtContact)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(10.0f - cfg.player_radius + 0.001f, cfg.player_height, 0.0f), 0);

    sdl3d_vec2 parallel_to_wall{0.0f, 1.0f};
    for (int i = 0; i < 20; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), parallel_to_wall, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_GT(m.position.z, 2.0f);
    EXPECT_LE(m.position.x, 10.0f - cfg.player_radius + 0.02f);
    EXPECT_TRUE(m.on_ground);
}

TEST_F(FpsMoverFixture, StepUpOntoHigherSectorWhenWalking)
{
    BuildStair();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    /* Spawn standing on the lower sector, near the boundary. */
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(-1.0f, 1.6f, 0), 0);

    /* Walk in +X over the step. */
    sdl3d_vec2 fwd{1.0f, 0.0f};
    for (int i = 0; i < 60; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), fwd, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_GT(m.position.x, 1.0f);
    /* Player should now be standing on floor=1.0 → eye at 2.6. */
    EXPECT_NEAR(m.position.y, 2.6f, 0.05f);
    EXPECT_TRUE(m.on_ground);
}

TEST_F(FpsMoverFixture, StrafeOffStairEdgeKeepsMoving)
{
    BuildStair();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(0.2f, 1.0f + cfg.player_height, -4.0f), 0);

    sdl3d_vec2 off_stair_edge{-0.25f, 1.0f};
    for (int i = 0; i < 30; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), off_stair_edge, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_GT(m.position.z, 0.5f);
    EXPECT_LT(m.position.x, 0.0f);
}

TEST_F(FpsMoverFixture, WalksSmoothlyUpWalkableSlope)
{
    BuildWalkableRamp();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    const float start_x = -4.0f;
    const float start_z = 0.0f;
    const float start_floor = sdl3d_sector_floor_at(&sectors[0], start_x, start_z);
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(start_x, start_floor + cfg.player_height, start_z), 0);

    sdl3d_vec2 uphill{1.0f, 0.0f};
    for (int i = 0; i < 30; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), uphill, 0, 0, 0.002f, 1.0f / 120.0f);
    }

    EXPECT_GT(m.position.x, start_x);
    EXPECT_TRUE(m.on_ground);
    EXPECT_NEAR(m.position.y, sdl3d_sector_floor_at(&sectors[0], m.position.x, m.position.z) + cfg.player_height,
                0.02f);
    EXPECT_GT(m.position.y, start_floor + cfg.player_height);
}

TEST_F(FpsMoverFixture, RejectsSteepSlopeAsWalkableGround)
{
    BuildSteepRamp();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(-1.0f, cfg.player_height, 0), 0);

    sdl3d_vec2 toward_steep_slope{1.0f, 0.0f};
    for (int i = 0; i < 60; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), toward_steep_slope, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_LT(m.position.x, 0.0f);
    EXPECT_TRUE(m.on_ground);
    EXPECT_EQ(m.current_sector, 0);
}

TEST_F(FpsMoverFixture, JumpWorksFromRaisedPlatform)
{
    BuildRaisedPlatform();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(2.0f, 2.5f + cfg.player_height, 0.0f), 0);

    sdl3d_fps_mover_jump(&m);
    EXPECT_FALSE(m.on_ground);
    EXPECT_FLOAT_EQ(m.vertical_velocity, cfg.jump_velocity);

    const float start_y = m.position.y;
    sdl3d_vec2 zero{0, 0};
    sdl3d_fps_mover_update(&m, &level, sectors.data(), zero, 0, 0, 0.002f, 1.0f / 60.0f);

    EXPECT_GT(m.position.y, start_y);
    EXPECT_FALSE(m.on_ground);
}

TEST_F(FpsMoverFixture, AirborneMoverCanLeaveRaisedPlatform)
{
    BuildRaisedPlatform();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(2.0f, 2.5f + cfg.player_height, 0.0f), 0);

    sdl3d_fps_mover_jump(&m);

    sdl3d_vec2 toward_lower_floor{-1.0f, 0.0f};
    for (int i = 0; i < 30; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), toward_lower_floor, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_LT(m.position.x, -1.0f);
    EXPECT_GT(m.position.y - cfg.player_height, 0.0f);
    EXPECT_FALSE(m.on_ground);
}

TEST_F(FpsMoverFixture, GroundedMoverCanWalkAlongRaisedPlatformEdge)
{
    BuildRaisedPlatform();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(0.2f, 2.5f + cfg.player_height, -4.0f), 0);

    sdl3d_vec2 along_edge{0.0f, 1.0f};
    for (int i = 0; i < 30; ++i)
    {
        sdl3d_fps_mover_update(&m, &level, sectors.data(), along_edge, 0, 0, 0.002f, 1.0f / 60.0f);
    }

    EXPECT_NEAR(m.position.x, 0.2f, 0.05f);
    EXPECT_GT(m.position.z, 0.5f);
    EXPECT_TRUE(m.on_ground);
    EXPECT_NEAR(m.position.y, 2.5f + cfg.player_height, 0.01f);
}

TEST_F(FpsMoverFixture, FallingBelowWorldRestoresLastGood)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(0, 1.6f, 0), 0);

    /* Run one frame to capture last-known-good. */
    sdl3d_vec2 zero{0, 0};
    sdl3d_fps_mover_update(&m, &level, sectors.data(), zero, 0, 0, 0.002f, 1.0f / 60.0f);
    sdl3d_vec3 expected_good = m.position;

    /* Forcibly drop the player below the world and run physics. */
    m.position.y = -100.0f;
    m.on_ground = false;
    m.vertical_velocity = -10.0f;

    sdl3d_fps_mover_update(&m, &level, sectors.data(), zero, 0, 0, 0.002f, 1.0f / 60.0f);

    EXPECT_TRUE(m.on_ground);
    EXPECT_NEAR(m.position.x, expected_good.x, 0.01f);
    EXPECT_NEAR(m.position.y, expected_good.y, 0.01f);
    EXPECT_NEAR(m.position.z, expected_good.z, 0.01f);
}

TEST_F(FpsMoverFixture, LaunchAppliesUpwardVelocityEvenWhenAirborne)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(0, cfg.player_height, 0), 0);

    sdl3d_fps_mover_launch(&m, 14.0f);

    EXPECT_FALSE(m.on_ground);
    EXPECT_FLOAT_EQ(m.vertical_velocity, 14.0f);
    sdl3d_fps_mover_update(&m, &level, sectors.data(), sdl3d_vec2{0, 0}, 0, 0, 0.002f, 1.0f / 60.0f);
    EXPECT_GT(m.position.y, cfg.player_height);

    sdl3d_fps_mover_launch(&m, 10.0f);
    EXPECT_FLOAT_EQ(m.vertical_velocity, 10.0f);
    sdl3d_fps_mover_launch(&m, 0.0f);
    EXPECT_FLOAT_EQ(m.vertical_velocity, 10.0f);
}

TEST_F(FpsMoverFixture, NullArgsAreSafeNoOps)
{
    BuildFlat();
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(0, 1.6f, 0), 0);

    sdl3d_vec2 zero{0, 0};
    /* These should not crash. */
    sdl3d_fps_mover_update(nullptr, &level, sectors.data(), zero, 0, 0, 0, 1.0f / 60.0f);
    sdl3d_fps_mover_update(&m, nullptr, sectors.data(), zero, 0, 0, 0, 1.0f / 60.0f);
    sdl3d_fps_mover_update(&m, &level, nullptr, zero, 0, 0, 0, 1.0f / 60.0f);
    sdl3d_fps_mover_jump(nullptr);
    sdl3d_fps_mover_launch(nullptr, 12.0f);
    sdl3d_fps_mover_init(nullptr, &cfg, sdl3d_vec3_make(0, 0, 0), 0);
    sdl3d_fps_mover_init(&m, nullptr, sdl3d_vec3_make(0, 0, 0), 0);
}

TEST(FpsMoverCamera, CameraIncludesViewSmoothOffset)
{
    sdl3d_fps_mover_config cfg = DefaultConfig();
    sdl3d_fps_mover m;
    sdl3d_fps_mover_init(&m, &cfg, sdl3d_vec3_make(1, 2, 3), 0);
    m.view_smooth = 0.25f;

    sdl3d_camera3d cam = sdl3d_fps_mover_camera(&m, 75.0f);
    EXPECT_FLOAT_EQ(cam.position.x, 1.0f);
    EXPECT_FLOAT_EQ(cam.position.y, 2.25f);
    EXPECT_FLOAT_EQ(cam.position.z, 3.0f);
    EXPECT_FLOAT_EQ(cam.fovy, 75.0f);
    EXPECT_EQ(cam.projection, SDL3D_CAMERA_PERSPECTIVE);
}

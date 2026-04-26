/*
 * Unit tests for sdl3d_sprite_actor / sdl3d_sprite_scene.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/math.h"
#include "sdl3d/sprite_actor.h"
}

static constexpr float SPRITE_TEST_PI = 3.14159265358979323846f;

static sdl3d_sector make_flat_test_sector(float floor_y, float ceil_y)
{
    sdl3d_sector sector{};
    sector.points[0][0] = 0.0f;
    sector.points[0][1] = 0.0f;
    sector.points[1][0] = 10.0f;
    sector.points[1][1] = 0.0f;
    sector.points[2][0] = 10.0f;
    sector.points[2][1] = 10.0f;
    sector.points[3][0] = 0.0f;
    sector.points[3][1] = 10.0f;
    sector.num_points = 4;
    sector.floor_y = floor_y;
    sector.ceil_y = ceil_y;
    return sector;
}

/* ================================================================== */
/* Scene lifecycle                                                    */
/* ================================================================== */

TEST(SpriteScene, InitAndFree)
{
    sdl3d_sprite_scene scene;
    sdl3d_sprite_scene_init(&scene);
    EXPECT_EQ(scene.count, 0);
    EXPECT_EQ(scene.capacity, 0);
    sdl3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, FreeNullIsSafe)
{
    sdl3d_sprite_scene_free(nullptr);
}

TEST(SpriteScene, AddReturnsValidActor)
{
    sdl3d_sprite_scene scene;
    sdl3d_sprite_scene_init(&scene);

    sdl3d_sprite_actor *a = sdl3d_sprite_scene_add(&scene);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(scene.count, 1);
    EXPECT_TRUE(a->visible);
    EXPECT_EQ(a->sector_id, -1);
    EXPECT_EQ(a->tint.r, 255);
    EXPECT_EQ(a->tint.a, 255);
    EXPECT_FLOAT_EQ(a->visual_ground_offset, 0.0f);
    EXPECT_FLOAT_EQ(a->facing_yaw, 0.0f);

    sdl3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, AddNullReturnsNull)
{
    EXPECT_EQ(sdl3d_sprite_scene_add(nullptr), nullptr);
}

TEST(SpriteScene, RemoveSwapsWithLast)
{
    sdl3d_sprite_scene scene;
    sdl3d_sprite_scene_init(&scene);

    sdl3d_sprite_actor *a = sdl3d_sprite_scene_add(&scene);
    sdl3d_sprite_actor *b = sdl3d_sprite_scene_add(&scene);
    sdl3d_sprite_actor *c = sdl3d_sprite_scene_add(&scene);
    a->bob_speed = 1.0f;
    b->bob_speed = 2.0f;
    c->bob_speed = 3.0f;

    /* Remove index 0 — c should swap into slot 0. */
    sdl3d_sprite_scene_remove(&scene, 0);
    EXPECT_EQ(scene.count, 2);
    EXPECT_FLOAT_EQ(scene.actors[0].bob_speed, 3.0f);
    EXPECT_FLOAT_EQ(scene.actors[1].bob_speed, 2.0f);

    sdl3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, RemoveOutOfBoundsIsSafe)
{
    sdl3d_sprite_scene scene;
    sdl3d_sprite_scene_init(&scene);
    sdl3d_sprite_scene_remove(&scene, 0);
    sdl3d_sprite_scene_remove(&scene, -1);
    sdl3d_sprite_scene_remove(nullptr, 0);
    sdl3d_sprite_scene_free(&scene);
}

/* ================================================================== */
/* Update                                                             */
/* ================================================================== */

TEST(SpriteScene, UpdateAdvancesBobPhase)
{
    sdl3d_sprite_scene scene;
    sdl3d_sprite_scene_init(&scene);

    sdl3d_sprite_actor *a = sdl3d_sprite_scene_add(&scene);
    a->bob_amplitude = 0.1f;
    a->bob_speed = 2.0f;
    EXPECT_FLOAT_EQ(a->bob_phase, 0.0f);

    sdl3d_sprite_scene_update(&scene, 0.5f);
    EXPECT_FLOAT_EQ(scene.actors[0].bob_phase, 0.5f);

    sdl3d_sprite_scene_update(&scene, 0.25f);
    EXPECT_FLOAT_EQ(scene.actors[0].bob_phase, 0.75f);

    sdl3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, UpdateAdvancesLoopingAnimation)
{
    sdl3d_sprite_scene scene;
    sdl3d_sprite_scene_init(&scene);

    sdl3d_sprite_rotation_set frames[3]{};
    sdl3d_sprite_actor *a = sdl3d_sprite_scene_add(&scene);
    ASSERT_NE(a, nullptr);
    sdl3d_sprite_actor_play_animation(a, frames, 3, 2.0f, true);

    sdl3d_sprite_scene_update(&scene, 0.50f);
    EXPECT_EQ(sdl3d_sprite_actor_current_animation_frame(&scene.actors[0]), 1);

    sdl3d_sprite_scene_update(&scene, 1.00f);
    EXPECT_EQ(sdl3d_sprite_actor_current_animation_frame(&scene.actors[0]), 0);

    sdl3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, UpdateClampsNonLoopingAnimation)
{
    sdl3d_sprite_scene scene;
    sdl3d_sprite_scene_init(&scene);

    sdl3d_sprite_rotation_set frames[3]{};
    sdl3d_sprite_actor *a = sdl3d_sprite_scene_add(&scene);
    ASSERT_NE(a, nullptr);
    sdl3d_sprite_actor_play_animation(a, frames, 3, 10.0f, false);

    sdl3d_sprite_scene_update(&scene, 1.0f);
    EXPECT_EQ(sdl3d_sprite_actor_current_animation_frame(&scene.actors[0]), 2);

    sdl3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, UpdateNullIsSafe)
{
    sdl3d_sprite_scene_update(nullptr, 1.0f);
}

TEST(SpriteActor, DrawPositionAppliesVisualGroundOffset)
{
    sdl3d_sprite_actor actor{};
    actor.position = sdl3d_vec3_make(2.0f, 3.0f, 4.0f);
    actor.visual_ground_offset = 0.5f;

    sdl3d_vec3 pos = sdl3d_sprite_actor_draw_position(&actor);
    EXPECT_FLOAT_EQ(pos.x, 2.0f);
    EXPECT_FLOAT_EQ(pos.y, 2.5f);
    EXPECT_FLOAT_EQ(pos.z, 4.0f);
}

TEST(SpriteActor, DrawPositionNullReturnsOrigin)
{
    sdl3d_vec3 pos = sdl3d_sprite_actor_draw_position(nullptr);
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(pos.z, 0.0f);
}

TEST(SpriteActor, SetFacingDirectionUsesWorldXZYaw)
{
    sdl3d_sprite_actor actor{};

    sdl3d_sprite_actor_set_facing_direction(&actor, 1.0f, 0.0f);
    EXPECT_NEAR(actor.facing_yaw, SPRITE_TEST_PI * 0.5f, 1e-5f);

    sdl3d_sprite_actor_set_facing_direction(&actor, 0.0f, -1.0f);
    EXPECT_NEAR(actor.facing_yaw, 0.0f, 1e-5f);

    sdl3d_sprite_actor_set_facing_direction(&actor, 0.0f, 0.0f);
    EXPECT_NEAR(actor.facing_yaw, 0.0f, 1e-5f);
}

TEST(SpriteActor, SetFacingYawWrapsToSignedRange)
{
    sdl3d_sprite_actor actor{};
    sdl3d_sprite_actor_set_facing_yaw(&actor, SPRITE_TEST_PI * 2.5f);

    EXPECT_NEAR(actor.facing_yaw, SPRITE_TEST_PI * 0.5f, 1e-5f);
}

TEST(SpriteActor, CanStandAtReturnsFloorHeight)
{
    sdl3d_level level{};
    sdl3d_sector sector = make_flat_test_sector(2.0f, 6.0f);
    level.sector_count = 1;

    sdl3d_sprite_actor actor{};
    actor.position = sdl3d_vec3_make(1.0f, 2.0f, 1.0f);

    float floor_y = 0.0f;
    EXPECT_TRUE(sdl3d_sprite_actor_can_stand_at(&actor, &level, &sector, 5.0f, 5.0f, 0.5f, 1.8f, &floor_y));
    EXPECT_FLOAT_EQ(floor_y, 2.0f);
}

TEST(SpriteActor, CanStandAtRejectsOutsideLevelAndTooHighStep)
{
    sdl3d_level level{};
    sdl3d_sector sector = make_flat_test_sector(2.0f, 6.0f);
    level.sector_count = 1;

    sdl3d_sprite_actor actor{};
    actor.position = sdl3d_vec3_make(1.0f, 0.0f, 1.0f);

    EXPECT_FALSE(sdl3d_sprite_actor_can_stand_at(&actor, &level, &sector, 12.0f, 5.0f, 0.5f, 1.8f, nullptr));
    EXPECT_FALSE(sdl3d_sprite_actor_can_stand_at(&actor, &level, &sector, 5.0f, 5.0f, 0.5f, 1.8f, nullptr));
}

TEST(SpriteActor, SnapToGroundUsesHighestSupportUnderProbe)
{
    sdl3d_level level{};
    sdl3d_sector sector = make_flat_test_sector(2.0f, 6.0f);
    level.sector_count = 1;

    sdl3d_sprite_actor actor{};
    actor.position = sdl3d_vec3_make(5.0f, 2.4f, 5.0f);

    EXPECT_TRUE(sdl3d_sprite_actor_snap_to_ground(&actor, &level, &sector, 0.5f, 1.8f));
    EXPECT_FLOAT_EQ(actor.position.y, 2.0f);
}

/* ================================================================== */
/* Texture selection                                                  */
/* ================================================================== */

TEST(SpriteActor, SelectTextureNoRotationsReturnsSingle)
{
    sdl3d_texture2d tex{};
    tex.width = 32;
    sdl3d_sprite_actor actor{};
    actor.texture = &tex;
    actor.rotations = nullptr;

    const sdl3d_texture2d *result = sdl3d_sprite_select_texture(&actor, 10.0f, 10.0f);
    EXPECT_EQ(result, &tex);
}

TEST(SpriteActor, SelectTextureNullActorReturnsNull)
{
    EXPECT_EQ(sdl3d_sprite_select_texture(nullptr, 0, 0), nullptr);
}

TEST(SpriteActor, SelectTextureSouthWhenCameraSouth)
{
    sdl3d_texture2d frames[SDL3D_SPRITE_ROTATION_COUNT]{};
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
        frames[i].width = i + 1; /* use width as a tag */

    sdl3d_sprite_rotation_set rot{};
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
        rot.frames[i] = &frames[i];

    sdl3d_sprite_actor actor{};
    actor.position = sdl3d_vec3_make(0, 0, 0);
    actor.rotations = &rot;

    /* Camera directly south of actor (negative Z). */
    const sdl3d_texture2d *result = sdl3d_sprite_select_texture(&actor, 0.0f, -10.0f);
    /* Should select frame[0] = south. */
    EXPECT_EQ(result->width, 1);
}

TEST(SpriteActor, SelectTextureNorthWhenCameraNorth)
{
    sdl3d_texture2d frames[SDL3D_SPRITE_ROTATION_COUNT]{};
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
        frames[i].width = i + 1;

    sdl3d_sprite_rotation_set rot{};
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
        rot.frames[i] = &frames[i];

    sdl3d_sprite_actor actor{};
    actor.position = sdl3d_vec3_make(0, 0, 0);
    actor.rotations = &rot;

    /* Camera directly north of actor (positive Z). */
    const sdl3d_texture2d *result = sdl3d_sprite_select_texture(&actor, 0.0f, 10.0f);
    /* Should select frame[4] = north. */
    EXPECT_EQ(result->width, 5);
}

TEST(SpriteActor, SelectTextureUsesActorFacingYaw)
{
    sdl3d_texture2d frames[SDL3D_SPRITE_ROTATION_COUNT]{};
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
        frames[i].width = i + 1;

    sdl3d_sprite_rotation_set rot{};
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
        rot.frames[i] = &frames[i];

    sdl3d_sprite_actor actor{};
    actor.position = sdl3d_vec3_make(0, 0, 0);
    actor.rotations = &rot;

    sdl3d_sprite_actor_set_facing_direction(&actor, 1.0f, 0.0f);
    const sdl3d_texture2d *front_from_east = sdl3d_sprite_select_texture(&actor, 10.0f, 0.0f);
    ASSERT_NE(front_from_east, nullptr);
    EXPECT_EQ(front_from_east->width, 1);

    const sdl3d_texture2d *side_from_south = sdl3d_sprite_select_texture(&actor, 0.0f, -10.0f);
    ASSERT_NE(side_from_south, nullptr);
    EXPECT_EQ(side_from_south->width, 7);
}

TEST(SpriteActor, SelectTextureFallsBackWhenFrameNull)
{
    sdl3d_texture2d fallback{};
    fallback.width = 99;

    sdl3d_sprite_rotation_set rot{};
    /* All frames NULL. */

    sdl3d_sprite_actor actor{};
    actor.position = sdl3d_vec3_make(0, 0, 0);
    actor.texture = &fallback;
    actor.rotations = &rot;

    const sdl3d_texture2d *result = sdl3d_sprite_select_texture(&actor, 0.0f, -10.0f);
    EXPECT_EQ(result, &fallback);
}

TEST(SpriteActor, SelectTextureUsesCurrentAnimationFrame)
{
    sdl3d_texture2d south_frame0{};
    sdl3d_texture2d south_frame1{};
    south_frame0.width = 10;
    south_frame1.width = 20;

    sdl3d_sprite_rotation_set frames[2]{};
    frames[0].frames[0] = &south_frame0;
    frames[1].frames[0] = &south_frame1;

    sdl3d_sprite_actor actor{};
    actor.position = sdl3d_vec3_make(0, 0, 0);
    sdl3d_sprite_actor_play_animation(&actor, frames, 2, 4.0f, true);
    actor.animation_time = 0.25f;

    const sdl3d_texture2d *result = sdl3d_sprite_select_texture(&actor, 0.0f, -10.0f);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->width, 20);
}

TEST(SpriteActor, StopAnimationReturnsToBaseRotation)
{
    sdl3d_texture2d base{};
    sdl3d_texture2d animated{};
    base.width = 10;
    animated.width = 20;

    sdl3d_sprite_rotation_set base_rotation{};
    sdl3d_sprite_rotation_set animation_frame{};
    base_rotation.frames[0] = &base;
    animation_frame.frames[0] = &animated;

    sdl3d_sprite_actor actor{};
    actor.position = sdl3d_vec3_make(0, 0, 0);
    actor.rotations = &base_rotation;
    sdl3d_sprite_actor_play_animation(&actor, &animation_frame, 1, 8.0f, true);
    sdl3d_sprite_actor_stop_animation(&actor);

    const sdl3d_texture2d *result = sdl3d_sprite_select_texture(&actor, 0.0f, -10.0f);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->width, 10);
}

/* ================================================================== */
/* Draw (NULL safety)                                                 */
/* ================================================================== */

TEST(SpriteScene, DrawNullArgsAreSafe)
{
    sdl3d_sprite_scene scene;
    sdl3d_sprite_scene_init(&scene);
    sdl3d_sprite_scene_draw(nullptr, nullptr, sdl3d_vec3_make(0, 0, 0), nullptr);
    sdl3d_sprite_scene_draw(&scene, nullptr, sdl3d_vec3_make(0, 0, 0), nullptr);
    sdl3d_sprite_scene_free(&scene);
}

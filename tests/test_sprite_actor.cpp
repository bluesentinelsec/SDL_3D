/*
 * Unit tests for slayer3d_sprite_actor / slayer3d_sprite_scene.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "slayer3d/math.h"
#include "slayer3d/sprite_actor.h"
}

static constexpr float SPRITE_TEST_PI = 3.14159265358979323846f;

static slayer3d_sector make_flat_test_sector(float floor_y, float ceil_y)
{
    slayer3d_sector sector{};
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
    slayer3d_sprite_scene scene;
    slayer3d_sprite_scene_init(&scene);
    EXPECT_EQ(scene.count, 0);
    EXPECT_EQ(scene.capacity, 0);
    slayer3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, FreeNullIsSafe)
{
    slayer3d_sprite_scene_free(nullptr);
}

TEST(SpriteScene, AddReturnsValidActor)
{
    slayer3d_sprite_scene scene;
    slayer3d_sprite_scene_init(&scene);

    slayer3d_sprite_actor *a = slayer3d_sprite_scene_add(&scene);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(scene.count, 1);
    EXPECT_TRUE(a->visible);
    EXPECT_EQ(a->sector_id, -1);
    EXPECT_EQ(a->tint.r, 255);
    EXPECT_EQ(a->tint.a, 255);
    EXPECT_FLOAT_EQ(a->visual_ground_offset, 0.0f);
    EXPECT_FLOAT_EQ(a->facing_yaw, 0.0f);

    slayer3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, AddNullReturnsNull)
{
    EXPECT_EQ(slayer3d_sprite_scene_add(nullptr), nullptr);
}

TEST(SpriteScene, RemoveSwapsWithLast)
{
    slayer3d_sprite_scene scene;
    slayer3d_sprite_scene_init(&scene);

    slayer3d_sprite_actor *a = slayer3d_sprite_scene_add(&scene);
    slayer3d_sprite_actor *b = slayer3d_sprite_scene_add(&scene);
    slayer3d_sprite_actor *c = slayer3d_sprite_scene_add(&scene);
    a->bob_speed = 1.0f;
    b->bob_speed = 2.0f;
    c->bob_speed = 3.0f;

    /* Remove index 0 — c should swap into slot 0. */
    slayer3d_sprite_scene_remove(&scene, 0);
    EXPECT_EQ(scene.count, 2);
    EXPECT_FLOAT_EQ(scene.actors[0].bob_speed, 3.0f);
    EXPECT_FLOAT_EQ(scene.actors[1].bob_speed, 2.0f);

    slayer3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, RemoveOutOfBoundsIsSafe)
{
    slayer3d_sprite_scene scene;
    slayer3d_sprite_scene_init(&scene);
    slayer3d_sprite_scene_remove(&scene, 0);
    slayer3d_sprite_scene_remove(&scene, -1);
    slayer3d_sprite_scene_remove(nullptr, 0);
    slayer3d_sprite_scene_free(&scene);
}

/* ================================================================== */
/* Update                                                             */
/* ================================================================== */

TEST(SpriteScene, UpdateAdvancesBobPhase)
{
    slayer3d_sprite_scene scene;
    slayer3d_sprite_scene_init(&scene);

    slayer3d_sprite_actor *a = slayer3d_sprite_scene_add(&scene);
    a->bob_amplitude = 0.1f;
    a->bob_speed = 2.0f;
    EXPECT_FLOAT_EQ(a->bob_phase, 0.0f);

    slayer3d_sprite_scene_update(&scene, 0.5f);
    EXPECT_FLOAT_EQ(scene.actors[0].bob_phase, 0.5f);

    slayer3d_sprite_scene_update(&scene, 0.25f);
    EXPECT_FLOAT_EQ(scene.actors[0].bob_phase, 0.75f);

    slayer3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, UpdateAdvancesLoopingAnimation)
{
    slayer3d_sprite_scene scene;
    slayer3d_sprite_scene_init(&scene);

    slayer3d_sprite_rotation_set frames[3]{};
    slayer3d_sprite_actor *a = slayer3d_sprite_scene_add(&scene);
    ASSERT_NE(a, nullptr);
    slayer3d_sprite_actor_play_animation(a, frames, 3, 2.0f, true);

    slayer3d_sprite_scene_update(&scene, 0.50f);
    EXPECT_EQ(slayer3d_sprite_actor_current_animation_frame(&scene.actors[0]), 1);

    slayer3d_sprite_scene_update(&scene, 1.00f);
    EXPECT_EQ(slayer3d_sprite_actor_current_animation_frame(&scene.actors[0]), 0);

    slayer3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, UpdateClampsNonLoopingAnimation)
{
    slayer3d_sprite_scene scene;
    slayer3d_sprite_scene_init(&scene);

    slayer3d_sprite_rotation_set frames[3]{};
    slayer3d_sprite_actor *a = slayer3d_sprite_scene_add(&scene);
    ASSERT_NE(a, nullptr);
    slayer3d_sprite_actor_play_animation(a, frames, 3, 10.0f, false);

    slayer3d_sprite_scene_update(&scene, 1.0f);
    EXPECT_EQ(slayer3d_sprite_actor_current_animation_frame(&scene.actors[0]), 2);

    slayer3d_sprite_scene_free(&scene);
}

TEST(SpriteScene, UpdateNullIsSafe)
{
    slayer3d_sprite_scene_update(nullptr, 1.0f);
}

TEST(SpriteActor, DrawPositionAppliesVisualGroundOffset)
{
    slayer3d_sprite_actor actor{};
    actor.position = slayer3d_vec3_make(2.0f, 3.0f, 4.0f);
    actor.visual_ground_offset = 0.5f;

    slayer3d_vec3 pos = slayer3d_sprite_actor_draw_position(&actor);
    EXPECT_FLOAT_EQ(pos.x, 2.0f);
    EXPECT_FLOAT_EQ(pos.y, 2.5f);
    EXPECT_FLOAT_EQ(pos.z, 4.0f);
}

TEST(SpriteActor, DrawPositionNullReturnsOrigin)
{
    slayer3d_vec3 pos = slayer3d_sprite_actor_draw_position(nullptr);
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(pos.z, 0.0f);
}

TEST(SpriteActor, SetFacingDirectionUsesWorldXZYaw)
{
    slayer3d_sprite_actor actor{};

    slayer3d_sprite_actor_set_facing_direction(&actor, 1.0f, 0.0f);
    EXPECT_NEAR(actor.facing_yaw, SPRITE_TEST_PI * 0.5f, 1e-5f);

    slayer3d_sprite_actor_set_facing_direction(&actor, 0.0f, -1.0f);
    EXPECT_NEAR(actor.facing_yaw, 0.0f, 1e-5f);

    slayer3d_sprite_actor_set_facing_direction(&actor, 0.0f, 0.0f);
    EXPECT_NEAR(actor.facing_yaw, 0.0f, 1e-5f);
}

TEST(SpriteActor, SetFacingYawWrapsToSignedRange)
{
    slayer3d_sprite_actor actor{};
    slayer3d_sprite_actor_set_facing_yaw(&actor, SPRITE_TEST_PI * 2.5f);

    EXPECT_NEAR(actor.facing_yaw, SPRITE_TEST_PI * 0.5f, 1e-5f);
}

TEST(SpriteActor, CanStandAtReturnsFloorHeight)
{
    slayer3d_level level{};
    slayer3d_sector sector = make_flat_test_sector(2.0f, 6.0f);
    level.sector_count = 1;

    slayer3d_sprite_actor actor{};
    actor.position = slayer3d_vec3_make(1.0f, 2.0f, 1.0f);

    float floor_y = 0.0f;
    EXPECT_TRUE(slayer3d_sprite_actor_can_stand_at(&actor, &level, &sector, 5.0f, 5.0f, 0.5f, 1.8f, &floor_y));
    EXPECT_FLOAT_EQ(floor_y, 2.0f);
}

TEST(SpriteActor, CanStandAtRejectsOutsideLevelAndTooHighStep)
{
    slayer3d_level level{};
    slayer3d_sector sector = make_flat_test_sector(2.0f, 6.0f);
    level.sector_count = 1;

    slayer3d_sprite_actor actor{};
    actor.position = slayer3d_vec3_make(1.0f, 0.0f, 1.0f);

    EXPECT_FALSE(slayer3d_sprite_actor_can_stand_at(&actor, &level, &sector, 12.0f, 5.0f, 0.5f, 1.8f, nullptr));
    EXPECT_FALSE(slayer3d_sprite_actor_can_stand_at(&actor, &level, &sector, 5.0f, 5.0f, 0.5f, 1.8f, nullptr));
}

TEST(SpriteActor, SnapToGroundUsesHighestSupportUnderProbe)
{
    slayer3d_level level{};
    slayer3d_sector sector = make_flat_test_sector(2.0f, 6.0f);
    level.sector_count = 1;

    slayer3d_sprite_actor actor{};
    actor.position = slayer3d_vec3_make(5.0f, 2.4f, 5.0f);

    EXPECT_TRUE(slayer3d_sprite_actor_snap_to_ground(&actor, &level, &sector, 0.5f, 1.8f));
    EXPECT_FLOAT_EQ(actor.position.y, 2.0f);
}

/* ================================================================== */
/* Texture selection                                                  */
/* ================================================================== */

TEST(SpriteActor, SelectTextureNoRotationsReturnsSingle)
{
    slayer3d_texture2d tex{};
    tex.width = 32;
    slayer3d_sprite_actor actor{};
    actor.texture = &tex;
    actor.rotations = nullptr;

    const slayer3d_texture2d *result = slayer3d_sprite_select_texture(&actor, 10.0f, 10.0f);
    EXPECT_EQ(result, &tex);
}

TEST(SpriteActor, SelectTextureNullActorReturnsNull)
{
    EXPECT_EQ(slayer3d_sprite_select_texture(nullptr, 0, 0), nullptr);
}

TEST(SpriteActor, SelectTextureSouthWhenCameraSouth)
{
    slayer3d_texture2d frames[SLAYER3D_SPRITE_ROTATION_COUNT]{};
    for (int i = 0; i < SLAYER3D_SPRITE_ROTATION_COUNT; ++i)
        frames[i].width = i + 1; /* use width as a tag */

    slayer3d_sprite_rotation_set rot{};
    for (int i = 0; i < SLAYER3D_SPRITE_ROTATION_COUNT; ++i)
        rot.frames[i] = &frames[i];

    slayer3d_sprite_actor actor{};
    actor.position = slayer3d_vec3_make(0, 0, 0);
    actor.rotations = &rot;

    /* Camera directly south of actor (negative Z). */
    const slayer3d_texture2d *result = slayer3d_sprite_select_texture(&actor, 0.0f, -10.0f);
    /* Should select frame[0] = south. */
    EXPECT_EQ(result->width, 1);
}

TEST(SpriteActor, SelectTextureNorthWhenCameraNorth)
{
    slayer3d_texture2d frames[SLAYER3D_SPRITE_ROTATION_COUNT]{};
    for (int i = 0; i < SLAYER3D_SPRITE_ROTATION_COUNT; ++i)
        frames[i].width = i + 1;

    slayer3d_sprite_rotation_set rot{};
    for (int i = 0; i < SLAYER3D_SPRITE_ROTATION_COUNT; ++i)
        rot.frames[i] = &frames[i];

    slayer3d_sprite_actor actor{};
    actor.position = slayer3d_vec3_make(0, 0, 0);
    actor.rotations = &rot;

    /* Camera directly north of actor (positive Z). */
    const slayer3d_texture2d *result = slayer3d_sprite_select_texture(&actor, 0.0f, 10.0f);
    /* Should select frame[4] = north. */
    EXPECT_EQ(result->width, 5);
}

TEST(SpriteActor, SelectTextureUsesActorFacingYaw)
{
    slayer3d_texture2d frames[SLAYER3D_SPRITE_ROTATION_COUNT]{};
    for (int i = 0; i < SLAYER3D_SPRITE_ROTATION_COUNT; ++i)
        frames[i].width = i + 1;

    slayer3d_sprite_rotation_set rot{};
    for (int i = 0; i < SLAYER3D_SPRITE_ROTATION_COUNT; ++i)
        rot.frames[i] = &frames[i];

    slayer3d_sprite_actor actor{};
    actor.position = slayer3d_vec3_make(0, 0, 0);
    actor.rotations = &rot;

    slayer3d_sprite_actor_set_facing_direction(&actor, 1.0f, 0.0f);
    const slayer3d_texture2d *front_from_east = slayer3d_sprite_select_texture(&actor, 10.0f, 0.0f);
    ASSERT_NE(front_from_east, nullptr);
    EXPECT_EQ(front_from_east->width, 1);

    const slayer3d_texture2d *side_from_south = slayer3d_sprite_select_texture(&actor, 0.0f, -10.0f);
    ASSERT_NE(side_from_south, nullptr);
    EXPECT_EQ(side_from_south->width, 7);
}

TEST(SpriteActor, SelectTextureFallsBackWhenFrameNull)
{
    slayer3d_texture2d fallback{};
    fallback.width = 99;

    slayer3d_sprite_rotation_set rot{};
    /* All frames NULL. */

    slayer3d_sprite_actor actor{};
    actor.position = slayer3d_vec3_make(0, 0, 0);
    actor.texture = &fallback;
    actor.rotations = &rot;

    const slayer3d_texture2d *result = slayer3d_sprite_select_texture(&actor, 0.0f, -10.0f);
    EXPECT_EQ(result, &fallback);
}

TEST(SpriteActor, SelectTextureUsesCurrentAnimationFrame)
{
    slayer3d_texture2d south_frame0{};
    slayer3d_texture2d south_frame1{};
    south_frame0.width = 10;
    south_frame1.width = 20;

    slayer3d_sprite_rotation_set frames[2]{};
    frames[0].frames[0] = &south_frame0;
    frames[1].frames[0] = &south_frame1;

    slayer3d_sprite_actor actor{};
    actor.position = slayer3d_vec3_make(0, 0, 0);
    slayer3d_sprite_actor_play_animation(&actor, frames, 2, 4.0f, true);
    actor.animation_time = 0.25f;

    const slayer3d_texture2d *result = slayer3d_sprite_select_texture(&actor, 0.0f, -10.0f);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->width, 20);
}

TEST(SpriteActor, StopAnimationReturnsToBaseRotation)
{
    slayer3d_texture2d base{};
    slayer3d_texture2d animated{};
    base.width = 10;
    animated.width = 20;

    slayer3d_sprite_rotation_set base_rotation{};
    slayer3d_sprite_rotation_set animation_frame{};
    base_rotation.frames[0] = &base;
    animation_frame.frames[0] = &animated;

    slayer3d_sprite_actor actor{};
    actor.position = slayer3d_vec3_make(0, 0, 0);
    actor.rotations = &base_rotation;
    slayer3d_sprite_actor_play_animation(&actor, &animation_frame, 1, 8.0f, true);
    slayer3d_sprite_actor_stop_animation(&actor);

    const slayer3d_texture2d *result = slayer3d_sprite_select_texture(&actor, 0.0f, -10.0f);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->width, 10);
}

/* ================================================================== */
/* Draw (NULL safety)                                                 */
/* ================================================================== */

TEST(SpriteScene, DrawNullArgsAreSafe)
{
    slayer3d_sprite_scene scene;
    slayer3d_sprite_scene_init(&scene);
    slayer3d_sprite_scene_draw(nullptr, nullptr, slayer3d_vec3_make(0, 0, 0), nullptr);
    slayer3d_sprite_scene_draw(&scene, nullptr, slayer3d_vec3_make(0, 0, 0), nullptr);
    slayer3d_sprite_scene_free(&scene);
}

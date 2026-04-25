/*
 * Unit tests for sdl3d_sprite_actor / sdl3d_sprite_scene.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/math.h"
#include "sdl3d/sprite_actor.h"
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

TEST(SpriteScene, UpdateNullIsSafe)
{
    sdl3d_sprite_scene_update(nullptr, 1.0f);
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

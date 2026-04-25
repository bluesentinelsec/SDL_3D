/*
 * Tests for the scene graph and actor system.
 */

#include <gtest/gtest.h>

#include <cstring>

extern "C"
{
#include "sdl3d/math.h"
#include "sdl3d/scene.h"
}

/* ================================================================== */
/* Scene lifecycle                                                    */
/* ================================================================== */

TEST(SDL3DScene, CreateAndDestroy)
{
    sdl3d_scene *scene = sdl3d_create_scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(sdl3d_scene_get_actor_count(scene), 0);
    sdl3d_destroy_scene(scene);
}

TEST(SDL3DScene, DestroyNullIsSafe)
{
    sdl3d_destroy_scene(nullptr);
}

TEST(SDL3DScene, DestroyWithActors)
{
    sdl3d_model model{};
    model.mesh_count = 0;
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_scene_add_actor(scene, &model);
    sdl3d_scene_add_actor(scene, &model);
    EXPECT_EQ(sdl3d_scene_get_actor_count(scene), 2);
    sdl3d_destroy_scene(scene); /* Must free all actors. */
}

/* ================================================================== */
/* Actor management                                                   */
/* ================================================================== */

TEST(SDL3DScene, AddActorReturnsHandle)
{
    sdl3d_model model{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(sdl3d_scene_get_actor_count(scene), 1);
    EXPECT_EQ(sdl3d_actor_get_model(actor), &model);
    sdl3d_destroy_scene(scene);
}

TEST(SDL3DScene, AddMultipleActors)
{
    sdl3d_model m1{}, m2{}, m3{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *a1 = sdl3d_scene_add_actor(scene, &m1);
    sdl3d_actor *a2 = sdl3d_scene_add_actor(scene, &m2);
    sdl3d_actor *a3 = sdl3d_scene_add_actor(scene, &m3);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(a3, nullptr);
    EXPECT_EQ(sdl3d_scene_get_actor_count(scene), 3);
    sdl3d_destroy_scene(scene);
}

TEST(SDL3DScene, RemoveActor)
{
    sdl3d_model model{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *a1 = sdl3d_scene_add_actor(scene, &model);
    sdl3d_actor *a2 = sdl3d_scene_add_actor(scene, &model);
    EXPECT_EQ(sdl3d_scene_get_actor_count(scene), 2);

    sdl3d_scene_remove_actor(scene, a1);
    EXPECT_EQ(sdl3d_scene_get_actor_count(scene), 1);

    sdl3d_scene_remove_actor(scene, a2);
    EXPECT_EQ(sdl3d_scene_get_actor_count(scene), 0);

    sdl3d_destroy_scene(scene);
}

TEST(SDL3DScene, RemoveMiddleActor)
{
    sdl3d_model model{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_scene_add_actor(scene, &model);
    sdl3d_actor *mid = sdl3d_scene_add_actor(scene, &model);
    sdl3d_scene_add_actor(scene, &model);
    EXPECT_EQ(sdl3d_scene_get_actor_count(scene), 3);

    sdl3d_scene_remove_actor(scene, mid);
    EXPECT_EQ(sdl3d_scene_get_actor_count(scene), 2);

    sdl3d_destroy_scene(scene);
}

TEST(SDL3DScene, RemoveNullIsSafe)
{
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_scene_remove_actor(scene, nullptr);
    sdl3d_scene_remove_actor(nullptr, nullptr);
    EXPECT_EQ(sdl3d_scene_get_actor_count(scene), 0);
    sdl3d_destroy_scene(scene);
}

/* ================================================================== */
/* Null rejection                                                     */
/* ================================================================== */

TEST(SDL3DScene, AddActorNullSceneReturnsNull)
{
    sdl3d_model model{};
    EXPECT_EQ(sdl3d_scene_add_actor(nullptr, &model), nullptr);
}

TEST(SDL3DScene, AddActorNullModelReturnsNull)
{
    sdl3d_scene *scene = sdl3d_create_scene();
    EXPECT_EQ(sdl3d_scene_add_actor(scene, nullptr), nullptr);
    sdl3d_destroy_scene(scene);
}

TEST(SDL3DScene, GetActorCountNullReturnsZero)
{
    EXPECT_EQ(sdl3d_scene_get_actor_count(nullptr), 0);
}

TEST(SDL3DScene, DrawSceneNullContextFails)
{
    sdl3d_scene *scene = sdl3d_create_scene();
    EXPECT_FALSE(sdl3d_draw_scene(nullptr, scene));
    sdl3d_destroy_scene(scene);
}

TEST(SDL3DScene, DrawSceneNullSceneFails)
{
    EXPECT_FALSE(sdl3d_draw_scene(nullptr, nullptr));
}

TEST(SDL3DScene, DrawSceneWithVisibilityRejectsNullArgs)
{
    sdl3d_visibility_result vis{};
    bool sectors[1] = {true};
    vis.sector_visible = sectors;
    vis.visible_count = 1;

    EXPECT_FALSE(sdl3d_draw_scene_with_visibility(nullptr, nullptr, &vis));

    sdl3d_scene *scene = sdl3d_create_scene();
    EXPECT_FALSE(sdl3d_draw_scene_with_visibility(nullptr, scene, &vis));
    sdl3d_destroy_scene(scene);
}

/* ================================================================== */
/* Actor properties — defaults                                        */
/* ================================================================== */

TEST(SDL3DActor, DefaultProperties)
{
    sdl3d_model model{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);

    sdl3d_vec3 pos = sdl3d_actor_get_position(actor);
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(pos.z, 0.0f);

    sdl3d_vec3 scale = sdl3d_actor_get_scale(actor);
    EXPECT_FLOAT_EQ(scale.x, 1.0f);
    EXPECT_FLOAT_EQ(scale.y, 1.0f);
    EXPECT_FLOAT_EQ(scale.z, 1.0f);

    EXPECT_TRUE(sdl3d_actor_is_visible(actor));
    EXPECT_EQ(sdl3d_actor_get_model(actor), &model);
    EXPECT_EQ(sdl3d_actor_get_sector(actor), -1);

    sdl3d_destroy_scene(scene);
}

TEST(SDL3DActor, SectorSetAndGet)
{
    sdl3d_model model{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);

    sdl3d_actor_set_sector(actor, 7);
    EXPECT_EQ(sdl3d_actor_get_sector(actor), 7);

    sdl3d_actor_set_sector(actor, -1);
    EXPECT_EQ(sdl3d_actor_get_sector(actor), -1);

    /* NULL-safe accessors. */
    sdl3d_actor_set_sector(nullptr, 3);
    EXPECT_EQ(sdl3d_actor_get_sector(nullptr), -1);

    sdl3d_destroy_scene(scene);
}

/* ================================================================== */
/* Actor properties — set/get                                         */
/* ================================================================== */

struct PositionCase
{
    const char *label;
    float x, y, z;
};

class SDL3DActorPosition : public ::testing::TestWithParam<PositionCase>
{
};

TEST_P(SDL3DActorPosition, SetAndGet)
{
    const auto &c = GetParam();
    sdl3d_model model{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);

    sdl3d_actor_set_position(actor, sdl3d_vec3_make(c.x, c.y, c.z));
    sdl3d_vec3 pos = sdl3d_actor_get_position(actor);
    EXPECT_FLOAT_EQ(pos.x, c.x) << c.label;
    EXPECT_FLOAT_EQ(pos.y, c.y) << c.label;
    EXPECT_FLOAT_EQ(pos.z, c.z) << c.label;

    sdl3d_destroy_scene(scene);
}

INSTANTIATE_TEST_SUITE_P(Actor, SDL3DActorPosition,
                         ::testing::Values(PositionCase{"origin", 0, 0, 0}, PositionCase{"positive", 1, 2, 3},
                                           PositionCase{"negative", -5, -10, -15},
                                           PositionCase{"large", 1000, 2000, 3000},
                                           PositionCase{"fractional", 0.5f, 0.25f, 0.125f}));

TEST(SDL3DActor, SetScale)
{
    sdl3d_model model{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);

    sdl3d_actor_set_scale(actor, sdl3d_vec3_make(2.0f, 3.0f, 4.0f));
    sdl3d_vec3 s = sdl3d_actor_get_scale(actor);
    EXPECT_FLOAT_EQ(s.x, 2.0f);
    EXPECT_FLOAT_EQ(s.y, 3.0f);
    EXPECT_FLOAT_EQ(s.z, 4.0f);

    sdl3d_destroy_scene(scene);
}

TEST(SDL3DActor, SetVisibility)
{
    sdl3d_model model{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);

    EXPECT_TRUE(sdl3d_actor_is_visible(actor));
    sdl3d_actor_set_visible(actor, false);
    EXPECT_FALSE(sdl3d_actor_is_visible(actor));
    sdl3d_actor_set_visible(actor, true);
    EXPECT_TRUE(sdl3d_actor_is_visible(actor));

    sdl3d_destroy_scene(scene);
}

TEST(SDL3DActor, SetTint)
{
    sdl3d_model model{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);

    sdl3d_color red = {255, 0, 0, 255};
    sdl3d_actor_set_tint(actor, red);
    /* No getter for tint — just verify it doesn't crash. */

    sdl3d_destroy_scene(scene);
}

/* ================================================================== */
/* Null actor property access                                         */
/* ================================================================== */

TEST(SDL3DActor, NullActorPropertyAccessIsSafe)
{
    sdl3d_actor_set_position(nullptr, sdl3d_vec3_make(1, 2, 3));
    sdl3d_actor_set_rotation(nullptr, sdl3d_vec3_make(0, 1, 0), 1.0f);
    sdl3d_actor_set_scale(nullptr, sdl3d_vec3_make(1, 1, 1));
    sdl3d_actor_set_visible(nullptr, true);
    sdl3d_actor_set_tint(nullptr, (sdl3d_color){255, 255, 255, 255});

    sdl3d_vec3 pos = sdl3d_actor_get_position(nullptr);
    EXPECT_FLOAT_EQ(pos.x, 0.0f);

    sdl3d_vec3 scale = sdl3d_actor_get_scale(nullptr);
    EXPECT_FLOAT_EQ(scale.x, 1.0f);

    EXPECT_FALSE(sdl3d_actor_is_visible(nullptr));
    EXPECT_EQ(sdl3d_actor_get_model(nullptr), nullptr);
}

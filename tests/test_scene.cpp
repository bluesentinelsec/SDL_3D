/*
 * Tests for the scene graph and actor system.
 */

#include <gtest/gtest.h>

#include <cstring>

extern "C"
{
#include "slayer3d/math.h"
#include "slayer3d/scene.h"
}

/* ================================================================== */
/* Scene lifecycle                                                    */
/* ================================================================== */

TEST(SLAYER3DScene, CreateAndDestroy)
{
    slayer3d_scene *scene = slayer3d_create_scene();
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(slayer3d_scene_get_actor_count(scene), 0);
    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DScene, DestroyNullIsSafe)
{
    slayer3d_destroy_scene(nullptr);
}

TEST(SLAYER3DScene, DestroyWithActors)
{
    slayer3d_model model{};
    model.mesh_count = 0;
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_scene_add_actor(scene, &model);
    slayer3d_scene_add_actor(scene, &model);
    EXPECT_EQ(slayer3d_scene_get_actor_count(scene), 2);
    slayer3d_destroy_scene(scene); /* Must free all actors. */
}

/* ================================================================== */
/* Actor management                                                   */
/* ================================================================== */

TEST(SLAYER3DScene, AddActorReturnsHandle)
{
    slayer3d_model model{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *actor = slayer3d_scene_add_actor(scene, &model);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(slayer3d_scene_get_actor_count(scene), 1);
    EXPECT_EQ(slayer3d_actor_get_model(actor), &model);
    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DScene, AddMultipleActors)
{
    slayer3d_model m1{}, m2{}, m3{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *a1 = slayer3d_scene_add_actor(scene, &m1);
    slayer3d_actor *a2 = slayer3d_scene_add_actor(scene, &m2);
    slayer3d_actor *a3 = slayer3d_scene_add_actor(scene, &m3);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(a3, nullptr);
    EXPECT_EQ(slayer3d_scene_get_actor_count(scene), 3);
    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DScene, RemoveActor)
{
    slayer3d_model model{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *a1 = slayer3d_scene_add_actor(scene, &model);
    slayer3d_actor *a2 = slayer3d_scene_add_actor(scene, &model);
    EXPECT_EQ(slayer3d_scene_get_actor_count(scene), 2);

    slayer3d_scene_remove_actor(scene, a1);
    EXPECT_EQ(slayer3d_scene_get_actor_count(scene), 1);

    slayer3d_scene_remove_actor(scene, a2);
    EXPECT_EQ(slayer3d_scene_get_actor_count(scene), 0);

    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DScene, RemoveMiddleActor)
{
    slayer3d_model model{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_scene_add_actor(scene, &model);
    slayer3d_actor *mid = slayer3d_scene_add_actor(scene, &model);
    slayer3d_scene_add_actor(scene, &model);
    EXPECT_EQ(slayer3d_scene_get_actor_count(scene), 3);

    slayer3d_scene_remove_actor(scene, mid);
    EXPECT_EQ(slayer3d_scene_get_actor_count(scene), 2);

    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DScene, RemoveNullIsSafe)
{
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_scene_remove_actor(scene, nullptr);
    slayer3d_scene_remove_actor(nullptr, nullptr);
    EXPECT_EQ(slayer3d_scene_get_actor_count(scene), 0);
    slayer3d_destroy_scene(scene);
}

/* ================================================================== */
/* Null rejection                                                     */
/* ================================================================== */

TEST(SLAYER3DScene, AddActorNullSceneReturnsNull)
{
    slayer3d_model model{};
    EXPECT_EQ(slayer3d_scene_add_actor(nullptr, &model), nullptr);
}

TEST(SLAYER3DScene, AddActorNullModelReturnsNull)
{
    slayer3d_scene *scene = slayer3d_create_scene();
    EXPECT_EQ(slayer3d_scene_add_actor(scene, nullptr), nullptr);
    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DScene, GetActorCountNullReturnsZero)
{
    EXPECT_EQ(slayer3d_scene_get_actor_count(nullptr), 0);
}

TEST(SLAYER3DScene, DrawSceneNullContextFails)
{
    slayer3d_scene *scene = slayer3d_create_scene();
    EXPECT_FALSE(slayer3d_draw_scene(nullptr, scene));
    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DScene, DrawSceneNullSceneFails)
{
    EXPECT_FALSE(slayer3d_draw_scene(nullptr, nullptr));
}

TEST(SLAYER3DScene, DrawSceneWithVisibilityRejectsNullArgs)
{
    slayer3d_visibility_result vis{};
    bool sectors[1] = {true};
    vis.sector_visible = sectors;
    vis.visible_count = 1;

    EXPECT_FALSE(slayer3d_draw_scene_with_visibility(nullptr, nullptr, &vis));

    slayer3d_scene *scene = slayer3d_create_scene();
    EXPECT_FALSE(slayer3d_draw_scene_with_visibility(nullptr, scene, &vis));
    slayer3d_destroy_scene(scene);
}

/* ================================================================== */
/* Actor properties — defaults                                        */
/* ================================================================== */

TEST(SLAYER3DActor, DefaultProperties)
{
    slayer3d_model model{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *actor = slayer3d_scene_add_actor(scene, &model);

    slayer3d_vec3 pos = slayer3d_actor_get_position(actor);
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(pos.z, 0.0f);

    slayer3d_vec3 scale = slayer3d_actor_get_scale(actor);
    EXPECT_FLOAT_EQ(scale.x, 1.0f);
    EXPECT_FLOAT_EQ(scale.y, 1.0f);
    EXPECT_FLOAT_EQ(scale.z, 1.0f);

    EXPECT_TRUE(slayer3d_actor_is_visible(actor));
    EXPECT_EQ(slayer3d_actor_get_model(actor), &model);
    EXPECT_EQ(slayer3d_actor_get_sector(actor), -1);

    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DActor, SectorSetAndGet)
{
    slayer3d_model model{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *actor = slayer3d_scene_add_actor(scene, &model);

    slayer3d_actor_set_sector(actor, 7);
    EXPECT_EQ(slayer3d_actor_get_sector(actor), 7);

    slayer3d_actor_set_sector(actor, -1);
    EXPECT_EQ(slayer3d_actor_get_sector(actor), -1);

    /* NULL-safe accessors. */
    slayer3d_actor_set_sector(nullptr, 3);
    EXPECT_EQ(slayer3d_actor_get_sector(nullptr), -1);

    slayer3d_destroy_scene(scene);
}

/* ================================================================== */
/* Actor properties — set/get                                         */
/* ================================================================== */

struct PositionCase
{
    const char *label;
    float x, y, z;
};

class SLAYER3DActorPosition : public ::testing::TestWithParam<PositionCase>
{
};

TEST_P(SLAYER3DActorPosition, SetAndGet)
{
    const auto &c = GetParam();
    slayer3d_model model{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *actor = slayer3d_scene_add_actor(scene, &model);

    slayer3d_actor_set_position(actor, slayer3d_vec3_make(c.x, c.y, c.z));
    slayer3d_vec3 pos = slayer3d_actor_get_position(actor);
    EXPECT_FLOAT_EQ(pos.x, c.x) << c.label;
    EXPECT_FLOAT_EQ(pos.y, c.y) << c.label;
    EXPECT_FLOAT_EQ(pos.z, c.z) << c.label;

    slayer3d_destroy_scene(scene);
}

INSTANTIATE_TEST_SUITE_P(Actor, SLAYER3DActorPosition,
                         ::testing::Values(PositionCase{"origin", 0, 0, 0}, PositionCase{"positive", 1, 2, 3},
                                           PositionCase{"negative", -5, -10, -15},
                                           PositionCase{"large", 1000, 2000, 3000},
                                           PositionCase{"fractional", 0.5f, 0.25f, 0.125f}));

TEST(SLAYER3DActor, SetScale)
{
    slayer3d_model model{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *actor = slayer3d_scene_add_actor(scene, &model);

    slayer3d_actor_set_scale(actor, slayer3d_vec3_make(2.0f, 3.0f, 4.0f));
    slayer3d_vec3 s = slayer3d_actor_get_scale(actor);
    EXPECT_FLOAT_EQ(s.x, 2.0f);
    EXPECT_FLOAT_EQ(s.y, 3.0f);
    EXPECT_FLOAT_EQ(s.z, 4.0f);

    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DActor, SetVisibility)
{
    slayer3d_model model{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *actor = slayer3d_scene_add_actor(scene, &model);

    EXPECT_TRUE(slayer3d_actor_is_visible(actor));
    slayer3d_actor_set_visible(actor, false);
    EXPECT_FALSE(slayer3d_actor_is_visible(actor));
    slayer3d_actor_set_visible(actor, true);
    EXPECT_TRUE(slayer3d_actor_is_visible(actor));

    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DActor, SetTint)
{
    slayer3d_model model{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *actor = slayer3d_scene_add_actor(scene, &model);

    slayer3d_color red = {255, 0, 0, 255};
    slayer3d_actor_set_tint(actor, red);
    /* No getter for tint — just verify it doesn't crash. */

    slayer3d_destroy_scene(scene);
}

/* ================================================================== */
/* Null actor property access                                         */
/* ================================================================== */

TEST(SLAYER3DActor, NullActorPropertyAccessIsSafe)
{
    slayer3d_actor_set_position(nullptr, slayer3d_vec3_make(1, 2, 3));
    slayer3d_actor_set_rotation(nullptr, slayer3d_vec3_make(0, 1, 0), 1.0f);
    slayer3d_actor_set_scale(nullptr, slayer3d_vec3_make(1, 1, 1));
    slayer3d_actor_set_visible(nullptr, true);
    slayer3d_actor_set_tint(nullptr, (slayer3d_color){255, 255, 255, 255});

    slayer3d_vec3 pos = slayer3d_actor_get_position(nullptr);
    EXPECT_FLOAT_EQ(pos.x, 0.0f);

    slayer3d_vec3 scale = slayer3d_actor_get_scale(nullptr);
    EXPECT_FLOAT_EQ(scale.x, 1.0f);

    EXPECT_FALSE(slayer3d_actor_is_visible(nullptr));
    EXPECT_EQ(slayer3d_actor_get_model(nullptr), nullptr);
}

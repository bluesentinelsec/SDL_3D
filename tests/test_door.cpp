#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/door.h"
}

static sdl3d_door_desc make_split_door_desc()
{
    sdl3d_door_desc desc{};
    desc.door_id = 7;
    desc.name = "nukage_north";
    desc.panel_count = 2;
    desc.panels[0].closed_bounds = {sdl3d_vec3{3.0f, 0.0f, 16.0f}, sdl3d_vec3{5.0f, 3.0f, 16.25f}};
    desc.panels[0].open_offset = sdl3d_vec3{-2.0f, 0.0f, 0.0f};
    desc.panels[1].closed_bounds = {sdl3d_vec3{5.0f, 0.0f, 16.0f}, sdl3d_vec3{7.0f, 3.0f, 16.25f}};
    desc.panels[1].open_offset = sdl3d_vec3{2.0f, 0.0f, 0.0f};
    desc.open_seconds = 2.0f;
    desc.close_seconds = 1.0f;
    return desc;
}

TEST(Door, InitializesClosedByDefault)
{
    sdl3d_door door{};
    sdl3d_door_desc desc = make_split_door_desc();
    sdl3d_door_init(&door, &desc);

    EXPECT_TRUE(door.enabled);
    EXPECT_EQ(sdl3d_door_panel_count(&door), 2);
    EXPECT_EQ(sdl3d_door_get_state(&door), SDL3D_DOOR_CLOSED);
    EXPECT_FLOAT_EQ(sdl3d_door_get_open_fraction(&door), 0.0f);
}

TEST(Door, AnimatesPanelsByOpenFraction)
{
    sdl3d_door door{};
    sdl3d_door_desc desc = make_split_door_desc();
    sdl3d_door_init(&door, &desc);

    ASSERT_TRUE(sdl3d_door_open(&door));
    sdl3d_door_update(&door, 1.0f);

    sdl3d_bounding_box left{};
    sdl3d_bounding_box right{};
    ASSERT_TRUE(sdl3d_door_get_panel_bounds(&door, 0, &left));
    ASSERT_TRUE(sdl3d_door_get_panel_bounds(&door, 1, &right));
    EXPECT_FLOAT_EQ(sdl3d_door_get_open_fraction(&door), 0.5f);
    EXPECT_FLOAT_EQ(left.min.x, 2.0f);
    EXPECT_FLOAT_EQ(left.max.x, 4.0f);
    EXPECT_FLOAT_EQ(right.min.x, 6.0f);
    EXPECT_FLOAT_EQ(right.max.x, 8.0f);

    sdl3d_door_update(&door, 1.0f);
    EXPECT_EQ(sdl3d_door_get_state(&door), SDL3D_DOOR_OPEN);
    EXPECT_FLOAT_EQ(sdl3d_door_get_open_fraction(&door), 1.0f);
}

TEST(Door, AutoClosesWhenHoldTimeExpires)
{
    sdl3d_door door{};
    sdl3d_door_desc desc = make_split_door_desc();
    desc.open_seconds = 0.0f;
    desc.close_seconds = 2.0f;
    desc.stay_open_seconds = 0.5f;
    sdl3d_door_init(&door, &desc);

    ASSERT_TRUE(sdl3d_door_open(&door));
    EXPECT_EQ(sdl3d_door_get_state(&door), SDL3D_DOOR_OPEN);

    sdl3d_door_update(&door, 0.5f);
    EXPECT_EQ(sdl3d_door_get_state(&door), SDL3D_DOOR_CLOSING);
    sdl3d_door_update(&door, 1.0f);
    EXPECT_FLOAT_EQ(sdl3d_door_get_open_fraction(&door), 0.5f);
    sdl3d_door_update(&door, 1.0f);
    EXPECT_EQ(sdl3d_door_get_state(&door), SDL3D_DOOR_CLOSED);
}

TEST(Door, AutoCloseDelayCanBeChangedAtRuntime)
{
    sdl3d_door door{};
    sdl3d_door_desc desc = make_split_door_desc();
    desc.open_seconds = 0.0f;
    desc.close_seconds = 1.0f;
    sdl3d_door_init(&door, &desc);

    sdl3d_door_set_auto_close_delay(&door, 5.0f);
    ASSERT_TRUE(sdl3d_door_open(&door));
    EXPECT_EQ(sdl3d_door_get_state(&door), SDL3D_DOOR_OPEN);

    sdl3d_door_update(&door, 4.9f);
    EXPECT_EQ(sdl3d_door_get_state(&door), SDL3D_DOOR_OPEN);
    sdl3d_door_update(&door, 0.1f);
    EXPECT_EQ(sdl3d_door_get_state(&door), SDL3D_DOOR_CLOSING);

    ASSERT_TRUE(sdl3d_door_open(&door));
    sdl3d_door_set_auto_close_delay(&door, 0.0f);
    sdl3d_door_update(&door, 10.0f);
    EXPECT_EQ(sdl3d_door_get_state(&door), SDL3D_DOOR_OPEN);
}

TEST(Door, InteractionRangeUsesClosedDoorway)
{
    sdl3d_door door{};
    sdl3d_door_desc desc = make_split_door_desc();
    desc.open_seconds = 0.0f;
    sdl3d_door_init(&door, &desc);
    ASSERT_TRUE(sdl3d_door_open(&door));

    EXPECT_TRUE(sdl3d_door_point_in_interaction_range(&door, sdl3d_vec3{5.0f, 1.0f, 14.5f}, 1.6f));
    EXPECT_FALSE(sdl3d_door_point_in_interaction_range(&door, sdl3d_vec3{5.0f, 1.0f, 12.0f}, 1.6f));
}

TEST(Door, ResolvesCylinderAgainstClosedPanel)
{
    sdl3d_door door{};
    sdl3d_door_desc desc = make_split_door_desc();
    sdl3d_door_init(&door, &desc);

    sdl3d_vec3 eye{4.0f, 1.8f, 15.95f};
    EXPECT_TRUE(sdl3d_door_intersects_cylinder(&door, eye, 1.8f, 0.3f));
    EXPECT_TRUE(sdl3d_door_resolve_cylinder(&door, &eye, 1.8f, 0.3f));
    EXPECT_FALSE(sdl3d_door_intersects_cylinder(&door, eye, 1.8f, 0.3f));
}

TEST(Door, VerticalSlidingPanelMovesOutOfCollision)
{
    sdl3d_door_desc desc{};
    desc.panel_count = 1;
    desc.panels[0].closed_bounds = {sdl3d_vec3{9.8f, 0.0f, 18.0f}, sdl3d_vec3{10.2f, 3.0f, 22.0f}};
    desc.panels[0].open_offset = sdl3d_vec3{0.0f, 4.0f, 0.0f};
    desc.open_seconds = 1.0f;

    sdl3d_door door{};
    sdl3d_door_init(&door, &desc);
    ASSERT_TRUE(sdl3d_door_open(&door));
    sdl3d_door_update(&door, 1.0f);

    sdl3d_vec3 eye{10.0f, 1.8f, 20.0f};
    EXPECT_FALSE(sdl3d_door_intersects_cylinder(&door, eye, 1.8f, 0.3f));
}

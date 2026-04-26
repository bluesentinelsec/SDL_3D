#include <gtest/gtest.h>

extern "C"
{
#include "surveillance.h"
}

static sdl3d_bounding_box bounds()
{
    return (sdl3d_bounding_box){sdl3d_vec3_make(-1.0f, 0.0f, -1.0f), sdl3d_vec3_make(1.0f, 2.0f, 1.0f)};
}

static sdl3d_camera3d camera()
{
    sdl3d_camera3d cam{};
    cam.position = sdl3d_vec3_make(4.0f, 3.0f, 25.0f);
    cam.target = sdl3d_vec3_make(4.0f, 0.0f, 18.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 70.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;
    return cam;
}

TEST(DoomSurveillance, InitStartsEnabledInactive)
{
    doom_surveillance_camera surveillance{};
    sdl3d_camera3d cam = camera();
    doom_surveillance_init(&surveillance, bounds(), cam);

    EXPECT_TRUE(surveillance.enabled);
    EXPECT_FALSE(doom_surveillance_is_active(&surveillance));
    EXPECT_EQ(nullptr, doom_surveillance_active_camera(&surveillance));
    EXPECT_FLOAT_EQ(cam.position.z, surveillance.camera.position.z);
}

TEST(DoomSurveillance, ActiveWhileSampleInsideButtonBounds)
{
    doom_surveillance_camera surveillance{};
    doom_surveillance_init(&surveillance, bounds(), camera());

    EXPECT_FALSE(doom_surveillance_update(&surveillance, sdl3d_vec3_make(2.0f, 1.0f, 0.0f)));
    EXPECT_FALSE(doom_surveillance_is_active(&surveillance));
    EXPECT_EQ(nullptr, doom_surveillance_active_camera(&surveillance));

    EXPECT_TRUE(doom_surveillance_update(&surveillance, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(doom_surveillance_is_active(&surveillance));
    ASSERT_NE(nullptr, doom_surveillance_active_camera(&surveillance));
    EXPECT_FLOAT_EQ(70.0f, doom_surveillance_active_camera(&surveillance)->fovy);

    EXPECT_FALSE(doom_surveillance_update(&surveillance, sdl3d_vec3_make(0.0f, 2.5f, 0.0f)));
    EXPECT_FALSE(doom_surveillance_is_active(&surveillance));
}

TEST(DoomSurveillance, DisabledCameraNeverActivates)
{
    doom_surveillance_camera surveillance{};
    doom_surveillance_init(&surveillance, bounds(), camera());
    surveillance.enabled = false;

    EXPECT_FALSE(doom_surveillance_update(&surveillance, sdl3d_vec3_make(0.0f, 1.0f, 0.0f)));
    EXPECT_FALSE(doom_surveillance_is_active(&surveillance));
}

TEST(DoomSurveillance, NullSafe)
{
    doom_surveillance_init(nullptr, bounds(), camera());
    EXPECT_FALSE(doom_surveillance_update(nullptr, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(doom_surveillance_is_active(nullptr));
    EXPECT_EQ(nullptr, doom_surveillance_active_camera(nullptr));
}

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

struct signal_capture
{
    int count = 0;
    int signal_id = 0;
    int event = 0;
    bool inside = false;
};

static void capture_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    signal_capture *capture = (signal_capture *)userdata;
    capture->count++;
    capture->signal_id = signal_id;
    capture->event = sdl3d_properties_get_int(payload, "event", 0);
    capture->inside = sdl3d_properties_get_bool(payload, "inside", false);
}

TEST(DoomSurveillance, InitStartsEnabledInactive)
{
    doom_surveillance_camera surveillance{};
    sdl3d_camera3d cam = camera();
    doom_surveillance_init(&surveillance, bounds(), cam, 100, 101);

    EXPECT_TRUE(surveillance.enabled);
    EXPECT_FALSE(doom_surveillance_is_active(&surveillance));
    EXPECT_EQ(nullptr, doom_surveillance_active_camera(&surveillance));
    EXPECT_FLOAT_EQ(cam.position.z, surveillance.camera.position.z);
}

TEST(DoomSurveillance, EmitsEnterAndExitSignals)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    signal_capture enter{};
    signal_capture exit{};
    ASSERT_GT(sdl3d_signal_connect(bus, 100, capture_signal, &enter), 0);
    ASSERT_GT(sdl3d_signal_connect(bus, 101, capture_signal, &exit), 0);
    doom_surveillance_camera surveillance{};
    doom_surveillance_init(&surveillance, bounds(), camera(), 100, 101);

    EXPECT_FALSE(doom_surveillance_update(&surveillance, world, sdl3d_vec3_make(2.0f, 1.0f, 0.0f)));
    EXPECT_FALSE(doom_surveillance_is_active(&surveillance));
    EXPECT_EQ(enter.count, 0);
    EXPECT_EQ(exit.count, 0);

    EXPECT_FALSE(doom_surveillance_update(&surveillance, world, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)));
    EXPECT_EQ(enter.count, 1);
    EXPECT_EQ(enter.signal_id, 100);
    EXPECT_EQ(enter.event, SDL3D_LOGIC_SENSOR_EVENT_ENTER);
    EXPECT_TRUE(enter.inside);
    EXPECT_EQ(exit.count, 0);

    surveillance.active = true;
    EXPECT_TRUE(doom_surveillance_is_active(&surveillance));
    ASSERT_NE(nullptr, doom_surveillance_active_camera(&surveillance));
    EXPECT_FLOAT_EQ(70.0f, doom_surveillance_active_camera(&surveillance)->fovy);

    EXPECT_TRUE(doom_surveillance_update(&surveillance, world, sdl3d_vec3_make(0.0f, 2.5f, 0.0f)));
    EXPECT_EQ(exit.count, 1);
    EXPECT_EQ(exit.signal_id, 101);
    EXPECT_EQ(exit.event, SDL3D_LOGIC_SENSOR_EVENT_EXIT);
    EXPECT_FALSE(exit.inside);

    surveillance.active = false;
    EXPECT_FALSE(doom_surveillance_is_active(&surveillance));

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(DoomSurveillance, DisabledCameraEmitsNoSignals)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_logic_world *world = sdl3d_logic_world_create(bus, nullptr);
    signal_capture capture{};
    ASSERT_GT(sdl3d_signal_connect(bus, 100, capture_signal, &capture), 0);
    doom_surveillance_camera surveillance{};
    doom_surveillance_init(&surveillance, bounds(), camera(), 100, 101);
    surveillance.enabled = false;

    EXPECT_FALSE(doom_surveillance_update(&surveillance, world, sdl3d_vec3_make(0.0f, 1.0f, 0.0f)));
    EXPECT_FALSE(doom_surveillance_is_active(&surveillance));
    EXPECT_EQ(capture.count, 0);

    sdl3d_logic_world_destroy(world);
    sdl3d_signal_bus_destroy(bus);
}

TEST(DoomSurveillance, NullSafe)
{
    doom_surveillance_init(nullptr, bounds(), camera(), 100, 101);
    EXPECT_FALSE(doom_surveillance_update(nullptr, nullptr, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(doom_surveillance_is_active(nullptr));
    EXPECT_EQ(nullptr, doom_surveillance_active_camera(nullptr));
}

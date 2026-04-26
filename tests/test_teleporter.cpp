#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/fps_mover.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/teleporter.h"
}

namespace
{
struct TeleportCapture
{
    int count = 0;
    int signal_id = 0;
    int teleporter_id = -1;
    sdl3d_teleport_destination destination{};
};

sdl3d_bounding_box make_bounds()
{
    return {sdl3d_vec3_make(-1.0f, 0.0f, -1.0f), sdl3d_vec3_make(1.0f, 2.0f, 1.0f)};
}

sdl3d_teleport_destination make_destination()
{
    sdl3d_teleport_destination destination{};
    destination.position = sdl3d_vec3_make(10.0f, 3.0f, 20.0f);
    destination.yaw = 1.5f;
    destination.pitch = -0.25f;
    destination.use_yaw = true;
    destination.use_pitch = true;
    return destination;
}

void capture_teleport(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    auto *capture = static_cast<TeleportCapture *>(userdata);
    capture->count++;
    capture->signal_id = signal_id;
    capture->teleporter_id = sdl3d_properties_get_int(payload, "teleporter_id", -1);
    EXPECT_TRUE(sdl3d_teleport_destination_from_payload(payload, &capture->destination));
}
} // namespace

TEST(SDL3DTeleporter, InitSetsExpectedDefaults)
{
    sdl3d_teleporter teleporter{};
    sdl3d_teleporter_init(&teleporter, 7, make_bounds(), make_destination());

    EXPECT_TRUE(teleporter.enabled);
    EXPECT_EQ(SDL3D_SIGNAL_TELEPORT, teleporter.signal_id);
    EXPECT_EQ(7, teleporter.teleporter_id);
    EXPECT_GT(teleporter.cooldown_seconds, 0.0f);
    EXPECT_FALSE(teleporter.was_inside);
}

TEST(SDL3DTeleporter, EmitsOnceWhenPointEnters)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    ASSERT_NE(nullptr, bus);

    TeleportCapture capture;
    ASSERT_NE(0, sdl3d_signal_connect(bus, SDL3D_SIGNAL_TELEPORT, capture_teleport, &capture));

    sdl3d_teleporter teleporter{};
    sdl3d_teleporter_init(&teleporter, 42, make_bounds(), make_destination());

    EXPECT_FALSE(sdl3d_teleporter_update(&teleporter, sdl3d_vec3_make(-2.0f, 1.0f, 0.0f), 0.016f, bus));
    EXPECT_TRUE(sdl3d_teleporter_update(&teleporter, sdl3d_vec3_make(0.0f, 1.0f, 0.0f), 0.016f, bus));
    EXPECT_FALSE(sdl3d_teleporter_update(&teleporter, sdl3d_vec3_make(0.25f, 1.0f, 0.0f), 0.016f, bus));

    EXPECT_EQ(1, capture.count);
    EXPECT_EQ(SDL3D_SIGNAL_TELEPORT, capture.signal_id);
    EXPECT_EQ(42, capture.teleporter_id);
    EXPECT_FLOAT_EQ(10.0f, capture.destination.position.x);
    EXPECT_FLOAT_EQ(3.0f, capture.destination.position.y);
    EXPECT_FLOAT_EQ(20.0f, capture.destination.position.z);
    EXPECT_FLOAT_EQ(1.5f, capture.destination.yaw);
    EXPECT_FLOAT_EQ(-0.25f, capture.destination.pitch);
    EXPECT_TRUE(capture.destination.use_yaw);
    EXPECT_TRUE(capture.destination.use_pitch);

    sdl3d_signal_bus_destroy(bus);
}

TEST(SDL3DTeleporter, CooldownBlocksImmediateReentry)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    ASSERT_NE(nullptr, bus);

    TeleportCapture capture;
    ASSERT_NE(0, sdl3d_signal_connect(bus, SDL3D_SIGNAL_TELEPORT, capture_teleport, &capture));

    sdl3d_teleporter teleporter{};
    sdl3d_teleporter_init(&teleporter, 1, make_bounds(), make_destination());
    teleporter.cooldown_seconds = 0.5f;

    EXPECT_TRUE(sdl3d_teleporter_update(&teleporter, sdl3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, bus));
    EXPECT_FALSE(sdl3d_teleporter_update(&teleporter, sdl3d_vec3_make(2.0f, 1.0f, 0.0f), 0.1f, bus));
    EXPECT_FALSE(sdl3d_teleporter_update(&teleporter, sdl3d_vec3_make(0.0f, 1.0f, 0.0f), 0.1f, bus));
    EXPECT_FALSE(sdl3d_teleporter_update(&teleporter, sdl3d_vec3_make(2.0f, 1.0f, 0.0f), 0.3f, bus));
    EXPECT_TRUE(sdl3d_teleporter_update(&teleporter, sdl3d_vec3_make(0.0f, 1.0f, 0.0f), 0.2f, bus));

    EXPECT_EQ(2, capture.count);
    sdl3d_signal_bus_destroy(bus);
}

TEST(SDL3DTeleporter, ResetAllowsReemitWithoutLeaving)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    ASSERT_NE(nullptr, bus);

    TeleportCapture capture;
    ASSERT_NE(0, sdl3d_signal_connect(bus, SDL3D_SIGNAL_TELEPORT, capture_teleport, &capture));

    sdl3d_teleporter teleporter{};
    sdl3d_teleporter_init(&teleporter, 1, make_bounds(), make_destination());
    teleporter.cooldown_seconds = 0.0f;

    EXPECT_TRUE(sdl3d_teleporter_update(&teleporter, sdl3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, bus));
    sdl3d_teleporter_reset(&teleporter);
    EXPECT_TRUE(sdl3d_teleporter_update(&teleporter, sdl3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, bus));

    EXPECT_EQ(2, capture.count);
    sdl3d_signal_bus_destroy(bus);
}

TEST(SDL3DTeleporter, PayloadDecodeRejectsMissingDestination)
{
    sdl3d_teleport_destination destination{};
    sdl3d_properties *payload = sdl3d_properties_create();
    ASSERT_NE(nullptr, payload);

    EXPECT_FALSE(sdl3d_teleport_destination_from_payload(nullptr, &destination));
    EXPECT_FALSE(sdl3d_teleport_destination_from_payload(payload, nullptr));
    EXPECT_FALSE(sdl3d_teleport_destination_from_payload(payload, &destination));

    sdl3d_properties_destroy(payload);
}

TEST(SDL3DFpsMoverTeleport, ResetsMotionStateAndOptionallyFacing)
{
    sdl3d_fps_mover_config config{};
    config.player_height = 1.6f;
    sdl3d_fps_mover mover{};
    sdl3d_fps_mover_init(&mover, &config, sdl3d_vec3_make(1.0f, 2.0f, 3.0f), 0.25f);

    mover.pitch = 0.1f;
    mover.vertical_velocity = 12.0f;
    mover.view_smooth = -0.5f;
    mover.current_sector = 4;

    sdl3d_fps_mover_teleport(&mover, sdl3d_vec3_make(10.0f, 3.0f, 20.0f), true, 1.5f, false, 0.8f);

    EXPECT_FLOAT_EQ(10.0f, mover.position.x);
    EXPECT_FLOAT_EQ(3.0f, mover.position.y);
    EXPECT_FLOAT_EQ(20.0f, mover.position.z);
    EXPECT_FLOAT_EQ(1.5f, mover.yaw);
    EXPECT_FLOAT_EQ(0.1f, mover.pitch);
    EXPECT_FLOAT_EQ(0.0f, mover.vertical_velocity);
    EXPECT_FLOAT_EQ(0.0f, mover.view_smooth);
    EXPECT_FALSE(mover.on_ground);
    EXPECT_EQ(-1, mover.current_sector);
}

#include <gtest/gtest.h>
extern "C"
{
#include "slayer3d/app.h"
}

TEST(SLAYER3DApp, InitAndClose)
{
    // Can't easily test window creation in CI, but verify the functions exist
    // and don't crash when called in wrong order
    EXPECT_FALSE(slayer3d_should_close());
    EXPECT_EQ(slayer3d_get_context(), nullptr);
    EXPECT_EQ(slayer3d_get_window(), nullptr);
    EXPECT_FLOAT_EQ(slayer3d_get_frame_time(), 0.0f);
    EXPECT_EQ(slayer3d_get_fps(), 0);
}

TEST(SLAYER3DApp, KeyStateDefaultsFalse)
{
    EXPECT_FALSE(slayer3d_is_key_down(SDL_SCANCODE_W));
    EXPECT_FALSE(slayer3d_is_key_pressed(SDL_SCANCODE_SPACE));
}

TEST(SLAYER3DApp, MouseDeltaDefaultsZero)
{
    float dx = 99, dy = 99;
    slayer3d_get_mouse_delta(&dx, &dy);
    EXPECT_FLOAT_EQ(dx, 0.0f);
    EXPECT_FLOAT_EQ(dy, 0.0f);
}

TEST(SLAYER3DApp, SetTargetFPS)
{
    slayer3d_set_target_fps(30);
    slayer3d_set_target_fps(0);
    slayer3d_set_target_fps(60);
    // Just verify it doesn't crash
}

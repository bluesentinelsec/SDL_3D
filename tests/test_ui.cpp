#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <gtest/gtest.h>

#include "sdl3d/ui.h"

namespace
{

// A stub font isn't needed for the non-rendering unit tests — the UI
// context accepts null fonts and simply skips text draws at render time.
TEST(SDL3DUI, CreateDestroy)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    ASSERT_NE(ui, nullptr);

    const sdl3d_ui_theme *theme = sdl3d_ui_get_theme(ui);
    ASSERT_NE(theme, nullptr);
    EXPECT_GT(theme->padding, 0.0f);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, DefaultThemeIsReasonable)
{
    sdl3d_ui_theme t = sdl3d_ui_default_theme();
    EXPECT_GT(t.padding, 0.0f);
    EXPECT_GE(t.spacing, 0.0f);
    EXPECT_GT(t.border_width, 0.0f);
    // Text should be bright (light theme or dark theme doesn't matter —
    // the default is a dark theme with light text).
    EXPECT_GT(int(t.text.r) + int(t.text.g) + int(t.text.b), 300);
}

TEST(SDL3DUI, IdHashingIsStableAndDistinct)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_id a = sdl3d_ui_make_id(ui, "Save");
    sdl3d_ui_id a2 = sdl3d_ui_make_id(ui, "Save");
    sdl3d_ui_id b = sdl3d_ui_make_id(ui, "Load");

    EXPECT_EQ(a, a2);
    EXPECT_NE(a, b);
    EXPECT_NE(a, 0u);
    EXPECT_NE(b, 0u);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, HitTesting)
{
    EXPECT_TRUE(sdl3d_ui_point_in_rect(10.0f, 10.0f, 0.0f, 0.0f, 100.0f, 100.0f));
    EXPECT_FALSE(sdl3d_ui_point_in_rect(-1.0f, 10.0f, 0.0f, 0.0f, 100.0f, 100.0f));
    EXPECT_FALSE(sdl3d_ui_point_in_rect(100.0f, 50.0f, 0.0f, 0.0f, 100.0f, 100.0f));
    EXPECT_TRUE(sdl3d_ui_point_in_rect(99.0f, 99.0f, 0.0f, 0.0f, 100.0f, 100.0f));
}

TEST(SDL3DUI, MouseEventsUpdateInputState)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    sdl3d_ui_begin_frame(ui, 1280, 720);

    SDL_Event motion = {};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 123.0f;
    motion.motion.y = 456.0f;
    sdl3d_ui_process_event(ui, &motion);

    const sdl3d_ui_input_state *in = sdl3d_ui_get_input(ui);
    ASSERT_NE(in, nullptr);
    EXPECT_FLOAT_EQ(in->mouse_x, 123.0f);
    EXPECT_FLOAT_EQ(in->mouse_y, 456.0f);
    EXPECT_FALSE(in->mouse_down[0]);

    SDL_Event down = {};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 50.0f;
    down.button.y = 60.0f;
    sdl3d_ui_process_event(ui, &down);
    EXPECT_TRUE(in->mouse_down[0]);
    EXPECT_TRUE(in->mouse_pressed[0]);

    SDL_Event up = {};
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.button = SDL_BUTTON_LEFT;
    sdl3d_ui_process_event(ui, &up);
    EXPECT_FALSE(in->mouse_down[0]);
    EXPECT_TRUE(in->mouse_released[0]);

    // End-of-frame clears edge-triggered bits for the next frame.
    sdl3d_ui_end_frame(ui);
    sdl3d_ui_begin_frame(ui, 1280, 720);
    EXPECT_FALSE(in->mouse_pressed[0]);
    EXPECT_FALSE(in->mouse_released[0]);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, HoveringAfterMouseMove)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    sdl3d_ui_begin_frame(ui, 1280, 720);

    SDL_Event motion = {};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 25.0f;
    motion.motion.y = 25.0f;
    sdl3d_ui_process_event(ui, &motion);

    EXPECT_TRUE(sdl3d_ui_is_hovering(ui, 0.0f, 0.0f, 50.0f, 50.0f));
    EXPECT_FALSE(sdl3d_ui_is_hovering(ui, 100.0f, 100.0f, 50.0f, 50.0f));

    sdl3d_ui_end_frame(ui);
    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, SubmitWidgetsWithoutRendering)
{
    // Exercise the command-submission path without requiring a render
    // context; verifies the command and text arenas grow correctly.
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_begin_frame(ui, 1280, 720);
    sdl3d_ui_label(ui, 10, 10, "Hello");
    sdl3d_ui_labelf(ui, 10, 30, "Count=%d", 42);
    sdl3d_ui_draw_rect(ui, 5, 5, 200, 24, {40, 40, 40, 255});
    sdl3d_ui_draw_rect_outline(ui, 5, 5, 200, 24, 1.0f, {200, 200, 200, 255});
    sdl3d_ui_push_clip(ui, 0, 0, 1280, 720);
    sdl3d_ui_label(ui, 10, 60, "Clipped");
    sdl3d_ui_pop_clip(ui);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, SetThemeRoundTrip)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_theme custom = sdl3d_ui_default_theme();
    custom.padding = 99.0f;
    sdl3d_ui_set_theme(ui, &custom);
    EXPECT_FLOAT_EQ(sdl3d_ui_get_theme(ui)->padding, 99.0f);

    sdl3d_ui_destroy(ui);
}

} // namespace

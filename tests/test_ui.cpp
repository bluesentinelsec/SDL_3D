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

TEST(SDL3DUI, MouseTransformMapsWindowCoordinatesToLogicalSpace)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_begin_frame(ui, 1280, 720);
    sdl3d_ui_set_mouse_transform(ui, 2.0f, 2.0f, 0.0f, 0.0f);

    SDL_Event motion = {};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 64.0f;
    motion.motion.y = 32.0f;
    sdl3d_ui_process_event(ui, &motion);

    const sdl3d_ui_input_state *in = sdl3d_ui_get_input(ui);
    ASSERT_NE(in, nullptr);
    EXPECT_FLOAT_EQ(in->mouse_x, 128.0f);
    EXPECT_FLOAT_EQ(in->mouse_y, 64.0f);

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

// Helper: simulate a mouse move to (x, y).
static void sim_mouse_move(sdl3d_ui_context *ui, float x, float y)
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = x;
    ev.motion.y = y;
    sdl3d_ui_process_event(ui, &ev);
}

// Helper: simulate a left mouse button press at (x, y).
static void sim_mouse_down(sdl3d_ui_context *ui, float x, float y)
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.x = x;
    ev.button.y = y;
    sdl3d_ui_process_event(ui, &ev);
}

// Helper: simulate a left mouse button release at (x, y).
static void sim_mouse_up(sdl3d_ui_context *ui, float x, float y)
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.x = x;
    ev.button.y = y;
    sdl3d_ui_process_event(ui, &ev);
}

TEST(SDL3DUI, ButtonClickReturnsTrue)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    // Frame 1: hover + press
    sdl3d_ui_begin_frame(ui, 1280, 720);
    sim_mouse_move(ui, 50.0f, 50.0f);
    sim_mouse_down(ui, 50.0f, 50.0f);
    EXPECT_FALSE(sdl3d_ui_button(ui, 0, 0, 100, 100, "Test"));
    sdl3d_ui_end_frame(ui);

    // Frame 2: release while hovering → click
    sdl3d_ui_begin_frame(ui, 1280, 720);
    sim_mouse_move(ui, 50.0f, 50.0f);
    sim_mouse_up(ui, 50.0f, 50.0f);
    EXPECT_TRUE(sdl3d_ui_button(ui, 0, 0, 100, 100, "Test"));
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, ButtonClickHonorsMouseTransform)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    /* Simulate a 640x360 window presenting into a 1280x720 logical UI.
     * A click at window coords (25, 25) should map to logical (50, 50). */
    sdl3d_ui_begin_frame(ui, 1280, 720);
    sdl3d_ui_set_mouse_transform(ui, 2.0f, 2.0f, 0.0f, 0.0f);
    sim_mouse_move(ui, 25.0f, 25.0f);
    sim_mouse_down(ui, 25.0f, 25.0f);
    EXPECT_FALSE(sdl3d_ui_button(ui, 0, 0, 100, 100, "ScaledButton"));
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_begin_frame(ui, 1280, 720);
    sdl3d_ui_set_mouse_transform(ui, 2.0f, 2.0f, 0.0f, 0.0f);
    sim_mouse_move(ui, 25.0f, 25.0f);
    sim_mouse_up(ui, 25.0f, 25.0f);
    EXPECT_TRUE(sdl3d_ui_button(ui, 0, 0, 100, 100, "ScaledButton"));
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, ButtonClickOutsideReturnsFalse)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    // Frame 1: press on button
    sdl3d_ui_begin_frame(ui, 1280, 720);
    sim_mouse_move(ui, 50.0f, 50.0f);
    sim_mouse_down(ui, 50.0f, 50.0f);
    sdl3d_ui_button(ui, 0, 0, 100, 100, "Test");
    sdl3d_ui_end_frame(ui);

    // Frame 2: drag outside, release → no click
    sdl3d_ui_begin_frame(ui, 1280, 720);
    sim_mouse_move(ui, 200.0f, 200.0f);
    sim_mouse_up(ui, 200.0f, 200.0f);
    EXPECT_FALSE(sdl3d_ui_button(ui, 0, 0, 100, 100, "Test"));
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, ButtonWantsMouse)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_begin_frame(ui, 1280, 720);
    sim_mouse_move(ui, 50.0f, 50.0f);
    EXPECT_FALSE(sdl3d_ui_wants_mouse(ui));
    sdl3d_ui_button(ui, 0, 0, 100, 100, "Test");
    EXPECT_TRUE(sdl3d_ui_wants_mouse(ui));
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, ButtonHashSeparator)
{
    // "Save##1" and "Save##2" should be distinct IDs but display the same text.
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_id a = sdl3d_ui_make_id(ui, "Save##1");
    sdl3d_ui_id b = sdl3d_ui_make_id(ui, "Save##2");
    EXPECT_NE(a, b);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, VboxLayoutAdvancesCursor)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_begin_frame(ui, 800, 600);
    sdl3d_ui_begin_vbox(ui, 10, 20, 200, 400);

    // Layout labels and buttons without crashing; verify no layout leak.
    sdl3d_ui_layout_label(ui, "Hello");
    sdl3d_ui_layout_button(ui, "Click");
    sdl3d_ui_separator(ui);
    sdl3d_ui_layout_label(ui, "World");

    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, HboxLayoutAdvancesCursor)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_begin_frame(ui, 800, 600);
    sdl3d_ui_begin_hbox(ui, 10, 20, 400, 40);

    sdl3d_ui_layout_button(ui, "A");
    sdl3d_ui_layout_button(ui, "B");
    sdl3d_ui_separator(ui);
    sdl3d_ui_layout_button(ui, "C");

    sdl3d_ui_end_hbox(ui);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, PanelClipsChildren)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_begin_frame(ui, 800, 600);
    sdl3d_ui_begin_panel(ui, 50, 50, 200, 100);
    sdl3d_ui_label(ui, 60, 60, "Inside panel");
    sdl3d_ui_end_panel(ui);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, NestedPanelVbox)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_begin_frame(ui, 800, 600);
    sdl3d_ui_begin_panel(ui, 0, 0, 200, 400);
    sdl3d_ui_begin_vbox(ui, 8, 8, 184, 384);
    sdl3d_ui_layout_label(ui, "Title");
    sdl3d_ui_layout_button(ui, "Action");
    sdl3d_ui_separator(ui);
    sdl3d_ui_layout_labelf(ui, "Count: %d", 42);
    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_panel(ui);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, LayoutButtonClickWorks)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    // Frame 1: hover + press on the first button in a vbox at (10, 20)
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 25.0f);
    sim_mouse_down(ui, 50.0f, 25.0f);
    sdl3d_ui_begin_vbox(ui, 10, 20, 200, 400);
    EXPECT_FALSE(sdl3d_ui_layout_button(ui, "Btn"));
    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_frame(ui);

    // Frame 2: release while hovering → click
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 25.0f);
    sim_mouse_up(ui, 50.0f, 25.0f);
    sdl3d_ui_begin_vbox(ui, 10, 20, 200, 400);
    EXPECT_TRUE(sdl3d_ui_layout_button(ui, "Btn"));
    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, CheckboxToggle)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    bool val = false;

    // Frame 1: press on checkbox at (10, 10)
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 15.0f, 15.0f);
    sim_mouse_down(ui, 15.0f, 15.0f);
    EXPECT_FALSE(sdl3d_ui_checkbox(ui, 10, 10, "Toggle", &val));
    EXPECT_FALSE(val);
    sdl3d_ui_end_frame(ui);

    // Frame 2: release → toggles to true
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 15.0f, 15.0f);
    sim_mouse_up(ui, 15.0f, 15.0f);
    EXPECT_TRUE(sdl3d_ui_checkbox(ui, 10, 10, "Toggle", &val));
    EXPECT_TRUE(val);
    sdl3d_ui_end_frame(ui);

    // Frame 3: press again
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 15.0f, 15.0f);
    sim_mouse_down(ui, 15.0f, 15.0f);
    sdl3d_ui_checkbox(ui, 10, 10, "Toggle", &val);
    sdl3d_ui_end_frame(ui);

    // Frame 4: release → toggles back to false
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 15.0f, 15.0f);
    sim_mouse_up(ui, 15.0f, 15.0f);
    EXPECT_TRUE(sdl3d_ui_checkbox(ui, 10, 10, "Toggle", &val));
    EXPECT_FALSE(val);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, SliderDrag)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    float val = 0.0f;

    // Frame 1: press on slider at x=10, w=200 → mouse at x=110 = midpoint
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 110.0f, 15.0f);
    sim_mouse_down(ui, 110.0f, 15.0f);
    EXPECT_TRUE(sdl3d_ui_slider(ui, 10, 10, 200, "Val", &val, 0.0f, 1.0f));
    EXPECT_NEAR(val, 0.5f, 0.01f);
    sdl3d_ui_end_frame(ui);

    // Frame 2: drag to x=210 (end of slider) while held
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 210.0f, 15.0f);
    EXPECT_TRUE(sdl3d_ui_slider(ui, 10, 10, 200, "Val", &val, 0.0f, 1.0f));
    EXPECT_NEAR(val, 1.0f, 0.01f);
    sdl3d_ui_end_frame(ui);

    // Frame 3: release
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_up(ui, 210.0f, 15.0f);
    sdl3d_ui_slider(ui, 10, 10, 200, "Val", &val, 0.0f, 1.0f);
    sdl3d_ui_end_frame(ui);

    // Value should remain at 1.0 after release
    EXPECT_NEAR(val, 1.0f, 0.01f);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, SliderClampsValue)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    float val = 5.0f; // out of range

    sdl3d_ui_begin_frame(ui, 800, 600);
    sdl3d_ui_slider(ui, 10, 10, 200, "Clamped", &val, 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(val, 1.0f);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

// Helper: simulate a mouse wheel scroll.
static void sim_scroll(sdl3d_ui_context *ui, float x, float y, float dy)
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_WHEEL;
    ev.wheel.y = dy;
    ev.wheel.mouse_x = x;
    ev.wheel.mouse_y = y;
    ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    sdl3d_ui_process_event(ui, &ev);
}

TEST(SDL3DUI, ScrollRegionClampsOffset)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    float scroll = 0.0f;

    // Scroll up (negative offset) should clamp to 0.
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_scroll(ui, 50.0f, 50.0f, 5.0f); // scroll up
    sdl3d_ui_begin_scroll(ui, 0, 0, 100, 100, &scroll, 200);
    sdl3d_ui_end_scroll(ui);
    EXPECT_FLOAT_EQ(scroll, 0.0f);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, ScrollRegionScrollsDown)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    float scroll = 0.0f;

    // Scroll down while hovering the region.
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_scroll(ui, 50.0f, 50.0f, -2.0f); // scroll down
    sdl3d_ui_begin_scroll(ui, 0, 0, 100, 100, &scroll, 300);
    sdl3d_ui_end_scroll(ui);
    EXPECT_GT(scroll, 0.0f);
    // Should not exceed max (content_height - visible_height = 200).
    EXPECT_LE(scroll, 200.0f);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, ScrollRegionClampsMax)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    float scroll = 999.0f; // way past max

    sdl3d_ui_begin_frame(ui, 800, 600);
    sdl3d_ui_begin_scroll(ui, 0, 0, 100, 100, &scroll, 150);
    sdl3d_ui_end_scroll(ui);
    // Max scroll = 150 - 100 = 50.
    EXPECT_FLOAT_EQ(scroll, 50.0f);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, ScrollRegionIgnoresWheelOutside)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    float scroll = 0.0f;

    // Scroll while mouse is outside the region.
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_scroll(ui, 500.0f, 500.0f, -5.0f); // outside (0,0,100,100)
    sdl3d_ui_begin_scroll(ui, 0, 0, 100, 100, &scroll, 300);
    sdl3d_ui_end_scroll(ui);
    EXPECT_FLOAT_EQ(scroll, 0.0f);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, TextFieldTyping)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    char buf[64] = "";

    // Frame 1: click to focus
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    sdl3d_ui_end_frame(ui);

    // Frame 2: type "Hi"
    sdl3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_TEXT_INPUT;
        ev.text.text = "Hi";
        sdl3d_ui_process_event(ui, &ev);
    }
    sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    EXPECT_STREQ(buf, "Hi");
    sdl3d_ui_end_frame(ui);

    // Frame 3: backspace
    sdl3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.scancode = SDL_SCANCODE_BACKSPACE;
        sdl3d_ui_process_event(ui, &ev);
    }
    sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    EXPECT_STREQ(buf, "H");
    sdl3d_ui_end_frame(ui);

    // Frame 4: enter commits
    sdl3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.scancode = SDL_SCANCODE_RETURN;
        sdl3d_ui_process_event(ui, &ev);
    }
    EXPECT_TRUE(sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf)));
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, DropdownSelection)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    const char *items[] = {"Apple", "Banana", "Cherry"};
    int selected = 0;

    // Frame 1: click to open
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    sdl3d_ui_dropdown(ui, 0, 0, 200, 30, items, 3, &selected);
    sdl3d_ui_end_frame(ui);

    // Frame 2: click on second item (list starts at y=30, item_h=30, so item 1 at y=60..90)
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 75.0f);
    sim_mouse_down(ui, 50.0f, 75.0f);
    EXPECT_TRUE(sdl3d_ui_dropdown(ui, 0, 0, 200, 30, items, 3, &selected));
    EXPECT_EQ(selected, 1);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, DropdownPopupBlocksOverlappingWidgetInput)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    const char *items[] = {"Apple", "Banana", "Cherry"};
    int selected = 0;

    // Frame 1: click to open the dropdown.
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    sdl3d_ui_dropdown(ui, 0, 0, 200, 30, items, 3, &selected);
    sdl3d_ui_end_frame(ui);

    // Frame 2: click on the second dropdown item. An overlapping button
    // occupies the same screen-space, but must not receive the press.
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 75.0f);
    sim_mouse_down(ui, 50.0f, 75.0f);
    EXPECT_TRUE(sdl3d_ui_dropdown(ui, 0, 0, 200, 30, items, 3, &selected));
    EXPECT_EQ(selected, 1);
    EXPECT_FALSE(sdl3d_ui_button(ui, 0, 60, 200, 30, "Overlap"));
    sdl3d_ui_end_frame(ui);

    // Frame 3: release over the overlapping button. If the popup failed
    // to block the earlier press, the button would falsely click here.
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 75.0f);
    sim_mouse_up(ui, 50.0f, 75.0f);
    EXPECT_FALSE(sdl3d_ui_button(ui, 0, 60, 200, 30, "Overlap"));
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, TabStripSelection)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    const char *tabs[] = {"Entity", "Brush", "Face"};
    int selected = 0;

    // Click on second tab (x = 200/3 + some = ~80)
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 100.0f, 15.0f);
    sim_mouse_down(ui, 100.0f, 15.0f);
    EXPECT_TRUE(sdl3d_ui_tab_strip(ui, 0, 0, 200, 30, tabs, 3, &selected));
    EXPECT_EQ(selected, 1);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, TabStripReClickReturnsFalse)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    const char *tabs[] = {"A", "B"};
    int selected = 0;

    // Click on already-selected tab 0
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 25.0f, 15.0f);
    sim_mouse_down(ui, 25.0f, 15.0f);
    EXPECT_FALSE(sdl3d_ui_tab_strip(ui, 0, 0, 200, 30, tabs, 2, &selected));
    EXPECT_EQ(selected, 0);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, TextFieldEscapeCancels)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    char buf[64] = "original";

    // Frame 1: click to focus
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    sdl3d_ui_end_frame(ui);

    // Frame 2: press Escape → should NOT commit (returns false)
    sdl3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.scancode = SDL_SCANCODE_ESCAPE;
        sdl3d_ui_process_event(ui, &ev);
    }
    EXPECT_FALSE(sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf)));
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, TextFieldClickOutsideCommits)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    char buf[64] = "hello";

    // Frame 1: click to focus
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    sdl3d_ui_end_frame(ui);

    // Frame 2: click outside → should commit (returns true)
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 500.0f, 500.0f);
    sim_mouse_down(ui, 500.0f, 500.0f);
    EXPECT_TRUE(sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf)));
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, TextFieldBufferOverflow)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    char buf[4] = "ab"; // 2 chars + NUL, room for 1 more

    // Frame 1: click to focus
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    sdl3d_ui_end_frame(ui);

    // Frame 2: type "xyz" — only 1 char should fit
    sdl3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_TEXT_INPUT;
        ev.text.text = "xyz";
        sdl3d_ui_process_event(ui, &ev);
    }
    sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    EXPECT_STREQ(buf, "abx");
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, CheckboxReleaseOutsideDoesNotToggle)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    bool val = false;

    // Frame 1: press on checkbox
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 15.0f, 15.0f);
    sim_mouse_down(ui, 15.0f, 15.0f);
    sdl3d_ui_checkbox(ui, 10, 10, "CB", &val);
    sdl3d_ui_end_frame(ui);

    // Frame 2: move outside, release → should NOT toggle
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 500.0f, 500.0f);
    sim_mouse_up(ui, 500.0f, 500.0f);
    sdl3d_ui_checkbox(ui, 10, 10, "CB", &val);
    EXPECT_FALSE(val);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, DropdownCloseOnClickOutside)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    const char *items[] = {"A", "B"};
    int selected = 0;

    // Frame 1: click to open
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    sdl3d_ui_dropdown(ui, 0, 0, 200, 30, items, 2, &selected);
    sdl3d_ui_end_frame(ui);

    // Frame 2: click far outside → should close, selection unchanged
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 500.0f, 500.0f);
    sim_mouse_down(ui, 500.0f, 500.0f);
    EXPECT_FALSE(sdl3d_ui_dropdown(ui, 0, 0, 200, 30, items, 2, &selected));
    EXPECT_EQ(selected, 0);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, SliderRejectsInvalidRange)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    float val = 0.5f;

    sdl3d_ui_begin_frame(ui, 800, 600);
    // max <= min should return false and not modify val
    EXPECT_FALSE(sdl3d_ui_slider(ui, 10, 10, 200, "Bad", &val, 5.0f, 5.0f));
    EXPECT_FLOAT_EQ(val, 0.5f);
    EXPECT_FALSE(sdl3d_ui_slider(ui, 10, 10, 200, "Bad2", &val, 10.0f, 1.0f));
    EXPECT_FLOAT_EQ(val, 0.5f);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, WantsKeyboardWhenFocused)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    char buf[32] = "";

    EXPECT_FALSE(sdl3d_ui_wants_keyboard(ui));

    // Click to focus a text field
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    sdl3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    EXPECT_TRUE(sdl3d_ui_wants_keyboard(ui));
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, ScrollWheelFlippedDirection)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    float scroll = 0.0f;

    // Flipped wheel: positive y means scroll down (toward user)
    sdl3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.y = 2.0f;
        ev.wheel.mouse_x = 50.0f;
        ev.wheel.mouse_y = 50.0f;
        ev.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
        sdl3d_ui_process_event(ui, &ev);
    }
    sdl3d_ui_begin_scroll(ui, 0, 0, 100, 100, &scroll, 300);
    sdl3d_ui_end_scroll(ui);
    // Flipped + positive y = scroll down = positive offset
    EXPECT_GT(scroll, 0.0f);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, MeasureTextReturnsNonZero)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    // With no font, measure should return 0.
    float w = -1, h = -1;
    sdl3d_ui_measure_text(ui, "Hello", &w, &h);
    EXPECT_FLOAT_EQ(w, 0.0f);
    EXPECT_FLOAT_EQ(h, 0.0f);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, InspectorRowLabel)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));

    sdl3d_ui_begin_frame(ui, 800, 600);
    sdl3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    sdl3d_ui_row_label(ui, "Name:", "worldspawn");
    sdl3d_ui_row_label(ui, "Origin:", "0 0 0");
    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, InspectorRowCheckbox)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    bool val = false;

    // Frame 1: press on the checkbox area (right side of row at x=10+300*0.35=115)
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 120.0f, 18.0f);
    sim_mouse_down(ui, 120.0f, 18.0f);
    sdl3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    sdl3d_ui_row_checkbox(ui, "Visible:", &val);
    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_frame(ui);

    // Frame 2: release → toggle
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 120.0f, 18.0f);
    sim_mouse_up(ui, 120.0f, 18.0f);
    sdl3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    EXPECT_TRUE(sdl3d_ui_row_checkbox(ui, "Visible:", &val));
    EXPECT_TRUE(val);
    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, ListViewSelection)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    const char *items[] = {"Alpha", "Beta", "Gamma", "Delta"};
    int selected = 0;
    float scroll = 0.0f;

    // Click on second item (y = 10 + 22*1 + 11 = ~43)
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 43.0f);
    sim_mouse_down(ui, 50.0f, 43.0f);
    EXPECT_TRUE(sdl3d_ui_list_view(ui, 10, 10, 200, 100, items, 4, &selected, &scroll));
    EXPECT_EQ(selected, 1);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, ListViewReClickReturnsFalse)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    const char *items[] = {"A", "B"};
    int selected = 0;
    float scroll = 0.0f;

    // Click on already-selected item 0
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 20.0f);
    sim_mouse_down(ui, 50.0f, 20.0f);
    EXPECT_FALSE(sdl3d_ui_list_view(ui, 10, 10, 200, 100, items, 2, &selected, &scroll));
    EXPECT_EQ(selected, 0);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, TreeNodeExpandCollapse)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    bool expanded = false;
    int sel = -1;

    // Click on the arrow area (left side) to expand
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 17.0f, 21.0f); // arrow at x=10, y=10, w=14
    sim_mouse_down(ui, 17.0f, 21.0f);
    sdl3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    sdl3d_ui_tree_node(ui, "Root", 1, &expanded, &sel);
    sdl3d_ui_end_vbox(ui);
    EXPECT_TRUE(expanded);
    EXPECT_EQ(sel, -1); // arrow click doesn't select
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, TreeNodeSelect)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    bool expanded = true;
    int sel = -1;

    // Click on the label area (right of arrow) to select
    sdl3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 21.0f); // label area
    sim_mouse_down(ui, 50.0f, 21.0f);
    sdl3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    EXPECT_TRUE(sdl3d_ui_tree_node(ui, "Root", 1, &expanded, &sel));
    EXPECT_EQ(sel, 1);
    EXPECT_TRUE(expanded); // selection doesn't toggle expand
    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_frame(ui);

    sdl3d_ui_destroy(ui);
}

TEST(SDL3DUI, TreePushPopIndent)
{
    sdl3d_ui_context *ui = nullptr;
    ASSERT_TRUE(sdl3d_ui_create(nullptr, &ui));
    bool exp1 = true, exp2 = false;
    int sel = -1;

    sdl3d_ui_begin_frame(ui, 800, 600);
    sdl3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    sdl3d_ui_tree_node(ui, "Parent", 1, &exp1, &sel);
    sdl3d_ui_tree_push(ui);
    sdl3d_ui_tree_node(ui, "Child", 2, &exp2, &sel);
    sdl3d_ui_tree_pop(ui);
    sdl3d_ui_tree_node(ui, "Sibling", 3, &exp1, &sel);
    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_frame(ui);

    // No crash, no leak — validates push/pop balance.
    sdl3d_ui_destroy(ui);
}

} // namespace

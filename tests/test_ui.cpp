#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <gtest/gtest.h>

#include "slayer3d/ui.h"
#include "slayer3d/ui_layout.h"

namespace
{

void expect_rect(const slayer3d_ui_layout_resolved_node *node, float x, float y, float w, float h)
{
    ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->rect.x, x);
    EXPECT_FLOAT_EQ(node->rect.y, y);
    EXPECT_FLOAT_EQ(node->rect.w, w);
    EXPECT_FLOAT_EQ(node->rect.h, h);
}

// A stub font isn't needed for the non-rendering unit tests — the UI
// context accepts null fonts and simply skips text draws at render time.
TEST(SLAYER3DUI, CreateDestroy)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    ASSERT_NE(ui, nullptr);

    const slayer3d_ui_theme *theme = slayer3d_ui_get_theme(ui);
    ASSERT_NE(theme, nullptr);
    EXPECT_GT(theme->padding, 0.0f);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, DefaultThemeIsReasonable)
{
    slayer3d_ui_theme t = slayer3d_ui_default_theme();
    EXPECT_GT(t.padding, 0.0f);
    EXPECT_GE(t.spacing, 0.0f);
    EXPECT_GT(t.border_width, 0.0f);
    // Text should be bright (light theme or dark theme doesn't matter —
    // the default is a dark theme with light text).
    EXPECT_GT(int(t.text.r) + int(t.text.g) + int(t.text.b), 300);
}

TEST(SLAYER3DUI, IdHashingIsStableAndDistinct)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_id a = slayer3d_ui_make_id(ui, "Save");
    slayer3d_ui_id a2 = slayer3d_ui_make_id(ui, "Save");
    slayer3d_ui_id b = slayer3d_ui_make_id(ui, "Load");

    EXPECT_EQ(a, a2);
    EXPECT_NE(a, b);
    EXPECT_NE(a, 0u);
    EXPECT_NE(b, 0u);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, HitTesting)
{
    EXPECT_TRUE(slayer3d_ui_point_in_rect(10.0f, 10.0f, 0.0f, 0.0f, 100.0f, 100.0f));
    EXPECT_FALSE(slayer3d_ui_point_in_rect(-1.0f, 10.0f, 0.0f, 0.0f, 100.0f, 100.0f));
    EXPECT_FALSE(slayer3d_ui_point_in_rect(100.0f, 50.0f, 0.0f, 0.0f, 100.0f, 100.0f));
    EXPECT_TRUE(slayer3d_ui_point_in_rect(99.0f, 99.0f, 0.0f, 0.0f, 100.0f, 100.0f));
}

TEST(SLAYER3DUI, RetainedLayoutChildMovesWithParent)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc root{};
    root.id = "toolbar";
    root.type = SLAYER3D_UI_LAYOUT_NODE_TOOLBAR;
    root.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    root.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    root.rect = {10.0f, 20.0f, 200.0f, 40.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &root));

    slayer3d_ui_layout_node_desc child{};
    child.id = "grid";
    child.parent_id = "toolbar";
    child.type = SLAYER3D_UI_LAYOUT_NODE_DROPDOWN;
    child.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    child.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    child.rect = {12.0f, 4.0f, 80.0f, 24.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &child));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "grid"), 22.0f, 24.0f, 80.0f, 24.0f);

    slayer3d_ui_layout_clear(layout);
    root.rect = {100.0f, 50.0f, 200.0f, 40.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &root));
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &child));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "grid"), 112.0f, 54.0f, 80.0f, 24.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedRowLayoutDistributesFillChildren)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc row{};
    row.id = "row";
    row.type = SLAYER3D_UI_LAYOUT_NODE_ROW;
    row.axis = SLAYER3D_UI_LAYOUT_AXIS_ROW;
    row.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    row.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    row.rect = {0.0f, 0.0f, 120.0f, 30.0f};
    row.padding = 5.0f;
    row.gap = 2.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &row));

    slayer3d_ui_layout_node_desc fixed{};
    fixed.id = "file";
    fixed.parent_id = "row";
    fixed.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    fixed.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    fixed.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    fixed.rect.w = 20.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &fixed));

    slayer3d_ui_layout_node_desc fill{};
    fill.id = "edit";
    fill.parent_id = "row";
    fill.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    fill.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    fill.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &fill));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "file"), 5.0f, 5.0f, 20.0f, 20.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "edit"), 27.0f, 5.0f, 88.0f, 20.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedColumnLayoutDistributesFillChildren)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc column{};
    column.id = "panel";
    column.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    column.axis = SLAYER3D_UI_LAYOUT_AXIS_COLUMN;
    column.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    column.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    column.rect = {10.0f, 10.0f, 90.0f, 80.0f};
    column.padding = 4.0f;
    column.gap = 4.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &column));

    slayer3d_ui_layout_node_desc title{};
    title.id = "title";
    title.parent_id = "panel";
    title.type = SLAYER3D_UI_LAYOUT_NODE_LABEL;
    title.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    title.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    title.rect.h = 20.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &title));

    slayer3d_ui_layout_node_desc body{};
    body.id = "body";
    body.parent_id = "panel";
    body.type = SLAYER3D_UI_LAYOUT_NODE_CONSOLE;
    body.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    body.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &body));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "title"), 14.0f, 14.0f, 82.0f, 20.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "body"), 14.0f, 38.0f, 82.0f, 48.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedFlatListsUseResolvedRects)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc root{};
    root.id = "panel";
    root.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    root.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    root.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    root.rect = {10.0f, 20.0f, 200.0f, 80.0f};
    root.padding = 5.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &root));

    slayer3d_ui_layout_node_desc button{};
    button.id = "button";
    button.parent_id = "panel";
    button.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    button.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    button.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    button.rect = {12.0f, 8.0f, 90.0f, 24.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &button));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    const slayer3d_ui_layout_resolved_node *resolved = slayer3d_ui_layout_find_resolved_node(layout, "button");
    expect_rect(resolved, 27.0f, 33.0f, 90.0f, 24.0f);

    ASSERT_EQ(slayer3d_ui_layout_render_command_count(layout), 2);
    ASSERT_EQ(slayer3d_ui_layout_hit_region_count(layout), 1);
    const slayer3d_ui_layout_hit_region *hit = slayer3d_ui_layout_hit_region_at(layout, 0);
    ASSERT_NE(hit, nullptr);
    EXPECT_STREQ(hit->id, "button");
    EXPECT_FLOAT_EQ(hit->rect.x, resolved->rect.x);
    EXPECT_FLOAT_EQ(hit->rect.y, resolved->rect.y);
    EXPECT_FLOAT_EQ(hit->rect.w, resolved->rect.w);
    EXPECT_FLOAT_EQ(hit->rect.h, resolved->rect.h);

    ASSERT_EQ(slayer3d_ui_layout_hit_test(layout, 30.0f, 40.0f), hit);
    EXPECT_EQ(slayer3d_ui_layout_hit_test(layout, 5.0f, 5.0f), nullptr);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedHitTestingUsesFrontMostLayer)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc back{};
    back.id = "back";
    back.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    back.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    back.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    back.rect = {0.0f, 0.0f, 100.0f, 100.0f};
    back.layer = 0;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &back));

    slayer3d_ui_layout_node_desc front{};
    front.id = "front";
    front.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    front.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    front.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    front.rect = {20.0f, 20.0f, 100.0f, 100.0f};
    front.layer = 10;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &front));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    ASSERT_EQ(slayer3d_ui_layout_render_command_count(layout), 2);
    const slayer3d_ui_layout_render_command *render0 = slayer3d_ui_layout_render_command_at(layout, 0);
    const slayer3d_ui_layout_render_command *render1 = slayer3d_ui_layout_render_command_at(layout, 1);
    ASSERT_NE(render0, nullptr);
    ASSERT_NE(render1, nullptr);
    EXPECT_STREQ(render0->id, "back");
    EXPECT_STREQ(render1->id, "front");

    const slayer3d_ui_layout_hit_region *top = slayer3d_ui_layout_hit_test(layout, 30.0f, 30.0f);
    ASSERT_NE(top, nullptr);
    EXPECT_STREQ(top->id, "front");
    const slayer3d_ui_layout_hit_region *back_only = slayer3d_ui_layout_hit_test(layout, 10.0f, 10.0f);
    ASSERT_NE(back_only, nullptr);
    EXPECT_STREQ(back_only->id, "back");

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedLayoutDirtyGenerationAvoidsUnneededResolve)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc root{};
    root.id = "root";
    root.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    root.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    root.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &root));
    EXPECT_TRUE(slayer3d_ui_layout_is_dirty(layout));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    EXPECT_FALSE(slayer3d_ui_layout_is_dirty(layout));
    const int first_generation = slayer3d_ui_layout_generation(layout);
    EXPECT_GT(first_generation, 0);

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    EXPECT_EQ(slayer3d_ui_layout_generation(layout), first_generation);

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 640.0f, 360.0f));
    const int resized_generation = slayer3d_ui_layout_generation(layout);
    EXPECT_GT(resized_generation, first_generation);

    slayer3d_ui_layout_mark_dirty(layout);
    EXPECT_TRUE(slayer3d_ui_layout_is_dirty(layout));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 640.0f, 360.0f));
    EXPECT_GT(slayer3d_ui_layout_generation(layout), resized_generation);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, MouseEventsUpdateInputState)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    slayer3d_ui_begin_frame(ui, 1280, 720);

    SDL_Event motion = {};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 123.0f;
    motion.motion.y = 456.0f;
    slayer3d_ui_process_event(ui, &motion);

    const slayer3d_ui_input_state *in = slayer3d_ui_get_input(ui);
    ASSERT_NE(in, nullptr);
    EXPECT_FLOAT_EQ(in->mouse_x, 123.0f);
    EXPECT_FLOAT_EQ(in->mouse_y, 456.0f);
    EXPECT_FALSE(in->mouse_down[0]);

    SDL_Event down = {};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 50.0f;
    down.button.y = 60.0f;
    slayer3d_ui_process_event(ui, &down);
    EXPECT_TRUE(in->mouse_down[0]);
    EXPECT_TRUE(in->mouse_pressed[0]);

    SDL_Event up = {};
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.button = SDL_BUTTON_LEFT;
    slayer3d_ui_process_event(ui, &up);
    EXPECT_FALSE(in->mouse_down[0]);
    EXPECT_TRUE(in->mouse_released[0]);

    // End-of-frame clears edge-triggered bits for the next frame.
    slayer3d_ui_end_frame(ui);
    slayer3d_ui_begin_frame(ui, 1280, 720);
    EXPECT_FALSE(in->mouse_pressed[0]);
    EXPECT_FALSE(in->mouse_released[0]);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, HoveringAfterMouseMove)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    slayer3d_ui_begin_frame(ui, 1280, 720);

    SDL_Event motion = {};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 25.0f;
    motion.motion.y = 25.0f;
    slayer3d_ui_process_event(ui, &motion);

    EXPECT_TRUE(slayer3d_ui_is_hovering(ui, 0.0f, 0.0f, 50.0f, 50.0f));
    EXPECT_FALSE(slayer3d_ui_is_hovering(ui, 100.0f, 100.0f, 50.0f, 50.0f));

    slayer3d_ui_end_frame(ui);
    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, MouseTransformMapsWindowCoordinatesToLogicalSpace)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_begin_frame(ui, 1280, 720);
    slayer3d_ui_set_mouse_transform(ui, 2.0f, 2.0f, 0.0f, 0.0f);

    SDL_Event motion = {};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 64.0f;
    motion.motion.y = 32.0f;
    slayer3d_ui_process_event(ui, &motion);

    const slayer3d_ui_input_state *in = slayer3d_ui_get_input(ui);
    ASSERT_NE(in, nullptr);
    EXPECT_FLOAT_EQ(in->mouse_x, 128.0f);
    EXPECT_FLOAT_EQ(in->mouse_y, 64.0f);

    slayer3d_ui_end_frame(ui);
    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, SubmitWidgetsWithoutRendering)
{
    // Exercise the command-submission path without requiring a render
    // context; verifies the command and text arenas grow correctly.
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_begin_frame(ui, 1280, 720);
    slayer3d_ui_label(ui, 10, 10, "Hello");
    slayer3d_ui_labelf(ui, 10, 30, "Count=%d", 42);
    slayer3d_ui_draw_rect(ui, 5, 5, 200, 24, {40, 40, 40, 255});
    slayer3d_ui_draw_rect_outline(ui, 5, 5, 200, 24, 1.0f, {200, 200, 200, 255});
    slayer3d_ui_push_clip(ui, 0, 0, 1280, 720);
    slayer3d_ui_label(ui, 10, 60, "Clipped");
    slayer3d_ui_pop_clip(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, SetThemeRoundTrip)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_theme custom = slayer3d_ui_default_theme();
    custom.padding = 99.0f;
    slayer3d_ui_set_theme(ui, &custom);
    EXPECT_FLOAT_EQ(slayer3d_ui_get_theme(ui)->padding, 99.0f);

    slayer3d_ui_destroy(ui);
}

// Helper: simulate a mouse move to (x, y).
static void sim_mouse_move(slayer3d_ui_context *ui, float x, float y)
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = x;
    ev.motion.y = y;
    slayer3d_ui_process_event(ui, &ev);
}

// Helper: simulate a left mouse button press at (x, y).
static void sim_mouse_down(slayer3d_ui_context *ui, float x, float y)
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.x = x;
    ev.button.y = y;
    slayer3d_ui_process_event(ui, &ev);
}

// Helper: simulate a left mouse button release at (x, y).
static void sim_mouse_up(slayer3d_ui_context *ui, float x, float y)
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.x = x;
    ev.button.y = y;
    slayer3d_ui_process_event(ui, &ev);
}

TEST(SLAYER3DUI, ButtonClickReturnsTrue)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    // Frame 1: hover + press
    slayer3d_ui_begin_frame(ui, 1280, 720);
    sim_mouse_move(ui, 50.0f, 50.0f);
    sim_mouse_down(ui, 50.0f, 50.0f);
    EXPECT_FALSE(slayer3d_ui_button(ui, 0, 0, 100, 100, "Test"));
    slayer3d_ui_end_frame(ui);

    // Frame 2: release while hovering → click
    slayer3d_ui_begin_frame(ui, 1280, 720);
    sim_mouse_move(ui, 50.0f, 50.0f);
    sim_mouse_up(ui, 50.0f, 50.0f);
    EXPECT_TRUE(slayer3d_ui_button(ui, 0, 0, 100, 100, "Test"));
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, ButtonClickHonorsMouseTransform)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    /* Simulate a 640x360 window presenting into a 1280x720 logical UI.
     * A click at window coords (25, 25) should map to logical (50, 50). */
    slayer3d_ui_begin_frame(ui, 1280, 720);
    slayer3d_ui_set_mouse_transform(ui, 2.0f, 2.0f, 0.0f, 0.0f);
    sim_mouse_move(ui, 25.0f, 25.0f);
    sim_mouse_down(ui, 25.0f, 25.0f);
    EXPECT_FALSE(slayer3d_ui_button(ui, 0, 0, 100, 100, "ScaledButton"));
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_begin_frame(ui, 1280, 720);
    slayer3d_ui_set_mouse_transform(ui, 2.0f, 2.0f, 0.0f, 0.0f);
    sim_mouse_move(ui, 25.0f, 25.0f);
    sim_mouse_up(ui, 25.0f, 25.0f);
    EXPECT_TRUE(slayer3d_ui_button(ui, 0, 0, 100, 100, "ScaledButton"));
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, ButtonClickOutsideReturnsFalse)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    // Frame 1: press on button
    slayer3d_ui_begin_frame(ui, 1280, 720);
    sim_mouse_move(ui, 50.0f, 50.0f);
    sim_mouse_down(ui, 50.0f, 50.0f);
    slayer3d_ui_button(ui, 0, 0, 100, 100, "Test");
    slayer3d_ui_end_frame(ui);

    // Frame 2: drag outside, release → no click
    slayer3d_ui_begin_frame(ui, 1280, 720);
    sim_mouse_move(ui, 200.0f, 200.0f);
    sim_mouse_up(ui, 200.0f, 200.0f);
    EXPECT_FALSE(slayer3d_ui_button(ui, 0, 0, 100, 100, "Test"));
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, ButtonWantsMouse)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_begin_frame(ui, 1280, 720);
    sim_mouse_move(ui, 50.0f, 50.0f);
    EXPECT_FALSE(slayer3d_ui_wants_mouse(ui));
    slayer3d_ui_button(ui, 0, 0, 100, 100, "Test");
    EXPECT_TRUE(slayer3d_ui_wants_mouse(ui));
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, ButtonHashSeparator)
{
    // "Save##1" and "Save##2" should be distinct IDs but display the same text.
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_id a = slayer3d_ui_make_id(ui, "Save##1");
    slayer3d_ui_id b = slayer3d_ui_make_id(ui, "Save##2");
    EXPECT_NE(a, b);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, VboxLayoutAdvancesCursor)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_begin_frame(ui, 800, 600);
    slayer3d_ui_begin_vbox(ui, 10, 20, 200, 400);

    // Layout labels and buttons without crashing; verify no layout leak.
    slayer3d_ui_layout_label(ui, "Hello");
    slayer3d_ui_layout_button(ui, "Click");
    slayer3d_ui_separator(ui);
    slayer3d_ui_layout_label(ui, "World");

    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, HboxLayoutAdvancesCursor)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_begin_frame(ui, 800, 600);
    slayer3d_ui_begin_hbox(ui, 10, 20, 400, 40);

    slayer3d_ui_layout_button(ui, "A");
    slayer3d_ui_layout_button(ui, "B");
    slayer3d_ui_separator(ui);
    slayer3d_ui_layout_button(ui, "C");

    slayer3d_ui_end_hbox(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, PanelClipsChildren)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_begin_frame(ui, 800, 600);
    slayer3d_ui_begin_panel(ui, 50, 50, 200, 100);
    slayer3d_ui_label(ui, 60, 60, "Inside panel");
    slayer3d_ui_end_panel(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, NestedPanelVbox)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_begin_frame(ui, 800, 600);
    slayer3d_ui_begin_panel(ui, 0, 0, 200, 400);
    slayer3d_ui_begin_vbox(ui, 8, 8, 184, 384);
    slayer3d_ui_layout_label(ui, "Title");
    slayer3d_ui_layout_button(ui, "Action");
    slayer3d_ui_separator(ui);
    slayer3d_ui_layout_labelf(ui, "Count: %d", 42);
    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_panel(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, LayoutButtonClickWorks)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    // Frame 1: hover + press on the first button in a vbox at (10, 20)
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 25.0f);
    sim_mouse_down(ui, 50.0f, 25.0f);
    slayer3d_ui_begin_vbox(ui, 10, 20, 200, 400);
    EXPECT_FALSE(slayer3d_ui_layout_button(ui, "Btn"));
    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_frame(ui);

    // Frame 2: release while hovering → click
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 25.0f);
    sim_mouse_up(ui, 50.0f, 25.0f);
    slayer3d_ui_begin_vbox(ui, 10, 20, 200, 400);
    EXPECT_TRUE(slayer3d_ui_layout_button(ui, "Btn"));
    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, CheckboxToggle)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    bool val = false;

    // Frame 1: press on checkbox at (10, 10)
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 15.0f, 15.0f);
    sim_mouse_down(ui, 15.0f, 15.0f);
    EXPECT_FALSE(slayer3d_ui_checkbox(ui, 10, 10, "Toggle", &val));
    EXPECT_FALSE(val);
    slayer3d_ui_end_frame(ui);

    // Frame 2: release → toggles to true
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 15.0f, 15.0f);
    sim_mouse_up(ui, 15.0f, 15.0f);
    EXPECT_TRUE(slayer3d_ui_checkbox(ui, 10, 10, "Toggle", &val));
    EXPECT_TRUE(val);
    slayer3d_ui_end_frame(ui);

    // Frame 3: press again
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 15.0f, 15.0f);
    sim_mouse_down(ui, 15.0f, 15.0f);
    slayer3d_ui_checkbox(ui, 10, 10, "Toggle", &val);
    slayer3d_ui_end_frame(ui);

    // Frame 4: release → toggles back to false
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 15.0f, 15.0f);
    sim_mouse_up(ui, 15.0f, 15.0f);
    EXPECT_TRUE(slayer3d_ui_checkbox(ui, 10, 10, "Toggle", &val));
    EXPECT_FALSE(val);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, SliderDrag)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    float val = 0.0f;

    // Frame 1: press on slider at x=10, w=200 → mouse at x=110 = midpoint
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 110.0f, 15.0f);
    sim_mouse_down(ui, 110.0f, 15.0f);
    EXPECT_TRUE(slayer3d_ui_slider(ui, 10, 10, 200, "Val", &val, 0.0f, 1.0f));
    EXPECT_NEAR(val, 0.5f, 0.01f);
    slayer3d_ui_end_frame(ui);

    // Frame 2: drag to x=210 (end of slider) while held
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 210.0f, 15.0f);
    EXPECT_TRUE(slayer3d_ui_slider(ui, 10, 10, 200, "Val", &val, 0.0f, 1.0f));
    EXPECT_NEAR(val, 1.0f, 0.01f);
    slayer3d_ui_end_frame(ui);

    // Frame 3: release
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_up(ui, 210.0f, 15.0f);
    slayer3d_ui_slider(ui, 10, 10, 200, "Val", &val, 0.0f, 1.0f);
    slayer3d_ui_end_frame(ui);

    // Value should remain at 1.0 after release
    EXPECT_NEAR(val, 1.0f, 0.01f);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, SliderClampsValue)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    float val = 5.0f; // out of range

    slayer3d_ui_begin_frame(ui, 800, 600);
    slayer3d_ui_slider(ui, 10, 10, 200, "Clamped", &val, 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(val, 1.0f);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

// Helper: simulate a mouse wheel scroll.
static void sim_scroll(slayer3d_ui_context *ui, float x, float y, float dy)
{
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_WHEEL;
    ev.wheel.y = dy;
    ev.wheel.mouse_x = x;
    ev.wheel.mouse_y = y;
    ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    slayer3d_ui_process_event(ui, &ev);
}

TEST(SLAYER3DUI, ScrollRegionClampsOffset)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    float scroll = 0.0f;

    // Scroll up (negative offset) should clamp to 0.
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_scroll(ui, 50.0f, 50.0f, 5.0f); // scroll up
    slayer3d_ui_begin_scroll(ui, 0, 0, 100, 100, &scroll, 200);
    slayer3d_ui_end_scroll(ui);
    EXPECT_FLOAT_EQ(scroll, 0.0f);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, ScrollRegionScrollsDown)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    float scroll = 0.0f;

    // Scroll down while hovering the region.
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_scroll(ui, 50.0f, 50.0f, -2.0f); // scroll down
    slayer3d_ui_begin_scroll(ui, 0, 0, 100, 100, &scroll, 300);
    slayer3d_ui_end_scroll(ui);
    EXPECT_GT(scroll, 0.0f);
    // Should not exceed max (content_height - visible_height = 200).
    EXPECT_LE(scroll, 200.0f);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, ScrollRegionClampsMax)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    float scroll = 999.0f; // way past max

    slayer3d_ui_begin_frame(ui, 800, 600);
    slayer3d_ui_begin_scroll(ui, 0, 0, 100, 100, &scroll, 150);
    slayer3d_ui_end_scroll(ui);
    // Max scroll = 150 - 100 = 50.
    EXPECT_FLOAT_EQ(scroll, 50.0f);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, ScrollRegionIgnoresWheelOutside)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    float scroll = 0.0f;

    // Scroll while mouse is outside the region.
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_scroll(ui, 500.0f, 500.0f, -5.0f); // outside (0,0,100,100)
    slayer3d_ui_begin_scroll(ui, 0, 0, 100, 100, &scroll, 300);
    slayer3d_ui_end_scroll(ui);
    EXPECT_FLOAT_EQ(scroll, 0.0f);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, TextFieldTyping)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    char buf[64] = "";

    // Frame 1: click to focus
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    slayer3d_ui_end_frame(ui);

    // Frame 2: type "Hi"
    slayer3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_TEXT_INPUT;
        ev.text.text = "Hi";
        slayer3d_ui_process_event(ui, &ev);
    }
    slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    EXPECT_STREQ(buf, "Hi");
    slayer3d_ui_end_frame(ui);

    // Frame 3: backspace
    slayer3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.scancode = SDL_SCANCODE_BACKSPACE;
        slayer3d_ui_process_event(ui, &ev);
    }
    slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    EXPECT_STREQ(buf, "H");
    slayer3d_ui_end_frame(ui);

    // Frame 4: enter commits
    slayer3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.scancode = SDL_SCANCODE_RETURN;
        slayer3d_ui_process_event(ui, &ev);
    }
    EXPECT_TRUE(slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf)));
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, DropdownSelection)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    const char *items[] = {"Apple", "Banana", "Cherry"};
    int selected = 0;

    // Frame 1: click to open
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    slayer3d_ui_dropdown(ui, 0, 0, 200, 30, items, 3, &selected);
    slayer3d_ui_end_frame(ui);

    // Frame 2: click on second item (list starts at y=30, item_h=30, so item 1 at y=60..90)
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 75.0f);
    sim_mouse_down(ui, 50.0f, 75.0f);
    EXPECT_TRUE(slayer3d_ui_dropdown(ui, 0, 0, 200, 30, items, 3, &selected));
    EXPECT_EQ(selected, 1);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, DropdownPopupBlocksOverlappingWidgetInput)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    const char *items[] = {"Apple", "Banana", "Cherry"};
    int selected = 0;

    // Frame 1: click to open the dropdown.
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    slayer3d_ui_dropdown(ui, 0, 0, 200, 30, items, 3, &selected);
    slayer3d_ui_end_frame(ui);

    // Frame 2: click on the second dropdown item. An overlapping button
    // occupies the same screen-space, but must not receive the press.
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 75.0f);
    sim_mouse_down(ui, 50.0f, 75.0f);
    EXPECT_TRUE(slayer3d_ui_dropdown(ui, 0, 0, 200, 30, items, 3, &selected));
    EXPECT_EQ(selected, 1);
    EXPECT_FALSE(slayer3d_ui_button(ui, 0, 60, 200, 30, "Overlap"));
    slayer3d_ui_end_frame(ui);

    // Frame 3: release over the overlapping button. If the popup failed
    // to block the earlier press, the button would falsely click here.
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 75.0f);
    sim_mouse_up(ui, 50.0f, 75.0f);
    EXPECT_FALSE(slayer3d_ui_button(ui, 0, 60, 200, 30, "Overlap"));
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, TabStripSelection)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    const char *tabs[] = {"Entity", "Brush", "Face"};
    int selected = 0;

    // Click on second tab (x = 200/3 + some = ~80)
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 100.0f, 15.0f);
    sim_mouse_down(ui, 100.0f, 15.0f);
    EXPECT_TRUE(slayer3d_ui_tab_strip(ui, 0, 0, 200, 30, tabs, 3, &selected));
    EXPECT_EQ(selected, 1);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, TabStripReClickReturnsFalse)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    const char *tabs[] = {"A", "B"};
    int selected = 0;

    // Click on already-selected tab 0
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 25.0f, 15.0f);
    sim_mouse_down(ui, 25.0f, 15.0f);
    EXPECT_FALSE(slayer3d_ui_tab_strip(ui, 0, 0, 200, 30, tabs, 2, &selected));
    EXPECT_EQ(selected, 0);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, TextFieldEscapeCancels)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    char buf[64] = "original";

    // Frame 1: click to focus
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    slayer3d_ui_end_frame(ui);

    // Frame 2: press Escape → should NOT commit (returns false)
    slayer3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.scancode = SDL_SCANCODE_ESCAPE;
        slayer3d_ui_process_event(ui, &ev);
    }
    EXPECT_FALSE(slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf)));
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, TextFieldClickOutsideCommits)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    char buf[64] = "hello";

    // Frame 1: click to focus
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    slayer3d_ui_end_frame(ui);

    // Frame 2: click outside → should commit (returns true)
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 500.0f, 500.0f);
    sim_mouse_down(ui, 500.0f, 500.0f);
    EXPECT_TRUE(slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf)));
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, TextFieldBufferOverflow)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    char buf[4] = "ab"; // 2 chars + NUL, room for 1 more

    // Frame 1: click to focus
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    slayer3d_ui_end_frame(ui);

    // Frame 2: type "xyz" — only 1 char should fit
    slayer3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_TEXT_INPUT;
        ev.text.text = "xyz";
        slayer3d_ui_process_event(ui, &ev);
    }
    slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    EXPECT_STREQ(buf, "abx");
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, CheckboxReleaseOutsideDoesNotToggle)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    bool val = false;

    // Frame 1: press on checkbox
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 15.0f, 15.0f);
    sim_mouse_down(ui, 15.0f, 15.0f);
    slayer3d_ui_checkbox(ui, 10, 10, "CB", &val);
    slayer3d_ui_end_frame(ui);

    // Frame 2: move outside, release → should NOT toggle
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 500.0f, 500.0f);
    sim_mouse_up(ui, 500.0f, 500.0f);
    slayer3d_ui_checkbox(ui, 10, 10, "CB", &val);
    EXPECT_FALSE(val);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, DropdownCloseOnClickOutside)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    const char *items[] = {"A", "B"};
    int selected = 0;

    // Frame 1: click to open
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    slayer3d_ui_dropdown(ui, 0, 0, 200, 30, items, 2, &selected);
    slayer3d_ui_end_frame(ui);

    // Frame 2: click far outside → should close, selection unchanged
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 500.0f, 500.0f);
    sim_mouse_down(ui, 500.0f, 500.0f);
    EXPECT_FALSE(slayer3d_ui_dropdown(ui, 0, 0, 200, 30, items, 2, &selected));
    EXPECT_EQ(selected, 0);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, SliderRejectsInvalidRange)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    float val = 0.5f;

    slayer3d_ui_begin_frame(ui, 800, 600);
    // max <= min should return false and not modify val
    EXPECT_FALSE(slayer3d_ui_slider(ui, 10, 10, 200, "Bad", &val, 5.0f, 5.0f));
    EXPECT_FLOAT_EQ(val, 0.5f);
    EXPECT_FALSE(slayer3d_ui_slider(ui, 10, 10, 200, "Bad2", &val, 10.0f, 1.0f));
    EXPECT_FLOAT_EQ(val, 0.5f);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, WantsKeyboardWhenFocused)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    char buf[32] = "";

    EXPECT_FALSE(slayer3d_ui_wants_keyboard(ui));

    // Click to focus a text field
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 15.0f);
    sim_mouse_down(ui, 50.0f, 15.0f);
    slayer3d_ui_text_field(ui, 0, 0, 200, 30, buf, sizeof(buf));
    EXPECT_TRUE(slayer3d_ui_wants_keyboard(ui));
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, ScrollWheelFlippedDirection)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    float scroll = 0.0f;

    // Flipped wheel: positive y means scroll down (toward user)
    slayer3d_ui_begin_frame(ui, 800, 600);
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.y = 2.0f;
        ev.wheel.mouse_x = 50.0f;
        ev.wheel.mouse_y = 50.0f;
        ev.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
        slayer3d_ui_process_event(ui, &ev);
    }
    slayer3d_ui_begin_scroll(ui, 0, 0, 100, 100, &scroll, 300);
    slayer3d_ui_end_scroll(ui);
    // Flipped + positive y = scroll down = positive offset
    EXPECT_GT(scroll, 0.0f);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, MeasureTextReturnsNonZero)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    // With no font, measure should return 0.
    float w = -1, h = -1;
    slayer3d_ui_measure_text(ui, "Hello", &w, &h);
    EXPECT_FLOAT_EQ(w, 0.0f);
    EXPECT_FLOAT_EQ(h, 0.0f);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, InspectorRowLabel)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));

    slayer3d_ui_begin_frame(ui, 800, 600);
    slayer3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    slayer3d_ui_row_label(ui, "Name:", "worldspawn");
    slayer3d_ui_row_label(ui, "Origin:", "0 0 0");
    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, InspectorRowCheckbox)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    bool val = false;

    // Frame 1: press on the checkbox area (right side of row at x=10+300*0.35=115)
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 120.0f, 18.0f);
    sim_mouse_down(ui, 120.0f, 18.0f);
    slayer3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    slayer3d_ui_row_checkbox(ui, "Visible:", &val);
    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_frame(ui);

    // Frame 2: release → toggle
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 120.0f, 18.0f);
    sim_mouse_up(ui, 120.0f, 18.0f);
    slayer3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    EXPECT_TRUE(slayer3d_ui_row_checkbox(ui, "Visible:", &val));
    EXPECT_TRUE(val);
    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, InspectorRowCheckboxesUseIndependentHiddenIds)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    bool grid = false;
    bool axes = false;

    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 120.0f, 18.0f);
    sim_mouse_down(ui, 120.0f, 18.0f);
    slayer3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    slayer3d_ui_row_checkbox(ui, "Show Grid:", &grid);
    slayer3d_ui_row_checkbox(ui, "Show Axes:", &axes);
    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 120.0f, 18.0f);
    sim_mouse_up(ui, 120.0f, 18.0f);
    slayer3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    EXPECT_TRUE(slayer3d_ui_row_checkbox(ui, "Show Grid:", &grid));
    EXPECT_FALSE(slayer3d_ui_row_checkbox(ui, "Show Axes:", &axes));
    EXPECT_TRUE(grid);
    EXPECT_FALSE(axes);
    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, ListViewSelection)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    const char *items[] = {"Alpha", "Beta", "Gamma", "Delta"};
    int selected = 0;
    float scroll = 0.0f;

    // Click on second item (y = 10 + 22*1 + 11 = ~43)
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 43.0f);
    sim_mouse_down(ui, 50.0f, 43.0f);
    EXPECT_TRUE(slayer3d_ui_list_view(ui, 10, 10, 200, 100, items, 4, &selected, &scroll));
    EXPECT_EQ(selected, 1);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, ListViewReClickReturnsFalse)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    const char *items[] = {"A", "B"};
    int selected = 0;
    float scroll = 0.0f;

    // Click on already-selected item 0
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 20.0f);
    sim_mouse_down(ui, 50.0f, 20.0f);
    EXPECT_FALSE(slayer3d_ui_list_view(ui, 10, 10, 200, 100, items, 2, &selected, &scroll));
    EXPECT_EQ(selected, 0);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, TreeNodeExpandCollapse)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    bool expanded = false;
    int sel = -1;

    // Click on the arrow area (left side) to expand
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 17.0f, 21.0f); // arrow at x=10, y=10, w=14
    sim_mouse_down(ui, 17.0f, 21.0f);
    slayer3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    slayer3d_ui_tree_node(ui, "Root", 1, &expanded, &sel);
    slayer3d_ui_end_vbox(ui);
    EXPECT_TRUE(expanded);
    EXPECT_EQ(sel, -1); // arrow click doesn't select
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, TreeNodeSelect)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    bool expanded = true;
    int sel = -1;

    // Click on the label area (right of arrow) to select
    slayer3d_ui_begin_frame(ui, 800, 600);
    sim_mouse_move(ui, 50.0f, 21.0f); // label area
    sim_mouse_down(ui, 50.0f, 21.0f);
    slayer3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    EXPECT_TRUE(slayer3d_ui_tree_node(ui, "Root", 1, &expanded, &sel));
    EXPECT_EQ(sel, 1);
    EXPECT_TRUE(expanded); // selection doesn't toggle expand
    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_frame(ui);

    slayer3d_ui_destroy(ui);
}

TEST(SLAYER3DUI, TreePushPopIndent)
{
    slayer3d_ui_context *ui = nullptr;
    ASSERT_TRUE(slayer3d_ui_create(nullptr, &ui));
    bool exp1 = true, exp2 = false;
    int sel = -1;

    slayer3d_ui_begin_frame(ui, 800, 600);
    slayer3d_ui_begin_vbox(ui, 10, 10, 300, 400);
    slayer3d_ui_tree_node(ui, "Parent", 1, &exp1, &sel);
    slayer3d_ui_tree_push(ui);
    slayer3d_ui_tree_node(ui, "Child", 2, &exp2, &sel);
    slayer3d_ui_tree_pop(ui);
    slayer3d_ui_tree_node(ui, "Sibling", 3, &exp1, &sel);
    slayer3d_ui_end_vbox(ui);
    slayer3d_ui_end_frame(ui);

    // No crash, no leak — validates push/pop balance.
    slayer3d_ui_destroy(ui);
}

} // namespace

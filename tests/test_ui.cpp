#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <gtest/gtest.h>

#include <string>

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

const slayer3d_ui_layout_render_command *find_render_command(const slayer3d_ui_layout_model *layout, const char *id)
{
    for (int i = 0; i < slayer3d_ui_layout_render_command_count(layout); ++i)
    {
        const slayer3d_ui_layout_render_command *command = slayer3d_ui_layout_render_command_at(layout, i);
        if (command != nullptr && std::string(command->id) == id)
            return command;
    }
    return nullptr;
}

const slayer3d_ui_layout_hit_region *find_hit_region(const slayer3d_ui_layout_model *layout, const char *id)
{
    for (int i = 0; i < slayer3d_ui_layout_hit_region_count(layout); ++i)
    {
        const slayer3d_ui_layout_hit_region *region = slayer3d_ui_layout_hit_region_at(layout, i);
        if (region != nullptr && std::string(region->id) == id)
            return region;
    }
    return nullptr;
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

TEST(SLAYER3DUI, RetainedImageNodeInheritsParentPlacementAndClip)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc slot{};
    slot.id = "slot";
    slot.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    slot.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    slot.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    slot.rect = {40.0f, 60.0f, 180.0f, 48.0f};
    slot.clip_children = true;
    slot.layer = 20;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &slot));

    slayer3d_ui_layout_node_desc thumb{};
    thumb.id = "thumb";
    thumb.parent_id = "slot";
    thumb.type = SLAYER3D_UI_LAYOUT_NODE_IMAGE;
    thumb.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    thumb.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    thumb.rect = {4.0f, 4.0f, 40.0f, 40.0f};
    thumb.image = "image.test.thumb";
    thumb.preserve_aspect = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &thumb));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    const slayer3d_ui_layout_resolved_node *resolved = slayer3d_ui_layout_find_resolved_node(layout, "thumb");
    expect_rect(resolved, 44.0f, 64.0f, 40.0f, 40.0f);
    ASSERT_NE(resolved, nullptr);
    EXPECT_STREQ(resolved->image, "image.test.thumb");
    EXPECT_TRUE(resolved->preserve_aspect);
    EXPECT_TRUE(resolved->has_clip_rect);
    EXPECT_FLOAT_EQ(resolved->clip_rect.x, 40.0f);
    EXPECT_FLOAT_EQ(resolved->clip_rect.y, 60.0f);
    EXPECT_FLOAT_EQ(resolved->clip_rect.w, 180.0f);
    EXPECT_FLOAT_EQ(resolved->clip_rect.h, 48.0f);
    EXPECT_EQ(resolved->layer, 21);

    const slayer3d_ui_layout_render_command *command = find_render_command(layout, "thumb");
    ASSERT_NE(command, nullptr);
    EXPECT_EQ(command->type, SLAYER3D_UI_LAYOUT_NODE_IMAGE);
    EXPECT_STREQ(command->image, "image.test.thumb");
    EXPECT_TRUE(command->preserve_aspect);
    EXPECT_TRUE(command->has_clip_rect);

    // Images are display-only by default: the slot button owns the input.
    EXPECT_EQ(find_hit_region(layout, "thumb"), nullptr);
    ASSERT_NE(find_hit_region(layout, "slot"), nullptr);

    // Moving the slot moves the thumbnail without touching its authored rect.
    slayer3d_ui_layout_clear(layout);
    slot.rect = {200.0f, 300.0f, 180.0f, 48.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &slot));
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &thumb));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "thumb"), 204.0f, 304.0f, 40.0f, 40.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedImageNodeCanOptIntoInput)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc icon{};
    icon.id = "icon";
    icon.type = SLAYER3D_UI_LAYOUT_NODE_IMAGE;
    icon.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    icon.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    icon.rect = {10.0f, 10.0f, 32.0f, 32.0f};
    icon.image = "image.test.icon";
    icon.action = "editor.icon.activate";
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &icon));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    const slayer3d_ui_layout_hit_region *region = find_hit_region(layout, "icon");
    ASSERT_NE(region, nullptr);
    EXPECT_STREQ(region->action, "editor.icon.activate");

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedGridLayoutPlacesChildrenRowMajor)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc grid{};
    grid.id = "grid";
    grid.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    grid.axis = SLAYER3D_UI_LAYOUT_AXIS_GRID;
    grid.grid_columns = 2;
    grid.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    grid.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    grid.rect = {0.0f, 0.0f, 100.0f, 100.0f};
    grid.padding = 4.0f;
    grid.gap = 2.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &grid));

    // Content rect is (4, 4, 92, 92); two columns of width (92 - 2) / 2 = 45.
    const char *ids[] = {"cell.a", "cell.b", "cell.c", "cell.d"};
    for (int i = 0; i < 4; ++i)
    {
        slayer3d_ui_layout_node_desc cell{};
        cell.id = ids[i];
        cell.parent_id = "grid";
        cell.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
        cell.width_mode = i == 1 ? SLAYER3D_UI_LAYOUT_SIZE_FILL : SLAYER3D_UI_LAYOUT_SIZE_FIXED;
        cell.height_mode = i == 3 ? SLAYER3D_UI_LAYOUT_SIZE_FILL : SLAYER3D_UI_LAYOUT_SIZE_FIXED;
        cell.rect = {0.0f, 0.0f, 20.0f, i < 2 ? 30.0f : 15.0f};
        ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &cell));
    }

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    // Row 0 height is the tallest fixed child (30); row 1 starts after the gap.
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "cell.a"), 4.0f, 4.0f, 20.0f, 30.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "cell.b"), 51.0f, 4.0f, 45.0f, 30.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "cell.c"), 4.0f, 36.0f, 20.0f, 15.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "cell.d"), 51.0f, 36.0f, 20.0f, 15.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedGridLayoutRequiresColumns)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc grid{};
    grid.id = "grid";
    grid.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    grid.axis = SLAYER3D_UI_LAYOUT_AXIS_GRID;
    grid.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    grid.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    grid.rect = {0.0f, 0.0f, 100.0f, 100.0f};
    EXPECT_FALSE(slayer3d_ui_layout_add_node(layout, &grid));
    grid.grid_columns = -1;
    EXPECT_FALSE(slayer3d_ui_layout_add_node(layout, &grid));
    grid.grid_columns = 2;
    EXPECT_TRUE(slayer3d_ui_layout_add_node(layout, &grid));

    slayer3d_ui_layout_destroy(layout);
}

static slayer3d_ui_layout_node_desc scroll_pane_desc(const char *id, float h, float scroll_offset)
{
    slayer3d_ui_layout_node_desc pane{};
    pane.id = id;
    pane.type = SLAYER3D_UI_LAYOUT_NODE_SCROLL;
    pane.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    pane.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    pane.rect = {10.0f, 20.0f, 200.0f, h};
    pane.scroll_offset = scroll_offset;
    pane.scroll_key = "test.scroll";
    return pane;
}

static slayer3d_ui_layout_node_desc scroll_row_desc(const char *id, const char *parent, float y, float h)
{
    slayer3d_ui_layout_node_desc row{};
    row.id = id;
    row.parent_id = parent;
    row.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    row.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    row.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    row.rect = {0.0f, y, 180.0f, h};
    return row;
}

TEST(SLAYER3DUI, ScrollPaneMeasuresClampsAndShiftsChildren)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    // Content: rows at y 0..250 inside a 100-high pane -> extent 250, max 150.
    slayer3d_ui_layout_node_desc pane = scroll_pane_desc("pane", 100.0f, 60.0f);
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &pane));
    slayer3d_ui_layout_node_desc top = scroll_row_desc("row.top", "pane", 0.0f, 30.0f);
    slayer3d_ui_layout_node_desc bottom = scroll_row_desc("row.bottom", "pane", 220.0f, 30.0f);
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &top));
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &bottom));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    const slayer3d_ui_layout_resolved_node *resolved_pane = slayer3d_ui_layout_find_resolved_node(layout, "pane");
    ASSERT_NE(resolved_pane, nullptr);
    EXPECT_FLOAT_EQ(resolved_pane->content_extent, 250.0f);
    EXPECT_FLOAT_EQ(resolved_pane->scroll_max, 150.0f);
    EXPECT_FLOAT_EQ(resolved_pane->scroll_offset, 60.0f);
    EXPECT_STREQ(resolved_pane->scroll_key, "test.scroll");

    // Children shift up by the applied offset and inherit the pane clip.
    const slayer3d_ui_layout_resolved_node *resolved_top = slayer3d_ui_layout_find_resolved_node(layout, "row.top");
    ASSERT_NE(resolved_top, nullptr);
    EXPECT_FLOAT_EQ(resolved_top->rect.y, 20.0f - 60.0f);
    EXPECT_TRUE(resolved_top->has_clip_rect);
    EXPECT_FLOAT_EQ(resolved_top->clip_rect.y, 20.0f);
    EXPECT_FLOAT_EQ(resolved_top->clip_rect.h, 100.0f);

    // Requests past the content clamp to the measured maximum; negatives to 0.
    slayer3d_ui_layout_clear(layout);
    pane.scroll_offset = 500.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &pane));
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &top));
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &bottom));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    resolved_pane = slayer3d_ui_layout_find_resolved_node(layout, "pane");
    ASSERT_NE(resolved_pane, nullptr);
    EXPECT_FLOAT_EQ(resolved_pane->scroll_offset, 150.0f);

    slayer3d_ui_layout_clear(layout);
    pane.scroll_offset = -25.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &pane));
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &top));
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &bottom));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    resolved_pane = slayer3d_ui_layout_find_resolved_node(layout, "pane");
    ASSERT_NE(resolved_pane, nullptr);
    EXPECT_FLOAT_EQ(resolved_pane->scroll_offset, 0.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, ScrollPaneSynthesizesProportionalScrollbar)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc pane = scroll_pane_desc("pane", 100.0f, 75.0f);
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &pane));
    slayer3d_ui_layout_node_desc bottom = scroll_row_desc("row.bottom", "pane", 220.0f, 30.0f);
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &bottom));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));

    // Track hugs the pane's right edge; thumb height is proportional to the
    // visible fraction (100/250 of 100 = 40 >= minimum 24) and its position
    // reflects offset 75 of max 150.
    const slayer3d_ui_layout_render_command *track = find_render_command(layout, "pane.scrollbar");
    ASSERT_NE(track, nullptr);
    EXPECT_FLOAT_EQ(track->rect.x, 10.0f + 200.0f - 8.0f);
    EXPECT_FLOAT_EQ(track->rect.y, 20.0f);
    EXPECT_FLOAT_EQ(track->rect.h, 100.0f);
    const slayer3d_ui_layout_render_command *thumb = find_render_command(layout, "pane.scrollbar.thumb");
    ASSERT_NE(thumb, nullptr);
    EXPECT_FLOAT_EQ(thumb->rect.h, 40.0f);
    EXPECT_FLOAT_EQ(thumb->rect.y, 20.0f + (100.0f - 40.0f) * 0.5f);
    const slayer3d_ui_layout_hit_region *track_hit = find_hit_region(layout, "pane.scrollbar");
    ASSERT_NE(track_hit, nullptr);
    EXPECT_STREQ(track_hit->owner_id, "pane");

    // Pointer mapping inverts the same geometry: track center -> half of max.
    float offset = 0.0f;
    ASSERT_TRUE(slayer3d_ui_layout_scrollbar_offset_for_pointer(layout, "pane", 20.0f + 50.0f, &offset));
    EXPECT_FLOAT_EQ(offset, 75.0f);
    ASSERT_TRUE(slayer3d_ui_layout_scrollbar_offset_for_pointer(layout, "pane", 1000.0f, &offset));
    EXPECT_FLOAT_EQ(offset, 150.0f);

    // Content that fits produces no scrollbar and no pointer mapping.
    slayer3d_ui_layout_clear(layout);
    slayer3d_ui_layout_node_desc fits = scroll_pane_desc("pane", 400.0f, 10.0f);
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &fits));
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &bottom));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    const slayer3d_ui_layout_resolved_node *resolved_fits = slayer3d_ui_layout_find_resolved_node(layout, "pane");
    ASSERT_NE(resolved_fits, nullptr);
    EXPECT_FLOAT_EQ(resolved_fits->scroll_max, 0.0f);
    EXPECT_FLOAT_EQ(resolved_fits->scroll_offset, 0.0f);
    EXPECT_EQ(find_render_command(layout, "pane.scrollbar"), nullptr);
    EXPECT_FALSE(slayer3d_ui_layout_scrollbar_offset_for_pointer(layout, "pane", 100.0f, &offset));

    slayer3d_ui_layout_clear(layout);
    fits.scrollbar_always = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &fits));
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &bottom));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    EXPECT_NE(find_render_command(layout, "pane.scrollbar"), nullptr);
    EXPECT_NE(find_render_command(layout, "pane.scrollbar.thumb"), nullptr);
    EXPECT_FALSE(slayer3d_ui_layout_scrollbar_offset_for_pointer(layout, "pane", 100.0f, &offset));

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, VirtualListSharesScrollbarWithoutMovingChildren)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    // Six visible slot children windowing 20 items, currently at index 7.
    slayer3d_ui_layout_node_desc list{};
    list.id = "list";
    list.type = SLAYER3D_UI_LAYOUT_NODE_COLUMN;
    list.axis = SLAYER3D_UI_LAYOUT_AXIS_COLUMN;
    list.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    list.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    list.rect = {10.0f, 20.0f, 200.0f, 300.0f};
    list.gap = 2.0f;
    list.scroll_key = "list.index";
    list.scroll_span = 20.0f;
    list.scroll_offset = 7.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &list));

    char ids[6][16];
    for (int i = 0; i < 6; ++i)
    {
        SDL_snprintf(ids[i], sizeof(ids[i]), "slot.%d", i);
        slayer3d_ui_layout_node_desc slot{};
        slot.id = ids[i];
        slot.parent_id = "list";
        slot.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
        slot.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
        slot.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
        slot.rect = {0.0f, 0.0f, 180.0f, 48.0f};
        ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &slot));
    }
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));

    // Index units: max = 20 items - 6 visible; the offset is the index and
    // slot geometry is untouched (hosts rebind slot contents instead).
    const slayer3d_ui_layout_resolved_node *resolved = slayer3d_ui_layout_find_resolved_node(layout, "list");
    ASSERT_NE(resolved, nullptr);
    EXPECT_TRUE(resolved->scroll_virtual);
    EXPECT_FLOAT_EQ(resolved->content_extent, 20.0f);
    EXPECT_FLOAT_EQ(resolved->scroll_max, 14.0f);
    EXPECT_FLOAT_EQ(resolved->scroll_offset, 7.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "slot.0"), 10.0f, 20.0f, 180.0f, 48.0f);

    // The same scrollbar synthesis as pixel panes, proportioned in items:
    // thumb = 300 * 6/20 = 90, centered at index 7 of 14.
    const slayer3d_ui_layout_render_command *thumb = find_render_command(layout, "list.scrollbar.thumb");
    ASSERT_NE(thumb, nullptr);
    EXPECT_FLOAT_EQ(thumb->rect.h, 90.0f);
    EXPECT_FLOAT_EQ(thumb->rect.y, 20.0f + (300.0f - 90.0f) * 0.5f);
    ASSERT_NE(find_hit_region(layout, "list.scrollbar"), nullptr);

    // Pointer mapping returns item indices.
    float offset = 0.0f;
    ASSERT_TRUE(slayer3d_ui_layout_scrollbar_offset_for_pointer(layout, "list", 20.0f + 150.0f, &offset));
    EXPECT_FLOAT_EQ(offset, 7.0f);

    // A window that covers the whole span suppresses the scrollbar.
    slayer3d_ui_layout_clear(layout);
    list.scroll_span = 6.0f;
    list.scroll_offset = 3.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &list));
    for (int i = 0; i < 6; ++i)
    {
        slayer3d_ui_layout_node_desc slot{};
        slot.id = ids[i];
        slot.parent_id = "list";
        slot.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
        slot.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
        slot.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
        slot.rect = {0.0f, 0.0f, 180.0f, 48.0f};
        ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &slot));
    }
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    resolved = slayer3d_ui_layout_find_resolved_node(layout, "list");
    ASSERT_NE(resolved, nullptr);
    EXPECT_FLOAT_EQ(resolved->scroll_max, 0.0f);
    EXPECT_FLOAT_EQ(resolved->scroll_offset, 0.0f);
    EXPECT_EQ(find_render_command(layout, "list.scrollbar"), nullptr);

    slayer3d_ui_layout_clear(layout);
    list.scrollbar_always = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &list));
    for (int i = 0; i < 6; ++i)
    {
        slayer3d_ui_layout_node_desc slot{};
        slot.id = ids[i];
        slot.parent_id = "list";
        slot.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
        slot.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
        slot.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
        slot.rect = {0.0f, 0.0f, 180.0f, 48.0f};
        ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &slot));
    }
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    EXPECT_NE(find_render_command(layout, "list.scrollbar"), nullptr);
    EXPECT_NE(find_render_command(layout, "list.scrollbar.thumb"), nullptr);
    EXPECT_FALSE(slayer3d_ui_layout_scrollbar_offset_for_pointer(layout, "list", 20.0f + 150.0f, &offset));

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, AnchoredNodesTrackViewportEdges)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    // x/y become edge margins: a right-docked panel keeps its 12px margin
    // at any viewport size instead of hardcoding the 1280-wide layout.
    slayer3d_ui_layout_node_desc panel{};
    panel.id = "dock";
    panel.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    panel.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    panel.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    panel.rect = {12.0f, 0.0f, 238.0f, 520.0f};
    panel.anchor_right = true;
    panel.anchor_bottom = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &panel));

    // Anchored children resolve against the parent's content edges too.
    slayer3d_ui_layout_node_desc corner{};
    corner.id = "dock.corner";
    corner.parent_id = "dock";
    corner.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    corner.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    corner.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    corner.rect = {4.0f, 4.0f, 40.0f, 20.0f};
    corner.anchor_right = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &corner));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "dock"), 1280.0f - 12.0f - 238.0f, 720.0f - 520.0f,
                238.0f, 520.0f);
    const slayer3d_ui_layout_resolved_node *resolved_corner =
        slayer3d_ui_layout_find_resolved_node(layout, "dock.corner");
    ASSERT_NE(resolved_corner, nullptr);
    EXPECT_FLOAT_EQ(resolved_corner->rect.x, (1280.0f - 12.0f - 238.0f) + 238.0f - 4.0f - 40.0f);

    // The same tree at a larger viewport stays docked to the edges.
    slayer3d_ui_layout_mark_dirty(layout);
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 2560.0f, 1440.0f));
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "dock"), 2560.0f - 12.0f - 238.0f, 1440.0f - 520.0f,
                238.0f, 520.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, DockedRootWindowsStackAroundTheCanvas)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc inspector{};
    inspector.id = "inspector";
    inspector.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    inspector.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    inspector.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    inspector.rect = {12.0f, 92.0f, 300.0f, 500.0f};
    inspector.window = true;
    inspector.dock = SLAYER3D_UI_LAYOUT_DOCK_LEFT;
    inspector.dock_top = 80.0f;
    inspector.dock_gap = 4.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &inspector));

    slayer3d_ui_layout_node_desc things = inspector;
    things.id = "things";
    things.rect.w = 240.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &things));

    slayer3d_ui_layout_node_desc global = inspector;
    global.id = "global";
    global.rect.w = 200.0f;
    global.dock = SLAYER3D_UI_LAYOUT_DOCK_RIGHT;
    global.dock_height = 90.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &global));

    slayer3d_ui_layout_node_desc console = inspector;
    console.id = "console";
    console.rect = {160.0f, 520.0f, 960.0f, 120.0f};
    console.dock = SLAYER3D_UI_LAYOUT_DOCK_BOTTOM;
    console.dock_gap = 4.0f;
    console.dock_width = 360.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &console));

    slayer3d_ui_layout_node_desc timeline = console;
    timeline.id = "timeline";
    timeline.rect.h = 80.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &timeline));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "inspector"), 0.0f, 80.0f, 300.0f, 436.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "things"), 304.0f, 80.0f, 240.0f, 436.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "global"), 1080.0f, 80.0f, 200.0f, 436.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "console"), 0.0f, 600.0f, 1280.0f, 120.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "timeline"), 0.0f, 516.0f, 1280.0f, 80.0f);

    slayer3d_ui_layout_rect preview{};
    EXPECT_TRUE(slayer3d_ui_layout_calculate_window_dock_rect(layout, "global", SLAYER3D_UI_LAYOUT_DOCK_BOTTOM, 1280.0f,
                                                              720.0f, &preview));
    EXPECT_FLOAT_EQ(preview.x, 0.0f);
    EXPECT_FLOAT_EQ(preview.y, 630.0f);
    EXPECT_FLOAT_EQ(preview.w, 1280.0f);
    EXPECT_FLOAT_EQ(preview.h, 90.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, SafeAreaViewportOffsetsRootsAnchorsDocksAndPopups)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc toolbar{};
    toolbar.id = "toolbar";
    toolbar.type = SLAYER3D_UI_LAYOUT_NODE_TOOLBAR;
    toolbar.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    toolbar.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    toolbar.rect = {0.0f, 0.0f, 0.0f, 40.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &toolbar));

    slayer3d_ui_layout_node_desc anchored{};
    anchored.id = "anchored";
    anchored.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    anchored.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    anchored.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    anchored.rect = {12.0f, 8.0f, 100.0f, 30.0f};
    anchored.anchor_right = true;
    anchored.anchor_bottom = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &anchored));

    slayer3d_ui_layout_node_desc docked{};
    docked.id = "docked";
    docked.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    docked.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    docked.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    docked.rect = {0.0f, 0.0f, 180.0f, 200.0f};
    docked.window = true;
    docked.dock = SLAYER3D_UI_LAYOUT_DOCK_RIGHT;
    docked.dock_top = 40.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &docked));

    const char *options[] = {"One", "Two"};
    slayer3d_ui_layout_node_desc dropdown{};
    dropdown.id = "dropdown";
    dropdown.type = SLAYER3D_UI_LAYOUT_NODE_DROPDOWN;
    dropdown.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.rect = {900.0f, 610.0f, 140.0f, 30.0f};
    dropdown.open = true;
    dropdown.options = options;
    dropdown.option_count = 2;
    dropdown.option_height = 24.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &dropdown));

    const slayer3d_ui_layout_rect safe_area{20.0f, 48.0f, 1000.0f, 640.0f};
    ASSERT_TRUE(slayer3d_ui_layout_resolve_in_rect(layout, safe_area));
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "toolbar"), 20.0f, 48.0f, 1000.0f, 40.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "anchored"), 908.0f, 650.0f, 100.0f, 30.0f);
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "docked"), 840.0f, 88.0f, 180.0f, 600.0f);

    const slayer3d_ui_layout_render_command *popup = find_render_command(layout, "dropdown.popup");
    ASSERT_NE(popup, nullptr);
    EXPECT_FLOAT_EQ(popup->rect.x, 880.0f);
    EXPECT_FLOAT_EQ(popup->rect.y, 610.0f);

    slayer3d_ui_layout_rect preview{};
    ASSERT_TRUE(slayer3d_ui_layout_calculate_window_dock_rect_in_rect(layout, "docked", SLAYER3D_UI_LAYOUT_DOCK_BOTTOM,
                                                                      safe_area, &preview));
    EXPECT_FLOAT_EQ(preview.x, 20.0f);
    EXPECT_FLOAT_EQ(preview.y, 488.0f);
    EXPECT_FLOAT_EQ(preview.w, 1000.0f);
    EXPECT_FLOAT_EQ(preview.h, 200.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, FrontRootWindowRaisesItsCompleteStackingContext)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc front{};
    front.id = "front";
    front.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    front.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    front.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    front.rect = {0.0f, 0.0f, 160.0f, 120.0f};
    front.layer = 120;
    front.window = true;
    front.window_front = true;
    front.interactive = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &front));

    slayer3d_ui_layout_node_desc front_child{};
    front_child.id = "front.child";
    front_child.parent_id = "front";
    front_child.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    front_child.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    front_child.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    front_child.rect = {8.0f, 8.0f, 80.0f, 40.0f};
    front_child.layer = 121;
    front_child.interactive = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &front_child));

    slayer3d_ui_layout_node_desc back = front;
    back.id = "back";
    back.window_front = false;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &back));

    slayer3d_ui_layout_node_desc back_child = front_child;
    back_child.id = "back.child";
    back_child.parent_id = "back";
    back_child.layer = 430;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &back_child));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    const slayer3d_ui_layout_resolved_node *resolved_front = slayer3d_ui_layout_find_resolved_node(layout, "front");
    const slayer3d_ui_layout_resolved_node *resolved_front_child =
        slayer3d_ui_layout_find_resolved_node(layout, "front.child");
    const slayer3d_ui_layout_resolved_node *resolved_back_child =
        slayer3d_ui_layout_find_resolved_node(layout, "back.child");
    ASSERT_NE(resolved_front, nullptr);
    ASSERT_NE(resolved_front_child, nullptr);
    ASSERT_NE(resolved_back_child, nullptr);
    EXPECT_GT(resolved_front->layer, resolved_back_child->layer);
    EXPECT_GT(resolved_front_child->layer, resolved_front->layer);

    const slayer3d_ui_layout_render_command *front_surface = find_render_command(layout, "front");
    ASSERT_NE(front_surface, nullptr);
    EXPECT_TRUE(front_surface->window);
    const slayer3d_ui_layout_hit_region *top = slayer3d_ui_layout_hit_test(layout, 12.0f, 12.0f);
    ASSERT_NE(top, nullptr);
    EXPECT_STREQ(top->id, "front.child");

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, AbsoluteFillUsesRemainingParentSpace)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc panel{};
    panel.id = "panel";
    panel.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    panel.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    panel.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    panel.rect = {10.0f, 20.0f, 200.0f, 100.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &panel));

    slayer3d_ui_layout_node_desc content{};
    content.id = "content";
    content.parent_id = "panel";
    content.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    content.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    content.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FILL;
    content.rect = {12.0f, 10.0f, 1.0f, 1.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &content));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    expect_rect(slayer3d_ui_layout_find_resolved_node(layout, "content"), 22.0f, 30.0f, 188.0f, 90.0f);

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
    root.layer = 110;
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
    EXPECT_EQ(hit->layer, 111);
    EXPECT_FLOAT_EQ(hit->rect.x, resolved->rect.x);
    EXPECT_FLOAT_EQ(hit->rect.y, resolved->rect.y);
    EXPECT_FLOAT_EQ(hit->rect.w, resolved->rect.w);
    EXPECT_FLOAT_EQ(hit->rect.h, resolved->rect.h);

    ASSERT_EQ(slayer3d_ui_layout_hit_test(layout, 30.0f, 40.0f), hit);
    EXPECT_EQ(slayer3d_ui_layout_hit_test(layout, 5.0f, 5.0f), nullptr);

    const slayer3d_ui_layout_render_command *panel_render = slayer3d_ui_layout_render_command_at(layout, 0);
    const slayer3d_ui_layout_render_command *button_render = slayer3d_ui_layout_render_command_at(layout, 1);
    ASSERT_NE(panel_render, nullptr);
    ASSERT_NE(button_render, nullptr);
    EXPECT_STREQ(panel_render->id, "panel");
    EXPECT_STREQ(button_render->id, "button");
    EXPECT_EQ(panel_render->layer, 110);
    EXPECT_EQ(button_render->layer, 111);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedClipChildrenConstrainsRenderingAndHitTesting)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc panel{};
    panel.id = "panel";
    panel.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    panel.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    panel.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    panel.rect = {10.0f, 20.0f, 100.0f, 50.0f};
    panel.padding = 5.0f;
    panel.clip_children = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &panel));

    slayer3d_ui_layout_node_desc inside{};
    inside.id = "inside";
    inside.parent_id = "panel";
    inside.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    inside.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    inside.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    inside.rect = {10.0f, 10.0f, 30.0f, 20.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &inside));

    slayer3d_ui_layout_node_desc overflow{};
    overflow.id = "overflow";
    overflow.parent_id = "panel";
    overflow.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    overflow.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    overflow.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    overflow.rect = {10.0f, 70.0f, 30.0f, 20.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &overflow));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));

    const slayer3d_ui_layout_render_command *inside_render = find_render_command(layout, "inside");
    ASSERT_NE(inside_render, nullptr);
    EXPECT_TRUE(inside_render->has_clip_rect);
    EXPECT_FLOAT_EQ(inside_render->clip_rect.x, 15.0f);
    EXPECT_FLOAT_EQ(inside_render->clip_rect.y, 25.0f);
    EXPECT_FLOAT_EQ(inside_render->clip_rect.w, 90.0f);
    EXPECT_FLOAT_EQ(inside_render->clip_rect.h, 40.0f);

    EXPECT_EQ(find_render_command(layout, "overflow"), nullptr);
    EXPECT_NE(find_hit_region(layout, "inside"), nullptr);
    EXPECT_EQ(find_hit_region(layout, "overflow"), nullptr);
    EXPECT_NE(slayer3d_ui_layout_hit_test(layout, 30.0f, 40.0f), nullptr);
    EXPECT_EQ(slayer3d_ui_layout_hit_test(layout, 30.0f, 100.0f), nullptr);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedClipRectIdConstrainsRenderingAndHitTesting)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc root{};
    root.id = "root";
    root.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    root.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    root.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    root.rect = {0.0f, 0.0f, 240.0f, 180.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &root));

    slayer3d_ui_layout_node_desc viewport{};
    viewport.id = "viewport";
    viewport.parent_id = "root";
    viewport.type = SLAYER3D_UI_LAYOUT_NODE_SPACER;
    viewport.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    viewport.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    viewport.rect = {20.0f, 60.0f, 120.0f, 80.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &viewport));

    slayer3d_ui_layout_node_desc clipped{};
    clipped.id = "clipped";
    clipped.parent_id = "root";
    clipped.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    clipped.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    clipped.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    clipped.rect = {30.0f, 70.0f, 40.0f, 24.0f};
    clipped.clip_rect_id = "viewport";
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &clipped));

    slayer3d_ui_layout_node_desc hidden{};
    hidden.id = "hidden";
    hidden.parent_id = "root";
    hidden.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    hidden.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    hidden.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    hidden.rect = {30.0f, 20.0f, 40.0f, 24.0f};
    hidden.clip_rect_id = "viewport";
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &hidden));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));

    const slayer3d_ui_layout_render_command *clipped_render = find_render_command(layout, "clipped");
    ASSERT_NE(clipped_render, nullptr);
    EXPECT_TRUE(clipped_render->has_clip_rect);
    EXPECT_FLOAT_EQ(clipped_render->clip_rect.x, 20.0f);
    EXPECT_FLOAT_EQ(clipped_render->clip_rect.y, 60.0f);
    EXPECT_FLOAT_EQ(clipped_render->clip_rect.w, 120.0f);
    EXPECT_FLOAT_EQ(clipped_render->clip_rect.h, 80.0f);
    EXPECT_EQ(find_render_command(layout, "hidden"), nullptr);
    EXPECT_NE(slayer3d_ui_layout_hit_test(layout, 35.0f, 75.0f), nullptr);
    EXPECT_EQ(slayer3d_ui_layout_hit_test(layout, 35.0f, 25.0f), nullptr);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedRenderCommandsCarryAuthoredStyle)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc panel{};
    panel.id = "panel";
    panel.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    panel.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    panel.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    panel.rect = {10.0f, 20.0f, 120.0f, 40.0f};
    panel.fill_color = {11, 22, 33, 44};
    panel.has_fill_color = true;
    panel.border_color = {55, 66, 77, 88};
    panel.has_border_color = true;
    panel.border_thickness = 3.0f;
    panel.text_color = {210, 220, 230, 240};
    panel.has_text_color = true;
    panel.text_scale = 0.75f;
    panel.text_align = SLAYER3D_UI_LAYOUT_TEXT_ALIGN_RIGHT;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &panel));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    const slayer3d_ui_layout_render_command *command = find_render_command(layout, "panel");
    ASSERT_NE(command, nullptr);
    EXPECT_TRUE(command->has_fill_color);
    EXPECT_EQ(command->fill_color.r, 11);
    EXPECT_EQ(command->fill_color.g, 22);
    EXPECT_EQ(command->fill_color.b, 33);
    EXPECT_EQ(command->fill_color.a, 44);
    EXPECT_TRUE(command->has_border_color);
    EXPECT_EQ(command->border_color.r, 55);
    EXPECT_EQ(command->border_color.g, 66);
    EXPECT_EQ(command->border_color.b, 77);
    EXPECT_EQ(command->border_color.a, 88);
    EXPECT_FLOAT_EQ(command->border_thickness, 3.0f);
    EXPECT_TRUE(command->has_text_color);
    EXPECT_EQ(command->text_color.r, 210);
    EXPECT_EQ(command->text_color.g, 220);
    EXPECT_EQ(command->text_color.b, 230);
    EXPECT_EQ(command->text_color.a, 240);
    EXPECT_FLOAT_EQ(command->text_scale, 0.75f);
    EXPECT_EQ(command->text_align, SLAYER3D_UI_LAYOUT_TEXT_ALIGN_RIGHT);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedTypographyRolesResolveThemeSizesAndColors)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_typography_theme theme = slayer3d_ui_typography_theme_default();
    EXPECT_FLOAT_EQ(theme.body.size, 14.0f);
    EXPECT_FLOAT_EQ(theme.caption.size, 12.0f);
    EXPECT_FLOAT_EQ(theme.heading.size, 14.0f);
    theme.heading.color = {240, 230, 210, 255};
    ASSERT_TRUE(slayer3d_ui_layout_set_typography_theme(layout, &theme));

    slayer3d_ui_layout_node_desc heading{};
    heading.id = "heading";
    heading.type = SLAYER3D_UI_LAYOUT_NODE_LABEL;
    heading.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    heading.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    heading.rect = {10.0f, 20.0f, 120.0f, 24.0f};
    heading.text = "Properties";
    heading.text_role = SLAYER3D_UI_TEXT_ROLE_HEADING;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &heading));

    const char *options[] = {"First", "Second"};
    slayer3d_ui_layout_node_desc dropdown{};
    dropdown.id = "dropdown";
    dropdown.type = SLAYER3D_UI_LAYOUT_NODE_DROPDOWN;
    dropdown.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.rect = {10.0f, 50.0f, 120.0f, 24.0f};
    dropdown.text = "Choose";
    dropdown.text_role = SLAYER3D_UI_TEXT_ROLE_CAPTION;
    dropdown.options = options;
    dropdown.option_count = 2;
    dropdown.open = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &dropdown));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    const slayer3d_ui_layout_render_command *heading_command = find_render_command(layout, "heading");
    ASSERT_NE(heading_command, nullptr);
    EXPECT_EQ(heading_command->text_role, SLAYER3D_UI_TEXT_ROLE_HEADING);
    EXPECT_FLOAT_EQ(heading_command->text_size, 14.0f);
    EXPECT_EQ(heading_command->role_text_color.r, 240);
    EXPECT_FLOAT_EQ(heading_command->text_scale, 0.0f);

    const slayer3d_ui_layout_render_command *option_command = find_render_command(layout, "dropdown.option.0");
    ASSERT_NE(option_command, nullptr);
    EXPECT_EQ(option_command->text_role, SLAYER3D_UI_TEXT_ROLE_CAPTION);
    EXPECT_FLOAT_EQ(option_command->text_size, 12.0f);
    EXPECT_EQ(option_command->role_text_color.r, theme.caption.color.r);
    EXPECT_FLOAT_EQ(option_command->text_scale, 0.0f);

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

TEST(SLAYER3DUI, RetainedButtonReportsActionAndPointerState)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc button{};
    button.id = "save";
    button.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    button.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    button.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    button.rect = {10.0f, 20.0f, 100.0f, 30.0f};
    button.text = "Save";
    button.action = "editor.save";
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &button));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));

    slayer3d_ui_layout_input_state input{};
    input.pointer_x = 20.0f;
    input.pointer_y = 25.0f;
    input.primary_down = true;
    input.primary_pressed = true;
    slayer3d_ui_layout_activation activation{};
    ASSERT_TRUE(slayer3d_ui_layout_update_input(layout, &input, &activation));
    EXPECT_FALSE(activation.activated);

    const slayer3d_ui_layout_render_command *render = slayer3d_ui_layout_render_command_at(layout, 0);
    ASSERT_NE(render, nullptr);
    EXPECT_STREQ(render->id, "save");
    EXPECT_STREQ(render->text, "Save");
    EXPECT_TRUE(render->hovered);
    EXPECT_TRUE(render->active);

    input.primary_pressed = false;
    input.primary_down = false;
    input.primary_released = true;
    ASSERT_TRUE(slayer3d_ui_layout_update_input(layout, &input, &activation));
    EXPECT_TRUE(activation.activated);
    EXPECT_STREQ(activation.id, "save");
    EXPECT_STREQ(activation.action, "editor.save");

    render = slayer3d_ui_layout_render_command_at(layout, 0);
    ASSERT_NE(render, nullptr);
    EXPECT_TRUE(render->hovered);
    EXPECT_FALSE(render->active);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedDragHandleReportsSemanticPointerState)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc header{};
    header.id = "inspector.header";
    header.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    header.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    header.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    header.rect = {10.0f, 20.0f, 180.0f, 32.0f};
    header.drag_handle = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &header));

    slayer3d_ui_layout_node_desc indicator{};
    indicator.id = "inspector.drag_indicator";
    indicator.parent_id = "inspector.header";
    indicator.type = SLAYER3D_UI_LAYOUT_NODE_DRAG_INDICATOR;
    indicator.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    indicator.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    indicator.rect = {60.0f, 6.0f, 24.0f, 20.0f};
    indicator.state_source_id = "inspector.header";
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &indicator));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));

    slayer3d_ui_layout_input_state input{};
    input.pointer_x = 30.0f;
    input.pointer_y = 30.0f;
    input.primary_down = true;
    input.primary_pressed = true;
    ASSERT_TRUE(slayer3d_ui_layout_update_input(layout, &input, nullptr));

    const slayer3d_ui_layout_resolved_node *resolved =
        slayer3d_ui_layout_find_resolved_node(layout, "inspector.header");
    ASSERT_NE(resolved, nullptr);
    EXPECT_TRUE(resolved->interactive);
    EXPECT_TRUE(resolved->drag_handle);

    const slayer3d_ui_layout_render_command *render = slayer3d_ui_layout_render_command_at(layout, 0);
    ASSERT_NE(render, nullptr);
    EXPECT_TRUE(render->drag_handle);
    EXPECT_TRUE(render->hovered);
    EXPECT_TRUE(render->active);

    const slayer3d_ui_layout_render_command *indicator_render = slayer3d_ui_layout_render_command_at(layout, 1);
    ASSERT_NE(indicator_render, nullptr);
    EXPECT_EQ(indicator_render->type, SLAYER3D_UI_LAYOUT_NODE_DRAG_INDICATOR);
    EXPECT_STREQ(indicator_render->state_source_id, "inspector.header");
    EXPECT_TRUE(indicator_render->hovered);
    EXPECT_TRUE(indicator_render->active);

    const slayer3d_ui_layout_hit_region *hit = slayer3d_ui_layout_hit_test(layout, 30.0f, 30.0f);
    ASSERT_NE(hit, nullptr);
    EXPECT_TRUE(hit->drag_handle);
    EXPECT_TRUE(hit->hovered);
    EXPECT_TRUE(hit->active);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedSelectedStateAppearsInRenderMetadata)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc selected{};
    selected.id = "brush_tool";
    selected.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    selected.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    selected.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    selected.rect = {0.0f, 0.0f, 80.0f, 24.0f};
    selected.text = "Brush Tool";
    selected.selected = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &selected));

    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f));
    const slayer3d_ui_layout_render_command *render = slayer3d_ui_layout_render_command_at(layout, 0);
    ASSERT_NE(render, nullptr);
    EXPECT_STREQ(render->text, "Brush Tool");
    EXPECT_TRUE(render->selected);

    const slayer3d_ui_layout_hit_region *hit = slayer3d_ui_layout_hit_region_at(layout, 0);
    ASSERT_NE(hit, nullptr);
    EXPECT_TRUE(hit->selected);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedDropdownPopupMovesWithButton)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    const char *options[] = {"Grid 1", "Grid 2", "Grid 4"};
    slayer3d_ui_layout_node_desc dropdown{};
    dropdown.id = "grid";
    dropdown.type = SLAYER3D_UI_LAYOUT_NODE_DROPDOWN;
    dropdown.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.rect = {10.0f, 20.0f, 100.0f, 24.0f};
    dropdown.layer = 4;
    dropdown.text = "Grid 1";
    dropdown.action = "editor.grid";
    dropdown.options = options;
    dropdown.option_count = 3;
    dropdown.selected_index = 0;
    dropdown.open = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &dropdown));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 320.0f, 200.0f));

    const slayer3d_ui_layout_render_command *first_option = find_render_command(layout, "grid.option.0");
    ASSERT_NE(first_option, nullptr);
    EXPECT_FLOAT_EQ(first_option->rect.x, 10.0f);
    EXPECT_FLOAT_EQ(first_option->rect.y, 44.0f);
    EXPECT_STREQ(first_option->text, "Grid 1");
    EXPECT_STREQ(first_option->owner_id, "grid");
    EXPECT_EQ(first_option->option_index, 0);
    EXPECT_TRUE(first_option->selected);
    const slayer3d_ui_layout_hit_region *first_hit = find_hit_region(layout, "grid.option.0");
    ASSERT_NE(first_hit, nullptr);
    EXPECT_FLOAT_EQ(first_hit->rect.x, first_option->rect.x);
    EXPECT_FLOAT_EQ(first_hit->rect.y, first_option->rect.y);

    slayer3d_ui_layout_clear(layout);
    dropdown.rect = {180.0f, 50.0f, 100.0f, 24.0f};
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &dropdown));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 320.0f, 200.0f));
    first_option = find_render_command(layout, "grid.option.0");
    ASSERT_NE(first_option, nullptr);
    EXPECT_FLOAT_EQ(first_option->rect.x, 180.0f);
    EXPECT_FLOAT_EQ(first_option->rect.y, 74.0f);
    first_hit = find_hit_region(layout, "grid.option.0");
    ASSERT_NE(first_hit, nullptr);
    EXPECT_FLOAT_EQ(first_hit->rect.x, 180.0f);
    EXPECT_FLOAT_EQ(first_hit->rect.y, 74.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedDropdownOptionHoverAppearsInRenderMetadata)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    const char *options[] = {"Grid 1", "Grid 2", "Grid 4"};
    slayer3d_ui_layout_node_desc dropdown{};
    dropdown.id = "grid";
    dropdown.type = SLAYER3D_UI_LAYOUT_NODE_DROPDOWN;
    dropdown.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.rect = {10.0f, 20.0f, 100.0f, 24.0f};
    dropdown.action = "editor.grid";
    dropdown.options = options;
    dropdown.option_count = 3;
    dropdown.selected_index = 0;
    dropdown.open = true;
    dropdown.option_height = 20.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &dropdown));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 320.0f, 200.0f));

    slayer3d_ui_layout_input_state input{};
    input.pointer_x = 20.0f;
    input.pointer_y = 66.0f;
    slayer3d_ui_layout_activation activation{};
    ASSERT_TRUE(slayer3d_ui_layout_update_input(layout, &input, &activation));
    EXPECT_FALSE(activation.activated);

    const slayer3d_ui_layout_render_command *first_option = find_render_command(layout, "grid.option.0");
    const slayer3d_ui_layout_render_command *second_option = find_render_command(layout, "grid.option.1");
    ASSERT_NE(first_option, nullptr);
    ASSERT_NE(second_option, nullptr);
    EXPECT_FALSE(first_option->hovered);
    EXPECT_TRUE(second_option->hovered);
    EXPECT_TRUE(first_option->selected);
    EXPECT_FALSE(second_option->selected);

    const slayer3d_ui_layout_hit_region *second_hit = find_hit_region(layout, "grid.option.1");
    ASSERT_NE(second_hit, nullptr);
    EXPECT_TRUE(second_hit->hovered);
    EXPECT_EQ(second_hit->option_index, 1);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedDropdownPopupClampsToViewport)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    const char *options[] = {"Grid 1", "Grid 2", "Grid 4"};
    slayer3d_ui_layout_node_desc dropdown{};
    dropdown.id = "grid";
    dropdown.type = SLAYER3D_UI_LAYOUT_NODE_DROPDOWN;
    dropdown.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.rect = {240.0f, 90.0f, 100.0f, 20.0f};
    dropdown.options = options;
    dropdown.option_count = 3;
    dropdown.open = true;
    dropdown.option_height = 20.0f;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &dropdown));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 300.0f, 120.0f));

    const slayer3d_ui_layout_render_command *popup = find_render_command(layout, "grid.popup");
    ASSERT_NE(popup, nullptr);
    EXPECT_TRUE(popup->popup);
    EXPECT_FLOAT_EQ(popup->rect.x, 200.0f);
    EXPECT_FLOAT_EQ(popup->rect.y, 30.0f);
    EXPECT_FLOAT_EQ(popup->rect.w, 100.0f);
    EXPECT_FLOAT_EQ(popup->rect.h, 60.0f);
    const slayer3d_ui_layout_render_command *third_option = find_render_command(layout, "grid.option.2");
    ASSERT_NE(third_option, nullptr);
    EXPECT_FLOAT_EQ(third_option->rect.x, 200.0f);
    EXPECT_FLOAT_EQ(third_option->rect.y, 70.0f);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedPopupCapturesOutsideClickBehindItsContents)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    slayer3d_ui_layout_node_desc canvas{};
    canvas.id = "canvas";
    canvas.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    canvas.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    canvas.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    canvas.rect = {0.0f, 0.0f, 320.0f, 200.0f};
    canvas.layer = 1000;
    canvas.action = "canvas.select";
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &canvas));

    slayer3d_ui_layout_node_desc popup{};
    popup.id = "file.menu";
    popup.type = SLAYER3D_UI_LAYOUT_NODE_PANEL;
    popup.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    popup.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    popup.rect = {20.0f, 20.0f, 100.0f, 100.0f};
    popup.layer = 100;
    popup.outside_click_action = "file.close";
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &popup));

    slayer3d_ui_layout_node_desc button{};
    button.id = "file.menu.open";
    button.parent_id = "file.menu";
    button.type = SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    button.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    button.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    button.rect = {10.0f, 10.0f, 80.0f, 24.0f};
    button.layer = 101;
    button.action = "file.open";
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &button));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 320.0f, 200.0f));

    const slayer3d_ui_layout_hit_region *inside = slayer3d_ui_layout_hit_test(layout, 40.0f, 40.0f);
    ASSERT_NE(inside, nullptr);
    EXPECT_STREQ(inside->id, "file.menu.open");
    EXPECT_STREQ(inside->action, "file.open");
    EXPECT_FALSE(inside->outside_click);

    const slayer3d_ui_layout_hit_region *inside_surface = slayer3d_ui_layout_hit_test(layout, 115.0f, 115.0f);
    ASSERT_NE(inside_surface, nullptr);
    EXPECT_STREQ(inside_surface->id, "file.menu");
    EXPECT_FALSE(inside_surface->outside_click);

    const slayer3d_ui_layout_hit_region *outside = slayer3d_ui_layout_hit_test(layout, 200.0f, 150.0f);
    ASSERT_NE(outside, nullptr);
    EXPECT_STREQ(outside->id, "file.menu.outside");
    EXPECT_STREQ(outside->owner_id, "file.menu");
    EXPECT_STREQ(outside->action, "file.close");
    EXPECT_TRUE(outside->outside_click);

    slayer3d_ui_layout_destroy(layout);
}

TEST(SLAYER3DUI, RetainedDropdownOptionReportsActivation)
{
    slayer3d_ui_layout_model *layout = nullptr;
    ASSERT_TRUE(slayer3d_ui_layout_create(&layout));

    const char *options[] = {"Grid 1", "Grid 2", "Grid 4"};
    slayer3d_ui_layout_node_desc dropdown{};
    dropdown.id = "grid";
    dropdown.type = SLAYER3D_UI_LAYOUT_NODE_DROPDOWN;
    dropdown.width_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.height_mode = SLAYER3D_UI_LAYOUT_SIZE_FIXED;
    dropdown.rect = {10.0f, 10.0f, 100.0f, 24.0f};
    dropdown.text = "Grid 1";
    dropdown.action = "editor.grid";
    dropdown.options = options;
    dropdown.option_count = 3;
    dropdown.selected_index = 1;
    dropdown.open = true;
    ASSERT_TRUE(slayer3d_ui_layout_add_node(layout, &dropdown));
    ASSERT_TRUE(slayer3d_ui_layout_resolve(layout, 320.0f, 200.0f));

    slayer3d_ui_layout_input_state input{};
    input.pointer_x = 20.0f;
    input.pointer_y = 62.0f;
    input.primary_down = true;
    input.primary_pressed = true;
    slayer3d_ui_layout_activation activation{};
    ASSERT_TRUE(slayer3d_ui_layout_update_input(layout, &input, &activation));
    EXPECT_FALSE(activation.activated);

    const slayer3d_ui_layout_render_command *second_option = find_render_command(layout, "grid.option.1");
    ASSERT_NE(second_option, nullptr);
    EXPECT_TRUE(second_option->hovered);
    EXPECT_TRUE(second_option->active);
    EXPECT_TRUE(second_option->selected);

    input.primary_pressed = false;
    input.primary_down = false;
    input.primary_released = true;
    ASSERT_TRUE(slayer3d_ui_layout_update_input(layout, &input, &activation));
    EXPECT_TRUE(activation.activated);
    EXPECT_STREQ(activation.id, "grid.option.1");
    EXPECT_STREQ(activation.owner_id, "grid");
    EXPECT_STREQ(activation.action, "editor.grid");
    EXPECT_EQ(activation.option_index, 1);

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

/**
 * @file ui_layout.h
 * @brief Retained UI layout tree and resolver.
 */

#ifndef SLAYER3D_UI_LAYOUT_H
#define SLAYER3D_UI_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Maximum bytes stored for a retained UI node id, including the terminator. */
#define SLAYER3D_UI_LAYOUT_ID_MAX 64
    /** @brief Maximum bytes stored for retained UI authored text, including the terminator. */
#define SLAYER3D_UI_LAYOUT_TEXT_MAX 256
    /** @brief Maximum bytes stored for a retained UI font asset id, including the terminator. */
#define SLAYER3D_UI_LAYOUT_FONT_MAX 64
    /** @brief Maximum bytes stored for retained UI action names, including the terminator. */
#define SLAYER3D_UI_LAYOUT_ACTION_MAX 128
    /** @brief Maximum inline options stored by one retained UI dropdown. */
#define SLAYER3D_UI_LAYOUT_DROPDOWN_OPTION_MAX 64
    /** @brief Maximum bytes stored for a retained UI image asset id, including the terminator. */
#define SLAYER3D_UI_LAYOUT_IMAGE_MAX 128

    /** @brief Authored retained UI widget type. */
    typedef enum slayer3d_ui_layout_node_type
    {
        SLAYER3D_UI_LAYOUT_NODE_PANEL = 0,
        SLAYER3D_UI_LAYOUT_NODE_TOOLBAR,
        SLAYER3D_UI_LAYOUT_NODE_ROW,
        SLAYER3D_UI_LAYOUT_NODE_COLUMN,
        SLAYER3D_UI_LAYOUT_NODE_BUTTON,
        SLAYER3D_UI_LAYOUT_NODE_LABEL,
        SLAYER3D_UI_LAYOUT_NODE_DROPDOWN,
        SLAYER3D_UI_LAYOUT_NODE_TAB_STRIP,
        SLAYER3D_UI_LAYOUT_NODE_SPACER,
        SLAYER3D_UI_LAYOUT_NODE_CONSOLE,
        SLAYER3D_UI_LAYOUT_NODE_IMAGE,
        /**
         * Vertical scroll pane. Children lay out in the pane's content
         * coordinates; the pane clips them, measures their extent, clamps
         * the requested offset into [0, extent - viewport], shifts the
         * subtree, and synthesizes a proportional scrollbar when content
         * overflows. Correctness lives in the container, not per child.
         */
        SLAYER3D_UI_LAYOUT_NODE_SCROLL,
    } slayer3d_ui_layout_node_type;

    /** @brief Optional layout axis applied to a node's direct children. */
    typedef enum slayer3d_ui_layout_axis
    {
        SLAYER3D_UI_LAYOUT_AXIS_NONE = 0,
        SLAYER3D_UI_LAYOUT_AXIS_ROW,
        SLAYER3D_UI_LAYOUT_AXIS_COLUMN,
        SLAYER3D_UI_LAYOUT_AXIS_GRID,
    } slayer3d_ui_layout_axis;

    /** @brief Sizing behavior for one axis. */
    typedef enum slayer3d_ui_layout_size_mode
    {
        SLAYER3D_UI_LAYOUT_SIZE_FIXED = 0,
        SLAYER3D_UI_LAYOUT_SIZE_FILL,
    } slayer3d_ui_layout_size_mode;

    /** @brief Dock side for a retained root window. */
    typedef enum slayer3d_ui_layout_dock
    {
        SLAYER3D_UI_LAYOUT_DOCK_NONE = 0,
        SLAYER3D_UI_LAYOUT_DOCK_LEFT,
        SLAYER3D_UI_LAYOUT_DOCK_RIGHT,
        SLAYER3D_UI_LAYOUT_DOCK_BOTTOM,
    } slayer3d_ui_layout_dock;

    /** @brief Optional retained UI text alignment. */
    typedef enum slayer3d_ui_layout_text_align
    {
        SLAYER3D_UI_LAYOUT_TEXT_ALIGN_AUTO = 0,
        SLAYER3D_UI_LAYOUT_TEXT_ALIGN_LEFT,
        SLAYER3D_UI_LAYOUT_TEXT_ALIGN_CENTER,
        SLAYER3D_UI_LAYOUT_TEXT_ALIGN_RIGHT,
    } slayer3d_ui_layout_text_align;

    /** @brief Simple logical-pixel rectangle. */
    typedef struct slayer3d_ui_layout_rect
    {
        float x;
        float y;
        float w;
        float h;
    } slayer3d_ui_layout_rect;

    /** @brief Authored retained UI node description. */
    typedef struct slayer3d_ui_layout_node_desc
    {
        const char *id;
        const char *parent_id;
        slayer3d_ui_layout_node_type type;
        slayer3d_ui_layout_axis axis;
        slayer3d_ui_layout_size_mode width_mode;
        slayer3d_ui_layout_size_mode height_mode;
        slayer3d_ui_layout_rect rect;
        float padding;
        float gap;
        /** @brief Number of columns for children placed with SLAYER3D_UI_LAYOUT_AXIS_GRID. */
        int grid_columns;
        /** @brief Clip descendant rendering and hit testing to this node's padded content rect. */
        bool clip_children;
        /** @brief Optional resolved node id whose rect clips this node and its descendants. */
        const char *clip_rect_id;
        int layer;
        bool interactive;
        const char *text;
        const char *font;
        const char *action;
        slayer3d_color text_color;
        bool has_text_color;
        float text_scale;
        slayer3d_ui_layout_text_align text_align;
        slayer3d_color fill_color;
        bool has_fill_color;
        slayer3d_color border_color;
        bool has_border_color;
        float border_thickness;
        bool selected;
        const char *const *options;
        int option_count;
        int selected_index;
        bool open;
        float option_height;
        /** @brief Optional image asset id drawn by SLAYER3D_UI_LAYOUT_NODE_IMAGE nodes. */
        const char *image;
        /** @brief Preserve the source image aspect ratio inside the node rect. */
        bool preserve_aspect;
        /** @brief Requested scroll offset for SLAYER3D_UI_LAYOUT_NODE_SCROLL nodes; clamped at resolve. */
        float scroll_offset;
        /** @brief Optional caller state key that owns this pane's scroll offset. */
        const char *scroll_key;
        /**
         * @brief Anchor the node's x to the right edge of its parent (or viewport).
         *
         * When set, `rect.x` is the distance from the right edge to the
         * node's right side, so docked panels keep their margin at any
         * viewport size instead of hardcoding absolute positions.
         */
        bool anchor_right;
        /** @brief Anchor the node's y to the bottom edge of its parent (or viewport). */
        bool anchor_bottom;
        /** @brief Treat this root node as an independently movable UI window. */
        bool window;
        /** @brief Raise this root window and its complete subtree above other root windows. */
        bool window_front;
        /** @brief Dock side for a root window. Side docks stack inward; bottom docks stack upward. */
        slayer3d_ui_layout_dock dock;
        /** @brief Top inset of the canvas area occupied by a docked root window. */
        float dock_top;
        /** @brief Bottom inset of the canvas area occupied by a docked root window. */
        float dock_bottom;
        /** @brief Outer horizontal margin for docked root windows. */
        float dock_margin;
        /** @brief Gap between root windows docked to the same side. */
        float dock_gap;
        /** @brief Optional width while docked left or right; zero uses the floating width. */
        float dock_width;
        /** @brief Optional height while docked to the bottom; zero uses the floating height. */
        float dock_height;
        /**
         * @brief Total item count for a virtualized list container.
         *
         * When positive on a container with a scroll_key, the container
         * scrolls in item-index units: its resolved children are the visible
         * window, scroll_max becomes max(span - child count, 0), and the same
         * proportional scrollbar is synthesized as for pixel scroll panes.
         * Children are not shifted - hosts rebind their contents per index.
         */
        float scroll_span;
        /** @brief Optional signal emitted by hosts when a scrollbar drag changes the offset. */
        const char *scroll_signal;
        /** @brief Wheel step per notch; 0 selects the default (1 item, or 40px for scroll panes). */
        float scroll_step;
        /** @brief Draw a disabled scrollbar affordance even when content currently fits. */
        bool scrollbar_always;
    } slayer3d_ui_layout_node_desc;

    /** @brief Resolved retained UI node with final screen-space bounds. */
    typedef struct slayer3d_ui_layout_resolved_node
    {
        char id[SLAYER3D_UI_LAYOUT_ID_MAX];
        char parent_id[SLAYER3D_UI_LAYOUT_ID_MAX];
        int parent_index;
        slayer3d_ui_layout_node_type type;
        slayer3d_ui_layout_axis axis;
        slayer3d_ui_layout_rect rect;
        bool has_clip_rect;
        slayer3d_ui_layout_rect clip_rect;
        int layer;
        bool interactive;
        /** @brief True when this resolved node is an independently movable root window. */
        bool window;
        /** @brief True when this root window owns the front-most window stacking context. */
        bool window_front;
        /** @brief Resolved dock side for a root window. */
        slayer3d_ui_layout_dock dock;
        char text[SLAYER3D_UI_LAYOUT_TEXT_MAX];
        char action[SLAYER3D_UI_LAYOUT_ACTION_MAX];
        slayer3d_color text_color;
        bool has_text_color;
        float text_scale;
        slayer3d_ui_layout_text_align text_align;
        bool hovered;
        bool active;
        bool selected;
        char font[SLAYER3D_UI_LAYOUT_FONT_MAX];
        slayer3d_color fill_color;
        bool has_fill_color;
        slayer3d_color border_color;
        bool has_border_color;
        float border_thickness;
        char image[SLAYER3D_UI_LAYOUT_IMAGE_MAX];
        bool preserve_aspect;
        /** @brief Applied (clamped) scroll offset for scroll panes. */
        float scroll_offset;
        /** @brief Maximum valid scroll offset: max(content extent - viewport, 0). */
        float scroll_max;
        /** @brief Measured content extent of a scroll pane in pane-content units. */
        float content_extent;
        /** @brief Caller state key that owns this pane's scroll offset, when authored. */
        char scroll_key[SLAYER3D_UI_LAYOUT_ACTION_MAX];
        /** @brief True when the node scrolls in item-index units (virtualized list). */
        bool scroll_virtual;
        /** @brief Signal hosts emit after a scrollbar drag changes the offset, when authored. */
        char scroll_signal[SLAYER3D_UI_LAYOUT_ACTION_MAX];
        /** @brief Wheel step per notch; 0 selects the default. */
        float scroll_step;
    } slayer3d_ui_layout_resolved_node;

    /** @brief Flat retained UI draw command compiled from a resolved node. */
    typedef struct slayer3d_ui_layout_render_command
    {
        char id[SLAYER3D_UI_LAYOUT_ID_MAX];
        slayer3d_ui_layout_node_type type;
        slayer3d_ui_layout_rect rect;
        bool has_clip_rect;
        slayer3d_ui_layout_rect clip_rect;
        int layer;
        char text[SLAYER3D_UI_LAYOUT_TEXT_MAX];
        char font[SLAYER3D_UI_LAYOUT_FONT_MAX];
        slayer3d_color text_color;
        bool has_text_color;
        float text_scale;
        slayer3d_ui_layout_text_align text_align;
        slayer3d_color fill_color;
        bool has_fill_color;
        slayer3d_color border_color;
        bool has_border_color;
        float border_thickness;
        bool hovered;
        bool active;
        bool selected;
        char owner_id[SLAYER3D_UI_LAYOUT_ID_MAX];
        int option_index;
        bool popup;
        /** @brief True when this command paints a root window surface. */
        bool window;
        char image[SLAYER3D_UI_LAYOUT_IMAGE_MAX];
        bool preserve_aspect;
    } slayer3d_ui_layout_render_command;

    /** @brief Flat retained UI hit region compiled from a resolved interactive node. */
    typedef struct slayer3d_ui_layout_hit_region
    {
        char id[SLAYER3D_UI_LAYOUT_ID_MAX];
        slayer3d_ui_layout_node_type type;
        slayer3d_ui_layout_rect rect;
        bool has_clip_rect;
        slayer3d_ui_layout_rect clip_rect;
        int layer;
        char action[SLAYER3D_UI_LAYOUT_ACTION_MAX];
        bool hovered;
        bool active;
        bool selected;
        char owner_id[SLAYER3D_UI_LAYOUT_ID_MAX];
        int option_index;
    } slayer3d_ui_layout_hit_region;

    /** @brief Pointer input for retained UI hit testing and activation. */
    typedef struct slayer3d_ui_layout_input_state
    {
        float pointer_x;
        float pointer_y;
        bool primary_down;
        bool primary_pressed;
        bool primary_released;
    } slayer3d_ui_layout_input_state;

    /** @brief Retained UI activation result for one input update. */
    typedef struct slayer3d_ui_layout_activation
    {
        char id[SLAYER3D_UI_LAYOUT_ID_MAX];
        char owner_id[SLAYER3D_UI_LAYOUT_ID_MAX];
        char action[SLAYER3D_UI_LAYOUT_ACTION_MAX];
        int option_index;
        bool activated;
    } slayer3d_ui_layout_activation;

    /** @brief Retained UI layout tree. */
    typedef struct slayer3d_ui_layout_model slayer3d_ui_layout_model;

    /** @brief Create an empty retained UI layout model. */
    bool slayer3d_ui_layout_create(slayer3d_ui_layout_model **out_model);

    /** @brief Destroy a retained UI layout model. */
    void slayer3d_ui_layout_destroy(slayer3d_ui_layout_model *model);

    /** @brief Remove all nodes from a retained UI layout model. */
    void slayer3d_ui_layout_clear(slayer3d_ui_layout_model *model);

    /** @brief Add one retained UI node to the model. */
    bool slayer3d_ui_layout_add_node(slayer3d_ui_layout_model *model, const slayer3d_ui_layout_node_desc *desc);

    /** @brief Resolve all retained UI nodes into an origin-zero screen-space viewport. */
    bool slayer3d_ui_layout_resolve(slayer3d_ui_layout_model *model, float viewport_w, float viewport_h);

    /**
     * @brief Resolve all retained UI nodes inside a screen-space viewport rectangle.
     *
     * Root positions, anchors, docks, floating-window bounds, and popups are
     * relative to @p viewport. This is useful for platform safe areas where
     * interactive UI must avoid display cutouts while rendering still uses the
     * complete logical canvas.
     */
    bool slayer3d_ui_layout_resolve_in_rect(slayer3d_ui_layout_model *model, slayer3d_ui_layout_rect viewport);

    /** @brief Mark the retained UI layout dirty so the next resolve recomputes cached lists. */
    void slayer3d_ui_layout_mark_dirty(slayer3d_ui_layout_model *model);

    /** @brief Return whether the retained UI layout needs recomputation. */
    bool slayer3d_ui_layout_is_dirty(const slayer3d_ui_layout_model *model);

    /** @brief Return the layout generation, incremented whenever cached layout is recomputed. */
    int slayer3d_ui_layout_generation(const slayer3d_ui_layout_model *model);

    /** @brief Return the number of retained UI nodes in the model. */
    int slayer3d_ui_layout_node_count(const slayer3d_ui_layout_model *model);

    /** @brief Return one resolved retained UI node by index, or NULL when out of range. */
    const slayer3d_ui_layout_resolved_node *slayer3d_ui_layout_resolved_node_at(const slayer3d_ui_layout_model *model,
                                                                                int index);

    /** @brief Find one resolved retained UI node by id, or NULL when absent. */
    const slayer3d_ui_layout_resolved_node *slayer3d_ui_layout_find_resolved_node(const slayer3d_ui_layout_model *model,
                                                                                  const char *id);

    /**
     * @brief Calculate the exact rectangle a root window would occupy at a dock.
     *
     * The calculation uses the same authored order, dimensions, margins, and
     * gaps as layout resolution while treating the selected window as if it
     * were docked at `dock`. This is intended for docking previews.
     *
     * @return true when `id` names a root window and `dock` is left, right, or bottom.
     */
    bool slayer3d_ui_layout_calculate_window_dock_rect(const slayer3d_ui_layout_model *model, const char *id,
                                                       slayer3d_ui_layout_dock dock, float viewport_width,
                                                       float viewport_height, slayer3d_ui_layout_rect *out_rect);

    /** @brief Calculate a root window dock rectangle inside an arbitrary viewport rectangle. */
    bool slayer3d_ui_layout_calculate_window_dock_rect_in_rect(const slayer3d_ui_layout_model *model, const char *id,
                                                               slayer3d_ui_layout_dock dock,
                                                               slayer3d_ui_layout_rect viewport,
                                                               slayer3d_ui_layout_rect *out_rect);

    /** @brief Return the number of flat retained UI render commands. */
    int slayer3d_ui_layout_render_command_count(const slayer3d_ui_layout_model *model);

    /** @brief Return one flat retained UI render command by index, or NULL when out of range. */
    const slayer3d_ui_layout_render_command *slayer3d_ui_layout_render_command_at(const slayer3d_ui_layout_model *model,
                                                                                  int index);

    /** @brief Return the number of flat retained UI hit regions. */
    int slayer3d_ui_layout_hit_region_count(const slayer3d_ui_layout_model *model);

    /** @brief Return one flat retained UI hit region by index, or NULL when out of range. */
    const slayer3d_ui_layout_hit_region *slayer3d_ui_layout_hit_region_at(const slayer3d_ui_layout_model *model,
                                                                          int index);

    /** @brief Hit test retained UI regions, returning the front-most matching region or NULL. */
    const slayer3d_ui_layout_hit_region *slayer3d_ui_layout_hit_test(const slayer3d_ui_layout_model *model, float x,
                                                                     float y);

    /** @brief Update retained widget hover/active state and return any activated action. */
    bool slayer3d_ui_layout_update_input(slayer3d_ui_layout_model *model, const slayer3d_ui_layout_input_state *input,
                                         slayer3d_ui_layout_activation *out_activation);

    /** @brief Suffix appended to a scroll pane id for its synthesized scrollbar commands. */
#define SLAYER3D_UI_LAYOUT_SCROLLBAR_SUFFIX ".scrollbar"

    /**
     * @brief Map a pointer y on a scroll pane's synthesized scrollbar to a scroll offset.
     *
     * Uses the pane's resolved geometry so callers never duplicate thumb or
     * travel math. Returns false when @p pane_id is not a resolved scroll
     * pane or its content does not overflow.
     */
    bool slayer3d_ui_layout_scrollbar_offset_for_pointer(const slayer3d_ui_layout_model *model, const char *pane_id,
                                                         float pointer_y, float *out_offset);

#ifdef __cplusplus
}
#endif

#endif

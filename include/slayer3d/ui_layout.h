/**
 * @file ui_layout.h
 * @brief Retained UI layout tree and resolver.
 */

#ifndef SLAYER3D_UI_LAYOUT_H
#define SLAYER3D_UI_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Maximum bytes stored for a retained UI node id, including the terminator. */
#define SLAYER3D_UI_LAYOUT_ID_MAX 64

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
    } slayer3d_ui_layout_node_type;

    /** @brief Optional layout axis applied to a node's direct children. */
    typedef enum slayer3d_ui_layout_axis
    {
        SLAYER3D_UI_LAYOUT_AXIS_NONE = 0,
        SLAYER3D_UI_LAYOUT_AXIS_ROW,
        SLAYER3D_UI_LAYOUT_AXIS_COLUMN,
    } slayer3d_ui_layout_axis;

    /** @brief Sizing behavior for one axis. */
    typedef enum slayer3d_ui_layout_size_mode
    {
        SLAYER3D_UI_LAYOUT_SIZE_FIXED = 0,
        SLAYER3D_UI_LAYOUT_SIZE_FILL,
    } slayer3d_ui_layout_size_mode;

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
        int layer;
        bool interactive;
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
        int layer;
        bool interactive;
    } slayer3d_ui_layout_resolved_node;

    /** @brief Flat retained UI draw command compiled from a resolved node. */
    typedef struct slayer3d_ui_layout_render_command
    {
        char id[SLAYER3D_UI_LAYOUT_ID_MAX];
        slayer3d_ui_layout_node_type type;
        slayer3d_ui_layout_rect rect;
        int layer;
    } slayer3d_ui_layout_render_command;

    /** @brief Flat retained UI hit region compiled from a resolved interactive node. */
    typedef struct slayer3d_ui_layout_hit_region
    {
        char id[SLAYER3D_UI_LAYOUT_ID_MAX];
        slayer3d_ui_layout_node_type type;
        slayer3d_ui_layout_rect rect;
        int layer;
    } slayer3d_ui_layout_hit_region;

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

    /** @brief Resolve all retained UI nodes into screen-space rectangles. */
    bool slayer3d_ui_layout_resolve(slayer3d_ui_layout_model *model, float viewport_w, float viewport_h);

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

#ifdef __cplusplus
}
#endif

#endif

/**
 * @file ui_layout.c
 * @brief Retained UI layout tree and resolver.
 */

#include "slayer3d/ui_layout.h"

#include <SDL3/SDL_stdinc.h>

typedef struct ui_layout_node
{
    char id[SLAYER3D_UI_LAYOUT_ID_MAX];
    char parent_id[SLAYER3D_UI_LAYOUT_ID_MAX];
    int parent_index;
    slayer3d_ui_layout_node_type type;
    slayer3d_ui_layout_axis axis;
    slayer3d_ui_layout_size_mode width_mode;
    slayer3d_ui_layout_size_mode height_mode;
    slayer3d_ui_layout_rect local_rect;
    slayer3d_ui_layout_rect resolved_rect;
    float padding;
    float gap;
    bool resolved;
    bool resolving;
} ui_layout_node;

struct slayer3d_ui_layout_model
{
    ui_layout_node *nodes;
    slayer3d_ui_layout_resolved_node *resolved_nodes;
    int count;
    int capacity;
};

static bool ui_layout_id_valid(const char *id)
{
    return id != NULL && id[0] != '\0' && SDL_strlen(id) < SLAYER3D_UI_LAYOUT_ID_MAX;
}

static void ui_layout_copy_id(char *dst, const char *src)
{
    SDL_snprintf(dst, SLAYER3D_UI_LAYOUT_ID_MAX, "%s", src != NULL ? src : "");
}

static int ui_layout_find_node_index(const slayer3d_ui_layout_model *model, const char *id)
{
    if (model == NULL || id == NULL || id[0] == '\0')
        return -1;
    for (int i = 0; i < model->count; ++i)
    {
        if (SDL_strcmp(model->nodes[i].id, id) == 0)
            return i;
    }
    return -1;
}

static bool ui_layout_reserve(slayer3d_ui_layout_model *model, int capacity)
{
    if (model->capacity >= capacity)
        return true;
    int new_capacity = model->capacity > 0 ? model->capacity * 2 : 16;
    while (new_capacity < capacity)
        new_capacity *= 2;
    ui_layout_node *nodes = (ui_layout_node *)SDL_calloc((size_t)new_capacity, sizeof(*nodes));
    slayer3d_ui_layout_resolved_node *resolved_nodes =
        (slayer3d_ui_layout_resolved_node *)SDL_calloc((size_t)new_capacity, sizeof(*resolved_nodes));
    if (nodes == NULL || resolved_nodes == NULL)
    {
        SDL_free(nodes);
        SDL_free(resolved_nodes);
        return false;
    }
    if (model->count > 0)
    {
        SDL_memcpy(nodes, model->nodes, (size_t)model->count * sizeof(*nodes));
        SDL_memcpy(resolved_nodes, model->resolved_nodes, (size_t)model->count * sizeof(*resolved_nodes));
    }
    SDL_free(model->nodes);
    SDL_free(model->resolved_nodes);
    model->nodes = nodes;
    model->resolved_nodes = resolved_nodes;
    model->capacity = new_capacity;
    return true;
}

bool slayer3d_ui_layout_create(slayer3d_ui_layout_model **out_model)
{
    if (out_model == NULL)
        return false;
    slayer3d_ui_layout_model *model = (slayer3d_ui_layout_model *)SDL_calloc(1, sizeof(*model));
    if (model == NULL)
        return false;
    *out_model = model;
    return true;
}

void slayer3d_ui_layout_destroy(slayer3d_ui_layout_model *model)
{
    if (model == NULL)
        return;
    SDL_free(model->nodes);
    SDL_free(model->resolved_nodes);
    SDL_free(model);
}

void slayer3d_ui_layout_clear(slayer3d_ui_layout_model *model)
{
    if (model == NULL)
        return;
    model->count = 0;
}

bool slayer3d_ui_layout_add_node(slayer3d_ui_layout_model *model, const slayer3d_ui_layout_node_desc *desc)
{
    if (model == NULL || desc == NULL || !ui_layout_id_valid(desc->id) ||
        ui_layout_find_node_index(model, desc->id) >= 0)
    {
        return false;
    }
    if (desc->parent_id != NULL && desc->parent_id[0] != '\0' && !ui_layout_id_valid(desc->parent_id))
        return false;
    if (desc->padding < 0.0f || desc->gap < 0.0f)
        return false;
    if ((desc->width_mode == SLAYER3D_UI_LAYOUT_SIZE_FIXED && desc->rect.w <= 0.0f) ||
        (desc->height_mode == SLAYER3D_UI_LAYOUT_SIZE_FIXED && desc->rect.h <= 0.0f))
    {
        return false;
    }
    if (!ui_layout_reserve(model, model->count + 1))
        return false;

    ui_layout_node *node = &model->nodes[model->count++];
    SDL_zero(*node);
    ui_layout_copy_id(node->id, desc->id);
    ui_layout_copy_id(node->parent_id, desc->parent_id);
    node->parent_index = -1;
    node->type = desc->type;
    node->axis = desc->axis;
    node->width_mode = desc->width_mode;
    node->height_mode = desc->height_mode;
    node->local_rect = desc->rect;
    node->padding = desc->padding;
    node->gap = desc->gap;
    return true;
}

static float ui_layout_node_width(const ui_layout_node *node, float fallback)
{
    return node->width_mode == SLAYER3D_UI_LAYOUT_SIZE_FILL ? SDL_max(fallback, 0.0f) : node->local_rect.w;
}

static float ui_layout_node_height(const ui_layout_node *node, float fallback)
{
    return node->height_mode == SLAYER3D_UI_LAYOUT_SIZE_FILL ? SDL_max(fallback, 0.0f) : node->local_rect.h;
}

static int ui_layout_child_count(const slayer3d_ui_layout_model *model, int parent_index)
{
    int count = 0;
    for (int i = 0; i < model->count; ++i)
    {
        if (model->nodes[i].parent_index == parent_index)
            ++count;
    }
    return count;
}

static void ui_layout_content_rect(const ui_layout_node *node, slayer3d_ui_layout_rect *out_rect)
{
    *out_rect = node->resolved_rect;
    const float pad = SDL_min(node->padding, SDL_min(out_rect->w, out_rect->h) * 0.5f);
    out_rect->x += pad;
    out_rect->y += pad;
    out_rect->w = SDL_max(out_rect->w - pad * 2.0f, 0.0f);
    out_rect->h = SDL_max(out_rect->h - pad * 2.0f, 0.0f);
}

static float ui_layout_fixed_child_extent(const ui_layout_node *node, slayer3d_ui_layout_axis axis)
{
    if (axis == SLAYER3D_UI_LAYOUT_AXIS_ROW)
        return node->width_mode == SLAYER3D_UI_LAYOUT_SIZE_FIXED ? node->local_rect.w : 0.0f;
    if (axis == SLAYER3D_UI_LAYOUT_AXIS_COLUMN)
        return node->height_mode == SLAYER3D_UI_LAYOUT_SIZE_FIXED ? node->local_rect.h : 0.0f;
    return 0.0f;
}

static bool ui_layout_child_fills_axis(const ui_layout_node *node, slayer3d_ui_layout_axis axis)
{
    if (axis == SLAYER3D_UI_LAYOUT_AXIS_ROW)
        return node->width_mode == SLAYER3D_UI_LAYOUT_SIZE_FILL;
    if (axis == SLAYER3D_UI_LAYOUT_AXIS_COLUMN)
        return node->height_mode == SLAYER3D_UI_LAYOUT_SIZE_FILL;
    return false;
}

static float ui_layout_distributed_fill_extent(const slayer3d_ui_layout_model *model, int parent_index,
                                               slayer3d_ui_layout_axis axis, float available, float gap)
{
    float fixed = 0.0f;
    int fill_count = 0;
    int child_count = 0;
    for (int i = 0; i < model->count; ++i)
    {
        const ui_layout_node *child = &model->nodes[i];
        if (child->parent_index != parent_index)
            continue;
        ++child_count;
        fixed += ui_layout_fixed_child_extent(child, axis);
        if (ui_layout_child_fills_axis(child, axis))
            ++fill_count;
    }
    const float total_gap = child_count > 1 ? gap * (float)(child_count - 1) : 0.0f;
    const float fill_total = SDL_max(available - fixed - total_gap, 0.0f);
    return fill_count > 0 ? fill_total / (float)fill_count : 0.0f;
}

static bool ui_layout_resolve_node(slayer3d_ui_layout_model *model, int index, float viewport_w, float viewport_h);

static bool ui_layout_resolve_children(slayer3d_ui_layout_model *model, int parent_index, float viewport_w,
                                       float viewport_h)
{
    ui_layout_node *parent = &model->nodes[parent_index];
    const int child_count = ui_layout_child_count(model, parent_index);
    if (child_count == 0)
        return true;

    slayer3d_ui_layout_rect content;
    ui_layout_content_rect(parent, &content);
    const float row_fill =
        ui_layout_distributed_fill_extent(model, parent_index, SLAYER3D_UI_LAYOUT_AXIS_ROW, content.w, parent->gap);
    const float column_fill =
        ui_layout_distributed_fill_extent(model, parent_index, SLAYER3D_UI_LAYOUT_AXIS_COLUMN, content.h, parent->gap);
    float cursor_x = content.x;
    float cursor_y = content.y;

    for (int i = 0; i < model->count; ++i)
    {
        ui_layout_node *child = &model->nodes[i];
        if (child->parent_index != parent_index)
            continue;

        if (parent->axis == SLAYER3D_UI_LAYOUT_AXIS_ROW)
        {
            child->resolved_rect.x = cursor_x + child->local_rect.x;
            child->resolved_rect.y = content.y + child->local_rect.y;
            child->resolved_rect.w = ui_layout_node_width(child, row_fill);
            child->resolved_rect.h = ui_layout_node_height(child, content.h);
            cursor_x += child->resolved_rect.w + parent->gap;
        }
        else if (parent->axis == SLAYER3D_UI_LAYOUT_AXIS_COLUMN)
        {
            child->resolved_rect.x = content.x + child->local_rect.x;
            child->resolved_rect.y = cursor_y + child->local_rect.y;
            child->resolved_rect.w = ui_layout_node_width(child, content.w);
            child->resolved_rect.h = ui_layout_node_height(child, column_fill);
            cursor_y += child->resolved_rect.h + parent->gap;
        }
        else
        {
            child->resolved_rect.x = content.x + child->local_rect.x;
            child->resolved_rect.y = content.y + child->local_rect.y;
            child->resolved_rect.w = ui_layout_node_width(child, content.w);
            child->resolved_rect.h = ui_layout_node_height(child, content.h);
        }

        child->resolved = true;
        child->resolving = false;
        if (!ui_layout_resolve_children(model, i, viewport_w, viewport_h))
            return false;
    }
    return true;
}

static bool ui_layout_resolve_node(slayer3d_ui_layout_model *model, int index, float viewport_w, float viewport_h)
{
    ui_layout_node *node = &model->nodes[index];
    if (node->resolved)
        return true;
    if (node->resolving)
        return false;
    node->resolving = true;

    if (node->parent_index >= 0)
    {
        if (!ui_layout_resolve_node(model, node->parent_index, viewport_w, viewport_h))
            return false;
    }
    else
    {
        node->resolved_rect.x = node->local_rect.x;
        node->resolved_rect.y = node->local_rect.y;
        node->resolved_rect.w = ui_layout_node_width(node, viewport_w);
        node->resolved_rect.h = ui_layout_node_height(node, viewport_h);
        node->resolved = true;
        node->resolving = false;
        return ui_layout_resolve_children(model, index, viewport_w, viewport_h);
    }

    node->resolving = false;
    return true;
}

static void ui_layout_store_resolved_nodes(slayer3d_ui_layout_model *model)
{
    for (int i = 0; i < model->count; ++i)
    {
        const ui_layout_node *node = &model->nodes[i];
        slayer3d_ui_layout_resolved_node *resolved = &model->resolved_nodes[i];
        SDL_zero(*resolved);
        ui_layout_copy_id(resolved->id, node->id);
        ui_layout_copy_id(resolved->parent_id, node->parent_id);
        resolved->parent_index = node->parent_index;
        resolved->type = node->type;
        resolved->axis = node->axis;
        resolved->rect = node->resolved_rect;
    }
}

bool slayer3d_ui_layout_resolve(slayer3d_ui_layout_model *model, float viewport_w, float viewport_h)
{
    if (model == NULL || viewport_w <= 0.0f || viewport_h <= 0.0f)
        return false;

    for (int i = 0; i < model->count; ++i)
    {
        ui_layout_node *node = &model->nodes[i];
        node->resolved = false;
        node->resolving = false;
        node->parent_index = node->parent_id[0] != '\0' ? ui_layout_find_node_index(model, node->parent_id) : -1;
        if (node->parent_id[0] != '\0' && node->parent_index < 0)
            return false;
    }

    for (int i = 0; i < model->count; ++i)
    {
        if (model->nodes[i].parent_index < 0 && !ui_layout_resolve_node(model, i, viewport_w, viewport_h))
            return false;
    }
    ui_layout_store_resolved_nodes(model);
    return true;
}

int slayer3d_ui_layout_node_count(const slayer3d_ui_layout_model *model)
{
    return model != NULL ? model->count : 0;
}

const slayer3d_ui_layout_resolved_node *slayer3d_ui_layout_resolved_node_at(const slayer3d_ui_layout_model *model,
                                                                            int index)
{
    if (model == NULL || index < 0 || index >= model->count || !model->nodes[index].resolved)
        return NULL;
    return &model->resolved_nodes[index];
}

const slayer3d_ui_layout_resolved_node *slayer3d_ui_layout_find_resolved_node(const slayer3d_ui_layout_model *model,
                                                                              const char *id)
{
    const int index = ui_layout_find_node_index(model, id);
    return index >= 0 ? slayer3d_ui_layout_resolved_node_at(model, index) : NULL;
}

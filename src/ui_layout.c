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
    bool has_resolved_clip_rect;
    slayer3d_ui_layout_rect resolved_clip_rect;
    float padding;
    float gap;
    bool clip_children;
    int layer;
    int resolved_layer;
    bool interactive;
    char text[SLAYER3D_UI_LAYOUT_TEXT_MAX];
    char font[SLAYER3D_UI_LAYOUT_FONT_MAX];
    char action[SLAYER3D_UI_LAYOUT_ACTION_MAX];
    slayer3d_color text_color;
    bool has_text_color;
    float text_scale;
    slayer3d_ui_layout_text_align text_align;
    slayer3d_color fill_color;
    bool has_fill_color;
    slayer3d_color border_color;
    bool has_border_color;
    float border_thickness;
    char options[SLAYER3D_UI_LAYOUT_DROPDOWN_OPTION_MAX][SLAYER3D_UI_LAYOUT_TEXT_MAX];
    int option_count;
    int selected_index;
    bool open;
    float option_height;
    bool hovered;
    bool active;
    bool selected;
    bool resolved;
    bool resolving;
} ui_layout_node;

struct slayer3d_ui_layout_model
{
    ui_layout_node *nodes;
    slayer3d_ui_layout_resolved_node *resolved_nodes;
    slayer3d_ui_layout_render_command *render_commands;
    slayer3d_ui_layout_hit_region *hit_regions;
    int count;
    int capacity;
    int flat_capacity;
    int render_count;
    int hit_region_count;
    int generation;
    float resolved_viewport_w;
    float resolved_viewport_h;
    char active_id[SLAYER3D_UI_LAYOUT_ID_MAX];
    char hover_id[SLAYER3D_UI_LAYOUT_ID_MAX];
    bool dirty;
};

enum
{
    UI_LAYOUT_POPUP_LAYER_OFFSET = 1000,
};

static bool ui_layout_id_valid(const char *id)
{
    return id != NULL && id[0] != '\0' && SDL_strlen(id) < SLAYER3D_UI_LAYOUT_ID_MAX;
}

static void ui_layout_copy_id(char *dst, const char *src)
{
    SDL_snprintf(dst, SLAYER3D_UI_LAYOUT_ID_MAX, "%s", src != NULL ? src : "");
}

static void ui_layout_copy_text(char *dst, const char *src)
{
    SDL_snprintf(dst, SLAYER3D_UI_LAYOUT_TEXT_MAX, "%s", src != NULL ? src : "");
}

static void ui_layout_copy_font(char *dst, const char *src)
{
    SDL_snprintf(dst, SLAYER3D_UI_LAYOUT_FONT_MAX, "%s", src != NULL ? src : "");
}

static void ui_layout_copy_action(char *dst, const char *src)
{
    SDL_snprintf(dst, SLAYER3D_UI_LAYOUT_ACTION_MAX, "%s", src != NULL ? src : "");
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

static bool ui_layout_reserve_flat_lists(slayer3d_ui_layout_model *model, int capacity)
{
    if (model->flat_capacity >= capacity)
        return true;
    int new_capacity = model->flat_capacity > 0 ? model->flat_capacity * 2 : 16;
    while (new_capacity < capacity)
        new_capacity *= 2;
    slayer3d_ui_layout_render_command *render_commands =
        (slayer3d_ui_layout_render_command *)SDL_calloc((size_t)new_capacity, sizeof(*render_commands));
    slayer3d_ui_layout_hit_region *hit_regions =
        (slayer3d_ui_layout_hit_region *)SDL_calloc((size_t)new_capacity, sizeof(*hit_regions));
    if (render_commands == NULL || hit_regions == NULL)
    {
        SDL_free(render_commands);
        SDL_free(hit_regions);
        return false;
    }
    SDL_free(model->render_commands);
    SDL_free(model->hit_regions);
    model->render_commands = render_commands;
    model->hit_regions = hit_regions;
    model->flat_capacity = new_capacity;
    model->render_count = 0;
    model->hit_region_count = 0;
    return true;
}

static bool ui_layout_type_interactive(slayer3d_ui_layout_node_type type)
{
    return type == SLAYER3D_UI_LAYOUT_NODE_BUTTON || type == SLAYER3D_UI_LAYOUT_NODE_DROPDOWN ||
           type == SLAYER3D_UI_LAYOUT_NODE_TAB_STRIP;
}

static bool ui_layout_rect_intersection(slayer3d_ui_layout_rect a, slayer3d_ui_layout_rect b,
                                        slayer3d_ui_layout_rect *out_rect)
{
    const float min_x = SDL_max(a.x, b.x);
    const float min_y = SDL_max(a.y, b.y);
    const float max_x = SDL_min(a.x + a.w, b.x + b.w);
    const float max_y = SDL_min(a.y + a.h, b.y + b.h);
    if (max_x <= min_x || max_y <= min_y)
    {
        if (out_rect != NULL)
            *out_rect = (slayer3d_ui_layout_rect){min_x, min_y, 0.0f, 0.0f};
        return false;
    }
    if (out_rect != NULL)
        *out_rect = (slayer3d_ui_layout_rect){min_x, min_y, max_x - min_x, max_y - min_y};
    return true;
}

static bool ui_layout_clip_allows_rect(bool has_clip_rect, slayer3d_ui_layout_rect clip_rect,
                                       slayer3d_ui_layout_rect rect)
{
    if (!has_clip_rect)
        return true;
    slayer3d_ui_layout_rect ignored;
    return ui_layout_rect_intersection(clip_rect, rect, &ignored);
}

static bool ui_layout_text_valid(const char *text)
{
    return text == NULL || SDL_strlen(text) < SLAYER3D_UI_LAYOUT_TEXT_MAX;
}

static bool ui_layout_action_valid(const char *action)
{
    return action == NULL || SDL_strlen(action) < SLAYER3D_UI_LAYOUT_ACTION_MAX;
}

static bool ui_layout_font_valid(const char *font)
{
    return font == NULL || SDL_strlen(font) < SLAYER3D_UI_LAYOUT_FONT_MAX;
}

static bool ui_layout_dropdown_options_valid(const slayer3d_ui_layout_node_desc *desc)
{
    if (desc->option_count < 0 || desc->option_count > SLAYER3D_UI_LAYOUT_DROPDOWN_OPTION_MAX)
        return false;
    if (desc->option_count > 0 && desc->options == NULL)
        return false;
    if (desc->selected_index < -1 || (desc->option_count == 0 && desc->selected_index > 0) ||
        (desc->option_count > 0 && desc->selected_index >= desc->option_count))
        return false;
    if (desc->option_height < 0.0f)
        return false;
    for (int i = 0; i < desc->option_count; ++i)
    {
        const char *option = desc->options[i];
        if (option == NULL || option[0] == '\0' || SDL_strlen(option) >= SLAYER3D_UI_LAYOUT_TEXT_MAX)
            return false;
    }
    return true;
}

bool slayer3d_ui_layout_create(slayer3d_ui_layout_model **out_model)
{
    if (out_model == NULL)
        return false;
    slayer3d_ui_layout_model *model = (slayer3d_ui_layout_model *)SDL_calloc(1, sizeof(*model));
    if (model == NULL)
        return false;
    model->dirty = true;
    *out_model = model;
    return true;
}

void slayer3d_ui_layout_destroy(slayer3d_ui_layout_model *model)
{
    if (model == NULL)
        return;
    SDL_free(model->nodes);
    SDL_free(model->resolved_nodes);
    SDL_free(model->render_commands);
    SDL_free(model->hit_regions);
    SDL_free(model);
}

void slayer3d_ui_layout_clear(slayer3d_ui_layout_model *model)
{
    if (model == NULL)
        return;
    model->count = 0;
    model->render_count = 0;
    model->hit_region_count = 0;
    model->active_id[0] = '\0';
    model->hover_id[0] = '\0';
    model->dirty = true;
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
    if (desc->padding < 0.0f || desc->gap < 0.0f || desc->border_thickness < 0.0f)
        return false;
    if (desc->text_scale < 0.0f || desc->text_align < SLAYER3D_UI_LAYOUT_TEXT_ALIGN_AUTO ||
        desc->text_align > SLAYER3D_UI_LAYOUT_TEXT_ALIGN_RIGHT)
    {
        return false;
    }
    if (!ui_layout_text_valid(desc->text) || !ui_layout_font_valid(desc->font) || !ui_layout_action_valid(desc->action))
        return false;
    if (desc->type != SLAYER3D_UI_LAYOUT_NODE_DROPDOWN && desc->option_count > 0)
        return false;
    if (!ui_layout_dropdown_options_valid(desc))
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
    node->clip_children = desc->clip_children;
    node->layer = desc->layer;
    node->resolved_layer = desc->layer;
    ui_layout_copy_text(node->text, desc->text);
    ui_layout_copy_font(node->font, desc->font);
    ui_layout_copy_action(node->action, desc->action);
    node->text_color = desc->text_color;
    node->has_text_color = desc->has_text_color;
    node->text_scale = desc->text_scale;
    node->text_align = desc->text_align;
    node->fill_color = desc->fill_color;
    node->has_fill_color = desc->has_fill_color;
    node->border_color = desc->border_color;
    node->has_border_color = desc->has_border_color;
    node->border_thickness = desc->border_thickness;
    node->option_count = desc->type == SLAYER3D_UI_LAYOUT_NODE_DROPDOWN ? desc->option_count : 0;
    node->selected_index = desc->selected_index;
    node->open = desc->open;
    node->option_height = desc->option_height;
    for (int i = 0; i < node->option_count; ++i)
        ui_layout_copy_text(node->options[i], desc->options[i]);
    node->interactive = desc->interactive || desc->action != NULL || ui_layout_type_interactive(desc->type);
    node->selected = desc->selected;
    model->dirty = true;
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
        child->resolved_layer = SDL_max(child->layer, parent->resolved_layer + 1);
        child->has_resolved_clip_rect = parent->has_resolved_clip_rect;
        child->resolved_clip_rect = parent->resolved_clip_rect;
        if (parent->clip_children)
        {
            if (child->has_resolved_clip_rect)
            {
                child->has_resolved_clip_rect =
                    ui_layout_rect_intersection(child->resolved_clip_rect, content, &child->resolved_clip_rect);
            }
            else
            {
                child->has_resolved_clip_rect = true;
                child->resolved_clip_rect = content;
            }
        }
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
        node->resolved_layer = node->layer;
        node->has_resolved_clip_rect = false;
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
        resolved->has_clip_rect = node->has_resolved_clip_rect;
        resolved->clip_rect = node->resolved_clip_rect;
        resolved->layer = node->resolved_layer;
        resolved->interactive = node->interactive;
        ui_layout_copy_text(resolved->text, node->text);
        ui_layout_copy_font(resolved->font, node->font);
        ui_layout_copy_action(resolved->action, node->action);
        resolved->text_color = node->text_color;
        resolved->has_text_color = node->has_text_color;
        resolved->text_scale = node->text_scale;
        resolved->text_align = node->text_align;
        resolved->hovered = node->hovered;
        resolved->active = node->active;
        resolved->selected = node->selected;
        resolved->fill_color = node->fill_color;
        resolved->has_fill_color = node->has_fill_color;
        resolved->border_color = node->border_color;
        resolved->has_border_color = node->has_border_color;
        resolved->border_thickness = node->border_thickness;
    }
}

static bool ui_layout_rect_contains(slayer3d_ui_layout_rect rect, float x, float y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

static void ui_layout_swap_render_commands(slayer3d_ui_layout_render_command *a, slayer3d_ui_layout_render_command *b)
{
    slayer3d_ui_layout_render_command tmp = *a;
    *a = *b;
    *b = tmp;
}

static void ui_layout_swap_hit_regions(slayer3d_ui_layout_hit_region *a, slayer3d_ui_layout_hit_region *b)
{
    slayer3d_ui_layout_hit_region tmp = *a;
    *a = *b;
    *b = tmp;
}

static void ui_layout_sort_flat_lists(slayer3d_ui_layout_model *model)
{
    for (int i = 1; i < model->render_count; ++i)
    {
        for (int j = i; j > 0 && model->render_commands[j - 1].layer > model->render_commands[j].layer; --j)
            ui_layout_swap_render_commands(&model->render_commands[j - 1], &model->render_commands[j]);
    }
    for (int i = 1; i < model->hit_region_count; ++i)
    {
        for (int j = i; j > 0 && model->hit_regions[j - 1].layer > model->hit_regions[j].layer; --j)
            ui_layout_swap_hit_regions(&model->hit_regions[j - 1], &model->hit_regions[j]);
    }
}

static int ui_layout_required_flat_capacity(const slayer3d_ui_layout_model *model)
{
    int required = model->count;
    for (int i = 0; i < model->count; ++i)
    {
        const ui_layout_node *node = &model->nodes[i];
        if (node->type == SLAYER3D_UI_LAYOUT_NODE_DROPDOWN && node->open && node->option_count > 0)
            required += 1 + node->option_count;
    }
    return required;
}

static bool ui_layout_id_matches(const char *a, const char *b)
{
    return a != NULL && b != NULL && a[0] != '\0' && SDL_strcmp(a, b) == 0;
}

static void ui_layout_store_render_command(slayer3d_ui_layout_model *model, const char *id, const char *owner_id,
                                           slayer3d_ui_layout_node_type type, slayer3d_ui_layout_rect rect, int layer,
                                           bool has_clip_rect, slayer3d_ui_layout_rect clip_rect, const char *text,
                                           const char *font, bool selected, bool popup, int option_index,
                                           slayer3d_color text_color, bool has_text_color, float text_scale,
                                           slayer3d_ui_layout_text_align text_align, slayer3d_color fill_color,
                                           bool has_fill_color, slayer3d_color border_color, bool has_border_color,
                                           float border_thickness)
{
    slayer3d_ui_layout_render_command *render = &model->render_commands[model->render_count++];
    SDL_zero(*render);
    ui_layout_copy_id(render->id, id);
    ui_layout_copy_id(render->owner_id, owner_id != NULL ? owner_id : id);
    render->type = type;
    render->rect = rect;
    render->has_clip_rect = has_clip_rect;
    render->clip_rect = clip_rect;
    render->layer = layer;
    ui_layout_copy_text(render->text, text);
    ui_layout_copy_font(render->font, font);
    render->text_color = text_color;
    render->has_text_color = has_text_color;
    render->text_scale = text_scale;
    render->text_align = text_align;
    render->fill_color = fill_color;
    render->has_fill_color = has_fill_color;
    render->border_color = border_color;
    render->has_border_color = has_border_color;
    render->border_thickness = border_thickness;
    render->hovered = ui_layout_id_matches(model->hover_id, id);
    render->active = ui_layout_id_matches(model->active_id, id);
    render->selected = selected;
    render->popup = popup;
    render->option_index = option_index;
}

static void ui_layout_store_hit_region(slayer3d_ui_layout_model *model, const char *id, const char *owner_id,
                                       slayer3d_ui_layout_node_type type, slayer3d_ui_layout_rect rect, int layer,
                                       bool has_clip_rect, slayer3d_ui_layout_rect clip_rect, const char *action,
                                       bool selected, int option_index)
{
    slayer3d_ui_layout_hit_region *hit = &model->hit_regions[model->hit_region_count++];
    SDL_zero(*hit);
    ui_layout_copy_id(hit->id, id);
    ui_layout_copy_id(hit->owner_id, owner_id != NULL ? owner_id : id);
    hit->type = type;
    hit->rect = rect;
    hit->has_clip_rect = has_clip_rect;
    hit->clip_rect = clip_rect;
    hit->layer = layer;
    ui_layout_copy_action(hit->action, action);
    hit->hovered = ui_layout_id_matches(model->hover_id, id);
    hit->active = ui_layout_id_matches(model->active_id, id);
    hit->selected = selected;
    hit->option_index = option_index;
}

static slayer3d_ui_layout_rect ui_layout_dropdown_popup_rect(const slayer3d_ui_layout_model *model,
                                                             const ui_layout_node *node)
{
    const float option_height = node->option_height > 0.0f ? node->option_height : node->resolved_rect.h;
    slayer3d_ui_layout_rect rect = {
        node->resolved_rect.x,
        node->resolved_rect.y + node->resolved_rect.h,
        node->resolved_rect.w,
        option_height * (float)node->option_count,
    };
    if (rect.x + rect.w > model->resolved_viewport_w)
        rect.x = SDL_max(0.0f, model->resolved_viewport_w - rect.w);
    if (rect.y + rect.h > model->resolved_viewport_h)
        rect.y = SDL_max(0.0f, node->resolved_rect.y - rect.h);
    return rect;
}

static bool ui_layout_compile_dropdown(slayer3d_ui_layout_model *model, const ui_layout_node *node)
{
    if (!node->open || node->option_count <= 0)
        return true;

    char popup_id[SLAYER3D_UI_LAYOUT_ID_MAX];
    SDL_snprintf(popup_id, sizeof(popup_id), "%s.popup", node->id);
    const int popup_layer = node->resolved_layer + UI_LAYOUT_POPUP_LAYER_OFFSET;
    const slayer3d_ui_layout_rect popup_rect = ui_layout_dropdown_popup_rect(model, node);
    ui_layout_store_render_command(model, popup_id, node->id, SLAYER3D_UI_LAYOUT_NODE_PANEL, popup_rect, popup_layer,
                                   false, (slayer3d_ui_layout_rect){0}, "", node->font, false, true, -1,
                                   (slayer3d_color){0}, false, 0.0f, SLAYER3D_UI_LAYOUT_TEXT_ALIGN_AUTO,
                                   (slayer3d_color){0}, false, (slayer3d_color){0}, false, 0.0f);

    const float option_height = node->option_height > 0.0f ? node->option_height : node->resolved_rect.h;
    for (int i = 0; i < node->option_count; ++i)
    {
        char option_id[SLAYER3D_UI_LAYOUT_ID_MAX];
        SDL_snprintf(option_id, sizeof(option_id), "%s.option.%d", node->id, i);
        slayer3d_ui_layout_rect option_rect = {
            popup_rect.x,
            popup_rect.y + option_height * (float)i,
            popup_rect.w,
            option_height,
        };
        const bool selected = i == node->selected_index;
        ui_layout_store_render_command(model, option_id, node->id, SLAYER3D_UI_LAYOUT_NODE_BUTTON, option_rect,
                                       popup_layer + 1, false, (slayer3d_ui_layout_rect){0}, node->options[i],
                                       node->font, selected, false, i, node->text_color, node->has_text_color,
                                       node->text_scale, node->text_align, (slayer3d_color){0}, false,
                                       (slayer3d_color){0}, false, 0.0f);
        ui_layout_store_hit_region(model, option_id, node->id, SLAYER3D_UI_LAYOUT_NODE_BUTTON, option_rect,
                                   popup_layer + 1, false, (slayer3d_ui_layout_rect){0}, node->action, selected, i);
    }
    return true;
}

static bool ui_layout_compile_flat_lists(slayer3d_ui_layout_model *model)
{
    if (!ui_layout_reserve_flat_lists(model, ui_layout_required_flat_capacity(model)))
        return false;
    model->render_count = 0;
    model->hit_region_count = 0;
    for (int i = 0; i < model->count; ++i)
    {
        const slayer3d_ui_layout_resolved_node *node = &model->resolved_nodes[i];
        const ui_layout_node *source = &model->nodes[i];
        const bool visible_in_clip = ui_layout_clip_allows_rect(node->has_clip_rect, node->clip_rect, node->rect);
        if (visible_in_clip)
        {
            ui_layout_store_render_command(model, node->id, node->id, node->type, node->rect, node->layer,
                                           node->has_clip_rect, node->clip_rect, node->text, node->font, node->selected,
                                           false, -1, node->text_color, node->has_text_color, node->text_scale,
                                           node->text_align, node->fill_color, node->has_fill_color, node->border_color,
                                           node->has_border_color, node->border_thickness);
        }

        if (node->interactive && visible_in_clip)
        {
            ui_layout_store_hit_region(model, node->id, node->id, node->type, node->rect, node->layer,
                                       node->has_clip_rect, node->clip_rect, node->action, node->selected, -1);
        }
        if (source->type == SLAYER3D_UI_LAYOUT_NODE_DROPDOWN && !ui_layout_compile_dropdown(model, source))
            return false;
    }
    ui_layout_sort_flat_lists(model);
    return true;
}

bool slayer3d_ui_layout_resolve(slayer3d_ui_layout_model *model, float viewport_w, float viewport_h)
{
    if (model == NULL || viewport_w <= 0.0f || viewport_h <= 0.0f)
        return false;
    if (!model->dirty && model->resolved_viewport_w == viewport_w && model->resolved_viewport_h == viewport_h)
        return true;

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
    model->resolved_viewport_w = viewport_w;
    model->resolved_viewport_h = viewport_h;
    if (!ui_layout_compile_flat_lists(model))
        return false;
    model->dirty = false;
    ++model->generation;
    return true;
}

void slayer3d_ui_layout_mark_dirty(slayer3d_ui_layout_model *model)
{
    if (model != NULL)
        model->dirty = true;
}

bool slayer3d_ui_layout_is_dirty(const slayer3d_ui_layout_model *model)
{
    return model != NULL && model->dirty;
}

int slayer3d_ui_layout_generation(const slayer3d_ui_layout_model *model)
{
    return model != NULL ? model->generation : 0;
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

int slayer3d_ui_layout_render_command_count(const slayer3d_ui_layout_model *model)
{
    return model != NULL ? model->render_count : 0;
}

const slayer3d_ui_layout_render_command *slayer3d_ui_layout_render_command_at(const slayer3d_ui_layout_model *model,
                                                                              int index)
{
    if (model == NULL || index < 0 || index >= model->render_count)
        return NULL;
    return &model->render_commands[index];
}

int slayer3d_ui_layout_hit_region_count(const slayer3d_ui_layout_model *model)
{
    return model != NULL ? model->hit_region_count : 0;
}

const slayer3d_ui_layout_hit_region *slayer3d_ui_layout_hit_region_at(const slayer3d_ui_layout_model *model, int index)
{
    if (model == NULL || index < 0 || index >= model->hit_region_count)
        return NULL;
    return &model->hit_regions[index];
}

const slayer3d_ui_layout_hit_region *slayer3d_ui_layout_hit_test(const slayer3d_ui_layout_model *model, float x,
                                                                 float y)
{
    if (model == NULL)
        return NULL;
    for (int i = model->hit_region_count - 1; i >= 0; --i)
    {
        const slayer3d_ui_layout_hit_region *region = &model->hit_regions[i];
        if (ui_layout_rect_contains(region->rect, x, y) &&
            (!region->has_clip_rect || ui_layout_rect_contains(region->clip_rect, x, y)))
        {
            return region;
        }
    }
    return NULL;
}

static void ui_layout_clear_pointer_state(slayer3d_ui_layout_model *model)
{
    for (int i = 0; i < model->count; ++i)
    {
        model->nodes[i].hovered = false;
        model->nodes[i].active = false;
    }
}

bool slayer3d_ui_layout_update_input(slayer3d_ui_layout_model *model, const slayer3d_ui_layout_input_state *input,
                                     slayer3d_ui_layout_activation *out_activation)
{
    if (out_activation != NULL)
        SDL_zero(*out_activation);
    if (model == NULL || input == NULL || model->dirty)
        return false;

    const slayer3d_ui_layout_hit_region *hit = slayer3d_ui_layout_hit_test(model, input->pointer_x, input->pointer_y);
    const char *hit_id = hit != NULL ? hit->id : NULL;
    ui_layout_copy_id(model->hover_id, hit_id);
    ui_layout_clear_pointer_state(model);

    if (hit_id != NULL)
    {
        const int hit_index = ui_layout_find_node_index(model, hit->owner_id);
        if (hit_index >= 0)
            model->nodes[hit_index].hovered = true;
    }

    if (input->primary_pressed)
        ui_layout_copy_id(model->active_id, hit_id);

    const int active_index = ui_layout_find_node_index(model, model->active_id);
    if (active_index >= 0 && input->primary_down)
        model->nodes[active_index].active = true;

    if (input->primary_released)
    {
        const bool activated =
            hit_id != NULL && model->active_id[0] != '\0' && SDL_strcmp(hit_id, model->active_id) == 0;
        if (activated && out_activation != NULL)
        {
            out_activation->activated = true;
            ui_layout_copy_id(out_activation->id, hit_id);
            ui_layout_copy_id(out_activation->owner_id, hit->owner_id);
            ui_layout_copy_action(out_activation->action, hit->action);
            out_activation->option_index = hit->option_index;
        }
        model->active_id[0] = '\0';
    }

    ui_layout_store_resolved_nodes(model);
    return ui_layout_compile_flat_lists(model);
}

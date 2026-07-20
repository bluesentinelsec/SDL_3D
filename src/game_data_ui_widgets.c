/**
 * @file game_data_ui_widgets.c
 * @brief Runtime bridge from authored game-data UI widgets to retained UI layout.
 */

#include "game_data_internal.h"

static slayer3d_ui_layout_node_type ui_widget_type_from_string(const char *type)
{
    if (type == NULL)
        return SLAYER3D_UI_LAYOUT_NODE_PANEL;
    if (SDL_strcmp(type, "toolbar") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_TOOLBAR;
    if (SDL_strcmp(type, "row") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_ROW;
    if (SDL_strcmp(type, "column") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_COLUMN;
    if (SDL_strcmp(type, "button") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    if (SDL_strcmp(type, "label") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_LABEL;
    if (SDL_strcmp(type, "dropdown") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_DROPDOWN;
    if (SDL_strcmp(type, "tab_strip") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_TAB_STRIP;
    if (SDL_strcmp(type, "spacer") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_SPACER;
    if (SDL_strcmp(type, "console") == 0 || SDL_strcmp(type, "log_view") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_CONSOLE;
    if (SDL_strcmp(type, "image") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_IMAGE;
    if (SDL_strcmp(type, "scroll") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_SCROLL;
    return SLAYER3D_UI_LAYOUT_NODE_PANEL;
}

static slayer3d_ui_layout_axis ui_widget_axis_from_string(const char *layout)
{
    if (layout != NULL && SDL_strcmp(layout, "row") == 0)
        return SLAYER3D_UI_LAYOUT_AXIS_ROW;
    if (layout != NULL && SDL_strcmp(layout, "column") == 0)
        return SLAYER3D_UI_LAYOUT_AXIS_COLUMN;
    if (layout != NULL && SDL_strcmp(layout, "grid") == 0)
        return SLAYER3D_UI_LAYOUT_AXIS_GRID;
    return SLAYER3D_UI_LAYOUT_AXIS_NONE;
}

static yyjson_val *ui_widget_size_value(yyjson_val *node, const char *short_key, const char *long_key)
{
    yyjson_val *value = obj_get(node, short_key);
    return value != NULL ? value : obj_get(node, long_key);
}

static slayer3d_ui_layout_size_mode ui_widget_size_mode_from_value(yyjson_val *value)
{
    return yyjson_is_str(value) && SDL_strcmp(yyjson_get_str(value), "fill") == 0 ? SLAYER3D_UI_LAYOUT_SIZE_FILL
                                                                                  : SLAYER3D_UI_LAYOUT_SIZE_FIXED;
}

static float ui_widget_size_number(yyjson_val *value, float fallback)
{
    return yyjson_is_num(value) ? (float)yyjson_get_num(value) : fallback;
}

static int ui_widget_int(yyjson_val *node, const char *primary_key, const char *secondary_key, int fallback)
{
    yyjson_val *value = obj_get(node, primary_key);
    if (value == NULL)
        value = obj_get(node, secondary_key);
    return yyjson_is_int(value) ? (int)yyjson_get_int(value) : fallback;
}

static bool ui_widget_bool(yyjson_val *node, const char *key, bool fallback)
{
    yyjson_val *value = obj_get(node, key);
    return yyjson_is_bool(value) ? yyjson_get_bool(value) : fallback;
}

static slayer3d_ui_layout_text_align ui_widget_text_align_from_string(const char *align)
{
    if (align != NULL && SDL_strcmp(align, "left") == 0)
        return SLAYER3D_UI_LAYOUT_TEXT_ALIGN_LEFT;
    if (align != NULL && SDL_strcmp(align, "center") == 0)
        return SLAYER3D_UI_LAYOUT_TEXT_ALIGN_CENTER;
    if (align != NULL && SDL_strcmp(align, "right") == 0)
        return SLAYER3D_UI_LAYOUT_TEXT_ALIGN_RIGHT;
    return SLAYER3D_UI_LAYOUT_TEXT_ALIGN_AUTO;
}

static const char *ui_widget_string(yyjson_val *node, const char *primary_key, const char *secondary_key)
{
    const char *value = json_string(node, primary_key, NULL);
    return value != NULL ? value : json_string(node, secondary_key, NULL);
}

static bool ui_widget_scene_float(const slayer3d_game_data_runtime *runtime, const char *key, float *out_value)
{
    if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0' || out_value == NULL)
        return false;

    const slayer3d_value *value = slayer3d_properties_get_value(runtime->scene_state, key);
    if (value == NULL)
        return false;
    if (value->type == SLAYER3D_VALUE_FLOAT)
    {
        *out_value = value->as_float;
        return true;
    }
    if (value->type == SLAYER3D_VALUE_INT)
    {
        *out_value = (float)value->as_int;
        return true;
    }
    return false;
}

static const char *ui_widget_scene_string(const slayer3d_game_data_runtime *runtime, const char *key,
                                          const char *fallback)
{
    if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
        return fallback;
    return slayer3d_properties_get_string(runtime->scene_state, key, fallback);
}

static slayer3d_ui_layout_dock ui_widget_dock_from_string(const char *dock)
{
    if (dock != NULL && SDL_strcmp(dock, "left") == 0)
        return SLAYER3D_UI_LAYOUT_DOCK_LEFT;
    if (dock != NULL && SDL_strcmp(dock, "right") == 0)
        return SLAYER3D_UI_LAYOUT_DOCK_RIGHT;
    if (dock != NULL && SDL_strcmp(dock, "bottom") == 0)
        return SLAYER3D_UI_LAYOUT_DOCK_BOTTOM;
    return SLAYER3D_UI_LAYOUT_DOCK_NONE;
}

static bool ui_widget_format_bound_text(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_ui_metrics *metrics, yyjson_val *node, char *buffer,
                                        size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    buffer[0] = '\0';

    const char *format = json_string(node, "format", NULL);
    yyjson_val *bindings = obj_get(node, "bindings");
    if (format == NULL || !yyjson_is_arr(bindings) || yyjson_arr_size(bindings) == 0)
        return false;

    size_t binding_index = 0;
    size_t write_index = 0;
    for (const char *cursor = format; *cursor != '\0'; ++cursor)
    {
        if (*cursor != '%')
        {
            if (write_index + 1U >= buffer_size)
                return false;
            buffer[write_index++] = *cursor;
            buffer[write_index] = '\0';
            continue;
        }

        ++cursor;
        if (*cursor == '\0')
            return false;
        if (*cursor == '%')
        {
            if (write_index + 1U >= buffer_size)
                return false;
            buffer[write_index++] = '%';
            buffer[write_index] = '\0';
            continue;
        }
        if (*cursor != 's' || binding_index >= yyjson_arr_size(bindings))
            return false;

        char value[64];
        if (!slayer3d_game_data_ui_binding_to_string(runtime, metrics, yyjson_arr_get(bindings, binding_index), value,
                                                     sizeof(value)))
        {
            return false;
        }
        const size_t value_len = SDL_strlen(value);
        if (write_index + value_len >= buffer_size)
            return false;
        SDL_memcpy(buffer + write_index, value, value_len + 1U);
        write_index += value_len;
        ++binding_index;
    }
    return binding_index == yyjson_arr_size(bindings);
}

static int ui_widget_selected_index_from_values(const slayer3d_game_data_runtime *runtime, yyjson_val *node,
                                                int fallback)
{
    const char *key = json_string(node, "selected_value_key", NULL);
    yyjson_val *values = obj_get(node, "values");
    float selected_value = 0.0f;
    if (!ui_widget_scene_float(runtime, key, &selected_value) || !yyjson_is_arr(values))
        return fallback;

    for (size_t i = 0, count = yyjson_arr_size(values); i < count; ++i)
    {
        yyjson_val *value = yyjson_arr_get(values, i);
        if (yyjson_is_num(value) && SDL_fabsf((float)yyjson_get_num(value) - selected_value) <= 0.0001f)
            return (int)i;
    }
    return fallback;
}

static const char *ui_widget_dynamic_text(const slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_ui_metrics *metrics, yyjson_val *node, char *buffer,
                                          size_t buffer_size)
{
    if (ui_widget_format_bound_text(runtime, metrics, node, buffer, buffer_size))
        return buffer;

    const char *format = json_string(node, "text_format", NULL);
    const char *key = json_string(node, "text_value_key", NULL);
    float value = 0.0f;
    if (format == NULL || key == NULL || buffer == NULL || buffer_size == 0U ||
        !ui_widget_scene_float(runtime, key, &value))
    {
        return ui_widget_string(node, "text", "label");
    }

    SDL_snprintf(buffer, buffer_size, format, value);
    return buffer;
}

static bool ui_widget_add_node(const slayer3d_game_data_runtime *runtime, const slayer3d_game_data_ui_metrics *metrics,
                               yyjson_val *node, const char *parent_id, slayer3d_ui_layout_model *layout)
{
    yyjson_val *visible_if = obj_get(node, "visible_if");
    if (visible_if != NULL && !eval_data_condition(runtime, visible_if, metrics))
        return true;

    yyjson_val *width = ui_widget_size_value(node, "w", "width");
    yyjson_val *height = ui_widget_size_value(node, "h", "height");

    slayer3d_ui_layout_node_desc desc;
    SDL_zero(desc);
    desc.id = json_string(node, "id", NULL);
    desc.parent_id = parent_id;
    desc.type = ui_widget_type_from_string(json_string(node, "type", NULL));
    desc.axis = ui_widget_axis_from_string(json_string(node, "layout", NULL));
    desc.width_mode = width != NULL ? ui_widget_size_mode_from_value(width) : SLAYER3D_UI_LAYOUT_SIZE_FILL;
    desc.height_mode = height != NULL ? ui_widget_size_mode_from_value(height) : SLAYER3D_UI_LAYOUT_SIZE_FILL;
    desc.rect.x = json_float(node, "x", 0.0f);
    desc.rect.y = json_float(node, "y", 0.0f);
    float scene_x = 0.0f;
    if (ui_widget_scene_float(runtime, json_string(node, "x_key", NULL), &scene_x))
        desc.rect.x = scene_x;
    float scene_y = 0.0f;
    if (ui_widget_scene_float(runtime, json_string(node, "y_key", NULL), &scene_y))
        desc.rect.y = scene_y;
    float scroll_y = 0.0f;
    if (ui_widget_scene_float(runtime, json_string(node, "scroll_y_key", NULL), &scroll_y))
        desc.rect.y -= scroll_y;
    desc.rect.w = ui_widget_size_number(width, 1.0f);
    desc.rect.h = ui_widget_size_number(height, 1.0f);
    float scene_w = 0.0f;
    if (ui_widget_scene_float(runtime, json_string(node, "w_key", NULL), &scene_w))
        desc.rect.w = scene_w;
    float scene_h = 0.0f;
    if (ui_widget_scene_float(runtime, json_string(node, "h_key", NULL), &scene_h))
        desc.rect.h = scene_h;
    desc.padding = json_float(node, "padding", 0.0f);
    desc.gap = json_float(node, "gap", 0.0f);
    desc.grid_columns = ui_widget_int(node, "columns", "grid_columns", 0);
    desc.clip_children = ui_widget_bool(node, "clip_children", false);
    desc.clip_rect_id = json_string(node, "clip_rect_id", NULL);
    desc.layer = ui_widget_int(node, "layer", "z", 0);
    desc.interactive = ui_widget_bool(node, "interactive", false);

    char text_buffer[SLAYER3D_UI_LAYOUT_TEXT_MAX];
    desc.text = ui_widget_dynamic_text(runtime, metrics, node, text_buffer, sizeof(text_buffer));
    desc.font = json_string(node, "font", NULL);
    desc.action = json_string(node, "action", NULL);
    desc.has_text_color = obj_get(node, "text_color") != NULL;
    desc.text_color = json_color(node, "text_color", (slayer3d_color){0, 0, 0, 0});
    desc.text_scale = json_float(node, "text_scale", 0.0f);
    desc.text_align = ui_widget_text_align_from_string(json_string(node, "align", NULL));
    yyjson_val *fill_color = obj_get(node, "color");
    if (fill_color == NULL)
        fill_color = obj_get(node, "fill_color");
    desc.has_fill_color = fill_color != NULL;
    desc.fill_color = json_color_value(fill_color, (slayer3d_color){0, 0, 0, 0});
    desc.has_border_color = obj_get(node, "border_color") != NULL;
    desc.border_color = json_color(node, "border_color", (slayer3d_color){0, 0, 0, 0});
    desc.border_thickness = json_float(node, "border_thickness", 0.0f);
    yyjson_val *selected_if = obj_get(node, "selected_if");
    desc.selected = ui_widget_bool(node, "selected", false) ||
                    (selected_if != NULL && eval_data_condition(runtime, selected_if, metrics));
    desc.selected_index =
        ui_widget_selected_index_from_values(runtime, node, ui_widget_int(node, "selected_index", "selected_index", 0));
    desc.image = json_string(node, "image", NULL);
    desc.preserve_aspect = ui_widget_bool(node, "preserve_aspect", false);
    const char *anchor_x = json_string(node, "anchor_x", NULL);
    desc.anchor_right = anchor_x != NULL && SDL_strcmp(anchor_x, "right") == 0;
    const char *anchor_y = json_string(node, "anchor_y", NULL);
    desc.anchor_bottom = anchor_y != NULL && SDL_strcmp(anchor_y, "bottom") == 0;
    yyjson_val *window = obj_get(node, "window");
    if (yyjson_is_obj(window))
    {
        desc.window = true;
        const char *default_dock = json_string(window, "default_dock", "none");
        const char *dock = ui_widget_scene_string(runtime, json_string(window, "dock_key", NULL), default_dock);
        desc.dock = ui_widget_dock_from_string(dock);
        desc.dock_top = json_float(window, "dock_top", 0.0f);
        desc.dock_bottom = json_float(window, "dock_bottom", 0.0f);
        (void)ui_widget_scene_float(runtime, json_string(window, "dock_top_key", NULL), &desc.dock_top);
        (void)ui_widget_scene_float(runtime, json_string(window, "dock_bottom_key", NULL), &desc.dock_bottom);
        desc.dock_margin = json_float(window, "dock_margin", 0.0f);
        desc.dock_gap = json_float(window, "dock_gap", 0.0f);
        desc.dock_width = json_float(window, "dock_width", 0.0f);
        desc.dock_height = json_float(window, "dock_height", 0.0f);
    }
    desc.scroll_key = json_string(node, "scroll_key", NULL);
    float pane_scroll = 0.0f;
    if (ui_widget_scene_float(runtime, desc.scroll_key, &pane_scroll))
        desc.scroll_offset = pane_scroll;
    float scroll_span = 0.0f;
    if (ui_widget_scene_float(runtime, json_string(node, "scroll_count_key", NULL), &scroll_span))
        desc.scroll_span = SDL_max(scroll_span, 0.0f);
    desc.scroll_signal = json_string(node, "scroll_signal", NULL);
    desc.scroll_step = json_float(node, "scroll_step", 0.0f);
    desc.scrollbar_always = ui_widget_bool(node, "scrollbar_always", false);
    desc.open = ui_widget_bool(node, "open", false);
    const char *open_key = json_string(node, "open_key", NULL);
    if (open_key != NULL && open_key[0] != '\0' && runtime != NULL && runtime->scene_state != NULL)
        desc.open = slayer3d_properties_get_bool(runtime->scene_state, open_key, desc.open);
    desc.option_height = json_float(node, "option_height", 0.0f);

    const char *options[SLAYER3D_UI_LAYOUT_DROPDOWN_OPTION_MAX];
    yyjson_val *option_values = obj_get(node, "options");
    if (yyjson_is_arr(option_values))
    {
        desc.option_count = (int)yyjson_arr_size(option_values);
        desc.options = options;
        for (int i = 0; i < desc.option_count; ++i)
            options[i] = yyjson_get_str(yyjson_arr_get(option_values, (size_t)i));
    }

    if (!slayer3d_ui_layout_add_node(layout, &desc))
        return false;

    yyjson_val *children = obj_get(node, "children");
    for (size_t i = 0; yyjson_is_arr(children) && i < yyjson_arr_size(children); ++i)
        if (!ui_widget_add_node(runtime, metrics, yyjson_arr_get(children, i), desc.id, layout))
            return false;
    return true;
}

static bool ui_widget_add_root_widgets(const slayer3d_game_data_runtime *runtime, yyjson_val *root,
                                       const slayer3d_game_data_ui_metrics *metrics, slayer3d_ui_layout_model *layout)
{
    yyjson_val *widgets = obj_get(obj_get(root, "ui"), "widgets");
    for (size_t i = 0; yyjson_is_arr(widgets) && i < yyjson_arr_size(widgets); ++i)
        if (!ui_widget_add_node(runtime, metrics, yyjson_arr_get(widgets, i), NULL, layout))
            return false;
    return true;
}

static bool ui_widget_for_each_image_id(yyjson_val *node, slayer3d_game_data_ui_widget_image_id_fn callback,
                                        void *userdata)
{
    const char *type = json_string(node, "type", NULL);
    const char *image = json_string(node, "image", NULL);
    if (type != NULL && SDL_strcmp(type, "image") == 0 && image != NULL && image[0] != '\0' &&
        !callback(userdata, image))
    {
        return false;
    }
    yyjson_val *children = obj_get(node, "children");
    for (size_t i = 0; yyjson_is_arr(children) && i < yyjson_arr_size(children); ++i)
        if (!ui_widget_for_each_image_id(yyjson_arr_get(children, i), callback, userdata))
            return false;
    return true;
}

bool slayer3d_game_data_for_each_ui_widget_image_id(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_ui_widget_image_id_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    yyjson_val *roots[2];
    roots[0] = runtime_root(runtime);
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;

    for (int root_index = 0; root_index < 2; ++root_index)
    {
        yyjson_val *widgets = obj_get(obj_get(roots[root_index], "ui"), "widgets");
        for (size_t i = 0; yyjson_is_arr(widgets) && i < yyjson_arr_size(widgets); ++i)
            if (!ui_widget_for_each_image_id(yyjson_arr_get(widgets, i), callback, userdata))
                return true;
    }
    return true;
}

bool slayer3d_game_data_sync_ui_scroll_limits(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    slayer3d_ui_layout_model *layout = NULL;
    if (!slayer3d_ui_layout_create(&layout))
        return false;
    float viewport_w = 0.0f;
    float viewport_h = 0.0f;
    slayer3d_game_data_ui_viewport(runtime, &viewport_w, &viewport_h);
    if (!slayer3d_game_data_build_active_ui_widget_layout(runtime, viewport_w, viewport_h, NULL, layout))
    {
        slayer3d_ui_layout_destroy(layout);
        return false;
    }

    for (int i = 0, count = slayer3d_ui_layout_node_count(layout); i < count; ++i)
    {
        const slayer3d_ui_layout_resolved_node *node = slayer3d_ui_layout_resolved_node_at(layout, i);
        if (node == NULL || node->scroll_key[0] == '\0' ||
            (node->type != SLAYER3D_UI_LAYOUT_NODE_SCROLL && !node->scroll_virtual))
            continue;

        /* The pane already clamps what it renders; clamping the owning state
         * key keeps steppers and drags anchored to real content bounds, and
         * the published limit lets data-authored logic bind against it.
         * Virtualized lists store item indices, typically as ints - preserve
         * the key's authored type. */
        const slayer3d_value *existing = slayer3d_properties_get_value(runtime->scene_state, node->scroll_key);
        const bool integer_key = existing != NULL && existing->type == SLAYER3D_VALUE_INT;
        const float requested = integer_key
                                    ? (float)slayer3d_properties_get_int(runtime->scene_state, node->scroll_key, 0)
                                    : slayer3d_properties_get_float(runtime->scene_state, node->scroll_key, 0.0f);
        if (requested != node->scroll_offset)
        {
            if (integer_key)
                slayer3d_properties_set_int(runtime->scene_state, node->scroll_key,
                                            (int)SDL_lroundf(node->scroll_offset));
            else
                slayer3d_properties_set_float(runtime->scene_state, node->scroll_key, node->scroll_offset);
        }

        char limit_key[SLAYER3D_UI_LAYOUT_ACTION_MAX + 8];
        SDL_snprintf(limit_key, sizeof(limit_key), "%s.limit", node->scroll_key);
        slayer3d_properties_set_float(runtime->scene_state, limit_key, node->scroll_max);
    }

    slayer3d_ui_layout_destroy(layout);
    return true;
}

bool slayer3d_game_data_set_ui_viewport(slayer3d_game_data_runtime *runtime, float width, float height)
{
    if (runtime == NULL || width <= 0.0f || height <= 0.0f)
        return false;
    runtime->ui_viewport_w = width;
    runtime->ui_viewport_h = height;
    return true;
}

void slayer3d_game_data_ui_viewport(const slayer3d_game_data_runtime *runtime, float *out_width, float *out_height)
{
    const bool published = runtime != NULL && runtime->ui_viewport_w > 0.0f && runtime->ui_viewport_h > 0.0f;
    if (out_width != NULL)
        *out_width = published ? runtime->ui_viewport_w : 1280.0f;
    if (out_height != NULL)
        *out_height = published ? runtime->ui_viewport_h : 720.0f;
}

bool slayer3d_game_data_build_active_ui_widget_layout(const slayer3d_game_data_runtime *runtime, float viewport_w,
                                                      float viewport_h, const slayer3d_game_data_ui_metrics *metrics,
                                                      slayer3d_ui_layout_model *layout)
{
    if (runtime == NULL || layout == NULL)
        return false;

    slayer3d_ui_layout_clear(layout);
    if (!ui_widget_add_root_widgets(runtime, runtime_root(runtime), metrics, layout))
        return false;
    const scene_entry *scene = active_scene_entry_const(runtime);
    if (scene != NULL && !ui_widget_add_root_widgets(runtime, scene->root, metrics, layout))
        return false;
    return slayer3d_ui_layout_resolve(layout, viewport_w, viewport_h);
}

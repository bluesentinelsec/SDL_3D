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
    return SLAYER3D_UI_LAYOUT_NODE_PANEL;
}

static slayer3d_ui_layout_axis ui_widget_axis_from_string(const char *layout)
{
    if (layout != NULL && SDL_strcmp(layout, "row") == 0)
        return SLAYER3D_UI_LAYOUT_AXIS_ROW;
    if (layout != NULL && SDL_strcmp(layout, "column") == 0)
        return SLAYER3D_UI_LAYOUT_AXIS_COLUMN;
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
    desc.rect.w = ui_widget_size_number(width, 1.0f);
    desc.rect.h = ui_widget_size_number(height, 1.0f);
    desc.padding = json_float(node, "padding", 0.0f);
    desc.gap = json_float(node, "gap", 0.0f);
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

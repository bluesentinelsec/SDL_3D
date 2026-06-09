/**
 * @file game_data_editor_grid_widget.c
 * @brief Editor grid-size widget handling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

#define EDITOR_GRID_DROPDOWN_ID "ui.editor_shell.tool_toolbar.grid.button"

bool editor_mouse_in_rect(float mouse_x, float mouse_y, yyjson_val *rect)
{
    if (!yyjson_is_obj(rect))
        return false;
    const float x = json_float(rect, "x", 0.0f);
    const float y = json_float(rect, "y", 0.0f);
    const float w = json_float(rect, "w", 0.0f);
    const float h = json_float(rect, "h", 0.0f);
    return w > 0.0f && h > 0.0f && mouse_x >= x && mouse_y >= y && mouse_x < x + w && mouse_y < y + h;
}

static void editor_set_grid_value(slayer3d_game_data_runtime *runtime, yyjson_val *widget, float value)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const char *grid_key = json_string(widget, "grid_key", "editor.grid.size");
    const char *brush_grid_key = json_string(widget, "brush_grid_key", "editor.brush.grid_size");
    slayer3d_properties_set_float(runtime->scene_state, grid_key, value);
    if (brush_grid_key != NULL && brush_grid_key[0] != '\0')
        slayer3d_properties_set_float(runtime->scene_state, brush_grid_key, value);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "grid size selected");
}

static const slayer3d_ui_layout_hit_region *editor_retained_grid_hit(const slayer3d_game_data_runtime *runtime,
                                                                     slayer3d_ui_layout_model *layout, float mouse_x,
                                                                     float mouse_y)
{
    if (layout == NULL || !slayer3d_game_data_build_active_ui_widget_layout(runtime, 1280.0f, 720.0f, NULL, layout))
        return NULL;
    const slayer3d_ui_layout_hit_region *hit = slayer3d_ui_layout_hit_test(layout, mouse_x, mouse_y);
    if (hit == NULL)
        return NULL;
    if (SDL_strcmp(hit->id, EDITOR_GRID_DROPDOWN_ID) == 0 || SDL_strcmp(hit->owner_id, EDITOR_GRID_DROPDOWN_ID) == 0)
    {
        return hit;
    }
    return NULL;
}

bool editor_handle_grid_widget(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    yyjson_val *widget = obj_get(editor, "grid_widget");
    yyjson_val *values = obj_get(widget, "values");
    if (runtime == NULL || runtime->scene_state == NULL || !yyjson_is_obj(widget) || !yyjson_is_arr(values))
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (input == NULL || !slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        return true;

    const char *open_key = json_string(widget, "open_key", "editor.grid.menu.open");
    const bool opened = slayer3d_properties_get_bool(runtime->scene_state, open_key, false);
    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);

    slayer3d_ui_layout_model *layout = NULL;
    const slayer3d_ui_layout_hit_region *retained_hit = NULL;
    if (slayer3d_ui_layout_create(&layout))
        retained_hit = editor_retained_grid_hit(runtime, layout, mouse_x, mouse_y);

    const bool over_retained_button =
        retained_hit != NULL && SDL_strcmp(retained_hit->id, EDITOR_GRID_DROPDOWN_ID) == 0;
    const bool over_retained_option = retained_hit != NULL && retained_hit->option_index >= 0;
    const bool use_legacy_hit_rects = retained_hit == NULL;
    const bool over_button =
        over_retained_button ||
        (use_legacy_hit_rects && editor_mouse_in_rect(mouse_x, mouse_y, obj_get(widget, "button")));
    const bool over_popup = over_retained_option ||
                            (use_legacy_hit_rects && editor_mouse_in_rect(mouse_x, mouse_y, obj_get(widget, "popup")));
    if (!left_pressed)
    {
        if (out_consumed != NULL)
            *out_consumed = over_button || (opened && over_popup);
        slayer3d_ui_layout_destroy(layout);
        return true;
    }

    if (over_button)
    {
        slayer3d_properties_set_bool(runtime->scene_state, open_key, !opened);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }

    if (opened && over_popup)
    {
        int index = retained_hit != NULL && retained_hit->option_index >= 0 ? retained_hit->option_index : -1;
        if (index < 0)
        {
            yyjson_val *popup = obj_get(widget, "popup");
            const float y = json_float(popup, "y", 0.0f);
            const float row_height = SDL_max(json_float(widget, "row_height", 24.0f), 1.0f);
            index = (int)SDL_floorf((mouse_y - y) / row_height);
        }
        if (index >= 0 && index < (int)yyjson_arr_size(values))
        {
            yyjson_val *value = yyjson_arr_get(values, (size_t)index);
            if (yyjson_is_num(value))
                editor_set_grid_value(runtime, widget, (float)yyjson_get_num(value));
        }
        slayer3d_properties_set_bool(runtime->scene_state, open_key, false);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }

    if (opened)
        slayer3d_properties_set_bool(runtime->scene_state, open_key, false);
    slayer3d_ui_layout_destroy(layout);
    return true;
}

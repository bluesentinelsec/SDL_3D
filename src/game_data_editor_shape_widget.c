/**
 * @file game_data_editor_shape_widget.c
 * @brief Editor brush-shape dropdown UI handling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

#define EDITOR_SHAPE_DROPDOWN_DEFAULT_ID "ui.editor_shell.toolbar.shape.button"

static const slayer3d_ui_layout_hit_region *editor_retained_shape_hit(const slayer3d_game_data_runtime *runtime,
                                                                      slayer3d_ui_layout_model *layout,
                                                                      const char *dropdown_id, float mouse_x,
                                                                      float mouse_y, bool *out_layout_valid)
{
    if (out_layout_valid != NULL)
        *out_layout_valid = false;
    float viewport_w = 0.0f;
    float viewport_h = 0.0f;
    slayer3d_game_data_ui_viewport(runtime, &viewport_w, &viewport_h);
    if (layout == NULL || dropdown_id == NULL ||
        !slayer3d_game_data_build_active_ui_widget_layout(runtime, viewport_w, viewport_h, NULL, layout))
    {
        return NULL;
    }
    if (out_layout_valid != NULL)
        *out_layout_valid = true;
    const slayer3d_ui_layout_hit_region *hit = slayer3d_ui_layout_hit_test(layout, mouse_x, mouse_y);
    if (hit == NULL)
        return NULL;
    if (SDL_strcmp(hit->id, dropdown_id) == 0 || SDL_strcmp(hit->owner_id, dropdown_id) == 0)
        return hit;
    return NULL;
}

static bool editor_shape_option_value(yyjson_val *options, int index, const char **out_id, const char **out_label)
{
    if (out_id != NULL)
        *out_id = NULL;
    if (out_label != NULL)
        *out_label = NULL;
    if (!yyjson_is_arr(options) || index < 0 || index >= (int)yyjson_arr_size(options))
        return false;

    yyjson_val *option = yyjson_arr_get(options, (size_t)index);
    const char *id = NULL;
    const char *label = NULL;
    if (yyjson_is_obj(option))
    {
        id = json_string(option, "id", NULL);
        label = json_string(option, "label", NULL);
    }
    else if (yyjson_is_str(option))
    {
        label = yyjson_get_str(option);
        id = label;
    }
    if (id == NULL || id[0] == '\0' || label == NULL || label[0] == '\0')
        return false;
    if (out_id != NULL)
        *out_id = id;
    if (out_label != NULL)
        *out_label = label;
    return true;
}

static void editor_select_shape_option(slayer3d_game_data_runtime *runtime, yyjson_val *widget, int index)
{
    if (runtime == NULL || runtime->scene_state == NULL || !yyjson_is_obj(widget))
        return;

    const char *id = NULL;
    const char *label = NULL;
    if (!editor_shape_option_value(obj_get(widget, "options"), index, &id, &label))
        return;

    slayer3d_properties_set_int(runtime->scene_state, json_string(widget, "index_key", "editor.shape.index"), index);
    slayer3d_properties_set_string(runtime->scene_state, json_string(widget, "id_key", "editor.shape.id"), id);
    slayer3d_properties_set_string(runtime->scene_state, json_string(widget, "label_key", "editor.shape.label"), label);

    char message[96];
    SDL_snprintf(message, sizeof(message), "shape selected: %s", label);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
}

bool editor_handle_shape_widget(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;

    yyjson_val *widget = obj_get(editor, "shape_widget");
    yyjson_val *options = obj_get(widget, "options");
    if (runtime == NULL || runtime->scene_state == NULL || !yyjson_is_obj(widget) || !yyjson_is_arr(options))
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (input == NULL || !slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        return true;

    const char *dropdown_id = json_string(widget, "dropdown_id", EDITOR_SHAPE_DROPDOWN_DEFAULT_ID);
    const char *open_key = json_string(widget, "open_key", "editor.shape.menu.open");
    const bool opened = slayer3d_properties_get_bool(runtime->scene_state, open_key, false);
    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);

    slayer3d_ui_layout_model *layout = NULL;
    const slayer3d_ui_layout_hit_region *retained_hit = NULL;
    bool retained_layout_valid = false;
    if (slayer3d_ui_layout_create(&layout))
        retained_hit =
            editor_retained_shape_hit(runtime, layout, dropdown_id, mouse_x, mouse_y, &retained_layout_valid);

    const bool over_button = retained_hit != NULL && SDL_strcmp(retained_hit->id, dropdown_id) == 0;
    const bool over_option = retained_hit != NULL && retained_hit->option_index >= 0;

    if (!left_pressed)
    {
        if (out_consumed != NULL)
            *out_consumed = over_button || (opened && over_option);
        slayer3d_ui_layout_destroy(layout);
        return true;
    }

    if (over_button)
    {
        const bool next_open = !opened;
        slayer3d_properties_set_bool(runtime->scene_state, open_key, next_open);
        if (next_open)
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.file.menu.open", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.global.panel.open", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.grid.menu.open", false);
        }
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }

    if (opened && over_option)
    {
        editor_select_shape_option(runtime, widget, retained_hit->option_index);
        slayer3d_properties_set_bool(runtime->scene_state, open_key, false);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }

    if (opened && retained_layout_valid)
    {
        /* A click outside an open popup dismisses it and stops there; it
         * must never fall through to the world or widgets underneath. */
        slayer3d_properties_set_bool(runtime->scene_state, open_key, false);
        if (out_consumed != NULL)
            *out_consumed = true;
    }

    slayer3d_ui_layout_destroy(layout);
    return true;
}

/**
 * @file game_data_editor_toolbar.c
 * @brief Editor toolbar widget handling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

bool editor_handle_prefabs_widget(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    yyjson_val *widget = obj_get(editor, "prefabs_widget");
    if (runtime == NULL || runtime->scene_state == NULL || !yyjson_is_obj(widget))
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (input == NULL || !slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        return true;

    const bool over_button = editor_mouse_in_rect(mouse_x, mouse_y, obj_get(widget, "button"));
    if (out_consumed != NULL)
        *out_consumed = over_button;
    if (!over_button || !slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT))
        return true;

    const char *active_key = json_string(widget, "active_key", "editor.palette.active");
    const char *cursor_key = json_string(widget, "cursor_key", "editor.palette.brush.cursor");
    const char *active_value = json_string(widget, "active_value", "brush");
    const char *default_cursor = json_string(widget, "default_cursor", "floor");
    slayer3d_properties_set_string(runtime->scene_state, active_key, active_value);
    slayer3d_properties_set_string(runtime->scene_state, cursor_key, default_cursor);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "prefabs palette opened");
    return true;
}

bool editor_handle_tool_mode_buttons(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    yyjson_val *buttons = obj_get(editor, "tool_mode_buttons");
    if (runtime == NULL || runtime->scene_state == NULL || !yyjson_is_arr(buttons))
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (input == NULL || !slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        return true;

    const bool clicked = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    for (size_t i = 0, count = yyjson_arr_size(buttons); i < count; ++i)
    {
        yyjson_val *button = yyjson_arr_get(buttons, i);
        if (!yyjson_is_obj(button) || !editor_mouse_in_rect(mouse_x, mouse_y, obj_get(button, "button")))
            continue;
        if (out_consumed != NULL)
            *out_consumed = true;
        if (!clicked)
            return true;

        const char *mode = json_string(button, "mode", NULL);
        const char *tool_mode = json_string(button, "tool_mode", mode);
        if (mode == NULL || mode[0] == '\0' || tool_mode == NULL || tool_mode[0] == '\0')
            return true;
        slayer3d_properties_set_string(runtime->scene_state, "editor.mode", mode);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.mode", tool_mode);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                       json_string(button, "message", "tool mode"));
        clear_editor_command_preview(runtime);
        return true;
    }
    return true;
}

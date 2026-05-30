/**
 * @file game_data_editor_toolbar.c
 * @brief Editor toolbar widget handling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

static bool editor_retained_ui_hit(const slayer3d_game_data_runtime *runtime, float mouse_x, float mouse_y,
                                   slayer3d_ui_layout_model **out_layout, const slayer3d_ui_layout_hit_region **out_hit)
{
    if (out_layout != NULL)
        *out_layout = NULL;
    if (out_hit != NULL)
        *out_hit = NULL;

    slayer3d_ui_layout_model *layout = NULL;
    if (!slayer3d_ui_layout_create(&layout))
        return false;
    if (!slayer3d_game_data_build_active_ui_widget_layout(runtime, 1280.0f, 720.0f, NULL, layout))
    {
        slayer3d_ui_layout_destroy(layout);
        return false;
    }

    const slayer3d_ui_layout_hit_region *hit = slayer3d_ui_layout_hit_test(layout, mouse_x, mouse_y);
    if (out_hit != NULL)
        *out_hit = hit;
    if (out_layout != NULL)
        *out_layout = layout;
    else
        slayer3d_ui_layout_destroy(layout);
    return hit != NULL;
}

static bool editor_hit_id_has_prefix(const slayer3d_ui_layout_hit_region *hit, const char *prefix)
{
    if (hit == NULL || prefix == NULL)
        return false;
    return SDL_strncmp(hit->id, prefix, SDL_strlen(prefix)) == 0;
}

static bool editor_hit_is_toolbar(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.toolbar.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.tool_toolbar.");
}

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

    slayer3d_ui_layout_model *layout = NULL;
    const slayer3d_ui_layout_hit_region *hit = NULL;
    (void)editor_retained_ui_hit(runtime, mouse_x, mouse_y, &layout, &hit);
    const bool over_button = (hit != NULL && SDL_strcmp(hit->action, "editor.palette.prefabs") == 0) ||
                             editor_mouse_in_rect(mouse_x, mouse_y, obj_get(widget, "button"));
    if (out_consumed != NULL)
        *out_consumed = over_button;
    if (!over_button || !slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT))
    {
        slayer3d_ui_layout_destroy(layout);
        return true;
    }

    const char *active_key = json_string(widget, "active_key", "editor.palette.active");
    const char *cursor_key = json_string(widget, "cursor_key", "editor.palette.brush.cursor");
    const char *active_value = json_string(widget, "active_value", "brush");
    const char *default_cursor = json_string(widget, "default_cursor", "floor");
    slayer3d_properties_set_string(runtime->scene_state, active_key, active_value);
    slayer3d_properties_set_string(runtime->scene_state, cursor_key, default_cursor);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "prefabs palette opened");
    slayer3d_ui_layout_destroy(layout);
    return true;
}

static const char *editor_mode_for_tool_action(const char *action)
{
    if (SDL_strcmp(action, "editor.tool.select") == 0)
        return "select";
    if (SDL_strcmp(action, "editor.tool.brush") == 0)
        return "brush";
    if (SDL_strcmp(action, "editor.tool.face") == 0)
        return "face";
    if (SDL_strcmp(action, "editor.tool.vertex") == 0)
        return "vertex";
    if (SDL_strcmp(action, "editor.tool.texture") == 0)
        return "texture";
    return NULL;
}

bool slayer3d_game_data_set_editor_tool_mode(slayer3d_game_data_runtime *runtime, const char *mode,
                                             const char *message_override)
{
    if (runtime == NULL || runtime->scene_state == NULL || mode == NULL || mode[0] == '\0')
        return false;

    const char *tool_mode = mode;
    const char *message = message_override;
    if (SDL_strcmp(mode, "select") == 0)
    {
        tool_mode = "select";
        if (message == NULL || message[0] == '\0')
            message = "select mode";
    }
    else if (SDL_strcmp(mode, "brush") == 0)
    {
        tool_mode = "brush";
        if (message == NULL || message[0] == '\0')
            message = "brush tool";
    }
    else if (SDL_strcmp(mode, "face") == 0)
    {
        tool_mode = "face";
        if (message == NULL || message[0] == '\0')
            message = "face tool";
    }
    else if (SDL_strcmp(mode, "vertex") == 0)
    {
        tool_mode = "vertex";
        if (message == NULL || message[0] == '\0')
        {
            const int selected_count = slayer3d_properties_get_int(runtime->scene_state, "editor.selection.count", 0);
            message = selected_count > 0 ? "vertex tool" : "select a brush before vertex tool";
        }
    }
    else if (SDL_strcmp(mode, "texture") == 0)
    {
        tool_mode = "paint";
        if (message == NULL || message[0] == '\0')
            message = "texture tool";
    }
    else
    {
        return false;
    }

    if (SDL_strcmp(mode, "vertex") != 0)
        (void)slayer3d_game_data_clear_editor_vertex_selection(runtime);
    slayer3d_properties_set_string(runtime->scene_state, "editor.mode", mode);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.mode", tool_mode);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    clear_editor_command_preview(runtime);
    return true;
}

static bool editor_apply_tool_action(slayer3d_game_data_runtime *runtime, const char *action)
{
    const char *mode = editor_mode_for_tool_action(action);
    return mode != NULL && slayer3d_game_data_set_editor_tool_mode(runtime, mode, NULL);
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
    slayer3d_ui_layout_model *layout = NULL;
    const slayer3d_ui_layout_hit_region *hit = NULL;
    (void)editor_retained_ui_hit(runtime, mouse_x, mouse_y, &layout, &hit);
    if (editor_hit_is_toolbar(hit))
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        if (clicked && hit->action[0] != '\0')
            (void)editor_apply_tool_action(runtime, hit->action);
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    slayer3d_ui_layout_destroy(layout);

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
        if (mode == NULL || mode[0] == '\0')
            return true;
        (void)slayer3d_game_data_set_editor_tool_mode(runtime, mode, json_string(button, "message", NULL));
        return true;
    }
    return true;
}

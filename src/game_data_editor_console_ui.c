/**
 * @file game_data_editor_console_ui.c
 * @brief Editor console focus, selection, copy, and scroll handling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#define EDITOR_CONSOLE_HISTORY_COUNT 64
#define EDITOR_CONSOLE_VISIBLE_COUNT 5
#define EDITOR_CONSOLE_COPY_BUFFER_SIZE 32768

bool editor_set_console_scroll(slayer3d_game_data_runtime *runtime, int scroll)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const int count = SDL_clamp(slayer3d_properties_get_int(runtime->scene_state, "editor.console.count", 0), 0,
                                EDITOR_CONSOLE_HISTORY_COUNT);
    const int max_scroll = SDL_max(0, count - EDITOR_CONSOLE_VISIBLE_COUNT);
    scroll = SDL_clamp(scroll, 0, max_scroll);
    slayer3d_properties_set_int(runtime->scene_state, "editor.console.scroll", scroll);
    editor_refresh_console_lines(runtime);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "console scrolled");
    return true;
}

bool editor_scroll_console_by(slayer3d_game_data_runtime *runtime, int delta)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const int scroll = slayer3d_properties_get_int(runtime->scene_state, "editor.console.scroll", 0);
    return editor_set_console_scroll(runtime, scroll + delta);
}

void editor_set_console_focus(slayer3d_game_data_runtime *runtime, bool focused)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.console.focused", focused);
}

void editor_clear_console_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.console.selection.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.console.selection.has", false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.console.selection.clipboard", "");
    editor_refresh_console_lines(runtime);
}

static int editor_console_visible_line_at(const slayer3d_ui_layout_model *layout, float mouse_x, float mouse_y)
{
    if (layout == NULL)
        return -1;

    for (int i = 0; i < EDITOR_CONSOLE_VISIBLE_COUNT; ++i)
    {
        char row_id[64];
        SDL_snprintf(row_id, sizeof(row_id), "ui.editor_shell.console.line%d.row", i);
        const slayer3d_ui_layout_render_command *row = editor_find_layout_render_by_id(layout, row_id);
        if (row != NULL && mouse_x >= row->rect.x && mouse_x < row->rect.x + row->rect.w && mouse_y >= row->rect.y &&
            mouse_y < row->rect.y + row->rect.h)
            return i;
    }
    return -1;
}

static int editor_console_history_index_at(const slayer3d_game_data_runtime *runtime,
                                           const slayer3d_ui_layout_model *layout, float mouse_x, float mouse_y)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return -1;
    const int line = editor_console_visible_line_at(layout, mouse_x, mouse_y);
    if (line < 0)
        return -1;
    const int count = SDL_clamp(slayer3d_properties_get_int(runtime->scene_state, "editor.console.count", 0), 0,
                                EDITOR_CONSOLE_HISTORY_COUNT);
    const int scroll = SDL_clamp(slayer3d_properties_get_int(runtime->scene_state, "editor.console.scroll", 0), 0,
                                 SDL_max(0, count - EDITOR_CONSOLE_VISIBLE_COUNT));
    const int history_index = scroll + line;
    return history_index >= 0 && history_index < count ? history_index : -1;
}

bool editor_console_set_selection_cursor(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_model *layout,
                                         float mouse_x, float mouse_y)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const int history_index = editor_console_history_index_at(runtime, layout, mouse_x, mouse_y);
    if (history_index < 0)
        return false;
    slayer3d_properties_set_int(runtime->scene_state, "editor.console.selection.cursor", history_index);
    editor_refresh_console_lines(runtime);
    return true;
}

bool editor_console_begin_selection(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_model *layout,
                                    float mouse_x, float mouse_y)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const int history_index = editor_console_history_index_at(runtime, layout, mouse_x, mouse_y);
    if (history_index < 0)
    {
        editor_clear_console_selection(runtime);
        return false;
    }
    slayer3d_properties_set_bool(runtime->scene_state, "editor.console.selection.active", true);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.console.selection.has", true);
    slayer3d_properties_set_int(runtime->scene_state, "editor.console.selection.anchor", history_index);
    slayer3d_properties_set_int(runtime->scene_state, "editor.console.selection.cursor", history_index);
    editor_refresh_console_lines(runtime);
    return true;
}

bool editor_console_copy_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL ||
        !slayer3d_properties_get_bool(runtime->scene_state, "editor.console.selection.has", false))
    {
        return false;
    }

    const int count = SDL_clamp(slayer3d_properties_get_int(runtime->scene_state, "editor.console.count", 0), 0,
                                EDITOR_CONSOLE_HISTORY_COUNT);
    const int anchor = slayer3d_properties_get_int(runtime->scene_state, "editor.console.selection.anchor", -1);
    const int cursor = slayer3d_properties_get_int(runtime->scene_state, "editor.console.selection.cursor", anchor);
    int min_index = SDL_clamp(SDL_min(anchor, cursor), 0, SDL_max(0, count - 1));
    int max_index = SDL_clamp(SDL_max(anchor, cursor), 0, SDL_max(0, count - 1));
    if (count <= 0 || anchor < 0 || cursor < 0 || min_index > max_index)
        return false;

    char selected_text[EDITOR_CONSOLE_COPY_BUFFER_SIZE];
    selected_text[0] = '\0';
    for (int i = min_index; i <= max_index; ++i)
    {
        char history_key[64];
        SDL_snprintf(history_key, sizeof(history_key), "editor.console.history%d", i);
        const char *line = slayer3d_properties_get_string(runtime->scene_state, history_key, "");
        if (line == NULL || line[0] == '\0')
            continue;
        if (selected_text[0] != '\0')
            SDL_strlcat(selected_text, "\n", sizeof(selected_text));
        SDL_strlcat(selected_text, line, sizeof(selected_text));
    }

    if (selected_text[0] == '\0')
        return false;
    (void)SDL_SetClipboardText(selected_text);
    slayer3d_properties_set_string(runtime->scene_state, "editor.console.selection.clipboard", selected_text);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "console selection copied");
    return true;
}

bool editor_console_copy_selection_if_requested(slayer3d_game_data_runtime *runtime,
                                                const slayer3d_input_manager *input)
{
    if (runtime == NULL || runtime->scene_state == NULL || input == NULL ||
        !slayer3d_properties_get_bool(runtime->scene_state, "editor.console.focused", false) ||
        !slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_C))
    {
        return false;
    }
    const SDL_Keymod mod = SDL_GetModState();
    if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) == 0)
        return false;
    return editor_console_copy_selection(runtime);
}

/**
 * @file game_presentation_menus.c
 * @brief Menu input/update helpers for JSON-authored game presentation.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_stdinc.h>

static bool menu_action_pressed(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                int action_id, const char *fallback_name)
{
    if (action_id >= 0 && slayer3d_game_data_active_scene_allows_action(runtime, action_id) &&
        slayer3d_input_is_pressed(input, action_id))
    {
        return true;
    }

    if (fallback_name != NULL && input != NULL)
    {
        const int fallback_id = slayer3d_input_find_action(input, fallback_name);
        if (fallback_id >= 0 && slayer3d_input_is_pressed(input, fallback_id))
            return true;
    }

    return false;
}

static bool menu_action_held(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                             int action_id, const char *fallback_name)
{
    if (action_id >= 0 && slayer3d_game_data_active_scene_allows_action(runtime, action_id) &&
        slayer3d_input_is_held(input, action_id))
    {
        return true;
    }

    if (fallback_name != NULL && input != NULL)
    {
        const int fallback_id = slayer3d_input_find_action(input, fallback_name);
        if (fallback_id >= 0 && slayer3d_input_is_held(input, fallback_id))
            return true;
    }

    return false;
}

static bool menu_input_is_idle(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                               const slayer3d_game_data_menu *menu)
{
    if (menu == NULL)
        return true;
    if (input == NULL)
        return false;

    return !menu_action_held(runtime, input, menu->up_action_id, "ui_up") &&
           !menu_action_held(runtime, input, menu->down_action_id, "ui_down") &&
           !menu_action_held(runtime, input, menu->left_action_id, "ui_left") &&
           !menu_action_held(runtime, input, menu->right_action_id, "ui_right") &&
           !menu_action_held(runtime, input, menu->select_action_id, "ui_accept") &&
           !menu_action_held(runtime, input, menu->back_action_id, "ui_back");
}

static void menu_result_set_scene_state(slayer3d_game_data_menu_update_result *result,
                                        const slayer3d_game_data_menu_item *item)
{
    if (result == NULL || item == NULL)
        return;
    result->scene_state_key = item->scene_state_key;
    result->scene_state_value = item->scene_state_value;
    if (item->dynamic_list_item && item->scene_state_value != NULL)
    {
        SDL_strlcpy(result->scene_state_value_storage, item->scene_state_value,
                    sizeof(result->scene_state_value_storage));
        result->scene_state_value = result->scene_state_value_storage;
    }
}

bool slayer3d_game_data_update_menus_for_metrics(slayer3d_game_data_runtime *runtime,
                                                 const slayer3d_input_manager *input, bool *input_armed,
                                                 const slayer3d_game_data_ui_metrics *metrics,
                                                 slayer3d_game_data_menu_update_result *out_result)
{
    if (out_result != NULL)
    {
        SDL_zero(*out_result);
        out_result->selected_index = -1;
        out_result->signal_id = -1;
        out_result->move_signal_id = -1;
        out_result->select_signal_id = -1;
    }
    if (runtime == NULL || input_armed == NULL)
        return false;

    slayer3d_game_data_menu menu;
    if (!slayer3d_game_data_get_active_menu_for_metrics(runtime, metrics, &menu))
        return true;

    if (out_result != NULL)
    {
        out_result->menu = menu.name;
        out_result->selected_index = menu.selected_index;
    }

    if (slayer3d_game_data_menu_input_binding_capture_active(runtime))
    {
        const slayer3d_game_data_input_binding_capture_status capture_status =
            slayer3d_game_data_update_menu_input_binding_capture(runtime, input);
        if (out_result != NULL)
        {
            out_result->handled_input = true;
            out_result->input_binding_changed = capture_status == SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CHANGED;
            out_result->input_binding_conflict = capture_status == SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CONFLICT;
            out_result->selected = out_result->input_binding_changed;
            out_result->select_signal_id = out_result->input_binding_changed ? menu.select_signal_id : -1;
        }
        return true;
    }
    if (slayer3d_game_data_menu_text_entry_capture_active(runtime))
    {
        const slayer3d_game_data_text_entry_capture_status capture_status =
            slayer3d_game_data_update_menu_text_entry_capture(runtime, input);
        if (out_result != NULL)
        {
            out_result->handled_input = true;
            out_result->text_entry_changed = capture_status == SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CHANGED;
            out_result->text_entry_submitted = capture_status == SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_SUBMITTED;
            out_result->text_entry_canceled = capture_status == SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CANCELED;
            out_result->control_changed = out_result->text_entry_changed;
            out_result->selected = out_result->text_entry_submitted;
            out_result->select_signal_id = out_result->text_entry_submitted ? menu.select_signal_id : -1;
        }
        return true;
    }

    if (!*input_armed)
    {
        if (menu_input_is_idle(runtime, input, &menu))
            *input_armed = true;
        return true;
    }

    bool handled = false;
    if (menu_action_pressed(runtime, input, menu.up_action_id, "ui_up"))
    {
        const bool moved = slayer3d_game_data_menu_move(runtime, menu.name, -1);
        handled = moved || handled;
        if (moved && out_result != NULL)
            out_result->move_signal_id = menu.move_signal_id;
    }
    if (menu_action_pressed(runtime, input, menu.down_action_id, "ui_down"))
    {
        const bool moved = slayer3d_game_data_menu_move(runtime, menu.name, 1);
        handled = moved || handled;
        if (moved && out_result != NULL)
            out_result->move_signal_id = menu.move_signal_id;
    }
    int control_direction = 0;
    bool control_adjust_input = false;
    if (menu_action_pressed(runtime, input, menu.left_action_id, "ui_left"))
    {
        handled = true;
        control_direction = -1;
        control_adjust_input = true;
    }
    if (menu_action_pressed(runtime, input, menu.right_action_id, "ui_right"))
    {
        handled = true;
        control_direction = 1;
        control_adjust_input = true;
    }
    if (menu_action_pressed(runtime, input, menu.select_action_id, "ui_accept"))
    {
        handled = true;
        control_direction = 1;
        control_adjust_input = false;
    }
    if (menu_action_pressed(runtime, input, menu.back_action_id, "ui_back"))
    {
        handled = true;
        slayer3d_game_data_menu refreshed;
        if (!slayer3d_game_data_get_active_menu_for_metrics(runtime, metrics, &refreshed))
            return true;

        slayer3d_game_data_menu_item item;
        bool found_back_item = false;
        for (int i = 0; i < refreshed.item_count; ++i)
        {
            if (!slayer3d_game_data_get_menu_item(runtime, refreshed.name, i, &item))
                continue;
            if (!item.return_scene && !(item.label != NULL && SDL_strcasecmp(item.label, "Back") == 0))
                continue;
            found_back_item = true;
            if (out_result != NULL)
            {
                out_result->menu = refreshed.name;
                out_result->selected_index = i;
                out_result->control_changed = false;
                out_result->selected = true;
                out_result->select_signal_id = refreshed.select_signal_id;
                out_result->quit = item.quit;
                out_result->scene = item.scene;
                out_result->return_to = item.return_to;
                menu_result_set_scene_state(out_result, &item);
                out_result->return_scene = item.return_scene;
                out_result->signal_id = item.signal_id;
                out_result->pause_command = item.pause_command;
                out_result->has_return_paused = item.has_return_paused;
                out_result->return_paused = item.return_paused;
                out_result->handled_input = true;
            }
            break;
        }
        if (!found_back_item && out_result != NULL)
            out_result->handled_input = true;
        return true;
    }

    if (control_direction != 0)
    {
        slayer3d_game_data_menu refreshed;
        if (!slayer3d_game_data_get_active_menu_for_metrics(runtime, metrics, &refreshed))
            return true;

        slayer3d_game_data_menu_item item;
        /* Selecting the initially highlighted dynamic-list row should publish its selected outputs. */
        (void)slayer3d_game_data_publish_menu_selection(runtime, refreshed.name);
        if (!slayer3d_game_data_get_menu_item(runtime, refreshed.name, refreshed.selected_index, &item))
            return true;

        if (out_result != NULL)
        {
            out_result->menu = refreshed.name;
            out_result->selected_index = refreshed.selected_index;
            if (!control_adjust_input && item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            {
                out_result->input_binding_capture_started = slayer3d_game_data_start_menu_input_binding_capture(
                    runtime, refreshed.name, refreshed.selected_index);
                out_result->selected = out_result->input_binding_capture_started;
                out_result->select_signal_id = out_result->selected ? refreshed.select_signal_id : -1;
                out_result->signal_id = -1;
                out_result->quit = false;
                out_result->scene = NULL;
                out_result->return_to = NULL;
                out_result->scene_state_key = NULL;
                out_result->scene_state_value = NULL;
                out_result->return_scene = false;
                out_result->pause_command = SLAYER3D_GAME_DATA_MENU_PAUSE_NONE;
                out_result->has_return_paused = false;
                out_result->return_paused = false;
                out_result->control_changed = false;
                out_result->handled_input = true;
                return true;
            }
            if (!control_adjust_input && item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
            {
                out_result->text_entry_capture_started =
                    slayer3d_game_data_start_menu_text_entry_capture(runtime, refreshed.name, refreshed.selected_index);
                out_result->selected = out_result->text_entry_capture_started;
                out_result->select_signal_id = out_result->selected ? refreshed.select_signal_id : -1;
                out_result->signal_id = -1;
                out_result->quit = false;
                out_result->scene = NULL;
                out_result->return_to = NULL;
                out_result->scene_state_key = NULL;
                out_result->scene_state_value = NULL;
                out_result->return_scene = false;
                out_result->pause_command = SLAYER3D_GAME_DATA_MENU_PAUSE_NONE;
                out_result->has_return_paused = false;
                out_result->return_paused = false;
                out_result->control_changed = false;
                out_result->handled_input = true;
                return true;
            }
            out_result->control_changed =
                control_adjust_input && (item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING ||
                                         item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
                    ? false
                    : slayer3d_game_data_adjust_menu_item_control(runtime, &item, control_direction);
            out_result->selected = !control_adjust_input || out_result->control_changed;
            out_result->select_signal_id = out_result->selected ? refreshed.select_signal_id : -1;
            out_result->quit = !control_adjust_input && !out_result->control_changed && item.quit;
            out_result->scene = !control_adjust_input && !out_result->control_changed ? item.scene : NULL;
            out_result->return_to = !control_adjust_input && !out_result->control_changed ? item.return_to : NULL;
            if (!control_adjust_input && !out_result->control_changed)
                menu_result_set_scene_state(out_result, &item);
            else
            {
                out_result->scene_state_key = NULL;
                out_result->scene_state_value = NULL;
            }
            out_result->return_scene = !control_adjust_input && !out_result->control_changed && item.return_scene;
            out_result->signal_id = out_result->selected ? item.signal_id : -1;
            out_result->pause_command = !control_adjust_input && !out_result->control_changed
                                            ? item.pause_command
                                            : SLAYER3D_GAME_DATA_MENU_PAUSE_NONE;
            out_result->has_return_paused =
                !control_adjust_input && !out_result->control_changed && item.has_return_paused;
            out_result->return_paused = item.return_paused;
        }
        else
        {
            if (!control_adjust_input && item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
                (void)slayer3d_game_data_start_menu_input_binding_capture(runtime, refreshed.name,
                                                                          refreshed.selected_index);
            else if (!control_adjust_input && item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
                (void)slayer3d_game_data_start_menu_text_entry_capture(runtime, refreshed.name,
                                                                       refreshed.selected_index);
            else if (item.control_type != SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING &&
                     item.control_type != SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
                (void)slayer3d_game_data_adjust_menu_item_control(runtime, &item, control_direction);
        }
    }
    else if (handled && out_result != NULL)
    {
        slayer3d_game_data_menu refreshed;
        if (slayer3d_game_data_get_active_menu_for_metrics(runtime, metrics, &refreshed))
            out_result->selected_index = refreshed.selected_index;
    }

    if (out_result != NULL)
        out_result->handled_input = handled;
    return true;
}

bool slayer3d_game_data_update_menus(slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                     bool *input_armed, slayer3d_game_data_menu_update_result *out_result)
{
    return slayer3d_game_data_update_menus_for_metrics(runtime, input, input_armed, NULL, out_result);
}

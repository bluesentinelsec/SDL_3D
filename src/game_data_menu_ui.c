/**
 * @file game_data_menu_ui.c
 * @brief Menu and authored UI runtime helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_log.h>

#include "slayer3d/input.h"

static slayer3d_game_data_menu_control_type parse_menu_control_type(const char *type)
{
    if (type == NULL)
        return SLAYER3D_GAME_DATA_MENU_CONTROL_NONE;
    if (SDL_strcmp(type, "toggle") == 0)
        return SLAYER3D_GAME_DATA_MENU_CONTROL_TOGGLE;
    if (SDL_strcmp(type, "choice") == 0)
        return SLAYER3D_GAME_DATA_MENU_CONTROL_CHOICE;
    if (SDL_strcmp(type, "range") == 0)
        return SLAYER3D_GAME_DATA_MENU_CONTROL_RANGE;
    if (SDL_strcmp(type, "input_binding") == 0)
        return SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING;
    if (SDL_strcmp(type, "text") == 0)
        return SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT;
    return SLAYER3D_GAME_DATA_MENU_CONTROL_NONE;
}

static bool menu_item_is_dynamic_list(yyjson_val *item)
{
    return yyjson_is_obj(item) && SDL_strcmp(json_string(item, "type", ""), "dynamic_list") == 0;
}

static yyjson_val *menu_dynamic_list_source(yyjson_val *item)
{
    return menu_item_is_dynamic_list(item) ? obj_get(item, "source") : NULL;
}

static int menu_dynamic_list_count(const slayer3d_game_data_runtime *runtime, yyjson_val *item)
{
    yyjson_val *source = menu_dynamic_list_source(item);
    if (runtime == NULL || !yyjson_is_obj(source))
        return 0;

    const char *type = json_string(source, "type", "");
    if (SDL_strcmp(type, "scene_state_indexed") == 0)
    {
        const char *count_key = json_string(source, "count_key", NULL);
        if (count_key == NULL)
            return 0;
        return SDL_max(0, slayer3d_properties_get_int(runtime->scene_state, count_key, 0));
    }
    if (SDL_strcmp(type, "runtime_collection") == 0)
        return slayer3d_game_data_runtime_collection_count(runtime, json_string(source, "collection", NULL));
    return 0;
}

static int menu_dynamic_list_row_count(const slayer3d_game_data_runtime *runtime, yyjson_val *item)
{
    const int count = menu_dynamic_list_count(runtime, item);
    if (count > 0)
        return count;
    return json_string(item, "empty_label", NULL) != NULL ? 1 : 0;
}

static int menu_authored_item_row_count(const slayer3d_game_data_runtime *runtime, yyjson_val *item)
{
    return menu_item_is_dynamic_list(item) ? menu_dynamic_list_row_count(runtime, item) : 1;
}

int menu_runtime_item_count(const slayer3d_game_data_runtime *runtime, const scene_menu_state *menu)
{
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    int count = 0;
    for (size_t i = 0; yyjson_is_arr(items) && i < yyjson_arr_size(items); ++i)
        count += menu_authored_item_row_count(runtime, yyjson_arr_get(items, i));
    return count;
}

static bool resolve_menu_item_at(const slayer3d_game_data_runtime *runtime, const scene_menu_state *menu,
                                 int item_index, yyjson_val **out_item, int *out_dynamic_index, bool *out_dynamic_empty)
{
    if (out_item != NULL)
        *out_item = NULL;
    if (out_dynamic_index != NULL)
        *out_dynamic_index = -1;
    if (out_dynamic_empty != NULL)
        *out_dynamic_empty = false;
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    if (!yyjson_is_arr(items) || item_index < 0)
        return false;

    int cursor = 0;
    for (size_t i = 0; i < yyjson_arr_size(items); ++i)
    {
        yyjson_val *item = yyjson_arr_get(items, i);
        const int rows = menu_authored_item_row_count(runtime, item);
        if (item_index < cursor + rows)
        {
            if (out_item != NULL)
                *out_item = item;
            if (menu_item_is_dynamic_list(item))
            {
                const int source_count = menu_dynamic_list_count(runtime, item);
                if (source_count > 0)
                {
                    if (out_dynamic_index != NULL)
                        *out_dynamic_index = item_index - cursor;
                }
                else
                {
                    if (out_dynamic_empty != NULL)
                        *out_dynamic_empty = true;
                }
            }
            return true;
        }
        cursor += rows;
    }
    return false;
}

static bool menu_dynamic_list_format_key(yyjson_val *source, const char *field, int index, char *buffer,
                                         size_t buffer_size)
{
    const char *format = json_string(source, field, NULL);
    if (format == NULL || buffer == NULL || buffer_size == 0U)
        return false;

    const char *placeholder = SDL_strstr(format, "%d");
    if (placeholder == NULL)
        return false;
    if (SDL_strstr(placeholder + 2, "%") != NULL)
        return false;
    for (const char *scan = format; scan < placeholder; ++scan)
    {
        if (*scan == '%')
            return false;
    }

    char index_text[32];
    SDL_snprintf(index_text, sizeof(index_text), "%d", index);
    const size_t prefix_len = (size_t)(placeholder - format);
    const size_t suffix_len = SDL_strlen(placeholder + 2);
    const size_t required = prefix_len + SDL_strlen(index_text) + suffix_len + 1U;
    if (required > buffer_size)
        return false;
    SDL_memcpy(buffer, format, prefix_len);
    SDL_strlcpy(buffer + prefix_len, index_text, buffer_size - prefix_len);
    SDL_strlcpy(buffer + prefix_len + SDL_strlen(index_text), placeholder + 2,
                buffer_size - prefix_len - SDL_strlen(index_text));
    return true;
}

static const char *menu_dynamic_list_entry_string(const slayer3d_game_data_runtime *runtime, yyjson_val *item,
                                                  int dynamic_index, const char *format_field, char *buffer,
                                                  size_t buffer_size)
{
    yyjson_val *source = menu_dynamic_list_source(item);
    if (runtime == NULL || !yyjson_is_obj(source) || dynamic_index < 0 || buffer == NULL || buffer_size == 0U)
        return NULL;

    const char *type = json_string(source, "type", "");
    if (SDL_strcmp(type, "scene_state_indexed") == 0)
    {
        char key[SLAYER3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
        if (!menu_dynamic_list_format_key(source, format_field, dynamic_index, key, sizeof(key)))
            return NULL;
        return slayer3d_properties_get_string(runtime->scene_state, key, NULL);
    }
    if (SDL_strcmp(type, "runtime_collection") == 0)
    {
        const char *field = NULL;
        if (SDL_strcmp(format_field, "label_key_format") == 0)
            field = json_string(source, "label_field", NULL);
        else if (SDL_strcmp(format_field, "value_key_format") == 0)
            field = json_string(source, "value_field", NULL);
        if (field == NULL)
            return NULL;

        const runtime_collection *collection =
            find_runtime_collection_const(runtime, json_string(source, "collection", NULL));
        return runtime_collection_field_to_string(collection, dynamic_index, field, buffer, buffer_size) ? buffer
                                                                                                         : NULL;
    }
    return NULL;
}

static void menu_dynamic_list_apply_label_template(const char *format, const char *label, char *buffer,
                                                   size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    if (format == NULL)
    {
        SDL_strlcpy(buffer, label != NULL ? label : "", buffer_size);
        return;
    }

    const char *cursor = format;
    size_t offset = 0U;
    buffer[0] = '\0';
    while (*cursor != '\0' && offset + 1U < buffer_size)
    {
        const char *marker = SDL_strstr(cursor, "{label}");
        if (marker == NULL)
        {
            SDL_strlcpy(buffer + offset, cursor, buffer_size - offset);
            return;
        }
        const size_t prefix_len = (size_t)(marker - cursor);
        const size_t writable_prefix = SDL_min(prefix_len, buffer_size - offset - 1U);
        SDL_memcpy(buffer + offset, cursor, writable_prefix);
        offset += writable_prefix;
        buffer[offset] = '\0';
        const char *value = label != NULL ? label : "";
        const size_t value_len = SDL_strlen(value);
        const size_t writable_value = SDL_min(value_len, buffer_size - offset - 1U);
        SDL_memcpy(buffer + offset, value, writable_value);
        offset += writable_value;
        buffer[offset] = '\0';
        cursor = marker + SDL_strlen("{label}");
    }
}

static const char *menu_dynamic_list_label(const slayer3d_game_data_runtime *runtime, yyjson_val *item,
                                           int dynamic_index, bool dynamic_empty, char *buffer, size_t buffer_size)
{
    if (runtime == NULL || item == NULL || buffer == NULL || buffer_size == 0U)
        return NULL;
    if (dynamic_empty)
    {
        SDL_strlcpy(buffer, json_string(item, "empty_label", "No entries"), buffer_size);
        return buffer;
    }

    char raw_label[SLAYER3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
    const char *label =
        menu_dynamic_list_entry_string(runtime, item, dynamic_index, "label_key_format", raw_label, sizeof(raw_label));
    if (label == NULL)
        label = menu_dynamic_list_entry_string(runtime, item, dynamic_index, "value_key_format", raw_label,
                                               sizeof(raw_label));
    if (label == NULL)
        label = "";

    const char *label_format = json_string(item, "label_format", NULL);
    if (label_format != NULL)
    {
        menu_dynamic_list_apply_label_template(label_format, label, buffer, buffer_size);
        return buffer;
    }
    SDL_strlcpy(buffer, label, buffer_size);
    return buffer;
}

static const char *menu_dynamic_list_value(const slayer3d_game_data_runtime *runtime, yyjson_val *item,
                                           int dynamic_index, char *buffer, size_t buffer_size)
{
    if (runtime == NULL || item == NULL || dynamic_index < 0 || buffer == NULL || buffer_size == 0U)
        return NULL;

    char raw_value[SLAYER3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
    const char *value =
        menu_dynamic_list_entry_string(runtime, item, dynamic_index, "value_key_format", raw_value, sizeof(raw_value));
    if (value == NULL)
        value = menu_dynamic_list_entry_string(runtime, item, dynamic_index, "label_key_format", raw_value,
                                               sizeof(raw_value));
    if (value == NULL)
        return NULL;

    SDL_strlcpy(buffer, value, buffer_size);
    return buffer;
}

static const char *menu_dynamic_scene_state_value(const slayer3d_game_data_runtime *runtime, yyjson_val *item,
                                                  int dynamic_index, const char *value_from, char *buffer,
                                                  size_t buffer_size)
{
    if (runtime == NULL || item == NULL || dynamic_index < 0 || buffer == NULL || buffer_size == 0U)
        return NULL;
    if (value_from == NULL || SDL_strcmp(value_from, "value") == 0)
        return menu_dynamic_list_value(runtime, item, dynamic_index, buffer, buffer_size);
    if (SDL_strcmp(value_from, "label") == 0)
        return menu_dynamic_list_label(runtime, item, dynamic_index, false, buffer, buffer_size);
    if (SDL_strcmp(value_from, "index") == 0)
    {
        SDL_snprintf(buffer, buffer_size, "%d", dynamic_index);
        return buffer;
    }
    return NULL;
}

void update_dynamic_list_selection_state(slayer3d_game_data_runtime *runtime, scene_menu_state *menu)
{
    yyjson_val *item = NULL;
    int dynamic_index = -1;
    bool dynamic_empty = false;
    if (runtime == NULL || menu == NULL ||
        !resolve_menu_item_at(runtime, menu, menu->selected_index, &item, &dynamic_index, &dynamic_empty) ||
        !menu_item_is_dynamic_list(item) || dynamic_empty)
        return;

    const char *selected_index_key = json_string(item, "selected_index_key", NULL);
    if (selected_index_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, selected_index_key, dynamic_index);
    const char *selected_value_key = json_string(item, "selected_value_key", NULL);
    char selected_value_buffer[SLAYER3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
    const char *selected_value =
        menu_dynamic_list_value(runtime, item, dynamic_index, selected_value_buffer, sizeof(selected_value_buffer));
    if (selected_value_key != NULL && selected_value != NULL)
        slayer3d_properties_set_string(runtime->scene_state, selected_value_key, selected_value);
}

bool json_value_matches_property(yyjson_val *value, const slayer3d_value *property);
static slayer3d_game_data_menu_pause_command parse_menu_pause_command(const char *pause)
{
    if (pause == NULL)
        return SLAYER3D_GAME_DATA_MENU_PAUSE_NONE;
    if (SDL_strcmp(pause, "pause") == 0)
        return SLAYER3D_GAME_DATA_MENU_PAUSE_PAUSE;
    if (SDL_strcmp(pause, "resume") == 0 || SDL_strcmp(pause, "unpause") == 0)
        return SLAYER3D_GAME_DATA_MENU_PAUSE_RESUME;
    if (SDL_strcmp(pause, "toggle") == 0)
        return SLAYER3D_GAME_DATA_MENU_PAUSE_TOGGLE;
    return SLAYER3D_GAME_DATA_MENU_PAUSE_NONE;
}

bool slayer3d_game_data_get_menu_item(const slayer3d_game_data_runtime *runtime, const char *menu_name, int index,
                                      slayer3d_game_data_menu_item *out_item)
{
    if (out_item != NULL)
    {
        SDL_zero(*out_item);
        out_item->signal_id = -1;
    }
    const scene_menu_state *menu = find_scene_menu_const(active_scene_entry_const(runtime), menu_name);
    const int item_count = menu_runtime_item_count(runtime, menu);
    if (runtime == NULL || menu == NULL || out_item == NULL || index < 0 || index >= item_count)
        return false;

    yyjson_val *item = NULL;
    int dynamic_index = -1;
    bool dynamic_empty = false;
    if (!resolve_menu_item_at(runtime, menu, index, &item, &dynamic_index, &dynamic_empty))
        return false;

    if (menu_item_is_dynamic_list(item))
    {
        out_item->label =
            menu_dynamic_list_label(runtime, item, dynamic_index, dynamic_empty, out_item->dynamic_list_label_storage,
                                    sizeof(out_item->dynamic_list_label_storage));
        out_item->scene = dynamic_empty ? NULL : json_string(item, "scene", NULL);
        out_item->return_to = dynamic_empty ? NULL : json_string(item, "return_to", NULL);
        yyjson_val *scene_state = obj_get(item, "scene_state");
        out_item->scene_state_key = dynamic_empty ? NULL : json_string(scene_state, "key", NULL);
        if (!dynamic_empty && out_item->scene_state_key != NULL)
        {
            const char *static_scene_state_value = json_string(scene_state, "value", NULL);
            out_item->scene_state_value =
                static_scene_state_value != NULL
                    ? static_scene_state_value
                    : menu_dynamic_scene_state_value(runtime, item, dynamic_index,
                                                     json_string(scene_state, "value_from", "value"),
                                                     out_item->dynamic_list_scene_state_value_storage,
                                                     sizeof(out_item->dynamic_list_scene_state_value_storage));
        }
        out_item->return_scene = !dynamic_empty && json_bool(item, "return_scene", false);
        out_item->quit = !dynamic_empty && json_bool(item, "quit", false);
        out_item->signal_id =
            dynamic_empty ? -1 : slayer3d_game_data_find_signal(runtime, json_string(item, "signal", NULL));
        out_item->pause_command = dynamic_empty ? SLAYER3D_GAME_DATA_MENU_PAUSE_NONE
                                                : parse_menu_pause_command(json_string(item, "pause", NULL));
        out_item->has_return_paused = !dynamic_empty && obj_get(item, "return_paused") != NULL;
        out_item->return_paused = !dynamic_empty && json_bool(item, "return_paused", false);
        out_item->dynamic_list_item = true;
        out_item->dynamic_list_name = json_string(item, "name", NULL);
        out_item->dynamic_list_index = dynamic_index;
        out_item->dynamic_list_value =
            dynamic_empty ? NULL
                          : menu_dynamic_list_value(runtime, item, dynamic_index, out_item->dynamic_list_value_storage,
                                                    sizeof(out_item->dynamic_list_value_storage));
        return out_item->label != NULL;
    }

    yyjson_val *control = obj_get(item, "control");
    out_item->label = json_string(item, "label", NULL);
    out_item->scene = json_string(item, "scene", NULL);
    out_item->return_to = json_string(item, "return_to", NULL);
    yyjson_val *scene_state = obj_get(item, "scene_state");
    out_item->scene_state_key = json_string(scene_state, "key", NULL);
    out_item->scene_state_value = json_string(scene_state, "value", NULL);
    out_item->return_scene = json_bool(item, "return_scene", false);
    out_item->quit = json_bool(item, "quit", false);
    out_item->signal_id = slayer3d_game_data_find_signal(runtime, json_string(item, "signal", NULL));
    out_item->pause_command = parse_menu_pause_command(json_string(item, "pause", NULL));
    out_item->has_return_paused = obj_get(item, "return_paused") != NULL;
    out_item->return_paused = json_bool(item, "return_paused", false);
    out_item->control_type = parse_menu_control_type(json_string(control, "type", NULL));
    out_item->control_target = json_string(control, "target", NULL);
    out_item->control_key = json_string(control, "key", NULL);
    out_item->choice_count =
        yyjson_is_arr(obj_get(control, "choices")) ? (int)yyjson_arr_size(obj_get(control, "choices")) : 0;
    out_item->input_binding_count =
        yyjson_is_arr(obj_get(control, "bindings")) ? (int)yyjson_arr_size(obj_get(control, "bindings")) : 0;
    out_item->dynamic_list_index = -1;
    return yyjson_is_obj(item) && out_item->label != NULL;
}

static yyjson_val *find_menu_item_control_json(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_menu_item *item)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    for (int m = 0; scene != NULL && item != NULL && m < scene->menu_count; ++m)
    {
        yyjson_val *items = obj_get(scene->menus[m].menu, "items");
        for (size_t i = 0; yyjson_is_arr(items) && i < yyjson_arr_size(items); ++i)
        {
            yyjson_val *candidate = yyjson_arr_get(items, i);
            yyjson_val *control = obj_get(candidate, "control");
            const char *label = json_string(candidate, "label", NULL);
            const char *target = json_string(control, "target", NULL);
            const char *key = json_string(control, "key", NULL);
            if (label != NULL && target != NULL && key != NULL && item->label != NULL && item->control_target != NULL &&
                item->control_key != NULL && SDL_strcmp(label, item->label) == 0 &&
                SDL_strcmp(target, item->control_target) == 0 && SDL_strcmp(key, item->control_key) == 0)
                return control;
        }
    }
    return NULL;
}

static yyjson_val *find_menu_item_at(const slayer3d_game_data_runtime *runtime, const char *menu_name, int item_index)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    const scene_menu_state *menu = find_scene_menu_const(scene, menu_name);
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    if (runtime == NULL || menu == NULL || !yyjson_is_arr(items) || item_index < 0 || item_index >= menu->item_count)
        return NULL;
    return yyjson_arr_get(items, (size_t)item_index);
}

static void set_menu_binding_status(slayer3d_game_data_runtime *runtime, const char *status)
{
    if (runtime != NULL && runtime->scene_state != NULL)
    {
        slayer3d_properties_set_string(runtime->scene_state, "keyboard_binding_status", status != NULL ? status : "");
        slayer3d_properties_set_string(runtime->scene_state, "mouse_binding_status", status != NULL ? status : "");
        slayer3d_properties_set_string(runtime->scene_state, "gamepad_binding_status", status != NULL ? status : "");
    }
}

static menu_binding_device menu_binding_control_device(yyjson_val *control)
{
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        const char *device = json_string(binding, "device", "keyboard");
        if (SDL_strcmp(device, "mouse") == 0)
            return MENU_BINDING_DEVICE_MOUSE_BUTTON;
        if (SDL_strcmp(device, "gamepad") == 0)
            return MENU_BINDING_DEVICE_GAMEPAD_BUTTON;
    }
    return MENU_BINDING_DEVICE_KEYBOARD;
}

static const char *menu_binding_device_input_name(menu_binding_device device)
{
    switch (device)
    {
    case MENU_BINDING_DEVICE_MOUSE_BUTTON:
        return "mouse button";
    case MENU_BINDING_DEVICE_GAMEPAD_BUTTON:
        return "button";
    case MENU_BINDING_DEVICE_KEYBOARD:
    default:
        return "key";
    }
}

static SDL_Scancode current_action_keyboard_binding(const slayer3d_game_data_runtime *runtime, const char *action)
{
    const int action_id = slayer3d_game_data_find_action(runtime, action);
    for (int i = 0; runtime != NULL && action_id >= 0 && i < runtime->input_binding_count; ++i)
    {
        const input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SLAYER3D_INPUT_KEYBOARD)
            return spec->scancode;
    }
    return SDL_SCANCODE_UNKNOWN;
}

static SDL_GamepadButton current_action_gamepad_button_binding(const slayer3d_game_data_runtime *runtime,
                                                               const char *action)
{
    const int action_id = slayer3d_game_data_find_action(runtime, action);
    for (int i = 0; runtime != NULL && action_id >= 0 && i < runtime->input_binding_count; ++i)
    {
        const input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SLAYER3D_INPUT_GAMEPAD_BUTTON)
            return spec->gamepad_button;
    }
    return SDL_GAMEPAD_BUTTON_INVALID;
}

static Uint8 current_action_mouse_button_binding(const slayer3d_game_data_runtime *runtime, const char *action)
{
    const int action_id = slayer3d_game_data_find_action(runtime, action);
    for (int i = 0; runtime != NULL && action_id >= 0 && i < runtime->input_binding_count; ++i)
    {
        const input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SLAYER3D_INPUT_MOUSE_BUTTON)
            return spec->mouse_button;
    }
    return 0;
}

static SDL_Scancode current_input_binding_control_scancode(const slayer3d_game_data_runtime *runtime,
                                                           yyjson_val *control)
{
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "keyboard") != 0)
            continue;
        const SDL_Scancode current = current_action_keyboard_binding(runtime, json_string(binding, "action", NULL));
        if (current != SDL_SCANCODE_UNKNOWN)
            return current;
    }
    return scancode_from_json(json_string(control, "default", NULL));
}

static SDL_GamepadButton current_input_binding_control_gamepad_button(const slayer3d_game_data_runtime *runtime,
                                                                      yyjson_val *control)
{
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "gamepad") != 0)
            continue;
        const SDL_GamepadButton current =
            current_action_gamepad_button_binding(runtime, json_string(binding, "action", NULL));
        if (current != SDL_GAMEPAD_BUTTON_INVALID)
            return current;
    }
    return gamepad_button_from_json(json_string(control, "default", NULL));
}

static Uint8 current_input_binding_control_mouse_button(const slayer3d_game_data_runtime *runtime, yyjson_val *control)
{
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "mouse") != 0)
            continue;
        const Uint8 current = current_action_mouse_button_binding(runtime, json_string(binding, "action", NULL));
        if (current != 0)
            return current;
    }
    return mouse_button_from_json(json_string(control, "default", NULL));
}

static bool set_input_binding_control_scancode(slayer3d_game_data_runtime *runtime, yyjson_val *control,
                                               SDL_Scancode scancode)
{
    bool changed = false;
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "keyboard") != 0)
            continue;
        if (set_action_keyboard_binding(runtime, json_string(binding, "action", NULL), scancode))
            changed = true;
    }
    return changed;
}

static bool set_input_binding_control_gamepad_button(slayer3d_game_data_runtime *runtime, yyjson_val *control,
                                                     SDL_GamepadButton button)
{
    bool changed = false;
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "gamepad") != 0)
            continue;
        if (set_action_gamepad_button_binding(runtime, json_string(binding, "action", NULL), button))
            changed = true;
    }
    return changed;
}

static bool set_input_binding_control_mouse_button(slayer3d_game_data_runtime *runtime, yyjson_val *control,
                                                   Uint8 button)
{
    bool changed = false;
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "mouse") != 0)
            continue;
        if (set_action_mouse_button_binding(runtime, json_string(binding, "action", NULL), button))
            changed = true;
    }
    return changed;
}

static const char *keyboard_binding_conflict_label(const slayer3d_game_data_runtime *runtime, const char *menu_name,
                                                   int item_index, SDL_Scancode scancode)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    const scene_menu_state *menu = find_scene_menu_const(scene, menu_name);
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    for (int i = 0; yyjson_is_arr(items) && i < menu->item_count; ++i)
    {
        if (i == item_index)
            continue;
        yyjson_val *item = yyjson_arr_get(items, (size_t)i);
        yyjson_val *control = obj_get(item, "control");
        if (parse_menu_control_type(json_string(control, "type", NULL)) !=
            SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            continue;
        if (menu_binding_control_device(control) != MENU_BINDING_DEVICE_KEYBOARD)
            continue;
        if (current_input_binding_control_scancode(runtime, control) == scancode)
            return json_string(item, "label", NULL);
    }
    return NULL;
}

static const char *gamepad_button_binding_conflict_label(const slayer3d_game_data_runtime *runtime,
                                                         const char *menu_name, int item_index,
                                                         SDL_GamepadButton button)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    const scene_menu_state *menu = find_scene_menu_const(scene, menu_name);
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    for (int i = 0; yyjson_is_arr(items) && i < menu->item_count; ++i)
    {
        if (i == item_index)
            continue;
        yyjson_val *item = yyjson_arr_get(items, (size_t)i);
        yyjson_val *control = obj_get(item, "control");
        if (parse_menu_control_type(json_string(control, "type", NULL)) !=
            SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            continue;
        if (menu_binding_control_device(control) != MENU_BINDING_DEVICE_GAMEPAD_BUTTON)
            continue;
        if (current_input_binding_control_gamepad_button(runtime, control) == button)
            return json_string(item, "label", NULL);
    }
    return NULL;
}

static const char *mouse_button_binding_conflict_label(const slayer3d_game_data_runtime *runtime, const char *menu_name,
                                                       int item_index, Uint8 button)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    const scene_menu_state *menu = find_scene_menu_const(scene, menu_name);
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    for (int i = 0; yyjson_is_arr(items) && i < menu->item_count; ++i)
    {
        if (i == item_index)
            continue;
        yyjson_val *item = yyjson_arr_get(items, (size_t)i);
        yyjson_val *control = obj_get(item, "control");
        if (parse_menu_control_type(json_string(control, "type", NULL)) !=
            SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            continue;
        if (menu_binding_control_device(control) != MENU_BINDING_DEVICE_MOUSE_BUTTON)
            continue;
        if (current_input_binding_control_mouse_button(runtime, control) == button)
            return json_string(item, "label", NULL);
    }
    return NULL;
}

bool slayer3d_game_data_start_menu_input_binding_capture(slayer3d_game_data_runtime *runtime, const char *menu_name,
                                                         int item_index)
{
    yyjson_val *item = find_menu_item_at(runtime, menu_name, item_index);
    yyjson_val *control = obj_get(item, "control");
    if (runtime == NULL || menu_name == NULL ||
        parse_menu_control_type(json_string(control, "type", NULL)) != SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
        return false;

    runtime->input_capture.active = true;
    runtime->input_capture.menu = menu_name;
    runtime->input_capture.item_index = item_index;
    char status[128];
    SDL_snprintf(status, sizeof(status), "Press a %s for %s",
                 menu_binding_device_input_name(menu_binding_control_device(control)),
                 json_string(item, "label", "binding"));
    set_menu_binding_status(runtime, status);
    return true;
}

bool slayer3d_game_data_menu_input_binding_capture_active(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->input_capture.active;
}

slayer3d_game_data_input_binding_capture_status slayer3d_game_data_update_menu_input_binding_capture(
    slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input)
{
    if (runtime == NULL || !runtime->input_capture.active)
        return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_NONE;

    yyjson_val *item = find_menu_item_at(runtime, runtime->input_capture.menu, runtime->input_capture.item_index);
    yyjson_val *control = obj_get(item, "control");
    const menu_binding_device device = menu_binding_control_device(control);
    if (device == MENU_BINDING_DEVICE_GAMEPAD_BUTTON)
    {
        const SDL_Scancode cancel_key = scancode_from_json(json_string(control, "cancel_key", "ESCAPE"));
        if (slayer3d_input_get_pressed_scancode(input) == cancel_key)
        {
            runtime->input_capture.active = false;
            set_menu_binding_status(runtime, "Canceled");
            return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CANCELED;
        }

        const SDL_GamepadButton button = slayer3d_input_get_pressed_gamepad_button(input);
        if (button == SDL_GAMEPAD_BUTTON_INVALID)
            return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

        const char *conflict = gamepad_button_binding_conflict_label(runtime, runtime->input_capture.menu,
                                                                     runtime->input_capture.item_index, button);
        if (conflict != NULL)
        {
            char status[128];
            SDL_snprintf(status, sizeof(status), "%s already uses %s", conflict, gamepad_button_display_name(button));
            set_menu_binding_status(runtime, status);
            return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CONFLICT;
        }

        if (!set_input_binding_control_gamepad_button(runtime, control, button))
            return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

        char status[128];
        SDL_snprintf(status, sizeof(status), "%s set to %s", json_string(item, "label", "Binding"),
                     gamepad_button_display_name(button));
        runtime->input_capture.active = false;
        set_menu_binding_status(runtime, status);
        return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CHANGED;
    }
    if (device == MENU_BINDING_DEVICE_MOUSE_BUTTON)
    {
        const SDL_Scancode cancel_key = scancode_from_json(json_string(control, "cancel_key", "ESCAPE"));
        if (slayer3d_input_get_pressed_scancode(input) == cancel_key)
        {
            runtime->input_capture.active = false;
            set_menu_binding_status(runtime, "Canceled");
            return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CANCELED;
        }

        const Uint8 button = slayer3d_input_get_pressed_mouse_button(input);
        if (button == 0)
            return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

        const char *conflict = mouse_button_binding_conflict_label(runtime, runtime->input_capture.menu,
                                                                   runtime->input_capture.item_index, button);
        if (conflict != NULL)
        {
            char status[128];
            SDL_snprintf(status, sizeof(status), "%s already uses %s", conflict, mouse_button_display_name(button));
            set_menu_binding_status(runtime, status);
            return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CONFLICT;
        }

        if (!set_input_binding_control_mouse_button(runtime, control, button))
            return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

        char status[128];
        SDL_snprintf(status, sizeof(status), "%s set to %s", json_string(item, "label", "Binding"),
                     mouse_button_display_name(button));
        runtime->input_capture.active = false;
        set_menu_binding_status(runtime, status);
        return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CHANGED;
    }

    const SDL_Scancode scancode = slayer3d_input_get_pressed_scancode(input);
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

    const SDL_Scancode cancel = scancode_from_json(json_string(control, "cancel_key", "ESCAPE"));
    if (scancode == cancel && !json_bool(control, "allow_escape", false))
    {
        runtime->input_capture.active = false;
        set_menu_binding_status(runtime, "Canceled");
        return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CANCELED;
    }

    const char *conflict = keyboard_binding_conflict_label(runtime, runtime->input_capture.menu,
                                                           runtime->input_capture.item_index, scancode);
    if (conflict != NULL)
    {
        char status[128];
        SDL_snprintf(status, sizeof(status), "%s already uses %s", conflict, scancode_display_name(scancode));
        set_menu_binding_status(runtime, status);
        return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CONFLICT;
    }

    if (!set_input_binding_control_scancode(runtime, control, scancode))
        return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

    char status[128];
    SDL_snprintf(status, sizeof(status), "%s set to %s", json_string(item, "label", "Binding"),
                 scancode_display_name(scancode));
    runtime->input_capture.active = false;
    set_menu_binding_status(runtime, status);
    return SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CHANGED;
}

bool slayer3d_game_data_reset_menu_input_bindings(slayer3d_game_data_runtime *runtime, const char *menu_name)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    const scene_menu_state *menu = find_scene_menu_const(scene, menu_name);
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    bool changed = false;
    for (int i = 0; yyjson_is_arr(items) && i < menu->item_count; ++i)
    {
        yyjson_val *item = yyjson_arr_get(items, (size_t)i);
        yyjson_val *control = obj_get(item, "control");
        if (parse_menu_control_type(json_string(control, "type", NULL)) !=
            SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            continue;
        const menu_binding_device device = menu_binding_control_device(control);
        if (device == MENU_BINDING_DEVICE_GAMEPAD_BUTTON)
        {
            const SDL_GamepadButton button = gamepad_button_from_json(json_string(control, "default", NULL));
            if (button != SDL_GAMEPAD_BUTTON_INVALID &&
                set_input_binding_control_gamepad_button(runtime, control, button))
                changed = true;
        }
        else if (device == MENU_BINDING_DEVICE_MOUSE_BUTTON)
        {
            const Uint8 button = mouse_button_from_json(json_string(control, "default", NULL));
            if (button != 0 && set_input_binding_control_mouse_button(runtime, control, button))
                changed = true;
        }
        else
        {
            const SDL_Scancode key = scancode_from_json(json_string(control, "default", NULL));
            if (key != SDL_SCANCODE_UNKNOWN && set_input_binding_control_scancode(runtime, control, key))
                changed = true;
        }
    }
    runtime->input_capture.active = false;
    if (changed)
        set_menu_binding_status(runtime, "Input settings reset");
    return changed;
}

static slayer3d_properties *menu_control_properties(slayer3d_game_data_runtime *runtime, yyjson_val *control)
{
    if (runtime == NULL || control == NULL)
        return NULL;

    const char *target = json_string(control, "target", NULL);
    if (target == NULL || SDL_strcmp(target, "scene_state") == 0)
        return runtime->scene_state;

    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    return actor != NULL ? actor->props : NULL;
}

static const slayer3d_properties *menu_control_properties_const(const slayer3d_game_data_runtime *runtime,
                                                                yyjson_val *control)
{
    return menu_control_properties((slayer3d_game_data_runtime *)runtime, control);
}

static const char *menu_control_string_value(const slayer3d_game_data_runtime *runtime, yyjson_val *control)
{
    const char *key = json_string(control, "key", NULL);
    const char *fallback = json_string(control, "default", "");
    const slayer3d_properties *props = menu_control_properties_const(runtime, control);
    return props != NULL && key != NULL ? slayer3d_properties_get_string(props, key, fallback) : fallback;
}

static const slayer3d_value *menu_control_value_or_default(const slayer3d_properties *props, const char *key,
                                                           yyjson_val *control, slayer3d_value *default_value)
{
    const slayer3d_value *value = props != NULL && key != NULL ? slayer3d_properties_get_value(props, key) : NULL;
    if (value == NULL && default_value != NULL)
    {
        yyjson_val *fallback = obj_get(control, "default");
        if (yyjson_is_str(fallback))
        {
            SDL_zero(*default_value);
            default_value->type = SLAYER3D_VALUE_STRING;
            default_value->as_string = (char *)yyjson_get_str(fallback);
            value = default_value;
        }
    }
    return value;
}

static bool menu_control_set_string_value(slayer3d_game_data_runtime *runtime, yyjson_val *control, const char *value)
{
    const char *key = json_string(control, "key", NULL);
    slayer3d_properties *props = menu_control_properties(runtime, control);
    if (props == NULL || key == NULL || value == NULL)
        return false;
    slayer3d_properties_set_string(props, key, value);
    return true;
}

static bool menu_text_restricted_charset(const char *charset)
{
    return charset != NULL && SDL_strcmp(charset, "text") != 0 && SDL_strcmp(charset, "utf8") != 0;
}

static bool menu_text_allowed_ascii(const char *charset, unsigned char ch)
{
    if (ch < 0x20U || ch == 0x7fU)
        return false;

    if (charset == NULL || SDL_strcmp(charset, "text") == 0 || SDL_strcmp(charset, "utf8") == 0)
        return true;
    if (SDL_strcmp(charset, "ascii") == 0)
        return ch < 0x80U;
    if (SDL_strcmp(charset, "integer") == 0 || SDL_strcmp(charset, "digits") == 0)
        return ch >= '0' && ch <= '9';
    if (SDL_strcmp(charset, "numeric") == 0)
        return (ch >= '0' && ch <= '9') || ch == '.' || ch == '-';
    if (SDL_strcmp(charset, "hostname") == 0)
    {
        return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '.' ||
               ch == '-' || ch == '_' || ch == ':';
    }
    return true;
}

static int menu_text_valid_utf8_sequence_bytes(const unsigned char *cursor, size_t remaining)
{
    if (cursor == NULL || remaining == 0U || cursor[0] == '\0')
        return 0;

    const unsigned char b0 = cursor[0];
    if ((b0 & 0x80U) == 0U)
        return 1;
    if (remaining < 2U)
        return 0;
    if (b0 >= 0xc2U && b0 <= 0xdfU)
        return (cursor[1] & 0xc0U) == 0x80U ? 2 : 0;
    if (remaining < 3U)
        return 0;
    if (b0 == 0xe0U)
        return (cursor[1] >= 0xa0U && cursor[1] <= 0xbfU && (cursor[2] & 0xc0U) == 0x80U) ? 3 : 0;
    if ((b0 >= 0xe1U && b0 <= 0xecU) || (b0 >= 0xeeU && b0 <= 0xefU))
        return ((cursor[1] & 0xc0U) == 0x80U && (cursor[2] & 0xc0U) == 0x80U) ? 3 : 0;
    if (b0 == 0xedU)
        return (cursor[1] >= 0x80U && cursor[1] <= 0x9fU && (cursor[2] & 0xc0U) == 0x80U) ? 3 : 0;
    if (remaining < 4U)
        return 0;
    if (b0 == 0xf0U)
        return (cursor[1] >= 0x90U && cursor[1] <= 0xbfU && (cursor[2] & 0xc0U) == 0x80U &&
                (cursor[3] & 0xc0U) == 0x80U)
                   ? 4
                   : 0;
    if (b0 >= 0xf1U && b0 <= 0xf3U)
        return ((cursor[1] & 0xc0U) == 0x80U && (cursor[2] & 0xc0U) == 0x80U && (cursor[3] & 0xc0U) == 0x80U) ? 4 : 0;
    if (b0 == 0xf4U)
        return (cursor[1] >= 0x80U && cursor[1] <= 0x8fU && (cursor[2] & 0xc0U) == 0x80U &&
                (cursor[3] & 0xc0U) == 0x80U)
                   ? 4
                   : 0;
    return 0;
}

static bool menu_text_append_filtered(yyjson_val *control, char *buffer, size_t buffer_size, const char *text)
{
    if (control == NULL || buffer == NULL || buffer_size == 0U || text == NULL || text[0] == '\0')
        return false;

    bool changed = false;
    size_t used = SDL_strlen(buffer);
    const int max_length = SDL_clamp(json_int(control, "max_length", SLAYER3D_GAME_DATA_MENU_TEXT_MAX_BYTES), 0,
                                     (int)SDL_min(buffer_size - 1U, (size_t)SLAYER3D_GAME_DATA_MENU_TEXT_MAX_BYTES));
    const char *charset = json_string(control, "charset", json_string(control, "allow", "text"));
    const unsigned char *cursor = (const unsigned char *)text;
    size_t remaining = SDL_strlen(text);
    while (remaining > 0U && *cursor != '\0' && used < (size_t)max_length)
    {
        int bytes = menu_text_valid_utf8_sequence_bytes(cursor, remaining);
        if (bytes <= 0)
        {
            ++cursor;
            --remaining;
            continue;
        }
        if (bytes == 1)
        {
            if (!menu_text_allowed_ascii(charset, *cursor))
            {
                ++cursor;
                --remaining;
                continue;
            }
        }
        else if (menu_text_restricted_charset(charset))
        {
            cursor += bytes;
            remaining -= (size_t)bytes;
            continue;
        }

        if (used + (size_t)bytes > (size_t)max_length || used + (size_t)bytes + 1U > buffer_size)
            break;
        for (int i = 0; i < bytes; ++i)
            buffer[used++] = (char)*cursor++;
        remaining -= (size_t)bytes;
        buffer[used] = '\0';
        changed = true;
    }
    return changed;
}

static bool menu_text_backspace(char *buffer)
{
    if (buffer == NULL || buffer[0] == '\0')
        return false;

    size_t len = SDL_strlen(buffer);
    do
    {
        --len;
    } while (len > 0U && (((unsigned char)buffer[len] & 0xc0U) == 0x80U));
    buffer[len] = '\0';
    return true;
}

bool slayer3d_game_data_start_menu_text_entry_capture(slayer3d_game_data_runtime *runtime, const char *menu_name,
                                                      int item_index)
{
    yyjson_val *item = find_menu_item_at(runtime, menu_name, item_index);
    yyjson_val *control = obj_get(item, "control");
    if (runtime == NULL || menu_name == NULL ||
        parse_menu_control_type(json_string(control, "type", NULL)) != SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
        return false;

    clear_menu_text_entry_capture(runtime);
    runtime->text_capture.active = true;
    runtime->text_capture.menu = menu_name;
    runtime->text_capture.item_index = item_index;
    runtime->text_capture.original = SDL_strdup(menu_control_string_value(runtime, control));
    if (runtime->text_capture.original == NULL)
    {
        clear_menu_text_entry_capture(runtime);
        return false;
    }
    (void)menu_control_set_string_value(runtime, control, runtime->text_capture.original);
    return true;
}

bool slayer3d_game_data_menu_text_entry_capture_active(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->text_capture.active;
}

slayer3d_game_data_text_entry_capture_status slayer3d_game_data_update_menu_text_entry_capture(
    slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input)
{
    if (runtime == NULL || !runtime->text_capture.active)
        return SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_NONE;

    yyjson_val *item = find_menu_item_at(runtime, runtime->text_capture.menu, runtime->text_capture.item_index);
    yyjson_val *control = obj_get(item, "control");
    if (parse_menu_control_type(json_string(control, "type", NULL)) != SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
    {
        clear_menu_text_entry_capture(runtime);
        return SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CANCELED;
    }

    const scene_menu_state *menu = find_scene_menu_const(active_scene_entry_const(runtime), runtime->text_capture.menu);
    slayer3d_game_data_menu menu_desc;
    SDL_zero(menu_desc);
    menu_desc.select_action_id = -1;
    menu_desc.back_action_id = -1;
    if (menu != NULL)
        (void)slayer3d_game_data_get_active_menu(runtime, &menu_desc);

    const SDL_Scancode scancode = slayer3d_input_get_pressed_scancode(input);
    const SDL_Scancode cancel_key = scancode_from_json(json_string(control, "cancel_key", "ESCAPE"));
    const bool cancel_pressed =
        (cancel_key != SDL_SCANCODE_UNKNOWN && scancode == cancel_key) ||
        (menu_desc.back_action_id >= 0 && slayer3d_input_is_pressed(input, menu_desc.back_action_id));
    if (cancel_pressed)
    {
        (void)menu_control_set_string_value(
            runtime, control, runtime->text_capture.original != NULL ? runtime->text_capture.original : "");
        clear_menu_text_entry_capture(runtime);
        return SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CANCELED;
    }

    const bool submit_pressed =
        scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER ||
        (menu_desc.select_action_id >= 0 && slayer3d_input_is_pressed(input, menu_desc.select_action_id));
    if (submit_pressed)
    {
        clear_menu_text_entry_capture(runtime);
        return SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_SUBMITTED;
    }

    char value[256];
    SDL_strlcpy(value, menu_control_string_value(runtime, control), sizeof(value));
    bool changed = false;
    if (scancode == SDL_SCANCODE_BACKSPACE || scancode == SDL_SCANCODE_DELETE)
        changed = menu_text_backspace(value) || changed;
    changed = menu_text_append_filtered(control, value, sizeof(value), slayer3d_input_get_text_input(input)) || changed;
    if (changed)
    {
        (void)menu_control_set_string_value(runtime, control, value);
        return SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CHANGED;
    }
    return SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_WAITING;
}

bool set_property_from_json(slayer3d_properties *props, const char *key, yyjson_val *value)
{
    if (props == NULL || key == NULL || value == NULL)
        return false;
    if (yyjson_is_arr(value) && yyjson_arr_size(value) == 3 && yyjson_is_num(yyjson_arr_get(value, 0)) &&
        yyjson_is_num(yyjson_arr_get(value, 1)) && yyjson_is_num(yyjson_arr_get(value, 2)))
    {
        slayer3d_properties_set_vec3(props, key, json_vec3_value(value, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
        return true;
    }
    if (yyjson_is_arr(value) && yyjson_arr_size(value) == 4 && yyjson_is_num(yyjson_arr_get(value, 0)) &&
        yyjson_is_num(yyjson_arr_get(value, 1)) && yyjson_is_num(yyjson_arr_get(value, 2)) &&
        yyjson_is_num(yyjson_arr_get(value, 3)))
    {
        slayer3d_properties_set_color(props, key, json_color_value(value, (slayer3d_color){255, 255, 255, 255}));
        return true;
    }
    if (yyjson_is_bool(value))
    {
        slayer3d_properties_set_bool(props, key, yyjson_get_bool(value));
        return true;
    }
    if (yyjson_is_int(value))
    {
        slayer3d_properties_set_int(props, key, (int)yyjson_get_sint(value));
        return true;
    }
    if (yyjson_is_num(value))
    {
        slayer3d_properties_set_float(props, key, (float)yyjson_get_num(value));
        return true;
    }
    if (yyjson_is_str(value))
    {
        slayer3d_properties_set_string(props, key, yyjson_get_str(value));
        return true;
    }
    return false;
}

bool json_value_matches_property(yyjson_val *value, const slayer3d_value *property)
{
    if (value == NULL || property == NULL)
        return false;
    switch (property->type)
    {
    case SLAYER3D_VALUE_BOOL:
        return yyjson_is_bool(value) && yyjson_get_bool(value) == property->as_bool;
    case SLAYER3D_VALUE_INT:
        return yyjson_is_int(value) && (int)yyjson_get_sint(value) == property->as_int;
    case SLAYER3D_VALUE_FLOAT:
        return yyjson_is_num(value) && SDL_fabsf((float)yyjson_get_num(value) - property->as_float) < 0.0001f;
    case SLAYER3D_VALUE_STRING:
        return yyjson_is_str(value) &&
               SDL_strcmp(yyjson_get_str(value), property->as_string != NULL ? property->as_string : "") == 0;
    case SLAYER3D_VALUE_VEC3:
    case SLAYER3D_VALUE_COLOR:
        return false;
    }
    return false;
}

static bool menu_range_is_integer(yyjson_val *control)
{
    return SDL_strcmp(json_string(control, "value_type", ""), "int") == 0 ||
           SDL_strcmp(json_string(control, "value_type", ""), "integer") == 0;
}

static float menu_control_numeric_value(const slayer3d_value *value, yyjson_val *control, float fallback)
{
    if (value != NULL && value->type == SLAYER3D_VALUE_INT)
        return (float)value->as_int;
    if (value != NULL && value->type == SLAYER3D_VALUE_FLOAT)
        return value->as_float;
    return json_float(control, "default", fallback);
}

bool slayer3d_game_data_adjust_menu_item_control(slayer3d_game_data_runtime *runtime,
                                                 const slayer3d_game_data_menu_item *item, int direction)
{
    if (direction == 0)
        return false;
    if (runtime == NULL || item == NULL || item->control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_NONE ||
        item->control_target == NULL || item->control_key == NULL)
        return false;

    yyjson_val *control = find_menu_item_control_json(runtime, item);
    slayer3d_properties *props = menu_control_properties(runtime, control);
    if (props == NULL)
        return false;
    const int step_direction = direction < 0 ? -1 : 1;
    switch (item->control_type)
    {
    case SLAYER3D_GAME_DATA_MENU_CONTROL_TOGGLE: {
        const bool current =
            slayer3d_properties_get_bool(props, item->control_key, json_bool(control, "default", false));
        slayer3d_properties_set_bool(props, item->control_key, !current);
        return true;
    }
    case SLAYER3D_GAME_DATA_MENU_CONTROL_CHOICE: {
        yyjson_val *choices = obj_get(control, "choices");
        if (!yyjson_is_arr(choices) || yyjson_arr_size(choices) == 0)
            return false;
        slayer3d_value default_value;
        const slayer3d_value *current =
            menu_control_value_or_default(props, item->control_key, control, &default_value);
        int current_index = -1;
        for (size_t i = 0; i < yyjson_arr_size(choices); ++i)
        {
            yyjson_val *choice = yyjson_arr_get(choices, i);
            yyjson_val *value = yyjson_is_obj(choice) ? obj_get(choice, "value") : choice;
            if (json_value_matches_property(value, current))
            {
                current_index = (int)i;
                break;
            }
        }
        const int choice_count = (int)yyjson_arr_size(choices);
        int next_index =
            current_index >= 0 ? current_index + step_direction : (step_direction < 0 ? choice_count - 1 : 0);
        next_index %= choice_count;
        if (next_index < 0)
            next_index += choice_count;
        yyjson_val *next = yyjson_arr_get(choices, (size_t)next_index);
        return set_property_from_json(props, item->control_key, yyjson_is_obj(next) ? obj_get(next, "value") : next);
    }
    case SLAYER3D_GAME_DATA_MENU_CONTROL_RANGE: {
        const float min_value = json_float(control, "min", 0.0f);
        const float max_value = json_float(control, "max", 1.0f);
        const float step = json_float(control, "step", 1.0f);
        const slayer3d_value *current = slayer3d_properties_get_value(props, item->control_key);
        float value = menu_control_numeric_value(current, control, min_value);
        value = SDL_clamp(value + step * (float)step_direction, min_value, max_value);
        if (menu_range_is_integer(control))
            slayer3d_properties_set_int(props, item->control_key, (int)SDL_lroundf(value));
        else
            slayer3d_properties_set_float(props, item->control_key, value);
        return true;
    }
    case SLAYER3D_GAME_DATA_MENU_CONTROL_NONE:
    case SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING:
    case SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT:
        return false;
    }
    return false;
}

bool slayer3d_game_data_apply_menu_item_control(slayer3d_game_data_runtime *runtime,
                                                const slayer3d_game_data_menu_item *item)
{
    return slayer3d_game_data_adjust_menu_item_control(runtime, item, 1);
}

int slayer3d_game_data_scene_shortcut_count(const slayer3d_game_data_runtime *runtime)
{
    yyjson_val *shortcuts = obj_get(obj_get(runtime_root(runtime), "app"), "scene_shortcuts");
    return yyjson_is_arr(shortcuts) ? (int)yyjson_arr_size(shortcuts) : 0;
}

bool slayer3d_game_data_scene_shortcut_at(const slayer3d_game_data_runtime *runtime, int index,
                                          slayer3d_game_data_scene_shortcut *out_shortcut)
{
    if (out_shortcut != NULL)
    {
        SDL_zero(*out_shortcut);
        out_shortcut->action_id = -1;
    }
    yyjson_val *shortcuts = obj_get(obj_get(runtime_root(runtime), "app"), "scene_shortcuts");
    if (runtime == NULL || out_shortcut == NULL || !yyjson_is_arr(shortcuts) || index < 0 ||
        index >= (int)yyjson_arr_size(shortcuts))
        return false;

    yyjson_val *shortcut = yyjson_arr_get(shortcuts, (size_t)index);
    out_shortcut->action = json_string(shortcut, "action", NULL);
    out_shortcut->scene = json_string(shortcut, "scene", NULL);
    out_shortcut->action_id = slayer3d_game_data_find_action(runtime, out_shortcut->action);
    return yyjson_is_obj(shortcut) && out_shortcut->action != NULL && out_shortcut->scene != NULL;
}

bool slayer3d_game_data_active_menu_input_is_idle(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_input_manager *input)
{
    if (slayer3d_game_data_menu_input_binding_capture_active(runtime))
        return false;
    if (slayer3d_game_data_menu_text_entry_capture_active(runtime))
        return false;
    slayer3d_game_data_menu menu;
    if (!slayer3d_game_data_get_active_menu(runtime, &menu))
        return true;
    if (input == NULL)
        return false;

    return (menu.up_action_id < 0 || !slayer3d_game_data_active_scene_allows_action(runtime, menu.up_action_id) ||
            !slayer3d_input_is_held(input, menu.up_action_id)) &&
           (menu.down_action_id < 0 || !slayer3d_game_data_active_scene_allows_action(runtime, menu.down_action_id) ||
            !slayer3d_input_is_held(input, menu.down_action_id)) &&
           (menu.left_action_id < 0 || !slayer3d_game_data_active_scene_allows_action(runtime, menu.left_action_id) ||
            !slayer3d_input_is_held(input, menu.left_action_id)) &&
           (menu.right_action_id < 0 || !slayer3d_game_data_active_scene_allows_action(runtime, menu.right_action_id) ||
            !slayer3d_input_is_held(input, menu.right_action_id)) &&
           (menu.select_action_id < 0 ||
            !slayer3d_game_data_active_scene_allows_action(runtime, menu.select_action_id) ||
            !slayer3d_input_is_held(input, menu.select_action_id));
}

static void ui_text_from_json(yyjson_val *item, slayer3d_game_data_ui_text *text)
{
    if (text == NULL)
        return;
    SDL_zero(*text);
    text->name = json_string(item, "name", NULL);
    text->font = json_string(item, "font", NULL);
    text->text = json_string(item, "text", NULL);
    text->format = json_string(item, "format", NULL);
    text->source = json_string(item, "source", NULL);
    text->visible = json_string(item, "visible", "always");
    text->layer = json_int(item, "layer", json_int(item, "z", 0));
    text->x = json_float(item, "x", 0.0f);
    text->y = json_float(item, "y", 0.0f);
    text->normalized = json_bool(item, "normalized", false);
    text->align = parse_ui_align(json_string(item, "align", NULL), json_bool(item, "centered", false)
                                                                       ? SLAYER3D_GAME_DATA_UI_ALIGN_CENTER
                                                                       : SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
    text->centered = text->align == SLAYER3D_GAME_DATA_UI_ALIGN_CENTER;
    text->scale = json_float(item, "scale", 1.0f);
    text->pulse_alpha = json_bool(item, "pulse_alpha", false);
    text->color = json_color(item, "color", (slayer3d_color){255, 255, 255, 255});
}

static bool for_each_authored_ui_text_root(yyjson_val *root, slayer3d_game_data_ui_text_fn callback, void *userdata)
{
    yyjson_val *texts = obj_get(obj_get(root, "ui"), "text");
    for (size_t i = 0; yyjson_is_arr(texts) && i < yyjson_arr_size(texts); ++i)
    {
        slayer3d_game_data_ui_text text;
        ui_text_from_json(yyjson_arr_get(texts, i), &text);
        if (!callback(userdata, &text))
            return false;
    }
    return true;
}

static bool ui_tool_row_value_to_string(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_ui_metrics *metrics, yyjson_val *row, char *buffer,
                                        size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0)
        return false;
    buffer[0] = '\0';
    yyjson_val *binding = obj_get(row, "binding");
    if (yyjson_is_obj(binding))
        return slayer3d_game_data_ui_binding_to_string(runtime, metrics, binding, buffer, buffer_size);
    return slayer3d_game_data_ui_json_scalar_to_string(obj_get(row, "value"), buffer, buffer_size);
}

static bool emit_inspector_text(const slayer3d_game_data_runtime *runtime, const slayer3d_game_data_ui_metrics *metrics,
                                yyjson_val *inspector, const char *text_value, float x, float y, slayer3d_color color,
                                slayer3d_game_data_ui_align align, slayer3d_game_data_ui_text_fn callback,
                                void *userdata)
{
    if (text_value == NULL || text_value[0] == '\0')
        return true;

    yyjson_val *active_if = obj_get(inspector, "visible_if");
    if (active_if != NULL && !eval_data_condition(runtime, active_if, metrics))
        return true;

    slayer3d_game_data_ui_text text;
    SDL_zero(text);
    text.name = json_string(inspector, "name", NULL);
    text.font = json_string(inspector, "font", NULL);
    text.text = text_value;
    text.visible = "always";
    text.x = x;
    text.y = y;
    text.normalized = json_bool(inspector, "normalized", false);
    text.align = align;
    text.centered = align == SLAYER3D_GAME_DATA_UI_ALIGN_CENTER;
    text.scale = json_float(inspector, "scale", 1.0f);
    text.color = color;
    return callback(userdata, &text);
}

static bool for_each_ui_inspector_text_root(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_ui_metrics *metrics, yyjson_val *root,
                                            slayer3d_game_data_ui_text_fn callback, void *userdata)
{
    yyjson_val *inspectors = obj_get(obj_get(root, "ui"), "inspectors");
    for (size_t i = 0; yyjson_is_arr(inspectors) && i < yyjson_arr_size(inspectors); ++i)
    {
        yyjson_val *inspector = yyjson_arr_get(inspectors, i);
        const bool normalized = json_bool(inspector, "normalized", false);
        const float x = json_float(inspector, "x", 0.0f);
        float y = json_float(inspector, "y", 0.0f);
        const float width = json_float(inspector, "w", json_float(inspector, "width", normalized ? 0.25f : 280.0f));
        const float row_height = json_float(inspector, "row_height", normalized ? 0.032f : 24.0f);
        const float label_width = json_float(inspector, "label_width", width * 0.42f);
        const float padding = json_float(inspector, "padding", normalized ? 0.012f : 10.0f);
        const float value_x = x + padding + label_width;
        const float row_x = x + padding;
        const slayer3d_color title_color = json_color(inspector, "title_color", (slayer3d_color){235, 240, 255, 255});
        const slayer3d_color label_color = json_color(inspector, "label_color", (slayer3d_color){160, 176, 205, 255});
        const slayer3d_color value_color = json_color(inspector, "value_color", (slayer3d_color){255, 255, 255, 255});
        const char *title = json_string(inspector, "title", NULL);
        if (title != NULL && title[0] != '\0')
        {
            if (!emit_inspector_text(runtime, metrics, inspector, title, row_x, y + padding, title_color,
                                     SLAYER3D_GAME_DATA_UI_ALIGN_LEFT, callback, userdata))
                return false;
            y += row_height;
        }

        yyjson_val *rows = obj_get(inspector, "rows");
        for (size_t row_index = 0; yyjson_is_arr(rows) && row_index < yyjson_arr_size(rows); ++row_index)
        {
            yyjson_val *row = yyjson_arr_get(rows, row_index);
            const float row_y = y + padding + row_height * (float)row_index;
            if (!emit_inspector_text(runtime, metrics, inspector, json_string(row, "label", ""), row_x, row_y,
                                     label_color, SLAYER3D_GAME_DATA_UI_ALIGN_LEFT, callback, userdata))
                return false;
            char value[128];
            if (!ui_tool_row_value_to_string(runtime, metrics, row, value, sizeof(value)))
                SDL_strlcpy(value, json_string(row, "empty", "-"), sizeof(value));
            if (!emit_inspector_text(runtime, metrics, inspector, value, value_x, row_y, value_color,
                                     SLAYER3D_GAME_DATA_UI_ALIGN_LEFT, callback, userdata))
                return false;
        }
    }
    return true;
}

static bool emit_ui_menu_cursor(yyjson_val *presenter, float row_y, slayer3d_game_data_ui_text_fn callback,
                                void *userdata)
{
    yyjson_val *cursor = obj_get(presenter, "cursor");
    if (!yyjson_is_obj(cursor) || !json_bool(cursor, "visible", true))
        return true;

    slayer3d_game_data_ui_text text;
    SDL_zero(text);
    text.name = json_string(presenter, "name", NULL);
    text.font = json_string(cursor, "font", json_string(presenter, "font", NULL));
    text.text = json_string(cursor, "text", ">");
    text.visible = "always";
    text.x = json_float(presenter, "x", 0.0f) + json_float(cursor, "offset_x", -0.05f);
    text.y = row_y + json_float(cursor, "offset_y", 0.0f);
    text.normalized = json_bool(presenter, "normalized", false);
    text.align = parse_ui_align(json_string(cursor, "align", NULL), SLAYER3D_GAME_DATA_UI_ALIGN_CENTER);
    text.centered = text.align == SLAYER3D_GAME_DATA_UI_ALIGN_CENTER;
    text.scale = json_float(cursor, "scale", json_float(presenter, "scale", 1.0f));
    text.pulse_alpha = json_bool(cursor, "pulse_alpha", false);
    text.color = json_color(cursor, "color", (slayer3d_color){255, 222, 140, 255});
    return callback(userdata, &text);
}

static const char *choice_label_for_property(const slayer3d_game_data_runtime *runtime, yyjson_val *control)
{
    const slayer3d_properties *props = menu_control_properties_const(runtime, control);
    const char *key = json_string(control, "key", NULL);
    slayer3d_value default_value;
    const slayer3d_value *value = menu_control_value_or_default(props, key, control, &default_value);
    yyjson_val *choices = obj_get(control, "choices");
    for (size_t i = 0; yyjson_is_arr(choices) && i < yyjson_arr_size(choices); ++i)
    {
        yyjson_val *choice = yyjson_arr_get(choices, i);
        yyjson_val *choice_value = yyjson_is_obj(choice) ? obj_get(choice, "value") : choice;
        if (json_value_matches_property(choice_value, value))
            return yyjson_is_obj(choice) ? json_string(choice, "label", NULL) : NULL;
    }
    return NULL;
}

static void format_menu_item_label(const slayer3d_game_data_runtime *runtime, yyjson_val *item, char *buffer,
                                   size_t buffer_size)
{
    const char *label = json_string(item, "label", "");
    yyjson_val *control = obj_get(item, "control");
    const slayer3d_game_data_menu_control_type type = parse_menu_control_type(json_string(control, "type", NULL));
    if (buffer == NULL || buffer_size == 0)
        return;

    if (type == SLAYER3D_GAME_DATA_MENU_CONTROL_NONE)
    {
        SDL_strlcpy(buffer, label, buffer_size);
        return;
    }
    if (type == SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
    {
        if (runtime != NULL && runtime->input_capture.active &&
            item == find_menu_item_at(runtime, runtime->input_capture.menu, runtime->input_capture.item_index))
        {
            SDL_snprintf(buffer, buffer_size, "%s: Press a %s", label,
                         menu_binding_device_input_name(menu_binding_control_device(control)));
            return;
        }
        const menu_binding_device device = menu_binding_control_device(control);
        if (device == MENU_BINDING_DEVICE_GAMEPAD_BUTTON)
        {
            SDL_snprintf(buffer, buffer_size, "%s: %s", label,
                         gamepad_button_display_name(current_input_binding_control_gamepad_button(runtime, control)));
        }
        else if (device == MENU_BINDING_DEVICE_MOUSE_BUTTON)
        {
            SDL_snprintf(buffer, buffer_size, "%s: %s", label,
                         mouse_button_display_name(current_input_binding_control_mouse_button(runtime, control)));
        }
        else
        {
            SDL_snprintf(buffer, buffer_size, "%s: %s", label,
                         scancode_display_name(current_input_binding_control_scancode(runtime, control)));
        }
        return;
    }
    if (type == SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
    {
        char value[256];
        SDL_strlcpy(value, menu_control_string_value(runtime, control), sizeof(value));
        const bool active =
            runtime != NULL && runtime->text_capture.active &&
            item == find_menu_item_at(runtime, runtime->text_capture.menu, runtime->text_capture.item_index);
        const char *placeholder = json_string(control, "placeholder", "");
        const char *cursor = json_string(control, "cursor", "|");
        if (value[0] == '\0' && placeholder != NULL && placeholder[0] != '\0' && !active)
            SDL_snprintf(buffer, buffer_size, "%s: %s", label, placeholder);
        else if (active)
            SDL_snprintf(buffer, buffer_size, "%s: %s%s", label, value, cursor != NULL ? cursor : "|");
        else
            SDL_snprintf(buffer, buffer_size, "%s: %s", label, value);
        return;
    }

    const slayer3d_properties *props = menu_control_properties_const(runtime, control);
    const char *key = json_string(control, "key", NULL);
    slayer3d_value default_value;
    const slayer3d_value *value = menu_control_value_or_default(props, key, control, &default_value);
    if (type == SLAYER3D_GAME_DATA_MENU_CONTROL_TOGGLE)
    {
        const bool enabled =
            value != NULL && value->type == SLAYER3D_VALUE_BOOL ? value->as_bool : json_bool(control, "default", false);
        SDL_snprintf(buffer, buffer_size, "%s: %s", label, enabled ? "On" : "Off");
        return;
    }
    if (type == SLAYER3D_GAME_DATA_MENU_CONTROL_CHOICE)
    {
        const char *choice_label = choice_label_for_property(runtime, control);
        if (choice_label == NULL && value != NULL && value->type == SLAYER3D_VALUE_STRING)
            choice_label = value->as_string;
        SDL_snprintf(buffer, buffer_size, "%s: %s", label, choice_label != NULL ? choice_label : "-");
        return;
    }
    if (type == SLAYER3D_GAME_DATA_MENU_CONTROL_RANGE)
    {
        const float min_value = json_float(control, "min", 0.0f);
        const float max_value = json_float(control, "max", 1.0f);
        const float current = SDL_clamp(menu_control_numeric_value(value, control, min_value), min_value, max_value);
        if (SDL_strcmp(json_string(control, "display", ""), "slider") == 0)
        {
            const int slots = SDL_max(1, json_int(control, "slots", 10));
            const float t = max_value > min_value ? (current - min_value) / (max_value - min_value) : 0.0f;
            int filled = (int)SDL_lroundf(SDL_clamp(t, 0.0f, 1.0f) * (float)slots);
            filled = SDL_clamp(filled, 0, slots);
            const char left = first_json_string_char(control, "slider_left", '[');
            const char fill = first_json_string_char(control, "slider_fill", '#');
            const char empty = first_json_string_char(control, "slider_empty", '-');
            const char right = first_json_string_char(control, "slider_right", ']');
            char slider[64];
            size_t pos = 0;
            slider[pos++] = left;
            for (int i = 0; i < slots && pos + 2 < sizeof(slider); ++i)
                slider[pos++] = i < filled ? fill : empty;
            slider[pos++] = right;
            slider[pos] = '\0';
            if (menu_range_is_integer(control))
                SDL_snprintf(buffer, buffer_size, "%s  %s %d/%d", label, slider, (int)SDL_lroundf(current),
                             (int)SDL_lroundf(max_value));
            else
                SDL_snprintf(buffer, buffer_size, "%s  %s %.2g", label, slider, current);
            return;
        }
        if (menu_range_is_integer(control))
            SDL_snprintf(buffer, buffer_size, "%s: %d", label, (int)SDL_lroundf(current));
        else
            SDL_snprintf(buffer, buffer_size, "%s: %.2g", label, current);
        return;
    }
    SDL_strlcpy(buffer, label, buffer_size);
}

static bool for_each_ui_menu_presenter(const slayer3d_game_data_runtime *runtime, const scene_entry *scene,
                                       const slayer3d_game_data_ui_metrics *metrics, yyjson_val *presenter,
                                       slayer3d_game_data_ui_text_fn callback, void *userdata)
{
    if (scene == NULL || presenter == NULL)
        return true;

    const scene_menu_state *menu_state = find_scene_menu_const(scene, json_string(presenter, "menu", NULL));
    if (menu_state == NULL)
        return true;
    const int item_count = menu_runtime_item_count(runtime, menu_state);
    if (item_count <= 0)
        return true;

    yyjson_val *presenter_active_if = obj_get(presenter, "active_if");
    if (presenter_active_if != NULL && !eval_data_condition(runtime, presenter_active_if, metrics))
        return true;
    yyjson_val *menu_active_if = obj_get(menu_state->menu, "active_if");
    if (menu_active_if != NULL && !eval_data_condition(runtime, menu_active_if, metrics))
        return true;

    const float x = json_float(presenter, "x", 0.0f);
    const float y = json_float(presenter, "y", 0.0f);
    const float gap = json_float(presenter, "gap", json_bool(presenter, "normalized", false) ? 0.08f : 42.0f);
    const bool normalized = json_bool(presenter, "normalized", false);
    const slayer3d_game_data_ui_align align =
        parse_ui_align(json_string(presenter, "align", NULL), SLAYER3D_GAME_DATA_UI_ALIGN_CENTER);
    const int selected_index = SDL_clamp(menu_state->selected_index, 0, item_count - 1);
    const int visible_count = json_int(presenter, "visible_count", item_count);
    int first_index = 0;
    int last_index = item_count;
    if (visible_count > 0 && visible_count < item_count)
    {
        first_index = selected_index - visible_count / 2;
        first_index = SDL_clamp(first_index, 0, item_count - visible_count);
        last_index = first_index + visible_count;
    }

    for (int i = first_index; i < last_index; ++i)
    {
        yyjson_val *item = NULL;
        int dynamic_index = -1;
        bool dynamic_empty = false;
        if (!resolve_menu_item_at(runtime, menu_state, i, &item, &dynamic_index, &dynamic_empty))
            continue;
        const bool selected = i == selected_index;
        const float row_y = y + gap * (float)(i - first_index);

        if (selected && !emit_ui_menu_cursor(presenter, row_y, callback, userdata))
            return false;

        slayer3d_game_data_ui_text text;
        char label[128];
        if (menu_item_is_dynamic_list(item))
            (void)menu_dynamic_list_label(runtime, item, dynamic_index, dynamic_empty, label, sizeof(label));
        else
            format_menu_item_label(runtime, item, label, sizeof(label));
        SDL_zero(text);
        text.name = json_string(presenter, "name", NULL);
        text.font = json_string(presenter, "font", NULL);
        text.text = label;
        text.visible = "always";
        text.x = x;
        text.y = row_y;
        text.normalized = normalized;
        text.align = align;
        text.centered = align == SLAYER3D_GAME_DATA_UI_ALIGN_CENTER;
        text.scale = json_float(presenter, "scale", 1.0f);
        text.pulse_alpha = selected && json_bool(presenter, "selected_pulse_alpha", false);
        text.color = selected ? json_color(presenter, "selected_color",
                                           json_color(presenter, "color", (slayer3d_color){255, 255, 255, 255}))
                              : json_color(presenter, "color", (slayer3d_color){255, 255, 255, 255});
        if (!callback(userdata, &text))
            return false;
    }
    return true;
}

static bool for_each_ui_menu_root(const slayer3d_game_data_runtime *runtime, const scene_entry *scene,
                                  const slayer3d_game_data_ui_metrics *metrics, yyjson_val *root,
                                  slayer3d_game_data_ui_text_fn callback, void *userdata)
{
    yyjson_val *menus = obj_get(obj_get(root, "ui"), "menus");
    for (size_t i = 0; yyjson_is_arr(menus) && i < yyjson_arr_size(menus); ++i)
    {
        if (!for_each_ui_menu_presenter(runtime, scene, metrics, yyjson_arr_get(menus, i), callback, userdata))
            return false;
    }
    return true;
}

static bool ends_with_cstr(const char *value, const char *suffix)
{
    if (value == NULL || suffix == NULL)
        return false;
    const size_t value_len = SDL_strlen(value);
    const size_t suffix_len = SDL_strlen(suffix);
    return value_len >= suffix_len && SDL_strcmp(value + value_len - suffix_len, suffix) == 0;
}

static void retained_ui_compat_name(const char *id, bool text_name, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    buffer[0] = '\0';
    if (id == NULL)
        return;

    if (ends_with_cstr(id, ".button.popup"))
    {
        const size_t prefix_len = SDL_strlen(id) - SDL_strlen(".button.popup");
        SDL_snprintf(buffer, buffer_size, "%.*s.popup", (int)prefix_len, id);
        return;
    }

    const char *option_segment = SDL_strstr(id, ".button.option.");
    if (option_segment != NULL)
    {
        const size_t prefix_len = (size_t)(option_segment - id);
        SDL_snprintf(buffer, buffer_size, "%.*s.option.%s", (int)prefix_len, id,
                     option_segment + SDL_strlen(".button.option."));
        return;
    }

    if (text_name && ends_with_cstr(id, ".button"))
    {
        const size_t prefix_len = SDL_strlen(id) - SDL_strlen(".button");
        SDL_snprintf(buffer, buffer_size, "%.*s.label", (int)prefix_len, id);
        return;
    }

    SDL_strlcpy(buffer, id, buffer_size);
}

static bool retained_ui_text_is_left_aligned(const char *id)
{
    return id != NULL &&
           (ends_with_cstr(id, ".label") || ends_with_cstr(id, ".title") || SDL_strstr(id, ".console.line") != NULL);
}

static bool retained_ui_text_is_right_aligned(const char *id)
{
    return id != NULL &&
           (ends_with_cstr(id, ".value") || ends_with_cstr(id, ".toggle") || ends_with_cstr(id, ".placeholder"));
}

static slayer3d_game_data_ui_align retained_ui_text_align(const slayer3d_ui_layout_render_command *command)
{
    if (command != NULL)
    {
        if (command->text_align == SLAYER3D_UI_LAYOUT_TEXT_ALIGN_LEFT)
            return SLAYER3D_GAME_DATA_UI_ALIGN_LEFT;
        if (command->text_align == SLAYER3D_UI_LAYOUT_TEXT_ALIGN_CENTER)
            return SLAYER3D_GAME_DATA_UI_ALIGN_CENTER;
        if (command->text_align == SLAYER3D_UI_LAYOUT_TEXT_ALIGN_RIGHT)
            return SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT;
    }
    if (command != NULL && retained_ui_text_is_left_aligned(command->id))
        return SLAYER3D_GAME_DATA_UI_ALIGN_LEFT;
    if (command != NULL && retained_ui_text_is_right_aligned(command->id))
        return SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT;
    return SLAYER3D_GAME_DATA_UI_ALIGN_CENTER;
}

static bool retained_ui_text_from_layout_model(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_ui_layout_model *layout,
                                               slayer3d_game_data_ui_text_fn callback, void *userdata)
{
    if (layout == NULL)
        return false;

    bool ok = true;
    for (int i = 0; ok && i < slayer3d_ui_layout_render_command_count(layout); ++i)
    {
        const slayer3d_ui_layout_render_command *command = slayer3d_ui_layout_render_command_at(layout, i);
        if (command == NULL || command->text[0] == '\0' || command->type == SLAYER3D_UI_LAYOUT_NODE_PANEL ||
            command->type == SLAYER3D_UI_LAYOUT_NODE_TOOLBAR || command->type == SLAYER3D_UI_LAYOUT_NODE_ROW ||
            command->type == SLAYER3D_UI_LAYOUT_NODE_COLUMN || command->type == SLAYER3D_UI_LAYOUT_NODE_SPACER)
        {
            continue;
        }

        char name[SLAYER3D_UI_LAYOUT_ID_MAX];
        retained_ui_compat_name(command->id, true, name, sizeof(name));

        slayer3d_game_data_ui_text text;
        SDL_zero(text);
        text.name = name;
        text.font = command->font[0] != '\0' ? command->font : "font.editor_shell.ui";
        text.text = command->text;
        text.visible = "always";
        text.layer = command->layer;
        text.align = retained_ui_text_align(command);
        if (text.align == SLAYER3D_GAME_DATA_UI_ALIGN_LEFT)
        {
            text.x = command->rect.x + 8.0f;
            text.centered = false;
        }
        else if (text.align == SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT)
        {
            text.x = command->rect.x + command->rect.w - 8.0f;
            text.centered = false;
        }
        else
        {
            text.x = command->rect.x + command->rect.w * 0.5f;
            text.centered = true;
        }
        text.normalized = false;
        slayer3d_game_data_font_asset font_asset;
        const bool has_font_asset = slayer3d_game_data_get_font_asset(runtime, text.font, &font_asset);
        const float font_size = has_font_asset && font_asset.size > 0.0f ? font_asset.size : 14.0f;
        text.scale = command->text_scale > 0.0f ? command->text_scale : command->text_size / font_size;
        const float text_height = font_size * text.scale;
        text.y = command->rect.y + SDL_max((command->rect.h - text_height) * 0.5f, 0.0f);
        text.color = command->has_text_color
                         ? command->text_color
                         : (command->selected ? (slayer3d_color){255, 255, 255, 255} : command->role_text_color);
        text.has_clip_rect = command->has_clip_rect;
        text.clip_x = command->clip_rect.x;
        text.clip_y = command->clip_rect.y;
        text.clip_w = command->clip_rect.w;
        text.clip_h = command->clip_rect.h;
        text.clip_normalized = false;
        ok = callback(userdata, &text);
    }

    return ok;
}

static bool create_retained_ui_layout(const slayer3d_game_data_runtime *runtime,
                                      const slayer3d_game_data_ui_metrics *metrics,
                                      slayer3d_ui_layout_model **out_layout)
{
    if (out_layout == NULL)
        return false;
    *out_layout = NULL;

    slayer3d_ui_layout_model *layout = NULL;
    if (!slayer3d_ui_layout_create(&layout))
        return false;
    float viewport_w = 0.0f;
    float viewport_h = 0.0f;
    slayer3d_game_data_ui_viewport(runtime, &viewport_w, &viewport_h);
    if (!slayer3d_game_data_build_active_ui_widget_layout(runtime, viewport_w, viewport_h, metrics, layout))
    {
        slayer3d_ui_layout_destroy(layout);
        return false;
    }

    *out_layout = layout;
    return true;
}

bool slayer3d_game_data_for_each_ui_text_for_metrics(const slayer3d_game_data_runtime *runtime,
                                                     const slayer3d_game_data_ui_metrics *metrics,
                                                     slayer3d_game_data_ui_text_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    yyjson_val *roots[2];
    roots[0] = runtime_root(runtime);
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;

    for (int root_index = 0; root_index < 2; ++root_index)
        if (!for_each_authored_ui_text_root(roots[root_index], callback, userdata) ||
            !for_each_ui_inspector_text_root(runtime, metrics, roots[root_index], callback, userdata) ||
            !for_each_ui_menu_root(runtime, scene, metrics, roots[root_index], callback, userdata))
            return true;
    slayer3d_ui_layout_model *layout = game_data_prepare_active_ui_widget_layout(runtime, metrics);
    if (layout == NULL)
        return true;
    const bool retained_ok = retained_ui_text_from_layout_model(runtime, layout, callback, userdata);
    if (!retained_ok)
        return true;
    return true;
}

bool slayer3d_game_data_for_each_ui_text(const slayer3d_game_data_runtime *runtime,
                                         slayer3d_game_data_ui_text_fn callback, void *userdata)
{
    return slayer3d_game_data_for_each_ui_text_for_metrics(runtime, NULL, callback, userdata);
}

static void ui_image_from_json(yyjson_val *item, slayer3d_game_data_ui_image *image)
{
    if (image == NULL)
        return;
    SDL_zero(*image);
    image->name = json_string(item, "name", NULL);
    image->image = json_string(item, "image", NULL);
    image->visible = json_string(item, "visible", "always");
    image->layer = json_int(item, "layer", json_int(item, "z", 0));
    image->x = json_float(item, "x", 0.0f);
    image->y = json_float(item, "y", 0.0f);
    image->w = json_float(item, "w", json_float(item, "width", 0.0f));
    image->h = json_float(item, "h", json_float(item, "height", 0.0f));
    image->normalized = json_bool(item, "normalized", false);
    image->preserve_aspect = json_bool(item, "preserve_aspect", true);
    image->align = parse_ui_align(json_string(item, "align", NULL), SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
    image->valign = parse_ui_valign(json_string(item, "valign", NULL), SLAYER3D_GAME_DATA_UI_VALIGN_TOP);
    image->scale = json_float(item, "scale", 1.0f);
    image->color = json_color(item, "color", (slayer3d_color){255, 255, 255, 255});
    image->effect = parse_ui_image_effect(json_string(item, "effect", NULL));
    image->effect_speed = json_float(item, "effect_speed", 1.0f);
    image->scroll_y_key = json_string(item, "scroll_y_key", NULL);
    image->has_clip_rect = obj_get(item, "clip_rect") != NULL;
    yyjson_val *clip_rect = obj_get(item, "clip_rect");
    image->clip_x = json_float(clip_rect, "x", 0.0f);
    image->clip_y = json_float(clip_rect, "y", 0.0f);
    image->clip_w = json_float(clip_rect, "w", 0.0f);
    image->clip_h = json_float(clip_rect, "h", 0.0f);
    image->clip_normalized = json_bool(clip_rect, "normalized", false);
}

static void ui_rect_from_json(yyjson_val *item, slayer3d_game_data_ui_rect *rect)
{
    if (rect == NULL)
        return;
    SDL_zero(*rect);
    rect->name = json_string(item, "name", NULL);
    rect->visible = json_string(item, "visible", "always");
    rect->layer = json_int(item, "layer", json_int(item, "z", 0));
    rect->x = json_float(item, "x", 0.0f);
    rect->y = json_float(item, "y", 0.0f);
    rect->w = json_float(item, "w", json_float(item, "width", 0.0f));
    rect->h = json_float(item, "h", json_float(item, "height", 0.0f));
    rect->normalized = json_bool(item, "normalized", false);
    rect->align = parse_ui_align(json_string(item, "align", NULL), SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
    rect->valign = parse_ui_valign(json_string(item, "valign", NULL), SLAYER3D_GAME_DATA_UI_VALIGN_TOP);
    rect->scale = json_float(item, "scale", 1.0f);
    rect->color = json_color(item, "color", (slayer3d_color){255, 255, 255, 255});
    yyjson_val *alpha_source = obj_get(item, "alpha_source");
    rect->alpha_source_target = json_string(alpha_source, "target", NULL);
    rect->alpha_source_key = json_string(alpha_source, "key", NULL);
    rect->alpha_source_scale = json_float(alpha_source, "scale", 1.0f);
    rect->alpha_source_min = json_float(alpha_source, "min", 0.0f);
    rect->alpha_source_max = json_float(alpha_source, "max", 1.0f);
    if (rect->alpha_source_max < rect->alpha_source_min)
        rect->alpha_source_max = rect->alpha_source_min;
    rect->pulse_alpha = json_bool(item, "pulse_alpha", false);
    rect->pulse_rate = json_float(item, "pulse_rate", 1.0f);
    rect->pulse_min = json_float(item, "pulse_min", 0.5f);
    rect->pulse_max = json_float(item, "pulse_max", 1.0f);
    if (rect->pulse_max < rect->pulse_min)
        rect->pulse_max = rect->pulse_min;
}

static bool for_each_authored_ui_image_root(yyjson_val *root, slayer3d_game_data_ui_image_fn callback, void *userdata)
{
    yyjson_val *images = obj_get(obj_get(root, "ui"), "images");
    for (size_t i = 0; yyjson_is_arr(images) && i < yyjson_arr_size(images); ++i)
    {
        slayer3d_game_data_ui_image image;
        ui_image_from_json(yyjson_arr_get(images, i), &image);
        if (!callback(userdata, &image))
            return false;
    }
    return true;
}

static bool for_each_authored_ui_rect_root(yyjson_val *root, slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    yyjson_val *rects = obj_get(obj_get(root, "ui"), "rects");
    for (size_t i = 0; yyjson_is_arr(rects) && i < yyjson_arr_size(rects); ++i)
    {
        slayer3d_game_data_ui_rect rect;
        ui_rect_from_json(yyjson_arr_get(rects, i), &rect);
        if (!callback(userdata, &rect))
            return false;
    }
    return true;
}

static bool emit_ui_rect_from_values_layered(yyjson_val *source, const char *name, int layer, float x, float y, float w,
                                             float h, slayer3d_color color, slayer3d_game_data_ui_rect_fn callback,
                                             void *userdata)
{
    if (color.a == 0 || w <= 0.0f || h <= 0.0f)
        return true;

    slayer3d_game_data_ui_rect rect;
    SDL_zero(rect);
    rect.name = name;
    rect.visible = "always";
    rect.layer = layer;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    rect.normalized = json_bool(source, "normalized", false);
    rect.align = parse_ui_align(json_string(source, "align", NULL), SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
    rect.valign = parse_ui_valign(json_string(source, "valign", NULL), SLAYER3D_GAME_DATA_UI_VALIGN_TOP);
    rect.scale = json_float(source, "scale", 1.0f);
    rect.color = color;
    return callback(userdata, &rect);
}

static bool emit_ui_rect_from_values(yyjson_val *source, const char *name, float x, float y, float w, float h,
                                     slayer3d_color color, slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    const int layer = json_int(source, "layer", json_int(source, "z", 0));
    return emit_ui_rect_from_values_layered(source, name, layer, x, y, w, h, color, callback, userdata);
}

static bool emit_ui_rect_from_values_clipped_layered(yyjson_val *source, const char *name, int layer, float x, float y,
                                                     float w, float h, slayer3d_color color, bool has_clip_rect,
                                                     slayer3d_ui_layout_rect clip_rect,
                                                     slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    if (color.a == 0 || w <= 0.0f || h <= 0.0f)
        return true;

    slayer3d_game_data_ui_rect rect;
    SDL_zero(rect);
    rect.name = name;
    rect.visible = "always";
    rect.layer = layer;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    rect.normalized = json_bool(source, "normalized", false);
    rect.align = parse_ui_align(json_string(source, "align", NULL), SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
    rect.valign = parse_ui_valign(json_string(source, "valign", NULL), SLAYER3D_GAME_DATA_UI_VALIGN_TOP);
    rect.scale = json_float(source, "scale", 1.0f);
    rect.color = color;
    rect.has_clip_rect = has_clip_rect;
    rect.clip_x = clip_rect.x;
    rect.clip_y = clip_rect.y;
    rect.clip_w = clip_rect.w;
    rect.clip_h = clip_rect.h;
    rect.clip_normalized = false;
    return callback(userdata, &rect);
}

static bool emit_ui_panel_rects(const slayer3d_game_data_runtime *runtime, yyjson_val *panel,
                                slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    yyjson_val *active_if = obj_get(panel, "visible_if");
    if (active_if != NULL && !eval_data_condition(runtime, active_if, NULL))
        return true;

    const char *name = json_string(panel, "name", NULL);
    const float x = json_float(panel, "x", 0.0f);
    const float y = json_float(panel, "y", 0.0f);
    const float w = json_float(panel, "w", json_float(panel, "width", 0.0f));
    const float h = json_float(panel, "h", json_float(panel, "height", 0.0f));
    const slayer3d_color color = json_color(panel, "color", (slayer3d_color){16, 22, 32, 210});
    if (!emit_ui_rect_from_values(panel, name, x, y, w, h, color, callback, userdata))
        return false;

    const float border = json_float(panel, "border_thickness", 0.0f);
    const slayer3d_color border_color = json_color(panel, "border_color", (slayer3d_color){0, 0, 0, 0});
    if (border <= 0.0f || border_color.a == 0)
        return true;

    return emit_ui_rect_from_values(panel, name, x, y, w, border, border_color, callback, userdata) &&
           emit_ui_rect_from_values(panel, name, x, y + h - border, w, border, border_color, callback, userdata) &&
           emit_ui_rect_from_values(panel, name, x, y, border, h, border_color, callback, userdata) &&
           emit_ui_rect_from_values(panel, name, x + w - border, y, border, h, border_color, callback, userdata);
}

static bool for_each_ui_panel_rect_root(const slayer3d_game_data_runtime *runtime, yyjson_val *root,
                                        slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    yyjson_val *panels = obj_get(obj_get(root, "ui"), "panels");
    for (size_t i = 0; yyjson_is_arr(panels) && i < yyjson_arr_size(panels); ++i)
    {
        if (!emit_ui_panel_rects(runtime, yyjson_arr_get(panels, i), callback, userdata))
            return false;
    }
    return true;
}

static bool emit_retained_ui_rect_border(const char *name, const slayer3d_ui_layout_rect *rect, int layer, float border,
                                         slayer3d_color color, bool has_clip_rect, slayer3d_ui_layout_rect clip_rect,
                                         slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    if (name == NULL || rect == NULL || border <= 0.0f || color.a == 0)
        return true;
    return emit_ui_rect_from_values_clipped_layered(NULL, name, layer, rect->x, rect->y, rect->w, border, color,
                                                    has_clip_rect, clip_rect, callback, userdata) &&
           emit_ui_rect_from_values_clipped_layered(NULL, name, layer, rect->x, rect->y + rect->h - border, rect->w,
                                                    border, color, has_clip_rect, clip_rect, callback, userdata) &&
           emit_ui_rect_from_values_clipped_layered(NULL, name, layer, rect->x, rect->y, border, rect->h, color,
                                                    has_clip_rect, clip_rect, callback, userdata) &&
           emit_ui_rect_from_values_clipped_layered(NULL, name, layer, rect->x + rect->w - border, rect->y, border,
                                                    rect->h, color, has_clip_rect, clip_rect, callback, userdata);
}

static slayer3d_color retained_ui_command_fill(const slayer3d_ui_layout_render_command *command)
{
    if (command == NULL)
        return (slayer3d_color){0, 0, 0, 0};
    const bool control =
        command->type == SLAYER3D_UI_LAYOUT_NODE_BUTTON || command->type == SLAYER3D_UI_LAYOUT_NODE_DROPDOWN;
    if (command->selected && command->option_index < 0 && control)
    {
        return (slayer3d_color){38, 104, 56, 255};
    }
    if (command->has_fill_color)
        return command->fill_color;
    if (command->window)
        return (slayer3d_color){10, 15, 23, 255};
    if (command->popup)
        return (slayer3d_color){14, 20, 30, 248};
    if (command->option_index >= 0 && command->selected)
        return (slayer3d_color){36, 82, 136, 248};
    if (command->option_index >= 0 && command->hovered)
        return (slayer3d_color){54, 102, 166, 248};
    if (command->option_index >= 0)
        return (slayer3d_color){17, 24, 35, 244};
    if (control && command->active)
        return (slayer3d_color){42, 58, 78, 248};
    if (control && command->hovered)
        return (slayer3d_color){54, 102, 166, 248};
    if (command->type == SLAYER3D_UI_LAYOUT_NODE_TOOLBAR)
        return (slayer3d_color){7, 10, 17, 245};
    if (control)
        return (slayer3d_color){26, 35, 48, 242};
    if (command->type == SLAYER3D_UI_LAYOUT_NODE_PANEL)
        return (slayer3d_color){10, 15, 23, 228};
    return (slayer3d_color){0, 0, 0, 0};
}

static slayer3d_color retained_ui_command_border(const slayer3d_ui_layout_render_command *command)
{
    if (command == NULL)
        return (slayer3d_color){0, 0, 0, 0};
    if (command->selected)
        return (slayer3d_color){96, 255, 128, 255};
    if (command->has_border_color)
        return command->border_color;
    if (command->popup)
        return (slayer3d_color){105, 142, 178, 245};
    if (command->type == SLAYER3D_UI_LAYOUT_NODE_BUTTON || command->type == SLAYER3D_UI_LAYOUT_NODE_DROPDOWN ||
        command->type == SLAYER3D_UI_LAYOUT_NODE_TOOLBAR)
    {
        return (slayer3d_color){82, 115, 154, 235};
    }
    return (slayer3d_color){0, 0, 0, 0};
}

static bool retained_ui_selected_name(const char *id, char *buffer, size_t buffer_size)
{
    if (id == NULL || buffer == NULL || buffer_size == 0U || !ends_with_cstr(id, ".button"))
        return false;
    const size_t prefix_len = SDL_strlen(id) - SDL_strlen(".button");
    SDL_snprintf(buffer, buffer_size, "%.*s.selected", (int)prefix_len, id);
    return true;
}

static bool emit_retained_drag_indicator(const slayer3d_ui_layout_render_command *command,
                                         slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    if (command == NULL || callback == NULL)
        return false;

    if (command->hovered || command->active)
    {
        const slayer3d_color background =
            command->active ? (slayer3d_color){42, 58, 78, 235} : (slayer3d_color){54, 102, 166, 210};
        if (!emit_ui_rect_from_values_clipped_layered(NULL, command->id, command->layer, command->rect.x,
                                                      command->rect.y, command->rect.w, command->rect.h, background,
                                                      command->has_clip_rect, command->clip_rect, callback, userdata))
        {
            return false;
        }
    }

    const float bar_width = SDL_max(SDL_min(command->rect.w - 8.0f, 14.0f), 4.0f);
    const float bar_height = 2.0f;
    const float bar_gap = 3.0f;
    const float total_height = bar_height * 3.0f + bar_gap * 2.0f;
    const float x = command->rect.x + (command->rect.w - bar_width) * 0.5f;
    const float y = command->rect.y + (command->rect.h - total_height) * 0.5f;
    const slayer3d_color bar_color = command->hovered || command->active ? (slayer3d_color){225, 238, 255, 255}
                                                                         : (slayer3d_color){126, 148, 176, 235};
    for (int i = 0; i < 3; ++i)
    {
        char name[SLAYER3D_UI_LAYOUT_ID_MAX];
        SDL_snprintf(name, sizeof(name), "%s.bar.%d", command->id, i);
        if (!emit_ui_rect_from_values_clipped_layered(
                NULL, name, command->layer + 1, x, y + (bar_height + bar_gap) * (float)i, bar_width, bar_height,
                bar_color, command->has_clip_rect, command->clip_rect, callback, userdata))
        {
            return false;
        }
    }
    return true;
}

static bool emit_retained_resize_indicator(const slayer3d_ui_layout_render_command *command,
                                           slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    if (command == NULL || callback == NULL || command->resize_edge == SLAYER3D_UI_LAYOUT_RESIZE_EDGE_NONE)
        return false;

    const bool vertical = command->resize_edge == SLAYER3D_UI_LAYOUT_RESIZE_EDGE_LEFT ||
                          command->resize_edge == SLAYER3D_UI_LAYOUT_RESIZE_EDGE_RIGHT;
    slayer3d_ui_layout_rect rail = command->rect;
    if (vertical)
    {
        rail.w = command->active ? 3.0f : 2.0f;
        rail.x = command->rect.x + (command->rect.w - rail.w) * 0.5f;
    }
    else
    {
        rail.h = command->active ? 3.0f : 2.0f;
        rail.y = command->rect.y + (command->rect.h - rail.h) * 0.5f;
    }

    const slayer3d_color color = command->active    ? (slayer3d_color){225, 238, 255, 255}
                                 : command->hovered ? (slayer3d_color){105, 160, 218, 255}
                                                    : (slayer3d_color){82, 104, 130, 220};
    return emit_ui_rect_from_values_clipped_layered(NULL, command->id, command->layer, rail.x, rail.y, rail.w, rail.h,
                                                    color, command->has_clip_rect, command->clip_rect, callback,
                                                    userdata);
}

static bool retained_ui_rects_from_layout_model(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_ui_layout_model *layout,
                                                slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    if (runtime == NULL || layout == NULL)
        return false;
    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (input != NULL && slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
    {
        slayer3d_ui_layout_input_state ui_input;
        SDL_zero(ui_input);
        ui_input.pointer_x = mouse_x;
        ui_input.pointer_y = mouse_y;
        ui_input.primary_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
        ui_input.primary_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
        ui_input.primary_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
        (void)slayer3d_ui_layout_update_input(layout, &ui_input, NULL);
    }

    bool ok = true;
    for (int i = 0; ok && i < slayer3d_ui_layout_render_command_count(layout); ++i)
    {
        const slayer3d_ui_layout_render_command *command = slayer3d_ui_layout_render_command_at(layout, i);
        if (command == NULL || command->type == SLAYER3D_UI_LAYOUT_NODE_LABEL ||
            command->type == SLAYER3D_UI_LAYOUT_NODE_SPACER || command->type == SLAYER3D_UI_LAYOUT_NODE_ROW ||
            command->type == SLAYER3D_UI_LAYOUT_NODE_COLUMN || command->type == SLAYER3D_UI_LAYOUT_NODE_IMAGE)
        {
            continue;
        }
        if (command->type == SLAYER3D_UI_LAYOUT_NODE_DRAG_INDICATOR)
        {
            ok = emit_retained_drag_indicator(command, callback, userdata);
            continue;
        }
        if (command->type == SLAYER3D_UI_LAYOUT_NODE_RESIZE_INDICATOR)
        {
            ok = emit_retained_resize_indicator(command, callback, userdata);
            continue;
        }

        char name[SLAYER3D_UI_LAYOUT_ID_MAX];
        retained_ui_compat_name(command->id, false, name, sizeof(name));
        const slayer3d_color fill = retained_ui_command_fill(command);
        const slayer3d_color border = retained_ui_command_border(command);
        const float border_thickness =
            command->border_thickness > 0.0f ? command->border_thickness : (command->selected ? 3.0f : 1.0f);
        ok = emit_ui_rect_from_values_clipped_layered(NULL, name, command->layer, command->rect.x, command->rect.y,
                                                      command->rect.w, command->rect.h, fill, command->has_clip_rect,
                                                      command->clip_rect, callback, userdata) &&
             emit_retained_ui_rect_border(name, &command->rect, command->layer, border_thickness, border,
                                          command->has_clip_rect, command->clip_rect, callback, userdata);
        if (ok && command->selected)
        {
            char selected_name[SLAYER3D_UI_LAYOUT_ID_MAX];
            if (retained_ui_selected_name(command->id, selected_name, sizeof(selected_name)))
            {
                ok = emit_retained_ui_rect_border(selected_name, &command->rect, command->layer, 4.0f,
                                                  (slayer3d_color){96, 255, 128, 255}, command->has_clip_rect,
                                                  command->clip_rect, callback, userdata);
            }
        }
    }

    return ok;
}

static bool retained_ui_images_from_layout_model(const slayer3d_ui_layout_model *layout,
                                                 slayer3d_game_data_ui_image_fn callback, void *userdata)
{
    if (layout == NULL)
        return false;

    bool ok = true;
    for (int i = 0; ok && i < slayer3d_ui_layout_render_command_count(layout); ++i)
    {
        const slayer3d_ui_layout_render_command *command = slayer3d_ui_layout_render_command_at(layout, i);
        if (command == NULL || command->type != SLAYER3D_UI_LAYOUT_NODE_IMAGE || command->image[0] == '\0')
            continue;

        slayer3d_game_data_ui_image image;
        SDL_zero(image);
        image.name = command->id;
        image.image = command->image;
        image.visible = "always";
        image.layer = command->layer;
        image.x = command->rect.x;
        image.y = command->rect.y;
        image.w = command->rect.w;
        image.h = command->rect.h;
        image.preserve_aspect = command->preserve_aspect;
        image.align = SLAYER3D_GAME_DATA_UI_ALIGN_LEFT;
        image.valign = SLAYER3D_GAME_DATA_UI_VALIGN_TOP;
        image.scale = 1.0f;
        image.color = command->has_fill_color ? command->fill_color : (slayer3d_color){255, 255, 255, 255};
        image.has_clip_rect = command->has_clip_rect;
        image.clip_x = command->clip_rect.x;
        image.clip_y = command->clip_rect.y;
        image.clip_w = command->clip_rect.w;
        image.clip_h = command->clip_rect.h;
        image.clip_normalized = false;
        ok = callback(userdata, &image);
    }

    return ok;
}

static bool for_each_ui_inspector_rect_root(const slayer3d_game_data_runtime *runtime, yyjson_val *root,
                                            slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    yyjson_val *inspectors = obj_get(obj_get(root, "ui"), "inspectors");
    for (size_t i = 0; yyjson_is_arr(inspectors) && i < yyjson_arr_size(inspectors); ++i)
    {
        yyjson_val *inspector = yyjson_arr_get(inspectors, i);
        yyjson_val *active_if = obj_get(inspector, "visible_if");
        if (active_if != NULL && !eval_data_condition(runtime, active_if, NULL))
            continue;

        const bool normalized = json_bool(inspector, "normalized", false);
        const float x = json_float(inspector, "x", 0.0f);
        const float y = json_float(inspector, "y", 0.0f);
        const float w = json_float(inspector, "w", json_float(inspector, "width", normalized ? 0.25f : 280.0f));
        const float row_height = json_float(inspector, "row_height", normalized ? 0.032f : 24.0f);
        const float padding = json_float(inspector, "padding", normalized ? 0.012f : 10.0f);
        const size_t row_count = yyjson_arr_size(obj_get(inspector, "rows"));
        const bool has_title = json_string(inspector, "title", NULL) != NULL;
        const float total_rows = (float)row_count + (has_title ? 1.0f : 0.0f);
        const float h =
            json_float(inspector, "h", json_float(inspector, "height", padding * 2.0f + row_height * total_rows));
        const slayer3d_color color = json_color(inspector, "background_color", (slayer3d_color){10, 14, 20, 215});
        if (json_bool(inspector, "panel", true) &&
            !emit_ui_rect_from_values(inspector, json_string(inspector, "name", NULL), x, y, w, h, color, callback,
                                      userdata))
            return false;

        const slayer3d_color row_color = json_color(inspector, "row_color", (slayer3d_color){255, 255, 255, 0});
        if (row_color.a == 0)
            continue;
        const float row_y0 = y + padding + (has_title ? row_height : 0.0f);
        for (size_t row_index = 0; row_index < row_count; ++row_index)
        {
            if (!emit_ui_rect_from_values(inspector, json_string(inspector, "name", NULL), x + padding,
                                          row_y0 + row_height * (float)row_index, w - padding * 2.0f,
                                          SDL_max(1.0f, row_height - 1.0f), row_color, callback, userdata))
                return false;
        }
    }
    return true;
}

static bool emit_editor_lasso_rect(slayer3d_game_data_ui_rect_fn callback, void *userdata, const char *name, float x,
                                   float y, float w, float h, slayer3d_color color)
{
    if (w <= 0.0f || h <= 0.0f)
        return true;

    slayer3d_game_data_ui_rect rect;
    SDL_zero(rect);
    rect.name = name;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    rect.scale = 1.0f;
    rect.color = color;
    return callback(userdata, &rect);
}

static bool for_each_editor_lasso_rect(const slayer3d_game_data_runtime *runtime,
                                       slayer3d_game_data_ui_rect_fn callback, void *userdata, const char *state_prefix,
                                       const char *rect_prefix)
{
    if (runtime == NULL || callback == NULL || runtime->scene_state == NULL || state_prefix == NULL ||
        rect_prefix == NULL)
    {
        return true;
    }

    char key[128];
    SDL_snprintf(key, sizeof(key), "%s.active", state_prefix);
    if (!slayer3d_properties_get_bool(runtime->scene_state, key, false))
        return true;

    SDL_snprintf(key, sizeof(key), "%s.start_x", state_prefix);
    const float start_x = slayer3d_properties_get_float(runtime->scene_state, key, 0.0f);
    SDL_snprintf(key, sizeof(key), "%s.start_y", state_prefix);
    const float start_y = slayer3d_properties_get_float(runtime->scene_state, key, 0.0f);
    SDL_snprintf(key, sizeof(key), "%s.end_x", state_prefix);
    const float end_x = slayer3d_properties_get_float(runtime->scene_state, key, start_x);
    SDL_snprintf(key, sizeof(key), "%s.end_y", state_prefix);
    const float end_y = slayer3d_properties_get_float(runtime->scene_state, key, start_y);
    const float x = SDL_min(start_x, end_x);
    const float y = SDL_min(start_y, end_y);
    const float w = SDL_fabsf(end_x - start_x);
    const float h = SDL_fabsf(end_y - start_y);
    if (w < 1.0f || h < 1.0f)
        return true;

    SDL_snprintf(key, sizeof(key), "%s.additive", state_prefix);
    const bool additive = slayer3d_properties_get_bool(runtime->scene_state, key, false);
    const slayer3d_color fill = additive ? (slayer3d_color){80, 255, 160, 48} : (slayer3d_color){255, 224, 64, 42};
    const slayer3d_color border = additive ? (slayer3d_color){80, 255, 160, 235} : (slayer3d_color){255, 224, 64, 235};
    const float t = 2.0f;

    const bool edge_lasso = SDL_strcmp(rect_prefix, "ui.editor.edge_lasso") == 0;
    const char *fill_name = edge_lasso ? "ui.editor.edge_lasso.fill" : "ui.editor.vertex_lasso.fill";
    const char *top_name = edge_lasso ? "ui.editor.edge_lasso.top" : "ui.editor.vertex_lasso.top";
    const char *bottom_name = edge_lasso ? "ui.editor.edge_lasso.bottom" : "ui.editor.vertex_lasso.bottom";
    const char *left_name = edge_lasso ? "ui.editor.edge_lasso.left" : "ui.editor.vertex_lasso.left";
    const char *right_name = edge_lasso ? "ui.editor.edge_lasso.right" : "ui.editor.vertex_lasso.right";
    return emit_editor_lasso_rect(callback, userdata, fill_name, x, y, w, h, fill) &&
           emit_editor_lasso_rect(callback, userdata, top_name, x, y, w, t, border) &&
           emit_editor_lasso_rect(callback, userdata, bottom_name, x, y + h - t, w, t, border) &&
           emit_editor_lasso_rect(callback, userdata, left_name, x, y, t, h, border) &&
           emit_editor_lasso_rect(callback, userdata, right_name, x + w - t, y, t, h, border);
}

static bool for_each_editor_selection_lasso_rect(const slayer3d_game_data_runtime *runtime,
                                                 slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    return for_each_editor_lasso_rect(runtime, callback, userdata, "editor.vertex.lasso", "ui.editor.vertex_lasso") &&
           for_each_editor_lasso_rect(runtime, callback, userdata, "editor.edge.lasso", "ui.editor.edge_lasso");
}

bool slayer3d_game_data_for_each_ui_image(const slayer3d_game_data_runtime *runtime,
                                          slayer3d_game_data_ui_image_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    yyjson_val *roots[2];
    roots[0] = runtime_root(runtime);
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;

    for (int root_index = 0; root_index < 2; ++root_index)
        if (!for_each_authored_ui_image_root(roots[root_index], callback, userdata))
            return true;
    slayer3d_ui_layout_model *layout = NULL;
    if (!create_retained_ui_layout(runtime, NULL, &layout))
        return true;
    const bool retained_ok = retained_ui_images_from_layout_model(layout, callback, userdata);
    slayer3d_ui_layout_destroy(layout);
    if (!retained_ok)
        return true;
    return true;
}

bool slayer3d_game_data_for_each_ui_rect(const slayer3d_game_data_runtime *runtime,
                                         slayer3d_game_data_ui_rect_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    yyjson_val *roots[2];
    roots[0] = runtime_root(runtime);
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;

    for (int root_index = 0; root_index < 2; ++root_index)
        if (!for_each_authored_ui_rect_root(roots[root_index], callback, userdata) ||
            !for_each_ui_panel_rect_root(runtime, roots[root_index], callback, userdata) ||
            !for_each_ui_inspector_rect_root(runtime, roots[root_index], callback, userdata))
            return true;
    slayer3d_ui_layout_model *layout = NULL;
    if (!create_retained_ui_layout(runtime, NULL, &layout))
        return true;
    const bool retained_ok = retained_ui_rects_from_layout_model(runtime, layout, callback, userdata);
    slayer3d_ui_layout_destroy(layout);
    if (!retained_ok)
        return true;
    if (!for_each_editor_selection_lasso_rect(runtime, callback, userdata))
        return true;
    return true;
}

bool slayer3d_game_data_for_each_ui_layered(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_ui_metrics *metrics,
                                            slayer3d_game_data_ui_rect_fn rect_callback,
                                            slayer3d_game_data_ui_image_fn image_callback,
                                            slayer3d_game_data_ui_text_fn text_callback, void *userdata)
{
    if (runtime == NULL || rect_callback == NULL)
        return false;

    slayer3d_ui_layout_model *layout = game_data_prepare_active_ui_widget_layout(runtime, metrics);
    if (layout == NULL)
        return true;

    yyjson_val *roots[2];
    roots[0] = runtime_root(runtime);
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;

    bool ok = true;
    for (int root_index = 0; ok && root_index < 2; ++root_index)
    {
        ok = for_each_authored_ui_rect_root(roots[root_index], rect_callback, userdata) &&
             for_each_ui_panel_rect_root(runtime, roots[root_index], rect_callback, userdata) &&
             for_each_ui_inspector_rect_root(runtime, roots[root_index], rect_callback, userdata);
    }
    ok = ok && retained_ui_rects_from_layout_model(runtime, layout, rect_callback, userdata) &&
         for_each_editor_selection_lasso_rect(runtime, rect_callback, userdata);

    if (image_callback != NULL)
    {
        for (int root_index = 0; ok && root_index < 2; ++root_index)
            ok = for_each_authored_ui_image_root(roots[root_index], image_callback, userdata);
        ok = ok && retained_ui_images_from_layout_model(layout, image_callback, userdata);
    }

    if (text_callback != NULL)
    {
        for (int root_index = 0; ok && root_index < 2; ++root_index)
        {
            ok = for_each_authored_ui_text_root(roots[root_index], text_callback, userdata) &&
                 for_each_ui_inspector_text_root(runtime, metrics, roots[root_index], text_callback, userdata) &&
                 for_each_ui_menu_root(runtime, scene, metrics, roots[root_index], text_callback, userdata);
        }
        ok = ok && retained_ui_text_from_layout_model(runtime, layout, text_callback, userdata);
    }

    return ok;
}

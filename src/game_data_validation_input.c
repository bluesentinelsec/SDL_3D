/**
 * @file game_data_validation_input.c
 * @brief Input-related JSON game data validation.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/input.h"

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static bool is_non_empty_string(yyjson_val *object, const char *key)
{
    const char *value = json_string(object, key);
    return value != NULL && value[0] != '\0';
}

static bool validation_key_name_valid(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return false;
    if (SDL_strcmp(name, "UP") == 0 || SDL_strcmp(name, "DOWN") == 0 || SDL_strcmp(name, "LEFT") == 0 ||
        SDL_strcmp(name, "RIGHT") == 0 || SDL_strcmp(name, "RETURN") == 0 || SDL_strcmp(name, "ESCAPE") == 0 ||
        SDL_strcmp(name, "BACKSPACE") == 0 || SDL_strcmp(name, "DELETE") == 0 || SDL_strcmp(name, "COMMA") == 0 ||
        SDL_strcmp(name, "PERIOD") == 0)
    {
        return true;
    }
    if (SDL_strlen(name) == 1)
        return SDL_GetScancodeFromKey(SDL_GetKeyFromName(name), NULL) != SDL_SCANCODE_UNKNOWN;
    return SDL_GetScancodeFromName(name) != SDL_SCANCODE_UNKNOWN;
}

static bool validation_input_modifier_name_valid(const char *name)
{
    return name != NULL && (SDL_strcasecmp(name, "shift") == 0 || SDL_strcasecmp(name, "ctrl") == 0 ||
                            SDL_strcasecmp(name, "control") == 0 || SDL_strcasecmp(name, "alt") == 0 ||
                            SDL_strcasecmp(name, "option") == 0 || SDL_strcasecmp(name, "gui") == 0 ||
                            SDL_strcasecmp(name, "cmd") == 0 || SDL_strcasecmp(name, "command") == 0 ||
                            SDL_strcasecmp(name, "meta") == 0 || SDL_strcasecmp(name, "primary") == 0);
}

static int validation_input_modifier_mask(const char *name)
{
    if (name == NULL)
        return -1;
    if (SDL_strcasecmp(name, "shift") == 0)
        return SLAYER3D_INPUT_MOD_SHIFT;
    if (SDL_strcasecmp(name, "ctrl") == 0 || SDL_strcasecmp(name, "control") == 0)
        return SLAYER3D_INPUT_MOD_CTRL;
    if (SDL_strcasecmp(name, "alt") == 0 || SDL_strcasecmp(name, "option") == 0)
        return SLAYER3D_INPUT_MOD_ALT;
    if (SDL_strcasecmp(name, "gui") == 0 || SDL_strcasecmp(name, "cmd") == 0 || SDL_strcasecmp(name, "command") == 0 ||
        SDL_strcasecmp(name, "meta") == 0)
    {
        return SLAYER3D_INPUT_MOD_GUI;
    }
    if (SDL_strcasecmp(name, "primary") == 0)
        return SLAYER3D_INPUT_MOD_PRIMARY;
    return -1;
}

static bool validate_keyboard_modifier_mask(validation_context *ctx, yyjson_val *value, const char *path,
                                            const char *field, int *out_mask)
{
    int mask = SLAYER3D_INPUT_MOD_NONE;
    if (value == NULL)
    {
        if (out_mask != NULL)
            *out_mask = mask;
        return true;
    }

    if (yyjson_is_str(value))
    {
        const char *modifier = yyjson_get_str(value);
        if (!validation_input_modifier_name_valid(modifier))
            return validation_error(ctx, path, "%s contains unsupported modifier '%s'", field,
                                    modifier != NULL ? modifier : "<missing>");
        mask |= validation_input_modifier_mask(modifier);
    }
    else if (yyjson_is_arr(value))
    {
        for (size_t i = 0; i < yyjson_arr_size(value); ++i)
        {
            yyjson_val *entry = yyjson_arr_get(value, i);
            if (!yyjson_is_str(entry))
                return validation_error(ctx, path, "%s entries must be modifier strings", field);
            const char *modifier = yyjson_get_str(entry);
            if (!validation_input_modifier_name_valid(modifier))
                return validation_error(ctx, path, "%s contains unsupported modifier '%s'", field,
                                        modifier != NULL ? modifier : "<missing>");
            mask |= validation_input_modifier_mask(modifier);
        }
    }
    else
    {
        return validation_error(ctx, path, "%s must be a modifier string or array", field);
    }

    if (out_mask != NULL)
        *out_mask = mask;
    return true;
}

static bool validate_keyboard_modifiers(validation_context *ctx, yyjson_val *binding, const char *path)
{
    yyjson_val *modifiers = obj_get(binding, "modifiers");
    yyjson_val *required = obj_get(binding, "required_modifiers");
    yyjson_val *excluded = obj_get(binding, "excluded_modifiers");
    int required_mask = SLAYER3D_INPUT_MOD_NONE;
    int excluded_mask = SLAYER3D_INPUT_MOD_NONE;

    if (modifiers != NULL && required != NULL)
        return validation_error(ctx, path, "keyboard binding must not define both modifiers and required_modifiers");
    if (!validate_keyboard_modifier_mask(ctx, modifiers != NULL ? modifiers : required, path,
                                         "keyboard binding required_modifiers", &required_mask) ||
        !validate_keyboard_modifier_mask(ctx, excluded, path, "keyboard binding excluded_modifiers", &excluded_mask))
    {
        return false;
    }
    if ((required_mask & excluded_mask) != 0)
        return validation_error(ctx, path,
                                "keyboard binding required_modifiers and excluded_modifiers must not overlap");
    return true;
}

bool validation_mouse_button_name_valid(const char *name)
{
    return name != NULL &&
           (SDL_strcmp(name, "LEFT") == 0 || SDL_strcmp(name, "MIDDLE") == 0 || SDL_strcmp(name, "RIGHT") == 0 ||
            SDL_strcmp(name, "X1") == 0 || SDL_strcmp(name, "X2") == 0);
}

static bool validation_mouse_axis_name_valid(const char *name)
{
    return name != NULL && (SDL_strcmp(name, "x") == 0 || SDL_strcmp(name, "y") == 0 ||
                            SDL_strcmp(name, "wheel") == 0 || SDL_strcmp(name, "wheel_x") == 0);
}

static bool validation_gamepad_axis_name_valid(const char *name)
{
    return name != NULL && (SDL_strcmp(name, "left_x") == 0 || SDL_strcmp(name, "left_y") == 0 ||
                            SDL_strcmp(name, "right_x") == 0 || SDL_strcmp(name, "right_y") == 0 ||
                            SDL_strcmp(name, "left_trigger") == 0 || SDL_strcmp(name, "right_trigger") == 0);
}

static bool validation_gamepad_button_name_valid(const char *name)
{
    static const char *const valid[] = {
        "START",       "BACK",          "SOUTH",          "NORTH",        "EAST",          "WEST",         "LEFT_STICK",
        "RIGHT_STICK", "LEFT_SHOULDER", "RIGHT_SHOULDER", "DPAD_UP",      "DPAD_DOWN",     "DPAD_LEFT",    "DPAD_RIGHT",
        "GUIDE",       "MISC1",         "RIGHT_PADDLE1",  "LEFT_PADDLE1", "RIGHT_PADDLE2", "LEFT_PADDLE2", "TOUCHPAD"};
    for (size_t i = 0; name != NULL && i < SDL_arraysize(valid); ++i)
    {
        if (SDL_strcmp(name, valid[i]) == 0)
            return true;
    }
    return false;
}

static bool input_device_name_valid(const char *device)
{
    return SDL_strcmp(device != NULL ? device : "", "keyboard") == 0 ||
           SDL_strcmp(device != NULL ? device : "", "gamepad") == 0 ||
           SDL_strcmp(device != NULL ? device : "", "mouse") == 0;
}

bool collect_input_actions(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *contexts = obj_get(obj_get(root, "input"), "contexts");
    if (contexts == NULL)
        return true;
    if (!yyjson_is_arr(contexts))
        return validation_error(ctx, "$.input.contexts", "input contexts must be an array");

    for (size_t c = 0; c < yyjson_arr_size(contexts); ++c)
    {
        yyjson_val *context = yyjson_arr_get(contexts, c);
        yyjson_val *actions = obj_get(context, "actions");
        if (actions == NULL)
            continue;
        if (!yyjson_is_arr(actions))
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.input.contexts[%zu].actions", c);
            return validation_error(ctx, path, "input context actions must be an array");
        }
        for (size_t a = 0; a < yyjson_arr_size(actions); ++a)
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.input.contexts[%zu].actions[%zu]", c, a);
            yyjson_val *action = yyjson_arr_get(actions, a);
            if (!yyjson_is_obj(action))
                return validation_error(ctx, path, "input actions must be objects");
            const char *name = json_string(action, "name");
            if (name != NULL && SDL_strlen(name) >= SLAYER3D_INPUT_ACTION_NAME_MAX)
                return validation_error(ctx, path, "input action name must be shorter than %d bytes",
                                        SLAYER3D_INPUT_ACTION_NAME_MAX);
            if (!require_unique_name(ctx, &names->actions, "input action", name, path))
                return false;
        }
    }
    return true;
}

bool collect_input_assignment_sets(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *sets = obj_get(obj_get(root, "input"), "device_assignment_sets");
    if (sets == NULL)
        return true;
    if (!yyjson_is_arr(sets))
        return validation_error(ctx, "$.input.device_assignment_sets", "input device assignment sets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(sets); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        yyjson_val *set = yyjson_arr_get(sets, i);
        format_path(path, sizeof(path), "$.input.device_assignment_sets[%zu]", i);
        if (!yyjson_is_obj(set))
            return validation_error(ctx, path, "input device assignment sets must be objects");
        const char *name = json_string(set, "name");
        if (name != NULL && name[0] != '\0' &&
            !require_unique_name(ctx, &names->input_assignment_sets, "input device assignment set", name, path))
        {
            return false;
        }
    }
    return true;
}

bool collect_input_profiles(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *profiles = obj_get(obj_get(root, "input"), "profiles");
    if (profiles == NULL)
        return true;
    if (!yyjson_is_arr(profiles))
        return validation_error(ctx, "$.input.profiles", "input profiles must be an array");

    for (size_t i = 0; i < yyjson_arr_size(profiles); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        yyjson_val *profile = yyjson_arr_get(profiles, i);
        format_path(path, sizeof(path), "$.input.profiles[%zu]", i);
        if (!yyjson_is_obj(profile))
            return validation_error(ctx, path, "input profiles must be objects");
        const char *name = json_string(profile, "name");
        if (name != NULL && name[0] != '\0' &&
            !require_unique_name(ctx, &names->input_profiles, "input profile", name, path))
        {
            return false;
        }
    }
    return true;
}

bool validate_input_bindings(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *contexts = obj_get(obj_get(root, "input"), "contexts");
    for (size_t c = 0; yyjson_is_arr(contexts) && c < yyjson_arr_size(contexts); ++c)
    {
        yyjson_val *actions = obj_get(yyjson_arr_get(contexts, c), "actions");
        for (size_t a = 0; yyjson_is_arr(actions) && a < yyjson_arr_size(actions); ++a)
        {
            yyjson_val *action = yyjson_arr_get(actions, a);
            yyjson_val *bindings = obj_get(action, "bindings");
            if (bindings == NULL)
                continue;
            if (!yyjson_is_arr(bindings))
            {
                char path[PATH_BUFFER_SIZE];
                format_path(path, sizeof(path), "$.input.contexts[%zu].actions[%zu].bindings", c, a);
                return validation_error(ctx, path, "input action bindings must be an array");
            }

            for (size_t b = 0; b < yyjson_arr_size(bindings); ++b)
            {
                char path[PATH_BUFFER_SIZE];
                format_path(path, sizeof(path), "$.input.contexts[%zu].actions[%zu].bindings[%zu]", c, a, b);
                yyjson_val *binding = yyjson_arr_get(bindings, b);
                const char *device = json_string(binding, "device");
                if (SDL_strcmp(device != NULL ? device : "", "keyboard") == 0)
                {
                    if (!is_non_empty_string(binding, "key"))
                        return validation_error(ctx, path, "keyboard binding requires a non-empty key");
                    if (!validate_keyboard_modifiers(ctx, binding, path))
                        return false;
                }
                else if (SDL_strcmp(device != NULL ? device : "", "gamepad") == 0)
                {
                    if (!is_non_empty_string(binding, "axis") && !is_non_empty_string(binding, "button"))
                        return validation_error(ctx, path, "gamepad binding requires an axis or button");
                    yyjson_val *slot = obj_get(binding, "slot");
                    if (slot != NULL && (!yyjson_is_num(slot) || yyjson_get_sint(slot) < -1 ||
                                         yyjson_get_sint(slot) >= SLAYER3D_INPUT_MAX_GAMEPADS))
                        return validation_error(ctx, path, "gamepad binding slot must be -1 or a valid slot index");
                }
                else if (SDL_strcmp(device != NULL ? device : "", "mouse") == 0)
                {
                    if (!is_non_empty_string(binding, "axis") && !is_non_empty_string(binding, "button"))
                        return validation_error(ctx, path, "mouse binding requires an axis or button");
                }
                else
                {
                    return validation_error(ctx, path, "unsupported input binding device '%s'",
                                            device != NULL ? device : "<missing>");
                }
            }
        }
    }
    return true;
}

static bool validate_input_profile_binding(validation_context *ctx, yyjson_val *binding, const char *path,
                                           validation_names *names)
{
    if (!yyjson_is_obj(binding))
        return validation_error(ctx, path, "input profile bindings must be objects");
    const char *action = json_string(binding, "action");
    const char *device = json_string(binding, "device");
    if (!require_ref(ctx, &names->actions, "input action", action, path))
        return false;

    if (SDL_strcmp(device != NULL ? device : "", "keyboard") == 0)
    {
        const char *key = json_string(binding, "key");
        if (!validation_key_name_valid(key))
            return validation_error(ctx, path, "keyboard input profile binding requires a valid key");
        if (!validate_keyboard_modifiers(ctx, binding, path))
            return false;
    }
    else if (SDL_strcmp(device != NULL ? device : "", "gamepad") == 0)
    {
        const char *axis = json_string(binding, "axis");
        const char *button = json_string(binding, "button");
        if (axis == NULL && button == NULL)
            return validation_error(ctx, path, "gamepad input profile binding requires an axis or button");
        if (axis != NULL && !validation_gamepad_axis_name_valid(axis))
            return validation_error(ctx, path, "gamepad input profile binding requires a valid axis");
        if (button != NULL && !validation_gamepad_button_name_valid(button))
            return validation_error(ctx, path, "gamepad input profile binding requires a valid button");
        yyjson_val *slot = obj_get(binding, "slot");
        if (slot != NULL && (!yyjson_is_num(slot) || yyjson_get_sint(slot) < -1 ||
                             yyjson_get_sint(slot) >= SLAYER3D_INPUT_MAX_GAMEPADS))
        {
            return validation_error(ctx, path, "gamepad input profile binding slot must be -1 or a valid slot index");
        }
    }
    else if (SDL_strcmp(device != NULL ? device : "", "mouse") == 0)
    {
        const char *axis = json_string(binding, "axis");
        const char *button = json_string(binding, "button");
        if (axis == NULL && button == NULL)
            return validation_error(ctx, path, "mouse input profile binding requires an axis or button");
        if (axis != NULL && !validation_mouse_axis_name_valid(axis))
            return validation_error(ctx, path, "mouse input profile binding requires a valid axis");
        if (button != NULL && !validation_mouse_button_name_valid(button))
            return validation_error(ctx, path, "mouse input profile binding requires a valid button");
    }
    else
    {
        return validation_error(ctx, path, "unsupported input profile binding device '%s'",
                                device != NULL ? device : "<missing>");
    }
    return true;
}

yyjson_val *slayer3d_game_data_find_input_assignment_set_json(yyjson_val *root, const char *set_name)
{
    yyjson_val *sets = obj_get(obj_get(root, "input"), "device_assignment_sets");
    for (size_t i = 0; set_name != NULL && yyjson_is_arr(sets) && i < yyjson_arr_size(sets); ++i)
    {
        yyjson_val *set = yyjson_arr_get(sets, i);
        const char *name = json_string(set, "name");
        if (name != NULL && SDL_strcmp(name, set_name) == 0)
            return set;
    }
    return NULL;
}

static bool input_assignment_set_has_semantic(yyjson_val *set, const char *semantic)
{
    yyjson_val *bindings = obj_get(set, "bindings");
    for (size_t i = 0; semantic != NULL && yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        const char *binding_semantic = json_string(binding, "semantic");
        if (binding_semantic != NULL && SDL_strcmp(binding_semantic, semantic) == 0)
            return true;
    }
    return false;
}

static bool validate_input_assignment_set_binding(validation_context *ctx, yyjson_val *binding, const char *device,
                                                  const char *path)
{
    if (!yyjson_is_obj(binding))
        return validation_error(ctx, path, "input device assignment bindings must be objects");
    if (!is_non_empty_string(binding, "semantic"))
        return validation_error(ctx, path, "input device assignment binding requires a non-empty semantic");

    if (SDL_strcmp(device != NULL ? device : "", "keyboard") == 0)
    {
        if (!validation_key_name_valid(json_string(binding, "key")))
            return validation_error(ctx, path, "keyboard input device assignment binding requires a valid key");
        if (!validate_keyboard_modifiers(ctx, binding, path))
            return false;
    }
    else if (SDL_strcmp(device != NULL ? device : "", "gamepad") == 0)
    {
        const char *axis = json_string(binding, "axis");
        const char *button = json_string(binding, "button");
        if (axis == NULL && button == NULL)
            return validation_error(ctx, path, "gamepad input device assignment binding requires an axis or button");
        if (axis != NULL && !validation_gamepad_axis_name_valid(axis))
            return validation_error(ctx, path, "gamepad input device assignment binding requires a valid axis");
        if (button != NULL && !validation_gamepad_button_name_valid(button))
            return validation_error(ctx, path, "gamepad input device assignment binding requires a valid button");
    }
    else if (SDL_strcmp(device != NULL ? device : "", "mouse") == 0)
    {
        const char *axis = json_string(binding, "axis");
        const char *button = json_string(binding, "button");
        if (axis == NULL && button == NULL)
            return validation_error(ctx, path, "mouse input device assignment binding requires an axis or button");
        if (axis != NULL && !validation_mouse_axis_name_valid(axis))
            return validation_error(ctx, path, "mouse input device assignment binding requires a valid axis");
        if (button != NULL && !validation_mouse_button_name_valid(button))
            return validation_error(ctx, path, "mouse input device assignment binding requires a valid button");
    }
    else
    {
        return validation_error(ctx, path, "unsupported input device assignment device '%s'",
                                device != NULL ? device : "<missing>");
    }
    return true;
}

bool validate_input_assignment_sets(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *sets = obj_get(obj_get(root, "input"), "device_assignment_sets");
    if (sets == NULL)
        return true;
    if (!yyjson_is_arr(sets))
        return validation_error(ctx, "$.input.device_assignment_sets", "input device assignment sets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(sets); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        yyjson_val *set = yyjson_arr_get(sets, i);
        format_path(path, sizeof(path), "$.input.device_assignment_sets[%zu]", i);
        if (!yyjson_is_obj(set))
            return validation_error(ctx, path, "input device assignment sets must be objects");
        const char *device = json_string(set, "device");
        if (!is_non_empty_string(set, "name"))
            return validation_error(ctx, path, "input device assignment set requires a non-empty name");
        if (!input_device_name_valid(device))
            return validation_error(ctx, path, "input device assignment set has unsupported device '%s'",
                                    device != NULL ? device : "<missing>");

        yyjson_val *bindings = obj_get(set, "bindings");
        if (!yyjson_is_arr(bindings) || yyjson_arr_size(bindings) == 0)
            return validation_error(ctx, path, "input device assignment set requires non-empty bindings");
        for (size_t b = 0; b < yyjson_arr_size(bindings); ++b)
        {
            char binding_path[PATH_BUFFER_SIZE];
            format_path(binding_path, sizeof(binding_path), "%s.bindings[%zu]", path, b);
            if (!validate_input_assignment_set_binding(ctx, yyjson_arr_get(bindings, b), device, binding_path))
                return false;
        }
    }
    return true;
}

static bool validate_input_profile_assignment(validation_context *ctx, yyjson_val *root, yyjson_val *assignment,
                                              const char *path, validation_names *names)
{
    if (!yyjson_is_obj(assignment))
        return validation_error(ctx, path, "input profile assignments must be objects");
    const char *set_name = json_string(assignment, "set");
    if (!require_ref(ctx, &names->input_assignment_sets, "input device assignment set", set_name, path))
        return false;

    yyjson_val *set = slayer3d_game_data_find_input_assignment_set_json(root, set_name);
    const char *device = json_string(set, "device");
    yyjson_val *slot = obj_get(assignment, "slot");
    if (slot != NULL && SDL_strcmp(device != NULL ? device : "", "gamepad") != 0)
    {
        return validation_error(ctx, path, "input profile assignment slot is only valid for gamepad assignment sets");
    }
    if (slot != NULL &&
        (!yyjson_is_num(slot) || yyjson_get_sint(slot) < -1 || yyjson_get_sint(slot) >= SLAYER3D_INPUT_MAX_GAMEPADS))
    {
        return validation_error(ctx, path, "input profile assignment slot must be -1 or a valid slot index");
    }

    yyjson_val *actions = obj_get(assignment, "actions");
    if (!yyjson_is_obj(actions))
        return validation_error(ctx, path, "input profile assignment requires an actions object");

    yyjson_val *bindings = obj_get(set, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        char action_path[PATH_BUFFER_SIZE];
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        const char *semantic = json_string(binding, "semantic");
        const char *action = semantic != NULL ? json_string(actions, semantic) : NULL;
        format_path(action_path, sizeof(action_path), "%s.actions.%s", path, semantic != NULL ? semantic : "<missing>");
        if (!require_ref(ctx, &names->actions, "input action", action, action_path))
            return false;
    }
    size_t idx, max;
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_foreach(actions, idx, max, key, value)
    {
        char action_path[PATH_BUFFER_SIZE];
        const char *semantic = yyjson_is_str(key) ? yyjson_get_str(key) : NULL;
        const char *action = yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
        format_path(action_path, sizeof(action_path), "%s.actions.%s", path, semantic != NULL ? semantic : "<invalid>");
        if (!input_assignment_set_has_semantic(set, semantic))
            return validation_error(ctx, action_path, "input profile assignment maps unknown semantic '%s'",
                                    semantic != NULL ? semantic : "<invalid>");
        if (!require_ref(ctx, &names->actions, "input action", action, action_path))
            return false;
    }
    return true;
}

bool validate_input_profiles(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *profiles = obj_get(obj_get(root, "input"), "profiles");
    if (profiles == NULL)
        return true;
    if (!yyjson_is_arr(profiles))
        return validation_error(ctx, "$.input.profiles", "input profiles must be an array");

    name_table profile_names = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(profiles); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        yyjson_val *profile = yyjson_arr_get(profiles, i);
        format_path(path, sizeof(path), "$.input.profiles[%zu]", i);
        if (!yyjson_is_obj(profile))
        {
            ok = validation_error(ctx, path, "input profiles must be objects");
            break;
        }
        ok = require_unique_name(ctx, &profile_names, "input profile", json_string(profile, "name"), path);

        yyjson_val *min_gamepads = obj_get(profile, "min_gamepads");
        yyjson_val *max_gamepads = obj_get(profile, "max_gamepads");
        if (ok && min_gamepads != NULL &&
            (!yyjson_is_num(min_gamepads) || yyjson_get_sint(min_gamepads) < 0 ||
             yyjson_get_sint(min_gamepads) > SLAYER3D_INPUT_MAX_GAMEPADS))
        {
            ok = validation_error(ctx, path, "input profile min_gamepads must be a valid gamepad count");
        }
        if (ok && max_gamepads != NULL &&
            (!yyjson_is_num(max_gamepads) || yyjson_get_sint(max_gamepads) < 0 ||
             yyjson_get_sint(max_gamepads) > SLAYER3D_INPUT_MAX_GAMEPADS))
        {
            ok = validation_error(ctx, path, "input profile max_gamepads must be a valid gamepad count");
        }
        if (ok && yyjson_is_num(min_gamepads) && yyjson_is_num(max_gamepads) &&
            yyjson_get_sint(min_gamepads) > yyjson_get_sint(max_gamepads))
        {
            ok = validation_error(ctx, path, "input profile min_gamepads cannot exceed max_gamepads");
        }
        if (ok && obj_get(profile, "active_if") != NULL)
        {
            char condition_path[PATH_BUFFER_SIZE];
            format_path(condition_path, sizeof(condition_path), "%s.active_if", path);
            ok = validate_data_condition(ctx, obj_get(profile, "active_if"), condition_path, names);
        }

        yyjson_val *unbind = obj_get(profile, "unbind");
        if (ok && unbind != NULL && !yyjson_is_arr(unbind))
            ok = validation_error(ctx, path, "input profile unbind must be an array");
        for (size_t u = 0; ok && yyjson_is_arr(unbind) && u < yyjson_arr_size(unbind); ++u)
        {
            char item_path[PATH_BUFFER_SIZE];
            format_path(item_path, sizeof(item_path), "%s.unbind[%zu]", path, u);
            ok =
                require_ref(ctx, &names->actions, "input action", yyjson_get_str(yyjson_arr_get(unbind, u)), item_path);
        }

        yyjson_val *bindings = obj_get(profile, "bindings");
        if (ok && bindings != NULL && !yyjson_is_arr(bindings))
            ok = validation_error(ctx, path, "input profile bindings must be an array");
        for (size_t b = 0; ok && yyjson_is_arr(bindings) && b < yyjson_arr_size(bindings); ++b)
        {
            char binding_path[PATH_BUFFER_SIZE];
            format_path(binding_path, sizeof(binding_path), "%s.bindings[%zu]", path, b);
            ok = validate_input_profile_binding(ctx, yyjson_arr_get(bindings, b), binding_path, names);
        }

        yyjson_val *assignments = obj_get(profile, "assignments");
        if (ok && bindings != NULL && assignments != NULL)
            ok = validation_error(ctx, path, "input profile cannot mix bindings and assignments");
        if (ok && assignments != NULL && !yyjson_is_arr(assignments))
            ok = validation_error(ctx, path, "input profile assignments must be an array");
        for (size_t a = 0; ok && yyjson_is_arr(assignments) && a < yyjson_arr_size(assignments); ++a)
        {
            char assignment_path[PATH_BUFFER_SIZE];
            format_path(assignment_path, sizeof(assignment_path), "%s.assignments[%zu]", path, a);
            ok = validate_input_profile_assignment(ctx, root, yyjson_arr_get(assignments, a), assignment_path, names);
        }
    }
    name_table_destroy(&profile_names);
    return ok;
}

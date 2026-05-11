#include "slayer3d/input.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <stdint.h>

#include "mouse_trace_internal.h"

#define SLAYER3D_INPUT_DEFAULT_DEADZONE 0.15f
#define SLAYER3D_INPUT_MAX_MOUSE_BUTTONS 16
#define SLAYER3D_INPUT_DEMO_MAGIC "SLAYER3DEMO"
#define SLAYER3D_INPUT_DEMO_MAGIC_SIZE 8
#define SLAYER3D_INPUT_DEMO_VERSION 3U
#define SLAYER3D_INPUT_DEMO_ACTION_COUNT SLAYER3D_INPUT_MAX_ACTIONS
#define SLAYER3D_INPUT_DEMO_FLAG_PRESSED 0x01U
#define SLAYER3D_INPUT_DEMO_FLAG_RELEASED 0x02U
#define SLAYER3D_INPUT_DEMO_FLAG_HELD 0x04U

typedef struct slayer3d_input_action_entry
{
    char name[SLAYER3D_INPUT_ACTION_NAME_MAX];
    bool registered;
} slayer3d_input_action_entry;

struct slayer3d_demo_recorder
{
    slayer3d_input_manager *input;
    slayer3d_input_snapshot *snapshots;
    Uint32 count;
    Uint32 capacity;
    bool recording;
};

struct slayer3d_demo_player
{
    slayer3d_input_snapshot *snapshots;
    Uint32 count;
    Uint32 cursor;
    float tick_rate;
};

typedef struct slayer3d_input_gamepad_slot
{
    SDL_Gamepad *gamepad;
    SDL_JoystickID id;
    bool connected;
    bool button_down[SDL_GAMEPAD_BUTTON_COUNT];
    bool button_pressed_this_frame[SDL_GAMEPAD_BUTTON_COUNT];
    bool button_released_this_frame[SDL_GAMEPAD_BUTTON_COUNT];
    float axis_value[SDL_GAMEPAD_AXIS_COUNT];
} slayer3d_input_gamepad_slot;

struct slayer3d_input_manager
{
    slayer3d_input_action_entry actions[SLAYER3D_INPUT_MAX_ACTIONS];
    int action_count;

    slayer3d_input_binding bindings[SLAYER3D_INPUT_MAX_BINDINGS];
    int binding_count;

    float mouse_dx_accum;
    float mouse_dy_accum;
    float mouse_wheel_x_accum;
    float mouse_wheel_y_accum;
    bool discard_mouse_motion_until_update;
    bool discard_next_mouse_motion;
    bool discarded_mouse_motion_since_request;

    bool key_down[SDL_SCANCODE_COUNT];
    int key_down_modifiers[SDL_SCANCODE_COUNT];
    bool key_pressed_this_frame[SDL_SCANCODE_COUNT];
    bool key_released_this_frame[SDL_SCANCODE_COUNT];
    int key_pressed_modifiers_this_frame[SDL_SCANCODE_COUNT];
    int key_released_modifiers_this_frame[SDL_SCANCODE_COUNT];
    bool mouse_down[SLAYER3D_INPUT_MAX_MOUSE_BUTTONS];
    bool mouse_pressed_this_frame[SLAYER3D_INPUT_MAX_MOUSE_BUTTONS];
    bool mouse_released_this_frame[SLAYER3D_INPUT_MAX_MOUSE_BUTTONS];
    slayer3d_input_gamepad_slot gamepads[SLAYER3D_INPUT_MAX_GAMEPADS];
    int gamepad_count;

    slayer3d_input_snapshot snapshot;
    bool prev_held[SLAYER3D_INPUT_MAX_ACTIONS];
    SDL_Scancode pressed_scancode;
    Uint8 pressed_mouse_button;
    SDL_GamepadButton pressed_gamepad_button;
    char pending_text_input[SLAYER3D_INPUT_TEXT_MAX];
    char current_text_input[SLAYER3D_INPUT_TEXT_MAX];

    float deadzone;

    bool action_override_active[SLAYER3D_INPUT_MAX_ACTIONS];
    float action_override_value[SLAYER3D_INPUT_MAX_ACTIONS];

    slayer3d_demo_recorder *recorder;
    slayer3d_demo_player *demo_player;
};

static bool slayer3d_input_action_id_valid(int action_id)
{
    return action_id >= 0 && action_id < SLAYER3D_INPUT_MAX_ACTIONS;
}

static int slayer3d_input_modifier_mask_from_sdl(SDL_Keymod mod)
{
    int mask = SLAYER3D_INPUT_MOD_NONE;

    if ((mod & SDL_KMOD_SHIFT) != 0)
    {
        mask |= SLAYER3D_INPUT_MOD_SHIFT;
    }
    if ((mod & SDL_KMOD_CTRL) != 0)
    {
        mask |= SLAYER3D_INPUT_MOD_CTRL;
    }
    if ((mod & SDL_KMOD_ALT) != 0)
    {
        mask |= SLAYER3D_INPUT_MOD_ALT;
    }
    if ((mod & SDL_KMOD_GUI) != 0)
    {
        mask |= SLAYER3D_INPUT_MOD_GUI;
    }

    return mask;
}

static bool slayer3d_input_modifiers_match(int current_modifiers, int required_modifiers, int excluded_modifiers)
{
    return ((current_modifiers & required_modifiers) == required_modifiers) &&
           ((current_modifiers & excluded_modifiers) == 0);
}

static float slayer3d_input_absf(float value)
{
    return value < 0.0f ? -value : value;
}

static float slayer3d_input_clampf(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static float slayer3d_input_signed_max_magnitude(float current, float candidate)
{
    return slayer3d_input_absf(candidate) > slayer3d_input_absf(current) ? candidate : current;
}

static bool slayer3d_input_binding_valid(const slayer3d_input_manager *input, int action_id)
{
    return input != NULL && slayer3d_input_action_id_valid(action_id) && action_id < input->action_count &&
           input->actions[action_id].registered;
}

static bool slayer3d_input_gamepad_index_valid(int gamepad_index)
{
    return gamepad_index >= 0 && gamepad_index < SLAYER3D_INPUT_MAX_GAMEPADS;
}

static void slayer3d_input_add_binding(slayer3d_input_manager *input, const slayer3d_input_binding *binding)
{
    if (input == NULL || binding == NULL || !slayer3d_input_binding_valid(input, binding->action_id) ||
        input->binding_count >= SLAYER3D_INPUT_MAX_BINDINGS)
    {
        return;
    }

    input->bindings[input->binding_count++] = *binding;
}

static slayer3d_input_gamepad_slot *slayer3d_input_find_gamepad_slot(slayer3d_input_manager *input,
                                                                     SDL_JoystickID joystick_id)
{
    if (input == NULL)
    {
        return NULL;
    }

    for (int i = 0; i < SLAYER3D_INPUT_MAX_GAMEPADS; ++i)
    {
        slayer3d_input_gamepad_slot *slot = &input->gamepads[i];
        if (slot->connected && slot->id == joystick_id)
        {
            return slot;
        }
    }
    return NULL;
}

static slayer3d_input_gamepad_slot *slayer3d_input_find_free_gamepad_slot(slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return NULL;
    }

    for (int i = 0; i < SLAYER3D_INPUT_MAX_GAMEPADS; ++i)
    {
        slayer3d_input_gamepad_slot *slot = &input->gamepads[i];
        if (!slot->connected)
        {
            return slot;
        }
    }
    return NULL;
}

static const slayer3d_input_gamepad_slot *slayer3d_input_gamepad_slot_at(const slayer3d_input_manager *input,
                                                                         int gamepad_index);

static bool slayer3d_input_slot_is_open(const slayer3d_input_gamepad_slot *slot)
{
    return slot != NULL && slot->connected;
}

static void slayer3d_input_sync_gamepad_slot(slayer3d_input_gamepad_slot *slot);

static void slayer3d_input_close_gamepad(slayer3d_input_manager *input, slayer3d_input_gamepad_slot *slot)
{
    if (input == NULL || slot == NULL || !slot->connected)
    {
        return;
    }

    const int slot_index = (int)(slot - input->gamepads);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D gamepad slot closing: slot=%d id=%d", slot_index, slot->id);
    if (slot->gamepad != NULL)
    {
        SDL_CloseGamepad(slot->gamepad);
    }
    SDL_zero(*slot);
    if (input->gamepad_count > 0)
    {
        input->gamepad_count--;
    }
}

static bool slayer3d_input_open_gamepad_slot(slayer3d_input_manager *input, SDL_JoystickID joystick_id)
{
    slayer3d_input_gamepad_slot *slot;
    if (input == NULL)
    {
        return false;
    }

    slot = slayer3d_input_find_gamepad_slot(input, joystick_id);
    if (slot == NULL)
    {
        slot = slayer3d_input_find_free_gamepad_slot(input);
    }
    if (slot == NULL)
    {
        return false;
    }

    if (slot->connected && slot->id == joystick_id && slot->gamepad != NULL)
    {
        return true;
    }

    if (!slot->connected)
    {
        SDL_zero(*slot);
        slot->connected = true;
        slot->id = joystick_id;
        input->gamepad_count++;
    }

    if (slot->gamepad != NULL)
    {
        SDL_CloseGamepad(slot->gamepad);
        slot->gamepad = NULL;
    }
    slot->gamepad = SDL_OpenGamepad(joystick_id);
    if (slot->gamepad != NULL)
    {
        slayer3d_input_sync_gamepad_slot(slot);
    }
    const char *name = slot->gamepad != NULL ? SDL_GetGamepadName(slot->gamepad) : NULL;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D gamepad slot open: slot=%d id=%d name=%s connected=%d",
                (int)(slot - input->gamepads), joystick_id, name != NULL ? name : "<unopened>",
                slot->gamepad != NULL ? 1 : 0);
    return true;
}

static void slayer3d_input_refresh_gamepads(slayer3d_input_manager *input)
{
    int count = 0;
    SDL_JoystickID *gamepads;
    if (input == NULL)
    {
        return;
    }

    gamepads = SDL_GetGamepads(&count);
    if (gamepads == NULL)
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        if (input->gamepad_count >= SLAYER3D_INPUT_MAX_GAMEPADS)
        {
            break;
        }
        slayer3d_input_gamepad_slot *slot = slayer3d_input_find_gamepad_slot(input, gamepads[i]);
        if (slot == NULL || slot->gamepad == NULL)
        {
            (void)slayer3d_input_open_gamepad_slot(input, gamepads[i]);
        }
    }

    SDL_free(gamepads);
}

static float slayer3d_input_gamepad_slot_axis_value(const slayer3d_input_gamepad_slot *slot, SDL_GamepadAxis axis,
                                                    float deadzone)
{
    if (!slayer3d_input_slot_is_open(slot) || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
    {
        return 0.0f;
    }
    float value = slot->axis_value[axis];
    if (slayer3d_input_absf(value) < deadzone)
    {
        return 0.0f;
    }

    return value;
}

static void slayer3d_input_sync_gamepad_slot(slayer3d_input_gamepad_slot *slot)
{
    if (slot == NULL || slot->gamepad == NULL)
    {
        return;
    }

    for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
    {
        slot->button_down[button] = SDL_GetGamepadButton(slot->gamepad, (SDL_GamepadButton)button) != 0;
    }
    for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
    {
        float value = (float)SDL_GetGamepadAxis(slot->gamepad, (SDL_GamepadAxis)axis) / 32767.0f;
        if (value < -1.0f)
        {
            value = -1.0f;
        }
        else if (value > 1.0f)
        {
            value = 1.0f;
        }
        slot->axis_value[axis] = value;
    }
}

static float slayer3d_input_gamepad_axis_value(const slayer3d_input_manager *input, SDL_GamepadAxis axis)
{
    if (input == NULL)
    {
        return 0.0f;
    }

    float value = 0.0f;
    for (int i = 0; i < SLAYER3D_INPUT_MAX_GAMEPADS; ++i)
    {
        const slayer3d_input_gamepad_slot *slot = &input->gamepads[i];
        if (!slot->connected)
        {
            continue;
        }
        value = slayer3d_input_signed_max_magnitude(
            value, slayer3d_input_gamepad_slot_axis_value(slot, axis, input->deadzone));
    }
    return value;
}

static float slayer3d_input_gamepad_axis_value_at(const slayer3d_input_manager *input, int gamepad_index,
                                                  SDL_GamepadAxis axis)
{
    const slayer3d_input_gamepad_slot *slot = slayer3d_input_gamepad_slot_at(input, gamepad_index);
    if (slot == NULL)
    {
        return 0.0f;
    }
    return slayer3d_input_gamepad_slot_axis_value(slot, axis,
                                                  input != NULL ? input->deadzone : SLAYER3D_INPUT_DEFAULT_DEADZONE);
}

static bool slayer3d_input_mouse_button_valid(Uint8 button)
{
    return button < SLAYER3D_INPUT_MAX_MOUSE_BUTTONS;
}

static bool slayer3d_input_gamepad_button_valid(SDL_GamepadButton button)
{
    return button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT;
}

static bool slayer3d_input_gamepad_axis_valid(SDL_GamepadAxis axis)
{
    return axis >= 0 && axis < SDL_GAMEPAD_AXIS_COUNT;
}

static bool slayer3d_input_gamepad_slot_button_down(const slayer3d_input_gamepad_slot *slot, SDL_GamepadButton button)
{
    return slayer3d_input_slot_is_open(slot) && slayer3d_input_gamepad_button_valid(button) &&
           slot->button_down[button];
}

static bool slayer3d_input_gamepad_slot_button_pressed(const slayer3d_input_gamepad_slot *slot,
                                                       SDL_GamepadButton button)
{
    return slayer3d_input_slot_is_open(slot) && slayer3d_input_gamepad_button_valid(button) &&
           slot->button_pressed_this_frame[button];
}

static bool slayer3d_input_gamepad_slot_button_released(const slayer3d_input_gamepad_slot *slot,
                                                        SDL_GamepadButton button)
{
    return slayer3d_input_slot_is_open(slot) && slayer3d_input_gamepad_button_valid(button) &&
           slot->button_released_this_frame[button];
}

static float slayer3d_input_mouse_axis_value(const slayer3d_input_manager *input, slayer3d_mouse_axis axis)
{
    if (input == NULL)
    {
        return 0.0f;
    }

    switch (axis)
    {
    case SLAYER3D_MOUSE_AXIS_X:
        return input->mouse_dx_accum;
    case SLAYER3D_MOUSE_AXIS_Y:
        return input->mouse_dy_accum;
    case SLAYER3D_MOUSE_AXIS_WHEEL:
        return input->mouse_wheel_y_accum;
    case SLAYER3D_MOUSE_AXIS_WHEEL_X:
        return input->mouse_wheel_x_accum;
    default:
        return 0.0f;
    }
}

static bool slayer3d_input_has_opposite_axis_binding(const slayer3d_input_manager *input,
                                                     const slayer3d_input_binding *binding)
{
    if (input == NULL || binding == NULL ||
        (binding->source != SLAYER3D_INPUT_MOUSE_AXIS && binding->source != SLAYER3D_INPUT_GAMEPAD_AXIS))
    {
        return false;
    }

    for (int i = 0; i < input->binding_count; ++i)
    {
        const slayer3d_input_binding *other = &input->bindings[i];
        if (other == binding || other->action_id == binding->action_id || other->source != binding->source)
        {
            continue;
        }

        if (binding->source == SLAYER3D_INPUT_MOUSE_AXIS && other->mouse_axis == binding->mouse_axis &&
            other->scale * binding->scale < 0.0f)
        {
            return true;
        }
        if (binding->source == SLAYER3D_INPUT_GAMEPAD_AXIS && other->gamepad_axis == binding->gamepad_axis &&
            other->gamepad_index == binding->gamepad_index && other->scale * binding->scale < 0.0f)
        {
            return true;
        }
    }

    return false;
}

static float slayer3d_input_axis_binding_value(const slayer3d_input_manager *input,
                                               const slayer3d_input_binding *binding, float raw_value)
{
    float value = raw_value * binding->scale;
    if (slayer3d_input_has_opposite_axis_binding(input, binding) && value < 0.0f)
    {
        return 0.0f;
    }
    return value;
}

static float slayer3d_input_binding_value(const slayer3d_input_manager *input, const slayer3d_input_binding *binding)
{
    if (input == NULL || binding == NULL)
    {
        return 0.0f;
    }

    switch (binding->source)
    {
    case SLAYER3D_INPUT_KEYBOARD:
        if (binding->scancode < 0 || binding->scancode >= SDL_SCANCODE_COUNT)
        {
            return 0.0f;
        }
        if (!slayer3d_input_modifiers_match(input->key_down_modifiers[binding->scancode], binding->required_modifiers,
                                            binding->excluded_modifiers))
        {
            return 0.0f;
        }
        return input->key_down[binding->scancode] ? binding->scale : 0.0f;
    case SLAYER3D_INPUT_MOUSE_BUTTON:
        if (!slayer3d_input_mouse_button_valid(binding->mouse_button))
        {
            return 0.0f;
        }
        return input->mouse_down[binding->mouse_button] ? binding->scale : 0.0f;
    case SLAYER3D_INPUT_MOUSE_AXIS:
        return slayer3d_input_axis_binding_value(input, binding,
                                                 slayer3d_input_mouse_axis_value(input, binding->mouse_axis));
    case SLAYER3D_INPUT_GAMEPAD_BUTTON:
        if (!slayer3d_input_gamepad_button_valid(binding->gamepad_button))
        {
            return 0.0f;
        }
        for (int i = 0; i < SLAYER3D_INPUT_MAX_GAMEPADS; ++i)
        {
            if (binding->gamepad_index >= 0 && i != binding->gamepad_index)
            {
                continue;
            }
            if (slayer3d_input_gamepad_slot_button_down(&input->gamepads[i], binding->gamepad_button))
            {
                return binding->scale;
            }
        }
        return 0.0f;
    case SLAYER3D_INPUT_GAMEPAD_AXIS:
        return slayer3d_input_axis_binding_value(
            input, binding,
            binding->gamepad_index >= 0
                ? slayer3d_input_gamepad_axis_value_at(input, binding->gamepad_index, binding->gamepad_axis)
                : slayer3d_input_gamepad_axis_value(input, binding->gamepad_axis));
    default:
        return 0.0f;
    }
}

static bool slayer3d_input_binding_pressed(const slayer3d_input_manager *input, const slayer3d_input_binding *binding)
{
    if (input == NULL || binding == NULL)
    {
        return false;
    }

    switch (binding->source)
    {
    case SLAYER3D_INPUT_KEYBOARD:
        if (binding->scancode < 0 || binding->scancode >= SDL_SCANCODE_COUNT)
        {
            return false;
        }
        return slayer3d_input_modifiers_match(input->key_pressed_modifiers_this_frame[binding->scancode],
                                              binding->required_modifiers, binding->excluded_modifiers) &&
               input->key_pressed_this_frame[binding->scancode];
    case SLAYER3D_INPUT_MOUSE_BUTTON:
        return slayer3d_input_mouse_button_valid(binding->mouse_button) &&
               input->mouse_pressed_this_frame[binding->mouse_button];
    case SLAYER3D_INPUT_GAMEPAD_BUTTON:
        if (!slayer3d_input_gamepad_button_valid(binding->gamepad_button))
        {
            return false;
        }
        for (int i = 0; i < SLAYER3D_INPUT_MAX_GAMEPADS; ++i)
        {
            if (binding->gamepad_index >= 0 && i != binding->gamepad_index)
            {
                continue;
            }
            if (slayer3d_input_gamepad_slot_button_pressed(&input->gamepads[i], binding->gamepad_button))
            {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

static bool slayer3d_input_binding_released(const slayer3d_input_manager *input, const slayer3d_input_binding *binding)
{
    if (input == NULL || binding == NULL)
    {
        return false;
    }

    switch (binding->source)
    {
    case SLAYER3D_INPUT_KEYBOARD:
        return binding->scancode >= 0 && binding->scancode < SDL_SCANCODE_COUNT &&
               slayer3d_input_modifiers_match(input->key_released_modifiers_this_frame[binding->scancode],
                                              binding->required_modifiers, binding->excluded_modifiers) &&
               input->key_released_this_frame[binding->scancode];
    case SLAYER3D_INPUT_MOUSE_BUTTON:
        return slayer3d_input_mouse_button_valid(binding->mouse_button) &&
               input->mouse_released_this_frame[binding->mouse_button];
    case SLAYER3D_INPUT_GAMEPAD_BUTTON:
        if (!slayer3d_input_gamepad_button_valid(binding->gamepad_button))
        {
            return false;
        }
        for (int i = 0; i < SLAYER3D_INPUT_MAX_GAMEPADS; ++i)
        {
            if (binding->gamepad_index >= 0 && i != binding->gamepad_index)
            {
                continue;
            }
            if (slayer3d_input_gamepad_slot_button_released(&input->gamepads[i], binding->gamepad_button))
            {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

static void slayer3d_input_reset_transients(slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return;
    }

    input->mouse_dx_accum = 0.0f;
    input->mouse_dy_accum = 0.0f;
    input->mouse_wheel_x_accum = 0.0f;
    input->mouse_wheel_y_accum = 0.0f;
    input->pending_text_input[0] = '\0';
    SDL_memset(input->key_pressed_this_frame, 0, sizeof(input->key_pressed_this_frame));
    SDL_memset(input->key_released_this_frame, 0, sizeof(input->key_released_this_frame));
    SDL_memset(input->key_pressed_modifiers_this_frame, 0, sizeof(input->key_pressed_modifiers_this_frame));
    SDL_memset(input->key_released_modifiers_this_frame, 0, sizeof(input->key_released_modifiers_this_frame));
    SDL_memset(input->mouse_pressed_this_frame, 0, sizeof(input->mouse_pressed_this_frame));
    SDL_memset(input->mouse_released_this_frame, 0, sizeof(input->mouse_released_this_frame));
    for (int i = 0; i < SLAYER3D_INPUT_MAX_GAMEPADS; ++i)
    {
        SDL_memset(input->gamepads[i].button_pressed_this_frame, 0,
                   sizeof(input->gamepads[i].button_pressed_this_frame));
        SDL_memset(input->gamepads[i].button_released_this_frame, 0,
                   sizeof(input->gamepads[i].button_released_this_frame));
    }
}

static void slayer3d_input_append_text(slayer3d_input_manager *input, const char *text)
{
    if (input == NULL || text == NULL || text[0] == '\0')
    {
        return;
    }

    const size_t current = SDL_strlen(input->pending_text_input);
    if (current + 1U >= sizeof(input->pending_text_input))
    {
        return;
    }
    SDL_strlcpy(input->pending_text_input + current, text, sizeof(input->pending_text_input) - current);
}

static bool slayer3d_input_physical_any_pressed(const slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return false;
    }

    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i)
    {
        if (input->key_pressed_this_frame[i])
        {
            return true;
        }
    }
    for (int i = 0; i < SLAYER3D_INPUT_MAX_MOUSE_BUTTONS; ++i)
    {
        if (input->mouse_pressed_this_frame[i])
        {
            return true;
        }
    }
    for (int slot_index = 0; slot_index < SLAYER3D_INPUT_MAX_GAMEPADS; ++slot_index)
    {
        const slayer3d_input_gamepad_slot *slot = &input->gamepads[slot_index];
        if (!slot->connected)
        {
            continue;
        }
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
        {
            if (slot->button_pressed_this_frame[i])
            {
                return true;
            }
        }
    }
    return false;
}

static SDL_Scancode slayer3d_input_first_pressed_scancode(const slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return SDL_SCANCODE_UNKNOWN;
    }

    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i)
    {
        if (input->key_pressed_this_frame[i])
        {
            return (SDL_Scancode)i;
        }
    }
    return SDL_SCANCODE_UNKNOWN;
}

static SDL_GamepadButton slayer3d_input_first_pressed_gamepad_button(const slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return SDL_GAMEPAD_BUTTON_INVALID;
    }

    for (int slot_index = 0; slot_index < SLAYER3D_INPUT_MAX_GAMEPADS; ++slot_index)
    {
        const slayer3d_input_gamepad_slot *slot = &input->gamepads[slot_index];
        if (!slot->connected)
        {
            continue;
        }
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
        {
            if (slot->button_pressed_this_frame[i])
            {
                return (SDL_GamepadButton)i;
            }
        }
    }
    return SDL_GAMEPAD_BUTTON_INVALID;
}

static Uint8 slayer3d_input_first_pressed_mouse_button(const slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return 0;
    }

    for (int i = 0; i < SLAYER3D_INPUT_MAX_MOUSE_BUTTONS; ++i)
    {
        if (input->mouse_pressed_this_frame[i])
        {
            return (Uint8)i;
        }
    }
    return 0;
}

static bool slayer3d_demo_recorder_append(slayer3d_demo_recorder *recorder, const slayer3d_input_snapshot *snapshot)
{
    slayer3d_input_snapshot *grown;
    Uint32 next_capacity;
    size_t bytes;

    if (recorder == NULL || snapshot == NULL || !recorder->recording)
    {
        return true;
    }

    if (recorder->count >= recorder->capacity)
    {
        next_capacity = recorder->capacity == 0 ? 64U : recorder->capacity * 2U;
        bytes = (size_t)next_capacity * sizeof(*recorder->snapshots);
        grown = (slayer3d_input_snapshot *)SDL_realloc(recorder->snapshots, bytes);
        if (grown == NULL)
        {
            return false;
        }
        recorder->snapshots = grown;
        recorder->capacity = next_capacity;
    }

    recorder->snapshots[recorder->count++] = *snapshot;
    return true;
}

static bool slayer3d_input_write_all(SDL_IOStream *stream, const void *data, size_t size)
{
    return stream != NULL && data != NULL && SDL_WriteIO(stream, data, size) == size;
}

static bool slayer3d_input_read_all(SDL_IOStream *stream, void *data, size_t size)
{
    return stream != NULL && data != NULL && SDL_ReadIO(stream, data, size) == size;
}

static bool slayer3d_input_write_u8(SDL_IOStream *stream, Uint8 value)
{
    return slayer3d_input_write_all(stream, &value, sizeof(value));
}

static bool slayer3d_input_read_u8(SDL_IOStream *stream, Uint8 *out_value)
{
    return slayer3d_input_read_all(stream, out_value, sizeof(*out_value));
}

static bool slayer3d_input_write_u32_le(SDL_IOStream *stream, Uint32 value)
{
    Uint8 bytes[4];
    bytes[0] = (Uint8)(value & 0xffU);
    bytes[1] = (Uint8)((value >> 8U) & 0xffU);
    bytes[2] = (Uint8)((value >> 16U) & 0xffU);
    bytes[3] = (Uint8)((value >> 24U) & 0xffU);
    return slayer3d_input_write_all(stream, bytes, sizeof(bytes));
}

static bool slayer3d_input_read_u32_le(SDL_IOStream *stream, Uint32 *out_value)
{
    Uint8 bytes[4];
    if (out_value == NULL || !slayer3d_input_read_all(stream, bytes, sizeof(bytes)))
    {
        return false;
    }

    *out_value = (Uint32)bytes[0] | ((Uint32)bytes[1] << 8U) | ((Uint32)bytes[2] << 16U) | ((Uint32)bytes[3] << 24U);
    return true;
}

static bool slayer3d_input_write_i32_le(SDL_IOStream *stream, int value)
{
    return slayer3d_input_write_u32_le(stream, (Uint32)(Sint32)value);
}

static bool slayer3d_input_read_i32_le(SDL_IOStream *stream, int *out_value)
{
    Uint32 bits;
    if (out_value == NULL || !slayer3d_input_read_u32_le(stream, &bits))
    {
        return false;
    }

    *out_value = (int)(Sint32)bits;
    return true;
}

static bool slayer3d_input_write_f32_le(SDL_IOStream *stream, float value)
{
    Uint32 bits;
    SDL_memcpy(&bits, &value, sizeof(bits));
    return slayer3d_input_write_u32_le(stream, bits);
}

static bool slayer3d_input_read_f32_le(SDL_IOStream *stream, float *out_value)
{
    Uint32 bits;
    if (out_value == NULL || !slayer3d_input_read_u32_le(stream, &bits))
    {
        return false;
    }

    SDL_memcpy(out_value, &bits, sizeof(bits));
    return true;
}

static Uint8 slayer3d_input_demo_flags_from_action(const slayer3d_action_state *action)
{
    Uint8 flags = 0U;
    if (action->pressed)
    {
        flags |= SLAYER3D_INPUT_DEMO_FLAG_PRESSED;
    }
    if (action->released)
    {
        flags |= SLAYER3D_INPUT_DEMO_FLAG_RELEASED;
    }
    if (action->held)
    {
        flags |= SLAYER3D_INPUT_DEMO_FLAG_HELD;
    }
    return flags;
}

static void slayer3d_input_demo_flags_to_action(Uint8 flags, slayer3d_action_state *action)
{
    action->pressed = (flags & SLAYER3D_INPUT_DEMO_FLAG_PRESSED) != 0;
    action->released = (flags & SLAYER3D_INPUT_DEMO_FLAG_RELEASED) != 0;
    action->held = (flags & SLAYER3D_INPUT_DEMO_FLAG_HELD) != 0;
}

static bool slayer3d_demo_write_header(SDL_IOStream *stream, float tick_rate, Uint32 tick_count)
{
    return slayer3d_input_write_all(stream, SLAYER3D_INPUT_DEMO_MAGIC, SLAYER3D_INPUT_DEMO_MAGIC_SIZE) &&
           slayer3d_input_write_u32_le(stream, SLAYER3D_INPUT_DEMO_VERSION) &&
           slayer3d_input_write_f32_le(stream, tick_rate) && slayer3d_input_write_u32_le(stream, tick_count) &&
           slayer3d_input_write_u32_le(stream, SLAYER3D_INPUT_DEMO_ACTION_COUNT);
}

static bool slayer3d_demo_read_header(SDL_IOStream *stream, float *out_tick_rate, Uint32 *out_tick_count,
                                      Uint32 *out_action_count)
{
    char magic[SLAYER3D_INPUT_DEMO_MAGIC_SIZE];
    Uint32 version;

    if (!slayer3d_input_read_all(stream, magic, sizeof(magic)) ||
        SDL_memcmp(magic, SLAYER3D_INPUT_DEMO_MAGIC, SLAYER3D_INPUT_DEMO_MAGIC_SIZE) != 0 ||
        !slayer3d_input_read_u32_le(stream, &version) || version != SLAYER3D_INPUT_DEMO_VERSION ||
        !slayer3d_input_read_f32_le(stream, out_tick_rate) || !slayer3d_input_read_u32_le(stream, out_tick_count) ||
        !slayer3d_input_read_u32_le(stream, out_action_count) || *out_action_count != SLAYER3D_INPUT_DEMO_ACTION_COUNT)
    {
        SDL_SetError("Invalid SLAYER3D demo file.");
        return false;
    }

    return true;
}

static bool slayer3d_demo_write_snapshot(SDL_IOStream *stream, const slayer3d_input_snapshot *snapshot)
{
    if (stream == NULL || snapshot == NULL)
    {
        return false;
    }

    if (!slayer3d_input_write_i32_le(stream, snapshot->tick) ||
        !slayer3d_input_write_f32_le(stream, snapshot->mouse_dx) ||
        !slayer3d_input_write_f32_le(stream, snapshot->mouse_dy) ||
        !slayer3d_input_write_u8(stream, snapshot->any_pressed ? 1U : 0U))
    {
        return false;
    }

    for (int i = 0; i < SLAYER3D_INPUT_DEMO_ACTION_COUNT; ++i)
    {
        if (!slayer3d_input_write_u8(stream, slayer3d_input_demo_flags_from_action(&snapshot->actions[i])) ||
            !slayer3d_input_write_f32_le(stream, snapshot->actions[i].value))
        {
            return false;
        }
    }

    return true;
}

static bool slayer3d_demo_read_snapshot(SDL_IOStream *stream, slayer3d_input_snapshot *snapshot)
{
    if (stream == NULL || snapshot == NULL)
    {
        return false;
    }

    SDL_zero(*snapshot);
    Uint8 any_pressed = 0U;
    if (!slayer3d_input_read_i32_le(stream, &snapshot->tick) ||
        !slayer3d_input_read_f32_le(stream, &snapshot->mouse_dx) ||
        !slayer3d_input_read_f32_le(stream, &snapshot->mouse_dy) || !slayer3d_input_read_u8(stream, &any_pressed))
    {
        return false;
    }
    snapshot->any_pressed = any_pressed != 0U;

    for (int i = 0; i < SLAYER3D_INPUT_DEMO_ACTION_COUNT; ++i)
    {
        Uint8 flags;
        if (!slayer3d_input_read_u8(stream, &flags) || !slayer3d_input_read_f32_le(stream, &snapshot->actions[i].value))
        {
            return false;
        }
        slayer3d_input_demo_flags_to_action(flags, &snapshot->actions[i]);
    }

    return true;
}

slayer3d_input_manager *slayer3d_input_create(void)
{
    slayer3d_input_manager *input = (slayer3d_input_manager *)SDL_calloc(1, sizeof(*input));
    if (input == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    input->deadzone = SLAYER3D_INPUT_DEFAULT_DEADZONE;
    input->pressed_scancode = SDL_SCANCODE_UNKNOWN;
    input->pressed_gamepad_button = SDL_GAMEPAD_BUTTON_INVALID;
    slayer3d_mouse_tracef("input.create", "input=%p deadzone=%.3f", (void *)input, input->deadzone);
    slayer3d_input_refresh_gamepads(input);
    return input;
}

void slayer3d_input_destroy(slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return;
    }

    if (input->recorder != NULL)
    {
        input->recorder->input = NULL;
        input->recorder->recording = false;
    }
    for (int i = 0; i < SLAYER3D_INPUT_MAX_GAMEPADS; ++i)
    {
        slayer3d_input_close_gamepad(input, &input->gamepads[i]);
    }
    SDL_free(input);
}

int slayer3d_input_register_action(slayer3d_input_manager *input, const char *name)
{
    size_t length;
    int existing;

    if (input == NULL || name == NULL || name[0] == '\0')
    {
        return -1;
    }

    existing = slayer3d_input_find_action(input, name);
    if (existing >= 0)
    {
        return existing;
    }

    length = SDL_strlen(name);
    if (length >= SLAYER3D_INPUT_ACTION_NAME_MAX || input->action_count >= SLAYER3D_INPUT_MAX_ACTIONS)
    {
        return -1;
    }

    int action_id = input->action_count++;
    SDL_strlcpy(input->actions[action_id].name, name, sizeof(input->actions[action_id].name));
    input->actions[action_id].registered = true;
    return action_id;
}

int slayer3d_input_find_action(const slayer3d_input_manager *input, const char *name)
{
    if (input == NULL || name == NULL)
    {
        return -1;
    }

    for (int i = 0; i < input->action_count; ++i)
    {
        if (input->actions[i].registered && SDL_strcmp(input->actions[i].name, name) == 0)
        {
            return i;
        }
    }

    return -1;
}

void slayer3d_input_bind_key(slayer3d_input_manager *input, int action_id, SDL_Scancode key)
{
    slayer3d_input_bind_key_mod_mask(input, action_id, key, SLAYER3D_INPUT_MOD_NONE, SLAYER3D_INPUT_MOD_NONE);
}

void slayer3d_input_bind_key_mod(slayer3d_input_manager *input, int action_id, SDL_Scancode key, int required_modifiers)
{
    slayer3d_input_bind_key_mod_mask(input, action_id, key, required_modifiers, SLAYER3D_INPUT_MOD_NONE);
}

void slayer3d_input_bind_key_mod_mask(slayer3d_input_manager *input, int action_id, SDL_Scancode key,
                                      int required_modifiers, int excluded_modifiers)
{
    slayer3d_input_binding binding;
    SDL_zero(binding);
    binding.action_id = action_id;
    binding.source = SLAYER3D_INPUT_KEYBOARD;
    binding.gamepad_index = -1;
    binding.required_modifiers = required_modifiers;
    binding.excluded_modifiers = excluded_modifiers;
    binding.scancode = key;
    binding.scale = 1.0f;
    slayer3d_input_add_binding(input, &binding);
}

void slayer3d_input_bind_mouse_button(slayer3d_input_manager *input, int action_id, Uint8 button)
{
    slayer3d_input_binding binding;
    SDL_zero(binding);
    binding.action_id = action_id;
    binding.source = SLAYER3D_INPUT_MOUSE_BUTTON;
    binding.gamepad_index = -1;
    binding.mouse_button = button;
    binding.scale = 1.0f;
    slayer3d_input_add_binding(input, &binding);
}

void slayer3d_input_bind_mouse_axis(slayer3d_input_manager *input, int action_id, slayer3d_mouse_axis axis, float scale)
{
    slayer3d_input_binding binding;
    SDL_zero(binding);
    binding.action_id = action_id;
    binding.source = SLAYER3D_INPUT_MOUSE_AXIS;
    binding.gamepad_index = -1;
    binding.mouse_axis = axis;
    binding.scale = scale;
    slayer3d_input_add_binding(input, &binding);
}

void slayer3d_input_bind_gamepad_button(slayer3d_input_manager *input, int action_id, SDL_GamepadButton button)
{
    slayer3d_input_bind_gamepad_button_at(input, action_id, -1, button);
}

void slayer3d_input_bind_gamepad_button_at(slayer3d_input_manager *input, int action_id, int gamepad_index,
                                           SDL_GamepadButton button)
{
    slayer3d_input_binding binding;
    SDL_zero(binding);
    binding.action_id = action_id;
    binding.source = SLAYER3D_INPUT_GAMEPAD_BUTTON;
    binding.gamepad_index = slayer3d_input_gamepad_index_valid(gamepad_index) ? gamepad_index : -1;
    binding.gamepad_button = button;
    binding.scale = 1.0f;
    slayer3d_input_add_binding(input, &binding);
}

void slayer3d_input_bind_gamepad_axis(slayer3d_input_manager *input, int action_id, SDL_GamepadAxis axis, float scale)
{
    slayer3d_input_bind_gamepad_axis_at(input, action_id, -1, axis, scale);
}

void slayer3d_input_bind_gamepad_axis_at(slayer3d_input_manager *input, int action_id, int gamepad_index,
                                         SDL_GamepadAxis axis, float scale)
{
    slayer3d_input_binding binding;
    SDL_zero(binding);
    binding.action_id = action_id;
    binding.source = SLAYER3D_INPUT_GAMEPAD_AXIS;
    binding.gamepad_index = slayer3d_input_gamepad_index_valid(gamepad_index) ? gamepad_index : -1;
    binding.gamepad_axis = axis;
    binding.scale = scale;
    slayer3d_input_add_binding(input, &binding);
}

void slayer3d_input_unbind_action(slayer3d_input_manager *input, int action_id)
{
    if (input == NULL)
    {
        return;
    }

    for (int i = 0; i < input->binding_count;)
    {
        if (input->bindings[i].action_id == action_id)
        {
            input->bindings[i] = input->bindings[input->binding_count - 1];
            input->binding_count--;
        }
        else
        {
            i++;
        }
    }
}

void slayer3d_input_set_action_override(slayer3d_input_manager *input, int action_id, float value)
{
    if (input == NULL || !slayer3d_input_action_id_valid(action_id) || action_id >= input->action_count ||
        !input->actions[action_id].registered)
    {
        return;
    }

    input->action_override_active[action_id] = true;
    input->action_override_value[action_id] = slayer3d_input_clampf(value, -1.0f, 1.0f);
}

void slayer3d_input_clear_action_override(slayer3d_input_manager *input, int action_id)
{
    if (input == NULL || !slayer3d_input_action_id_valid(action_id))
    {
        return;
    }

    input->action_override_active[action_id] = false;
    input->action_override_value[action_id] = 0.0f;
}

void slayer3d_input_clear_action_overrides(slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return;
    }

    SDL_memset(input->action_override_active, 0, sizeof(input->action_override_active));
    SDL_memset(input->action_override_value, 0, sizeof(input->action_override_value));
}

void slayer3d_input_process_event(slayer3d_input_manager *input, const SDL_Event *event)
{
    if (input == NULL || event == NULL)
    {
        return;
    }

    switch (event->type)
    {
    case SDL_EVENT_TEXT_INPUT:
        slayer3d_input_append_text(input, event->text.text);
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.scancode >= 0 && event->key.scancode < SDL_SCANCODE_COUNT && !event->key.repeat)
        {
            int modifiers = slayer3d_input_modifier_mask_from_sdl(event->key.mod) |
                            slayer3d_input_modifier_mask_from_sdl(SDL_GetModState());
            input->key_down[event->key.scancode] = true;
            input->key_down_modifiers[event->key.scancode] = modifiers;
            input->key_pressed_this_frame[event->key.scancode] = true;
            input->key_pressed_modifiers_this_frame[event->key.scancode] |= modifiers;
        }
        break;
    case SDL_EVENT_KEY_UP:
        if (event->key.scancode >= 0 && event->key.scancode < SDL_SCANCODE_COUNT)
        {
            int modifiers = input->key_down_modifiers[event->key.scancode] |
                            slayer3d_input_modifier_mask_from_sdl(event->key.mod) |
                            slayer3d_input_modifier_mask_from_sdl(SDL_GetModState());
            input->key_down[event->key.scancode] = false;
            input->key_down_modifiers[event->key.scancode] = SLAYER3D_INPUT_MOD_NONE;
            input->key_released_this_frame[event->key.scancode] = true;
            input->key_released_modifiers_this_frame[event->key.scancode] |= modifiers;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        const bool discard_motion = input->discard_mouse_motion_until_update || input->discard_next_mouse_motion;
        slayer3d_mouse_tracef(
            "input.event.mouse_motion",
            "input=%p x=%.3f y=%.3f xrel=%.3f yrel=%.3f accum_before=(%.3f,%.3f) discard_until_update=%d "
            "discard_next=%d",
            (void *)input, event->motion.x, event->motion.y, event->motion.xrel, event->motion.yrel,
            input->mouse_dx_accum, input->mouse_dy_accum, input->discard_mouse_motion_until_update ? 1 : 0,
            input->discard_next_mouse_motion ? 1 : 0);
        if (discard_motion)
        {
            input->discarded_mouse_motion_since_request = true;
            if (!input->discard_mouse_motion_until_update)
            {
                input->discard_next_mouse_motion = false;
            }
        }
        else
        {
            input->mouse_dx_accum += event->motion.xrel;
            input->mouse_dy_accum += event->motion.yrel;
        }
        slayer3d_mouse_tracef("input.event.mouse_motion.applied", "input=%p discarded=%d accum_after=(%.3f,%.3f)",
                              (void *)input, discard_motion ? 1 : 0, input->mouse_dx_accum, input->mouse_dy_accum);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        input->mouse_wheel_x_accum += event->wheel.x;
        input->mouse_wheel_y_accum += event->wheel.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (slayer3d_input_mouse_button_valid(event->button.button))
        {
            input->mouse_down[event->button.button] = true;
            input->mouse_pressed_this_frame[event->button.button] = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (slayer3d_input_mouse_button_valid(event->button.button))
        {
            input->mouse_down[event->button.button] = false;
            input->mouse_released_this_frame[event->button.button] = true;
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D gamepad added event: id=%d", event->gdevice.which);
        (void)slayer3d_input_open_gamepad_slot(input, event->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED: {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D gamepad removed event: id=%d", event->gdevice.which);
        slayer3d_input_gamepad_slot *slot = slayer3d_input_find_gamepad_slot(input, event->gdevice.which);
        if (slot != NULL)
        {
            slayer3d_input_close_gamepad(input, slot);
            slayer3d_input_refresh_gamepads(input);
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        slayer3d_input_gamepad_slot *slot = slayer3d_input_find_gamepad_slot(input, event->gaxis.which);
        if (slot == NULL)
        {
            slot = slayer3d_input_find_free_gamepad_slot(input);
            if (slot != NULL)
            {
                slot->connected = true;
                slot->id = event->gaxis.which;
                input->gamepad_count++;
            }
        }
        if (slot != NULL && slayer3d_input_gamepad_axis_valid((SDL_GamepadAxis)event->gaxis.axis))
        {
            float value = (float)event->gaxis.value / 32767.0f;
            if (event->gaxis.value == -32768)
            {
                value = -1.0f;
            }
            slot->axis_value[event->gaxis.axis] = slayer3d_input_clampf(value, -1.0f, 1.0f);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "SLAYER3D gamepad axis motion: id=%d axis=%d value=%d normalized=%.3f", event->gaxis.which,
                        event->gaxis.axis, event->gaxis.value, slot->axis_value[event->gaxis.axis]);
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        slayer3d_input_gamepad_slot *slot = slayer3d_input_find_gamepad_slot(input, event->gbutton.which);
        if (slot == NULL)
        {
            slot = slayer3d_input_find_free_gamepad_slot(input);
            if (slot != NULL)
            {
                slot->connected = true;
                slot->id = event->gbutton.which;
                input->gamepad_count++;
            }
        }
        if (slot != NULL && slayer3d_input_gamepad_button_valid((SDL_GamepadButton)event->gbutton.button))
        {
            slot->button_down[event->gbutton.button] = true;
            slot->button_pressed_this_frame[event->gbutton.button] = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D gamepad button down: id=%d button=%d",
                        event->gbutton.which, event->gbutton.button);
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        slayer3d_input_gamepad_slot *slot = slayer3d_input_find_gamepad_slot(input, event->gbutton.which);
        if (slot == NULL)
        {
            slot = slayer3d_input_find_free_gamepad_slot(input);
            if (slot != NULL)
            {
                slot->connected = true;
                slot->id = event->gbutton.which;
                input->gamepad_count++;
            }
        }
        if (slot != NULL && slayer3d_input_gamepad_button_valid((SDL_GamepadButton)event->gbutton.button))
        {
            slot->button_down[event->gbutton.button] = false;
            slot->button_released_this_frame[event->gbutton.button] = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D gamepad button up: id=%d button=%d",
                        event->gbutton.which, event->gbutton.button);
        }
        break;
    }
    default:
        break;
    }
}

void slayer3d_input_discard_mouse_motion(slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return;
    }

    slayer3d_mouse_tracef("input.discard_mouse_motion", "input=%p accum_before=(%.3f,%.3f)", (void *)input,
                          input->mouse_dx_accum, input->mouse_dy_accum);
    input->mouse_dx_accum = 0.0f;
    input->mouse_dy_accum = 0.0f;
    input->discard_mouse_motion_until_update = true;
    input->discard_next_mouse_motion = true;
    input->discarded_mouse_motion_since_request = false;
}

const slayer3d_input_snapshot *slayer3d_input_update(slayer3d_input_manager *input, int tick)
{
    if (input == NULL)
    {
        return NULL;
    }

    if (input->demo_player != NULL)
    {
        slayer3d_demo_player *player = input->demo_player;
        if (player->cursor < player->count)
        {
            input->snapshot = player->snapshots[player->cursor++];
            input->pressed_scancode = SDL_SCANCODE_UNKNOWN;
            input->pressed_mouse_button = 0;
            input->pressed_gamepad_button = SDL_GAMEPAD_BUTTON_INVALID;
            input->current_text_input[0] = '\0';
            SDL_memset(input->prev_held, 0, sizeof(input->prev_held));
            for (int i = 0; i < SLAYER3D_INPUT_MAX_ACTIONS; ++i)
            {
                input->prev_held[i] = input->snapshot.actions[i].held;
            }
            slayer3d_input_reset_transients(input);
            return &input->snapshot;
        }
    }

    for (int i = 0; i < SLAYER3D_INPUT_MAX_GAMEPADS; ++i)
    {
        slayer3d_input_sync_gamepad_slot(&input->gamepads[i]);
    }

    slayer3d_input_snapshot next;
    SDL_zero(next);
    next.tick = tick;
    next.mouse_dx = input->mouse_dx_accum;
    next.mouse_dy = input->mouse_dy_accum;
    slayer3d_mouse_tracef(
        "input.update.begin",
        "input=%p tick=%d snapshot_mouse=(%.3f,%.3f) discard_until_update=%d discard_next=%d discarded_since=%d",
        (void *)input, tick, next.mouse_dx, next.mouse_dy, input->discard_mouse_motion_until_update ? 1 : 0,
        input->discard_next_mouse_motion ? 1 : 0, input->discarded_mouse_motion_since_request ? 1 : 0);
    if (input->discard_mouse_motion_until_update && input->discarded_mouse_motion_since_request)
    {
        input->discard_next_mouse_motion = false;
    }
    input->discard_mouse_motion_until_update = false;
    input->discarded_mouse_motion_since_request = false;
    next.any_pressed = slayer3d_input_physical_any_pressed(input);
    input->pressed_scancode = slayer3d_input_first_pressed_scancode(input);
    input->pressed_mouse_button = slayer3d_input_first_pressed_mouse_button(input);
    input->pressed_gamepad_button = slayer3d_input_first_pressed_gamepad_button(input);
    SDL_strlcpy(input->current_text_input, input->pending_text_input, sizeof(input->current_text_input));

    for (int action_id = 0; action_id < input->action_count; ++action_id)
    {
        bool explicit_pressed = false;
        bool explicit_released = false;
        bool was_held = input->prev_held[action_id];
        float value = 0.0f;
        const bool override_active = input->action_override_active[action_id];

        for (int i = 0; i < input->binding_count; ++i)
        {
            const slayer3d_input_binding *binding = &input->bindings[i];
            if (binding->action_id != action_id)
            {
                continue;
            }

            value = slayer3d_input_signed_max_magnitude(value, slayer3d_input_binding_value(input, binding));
            explicit_pressed = explicit_pressed || slayer3d_input_binding_pressed(input, binding);
            explicit_released = explicit_released || slayer3d_input_binding_released(input, binding);
        }

        if (override_active)
        {
            value = input->action_override_value[action_id];
            explicit_pressed = value > 0.0001f;
            explicit_released = value <= 0.0001f && was_held;
        }

        value = slayer3d_input_clampf(value, -1.0f, 1.0f);
        next.actions[action_id].value = value;
        next.actions[action_id].held = slayer3d_input_absf(value) > 0.0001f;
        next.actions[action_id].pressed = (next.actions[action_id].held && !was_held) || explicit_pressed;
        next.actions[action_id].released =
            (!next.actions[action_id].held && was_held) || (explicit_released && (was_held || explicit_pressed));
        next.any_pressed = next.any_pressed || next.actions[action_id].pressed;
        input->prev_held[action_id] = next.actions[action_id].held;
    }

    input->snapshot = next;
    (void)slayer3d_demo_recorder_append(input->recorder, &input->snapshot);
    slayer3d_input_reset_transients(input);
    slayer3d_mouse_tracef("input.update.end", "input=%p tick=%d stored_mouse=(%.3f,%.3f) any_pressed=%d", (void *)input,
                          input->snapshot.tick, input->snapshot.mouse_dx, input->snapshot.mouse_dy,
                          input->snapshot.any_pressed ? 1 : 0);
    return &input->snapshot;
}

bool slayer3d_input_is_pressed(const slayer3d_input_manager *input, int action_id)
{
    return input != NULL && slayer3d_input_action_id_valid(action_id) && input->snapshot.actions[action_id].pressed;
}

bool slayer3d_input_is_released(const slayer3d_input_manager *input, int action_id)
{
    return input != NULL && slayer3d_input_action_id_valid(action_id) && input->snapshot.actions[action_id].released;
}

bool slayer3d_input_is_held(const slayer3d_input_manager *input, int action_id)
{
    return input != NULL && slayer3d_input_action_id_valid(action_id) && input->snapshot.actions[action_id].held;
}

float slayer3d_input_get_value(const slayer3d_input_manager *input, int action_id)
{
    return input != NULL && slayer3d_input_action_id_valid(action_id) ? input->snapshot.actions[action_id].value : 0.0f;
}

SDL_Scancode slayer3d_input_get_pressed_scancode(const slayer3d_input_manager *input)
{
    return input != NULL ? input->pressed_scancode : SDL_SCANCODE_UNKNOWN;
}

SDL_GamepadButton slayer3d_input_get_pressed_gamepad_button(const slayer3d_input_manager *input)
{
    return input != NULL ? input->pressed_gamepad_button : SDL_GAMEPAD_BUTTON_INVALID;
}

Uint8 slayer3d_input_get_pressed_mouse_button(const slayer3d_input_manager *input)
{
    return input != NULL ? input->pressed_mouse_button : 0;
}

bool slayer3d_input_any_pressed(const slayer3d_input_manager *input)
{
    return input != NULL && input->snapshot.any_pressed;
}

float slayer3d_input_get_mouse_dx(const slayer3d_input_manager *input)
{
    return input != NULL ? input->snapshot.mouse_dx : 0.0f;
}

float slayer3d_input_get_mouse_dy(const slayer3d_input_manager *input)
{
    return input != NULL ? input->snapshot.mouse_dy : 0.0f;
}

slayer3d_vec2 slayer3d_input_get_axis_pair(const slayer3d_input_manager *input, int negative_x_action,
                                           int positive_x_action, int negative_y_action, int positive_y_action)
{
    slayer3d_vec2 axis = {0.0f, 0.0f};
    axis.x = slayer3d_input_get_value(input, positive_x_action) - slayer3d_input_get_value(input, negative_x_action);
    axis.y = slayer3d_input_get_value(input, positive_y_action) - slayer3d_input_get_value(input, negative_y_action);
    return axis;
}

static const slayer3d_input_gamepad_slot *slayer3d_input_gamepad_slot_at(const slayer3d_input_manager *input,
                                                                         int gamepad_index)
{
    if (input == NULL || gamepad_index < 0 || gamepad_index >= SLAYER3D_INPUT_MAX_GAMEPADS)
    {
        return NULL;
    }
    return &input->gamepads[gamepad_index];
}

int slayer3d_input_gamepad_count(const slayer3d_input_manager *input)
{
    return input != NULL ? input->gamepad_count : 0;
}

bool slayer3d_input_gamepad_is_connected(const slayer3d_input_manager *input, int gamepad_index)
{
    const slayer3d_input_gamepad_slot *slot = slayer3d_input_gamepad_slot_at(input, gamepad_index);
    return slot != NULL && slot->connected;
}

SDL_JoystickID slayer3d_input_gamepad_id_at(const slayer3d_input_manager *input, int gamepad_index)
{
    const slayer3d_input_gamepad_slot *slot = slayer3d_input_gamepad_slot_at(input, gamepad_index);
    return slot != NULL && slot->connected ? slot->id : 0;
}

bool slayer3d_input_is_gamepad_button_held(const slayer3d_input_manager *input, int gamepad_index,
                                           SDL_GamepadButton button)
{
    const slayer3d_input_gamepad_slot *slot = slayer3d_input_gamepad_slot_at(input, gamepad_index);
    return slot != NULL && slayer3d_input_gamepad_slot_button_down(slot, button);
}

float slayer3d_input_get_gamepad_axis(const slayer3d_input_manager *input, int gamepad_index, SDL_GamepadAxis axis)
{
    const slayer3d_input_gamepad_slot *slot = slayer3d_input_gamepad_slot_at(input, gamepad_index);
    if (slot == NULL)
    {
        return 0.0f;
    }
    return slayer3d_input_gamepad_slot_axis_value(slot, axis,
                                                  input != NULL ? input->deadzone : SLAYER3D_INPUT_DEFAULT_DEADZONE);
}

slayer3d_vec2 slayer3d_input_get_gamepad_left_stick(const slayer3d_input_manager *input, int gamepad_index)
{
    slayer3d_vec2 stick = {0.0f, 0.0f};
    stick.x = slayer3d_input_get_gamepad_axis(input, gamepad_index, SDL_GAMEPAD_AXIS_LEFTX);
    stick.y = slayer3d_input_get_gamepad_axis(input, gamepad_index, SDL_GAMEPAD_AXIS_LEFTY);
    return stick;
}

slayer3d_vec2 slayer3d_input_get_gamepad_right_stick(const slayer3d_input_manager *input, int gamepad_index)
{
    slayer3d_vec2 stick = {0.0f, 0.0f};
    stick.x = slayer3d_input_get_gamepad_axis(input, gamepad_index, SDL_GAMEPAD_AXIS_RIGHTX);
    stick.y = slayer3d_input_get_gamepad_axis(input, gamepad_index, SDL_GAMEPAD_AXIS_RIGHTY);
    return stick;
}

bool slayer3d_input_is_gamepad_left_stick_pressed(const slayer3d_input_manager *input, int gamepad_index)
{
    return slayer3d_input_is_gamepad_button_held(input, gamepad_index, SDL_GAMEPAD_BUTTON_LEFT_STICK);
}

bool slayer3d_input_is_gamepad_right_stick_pressed(const slayer3d_input_manager *input, int gamepad_index)
{
    return slayer3d_input_is_gamepad_button_held(input, gamepad_index, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
}

bool slayer3d_input_is_gamepad_face_button_pressed(const slayer3d_input_manager *input, int gamepad_index)
{
    return slayer3d_input_is_gamepad_button_held(input, gamepad_index, SDL_GAMEPAD_BUTTON_SOUTH) ||
           slayer3d_input_is_gamepad_button_held(input, gamepad_index, SDL_GAMEPAD_BUTTON_EAST) ||
           slayer3d_input_is_gamepad_button_held(input, gamepad_index, SDL_GAMEPAD_BUTTON_WEST) ||
           slayer3d_input_is_gamepad_button_held(input, gamepad_index, SDL_GAMEPAD_BUTTON_NORTH);
}

bool slayer3d_input_is_gamepad_start_pressed(const slayer3d_input_manager *input, int gamepad_index)
{
    return slayer3d_input_is_gamepad_button_held(input, gamepad_index, SDL_GAMEPAD_BUTTON_START);
}

bool slayer3d_input_is_gamepad_select_pressed(const slayer3d_input_manager *input, int gamepad_index)
{
    return slayer3d_input_is_gamepad_button_held(input, gamepad_index, SDL_GAMEPAD_BUTTON_BACK);
}

static Uint16 slayer3d_input_rumble_scale(float value)
{
    if (value <= 0.0f)
    {
        return 0;
    }
    if (value >= 1.0f)
    {
        return UINT16_MAX;
    }
    return (Uint16)(value * (float)UINT16_MAX);
}

bool slayer3d_input_rumble_gamepad(slayer3d_input_manager *input, int gamepad_index, float low_frequency_rumble,
                                   float high_frequency_rumble, Uint32 duration_ms)
{
    const slayer3d_input_gamepad_slot *slot = slayer3d_input_gamepad_slot_at(input, gamepad_index);
    if (slot == NULL || slot->gamepad == NULL)
    {
        return false;
    }

    const bool ok = SDL_RumbleGamepad(slot->gamepad, slayer3d_input_rumble_scale(low_frequency_rumble),
                                      slayer3d_input_rumble_scale(high_frequency_rumble), duration_ms);
    const char *name = SDL_GetGamepadName(slot->gamepad);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SLAYER3D gamepad rumble: slot=%d id=%d name=%s low=%.3f high=%.3f duration=%u result=%s error=%s",
                gamepad_index, slot->id, name != NULL ? name : "<unknown>", low_frequency_rumble, high_frequency_rumble,
                duration_ms, ok ? "ok" : "failed", ok ? "" : SDL_GetError());
    return ok;
}

bool slayer3d_input_rumble_all_gamepads(slayer3d_input_manager *input, float low_frequency_rumble,
                                        float high_frequency_rumble, Uint32 duration_ms)
{
    bool any = false;
    if (input == NULL)
    {
        return false;
    }

    for (int i = 0; i < SLAYER3D_INPUT_MAX_GAMEPADS; ++i)
    {
        if (!slayer3d_input_gamepad_is_connected(input, i))
        {
            continue;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D gamepad rumble request routed to slot=%d", i);
        any = slayer3d_input_rumble_gamepad(input, i, low_frequency_rumble, high_frequency_rumble, duration_ms) || any;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D gamepad rumble all result=%s", any ? "accepted" : "rejected");
    return any;
}

const slayer3d_input_snapshot *slayer3d_input_get_snapshot(const slayer3d_input_manager *input)
{
    return input != NULL ? &input->snapshot : NULL;
}

const char *slayer3d_input_get_text_input(const slayer3d_input_manager *input)
{
    return input != NULL ? input->current_text_input : "";
}

static int slayer3d_input_ensure_action(slayer3d_input_manager *input, const char *name)
{
    return slayer3d_input_register_action(input, name);
}

void slayer3d_input_bind_fps_defaults(slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return;
    }

    int move_forward = slayer3d_input_ensure_action(input, "move_forward");
    int move_back = slayer3d_input_ensure_action(input, "move_back");
    int move_left = slayer3d_input_ensure_action(input, "move_left");
    int move_right = slayer3d_input_ensure_action(input, "move_right");
    int look_up = slayer3d_input_ensure_action(input, "look_up");
    int look_down = slayer3d_input_ensure_action(input, "look_down");
    int look_left = slayer3d_input_ensure_action(input, "look_left");
    int look_right = slayer3d_input_ensure_action(input, "look_right");
    int jump = slayer3d_input_ensure_action(input, "jump");
    int fire = slayer3d_input_ensure_action(input, "fire");
    int alt_fire = slayer3d_input_ensure_action(input, "alt_fire");
    int reload = slayer3d_input_ensure_action(input, "reload");
    int interact = slayer3d_input_ensure_action(input, "interact");
    int crouch = slayer3d_input_ensure_action(input, "crouch");
    int sprint = slayer3d_input_ensure_action(input, "sprint");
    int menu = slayer3d_input_ensure_action(input, "menu");
    int pause = slayer3d_input_ensure_action(input, "pause");

    slayer3d_input_bind_key(input, move_forward, SDL_SCANCODE_W);
    slayer3d_input_bind_gamepad_axis(input, move_forward, SDL_GAMEPAD_AXIS_LEFTY, -1.0f);
    slayer3d_input_bind_key(input, move_back, SDL_SCANCODE_S);
    slayer3d_input_bind_gamepad_axis(input, move_back, SDL_GAMEPAD_AXIS_LEFTY, 1.0f);
    slayer3d_input_bind_key(input, move_left, SDL_SCANCODE_A);
    slayer3d_input_bind_gamepad_axis(input, move_left, SDL_GAMEPAD_AXIS_LEFTX, -1.0f);
    slayer3d_input_bind_key(input, move_right, SDL_SCANCODE_D);
    slayer3d_input_bind_gamepad_axis(input, move_right, SDL_GAMEPAD_AXIS_LEFTX, 1.0f);

    slayer3d_input_bind_mouse_axis(input, look_up, SLAYER3D_MOUSE_AXIS_Y, -1.0f);
    slayer3d_input_bind_gamepad_axis(input, look_up, SDL_GAMEPAD_AXIS_RIGHTY, -1.0f);
    slayer3d_input_bind_mouse_axis(input, look_down, SLAYER3D_MOUSE_AXIS_Y, 1.0f);
    slayer3d_input_bind_gamepad_axis(input, look_down, SDL_GAMEPAD_AXIS_RIGHTY, 1.0f);
    slayer3d_input_bind_mouse_axis(input, look_left, SLAYER3D_MOUSE_AXIS_X, -1.0f);
    slayer3d_input_bind_gamepad_axis(input, look_left, SDL_GAMEPAD_AXIS_RIGHTX, -1.0f);
    slayer3d_input_bind_mouse_axis(input, look_right, SLAYER3D_MOUSE_AXIS_X, 1.0f);
    slayer3d_input_bind_gamepad_axis(input, look_right, SDL_GAMEPAD_AXIS_RIGHTX, 1.0f);

    slayer3d_input_bind_key(input, jump, SDL_SCANCODE_SPACE);
    slayer3d_input_bind_gamepad_button(input, jump, SDL_GAMEPAD_BUTTON_SOUTH);
    slayer3d_input_bind_mouse_button(input, fire, SDL_BUTTON_LEFT);
    slayer3d_input_bind_gamepad_axis(input, fire, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f);
    slayer3d_input_bind_mouse_button(input, alt_fire, SDL_BUTTON_RIGHT);
    slayer3d_input_bind_gamepad_axis(input, alt_fire, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.0f);
    slayer3d_input_bind_key(input, reload, SDL_SCANCODE_R);
    slayer3d_input_bind_gamepad_button(input, reload, SDL_GAMEPAD_BUTTON_WEST);
    slayer3d_input_bind_key(input, interact, SDL_SCANCODE_E);
    slayer3d_input_bind_gamepad_button(input, interact, SDL_GAMEPAD_BUTTON_NORTH);
    slayer3d_input_bind_key(input, crouch, SDL_SCANCODE_LCTRL);
    slayer3d_input_bind_gamepad_button(input, crouch, SDL_GAMEPAD_BUTTON_EAST);
    slayer3d_input_bind_key(input, sprint, SDL_SCANCODE_LSHIFT);
    slayer3d_input_bind_gamepad_button(input, sprint, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    slayer3d_input_bind_key(input, menu, SDL_SCANCODE_ESCAPE);
    slayer3d_input_bind_gamepad_button(input, menu, SDL_GAMEPAD_BUTTON_BACK);
    slayer3d_input_bind_key(input, pause, SDL_SCANCODE_RETURN);
    slayer3d_input_bind_key(input, pause, SDL_SCANCODE_RETURN2);
    slayer3d_input_bind_key(input, pause, SDL_SCANCODE_P);
    slayer3d_input_bind_gamepad_button(input, pause, SDL_GAMEPAD_BUTTON_START);
}

void slayer3d_input_bind_ui_defaults(slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return;
    }

    int accept = slayer3d_input_ensure_action(input, "ui_accept");
    int back = slayer3d_input_ensure_action(input, "ui_back");
    int up = slayer3d_input_ensure_action(input, "ui_up");
    int down = slayer3d_input_ensure_action(input, "ui_down");
    int left = slayer3d_input_ensure_action(input, "ui_left");
    int right = slayer3d_input_ensure_action(input, "ui_right");
    int tab_next = slayer3d_input_ensure_action(input, "ui_tab_next");
    int tab_prev = slayer3d_input_ensure_action(input, "ui_tab_prev");

    slayer3d_input_bind_key(input, accept, SDL_SCANCODE_RETURN);
    slayer3d_input_bind_gamepad_button(input, accept, SDL_GAMEPAD_BUTTON_SOUTH);
    slayer3d_input_bind_key(input, back, SDL_SCANCODE_ESCAPE);
    slayer3d_input_bind_gamepad_button(input, back, SDL_GAMEPAD_BUTTON_EAST);

    slayer3d_input_bind_key(input, up, SDL_SCANCODE_UP);
    slayer3d_input_bind_gamepad_button(input, up, SDL_GAMEPAD_BUTTON_DPAD_UP);
    slayer3d_input_bind_gamepad_axis(input, up, SDL_GAMEPAD_AXIS_LEFTY, -1.0f);
    slayer3d_input_bind_key(input, down, SDL_SCANCODE_DOWN);
    slayer3d_input_bind_gamepad_button(input, down, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    slayer3d_input_bind_gamepad_axis(input, down, SDL_GAMEPAD_AXIS_LEFTY, 1.0f);
    slayer3d_input_bind_key(input, left, SDL_SCANCODE_LEFT);
    slayer3d_input_bind_gamepad_button(input, left, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    slayer3d_input_bind_gamepad_axis(input, left, SDL_GAMEPAD_AXIS_LEFTX, -1.0f);
    slayer3d_input_bind_key(input, right, SDL_SCANCODE_RIGHT);
    slayer3d_input_bind_gamepad_button(input, right, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    slayer3d_input_bind_gamepad_axis(input, right, SDL_GAMEPAD_AXIS_LEFTX, 1.0f);

    slayer3d_input_bind_key_mod_mask(input, tab_next, SDL_SCANCODE_TAB, SLAYER3D_INPUT_MOD_NONE,
                                     SLAYER3D_INPUT_MOD_SHIFT);
    slayer3d_input_bind_gamepad_button(input, tab_next, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    slayer3d_input_bind_key_mod(input, tab_prev, SDL_SCANCODE_TAB, SLAYER3D_INPUT_MOD_SHIFT);
    slayer3d_input_bind_gamepad_button(input, tab_prev, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
}

void slayer3d_input_set_deadzone(slayer3d_input_manager *input, float deadzone)
{
    if (input == NULL)
    {
        return;
    }
    input->deadzone = slayer3d_input_clampf(deadzone, 0.0f, 1.0f);
}

slayer3d_demo_recorder *slayer3d_demo_record_start(slayer3d_input_manager *input)
{
    if (input == NULL)
    {
        return NULL;
    }

    slayer3d_demo_recorder *recorder = (slayer3d_demo_recorder *)SDL_calloc(1, sizeof(*recorder));
    if (recorder == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    if (input->recorder != NULL)
    {
        input->recorder->input = NULL;
        input->recorder->recording = false;
    }

    recorder->input = input;
    recorder->recording = true;
    input->recorder = recorder;
    return recorder;
}

void slayer3d_demo_record_stop(slayer3d_demo_recorder *recorder)
{
    if (recorder == NULL)
    {
        return;
    }

    recorder->recording = false;
    if (recorder->input != NULL && recorder->input->recorder == recorder)
    {
        recorder->input->recorder = NULL;
    }
    recorder->input = NULL;
}

bool slayer3d_demo_save(const slayer3d_demo_recorder *recorder, const char *path, float tick_rate)
{
    SDL_IOStream *stream;
    bool ok;

    if (recorder == NULL || path == NULL)
    {
        return SDL_InvalidParamError(recorder == NULL ? "recorder" : "path");
    }

    stream = SDL_IOFromFile(path, "wb");
    if (stream == NULL)
    {
        return false;
    }

    ok = slayer3d_demo_write_header(stream, tick_rate, recorder->count);
    for (Uint32 i = 0; ok && i < recorder->count; ++i)
    {
        ok = slayer3d_demo_write_snapshot(stream, &recorder->snapshots[i]);
    }

    return SDL_CloseIO(stream) && ok;
}

Uint32 slayer3d_demo_record_count(const slayer3d_demo_recorder *recorder)
{
    return recorder != NULL ? recorder->count : 0U;
}

const slayer3d_input_snapshot *slayer3d_demo_record_snapshot(const slayer3d_demo_recorder *recorder, Uint32 index)
{
    return recorder != NULL && index < recorder->count ? &recorder->snapshots[index] : NULL;
}

void slayer3d_demo_record_free(slayer3d_demo_recorder *recorder)
{
    if (recorder == NULL)
    {
        return;
    }

    slayer3d_demo_record_stop(recorder);
    SDL_free(recorder->snapshots);
    SDL_free(recorder);
}

slayer3d_demo_player *slayer3d_demo_playback_load(const char *path)
{
    SDL_IOStream *stream;
    slayer3d_demo_player *player;
    Uint32 tick_count;
    Uint32 action_count;
    size_t snapshot_bytes;
    float tick_rate;

    if (path == NULL)
    {
        SDL_InvalidParamError("path");
        return NULL;
    }

    stream = SDL_IOFromFile(path, "rb");
    if (stream == NULL)
    {
        return NULL;
    }

    if (!slayer3d_demo_read_header(stream, &tick_rate, &tick_count, &action_count))
    {
        SDL_CloseIO(stream);
        return NULL;
    }

    player = (slayer3d_demo_player *)SDL_calloc(1, sizeof(*player));
    if (player == NULL)
    {
        SDL_CloseIO(stream);
        SDL_OutOfMemory();
        return NULL;
    }

    player->tick_rate = tick_rate;
    player->count = tick_count;

    if (tick_count > 0)
    {
        if (tick_count > (Uint32)(SIZE_MAX / sizeof(*player->snapshots)))
        {
            slayer3d_demo_playback_free(player);
            SDL_CloseIO(stream);
            SDL_SetError("SLAYER3D demo file is too large.");
            return NULL;
        }

        snapshot_bytes = (size_t)tick_count * sizeof(*player->snapshots);
        player->snapshots = (slayer3d_input_snapshot *)SDL_malloc(snapshot_bytes);
        if (player->snapshots == NULL)
        {
            slayer3d_demo_playback_free(player);
            SDL_CloseIO(stream);
            SDL_OutOfMemory();
            return NULL;
        }

        for (Uint32 i = 0; i < tick_count; ++i)
        {
            if (!slayer3d_demo_read_snapshot(stream, &player->snapshots[i]))
            {
                slayer3d_demo_playback_free(player);
                SDL_CloseIO(stream);
                SDL_SetError("Truncated SLAYER3D demo file.");
                return NULL;
            }
        }
    }

    if (!SDL_CloseIO(stream))
    {
        slayer3d_demo_playback_free(player);
        return NULL;
    }

    return player;
}

void slayer3d_demo_playback_start(slayer3d_input_manager *input, slayer3d_demo_player *player)
{
    if (input == NULL)
    {
        return;
    }

    input->demo_player = player;
    if (player != NULL)
    {
        player->cursor = 0;
    }
}

void slayer3d_demo_playback_stop(slayer3d_input_manager *input)
{
    if (input != NULL)
    {
        input->demo_player = NULL;
    }
}

bool slayer3d_demo_playback_finished(const slayer3d_demo_player *player)
{
    return player == NULL || player->cursor >= player->count;
}

float slayer3d_demo_playback_tick_rate(const slayer3d_demo_player *player)
{
    return player != NULL ? player->tick_rate : 0.0f;
}

Uint32 slayer3d_demo_playback_count(const slayer3d_demo_player *player)
{
    return player != NULL ? player->count : 0U;
}

void slayer3d_demo_playback_free(slayer3d_demo_player *player)
{
    if (player == NULL)
    {
        return;
    }

    SDL_free(player->snapshots);
    SDL_free(player);
}

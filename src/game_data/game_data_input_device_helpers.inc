/* Input device name conversion helpers. Included by src/game_data.c to preserve internal linkage. */

static SDL_Scancode scancode_from_json(const char *name)
{
    if (name == NULL)
        return SDL_SCANCODE_UNKNOWN;
    if (SDL_strcmp(name, "UP") == 0)
        return SDL_SCANCODE_UP;
    if (SDL_strcmp(name, "DOWN") == 0)
        return SDL_SCANCODE_DOWN;
    if (SDL_strcmp(name, "LEFT") == 0)
        return SDL_SCANCODE_LEFT;
    if (SDL_strcmp(name, "RIGHT") == 0)
        return SDL_SCANCODE_RIGHT;
    if (SDL_strcmp(name, "RETURN") == 0)
        return SDL_SCANCODE_RETURN;
    if (SDL_strcmp(name, "ESCAPE") == 0)
        return SDL_SCANCODE_ESCAPE;
    if (SDL_strcmp(name, "BACKSPACE") == 0 || SDL_strcmp(name, "DELETE") == 0)
        return SDL_SCANCODE_BACKSPACE;
    if (SDL_strlen(name) == 1)
        return SDL_GetScancodeFromKey(SDL_GetKeyFromName(name), NULL);
    return SDL_GetScancodeFromName(name);
}

static const char *scancode_display_name(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return "-";
    const char *name = SDL_GetScancodeName(scancode);
    return name != NULL && name[0] != '\0' ? name : "-";
}

static Uint8 mouse_button_from_json(const char *name)
{
    if (name == NULL)
        return 0;
    if (SDL_strcmp(name, "LEFT") == 0)
        return SDL_BUTTON_LEFT;
    if (SDL_strcmp(name, "MIDDLE") == 0)
        return SDL_BUTTON_MIDDLE;
    if (SDL_strcmp(name, "RIGHT") == 0)
        return SDL_BUTTON_RIGHT;
    if (SDL_strcmp(name, "X1") == 0)
        return SDL_BUTTON_X1;
    if (SDL_strcmp(name, "X2") == 0)
        return SDL_BUTTON_X2;
    return 0;
}

static const char *mouse_button_display_name(Uint8 button)
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        return "Left Mouse";
    case SDL_BUTTON_MIDDLE:
        return "Middle Mouse";
    case SDL_BUTTON_RIGHT:
        return "Right Mouse";
    case SDL_BUTTON_X1:
        return "Mouse X1";
    case SDL_BUTTON_X2:
        return "Mouse X2";
    default:
        return "-";
    }
}

static slayer3d_mouse_axis mouse_axis_from_json(const char *name, bool *valid)
{
    if (valid != NULL)
        *valid = true;
    if (name == NULL)
    {
        if (valid != NULL)
            *valid = false;
        return SLAYER3D_MOUSE_AXIS_X;
    }
    if (SDL_strcmp(name, "x") == 0)
        return SLAYER3D_MOUSE_AXIS_X;
    if (SDL_strcmp(name, "y") == 0)
        return SLAYER3D_MOUSE_AXIS_Y;
    if (SDL_strcmp(name, "wheel") == 0)
        return SLAYER3D_MOUSE_AXIS_WHEEL;
    if (SDL_strcmp(name, "wheel_x") == 0)
        return SLAYER3D_MOUSE_AXIS_WHEEL_X;
    if (valid != NULL)
        *valid = false;
    return SLAYER3D_MOUSE_AXIS_X;
}

static const char *gamepad_button_display_name(SDL_GamepadButton button)
{
    if (button == SDL_GAMEPAD_BUTTON_INVALID)
        return "-";
    const char *name = SDL_GetGamepadStringForButton(button);
    return name != NULL && name[0] != '\0' ? name : "-";
}

static SDL_GamepadAxis gamepad_axis_from_json(const char *name)
{
    if (name == NULL)
        return SDL_GAMEPAD_AXIS_INVALID;
    if (SDL_strcmp(name, "left_x") == 0)
        return SDL_GAMEPAD_AXIS_LEFTX;
    if (SDL_strcmp(name, "left_y") == 0)
        return SDL_GAMEPAD_AXIS_LEFTY;
    if (SDL_strcmp(name, "right_x") == 0)
        return SDL_GAMEPAD_AXIS_RIGHTX;
    if (SDL_strcmp(name, "right_y") == 0)
        return SDL_GAMEPAD_AXIS_RIGHTY;
    if (SDL_strcmp(name, "left_trigger") == 0)
        return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    if (SDL_strcmp(name, "right_trigger") == 0)
        return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    return SDL_GAMEPAD_AXIS_INVALID;
}

static SDL_GamepadButton gamepad_button_from_json(const char *name)
{
    if (name == NULL)
        return SDL_GAMEPAD_BUTTON_INVALID;
    if (SDL_strcmp(name, "START") == 0)
        return SDL_GAMEPAD_BUTTON_START;
    if (SDL_strcmp(name, "BACK") == 0)
        return SDL_GAMEPAD_BUTTON_BACK;
    if (SDL_strcmp(name, "SOUTH") == 0)
        return SDL_GAMEPAD_BUTTON_SOUTH;
    if (SDL_strcmp(name, "NORTH") == 0)
        return SDL_GAMEPAD_BUTTON_NORTH;
    if (SDL_strcmp(name, "EAST") == 0)
        return SDL_GAMEPAD_BUTTON_EAST;
    if (SDL_strcmp(name, "WEST") == 0)
        return SDL_GAMEPAD_BUTTON_WEST;
    if (SDL_strcmp(name, "LEFT_STICK") == 0)
        return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    if (SDL_strcmp(name, "RIGHT_STICK") == 0)
        return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    if (SDL_strcmp(name, "LEFT_SHOULDER") == 0)
        return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    if (SDL_strcmp(name, "RIGHT_SHOULDER") == 0)
        return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    if (SDL_strcmp(name, "DPAD_UP") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_UP;
    if (SDL_strcmp(name, "DPAD_DOWN") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    if (SDL_strcmp(name, "DPAD_LEFT") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    if (SDL_strcmp(name, "DPAD_RIGHT") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    if (SDL_strcmp(name, "GUIDE") == 0)
        return SDL_GAMEPAD_BUTTON_GUIDE;
    if (SDL_strcmp(name, "MISC1") == 0)
        return SDL_GAMEPAD_BUTTON_MISC1;
    if (SDL_strcmp(name, "RIGHT_PADDLE1") == 0)
        return SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1;
    if (SDL_strcmp(name, "LEFT_PADDLE1") == 0)
        return SDL_GAMEPAD_BUTTON_LEFT_PADDLE1;
    if (SDL_strcmp(name, "RIGHT_PADDLE2") == 0)
        return SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2;
    if (SDL_strcmp(name, "LEFT_PADDLE2") == 0)
        return SDL_GAMEPAD_BUTTON_LEFT_PADDLE2;
    if (SDL_strcmp(name, "TOUCHPAD") == 0)
        return SDL_GAMEPAD_BUTTON_TOUCHPAD;
    return SDL_GAMEPAD_BUTTON_INVALID;
}

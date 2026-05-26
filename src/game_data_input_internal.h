#ifndef SLAYER3D_GAME_DATA_INPUT_INTERNAL_H
#define SLAYER3D_GAME_DATA_INPUT_INTERNAL_H

#include <stdbool.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/input.h"

SDL_Scancode scancode_from_json(const char *name);
const char *scancode_display_name(SDL_Scancode scancode);
Uint8 mouse_button_from_json(const char *name);
const char *mouse_button_display_name(Uint8 button);
slayer3d_mouse_axis mouse_axis_from_json(const char *name, bool *valid);
const char *gamepad_button_display_name(SDL_GamepadButton button);
SDL_GamepadAxis gamepad_axis_from_json(const char *name);
SDL_GamepadButton gamepad_button_from_json(const char *name);

#endif

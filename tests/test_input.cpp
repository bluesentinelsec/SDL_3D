#define SDL_MAIN_HANDLED

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/input.h"
}

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
struct InputPtr
{
    sdl3d_input_manager *input = sdl3d_input_create();

    ~InputPtr()
    {
        sdl3d_input_destroy(input);
    }
};

std::string demo_path()
{
    char *pref_path = SDL_GetPrefPath("SDL3D", "tests");
    if (pref_path == nullptr)
    {
        return "sdl3d_input_test.dem";
    }

    std::string path = pref_path;
    SDL_free(pref_path);
    path += "sdl3d_input_test.dem";
    return path;
}

std::vector<unsigned char> read_binary_file(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

Uint32 read_u32_le(const std::vector<unsigned char> &bytes, size_t offset)
{
    return static_cast<Uint32>(bytes[offset]) | (static_cast<Uint32>(bytes[offset + 1]) << 8U) |
           (static_cast<Uint32>(bytes[offset + 2]) << 16U) | (static_cast<Uint32>(bytes[offset + 3]) << 24U);
}

void push_key(sdl3d_input_manager *input, SDL_EventType type, SDL_Scancode scancode)
{
    SDL_Event event{};
    event.type = type;
    event.key.scancode = scancode;
    sdl3d_input_process_event(input, &event);
}

void push_text(sdl3d_input_manager *input, const char *text)
{
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = text;
    sdl3d_input_process_event(input, &event);
}

void push_key_mod(sdl3d_input_manager *input, SDL_EventType type, SDL_Scancode scancode, SDL_Keymod modifiers)
{
    SDL_Event event{};
    event.type = type;
    event.key.scancode = scancode;
    event.key.mod = modifiers;
    sdl3d_input_process_event(input, &event);
}

void push_mouse_button(sdl3d_input_manager *input, SDL_EventType type, Uint8 button)
{
    SDL_Event event{};
    event.type = type;
    event.button.button = button;
    sdl3d_input_process_event(input, &event);
}

void push_gamepad_device(sdl3d_input_manager *input, SDL_EventType type, SDL_JoystickID which)
{
    SDL_Event event{};
    event.type = type;
    event.gdevice.which = which;
    sdl3d_input_process_event(input, &event);
}

void push_gamepad_button(sdl3d_input_manager *input, SDL_EventType type, SDL_JoystickID which, SDL_GamepadButton button)
{
    SDL_Event event{};
    event.type = type;
    event.gbutton.which = which;
    event.gbutton.button = button;
    sdl3d_input_process_event(input, &event);
}

void push_gamepad_axis(sdl3d_input_manager *input, SDL_JoystickID which, SDL_GamepadAxis axis, Sint16 value)
{
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.which = which;
    event.gaxis.axis = static_cast<Uint8>(axis);
    event.gaxis.value = value;
    sdl3d_input_process_event(input, &event);
}

void push_mouse_motion(sdl3d_input_manager *input, float dx, float dy)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel = dx;
    event.motion.yrel = dy;
    sdl3d_input_process_event(input, &event);
}

int find_gamepad_slot(const sdl3d_input_manager *input, SDL_JoystickID which)
{
    for (int i = 0; i < SDL3D_INPUT_MAX_GAMEPADS; ++i)
    {
        if (sdl3d_input_gamepad_is_connected(input, i) && sdl3d_input_gamepad_id_at(input, i) == which)
        {
            return i;
        }
    }
    return -1;
}
} // namespace

TEST(Input, RegisterActionFindsByName)
{
    InputPtr input;
    ASSERT_NE(input.input, nullptr);

    int action = sdl3d_input_register_action(input.input, "jump");
    EXPECT_GE(action, 0);
    EXPECT_EQ(action, sdl3d_input_find_action(input.input, "jump"));
    EXPECT_EQ(-1, sdl3d_input_find_action(input.input, "missing"));
}

TEST(Input, RegisterDuplicateReturnsSameId)
{
    InputPtr input;

    int first = sdl3d_input_register_action(input.input, "fire");
    int second = sdl3d_input_register_action(input.input, "fire");

    EXPECT_GE(first, 0);
    EXPECT_EQ(first, second);
}

TEST(Input, RejectsInvalidActionNames)
{
    InputPtr input;
    char long_name[SDL3D_INPUT_ACTION_NAME_MAX + 2]{};
    SDL_memset(long_name, 'x', sizeof(long_name) - 1);

    EXPECT_EQ(-1, sdl3d_input_register_action(input.input, nullptr));
    EXPECT_EQ(-1, sdl3d_input_register_action(input.input, ""));
    EXPECT_EQ(-1, sdl3d_input_register_action(input.input, long_name));
}

TEST(Input, BindKeyAndQueryHeld)
{
    InputPtr input;
    int action = sdl3d_input_register_action(input.input, "move_forward");
    sdl3d_input_bind_key(input.input, action, SDL_SCANCODE_W);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W);
    sdl3d_input_update(input.input, 1);

    EXPECT_TRUE(sdl3d_input_is_held(input.input, action));
    EXPECT_FLOAT_EQ(1.0f, sdl3d_input_get_value(input.input, action));
}

TEST(Input, PressedEdgeDetection)
{
    InputPtr input;
    int action = sdl3d_input_register_action(input.input, "jump");
    sdl3d_input_bind_key(input.input, action, SDL_SCANCODE_SPACE);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE);
    sdl3d_input_update(input.input, 1);
    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, action));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, action));

    sdl3d_input_update(input.input, 2);
    EXPECT_FALSE(sdl3d_input_is_pressed(input.input, action));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, action));
}

TEST(Input, PressedScancodeIsCapturedForCurrentTick)
{
    InputPtr input;

    EXPECT_EQ(sdl3d_input_get_pressed_scancode(input.input), SDL_SCANCODE_UNKNOWN);
    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_I);
    sdl3d_input_update(input.input, 1);
    EXPECT_EQ(sdl3d_input_get_pressed_scancode(input.input), SDL_SCANCODE_I);

    sdl3d_input_update(input.input, 2);
    EXPECT_EQ(sdl3d_input_get_pressed_scancode(input.input), SDL_SCANCODE_UNKNOWN);
}

TEST(Input, ReleasedEdgeDetection)
{
    InputPtr input;
    int action = sdl3d_input_register_action(input.input, "jump");
    sdl3d_input_bind_key(input.input, action, SDL_SCANCODE_SPACE);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE);
    sdl3d_input_update(input.input, 1);
    push_key(input.input, SDL_EVENT_KEY_UP, SDL_SCANCODE_SPACE);
    sdl3d_input_update(input.input, 2);

    EXPECT_TRUE(sdl3d_input_is_released(input.input, action));
    EXPECT_FALSE(sdl3d_input_is_held(input.input, action));

    sdl3d_input_update(input.input, 3);
    EXPECT_FALSE(sdl3d_input_is_released(input.input, action));
}

TEST(Input, MouseDeltaAccumulation)
{
    InputPtr input;

    push_mouse_motion(input.input, 3.0f, -2.0f);
    push_mouse_motion(input.input, 4.5f, 1.0f);
    sdl3d_input_update(input.input, 10);

    EXPECT_FLOAT_EQ(7.5f, sdl3d_input_get_mouse_dx(input.input));
    EXPECT_FLOAT_EQ(-1.0f, sdl3d_input_get_mouse_dy(input.input));

    sdl3d_input_update(input.input, 11);
    EXPECT_FLOAT_EQ(0.0f, sdl3d_input_get_mouse_dx(input.input));
    EXPECT_FLOAT_EQ(0.0f, sdl3d_input_get_mouse_dy(input.input));
}

TEST(Input, MouseAxisBindingUsesScale)
{
    InputPtr input;
    int look_left = sdl3d_input_register_action(input.input, "look_left");
    sdl3d_input_bind_mouse_axis(input.input, look_left, SDL3D_MOUSE_AXIS_X, -1.0f);

    push_mouse_motion(input.input, 0.25f, 0.0f);
    sdl3d_input_update(input.input, 1);

    EXPECT_TRUE(sdl3d_input_is_held(input.input, look_left));
    EXPECT_FLOAT_EQ(-0.25f, sdl3d_input_get_value(input.input, look_left));
}

TEST(Input, OppositeMouseAxisBindingsUsePositiveDirection)
{
    InputPtr input;
    int look_left = sdl3d_input_register_action(input.input, "look_left");
    int look_right = sdl3d_input_register_action(input.input, "look_right");
    sdl3d_input_bind_mouse_axis(input.input, look_left, SDL3D_MOUSE_AXIS_X, -1.0f);
    sdl3d_input_bind_mouse_axis(input.input, look_right, SDL3D_MOUSE_AXIS_X, 1.0f);

    push_mouse_motion(input.input, 0.25f, 0.0f);
    sdl3d_input_update(input.input, 1);
    EXPECT_FALSE(sdl3d_input_is_held(input.input, look_left));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, look_right));
    EXPECT_FLOAT_EQ(0.0f, sdl3d_input_get_value(input.input, look_left));
    EXPECT_FLOAT_EQ(0.25f, sdl3d_input_get_value(input.input, look_right));

    push_mouse_motion(input.input, -0.5f, 0.0f);
    sdl3d_input_update(input.input, 2);
    EXPECT_TRUE(sdl3d_input_is_held(input.input, look_left));
    EXPECT_FALSE(sdl3d_input_is_held(input.input, look_right));
    EXPECT_FLOAT_EQ(0.5f, sdl3d_input_get_value(input.input, look_left));
    EXPECT_FLOAT_EQ(0.0f, sdl3d_input_get_value(input.input, look_right));
}

TEST(Input, MouseButtonPressAndRelease)
{
    InputPtr input;
    int fire = sdl3d_input_register_action(input.input, "fire");
    sdl3d_input_bind_mouse_button(input.input, fire, SDL_BUTTON_LEFT);

    push_mouse_button(input.input, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
    sdl3d_input_update(input.input, 1);
    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, fire));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, fire));
    EXPECT_EQ(sdl3d_input_get_pressed_mouse_button(input.input), SDL_BUTTON_LEFT);

    push_mouse_button(input.input, SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
    sdl3d_input_update(input.input, 2);
    EXPECT_TRUE(sdl3d_input_is_released(input.input, fire));
    EXPECT_FALSE(sdl3d_input_is_held(input.input, fire));
    EXPECT_EQ(sdl3d_input_get_pressed_mouse_button(input.input), 0);
}

TEST(Input, GamepadButtonPressAndRelease)
{
    InputPtr input;
    if (sdl3d_input_gamepad_count(input.input) >= SDL3D_INPUT_MAX_GAMEPADS)
    {
        GTEST_SKIP() << "requires an available gamepad slot";
    }
    int pause = sdl3d_input_register_action(input.input, "pause");
    sdl3d_input_bind_gamepad_button(input.input, pause, SDL_GAMEPAD_BUTTON_START);

    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1001);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1001, SDL_GAMEPAD_BUTTON_START);
    sdl3d_input_update(input.input, 1);
    EXPECT_TRUE(sdl3d_input_is_held(input.input, pause));
    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, pause));
    EXPECT_EQ(sdl3d_input_get_pressed_gamepad_button(input.input), SDL_GAMEPAD_BUTTON_START);

    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_UP, 1001, SDL_GAMEPAD_BUTTON_START);
    sdl3d_input_update(input.input, 2);
    EXPECT_FALSE(sdl3d_input_is_held(input.input, pause));
    EXPECT_TRUE(sdl3d_input_is_released(input.input, pause));
    EXPECT_EQ(sdl3d_input_get_pressed_gamepad_button(input.input), SDL_GAMEPAD_BUTTON_INVALID);
}

TEST(Input, GamepadBindingsCanTargetSpecificSlots)
{
    InputPtr input;
    if (sdl3d_input_gamepad_count(input.input) != 0)
    {
        GTEST_SKIP() << "requires no pre-connected gamepads";
    }

    int player_one = sdl3d_input_register_action(input.input, "player_one");
    int player_two = sdl3d_input_register_action(input.input, "player_two");
    sdl3d_input_bind_gamepad_button_at(input.input, player_one, 0, SDL_GAMEPAD_BUTTON_SOUTH);
    sdl3d_input_bind_gamepad_button_at(input.input, player_two, 1, SDL_GAMEPAD_BUTTON_SOUTH);

    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1101);
    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1102);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1101, SDL_GAMEPAD_BUTTON_SOUTH);
    sdl3d_input_update(input.input, 1);
    EXPECT_TRUE(sdl3d_input_is_held(input.input, player_one));
    EXPECT_FALSE(sdl3d_input_is_held(input.input, player_two));

    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_UP, 1101, SDL_GAMEPAD_BUTTON_SOUTH);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1102, SDL_GAMEPAD_BUTTON_SOUTH);
    sdl3d_input_update(input.input, 2);
    EXPECT_FALSE(sdl3d_input_is_held(input.input, player_one));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, player_two));
}

TEST(Input, GamepadAxisAndConvenienceQueries)
{
    InputPtr input;
    if (sdl3d_input_gamepad_count(input.input) != 0)
    {
        GTEST_SKIP() << "requires no pre-connected gamepads";
    }
    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1007);
    push_gamepad_axis(input.input, 1007, SDL_GAMEPAD_AXIS_LEFTX, 18000);
    push_gamepad_axis(input.input, 1007, SDL_GAMEPAD_AXIS_LEFTY, -18000);
    push_gamepad_axis(input.input, 1007, SDL_GAMEPAD_AXIS_RIGHTX, -22000);
    push_gamepad_axis(input.input, 1007, SDL_GAMEPAD_AXIS_RIGHTY, 23000);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1007, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1007, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1007, SDL_GAMEPAD_BUTTON_SOUTH);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1007, SDL_GAMEPAD_BUTTON_START);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1007, SDL_GAMEPAD_BUTTON_BACK);
    sdl3d_input_update(input.input, 1);

    EXPECT_GE(sdl3d_input_gamepad_count(input.input), 1);
    const int slot = find_gamepad_slot(input.input, 1007);
    ASSERT_GE(slot, 0);
    EXPECT_TRUE(sdl3d_input_is_gamepad_left_stick_pressed(input.input, slot));
    EXPECT_TRUE(sdl3d_input_is_gamepad_right_stick_pressed(input.input, slot));
    EXPECT_TRUE(sdl3d_input_is_gamepad_face_button_pressed(input.input, slot));
    EXPECT_TRUE(sdl3d_input_is_gamepad_start_pressed(input.input, slot));
    EXPECT_TRUE(sdl3d_input_is_gamepad_select_pressed(input.input, slot));

    sdl3d_vec2 left = sdl3d_input_get_gamepad_left_stick(input.input, slot);
    sdl3d_vec2 right = sdl3d_input_get_gamepad_right_stick(input.input, slot);
    EXPECT_GT(left.x, 0.0f);
    EXPECT_LT(left.y, 0.0f);
    EXPECT_LT(right.x, 0.0f);
    EXPECT_GT(right.y, 0.0f);
}

TEST(Input, GamepadHotplugSupportsMultipleSlots)
{
    InputPtr input;
    if (sdl3d_input_gamepad_count(input.input) != 0)
    {
        GTEST_SKIP() << "requires no pre-connected gamepads";
    }
    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1011);
    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1012);
    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1013);
    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1014);
    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1015);
    sdl3d_input_update(input.input, 1);

    EXPECT_EQ(sdl3d_input_gamepad_count(input.input), 4);
    EXPECT_TRUE(sdl3d_input_gamepad_is_connected(input.input, 0));
    EXPECT_TRUE(sdl3d_input_gamepad_is_connected(input.input, 1));
    EXPECT_TRUE(sdl3d_input_gamepad_is_connected(input.input, 2));
    EXPECT_TRUE(sdl3d_input_gamepad_is_connected(input.input, 3));

    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_REMOVED, 1012);
    sdl3d_input_update(input.input, 2);
    EXPECT_EQ(sdl3d_input_gamepad_count(input.input), 3);

    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1016);
    sdl3d_input_update(input.input, 3);
    EXPECT_EQ(sdl3d_input_gamepad_count(input.input), 4);
}

TEST(Input, UiDefaultsRespondToGamepadDirectionsAndBack)
{
    InputPtr input;
    sdl3d_input_bind_ui_defaults(input.input);
    int ui_left = sdl3d_input_find_action(input.input, "ui_left");
    int ui_down = sdl3d_input_find_action(input.input, "ui_down");
    int ui_back = sdl3d_input_find_action(input.input, "ui_back");
    ASSERT_GE(ui_left, 0);
    ASSERT_GE(ui_down, 0);
    ASSERT_GE(ui_back, 0);

    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1021);
    push_gamepad_axis(input.input, 1021, SDL_GAMEPAD_AXIS_LEFTX, -22000);
    push_gamepad_axis(input.input, 1021, SDL_GAMEPAD_AXIS_LEFTY, 22000);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1021, SDL_GAMEPAD_BUTTON_EAST);
    sdl3d_input_update(input.input, 1);

    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, ui_left));
    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, ui_down));
    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, ui_back));
}

TEST(Input, MultipleGamepadsCanHoldTheSameAction)
{
    InputPtr input;
    if (sdl3d_input_gamepad_count(input.input) != 0)
    {
        GTEST_SKIP() << "requires no pre-connected gamepads";
    }
    sdl3d_input_bind_ui_defaults(input.input);
    int accept = sdl3d_input_find_action(input.input, "ui_accept");
    ASSERT_GE(accept, 0);

    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1031);
    push_gamepad_device(input.input, SDL_EVENT_GAMEPAD_ADDED, 1032);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1031, SDL_GAMEPAD_BUTTON_SOUTH);
    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1032, SDL_GAMEPAD_BUTTON_SOUTH);
    sdl3d_input_update(input.input, 1);
    EXPECT_TRUE(sdl3d_input_is_held(input.input, accept));

    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_UP, 1031, SDL_GAMEPAD_BUTTON_SOUTH);
    sdl3d_input_update(input.input, 2);
    EXPECT_TRUE(sdl3d_input_is_held(input.input, accept));

    push_gamepad_button(input.input, SDL_EVENT_GAMEPAD_BUTTON_UP, 1032, SDL_GAMEPAD_BUTTON_SOUTH);
    sdl3d_input_update(input.input, 3);
    EXPECT_FALSE(sdl3d_input_is_held(input.input, accept));
}

TEST(Input, GamepadRumbleNoOpsWithoutConnectedDevices)
{
    InputPtr input;
    EXPECT_FALSE(sdl3d_input_rumble_gamepad(input.input, 0, 0.5f, 0.5f, 50));
    EXPECT_FALSE(sdl3d_input_rumble_all_gamepads(input.input, 0.5f, 0.5f, 50));
}

TEST(Input, MultipleBindingsSameAction)
{
    InputPtr input;
    int action = sdl3d_input_register_action(input.input, "move_forward");
    sdl3d_input_bind_key(input.input, action, SDL_SCANCODE_W);
    sdl3d_input_bind_key(input.input, action, SDL_SCANCODE_UP);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_UP);
    sdl3d_input_update(input.input, 1);

    EXPECT_TRUE(sdl3d_input_is_held(input.input, action));
}

TEST(Input, MultipleKeyboardActionsShareOneSnapshot)
{
    InputPtr input;
    int toggle_lighting = sdl3d_input_register_action(input.input, "toggle_lighting");
    int toggle_debug = sdl3d_input_register_action(input.input, "toggle_debug");
    sdl3d_input_bind_key(input.input, toggle_lighting, SDL_SCANCODE_L);
    sdl3d_input_bind_key(input.input, toggle_debug, SDL_SCANCODE_F1);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_L);
    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_F1);
    sdl3d_input_update(input.input, 1);

    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, toggle_lighting));
    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, toggle_debug));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, toggle_lighting));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, toggle_debug));

    sdl3d_input_update(input.input, 2);
    EXPECT_FALSE(sdl3d_input_is_pressed(input.input, toggle_lighting));
    EXPECT_FALSE(sdl3d_input_is_pressed(input.input, toggle_debug));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, toggle_lighting));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, toggle_debug));
}

TEST(Input, AxisPairComposesFourActions)
{
    InputPtr input;
    int left = sdl3d_input_register_action(input.input, "left");
    int right = sdl3d_input_register_action(input.input, "right");
    int down = sdl3d_input_register_action(input.input, "down");
    int up = sdl3d_input_register_action(input.input, "up");
    sdl3d_input_bind_key(input.input, right, SDL_SCANCODE_D);
    sdl3d_input_bind_key(input.input, up, SDL_SCANCODE_W);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_D);
    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W);
    sdl3d_input_update(input.input, 1);

    sdl3d_vec2 axis = sdl3d_input_get_axis_pair(input.input, left, right, down, up);
    EXPECT_FLOAT_EQ(1.0f, axis.x);
    EXPECT_FLOAT_EQ(1.0f, axis.y);
}

TEST(Input, UnbindActionRemovesBindings)
{
    InputPtr input;
    int action = sdl3d_input_register_action(input.input, "jump");
    sdl3d_input_bind_key(input.input, action, SDL_SCANCODE_SPACE);
    sdl3d_input_unbind_action(input.input, action);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE);
    sdl3d_input_update(input.input, 1);

    EXPECT_FALSE(sdl3d_input_is_held(input.input, action));
}

TEST(Input, ActionOverrideReplacesLiveBindings)
{
    InputPtr input;
    int action = sdl3d_input_register_action(input.input, "move");
    sdl3d_input_bind_key(input.input, action, SDL_SCANCODE_W);

    sdl3d_input_set_action_override(input.input, action, 1.0f);
    sdl3d_input_update(input.input, 1);
    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, action));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, action));
    EXPECT_FLOAT_EQ(1.0f, sdl3d_input_get_value(input.input, action));

    sdl3d_input_clear_action_override(input.input, action);
    sdl3d_input_update(input.input, 2);
    EXPECT_FALSE(sdl3d_input_is_held(input.input, action));
    EXPECT_FALSE(sdl3d_input_is_pressed(input.input, action));

    sdl3d_input_set_action_override(input.input, action, -0.5f);
    sdl3d_input_update(input.input, 3);
    EXPECT_TRUE(sdl3d_input_is_held(input.input, action));
    EXPECT_FLOAT_EQ(-0.5f, sdl3d_input_get_value(input.input, action));

    sdl3d_input_clear_action_overrides(input.input);
    sdl3d_input_update(input.input, 4);
    EXPECT_FALSE(sdl3d_input_is_held(input.input, action));
}

TEST(Input, AnyPressedTracksUnboundAndBoundButtonLikeInput)
{
    InputPtr input;
    int fire = sdl3d_input_register_action(input.input, "fire");
    sdl3d_input_bind_mouse_button(input.input, fire, SDL_BUTTON_LEFT);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_F9);
    const sdl3d_input_snapshot *unbound_key = sdl3d_input_update(input.input, 1);
    ASSERT_NE(unbound_key, nullptr);
    EXPECT_TRUE(unbound_key->any_pressed);
    EXPECT_TRUE(sdl3d_input_any_pressed(input.input));

    sdl3d_input_update(input.input, 2);
    EXPECT_FALSE(sdl3d_input_any_pressed(input.input));

    push_mouse_button(input.input, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
    const sdl3d_input_snapshot *bound_mouse = sdl3d_input_update(input.input, 3);
    ASSERT_NE(bound_mouse, nullptr);
    EXPECT_TRUE(bound_mouse->actions[fire].pressed);
    EXPECT_TRUE(sdl3d_input_any_pressed(input.input));
}

TEST(Input, FpsDefaultsRegisterAllActions)
{
    InputPtr input;
    sdl3d_input_bind_fps_defaults(input.input);

    const char *names[] = {"move_forward", "move_back",  "move_left", "move_right", "look_up",  "look_down",
                           "look_left",    "look_right", "jump",      "fire",       "alt_fire", "reload",
                           "interact",     "crouch",     "sprint",    "menu",       "pause"};
    for (const char *name : names)
    {
        EXPECT_GE(sdl3d_input_find_action(input.input, name), 0) << name;
    }
}

TEST(Input, UiDefaultsRegisterAllActions)
{
    InputPtr input;
    sdl3d_input_bind_ui_defaults(input.input);

    const char *names[] = {"ui_accept", "ui_back",  "ui_up",       "ui_down",
                           "ui_left",   "ui_right", "ui_tab_next", "ui_tab_prev"};
    for (const char *name : names)
    {
        EXPECT_GE(sdl3d_input_find_action(input.input, name), 0) << name;
    }
}

TEST(Input, ShiftTabTriggersPreviousTabOnly)
{
    InputPtr input;
    sdl3d_input_bind_ui_defaults(input.input);
    int tab_next = sdl3d_input_find_action(input.input, "ui_tab_next");
    int tab_prev = sdl3d_input_find_action(input.input, "ui_tab_prev");

    SDL_SetModState(SDL_KMOD_SHIFT);
    push_key_mod(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_TAB, SDL_KMOD_SHIFT);
    sdl3d_input_update(input.input, 1);
    SDL_SetModState(SDL_KMOD_NONE);

    EXPECT_FALSE(sdl3d_input_is_pressed(input.input, tab_next));
    EXPECT_FALSE(sdl3d_input_is_held(input.input, tab_next));
    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, tab_prev));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, tab_prev));
}

TEST(Input, SnapshotPointerIsStableUntilNextUpdate)
{
    InputPtr input;
    const sdl3d_input_snapshot *first = sdl3d_input_update(input.input, 1);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(1, first->tick);

    const sdl3d_input_snapshot *second = sdl3d_input_get_snapshot(input.input);
    EXPECT_EQ(first, second);

    const sdl3d_input_snapshot *third = sdl3d_input_update(input.input, 2);
    EXPECT_EQ(first, third);
    EXPECT_EQ(2, third->tick);
}

TEST(Input, NullSafety)
{
    sdl3d_input_destroy(nullptr);
    EXPECT_EQ(-1, sdl3d_input_register_action(nullptr, "x"));
    EXPECT_EQ(-1, sdl3d_input_find_action(nullptr, "x"));
    sdl3d_input_bind_key(nullptr, 0, SDL_SCANCODE_A);
    sdl3d_input_bind_key_mod_mask(nullptr, 0, SDL_SCANCODE_A, SDL3D_INPUT_MOD_SHIFT, SDL3D_INPUT_MOD_CTRL);
    sdl3d_input_bind_mouse_button(nullptr, 0, SDL_BUTTON_LEFT);
    sdl3d_input_bind_mouse_axis(nullptr, 0, SDL3D_MOUSE_AXIS_X, 1.0f);
    sdl3d_input_bind_gamepad_button(nullptr, 0, SDL_GAMEPAD_BUTTON_SOUTH);
    sdl3d_input_bind_gamepad_axis(nullptr, 0, SDL_GAMEPAD_AXIS_LEFTX, 1.0f);
    sdl3d_input_unbind_action(nullptr, 0);
    sdl3d_input_process_event(nullptr, nullptr);
    EXPECT_EQ(nullptr, sdl3d_input_update(nullptr, 0));
    EXPECT_FALSE(sdl3d_input_is_pressed(nullptr, 0));
    EXPECT_FALSE(sdl3d_input_is_released(nullptr, 0));
    EXPECT_FALSE(sdl3d_input_is_held(nullptr, 0));
    EXPECT_FLOAT_EQ(0.0f, sdl3d_input_get_value(nullptr, 0));
    EXPECT_FLOAT_EQ(0.0f, sdl3d_input_get_mouse_dx(nullptr));
    EXPECT_FLOAT_EQ(0.0f, sdl3d_input_get_mouse_dy(nullptr));
    EXPECT_EQ(nullptr, sdl3d_input_get_snapshot(nullptr));
    sdl3d_input_bind_fps_defaults(nullptr);
    sdl3d_input_bind_ui_defaults(nullptr);
    sdl3d_input_set_action_override(nullptr, 0, 1.0f);
    sdl3d_input_clear_action_override(nullptr, 0);
    sdl3d_input_clear_action_overrides(nullptr);
    sdl3d_input_set_deadzone(nullptr, 0.2f);
}

TEST(InputDemo, RecordAndPlayback)
{
    InputPtr input;
    int fire = sdl3d_input_register_action(input.input, "fire");
    sdl3d_input_bind_mouse_button(input.input, fire, SDL_BUTTON_LEFT);

    sdl3d_demo_recorder *recorder = sdl3d_demo_record_start(input.input);
    ASSERT_NE(recorder, nullptr);

    push_mouse_button(input.input, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
    const sdl3d_input_snapshot *first = sdl3d_input_update(input.input, 100);
    ASSERT_NE(first, nullptr);
    push_mouse_button(input.input, SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
    const sdl3d_input_snapshot *second = sdl3d_input_update(input.input, 101);
    ASSERT_NE(second, nullptr);
    sdl3d_demo_record_stop(recorder);

    ASSERT_EQ(2U, sdl3d_demo_record_count(recorder));
    ASSERT_TRUE(sdl3d_demo_record_snapshot(recorder, 0)->actions[fire].pressed);
    ASSERT_TRUE(sdl3d_demo_record_snapshot(recorder, 1)->actions[fire].released);

    std::string path = demo_path();
    ASSERT_TRUE(sdl3d_demo_save(recorder, path.c_str(), 1.0f / 60.0f)) << SDL_GetError();

    std::vector<unsigned char> bytes = read_binary_file(path);
    constexpr size_t kDemoHeaderSize = 24U;
    constexpr size_t kDemoSnapshotSize = 13U + SDL3D_INPUT_MAX_ACTIONS * 5U;
    ASSERT_EQ(kDemoHeaderSize + 2U * kDemoSnapshotSize, bytes.size());
    EXPECT_EQ('S', bytes[0]);
    EXPECT_EQ('D', bytes[1]);
    EXPECT_EQ('L', bytes[2]);
    EXPECT_EQ('3', bytes[3]);
    EXPECT_EQ('D', bytes[4]);
    EXPECT_EQ('E', bytes[5]);
    EXPECT_EQ('M', bytes[6]);
    EXPECT_EQ('O', bytes[7]);
    EXPECT_EQ(3U, read_u32_le(bytes, 8));
    EXPECT_EQ(2U, read_u32_le(bytes, 16));
    EXPECT_EQ(SDL3D_INPUT_MAX_ACTIONS, read_u32_le(bytes, 20));

    sdl3d_demo_record_free(recorder);

    sdl3d_demo_player *player = sdl3d_demo_playback_load(path.c_str());
    ASSERT_NE(player, nullptr) << SDL_GetError();
    EXPECT_EQ(2U, sdl3d_demo_playback_count(player));
    EXPECT_FLOAT_EQ(1.0f / 60.0f, sdl3d_demo_playback_tick_rate(player));

    InputPtr playback_input;
    sdl3d_demo_playback_start(playback_input.input, player);
    const sdl3d_input_snapshot *played_first = sdl3d_input_update(playback_input.input, 0);
    ASSERT_NE(played_first, nullptr);
    EXPECT_TRUE(played_first->actions[fire].pressed);
    EXPECT_TRUE(played_first->any_pressed);
    EXPECT_TRUE(sdl3d_input_any_pressed(playback_input.input));
    EXPECT_FALSE(sdl3d_demo_playback_finished(player));

    const sdl3d_input_snapshot *played_second = sdl3d_input_update(playback_input.input, 1);
    ASSERT_NE(played_second, nullptr);
    EXPECT_TRUE(played_second->actions[fire].released);
    EXPECT_TRUE(sdl3d_demo_playback_finished(player));

    sdl3d_demo_playback_stop(playback_input.input);
    sdl3d_demo_playback_free(player);
}

TEST(InputDemo, RecordsKeyboardActions)
{
    InputPtr input;
    int toggle_lighting = sdl3d_input_register_action(input.input, "toggle_lighting");
    sdl3d_input_bind_key(input.input, toggle_lighting, SDL_SCANCODE_L);

    sdl3d_demo_recorder *recorder = sdl3d_demo_record_start(input.input);
    ASSERT_NE(recorder, nullptr);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_L);
    ASSERT_NE(sdl3d_input_update(input.input, 7), nullptr);
    sdl3d_demo_record_stop(recorder);

    ASSERT_EQ(1U, sdl3d_demo_record_count(recorder));
    const sdl3d_input_snapshot *recorded = sdl3d_demo_record_snapshot(recorder, 0);
    ASSERT_NE(recorded, nullptr);
    EXPECT_EQ(7, recorded->tick);
    EXPECT_TRUE(recorded->actions[toggle_lighting].pressed);
    EXPECT_TRUE(recorded->actions[toggle_lighting].held);

    sdl3d_demo_record_free(recorder);
}

TEST(InputDemo, PlaybackStopResumesLiveInput)
{
    InputPtr input;
    int jump = sdl3d_input_register_action(input.input, "jump");
    sdl3d_input_bind_key(input.input, jump, SDL_SCANCODE_SPACE);

    sdl3d_demo_player *empty = nullptr;
    sdl3d_demo_playback_start(input.input, empty);
    sdl3d_demo_playback_stop(input.input);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE);
    sdl3d_input_update(input.input, 1);
    EXPECT_TRUE(sdl3d_input_is_held(input.input, jump));
}

TEST(InputDemo, PlaybackStopPreservesPendingLiveInput)
{
    InputPtr input;
    int jump = sdl3d_input_register_action(input.input, "jump");
    sdl3d_input_bind_key(input.input, jump, SDL_SCANCODE_SPACE);

    sdl3d_demo_recorder *recorder = sdl3d_demo_record_start(input.input);
    ASSERT_NE(recorder, nullptr);
    ASSERT_NE(sdl3d_input_update(input.input, 0), nullptr);
    sdl3d_demo_record_stop(recorder);

    std::string path = demo_path();
    ASSERT_TRUE(sdl3d_demo_save(recorder, path.c_str(), 1.0f / 60.0f)) << SDL_GetError();
    sdl3d_demo_record_free(recorder);

    sdl3d_demo_player *player = sdl3d_demo_playback_load(path.c_str());
    ASSERT_NE(player, nullptr) << SDL_GetError();
    sdl3d_demo_playback_start(input.input, player);

    push_key(input.input, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE);
    sdl3d_demo_playback_stop(input.input);

    sdl3d_input_update(input.input, 1);
    EXPECT_TRUE(sdl3d_input_is_pressed(input.input, jump));
    EXPECT_TRUE(sdl3d_input_is_held(input.input, jump));

    sdl3d_demo_playback_free(player);
}

TEST(InputManager, CapturesTextInputForCurrentTick)
{
    InputPtr input;

    push_text(input.input, "abc");
    push_text(input.input, ".42");
    ASSERT_NE(sdl3d_input_update(input.input, 1), nullptr);
    EXPECT_STREQ(sdl3d_input_get_text_input(input.input), "abc.42");

    ASSERT_NE(sdl3d_input_update(input.input, 2), nullptr);
    EXPECT_STREQ(sdl3d_input_get_text_input(input.input), "");
}

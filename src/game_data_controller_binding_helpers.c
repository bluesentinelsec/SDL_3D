/**
 * @file game_data_controller_binding_helpers.c
 * @brief Controller action and signal binding helper functions.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

int fps_controller_action_id(const slayer3d_game_data_runtime *runtime, yyjson_val *component, const char *name)
{
    yyjson_val *actions = obj_get(component, "actions");
    const char *action = json_string(actions, name, NULL);
    return slayer3d_game_data_find_action(runtime, action);
}

float fps_controller_action_value(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                  int action_id)
{
    if (input == NULL || action_id < 0 || !slayer3d_game_data_active_scene_allows_action(runtime, action_id))
        return 0.0f;
    return slayer3d_input_get_value(input, action_id);
}

bool fps_controller_action_pressed(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                   int action_id)
{
    return input != NULL && action_id >= 0 && slayer3d_game_data_active_scene_allows_action(runtime, action_id) &&
           slayer3d_input_is_pressed(input, action_id);
}

slayer3d_actor_patrol_mode parse_patrol_mode(const char *value)
{
    if (value != NULL && SDL_strcmp(value, "ping_pong") == 0)
        return SLAYER3D_ACTOR_PATROL_PING_PONG;
    return SLAYER3D_ACTOR_PATROL_LOOP;
}

int patrol_signal_id(const slayer3d_game_data_runtime *runtime, yyjson_val *component, const char *name)
{
    yyjson_val *signals = obj_get(component, "signals");
    return slayer3d_game_data_find_signal(runtime, json_string(signals, name, NULL));
}

#ifndef SLAYER3D_GAME_DATA_CONTROLLER_INTERNAL_H
#define SLAYER3D_GAME_DATA_CONTROLLER_INTERNAL_H

#include <stdbool.h>

#include "game_data_runtime_types.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/input.h"
#include "yyjson.h"

typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

fps_controller_runtime *find_fps_controller(slayer3d_game_data_runtime *runtime, const char *entity_name);
const fps_controller_runtime *find_fps_controller_const(const slayer3d_game_data_runtime *runtime,
                                                        const char *entity_name);
fps_controller_runtime *find_or_add_fps_controller(slayer3d_game_data_runtime *runtime, const char *entity_name,
                                                   yyjson_val *component);
patrol_controller_runtime *find_patrol_controller(slayer3d_game_data_runtime *runtime, const char *entity_name);
patrol_controller_runtime *find_or_add_patrol_controller(slayer3d_game_data_runtime *runtime, const char *entity_name,
                                                         yyjson_val *component);
int fps_controller_action_id(const slayer3d_game_data_runtime *runtime, yyjson_val *component, const char *name);
float fps_controller_action_value(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                  int action_id);
bool fps_controller_action_pressed(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                   int action_id);
slayer3d_actor_patrol_mode parse_patrol_mode(const char *value);
int patrol_signal_id(const slayer3d_game_data_runtime *runtime, yyjson_val *component, const char *name);
void update_patrol_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                              slayer3d_registered_actor *actor, float dt);
void update_fps_sector_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                  slayer3d_registered_actor *actor, const slayer3d_input_manager *input, float dt);
void update_fps_brush_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                 slayer3d_registered_actor *actor, const slayer3d_input_manager *input, float dt);
void update_editor_camera_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                     slayer3d_registered_actor *actor, const slayer3d_input_manager *input, float dt);
bool execute_fps_controller_launch_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                          const slayer3d_properties *payload);
bool execute_fps_controller_teleport_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload);
bool execute_fps_controller_push_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                        const slayer3d_properties *payload);

#endif

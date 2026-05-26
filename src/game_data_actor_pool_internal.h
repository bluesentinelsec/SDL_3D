#ifndef SLAYER3D_GAME_DATA_ACTOR_POOL_INTERNAL_H
#define SLAYER3D_GAME_DATA_ACTOR_POOL_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include "game_data_actor_pool_types.h"
#include "game_data_runtime_types.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/properties.h"
#include "yyjson.h"

typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

const actor_pool_runtime *find_actor_pool_for_actor_const(const slayer3d_game_data_runtime *runtime,
                                                          const char *actor_name, int *out_index);
actor_pool_runtime *find_actor_pool(slayer3d_game_data_runtime *runtime, const char *name);
const actor_pool_runtime *find_actor_pool_const(const slayer3d_game_data_runtime *runtime, const char *name);
actor_pool_runtime *find_actor_pool_for_actor(slayer3d_game_data_runtime *runtime, const char *actor_name,
                                              int *out_index);
bool actor_pool_in_scene(const actor_pool_runtime *pool, const char *scene_name);
slayer3d_registered_actor *actor_pool_allocate(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                               int *out_index);
actor_lifecycle_state actor_pool_lifecycle_state(const actor_pool_runtime *pool, int index);
void actor_pool_set_lifecycle_state(actor_pool_runtime *pool, slayer3d_registered_actor *actor, int index,
                                    actor_lifecycle_state state);
bool actor_pool_actor_is_active(const actor_pool_runtime *pool, const slayer3d_registered_actor *actor, int index);
bool actor_pool_actor_is_available(const actor_pool_runtime *pool, const slayer3d_registered_actor *actor, int index);
int actor_pool_active_count(const slayer3d_game_data_runtime *runtime, const actor_pool_runtime *pool);
int actor_pool_available_count(const slayer3d_game_data_runtime *runtime, const actor_pool_runtime *pool);
void actor_pool_copy_reason(char *target, size_t target_size, const char *reason);
void actor_pool_note_spawn_attempt(actor_pool_runtime *pool);
void actor_pool_note_spawn_success(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool);
void actor_pool_note_spawn_failure(actor_pool_runtime *pool, const char *reason);
bool initialize_pooled_actor(actor_pool_runtime *pool, slayer3d_registered_actor *actor, int index, bool active);
bool actor_pool_initialize_slot(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool, int index, bool active);
bool actor_pool_request_despawn(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                slayer3d_registered_actor *actor, int index, const char *reason);

slayer3d_registered_actor *actor_from_payload_key(slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_properties *payload, const char *key);
slayer3d_vec3 actor_spawn_position_from_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                               const slayer3d_properties *payload, slayer3d_vec3 fallback,
                                               const slayer3d_registered_actor *source_actor);
void apply_actor_spawn_properties(slayer3d_registered_actor *actor, yyjson_val *properties);
void actor_set_position(slayer3d_registered_actor *actor, slayer3d_vec3 position);
slayer3d_vec3 actor_vec_property(const slayer3d_registered_actor *actor, const char *key);
float actor_numeric_property(const slayer3d_registered_actor *actor, const char *key, float fallback);
void copy_property_value(slayer3d_properties *target, const char *key, const slayer3d_value *value);
void set_actor_numeric_property(slayer3d_registered_actor *actor, const char *key, float value);

#endif

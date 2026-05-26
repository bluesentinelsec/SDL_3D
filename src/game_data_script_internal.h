#ifndef SLAYER3D_GAME_DATA_SCRIPT_INTERNAL_H
#define SLAYER3D_GAME_DATA_SCRIPT_INTERNAL_H

#include <stdbool.h>

#include "game_data_logic_types.h"
#include "game_data_runtime_types.h"
#include "script_internal.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/asset.h"
#include "slayer3d/game_data.h"
#include "slayer3d/properties.h"
#include "slayer3d/script.h"
#include "yyjson.h"

typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

adapter_entry *find_adapter(slayer3d_game_data_runtime *runtime, const char *name);
script_entry *find_script(slayer3d_game_data_runtime *runtime, const char *id);
bool append_adapter(slayer3d_game_data_runtime *runtime, const char *name, slayer3d_game_data_adapter_fn callback,
                    void *userdata);
bool set_adapter_lua_function(slayer3d_game_data_runtime *runtime, const char *name, const char *script_id,
                              const char *function_name, slayer3d_script_ref function_ref);
bool invoke_adapter(slayer3d_game_data_runtime *runtime, adapter_entry *adapter, slayer3d_registered_actor *target,
                    const slayer3d_properties *payload);
void lua_push_actor_wrapper(lua_State *lua, const slayer3d_registered_actor *actor);
void register_lua_api(slayer3d_game_data_runtime *runtime, slayer3d_script_engine *engine);
bool load_script_index_into_engine(slayer3d_game_data_runtime *runtime, slayer3d_asset_resolver *assets,
                                   slayer3d_script_engine *engine, int index, slayer3d_script_ref *module_refs,
                                   bool *loading, bool *loaded, char *error_buffer, int error_buffer_size);
bool load_scripts(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_lua_adapters(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                       int error_buffer_size);

#endif

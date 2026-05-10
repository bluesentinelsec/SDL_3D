#ifndef SLAYER3D_SCRIPT_INTERNAL_H
#define SLAYER3D_SCRIPT_INTERNAL_H

#include "slayer3d/script.h"

#include "lua.h"

lua_State *slayer3d_script_engine_lua_state(slayer3d_script_engine *engine);
bool slayer3d_script_engine_push_ref(slayer3d_script_engine *engine, slayer3d_script_ref ref);

#endif /* SLAYER3D_SCRIPT_INTERNAL_H */

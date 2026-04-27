#ifndef SDL3D_SCRIPT_INTERNAL_H
#define SDL3D_SCRIPT_INTERNAL_H

#include "sdl3d/script.h"

#include "lua.h"

lua_State *sdl3d_script_engine_lua_state(sdl3d_script_engine *engine);
bool sdl3d_script_engine_push_ref(sdl3d_script_engine *engine, sdl3d_script_ref ref);

#endif /* SDL3D_SCRIPT_INTERNAL_H */

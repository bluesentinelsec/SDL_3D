/**
 * @file script.c
 * @brief Embedded Lua scripting runtime implementation.
 */

#include "sdl3d/script.h"

#include <SDL3/SDL_stdinc.h>

#include "lauxlib.h"
#include "lualib.h"
#include "script_internal.h"

struct sdl3d_script_engine
{
    lua_State *lua;
};

static void set_script_error(char *buffer, int buffer_size, const char *message)
{
    if (buffer != NULL && buffer_size > 0)
    {
        SDL_snprintf(buffer, (size_t)buffer_size, "%s", message != NULL ? message : "unknown Lua error");
    }
}

sdl3d_script_engine *sdl3d_script_engine_create(void)
{
    sdl3d_script_engine *engine = (sdl3d_script_engine *)SDL_calloc(1, sizeof(*engine));
    if (engine == NULL)
    {
        return NULL;
    }

    engine->lua = luaL_newstate();
    if (engine->lua == NULL)
    {
        SDL_free(engine);
        return NULL;
    }

    luaL_requiref(engine->lua, "_G", luaopen_base, 1);
    lua_pop(engine->lua, 1);
    luaL_requiref(engine->lua, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(engine->lua, 1);
    luaL_requiref(engine->lua, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(engine->lua, 1);
    luaL_requiref(engine->lua, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(engine->lua, 1);
    luaL_requiref(engine->lua, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(engine->lua, 1);
    return engine;
}

void sdl3d_script_engine_destroy(sdl3d_script_engine *engine)
{
    if (engine == NULL)
    {
        return;
    }

    if (engine->lua != NULL)
    {
        lua_close(engine->lua);
    }
    SDL_free(engine);
}

bool sdl3d_script_engine_load_file(sdl3d_script_engine *engine, const char *path, char *error_buffer,
                                   int error_buffer_size)
{
    if (engine == NULL || engine->lua == NULL || path == NULL || path[0] == '\0')
    {
        set_script_error(error_buffer, error_buffer_size, "invalid Lua load arguments");
        return false;
    }

    const int result = luaL_dofile(engine->lua, path);
    if (result != LUA_OK)
    {
        set_script_error(error_buffer, error_buffer_size, lua_tostring(engine->lua, -1));
        lua_pop(engine->lua, 1);
        return false;
    }

    return true;
}

lua_State *sdl3d_script_engine_lua_state(sdl3d_script_engine *engine)
{
    return engine != NULL ? engine->lua : NULL;
}

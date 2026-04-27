/**
 * @file script.c
 * @brief Embedded Lua scripting runtime implementation.
 */

#include "sdl3d/script.h"

#include <SDL3/SDL_stdinc.h>

#include "lauxlib.h"
#include "lua.h"
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

static bool push_module_table(lua_State *lua, sdl3d_script_ref module_ref)
{
    if (lua == NULL || module_ref == SDL3D_SCRIPT_REF_INVALID)
    {
        return false;
    }

    lua_rawgeti(lua, LUA_REGISTRYINDEX, module_ref);
    if (!lua_istable(lua, -1))
    {
        lua_pop(lua, 1);
        return false;
    }
    return true;
}

static bool publish_module_global(lua_State *lua, const char *module_name)
{
    if (lua == NULL || module_name == NULL || module_name[0] == '\0' || !lua_istable(lua, -1))
    {
        return false;
    }

    char *copy = SDL_strdup(module_name);
    if (copy == NULL)
    {
        return false;
    }

    char *save = NULL;
    char *part = SDL_strtok_r(copy, ".", &save);
    if (part == NULL)
    {
        SDL_free(copy);
        return false;
    }

    char *next = SDL_strtok_r(NULL, ".", &save);
    if (next == NULL)
    {
        lua_pushvalue(lua, -1);
        lua_setglobal(lua, part);
        SDL_free(copy);
        return true;
    }

    lua_getglobal(lua, part);
    if (!lua_istable(lua, -1))
    {
        lua_pop(lua, 1);
        lua_newtable(lua);
        lua_pushvalue(lua, -1);
        lua_setglobal(lua, part);
    }

    part = next;
    next = SDL_strtok_r(NULL, ".", &save);
    while (next != NULL)
    {
        lua_getfield(lua, -1, part);
        if (!lua_istable(lua, -1))
        {
            lua_pop(lua, 1);
            lua_newtable(lua);
            lua_pushvalue(lua, -1);
            lua_setfield(lua, -3, part);
        }
        lua_remove(lua, -2);
        part = next;
        next = SDL_strtok_r(NULL, ".", &save);
    }

    lua_pushvalue(lua, -2);
    lua_setfield(lua, -2, part);
    lua_pop(lua, 1);
    SDL_free(copy);
    return true;
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

bool sdl3d_script_engine_load_module_file(sdl3d_script_engine *engine, const char *path, const char *module_name,
                                          sdl3d_script_ref *out_module_ref, char *error_buffer, int error_buffer_size)
{
    if (out_module_ref != NULL)
    {
        *out_module_ref = SDL3D_SCRIPT_REF_INVALID;
    }
    if (engine == NULL || engine->lua == NULL || path == NULL || path[0] == '\0' || module_name == NULL ||
        module_name[0] == '\0' || out_module_ref == NULL)
    {
        set_script_error(error_buffer, error_buffer_size, "invalid Lua module load arguments");
        return false;
    }

    int result = luaL_loadfile(engine->lua, path);
    if (result == LUA_OK)
        result = lua_pcall(engine->lua, 0, 1, 0);
    if (result != LUA_OK)
    {
        set_script_error(error_buffer, error_buffer_size, lua_tostring(engine->lua, -1));
        lua_pop(engine->lua, 1);
        return false;
    }

    if (!lua_istable(engine->lua, -1))
    {
        lua_pop(engine->lua, 1);
        set_script_error(error_buffer, error_buffer_size, "Lua module must return a table");
        return false;
    }

    if (!publish_module_global(engine->lua, module_name))
    {
        lua_pop(engine->lua, 1);
        set_script_error(error_buffer, error_buffer_size, "failed to publish Lua module");
        return false;
    }

    *out_module_ref = luaL_ref(engine->lua, LUA_REGISTRYINDEX);
    if (*out_module_ref == LUA_NOREF || *out_module_ref == LUA_REFNIL)
    {
        *out_module_ref = SDL3D_SCRIPT_REF_INVALID;
        set_script_error(error_buffer, error_buffer_size, "failed to store Lua module reference");
        return false;
    }

    return true;
}

bool sdl3d_script_engine_load_module_buffer(sdl3d_script_engine *engine, const void *source, size_t source_size,
                                            const char *chunk_name, const char *module_name,
                                            sdl3d_script_ref *out_module_ref, char *error_buffer, int error_buffer_size)
{
    if (out_module_ref != NULL)
        *out_module_ref = SDL3D_SCRIPT_REF_INVALID;
    if (engine == NULL || engine->lua == NULL || source == NULL || source_size == 0u || module_name == NULL ||
        module_name[0] == '\0' || out_module_ref == NULL)
    {
        set_script_error(error_buffer, error_buffer_size, "invalid Lua module load arguments");
        return false;
    }

    const char *name = chunk_name != NULL && chunk_name[0] != '\0' ? chunk_name : module_name;
    int result = luaL_loadbufferx(engine->lua, (const char *)source, source_size, name, NULL);
    if (result == LUA_OK)
        result = lua_pcall(engine->lua, 0, 1, 0);
    if (result != LUA_OK)
    {
        set_script_error(error_buffer, error_buffer_size, lua_tostring(engine->lua, -1));
        lua_pop(engine->lua, 1);
        return false;
    }

    if (!lua_istable(engine->lua, -1))
    {
        lua_pop(engine->lua, 1);
        set_script_error(error_buffer, error_buffer_size, "Lua module must return a table");
        return false;
    }

    if (!publish_module_global(engine->lua, module_name))
    {
        lua_pop(engine->lua, 1);
        set_script_error(error_buffer, error_buffer_size, "failed to publish Lua module");
        return false;
    }

    *out_module_ref = luaL_ref(engine->lua, LUA_REGISTRYINDEX);
    if (*out_module_ref == LUA_NOREF || *out_module_ref == LUA_REFNIL)
    {
        *out_module_ref = SDL3D_SCRIPT_REF_INVALID;
        set_script_error(error_buffer, error_buffer_size, "failed to store Lua module reference");
        return false;
    }

    return true;
}

bool sdl3d_script_engine_ref_module_function(sdl3d_script_engine *engine, sdl3d_script_ref module_ref,
                                             const char *function_name, sdl3d_script_ref *out_function_ref,
                                             char *error_buffer, int error_buffer_size)
{
    if (out_function_ref != NULL)
    {
        *out_function_ref = SDL3D_SCRIPT_REF_INVALID;
    }
    if (engine == NULL || engine->lua == NULL || function_name == NULL || function_name[0] == '\0' ||
        out_function_ref == NULL || !push_module_table(engine->lua, module_ref))
    {
        set_script_error(error_buffer, error_buffer_size, "invalid Lua function reference arguments");
        return false;
    }

    char *copy = SDL_strdup(function_name);
    if (copy == NULL)
    {
        lua_pop(engine->lua, 1);
        set_script_error(error_buffer, error_buffer_size, "failed to copy Lua function name");
        return false;
    }

    char *save = NULL;
    char *part = SDL_strtok_r(copy, ".", &save);
    while (part != NULL && !lua_isnil(engine->lua, -1))
    {
        lua_getfield(engine->lua, -1, part);
        lua_remove(engine->lua, -2);
        part = SDL_strtok_r(NULL, ".", &save);
    }
    SDL_free(copy);

    if (!lua_isfunction(engine->lua, -1))
    {
        lua_pop(engine->lua, 1);
        set_script_error(error_buffer, error_buffer_size, "Lua module function was not found");
        return false;
    }

    *out_function_ref = luaL_ref(engine->lua, LUA_REGISTRYINDEX);
    if (*out_function_ref == LUA_NOREF || *out_function_ref == LUA_REFNIL)
    {
        *out_function_ref = SDL3D_SCRIPT_REF_INVALID;
        set_script_error(error_buffer, error_buffer_size, "failed to store Lua function reference");
        return false;
    }

    return true;
}

void sdl3d_script_engine_unref(sdl3d_script_engine *engine, sdl3d_script_ref ref)
{
    if (engine == NULL || engine->lua == NULL || ref == SDL3D_SCRIPT_REF_INVALID)
    {
        return;
    }
    luaL_unref(engine->lua, LUA_REGISTRYINDEX, ref);
}

lua_State *sdl3d_script_engine_lua_state(sdl3d_script_engine *engine)
{
    return engine != NULL ? engine->lua : NULL;
}

bool sdl3d_script_engine_push_ref(sdl3d_script_engine *engine, sdl3d_script_ref ref)
{
    if (engine == NULL || engine->lua == NULL || ref == SDL3D_SCRIPT_REF_INVALID)
    {
        return false;
    }
    lua_rawgeti(engine->lua, LUA_REGISTRYINDEX, ref);
    if (lua_isnil(engine->lua, -1))
    {
        lua_pop(engine->lua, 1);
        return false;
    }
    return true;
}

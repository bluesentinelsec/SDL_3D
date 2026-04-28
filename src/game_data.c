/**
 * @file game_data.c
 * @brief JSON-authored game data runtime implementation.
 */

#include "sdl3d/game_data.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_validation.h"
#include "lauxlib.h"
#include "lua.h"
#include "script_internal.h"
#include "sdl3d/asset.h"
#include "sdl3d/input.h"
#include "sdl3d/math.h"
#include "sdl3d/script.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/timer_pool.h"
#include "yyjson.h"

#define SDL3D_GAME_DATA_SIGNAL_BASE 20000

typedef enum game_data_sensor_type
{
    GAME_DATA_SENSOR_BOUNDS_EXIT,
    GAME_DATA_SENSOR_BOUNDS_REFLECT,
    GAME_DATA_SENSOR_CONTACT_2D,
    GAME_DATA_SENSOR_INPUT_PRESSED,
} game_data_sensor_type;

typedef struct named_signal
{
    const char *name;
    int id;
} named_signal;

typedef struct named_timer
{
    const char *name;
    float delay;
    int signal_id;
    bool repeating;
    float interval;
} named_timer;

typedef struct named_action
{
    const char *name;
    int id;
} named_action;

typedef struct adapter_entry
{
    char *name;
    char *lua_script_id;
    char *lua_function;
    sdl3d_script_ref lua_function_ref;
    sdl3d_game_data_adapter_fn callback;
    void *userdata;
} adapter_entry;

typedef struct script_entry
{
    const char *id;
    const char *path;
    const char *module;
    const char **dependencies;
    int dependency_count;
    sdl3d_script_ref module_ref;
    bool autoload;
    bool loading;
    bool loaded;
} script_entry;

typedef struct binding_entry
{
    struct sdl3d_game_data_runtime *runtime;
    yyjson_val *actions;
    int connection_id;
} binding_entry;

typedef struct sensor_entry
{
    game_data_sensor_type type;
    const char *entity;
    const char *other;
    const char *action;
    const char *axis;
    const char *side;
    float min_value;
    float max_value;
    float threshold;
    int signal_id;
    bool was_active;
} sensor_entry;

typedef struct scene_menu_state
{
    yyjson_val *menu;
    int selected_index;
    int item_count;
} scene_menu_state;

typedef struct ui_state_entry
{
    char *name;
    sdl3d_game_data_ui_state state;
} ui_state_entry;

typedef struct scene_entry
{
    yyjson_doc *doc;
    yyjson_val *root;
    const char *name;
    const char **entities;
    int entity_count;
    bool has_entity_filter;
    scene_menu_state *menus;
    int menu_count;
} scene_entry;

typedef struct sdl3d_game_data_runtime
{
    yyjson_doc *doc;
    sdl3d_game_session *session;
    named_signal *signals;
    int signal_count;
    named_timer *timers;
    int timer_count;
    named_action *actions;
    int action_count;
    adapter_entry *adapters;
    int adapter_count;
    binding_entry *bindings;
    int binding_count;
    sensor_entry *sensors;
    int sensor_count;
    sdl3d_script_engine *scripts;
    script_entry *script_entries;
    int script_count;
    sdl3d_asset_resolver *assets;
    char *base_dir;
    scene_entry *scenes;
    int scene_count;
    int active_scene_index;
    sdl3d_properties *scene_state;
    ui_state_entry *ui_states;
    int ui_state_count;
    int ui_state_capacity;
    const char *active_camera;
    float current_dt;
    unsigned int rng_state;
} sdl3d_game_data_runtime;

static void set_error(char *buffer, int buffer_size, const char *message)
{
    if (buffer != NULL && buffer_size > 0)
    {
        SDL_snprintf(buffer, (size_t)buffer_size, "%s", message != NULL ? message : "unknown game data error");
    }
}

static bool path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return SDL_strlen(path) > 2 && path[1] == ':';
}

static char *path_dirname(const char *path)
{
    if (path == NULL)
        return NULL;

    const char *last = NULL;
    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            last = p;
    }

    if (last == NULL)
        return SDL_strdup(".");

    const size_t length = (size_t)(last - path);
    if (length == 0)
        return SDL_strdup(path[0] == '\\' ? "\\" : "/");

    char *dir = (char *)SDL_malloc(length + 1);
    if (dir == NULL)
        return NULL;
    SDL_memcpy(dir, path, length);
    dir[length] = '\0';
    return dir;
}

static char *path_basename(const char *path)
{
    if (path == NULL)
        return NULL;

    const char *base = path;
    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    return SDL_strdup(base);
}

static const char *asset_path_without_scheme(const char *path)
{
    return path != NULL && SDL_strncmp(path, "asset://", 8) == 0 ? path + 8 : path;
}

static char *path_join(const char *base_dir, const char *path)
{
    if (path == NULL)
        return NULL;
    if (path_is_absolute(path) || base_dir == NULL || base_dir[0] == '\0')
        return SDL_strdup(path);

    const size_t base_len = SDL_strlen(base_dir);
    const size_t path_len = SDL_strlen(path);
    const bool needs_sep = base_len > 0 && base_dir[base_len - 1] != '/' && base_dir[base_len - 1] != '\\';
    char *joined = (char *)SDL_malloc(base_len + (needs_sep ? 1u : 0u) + path_len + 1u);
    if (joined == NULL)
        return NULL;

    SDL_memcpy(joined, base_dir, base_len);
    size_t offset = base_len;
    if (needs_sep)
        joined[offset++] = '/';
    SDL_memcpy(joined + offset, path, path_len);
    joined[offset + path_len] = '\0';
    return joined;
}

static sdl3d_actor_registry *runtime_registry(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL ? sdl3d_game_session_get_registry(runtime->session) : NULL;
}

static sdl3d_signal_bus *runtime_bus(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL ? sdl3d_game_session_get_signal_bus(runtime->session) : NULL;
}

static sdl3d_timer_pool *runtime_timers(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL ? sdl3d_game_session_get_timer_pool(runtime->session) : NULL;
}

static sdl3d_input_manager *runtime_input(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL ? sdl3d_game_session_get_input(runtime->session) : NULL;
}

static void actor_set_position(sdl3d_registered_actor *actor, sdl3d_vec3 position);
static void lua_push_actor_wrapper(lua_State *lua, const sdl3d_registered_actor *actor);
static void copy_property_value(sdl3d_properties *target, const char *key, const sdl3d_value *value);

static sdl3d_game_data_runtime *lua_runtime(lua_State *lua)
{
    return (sdl3d_game_data_runtime *)lua_touserdata(lua, lua_upvalueindex(1));
}

static sdl3d_registered_actor *lua_actor_arg(lua_State *lua, sdl3d_game_data_runtime *runtime, int index)
{
    const char *actor_name = NULL;
    if (lua_istable(lua, index))
    {
        lua_getfield(lua, index, "_name");
        actor_name = lua_tostring(lua, -1);
        lua_pop(lua, 1);
    }
    else
    {
        actor_name = luaL_checkstring(lua, index);
    }
    return sdl3d_game_data_find_actor(runtime, actor_name);
}

static int lua_get_position(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor == NULL)
    {
        lua_pushnil(lua);
        return 1;
    }

    lua_pushnumber(lua, actor->position.x);
    lua_pushnumber(lua, actor->position.y);
    lua_pushnumber(lua, actor->position.z);
    return 3;
}

static int lua_set_position(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor == NULL)
        return 0;

    const sdl3d_vec3 position = sdl3d_vec3_make((float)luaL_checknumber(lua, 2), (float)luaL_checknumber(lua, 3),
                                                (float)luaL_optnumber(lua, 4, actor->position.z));
    actor_set_position(actor, position);
    return 0;
}

static int lua_get_float(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const float fallback = (float)luaL_optnumber(lua, 3, 0.0);
    lua_pushnumber(lua, actor != NULL ? sdl3d_properties_get_float(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_float(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        sdl3d_properties_set_float(actor->props, luaL_checkstring(lua, 2), (float)luaL_checknumber(lua, 3));
    return 0;
}

static int lua_get_int(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const int fallback = (int)luaL_optinteger(lua, 3, 0);
    lua_pushinteger(lua, actor != NULL ? sdl3d_properties_get_int(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_int(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        sdl3d_properties_set_int(actor->props, luaL_checkstring(lua, 2), (int)luaL_checkinteger(lua, 3));
    return 0;
}

static int lua_get_bool(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const bool fallback = lua_toboolean(lua, 3);
    lua_pushboolean(lua, actor != NULL ? sdl3d_properties_get_bool(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_bool(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        sdl3d_properties_set_bool(actor->props, luaL_checkstring(lua, 2), lua_toboolean(lua, 3));
    return 0;
}

static int lua_get_vec3(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const sdl3d_vec3 value = actor != NULL
                                 ? sdl3d_properties_get_vec3(actor->props, key, sdl3d_vec3_make(0.0f, 0.0f, 0.0f))
                                 : sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    lua_pushnumber(lua, value.x);
    lua_pushnumber(lua, value.y);
    lua_pushnumber(lua, value.z);
    return 3;
}

static int lua_set_vec3(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
    {
        sdl3d_properties_set_vec3(actor->props, luaL_checkstring(lua, 2),
                                  sdl3d_vec3_make((float)luaL_checknumber(lua, 3), (float)luaL_checknumber(lua, 4),
                                                  (float)luaL_optnumber(lua, 5, 0.0)));
    }
    return 0;
}

static int lua_get_dt(lua_State *lua)
{
    lua_pushnumber(lua, sdl3d_game_data_delta_time(lua_runtime(lua)));
    return 1;
}

static int lua_state_get(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    const sdl3d_value *value =
        sdl3d_properties_get_value(sdl3d_game_data_scene_state(runtime), luaL_checkstring(lua, 1));
    if (value == NULL)
    {
        lua_pushvalue(lua, 2);
        return 1;
    }

    switch (value->type)
    {
    case SDL3D_VALUE_INT:
        lua_pushinteger(lua, value->as_int);
        break;
    case SDL3D_VALUE_FLOAT:
        lua_pushnumber(lua, value->as_float);
        break;
    case SDL3D_VALUE_BOOL:
        lua_pushboolean(lua, value->as_bool);
        break;
    case SDL3D_VALUE_STRING:
        lua_pushstring(lua, value->as_string != NULL ? value->as_string : "");
        break;
    case SDL3D_VALUE_VEC3:
        lua_newtable(lua);
        lua_pushnumber(lua, value->as_vec3.x);
        lua_setfield(lua, -2, "x");
        lua_pushnumber(lua, value->as_vec3.y);
        lua_setfield(lua, -2, "y");
        lua_pushnumber(lua, value->as_vec3.z);
        lua_setfield(lua, -2, "z");
        break;
    case SDL3D_VALUE_COLOR:
        lua_newtable(lua);
        lua_pushinteger(lua, value->as_color.r);
        lua_setfield(lua, -2, "r");
        lua_pushinteger(lua, value->as_color.g);
        lua_setfield(lua, -2, "g");
        lua_pushinteger(lua, value->as_color.b);
        lua_setfield(lua, -2, "b");
        lua_pushinteger(lua, value->as_color.a);
        lua_setfield(lua, -2, "a");
        break;
    }
    return 1;
}

static int lua_state_set(lua_State *lua)
{
    sdl3d_properties *state = sdl3d_game_data_mutable_scene_state(lua_runtime(lua));
    const char *key = luaL_checkstring(lua, 1);
    if (state == NULL)
        return 0;

    if (lua_isnoneornil(lua, 2))
    {
        sdl3d_properties_remove(state, key);
    }
    else if (lua_isboolean(lua, 2))
    {
        sdl3d_properties_set_bool(state, key, lua_toboolean(lua, 2));
    }
    else if (lua_isinteger(lua, 2))
    {
        sdl3d_properties_set_int(state, key, (int)lua_tointeger(lua, 2));
    }
    else if (lua_isnumber(lua, 2))
    {
        sdl3d_properties_set_float(state, key, (float)lua_tonumber(lua, 2));
    }
    else if (lua_isstring(lua, 2))
    {
        sdl3d_properties_set_string(state, key, lua_tostring(lua, 2));
    }
    else if (lua_istable(lua, 2))
    {
        lua_getfield(lua, 2, "x");
        lua_getfield(lua, 2, "y");
        lua_getfield(lua, 2, "z");
        const bool has_xy = lua_isnumber(lua, -3) && lua_isnumber(lua, -2);
        const sdl3d_vec3 value = sdl3d_vec3_make((float)lua_tonumber(lua, -3), (float)lua_tonumber(lua, -2),
                                                 lua_isnumber(lua, -1) ? (float)lua_tonumber(lua, -1) : 0.0f);
        lua_pop(lua, 3);
        if (has_xy)
            sdl3d_properties_set_vec3(state, key, value);
    }
    return 0;
}

static int lua_random(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    if (runtime == NULL)
    {
        lua_pushnumber(lua, 0.0);
        return 1;
    }

    runtime->rng_state = runtime->rng_state * 1664525u + 1013904223u;
    lua_pushnumber(lua, (lua_Number)((runtime->rng_state >> 8) & 0x00FFFFFFu) / (lua_Number)0x01000000u);
    return 1;
}

static int lua_actor_with_tags(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    const int arg_count = lua_gettop(lua);
    const char *tags[16];
    int tag_count = 0;
    if (lua_istable(lua, 1))
    {
        const lua_Integer count = luaL_len(lua, 1);
        for (lua_Integer i = 1; i <= count && tag_count < (int)SDL_arraysize(tags); ++i)
        {
            lua_geti(lua, 1, i);
            const char *tag = lua_tostring(lua, -1);
            if (tag != NULL && tag[0] != '\0')
                tags[tag_count++] = tag;
            lua_pop(lua, 1);
        }
    }
    else
    {
        for (int i = 1; i <= arg_count && tag_count < (int)SDL_arraysize(tags); ++i)
        {
            const char *tag = lua_tostring(lua, i);
            if (tag != NULL && tag[0] != '\0')
                tags[tag_count++] = tag;
        }
    }

    lua_push_actor_wrapper(lua, sdl3d_game_data_find_actor_with_tags(runtime, tags, tag_count));
    return 1;
}

static int lua_log(lua_State *lua)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[lua] %s", luaL_checkstring(lua, 1));
    return 0;
}

static void install_lua_helpers(lua_State *lua)
{
    static const char *source_parts[] = {
        "local Actor = {}\n"
        "local Vec3 = {}\n"
        "local Vec3_mt = { __index = Vec3 }\n"
        "local function as_vec3(value, fallback)\n"
        "    if value == nil then return fallback or Vec3(0, 0, 0) end\n"
        "    if getmetatable(value) == Vec3_mt then return value end\n"
        "    return Vec3(value.x or value[1] or 0, value.y or value[2] or 0, value.z or value[3] or 0)\n"
        "end\n"
        "function Vec3.new(x, y, z)\n"
        "    return setmetatable({ x = x or 0, y = y or 0, z = z or 0 }, Vec3_mt)\n"
        "end\n"
        "setmetatable(Vec3, { __call = function(_, x, y, z) return Vec3.new(x, y, z) end })\n"
        "function Vec3.length(v)\n"
        "    v = as_vec3(v)\n"
        "    return math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)\n"
        "end\n"
        "function Vec3.normalize(v)\n"
        "    v = as_vec3(v)\n"
        "    local len = Vec3.length(v)\n"
        "    if len <= 0.000001 then return Vec3(0, 0, 0) end\n"
        "    return Vec3(v.x / len, v.y / len, v.z / len)\n"
        "end\n"
        "function Vec3.clamp(v, lo, hi)\n"
        "    v = as_vec3(v)\n"
        "    return Vec3(math.clamp(v.x, lo, hi), math.clamp(v.y, lo, hi), math.clamp(v.z, lo, hi))\n"
        "end\n"
        "function Vec3_mt.__add(a, b)\n"
        "    a, b = as_vec3(a), as_vec3(b)\n"
        "    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z)\n"
        "end\n"
        "function Vec3_mt.__sub(a, b)\n"
        "    a, b = as_vec3(a), as_vec3(b)\n"
        "    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z)\n"
        "end\n"
        "function Vec3_mt.__mul(a, b)\n"
        "    if type(a) == 'number' then return Vec3(a * b.x, a * b.y, a * b.z) end\n"
        "    if type(b) == 'number' then return Vec3(a.x * b, a.y * b, a.z * b) end\n"
        "    return Vec3(a.x * b.x, a.y * b.y, a.z * b.z)\n"
        "end\n"
        "function math.clamp(value, lo, hi)\n"
        "    if value < lo then return lo end\n"
        "    if value > hi then return hi end\n"
        "    return value\n"
        "end\n"
        "function math.lerp(a, b, t)\n"
        "    return a + (b - a) * t\n"
        "end\n",
        "function Actor:get_float(key, fallback) return sdl3d.get_float(self, key, fallback or 0) end\n"
        "function Actor:set_float(key, value) sdl3d.set_float(self, key, value) end\n"
        "function Actor:get_int(key, fallback) return sdl3d.get_int(self, key, fallback or 0) end\n"
        "function Actor:set_int(key, value) sdl3d.set_int(self, key, value) end\n"
        "function Actor:get_bool(key, fallback) return sdl3d.get_bool(self, key, fallback or false) end\n"
        "function Actor:set_bool(key, value) sdl3d.set_bool(self, key, value and true or false) end\n"
        "function Actor:get_vec3(key, fallback)\n"
        "    local x, y, z = sdl3d.get_vec3(self, key)\n"
        "    if x == nil then return fallback end\n"
        "    return Vec3(x, y, z)\n"
        "end\n"
        "function Actor:set_vec3(key, value)\n"
        "    value = as_vec3(value)\n"
        "    sdl3d.set_vec3(self, key, value.x, value.y, value.z)\n"
        "end\n"
        "function Actor:get_position()\n"
        "    local x, y, z = sdl3d.get_position(self)\n"
        "    if x == nil then return nil end\n"
        "    return Vec3(x, y, z)\n"
        "end\n"
        "function Actor:set_position(value)\n"
        "    value = as_vec3(value)\n"
        "    sdl3d.set_position(self, value.x, value.y, value.z)\n"
        "end\n"
        "Actor.__index = function(self, key)\n"
        "    if key == 'name' then return rawget(self, '_name') end\n"
        "    if key == 'position' then return Actor.get_position(self) end\n"
        "    if key == 'velocity' then return Actor.get_vec3(self, 'velocity', Vec3(0, 0, 0)) end\n"
        "    return Actor[key]\n"
        "end\n"
        "Actor.__newindex = function(self, key, value)\n"
        "    if key == 'position' then Actor.set_position(self, value); return end\n"
        "    if key == 'velocity' then Actor.set_vec3(self, 'velocity', value); return end\n"
        "    rawset(self, key, value)\n"
        "end\n"
        "function sdl3d.actor(name)\n"
        "    if name == nil or name == '' then return nil end\n"
        "    return setmetatable({ _name = name }, Actor)\n"
        "end\n"
        "function sdl3d._context(adapter, dt)\n"
        "    return {\n"
        "        adapter = adapter,\n"
        "        name = adapter,\n"
        "        dt = dt or 0,\n"
        "        actor = function(self_or_name, maybe_name) return sdl3d.actor(maybe_name or self_or_name) end,\n"
        "        actor_with_tags = function(self_or_tag, maybe_tag, ...)\n"
        "            if type(self_or_tag) == 'table' and self_or_tag.adapter ~= nil then\n"
        "                return sdl3d.actor_with_tags(maybe_tag, ...)\n"
        "            end\n"
        "            if type(self_or_tag) == 'table' then return sdl3d.actor_with_tags(self_or_tag) end\n"
        "            return sdl3d.actor_with_tags(self_or_tag, maybe_tag, ...)\n"
        "        end,\n"
        "        state_get = function(self_or_key, maybe_key, fallback)\n"
        "            if type(self_or_key) == 'table' and self_or_key.adapter ~= nil then\n"
        "                return sdl3d.state_get(maybe_key, fallback)\n"
        "            end\n"
        "            return sdl3d.state_get(self_or_key, maybe_key)\n"
        "        end,\n"
        "        state_set = function(self_or_key, maybe_key, maybe_value)\n"
        "            if type(self_or_key) == 'table' and self_or_key.adapter ~= nil then\n"
        "                sdl3d.state_set(maybe_key, maybe_value); return\n"
        "            end\n"
        "            sdl3d.state_set(self_or_key, maybe_key)\n"
        "        end,\n"
        "        random = function(_) return sdl3d.random() end,\n"
        "        log = function(self_or_message, maybe_message) sdl3d.log(maybe_message or self_or_message) end,\n"
        "    }\n"
        "end\n"
        "sdl3d.Actor = Actor\n"
        "sdl3d.Vec3 = Vec3\n"
        "sdl3d.api = 'sdl3d.lua.v1'\n"
        "_G.Vec3 = Vec3\n",
    };

    size_t source_len = 0;
    for (size_t i = 0; i < SDL_arraysize(source_parts); ++i)
        source_len += SDL_strlen(source_parts[i]);

    char *source = (char *)SDL_malloc(source_len + 1u);
    if (source == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[lua] failed to allocate gameplay API source");
        return;
    }

    size_t offset = 0;
    for (size_t i = 0; i < SDL_arraysize(source_parts); ++i)
    {
        const size_t part_len = SDL_strlen(source_parts[i]);
        SDL_memcpy(source + offset, source_parts[i], part_len);
        offset += part_len;
    }
    source[offset] = '\0';

    if (luaL_dostring(lua, source) != LUA_OK)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[lua] failed to install gameplay API: %s", lua_tostring(lua, -1));
        lua_pop(lua, 1);
    }
    SDL_free(source);
}

static void register_lua_api(sdl3d_game_data_runtime *runtime, sdl3d_script_engine *engine)
{
    if (runtime == NULL || engine == NULL)
        return;

    lua_State *lua = sdl3d_script_engine_lua_state(engine);
    if (lua == NULL)
        return;

    lua_newtable(lua);
#define SDL3D_LUA_BIND(name, fn)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        lua_pushlightuserdata(lua, runtime);                                                                           \
        lua_pushcclosure(lua, (fn), 1);                                                                                \
        lua_setfield(lua, -2, (name));                                                                                 \
    } while (0)
    SDL3D_LUA_BIND("get_position", lua_get_position);
    SDL3D_LUA_BIND("set_position", lua_set_position);
    SDL3D_LUA_BIND("get_float", lua_get_float);
    SDL3D_LUA_BIND("set_float", lua_set_float);
    SDL3D_LUA_BIND("get_int", lua_get_int);
    SDL3D_LUA_BIND("set_int", lua_set_int);
    SDL3D_LUA_BIND("get_bool", lua_get_bool);
    SDL3D_LUA_BIND("set_bool", lua_set_bool);
    SDL3D_LUA_BIND("get_vec3", lua_get_vec3);
    SDL3D_LUA_BIND("set_vec3", lua_set_vec3);
    SDL3D_LUA_BIND("dt", lua_get_dt);
    SDL3D_LUA_BIND("state_get", lua_state_get);
    SDL3D_LUA_BIND("state_set", lua_state_set);
    SDL3D_LUA_BIND("random", lua_random);
    SDL3D_LUA_BIND("actor_with_tags", lua_actor_with_tags);
    SDL3D_LUA_BIND("log", lua_log);
#undef SDL3D_LUA_BIND
    lua_setglobal(lua, "sdl3d");
    install_lua_helpers(lua);
}

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return yyjson_is_obj(object) ? yyjson_obj_get(object, key) : NULL;
}

static const char *json_string(yyjson_val *object, const char *key, const char *fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : fallback;
}

static bool json_bool(yyjson_val *object, const char *key, bool fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_bool(value) ? yyjson_get_bool(value) : fallback;
}

static float json_float(yyjson_val *object, const char *key, float fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_num(value) ? (float)yyjson_get_num(value) : fallback;
}

static int json_int(yyjson_val *object, const char *key, int fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_int(value) ? (int)yyjson_get_int(value) : fallback;
}

static sdl3d_vec3 json_vec3_value(yyjson_val *value, sdl3d_vec3 fallback)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 2)
        return fallback;

    yyjson_val *x = yyjson_arr_get(value, 0);
    yyjson_val *y = yyjson_arr_get(value, 1);
    yyjson_val *z = yyjson_arr_get(value, 2);
    if (!yyjson_is_num(x) || !yyjson_is_num(y))
        return fallback;

    return sdl3d_vec3_make((float)yyjson_get_num(x), (float)yyjson_get_num(y),
                           yyjson_is_num(z) ? (float)yyjson_get_num(z) : fallback.z);
}

static sdl3d_vec3 json_vec3(yyjson_val *object, const char *key, sdl3d_vec3 fallback)
{
    return json_vec3_value(obj_get(object, key), fallback);
}

static sdl3d_color json_color_value(yyjson_val *value, sdl3d_color fallback)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 3)
        return fallback;

    yyjson_val *r = yyjson_arr_get(value, 0);
    yyjson_val *g = yyjson_arr_get(value, 1);
    yyjson_val *b = yyjson_arr_get(value, 2);
    yyjson_val *a = yyjson_arr_get(value, 3);
    if (!yyjson_is_num(r) || !yyjson_is_num(g) || !yyjson_is_num(b))
        return fallback;

    return (sdl3d_color){
        (Uint8)SDL_clamp((int)yyjson_get_num(r), 0, 255),
        (Uint8)SDL_clamp((int)yyjson_get_num(g), 0, 255),
        (Uint8)SDL_clamp((int)yyjson_get_num(b), 0, 255),
        yyjson_is_num(a) ? (Uint8)SDL_clamp((int)yyjson_get_num(a), 0, 255) : fallback.a,
    };
}

static sdl3d_color json_color(yyjson_val *object, const char *key, sdl3d_color fallback)
{
    return json_color_value(obj_get(object, key), fallback);
}

static yyjson_val *runtime_root(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->doc != NULL ? yyjson_doc_get_root(runtime->doc) : NULL;
}

static scene_entry *active_scene_entry(sdl3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->active_scene_index < 0 || runtime->active_scene_index >= runtime->scene_count)
        return NULL;
    return &runtime->scenes[runtime->active_scene_index];
}

static const scene_entry *active_scene_entry_const(const sdl3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->active_scene_index < 0 || runtime->active_scene_index >= runtime->scene_count)
        return NULL;
    return &runtime->scenes[runtime->active_scene_index];
}

static scene_entry *find_scene(sdl3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->scene_count; ++i)
    {
        if (runtime->scenes[i].name != NULL && SDL_strcmp(runtime->scenes[i].name, name) == 0)
            return &runtime->scenes[i];
    }
    return NULL;
}

static const scene_entry *find_scene_const(const sdl3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->scene_count; ++i)
    {
        if (runtime->scenes[i].name != NULL && SDL_strcmp(runtime->scenes[i].name, name) == 0)
            return &runtime->scenes[i];
    }
    return NULL;
}

static scene_menu_state *find_scene_menu(scene_entry *scene, const char *name)
{
    if (scene == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < scene->menu_count; ++i)
    {
        const char *menu_name = json_string(scene->menus[i].menu, "name", NULL);
        if (menu_name != NULL && SDL_strcmp(menu_name, name) == 0)
            return &scene->menus[i];
    }
    return NULL;
}

static const scene_menu_state *find_scene_menu_const(const scene_entry *scene, const char *name)
{
    if (scene == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < scene->menu_count; ++i)
    {
        const char *menu_name = json_string(scene->menus[i].menu, "name", NULL);
        if (menu_name != NULL && SDL_strcmp(menu_name, name) == 0)
            return &scene->menus[i];
    }
    return NULL;
}

static bool scene_has_entity(const scene_entry *scene, const char *entity_name)
{
    if (scene == NULL || !scene->has_entity_filter)
        return true;
    if (entity_name == NULL)
        return false;
    for (int i = 0; i < scene->entity_count; ++i)
    {
        if (scene->entities[i] != NULL && SDL_strcmp(scene->entities[i], entity_name) == 0)
            return true;
    }
    return false;
}

static bool active_scene_has_entity_internal(const sdl3d_game_data_runtime *runtime, const char *entity_name)
{
    return scene_has_entity(active_scene_entry_const(runtime), entity_name);
}

static void apply_scene_camera(sdl3d_game_data_runtime *runtime, const scene_entry *scene)
{
    const char *camera = scene != NULL ? json_string(scene->root, "camera", NULL) : NULL;
    if (runtime != NULL && camera != NULL)
        runtime->active_camera = camera;
}

static sdl3d_properties *create_scene_enter_payload(const char *from_scene, const char *to_scene,
                                                    const sdl3d_properties *payload)
{
    sdl3d_properties *enter_payload = sdl3d_properties_create();
    if (enter_payload == NULL)
        return NULL;

    const int count = sdl3d_properties_count(payload);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (sdl3d_properties_get_key_at(payload, i, &key, NULL))
            copy_property_value(enter_payload, key, sdl3d_properties_get_value(payload, key));
    }

    sdl3d_properties_set_string(enter_payload, "from_scene", from_scene != NULL ? from_scene : "");
    sdl3d_properties_set_string(enter_payload, "to_scene", to_scene != NULL ? to_scene : "");
    return enter_payload;
}

static void emit_scene_enter_signal(sdl3d_game_data_runtime *runtime, const scene_entry *scene, const char *from_scene,
                                    const sdl3d_properties *payload)
{
    if (runtime == NULL || scene == NULL)
        return;

    const int signal_id = sdl3d_game_data_find_signal(runtime, json_string(scene->root, "on_enter_signal", NULL));
    if (signal_id >= 0 && runtime_bus(runtime) != NULL)
    {
        sdl3d_properties *enter_payload = create_scene_enter_payload(from_scene, scene->name, payload);
        sdl3d_signal_emit(runtime_bus(runtime), signal_id, enter_payload);
        sdl3d_properties_destroy(enter_payload);
    }
}

static yyjson_val *find_entity_json(const sdl3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (entity_name != NULL && name != NULL && SDL_strcmp(entity_name, name) == 0)
            return entity;
    }
    return NULL;
}

static bool entity_json_has_tag(yyjson_val *entity, const char *tag)
{
    yyjson_val *tags = obj_get(entity, "tags");
    for (size_t i = 0; tag != NULL && yyjson_is_arr(tags) && i < yyjson_arr_size(tags); ++i)
    {
        yyjson_val *item = yyjson_arr_get(tags, i);
        if (yyjson_is_str(item) && SDL_strcmp(yyjson_get_str(item), tag) == 0)
            return true;
    }
    return false;
}

static bool entity_json_has_tags(yyjson_val *entity, const char *const *tags, int tag_count)
{
    if (tag_count <= 0)
        return false;
    for (int i = 0; i < tag_count; ++i)
    {
        if (!entity_json_has_tag(entity, tags[i]))
            return false;
    }
    return true;
}

static yyjson_val *find_component_json(yyjson_val *entity, const char *type)
{
    yyjson_val *components = obj_get(entity, "components");
    for (size_t i = 0; yyjson_is_arr(components) && i < yyjson_arr_size(components); ++i)
    {
        yyjson_val *component = yyjson_arr_get(components, i);
        const char *component_type = json_string(component, "type", NULL);
        if (component_type != NULL && type != NULL && SDL_strcmp(component_type, type) == 0)
            return component;
    }
    return NULL;
}

static yyjson_val *find_font_json(const sdl3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *fonts = obj_get(obj_get(runtime_root(runtime), "assets"), "fonts");
    for (size_t i = 0; id != NULL && yyjson_is_arr(fonts) && i < yyjson_arr_size(fonts); ++i)
    {
        yyjson_val *font = yyjson_arr_get(fonts, i);
        const char *font_id = json_string(font, "id", NULL);
        if (font_id != NULL && SDL_strcmp(font_id, id) == 0)
            return font;
    }
    return NULL;
}

static yyjson_val *find_image_json(const sdl3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *images = obj_get(obj_get(runtime_root(runtime), "assets"), "images");
    for (size_t i = 0; id != NULL && yyjson_is_arr(images) && i < yyjson_arr_size(images); ++i)
    {
        yyjson_val *image = yyjson_arr_get(images, i);
        const char *image_id = json_string(image, "id", NULL);
        if (image_id != NULL && SDL_strcmp(image_id, id) == 0)
            return image;
    }
    return NULL;
}

static yyjson_val *find_camera_json(const sdl3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *cameras = obj_get(obj_get(runtime_root(runtime), "world"), "cameras");
    for (size_t i = 0; yyjson_is_arr(cameras) && i < yyjson_arr_size(cameras); ++i)
    {
        yyjson_val *camera = yyjson_arr_get(cameras, i);
        const char *camera_name = json_string(camera, "name", NULL);
        if (camera_name != NULL && name != NULL && SDL_strcmp(camera_name, name) == 0)
            return camera;
    }
    return NULL;
}

static sdl3d_backend parse_backend(const char *value, sdl3d_backend fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "auto") == 0)
        return SDL3D_BACKEND_AUTO;
    if (SDL_strcasecmp(value, "software") == 0)
        return SDL3D_BACKEND_SOFTWARE;
    if (SDL_strcasecmp(value, "sdlgpu") == 0 || SDL_strcasecmp(value, "gpu") == 0)
        return SDL3D_BACKEND_SDLGPU;
    return fallback;
}

static sdl3d_tonemap_mode parse_tonemap(const char *value, sdl3d_tonemap_mode fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "none") == 0)
        return SDL3D_TONEMAP_NONE;
    if (SDL_strcasecmp(value, "reinhard") == 0)
        return SDL3D_TONEMAP_REINHARD;
    if (SDL_strcasecmp(value, "aces") == 0)
        return SDL3D_TONEMAP_ACES;
    return fallback;
}

static sdl3d_transition_type parse_transition_type(const char *value, sdl3d_transition_type fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "fade") == 0)
        return SDL3D_TRANSITION_FADE;
    if (SDL_strcasecmp(value, "circle") == 0)
        return SDL3D_TRANSITION_CIRCLE;
    if (SDL_strcasecmp(value, "melt") == 0)
        return SDL3D_TRANSITION_MELT;
    if (SDL_strcasecmp(value, "pixelate") == 0)
        return SDL3D_TRANSITION_PIXELATE;
    return fallback;
}

static sdl3d_transition_direction parse_transition_direction(const char *value, sdl3d_transition_direction fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "in") == 0)
        return SDL3D_TRANSITION_IN;
    if (SDL_strcasecmp(value, "out") == 0)
        return SDL3D_TRANSITION_OUT;
    return fallback;
}

static sdl3d_builtin_font parse_builtin_font(const char *value, sdl3d_builtin_font fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "Inter") == 0 || SDL_strcasecmp(value, "inter") == 0)
        return SDL3D_BUILTIN_FONT_INTER;
    return fallback;
}

static sdl3d_game_data_ui_align parse_ui_align(const char *value, sdl3d_game_data_ui_align fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "left") == 0)
        return SDL3D_GAME_DATA_UI_ALIGN_LEFT;
    if (SDL_strcasecmp(value, "center") == 0 || SDL_strcasecmp(value, "middle") == 0)
        return SDL3D_GAME_DATA_UI_ALIGN_CENTER;
    if (SDL_strcasecmp(value, "right") == 0)
        return SDL3D_GAME_DATA_UI_ALIGN_RIGHT;
    return fallback;
}

static sdl3d_game_data_ui_valign parse_ui_valign(const char *value, sdl3d_game_data_ui_valign fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "top") == 0)
        return SDL3D_GAME_DATA_UI_VALIGN_TOP;
    if (SDL_strcasecmp(value, "center") == 0 || SDL_strcasecmp(value, "middle") == 0)
        return SDL3D_GAME_DATA_UI_VALIGN_CENTER;
    if (SDL_strcasecmp(value, "bottom") == 0)
        return SDL3D_GAME_DATA_UI_VALIGN_BOTTOM;
    return fallback;
}

static int axis_index(const char *axis)
{
    if (axis == NULL)
        return -1;
    if (SDL_strcmp(axis, "x") == 0)
        return 0;
    if (SDL_strcmp(axis, "y") == 0)
        return 1;
    if (SDL_strcmp(axis, "z") == 0)
        return 2;
    return -1;
}

static float vec_axis(sdl3d_vec3 value, int axis)
{
    if (axis == 0)
        return value.x;
    if (axis == 1)
        return value.y;
    if (axis == 2)
        return value.z;
    return 0.0f;
}

static void set_vec_axis(sdl3d_vec3 *value, int axis, float component)
{
    if (value == NULL)
        return;
    if (axis == 0)
        value->x = component;
    else if (axis == 1)
        value->y = component;
    else if (axis == 2)
        value->z = component;
}

static sdl3d_vec3 actor_vec_property(const sdl3d_registered_actor *actor, const char *key)
{
    return actor != NULL ? sdl3d_properties_get_vec3(actor->props, key, sdl3d_vec3_make(0.0f, 0.0f, 0.0f))
                         : sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static void actor_set_position(sdl3d_registered_actor *actor, sdl3d_vec3 position)
{
    if (actor == NULL)
        return;
    actor->position = position;
    sdl3d_properties_set_vec3(actor->props, "origin", position);
}

static void copy_property_value(sdl3d_properties *target, const char *key, const sdl3d_value *value)
{
    if (target == NULL || key == NULL || value == NULL)
        return;

    switch (value->type)
    {
    case SDL3D_VALUE_INT:
        sdl3d_properties_set_int(target, key, value->as_int);
        break;
    case SDL3D_VALUE_FLOAT:
        sdl3d_properties_set_float(target, key, value->as_float);
        break;
    case SDL3D_VALUE_BOOL:
        sdl3d_properties_set_bool(target, key, value->as_bool);
        break;
    case SDL3D_VALUE_VEC3:
        sdl3d_properties_set_vec3(target, key, value->as_vec3);
        break;
    case SDL3D_VALUE_STRING:
        sdl3d_properties_set_string(target, key, value->as_string);
        break;
    case SDL3D_VALUE_COLOR:
        sdl3d_properties_set_color(target, key, value->as_color);
        break;
    }
}

static SDL_Scancode scancode_from_json(const char *name)
{
    if (name == NULL)
        return SDL_SCANCODE_UNKNOWN;
    if (SDL_strcmp(name, "UP") == 0)
        return SDL_SCANCODE_UP;
    if (SDL_strcmp(name, "DOWN") == 0)
        return SDL_SCANCODE_DOWN;
    if (SDL_strcmp(name, "LEFT") == 0)
        return SDL_SCANCODE_LEFT;
    if (SDL_strcmp(name, "RIGHT") == 0)
        return SDL_SCANCODE_RIGHT;
    if (SDL_strcmp(name, "RETURN") == 0)
        return SDL_SCANCODE_RETURN;
    if (SDL_strcmp(name, "ESCAPE") == 0)
        return SDL_SCANCODE_ESCAPE;
    if (SDL_strlen(name) == 1)
        return SDL_GetScancodeFromKey(SDL_GetKeyFromName(name), NULL);
    return SDL_GetScancodeFromName(name);
}

static SDL_GamepadAxis gamepad_axis_from_json(const char *name)
{
    if (name == NULL)
        return SDL_GAMEPAD_AXIS_INVALID;
    if (SDL_strcmp(name, "left_x") == 0)
        return SDL_GAMEPAD_AXIS_LEFTX;
    if (SDL_strcmp(name, "left_y") == 0)
        return SDL_GAMEPAD_AXIS_LEFTY;
    if (SDL_strcmp(name, "right_x") == 0)
        return SDL_GAMEPAD_AXIS_RIGHTX;
    if (SDL_strcmp(name, "right_y") == 0)
        return SDL_GAMEPAD_AXIS_RIGHTY;
    if (SDL_strcmp(name, "left_trigger") == 0)
        return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    if (SDL_strcmp(name, "right_trigger") == 0)
        return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    return SDL_GAMEPAD_AXIS_INVALID;
}

static SDL_GamepadButton gamepad_button_from_json(const char *name)
{
    if (name == NULL)
        return SDL_GAMEPAD_BUTTON_INVALID;
    if (SDL_strcmp(name, "START") == 0)
        return SDL_GAMEPAD_BUTTON_START;
    if (SDL_strcmp(name, "BACK") == 0)
        return SDL_GAMEPAD_BUTTON_BACK;
    if (SDL_strcmp(name, "SOUTH") == 0)
        return SDL_GAMEPAD_BUTTON_SOUTH;
    if (SDL_strcmp(name, "NORTH") == 0)
        return SDL_GAMEPAD_BUTTON_NORTH;
    if (SDL_strcmp(name, "EAST") == 0)
        return SDL_GAMEPAD_BUTTON_EAST;
    if (SDL_strcmp(name, "WEST") == 0)
        return SDL_GAMEPAD_BUTTON_WEST;
    if (SDL_strcmp(name, "DPAD_UP") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_UP;
    if (SDL_strcmp(name, "DPAD_DOWN") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    if (SDL_strcmp(name, "DPAD_LEFT") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    if (SDL_strcmp(name, "DPAD_RIGHT") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    return SDL_GAMEPAD_BUTTON_INVALID;
}

static int find_timer_index(const sdl3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return -1;
    for (int i = 0; i < runtime->timer_count; ++i)
    {
        if (SDL_strcmp(runtime->timers[i].name, name) == 0)
            return i;
    }
    return -1;
}

static adapter_entry *find_adapter(sdl3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->adapter_count; ++i)
    {
        if (SDL_strcmp(runtime->adapters[i].name, name) == 0)
            return &runtime->adapters[i];
    }
    return NULL;
}

static script_entry *find_script(sdl3d_game_data_runtime *runtime, const char *id)
{
    if (runtime == NULL || id == NULL)
        return NULL;
    for (int i = 0; i < runtime->script_count; ++i)
    {
        if (SDL_strcmp(runtime->script_entries[i].id, id) == 0)
            return &runtime->script_entries[i];
    }
    return NULL;
}

static bool append_adapter(sdl3d_game_data_runtime *runtime, const char *name, sdl3d_game_data_adapter_fn callback,
                           void *userdata)
{
    adapter_entry *entries =
        (adapter_entry *)SDL_realloc(runtime->adapters, (size_t)(runtime->adapter_count + 1) * sizeof(*entries));
    if (entries == NULL)
        return false;
    runtime->adapters = entries;

    adapter_entry *entry = &runtime->adapters[runtime->adapter_count];
    SDL_zero(*entry);
    entry->name = SDL_strdup(name);
    if (entry->name == NULL)
        return false;
    entry->callback = callback;
    entry->userdata = userdata;
    runtime->adapter_count++;
    return true;
}

static bool set_adapter_lua_function(sdl3d_game_data_runtime *runtime, const char *name, const char *script_id,
                                     const char *function_name, sdl3d_script_ref function_ref)
{
    if (runtime == NULL || name == NULL || name[0] == '\0' || script_id == NULL || script_id[0] == '\0' ||
        function_name == NULL || function_name[0] == '\0' || function_ref == SDL3D_SCRIPT_REF_INVALID)
        return false;

    adapter_entry *entry = find_adapter(runtime, name);
    if (entry == NULL)
    {
        if (!append_adapter(runtime, name, NULL, NULL))
            return false;
        entry = find_adapter(runtime, name);
    }
    if (entry == NULL)
        return false;

    char *script_copy = SDL_strdup(script_id);
    char *function_copy = SDL_strdup(function_name);
    if (script_copy == NULL || function_copy == NULL)
    {
        SDL_free(script_copy);
        SDL_free(function_copy);
        return false;
    }

    SDL_free(entry->lua_script_id);
    SDL_free(entry->lua_function);
    entry->lua_script_id = script_copy;
    entry->lua_function = function_copy;
    if (entry->lua_function_ref != SDL3D_SCRIPT_REF_INVALID)
        sdl3d_script_engine_unref(runtime->scripts, entry->lua_function_ref);
    entry->lua_function_ref = function_ref;
    return true;
}

static void lua_push_property_value(lua_State *lua, const sdl3d_value *value)
{
    if (value == NULL)
    {
        lua_pushnil(lua);
        return;
    }

    switch (value->type)
    {
    case SDL3D_VALUE_INT:
        lua_pushinteger(lua, value->as_int);
        break;
    case SDL3D_VALUE_FLOAT:
        lua_pushnumber(lua, value->as_float);
        break;
    case SDL3D_VALUE_BOOL:
        lua_pushboolean(lua, value->as_bool);
        break;
    case SDL3D_VALUE_STRING:
        lua_pushstring(lua, value->as_string != NULL ? value->as_string : "");
        break;
    case SDL3D_VALUE_VEC3:
        lua_newtable(lua);
        lua_pushnumber(lua, value->as_vec3.x);
        lua_setfield(lua, -2, "x");
        lua_pushnumber(lua, value->as_vec3.y);
        lua_setfield(lua, -2, "y");
        lua_pushnumber(lua, value->as_vec3.z);
        lua_setfield(lua, -2, "z");
        break;
    case SDL3D_VALUE_COLOR:
        lua_newtable(lua);
        lua_pushinteger(lua, value->as_color.r);
        lua_setfield(lua, -2, "r");
        lua_pushinteger(lua, value->as_color.g);
        lua_setfield(lua, -2, "g");
        lua_pushinteger(lua, value->as_color.b);
        lua_setfield(lua, -2, "b");
        lua_pushinteger(lua, value->as_color.a);
        lua_setfield(lua, -2, "a");
        break;
    }
}

static void lua_push_payload(lua_State *lua, const sdl3d_properties *payload)
{
    lua_newtable(lua);
    if (payload == NULL)
        return;

    const int count = sdl3d_properties_count(payload);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (!sdl3d_properties_get_key_at(payload, i, &key, NULL) || key == NULL)
            continue;
        lua_push_property_value(lua, sdl3d_properties_get_value(payload, key));
        lua_setfield(lua, -2, key);
    }
}

static void lua_push_actor_wrapper(lua_State *lua, const sdl3d_registered_actor *actor)
{
    if (actor == NULL)
    {
        lua_pushnil(lua);
        return;
    }

    lua_getglobal(lua, "sdl3d");
    if (!lua_istable(lua, -1))
    {
        lua_pop(lua, 1);
        lua_pushnil(lua);
        return;
    }
    lua_getfield(lua, -1, "actor");
    if (!lua_isfunction(lua, -1))
    {
        lua_pop(lua, 2);
        lua_pushnil(lua);
        return;
    }
    lua_pushstring(lua, actor->name);
    if (lua_pcall(lua, 1, 1, 0) != LUA_OK)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[lua] actor wrapper creation failed: %s", lua_tostring(lua, -1));
        lua_pop(lua, 2);
        lua_pushnil(lua);
        return;
    }
    lua_remove(lua, -2);
}

static void lua_push_adapter_context(lua_State *lua, const sdl3d_game_data_runtime *runtime,
                                     const adapter_entry *adapter)
{
    lua_getglobal(lua, "sdl3d");
    if (!lua_istable(lua, -1))
    {
        lua_pop(lua, 1);
        lua_newtable(lua);
        return;
    }
    lua_getfield(lua, -1, "_context");
    if (!lua_isfunction(lua, -1))
    {
        lua_pop(lua, 2);
        lua_newtable(lua);
        return;
    }
    lua_pushstring(lua, adapter != NULL ? adapter->name : "");
    lua_pushnumber(lua, runtime != NULL ? runtime->current_dt : 0.0f);
    if (lua_pcall(lua, 2, 1, 0) != LUA_OK)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[lua] adapter context creation failed: %s", lua_tostring(lua, -1));
        lua_pop(lua, 2);
        lua_newtable(lua);
        return;
    }
    lua_remove(lua, -2);
}

static bool call_lua_adapter(sdl3d_game_data_runtime *runtime, const adapter_entry *adapter,
                             sdl3d_registered_actor *target, const sdl3d_properties *payload)
{
    if (runtime == NULL || runtime->scripts == NULL || adapter == NULL ||
        adapter->lua_function_ref == SDL3D_SCRIPT_REF_INVALID)
        return false;

    lua_State *lua = sdl3d_script_engine_lua_state(runtime->scripts);
    if (lua == NULL || !sdl3d_script_engine_push_ref(runtime->scripts, adapter->lua_function_ref))
        return false;

    lua_push_actor_wrapper(lua, target);
    lua_push_payload(lua, payload);
    lua_push_adapter_context(lua, runtime, adapter);

    if (lua_pcall(lua, 3, 1, 0) != LUA_OK)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[lua] adapter %s failed: %s", adapter->name, lua_tostring(lua, -1));
        lua_pop(lua, 1);
        return false;
    }

    const bool ok = lua_isboolean(lua, -1) ? lua_toboolean(lua, -1) : true;
    lua_pop(lua, 1);
    return ok;
}

static bool invoke_adapter(sdl3d_game_data_runtime *runtime, adapter_entry *adapter, sdl3d_registered_actor *target,
                           const sdl3d_properties *payload)
{
    if (adapter == NULL)
        return false;
    if (adapter->callback != NULL)
        return adapter->callback(adapter->userdata, runtime, adapter->name, target, payload);
    return call_lua_adapter(runtime, adapter, target, payload);
}

int sdl3d_game_data_find_signal(const sdl3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return -1;
    for (int i = 0; i < runtime->signal_count; ++i)
    {
        if (SDL_strcmp(runtime->signals[i].name, name) == 0)
            return runtime->signals[i].id;
    }
    return -1;
}

int sdl3d_game_data_find_action(const sdl3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return -1;
    for (int i = 0; i < runtime->action_count; ++i)
    {
        if (SDL_strcmp(runtime->actions[i].name, name) == 0)
            return runtime->actions[i].id;
    }
    return -1;
}

static const char *find_action_name(const sdl3d_game_data_runtime *runtime, int action_id)
{
    if (runtime == NULL || action_id < 0)
        return NULL;
    for (int i = 0; i < runtime->action_count; ++i)
    {
        if (runtime->actions[i].id == action_id)
            return runtime->actions[i].name;
    }
    return NULL;
}

sdl3d_registered_actor *sdl3d_game_data_find_actor(const sdl3d_game_data_runtime *runtime, const char *name)
{
    return sdl3d_actor_registry_find(runtime_registry(runtime), name);
}

sdl3d_registered_actor *sdl3d_game_data_find_actor_with_tag(const sdl3d_game_data_runtime *runtime, const char *tag)
{
    const char *tags[1] = {tag};
    return sdl3d_game_data_find_actor_with_tags(runtime, tags, 1);
}

sdl3d_registered_actor *sdl3d_game_data_find_actor_with_tags(const sdl3d_game_data_runtime *runtime,
                                                             const char *const *tags, int tag_count)
{
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    if (runtime == NULL || tags == NULL || tag_count <= 0 || !yyjson_is_arr(entities))
        return NULL;

    for (size_t i = 0; i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        if (!entity_json_has_tags(entity, tags, tag_count))
            continue;
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(entity, "name", NULL));
        if (actor != NULL)
            return actor;
    }
    return NULL;
}

bool sdl3d_game_data_get_app_control(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_app_control *out_control)
{
    if (out_control != NULL)
    {
        out_control->start_signal_id = -1;
        out_control->quit_action_id = -1;
        out_control->pause_action_id = -1;
        out_control->startup_transition = NULL;
        out_control->quit_transition = NULL;
        out_control->quit_signal_id = -1;
    }
    if (runtime == NULL || out_control == NULL)
        return false;

    yyjson_val *app = obj_get(runtime_root(runtime), "app");
    yyjson_val *pause = obj_get(app, "pause");
    yyjson_val *quit = obj_get(app, "quit");
    out_control->start_signal_id = sdl3d_game_data_find_signal(runtime, json_string(app, "start_signal", NULL));
    out_control->pause_action_id = sdl3d_game_data_find_action(runtime, json_string(pause, "action", NULL));
    out_control->startup_transition = json_string(app, "startup_transition", NULL);
    out_control->quit_action_id = sdl3d_game_data_find_action(runtime, json_string(quit, "action", NULL));
    out_control->quit_transition = json_string(quit, "transition", NULL);
    out_control->quit_signal_id = sdl3d_game_data_find_signal(runtime, json_string(quit, "quit_signal", NULL));
    return true;
}

bool sdl3d_game_data_get_font_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                    sdl3d_game_data_font_asset *out_font)
{
    if (out_font != NULL)
    {
        SDL_zero(*out_font);
        out_font->builtin_id = SDL3D_BUILTIN_FONT_INTER;
        out_font->size = 16.0f;
    }
    if (runtime == NULL || id == NULL || out_font == NULL)
        return false;

    yyjson_val *font = find_font_json(runtime, id);
    if (!yyjson_is_obj(font))
        return false;

    out_font->id = json_string(font, "id", NULL);
    out_font->path = json_string(font, "path", NULL);
    out_font->size = json_float(font, "size", out_font->size);
    const char *builtin = json_string(font, "builtin", NULL);
    out_font->builtin = builtin != NULL;
    out_font->builtin_id = parse_builtin_font(builtin, out_font->builtin_id);
    return true;
}

bool sdl3d_game_data_get_image_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                     sdl3d_game_data_image_asset *out_image)
{
    if (out_image != NULL)
        SDL_zero(*out_image);
    if (runtime == NULL || id == NULL || out_image == NULL)
        return false;

    yyjson_val *image = find_image_json(runtime, id);
    if (!yyjson_is_obj(image))
        return false;

    out_image->id = json_string(image, "id", NULL);
    out_image->path = json_string(image, "path", NULL);
    return out_image->id != NULL && out_image->path != NULL;
}

const char *sdl3d_game_data_active_camera(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->active_camera : NULL;
}

bool sdl3d_game_data_get_camera(const sdl3d_game_data_runtime *runtime, const char *name, sdl3d_camera3d *out_camera)
{
    if (out_camera != NULL)
        SDL_zero(*out_camera);
    if (runtime == NULL || name == NULL || out_camera == NULL)
        return false;

    yyjson_val *camera_json = find_camera_json(runtime, name);
    if (camera_json == NULL)
        return false;

    const char *type = json_string(camera_json, "type", "perspective");
    if (SDL_strcmp(type, "adapter") == 0)
        return false;

    if (SDL_strcmp(type, "chase") == 0)
    {
        sdl3d_registered_actor *target =
            sdl3d_game_data_find_actor(runtime, json_string(camera_json, "target_entity", NULL));
        if (target == NULL)
            return false;

        const char *velocity_property = json_string(camera_json, "velocity_property", "velocity");
        sdl3d_vec3 velocity =
            sdl3d_properties_get_vec3(target->props, velocity_property, sdl3d_vec3_make(1.0f, 0.0f, 0.0f));
        velocity.z = 0.0f;
        float velocity_len = SDL_sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
        if (velocity_len < 0.001f)
        {
            velocity = json_vec3(camera_json, "fallback_forward", sdl3d_vec3_make(1.0f, 0.0f, 0.0f));
            velocity.z = 0.0f;
            velocity_len = SDL_sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
        }
        if (velocity_len < 0.001f)
            velocity = sdl3d_vec3_make(1.0f, 0.0f, 0.0f);
        else
        {
            velocity.x /= velocity_len;
            velocity.y /= velocity_len;
        }

        const float target_z_offset = json_float(camera_json, "target_z_offset", 0.0f);
        const float camera_height = json_float(camera_json, "height", 1.0f);
        const float chase_distance = json_float(camera_json, "chase_distance", 2.0f);
        const float lookahead = json_float(camera_json, "lookahead", 1.0f);
        const sdl3d_vec3 target_offset = json_vec3(camera_json, "target_offset", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
        const sdl3d_vec3 eye_offset = json_vec3(camera_json, "eye_offset", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
        const sdl3d_vec3 anchor =
            sdl3d_vec3_make(target->position.x + target_offset.x, target->position.y + target_offset.y,
                            target->position.z + target_offset.z);

        out_camera->position = sdl3d_vec3_make(anchor.x - velocity.x * chase_distance + eye_offset.x,
                                               anchor.y - velocity.y * chase_distance + eye_offset.y,
                                               anchor.z + camera_height + eye_offset.z);
        out_camera->target = sdl3d_vec3_make(anchor.x + velocity.x * lookahead, anchor.y + velocity.y * lookahead,
                                             anchor.z + target_z_offset);
        out_camera->up = json_vec3(camera_json, "up", sdl3d_vec3_make(0.0f, 0.0f, 1.0f));
        out_camera->fovy = json_float(camera_json, "fovy", 60.0f);
        out_camera->projection = SDL3D_CAMERA_PERSPECTIVE;
        return true;
    }

    out_camera->position = json_vec3(camera_json, "position", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    out_camera->target = json_vec3(camera_json, "target", sdl3d_vec3_make(0.0f, 0.0f, -1.0f));
    out_camera->up = json_vec3(camera_json, "up", sdl3d_vec3_make(0.0f, 1.0f, 0.0f));
    if (SDL_strcmp(type, "orthographic") == 0)
    {
        out_camera->projection = SDL3D_CAMERA_ORTHOGRAPHIC;
        out_camera->fovy = json_float(camera_json, "size", 10.0f);
    }
    else
    {
        out_camera->projection = SDL3D_CAMERA_PERSPECTIVE;
        out_camera->fovy = json_float(camera_json, "fovy", 60.0f);
    }
    return true;
}

bool sdl3d_game_data_get_camera_float(const sdl3d_game_data_runtime *runtime, const char *camera_name,
                                      const char *property_name, float *out_value)
{
    if (out_value != NULL)
        *out_value = 0.0f;
    if (runtime == NULL || camera_name == NULL || property_name == NULL || out_value == NULL)
        return false;

    yyjson_val *camera = find_camera_json(runtime, camera_name);
    yyjson_val *value = obj_get(obj_get(camera, "properties"), property_name);
    if (!yyjson_is_num(value))
        value = obj_get(camera, property_name);
    if (!yyjson_is_num(value))
        return false;

    *out_value = (float)yyjson_get_num(value);
    return true;
}

int sdl3d_game_data_world_light_count(const sdl3d_game_data_runtime *runtime)
{
    yyjson_val *lights = obj_get(obj_get(runtime_root(runtime), "world"), "lights");
    return yyjson_is_arr(lights) ? (int)yyjson_arr_size(lights) : 0;
}

bool sdl3d_game_data_get_world_ambient_light(const sdl3d_game_data_runtime *runtime, float out_rgb[3])
{
    if (out_rgb != NULL)
    {
        out_rgb[0] = 0.0f;
        out_rgb[1] = 0.0f;
        out_rgb[2] = 0.0f;
    }
    if (runtime == NULL || out_rgb == NULL)
        return false;

    yyjson_val *ambient = obj_get(obj_get(runtime_root(runtime), "world"), "ambient_light");
    if (!yyjson_is_arr(ambient) || yyjson_arr_size(ambient) < 3)
        return false;

    for (int i = 0; i < 3; ++i)
    {
        yyjson_val *channel = yyjson_arr_get(ambient, (size_t)i);
        if (!yyjson_is_num(channel))
            return false;
        out_rgb[i] = (float)yyjson_get_num(channel);
    }
    return true;
}

bool sdl3d_game_data_get_world_light(const sdl3d_game_data_runtime *runtime, int index, sdl3d_light *out_light)
{
    if (out_light != NULL)
        SDL_zero(*out_light);
    yyjson_val *lights = obj_get(obj_get(runtime_root(runtime), "world"), "lights");
    if (runtime == NULL || index < 0 || out_light == NULL || !yyjson_is_arr(lights) ||
        (size_t)index >= yyjson_arr_size(lights))
        return false;

    yyjson_val *light_json = yyjson_arr_get(lights, (size_t)index);
    const char *type = json_string(light_json, "type", "point");
    if (SDL_strcmp(type, "directional") == 0)
        out_light->type = SDL3D_LIGHT_DIRECTIONAL;
    else if (SDL_strcmp(type, "spot") == 0)
        out_light->type = SDL3D_LIGHT_SPOT;
    else
        out_light->type = SDL3D_LIGHT_POINT;

    out_light->position = json_vec3(light_json, "position", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    const char *target_entity = json_string(light_json, "target_entity", NULL);
    sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, target_entity);
    if (target != NULL)
    {
        const sdl3d_vec3 offset = json_vec3(light_json, "offset", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
        out_light->position = sdl3d_vec3_make(target->position.x + offset.x, target->position.y + offset.y,
                                              target->position.z + offset.z);
    }
    out_light->direction = json_vec3(light_json, "direction", sdl3d_vec3_make(0.0f, -1.0f, 0.0f));
    yyjson_val *color = obj_get(light_json, "color");
    out_light->color[0] = 1.0f;
    out_light->color[1] = 1.0f;
    out_light->color[2] = 1.0f;
    for (int i = 0; yyjson_is_arr(color) && i < 3; ++i)
    {
        yyjson_val *channel = yyjson_arr_get(color, (size_t)i);
        if (yyjson_is_num(channel))
            out_light->color[i] = (float)yyjson_get_num(channel);
    }
    out_light->intensity = json_float(light_json, "intensity", 1.0f);
    out_light->range = json_float(light_json, "range", 10.0f);
    out_light->inner_cutoff = json_float(light_json, "inner_cutoff", 0.0f);
    out_light->outer_cutoff = json_float(light_json, "outer_cutoff", 0.0f);
    return true;
}

static float game_data_clampf(float value, float lo, float hi);

static void light_color_lerp(float color[3], const sdl3d_vec3 target, float t)
{
    if (color == NULL)
        return;
    t = game_data_clampf(t, 0.0f, 1.0f);
    color[0] = color[0] + (target.x - color[0]) * t;
    color[1] = color[1] + (target.y - color[1]) * t;
    color[2] = color[2] + (target.z - color[2]) * t;
}

static void apply_light_effects(const sdl3d_game_data_runtime *runtime, yyjson_val *light_json,
                                const sdl3d_game_data_render_eval *eval, sdl3d_light *light)
{
    yyjson_val *effects = obj_get(light_json, "effects");
    if (runtime == NULL || light == NULL || !yyjson_is_arr(effects))
        return;

    for (size_t i = 0; i < yyjson_arr_size(effects); ++i)
    {
        yyjson_val *effect = yyjson_arr_get(effects, i);
        const char *type = json_string(effect, "type", "");
        float value = 0.0f;
        if (SDL_strcmp(type, "pulse") == 0)
        {
            const float time = eval != NULL ? eval->time : 0.0f;
            const float rate = json_float(effect, "rate", 1.0f);
            const float phase = json_float(effect, "phase", 0.0f);
            value = 0.5f + 0.5f * SDL_sinf(time * rate + phase);
        }
        else if (SDL_strcmp(type, "flash") == 0)
        {
            sdl3d_registered_actor *source = sdl3d_game_data_find_actor(runtime, json_string(effect, "source", NULL));
            const char *property = json_string(effect, "property", NULL);
            value =
                source != NULL && property != NULL ? sdl3d_properties_get_float(source->props, property, 0.0f) : 0.0f;
            value = game_data_clampf(value, 0.0f, 1.0f);
        }
        else
        {
            continue;
        }

        yyjson_val *color = obj_get(effect, "color");
        if (yyjson_is_arr(color))
        {
            const sdl3d_vec3 target =
                json_vec3_value(color, sdl3d_vec3_make(light->color[0], light->color[1], light->color[2]));
            light_color_lerp(light->color, target, value * json_float(effect, "color_blend", 1.0f));
        }
        light->intensity += json_float(effect, "intensity_add", 0.0f) * value;
        light->range += json_float(effect, "range_add", 0.0f) * value;
    }
}

bool sdl3d_game_data_get_world_light_evaluated(const sdl3d_game_data_runtime *runtime, int index,
                                               const sdl3d_game_data_render_eval *eval, sdl3d_light *out_light)
{
    if (!sdl3d_game_data_get_world_light(runtime, index, out_light))
        return false;

    yyjson_val *lights = obj_get(obj_get(runtime_root(runtime), "world"), "lights");
    yyjson_val *light_json = yyjson_is_arr(lights) ? yyjson_arr_get(lights, (size_t)index) : NULL;
    apply_light_effects(runtime, light_json, eval, out_light);
    return true;
}

static float game_data_clampf(float value, float lo, float hi)
{
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

static sdl3d_color game_data_color_lerp(sdl3d_color a, sdl3d_color b, float t)
{
    t = game_data_clampf(t, 0.0f, 1.0f);
    return (sdl3d_color){
        (Uint8)((float)a.r + ((float)b.r - (float)a.r) * t),
        (Uint8)((float)a.g + ((float)b.g - (float)a.g) * t),
        (Uint8)((float)a.b + ((float)b.b - (float)a.b) * t),
        (Uint8)((float)a.a + ((float)b.a - (float)a.a) * t),
    };
}

static void apply_render_effects(const sdl3d_game_data_runtime *runtime, yyjson_val *component,
                                 const sdl3d_game_data_render_eval *eval, sdl3d_game_data_render_primitive *primitive)
{
    yyjson_val *effects = obj_get(component, "effects");
    if (!yyjson_is_arr(effects) || primitive == NULL)
        return;

    for (size_t i = 0; i < yyjson_arr_size(effects); ++i)
    {
        yyjson_val *effect = yyjson_arr_get(effects, i);
        const char *type = json_string(effect, "type", "");
        if (SDL_strcmp(type, "flash") == 0)
        {
            sdl3d_registered_actor *source = sdl3d_game_data_find_actor(runtime, json_string(effect, "source", NULL));
            const char *property = json_string(effect, "property", NULL);
            const float value = game_data_clampf(
                source != NULL && property != NULL ? sdl3d_properties_get_float(source->props, property, 0.0f) : 0.0f,
                0.0f, 1.0f);
            primitive->color =
                game_data_color_lerp(primitive->color, json_color(effect, "color", primitive->color), value);

            const sdl3d_vec3 size_add = json_vec3(effect, "size_add", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
            const char *size_mode = json_string(effect, "size_mode", "vector");
            if (SDL_strcmp(size_mode, "minor_axis") == 0 && primitive->type == SDL3D_GAME_DATA_RENDER_CUBE)
            {
                if (primitive->size.x <= primitive->size.y)
                    primitive->size.x += size_add.x * value;
                else
                    primitive->size.y += size_add.y * value;
            }
            else
            {
                primitive->size.x += size_add.x * value;
                primitive->size.y += size_add.y * value;
                primitive->size.z += size_add.z * value;
            }

            const sdl3d_vec3 emissive = json_vec3(effect, "emissive", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
            primitive->emissive_color.x += emissive.x * value;
            primitive->emissive_color.y += emissive.y * value;
            primitive->emissive_color.z += emissive.z * value;
        }
        else if (SDL_strcmp(type, "pulse") == 0)
        {
            const float time = eval != NULL ? eval->time : 0.0f;
            const float rate = json_float(effect, "rate", 1.0f);
            const float pulse = 0.5f + 0.5f * SDL_sinf(time * rate);
            primitive->color =
                game_data_color_lerp(primitive->color, json_color(effect, "color", primitive->color), pulse);

            const sdl3d_vec3 base = json_vec3(effect, "emissive_base", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
            const sdl3d_vec3 add = json_vec3(effect, "emissive_add", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
            primitive->emissive_color.x += base.x + add.x * pulse;
            primitive->emissive_color.y += base.y + add.y * pulse;
            primitive->emissive_color.z += base.z + add.z * pulse;
        }
        else if (SDL_strcmp(type, "emissive") == 0)
        {
            const sdl3d_vec3 rgb = json_vec3(effect, "color", sdl3d_vec3_make(0.2f, 0.2f, 0.2f));
            primitive->emissive_color.x += rgb.x;
            primitive->emissive_color.y += rgb.y;
            primitive->emissive_color.z += rgb.z;
        }
    }
}

static bool for_each_render_primitive_internal(const sdl3d_game_data_runtime *runtime,
                                               const sdl3d_game_data_render_eval *eval,
                                               sdl3d_game_data_render_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, entity_name);
        yyjson_val *components = obj_get(entity, "components");
        if (actor == NULL || !actor->active || !yyjson_is_arr(components))
            continue;

        for (size_t c = 0; c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type", "");
            sdl3d_game_data_render_primitive primitive;
            SDL_zero(primitive);
            primitive.entity_name = entity_name;
            primitive.position = actor->position;
            const sdl3d_vec3 offset = json_vec3(component, "offset", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
            primitive.position.x += offset.x;
            primitive.position.y += offset.y;
            primitive.position.z += offset.z;
            primitive.color = json_color(component, "color", (sdl3d_color){255, 255, 255, 255});
            primitive.emissive = json_bool(component, "emissive", false);
            primitive.emissive_color =
                primitive.emissive ? sdl3d_vec3_make(0.2f, 0.2f, 0.2f) : sdl3d_vec3_make(0.0f, 0.0f, 0.0f);

            if (SDL_strcmp(type, "render.cube") == 0)
            {
                primitive.type = SDL3D_GAME_DATA_RENDER_CUBE;
                primitive.size = json_vec3(component, "size", sdl3d_vec3_make(1.0f, 1.0f, 1.0f));
            }
            else if (SDL_strcmp(type, "render.sphere") == 0)
            {
                primitive.type = SDL3D_GAME_DATA_RENDER_SPHERE;
                primitive.radius = json_float(component, "radius", 0.5f);
                primitive.slices = json_int(component, "slices", 16);
                primitive.rings = json_int(component, "rings", 8);
            }
            else
            {
                continue;
            }

            if (eval != NULL)
                apply_render_effects(runtime, component, eval, &primitive);
            if (!callback(userdata, &primitive))
                return true;
        }
    }
    return true;
}

bool sdl3d_game_data_for_each_render_primitive(const sdl3d_game_data_runtime *runtime,
                                               sdl3d_game_data_render_primitive_fn callback, void *userdata)
{
    return for_each_render_primitive_internal(runtime, NULL, callback, userdata);
}

bool sdl3d_game_data_for_each_render_primitive_evaluated(const sdl3d_game_data_runtime *runtime,
                                                         const sdl3d_game_data_render_eval *eval,
                                                         sdl3d_game_data_render_primitive_fn callback, void *userdata)
{
    return for_each_render_primitive_internal(runtime, eval, callback, userdata);
}

bool sdl3d_game_data_get_particle_emitter(const sdl3d_game_data_runtime *runtime, const char *entity_name,
                                          sdl3d_particle_config *out_config)
{
    if (out_config != NULL)
        SDL_zero(*out_config);
    if (runtime == NULL || entity_name == NULL || out_config == NULL)
        return false;

    yyjson_val *entity = find_entity_json(runtime, entity_name);
    yyjson_val *component = find_component_json(entity, "particles.emitter");
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, entity_name);
    if (component == NULL || actor == NULL)
        return false;

    out_config->position = actor->position;
    out_config->direction = json_vec3(component, "direction", sdl3d_vec3_make(0.0f, 1.0f, 0.0f));
    out_config->spread = json_float(component, "spread", 0.0f);
    out_config->speed_min = json_float(component, "speed_min", 0.0f);
    out_config->speed_max = json_float(component, "speed_max", 0.0f);
    out_config->lifetime_min = json_float(component, "lifetime_min", 1.0f);
    out_config->lifetime_max = json_float(component, "lifetime_max", 1.0f);
    out_config->size_start = json_float(component, "size_start", 0.05f);
    out_config->size_end = json_float(component, "size_end", 0.01f);
    out_config->color_start = json_color(component, "color_start", (sdl3d_color){255, 255, 255, 255});
    out_config->color_end = json_color(component, "color_end", (sdl3d_color){255, 255, 255, 0});
    out_config->gravity = json_float(component, "gravity", 0.0f);
    out_config->max_particles = json_int(component, "max_particles", 128);
    out_config->emit_rate = json_float(component, "emit_rate", 0.0f);
    const char *shape = json_string(component, "shape", "point");
    if (SDL_strcmp(shape, "box") == 0)
        out_config->shape = SDL3D_PARTICLE_EMITTER_BOX;
    else if (SDL_strcmp(shape, "circle") == 0)
        out_config->shape = SDL3D_PARTICLE_EMITTER_CIRCLE;
    else
        out_config->shape = SDL3D_PARTICLE_EMITTER_POINT;
    out_config->extents = json_vec3(component, "extents", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    out_config->radius = json_float(component, "radius", 0.0f);
    out_config->emissive_intensity = json_float(component, "emissive_intensity", 1.0f);
    out_config->camera_facing = json_bool(component, "camera_facing", true);
    out_config->depth_test = json_bool(component, "depth_test", true);
    out_config->additive_blend = json_bool(component, "additive_blend", false);
    out_config->texture = NULL;
    out_config->random_seed = (Uint32)json_int(component, "random_seed", 0);
    return true;
}

bool sdl3d_game_data_get_particle_emitter_draw_emissive(const sdl3d_game_data_runtime *runtime, const char *entity_name,
                                                        sdl3d_vec3 *out_rgb)
{
    if (out_rgb != NULL)
        *out_rgb = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (runtime == NULL || entity_name == NULL || out_rgb == NULL)
        return false;

    yyjson_val *entity = find_entity_json(runtime, entity_name);
    yyjson_val *component = find_component_json(entity, "particles.emitter");
    if (component == NULL)
        return false;

    *out_rgb = json_vec3(component, "draw_emissive", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    return true;
}

bool sdl3d_game_data_for_each_particle_emitter(const sdl3d_game_data_runtime *runtime,
                                               sdl3d_game_data_particle_emitter_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;

        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, entity_name);
        yyjson_val *component = find_component_json(entity, "particles.emitter");
        if (actor == NULL || !actor->active || component == NULL)
            continue;

        sdl3d_game_data_particle_emitter emitter;
        SDL_zero(emitter);
        emitter.entity_name = entity_name;
        if (!sdl3d_game_data_get_particle_emitter(runtime, entity_name, &emitter.config))
            continue;
        (void)sdl3d_game_data_get_particle_emitter_draw_emissive(runtime, entity_name, &emitter.draw_emissive);

        if (!callback(userdata, &emitter))
            return true;
    }
    return true;
}

bool sdl3d_game_data_get_render_settings(const sdl3d_game_data_runtime *runtime,
                                         sdl3d_game_data_render_settings *out_settings)
{
    if (out_settings != NULL)
    {
        SDL_zero(*out_settings);
        out_settings->clear_color = (sdl3d_color){0, 0, 0, 255};
        out_settings->lighting_enabled = true;
        out_settings->bloom_enabled = true;
        out_settings->ssao_enabled = true;
        out_settings->tonemap = SDL3D_TONEMAP_ACES;
    }
    if (runtime == NULL || out_settings == NULL)
        return false;

    yyjson_val *render = obj_get(runtime_root(runtime), "render");
    if (!yyjson_is_obj(render))
        return true;

    out_settings->clear_color = json_color(render, "clear_color", out_settings->clear_color);
    out_settings->lighting_enabled = json_bool(render, "lighting", out_settings->lighting_enabled);
    out_settings->bloom_enabled = json_bool(render, "bloom", out_settings->bloom_enabled);
    out_settings->ssao_enabled = json_bool(render, "ssao", out_settings->ssao_enabled);
    out_settings->tonemap = parse_tonemap(json_string(render, "tonemap", NULL), out_settings->tonemap);
    return true;
}

bool sdl3d_game_data_get_transition(const sdl3d_game_data_runtime *runtime, const char *name,
                                    sdl3d_game_data_transition_desc *out_transition)
{
    if (out_transition != NULL)
    {
        SDL_zero(*out_transition);
        out_transition->type = SDL3D_TRANSITION_FADE;
        out_transition->direction = SDL3D_TRANSITION_IN;
        out_transition->color = (sdl3d_color){0, 0, 0, 255};
        out_transition->duration = 0.5f;
        out_transition->done_signal_id = -1;
    }
    if (runtime == NULL || name == NULL || out_transition == NULL)
        return false;

    yyjson_val *transition = obj_get(obj_get(runtime_root(runtime), "transitions"), name);
    if (!yyjson_is_obj(transition))
        return false;

    out_transition->type = parse_transition_type(json_string(transition, "type", NULL), out_transition->type);
    out_transition->direction =
        parse_transition_direction(json_string(transition, "direction", NULL), out_transition->direction);
    out_transition->color = json_color(transition, "color", out_transition->color);
    out_transition->duration = json_float(transition, "duration", out_transition->duration);
    const char *done_signal = json_string(transition, "done_signal", NULL);
    out_transition->done_signal_id =
        done_signal != NULL ? sdl3d_game_data_find_signal(runtime, done_signal) : out_transition->done_signal_id;
    return true;
}

const char *sdl3d_game_data_active_scene(const sdl3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return scene != NULL ? scene->name : NULL;
}

int sdl3d_game_data_scene_count(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_count : 0;
}

const char *sdl3d_game_data_scene_name_at(const sdl3d_game_data_runtime *runtime, int index)
{
    if (runtime == NULL || index < 0 || index >= runtime->scene_count)
        return NULL;
    return runtime->scenes[index].name;
}

sdl3d_properties *sdl3d_game_data_mutable_scene_state(sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_state : NULL;
}

const sdl3d_properties *sdl3d_game_data_scene_state(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_state : NULL;
}

void sdl3d_game_data_ui_state_init(sdl3d_game_data_ui_state *state)
{
    if (state == NULL)
        return;
    SDL_zero(*state);
    state->visible = true;
    state->scale = 1.0f;
    state->alpha = 1.0f;
    state->tint = (sdl3d_color){255, 255, 255, 255};
}

static ui_state_entry *find_ui_state_entry(sdl3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->ui_state_count; ++i)
    {
        if (runtime->ui_states[i].name != NULL && SDL_strcmp(runtime->ui_states[i].name, name) == 0)
            return &runtime->ui_states[i];
    }
    return NULL;
}

static const ui_state_entry *find_ui_state_entry_const(const sdl3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->ui_state_count; ++i)
    {
        if (runtime->ui_states[i].name != NULL && SDL_strcmp(runtime->ui_states[i].name, name) == 0)
            return &runtime->ui_states[i];
    }
    return NULL;
}

static bool ensure_ui_state_capacity(sdl3d_game_data_runtime *runtime, int required)
{
    if (runtime == NULL)
        return false;
    if (required <= runtime->ui_state_capacity)
        return true;

    int next_capacity = runtime->ui_state_capacity < 8 ? 8 : runtime->ui_state_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    ui_state_entry *entries =
        (ui_state_entry *)SDL_realloc(runtime->ui_states, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + runtime->ui_state_capacity, 0,
               (size_t)(next_capacity - runtime->ui_state_capacity) * sizeof(*entries));
    runtime->ui_states = entries;
    runtime->ui_state_capacity = next_capacity;
    return true;
}

bool sdl3d_game_data_set_ui_state(sdl3d_game_data_runtime *runtime, const char *name,
                                  const sdl3d_game_data_ui_state *state)
{
    if (runtime == NULL || name == NULL || name[0] == '\0' || state == NULL)
        return false;

    ui_state_entry *entry = find_ui_state_entry(runtime, name);
    if (entry == NULL)
    {
        if (!ensure_ui_state_capacity(runtime, runtime->ui_state_count + 1))
            return false;
        entry = &runtime->ui_states[runtime->ui_state_count];
        entry->name = SDL_strdup(name);
        if (entry->name == NULL)
            return false;
        ++runtime->ui_state_count;
    }

    entry->state = *state;
    return true;
}

bool sdl3d_game_data_get_ui_state(const sdl3d_game_data_runtime *runtime, const char *name,
                                  sdl3d_game_data_ui_state *out_state)
{
    if (out_state != NULL)
        sdl3d_game_data_ui_state_init(out_state);
    if (runtime == NULL || name == NULL || out_state == NULL)
        return false;

    const ui_state_entry *entry = find_ui_state_entry_const(runtime, name);
    if (entry == NULL)
        return false;

    *out_state = entry->state;
    return true;
}

bool sdl3d_game_data_clear_ui_state(sdl3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return false;

    for (int i = 0; i < runtime->ui_state_count; ++i)
    {
        if (runtime->ui_states[i].name != NULL && SDL_strcmp(runtime->ui_states[i].name, name) == 0)
        {
            SDL_free(runtime->ui_states[i].name);
            if (i + 1 < runtime->ui_state_count)
                runtime->ui_states[i] = runtime->ui_states[runtime->ui_state_count - 1];
            --runtime->ui_state_count;
            return true;
        }
    }
    return false;
}

void sdl3d_game_data_clear_ui_states(sdl3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < runtime->ui_state_count; ++i)
        SDL_free(runtime->ui_states[i].name);
    runtime->ui_state_count = 0;
}

bool sdl3d_game_data_get_active_splash(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_splash *out_splash)
{
    if (out_splash != NULL)
    {
        SDL_zero(*out_splash);
        out_splash->hold_seconds = 1.0f;
        out_splash->skip_on_input = true;
    }
    if (runtime == NULL || out_splash == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *splash = obj_get(scene != NULL ? scene->root : NULL, "splash");
    if (!yyjson_is_obj(splash))
        return false;

    out_splash->next_scene = json_string(splash, "next_scene", NULL);
    out_splash->hold_seconds = json_float(splash, "hold_seconds", out_splash->hold_seconds);
    out_splash->skip_on_input = json_bool(splash, "skip_on_input", out_splash->skip_on_input);
    return out_splash->next_scene != NULL;
}

static yyjson_val *active_skip_policy_json(const sdl3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *root = scene != NULL ? scene->root : NULL;
    yyjson_val *policy = obj_get(root, "skip_policy");
    if (yyjson_is_obj(policy))
        return policy;

    yyjson_val *timeline = obj_get(root, "timeline");
    policy = obj_get(timeline, "skip_policy");
    return yyjson_is_obj(policy) ? policy : NULL;
}

bool sdl3d_game_data_get_active_skip_policy(const sdl3d_game_data_runtime *runtime,
                                            sdl3d_game_data_skip_policy *out_policy)
{
    if (out_policy != NULL)
    {
        SDL_zero(*out_policy);
        out_policy->enabled = false;
        out_policy->input = SDL3D_GAME_DATA_SKIP_INPUT_ANY;
        out_policy->action_id = -1;
        out_policy->preserve_exit_transition = true;
        out_policy->consume_input = true;
    }
    if (runtime == NULL || out_policy == NULL)
        return false;

    yyjson_val *policy = active_skip_policy_json(runtime);
    if (!yyjson_is_obj(policy))
        return false;

    out_policy->enabled = json_bool(policy, "enabled", true);
    if (!out_policy->enabled)
        return false;

    const char *input = json_string(policy, "input", NULL);
    out_policy->action = json_string(policy, "action", NULL);
    if (input == NULL)
        out_policy->input =
            out_policy->action != NULL ? SDL3D_GAME_DATA_SKIP_INPUT_ACTION : SDL3D_GAME_DATA_SKIP_INPUT_ANY;
    else if (SDL_strcmp(input, "any") == 0 || SDL_strcmp(input, "any_input") == 0)
        out_policy->input = SDL3D_GAME_DATA_SKIP_INPUT_ANY;
    else if (SDL_strcmp(input, "action") == 0)
        out_policy->input = SDL3D_GAME_DATA_SKIP_INPUT_ACTION;
    else
        out_policy->input = SDL3D_GAME_DATA_SKIP_INPUT_DISABLED;

    out_policy->action_id = sdl3d_game_data_find_action(runtime, out_policy->action);
    out_policy->scene = json_string(policy, "scene", json_string(policy, "target_scene", NULL));
    out_policy->preserve_exit_transition = json_bool(policy, "preserve_exit_transition", true);
    out_policy->consume_input = json_bool(policy, "consume_input", true);
    return out_policy->input != SDL3D_GAME_DATA_SKIP_INPUT_DISABLED;
}

static yyjson_val *active_timeline_events(const sdl3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *timeline = obj_get(scene != NULL ? scene->root : NULL, "timeline");
    if (!yyjson_is_obj(timeline) || !json_bool(timeline, "autoplay", false))
        return NULL;

    yyjson_val *events = obj_get(timeline, "events");
    if (events == NULL)
        events = obj_get(timeline, "tracks");
    return yyjson_is_arr(events) ? events : NULL;
}

static bool execute_one_action(sdl3d_game_data_runtime *runtime, yyjson_val *action, const sdl3d_properties *payload);

void sdl3d_game_data_timeline_state_init(sdl3d_game_data_timeline_state *state)
{
    if (state != NULL)
        SDL_zero(*state);
}

bool sdl3d_game_data_update_timeline(sdl3d_game_data_runtime *runtime, sdl3d_game_data_timeline_state *state, float dt,
                                     sdl3d_game_data_timeline_update_result *out_result)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    if (runtime == NULL || state == NULL)
        return false;

    const char *active_scene = sdl3d_game_data_active_scene(runtime);
    if (state->scene != active_scene)
    {
        state->scene = active_scene;
        state->time = 0.0f;
        state->next_event_index = 0;
        state->complete = false;
    }

    yyjson_val *events = active_timeline_events(runtime);
    if (!yyjson_is_arr(events))
    {
        state->complete = true;
        if (out_result != NULL)
            out_result->complete = true;
        return true;
    }

    const int event_count = (int)yyjson_arr_size(events);
    if (state->next_event_index >= event_count)
    {
        state->complete = true;
        if (out_result != NULL)
            out_result->complete = true;
        return true;
    }

    state->time += dt > 0.0f ? dt : 0.0f;
    bool ok = true;
    while (state->next_event_index < event_count)
    {
        yyjson_val *event = yyjson_arr_get(events, (size_t)state->next_event_index);
        const float event_time = json_float(event, "time", 0.0f);
        if (event_time > state->time)
            break;

        ++state->next_event_index;
        yyjson_val *action = obj_get(event, "action");
        const char *type = json_string(action, "type", "");
        if (SDL_strcmp(type, "scene.request") == 0)
        {
            if (out_result != NULL)
                out_result->scene_request = json_string(action, "scene", NULL);
        }
        else if (yyjson_is_obj(action))
        {
            ok = execute_one_action(runtime, action, NULL) && ok;
        }

        if (out_result != NULL)
            ++out_result->actions_executed;
        if (out_result != NULL && out_result->scene_request != NULL)
            break;
    }

    state->complete = state->next_event_index >= event_count;
    if (out_result != NULL)
        out_result->complete = state->complete;
    return ok;
}

bool sdl3d_game_data_set_active_scene(sdl3d_game_data_runtime *runtime, const char *scene_name)
{
    return sdl3d_game_data_set_active_scene_with_payload(runtime, scene_name, NULL);
}

bool sdl3d_game_data_set_active_scene_with_payload(sdl3d_game_data_runtime *runtime, const char *scene_name,
                                                   const sdl3d_properties *payload)
{
    scene_entry *scene = find_scene(runtime, scene_name);
    if (runtime == NULL || scene == NULL)
        return false;

    const char *previous = sdl3d_game_data_active_scene(runtime);
    runtime->active_scene_index = (int)(scene - runtime->scenes);
    apply_scene_camera(runtime, scene);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SDL3D game data scene set: %s -> %s",
                 previous != NULL ? previous : "<none>", scene->name != NULL ? scene->name : "<none>");
    emit_scene_enter_signal(runtime, scene, previous, payload);
    return true;
}

bool sdl3d_game_data_active_scene_updates_game(const sdl3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return scene != NULL ? json_bool(scene->root, "updates_game", true) : true;
}

static bool update_phase_default(const sdl3d_game_data_runtime *runtime, const char *phase, bool paused)
{
    if (phase != NULL && SDL_strcmp(phase, "simulation") == 0)
        return !paused && sdl3d_game_data_active_scene_updates_game(runtime);
    if (phase != NULL && SDL_strcmp(phase, "app_flow") == 0)
        return true;
    if (phase != NULL && (SDL_strcmp(phase, "presentation") == 0 || SDL_strcmp(phase, "property_effects") == 0 ||
                          SDL_strcmp(phase, "particles") == 0))
        return true;
    return !paused;
}

static bool eval_update_phase_entry(yyjson_val *entry, bool paused, bool fallback)
{
    if (yyjson_is_bool(entry))
        return yyjson_get_bool(entry);
    if (!yyjson_is_obj(entry))
        return fallback;
    if (!json_bool(entry, "active", true))
        return false;
    if (paused)
        return json_bool(entry, "when_paused", fallback);
    return json_bool(entry, "when_unpaused", true);
}

bool sdl3d_game_data_active_scene_update_phase(const sdl3d_game_data_runtime *runtime, const char *phase, bool paused)
{
    if (runtime == NULL || phase == NULL)
        return false;

    const bool fallback = update_phase_default(runtime, phase, paused);
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *scene_entry_json = obj_get(obj_get(scene != NULL ? scene->root : NULL, "update_phases"), phase);
    if (scene_entry_json != NULL)
        return eval_update_phase_entry(scene_entry_json, paused, fallback);

    yyjson_val *root_entry_json = obj_get(obj_get(runtime_root(runtime), "update_phases"), phase);
    return eval_update_phase_entry(root_entry_json, paused, fallback);
}

bool sdl3d_game_data_active_scene_renders_world(const sdl3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return scene != NULL ? json_bool(scene->root, "renders_world", true) : true;
}

bool sdl3d_game_data_active_scene_has_entity(const sdl3d_game_data_runtime *runtime, const char *entity_name)
{
    return runtime != NULL && active_scene_has_entity_internal(runtime, entity_name);
}

static bool string_array_contains(yyjson_val *array, const char *value)
{
    for (size_t i = 0; value != NULL && yyjson_is_arr(array) && i < yyjson_arr_size(array); ++i)
    {
        yyjson_val *item = yyjson_arr_get(array, i);
        const char *text = yyjson_is_str(item) ? yyjson_get_str(item) : NULL;
        if (text != NULL && SDL_strcmp(text, value) == 0)
            return true;
    }
    return false;
}

bool sdl3d_game_data_active_scene_allows_action(const sdl3d_game_data_runtime *runtime, int action_id)
{
    const char *action = find_action_name(runtime, action_id);
    if (runtime == NULL || action == NULL)
        return false;

    yyjson_val *global_actions =
        obj_get(obj_get(obj_get(runtime_root(runtime), "app"), "input_policy"), "global_actions");
    if (string_array_contains(global_actions, action))
        return true;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *actions = obj_get(obj_get(scene != NULL ? scene->root : NULL, "input"), "actions");
    if (actions == NULL)
        return true;
    return string_array_contains(actions, action);
}

bool sdl3d_game_data_get_scene_transition_policy(const sdl3d_game_data_runtime *runtime,
                                                 sdl3d_game_data_scene_transition_policy *out_policy)
{
    if (out_policy != NULL)
    {
        out_policy->allow_same_scene = false;
        out_policy->allow_interrupt = false;
        out_policy->reset_menu_input_on_request = true;
    }
    if (runtime == NULL || out_policy == NULL)
        return false;

    yyjson_val *policy = obj_get(obj_get(runtime_root(runtime), "app"), "scene_transition_policy");
    out_policy->allow_same_scene = json_bool(policy, "allow_same_scene", out_policy->allow_same_scene);
    out_policy->allow_interrupt = json_bool(policy, "allow_interrupt", out_policy->allow_interrupt);
    out_policy->reset_menu_input_on_request =
        json_bool(policy, "reset_menu_input_on_request", out_policy->reset_menu_input_on_request);
    return true;
}

bool sdl3d_game_data_get_scene_transition(const sdl3d_game_data_runtime *runtime, const char *scene_name,
                                          const char *phase, sdl3d_game_data_transition_desc *out_transition)
{
    const scene_entry *scene = find_scene_const(runtime, scene_name);
    if (scene == NULL || phase == NULL)
        return false;

    const char *transition_name = json_string(obj_get(scene->root, "transitions"), phase, NULL);
    return transition_name != NULL && sdl3d_game_data_get_transition(runtime, transition_name, out_transition);
}

bool sdl3d_game_data_get_active_menu(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_menu *out_menu)
{
    if (out_menu != NULL)
    {
        SDL_zero(*out_menu);
        out_menu->up_action_id = -1;
        out_menu->down_action_id = -1;
        out_menu->select_action_id = -1;
    }
    const scene_entry *scene = active_scene_entry_const(runtime);
    if (runtime == NULL || out_menu == NULL || scene == NULL || scene->menu_count <= 0)
        return false;

    const scene_menu_state *state = &scene->menus[0];
    yyjson_val *menu = state->menu;
    out_menu->name = json_string(menu, "name", NULL);
    out_menu->up_action_id = sdl3d_game_data_find_action(runtime, json_string(menu, "up_action", NULL));
    out_menu->down_action_id = sdl3d_game_data_find_action(runtime, json_string(menu, "down_action", NULL));
    out_menu->select_action_id = sdl3d_game_data_find_action(runtime, json_string(menu, "select_action", NULL));
    out_menu->selected_index = state->selected_index;
    out_menu->item_count = state->item_count;
    return out_menu->name != NULL && out_menu->item_count > 0;
}

bool sdl3d_game_data_menu_move(sdl3d_game_data_runtime *runtime, const char *menu_name, int delta)
{
    scene_menu_state *menu = find_scene_menu(active_scene_entry(runtime), menu_name);
    if (menu == NULL || menu->item_count <= 0)
        return false;

    int next = (menu->selected_index + delta) % menu->item_count;
    if (next < 0)
        next += menu->item_count;
    menu->selected_index = next;
    return true;
}

static sdl3d_game_data_menu_control_type parse_menu_control_type(const char *type)
{
    if (type == NULL)
        return SDL3D_GAME_DATA_MENU_CONTROL_NONE;
    if (SDL_strcmp(type, "toggle") == 0)
        return SDL3D_GAME_DATA_MENU_CONTROL_TOGGLE;
    if (SDL_strcmp(type, "choice") == 0)
        return SDL3D_GAME_DATA_MENU_CONTROL_CHOICE;
    if (SDL_strcmp(type, "range") == 0)
        return SDL3D_GAME_DATA_MENU_CONTROL_RANGE;
    return SDL3D_GAME_DATA_MENU_CONTROL_NONE;
}

static bool json_value_matches_property(yyjson_val *value, const sdl3d_value *property);

bool sdl3d_game_data_get_menu_item(const sdl3d_game_data_runtime *runtime, const char *menu_name, int index,
                                   sdl3d_game_data_menu_item *out_item)
{
    if (out_item != NULL)
    {
        SDL_zero(*out_item);
        out_item->signal_id = -1;
    }
    const scene_menu_state *menu = find_scene_menu_const(active_scene_entry_const(runtime), menu_name);
    if (runtime == NULL || menu == NULL || out_item == NULL || index < 0 || index >= menu->item_count)
        return false;

    yyjson_val *item = yyjson_arr_get(obj_get(menu->menu, "items"), (size_t)index);
    yyjson_val *control = obj_get(item, "control");
    out_item->label = json_string(item, "label", NULL);
    out_item->scene = json_string(item, "scene", NULL);
    out_item->quit = json_bool(item, "quit", false);
    out_item->signal_id = sdl3d_game_data_find_signal(runtime, json_string(item, "signal", NULL));
    out_item->control_type = parse_menu_control_type(json_string(control, "type", NULL));
    out_item->control_target = json_string(control, "target", NULL);
    out_item->control_key = json_string(control, "key", NULL);
    out_item->choice_count =
        yyjson_is_arr(obj_get(control, "choices")) ? (int)yyjson_arr_size(obj_get(control, "choices")) : 0;
    return yyjson_is_obj(item) && out_item->label != NULL;
}

static yyjson_val *find_menu_item_control_json(const sdl3d_game_data_runtime *runtime,
                                               const sdl3d_game_data_menu_item *item)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    for (int m = 0; scene != NULL && item != NULL && m < scene->menu_count; ++m)
    {
        yyjson_val *items = obj_get(scene->menus[m].menu, "items");
        for (size_t i = 0; yyjson_is_arr(items) && i < yyjson_arr_size(items); ++i)
        {
            yyjson_val *candidate = yyjson_arr_get(items, i);
            yyjson_val *control = obj_get(candidate, "control");
            const char *label = json_string(candidate, "label", NULL);
            const char *target = json_string(control, "target", NULL);
            const char *key = json_string(control, "key", NULL);
            if (label != NULL && target != NULL && key != NULL && item->label != NULL && item->control_target != NULL &&
                item->control_key != NULL && SDL_strcmp(label, item->label) == 0 &&
                SDL_strcmp(target, item->control_target) == 0 && SDL_strcmp(key, item->control_key) == 0)
                return control;
        }
    }
    return NULL;
}

static bool set_property_from_json(sdl3d_properties *props, const char *key, yyjson_val *value)
{
    if (props == NULL || key == NULL || value == NULL)
        return false;
    if (yyjson_is_bool(value))
    {
        sdl3d_properties_set_bool(props, key, yyjson_get_bool(value));
        return true;
    }
    if (yyjson_is_int(value))
    {
        sdl3d_properties_set_int(props, key, (int)yyjson_get_sint(value));
        return true;
    }
    if (yyjson_is_num(value))
    {
        sdl3d_properties_set_float(props, key, (float)yyjson_get_num(value));
        return true;
    }
    if (yyjson_is_str(value))
    {
        sdl3d_properties_set_string(props, key, yyjson_get_str(value));
        return true;
    }
    return false;
}

static bool json_value_matches_property(yyjson_val *value, const sdl3d_value *property)
{
    if (value == NULL || property == NULL)
        return false;
    switch (property->type)
    {
    case SDL3D_VALUE_BOOL:
        return yyjson_is_bool(value) && yyjson_get_bool(value) == property->as_bool;
    case SDL3D_VALUE_INT:
        return yyjson_is_int(value) && (int)yyjson_get_sint(value) == property->as_int;
    case SDL3D_VALUE_FLOAT:
        return yyjson_is_num(value) && SDL_fabsf((float)yyjson_get_num(value) - property->as_float) < 0.0001f;
    case SDL3D_VALUE_STRING:
        return yyjson_is_str(value) &&
               SDL_strcmp(yyjson_get_str(value), property->as_string != NULL ? property->as_string : "") == 0;
    case SDL3D_VALUE_VEC3:
    case SDL3D_VALUE_COLOR:
        return false;
    }
    return false;
}

bool sdl3d_game_data_apply_menu_item_control(sdl3d_game_data_runtime *runtime, const sdl3d_game_data_menu_item *item)
{
    if (runtime == NULL || item == NULL || item->control_type == SDL3D_GAME_DATA_MENU_CONTROL_NONE ||
        item->control_target == NULL || item->control_key == NULL)
        return false;

    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, item->control_target);
    if (actor == NULL)
        return false;

    yyjson_val *control = find_menu_item_control_json(runtime, item);
    switch (item->control_type)
    {
    case SDL3D_GAME_DATA_MENU_CONTROL_TOGGLE: {
        const bool current =
            sdl3d_properties_get_bool(actor->props, item->control_key, json_bool(control, "default", false));
        sdl3d_properties_set_bool(actor->props, item->control_key, !current);
        return true;
    }
    case SDL3D_GAME_DATA_MENU_CONTROL_CHOICE: {
        yyjson_val *choices = obj_get(control, "choices");
        if (!yyjson_is_arr(choices) || yyjson_arr_size(choices) == 0)
            return false;
        const sdl3d_value *current = sdl3d_properties_get_value(actor->props, item->control_key);
        int current_index = -1;
        for (size_t i = 0; i < yyjson_arr_size(choices); ++i)
        {
            yyjson_val *choice = yyjson_arr_get(choices, i);
            yyjson_val *value = yyjson_is_obj(choice) ? obj_get(choice, "value") : choice;
            if (json_value_matches_property(value, current))
            {
                current_index = (int)i;
                break;
            }
        }
        const int next_index = (current_index + 1) % (int)yyjson_arr_size(choices);
        yyjson_val *next = yyjson_arr_get(choices, (size_t)next_index);
        return set_property_from_json(actor->props, item->control_key,
                                      yyjson_is_obj(next) ? obj_get(next, "value") : next);
    }
    case SDL3D_GAME_DATA_MENU_CONTROL_RANGE: {
        const float min_value = json_float(control, "min", 0.0f);
        const float max_value = json_float(control, "max", 1.0f);
        const float step = json_float(control, "step", 1.0f);
        float value =
            sdl3d_properties_get_float(actor->props, item->control_key, json_float(control, "default", min_value));
        value += step;
        if (value > max_value)
            value = min_value;
        sdl3d_properties_set_float(actor->props, item->control_key, SDL_clamp(value, min_value, max_value));
        return true;
    }
    case SDL3D_GAME_DATA_MENU_CONTROL_NONE:
        return false;
    }
    return false;
}

int sdl3d_game_data_scene_shortcut_count(const sdl3d_game_data_runtime *runtime)
{
    yyjson_val *shortcuts = obj_get(obj_get(runtime_root(runtime), "app"), "scene_shortcuts");
    return yyjson_is_arr(shortcuts) ? (int)yyjson_arr_size(shortcuts) : 0;
}

bool sdl3d_game_data_scene_shortcut_at(const sdl3d_game_data_runtime *runtime, int index,
                                       sdl3d_game_data_scene_shortcut *out_shortcut)
{
    if (out_shortcut != NULL)
    {
        SDL_zero(*out_shortcut);
        out_shortcut->action_id = -1;
    }
    yyjson_val *shortcuts = obj_get(obj_get(runtime_root(runtime), "app"), "scene_shortcuts");
    if (runtime == NULL || out_shortcut == NULL || !yyjson_is_arr(shortcuts) || index < 0 ||
        index >= (int)yyjson_arr_size(shortcuts))
        return false;

    yyjson_val *shortcut = yyjson_arr_get(shortcuts, (size_t)index);
    out_shortcut->action = json_string(shortcut, "action", NULL);
    out_shortcut->scene = json_string(shortcut, "scene", NULL);
    out_shortcut->action_id = sdl3d_game_data_find_action(runtime, out_shortcut->action);
    return yyjson_is_obj(shortcut) && out_shortcut->action != NULL && out_shortcut->scene != NULL;
}

bool sdl3d_game_data_active_menu_input_is_idle(const sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input)
{
    sdl3d_game_data_menu menu;
    if (!sdl3d_game_data_get_active_menu(runtime, &menu))
        return true;
    if (input == NULL)
        return false;

    return (menu.up_action_id < 0 || !sdl3d_game_data_active_scene_allows_action(runtime, menu.up_action_id) ||
            !sdl3d_input_is_held(input, menu.up_action_id)) &&
           (menu.down_action_id < 0 || !sdl3d_game_data_active_scene_allows_action(runtime, menu.down_action_id) ||
            !sdl3d_input_is_held(input, menu.down_action_id)) &&
           (menu.select_action_id < 0 || !sdl3d_game_data_active_scene_allows_action(runtime, menu.select_action_id) ||
            !sdl3d_input_is_held(input, menu.select_action_id));
}

static void ui_text_from_json(yyjson_val *item, sdl3d_game_data_ui_text *text)
{
    if (text == NULL)
        return;
    SDL_zero(*text);
    text->name = json_string(item, "name", NULL);
    text->font = json_string(item, "font", NULL);
    text->text = json_string(item, "text", NULL);
    text->format = json_string(item, "format", NULL);
    text->source = json_string(item, "source", NULL);
    text->visible = json_string(item, "visible", "always");
    text->x = json_float(item, "x", 0.0f);
    text->y = json_float(item, "y", 0.0f);
    text->normalized = json_bool(item, "normalized", false);
    text->align = parse_ui_align(json_string(item, "align", NULL), json_bool(item, "centered", false)
                                                                       ? SDL3D_GAME_DATA_UI_ALIGN_CENTER
                                                                       : SDL3D_GAME_DATA_UI_ALIGN_LEFT);
    text->centered = text->align == SDL3D_GAME_DATA_UI_ALIGN_CENTER;
    text->scale = json_float(item, "scale", 1.0f);
    text->pulse_alpha = json_bool(item, "pulse_alpha", false);
    text->color = json_color(item, "color", (sdl3d_color){255, 255, 255, 255});
}

static bool for_each_authored_ui_text_root(yyjson_val *root, sdl3d_game_data_ui_text_fn callback, void *userdata)
{
    yyjson_val *texts = obj_get(obj_get(root, "ui"), "text");
    for (size_t i = 0; yyjson_is_arr(texts) && i < yyjson_arr_size(texts); ++i)
    {
        sdl3d_game_data_ui_text text;
        ui_text_from_json(yyjson_arr_get(texts, i), &text);
        if (!callback(userdata, &text))
            return false;
    }
    return true;
}

static bool emit_ui_menu_cursor(yyjson_val *presenter, float row_y, sdl3d_game_data_ui_text_fn callback, void *userdata)
{
    yyjson_val *cursor = obj_get(presenter, "cursor");
    if (!yyjson_is_obj(cursor) || !json_bool(cursor, "visible", true))
        return true;

    sdl3d_game_data_ui_text text;
    SDL_zero(text);
    text.name = json_string(presenter, "name", NULL);
    text.font = json_string(cursor, "font", json_string(presenter, "font", NULL));
    text.text = json_string(cursor, "text", ">");
    text.visible = "always";
    text.x = json_float(presenter, "x", 0.0f) + json_float(cursor, "offset_x", -0.05f);
    text.y = row_y + json_float(cursor, "offset_y", 0.0f);
    text.normalized = json_bool(presenter, "normalized", false);
    text.align = parse_ui_align(json_string(cursor, "align", NULL), SDL3D_GAME_DATA_UI_ALIGN_CENTER);
    text.centered = text.align == SDL3D_GAME_DATA_UI_ALIGN_CENTER;
    text.scale = json_float(cursor, "scale", json_float(presenter, "scale", 1.0f));
    text.pulse_alpha = json_bool(cursor, "pulse_alpha", false);
    text.color = json_color(cursor, "color", (sdl3d_color){255, 222, 140, 255});
    return callback(userdata, &text);
}

static const char *choice_label_for_property(const sdl3d_game_data_runtime *runtime, yyjson_val *control)
{
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(control, "target", NULL));
    const char *key = json_string(control, "key", NULL);
    const sdl3d_value *value = actor != NULL && key != NULL ? sdl3d_properties_get_value(actor->props, key) : NULL;
    yyjson_val *choices = obj_get(control, "choices");
    for (size_t i = 0; yyjson_is_arr(choices) && i < yyjson_arr_size(choices); ++i)
    {
        yyjson_val *choice = yyjson_arr_get(choices, i);
        yyjson_val *choice_value = yyjson_is_obj(choice) ? obj_get(choice, "value") : choice;
        if (json_value_matches_property(choice_value, value))
            return yyjson_is_obj(choice) ? json_string(choice, "label", NULL) : NULL;
    }
    return NULL;
}

static void format_menu_item_label(const sdl3d_game_data_runtime *runtime, yyjson_val *item, char *buffer,
                                   size_t buffer_size)
{
    const char *label = json_string(item, "label", "");
    yyjson_val *control = obj_get(item, "control");
    const sdl3d_game_data_menu_control_type type = parse_menu_control_type(json_string(control, "type", NULL));
    if (buffer == NULL || buffer_size == 0)
        return;

    if (type == SDL3D_GAME_DATA_MENU_CONTROL_NONE)
    {
        SDL_strlcpy(buffer, label, buffer_size);
        return;
    }

    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(control, "target", NULL));
    const char *key = json_string(control, "key", NULL);
    const sdl3d_value *value = actor != NULL && key != NULL ? sdl3d_properties_get_value(actor->props, key) : NULL;
    if (type == SDL3D_GAME_DATA_MENU_CONTROL_TOGGLE)
    {
        const bool enabled =
            value != NULL && value->type == SDL3D_VALUE_BOOL ? value->as_bool : json_bool(control, "default", false);
        SDL_snprintf(buffer, buffer_size, "%s: %s", label, enabled ? "On" : "Off");
        return;
    }
    if (type == SDL3D_GAME_DATA_MENU_CONTROL_CHOICE)
    {
        const char *choice_label = choice_label_for_property(runtime, control);
        if (choice_label == NULL && value != NULL && value->type == SDL3D_VALUE_STRING)
            choice_label = value->as_string;
        SDL_snprintf(buffer, buffer_size, "%s: %s", label, choice_label != NULL ? choice_label : "-");
        return;
    }
    if (type == SDL3D_GAME_DATA_MENU_CONTROL_RANGE)
    {
        const float current =
            value != NULL && value->type == SDL3D_VALUE_FLOAT ? value->as_float : json_float(control, "default", 0.0f);
        SDL_snprintf(buffer, buffer_size, "%s: %.2g", label, current);
        return;
    }
    SDL_strlcpy(buffer, label, buffer_size);
}

static bool for_each_ui_menu_presenter(const sdl3d_game_data_runtime *runtime, const scene_entry *scene,
                                       yyjson_val *presenter, sdl3d_game_data_ui_text_fn callback, void *userdata)
{
    if (scene == NULL || presenter == NULL)
        return true;

    const scene_menu_state *menu_state = find_scene_menu_const(scene, json_string(presenter, "menu", NULL));
    yyjson_val *items = menu_state != NULL ? obj_get(menu_state->menu, "items") : NULL;
    if (menu_state == NULL || !yyjson_is_arr(items))
        return true;

    const float x = json_float(presenter, "x", 0.0f);
    const float y = json_float(presenter, "y", 0.0f);
    const float gap = json_float(presenter, "gap", json_bool(presenter, "normalized", false) ? 0.08f : 42.0f);
    const bool normalized = json_bool(presenter, "normalized", false);
    const sdl3d_game_data_ui_align align =
        parse_ui_align(json_string(presenter, "align", NULL), SDL3D_GAME_DATA_UI_ALIGN_CENTER);

    for (int i = 0; i < menu_state->item_count; ++i)
    {
        yyjson_val *item = yyjson_arr_get(items, (size_t)i);
        const bool selected = i == menu_state->selected_index;
        const float row_y = y + gap * (float)i;

        if (selected && !emit_ui_menu_cursor(presenter, row_y, callback, userdata))
            return false;

        sdl3d_game_data_ui_text text;
        char label[128];
        format_menu_item_label(runtime, item, label, sizeof(label));
        SDL_zero(text);
        text.name = json_string(presenter, "name", NULL);
        text.font = json_string(presenter, "font", NULL);
        text.text = label;
        text.visible = "always";
        text.x = x;
        text.y = row_y;
        text.normalized = normalized;
        text.align = align;
        text.centered = align == SDL3D_GAME_DATA_UI_ALIGN_CENTER;
        text.scale = json_float(presenter, "scale", 1.0f);
        text.pulse_alpha = selected && json_bool(presenter, "selected_pulse_alpha", false);
        text.color = selected ? json_color(presenter, "selected_color",
                                           json_color(presenter, "color", (sdl3d_color){255, 255, 255, 255}))
                              : json_color(presenter, "color", (sdl3d_color){255, 255, 255, 255});
        if (!callback(userdata, &text))
            return false;
    }
    return true;
}

static bool for_each_ui_menu_root(const sdl3d_game_data_runtime *runtime, const scene_entry *scene, yyjson_val *root,
                                  sdl3d_game_data_ui_text_fn callback, void *userdata)
{
    yyjson_val *menus = obj_get(obj_get(root, "ui"), "menus");
    for (size_t i = 0; yyjson_is_arr(menus) && i < yyjson_arr_size(menus); ++i)
    {
        if (!for_each_ui_menu_presenter(runtime, scene, yyjson_arr_get(menus, i), callback, userdata))
            return false;
    }
    return true;
}

bool sdl3d_game_data_for_each_ui_text(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_ui_text_fn callback,
                                      void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    yyjson_val *roots[2];
    roots[0] = runtime_root(runtime);
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;

    for (int root_index = 0; root_index < 2; ++root_index)
        if (!for_each_authored_ui_text_root(roots[root_index], callback, userdata) ||
            !for_each_ui_menu_root(runtime, scene, roots[root_index], callback, userdata))
            return true;
    return true;
}

static void ui_image_from_json(yyjson_val *item, sdl3d_game_data_ui_image *image)
{
    if (image == NULL)
        return;
    SDL_zero(*image);
    image->name = json_string(item, "name", NULL);
    image->image = json_string(item, "image", NULL);
    image->visible = json_string(item, "visible", "always");
    image->x = json_float(item, "x", 0.0f);
    image->y = json_float(item, "y", 0.0f);
    image->w = json_float(item, "w", json_float(item, "width", 0.0f));
    image->h = json_float(item, "h", json_float(item, "height", 0.0f));
    image->normalized = json_bool(item, "normalized", false);
    image->preserve_aspect = json_bool(item, "preserve_aspect", true);
    image->align = parse_ui_align(json_string(item, "align", NULL), SDL3D_GAME_DATA_UI_ALIGN_LEFT);
    image->valign = parse_ui_valign(json_string(item, "valign", NULL), SDL3D_GAME_DATA_UI_VALIGN_TOP);
    image->scale = json_float(item, "scale", 1.0f);
    image->color = json_color(item, "color", (sdl3d_color){255, 255, 255, 255});
}

static bool for_each_authored_ui_image_root(yyjson_val *root, sdl3d_game_data_ui_image_fn callback, void *userdata)
{
    yyjson_val *images = obj_get(obj_get(root, "ui"), "images");
    for (size_t i = 0; yyjson_is_arr(images) && i < yyjson_arr_size(images); ++i)
    {
        sdl3d_game_data_ui_image image;
        ui_image_from_json(yyjson_arr_get(images, i), &image);
        if (!callback(userdata, &image))
            return false;
    }
    return true;
}

bool sdl3d_game_data_for_each_ui_image(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_ui_image_fn callback,
                                       void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    yyjson_val *roots[2];
    roots[0] = runtime_root(runtime);
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;

    for (int root_index = 0; root_index < 2; ++root_index)
        if (!for_each_authored_ui_image_root(roots[root_index], callback, userdata))
            return true;
    return true;
}

float sdl3d_game_data_delta_time(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->current_dt : 0.0f;
}

static void set_actor_property_from_json(sdl3d_registered_actor *actor, const char *key, yyjson_val *value)
{
    if (actor == NULL || key == NULL || value == NULL)
        return;

    if (yyjson_is_bool(value))
        sdl3d_properties_set_bool(actor->props, key, yyjson_get_bool(value));
    else if (yyjson_is_int(value))
        sdl3d_properties_set_int(actor->props, key, (int)yyjson_get_int(value));
    else if (yyjson_is_num(value))
        sdl3d_properties_set_float(actor->props, key, (float)yyjson_get_real(value));
    else if (yyjson_is_str(value))
        sdl3d_properties_set_string(actor->props, key, yyjson_get_str(value));
    else if (yyjson_is_arr(value))
        sdl3d_properties_set_vec3(actor->props, key, json_vec3_value(value, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)));
}

static void load_actor_properties(sdl3d_registered_actor *actor, yyjson_val *properties)
{
    if (actor == NULL || !yyjson_is_obj(properties))
        return;

    yyjson_val *key;
    yyjson_val *entry;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        entry = yyjson_obj_iter_get_val(key);
        const char *name = yyjson_get_str(key);
        const char *type = json_string(entry, "type", NULL);
        yyjson_val *value = obj_get(entry, "value");
        if (name == NULL || value == NULL)
            continue;

        if (type != NULL && (SDL_strcmp(type, "vec2") == 0 || SDL_strcmp(type, "vec3") == 0))
            sdl3d_properties_set_vec3(actor->props, name, json_vec3_value(value, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)));
        else
            set_actor_property_from_json(actor, name, value);
    }
}

static bool load_entities(sdl3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size)
{
    sdl3d_actor_registry *registry = runtime_registry(runtime);
    yyjson_val *entities = obj_get(root, "entities");
    if (!yyjson_is_arr(entities))
        return true;
    if (registry == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data entities require an actor registry");
        return false;
    }

    const size_t count = yyjson_arr_size(entities);
    for (size_t i = 0; i < count; ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *name = json_string(entity, "name", NULL);
        if (name == NULL || name[0] == '\0')
        {
            set_error(error_buffer, error_buffer_size, "entity is missing a non-empty name");
            return false;
        }

        sdl3d_registered_actor *actor = sdl3d_actor_registry_add(registry, name);
        if (actor == NULL)
        {
            set_error(error_buffer, error_buffer_size, "failed to register JSON actor");
            return false;
        }
        actor->active = json_bool(entity, "active", true);
        sdl3d_properties_set_string(actor->props, "classname", name);

        yyjson_val *transform = obj_get(entity, "transform");
        actor_set_position(actor, json_vec3(transform, "position", sdl3d_vec3_make(0.0f, 0.0f, 0.0f)));
        load_actor_properties(actor, obj_get(entity, "properties"));
    }

    return true;
}

static bool load_signals(sdl3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size)
{
    yyjson_val *signals = obj_get(root, "signals");
    if (!yyjson_is_arr(signals))
        return true;

    const int count = (int)yyjson_arr_size(signals);
    runtime->signals = (named_signal *)SDL_calloc((size_t)count, sizeof(*runtime->signals));
    if (runtime->signals == NULL && count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate signal table");
        return false;
    }
    runtime->signal_count = count;

    for (int i = 0; i < count; ++i)
    {
        yyjson_val *signal = yyjson_arr_get(signals, (size_t)i);
        if (!yyjson_is_str(signal))
        {
            set_error(error_buffer, error_buffer_size, "signal entries must be strings");
            return false;
        }
        runtime->signals[i].name = yyjson_get_str(signal);
        runtime->signals[i].id = SDL3D_GAME_DATA_SIGNAL_BASE + i;
    }
    return true;
}

static bool load_input(sdl3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size)
{
    sdl3d_input_manager *input = runtime_input(runtime);
    yyjson_val *input_root = obj_get(root, "input");
    yyjson_val *contexts = obj_get(input_root, "contexts");
    if (!yyjson_is_arr(contexts))
        return true;
    if (input == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data input requires an input manager");
        return false;
    }

    for (size_t c = 0; c < yyjson_arr_size(contexts); ++c)
    {
        yyjson_val *context = yyjson_arr_get(contexts, c);
        yyjson_val *actions = obj_get(context, "actions");
        if (!yyjson_is_arr(actions))
            continue;

        for (size_t a = 0; a < yyjson_arr_size(actions); ++a)
        {
            yyjson_val *action = yyjson_arr_get(actions, a);
            const char *name = json_string(action, "name", NULL);
            if (name == NULL)
                continue;

            const int action_id = sdl3d_input_register_action(input, name);
            named_action *entries =
                (named_action *)SDL_realloc(runtime->actions, (size_t)(runtime->action_count + 1) * sizeof(*entries));
            if (entries == NULL)
                return false;
            runtime->actions = entries;
            runtime->actions[runtime->action_count++] = (named_action){name, action_id};

            yyjson_val *bindings = obj_get(action, "bindings");
            for (size_t b = 0; yyjson_is_arr(bindings) && b < yyjson_arr_size(bindings); ++b)
            {
                yyjson_val *binding = yyjson_arr_get(bindings, b);
                const char *device = json_string(binding, "device", "");
                const float scale = json_float(binding, "scale", 1.0f);
                if (SDL_strcmp(device, "keyboard") == 0)
                {
                    SDL_Scancode code = scancode_from_json(json_string(binding, "key", NULL));
                    if (code != SDL_SCANCODE_UNKNOWN)
                        sdl3d_input_bind_key(input, action_id, code);
                }
                else if (SDL_strcmp(device, "gamepad") == 0)
                {
                    const char *axis = json_string(binding, "axis", NULL);
                    const char *button = json_string(binding, "button", NULL);
                    if (axis != NULL)
                        sdl3d_input_bind_gamepad_axis(input, action_id, gamepad_axis_from_json(axis), scale);
                    else if (button != NULL)
                        sdl3d_input_bind_gamepad_button(input, action_id, gamepad_button_from_json(button));
                }
            }
        }
    }
    return true;
}

static bool load_timers(sdl3d_game_data_runtime *runtime, yyjson_val *logic, char *error_buffer, int error_buffer_size)
{
    yyjson_val *timers = obj_get(logic, "timers");
    if (!yyjson_is_arr(timers))
        return true;

    const int count = (int)yyjson_arr_size(timers);
    runtime->timers = (named_timer *)SDL_calloc((size_t)count, sizeof(*runtime->timers));
    if (runtime->timers == NULL && count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate timer table");
        return false;
    }
    runtime->timer_count = count;

    for (int i = 0; i < count; ++i)
    {
        yyjson_val *timer = yyjson_arr_get(timers, (size_t)i);
        runtime->timers[i].name = json_string(timer, "name", NULL);
        runtime->timers[i].delay = json_float(timer, "delay", 0.0f);
        runtime->timers[i].signal_id = sdl3d_game_data_find_signal(runtime, json_string(timer, "signal", NULL));
        runtime->timers[i].repeating = json_bool(timer, "repeating", false);
        runtime->timers[i].interval = json_float(timer, "interval", runtime->timers[i].delay);
    }
    return true;
}

static bool load_script_entry(sdl3d_game_data_runtime *runtime, script_entry *entry, char *error_buffer,
                              int error_buffer_size)
{
    if (entry == NULL)
        return false;
    if (entry->loaded)
        return true;
    if (entry->loading)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script dependency cycle at %s", entry->id);
        }
        return false;
    }

    entry->loading = true;
    for (int i = 0; i < entry->dependency_count; ++i)
    {
        script_entry *dependency = find_script(runtime, entry->dependencies[i]);
        if (dependency == NULL)
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s depends on missing script %s",
                             entry->id, entry->dependencies[i]);
            }
            entry->loading = false;
            return false;
        }
        if (!load_script_entry(runtime, dependency, error_buffer, error_buffer_size))
        {
            entry->loading = false;
            return false;
        }
    }

    char *resolved_path = path_join(runtime->base_dir, entry->path);
    if (resolved_path == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to resolve script path for %s", entry->id);
        }
        entry->loading = false;
        return false;
    }

    sdl3d_asset_buffer script_buffer;
    SDL_zero(script_buffer);
    char asset_error[256];
    if (!sdl3d_asset_resolver_read_file(runtime->assets, resolved_path, &script_buffer, asset_error,
                                        (int)sizeof(asset_error)))
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s (%s) could not be read: %s", entry->id,
                         entry->path, asset_error);
        }
        SDL_free(resolved_path);
        entry->loading = false;
        return false;
    }

    char script_error[512];
    const bool ok = sdl3d_script_engine_load_module_buffer(runtime->scripts, script_buffer.data, script_buffer.size,
                                                           resolved_path, entry->module, &entry->module_ref,
                                                           script_error, (int)sizeof(script_error));
    sdl3d_asset_buffer_free(&script_buffer);
    SDL_free(resolved_path);
    if (!ok)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s (%s) failed: %s", entry->id,
                         entry->path, script_error);
        }
        entry->loading = false;
        return false;
    }

    entry->loaded = true;
    entry->loading = false;
    return true;
}

static bool load_script_index_into_engine(sdl3d_game_data_runtime *runtime, sdl3d_asset_resolver *assets,
                                          sdl3d_script_engine *engine, int index, sdl3d_script_ref *module_refs,
                                          bool *loading, bool *loaded, char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || assets == NULL || engine == NULL || index < 0 || index >= runtime->script_count ||
        module_refs == NULL || loading == NULL || loaded == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid Lua reload arguments");
        return false;
    }
    if (loaded[index])
        return true;
    if (loading[index])
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script dependency cycle at %s",
                         runtime->script_entries[index].id);
        }
        return false;
    }

    script_entry *entry = &runtime->script_entries[index];
    loading[index] = true;
    for (int dep = 0; dep < entry->dependency_count; ++dep)
    {
        script_entry *dependency = find_script(runtime, entry->dependencies[dep]);
        const int dependency_index = dependency != NULL ? (int)(dependency - runtime->script_entries) : -1;
        if (dependency_index < 0 ||
            !load_script_index_into_engine(runtime, assets, engine, dependency_index, module_refs, loading, loaded,
                                           error_buffer, error_buffer_size))
        {
            loading[index] = false;
            return false;
        }
    }

    char *resolved_path = path_join(runtime->base_dir, entry->path);
    if (resolved_path == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to resolve script path for %s", entry->id);
        }
        loading[index] = false;
        return false;
    }

    sdl3d_asset_buffer script_buffer;
    SDL_zero(script_buffer);
    char asset_error[256];
    if (!sdl3d_asset_resolver_read_file(assets, resolved_path, &script_buffer, asset_error, (int)sizeof(asset_error)))
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s (%s) could not be read: %s", entry->id,
                         entry->path, asset_error);
        }
        SDL_free(resolved_path);
        loading[index] = false;
        return false;
    }

    char script_error[512];
    const bool ok = sdl3d_script_engine_load_module_buffer(engine, script_buffer.data, script_buffer.size,
                                                           resolved_path, entry->module, &module_refs[index],
                                                           script_error, (int)sizeof(script_error));
    sdl3d_asset_buffer_free(&script_buffer);
    SDL_free(resolved_path);
    if (!ok)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s (%s) failed: %s", entry->id,
                         entry->path, script_error);
        }
        loading[index] = false;
        return false;
    }

    loaded[index] = true;
    loading[index] = false;
    return true;
}

static bool load_scripts(sdl3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size)
{
    yyjson_val *scripts = obj_get(root, "scripts");
    if (!yyjson_is_arr(scripts) || yyjson_arr_size(scripts) == 0)
        return true;

    runtime->script_count = (int)yyjson_arr_size(scripts);
    runtime->script_entries =
        (script_entry *)SDL_calloc((size_t)runtime->script_count, sizeof(*runtime->script_entries));
    if (runtime->script_entries == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate script manifest");
        return false;
    }

    for (int i = 0; i < runtime->script_count; ++i)
    {
        yyjson_val *script = yyjson_arr_get(scripts, (size_t)i);
        script_entry *entry = &runtime->script_entries[i];
        entry->id = json_string(script, "id", NULL);
        entry->path = json_string(script, "path", NULL);
        entry->module = json_string(script, "module", NULL);
        entry->autoload = json_bool(script, "autoload", true);
        entry->module_ref = SDL3D_SCRIPT_REF_INVALID;
        if (entry->id == NULL || entry->id[0] == '\0' || entry->path == NULL || entry->path[0] == '\0' ||
            entry->module == NULL || entry->module[0] == '\0')
        {
            set_error(error_buffer, error_buffer_size, "script entry requires non-empty id, path, and module");
            return false;
        }

        for (int prior = 0; prior < i; ++prior)
        {
            if (SDL_strcmp(runtime->script_entries[prior].id, entry->id) == 0)
            {
                set_error(error_buffer, error_buffer_size, "duplicate script id");
                return false;
            }
            if (SDL_strcmp(runtime->script_entries[prior].module, entry->module) == 0)
            {
                set_error(error_buffer, error_buffer_size, "duplicate script module");
                return false;
            }
        }

        yyjson_val *dependencies = obj_get(script, "dependencies");
        if (yyjson_is_arr(dependencies) && yyjson_arr_size(dependencies) > 0)
        {
            entry->dependency_count = (int)yyjson_arr_size(dependencies);
            entry->dependencies =
                (const char **)SDL_calloc((size_t)entry->dependency_count, sizeof(*entry->dependencies));
            if (entry->dependencies == NULL)
            {
                set_error(error_buffer, error_buffer_size, "failed to allocate script dependencies");
                return false;
            }
            for (int dep = 0; dep < entry->dependency_count; ++dep)
            {
                yyjson_val *dependency = yyjson_arr_get(dependencies, (size_t)dep);
                if (!yyjson_is_str(dependency) || yyjson_get_str(dependency)[0] == '\0')
                {
                    set_error(error_buffer, error_buffer_size, "script dependencies must be non-empty strings");
                    return false;
                }
                entry->dependencies[dep] = yyjson_get_str(dependency);
            }
        }
    }

    for (int i = 0; i < runtime->script_count; ++i)
    {
        script_entry *entry = &runtime->script_entries[i];
        for (int dep = 0; dep < entry->dependency_count; ++dep)
        {
            if (find_script(runtime, entry->dependencies[dep]) == NULL)
            {
                if (error_buffer != NULL && error_buffer_size > 0)
                {
                    SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s depends on missing script %s",
                                 entry->id, entry->dependencies[dep]);
                }
                return false;
            }
        }
    }

    runtime->scripts = sdl3d_script_engine_create();
    if (runtime->scripts == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to create Lua script engine");
        return false;
    }

    register_lua_api(runtime, runtime->scripts);
    for (int i = 0; i < runtime->script_count; ++i)
    {
        if (runtime->script_entries[i].autoload &&
            !load_script_entry(runtime, &runtime->script_entries[i], error_buffer, error_buffer_size))
        {
            return false;
        }
    }
    return true;
}

static bool load_lua_adapters(sdl3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                              int error_buffer_size)
{
    yyjson_val *adapters = obj_get(root, "adapters");
    if (!yyjson_is_arr(adapters))
        return true;

    for (size_t i = 0; i < yyjson_arr_size(adapters); ++i)
    {
        yyjson_val *adapter = yyjson_arr_get(adapters, i);
        const char *function_name = json_string(adapter, "function", NULL);
        if (function_name == NULL)
            continue;
        if (runtime->scripts == NULL)
        {
            set_error(error_buffer, error_buffer_size, "Lua adapter declared without any loaded scripts");
            return false;
        }

        const char *name = json_string(adapter, "name", NULL);
        const char *script_id = json_string(adapter, "script", NULL);
        script_entry *script = find_script(runtime, script_id);
        if (script == NULL)
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua adapter %s references missing script %s",
                             name != NULL ? name : "<unnamed>", script_id != NULL ? script_id : "<null>");
            }
            return false;
        }
        if (!load_script_entry(runtime, script, error_buffer, error_buffer_size))
            return false;

        sdl3d_script_ref function_ref = SDL3D_SCRIPT_REF_INVALID;
        char script_error[256];
        if (!sdl3d_script_engine_ref_module_function(runtime->scripts, script->module_ref, function_name, &function_ref,
                                                     script_error, (int)sizeof(script_error)))
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size,
                             "Lua adapter %s function %s in script %s failed: %s", name != NULL ? name : "<unnamed>",
                             function_name, script->id, script_error);
            }
            return false;
        }

        if (!set_adapter_lua_function(runtime, name, script->id, function_name, function_ref))
        {
            sdl3d_script_engine_unref(runtime->scripts, function_ref);
            set_error(error_buffer, error_buffer_size, "failed to register Lua adapter");
            return false;
        }
    }
    return true;
}

static int action_signal_id(sdl3d_game_data_runtime *runtime, yyjson_val *action, const char *key)
{
    return sdl3d_game_data_find_signal(runtime, json_string(action, key, NULL));
}

static bool execute_action_array(sdl3d_game_data_runtime *runtime, yyjson_val *actions,
                                 const sdl3d_properties *payload);

static bool compare_value(const sdl3d_value *left, const char *op, yyjson_val *right)
{
    if (left == NULL || op == NULL || right == NULL)
        return false;

    if (left->type == SDL3D_VALUE_INT && yyjson_is_num(right))
    {
        const int value = (int)yyjson_get_int(right);
        if (SDL_strcmp(op, ">=") == 0)
            return left->as_int >= value;
        if (SDL_strcmp(op, ">") == 0)
            return left->as_int > value;
        if (SDL_strcmp(op, "<=") == 0)
            return left->as_int <= value;
        if (SDL_strcmp(op, "<") == 0)
            return left->as_int < value;
        if (SDL_strcmp(op, "==") == 0)
            return left->as_int == value;
    }
    if (left->type == SDL3D_VALUE_FLOAT && yyjson_is_num(right))
    {
        const float value = (float)yyjson_get_real(right);
        if (SDL_strcmp(op, ">=") == 0)
            return left->as_float >= value;
        if (SDL_strcmp(op, ">") == 0)
            return left->as_float > value;
        if (SDL_strcmp(op, "<=") == 0)
            return left->as_float <= value;
        if (SDL_strcmp(op, "<") == 0)
            return left->as_float < value;
        if (SDL_strcmp(op, "==") == 0)
            return left->as_float == value;
    }
    if (left->type == SDL3D_VALUE_BOOL && yyjson_is_bool(right) && SDL_strcmp(op, "==") == 0)
        return left->as_bool == yyjson_get_bool(right);
    if (left->type == SDL3D_VALUE_STRING && yyjson_is_str(right) && SDL_strcmp(op, "==") == 0)
        return SDL_strcmp(left->as_string, yyjson_get_str(right)) == 0;
    return false;
}

static yyjson_val *find_ui_text_json(const sdl3d_game_data_runtime *runtime, const char *name)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *scene_texts = obj_get(obj_get(scene != NULL ? scene->root : NULL, "ui"), "text");
    for (size_t i = 0; name != NULL && yyjson_is_arr(scene_texts) && i < yyjson_arr_size(scene_texts); ++i)
    {
        yyjson_val *text = yyjson_arr_get(scene_texts, i);
        const char *text_name = json_string(text, "name", NULL);
        if (text_name != NULL && SDL_strcmp(text_name, name) == 0)
            return text;
    }
    yyjson_val *scene_menus = obj_get(obj_get(scene != NULL ? scene->root : NULL, "ui"), "menus");
    for (size_t i = 0; name != NULL && yyjson_is_arr(scene_menus) && i < yyjson_arr_size(scene_menus); ++i)
    {
        yyjson_val *menu = yyjson_arr_get(scene_menus, i);
        const char *menu_name = json_string(menu, "name", NULL);
        if (menu_name != NULL && SDL_strcmp(menu_name, name) == 0)
            return menu;
    }

    yyjson_val *texts = obj_get(obj_get(runtime_root(runtime), "ui"), "text");
    for (size_t i = 0; name != NULL && yyjson_is_arr(texts) && i < yyjson_arr_size(texts); ++i)
    {
        yyjson_val *text = yyjson_arr_get(texts, i);
        const char *text_name = json_string(text, "name", NULL);
        if (text_name != NULL && SDL_strcmp(text_name, name) == 0)
            return text;
    }
    yyjson_val *menus = obj_get(obj_get(runtime_root(runtime), "ui"), "menus");
    for (size_t i = 0; name != NULL && yyjson_is_arr(menus) && i < yyjson_arr_size(menus); ++i)
    {
        yyjson_val *menu = yyjson_arr_get(menus, i);
        const char *menu_name = json_string(menu, "name", NULL);
        if (menu_name != NULL && SDL_strcmp(menu_name, name) == 0)
            return menu;
    }
    return NULL;
}

static yyjson_val *find_ui_image_json(const sdl3d_game_data_runtime *runtime, const char *name)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *scene_images = obj_get(obj_get(scene != NULL ? scene->root : NULL, "ui"), "images");
    for (size_t i = 0; name != NULL && yyjson_is_arr(scene_images) && i < yyjson_arr_size(scene_images); ++i)
    {
        yyjson_val *image = yyjson_arr_get(scene_images, i);
        const char *image_name = json_string(image, "name", NULL);
        if (image_name != NULL && SDL_strcmp(image_name, name) == 0)
            return image;
    }

    yyjson_val *images = obj_get(obj_get(runtime_root(runtime), "ui"), "images");
    for (size_t i = 0; name != NULL && yyjson_is_arr(images) && i < yyjson_arr_size(images); ++i)
    {
        yyjson_val *image = yyjson_arr_get(images, i);
        const char *image_name = json_string(image, "name", NULL);
        if (image_name != NULL && SDL_strcmp(image_name, name) == 0)
            return image;
    }
    return NULL;
}

static bool value_equals_json_bool(const sdl3d_value *left, bool right)
{
    return left != NULL && left->type == SDL3D_VALUE_BOOL && left->as_bool == right;
}

static bool eval_data_condition(const sdl3d_game_data_runtime *runtime, yyjson_val *condition,
                                const sdl3d_game_data_ui_metrics *metrics)
{
    if (condition == NULL)
        return true;
    if (!yyjson_is_obj(condition))
        return false;

    const char *type = json_string(condition, "type", "");
    if (SDL_strcmp(type, "always") == 0)
        return true;
    if (SDL_strcmp(type, "camera.active") == 0)
    {
        const char *camera = json_string(condition, "camera", NULL);
        const char *active = sdl3d_game_data_active_camera(runtime);
        return camera != NULL && active != NULL && SDL_strcmp(camera, active) == 0;
    }
    if (SDL_strcmp(type, "app.paused") == 0)
    {
        const bool expected = json_bool(condition, "equals", true);
        return (metrics != NULL && metrics->paused) == expected;
    }
    if (SDL_strcmp(type, "menu.selected") == 0)
    {
        const scene_entry *scene = active_scene_entry_const(runtime);
        const scene_menu_state *menu = find_scene_menu_const(scene, json_string(condition, "menu", NULL));
        return menu != NULL && menu->selected_index == json_int(condition, "index", -1);
    }
    if (SDL_strcmp(type, "property.compare") == 0)
    {
        sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, json_string(condition, "target", NULL));
        const char *key = json_string(condition, "key", NULL);
        const char *op = json_string(condition, "op", NULL);
        return target != NULL && key != NULL &&
               compare_value(sdl3d_properties_get_value(target->props, key), op, obj_get(condition, "value"));
    }
    if (SDL_strcmp(type, "property.bool") == 0)
    {
        sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, json_string(condition, "target", NULL));
        const char *key = json_string(condition, "key", NULL);
        return target != NULL && key != NULL &&
               value_equals_json_bool(sdl3d_properties_get_value(target->props, key),
                                      json_bool(condition, "equals", true));
    }
    if (SDL_strcmp(type, "all") == 0 || SDL_strcmp(type, "any") == 0)
    {
        yyjson_val *conditions = obj_get(condition, "conditions");
        if (!yyjson_is_arr(conditions))
            return false;
        const bool require_all = SDL_strcmp(type, "all") == 0;
        bool saw_any = false;
        for (size_t i = 0; i < yyjson_arr_size(conditions); ++i)
        {
            saw_any = true;
            const bool passed = eval_data_condition(runtime, yyjson_arr_get(conditions, i), metrics);
            if (require_all && !passed)
                return false;
            if (!require_all && passed)
                return true;
        }
        return require_all && saw_any;
    }
    if (SDL_strcmp(type, "not") == 0)
        return !eval_data_condition(runtime, obj_get(condition, "condition"), metrics);
    return false;
}

static bool ui_text_authored_is_visible(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_text *text,
                                        const sdl3d_game_data_ui_metrics *metrics)
{
    if (runtime == NULL || text == NULL)
        return false;
    yyjson_val *text_json = find_ui_text_json(runtime, text->name);
    yyjson_val *condition = obj_get(text_json, "visible_if");
    if (condition != NULL)
        return eval_data_condition(runtime, condition, metrics);
    return text->visible == NULL || SDL_strcmp(text->visible, "always") == 0;
}

static bool ui_image_authored_is_visible(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_image *image,
                                         const sdl3d_game_data_ui_metrics *metrics)
{
    if (runtime == NULL || image == NULL)
        return false;
    yyjson_val *image_json = find_ui_image_json(runtime, image->name);
    yyjson_val *condition = obj_get(image_json, "visible_if");
    if (condition != NULL)
        return eval_data_condition(runtime, condition, metrics);
    return image->visible == NULL || SDL_strcmp(image->visible, "always") == 0;
}

static float clamp_unit(float value)
{
    return SDL_clamp(value, 0.0f, 1.0f);
}

static Uint8 multiply_u8(Uint8 value, float multiplier)
{
    return (Uint8)SDL_clamp((int)((float)value * multiplier + 0.5f), 0, 255);
}

static sdl3d_color apply_ui_state_color(sdl3d_color color, const sdl3d_game_data_ui_state *state)
{
    if (state == NULL)
        return color;

    if ((state->flags & SDL3D_GAME_DATA_UI_STATE_TINT) != 0u)
    {
        color.r = multiply_u8(color.r, (float)state->tint.r / 255.0f);
        color.g = multiply_u8(color.g, (float)state->tint.g / 255.0f);
        color.b = multiply_u8(color.b, (float)state->tint.b / 255.0f);
        color.a = multiply_u8(color.a, (float)state->tint.a / 255.0f);
    }
    if ((state->flags & SDL3D_GAME_DATA_UI_STATE_ALPHA) != 0u)
        color.a = multiply_u8(color.a, clamp_unit(state->alpha));
    return color;
}

bool sdl3d_game_data_resolve_ui_text(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_text *text,
                                     const sdl3d_game_data_ui_metrics *metrics, sdl3d_game_data_ui_text *out_text,
                                     bool *out_visible)
{
    if (out_visible != NULL)
        *out_visible = false;
    if (runtime == NULL || text == NULL || out_text == NULL)
        return false;

    sdl3d_game_data_ui_text resolved = *text;
    bool visible = ui_text_authored_is_visible(runtime, text, metrics);
    sdl3d_game_data_ui_state state;
    if (sdl3d_game_data_get_ui_state(runtime, text->name, &state))
    {
        if ((state.flags & SDL3D_GAME_DATA_UI_STATE_VISIBLE) != 0u)
            visible = state.visible;
        if ((state.flags & SDL3D_GAME_DATA_UI_STATE_OFFSET) != 0u)
        {
            resolved.x += state.offset_x;
            resolved.y += state.offset_y;
        }
        if ((state.flags & SDL3D_GAME_DATA_UI_STATE_SCALE) != 0u)
            resolved.scale *= state.scale;
        resolved.color = apply_ui_state_color(resolved.color, &state);
    }

    if (resolved.scale <= 0.0f || resolved.color.a == 0)
        visible = false;
    if (out_visible != NULL)
        *out_visible = visible;
    *out_text = resolved;
    return true;
}

bool sdl3d_game_data_resolve_ui_image(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_image *image,
                                      const sdl3d_game_data_ui_metrics *metrics, sdl3d_game_data_ui_image *out_image,
                                      bool *out_visible)
{
    if (out_visible != NULL)
        *out_visible = false;
    if (runtime == NULL || image == NULL || out_image == NULL)
        return false;

    sdl3d_game_data_ui_image resolved = *image;
    bool visible = ui_image_authored_is_visible(runtime, image, metrics);
    sdl3d_game_data_ui_state state;
    if (sdl3d_game_data_get_ui_state(runtime, image->name, &state))
    {
        if ((state.flags & SDL3D_GAME_DATA_UI_STATE_VISIBLE) != 0u)
            visible = state.visible;
        if ((state.flags & SDL3D_GAME_DATA_UI_STATE_OFFSET) != 0u)
        {
            resolved.x += state.offset_x;
            resolved.y += state.offset_y;
        }
        if ((state.flags & SDL3D_GAME_DATA_UI_STATE_SCALE) != 0u)
            resolved.scale *= state.scale;
        resolved.color = apply_ui_state_color(resolved.color, &state);
    }

    if (resolved.scale <= 0.0f || resolved.color.a == 0)
        visible = false;
    if (out_visible != NULL)
        *out_visible = visible;
    *out_image = resolved;
    return true;
}

bool sdl3d_game_data_ui_text_is_visible(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_text *text,
                                        const sdl3d_game_data_ui_metrics *metrics)
{
    bool visible = false;
    sdl3d_game_data_ui_text resolved;
    return sdl3d_game_data_resolve_ui_text(runtime, text, metrics, &resolved, &visible) && visible;
}

bool sdl3d_game_data_ui_image_is_visible(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_image *image,
                                         const sdl3d_game_data_ui_metrics *metrics)
{
    bool visible = false;
    sdl3d_game_data_ui_image resolved;
    return sdl3d_game_data_resolve_ui_image(runtime, image, metrics, &resolved, &visible) && visible;
}

bool sdl3d_game_data_app_pause_allowed(const sdl3d_game_data_runtime *runtime,
                                       const sdl3d_game_data_ui_metrics *metrics)
{
    if (runtime == NULL)
        return false;

    yyjson_val *pause = obj_get(obj_get(runtime_root(runtime), "app"), "pause");
    yyjson_val *condition = obj_get(pause, "allowed_if");
    if (condition == NULL)
        return true;
    return eval_data_condition(runtime, condition, metrics);
}

static yyjson_val *find_presentation_clock(const sdl3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *clocks = obj_get(obj_get(runtime_root(runtime), "presentation"), "clocks");
    for (size_t i = 0; name != NULL && yyjson_is_arr(clocks) && i < yyjson_arr_size(clocks); ++i)
    {
        yyjson_val *clock = yyjson_arr_get(clocks, i);
        const char *clock_name = json_string(clock, "name", NULL);
        if (clock_name != NULL && SDL_strcmp(clock_name, name) == 0)
            return clock;
    }
    return NULL;
}

static sdl3d_registered_actor *clock_actor(const sdl3d_game_data_runtime *runtime, yyjson_val *clock)
{
    return sdl3d_game_data_find_actor(runtime, json_string(clock, "target", NULL));
}

static float clock_property_float(const sdl3d_game_data_runtime *runtime, yyjson_val *ref, float fallback)
{
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(ref, "target", NULL));
    const char *key = json_string(ref, "key", NULL);
    return actor != NULL && key != NULL ? sdl3d_properties_get_float(actor->props, key, fallback) : fallback;
}

bool sdl3d_game_data_update_presentation_clocks(sdl3d_game_data_runtime *runtime, float dt, bool paused,
                                                bool pause_entered)
{
    if (runtime == NULL)
        return false;
    yyjson_val *clocks = obj_get(obj_get(runtime_root(runtime), "presentation"), "clocks");
    if (!yyjson_is_arr(clocks))
        return true;

    sdl3d_game_data_ui_metrics metrics;
    SDL_zero(metrics);
    metrics.paused = paused;
    for (size_t i = 0; i < yyjson_arr_size(clocks); ++i)
    {
        yyjson_val *clock = yyjson_arr_get(clocks, i);
        sdl3d_registered_actor *actor = clock_actor(runtime, clock);
        const char *key = json_string(clock, "key", NULL);
        if (actor == NULL || key == NULL)
            continue;

        float value = sdl3d_properties_get_float(actor->props, key, json_float(clock, "initial", 0.0f));
        if (pause_entered && json_bool(clock, "reset_on_pause_enter", false))
            value = json_float(clock, "reset_value", 0.0f);

        yyjson_val *condition = obj_get(clock, "active_if");
        const bool active = condition == NULL || eval_data_condition(runtime, condition, &metrics);
        if (active)
        {
            const float speed =
                clock_property_float(runtime, obj_get(clock, "speed_property"), json_float(clock, "speed", 1.0f));
            value += dt * speed;
            const float wrap = json_float(clock, "wrap", 0.0f);
            if (wrap > 0.0f)
            {
                while (value >= wrap)
                    value -= wrap;
                while (value < 0.0f)
                    value += wrap;
            }
        }
        sdl3d_properties_set_float(actor->props, key, value);
    }
    return true;
}

float sdl3d_game_data_ui_pulse_phase(const sdl3d_game_data_runtime *runtime, float fallback)
{
    if (runtime == NULL)
        return fallback;
    const char *clock_name = json_string(obj_get(runtime_root(runtime), "presentation"), "ui_pulse_clock", NULL);
    yyjson_val *clock = find_presentation_clock(runtime, clock_name);
    sdl3d_registered_actor *actor = clock_actor(runtime, clock);
    const char *key = json_string(clock, "key", NULL);
    return actor != NULL && key != NULL ? sdl3d_properties_get_float(actor->props, key, fallback) : fallback;
}

float sdl3d_game_data_fps_sample_seconds(const sdl3d_game_data_runtime *runtime, float fallback)
{
    if (runtime == NULL)
        return fallback;
    return json_float(obj_get(obj_get(runtime_root(runtime), "presentation"), "metrics"), "fps_sample_seconds",
                      fallback);
}

typedef enum ui_value_type
{
    UI_VALUE_NONE,
    UI_VALUE_INT,
    UI_VALUE_FLOAT,
    UI_VALUE_UINT64,
    UI_VALUE_BOOL,
    UI_VALUE_STRING,
} ui_value_type;

typedef struct ui_value
{
    ui_value_type type;
    union {
        int as_int;
        float as_float;
        Uint64 as_uint64;
        bool as_bool;
        const char *as_string;
    };
} ui_value;

static bool read_ui_binding_value(const sdl3d_game_data_runtime *runtime, yyjson_val *binding,
                                  const sdl3d_game_data_ui_metrics *metrics, ui_value *out_value)
{
    if (out_value != NULL)
        SDL_zero(*out_value);
    if (binding == NULL || out_value == NULL)
        return false;

    const char *type = json_string(binding, "type", "");
    if (SDL_strcmp(type, "metric") == 0)
    {
        const char *metric = json_string(binding, "metric", NULL);
        if (metric != NULL && SDL_strcmp(metric, "fps") == 0)
        {
            out_value->type = UI_VALUE_FLOAT;
            out_value->as_float = metrics != NULL ? metrics->fps : 0.0f;
            return true;
        }
        if (metric != NULL && SDL_strcmp(metric, "frame") == 0)
        {
            out_value->type = UI_VALUE_UINT64;
            out_value->as_uint64 = metrics != NULL ? metrics->frame : 0u;
            return true;
        }
        if (metric != NULL && SDL_strcmp(metric, "paused") == 0)
        {
            out_value->type = UI_VALUE_BOOL;
            out_value->as_bool = metrics != NULL && metrics->paused;
            return true;
        }
        return false;
    }

    if (SDL_strcmp(type, "property") == 0)
    {
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(binding, "entity", NULL));
        const char *key = json_string(binding, "key", NULL);
        const sdl3d_value *value = actor != NULL && key != NULL ? sdl3d_properties_get_value(actor->props, key) : NULL;
        if (value == NULL)
            return false;
        switch (value->type)
        {
        case SDL3D_VALUE_INT:
            out_value->type = UI_VALUE_INT;
            out_value->as_int = value->as_int;
            return true;
        case SDL3D_VALUE_FLOAT:
            out_value->type = UI_VALUE_FLOAT;
            out_value->as_float = value->as_float;
            return true;
        case SDL3D_VALUE_BOOL:
            out_value->type = UI_VALUE_BOOL;
            out_value->as_bool = value->as_bool;
            return true;
        case SDL3D_VALUE_STRING:
            out_value->type = UI_VALUE_STRING;
            out_value->as_string = value->as_string != NULL ? value->as_string : "";
            return true;
        case SDL3D_VALUE_VEC3:
        case SDL3D_VALUE_COLOR:
            return false;
        }
    }
    return false;
}

static bool format_bound_ui_text(const char *format, const ui_value *values, int value_count, char *buffer,
                                 size_t buffer_size)
{
    if (format == NULL || values == NULL || buffer == NULL || buffer_size == 0)
        return false;

    if (value_count == 1)
    {
        if (values[0].type == UI_VALUE_INT)
            SDL_snprintf(buffer, buffer_size, format, values[0].as_int);
        else if (values[0].type == UI_VALUE_FLOAT)
            SDL_snprintf(buffer, buffer_size, format, values[0].as_float);
        else if (values[0].type == UI_VALUE_UINT64)
            SDL_snprintf(buffer, buffer_size, format, (unsigned long long)values[0].as_uint64);
        else if (values[0].type == UI_VALUE_BOOL)
            SDL_snprintf(buffer, buffer_size, format, values[0].as_bool ? 1 : 0);
        else if (values[0].type == UI_VALUE_STRING)
            SDL_snprintf(buffer, buffer_size, format, values[0].as_string);
        else
            return false;
        return true;
    }
    if (value_count == 2 && values[0].type == UI_VALUE_INT && values[1].type == UI_VALUE_INT)
    {
        SDL_snprintf(buffer, buffer_size, format, values[0].as_int, values[1].as_int);
        return true;
    }
    if (value_count == 2 && values[0].type == UI_VALUE_FLOAT && values[1].type == UI_VALUE_UINT64)
    {
        SDL_snprintf(buffer, buffer_size, format, values[0].as_float, (unsigned long long)values[1].as_uint64);
        return true;
    }
    return false;
}

bool sdl3d_game_data_format_ui_text(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_text *text,
                                    const sdl3d_game_data_ui_metrics *metrics, char *buffer, size_t buffer_size)
{
    if (buffer != NULL && buffer_size > 0)
        buffer[0] = '\0';
    if (runtime == NULL || text == NULL || buffer == NULL || buffer_size == 0)
        return false;

    yyjson_val *text_json = find_ui_text_json(runtime, text->name);
    yyjson_val *bindings = obj_get(text_json, "bindings");
    if (yyjson_is_arr(bindings) && yyjson_arr_size(bindings) > 0)
    {
        ui_value values[4];
        const int value_count = (int)SDL_min(yyjson_arr_size(bindings), SDL_arraysize(values));
        for (int i = 0; i < value_count; ++i)
        {
            if (!read_ui_binding_value(runtime, yyjson_arr_get(bindings, (size_t)i), metrics, &values[i]))
                return false;
        }
        return format_bound_ui_text(text->format, values, value_count, buffer, buffer_size);
    }

    if (text->text != NULL)
    {
        SDL_snprintf(buffer, buffer_size, "%s", text->text);
        return true;
    }
    return false;
}

static bool execute_one_action(sdl3d_game_data_runtime *runtime, yyjson_val *action, const sdl3d_properties *payload)
{
    const char *type = json_string(action, "type", "");
    sdl3d_signal_bus *bus = runtime_bus(runtime);
    sdl3d_timer_pool *timers = runtime_timers(runtime);

    if (SDL_strcmp(type, "signal.emit") == 0)
    {
        const int signal_id = action_signal_id(runtime, action, "signal");
        if (signal_id >= 0 && bus != NULL)
            sdl3d_signal_emit(bus, signal_id, payload);
        return signal_id >= 0;
    }

    if (SDL_strcmp(type, "timer.start") == 0)
    {
        const int timer_index = find_timer_index(runtime, json_string(action, "timer", NULL));
        if (timer_index < 0 || timers == NULL)
            return false;
        const named_timer *timer = &runtime->timers[timer_index];
        return sdl3d_timer_start(timers, json_float(action, "delay", timer->delay), timer->signal_id, timer->repeating,
                                 timer->interval) != 0;
    }

    if (SDL_strcmp(type, "property.set") == 0 || SDL_strcmp(type, "property.add") == 0)
    {
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        const char *key = json_string(action, "key", NULL);
        yyjson_val *value = obj_get(action, "value");
        if (actor == NULL || key == NULL || value == NULL)
            return false;

        if (SDL_strcmp(type, "property.add") == 0)
        {
            const sdl3d_value *existing = sdl3d_properties_get_value(actor->props, key);
            if (existing != NULL && existing->type == SDL3D_VALUE_INT && yyjson_is_num(value))
                sdl3d_properties_set_int(actor->props, key, existing->as_int + (int)yyjson_get_int(value));
            else if (existing != NULL && existing->type == SDL3D_VALUE_FLOAT && yyjson_is_num(value))
                sdl3d_properties_set_float(actor->props, key, existing->as_float + (float)yyjson_get_real(value));
            else
                return false;
        }
        else
        {
            set_actor_property_from_json(actor, key, value);
        }
        return true;
    }

    if (SDL_strcmp(type, "transform.set_position") == 0)
    {
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        if (actor == NULL)
            return false;
        actor_set_position(actor, json_vec3(action, "position", actor->position));
        return true;
    }

    if (SDL_strcmp(type, "camera.toggle") == 0)
    {
        const char *camera = json_string(action, "camera", NULL);
        const char *fallback = json_string(action, "fallback", NULL);
        runtime->active_camera =
            runtime->active_camera != NULL && camera != NULL && SDL_strcmp(runtime->active_camera, camera) == 0
                ? fallback
                : camera;
        return runtime->active_camera != NULL;
    }

    if (SDL_strcmp(type, "scene.set") == 0)
        return sdl3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL), payload);

    if (SDL_strcmp(type, "adapter.invoke") == 0)
    {
        const char *adapter_name = json_string(action, "adapter", NULL);
        adapter_entry *adapter = find_adapter(runtime, adapter_name);
        if (adapter == NULL)
            return false;
        sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        return invoke_adapter(runtime, adapter, target, payload);
    }

    if (SDL_strcmp(type, "branch") == 0)
    {
        yyjson_val *condition = obj_get(action, "if");
        sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, json_string(condition, "target", NULL));
        const char *key = json_string(condition, "key", NULL);
        const char *op = json_string(condition, "op", NULL);
        const bool passed =
            target != NULL && key != NULL &&
            compare_value(sdl3d_properties_get_value(target->props, key), op, obj_get(condition, "value"));
        return execute_action_array(runtime, obj_get(action, passed ? "then" : "else"), payload);
    }

    return false;
}

static bool execute_action_array(sdl3d_game_data_runtime *runtime, yyjson_val *actions, const sdl3d_properties *payload)
{
    if (!yyjson_is_arr(actions))
        return false;

    bool ok = true;
    for (size_t i = 0; i < yyjson_arr_size(actions); ++i)
    {
        yyjson_val *action = yyjson_arr_get(actions, i);
        if (yyjson_is_obj(action))
            ok = execute_one_action(runtime, action, payload) && ok;
    }
    return ok;
}

static void execute_binding(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    binding_entry *binding = (binding_entry *)userdata;
    (void)signal_id;
    if (binding != NULL)
        execute_action_array(binding->runtime, binding->actions, payload);
}

static bool load_bindings(sdl3d_game_data_runtime *runtime, yyjson_val *logic, char *error_buffer,
                          int error_buffer_size)
{
    yyjson_val *bindings = obj_get(logic, "bindings");
    if (!yyjson_is_arr(bindings))
        return true;

    sdl3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data logic bindings require a signal bus");
        return false;
    }

    const int count = (int)yyjson_arr_size(bindings);
    runtime->bindings = (binding_entry *)SDL_calloc((size_t)count, sizeof(*runtime->bindings));
    if (runtime->bindings == NULL && count > 0)
        return false;
    runtime->binding_count = count;

    for (int i = 0; i < count; ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, (size_t)i);
        const int signal_id = sdl3d_game_data_find_signal(runtime, json_string(binding, "signal", NULL));
        if (signal_id < 0)
            continue;
        runtime->bindings[i].runtime = runtime;
        runtime->bindings[i].actions = obj_get(binding, "actions");
        runtime->bindings[i].connection_id =
            sdl3d_signal_connect(bus, signal_id, execute_binding, &runtime->bindings[i]);
        if (runtime->bindings[i].connection_id == 0)
            return false;
    }
    return true;
}

static bool load_sensors(sdl3d_game_data_runtime *runtime, yyjson_val *logic)
{
    yyjson_val *sensors = obj_get(logic, "sensors");
    if (!yyjson_is_arr(sensors))
        return true;

    const int count = (int)yyjson_arr_size(sensors);
    runtime->sensors = (sensor_entry *)SDL_calloc((size_t)count, sizeof(*runtime->sensors));
    if (runtime->sensors == NULL && count > 0)
        return false;
    runtime->sensor_count = count;

    for (int i = 0; i < count; ++i)
    {
        yyjson_val *sensor = yyjson_arr_get(sensors, (size_t)i);
        sensor_entry *entry = &runtime->sensors[i];
        const char *type = json_string(sensor, "type", "");
        if (SDL_strcmp(type, "sensor.bounds_exit") == 0)
            entry->type = GAME_DATA_SENSOR_BOUNDS_EXIT;
        else if (SDL_strcmp(type, "sensor.bounds_reflect") == 0)
            entry->type = GAME_DATA_SENSOR_BOUNDS_REFLECT;
        else if (SDL_strcmp(type, "sensor.contact_2d") == 0)
            entry->type = GAME_DATA_SENSOR_CONTACT_2D;
        else if (SDL_strcmp(type, "sensor.input_pressed") == 0)
            entry->type = GAME_DATA_SENSOR_INPUT_PRESSED;

        entry->entity = json_string(sensor, "entity", json_string(sensor, "a", NULL));
        entry->other = json_string(sensor, "b", NULL);
        entry->action = json_string(sensor, "action", NULL);
        entry->axis = json_string(sensor, "axis", NULL);
        entry->side = json_string(sensor, "side", NULL);
        entry->min_value = json_float(sensor, "min", 0.0f);
        entry->max_value = json_float(sensor, "max", 0.0f);
        entry->threshold = json_float(sensor, "threshold", 0.0f);
        entry->signal_id = sdl3d_game_data_find_signal(
            runtime, json_string(sensor, "on_enter",
                                 json_string(sensor, "on_pressed", json_string(sensor, "on_reflect", NULL))));
    }
    return true;
}

static void load_active_camera(sdl3d_game_data_runtime *runtime, yyjson_val *root)
{
    yyjson_val *cameras = obj_get(obj_get(root, "world"), "cameras");
    for (size_t i = 0; yyjson_is_arr(cameras) && i < yyjson_arr_size(cameras); ++i)
    {
        yyjson_val *camera = yyjson_arr_get(cameras, i);
        if (json_bool(camera, "active", false))
        {
            runtime->active_camera = json_string(camera, "name", NULL);
            return;
        }
    }
}

static bool load_scene_menus(scene_entry *scene, char *error_buffer, int error_buffer_size)
{
    yyjson_val *menus = obj_get(scene->root, "menus");
    if (!yyjson_is_arr(menus))
        return true;

    scene->menu_count = (int)yyjson_arr_size(menus);
    scene->menus = (scene_menu_state *)SDL_calloc((size_t)scene->menu_count, sizeof(*scene->menus));
    if (scene->menus == NULL && scene->menu_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate scene menus");
        return false;
    }

    for (int i = 0; i < scene->menu_count; ++i)
    {
        yyjson_val *menu = yyjson_arr_get(menus, (size_t)i);
        if (!yyjson_is_obj(menu) || json_string(menu, "name", NULL) == NULL)
        {
            set_error(error_buffer, error_buffer_size, "scene menu requires a non-empty name");
            return false;
        }

        yyjson_val *items = obj_get(menu, "items");
        if (!yyjson_is_arr(items) || yyjson_arr_size(items) <= 0)
        {
            set_error(error_buffer, error_buffer_size, "scene menu requires at least one item");
            return false;
        }

        scene->menus[i].menu = menu;
        scene->menus[i].item_count = (int)yyjson_arr_size(items);
        scene->menus[i].selected_index = SDL_clamp(json_int(menu, "selected", 0), 0, scene->menus[i].item_count - 1);
    }
    return true;
}

static bool load_scene_entities(scene_entry *scene, char *error_buffer, int error_buffer_size)
{
    yyjson_val *entities = obj_get(scene->root, "entities");
    if (entities == NULL)
        return true;
    if (!yyjson_is_arr(entities))
    {
        set_error(error_buffer, error_buffer_size, "scene entities must be an array");
        return false;
    }

    scene->has_entity_filter = true;
    scene->entity_count = (int)yyjson_arr_size(entities);
    if (scene->entity_count <= 0)
        return true;

    scene->entities = (const char **)SDL_calloc((size_t)scene->entity_count, sizeof(*scene->entities));
    if (scene->entities == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate scene entity list");
        return false;
    }

    for (int i = 0; i < scene->entity_count; ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, (size_t)i);
        if (!yyjson_is_str(entity) || yyjson_get_str(entity)[0] == '\0')
        {
            set_error(error_buffer, error_buffer_size, "scene entity entries must be non-empty strings");
            return false;
        }
        scene->entities[i] = yyjson_get_str(entity);
    }
    return true;
}

static bool load_scenes(sdl3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size)
{
    yyjson_val *scenes = obj_get(root, "scenes");
    yyjson_val *files = obj_get(scenes, "files");
    if (!yyjson_is_arr(files))
    {
        runtime->active_scene_index = -1;
        return true;
    }

    runtime->scene_count = (int)yyjson_arr_size(files);
    runtime->scenes = (scene_entry *)SDL_calloc((size_t)runtime->scene_count, sizeof(*runtime->scenes));
    if (runtime->scenes == NULL && runtime->scene_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate scene table");
        return false;
    }

    for (int i = 0; i < runtime->scene_count; ++i)
    {
        yyjson_val *file = yyjson_arr_get(files, (size_t)i);
        if (!yyjson_is_str(file) || yyjson_get_str(file)[0] == '\0')
        {
            set_error(error_buffer, error_buffer_size, "scene files must be non-empty strings");
            return false;
        }

        char *resolved_path = path_join(runtime->base_dir, yyjson_get_str(file));
        if (resolved_path == NULL)
        {
            set_error(error_buffer, error_buffer_size, "failed to resolve scene path");
            return false;
        }

        sdl3d_asset_buffer scene_buffer;
        SDL_zero(scene_buffer);
        char asset_error[256];
        if (!sdl3d_asset_resolver_read_file(runtime->assets, resolved_path, &scene_buffer, asset_error,
                                            (int)sizeof(asset_error)))
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size, "scene asset %s could not be read: %s",
                             yyjson_get_str(file), asset_error);
            }
            SDL_free(resolved_path);
            return false;
        }

        yyjson_read_err err;
        yyjson_doc *doc =
            yyjson_read_opts((char *)scene_buffer.data, scene_buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
        sdl3d_asset_buffer_free(&scene_buffer);
        SDL_free(resolved_path);
        if (doc == NULL)
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size, "scene yyjson error %u at byte %llu: %s",
                             err.code, (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
            }
            return false;
        }

        yyjson_val *scene_root = yyjson_doc_get_root(doc);
        const char *schema = json_string(scene_root, "schema", NULL);
        const char *name = json_string(scene_root, "name", NULL);
        if (!yyjson_is_obj(scene_root) || schema == NULL || SDL_strcmp(schema, "sdl3d.scene.v0") != 0 || name == NULL ||
            name[0] == '\0')
        {
            yyjson_doc_free(doc);
            set_error(error_buffer, error_buffer_size, "scene file has unsupported schema or missing name");
            return false;
        }
        for (int prior = 0; prior < i; ++prior)
        {
            if (SDL_strcmp(runtime->scenes[prior].name, name) == 0)
            {
                yyjson_doc_free(doc);
                set_error(error_buffer, error_buffer_size, "duplicate scene name");
                return false;
            }
        }

        runtime->scenes[i].doc = doc;
        runtime->scenes[i].root = scene_root;
        runtime->scenes[i].name = name;
        if (!load_scene_entities(&runtime->scenes[i], error_buffer, error_buffer_size) ||
            !load_scene_menus(&runtime->scenes[i], error_buffer, error_buffer_size))
            return false;
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL3D game data scene loaded: name=%s updates_game=%d renders_world=%d entities=%d menus=%d",
                     name, json_bool(scene_root, "updates_game", true) ? 1 : 0,
                     json_bool(scene_root, "renders_world", true) ? 1 : 0, runtime->scenes[i].entity_count,
                     runtime->scenes[i].menu_count);
    }

    runtime->active_scene_index = runtime->scene_count > 0 ? 0 : -1;
    const char *initial = json_string(scenes, "initial", NULL);
    if (initial != NULL)
    {
        scene_entry *scene = find_scene(runtime, initial);
        if (scene == NULL)
        {
            set_error(error_buffer, error_buffer_size, "initial scene does not reference a loaded scene");
            return false;
        }
        runtime->active_scene_index = (int)(scene - runtime->scenes);
    }
    apply_scene_camera(runtime, active_scene_entry(runtime));
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SDL3D game data initial scene: %s",
                 sdl3d_game_data_active_scene(runtime) != NULL ? sdl3d_game_data_active_scene(runtime) : "<none>");
    emit_scene_enter_signal(runtime, active_scene_entry(runtime), NULL, NULL);
    return true;
}

static void update_control_components(sdl3d_game_data_runtime *runtime, yyjson_val *root, float dt)
{
    sdl3d_input_manager *input = runtime_input(runtime);
    yyjson_val *entities = obj_get(root, "entities");
    yyjson_val *world_bounds = obj_get(obj_get(root, "world"), "bounds");
    const sdl3d_vec3 world_min = json_vec3(world_bounds, "min", sdl3d_vec3_make(-100000.0f, -100000.0f, -100000.0f));
    const sdl3d_vec3 world_max = json_vec3(world_bounds, "max", sdl3d_vec3_make(100000.0f, 100000.0f, 100000.0f));

    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, entity_name);
        yyjson_val *components = obj_get(entity, "components");
        if (actor == NULL || !actor->active || !yyjson_is_arr(components))
            continue;

        for (size_t c = 0; c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type", "");
            if (SDL_strcmp(type, "control.axis_1d") == 0 && input != NULL)
            {
                const int negative = sdl3d_game_data_find_action(runtime, json_string(component, "negative", NULL));
                const int positive = sdl3d_game_data_find_action(runtime, json_string(component, "positive", NULL));
                const int axis = axis_index(json_string(component, "axis", NULL));
                const float value = sdl3d_input_get_value(input, positive) - sdl3d_input_get_value(input, negative);
                const float speed = sdl3d_properties_get_float(actor->props, "speed", 0.0f);
                const float half_height = sdl3d_properties_get_float(actor->props, "half_height", 0.0f);
                const float lo = vec_axis(world_min, axis) + half_height;
                const float hi = vec_axis(world_max, axis) - half_height;
                sdl3d_vec3 position = actor->position;
                set_vec_axis(&position, axis, SDL_clamp(vec_axis(position, axis) + value * speed * dt, lo, hi));
                actor_set_position(actor, position);
            }
            else if (SDL_strcmp(type, "adapter.controller") == 0)
            {
                adapter_entry *adapter = find_adapter(runtime, json_string(component, "adapter", NULL));
                if (adapter != NULL)
                {
                    sdl3d_properties *payload = sdl3d_properties_create();
                    if (payload != NULL)
                    {
                        sdl3d_properties_set_string(payload, "target_actor_name", json_string(component, "target", ""));
                    }
                    invoke_adapter(runtime, adapter, actor, payload);
                    sdl3d_properties_destroy(payload);
                }
            }
        }
    }
}

static void update_motion_components(sdl3d_game_data_runtime *runtime, yyjson_val *root, float dt)
{
    yyjson_val *entities = obj_get(root, "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, entity_name);
        yyjson_val *components = obj_get(entity, "components");
        if (actor == NULL || !actor->active || !yyjson_is_arr(components))
            continue;

        for (size_t c = 0; c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            if (SDL_strcmp(json_string(component, "type", ""), "motion.velocity_2d") != 0 ||
                !sdl3d_properties_get_bool(actor->props, "active_motion", true))
            {
                continue;
            }

            const char *property = json_string(component, "property", "velocity");
            const sdl3d_vec3 velocity = actor_vec_property(actor, property);
            actor_set_position(actor, sdl3d_vec3_make(actor->position.x + velocity.x * dt,
                                                      actor->position.y + velocity.y * dt, actor->position.z));
        }
    }
}

static float move_float_toward(float value, float target, float max_delta)
{
    if (max_delta < 0.0f)
        max_delta = -max_delta;
    if (value < target)
        return SDL_min(value + max_delta, target);
    if (value > target)
        return SDL_max(value - max_delta, target);
    return target;
}

bool sdl3d_game_data_update_property_effects(sdl3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL || runtime->doc == NULL)
        return false;
    if (dt < 0.0f)
        dt = 0.0f;

    bool ok = true;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;

        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, entity_name);
        yyjson_val *components = obj_get(entity, "components");
        if (actor == NULL || !actor->active || !yyjson_is_arr(components))
            continue;

        for (size_t c = 0; c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            if (SDL_strcmp(json_string(component, "type", ""), "property.decay") != 0)
                continue;

            const char *property = json_string(component, "property", NULL);
            const sdl3d_value *existing = property != NULL ? sdl3d_properties_get_value(actor->props, property) : NULL;
            if (property == NULL || existing == NULL ||
                (existing->type != SDL3D_VALUE_FLOAT && existing->type != SDL3D_VALUE_INT))
            {
                ok = false;
                continue;
            }

            const char *rate_property = json_string(component, "rate_property", NULL);
            const float rate = rate_property != NULL ? sdl3d_properties_get_float(actor->props, rate_property, 0.0f)
                                                     : json_float(component, "rate", 1.0f);
            const float target = json_float(component, "target", 0.0f);
            float value = existing->type == SDL3D_VALUE_FLOAT ? existing->as_float : (float)existing->as_int;
            value = move_float_toward(value, target, rate * dt);
            if (obj_get(component, "min") != NULL)
                value = SDL_max(value, json_float(component, "min", value));
            if (obj_get(component, "max") != NULL)
                value = SDL_min(value, json_float(component, "max", value));

            if (existing->type == SDL3D_VALUE_INT)
                sdl3d_properties_set_int(actor->props, property, (int)SDL_lroundf(value));
            else
                sdl3d_properties_set_float(actor->props, property, value);
        }
    }
    return ok;
}

static void emit_sensor_signal(sdl3d_game_data_runtime *runtime, const sensor_entry *sensor, sdl3d_registered_actor *a,
                               sdl3d_registered_actor *b)
{
    sdl3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;

    sdl3d_properties *payload = sdl3d_properties_create();
    if (payload != NULL)
    {
        if (a != NULL)
            sdl3d_properties_set_string(payload, "actor_name", a->name);
        if (b != NULL)
            sdl3d_properties_set_string(payload, "other_actor_name", b->name);
    }
    sdl3d_signal_emit(bus, sensor->signal_id, payload);
    sdl3d_properties_destroy(payload);
}

static void update_sensors(sdl3d_game_data_runtime *runtime)
{
    for (int i = 0; i < runtime->sensor_count; ++i)
    {
        sensor_entry *sensor = &runtime->sensors[i];
        if ((sensor->entity != NULL && !active_scene_has_entity_internal(runtime, sensor->entity)) ||
            (sensor->other != NULL && !active_scene_has_entity_internal(runtime, sensor->other)))
        {
            sensor->was_active = false;
            continue;
        }

        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, sensor->entity);
        if (sensor->type == GAME_DATA_SENSOR_INPUT_PRESSED)
        {
            const int action_id = sdl3d_game_data_find_action(runtime, sensor->action);
            if (sdl3d_input_is_pressed(runtime_input(runtime), action_id))
                emit_sensor_signal(runtime, sensor, NULL, NULL);
            continue;
        }
        if (actor == NULL)
            continue;

        if (sensor->type == GAME_DATA_SENSOR_BOUNDS_EXIT)
        {
            const int axis = axis_index(sensor->axis);
            const float value = vec_axis(actor->position, axis);
            const bool active =
                SDL_strcmp(sensor->side, "min") == 0 ? value < sensor->threshold : value > sensor->threshold;
            if (active && !sensor->was_active)
                emit_sensor_signal(runtime, sensor, actor, NULL);
            sensor->was_active = active;
        }
        else if (sensor->type == GAME_DATA_SENSOR_BOUNDS_REFLECT)
        {
            const int axis = axis_index(sensor->axis);
            const float value = vec_axis(actor->position, axis);
            if (value < sensor->min_value || value > sensor->max_value)
            {
                sdl3d_vec3 position = actor->position;
                sdl3d_vec3 velocity = actor_vec_property(actor, "velocity");
                set_vec_axis(&position, axis, SDL_clamp(value, sensor->min_value, sensor->max_value));
                set_vec_axis(&velocity, axis, -vec_axis(velocity, axis));
                actor_set_position(actor, position);
                sdl3d_properties_set_vec3(actor->props, "velocity", velocity);
                emit_sensor_signal(runtime, sensor, actor, NULL);
            }
        }
        else if (sensor->type == GAME_DATA_SENSOR_CONTACT_2D)
        {
            sdl3d_registered_actor *other = sdl3d_game_data_find_actor(runtime, sensor->other);
            if (other == NULL)
                continue;
            const float radius = sdl3d_properties_get_float(actor->props, "radius", 0.0f);
            const float half_width = sdl3d_properties_get_float(other->props, "half_width", 0.0f);
            const float half_height = sdl3d_properties_get_float(other->props, "half_height", 0.0f);
            const float closest_x =
                SDL_clamp(actor->position.x, other->position.x - half_width, other->position.x + half_width);
            const float closest_y =
                SDL_clamp(actor->position.y, other->position.y - half_height, other->position.y + half_height);
            const float dx = actor->position.x - closest_x;
            const float dy = actor->position.y - closest_y;
            const bool active = dx * dx + dy * dy <= radius * radius;
            if (active && !sensor->was_active)
                emit_sensor_signal(runtime, sensor, actor, other);
            sensor->was_active = active;
        }
    }
}

bool sdl3d_game_data_update(sdl3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL || runtime->doc == NULL)
        return false;
    if (dt < 0.0f)
        dt = 0.0f;
    runtime->current_dt = dt;

    yyjson_val *root = yyjson_doc_get_root(runtime->doc);
    update_control_components(runtime, root, dt);
    update_motion_components(runtime, root, dt);
    update_sensors(runtime);
    return true;
}

bool sdl3d_game_data_register_adapter(sdl3d_game_data_runtime *runtime, const char *name,
                                      sdl3d_game_data_adapter_fn callback, void *userdata)
{
    if (runtime == NULL || name == NULL || name[0] == '\0' || callback == NULL)
        return false;
    adapter_entry *entry = find_adapter(runtime, name);
    if (entry != NULL)
    {
        SDL_free(entry->lua_script_id);
        entry->lua_script_id = NULL;
        SDL_free(entry->lua_function);
        entry->lua_function = NULL;
        if (entry->lua_function_ref != SDL3D_SCRIPT_REF_INVALID)
        {
            sdl3d_script_engine_unref(runtime->scripts, entry->lua_function_ref);
            entry->lua_function_ref = SDL3D_SCRIPT_REF_INVALID;
        }
        entry->callback = callback;
        entry->userdata = userdata;
        return true;
    }
    return append_adapter(runtime, name, callback, userdata);
}

bool sdl3d_game_data_reload_scripts(sdl3d_game_data_runtime *runtime, sdl3d_asset_resolver *assets, char *error_buffer,
                                    int error_buffer_size)
{
    if (runtime == NULL || assets == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid Lua reload arguments");
        return false;
    }
    if (runtime->script_count == 0)
        return true;

    sdl3d_script_engine *new_engine = sdl3d_script_engine_create();
    if (new_engine == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to create Lua script engine");
        return false;
    }
    register_lua_api(runtime, new_engine);

    sdl3d_script_ref *module_refs = (sdl3d_script_ref *)SDL_calloc((size_t)runtime->script_count, sizeof(*module_refs));
    sdl3d_script_ref *function_refs =
        (sdl3d_script_ref *)SDL_calloc((size_t)runtime->adapter_count, sizeof(*function_refs));
    bool *loading = (bool *)SDL_calloc((size_t)runtime->script_count, sizeof(*loading));
    bool *loaded = (bool *)SDL_calloc((size_t)runtime->script_count, sizeof(*loaded));
    if (module_refs == NULL || function_refs == NULL || loading == NULL || loaded == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate Lua reload state");
        SDL_free(module_refs);
        SDL_free(function_refs);
        SDL_free(loading);
        SDL_free(loaded);
        sdl3d_script_engine_destroy(new_engine);
        return false;
    }

    bool ok = true;
    for (int i = 0; i < runtime->script_count && ok; ++i)
    {
        if (runtime->script_entries[i].autoload)
        {
            ok = load_script_index_into_engine(runtime, assets, new_engine, i, module_refs, loading, loaded,
                                               error_buffer, error_buffer_size);
        }
    }

    for (int i = 0; i < runtime->adapter_count && ok; ++i)
    {
        adapter_entry *adapter = &runtime->adapters[i];
        if (adapter->callback != NULL || adapter->lua_script_id == NULL || adapter->lua_function == NULL)
            continue;

        script_entry *script = find_script(runtime, adapter->lua_script_id);
        const int script_index = script != NULL ? (int)(script - runtime->script_entries) : -1;
        if (script_index < 0)
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua adapter %s references missing script %s",
                             adapter->name, adapter->lua_script_id);
            }
            ok = false;
            break;
        }
        if (!load_script_index_into_engine(runtime, assets, new_engine, script_index, module_refs, loading, loaded,
                                           error_buffer, error_buffer_size))
        {
            ok = false;
            break;
        }

        char script_error[256];
        if (!sdl3d_script_engine_ref_module_function(new_engine, module_refs[script_index], adapter->lua_function,
                                                     &function_refs[i], script_error, (int)sizeof(script_error)))
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size,
                             "Lua adapter %s function %s in script %s failed: %s", adapter->name, adapter->lua_function,
                             script->id, script_error);
            }
            ok = false;
        }
    }

    if (!ok)
    {
        for (int i = 0; i < runtime->adapter_count; ++i)
            sdl3d_script_engine_unref(new_engine, function_refs[i]);
        for (int i = 0; i < runtime->script_count; ++i)
            sdl3d_script_engine_unref(new_engine, module_refs[i]);
        SDL_free(module_refs);
        SDL_free(function_refs);
        SDL_free(loading);
        SDL_free(loaded);
        sdl3d_script_engine_destroy(new_engine);
        return false;
    }

    sdl3d_script_engine *old_engine = runtime->scripts;
    for (int i = 0; i < runtime->script_count; ++i)
    {
        runtime->script_entries[i].module_ref = module_refs[i];
        runtime->script_entries[i].loaded = loaded[i];
        runtime->script_entries[i].loading = false;
    }
    runtime->scripts = new_engine;
    for (int i = 0; i < runtime->adapter_count; ++i)
    {
        if (runtime->adapters[i].callback == NULL && runtime->adapters[i].lua_function != NULL)
            runtime->adapters[i].lua_function_ref = function_refs[i];
        else
            runtime->adapters[i].lua_function_ref = SDL3D_SCRIPT_REF_INVALID;
    }

    sdl3d_script_engine_destroy(old_engine);
    SDL_free(module_refs);
    SDL_free(function_refs);
    SDL_free(loading);
    SDL_free(loaded);
    return true;
}

static bool apply_app_config_from_root(yyjson_val *root, sdl3d_game_config *out_config, char *title_buffer,
                                       int title_buffer_size, char *error_buffer, int error_buffer_size)
{
    if (!yyjson_is_obj(root) || SDL_strcmp(json_string(root, "schema", ""), "sdl3d.game.v0") != 0)
    {
        set_error(error_buffer, error_buffer_size, "unsupported or missing game data schema");
        return false;
    }

    yyjson_val *app = obj_get(root, "app");
    if (!yyjson_is_obj(app))
        return true;

    const char *title = json_string(app, "title", NULL);
    if (title != NULL && title_buffer != NULL && title_buffer_size > 0)
    {
        SDL_snprintf(title_buffer, (size_t)title_buffer_size, "%s", title);
        out_config->title = title_buffer;
    }
    out_config->width = json_int(app, "width", out_config->width);
    out_config->height = json_int(app, "height", out_config->height);
    out_config->backend = parse_backend(json_string(app, "backend", NULL), out_config->backend);
    out_config->tick_rate = json_float(app, "tick_rate", out_config->tick_rate);
    out_config->max_ticks_per_frame = json_int(app, "max_ticks_per_frame", out_config->max_ticks_per_frame);
    out_config->enable_audio = json_bool(app, "enable_audio", out_config->enable_audio);
    return true;
}

bool sdl3d_game_data_load_app_config_asset(sdl3d_asset_resolver *assets, const char *asset_path,
                                           sdl3d_game_config *out_config, char *title_buffer, int title_buffer_size,
                                           char *error_buffer, int error_buffer_size)
{
    if (assets == NULL || asset_path == NULL || asset_path[0] == '\0' || out_config == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid app config load arguments");
        return false;
    }

    sdl3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!sdl3d_asset_resolver_read_file(assets, asset_path, &buffer, asset_error, (int)sizeof(asset_error)))
    {
        if (error_buffer != NULL && error_buffer_size > 0)
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to read game data asset %s: %s", asset_path,
                         asset_error);
        return false;
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    sdl3d_asset_buffer_free(&buffer);
    if (doc == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "yyjson error %u at byte %llu: %s", err.code,
                         (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
        return false;
    }

    const bool ok = apply_app_config_from_root(yyjson_doc_get_root(doc), out_config, title_buffer, title_buffer_size,
                                               error_buffer, error_buffer_size);
    yyjson_doc_free(doc);
    return ok;
}

bool sdl3d_game_data_load_app_config_file(const char *path, sdl3d_game_config *out_config, char *title_buffer,
                                          int title_buffer_size, char *error_buffer, int error_buffer_size)
{
    if (path == NULL || path[0] == '\0' || out_config == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid app config load arguments");
        return false;
    }

    char *base_dir = path_dirname(path);
    char *asset_name = path_basename(path);
    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    if (base_dir == NULL || asset_name == NULL || assets == NULL)
    {
        SDL_free(base_dir);
        SDL_free(asset_name);
        sdl3d_asset_resolver_destroy(assets);
        set_error(error_buffer, error_buffer_size, "failed to allocate app config loader");
        return false;
    }

    char asset_error[256];
    const bool mounted = sdl3d_asset_resolver_mount_directory(assets, base_dir, asset_error, (int)sizeof(asset_error));
    bool ok = false;
    if (!mounted)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to mount game data directory %s: %s",
                         base_dir, asset_error);
    }
    else
    {
        ok = sdl3d_game_data_load_app_config_asset(assets, asset_name, out_config, title_buffer, title_buffer_size,
                                                   error_buffer, error_buffer_size);
    }

    sdl3d_asset_resolver_destroy(assets);
    SDL_free(base_dir);
    SDL_free(asset_name);
    return ok;
}

bool sdl3d_game_data_load_asset(sdl3d_asset_resolver *assets, const char *asset_path, sdl3d_game_session *session,
                                sdl3d_game_data_runtime **out_runtime, char *error_buffer, int error_buffer_size)
{
    if (out_runtime != NULL)
        *out_runtime = NULL;
    if (assets == NULL || asset_path == NULL || asset_path[0] == '\0' || session == NULL || out_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid game data load arguments");
        return false;
    }

    sdl3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!sdl3d_asset_resolver_read_file(assets, asset_path, &buffer, asset_error, (int)sizeof(asset_error)))
    {
        if (error_buffer != NULL && error_buffer_size > 0)
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to read game data asset %s: %s", asset_path,
                         asset_error);
        return false;
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    sdl3d_asset_buffer_free(&buffer);
    if (doc == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "yyjson error %u at byte %llu: %s", err.code,
                         (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
        return false;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root) || SDL_strcmp(json_string(root, "schema", ""), "sdl3d.game.v0") != 0)
    {
        yyjson_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "unsupported or missing game data schema");
        return false;
    }

    sdl3d_game_data_runtime *runtime = (sdl3d_game_data_runtime *)SDL_calloc(1, sizeof(*runtime));
    if (runtime == NULL)
    {
        yyjson_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to allocate game data runtime");
        return false;
    }
    runtime->doc = doc;
    runtime->session = session;
    runtime->assets = assets;
    runtime->base_dir = path_dirname(asset_path_without_scheme(asset_path));
    runtime->scene_state = sdl3d_properties_create();
    runtime->rng_state = 0xC0FFEEu;
    if (runtime->base_dir == NULL || runtime->scene_state == NULL)
    {
        sdl3d_game_data_destroy(runtime);
        set_error(error_buffer, error_buffer_size, "failed to allocate game data runtime state");
        return false;
    }
    if (!sdl3d_game_data_validate_document(root, asset_path, runtime->base_dir, assets, NULL, error_buffer,
                                           error_buffer_size))
    {
        sdl3d_game_data_destroy(runtime);
        return false;
    }

    yyjson_val *logic = obj_get(root, "logic");
    load_active_camera(runtime, root);
    bool ok = load_signals(runtime, root, error_buffer, error_buffer_size) &&
              load_entities(runtime, root, error_buffer, error_buffer_size) &&
              load_input(runtime, root, error_buffer, error_buffer_size) &&
              load_timers(runtime, logic, error_buffer, error_buffer_size) && load_sensors(runtime, logic) &&
              load_scripts(runtime, root, error_buffer, error_buffer_size) &&
              load_lua_adapters(runtime, root, error_buffer, error_buffer_size) &&
              load_bindings(runtime, logic, error_buffer, error_buffer_size) &&
              load_scenes(runtime, root, error_buffer, error_buffer_size);
    if (!ok)
    {
        sdl3d_game_data_destroy(runtime);
        return false;
    }

    runtime->assets = NULL;
    *out_runtime = runtime;
    return true;
}

bool sdl3d_game_data_load_file(const char *path, sdl3d_game_session *session, sdl3d_game_data_runtime **out_runtime,
                               char *error_buffer, int error_buffer_size)
{
    if (out_runtime != NULL)
        *out_runtime = NULL;
    if (path == NULL || path[0] == '\0' || session == NULL || out_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid game data load arguments");
        return false;
    }

    char *base_dir = path_dirname(path);
    char *asset_name = path_basename(path);
    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    if (base_dir == NULL || asset_name == NULL || assets == NULL)
    {
        SDL_free(base_dir);
        SDL_free(asset_name);
        sdl3d_asset_resolver_destroy(assets);
        set_error(error_buffer, error_buffer_size, "failed to create game data asset resolver");
        return false;
    }

    char asset_error[256];
    const bool mounted = sdl3d_asset_resolver_mount_directory(assets, base_dir, asset_error, (int)sizeof(asset_error));
    if (!mounted)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to mount game data directory: %s",
                         asset_error);
        SDL_free(base_dir);
        SDL_free(asset_name);
        sdl3d_asset_resolver_destroy(assets);
        return false;
    }

    const bool ok =
        sdl3d_game_data_load_asset(assets, asset_name, session, out_runtime, error_buffer, error_buffer_size);
    SDL_free(base_dir);
    SDL_free(asset_name);
    sdl3d_asset_resolver_destroy(assets);
    return ok;
}

void sdl3d_game_data_destroy(sdl3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;

    sdl3d_signal_bus *bus = runtime_bus(runtime);
    for (int i = 0; i < runtime->binding_count; ++i)
    {
        if (runtime->bindings[i].connection_id != 0)
            sdl3d_signal_disconnect(bus, runtime->bindings[i].connection_id);
    }
    for (int i = 0; i < runtime->adapter_count; ++i)
    {
        SDL_free(runtime->adapters[i].name);
        SDL_free(runtime->adapters[i].lua_script_id);
        SDL_free(runtime->adapters[i].lua_function);
        sdl3d_script_engine_unref(runtime->scripts, runtime->adapters[i].lua_function_ref);
    }
    for (int i = 0; i < runtime->script_count; ++i)
    {
        sdl3d_script_engine_unref(runtime->scripts, runtime->script_entries[i].module_ref);
        SDL_free(runtime->script_entries[i].dependencies);
    }
    for (int i = 0; i < runtime->scene_count; ++i)
    {
        SDL_free(runtime->scenes[i].entities);
        SDL_free(runtime->scenes[i].menus);
        yyjson_doc_free(runtime->scenes[i].doc);
    }
    for (int i = 0; i < runtime->ui_state_count; ++i)
        SDL_free(runtime->ui_states[i].name);

    sdl3d_script_engine_destroy(runtime->scripts);
    sdl3d_properties_destroy(runtime->scene_state);
    SDL_free(runtime->base_dir);
    SDL_free(runtime->scenes);
    SDL_free(runtime->script_entries);
    SDL_free(runtime->signals);
    SDL_free(runtime->timers);
    SDL_free(runtime->actions);
    SDL_free(runtime->adapters);
    SDL_free(runtime->bindings);
    SDL_free(runtime->sensors);
    SDL_free(runtime->ui_states);
    yyjson_doc_free(runtime->doc);
    SDL_free(runtime);
}

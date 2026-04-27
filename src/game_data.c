/**
 * @file game_data.c
 * @brief JSON-authored game data runtime implementation.
 */

#include "sdl3d/game_data.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include "lauxlib.h"
#include "lua.h"
#include "script_internal.h"
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
    char *base_dir;
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

static sdl3d_game_data_runtime *lua_runtime(lua_State *lua)
{
    return (sdl3d_game_data_runtime *)lua_touserdata(lua, lua_upvalueindex(1));
}

static sdl3d_registered_actor *lua_actor_arg(lua_State *lua, sdl3d_game_data_runtime *runtime, int index)
{
    const char *actor_name = luaL_checkstring(lua, index);
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

static int lua_log(lua_State *lua)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[lua] %s", luaL_checkstring(lua, 1));
    return 0;
}

static void register_lua_api(sdl3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scripts == NULL)
        return;

    lua_State *lua = sdl3d_script_engine_lua_state(runtime->scripts);
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
    SDL3D_LUA_BIND("random", lua_random);
    SDL3D_LUA_BIND("log", lua_log);
#undef SDL3D_LUA_BIND
    lua_setglobal(lua, "sdl3d");
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
    return yyjson_is_num(value) ? (float)yyjson_get_real(value) : fallback;
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

    return sdl3d_vec3_make((float)yyjson_get_real(x), (float)yyjson_get_real(y),
                           yyjson_is_num(z) ? (float)yyjson_get_real(z) : fallback.z);
}

static sdl3d_vec3 json_vec3(yyjson_val *object, const char *key, sdl3d_vec3 fallback)
{
    return json_vec3_value(obj_get(object, key), fallback);
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

static bool set_adapter_lua_function(sdl3d_game_data_runtime *runtime, const char *name, const char *function_name,
                                     sdl3d_script_ref function_ref)
{
    if (runtime == NULL || name == NULL || name[0] == '\0' || function_name == NULL || function_name[0] == '\0' ||
        function_ref == SDL3D_SCRIPT_REF_INVALID)
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

    char *copy = SDL_strdup(function_name);
    if (copy == NULL)
        return false;
    SDL_free(entry->lua_function);
    entry->lua_function = copy;
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

static bool call_lua_adapter(sdl3d_game_data_runtime *runtime, const adapter_entry *adapter,
                             sdl3d_registered_actor *target, const sdl3d_properties *payload)
{
    if (runtime == NULL || runtime->scripts == NULL || adapter == NULL ||
        adapter->lua_function_ref == SDL3D_SCRIPT_REF_INVALID)
        return false;

    lua_State *lua = sdl3d_script_engine_lua_state(runtime->scripts);
    if (lua == NULL || !sdl3d_script_engine_push_ref(runtime->scripts, adapter->lua_function_ref))
        return false;

    lua_pushstring(lua, target != NULL ? target->name : "");
    lua_push_payload(lua, payload);
    lua_pushstring(lua, adapter->name);

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

sdl3d_registered_actor *sdl3d_game_data_find_actor(const sdl3d_game_data_runtime *runtime, const char *name)
{
    return sdl3d_actor_registry_find(runtime_registry(runtime), name);
}

const char *sdl3d_game_data_active_camera(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->active_camera : NULL;
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

    char *resolved = path_join(runtime->base_dir, entry->path);
    if (resolved == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to resolve script path for %s", entry->id);
        }
        entry->loading = false;
        return false;
    }

    char script_error[512];
    const bool ok = sdl3d_script_engine_load_module_file(runtime->scripts, resolved, entry->module, &entry->module_ref,
                                                         script_error, (int)sizeof(script_error));
    SDL_free(resolved);
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

    register_lua_api(runtime);
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

        if (!set_adapter_lua_function(runtime, name, function_name, function_ref))
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
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(entity, "name", NULL));
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
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(entity, "name", NULL));
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

bool sdl3d_game_data_load_file(const char *path, sdl3d_game_session *session, sdl3d_game_data_runtime **out_runtime,
                               char *error_buffer, int error_buffer_size)
{
    if (out_runtime != NULL)
        *out_runtime = NULL;
    if (path == NULL || session == NULL || out_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid game data load arguments");
        return false;
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_file(path, YYJSON_READ_NOFLAG, NULL, &err);
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
    runtime->base_dir = path_dirname(path);
    runtime->rng_state = 0xC0FFEEu;
    if (runtime->base_dir == NULL)
    {
        sdl3d_game_data_destroy(runtime);
        set_error(error_buffer, error_buffer_size, "failed to resolve game data base directory");
        return false;
    }

    yyjson_val *logic = obj_get(root, "logic");
    bool ok = load_signals(runtime, root, error_buffer, error_buffer_size) &&
              load_entities(runtime, root, error_buffer, error_buffer_size) &&
              load_input(runtime, root, error_buffer, error_buffer_size) &&
              load_timers(runtime, logic, error_buffer, error_buffer_size) && load_sensors(runtime, logic) &&
              load_scripts(runtime, root, error_buffer, error_buffer_size) &&
              load_lua_adapters(runtime, root, error_buffer, error_buffer_size) &&
              load_bindings(runtime, logic, error_buffer, error_buffer_size);
    if (!ok)
    {
        sdl3d_game_data_destroy(runtime);
        return false;
    }

    load_active_camera(runtime, root);
    *out_runtime = runtime;
    return true;
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
        SDL_free(runtime->adapters[i].lua_function);
        sdl3d_script_engine_unref(runtime->scripts, runtime->adapters[i].lua_function_ref);
    }
    for (int i = 0; i < runtime->script_count; ++i)
    {
        sdl3d_script_engine_unref(runtime->scripts, runtime->script_entries[i].module_ref);
        SDL_free(runtime->script_entries[i].dependencies);
    }

    sdl3d_script_engine_destroy(runtime->scripts);
    SDL_free(runtime->base_dir);
    SDL_free(runtime->script_entries);
    SDL_free(runtime->signals);
    SDL_free(runtime->timers);
    SDL_free(runtime->actions);
    SDL_free(runtime->adapters);
    SDL_free(runtime->bindings);
    SDL_free(runtime->sensors);
    yyjson_doc_free(runtime->doc);
    SDL_free(runtime);
}

/**
 * @file game_data.c
 * @brief JSON-authored game data runtime implementation.
 */

#include "sdl3d/game_data.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include "game_data_standard_options.h"
#include "game_data_validation.h"
#include "lauxlib.h"
#include "lua.h"
#include "network_replication_schema.h"
#include "script_internal.h"
#include "sdl3d/asset.h"
#include "sdl3d/input.h"
#include "sdl3d/math.h"
#include "sdl3d/network.h"
#include "sdl3d/script.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/timer_pool.h"
#include "yyjson.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#define SDL3D_GAME_DATA_SIGNAL_BASE 20000
#define SDL3D_GAME_DATA_NETWORK_SNAPSHOT_MAGIC 0x53335253u /* "S3RS" */
#define SDL3D_GAME_DATA_NETWORK_SNAPSHOT_VERSION 1u
#define SDL3D_GAME_DATA_NETWORK_INPUT_MAGIC 0x49335253u /* "S3RI" */
#define SDL3D_GAME_DATA_NETWORK_INPUT_VERSION 1u
#define SDL3D_GAME_DATA_NETWORK_CONTROL_MAGIC 0x43335253u /* "S3RC" */
#define SDL3D_GAME_DATA_NETWORK_CONTROL_VERSION 1u
#define SDL3D_GAME_DATA_MENU_TEXT_MAX_BYTES 255

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

typedef struct sensor_contact_pair_state
{
    const char *actor_name;
    const char *other_actor_name;
    bool active;
    bool seen;
} sensor_contact_pair_state;

typedef struct sensor_entry
{
    game_data_sensor_type type;
    const char *entity;
    const char *other;
    const char *entity_tag;
    const char *other_tag;
    const char *action;
    const char *axis;
    const char *side;
    float min_value;
    float max_value;
    float threshold;
    int signal_id;
    bool was_active;
    sensor_contact_pair_state *contact_pairs;
    int contact_pair_count;
    int contact_pair_capacity;
} sensor_entry;

typedef struct sensor_actor_list
{
    sdl3d_registered_actor **items;
    int count;
    int capacity;
} sensor_actor_list;

typedef struct input_binding_spec
{
    const char *action;
    int action_id;
    sdl3d_input_source source;
    int gamepad_index;
    int required_modifiers;
    int excluded_modifiers;
    union {
        SDL_Scancode scancode;
        Uint8 mouse_button;
        sdl3d_mouse_axis mouse_axis;
        SDL_GamepadButton gamepad_button;
        SDL_GamepadAxis gamepad_axis;
    };
    float scale;
} input_binding_spec;

typedef struct menu_input_binding_capture
{
    bool active;
    const char *menu;
    int item_index;
} menu_input_binding_capture;

typedef struct menu_text_entry_capture
{
    bool active;
    const char *menu;
    int item_index;
    char *original;
} menu_text_entry_capture;

typedef enum menu_binding_device
{
    MENU_BINDING_DEVICE_KEYBOARD = 0,
    MENU_BINDING_DEVICE_MOUSE_BUTTON = 1,
    MENU_BINDING_DEVICE_GAMEPAD_BUTTON = 2,
} menu_binding_device;

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
    bool animated;
} ui_state_entry;

typedef enum game_data_tween_target
{
    GAME_DATA_TWEEN_UI,
    GAME_DATA_TWEEN_PROPERTY,
} game_data_tween_target;

typedef enum game_data_tween_value_type
{
    GAME_DATA_TWEEN_FLOAT,
    GAME_DATA_TWEEN_VEC3,
    GAME_DATA_TWEEN_COLOR,
} game_data_tween_value_type;

typedef enum game_data_tween_easing
{
    GAME_DATA_TWEEN_LINEAR,
    GAME_DATA_TWEEN_IN_QUAD,
    GAME_DATA_TWEEN_OUT_QUAD,
    GAME_DATA_TWEEN_IN_OUT_QUAD,
} game_data_tween_easing;

typedef enum game_data_tween_repeat
{
    GAME_DATA_TWEEN_REPEAT_NONE,
    GAME_DATA_TWEEN_REPEAT_LOOP,
    GAME_DATA_TWEEN_REPEAT_PING_PONG,
} game_data_tween_repeat;

typedef struct game_data_tween_value
{
    game_data_tween_value_type type;
    union {
        float as_float;
        sdl3d_vec3 as_vec3;
        sdl3d_color as_color;
    };
} game_data_tween_value;

typedef struct game_data_animation
{
    game_data_tween_target target_type;
    const char *target;
    const char *property;
    const char *key;
    const char *scene;
    float duration;
    float elapsed;
    game_data_tween_easing easing;
    game_data_tween_repeat repeat;
    game_data_tween_value from;
    game_data_tween_value to;
    sdl3d_value_type property_type;
    int done_signal_id;
} game_data_animation;

typedef struct materialized_audio_file
{
    char *asset_path;
    char *file_path;
} materialized_audio_file;

typedef struct property_snapshot
{
    char *name;
    char *target;
    sdl3d_properties *properties;
} property_snapshot;

typedef struct runtime_collection
{
    char *name;
    sdl3d_properties **rows;
    int row_count;
    int row_capacity;
} runtime_collection;

typedef enum actor_pool_exhaustion_policy
{
    ACTOR_POOL_EXHAUST_FAIL,
    ACTOR_POOL_EXHAUST_REUSE_OLDEST,
} actor_pool_exhaustion_policy;

typedef enum actor_pool_scene_exit_policy
{
    ACTOR_POOL_SCENE_EXIT_RESET,
    ACTOR_POOL_SCENE_EXIT_DESPAWN,
    ACTOR_POOL_SCENE_EXIT_PRESERVE,
} actor_pool_scene_exit_policy;

typedef enum actor_lifecycle_state
{
    ACTOR_LIFECYCLE_INACTIVE,
    ACTOR_LIFECYCLE_SPAWNING,
    ACTOR_LIFECYCLE_ACTIVE,
    ACTOR_LIFECYCLE_DESPAWNING,
} actor_lifecycle_state;

typedef struct actor_pool_runtime
{
    char *name;
    const char *archetype;
    const char *scene;
    const char **scenes;
    int scene_count;
    yyjson_val *archetype_json;
    char **actor_names;
    Uint64 *spawn_generations;
    actor_lifecycle_state *lifecycle_states;
    Uint64 spawn_generation_counter;
    int capacity;
    bool initial_active;
    actor_pool_exhaustion_policy exhaustion;
    actor_pool_scene_exit_policy scene_exit_policy;
} actor_pool_runtime;

typedef struct runtime_direct_connect_session
{
    char *name;
    sdl3d_network_session *session;
} runtime_direct_connect_session;

typedef struct runtime_host_session
{
    char *name;
    sdl3d_network_session *session;
} runtime_host_session;

typedef struct runtime_discovery_session
{
    char *name;
    sdl3d_network_discovery_session *session;
} runtime_discovery_session;

typedef struct network_diagnostic_runtime_state
{
    char *name;
    Uint64 last_log_ms;
    bool logged;
} network_diagnostic_runtime_state;

typedef struct scene_activity_state
{
    const char *scene;
    float idle_elapsed;
    float *periodic_elapsed;
    int periodic_count;
    int periodic_capacity;
    bool idle;
    bool entered;
} scene_activity_state;

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
    input_binding_spec *input_bindings;
    int input_binding_count;
    int input_binding_capacity;
    menu_input_binding_capture input_capture;
    menu_text_entry_capture text_capture;
    sdl3d_script_engine *scripts;
    script_entry *script_entries;
    int script_count;
    sdl3d_asset_resolver *assets;
    bool owns_assets;
    char *base_dir;
    sdl3d_storage_config storage_config;
    scene_entry *scenes;
    int scene_count;
    int active_scene_index;
    sdl3d_properties *scene_state;
    ui_state_entry *ui_states;
    int ui_state_count;
    int ui_state_capacity;
    game_data_animation *animations;
    int animation_count;
    int animation_capacity;
    const char *animation_scene;
    materialized_audio_file *audio_files;
    int audio_file_count;
    int audio_file_capacity;
    property_snapshot *property_snapshots;
    int property_snapshot_count;
    int property_snapshot_capacity;
    runtime_collection *collections;
    int collection_count;
    int collection_capacity;
    actor_pool_runtime *actor_pools;
    int actor_pool_count;
    runtime_direct_connect_session *direct_connect_sessions;
    int direct_connect_session_count;
    int direct_connect_session_capacity;
    runtime_host_session *host_sessions;
    int host_session_count;
    int host_session_capacity;
    runtime_discovery_session *discovery_sessions;
    int discovery_session_count;
    int discovery_session_capacity;
    network_diagnostic_runtime_state *network_diagnostics;
    int network_diagnostic_count;
    int network_diagnostic_capacity;
    int actor_lifecycle_defer_depth;
    bool actor_lifecycle_flush_pending;
    sdl3d_storage *storage;
    bool has_network_schema;
    Uint8 network_schema_hash[SDL3D_REPLICATION_SCHEMA_HASH_SIZE];
    const char *active_camera;
    scene_activity_state activity;
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

static void set_errorf(char *buffer, int buffer_size, const char *format, ...)
{
    if (buffer == NULL || buffer_size <= 0)
        return;

    va_list args;
    va_start(args, format);
    SDL_vsnprintf(buffer, (size_t)buffer_size, format != NULL ? format : "unknown game data error", args);
    va_end(args);
}

static void clear_menu_text_entry_capture(sdl3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;

    runtime->text_capture.active = false;
    runtime->text_capture.menu = NULL;
    runtime->text_capture.item_index = -1;
    SDL_free(runtime->text_capture.original);
    runtime->text_capture.original = NULL;
}

static bool append_format(char *buffer, size_t buffer_size, size_t *offset, const char *format, ...)
{
    if (buffer == NULL || buffer_size == 0U || offset == NULL || *offset >= buffer_size || format == NULL)
        return false;

    va_list args;
    va_start(args, format);
    const int written = SDL_vsnprintf(buffer + *offset, buffer_size - *offset, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= buffer_size - *offset)
    {
        buffer[buffer_size - 1U] = '\0';
        return false;
    }

    *offset += (size_t)written;
    return true;
}

static bool set_action_keyboard_binding(sdl3d_game_data_runtime *runtime, const char *action, SDL_Scancode scancode);
static bool set_action_mouse_button_binding(sdl3d_game_data_runtime *runtime, const char *action, Uint8 button);
static bool set_action_gamepad_button_binding(sdl3d_game_data_runtime *runtime, const char *action,
                                              SDL_GamepadButton button);
static bool eval_data_condition(const sdl3d_game_data_runtime *runtime, yyjson_val *condition,
                                const sdl3d_game_data_ui_metrics *metrics);
static int menu_runtime_item_count(const sdl3d_game_data_runtime *runtime, const scene_menu_state *menu);
static void update_dynamic_list_selection_state(sdl3d_game_data_runtime *runtime, scene_menu_state *menu);

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
static yyjson_val *obj_get(yyjson_val *object, const char *key);
static const char *json_string(yyjson_val *object, const char *key, const char *fallback);
static yyjson_val *runtime_root(const sdl3d_game_data_runtime *runtime);
static actor_pool_runtime *find_actor_pool(sdl3d_game_data_runtime *runtime, const char *name);
static actor_pool_runtime *find_actor_pool_for_actor(sdl3d_game_data_runtime *runtime, const char *actor_name,
                                                     int *out_index);
static const actor_pool_runtime *find_actor_pool_for_actor_const(const sdl3d_game_data_runtime *runtime,
                                                                 const char *actor_name, int *out_index);
static bool actor_pool_in_scene(const actor_pool_runtime *pool, const char *scene_name);
static sdl3d_registered_actor *actor_pool_allocate(sdl3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                                   int *out_index);
static void actor_pool_set_lifecycle_state(actor_pool_runtime *pool, sdl3d_registered_actor *actor, int index,
                                           actor_lifecycle_state state);
static bool actor_pool_actor_is_active(const actor_pool_runtime *pool, const sdl3d_registered_actor *actor, int index);
static bool actor_pool_actor_is_available(const actor_pool_runtime *pool, const sdl3d_registered_actor *actor,
                                          int index);
static bool initialize_pooled_actor(actor_pool_runtime *pool, sdl3d_registered_actor *actor, int index, bool active);
static bool actor_pool_request_despawn(sdl3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                       sdl3d_registered_actor *actor, int index);
static bool apply_actor_pool_scene_exit_policies(sdl3d_game_data_runtime *runtime, const char *from_scene,
                                                 const char *to_scene);
static void actor_lifecycle_defer_begin(sdl3d_game_data_runtime *runtime);
static void actor_lifecycle_defer_end(sdl3d_game_data_runtime *runtime);
static bool entity_json_has_tags(yyjson_val *entity, const char *const *tags, int tag_count);

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

static bool ensure_runtime_storage(sdl3d_game_data_runtime *runtime, char *error_buffer, int error_buffer_size);

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

static int lua_get_string(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const char *fallback = luaL_optstring(lua, 3, "");
    lua_pushstring(lua, actor != NULL ? sdl3d_properties_get_string(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_string(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        sdl3d_properties_set_string(actor->props, luaL_checkstring(lua, 2), luaL_checkstring(lua, 3));
    return 0;
}

static int lua_get_vec3(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    if (actor == NULL)
    {
        lua_pushnil(lua);
        return 1;
    }
    const sdl3d_vec3 value = sdl3d_properties_get_vec3(actor->props, key, sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
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

static int lua_actor_active(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    lua_pushboolean(lua, actor != NULL && actor->active);
    return 1;
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

static int lua_collect_tags(lua_State *lua, int start_index, const char **tags, int max_tags)
{
    const int arg_count = lua_gettop(lua);
    int tag_count = 0;
    if (lua_istable(lua, start_index))
    {
        const lua_Integer count = luaL_len(lua, start_index);
        for (lua_Integer i = 1; i <= count && tag_count < max_tags; ++i)
        {
            lua_geti(lua, start_index, i);
            const char *tag = lua_tostring(lua, -1);
            if (tag != NULL && tag[0] != '\0')
                tags[tag_count++] = tag;
            lua_pop(lua, 1);
        }
        return tag_count;
    }

    for (int i = start_index; i <= arg_count && tag_count < max_tags; ++i)
    {
        const char *tag = lua_tostring(lua, i);
        if (tag != NULL && tag[0] != '\0')
            tags[tag_count++] = tag;
    }
    return tag_count;
}

static bool lua_read_vec3_value(lua_State *lua, int index, sdl3d_vec3 fallback, sdl3d_vec3 *out_value)
{
    if (out_value != NULL)
        *out_value = fallback;
    if (!lua_istable(lua, index))
        return false;

    index = lua_absindex(lua, index);
    lua_getfield(lua, index, "x");
    lua_getfield(lua, index, "y");
    lua_getfield(lua, index, "z");
    bool has_xy = lua_isnumber(lua, -3) && lua_isnumber(lua, -2);
    sdl3d_vec3 value = sdl3d_vec3_make(has_xy ? (float)lua_tonumber(lua, -3) : fallback.x,
                                       has_xy ? (float)lua_tonumber(lua, -2) : fallback.y,
                                       lua_isnumber(lua, -1) ? (float)lua_tonumber(lua, -1) : fallback.z);
    lua_pop(lua, 3);

    if (!has_xy)
    {
        lua_geti(lua, index, 1);
        lua_geti(lua, index, 2);
        lua_geti(lua, index, 3);
        has_xy = lua_isnumber(lua, -3) && lua_isnumber(lua, -2);
        value = sdl3d_vec3_make(has_xy ? (float)lua_tonumber(lua, -3) : fallback.x,
                                has_xy ? (float)lua_tonumber(lua, -2) : fallback.y,
                                lua_isnumber(lua, -1) ? (float)lua_tonumber(lua, -1) : fallback.z);
        lua_pop(lua, 3);
    }

    if (!has_xy)
        return false;
    if (out_value != NULL)
        *out_value = value;
    return true;
}

static bool lua_read_vec3_field(lua_State *lua, int table_index, const char *field, sdl3d_vec3 fallback,
                                sdl3d_vec3 *out_value)
{
    if (!lua_istable(lua, table_index) || field == NULL)
        return false;
    table_index = lua_absindex(lua, table_index);
    lua_getfield(lua, table_index, field);
    const bool ok = lua_read_vec3_value(lua, -1, fallback, out_value);
    lua_pop(lua, 1);
    return ok;
}

static void lua_set_actor_property_from_value(lua_State *lua, sdl3d_registered_actor *actor, const char *key, int index)
{
    if (actor == NULL || key == NULL)
        return;
    index = lua_absindex(lua, index);
    if (lua_isboolean(lua, index))
        sdl3d_properties_set_bool(actor->props, key, lua_toboolean(lua, index));
    else if (lua_isinteger(lua, index))
        sdl3d_properties_set_int(actor->props, key, (int)lua_tointeger(lua, index));
    else if (lua_isnumber(lua, index))
        sdl3d_properties_set_float(actor->props, key, (float)lua_tonumber(lua, index));
    else if (lua_isstring(lua, index))
        sdl3d_properties_set_string(actor->props, key, lua_tostring(lua, index));
    else if (lua_istable(lua, index))
    {
        sdl3d_vec3 value;
        if (lua_read_vec3_value(lua, index, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), &value))
            sdl3d_properties_set_vec3(actor->props, key, value);
    }
}

static sdl3d_registered_actor *lua_actor_from_value(lua_State *lua, sdl3d_game_data_runtime *runtime, int index)
{
    if (lua_isnoneornil(lua, index))
        return NULL;
    if (lua_istable(lua, index))
    {
        lua_getfield(lua, index, "_name");
        const char *name = lua_tostring(lua, -1);
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, name);
        lua_pop(lua, 1);
        return actor;
    }
    if (lua_isstring(lua, index))
        return sdl3d_game_data_find_actor(runtime, lua_tostring(lua, index));
    return NULL;
}

static int lua_spawn_actor(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    actor_pool_runtime *pool = find_actor_pool(runtime, luaL_checkstring(lua, 1));
    int actor_index = -1;
    sdl3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
    if (pool == NULL || actor == NULL || actor_index < 0)
    {
        lua_pushnil(lua);
        lua_pushstring(lua, "actor pool exhausted or missing");
        return 2;
    }

    actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
    if (!initialize_pooled_actor(pool, actor, actor_index, true))
    {
        lua_pushnil(lua);
        lua_pushstring(lua, "failed to initialize pooled actor");
        return 2;
    }

    if (pool->spawn_generations != NULL)
    {
        pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
        sdl3d_properties_set_int(actor->props, "pool_spawn_generation",
                                 (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
    }

    sdl3d_vec3 position = actor->position;
    if (lua_istable(lua, 2))
    {
        lua_read_vec3_field(lua, 2, "position", position, &position);
        lua_getfield(lua, 2, "from");
        sdl3d_registered_actor *from_actor = lua_actor_from_value(lua, runtime, -1);
        lua_pop(lua, 1);
        if (from_actor != NULL)
            position = from_actor->position;

        sdl3d_vec3 offset = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
        if (lua_read_vec3_field(lua, 2, "offset", offset, &offset))
        {
            position.x += offset.x;
            position.y += offset.y;
            position.z += offset.z;
        }

        lua_getfield(lua, 2, "properties");
        if (lua_istable(lua, -1))
        {
            lua_pushnil(lua);
            while (lua_next(lua, -2) != 0)
            {
                const char *key = lua_tostring(lua, -2);
                if (key != NULL && key[0] != '\0')
                    lua_set_actor_property_from_value(lua, actor, key, -1);
                lua_pop(lua, 1);
            }
        }
        lua_pop(lua, 1);
    }

    actor_set_position(actor, position);
    lua_push_actor_wrapper(lua, actor);
    lua_pushinteger(lua, actor->id);
    lua_pushinteger(lua, actor_index);
    return 3;
}

static int lua_despawn_actor(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    sdl3d_registered_actor *actor = lua_actor_from_value(lua, runtime, 1);
    if (actor == NULL)
    {
        lua_pushboolean(lua, false);
        return 1;
    }

    int actor_index = -1;
    actor_pool_runtime *pool = find_actor_pool_for_actor(runtime, actor->name, &actor_index);
    const bool ok = pool != NULL && actor_index >= 0 ? actor_pool_request_despawn(runtime, pool, actor, actor_index)
                                                     : (actor->active = false, true);
    lua_pushboolean(lua, ok);
    return 1;
}

static int lua_despawn_actors_by_tag(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *tag = luaL_checkstring(lua, 1);
    int despawned = 0;
    if (runtime != NULL && tag != NULL && tag[0] != '\0')
    {
        for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
        {
            actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
            if (!entity_json_has_tags(pool->archetype_json, &tag, 1))
                continue;
            for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
            {
                sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
                if (actor_pool_actor_is_active(pool, actor, actor_index) &&
                    actor_pool_request_despawn(runtime, pool, actor, actor_index))
                    ++despawned;
            }
        }
    }
    lua_pushinteger(lua, despawned);
    return 1;
}

static int lua_pool_capacity(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? pool->capacity : 0);
    return 1;
}

static int lua_pool_active_count(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    actor_pool_runtime *pool = find_actor_pool(runtime, luaL_checkstring(lua, 1));
    int count = 0;
    for (int i = 0; pool != NULL && i < pool->capacity; ++i)
    {
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[i]);
        if (actor_pool_actor_is_active(pool, actor, i))
            ++count;
    }
    lua_pushinteger(lua, count);
    return 1;
}

static int lua_pool_available_count(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    actor_pool_runtime *pool = find_actor_pool(runtime, luaL_checkstring(lua, 1));
    int count = 0;
    for (int i = 0; pool != NULL && i < pool->capacity; ++i)
    {
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[i]);
        if (actor_pool_actor_is_available(pool, actor, i))
            ++count;
    }
    lua_pushinteger(lua, count);
    return 1;
}

static int lua_active_actors_with_tags(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *tags[16];
    const int tag_count = lua_collect_tags(lua, 1, tags, (int)SDL_arraysize(tags));
    lua_newtable(lua);
    if (runtime == NULL || tag_count <= 0)
        return 1;

    int out_index = 1;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        if (!entity_json_has_tags(entity, tags, tag_count))
            continue;
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(entity, "name", NULL));
        if (actor != NULL && actor->active)
        {
            lua_push_actor_wrapper(lua, actor);
            lua_seti(lua, -2, out_index++);
        }
    }

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!entity_json_has_tags(pool->archetype_json, tags, tag_count))
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index))
            {
                lua_push_actor_wrapper(lua, actor);
                lua_seti(lua, -2, out_index++);
            }
        }
    }
    return 1;
}

static int lua_log(lua_State *lua)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[lua] %s", luaL_checkstring(lua, 1));
    return 0;
}

static int lua_storage_read(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    if (!ensure_runtime_storage(runtime, error, (int)sizeof(error)))
    {
        lua_pushnil(lua);
        lua_pushstring(lua, error);
        return 2;
    }

    sdl3d_storage_buffer buffer;
    SDL_zero(buffer);
    if (!sdl3d_storage_read_file(runtime->storage, path, &buffer, error, (int)sizeof(error)))
    {
        lua_pushnil(lua);
        lua_pushstring(lua, error);
        return 2;
    }

    lua_pushlstring(lua, (const char *)buffer.data, buffer.size);
    sdl3d_storage_buffer_free(&buffer);
    return 1;
}

static int lua_storage_write(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);
    size_t size = 0u;
    const char *data = luaL_checklstring(lua, 2, &size);

    char error[256];
    const bool ok = ensure_runtime_storage(runtime, error, (int)sizeof(error)) &&
                    sdl3d_storage_write_file(runtime->storage, path, data, size, error, (int)sizeof(error));
    lua_pushboolean(lua, ok);
    if (!ok)
    {
        lua_pushstring(lua, error);
        return 2;
    }
    return 1;
}

static int lua_storage_exists(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    const bool ok =
        ensure_runtime_storage(runtime, error, (int)sizeof(error)) && sdl3d_storage_exists(runtime->storage, path);
    lua_pushboolean(lua, ok);
    return 1;
}

static int lua_storage_mkdir(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    const bool ok = ensure_runtime_storage(runtime, error, (int)sizeof(error)) &&
                    sdl3d_storage_create_directory(runtime->storage, path, error, (int)sizeof(error));
    lua_pushboolean(lua, ok);
    if (!ok)
    {
        lua_pushstring(lua, error);
        return 2;
    }
    return 1;
}

static int lua_storage_delete(lua_State *lua)
{
    sdl3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    const bool ok = ensure_runtime_storage(runtime, error, (int)sizeof(error)) &&
                    sdl3d_storage_delete(runtime->storage, path, error, (int)sizeof(error));
    lua_pushboolean(lua, ok);
    if (!ok)
    {
        lua_pushstring(lua, error);
        return 2;
    }
    return 1;
}

static void lua_push_json_value(lua_State *lua, yyjson_val *value, int depth)
{
    if (value == NULL || depth > 64)
    {
        lua_pushnil(lua);
        return;
    }
    if (yyjson_is_null(value))
    {
        lua_pushnil(lua);
    }
    else if (yyjson_is_bool(value))
    {
        lua_pushboolean(lua, yyjson_get_bool(value));
    }
    else if (yyjson_is_int(value))
    {
        lua_pushinteger(lua, (lua_Integer)yyjson_get_sint(value));
    }
    else if (yyjson_is_num(value))
    {
        lua_pushnumber(lua, (lua_Number)yyjson_get_real(value));
    }
    else if (yyjson_is_str(value))
    {
        lua_pushlstring(lua, yyjson_get_str(value), yyjson_get_len(value));
    }
    else if (yyjson_is_arr(value))
    {
        lua_newtable(lua);
        size_t idx, max;
        yyjson_val *entry;
        yyjson_arr_foreach(value, idx, max, entry)
        {
            lua_push_json_value(lua, entry, depth + 1);
            lua_seti(lua, -2, (lua_Integer)idx + 1);
        }
    }
    else if (yyjson_is_obj(value))
    {
        lua_newtable(lua);
        size_t idx, max;
        yyjson_val *key;
        yyjson_val *val;
        yyjson_obj_foreach(value, idx, max, key, val)
        {
            lua_push_json_value(lua, val, depth + 1);
            lua_setfield(lua, -2, yyjson_get_str(key));
        }
    }
    else
    {
        lua_pushnil(lua);
    }
}

static bool lua_table_is_json_array(lua_State *lua, int index, lua_Integer *out_count)
{
    lua_Integer max_index = 0;
    lua_Integer count = 0;
    bool array = true;

    lua_pushnil(lua);
    while (lua_next(lua, index) != 0)
    {
        if (lua_isinteger(lua, -2))
        {
            const lua_Integer key = lua_tointeger(lua, -2);
            if (key <= 0)
                array = false;
            else
            {
                ++count;
                if (key > max_index)
                    max_index = key;
            }
        }
        else
        {
            array = false;
        }
        lua_pop(lua, 1);
    }

    if (out_count != NULL)
        *out_count = array && max_index == count ? count : 0;
    return array && max_index == count;
}

static yyjson_mut_val *lua_value_to_json(lua_State *lua, yyjson_mut_doc *doc, int index, int depth, char *error,
                                         int error_size)
{
    if (depth > 64)
    {
        set_error(error, error_size, "JSON value is too deeply nested");
        return NULL;
    }

    index = lua_absindex(lua, index);
    if (lua_isnoneornil(lua, index))
        return yyjson_mut_null(doc);
    if (lua_isboolean(lua, index))
        return yyjson_mut_bool(doc, lua_toboolean(lua, index));
    if (lua_isinteger(lua, index))
        return yyjson_mut_sint(doc, (int64_t)lua_tointeger(lua, index));
    if (lua_isnumber(lua, index))
        return yyjson_mut_real(doc, (double)lua_tonumber(lua, index));
    if (lua_isstring(lua, index))
    {
        size_t len = 0u;
        const char *text = lua_tolstring(lua, index, &len);
        return yyjson_mut_strncpy(doc, text, len);
    }
    if (!lua_istable(lua, index))
    {
        set_error(error, error_size, "JSON encode supports nil, bool, number, string, and table values");
        return NULL;
    }

    lua_Integer count = 0;
    if (lua_table_is_json_array(lua, index, &count))
    {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        if (arr == NULL)
            return NULL;
        for (lua_Integer i = 1; i <= count; ++i)
        {
            lua_geti(lua, index, i);
            yyjson_mut_val *item = lua_value_to_json(lua, doc, -1, depth + 1, error, error_size);
            lua_pop(lua, 1);
            if (item == NULL || !yyjson_mut_arr_add_val(arr, item))
                return NULL;
        }
        return arr;
    }

    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL)
        return NULL;

    lua_pushnil(lua);
    while (lua_next(lua, index) != 0)
    {
        if (!lua_isstring(lua, -2))
        {
            lua_pop(lua, 2);
            set_error(error, error_size, "JSON object keys must be strings");
            return NULL;
        }
        size_t key_len = 0u;
        const char *key_text = lua_tolstring(lua, -2, &key_len);
        yyjson_mut_val *key = yyjson_mut_strncpy(doc, key_text, key_len);
        yyjson_mut_val *val = lua_value_to_json(lua, doc, -1, depth + 1, error, error_size);
        lua_pop(lua, 1);
        if (key == NULL || val == NULL || !yyjson_mut_obj_add(obj, key, val))
            return NULL;
    }
    return obj;
}

static int lua_json_decode(lua_State *lua)
{
    size_t len = 0u;
    const char *text = luaL_checklstring(lua, 1, &len);
    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)(void *)(size_t)(const void *)text, len, YYJSON_READ_NOFLAG, NULL, &err);
    if (doc == NULL)
    {
        lua_pushnil(lua);
        lua_pushfstring(lua, "yyjson error %d at byte %d: %s", (int)err.code, (int)err.pos,
                        err.msg != NULL ? err.msg : "");
        return 2;
    }

    lua_push_json_value(lua, yyjson_doc_get_root(doc), 0);
    yyjson_doc_free(doc);
    return 1;
}

static int lua_json_encode(lua_State *lua)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL)
    {
        lua_pushnil(lua);
        lua_pushstring(lua, "failed to allocate JSON document");
        return 2;
    }

    char error[256];
    error[0] = '\0';
    yyjson_mut_val *root = lua_value_to_json(lua, doc, 1, 0, error, (int)sizeof(error));
    if (root == NULL)
    {
        yyjson_mut_doc_free(doc);
        lua_pushnil(lua);
        lua_pushstring(lua, error[0] != '\0' ? error : "failed to encode JSON value");
        return 2;
    }

    yyjson_mut_doc_set_root(doc, root);
    size_t len = 0u;
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &len);
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        lua_pushnil(lua);
        lua_pushstring(lua, "failed to write JSON");
        return 2;
    }

    lua_pushlstring(lua, json, len);
    free(json);
    return 1;
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
        "function Actor:get_string(key, fallback) return sdl3d.get_string(self, key, fallback or '') end\n"
        "function Actor:set_string(key, value) sdl3d.set_string(self, key, value or '') end\n"
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
        "    if key == 'active' then return sdl3d.actor_active(self) end\n"
        "    if key == 'position' then return Actor.get_position(self) end\n"
        "    if key == 'velocity' then return Actor.get_vec3(self, 'velocity', Vec3(0, 0, 0)) end\n"
        "    return Actor[key]\n"
        "end\n"
        "Actor.__newindex = function(self, key, value)\n"
        "    if key == 'position' then Actor.set_position(self, value); return end\n"
        "    if key == 'velocity' then Actor.set_vec3(self, 'velocity', value); return end\n"
        "    rawset(self, key, value)\n"
        "end\n",
        "function sdl3d.actor(name)\n"
        "    if name == nil or name == '' then return nil end\n"
        "    return setmetatable({ _name = name }, Actor)\n"
        "end\n"
        "function Actor:is_active() return sdl3d.actor_active(self) end\n"
        "function Actor:despawn() return sdl3d.despawn(self) end\n"
        "function sdl3d._context(adapter, dt)\n"
        "    return {\n"
        "        adapter = adapter,\n"
        "        name = adapter,\n"
        "        dt = dt or 0,\n"
        "        actor = function(self_or_name, maybe_name) return sdl3d.actor(maybe_name or self_or_name) end,\n"
        "        spawn = function(self_or_pool, maybe_pool, maybe_options)\n"
        "            if type(self_or_pool) == 'table' and self_or_pool.adapter ~= nil then\n"
        "                return sdl3d.spawn(maybe_pool, maybe_options)\n"
        "            end\n"
        "            return sdl3d.spawn(self_or_pool, maybe_pool)\n"
        "        end,\n"
        "        despawn = function(self_or_actor, maybe_actor)\n"
        "            if type(self_or_actor) == 'table' and self_or_actor.adapter ~= nil then\n"
        "                return sdl3d.despawn(maybe_actor)\n"
        "            end\n"
        "            return sdl3d.despawn(self_or_actor)\n"
        "        end,\n"
        "        despawn_by_tag = function(self_or_tag, maybe_tag)\n"
        "            if type(self_or_tag) == 'table' and self_or_tag.adapter ~= nil then\n"
        "                return sdl3d.despawn_by_tag(maybe_tag)\n"
        "            end\n"
        "            return sdl3d.despawn_by_tag(self_or_tag)\n"
        "        end,\n"
        "        actor_with_tags = function(self_or_tag, maybe_tag, ...)\n"
        "            if type(self_or_tag) == 'table' and self_or_tag.adapter ~= nil then\n"
        "                return sdl3d.actor_with_tags(maybe_tag, ...)\n"
        "            end\n"
        "            if type(self_or_tag) == 'table' then return sdl3d.actor_with_tags(self_or_tag) end\n"
        "            return sdl3d.actor_with_tags(self_or_tag, maybe_tag, ...)\n"
        "        end,\n",
        "        active_actors_with_tags = function(self_or_tag, maybe_tag, ...)\n"
        "            if type(self_or_tag) == 'table' and self_or_tag.adapter ~= nil then\n"
        "                return sdl3d.active_actors_with_tags(maybe_tag, ...)\n"
        "            end\n"
        "            if type(self_or_tag) == 'table' then return sdl3d.active_actors_with_tags(self_or_tag) end\n"
        "            return sdl3d.active_actors_with_tags(self_or_tag, maybe_tag, ...)\n"
        "        end,\n"
        "        pool_capacity = function(self_or_pool, maybe_pool)\n"
        "            return sdl3d.pool_capacity(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_active_count = function(self_or_pool, maybe_pool)\n"
        "            return sdl3d.pool_active_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_available_count = function(self_or_pool, maybe_pool)\n"
        "            return sdl3d.pool_available_count(maybe_pool or self_or_pool)\n"
        "        end,\n",
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
        "        storage = sdl3d.storage,\n"
        "    }\n"
        "end\n"
        "sdl3d.storage = {\n"
        "    read = sdl3d.storage_read,\n"
        "    write = sdl3d.storage_write,\n"
        "    exists = sdl3d.storage_exists,\n"
        "    mkdir = sdl3d.storage_mkdir,\n"
        "    delete = sdl3d.storage_delete,\n"
        "}\n"
        "sdl3d.json = {\n"
        "    decode = sdl3d.json_decode,\n"
        "    encode = sdl3d.json_encode,\n"
        "}\n"
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
    SDL3D_LUA_BIND("get_string", lua_get_string);
    SDL3D_LUA_BIND("set_string", lua_set_string);
    SDL3D_LUA_BIND("get_vec3", lua_get_vec3);
    SDL3D_LUA_BIND("set_vec3", lua_set_vec3);
    SDL3D_LUA_BIND("actor_active", lua_actor_active);
    SDL3D_LUA_BIND("dt", lua_get_dt);
    SDL3D_LUA_BIND("state_get", lua_state_get);
    SDL3D_LUA_BIND("state_set", lua_state_set);
    SDL3D_LUA_BIND("random", lua_random);
    SDL3D_LUA_BIND("actor_with_tags", lua_actor_with_tags);
    SDL3D_LUA_BIND("active_actors_with_tags", lua_active_actors_with_tags);
    SDL3D_LUA_BIND("spawn", lua_spawn_actor);
    SDL3D_LUA_BIND("despawn", lua_despawn_actor);
    SDL3D_LUA_BIND("despawn_by_tag", lua_despawn_actors_by_tag);
    SDL3D_LUA_BIND("pool_capacity", lua_pool_capacity);
    SDL3D_LUA_BIND("pool_active_count", lua_pool_active_count);
    SDL3D_LUA_BIND("pool_available_count", lua_pool_available_count);
    SDL3D_LUA_BIND("log", lua_log);
    SDL3D_LUA_BIND("storage_read", lua_storage_read);
    SDL3D_LUA_BIND("storage_write", lua_storage_write);
    SDL3D_LUA_BIND("storage_exists", lua_storage_exists);
    SDL3D_LUA_BIND("storage_mkdir", lua_storage_mkdir);
    SDL3D_LUA_BIND("storage_delete", lua_storage_delete);
    SDL3D_LUA_BIND("json_decode", lua_json_decode);
    SDL3D_LUA_BIND("json_encode", lua_json_encode);
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

static const char *first_non_empty_string(const char *first, const char *second, const char *fallback)
{
    if (first != NULL && first[0] != '\0')
        return first;
    if (second != NULL && second[0] != '\0')
        return second;
    return fallback;
}

static char first_json_string_char(yyjson_val *object, const char *key, char fallback)
{
    const char *value = json_string(object, key, NULL);
    return value != NULL && value[0] != '\0' ? value[0] : fallback;
}

static void load_storage_config(sdl3d_game_data_runtime *runtime, yyjson_val *root);

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

static int json_int_or_string(yyjson_val *object, const char *key, int fallback)
{
    yyjson_val *value = obj_get(object, key);
    if (yyjson_is_int(value))
        return (int)yyjson_get_int(value);
    if (yyjson_is_str(value))
        return SDL_atoi(yyjson_get_str(value));
    return fallback;
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
    const scene_entry *scene = active_scene_entry_const(runtime);
    if (scene_has_entity(scene, entity_name))
        return true;
    if (runtime == NULL || scene == NULL || entity_name == NULL)
        return false;
    return actor_pool_in_scene(find_actor_pool_for_actor_const(runtime, entity_name, NULL), scene->name);
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

static bool entity_json_has_all_tags_from_json(yyjson_val *entity, yyjson_val *tags)
{
    if (!yyjson_is_arr(tags) || yyjson_arr_size(tags) == 0)
        return false;

    for (size_t i = 0; i < yyjson_arr_size(tags); ++i)
    {
        yyjson_val *tag = yyjson_arr_get(tags, i);
        if (!yyjson_is_str(tag) || !entity_json_has_tag(entity, yyjson_get_str(tag)))
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

static yyjson_val *find_sound_json(const sdl3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *sounds = obj_get(obj_get(runtime_root(runtime), "assets"), "sounds");
    for (size_t i = 0; id != NULL && yyjson_is_arr(sounds) && i < yyjson_arr_size(sounds); ++i)
    {
        yyjson_val *sound = yyjson_arr_get(sounds, i);
        const char *sound_id = json_string(sound, "id", NULL);
        if (sound_id != NULL && SDL_strcmp(sound_id, id) == 0)
            return sound;
    }
    return NULL;
}

static yyjson_val *find_music_json(const sdl3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *music_assets = obj_get(obj_get(runtime_root(runtime), "assets"), "music");
    for (size_t i = 0; id != NULL && yyjson_is_arr(music_assets) && i < yyjson_arr_size(music_assets); ++i)
    {
        yyjson_val *music = yyjson_arr_get(music_assets, i);
        const char *music_id = json_string(music, "id", NULL);
        if (music_id != NULL && SDL_strcmp(music_id, id) == 0)
            return music;
    }
    return NULL;
}

static yyjson_val *find_sprite_json(const sdl3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *sprites = obj_get(obj_get(runtime_root(runtime), "assets"), "sprites");
    for (size_t i = 0; id != NULL && yyjson_is_arr(sprites) && i < yyjson_arr_size(sprites); ++i)
    {
        yyjson_val *sprite = yyjson_arr_get(sprites, i);
        const char *sprite_id = json_string(sprite, "id", NULL);
        if (sprite_id != NULL && SDL_strcmp(sprite_id, id) == 0)
            return sprite;
    }
    return NULL;
}

static sdl3d_audio_bus parse_audio_bus(const char *bus, sdl3d_audio_bus fallback)
{
    if (bus == NULL)
        return fallback;
    if (SDL_strcmp(bus, "music") == 0)
        return SDL3D_AUDIO_BUS_MUSIC;
    if (SDL_strcmp(bus, "sound_effects") == 0 || SDL_strcmp(bus, "sfx") == 0)
        return SDL3D_AUDIO_BUS_SOUND_EFFECTS;
    if (SDL_strcmp(bus, "dialogue") == 0 || SDL_strcmp(bus, "dialog") == 0)
        return SDL3D_AUDIO_BUS_DIALOGUE;
    if (SDL_strcmp(bus, "ambience") == 0 || SDL_strcmp(bus, "ambiance") == 0 || SDL_strcmp(bus, "ambient") == 0)
        return SDL3D_AUDIO_BUS_AMBIENCE;
    return fallback;
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
    if (SDL_strcasecmp(value, "opengl") == 0 || SDL_strcasecmp(value, "gl") == 0 || SDL_strcasecmp(value, "gpu") == 0)
        return SDL3D_BACKEND_OPENGL;
    return fallback;
}

static sdl3d_window_mode parse_window_mode(const char *value, sdl3d_window_mode fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "windowed") == 0 || SDL_strcasecmp(value, "window") == 0)
        return SDL3D_WINDOW_MODE_WINDOWED;
    if (SDL_strcasecmp(value, "fullscreen_exclusive") == 0 || SDL_strcasecmp(value, "exclusive") == 0)
        return SDL3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE;
    if (SDL_strcasecmp(value, "fullscreen_borderless") == 0 || SDL_strcasecmp(value, "borderless") == 0 ||
        SDL_strcasecmp(value, "desktop_fullscreen") == 0)
        return SDL3D_WINDOW_MODE_FULLSCREEN_BORDERLESS;
    return fallback;
}

static void storage_config_from_root(yyjson_val *root, sdl3d_storage_config *out_config);

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

static const char *parse_ui_image_effect(const char *value)
{
    if (value == NULL || value[0] == '\0')
        return NULL;
    if (SDL_strcasecmp(value, "melt") == 0)
        return "melt";
    return value;
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

typedef struct game_data_snapshot_value
{
    sdl3d_replication_field_type type;
    union {
        bool as_bool;
        Sint32 as_int32;
        float as_float32;
        sdl3d_vec2 as_vec2;
        sdl3d_vec3 as_vec3;
    } value;
} game_data_snapshot_value;

typedef struct game_data_input_value
{
    int action_id;
    float value;
} game_data_input_value;

static yyjson_val *game_data_find_replication_channel_by_name(const sdl3d_game_data_runtime *runtime,
                                                              const char *replication_name, int *out_index)
{
    yyjson_val *replication = obj_get(obj_get(runtime_root(runtime), "network"), "replication");
    for (size_t i = 0; yyjson_is_arr(replication) && i < yyjson_arr_size(replication); ++i)
    {
        yyjson_val *channel = yyjson_arr_get(replication, i);
        if (SDL_strcmp(json_string(channel, "name", ""), replication_name != NULL ? replication_name : "") == 0)
        {
            if (out_index != NULL)
                *out_index = (int)i;
            return channel;
        }
    }
    return NULL;
}

static yyjson_val *game_data_find_replication_channel_by_index(const sdl3d_game_data_runtime *runtime, Uint32 index)
{
    yyjson_val *replication = obj_get(obj_get(runtime_root(runtime), "network"), "replication");
    return yyjson_is_arr(replication) && index < yyjson_arr_size(replication) ? yyjson_arr_get(replication, index)
                                                                              : NULL;
}

static bool game_data_replication_channel_is_host_to_client(yyjson_val *channel)
{
    return SDL_strcmp(json_string(channel, "direction", ""), "host_to_client") == 0;
}

static bool game_data_replication_channel_is_client_to_host(yyjson_val *channel)
{
    return SDL_strcmp(json_string(channel, "direction", ""), "client_to_host") == 0;
}

static size_t game_data_replication_channel_field_count(yyjson_val *channel)
{
    size_t count = 0U;
    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t i = 0; yyjson_is_arr(actors) && i < yyjson_arr_size(actors); ++i)
    {
        yyjson_val *fields = obj_get(yyjson_arr_get(actors, i), "fields");
        if (yyjson_is_arr(fields))
            count += yyjson_arr_size(fields);
    }
    return count;
}

static bool game_data_replication_channel_packet_size(yyjson_val *channel, size_t *out_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (channel == NULL)
        return false;

    size_t size = 4U + 4U + 4U + 4U + SDL3D_REPLICATION_SCHEMA_HASH_SIZE + 4U;
    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t actor_index = 0U; yyjson_is_arr(actors) && actor_index < yyjson_arr_size(actors); ++actor_index)
    {
        yyjson_val *fields = obj_get(yyjson_arr_get(actors, actor_index), "fields");
        for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
        {
            sdl3d_replication_field_descriptor field;
            if (!sdl3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field))
                return false;
            const size_t field_size = sdl3d_replication_field_wire_size(field.type);
            if (field_size == 0U || size > SIZE_MAX - 1U - field_size)
                return false;
            size += 1U + field_size;
        }
    }

    if (out_size != NULL)
        *out_size = size;
    return true;
}

static size_t game_data_replication_channel_input_count(yyjson_val *channel)
{
    yyjson_val *inputs = obj_get(channel, "inputs");
    return yyjson_is_arr(inputs) ? yyjson_arr_size(inputs) : 0U;
}

static bool game_data_replication_input_packet_size(yyjson_val *channel, size_t *out_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (channel == NULL)
        return false;

    const size_t input_count = game_data_replication_channel_input_count(channel);
    const size_t input_value_size = 1U + sdl3d_replication_field_wire_size(SDL3D_REPLICATION_FIELD_FLOAT32);
    size_t size = 4U + 4U + 4U + 4U + SDL3D_REPLICATION_SCHEMA_HASH_SIZE + 4U;
    if (input_value_size == 1U || input_count > (SIZE_MAX - size) / input_value_size)
        return false;
    size += input_count * input_value_size;

    if (out_size != NULL)
        *out_size = size;
    return true;
}

static const char *game_data_replication_input_action(yyjson_val *input)
{
    return json_string(input, "action", NULL);
}

static int game_data_replication_action_id(const sdl3d_game_data_runtime *runtime, yyjson_val *input)
{
    return sdl3d_game_data_find_action(runtime, game_data_replication_input_action(input));
}

static sdl3d_game_data_network_direction game_data_network_direction_from_string(const char *direction)
{
    if (direction == NULL)
        return SDL3D_GAME_DATA_NETWORK_DIRECTION_INVALID;
    if (SDL_strcmp(direction, "host_to_client") == 0)
        return SDL3D_GAME_DATA_NETWORK_DIRECTION_HOST_TO_CLIENT;
    if (SDL_strcmp(direction, "client_to_host") == 0)
        return SDL3D_GAME_DATA_NETWORK_DIRECTION_CLIENT_TO_HOST;
    if (SDL_strcmp(direction, "bidirectional") == 0)
        return SDL3D_GAME_DATA_NETWORK_DIRECTION_BIDIRECTIONAL;
    return SDL3D_GAME_DATA_NETWORK_DIRECTION_INVALID;
}

static const char *game_data_network_direction_name(sdl3d_game_data_network_direction direction)
{
    switch (direction)
    {
    case SDL3D_GAME_DATA_NETWORK_DIRECTION_HOST_TO_CLIENT:
        return "host_to_client";
    case SDL3D_GAME_DATA_NETWORK_DIRECTION_CLIENT_TO_HOST:
        return "client_to_host";
    case SDL3D_GAME_DATA_NETWORK_DIRECTION_BIDIRECTIONAL:
        return "bidirectional";
    default:
        return "invalid";
    }
}

static yyjson_val *game_data_find_network_control_by_name(const sdl3d_game_data_runtime *runtime,
                                                          const char *control_name, int *out_index)
{
    yyjson_val *controls = obj_get(obj_get(runtime_root(runtime), "network"), "control_messages");
    for (size_t i = 0; yyjson_is_arr(controls) && i < yyjson_arr_size(controls); ++i)
    {
        yyjson_val *control = yyjson_arr_get(controls, i);
        if (SDL_strcmp(json_string(control, "name", ""), control_name != NULL ? control_name : "") == 0)
        {
            if (out_index != NULL)
                *out_index = (int)i;
            return control;
        }
    }
    return NULL;
}

static yyjson_val *game_data_find_network_control_by_index(const sdl3d_game_data_runtime *runtime, Uint32 index)
{
    yyjson_val *controls = obj_get(obj_get(runtime_root(runtime), "network"), "control_messages");
    return yyjson_is_arr(controls) && index < yyjson_arr_size(controls) ? yyjson_arr_get(controls, index) : NULL;
}

static bool game_data_network_control_packet_size(size_t *out_size)
{
    const size_t size = 4U + 4U + 4U + 4U + SDL3D_REPLICATION_SCHEMA_HASH_SIZE;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

static int game_data_network_control_signal_id(const sdl3d_game_data_runtime *runtime, yyjson_val *control)
{
    return sdl3d_game_data_find_signal(runtime, json_string(control, "signal", NULL));
}

static const char *game_data_replication_property_key(const char *path)
{
    static const char prefix[] = "properties.";
    const size_t prefix_len = sizeof(prefix) - 1U;
    return path != NULL && SDL_strncmp(path, prefix, prefix_len) == 0 ? path + prefix_len : NULL;
}

static bool game_data_read_actor_replication_field(const sdl3d_registered_actor *actor,
                                                   const sdl3d_replication_field_descriptor *field,
                                                   game_data_snapshot_value *out_value)
{
    if (actor == NULL || field == NULL || out_value == NULL || field->path == NULL)
        return false;

    out_value->type = field->type;
    if (SDL_strcmp(field->path, "position") == 0)
    {
        if (field->type != SDL3D_REPLICATION_FIELD_VEC3)
            return false;
        out_value->value.as_vec3 = actor->position;
        return true;
    }
    if (SDL_strcmp(field->path, "rotation") == 0 || SDL_strcmp(field->path, "scale") == 0)
    {
        if (field->type != SDL3D_REPLICATION_FIELD_VEC3)
            return false;
        out_value->value.as_vec3 =
            sdl3d_properties_get_vec3(actor->props, field->path, sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
        return true;
    }

    const char *key = game_data_replication_property_key(field->path);
    const sdl3d_value *property = key != NULL ? sdl3d_properties_get_value(actor->props, key) : NULL;
    if (property == NULL)
        return false;

    switch (field->type)
    {
    case SDL3D_REPLICATION_FIELD_BOOL:
        if (property->type != SDL3D_VALUE_BOOL)
            return false;
        out_value->value.as_bool = property->as_bool;
        return true;
    case SDL3D_REPLICATION_FIELD_INT32:
    case SDL3D_REPLICATION_FIELD_ENUM_ID:
        if (property->type != SDL3D_VALUE_INT)
            return false;
        out_value->value.as_int32 = (Sint32)property->as_int;
        return true;
    case SDL3D_REPLICATION_FIELD_FLOAT32:
        if (property->type != SDL3D_VALUE_FLOAT)
            return false;
        out_value->value.as_float32 = property->as_float;
        return true;
    case SDL3D_REPLICATION_FIELD_VEC2:
        if (property->type != SDL3D_VALUE_VEC3)
            return false;
        out_value->value.as_vec2 = (sdl3d_vec2){property->as_vec3.x, property->as_vec3.y};
        return true;
    case SDL3D_REPLICATION_FIELD_VEC3:
        if (property->type != SDL3D_VALUE_VEC3)
            return false;
        out_value->value.as_vec3 = property->as_vec3;
        return true;
    default:
        return false;
    }
}

static bool game_data_write_snapshot_value(sdl3d_replication_writer *writer, const game_data_snapshot_value *value)
{
    if (writer == NULL || value == NULL || !sdl3d_replication_write_field_type(writer, value->type))
        return false;

    switch (value->type)
    {
    case SDL3D_REPLICATION_FIELD_BOOL:
        return sdl3d_replication_write_bool(writer, value->value.as_bool);
    case SDL3D_REPLICATION_FIELD_INT32:
        return sdl3d_replication_write_int32(writer, value->value.as_int32);
    case SDL3D_REPLICATION_FIELD_FLOAT32:
        return sdl3d_replication_write_float32(writer, value->value.as_float32);
    case SDL3D_REPLICATION_FIELD_ENUM_ID:
        return sdl3d_replication_write_enum_id(writer, value->value.as_int32);
    case SDL3D_REPLICATION_FIELD_VEC2:
        return sdl3d_replication_write_vec2(writer, value->value.as_vec2);
    case SDL3D_REPLICATION_FIELD_VEC3:
        return sdl3d_replication_write_vec3(writer, value->value.as_vec3);
    default:
        return false;
    }
}

static bool game_data_read_snapshot_value(sdl3d_replication_reader *reader, sdl3d_replication_field_type expected_type,
                                          game_data_snapshot_value *out_value)
{
    if (reader == NULL || out_value == NULL)
        return false;

    sdl3d_replication_field_type packet_type = SDL3D_REPLICATION_FIELD_BOOL;
    if (!sdl3d_replication_read_field_type(reader, &packet_type) || packet_type != expected_type)
        return false;

    out_value->type = expected_type;
    switch (expected_type)
    {
    case SDL3D_REPLICATION_FIELD_BOOL:
        return sdl3d_replication_read_bool(reader, &out_value->value.as_bool);
    case SDL3D_REPLICATION_FIELD_INT32:
        return sdl3d_replication_read_int32(reader, &out_value->value.as_int32);
    case SDL3D_REPLICATION_FIELD_FLOAT32:
        return sdl3d_replication_read_float32(reader, &out_value->value.as_float32);
    case SDL3D_REPLICATION_FIELD_ENUM_ID:
        return sdl3d_replication_read_enum_id(reader, &out_value->value.as_int32);
    case SDL3D_REPLICATION_FIELD_VEC2:
        return sdl3d_replication_read_vec2(reader, &out_value->value.as_vec2);
    case SDL3D_REPLICATION_FIELD_VEC3:
        return sdl3d_replication_read_vec3(reader, &out_value->value.as_vec3);
    default:
        return false;
    }
}

static bool game_data_apply_actor_replication_field(sdl3d_registered_actor *actor,
                                                    const sdl3d_replication_field_descriptor *field,
                                                    const game_data_snapshot_value *value)
{
    if (actor == NULL || field == NULL || value == NULL || field->path == NULL || field->type != value->type)
        return false;

    if (SDL_strcmp(field->path, "position") == 0)
    {
        if (value->type != SDL3D_REPLICATION_FIELD_VEC3)
            return false;
        actor_set_position(actor, value->value.as_vec3);
        return true;
    }
    if (SDL_strcmp(field->path, "rotation") == 0 || SDL_strcmp(field->path, "scale") == 0)
    {
        if (value->type != SDL3D_REPLICATION_FIELD_VEC3)
            return false;
        sdl3d_properties_set_vec3(actor->props, field->path, value->value.as_vec3);
        return true;
    }

    const char *key = game_data_replication_property_key(field->path);
    if (key == NULL)
        return false;

    switch (value->type)
    {
    case SDL3D_REPLICATION_FIELD_BOOL:
        sdl3d_properties_set_bool(actor->props, key, value->value.as_bool);
        return true;
    case SDL3D_REPLICATION_FIELD_INT32:
    case SDL3D_REPLICATION_FIELD_ENUM_ID:
        sdl3d_properties_set_int(actor->props, key, value->value.as_int32);
        return true;
    case SDL3D_REPLICATION_FIELD_FLOAT32:
        sdl3d_properties_set_float(actor->props, key, value->value.as_float32);
        return true;
    case SDL3D_REPLICATION_FIELD_VEC2: {
        const sdl3d_vec3 current = sdl3d_properties_get_vec3(actor->props, key, sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
        sdl3d_properties_set_vec3(actor->props, key,
                                  sdl3d_vec3_make(value->value.as_vec2.x, value->value.as_vec2.y, current.z));
        return true;
    }
    case SDL3D_REPLICATION_FIELD_VEC3:
        sdl3d_properties_set_vec3(actor->props, key, value->value.as_vec3);
        return true;
    default:
        return false;
    }
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
    if (SDL_strcmp(name, "BACKSPACE") == 0 || SDL_strcmp(name, "DELETE") == 0)
        return SDL_SCANCODE_BACKSPACE;
    if (SDL_strlen(name) == 1)
        return SDL_GetScancodeFromKey(SDL_GetKeyFromName(name), NULL);
    return SDL_GetScancodeFromName(name);
}

static const char *scancode_display_name(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return "-";
    const char *name = SDL_GetScancodeName(scancode);
    return name != NULL && name[0] != '\0' ? name : "-";
}

static Uint8 mouse_button_from_json(const char *name)
{
    if (name == NULL)
        return 0;
    if (SDL_strcmp(name, "LEFT") == 0)
        return SDL_BUTTON_LEFT;
    if (SDL_strcmp(name, "MIDDLE") == 0)
        return SDL_BUTTON_MIDDLE;
    if (SDL_strcmp(name, "RIGHT") == 0)
        return SDL_BUTTON_RIGHT;
    if (SDL_strcmp(name, "X1") == 0)
        return SDL_BUTTON_X1;
    if (SDL_strcmp(name, "X2") == 0)
        return SDL_BUTTON_X2;
    return 0;
}

static const char *mouse_button_display_name(Uint8 button)
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        return "Left Mouse";
    case SDL_BUTTON_MIDDLE:
        return "Middle Mouse";
    case SDL_BUTTON_RIGHT:
        return "Right Mouse";
    case SDL_BUTTON_X1:
        return "Mouse X1";
    case SDL_BUTTON_X2:
        return "Mouse X2";
    default:
        return "-";
    }
}

static sdl3d_mouse_axis mouse_axis_from_json(const char *name, bool *valid)
{
    if (valid != NULL)
        *valid = true;
    if (name == NULL)
    {
        if (valid != NULL)
            *valid = false;
        return SDL3D_MOUSE_AXIS_X;
    }
    if (SDL_strcmp(name, "x") == 0)
        return SDL3D_MOUSE_AXIS_X;
    if (SDL_strcmp(name, "y") == 0)
        return SDL3D_MOUSE_AXIS_Y;
    if (SDL_strcmp(name, "wheel") == 0)
        return SDL3D_MOUSE_AXIS_WHEEL;
    if (SDL_strcmp(name, "wheel_x") == 0)
        return SDL3D_MOUSE_AXIS_WHEEL_X;
    if (valid != NULL)
        *valid = false;
    return SDL3D_MOUSE_AXIS_X;
}

static const char *gamepad_button_display_name(SDL_GamepadButton button)
{
    if (button == SDL_GAMEPAD_BUTTON_INVALID)
        return "-";
    const char *name = SDL_GetGamepadStringForButton(button);
    return name != NULL && name[0] != '\0' ? name : "-";
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
    if (SDL_strcmp(name, "LEFT_STICK") == 0)
        return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    if (SDL_strcmp(name, "RIGHT_STICK") == 0)
        return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    if (SDL_strcmp(name, "LEFT_SHOULDER") == 0)
        return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    if (SDL_strcmp(name, "RIGHT_SHOULDER") == 0)
        return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    if (SDL_strcmp(name, "DPAD_UP") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_UP;
    if (SDL_strcmp(name, "DPAD_DOWN") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    if (SDL_strcmp(name, "DPAD_LEFT") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    if (SDL_strcmp(name, "DPAD_RIGHT") == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    if (SDL_strcmp(name, "GUIDE") == 0)
        return SDL_GAMEPAD_BUTTON_GUIDE;
    if (SDL_strcmp(name, "MISC1") == 0)
        return SDL_GAMEPAD_BUTTON_MISC1;
    if (SDL_strcmp(name, "RIGHT_PADDLE1") == 0)
        return SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1;
    if (SDL_strcmp(name, "LEFT_PADDLE1") == 0)
        return SDL_GAMEPAD_BUTTON_LEFT_PADDLE1;
    if (SDL_strcmp(name, "RIGHT_PADDLE2") == 0)
        return SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2;
    if (SDL_strcmp(name, "LEFT_PADDLE2") == 0)
        return SDL_GAMEPAD_BUTTON_LEFT_PADDLE2;
    if (SDL_strcmp(name, "TOUCHPAD") == 0)
        return SDL_GAMEPAD_BUTTON_TOUCHPAD;
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
        out_control->window_apply_signal_id = -1;
        out_control->window_settings_target = NULL;
        out_control->window_display_mode_key = NULL;
        out_control->window_renderer_key = NULL;
        out_control->window_vsync_key = NULL;
    }
    if (runtime == NULL || out_control == NULL)
        return false;

    yyjson_val *app = obj_get(runtime_root(runtime), "app");
    yyjson_val *pause = obj_get(app, "pause");
    yyjson_val *quit = obj_get(app, "quit");
    yyjson_val *window = obj_get(app, "window");
    yyjson_val *window_settings = obj_get(window, "settings");
    out_control->start_signal_id = sdl3d_game_data_find_signal(runtime, json_string(app, "start_signal", NULL));
    out_control->pause_action_id = sdl3d_game_data_find_action(runtime, json_string(pause, "action", NULL));
    out_control->startup_transition = json_string(app, "startup_transition", NULL);
    out_control->quit_action_id = sdl3d_game_data_find_action(runtime, json_string(quit, "action", NULL));
    out_control->quit_transition = json_string(quit, "transition", NULL);
    out_control->quit_signal_id = sdl3d_game_data_find_signal(runtime, json_string(quit, "quit_signal", NULL));
    out_control->window_apply_signal_id =
        sdl3d_game_data_find_signal(runtime, json_string(window, "apply_signal", NULL));
    out_control->window_settings_target = json_string(window_settings, "target", "entity.settings");
    out_control->window_display_mode_key = json_string(window_settings, "display_mode", "display_mode");
    out_control->window_renderer_key = json_string(window_settings, "renderer", "renderer");
    out_control->window_vsync_key = json_string(window_settings, "vsync", "vsync");
    return true;
}

bool sdl3d_game_data_app_signal_applies_window_settings(const sdl3d_game_data_runtime *runtime, int signal_id)
{
    if (runtime == NULL || signal_id < 0)
        return false;

    yyjson_val *window = obj_get(obj_get(runtime_root(runtime), "app"), "window");
    const char *apply_signal = json_string(window, "apply_signal", NULL);
    if (apply_signal != NULL && sdl3d_game_data_find_signal(runtime, apply_signal) == signal_id)
        return true;

    yyjson_val *apply_signals = obj_get(window, "apply_signals");
    if (!yyjson_is_arr(apply_signals))
        return false;

    for (size_t i = 0; i < yyjson_arr_size(apply_signals); ++i)
    {
        yyjson_val *signal = yyjson_arr_get(apply_signals, i);
        if (yyjson_is_str(signal) && sdl3d_game_data_find_signal(runtime, yyjson_get_str(signal)) == signal_id)
            return true;
    }
    return false;
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
    out_image->sprite = json_string(image, "sprite", NULL);
    return out_image->id != NULL && (out_image->path != NULL || out_image->sprite != NULL);
}

bool sdl3d_game_data_get_sound_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                     sdl3d_game_data_sound_asset *out_sound)
{
    if (out_sound != NULL)
    {
        SDL_zero(*out_sound);
        out_sound->volume = 1.0f;
        out_sound->pitch = 1.0f;
        out_sound->bus = SDL3D_AUDIO_BUS_SOUND_EFFECTS;
    }
    if (runtime == NULL || id == NULL || out_sound == NULL)
        return false;

    yyjson_val *sound = find_sound_json(runtime, id);
    if (!yyjson_is_obj(sound))
        return false;

    out_sound->id = json_string(sound, "id", NULL);
    out_sound->path = json_string(sound, "path", NULL);
    out_sound->volume = json_float(sound, "volume", out_sound->volume);
    out_sound->pitch = json_float(sound, "pitch", out_sound->pitch);
    out_sound->pan = json_float(sound, "pan", out_sound->pan);
    out_sound->bus = parse_audio_bus(json_string(sound, "bus", NULL), out_sound->bus);
    return out_sound->id != NULL && out_sound->path != NULL;
}

bool sdl3d_game_data_get_music_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                     sdl3d_game_data_music_asset *out_music)
{
    if (out_music != NULL)
    {
        SDL_zero(*out_music);
        out_music->volume = 1.0f;
        out_music->loop = true;
    }
    if (runtime == NULL || id == NULL || out_music == NULL)
        return false;

    yyjson_val *music = find_music_json(runtime, id);
    if (!yyjson_is_obj(music))
        return false;

    out_music->id = json_string(music, "id", NULL);
    out_music->path = json_string(music, "path", NULL);
    out_music->volume = json_float(music, "volume", out_music->volume);
    out_music->loop = json_bool(music, "loop", out_music->loop);
    return out_music->id != NULL && out_music->path != NULL;
}

bool sdl3d_game_data_get_sprite_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                      sdl3d_game_data_sprite_asset *out_sprite)
{
    if (out_sprite != NULL)
    {
        SDL_zero(*out_sprite);
        out_sprite->columns = 1;
        out_sprite->rows = 1;
        out_sprite->frame_count = 1;
        out_sprite->direction_count = 1;
        out_sprite->loop = true;
        out_sprite->lighting = true;
    }
    if (runtime == NULL || id == NULL || out_sprite == NULL)
        return false;

    yyjson_val *sprite = find_sprite_json(runtime, id);
    if (!yyjson_is_obj(sprite))
        return false;

    out_sprite->id = json_string(sprite, "id", NULL);
    out_sprite->path = json_string(sprite, "path", NULL);
    out_sprite->shader_vertex_path = json_string(sprite, "shader_vertex_path", NULL);
    out_sprite->shader_fragment_path = json_string(sprite, "shader_fragment_path", NULL);
    out_sprite->frame_width = json_int(sprite, "frame_width", out_sprite->frame_width);
    out_sprite->frame_height = json_int(sprite, "frame_height", out_sprite->frame_height);
    out_sprite->columns = json_int(sprite, "columns", out_sprite->columns);
    out_sprite->rows = json_int(sprite, "rows", out_sprite->rows);
    out_sprite->frame_count = json_int(sprite, "frame_count", out_sprite->frame_count);
    out_sprite->direction_count = json_int(sprite, "direction_count", out_sprite->direction_count);
    out_sprite->fps = json_float(sprite, "fps", out_sprite->fps);
    out_sprite->loop = json_bool(sprite, "loop", out_sprite->loop);
    out_sprite->lighting = json_bool(sprite, "lighting", out_sprite->lighting);
    out_sprite->emissive = json_bool(sprite, "emissive", out_sprite->emissive);
    out_sprite->visual_ground_offset = json_float(sprite, "visual_ground_offset", out_sprite->visual_ground_offset);
    out_sprite->effect = parse_ui_image_effect(json_string(sprite, "effect", NULL));
    out_sprite->effect_delay = json_float(sprite, "effect_delay", 0.0f);
    out_sprite->effect_duration = json_float(sprite, "effect_duration", 1.0f);

    if (out_sprite->id == NULL || out_sprite->path == NULL || out_sprite->frame_width <= 0 ||
        out_sprite->frame_height <= 0 || out_sprite->columns <= 0 || out_sprite->rows <= 0 ||
        out_sprite->frame_count <= 0 || out_sprite->direction_count <= 0)
    {
        SDL_zero(*out_sprite);
        return false;
    }
    return true;
}

bool sdl3d_game_data_load_sprite_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                       sdl3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                       int error_buffer_size)
{
    sdl3d_game_data_sprite_asset sprite;
    sdl3d_sprite_asset_source source;

    if (out_sprite != NULL)
        SDL_zero(*out_sprite);
    if (runtime == NULL || id == NULL || out_sprite == NULL)
        return false;

    if (!sdl3d_game_data_get_sprite_asset(runtime, id, &sprite))
    {
        set_error(error_buffer, error_buffer_size, "sprite asset not found");
        return false;
    }

    SDL_zero(source);
    source.kind = SDL3D_SPRITE_ASSET_SOURCE_SHEET;
    source.sheet_path = sprite.path;
    source.shader_vertex_path = sprite.shader_vertex_path;
    source.shader_fragment_path = sprite.shader_fragment_path;
    source.frame_width = sprite.frame_width;
    source.frame_height = sprite.frame_height;
    source.columns = sprite.columns;
    source.rows = sprite.rows;
    source.frame_count = sprite.frame_count;
    source.direction_count = sprite.direction_count;
    source.fps = sprite.fps;
    source.loop = sprite.loop;
    source.lighting = sprite.lighting;
    source.emissive = sprite.emissive;
    source.visual_ground_offset = sprite.visual_ground_offset;
    source.effect = sprite.effect;
    source.effect_delay = sprite.effect_delay;
    source.effect_duration = sprite.effect_duration;

    if (!sdl3d_sprite_asset_load(runtime->assets, &source, out_sprite, error_buffer, error_buffer_size))
        return false;
    return true;
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
    yyjson_val *target_entities = obj_get(light_json, "target_entities");
    for (size_t i = 0; target == NULL && yyjson_is_arr(target_entities) && i < yyjson_arr_size(target_entities); ++i)
    {
        const char *candidate = yyjson_get_str(yyjson_arr_get(target_entities, i));
        if (candidate != NULL && active_scene_has_entity_internal(runtime, candidate))
            target = sdl3d_game_data_find_actor(runtime, candidate);
    }
    for (size_t i = 0; target == NULL && yyjson_is_arr(target_entities) && i < yyjson_arr_size(target_entities); ++i)
    {
        const char *candidate = yyjson_get_str(yyjson_arr_get(target_entities, i));
        if (candidate != NULL)
            target = sdl3d_game_data_find_actor(runtime, candidate);
    }
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

static bool light_effect_sample_color_cycle(yyjson_val *effect, float time, sdl3d_vec3 fallback, sdl3d_vec3 *out_color)
{
    yyjson_val *colors = obj_get(effect, "colors");
    if (out_color == NULL || !yyjson_is_arr(colors) || yyjson_arr_size(colors) == 0)
        return false;

    const size_t count = yyjson_arr_size(colors);
    if (count == 1)
    {
        *out_color = json_vec3_value(yyjson_arr_get(colors, 0), fallback);
        return true;
    }

    const float duration = SDL_max(json_float(effect, "duration", 4.0f), 0.001f);
    float phase = json_float(effect, "phase", 0.0f);
    float cycle = SDL_fmodf(time / duration + phase, 1.0f);
    if (cycle < 0.0f)
        cycle += 1.0f;

    const float scaled = cycle * (float)count;
    size_t index = (size_t)SDL_floorf(scaled);
    if (index >= count)
        index = count - 1;
    const size_t next_index = (index + 1U) % count;

    float t = scaled - (float)index;
    if (json_bool(effect, "smooth", true))
        t = t * t * (3.0f - 2.0f * t);

    const sdl3d_vec3 a = json_vec3_value(yyjson_arr_get(colors, index), fallback);
    const sdl3d_vec3 b = json_vec3_value(yyjson_arr_get(colors, next_index), a);
    *out_color = sdl3d_vec3_make(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    return true;
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
        else if (SDL_strcmp(type, "color_cycle") == 0)
        {
            const float time = eval != NULL ? eval->time : 0.0f;
            sdl3d_vec3 target;
            if (light_effect_sample_color_cycle(
                    effect, time, sdl3d_vec3_make(light->color[0], light->color[1], light->color[2]), &target))
            {
                light_color_lerp(light->color, target, json_float(effect, "color_blend", 1.0f));
            }
            continue;
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

            const sdl3d_vec3 size_add = json_vec3(effect, "size_add", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
            primitive->size.x += size_add.x * pulse;
            primitive->size.y += size_add.y * pulse;
            primitive->size.z += size_add.z * pulse;
            primitive->radius += json_float(effect, "radius_add", 0.0f) * pulse;
        }
        else if (SDL_strcmp(type, "drift") == 0)
        {
            const float time = eval != NULL ? eval->time : 0.0f;
            const float phase = json_float(effect, "phase", 0.0f);
            const sdl3d_vec3 offset = json_vec3(effect, "offset", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
            const sdl3d_vec3 rates =
                json_vec3(effect, "rates",
                          sdl3d_vec3_make(json_float(effect, "rate", 1.0f), json_float(effect, "rate", 1.0f),
                                          json_float(effect, "rate", 1.0f)));
            primitive->position.x += offset.x * SDL_sinf(time * rates.x + phase);
            primitive->position.y += offset.y * SDL_cosf(time * rates.y + phase * 1.37f);
            primitive->position.z += offset.z * SDL_sinf(time * rates.z + phase * 0.73f);
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

    sdl3d_game_data_runtime *mutable_runtime = (sdl3d_game_data_runtime *)runtime;
    actor_lifecycle_defer_begin(mutable_runtime);
    bool keep_iterating = true;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; keep_iterating && yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
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
            primitive.rotation_axis = json_vec3(component, "rotation_axis", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
            primitive.rotation_angle = json_float(component, "rotation_angle", 0.0f);
            const char *rotation_property = json_string(component, "rotation_property", NULL);
            if (rotation_property != NULL)
                primitive.rotation_angle += sdl3d_properties_get_float(actor->props, rotation_property, 0.0f);
            primitive.color = json_color(component, "color", (sdl3d_color){255, 255, 255, 255});
            primitive.texture_image = json_string(component, "texture", NULL);
            primitive.lighting_enabled = json_bool(component, "lighting", true);
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
            {
                keep_iterating = false;
                break;
            }
        }
    }
    actor_lifecycle_defer_end(mutable_runtime);
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

    sdl3d_game_data_runtime *mutable_runtime = (sdl3d_game_data_runtime *)runtime;
    actor_lifecycle_defer_begin(mutable_runtime);
    bool keep_iterating = true;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; keep_iterating && yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
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
            keep_iterating = false;
    }
    actor_lifecycle_defer_end(mutable_runtime);
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

static bool set_ui_state_internal(sdl3d_game_data_runtime *runtime, const char *name,
                                  const sdl3d_game_data_ui_state *state, bool animated)
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
    entry->animated = animated;
    return true;
}

bool sdl3d_game_data_set_ui_state(sdl3d_game_data_runtime *runtime, const char *name,
                                  const sdl3d_game_data_ui_state *state)
{
    return set_ui_state_internal(runtime, name, state, false);
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

static void clear_animated_ui_states(sdl3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < runtime->ui_state_count;)
    {
        if (!runtime->ui_states[i].animated)
        {
            ++i;
            continue;
        }

        SDL_free(runtime->ui_states[i].name);
        if (i + 1 < runtime->ui_state_count)
            runtime->ui_states[i] = runtime->ui_states[runtime->ui_state_count - 1];
        --runtime->ui_state_count;
    }
}

static void reset_animation_scope_if_needed(sdl3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;

    const char *active_scene = sdl3d_game_data_active_scene(runtime);
    if (runtime->animation_scene == active_scene)
        return;

    if (runtime->animation_scene != NULL)
    {
        runtime->animation_count = 0;
        clear_animated_ui_states(runtime);
    }
    runtime->animation_scene = active_scene;
}

static bool ensure_animation_capacity(sdl3d_game_data_runtime *runtime, int required)
{
    if (runtime == NULL)
        return false;
    if (required <= runtime->animation_capacity)
        return true;

    int next_capacity = runtime->animation_capacity < 8 ? 8 : runtime->animation_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    game_data_animation *entries =
        (game_data_animation *)SDL_realloc(runtime->animations, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + runtime->animation_capacity, 0,
               (size_t)(next_capacity - runtime->animation_capacity) * sizeof(*entries));
    runtime->animations = entries;
    runtime->animation_capacity = next_capacity;
    return true;
}

static game_data_tween_easing parse_tween_easing(const char *name)
{
    if (name == NULL || SDL_strcmp(name, "linear") == 0)
        return GAME_DATA_TWEEN_LINEAR;
    if (SDL_strcmp(name, "in_quad") == 0)
        return GAME_DATA_TWEEN_IN_QUAD;
    if (SDL_strcmp(name, "out_quad") == 0)
        return GAME_DATA_TWEEN_OUT_QUAD;
    if (SDL_strcmp(name, "in_out_quad") == 0)
        return GAME_DATA_TWEEN_IN_OUT_QUAD;
    return GAME_DATA_TWEEN_LINEAR;
}

static game_data_tween_repeat parse_tween_repeat(const char *name)
{
    if (name == NULL || SDL_strcmp(name, "none") == 0)
        return GAME_DATA_TWEEN_REPEAT_NONE;
    if (SDL_strcmp(name, "loop") == 0)
        return GAME_DATA_TWEEN_REPEAT_LOOP;
    if (SDL_strcmp(name, "ping_pong") == 0)
        return GAME_DATA_TWEEN_REPEAT_PING_PONG;
    return GAME_DATA_TWEEN_REPEAT_NONE;
}

static float apply_tween_easing(game_data_tween_easing easing, float t)
{
    t = SDL_clamp(t, 0.0f, 1.0f);
    switch (easing)
    {
    case GAME_DATA_TWEEN_IN_QUAD:
        return t * t;
    case GAME_DATA_TWEEN_OUT_QUAD:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case GAME_DATA_TWEEN_IN_OUT_QUAD:
        return t < 0.5f ? 2.0f * t * t : 1.0f - SDL_powf(-2.0f * t + 2.0f, 2.0f) * 0.5f;
    case GAME_DATA_TWEEN_LINEAR:
    default:
        return t;
    }
}

static game_data_tween_value tween_float(float value)
{
    game_data_tween_value out;
    SDL_zero(out);
    out.type = GAME_DATA_TWEEN_FLOAT;
    out.as_float = value;
    return out;
}

static game_data_tween_value tween_vec3(sdl3d_vec3 value)
{
    game_data_tween_value out;
    SDL_zero(out);
    out.type = GAME_DATA_TWEEN_VEC3;
    out.as_vec3 = value;
    return out;
}

static game_data_tween_value tween_color(sdl3d_color value)
{
    game_data_tween_value out;
    SDL_zero(out);
    out.type = GAME_DATA_TWEEN_COLOR;
    out.as_color = value;
    return out;
}

static bool json_tween_value(yyjson_val *value, game_data_tween_value_type preferred, game_data_tween_value *out_value)
{
    if (value == NULL || out_value == NULL)
        return false;
    if (yyjson_is_num(value))
    {
        *out_value = tween_float((float)yyjson_get_num(value));
        return true;
    }
    if (yyjson_is_arr(value))
    {
        if (preferred == GAME_DATA_TWEEN_COLOR)
            *out_value = tween_color(json_color_value(value, (sdl3d_color){255, 255, 255, 255}));
        else
            *out_value = tween_vec3(json_vec3_value(value, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)));
        return true;
    }
    return false;
}

static game_data_tween_value_type tween_preferred_type(const char *value_type, const sdl3d_value *current,
                                                       const char *ui_property)
{
    if (value_type != NULL && SDL_strcmp(value_type, "color") == 0)
        return GAME_DATA_TWEEN_COLOR;
    if (value_type != NULL && SDL_strcmp(value_type, "vec3") == 0)
        return GAME_DATA_TWEEN_VEC3;
    if (ui_property != NULL && (SDL_strcmp(ui_property, "tint") == 0 || SDL_strcmp(ui_property, "color") == 0))
        return GAME_DATA_TWEEN_COLOR;
    if (current != NULL && current->type == SDL3D_VALUE_COLOR)
        return GAME_DATA_TWEEN_COLOR;
    if (current != NULL && current->type == SDL3D_VALUE_VEC3)
        return GAME_DATA_TWEEN_VEC3;
    return GAME_DATA_TWEEN_FLOAT;
}

static bool current_property_tween_value(const sdl3d_value *current, game_data_tween_value_type preferred,
                                         game_data_tween_value *out_value, sdl3d_value_type *out_property_type)
{
    if (current == NULL || out_value == NULL || out_property_type == NULL)
        return false;

    *out_property_type = current->type;
    switch (current->type)
    {
    case SDL3D_VALUE_INT:
        *out_value = tween_float((float)current->as_int);
        return true;
    case SDL3D_VALUE_FLOAT:
        *out_value = tween_float(current->as_float);
        return true;
    case SDL3D_VALUE_VEC3:
        *out_value = tween_vec3(current->as_vec3);
        return true;
    case SDL3D_VALUE_COLOR:
        *out_value = preferred == GAME_DATA_TWEEN_COLOR
                         ? tween_color(current->as_color)
                         : tween_vec3(sdl3d_vec3_make((float)current->as_color.r, (float)current->as_color.g,
                                                      (float)current->as_color.b));
        return true;
    case SDL3D_VALUE_BOOL:
    case SDL3D_VALUE_STRING:
        return false;
    }
    return false;
}

static bool current_ui_tween_value(const sdl3d_game_data_ui_state *state, const char *property,
                                   game_data_tween_value *out_value)
{
    if (property == NULL || out_value == NULL)
        return false;
    if (SDL_strcmp(property, "alpha") == 0)
        *out_value = tween_float(state != NULL ? state->alpha : 1.0f);
    else if (SDL_strcmp(property, "scale") == 0)
        *out_value = tween_float(state != NULL ? state->scale : 1.0f);
    else if (SDL_strcmp(property, "offset_x") == 0 || SDL_strcmp(property, "x") == 0)
        *out_value = tween_float(state != NULL ? state->offset_x : 0.0f);
    else if (SDL_strcmp(property, "offset_y") == 0 || SDL_strcmp(property, "y") == 0)
        *out_value = tween_float(state != NULL ? state->offset_y : 0.0f);
    else if (SDL_strcmp(property, "tint") == 0 || SDL_strcmp(property, "color") == 0)
        *out_value = tween_color(state != NULL ? state->tint : (sdl3d_color){255, 255, 255, 255});
    else
        return false;
    return true;
}

static game_data_tween_value interpolate_tween_value(const game_data_tween_value *from, const game_data_tween_value *to,
                                                     float t)
{
    if (from->type == GAME_DATA_TWEEN_VEC3 && to->type == GAME_DATA_TWEEN_VEC3)
    {
        return tween_vec3(sdl3d_vec3_make(from->as_vec3.x + (to->as_vec3.x - from->as_vec3.x) * t,
                                          from->as_vec3.y + (to->as_vec3.y - from->as_vec3.y) * t,
                                          from->as_vec3.z + (to->as_vec3.z - from->as_vec3.z) * t));
    }
    if (from->type == GAME_DATA_TWEEN_COLOR && to->type == GAME_DATA_TWEEN_COLOR)
    {
        return tween_color((sdl3d_color){
            (Uint8)SDL_clamp(
                (int)((float)from->as_color.r + ((float)to->as_color.r - (float)from->as_color.r) * t + 0.5f), 0, 255),
            (Uint8)SDL_clamp(
                (int)((float)from->as_color.g + ((float)to->as_color.g - (float)from->as_color.g) * t + 0.5f), 0, 255),
            (Uint8)SDL_clamp(
                (int)((float)from->as_color.b + ((float)to->as_color.b - (float)from->as_color.b) * t + 0.5f), 0, 255),
            (Uint8)SDL_clamp(
                (int)((float)from->as_color.a + ((float)to->as_color.a - (float)from->as_color.a) * t + 0.5f), 0, 255),
        });
    }
    return tween_float(from->as_float + (to->as_float - from->as_float) * t);
}

static void apply_ui_tween_value(sdl3d_game_data_runtime *runtime, const char *target, const char *property,
                                 const game_data_tween_value *value)
{
    sdl3d_game_data_ui_state state;
    (void)sdl3d_game_data_get_ui_state(runtime, target, &state);
    if (SDL_strcmp(property, "alpha") == 0 && value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SDL3D_GAME_DATA_UI_STATE_ALPHA;
        state.alpha = value->as_float;
    }
    else if (SDL_strcmp(property, "scale") == 0 && value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SDL3D_GAME_DATA_UI_STATE_SCALE;
        state.scale = value->as_float;
    }
    else if ((SDL_strcmp(property, "offset_x") == 0 || SDL_strcmp(property, "x") == 0) &&
             value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SDL3D_GAME_DATA_UI_STATE_OFFSET;
        state.offset_x = value->as_float;
    }
    else if ((SDL_strcmp(property, "offset_y") == 0 || SDL_strcmp(property, "y") == 0) &&
             value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SDL3D_GAME_DATA_UI_STATE_OFFSET;
        state.offset_y = value->as_float;
    }
    else if ((SDL_strcmp(property, "tint") == 0 || SDL_strcmp(property, "color") == 0) &&
             value->type == GAME_DATA_TWEEN_COLOR)
    {
        state.flags |= SDL3D_GAME_DATA_UI_STATE_TINT;
        state.tint = value->as_color;
    }
    (void)set_ui_state_internal(runtime, target, &state, true);
}

static void apply_property_tween_value(sdl3d_game_data_runtime *runtime, const game_data_animation *animation,
                                       const game_data_tween_value *value)
{
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, animation->target);
    if (actor == NULL || animation->key == NULL)
        return;

    if ((animation->property_type == SDL3D_VALUE_INT || animation->property_type == SDL3D_VALUE_FLOAT) &&
        value->type == GAME_DATA_TWEEN_FLOAT)
    {
        if (animation->property_type == SDL3D_VALUE_INT)
            sdl3d_properties_set_int(actor->props, animation->key, (int)SDL_floorf(value->as_float + 0.5f));
        else
            sdl3d_properties_set_float(actor->props, animation->key, value->as_float);
    }
    else if (animation->property_type == SDL3D_VALUE_VEC3 && value->type == GAME_DATA_TWEEN_VEC3)
    {
        sdl3d_properties_set_vec3(actor->props, animation->key, value->as_vec3);
    }
    else if (animation->property_type == SDL3D_VALUE_COLOR && value->type == GAME_DATA_TWEEN_COLOR)
    {
        sdl3d_properties_set_color(actor->props, animation->key, value->as_color);
    }
}

static void apply_animation_value(sdl3d_game_data_runtime *runtime, const game_data_animation *animation,
                                  const game_data_tween_value *value)
{
    if (animation->target_type == GAME_DATA_TWEEN_UI)
        apply_ui_tween_value(runtime, animation->target, animation->property, value);
    else
        apply_property_tween_value(runtime, animation, value);
}

static void remove_conflicting_animation(sdl3d_game_data_runtime *runtime, const game_data_animation *animation)
{
    for (int i = 0; runtime != NULL && i < runtime->animation_count;)
    {
        game_data_animation *existing = &runtime->animations[i];
        const char *existing_property =
            existing->target_type == GAME_DATA_TWEEN_PROPERTY ? existing->key : existing->property;
        const char *new_property =
            animation->target_type == GAME_DATA_TWEEN_PROPERTY ? animation->key : animation->property;
        if (existing->target_type == animation->target_type && existing->target != NULL && animation->target != NULL &&
            existing_property != NULL && new_property != NULL && SDL_strcmp(existing->target, animation->target) == 0 &&
            SDL_strcmp(existing_property, new_property) == 0)
        {
            runtime->animations[i] = runtime->animations[runtime->animation_count - 1];
            --runtime->animation_count;
            continue;
        }
        ++i;
    }
}

static bool start_animation(sdl3d_game_data_runtime *runtime, const game_data_animation *animation)
{
    if (runtime == NULL || animation == NULL || animation->target == NULL || animation->duration < 0.0f)
        return false;

    reset_animation_scope_if_needed(runtime);
    apply_animation_value(runtime, animation, &animation->from);

    if (animation->duration <= 0.0f)
    {
        apply_animation_value(runtime, animation, &animation->to);
        if (animation->done_signal_id >= 0)
            sdl3d_signal_emit(runtime_bus(runtime), animation->done_signal_id, NULL);
        return true;
    }

    remove_conflicting_animation(runtime, animation);
    if (!ensure_animation_capacity(runtime, runtime->animation_count + 1))
        return false;
    runtime->animations[runtime->animation_count] = *animation;
    ++runtime->animation_count;
    return true;
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
        out_policy->block_menus = true;
        out_policy->block_scene_shortcuts = true;
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
    out_policy->block_menus = json_bool(policy, "block_menus", out_policy->consume_input);
    out_policy->block_scene_shortcuts =
        json_bool(policy, "block_scene_shortcuts", json_bool(policy, "block_shortcuts", out_policy->consume_input));
    return out_policy->input != SDL3D_GAME_DATA_SKIP_INPUT_DISABLED;
}

bool sdl3d_game_data_get_active_timeline_policy(const sdl3d_game_data_runtime *runtime,
                                                sdl3d_game_data_timeline_policy *out_policy)
{
    if (out_policy != NULL)
        SDL_zero(*out_policy);
    if (runtime == NULL || out_policy == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *timeline = obj_get(scene != NULL ? scene->root : NULL, "timeline");
    if (!yyjson_is_obj(timeline) || !json_bool(timeline, "autoplay", false))
        return false;

    out_policy->block_menus = json_bool(timeline, "block_menus", false);
    out_policy->block_scene_shortcuts =
        json_bool(timeline, "block_scene_shortcuts", json_bool(timeline, "block_shortcuts", false));
    return true;
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
    if (previous != NULL && scene->name != NULL && SDL_strcmp(previous, scene->name) != 0 &&
        !apply_actor_pool_scene_exit_policies(runtime, previous, scene->name))
    {
        return false;
    }
    runtime->active_scene_index = (int)(scene - runtime->scenes);
    runtime->input_capture.active = false;
    clear_menu_text_entry_capture(runtime);
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
    if (phase != NULL && (SDL_strcmp(phase, "scene_activity") == 0 || SDL_strcmp(phase, "presentation") == 0 ||
                          SDL_strcmp(phase, "property_effects") == 0 || SDL_strcmp(phase, "particles") == 0))
        return true;
    return !paused;
}

static bool eval_data_condition(const sdl3d_game_data_runtime *runtime, yyjson_val *condition,
                                const sdl3d_game_data_ui_metrics *metrics);

static bool eval_update_phase_entry(const sdl3d_game_data_runtime *runtime, yyjson_val *entry, bool paused,
                                    bool fallback)
{
    if (yyjson_is_bool(entry))
        return yyjson_get_bool(entry);
    if (!yyjson_is_obj(entry))
        return fallback;
    yyjson_val *active_if = obj_get(entry, "active_if");
    if (active_if != NULL && !eval_data_condition(runtime, active_if, NULL))
        return false;
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
        return eval_update_phase_entry(runtime, scene_entry_json, paused, fallback);

    yyjson_val *root_entry_json = obj_get(obj_get(runtime_root(runtime), "update_phases"), phase);
    return eval_update_phase_entry(runtime, root_entry_json, paused, fallback);
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

static bool eval_data_condition(const sdl3d_game_data_runtime *runtime, yyjson_val *condition,
                                const sdl3d_game_data_ui_metrics *metrics);

static const scene_menu_state *active_scene_menu_for_metrics(const sdl3d_game_data_runtime *runtime,
                                                             const sdl3d_game_data_ui_metrics *metrics)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    if (runtime == NULL || scene == NULL || scene->menu_count <= 0)
        return NULL;

    for (int i = 0; i < scene->menu_count; ++i)
    {
        yyjson_val *active_if = obj_get(scene->menus[i].menu, "active_if");
        if (active_if == NULL || eval_data_condition(runtime, active_if, metrics))
            return &scene->menus[i];
    }
    return NULL;
}

bool sdl3d_game_data_get_active_menu_for_metrics(const sdl3d_game_data_runtime *runtime,
                                                 const sdl3d_game_data_ui_metrics *metrics,
                                                 sdl3d_game_data_menu *out_menu)
{
    if (out_menu != NULL)
    {
        SDL_zero(*out_menu);
        out_menu->up_action_id = -1;
        out_menu->down_action_id = -1;
        out_menu->left_action_id = -1;
        out_menu->right_action_id = -1;
        out_menu->select_action_id = -1;
        out_menu->back_action_id = -1;
        out_menu->move_signal_id = -1;
        out_menu->select_signal_id = -1;
    }
    if (runtime == NULL || out_menu == NULL)
        return false;

    const scene_menu_state *state = active_scene_menu_for_metrics(runtime, metrics);
    if (state == NULL)
        return false;
    yyjson_val *menu = state->menu;
    out_menu->name = json_string(menu, "name", NULL);
    out_menu->up_action_id = sdl3d_game_data_find_action(runtime, json_string(menu, "up_action", NULL));
    out_menu->down_action_id = sdl3d_game_data_find_action(runtime, json_string(menu, "down_action", NULL));
    out_menu->left_action_id = sdl3d_game_data_find_action(runtime, json_string(menu, "left_action", NULL));
    out_menu->right_action_id = sdl3d_game_data_find_action(runtime, json_string(menu, "right_action", NULL));
    out_menu->select_action_id = sdl3d_game_data_find_action(runtime, json_string(menu, "select_action", NULL));
    out_menu->back_action_id = sdl3d_game_data_find_action(runtime, json_string(menu, "back_action", NULL));
    out_menu->move_signal_id = sdl3d_game_data_find_signal(runtime, json_string(menu, "move_signal", NULL));
    out_menu->select_signal_id = sdl3d_game_data_find_signal(runtime, json_string(menu, "select_signal", NULL));
    out_menu->item_count = menu_runtime_item_count(runtime, state);
    out_menu->selected_index =
        out_menu->item_count > 0 ? SDL_clamp(state->selected_index, 0, out_menu->item_count - 1) : -1;
    return out_menu->name != NULL && out_menu->item_count > 0;
}

bool sdl3d_game_data_get_active_menu(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_menu *out_menu)
{
    return sdl3d_game_data_get_active_menu_for_metrics(runtime, NULL, out_menu);
}

bool sdl3d_game_data_menu_move(sdl3d_game_data_runtime *runtime, const char *menu_name, int delta)
{
    scene_menu_state *menu = find_scene_menu(active_scene_entry(runtime), menu_name);
    const int item_count = menu_runtime_item_count(runtime, menu);
    if (menu == NULL || item_count <= 0)
        return false;

    menu->selected_index = SDL_clamp(menu->selected_index, 0, item_count - 1);
    int next = (menu->selected_index + delta) % item_count;
    if (next < 0)
        next += item_count;
    menu->selected_index = next;
    update_dynamic_list_selection_state(runtime, menu);
    return true;
}

bool sdl3d_game_data_publish_menu_selection(sdl3d_game_data_runtime *runtime, const char *menu_name)
{
    scene_menu_state *menu = find_scene_menu(active_scene_entry(runtime), menu_name);
    const int item_count = menu_runtime_item_count(runtime, menu);
    if (menu == NULL || item_count <= 0)
        return false;

    menu->selected_index = SDL_clamp(menu->selected_index, 0, item_count - 1);
    update_dynamic_list_selection_state(runtime, menu);
    return true;
}

static runtime_collection *find_runtime_collection(sdl3d_game_data_runtime *runtime, const char *collection_name)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->collection_count; ++i)
    {
        if (SDL_strcmp(runtime->collections[i].name, collection_name) == 0)
            return &runtime->collections[i];
    }
    return NULL;
}

static const runtime_collection *find_runtime_collection_const(const sdl3d_game_data_runtime *runtime,
                                                               const char *collection_name)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->collection_count; ++i)
    {
        if (SDL_strcmp(runtime->collections[i].name, collection_name) == 0)
            return &runtime->collections[i];
    }
    return NULL;
}

static runtime_collection *get_or_create_runtime_collection(sdl3d_game_data_runtime *runtime,
                                                            const char *collection_name)
{
    runtime_collection *existing = find_runtime_collection(runtime, collection_name);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0')
        return NULL;

    if (runtime->collection_count >= runtime->collection_capacity)
    {
        const int next_capacity = runtime->collection_capacity > 0 ? runtime->collection_capacity * 2 : 4;
        runtime_collection *next = (runtime_collection *)SDL_realloc(
            runtime->collections, (size_t)next_capacity * sizeof(*runtime->collections));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->collection_capacity, 0,
                   (size_t)(next_capacity - runtime->collection_capacity) * sizeof(*runtime->collections));
        runtime->collections = next;
        runtime->collection_capacity = next_capacity;
    }

    runtime_collection *collection = &runtime->collections[runtime->collection_count];
    SDL_zero(*collection);
    collection->name = SDL_strdup(collection_name);
    if (collection->name == NULL)
        return NULL;
    ++runtime->collection_count;
    return collection;
}

static sdl3d_properties *runtime_collection_ensure_row(runtime_collection *collection, int row_index)
{
    if (collection == NULL || row_index < 0)
        return NULL;
    if (row_index > collection->row_count)
        return NULL;
    if (row_index >= collection->row_capacity)
    {
        int next_capacity = collection->row_capacity > 0 ? collection->row_capacity : 4;
        while (next_capacity <= row_index)
            next_capacity *= 2;
        sdl3d_properties **next =
            (sdl3d_properties **)SDL_realloc(collection->rows, (size_t)next_capacity * sizeof(*collection->rows));
        if (next == NULL)
            return NULL;
        SDL_memset(next + collection->row_capacity, 0,
                   (size_t)(next_capacity - collection->row_capacity) * sizeof(*collection->rows));
        collection->rows = next;
        collection->row_capacity = next_capacity;
    }
    if (collection->rows[row_index] == NULL)
    {
        collection->rows[row_index] = sdl3d_properties_create();
        if (collection->rows[row_index] == NULL)
            return NULL;
    }
    if (row_index >= collection->row_count)
        collection->row_count = row_index + 1;
    return collection->rows[row_index];
}

static bool runtime_collection_field_to_string(const runtime_collection *collection, int row_index,
                                               const char *field_name, char *buffer, size_t buffer_size)
{
    if (collection == NULL || row_index < 0 || row_index >= collection->row_count || field_name == NULL ||
        field_name[0] == '\0' || buffer == NULL || buffer_size == 0U || collection->rows[row_index] == NULL)
        return false;

    const sdl3d_value *value = sdl3d_properties_get_value(collection->rows[row_index], field_name);
    if (value == NULL)
        return false;

    switch (value->type)
    {
    case SDL3D_VALUE_INT:
        SDL_snprintf(buffer, buffer_size, "%d", value->as_int);
        return true;
    case SDL3D_VALUE_FLOAT:
        SDL_snprintf(buffer, buffer_size, "%.3f", (double)value->as_float);
        return true;
    case SDL3D_VALUE_BOOL:
        SDL_strlcpy(buffer, value->as_bool ? "true" : "false", buffer_size);
        return true;
    case SDL3D_VALUE_STRING:
        SDL_strlcpy(buffer, value->as_string != NULL ? value->as_string : "", buffer_size);
        return true;
    default:
        return false;
    }
}

bool sdl3d_game_data_runtime_collection_clear(sdl3d_game_data_runtime *runtime, const char *collection_name)
{
    runtime_collection *collection = find_runtime_collection(runtime, collection_name);
    if (collection == NULL)
        return runtime != NULL && collection_name != NULL && collection_name[0] != '\0';

    for (int i = 0; i < collection->row_count; ++i)
    {
        sdl3d_properties_destroy(collection->rows[i]);
        collection->rows[i] = NULL;
    }
    collection->row_count = 0;
    return true;
}

int sdl3d_game_data_runtime_collection_count(const sdl3d_game_data_runtime *runtime, const char *collection_name)
{
    const runtime_collection *collection = find_runtime_collection_const(runtime, collection_name);
    return collection != NULL ? collection->row_count : 0;
}

bool sdl3d_game_data_runtime_collection_set_string(sdl3d_game_data_runtime *runtime, const char *collection_name,
                                                   int row_index, const char *field_name, const char *value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    sdl3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    sdl3d_properties_set_string(row, field_name, value != NULL ? value : "");
    return true;
}

bool sdl3d_game_data_runtime_collection_set_int(sdl3d_game_data_runtime *runtime, const char *collection_name,
                                                int row_index, const char *field_name, int value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    sdl3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    sdl3d_properties_set_int(row, field_name, value);
    return true;
}

bool sdl3d_game_data_runtime_collection_set_float(sdl3d_game_data_runtime *runtime, const char *collection_name,
                                                  int row_index, const char *field_name, float value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    sdl3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    sdl3d_properties_set_float(row, field_name, value);
    return true;
}

bool sdl3d_game_data_runtime_collection_set_bool(sdl3d_game_data_runtime *runtime, const char *collection_name,
                                                 int row_index, const char *field_name, bool value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    sdl3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    sdl3d_properties_set_bool(row, field_name, value);
    return true;
}

static const char *game_data_network_state_name(sdl3d_network_state state)
{
    switch (state)
    {
    case SDL3D_NETWORK_STATE_DISCONNECTED:
        return "disconnected";
    case SDL3D_NETWORK_STATE_CONNECTING:
        return "connecting";
    case SDL3D_NETWORK_STATE_WAITING:
        return "waiting";
    case SDL3D_NETWORK_STATE_CONNECTED:
        return "connected";
    case SDL3D_NETWORK_STATE_REJECTED:
        return "rejected";
    case SDL3D_NETWORK_STATE_TIMED_OUT:
        return "timed_out";
    case SDL3D_NETWORK_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static runtime_direct_connect_session *find_direct_connect_session(sdl3d_game_data_runtime *runtime,
                                                                   const char *session_name)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->direct_connect_session_count; ++i)
    {
        if (SDL_strcmp(runtime->direct_connect_sessions[i].name, session_name) == 0)
            return &runtime->direct_connect_sessions[i];
    }
    return NULL;
}

static runtime_direct_connect_session *get_or_create_direct_connect_session(sdl3d_game_data_runtime *runtime,
                                                                            const char *session_name)
{
    runtime_direct_connect_session *existing = find_direct_connect_session(runtime, session_name);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;

    if (runtime->direct_connect_session_count >= runtime->direct_connect_session_capacity)
    {
        const int next_capacity =
            runtime->direct_connect_session_capacity > 0 ? runtime->direct_connect_session_capacity * 2 : 2;
        runtime_direct_connect_session *next = (runtime_direct_connect_session *)SDL_realloc(
            runtime->direct_connect_sessions, (size_t)next_capacity * sizeof(*runtime->direct_connect_sessions));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->direct_connect_session_capacity, 0,
                   (size_t)(next_capacity - runtime->direct_connect_session_capacity) *
                       sizeof(*runtime->direct_connect_sessions));
        runtime->direct_connect_sessions = next;
        runtime->direct_connect_session_capacity = next_capacity;
    }

    runtime_direct_connect_session *entry = &runtime->direct_connect_sessions[runtime->direct_connect_session_count];
    SDL_zero(*entry);
    entry->name = SDL_strdup(session_name);
    if (entry->name == NULL)
        return NULL;
    ++runtime->direct_connect_session_count;
    return entry;
}

static void direct_connect_publish_status_entry(sdl3d_game_data_runtime *runtime,
                                                const runtime_direct_connect_session *entry, const char *status_key,
                                                const char *state_key, const char *connected_key,
                                                const char *fallback_status)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const sdl3d_network_state state = entry != NULL && entry->session != NULL
                                          ? sdl3d_network_session_state(entry->session)
                                          : SDL3D_NETWORK_STATE_DISCONNECTED;
    const char *status = entry != NULL && entry->session != NULL ? sdl3d_network_session_status(entry->session) : NULL;
    if (status == NULL || status[0] == '\0')
        status = fallback_status != NULL ? fallback_status : game_data_network_state_name(state);

    if (status_key != NULL && status_key[0] != '\0')
        sdl3d_properties_set_string(runtime->scene_state, status_key, status);
    if (state_key != NULL && state_key[0] != '\0')
        sdl3d_properties_set_string(runtime->scene_state, state_key, game_data_network_state_name(state));
    if (connected_key != NULL && connected_key[0] != '\0')
        sdl3d_properties_set_bool(runtime->scene_state, connected_key, state == SDL3D_NETWORK_STATE_CONNECTED);
}

static void direct_connect_publish_manual_status(sdl3d_game_data_runtime *runtime, const char *status_key,
                                                 const char *state_key, const char *connected_key, const char *status,
                                                 const char *state, bool connected)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    if (status_key != NULL && status_key[0] != '\0')
        sdl3d_properties_set_string(runtime->scene_state, status_key, status != NULL ? status : "");
    if (state_key != NULL && state_key[0] != '\0')
        sdl3d_properties_set_string(runtime->scene_state, state_key, state != NULL ? state : "unknown");
    if (connected_key != NULL && connected_key[0] != '\0')
        sdl3d_properties_set_bool(runtime->scene_state, connected_key, connected);
}

sdl3d_network_session *sdl3d_game_data_get_network_direct_connect_session(sdl3d_game_data_runtime *runtime,
                                                                          const char *session_name)
{
    runtime_direct_connect_session *entry = find_direct_connect_session(runtime, session_name);
    return entry != NULL ? entry->session : NULL;
}

bool sdl3d_game_data_network_direct_connect_publish_status(sdl3d_game_data_runtime *runtime, const char *session_name,
                                                           const char *status_key, const char *state_key,
                                                           const char *connected_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    direct_connect_publish_status_entry(runtime, find_direct_connect_session(runtime, session_name), status_key,
                                        state_key, connected_key, "Disconnected");
    return true;
}

bool sdl3d_game_data_network_direct_connect_cancel(sdl3d_game_data_runtime *runtime, const char *session_name,
                                                   const char *status_key, const char *state_key,
                                                   const char *connected_key, const char *status)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_direct_connect_session *entry = find_direct_connect_session(runtime, session_name);
    if (entry != NULL && entry->session != NULL)
    {
        sdl3d_network_session_destroy(entry->session);
        entry->session = NULL;
    }
    direct_connect_publish_status_entry(runtime, entry, status_key, state_key, connected_key,
                                        status != NULL ? status : "Disconnected");
    return true;
}

bool sdl3d_game_data_network_direct_connect_start(sdl3d_game_data_runtime *runtime, const char *session_name,
                                                  const char *host, int port, const char *status_key,
                                                  const char *state_key, const char *connected_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_direct_connect_session *entry = get_or_create_direct_connect_session(runtime, session_name);
    if (entry == NULL)
        return false;

    if (host == NULL || host[0] == '\0')
    {
        direct_connect_publish_manual_status(runtime, status_key, state_key, connected_key, "Invalid host", "error",
                                             false);
        return false;
    }

    if (port <= 0 || port > 65535)
    {
        direct_connect_publish_manual_status(runtime, status_key, state_key, connected_key, "Invalid port", "error",
                                             false);
        return false;
    }

    if (entry->session != NULL)
    {
        sdl3d_network_session_destroy(entry->session);
        entry->session = NULL;
    }

    sdl3d_network_session_desc desc;
    sdl3d_network_session_desc_init(&desc);
    desc.role = SDL3D_NETWORK_ROLE_CLIENT;
    desc.host = host;
    desc.port = (Uint16)port;
    desc.local_port = 0;
    desc.handshake_timeout = 5.0f;
    desc.idle_timeout = 10.0f;

    if (!sdl3d_network_session_create(&desc, &entry->session))
    {
        direct_connect_publish_manual_status(runtime, status_key, state_key, connected_key, SDL_GetError(), "error",
                                             false);
        return false;
    }

    direct_connect_publish_status_entry(runtime, entry, status_key, state_key, connected_key, "Connecting");
    return true;
}

static runtime_host_session *find_host_session(sdl3d_game_data_runtime *runtime, const char *session_name)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->host_session_count; ++i)
    {
        if (SDL_strcmp(runtime->host_sessions[i].name, session_name) == 0)
            return &runtime->host_sessions[i];
    }
    return NULL;
}

static runtime_host_session *get_or_create_host_session(sdl3d_game_data_runtime *runtime, const char *session_name)
{
    runtime_host_session *existing = find_host_session(runtime, session_name);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;

    if (runtime->host_session_count >= runtime->host_session_capacity)
    {
        const int next_capacity = runtime->host_session_capacity > 0 ? runtime->host_session_capacity * 2 : 2;
        runtime_host_session *next =
            (runtime_host_session *)SDL_realloc(runtime->host_sessions, (size_t)next_capacity * sizeof(*next));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->host_session_capacity, 0,
                   (size_t)(next_capacity - runtime->host_session_capacity) * sizeof(*next));
        runtime->host_sessions = next;
        runtime->host_session_capacity = next_capacity;
    }

    runtime_host_session *entry = &runtime->host_sessions[runtime->host_session_count];
    SDL_zero(*entry);
    entry->name = SDL_strdup(session_name);
    if (entry->name == NULL)
        return NULL;
    ++runtime->host_session_count;
    return entry;
}

static void host_publish_manual_status(sdl3d_game_data_runtime *runtime, const char *status_key,
                                       const char *endpoint_key, const char *peer_key, const char *connected_key,
                                       const char *status, Uint16 port, const char *peer, bool connected)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    if (status_key != NULL && status_key[0] != '\0')
        sdl3d_properties_set_string(runtime->scene_state, status_key, status != NULL ? status : "");
    if (endpoint_key != NULL && endpoint_key[0] != '\0')
    {
        char endpoint[32];
        SDL_snprintf(endpoint, sizeof(endpoint), "UDP %u", (unsigned int)port);
        sdl3d_properties_set_string(runtime->scene_state, endpoint_key, endpoint);
    }
    if (peer_key != NULL && peer_key[0] != '\0')
        sdl3d_properties_set_string(runtime->scene_state, peer_key, peer != NULL ? peer : "Waiting for client");
    if (connected_key != NULL && connected_key[0] != '\0')
        sdl3d_properties_set_bool(runtime->scene_state, connected_key, connected);
}

static void host_publish_status_entry(sdl3d_game_data_runtime *runtime, const runtime_host_session *entry,
                                      const char *status_key, const char *endpoint_key, const char *peer_key,
                                      const char *connected_key, const char *fallback_status, Uint16 fallback_port)
{
    const sdl3d_network_state state = entry != NULL && entry->session != NULL
                                          ? sdl3d_network_session_state(entry->session)
                                          : SDL3D_NETWORK_STATE_DISCONNECTED;
    const char *status = entry != NULL && entry->session != NULL ? sdl3d_network_session_status(entry->session) : NULL;
    const Uint16 port =
        entry != NULL && entry->session != NULL ? sdl3d_network_session_port(entry->session) : fallback_port;
    char peer_label[SDL3D_NETWORK_MAX_HOST_LENGTH + 48];
    char peer_host[SDL3D_NETWORK_MAX_HOST_LENGTH];
    Uint16 peer_port = 0;
    const bool connected =
        entry != NULL && entry->session != NULL && sdl3d_network_session_is_connected(entry->session);

    if (status == NULL || status[0] == '\0')
        status = fallback_status != NULL ? fallback_status : game_data_network_state_name(state);
    SDL_snprintf(peer_label, sizeof(peer_label), "Waiting for client");
    SDL_zero(peer_host);
    if (connected &&
        sdl3d_network_session_get_peer_endpoint(entry->session, peer_host, (int)sizeof(peer_host), &peer_port))
        SDL_snprintf(peer_label, sizeof(peer_label), "Client 1 - %s:%u", peer_host, (unsigned int)peer_port);
    else if (connected)
        SDL_snprintf(peer_label, sizeof(peer_label), "Client 1 - Connected");

    host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key, status, port, peer_label,
                               connected);
}

sdl3d_network_session *sdl3d_game_data_get_network_host_session(sdl3d_game_data_runtime *runtime,
                                                                const char *session_name)
{
    runtime_host_session *entry = find_host_session(runtime, session_name);
    return entry != NULL ? entry->session : NULL;
}

bool sdl3d_game_data_network_host_publish_status(sdl3d_game_data_runtime *runtime, const char *session_name,
                                                 const char *status_key, const char *endpoint_key, const char *peer_key,
                                                 const char *connected_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    host_publish_status_entry(runtime, find_host_session(runtime, session_name), status_key, endpoint_key, peer_key,
                              connected_key, "Not hosting", SDL3D_NETWORK_DEFAULT_PORT);
    return true;
}

bool sdl3d_game_data_network_host_cancel(sdl3d_game_data_runtime *runtime, const char *session_name,
                                         const char *status_key, const char *endpoint_key, const char *peer_key,
                                         const char *connected_key, const char *status)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_host_session *entry = find_host_session(runtime, session_name);
    Uint16 port = SDL3D_NETWORK_DEFAULT_PORT;
    if (entry != NULL && entry->session != NULL)
    {
        port = sdl3d_network_session_port(entry->session);
        sdl3d_network_session_destroy(entry->session);
        entry->session = NULL;
    }
    host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key,
                               status != NULL ? status : "Not hosting", port, "Waiting for client", false);
    return true;
}

bool sdl3d_game_data_network_host_start(sdl3d_game_data_runtime *runtime, const char *session_name, int port,
                                        const char *advertised_name, const char *status_key, const char *endpoint_key,
                                        const char *peer_key, const char *connected_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_host_session *entry = get_or_create_host_session(runtime, session_name);
    if (entry == NULL)
        return false;

    if (entry->session != NULL)
    {
        host_publish_status_entry(runtime, entry, status_key, endpoint_key, peer_key, connected_key, "Waiting",
                                  (Uint16)port);
        return true;
    }

    if (port <= 0 || port > 65535)
    {
        host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key, "Invalid host port",
                                   SDL3D_NETWORK_DEFAULT_PORT, "Waiting for client", false);
        return false;
    }

    sdl3d_network_session_desc desc;
    sdl3d_network_session_desc_init(&desc);
    desc.role = SDL3D_NETWORK_ROLE_HOST;
    desc.host = NULL;
    desc.port = (Uint16)port;
    desc.local_port = 0;
    desc.handshake_timeout = 5.0f;
    desc.idle_timeout = 10.0f;
    desc.session_name = advertised_name != NULL && advertised_name[0] != '\0' ? advertised_name : "SDL3D Session";

    if (!sdl3d_network_session_create(&desc, &entry->session))
    {
        host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key, SDL_GetError(),
                                   (Uint16)SDL_max(port, 0), "Waiting for client", false);
        return false;
    }

    host_publish_status_entry(runtime, entry, status_key, endpoint_key, peer_key, connected_key, "Waiting",
                              (Uint16)port);
    return true;
}

static runtime_discovery_session *find_discovery_session(sdl3d_game_data_runtime *runtime, const char *session_name)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->discovery_session_count; ++i)
    {
        if (SDL_strcmp(runtime->discovery_sessions[i].name, session_name) == 0)
            return &runtime->discovery_sessions[i];
    }
    return NULL;
}

static runtime_discovery_session *get_or_create_discovery_session(sdl3d_game_data_runtime *runtime,
                                                                  const char *session_name)
{
    runtime_discovery_session *existing = find_discovery_session(runtime, session_name);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;

    if (runtime->discovery_session_count >= runtime->discovery_session_capacity)
    {
        const int next_capacity = runtime->discovery_session_capacity > 0 ? runtime->discovery_session_capacity * 2 : 2;
        runtime_discovery_session *next = (runtime_discovery_session *)SDL_realloc(
            runtime->discovery_sessions, (size_t)next_capacity * sizeof(*runtime->discovery_sessions));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->discovery_session_capacity, 0,
                   (size_t)(next_capacity - runtime->discovery_session_capacity) *
                       sizeof(*runtime->discovery_sessions));
        runtime->discovery_sessions = next;
        runtime->discovery_session_capacity = next_capacity;
    }

    runtime_discovery_session *entry = &runtime->discovery_sessions[runtime->discovery_session_count];
    SDL_zero(*entry);
    entry->name = SDL_strdup(session_name);
    if (entry->name == NULL)
        return NULL;
    ++runtime->discovery_session_count;
    return entry;
}

static void discovery_publish_manual_status(sdl3d_game_data_runtime *runtime, const char *collection_name,
                                            const char *status_key, const char *count_key, const char *status,
                                            int count)
{
    if (runtime == NULL)
        return;
    if (collection_name != NULL && collection_name[0] != '\0')
        (void)sdl3d_game_data_runtime_collection_clear(runtime, collection_name);
    if (runtime->scene_state != NULL && status_key != NULL && status_key[0] != '\0')
        sdl3d_properties_set_string(runtime->scene_state, status_key, status != NULL ? status : "");
    if (runtime->scene_state != NULL && count_key != NULL && count_key[0] != '\0')
        sdl3d_properties_set_int(runtime->scene_state, count_key, count);
}

static void discovery_publish_results(sdl3d_game_data_runtime *runtime, const runtime_discovery_session *entry,
                                      const char *collection_name, const char *status_key, const char *count_key)
{
    if (runtime == NULL)
        return;

    const int result_count =
        entry != NULL && entry->session != NULL ? sdl3d_network_discovery_session_result_count(entry->session) : 0;
    const char *status =
        entry != NULL && entry->session != NULL ? sdl3d_network_discovery_session_status(entry->session) : "Idle";
    if (status == NULL || status[0] == '\0')
        status = result_count > 0 ? "Session found" : "Scanning";

    if (collection_name != NULL && collection_name[0] != '\0')
    {
        (void)sdl3d_game_data_runtime_collection_clear(runtime, collection_name);
        for (int i = 0; i < result_count; ++i)
        {
            sdl3d_network_discovery_result result;
            char label[SDL3D_NETWORK_MAX_STATUS_LENGTH + SDL3D_NETWORK_MAX_HOST_LENGTH + 32];
            char endpoint[SDL3D_NETWORK_MAX_HOST_LENGTH + 16];
            SDL_zero(result);
            if (!sdl3d_network_discovery_session_get_result(entry->session, i, &result))
                continue;

            SDL_snprintf(endpoint, sizeof(endpoint), "%s:%u", result.host, (unsigned int)result.port);
            SDL_snprintf(label, sizeof(label), "%s  %s%s%s",
                         result.session_name[0] != '\0' ? result.session_name : "SDL3D Session", endpoint,
                         result.status[0] != '\0' ? "  " : "", result.status[0] != '\0' ? result.status : "");
            (void)sdl3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "label", label);
            (void)sdl3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "name",
                                                                result.session_name[0] != '\0' ? result.session_name
                                                                                               : "SDL3D Session");
            (void)sdl3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "host", result.host);
            (void)sdl3d_game_data_runtime_collection_set_int(runtime, collection_name, i, "port", (int)result.port);
            (void)sdl3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "status", result.status);
            (void)sdl3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "endpoint", endpoint);
        }
    }

    if (runtime->scene_state != NULL && status_key != NULL && status_key[0] != '\0')
        sdl3d_properties_set_string(runtime->scene_state, status_key, status);
    if (runtime->scene_state != NULL && count_key != NULL && count_key[0] != '\0')
        sdl3d_properties_set_int(runtime->scene_state, count_key, result_count);
}

bool sdl3d_game_data_network_discovery_start(sdl3d_game_data_runtime *runtime, const char *session_name,
                                             const char *host, int port, int local_port, const char *collection_name,
                                             const char *status_key, const char *count_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_discovery_session *entry = get_or_create_discovery_session(runtime, session_name);
    if (entry == NULL)
        return false;

    if (port <= 0 || port > 65535 || local_port < 0 || local_port > 65535)
    {
        discovery_publish_manual_status(runtime, collection_name, status_key, count_key, "Invalid discovery port", 0);
        return false;
    }

    if (entry->session == NULL)
    {
        sdl3d_network_discovery_session_desc desc;
        sdl3d_network_discovery_session_desc_init(&desc);
        desc.host = host != NULL && host[0] != '\0' ? host : NULL;
        desc.port = (Uint16)port;
        desc.local_port = (Uint16)local_port;
        if (!sdl3d_network_discovery_session_create(&desc, &entry->session))
        {
            discovery_publish_manual_status(runtime, collection_name, status_key, count_key, SDL_GetError(), 0);
            return false;
        }
    }

    if (!sdl3d_network_discovery_session_refresh(entry->session))
    {
        discovery_publish_manual_status(runtime, collection_name, status_key, count_key, SDL_GetError(), 0);
        return false;
    }

    discovery_publish_results(runtime, entry, collection_name, status_key, count_key);
    return true;
}

bool sdl3d_game_data_network_discovery_update(sdl3d_game_data_runtime *runtime, const char *session_name, float dt,
                                              const char *collection_name, const char *status_key,
                                              const char *count_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_discovery_session *entry = find_discovery_session(runtime, session_name);
    if (entry == NULL || entry->session == NULL)
    {
        discovery_publish_manual_status(runtime, collection_name, status_key, count_key, "Idle", 0);
        return true;
    }
    if (dt < 0.0f)
        dt = 0.0f;
    if (!sdl3d_network_discovery_session_update(entry->session, dt))
    {
        discovery_publish_results(runtime, entry, collection_name, status_key, count_key);
        return false;
    }
    discovery_publish_results(runtime, entry, collection_name, status_key, count_key);
    return true;
}

bool sdl3d_game_data_network_discovery_cancel(sdl3d_game_data_runtime *runtime, const char *session_name,
                                              const char *collection_name, const char *status_key,
                                              const char *count_key, const char *status)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_discovery_session *entry = find_discovery_session(runtime, session_name);
    if (entry != NULL && entry->session != NULL)
    {
        sdl3d_network_discovery_session_destroy(entry->session);
        entry->session = NULL;
    }
    discovery_publish_manual_status(runtime, collection_name, status_key, count_key,
                                    status != NULL ? status : "Discovery canceled", 0);
    return true;
}

bool sdl3d_game_data_network_discovery_connect_selected(sdl3d_game_data_runtime *runtime, const char *discovery_name,
                                                        const char *collection_name, int selected_index,
                                                        const char *direct_connect_name, const char *host_key,
                                                        const char *port_key, const char *status_key,
                                                        const char *state_key, const char *connected_key,
                                                        const char *connecting_status)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || selected_index < 0 ||
        direct_connect_name == NULL || direct_connect_name[0] == '\0')
    {
        return false;
    }

    const runtime_collection *collection = find_runtime_collection_const(runtime, collection_name);
    if (collection == NULL || selected_index >= collection->row_count || collection->rows[selected_index] == NULL)
    {
        direct_connect_publish_manual_status(runtime, status_key, state_key, connected_key, "No session selected",
                                             "error", false);
        return false;
    }

    const char *host = sdl3d_properties_get_string(collection->rows[selected_index], "host", NULL);
    char host_copy[SDL3D_NETWORK_MAX_HOST_LENGTH];
    const sdl3d_value *port_value = sdl3d_properties_get_value(collection->rows[selected_index], "port");
    const int port = port_value != NULL && port_value->type == SDL3D_VALUE_INT
                         ? port_value->as_int
                         : SDL_atoi(sdl3d_properties_get_string(collection->rows[selected_index], "port", "0"));
    SDL_strlcpy(host_copy, host != NULL ? host : "", sizeof(host_copy));
    if (runtime->scene_state != NULL)
    {
        if (host_key != NULL && host_key[0] != '\0')
            sdl3d_properties_set_string(runtime->scene_state, host_key, host_copy);
        if (port_key != NULL && port_key[0] != '\0')
        {
            char port_text[16];
            SDL_snprintf(port_text, sizeof(port_text), "%d", port);
            sdl3d_properties_set_string(runtime->scene_state, port_key, port_text);
        }
    }

    (void)sdl3d_game_data_network_discovery_cancel(runtime, discovery_name, collection_name, NULL, NULL,
                                                   "Discovery canceled");
    const bool ok = sdl3d_game_data_network_direct_connect_start(runtime, direct_connect_name, host_copy, port,
                                                                 status_key, state_key, connected_key);
    if (ok && connecting_status != NULL && connecting_status[0] != '\0' && runtime->scene_state != NULL &&
        status_key != NULL && status_key[0] != '\0')
    {
        sdl3d_properties_set_string(runtime->scene_state, status_key, connecting_status);
    }
    return ok;
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
    if (SDL_strcmp(type, "input_binding") == 0)
        return SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING;
    if (SDL_strcmp(type, "text") == 0)
        return SDL3D_GAME_DATA_MENU_CONTROL_TEXT;
    return SDL3D_GAME_DATA_MENU_CONTROL_NONE;
}

static bool menu_item_is_dynamic_list(yyjson_val *item)
{
    return yyjson_is_obj(item) && SDL_strcmp(json_string(item, "type", ""), "dynamic_list") == 0;
}

static yyjson_val *menu_dynamic_list_source(yyjson_val *item)
{
    return menu_item_is_dynamic_list(item) ? obj_get(item, "source") : NULL;
}

static int menu_dynamic_list_count(const sdl3d_game_data_runtime *runtime, yyjson_val *item)
{
    yyjson_val *source = menu_dynamic_list_source(item);
    if (runtime == NULL || !yyjson_is_obj(source))
        return 0;

    const char *type = json_string(source, "type", "");
    if (SDL_strcmp(type, "scene_state_indexed") == 0)
    {
        const char *count_key = json_string(source, "count_key", NULL);
        if (count_key == NULL)
            return 0;
        return SDL_max(0, sdl3d_properties_get_int(runtime->scene_state, count_key, 0));
    }
    if (SDL_strcmp(type, "runtime_collection") == 0)
        return sdl3d_game_data_runtime_collection_count(runtime, json_string(source, "collection", NULL));
    return 0;
}

static int menu_dynamic_list_row_count(const sdl3d_game_data_runtime *runtime, yyjson_val *item)
{
    const int count = menu_dynamic_list_count(runtime, item);
    if (count > 0)
        return count;
    return json_string(item, "empty_label", NULL) != NULL ? 1 : 0;
}

static int menu_authored_item_row_count(const sdl3d_game_data_runtime *runtime, yyjson_val *item)
{
    return menu_item_is_dynamic_list(item) ? menu_dynamic_list_row_count(runtime, item) : 1;
}

static int menu_runtime_item_count(const sdl3d_game_data_runtime *runtime, const scene_menu_state *menu)
{
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    int count = 0;
    for (size_t i = 0; yyjson_is_arr(items) && i < yyjson_arr_size(items); ++i)
        count += menu_authored_item_row_count(runtime, yyjson_arr_get(items, i));
    return count;
}

static bool resolve_menu_item_at(const sdl3d_game_data_runtime *runtime, const scene_menu_state *menu, int item_index,
                                 yyjson_val **out_item, int *out_dynamic_index, bool *out_dynamic_empty)
{
    if (out_item != NULL)
        *out_item = NULL;
    if (out_dynamic_index != NULL)
        *out_dynamic_index = -1;
    if (out_dynamic_empty != NULL)
        *out_dynamic_empty = false;
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    if (!yyjson_is_arr(items) || item_index < 0)
        return false;

    int cursor = 0;
    for (size_t i = 0; i < yyjson_arr_size(items); ++i)
    {
        yyjson_val *item = yyjson_arr_get(items, i);
        const int rows = menu_authored_item_row_count(runtime, item);
        if (item_index < cursor + rows)
        {
            if (out_item != NULL)
                *out_item = item;
            if (menu_item_is_dynamic_list(item))
            {
                const int source_count = menu_dynamic_list_count(runtime, item);
                if (source_count > 0)
                {
                    if (out_dynamic_index != NULL)
                        *out_dynamic_index = item_index - cursor;
                }
                else
                {
                    if (out_dynamic_empty != NULL)
                        *out_dynamic_empty = true;
                }
            }
            return true;
        }
        cursor += rows;
    }
    return false;
}

static bool menu_dynamic_list_format_key(yyjson_val *source, const char *field, int index, char *buffer,
                                         size_t buffer_size)
{
    const char *format = json_string(source, field, NULL);
    if (format == NULL || buffer == NULL || buffer_size == 0U)
        return false;

    const char *placeholder = SDL_strstr(format, "%d");
    if (placeholder == NULL)
        return false;
    if (SDL_strstr(placeholder + 2, "%") != NULL)
        return false;
    for (const char *scan = format; scan < placeholder; ++scan)
    {
        if (*scan == '%')
            return false;
    }

    char index_text[32];
    SDL_snprintf(index_text, sizeof(index_text), "%d", index);
    const size_t prefix_len = (size_t)(placeholder - format);
    const size_t suffix_len = SDL_strlen(placeholder + 2);
    const size_t required = prefix_len + SDL_strlen(index_text) + suffix_len + 1U;
    if (required > buffer_size)
        return false;
    SDL_memcpy(buffer, format, prefix_len);
    SDL_strlcpy(buffer + prefix_len, index_text, buffer_size - prefix_len);
    SDL_strlcpy(buffer + prefix_len + SDL_strlen(index_text), placeholder + 2,
                buffer_size - prefix_len - SDL_strlen(index_text));
    return true;
}

static const char *menu_dynamic_list_entry_string(const sdl3d_game_data_runtime *runtime, yyjson_val *item,
                                                  int dynamic_index, const char *format_field, char *buffer,
                                                  size_t buffer_size)
{
    yyjson_val *source = menu_dynamic_list_source(item);
    if (runtime == NULL || !yyjson_is_obj(source) || dynamic_index < 0 || buffer == NULL || buffer_size == 0U)
        return NULL;

    const char *type = json_string(source, "type", "");
    if (SDL_strcmp(type, "scene_state_indexed") == 0)
    {
        char key[SDL3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
        if (!menu_dynamic_list_format_key(source, format_field, dynamic_index, key, sizeof(key)))
            return NULL;
        return sdl3d_properties_get_string(runtime->scene_state, key, NULL);
    }
    if (SDL_strcmp(type, "runtime_collection") == 0)
    {
        const char *field = NULL;
        if (SDL_strcmp(format_field, "label_key_format") == 0)
            field = json_string(source, "label_field", NULL);
        else if (SDL_strcmp(format_field, "value_key_format") == 0)
            field = json_string(source, "value_field", NULL);
        if (field == NULL)
            return NULL;

        const runtime_collection *collection =
            find_runtime_collection_const(runtime, json_string(source, "collection", NULL));
        return runtime_collection_field_to_string(collection, dynamic_index, field, buffer, buffer_size) ? buffer
                                                                                                         : NULL;
    }
    return NULL;
}

static void menu_dynamic_list_apply_label_template(const char *format, const char *label, char *buffer,
                                                   size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    if (format == NULL)
    {
        SDL_strlcpy(buffer, label != NULL ? label : "", buffer_size);
        return;
    }

    const char *cursor = format;
    size_t offset = 0U;
    buffer[0] = '\0';
    while (*cursor != '\0' && offset + 1U < buffer_size)
    {
        const char *marker = SDL_strstr(cursor, "{label}");
        if (marker == NULL)
        {
            SDL_strlcpy(buffer + offset, cursor, buffer_size - offset);
            return;
        }
        const size_t prefix_len = (size_t)(marker - cursor);
        const size_t writable_prefix = SDL_min(prefix_len, buffer_size - offset - 1U);
        SDL_memcpy(buffer + offset, cursor, writable_prefix);
        offset += writable_prefix;
        buffer[offset] = '\0';
        const char *value = label != NULL ? label : "";
        const size_t value_len = SDL_strlen(value);
        const size_t writable_value = SDL_min(value_len, buffer_size - offset - 1U);
        SDL_memcpy(buffer + offset, value, writable_value);
        offset += writable_value;
        buffer[offset] = '\0';
        cursor = marker + SDL_strlen("{label}");
    }
}

static const char *menu_dynamic_list_label(const sdl3d_game_data_runtime *runtime, yyjson_val *item, int dynamic_index,
                                           bool dynamic_empty, char *buffer, size_t buffer_size)
{
    if (runtime == NULL || item == NULL || buffer == NULL || buffer_size == 0U)
        return NULL;
    if (dynamic_empty)
    {
        SDL_strlcpy(buffer, json_string(item, "empty_label", "No entries"), buffer_size);
        return buffer;
    }

    char raw_label[SDL3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
    const char *label =
        menu_dynamic_list_entry_string(runtime, item, dynamic_index, "label_key_format", raw_label, sizeof(raw_label));
    if (label == NULL)
        label = menu_dynamic_list_entry_string(runtime, item, dynamic_index, "value_key_format", raw_label,
                                               sizeof(raw_label));
    if (label == NULL)
        label = "";

    const char *label_format = json_string(item, "label_format", NULL);
    if (label_format != NULL)
    {
        menu_dynamic_list_apply_label_template(label_format, label, buffer, buffer_size);
        return buffer;
    }
    SDL_strlcpy(buffer, label, buffer_size);
    return buffer;
}

static const char *menu_dynamic_list_value(const sdl3d_game_data_runtime *runtime, yyjson_val *item, int dynamic_index,
                                           char *buffer, size_t buffer_size)
{
    if (runtime == NULL || item == NULL || dynamic_index < 0 || buffer == NULL || buffer_size == 0U)
        return NULL;

    char raw_value[SDL3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
    const char *value =
        menu_dynamic_list_entry_string(runtime, item, dynamic_index, "value_key_format", raw_value, sizeof(raw_value));
    if (value == NULL)
        value = menu_dynamic_list_entry_string(runtime, item, dynamic_index, "label_key_format", raw_value,
                                               sizeof(raw_value));
    if (value == NULL)
        return NULL;

    SDL_strlcpy(buffer, value, buffer_size);
    return buffer;
}

static const char *menu_dynamic_scene_state_value(const sdl3d_game_data_runtime *runtime, yyjson_val *item,
                                                  int dynamic_index, const char *value_from, char *buffer,
                                                  size_t buffer_size)
{
    if (runtime == NULL || item == NULL || dynamic_index < 0 || buffer == NULL || buffer_size == 0U)
        return NULL;
    if (value_from == NULL || SDL_strcmp(value_from, "value") == 0)
        return menu_dynamic_list_value(runtime, item, dynamic_index, buffer, buffer_size);
    if (SDL_strcmp(value_from, "label") == 0)
        return menu_dynamic_list_label(runtime, item, dynamic_index, false, buffer, buffer_size);
    if (SDL_strcmp(value_from, "index") == 0)
    {
        SDL_snprintf(buffer, buffer_size, "%d", dynamic_index);
        return buffer;
    }
    return NULL;
}

static void update_dynamic_list_selection_state(sdl3d_game_data_runtime *runtime, scene_menu_state *menu)
{
    yyjson_val *item = NULL;
    int dynamic_index = -1;
    bool dynamic_empty = false;
    if (runtime == NULL || menu == NULL ||
        !resolve_menu_item_at(runtime, menu, menu->selected_index, &item, &dynamic_index, &dynamic_empty) ||
        !menu_item_is_dynamic_list(item) || dynamic_empty)
        return;

    const char *selected_index_key = json_string(item, "selected_index_key", NULL);
    if (selected_index_key != NULL)
        sdl3d_properties_set_int(runtime->scene_state, selected_index_key, dynamic_index);
    const char *selected_value_key = json_string(item, "selected_value_key", NULL);
    char selected_value_buffer[SDL3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
    const char *selected_value =
        menu_dynamic_list_value(runtime, item, dynamic_index, selected_value_buffer, sizeof(selected_value_buffer));
    if (selected_value_key != NULL && selected_value != NULL)
        sdl3d_properties_set_string(runtime->scene_state, selected_value_key, selected_value);
}

static bool json_value_matches_property(yyjson_val *value, const sdl3d_value *property);
static sdl3d_game_data_menu_pause_command parse_menu_pause_command(const char *pause)
{
    if (pause == NULL)
        return SDL3D_GAME_DATA_MENU_PAUSE_NONE;
    if (SDL_strcmp(pause, "pause") == 0)
        return SDL3D_GAME_DATA_MENU_PAUSE_PAUSE;
    if (SDL_strcmp(pause, "resume") == 0 || SDL_strcmp(pause, "unpause") == 0)
        return SDL3D_GAME_DATA_MENU_PAUSE_RESUME;
    if (SDL_strcmp(pause, "toggle") == 0)
        return SDL3D_GAME_DATA_MENU_PAUSE_TOGGLE;
    return SDL3D_GAME_DATA_MENU_PAUSE_NONE;
}

bool sdl3d_game_data_get_menu_item(const sdl3d_game_data_runtime *runtime, const char *menu_name, int index,
                                   sdl3d_game_data_menu_item *out_item)
{
    if (out_item != NULL)
    {
        SDL_zero(*out_item);
        out_item->signal_id = -1;
    }
    const scene_menu_state *menu = find_scene_menu_const(active_scene_entry_const(runtime), menu_name);
    const int item_count = menu_runtime_item_count(runtime, menu);
    if (runtime == NULL || menu == NULL || out_item == NULL || index < 0 || index >= item_count)
        return false;

    yyjson_val *item = NULL;
    int dynamic_index = -1;
    bool dynamic_empty = false;
    if (!resolve_menu_item_at(runtime, menu, index, &item, &dynamic_index, &dynamic_empty))
        return false;

    if (menu_item_is_dynamic_list(item))
    {
        out_item->label =
            menu_dynamic_list_label(runtime, item, dynamic_index, dynamic_empty, out_item->dynamic_list_label_storage,
                                    sizeof(out_item->dynamic_list_label_storage));
        out_item->scene = dynamic_empty ? NULL : json_string(item, "scene", NULL);
        out_item->return_to = dynamic_empty ? NULL : json_string(item, "return_to", NULL);
        yyjson_val *scene_state = obj_get(item, "scene_state");
        out_item->scene_state_key = dynamic_empty ? NULL : json_string(scene_state, "key", NULL);
        if (!dynamic_empty && out_item->scene_state_key != NULL)
        {
            const char *static_scene_state_value = json_string(scene_state, "value", NULL);
            out_item->scene_state_value =
                static_scene_state_value != NULL
                    ? static_scene_state_value
                    : menu_dynamic_scene_state_value(runtime, item, dynamic_index,
                                                     json_string(scene_state, "value_from", "value"),
                                                     out_item->dynamic_list_scene_state_value_storage,
                                                     sizeof(out_item->dynamic_list_scene_state_value_storage));
        }
        out_item->return_scene = !dynamic_empty && json_bool(item, "return_scene", false);
        out_item->quit = !dynamic_empty && json_bool(item, "quit", false);
        out_item->signal_id =
            dynamic_empty ? -1 : sdl3d_game_data_find_signal(runtime, json_string(item, "signal", NULL));
        out_item->pause_command = dynamic_empty ? SDL3D_GAME_DATA_MENU_PAUSE_NONE
                                                : parse_menu_pause_command(json_string(item, "pause", NULL));
        out_item->has_return_paused = !dynamic_empty && obj_get(item, "return_paused") != NULL;
        out_item->return_paused = !dynamic_empty && json_bool(item, "return_paused", false);
        out_item->dynamic_list_item = true;
        out_item->dynamic_list_name = json_string(item, "name", NULL);
        out_item->dynamic_list_index = dynamic_index;
        out_item->dynamic_list_value =
            dynamic_empty ? NULL
                          : menu_dynamic_list_value(runtime, item, dynamic_index, out_item->dynamic_list_value_storage,
                                                    sizeof(out_item->dynamic_list_value_storage));
        return out_item->label != NULL;
    }

    yyjson_val *control = obj_get(item, "control");
    out_item->label = json_string(item, "label", NULL);
    out_item->scene = json_string(item, "scene", NULL);
    out_item->return_to = json_string(item, "return_to", NULL);
    yyjson_val *scene_state = obj_get(item, "scene_state");
    out_item->scene_state_key = json_string(scene_state, "key", NULL);
    out_item->scene_state_value = json_string(scene_state, "value", NULL);
    out_item->return_scene = json_bool(item, "return_scene", false);
    out_item->quit = json_bool(item, "quit", false);
    out_item->signal_id = sdl3d_game_data_find_signal(runtime, json_string(item, "signal", NULL));
    out_item->pause_command = parse_menu_pause_command(json_string(item, "pause", NULL));
    out_item->has_return_paused = obj_get(item, "return_paused") != NULL;
    out_item->return_paused = json_bool(item, "return_paused", false);
    out_item->control_type = parse_menu_control_type(json_string(control, "type", NULL));
    out_item->control_target = json_string(control, "target", NULL);
    out_item->control_key = json_string(control, "key", NULL);
    out_item->choice_count =
        yyjson_is_arr(obj_get(control, "choices")) ? (int)yyjson_arr_size(obj_get(control, "choices")) : 0;
    out_item->input_binding_count =
        yyjson_is_arr(obj_get(control, "bindings")) ? (int)yyjson_arr_size(obj_get(control, "bindings")) : 0;
    out_item->dynamic_list_index = -1;
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

static yyjson_val *find_menu_item_at(const sdl3d_game_data_runtime *runtime, const char *menu_name, int item_index)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    const scene_menu_state *menu = find_scene_menu_const(scene, menu_name);
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    if (runtime == NULL || menu == NULL || !yyjson_is_arr(items) || item_index < 0 || item_index >= menu->item_count)
        return NULL;
    return yyjson_arr_get(items, (size_t)item_index);
}

static void set_menu_binding_status(sdl3d_game_data_runtime *runtime, const char *status)
{
    if (runtime != NULL && runtime->scene_state != NULL)
    {
        sdl3d_properties_set_string(runtime->scene_state, "keyboard_binding_status", status != NULL ? status : "");
        sdl3d_properties_set_string(runtime->scene_state, "mouse_binding_status", status != NULL ? status : "");
        sdl3d_properties_set_string(runtime->scene_state, "gamepad_binding_status", status != NULL ? status : "");
    }
}

static menu_binding_device menu_binding_control_device(yyjson_val *control)
{
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        const char *device = json_string(binding, "device", "keyboard");
        if (SDL_strcmp(device, "mouse") == 0)
            return MENU_BINDING_DEVICE_MOUSE_BUTTON;
        if (SDL_strcmp(device, "gamepad") == 0)
            return MENU_BINDING_DEVICE_GAMEPAD_BUTTON;
    }
    return MENU_BINDING_DEVICE_KEYBOARD;
}

static const char *menu_binding_device_input_name(menu_binding_device device)
{
    switch (device)
    {
    case MENU_BINDING_DEVICE_MOUSE_BUTTON:
        return "mouse button";
    case MENU_BINDING_DEVICE_GAMEPAD_BUTTON:
        return "button";
    case MENU_BINDING_DEVICE_KEYBOARD:
    default:
        return "key";
    }
}

static SDL_Scancode current_action_keyboard_binding(const sdl3d_game_data_runtime *runtime, const char *action)
{
    const int action_id = sdl3d_game_data_find_action(runtime, action);
    for (int i = 0; runtime != NULL && action_id >= 0 && i < runtime->input_binding_count; ++i)
    {
        const input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SDL3D_INPUT_KEYBOARD)
            return spec->scancode;
    }
    return SDL_SCANCODE_UNKNOWN;
}

static SDL_GamepadButton current_action_gamepad_button_binding(const sdl3d_game_data_runtime *runtime,
                                                               const char *action)
{
    const int action_id = sdl3d_game_data_find_action(runtime, action);
    for (int i = 0; runtime != NULL && action_id >= 0 && i < runtime->input_binding_count; ++i)
    {
        const input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SDL3D_INPUT_GAMEPAD_BUTTON)
            return spec->gamepad_button;
    }
    return SDL_GAMEPAD_BUTTON_INVALID;
}

static Uint8 current_action_mouse_button_binding(const sdl3d_game_data_runtime *runtime, const char *action)
{
    const int action_id = sdl3d_game_data_find_action(runtime, action);
    for (int i = 0; runtime != NULL && action_id >= 0 && i < runtime->input_binding_count; ++i)
    {
        const input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SDL3D_INPUT_MOUSE_BUTTON)
            return spec->mouse_button;
    }
    return 0;
}

static SDL_Scancode current_input_binding_control_scancode(const sdl3d_game_data_runtime *runtime, yyjson_val *control)
{
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "keyboard") != 0)
            continue;
        const SDL_Scancode current = current_action_keyboard_binding(runtime, json_string(binding, "action", NULL));
        if (current != SDL_SCANCODE_UNKNOWN)
            return current;
    }
    return scancode_from_json(json_string(control, "default", NULL));
}

static SDL_GamepadButton current_input_binding_control_gamepad_button(const sdl3d_game_data_runtime *runtime,
                                                                      yyjson_val *control)
{
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "gamepad") != 0)
            continue;
        const SDL_GamepadButton current =
            current_action_gamepad_button_binding(runtime, json_string(binding, "action", NULL));
        if (current != SDL_GAMEPAD_BUTTON_INVALID)
            return current;
    }
    return gamepad_button_from_json(json_string(control, "default", NULL));
}

static Uint8 current_input_binding_control_mouse_button(const sdl3d_game_data_runtime *runtime, yyjson_val *control)
{
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "mouse") != 0)
            continue;
        const Uint8 current = current_action_mouse_button_binding(runtime, json_string(binding, "action", NULL));
        if (current != 0)
            return current;
    }
    return mouse_button_from_json(json_string(control, "default", NULL));
}

static bool set_input_binding_control_scancode(sdl3d_game_data_runtime *runtime, yyjson_val *control,
                                               SDL_Scancode scancode)
{
    bool changed = false;
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "keyboard") != 0)
            continue;
        if (set_action_keyboard_binding(runtime, json_string(binding, "action", NULL), scancode))
            changed = true;
    }
    return changed;
}

static bool set_input_binding_control_gamepad_button(sdl3d_game_data_runtime *runtime, yyjson_val *control,
                                                     SDL_GamepadButton button)
{
    bool changed = false;
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "gamepad") != 0)
            continue;
        if (set_action_gamepad_button_binding(runtime, json_string(binding, "action", NULL), button))
            changed = true;
    }
    return changed;
}

static bool set_input_binding_control_mouse_button(sdl3d_game_data_runtime *runtime, yyjson_val *control, Uint8 button)
{
    bool changed = false;
    yyjson_val *bindings = obj_get(control, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (SDL_strcmp(json_string(binding, "device", "keyboard"), "mouse") != 0)
            continue;
        if (set_action_mouse_button_binding(runtime, json_string(binding, "action", NULL), button))
            changed = true;
    }
    return changed;
}

static const char *keyboard_binding_conflict_label(const sdl3d_game_data_runtime *runtime, const char *menu_name,
                                                   int item_index, SDL_Scancode scancode)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    const scene_menu_state *menu = find_scene_menu_const(scene, menu_name);
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    for (int i = 0; yyjson_is_arr(items) && i < menu->item_count; ++i)
    {
        if (i == item_index)
            continue;
        yyjson_val *item = yyjson_arr_get(items, (size_t)i);
        yyjson_val *control = obj_get(item, "control");
        if (parse_menu_control_type(json_string(control, "type", NULL)) != SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            continue;
        if (menu_binding_control_device(control) != MENU_BINDING_DEVICE_KEYBOARD)
            continue;
        if (current_input_binding_control_scancode(runtime, control) == scancode)
            return json_string(item, "label", NULL);
    }
    return NULL;
}

static const char *gamepad_button_binding_conflict_label(const sdl3d_game_data_runtime *runtime, const char *menu_name,
                                                         int item_index, SDL_GamepadButton button)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    const scene_menu_state *menu = find_scene_menu_const(scene, menu_name);
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    for (int i = 0; yyjson_is_arr(items) && i < menu->item_count; ++i)
    {
        if (i == item_index)
            continue;
        yyjson_val *item = yyjson_arr_get(items, (size_t)i);
        yyjson_val *control = obj_get(item, "control");
        if (parse_menu_control_type(json_string(control, "type", NULL)) != SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            continue;
        if (menu_binding_control_device(control) != MENU_BINDING_DEVICE_GAMEPAD_BUTTON)
            continue;
        if (current_input_binding_control_gamepad_button(runtime, control) == button)
            return json_string(item, "label", NULL);
    }
    return NULL;
}

static const char *mouse_button_binding_conflict_label(const sdl3d_game_data_runtime *runtime, const char *menu_name,
                                                       int item_index, Uint8 button)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    const scene_menu_state *menu = find_scene_menu_const(scene, menu_name);
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    for (int i = 0; yyjson_is_arr(items) && i < menu->item_count; ++i)
    {
        if (i == item_index)
            continue;
        yyjson_val *item = yyjson_arr_get(items, (size_t)i);
        yyjson_val *control = obj_get(item, "control");
        if (parse_menu_control_type(json_string(control, "type", NULL)) != SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            continue;
        if (menu_binding_control_device(control) != MENU_BINDING_DEVICE_MOUSE_BUTTON)
            continue;
        if (current_input_binding_control_mouse_button(runtime, control) == button)
            return json_string(item, "label", NULL);
    }
    return NULL;
}

bool sdl3d_game_data_start_menu_input_binding_capture(sdl3d_game_data_runtime *runtime, const char *menu_name,
                                                      int item_index)
{
    yyjson_val *item = find_menu_item_at(runtime, menu_name, item_index);
    yyjson_val *control = obj_get(item, "control");
    if (runtime == NULL || menu_name == NULL ||
        parse_menu_control_type(json_string(control, "type", NULL)) != SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
        return false;

    runtime->input_capture.active = true;
    runtime->input_capture.menu = menu_name;
    runtime->input_capture.item_index = item_index;
    char status[128];
    SDL_snprintf(status, sizeof(status), "Press a %s for %s",
                 menu_binding_device_input_name(menu_binding_control_device(control)),
                 json_string(item, "label", "binding"));
    set_menu_binding_status(runtime, status);
    return true;
}

bool sdl3d_game_data_menu_input_binding_capture_active(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->input_capture.active;
}

sdl3d_game_data_input_binding_capture_status sdl3d_game_data_update_menu_input_binding_capture(
    sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input)
{
    if (runtime == NULL || !runtime->input_capture.active)
        return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_NONE;

    yyjson_val *item = find_menu_item_at(runtime, runtime->input_capture.menu, runtime->input_capture.item_index);
    yyjson_val *control = obj_get(item, "control");
    const menu_binding_device device = menu_binding_control_device(control);
    if (device == MENU_BINDING_DEVICE_GAMEPAD_BUTTON)
    {
        const SDL_Scancode cancel_key = scancode_from_json(json_string(control, "cancel_key", "ESCAPE"));
        if (sdl3d_input_get_pressed_scancode(input) == cancel_key)
        {
            runtime->input_capture.active = false;
            set_menu_binding_status(runtime, "Canceled");
            return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CANCELED;
        }

        const SDL_GamepadButton button = sdl3d_input_get_pressed_gamepad_button(input);
        if (button == SDL_GAMEPAD_BUTTON_INVALID)
            return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

        const char *conflict = gamepad_button_binding_conflict_label(runtime, runtime->input_capture.menu,
                                                                     runtime->input_capture.item_index, button);
        if (conflict != NULL)
        {
            char status[128];
            SDL_snprintf(status, sizeof(status), "%s already uses %s", conflict, gamepad_button_display_name(button));
            set_menu_binding_status(runtime, status);
            return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CONFLICT;
        }

        if (!set_input_binding_control_gamepad_button(runtime, control, button))
            return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

        char status[128];
        SDL_snprintf(status, sizeof(status), "%s set to %s", json_string(item, "label", "Binding"),
                     gamepad_button_display_name(button));
        runtime->input_capture.active = false;
        set_menu_binding_status(runtime, status);
        return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CHANGED;
    }
    if (device == MENU_BINDING_DEVICE_MOUSE_BUTTON)
    {
        const SDL_Scancode cancel_key = scancode_from_json(json_string(control, "cancel_key", "ESCAPE"));
        if (sdl3d_input_get_pressed_scancode(input) == cancel_key)
        {
            runtime->input_capture.active = false;
            set_menu_binding_status(runtime, "Canceled");
            return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CANCELED;
        }

        const Uint8 button = sdl3d_input_get_pressed_mouse_button(input);
        if (button == 0)
            return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

        const char *conflict = mouse_button_binding_conflict_label(runtime, runtime->input_capture.menu,
                                                                   runtime->input_capture.item_index, button);
        if (conflict != NULL)
        {
            char status[128];
            SDL_snprintf(status, sizeof(status), "%s already uses %s", conflict, mouse_button_display_name(button));
            set_menu_binding_status(runtime, status);
            return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CONFLICT;
        }

        if (!set_input_binding_control_mouse_button(runtime, control, button))
            return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

        char status[128];
        SDL_snprintf(status, sizeof(status), "%s set to %s", json_string(item, "label", "Binding"),
                     mouse_button_display_name(button));
        runtime->input_capture.active = false;
        set_menu_binding_status(runtime, status);
        return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CHANGED;
    }

    const SDL_Scancode scancode = sdl3d_input_get_pressed_scancode(input);
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

    const SDL_Scancode cancel = scancode_from_json(json_string(control, "cancel_key", "ESCAPE"));
    if (scancode == cancel && !json_bool(control, "allow_escape", false))
    {
        runtime->input_capture.active = false;
        set_menu_binding_status(runtime, "Canceled");
        return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CANCELED;
    }

    const char *conflict = keyboard_binding_conflict_label(runtime, runtime->input_capture.menu,
                                                           runtime->input_capture.item_index, scancode);
    if (conflict != NULL)
    {
        char status[128];
        SDL_snprintf(status, sizeof(status), "%s already uses %s", conflict, scancode_display_name(scancode));
        set_menu_binding_status(runtime, status);
        return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CONFLICT;
    }

    if (!set_input_binding_control_scancode(runtime, control, scancode))
        return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING;

    char status[128];
    SDL_snprintf(status, sizeof(status), "%s set to %s", json_string(item, "label", "Binding"),
                 scancode_display_name(scancode));
    runtime->input_capture.active = false;
    set_menu_binding_status(runtime, status);
    return SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CHANGED;
}

bool sdl3d_game_data_reset_menu_input_bindings(sdl3d_game_data_runtime *runtime, const char *menu_name)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    const scene_menu_state *menu = find_scene_menu_const(scene, menu_name);
    yyjson_val *items = menu != NULL ? obj_get(menu->menu, "items") : NULL;
    bool changed = false;
    for (int i = 0; yyjson_is_arr(items) && i < menu->item_count; ++i)
    {
        yyjson_val *item = yyjson_arr_get(items, (size_t)i);
        yyjson_val *control = obj_get(item, "control");
        if (parse_menu_control_type(json_string(control, "type", NULL)) != SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            continue;
        const menu_binding_device device = menu_binding_control_device(control);
        if (device == MENU_BINDING_DEVICE_GAMEPAD_BUTTON)
        {
            const SDL_GamepadButton button = gamepad_button_from_json(json_string(control, "default", NULL));
            if (button != SDL_GAMEPAD_BUTTON_INVALID &&
                set_input_binding_control_gamepad_button(runtime, control, button))
                changed = true;
        }
        else if (device == MENU_BINDING_DEVICE_MOUSE_BUTTON)
        {
            const Uint8 button = mouse_button_from_json(json_string(control, "default", NULL));
            if (button != 0 && set_input_binding_control_mouse_button(runtime, control, button))
                changed = true;
        }
        else
        {
            const SDL_Scancode key = scancode_from_json(json_string(control, "default", NULL));
            if (key != SDL_SCANCODE_UNKNOWN && set_input_binding_control_scancode(runtime, control, key))
                changed = true;
        }
    }
    runtime->input_capture.active = false;
    if (changed)
        set_menu_binding_status(runtime, "Input settings reset");
    return changed;
}

static sdl3d_properties *menu_control_properties(sdl3d_game_data_runtime *runtime, yyjson_val *control)
{
    if (runtime == NULL || control == NULL)
        return NULL;

    const char *target = json_string(control, "target", NULL);
    if (target == NULL || SDL_strcmp(target, "scene_state") == 0)
        return runtime->scene_state;

    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, target);
    return actor != NULL ? actor->props : NULL;
}

static const sdl3d_properties *menu_control_properties_const(const sdl3d_game_data_runtime *runtime,
                                                             yyjson_val *control)
{
    return menu_control_properties((sdl3d_game_data_runtime *)runtime, control);
}

static const char *menu_control_string_value(const sdl3d_game_data_runtime *runtime, yyjson_val *control)
{
    const char *key = json_string(control, "key", NULL);
    const char *fallback = json_string(control, "default", "");
    const sdl3d_properties *props = menu_control_properties_const(runtime, control);
    return props != NULL && key != NULL ? sdl3d_properties_get_string(props, key, fallback) : fallback;
}

static bool menu_control_set_string_value(sdl3d_game_data_runtime *runtime, yyjson_val *control, const char *value)
{
    const char *key = json_string(control, "key", NULL);
    sdl3d_properties *props = menu_control_properties(runtime, control);
    if (props == NULL || key == NULL || value == NULL)
        return false;
    sdl3d_properties_set_string(props, key, value);
    return true;
}

static bool menu_text_restricted_charset(const char *charset)
{
    return charset != NULL && SDL_strcmp(charset, "text") != 0 && SDL_strcmp(charset, "utf8") != 0;
}

static bool menu_text_allowed_ascii(const char *charset, unsigned char ch)
{
    if (ch < 0x20U || ch == 0x7fU)
        return false;

    if (charset == NULL || SDL_strcmp(charset, "text") == 0 || SDL_strcmp(charset, "utf8") == 0)
        return true;
    if (SDL_strcmp(charset, "ascii") == 0)
        return ch < 0x80U;
    if (SDL_strcmp(charset, "integer") == 0 || SDL_strcmp(charset, "digits") == 0)
        return ch >= '0' && ch <= '9';
    if (SDL_strcmp(charset, "numeric") == 0)
        return (ch >= '0' && ch <= '9') || ch == '.' || ch == '-';
    if (SDL_strcmp(charset, "hostname") == 0)
    {
        return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '.' ||
               ch == '-' || ch == '_' || ch == ':';
    }
    return true;
}

static int menu_text_valid_utf8_sequence_bytes(const unsigned char *cursor, size_t remaining)
{
    if (cursor == NULL || remaining == 0U || cursor[0] == '\0')
        return 0;

    const unsigned char b0 = cursor[0];
    if ((b0 & 0x80U) == 0U)
        return 1;
    if (remaining < 2U)
        return 0;
    if (b0 >= 0xc2U && b0 <= 0xdfU)
        return (cursor[1] & 0xc0U) == 0x80U ? 2 : 0;
    if (remaining < 3U)
        return 0;
    if (b0 == 0xe0U)
        return (cursor[1] >= 0xa0U && cursor[1] <= 0xbfU && (cursor[2] & 0xc0U) == 0x80U) ? 3 : 0;
    if ((b0 >= 0xe1U && b0 <= 0xecU) || (b0 >= 0xeeU && b0 <= 0xefU))
        return ((cursor[1] & 0xc0U) == 0x80U && (cursor[2] & 0xc0U) == 0x80U) ? 3 : 0;
    if (b0 == 0xedU)
        return (cursor[1] >= 0x80U && cursor[1] <= 0x9fU && (cursor[2] & 0xc0U) == 0x80U) ? 3 : 0;
    if (remaining < 4U)
        return 0;
    if (b0 == 0xf0U)
        return (cursor[1] >= 0x90U && cursor[1] <= 0xbfU && (cursor[2] & 0xc0U) == 0x80U &&
                (cursor[3] & 0xc0U) == 0x80U)
                   ? 4
                   : 0;
    if (b0 >= 0xf1U && b0 <= 0xf3U)
        return ((cursor[1] & 0xc0U) == 0x80U && (cursor[2] & 0xc0U) == 0x80U && (cursor[3] & 0xc0U) == 0x80U) ? 4 : 0;
    if (b0 == 0xf4U)
        return (cursor[1] >= 0x80U && cursor[1] <= 0x8fU && (cursor[2] & 0xc0U) == 0x80U &&
                (cursor[3] & 0xc0U) == 0x80U)
                   ? 4
                   : 0;
    return 0;
}

static bool menu_text_append_filtered(yyjson_val *control, char *buffer, size_t buffer_size, const char *text)
{
    if (control == NULL || buffer == NULL || buffer_size == 0U || text == NULL || text[0] == '\0')
        return false;

    bool changed = false;
    size_t used = SDL_strlen(buffer);
    const int max_length = SDL_clamp(json_int(control, "max_length", SDL3D_GAME_DATA_MENU_TEXT_MAX_BYTES), 0,
                                     (int)SDL_min(buffer_size - 1U, (size_t)SDL3D_GAME_DATA_MENU_TEXT_MAX_BYTES));
    const char *charset = json_string(control, "charset", json_string(control, "allow", "text"));
    const unsigned char *cursor = (const unsigned char *)text;
    size_t remaining = SDL_strlen(text);
    while (remaining > 0U && *cursor != '\0' && used < (size_t)max_length)
    {
        int bytes = menu_text_valid_utf8_sequence_bytes(cursor, remaining);
        if (bytes <= 0)
        {
            ++cursor;
            --remaining;
            continue;
        }
        if (bytes == 1)
        {
            if (!menu_text_allowed_ascii(charset, *cursor))
            {
                ++cursor;
                --remaining;
                continue;
            }
        }
        else if (menu_text_restricted_charset(charset))
        {
            cursor += bytes;
            remaining -= (size_t)bytes;
            continue;
        }

        if (used + (size_t)bytes > (size_t)max_length || used + (size_t)bytes + 1U > buffer_size)
            break;
        for (int i = 0; i < bytes; ++i)
            buffer[used++] = (char)*cursor++;
        remaining -= (size_t)bytes;
        buffer[used] = '\0';
        changed = true;
    }
    return changed;
}

static bool menu_text_backspace(char *buffer)
{
    if (buffer == NULL || buffer[0] == '\0')
        return false;

    size_t len = SDL_strlen(buffer);
    do
    {
        --len;
    } while (len > 0U && (((unsigned char)buffer[len] & 0xc0U) == 0x80U));
    buffer[len] = '\0';
    return true;
}

bool sdl3d_game_data_start_menu_text_entry_capture(sdl3d_game_data_runtime *runtime, const char *menu_name,
                                                   int item_index)
{
    yyjson_val *item = find_menu_item_at(runtime, menu_name, item_index);
    yyjson_val *control = obj_get(item, "control");
    if (runtime == NULL || menu_name == NULL ||
        parse_menu_control_type(json_string(control, "type", NULL)) != SDL3D_GAME_DATA_MENU_CONTROL_TEXT)
        return false;

    clear_menu_text_entry_capture(runtime);
    runtime->text_capture.active = true;
    runtime->text_capture.menu = menu_name;
    runtime->text_capture.item_index = item_index;
    runtime->text_capture.original = SDL_strdup(menu_control_string_value(runtime, control));
    if (runtime->text_capture.original == NULL)
    {
        clear_menu_text_entry_capture(runtime);
        return false;
    }
    (void)menu_control_set_string_value(runtime, control, runtime->text_capture.original);
    return true;
}

bool sdl3d_game_data_menu_text_entry_capture_active(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->text_capture.active;
}

sdl3d_game_data_text_entry_capture_status sdl3d_game_data_update_menu_text_entry_capture(
    sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input)
{
    if (runtime == NULL || !runtime->text_capture.active)
        return SDL3D_GAME_DATA_TEXT_ENTRY_CAPTURE_NONE;

    yyjson_val *item = find_menu_item_at(runtime, runtime->text_capture.menu, runtime->text_capture.item_index);
    yyjson_val *control = obj_get(item, "control");
    if (parse_menu_control_type(json_string(control, "type", NULL)) != SDL3D_GAME_DATA_MENU_CONTROL_TEXT)
    {
        clear_menu_text_entry_capture(runtime);
        return SDL3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CANCELED;
    }

    const scene_menu_state *menu = find_scene_menu_const(active_scene_entry_const(runtime), runtime->text_capture.menu);
    sdl3d_game_data_menu menu_desc;
    SDL_zero(menu_desc);
    menu_desc.select_action_id = -1;
    menu_desc.back_action_id = -1;
    if (menu != NULL)
        (void)sdl3d_game_data_get_active_menu(runtime, &menu_desc);

    const SDL_Scancode scancode = sdl3d_input_get_pressed_scancode(input);
    const SDL_Scancode cancel_key = scancode_from_json(json_string(control, "cancel_key", "ESCAPE"));
    const bool cancel_pressed =
        (cancel_key != SDL_SCANCODE_UNKNOWN && scancode == cancel_key) ||
        (menu_desc.back_action_id >= 0 && sdl3d_input_is_pressed(input, menu_desc.back_action_id));
    if (cancel_pressed)
    {
        (void)menu_control_set_string_value(
            runtime, control, runtime->text_capture.original != NULL ? runtime->text_capture.original : "");
        clear_menu_text_entry_capture(runtime);
        return SDL3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CANCELED;
    }

    const bool submit_pressed =
        scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER ||
        (menu_desc.select_action_id >= 0 && sdl3d_input_is_pressed(input, menu_desc.select_action_id));
    if (submit_pressed)
    {
        clear_menu_text_entry_capture(runtime);
        return SDL3D_GAME_DATA_TEXT_ENTRY_CAPTURE_SUBMITTED;
    }

    char value[256];
    SDL_strlcpy(value, menu_control_string_value(runtime, control), sizeof(value));
    bool changed = false;
    if (scancode == SDL_SCANCODE_BACKSPACE || scancode == SDL_SCANCODE_DELETE)
        changed = menu_text_backspace(value) || changed;
    changed = menu_text_append_filtered(control, value, sizeof(value), sdl3d_input_get_text_input(input)) || changed;
    if (changed)
    {
        (void)menu_control_set_string_value(runtime, control, value);
        return SDL3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CHANGED;
    }
    return SDL3D_GAME_DATA_TEXT_ENTRY_CAPTURE_WAITING;
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

static bool menu_range_is_integer(yyjson_val *control)
{
    return SDL_strcmp(json_string(control, "value_type", ""), "int") == 0 ||
           SDL_strcmp(json_string(control, "value_type", ""), "integer") == 0;
}

static float menu_control_numeric_value(const sdl3d_value *value, yyjson_val *control, float fallback)
{
    if (value != NULL && value->type == SDL3D_VALUE_INT)
        return (float)value->as_int;
    if (value != NULL && value->type == SDL3D_VALUE_FLOAT)
        return value->as_float;
    return json_float(control, "default", fallback);
}

bool sdl3d_game_data_adjust_menu_item_control(sdl3d_game_data_runtime *runtime, const sdl3d_game_data_menu_item *item,
                                              int direction)
{
    if (direction == 0)
        return false;
    if (runtime == NULL || item == NULL || item->control_type == SDL3D_GAME_DATA_MENU_CONTROL_NONE ||
        item->control_target == NULL || item->control_key == NULL)
        return false;

    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, item->control_target);
    if (actor == NULL)
        return false;

    yyjson_val *control = find_menu_item_control_json(runtime, item);
    const int step_direction = direction < 0 ? -1 : 1;
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
        const int choice_count = (int)yyjson_arr_size(choices);
        int next_index =
            current_index >= 0 ? current_index + step_direction : (step_direction < 0 ? choice_count - 1 : 0);
        next_index %= choice_count;
        if (next_index < 0)
            next_index += choice_count;
        yyjson_val *next = yyjson_arr_get(choices, (size_t)next_index);
        return set_property_from_json(actor->props, item->control_key,
                                      yyjson_is_obj(next) ? obj_get(next, "value") : next);
    }
    case SDL3D_GAME_DATA_MENU_CONTROL_RANGE: {
        const float min_value = json_float(control, "min", 0.0f);
        const float max_value = json_float(control, "max", 1.0f);
        const float step = json_float(control, "step", 1.0f);
        const sdl3d_value *current = sdl3d_properties_get_value(actor->props, item->control_key);
        float value = menu_control_numeric_value(current, control, min_value);
        value = SDL_clamp(value + step * (float)step_direction, min_value, max_value);
        if (menu_range_is_integer(control))
            sdl3d_properties_set_int(actor->props, item->control_key, (int)SDL_lroundf(value));
        else
            sdl3d_properties_set_float(actor->props, item->control_key, value);
        return true;
    }
    case SDL3D_GAME_DATA_MENU_CONTROL_NONE:
    case SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING:
    case SDL3D_GAME_DATA_MENU_CONTROL_TEXT:
        return false;
    }
    return false;
}

bool sdl3d_game_data_apply_menu_item_control(sdl3d_game_data_runtime *runtime, const sdl3d_game_data_menu_item *item)
{
    return sdl3d_game_data_adjust_menu_item_control(runtime, item, 1);
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
    if (sdl3d_game_data_menu_input_binding_capture_active(runtime))
        return false;
    if (sdl3d_game_data_menu_text_entry_capture_active(runtime))
        return false;
    sdl3d_game_data_menu menu;
    if (!sdl3d_game_data_get_active_menu(runtime, &menu))
        return true;
    if (input == NULL)
        return false;

    return (menu.up_action_id < 0 || !sdl3d_game_data_active_scene_allows_action(runtime, menu.up_action_id) ||
            !sdl3d_input_is_held(input, menu.up_action_id)) &&
           (menu.down_action_id < 0 || !sdl3d_game_data_active_scene_allows_action(runtime, menu.down_action_id) ||
            !sdl3d_input_is_held(input, menu.down_action_id)) &&
           (menu.left_action_id < 0 || !sdl3d_game_data_active_scene_allows_action(runtime, menu.left_action_id) ||
            !sdl3d_input_is_held(input, menu.left_action_id)) &&
           (menu.right_action_id < 0 || !sdl3d_game_data_active_scene_allows_action(runtime, menu.right_action_id) ||
            !sdl3d_input_is_held(input, menu.right_action_id)) &&
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
    if (type == SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
    {
        if (runtime != NULL && runtime->input_capture.active &&
            item == find_menu_item_at(runtime, runtime->input_capture.menu, runtime->input_capture.item_index))
        {
            SDL_snprintf(buffer, buffer_size, "%s: Press a %s", label,
                         menu_binding_device_input_name(menu_binding_control_device(control)));
            return;
        }
        const menu_binding_device device = menu_binding_control_device(control);
        if (device == MENU_BINDING_DEVICE_GAMEPAD_BUTTON)
        {
            SDL_snprintf(buffer, buffer_size, "%s: %s", label,
                         gamepad_button_display_name(current_input_binding_control_gamepad_button(runtime, control)));
        }
        else if (device == MENU_BINDING_DEVICE_MOUSE_BUTTON)
        {
            SDL_snprintf(buffer, buffer_size, "%s: %s", label,
                         mouse_button_display_name(current_input_binding_control_mouse_button(runtime, control)));
        }
        else
        {
            SDL_snprintf(buffer, buffer_size, "%s: %s", label,
                         scancode_display_name(current_input_binding_control_scancode(runtime, control)));
        }
        return;
    }
    if (type == SDL3D_GAME_DATA_MENU_CONTROL_TEXT)
    {
        char value[256];
        SDL_strlcpy(value, menu_control_string_value(runtime, control), sizeof(value));
        const bool active =
            runtime != NULL && runtime->text_capture.active &&
            item == find_menu_item_at(runtime, runtime->text_capture.menu, runtime->text_capture.item_index);
        const char *placeholder = json_string(control, "placeholder", "");
        const char *cursor = json_string(control, "cursor", "|");
        if (value[0] == '\0' && placeholder != NULL && placeholder[0] != '\0' && !active)
            SDL_snprintf(buffer, buffer_size, "%s: %s", label, placeholder);
        else if (active)
            SDL_snprintf(buffer, buffer_size, "%s: %s%s", label, value, cursor != NULL ? cursor : "|");
        else
            SDL_snprintf(buffer, buffer_size, "%s: %s", label, value);
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
        const float min_value = json_float(control, "min", 0.0f);
        const float max_value = json_float(control, "max", 1.0f);
        const float current = SDL_clamp(menu_control_numeric_value(value, control, min_value), min_value, max_value);
        if (SDL_strcmp(json_string(control, "display", ""), "slider") == 0)
        {
            const int slots = SDL_max(1, json_int(control, "slots", 10));
            const float t = max_value > min_value ? (current - min_value) / (max_value - min_value) : 0.0f;
            int filled = (int)SDL_lroundf(SDL_clamp(t, 0.0f, 1.0f) * (float)slots);
            filled = SDL_clamp(filled, 0, slots);
            const char left = first_json_string_char(control, "slider_left", '[');
            const char fill = first_json_string_char(control, "slider_fill", '#');
            const char empty = first_json_string_char(control, "slider_empty", '-');
            const char right = first_json_string_char(control, "slider_right", ']');
            char slider[64];
            size_t pos = 0;
            slider[pos++] = left;
            for (int i = 0; i < slots && pos + 2 < sizeof(slider); ++i)
                slider[pos++] = i < filled ? fill : empty;
            slider[pos++] = right;
            slider[pos] = '\0';
            if (menu_range_is_integer(control))
                SDL_snprintf(buffer, buffer_size, "%s  %s %d/%d", label, slider, (int)SDL_lroundf(current),
                             (int)SDL_lroundf(max_value));
            else
                SDL_snprintf(buffer, buffer_size, "%s  %s %.2g", label, slider, current);
            return;
        }
        if (menu_range_is_integer(control))
            SDL_snprintf(buffer, buffer_size, "%s: %d", label, (int)SDL_lroundf(current));
        else
            SDL_snprintf(buffer, buffer_size, "%s: %.2g", label, current);
        return;
    }
    SDL_strlcpy(buffer, label, buffer_size);
}

static bool for_each_ui_menu_presenter(const sdl3d_game_data_runtime *runtime, const scene_entry *scene,
                                       const sdl3d_game_data_ui_metrics *metrics, yyjson_val *presenter,
                                       sdl3d_game_data_ui_text_fn callback, void *userdata)
{
    if (scene == NULL || presenter == NULL)
        return true;

    const scene_menu_state *menu_state = find_scene_menu_const(scene, json_string(presenter, "menu", NULL));
    if (menu_state == NULL)
        return true;
    const int item_count = menu_runtime_item_count(runtime, menu_state);
    if (item_count <= 0)
        return true;

    yyjson_val *presenter_active_if = obj_get(presenter, "active_if");
    if (presenter_active_if != NULL && !eval_data_condition(runtime, presenter_active_if, metrics))
        return true;
    yyjson_val *menu_active_if = obj_get(menu_state->menu, "active_if");
    if (menu_active_if != NULL && !eval_data_condition(runtime, menu_active_if, metrics))
        return true;

    const float x = json_float(presenter, "x", 0.0f);
    const float y = json_float(presenter, "y", 0.0f);
    const float gap = json_float(presenter, "gap", json_bool(presenter, "normalized", false) ? 0.08f : 42.0f);
    const bool normalized = json_bool(presenter, "normalized", false);
    const sdl3d_game_data_ui_align align =
        parse_ui_align(json_string(presenter, "align", NULL), SDL3D_GAME_DATA_UI_ALIGN_CENTER);
    const int selected_index = SDL_clamp(menu_state->selected_index, 0, item_count - 1);
    const int visible_count = json_int(presenter, "visible_count", item_count);
    int first_index = 0;
    int last_index = item_count;
    if (visible_count > 0 && visible_count < item_count)
    {
        first_index = selected_index - visible_count / 2;
        first_index = SDL_clamp(first_index, 0, item_count - visible_count);
        last_index = first_index + visible_count;
    }

    for (int i = first_index; i < last_index; ++i)
    {
        yyjson_val *item = NULL;
        int dynamic_index = -1;
        bool dynamic_empty = false;
        if (!resolve_menu_item_at(runtime, menu_state, i, &item, &dynamic_index, &dynamic_empty))
            continue;
        const bool selected = i == selected_index;
        const float row_y = y + gap * (float)(i - first_index);

        if (selected && !emit_ui_menu_cursor(presenter, row_y, callback, userdata))
            return false;

        sdl3d_game_data_ui_text text;
        char label[128];
        if (menu_item_is_dynamic_list(item))
            (void)menu_dynamic_list_label(runtime, item, dynamic_index, dynamic_empty, label, sizeof(label));
        else
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

static bool for_each_ui_menu_root(const sdl3d_game_data_runtime *runtime, const scene_entry *scene,
                                  const sdl3d_game_data_ui_metrics *metrics, yyjson_val *root,
                                  sdl3d_game_data_ui_text_fn callback, void *userdata)
{
    yyjson_val *menus = obj_get(obj_get(root, "ui"), "menus");
    for (size_t i = 0; yyjson_is_arr(menus) && i < yyjson_arr_size(menus); ++i)
    {
        if (!for_each_ui_menu_presenter(runtime, scene, metrics, yyjson_arr_get(menus, i), callback, userdata))
            return false;
    }
    return true;
}

bool sdl3d_game_data_for_each_ui_text_for_metrics(const sdl3d_game_data_runtime *runtime,
                                                  const sdl3d_game_data_ui_metrics *metrics,
                                                  sdl3d_game_data_ui_text_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    yyjson_val *roots[2];
    roots[0] = runtime_root(runtime);
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;

    for (int root_index = 0; root_index < 2; ++root_index)
        if (!for_each_authored_ui_text_root(roots[root_index], callback, userdata) ||
            !for_each_ui_menu_root(runtime, scene, metrics, roots[root_index], callback, userdata))
            return true;
    return true;
}

bool sdl3d_game_data_for_each_ui_text(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_ui_text_fn callback,
                                      void *userdata)
{
    return sdl3d_game_data_for_each_ui_text_for_metrics(runtime, NULL, callback, userdata);
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
    image->effect = parse_ui_image_effect(json_string(item, "effect", NULL));
    image->effect_speed = json_float(item, "effect_speed", 1.0f);
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

static bool format_payload_string(const sdl3d_properties *payload, const char *format, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    buffer[0] = '\0';
    if (format == NULL)
        return true;
    if (payload == NULL || SDL_strchr(format, '{') == NULL)
    {
        SDL_strlcpy(buffer, format, buffer_size);
        return SDL_strlen(format) < buffer_size;
    }

    size_t offset = 0U;
    const char *cursor = format;
    while (*cursor != '\0' && offset + 1U < buffer_size)
    {
        const char *open = SDL_strchr(cursor, '{');
        if (open == NULL)
        {
            const size_t remaining = buffer_size - offset;
            const size_t copied = SDL_strlcpy(buffer + offset, cursor, remaining);
            offset += SDL_min(copied, remaining > 0U ? remaining - 1U : 0U);
            if (copied < remaining)
                cursor += copied;
            break;
        }

        const size_t literal_len = (size_t)(open - cursor);
        const size_t literal_copy = SDL_min(literal_len, buffer_size - offset - 1U);
        SDL_memcpy(buffer + offset, cursor, literal_copy);
        offset += literal_copy;
        buffer[offset] = '\0';
        if (literal_copy < literal_len)
            break;

        const char *close = SDL_strchr(open + 1, '}');
        if (close == NULL)
        {
            const size_t remaining = buffer_size - offset;
            const size_t copied = SDL_strlcpy(buffer + offset, open, remaining);
            offset += SDL_min(copied, remaining > 0U ? remaining - 1U : 0U);
            if (copied < remaining)
                cursor = open + copied;
            break;
        }

        char key[64];
        const size_t key_len = (size_t)(close - open - 1);
        if (key_len > 0U && key_len < sizeof(key))
        {
            SDL_memcpy(key, open + 1, key_len);
            key[key_len] = '\0';
            const char *replacement = sdl3d_properties_get_string(payload, key, "");
            const size_t remaining = buffer_size - offset;
            const size_t copied = SDL_strlcpy(buffer + offset, replacement, remaining);
            offset += SDL_min(copied, remaining > 0U ? remaining - 1U : 0U);
        }
        cursor = close + 1;
    }
    buffer[buffer_size - 1U] = '\0';
    return cursor[0] == '\0';
}

static bool set_property_from_json_with_payload(sdl3d_properties *props, const char *key, yyjson_val *value,
                                                const sdl3d_properties *payload)
{
    if (props == NULL || key == NULL || value == NULL)
        return false;
    if (yyjson_is_str(value))
    {
        char formatted[256];
        if (!format_payload_string(payload, yyjson_get_str(value), formatted, sizeof(formatted)))
            return false;
        sdl3d_properties_set_string(props, key, formatted);
        return true;
    }
    return set_property_from_json(props, key, value);
}

static sdl3d_properties *properties_from_json_payload(yyjson_val *json, const sdl3d_properties *source_payload)
{
    sdl3d_properties *payload = sdl3d_properties_create();
    if (payload == NULL)
        return NULL;
    if (json == NULL)
        return payload;
    if (!yyjson_is_obj(json))
    {
        sdl3d_properties_destroy(payload);
        return NULL;
    }

    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(json, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        value = yyjson_obj_iter_get_val(key);
        if (name == NULL || name[0] == '\0' ||
            !set_property_from_json_with_payload(payload, name, value, source_payload))
        {
            sdl3d_properties_destroy(payload);
            return NULL;
        }
    }
    return payload;
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

static void initialize_actor_from_json(sdl3d_registered_actor *actor, yyjson_val *entity, const char *classname,
                                       bool active)
{
    if (actor == NULL)
        return;

    actor->active = active;
    sdl3d_properties_clear(actor->props);
    sdl3d_properties_set_string(actor->props, "classname", classname != NULL ? classname : actor->name);

    yyjson_val *transform = obj_get(entity, "transform");
    actor_set_position(actor, json_vec3(transform, "position", sdl3d_vec3_make(0.0f, 0.0f, 0.0f)));
    load_actor_properties(actor, obj_get(entity, "properties"));
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
        initialize_actor_from_json(actor, entity, name, json_bool(entity, "active", true));
    }

    return true;
}

static yyjson_val *find_actor_archetype_json(yyjson_val *root, const char *name)
{
    yyjson_val *archetypes = obj_get(root, "actor_archetypes");
    for (size_t i = 0; yyjson_is_arr(archetypes) && i < yyjson_arr_size(archetypes); ++i)
    {
        yyjson_val *archetype = yyjson_arr_get(archetypes, i);
        const char *archetype_name = json_string(archetype, "name", NULL);
        if (archetype_name != NULL && name != NULL && SDL_strcmp(archetype_name, name) == 0)
            return archetype;
    }
    return NULL;
}

static actor_pool_exhaustion_policy actor_pool_exhaustion_from_string(const char *value)
{
    if (value != NULL && SDL_strcmp(value, "reuse_oldest") == 0)
        return ACTOR_POOL_EXHAUST_REUSE_OLDEST;
    return ACTOR_POOL_EXHAUST_FAIL;
}

static const char *actor_lifecycle_state_name(actor_lifecycle_state state)
{
    switch (state)
    {
    case ACTOR_LIFECYCLE_SPAWNING:
        return "spawning";
    case ACTOR_LIFECYCLE_ACTIVE:
        return "active";
    case ACTOR_LIFECYCLE_DESPAWNING:
        return "despawning";
    case ACTOR_LIFECYCLE_INACTIVE:
    default:
        return "inactive";
    }
}

static actor_lifecycle_state actor_pool_lifecycle_state(const actor_pool_runtime *pool, int index)
{
    if (pool == NULL || index < 0 || index >= pool->capacity || pool->lifecycle_states == NULL)
        return ACTOR_LIFECYCLE_INACTIVE;
    return pool->lifecycle_states[index];
}

static actor_pool_scene_exit_policy actor_pool_scene_exit_policy_from_string(const char *policy,
                                                                             actor_pool_scene_exit_policy fallback)
{
    if (policy == NULL)
        return fallback;
    if (SDL_strcmp(policy, "reset") == 0)
        return ACTOR_POOL_SCENE_EXIT_RESET;
    if (SDL_strcmp(policy, "despawn") == 0)
        return ACTOR_POOL_SCENE_EXIT_DESPAWN;
    if (SDL_strcmp(policy, "preserve") == 0)
        return ACTOR_POOL_SCENE_EXIT_PRESERVE;
    return fallback;
}

static bool actor_pool_in_scene(const actor_pool_runtime *pool, const char *scene_name)
{
    if (pool == NULL || scene_name == NULL)
        return false;
    for (int i = 0; i < pool->scene_count; ++i)
    {
        if (pool->scenes[i] != NULL && SDL_strcmp(pool->scenes[i], scene_name) == 0)
            return true;
    }
    return false;
}

static void actor_pool_set_lifecycle_state(actor_pool_runtime *pool, sdl3d_registered_actor *actor, int index,
                                           actor_lifecycle_state state)
{
    if (pool == NULL || actor == NULL || index < 0 || index >= pool->capacity)
        return;
    if (pool->lifecycle_states != NULL)
        pool->lifecycle_states[index] = state;
    sdl3d_properties_set_string(actor->props, "pool_lifecycle", actor_lifecycle_state_name(state));
}

static bool actor_pool_actor_is_active(const actor_pool_runtime *pool, const sdl3d_registered_actor *actor, int index)
{
    return actor != NULL && actor->active &&
           (pool == NULL || pool->lifecycle_states == NULL ||
            actor_pool_lifecycle_state(pool, index) == ACTOR_LIFECYCLE_ACTIVE);
}

static bool actor_pool_actor_is_available(const actor_pool_runtime *pool, const sdl3d_registered_actor *actor,
                                          int index)
{
    return actor != NULL && !actor->active &&
           (pool == NULL || pool->lifecycle_states == NULL ||
            actor_pool_lifecycle_state(pool, index) == ACTOR_LIFECYCLE_INACTIVE);
}

static void actor_lifecycle_defer_begin(sdl3d_game_data_runtime *runtime)
{
    if (runtime != NULL)
        runtime->actor_lifecycle_defer_depth++;
}

static void actor_lifecycle_flush(sdl3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->actor_lifecycle_defer_depth > 0 || !runtime->actor_lifecycle_flush_pending)
        return;

    runtime->actor_lifecycle_flush_pending = false;
    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            if (actor_pool_lifecycle_state(pool, actor_index) != ACTOR_LIFECYCLE_DESPAWNING)
                continue;
            sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor != NULL)
                (void)initialize_pooled_actor(pool, actor, actor_index, false);
        }
    }
}

static void actor_lifecycle_defer_end(sdl3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->actor_lifecycle_defer_depth <= 0)
        return;
    runtime->actor_lifecycle_defer_depth--;
    actor_lifecycle_flush(runtime);
}

static bool actor_pool_initialize_slot(sdl3d_game_data_runtime *runtime, actor_pool_runtime *pool, int index,
                                       bool active)
{
    if (runtime == NULL || pool == NULL || index < 0 || index >= pool->capacity)
        return false;
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[index]);
    if (actor == NULL || !initialize_pooled_actor(pool, actor, index, active))
        return false;
    if (active && pool->spawn_generations != NULL)
    {
        pool->spawn_generations[index] = ++pool->spawn_generation_counter;
        sdl3d_properties_set_int(actor->props, "pool_spawn_generation",
                                 (int)SDL_min(pool->spawn_generations[index], (Uint64)SDL_MAX_SINT32));
    }
    return true;
}

static bool initialize_pooled_actor(actor_pool_runtime *pool, sdl3d_registered_actor *actor, int index, bool active)
{
    if (pool == NULL || actor == NULL || index < 0 || index >= pool->capacity)
        return false;
    initialize_actor_from_json(actor, pool->archetype_json, pool->archetype, active);
    sdl3d_properties_set_string(actor->props, "pool", pool->name);
    sdl3d_properties_set_int(actor->props, "pool_index", index);
    if (pool->scene != NULL)
        sdl3d_properties_set_string(actor->props, "pool_scene", pool->scene);
    if (!active && pool->spawn_generations != NULL)
        pool->spawn_generations[index] = 0U;
    actor_pool_set_lifecycle_state(pool, actor, index, active ? ACTOR_LIFECYCLE_ACTIVE : ACTOR_LIFECYCLE_INACTIVE);
    return true;
}

static bool apply_actor_pool_scene_exit_policies(sdl3d_game_data_runtime *runtime, const char *from_scene,
                                                 const char *to_scene)
{
    if (runtime == NULL || from_scene == NULL)
        return true;

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, from_scene) || actor_pool_in_scene(pool, to_scene))
            continue;
        if (pool->scene_exit_policy == ACTOR_POOL_SCENE_EXIT_PRESERVE)
            continue;

        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor == NULL)
                return false;
            if (pool->scene_exit_policy == ACTOR_POOL_SCENE_EXIT_DESPAWN)
            {
                if (actor_pool_lifecycle_state(pool, actor_index) != ACTOR_LIFECYCLE_INACTIVE &&
                    !actor_pool_request_despawn(runtime, pool, actor, actor_index))
                {
                    return false;
                }
                continue;
            }
            if (!actor_pool_initialize_slot(runtime, pool, actor_index, pool->initial_active))
                return false;
        }
    }
    return true;
}

static bool load_actor_pools(sdl3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                             int error_buffer_size)
{
    sdl3d_actor_registry *registry = runtime_registry(runtime);
    yyjson_val *pools = obj_get(root, "actor_pools");
    if (!yyjson_is_arr(pools))
        return true;
    if (registry == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data actor pools require an actor registry");
        return false;
    }

    runtime->actor_pool_count = (int)yyjson_arr_size(pools);
    runtime->actor_pools =
        (actor_pool_runtime *)SDL_calloc((size_t)runtime->actor_pool_count, sizeof(*runtime->actor_pools));
    if (runtime->actor_pools == NULL && runtime->actor_pool_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate actor pools");
        return false;
    }

    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        yyjson_val *pool_json = yyjson_arr_get(pools, (size_t)i);
        actor_pool_runtime *pool = &runtime->actor_pools[i];
        const char *name = json_string(pool_json, "name", NULL);
        const char *archetype = json_string(pool_json, "archetype", NULL);
        const int capacity = json_int(pool_json, "capacity", 0);
        yyjson_val *archetype_json = find_actor_archetype_json(root, archetype);
        if (name == NULL || archetype == NULL || capacity <= 0 || archetype_json == NULL)
        {
            set_error(error_buffer, error_buffer_size, "invalid actor pool declaration");
            return false;
        }

        pool->name = SDL_strdup(name);
        pool->archetype = archetype;
        yyjson_val *scene_list = obj_get(pool_json, "scenes");
        const char *single_scene = json_string(pool_json, "scene", NULL);
        if (yyjson_is_arr(scene_list))
        {
            pool->scene_count = (int)yyjson_arr_size(scene_list);
            pool->scenes = (const char **)SDL_calloc((size_t)pool->scene_count, sizeof(*pool->scenes));
            if (pool->scenes == NULL && pool->scene_count > 0)
            {
                set_error(error_buffer, error_buffer_size, "failed to allocate actor pool scenes");
                return false;
            }
            for (int scene_index = 0; scene_index < pool->scene_count; ++scene_index)
            {
                yyjson_val *scene_value = yyjson_arr_get(scene_list, (size_t)scene_index);
                if (!yyjson_is_str(scene_value) || yyjson_get_str(scene_value)[0] == '\0')
                {
                    set_error(error_buffer, error_buffer_size, "invalid actor pool scene declaration");
                    return false;
                }
                pool->scenes[scene_index] = yyjson_get_str(scene_value);
            }
        }
        else if (single_scene != NULL)
        {
            pool->scene_count = 1;
            pool->scenes = (const char **)SDL_calloc(1U, sizeof(*pool->scenes));
            if (pool->scenes == NULL)
            {
                set_error(error_buffer, error_buffer_size, "failed to allocate actor pool scenes");
                return false;
            }
            pool->scenes[0] = single_scene;
        }
        pool->scene = pool->scene_count > 0 ? pool->scenes[0] : NULL;
        pool->archetype_json = archetype_json;
        pool->capacity = capacity;
        pool->initial_active = json_bool(pool_json, "initial_active", false);
        pool->exhaustion = actor_pool_exhaustion_from_string(json_string(pool_json, "on_exhausted", "fail"));
        pool->scene_exit_policy = actor_pool_scene_exit_policy_from_string(
            json_string(pool_json, "on_scene_exit", NULL),
            pool->scene_count > 0 ? ACTOR_POOL_SCENE_EXIT_RESET : ACTOR_POOL_SCENE_EXIT_PRESERVE);
        pool->actor_names = (char **)SDL_calloc((size_t)capacity, sizeof(*pool->actor_names));
        pool->spawn_generations = (Uint64 *)SDL_calloc((size_t)capacity, sizeof(*pool->spawn_generations));
        pool->lifecycle_states = (actor_lifecycle_state *)SDL_calloc((size_t)capacity, sizeof(*pool->lifecycle_states));
        if (pool->name == NULL || pool->actor_names == NULL || pool->spawn_generations == NULL ||
            pool->lifecycle_states == NULL)
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate actor pool entries");
            return false;
        }

        for (int actor_index = 0; actor_index < capacity; ++actor_index)
        {
            char actor_name[256];
            SDL_snprintf(actor_name, sizeof(actor_name), "%s.%d", name, actor_index);
            sdl3d_registered_actor *actor = sdl3d_actor_registry_add(registry, actor_name);
            if (actor == NULL)
            {
                set_error(error_buffer, error_buffer_size, "failed to register pooled actor");
                return false;
            }
            pool->actor_names[actor_index] = SDL_strdup(actor_name);
            if (pool->actor_names[actor_index] == NULL ||
                !initialize_pooled_actor(pool, actor, actor_index, pool->initial_active))
            {
                set_error(error_buffer, error_buffer_size, "failed to initialize pooled actor");
                return false;
            }
            if (pool->initial_active)
            {
                pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
                sdl3d_properties_set_int(actor->props, "pool_spawn_generation",
                                         (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
            }
        }
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

static bool append_input_binding_spec(sdl3d_game_data_runtime *runtime, const input_binding_spec *spec)
{
    if (runtime == NULL || spec == NULL)
        return false;
    if (runtime->input_binding_count >= runtime->input_binding_capacity)
    {
        const int next_capacity = runtime->input_binding_capacity > 0 ? runtime->input_binding_capacity * 2 : 32;
        input_binding_spec *next = (input_binding_spec *)SDL_realloc(
            runtime->input_bindings, (size_t)next_capacity * sizeof(*runtime->input_bindings));
        if (next == NULL)
            return false;
        SDL_memset(next + runtime->input_binding_capacity, 0,
                   (size_t)(next_capacity - runtime->input_binding_capacity) * sizeof(*runtime->input_bindings));
        runtime->input_bindings = next;
        runtime->input_binding_capacity = next_capacity;
    }
    runtime->input_bindings[runtime->input_binding_count++] = *spec;
    return true;
}

static void bind_input_spec(sdl3d_input_manager *input, const input_binding_spec *spec)
{
    if (input == NULL || spec == NULL || spec->action_id < 0)
        return;

    switch (spec->source)
    {
    case SDL3D_INPUT_KEYBOARD:
        sdl3d_input_bind_key_mod_mask(input, spec->action_id, spec->scancode, spec->required_modifiers,
                                      spec->excluded_modifiers);
        break;
    case SDL3D_INPUT_MOUSE_BUTTON:
        sdl3d_input_bind_mouse_button(input, spec->action_id, spec->mouse_button);
        break;
    case SDL3D_INPUT_MOUSE_AXIS:
        sdl3d_input_bind_mouse_axis(input, spec->action_id, spec->mouse_axis, spec->scale);
        break;
    case SDL3D_INPUT_GAMEPAD_BUTTON:
        sdl3d_input_bind_gamepad_button_at(input, spec->action_id, spec->gamepad_index, spec->gamepad_button);
        break;
    case SDL3D_INPUT_GAMEPAD_AXIS:
        sdl3d_input_bind_gamepad_axis_at(input, spec->action_id, spec->gamepad_index, spec->gamepad_axis, spec->scale);
        break;
    }
}

static void rebind_action_from_specs(sdl3d_game_data_runtime *runtime, int action_id)
{
    sdl3d_input_manager *input = runtime_input(runtime);
    if (input == NULL || action_id < 0)
        return;

    sdl3d_input_unbind_action(input, action_id);
    for (int i = 0; i < runtime->input_binding_count; ++i)
    {
        if (runtime->input_bindings[i].action_id == action_id)
            bind_input_spec(input, &runtime->input_bindings[i]);
    }
}

static bool set_action_keyboard_binding(sdl3d_game_data_runtime *runtime, const char *action, SDL_Scancode scancode)
{
    const int action_id = sdl3d_game_data_find_action(runtime, action);
    if (runtime == NULL || action == NULL || action_id < 0 || scancode == SDL_SCANCODE_UNKNOWN)
        return false;

    for (int i = 0; i < runtime->input_binding_count;)
    {
        input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SDL3D_INPUT_KEYBOARD)
        {
            *spec = runtime->input_bindings[runtime->input_binding_count - 1];
            runtime->input_binding_count--;
        }
        else
        {
            i++;
        }
    }

    input_binding_spec spec;
    SDL_zero(spec);
    spec.action = action;
    spec.action_id = action_id;
    spec.source = SDL3D_INPUT_KEYBOARD;
    spec.gamepad_index = -1;
    spec.scancode = scancode;
    spec.scale = 1.0f;
    if (!append_input_binding_spec(runtime, &spec))
        return false;
    rebind_action_from_specs(runtime, action_id);
    return true;
}

static bool set_action_mouse_button_binding(sdl3d_game_data_runtime *runtime, const char *action, Uint8 button)
{
    const int action_id = sdl3d_game_data_find_action(runtime, action);
    if (runtime == NULL || action == NULL || action_id < 0 || button == 0)
        return false;

    for (int i = 0; i < runtime->input_binding_count;)
    {
        input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SDL3D_INPUT_MOUSE_BUTTON)
        {
            *spec = runtime->input_bindings[runtime->input_binding_count - 1];
            runtime->input_binding_count--;
        }
        else
        {
            i++;
        }
    }

    input_binding_spec spec;
    SDL_zero(spec);
    spec.action = action;
    spec.action_id = action_id;
    spec.source = SDL3D_INPUT_MOUSE_BUTTON;
    spec.gamepad_index = -1;
    spec.mouse_button = button;
    spec.scale = 1.0f;
    if (!append_input_binding_spec(runtime, &spec))
        return false;
    rebind_action_from_specs(runtime, action_id);
    return true;
}

static bool set_action_gamepad_button_binding(sdl3d_game_data_runtime *runtime, const char *action,
                                              SDL_GamepadButton button)
{
    const int action_id = sdl3d_game_data_find_action(runtime, action);
    if (runtime == NULL || action == NULL || action_id < 0 || button == SDL_GAMEPAD_BUTTON_INVALID)
        return false;

    for (int i = 0; i < runtime->input_binding_count;)
    {
        input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SDL3D_INPUT_GAMEPAD_BUTTON)
        {
            *spec = runtime->input_bindings[runtime->input_binding_count - 1];
            runtime->input_binding_count--;
        }
        else
        {
            i++;
        }
    }

    input_binding_spec spec;
    SDL_zero(spec);
    spec.action = action;
    spec.action_id = action_id;
    spec.source = SDL3D_INPUT_GAMEPAD_BUTTON;
    spec.gamepad_index = -1;
    spec.gamepad_button = button;
    spec.scale = 1.0f;
    if (!append_input_binding_spec(runtime, &spec))
        return false;
    rebind_action_from_specs(runtime, action_id);
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
                    {
                        input_binding_spec spec;
                        SDL_zero(spec);
                        spec.action = name;
                        spec.action_id = action_id;
                        spec.source = SDL3D_INPUT_KEYBOARD;
                        spec.gamepad_index = -1;
                        spec.scancode = code;
                        spec.scale = 1.0f;
                        if (!append_input_binding_spec(runtime, &spec))
                            return false;
                        bind_input_spec(input, &spec);
                    }
                }
                else if (SDL_strcmp(device, "mouse") == 0)
                {
                    const char *axis = json_string(binding, "axis", NULL);
                    const char *button = json_string(binding, "button", NULL);
                    if (axis != NULL)
                    {
                        bool valid_axis = false;
                        const sdl3d_mouse_axis mouse_axis = mouse_axis_from_json(axis, &valid_axis);
                        if (valid_axis)
                        {
                            input_binding_spec spec;
                            SDL_zero(spec);
                            spec.action = name;
                            spec.action_id = action_id;
                            spec.source = SDL3D_INPUT_MOUSE_AXIS;
                            spec.gamepad_index = -1;
                            spec.mouse_axis = mouse_axis;
                            spec.scale = scale;
                            if (!append_input_binding_spec(runtime, &spec))
                                return false;
                            bind_input_spec(input, &spec);
                        }
                    }
                    else if (button != NULL)
                    {
                        const Uint8 mouse_button = mouse_button_from_json(button);
                        if (mouse_button != 0)
                        {
                            input_binding_spec spec;
                            SDL_zero(spec);
                            spec.action = name;
                            spec.action_id = action_id;
                            spec.source = SDL3D_INPUT_MOUSE_BUTTON;
                            spec.gamepad_index = -1;
                            spec.mouse_button = mouse_button;
                            spec.scale = 1.0f;
                            if (!append_input_binding_spec(runtime, &spec))
                                return false;
                            bind_input_spec(input, &spec);
                        }
                    }
                }
                else if (SDL_strcmp(device, "gamepad") == 0)
                {
                    const char *axis = json_string(binding, "axis", NULL);
                    const char *button = json_string(binding, "button", NULL);
                    if (axis != NULL)
                    {
                        const SDL_GamepadAxis gamepad_axis = gamepad_axis_from_json(axis);
                        if (gamepad_axis != SDL_GAMEPAD_AXIS_INVALID)
                        {
                            input_binding_spec spec;
                            SDL_zero(spec);
                            spec.action = name;
                            spec.action_id = action_id;
                            spec.source = SDL3D_INPUT_GAMEPAD_AXIS;
                            spec.gamepad_index = json_int(binding, "slot", -1);
                            spec.gamepad_axis = gamepad_axis;
                            spec.scale = scale;
                            if (!append_input_binding_spec(runtime, &spec))
                                return false;
                            bind_input_spec(input, &spec);
                        }
                    }
                    else if (button != NULL)
                    {
                        const SDL_GamepadButton gamepad_button = gamepad_button_from_json(button);
                        if (gamepad_button != SDL_GAMEPAD_BUTTON_INVALID)
                        {
                            input_binding_spec spec;
                            SDL_zero(spec);
                            spec.action = name;
                            spec.action_id = action_id;
                            spec.source = SDL3D_INPUT_GAMEPAD_BUTTON;
                            spec.gamepad_index = json_int(binding, "slot", -1);
                            spec.gamepad_button = gamepad_button;
                            spec.scale = 1.0f;
                            if (!append_input_binding_spec(runtime, &spec))
                                return false;
                            bind_input_spec(input, &spec);
                        }
                    }
                }
            }
        }
    }
    return true;
}

static yyjson_val *find_input_profile_json(const sdl3d_game_data_runtime *runtime, const char *profile_name)
{
    yyjson_val *profiles = obj_get(obj_get(runtime_root(runtime), "input"), "profiles");
    for (size_t i = 0; profile_name != NULL && yyjson_is_arr(profiles) && i < yyjson_arr_size(profiles); ++i)
    {
        yyjson_val *profile = yyjson_arr_get(profiles, i);
        const char *name = json_string(profile, "name", NULL);
        if (name != NULL && SDL_strcmp(name, profile_name) == 0)
            return profile;
    }
    return NULL;
}

static bool input_profile_binding_to_spec(const sdl3d_game_data_runtime *runtime, yyjson_val *binding,
                                          input_binding_spec *out_spec, char *error_buffer, int error_buffer_size)
{
    const char *action = json_string(binding, "action", NULL);
    const char *device = json_string(binding, "device", "");
    const int action_id = sdl3d_game_data_find_action(runtime, action);
    const float scale = json_float(binding, "scale", 1.0f);
    if (binding == NULL || out_spec == NULL || action == NULL || action_id < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "input profile binding references unknown action '%s'",
                   action != NULL ? action : "<missing>");
        return false;
    }

    SDL_zero(*out_spec);
    out_spec->action = action;
    out_spec->action_id = action_id;
    out_spec->gamepad_index = -1;
    out_spec->scale = scale;

    if (SDL_strcmp(device, "keyboard") == 0)
    {
        const SDL_Scancode code = scancode_from_json(json_string(binding, "key", NULL));
        if (code == SDL_SCANCODE_UNKNOWN)
        {
            set_error(error_buffer, error_buffer_size, "input profile keyboard binding has an invalid key");
            return false;
        }
        out_spec->source = SDL3D_INPUT_KEYBOARD;
        out_spec->scancode = code;
        out_spec->scale = 1.0f;
        return true;
    }
    if (SDL_strcmp(device, "mouse") == 0)
    {
        const char *axis = json_string(binding, "axis", NULL);
        const char *button = json_string(binding, "button", NULL);
        if (axis != NULL)
        {
            bool valid_axis = false;
            out_spec->mouse_axis = mouse_axis_from_json(axis, &valid_axis);
            if (!valid_axis)
            {
                set_error(error_buffer, error_buffer_size, "input profile mouse binding has an invalid axis");
                return false;
            }
            out_spec->source = SDL3D_INPUT_MOUSE_AXIS;
            return true;
        }
        out_spec->mouse_button = mouse_button_from_json(button);
        if (out_spec->mouse_button == 0)
        {
            set_error(error_buffer, error_buffer_size, "input profile mouse binding has an invalid button");
            return false;
        }
        out_spec->source = SDL3D_INPUT_MOUSE_BUTTON;
        out_spec->scale = 1.0f;
        return true;
    }
    if (SDL_strcmp(device, "gamepad") == 0)
    {
        const char *axis = json_string(binding, "axis", NULL);
        const char *button = json_string(binding, "button", NULL);
        out_spec->gamepad_index = json_int(binding, "slot", -1);
        if (axis != NULL)
        {
            out_spec->gamepad_axis = gamepad_axis_from_json(axis);
            if (out_spec->gamepad_axis == SDL_GAMEPAD_AXIS_INVALID)
            {
                set_error(error_buffer, error_buffer_size, "input profile gamepad binding has an invalid axis");
                return false;
            }
            out_spec->source = SDL3D_INPUT_GAMEPAD_AXIS;
            return true;
        }
        out_spec->gamepad_button = gamepad_button_from_json(button);
        if (out_spec->gamepad_button == SDL_GAMEPAD_BUTTON_INVALID)
        {
            set_error(error_buffer, error_buffer_size, "input profile gamepad binding has an invalid button");
            return false;
        }
        out_spec->source = SDL3D_INPUT_GAMEPAD_BUTTON;
        out_spec->scale = 1.0f;
        return true;
    }

    set_errorf(error_buffer, error_buffer_size, "input profile binding uses unsupported device '%s'",
               device != NULL ? device : "<missing>");
    return false;
}

static bool input_assignment_binding_to_spec(const sdl3d_game_data_runtime *runtime, const char *device,
                                             yyjson_val *binding, const char *action, int gamepad_slot,
                                             input_binding_spec *out_spec, char *error_buffer, int error_buffer_size)
{
    const int action_id = sdl3d_game_data_find_action(runtime, action);
    const float scale = json_float(binding, "scale", 1.0f);
    if (runtime == NULL || binding == NULL || out_spec == NULL || action == NULL || action_id < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "input assignment references unknown action '%s'",
                   action != NULL ? action : "<missing>");
        return false;
    }

    SDL_zero(*out_spec);
    out_spec->action = action;
    out_spec->action_id = action_id;
    out_spec->gamepad_index = -1;
    out_spec->scale = scale;

    if (SDL_strcmp(device != NULL ? device : "", "keyboard") == 0)
    {
        const SDL_Scancode code = scancode_from_json(json_string(binding, "key", NULL));
        if (code == SDL_SCANCODE_UNKNOWN)
        {
            set_error(error_buffer, error_buffer_size, "input assignment keyboard binding has an invalid key");
            return false;
        }
        out_spec->source = SDL3D_INPUT_KEYBOARD;
        out_spec->scancode = code;
        out_spec->scale = 1.0f;
        return true;
    }

    if (SDL_strcmp(device != NULL ? device : "", "mouse") == 0)
    {
        const char *axis = json_string(binding, "axis", NULL);
        const char *button = json_string(binding, "button", NULL);
        if (axis != NULL)
        {
            bool valid_axis = false;
            out_spec->mouse_axis = mouse_axis_from_json(axis, &valid_axis);
            if (!valid_axis)
            {
                set_error(error_buffer, error_buffer_size, "input assignment mouse binding has an invalid axis");
                return false;
            }
            out_spec->source = SDL3D_INPUT_MOUSE_AXIS;
            return true;
        }
        out_spec->mouse_button = mouse_button_from_json(button);
        if (out_spec->mouse_button == 0)
        {
            set_error(error_buffer, error_buffer_size, "input assignment mouse binding has an invalid button");
            return false;
        }
        out_spec->source = SDL3D_INPUT_MOUSE_BUTTON;
        out_spec->scale = 1.0f;
        return true;
    }

    if (SDL_strcmp(device != NULL ? device : "", "gamepad") == 0)
    {
        const char *axis = json_string(binding, "axis", NULL);
        const char *button = json_string(binding, "button", NULL);
        out_spec->gamepad_index = gamepad_slot;
        if (axis != NULL)
        {
            out_spec->gamepad_axis = gamepad_axis_from_json(axis);
            if (out_spec->gamepad_axis == SDL_GAMEPAD_AXIS_INVALID)
            {
                set_error(error_buffer, error_buffer_size, "input assignment gamepad binding has an invalid axis");
                return false;
            }
            out_spec->source = SDL3D_INPUT_GAMEPAD_AXIS;
            return true;
        }
        out_spec->gamepad_button = gamepad_button_from_json(button);
        if (out_spec->gamepad_button == SDL_GAMEPAD_BUTTON_INVALID)
        {
            set_error(error_buffer, error_buffer_size, "input assignment gamepad binding has an invalid button");
            return false;
        }
        out_spec->source = SDL3D_INPUT_GAMEPAD_BUTTON;
        out_spec->scale = 1.0f;
        return true;
    }

    set_errorf(error_buffer, error_buffer_size, "input assignment set uses unsupported device '%s'",
               device != NULL ? device : "<missing>");
    return false;
}

static bool apply_input_assignment_json(sdl3d_game_data_runtime *runtime, sdl3d_input_manager *input,
                                        yyjson_val *assignment, char *error_buffer, int error_buffer_size)
{
    const char *set_name = json_string(assignment, "set", NULL);
    yyjson_val *set = sdl3d_game_data_find_input_assignment_set_json(runtime_root(runtime), set_name);
    yyjson_val *actions = obj_get(assignment, "actions");
    yyjson_val *bindings = obj_get(set, "bindings");
    const char *device = json_string(set, "device", NULL);
    const int gamepad_slot = json_int(assignment, "slot", -1);
    if (runtime == NULL || input == NULL || assignment == NULL || set == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "input assignment set '%s' was not found",
                   set_name != NULL ? set_name : "<missing>");
        return false;
    }
    if (!yyjson_is_obj(actions))
    {
        set_errorf(error_buffer, error_buffer_size, "input assignment '%s' requires an actions object",
                   set_name != NULL ? set_name : "<missing>");
        return false;
    }

    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        const char *semantic = json_string(binding, "semantic", NULL);
        const char *action = semantic != NULL ? json_string(actions, semantic, NULL) : NULL;
        input_binding_spec spec;
        if (semantic == NULL || action == NULL)
        {
            set_errorf(error_buffer, error_buffer_size, "input assignment '%s' is missing action for semantic '%s'",
                       set_name != NULL ? set_name : "<missing>", semantic != NULL ? semantic : "<missing>");
            return false;
        }
        if (!input_assignment_binding_to_spec(runtime, device, binding, action, gamepad_slot, &spec, error_buffer,
                                              error_buffer_size))
        {
            return false;
        }
        bind_input_spec(input, &spec);
    }
    return true;
}

static bool apply_input_profile_json(sdl3d_game_data_runtime *runtime, sdl3d_input_manager *input, yyjson_val *profile,
                                     char *error_buffer, int error_buffer_size)
{
    yyjson_val *unbind = obj_get(profile, "unbind");
    yyjson_val *bindings = obj_get(profile, "bindings");
    yyjson_val *assignments = obj_get(profile, "assignments");
    if (runtime == NULL || input == NULL || profile == NULL)
    {
        set_error(error_buffer, error_buffer_size, "input profile apply requires runtime, input, and profile");
        return false;
    }
    if (bindings != NULL && assignments != NULL)
    {
        set_error(error_buffer, error_buffer_size, "input profile cannot mix bindings and assignments");
        return false;
    }

    for (size_t i = 0; yyjson_is_arr(unbind) && i < yyjson_arr_size(unbind); ++i)
    {
        const char *action = yyjson_get_str(yyjson_arr_get(unbind, i));
        const int action_id = sdl3d_game_data_find_action(runtime, action);
        if (action_id < 0)
        {
            set_errorf(error_buffer, error_buffer_size, "input profile unbind references unknown action '%s'",
                       action != NULL ? action : "<invalid>");
            return false;
        }
        sdl3d_input_unbind_action(input, action_id);
    }

    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        input_binding_spec spec;
        if (!input_profile_binding_to_spec(runtime, yyjson_arr_get(bindings, i), &spec, error_buffer,
                                           error_buffer_size))
        {
            return false;
        }
        bind_input_spec(input, &spec);
    }

    for (size_t i = 0; yyjson_is_arr(assignments) && i < yyjson_arr_size(assignments); ++i)
    {
        if (!apply_input_assignment_json(runtime, input, yyjson_arr_get(assignments, i), error_buffer,
                                         error_buffer_size))
        {
            return false;
        }
    }
    return true;
}

static bool input_profile_matches(const sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input,
                                  yyjson_val *profile)
{
    const int gamepads = sdl3d_input_gamepad_count(input);
    yyjson_val *min_gamepads = obj_get(profile, "min_gamepads");
    yyjson_val *max_gamepads = obj_get(profile, "max_gamepads");
    if (yyjson_is_num(min_gamepads) && gamepads < (int)yyjson_get_sint(min_gamepads))
        return false;
    if (yyjson_is_num(max_gamepads) && gamepads > (int)yyjson_get_sint(max_gamepads))
        return false;
    return eval_data_condition(runtime, obj_get(profile, "active_if"), NULL);
}

static yyjson_val *find_active_input_profile_json(const sdl3d_game_data_runtime *runtime,
                                                  const sdl3d_input_manager *input)
{
    if (runtime == NULL || input == NULL)
        return NULL;

    yyjson_val *profiles = obj_get(obj_get(runtime_root(runtime), "input"), "profiles");
    for (size_t i = 0; yyjson_is_arr(profiles) && i < yyjson_arr_size(profiles); ++i)
    {
        yyjson_val *profile = yyjson_arr_get(profiles, i);
        if (input_profile_matches(runtime, input, profile))
            return profile;
    }
    return NULL;
}

bool sdl3d_game_data_apply_input_profile(sdl3d_game_data_runtime *runtime, sdl3d_input_manager *input,
                                         const char *profile_name, char *error_buffer, int error_buffer_size)
{
    yyjson_val *profile = find_input_profile_json(runtime, profile_name);
    if (profile == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "input profile '%s' was not found",
                   profile_name != NULL ? profile_name : "<null>");
        return false;
    }
    return apply_input_profile_json(runtime, input, profile, error_buffer, error_buffer_size);
}

bool sdl3d_game_data_apply_active_input_profile(sdl3d_game_data_runtime *runtime, sdl3d_input_manager *input,
                                                const char **out_profile_name, char *error_buffer,
                                                int error_buffer_size)
{
    if (out_profile_name != NULL)
        *out_profile_name = NULL;
    if (runtime == NULL || input == NULL)
    {
        set_error(error_buffer, error_buffer_size, "active input profile apply requires runtime and input");
        return false;
    }

    yyjson_val *profile = find_active_input_profile_json(runtime, input);
    if (profile != NULL)
    {
        const char *name = json_string(profile, "name", NULL);
        if (!apply_input_profile_json(runtime, input, profile, error_buffer, error_buffer_size))
            return false;
        if (out_profile_name != NULL)
            *out_profile_name = name;
        return true;
    }

    set_error(error_buffer, error_buffer_size, "no authored input profile matched the current state");
    return false;
}

bool sdl3d_game_data_get_active_input_profile_name(const sdl3d_game_data_runtime *runtime,
                                                   const sdl3d_input_manager *input, const char **out_profile_name)
{
    if (out_profile_name != NULL)
        *out_profile_name = NULL;

    yyjson_val *profile = find_active_input_profile_json(runtime, input);
    if (profile == NULL)
        return false;

    if (out_profile_name != NULL)
        *out_profile_name = json_string(profile, "name", NULL);
    return true;
}

void sdl3d_game_data_input_profile_refresh_state_init(sdl3d_game_data_input_profile_refresh_state *state)
{
    if (state == NULL)
        return;

    state->gamepad_count = -1;
    state->initialized = false;
}

bool sdl3d_game_data_apply_active_input_profile_on_device_change(sdl3d_game_data_runtime *runtime,
                                                                 sdl3d_input_manager *input,
                                                                 sdl3d_game_data_input_profile_refresh_state *state,
                                                                 const char **out_profile_name, bool *out_applied,
                                                                 char *error_buffer, int error_buffer_size)
{
    if (out_profile_name != NULL)
        *out_profile_name = NULL;
    if (out_applied != NULL)
        *out_applied = false;
    if (runtime == NULL || input == NULL || state == NULL)
    {
        set_error(error_buffer, error_buffer_size,
                  "active input profile device refresh requires runtime, input, and refresh state");
        return false;
    }

    const int gamepad_count = sdl3d_input_gamepad_count(input);
    if (state->initialized && state->gamepad_count == gamepad_count)
        return true;

    const char *profile_name = NULL;
    if (!sdl3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error_buffer, error_buffer_size))
        return false;

    state->gamepad_count = gamepad_count;
    state->initialized = true;
    if (out_profile_name != NULL)
        *out_profile_name = profile_name;
    if (out_applied != NULL)
        *out_applied = true;
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

static bool json_scalar_to_value(yyjson_val *json, sdl3d_value *out_value)
{
    if (json == NULL || out_value == NULL)
        return false;

    SDL_zero(*out_value);
    if (yyjson_is_str(json))
    {
        out_value->type = SDL3D_VALUE_STRING;
        out_value->as_string = (char *)yyjson_get_str(json);
        return out_value->as_string != NULL;
    }
    if (yyjson_is_bool(json))
    {
        out_value->type = SDL3D_VALUE_BOOL;
        out_value->as_bool = yyjson_get_bool(json);
        return true;
    }
    if (yyjson_is_int(json))
    {
        out_value->type = SDL3D_VALUE_INT;
        out_value->as_int = (int)yyjson_get_sint(json);
        return true;
    }
    if (yyjson_is_num(json))
    {
        out_value->type = SDL3D_VALUE_FLOAT;
        out_value->as_float = (float)yyjson_get_real(json);
        return true;
    }
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
    if (SDL_strcmp(type, "scene_state.compare") == 0)
    {
        const char *key = json_string(condition, "key", NULL);
        const char *op = json_string(condition, "op", NULL);
        const sdl3d_value *left = runtime != NULL && runtime->scene_state != NULL && key != NULL
                                      ? sdl3d_properties_get_value(runtime->scene_state, key)
                                      : NULL;
        sdl3d_value fallback;
        if (left == NULL && json_scalar_to_value(obj_get(condition, "default"), &fallback))
            left = &fallback;
        return key != NULL && compare_value(left, op, obj_get(condition, "value"));
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
    if (SDL_strcmp(type, "scene_state") == 0)
    {
        const char *key = json_string(binding, "key", NULL);
        const sdl3d_value *value = runtime != NULL && runtime->scene_state != NULL && key != NULL
                                       ? sdl3d_properties_get_value(runtime->scene_state, key)
                                       : NULL;
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

static sdl3d_value_type property_type_from_name(const char *name, sdl3d_value_type fallback)
{
    if (name == NULL)
        return fallback;
    if (SDL_strcmp(name, "int") == 0)
        return SDL3D_VALUE_INT;
    if (SDL_strcmp(name, "float") == 0 || SDL_strcmp(name, "number") == 0)
        return SDL3D_VALUE_FLOAT;
    if (SDL_strcmp(name, "vec3") == 0)
        return SDL3D_VALUE_VEC3;
    if (SDL_strcmp(name, "color") == 0)
        return SDL3D_VALUE_COLOR;
    return fallback;
}

static bool start_property_animation_from_json(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
    const char *key = json_string(action, "key", NULL);
    yyjson_val *to_json = obj_get(action, "to");
    if (to_json == NULL)
        to_json = obj_get(action, "value");
    if (actor == NULL || key == NULL || to_json == NULL)
        return false;

    const sdl3d_value *current = sdl3d_properties_get_value(actor->props, key);
    const char *value_type = json_string(action, "value_type", NULL);
    const game_data_tween_value_type preferred = tween_preferred_type(value_type, current, NULL);

    game_data_animation animation;
    SDL_zero(animation);
    animation.target_type = GAME_DATA_TWEEN_PROPERTY;
    animation.target = actor->name;
    animation.key = key;
    animation.scene = sdl3d_game_data_active_scene(runtime);
    animation.duration = json_float(action, "duration", 0.0f);
    animation.easing = parse_tween_easing(json_string(action, "easing", NULL));
    animation.repeat = parse_tween_repeat(json_string(action, "repeat", NULL));
    animation.done_signal_id = action_signal_id(runtime, action, "done_signal");
    animation.property_type = current != NULL ? current->type : property_type_from_name(value_type, SDL3D_VALUE_FLOAT);

    yyjson_val *from_json = obj_get(action, "from");
    if (from_json != NULL)
    {
        if (!json_tween_value(from_json, preferred, &animation.from))
            return false;
    }
    else if (!current_property_tween_value(current, preferred, &animation.from, &animation.property_type))
    {
        animation.from = preferred == GAME_DATA_TWEEN_COLOR  ? tween_color((sdl3d_color){255, 255, 255, 255})
                         : preferred == GAME_DATA_TWEEN_VEC3 ? tween_vec3(sdl3d_vec3_make(0.0f, 0.0f, 0.0f))
                                                             : tween_float(0.0f);
    }

    if (!json_tween_value(to_json, preferred, &animation.to))
        return false;
    if (animation.from.type != animation.to.type)
        return false;
    if (animation.to.type == GAME_DATA_TWEEN_VEC3)
        animation.property_type = SDL3D_VALUE_VEC3;
    else if (animation.to.type == GAME_DATA_TWEEN_COLOR)
        animation.property_type = SDL3D_VALUE_COLOR;
    else
        animation.property_type = property_type_from_name(value_type, animation.property_type);

    return start_animation(runtime, &animation);
}

static bool start_ui_animation_from_json(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", json_string(action, "ui", NULL));
    const char *property = json_string(action, "property", NULL);
    yyjson_val *to_json = obj_get(action, "to");
    if (to_json == NULL)
        to_json = obj_get(action, "value");
    if (target == NULL || property == NULL || to_json == NULL)
        return false;

    sdl3d_game_data_ui_state state;
    sdl3d_game_data_ui_state_init(&state);
    (void)sdl3d_game_data_get_ui_state(runtime, target, &state);
    const game_data_tween_value_type preferred =
        tween_preferred_type(json_string(action, "value_type", NULL), NULL, property);

    game_data_animation animation;
    SDL_zero(animation);
    animation.target_type = GAME_DATA_TWEEN_UI;
    animation.target = target;
    animation.property = property;
    animation.scene = sdl3d_game_data_active_scene(runtime);
    animation.duration = json_float(action, "duration", 0.0f);
    animation.easing = parse_tween_easing(json_string(action, "easing", NULL));
    animation.repeat = parse_tween_repeat(json_string(action, "repeat", NULL));
    animation.done_signal_id = action_signal_id(runtime, action, "done_signal");
    animation.property_type = SDL3D_VALUE_FLOAT;

    yyjson_val *from_json = obj_get(action, "from");
    if (from_json != NULL)
    {
        if (!json_tween_value(from_json, preferred, &animation.from))
            return false;
    }
    else if (!current_ui_tween_value(&state, property, &animation.from))
    {
        return false;
    }

    if (!json_tween_value(to_json, preferred, &animation.to))
        return false;
    if (animation.from.type != animation.to.type)
        return false;
    return start_animation(runtime, &animation);
}

bool sdl3d_game_data_update_animations(sdl3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL)
        return false;

    reset_animation_scope_if_needed(runtime);
    const float step = dt > 0.0f ? dt : 0.0f;
    sdl3d_signal_bus *bus = runtime_bus(runtime);
    for (int i = 0; i < runtime->animation_count;)
    {
        game_data_animation *animation = &runtime->animations[i];
        animation->elapsed += step;

        bool complete = false;
        float t = 1.0f;
        if (animation->duration > 0.0f)
        {
            if (animation->repeat == GAME_DATA_TWEEN_REPEAT_LOOP)
            {
                t = SDL_fmodf(animation->elapsed, animation->duration) / animation->duration;
            }
            else if (animation->repeat == GAME_DATA_TWEEN_REPEAT_PING_PONG)
            {
                const float cycle_float = SDL_floorf(animation->elapsed / animation->duration);
                t = SDL_fmodf(animation->elapsed, animation->duration) / animation->duration;
                if (((int)cycle_float % 2) != 0)
                    t = 1.0f - t;
            }
            else
            {
                complete = animation->elapsed >= animation->duration;
                t = complete ? 1.0f : animation->elapsed / animation->duration;
            }
        }

        const float eased = apply_tween_easing(animation->easing, t);
        const game_data_tween_value value = interpolate_tween_value(&animation->from, &animation->to, eased);
        apply_animation_value(runtime, animation, &value);

        if (complete)
        {
            if (animation->done_signal_id >= 0 && bus != NULL)
                sdl3d_signal_emit(bus, animation->done_signal_id, NULL);
            runtime->animations[i] = runtime->animations[runtime->animation_count - 1];
            --runtime->animation_count;
            continue;
        }
        ++i;
    }
    return true;
}

static bool ensure_audio_file_capacity(sdl3d_game_data_runtime *runtime, int required)
{
    if (runtime == NULL)
        return false;
    if (required <= runtime->audio_file_capacity)
        return true;

    int next_capacity = runtime->audio_file_capacity < 8 ? 8 : runtime->audio_file_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    materialized_audio_file *files =
        (materialized_audio_file *)SDL_realloc(runtime->audio_files, (size_t)next_capacity * sizeof(*files));
    if (files == NULL)
        return false;

    SDL_memset(files + runtime->audio_file_capacity, 0,
               (size_t)(next_capacity - runtime->audio_file_capacity) * sizeof(*files));
    runtime->audio_files = files;
    runtime->audio_file_capacity = next_capacity;
    return true;
}

static uint32_t hash_audio_asset_path(const char *path)
{
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)path; p != NULL && *p != '\0'; ++p)
    {
        hash ^= (uint32_t)*p;
        hash *= 16777619u;
    }
    return hash;
}

static char *make_safe_audio_filename(const char *path)
{
    char *base = path_basename(asset_path_without_scheme(path));
    if (base == NULL)
        return NULL;

    for (char *p = base; *p != '\0'; ++p)
    {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' ||
              *p == '_' || *p == '-'))
        {
            *p = '_';
        }
    }

    char hash[16];
    SDL_snprintf(hash, sizeof(hash), "%08x-", (unsigned int)hash_audio_asset_path(path));
    const size_t hash_len = SDL_strlen(hash);
    const size_t base_len = SDL_strlen(base);
    char *filename = (char *)SDL_malloc(hash_len + base_len + 1u);
    if (filename == NULL)
    {
        SDL_free(base);
        return NULL;
    }
    SDL_memcpy(filename, hash, hash_len);
    SDL_memcpy(filename + hash_len, base, base_len + 1u);
    SDL_free(base);
    return filename;
}

static bool ensure_runtime_storage(sdl3d_game_data_runtime *runtime, char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data runtime is required");
        return false;
    }
    if (runtime->storage != NULL)
        return true;

    return sdl3d_storage_create(&runtime->storage_config, &runtime->storage, error_buffer, error_buffer_size);
}

static yyjson_val *find_persistence_entry(const sdl3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *entries = obj_get(obj_get(runtime_root(runtime), "persistence"), "entries");
    for (size_t i = 0; name != NULL && yyjson_is_arr(entries) && i < yyjson_arr_size(entries); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(entries, i);
        const char *entry_name = json_string(entry, "name", NULL);
        if (entry_name != NULL && SDL_strcmp(entry_name, name) == 0)
            return entry;
    }
    return NULL;
}

static const char *action_persistence_entry_name(yyjson_val *action)
{
    const char *entry = json_string(action, "entry", NULL);
    return entry != NULL ? entry : json_string(action, "name", NULL);
}

static bool persistence_entry_is_enabled(sdl3d_game_data_runtime *runtime, yyjson_val *entry)
{
    yyjson_val *condition = obj_get(entry, "enabled_if");
    return condition == NULL || eval_data_condition(runtime, condition, NULL);
}

static const char *persistence_property_key(yyjson_val *property)
{
    if (yyjson_is_str(property))
        return yyjson_get_str(property);
    return json_string(property, "key", NULL);
}

static bool json_add_property_value(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key,
                                    const sdl3d_value *value)
{
    if (doc == NULL || object == NULL || key == NULL || value == NULL)
        return false;

    switch (value->type)
    {
    case SDL3D_VALUE_INT:
        return yyjson_mut_obj_add_int(doc, object, key, value->as_int);
    case SDL3D_VALUE_FLOAT:
        return yyjson_mut_obj_add_real(doc, object, key, value->as_float);
    case SDL3D_VALUE_BOOL:
        return yyjson_mut_obj_add_bool(doc, object, key, value->as_bool);
    case SDL3D_VALUE_STRING:
        return yyjson_mut_obj_add_strcpy(doc, object, key, value->as_string != NULL ? value->as_string : "");
    case SDL3D_VALUE_VEC3: {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        return arr != NULL && yyjson_mut_arr_add_real(doc, arr, value->as_vec3.x) &&
               yyjson_mut_arr_add_real(doc, arr, value->as_vec3.y) &&
               yyjson_mut_arr_add_real(doc, arr, value->as_vec3.z) && yyjson_mut_obj_add_val(doc, object, key, arr);
    }
    case SDL3D_VALUE_COLOR: {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        return arr != NULL && yyjson_mut_arr_add_int(doc, arr, value->as_color.r) &&
               yyjson_mut_arr_add_int(doc, arr, value->as_color.g) &&
               yyjson_mut_arr_add_int(doc, arr, value->as_color.b) &&
               yyjson_mut_arr_add_int(doc, arr, value->as_color.a) && yyjson_mut_obj_add_val(doc, object, key, arr);
    }
    }
    return false;
}

static bool execute_persistence_save(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *entry = find_persistence_entry(runtime, action_persistence_entry_name(action));
    const char *path = json_string(entry, "path", NULL);
    const char *target = json_string(entry, "target", NULL);
    yyjson_val *properties = obj_get(entry, "properties");
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, target);
    if (entry == NULL || path == NULL || actor == NULL || !yyjson_is_arr(properties))
        return false;
    if (!persistence_entry_is_enabled(runtime, entry))
        return true;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    if (root == NULL)
    {
        yyjson_mut_doc_free(doc);
        return false;
    }
    yyjson_mut_doc_set_root(doc, root);

    const char *schema = json_string(entry, "schema", NULL);
    if (schema != NULL && !yyjson_mut_obj_add_strcpy(doc, root, "schema", schema))
    {
        yyjson_mut_doc_free(doc);
        return false;
    }
    yyjson_val *version = obj_get(entry, "version");
    if (yyjson_is_int(version) && !yyjson_mut_obj_add_int(doc, root, "version", (int)yyjson_get_int(version)))
    {
        yyjson_mut_doc_free(doc);
        return false;
    }

    for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
    {
        const char *key = persistence_property_key(yyjson_arr_get(properties, i));
        if (key == NULL || key[0] == '\0')
            continue;
        const sdl3d_value *value = sdl3d_properties_get_value(actor->props, key);
        if (value != NULL && !json_add_property_value(doc, root, key, value))
        {
            yyjson_mut_doc_free(doc);
            return false;
        }
    }

    size_t size = 0u;
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size);
    yyjson_mut_doc_free(doc);
    if (json == NULL)
        return false;

    char storage_error[256];
    const bool ok =
        ensure_runtime_storage(runtime, storage_error, (int)sizeof(storage_error)) &&
        sdl3d_storage_write_file(runtime->storage, path, json, size, storage_error, (int)sizeof(storage_error));
    free(json);
    if (!ok)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D persistence save failed: %s", storage_error);
    return ok;
}

static bool execute_persistence_load(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *entry = find_persistence_entry(runtime, action_persistence_entry_name(action));
    const char *path = json_string(entry, "path", NULL);
    const char *target = json_string(entry, "target", NULL);
    yyjson_val *properties = obj_get(entry, "properties");
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, target);
    if (entry == NULL || path == NULL || actor == NULL || !yyjson_is_arr(properties))
        return false;
    if (!persistence_entry_is_enabled(runtime, entry))
        return true;

    char storage_error[256];
    if (!ensure_runtime_storage(runtime, storage_error, (int)sizeof(storage_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D persistence storage unavailable: %s", storage_error);
        return false;
    }

    sdl3d_storage_buffer buffer;
    SDL_zero(buffer);
    if (!sdl3d_storage_read_file(runtime->storage, path, &buffer, storage_error, (int)sizeof(storage_error)))
        return true;

    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, NULL);
    yyjson_val *root = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    const char *schema = json_string(entry, "schema", NULL);
    yyjson_val *version = obj_get(entry, "version");
    const bool schema_ok = schema == NULL || SDL_strcmp(json_string(root, "schema", ""), schema) == 0;
    const bool version_ok =
        !yyjson_is_int(version) || (yyjson_is_int(obj_get(root, "version")) &&
                                    yyjson_get_int(obj_get(root, "version")) == yyjson_get_int(version));
    if (yyjson_is_obj(root) && schema_ok && version_ok)
    {
        for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
        {
            const char *key = persistence_property_key(yyjson_arr_get(properties, i));
            yyjson_val *value = key != NULL ? obj_get(root, key) : NULL;
            if (value != NULL)
                set_actor_property_from_json(actor, key, value);
        }
    }
    else
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D persistence ignored incompatible file: %s", path);
    }

    yyjson_doc_free(doc);
    sdl3d_storage_buffer_free(&buffer);
    return true;
}

static bool execute_persistence_action(sdl3d_game_data_runtime *runtime, yyjson_val *action, const char *type)
{
    if (SDL_strcmp(type, "persistence.save") == 0)
        return execute_persistence_save(runtime, action);
    if (SDL_strcmp(type, "persistence.load") == 0)
        return execute_persistence_load(runtime, action);
    return false;
}

static bool ensure_audio_cache_storage(sdl3d_game_data_runtime *runtime)
{
    char storage_error[256];
    if (!ensure_runtime_storage(runtime, storage_error, (int)sizeof(storage_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio cache storage unavailable: %s", storage_error);
        return false;
    }

    if (!sdl3d_storage_create_directory(runtime->storage, "cache://audio", storage_error, (int)sizeof(storage_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio cache directory unavailable: %s", storage_error);
        return false;
    }

    char path[4096];
    if (sdl3d_storage_resolve_path(runtime->storage, "cache://audio", path, sizeof(path)))
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio cache directory: %s", path);
    return true;
}

static char *make_audio_cache_path(const sdl3d_game_data_runtime *runtime, const char *filename)
{
    char virtual_path[512];
    char resolved[4096];
    if (runtime == NULL || filename == NULL ||
        SDL_snprintf(virtual_path, sizeof(virtual_path), "cache://audio/%s", filename) >= (int)sizeof(virtual_path) ||
        !sdl3d_storage_resolve_path(runtime->storage, virtual_path, resolved, sizeof(resolved)))
    {
        return NULL;
    }
    return SDL_strdup(resolved);
}

static char *resolve_authored_audio_path(const sdl3d_game_data_runtime *runtime, const char *path)
{
    if (path == NULL || path[0] == '\0')
        return NULL;
    if (SDL_strncmp(path, "asset://", 8) == 0)
        return SDL_strdup(path);
    return path_join(runtime != NULL ? runtime->base_dir : NULL, path);
}

static char *materialize_audio_asset(sdl3d_game_data_runtime *runtime, const char *path)
{
    char *resolved = resolve_authored_audio_path(runtime, path);
    if (runtime == NULL || resolved == NULL)
        return resolved;

    for (int i = 0; i < runtime->audio_file_count; ++i)
    {
        if (SDL_strcmp(runtime->audio_files[i].asset_path, resolved) == 0)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio asset cache hit: %s -> %s", resolved,
                        runtime->audio_files[i].file_path);
            SDL_free(resolved);
            return SDL_strdup(runtime->audio_files[i].file_path);
        }
    }

    if (runtime->assets == NULL || !sdl3d_asset_resolver_exists(runtime->assets, resolved))
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio using filesystem path: %s", resolved);
        return resolved;
    }

    if (!ensure_audio_cache_storage(runtime))
    {
        SDL_free(resolved);
        return NULL;
    }

    sdl3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!sdl3d_asset_resolver_read_file(runtime->assets, resolved, &buffer, asset_error, (int)sizeof(asset_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio asset read failed: %s", asset_error);
        SDL_free(resolved);
        return NULL;
    }

    char *filename = make_safe_audio_filename(resolved);
    char virtual_path[512];
    const bool has_virtual_path =
        filename != NULL &&
        SDL_snprintf(virtual_path, sizeof(virtual_path), "cache://audio/%s", filename) < (int)sizeof(virtual_path);
    char *file_path = has_virtual_path ? make_audio_cache_path(runtime, filename) : NULL;
    SDL_free(filename);
    if (file_path == NULL)
    {
        sdl3d_asset_buffer_free(&buffer);
        SDL_free(resolved);
        return NULL;
    }

    char storage_error[256];
    const bool wrote = sdl3d_storage_write_file(runtime->storage, virtual_path, buffer.data, buffer.size, storage_error,
                                                (int)sizeof(storage_error));
    sdl3d_asset_buffer_free(&buffer);
    if (!wrote)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio asset materialization failed: %s: %s", file_path,
                    storage_error);
        SDL_free(file_path);
        SDL_free(resolved);
        return NULL;
    }

    if (!ensure_audio_file_capacity(runtime, runtime->audio_file_count + 1))
    {
        SDL_free(file_path);
        SDL_free(resolved);
        return NULL;
    }
    runtime->audio_files[runtime->audio_file_count].asset_path = SDL_strdup(resolved);
    runtime->audio_files[runtime->audio_file_count].file_path = SDL_strdup(file_path);
    if (runtime->audio_files[runtime->audio_file_count].asset_path == NULL ||
        runtime->audio_files[runtime->audio_file_count].file_path == NULL)
    {
        SDL_free(runtime->audio_files[runtime->audio_file_count].asset_path);
        SDL_free(runtime->audio_files[runtime->audio_file_count].file_path);
        SDL_free(file_path);
        SDL_free(resolved);
        return NULL;
    }
    ++runtime->audio_file_count;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio asset materialized: %s -> %s", resolved, file_path);
    SDL_free(resolved);
    return file_path;
}

bool sdl3d_game_data_prepare_audio_file(sdl3d_game_data_runtime *runtime, const char *path, char *out_path,
                                        int out_path_size)
{
    if (out_path != NULL && out_path_size > 0)
        out_path[0] = '\0';
    if (runtime == NULL || path == NULL || path[0] == '\0' || out_path == NULL || out_path_size <= 0)
        return false;

    char *file_path = materialize_audio_asset(runtime, path);
    if (file_path == NULL)
        return false;

    const size_t len = SDL_strlen(file_path);
    const bool ok = len < (size_t)out_path_size;
    if (ok)
        SDL_memcpy(out_path, file_path, len + 1u);
    SDL_free(file_path);
    return ok;
}

static bool execute_audio_play_sfx(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    sdl3d_audio_engine *audio = sdl3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio.play_sfx ignored because audio engine is unavailable");
        return true;
    }

    sdl3d_game_data_sound_asset sound;
    const char *sound_id = json_string(action, "sound", json_string(action, "asset", NULL));
    const bool has_asset = sdl3d_game_data_get_sound_asset(runtime, sound_id, &sound);
    const char *path = json_string(action, "path", has_asset ? sound.path : NULL);
    if (path == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio.play_sfx missing sound asset/path: %s",
                    sound_id != NULL ? sound_id : "<none>");
        return false;
    }

    sdl3d_audio_play_desc desc = sdl3d_audio_play_desc_default();
    if (has_asset)
    {
        desc.volume = sound.volume;
        desc.pitch = sound.pitch;
        desc.pan = sound.pan;
        desc.bus = sound.bus;
    }
    desc.volume = json_float(action, "volume", desc.volume);
    desc.pitch = json_float(action, "pitch", desc.pitch);
    desc.pan = json_float(action, "pan", desc.pan);
    desc.bus = parse_audio_bus(json_string(action, "bus", NULL), desc.bus);

    char file_path[4096];
    if (!sdl3d_game_data_prepare_audio_file(runtime, path, file_path, sizeof(file_path)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio.play_sfx failed to resolve: %s", path);
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio.play_sfx sound=%s path=%s resolved=%s volume=%.3f bus=%d",
                sound_id != NULL ? sound_id : "<path>", path, file_path, desc.volume, (int)desc.bus);
    const bool ok = sdl3d_audio_play_sound_file(audio, file_path, &desc);
    if (!ok)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio.play_sfx failed: %s", SDL_GetError());
    return ok;
}

static bool execute_audio_play_music(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    sdl3d_audio_engine *audio = sdl3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL3D audio.play_music ignored because audio engine is unavailable");
        return true;
    }

    sdl3d_game_data_music_asset music;
    const char *music_id = json_string(action, "music", json_string(action, "asset", NULL));
    const bool has_asset = sdl3d_game_data_get_music_asset(runtime, music_id, &music);
    const char *path = json_string(action, "path", has_asset ? music.path : NULL);
    if (path == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio.play_music missing music asset/path: %s",
                    music_id != NULL ? music_id : "<none>");
        return false;
    }

    const float volume = json_float(action, "volume", has_asset ? music.volume : 1.0f);
    const bool loop = json_bool(action, "loop", has_asset ? music.loop : true);
    const float fade = json_float(action, "fade", json_float(action, "fade_seconds", 0.0f));

    char file_path[4096];
    if (!sdl3d_game_data_prepare_audio_file(runtime, path, file_path, sizeof(file_path)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio.play_music failed to resolve: %s", path);
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio.play_music music=%s path=%s resolved=%s volume=%.3f loop=%d",
                music_id != NULL ? music_id : "<path>", path, file_path, volume, loop ? 1 : 0);
    const bool ok = sdl3d_audio_play_music(audio, file_path, loop, volume, fade);
    if (!ok)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D audio.play_music failed: %s", SDL_GetError());
    return ok;
}

static float action_float_or_property(sdl3d_game_data_runtime *runtime, yyjson_val *action, const char *key,
                                      float fallback)
{
    yyjson_val *source = obj_get(action, "source");
    if (yyjson_is_obj(source))
    {
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(source, "target", NULL));
        const char *property = json_string(source, "key", NULL);
        const sdl3d_value *value =
            actor != NULL && property != NULL ? sdl3d_properties_get_value(actor->props, property) : NULL;
        if (value != NULL && value->type == SDL3D_VALUE_FLOAT)
            return value->as_float * json_float(source, "scale", 1.0f);
        if (value != NULL && value->type == SDL3D_VALUE_INT)
            return (float)value->as_int * json_float(source, "scale", 1.0f);
    }
    return json_float(action, key, fallback);
}

static bool execute_audio_action(sdl3d_game_data_runtime *runtime, yyjson_val *action, const char *type)
{
    sdl3d_audio_engine *audio = sdl3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (SDL_strcmp(type, "audio.play_sfx") == 0)
        return execute_audio_play_sfx(runtime, action);
    if (SDL_strcmp(type, "audio.play_music") == 0)
        return execute_audio_play_music(runtime, action);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SDL3D %s ignored because audio engine is unavailable", type);
        return true;
    }
    if (SDL_strcmp(type, "audio.stop_sfx") == 0)
    {
        sdl3d_audio_stop_bus(audio, parse_audio_bus(json_string(action, "bus", NULL), SDL3D_AUDIO_BUS_SOUND_EFFECTS));
        return true;
    }
    if (SDL_strcmp(type, "audio.stop_music") == 0)
    {
        sdl3d_audio_stop_music(audio, json_float(action, "fade", json_float(action, "fade_seconds", 0.0f)));
        return true;
    }
    if (SDL_strcmp(type, "audio.fade_music") == 0)
    {
        sdl3d_audio_fade_music(audio, json_float(action, "volume", 1.0f),
                               json_float(action, "duration", json_float(action, "fade_seconds", 0.0f)));
        return true;
    }
    if (SDL_strcmp(type, "audio.set_bus_volume") == 0)
    {
        sdl3d_audio_set_bus_volume(audio, parse_audio_bus(json_string(action, "bus", NULL), SDL3D_AUDIO_BUS_COUNT),
                                   action_float_or_property(runtime, action, "volume", 1.0f));
        return true;
    }
    return false;
}

static bool json_string_array_contains(yyjson_val *array, const char *value)
{
    if (!yyjson_is_arr(array) || value == NULL)
        return false;
    for (size_t i = 0; i < yyjson_arr_size(array); ++i)
    {
        const char *entry = yyjson_get_str(yyjson_arr_get(array, i));
        if (entry != NULL && SDL_strcmp(entry, value) == 0)
            return true;
    }
    return false;
}

static property_snapshot *find_property_snapshot(sdl3d_game_data_runtime *runtime, const char *name, const char *target)
{
    if (runtime == NULL || name == NULL || target == NULL)
        return NULL;
    for (int i = 0; i < runtime->property_snapshot_count; ++i)
    {
        property_snapshot *snapshot = &runtime->property_snapshots[i];
        if (snapshot->name != NULL && snapshot->target != NULL && SDL_strcmp(snapshot->name, name) == 0 &&
            SDL_strcmp(snapshot->target, target) == 0)
            return snapshot;
    }
    return NULL;
}

static property_snapshot *get_or_create_property_snapshot(sdl3d_game_data_runtime *runtime, const char *name,
                                                          const char *target)
{
    property_snapshot *existing = find_property_snapshot(runtime, name, target);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || name == NULL || name[0] == '\0' || target == NULL || target[0] == '\0')
        return NULL;
    if (runtime->property_snapshot_count >= runtime->property_snapshot_capacity)
    {
        const int next_capacity = runtime->property_snapshot_capacity > 0 ? runtime->property_snapshot_capacity * 2 : 4;
        property_snapshot *next = (property_snapshot *)SDL_realloc(
            runtime->property_snapshots, (size_t)next_capacity * sizeof(*runtime->property_snapshots));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->property_snapshot_capacity, 0,
                   (size_t)(next_capacity - runtime->property_snapshot_capacity) *
                       sizeof(*runtime->property_snapshots));
        runtime->property_snapshots = next;
        runtime->property_snapshot_capacity = next_capacity;
    }

    property_snapshot *snapshot = &runtime->property_snapshots[runtime->property_snapshot_count];
    snapshot->name = SDL_strdup(name);
    snapshot->target = SDL_strdup(target);
    snapshot->properties = sdl3d_properties_create();
    if (snapshot->name == NULL || snapshot->target == NULL || snapshot->properties == NULL)
    {
        SDL_free(snapshot->name);
        SDL_free(snapshot->target);
        sdl3d_properties_destroy(snapshot->properties);
        SDL_zero(*snapshot);
        return NULL;
    }
    runtime->property_snapshot_count++;
    return snapshot;
}

static void copy_selected_properties(sdl3d_properties *target, const sdl3d_properties *source, yyjson_val *keys)
{
    if (target == NULL || source == NULL)
        return;
    if (yyjson_is_arr(keys))
    {
        for (size_t i = 0; i < yyjson_arr_size(keys); ++i)
        {
            const char *key = yyjson_get_str(yyjson_arr_get(keys, i));
            copy_property_value(target, key, sdl3d_properties_get_value(source, key));
        }
        return;
    }

    const int count = sdl3d_properties_count(source);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (sdl3d_properties_get_key_at(source, i, &key, NULL))
            copy_property_value(target, key, sdl3d_properties_get_value(source, key));
    }
}

static bool snapshot_actor_properties(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    const char *name = json_string(action, "name", NULL);
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, target);
    property_snapshot *snapshot = get_or_create_property_snapshot(runtime, name, target);
    if (actor == NULL || snapshot == NULL)
        return false;

    sdl3d_properties_clear(snapshot->properties);
    copy_selected_properties(snapshot->properties, actor->props, obj_get(action, "keys"));
    return true;
}

static bool restore_actor_property_snapshot(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    const char *name = json_string(action, "name", NULL);
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, target);
    property_snapshot *snapshot = find_property_snapshot(runtime, name, target);
    if (actor == NULL || snapshot == NULL)
        return false;

    copy_selected_properties(actor->props, snapshot->properties, obj_get(action, "keys"));
    return true;
}

static bool reset_actor_properties_to_authored_defaults(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, target);
    yyjson_val *entity = find_entity_json(runtime, target);
    yyjson_val *properties = obj_get(entity, "properties");
    yyjson_val *keys = obj_get(action, "keys");
    if (actor == NULL || !yyjson_is_obj(properties))
        return false;

    yyjson_val *key;
    yyjson_val *entry;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        if (yyjson_is_arr(keys) && !json_string_array_contains(keys, name))
            continue;
        entry = yyjson_obj_iter_get_val(key);
        yyjson_val *value = obj_get(entry, "value");
        if (value != NULL)
            set_actor_property_from_json(actor, name, value);
    }
    return true;
}

static actor_pool_runtime *find_actor_pool(sdl3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        if (runtime->actor_pools[i].name != NULL && SDL_strcmp(runtime->actor_pools[i].name, name) == 0)
            return &runtime->actor_pools[i];
    }
    return NULL;
}

static int actor_pool_index_for_actor(const actor_pool_runtime *pool, const char *actor_name)
{
    if (pool == NULL || actor_name == NULL)
        return -1;
    for (int i = 0; i < pool->capacity; ++i)
    {
        if (pool->actor_names[i] != NULL && SDL_strcmp(pool->actor_names[i], actor_name) == 0)
            return i;
    }
    return -1;
}

static actor_pool_runtime *find_actor_pool_for_actor(sdl3d_game_data_runtime *runtime, const char *actor_name,
                                                     int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (runtime == NULL || actor_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        const int index = actor_pool_index_for_actor(&runtime->actor_pools[i], actor_name);
        if (index >= 0)
        {
            if (out_index != NULL)
                *out_index = index;
            return &runtime->actor_pools[i];
        }
    }
    return NULL;
}

static const actor_pool_runtime *find_actor_pool_for_actor_const(const sdl3d_game_data_runtime *runtime,
                                                                 const char *actor_name, int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (runtime == NULL || actor_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        const int index = actor_pool_index_for_actor(&runtime->actor_pools[i], actor_name);
        if (index >= 0)
        {
            if (out_index != NULL)
                *out_index = index;
            return &runtime->actor_pools[i];
        }
    }
    return NULL;
}

static sdl3d_registered_actor *actor_pool_allocate(sdl3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                                   int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (runtime == NULL || pool == NULL)
        return NULL;

    for (int i = 0; i < pool->capacity; ++i)
    {
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[i]);
        if (actor_pool_actor_is_available(pool, actor, i))
        {
            if (out_index != NULL)
                *out_index = i;
            return actor;
        }
    }

    if (pool->exhaustion != ACTOR_POOL_EXHAUST_REUSE_OLDEST || pool->capacity <= 0)
        return NULL;

    int index = -1;
    Uint64 oldest_generation = SDL_MAX_UINT64;
    for (int i = 0; i < pool->capacity; ++i)
    {
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[i]);
        if (!actor_pool_actor_is_active(pool, actor, i))
            continue;
        const Uint64 generation = pool->spawn_generations != NULL ? pool->spawn_generations[i] : 0U;
        if (index < 0 || generation < oldest_generation)
        {
            index = i;
            oldest_generation = generation;
        }
    }
    if (index < 0)
        return NULL;
    if (out_index != NULL)
        *out_index = index;
    return sdl3d_game_data_find_actor(runtime, pool->actor_names[index]);
}

static bool actor_pool_request_despawn(sdl3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                       sdl3d_registered_actor *actor, int index)
{
    if (runtime == NULL || pool == NULL || actor == NULL || index < 0 || index >= pool->capacity)
        return false;

    actor->active = false;
    actor_pool_set_lifecycle_state(pool, actor, index, ACTOR_LIFECYCLE_DESPAWNING);
    if (runtime->actor_lifecycle_defer_depth > 0)
    {
        runtime->actor_lifecycle_flush_pending = true;
        return true;
    }
    return initialize_pooled_actor(pool, actor, index, false);
}

static void apply_actor_spawn_properties(sdl3d_registered_actor *actor, yyjson_val *properties)
{
    if (actor == NULL || !yyjson_is_obj(properties))
        return;

    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        value = yyjson_obj_iter_get_val(key);
        if (name != NULL)
            set_actor_property_from_json(actor, name, value);
    }
}

static sdl3d_vec3 actor_spawn_position_from_action(sdl3d_game_data_runtime *runtime, yyjson_val *action,
                                                   sdl3d_vec3 fallback)
{
    sdl3d_vec3 position = fallback;
    yyjson_val *position_json = obj_get(action, "position");
    if (position_json != NULL)
        position = json_vec3_value(position_json, position);

    const char *from_actor_name = json_string(action, "from", NULL);
    sdl3d_registered_actor *from_actor = sdl3d_game_data_find_actor(runtime, from_actor_name);
    if (from_actor != NULL)
        position = from_actor->position;

    yyjson_val *offset_json = obj_get(action, "offset");
    if (offset_json != NULL)
    {
        const sdl3d_vec3 offset = json_vec3_value(offset_json, sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
        position.x += offset.x;
        position.y += offset.y;
        position.z += offset.z;
    }
    return position;
}

static bool execute_actor_spawn_action(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    actor_pool_runtime *pool = find_actor_pool(runtime, json_string(action, "pool", NULL));
    int actor_index = -1;
    sdl3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
    if (pool == NULL || actor == NULL || actor_index < 0)
        return false;

    actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
    if (!initialize_pooled_actor(pool, actor, actor_index, true))
        return false;
    if (pool->spawn_generations != NULL)
    {
        pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
        sdl3d_properties_set_int(actor->props, "pool_spawn_generation",
                                 (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
    }
    actor_set_position(actor, actor_spawn_position_from_action(runtime, action, actor->position));
    apply_actor_spawn_properties(actor, obj_get(action, "properties"));

    const char *actor_key = json_string(action, "output_actor_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && actor_key != NULL)
        sdl3d_properties_set_string(runtime->scene_state, actor_key, actor->name);
    const char *id_key = json_string(action, "output_id_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && id_key != NULL)
        sdl3d_properties_set_int(runtime->scene_state, id_key, actor->id);
    const char *pool_index_key = json_string(action, "output_pool_index_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && pool_index_key != NULL)
        sdl3d_properties_set_int(runtime->scene_state, pool_index_key, actor_index);
    return true;
}

static bool execute_actor_despawn_action(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, target);
    if (actor == NULL)
        return false;

    int actor_index = -1;
    actor_pool_runtime *pool = find_actor_pool_for_actor(runtime, actor->name, &actor_index);
    if (pool != NULL && actor_index >= 0)
        return actor_pool_request_despawn(runtime, pool, actor, actor_index);

    actor->active = false;
    return true;
}

static bool execute_actor_despawn_by_tag_action(sdl3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *tag = json_string(action, "tag", NULL);
    if (runtime == NULL || tag == NULL || tag[0] == '\0')
        return false;

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!entity_json_has_tags(pool->archetype_json, &tag, 1))
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index))
            {
                (void)actor_pool_request_despawn(runtime, pool, actor, actor_index);
            }
        }
    }
    return true;
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

    if (SDL_strcmp(type, "property.snapshot") == 0)
        return snapshot_actor_properties(runtime, action);

    if (SDL_strcmp(type, "property.restore_snapshot") == 0)
        return restore_actor_property_snapshot(runtime, action);

    if (SDL_strcmp(type, "property.animate") == 0)
        return start_property_animation_from_json(runtime, action);

    if (SDL_strcmp(type, "property.reset_defaults") == 0)
        return reset_actor_properties_to_authored_defaults(runtime, action);

    if (SDL_strcmp(type, "input.reset_bindings") == 0)
        return sdl3d_game_data_reset_menu_input_bindings(runtime, json_string(action, "menu", NULL));

    if (SDL_strcmp(type, "input.apply_profile") == 0)
    {
        char error[256] = {0};
        sdl3d_input_manager *input = runtime_input(runtime);
        const char *profile = json_string(action, "profile", NULL);
        if (!sdl3d_game_data_apply_input_profile(runtime, input, profile, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data input profile apply failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        return true;
    }

    if (SDL_strcmp(type, "input.apply_active_profile") == 0)
    {
        char error[256] = {0};
        const char *profile = NULL;
        sdl3d_input_manager *input = runtime_input(runtime);
        if (!sdl3d_game_data_apply_active_input_profile(runtime, input, &profile, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data active input profile apply failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "game data active input profile applied: profile=%s",
                    profile != NULL ? profile : "<none>");
        return true;
    }

    if (SDL_strcmp(type, "input.clear_network_input_overrides") == 0)
    {
        char error[256] = {0};
        sdl3d_input_manager *input = runtime_input(runtime);
        const char *channel = json_string(action, "channel", NULL);
        if (!sdl3d_game_data_clear_network_input_overrides(runtime, channel, input, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data network input override clear failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        return true;
    }

    if (SDL_strcmp(type, "scene_state.set") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        yyjson_val *value = obj_get(action, "value");
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0' || value == NULL)
            return false;
        return set_property_from_json_with_payload(runtime->scene_state, key, value, payload);
    }

    if (SDL_strcmp(type, "network.direct_connect.start") == 0)
    {
        const char *name = json_string(action, "name", NULL);
        const char *host_key = json_string(action, "host_key", NULL);
        const char *port_key = json_string(action, "port_key", NULL);
        const char *host = runtime != NULL && host_key != NULL
                               ? sdl3d_properties_get_string(runtime->scene_state, host_key,
                                                             json_string(action, "default_host", "127.0.0.1"))
                               : json_string(action, "host", json_string(action, "default_host", "127.0.0.1"));
        const int default_port = json_int(action, "default_port", SDL3D_NETWORK_DEFAULT_PORT);
        const char *port_text = runtime != NULL && port_key != NULL
                                    ? sdl3d_properties_get_string(runtime->scene_state, port_key, NULL)
                                    : NULL;
        const int port = port_text != NULL ? SDL_atoi(port_text) : json_int_or_string(action, "port", default_port);
        return sdl3d_game_data_network_direct_connect_start(
            runtime, name, host, port, json_string(action, "status_key", NULL), json_string(action, "state_key", NULL),
            json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.direct_connect.cancel") == 0)
    {
        char status[256];
        const char *status_text = json_string(action, "status", "Disconnected");
        (void)format_payload_string(payload, status_text, status, sizeof(status));
        return sdl3d_game_data_network_direct_connect_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL), status);
    }

    if (SDL_strcmp(type, "network.direct_connect.observe") == 0)
    {
        return sdl3d_game_data_network_direct_connect_publish_status(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.host.start") == 0)
    {
        const int default_port = json_int(action, "default_port", SDL3D_NETWORK_DEFAULT_PORT);
        const int port = json_int_or_string(action, "port", default_port);
        return sdl3d_game_data_network_host_start(
            runtime, json_string(action, "name", NULL), port,
            json_string(action, "session_name", json_string(action, "advertised_name", NULL)),
            json_string(action, "status_key", NULL), json_string(action, "endpoint_key", NULL),
            json_string(action, "peer_key", NULL), json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.host.cancel") == 0)
    {
        char status[256];
        const char *status_text = json_string(action, "status", "Not hosting");
        (void)format_payload_string(payload, status_text, status, sizeof(status));
        return sdl3d_game_data_network_host_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "endpoint_key", NULL), json_string(action, "peer_key", NULL),
            json_string(action, "connected_key", NULL), status);
    }

    if (SDL_strcmp(type, "network.host.observe") == 0)
    {
        return sdl3d_game_data_network_host_publish_status(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "endpoint_key", NULL), json_string(action, "peer_key", NULL),
            json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.start") == 0 || SDL_strcmp(type, "network.discovery.refresh") == 0)
    {
        const int default_port = json_int(action, "default_port", SDL3D_NETWORK_DEFAULT_PORT);
        const int port = json_int_or_string(action, "port", default_port);
        const int local_port = json_int_or_string(action, "local_port", 0);
        return sdl3d_game_data_network_discovery_start(
            runtime, json_string(action, "name", NULL), json_string(action, "host", NULL), port, local_port,
            json_string(action, "collection", NULL), json_string(action, "status_key", NULL),
            json_string(action, "count_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.observe") == 0)
    {
        return sdl3d_game_data_network_discovery_update(
            runtime, json_string(action, "name", NULL),
            json_float(action, "dt", json_float(action, "update_seconds", 0.016f)),
            json_string(action, "collection", NULL), json_string(action, "status_key", NULL),
            json_string(action, "count_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.cancel") == 0)
    {
        return sdl3d_game_data_network_discovery_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "collection", NULL),
            json_string(action, "status_key", NULL), json_string(action, "count_key", NULL),
            json_string(action, "status", "Discovery canceled"));
    }

    if (SDL_strcmp(type, "network.discovery.connect_selected") == 0)
    {
        const char *index_key = json_string(action, "selected_index_key", NULL);
        const int selected_index =
            runtime != NULL && runtime->scene_state != NULL && index_key != NULL
                ? sdl3d_properties_get_int(runtime->scene_state, index_key, json_int(action, "selected_index", 0))
                : json_int(action, "selected_index", 0);
        return sdl3d_game_data_network_discovery_connect_selected(
            runtime, json_string(action, "name", NULL), json_string(action, "collection", NULL), selected_index,
            json_string(action, "direct_connect_name", NULL), json_string(action, "host_key", NULL),
            json_string(action, "port_key", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL),
            json_string(action, "connecting_status", NULL));
    }

    if (SDL_strcmp(type, "ui.animate") == 0)
        return start_ui_animation_from_json(runtime, action);

    if (SDL_strncmp(type, "audio.", 6) == 0)
        return execute_audio_action(runtime, action, type);

    if (SDL_strncmp(type, "persistence.", 12) == 0)
        return execute_persistence_action(runtime, action, type);

    if (SDL_strcmp(type, "entity.set_active") == 0)
    {
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        yyjson_val *active = obj_get(action, "active");
        if (actor == NULL || !yyjson_is_bool(active))
            return false;
        actor->active = yyjson_get_bool(active);
        return true;
    }

    if (SDL_strcmp(type, "actor.spawn") == 0)
        return execute_actor_spawn_action(runtime, action);

    if (SDL_strcmp(type, "actor.despawn") == 0)
        return execute_actor_despawn_action(runtime, action);

    if (SDL_strcmp(type, "actor.despawn_by_tag") == 0)
        return execute_actor_despawn_by_tag_action(runtime, action);

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

    if (SDL_strcmp(type, "camera.set") == 0)
    {
        const char *camera = json_string(action, "camera", NULL);
        if (camera == NULL)
            return false;
        runtime->active_camera = camera;
        return true;
    }

    if (SDL_strcmp(type, "scene.set") == 0)
    {
        yyjson_val *authored_payload = obj_get(action, "payload");
        if (authored_payload == NULL)
            return sdl3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL), payload);

        sdl3d_properties *scene_payload = properties_from_json_payload(authored_payload, payload);
        if (scene_payload == NULL)
            return false;
        const bool ok =
            sdl3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL), scene_payload);
        sdl3d_properties_destroy(scene_payload);
        return ok;
    }

    if (SDL_strcmp(type, "adapter.invoke") == 0)
    {
        const char *adapter_name = json_string(action, "adapter", NULL);
        adapter_entry *adapter = find_adapter(runtime, adapter_name);
        if (adapter == NULL)
            return false;
        const char *target_name = json_string(action, "target", NULL);
        if ((target_name == NULL || target_name[0] == '\0') && payload != NULL)
            target_name = sdl3d_properties_get_string(payload, "actor_name", NULL);
        sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, target_name);
        return invoke_adapter(runtime, adapter, target, payload);
    }

    if (SDL_strcmp(type, "branch") == 0)
    {
        yyjson_val *condition = obj_get(action, "if");
        const bool passed = eval_data_condition(runtime, condition, NULL);
        return execute_action_array(runtime, obj_get(action, passed ? "then" : "else"), payload);
    }

    return false;
}

static bool execute_action_array(sdl3d_game_data_runtime *runtime, yyjson_val *actions, const sdl3d_properties *payload)
{
    if (!yyjson_is_arr(actions))
        return false;

    actor_lifecycle_defer_begin(runtime);
    bool ok = true;
    for (size_t i = 0; i < yyjson_arr_size(actions); ++i)
    {
        yyjson_val *action = yyjson_arr_get(actions, i);
        if (yyjson_is_obj(action))
            ok = execute_one_action(runtime, action, payload) && ok;
    }
    actor_lifecycle_defer_end(runtime);
    return ok;
}

static bool execute_optional_action_array(sdl3d_game_data_runtime *runtime, yyjson_val *actions,
                                          const sdl3d_properties *payload)
{
    if (actions == NULL)
        return true;
    return execute_action_array(runtime, actions, payload);
}

static yyjson_val *active_scene_activity_json(const sdl3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *activity = obj_get(scene != NULL ? scene->root : NULL, "activity");
    if (!yyjson_is_obj(activity) || !json_bool(activity, "enabled", true))
        return NULL;
    return activity;
}

static bool ensure_activity_periodic_capacity(scene_activity_state *state, int required)
{
    if (state == NULL)
        return false;
    if (required <= state->periodic_capacity)
        return true;

    int next_capacity = state->periodic_capacity < 4 ? 4 : state->periodic_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    float *elapsed = (float *)SDL_realloc(state->periodic_elapsed, (size_t)next_capacity * sizeof(*elapsed));
    if (elapsed == NULL)
        return false;
    SDL_memset(elapsed + state->periodic_capacity, 0,
               (size_t)(next_capacity - state->periodic_capacity) * sizeof(*elapsed));
    state->periodic_elapsed = elapsed;
    state->periodic_capacity = next_capacity;
    return true;
}

static bool activity_reset_for_scene(sdl3d_game_data_runtime *runtime, const char *scene, yyjson_val *activity)
{
    scene_activity_state *state = &runtime->activity;
    state->scene = scene;
    state->idle_elapsed = 0.0f;
    state->idle = false;
    state->entered = false;

    const int periodic_count = (int)yyjson_arr_size(obj_get(activity, "periodic"));
    if (!ensure_activity_periodic_capacity(state, periodic_count))
        return false;
    state->periodic_count = periodic_count;
    if (periodic_count > 0)
        SDL_memset(state->periodic_elapsed, 0, (size_t)periodic_count * sizeof(*state->periodic_elapsed));
    return true;
}

static bool activity_input_matches(const sdl3d_game_data_runtime *runtime, yyjson_val *activity,
                                   const sdl3d_input_manager *input)
{
    if (runtime == NULL || activity == NULL || input == NULL)
        return false;

    const char *mode = json_string(activity, "input", "any");
    if (SDL_strcmp(mode, "disabled") == 0 || SDL_strcmp(mode, "none") == 0)
        return false;
    if (SDL_strcmp(mode, "action") == 0)
    {
        const int action_id = sdl3d_game_data_find_action(runtime, json_string(activity, "action", NULL));
        return action_id >= 0 && sdl3d_input_is_pressed(input, action_id);
    }
    return sdl3d_input_any_pressed(input);
}

bool sdl3d_game_data_scene_activity_consumes_wake_input(const sdl3d_game_data_runtime *runtime,
                                                        const sdl3d_input_manager *input, bool *out_block_menus,
                                                        bool *out_block_scene_shortcuts)
{
    if (out_block_menus != NULL)
        *out_block_menus = false;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = false;
    if (runtime == NULL || input == NULL)
        return false;

    yyjson_val *activity = active_scene_activity_json(runtime);
    const char *active_scene = sdl3d_game_data_active_scene(runtime);
    const scene_activity_state *state = &runtime->activity;
    if (activity == NULL || state->scene != active_scene || !state->idle ||
        !activity_input_matches(runtime, activity, input))
    {
        return false;
    }

    const bool consume = json_bool(activity, "consume_wake_input", false);
    if (out_block_menus != NULL)
        *out_block_menus = json_bool(activity, "block_menus_on_wake", consume);
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = json_bool(activity, "block_scene_shortcuts_on_wake", consume);
    return consume;
}

bool sdl3d_game_data_update_scene_activity(sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input, float dt)
{
    if (runtime == NULL)
        return false;

    if (dt < 0.0f)
        dt = 0.0f;

    yyjson_val *activity = active_scene_activity_json(runtime);
    const char *active_scene = sdl3d_game_data_active_scene(runtime);
    scene_activity_state *state = &runtime->activity;
    if (activity == NULL)
    {
        state->scene = active_scene;
        state->idle_elapsed = 0.0f;
        state->idle = false;
        state->entered = false;
        state->periodic_count = 0;
        return true;
    }

    if (state->scene != active_scene)
    {
        if (!activity_reset_for_scene(runtime, active_scene, activity))
            return false;
    }

    bool ok = true;
    if (!state->entered)
    {
        ok = execute_optional_action_array(runtime, obj_get(activity, "on_enter"), NULL) && ok;
        state->entered = true;
    }

    const bool input_active = activity_input_matches(runtime, activity, input);
    if (input_active)
    {
        state->idle_elapsed = 0.0f;
        if (json_bool(activity, "reset_periodic_on_input", true) && state->periodic_count > 0)
            SDL_memset(state->periodic_elapsed, 0, (size_t)state->periodic_count * sizeof(*state->periodic_elapsed));
        if (state->idle)
        {
            state->idle = false;
            ok = execute_optional_action_array(runtime, obj_get(activity, "on_active"), NULL) && ok;
        }
    }
    else
    {
        state->idle_elapsed += dt;
    }

    const float idle_after = json_float(activity, "idle_after", json_float(activity, "idle_seconds", -1.0f));
    if (idle_after >= 0.0f && !state->idle && state->idle_elapsed >= idle_after)
    {
        state->idle = true;
        ok = execute_optional_action_array(runtime, obj_get(activity, "on_idle"), NULL) && ok;
    }

    yyjson_val *periodic = obj_get(activity, "periodic");
    const int periodic_count = (int)yyjson_arr_size(periodic);
    if (periodic_count != state->periodic_count && !activity_reset_for_scene(runtime, active_scene, activity))
        return false;
    for (int i = 0; i < state->periodic_count; ++i)
    {
        yyjson_val *entry = yyjson_arr_get(periodic, (size_t)i);
        if (!yyjson_is_obj(entry))
            continue;
        const float interval = json_float(entry, "interval", 0.0f);
        if (interval <= 0.0f)
            continue;
        state->periodic_elapsed[i] += dt;
        if (state->periodic_elapsed[i] < interval)
            continue;

        state->periodic_elapsed[i] = SDL_fmodf(state->periodic_elapsed[i], interval);
        ok = execute_optional_action_array(runtime, obj_get(entry, "actions"), NULL) && ok;
        if (json_bool(entry, "reset_idle", false))
        {
            state->idle_elapsed = 0.0f;
            state->idle = false;
        }
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
        entry->entity_tag = json_string(sensor, "a_tag", NULL);
        entry->other_tag = json_string(sensor, "b_tag", NULL);
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

static const char *scene_file_entry_package(yyjson_val *entry)
{
    yyjson_val *package = obj_get(entry, "package");
    return yyjson_is_str(package) ? yyjson_get_str(package) : NULL;
}

static int scene_source_count(yyjson_val *files, char *error_buffer, int error_buffer_size)
{
    int count = 0;
    for (size_t i = 0; yyjson_is_arr(files) && i < yyjson_arr_size(files); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(files, i);
        if (yyjson_is_str(entry))
        {
            count++;
            continue;
        }

        const char *package = scene_file_entry_package(entry);
        if (package != NULL && SDL_strcmp(package, "standard_options") == 0)
        {
            count += SDL3D_STANDARD_OPTIONS_SCENE_COUNT;
            continue;
        }

        set_error(error_buffer, error_buffer_size, "scene files must be strings or known package objects");
        return -1;
    }
    return count;
}

static bool install_scene_doc(sdl3d_game_data_runtime *runtime, yyjson_doc *doc, int scene_index, char *error_buffer,
                              int error_buffer_size)
{
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
    for (int prior = 0; prior < scene_index; ++prior)
    {
        if (SDL_strcmp(runtime->scenes[prior].name, name) == 0)
        {
            yyjson_doc_free(doc);
            set_error(error_buffer, error_buffer_size, "duplicate scene name");
            return false;
        }
    }

    runtime->scenes[scene_index].doc = doc;
    runtime->scenes[scene_index].root = scene_root;
    runtime->scenes[scene_index].name = name;
    if (!load_scene_entities(&runtime->scenes[scene_index], error_buffer, error_buffer_size) ||
        !load_scene_menus(&runtime->scenes[scene_index], error_buffer, error_buffer_size))
        return false;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "SDL3D game data scene loaded: name=%s updates_game=%d renders_world=%d entities=%d menus=%d", name,
                 json_bool(scene_root, "updates_game", true) ? 1 : 0,
                 json_bool(scene_root, "renders_world", true) ? 1 : 0, runtime->scenes[scene_index].entity_count,
                 runtime->scenes[scene_index].menu_count);
    return true;
}

static bool load_scene_file(sdl3d_game_data_runtime *runtime, const char *file_path, int scene_index,
                            char *error_buffer, int error_buffer_size)
{
    if (file_path == NULL || file_path[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "scene files must be non-empty strings");
        return false;
    }

    char *resolved_path = path_join(runtime->base_dir, file_path);
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
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "scene asset %s could not be read: %s", file_path,
                         asset_error);
        }
        SDL_free(resolved_path);
        return false;
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)scene_buffer.data, scene_buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    sdl3d_asset_buffer_free(&scene_buffer);
    SDL_free(resolved_path);
    if (doc == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "scene yyjson error %u at byte %llu: %s", err.code,
                         (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
        }
        return false;
    }

    return install_scene_doc(runtime, doc, scene_index, error_buffer, error_buffer_size);
}

static bool load_scene_package(sdl3d_game_data_runtime *runtime, yyjson_val *root, const char *package,
                               int *scene_index, char *error_buffer, int error_buffer_size)
{
    sdl3d_standard_options_scene_docs docs;
    if (!sdl3d_standard_options_build_scene_docs(root, package, &docs, error_buffer, error_buffer_size))
        return false;

    bool ok = true;
    for (int i = 0; ok && i < docs.count; ++i)
    {
        yyjson_doc *doc = docs.docs[i];
        docs.docs[i] = NULL;
        ok = install_scene_doc(runtime, doc, *scene_index, error_buffer, error_buffer_size);
        if (ok)
            (*scene_index)++;
    }
    sdl3d_standard_options_scene_docs_free(&docs);
    return ok;
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

    runtime->scene_count = scene_source_count(files, error_buffer, error_buffer_size);
    if (runtime->scene_count < 0)
        return false;
    runtime->scenes = (scene_entry *)SDL_calloc((size_t)runtime->scene_count, sizeof(*runtime->scenes));
    if (runtime->scenes == NULL && runtime->scene_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate scene table");
        return false;
    }

    int scene_index = 0;
    for (size_t i = 0; i < yyjson_arr_size(files); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(files, i);
        if (yyjson_is_str(entry))
        {
            if (!load_scene_file(runtime, yyjson_get_str(entry), scene_index, error_buffer, error_buffer_size))
                return false;
            scene_index++;
            continue;
        }

        const char *package = scene_file_entry_package(entry);
        if (package != NULL)
        {
            if (!load_scene_package(runtime, root, package, &scene_index, error_buffer, error_buffer_size))
                return false;
            continue;
        }

        set_error(error_buffer, error_buffer_size, "scene files must be strings or known package objects");
        return false;
    }

    if (scene_index != runtime->scene_count)
    {
        set_error(error_buffer, error_buffer_size, "scene package generated an unexpected scene count");
        return false;
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
            yyjson_val *enabled_if = obj_get(component, "enabled_if");
            if (enabled_if != NULL && !eval_data_condition(runtime, enabled_if, NULL))
            {
                continue;
            }
            if (SDL_strcmp(type, "control.axis_1d") == 0 && input != NULL)
            {
                const int negative = sdl3d_game_data_find_action(runtime, json_string(component, "negative", NULL));
                const int positive = sdl3d_game_data_find_action(runtime, json_string(component, "positive", NULL));
                if ((negative >= 0 && !sdl3d_game_data_active_scene_allows_action(runtime, negative)) ||
                    (positive >= 0 && !sdl3d_game_data_active_scene_allows_action(runtime, positive)))
                {
                    continue;
                }
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
            const char *type = json_string(component, "type", "");
            if (!sdl3d_properties_get_bool(actor->props, "active_motion", true))
            {
                continue;
            }

            if (SDL_strcmp(type, "motion.velocity_2d") == 0)
            {
                const char *property = json_string(component, "property", "velocity");
                const sdl3d_vec3 velocity = actor_vec_property(actor, property);
                actor_set_position(actor, sdl3d_vec3_make(actor->position.x + velocity.x * dt,
                                                          actor->position.y + velocity.y * dt, actor->position.z));
            }
            else if (SDL_strcmp(type, "motion.oscillate") == 0)
            {
                const char *time_property = json_string(component, "time_property", "motion_time");
                const float time = sdl3d_properties_get_float(actor->props, time_property, 0.0f) + dt;
                sdl3d_properties_set_float(actor->props, time_property, time);

                const sdl3d_vec3 origin = json_vec3_value(obj_get(component, "origin"), actor->position);
                const sdl3d_vec3 amplitude =
                    json_vec3_value(obj_get(component, "amplitude"), sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
                const float rate = json_float(component, "rate", 1.0f);
                const float phase = json_float(component, "phase", 0.0f);
                const float wave = SDL_sinf(time * rate + phase);
                actor_set_position(actor, sdl3d_vec3_make(origin.x + amplitude.x * wave, origin.y + amplitude.y * wave,
                                                          origin.z + amplitude.z * wave));
            }
            else if (SDL_strcmp(type, "motion.spin") == 0)
            {
                const char *property = json_string(component, "property", "rotation_angle");
                const float rate = json_float(component, "rate", 1.0f);
                float angle = sdl3d_properties_get_float(actor->props, property, 0.0f) + rate * dt;
                const float two_pi = 6.28318530717958647692f;
                if (angle > two_pi || angle < -two_pi)
                    angle = SDL_fmodf(angle, two_pi);
                sdl3d_properties_set_float(actor->props, property, angle);
            }
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

static bool runtime_actor_is_active(const sdl3d_game_data_runtime *runtime, const sdl3d_registered_actor *actor)
{
    if (runtime == NULL || actor == NULL)
        return false;
    int actor_index = -1;
    const actor_pool_runtime *pool = find_actor_pool_for_actor_const(runtime, actor->name, &actor_index);
    if (pool != NULL)
        return actor_pool_actor_is_active(pool, actor, actor_index);
    return actor->active;
}

static bool sensor_actor_list_add(sensor_actor_list *list, sdl3d_registered_actor *actor)
{
    if (list == NULL || actor == NULL)
        return false;
    if (list->count >= list->capacity)
    {
        const int new_capacity = list->capacity > 0 ? list->capacity * 2 : 8;
        sdl3d_registered_actor **items =
            (sdl3d_registered_actor **)SDL_realloc(list->items, (size_t)new_capacity * sizeof(*items));
        if (items == NULL)
            return false;
        list->items = items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = actor;
    return true;
}

static void sensor_actor_list_free(sensor_actor_list *list)
{
    if (list == NULL)
        return;
    SDL_free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool collect_sensor_endpoint_actors(sdl3d_game_data_runtime *runtime, const char *actor_name, const char *tag,
                                           sensor_actor_list *out_list)
{
    if (runtime == NULL || out_list == NULL)
        return false;

    if (actor_name != NULL)
    {
        if (!active_scene_has_entity_internal(runtime, actor_name))
            return true;
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, actor_name);
        if (runtime_actor_is_active(runtime, actor))
            return sensor_actor_list_add(out_list, actor);
        return true;
    }

    if (tag == NULL || tag[0] == '\0')
        return true;

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *tags[1] = {tag};
        if (!entity_json_has_tags(entity, tags, 1))
            continue;
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, entity_name);
        if (runtime_actor_is_active(runtime, actor) && !sensor_actor_list_add(out_list, actor))
            return false;
    }

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        const char *tags[1] = {tag};
        if (!entity_json_has_tags(pool->archetype_json, tags, 1) ||
            !actor_pool_in_scene(pool, sdl3d_game_data_active_scene(runtime)))
        {
            continue;
        }
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index) && !sensor_actor_list_add(out_list, actor))
                return false;
        }
    }
    return true;
}

static sensor_contact_pair_state *sensor_contact_pair_state_for(sensor_entry *sensor, const char *actor_name,
                                                                const char *other_actor_name)
{
    if (sensor == NULL || actor_name == NULL || other_actor_name == NULL)
        return NULL;
    for (int i = 0; i < sensor->contact_pair_count; ++i)
    {
        sensor_contact_pair_state *state = &sensor->contact_pairs[i];
        if (state->actor_name != NULL && state->other_actor_name != NULL &&
            SDL_strcmp(state->actor_name, actor_name) == 0 &&
            SDL_strcmp(state->other_actor_name, other_actor_name) == 0)
        {
            return state;
        }
    }

    if (sensor->contact_pair_count >= sensor->contact_pair_capacity)
    {
        const int new_capacity = sensor->contact_pair_capacity > 0 ? sensor->contact_pair_capacity * 2 : 8;
        sensor_contact_pair_state *pairs = (sensor_contact_pair_state *)SDL_realloc(
            sensor->contact_pairs, (size_t)new_capacity * sizeof(*sensor->contact_pairs));
        if (pairs == NULL)
            return NULL;
        SDL_memset(pairs + sensor->contact_pair_capacity, 0,
                   (size_t)(new_capacity - sensor->contact_pair_capacity) * sizeof(*pairs));
        sensor->contact_pairs = pairs;
        sensor->contact_pair_capacity = new_capacity;
    }

    sensor_contact_pair_state *state = &sensor->contact_pairs[sensor->contact_pair_count++];
    SDL_zero(*state);
    state->actor_name = actor_name;
    state->other_actor_name = other_actor_name;
    return state;
}

static bool actors_contact_2d(const sdl3d_registered_actor *a, const sdl3d_registered_actor *b)
{
    if (a == NULL || b == NULL)
        return false;

    const float a_radius = sdl3d_properties_get_float(a->props, "radius", 0.0f);
    const float b_radius = sdl3d_properties_get_float(b->props, "radius", 0.0f);
    const float a_half_width = sdl3d_properties_get_float(a->props, "half_width", 0.0f);
    const float a_half_height = sdl3d_properties_get_float(a->props, "half_height", 0.0f);
    const float b_half_width = sdl3d_properties_get_float(b->props, "half_width", 0.0f);
    const float b_half_height = sdl3d_properties_get_float(b->props, "half_height", 0.0f);

    if (a_radius > 0.0f && b_half_width > 0.0f && b_half_height > 0.0f)
    {
        const float closest_x = SDL_clamp(a->position.x, b->position.x - b_half_width, b->position.x + b_half_width);
        const float closest_y = SDL_clamp(a->position.y, b->position.y - b_half_height, b->position.y + b_half_height);
        const float dx = a->position.x - closest_x;
        const float dy = a->position.y - closest_y;
        return dx * dx + dy * dy <= a_radius * a_radius;
    }
    if (a_radius > 0.0f && b_radius > 0.0f)
    {
        const float dx = a->position.x - b->position.x;
        const float dy = a->position.y - b->position.y;
        const float radius = a_radius + b_radius;
        return dx * dx + dy * dy <= radius * radius;
    }
    if (a_half_width > 0.0f && a_half_height > 0.0f && b_half_width > 0.0f && b_half_height > 0.0f)
    {
        return SDL_fabsf(a->position.x - b->position.x) <= a_half_width + b_half_width &&
               SDL_fabsf(a->position.y - b->position.y) <= a_half_height + b_half_height;
    }
    if (a_half_width > 0.0f && a_half_height > 0.0f && b_radius > 0.0f)
    {
        return actors_contact_2d(b, a);
    }
    return false;
}

static void sensor_contact_states_begin(sensor_entry *sensor)
{
    if (sensor == NULL)
        return;
    for (int i = 0; i < sensor->contact_pair_count; ++i)
        sensor->contact_pairs[i].seen = false;
}

static void sensor_contact_states_end(sensor_entry *sensor)
{
    if (sensor == NULL)
        return;
    bool any_active = false;
    for (int i = 0; i < sensor->contact_pair_count; ++i)
    {
        if (!sensor->contact_pairs[i].seen)
            sensor->contact_pairs[i].active = false;
        any_active = any_active || sensor->contact_pairs[i].active;
    }
    sensor->was_active = any_active;
}

static void update_sensor_contact_pair(sdl3d_game_data_runtime *runtime, sensor_entry *sensor,
                                       sdl3d_registered_actor *actor, sdl3d_registered_actor *other)
{
    if (!runtime_actor_is_active(runtime, actor) || !runtime_actor_is_active(runtime, other))
        return;

    sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, actor->name, other->name);
    if (state == NULL)
        return;

    const bool active = actors_contact_2d(actor, other);
    if (active && !state->active)
        emit_sensor_signal(runtime, sensor, actor, other);
    state->active = active;
    state->seen = true;
}

static void update_contact_sensor(sdl3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list actors;
    sensor_actor_list others;
    SDL_zero(actors);
    SDL_zero(others);

    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &actors) &&
        collect_sensor_endpoint_actors(runtime, sensor->other, sensor->other_tag, &others))
    {
        for (int a = 0; a < actors.count; ++a)
        {
            for (int b = 0; b < others.count; ++b)
            {
                if (actors.items[a] != others.items[b])
                    update_sensor_contact_pair(runtime, sensor, actors.items[a], others.items[b]);
            }
        }
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&actors);
    sensor_actor_list_free(&others);
}

static void update_sensors(sdl3d_game_data_runtime *runtime)
{
    for (int i = 0; i < runtime->sensor_count; ++i)
    {
        sensor_entry *sensor = &runtime->sensors[i];
        if (sensor->type == GAME_DATA_SENSOR_CONTACT_2D)
        {
            update_contact_sensor(runtime, sensor);
            continue;
        }

        if (sensor->entity != NULL && !active_scene_has_entity_internal(runtime, sensor->entity))
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
    actor_lifecycle_defer_begin(runtime);
    update_control_components(runtime, root, dt);
    update_motion_components(runtime, root, dt);
    update_sensors(runtime);
    actor_lifecycle_defer_end(runtime);
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
    out_config->width = json_int(app, "window_width", json_int(app, "width", out_config->width));
    out_config->height = json_int(app, "window_height", json_int(app, "height", out_config->height));
    out_config->logical_width = json_int(app, "logical_width", json_int(app, "width", out_config->logical_width));
    out_config->logical_height = json_int(app, "logical_height", json_int(app, "height", out_config->logical_height));
    out_config->icon_path = json_string(app, "icon_path", json_string(app, "icon", out_config->icon_path));
    out_config->backend = parse_backend(json_string(app, "backend", NULL), out_config->backend);
    yyjson_val *window = obj_get(app, "window");
    if (yyjson_is_obj(window))
    {
        const char *window_title = json_string(window, "title", NULL);
        if (window_title != NULL && title_buffer != NULL && title_buffer_size > 0)
        {
            SDL_snprintf(title_buffer, (size_t)title_buffer_size, "%s", window_title);
            out_config->title = title_buffer;
        }
#if defined(SDL3D_PRODUCTION_BUILD)
        const char *mode = json_string(window, "production_display_mode", json_string(window, "display_mode", NULL));
#else
        const char *mode = json_string(window, "development_display_mode", json_string(window, "display_mode", NULL));
#endif
        out_config->display_mode = parse_window_mode(mode, out_config->display_mode);
        yyjson_val *vsync = obj_get(window, "vsync");
        if (yyjson_is_bool(vsync))
            out_config->vsync = yyjson_get_bool(vsync) ? 1 : -1;
        yyjson_val *maximized = obj_get(window, "maximized");
        if (yyjson_is_bool(maximized))
            out_config->maximized = yyjson_get_bool(maximized) ? 1 : -1;
        out_config->backend = parse_backend(json_string(window, "renderer", NULL), out_config->backend);
        out_config->icon_path = json_string(window, "icon_path", json_string(window, "icon", out_config->icon_path));
    }
    out_config->tick_rate = json_float(app, "tick_rate", out_config->tick_rate);
    out_config->max_ticks_per_frame = json_int(app, "max_ticks_per_frame", out_config->max_ticks_per_frame);
    out_config->enable_audio = json_bool(app, "enable_audio", out_config->enable_audio);
    return true;
}

static void apply_persisted_app_settings(yyjson_val *root, sdl3d_game_config *out_config)
{
    yyjson_val *app = obj_get(root, "app");
    const char *settings_path = json_string(app, "settings_path", NULL);
    if (settings_path == NULL || settings_path[0] == '\0')
        return;

    sdl3d_storage_config storage_config;
    storage_config_from_root(root, &storage_config);

    char storage_error[256];
    sdl3d_storage *storage = NULL;
    if (!sdl3d_storage_create(&storage_config, &storage, storage_error, (int)sizeof(storage_error)))
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SDL3D app settings storage unavailable: %s", storage_error);
        return;
    }

    sdl3d_storage_buffer buffer;
    SDL_zero(buffer);
    if (!sdl3d_storage_read_file(storage, settings_path, &buffer, storage_error, (int)sizeof(storage_error)))
    {
        sdl3d_storage_destroy(storage);
        return;
    }

    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, NULL);
    yyjson_val *settings = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    if (yyjson_is_obj(settings))
    {
        out_config->display_mode =
            parse_window_mode(json_string(settings, "display_mode", NULL), out_config->display_mode);
        out_config->backend = parse_backend(json_string(settings, "renderer", NULL), out_config->backend);
        yyjson_val *vsync = obj_get(settings, "vsync");
        if (yyjson_is_bool(vsync))
            out_config->vsync = yyjson_get_bool(vsync) ? 1 : -1;
    }

    yyjson_doc_free(doc);
    sdl3d_storage_buffer_free(&buffer);
    sdl3d_storage_destroy(storage);
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

    yyjson_val *root = yyjson_doc_get_root(doc);
    const bool ok =
        apply_app_config_from_root(root, out_config, title_buffer, title_buffer_size, error_buffer, error_buffer_size);
    if (ok)
        apply_persisted_app_settings(root, out_config);
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
    if (!sdl3d_game_data_network_schema_hash(root, runtime->network_schema_hash, &runtime->has_network_schema))
    {
        sdl3d_game_data_destroy(runtime);
        set_error(error_buffer, error_buffer_size, "failed to compute network schema hash");
        return false;
    }
    load_storage_config(runtime, root);

    yyjson_val *logic = obj_get(root, "logic");
    load_active_camera(runtime, root);
    bool ok = load_signals(runtime, root, error_buffer, error_buffer_size) &&
              load_entities(runtime, root, error_buffer, error_buffer_size) &&
              load_actor_pools(runtime, root, error_buffer, error_buffer_size) &&
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
    if (ok && out_runtime != NULL && *out_runtime != NULL)
        (*out_runtime)->owns_assets = true;
    else
        sdl3d_asset_resolver_destroy(assets);
    SDL_free(base_dir);
    SDL_free(asset_name);
    return ok;
}

static void storage_config_from_root(yyjson_val *root, sdl3d_storage_config *out_config)
{
    if (out_config == NULL)
        return;
    sdl3d_storage_config_init(out_config);

    yyjson_val *storage = obj_get(root, "storage");
    yyjson_val *metadata = obj_get(root, "metadata");
    yyjson_val *app = obj_get(root, "app");

    out_config->organization =
        first_non_empty_string(json_string(storage, "organization", NULL), json_string(metadata, "organization", NULL),
                               out_config->organization);
    out_config->application = first_non_empty_string(
        json_string(storage, "application", NULL), json_string(app, "title", NULL),
        first_non_empty_string(json_string(metadata, "name", NULL), NULL, out_config->application));
    out_config->profile = json_string(storage, "profile", NULL);
    out_config->user_root_override = json_string(storage, "user_root_override", NULL);
    out_config->cache_root_override = json_string(storage, "cache_root_override", NULL);
}

static void load_storage_config(sdl3d_game_data_runtime *runtime, yyjson_val *root)
{
    if (runtime == NULL)
        return;
    storage_config_from_root(root, &runtime->storage_config);
}

bool sdl3d_game_data_get_storage_config(const sdl3d_game_data_runtime *runtime, sdl3d_storage_config *out_config)
{
    if (runtime == NULL || out_config == NULL)
        return false;

    *out_config = runtime->storage_config;
    return true;
}

bool sdl3d_game_data_has_network_schema(const sdl3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->has_network_schema;
}

bool sdl3d_game_data_get_network_schema_hash(const sdl3d_game_data_runtime *runtime,
                                             Uint8 out_hash[SDL3D_REPLICATION_SCHEMA_HASH_SIZE])
{
    if (runtime == NULL || out_hash == NULL || !runtime->has_network_schema)
        return false;

    SDL_memcpy(out_hash, runtime->network_schema_hash, SDL3D_REPLICATION_SCHEMA_HASH_SIZE);
    return true;
}

bool sdl3d_game_data_get_network_scene_state_key(const sdl3d_game_data_runtime *runtime, const char *scope,
                                                 const char *name, const char **out_key)
{
    if (out_key != NULL)
        *out_key = NULL;
    if (runtime == NULL || scope == NULL || scope[0] == '\0' || name == NULL || name[0] == '\0' || out_key == NULL)
    {
        return false;
    }

    yyjson_val *root = runtime_root(runtime);
    yyjson_val *scene_state = obj_get(obj_get(root, "network"), "scene_state");
    yyjson_val *group = obj_get(scene_state, scope);
    const char *key = json_string(group, name, NULL);
    if (key == NULL || key[0] == '\0')
        return false;

    *out_key = key;
    return true;
}

static yyjson_val *network_session_flow_json(const sdl3d_game_data_runtime *runtime)
{
    return obj_get(obj_get(runtime_root(runtime), "network"), "session_flow");
}

static bool game_data_get_network_session_string(const sdl3d_game_data_runtime *runtime, const char *section,
                                                 const char *name, const char **out_value)
{
    if (out_value != NULL)
        *out_value = NULL;
    if (runtime == NULL || section == NULL || section[0] == '\0' || name == NULL || name[0] == '\0' ||
        out_value == NULL)
    {
        return false;
    }

    const char *value = json_string(obj_get(network_session_flow_json(runtime), section), name, NULL);
    if (value == NULL || value[0] == '\0')
        return false;

    *out_value = value;
    return true;
}

bool sdl3d_game_data_get_network_session_scene(const sdl3d_game_data_runtime *runtime, const char *name,
                                               const char **out_scene)
{
    return game_data_get_network_session_string(runtime, "scenes", name, out_scene);
}

bool sdl3d_game_data_get_network_session_state_key(const sdl3d_game_data_runtime *runtime, const char *name,
                                                   const char **out_key)
{
    return game_data_get_network_session_string(runtime, "state_keys", name, out_key);
}

bool sdl3d_game_data_get_network_session_state_value(const sdl3d_game_data_runtime *runtime, const char *group,
                                                     const char *name, const char **out_value)
{
    if (out_value != NULL)
        *out_value = NULL;
    if (runtime == NULL || group == NULL || group[0] == '\0' || name == NULL || name[0] == '\0' || out_value == NULL)
    {
        return false;
    }

    yyjson_val *groups = obj_get(network_session_flow_json(runtime), "state_values");
    const char *value = json_string(obj_get(groups, group), name, NULL);
    if (value == NULL || value[0] == '\0')
        return false;

    *out_value = value;
    return true;
}

bool sdl3d_game_data_get_network_session_message(const sdl3d_game_data_runtime *runtime, const char *group,
                                                 const char *name, const char **out_message)
{
    if (out_message != NULL)
        *out_message = NULL;
    if (runtime == NULL || group == NULL || group[0] == '\0' || name == NULL || name[0] == '\0' || out_message == NULL)
    {
        return false;
    }

    yyjson_val *groups = obj_get(network_session_flow_json(runtime), "messages");
    const char *value = json_string(obj_get(groups, group), name, NULL);
    if (value == NULL || value[0] == '\0')
        return false;

    *out_message = value;
    return true;
}

static yyjson_val *network_managed_runtime_json(const sdl3d_game_data_runtime *runtime)
{
    return obj_get(network_session_flow_json(runtime), "managed_runtime");
}

bool sdl3d_game_data_network_managed_runtime_enabled(const sdl3d_game_data_runtime *runtime)
{
    return json_bool(network_managed_runtime_json(runtime), "enabled", false);
}

bool sdl3d_game_data_get_network_managed_termination_ack_delay(const sdl3d_game_data_runtime *runtime,
                                                               float *out_seconds)
{
    if (out_seconds != NULL)
        *out_seconds = 0.0f;
    if (runtime == NULL || out_seconds == NULL)
        return false;

    yyjson_val *value = obj_get(network_managed_runtime_json(runtime), "termination_ack_delay_seconds");
    if (!yyjson_is_num(value))
        return false;

    *out_seconds = SDL_max((float)yyjson_get_real(value), 0.0f);
    return true;
}

bool sdl3d_game_data_network_managed_keep_alive_scene_matches(const sdl3d_game_data_runtime *runtime,
                                                              const char *session_name, const char *scene_name)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0' || scene_name == NULL ||
        scene_name[0] == '\0')
    {
        return false;
    }

    yyjson_val *keep_alive = obj_get(network_managed_runtime_json(runtime), "keep_alive_scenes");
    yyjson_val *scenes = obj_get(keep_alive, session_name);
    if (!yyjson_is_arr(scenes))
        return false;

    for (size_t i = 0U; i < yyjson_arr_size(scenes); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(scenes, i);
        const char *semantic = yyjson_is_str(entry) ? yyjson_get_str(entry) : NULL;
        const char *resolved_scene = NULL;
        if (semantic != NULL && sdl3d_game_data_get_network_session_scene(runtime, semantic, &resolved_scene) &&
            resolved_scene != NULL && SDL_strcmp(resolved_scene, scene_name) == 0)
        {
            return true;
        }
    }

    return false;
}

bool sdl3d_game_data_run_network_session_flow_event(sdl3d_game_data_runtime *runtime, sdl3d_game_context *ctx,
                                                    const char *name, const sdl3d_properties *payload,
                                                    char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || name == NULL || name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "network session flow event requires runtime and name");
        return false;
    }

    yyjson_val *events = obj_get(network_session_flow_json(runtime), "events");
    yyjson_val *event = obj_get(events, name);
    if (event == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network session flow event not found");
        return false;
    }

    yyjson_val *actions = event;
    if (yyjson_is_obj(event))
    {
        yyjson_val *pause = obj_get(event, "pause");
        if (pause != NULL && ctx == NULL)
        {
            set_error(error_buffer, error_buffer_size, "network session flow event pause requires a game context");
            return false;
        }
        if (pause != NULL && yyjson_is_bool(pause))
            ctx->paused = yyjson_get_bool(pause);
        actions = obj_get(event, "actions");
    }

    if (!execute_optional_action_array(runtime, actions, payload))
    {
        set_error(error_buffer, error_buffer_size, "network session flow event action failed");
        return false;
    }
    return true;
}

static yyjson_val *network_runtime_bindings_json(const sdl3d_game_data_runtime *runtime)
{
    return obj_get(obj_get(runtime_root(runtime), "network"), "runtime_bindings");
}

static bool game_data_get_network_runtime_binding(const sdl3d_game_data_runtime *runtime, const char *section,
                                                  const char *name, const char **out_value)
{
    if (out_value != NULL)
        *out_value = NULL;
    if (runtime == NULL || section == NULL || section[0] == '\0' || name == NULL || name[0] == '\0' ||
        out_value == NULL)
    {
        return false;
    }

    const char *value = json_string(obj_get(network_runtime_bindings_json(runtime), section), name, NULL);
    if (value == NULL || value[0] == '\0')
        return false;

    *out_value = value;
    return true;
}

bool sdl3d_game_data_get_network_runtime_replication(const sdl3d_game_data_runtime *runtime, const char *name,
                                                     const char **out_channel)
{
    return game_data_get_network_runtime_binding(runtime, "replication", name, out_channel);
}

bool sdl3d_game_data_get_network_runtime_control(const sdl3d_game_data_runtime *runtime, const char *name,
                                                 const char **out_control)
{
    return game_data_get_network_runtime_binding(runtime, "controls", name, out_control);
}

static bool game_data_get_network_runtime_binding_name_for_value(const sdl3d_game_data_runtime *runtime,
                                                                 const char *section, const char *value,
                                                                 const char **out_name)
{
    if (out_name != NULL)
        *out_name = NULL;
    if (runtime == NULL || section == NULL || section[0] == '\0' || value == NULL || value[0] == '\0' ||
        out_name == NULL)
    {
        return false;
    }

    yyjson_val *bindings = obj_get(network_runtime_bindings_json(runtime), section);
    if (!yyjson_is_obj(bindings))
        return false;

    yyjson_val *key = NULL;
    yyjson_val *binding_value = NULL;
    size_t index = 0U;
    size_t max = 0U;
    yyjson_obj_foreach(bindings, index, max, key, binding_value)
    {
        const char *semantic_name = yyjson_is_str(key) ? yyjson_get_str(key) : NULL;
        const char *concrete_value = yyjson_is_str(binding_value) ? yyjson_get_str(binding_value) : NULL;
        if (semantic_name != NULL && concrete_value != NULL && SDL_strcmp(concrete_value, value) == 0)
        {
            *out_name = semantic_name;
            return true;
        }
    }

    return false;
}

bool sdl3d_game_data_get_network_runtime_control_binding(const sdl3d_game_data_runtime *runtime,
                                                         const char *control_name, const char **out_binding)
{
    return game_data_get_network_runtime_binding_name_for_value(runtime, "controls", control_name, out_binding);
}

bool sdl3d_game_data_get_network_runtime_action(const sdl3d_game_data_runtime *runtime, const char *name,
                                                int *out_action)
{
    const char *action_name = NULL;
    if (out_action != NULL)
        *out_action = -1;
    if (out_action == NULL || !game_data_get_network_runtime_binding(runtime, "actions", name, &action_name))
        return false;

    const int action_id = sdl3d_game_data_find_action(runtime, action_name);
    if (action_id < 0)
        return false;

    *out_action = action_id;
    return true;
}

bool sdl3d_game_data_get_network_runtime_signal(const sdl3d_game_data_runtime *runtime, const char *name,
                                                int *out_signal)
{
    const char *signal_name = NULL;
    if (out_signal != NULL)
        *out_signal = -1;
    if (out_signal == NULL || !game_data_get_network_runtime_binding(runtime, "signals", name, &signal_name))
        return false;

    const int signal_id = sdl3d_game_data_find_signal(runtime, signal_name);
    if (signal_id < 0)
        return false;

    *out_signal = signal_id;
    return true;
}

static yyjson_val *haptics_policies_json(const sdl3d_game_data_runtime *runtime)
{
    return obj_get(obj_get(runtime_root(runtime), "haptics"), "policies");
}

static yyjson_val *haptics_policy_json(const sdl3d_game_data_runtime *runtime, int index)
{
    yyjson_val *policies = haptics_policies_json(runtime);
    if (!yyjson_is_arr(policies) || index < 0 || (size_t)index >= yyjson_arr_size(policies))
        return NULL;
    return yyjson_arr_get(policies, (size_t)index);
}

int sdl3d_game_data_haptics_policy_count(const sdl3d_game_data_runtime *runtime)
{
    yyjson_val *policies = haptics_policies_json(runtime);
    return yyjson_is_arr(policies) ? (int)yyjson_arr_size(policies) : 0;
}

bool sdl3d_game_data_get_haptics_policy_at(const sdl3d_game_data_runtime *runtime, int index,
                                           sdl3d_game_data_haptics_policy *out_policy)
{
    if (out_policy != NULL)
        SDL_zero(*out_policy);
    if (runtime == NULL || out_policy == NULL)
        return false;

    yyjson_val *policy = haptics_policy_json(runtime, index);
    if (!yyjson_is_obj(policy))
        return false;

    const char *signal = json_string(policy, "signal", NULL);
    out_policy->name = json_string(policy, "name", NULL);
    out_policy->signal_id = sdl3d_game_data_find_signal(runtime, signal);
    out_policy->low_frequency = json_float(policy, "low_frequency", json_float(policy, "low", 0.0f));
    out_policy->high_frequency = json_float(policy, "high_frequency", json_float(policy, "high", 0.0f));
    out_policy->duration_ms = (Uint32)SDL_max(json_int(policy, "duration_ms", 0), 0);
    return out_policy->name != NULL && out_policy->signal_id >= 0 && out_policy->duration_ms > 0U;
}

static bool haptics_payload_actor_filter_matches(const sdl3d_game_data_runtime *runtime, yyjson_val *filter,
                                                 const sdl3d_properties *payload)
{
    if (runtime == NULL || !yyjson_is_obj(filter) || payload == NULL)
        return false;
    if (!eval_data_condition(runtime, obj_get(filter, "active_if"), NULL))
        return false;

    const char *key = json_string(filter, "key", NULL);
    const char *payload_actor_name = key != NULL ? sdl3d_properties_get_string(payload, key, NULL) : NULL;
    if (payload_actor_name == NULL || payload_actor_name[0] == '\0')
        return false;

    const char *actor_name = json_string(filter, "actor", NULL);
    if (actor_name != NULL && SDL_strcmp(actor_name, payload_actor_name) == 0)
        return true;

    yyjson_val *tags = obj_get(filter, "tags");
    if (yyjson_is_arr(tags))
    {
        yyjson_val *entity = find_entity_json(runtime, payload_actor_name);
        return entity != NULL && entity_json_has_all_tags_from_json(entity, tags);
    }

    return false;
}

static bool haptics_policy_payload_matches(const sdl3d_game_data_runtime *runtime, yyjson_val *policy,
                                           const sdl3d_properties *payload)
{
    yyjson_val *filters = obj_get(policy, "payload_actor_filters");
    if (filters == NULL)
        return true;
    if (!yyjson_is_arr(filters) || yyjson_arr_size(filters) == 0)
        return false;

    for (size_t i = 0; i < yyjson_arr_size(filters); ++i)
    {
        if (haptics_payload_actor_filter_matches(runtime, yyjson_arr_get(filters, i), payload))
            return true;
    }
    return false;
}

bool sdl3d_game_data_match_haptics_policy(const sdl3d_game_data_runtime *runtime, int index, int signal_id,
                                          const sdl3d_properties *payload, sdl3d_game_data_haptics_policy *out_policy)
{
    yyjson_val *policy = haptics_policy_json(runtime, index);
    sdl3d_game_data_haptics_policy candidate;
    if (!sdl3d_game_data_get_haptics_policy_at(runtime, index, &candidate))
        return false;
    if (candidate.signal_id != signal_id)
        return false;
    if (!eval_data_condition(runtime, obj_get(policy, "enabled_if"), NULL))
        return false;
    if (!haptics_policy_payload_matches(runtime, policy, payload))
        return false;

    if (out_policy != NULL)
        *out_policy = candidate;
    return true;
}

static yyjson_val *network_runtime_pause_json(const sdl3d_game_data_runtime *runtime)
{
    return obj_get(network_runtime_bindings_json(runtime), "pause");
}

static bool network_runtime_pause_state_binding(const sdl3d_game_data_runtime *runtime,
                                                sdl3d_registered_actor **out_actor, const char **out_property,
                                                char *error_buffer, int error_buffer_size)
{
    if (out_actor != NULL)
        *out_actor = NULL;
    if (out_property != NULL)
        *out_property = NULL;
    if (runtime == NULL || out_actor == NULL || out_property == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network pause state requires a runtime");
        return false;
    }

    yyjson_val *state = obj_get(network_runtime_pause_json(runtime), "state");
    const char *actor_name = json_string(state, "actor", NULL);
    const char *property = json_string(state, "property", NULL);
    if (actor_name == NULL || actor_name[0] == '\0' || property == NULL || property[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "network pause state binding is not authored");
        return false;
    }

    sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, actor_name);
    if (actor == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "network pause actor '%s' not found", actor_name);
        return false;
    }

    *out_actor = actor;
    *out_property = property;
    return true;
}

bool sdl3d_game_data_get_network_runtime_pause_action(const sdl3d_game_data_runtime *runtime, int *out_action_id)
{
    if (out_action_id != NULL)
        *out_action_id = -1;
    if (runtime == NULL || out_action_id == NULL)
        return false;

    const char *action = json_string(network_runtime_pause_json(runtime), "action", NULL);
    if (action == NULL || action[0] == '\0')
        return false;

    const int action_id = sdl3d_game_data_find_action(runtime, action);
    if (action_id < 0)
        return false;

    *out_action_id = action_id;
    return true;
}

bool sdl3d_game_data_get_network_runtime_pause_state(const sdl3d_game_data_runtime *runtime, bool *out_paused,
                                                     char *error_buffer, int error_buffer_size)
{
    if (out_paused != NULL)
        *out_paused = false;
    if (out_paused == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network pause state output is required");
        return false;
    }

    sdl3d_registered_actor *actor = NULL;
    const char *property = NULL;
    if (!network_runtime_pause_state_binding(runtime, &actor, &property, error_buffer, error_buffer_size))
        return false;

    const sdl3d_value *value = sdl3d_properties_get_value(actor->props, property);
    if (value == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "network pause property '%s' not found", property);
        return false;
    }
    if (value->type != SDL3D_VALUE_BOOL)
    {
        set_errorf(error_buffer, error_buffer_size, "network pause property '%s' must be bool", property);
        return false;
    }

    *out_paused = value->as_bool;
    return true;
}

bool sdl3d_game_data_set_network_runtime_pause_state(sdl3d_game_data_runtime *runtime, bool paused, char *error_buffer,
                                                     int error_buffer_size)
{
    sdl3d_registered_actor *actor = NULL;
    const char *property = NULL;
    if (!network_runtime_pause_state_binding(runtime, &actor, &property, error_buffer, error_buffer_size))
        return false;

    const sdl3d_value *existing = sdl3d_properties_get_value(actor->props, property);
    if (existing != NULL && existing->type != SDL3D_VALUE_BOOL)
    {
        set_errorf(error_buffer, error_buffer_size, "network pause property '%s' must be bool", property);
        return false;
    }

    sdl3d_properties_set_bool(actor->props, property, paused);
    return true;
}

static bool game_data_append_snapshot_value(char *buffer, size_t buffer_size, size_t *offset,
                                            const game_data_snapshot_value *value)
{
    if (value == NULL)
        return false;

    switch (value->type)
    {
    case SDL3D_REPLICATION_FIELD_BOOL:
        return append_format(buffer, buffer_size, offset, "%s", value->value.as_bool ? "true" : "false");
    case SDL3D_REPLICATION_FIELD_INT32:
    case SDL3D_REPLICATION_FIELD_ENUM_ID:
        return append_format(buffer, buffer_size, offset, "%d", (int)value->value.as_int32);
    case SDL3D_REPLICATION_FIELD_FLOAT32:
        return append_format(buffer, buffer_size, offset, "%.3f", value->value.as_float32);
    case SDL3D_REPLICATION_FIELD_VEC2:
        return append_format(buffer, buffer_size, offset, "(%.3f,%.3f)", value->value.as_vec2.x,
                             value->value.as_vec2.y);
    case SDL3D_REPLICATION_FIELD_VEC3:
        return append_format(buffer, buffer_size, offset, "(%.3f,%.3f,%.3f)", value->value.as_vec3.x,
                             value->value.as_vec3.y, value->value.as_vec3.z);
    default:
        return false;
    }
}

static bool game_data_describe_network_snapshot_ex(const sdl3d_game_data_runtime *runtime, const char *replication_name,
                                                   Uint32 tick, bool include_session_state, char *buffer,
                                                   size_t buffer_size, char *error_buffer, int error_buffer_size)
{
    if (buffer != NULL && buffer_size > 0U)
        buffer[0] = '\0';
    if (runtime == NULL || replication_name == NULL || replication_name[0] == '\0' || buffer == NULL ||
        buffer_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot describe requires runtime, channel, and buffer");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot describe requires an authored network schema");
        return false;
    }

    yyjson_val *channel = game_data_find_replication_channel_by_name(runtime, replication_name, NULL);
    if (channel == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot describe channel not found");
        return false;
    }
    if (!game_data_replication_channel_is_host_to_client(channel))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot describe channel must be host_to_client");
        return false;
    }

    size_t offset = 0U;
    const char *active_scene = sdl3d_game_data_active_scene(runtime);
    if (!append_format(buffer, buffer_size, &offset, "tick=%u scene=%s channel=%s", (unsigned int)tick,
                       active_scene != NULL ? active_scene : "<none>", replication_name))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot describe buffer is too small");
        return false;
    }

    yyjson_val *state_keys = obj_get(network_session_flow_json(runtime), "state_keys");
    if (include_session_state && yyjson_is_obj(state_keys))
    {
        yyjson_val *state_key = NULL;
        yyjson_val *state_value_key = NULL;
        size_t state_index = 0U;
        size_t state_max = 0U;
        yyjson_obj_foreach(state_keys, state_index, state_max, state_key, state_value_key)
        {
            const char *semantic = yyjson_get_str(state_key);
            const char *key = yyjson_is_str(state_value_key) ? yyjson_get_str(state_value_key) : NULL;
            const char *value = sdl3d_properties_get_string(runtime->scene_state, key, "none");
            if (semantic != NULL && key != NULL &&
                !append_format(buffer, buffer_size, &offset, " %s=%s", semantic, value != NULL ? value : "none"))
            {
                set_error(error_buffer, error_buffer_size, "network snapshot describe buffer is too small");
                return false;
            }
        }
    }

    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t actor_index = 0U; yyjson_is_arr(actors) && actor_index < yyjson_arr_size(actors); ++actor_index)
    {
        yyjson_val *actor_schema = yyjson_arr_get(actors, actor_index);
        const char *entity_name = json_string(actor_schema, "entity", NULL);
        const sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, entity_name);
        if (actor == NULL)
        {
            set_errorf(error_buffer, error_buffer_size, "network snapshot describe actor '%s' not found",
                       entity_name != NULL ? entity_name : "<null>");
            return false;
        }

        yyjson_val *fields = obj_get(actor_schema, "fields");
        for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
        {
            sdl3d_replication_field_descriptor field;
            game_data_snapshot_value value;
            if (!sdl3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) ||
                !game_data_read_actor_replication_field(actor, &field, &value))
            {
                set_errorf(error_buffer, error_buffer_size,
                           "network snapshot describe failed to read field '%s' on actor '%s'",
                           field.path != NULL ? field.path : "<invalid>", entity_name != NULL ? entity_name : "<null>");
                return false;
            }
            if (!append_format(buffer, buffer_size, &offset, " %s.%s=", entity_name != NULL ? entity_name : "<null>",
                               field.path != NULL ? field.path : "<invalid>") ||
                !game_data_append_snapshot_value(buffer, buffer_size, &offset, &value))
            {
                set_error(error_buffer, error_buffer_size, "network snapshot describe buffer is too small");
                return false;
            }
        }
    }

    return true;
}

bool sdl3d_game_data_describe_network_snapshot(const sdl3d_game_data_runtime *runtime, const char *replication_name,
                                               Uint32 tick, char *buffer, size_t buffer_size, char *error_buffer,
                                               int error_buffer_size)
{
    return game_data_describe_network_snapshot_ex(runtime, replication_name, tick, true, buffer, buffer_size,
                                                  error_buffer, error_buffer_size);
}

static yyjson_val *network_diagnostics_json(const sdl3d_game_data_runtime *runtime)
{
    return obj_get(obj_get(runtime_root(runtime), "network"), "diagnostics");
}

static yyjson_val *find_network_snapshot_diagnostic_json(const sdl3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *snapshots = obj_get(network_diagnostics_json(runtime), "snapshots");
    if (name == NULL || name[0] == '\0' || !yyjson_is_arr(snapshots))
        return NULL;

    for (size_t i = 0U; i < yyjson_arr_size(snapshots); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(snapshots, i);
        const char *entry_name = json_string(entry, "name", NULL);
        if (entry_name != NULL && SDL_strcmp(entry_name, name) == 0)
            return entry;
    }
    return NULL;
}

static SDL_LogPriority network_diagnostic_log_priority(const char *level)
{
    if (level == NULL || level[0] == '\0' || SDL_strcmp(level, "info") == 0)
        return SDL_LOG_PRIORITY_INFO;
    if (SDL_strcmp(level, "debug") == 0)
        return SDL_LOG_PRIORITY_DEBUG;
    if (SDL_strcmp(level, "warn") == 0 || SDL_strcmp(level, "warning") == 0)
        return SDL_LOG_PRIORITY_WARN;
    if (SDL_strcmp(level, "error") == 0)
        return SDL_LOG_PRIORITY_ERROR;
    if (SDL_strcmp(level, "critical") == 0)
        return SDL_LOG_PRIORITY_CRITICAL;
    return SDL_LOG_PRIORITY_INFO;
}

static network_diagnostic_runtime_state *find_network_diagnostic_state(sdl3d_game_data_runtime *runtime,
                                                                       const char *name)
{
    if (runtime == NULL || name == NULL || name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->network_diagnostic_count; ++i)
    {
        if (runtime->network_diagnostics[i].name != NULL && SDL_strcmp(runtime->network_diagnostics[i].name, name) == 0)
        {
            return &runtime->network_diagnostics[i];
        }
    }
    return NULL;
}

static bool ensure_network_diagnostic_state_capacity(sdl3d_game_data_runtime *runtime, int required)
{
    if (runtime == NULL)
        return false;
    if (required <= runtime->network_diagnostic_capacity)
        return true;

    int next_capacity = runtime->network_diagnostic_capacity < 4 ? 4 : runtime->network_diagnostic_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    network_diagnostic_runtime_state *states = (network_diagnostic_runtime_state *)SDL_realloc(
        runtime->network_diagnostics, (size_t)next_capacity * sizeof(*states));
    if (states == NULL)
        return false;

    SDL_memset(states + runtime->network_diagnostic_capacity, 0,
               (size_t)(next_capacity - runtime->network_diagnostic_capacity) * sizeof(*states));
    runtime->network_diagnostics = states;
    runtime->network_diagnostic_capacity = next_capacity;
    return true;
}

static network_diagnostic_runtime_state *ensure_network_diagnostic_state(sdl3d_game_data_runtime *runtime,
                                                                         const char *name)
{
    network_diagnostic_runtime_state *state = find_network_diagnostic_state(runtime, name);
    if (state != NULL)
        return state;
    if (runtime == NULL || name == NULL || name[0] == '\0' ||
        !ensure_network_diagnostic_state_capacity(runtime, runtime->network_diagnostic_count + 1))
    {
        return NULL;
    }

    state = &runtime->network_diagnostics[runtime->network_diagnostic_count];
    state->name = SDL_strdup(name);
    if (state->name == NULL)
        return NULL;
    state->last_log_ms = 0U;
    state->logged = false;
    runtime->network_diagnostic_count++;
    return state;
}

bool sdl3d_game_data_log_network_snapshot_diagnostic(sdl3d_game_data_runtime *runtime, const char *diagnostic_name,
                                                     Uint32 tick, const char *event, const char *extra,
                                                     bool *out_logged, char *error_buffer, int error_buffer_size)
{
    if (out_logged != NULL)
        *out_logged = false;
    if (runtime == NULL || diagnostic_name == NULL || diagnostic_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "network snapshot diagnostic requires runtime and name");
        return false;
    }

    yyjson_val *diagnostic = find_network_snapshot_diagnostic_json(runtime, diagnostic_name);
    if (diagnostic == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot diagnostic not found");
        return false;
    }
    if (!json_bool(diagnostic, "enabled", true))
        return true;

    network_diagnostic_runtime_state *state = ensure_network_diagnostic_state(runtime, diagnostic_name);
    if (state == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot diagnostic state allocation failed");
        return false;
    }

    const float cadence_seconds = SDL_max(json_float(diagnostic, "cadence_seconds", 0.0f), 0.0f);
    const Uint64 now = SDL_GetTicks();
    if (state->logged && cadence_seconds > 0.0f &&
        (double)(now - state->last_log_ms) < (double)cadence_seconds * 1000.0)
    {
        return true;
    }

    const char *replication_name = json_string(diagnostic, "replication", NULL);
    char description[4096] = {0};
    if (!game_data_describe_network_snapshot_ex(runtime, replication_name, tick,
                                                json_bool(diagnostic, "include_session_state", true), description,
                                                sizeof(description), error_buffer, error_buffer_size))
    {
        return false;
    }

    char tick_text[32];
    SDL_snprintf(tick_text, sizeof(tick_text), "%u", (unsigned int)tick);

    sdl3d_properties *payload = sdl3d_properties_create();
    if (payload == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot diagnostic payload allocation failed");
        return false;
    }
    sdl3d_properties_set_string(payload, "name", diagnostic_name);
    sdl3d_properties_set_string(payload, "event", event != NULL ? event : "");
    sdl3d_properties_set_string(payload, "extra", extra != NULL ? extra : "");
    sdl3d_properties_set_string(payload, "tick", tick_text);
    sdl3d_properties_set_string(payload, "description", description);

    char message[4096] = {0};
    const char *message_template = json_string(diagnostic, "message", "{event} {description} {extra}");
    const bool formatted = format_payload_string(payload, message_template, message, sizeof(message));
    sdl3d_properties_destroy(payload);
    if (!formatted)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot diagnostic message is too long");
        return false;
    }

    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION,
                   network_diagnostic_log_priority(json_string(diagnostic, "level", NULL)), "%s", message);
    state->last_log_ms = now;
    state->logged = true;
    if (out_logged != NULL)
        *out_logged = true;
    return true;
}

bool sdl3d_game_data_encode_network_snapshot(const sdl3d_game_data_runtime *runtime, const char *replication_name,
                                             Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                             char *error_buffer, int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (runtime == NULL || replication_name == NULL || replication_name[0] == '\0' || buffer == NULL ||
        buffer_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot encode requires runtime, channel, and buffer");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot encode requires an authored network schema");
        return false;
    }

    int channel_index = -1;
    yyjson_val *channel = game_data_find_replication_channel_by_name(runtime, replication_name, &channel_index);
    if (channel == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot replication channel not found");
        return false;
    }
    if (!game_data_replication_channel_is_host_to_client(channel))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot channel must be host_to_client");
        return false;
    }

    const size_t field_count = game_data_replication_channel_field_count(channel);
    if (field_count > SDL_MAX_UINT32 || channel_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot channel is too large");
        return false;
    }
    size_t required_size = 0U;
    if (!game_data_replication_channel_packet_size(channel, &required_size))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot channel contains unsupported field data");
        return false;
    }
    if (buffer_size < required_size)
    {
        set_errorf(error_buffer, error_buffer_size, "network snapshot requires %zu bytes, buffer has %zu bytes",
                   required_size, buffer_size);
        return false;
    }

    sdl3d_replication_writer writer;
    sdl3d_replication_writer_init(&writer, buffer, buffer_size);
    if (!sdl3d_replication_write_uint32(&writer, SDL3D_GAME_DATA_NETWORK_SNAPSHOT_MAGIC) ||
        !sdl3d_replication_write_uint32(&writer, SDL3D_GAME_DATA_NETWORK_SNAPSHOT_VERSION) ||
        !sdl3d_replication_write_uint32(&writer, tick) ||
        !sdl3d_replication_write_uint32(&writer, (Uint32)channel_index) ||
        !sdl3d_replication_write_bytes(&writer, runtime->network_schema_hash, SDL3D_REPLICATION_SCHEMA_HASH_SIZE) ||
        !sdl3d_replication_write_uint32(&writer, (Uint32)field_count))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot buffer is too small for header");
        return false;
    }

    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t actor_index = 0U; yyjson_is_arr(actors) && actor_index < yyjson_arr_size(actors); ++actor_index)
    {
        yyjson_val *actor_schema = yyjson_arr_get(actors, actor_index);
        const char *entity_name = json_string(actor_schema, "entity", NULL);
        sdl3d_registered_actor *actor = sdl3d_game_data_find_actor(runtime, entity_name);
        if (actor == NULL)
        {
            set_errorf(error_buffer, error_buffer_size, "network snapshot actor '%s' not found",
                       entity_name != NULL ? entity_name : "<null>");
            return false;
        }

        yyjson_val *fields = obj_get(actor_schema, "fields");
        for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
        {
            sdl3d_replication_field_descriptor field;
            game_data_snapshot_value value;
            if (!sdl3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) ||
                !game_data_read_actor_replication_field(actor, &field, &value))
            {
                set_errorf(error_buffer, error_buffer_size, "network snapshot failed to read field '%s' on actor '%s'",
                           field.path != NULL ? field.path : "<invalid>", entity_name != NULL ? entity_name : "<null>");
                return false;
            }
            if (!game_data_write_snapshot_value(&writer, &value))
            {
                set_error(error_buffer, error_buffer_size, "network snapshot buffer is too small for field data");
                return false;
            }
        }
    }

    if (out_size != NULL)
        *out_size = sdl3d_replication_writer_offset(&writer);
    return true;
}

bool sdl3d_game_data_encode_network_runtime_snapshot(const sdl3d_game_data_runtime *runtime, const char *binding_name,
                                                     Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                                     char *error_buffer, int error_buffer_size)
{
    const char *replication_name = NULL;
    if (!sdl3d_game_data_get_network_runtime_replication(runtime, binding_name, &replication_name))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime replication binding '%s' not found",
                   binding_name != NULL ? binding_name : "<null>");
        if (out_size != NULL)
            *out_size = 0U;
        return false;
    }
    return sdl3d_game_data_encode_network_snapshot(runtime, replication_name, tick, buffer, buffer_size, out_size,
                                                   error_buffer, error_buffer_size);
}

bool sdl3d_game_data_send_network_runtime_snapshot(const sdl3d_game_data_runtime *runtime,
                                                   sdl3d_network_session *session, const char *binding_name,
                                                   Uint32 tick, char *error_buffer, int error_buffer_size)
{
    Uint8 packet[SDL3D_NETWORK_MAX_PACKET_SIZE];
    size_t packet_size = 0U;
    if (session == NULL || !sdl3d_network_session_is_connected(session))
    {
        set_error(error_buffer, error_buffer_size, "network runtime snapshot send requires connected session");
        return false;
    }
    if (!sdl3d_game_data_encode_network_runtime_snapshot(runtime, binding_name, tick, packet, sizeof(packet),
                                                         &packet_size, error_buffer, error_buffer_size))
    {
        return false;
    }
    if (!sdl3d_network_session_send(session, packet, (int)packet_size))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime snapshot send failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

static bool game_data_network_packet_matches_replication_binding(const sdl3d_game_data_runtime *runtime,
                                                                 const char *binding_name, const void *packet,
                                                                 size_t packet_size, Uint32 expected_magic,
                                                                 Uint32 expected_version, bool host_to_client,
                                                                 char *error_buffer, int error_buffer_size)
{
    const char *expected_channel = NULL;
    if (runtime == NULL || packet == NULL || packet_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication check requires runtime and packet");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size,
                  "network runtime replication check requires an authored network schema");
        return false;
    }
    if (!sdl3d_game_data_get_network_runtime_replication(runtime, binding_name, &expected_channel))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime replication binding '%s' not found",
                   binding_name != NULL ? binding_name : "<null>");
        return false;
    }

    sdl3d_replication_reader reader;
    sdl3d_replication_reader_init(&reader, packet, packet_size);
    Uint32 magic = 0U;
    Uint32 version = 0U;
    Uint32 tick = 0U;
    Uint32 channel_index = 0U;
    Uint8 schema_hash[SDL3D_REPLICATION_SCHEMA_HASH_SIZE];
    if (!sdl3d_replication_read_uint32(&reader, &magic) || !sdl3d_replication_read_uint32(&reader, &version) ||
        !sdl3d_replication_read_uint32(&reader, &tick) || !sdl3d_replication_read_uint32(&reader, &channel_index) ||
        !sdl3d_replication_read_bytes(&reader, schema_hash, sizeof(schema_hash)))
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication packet is too small for header");
        return false;
    }
    if (magic != expected_magic || version != expected_version)
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication packet has unsupported header");
        return false;
    }
    if (SDL_memcmp(schema_hash, runtime->network_schema_hash, SDL3D_REPLICATION_SCHEMA_HASH_SIZE) != 0)
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication schema hash does not match runtime");
        return false;
    }

    yyjson_val *channel = game_data_find_replication_channel_by_index(runtime, channel_index);
    if (channel == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication channel is invalid for this runtime");
        return false;
    }
    if (host_to_client && !game_data_replication_channel_is_host_to_client(channel))
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication channel must be host_to_client");
        return false;
    }
    if (!host_to_client && !game_data_replication_channel_is_client_to_host(channel))
    {
        set_error(error_buffer, error_buffer_size, "network runtime replication channel must be client_to_host");
        return false;
    }

    const char *actual_channel = json_string(channel, "name", NULL);
    if (actual_channel == NULL || SDL_strcmp(actual_channel, expected_channel) != 0)
    {
        set_errorf(error_buffer, error_buffer_size,
                   "network runtime replication packet channel '%s' does not match binding '%s'",
                   actual_channel != NULL ? actual_channel : "<null>", binding_name != NULL ? binding_name : "<null>");
        return false;
    }
    return true;
}

bool sdl3d_game_data_apply_network_snapshot(sdl3d_game_data_runtime *runtime, const void *packet, size_t packet_size,
                                            Uint32 *out_tick, char *error_buffer, int error_buffer_size)
{
    if (out_tick != NULL)
        *out_tick = 0U;
    if (runtime == NULL || packet == NULL || packet_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot apply requires runtime and packet");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot apply requires an authored network schema");
        return false;
    }

    sdl3d_replication_reader reader;
    sdl3d_replication_reader_init(&reader, packet, packet_size);

    Uint32 magic = 0U;
    Uint32 version = 0U;
    Uint32 tick = 0U;
    Uint32 channel_index = 0U;
    Uint8 schema_hash[SDL3D_REPLICATION_SCHEMA_HASH_SIZE];
    Uint32 packet_field_count = 0U;
    if (!sdl3d_replication_read_uint32(&reader, &magic) || !sdl3d_replication_read_uint32(&reader, &version) ||
        !sdl3d_replication_read_uint32(&reader, &tick) || !sdl3d_replication_read_uint32(&reader, &channel_index) ||
        !sdl3d_replication_read_bytes(&reader, schema_hash, sizeof(schema_hash)) ||
        !sdl3d_replication_read_uint32(&reader, &packet_field_count))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot packet is too small for header");
        return false;
    }
    if (magic != SDL3D_GAME_DATA_NETWORK_SNAPSHOT_MAGIC || version != SDL3D_GAME_DATA_NETWORK_SNAPSHOT_VERSION)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot packet has unsupported header");
        return false;
    }
    if (SDL_memcmp(schema_hash, runtime->network_schema_hash, SDL3D_REPLICATION_SCHEMA_HASH_SIZE) != 0)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot schema hash does not match runtime");
        return false;
    }

    yyjson_val *channel = game_data_find_replication_channel_by_index(runtime, channel_index);
    if (channel == NULL || !game_data_replication_channel_is_host_to_client(channel))
    {
        set_error(error_buffer, error_buffer_size, "network snapshot channel is invalid for this runtime");
        return false;
    }

    const size_t field_count = game_data_replication_channel_field_count(channel);
    if ((Uint32)field_count != packet_field_count)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot field count does not match schema");
        return false;
    }

    game_data_snapshot_value *values =
        field_count > 0U ? (game_data_snapshot_value *)SDL_calloc(field_count, sizeof(*values)) : NULL;
    if (values == NULL && field_count > 0U)
    {
        set_error(error_buffer, error_buffer_size, "network snapshot failed to allocate decoded field storage");
        return false;
    }

    size_t decoded_index = 0U;
    bool ok = true;
    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t actor_index = 0U; ok && yyjson_is_arr(actors) && actor_index < yyjson_arr_size(actors); ++actor_index)
    {
        yyjson_val *fields = obj_get(yyjson_arr_get(actors, actor_index), "fields");
        for (size_t field_index = 0U; ok && yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields);
             ++field_index)
        {
            sdl3d_replication_field_descriptor field;
            ok = sdl3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) &&
                 decoded_index < field_count &&
                 game_data_read_snapshot_value(&reader, field.type, &values[decoded_index]);
            ++decoded_index;
        }
    }
    if (ok && decoded_index != field_count)
        ok = false;
    if (ok && sdl3d_replication_reader_remaining(&reader) != 0U)
        ok = false;
    if (!ok)
    {
        SDL_free(values);
        set_error(error_buffer, error_buffer_size, "network snapshot field data does not match schema");
        return false;
    }

    const size_t actor_count = yyjson_is_arr(actors) ? yyjson_arr_size(actors) : 0U;
    sdl3d_registered_actor **resolved_actors =
        actor_count > 0U ? (sdl3d_registered_actor **)SDL_calloc(actor_count, sizeof(*resolved_actors)) : NULL;
    if (resolved_actors == NULL && actor_count > 0U)
    {
        SDL_free(values);
        set_error(error_buffer, error_buffer_size, "network snapshot failed to allocate actor lookup storage");
        return false;
    }

    /* Resolve every actor before applying any field so a malformed runtime cannot produce a partial snapshot apply. */
    for (size_t actor_index = 0U; actor_index < actor_count; ++actor_index)
    {
        yyjson_val *actor_schema = yyjson_arr_get(actors, actor_index);
        const char *entity_name = json_string(actor_schema, "entity", NULL);
        resolved_actors[actor_index] = sdl3d_game_data_find_actor(runtime, entity_name);
        if (resolved_actors[actor_index] == NULL)
        {
            SDL_free(resolved_actors);
            SDL_free(values);
            set_errorf(error_buffer, error_buffer_size, "network snapshot actor '%s' not found",
                       entity_name != NULL ? entity_name : "<null>");
            return false;
        }
    }

    actor_lifecycle_defer_begin(runtime);
    bool apply_ok = true;
    size_t apply_index = 0U;
    for (size_t actor_index = 0U; apply_ok && actor_index < actor_count; ++actor_index)
    {
        yyjson_val *actor_schema = yyjson_arr_get(actors, actor_index);
        sdl3d_registered_actor *actor = resolved_actors[actor_index];
        yyjson_val *fields = obj_get(actor_schema, "fields");
        for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
        {
            sdl3d_replication_field_descriptor field;
            if (!sdl3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field) ||
                apply_index >= field_count ||
                !game_data_apply_actor_replication_field(actor, &field, &values[apply_index]))
            {
                set_errorf(error_buffer, error_buffer_size, "network snapshot failed to apply field '%s' on actor '%s'",
                           field.path != NULL ? field.path : "<invalid>",
                           actor_schema != NULL ? json_string(actor_schema, "entity", "<null>") : "<null>");
                apply_ok = false;
                break;
            }
            ++apply_index;
        }
    }
    actor_lifecycle_defer_end(runtime);
    if (!apply_ok)
    {
        SDL_free(resolved_actors);
        SDL_free(values);
        return false;
    }

    SDL_free(resolved_actors);
    SDL_free(values);
    if (out_tick != NULL)
        *out_tick = tick;
    return true;
}

bool sdl3d_game_data_apply_network_runtime_snapshot(sdl3d_game_data_runtime *runtime, const char *binding_name,
                                                    const void *packet, size_t packet_size, Uint32 *out_tick,
                                                    char *error_buffer, int error_buffer_size)
{
    if (!game_data_network_packet_matches_replication_binding(
            runtime, binding_name, packet, packet_size, SDL3D_GAME_DATA_NETWORK_SNAPSHOT_MAGIC,
            SDL3D_GAME_DATA_NETWORK_SNAPSHOT_VERSION, true, error_buffer, error_buffer_size))
    {
        if (out_tick != NULL)
            *out_tick = 0U;
        return false;
    }
    return sdl3d_game_data_apply_network_snapshot(runtime, packet, packet_size, out_tick, error_buffer,
                                                  error_buffer_size);
}

bool sdl3d_game_data_encode_network_input(const sdl3d_game_data_runtime *runtime, const char *replication_name,
                                          const sdl3d_input_manager *input, Uint32 tick, void *buffer,
                                          size_t buffer_size, size_t *out_size, char *error_buffer,
                                          int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (runtime == NULL || replication_name == NULL || replication_name[0] == '\0' || input == NULL || buffer == NULL ||
        buffer_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network input encode requires runtime, channel, input, and buffer");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network input encode requires an authored network schema");
        return false;
    }

    int channel_index = -1;
    yyjson_val *channel = game_data_find_replication_channel_by_name(runtime, replication_name, &channel_index);
    if (channel == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network input replication channel not found");
        return false;
    }
    if (!game_data_replication_channel_is_client_to_host(channel))
    {
        set_error(error_buffer, error_buffer_size, "network input channel must be client_to_host");
        return false;
    }

    const size_t input_count = game_data_replication_channel_input_count(channel);
    if (input_count > SDL_MAX_UINT32 || channel_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "network input channel is too large");
        return false;
    }
    size_t required_size = 0U;
    if (!game_data_replication_input_packet_size(channel, &required_size))
    {
        set_error(error_buffer, error_buffer_size, "network input channel contains unsupported field data");
        return false;
    }
    if (buffer_size < required_size)
    {
        set_errorf(error_buffer, error_buffer_size, "network input requires %zu bytes, buffer has %zu bytes",
                   required_size, buffer_size);
        return false;
    }

    sdl3d_replication_writer writer;
    sdl3d_replication_writer_init(&writer, buffer, buffer_size);
    if (!sdl3d_replication_write_uint32(&writer, SDL3D_GAME_DATA_NETWORK_INPUT_MAGIC) ||
        !sdl3d_replication_write_uint32(&writer, SDL3D_GAME_DATA_NETWORK_INPUT_VERSION) ||
        !sdl3d_replication_write_uint32(&writer, tick) ||
        !sdl3d_replication_write_uint32(&writer, (Uint32)channel_index) ||
        !sdl3d_replication_write_bytes(&writer, runtime->network_schema_hash, SDL3D_REPLICATION_SCHEMA_HASH_SIZE) ||
        !sdl3d_replication_write_uint32(&writer, (Uint32)input_count))
    {
        set_error(error_buffer, error_buffer_size, "network input buffer is too small for header");
        return false;
    }

    yyjson_val *inputs = obj_get(channel, "inputs");
    for (size_t input_index = 0U; yyjson_is_arr(inputs) && input_index < yyjson_arr_size(inputs); ++input_index)
    {
        yyjson_val *input_schema = yyjson_arr_get(inputs, input_index);
        const char *action_name = game_data_replication_input_action(input_schema);
        const int action_id = game_data_replication_action_id(runtime, input_schema);
        if (action_id < 0)
        {
            set_errorf(error_buffer, error_buffer_size, "network input action '%s' not found",
                       action_name != NULL ? action_name : "<null>");
            return false;
        }

        const float value = SDL_clamp(sdl3d_input_get_value(input, action_id), -1.0f, 1.0f);
        if (!sdl3d_replication_write_field_type(&writer, SDL3D_REPLICATION_FIELD_FLOAT32) ||
            !sdl3d_replication_write_float32(&writer, value))
        {
            set_error(error_buffer, error_buffer_size, "network input buffer is too small for action data");
            return false;
        }
    }

    if (out_size != NULL)
        *out_size = sdl3d_replication_writer_offset(&writer);
    return true;
}

bool sdl3d_game_data_encode_network_runtime_input(const sdl3d_game_data_runtime *runtime, const char *binding_name,
                                                  const sdl3d_input_manager *input, Uint32 tick, void *buffer,
                                                  size_t buffer_size, size_t *out_size, char *error_buffer,
                                                  int error_buffer_size)
{
    const char *replication_name = NULL;
    if (!sdl3d_game_data_get_network_runtime_replication(runtime, binding_name, &replication_name))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime replication binding '%s' not found",
                   binding_name != NULL ? binding_name : "<null>");
        if (out_size != NULL)
            *out_size = 0U;
        return false;
    }
    return sdl3d_game_data_encode_network_input(runtime, replication_name, input, tick, buffer, buffer_size, out_size,
                                                error_buffer, error_buffer_size);
}

bool sdl3d_game_data_send_network_runtime_input(const sdl3d_game_data_runtime *runtime, sdl3d_network_session *session,
                                                const char *binding_name, const sdl3d_input_manager *input, Uint32 tick,
                                                char *error_buffer, int error_buffer_size)
{
    Uint8 packet[SDL3D_NETWORK_MAX_PACKET_SIZE];
    size_t packet_size = 0U;
    if (session == NULL || !sdl3d_network_session_is_connected(session))
    {
        set_error(error_buffer, error_buffer_size, "network runtime input send requires connected session");
        return false;
    }
    if (!sdl3d_game_data_encode_network_runtime_input(runtime, binding_name, input, tick, packet, sizeof(packet),
                                                      &packet_size, error_buffer, error_buffer_size))
    {
        return false;
    }
    if (!sdl3d_network_session_send(session, packet, (int)packet_size))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime input send failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool sdl3d_game_data_apply_network_input(const sdl3d_game_data_runtime *runtime, sdl3d_input_manager *input,
                                         const void *packet, size_t packet_size, Uint32 *out_tick, char *error_buffer,
                                         int error_buffer_size)
{
    if (out_tick != NULL)
        *out_tick = 0U;
    if (runtime == NULL || input == NULL || packet == NULL || packet_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network input apply requires runtime, input, and packet");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network input apply requires an authored network schema");
        return false;
    }

    sdl3d_replication_reader reader;
    sdl3d_replication_reader_init(&reader, packet, packet_size);

    Uint32 magic = 0U;
    Uint32 version = 0U;
    Uint32 tick = 0U;
    Uint32 channel_index = 0U;
    Uint8 schema_hash[SDL3D_REPLICATION_SCHEMA_HASH_SIZE];
    Uint32 packet_input_count = 0U;
    if (!sdl3d_replication_read_uint32(&reader, &magic) || !sdl3d_replication_read_uint32(&reader, &version) ||
        !sdl3d_replication_read_uint32(&reader, &tick) || !sdl3d_replication_read_uint32(&reader, &channel_index) ||
        !sdl3d_replication_read_bytes(&reader, schema_hash, sizeof(schema_hash)) ||
        !sdl3d_replication_read_uint32(&reader, &packet_input_count))
    {
        set_error(error_buffer, error_buffer_size, "network input packet is too small for header");
        return false;
    }
    if (magic != SDL3D_GAME_DATA_NETWORK_INPUT_MAGIC || version != SDL3D_GAME_DATA_NETWORK_INPUT_VERSION)
    {
        set_error(error_buffer, error_buffer_size, "network input packet has unsupported header");
        return false;
    }
    if (SDL_memcmp(schema_hash, runtime->network_schema_hash, SDL3D_REPLICATION_SCHEMA_HASH_SIZE) != 0)
    {
        set_error(error_buffer, error_buffer_size, "network input schema hash does not match runtime");
        return false;
    }

    yyjson_val *channel = game_data_find_replication_channel_by_index(runtime, channel_index);
    if (channel == NULL || !game_data_replication_channel_is_client_to_host(channel))
    {
        set_error(error_buffer, error_buffer_size, "network input channel is invalid for this runtime");
        return false;
    }

    const size_t input_count = game_data_replication_channel_input_count(channel);
    if ((Uint32)input_count != packet_input_count)
    {
        set_error(error_buffer, error_buffer_size, "network input count does not match schema");
        return false;
    }

    game_data_input_value *values =
        input_count > 0U ? (game_data_input_value *)SDL_calloc(input_count, sizeof(*values)) : NULL;
    if (values == NULL && input_count > 0U)
    {
        set_error(error_buffer, error_buffer_size, "network input failed to allocate decoded action storage");
        return false;
    }

    bool ok = true;
    yyjson_val *inputs = obj_get(channel, "inputs");
    for (size_t input_index = 0U; ok && yyjson_is_arr(inputs) && input_index < yyjson_arr_size(inputs); ++input_index)
    {
        yyjson_val *input_schema = yyjson_arr_get(inputs, input_index);
        sdl3d_replication_field_type type = SDL3D_REPLICATION_FIELD_BOOL;
        values[input_index].action_id = game_data_replication_action_id(runtime, input_schema);
        ok = values[input_index].action_id >= 0 && sdl3d_replication_read_field_type(&reader, &type) &&
             type == SDL3D_REPLICATION_FIELD_FLOAT32 &&
             sdl3d_replication_read_float32(&reader, &values[input_index].value);
        values[input_index].value = SDL_clamp(values[input_index].value, -1.0f, 1.0f);
    }
    if (ok && sdl3d_replication_reader_remaining(&reader) != 0U)
        ok = false;
    if (!ok)
    {
        SDL_free(values);
        set_error(error_buffer, error_buffer_size, "network input action data does not match schema");
        return false;
    }

    for (size_t input_index = 0U; input_index < input_count; ++input_index)
        sdl3d_input_set_action_override(input, values[input_index].action_id, values[input_index].value);

    SDL_free(values);
    if (out_tick != NULL)
        *out_tick = tick;
    return true;
}

bool sdl3d_game_data_apply_network_runtime_input(const sdl3d_game_data_runtime *runtime, const char *binding_name,
                                                 sdl3d_input_manager *input, const void *packet, size_t packet_size,
                                                 Uint32 *out_tick, char *error_buffer, int error_buffer_size)
{
    if (!game_data_network_packet_matches_replication_binding(
            runtime, binding_name, packet, packet_size, SDL3D_GAME_DATA_NETWORK_INPUT_MAGIC,
            SDL3D_GAME_DATA_NETWORK_INPUT_VERSION, false, error_buffer, error_buffer_size))
    {
        if (out_tick != NULL)
            *out_tick = 0U;
        return false;
    }
    return sdl3d_game_data_apply_network_input(runtime, input, packet, packet_size, out_tick, error_buffer,
                                               error_buffer_size);
}

bool sdl3d_game_data_clear_network_input_overrides(const sdl3d_game_data_runtime *runtime, const char *replication_name,
                                                   sdl3d_input_manager *input, char *error_buffer,
                                                   int error_buffer_size)
{
    if (runtime == NULL || replication_name == NULL || replication_name[0] == '\0' || input == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network input override clear requires runtime, channel, and input");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network input override clear requires an authored network schema");
        return false;
    }

    yyjson_val *channel = game_data_find_replication_channel_by_name(runtime, replication_name, NULL);
    if (channel == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network input replication channel not found");
        return false;
    }
    if (!game_data_replication_channel_is_client_to_host(channel))
    {
        set_error(error_buffer, error_buffer_size, "network input channel must be client_to_host");
        return false;
    }

    yyjson_val *inputs = obj_get(channel, "inputs");
    for (size_t input_index = 0U; yyjson_is_arr(inputs) && input_index < yyjson_arr_size(inputs); ++input_index)
    {
        yyjson_val *input_schema = yyjson_arr_get(inputs, input_index);
        const char *action_name = game_data_replication_input_action(input_schema);
        const int action_id = game_data_replication_action_id(runtime, input_schema);
        if (action_id < 0)
        {
            set_errorf(error_buffer, error_buffer_size, "network input action '%s' not found",
                       action_name != NULL ? action_name : "<null>");
            return false;
        }
        sdl3d_input_clear_action_override(input, action_id);
    }

    return true;
}

bool sdl3d_game_data_encode_network_control(const sdl3d_game_data_runtime *runtime, const char *control_name,
                                            Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                            char *error_buffer, int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (runtime == NULL || control_name == NULL || control_name[0] == '\0' || buffer == NULL || buffer_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network control encode requires runtime, control, and buffer");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network control encode requires an authored network schema");
        return false;
    }

    int control_index = -1;
    yyjson_val *control = game_data_find_network_control_by_name(runtime, control_name, &control_index);
    if (control == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network control message not found");
        return false;
    }
    if (control_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "network control message index is invalid");
        return false;
    }

    size_t required_size = 0U;
    (void)game_data_network_control_packet_size(&required_size);
    if (buffer_size < required_size)
    {
        set_errorf(error_buffer, error_buffer_size, "network control requires %zu bytes, buffer has %zu bytes",
                   required_size, buffer_size);
        return false;
    }

    sdl3d_replication_writer writer;
    sdl3d_replication_writer_init(&writer, buffer, buffer_size);
    if (!sdl3d_replication_write_uint32(&writer, SDL3D_GAME_DATA_NETWORK_CONTROL_MAGIC) ||
        !sdl3d_replication_write_uint32(&writer, SDL3D_GAME_DATA_NETWORK_CONTROL_VERSION) ||
        !sdl3d_replication_write_uint32(&writer, tick) ||
        !sdl3d_replication_write_uint32(&writer, (Uint32)control_index) ||
        !sdl3d_replication_write_bytes(&writer, runtime->network_schema_hash, SDL3D_REPLICATION_SCHEMA_HASH_SIZE))
    {
        set_error(error_buffer, error_buffer_size, "network control buffer is too small for packet");
        return false;
    }

    if (out_size != NULL)
        *out_size = sdl3d_replication_writer_offset(&writer);
    return true;
}

bool sdl3d_game_data_encode_network_runtime_control(const sdl3d_game_data_runtime *runtime, const char *binding_name,
                                                    Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                                    char *error_buffer, int error_buffer_size)
{
    const char *control_name = NULL;
    if (!sdl3d_game_data_get_network_runtime_control(runtime, binding_name, &control_name))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime control binding '%s' not found",
                   binding_name != NULL ? binding_name : "<null>");
        if (out_size != NULL)
            *out_size = 0U;
        return false;
    }
    return sdl3d_game_data_encode_network_control(runtime, control_name, tick, buffer, buffer_size, out_size,
                                                  error_buffer, error_buffer_size);
}

bool sdl3d_game_data_decode_network_control(const sdl3d_game_data_runtime *runtime, const void *packet,
                                            size_t packet_size, sdl3d_game_data_network_control *out_control,
                                            char *error_buffer, int error_buffer_size)
{
    if (out_control != NULL)
    {
        out_control->name = NULL;
        out_control->direction = SDL3D_GAME_DATA_NETWORK_DIRECTION_INVALID;
        out_control->signal_id = -1;
        out_control->tick = 0U;
    }
    if (runtime == NULL || packet == NULL || packet_size == 0U)
    {
        set_error(error_buffer, error_buffer_size, "network control decode requires runtime and packet");
        return false;
    }
    if (!runtime->has_network_schema)
    {
        set_error(error_buffer, error_buffer_size, "network control decode requires an authored network schema");
        return false;
    }

    size_t required_size = 0U;
    (void)game_data_network_control_packet_size(&required_size);
    if (packet_size != required_size)
    {
        set_errorf(error_buffer, error_buffer_size, "network control packet requires %zu bytes, packet has %zu bytes",
                   required_size, packet_size);
        return false;
    }

    sdl3d_replication_reader reader;
    sdl3d_replication_reader_init(&reader, packet, packet_size);

    Uint32 magic = 0U;
    Uint32 version = 0U;
    Uint32 tick = 0U;
    Uint32 control_index = 0U;
    Uint8 schema_hash[SDL3D_REPLICATION_SCHEMA_HASH_SIZE];
    if (!sdl3d_replication_read_uint32(&reader, &magic) || !sdl3d_replication_read_uint32(&reader, &version) ||
        !sdl3d_replication_read_uint32(&reader, &tick) || !sdl3d_replication_read_uint32(&reader, &control_index) ||
        !sdl3d_replication_read_bytes(&reader, schema_hash, sizeof(schema_hash)) ||
        sdl3d_replication_reader_remaining(&reader) != 0U)
    {
        set_error(error_buffer, error_buffer_size, "network control packet is malformed");
        return false;
    }
    if (magic != SDL3D_GAME_DATA_NETWORK_CONTROL_MAGIC || version != SDL3D_GAME_DATA_NETWORK_CONTROL_VERSION)
    {
        set_error(error_buffer, error_buffer_size, "network control packet has unsupported header");
        return false;
    }
    if (SDL_memcmp(schema_hash, runtime->network_schema_hash, SDL3D_REPLICATION_SCHEMA_HASH_SIZE) != 0)
    {
        set_error(error_buffer, error_buffer_size, "network control schema hash does not match runtime");
        return false;
    }

    yyjson_val *control = game_data_find_network_control_by_index(runtime, control_index);
    if (control == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network control message index is invalid for this runtime");
        return false;
    }

    const sdl3d_game_data_network_direction direction =
        game_data_network_direction_from_string(json_string(control, "direction", NULL));
    const int signal_id = game_data_network_control_signal_id(runtime, control);
    if (direction == SDL3D_GAME_DATA_NETWORK_DIRECTION_INVALID || signal_id < 0)
    {
        set_error(error_buffer, error_buffer_size, "network control message metadata is invalid for this runtime");
        return false;
    }

    if (out_control != NULL)
    {
        out_control->name = json_string(control, "name", NULL);
        out_control->direction = direction;
        out_control->signal_id = signal_id;
        out_control->tick = tick;
    }
    return true;
}

bool sdl3d_game_data_decode_network_runtime_control(const sdl3d_game_data_runtime *runtime, const void *packet,
                                                    size_t packet_size, const char **out_binding,
                                                    sdl3d_game_data_network_control *out_control, char *error_buffer,
                                                    int error_buffer_size)
{
    sdl3d_game_data_network_control control;
    if (out_binding != NULL)
        *out_binding = NULL;
    if (!sdl3d_game_data_decode_network_control(runtime, packet, packet_size, &control, error_buffer,
                                                error_buffer_size))
    {
        return false;
    }

    const char *binding = NULL;
    if (!sdl3d_game_data_get_network_runtime_control_binding(runtime, control.name, &binding))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime control binding for '%s' not found",
                   control.name != NULL ? control.name : "<null>");
        return false;
    }

    if (out_binding != NULL)
        *out_binding = binding;
    if (out_control != NULL)
        *out_control = control;
    return true;
}

bool sdl3d_game_data_send_network_runtime_control(const sdl3d_game_data_runtime *runtime,
                                                  sdl3d_network_session *session, const char *binding_name, Uint32 tick,
                                                  char *error_buffer, int error_buffer_size)
{
    Uint8 packet[SDL3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE];
    size_t packet_size = 0U;
    if (session == NULL || !sdl3d_network_session_is_connected(session))
    {
        set_error(error_buffer, error_buffer_size, "network runtime control send requires connected session");
        return false;
    }
    if (!sdl3d_game_data_encode_network_runtime_control(runtime, binding_name, tick, packet, sizeof(packet),
                                                        &packet_size, error_buffer, error_buffer_size))
    {
        return false;
    }
    if (!sdl3d_network_session_send(session, packet, (int)packet_size))
    {
        set_errorf(error_buffer, error_buffer_size, "network runtime control send failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool sdl3d_game_data_apply_network_control(sdl3d_game_data_runtime *runtime, const void *packet, size_t packet_size,
                                           sdl3d_game_data_network_control *out_control, char *error_buffer,
                                           int error_buffer_size)
{
    sdl3d_game_data_network_control control;
    if (!sdl3d_game_data_decode_network_control(runtime, packet, packet_size, &control, error_buffer,
                                                error_buffer_size))
        return false;

    sdl3d_properties *payload = sdl3d_properties_create();
    if (payload == NULL)
    {
        set_error(error_buffer, error_buffer_size, "network control failed to allocate signal payload");
        return false;
    }

    sdl3d_properties_set_string(payload, "network_control", control.name != NULL ? control.name : "");
    sdl3d_properties_set_string(payload, "network_direction", game_data_network_direction_name(control.direction));
    sdl3d_properties_set_int(payload, "network_tick", (int)SDL_min(control.tick, (Uint32)SDL_MAX_SINT32));
    sdl3d_signal_emit(runtime_bus(runtime), control.signal_id, payload);
    sdl3d_properties_destroy(payload);

    if (out_control != NULL)
        *out_control = control;
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
    for (int i = 0; i < runtime->audio_file_count; ++i)
    {
        SDL_free(runtime->audio_files[i].asset_path);
        SDL_free(runtime->audio_files[i].file_path);
    }
    for (int i = 0; i < runtime->property_snapshot_count; ++i)
    {
        SDL_free(runtime->property_snapshots[i].name);
        SDL_free(runtime->property_snapshots[i].target);
        sdl3d_properties_destroy(runtime->property_snapshots[i].properties);
    }
    for (int i = 0; i < runtime->collection_count; ++i)
    {
        SDL_free(runtime->collections[i].name);
        for (int row = 0; row < runtime->collections[i].row_count; ++row)
            sdl3d_properties_destroy(runtime->collections[i].rows[row]);
        SDL_free(runtime->collections[i].rows);
    }
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        SDL_free(runtime->actor_pools[i].name);
        for (int actor_index = 0; actor_index < runtime->actor_pools[i].capacity; ++actor_index)
            SDL_free(runtime->actor_pools[i].actor_names[actor_index]);
        SDL_free(runtime->actor_pools[i].scenes);
        SDL_free(runtime->actor_pools[i].actor_names);
        SDL_free(runtime->actor_pools[i].spawn_generations);
        SDL_free(runtime->actor_pools[i].lifecycle_states);
    }
    for (int i = 0; i < runtime->direct_connect_session_count; ++i)
    {
        SDL_free(runtime->direct_connect_sessions[i].name);
        sdl3d_network_session_destroy(runtime->direct_connect_sessions[i].session);
    }
    for (int i = 0; i < runtime->host_session_count; ++i)
    {
        SDL_free(runtime->host_sessions[i].name);
        sdl3d_network_session_destroy(runtime->host_sessions[i].session);
    }
    for (int i = 0; i < runtime->discovery_session_count; ++i)
    {
        SDL_free(runtime->discovery_sessions[i].name);
        sdl3d_network_discovery_session_destroy(runtime->discovery_sessions[i].session);
    }
    for (int i = 0; i < runtime->network_diagnostic_count; ++i)
        SDL_free(runtime->network_diagnostics[i].name);

    clear_menu_text_entry_capture(runtime);
    sdl3d_script_engine_destroy(runtime->scripts);
    sdl3d_properties_destroy(runtime->scene_state);
    sdl3d_storage_destroy(runtime->storage);
    if (runtime->owns_assets)
        sdl3d_asset_resolver_destroy(runtime->assets);
    SDL_free(runtime->base_dir);
    SDL_free(runtime->scenes);
    SDL_free(runtime->script_entries);
    SDL_free(runtime->signals);
    SDL_free(runtime->timers);
    SDL_free(runtime->actions);
    SDL_free(runtime->adapters);
    SDL_free(runtime->bindings);
    for (int i = 0; i < runtime->sensor_count; ++i)
        SDL_free(runtime->sensors[i].contact_pairs);
    SDL_free(runtime->sensors);
    SDL_free(runtime->input_bindings);
    SDL_free(runtime->ui_states);
    SDL_free(runtime->animations);
    SDL_free(runtime->audio_files);
    SDL_free(runtime->property_snapshots);
    SDL_free(runtime->collections);
    SDL_free(runtime->actor_pools);
    SDL_free(runtime->direct_connect_sessions);
    SDL_free(runtime->host_sessions);
    SDL_free(runtime->discovery_sessions);
    SDL_free(runtime->network_diagnostics);
    SDL_free(runtime->activity.periodic_elapsed);
    yyjson_doc_free(runtime->doc);
    SDL_free(runtime);
}

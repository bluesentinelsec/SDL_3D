/**
 * @file game_data.c
 * @brief JSON-authored game data runtime implementation.
 */

#include "slayer3d/game_data.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include "game_data_brush_internal.h"
#include "game_data_standard_options.h"
#include "game_data_validation.h"
#include "lauxlib.h"
#include "lua.h"
#include "network_replication_schema.h"
#include "script_internal.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/asset.h"
#include "slayer3d/collision.h"
#include "slayer3d/door.h"
#include "slayer3d/fps_mover.h"
#include "slayer3d/input.h"
#include "slayer3d/level.h"
#include "slayer3d/math.h"
#include "slayer3d/network.h"
#include "slayer3d/script.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/timer_pool.h"
#include "yyjson.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#define SLAYER3D_GAME_DATA_SIGNAL_BASE 20000
#define SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_MAGIC 0x53335253u /* "S3RS" */
#define SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_VERSION 1u
#define SLAYER3D_GAME_DATA_NETWORK_INPUT_MAGIC 0x49335253u /* "S3RI" */
#define SLAYER3D_GAME_DATA_NETWORK_INPUT_VERSION 1u
#define SLAYER3D_GAME_DATA_NETWORK_CONTROL_MAGIC 0x43335253u /* "S3RC" */
#define SLAYER3D_GAME_DATA_NETWORK_CONTROL_VERSION 1u
#define SLAYER3D_GAME_DATA_MENU_TEXT_MAX_BYTES 255

typedef enum game_data_sensor_type
{
    GAME_DATA_SENSOR_BOUNDS_EXIT,
    GAME_DATA_SENSOR_BOUNDS_REFLECT,
    GAME_DATA_SENSOR_CONTACT_2D,
    GAME_DATA_SENSOR_HEARING,
    GAME_DATA_SENSOR_INPUT_PRESSED,
    GAME_DATA_SENSOR_BRUSH_CONTENTS,
    GAME_DATA_SENSOR_BRUSH_PERCEPTION,
    GAME_DATA_SENSOR_PERCEPTION,
    GAME_DATA_SENSOR_SECTOR,
    GAME_DATA_SENSOR_VOLUME,
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
    slayer3d_script_ref lua_function_ref;
    slayer3d_game_data_adapter_fn callback;
    void *userdata;
} adapter_entry;

typedef struct script_entry
{
    const char *id;
    const char *path;
    const char *module;
    const char **dependencies;
    int dependency_count;
    slayer3d_script_ref module_ref;
    bool autoload;
    bool loading;
    bool loaded;
} script_entry;

typedef struct binding_entry
{
    struct slayer3d_game_data_runtime *runtime;
    yyjson_val *actions;
    int connection_id;
} binding_entry;

typedef struct sensor_contact_pair_state
{
    const char *actor_name;
    const char *other_actor_name;
    bool owns_actor_name;
    bool owns_other_actor_name;
    bool active;
    bool seen;
} sensor_contact_pair_state;

typedef struct sensor_entry
{
    game_data_sensor_type type;
    yyjson_val *json;
    const char *name;
    const char *entity;
    const char *other;
    const char *entity_tag;
    const char *other_tag;
    const char *sector_level;
    const char *sector;
    const char *sector_property;
    int sector_index;
    const char *action;
    const char *axis;
    const char *side;
    float min_value;
    float max_value;
    float threshold;
    float range;
    float min_dot;
    float observer_eye_height;
    float target_eye_height;
    const char *yaw_property;
    slayer3d_vec3 volume_min;
    slayer3d_vec3 volume_max;
    int signal_id;
    yyjson_val *actions;
    const char *edge;
    bool was_active;
    sensor_contact_pair_state *contact_pairs;
    int contact_pair_count;
    int contact_pair_capacity;
} sensor_entry;

typedef struct wave_schedule_entry
{
    yyjson_val *schedule;
    float elapsed;
    bool initialized;
} wave_schedule_entry;

typedef struct noise_event_runtime
{
    unsigned int id;
    char key[32];
    const char *source_actor_name;
    slayer3d_vec3 position;
    float radius;
    float loudness;
    float remaining_seconds;
} noise_event_runtime;

typedef struct sensor_actor_list
{
    slayer3d_registered_actor **items;
    int count;
    int capacity;
} sensor_actor_list;

static bool sensor_actor_list_add(sensor_actor_list *list, slayer3d_registered_actor *actor);
static void sensor_actor_list_free(sensor_actor_list *list);
static bool collect_sensor_endpoint_actors(slayer3d_game_data_runtime *runtime, const char *actor_name, const char *tag,
                                           sensor_actor_list *out_list);

typedef struct input_binding_spec
{
    const char *action;
    int action_id;
    slayer3d_input_source source;
    int gamepad_index;
    int required_modifiers;
    int excluded_modifiers;
    union {
        SDL_Scancode scancode;
        Uint8 mouse_button;
        slayer3d_mouse_axis mouse_axis;
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
    slayer3d_game_data_ui_state state;
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
        slayer3d_vec3 as_vec3;
        slayer3d_color as_color;
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
    slayer3d_value_type property_type;
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
    slayer3d_properties *properties;
} property_snapshot;

typedef struct runtime_collection
{
    char *name;
    slayer3d_properties **rows;
    int row_count;
    int row_capacity;
} runtime_collection;

typedef struct grid_map_runtime
{
    char *name;
    char *cells;
    char *walkable;
    int width;
    int height;
    float cell_width;
    float cell_height;
    float row_direction;
    slayer3d_vec3 origin;
    bool wrap_x;
    bool wrap_y;
} grid_map_runtime;

typedef struct grid_actor_index
{
    char *map;
    char *pool;
    slayer3d_registered_actor **actors;
    int width;
    int height;
} grid_actor_index;

typedef struct grid_pickup_kind_runtime
{
    char glyph;
    char *kind;
    int points;
    float z;
    float radius;
    int rings;
    int slices;
    slayer3d_color color;
    bool lighting;
    bool emissive;
} grid_pickup_kind_runtime;

typedef struct grid_pickup_layer_runtime
{
    char *name;
    const grid_map_runtime *map;
    grid_pickup_kind_runtime *kinds;
    int kind_count;
    Uint8 *cells;
    int active_count;
    slayer3d_vec3 *render_positions;
    int render_position_capacity;
} grid_pickup_layer_runtime;

typedef struct sector_level_runtime
{
    char *name;
    slayer3d_sector *sectors;
    const char **sector_names;
    int sector_count;
    slayer3d_level_material *materials;
    int material_count;
    slayer3d_level_light *lights;
    int light_count;
    slayer3d_level lightmapped;
    slayer3d_level vertex_baked;
    slayer3d_level unlit;
    slayer3d_level lightmapped_without_sector_lighting;
    slayer3d_level vertex_baked_without_sector_lighting;
    slayer3d_level unlit_without_sector_lighting;
} sector_level_runtime;

typedef struct brush_world_runtime
{
    slayer3d_game_data_brush_world desc;
    slayer3d_model render_model;
} brush_world_runtime;

typedef struct sector_door_runtime
{
    yyjson_val *json;
    slayer3d_door door;
} sector_door_runtime;

typedef struct sector_platform_runtime
{
    yyjson_val *json;
    const char *name;
    sector_level_runtime *level;
    int sector_index;
    float min_floor_y;
    float max_floor_y;
    float ceil_y;
    float cycle_seconds;
    float rebuild_min_delta;
    float time;
    float last_floor_y;
    bool has_last_floor_y;
    bool enabled;
} sector_platform_runtime;

typedef struct fps_controller_runtime
{
    const char *entity_name;
    yyjson_val *component;
    slayer3d_fps_mover mover;
    bool initialized;
} fps_controller_runtime;

typedef struct patrol_controller_runtime
{
    const char *entity_name;
    yyjson_val *component;
    slayer3d_actor_patrol_controller controller;
    bool initialized;
} patrol_controller_runtime;

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
    Uint64 spawn_attempt_count;
    Uint64 spawn_success_count;
    Uint64 spawn_failure_count;
    Uint64 exhaustion_count;
    Uint64 reuse_count;
    Uint64 despawn_count;
    int peak_active_count;
    char last_spawn_failure_reason[64];
    char last_despawn_reason[64];
    int capacity;
    bool initial_active;
    actor_pool_exhaustion_policy exhaustion;
    actor_pool_scene_exit_policy scene_exit_policy;
} actor_pool_runtime;

typedef struct runtime_direct_connect_session
{
    char *name;
    slayer3d_network_session *session;
} runtime_direct_connect_session;

typedef struct runtime_host_session
{
    char *name;
    slayer3d_network_session *session;
} runtime_host_session;

typedef struct runtime_discovery_session
{
    char *name;
    slayer3d_network_discovery_session *session;
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

typedef struct slayer3d_game_data_runtime
{
    yyjson_doc *doc;
    slayer3d_game_session *session;
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
    wave_schedule_entry *wave_schedules;
    int wave_schedule_count;
    noise_event_runtime *noise_events;
    int noise_event_count;
    int noise_event_capacity;
    unsigned int next_noise_event_id;
    input_binding_spec *input_bindings;
    int input_binding_count;
    int input_binding_capacity;
    menu_input_binding_capture input_capture;
    menu_text_entry_capture text_capture;
    slayer3d_script_engine *scripts;
    script_entry *script_entries;
    int script_count;
    slayer3d_asset_resolver *assets;
    bool owns_assets;
    char *base_dir;
    slayer3d_storage_config storage_config;
    scene_entry *scenes;
    int scene_count;
    int active_scene_index;
    slayer3d_properties *scene_state;
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
    grid_map_runtime *grid_maps;
    int grid_map_count;
    grid_actor_index *grid_actor_indices;
    int grid_actor_index_count;
    int grid_actor_index_capacity;
    grid_pickup_layer_runtime *grid_pickup_layers;
    int grid_pickup_layer_count;
    sector_level_runtime *sector_levels;
    int sector_level_count;
    brush_world_runtime *brush_worlds;
    int brush_world_count;
    slayer3d_game_data_brush_diagnostics brush_diagnostics;
    sector_door_runtime *sector_doors;
    int sector_door_count;
    sector_platform_runtime *sector_platforms;
    int sector_platform_count;
    fps_controller_runtime *fps_controllers;
    int fps_controller_count;
    int fps_controller_capacity;
    patrol_controller_runtime *patrol_controllers;
    int patrol_controller_count;
    int patrol_controller_capacity;
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
    slayer3d_storage *storage;
    bool has_network_schema;
    Uint8 network_schema_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE];
    const char *active_camera;
    scene_activity_state activity;
    float current_dt;
    unsigned int rng_state;
} slayer3d_game_data_runtime;

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

static void clear_menu_text_entry_capture(slayer3d_game_data_runtime *runtime)
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

static bool set_action_keyboard_binding(slayer3d_game_data_runtime *runtime, const char *action, SDL_Scancode scancode);
static bool set_action_mouse_button_binding(slayer3d_game_data_runtime *runtime, const char *action, Uint8 button);
static bool set_action_gamepad_button_binding(slayer3d_game_data_runtime *runtime, const char *action,
                                              SDL_GamepadButton button);
static bool eval_data_condition(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                                const slayer3d_game_data_ui_metrics *metrics);
static bool runtime_actor_is_active(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *actor);
static sector_level_runtime *find_sector_level_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name);
static const sector_level_runtime *find_sector_level_runtime(const slayer3d_game_data_runtime *runtime,
                                                             const char *name);
static int sector_level_find_sector_name(const sector_level_runtime *level, const char *sector_name);
static void modulate_color_by_sector_lighting(slayer3d_color *color, const slayer3d_sector *sector);
static int menu_runtime_item_count(const slayer3d_game_data_runtime *runtime, const scene_menu_state *menu);
static void update_dynamic_list_selection_state(slayer3d_game_data_runtime *runtime, scene_menu_state *menu);
static bool load_editor_metadata(yyjson_val *editor, slayer3d_game_data_editor_metadata *out_metadata,
                                 char *error_buffer, int error_buffer_size);
static void free_editor_metadata(slayer3d_game_data_editor_metadata *metadata);

static float game_data_random01(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return 0.0f;
    runtime->rng_state = runtime->rng_state * 1664525u + 1013904223u;
    return (float)((runtime->rng_state >> 8) & 0x00FFFFFFu) / (float)0x01000000u;
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

static slayer3d_actor_registry *runtime_registry(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? slayer3d_game_session_get_registry(runtime->session) : NULL;
}

static slayer3d_signal_bus *runtime_bus(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? slayer3d_game_session_get_signal_bus(runtime->session) : NULL;
}

static slayer3d_timer_pool *runtime_timers(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? slayer3d_game_session_get_timer_pool(runtime->session) : NULL;
}

static slayer3d_input_manager *runtime_input(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? slayer3d_game_session_get_input(runtime->session) : NULL;
}

static void actor_set_position(slayer3d_registered_actor *actor, slayer3d_vec3 position);
static void lua_push_actor_wrapper(lua_State *lua, const slayer3d_registered_actor *actor);
static void copy_property_value(slayer3d_properties *target, const char *key, const slayer3d_value *value);
static yyjson_val *obj_get(yyjson_val *object, const char *key);
static const char *json_string(yyjson_val *object, const char *key, const char *fallback);
static yyjson_val *runtime_root(const slayer3d_game_data_runtime *runtime);
static const brush_world_runtime *find_brush_world_runtime(const slayer3d_game_data_runtime *runtime, const char *name);
static actor_pool_runtime *find_actor_pool(slayer3d_game_data_runtime *runtime, const char *name);
static const actor_pool_runtime *find_actor_pool_const(const slayer3d_game_data_runtime *runtime, const char *name);
static actor_pool_runtime *find_actor_pool_for_actor(slayer3d_game_data_runtime *runtime, const char *actor_name,
                                                     int *out_index);
static const actor_pool_runtime *find_actor_pool_for_actor_const(const slayer3d_game_data_runtime *runtime,
                                                                 const char *actor_name, int *out_index);
static bool actor_pool_in_scene(const actor_pool_runtime *pool, const char *scene_name);
static slayer3d_registered_actor *actor_pool_allocate(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                                      int *out_index);
static actor_lifecycle_state actor_pool_lifecycle_state(const actor_pool_runtime *pool, int index);
static void actor_pool_set_lifecycle_state(actor_pool_runtime *pool, slayer3d_registered_actor *actor, int index,
                                           actor_lifecycle_state state);
static bool actor_pool_actor_is_active(const actor_pool_runtime *pool, const slayer3d_registered_actor *actor,
                                       int index);
static bool actor_pool_actor_is_available(const actor_pool_runtime *pool, const slayer3d_registered_actor *actor,
                                          int index);
static int actor_pool_active_count(const slayer3d_game_data_runtime *runtime, const actor_pool_runtime *pool);
static int actor_pool_available_count(const slayer3d_game_data_runtime *runtime, const actor_pool_runtime *pool);
static void actor_pool_note_spawn_attempt(actor_pool_runtime *pool);
static void actor_pool_note_spawn_success(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool);
static void actor_pool_note_spawn_failure(actor_pool_runtime *pool, const char *reason);
static bool initialize_pooled_actor(actor_pool_runtime *pool, slayer3d_registered_actor *actor, int index, bool active);
static bool actor_pool_initialize_slot(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool, int index,
                                       bool active);
static bool actor_pool_request_despawn(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                       slayer3d_registered_actor *actor, int index, const char *reason);
static bool apply_actor_pool_scene_exit_policies(slayer3d_game_data_runtime *runtime, const char *from_scene,
                                                 const char *to_scene);
static slayer3d_registered_actor *action_source_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload);
static bool actor_matches_target_filter(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_registered_actor *target,
                                        const slayer3d_registered_actor *source, yyjson_val *json,
                                        const slayer3d_properties *payload, const char *fallback_tag,
                                        bool fallback_exclude_source);
static void actor_lifecycle_defer_begin(slayer3d_game_data_runtime *runtime);
static void actor_lifecycle_defer_end(slayer3d_game_data_runtime *runtime);
static bool entity_json_has_tags(yyjson_val *entity, const char *const *tags, int tag_count);
static const grid_map_runtime *find_grid_map(const slayer3d_game_data_runtime *runtime, const char *name);
static bool grid_map_normalize_cell(const grid_map_runtime *map, int *col, int *row);
static char grid_map_cell(const grid_map_runtime *map, int col, int row);
static bool grid_map_cell_to_world(const grid_map_runtime *map, int col, int row, slayer3d_vec3 *out_position);
static bool grid_map_world_to_cell(const grid_map_runtime *map, float x, float y, int *out_col, int *out_row);
static bool grid_map_is_walkable(const grid_map_runtime *map, int col, int row);
static bool grid_map_next_step(const grid_map_runtime *map, int start_col, int start_row, int goal_col, int goal_row,
                               int *out_col, int *out_row);
static bool grid_actor_index_register(slayer3d_game_data_runtime *runtime, const grid_map_runtime *map,
                                      const char *pool_name, slayer3d_registered_actor *actor, int col, int row);
static void grid_actor_index_clear(slayer3d_game_data_runtime *runtime, const grid_map_runtime *map,
                                   const char *pool_name);
static slayer3d_registered_actor *grid_actor_index_find(slayer3d_game_data_runtime *runtime, const char *map_name,
                                                        const char *pool_name, int col, int row);
static grid_pickup_layer_runtime *find_grid_pickup_layer(slayer3d_game_data_runtime *runtime, const char *name);
static bool grid_pickup_layer_reset(slayer3d_game_data_runtime *runtime, grid_pickup_layer_runtime *layer);
static bool grid_pickup_layer_collect_at(slayer3d_game_data_runtime *runtime, grid_pickup_layer_runtime *layer, int col,
                                         int row, grid_pickup_kind_runtime *out_kind);

static slayer3d_game_data_runtime *lua_runtime(lua_State *lua)
{
    return (slayer3d_game_data_runtime *)lua_touserdata(lua, lua_upvalueindex(1));
}

static lua_Integer lua_counter_value(Uint64 value)
{
    return (lua_Integer)SDL_min(value, (Uint64)LUA_MAXINTEGER);
}

static slayer3d_registered_actor *lua_actor_arg(lua_State *lua, slayer3d_game_data_runtime *runtime, int index)
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
    return slayer3d_game_data_find_actor(runtime, actor_name);
}

static bool ensure_runtime_storage(slayer3d_game_data_runtime *runtime, char *error_buffer, int error_buffer_size);

static int lua_get_position(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
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
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor == NULL)
        return 0;

    const slayer3d_vec3 position = slayer3d_vec3_make((float)luaL_checknumber(lua, 2), (float)luaL_checknumber(lua, 3),
                                                      (float)luaL_optnumber(lua, 4, actor->position.z));
    actor_set_position(actor, position);
    return 0;
}

static int lua_get_float(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const float fallback = (float)luaL_optnumber(lua, 3, 0.0);
    lua_pushnumber(lua, actor != NULL ? slayer3d_properties_get_float(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_float(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        slayer3d_properties_set_float(actor->props, luaL_checkstring(lua, 2), (float)luaL_checknumber(lua, 3));
    return 0;
}

static int lua_get_int(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const int fallback = (int)luaL_optinteger(lua, 3, 0);
    lua_pushinteger(lua, actor != NULL ? slayer3d_properties_get_int(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_int(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        slayer3d_properties_set_int(actor->props, luaL_checkstring(lua, 2), (int)luaL_checkinteger(lua, 3));
    return 0;
}

static int lua_get_bool(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const bool fallback = lua_toboolean(lua, 3);
    lua_pushboolean(lua, actor != NULL ? slayer3d_properties_get_bool(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_bool(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        slayer3d_properties_set_bool(actor->props, luaL_checkstring(lua, 2), lua_toboolean(lua, 3));
    return 0;
}

static int lua_get_string(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const char *fallback = luaL_optstring(lua, 3, "");
    lua_pushstring(lua, actor != NULL ? slayer3d_properties_get_string(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_string(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        slayer3d_properties_set_string(actor->props, luaL_checkstring(lua, 2), luaL_checkstring(lua, 3));
    return 0;
}

static int lua_get_vec3(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    if (actor == NULL)
    {
        lua_pushnil(lua);
        return 1;
    }
    const slayer3d_vec3 value = slayer3d_properties_get_vec3(actor->props, key, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    lua_pushnumber(lua, value.x);
    lua_pushnumber(lua, value.y);
    lua_pushnumber(lua, value.z);
    return 3;
}

static int lua_set_vec3(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
    {
        slayer3d_properties_set_vec3(actor->props, luaL_checkstring(lua, 2),
                                     slayer3d_vec3_make((float)luaL_checknumber(lua, 3),
                                                        (float)luaL_checknumber(lua, 4),
                                                        (float)luaL_optnumber(lua, 5, 0.0)));
    }
    return 0;
}

static int lua_actor_active(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    lua_pushboolean(lua, actor != NULL && actor->active);
    return 1;
}

static int lua_get_dt(lua_State *lua)
{
    lua_pushnumber(lua, slayer3d_game_data_delta_time(lua_runtime(lua)));
    return 1;
}

static int lua_state_get(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const slayer3d_value *value =
        slayer3d_properties_get_value(slayer3d_game_data_scene_state(runtime), luaL_checkstring(lua, 1));
    if (value == NULL)
    {
        lua_pushvalue(lua, 2);
        return 1;
    }

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        lua_pushinteger(lua, value->as_int);
        break;
    case SLAYER3D_VALUE_FLOAT:
        lua_pushnumber(lua, value->as_float);
        break;
    case SLAYER3D_VALUE_BOOL:
        lua_pushboolean(lua, value->as_bool);
        break;
    case SLAYER3D_VALUE_STRING:
        lua_pushstring(lua, value->as_string != NULL ? value->as_string : "");
        break;
    case SLAYER3D_VALUE_VEC3:
        lua_newtable(lua);
        lua_pushnumber(lua, value->as_vec3.x);
        lua_setfield(lua, -2, "x");
        lua_pushnumber(lua, value->as_vec3.y);
        lua_setfield(lua, -2, "y");
        lua_pushnumber(lua, value->as_vec3.z);
        lua_setfield(lua, -2, "z");
        break;
    case SLAYER3D_VALUE_COLOR:
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
    slayer3d_properties *state = slayer3d_game_data_mutable_scene_state(lua_runtime(lua));
    const char *key = luaL_checkstring(lua, 1);
    if (state == NULL)
        return 0;

    if (lua_isnoneornil(lua, 2))
    {
        slayer3d_properties_remove(state, key);
    }
    else if (lua_isboolean(lua, 2))
    {
        slayer3d_properties_set_bool(state, key, lua_toboolean(lua, 2));
    }
    else if (lua_isinteger(lua, 2))
    {
        slayer3d_properties_set_int(state, key, (int)lua_tointeger(lua, 2));
    }
    else if (lua_isnumber(lua, 2))
    {
        slayer3d_properties_set_float(state, key, (float)lua_tonumber(lua, 2));
    }
    else if (lua_isstring(lua, 2))
    {
        slayer3d_properties_set_string(state, key, lua_tostring(lua, 2));
    }
    else if (lua_istable(lua, 2))
    {
        lua_getfield(lua, 2, "x");
        lua_getfield(lua, 2, "y");
        lua_getfield(lua, 2, "z");
        const bool has_xy = lua_isnumber(lua, -3) && lua_isnumber(lua, -2);
        const slayer3d_vec3 value = slayer3d_vec3_make((float)lua_tonumber(lua, -3), (float)lua_tonumber(lua, -2),
                                                       lua_isnumber(lua, -1) ? (float)lua_tonumber(lua, -1) : 0.0f);
        lua_pop(lua, 3);
        if (has_xy)
            slayer3d_properties_set_vec3(state, key, value);
    }
    return 0;
}

static int lua_random(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    if (runtime == NULL)
    {
        lua_pushnumber(lua, 0.0);
        return 1;
    }

    lua_pushnumber(lua, (lua_Number)game_data_random01(runtime));
    return 1;
}

static int lua_actor_with_tags(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
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

    lua_push_actor_wrapper(lua, slayer3d_game_data_find_actor_with_tags(runtime, tags, tag_count));
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

static bool lua_read_vec3_value(lua_State *lua, int index, slayer3d_vec3 fallback, slayer3d_vec3 *out_value)
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
    slayer3d_vec3 value = slayer3d_vec3_make(has_xy ? (float)lua_tonumber(lua, -3) : fallback.x,
                                             has_xy ? (float)lua_tonumber(lua, -2) : fallback.y,
                                             lua_isnumber(lua, -1) ? (float)lua_tonumber(lua, -1) : fallback.z);
    lua_pop(lua, 3);

    if (!has_xy)
    {
        lua_geti(lua, index, 1);
        lua_geti(lua, index, 2);
        lua_geti(lua, index, 3);
        has_xy = lua_isnumber(lua, -3) && lua_isnumber(lua, -2);
        value = slayer3d_vec3_make(has_xy ? (float)lua_tonumber(lua, -3) : fallback.x,
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

static bool lua_read_vec3_field(lua_State *lua, int table_index, const char *field, slayer3d_vec3 fallback,
                                slayer3d_vec3 *out_value)
{
    if (!lua_istable(lua, table_index) || field == NULL)
        return false;
    table_index = lua_absindex(lua, table_index);
    lua_getfield(lua, table_index, field);
    const bool ok = lua_read_vec3_value(lua, -1, fallback, out_value);
    lua_pop(lua, 1);
    return ok;
}

static void lua_set_actor_property_from_value(lua_State *lua, slayer3d_registered_actor *actor, const char *key,
                                              int index)
{
    if (actor == NULL || key == NULL)
        return;
    index = lua_absindex(lua, index);
    if (lua_isboolean(lua, index))
        slayer3d_properties_set_bool(actor->props, key, lua_toboolean(lua, index));
    else if (lua_isinteger(lua, index))
        slayer3d_properties_set_int(actor->props, key, (int)lua_tointeger(lua, index));
    else if (lua_isnumber(lua, index))
        slayer3d_properties_set_float(actor->props, key, (float)lua_tonumber(lua, index));
    else if (lua_isstring(lua, index))
        slayer3d_properties_set_string(actor->props, key, lua_tostring(lua, index));
    else if (lua_istable(lua, index))
    {
        slayer3d_vec3 value;
        if (lua_read_vec3_value(lua, index, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), &value))
            slayer3d_properties_set_vec3(actor->props, key, value);
    }
}

static slayer3d_registered_actor *lua_actor_from_value(lua_State *lua, slayer3d_game_data_runtime *runtime, int index)
{
    if (lua_isnoneornil(lua, index))
        return NULL;
    if (lua_istable(lua, index))
    {
        lua_getfield(lua, index, "_name");
        const char *name = lua_tostring(lua, -1);
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, name);
        lua_pop(lua, 1);
        return actor;
    }
    if (lua_isstring(lua, index))
        return slayer3d_game_data_find_actor(runtime, lua_tostring(lua, index));
    return NULL;
}

static int lua_spawn_actor(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    actor_pool_runtime *pool = find_actor_pool(runtime, luaL_checkstring(lua, 1));
    actor_pool_note_spawn_attempt(pool);
    int actor_index = -1;
    slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
    if (pool == NULL || actor == NULL || actor_index < 0)
    {
        actor_pool_note_spawn_failure(pool, "exhausted");
        lua_pushnil(lua);
        lua_pushstring(lua, "actor pool exhausted or missing");
        return 2;
    }

    actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
    if (!initialize_pooled_actor(pool, actor, actor_index, true))
    {
        actor_pool_note_spawn_failure(pool, "initialize_failed");
        lua_pushnil(lua);
        lua_pushstring(lua, "failed to initialize pooled actor");
        return 2;
    }

    if (pool->spawn_generations != NULL)
    {
        pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
        slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                    (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
    }

    slayer3d_vec3 position = actor->position;
    if (lua_istable(lua, 2))
    {
        lua_read_vec3_field(lua, 2, "position", position, &position);
        lua_getfield(lua, 2, "from");
        slayer3d_registered_actor *from_actor = lua_actor_from_value(lua, runtime, -1);
        lua_pop(lua, 1);
        if (from_actor != NULL)
            position = from_actor->position;

        slayer3d_vec3 offset = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
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
    actor_pool_note_spawn_success(runtime, pool);
    lua_push_actor_wrapper(lua, actor);
    lua_pushinteger(lua, actor->id);
    lua_pushinteger(lua, actor_index);
    return 3;
}

static int lua_despawn_actor(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_from_value(lua, runtime, 1);
    if (actor == NULL)
    {
        lua_pushboolean(lua, false);
        return 1;
    }

    int actor_index = -1;
    actor_pool_runtime *pool = find_actor_pool_for_actor(runtime, actor->name, &actor_index);
    const char *reason = lua_isstring(lua, 2) ? lua_tostring(lua, 2) : "lua";
    const bool ok = pool != NULL && actor_index >= 0
                        ? actor_pool_request_despawn(runtime, pool, actor, actor_index, reason)
                        : (actor->active = false, true);
    lua_pushboolean(lua, ok);
    return 1;
}

static int lua_despawn_actors_by_tag(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *tag = luaL_checkstring(lua, 1);
    const char *reason = lua_isstring(lua, 2) ? lua_tostring(lua, 2) : "lua_tag";
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
                slayer3d_registered_actor *actor =
                    slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
                if (actor_pool_actor_is_active(pool, actor, actor_index) &&
                    actor_pool_request_despawn(runtime, pool, actor, actor_index, reason))
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
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    actor_pool_runtime *pool = find_actor_pool(runtime, luaL_checkstring(lua, 1));
    lua_pushinteger(lua, actor_pool_active_count(runtime, pool));
    return 1;
}

static int lua_pool_available_count(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    actor_pool_runtime *pool = find_actor_pool(runtime, luaL_checkstring(lua, 1));
    lua_pushinteger(lua, actor_pool_available_count(runtime, pool));
    return 1;
}

static int lua_pool_peak_active_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? pool->peak_active_count : 0);
    return 1;
}

static int lua_pool_spawn_attempt_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->spawn_attempt_count) : 0);
    return 1;
}

static int lua_pool_spawn_success_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->spawn_success_count) : 0);
    return 1;
}

static int lua_pool_spawn_failure_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->spawn_failure_count) : 0);
    return 1;
}

static int lua_pool_exhaustion_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->exhaustion_count) : 0);
    return 1;
}

static int lua_pool_reuse_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->reuse_count) : 0);
    return 1;
}

static int lua_pool_despawn_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->despawn_count) : 0);
    return 1;
}

static int lua_pool_last_spawn_failure_reason(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushstring(lua, pool != NULL && pool->last_spawn_failure_reason[0] != '\0' ? pool->last_spawn_failure_reason
                                                                                   : "none");
    return 1;
}

static int lua_pool_last_despawn_reason(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushstring(lua, pool != NULL && pool->last_despawn_reason[0] != '\0' ? pool->last_despawn_reason : "none");
    return 1;
}

static int lua_active_actors_with_tags(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
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
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(entity, "name", NULL));
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
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index))
            {
                lua_push_actor_wrapper(lua, actor);
                lua_seti(lua, -2, out_index++);
            }
        }
    }
    return 1;
}

static int lua_grid_actor_at(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *map_name = luaL_checkstring(lua, 1);
    const char *pool_name = luaL_checkstring(lua, 2);
    const int col = (int)luaL_checkinteger(lua, 3);
    const int row = (int)luaL_checkinteger(lua, 4);
    lua_push_actor_wrapper(lua, grid_actor_index_find(runtime, map_name, pool_name, col, row));
    return 1;
}

static void lua_push_grid_pickup(lua_State *lua, const grid_pickup_kind_runtime *kind, int col, int row)
{
    if (kind == NULL)
    {
        lua_pushnil(lua);
        return;
    }
    lua_newtable(lua);
    lua_pushstring(lua, kind->kind != NULL ? kind->kind : "");
    lua_setfield(lua, -2, "kind");
    lua_pushinteger(lua, kind->points);
    lua_setfield(lua, -2, "points");
    lua_pushinteger(lua, col);
    lua_setfield(lua, -2, "col");
    lua_pushinteger(lua, row);
    lua_setfield(lua, -2, "row");
}

static int lua_grid_pickup_at(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    grid_pickup_layer_runtime *layer = find_grid_pickup_layer(runtime, luaL_checkstring(lua, 1));
    int col = (int)luaL_checkinteger(lua, 2);
    int row = (int)luaL_checkinteger(lua, 3);
    if (layer == NULL || layer->cells == NULL || !grid_map_normalize_cell(layer->map, &col, &row))
    {
        lua_pushnil(lua);
        return 1;
    }
    const Uint8 kind_cell = layer->cells[row * layer->map->width + col];
    lua_push_grid_pickup(
        lua, kind_cell > 0 && kind_cell <= (Uint8)layer->kind_count ? &layer->kinds[kind_cell - 1] : NULL, col, row);
    return 1;
}

static int lua_grid_collect_at(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    grid_pickup_layer_runtime *layer = find_grid_pickup_layer(runtime, luaL_checkstring(lua, 1));
    int col = (int)luaL_checkinteger(lua, 2);
    int row = (int)luaL_checkinteger(lua, 3);
    grid_pickup_kind_runtime kind;
    SDL_zero(kind);
    if (!grid_pickup_layer_collect_at(runtime, layer, col, row, &kind))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_push_grid_pickup(lua, &kind, col, row);
    return 1;
}

static int lua_grid_pickup_count(lua_State *lua)
{
    grid_pickup_layer_runtime *layer = find_grid_pickup_layer(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, layer != NULL ? layer->active_count : 0);
    return 1;
}

static int lua_log(lua_State *lua)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[lua] %s", luaL_checkstring(lua, 1));
    return 0;
}

static int lua_storage_read(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    if (!ensure_runtime_storage(runtime, error, (int)sizeof(error)))
    {
        lua_pushnil(lua);
        lua_pushstring(lua, error);
        return 2;
    }

    slayer3d_storage_buffer buffer;
    SDL_zero(buffer);
    if (!slayer3d_storage_read_file(runtime->storage, path, &buffer, error, (int)sizeof(error)))
    {
        lua_pushnil(lua);
        lua_pushstring(lua, error);
        return 2;
    }

    lua_pushlstring(lua, (const char *)buffer.data, buffer.size);
    slayer3d_storage_buffer_free(&buffer);
    return 1;
}

static int lua_storage_write(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);
    size_t size = 0u;
    const char *data = luaL_checklstring(lua, 2, &size);

    char error[256];
    const bool ok = ensure_runtime_storage(runtime, error, (int)sizeof(error)) &&
                    slayer3d_storage_write_file(runtime->storage, path, data, size, error, (int)sizeof(error));
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
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    const bool ok =
        ensure_runtime_storage(runtime, error, (int)sizeof(error)) && slayer3d_storage_exists(runtime->storage, path);
    lua_pushboolean(lua, ok);
    return 1;
}

static int lua_storage_mkdir(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    const bool ok = ensure_runtime_storage(runtime, error, (int)sizeof(error)) &&
                    slayer3d_storage_create_directory(runtime->storage, path, error, (int)sizeof(error));
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
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    const bool ok = ensure_runtime_storage(runtime, error, (int)sizeof(error)) &&
                    slayer3d_storage_delete(runtime->storage, path, error, (int)sizeof(error));
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

static int lua_grid_cell_to_world(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const int col = (int)luaL_checkinteger(lua, 2);
    const int row = (int)luaL_checkinteger(lua, 3);
    slayer3d_vec3 position;
    if (!grid_map_cell_to_world(map, col, row, &position))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_pushnumber(lua, position.x);
    lua_pushnumber(lua, position.y);
    lua_pushnumber(lua, position.z);
    return 3;
}

static int lua_grid_world_to_cell(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const float x = (float)luaL_checknumber(lua, 2);
    const float y = (float)luaL_checknumber(lua, 3);
    int col = 0;
    int row = 0;
    if (!grid_map_world_to_cell(map, x, y, &col, &row))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_pushinteger(lua, col);
    lua_pushinteger(lua, row);
    return 2;
}

static int lua_grid_tile(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const int col = (int)luaL_checkinteger(lua, 2);
    const int row = (int)luaL_checkinteger(lua, 3);
    const char cell = grid_map_cell(map, col, row);
    if (cell == '\0')
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_pushlstring(lua, &cell, 1);
    return 1;
}

static int lua_grid_walkable(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const int col = (int)luaL_checkinteger(lua, 2);
    const int row = (int)luaL_checkinteger(lua, 3);
    lua_pushboolean(lua, grid_map_is_walkable(map, col, row));
    return 1;
}

static void lua_push_grid_cell(lua_State *lua, const grid_map_runtime *map, int col, int row)
{
    lua_newtable(lua);
    lua_pushinteger(lua, col);
    lua_setfield(lua, -2, "col");
    lua_pushinteger(lua, row);
    lua_setfield(lua, -2, "row");
    const char cell = grid_map_cell(map, col, row);
    if (cell != '\0')
    {
        lua_pushlstring(lua, &cell, 1);
        lua_setfield(lua, -2, "tile");
    }
}

static int lua_grid_neighbors(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const int col = (int)luaL_checkinteger(lua, 2);
    const int row = (int)luaL_checkinteger(lua, 3);
    lua_newtable(lua);
    if (map == NULL)
        return 1;

    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, -1}, {0, 1}};
    int output_index = 1;
    for (size_t d = 0; d < SDL_arraysize(dirs); ++d)
    {
        int next_col = col + dirs[d][0];
        int next_row = row + dirs[d][1];
        if (!grid_map_normalize_cell(map, &next_col, &next_row) || !grid_map_is_walkable(map, next_col, next_row))
            continue;
        lua_push_grid_cell(lua, map, next_col, next_row);
        lua_rawseti(lua, -2, output_index++);
    }
    return 1;
}

static int lua_grid_next_step(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const int start_col = (int)luaL_checkinteger(lua, 2);
    const int start_row = (int)luaL_checkinteger(lua, 3);
    const int goal_col = (int)luaL_checkinteger(lua, 4);
    const int goal_row = (int)luaL_checkinteger(lua, 5);
    int next_col = start_col;
    int next_row = start_row;
    if (!grid_map_next_step(map, start_col, start_row, goal_col, goal_row, &next_col, &next_row))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_pushinteger(lua, next_col);
    lua_pushinteger(lua, next_row);
    return 2;
}

static void lua_push_sector_nav_node(lua_State *lua, const slayer3d_game_data_sector_nav_node *node)
{
    if (node == NULL || node->name == NULL)
    {
        lua_pushnil(lua);
        return;
    }
    lua_newtable(lua);
    lua_pushstring(lua, node->name);
    lua_setfield(lua, -2, "name");
    lua_pushinteger(lua, node->sector_index);
    lua_setfield(lua, -2, "sector_index");
    lua_pushnumber(lua, (lua_Number)node->position.x);
    lua_setfield(lua, -2, "x");
    lua_pushnumber(lua, (lua_Number)node->position.y);
    lua_setfield(lua, -2, "y");
    lua_pushnumber(lua, (lua_Number)node->position.z);
    lua_setfield(lua, -2, "z");
    lua_pushnumber(lua, (lua_Number)node->position.x);
    lua_rawseti(lua, -2, 1);
    lua_pushnumber(lua, (lua_Number)node->position.y);
    lua_rawseti(lua, -2, 2);
    lua_pushnumber(lua, (lua_Number)node->position.z);
    lua_rawseti(lua, -2, 3);
}

static slayer3d_vec3 lua_vec3_args(lua_State *lua, int first_index)
{
    return slayer3d_vec3_make((float)luaL_checknumber(lua, first_index), (float)luaL_checknumber(lua, first_index + 1),
                              (float)luaL_checknumber(lua, first_index + 2));
}

static int lua_sector_nav_nearest(lua_State *lua)
{
    slayer3d_game_data_sector_nav_node node;
    const char *graph = luaL_checkstring(lua, 1);
    const slayer3d_vec3 position = lua_vec3_args(lua, 2);
    if (!slayer3d_game_data_sector_nav_nearest_node(lua_runtime(lua), graph, position, &node))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_push_sector_nav_node(lua, &node);
    return 1;
}

static int lua_sector_nav_path_available(lua_State *lua)
{
    const char *graph = luaL_checkstring(lua, 1);
    const slayer3d_vec3 start = lua_vec3_args(lua, 2);
    const slayer3d_vec3 goal = lua_vec3_args(lua, 5);
    lua_pushboolean(lua, slayer3d_game_data_sector_nav_path_available(lua_runtime(lua), graph, start, goal));
    return 1;
}

static int lua_sector_nav_next_node(lua_State *lua)
{
    slayer3d_game_data_sector_nav_node node;
    const char *graph = luaL_checkstring(lua, 1);
    const slayer3d_vec3 start = lua_vec3_args(lua, 2);
    const slayer3d_vec3 goal = lua_vec3_args(lua, 5);
    if (!slayer3d_game_data_sector_nav_next_node(lua_runtime(lua), graph, start, goal, &node))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_push_sector_nav_node(lua, &node);
    return 1;
}

static int lua_sector_nav_path(lua_State *lua)
{
    const char *graph = luaL_checkstring(lua, 1);
    const slayer3d_vec3 start = lua_vec3_args(lua, 2);
    const slayer3d_vec3 goal = lua_vec3_args(lua, 5);
    int node_count = 0;
    float cost = 0.0f;
    if (!slayer3d_game_data_sector_nav_path(lua_runtime(lua), graph, start, goal, NULL, 0, &node_count, &cost) ||
        node_count <= 0)
    {
        lua_pushnil(lua);
        return 1;
    }

    slayer3d_game_data_sector_nav_node *nodes = (slayer3d_game_data_sector_nav_node *)SDL_malloc(
        sizeof(slayer3d_game_data_sector_nav_node) * (size_t)node_count);
    if (nodes == NULL || !slayer3d_game_data_sector_nav_path(lua_runtime(lua), graph, start, goal, nodes, node_count,
                                                             &node_count, &cost))
    {
        SDL_free(nodes);
        lua_pushnil(lua);
        return 1;
    }

    lua_newtable(lua);
    for (int i = 0; i < node_count; ++i)
    {
        lua_push_sector_nav_node(lua, &nodes[i]);
        lua_rawseti(lua, -2, i + 1);
    }
    lua_pushnumber(lua, (lua_Number)cost);
    lua_setfield(lua, -2, "cost");
    SDL_free(nodes);
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
        "function Actor:get_float(key, fallback) return slayer3d.get_float(self, key, fallback or 0) end\n"
        "function Actor:set_float(key, value) slayer3d.set_float(self, key, value) end\n"
        "function Actor:get_int(key, fallback) return slayer3d.get_int(self, key, fallback or 0) end\n"
        "function Actor:set_int(key, value) slayer3d.set_int(self, key, value) end\n"
        "function Actor:get_bool(key, fallback) return slayer3d.get_bool(self, key, fallback or false) end\n"
        "function Actor:set_bool(key, value) slayer3d.set_bool(self, key, value and true or false) end\n"
        "function Actor:get_string(key, fallback) return slayer3d.get_string(self, key, fallback or '') end\n"
        "function Actor:set_string(key, value) slayer3d.set_string(self, key, value or '') end\n"
        "function Actor:get_vec3(key, fallback)\n"
        "    local x, y, z = slayer3d.get_vec3(self, key)\n"
        "    if x == nil then return fallback end\n"
        "    return Vec3(x, y, z)\n"
        "end\n"
        "function Actor:set_vec3(key, value)\n"
        "    value = as_vec3(value)\n"
        "    slayer3d.set_vec3(self, key, value.x, value.y, value.z)\n"
        "end\n"
        "function Actor:get_position()\n"
        "    local x, y, z = slayer3d.get_position(self)\n"
        "    if x == nil then return nil end\n"
        "    return Vec3(x, y, z)\n"
        "end\n"
        "function Actor:set_position(value)\n"
        "    value = as_vec3(value)\n"
        "    slayer3d.set_position(self, value.x, value.y, value.z)\n"
        "end\n"
        "Actor.__index = function(self, key)\n"
        "    if key == 'name' then return rawget(self, '_name') end\n"
        "    if key == 'active' then return slayer3d.actor_active(self) end\n"
        "    if key == 'position' then return Actor.get_position(self) end\n"
        "    if key == 'velocity' then return Actor.get_vec3(self, 'velocity', Vec3(0, 0, 0)) end\n"
        "    return Actor[key]\n"
        "end\n"
        "Actor.__newindex = function(self, key, value)\n"
        "    if key == 'position' then Actor.set_position(self, value); return end\n"
        "    if key == 'velocity' then Actor.set_vec3(self, 'velocity', value); return end\n"
        "    rawset(self, key, value)\n"
        "end\n",
        "function slayer3d.actor(name)\n"
        "    if name == nil or name == '' then return nil end\n"
        "    return setmetatable({ _name = name }, Actor)\n"
        "end\n"
        "function Actor:is_active() return slayer3d.actor_active(self) end\n"
        "function Actor:despawn() return slayer3d.despawn(self) end\n"
        "function slayer3d._context(adapter, dt)\n"
        "    return {\n"
        "        adapter = adapter,\n"
        "        name = adapter,\n"
        "        dt = dt or 0,\n"
        "        actor = function(self_or_name, maybe_name) return slayer3d.actor(maybe_name or self_or_name) end,\n"
        "        spawn = function(self_or_pool, maybe_pool, maybe_options)\n"
        "            if type(self_or_pool) == 'table' and self_or_pool.adapter ~= nil then\n"
        "                return slayer3d.spawn(maybe_pool, maybe_options)\n"
        "            end\n"
        "            return slayer3d.spawn(self_or_pool, maybe_pool)\n"
        "        end,\n"
        "        despawn = function(self_or_actor, maybe_actor, maybe_reason)\n"
        "            if type(self_or_actor) == 'table' and self_or_actor.adapter ~= nil then\n"
        "                return slayer3d.despawn(maybe_actor, maybe_reason)\n"
        "            end\n"
        "            return slayer3d.despawn(self_or_actor, maybe_actor)\n"
        "        end,\n"
        "        despawn_by_tag = function(self_or_tag, maybe_tag, maybe_reason)\n"
        "            if type(self_or_tag) == 'table' and self_or_tag.adapter ~= nil then\n"
        "                return slayer3d.despawn_by_tag(maybe_tag, maybe_reason)\n"
        "            end\n"
        "            return slayer3d.despawn_by_tag(self_or_tag, maybe_tag)\n"
        "        end,\n"
        "        actor_with_tags = function(self_or_tag, maybe_tag, ...)\n"
        "            if type(self_or_tag) == 'table' and self_or_tag.adapter ~= nil then\n"
        "                return slayer3d.actor_with_tags(maybe_tag, ...)\n"
        "            end\n"
        "            if type(self_or_tag) == 'table' then return slayer3d.actor_with_tags(self_or_tag) end\n"
        "            return slayer3d.actor_with_tags(self_or_tag, maybe_tag, ...)\n"
        "        end,\n",
        "        active_actors_with_tags = function(self_or_tag, maybe_tag, ...)\n"
        "            if type(self_or_tag) == 'table' and self_or_tag.adapter ~= nil then\n"
        "                return slayer3d.active_actors_with_tags(maybe_tag, ...)\n"
        "            end\n"
        "            if type(self_or_tag) == 'table' then return slayer3d.active_actors_with_tags(self_or_tag) end\n"
        "            return slayer3d.active_actors_with_tags(self_or_tag, maybe_tag, ...)\n"
        "        end,\n"
        "        pool_capacity = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_capacity(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_active_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_active_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_available_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_available_count(maybe_pool or self_or_pool)\n"
        "        end,\n",
        "        pool_peak_active_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_peak_active_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_spawn_attempt_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_spawn_attempt_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_spawn_success_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_spawn_success_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_spawn_failure_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_spawn_failure_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_exhaustion_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_exhaustion_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_reuse_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_reuse_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_despawn_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_despawn_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_last_spawn_failure_reason = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_last_spawn_failure_reason(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_last_despawn_reason = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_last_despawn_reason(maybe_pool or self_or_pool)\n"
        "        end,\n",
        "        grid_cell_to_world = function(self_or_map, maybe_map, maybe_col, maybe_row)\n"
        "            local map, col, row\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                map, col, row = maybe_map, maybe_col, maybe_row\n"
        "            else\n"
        "                map, col, row = self_or_map, maybe_map, maybe_col\n"
        "            end\n"
        "            local x, y, z = slayer3d.grid_cell_to_world(map, col, row)\n"
        "            if x == nil then return nil end\n"
        "            return Vec3(x, y, z)\n"
        "        end,\n"
        "        grid_world_to_cell = function(self_or_map, maybe_map, maybe_position)\n"
        "            local map, position\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                map, position = maybe_map, maybe_position\n"
        "            else\n"
        "                map, position = self_or_map, maybe_map\n"
        "            end\n"
        "            position = as_vec3(position)\n"
        "            local col, row = slayer3d.grid_world_to_cell(map, position.x, position.y)\n"
        "            if col == nil then return nil end\n"
        "            return { col = col, row = row }\n"
        "        end,\n"
        "        grid_tile = function(self_or_map, maybe_map, maybe_col, maybe_row)\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                return slayer3d.grid_tile(maybe_map, maybe_col, maybe_row)\n"
        "            end\n"
        "            return slayer3d.grid_tile(self_or_map, maybe_map, maybe_col)\n"
        "        end,\n"
        "        grid_walkable = function(self_or_map, maybe_map, maybe_col, maybe_row)\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                return slayer3d.grid_walkable(maybe_map, maybe_col, maybe_row)\n"
        "            end\n"
        "            return slayer3d.grid_walkable(self_or_map, maybe_map, maybe_col)\n"
        "        end,\n"
        "        grid_neighbors = function(self_or_map, maybe_map, maybe_col, maybe_row)\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                return slayer3d.grid_neighbors(maybe_map, maybe_col, maybe_row)\n"
        "            end\n"
        "            return slayer3d.grid_neighbors(self_or_map, maybe_map, maybe_col)\n"
        "        end,\n"
        "        grid_next_step = function(self_or_map, maybe_map, maybe_start_col, maybe_start_row, maybe_goal_col, "
        "maybe_goal_row)\n"
        "            local map, start_col, start_row, goal_col, goal_row\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                map, start_col, start_row, goal_col, goal_row = maybe_map, maybe_start_col, maybe_start_row, "
        "maybe_goal_col, maybe_goal_row\n"
        "            else\n"
        "                map, start_col, start_row, goal_col, goal_row = self_or_map, maybe_map, maybe_start_col, "
        "maybe_start_row, maybe_goal_col\n"
        "            end\n"
        "            local col, row = slayer3d.grid_next_step(map, start_col, start_row, goal_col, goal_row)\n"
        "            if col == nil then return nil end\n"
        "            return { col = col, row = row }\n"
        "        end,\n",
        "        grid_actor_at = function(self_or_map, maybe_map, maybe_pool, maybe_col, maybe_row)\n"
        "            local map, pool, col, row\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                map, pool, col, row = maybe_map, maybe_pool, maybe_col, maybe_row\n"
        "            else\n"
        "                map, pool, col, row = self_or_map, maybe_map, maybe_pool, maybe_col\n"
        "            end\n"
        "            return slayer3d.grid_actor_at(map, pool, col, row)\n"
        "        end,\n",
        "        grid_pickup_at = function(self_or_layer, maybe_layer, maybe_col, maybe_row)\n"
        "            if type(self_or_layer) == 'table' and self_or_layer.adapter ~= nil then\n"
        "                return slayer3d.grid_pickup_at(maybe_layer, maybe_col, maybe_row)\n"
        "            end\n"
        "            return slayer3d.grid_pickup_at(self_or_layer, maybe_layer, maybe_col)\n"
        "        end,\n"
        "        grid_collect_at = function(self_or_layer, maybe_layer, maybe_col, maybe_row)\n"
        "            if type(self_or_layer) == 'table' and self_or_layer.adapter ~= nil then\n"
        "                return slayer3d.grid_collect_at(maybe_layer, maybe_col, maybe_row)\n"
        "            end\n"
        "            return slayer3d.grid_collect_at(self_or_layer, maybe_layer, maybe_col)\n"
        "        end,\n"
        "        grid_pickup_count = function(self_or_layer, maybe_layer)\n"
        "            return slayer3d.grid_pickup_count(maybe_layer or self_or_layer)\n"
        "        end,\n",
        "        sector_nav_nearest = function(self_or_graph, maybe_graph, maybe_position)\n"
        "            local graph, position\n"
        "            if type(self_or_graph) == 'table' and self_or_graph.adapter ~= nil then\n"
        "                graph, position = maybe_graph, maybe_position\n"
        "            else\n"
        "                graph, position = self_or_graph, maybe_graph\n"
        "            end\n"
        "            if type(position) == 'table' and position.position ~= nil then position = position.position end\n"
        "            position = as_vec3(position)\n"
        "            return slayer3d.sector_nav_nearest(graph, position.x, position.y, position.z)\n"
        "        end,\n"
        "        sector_nav_path_available = function(self_or_graph, maybe_graph, maybe_start, maybe_goal)\n"
        "            local graph, start, goal\n"
        "            if type(self_or_graph) == 'table' and self_or_graph.adapter ~= nil then\n"
        "                graph, start, goal = maybe_graph, maybe_start, maybe_goal\n"
        "            else\n"
        "                graph, start, goal = self_or_graph, maybe_graph, maybe_start\n"
        "            end\n"
        "            if type(start) == 'table' and start.position ~= nil then start = start.position end\n"
        "            if type(goal) == 'table' and goal.position ~= nil then goal = goal.position end\n"
        "            start, goal = as_vec3(start), as_vec3(goal)\n"
        "            return slayer3d.sector_nav_path_available(graph, start.x, start.y, start.z, goal.x, goal.y, "
        "goal.z)\n"
        "        end,\n"
        "        sector_nav_next_node = function(self_or_graph, maybe_graph, maybe_start, maybe_goal)\n"
        "            local graph, start, goal\n"
        "            if type(self_or_graph) == 'table' and self_or_graph.adapter ~= nil then\n"
        "                graph, start, goal = maybe_graph, maybe_start, maybe_goal\n"
        "            else\n"
        "                graph, start, goal = self_or_graph, maybe_graph, maybe_start\n"
        "            end\n"
        "            if type(start) == 'table' and start.position ~= nil then start = start.position end\n"
        "            if type(goal) == 'table' and goal.position ~= nil then goal = goal.position end\n"
        "            start, goal = as_vec3(start), as_vec3(goal)\n"
        "            return slayer3d.sector_nav_next_node(graph, start.x, start.y, start.z, goal.x, goal.y, goal.z)\n"
        "        end,\n"
        "        sector_nav_path = function(self_or_graph, maybe_graph, maybe_start, maybe_goal)\n"
        "            local graph, start, goal\n"
        "            if type(self_or_graph) == 'table' and self_or_graph.adapter ~= nil then\n"
        "                graph, start, goal = maybe_graph, maybe_start, maybe_goal\n"
        "            else\n"
        "                graph, start, goal = self_or_graph, maybe_graph, maybe_start\n"
        "            end\n"
        "            if type(start) == 'table' and start.position ~= nil then start = start.position end\n"
        "            if type(goal) == 'table' and goal.position ~= nil then goal = goal.position end\n"
        "            start, goal = as_vec3(start), as_vec3(goal)\n"
        "            return slayer3d.sector_nav_path(graph, start.x, start.y, start.z, goal.x, goal.y, goal.z)\n"
        "        end,\n",
        "        state_get = function(self_or_key, maybe_key, fallback)\n"
        "            if type(self_or_key) == 'table' and self_or_key.adapter ~= nil then\n"
        "                return slayer3d.state_get(maybe_key, fallback)\n"
        "            end\n"
        "            return slayer3d.state_get(self_or_key, maybe_key)\n"
        "        end,\n"
        "        state_set = function(self_or_key, maybe_key, maybe_value)\n"
        "            if type(self_or_key) == 'table' and self_or_key.adapter ~= nil then\n"
        "                slayer3d.state_set(maybe_key, maybe_value); return\n"
        "            end\n"
        "            slayer3d.state_set(self_or_key, maybe_key)\n"
        "        end,\n"
        "        random = function(_) return slayer3d.random() end,\n"
        "        log = function(self_or_message, maybe_message) slayer3d.log(maybe_message or self_or_message) end,\n"
        "        storage = slayer3d.storage,\n"
        "    }\n"
        "end\n"
        "slayer3d.storage = {\n"
        "    read = slayer3d.storage_read,\n"
        "    write = slayer3d.storage_write,\n"
        "    exists = slayer3d.storage_exists,\n"
        "    mkdir = slayer3d.storage_mkdir,\n"
        "    delete = slayer3d.storage_delete,\n"
        "}\n"
        "slayer3d.json = {\n"
        "    decode = slayer3d.json_decode,\n"
        "    encode = slayer3d.json_encode,\n"
        "}\n"
        "slayer3d.Actor = Actor\n"
        "slayer3d.Vec3 = Vec3\n"
        "slayer3d.api = 'slayer3d.lua.v1'\n"
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

static void register_lua_api(slayer3d_game_data_runtime *runtime, slayer3d_script_engine *engine)
{
    if (runtime == NULL || engine == NULL)
        return;

    lua_State *lua = slayer3d_script_engine_lua_state(engine);
    if (lua == NULL)
        return;

    lua_newtable(lua);
#define SLAYER3D_LUA_BIND(name, fn)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        lua_pushlightuserdata(lua, runtime);                                                                           \
        lua_pushcclosure(lua, (fn), 1);                                                                                \
        lua_setfield(lua, -2, (name));                                                                                 \
    } while (0)
    SLAYER3D_LUA_BIND("get_position", lua_get_position);
    SLAYER3D_LUA_BIND("set_position", lua_set_position);
    SLAYER3D_LUA_BIND("get_float", lua_get_float);
    SLAYER3D_LUA_BIND("set_float", lua_set_float);
    SLAYER3D_LUA_BIND("get_int", lua_get_int);
    SLAYER3D_LUA_BIND("set_int", lua_set_int);
    SLAYER3D_LUA_BIND("get_bool", lua_get_bool);
    SLAYER3D_LUA_BIND("set_bool", lua_set_bool);
    SLAYER3D_LUA_BIND("get_string", lua_get_string);
    SLAYER3D_LUA_BIND("set_string", lua_set_string);
    SLAYER3D_LUA_BIND("get_vec3", lua_get_vec3);
    SLAYER3D_LUA_BIND("set_vec3", lua_set_vec3);
    SLAYER3D_LUA_BIND("actor_active", lua_actor_active);
    SLAYER3D_LUA_BIND("dt", lua_get_dt);
    SLAYER3D_LUA_BIND("state_get", lua_state_get);
    SLAYER3D_LUA_BIND("state_set", lua_state_set);
    SLAYER3D_LUA_BIND("random", lua_random);
    SLAYER3D_LUA_BIND("actor_with_tags", lua_actor_with_tags);
    SLAYER3D_LUA_BIND("active_actors_with_tags", lua_active_actors_with_tags);
    SLAYER3D_LUA_BIND("spawn", lua_spawn_actor);
    SLAYER3D_LUA_BIND("despawn", lua_despawn_actor);
    SLAYER3D_LUA_BIND("despawn_by_tag", lua_despawn_actors_by_tag);
    SLAYER3D_LUA_BIND("pool_capacity", lua_pool_capacity);
    SLAYER3D_LUA_BIND("pool_active_count", lua_pool_active_count);
    SLAYER3D_LUA_BIND("pool_available_count", lua_pool_available_count);
    SLAYER3D_LUA_BIND("pool_peak_active_count", lua_pool_peak_active_count);
    SLAYER3D_LUA_BIND("pool_spawn_attempt_count", lua_pool_spawn_attempt_count);
    SLAYER3D_LUA_BIND("pool_spawn_success_count", lua_pool_spawn_success_count);
    SLAYER3D_LUA_BIND("pool_spawn_failure_count", lua_pool_spawn_failure_count);
    SLAYER3D_LUA_BIND("pool_exhaustion_count", lua_pool_exhaustion_count);
    SLAYER3D_LUA_BIND("pool_reuse_count", lua_pool_reuse_count);
    SLAYER3D_LUA_BIND("pool_despawn_count", lua_pool_despawn_count);
    SLAYER3D_LUA_BIND("pool_last_spawn_failure_reason", lua_pool_last_spawn_failure_reason);
    SLAYER3D_LUA_BIND("pool_last_despawn_reason", lua_pool_last_despawn_reason);
    SLAYER3D_LUA_BIND("grid_cell_to_world", lua_grid_cell_to_world);
    SLAYER3D_LUA_BIND("grid_world_to_cell", lua_grid_world_to_cell);
    SLAYER3D_LUA_BIND("grid_tile", lua_grid_tile);
    SLAYER3D_LUA_BIND("grid_walkable", lua_grid_walkable);
    SLAYER3D_LUA_BIND("grid_neighbors", lua_grid_neighbors);
    SLAYER3D_LUA_BIND("grid_next_step", lua_grid_next_step);
    SLAYER3D_LUA_BIND("grid_actor_at", lua_grid_actor_at);
    SLAYER3D_LUA_BIND("grid_pickup_at", lua_grid_pickup_at);
    SLAYER3D_LUA_BIND("grid_collect_at", lua_grid_collect_at);
    SLAYER3D_LUA_BIND("grid_pickup_count", lua_grid_pickup_count);
    SLAYER3D_LUA_BIND("sector_nav_nearest", lua_sector_nav_nearest);
    SLAYER3D_LUA_BIND("sector_nav_path_available", lua_sector_nav_path_available);
    SLAYER3D_LUA_BIND("sector_nav_next_node", lua_sector_nav_next_node);
    SLAYER3D_LUA_BIND("sector_nav_path", lua_sector_nav_path);
    SLAYER3D_LUA_BIND("log", lua_log);
    SLAYER3D_LUA_BIND("storage_read", lua_storage_read);
    SLAYER3D_LUA_BIND("storage_write", lua_storage_write);
    SLAYER3D_LUA_BIND("storage_exists", lua_storage_exists);
    SLAYER3D_LUA_BIND("storage_mkdir", lua_storage_mkdir);
    SLAYER3D_LUA_BIND("storage_delete", lua_storage_delete);
    SLAYER3D_LUA_BIND("json_decode", lua_json_decode);
    SLAYER3D_LUA_BIND("json_encode", lua_json_encode);
#undef SLAYER3D_LUA_BIND
    lua_setglobal(lua, "slayer3d");
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

static void load_storage_config(slayer3d_game_data_runtime *runtime, yyjson_val *root);

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

static slayer3d_vec3 json_vec3_value(yyjson_val *value, slayer3d_vec3 fallback)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 2)
        return fallback;

    yyjson_val *x = yyjson_arr_get(value, 0);
    yyjson_val *y = yyjson_arr_get(value, 1);
    yyjson_val *z = yyjson_arr_get(value, 2);
    if (!yyjson_is_num(x) || !yyjson_is_num(y))
        return fallback;

    return slayer3d_vec3_make((float)yyjson_get_num(x), (float)yyjson_get_num(y),
                              yyjson_is_num(z) ? (float)yyjson_get_num(z) : fallback.z);
}

static slayer3d_vec3 json_vec3(yyjson_val *object, const char *key, slayer3d_vec3 fallback)
{
    return json_vec3_value(obj_get(object, key), fallback);
}

static slayer3d_vec4 json_vec4_value(yyjson_val *value, slayer3d_vec4 fallback)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 3)
        return fallback;

    yyjson_val *x = yyjson_arr_get(value, 0);
    yyjson_val *y = yyjson_arr_get(value, 1);
    yyjson_val *z = yyjson_arr_get(value, 2);
    yyjson_val *w = yyjson_arr_get(value, 3);
    if (!yyjson_is_num(x) || !yyjson_is_num(y) || !yyjson_is_num(z))
        return fallback;

    return slayer3d_vec4_make((float)yyjson_get_num(x), (float)yyjson_get_num(y), (float)yyjson_get_num(z),
                              yyjson_is_num(w) ? (float)yyjson_get_num(w) : fallback.w);
}

static slayer3d_vec4 json_vec4(yyjson_val *object, const char *key, slayer3d_vec4 fallback)
{
    return json_vec4_value(obj_get(object, key), fallback);
}

static bool json_vec2_value(yyjson_val *value, float fallback_x, float fallback_y, float *out_x, float *out_y)
{
    if (out_x == NULL || out_y == NULL)
        return false;
    *out_x = fallback_x;
    *out_y = fallback_y;
    if (value == NULL)
        return true;
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 2)
        return false;
    yyjson_val *x = yyjson_arr_get(value, 0);
    yyjson_val *y = yyjson_arr_get(value, 1);
    if (!yyjson_is_num(x) || !yyjson_is_num(y))
        return false;
    *out_x = (float)yyjson_get_num(x);
    *out_y = (float)yyjson_get_num(y);
    return true;
}

static bool grid_map_normalize_cell(const grid_map_runtime *map, int *col, int *row)
{
    if (map == NULL || col == NULL || row == NULL || map->width <= 0 || map->height <= 0)
        return false;
    if (*col < 0 || *col >= map->width)
    {
        if (!map->wrap_x)
            return false;
        *col = (*col % map->width + map->width) % map->width;
    }
    if (*row < 0 || *row >= map->height)
    {
        if (!map->wrap_y)
            return false;
        *row = (*row % map->height + map->height) % map->height;
    }
    return true;
}

static char grid_map_cell(const grid_map_runtime *map, int col, int row)
{
    if (!grid_map_normalize_cell(map, &col, &row))
        return '\0';
    return map->cells[row * map->width + col];
}

static bool grid_map_is_walkable(const grid_map_runtime *map, int col, int row)
{
    const char cell = grid_map_cell(map, col, row);
    if (cell == '\0' || map == NULL || map->walkable == NULL)
        return false;
    for (const char *cursor = map->walkable; *cursor != '\0'; ++cursor)
    {
        if (*cursor == cell)
            return true;
    }
    return false;
}

static bool grid_map_cell_to_world(const grid_map_runtime *map, int col, int row, slayer3d_vec3 *out_position)
{
    if (map == NULL || out_position == NULL || !grid_map_normalize_cell(map, &col, &row))
        return false;
    *out_position =
        slayer3d_vec3_make(map->origin.x + (float)col * map->cell_width,
                           map->origin.y + (float)row * map->cell_height * map->row_direction, map->origin.z);
    return true;
}

static int grid_round_to_int(float value)
{
    return value >= 0.0f ? (int)(value + 0.5f) : (int)(value - 0.5f);
}

static bool grid_map_world_to_cell(const grid_map_runtime *map, float x, float y, int *out_col, int *out_row)
{
    if (map == NULL || out_col == NULL || out_row == NULL || map->cell_width <= 0.0f || map->cell_height <= 0.0f ||
        map->row_direction == 0.0f)
    {
        return false;
    }
    int col = grid_round_to_int((x - map->origin.x) / map->cell_width);
    int row = grid_round_to_int((y - map->origin.y) / (map->cell_height * map->row_direction));
    if (!grid_map_normalize_cell(map, &col, &row))
        return false;
    *out_col = col;
    *out_row = row;
    return true;
}

static const grid_map_runtime *find_grid_map(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->grid_map_count; ++i)
    {
        if (runtime->grid_maps[i].name != NULL && SDL_strcmp(runtime->grid_maps[i].name, name) == 0)
            return &runtime->grid_maps[i];
    }
    return NULL;
}

static grid_actor_index *find_grid_actor_index(slayer3d_game_data_runtime *runtime, const char *map_name,
                                               const char *pool_name)
{
    if (runtime == NULL || map_name == NULL || pool_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->grid_actor_index_count; ++i)
    {
        grid_actor_index *index = &runtime->grid_actor_indices[i];
        if (index->map != NULL && index->pool != NULL && SDL_strcmp(index->map, map_name) == 0 &&
            SDL_strcmp(index->pool, pool_name) == 0)
        {
            return index;
        }
    }
    return NULL;
}

static grid_actor_index *get_or_create_grid_actor_index(slayer3d_game_data_runtime *runtime,
                                                        const grid_map_runtime *map, const char *pool_name)
{
    grid_actor_index *existing = find_grid_actor_index(runtime, map != NULL ? map->name : NULL, pool_name);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || map == NULL || map->name == NULL || pool_name == NULL || pool_name[0] == '\0' ||
        map->width <= 0 || map->height <= 0)
    {
        return NULL;
    }

    if (runtime->grid_actor_index_count >= runtime->grid_actor_index_capacity)
    {
        const int next_capacity = runtime->grid_actor_index_capacity > 0 ? runtime->grid_actor_index_capacity * 2 : 4;
        grid_actor_index *next = (grid_actor_index *)SDL_realloc(
            runtime->grid_actor_indices, (size_t)next_capacity * sizeof(*runtime->grid_actor_indices));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->grid_actor_index_capacity, 0,
                   (size_t)(next_capacity - runtime->grid_actor_index_capacity) * sizeof(*runtime->grid_actor_indices));
        runtime->grid_actor_indices = next;
        runtime->grid_actor_index_capacity = next_capacity;
    }

    grid_actor_index *index = &runtime->grid_actor_indices[runtime->grid_actor_index_count];
    SDL_zero(*index);
    index->map = SDL_strdup(map->name);
    index->pool = SDL_strdup(pool_name);
    index->width = map->width;
    index->height = map->height;
    index->actors =
        (slayer3d_registered_actor **)SDL_calloc((size_t)map->width * (size_t)map->height, sizeof(*index->actors));
    if (index->map == NULL || index->pool == NULL || index->actors == NULL)
    {
        SDL_free(index->map);
        SDL_free(index->pool);
        SDL_free(index->actors);
        SDL_zero(*index);
        return NULL;
    }
    ++runtime->grid_actor_index_count;
    return index;
}

static void grid_actor_index_clear(slayer3d_game_data_runtime *runtime, const grid_map_runtime *map,
                                   const char *pool_name)
{
    grid_actor_index *index = find_grid_actor_index(runtime, map != NULL ? map->name : NULL, pool_name);
    if (index == NULL || index->actors == NULL || index->width <= 0 || index->height <= 0)
        return;
    SDL_memset(index->actors, 0, (size_t)index->width * (size_t)index->height * sizeof(*index->actors));
}

static bool grid_actor_index_register(slayer3d_game_data_runtime *runtime, const grid_map_runtime *map,
                                      const char *pool_name, slayer3d_registered_actor *actor, int col, int row)
{
    if (actor == NULL || !grid_map_normalize_cell(map, &col, &row))
        return false;
    grid_actor_index *index = get_or_create_grid_actor_index(runtime, map, pool_name);
    if (index == NULL || index->actors == NULL || index->width != map->width || index->height != map->height)
        return false;
    index->actors[row * index->width + col] = actor;
    return true;
}

static slayer3d_registered_actor *grid_actor_index_find(slayer3d_game_data_runtime *runtime, const char *map_name,
                                                        const char *pool_name, int col, int row)
{
    const grid_map_runtime *map = find_grid_map(runtime, map_name);
    if (!grid_map_normalize_cell(map, &col, &row))
        return NULL;
    grid_actor_index *index = find_grid_actor_index(runtime, map_name, pool_name);
    if (index == NULL || index->actors == NULL || index->width != map->width || index->height != map->height)
        return NULL;

    slayer3d_registered_actor *actor = index->actors[row * index->width + col];
    if (!runtime_actor_is_active(runtime, actor))
        return NULL;
    const char *actor_map = slayer3d_properties_get_string(actor->props, "grid_map", NULL);
    const int actor_col = slayer3d_properties_get_int(actor->props, "grid_col", -1);
    const int actor_row = slayer3d_properties_get_int(actor->props, "grid_row", -1);
    const int start_col = slayer3d_properties_get_int(actor->props, "grid_run_start_col", actor_col);
    const int start_row = slayer3d_properties_get_int(actor->props, "grid_run_start_row", actor_row);
    const int end_col = slayer3d_properties_get_int(actor->props, "grid_run_end_col", actor_col);
    const int end_row = slayer3d_properties_get_int(actor->props, "grid_run_end_row", actor_row);
    if (actor_map == NULL || SDL_strcmp(actor_map, map_name) != 0 || col < SDL_min(start_col, end_col) ||
        col > SDL_max(start_col, end_col) || row < SDL_min(start_row, end_row) || row > SDL_max(start_row, end_row))
    {
        return NULL;
    }
    return actor;
}

static grid_pickup_layer_runtime *find_grid_pickup_layer(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->grid_pickup_layer_count; ++i)
    {
        if (runtime->grid_pickup_layers[i].name != NULL && SDL_strcmp(runtime->grid_pickup_layers[i].name, name) == 0)
            return &runtime->grid_pickup_layers[i];
    }
    return NULL;
}

static bool grid_pickup_layer_reset(slayer3d_game_data_runtime *runtime, grid_pickup_layer_runtime *layer)
{
    (void)runtime;
    if (layer == NULL || layer->map == NULL || layer->cells == NULL)
        return false;
    const int count = layer->map->width * layer->map->height;
    SDL_memset(layer->cells, 0, (size_t)count * sizeof(*layer->cells));
    layer->active_count = 0;
    for (int row = 0; row < layer->map->height; ++row)
    {
        for (int col = 0; col < layer->map->width; ++col)
        {
            const char glyph = grid_map_cell(layer->map, col, row);
            for (int kind_index = 0; kind_index < layer->kind_count; ++kind_index)
            {
                if (layer->kinds[kind_index].glyph != glyph)
                    continue;
                layer->cells[row * layer->map->width + col] = (Uint8)(kind_index + 1);
                layer->active_count++;
                break;
            }
        }
    }
    return true;
}

static bool grid_pickup_layer_collect_at(slayer3d_game_data_runtime *runtime, grid_pickup_layer_runtime *layer, int col,
                                         int row, grid_pickup_kind_runtime *out_kind)
{
    (void)runtime;
    if (out_kind != NULL)
        SDL_zero(*out_kind);
    if (layer == NULL || layer->map == NULL || layer->cells == NULL || !grid_map_normalize_cell(layer->map, &col, &row))
        return false;
    Uint8 *cell = &layer->cells[row * layer->map->width + col];
    if (*cell == 0 || *cell > (Uint8)layer->kind_count)
        return false;
    if (out_kind != NULL)
        *out_kind = layer->kinds[*cell - 1];
    *cell = 0;
    if (layer->active_count > 0)
        layer->active_count--;
    return true;
}

static bool grid_map_next_step(const grid_map_runtime *map, int start_col, int start_row, int goal_col, int goal_row,
                               int *out_col, int *out_row)
{
    if (out_col != NULL)
        *out_col = start_col;
    if (out_row != NULL)
        *out_row = start_row;
    if (map == NULL || out_col == NULL || out_row == NULL || !grid_map_normalize_cell(map, &start_col, &start_row) ||
        !grid_map_normalize_cell(map, &goal_col, &goal_row) || !grid_map_is_walkable(map, start_col, start_row) ||
        !grid_map_is_walkable(map, goal_col, goal_row))
    {
        return false;
    }
    if (start_col == goal_col && start_row == goal_row)
        return true;

    const int count = map->width * map->height;
    bool *visited = (bool *)SDL_calloc((size_t)count, sizeof(*visited));
    int *previous = (int *)SDL_malloc((size_t)count * sizeof(*previous));
    int *queue = (int *)SDL_malloc((size_t)count * sizeof(*queue));
    if (visited == NULL || previous == NULL || queue == NULL)
    {
        SDL_free(visited);
        SDL_free(previous);
        SDL_free(queue);
        return false;
    }
    for (int i = 0; i < count; ++i)
        previous[i] = -1;

    const int start = start_row * map->width + start_col;
    const int goal = goal_row * map->width + goal_col;
    int read_index = 0;
    int write_index = 0;
    queue[write_index++] = start;
    visited[start] = true;

    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, -1}, {0, 1}};
    while (read_index < write_index && !visited[goal])
    {
        const int current = queue[read_index++];
        const int col = current % map->width;
        const int row = current / map->width;
        for (size_t d = 0; d < SDL_arraysize(dirs); ++d)
        {
            int next_col = col + dirs[d][0];
            int next_row = row + dirs[d][1];
            if (!grid_map_normalize_cell(map, &next_col, &next_row) || !grid_map_is_walkable(map, next_col, next_row))
            {
                continue;
            }
            const int next = next_row * map->width + next_col;
            if (visited[next])
                continue;
            visited[next] = true;
            previous[next] = current;
            queue[write_index++] = next;
            if (next == goal)
                break;
        }
    }

    bool found = visited[goal];
    if (found)
    {
        int step = goal;
        while (previous[step] >= 0 && previous[step] != start)
            step = previous[step];
        *out_col = step % map->width;
        *out_row = step / map->width;
    }

    SDL_free(visited);
    SDL_free(previous);
    SDL_free(queue);
    return found;
}

#include "game_data/game_data_world_loaders.inc"

static yyjson_val *runtime_root(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->doc != NULL ? yyjson_doc_get_root(runtime->doc) : NULL;
}

static scene_entry *active_scene_entry(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->active_scene_index < 0 || runtime->active_scene_index >= runtime->scene_count)
        return NULL;
    return &runtime->scenes[runtime->active_scene_index];
}

static const scene_entry *active_scene_entry_const(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->active_scene_index < 0 || runtime->active_scene_index >= runtime->scene_count)
        return NULL;
    return &runtime->scenes[runtime->active_scene_index];
}

static scene_entry *find_scene(slayer3d_game_data_runtime *runtime, const char *name)
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

static const scene_entry *find_scene_const(const slayer3d_game_data_runtime *runtime, const char *name)
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

static sector_door_runtime *find_sector_door(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->sector_door_count; ++i)
    {
        sector_door_runtime *door = &runtime->sector_doors[i];
        if (door->door.name != NULL && SDL_strcmp(door->door.name, name) == 0)
            return door;
    }
    return NULL;
}

static bool sector_door_in_scene(const sector_door_runtime *door, const char *scene_name)
{
    if (door == NULL)
        return false;
    const char *scene = json_string(door->json, "scene", NULL);
    if (scene == NULL || scene[0] == '\0')
        return true;
    return scene_name != NULL && SDL_strcmp(scene, scene_name) == 0;
}

static bool sector_door_in_active_scene(const slayer3d_game_data_runtime *runtime, const sector_door_runtime *door)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return sector_door_in_scene(door, scene != NULL ? scene->name : NULL);
}

static bool sector_platform_in_scene(const sector_platform_runtime *platform, const char *scene_name)
{
    if (platform == NULL)
        return false;
    const char *scene = json_string(platform->json, "scene", NULL);
    if (scene == NULL || scene[0] == '\0')
        return true;
    return scene_name != NULL && SDL_strcmp(scene, scene_name) == 0;
}

static bool sector_platform_in_active_scene(const slayer3d_game_data_runtime *runtime,
                                            const sector_platform_runtime *platform)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return sector_platform_in_scene(platform, scene != NULL ? scene->name : NULL);
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

static bool active_scene_has_entity_internal(const slayer3d_game_data_runtime *runtime, const char *entity_name)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    if (scene_has_entity(scene, entity_name))
        return true;
    if (runtime == NULL || scene == NULL || entity_name == NULL)
        return false;
    return actor_pool_in_scene(find_actor_pool_for_actor_const(runtime, entity_name, NULL), scene->name);
}

static void apply_scene_camera(slayer3d_game_data_runtime *runtime, const scene_entry *scene)
{
    const char *camera = scene != NULL ? json_string(scene->root, "camera", NULL) : NULL;
    if (runtime != NULL && camera != NULL)
        runtime->active_camera = camera;
}

static slayer3d_properties *create_scene_enter_payload(const char *from_scene, const char *to_scene,
                                                       const slayer3d_properties *payload)
{
    slayer3d_properties *enter_payload = slayer3d_properties_create();
    if (enter_payload == NULL)
        return NULL;

    const int count = slayer3d_properties_count(payload);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (slayer3d_properties_get_key_at(payload, i, &key, NULL))
            copy_property_value(enter_payload, key, slayer3d_properties_get_value(payload, key));
    }

    slayer3d_properties_set_string(enter_payload, "from_scene", from_scene != NULL ? from_scene : "");
    slayer3d_properties_set_string(enter_payload, "to_scene", to_scene != NULL ? to_scene : "");
    return enter_payload;
}

static void emit_scene_enter_signal(slayer3d_game_data_runtime *runtime, const scene_entry *scene,
                                    const char *from_scene, const slayer3d_properties *payload)
{
    if (runtime == NULL || scene == NULL)
        return;

    const int signal_id = slayer3d_game_data_find_signal(runtime, json_string(scene->root, "on_enter_signal", NULL));
    if (signal_id >= 0 && runtime_bus(runtime) != NULL)
    {
        slayer3d_properties *enter_payload = create_scene_enter_payload(from_scene, scene->name, payload);
        slayer3d_signal_emit(runtime_bus(runtime), signal_id, enter_payload);
        slayer3d_properties_destroy(enter_payload);
    }
}

static yyjson_val *find_entity_json(const slayer3d_game_data_runtime *runtime, const char *name)
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

static yyjson_val *find_actor_definition_json(const slayer3d_game_data_runtime *runtime, const char *actor_name)
{
    yyjson_val *entity = find_entity_json(runtime, actor_name);
    if (entity != NULL)
        return entity;

    const actor_pool_runtime *pool = find_actor_pool_for_actor_const(runtime, actor_name, NULL);
    return pool != NULL ? pool->archetype_json : NULL;
}

static yyjson_val *find_font_json(const slayer3d_game_data_runtime *runtime, const char *id)
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

static yyjson_val *find_image_json(const slayer3d_game_data_runtime *runtime, const char *id)
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

static yyjson_val *find_model_json(const slayer3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *models = obj_get(obj_get(runtime_root(runtime), "assets"), "models");
    for (size_t i = 0; id != NULL && yyjson_is_arr(models) && i < yyjson_arr_size(models); ++i)
    {
        yyjson_val *model = yyjson_arr_get(models, i);
        const char *model_id = json_string(model, "id", NULL);
        if (model_id != NULL && SDL_strcmp(model_id, id) == 0)
            return model;
    }
    return NULL;
}

static yyjson_val *find_sound_json(const slayer3d_game_data_runtime *runtime, const char *id)
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

static yyjson_val *find_music_json(const slayer3d_game_data_runtime *runtime, const char *id)
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

static yyjson_val *find_ambient_json(const slayer3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *ambient_assets = obj_get(obj_get(runtime_root(runtime), "assets"), "ambient");
    for (size_t i = 0; id != NULL && yyjson_is_arr(ambient_assets) && i < yyjson_arr_size(ambient_assets); ++i)
    {
        yyjson_val *ambient = yyjson_arr_get(ambient_assets, i);
        const char *ambient_id = json_string(ambient, "id", NULL);
        if (ambient_id != NULL && SDL_strcmp(ambient_id, id) == 0)
            return ambient;
    }
    return NULL;
}

static yyjson_val *find_sprite_json(const slayer3d_game_data_runtime *runtime, const char *id)
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

static slayer3d_audio_bus parse_audio_bus(const char *bus, slayer3d_audio_bus fallback)
{
    if (bus == NULL)
        return fallback;
    if (SDL_strcmp(bus, "music") == 0)
        return SLAYER3D_AUDIO_BUS_MUSIC;
    if (SDL_strcmp(bus, "sound_effects") == 0 || SDL_strcmp(bus, "sfx") == 0)
        return SLAYER3D_AUDIO_BUS_SOUND_EFFECTS;
    if (SDL_strcmp(bus, "dialogue") == 0 || SDL_strcmp(bus, "dialog") == 0)
        return SLAYER3D_AUDIO_BUS_DIALOGUE;
    if (SDL_strcmp(bus, "ambience") == 0 || SDL_strcmp(bus, "ambiance") == 0 || SDL_strcmp(bus, "ambient") == 0)
        return SLAYER3D_AUDIO_BUS_AMBIENCE;
    return fallback;
}

static yyjson_val *find_camera_json(const slayer3d_game_data_runtime *runtime, const char *name)
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

static fps_controller_runtime *find_fps_controller(slayer3d_game_data_runtime *runtime, const char *entity_name)
{
    if (runtime == NULL || entity_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->fps_controller_count; ++i)
    {
        fps_controller_runtime *controller = &runtime->fps_controllers[i];
        if (controller->entity_name != NULL && SDL_strcmp(controller->entity_name, entity_name) == 0)
            return controller;
    }
    return NULL;
}

static const fps_controller_runtime *find_fps_controller_const(const slayer3d_game_data_runtime *runtime,
                                                               const char *entity_name)
{
    if (runtime == NULL || entity_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->fps_controller_count; ++i)
    {
        const fps_controller_runtime *controller = &runtime->fps_controllers[i];
        if (controller->entity_name != NULL && SDL_strcmp(controller->entity_name, entity_name) == 0)
            return controller;
    }
    return NULL;
}

static fps_controller_runtime *find_or_add_fps_controller(slayer3d_game_data_runtime *runtime, const char *entity_name,
                                                          yyjson_val *component)
{
    fps_controller_runtime *controller = find_fps_controller(runtime, entity_name);
    if (controller != NULL)
    {
        if (component != NULL && controller->component != component)
        {
            controller->component = component;
            controller->initialized = false;
        }
        return controller;
    }
    if (runtime == NULL || entity_name == NULL || component == NULL)
        return NULL;
    if (runtime->fps_controller_count >= runtime->fps_controller_capacity)
    {
        const int next_capacity = runtime->fps_controller_capacity > 0 ? runtime->fps_controller_capacity * 2 : 4;
        fps_controller_runtime *controllers = (fps_controller_runtime *)SDL_realloc(
            runtime->fps_controllers, (size_t)next_capacity * sizeof(*controllers));
        if (controllers == NULL)
            return NULL;
        runtime->fps_controllers = controllers;
        runtime->fps_controller_capacity = next_capacity;
    }

    controller = &runtime->fps_controllers[runtime->fps_controller_count++];
    SDL_zero(*controller);
    controller->entity_name = entity_name;
    controller->component = component;
    return controller;
}

static patrol_controller_runtime *find_patrol_controller(slayer3d_game_data_runtime *runtime, const char *entity_name)
{
    if (runtime == NULL || entity_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->patrol_controller_count; ++i)
    {
        patrol_controller_runtime *controller = &runtime->patrol_controllers[i];
        if (controller->entity_name != NULL && SDL_strcmp(controller->entity_name, entity_name) == 0)
            return controller;
    }
    return NULL;
}

static patrol_controller_runtime *find_or_add_patrol_controller(slayer3d_game_data_runtime *runtime,
                                                                const char *entity_name, yyjson_val *component)
{
    patrol_controller_runtime *controller = find_patrol_controller(runtime, entity_name);
    if (controller != NULL)
    {
        if (component != NULL && controller->component != component)
        {
            controller->component = component;
            controller->initialized = false;
        }
        return controller;
    }
    if (runtime == NULL || entity_name == NULL || component == NULL)
        return NULL;
    if (runtime->patrol_controller_count >= runtime->patrol_controller_capacity)
    {
        const int next_capacity = runtime->patrol_controller_capacity > 0 ? runtime->patrol_controller_capacity * 2 : 4;
        patrol_controller_runtime *controllers = (patrol_controller_runtime *)SDL_realloc(
            runtime->patrol_controllers, (size_t)next_capacity * sizeof(*controllers));
        if (controllers == NULL)
            return NULL;
        runtime->patrol_controllers = controllers;
        runtime->patrol_controller_capacity = next_capacity;
    }

    controller = &runtime->patrol_controllers[runtime->patrol_controller_count++];
    SDL_zero(*controller);
    controller->entity_name = entity_name;
    controller->component = component;
    return controller;
}

static slayer3d_backend parse_backend(const char *value, slayer3d_backend fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "auto") == 0)
        return SLAYER3D_BACKEND_AUTO;
    if (SDL_strcasecmp(value, "software") == 0)
        return SLAYER3D_BACKEND_SOFTWARE;
    if (SDL_strcasecmp(value, "opengl") == 0 || SDL_strcasecmp(value, "gl") == 0 || SDL_strcasecmp(value, "gpu") == 0)
        return SLAYER3D_BACKEND_OPENGL;
    return fallback;
}

static slayer3d_window_mode parse_window_mode(const char *value, slayer3d_window_mode fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "windowed") == 0 || SDL_strcasecmp(value, "window") == 0)
        return SLAYER3D_WINDOW_MODE_WINDOWED;
    if (SDL_strcasecmp(value, "fullscreen_exclusive") == 0 || SDL_strcasecmp(value, "exclusive") == 0)
        return SLAYER3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE;
    if (SDL_strcasecmp(value, "fullscreen_borderless") == 0 || SDL_strcasecmp(value, "borderless") == 0 ||
        SDL_strcasecmp(value, "desktop_fullscreen") == 0)
        return SLAYER3D_WINDOW_MODE_FULLSCREEN_BORDERLESS;
    return fallback;
}

static void storage_config_from_root(yyjson_val *root, slayer3d_storage_config *out_config);

static slayer3d_tonemap_mode parse_tonemap(const char *value, slayer3d_tonemap_mode fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "none") == 0)
        return SLAYER3D_TONEMAP_NONE;
    if (SDL_strcasecmp(value, "reinhard") == 0)
        return SLAYER3D_TONEMAP_REINHARD;
    if (SDL_strcasecmp(value, "aces") == 0)
        return SLAYER3D_TONEMAP_ACES;
    return fallback;
}

static bool parse_render_profile(const char *value, slayer3d_render_profile *out_profile)
{
    if (value == NULL || out_profile == NULL)
        return false;
    if (SDL_strcasecmp(value, "modern") == 0)
        *out_profile = slayer3d_profile_modern();
    else if (SDL_strcasecmp(value, "ps1") == 0)
        *out_profile = slayer3d_profile_ps1();
    else if (SDL_strcasecmp(value, "n64") == 0)
        *out_profile = slayer3d_profile_n64();
    else if (SDL_strcasecmp(value, "dos") == 0)
        *out_profile = slayer3d_profile_dos();
    else if (SDL_strcasecmp(value, "snes") == 0)
        *out_profile = slayer3d_profile_snes();
    else if (SDL_strcasecmp(value, "grayscale") == 0)
        *out_profile = slayer3d_profile_grayscale();
    else if (SDL_strcasecmp(value, "gameboy") == 0)
        *out_profile = slayer3d_profile_gameboy();
    else
        return false;
    return true;
}

static const char *scene_state_string(const slayer3d_game_data_runtime *runtime, const char *key, const char *fallback)
{
    if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
        return fallback;
    return slayer3d_properties_get_string(runtime->scene_state, key, fallback);
}

static bool scene_state_bool(const slayer3d_game_data_runtime *runtime, const char *key, bool fallback)
{
    if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
        return fallback;
    const slayer3d_value *value = slayer3d_properties_get_value(runtime->scene_state, key);
    if (value == NULL)
        return fallback;
    if (value->type == SLAYER3D_VALUE_BOOL)
        return value->as_bool;
    if (value->type == SLAYER3D_VALUE_INT)
        return value->as_int != 0;
    if (value->type == SLAYER3D_VALUE_STRING && value->as_string != NULL)
    {
        return SDL_strcasecmp(value->as_string, "true") == 0 || SDL_strcmp(value->as_string, "1") == 0 ||
               SDL_strcasecmp(value->as_string, "on") == 0 || SDL_strcasecmp(value->as_string, "yes") == 0;
    }
    return fallback;
}

static float scene_state_float(const slayer3d_game_data_runtime *runtime, const char *key, float fallback)
{
    if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
        return fallback;
    const slayer3d_value *value = slayer3d_properties_get_value(runtime->scene_state, key);
    if (value == NULL)
        return fallback;
    if (value->type == SLAYER3D_VALUE_FLOAT)
        return value->as_float;
    if (value->type == SLAYER3D_VALUE_INT)
        return (float)value->as_int;
    return fallback;
}

static bool camera_fov_degrees_valid(float value)
{
    return value > 0.0f && value < 180.0f;
}

static slayer3d_camera_fov_axis parse_camera_fov_axis(const char *value, slayer3d_camera_fov_axis fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "vertical") == 0)
        return SLAYER3D_CAMERA_FOV_VERTICAL;
    if (SDL_strcasecmp(value, "horizontal") == 0)
        return SLAYER3D_CAMERA_FOV_HORIZONTAL;
    return fallback;
}

static float camera_fov_degrees(const slayer3d_game_data_runtime *runtime, yyjson_val *camera_json, float fallback)
{
    const float authored = json_float(camera_json, "fov", json_float(camera_json, "fovy", fallback));
    const float valid_authored = camera_fov_degrees_valid(authored) ? authored : fallback;
    const float runtime_value = scene_state_float(runtime, json_string(camera_json, "fov_key", NULL), valid_authored);
    return camera_fov_degrees_valid(runtime_value) ? runtime_value : valid_authored;
}

static slayer3d_camera_fov_axis camera_fov_axis(const slayer3d_game_data_runtime *runtime, yyjson_val *camera_json)
{
    const slayer3d_camera_fov_axis authored =
        parse_camera_fov_axis(json_string(camera_json, "fov_axis", NULL), SLAYER3D_CAMERA_FOV_VERTICAL);
    return parse_camera_fov_axis(scene_state_string(runtime, json_string(camera_json, "fov_axis_key", NULL),
                                                    json_string(camera_json, "fov_axis", NULL)),
                                 authored);
}

static slayer3d_transition_type parse_transition_type(const char *value, slayer3d_transition_type fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "fade") == 0)
        return SLAYER3D_TRANSITION_FADE;
    if (SDL_strcasecmp(value, "circle") == 0)
        return SLAYER3D_TRANSITION_CIRCLE;
    if (SDL_strcasecmp(value, "melt") == 0)
        return SLAYER3D_TRANSITION_MELT;
    if (SDL_strcasecmp(value, "pixelate") == 0)
        return SLAYER3D_TRANSITION_PIXELATE;
    return fallback;
}

static slayer3d_transition_direction parse_transition_direction(const char *value,
                                                                slayer3d_transition_direction fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "in") == 0)
        return SLAYER3D_TRANSITION_IN;
    if (SDL_strcasecmp(value, "out") == 0)
        return SLAYER3D_TRANSITION_OUT;
    return fallback;
}

static slayer3d_builtin_font parse_builtin_font(const char *value, slayer3d_builtin_font fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "Inter") == 0 || SDL_strcasecmp(value, "inter") == 0)
        return SLAYER3D_BUILTIN_FONT_INTER;
    return fallback;
}

static slayer3d_game_data_ui_align parse_ui_align(const char *value, slayer3d_game_data_ui_align fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "left") == 0)
        return SLAYER3D_GAME_DATA_UI_ALIGN_LEFT;
    if (SDL_strcasecmp(value, "center") == 0 || SDL_strcasecmp(value, "middle") == 0)
        return SLAYER3D_GAME_DATA_UI_ALIGN_CENTER;
    if (SDL_strcasecmp(value, "right") == 0)
        return SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT;
    return fallback;
}

static slayer3d_game_data_ui_valign parse_ui_valign(const char *value, slayer3d_game_data_ui_valign fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "top") == 0)
        return SLAYER3D_GAME_DATA_UI_VALIGN_TOP;
    if (SDL_strcasecmp(value, "center") == 0 || SDL_strcasecmp(value, "middle") == 0)
        return SLAYER3D_GAME_DATA_UI_VALIGN_CENTER;
    if (SDL_strcasecmp(value, "bottom") == 0)
        return SLAYER3D_GAME_DATA_UI_VALIGN_BOTTOM;
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

static float vec_axis(slayer3d_vec3 value, int axis)
{
    if (axis == 0)
        return value.x;
    if (axis == 1)
        return value.y;
    if (axis == 2)
        return value.z;
    return 0.0f;
}

static void set_vec_axis(slayer3d_vec3 *value, int axis, float component)
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

static slayer3d_vec3 actor_vec_property(const slayer3d_registered_actor *actor, const char *key)
{
    return actor != NULL ? slayer3d_properties_get_vec3(actor->props, key, slayer3d_vec3_make(0.0f, 0.0f, 0.0f))
                         : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static void actor_set_position(slayer3d_registered_actor *actor, slayer3d_vec3 position)
{
    if (actor == NULL)
        return;
    actor->position = position;
    slayer3d_properties_set_vec3(actor->props, "origin", position);
}

typedef struct game_data_snapshot_value
{
    slayer3d_replication_field_type type;
    union {
        bool as_bool;
        Sint32 as_int32;
        float as_float32;
        slayer3d_vec2 as_vec2;
        slayer3d_vec3 as_vec3;
    } value;
} game_data_snapshot_value;

typedef struct game_data_input_value
{
    int action_id;
    float value;
} game_data_input_value;

static yyjson_val *game_data_find_replication_channel_by_name(const slayer3d_game_data_runtime *runtime,
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

static yyjson_val *game_data_find_replication_channel_by_index(const slayer3d_game_data_runtime *runtime, Uint32 index)
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

static size_t game_data_replication_field_array_count(yyjson_val *fields)
{
    return yyjson_is_arr(fields) ? yyjson_arr_size(fields) : 0U;
}

static size_t game_data_replication_channel_field_count(const slayer3d_game_data_runtime *runtime, yyjson_val *channel)
{
    size_t count = 0U;
    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t i = 0; yyjson_is_arr(actors) && i < yyjson_arr_size(actors); ++i)
    {
        count += game_data_replication_field_array_count(obj_get(yyjson_arr_get(actors, i), "fields"));
    }

    yyjson_val *pools = obj_get(channel, "pools");
    for (size_t i = 0; yyjson_is_arr(pools) && i < yyjson_arr_size(pools); ++i)
    {
        yyjson_val *pool_schema = yyjson_arr_get(pools, i);
        const actor_pool_runtime *pool = find_actor_pool_const(runtime, json_string(pool_schema, "pool", NULL));
        if (pool != NULL && pool->capacity > 0)
            count += (size_t)pool->capacity * game_data_replication_field_array_count(obj_get(pool_schema, "fields"));
    }
    return count;
}

static bool game_data_add_replication_fields_packet_size(yyjson_val *fields, size_t multiplier, size_t *size)
{
    if (size == NULL)
        return false;

    for (size_t repeat = 0U; repeat < multiplier; ++repeat)
    {
        for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
        {
            slayer3d_replication_field_descriptor field;
            if (!slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field))
                return false;
            const size_t field_size = slayer3d_replication_field_wire_size(field.type);
            if (field_size == 0U || *size > SIZE_MAX - 1U - field_size)
                return false;
            *size += 1U + field_size;
        }
    }
    return true;
}

static bool game_data_replication_channel_packet_size(const slayer3d_game_data_runtime *runtime, yyjson_val *channel,
                                                      size_t *out_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (channel == NULL)
        return false;

    size_t size = 4U + 4U + 4U + 4U + SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE + 4U;
    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t actor_index = 0U; yyjson_is_arr(actors) && actor_index < yyjson_arr_size(actors); ++actor_index)
    {
        if (!game_data_add_replication_fields_packet_size(obj_get(yyjson_arr_get(actors, actor_index), "fields"), 1U,
                                                          &size))
            return false;
    }

    yyjson_val *pools = obj_get(channel, "pools");
    for (size_t pool_index = 0U; yyjson_is_arr(pools) && pool_index < yyjson_arr_size(pools); ++pool_index)
    {
        yyjson_val *pool_schema = yyjson_arr_get(pools, pool_index);
        const actor_pool_runtime *pool = find_actor_pool_const(runtime, json_string(pool_schema, "pool", NULL));
        if (pool == NULL || pool->capacity < 0)
            return false;
        if (!game_data_add_replication_fields_packet_size(obj_get(pool_schema, "fields"), (size_t)pool->capacity,
                                                          &size))
            return false;
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
    const size_t input_value_size = 1U + slayer3d_replication_field_wire_size(SLAYER3D_REPLICATION_FIELD_FLOAT32);
    size_t size = 4U + 4U + 4U + 4U + SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE + 4U;
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

static int game_data_replication_action_id(const slayer3d_game_data_runtime *runtime, yyjson_val *input)
{
    return slayer3d_game_data_find_action(runtime, game_data_replication_input_action(input));
}

static slayer3d_game_data_network_direction game_data_network_direction_from_string(const char *direction)
{
    if (direction == NULL)
        return SLAYER3D_GAME_DATA_NETWORK_DIRECTION_INVALID;
    if (SDL_strcmp(direction, "host_to_client") == 0)
        return SLAYER3D_GAME_DATA_NETWORK_DIRECTION_HOST_TO_CLIENT;
    if (SDL_strcmp(direction, "client_to_host") == 0)
        return SLAYER3D_GAME_DATA_NETWORK_DIRECTION_CLIENT_TO_HOST;
    if (SDL_strcmp(direction, "bidirectional") == 0)
        return SLAYER3D_GAME_DATA_NETWORK_DIRECTION_BIDIRECTIONAL;
    return SLAYER3D_GAME_DATA_NETWORK_DIRECTION_INVALID;
}

static const char *game_data_network_direction_name(slayer3d_game_data_network_direction direction)
{
    switch (direction)
    {
    case SLAYER3D_GAME_DATA_NETWORK_DIRECTION_HOST_TO_CLIENT:
        return "host_to_client";
    case SLAYER3D_GAME_DATA_NETWORK_DIRECTION_CLIENT_TO_HOST:
        return "client_to_host";
    case SLAYER3D_GAME_DATA_NETWORK_DIRECTION_BIDIRECTIONAL:
        return "bidirectional";
    default:
        return "invalid";
    }
}

static yyjson_val *game_data_find_network_control_by_name(const slayer3d_game_data_runtime *runtime,
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

static yyjson_val *game_data_find_network_control_by_index(const slayer3d_game_data_runtime *runtime, Uint32 index)
{
    yyjson_val *controls = obj_get(obj_get(runtime_root(runtime), "network"), "control_messages");
    return yyjson_is_arr(controls) && index < yyjson_arr_size(controls) ? yyjson_arr_get(controls, index) : NULL;
}

static bool game_data_network_control_packet_size(size_t *out_size)
{
    const size_t size = 4U + 4U + 4U + 4U + SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

static int game_data_network_control_signal_id(const slayer3d_game_data_runtime *runtime, yyjson_val *control)
{
    return slayer3d_game_data_find_signal(runtime, json_string(control, "signal", NULL));
}

static const char *game_data_replication_property_key(const char *path)
{
    static const char prefix[] = "properties.";
    const size_t prefix_len = sizeof(prefix) - 1U;
    return path != NULL && SDL_strncmp(path, prefix, prefix_len) == 0 ? path + prefix_len : NULL;
}

static bool game_data_read_actor_replication_field(const slayer3d_registered_actor *actor,
                                                   const slayer3d_replication_field_descriptor *field,
                                                   game_data_snapshot_value *out_value)
{
    if (actor == NULL || field == NULL || out_value == NULL || field->path == NULL)
        return false;

    out_value->type = field->type;
    if (SDL_strcmp(field->path, "active") == 0)
    {
        if (field->type != SLAYER3D_REPLICATION_FIELD_BOOL)
            return false;
        out_value->value.as_bool = actor->active;
        return true;
    }
    if (SDL_strcmp(field->path, "position") == 0)
    {
        if (field->type != SLAYER3D_REPLICATION_FIELD_VEC3)
            return false;
        out_value->value.as_vec3 = actor->position;
        return true;
    }
    if (SDL_strcmp(field->path, "rotation") == 0 || SDL_strcmp(field->path, "scale") == 0)
    {
        if (field->type != SLAYER3D_REPLICATION_FIELD_VEC3)
            return false;
        out_value->value.as_vec3 =
            slayer3d_properties_get_vec3(actor->props, field->path, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        return true;
    }

    const char *key = game_data_replication_property_key(field->path);
    const slayer3d_value *property = key != NULL ? slayer3d_properties_get_value(actor->props, key) : NULL;
    if (property == NULL)
        return false;

    switch (field->type)
    {
    case SLAYER3D_REPLICATION_FIELD_BOOL:
        if (property->type != SLAYER3D_VALUE_BOOL)
            return false;
        out_value->value.as_bool = property->as_bool;
        return true;
    case SLAYER3D_REPLICATION_FIELD_INT32:
    case SLAYER3D_REPLICATION_FIELD_ENUM_ID:
        if (property->type != SLAYER3D_VALUE_INT)
            return false;
        out_value->value.as_int32 = (Sint32)property->as_int;
        return true;
    case SLAYER3D_REPLICATION_FIELD_FLOAT32:
        if (property->type != SLAYER3D_VALUE_FLOAT)
            return false;
        out_value->value.as_float32 = property->as_float;
        return true;
    case SLAYER3D_REPLICATION_FIELD_VEC2:
        if (property->type != SLAYER3D_VALUE_VEC3)
            return false;
        out_value->value.as_vec2 = (slayer3d_vec2){property->as_vec3.x, property->as_vec3.y};
        return true;
    case SLAYER3D_REPLICATION_FIELD_VEC3:
        if (property->type != SLAYER3D_VALUE_VEC3)
            return false;
        out_value->value.as_vec3 = property->as_vec3;
        return true;
    default:
        return false;
    }
}

static bool game_data_write_snapshot_value(slayer3d_replication_writer *writer, const game_data_snapshot_value *value)
{
    if (writer == NULL || value == NULL || !slayer3d_replication_write_field_type(writer, value->type))
        return false;

    switch (value->type)
    {
    case SLAYER3D_REPLICATION_FIELD_BOOL:
        return slayer3d_replication_write_bool(writer, value->value.as_bool);
    case SLAYER3D_REPLICATION_FIELD_INT32:
        return slayer3d_replication_write_int32(writer, value->value.as_int32);
    case SLAYER3D_REPLICATION_FIELD_FLOAT32:
        return slayer3d_replication_write_float32(writer, value->value.as_float32);
    case SLAYER3D_REPLICATION_FIELD_ENUM_ID:
        return slayer3d_replication_write_enum_id(writer, value->value.as_int32);
    case SLAYER3D_REPLICATION_FIELD_VEC2:
        return slayer3d_replication_write_vec2(writer, value->value.as_vec2);
    case SLAYER3D_REPLICATION_FIELD_VEC3:
        return slayer3d_replication_write_vec3(writer, value->value.as_vec3);
    default:
        return false;
    }
}

static bool game_data_read_snapshot_value(slayer3d_replication_reader *reader,
                                          slayer3d_replication_field_type expected_type,
                                          game_data_snapshot_value *out_value)
{
    if (reader == NULL || out_value == NULL)
        return false;

    slayer3d_replication_field_type packet_type = SLAYER3D_REPLICATION_FIELD_BOOL;
    if (!slayer3d_replication_read_field_type(reader, &packet_type) || packet_type != expected_type)
        return false;

    out_value->type = expected_type;
    switch (expected_type)
    {
    case SLAYER3D_REPLICATION_FIELD_BOOL:
        return slayer3d_replication_read_bool(reader, &out_value->value.as_bool);
    case SLAYER3D_REPLICATION_FIELD_INT32:
        return slayer3d_replication_read_int32(reader, &out_value->value.as_int32);
    case SLAYER3D_REPLICATION_FIELD_FLOAT32:
        return slayer3d_replication_read_float32(reader, &out_value->value.as_float32);
    case SLAYER3D_REPLICATION_FIELD_ENUM_ID:
        return slayer3d_replication_read_enum_id(reader, &out_value->value.as_int32);
    case SLAYER3D_REPLICATION_FIELD_VEC2:
        return slayer3d_replication_read_vec2(reader, &out_value->value.as_vec2);
    case SLAYER3D_REPLICATION_FIELD_VEC3:
        return slayer3d_replication_read_vec3(reader, &out_value->value.as_vec3);
    default:
        return false;
    }
}

static bool game_data_apply_actor_replication_field(slayer3d_game_data_runtime *runtime,
                                                    slayer3d_registered_actor *actor,
                                                    const slayer3d_replication_field_descriptor *field,
                                                    const game_data_snapshot_value *value)
{
    if (actor == NULL || field == NULL || value == NULL || field->path == NULL || field->type != value->type)
        return false;

    if (SDL_strcmp(field->path, "active") == 0)
    {
        if (value->type != SLAYER3D_REPLICATION_FIELD_BOOL)
            return false;
        int actor_index = -1;
        actor_pool_runtime *pool = find_actor_pool_for_actor(runtime, actor->name, &actor_index);
        if (pool != NULL && actor_index >= 0)
        {
            if (value->value.as_bool)
            {
                if (actor_pool_actor_is_active(pool, actor, actor_index))
                    return true;
                return actor_pool_initialize_slot(runtime, pool, actor_index, true);
            }
            if (actor_pool_lifecycle_state(pool, actor_index) == ACTOR_LIFECYCLE_INACTIVE)
                return true;
            return actor_pool_request_despawn(runtime, pool, actor, actor_index, "network_snapshot");
        }
        actor->active = value->value.as_bool;
        return true;
    }
    if (SDL_strcmp(field->path, "position") == 0)
    {
        if (value->type != SLAYER3D_REPLICATION_FIELD_VEC3)
            return false;
        actor_set_position(actor, value->value.as_vec3);
        return true;
    }
    if (SDL_strcmp(field->path, "rotation") == 0 || SDL_strcmp(field->path, "scale") == 0)
    {
        if (value->type != SLAYER3D_REPLICATION_FIELD_VEC3)
            return false;
        slayer3d_properties_set_vec3(actor->props, field->path, value->value.as_vec3);
        return true;
    }

    const char *key = game_data_replication_property_key(field->path);
    if (key == NULL)
        return false;

    switch (value->type)
    {
    case SLAYER3D_REPLICATION_FIELD_BOOL:
        slayer3d_properties_set_bool(actor->props, key, value->value.as_bool);
        return true;
    case SLAYER3D_REPLICATION_FIELD_INT32:
    case SLAYER3D_REPLICATION_FIELD_ENUM_ID:
        slayer3d_properties_set_int(actor->props, key, value->value.as_int32);
        return true;
    case SLAYER3D_REPLICATION_FIELD_FLOAT32:
        slayer3d_properties_set_float(actor->props, key, value->value.as_float32);
        return true;
    case SLAYER3D_REPLICATION_FIELD_VEC2: {
        const slayer3d_vec3 current =
            slayer3d_properties_get_vec3(actor->props, key, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(actor->props, key,
                                     slayer3d_vec3_make(value->value.as_vec2.x, value->value.as_vec2.y, current.z));
        return true;
    }
    case SLAYER3D_REPLICATION_FIELD_VEC3:
        slayer3d_properties_set_vec3(actor->props, key, value->value.as_vec3);
        return true;
    default:
        return false;
    }
}

static void copy_property_value(slayer3d_properties *target, const char *key, const slayer3d_value *value)
{
    if (target == NULL || key == NULL || value == NULL)
        return;

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        slayer3d_properties_set_int(target, key, value->as_int);
        break;
    case SLAYER3D_VALUE_FLOAT:
        slayer3d_properties_set_float(target, key, value->as_float);
        break;
    case SLAYER3D_VALUE_BOOL:
        slayer3d_properties_set_bool(target, key, value->as_bool);
        break;
    case SLAYER3D_VALUE_VEC3:
        slayer3d_properties_set_vec3(target, key, value->as_vec3);
        break;
    case SLAYER3D_VALUE_STRING:
        slayer3d_properties_set_string(target, key, value->as_string);
        break;
    case SLAYER3D_VALUE_COLOR:
        slayer3d_properties_set_color(target, key, value->as_color);
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

static slayer3d_mouse_axis mouse_axis_from_json(const char *name, bool *valid)
{
    if (valid != NULL)
        *valid = true;
    if (name == NULL)
    {
        if (valid != NULL)
            *valid = false;
        return SLAYER3D_MOUSE_AXIS_X;
    }
    if (SDL_strcmp(name, "x") == 0)
        return SLAYER3D_MOUSE_AXIS_X;
    if (SDL_strcmp(name, "y") == 0)
        return SLAYER3D_MOUSE_AXIS_Y;
    if (SDL_strcmp(name, "wheel") == 0)
        return SLAYER3D_MOUSE_AXIS_WHEEL;
    if (SDL_strcmp(name, "wheel_x") == 0)
        return SLAYER3D_MOUSE_AXIS_WHEEL_X;
    if (valid != NULL)
        *valid = false;
    return SLAYER3D_MOUSE_AXIS_X;
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

static int find_timer_index(const slayer3d_game_data_runtime *runtime, const char *name)
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

static adapter_entry *find_adapter(slayer3d_game_data_runtime *runtime, const char *name)
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

static script_entry *find_script(slayer3d_game_data_runtime *runtime, const char *id)
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

static bool append_adapter(slayer3d_game_data_runtime *runtime, const char *name,
                           slayer3d_game_data_adapter_fn callback, void *userdata)
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

static bool set_adapter_lua_function(slayer3d_game_data_runtime *runtime, const char *name, const char *script_id,
                                     const char *function_name, slayer3d_script_ref function_ref)
{
    if (runtime == NULL || name == NULL || name[0] == '\0' || script_id == NULL || script_id[0] == '\0' ||
        function_name == NULL || function_name[0] == '\0' || function_ref == SLAYER3D_SCRIPT_REF_INVALID)
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
    if (entry->lua_function_ref != SLAYER3D_SCRIPT_REF_INVALID)
        slayer3d_script_engine_unref(runtime->scripts, entry->lua_function_ref);
    entry->lua_function_ref = function_ref;
    return true;
}

static void lua_push_property_value(lua_State *lua, const slayer3d_value *value)
{
    if (value == NULL)
    {
        lua_pushnil(lua);
        return;
    }

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        lua_pushinteger(lua, value->as_int);
        break;
    case SLAYER3D_VALUE_FLOAT:
        lua_pushnumber(lua, value->as_float);
        break;
    case SLAYER3D_VALUE_BOOL:
        lua_pushboolean(lua, value->as_bool);
        break;
    case SLAYER3D_VALUE_STRING:
        lua_pushstring(lua, value->as_string != NULL ? value->as_string : "");
        break;
    case SLAYER3D_VALUE_VEC3:
        lua_newtable(lua);
        lua_pushnumber(lua, value->as_vec3.x);
        lua_setfield(lua, -2, "x");
        lua_pushnumber(lua, value->as_vec3.y);
        lua_setfield(lua, -2, "y");
        lua_pushnumber(lua, value->as_vec3.z);
        lua_setfield(lua, -2, "z");
        break;
    case SLAYER3D_VALUE_COLOR:
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

static void lua_push_payload(lua_State *lua, const slayer3d_properties *payload)
{
    lua_newtable(lua);
    if (payload == NULL)
        return;

    const int count = slayer3d_properties_count(payload);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (!slayer3d_properties_get_key_at(payload, i, &key, NULL) || key == NULL)
            continue;
        lua_push_property_value(lua, slayer3d_properties_get_value(payload, key));
        lua_setfield(lua, -2, key);
    }
}

static void lua_push_actor_wrapper(lua_State *lua, const slayer3d_registered_actor *actor)
{
    if (actor == NULL)
    {
        lua_pushnil(lua);
        return;
    }

    lua_getglobal(lua, "slayer3d");
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

static void lua_push_adapter_context(lua_State *lua, const slayer3d_game_data_runtime *runtime,
                                     const adapter_entry *adapter)
{
    lua_getglobal(lua, "slayer3d");
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

static bool call_lua_adapter(slayer3d_game_data_runtime *runtime, const adapter_entry *adapter,
                             slayer3d_registered_actor *target, const slayer3d_properties *payload)
{
    if (runtime == NULL || runtime->scripts == NULL || adapter == NULL ||
        adapter->lua_function_ref == SLAYER3D_SCRIPT_REF_INVALID)
        return false;

    lua_State *lua = slayer3d_script_engine_lua_state(runtime->scripts);
    if (lua == NULL || !slayer3d_script_engine_push_ref(runtime->scripts, adapter->lua_function_ref))
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

static bool invoke_adapter(slayer3d_game_data_runtime *runtime, adapter_entry *adapter,
                           slayer3d_registered_actor *target, const slayer3d_properties *payload)
{
    if (adapter == NULL)
        return false;
    if (adapter->callback != NULL)
        return adapter->callback(adapter->userdata, runtime, adapter->name, target, payload);
    return call_lua_adapter(runtime, adapter, target, payload);
}

int slayer3d_game_data_find_signal(const slayer3d_game_data_runtime *runtime, const char *name)
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

int slayer3d_game_data_find_action(const slayer3d_game_data_runtime *runtime, const char *name)
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

static const char *find_action_name(const slayer3d_game_data_runtime *runtime, int action_id)
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

slayer3d_registered_actor *slayer3d_game_data_find_actor(const slayer3d_game_data_runtime *runtime, const char *name)
{
    return slayer3d_actor_registry_find(runtime_registry(runtime), name);
}

slayer3d_registered_actor *slayer3d_game_data_find_actor_with_tag(const slayer3d_game_data_runtime *runtime,
                                                                  const char *tag)
{
    const char *tags[1] = {tag};
    return slayer3d_game_data_find_actor_with_tags(runtime, tags, 1);
}

slayer3d_registered_actor *slayer3d_game_data_find_actor_with_tags(const slayer3d_game_data_runtime *runtime,
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
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(entity, "name", NULL));
        if (actor != NULL)
            return actor;
    }
    return NULL;
}

bool slayer3d_game_data_get_app_control(const slayer3d_game_data_runtime *runtime,
                                        slayer3d_game_data_app_control *out_control)
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
    out_control->start_signal_id = slayer3d_game_data_find_signal(runtime, json_string(app, "start_signal", NULL));
    out_control->pause_action_id = slayer3d_game_data_find_action(runtime, json_string(pause, "action", NULL));
    out_control->startup_transition = json_string(app, "startup_transition", NULL);
    out_control->quit_action_id = slayer3d_game_data_find_action(runtime, json_string(quit, "action", NULL));
    out_control->quit_transition = json_string(quit, "transition", NULL);
    out_control->quit_signal_id = slayer3d_game_data_find_signal(runtime, json_string(quit, "quit_signal", NULL));
    out_control->window_apply_signal_id =
        slayer3d_game_data_find_signal(runtime, json_string(window, "apply_signal", NULL));
    out_control->window_settings_target = json_string(window_settings, "target", "entity.settings");
    out_control->window_display_mode_key = json_string(window_settings, "display_mode", "display_mode");
    out_control->window_renderer_key = json_string(window_settings, "renderer", "renderer");
    out_control->window_vsync_key = json_string(window_settings, "vsync", "vsync");
    return true;
}

bool slayer3d_game_data_app_signal_applies_window_settings(const slayer3d_game_data_runtime *runtime, int signal_id)
{
    if (runtime == NULL || signal_id < 0)
        return false;

    yyjson_val *window = obj_get(obj_get(runtime_root(runtime), "app"), "window");
    const char *apply_signal = json_string(window, "apply_signal", NULL);
    if (apply_signal != NULL && slayer3d_game_data_find_signal(runtime, apply_signal) == signal_id)
        return true;

    yyjson_val *apply_signals = obj_get(window, "apply_signals");
    if (!yyjson_is_arr(apply_signals))
        return false;

    for (size_t i = 0; i < yyjson_arr_size(apply_signals); ++i)
    {
        yyjson_val *signal = yyjson_arr_get(apply_signals, i);
        if (yyjson_is_str(signal) && slayer3d_game_data_find_signal(runtime, yyjson_get_str(signal)) == signal_id)
            return true;
    }
    return false;
}

bool slayer3d_game_data_get_font_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                       slayer3d_game_data_font_asset *out_font)
{
    if (out_font != NULL)
    {
        SDL_zero(*out_font);
        out_font->builtin_id = SLAYER3D_BUILTIN_FONT_INTER;
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

bool slayer3d_game_data_get_image_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                        slayer3d_game_data_image_asset *out_image)
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

bool slayer3d_game_data_get_model_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                        slayer3d_game_data_model_asset *out_model)
{
    if (out_model != NULL)
        SDL_zero(*out_model);
    if (runtime == NULL || id == NULL || out_model == NULL)
        return false;

    yyjson_val *model = find_model_json(runtime, id);
    if (!yyjson_is_obj(model))
        return false;

    out_model->id = json_string(model, "id", NULL);
    out_model->path = json_string(model, "path", NULL);
    return out_model->id != NULL && out_model->path != NULL;
}

bool slayer3d_game_data_get_sound_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                        slayer3d_game_data_sound_asset *out_sound)
{
    if (out_sound != NULL)
    {
        SDL_zero(*out_sound);
        out_sound->volume = 1.0f;
        out_sound->pitch = 1.0f;
        out_sound->bus = SLAYER3D_AUDIO_BUS_SOUND_EFFECTS;
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

bool slayer3d_game_data_get_music_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                        slayer3d_game_data_music_asset *out_music)
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

bool slayer3d_game_data_get_ambient_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                          slayer3d_game_data_ambient_asset *out_ambient)
{
    if (out_ambient != NULL)
    {
        SDL_zero(*out_ambient);
        out_ambient->volume = 1.0f;
        out_ambient->loop = true;
    }
    if (runtime == NULL || id == NULL || out_ambient == NULL)
        return false;

    yyjson_val *ambient = find_ambient_json(runtime, id);
    if (!yyjson_is_obj(ambient))
        return false;

    out_ambient->id = json_string(ambient, "id", NULL);
    out_ambient->ambient_id = json_int(ambient, "ambient_id", -1);
    out_ambient->path = json_string(ambient, "path", NULL);
    out_ambient->volume = json_float(ambient, "volume", out_ambient->volume);
    out_ambient->loop = json_bool(ambient, "loop", out_ambient->loop);
    return out_ambient->id != NULL && out_ambient->ambient_id >= 0 && out_ambient->path != NULL;
}

bool slayer3d_game_data_get_sprite_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                         slayer3d_game_data_sprite_asset *out_sprite)
{
    if (out_sprite != NULL)
    {
        SDL_zero(*out_sprite);
        out_sprite->source_kind = SLAYER3D_SPRITE_ASSET_SOURCE_SHEET;
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
    const char *kind = json_string(sprite, "kind", "sheet");
    out_sprite->source_kind = kind != NULL && SDL_strcmp(kind, "files") == 0 ? SLAYER3D_SPRITE_ASSET_SOURCE_FILES
                                                                             : SLAYER3D_SPRITE_ASSET_SOURCE_SHEET;
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

    if (out_sprite->id == NULL || out_sprite->frame_count <= 0 || out_sprite->direction_count <= 0 ||
        (out_sprite->source_kind == SLAYER3D_SPRITE_ASSET_SOURCE_SHEET &&
         (out_sprite->path == NULL || out_sprite->frame_width <= 0 || out_sprite->frame_height <= 0 ||
          out_sprite->columns <= 0 || out_sprite->rows <= 0)))
    {
        SDL_zero(*out_sprite);
        return false;
    }
    return true;
}

static bool read_sprite_path_array(yyjson_val *array, const char ***out_paths, int *out_count)
{
    if (out_paths == NULL || out_count == NULL)
        return false;

    *out_paths = NULL;
    *out_count = 0;
    if (!yyjson_is_arr(array) || yyjson_arr_size(array) <= 0)
        return false;

    const size_t count = yyjson_arr_size(array);
    const char **paths = (const char **)SDL_calloc(count, sizeof(*paths));
    if (paths == NULL)
        return false;

    for (size_t i = 0; i < count; ++i)
    {
        yyjson_val *value = yyjson_arr_get(array, i);
        const char *path = yyjson_get_str(value);
        if (path == NULL || path[0] == '\0')
        {
            SDL_free(paths);
            return false;
        }
        paths[i] = path;
    }

    *out_paths = paths;
    *out_count = (int)count;
    return true;
}

bool slayer3d_game_data_load_sprite_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                          slayer3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                          int error_buffer_size)
{
    slayer3d_game_data_sprite_asset sprite;
    slayer3d_sprite_asset_source source;
    const char **base_paths = NULL;
    const char **frame_paths = NULL;
    int base_path_count = 0;
    int frame_path_count = 0;

    if (out_sprite != NULL)
        SDL_zero(*out_sprite);
    if (runtime == NULL || id == NULL || out_sprite == NULL)
        return false;

    if (!slayer3d_game_data_get_sprite_asset(runtime, id, &sprite))
    {
        set_error(error_buffer, error_buffer_size, "sprite asset not found");
        return false;
    }

    SDL_zero(source);
    source.kind = sprite.source_kind;
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

    if (sprite.source_kind == SLAYER3D_SPRITE_ASSET_SOURCE_FILES)
    {
        yyjson_val *sprite_json = find_sprite_json(runtime, id);
        if (!read_sprite_path_array(obj_get(sprite_json, "base_paths"), &base_paths, &base_path_count) ||
            !read_sprite_path_array(obj_get(sprite_json, "frame_paths"), &frame_paths, &frame_path_count))
        {
            SDL_free(base_paths);
            SDL_free(frame_paths);
            set_error(error_buffer, error_buffer_size, "invalid sprite file-list paths");
            return false;
        }
        if (base_path_count != sprite.direction_count ||
            frame_path_count != sprite.frame_count * sprite.direction_count)
        {
            SDL_free(base_paths);
            SDL_free(frame_paths);
            set_error(error_buffer, error_buffer_size, "sprite file-list path count does not match metadata");
            return false;
        }
        source.base_paths = base_paths;
        source.frame_paths = frame_paths;
    }

    const bool loaded =
        slayer3d_sprite_asset_load(runtime->assets, &source, out_sprite, error_buffer, error_buffer_size);
    SDL_free(base_paths);
    SDL_free(frame_paths);
    if (!loaded)
        return false;
    return true;
}

const char *slayer3d_game_data_active_camera(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->active_camera : NULL;
}

bool slayer3d_game_data_get_camera(const slayer3d_game_data_runtime *runtime, const char *name,
                                   slayer3d_camera3d *out_camera)
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

    if (SDL_strcmp(type, "fps") == 0)
    {
        slayer3d_registered_actor *target =
            slayer3d_game_data_find_actor(runtime, json_string(camera_json, "target_entity", NULL));
        if (target == NULL)
            return false;

        const float fov = camera_fov_degrees(runtime, camera_json, SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES);
        const slayer3d_camera_fov_axis fov_axis = camera_fov_axis(runtime, camera_json);
        const fps_controller_runtime *controller = find_fps_controller_const(runtime, target->name);
        if (controller != NULL && controller->initialized)
        {
            *out_camera = slayer3d_fps_mover_camera(&controller->mover, fov);
            out_camera->fov_axis = fov_axis;
            return true;
        }

        slayer3d_fps_mover fallback_mover;
        SDL_zero(fallback_mover);
        fallback_mover.position = target->position;
        fallback_mover.yaw = slayer3d_properties_get_float(
            target->props, json_string(camera_json, "yaw_property", "yaw"), json_float(camera_json, "yaw", 0.0f));
        fallback_mover.pitch = slayer3d_properties_get_float(
            target->props, json_string(camera_json, "pitch_property", "pitch"), json_float(camera_json, "pitch", 0.0f));
        fallback_mover.view_smooth = slayer3d_properties_get_float(
            target->props, json_string(camera_json, "view_smooth_property", "view_smooth"), 0.0f);
        *out_camera = slayer3d_fps_mover_camera(&fallback_mover, fov);
        out_camera->fov_axis = fov_axis;
        return true;
    }

    if (SDL_strcmp(type, "chase") == 0)
    {
        slayer3d_registered_actor *target =
            slayer3d_game_data_find_actor(runtime, json_string(camera_json, "target_entity", NULL));
        if (target == NULL)
            return false;

        const char *velocity_property = json_string(camera_json, "velocity_property", "velocity");
        slayer3d_vec3 velocity =
            slayer3d_properties_get_vec3(target->props, velocity_property, slayer3d_vec3_make(1.0f, 0.0f, 0.0f));
        velocity.z = 0.0f;
        float velocity_len = SDL_sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
        if (velocity_len < 0.001f)
        {
            velocity = json_vec3(camera_json, "fallback_forward", slayer3d_vec3_make(1.0f, 0.0f, 0.0f));
            velocity.z = 0.0f;
            velocity_len = SDL_sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
        }
        if (velocity_len < 0.001f)
            velocity = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        else
        {
            velocity.x /= velocity_len;
            velocity.y /= velocity_len;
        }

        const float target_z_offset = json_float(camera_json, "target_z_offset", 0.0f);
        const float camera_height = json_float(camera_json, "height", 1.0f);
        const float chase_distance = json_float(camera_json, "chase_distance", 2.0f);
        const float lookahead = json_float(camera_json, "lookahead", 1.0f);
        const slayer3d_vec3 target_offset =
            json_vec3(camera_json, "target_offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        const slayer3d_vec3 eye_offset = json_vec3(camera_json, "eye_offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        const slayer3d_vec3 anchor =
            slayer3d_vec3_make(target->position.x + target_offset.x, target->position.y + target_offset.y,
                               target->position.z + target_offset.z);

        out_camera->position = slayer3d_vec3_make(anchor.x - velocity.x * chase_distance + eye_offset.x,
                                                  anchor.y - velocity.y * chase_distance + eye_offset.y,
                                                  anchor.z + camera_height + eye_offset.z);
        out_camera->target = slayer3d_vec3_make(anchor.x + velocity.x * lookahead, anchor.y + velocity.y * lookahead,
                                                anchor.z + target_z_offset);
        out_camera->up = json_vec3(camera_json, "up", slayer3d_vec3_make(0.0f, 0.0f, 1.0f));
        out_camera->fovy = camera_fov_degrees(runtime, camera_json, SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES);
        out_camera->fov_axis = camera_fov_axis(runtime, camera_json);
        out_camera->projection = SLAYER3D_CAMERA_PERSPECTIVE;
        return true;
    }

    out_camera->position = json_vec3(camera_json, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    out_camera->target = json_vec3(camera_json, "target", slayer3d_vec3_make(0.0f, 0.0f, -1.0f));
    out_camera->up = json_vec3(camera_json, "up", slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    if (SDL_strcmp(type, "orthographic") == 0)
    {
        out_camera->projection = SLAYER3D_CAMERA_ORTHOGRAPHIC;
        out_camera->fovy = json_float(camera_json, "size", 10.0f);
    }
    else
    {
        out_camera->projection = SLAYER3D_CAMERA_PERSPECTIVE;
        out_camera->fovy = camera_fov_degrees(runtime, camera_json, SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES);
        out_camera->fov_axis = camera_fov_axis(runtime, camera_json);
    }
    return true;
}

bool slayer3d_game_data_get_camera_float(const slayer3d_game_data_runtime *runtime, const char *camera_name,
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

bool slayer3d_game_data_get_world_units(const slayer3d_game_data_runtime *runtime, const char **out_units,
                                        float *out_meters_per_unit)
{
    if (out_units != NULL)
        *out_units = NULL;
    if (out_meters_per_unit != NULL)
        *out_meters_per_unit = 0.0f;
    if (runtime == NULL || out_units == NULL || out_meters_per_unit == NULL)
        return false;

    yyjson_val *world = obj_get(runtime_root(runtime), "world");
    *out_units = json_string(world, "units", SLAYER3D_GAME_DATA_DEFAULT_WORLD_UNITS);
    *out_meters_per_unit = json_float(world, "meters_per_unit", SLAYER3D_GAME_DATA_DEFAULT_METERS_PER_UNIT);
    return *out_units != NULL && (*out_units)[0] != '\0' && *out_meters_per_unit > 0.0f;
}

int slayer3d_game_data_world_light_count(const slayer3d_game_data_runtime *runtime)
{
    yyjson_val *lights = obj_get(obj_get(runtime_root(runtime), "world"), "lights");
    int count = 0;
    for (size_t i = 0; yyjson_is_arr(lights) && i < yyjson_arr_size(lights); ++i)
    {
        yyjson_val *light = yyjson_arr_get(lights, i);
        if (scene_state_bool(runtime, json_string(light, "enabled_key", NULL), json_bool(light, "enabled", true)))
            count++;
    }
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; runtime != NULL && yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (!active_scene_has_entity_internal(runtime, entity_name) || actor == NULL || !actor->active)
            continue;
        yyjson_val *components = obj_get(entity, "components");
        for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type", "");
            if (SDL_strncmp(type, "light.", 6) == 0 &&
                scene_state_bool(runtime, json_string(component, "enabled_key", NULL),
                                 json_bool(component, "enabled", true)))
            {
                count++;
            }
        }
    }
    for (int pool_index = 0; runtime != NULL && pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
            continue;
        yyjson_val *components = obj_get(pool->archetype_json, "components");
        int light_components = 0;
        for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type", "");
            if (SDL_strncmp(type, "light.", 6) == 0 &&
                scene_state_bool(runtime, json_string(component, "enabled_key", NULL),
                                 json_bool(component, "enabled", true)))
            {
                light_components++;
            }
        }
        if (light_components <= 0)
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index))
                count += light_components;
        }
    }
    return count;
}

bool slayer3d_game_data_get_world_ambient_light(const slayer3d_game_data_runtime *runtime, float out_rgb[3])
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

static bool read_light_json(const slayer3d_game_data_runtime *runtime, yyjson_val *light_json,
                            const slayer3d_registered_actor *component_actor, slayer3d_light *out_light)
{
    if (out_light != NULL)
        SDL_zero(*out_light);
    if (runtime == NULL || light_json == NULL || out_light == NULL)
        return false;

    const char *type = json_string(light_json, "type", "point");
    if (SDL_strcmp(type, "directional") == 0 || SDL_strcmp(type, "light.directional") == 0)
        out_light->type = SLAYER3D_LIGHT_DIRECTIONAL;
    else if (SDL_strcmp(type, "spot") == 0 || SDL_strcmp(type, "light.spot") == 0)
        out_light->type = SLAYER3D_LIGHT_SPOT;
    else
        out_light->type = SLAYER3D_LIGHT_POINT;

    out_light->position = json_vec3(light_json, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    if (component_actor != NULL)
        out_light->position = component_actor->position;
    const char *target_entity = json_string(light_json, "target_entity", NULL);
    slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, target_entity);
    yyjson_val *target_entities = obj_get(light_json, "target_entities");
    for (size_t i = 0; target == NULL && yyjson_is_arr(target_entities) && i < yyjson_arr_size(target_entities); ++i)
    {
        const char *candidate = yyjson_get_str(yyjson_arr_get(target_entities, i));
        if (candidate != NULL && active_scene_has_entity_internal(runtime, candidate))
            target = slayer3d_game_data_find_actor(runtime, candidate);
    }
    for (size_t i = 0; target == NULL && yyjson_is_arr(target_entities) && i < yyjson_arr_size(target_entities); ++i)
    {
        const char *candidate = yyjson_get_str(yyjson_arr_get(target_entities, i));
        if (candidate != NULL)
            target = slayer3d_game_data_find_actor(runtime, candidate);
    }
    if (target != NULL)
        out_light->position = target->position;
    if (target != NULL || component_actor != NULL)
    {
        const slayer3d_vec3 offset = json_vec3(light_json, "offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        out_light->position = slayer3d_vec3_make(out_light->position.x + offset.x, out_light->position.y + offset.y,
                                                 out_light->position.z + offset.z);
    }
    out_light->direction = json_vec3(light_json, "direction", slayer3d_vec3_make(0.0f, -1.0f, 0.0f));
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

static bool slayer3d_game_data_get_world_light_internal(const slayer3d_game_data_runtime *runtime, int index,
                                                        slayer3d_light *out_light, yyjson_val **out_light_json)
{
    yyjson_val *lights = obj_get(obj_get(runtime_root(runtime), "world"), "lights");
    if (out_light_json != NULL)
        *out_light_json = NULL;
    if (runtime == NULL || index < 0 || out_light == NULL)
        return false;

    int remaining = index;
    for (size_t i = 0; yyjson_is_arr(lights) && i < yyjson_arr_size(lights); ++i)
    {
        yyjson_val *light = yyjson_arr_get(lights, i);
        if (!scene_state_bool(runtime, json_string(light, "enabled_key", NULL), json_bool(light, "enabled", true)))
            continue;
        if (remaining-- == 0)
        {
            if (out_light_json != NULL)
                *out_light_json = light;
            return read_light_json(runtime, light, NULL, out_light);
        }
    }

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (!active_scene_has_entity_internal(runtime, entity_name) || actor == NULL || !actor->active)
            continue;
        yyjson_val *components = obj_get(entity, "components");
        for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type", "");
            if (SDL_strncmp(type, "light.", 6) != 0)
                continue;
            if (!scene_state_bool(runtime, json_string(component, "enabled_key", NULL),
                                  json_bool(component, "enabled", true)))
            {
                continue;
            }
            if (remaining-- == 0)
            {
                if (out_light_json != NULL)
                    *out_light_json = component;
                return read_light_json(runtime, component, actor, out_light);
            }
        }
    }
    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
            continue;
        yyjson_val *components = obj_get(pool->archetype_json, "components");
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (!actor_pool_actor_is_active(pool, actor, actor_index))
                continue;
            for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
            {
                yyjson_val *component = yyjson_arr_get(components, c);
                const char *type = json_string(component, "type", "");
                if (SDL_strncmp(type, "light.", 6) != 0)
                    continue;
                if (!scene_state_bool(runtime, json_string(component, "enabled_key", NULL),
                                      json_bool(component, "enabled", true)))
                {
                    continue;
                }
                if (remaining-- == 0)
                {
                    if (out_light_json != NULL)
                        *out_light_json = component;
                    return read_light_json(runtime, component, actor, out_light);
                }
            }
        }
    }
    return false;
}

bool slayer3d_game_data_get_world_light(const slayer3d_game_data_runtime *runtime, int index, slayer3d_light *out_light)
{
    return slayer3d_game_data_get_world_light_internal(runtime, index, out_light, NULL);
}

static float game_data_clampf(float value, float lo, float hi);

static void light_color_lerp(float color[3], const slayer3d_vec3 target, float t)
{
    if (color == NULL)
        return;
    t = game_data_clampf(t, 0.0f, 1.0f);
    color[0] = color[0] + (target.x - color[0]) * t;
    color[1] = color[1] + (target.y - color[1]) * t;
    color[2] = color[2] + (target.z - color[2]) * t;
}

static bool light_effect_sample_color_cycle(yyjson_val *effect, float time, slayer3d_vec3 fallback,
                                            slayer3d_vec3 *out_color)
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

    const slayer3d_vec3 a = json_vec3_value(yyjson_arr_get(colors, index), fallback);
    const slayer3d_vec3 b = json_vec3_value(yyjson_arr_get(colors, next_index), a);
    *out_color = slayer3d_vec3_make(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    return true;
}

static void apply_light_effects(const slayer3d_game_data_runtime *runtime, yyjson_val *light_json,
                                const slayer3d_game_data_render_eval *eval, slayer3d_light *light)
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
            slayer3d_vec3 target;
            if (light_effect_sample_color_cycle(
                    effect, time, slayer3d_vec3_make(light->color[0], light->color[1], light->color[2]), &target))
            {
                light_color_lerp(light->color, target, json_float(effect, "color_blend", 1.0f));
            }
            continue;
        }
        else if (SDL_strcmp(type, "flash") == 0)
        {
            slayer3d_registered_actor *source =
                slayer3d_game_data_find_actor(runtime, json_string(effect, "source", NULL));
            const char *property = json_string(effect, "property", NULL);
            value = source != NULL && property != NULL ? slayer3d_properties_get_float(source->props, property, 0.0f)
                                                       : 0.0f;
            value = game_data_clampf(value, 0.0f, 1.0f);
        }
        else
        {
            continue;
        }

        yyjson_val *color = obj_get(effect, "color");
        if (yyjson_is_arr(color))
        {
            const slayer3d_vec3 target =
                json_vec3_value(color, slayer3d_vec3_make(light->color[0], light->color[1], light->color[2]));
            light_color_lerp(light->color, target, value * json_float(effect, "color_blend", 1.0f));
        }
        light->intensity += json_float(effect, "intensity_add", 0.0f) * value;
        light->range += json_float(effect, "range_add", 0.0f) * value;
    }
}

bool slayer3d_game_data_get_world_light_evaluated(const slayer3d_game_data_runtime *runtime, int index,
                                                  const slayer3d_game_data_render_eval *eval, slayer3d_light *out_light)
{
    yyjson_val *light_json = NULL;
    if (!slayer3d_game_data_get_world_light_internal(runtime, index, out_light, &light_json))
        return false;

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

static slayer3d_color game_data_color_lerp(slayer3d_color a, slayer3d_color b, float t)
{
    t = game_data_clampf(t, 0.0f, 1.0f);
    return (slayer3d_color){
        (Uint8)((float)a.r + ((float)b.r - (float)a.r) * t),
        (Uint8)((float)a.g + ((float)b.g - (float)a.g) * t),
        (Uint8)((float)a.b + ((float)b.b - (float)a.b) * t),
        (Uint8)((float)a.a + ((float)b.a - (float)a.a) * t),
    };
}

static void apply_render_effects(const slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                 const slayer3d_game_data_render_eval *eval,
                                 slayer3d_game_data_render_primitive *primitive)
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
            slayer3d_registered_actor *source =
                slayer3d_game_data_find_actor(runtime, json_string(effect, "source", NULL));
            const char *property = json_string(effect, "property", NULL);
            const float value = game_data_clampf(source != NULL && property != NULL
                                                     ? slayer3d_properties_get_float(source->props, property, 0.0f)
                                                     : 0.0f,
                                                 0.0f, 1.0f);
            primitive->color =
                game_data_color_lerp(primitive->color, json_color(effect, "color", primitive->color), value);

            const slayer3d_vec3 size_add = json_vec3(effect, "size_add", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            const char *size_mode = json_string(effect, "size_mode", "vector");
            if (SDL_strcmp(size_mode, "minor_axis") == 0 && primitive->type == SLAYER3D_GAME_DATA_RENDER_CUBE)
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

            const slayer3d_vec3 emissive = json_vec3(effect, "emissive", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
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

            const slayer3d_vec3 base = json_vec3(effect, "emissive_base", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            const slayer3d_vec3 add = json_vec3(effect, "emissive_add", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            primitive->emissive_color.x += base.x + add.x * pulse;
            primitive->emissive_color.y += base.y + add.y * pulse;
            primitive->emissive_color.z += base.z + add.z * pulse;

            const slayer3d_vec3 size_add = json_vec3(effect, "size_add", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            primitive->size.x += size_add.x * pulse;
            primitive->size.y += size_add.y * pulse;
            primitive->size.z += size_add.z * pulse;
            primitive->radius += json_float(effect, "radius_add", 0.0f) * pulse;
        }
        else if (SDL_strcmp(type, "drift") == 0)
        {
            const float time = eval != NULL ? eval->time : 0.0f;
            const float phase = json_float(effect, "phase", 0.0f);
            const slayer3d_vec3 offset = json_vec3(effect, "offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            const slayer3d_vec3 rates =
                json_vec3(effect, "rates",
                          slayer3d_vec3_make(json_float(effect, "rate", 1.0f), json_float(effect, "rate", 1.0f),
                                             json_float(effect, "rate", 1.0f)));
            primitive->position.x += offset.x * SDL_sinf(time * rates.x + phase);
            primitive->position.y += offset.y * SDL_cosf(time * rates.y + phase * 1.37f);
            primitive->position.z += offset.z * SDL_sinf(time * rates.z + phase * 0.73f);
        }
        else if (SDL_strcmp(type, "emissive") == 0)
        {
            const slayer3d_vec3 rgb = json_vec3(effect, "color", slayer3d_vec3_make(0.2f, 0.2f, 0.2f));
            primitive->emissive_color.x += rgb.x;
            primitive->emissive_color.y += rgb.y;
            primitive->emissive_color.z += rgb.z;
        }
    }
}

static slayer3d_game_data_mesh_primitive_kind mesh_primitive_kind_from_string(const char *name)
{
    if (name == NULL)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID;
    if (SDL_strcmp(name, "cube") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE;
    if (SDL_strcmp(name, "sphere") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE;
    if (SDL_strcmp(name, "capsule") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE;
    if (SDL_strcmp(name, "cylinder") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER;
    if (SDL_strcmp(name, "cone") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE;
    if (SDL_strcmp(name, "torus") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS;
    if (SDL_strcmp(name, "pyramid") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID;
    if (SDL_strcmp(name, "wedge") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE;
    if (SDL_strcmp(name, "plane") == 0 || SDL_strcmp(name, "quad") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE;
    if (SDL_strcmp(name, "disc") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC;
    if (SDL_strcmp(name, "hemisphere") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE;
    if (SDL_strcmp(name, "rounded_box") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX;
    if (SDL_strcmp(name, "tube_segment") == 0 || SDL_strcmp(name, "pipe") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT;
    if (SDL_strcmp(name, "arrow") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW;
    if (SDL_strcmp(name, "billboard_plane") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE;
    return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID;
}

static bool render_component_lighting_enabled(const slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                              bool fallback)
{
    return scene_state_bool(runtime, json_string(component, "lighting_key", NULL),
                            json_bool(component, "lighting", fallback));
}

static slayer3d_game_data_render_draw_mode render_draw_mode_from_string(const char *name)
{
    if (name == NULL || SDL_strcmp(name, "solid") == 0)
        return SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID;
    if (SDL_strcmp(name, "wire") == 0)
        return SLAYER3D_GAME_DATA_RENDER_DRAW_WIRE;
    if (SDL_strcmp(name, "solid_wire") == 0)
        return SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID_WIRE;
    return SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID;
}

static void populate_mesh_primitive_descriptor(const slayer3d_registered_actor *actor, yyjson_val *component,
                                               slayer3d_game_data_render_primitive *primitive)
{
    if (component == NULL || primitive == NULL)
        return;
    primitive->type = SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE;
    primitive->mesh_primitive = mesh_primitive_kind_from_string(json_string(component, "primitive", NULL));
    primitive->draw_mode = render_draw_mode_from_string(json_string(component, "draw_mode", NULL));
    primitive->size = json_vec3(component, "size", primitive->size);
    const char *size_property = json_string(component, "size_property", NULL);
    if (actor != NULL && size_property != NULL)
        primitive->size = slayer3d_properties_get_vec3(actor->props, size_property, primitive->size);
    primitive->radius = json_float(component, "radius", primitive->radius);
    primitive->height =
        json_float(component, "height", primitive->height > 0.0f ? primitive->height : primitive->size.y);
    primitive->radius_top = json_float(component, "radius_top", primitive->radius);
    primitive->radius_bottom = json_float(component, "radius_bottom", primitive->radius);
    primitive->major_radius = json_float(component, "major_radius", primitive->major_radius);
    primitive->minor_radius = json_float(component, "minor_radius", primitive->minor_radius);
    primitive->bevel_radius =
        json_float(component, "bevel_radius", json_float(component, "radius", primitive->bevel_radius));
    primitive->arc_angle = json_float(component, "arc_angle", primitive->arc_angle);
    primitive->slices = SDL_max(json_int(component, "slices", json_int(component, "segments", primitive->slices)), 3);
    primitive->rings = SDL_max(json_int(component, "rings", primitive->rings), 3);
    primitive->tube_segments = SDL_max(json_int(component, "tube_segments", primitive->tube_segments), 3);
    if (primitive->mesh_primitive == SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE)
        primitive->radius_top = json_float(component, "radius_top", 0.0f);
}

static const slayer3d_sector *active_scene_sector_for_position(const slayer3d_game_data_runtime *runtime,
                                                               slayer3d_vec3 position)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *instances = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "sector_levels");
    if (!yyjson_is_arr(instances))
        return NULL;

    for (size_t i = 0; i < yyjson_arr_size(instances); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(instances, i);
        if (!scene_state_bool(runtime, json_string(entry, "sector_lighting_key", NULL),
                              json_bool(entry, "sector_lighting", true)))
        {
            continue;
        }
        const sector_level_runtime *level = find_sector_level_runtime(runtime, json_string(entry, "level", NULL));
        if (level == NULL)
            continue;
        const slayer3d_vec3 origin = json_vec3(entry, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        const slayer3d_vec3 local =
            slayer3d_vec3_make(position.x - origin.x, position.y - origin.y, position.z - origin.z);
        const int sector_index =
            slayer3d_level_find_sector_at(&level->lightmapped, level->sectors, local.x, local.z, local.y);
        if (sector_index >= 0 && sector_index < level->sector_count && level->sectors[sector_index].has_lighting)
            return &level->sectors[sector_index];
    }
    return NULL;
}

static void apply_sector_lighting_to_render_primitive(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_render_primitive *primitive)
{
    if (runtime == NULL || primitive == NULL || !primitive->lighting_enabled || primitive->instances != NULL)
        return;
    modulate_color_by_sector_lighting(&primitive->color,
                                      active_scene_sector_for_position(runtime, primitive->position));
}

static bool emit_actor_render_primitives(const slayer3d_game_data_runtime *runtime,
                                         const slayer3d_game_data_render_eval *eval,
                                         const slayer3d_registered_actor *actor, yyjson_val *components,
                                         slayer3d_game_data_render_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || actor == NULL || callback == NULL || !yyjson_is_arr(components))
        return true;

    for (size_t c = 0; c < yyjson_arr_size(components); ++c)
    {
        yyjson_val *component = yyjson_arr_get(components, c);
        const char *type = json_string(component, "type", "");
        slayer3d_game_data_render_primitive primitive;
        SDL_zero(primitive);
        primitive.entity_name = actor->name;
        primitive.position = actor->position;
        const slayer3d_vec3 offset = json_vec3(component, "offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        primitive.position.x += offset.x;
        primitive.position.y += offset.y;
        primitive.position.z += offset.z;
        primitive.rotation_axis = json_vec3(component, "rotation_axis", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        primitive.rotation_angle = json_float(component, "rotation_angle", 0.0f);
        const char *rotation_property = json_string(component, "rotation_property", NULL);
        if (rotation_property != NULL)
            primitive.rotation_angle += slayer3d_properties_get_float(actor->props, rotation_property, 0.0f);
        primitive.color = json_color(component, "color", (slayer3d_color){255, 255, 255, 255});
        primitive.texture_image = json_string(component, "texture", NULL);
        primitive.lighting_enabled = render_component_lighting_enabled(runtime, component, true);
        primitive.emissive = json_bool(component, "emissive", false);
        primitive.emissive_color =
            primitive.emissive ? slayer3d_vec3_make(0.2f, 0.2f, 0.2f) : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        primitive.wire_color = json_color(component, "wire_color", (slayer3d_color){0, 0, 0, 255});
        primitive.size = slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
        primitive.radius = 0.5f;
        primitive.height = primitive.size.y;
        primitive.radius_top = primitive.radius;
        primitive.radius_bottom = primitive.radius;
        primitive.major_radius = 0.5f;
        primitive.minor_radius = 0.15f;
        primitive.bevel_radius = 0.15f;
        primitive.arc_angle = 3.1415927f;
        primitive.slices = 24;
        primitive.rings = 8;
        primitive.tube_segments = primitive.rings;

        if (SDL_strcmp(type, "render.cube") == 0)
        {
            primitive.type = SLAYER3D_GAME_DATA_RENDER_CUBE;
            primitive.size = json_vec3(component, "size", slayer3d_vec3_make(1.0f, 1.0f, 1.0f));
            const char *size_property = json_string(component, "size_property", NULL);
            if (size_property != NULL)
                primitive.size = slayer3d_properties_get_vec3(actor->props, size_property, primitive.size);
        }
        else if (SDL_strcmp(type, "render.sphere") == 0)
        {
            primitive.type = SLAYER3D_GAME_DATA_RENDER_SPHERE;
            primitive.radius = json_float(component, "radius", 0.5f);
            primitive.slices = json_int(component, "slices", 16);
            primitive.rings = json_int(component, "rings", 8);
        }
        else if (SDL_strcmp(type, "render.mesh_primitive") == 0)
        {
            populate_mesh_primitive_descriptor(actor, component, &primitive);
        }
        else if (SDL_strcmp(type, "render.composite") == 0)
        {
            yyjson_val *parts = obj_get(component, "parts");
            if (!yyjson_is_arr(parts))
                continue;
            for (size_t p = 0; p < yyjson_arr_size(parts); ++p)
            {
                yyjson_val *part = yyjson_arr_get(parts, p);
                slayer3d_game_data_render_primitive part_primitive = primitive;
                const slayer3d_vec3 part_offset = json_vec3(part, "offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
                part_primitive.position.x += part_offset.x;
                part_primitive.position.y += part_offset.y;
                part_primitive.position.z += part_offset.z;
                part_primitive.rotation_axis = json_vec3(part, "rotation_axis", part_primitive.rotation_axis);
                part_primitive.rotation_angle = json_float(part, "rotation_angle", part_primitive.rotation_angle);
                const char *part_rotation_property = json_string(part, "rotation_property", NULL);
                if (part_rotation_property != NULL)
                    part_primitive.rotation_angle +=
                        slayer3d_properties_get_float(actor->props, part_rotation_property, 0.0f);
                part_primitive.color = json_color(part, "color", part_primitive.color);
                part_primitive.texture_image = json_string(part, "texture", part_primitive.texture_image);
                part_primitive.lighting_enabled =
                    render_component_lighting_enabled(runtime, part, part_primitive.lighting_enabled);
                part_primitive.emissive = json_bool(part, "emissive", part_primitive.emissive);
                part_primitive.emissive_color =
                    part_primitive.emissive ? slayer3d_vec3_make(0.2f, 0.2f, 0.2f) : part_primitive.emissive_color;
                part_primitive.wire_color = json_color(part, "wire_color", part_primitive.wire_color);
                populate_mesh_primitive_descriptor(actor, part, &part_primitive);
                if (eval != NULL)
                {
                    apply_render_effects(runtime, component, eval, &part_primitive);
                    apply_render_effects(runtime, part, eval, &part_primitive);
                }
                apply_sector_lighting_to_render_primitive(runtime, &part_primitive);
                if (!callback(userdata, &part_primitive))
                    return false;
            }
            continue;
        }
        else if (SDL_strcmp(type, "render.sprite") == 0)
        {
            primitive.type = SLAYER3D_GAME_DATA_RENDER_SPRITE;
            primitive.sprite_asset = json_string(component, "sprite", NULL);
            (void)json_vec2_value(obj_get(component, "size"), 1.0f, 1.0f, &primitive.sprite_size.x,
                                  &primitive.sprite_size.y);
            primitive.sprite_facing_yaw = json_float(component, "facing_yaw", 0.0f);
            const char *facing_yaw_property = json_string(component, "facing_yaw_property", NULL);
            if (facing_yaw_property != NULL)
                primitive.sprite_facing_yaw += slayer3d_properties_get_float(actor->props, facing_yaw_property, 0.0f);
        }
        else if (SDL_strcmp(type, "render.model") == 0)
        {
            primitive.type = SLAYER3D_GAME_DATA_RENDER_MODEL;
            primitive.model_asset = json_string(component, "model", NULL);
            primitive.model_scale = json_vec3(component, "scale", slayer3d_vec3_make(1.0f, 1.0f, 1.0f));
            primitive.animation_clip = json_int(component, "animation_clip", -1);
            primitive.animation_time = json_float(component, "animation_time", 0.0f);
            primitive.animation_loop = json_bool(component, "animation_loop", true);
            const char *animation_time_property = json_string(component, "animation_time_property", NULL);
            if (animation_time_property != NULL)
                primitive.animation_time =
                    slayer3d_properties_get_float(actor->props, animation_time_property, primitive.animation_time);
        }
        else
        {
            continue;
        }

        if (eval != NULL)
            apply_render_effects(runtime, component, eval, &primitive);
        apply_sector_lighting_to_render_primitive(runtime, &primitive);
        if (!callback(userdata, &primitive))
            return false;
    }

    return true;
}

static bool emit_grid_pickup_layer_render_primitives(const slayer3d_game_data_runtime *runtime,
                                                     grid_pickup_layer_runtime *layer,
                                                     slayer3d_game_data_render_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || layer == NULL || layer->map == NULL || layer->cells == NULL || callback == NULL ||
        layer->active_count <= 0)
    {
        return true;
    }

    if (layer->render_position_capacity < layer->active_count)
    {
        slayer3d_vec3 *positions =
            (slayer3d_vec3 *)SDL_realloc(layer->render_positions, (size_t)layer->active_count * sizeof(*positions));
        if (positions == NULL)
            return false;
        layer->render_positions = positions;
        layer->render_position_capacity = layer->active_count;
    }

    for (int kind_index = 0; kind_index < layer->kind_count; ++kind_index)
    {
        const grid_pickup_kind_runtime *kind = &layer->kinds[kind_index];
        int count = 0;
        for (int row = 0; row < layer->map->height; ++row)
        {
            for (int col = 0; col < layer->map->width; ++col)
            {
                if (layer->cells[row * layer->map->width + col] != (Uint8)(kind_index + 1))
                    continue;
                slayer3d_vec3 position;
                if (!grid_map_cell_to_world(layer->map, col, row, &position))
                    continue;
                position.z = kind->z;
                layer->render_positions[count++] = position;
            }
        }
        if (count <= 0)
            continue;

        slayer3d_game_data_render_primitive primitive;
        SDL_zero(primitive);
        primitive.entity_name = layer->name;
        primitive.type = SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH;
        primitive.radius = kind->radius;
        primitive.rings = kind->rings;
        primitive.slices = kind->slices;
        primitive.color = kind->color;
        primitive.lighting_enabled = kind->lighting;
        primitive.emissive = kind->emissive;
        primitive.emissive_color =
            kind->emissive ? slayer3d_vec3_make(0.25f, 0.22f, 0.08f) : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        primitive.instances = layer->render_positions;
        primitive.instance_count = count;
        if (!callback(userdata, &primitive))
            return false;
    }
    return true;
}

static bool emit_sector_door_render_primitives(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_render_eval *eval,
                                               slayer3d_game_data_render_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return true;

    for (int door_index = 0; door_index < runtime->sector_door_count; ++door_index)
    {
        const sector_door_runtime *door = &runtime->sector_doors[door_index];
        if (!sector_door_in_active_scene(runtime, door))
            continue;

        yyjson_val *render = obj_get(door->json, "render");
        for (int panel_index = 0; panel_index < slayer3d_door_panel_count(&door->door); ++panel_index)
        {
            slayer3d_bounding_box bounds;
            if (!slayer3d_door_get_panel_bounds(&door->door, panel_index, &bounds))
                continue;

            slayer3d_game_data_render_primitive primitive;
            SDL_zero(primitive);
            primitive.entity_name = door->door.name;
            primitive.type = SLAYER3D_GAME_DATA_RENDER_CUBE;
            primitive.position =
                slayer3d_vec3_make((bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f,
                                   (bounds.min.z + bounds.max.z) * 0.5f);
            primitive.size = slayer3d_vec3_make(SDL_max(bounds.max.x - bounds.min.x, 0.001f),
                                                SDL_max(bounds.max.y - bounds.min.y, 0.001f),
                                                SDL_max(bounds.max.z - bounds.min.z, 0.001f));
            primitive.color = json_color(render, "color", (slayer3d_color){170, 185, 205, 255});
            primitive.texture_image = json_string(render, "texture", NULL);
            primitive.lighting_enabled = render_component_lighting_enabled(runtime, render, true);
            primitive.emissive = json_bool(render, "emissive", false);
            primitive.emissive_color =
                primitive.emissive ? json_vec3(render, "emissive_color", slayer3d_vec3_make(0.12f, 0.15f, 0.18f))
                                   : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
            if (eval != NULL)
                apply_render_effects(runtime, render, eval, &primitive);
            apply_sector_lighting_to_render_primitive(runtime, &primitive);
            if (!callback(userdata, &primitive))
                return false;
        }
    }
    return true;
}

static bool for_each_render_primitive_internal(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_render_eval *eval,
                                               slayer3d_game_data_render_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)runtime;
    actor_lifecycle_defer_begin(mutable_runtime);
    bool keep_iterating = true;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; keep_iterating && yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (actor == NULL || !actor->active)
            continue;
        keep_iterating =
            emit_actor_render_primitives(runtime, eval, actor, obj_get(entity, "components"), callback, userdata);
    }
    for (int pool_index = 0; keep_iterating && pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
            continue;
        yyjson_val *components = obj_get(pool->archetype_json, "components");
        for (int actor_index = 0; keep_iterating && actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (!actor_pool_actor_is_active(pool, actor, actor_index))
                continue;
            keep_iterating = emit_actor_render_primitives(runtime, eval, actor, components, callback, userdata);
        }
    }
    for (int layer_index = 0; keep_iterating && layer_index < runtime->grid_pickup_layer_count; ++layer_index)
    {
        keep_iterating = emit_grid_pickup_layer_render_primitives(
            runtime, &mutable_runtime->grid_pickup_layers[layer_index], callback, userdata);
    }
    if (keep_iterating)
        keep_iterating = emit_sector_door_render_primitives(runtime, eval, callback, userdata);
    actor_lifecycle_defer_end(mutable_runtime);
    return true;
}

bool slayer3d_game_data_for_each_render_primitive(const slayer3d_game_data_runtime *runtime,
                                                  slayer3d_game_data_render_primitive_fn callback, void *userdata)
{
    return for_each_render_primitive_internal(runtime, NULL, callback, userdata);
}

bool slayer3d_game_data_for_each_render_primitive_evaluated(const slayer3d_game_data_runtime *runtime,
                                                            const slayer3d_game_data_render_eval *eval,
                                                            slayer3d_game_data_render_primitive_fn callback,
                                                            void *userdata)
{
    return for_each_render_primitive_internal(runtime, eval, callback, userdata);
}

bool slayer3d_game_data_get_particle_emitter(const slayer3d_game_data_runtime *runtime, const char *entity_name,
                                             slayer3d_particle_config *out_config)
{
    if (out_config != NULL)
        SDL_zero(*out_config);
    if (runtime == NULL || entity_name == NULL || out_config == NULL)
        return false;

    yyjson_val *entity = find_actor_definition_json(runtime, entity_name);
    yyjson_val *component = find_component_json(entity, "particles.emitter");
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
    if (component == NULL || actor == NULL)
        return false;

    out_config->position = actor->position;
    out_config->direction = json_vec3(component, "direction", slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    out_config->spread = json_float(component, "spread", 0.0f);
    out_config->speed_min = json_float(component, "speed_min", 0.0f);
    out_config->speed_max = json_float(component, "speed_max", 0.0f);
    out_config->lifetime_min = json_float(component, "lifetime_min", 1.0f);
    out_config->lifetime_max = json_float(component, "lifetime_max", 1.0f);
    out_config->size_start = json_float(component, "size_start", 0.05f);
    out_config->size_end = json_float(component, "size_end", 0.01f);
    out_config->color_start = json_color(component, "color_start", (slayer3d_color){255, 255, 255, 255});
    out_config->color_end = json_color(component, "color_end", (slayer3d_color){255, 255, 255, 0});
    out_config->gravity = json_float(component, "gravity", 0.0f);
    out_config->max_particles = json_int(component, "max_particles", 128);
    out_config->emit_rate = json_float(component, "emit_rate", 0.0f);
    const char *shape = json_string(component, "shape", "point");
    if (SDL_strcmp(shape, "box") == 0)
        out_config->shape = SLAYER3D_PARTICLE_EMITTER_BOX;
    else if (SDL_strcmp(shape, "circle") == 0)
        out_config->shape = SLAYER3D_PARTICLE_EMITTER_CIRCLE;
    else
        out_config->shape = SLAYER3D_PARTICLE_EMITTER_POINT;
    out_config->extents = json_vec3(component, "extents", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    out_config->radius = json_float(component, "radius", 0.0f);
    out_config->emissive_intensity = json_float(component, "emissive_intensity", 1.0f);
    out_config->camera_facing = json_bool(component, "camera_facing", true);
    out_config->depth_test = json_bool(component, "depth_test", true);
    out_config->additive_blend = json_bool(component, "additive_blend", false);
    out_config->texture = NULL;
    out_config->random_seed = (Uint32)json_int(component, "random_seed", 0);
    return true;
}

bool slayer3d_game_data_get_particle_emitter_draw_emissive(const slayer3d_game_data_runtime *runtime,
                                                           const char *entity_name, slayer3d_vec3 *out_rgb)
{
    if (out_rgb != NULL)
        *out_rgb = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (runtime == NULL || entity_name == NULL || out_rgb == NULL)
        return false;

    yyjson_val *entity = find_actor_definition_json(runtime, entity_name);
    yyjson_val *component = find_component_json(entity, "particles.emitter");
    if (component == NULL)
        return false;

    *out_rgb = json_vec3(component, "draw_emissive", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return true;
}

bool slayer3d_game_data_for_each_particle_emitter(const slayer3d_game_data_runtime *runtime,
                                                  slayer3d_game_data_particle_emitter_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)runtime;
    actor_lifecycle_defer_begin(mutable_runtime);
    bool keep_iterating = true;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; keep_iterating && yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;

        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        yyjson_val *component = find_component_json(entity, "particles.emitter");
        if (actor == NULL || !actor->active || component == NULL)
            continue;

        slayer3d_game_data_particle_emitter emitter;
        SDL_zero(emitter);
        emitter.entity_name = entity_name;
        if (!slayer3d_game_data_get_particle_emitter(runtime, entity_name, &emitter.config))
            continue;
        (void)slayer3d_game_data_get_particle_emitter_draw_emissive(runtime, entity_name, &emitter.draw_emissive);

        if (!callback(userdata, &emitter))
            keep_iterating = false;
    }
    for (int pool_index = 0; keep_iterating && pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)) ||
            find_component_json(pool->archetype_json, "particles.emitter") == NULL)
        {
            continue;
        }

        for (int actor_index = 0; keep_iterating && actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (!actor_pool_actor_is_active(pool, actor, actor_index))
                continue;

            slayer3d_game_data_particle_emitter emitter;
            SDL_zero(emitter);
            emitter.entity_name = actor->name;
            if (!slayer3d_game_data_get_particle_emitter(runtime, actor->name, &emitter.config))
                continue;
            (void)slayer3d_game_data_get_particle_emitter_draw_emissive(runtime, actor->name, &emitter.draw_emissive);

            if (!callback(userdata, &emitter))
                keep_iterating = false;
        }
    }
    actor_lifecycle_defer_end(mutable_runtime);
    return true;
}

bool slayer3d_game_data_get_render_settings(const slayer3d_game_data_runtime *runtime,
                                            slayer3d_game_data_render_settings *out_settings)
{
    if (out_settings != NULL)
    {
        SDL_zero(*out_settings);
        out_settings->clear_color = (slayer3d_color){0, 0, 0, 255};
        out_settings->lighting_enabled = true;
        out_settings->bloom_enabled = true;
        out_settings->ssao_enabled = true;
        out_settings->tonemap = SLAYER3D_TONEMAP_ACES;
    }
    if (runtime == NULL || out_settings == NULL)
        return false;

    yyjson_val *render = obj_get(runtime_root(runtime), "render");
    if (!yyjson_is_obj(render))
        return true;

    out_settings->clear_color = json_color(render, "clear_color", out_settings->clear_color);
    out_settings->lighting_enabled = scene_state_bool(runtime, json_string(render, "lighting_key", NULL),
                                                      json_bool(render, "lighting", out_settings->lighting_enabled));
    out_settings->bloom_enabled = scene_state_bool(runtime, json_string(render, "bloom_key", NULL),
                                                   json_bool(render, "bloom", out_settings->bloom_enabled));
    out_settings->ssao_enabled = scene_state_bool(runtime, json_string(render, "ssao_key", NULL),
                                                  json_bool(render, "ssao", out_settings->ssao_enabled));
    const char *tonemap_name =
        scene_state_string(runtime, json_string(render, "tonemap_key", NULL), json_string(render, "tonemap", NULL));
    out_settings->tonemap = parse_tonemap(tonemap_name, out_settings->tonemap);

    const char *profile_name =
        scene_state_string(runtime, json_string(render, "profile_key", NULL), json_string(render, "profile", NULL));
    slayer3d_render_profile profile;
    if (parse_render_profile(profile_name, &profile))
    {
        out_settings->has_profile = true;
        out_settings->profile = profile;
        out_settings->profile_name = profile_name;
        if (tonemap_name == NULL)
            out_settings->tonemap = profile.tonemap;
    }
    return true;
}

bool slayer3d_game_data_get_transition(const slayer3d_game_data_runtime *runtime, const char *name,
                                       slayer3d_game_data_transition_desc *out_transition)
{
    if (out_transition != NULL)
    {
        SDL_zero(*out_transition);
        out_transition->type = SLAYER3D_TRANSITION_FADE;
        out_transition->direction = SLAYER3D_TRANSITION_IN;
        out_transition->color = (slayer3d_color){0, 0, 0, 255};
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
        done_signal != NULL ? slayer3d_game_data_find_signal(runtime, done_signal) : out_transition->done_signal_id;
    return true;
}

const char *slayer3d_game_data_active_scene(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return scene != NULL ? scene->name : NULL;
}

int slayer3d_game_data_scene_count(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_count : 0;
}

const char *slayer3d_game_data_scene_name_at(const slayer3d_game_data_runtime *runtime, int index)
{
    if (runtime == NULL || index < 0 || index >= runtime->scene_count)
        return NULL;
    return runtime->scenes[index].name;
}

slayer3d_properties *slayer3d_game_data_mutable_scene_state(slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_state : NULL;
}

const slayer3d_properties *slayer3d_game_data_scene_state(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_state : NULL;
}

static sector_level_runtime *find_sector_level_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->sector_level_count; ++i)
    {
        sector_level_runtime *level = &runtime->sector_levels[i];
        if (level->name != NULL && SDL_strcmp(level->name, name) == 0)
            return level;
    }
    return NULL;
}

static const sector_level_runtime *find_sector_level_runtime(const slayer3d_game_data_runtime *runtime,
                                                             const char *name)
{
    return find_sector_level_runtime_mutable((slayer3d_game_data_runtime *)runtime, name);
}

static int sector_level_find_sector_name(const sector_level_runtime *level, const char *sector_name)
{
    if (level == NULL || sector_name == NULL)
        return -1;
    for (int i = 0; i < level->sector_count; ++i)
    {
        if (level->sector_names[i] != NULL && SDL_strcmp(level->sector_names[i], sector_name) == 0)
            return i;
    }
    return -1;
}

static int sector_level_resolve_sector_index(const sector_level_runtime *level, const char *sector)
{
    if (level == NULL || sector == NULL || sector[0] == '\0')
        return -1;
    const int named_index = sector_level_find_sector_name(level, sector);
    if (named_index >= 0)
        return named_index;

    char *end = NULL;
    const long parsed = SDL_strtol(sector, &end, 10);
    if (end != NULL && *end == '\0' && parsed >= 0 && parsed < level->sector_count)
        return (int)parsed;
    return -1;
}

static float clamp01(float value)
{
    if (value <= 0.0f)
        return 0.0f;
    if (value >= 1.0f)
        return 1.0f;
    return value;
}

static void sector_lighting_rgb(const slayer3d_sector *sector, float out_rgb[3])
{
    if (out_rgb == NULL)
        return;
    out_rgb[0] = 1.0f;
    out_rgb[1] = 1.0f;
    out_rgb[2] = 1.0f;
    if (sector == NULL || !sector->has_lighting)
        return;

    const float level = SDL_clamp(sector->lighting_level, 0.0f, 255.0f) / 255.0f;
    const float influence = clamp01(sector->lighting_color[3]);
    for (int i = 0; i < 3; ++i)
    {
        const float tint = 1.0f + (clamp01(sector->lighting_color[i]) - 1.0f) * influence;
        out_rgb[i] = level * tint;
    }
}

static Uint8 color_channel_from_float(float value)
{
    value = SDL_clamp(value, 0.0f, 255.0f);
    return (Uint8)(value + 0.5f);
}

static void modulate_color_by_sector_lighting(slayer3d_color *color, const slayer3d_sector *sector)
{
    if (color == NULL || sector == NULL || !sector->has_lighting)
        return;
    float lighting[3];
    sector_lighting_rgb(sector, lighting);
    color->r = color_channel_from_float((float)color->r * lighting[0]);
    color->g = color_channel_from_float((float)color->g * lighting[1]);
    color->b = color_channel_from_float((float)color->b * lighting[2]);
}

static bool rebuild_sector_level_variants_atomic(sector_level_runtime *level, char *error_buffer, int error_buffer_size)
{
    if (level == NULL)
    {
        set_error(error_buffer, error_buffer_size, "sector level is invalid");
        return false;
    }

    slayer3d_level lightmapped;
    slayer3d_level vertex_baked;
    slayer3d_level unlit;
    slayer3d_level lightmapped_without_sector_lighting;
    slayer3d_level vertex_baked_without_sector_lighting;
    slayer3d_level unlit_without_sector_lighting;
    SDL_zero(lightmapped);
    SDL_zero(vertex_baked);
    SDL_zero(unlit);
    SDL_zero(lightmapped_without_sector_lighting);
    SDL_zero(vertex_baked_without_sector_lighting);
    SDL_zero(unlit_without_sector_lighting);

    bool ok = true;
    if (!build_sector_level_variant_set(level, level->sectors, &lightmapped, &vertex_baked, &unlit, "sector-lit",
                                        error_buffer, error_buffer_size))
    {
        ok = false;
    }
    slayer3d_sector *unlit_sectors = NULL;
    if (ok)
    {
        unlit_sectors = copy_sectors_without_sector_lighting(level);
        if (unlit_sectors == NULL && level->sector_count > 0)
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate sector lighting toggle rebuild variants");
            ok = false;
        }
    }
    if (ok && !build_sector_level_variant_set(level, unlit_sectors != NULL ? unlit_sectors : level->sectors,
                                              &lightmapped_without_sector_lighting,
                                              &vertex_baked_without_sector_lighting, &unlit_without_sector_lighting,
                                              "sector-neutral", error_buffer, error_buffer_size))
    {
        ok = false;
    }
    SDL_free(unlit_sectors);

    if (!ok)
    {
        slayer3d_free_level(&lightmapped);
        slayer3d_free_level(&vertex_baked);
        slayer3d_free_level(&unlit);
        slayer3d_free_level(&lightmapped_without_sector_lighting);
        slayer3d_free_level(&vertex_baked_without_sector_lighting);
        slayer3d_free_level(&unlit_without_sector_lighting);
        return false;
    }

    slayer3d_free_level(&level->lightmapped);
    slayer3d_free_level(&level->vertex_baked);
    slayer3d_free_level(&level->unlit);
    slayer3d_free_level(&level->lightmapped_without_sector_lighting);
    slayer3d_free_level(&level->vertex_baked_without_sector_lighting);
    slayer3d_free_level(&level->unlit_without_sector_lighting);
    level->lightmapped = lightmapped;
    level->vertex_baked = vertex_baked;
    level->unlit = unlit;
    level->lightmapped_without_sector_lighting = lightmapped_without_sector_lighting;
    level->vertex_baked_without_sector_lighting = vertex_baked_without_sector_lighting;
    level->unlit_without_sector_lighting = unlit_without_sector_lighting;
    return true;
}

bool slayer3d_game_data_get_sector_lighting(const slayer3d_game_data_runtime *runtime, const char *sector_level,
                                            const char *sector, float *out_level, float out_color[4],
                                            char *error_buffer, int error_buffer_size)
{
    if (out_level != NULL)
        *out_level = 0.0f;
    if (out_color != NULL)
        SDL_memset(out_color, 0, sizeof(float) * 4U);
    const sector_level_runtime *level = find_sector_level_runtime(runtime, sector_level);
    const int sector_index = sector_level_resolve_sector_index(level, sector);
    if (level == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' not found",
                   sector_level != NULL ? sector_level : "<null>");
        return false;
    }
    if (sector_index < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "sector '%s' not found", sector != NULL ? sector : "<null>");
        return false;
    }

    const slayer3d_sector *resolved = &level->sectors[sector_index];
    if (!resolved->has_lighting)
    {
        set_errorf(error_buffer, error_buffer_size, "sector '%s' has no authored lighting", sector);
        return false;
    }
    if (out_level == NULL || out_color == NULL)
    {
        set_error(error_buffer, error_buffer_size, "sector lighting output pointers are required");
        return false;
    }
    *out_level = resolved->lighting_level;
    SDL_memcpy(out_color, resolved->lighting_color, sizeof(resolved->lighting_color));
    return true;
}

bool slayer3d_game_data_set_sector_lighting(slayer3d_game_data_runtime *runtime, const char *sector_level,
                                            const char *sector, float level, const float color[4], char *error_buffer,
                                            int error_buffer_size)
{
    sector_level_runtime *resolved_level = find_sector_level_runtime_mutable(runtime, sector_level);
    const int sector_index = sector_level_resolve_sector_index(resolved_level, sector);
    if (resolved_level == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' not found",
                   sector_level != NULL ? sector_level : "<null>");
        return false;
    }
    if (sector_index < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "sector '%s' not found", sector != NULL ? sector : "<null>");
        return false;
    }
    if (color == NULL)
    {
        set_error(error_buffer, error_buffer_size, "sector lighting color is required");
        return false;
    }

    slayer3d_sector *target = &resolved_level->sectors[sector_index];
    const bool old_has_lighting = target->has_lighting;
    const float old_level = target->lighting_level;
    float old_color[4];
    SDL_memcpy(old_color, target->lighting_color, sizeof(old_color));

    target->has_lighting = true;
    target->lighting_level = SDL_clamp(level, 0.0f, 255.0f);
    for (int i = 0; i < 4; ++i)
        target->lighting_color[i] = clamp01(color[i]);

    if (!rebuild_sector_level_variants_atomic(resolved_level, error_buffer, error_buffer_size))
    {
        target->has_lighting = old_has_lighting;
        target->lighting_level = old_level;
        SDL_memcpy(target->lighting_color, old_color, sizeof(target->lighting_color));
        (void)rebuild_sector_level_variants_atomic(resolved_level, NULL, 0);
        return false;
    }
    return true;
}

static yyjson_val *find_sector_navigation_graph(const slayer3d_game_data_runtime *runtime, const char *graph_name)
{
    yyjson_val *graphs = obj_get(runtime_root(runtime), "sector_navigation");
    for (size_t i = 0; graph_name != NULL && yyjson_is_arr(graphs) && i < yyjson_arr_size(graphs); ++i)
    {
        yyjson_val *graph = yyjson_arr_get(graphs, i);
        const char *name = json_string(graph, "name", NULL);
        if (name != NULL && SDL_strcmp(name, graph_name) == 0)
            return graph;
    }
    return NULL;
}

static const sector_level_runtime *sector_navigation_level(const slayer3d_game_data_runtime *runtime, yyjson_val *graph)
{
    return find_sector_level_runtime(runtime, json_string(graph, "sector_level", NULL));
}

static int sector_navigation_node_count(yyjson_val *graph)
{
    yyjson_val *nodes = obj_get(graph, "nodes");
    return yyjson_is_arr(nodes) ? (int)yyjson_arr_size(nodes) : 0;
}

static yyjson_val *sector_navigation_node_at(yyjson_val *graph, int index)
{
    yyjson_val *nodes = obj_get(graph, "nodes");
    return index >= 0 && yyjson_is_arr(nodes) && index < (int)yyjson_arr_size(nodes)
               ? yyjson_arr_get(nodes, (size_t)index)
               : NULL;
}

static int sector_navigation_node_sector_index(const sector_level_runtime *level, yyjson_val *node)
{
    if (level == NULL || node == NULL)
        return -1;
    const int authored_index = json_int(node, "sector_index", -1);
    if (authored_index >= 0 && authored_index < level->sector_count)
        return authored_index;
    return sector_level_find_sector_name(level, json_string(node, "sector", NULL));
}

static bool sector_navigation_read_node(const slayer3d_game_data_runtime *runtime, yyjson_val *graph, int index,
                                        slayer3d_game_data_sector_nav_node *out_node)
{
    const sector_level_runtime *level = sector_navigation_level(runtime, graph);
    yyjson_val *node = sector_navigation_node_at(graph, index);
    if (level == NULL || node == NULL || out_node == NULL)
        return false;

    out_node->name = json_string(node, "name", NULL);
    out_node->sector_index = sector_navigation_node_sector_index(level, node);
    out_node->position = json_vec3(node, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return out_node->name != NULL && out_node->sector_index >= 0;
}

static int sector_navigation_find_node_index(yyjson_val *graph, const char *node_name)
{
    const int node_count = sector_navigation_node_count(graph);
    for (int i = 0; node_name != NULL && i < node_count; ++i)
    {
        yyjson_val *node = sector_navigation_node_at(graph, i);
        const char *name = json_string(node, "name", NULL);
        if (name != NULL && SDL_strcmp(name, node_name) == 0)
            return i;
    }
    return -1;
}

static float sector_navigation_distance_squared(slayer3d_vec3 a, slayer3d_vec3 b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

static float sector_navigation_link_default_cost(const slayer3d_game_data_runtime *runtime, yyjson_val *graph, int from,
                                                 int to)
{
    slayer3d_game_data_sector_nav_node from_node;
    slayer3d_game_data_sector_nav_node to_node;
    if (!sector_navigation_read_node(runtime, graph, from, &from_node) ||
        !sector_navigation_read_node(runtime, graph, to, &to_node))
    {
        return 1.0f;
    }
    return SDL_sqrtf(sector_navigation_distance_squared(from_node.position, to_node.position));
}

static int sector_navigation_nearest_index(const slayer3d_game_data_runtime *runtime, yyjson_val *graph,
                                           slayer3d_vec3 position)
{
    const sector_level_runtime *level = sector_navigation_level(runtime, graph);
    const int node_count = sector_navigation_node_count(graph);
    if (level == NULL || node_count <= 0)
        return -1;

    const int containing_sector =
        slayer3d_level_find_sector_at(&level->lightmapped, level->sectors, position.x, position.z, position.y);
    int best_index = -1;
    float best_distance = 1.0e30f;
    for (int pass = 0; pass < 2 && best_index < 0; ++pass)
    {
        const bool same_sector_only = pass == 0 && containing_sector >= 0;
        for (int i = 0; i < node_count; ++i)
        {
            slayer3d_game_data_sector_nav_node node;
            if (!sector_navigation_read_node(runtime, graph, i, &node))
                continue;
            if (same_sector_only && node.sector_index != containing_sector)
                continue;
            const float distance = sector_navigation_distance_squared(position, node.position);
            if (best_index < 0 || distance < best_distance)
            {
                best_index = i;
                best_distance = distance;
            }
        }
    }
    return best_index;
}

bool slayer3d_game_data_sector_nav_nearest_node(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                                slayer3d_vec3 position, slayer3d_game_data_sector_nav_node *out_node)
{
    yyjson_val *graph = find_sector_navigation_graph(runtime, graph_name);
    const int index = sector_navigation_nearest_index(runtime, graph, position);
    return index >= 0 && sector_navigation_read_node(runtime, graph, index, out_node);
}

bool slayer3d_game_data_sector_nav_path(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                        slayer3d_vec3 start, slayer3d_vec3 goal,
                                        slayer3d_game_data_sector_nav_node *out_nodes, int max_nodes,
                                        int *out_node_count, float *out_cost)
{
    if (out_node_count != NULL)
        *out_node_count = 0;
    if (out_cost != NULL)
        *out_cost = 0.0f;

    yyjson_val *graph = find_sector_navigation_graph(runtime, graph_name);
    const int node_count = sector_navigation_node_count(graph);
    const int start_index = sector_navigation_nearest_index(runtime, graph, start);
    const int goal_index = sector_navigation_nearest_index(runtime, graph, goal);
    if (graph == NULL || node_count <= 0 || start_index < 0 || goal_index < 0)
        return false;

    float *distances = (float *)SDL_malloc(sizeof(float) * (size_t)node_count);
    int *previous = (int *)SDL_malloc(sizeof(int) * (size_t)node_count);
    bool *visited = (bool *)SDL_calloc((size_t)node_count, sizeof(bool));
    int *reverse_path = (int *)SDL_malloc(sizeof(int) * (size_t)node_count);
    if (distances == NULL || previous == NULL || visited == NULL || reverse_path == NULL)
    {
        SDL_free(distances);
        SDL_free(previous);
        SDL_free(visited);
        SDL_free(reverse_path);
        return false;
    }

    for (int i = 0; i < node_count; ++i)
    {
        distances[i] = 1.0e30f;
        previous[i] = -1;
    }
    distances[start_index] = 0.0f;

    for (;;)
    {
        int current = -1;
        float current_distance = 1.0e30f;
        for (int i = 0; i < node_count; ++i)
        {
            if (!visited[i] && distances[i] < current_distance)
            {
                current = i;
                current_distance = distances[i];
            }
        }
        if (current < 0 || current == goal_index)
            break;
        visited[current] = true;

        yyjson_val *links = obj_get(graph, "links");
        for (size_t link_index = 0; yyjson_is_arr(links) && link_index < yyjson_arr_size(links); ++link_index)
        {
            yyjson_val *link = yyjson_arr_get(links, link_index);
            const int from = sector_navigation_find_node_index(graph, json_string(link, "from", NULL));
            const int to = sector_navigation_find_node_index(graph, json_string(link, "to", NULL));
            const bool bidirectional = json_bool(link, "bidirectional", true);
            int neighbor = -1;
            if (from == current)
                neighbor = to;
            else if (bidirectional && to == current)
                neighbor = from;
            if (neighbor < 0 || neighbor >= node_count || visited[neighbor])
                continue;

            const float cost = SDL_max(
                json_float(link, "cost", sector_navigation_link_default_cost(runtime, graph, current, neighbor)),
                0.0001f);
            const float candidate = distances[current] + cost;
            if (candidate < distances[neighbor])
            {
                distances[neighbor] = candidate;
                previous[neighbor] = current;
            }
        }
    }

    bool ok = start_index == goal_index || previous[goal_index] >= 0;
    int path_count = 0;
    if (ok)
    {
        for (int node = goal_index; node >= 0 && path_count < node_count; node = previous[node])
            reverse_path[path_count++] = node;
        if (path_count <= 0 || reverse_path[path_count - 1] != start_index)
            ok = false;
    }

    if (ok)
    {
        if (out_node_count != NULL)
            *out_node_count = path_count;
        if (out_cost != NULL)
            *out_cost = distances[goal_index];
        if (out_nodes != NULL)
        {
            if (max_nodes < path_count)
            {
                ok = false;
            }
            else
            {
                for (int i = 0; i < path_count; ++i)
                {
                    const int source_index = reverse_path[path_count - 1 - i];
                    if (!sector_navigation_read_node(runtime, graph, source_index, &out_nodes[i]))
                    {
                        ok = false;
                        break;
                    }
                }
            }
        }
    }

    SDL_free(distances);
    SDL_free(previous);
    SDL_free(visited);
    SDL_free(reverse_path);
    return ok;
}

bool slayer3d_game_data_sector_nav_path_available(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                                  slayer3d_vec3 start, slayer3d_vec3 goal)
{
    return slayer3d_game_data_sector_nav_path(runtime, graph_name, start, goal, NULL, 0, NULL, NULL);
}

bool slayer3d_game_data_sector_nav_next_node(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                             slayer3d_vec3 start, slayer3d_vec3 goal,
                                             slayer3d_game_data_sector_nav_node *out_node)
{
    int node_count = 0;
    if (!slayer3d_game_data_sector_nav_path(runtime, graph_name, start, goal, NULL, 0, &node_count, NULL) ||
        node_count <= 0)
        return false;

    slayer3d_game_data_sector_nav_node *nodes = (slayer3d_game_data_sector_nav_node *)SDL_malloc(
        sizeof(slayer3d_game_data_sector_nav_node) * (size_t)node_count);
    if (nodes == NULL)
        return false;
    const bool ok =
        slayer3d_game_data_sector_nav_path(runtime, graph_name, start, goal, nodes, node_count, &node_count, NULL);
    if (ok && out_node != NULL)
        *out_node = nodes[node_count > 1 ? 1 : 0];
    SDL_free(nodes);
    return ok;
}

bool slayer3d_game_data_get_sector_level(const slayer3d_game_data_runtime *runtime, const char *name,
                                         slayer3d_game_data_sector_level *out_level)
{
    if (out_level != NULL)
        SDL_zero(*out_level);
    if (runtime == NULL || name == NULL || out_level == NULL)
        return false;

    const sector_level_runtime *level = find_sector_level_runtime(runtime, name);
    if (level == NULL)
        return false;

    out_level->name = level->name;
    out_level->sectors = level->sectors;
    out_level->sector_names = level->sector_names;
    out_level->sector_count = level->sector_count;
    out_level->materials = level->materials;
    out_level->material_count = level->material_count;
    out_level->lights = level->lights;
    out_level->light_count = level->light_count;
    out_level->lightmapped = &level->lightmapped;
    out_level->vertex_baked = &level->vertex_baked;
    out_level->unlit = &level->unlit;
    return true;
}

static const brush_world_runtime *find_brush_world_runtime(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->brush_world_count; ++i)
    {
        const brush_world_runtime *world = &runtime->brush_worlds[i];
        if (world->desc.name != NULL && SDL_strcmp(world->desc.name, name) == 0)
            return world;
    }
    return NULL;
}

bool slayer3d_game_data_get_brush_world(const slayer3d_game_data_runtime *runtime, const char *name,
                                        slayer3d_game_data_brush_world *out_world)
{
    if (out_world != NULL)
        SDL_zero(*out_world);
    if (runtime == NULL || name == NULL || out_world == NULL)
        return false;

    const brush_world_runtime *world = find_brush_world_runtime(runtime, name);
    if (world == NULL)
        return false;

    *out_world = world->desc;
    return true;
}

bool slayer3d_game_data_trace_brush_world(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                          const slayer3d_game_data_brush_trace_desc *desc,
                                          slayer3d_game_data_brush_trace_result *out_result)
{
    if (out_result != NULL)
        *out_result = slayer3d_game_data_brush_trace_default_result(desc);
    if (runtime == NULL || world_name == NULL || desc == NULL || out_result == NULL)
        return false;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
        return false;
    return slayer3d_game_data_brush_world_trace_local_with_diagnostics(
        &world_runtime->desc, desc, true, out_result, &((slayer3d_game_data_runtime *)runtime)->brush_diagnostics);
}

typedef struct brush_scene_trace_context
{
    const slayer3d_game_data_runtime *runtime;
    const slayer3d_game_data_brush_trace_desc *world_desc;
    slayer3d_game_data_brush_trace_result closest;
    bool ok;
} brush_scene_trace_context;

static bool trace_brush_world_instance(void *userdata, const slayer3d_game_data_brush_world_instance *instance)
{
    brush_scene_trace_context *context = (brush_scene_trace_context *)userdata;
    if (context == NULL || context->runtime == NULL || context->world_desc == NULL || instance == NULL ||
        instance->world_name == NULL)
    {
        if (context != NULL)
            context->ok = false;
        return false;
    }

    slayer3d_game_data_brush_trace_desc local_desc = *context->world_desc;
    local_desc.start = slayer3d_vec3_sub(local_desc.start, instance->position);
    local_desc.end = slayer3d_vec3_sub(local_desc.end, instance->position);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(context->runtime, instance->world_name);
    if (world_runtime == NULL)
    {
        context->ok = false;
        return false;
    }
    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)context->runtime;
    ++mutable_runtime->brush_diagnostics.world_instance_count;
    if (instance->acceleration_enabled && world_runtime->desc.has_bounds)
    {
        const slayer3d_bounding_box trace_bounds = slayer3d_game_data_brush_trace_bounds(&local_desc);
        if (!slayer3d_check_aabb_aabb(trace_bounds, world_runtime->desc.bounds))
        {
            ++mutable_runtime->brush_diagnostics.world_bounds_reject_count;
            return true;
        }
    }

    slayer3d_game_data_brush_trace_result local_result;
    if (!slayer3d_game_data_brush_world_trace_local_with_diagnostics(&world_runtime->desc, &local_desc,
                                                                     instance->acceleration_enabled, &local_result,
                                                                     &mutable_runtime->brush_diagnostics))
    {
        context->ok = false;
        return false;
    }

    if (local_result.hit && (!context->closest.hit || local_result.fraction < context->closest.fraction ||
                             (local_result.start_solid && !context->closest.start_solid)))
    {
        local_result.end_position = slayer3d_vec3_add(local_result.end_position, instance->position);
        local_result.point = slayer3d_vec3_add(local_result.point, instance->position);
        context->closest = local_result;
    }
    return true;
}

bool slayer3d_game_data_trace_active_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_brush_trace_desc *desc,
                                                  slayer3d_game_data_brush_trace_result *out_result)
{
    if (out_result != NULL)
        *out_result = slayer3d_game_data_brush_trace_default_result(desc);
    if (runtime == NULL || desc == NULL || out_result == NULL || !slayer3d_game_data_brush_trace_shape_valid(desc))
        return false;

    brush_scene_trace_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.world_desc = desc;
    context.closest = slayer3d_game_data_brush_trace_default_result(desc);
    context.ok = true;
    if (!slayer3d_game_data_for_each_brush_world_instance(runtime, trace_brush_world_instance, &context) || !context.ok)
        return false;

    *out_result = context.closest;
    return true;
}

bool slayer3d_game_data_get_brush_diagnostics(const slayer3d_game_data_runtime *runtime,
                                              slayer3d_game_data_brush_diagnostics *out_diagnostics)
{
    if (runtime == NULL || out_diagnostics == NULL)
        return false;
    *out_diagnostics = runtime->brush_diagnostics;
    return true;
}

void slayer3d_game_data_reset_brush_diagnostics(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    SDL_zero(runtime->brush_diagnostics);
}

void slayer3d_game_data_accumulate_brush_render_diagnostics(slayer3d_game_data_runtime *runtime,
                                                            const slayer3d_render_stats *before,
                                                            const slayer3d_render_stats *after)
{
    if (runtime == NULL || before == NULL || after == NULL)
        return;

    runtime->brush_diagnostics.render_mesh_submissions +=
        after->model_mesh_submissions >= before->model_mesh_submissions
            ? after->model_mesh_submissions - before->model_mesh_submissions
            : 0u;
    runtime->brush_diagnostics.render_mesh_culled += after->model_mesh_culled >= before->model_mesh_culled
                                                         ? after->model_mesh_culled - before->model_mesh_culled
                                                         : 0u;
    runtime->brush_diagnostics.render_mesh_draws +=
        after->model_mesh_draws >= before->model_mesh_draws ? after->model_mesh_draws - before->model_mesh_draws : 0u;
    runtime->brush_diagnostics.render_triangles_submitted +=
        after->model_triangles_submitted >= before->model_triangles_submitted
            ? after->model_triangles_submitted - before->model_triangles_submitted
            : 0u;
}

typedef struct brush_named_trace_context
{
    const slayer3d_game_data_runtime *runtime;
    const char *world_name;
} brush_named_trace_context;

static bool brush_slide_trace_named(void *userdata, const slayer3d_game_data_brush_trace_desc *desc,
                                    slayer3d_game_data_brush_trace_result *out_result)
{
    const brush_named_trace_context *context = (const brush_named_trace_context *)userdata;
    if (context == NULL)
        return false;
    return slayer3d_game_data_trace_brush_world(context->runtime, context->world_name, desc, out_result);
}

static bool brush_slide_trace_active(void *userdata, const slayer3d_game_data_brush_trace_desc *desc,
                                     slayer3d_game_data_brush_trace_result *out_result)
{
    const slayer3d_game_data_runtime *runtime = (const slayer3d_game_data_runtime *)userdata;
    return slayer3d_game_data_trace_active_brush_worlds(runtime, desc, out_result);
}

bool slayer3d_game_data_slide_brush_world(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                          const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                          slayer3d_game_data_brush_trace_result *out_result)
{
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0')
        return false;
    brush_named_trace_context context;
    context.runtime = runtime;
    context.world_name = world_name;
    return slayer3d_game_data_brush_slide_with_trace(brush_slide_trace_named, &context, desc, max_bumps, out_result);
}

bool slayer3d_game_data_slide_active_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                                  slayer3d_game_data_brush_trace_result *out_result)
{
    if (runtime == NULL)
        return false;
    return slayer3d_game_data_brush_slide_with_trace(brush_slide_trace_active, (void *)runtime, desc, max_bumps,
                                                     out_result);
}

static slayer3d_game_data_sector_level_variant sector_level_variant_from_string(const char *variant,
                                                                                const slayer3d_level **out_level,
                                                                                const sector_level_runtime *level,
                                                                                bool sector_lighting_enabled)
{
    if (out_level != NULL)
        *out_level = NULL;
    if (level == NULL)
        return 0;

    const char *name = variant != NULL ? variant : "lightmapped";
    if (SDL_strcmp(name, "lightmapped") == 0)
    {
        if (out_level != NULL)
            *out_level = sector_lighting_enabled ? &level->lightmapped : &level->lightmapped_without_sector_lighting;
        return SLAYER3D_GAME_DATA_SECTOR_LEVEL_LIGHTMAPPED;
    }
    if (SDL_strcmp(name, "vertex_baked") == 0)
    {
        if (out_level != NULL)
            *out_level = sector_lighting_enabled ? &level->vertex_baked : &level->vertex_baked_without_sector_lighting;
        return SLAYER3D_GAME_DATA_SECTOR_LEVEL_VERTEX_BAKED;
    }
    if (SDL_strcmp(name, "unlit") == 0)
    {
        if (out_level != NULL)
            *out_level = sector_lighting_enabled ? &level->unlit : &level->unlit_without_sector_lighting;
        return SLAYER3D_GAME_DATA_SECTOR_LEVEL_UNLIT;
    }
    return 0;
}

bool slayer3d_game_data_for_each_sector_level_instance(const slayer3d_game_data_runtime *runtime,
                                                       slayer3d_game_data_sector_level_instance_fn callback,
                                                       void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *instances = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "sector_levels");
    if (instances == NULL)
        return true;
    if (!yyjson_is_arr(instances))
        return false;

    for (size_t i = 0; i < yyjson_arr_size(instances); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(instances, i);
        const char *level_name = json_string(entry, "level", NULL);
        const char *variant_name = scene_state_string(runtime, json_string(entry, "variant_key", NULL),
                                                      json_string(entry, "variant", "lightmapped"));
        const sector_level_runtime *level_runtime = find_sector_level_runtime(runtime, level_name);
        const bool sector_lighting_enabled = scene_state_bool(runtime, json_string(entry, "sector_lighting_key", NULL),
                                                              json_bool(entry, "sector_lighting", true));
        const slayer3d_level *level = NULL;
        const slayer3d_game_data_sector_level_variant variant =
            sector_level_variant_from_string(variant_name, &level, level_runtime, sector_lighting_enabled);
        if (level_runtime == NULL || level == NULL || variant == 0)
            return false;

        slayer3d_game_data_sector_level_instance instance;
        SDL_zero(instance);
        instance.level_name = level_runtime->name;
        instance.variant_name = variant_name;
        instance.variant = variant;
        instance.level = level;
        instance.sectors = level_runtime->sectors;
        instance.sector_count = level_runtime->sector_count;
        instance.position = json_vec3(entry, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        instance.portal_culling = scene_state_bool(runtime, json_string(entry, "portal_culling_key", NULL),
                                                   json_bool(entry, "portal_culling", true));
        instance.sector_lighting_enabled = sector_lighting_enabled;
        if (!callback(userdata, &instance))
            return true;
    }
    return true;
}

bool slayer3d_game_data_for_each_brush_world_instance(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_brush_world_instance_fn callback,
                                                      void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *instances = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "brush_worlds");
    if (instances == NULL)
        return true;
    if (!yyjson_is_arr(instances))
        return false;

    for (size_t i = 0; i < yyjson_arr_size(instances); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(instances, i);
        const char *world_name = json_string(entry, "world", NULL);
        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
        if (world_runtime == NULL)
            return false;

        slayer3d_game_data_brush_world_instance instance;
        SDL_zero(instance);
        instance.world_name = world_runtime->desc.name;
        instance.world = &world_runtime->desc;
        instance.position = json_vec3(entry, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        instance.acceleration_enabled = scene_state_bool(runtime, json_string(entry, "acceleration_key", NULL),
                                                         json_bool(entry, "acceleration", true));
        instance.lighting_enabled =
            scene_state_bool(runtime, json_string(entry, "lighting_key", NULL), json_bool(entry, "lighting", true));
        instance.debug_wireframe = scene_state_bool(runtime, json_string(entry, "debug_wireframe_key", NULL),
                                                    json_bool(entry, "debug_wireframe", false));
        if (!callback(userdata, &instance))
            return true;
    }
    return true;
}

void slayer3d_game_data_ui_state_init(slayer3d_game_data_ui_state *state)
{
    if (state == NULL)
        return;
    SDL_zero(*state);
    state->visible = true;
    state->scale = 1.0f;
    state->alpha = 1.0f;
    state->tint = (slayer3d_color){255, 255, 255, 255};
}

static ui_state_entry *find_ui_state_entry(slayer3d_game_data_runtime *runtime, const char *name)
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

static const ui_state_entry *find_ui_state_entry_const(const slayer3d_game_data_runtime *runtime, const char *name)
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

static bool ensure_ui_state_capacity(slayer3d_game_data_runtime *runtime, int required)
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

static bool set_ui_state_internal(slayer3d_game_data_runtime *runtime, const char *name,
                                  const slayer3d_game_data_ui_state *state, bool animated)
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

bool slayer3d_game_data_set_ui_state(slayer3d_game_data_runtime *runtime, const char *name,
                                     const slayer3d_game_data_ui_state *state)
{
    return set_ui_state_internal(runtime, name, state, false);
}

bool slayer3d_game_data_get_ui_state(const slayer3d_game_data_runtime *runtime, const char *name,
                                     slayer3d_game_data_ui_state *out_state)
{
    if (out_state != NULL)
        slayer3d_game_data_ui_state_init(out_state);
    if (runtime == NULL || name == NULL || out_state == NULL)
        return false;

    const ui_state_entry *entry = find_ui_state_entry_const(runtime, name);
    if (entry == NULL)
        return false;

    *out_state = entry->state;
    return true;
}

bool slayer3d_game_data_clear_ui_state(slayer3d_game_data_runtime *runtime, const char *name)
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

void slayer3d_game_data_clear_ui_states(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < runtime->ui_state_count; ++i)
        SDL_free(runtime->ui_states[i].name);
    runtime->ui_state_count = 0;
}

static void clear_animated_ui_states(slayer3d_game_data_runtime *runtime)
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

static void reset_animation_scope_if_needed(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (runtime->animation_scene == active_scene)
        return;

    if (runtime->animation_scene != NULL)
    {
        runtime->animation_count = 0;
        clear_animated_ui_states(runtime);
    }
    runtime->animation_scene = active_scene;
}

static bool ensure_animation_capacity(slayer3d_game_data_runtime *runtime, int required)
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

static game_data_tween_value tween_vec3(slayer3d_vec3 value)
{
    game_data_tween_value out;
    SDL_zero(out);
    out.type = GAME_DATA_TWEEN_VEC3;
    out.as_vec3 = value;
    return out;
}

static game_data_tween_value tween_color(slayer3d_color value)
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
            *out_value = tween_color(json_color_value(value, (slayer3d_color){255, 255, 255, 255}));
        else
            *out_value = tween_vec3(json_vec3_value(value, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
        return true;
    }
    return false;
}

static game_data_tween_value_type tween_preferred_type(const char *value_type, const slayer3d_value *current,
                                                       const char *ui_property)
{
    if (value_type != NULL && SDL_strcmp(value_type, "color") == 0)
        return GAME_DATA_TWEEN_COLOR;
    if (value_type != NULL && SDL_strcmp(value_type, "vec3") == 0)
        return GAME_DATA_TWEEN_VEC3;
    if (ui_property != NULL && (SDL_strcmp(ui_property, "tint") == 0 || SDL_strcmp(ui_property, "color") == 0))
        return GAME_DATA_TWEEN_COLOR;
    if (current != NULL && current->type == SLAYER3D_VALUE_COLOR)
        return GAME_DATA_TWEEN_COLOR;
    if (current != NULL && current->type == SLAYER3D_VALUE_VEC3)
        return GAME_DATA_TWEEN_VEC3;
    return GAME_DATA_TWEEN_FLOAT;
}

static bool current_property_tween_value(const slayer3d_value *current, game_data_tween_value_type preferred,
                                         game_data_tween_value *out_value, slayer3d_value_type *out_property_type)
{
    if (current == NULL || out_value == NULL || out_property_type == NULL)
        return false;

    *out_property_type = current->type;
    switch (current->type)
    {
    case SLAYER3D_VALUE_INT:
        *out_value = tween_float((float)current->as_int);
        return true;
    case SLAYER3D_VALUE_FLOAT:
        *out_value = tween_float(current->as_float);
        return true;
    case SLAYER3D_VALUE_VEC3:
        *out_value = tween_vec3(current->as_vec3);
        return true;
    case SLAYER3D_VALUE_COLOR:
        *out_value = preferred == GAME_DATA_TWEEN_COLOR
                         ? tween_color(current->as_color)
                         : tween_vec3(slayer3d_vec3_make((float)current->as_color.r, (float)current->as_color.g,
                                                         (float)current->as_color.b));
        return true;
    case SLAYER3D_VALUE_BOOL:
    case SLAYER3D_VALUE_STRING:
        return false;
    }
    return false;
}

static bool current_ui_tween_value(const slayer3d_game_data_ui_state *state, const char *property,
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
        *out_value = tween_color(state != NULL ? state->tint : (slayer3d_color){255, 255, 255, 255});
    else
        return false;
    return true;
}

static game_data_tween_value interpolate_tween_value(const game_data_tween_value *from, const game_data_tween_value *to,
                                                     float t)
{
    if (from->type == GAME_DATA_TWEEN_VEC3 && to->type == GAME_DATA_TWEEN_VEC3)
    {
        return tween_vec3(slayer3d_vec3_make(from->as_vec3.x + (to->as_vec3.x - from->as_vec3.x) * t,
                                             from->as_vec3.y + (to->as_vec3.y - from->as_vec3.y) * t,
                                             from->as_vec3.z + (to->as_vec3.z - from->as_vec3.z) * t));
    }
    if (from->type == GAME_DATA_TWEEN_COLOR && to->type == GAME_DATA_TWEEN_COLOR)
    {
        return tween_color((slayer3d_color){
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

static void apply_ui_tween_value(slayer3d_game_data_runtime *runtime, const char *target, const char *property,
                                 const game_data_tween_value *value)
{
    slayer3d_game_data_ui_state state;
    (void)slayer3d_game_data_get_ui_state(runtime, target, &state);
    if (SDL_strcmp(property, "alpha") == 0 && value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SLAYER3D_GAME_DATA_UI_STATE_ALPHA;
        state.alpha = value->as_float;
    }
    else if (SDL_strcmp(property, "scale") == 0 && value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SLAYER3D_GAME_DATA_UI_STATE_SCALE;
        state.scale = value->as_float;
    }
    else if ((SDL_strcmp(property, "offset_x") == 0 || SDL_strcmp(property, "x") == 0) &&
             value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SLAYER3D_GAME_DATA_UI_STATE_OFFSET;
        state.offset_x = value->as_float;
    }
    else if ((SDL_strcmp(property, "offset_y") == 0 || SDL_strcmp(property, "y") == 0) &&
             value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SLAYER3D_GAME_DATA_UI_STATE_OFFSET;
        state.offset_y = value->as_float;
    }
    else if ((SDL_strcmp(property, "tint") == 0 || SDL_strcmp(property, "color") == 0) &&
             value->type == GAME_DATA_TWEEN_COLOR)
    {
        state.flags |= SLAYER3D_GAME_DATA_UI_STATE_TINT;
        state.tint = value->as_color;
    }
    (void)set_ui_state_internal(runtime, target, &state, true);
}

static void apply_property_tween_value(slayer3d_game_data_runtime *runtime, const game_data_animation *animation,
                                       const game_data_tween_value *value)
{
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, animation->target);
    if (actor == NULL || animation->key == NULL)
        return;

    if ((animation->property_type == SLAYER3D_VALUE_INT || animation->property_type == SLAYER3D_VALUE_FLOAT) &&
        value->type == GAME_DATA_TWEEN_FLOAT)
    {
        if (animation->property_type == SLAYER3D_VALUE_INT)
            slayer3d_properties_set_int(actor->props, animation->key, (int)SDL_floorf(value->as_float + 0.5f));
        else
            slayer3d_properties_set_float(actor->props, animation->key, value->as_float);
    }
    else if (animation->property_type == SLAYER3D_VALUE_VEC3 && value->type == GAME_DATA_TWEEN_VEC3)
    {
        slayer3d_properties_set_vec3(actor->props, animation->key, value->as_vec3);
    }
    else if (animation->property_type == SLAYER3D_VALUE_COLOR && value->type == GAME_DATA_TWEEN_COLOR)
    {
        slayer3d_properties_set_color(actor->props, animation->key, value->as_color);
    }
}

static void apply_animation_value(slayer3d_game_data_runtime *runtime, const game_data_animation *animation,
                                  const game_data_tween_value *value)
{
    if (animation->target_type == GAME_DATA_TWEEN_UI)
        apply_ui_tween_value(runtime, animation->target, animation->property, value);
    else
        apply_property_tween_value(runtime, animation, value);
}

static void remove_conflicting_animation(slayer3d_game_data_runtime *runtime, const game_data_animation *animation)
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

static bool start_animation(slayer3d_game_data_runtime *runtime, const game_data_animation *animation)
{
    if (runtime == NULL || animation == NULL || animation->target == NULL || animation->duration < 0.0f)
        return false;

    reset_animation_scope_if_needed(runtime);
    apply_animation_value(runtime, animation, &animation->from);

    if (animation->duration <= 0.0f)
    {
        apply_animation_value(runtime, animation, &animation->to);
        if (animation->done_signal_id >= 0)
            slayer3d_signal_emit(runtime_bus(runtime), animation->done_signal_id, NULL);
        return true;
    }

    remove_conflicting_animation(runtime, animation);
    if (!ensure_animation_capacity(runtime, runtime->animation_count + 1))
        return false;
    runtime->animations[runtime->animation_count] = *animation;
    ++runtime->animation_count;
    return true;
}

static yyjson_val *active_skip_policy_json(const slayer3d_game_data_runtime *runtime)
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

bool slayer3d_game_data_get_active_skip_policy(const slayer3d_game_data_runtime *runtime,
                                               slayer3d_game_data_skip_policy *out_policy)
{
    if (out_policy != NULL)
    {
        SDL_zero(*out_policy);
        out_policy->enabled = false;
        out_policy->input = SLAYER3D_GAME_DATA_SKIP_INPUT_ANY;
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
            out_policy->action != NULL ? SLAYER3D_GAME_DATA_SKIP_INPUT_ACTION : SLAYER3D_GAME_DATA_SKIP_INPUT_ANY;
    else if (SDL_strcmp(input, "any") == 0 || SDL_strcmp(input, "any_input") == 0)
        out_policy->input = SLAYER3D_GAME_DATA_SKIP_INPUT_ANY;
    else if (SDL_strcmp(input, "action") == 0)
        out_policy->input = SLAYER3D_GAME_DATA_SKIP_INPUT_ACTION;
    else
        out_policy->input = SLAYER3D_GAME_DATA_SKIP_INPUT_DISABLED;

    out_policy->action_id = slayer3d_game_data_find_action(runtime, out_policy->action);
    out_policy->scene = json_string(policy, "scene", json_string(policy, "target_scene", NULL));
    out_policy->preserve_exit_transition = json_bool(policy, "preserve_exit_transition", true);
    out_policy->consume_input = json_bool(policy, "consume_input", true);
    out_policy->block_menus = json_bool(policy, "block_menus", out_policy->consume_input);
    out_policy->block_scene_shortcuts =
        json_bool(policy, "block_scene_shortcuts", json_bool(policy, "block_shortcuts", out_policy->consume_input));
    return out_policy->input != SLAYER3D_GAME_DATA_SKIP_INPUT_DISABLED;
}

bool slayer3d_game_data_get_active_timeline_policy(const slayer3d_game_data_runtime *runtime,
                                                   slayer3d_game_data_timeline_policy *out_policy)
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

static yyjson_val *active_timeline_events(const slayer3d_game_data_runtime *runtime)
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

static bool execute_one_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                               const slayer3d_properties *payload);
static bool execute_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                 const slayer3d_properties *payload);
static bool execute_fps_controller_launch_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                 const slayer3d_properties *payload);
static bool execute_fps_controller_teleport_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                   const slayer3d_properties *payload);
static slayer3d_registered_actor *actor_from_payload_key(slayer3d_game_data_runtime *runtime,
                                                         const slayer3d_properties *payload, const char *key);

void slayer3d_game_data_timeline_state_init(slayer3d_game_data_timeline_state *state)
{
    if (state != NULL)
        SDL_zero(*state);
}

bool slayer3d_game_data_update_timeline(slayer3d_game_data_runtime *runtime, slayer3d_game_data_timeline_state *state,
                                        float dt, slayer3d_game_data_timeline_update_result *out_result)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    if (runtime == NULL || state == NULL)
        return false;

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
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

bool slayer3d_game_data_set_active_scene(slayer3d_game_data_runtime *runtime, const char *scene_name)
{
    return slayer3d_game_data_set_active_scene_with_payload(runtime, scene_name, NULL);
}

bool slayer3d_game_data_set_active_scene_with_payload(slayer3d_game_data_runtime *runtime, const char *scene_name,
                                                      const slayer3d_properties *payload)
{
    scene_entry *scene = find_scene(runtime, scene_name);
    if (runtime == NULL || scene == NULL)
        return false;

    const char *previous = slayer3d_game_data_active_scene(runtime);
    if (previous != NULL && scene->name != NULL && SDL_strcmp(previous, scene->name) != 0 &&
        !apply_actor_pool_scene_exit_policies(runtime, previous, scene->name))
    {
        return false;
    }
    runtime->active_scene_index = (int)(scene - runtime->scenes);
    runtime->input_capture.active = false;
    clear_menu_text_entry_capture(runtime);
    apply_scene_camera(runtime, scene);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D game data scene set: %s -> %s",
                 previous != NULL ? previous : "<none>", scene->name != NULL ? scene->name : "<none>");
    emit_scene_enter_signal(runtime, scene, previous, payload);
    return true;
}

bool slayer3d_game_data_active_scene_updates_game(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return scene != NULL ? json_bool(scene->root, "updates_game", true) : true;
}

static bool update_phase_default(const slayer3d_game_data_runtime *runtime, const char *phase, bool paused)
{
    if (phase != NULL && SDL_strcmp(phase, "simulation") == 0)
        return !paused && slayer3d_game_data_active_scene_updates_game(runtime);
    if (phase != NULL && SDL_strcmp(phase, "app_flow") == 0)
        return true;
    if (phase != NULL && (SDL_strcmp(phase, "scene_activity") == 0 || SDL_strcmp(phase, "presentation") == 0 ||
                          SDL_strcmp(phase, "property_effects") == 0 || SDL_strcmp(phase, "particles") == 0))
        return true;
    return !paused;
}

static bool eval_data_condition(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                                const slayer3d_game_data_ui_metrics *metrics);

static bool eval_update_phase_entry(const slayer3d_game_data_runtime *runtime, yyjson_val *entry, bool paused,
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

bool slayer3d_game_data_active_scene_update_phase(const slayer3d_game_data_runtime *runtime, const char *phase,
                                                  bool paused)
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

bool slayer3d_game_data_active_scene_renders_world(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return scene != NULL ? json_bool(scene->root, "renders_world", true) : true;
}

bool slayer3d_game_data_get_active_scene_skybox(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_game_data_scene_skybox *out_skybox)
{
    if (out_skybox != NULL)
        SDL_zero(*out_skybox);
    if (runtime == NULL || out_skybox == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *skybox = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "skybox");
    if (!yyjson_is_obj(skybox))
        return false;

    out_skybox->pos_x = json_string(skybox, "pos_x", NULL);
    out_skybox->neg_x = json_string(skybox, "neg_x", NULL);
    out_skybox->pos_y = json_string(skybox, "pos_y", NULL);
    out_skybox->neg_y = json_string(skybox, "neg_y", NULL);
    out_skybox->pos_z = json_string(skybox, "pos_z", NULL);
    out_skybox->neg_z = json_string(skybox, "neg_z", NULL);
    out_skybox->size = json_float(skybox, "size", 400.0f);
    return out_skybox->pos_x != NULL && out_skybox->neg_x != NULL && out_skybox->pos_y != NULL &&
           out_skybox->neg_y != NULL && out_skybox->pos_z != NULL && out_skybox->neg_z != NULL;
}

bool slayer3d_game_data_active_scene_has_entity(const slayer3d_game_data_runtime *runtime, const char *entity_name)
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

bool slayer3d_game_data_active_scene_allows_action(const slayer3d_game_data_runtime *runtime, int action_id)
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

bool slayer3d_game_data_active_scene_mouse_capture(const slayer3d_game_data_runtime *runtime, bool paused)
{
    if (runtime == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *input = obj_get(scene != NULL ? scene->root : NULL, "input");
    const char *policy = json_string(input, "mouse_capture", "never");
    if (policy == NULL || SDL_strcmp(policy, "never") == 0)
        return false;
    if (SDL_strcmp(policy, "always") == 0)
        return true;
    if (SDL_strcmp(policy, "unpaused") == 0)
        return !paused;
    return false;
}

bool slayer3d_game_data_get_scene_transition_policy(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_scene_transition_policy *out_policy)
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

bool slayer3d_game_data_get_scene_transition(const slayer3d_game_data_runtime *runtime, const char *scene_name,
                                             const char *phase, slayer3d_game_data_transition_desc *out_transition)
{
    const scene_entry *scene = find_scene_const(runtime, scene_name);
    if (scene == NULL || phase == NULL)
        return false;

    const char *transition_name = json_string(obj_get(scene->root, "transitions"), phase, NULL);
    return transition_name != NULL && slayer3d_game_data_get_transition(runtime, transition_name, out_transition);
}

static bool eval_data_condition(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                                const slayer3d_game_data_ui_metrics *metrics);

static const scene_menu_state *active_scene_menu_for_metrics(const slayer3d_game_data_runtime *runtime,
                                                             const slayer3d_game_data_ui_metrics *metrics)
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

bool slayer3d_game_data_get_active_menu_for_metrics(const slayer3d_game_data_runtime *runtime,
                                                    const slayer3d_game_data_ui_metrics *metrics,
                                                    slayer3d_game_data_menu *out_menu)
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
    out_menu->up_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "up_action", NULL));
    out_menu->down_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "down_action", NULL));
    out_menu->left_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "left_action", NULL));
    out_menu->right_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "right_action", NULL));
    out_menu->select_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "select_action", NULL));
    out_menu->back_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "back_action", NULL));
    out_menu->move_signal_id = slayer3d_game_data_find_signal(runtime, json_string(menu, "move_signal", NULL));
    out_menu->select_signal_id = slayer3d_game_data_find_signal(runtime, json_string(menu, "select_signal", NULL));
    out_menu->item_count = menu_runtime_item_count(runtime, state);
    out_menu->selected_index =
        out_menu->item_count > 0 ? SDL_clamp(state->selected_index, 0, out_menu->item_count - 1) : -1;
    return out_menu->name != NULL && out_menu->item_count > 0;
}

bool slayer3d_game_data_get_active_menu(const slayer3d_game_data_runtime *runtime, slayer3d_game_data_menu *out_menu)
{
    return slayer3d_game_data_get_active_menu_for_metrics(runtime, NULL, out_menu);
}

bool slayer3d_game_data_menu_move(slayer3d_game_data_runtime *runtime, const char *menu_name, int delta)
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

bool slayer3d_game_data_publish_menu_selection(slayer3d_game_data_runtime *runtime, const char *menu_name)
{
    scene_menu_state *menu = find_scene_menu(active_scene_entry(runtime), menu_name);
    const int item_count = menu_runtime_item_count(runtime, menu);
    if (menu == NULL || item_count <= 0)
        return false;

    menu->selected_index = SDL_clamp(menu->selected_index, 0, item_count - 1);
    update_dynamic_list_selection_state(runtime, menu);
    return true;
}

static runtime_collection *find_runtime_collection(slayer3d_game_data_runtime *runtime, const char *collection_name)
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

static const runtime_collection *find_runtime_collection_const(const slayer3d_game_data_runtime *runtime,
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

static runtime_collection *get_or_create_runtime_collection(slayer3d_game_data_runtime *runtime,
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

static slayer3d_properties *runtime_collection_ensure_row(runtime_collection *collection, int row_index)
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
        slayer3d_properties **next =
            (slayer3d_properties **)SDL_realloc(collection->rows, (size_t)next_capacity * sizeof(*collection->rows));
        if (next == NULL)
            return NULL;
        SDL_memset(next + collection->row_capacity, 0,
                   (size_t)(next_capacity - collection->row_capacity) * sizeof(*collection->rows));
        collection->rows = next;
        collection->row_capacity = next_capacity;
    }
    if (collection->rows[row_index] == NULL)
    {
        collection->rows[row_index] = slayer3d_properties_create();
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

    const slayer3d_value *value = slayer3d_properties_get_value(collection->rows[row_index], field_name);
    if (value == NULL)
        return false;

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        SDL_snprintf(buffer, buffer_size, "%d", value->as_int);
        return true;
    case SLAYER3D_VALUE_FLOAT:
        SDL_snprintf(buffer, buffer_size, "%.3f", (double)value->as_float);
        return true;
    case SLAYER3D_VALUE_BOOL:
        SDL_strlcpy(buffer, value->as_bool ? "true" : "false", buffer_size);
        return true;
    case SLAYER3D_VALUE_STRING:
        SDL_strlcpy(buffer, value->as_string != NULL ? value->as_string : "", buffer_size);
        return true;
    default:
        return false;
    }
}

bool slayer3d_game_data_runtime_collection_clear(slayer3d_game_data_runtime *runtime, const char *collection_name)
{
    runtime_collection *collection = find_runtime_collection(runtime, collection_name);
    if (collection == NULL)
        return runtime != NULL && collection_name != NULL && collection_name[0] != '\0';

    for (int i = 0; i < collection->row_count; ++i)
    {
        slayer3d_properties_destroy(collection->rows[i]);
        collection->rows[i] = NULL;
    }
    collection->row_count = 0;
    return true;
}

int slayer3d_game_data_runtime_collection_count(const slayer3d_game_data_runtime *runtime, const char *collection_name)
{
    const runtime_collection *collection = find_runtime_collection_const(runtime, collection_name);
    return collection != NULL ? collection->row_count : 0;
}

bool slayer3d_game_data_runtime_collection_set_string(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                      int row_index, const char *field_name, const char *value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    slayer3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    slayer3d_properties_set_string(row, field_name, value != NULL ? value : "");
    return true;
}

bool slayer3d_game_data_runtime_collection_set_int(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                   int row_index, const char *field_name, int value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    slayer3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    slayer3d_properties_set_int(row, field_name, value);
    return true;
}

bool slayer3d_game_data_runtime_collection_set_float(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                     int row_index, const char *field_name, float value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    slayer3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    slayer3d_properties_set_float(row, field_name, value);
    return true;
}

bool slayer3d_game_data_runtime_collection_set_bool(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                    int row_index, const char *field_name, bool value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    slayer3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    slayer3d_properties_set_bool(row, field_name, value);
    return true;
}

static const char *game_data_network_state_name(slayer3d_network_state state)
{
    switch (state)
    {
    case SLAYER3D_NETWORK_STATE_DISCONNECTED:
        return "disconnected";
    case SLAYER3D_NETWORK_STATE_CONNECTING:
        return "connecting";
    case SLAYER3D_NETWORK_STATE_WAITING:
        return "waiting";
    case SLAYER3D_NETWORK_STATE_CONNECTED:
        return "connected";
    case SLAYER3D_NETWORK_STATE_REJECTED:
        return "rejected";
    case SLAYER3D_NETWORK_STATE_TIMED_OUT:
        return "timed_out";
    case SLAYER3D_NETWORK_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static runtime_direct_connect_session *find_direct_connect_session(slayer3d_game_data_runtime *runtime,
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

static runtime_direct_connect_session *get_or_create_direct_connect_session(slayer3d_game_data_runtime *runtime,
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

static void direct_connect_publish_status_entry(slayer3d_game_data_runtime *runtime,
                                                const runtime_direct_connect_session *entry, const char *status_key,
                                                const char *state_key, const char *connected_key,
                                                const char *fallback_status)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const slayer3d_network_state state = entry != NULL && entry->session != NULL
                                             ? slayer3d_network_session_state(entry->session)
                                             : SLAYER3D_NETWORK_STATE_DISCONNECTED;
    const char *status =
        entry != NULL && entry->session != NULL ? slayer3d_network_session_status(entry->session) : NULL;
    if (status == NULL || status[0] == '\0')
        status = fallback_status != NULL ? fallback_status : game_data_network_state_name(state);

    if (status_key != NULL && status_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, status_key, status);
    if (state_key != NULL && state_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, state_key, game_data_network_state_name(state));
    if (connected_key != NULL && connected_key[0] != '\0')
        slayer3d_properties_set_bool(runtime->scene_state, connected_key, state == SLAYER3D_NETWORK_STATE_CONNECTED);
}

static void direct_connect_publish_manual_status(slayer3d_game_data_runtime *runtime, const char *status_key,
                                                 const char *state_key, const char *connected_key, const char *status,
                                                 const char *state, bool connected)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    if (status_key != NULL && status_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, status_key, status != NULL ? status : "");
    if (state_key != NULL && state_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, state_key, state != NULL ? state : "unknown");
    if (connected_key != NULL && connected_key[0] != '\0')
        slayer3d_properties_set_bool(runtime->scene_state, connected_key, connected);
}

slayer3d_network_session *slayer3d_game_data_get_network_direct_connect_session(slayer3d_game_data_runtime *runtime,
                                                                                const char *session_name)
{
    runtime_direct_connect_session *entry = find_direct_connect_session(runtime, session_name);
    return entry != NULL ? entry->session : NULL;
}

bool slayer3d_game_data_network_direct_connect_publish_status(slayer3d_game_data_runtime *runtime,
                                                              const char *session_name, const char *status_key,
                                                              const char *state_key, const char *connected_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    direct_connect_publish_status_entry(runtime, find_direct_connect_session(runtime, session_name), status_key,
                                        state_key, connected_key, "Disconnected");
    return true;
}

bool slayer3d_game_data_network_direct_connect_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                      const char *status_key, const char *state_key,
                                                      const char *connected_key, const char *status)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_direct_connect_session *entry = find_direct_connect_session(runtime, session_name);
    if (entry != NULL && entry->session != NULL)
    {
        slayer3d_network_session_destroy(entry->session);
        entry->session = NULL;
    }
    direct_connect_publish_status_entry(runtime, entry, status_key, state_key, connected_key,
                                        status != NULL ? status : "Disconnected");
    return true;
}

bool slayer3d_game_data_network_direct_connect_start(slayer3d_game_data_runtime *runtime, const char *session_name,
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
        slayer3d_network_session_destroy(entry->session);
        entry->session = NULL;
    }

    slayer3d_network_session_desc desc;
    slayer3d_network_session_desc_init(&desc);
    desc.role = SLAYER3D_NETWORK_ROLE_CLIENT;
    desc.host = host;
    desc.port = (Uint16)port;
    desc.local_port = 0;
    desc.handshake_timeout = 5.0f;
    desc.idle_timeout = 10.0f;

    if (!slayer3d_network_session_create(&desc, &entry->session))
    {
        direct_connect_publish_manual_status(runtime, status_key, state_key, connected_key, SDL_GetError(), "error",
                                             false);
        return false;
    }

    direct_connect_publish_status_entry(runtime, entry, status_key, state_key, connected_key, "Connecting");
    return true;
}

static runtime_host_session *find_host_session(slayer3d_game_data_runtime *runtime, const char *session_name)
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

static runtime_host_session *get_or_create_host_session(slayer3d_game_data_runtime *runtime, const char *session_name)
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

static void host_publish_manual_status(slayer3d_game_data_runtime *runtime, const char *status_key,
                                       const char *endpoint_key, const char *peer_key, const char *connected_key,
                                       const char *status, Uint16 port, const char *peer, bool connected)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    if (status_key != NULL && status_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, status_key, status != NULL ? status : "");
    if (endpoint_key != NULL && endpoint_key[0] != '\0')
    {
        char endpoint[32];
        SDL_snprintf(endpoint, sizeof(endpoint), "UDP %u", (unsigned int)port);
        slayer3d_properties_set_string(runtime->scene_state, endpoint_key, endpoint);
    }
    if (peer_key != NULL && peer_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, peer_key, peer != NULL ? peer : "Waiting for client");
    if (connected_key != NULL && connected_key[0] != '\0')
        slayer3d_properties_set_bool(runtime->scene_state, connected_key, connected);
}

static void host_publish_status_entry(slayer3d_game_data_runtime *runtime, const runtime_host_session *entry,
                                      const char *status_key, const char *endpoint_key, const char *peer_key,
                                      const char *connected_key, const char *fallback_status, Uint16 fallback_port)
{
    const slayer3d_network_state state = entry != NULL && entry->session != NULL
                                             ? slayer3d_network_session_state(entry->session)
                                             : SLAYER3D_NETWORK_STATE_DISCONNECTED;
    const char *status =
        entry != NULL && entry->session != NULL ? slayer3d_network_session_status(entry->session) : NULL;
    const Uint16 port =
        entry != NULL && entry->session != NULL ? slayer3d_network_session_port(entry->session) : fallback_port;
    char peer_label[SLAYER3D_NETWORK_MAX_HOST_LENGTH + 48];
    char peer_host[SLAYER3D_NETWORK_MAX_HOST_LENGTH];
    Uint16 peer_port = 0;
    const bool connected =
        entry != NULL && entry->session != NULL && slayer3d_network_session_is_connected(entry->session);

    if (status == NULL || status[0] == '\0')
        status = fallback_status != NULL ? fallback_status : game_data_network_state_name(state);
    SDL_snprintf(peer_label, sizeof(peer_label), "Waiting for client");
    SDL_zero(peer_host);
    if (connected &&
        slayer3d_network_session_get_peer_endpoint(entry->session, peer_host, (int)sizeof(peer_host), &peer_port))
        SDL_snprintf(peer_label, sizeof(peer_label), "Client 1 - %s:%u", peer_host, (unsigned int)peer_port);
    else if (connected)
        SDL_snprintf(peer_label, sizeof(peer_label), "Client 1 - Connected");

    host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key, status, port, peer_label,
                               connected);
}

slayer3d_network_session *slayer3d_game_data_get_network_host_session(slayer3d_game_data_runtime *runtime,
                                                                      const char *session_name)
{
    runtime_host_session *entry = find_host_session(runtime, session_name);
    return entry != NULL ? entry->session : NULL;
}

bool slayer3d_game_data_network_host_publish_status(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                    const char *status_key, const char *endpoint_key,
                                                    const char *peer_key, const char *connected_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    host_publish_status_entry(runtime, find_host_session(runtime, session_name), status_key, endpoint_key, peer_key,
                              connected_key, "Not hosting", SLAYER3D_NETWORK_DEFAULT_PORT);
    return true;
}

bool slayer3d_game_data_network_host_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                            const char *status_key, const char *endpoint_key, const char *peer_key,
                                            const char *connected_key, const char *status)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_host_session *entry = find_host_session(runtime, session_name);
    Uint16 port = SLAYER3D_NETWORK_DEFAULT_PORT;
    if (entry != NULL && entry->session != NULL)
    {
        port = slayer3d_network_session_port(entry->session);
        slayer3d_network_session_destroy(entry->session);
        entry->session = NULL;
    }
    host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key,
                               status != NULL ? status : "Not hosting", port, "Waiting for client", false);
    return true;
}

bool slayer3d_game_data_network_host_start(slayer3d_game_data_runtime *runtime, const char *session_name, int port,
                                           const char *advertised_name, const char *status_key,
                                           const char *endpoint_key, const char *peer_key, const char *connected_key)
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
                                   SLAYER3D_NETWORK_DEFAULT_PORT, "Waiting for client", false);
        return false;
    }

    slayer3d_network_session_desc desc;
    slayer3d_network_session_desc_init(&desc);
    desc.role = SLAYER3D_NETWORK_ROLE_HOST;
    desc.host = NULL;
    desc.port = (Uint16)port;
    desc.local_port = 0;
    desc.handshake_timeout = 5.0f;
    desc.idle_timeout = 10.0f;
    desc.session_name = advertised_name != NULL && advertised_name[0] != '\0' ? advertised_name : "SLAYER3D Session";

    if (!slayer3d_network_session_create(&desc, &entry->session))
    {
        host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key, SDL_GetError(),
                                   (Uint16)SDL_max(port, 0), "Waiting for client", false);
        return false;
    }

    host_publish_status_entry(runtime, entry, status_key, endpoint_key, peer_key, connected_key, "Waiting",
                              (Uint16)port);
    return true;
}

static runtime_discovery_session *find_discovery_session(slayer3d_game_data_runtime *runtime, const char *session_name)
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

static runtime_discovery_session *get_or_create_discovery_session(slayer3d_game_data_runtime *runtime,
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

static void discovery_publish_manual_status(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                            const char *status_key, const char *count_key, const char *status,
                                            int count)
{
    if (runtime == NULL)
        return;
    if (collection_name != NULL && collection_name[0] != '\0')
        (void)slayer3d_game_data_runtime_collection_clear(runtime, collection_name);
    if (runtime->scene_state != NULL && status_key != NULL && status_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, status_key, status != NULL ? status : "");
    if (runtime->scene_state != NULL && count_key != NULL && count_key[0] != '\0')
        slayer3d_properties_set_int(runtime->scene_state, count_key, count);
}

static void discovery_publish_results(slayer3d_game_data_runtime *runtime, const runtime_discovery_session *entry,
                                      const char *collection_name, const char *status_key, const char *count_key)
{
    if (runtime == NULL)
        return;

    const int result_count =
        entry != NULL && entry->session != NULL ? slayer3d_network_discovery_session_result_count(entry->session) : 0;
    const char *status =
        entry != NULL && entry->session != NULL ? slayer3d_network_discovery_session_status(entry->session) : "Idle";
    if (status == NULL || status[0] == '\0')
        status = result_count > 0 ? "Session found" : "Scanning";

    if (collection_name != NULL && collection_name[0] != '\0')
    {
        (void)slayer3d_game_data_runtime_collection_clear(runtime, collection_name);
        for (int i = 0; i < result_count; ++i)
        {
            slayer3d_network_discovery_result result;
            char label[SLAYER3D_NETWORK_MAX_STATUS_LENGTH + SLAYER3D_NETWORK_MAX_HOST_LENGTH + 32];
            char endpoint[SLAYER3D_NETWORK_MAX_HOST_LENGTH + 16];
            SDL_zero(result);
            if (!slayer3d_network_discovery_session_get_result(entry->session, i, &result))
                continue;

            SDL_snprintf(endpoint, sizeof(endpoint), "%s:%u", result.host, (unsigned int)result.port);
            SDL_snprintf(label, sizeof(label), "%s  %s%s%s",
                         result.session_name[0] != '\0' ? result.session_name : "SLAYER3D Session", endpoint,
                         result.status[0] != '\0' ? "  " : "", result.status[0] != '\0' ? result.status : "");
            (void)slayer3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "label", label);
            (void)slayer3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "name",
                                                                   result.session_name[0] != '\0' ? result.session_name
                                                                                                  : "SLAYER3D Session");
            (void)slayer3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "host", result.host);
            (void)slayer3d_game_data_runtime_collection_set_int(runtime, collection_name, i, "port", (int)result.port);
            (void)slayer3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "status",
                                                                   result.status);
            (void)slayer3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "endpoint", endpoint);
        }
    }

    if (runtime->scene_state != NULL && status_key != NULL && status_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, status_key, status);
    if (runtime->scene_state != NULL && count_key != NULL && count_key[0] != '\0')
        slayer3d_properties_set_int(runtime->scene_state, count_key, result_count);
}

bool slayer3d_game_data_network_discovery_start(slayer3d_game_data_runtime *runtime, const char *session_name,
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
        slayer3d_network_discovery_session_desc desc;
        slayer3d_network_discovery_session_desc_init(&desc);
        desc.host = host != NULL && host[0] != '\0' ? host : NULL;
        desc.port = (Uint16)port;
        desc.local_port = (Uint16)local_port;
        if (!slayer3d_network_discovery_session_create(&desc, &entry->session))
        {
            discovery_publish_manual_status(runtime, collection_name, status_key, count_key, SDL_GetError(), 0);
            return false;
        }
    }

    if (!slayer3d_network_discovery_session_refresh(entry->session))
    {
        discovery_publish_manual_status(runtime, collection_name, status_key, count_key, SDL_GetError(), 0);
        return false;
    }

    discovery_publish_results(runtime, entry, collection_name, status_key, count_key);
    return true;
}

bool slayer3d_game_data_network_discovery_update(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                 float dt, const char *collection_name, const char *status_key,
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
    if (!slayer3d_network_discovery_session_update(entry->session, dt))
    {
        discovery_publish_results(runtime, entry, collection_name, status_key, count_key);
        return false;
    }
    discovery_publish_results(runtime, entry, collection_name, status_key, count_key);
    return true;
}

bool slayer3d_game_data_network_discovery_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                 const char *collection_name, const char *status_key,
                                                 const char *count_key, const char *status)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_discovery_session *entry = find_discovery_session(runtime, session_name);
    if (entry != NULL && entry->session != NULL)
    {
        slayer3d_network_discovery_session_destroy(entry->session);
        entry->session = NULL;
    }
    discovery_publish_manual_status(runtime, collection_name, status_key, count_key,
                                    status != NULL ? status : "Discovery canceled", 0);
    return true;
}

bool slayer3d_game_data_network_discovery_connect_selected(slayer3d_game_data_runtime *runtime,
                                                           const char *discovery_name, const char *collection_name,
                                                           int selected_index, const char *direct_connect_name,
                                                           const char *host_key, const char *port_key,
                                                           const char *status_key, const char *state_key,
                                                           const char *connected_key, const char *connecting_status)
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

    const char *host = slayer3d_properties_get_string(collection->rows[selected_index], "host", NULL);
    char host_copy[SLAYER3D_NETWORK_MAX_HOST_LENGTH];
    const slayer3d_value *port_value = slayer3d_properties_get_value(collection->rows[selected_index], "port");
    const int port = port_value != NULL && port_value->type == SLAYER3D_VALUE_INT
                         ? port_value->as_int
                         : SDL_atoi(slayer3d_properties_get_string(collection->rows[selected_index], "port", "0"));
    SDL_strlcpy(host_copy, host != NULL ? host : "", sizeof(host_copy));
    if (runtime->scene_state != NULL)
    {
        if (host_key != NULL && host_key[0] != '\0')
            slayer3d_properties_set_string(runtime->scene_state, host_key, host_copy);
        if (port_key != NULL && port_key[0] != '\0')
        {
            char port_text[16];
            SDL_snprintf(port_text, sizeof(port_text), "%d", port);
            slayer3d_properties_set_string(runtime->scene_state, port_key, port_text);
        }
    }

    (void)slayer3d_game_data_network_discovery_cancel(runtime, discovery_name, collection_name, NULL, NULL,
                                                      "Discovery canceled");
    const bool ok = slayer3d_game_data_network_direct_connect_start(runtime, direct_connect_name, host_copy, port,
                                                                    status_key, state_key, connected_key);
    if (ok && connecting_status != NULL && connecting_status[0] != '\0' && runtime->scene_state != NULL &&
        status_key != NULL && status_key[0] != '\0')
    {
        slayer3d_properties_set_string(runtime->scene_state, status_key, connecting_status);
    }
    return ok;
}

#include "game_data/game_data_menu_ui.inc"

float slayer3d_game_data_delta_time(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->current_dt : 0.0f;
}

#include "game_data/game_data_actors_input.inc"

#include "game_data/game_data_scripts.inc"

#include "game_data/game_data_actions.inc"

static yyjson_val *active_scene_activity_json(const slayer3d_game_data_runtime *runtime)
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

static bool activity_reset_for_scene(slayer3d_game_data_runtime *runtime, const char *scene, yyjson_val *activity)
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

static bool activity_input_matches(const slayer3d_game_data_runtime *runtime, yyjson_val *activity,
                                   const slayer3d_input_manager *input)
{
    if (runtime == NULL || activity == NULL || input == NULL)
        return false;

    const char *mode = json_string(activity, "input", "any");
    if (SDL_strcmp(mode, "disabled") == 0 || SDL_strcmp(mode, "none") == 0)
        return false;
    if (SDL_strcmp(mode, "action") == 0)
    {
        const int action_id = slayer3d_game_data_find_action(runtime, json_string(activity, "action", NULL));
        return action_id >= 0 && slayer3d_input_is_pressed(input, action_id);
    }
    return slayer3d_input_any_pressed(input);
}

bool slayer3d_game_data_scene_activity_consumes_wake_input(const slayer3d_game_data_runtime *runtime,
                                                           const slayer3d_input_manager *input, bool *out_block_menus,
                                                           bool *out_block_scene_shortcuts)
{
    if (out_block_menus != NULL)
        *out_block_menus = false;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = false;
    if (runtime == NULL || input == NULL)
        return false;

    yyjson_val *activity = active_scene_activity_json(runtime);
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
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

bool slayer3d_game_data_update_scene_activity(slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                              float dt)
{
    if (runtime == NULL)
        return false;

    if (dt < 0.0f)
        dt = 0.0f;

    yyjson_val *activity = active_scene_activity_json(runtime);
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
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

static void execute_binding(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    binding_entry *binding = (binding_entry *)userdata;
    (void)signal_id;
    if (binding != NULL)
        execute_action_array(binding->runtime, binding->actions, payload);
}

static bool load_bindings(slayer3d_game_data_runtime *runtime, yyjson_val *logic, char *error_buffer,
                          int error_buffer_size)
{
    yyjson_val *bindings = obj_get(logic, "bindings");
    if (!yyjson_is_arr(bindings))
        return true;

    slayer3d_signal_bus *bus = runtime_bus(runtime);
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
        const int signal_id = slayer3d_game_data_find_signal(runtime, json_string(binding, "signal", NULL));
        if (signal_id < 0)
            continue;
        runtime->bindings[i].runtime = runtime;
        runtime->bindings[i].actions = obj_get(binding, "actions");
        runtime->bindings[i].connection_id =
            slayer3d_signal_connect(bus, signal_id, execute_binding, &runtime->bindings[i]);
        if (runtime->bindings[i].connection_id == 0)
            return false;
    }
    return true;
}

static bool load_sensors(slayer3d_game_data_runtime *runtime, yyjson_val *logic)
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
        entry->json = sensor;
        const char *type = json_string(sensor, "type", "");
        if (SDL_strcmp(type, "sensor.bounds_exit") == 0)
            entry->type = GAME_DATA_SENSOR_BOUNDS_EXIT;
        else if (SDL_strcmp(type, "sensor.bounds_reflect") == 0)
            entry->type = GAME_DATA_SENSOR_BOUNDS_REFLECT;
        else if (SDL_strcmp(type, "sensor.contact_2d") == 0)
            entry->type = GAME_DATA_SENSOR_CONTACT_2D;
        else if (SDL_strcmp(type, "collision.on_overlap") == 0)
            entry->type = GAME_DATA_SENSOR_CONTACT_2D;
        else if (SDL_strcmp(type, "sensor.hearing") == 0)
            entry->type = GAME_DATA_SENSOR_HEARING;
        else if (SDL_strcmp(type, "sensor.input_pressed") == 0)
            entry->type = GAME_DATA_SENSOR_INPUT_PRESSED;
        else if (SDL_strcmp(type, "sensor.brush_contents") == 0)
            entry->type = GAME_DATA_SENSOR_BRUSH_CONTENTS;
        else if (SDL_strcmp(type, "sensor.brush_perception") == 0)
            entry->type = GAME_DATA_SENSOR_BRUSH_PERCEPTION;
        else if (SDL_strcmp(type, "sensor.perception") == 0)
            entry->type = GAME_DATA_SENSOR_PERCEPTION;
        else if (SDL_strcmp(type, "sensor.sector") == 0)
            entry->type = GAME_DATA_SENSOR_SECTOR;
        else if (SDL_strcmp(type, "sensor.volume") == 0)
            entry->type = GAME_DATA_SENSOR_VOLUME;

        entry->name = json_string(sensor, "name", NULL);
        entry->entity = json_string(sensor, "observer", json_string(sensor, "entity", json_string(sensor, "a", NULL)));
        if (entry->entity == NULL)
            entry->entity = json_string(sensor, "actor", NULL);
        entry->other = json_string(sensor, "target", json_string(sensor, "b", NULL));
        entry->entity_tag = json_string(sensor, "observer_tag", json_string(sensor, "a_tag", NULL));
        if (entry->entity_tag == NULL)
            entry->entity_tag = json_string(sensor, "actor_tag", NULL);
        entry->other_tag = json_string(sensor, "target_tag", json_string(sensor, "b_tag", NULL));
        entry->sector_level = json_string(sensor, "sector_level", NULL);
        entry->sector = json_string(sensor, "sector", NULL);
        entry->sector_property = json_string(sensor, "sector_property", "current_sector");
        entry->sector_index = json_int(sensor, "sector_index", -1);
        entry->action = json_string(sensor, "action", NULL);
        entry->axis = json_string(sensor, "axis", NULL);
        entry->side = json_string(sensor, "side", NULL);
        entry->min_value = json_float(sensor, "min", 0.0f);
        entry->max_value = json_float(sensor, "max", 0.0f);
        entry->threshold = json_float(sensor, "threshold", 0.0f);
        entry->range = json_float(sensor, "range", 64.0f);
        entry->min_dot = SDL_clamp(json_float(sensor, "min_dot", -1.0f), -1.0f, 1.0f);
        yyjson_val *fov_degrees = obj_get(sensor, "fov_degrees");
        if (yyjson_is_num(fov_degrees))
            entry->min_dot = SDL_cosf(SDL_clamp((float)yyjson_get_num(fov_degrees), 0.0f, 360.0f) * SDL_PI_F / 360.0f);
        entry->observer_eye_height = json_float(sensor, "observer_eye_height", json_float(sensor, "eye_height", 0.0f));
        entry->target_eye_height = json_float(sensor, "target_eye_height", entry->observer_eye_height);
        entry->yaw_property = json_string(sensor, "yaw_property", "yaw");
        entry->volume_min = json_vec3(sensor, "min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        entry->volume_max = json_vec3(sensor, "max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        entry->actions = obj_get(sensor, "actions");
        entry->edge = json_string(sensor, "edge", "enter");
        const char *signal_name =
            json_string(sensor, "on_enter", json_string(sensor, "on_pressed", json_string(sensor, "on_reflect", NULL)));
        if (SDL_strcmp(entry->edge, "stay") == 0 || SDL_strcmp(entry->edge, "overlap") == 0)
            signal_name = json_string(sensor, "on_stay", signal_name);
        else if (SDL_strcmp(entry->edge, "exit") == 0)
            signal_name = json_string(sensor, "on_exit", signal_name);
        entry->signal_id = slayer3d_game_data_find_signal(runtime, signal_name);
    }
    return true;
}

static bool load_wave_schedules(slayer3d_game_data_runtime *runtime, yyjson_val *logic)
{
    yyjson_val *schedules = obj_get(logic, "wave_schedules");
    if (!yyjson_is_arr(schedules))
        return true;

    const int count = (int)yyjson_arr_size(schedules);
    runtime->wave_schedules = (wave_schedule_entry *)SDL_calloc((size_t)count, sizeof(*runtime->wave_schedules));
    if (runtime->wave_schedules == NULL && count > 0)
        return false;
    runtime->wave_schedule_count = count;

    for (int i = 0; i < count; ++i)
    {
        runtime->wave_schedules[i].schedule = yyjson_arr_get(schedules, (size_t)i);
        runtime->wave_schedules[i].elapsed = 0.0f;
        runtime->wave_schedules[i].initialized = false;
    }
    return true;
}

static void load_active_camera(slayer3d_game_data_runtime *runtime, yyjson_val *root)
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
            count += SLAYER3D_STANDARD_OPTIONS_SCENE_COUNT;
            continue;
        }

        set_error(error_buffer, error_buffer_size, "scene files must be strings or known package objects");
        return -1;
    }
    return count;
}

static bool install_scene_doc(slayer3d_game_data_runtime *runtime, yyjson_doc *doc, int scene_index, char *error_buffer,
                              int error_buffer_size)
{
    yyjson_val *scene_root = yyjson_doc_get_root(doc);
    const char *schema = json_string(scene_root, "schema", NULL);
    const char *name = json_string(scene_root, "name", NULL);
    if (!yyjson_is_obj(scene_root) || schema == NULL || SDL_strcmp(schema, "slayer3d.scene.v0") != 0 || name == NULL ||
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
                 "SLAYER3D game data scene loaded: name=%s updates_game=%d renders_world=%d entities=%d menus=%d", name,
                 json_bool(scene_root, "updates_game", true) ? 1 : 0,
                 json_bool(scene_root, "renders_world", true) ? 1 : 0, runtime->scenes[scene_index].entity_count,
                 runtime->scenes[scene_index].menu_count);
    return true;
}

static bool load_scene_file(slayer3d_game_data_runtime *runtime, const char *file_path, int scene_index,
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

    slayer3d_asset_buffer scene_buffer;
    SDL_zero(scene_buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(runtime->assets, resolved_path, &scene_buffer, asset_error,
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
    slayer3d_asset_buffer_free(&scene_buffer);
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

static bool load_scene_package(slayer3d_game_data_runtime *runtime, yyjson_val *root, const char *package,
                               int *scene_index, char *error_buffer, int error_buffer_size)
{
    slayer3d_standard_options_scene_docs docs;
    if (!slayer3d_standard_options_build_scene_docs(root, package, &docs, error_buffer, error_buffer_size))
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
    slayer3d_standard_options_scene_docs_free(&docs);
    return ok;
}

static void copy_all_properties(slayer3d_properties *target, const slayer3d_properties *source)
{
    const int count = slayer3d_properties_count(source);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (slayer3d_properties_get_key_at(source, i, &key, NULL))
            copy_property_value(target, key, slayer3d_properties_get_value(source, key));
    }
}

static bool load_scenes(slayer3d_game_data_runtime *runtime, yyjson_val *root,
                        const slayer3d_game_data_load_options *options, char *error_buffer, int error_buffer_size)
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
    const bool using_initial_override =
        options != NULL && options->initial_scene_override != NULL && options->initial_scene_override[0] != '\0';
    const char *initial =
        using_initial_override ? options->initial_scene_override : json_string(scenes, "initial", NULL);
    if (initial != NULL)
    {
        scene_entry *scene = find_scene(runtime, initial);
        if (scene == NULL)
        {
            set_error(error_buffer, error_buffer_size,
                      using_initial_override ? "initial scene override does not reference a loaded scene"
                                             : "initial scene does not reference a loaded scene");
            return false;
        }
        runtime->active_scene_index = (int)(scene - runtime->scenes);
    }
    if (options != NULL && options->initial_scene_state != NULL)
        copy_all_properties(runtime->scene_state, options->initial_scene_state);
    apply_scene_camera(runtime, active_scene_entry(runtime));
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D game data initial scene: %s",
                 slayer3d_game_data_active_scene(runtime) != NULL ? slayer3d_game_data_active_scene(runtime)
                                                                  : "<none>");
    emit_scene_enter_signal(runtime, active_scene_entry(runtime), NULL,
                            options != NULL ? options->initial_scene_payload : NULL);
    return true;
}

static int fps_controller_action_id(const slayer3d_game_data_runtime *runtime, yyjson_val *component, const char *name)
{
    yyjson_val *actions = obj_get(component, "actions");
    const char *action = json_string(actions, name, NULL);
    return slayer3d_game_data_find_action(runtime, action);
}

static float fps_controller_action_value(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                         int action_id)
{
    if (input == NULL || action_id < 0 || !slayer3d_game_data_active_scene_allows_action(runtime, action_id))
        return 0.0f;
    return slayer3d_input_get_value(input, action_id);
}

static bool fps_controller_action_pressed(const slayer3d_game_data_runtime *runtime,
                                          const slayer3d_input_manager *input, int action_id)
{
    return input != NULL && action_id >= 0 && slayer3d_game_data_active_scene_allows_action(runtime, action_id) &&
           slayer3d_input_is_pressed(input, action_id);
}

static slayer3d_actor_patrol_mode parse_patrol_mode(const char *value)
{
    if (value != NULL && SDL_strcmp(value, "ping_pong") == 0)
        return SLAYER3D_ACTOR_PATROL_PING_PONG;
    return SLAYER3D_ACTOR_PATROL_LOOP;
}

static int patrol_signal_id(const slayer3d_game_data_runtime *runtime, yyjson_val *component, const char *name)
{
    yyjson_val *signals = obj_get(component, "signals");
    return slayer3d_game_data_find_signal(runtime, json_string(signals, name, NULL));
}

#include "game_data/game_data_update_runtime.inc"

bool slayer3d_game_data_register_adapter(slayer3d_game_data_runtime *runtime, const char *name,
                                         slayer3d_game_data_adapter_fn callback, void *userdata)
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
        if (entry->lua_function_ref != SLAYER3D_SCRIPT_REF_INVALID)
        {
            slayer3d_script_engine_unref(runtime->scripts, entry->lua_function_ref);
            entry->lua_function_ref = SLAYER3D_SCRIPT_REF_INVALID;
        }
        entry->callback = callback;
        entry->userdata = userdata;
        return true;
    }
    return append_adapter(runtime, name, callback, userdata);
}

bool slayer3d_game_data_reload_scripts(slayer3d_game_data_runtime *runtime, slayer3d_asset_resolver *assets,
                                       char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || assets == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid Lua reload arguments");
        return false;
    }
    if (runtime->script_count == 0)
        return true;

    slayer3d_script_engine *new_engine = slayer3d_script_engine_create();
    if (new_engine == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to create Lua script engine");
        return false;
    }
    register_lua_api(runtime, new_engine);

    slayer3d_script_ref *module_refs =
        (slayer3d_script_ref *)SDL_calloc((size_t)runtime->script_count, sizeof(*module_refs));
    slayer3d_script_ref *function_refs =
        (slayer3d_script_ref *)SDL_calloc((size_t)runtime->adapter_count, sizeof(*function_refs));
    bool *loading = (bool *)SDL_calloc((size_t)runtime->script_count, sizeof(*loading));
    bool *loaded = (bool *)SDL_calloc((size_t)runtime->script_count, sizeof(*loaded));
    if (module_refs == NULL || function_refs == NULL || loading == NULL || loaded == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate Lua reload state");
        SDL_free(module_refs);
        SDL_free(function_refs);
        SDL_free(loading);
        SDL_free(loaded);
        slayer3d_script_engine_destroy(new_engine);
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
        if (!slayer3d_script_engine_ref_module_function(new_engine, module_refs[script_index], adapter->lua_function,
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
            slayer3d_script_engine_unref(new_engine, function_refs[i]);
        for (int i = 0; i < runtime->script_count; ++i)
            slayer3d_script_engine_unref(new_engine, module_refs[i]);
        SDL_free(module_refs);
        SDL_free(function_refs);
        SDL_free(loading);
        SDL_free(loaded);
        slayer3d_script_engine_destroy(new_engine);
        return false;
    }

    slayer3d_script_engine *old_engine = runtime->scripts;
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
            runtime->adapters[i].lua_function_ref = SLAYER3D_SCRIPT_REF_INVALID;
    }

    slayer3d_script_engine_destroy(old_engine);
    SDL_free(module_refs);
    SDL_free(function_refs);
    SDL_free(loading);
    SDL_free(loaded);
    return true;
}

static bool apply_app_config_from_root(yyjson_val *root, slayer3d_game_config *out_config, char *title_buffer,
                                       int title_buffer_size, char *error_buffer, int error_buffer_size)
{
    if (!yyjson_is_obj(root) || SDL_strcmp(json_string(root, "schema", ""), "slayer3d.game.v0") != 0)
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
        out_config->width = json_int(window, "window_width", json_int(window, "width", out_config->width));
        out_config->height = json_int(window, "window_height", json_int(window, "height", out_config->height));
        out_config->logical_width = json_int(window, "logical_width", out_config->logical_width);
        out_config->logical_height = json_int(window, "logical_height", out_config->logical_height);
#if defined(SLAYER3D_PRODUCTION_BUILD)
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

static void apply_persisted_app_settings(yyjson_val *root, slayer3d_game_config *out_config)
{
    yyjson_val *app = obj_get(root, "app");
    const char *settings_path = json_string(app, "settings_path", NULL);
    if (settings_path == NULL || settings_path[0] == '\0')
        return;

    slayer3d_storage_config storage_config;
    storage_config_from_root(root, &storage_config);

    char storage_error[256];
    slayer3d_storage *storage = NULL;
    if (!slayer3d_storage_create(&storage_config, &storage, storage_error, (int)sizeof(storage_error)))
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D app settings storage unavailable: %s", storage_error);
        return;
    }

    slayer3d_storage_buffer buffer;
    SDL_zero(buffer);
    if (!slayer3d_storage_read_file(storage, settings_path, &buffer, storage_error, (int)sizeof(storage_error)))
    {
        slayer3d_storage_destroy(storage);
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
    slayer3d_storage_buffer_free(&buffer);
    slayer3d_storage_destroy(storage);
}

bool slayer3d_game_data_load_app_config_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                              slayer3d_game_config *out_config, char *title_buffer,
                                              int title_buffer_size, char *error_buffer, int error_buffer_size)
{
    if (assets == NULL || asset_path == NULL || asset_path[0] == '\0' || out_config == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid app config load arguments");
        return false;
    }

    yyjson_doc *doc = slayer3d_game_data_compose_asset(assets, asset_path, NULL, error_buffer, error_buffer_size);
    if (doc == NULL)
        return false;

    yyjson_val *root = yyjson_doc_get_root(doc);
    const bool ok =
        apply_app_config_from_root(root, out_config, title_buffer, title_buffer_size, error_buffer, error_buffer_size);
    if (ok)
        apply_persisted_app_settings(root, out_config);
    yyjson_doc_free(doc);
    return ok;
}

bool slayer3d_game_data_load_app_config_file(const char *path, slayer3d_game_config *out_config, char *title_buffer,
                                             int title_buffer_size, char *error_buffer, int error_buffer_size)
{
    if (path == NULL || path[0] == '\0' || out_config == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid app config load arguments");
        return false;
    }

    char *base_dir = path_dirname(path);
    char *asset_name = path_basename(path);
    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    if (base_dir == NULL || asset_name == NULL || assets == NULL)
    {
        SDL_free(base_dir);
        SDL_free(asset_name);
        slayer3d_asset_resolver_destroy(assets);
        set_error(error_buffer, error_buffer_size, "failed to allocate app config loader");
        return false;
    }

    char asset_error[256];
    const bool mounted =
        slayer3d_asset_resolver_mount_directory(assets, base_dir, asset_error, (int)sizeof(asset_error));
    bool ok = false;
    if (!mounted)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to mount game data directory %s: %s",
                         base_dir, asset_error);
    }
    else
    {
        ok = slayer3d_game_data_load_app_config_asset(assets, asset_name, out_config, title_buffer, title_buffer_size,
                                                      error_buffer, error_buffer_size);
    }

    slayer3d_asset_resolver_destroy(assets);
    SDL_free(base_dir);
    SDL_free(asset_name);
    return ok;
}

bool slayer3d_game_data_load_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                   slayer3d_game_session *session, slayer3d_game_data_runtime **out_runtime,
                                   char *error_buffer, int error_buffer_size)
{
    slayer3d_game_data_load_options options;
    SDL_zero(options);
    options.session = session;
    return slayer3d_game_data_load_asset_with_options(assets, asset_path, &options, out_runtime, error_buffer,
                                                      error_buffer_size);
}

bool slayer3d_game_data_load_asset_with_options(slayer3d_asset_resolver *assets, const char *asset_path,
                                                const slayer3d_game_data_load_options *options,
                                                slayer3d_game_data_runtime **out_runtime, char *error_buffer,
                                                int error_buffer_size)
{
    if (out_runtime != NULL)
        *out_runtime = NULL;
    if (assets == NULL || asset_path == NULL || asset_path[0] == '\0' || options == NULL || options->session == NULL ||
        out_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid game data load arguments");
        return false;
    }

    slayer3d_game_data_source_map *source_map = NULL;
    yyjson_doc *doc =
        slayer3d_game_data_compose_asset(assets, asset_path, &source_map, error_buffer, error_buffer_size);
    if (doc == NULL)
        return false;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root) || SDL_strcmp(json_string(root, "schema", ""), "slayer3d.game.v0") != 0)
    {
        slayer3d_game_data_source_map_destroy(source_map);
        yyjson_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "unsupported or missing game data schema");
        return false;
    }

    slayer3d_game_data_runtime *runtime = (slayer3d_game_data_runtime *)SDL_calloc(1, sizeof(*runtime));
    if (runtime == NULL)
    {
        slayer3d_game_data_source_map_destroy(source_map);
        yyjson_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to allocate game data runtime");
        return false;
    }
    runtime->doc = doc;
    runtime->session = options->session;
    runtime->assets = assets;
    runtime->base_dir = path_dirname(asset_path_without_scheme(asset_path));
    runtime->scene_state = slayer3d_properties_create();
    runtime->rng_state = 0xC0FFEEu;
    if (runtime->base_dir == NULL || runtime->scene_state == NULL)
    {
        slayer3d_game_data_source_map_destroy(source_map);
        slayer3d_game_data_destroy(runtime);
        set_error(error_buffer, error_buffer_size, "failed to allocate game data runtime state");
        return false;
    }
    if (!slayer3d_game_data_validate_document_with_source_map(root, asset_path, runtime->base_dir, assets, source_map,
                                                              NULL, error_buffer, error_buffer_size))
    {
        slayer3d_game_data_source_map_destroy(source_map);
        slayer3d_game_data_destroy(runtime);
        return false;
    }
    slayer3d_game_data_source_map_destroy(source_map);
    if (!slayer3d_game_data_network_schema_hash(root, runtime->network_schema_hash, &runtime->has_network_schema))
    {
        slayer3d_game_data_destroy(runtime);
        set_error(error_buffer, error_buffer_size, "failed to compute network schema hash");
        return false;
    }
    load_storage_config(runtime, root);

    yyjson_val *logic = obj_get(root, "logic");
    load_active_camera(runtime, root);
    bool ok = load_signals(runtime, root, error_buffer, error_buffer_size) &&
              load_entities(runtime, root, error_buffer, error_buffer_size) &&
              load_grid_maps(runtime, root, error_buffer, error_buffer_size) &&
              load_grid_pickup_layers(runtime, root, error_buffer, error_buffer_size) &&
              load_sector_levels(runtime, root, error_buffer, error_buffer_size) &&
              load_brush_worlds(runtime, root, error_buffer, error_buffer_size) &&
              load_sector_doors(runtime, root, error_buffer, error_buffer_size) &&
              load_sector_platforms(runtime, root, error_buffer, error_buffer_size) &&
              load_actor_pools(runtime, root, error_buffer, error_buffer_size) &&
              load_input(runtime, root, error_buffer, error_buffer_size) &&
              load_timers(runtime, logic, error_buffer, error_buffer_size) && load_sensors(runtime, logic) &&
              load_wave_schedules(runtime, logic) && load_scripts(runtime, root, error_buffer, error_buffer_size) &&
              load_lua_adapters(runtime, root, error_buffer, error_buffer_size) &&
              load_bindings(runtime, logic, error_buffer, error_buffer_size) &&
              load_scenes(runtime, root, options, error_buffer, error_buffer_size);
    if (!ok)
    {
        slayer3d_game_data_destroy(runtime);
        return false;
    }

    *out_runtime = runtime;
    return true;
}

bool slayer3d_game_data_load_file(const char *path, slayer3d_game_session *session,
                                  slayer3d_game_data_runtime **out_runtime, char *error_buffer, int error_buffer_size)
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
    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    if (base_dir == NULL || asset_name == NULL || assets == NULL)
    {
        SDL_free(base_dir);
        SDL_free(asset_name);
        slayer3d_asset_resolver_destroy(assets);
        set_error(error_buffer, error_buffer_size, "failed to create game data asset resolver");
        return false;
    }

    char asset_error[256];
    const bool mounted =
        slayer3d_asset_resolver_mount_directory(assets, base_dir, asset_error, (int)sizeof(asset_error));
    if (!mounted)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to mount game data directory: %s",
                         asset_error);
        SDL_free(base_dir);
        SDL_free(asset_name);
        slayer3d_asset_resolver_destroy(assets);
        return false;
    }

    const bool ok =
        slayer3d_game_data_load_asset(assets, asset_name, session, out_runtime, error_buffer, error_buffer_size);
    if (ok && out_runtime != NULL && *out_runtime != NULL)
        (*out_runtime)->owns_assets = true;
    else
        slayer3d_asset_resolver_destroy(assets);
    SDL_free(base_dir);
    SDL_free(asset_name);
    return ok;
}

static void storage_config_from_root(yyjson_val *root, slayer3d_storage_config *out_config)
{
    if (out_config == NULL)
        return;
    slayer3d_storage_config_init(out_config);

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

static void load_storage_config(slayer3d_game_data_runtime *runtime, yyjson_val *root)
{
    if (runtime == NULL)
        return;
    storage_config_from_root(root, &runtime->storage_config);
}

bool slayer3d_game_data_get_storage_config(const slayer3d_game_data_runtime *runtime,
                                           slayer3d_storage_config *out_config)
{
    if (runtime == NULL || out_config == NULL)
        return false;

    *out_config = runtime->storage_config;
    return true;
}

#include "game_data/game_data_network_runtime.inc"

void slayer3d_game_data_destroy(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;

    slayer3d_signal_bus *bus = runtime_bus(runtime);
    for (int i = 0; i < runtime->binding_count; ++i)
    {
        if (runtime->bindings[i].connection_id != 0)
            slayer3d_signal_disconnect(bus, runtime->bindings[i].connection_id);
    }
    for (int i = 0; i < runtime->adapter_count; ++i)
    {
        SDL_free(runtime->adapters[i].name);
        SDL_free(runtime->adapters[i].lua_script_id);
        SDL_free(runtime->adapters[i].lua_function);
        slayer3d_script_engine_unref(runtime->scripts, runtime->adapters[i].lua_function_ref);
    }
    for (int i = 0; i < runtime->script_count; ++i)
    {
        slayer3d_script_engine_unref(runtime->scripts, runtime->script_entries[i].module_ref);
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
        slayer3d_properties_destroy(runtime->property_snapshots[i].properties);
    }
    for (int i = 0; i < runtime->collection_count; ++i)
    {
        SDL_free(runtime->collections[i].name);
        for (int row = 0; row < runtime->collections[i].row_count; ++row)
            slayer3d_properties_destroy(runtime->collections[i].rows[row]);
        SDL_free(runtime->collections[i].rows);
    }
    for (int i = 0; i < runtime->grid_map_count; ++i)
    {
        SDL_free(runtime->grid_maps[i].name);
        SDL_free(runtime->grid_maps[i].cells);
        SDL_free(runtime->grid_maps[i].walkable);
    }
    for (int i = 0; i < runtime->grid_actor_index_count; ++i)
    {
        SDL_free(runtime->grid_actor_indices[i].map);
        SDL_free(runtime->grid_actor_indices[i].pool);
        SDL_free(runtime->grid_actor_indices[i].actors);
    }
    for (int i = 0; i < runtime->grid_pickup_layer_count; ++i)
    {
        SDL_free(runtime->grid_pickup_layers[i].name);
        for (int kind_index = 0; kind_index < runtime->grid_pickup_layers[i].kind_count; ++kind_index)
            SDL_free(runtime->grid_pickup_layers[i].kinds[kind_index].kind);
        SDL_free(runtime->grid_pickup_layers[i].kinds);
        SDL_free(runtime->grid_pickup_layers[i].cells);
        SDL_free(runtime->grid_pickup_layers[i].render_positions);
    }
    for (int i = 0; i < runtime->sector_level_count; ++i)
    {
        SDL_free(runtime->sector_levels[i].name);
        SDL_free(runtime->sector_levels[i].sectors);
        SDL_free(runtime->sector_levels[i].sector_names);
        SDL_free(runtime->sector_levels[i].materials);
        SDL_free(runtime->sector_levels[i].lights);
        slayer3d_free_level(&runtime->sector_levels[i].lightmapped);
        slayer3d_free_level(&runtime->sector_levels[i].vertex_baked);
        slayer3d_free_level(&runtime->sector_levels[i].unlit);
        slayer3d_free_level(&runtime->sector_levels[i].lightmapped_without_sector_lighting);
        slayer3d_free_level(&runtime->sector_levels[i].vertex_baked_without_sector_lighting);
        slayer3d_free_level(&runtime->sector_levels[i].unlit_without_sector_lighting);
    }
    for (int i = 0; i < runtime->brush_world_count; ++i)
    {
        slayer3d_game_data_brush_world *world = &runtime->brush_worlds[i].desc;
        slayer3d_free_model(&runtime->brush_worlds[i].render_model);
        free_editor_metadata(&world->editor);
        SDL_free((void *)world->name);
        SDL_free((void *)world->units);
        for (int material_index = 0; material_index < world->material_count; ++material_index)
        {
            slayer3d_game_data_brush_material *material =
                (slayer3d_game_data_brush_material *)&world->materials[material_index];
            free_editor_metadata(&material->editor);
            SDL_free((void *)material->name);
            SDL_free((void *)material->texture);
        }
        for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
        {
            slayer3d_game_data_brush *brush = (slayer3d_game_data_brush *)&world->brushes[brush_index];
            SDL_free((void *)brush->name);
            free_editor_metadata(&brush->editor);
            for (int tag_index = 0; tag_index < brush->tag_count; ++tag_index)
                SDL_free((void *)brush->tags[tag_index]);
            SDL_free((void *)brush->tags);
            for (int face_index = 0; face_index < brush->face_count; ++face_index)
            {
                slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[face_index];
                free_editor_metadata(&face->editor);
            }
            SDL_free((void *)brush->faces);
        }
        SDL_free((void *)world->materials);
        SDL_free((void *)world->brushes);
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
        slayer3d_network_session_destroy(runtime->direct_connect_sessions[i].session);
    }
    for (int i = 0; i < runtime->host_session_count; ++i)
    {
        SDL_free(runtime->host_sessions[i].name);
        slayer3d_network_session_destroy(runtime->host_sessions[i].session);
    }
    for (int i = 0; i < runtime->discovery_session_count; ++i)
    {
        SDL_free(runtime->discovery_sessions[i].name);
        slayer3d_network_discovery_session_destroy(runtime->discovery_sessions[i].session);
    }
    for (int i = 0; i < runtime->network_diagnostic_count; ++i)
        SDL_free(runtime->network_diagnostics[i].name);

    clear_menu_text_entry_capture(runtime);
    slayer3d_script_engine_destroy(runtime->scripts);
    slayer3d_properties_destroy(runtime->scene_state);
    slayer3d_storage_destroy(runtime->storage);
    if (runtime->owns_assets)
        slayer3d_asset_resolver_destroy(runtime->assets);
    SDL_free(runtime->base_dir);
    SDL_free(runtime->scenes);
    SDL_free(runtime->script_entries);
    SDL_free(runtime->signals);
    SDL_free(runtime->timers);
    SDL_free(runtime->actions);
    SDL_free(runtime->adapters);
    SDL_free(runtime->bindings);
    for (int i = 0; i < runtime->sensor_count; ++i)
    {
        for (int pair_index = 0; pair_index < runtime->sensors[i].contact_pair_count; ++pair_index)
            sensor_contact_pair_destroy(&runtime->sensors[i].contact_pairs[pair_index]);
        SDL_free(runtime->sensors[i].contact_pairs);
    }
    SDL_free(runtime->sensors);
    SDL_free(runtime->wave_schedules);
    SDL_free(runtime->noise_events);
    SDL_free(runtime->input_bindings);
    SDL_free(runtime->ui_states);
    SDL_free(runtime->animations);
    SDL_free(runtime->audio_files);
    SDL_free(runtime->property_snapshots);
    SDL_free(runtime->collections);
    SDL_free(runtime->grid_maps);
    SDL_free(runtime->grid_actor_indices);
    SDL_free(runtime->grid_pickup_layers);
    SDL_free(runtime->sector_levels);
    SDL_free(runtime->brush_worlds);
    SDL_free(runtime->sector_doors);
    SDL_free(runtime->sector_platforms);
    SDL_free(runtime->fps_controllers);
    SDL_free(runtime->patrol_controllers);
    SDL_free(runtime->actor_pools);
    SDL_free(runtime->direct_connect_sessions);
    SDL_free(runtime->host_sessions);
    SDL_free(runtime->discovery_sessions);
    SDL_free(runtime->network_diagnostics);
    SDL_free(runtime->activity.periodic_elapsed);
    yyjson_doc_free(runtime->doc);
    SDL_free(runtime);
}

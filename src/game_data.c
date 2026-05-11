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

#include "game_data/game_data_lua_api.inc"

#include "game_data/game_data_json_helpers.inc"

#include "game_data/game_data_grid_runtime.inc"

#include "game_data/game_data_world_loaders.inc"

#include "game_data/game_data_scene_lookup.inc"

#include "game_data/game_data_replication_helpers.inc"

#include "game_data/game_data_input_device_helpers.inc"

#include "game_data/game_data_adapter_helpers.inc"

#include "game_data/game_data_action_lookup.inc"

#include "game_data/game_data_render_runtime.inc"

#include "game_data/game_data_world_queries.inc"

#include "game_data/game_data_ui_animation_runtime.inc"

#include "game_data/game_data_scene_flow_runtime.inc"

#include "game_data/game_data_runtime_collections.inc"

#include "game_data/game_data_network_sessions.inc"

#include "game_data/game_data_menu_ui.inc"

#include "game_data/game_data_actors_input.inc"

#include "game_data/game_data_scripts.inc"

#include "game_data/game_data_actions.inc"

#include "game_data/game_data_scene_load_runtime.inc"

#include "game_data/game_data_controller_binding_helpers.inc"

#include "game_data/game_data_update_runtime.inc"

#include "game_data/game_data_load_runtime.inc"

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

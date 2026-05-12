#ifndef SLAYER3D_GAME_DATA_INTERNAL_H
#define SLAYER3D_GAME_DATA_INTERNAL_H

#include <stdbool.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "script_internal.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/asset.h"
#include "slayer3d/door.h"
#include "slayer3d/fps_mover.h"
#include "slayer3d/game_data.h"
#include "slayer3d/input.h"
#include "slayer3d/level.h"
#include "slayer3d/network.h"
#include "slayer3d/network_replication.h"
#include "slayer3d/properties.h"
#include "slayer3d/script.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/timer_pool.h"
#include "yyjson.h"

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
    Uint64 world_model_trace_count;
    Uint64 world_model_point_query_count;
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

void set_error(char *buffer, int buffer_size, const char *message);
void set_errorf(char *buffer, int buffer_size, const char *format, ...);
char *path_join(const char *base_dir, const char *path);

yyjson_val *obj_get(yyjson_val *object, const char *key);
const char *json_string(yyjson_val *object, const char *key, const char *fallback);
bool json_bool(yyjson_val *object, const char *key, bool fallback);
float json_float(yyjson_val *object, const char *key, float fallback);
int json_int(yyjson_val *object, const char *key, int fallback);
const char *first_non_empty_string(const char *first, const char *second, const char *fallback);
char first_json_string_char(yyjson_val *object, const char *key, char fallback);
int json_int_or_string(yyjson_val *object, const char *key, int fallback);
bool json_float_array(yyjson_val *value, float *out_values, int count, const float *fallback);
slayer3d_color json_color_value(yyjson_val *value, slayer3d_color fallback);
slayer3d_color json_color(yyjson_val *object, const char *key, slayer3d_color fallback);
bool json_vec2_value(yyjson_val *value, float fallback_x, float fallback_y, float *out_x, float *out_y);
slayer3d_vec3 json_vec3_value(yyjson_val *value, slayer3d_vec3 fallback);
slayer3d_vec3 json_vec3(yyjson_val *object, const char *key, slayer3d_vec3 fallback);
slayer3d_vec4 json_vec4_value(yyjson_val *value, slayer3d_vec4 fallback);
slayer3d_vec4 json_vec4(yyjson_val *object, const char *key, slayer3d_vec4 fallback);
const char *scene_state_string(const slayer3d_game_data_runtime *runtime, const char *key, const char *fallback);
bool scene_state_bool(const slayer3d_game_data_runtime *runtime, const char *key, bool fallback);
float scene_state_float(const slayer3d_game_data_runtime *runtime, const char *key, float fallback);

slayer3d_actor_registry *runtime_registry(const slayer3d_game_data_runtime *runtime);
slayer3d_signal_bus *runtime_bus(const slayer3d_game_data_runtime *runtime);
slayer3d_timer_pool *runtime_timers(const slayer3d_game_data_runtime *runtime);
slayer3d_input_manager *runtime_input(const slayer3d_game_data_runtime *runtime);
yyjson_val *runtime_root(const slayer3d_game_data_runtime *runtime);
scene_entry *active_scene_entry(slayer3d_game_data_runtime *runtime);
const scene_entry *active_scene_entry_const(const slayer3d_game_data_runtime *runtime);
scene_entry *find_scene(slayer3d_game_data_runtime *runtime, const char *name);
const scene_entry *find_scene_const(const slayer3d_game_data_runtime *runtime, const char *name);
scene_menu_state *find_scene_menu(scene_entry *scene, const char *name);
const scene_menu_state *find_scene_menu_const(const scene_entry *scene, const char *name);
sector_door_runtime *find_sector_door(slayer3d_game_data_runtime *runtime, const char *name);
bool sector_door_in_scene(const sector_door_runtime *door, const char *scene_name);
bool sector_door_in_active_scene(const slayer3d_game_data_runtime *runtime, const sector_door_runtime *door);
bool sector_platform_in_scene(const sector_platform_runtime *platform, const char *scene_name);
bool sector_platform_in_active_scene(const slayer3d_game_data_runtime *runtime,
                                     const sector_platform_runtime *platform);
void apply_scene_camera(slayer3d_game_data_runtime *runtime, const scene_entry *scene);
void emit_scene_enter_signal(slayer3d_game_data_runtime *runtime, const scene_entry *scene, const char *from_scene,
                             const slayer3d_properties *payload);
yyjson_val *find_entity_json(const slayer3d_game_data_runtime *runtime, const char *name);
yyjson_val *find_component_json(yyjson_val *entity, const char *type);
yyjson_val *find_actor_definition_json(const slayer3d_game_data_runtime *runtime, const char *actor_name);
yyjson_val *find_font_json(const slayer3d_game_data_runtime *runtime, const char *id);
yyjson_val *find_image_json(const slayer3d_game_data_runtime *runtime, const char *id);
yyjson_val *find_model_json(const slayer3d_game_data_runtime *runtime, const char *id);
yyjson_val *find_sound_json(const slayer3d_game_data_runtime *runtime, const char *id);
yyjson_val *find_music_json(const slayer3d_game_data_runtime *runtime, const char *id);
yyjson_val *find_ambient_json(const slayer3d_game_data_runtime *runtime, const char *id);
yyjson_val *find_sprite_json(const slayer3d_game_data_runtime *runtime, const char *id);
yyjson_val *find_camera_json(const slayer3d_game_data_runtime *runtime, const char *name);
const char *find_action_name(const slayer3d_game_data_runtime *runtime, int action_id);
bool runtime_actor_is_active(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *actor);
bool active_scene_has_entity_internal(const slayer3d_game_data_runtime *runtime, const char *entity_name);
bool entity_json_has_tags(yyjson_val *entity, const char *const *tags, int tag_count);
bool entity_json_has_all_tags_from_json(yyjson_val *entity, yyjson_val *tags);
const actor_pool_runtime *find_actor_pool_for_actor_const(const slayer3d_game_data_runtime *runtime,
                                                          const char *actor_name, int *out_index);
actor_pool_runtime *find_actor_pool(slayer3d_game_data_runtime *runtime, const char *name);
bool actor_pool_in_scene(const actor_pool_runtime *pool, const char *scene_name);
slayer3d_registered_actor *actor_pool_allocate(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                               int *out_index);
void actor_pool_set_lifecycle_state(actor_pool_runtime *pool, slayer3d_registered_actor *actor, int index,
                                    actor_lifecycle_state state);
bool actor_pool_actor_is_active(const actor_pool_runtime *pool, const slayer3d_registered_actor *actor, int index);
void actor_pool_note_spawn_attempt(actor_pool_runtime *pool);
void actor_pool_note_spawn_success(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool);
void actor_pool_note_spawn_failure(actor_pool_runtime *pool, const char *reason);
bool initialize_pooled_actor(actor_pool_runtime *pool, slayer3d_registered_actor *actor, int index, bool active);
bool actor_pool_request_despawn(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                slayer3d_registered_actor *actor, int index, const char *reason);
void apply_actor_spawn_properties(slayer3d_registered_actor *actor, yyjson_val *properties);
void actor_set_position(slayer3d_registered_actor *actor, slayer3d_vec3 position);
bool execute_grid_spawn_from_glyphs_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool execute_grid_spawn_runs_from_glyphs_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool execute_grid_pickup_layer_reset_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
slayer3d_vec3 actor_vec_property(const slayer3d_registered_actor *actor, const char *key);
float actor_numeric_property(const slayer3d_registered_actor *actor, const char *key, float fallback);
void copy_property_value(slayer3d_properties *target, const char *key, const slayer3d_value *value);
slayer3d_audio_bus parse_audio_bus(const char *bus, slayer3d_audio_bus fallback);
slayer3d_backend parse_backend(const char *value, slayer3d_backend fallback);
slayer3d_window_mode parse_window_mode(const char *value, slayer3d_window_mode fallback);
slayer3d_tonemap_mode parse_tonemap(const char *value, slayer3d_tonemap_mode fallback);
bool parse_render_profile(const char *value, slayer3d_render_profile *out_profile);
float camera_fov_degrees(const slayer3d_game_data_runtime *runtime, yyjson_val *camera_json, float fallback);
slayer3d_camera_fov_axis camera_fov_axis(const slayer3d_game_data_runtime *runtime, yyjson_val *camera_json);
slayer3d_transition_type parse_transition_type(const char *value, slayer3d_transition_type fallback);
slayer3d_transition_direction parse_transition_direction(const char *value, slayer3d_transition_direction fallback);
slayer3d_builtin_font parse_builtin_font(const char *value, slayer3d_builtin_font fallback);
slayer3d_game_data_ui_align parse_ui_align(const char *value, slayer3d_game_data_ui_align fallback);
slayer3d_game_data_ui_valign parse_ui_valign(const char *value, slayer3d_game_data_ui_valign fallback);
const char *parse_ui_image_effect(const char *value);
int axis_index(const char *axis);
float vec_axis(slayer3d_vec3 value, int axis);
void set_vec_axis(slayer3d_vec3 *value, int axis, float component);
fps_controller_runtime *find_fps_controller(slayer3d_game_data_runtime *runtime, const char *entity_name);
const fps_controller_runtime *find_fps_controller_const(const slayer3d_game_data_runtime *runtime,
                                                        const char *entity_name);
fps_controller_runtime *find_or_add_fps_controller(slayer3d_game_data_runtime *runtime, const char *entity_name,
                                                   yyjson_val *component);
patrol_controller_runtime *find_patrol_controller(slayer3d_game_data_runtime *runtime, const char *entity_name);
patrol_controller_runtime *find_or_add_patrol_controller(slayer3d_game_data_runtime *runtime, const char *entity_name,
                                                         yyjson_val *component);
float game_data_random01(slayer3d_game_data_runtime *runtime);
int action_signal_id(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *key);
bool execute_one_action(slayer3d_game_data_runtime *runtime, yyjson_val *action, const slayer3d_properties *payload);
bool execute_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions, const slayer3d_properties *payload);
bool execute_optional_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                   const slayer3d_properties *payload);
bool eval_data_condition(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                         const slayer3d_game_data_ui_metrics *metrics);
void emit_optional_signal(slayer3d_game_data_runtime *runtime, yyjson_val *json, const char *signal_key,
                          const slayer3d_properties *payload);
void actor_lifecycle_defer_begin(slayer3d_game_data_runtime *runtime);
void actor_lifecycle_defer_end(slayer3d_game_data_runtime *runtime);
bool json_scalar_to_value(yyjson_val *json, slayer3d_value *out_value);
bool set_property_from_value(slayer3d_properties *props, const char *key, const slayer3d_value *value);
bool actor_matches_target_filter(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *target,
                                 const slayer3d_registered_actor *source, yyjson_val *json,
                                 const slayer3d_properties *payload, const char *fallback_tag,
                                 bool fallback_exclude_source);
bool apply_combat_damage_to_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                  const slayer3d_properties *payload, slayer3d_registered_actor *actor, float amount);
bool sensor_actor_list_add(sensor_actor_list *list, slayer3d_registered_actor *actor);
void sensor_actor_list_free(sensor_actor_list *list);
bool collect_sensor_endpoint_actors(slayer3d_game_data_runtime *runtime, const char *actor_name, const char *tag,
                                    sensor_actor_list *out_list);
void sensor_contact_pair_destroy(sensor_contact_pair_state *state);
int actor_sector_index_for_sensor(const sector_level_runtime *level, const sensor_entry *sensor,
                                  const slayer3d_registered_actor *actor);
bool collect_effect_targets(slayer3d_game_data_runtime *runtime, const char *tag, sensor_actor_list *out_list);
float sector_door_distance_sq_xz(const slayer3d_door *door, slayer3d_vec3 point);
bool sector_door_is_in_front(const slayer3d_door *door, slayer3d_vec3 point, float yaw, float min_dot);
void update_sector_doors(slayer3d_game_data_runtime *runtime, float dt);
bool update_sector_platforms(slayer3d_game_data_runtime *runtime, float dt);
void update_control_components(slayer3d_game_data_runtime *runtime, yyjson_val *root, float dt);
void update_motion_components(slayer3d_game_data_runtime *runtime, yyjson_val *root, float dt);
void update_sensors(slayer3d_game_data_runtime *runtime);
void update_patrol_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                              slayer3d_registered_actor *actor, float dt);
void update_fps_sector_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                  slayer3d_registered_actor *actor, const slayer3d_input_manager *input, float dt);
void update_fps_brush_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                 slayer3d_registered_actor *actor, const slayer3d_input_manager *input, float dt);
bool update_brush_velocity_motion(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                  slayer3d_registered_actor *actor, int actor_id, int pool_index, int actor_index,
                                  float dt);
bool execute_fps_controller_launch_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                          const slayer3d_properties *payload);
bool execute_fps_controller_teleport_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload);
bool execute_projectile_fire_action_for_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              const slayer3d_properties *payload,
                                              slayer3d_registered_actor *source_actor);
void weapon_complete_reload(slayer3d_registered_actor *actor, yyjson_val *json);
const grid_map_runtime *find_grid_map(const slayer3d_game_data_runtime *runtime, const char *name);
bool grid_map_normalize_cell(const grid_map_runtime *map, int *col, int *row);
char grid_map_cell(const grid_map_runtime *map, int col, int row);
bool grid_map_is_walkable(const grid_map_runtime *map, int col, int row);
bool grid_map_cell_to_world(const grid_map_runtime *map, int col, int row, slayer3d_vec3 *out_position);
bool grid_map_world_to_cell(const grid_map_runtime *map, float x, float y, int *out_col, int *out_row);
bool grid_map_next_step(const grid_map_runtime *map, int start_col, int start_row, int goal_col, int goal_row,
                        int *out_col, int *out_row);
void grid_actor_index_clear(slayer3d_game_data_runtime *runtime, const grid_map_runtime *map, const char *pool_name);
bool grid_actor_index_register(slayer3d_game_data_runtime *runtime, const grid_map_runtime *map, const char *pool_name,
                               slayer3d_registered_actor *actor, int col, int row);
slayer3d_registered_actor *grid_actor_index_find(slayer3d_game_data_runtime *runtime, const char *map_name,
                                                 const char *pool_name, int col, int row);
grid_pickup_layer_runtime *find_grid_pickup_layer(slayer3d_game_data_runtime *runtime, const char *name);
bool grid_pickup_layer_reset(slayer3d_game_data_runtime *runtime, grid_pickup_layer_runtime *layer);
bool grid_pickup_layer_collect_at(slayer3d_game_data_runtime *runtime, grid_pickup_layer_runtime *layer, int col,
                                  int row, grid_pickup_kind_runtime *out_kind);
const runtime_collection *find_runtime_collection_const(const slayer3d_game_data_runtime *runtime,
                                                        const char *collection_name);
bool runtime_collection_field_to_string(const runtime_collection *collection, int row_index, const char *field_name,
                                        char *buffer, size_t buffer_size);
sector_level_runtime *find_sector_level_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name);
const sector_level_runtime *find_sector_level_runtime(const slayer3d_game_data_runtime *runtime, const char *name);
int sector_level_find_sector_name(const sector_level_runtime *level, const char *sector_name);
const brush_world_runtime *find_brush_world_runtime(const slayer3d_game_data_runtime *runtime, const char *name);
bool load_grid_maps(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_grid_pickup_layers(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                             int error_buffer_size);
bool load_sector_levels(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                        int error_buffer_size);
bool load_brush_worlds(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                       int error_buffer_size);
bool load_sector_doors(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                       int error_buffer_size);
bool load_sector_platforms(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                           int error_buffer_size);
void free_editor_metadata(slayer3d_game_data_editor_metadata *metadata);
unsigned int brush_content_flag_from_string(const char *name);
unsigned int brush_flags_from_json(yyjson_val *value, unsigned int (*flag_from_string)(const char *name),
                                   unsigned int fallback);
slayer3d_sector *copy_sectors_without_sector_lighting(const sector_level_runtime *level);
bool build_sector_level_variant_set(sector_level_runtime *level, const slayer3d_sector *sectors,
                                    slayer3d_level *out_lightmapped, slayer3d_level *out_vertex_baked,
                                    slayer3d_level *out_unlit, const char *stage, char *error_buffer,
                                    int error_buffer_size);
bool set_sector_level_geometry(sector_level_runtime *level, int sector_index, const slayer3d_sector_geometry *geometry,
                               char *error_buffer, int error_buffer_size);
void modulate_color_by_sector_lighting(slayer3d_color *color, const slayer3d_sector *sector);

SDL_Scancode scancode_from_json(const char *name);
const char *scancode_display_name(SDL_Scancode scancode);
Uint8 mouse_button_from_json(const char *name);
const char *mouse_button_display_name(Uint8 button);
slayer3d_mouse_axis mouse_axis_from_json(const char *name, bool *valid);
const char *gamepad_button_display_name(SDL_GamepadButton button);
SDL_GamepadAxis gamepad_axis_from_json(const char *name);
SDL_GamepadButton gamepad_button_from_json(const char *name);

int fps_controller_action_id(const slayer3d_game_data_runtime *runtime, yyjson_val *component, const char *name);
float fps_controller_action_value(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                  int action_id);
bool fps_controller_action_pressed(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                   int action_id);
slayer3d_actor_patrol_mode parse_patrol_mode(const char *value);
int patrol_signal_id(const slayer3d_game_data_runtime *runtime, yyjson_val *component, const char *name);

int find_timer_index(const slayer3d_game_data_runtime *runtime, const char *name);
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
bool load_timers(slayer3d_game_data_runtime *runtime, yyjson_val *logic, char *error_buffer, int error_buffer_size);
bool load_script_index_into_engine(slayer3d_game_data_runtime *runtime, slayer3d_asset_resolver *assets,
                                   slayer3d_script_engine *engine, int index, slayer3d_script_ref *module_refs,
                                   bool *loading, bool *loaded, char *error_buffer, int error_buffer_size);
bool load_scripts(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_lua_adapters(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                       int error_buffer_size);

#endif

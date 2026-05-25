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

typedef struct slayer3d_replication_field_descriptor slayer3d_replication_field_descriptor;

#define SLAYER3D_GAME_DATA_SIGNAL_BASE 20000
#define SLAYER3D_GAME_DATA_MENU_TEXT_MAX_BYTES 255
#define SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_MAGIC 0x53335253u /* "S3RS" */
#define SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_VERSION 1u
#define SLAYER3D_GAME_DATA_NETWORK_INPUT_MAGIC 0x49335253u /* "S3RI" */
#define SLAYER3D_GAME_DATA_NETWORK_INPUT_VERSION 1u
#define SLAYER3D_GAME_DATA_NETWORK_CONTROL_MAGIC 0x43335253u /* "S3RC" */
#define SLAYER3D_GAME_DATA_NETWORK_CONTROL_VERSION 1u
#define SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS 4

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

typedef struct editor_command_preview_state
{
    bool active;
    const char *scene;
    const char *command;
    const char *target;
    const char *world_name;
    const char *element_name;
    const char *element_stable_id;
    const char *material_name;
    const char *previous_material_name;
    const char *face_stable_id;
    int face_index;
    int material_index;
    int previous_material_index;
    yyjson_val *outputs;
    slayer3d_vec3 offset;
    bool has_bounds;
    slayer3d_bounding_box bounds;
} editor_command_preview_state;

typedef struct editor_placement_preview_state
{
    bool active;
    const char *scene;
    const char *mode;
    const char *kind;
    const char *axis;
    const char *world_name;
    const char *material_name;
    unsigned int contents;
    slayer3d_vec3 anchor;
    float snap;
    bool has_bounds;
    slayer3d_bounding_box bounds;
    bool has_source_candidate;
    int source_min[3];
    int source_max[3];
    int source_positive_overlap_count;
    char source_warning[256];
} editor_placement_preview_state;

typedef struct editor_drag_create_state
{
    bool active;
    bool moved;
    const char *scene;
    const char *world_name;
    const char *material_name;
    unsigned int contents;
    float grid_size;
    int start_cell[3];
    int current_cell[3];
    int source_min[3];
    int source_max[3];
} editor_drag_create_state;

typedef struct editor_drag_move_state
{
    bool active;
    bool moved;
    bool axis_lock_y;
    bool face_resize;
    bool vertex_lasso;
    bool lasso_additive;
    const char *scene;
    slayer3d_vec3 start_point;
    slayer3d_vec3 applied_offset;
    slayer3d_game_data_editor_selection face_selection;
    float grid_size;
    float start_mouse_x;
    float start_mouse_y;
    float current_mouse_x;
    float current_mouse_y;
} editor_drag_move_state;

typedef struct editor_camera_orbit_state
{
    bool active;
    slayer3d_vec3 pivot;
    float radius;
} editor_camera_orbit_state;

typedef struct editor_brush_source_box_runtime
{
    char *stable_id;
    char *name;
    char *prefab;
    char *material;
    char *face_materials[6];
    int min[3];
    int max[3];
    int vertex_count;
    int vertices[16][3];
    unsigned int contents;
} editor_brush_source_box_runtime;

#define SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT 8
#define SLAYER3D_EDITOR_SOURCE_BOX_EDGE_COUNT 12
#define SLAYER3D_EDITOR_SOURCE_BOX_FACE_COUNT 6
#define SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY 16
#define SLAYER3D_EDITOR_SOURCE_CONVEX_EDGE_CAPACITY 64
#define SLAYER3D_EDITOR_SOURCE_CONVEX_FACE_CAPACITY 32
#define SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX 320

typedef struct editor_brush_source_vertex
{
    int brush_index;
    int vertex_index;
    int coord[3];
    char stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
} editor_brush_source_vertex;

typedef struct editor_brush_source_edge
{
    int brush_index;
    int edge_index;
    int vertex_indices[2];
    char stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
} editor_brush_source_edge;

typedef struct editor_brush_source_face_ref
{
    int brush_index;
    int face_index;
    int vertex_indices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int vertex_count;
    char stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
} editor_brush_source_face_ref;

typedef struct editor_brush_source_vertex_model
{
    int brush_index;
    char brush_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
    int vertex_count;
    editor_brush_source_vertex vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int edge_count;
    editor_brush_source_edge edges[SLAYER3D_EDITOR_SOURCE_CONVEX_EDGE_CAPACITY];
    int face_count;
    editor_brush_source_face_ref faces[SLAYER3D_EDITOR_SOURCE_CONVEX_FACE_CAPACITY];
} editor_brush_source_vertex_model;

typedef struct editor_brush_source_shared_vertex
{
    int coord[3];
    int first_brush_index;
    int first_vertex_index;
    int reference_count;
} editor_brush_source_shared_vertex;

typedef struct editor_brush_source_vertex_diagnostics
{
    bool valid;
    int brush_count;
    int vertex_count;
    int edge_count;
    int face_count;
    int shared_vertex_count;
    int off_snap_count;
    int degenerate_count;
    int concave_count;
    int non_finite_count;
    char first_issue[256];
    char first_issue_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
} editor_brush_source_vertex_diagnostics;

typedef struct editor_brush_source_prefab_desc
{
    const char *prefab;
    const char *material;
    const char *axis;
    unsigned int contents;
    slayer3d_vec3 anchor;
    bool use_grid_bounds;
    slayer3d_vec3 min;
    slayer3d_vec3 max;
    slayer3d_vec3 grid_min;
    slayer3d_vec3 grid_max;
    float grid_size;
} editor_brush_source_prefab_desc;

typedef struct editor_brush_source_prefab_result
{
    bool valid;
    bool no_op;
    char brush_name[256];
    slayer3d_bounding_box bounds;
    int source_min[3];
    int source_max[3];
    int positive_overlap_count;
    char warning[256];
} editor_brush_source_prefab_result;

#define SLAYER3D_EDITOR_COMMAND_HISTORY_CAPACITY 32
#define SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY 512
#define SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY 512

typedef struct editor_source_vertex_selection
{
    char world_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char brush_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char brush_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
    char vertex_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
    int source_index;
    int vertex_index;
    int coord[3];
} editor_source_vertex_selection;

typedef struct editor_source_box_bounds_update
{
    int source_index;
    int min[3];
    int max[3];
} editor_source_box_bounds_update;

typedef enum editor_brush_source_vertex_operation_type
{
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_ADD,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_DELETE,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_DELETE_MANY,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MERGE,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MERGE_MANY_TO_TARGET,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_SNAP,
} editor_brush_source_vertex_operation_type;

typedef struct editor_brush_source_vertex_operation_desc
{
    const char *brush_identity;
    editor_brush_source_vertex_operation_type type;
    int vertex_index;
    int target_vertex_index;
    int vertex_indices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int vertex_index_count;
    int coord[3];
    int snap_units;
} editor_brush_source_vertex_operation_desc;

typedef struct editor_brush_source_vertex_operation_result
{
    bool valid;
    int vertex_count;
    int changed_count;
    int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    int face_count;
    char diagnostic[256];
    slayer3d_game_data_brush brush;
} editor_brush_source_vertex_operation_result;

typedef struct editor_command_transaction_entry
{
    int id;
    const char *scene;
    const char *command;
    const char *target;
    const char *world_name;
    const char *element_name;
    const char *element_stable_id;
    const char *material_name;
    const char *previous_material_name;
    const char *face_stable_id;
    int face_index;
    int material_index;
    int previous_material_index;
    slayer3d_vec3 offset;
    int rotation_quarter_turns;
    bool has_bounds;
    slayer3d_bounding_box bounds;
    int brush_index;
    bool has_source_box_snapshot;
    editor_brush_source_box_runtime source_box_snapshot;
    char message[128];
} editor_command_transaction_entry;

typedef struct editor_command_history_state
{
    editor_command_transaction_entry entries[SLAYER3D_EDITOR_COMMAND_HISTORY_CAPACITY];
    int count;
    int cursor;
    int next_id;
} editor_command_history_state;

typedef struct editor_player_start_runtime
{
    char *name;
    char *scene;
    char *target;
    slayer3d_vec3 position;
    float yaw;
    float pitch;
} editor_player_start_runtime;

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

typedef struct brush_world_compile_artifacts
{
    slayer3d_model render_model;
    slayer3d_model *brush_render_models;
    int brush_render_model_count;
    slayer3d_model *chunk_render_models;
    int chunk_render_model_count;
    slayer3d_bounding_box visibility_grid_bounds;
    float visibility_cell_size;
    int visibility_grid_dim_x;
    int visibility_grid_dim_y;
    int visibility_grid_dim_z;
    int visibility_grid_cell_count;
    Uint8 *visibility_grid_solid;
    Uint8 *visibility_grid_visible_cache[SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS];
    int visibility_grid_visible_cache_start[SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS];
    Uint64 visibility_grid_visible_cache_tick[SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS];
    Uint64 visibility_grid_visible_cache_clock;
} brush_world_compile_artifacts;

typedef struct brush_world_runtime
{
    slayer3d_game_data_brush_world desc;
    brush_world_compile_artifacts artifacts;
    editor_brush_source_box_runtime *editor_source_boxes;
    int editor_source_box_count;
    int editor_source_box_capacity;
    float editor_source_meters_per_unit;
    int editor_source_snap_units;
    bool editor_has_source_model;
    char *editor_source_path;
    Uint64 editor_revision;
    Uint64 editor_saved_revision;
    bool editor_dirty;
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
    editor_player_start_runtime *editor_player_starts;
    int editor_player_start_count;
    int editor_player_start_capacity;
    char *editor_player_start_source_path;
    Uint64 editor_player_start_revision;
    Uint64 editor_player_start_saved_revision;
    bool editor_player_start_dirty;
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
    slayer3d_game_data_editor_selection editor_active_selection;
    const char *editor_selection_scene;
    slayer3d_game_data_editor_selection editor_selected_brushes[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
    int editor_selected_brush_count;
    const char *editor_selected_brush_scene;
    editor_source_vertex_selection editor_selected_vertices[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    int editor_selected_vertex_count;
    const char *editor_selected_vertex_scene;
    editor_command_preview_state editor_command_preview;
    editor_placement_preview_state editor_placement_preview;
    editor_drag_create_state editor_drag_create;
    editor_drag_move_state editor_drag_move;
    editor_camera_orbit_state editor_camera_orbit;
    editor_command_history_state editor_command_history;
    scene_activity_state activity;
    float current_dt;
    unsigned int rng_state;
} slayer3d_game_data_runtime;

void set_error(char *buffer, int buffer_size, const char *message);
void set_errorf(char *buffer, int buffer_size, const char *format, ...);
bool append_format(char *buffer, size_t buffer_size, size_t *offset, const char *format, ...);
bool editor_save_bytes_atomic(const char *path, const void *data, size_t size, const char *kind, char *error_buffer,
                              int error_buffer_size);
char *path_join(const char *base_dir, const char *path);
char *path_dirname(const char *path);

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
int scene_state_int(const slayer3d_game_data_runtime *runtime, const char *key, int fallback);
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
const actor_pool_runtime *find_actor_pool_const(const slayer3d_game_data_runtime *runtime, const char *name);
actor_pool_runtime *find_actor_pool_for_actor(slayer3d_game_data_runtime *runtime, const char *actor_name,
                                              int *out_index);
bool actor_pool_in_scene(const actor_pool_runtime *pool, const char *scene_name);
slayer3d_registered_actor *actor_pool_allocate(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                               int *out_index);
actor_lifecycle_state actor_pool_lifecycle_state(const actor_pool_runtime *pool, int index);
void actor_pool_set_lifecycle_state(actor_pool_runtime *pool, slayer3d_registered_actor *actor, int index,
                                    actor_lifecycle_state state);
bool actor_pool_actor_is_active(const actor_pool_runtime *pool, const slayer3d_registered_actor *actor, int index);
bool actor_pool_actor_is_available(const actor_pool_runtime *pool, const slayer3d_registered_actor *actor, int index);
int actor_pool_active_count(const slayer3d_game_data_runtime *runtime, const actor_pool_runtime *pool);
int actor_pool_available_count(const slayer3d_game_data_runtime *runtime, const actor_pool_runtime *pool);
void actor_pool_copy_reason(char *target, size_t target_size, const char *reason);
void actor_pool_note_spawn_attempt(actor_pool_runtime *pool);
void actor_pool_note_spawn_success(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool);
void actor_pool_note_spawn_failure(actor_pool_runtime *pool, const char *reason);
bool initialize_pooled_actor(actor_pool_runtime *pool, slayer3d_registered_actor *actor, int index, bool active);
bool actor_pool_initialize_slot(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool, int index, bool active);
bool actor_pool_request_despawn(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                slayer3d_registered_actor *actor, int index, const char *reason);
slayer3d_registered_actor *actor_from_payload_key(slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_properties *payload, const char *key);
slayer3d_vec3 actor_spawn_position_from_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                               const slayer3d_properties *payload, slayer3d_vec3 fallback,
                                               const slayer3d_registered_actor *source_actor);
void apply_actor_spawn_properties(slayer3d_registered_actor *actor, yyjson_val *properties);
void actor_set_position(slayer3d_registered_actor *actor, slayer3d_vec3 position);
bool execute_grid_spawn_from_glyphs_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool execute_grid_spawn_runs_from_glyphs_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool execute_grid_pickup_layer_reset_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool execute_sector_door_state_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                      const slayer3d_properties *payload, const char *kind);
bool execute_sector_door_interact_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool execute_noise_emit_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                               const slayer3d_properties *payload);
slayer3d_registered_actor *action_target_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                               const slayer3d_properties *payload);
bool execute_persistence_action(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *type);
bool execute_audio_action(slayer3d_game_data_runtime *runtime, yyjson_val *action, const slayer3d_properties *payload,
                          const char *type);
slayer3d_vec3 actor_vec_property(const slayer3d_registered_actor *actor, const char *key);
float actor_numeric_property(const slayer3d_registered_actor *actor, const char *key, float fallback);
void copy_property_value(slayer3d_properties *target, const char *key, const slayer3d_value *value);
void set_actor_numeric_property(slayer3d_registered_actor *actor, const char *key, float value);
slayer3d_audio_bus parse_audio_bus(const char *bus, slayer3d_audio_bus fallback);
slayer3d_backend parse_backend(const char *value, slayer3d_backend fallback);
slayer3d_window_mode parse_window_mode(const char *value, slayer3d_window_mode fallback);
slayer3d_tonemap_mode parse_tonemap(const char *value, slayer3d_tonemap_mode fallback);
bool parse_render_profile(const char *value, slayer3d_render_profile *out_profile);
char *path_basename(const char *path);
const char *asset_path_without_scheme(const char *path);
bool ensure_runtime_storage(slayer3d_game_data_runtime *runtime, char *error_buffer, int error_buffer_size);
float camera_fov_degrees(const slayer3d_game_data_runtime *runtime, yyjson_val *camera_json, float fallback);
slayer3d_camera_fov_axis camera_fov_axis(const slayer3d_game_data_runtime *runtime, yyjson_val *camera_json);
slayer3d_transition_type parse_transition_type(const char *value, slayer3d_transition_type fallback);
slayer3d_transition_direction parse_transition_direction(const char *value, slayer3d_transition_direction fallback);
slayer3d_builtin_font parse_builtin_font(const char *value, slayer3d_builtin_font fallback);
slayer3d_game_data_ui_align parse_ui_align(const char *value, slayer3d_game_data_ui_align fallback);
slayer3d_game_data_ui_valign parse_ui_valign(const char *value, slayer3d_game_data_ui_valign fallback);
const char *parse_ui_image_effect(const char *value);
int axis_index(const char *axis);
void set_actor_property_from_json(slayer3d_registered_actor *actor, const char *key, yyjson_val *value);
bool set_property_from_json(slayer3d_properties *props, const char *key, yyjson_val *value);
bool set_property_from_json_with_payload(slayer3d_properties *props, const char *key, yyjson_val *value,
                                         const slayer3d_properties *payload);
bool json_value_matches_property(yyjson_val *value, const slayer3d_value *property);
bool format_payload_string(const slayer3d_properties *payload, const char *format, char *buffer, size_t buffer_size);
slayer3d_properties *properties_from_json_payload(yyjson_val *json, const slayer3d_properties *source_payload);
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
bool slayer3d_game_data_clear_active_editor_selection(slayer3d_game_data_runtime *runtime);
bool slayer3d_game_data_clear_editor_vertex_selection(slayer3d_game_data_runtime *runtime);
slayer3d_game_data_editor_selection resolved_editor_selection(const slayer3d_game_data_runtime *runtime,
                                                              const slayer3d_game_data_editor_selection *selection);
slayer3d_properties *slayer3d_game_data_create_editor_selection_payload(
    const slayer3d_game_data_editor_selection *selection);
bool slayer3d_game_data_delete_selected_editor_brushes(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                       const slayer3d_properties *payload);
bool slayer3d_game_data_resize_selected_editor_brushes_y(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                         const slayer3d_properties *payload);
bool slayer3d_game_data_snap_selected_editor_vertices(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_delete_selected_editor_vertices(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_merge_selected_editor_vertices_to_hover(slayer3d_game_data_runtime *runtime,
                                                                yyjson_val *action);
bool slayer3d_game_data_add_editor_vertex_to_source(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_validate_editor_vertex_source(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_preview_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_clear_editor_command_preview(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_commit_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              const slayer3d_properties *payload);
bool slayer3d_game_data_undo_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload);
bool slayer3d_game_data_redo_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload);
bool slayer3d_game_data_export_editor_brush_world_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_export_editor_level_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_save_editor_level_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_load_editor_level_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_prepare_editor_test_run_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_save_editor_test_run_manifest_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_publish_editor_brush_world_status_action(slayer3d_game_data_runtime *runtime,
                                                                 yyjson_val *action);
bool slayer3d_game_data_create_box_brush_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_place_editor_player_start_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_apply_editor_player_start_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_delete_editor_player_start_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool eval_data_condition(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                         const slayer3d_game_data_ui_metrics *metrics);
bool eval_data_condition_with_payload(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                                      const slayer3d_game_data_ui_metrics *metrics, const slayer3d_properties *payload);
void emit_optional_signal(slayer3d_game_data_runtime *runtime, yyjson_val *json, const char *signal_key,
                          const slayer3d_properties *payload);
void actor_lifecycle_defer_begin(slayer3d_game_data_runtime *runtime);
void actor_lifecycle_defer_end(slayer3d_game_data_runtime *runtime);
bool apply_actor_pool_scene_exit_policies(slayer3d_game_data_runtime *runtime, const char *from_scene,
                                          const char *to_scene);
void clear_menu_text_entry_capture(slayer3d_game_data_runtime *runtime);
int menu_runtime_item_count(const slayer3d_game_data_runtime *runtime, const scene_menu_state *menu);
void update_dynamic_list_selection_state(slayer3d_game_data_runtime *runtime, scene_menu_state *menu);
bool set_action_keyboard_binding(slayer3d_game_data_runtime *runtime, const char *action, SDL_Scancode scancode);
bool set_action_mouse_button_binding(slayer3d_game_data_runtime *runtime, const char *action, Uint8 button);
bool set_action_gamepad_button_binding(slayer3d_game_data_runtime *runtime, const char *action,
                                       SDL_GamepadButton button);
bool json_scalar_to_value(yyjson_val *json, slayer3d_value *out_value);
bool set_property_from_value(slayer3d_properties *props, const char *key, const slayer3d_value *value);
bool start_property_animation_from_json(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                        const slayer3d_properties *payload);
bool start_ui_animation_from_json(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool actor_matches_target_filter(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *target,
                                 const slayer3d_registered_actor *source, yyjson_val *json,
                                 const slayer3d_properties *payload, const char *fallback_tag,
                                 bool fallback_exclude_source);
bool snapshot_actor_properties(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool restore_actor_property_snapshot(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool reset_actor_properties_to_authored_defaults(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool execute_actor_spawn_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                const slayer3d_properties *payload);
bool execute_actor_despawn_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool execute_actor_despawn_action_with_payload(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                               const slayer3d_properties *payload);
bool execute_actor_despawn_by_tag_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool execute_interaction_use_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                    const slayer3d_properties *payload);
bool execute_effect_explosion_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                     const slayer3d_properties *payload);
slayer3d_registered_actor *action_source_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                               const slayer3d_properties *payload);
bool apply_combat_damage_to_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                  const slayer3d_properties *payload, slayer3d_registered_actor *actor, float amount);
bool execute_combat_damage_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                  const slayer3d_properties *payload);
bool execute_combat_heal_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                const slayer3d_properties *payload);
bool execute_combat_kill_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                const slayer3d_properties *payload);
bool execute_combat_revive_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                  const slayer3d_properties *payload);
bool execute_resource_amount_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                    const slayer3d_properties *payload, bool consume);
bool execute_resource_set_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                 const slayer3d_properties *payload);
bool execute_pickup_collect_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                   const slayer3d_properties *payload);
bool execute_resource_station_use_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                         const slayer3d_properties *payload);
bool execute_status_effect_apply_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                        const slayer3d_properties *payload);
bool execute_weapon_reload_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                  const slayer3d_properties *payload);
bool execute_weapon_hitscan_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                   const slayer3d_properties *payload);
bool execute_projectile_fire_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                    const slayer3d_properties *payload);
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
void update_editor_camera_controller(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                     slayer3d_registered_actor *actor, const slayer3d_input_manager *input, float dt);
bool update_brush_velocity_motion(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                  slayer3d_registered_actor *actor, int actor_id, int pool_index, int actor_index,
                                  float dt);
bool execute_fps_controller_launch_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                          const slayer3d_properties *payload);
bool execute_fps_controller_teleport_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload);
bool execute_fps_controller_push_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
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
brush_world_runtime *find_brush_world_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name);
void editor_brush_world_mark_dirty(brush_world_runtime *world_runtime);
const editor_player_start_runtime *find_editor_player_start(const slayer3d_game_data_runtime *runtime,
                                                            const char *name);
slayer3d_game_data_sector_level_variant sector_level_variant_from_string(const char *variant,
                                                                         const slayer3d_level **out_level,
                                                                         const sector_level_runtime *level,
                                                                         bool sector_lighting_enabled);
slayer3d_bounding_box translated_bounds(slayer3d_bounding_box bounds, slayer3d_vec3 position);
bool model_bounds(const slayer3d_model *model, slayer3d_bounding_box *out_bounds);
yyjson_val *active_editor_tooling_root(const slayer3d_game_data_runtime *runtime);
void init_editor_selection(slayer3d_game_data_editor_selection *selection);
bool editor_selection_active_for_scene(const slayer3d_game_data_runtime *runtime);
bool editor_command_preview_active_for_scene(const slayer3d_game_data_runtime *runtime);
bool editor_placement_preview_active_for_scene(const slayer3d_game_data_runtime *runtime);
bool editor_trace_desc_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                 slayer3d_game_data_world_trace_desc *out_trace);
bool editor_work_plane_desc_from_trace_json(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                            slayer3d_vec3 *out_normal, float *out_distance);
bool pick_editor_player_start(const slayer3d_game_data_runtime *runtime,
                              const slayer3d_game_data_world_trace_desc *trace,
                              slayer3d_game_data_editor_selection *out_selection);
bool editor_pick_selection_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                     const slayer3d_game_data_world_trace_desc *trace,
                                     slayer3d_game_data_editor_selection *out_selection);
bool editor_selection_mode_is_click(yyjson_val *selection);
void clear_editor_command_preview(slayer3d_game_data_runtime *runtime);
void clear_editor_placement_preview(slayer3d_game_data_runtime *runtime);
void update_editor_placement_preview(slayer3d_game_data_runtime *runtime, yyjson_val *editor,
                                     const slayer3d_game_data_editor_selection *hover_selection);
bool update_editor_drag_create(slayer3d_game_data_runtime *runtime, yyjson_val *editor,
                               const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed);
void publish_editor_selection(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                              const slayer3d_game_data_editor_selection *selection);
bool slayer3d_game_data_select_editor_brush_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_create_editor_source_box_brush(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                       const char *material_name, unsigned int contents,
                                                       const int source_min[3], const int source_max[3],
                                                       editor_brush_source_prefab_result *out_result);
bool slayer3d_game_data_translate_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset);
bool slayer3d_game_data_rotate_selected_editor_brushes_y(slayer3d_game_data_runtime *runtime, int quarter_turns);
void editor_set_string_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, const char *value);
void editor_set_bool_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, bool value);
void editor_set_int_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, int value);
void editor_set_float_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, float value);
void editor_set_vec3_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, slayer3d_vec3 value);
bool publish_editor_brush_world_status(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, const char *world_name,
                                       const char *message, bool publish_result);
bool editor_brush_world_generate_brush_name(const brush_world_runtime *world_runtime, char *buffer, size_t buffer_size);
void free_brush_world_runtime(brush_world_runtime *world_runtime);
void free_editor_player_starts_runtime(slayer3d_game_data_runtime *runtime);
void free_editor_command_history(editor_command_history_state *history);
slayer3d_bounding_box editor_resized_preview_bounds(slayer3d_bounding_box bounds, slayer3d_vec3 normal, float distance);
void publish_editor_command_preview(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, bool valid,
                                    const char *command, const char *target, const char *message,
                                    const slayer3d_game_data_editor_selection *selection,
                                    const slayer3d_bounding_box *bounds);
bool compile_brush_world_visibility_grid(brush_world_runtime *world_runtime);
void free_brush_world_visibility_grid(brush_world_runtime *world_runtime);
bool rebuild_brush_world_runtime_artifacts(brush_world_runtime *world_runtime, char *error_buffer,
                                           int error_buffer_size);
void free_editor_brush_source_model(brush_world_runtime *world_runtime);
void free_editor_brush_source_box_runtime(editor_brush_source_box_runtime *box);
bool copy_editor_brush_source_box_runtime(const editor_brush_source_box_runtime *source,
                                          editor_brush_source_box_runtime *dest);
bool load_editor_brush_source_boxes(brush_world_runtime *world_runtime, yyjson_val *boxes, float meters_per_unit,
                                    int snap_units, char *error_buffer, int error_buffer_size);
bool editor_brush_world_rebuild_from_source(brush_world_runtime *world_runtime, char *error_buffer,
                                            int error_buffer_size);
int editor_brush_world_find_source_box_index(const brush_world_runtime *world_runtime, const char *brush_identity);
bool editor_brush_world_copy_source_box_by_identity(const brush_world_runtime *world_runtime,
                                                    const char *brush_identity,
                                                    editor_brush_source_box_runtime *out_box, int *out_index,
                                                    char *error_buffer, int error_buffer_size);
bool editor_brush_source_box_from_create_desc(const brush_world_runtime *world_runtime,
                                              const slayer3d_game_data_create_box_brush_desc *desc,
                                              const char *brush_name, editor_brush_source_box_runtime *out_box);
slayer3d_bounding_box editor_brush_source_box_bounds_meters(const brush_world_runtime *world_runtime,
                                                            const editor_brush_source_box_runtime *box);
bool editor_brush_world_validate_source_box_candidate(const brush_world_runtime *world_runtime,
                                                      const editor_brush_source_box_runtime *box, int exclude_index,
                                                      char *error_buffer, int error_buffer_size);
bool editor_brush_world_run_source_prefab_command(brush_world_runtime *world_runtime,
                                                  const editor_brush_source_prefab_desc *desc, const char *brush_name,
                                                  const int *source_min, const int *source_max, bool apply,
                                                  editor_brush_source_prefab_result *out_result, char *error_buffer,
                                                  int error_buffer_size);
bool editor_brush_world_insert_source_box_at_index(brush_world_runtime *world_runtime, int box_index,
                                                   const editor_brush_source_box_runtime *box, char *error_buffer,
                                                   int error_buffer_size);
bool editor_brush_world_insert_source_box_from_brush(brush_world_runtime *world_runtime, int box_index,
                                                     const slayer3d_game_data_brush *brush, char *error_buffer,
                                                     int error_buffer_size);
bool editor_brush_world_remove_source_box_at_index(brush_world_runtime *world_runtime, int box_index,
                                                   char *error_buffer, int error_buffer_size);
bool editor_brush_world_translate_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                             slayer3d_vec3 offset, char *error_buffer, int error_buffer_size);
bool editor_brush_world_rotate_source_box_y_quarter_turns(brush_world_runtime *world_runtime, const char *brush_name,
                                                          int quarter_turns, char *error_buffer, int error_buffer_size);
int editor_brush_world_source_box_face_index_for_identity(const brush_world_runtime *world_runtime,
                                                          const char *brush_identity, int fallback_face_index,
                                                          const char *face_identity);
bool editor_brush_world_source_box_face_normal_for_identity(const brush_world_runtime *world_runtime,
                                                            const char *brush_identity, int fallback_face_index,
                                                            const char *face_identity, int *out_face_index,
                                                            slayer3d_vec3 *out_normal);
bool editor_brush_world_resize_source_box_face(brush_world_runtime *world_runtime, const char *brush_name,
                                               slayer3d_vec3 face_normal, float distance, char *error_buffer,
                                               int error_buffer_size);
bool editor_brush_world_update_source_box_bounds_batch(brush_world_runtime *world_runtime,
                                                       const editor_source_box_bounds_update *updates, int update_count,
                                                       char *error_buffer, int error_buffer_size);
bool editor_brush_world_build_source_convex_brush_from_vertices(
    const brush_world_runtime *world_runtime, const char *brush_identity,
    const int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3], int vertex_count,
    slayer3d_game_data_brush *out_brush, char *error_buffer, int error_buffer_size);
void editor_brush_source_free_runtime_brush(slayer3d_game_data_brush *brush);
bool editor_brush_world_preview_source_vertex_operation(const brush_world_runtime *world_runtime,
                                                        const editor_brush_source_vertex_operation_desc *desc,
                                                        editor_brush_source_vertex_operation_result *out_result,
                                                        char *error_buffer, int error_buffer_size);
bool editor_brush_world_apply_source_vertex_operation(brush_world_runtime *world_runtime,
                                                      const editor_brush_source_vertex_operation_desc *desc,
                                                      editor_brush_source_vertex_operation_result *out_result,
                                                      char *error_buffer, int error_buffer_size);
bool editor_brush_world_set_source_box_face_material(brush_world_runtime *world_runtime, const char *brush_name,
                                                     int face_index, const char *material_name, char *error_buffer,
                                                     int error_buffer_size);
bool editor_brush_source_validate_box_vertex_topology(const int vertices[SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT][3],
                                                      int snap_units,
                                                      editor_brush_source_vertex_diagnostics *out_diagnostics,
                                                      char *error_buffer, int error_buffer_size);
bool editor_brush_source_box_build_vertex_model(const brush_world_runtime *world_runtime, int source_index,
                                                editor_brush_source_vertex_model *out_model, char *error_buffer,
                                                int error_buffer_size);
bool editor_brush_world_validate_source_vertex_model(const brush_world_runtime *world_runtime,
                                                     const int *source_indices, int source_index_count,
                                                     editor_brush_source_vertex_diagnostics *out_diagnostics,
                                                     char *error_buffer, int error_buffer_size);
bool editor_brush_world_find_shared_source_vertices(const brush_world_runtime *world_runtime, const int *source_indices,
                                                    int source_index_count,
                                                    editor_brush_source_shared_vertex *out_vertices, int out_capacity,
                                                    int *out_count, char *error_buffer, int error_buffer_size);
bool slayer3d_game_data_validate_editor_brush_source_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_validate_editor_brush_enclosure_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
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
bool load_signals(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_entities(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_editor_player_starts(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                               int error_buffer_size);
bool load_actor_pools(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_input(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_bindings(slayer3d_game_data_runtime *runtime, yyjson_val *logic, char *error_buffer, int error_buffer_size);
bool load_sensors(slayer3d_game_data_runtime *runtime, yyjson_val *logic);
bool load_wave_schedules(slayer3d_game_data_runtime *runtime, yyjson_val *logic);
void load_active_camera(slayer3d_game_data_runtime *runtime, yyjson_val *root);
bool load_scenes(slayer3d_game_data_runtime *runtime, yyjson_val *root, const slayer3d_game_data_load_options *options,
                 char *error_buffer, int error_buffer_size);
bool load_script_index_into_engine(slayer3d_game_data_runtime *runtime, slayer3d_asset_resolver *assets,
                                   slayer3d_script_engine *engine, int index, slayer3d_script_ref *module_refs,
                                   bool *loading, bool *loaded, char *error_buffer, int error_buffer_size);
bool load_scripts(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_lua_adapters(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                       int error_buffer_size);

yyjson_val *game_data_find_replication_channel_by_name(const slayer3d_game_data_runtime *runtime,
                                                       const char *replication_name, int *out_index);
yyjson_val *game_data_find_replication_channel_by_index(const slayer3d_game_data_runtime *runtime, Uint32 index);
bool game_data_replication_channel_is_host_to_client(yyjson_val *channel);
bool game_data_replication_channel_is_client_to_host(yyjson_val *channel);
size_t game_data_replication_channel_field_count(const slayer3d_game_data_runtime *runtime, yyjson_val *channel);
bool game_data_replication_channel_packet_size(const slayer3d_game_data_runtime *runtime, yyjson_val *channel,
                                               size_t *out_size);
size_t game_data_replication_channel_input_count(yyjson_val *channel);
bool game_data_replication_input_packet_size(yyjson_val *channel, size_t *out_size);
const char *game_data_replication_input_action(yyjson_val *input);
int game_data_replication_action_id(const slayer3d_game_data_runtime *runtime, yyjson_val *input);
slayer3d_game_data_network_direction game_data_network_direction_from_string(const char *direction);
const char *game_data_network_direction_name(slayer3d_game_data_network_direction direction);
yyjson_val *game_data_find_network_control_by_name(const slayer3d_game_data_runtime *runtime, const char *control_name,
                                                   int *out_index);
yyjson_val *game_data_find_network_control_by_index(const slayer3d_game_data_runtime *runtime, Uint32 index);
bool game_data_network_control_packet_size(size_t *out_size);
int game_data_network_control_signal_id(const slayer3d_game_data_runtime *runtime, yyjson_val *control);
bool game_data_read_actor_replication_field(const slayer3d_registered_actor *actor,
                                            const slayer3d_replication_field_descriptor *field,
                                            game_data_snapshot_value *out_value);
bool game_data_write_snapshot_value(slayer3d_replication_writer *writer, const game_data_snapshot_value *value);
bool game_data_read_snapshot_value(slayer3d_replication_reader *reader, slayer3d_replication_field_type type,
                                   game_data_snapshot_value *out_value);
bool game_data_apply_actor_replication_field(slayer3d_game_data_runtime *runtime, slayer3d_registered_actor *actor,
                                             const slayer3d_replication_field_descriptor *field,
                                             const game_data_snapshot_value *value);

#endif

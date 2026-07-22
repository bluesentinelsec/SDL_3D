#ifndef SLAYER3D_GAME_DATA_INTERNAL_H
#define SLAYER3D_GAME_DATA_INTERNAL_H

#include <stdbool.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_actor_pool_types.h"
#include "game_data_animation_types.h"
#include "game_data_brush_internal.h"
#include "game_data_grid_types.h"
#include "game_data_logic_types.h"
#include "game_data_menu_types.h"
#include "game_data_network_types.h"
#include "game_data_runtime_types.h"
#include "game_data_world_types.h"
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
#include "slayer3d/ui_layout.h"
#include "yyjson.h"

#define SLAYER3D_GAME_DATA_SIGNAL_BASE 20000
#define SLAYER3D_GAME_DATA_MENU_TEXT_MAX_BYTES 255
#define SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_MAGIC 0x53335253u /* "S3RS" */
#define SLAYER3D_GAME_DATA_NETWORK_SNAPSHOT_VERSION 1u
#define SLAYER3D_GAME_DATA_NETWORK_INPUT_MAGIC 0x49335253u /* "S3RI" */
#define SLAYER3D_GAME_DATA_NETWORK_INPUT_VERSION 1u
#define SLAYER3D_GAME_DATA_NETWORK_CONTROL_MAGIC 0x43335253u /* "S3RC" */
#define SLAYER3D_GAME_DATA_NETWORK_CONTROL_VERSION 1u
#ifndef SLAYER3D_GAME_DATA_WORLD_VIEWPORT_MAX
#define SLAYER3D_GAME_DATA_WORLD_VIEWPORT_MAX 16
#endif
#include "game_data_editor_types.h"

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
    char *file_base_dir;
    slayer3d_storage_config storage_config;
    scene_entry *scenes;
    int scene_count;
    int active_scene_index;
    slayer3d_properties *scene_state;
    yyjson_doc *scene_sky_override;
    /* Logical UI viewport used to resolve retained widget layouts; 0 means
     * the 1280×720 default until the host publishes the render size. */
    float ui_viewport_w;
    float ui_viewport_h;
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
    editor_actor_runtime *editor_actors;
    int editor_actor_count;
    int editor_actor_capacity;
    Uint64 editor_actor_revision;
    bool editor_actor_dirty;
    editor_prefab_runtime *editor_prefabs;
    int editor_prefab_count;
    int editor_prefab_capacity;
    Uint64 editor_prefab_revision;
    bool editor_prefab_dirty;
    editor_connection_runtime *editor_connections;
    int editor_connection_count;
    int editor_connection_capacity;
    Uint64 editor_connection_revision;
    bool editor_connection_dirty;
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
    editor_source_edge_selection editor_selected_edges[SLAYER3D_EDITOR_SELECTED_EDGE_CAPACITY];
    int editor_selected_edge_count;
    const char *editor_selected_edge_scene;
    editor_command_preview_state editor_command_preview;
    editor_placement_preview_state editor_placement_preview;
    editor_drag_create_state editor_drag_create;
    editor_drag_move_state editor_drag_move;
    editor_clip_tool_state editor_clip_tool;
    editor_camera_orbit_state editor_camera_orbit;
    editor_camera_move_state editor_camera_move;
    bool editor_has_last_duplicate_offset;
    slayer3d_vec3 editor_last_duplicate_offset;
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
#include "game_data_actor_pool_internal.h"
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
slayer3d_audio_bus parse_audio_bus(const char *bus, slayer3d_audio_bus fallback);
slayer3d_backend parse_backend(const char *value, slayer3d_backend fallback);
slayer3d_window_mode parse_window_mode(const char *value, slayer3d_window_mode fallback);
slayer3d_tonemap_mode parse_tonemap(const char *value, slayer3d_tonemap_mode fallback);
bool parse_render_profile(const char *value, slayer3d_render_profile *out_profile);
char *path_basename(const char *path);
const char *asset_path_without_scheme(const char *path);
bool slayer3d_game_data_build_active_ui_widget_layout(const slayer3d_game_data_runtime *runtime, float viewport_w,
                                                      float viewport_h, const slayer3d_game_data_ui_metrics *metrics,
                                                      slayer3d_ui_layout_model *layout);
/* Read the published logical UI viewport, falling back to 1280×720. */
void slayer3d_game_data_ui_viewport(const slayer3d_game_data_runtime *runtime, float *out_width, float *out_height);

typedef struct game_data_scene_world_viewport
{
    const char *name;
    const char *camera;
    SDL_Rect rect;
    bool draw_skybox;
    bool draw_viewmodel;
    yyjson_val *work_plane;
    yyjson_val *grid;
} game_data_scene_world_viewport;

bool game_data_resolve_active_scene_world_viewports(const slayer3d_game_data_runtime *runtime,
                                                    game_data_scene_world_viewport *out_viewports, int capacity,
                                                    int *out_count);
bool slayer3d_game_data_ui_binding_to_string(const slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_ui_metrics *metrics, yyjson_val *binding,
                                             char *buffer, size_t buffer_size);
bool slayer3d_game_data_ui_json_scalar_to_string(yyjson_val *value, char *buffer, size_t buffer_size);
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
float game_data_random01(slayer3d_game_data_runtime *runtime);
int action_signal_id(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *key);
bool execute_one_action(slayer3d_game_data_runtime *runtime, yyjson_val *action, const slayer3d_properties *payload);
bool execute_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions, const slayer3d_properties *payload);
bool execute_optional_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                   const slayer3d_properties *payload);
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
bool update_brush_velocity_motion(slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                  slayer3d_registered_actor *actor, int actor_id, int pool_index, int actor_index,
                                  float dt);
bool execute_projectile_fire_action_for_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              const slayer3d_properties *payload,
                                              slayer3d_registered_actor *source_actor);
void weapon_complete_reload(slayer3d_registered_actor *actor, yyjson_val *json);
const runtime_collection *find_runtime_collection_const(const slayer3d_game_data_runtime *runtime,
                                                        const char *collection_name);
bool runtime_collection_field_to_string(const runtime_collection *collection, int row_index, const char *field_name,
                                        char *buffer, size_t buffer_size);
#include "game_data_editor_actions_internal.h"
#include "game_data_editor_internal.h"
#include "game_data_editor_object_state.h"
#include "game_data_world_internal.h"
void free_editor_metadata(slayer3d_game_data_editor_metadata *metadata);

int find_timer_index(const slayer3d_game_data_runtime *runtime, const char *name);
bool load_timers(slayer3d_game_data_runtime *runtime, yyjson_val *logic, char *error_buffer, int error_buffer_size);
bool load_signals(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_entities(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_editor_player_starts(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                               int error_buffer_size);
bool load_editor_actors(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                        int error_buffer_size);
bool load_editor_prefabs(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                         int error_buffer_size);
bool load_editor_connections(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                             int error_buffer_size);
bool load_actor_pools(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_input(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size);
bool load_bindings(slayer3d_game_data_runtime *runtime, yyjson_val *logic, char *error_buffer, int error_buffer_size);
bool load_sensors(slayer3d_game_data_runtime *runtime, yyjson_val *logic);
bool load_wave_schedules(slayer3d_game_data_runtime *runtime, yyjson_val *logic);
void load_active_camera(slayer3d_game_data_runtime *runtime, yyjson_val *root);
bool load_scenes(slayer3d_game_data_runtime *runtime, yyjson_val *root, const slayer3d_game_data_load_options *options,
                 char *error_buffer, int error_buffer_size);

#include "game_data_controller_internal.h"
#include "game_data_input_internal.h"
#include "game_data_network_internal.h"
#include "game_data_script_internal.h"

#endif

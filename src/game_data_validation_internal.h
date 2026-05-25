/**
 * @file game_data_validation_internal.h
 * @brief Shared internals for JSON game data validation modules.
 */

#ifndef SLAYER3D_GAME_DATA_VALIDATION_INTERNAL_H
#define SLAYER3D_GAME_DATA_VALIDATION_INTERNAL_H

#include "game_data_validation.h"

#include <stddef.h>

#define PATH_BUFFER_SIZE 256

typedef struct name_table
{
    const char **names;
    const char **paths;
    int count;
} name_table;

typedef struct script_manifest
{
    const char *id;
    const char *path;
    const char *module;
    const char *json_path;
    const char **dependencies;
    int dependency_count;
    bool visiting;
    bool visited;
} script_manifest;

typedef struct validation_context
{
    const slayer3d_game_data_validation_options *options;
    const char *source_path;
    const char *base_dir;
    const slayer3d_asset_resolver *assets;
    const slayer3d_game_data_source_map *source_map;
    char *error_buffer;
    int error_buffer_size;
    bool failed;
} validation_context;

typedef struct validation_names
{
    name_table entities;
    name_table actor_archetypes;
    name_table actor_pools;
    name_table actor_pool_actors;
    name_table grid_maps;
    name_table grid_pickup_layers;
    name_table sector_levels;
    name_table brush_worlds;
    name_table sector_navigation;
    name_table sector_doors;
    name_table sector_platforms;
    name_table editor_player_starts;
    name_table signals;
    name_table scripts;
    name_table script_modules;
    name_table adapters;
    name_table actions;
    name_table input_assignment_sets;
    name_table input_profiles;
    name_table network_input_channels;
    name_table timers;
    name_table cameras;
    name_table fonts;
    name_table images;
    name_table models;
    name_table sprites;
    name_table sounds;
    name_table music;
    name_table ambient;
    name_table scenes;
    name_table sensors;
    name_table persistence;
    name_table used_adapters;
    name_table used_scripts;
    script_manifest *script_manifests;
    int script_count;
} validation_names;

yyjson_val *validation_obj_get(yyjson_val *object, const char *key);
const char *validation_json_string(yyjson_val *object, const char *key);

void format_path(char *buffer, size_t buffer_size, const char *format, ...);
bool validation_error(validation_context *ctx, const char *json_path, const char *format, ...);

void name_table_destroy(name_table *table);
bool name_table_contains(const name_table *table, const char *name);
bool require_unique_name(validation_context *ctx, name_table *table, const char *kind, const char *name,
                         const char *json_path);
bool note_name(name_table *table, const char *name, const char *json_path);
bool require_ref(validation_context *ctx, const name_table *table, const char *kind, const char *name,
                 const char *json_path);
bool require_actor_ref(validation_context *ctx, const validation_names *names, const char *name, const char *json_path);
bool asset_path_exists(validation_context *ctx, const char *asset_path, const char *json_path, const char *asset_kind);
bool validate_editor_metadata(validation_context *ctx, yyjson_val *metadata, const char *json_path,
                              validation_names *names, bool allow_templates);
bool validate_editor_metadata_tree(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_editor_brush_sources(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_editor_player_starts(validation_context *ctx, yyjson_val *root, validation_names *names);
bool require_unique_editor_stable_id(validation_context *ctx, name_table *stable_ids, yyjson_val *json,
                                     const char *json_path);
bool validation_mouse_button_name_valid(const char *name);
bool validate_data_condition(validation_context *ctx, yyjson_val *condition, const char *path, validation_names *names);
bool validate_action_array(validation_context *ctx, yyjson_val *actions, const char *json_path,
                           validation_names *names);
bool validate_network_port_value(validation_context *ctx, yyjson_val *value, const char *json_path, const char *label);
bool validate_network(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_non_empty_string_field(validation_context *ctx, yyjson_val *json, const char *json_path, const char *type,
                                     const char *field);
bool validate_optional_signal_field(validation_context *ctx, yyjson_val *json, const char *json_path,
                                    validation_names *names, const char *field);
bool validate_target_filter_fields(validation_context *ctx, yyjson_val *json, const char *json_path, const char *type);
bool validation_audio_bus_name_valid(const char *bus);
bool collect_font_assets(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_sprite_assets(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_image_assets(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_model_assets(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_audio_assets(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_sector_levels(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_sector_navigation(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_sector_doors(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_sector_platforms(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_input_actions(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_input_assignment_sets(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_input_profiles(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_grid_maps(validation_context *ctx, yyjson_val *root, validation_names *names);
bool collect_grid_pickup_layers(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_input_bindings(validation_context *ctx, yyjson_val *root);
bool validate_input_assignment_sets(validation_context *ctx, yyjson_val *root);
bool validate_input_profiles(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_grid_maps(validation_context *ctx, yyjson_val *root);
bool validate_grid_pickup_layers(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_sector_levels(validation_context *ctx, yyjson_val *root);
bool validate_sector_navigation(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_sector_doors(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_sector_platforms(validation_context *ctx, yyjson_val *root, validation_names *names);
bool sector_level_has_sector_name(yyjson_val *root, const char *level_name, const char *sector_name);
bool sector_level_has_sector_index(yyjson_val *root, const char *level_name, int sector_index);
bool validate_storage(validation_context *ctx, yyjson_val *root);
bool validate_persistence(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_haptics(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_app_refs(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_update_phases(validation_context *ctx, yyjson_val *phases, const char *json_path,
                            validation_names *names);
bool validate_presentation(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_cameras(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_world_metadata(validation_context *ctx, yyjson_val *root);
bool validate_render_effects(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_lights(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_transitions(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_render_settings(validation_context *ctx, yyjson_val *root);
bool ui_metric_name_valid(const char *metric);
bool validate_ui_panels(validation_context *ctx, yyjson_val *panels, const char *path, validation_names *names);
bool validate_ui_inspectors(validation_context *ctx, yyjson_val *inspectors, const char *path, validation_names *names);
bool validate_ui(validation_context *ctx, yyjson_val *root, validation_names *names);
bool is_single_byte_string(yyjson_val *value);

bool is_vec_array(yyjson_val *value, size_t min_count);
bool is_exact_vec_array(yyjson_val *value, size_t count);
bool is_exact_vec3_or_vec4_array(yyjson_val *value);
bool numeric_array_values_positive(yyjson_val *value);
bool numeric_array_values_in_range(yyjson_val *value, double min_value, double max_value);

bool brush_content_name_valid(const char *name);
bool brush_surface_flag_name_valid(const char *name);
bool validate_brush_string_or_string_array(validation_context *ctx, yyjson_val *value, const char *path,
                                           const char *label, bool (*name_valid)(const char *name), bool allow_empty);
bool validate_brush_worlds(validation_context *ctx, yyjson_val *root, validation_names *names);
bool validate_scene_editor_tooling(validation_context *ctx, yyjson_val *scene_root, const char *json_path,
                                   validation_names *names);
bool validate_render_mesh_primitive_component(validation_context *ctx, yyjson_val *component, const char *path,
                                              const validation_names *names);
bool validate_render_composite_component(validation_context *ctx, yyjson_val *component, const char *path,
                                         const validation_names *names);
bool validate_render_camera_visibility_field(validation_context *ctx, yyjson_val *component, const char *path,
                                             const validation_names *names, const char *field);
bool validate_property_name_array_field(validation_context *ctx, yyjson_val *component, const char *path,
                                        const char *field, const char *label);
bool validate_fps_sector_component(validation_context *ctx, yyjson_val *component, const char *path,
                                   validation_names *names);
bool validate_fps_brush_component(validation_context *ctx, yyjson_val *component, const char *path,
                                  validation_names *names);
bool validate_editor_camera_component(validation_context *ctx, yyjson_val *component, const char *path,
                                      validation_names *names);

#endif /* SLAYER3D_GAME_DATA_VALIDATION_INTERNAL_H */

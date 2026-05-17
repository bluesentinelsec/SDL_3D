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

void format_path(char *buffer, size_t buffer_size, const char *format, ...);
bool validation_error(validation_context *ctx, const char *json_path, const char *format, ...);

void name_table_destroy(name_table *table);
bool require_unique_name(validation_context *ctx, name_table *table, const char *kind, const char *name,
                         const char *json_path);
bool require_ref(validation_context *ctx, const name_table *table, const char *kind, const char *name,
                 const char *json_path);

bool is_vec_array(yyjson_val *value, size_t min_count);
bool is_exact_vec_array(yyjson_val *value, size_t count);
bool is_exact_vec3_or_vec4_array(yyjson_val *value);
bool numeric_array_values_positive(yyjson_val *value);
bool numeric_array_values_in_range(yyjson_val *value, double min_value, double max_value);

bool brush_content_name_valid(const char *name);
bool brush_surface_flag_name_valid(const char *name);
bool validate_brush_string_or_string_array(validation_context *ctx, yyjson_val *value, const char *path,
                                           const char *label, bool (*name_valid)(const char *name), bool allow_empty);

#endif /* SLAYER3D_GAME_DATA_VALIDATION_INTERNAL_H */

#ifndef SLAYER3D_GAME_DATA_RUNTIME_TYPES_H
#define SLAYER3D_GAME_DATA_RUNTIME_TYPES_H

#include <stdbool.h>

#include <SDL3/SDL_stdinc.h>

#include "game_data_menu_types.h"
#include "slayer3d/network_replication.h"
#include "slayer3d/properties.h"
#include "slayer3d/types.h"
#include "yyjson.h"

typedef struct slayer3d_replication_field_descriptor slayer3d_replication_field_descriptor;

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

#endif

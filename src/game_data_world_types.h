#ifndef SLAYER3D_GAME_DATA_WORLD_TYPES_H
#define SLAYER3D_GAME_DATA_WORLD_TYPES_H

#include <stdbool.h>

#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "game_data_editor_types.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/door.h"
#include "slayer3d/fps_mover.h"
#include "slayer3d/game_data.h"
#include "slayer3d/level.h"
#include "slayer3d/model.h"
#include "yyjson.h"

#define SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS 4

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

#endif

#ifndef SLAYER3D_GAME_DATA_WORLD_INTERNAL_H
#define SLAYER3D_GAME_DATA_WORLD_INTERNAL_H

#include <stdbool.h>

#include "game_data_grid_types.h"
#include "game_data_runtime_types.h"
#include "game_data_world_types.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/collision.h"
#include "slayer3d/level.h"
#include "slayer3d/model.h"
#include "slayer3d/types.h"
#include "yyjson.h"

typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

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

sector_level_runtime *find_sector_level_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name);
const sector_level_runtime *find_sector_level_runtime(const slayer3d_game_data_runtime *runtime, const char *name);
int sector_level_find_sector_name(const sector_level_runtime *level, const char *sector_name);
const brush_world_runtime *find_brush_world_runtime(const slayer3d_game_data_runtime *runtime, const char *name);
brush_world_runtime *find_brush_world_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name);
void editor_brush_world_mark_dirty(brush_world_runtime *world_runtime);
const editor_player_start_runtime *find_editor_player_start(const slayer3d_game_data_runtime *runtime,
                                                            const char *name);
const editor_actor_runtime *find_editor_actor(const slayer3d_game_data_runtime *runtime, const char *name);
slayer3d_game_data_sector_level_variant sector_level_variant_from_string(const char *variant,
                                                                         const slayer3d_level **out_level,
                                                                         const sector_level_runtime *level,
                                                                         bool sector_lighting_enabled);
slayer3d_bounding_box translated_bounds(slayer3d_bounding_box bounds, slayer3d_vec3 position);
bool model_bounds(const slayer3d_model *model, slayer3d_bounding_box *out_bounds);

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

#endif

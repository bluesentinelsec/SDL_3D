/**
 * @file game_data_validation.h
 * @brief Internal JSON game data validation entry points.
 */

#ifndef SLAYER3D_GAME_DATA_VALIDATION_H
#define SLAYER3D_GAME_DATA_VALIDATION_H

#include "slayer3d/asset.h"
#include "slayer3d/game_data.h"
#include "slayer3d/network_replication.h"
#include "yyjson.h"

typedef struct slayer3d_game_data_source_map slayer3d_game_data_source_map;

bool slayer3d_game_data_validate_document(yyjson_val *root, const char *source_path, const char *base_dir,
                                          const slayer3d_asset_resolver *assets,
                                          const slayer3d_game_data_validation_options *options, char *error_buffer,
                                          int error_buffer_size);

bool slayer3d_game_data_validate_document_with_source_map(yyjson_val *root, const char *source_path,
                                                          const char *base_dir, const slayer3d_asset_resolver *assets,
                                                          const slayer3d_game_data_source_map *source_map,
                                                          const slayer3d_game_data_validation_options *options,
                                                          char *error_buffer, int error_buffer_size);

yyjson_doc *slayer3d_game_data_compose_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                             slayer3d_game_data_source_map **out_source_map, char *error_buffer,
                                             int error_buffer_size);

void slayer3d_game_data_source_map_destroy(slayer3d_game_data_source_map *map);

bool slayer3d_game_data_network_schema_hash(yyjson_val *root, Uint8 out_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE],
                                            bool *out_present);

yyjson_val *slayer3d_game_data_find_input_assignment_set_json(yyjson_val *root, const char *set_name);

#endif /* SLAYER3D_GAME_DATA_VALIDATION_H */

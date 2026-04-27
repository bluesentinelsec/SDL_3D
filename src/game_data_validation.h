/**
 * @file game_data_validation.h
 * @brief Internal JSON game data validation entry points.
 */

#ifndef SDL3D_GAME_DATA_VALIDATION_H
#define SDL3D_GAME_DATA_VALIDATION_H

#include "sdl3d/asset.h"
#include "sdl3d/game_data.h"
#include "yyjson.h"

bool sdl3d_game_data_validate_document(yyjson_val *root, const char *source_path, const char *base_dir,
                                       const sdl3d_asset_resolver *assets,
                                       const sdl3d_game_data_validation_options *options, char *error_buffer,
                                       int error_buffer_size);

#endif /* SDL3D_GAME_DATA_VALIDATION_H */

/**
 * @file game_data_asset_runtime.h
 * @brief Runtime asset lookup APIs for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_ASSET_RUNTIME_H
#define SLAYER3D_GAME_DATA_ASSET_RUNTIME_H

#include <stdbool.h>

#include "slayer3d/game_data_assets.h"
#include "slayer3d/sprite_asset.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

    /** @brief Read a font asset descriptor by id from `assets.fonts`. */
    bool slayer3d_game_data_get_font_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                           slayer3d_game_data_font_asset *out_font);

    /** @brief Read an image asset descriptor by id from `assets.images`. */
    bool slayer3d_game_data_get_image_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_image_asset *out_image);

    /** @brief Read a 3D model asset descriptor by id from `assets.models`. */
    bool slayer3d_game_data_get_model_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_model_asset *out_model);

    /** @brief Read a sound-effect asset descriptor by id from `assets.sounds`. */
    bool slayer3d_game_data_get_sound_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_sound_asset *out_sound);

    /** @brief Read a music asset descriptor by id from `assets.music`. */
    bool slayer3d_game_data_get_music_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_music_asset *out_music);

    /** @brief Read an ambient-zone asset descriptor by id from `assets.ambient`. */
    bool slayer3d_game_data_get_ambient_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                              slayer3d_game_data_ambient_asset *out_ambient);

    /** @brief Read a sprite asset descriptor by id from `assets.sprites`. */
    bool slayer3d_game_data_get_sprite_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                             slayer3d_game_data_sprite_asset *out_sprite);

    /**
     * @brief Load a sprite asset by id from `assets.sprites`.
     *
     * The runtime looks up the authored sprite descriptor, resolves the
     * source image through the game's asset resolver, and builds billboard
     * textures plus rotation sets ready for sprite actors.
     */
    bool slayer3d_game_data_load_sprite_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                              slayer3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                              int error_buffer_size);

    /**
     * @brief Resolve an authored audio path to a filesystem path usable by audio backends.
     *
     * Resolver-backed assets such as `asset://audio/title.ogg` are materialized
     * into the runtime's `cache://audio` storage root. Plain filesystem paths
     * are resolved relative to the loaded game data file and returned without
     * copying. The returned path is copied into @p out_path and remains valid
     * independently of the runtime.
     *
     * @param runtime Loaded game data runtime.
     * @param path Authored audio path or asset URI.
     * @param out_path Buffer that receives the filesystem path.
     * @param out_path_size Size of @p out_path in bytes.
     * @return true when the path was resolved and copied.
     */
    bool slayer3d_game_data_prepare_audio_file(slayer3d_game_data_runtime *runtime, const char *path, char *out_path,
                                               int out_path_size);

#ifdef __cplusplus
}
#endif

#endif

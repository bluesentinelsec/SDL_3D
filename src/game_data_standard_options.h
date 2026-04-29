/**
 * @file game_data_standard_options.h
 * @brief Internal helpers for generating engine-provided standard options scenes.
 */

#ifndef SDL3D_GAME_DATA_STANDARD_OPTIONS_H
#define SDL3D_GAME_DATA_STANDARD_OPTIONS_H

#include <stdbool.h>

#include "yyjson.h"

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        SDL3D_STANDARD_OPTIONS_SCENE_COUNT = 6
    };

    /**
     * @brief Immutable scene documents generated from a game's standard options config.
     *
     * The caller owns each document and must release the collection with
     * sdl3d_standard_options_scene_docs_free().
     */
    typedef struct sdl3d_standard_options_scene_docs
    {
        /** Generated immutable yyjson scene documents. */
        yyjson_doc **docs;
        /** Number of generated documents in docs. */
        int count;
    } sdl3d_standard_options_scene_docs;

    /**
     * @brief Build the standard options scene package from an authored game root.
     *
     * @param game_root Parsed `sdl3d.game.v0` root object containing `scenes.standard_options`.
     * @param package_name Scene package name. Only `standard_options` is supported.
     * @param out_docs Receives generated scene documents on success.
     * @param error_buffer Optional destination for a human-readable error message.
     * @param error_buffer_size Size of `error_buffer` in bytes.
     * @return true when all standard options scenes were generated successfully.
     */
    bool sdl3d_standard_options_build_scene_docs(yyjson_val *game_root, const char *package_name,
                                                 sdl3d_standard_options_scene_docs *out_docs, char *error_buffer,
                                                 int error_buffer_size);

    /**
     * @brief Free documents returned by sdl3d_standard_options_build_scene_docs().
     *
     * @param docs Generated document collection to release. Safe to pass NULL.
     */
    void sdl3d_standard_options_scene_docs_free(sdl3d_standard_options_scene_docs *docs);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_GAME_DATA_STANDARD_OPTIONS_H */

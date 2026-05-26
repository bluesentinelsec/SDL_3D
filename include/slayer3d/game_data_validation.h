/**
 * @file game_data_validation.h
 * @brief Public validation diagnostics for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_PUBLIC_VALIDATION_H
#define SLAYER3D_GAME_DATA_PUBLIC_VALIDATION_H

#include <stdbool.h>

#include "slayer3d/asset.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Authored game data diagnostic severity. */
    typedef enum slayer3d_game_data_diagnostic_severity
    {
        /** @brief Non-fatal issue that authors should review. */
        SLAYER3D_GAME_DATA_DIAGNOSTIC_WARNING = 1,
        /** @brief Fatal issue that prevents the data from loading. */
        SLAYER3D_GAME_DATA_DIAGNOSTIC_ERROR = 2,
    } slayer3d_game_data_diagnostic_severity;

    /**
     * @brief Callback for authored game data validation diagnostics.
     *
     * @p json_path is a best-effort JSON path to the authored object or field
     * that produced the diagnostic. @p message is a human-readable description
     * intended to be actionable without stepping through engine code.
     */
    typedef void (*slayer3d_game_data_diagnostic_fn)(void *userdata, slayer3d_game_data_diagnostic_severity severity,
                                                     const char *json_path, const char *message);

    /**
     * @brief Options controlling authored game data validation.
     */
    typedef struct slayer3d_game_data_validation_options
    {
        /** @brief Optional diagnostic callback. */
        slayer3d_game_data_diagnostic_fn diagnostic;
        /** @brief User pointer passed to @p diagnostic. */
        void *userdata;
        /** @brief When true, warnings also make validation fail. */
        bool treat_warnings_as_errors;
    } slayer3d_game_data_validation_options;

    /**
     * @brief Validate a JSON game data file without instantiating runtime state.
     *
     * Validation checks schema, authored names, references, supported generic
     * logic primitives, script manifest structure, dependency cycles, and script
     * file existence. It can emit warnings for suspicious but non-fatal data,
     * such as unused adapters or unsupported component types.
     *
     * @param path JSON file path.
     * @param options Optional validation options and diagnostic callback.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when no fatal validation error was found.
     */
    bool slayer3d_game_data_validate_file(const char *path, const slayer3d_game_data_validation_options *options,
                                          char *error_buffer, int error_buffer_size);

    /**
     * @brief Validate a JSON game data asset through a resolver.
     *
     * Validation reads the JSON and referenced script files from @p assets, so
     * authored data can be checked the same way whether it comes from a source
     * tree, packed archive, or embedded pack.
     *
     * @param assets Resolver containing the JSON asset and referenced scripts.
     * @param asset_path Virtual path, such as asset://pong.game.json.
     * @param options Optional validation options and diagnostic callback.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when no fatal validation error was found.
     */
    bool slayer3d_game_data_validate_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                           const slayer3d_game_data_validation_options *options, char *error_buffer,
                                           int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_DATA_PUBLIC_VALIDATION_H */

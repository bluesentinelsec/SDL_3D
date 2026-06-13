/**
 * @file map.h
 * @brief Slayer3D standalone map file validation.
 *
 * A Slayer3D map file is authored level data, distinct from a complete game
 * data document. Maps describe reusable level content such as brushes,
 * materials, actor instances, transforms, arbitrary properties, and generic
 * object connections. Game runtimes can interpret these maps according to their
 * own data model while preserving unknown game-specific properties.
 */

#ifndef SLAYER3D_MAP_H
#define SLAYER3D_MAP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SLAYER3D_MAP_FORMAT_ID "slayer3d.map"
#define SLAYER3D_MAP_FORMAT_VERSION 1

    /** @brief Map validation diagnostic severity. */
    typedef enum slayer3d_map_diagnostic_severity
    {
        /** @brief Non-fatal authoring issue. */
        SLAYER3D_MAP_DIAGNOSTIC_WARNING = 1,
        /** @brief Fatal schema or reference issue. */
        SLAYER3D_MAP_DIAGNOSTIC_ERROR = 2,
    } slayer3d_map_diagnostic_severity;

    /**
     * @brief Callback for map validation diagnostics.
     *
     * @p json_path is a best-effort JSON path to the invalid or suspicious
     * field. @p message is a human-readable explanation intended for editor UI
     * and tests.
     */
    typedef void (*slayer3d_map_diagnostic_fn)(void *userdata, slayer3d_map_diagnostic_severity severity,
                                               const char *json_path, const char *message);

    /** @brief Options controlling Slayer3D map validation. */
    typedef struct slayer3d_map_validation_options
    {
        /** @brief Optional diagnostic callback. */
        slayer3d_map_diagnostic_fn diagnostic;
        /** @brief User pointer passed to @p diagnostic. */
        void *userdata;
        /** @brief When true, warnings also make validation fail. */
        bool treat_warnings_as_errors;
    } slayer3d_map_validation_options;

    /**
     * @brief Validate a JSON Slayer3D map document from memory.
     *
     * Validation covers the initial `.slayermap.json` contract: format/version,
     * metadata, asset references, materials, brushes, actor instances,
     * transforms, arbitrary property bags, and object connections. Unknown
     * top-level fields are allowed for forward-compatible project extensions.
     *
     * @param json UTF-8 JSON document.
     * @param json_size Size of @p json in bytes.
     * @param options Optional validation options and diagnostic callback.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when no fatal validation error was found.
     */
    bool slayer3d_map_validate_json(const char *json, size_t json_size, const slayer3d_map_validation_options *options,
                                    char *error_buffer, int error_buffer_size);

    /**
     * @brief Validate a JSON Slayer3D map file from disk.
     *
     * @param path Filesystem path to a `.slayermap.json` document.
     * @param options Optional validation options and diagnostic callback.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when no fatal validation error was found.
     */
    bool slayer3d_map_validate_file(const char *path, const slayer3d_map_validation_options *options,
                                    char *error_buffer, int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_MAP_H */

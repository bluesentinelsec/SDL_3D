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

    /** @brief Opaque parsed Slayer3D map document. */
    typedef struct slayer3d_map_document slayer3d_map_document;

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
     * metadata, asset references, materials, brushes, actor instances, authored
     * light/effect markers, skyboxes, transforms, arbitrary property bags, and
     * object connections. Unknown top-level fields are allowed for
     * forward-compatible project extensions.
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

    /**
     * @brief Load and validate a Slayer3D map document from memory.
     *
     * The returned document owns a parsed copy of the JSON document and can be
     * serialized or written back through the map APIs. Unknown fields and
     * arbitrary project-specific property values are preserved by the parsed
     * document.
     *
     * @param json UTF-8 JSON document.
     * @param json_size Size of @p json in bytes.
     * @param options Optional validation options and diagnostic callback.
     * @param out_document Receives the loaded map document.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the document was parsed, validated, and loaded.
     */
    bool slayer3d_map_load_json(const char *json, size_t json_size, const slayer3d_map_validation_options *options,
                                slayer3d_map_document **out_document, char *error_buffer, int error_buffer_size);

    /**
     * @brief Load and validate a Slayer3D map document from disk.
     *
     * @param path Filesystem path to a `.slayermap.json` document.
     * @param options Optional validation options and diagnostic callback.
     * @param out_document Receives the loaded map document.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the file was read, parsed, validated, and loaded.
     */
    bool slayer3d_map_load_file(const char *path, const slayer3d_map_validation_options *options,
                                slayer3d_map_document **out_document, char *error_buffer, int error_buffer_size);

    /**
     * @brief Serialize a loaded map document to canonical pretty JSON.
     *
     * The returned UTF-8 string is null-terminated and must be released with
     * slayer3d_map_free_string().
     *
     * @param document Loaded map document.
     * @param out_json Receives the newly allocated JSON string.
     * @param out_json_size Optional byte length excluding the null terminator.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when serialization succeeds.
     */
    bool slayer3d_map_to_json(const slayer3d_map_document *document, char **out_json, size_t *out_json_size,
                              char *error_buffer, int error_buffer_size);

    /**
     * @brief Write a loaded map document to a JSON file.
     *
     * @param document Loaded map document.
     * @param path Filesystem path to write.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when serialization and file write succeed.
     */
    bool slayer3d_map_write_file(const slayer3d_map_document *document, const char *path, char *error_buffer,
                                 int error_buffer_size);

    /** @brief Destroy a loaded map document. */
    void slayer3d_map_destroy(slayer3d_map_document *document);

    /** @brief Free a string returned by slayer3d_map_to_json(). */
    void slayer3d_map_free_string(char *text);

    /** @brief Return the validated format version for a loaded map. */
    int slayer3d_map_get_version(const slayer3d_map_document *document);

    /** @brief Return the source path used by slayer3d_map_load_file(), if any. */
    const char *slayer3d_map_get_source_path(const slayer3d_map_document *document);

    /** @brief Return optional metadata.id from a loaded map. */
    const char *slayer3d_map_get_metadata_id(const slayer3d_map_document *document);

    /** @brief Return optional metadata.name from a loaded map. */
    const char *slayer3d_map_get_metadata_name(const slayer3d_map_document *document);

    /** @brief Return the number of material entries in a loaded map. */
    size_t slayer3d_map_get_material_count(const slayer3d_map_document *document);

    /** @brief Return the number of brush entries in a loaded map. */
    size_t slayer3d_map_get_brush_count(const slayer3d_map_document *document);

    /** @brief Return the number of actor entries in a loaded map. */
    size_t slayer3d_map_get_actor_count(const slayer3d_map_document *document);

    /** @brief Return the number of prefab entries in a loaded map. */
    size_t slayer3d_map_get_prefab_count(const slayer3d_map_document *document);

    /** @brief Return the number of light entries in a loaded map. */
    size_t slayer3d_map_get_light_count(const slayer3d_map_document *document);

    /** @brief Return the number of effect entries in a loaded map. */
    size_t slayer3d_map_get_effect_count(const slayer3d_map_document *document);

    /** @brief Return true when the loaded map authors a skybox selection. */
    bool slayer3d_map_has_skybox(const slayer3d_map_document *document);

    /** @brief Return the number of connection entries in a loaded map. */
    size_t slayer3d_map_get_connection_count(const slayer3d_map_document *document);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_MAP_H */

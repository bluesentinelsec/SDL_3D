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

#include "slayer3d/types.h"

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

    /** @brief Authored asset catalog kind. */
    typedef enum slayer3d_map_asset_kind
    {
        SLAYER3D_MAP_ASSET_TEXTURE,
        SLAYER3D_MAP_ASSET_MODEL,
        SLAYER3D_MAP_ASSET_SPRITE,
        SLAYER3D_MAP_ASSET_SKYBOX,
        SLAYER3D_MAP_ASSET_EFFECT,
    } slayer3d_map_asset_kind;

    /** @brief Loaded asset catalog entry. String pointers are owned by the map document. */
    typedef struct slayer3d_map_asset
    {
        const char *id;
        const char *path;
        size_t property_count;
    } slayer3d_map_asset;

    /** @brief Optional transform authored on a map object. */
    typedef struct slayer3d_map_transform
    {
        bool has_position;
        slayer3d_vec3 position;
        bool has_rotation;
        slayer3d_vec3 rotation;
        bool has_scale;
        slayer3d_vec3 scale;
        bool has_facing;
        slayer3d_vec3 facing;
    } slayer3d_map_transform;

    /** @brief Loaded material view. String pointers are owned by the map document. */
    typedef struct slayer3d_map_material
    {
        const char *id;
        const char *texture;
        bool has_color;
        slayer3d_color color;
        bool has_tint;
        slayer3d_color tint;
        size_t property_count;
    } slayer3d_map_material;

    /** @brief Box geometry view for version-1 brush maps. */
    typedef struct slayer3d_map_box_geometry
    {
        bool valid;
        slayer3d_vec3 min;
        slayer3d_vec3 max;
    } slayer3d_map_box_geometry;

    /** @brief Loaded brush view. String pointers are owned by the map document. */
    typedef struct slayer3d_map_brush
    {
        const char *id;
        const char *geometry_kind;
        slayer3d_map_box_geometry box;
        const char *material;
        bool has_color;
        slayer3d_color color;
        size_t face_count;
        size_t property_count;
    } slayer3d_map_brush;

    /** @brief Loaded actor view. String pointers are owned by the map document. */
    typedef struct slayer3d_map_actor
    {
        const char *id;
        const char *archetype;
        const char *model;
        const char *sprite;
        const char *primitive;
        const char *prefab;
        bool prefab_linked;
        const char *display_mode;
        slayer3d_map_transform transform;
        bool has_color;
        slayer3d_color color;
        size_t property_count;
    } slayer3d_map_actor;

    /**
     * @brief Minimal playable scene descriptor derived from a loaded map.
     *
     * This does not own geometry or actor data. It summarizes the runtime-facing
     * content and identifies the actor/object marked with `properties.type`,
     * `properties.actor-type`, or `properties.actor_type` equal to
     * `"player-character"` so a game can spawn a first-person controller from
     * an editor-authored map.
     */
    typedef struct slayer3d_map_playable_scene_desc
    {
        size_t texture_asset_count;
        size_t model_asset_count;
        size_t material_count;
        size_t playable_brush_count;
        size_t box_brush_count;
        size_t actor_count;
        bool has_player_character;
        size_t player_actor_index;
        const char *player_actor_id;
        slayer3d_vec3 player_position;
    } slayer3d_map_playable_scene_desc;

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

    /** @brief Return the number of entries in an authored asset catalog. */
    size_t slayer3d_map_get_asset_count(const slayer3d_map_document *document, slayer3d_map_asset_kind kind);

    /** @brief Read an asset catalog entry by kind and index. Returned strings are borrowed from @p document. */
    bool slayer3d_map_get_asset(const slayer3d_map_document *document, slayer3d_map_asset_kind kind, size_t index,
                                slayer3d_map_asset *out_asset);

    /** @brief Read a material by index. Returned string pointers are borrowed from @p document. */
    bool slayer3d_map_get_material(const slayer3d_map_document *document, size_t index,
                                   slayer3d_map_material *out_material);

    /** @brief Read a brush by index. Returned string pointers are borrowed from @p document. */
    bool slayer3d_map_get_brush(const slayer3d_map_document *document, size_t index, slayer3d_map_brush *out_brush);

    /** @brief Read an actor by index. Returned string pointers are borrowed from @p document. */
    bool slayer3d_map_get_actor(const slayer3d_map_document *document, size_t index, slayer3d_map_actor *out_actor);

    /** @brief Return the number of root-level arbitrary map properties. */
    size_t slayer3d_map_get_property_count(const slayer3d_map_document *document);

    /** @brief Return the root-level property key at @p property_index, or NULL. */
    const char *slayer3d_map_get_property_key(const slayer3d_map_document *document, size_t property_index);

    /** @brief Serialize a root-level property value to JSON. Free with slayer3d_map_free_string(). */
    bool slayer3d_map_get_property_json(const slayer3d_map_document *document, const char *key, char **out_json,
                                        size_t *out_json_size, char *error_buffer, int error_buffer_size);

    /** @brief Return the asset property key at @p property_index, or NULL. */
    const char *slayer3d_map_get_asset_property_key(const slayer3d_map_document *document, slayer3d_map_asset_kind kind,
                                                    size_t asset_index, size_t property_index);

    /** @brief Serialize an asset property value to JSON. Free with slayer3d_map_free_string(). */
    bool slayer3d_map_get_asset_property_json(const slayer3d_map_document *document, slayer3d_map_asset_kind kind,
                                              size_t asset_index, const char *key, char **out_json,
                                              size_t *out_json_size, char *error_buffer, int error_buffer_size);

    /** @brief Return the material property key at @p property_index, or NULL. */
    const char *slayer3d_map_get_material_property_key(const slayer3d_map_document *document, size_t material_index,
                                                       size_t property_index);

    /** @brief Serialize a material property value to JSON. Free with slayer3d_map_free_string(). */
    bool slayer3d_map_get_material_property_json(const slayer3d_map_document *document, size_t material_index,
                                                 const char *key, char **out_json, size_t *out_json_size,
                                                 char *error_buffer, int error_buffer_size);

    /** @brief Return the brush property key at @p property_index, or NULL. */
    const char *slayer3d_map_get_brush_property_key(const slayer3d_map_document *document, size_t brush_index,
                                                    size_t property_index);

    /** @brief Serialize a brush property value to JSON. Free with slayer3d_map_free_string(). */
    bool slayer3d_map_get_brush_property_json(const slayer3d_map_document *document, size_t brush_index,
                                              const char *key, char **out_json, size_t *out_json_size,
                                              char *error_buffer, int error_buffer_size);

    /** @brief Return the actor property key at @p property_index, or NULL. */
    const char *slayer3d_map_get_actor_property_key(const slayer3d_map_document *document, size_t actor_index,
                                                    size_t property_index);

    /** @brief Serialize an actor property value to JSON. Free with slayer3d_map_free_string(). */
    bool slayer3d_map_get_actor_property_json(const slayer3d_map_document *document, size_t actor_index,
                                              const char *key, char **out_json, size_t *out_json_size,
                                              char *error_buffer, int error_buffer_size);

    /**
     * @brief Build a minimal playable scene descriptor from a loaded map.
     *
     * The descriptor identifies playable brush geometry and placed actors, then
     * requires the first actor/object whose arbitrary property bag contains
     * `type`, `actor-type`, or `actor_type` equal to `"player-character"`. The
     * returned strings are borrowed from @p document.
     *
     * @return true when a playable scene descriptor was built and a
     * player-character actor was found.
     */
    bool slayer3d_map_build_playable_scene_desc(const slayer3d_map_document *document,
                                                slayer3d_map_playable_scene_desc *out_desc, char *error_buffer,
                                                int error_buffer_size);

    /**
     * @brief Write a minimal data-game package that can run a map as an FPS brush scene.
     *
     * This emits `playable_map.game.json` and `scenes/play.scene.json` under
     * @p output_dir. The generated game uses the existing data-game
     * `controller.fps_brush` component, converts map box and plane brushes into
     * runtime brush-world planes, and spawns the player at the first
     * actor/object marked with `properties.type`, `properties.actor-type`, or
     * `properties.actor_type` equal to `"player-character"`.
     *
     * The generated package is a first playable bridge. It preserves geometry,
     * material colors, material texture references, and the player spawn, but
     * does not copy external texture or model files into @p output_dir.
     */
    bool slayer3d_map_write_playable_game_files(const slayer3d_map_document *document, const char *output_dir,
                                                char *error_buffer, int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_MAP_H */

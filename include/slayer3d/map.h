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

    /** @brief Optional map-level fog state. String pointers are owned by the map document. */
    typedef struct slayer3d_map_fog
    {
        bool enabled;
        const char *mode;
        bool has_color;
        slayer3d_color color;
        bool has_start;
        float start;
        bool has_end;
        float end;
        bool has_density;
        float density;
    } slayer3d_map_fog;

    /**
     * @brief Map-level lighting and presentation state.
     *
     * Missing fields are expanded to engine defaults so callers can query this
     * structure for both newly authored maps and older maps without a `global`
     * object. Arbitrary map-global key/value data lives under
     * `global.properties`.
     */
    typedef struct slayer3d_map_global_state
    {
        bool has_ambient_light;
        slayer3d_color ambient_light;
        bool has_clear_color;
        slayer3d_color clear_color;
        bool has_exposure;
        float exposure;
        const char *tonemap;
        const char *lighting_preview_quality;
        slayer3d_map_fog fog;
        size_t property_count;
    } slayer3d_map_global_state;

    /** @brief Optional light animation/modulation authored on a map light. */
    typedef struct slayer3d_map_light_animation
    {
        bool enabled;
        const char *type;
        const char *preset;
        bool has_rate_hz;
        float rate_hz;
        bool has_amplitude;
        float amplitude;
        bool has_phase;
        float phase;
        bool has_min_intensity;
        float min_intensity;
        bool has_max_intensity;
        float max_intensity;
        bool has_axis;
        slayer3d_vec3 axis;
        bool has_radius;
        float radius;
        size_t property_count;
    } slayer3d_map_light_animation;

    /** @brief Loaded light view. String pointers are owned by the map document. */
    typedef struct slayer3d_map_light
    {
        const char *id;
        const char *source_actor;
        const char *kind;
        const char *type;
        slayer3d_map_transform transform;
        bool has_direction;
        slayer3d_vec3 direction;
        bool has_color;
        slayer3d_color color;
        bool has_intensity;
        float intensity;
        bool has_range;
        float range;
        bool has_inner_angle_degrees;
        float inner_angle_degrees;
        bool has_outer_angle_degrees;
        float outer_angle_degrees;
        bool has_width;
        float width;
        bool has_height;
        float height;
        bool has_radius;
        float radius;
        bool has_casts_shadow;
        bool casts_shadow;
        const char *shadow_mode;
        const char *falloff;
        const char *bake_group;
        slayer3d_map_light_animation animation;
        size_t property_count;
    } slayer3d_map_light;

    /** @brief Quality target for map-oriented lighting build/bake jobs. */
    typedef enum slayer3d_map_lighting_build_quality
    {
        /** @brief Fast interactive preview suitable for editor iteration. */
        SLAYER3D_MAP_LIGHTING_BUILD_PREVIEW = 0,
        /** @brief Default quality/performance balance for editor and CLI use. */
        SLAYER3D_MAP_LIGHTING_BUILD_BALANCED = 1,
        /** @brief Slower final-quality build target for shipping/export. */
        SLAYER3D_MAP_LIGHTING_BUILD_FINAL = 2,
    } slayer3d_map_lighting_build_quality;

    /**
     * @brief Options for planning map lighting work.
     *
     * This structure is intentionally map-oriented rather than editor-specific
     * so the same contract can be used by the editor GUI, editor/asset CLI
     * commands, and caller code.
     */
    typedef struct slayer3d_map_lighting_build_options
    {
        slayer3d_map_lighting_build_quality quality;
        /** @brief Maximum dynamic/runtime lights expected by the target renderer; 0 uses the engine default. */
        size_t max_dynamic_lights;
        /** @brief Maximum static/baked lights accepted by the build pipeline; 0 uses the engine default. */
        size_t max_static_lights;
        /** @brief When true, baked/static lights may also produce editor/runtime preview lights. */
        bool include_dynamic_preview;
    } slayer3d_map_lighting_build_options;

    /**
     * @brief Summary of lighting work required by an authored map.
     *
     * A plan is a non-owning summary. It does not bake lighting by itself; it is
     * the shared front door for later bake/compute commands and UI status.
     */
    typedef struct slayer3d_map_lighting_build_plan
    {
        slayer3d_map_lighting_build_quality quality;
        size_t total_light_count;
        size_t dynamic_light_count;
        size_t static_light_count;
        size_t area_light_count;
        size_t runtime_light_count;
        size_t bake_light_count;
        size_t max_dynamic_lights;
        size_t max_static_lights;
        bool requires_static_bake;
        bool has_dynamic_preview;
        bool dynamic_light_budget_exceeded;
        bool static_light_budget_exceeded;
    } slayer3d_map_lighting_build_plan;

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
        size_t light_count;
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

/** @brief Maximum number of animated sky layers a map skybox may author. */
#define SLAYER3D_MAP_SKY_MAX_LAYERS 8

    /** @brief One animated sky layer view. String pointers are owned by the map document. */
    typedef struct slayer3d_map_sky_layer
    {
        const char *texture; /**< Asset id or project-relative texture reference. */
        float scroll_x;      /**< UV scroll speed along U in texture repeats per second. */
        float scroll_y;      /**< UV scroll speed along V in texture repeats per second. */
        float scale;         /**< Texture tiling multiplier, defaults to 1. */
        float opacity;       /**< Layer opacity in (0, 1], defaults to 1. */
        float depth;         /**< Sky dome depth factor in (0, 1], defaults to 1. */
        bool has_tint;       /**< True when the layer authors an RGB tint. */
        slayer3d_color tint; /**< Layer tint, white when has_tint is false. */
    } slayer3d_map_sky_layer;

    /**
     * @brief Global map sky/environment view. String pointers are owned by the map document.
     *
     * The mode is the authored mode string when present, otherwise it is
     * inferred: maps with layers report "layers", maps with faces/asset/preset
     * report "cubemap", and maps without sky data report "none".
     */
    typedef struct slayer3d_map_sky
    {
        const char *mode;     /**< Effective sky mode: "none", "cubemap", or "layers". */
        const char *preset;   /**< Optional built-in preset id such as "sunset". */
        const char *asset;    /**< Optional single skybox asset reference. */
        const char *faces[6]; /**< Cubemap faces (+X, -X, +Y, -Y, +Z, -Z) or NULLs. */
        bool has_faces;       /**< True when all six cubemap faces are authored. */
        float size;           /**< Skybox size, defaults to 400. */
        size_t layer_count;   /**< Number of authored animated layers. */
    } slayer3d_map_sky;

    /**
     * @brief Read the global map sky configuration.
     *
     * Returns false when @p document has no skybox data; @p out_sky is then
     * zeroed with mode "none".
     */
    bool slayer3d_map_get_sky(const slayer3d_map_document *document, slayer3d_map_sky *out_sky);

    /** @brief Read one animated sky layer by index. Defaults are applied to omitted fields. */
    bool slayer3d_map_get_sky_layer(const slayer3d_map_document *document, size_t index,
                                    slayer3d_map_sky_layer *out_layer);

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

    /** @brief Read a light by index. Returned string pointers are borrowed from @p document. */
    bool slayer3d_map_get_light(const slayer3d_map_document *document, size_t index, slayer3d_map_light *out_light);

    /** @brief Read map-level global lighting and presentation state. */
    bool slayer3d_map_get_global_state(const slayer3d_map_document *document, slayer3d_map_global_state *out_global);

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

    /** @brief Return the light property key at @p property_index, or NULL. */
    const char *slayer3d_map_get_light_property_key(const slayer3d_map_document *document, size_t light_index,
                                                    size_t property_index);

    /** @brief Serialize a light property value to JSON. Free with slayer3d_map_free_string(). */
    bool slayer3d_map_get_light_property_json(const slayer3d_map_document *document, size_t light_index,
                                              const char *key, char **out_json, size_t *out_json_size,
                                              char *error_buffer, int error_buffer_size);

    /** @brief Return the global-state property key at @p property_index, or NULL. */
    const char *slayer3d_map_get_global_property_key(const slayer3d_map_document *document, size_t property_index);

    /** @brief Serialize a global-state property value to JSON. Free with slayer3d_map_free_string(). */
    bool slayer3d_map_get_global_property_json(const slayer3d_map_document *document, const char *key, char **out_json,
                                               size_t *out_json_size, char *error_buffer, int error_buffer_size);

    /** @brief Fill map lighting build options with engine/editor default values. */
    void slayer3d_map_init_lighting_build_options(slayer3d_map_lighting_build_options *options);

    /**
     * @brief Build a lighting work plan from a loaded map document.
     *
     * This generalized API is intended to be the common entry point for editor
     * GUI bake buttons, future CLI bake commands, and caller code. Budget
     * overages are reported in @p out_plan instead of causing failure so tools
     * can show actionable diagnostics and still display the complete plan.
     *
     * @return true when the plan was built. Returns false only for invalid
     * arguments or unreadable map data.
     */
    bool slayer3d_map_build_lighting_plan(const slayer3d_map_document *document,
                                          const slayer3d_map_lighting_build_options *options,
                                          slayer3d_map_lighting_build_plan *out_plan, char *error_buffer,
                                          int error_buffer_size);

    /**
     * @brief Build a JSON manifest describing planned static-lighting artifacts.
     *
     * This uses the same map-oriented options as
     * slayer3d_map_build_lighting_plan(). The returned manifest is metadata
     * only: it reserves a stable contract for editor GUI commands, editor CLI
     * commands, and caller code before concrete lightmap/vertex-bake payloads
     * are generated. Free @p out_json with slayer3d_map_free_string().
     */
    bool slayer3d_map_build_lighting_artifact_manifest_json(const slayer3d_map_document *document,
                                                            const slayer3d_map_lighting_build_options *options,
                                                            char **out_json, size_t *out_json_size, char *error_buffer,
                                                            int error_buffer_size);

    /**
     * @brief Build a deterministic static-lighting artifact JSON payload.
     *
     * This is the first concrete static-light output shared by the editor GUI,
     * editor CLI, and caller code. It emits per-face irradiance samples for box
     * brushes using baked/static map lights plus global ambient light. The
     * payload is intentionally simple and self-contained; future bake backends
     * may replace or augment it with atlas/lightmap textures while preserving
     * this API shape. Free @p out_json with slayer3d_map_free_string().
     */
    bool slayer3d_map_build_static_lighting_artifact_json(const slayer3d_map_document *document,
                                                          const slayer3d_map_lighting_build_options *options,
                                                          char **out_json, size_t *out_json_size, char *error_buffer,
                                                          int error_buffer_size);

    /**
     * @brief Validate a static-lighting artifact JSON payload.
     *
     * This validates the `slayer3d.lighting_static.v0` artifact emitted by
     * slayer3d_map_build_static_lighting_artifact_json(), the editor CLI, and
     * playable-map package export. It is intentionally independent from loaded
     * map documents so callers can reject malformed build artifacts before
     * consuming them at runtime.
     */
    bool slayer3d_map_validate_static_lighting_artifact_json(const char *json, size_t json_size, char *error_buffer,
                                                             int error_buffer_size);

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
     * @p output_dir. Maps that require static lighting also emit
     * `lighting/static.default.json` and declare it under
     * `world.lighting_artifacts`. The generated game uses the existing data-game
     * `controller.fps_brush` component, converts map box and plane brushes into
     * runtime brush-world planes, and spawns the player at the first actor/object
     * marked with `properties.type`, `properties.actor-type`, or
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

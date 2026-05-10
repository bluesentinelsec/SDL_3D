#ifndef SLAYER3D_SPRITE_ASSET_H
#define SLAYER3D_SPRITE_ASSET_H

#include <stdbool.h>

#include "slayer3d/asset.h"
#include "slayer3d/effects.h"
#include "slayer3d/sprite_actor.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum slayer3d_sprite_asset_source_kind
    {
        SLAYER3D_SPRITE_ASSET_SOURCE_SHEET = 0,
        SLAYER3D_SPRITE_ASSET_SOURCE_FILES = 1
    } slayer3d_sprite_asset_source_kind;

    /**
     * @brief Generic authored sprite source description.
     *
     * The source can be a sprite sheet or an explicit file list. Sheet sources
     * use @p sheet_path plus the grid metadata. File-list sources use
     * @p base_paths for the fallback rotation set and @p frame_paths for the
     * animation frames, both in frame-major, direction-minor order.
     */
    typedef struct slayer3d_sprite_asset_source
    {
        slayer3d_sprite_asset_source_kind kind;
        const char *sheet_path;
        const char *const *base_paths;
        const char *const *frame_paths;
        const char *shader_vertex_path;
        const char *shader_fragment_path;
        int frame_width;
        int frame_height;
        int columns;
        int rows;
        int frame_count;
        int direction_count;
        float fps;
        bool loop;
        bool lighting;
        bool emissive;
        float visual_ground_offset;
        const char *effect;
        float effect_delay;
        float effect_duration;
    } slayer3d_sprite_asset_source;

    /**
     * @brief Loaded sprite textures and rotation sets.
     *
     * The runtime owns the generated textures and rotation sets. Use
     * slayer3d_sprite_asset_free() to release them.
     */
    typedef struct slayer3d_sprite_asset_runtime
    {
        slayer3d_texture2d *base_textures;
        int base_texture_count;
        slayer3d_texture2d *animation_textures;
        int animation_texture_count;
        slayer3d_sprite_rotation_set base_rotations;
        slayer3d_sprite_rotation_set *animation_frames;
        int animation_frame_count;
        int direction_count;
        float fps;
        bool loop;
        bool lighting;
        bool emissive;
        float visual_ground_offset;
        const char *effect;
        float effect_delay;
        float effect_duration;
        char *shader_vertex_source;
        char *shader_fragment_source;
    } slayer3d_sprite_asset_runtime;

    /**
     * @brief Load a sprite asset from a generic source description.
     *
     * Sheet sources are decoded once and sliced into textures. File-list
     * sources load the explicit fallback rotation set and animation frames
     * directly. The loader keeps the generic runtime behavior unchanged:
     * callers still receive billboard-ready textures and rotation sets.
     *
     * @param assets Optional asset resolver for authored asset:// paths.
     * @param source Generic source description.
     * @param out_sprite Receives the loaded runtime.
     * @param error_buffer Optional human-readable error output.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the sprite was loaded successfully.
     */
    bool slayer3d_sprite_asset_load(const slayer3d_asset_resolver *assets, const slayer3d_sprite_asset_source *source,
                                    slayer3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                    int error_buffer_size);

    /**
     * @brief Load a sprite asset from a JSON manifest on disk.
     *
     * The manifest format is a small, standalone variant of the authored
     * sprite description model used by the game-data runtime. It is intended
     * for demos or tools that want data-authored sprite loading without
     * instantiating a full game-data runtime. Relative paths are resolved
     * against the manifest directory, and optional `asset_roots` entries can
     * mount additional filesystem roots for shared art without using `..`
     * traversal in the authored sprite paths.
     */
    bool slayer3d_sprite_asset_load_file(const char *path, slayer3d_sprite_asset_runtime *out_sprite,
                                         char *error_buffer, int error_buffer_size);

    /**
     * @brief Release textures and rotation sets owned by a sprite runtime.
     */
    void slayer3d_sprite_asset_free(slayer3d_sprite_asset_runtime *sprite);

    /** @brief Return the authored fallback rotation set, or NULL. */
    const slayer3d_sprite_rotation_set *slayer3d_sprite_asset_base_rotations(
        const slayer3d_sprite_asset_runtime *sprite);

    /** @brief Return the authored animation frame rotation sets, or NULL. */
    const slayer3d_sprite_rotation_set *slayer3d_sprite_asset_animation_frames(
        const slayer3d_sprite_asset_runtime *sprite);

    /** @brief Return the number of authored animation frames. */
    int slayer3d_sprite_asset_animation_frame_count(const slayer3d_sprite_asset_runtime *sprite);

    /** @brief Apply a loaded sprite runtime to a sprite actor. */
    void slayer3d_sprite_asset_apply_actor(slayer3d_sprite_actor *actor, const slayer3d_sprite_asset_runtime *sprite);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_SPRITE_ASSET_H */

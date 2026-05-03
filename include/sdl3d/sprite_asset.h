#ifndef SDL3D_SPRITE_ASSET_H
#define SDL3D_SPRITE_ASSET_H

#include <stdbool.h>

#include "sdl3d/asset.h"
#include "sdl3d/effects.h"
#include "sdl3d/sprite_actor.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum sdl3d_sprite_asset_source_kind
    {
        SDL3D_SPRITE_ASSET_SOURCE_SHEET = 0,
        SDL3D_SPRITE_ASSET_SOURCE_FILES = 1
    } sdl3d_sprite_asset_source_kind;

    /**
     * @brief Generic authored sprite source description.
     *
     * The source can be a sprite sheet or an explicit file list. Sheet sources
     * use @p sheet_path plus the grid metadata. File-list sources use
     * @p base_paths for the fallback rotation set and @p frame_paths for the
     * animation frames, both in frame-major, direction-minor order.
     */
    typedef struct sdl3d_sprite_asset_source
    {
        sdl3d_sprite_asset_source_kind kind;
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
    } sdl3d_sprite_asset_source;

    /**
     * @brief Loaded sprite textures and rotation sets.
     *
     * The runtime owns the generated textures and rotation sets. Use
     * sdl3d_sprite_asset_free() to release them.
     */
    typedef struct sdl3d_sprite_asset_runtime
    {
        sdl3d_texture2d *base_textures;
        int base_texture_count;
        sdl3d_texture2d *animation_textures;
        int animation_texture_count;
        sdl3d_sprite_rotation_set base_rotations;
        sdl3d_sprite_rotation_set *animation_frames;
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
    } sdl3d_sprite_asset_runtime;

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
    bool sdl3d_sprite_asset_load(const sdl3d_asset_resolver *assets, const sdl3d_sprite_asset_source *source,
                                 sdl3d_sprite_asset_runtime *out_sprite, char *error_buffer, int error_buffer_size);

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
    bool sdl3d_sprite_asset_load_file(const char *path, sdl3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                      int error_buffer_size);

    /**
     * @brief Release textures and rotation sets owned by a sprite runtime.
     */
    void sdl3d_sprite_asset_free(sdl3d_sprite_asset_runtime *sprite);

    /** @brief Return the authored fallback rotation set, or NULL. */
    const sdl3d_sprite_rotation_set *sdl3d_sprite_asset_base_rotations(const sdl3d_sprite_asset_runtime *sprite);

    /** @brief Return the authored animation frame rotation sets, or NULL. */
    const sdl3d_sprite_rotation_set *sdl3d_sprite_asset_animation_frames(const sdl3d_sprite_asset_runtime *sprite);

    /** @brief Return the number of authored animation frames. */
    int sdl3d_sprite_asset_animation_frame_count(const sdl3d_sprite_asset_runtime *sprite);

    /** @brief Apply a loaded sprite runtime to a sprite actor. */
    void sdl3d_sprite_asset_apply_actor(sdl3d_sprite_actor *actor, const sdl3d_sprite_asset_runtime *sprite);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_SPRITE_ASSET_H */

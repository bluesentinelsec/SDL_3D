/**
 * @file game_data_assets.h
 * @brief JSON-authored game data asset descriptors.
 */

#ifndef SLAYER3D_GAME_DATA_ASSETS_H
#define SLAYER3D_GAME_DATA_ASSETS_H

#include <stdbool.h>

#include "slayer3d/audio.h"
#include "slayer3d/font.h"
#include "slayer3d/sprite_asset.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Authored font asset descriptor. */
    typedef struct slayer3d_game_data_font_asset
    {
        /** @brief Stable asset id, such as `font.hud`. */
        const char *id;
        /** @brief Built-in font id when @p builtin is true. */
        slayer3d_builtin_font builtin_id;
        /** @brief True when this asset refers to an SLAYER3D built-in font. */
        bool builtin;
        /** @brief External font path when @p builtin is false, or NULL. */
        const char *path;
        /** @brief Requested font pixel size. */
        float size;
    } slayer3d_game_data_font_asset;

    /** @brief Callback for authored font asset descriptors. */
    typedef bool (*slayer3d_game_data_font_asset_fn)(void *userdata, const slayer3d_game_data_font_asset *font);

    /** @brief Authored image asset descriptor. */
    typedef struct slayer3d_game_data_image_asset
    {
        /** @brief Stable asset id, such as `image.logo`. */
        const char *id;
        /** @brief Virtual or filesystem path to the image bytes, or NULL. */
        const char *path;
        /** @brief Optional sprite asset id when the image is sprite-backed. */
        const char *sprite;
    } slayer3d_game_data_image_asset;

    /** @brief Authored 3D model asset descriptor. */
    typedef struct slayer3d_game_data_model_asset
    {
        /** @brief Stable asset id, such as `model.dragon`. */
        const char *id;
        /** @brief Virtual or filesystem path to the model source. */
        const char *path;
    } slayer3d_game_data_model_asset;

    /** @brief Callback for authored 3D model asset descriptors. */
    typedef bool (*slayer3d_game_data_model_asset_fn)(void *userdata, const slayer3d_game_data_model_asset *model);

    /** @brief Authored scene skybox descriptor using six image asset ids. */
    typedef struct slayer3d_game_data_scene_skybox
    {
        /** @brief +X face image asset id. */
        const char *pos_x;
        /** @brief -X face image asset id. */
        const char *neg_x;
        /** @brief +Y face image asset id. */
        const char *pos_y;
        /** @brief -Y face image asset id. */
        const char *neg_y;
        /** @brief +Z face image asset id. */
        const char *pos_z;
        /** @brief -Z face image asset id. */
        const char *neg_z;
        /** @brief Skybox cube half-size in world units. */
        float size;
    } slayer3d_game_data_scene_skybox;

    /** @brief Authored sound-effect asset descriptor. */
    typedef struct slayer3d_game_data_sound_asset
    {
        /** @brief Stable asset id, such as `sound.ui.select`. */
        const char *id;
        /** @brief Virtual or filesystem path to the sound bytes. */
        const char *path;
        /** @brief Default authored gain before bus volume. */
        float volume;
        /** @brief Default playback pitch. */
        float pitch;
        /** @brief Default stereo pan in [-1, 1]. */
        float pan;
        /** @brief Logical mix bus used by default. */
        slayer3d_audio_bus bus;
    } slayer3d_game_data_sound_asset;

    /** @brief Callback for authored sound-effect asset descriptors. */
    typedef bool (*slayer3d_game_data_sound_asset_fn)(void *userdata, const slayer3d_game_data_sound_asset *sound);

    /** @brief Authored music asset descriptor. */
    typedef struct slayer3d_game_data_music_asset
    {
        /** @brief Stable asset id, such as `music.title`. */
        const char *id;
        /** @brief Virtual or filesystem path to the stream bytes. */
        const char *path;
        /** @brief Default authored gain before bus volume. */
        float volume;
        /** @brief Whether playback should loop by default. */
        bool loop;
    } slayer3d_game_data_music_asset;

    /** @brief Callback for authored music asset descriptors. */
    typedef bool (*slayer3d_game_data_music_asset_fn)(void *userdata, const slayer3d_game_data_music_asset *music);

    /** @brief Authored ambient-zone asset descriptor. */
    typedef struct slayer3d_game_data_ambient_asset
    {
        /** @brief Stable asset id, such as `ambient.sewer.loop`. */
        const char *id;
        /** @brief Non-negative ambient zone id used by sector payloads. */
        int ambient_id;
        /** @brief Virtual or filesystem path to the ambient stream bytes. */
        const char *path;
        /** @brief Default authored gain before bus volume. */
        float volume;
        /** @brief Whether playback should loop by default. */
        bool loop;
    } slayer3d_game_data_ambient_asset;

    /** @brief Callback for authored ambient-zone asset descriptors. */
    typedef bool (*slayer3d_game_data_ambient_asset_fn)(void *userdata,
                                                        const slayer3d_game_data_ambient_asset *ambient);

    /** @brief Authored sprite asset descriptor. */
    typedef struct slayer3d_game_data_sprite_asset
    {
        /** @brief Stable asset id, such as `sprite.robot.walk`. */
        const char *id;
        /** @brief Source kind: sheet image or explicit file list. */
        slayer3d_sprite_asset_source_kind source_kind;
        /** @brief Virtual or filesystem path to the sprite source image. */
        const char *path;
        /** @brief Optional vertex shader source path for a sprite-specific GPU program. */
        const char *shader_vertex_path;
        /** @brief Optional fragment shader source path for a sprite-specific GPU program. */
        const char *shader_fragment_path;
        /** @brief Frame width in pixels for a grid or atlas source. */
        int frame_width;
        /** @brief Frame height in pixels for a grid or atlas source. */
        int frame_height;
        /** @brief Number of frames across the source image. */
        int columns;
        /** @brief Number of rows across the source image. */
        int rows;
        /** @brief Number of animation frames in the authored source. */
        int frame_count;
        /** @brief Number of directional frames per animation frame. */
        int direction_count;
        /** @brief Playback rate in frames per second. */
        float fps;
        /** @brief Whether playback wraps after the last frame. */
        bool loop;
        /** @brief Whether the sprite participates in dynamic lighting. */
        bool lighting;
        /** @brief Whether the sprite is emissive. */
        bool emissive;
        /** @brief Offset from logical feet/contact point to visible feet. */
        float visual_ground_offset;
        /** @brief Optional sprite overlay effect id, such as `melt`. */
        const char *effect;
        /** @brief Delay before the effect begins, in presentation seconds. */
        float effect_delay;
        /** @brief Duration of the effect ramp, in seconds. */
        float effect_duration;
    } slayer3d_game_data_sprite_asset;

#ifdef __cplusplus
}
#endif

#endif

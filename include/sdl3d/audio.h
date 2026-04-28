/**
 * @file audio.h
 * @brief Miniaudio-backed sound effects, music, and ambient-zone playback.
 */

#ifndef SDL3D_AUDIO_H
#define SDL3D_AUDIO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque audio engine handle. */
    typedef struct sdl3d_audio_engine sdl3d_audio_engine;

    /** @brief Opaque decoded sound-effect clip. */
    typedef struct sdl3d_audio_clip sdl3d_audio_clip;

    /**
     * @brief Logical audio mix bus.
     *
     * Bus volumes are multiplied by per-sound authored volumes. This lets games
     * expose independent settings for music, sound effects, dialogue, and
     * ambience without changing gameplay code or authored sound assets.
     */
    typedef enum sdl3d_audio_bus
    {
        SDL3D_AUDIO_BUS_SOUND_EFFECTS = 0, /**< One-shot gameplay and UI effects. */
        SDL3D_AUDIO_BUS_MUSIC = 1,         /**< Long-running music streams. */
        SDL3D_AUDIO_BUS_DIALOGUE = 2,      /**< Spoken dialogue and voice-over. */
        SDL3D_AUDIO_BUS_AMBIENCE = 3,      /**< Environmental loops and ambient zones. */
        SDL3D_AUDIO_BUS_COUNT = 4          /**< Number of built-in buses. */
    } sdl3d_audio_bus;

    /**
     * @brief One-shot playback parameters.
     */
    typedef struct sdl3d_audio_play_desc
    {
        float volume;        /**< Linear gain before bus volume. Values above 1.0 are allowed. */
        float pitch;         /**< Playback pitch ratio. Use 1.0 for original pitch. */
        float pan;           /**< Stereo pan in [-1, 1]. */
        sdl3d_audio_bus bus; /**< Mix bus used for this one-shot. */
    } sdl3d_audio_play_desc;

    /**
     * @brief Ambient sound mapping for sector ambient ids.
     *
     * `ambient_id == 0` is conventionally silence. A zone with NULL path also
     * stops ambient playback when selected.
     */
    typedef struct sdl3d_audio_ambient
    {
        int ambient_id;   /**< Sector ambient_sound_id this entry handles. */
        const char *path; /**< Audio file path, or NULL for silence. */
        float volume;     /**< Target music/ambient volume. */
        bool loop;        /**< Whether the ambient stream loops. */
    } sdl3d_audio_ambient;

    /**
     * @brief Return default one-shot playback parameters.
     */
    sdl3d_audio_play_desc sdl3d_audio_play_desc_default(void);

    /**
     * @brief Create and start the audio engine.
     *
     * Returns false with SDL_GetError() set when the platform audio device or
     * miniaudio engine cannot be initialized.
     */
    bool sdl3d_audio_create(sdl3d_audio_engine **out_audio);

    /**
     * @brief Destroy an audio engine, stopping all sounds first.
     *
     * Safe to call with NULL.
     */
    void sdl3d_audio_destroy(sdl3d_audio_engine *audio);

    /**
     * @brief Advance fades and release finished one-shot sounds.
     */
    void sdl3d_audio_update(sdl3d_audio_engine *audio, float dt);

    /**
     * @brief Set global gain applied by the miniaudio engine.
     */
    void sdl3d_audio_set_master_volume(sdl3d_audio_engine *audio, float volume);

    /**
     * @brief Return current global gain, or 0 when audio is NULL.
     */
    float sdl3d_audio_get_master_volume(const sdl3d_audio_engine *audio);

    /**
     * @brief Set the gain for one logical audio bus.
     *
     * Values below zero are clamped to zero. Changing a bus affects subsequent
     * playback immediately and retargets currently active music/ambient streams
     * that use the bus.
     */
    void sdl3d_audio_set_bus_volume(sdl3d_audio_engine *audio, sdl3d_audio_bus bus, float volume);

    /**
     * @brief Return a logical bus gain, or 0 when audio or the bus is invalid.
     */
    float sdl3d_audio_get_bus_volume(const sdl3d_audio_engine *audio, sdl3d_audio_bus bus);

    /**
     * @brief Load a decoded sound-effect clip for repeated low-latency playback.
     */
    bool sdl3d_audio_load_clip(sdl3d_audio_engine *audio, const char *path, sdl3d_audio_clip **out_clip);

    /**
     * @brief Free a sound-effect clip.
     *
     * Safe to call with NULL. Active one-shot instances already started from
     * this clip continue independently.
     */
    void sdl3d_audio_clip_destroy(sdl3d_audio_clip *clip);

    /**
     * @brief Play a one-shot instance of a loaded clip.
     */
    bool sdl3d_audio_play_clip(sdl3d_audio_engine *audio, const sdl3d_audio_clip *clip,
                               const sdl3d_audio_play_desc *desc);

    /**
     * @brief Load and play a one-shot sound file.
     *
     * This is convenient for infrequent effects. Repeated effects should use
     * sdl3d_audio_load_clip() plus sdl3d_audio_play_clip().
     */
    bool sdl3d_audio_play_sound_file(sdl3d_audio_engine *audio, const char *path, const sdl3d_audio_play_desc *desc);

    /**
     * @brief Crossfade to a music stream.
     *
     * Passing NULL or an empty path stops current music with the requested
     * fade. The fade is driven by sdl3d_audio_update().
     */
    bool sdl3d_audio_play_music(sdl3d_audio_engine *audio, const char *path, bool loop, float volume,
                                float fade_seconds);

    /**
     * @brief Stop current music with an optional fade.
     */
    void sdl3d_audio_stop_music(sdl3d_audio_engine *audio, float fade_seconds);

    /**
     * @brief Fade current music toward a new authored volume.
     *
     * The target volume is still multiplied by the music bus volume. Calling
     * with a non-positive fade applies the target immediately.
     */
    void sdl3d_audio_fade_music(sdl3d_audio_engine *audio, float volume, float fade_seconds);

    /**
     * @brief Stop all active one-shot instances on a bus.
     */
    void sdl3d_audio_stop_bus(sdl3d_audio_engine *audio, sdl3d_audio_bus bus);

    /**
     * @brief Select an ambient stream by id and crossfade to it.
     *
     * If no matching id exists, playback fades to silence.
     */
    bool sdl3d_audio_set_ambient(sdl3d_audio_engine *audio, const sdl3d_audio_ambient *ambients, int ambient_count,
                                 int ambient_id, float fade_seconds);

    /**
     * @brief Return the currently selected ambient id.
     */
    int sdl3d_audio_get_current_ambient_id(const sdl3d_audio_engine *audio);

#ifdef __cplusplus
}
#endif

#endif

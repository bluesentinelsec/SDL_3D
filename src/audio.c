#include "sdl3d/audio.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "miniaudio.h"

typedef struct sdl3d_active_sound
{
    ma_sound sound;
    bool loaded;
} sdl3d_active_sound;

typedef struct sdl3d_music_slot
{
    ma_sound sound;
    bool loaded;
    char *path;
    int ambient_id;
    float target_volume;
    bool loop;
} sdl3d_music_slot;

struct sdl3d_audio_engine
{
    ma_engine engine;
    bool loaded;

    sdl3d_active_sound *active_sounds;
    int active_count;
    int active_capacity;

    sdl3d_music_slot *music_current;
    sdl3d_music_slot *music_next;
    float music_fade_elapsed;
    float music_fade_duration;
    float music_current_start_volume;
    float music_next_target_volume;
    bool music_fading;
    int current_ambient_id;
};

struct sdl3d_audio_clip
{
    sdl3d_audio_engine *audio;
    ma_sound sound;
    bool loaded;
};

static float clamp_non_negative(float value)
{
    return value < 0.0f ? 0.0f : value;
}

static void active_sound_uninit(sdl3d_active_sound *active)
{
    if (active != NULL && active->loaded)
    {
        ma_sound_uninit(&active->sound);
        active->loaded = false;
    }
}

static void music_slot_destroy(sdl3d_music_slot *slot)
{
    if (slot == NULL)
    {
        return;
    }

    if (slot->loaded)
    {
        ma_sound_uninit(&slot->sound);
    }
    SDL_free(slot->path);
    SDL_free(slot);
}

static bool ensure_active_capacity(sdl3d_audio_engine *audio)
{
    if (audio->active_count < audio->active_capacity)
    {
        return true;
    }

    int new_capacity = audio->active_capacity > 0 ? audio->active_capacity * 2 : 16;
    sdl3d_active_sound *active_sounds =
        SDL_realloc(audio->active_sounds, (size_t)new_capacity * sizeof(*active_sounds));
    if (active_sounds == NULL)
    {
        return SDL_OutOfMemory();
    }

    SDL_memset(active_sounds + audio->active_capacity, 0,
               (size_t)(new_capacity - audio->active_capacity) * sizeof(*active_sounds));
    audio->active_sounds = active_sounds;
    audio->active_capacity = new_capacity;
    return true;
}

static void cleanup_finished_sounds(sdl3d_audio_engine *audio)
{
    int write_index = 0;

    for (int i = 0; i < audio->active_count; ++i)
    {
        sdl3d_active_sound *active = &audio->active_sounds[i];
        if (!active->loaded || ma_sound_at_end(&active->sound))
        {
            active_sound_uninit(active);
            continue;
        }

        if (write_index != i)
        {
            audio->active_sounds[write_index] = *active;
            SDL_zero(*active);
        }
        ++write_index;
    }

    audio->active_count = write_index;
}

static bool start_sound_instance(sdl3d_active_sound *active, const sdl3d_audio_play_desc *desc)
{
    sdl3d_audio_play_desc play_desc = desc != NULL ? *desc : sdl3d_audio_play_desc_default();

    if (play_desc.volume < 0.0f)
    {
        play_desc.volume = 0.0f;
    }
    if (play_desc.pitch <= 0.0f)
    {
        play_desc.pitch = 1.0f;
    }
    if (play_desc.pan < -1.0f)
    {
        play_desc.pan = -1.0f;
    }
    if (play_desc.pan > 1.0f)
    {
        play_desc.pan = 1.0f;
    }

    ma_sound_set_volume(&active->sound, play_desc.volume);
    ma_sound_set_pitch(&active->sound, play_desc.pitch);
    ma_sound_set_pan(&active->sound, play_desc.pan);
    if (ma_sound_seek_to_pcm_frame(&active->sound, 0) != MA_SUCCESS)
    {
        return SDL_SetError("Could not seek sound to start.");
    }
    if (ma_sound_start(&active->sound) != MA_SUCCESS)
    {
        return SDL_SetError("Could not start sound.");
    }
    return true;
}

static sdl3d_music_slot *music_slot_create(sdl3d_audio_engine *audio, const char *path, bool loop, float volume,
                                           int ambient_id)
{
    sdl3d_music_slot *slot;
    ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;

    if (loop)
    {
        flags |= MA_SOUND_FLAG_LOOPING;
    }

    slot = SDL_calloc(1, sizeof(*slot));
    if (slot == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    slot->path = SDL_strdup(path);
    if (slot->path == NULL)
    {
        SDL_free(slot);
        SDL_OutOfMemory();
        return NULL;
    }

    if (ma_sound_init_from_file(&audio->engine, path, flags, NULL, NULL, &slot->sound) != MA_SUCCESS)
    {
        SDL_SetError("Could not load music file: %s", path);
        music_slot_destroy(slot);
        return NULL;
    }

    slot->loaded = true;
    slot->ambient_id = ambient_id;
    slot->target_volume = clamp_non_negative(volume);
    slot->loop = loop;
    return slot;
}

sdl3d_audio_play_desc sdl3d_audio_play_desc_default(void)
{
    sdl3d_audio_play_desc desc;
    desc.volume = 1.0f;
    desc.pitch = 1.0f;
    desc.pan = 0.0f;
    return desc;
}

bool sdl3d_audio_create(sdl3d_audio_engine **out_audio)
{
    sdl3d_audio_engine *audio;

    if (out_audio == NULL)
    {
        return SDL_InvalidParamError("out_audio");
    }
    *out_audio = NULL;

    audio = SDL_calloc(1, sizeof(*audio));
    if (audio == NULL)
    {
        return SDL_OutOfMemory();
    }

    if (ma_engine_init(NULL, &audio->engine) != MA_SUCCESS)
    {
        SDL_free(audio);
        return SDL_SetError("Could not initialize audio engine.");
    }

    audio->loaded = true;
    audio->current_ambient_id = -1;
    *out_audio = audio;
    return true;
}

void sdl3d_audio_destroy(sdl3d_audio_engine *audio)
{
    if (audio == NULL)
    {
        return;
    }

    for (int i = 0; i < audio->active_count; ++i)
    {
        active_sound_uninit(&audio->active_sounds[i]);
    }
    SDL_free(audio->active_sounds);
    music_slot_destroy(audio->music_current);
    music_slot_destroy(audio->music_next);
    if (audio->loaded)
    {
        ma_engine_uninit(&audio->engine);
    }
    SDL_free(audio);
}

void sdl3d_audio_update(sdl3d_audio_engine *audio, float dt)
{
    if (audio == NULL)
    {
        return;
    }

    cleanup_finished_sounds(audio);

    if (!audio->music_fading || audio->music_fade_duration <= 0.0f)
    {
        return;
    }

    audio->music_fade_elapsed += dt > 0.0f ? dt : 0.0f;
    float t = audio->music_fade_elapsed / audio->music_fade_duration;
    if (t > 1.0f)
    {
        t = 1.0f;
    }

    if (audio->music_current != NULL)
    {
        ma_sound_set_volume(&audio->music_current->sound, audio->music_current_start_volume * (1.0f - t));
    }
    if (audio->music_next != NULL)
    {
        ma_sound_set_volume(&audio->music_next->sound, audio->music_next_target_volume * t);
    }

    if (t < 1.0f)
    {
        return;
    }

    music_slot_destroy(audio->music_current);
    audio->music_current = audio->music_next;
    audio->music_next = NULL;
    audio->music_fading = false;
    audio->music_fade_elapsed = 0.0f;
    audio->music_fade_duration = 0.0f;
    audio->current_ambient_id = audio->music_current != NULL ? audio->music_current->ambient_id : -1;
}

void sdl3d_audio_set_master_volume(sdl3d_audio_engine *audio, float volume)
{
    if (audio == NULL)
    {
        return;
    }

    ma_engine_set_volume(&audio->engine, clamp_non_negative(volume));
}

float sdl3d_audio_get_master_volume(const sdl3d_audio_engine *audio)
{
    if (audio == NULL)
    {
        return 0.0f;
    }

    return ma_engine_get_volume((ma_engine *)&audio->engine);
}

bool sdl3d_audio_load_clip(sdl3d_audio_engine *audio, const char *path, sdl3d_audio_clip **out_clip)
{
    sdl3d_audio_clip *clip;

    if (audio == NULL)
    {
        return SDL_InvalidParamError("audio");
    }
    if (path == NULL || path[0] == '\0')
    {
        return SDL_InvalidParamError("path");
    }
    if (out_clip == NULL)
    {
        return SDL_InvalidParamError("out_clip");
    }
    *out_clip = NULL;

    clip = SDL_calloc(1, sizeof(*clip));
    if (clip == NULL)
    {
        return SDL_OutOfMemory();
    }

    if (ma_sound_init_from_file(&audio->engine, path, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL,
                                NULL, &clip->sound) != MA_SUCCESS)
    {
        SDL_free(clip);
        return SDL_SetError("Could not load sound clip: %s", path);
    }

    clip->audio = audio;
    clip->loaded = true;
    *out_clip = clip;
    return true;
}

void sdl3d_audio_clip_destroy(sdl3d_audio_clip *clip)
{
    if (clip == NULL)
    {
        return;
    }

    if (clip->loaded)
    {
        ma_sound_uninit(&clip->sound);
    }
    SDL_free(clip);
}

bool sdl3d_audio_play_clip(sdl3d_audio_engine *audio, const sdl3d_audio_clip *clip, const sdl3d_audio_play_desc *desc)
{
    sdl3d_active_sound *active;

    if (audio == NULL)
    {
        return SDL_InvalidParamError("audio");
    }
    if (clip == NULL || !clip->loaded || clip->audio != audio)
    {
        return SDL_InvalidParamError("clip");
    }
    if (!ensure_active_capacity(audio))
    {
        return false;
    }

    active = &audio->active_sounds[audio->active_count];
    SDL_zero(*active);
    if (ma_sound_init_copy(&audio->engine, &clip->sound, MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, &active->sound) !=
        MA_SUCCESS)
    {
        return SDL_SetError("Could not create sound instance.");
    }

    active->loaded = true;
    if (!start_sound_instance(active, desc))
    {
        active_sound_uninit(active);
        return false;
    }

    ++audio->active_count;
    return true;
}

bool sdl3d_audio_play_sound_file(sdl3d_audio_engine *audio, const char *path, const sdl3d_audio_play_desc *desc)
{
    sdl3d_active_sound *active;

    if (audio == NULL)
    {
        return SDL_InvalidParamError("audio");
    }
    if (path == NULL || path[0] == '\0')
    {
        return SDL_InvalidParamError("path");
    }
    if (!ensure_active_capacity(audio))
    {
        return false;
    }

    active = &audio->active_sounds[audio->active_count];
    SDL_zero(*active);
    if (ma_sound_init_from_file(&audio->engine, path, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL,
                                NULL, &active->sound) != MA_SUCCESS)
    {
        return SDL_SetError("Could not load sound file: %s", path);
    }

    active->loaded = true;
    if (!start_sound_instance(active, desc))
    {
        active_sound_uninit(active);
        return false;
    }

    ++audio->active_count;
    return true;
}

bool sdl3d_audio_play_music(sdl3d_audio_engine *audio, const char *path, bool loop, float volume, float fade_seconds)
{
    sdl3d_music_slot *next = NULL;

    if (audio == NULL)
    {
        return SDL_InvalidParamError("audio");
    }

    if (path == NULL || path[0] == '\0')
    {
        sdl3d_audio_stop_music(audio, fade_seconds);
        return true;
    }

    next = music_slot_create(audio, path, loop, volume, -1);
    if (next == NULL)
    {
        return false;
    }

    music_slot_destroy(audio->music_next);
    audio->music_next = next;
    audio->music_next_target_volume = next->target_volume;

    if (fade_seconds <= 0.0f || audio->music_current == NULL)
    {
        music_slot_destroy(audio->music_current);
        audio->music_current = audio->music_next;
        audio->music_next = NULL;
        ma_sound_set_volume(&audio->music_current->sound, audio->music_current->target_volume);
        if (ma_sound_start(&audio->music_current->sound) != MA_SUCCESS)
        {
            music_slot_destroy(audio->music_current);
            audio->music_current = NULL;
            return SDL_SetError("Could not start music.");
        }
        audio->music_fading = false;
        audio->current_ambient_id = audio->music_current->ambient_id;
        return true;
    }

    ma_sound_set_volume(&audio->music_next->sound, 0.0f);
    if (ma_sound_start(&audio->music_next->sound) != MA_SUCCESS)
    {
        music_slot_destroy(audio->music_next);
        audio->music_next = NULL;
        return SDL_SetError("Could not start music.");
    }

    audio->music_current_start_volume = audio->music_current->target_volume;
    audio->music_fade_elapsed = 0.0f;
    audio->music_fade_duration = fade_seconds;
    audio->music_fading = true;
    return true;
}

void sdl3d_audio_stop_music(sdl3d_audio_engine *audio, float fade_seconds)
{
    if (audio == NULL)
    {
        return;
    }

    music_slot_destroy(audio->music_next);
    audio->music_next = NULL;

    if (audio->music_current == NULL)
    {
        audio->current_ambient_id = -1;
        return;
    }

    if (fade_seconds <= 0.0f)
    {
        music_slot_destroy(audio->music_current);
        audio->music_current = NULL;
        audio->music_fading = false;
        audio->current_ambient_id = -1;
        return;
    }

    audio->music_current_start_volume = audio->music_current->target_volume;
    audio->music_next_target_volume = 0.0f;
    audio->music_fade_elapsed = 0.0f;
    audio->music_fade_duration = fade_seconds;
    audio->music_fading = true;
}

bool sdl3d_audio_set_ambient(sdl3d_audio_engine *audio, const sdl3d_audio_ambient *ambients, int ambient_count,
                             int ambient_id, float fade_seconds)
{
    const sdl3d_audio_ambient *selected = NULL;

    if (audio == NULL)
    {
        return false;
    }
    if (audio->current_ambient_id == ambient_id && !audio->music_fading)
    {
        return true;
    }

    for (int i = 0; i < ambient_count; ++i)
    {
        if (ambients[i].ambient_id == ambient_id)
        {
            selected = &ambients[i];
            break;
        }
    }

    if (selected == NULL || selected->path == NULL || selected->path[0] == '\0')
    {
        sdl3d_audio_stop_music(audio, fade_seconds);
        if (audio->music_current != NULL)
        {
            audio->music_current->ambient_id = ambient_id;
        }
        audio->current_ambient_id = ambient_id;
        return true;
    }

    sdl3d_music_slot *next = music_slot_create(audio, selected->path, selected->loop, selected->volume, ambient_id);
    if (next == NULL)
    {
        return false;
    }

    music_slot_destroy(audio->music_next);
    audio->music_next = next;
    audio->music_next_target_volume = next->target_volume;
    if (fade_seconds <= 0.0f || audio->music_current == NULL)
    {
        music_slot_destroy(audio->music_current);
        audio->music_current = audio->music_next;
        audio->music_next = NULL;
        ma_sound_set_volume(&audio->music_current->sound, audio->music_current->target_volume);
        if (ma_sound_start(&audio->music_current->sound) != MA_SUCCESS)
        {
            music_slot_destroy(audio->music_current);
            audio->music_current = NULL;
            return SDL_SetError("Could not start ambient sound.");
        }
        audio->music_fading = false;
        audio->current_ambient_id = ambient_id;
        return true;
    }

    ma_sound_set_volume(&audio->music_next->sound, 0.0f);
    if (ma_sound_start(&audio->music_next->sound) != MA_SUCCESS)
    {
        music_slot_destroy(audio->music_next);
        audio->music_next = NULL;
        return SDL_SetError("Could not start ambient sound.");
    }

    audio->music_current_start_volume = audio->music_current->target_volume;
    audio->music_fade_elapsed = 0.0f;
    audio->music_fade_duration = fade_seconds;
    audio->music_fading = true;
    return true;
}

int sdl3d_audio_get_current_ambient_id(const sdl3d_audio_engine *audio)
{
    return audio != NULL ? audio->current_ambient_id : -1;
}

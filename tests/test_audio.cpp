#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/audio.h"
}

TEST(SDL3DAudio, DefaultPlayDescIsUsable)
{
    sdl3d_audio_play_desc desc = sdl3d_audio_play_desc_default();

    EXPECT_FLOAT_EQ(desc.volume, 1.0f);
    EXPECT_FLOAT_EQ(desc.pitch, 1.0f);
    EXPECT_FLOAT_EQ(desc.pan, 0.0f);
    EXPECT_EQ(desc.bus, SDL3D_AUDIO_BUS_SOUND_EFFECTS);
}

TEST(SDL3DAudio, NullSafeOperationsDoNotCrash)
{
    sdl3d_audio_update(nullptr, 1.0f / 60.0f);
    sdl3d_audio_set_master_volume(nullptr, 0.5f);
    sdl3d_audio_set_bus_volume(nullptr, SDL3D_AUDIO_BUS_MUSIC, 0.5f);
    EXPECT_FLOAT_EQ(sdl3d_audio_get_master_volume(nullptr), 0.0f);
    EXPECT_FLOAT_EQ(sdl3d_audio_get_bus_volume(nullptr, SDL3D_AUDIO_BUS_MUSIC), 0.0f);
    EXPECT_EQ(sdl3d_audio_get_current_ambient_id(nullptr), -1);
    sdl3d_audio_stop_music(nullptr, 0.25f);
    sdl3d_audio_fade_music(nullptr, 0.25f, 1.0f);
    sdl3d_audio_stop_bus(nullptr, SDL3D_AUDIO_BUS_SOUND_EFFECTS);
    sdl3d_audio_destroy(nullptr);
    sdl3d_audio_clip_destroy(nullptr);
}

TEST(SDL3DAudio, RejectsInvalidArguments)
{
    sdl3d_audio_engine *audio = nullptr;
    sdl3d_audio_clip *clip = nullptr;

    EXPECT_FALSE(sdl3d_audio_create(nullptr));
    EXPECT_FALSE(sdl3d_audio_load_clip(nullptr, "missing.wav", &clip));
    EXPECT_FALSE(sdl3d_audio_play_clip(nullptr, clip, nullptr));
    EXPECT_FALSE(sdl3d_audio_play_sound_file(nullptr, "missing.wav", nullptr));
    EXPECT_FALSE(sdl3d_audio_play_music(nullptr, "missing.wav", true, 1.0f, 0.0f));
    EXPECT_FALSE(sdl3d_audio_set_ambient(audio, nullptr, 0, 1, 0.0f));
}

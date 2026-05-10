#include <gtest/gtest.h>

extern "C"
{
#include "slayer3d/audio.h"
}

TEST(SLAYER3DAudio, DefaultPlayDescIsUsable)
{
    slayer3d_audio_play_desc desc = slayer3d_audio_play_desc_default();

    EXPECT_FLOAT_EQ(desc.volume, 1.0f);
    EXPECT_FLOAT_EQ(desc.pitch, 1.0f);
    EXPECT_FLOAT_EQ(desc.pan, 0.0f);
    EXPECT_EQ(desc.bus, SLAYER3D_AUDIO_BUS_SOUND_EFFECTS);
}

TEST(SLAYER3DAudio, NullSafeOperationsDoNotCrash)
{
    slayer3d_audio_update(nullptr, 1.0f / 60.0f);
    slayer3d_audio_set_master_volume(nullptr, 0.5f);
    slayer3d_audio_set_bus_volume(nullptr, SLAYER3D_AUDIO_BUS_MUSIC, 0.5f);
    EXPECT_FLOAT_EQ(slayer3d_audio_get_master_volume(nullptr), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_audio_get_bus_volume(nullptr, SLAYER3D_AUDIO_BUS_MUSIC), 0.0f);
    EXPECT_EQ(slayer3d_audio_get_current_ambient_id(nullptr), -1);
    slayer3d_audio_stop_music(nullptr, 0.25f);
    slayer3d_audio_fade_music(nullptr, 0.25f, 1.0f);
    slayer3d_audio_stop_bus(nullptr, SLAYER3D_AUDIO_BUS_SOUND_EFFECTS);
    slayer3d_audio_destroy(nullptr);
    slayer3d_audio_clip_destroy(nullptr);
}

TEST(SLAYER3DAudio, RejectsInvalidArguments)
{
    slayer3d_audio_engine *audio = nullptr;
    slayer3d_audio_clip *clip = nullptr;

    EXPECT_FALSE(slayer3d_audio_create(nullptr));
    EXPECT_FALSE(slayer3d_audio_load_clip(nullptr, "missing.wav", &clip));
    EXPECT_FALSE(slayer3d_audio_play_clip(nullptr, clip, nullptr));
    EXPECT_FALSE(slayer3d_audio_play_sound_file(nullptr, "missing.wav", nullptr));
    EXPECT_FALSE(slayer3d_audio_play_music(nullptr, "missing.wav", true, 1.0f, 0.0f));
    EXPECT_FALSE(slayer3d_audio_set_ambient(audio, nullptr, 0, 1, 0.0f));
}

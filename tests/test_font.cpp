#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>

#include "sdl3d/font.h"

TEST(SDL3DFont, ReloadingSameFontObjectAdvancesAtlasGeneration)
{
    sdl3d_font font{};

    ASSERT_TRUE(sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, SDL3D_BUILTIN_FONT_INTER, 24.0f, &font)) << SDL_GetError();
    const Uint32 first_generation = font.atlas_texture.generation;
    ASSERT_GT(first_generation, 0u);

    sdl3d_free_font(&font);
    const Uint32 freed_generation = font.atlas_texture.generation;
    EXPECT_GT(freed_generation, first_generation);
    EXPECT_EQ(nullptr, font.atlas_pixels);
    EXPECT_EQ(0, font.atlas_w);
    EXPECT_EQ(0, font.atlas_h);

    ASSERT_TRUE(sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, SDL3D_BUILTIN_FONT_INTER, 72.0f, &font)) << SDL_GetError();
    EXPECT_GT(font.atlas_texture.generation, freed_generation);
    EXPECT_EQ(2048, font.atlas_w);
    EXPECT_EQ(2048, font.atlas_h);

    sdl3d_free_font(&font);
}

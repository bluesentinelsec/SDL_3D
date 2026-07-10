#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>

#include "slayer3d/font.h"

TEST(SLAYER3DFont, EmbeddedInterLoadsWithoutMediaDirectory)
{
    slayer3d_font font{};

    ASSERT_TRUE(slayer3d_load_builtin_font(nullptr, SLAYER3D_BUILTIN_FONT_INTER, 24.0f, &font)) << SDL_GetError();
    EXPECT_GT(font.atlas_texture.generation, 0u);
    EXPECT_GT(font.atlas_w, 0);
    EXPECT_GT(font.atlas_h, 0);

    slayer3d_free_font(&font);
}

TEST(SLAYER3DFont, DiskBackedBuiltInFontRequiresMediaDirectory)
{
    slayer3d_font font{};

    EXPECT_FALSE(slayer3d_load_builtin_font(nullptr, SLAYER3D_BUILTIN_FONT_ROBOTO, 24.0f, &font));
    EXPECT_STREQ("Parameter 'media_dir' is invalid", SDL_GetError());
}

TEST(SLAYER3DFont, ReloadingSameFontObjectAdvancesAtlasGeneration)
{
    slayer3d_font font{};

    ASSERT_TRUE(slayer3d_load_builtin_font(SLAYER3D_MEDIA_DIR, SLAYER3D_BUILTIN_FONT_INTER, 24.0f, &font))
        << SDL_GetError();
    const Uint32 first_generation = font.atlas_texture.generation;
    ASSERT_GT(first_generation, 0u);

    slayer3d_free_font(&font);
    const Uint32 freed_generation = font.atlas_texture.generation;
    EXPECT_GT(freed_generation, first_generation);
    EXPECT_EQ(nullptr, font.atlas_pixels);
    EXPECT_EQ(0, font.atlas_w);
    EXPECT_EQ(0, font.atlas_h);

    ASSERT_TRUE(slayer3d_load_builtin_font(SLAYER3D_MEDIA_DIR, SLAYER3D_BUILTIN_FONT_INTER, 72.0f, &font))
        << SDL_GetError();
    EXPECT_GT(font.atlas_texture.generation, freed_generation);
    EXPECT_EQ(2048, font.atlas_w);
    EXPECT_EQ(2048, font.atlas_h);

    slayer3d_free_font(&font);
}

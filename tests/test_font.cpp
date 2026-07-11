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

TEST(SLAYER3DFont, DensityBakesLargerAtlasWithIdenticalMetrics)
{
    slayer3d_font base{};
    slayer3d_font dense{};

    ASSERT_TRUE(slayer3d_load_builtin_font(nullptr, SLAYER3D_BUILTIN_FONT_INTER, 24.0f, &base)) << SDL_GetError();
    ASSERT_TRUE(slayer3d_load_builtin_font_ex(nullptr, SLAYER3D_BUILTIN_FONT_INTER, 24.0f, 2.0f, &dense))
        << SDL_GetError();

    EXPECT_FLOAT_EQ(base.density, 1.0f);
    EXPECT_FLOAT_EQ(dense.density, 2.0f);
    EXPECT_FLOAT_EQ(base.size, dense.size);
    EXPECT_FLOAT_EQ(base.ascent, dense.ascent);
    EXPECT_FLOAT_EQ(base.descent, dense.descent);
    EXPECT_FLOAT_EQ(base.line_gap, dense.line_gap);

    // The atlas holds more texels, but every exposed metric stays in display
    // units so measurement and placement never depend on the density.
    EXPECT_GT(dense.atlas_w * dense.atlas_h, base.atlas_w * base.atlas_h);
    for (int i = 0; i < SLAYER3D_FONT_CHAR_COUNT; ++i)
    {
        EXPECT_NEAR(base.glyphs[i].xadvance, dense.glyphs[i].xadvance, 0.75f) << "glyph " << i;
        EXPECT_NEAR(base.glyphs[i].xoff, dense.glyphs[i].xoff, 0.75f) << "glyph " << i;
        EXPECT_NEAR(base.glyphs[i].yoff, dense.glyphs[i].yoff, 0.75f) << "glyph " << i;
    }

    float base_w = 0.0f, base_h = 0.0f;
    float dense_w = 0.0f, dense_h = 0.0f;
    slayer3d_measure_text(&base, "The quick brown fox jumps over 0123456789", &base_w, &base_h);
    slayer3d_measure_text(&dense, "The quick brown fox jumps over 0123456789", &dense_w, &dense_h);
    EXPECT_NEAR(base_w, dense_w, base_w * 0.02f);
    EXPECT_FLOAT_EQ(base_h, dense_h);

    slayer3d_free_font(&base);
    slayer3d_free_font(&dense);
}

TEST(SLAYER3DFont, DensityIsClampedToSupportedRange)
{
    slayer3d_font font{};

    ASSERT_TRUE(slayer3d_load_builtin_font_ex(nullptr, SLAYER3D_BUILTIN_FONT_INTER, 24.0f, 100.0f, &font))
        << SDL_GetError();
    EXPECT_FLOAT_EQ(font.density, SLAYER3D_FONT_DENSITY_MAX);
    slayer3d_free_font(&font);

    ASSERT_TRUE(slayer3d_load_builtin_font_ex(nullptr, SLAYER3D_BUILTIN_FONT_INTER, 24.0f, 0.01f, &font))
        << SDL_GetError();
    EXPECT_FLOAT_EQ(font.density, SLAYER3D_FONT_DENSITY_MIN);
    slayer3d_free_font(&font);
}

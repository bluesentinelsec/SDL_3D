#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>

#include "sdl3d/texture.h"

namespace
{

std::filesystem::path make_temp_dir(const char *leaf)
{
    std::filesystem::path root;
    char *pref_path = SDL_GetPrefPath("bluesentinelsec", "SDL3DTests");
    if (pref_path != nullptr)
    {
        root = pref_path;
        SDL_free(pref_path);
    }
    else
    {
        SDL_ClearError();
        root = std::filesystem::temp_directory_path();
    }

    const auto unique =
        std::to_string((long long)std::filesystem::file_time_type::clock::now().time_since_epoch().count());
    std::filesystem::path dir = root / (std::string(leaf) + "_" + unique);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST(SDL3DTexture, CreateFromImageCopiesPixelsAndDefaultsSampler)
{
    std::vector<Uint8> pixels = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    };

    sdl3d_image image{};
    image.pixels = pixels.data();
    image.width = 2;
    image.height = 2;

    sdl3d_texture2d texture{};
    ASSERT_TRUE(sdl3d_create_texture_from_image(&image, &texture)) << SDL_GetError();

    EXPECT_EQ(2, texture.width);
    EXPECT_EQ(2, texture.height);
    EXPECT_EQ(SDL3D_TEXTURE_FILTER_BILINEAR, texture.filter);
    EXPECT_EQ(SDL3D_TEXTURE_WRAP_CLAMP, texture.wrap_u);
    EXPECT_EQ(SDL3D_TEXTURE_WRAP_CLAMP, texture.wrap_v);
    ASSERT_NE(nullptr, texture.pixels);

    pixels[0] = 17;
    EXPECT_EQ(255, texture.pixels[0]);

    sdl3d_free_texture(&texture);
}

TEST(SDL3DTexture, LoadFromFileRoundTrip)
{
    constexpr int w = 2;
    constexpr int h = 2;
    const std::filesystem::path dir = make_temp_dir("texture_roundtrip");
    const std::filesystem::path png_path = dir / "roundtrip.png";

    std::vector<Uint8> pixels = {
        10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255,
    };

    sdl3d_image image{};
    image.pixels = pixels.data();
    image.width = w;
    image.height = h;

    ASSERT_TRUE(sdl3d_save_image_png(&image, png_path.string().c_str())) << SDL_GetError();

    sdl3d_texture2d texture{};
    ASSERT_TRUE(sdl3d_load_texture_from_file(png_path.string().c_str(), &texture)) << SDL_GetError();

    EXPECT_EQ(w, texture.width);
    EXPECT_EQ(h, texture.height);
    ASSERT_NE(nullptr, texture.pixels);

    for (size_t i = 0; i < pixels.size(); ++i)
    {
        EXPECT_EQ(pixels[i], texture.pixels[i]) << "byte " << i;
    }

    sdl3d_free_texture(&texture);
}

TEST(SDL3DTexture, SamplerStateMutatorsValidateEnums)
{
    std::vector<Uint8> pixels(4, 255);
    sdl3d_image image{};
    image.pixels = pixels.data();
    image.width = 1;
    image.height = 1;

    sdl3d_texture2d texture{};
    ASSERT_TRUE(sdl3d_create_texture_from_image(&image, &texture));

    EXPECT_TRUE(sdl3d_set_texture_filter(&texture, SDL3D_TEXTURE_FILTER_NEAREST));
    EXPECT_EQ(SDL3D_TEXTURE_FILTER_NEAREST, texture.filter);

    EXPECT_TRUE(sdl3d_set_texture_wrap(&texture, SDL3D_TEXTURE_WRAP_REPEAT, SDL3D_TEXTURE_WRAP_CLAMP));
    EXPECT_EQ(SDL3D_TEXTURE_WRAP_REPEAT, texture.wrap_u);
    EXPECT_EQ(SDL3D_TEXTURE_WRAP_CLAMP, texture.wrap_v);

    EXPECT_FALSE(sdl3d_set_texture_filter(&texture, (sdl3d_texture_filter)99));
    EXPECT_FALSE(sdl3d_set_texture_wrap(&texture, (sdl3d_texture_wrap)88, SDL3D_TEXTURE_WRAP_CLAMP));

    sdl3d_free_texture(&texture);
}

TEST(SDL3DTexture, FreeIsIdempotent)
{
    sdl3d_texture2d texture{};
    sdl3d_free_texture(&texture);
    sdl3d_free_texture(&texture);
}

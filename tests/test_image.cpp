#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <SDL3/SDL_error.h>

#include "sdl3d/image.h"

namespace
{

std::string make_temp_path(const char *leaf)
{
    std::filesystem::path tmp = std::filesystem::temp_directory_path() / leaf;
    return tmp.string();
}

} // namespace

TEST(SDL3DImage, SavePngAndReloadRoundTrip)
{
    constexpr int w = 4;
    constexpr int h = 3;
    std::vector<Uint8> pixels(w * h * 4);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int i = (y * w + x) * 4;
            pixels[i + 0] = static_cast<Uint8>(x * 60);
            pixels[i + 1] = static_cast<Uint8>(y * 80);
            pixels[i + 2] = static_cast<Uint8>((x + y) * 30);
            pixels[i + 3] = 255;
        }
    }

    sdl3d_image src{};
    src.pixels = pixels.data();
    src.width = w;
    src.height = h;

    const std::string path = make_temp_path("sdl3d_image_roundtrip.png");
    ASSERT_TRUE(sdl3d_save_image_png(&src, path.c_str())) << SDL_GetError();

    sdl3d_image loaded{};
    ASSERT_TRUE(sdl3d_load_image_from_file(path.c_str(), &loaded)) << SDL_GetError();
    EXPECT_EQ(loaded.width, w);
    EXPECT_EQ(loaded.height, h);
    ASSERT_NE(loaded.pixels, nullptr);

    for (size_t i = 0; i < pixels.size(); ++i)
    {
        EXPECT_EQ(loaded.pixels[i], pixels[i]) << "byte " << i;
    }

    sdl3d_free_image(&loaded);
    std::remove(path.c_str());
}

TEST(SDL3DImage, LoadMissingFileFails)
{
    sdl3d_image img{};
    EXPECT_FALSE(sdl3d_load_image_from_file("/nonexistent/path/does/not/exist.png", &img));
    EXPECT_EQ(img.pixels, nullptr);
}

TEST(SDL3DImage, LoadFromMemoryRejectsGarbage)
{
    const unsigned char garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22};
    sdl3d_image img{};
    EXPECT_FALSE(sdl3d_load_image_from_memory(garbage, sizeof(garbage), &img));
    EXPECT_EQ(img.pixels, nullptr);
}

TEST(SDL3DImage, FreeIsIdempotent)
{
    sdl3d_image img{};
    sdl3d_free_image(&img);
    sdl3d_free_image(&img);
}

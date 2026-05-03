#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C"
{
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include "sdl3d/asset.h"
#include "sdl3d/game.h"
#include "sdl3d/game_data.h"
#include "sdl3d/image.h"
#include "sdl3d/sprite_asset.h"
}

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

    const auto unique = std::to_string((unsigned long long)SDL_GetTicksNS());
    const std::filesystem::path dir = root / (std::string(leaf) + "_" + unique);
    std::filesystem::create_directories(dir);
    return dir;
}

bool write_text(const std::filesystem::path &path, const std::string &text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
    return out.good();
}

bool write_png(const std::filesystem::path &path, const sdl3d_image &image)
{
    std::filesystem::create_directories(path.parent_path());
    const std::string path_text = path.string();
    return sdl3d_save_image_png(&image, path_text.c_str());
}

std::vector<Uint8> make_rgba_sheet_pixels(int cell_width, int cell_height, int columns, int rows,
                                          const std::array<std::array<Uint8, 4>, 4> &cell_colors)
{
    std::vector<Uint8> pixels((size_t)cell_width * (size_t)cell_height * (size_t)columns * (size_t)rows * 4u, 0);
    const int image_width = cell_width * columns;
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const int cell_index = row * columns + column;
            const auto &color = cell_colors[(size_t)cell_index];
            for (int y = 0; y < cell_height; ++y)
            {
                for (int x = 0; x < cell_width; ++x)
                {
                    const size_t offset =
                        ((size_t)(row * cell_height + y) * (size_t)image_width + (size_t)(column * cell_width + x)) *
                        4u;
                    pixels[offset + 0] = color[0];
                    pixels[offset + 1] = color[1];
                    pixels[offset + 2] = color[2];
                    pixels[offset + 3] = color[3];
                }
            }
        }
    }
    return pixels;
}

void expect_texture_color(const sdl3d_texture2d *texture, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(texture->pixels, nullptr);
    EXPECT_EQ(texture->pixels[0], r);
    EXPECT_EQ(texture->pixels[1], g);
    EXPECT_EQ(texture->pixels[2], b);
    EXPECT_EQ(texture->pixels[3], a);
}

} // namespace

TEST(SpriteAsset, LoadsSheetAndSlicesFrames)
{
    const std::filesystem::path dir = make_temp_dir("sprite_sheet");
    const std::filesystem::path png_path = dir / "sheet.png";

    const int cell_width = 2;
    const int cell_height = 2;
    const int columns = 2;
    const int rows = 2;
    const std::array<std::array<Uint8, 4>, 4> colors = {
        {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}}};
    std::vector<Uint8> pixels = make_rgba_sheet_pixels(cell_width, cell_height, columns, rows, colors);

    sdl3d_image image{};
    image.pixels = pixels.data();
    image.width = cell_width * columns;
    image.height = cell_height * rows;
    ASSERT_TRUE(write_png(png_path, image)) << SDL_GetError();

    sdl3d_sprite_asset_source source{};
    source.kind = SDL3D_SPRITE_ASSET_SOURCE_SHEET;
    const std::string sheet_path_text = png_path.string();
    source.sheet_path = sheet_path_text.c_str();
    source.frame_width = cell_width;
    source.frame_height = cell_height;
    source.columns = columns;
    source.rows = rows;
    source.frame_count = 2;
    source.direction_count = 2;
    source.fps = 12.0f;
    source.loop = true;
    source.lighting = true;
    source.emissive = false;
    source.visual_ground_offset = 0.25f;

    sdl3d_sprite_asset_runtime runtime{};
    char error[256]{};
    ASSERT_TRUE(sdl3d_sprite_asset_load(nullptr, &source, &runtime, error, sizeof(error))) << error;

    ASSERT_EQ(runtime.base_texture_count, 2);
    ASSERT_EQ(runtime.animation_texture_count, 4);
    ASSERT_EQ(runtime.animation_frame_count, 2);
    ASSERT_NE(runtime.base_textures, nullptr);
    ASSERT_NE(runtime.animation_textures, nullptr);
    ASSERT_NE(runtime.animation_frames, nullptr);
    EXPECT_FLOAT_EQ(runtime.fps, 12.0f);
    EXPECT_TRUE(runtime.loop);
    EXPECT_TRUE(runtime.lighting);
    EXPECT_FALSE(runtime.emissive);
    EXPECT_FLOAT_EQ(runtime.visual_ground_offset, 0.25f);

    expect_texture_color(&runtime.base_textures[0], 255, 0, 0, 255);
    expect_texture_color(&runtime.base_textures[1], 0, 255, 0, 255);
    expect_texture_color(runtime.animation_frames[0].frames[0], 255, 0, 0, 255);
    expect_texture_color(runtime.animation_frames[0].frames[1], 0, 255, 0, 255);
    expect_texture_color(runtime.animation_frames[1].frames[0], 0, 0, 255, 255);
    expect_texture_color(runtime.animation_frames[1].frames[1], 255, 255, 0, 255);

    sdl3d_sprite_actor actor{};
    sdl3d_sprite_asset_apply_actor(&actor, &runtime);
    EXPECT_EQ(actor.texture, runtime.base_textures);
    EXPECT_EQ(actor.rotations, sdl3d_sprite_asset_base_rotations(&runtime));
    EXPECT_EQ(actor.animation_frames, sdl3d_sprite_asset_animation_frames(&runtime));
    EXPECT_EQ(actor.animation_frame_count, 2);
    EXPECT_FLOAT_EQ(actor.animation_fps, 12.0f);
    EXPECT_TRUE(actor.animation_loop);
    EXPECT_FLOAT_EQ(actor.visual_ground_offset, 0.25f);

    sdl3d_sprite_asset_free(&runtime);
}

TEST(SpriteAsset, LoadsExplicitFileListSources)
{
    const std::filesystem::path dir = make_temp_dir("sprite_files");

    const std::array<std::array<Uint8, 4>, 6> colors = {{{255, 0, 0, 255},
                                                         {0, 255, 0, 255},
                                                         {0, 0, 255, 255},
                                                         {255, 255, 0, 255},
                                                         {255, 0, 255, 255},
                                                         {0, 255, 255, 255}}};
    std::array<std::filesystem::path, 6> paths = {
        dir / "base_south.png",  dir / "base_east.png",    dir / "walk_0_south.png",
        dir / "walk_0_east.png", dir / "walk_1_south.png", dir / "walk_1_east.png",
    };
    std::array<std::string, 6> path_strings;

    for (size_t i = 0; i < paths.size(); ++i)
    {
        std::vector<Uint8> pixels = {colors[i][0], colors[i][1], colors[i][2], colors[i][3]};
        sdl3d_image image{};
        image.pixels = pixels.data();
        image.width = 1;
        image.height = 1;
        ASSERT_TRUE(write_png(paths[i], image)) << SDL_GetError();
        path_strings[i] = paths[i].string();
    }

    const char *base_paths[2] = {path_strings[0].c_str(), path_strings[1].c_str()};
    const char *frame_paths[4] = {path_strings[2].c_str(), path_strings[3].c_str(), path_strings[4].c_str(),
                                  path_strings[5].c_str()};

    sdl3d_sprite_asset_source source{};
    source.kind = SDL3D_SPRITE_ASSET_SOURCE_FILES;
    source.base_paths = base_paths;
    source.frame_paths = frame_paths;
    source.frame_count = 2;
    source.direction_count = 2;
    source.fps = 8.0f;
    source.loop = false;
    source.lighting = false;
    source.emissive = true;
    source.visual_ground_offset = 0.5f;

    sdl3d_sprite_asset_runtime runtime{};
    char error[256]{};
    ASSERT_TRUE(sdl3d_sprite_asset_load(nullptr, &source, &runtime, error, sizeof(error))) << error;

    ASSERT_EQ(runtime.base_texture_count, 2);
    ASSERT_EQ(runtime.animation_texture_count, 4);
    ASSERT_EQ(runtime.animation_frame_count, 2);
    expect_texture_color(&runtime.base_textures[0], 255, 0, 0, 255);
    expect_texture_color(&runtime.base_textures[1], 0, 255, 0, 255);
    expect_texture_color(runtime.animation_frames[0].frames[0], 0, 0, 255, 255);
    expect_texture_color(runtime.animation_frames[0].frames[1], 255, 255, 0, 255);
    expect_texture_color(runtime.animation_frames[1].frames[0], 255, 0, 255, 255);
    expect_texture_color(runtime.animation_frames[1].frames[1], 0, 255, 255, 255);

    sdl3d_sprite_asset_free(&runtime);
}

TEST(SpriteAsset, LoadsFromSpriteManifestFile)
{
    const std::filesystem::path dir = make_temp_dir("sprite_manifest");
    const std::filesystem::path manifest_path = dir / "robot.sprite.json";

    const std::array<std::array<Uint8, 4>, 6> colors = {{{15, 25, 35, 255},
                                                         {45, 55, 65, 255},
                                                         {75, 85, 95, 255},
                                                         {105, 115, 125, 255},
                                                         {135, 145, 155, 255},
                                                         {165, 175, 185, 255}}};
    std::array<std::filesystem::path, 6> paths = {
        dir / "sprites" / "base_south.png",  dir / "sprites" / "base_east.png",    dir / "sprites" / "walk_0_south.png",
        dir / "sprites" / "walk_0_east.png", dir / "sprites" / "walk_1_south.png", dir / "sprites" / "walk_1_east.png",
    };

    for (size_t i = 0; i < paths.size(); ++i)
    {
        std::vector<Uint8> pixels = {colors[i][0], colors[i][1], colors[i][2], colors[i][3]};
        sdl3d_image image{};
        image.pixels = pixels.data();
        image.width = 1;
        image.height = 1;
        ASSERT_TRUE(write_png(paths[i], image)) << SDL_GetError();
    }

    ASSERT_TRUE(write_text(manifest_path,
                           R"json({
  "kind": "files",
  "base_paths": [
    "sprites/base_south.png",
    "sprites/base_east.png"
  ],
  "frame_paths": [
    "sprites/walk_0_south.png",
    "sprites/walk_0_east.png",
    "sprites/walk_1_south.png",
    "sprites/walk_1_east.png"
  ],
  "frame_count": 2,
  "direction_count": 2,
  "fps": 9.0,
  "loop": false,
  "lighting": false,
  "emissive": true,
  "visual_ground_offset": 0.5
})json"))
        << SDL_GetError();

    sdl3d_sprite_asset_runtime runtime{};
    char error[256]{};
    ASSERT_TRUE(sdl3d_sprite_asset_load_file(manifest_path.string().c_str(), &runtime, error, sizeof(error))) << error;

    ASSERT_EQ(runtime.base_texture_count, 2);
    ASSERT_EQ(runtime.animation_frame_count, 2);
    expect_texture_color(&runtime.base_textures[0], 15, 25, 35, 255);
    expect_texture_color(&runtime.base_textures[1], 45, 55, 65, 255);
    expect_texture_color(runtime.animation_frames[0].frames[0], 75, 85, 95, 255);
    expect_texture_color(runtime.animation_frames[0].frames[1], 105, 115, 125, 255);
    expect_texture_color(runtime.animation_frames[1].frames[0], 135, 145, 155, 255);
    expect_texture_color(runtime.animation_frames[1].frames[1], 165, 175, 185, 255);
    EXPECT_FLOAT_EQ(runtime.fps, 9.0f);
    EXPECT_FALSE(runtime.loop);
    EXPECT_FALSE(runtime.lighting);
    EXPECT_TRUE(runtime.emissive);
    EXPECT_FLOAT_EQ(runtime.visual_ground_offset, 0.5f);

    sdl3d_sprite_asset_free(&runtime);
}

TEST(SpriteAsset, LoadsThroughGameDataSpriteBridge)
{
    const std::filesystem::path dir = make_temp_dir("sprite_bridge");
    const std::filesystem::path json_path = dir / "sprite.game.json";
    const std::filesystem::path image_path = dir / "sprites" / "robot" / "walk.png";

    const int cell_width = 2;
    const int cell_height = 2;
    const int columns = 2;
    const int rows = 2;
    const std::array<std::array<Uint8, 4>, 4> colors = {
        {{10, 20, 30, 255}, {40, 50, 60, 255}, {70, 80, 90, 255}, {100, 110, 120, 255}}};
    std::vector<Uint8> pixels = make_rgba_sheet_pixels(cell_width, cell_height, columns, rows, colors);

    sdl3d_image image{};
    image.pixels = pixels.data();
    image.width = cell_width * columns;
    image.height = cell_height * rows;
    ASSERT_TRUE(write_png(image_path, image)) << SDL_GetError();

    ASSERT_TRUE(write_text(json_path,
                           R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Sprite Bridge", "id": "test.sprite_bridge", "version": "0.1.0" },
  "world": { "name": "world.sprite_bridge", "kind": "fixed_screen" },
  "assets": {
    "sprites": [
      {
        "id": "sprite.robot.walk",
        "path": "asset://sprites/robot/walk.png",
        "frame_width": 2,
        "frame_height": 2,
        "columns": 2,
        "rows": 2,
        "frame_count": 2,
        "direction_count": 2,
        "fps": 8.0,
        "loop": true,
        "lighting": true,
        "emissive": false,
        "visual_ground_offset": 0.25
      }
    ]
  },
  "entities": []
})json"));

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char load_error[256]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        sdl3d_game_data_load_file(json_path.string().c_str(), session, &runtime, load_error, sizeof(load_error)))
        << load_error;

    sdl3d_sprite_asset_runtime sprite{};
    ASSERT_TRUE(
        sdl3d_game_data_load_sprite_asset(runtime, "sprite.robot.walk", &sprite, load_error, sizeof(load_error)))
        << load_error;

    ASSERT_EQ(sprite.base_texture_count, 2);
    ASSERT_EQ(sprite.animation_frame_count, 2);
    expect_texture_color(&sprite.base_textures[0], 10, 20, 30, 255);
    expect_texture_color(&sprite.base_textures[1], 40, 50, 60, 255);
    expect_texture_color(sprite.animation_frames[1].frames[0], 70, 80, 90, 255);
    expect_texture_color(sprite.animation_frames[1].frames[1], 100, 110, 120, 255);
    EXPECT_FLOAT_EQ(sprite.visual_ground_offset, 0.25f);

    sdl3d_sprite_asset_free(&sprite);
    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

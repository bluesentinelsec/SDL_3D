#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

extern "C"
{
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/asset.h"
}

namespace
{

void append_u16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
}

void append_u32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
}

void append_u64(std::vector<std::uint8_t> &bytes, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        bytes.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
}

std::vector<std::uint8_t> make_pack(const std::vector<std::pair<std::string, std::string>> &entries)
{
    std::uint64_t table_size = 0;
    for (const auto &entry : entries)
        table_size += 18u + static_cast<std::uint64_t>(entry.first.size());

    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), {'S', '3', 'D', 'P', 'A', 'K', '1', '\0'});
    append_u32(bytes, 1);
    append_u32(bytes, static_cast<std::uint32_t>(entries.size()));
    append_u64(bytes, 24);

    std::uint64_t data_offset = 24u + table_size;
    for (const auto &entry : entries)
    {
        append_u16(bytes, static_cast<std::uint16_t>(entry.first.size()));
        append_u64(bytes, data_offset);
        append_u64(bytes, static_cast<std::uint64_t>(entry.second.size()));
        bytes.insert(bytes.end(), entry.first.begin(), entry.first.end());
        data_offset += static_cast<std::uint64_t>(entry.second.size());
    }

    for (const auto &entry : entries)
        bytes.insert(bytes.end(), entry.second.begin(), entry.second.end());
    return bytes;
}

std::string buffer_string(const sdl3d_asset_buffer &buffer)
{
    return std::string(static_cast<const char *>(buffer.data), buffer.size);
}

} // namespace

TEST(AssetResolver, ReadsFromMountedDirectory)
{
    sdl3d_asset_resolver *resolver = sdl3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);

    char error[256]{};
    ASSERT_TRUE(sdl3d_asset_resolver_mount_directory(resolver, SDL3D_TEST_ASSETS_DIR, error, sizeof(error))) << error;
    EXPECT_TRUE(sdl3d_asset_resolver_exists(resolver, "asset://game_data/module_success.game.json"));
    EXPECT_FALSE(sdl3d_asset_resolver_exists(resolver, "asset://../CMakeLists.txt"));

    sdl3d_asset_buffer buffer{};
    ASSERT_TRUE(sdl3d_asset_resolver_read_file(resolver, "game_data/scripts/shared.lua", &buffer, error, sizeof(error)))
        << error;
    EXPECT_NE(buffer_string(buffer).find("function shared.speed"), std::string::npos);
    sdl3d_asset_buffer_free(&buffer);

    sdl3d_asset_resolver_destroy(resolver);
}

TEST(AssetResolver, ReadsFromMemoryPack)
{
    const std::vector<std::uint8_t> pack =
        make_pack({{"scripts/rules.lua", "return { value = 42 }\n"}, {"data/config.json", "{\"ok\":true}\n"}});
    sdl3d_asset_resolver *resolver = sdl3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);

    char error[256]{};
    ASSERT_TRUE(
        sdl3d_asset_resolver_mount_memory_pack(resolver, pack.data(), pack.size(), "unit-pack", error, sizeof(error)))
        << error;
    EXPECT_TRUE(sdl3d_asset_resolver_exists(resolver, "asset://scripts/rules.lua"));

    sdl3d_asset_buffer buffer{};
    ASSERT_TRUE(sdl3d_asset_resolver_read_file(resolver, "asset://data/config.json", &buffer, error, sizeof(error)))
        << error;
    EXPECT_EQ(buffer_string(buffer), "{\"ok\":true}\n");
    sdl3d_asset_buffer_free(&buffer);

    sdl3d_asset_resolver_destroy(resolver);
}

TEST(AssetResolver, LaterMountsOverrideEarlierMounts)
{
    const std::vector<std::uint8_t> base_pack = make_pack({{"data/value.txt", "base"}});
    const std::vector<std::uint8_t> patch_pack = make_pack({{"data/value.txt", "patch"}});
    sdl3d_asset_resolver *resolver = sdl3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);

    char error[256]{};
    ASSERT_TRUE(sdl3d_asset_resolver_mount_memory_pack(resolver, base_pack.data(), base_pack.size(), "base", error,
                                                       sizeof(error)))
        << error;
    ASSERT_TRUE(sdl3d_asset_resolver_mount_memory_pack(resolver, patch_pack.data(), patch_pack.size(), "patch", error,
                                                       sizeof(error)))
        << error;

    sdl3d_asset_buffer buffer{};
    ASSERT_TRUE(sdl3d_asset_resolver_read_file(resolver, "data/value.txt", &buffer, error, sizeof(error))) << error;
    EXPECT_EQ(buffer_string(buffer), "patch");
    sdl3d_asset_buffer_free(&buffer);

    sdl3d_asset_resolver_destroy(resolver);
}

TEST(AssetResolver, RejectsUnsafeAndUnknownSchemePaths)
{
    const std::vector<std::uint8_t> pack = make_pack({{"safe.txt", "ok"}});
    sdl3d_asset_resolver *resolver = sdl3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);

    char error[256]{};
    ASSERT_TRUE(
        sdl3d_asset_resolver_mount_memory_pack(resolver, pack.data(), pack.size(), "safe", error, sizeof(error)))
        << error;

    sdl3d_asset_buffer buffer{};
    EXPECT_FALSE(sdl3d_asset_resolver_read_file(resolver, "asset://../safe.txt", &buffer, error, sizeof(error)));
    EXPECT_FALSE(
        sdl3d_asset_resolver_read_file(resolver, "http://example.com/safe.txt", &buffer, error, sizeof(error)));
    EXPECT_FALSE(sdl3d_asset_resolver_read_file(resolver, "C:/safe.txt", &buffer, error, sizeof(error)));

    sdl3d_asset_resolver_destroy(resolver);
}

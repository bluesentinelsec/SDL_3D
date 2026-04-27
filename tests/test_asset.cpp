#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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

std::filesystem::path unique_test_dir(const char *name)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("sdl3d_asset_test_" + std::string(name));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void write_text(const std::filesystem::path &path, const char *text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
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

TEST(AssetPackWriter, WritesDeterministicPackReadableByResolver)
{
    const std::filesystem::path dir = unique_test_dir("round_trip");
    write_text(dir / "sources" / "a.txt", "alpha");
    write_text(dir / "sources" / "b.txt", "bravo");

    const std::filesystem::path pack_a = dir / "a.sdl3dpak";
    const std::filesystem::path pack_b = dir / "b.sdl3dpak";
    const std::string source_a = (dir / "sources" / "a.txt").string();
    const std::string source_b = (dir / "sources" / "b.txt").string();
    const sdl3d_asset_pack_source sources[] = {
        {"text/b.txt", source_b.c_str()},
        {"text/a.txt", source_a.c_str()},
    };

    char error[256]{};
    ASSERT_TRUE(sdl3d_asset_pack_write_file(pack_a.string().c_str(), sources, 2, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_asset_pack_write_file(pack_b.string().c_str(), sources, 2, error, sizeof(error))) << error;

    std::ifstream a(pack_a, std::ios::binary);
    std::ifstream b(pack_b, std::ios::binary);
    const std::string bytes_a{std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>()};
    const std::string bytes_b{std::istreambuf_iterator<char>(b), std::istreambuf_iterator<char>()};
    EXPECT_EQ(bytes_a, bytes_b);

    sdl3d_asset_resolver *resolver = sdl3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);
    ASSERT_TRUE(sdl3d_asset_resolver_mount_pack_file(resolver, pack_a.string().c_str(), error, sizeof(error))) << error;

    sdl3d_asset_buffer buffer{};
    ASSERT_TRUE(sdl3d_asset_resolver_read_file(resolver, "asset://text/a.txt", &buffer, error, sizeof(error))) << error;
    EXPECT_EQ(buffer_string(buffer), "alpha");
    sdl3d_asset_buffer_free(&buffer);

    sdl3d_asset_resolver_destroy(resolver);
    std::filesystem::remove_all(dir);
}

TEST(AssetPackWriter, RejectsDuplicateNormalizedPaths)
{
    const std::filesystem::path dir = unique_test_dir("duplicates");
    write_text(dir / "one.txt", "one");
    write_text(dir / "two.txt", "two");

    const std::string one = (dir / "one.txt").string();
    const std::string two = (dir / "two.txt").string();
    const sdl3d_asset_pack_source sources[] = {
        {"text/one.txt", one.c_str()},
        {"asset://text/./one.txt", two.c_str()},
    };

    char error[256]{};
    EXPECT_FALSE(
        sdl3d_asset_pack_write_file((dir / "bad.sdl3dpak").string().c_str(), sources, 2, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("duplicate"), std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST(AssetPackWriter, RejectsUnsafeAssetPaths)
{
    const std::filesystem::path dir = unique_test_dir("unsafe");
    write_text(dir / "source.txt", "source");

    const std::string source = (dir / "source.txt").string();
    const sdl3d_asset_pack_source sources[] = {
        {"../outside.txt", source.c_str()},
    };

    char error[256]{};
    EXPECT_FALSE(
        sdl3d_asset_pack_write_file((dir / "bad.sdl3dpak").string().c_str(), sources, 1, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("invalid"), std::string::npos);
    std::filesystem::remove_all(dir);
}

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

extern "C"
{
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/asset.h"
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

std::string buffer_string(const slayer3d_asset_buffer &buffer)
{
    return std::string(static_cast<const char *>(buffer.data), buffer.size);
}

struct EnumeratedAsset
{
    std::string directory;
    std::string name;
    slayer3d_asset_entry_type type;
};

slayer3d_asset_enumeration_result collect_asset_entry(void *userdata, const char *directory, const char *name,
                                                      slayer3d_asset_entry_type type)
{
    auto *entries = static_cast<std::vector<EnumeratedAsset> *>(userdata);
    entries->push_back({directory != nullptr ? directory : "", name != nullptr ? name : "", type});
    return SLAYER3D_ASSET_ENUM_CONTINUE;
}

const EnumeratedAsset *find_enumerated_asset(const std::vector<EnumeratedAsset> &entries, const char *name)
{
    for (const EnumeratedAsset &entry : entries)
    {
        if (entry.name == name)
            return &entry;
    }
    return nullptr;
}

std::filesystem::path unique_test_dir(const char *name)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path();
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const std::filesystem::path dir = root / ("slayer3d_asset_test_" + std::string(name) + "_" +
                                                  std::to_string(now) + "_" + std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directories(dir, error))
            return dir;
    }
    throw std::runtime_error("failed to create unique asset test directory");
}

void remove_test_dir(const std::filesystem::path &dir)
{
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

void write_text(const std::filesystem::path &path, const char *text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string read_binary_file(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(AssetResolver, ReadsFromMountedDirectory)
{
    slayer3d_asset_resolver *resolver = slayer3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);

    char error[256]{};
    ASSERT_TRUE(slayer3d_asset_resolver_mount_directory(resolver, SLAYER3D_TEST_ASSETS_DIR, error, sizeof(error)))
        << error;
    EXPECT_TRUE(slayer3d_asset_resolver_exists(resolver, "asset://game_data/module_success.game.json"));
    EXPECT_FALSE(slayer3d_asset_resolver_exists(resolver, "asset://../CMakeLists.txt"));

    slayer3d_asset_buffer buffer{};
    ASSERT_TRUE(
        slayer3d_asset_resolver_read_file(resolver, "game_data/scripts/shared.lua", &buffer, error, sizeof(error)))
        << error;
    EXPECT_NE(buffer_string(buffer).find("function shared.speed"), std::string::npos);
    slayer3d_asset_buffer_free(&buffer);

    slayer3d_asset_resolver_destroy(resolver);
}

TEST(AssetResolver, ReadsFromMemoryPack)
{
    const std::vector<std::uint8_t> pack =
        make_pack({{"scripts/rules.lua", "return { value = 42 }\n"}, {"data/config.json", "{\"ok\":true}\n"}});
    slayer3d_asset_resolver *resolver = slayer3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);

    char error[256]{};
    ASSERT_TRUE(slayer3d_asset_resolver_mount_memory_pack(resolver, pack.data(), pack.size(), "unit-pack", error,
                                                          sizeof(error)))
        << error;
    EXPECT_TRUE(slayer3d_asset_resolver_exists(resolver, "asset://scripts/rules.lua"));

    slayer3d_asset_buffer buffer{};
    ASSERT_TRUE(slayer3d_asset_resolver_read_file(resolver, "asset://data/config.json", &buffer, error, sizeof(error)))
        << error;
    EXPECT_EQ(buffer_string(buffer), "{\"ok\":true}\n");
    slayer3d_asset_buffer_free(&buffer);

    slayer3d_asset_resolver_destroy(resolver);
}

TEST(AssetResolver, EnumeratesMountedDirectory)
{
    const std::filesystem::path dir = unique_test_dir("enumerate_directory");
    write_text(dir / "textures" / "wall.png", "wall");
    write_text(dir / "textures" / "metal" / "floor.png", "floor");

    slayer3d_asset_resolver *resolver = slayer3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);

    char error[256]{};
    ASSERT_TRUE(slayer3d_asset_resolver_mount_directory(resolver, dir.string().c_str(), error, sizeof(error))) << error;

    std::vector<EnumeratedAsset> entries;
    ASSERT_TRUE(slayer3d_asset_resolver_enumerate(resolver, "asset://textures", collect_asset_entry, &entries, error,
                                                  sizeof(error)))
        << error;
    ASSERT_EQ(entries.size(), 2u);
    const EnumeratedAsset *wall = find_enumerated_asset(entries, "wall.png");
    const EnumeratedAsset *metal = find_enumerated_asset(entries, "metal");
    ASSERT_NE(wall, nullptr);
    ASSERT_NE(metal, nullptr);
    EXPECT_EQ(wall->directory, "asset://textures");
    EXPECT_EQ(wall->type, SLAYER3D_ASSET_ENTRY_FILE);
    EXPECT_EQ(metal->type, SLAYER3D_ASSET_ENTRY_DIRECTORY);

    slayer3d_asset_resolver_destroy(resolver);
    remove_test_dir(dir);
}

TEST(AssetResolver, EnumeratesMemoryPackVirtualDirectories)
{
    const std::vector<std::uint8_t> pack =
        make_pack({{"textures/wall.png", "wall"}, {"textures/metal/floor.png", "floor"}, {"scripts/main.lua", ""}});
    slayer3d_asset_resolver *resolver = slayer3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);

    char error[256]{};
    ASSERT_TRUE(slayer3d_asset_resolver_mount_memory_pack(resolver, pack.data(), pack.size(), "unit-pack", error,
                                                          sizeof(error)))
        << error;

    std::vector<EnumeratedAsset> entries;
    ASSERT_TRUE(slayer3d_asset_resolver_enumerate(resolver, "asset://textures", collect_asset_entry, &entries, error,
                                                  sizeof(error)))
        << error;
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].directory, "asset://textures");
    EXPECT_EQ(entries[0].name, "wall.png");
    EXPECT_EQ(entries[0].type, SLAYER3D_ASSET_ENTRY_FILE);
    EXPECT_EQ(entries[1].name, "metal");
    EXPECT_EQ(entries[1].type, SLAYER3D_ASSET_ENTRY_DIRECTORY);

    entries.clear();
    ASSERT_TRUE(slayer3d_asset_resolver_enumerate(resolver, "asset://textures/metal", collect_asset_entry, &entries,
                                                  error, sizeof(error)))
        << error;
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].directory, "asset://textures/metal");
    EXPECT_EQ(entries[0].name, "floor.png");
    EXPECT_EQ(entries[0].type, SLAYER3D_ASSET_ENTRY_FILE);

    slayer3d_asset_resolver_destroy(resolver);
}

TEST(AssetResolver, LaterMountsOverrideEarlierMounts)
{
    const std::vector<std::uint8_t> base_pack = make_pack({{"data/value.txt", "base"}});
    const std::vector<std::uint8_t> patch_pack = make_pack({{"data/value.txt", "patch"}});
    slayer3d_asset_resolver *resolver = slayer3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);

    char error[256]{};
    ASSERT_TRUE(slayer3d_asset_resolver_mount_memory_pack(resolver, base_pack.data(), base_pack.size(), "base", error,
                                                          sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_asset_resolver_mount_memory_pack(resolver, patch_pack.data(), patch_pack.size(), "patch",
                                                          error, sizeof(error)))
        << error;

    slayer3d_asset_buffer buffer{};
    ASSERT_TRUE(slayer3d_asset_resolver_read_file(resolver, "data/value.txt", &buffer, error, sizeof(error))) << error;
    EXPECT_EQ(buffer_string(buffer), "patch");
    slayer3d_asset_buffer_free(&buffer);

    slayer3d_asset_resolver_destroy(resolver);
}

TEST(AssetResolver, RejectsUnsafeAndUnknownSchemePaths)
{
    const std::vector<std::uint8_t> pack = make_pack({{"safe.txt", "ok"}});
    slayer3d_asset_resolver *resolver = slayer3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);

    char error[256]{};
    ASSERT_TRUE(
        slayer3d_asset_resolver_mount_memory_pack(resolver, pack.data(), pack.size(), "safe", error, sizeof(error)))
        << error;

    slayer3d_asset_buffer buffer{};
    EXPECT_FALSE(slayer3d_asset_resolver_read_file(resolver, "asset://../safe.txt", &buffer, error, sizeof(error)));
    EXPECT_FALSE(
        slayer3d_asset_resolver_read_file(resolver, "http://example.com/safe.txt", &buffer, error, sizeof(error)));
    EXPECT_FALSE(slayer3d_asset_resolver_read_file(resolver, "C:/safe.txt", &buffer, error, sizeof(error)));

    slayer3d_asset_resolver_destroy(resolver);
}

TEST(AssetPackWriter, WritesDeterministicPackReadableByResolver)
{
    const std::filesystem::path dir = unique_test_dir("round_trip");
    write_text(dir / "sources" / "a.txt", "alpha");
    write_text(dir / "sources" / "b.txt", "bravo");

    const std::filesystem::path pack_a = dir / "a.slayer3dpak";
    const std::filesystem::path pack_b = dir / "b.slayer3dpak";
    const std::string source_a = (dir / "sources" / "a.txt").string();
    const std::string source_b = (dir / "sources" / "b.txt").string();
    const slayer3d_asset_pack_source sources[] = {
        {"text/b.txt", source_b.c_str()},
        {"text/a.txt", source_a.c_str()},
    };

    char error[256]{};
    ASSERT_TRUE(slayer3d_asset_pack_write_file(pack_a.string().c_str(), sources, 2, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_asset_pack_write_file(pack_b.string().c_str(), sources, 2, error, sizeof(error))) << error;

    const std::string bytes_a = read_binary_file(pack_a);
    const std::string bytes_b = read_binary_file(pack_b);
    ASSERT_FALSE(bytes_a.empty());
    ASSERT_FALSE(bytes_b.empty());
    EXPECT_EQ(bytes_a, bytes_b);
    ASSERT_GE(bytes_a.size(), 8u);
    const std::string magic(bytes_a.data(), bytes_a.data() + 7);
    EXPECT_TRUE(magic == "S3DPAK1" || magic == "S3DCPK1" || magic == "S3DOPK1");
    EXPECT_EQ(bytes_a[7], '\0');

    slayer3d_asset_resolver *resolver = slayer3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);
    ASSERT_TRUE(slayer3d_asset_resolver_mount_pack_file(resolver, pack_a.string().c_str(), error, sizeof(error)))
        << error;

    slayer3d_asset_buffer buffer{};
    ASSERT_TRUE(slayer3d_asset_resolver_read_file(resolver, "asset://text/a.txt", &buffer, error, sizeof(error)))
        << error;
    EXPECT_EQ(buffer_string(buffer), "alpha");
    slayer3d_asset_buffer_free(&buffer);

    slayer3d_asset_resolver_destroy(resolver);

    resolver = slayer3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);
    ASSERT_TRUE(slayer3d_asset_resolver_mount_memory_pack(resolver, bytes_a.data(), bytes_a.size(), "embedded", error,
                                                          sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_asset_resolver_read_file(resolver, "asset://text/b.txt", &buffer, error, sizeof(error)))
        << error;
    EXPECT_EQ(buffer_string(buffer), "bravo");
    slayer3d_asset_buffer_free(&buffer);

    slayer3d_asset_resolver_destroy(resolver);
    remove_test_dir(dir);
}

TEST(AssetPackWriter, RejectsDuplicateNormalizedPaths)
{
    const std::filesystem::path dir = unique_test_dir("duplicates");
    write_text(dir / "one.txt", "one");
    write_text(dir / "two.txt", "two");

    const std::string one = (dir / "one.txt").string();
    const std::string two = (dir / "two.txt").string();
    const slayer3d_asset_pack_source sources[] = {
        {"text/one.txt", one.c_str()},
        {"asset://text/./one.txt", two.c_str()},
    };

    char error[256]{};
    EXPECT_FALSE(
        slayer3d_asset_pack_write_file((dir / "bad.slayer3dpak").string().c_str(), sources, 2, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("duplicate"), std::string::npos);
    remove_test_dir(dir);
}

TEST(AssetPackWriter, RejectsUnsafeAssetPaths)
{
    const std::filesystem::path dir = unique_test_dir("unsafe");
    write_text(dir / "source.txt", "source");

    const std::string source = (dir / "source.txt").string();
    const slayer3d_asset_pack_source sources[] = {
        {"../outside.txt", source.c_str()},
    };

    char error[256]{};
    EXPECT_FALSE(
        slayer3d_asset_pack_write_file((dir / "bad.slayer3dpak").string().c_str(), sources, 1, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("invalid"), std::string::npos);
    remove_test_dir(dir);
}

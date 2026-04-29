#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

extern "C"
{
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/storage.h"
}

namespace
{

std::filesystem::path unique_test_dir(const char *name)
{
    std::filesystem::path root;
    char *pref = SDL_GetPrefPath("SDL3DTests", "Storage");
    if (pref != nullptr)
    {
        root = pref;
        SDL_free(pref);
    }
    else
    {
        root = std::filesystem::temp_directory_path();
    }

    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const std::filesystem::path dir = root / ("sdl3d_storage_test_" + std::string(name) + "_" +
                                                  std::to_string(now) + "_" + std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directories(dir, error))
            return dir;
    }
    throw std::runtime_error("failed to create unique storage test directory");
}

void remove_test_dir(const std::filesystem::path &dir)
{
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

std::string normalize_slashes(std::string path)
{
    for (char &ch : path)
    {
        if (ch == '\\')
            ch = '/';
    }
    return path;
}

sdl3d_storage_config test_config()
{
    sdl3d_storage_config config{};
    sdl3d_storage_config_init(&config);
    config.organization = "Blue Sentinel";
    config.application = "Pong";
    return config;
}

} // namespace

TEST(StoragePathPolicy, BuildsOrgAppRootsForDesktopAndWebPlatforms)
{
    const sdl3d_storage_config config = test_config();
    char path[256]{};

    ASSERT_TRUE(sdl3d_storage_build_root_path(&config, SDL3D_STORAGE_PLATFORM_WINDOWS, SDL3D_STORAGE_ROOT_USER,
                                              "C:/Users/Example/AppData/Roaming", path, sizeof(path)));
    EXPECT_STREQ(path, "C:/Users/Example/AppData/Roaming/Blue Sentinel/Pong");

    ASSERT_TRUE(sdl3d_storage_build_root_path(&config, SDL3D_STORAGE_PLATFORM_APPLE, SDL3D_STORAGE_ROOT_USER,
                                              "/Users/example/Library/Application Support", path, sizeof(path)));
    EXPECT_STREQ(path, "/Users/example/Library/Application Support/Blue Sentinel/Pong");

    ASSERT_TRUE(sdl3d_storage_build_root_path(&config, SDL3D_STORAGE_PLATFORM_UNIX, SDL3D_STORAGE_ROOT_USER,
                                              "/home/example/.local/share", path, sizeof(path)));
    EXPECT_STREQ(path, "/home/example/.local/share/Blue Sentinel/Pong");

    ASSERT_TRUE(sdl3d_storage_build_root_path(&config, SDL3D_STORAGE_PLATFORM_ANDROID, SDL3D_STORAGE_ROOT_USER,
                                              "/data/user/0/com.example.pong/files", path, sizeof(path)));
    EXPECT_STREQ(path, "/data/user/0/com.example.pong/files/Blue Sentinel/Pong");

    ASSERT_TRUE(sdl3d_storage_build_root_path(&config, SDL3D_STORAGE_PLATFORM_EMSCRIPTEN, SDL3D_STORAGE_ROOT_USER,
                                              "/persistent", path, sizeof(path)));
    EXPECT_STREQ(path, "/persistent/Blue Sentinel/Pong");
}

TEST(StoragePathPolicy, BuildsProfiledCacheRoots)
{
    sdl3d_storage_config config = test_config();
    config.profile = "player1";
    char path[256]{};

    ASSERT_TRUE(sdl3d_storage_build_root_path(&config, SDL3D_STORAGE_PLATFORM_UNIX, SDL3D_STORAGE_ROOT_CACHE,
                                              "/home/example/.local/share", path, sizeof(path)));
    EXPECT_STREQ(path, "/home/example/.local/share/Blue Sentinel/Pong/profiles/player1/cache");
}

TEST(StoragePathPolicy, RejectsUnsafeMetadataAndSmallBuffers)
{
    sdl3d_storage_config config = test_config();
    char path[16]{};

    EXPECT_FALSE(sdl3d_storage_build_root_path(&config, SDL3D_STORAGE_PLATFORM_UNIX, SDL3D_STORAGE_ROOT_USER,
                                               "/home/example/.local/share", path, sizeof(path)));

    config.organization = "Blue/Sentinel";
    char large_path[256]{};
    EXPECT_FALSE(sdl3d_storage_build_root_path(&config, SDL3D_STORAGE_PLATFORM_UNIX, SDL3D_STORAGE_ROOT_USER, "/tmp",
                                               large_path, sizeof(large_path)));
}

TEST(StorageRuntime, CreatesOverrideRootsAndResolvesVirtualPaths)
{
    const std::filesystem::path root = unique_test_dir("resolve");
    const std::filesystem::path user_root = root / "user";
    const std::filesystem::path cache_root = root / "cache";

    sdl3d_storage_config config = test_config();
    const std::string user_root_string = user_root.string();
    const std::string cache_root_string = cache_root.string();
    config.user_root_override = user_root_string.c_str();
    config.cache_root_override = cache_root_string.c_str();

    sdl3d_storage *storage = nullptr;
    char error[256]{};
    ASSERT_TRUE(sdl3d_storage_create(&config, &storage, error, sizeof(error))) << error;
    ASSERT_NE(storage, nullptr);

    EXPECT_EQ(normalize_slashes(sdl3d_storage_get_root(storage, SDL3D_STORAGE_ROOT_USER)),
              normalize_slashes(user_root_string));
    EXPECT_EQ(normalize_slashes(sdl3d_storage_get_root(storage, SDL3D_STORAGE_ROOT_CACHE)),
              normalize_slashes(cache_root_string));

    char resolved[512]{};
    ASSERT_TRUE(sdl3d_storage_resolve_path(storage, "user://settings/options.json", resolved, sizeof(resolved)));
    EXPECT_EQ(normalize_slashes(resolved), normalize_slashes((user_root / "settings/options.json").string()));
    ASSERT_TRUE(sdl3d_storage_resolve_path(storage, "cache://audio/title.ogg", resolved, sizeof(resolved)));
    EXPECT_EQ(normalize_slashes(resolved), normalize_slashes((cache_root / "audio/title.ogg").string()));

    EXPECT_FALSE(sdl3d_storage_resolve_path(storage, "asset://settings/options.json", resolved, sizeof(resolved)));
    EXPECT_FALSE(sdl3d_storage_resolve_path(storage, "user://../escape.txt", resolved, sizeof(resolved)));
    EXPECT_FALSE(sdl3d_storage_resolve_path(storage, "user://settings//options.json", resolved, sizeof(resolved)));
    EXPECT_FALSE(sdl3d_storage_resolve_path(storage, "user://settings/", resolved, sizeof(resolved)));
    EXPECT_FALSE(sdl3d_storage_resolve_path(storage, "user://settings\\options.json", resolved, sizeof(resolved)));
    EXPECT_FALSE(sdl3d_storage_resolve_path(storage, "/absolute/path", resolved, sizeof(resolved)));

    sdl3d_storage_destroy(storage);
    remove_test_dir(root);
}

TEST(StorageRuntime, WritesReadsReplacesAndDeletesFiles)
{
    const std::filesystem::path root = unique_test_dir("rw");
    const std::filesystem::path user_root = root / "user";

    sdl3d_storage_config config = test_config();
    const std::string user_root_string = user_root.string();
    config.user_root_override = user_root_string.c_str();

    sdl3d_storage *storage = nullptr;
    char error[256]{};
    ASSERT_TRUE(sdl3d_storage_create(&config, &storage, error, sizeof(error))) << error;

    const char first[] = "first";
    ASSERT_TRUE(sdl3d_storage_write_file(storage, "user://scores/high_scores.json", first, sizeof(first) - 1u, error,
                                         sizeof(error)))
        << error;
    EXPECT_TRUE(sdl3d_storage_exists(storage, "user://scores/high_scores.json"));

    sdl3d_storage_buffer buffer{};
    ASSERT_TRUE(sdl3d_storage_read_file(storage, "user://scores/high_scores.json", &buffer, error, sizeof(error)))
        << error;
    EXPECT_EQ(std::string(static_cast<const char *>(buffer.data), buffer.size), "first");
    sdl3d_storage_buffer_free(&buffer);

    const char second[] = "second";
    ASSERT_TRUE(sdl3d_storage_write_file(storage, "user://scores/high_scores.json", second, sizeof(second) - 1u, error,
                                         sizeof(error)))
        << error;
    ASSERT_TRUE(sdl3d_storage_read_file(storage, "user://scores/high_scores.json", &buffer, error, sizeof(error)))
        << error;
    EXPECT_EQ(std::string(static_cast<const char *>(buffer.data), buffer.size), "second");
    sdl3d_storage_buffer_free(&buffer);

    ASSERT_TRUE(sdl3d_storage_create_directory(storage, "cache://audio/materialized", error, sizeof(error))) << error;
    EXPECT_TRUE(sdl3d_storage_exists(storage, "cache://audio/materialized"));

    ASSERT_TRUE(sdl3d_storage_delete(storage, "user://scores/high_scores.json", error, sizeof(error))) << error;
    EXPECT_FALSE(sdl3d_storage_exists(storage, "user://scores/high_scores.json"));

    sdl3d_storage_destroy(storage);
    remove_test_dir(root);
}

TEST(StorageRuntime, RejectsInvalidWrites)
{
    const std::filesystem::path root = unique_test_dir("reject");
    const std::filesystem::path user_root = root / "user";

    sdl3d_storage_config config = test_config();
    const std::string user_root_string = user_root.string();
    config.user_root_override = user_root_string.c_str();

    sdl3d_storage *storage = nullptr;
    char error[256]{};
    ASSERT_TRUE(sdl3d_storage_create(&config, &storage, error, sizeof(error))) << error;

    const char data[] = "nope";
    EXPECT_FALSE(sdl3d_storage_write_file(storage, "user://../escape.txt", data, sizeof(data), error, sizeof(error)));
    EXPECT_FALSE(sdl3d_storage_write_file(storage, "cache://bad\\name.txt", data, sizeof(data), error, sizeof(error)));
    EXPECT_FALSE(sdl3d_storage_write_file(storage, "asset://readonly.txt", data, sizeof(data), error, sizeof(error)));

    sdl3d_storage_destroy(storage);
    remove_test_dir(root);
}

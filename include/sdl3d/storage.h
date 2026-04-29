/**
 * @file storage.h
 * @brief Safe persistent and cache storage roots for game-authored data.
 *
 * The storage API is the writable companion to the read-only asset resolver.
 * Games address writable files through stable virtual paths such as
 * user://scores/high_scores.json and cache://audio/materialized.wav. SDL3D
 * resolves those paths to platform-appropriate locations derived from stable
 * organization and application metadata.
 */

#ifndef SDL3D_STORAGE_H
#define SDL3D_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque persistent storage context. */
    typedef struct sdl3d_storage sdl3d_storage;

    /** @brief Writable storage roots understood by sdl3d_storage. */
    typedef enum sdl3d_storage_root
    {
        /** @brief User-owned persistent data such as saves, settings, and high scores. */
        SDL3D_STORAGE_ROOT_USER = 0,
        /** @brief Disposable generated data such as decoded or materialized caches. */
        SDL3D_STORAGE_ROOT_CACHE = 1,
        /** @brief Number of storage roots. */
        SDL3D_STORAGE_ROOT_COUNT = 2,
    } sdl3d_storage_root;

    /**
     * @brief Platform layout selector for deterministic root planning.
     *
     * SDL3D_STORAGE_PLATFORM_NATIVE uses SDL_GetPrefPath() at runtime. The
     * named platform values are pure path planners for tests, tools, and
     * diagnostics that need to verify SDL3D's path policy without running on
     * every target OS.
     */
    typedef enum sdl3d_storage_platform
    {
        /** @brief Use SDL_GetPrefPath() for the current host platform. */
        SDL3D_STORAGE_PLATFORM_NATIVE = 0,
        /** @brief Plan a Windows-style app-data layout. */
        SDL3D_STORAGE_PLATFORM_WINDOWS,
        /** @brief Plan an Apple Application Support style layout. */
        SDL3D_STORAGE_PLATFORM_APPLE,
        /** @brief Plan an XDG-style Unix/Linux layout. */
        SDL3D_STORAGE_PLATFORM_UNIX,
        /** @brief Plan an app-private Android-style layout. */
        SDL3D_STORAGE_PLATFORM_ANDROID,
        /** @brief Plan an Emscripten virtual filesystem style layout. */
        SDL3D_STORAGE_PLATFORM_EMSCRIPTEN,
    } sdl3d_storage_platform;

    /**
     * @brief Configuration used to create a writable storage context.
     *
     * @p organization and @p application are required stable identifiers. Use
     * the same organization for all games from a publisher and never change an
     * application's identifier after release unless migration is intentional.
     *
     * @p profile is optional and, when present, scopes both user and cache roots
     * below profiles/<profile>. This is useful for tests, save profiles, or
     * parallel game instances.
     *
     * Root overrides are intended for tests and tools. Normal games should leave
     * them NULL so SDL3D can choose the platform-idiomatic location.
     */
    typedef struct sdl3d_storage_config
    {
        /** @brief Stable publisher, studio, company, or domain-style organization. */
        const char *organization;
        /** @brief Stable application/game identifier. */
        const char *application;
        /** @brief Optional profile subdirectory. */
        const char *profile;
        /** @brief Optional exact filesystem root for user://. */
        const char *user_root_override;
        /** @brief Optional exact filesystem root for cache://. */
        const char *cache_root_override;
    } sdl3d_storage_config;

    /**
     * @brief Owned bytes returned by sdl3d_storage_read_file().
     *
     * The data may be binary and is not guaranteed to be null-terminated. Free
     * it with sdl3d_storage_buffer_free().
     */
    typedef struct sdl3d_storage_buffer
    {
        /** @brief Owned byte buffer, or NULL when empty or invalid. */
        void *data;
        /** @brief Number of valid bytes in @p data. */
        size_t size;
    } sdl3d_storage_buffer;

    /**
     * @brief Fill a storage config with documented defaults.
     *
     * The default organization is "SDL3D" and the default application is
     * "SDL3D". Games should override both through data or host configuration.
     */
    void sdl3d_storage_config_init(sdl3d_storage_config *config);

    /**
     * @brief Build an org/app-aware root path for a target platform policy.
     *
     * @p base_path is the platform-provided parent directory, such as
     * %APPDATA%, Application Support, XDG_DATA_HOME, or an app-private mobile
     * root. SDL3D appends organization and application on every named platform
     * for consistency; @p profile appends profiles/<profile>; cache roots append
     * cache.
     *
     * This function does not touch the filesystem.
     *
     * @return true when @p out_path received a valid null-terminated path.
     */
    bool sdl3d_storage_build_root_path(const sdl3d_storage_config *config, sdl3d_storage_platform platform,
                                       sdl3d_storage_root root, const char *base_path, char *out_path,
                                       int out_path_size);

    /**
     * @brief Create a storage context and ensure its root directories exist.
     *
     * If root overrides are not provided, SDL3D uses SDL_GetPrefPath() with the
     * configured organization and application, then creates a cache subdirectory
     * beneath that root.
     *
     * @return true when the storage context was created.
     */
    bool sdl3d_storage_create(const sdl3d_storage_config *config, sdl3d_storage **out_storage, char *error_buffer,
                              int error_buffer_size);

    /** @brief Destroy a storage context. Safe to call with NULL. */
    void sdl3d_storage_destroy(sdl3d_storage *storage);

    /**
     * @brief Get the resolved filesystem root for user:// or cache://.
     *
     * The returned pointer is owned by @p storage and remains valid until
     * sdl3d_storage_destroy().
     */
    const char *sdl3d_storage_get_root(const sdl3d_storage *storage, sdl3d_storage_root root);

    /**
     * @brief Resolve a virtual user:// or cache:// path to a filesystem path.
     *
     * Paths are strict: absolute paths, parent traversal, empty segments,
     * backslashes, and unknown URI schemes are rejected.
     */
    bool sdl3d_storage_resolve_path(const sdl3d_storage *storage, const char *virtual_path, char *out_path,
                                    int out_path_size);

    /**
     * @brief Create a directory under user:// or cache://, including parents.
     */
    bool sdl3d_storage_create_directory(sdl3d_storage *storage, const char *virtual_path, char *error_buffer,
                                        int error_buffer_size);

    /**
     * @brief Return whether a virtual storage path currently exists.
     */
    bool sdl3d_storage_exists(const sdl3d_storage *storage, const char *virtual_path);

    /**
     * @brief Read a file from user:// or cache:// into an owned buffer.
     */
    bool sdl3d_storage_read_file(const sdl3d_storage *storage, const char *virtual_path,
                                 sdl3d_storage_buffer *out_buffer, char *error_buffer, int error_buffer_size);

    /**
     * @brief Write a complete file under user:// or cache://.
     *
     * Parent directories are created automatically. The write uses a temporary
     * file in the target directory and renames it into place; this is atomic on
     * platforms that provide same-directory atomic rename and falls back to
     * replace-then-rename only when the platform refuses to replace an existing
     * destination.
     */
    bool sdl3d_storage_write_file(sdl3d_storage *storage, const char *virtual_path, const void *data, size_t size,
                                  char *error_buffer, int error_buffer_size);

    /**
     * @brief Delete a file or empty directory under user:// or cache://.
     */
    bool sdl3d_storage_delete(sdl3d_storage *storage, const char *virtual_path, char *error_buffer,
                              int error_buffer_size);

    /**
     * @brief Free bytes returned by sdl3d_storage_read_file().
     */
    void sdl3d_storage_buffer_free(sdl3d_storage_buffer *buffer);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_STORAGE_H */

/**
 * @file asset.h
 * @brief Virtual asset resolver for source-tree, packed, and embedded data.
 *
 * SDL3D game data should refer to stable virtual paths such as
 * asset://scripts/pong.lua instead of assuming a particular filesystem layout.
 * A resolver maps those paths to mounted directories, packed archives, or
 * memory-backed archives. This keeps development builds convenient while
 * preserving a clean path toward single-file or archive-based shipping builds.
 */

#ifndef SDL3D_ASSET_H
#define SDL3D_ASSET_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque virtual asset resolver. */
    typedef struct sdl3d_asset_resolver sdl3d_asset_resolver;

    /**
     * @brief Owned bytes returned by an asset read.
     *
     * The buffer is not guaranteed to be text or null-terminated. Free it with
     * sdl3d_asset_buffer_free() when finished.
     */
    typedef struct sdl3d_asset_buffer
    {
        /** @brief Owned byte buffer, or NULL when empty or invalid. */
        void *data;
        /** @brief Number of valid bytes in @p data. */
        size_t size;
    } sdl3d_asset_buffer;

    /**
     * @brief Source file entry used when writing an SDL3D asset pack.
     *
     * @p asset_path is the stable virtual path stored in the pack, such as
     * scripts/pong.lua. @p source_path is the filesystem path read at build
     * time. The writer copies both strings only for the duration of the call.
     */
    typedef struct sdl3d_asset_pack_source
    {
        /** @brief Stable virtual path stored in the pack. */
        const char *asset_path;
        /** @brief Filesystem path whose bytes are stored for @p asset_path. */
        const char *source_path;
    } sdl3d_asset_pack_source;

    /**
     * @brief Create an empty asset resolver.
     *
     * Mount directories and packs before attempting to read assets. Later mounts
     * take precedence over earlier mounts, leaving room for future patch or mod
     * overlays without changing authored paths.
     */
    sdl3d_asset_resolver *sdl3d_asset_resolver_create(void);

    /**
     * @brief Destroy an asset resolver and all mounted pack metadata.
     *
     * Safe to call with NULL. Buffers already returned by
     * sdl3d_asset_resolver_read_file() remain caller-owned and must still be
     * freed by the caller.
     */
    void sdl3d_asset_resolver_destroy(sdl3d_asset_resolver *resolver);

    /**
     * @brief Mount a filesystem directory as an asset root.
     *
     * An authored path like asset://scripts/pong.lua resolves to
     * @p root_directory/scripts/pong.lua. The root path is copied by the
     * resolver.
     *
     * @return true when the directory mount was recorded.
     */
    bool sdl3d_asset_resolver_mount_directory(sdl3d_asset_resolver *resolver, const char *root_directory,
                                              char *error_buffer, int error_buffer_size);

    /**
     * @brief Mount an SDL3D pack file from disk.
     *
     * The pack is loaded into memory and parsed immediately. The current pack
     * format is intentionally small and uncompressed; compression, encryption,
     * and patch layering can be added behind the same resolver API later.
     *
     * @return true when the pack was read and mounted.
     */
    bool sdl3d_asset_resolver_mount_pack_file(sdl3d_asset_resolver *resolver, const char *pack_path, char *error_buffer,
                                              int error_buffer_size);

    /**
     * @brief Mount an SDL3D pack already present in memory.
     *
     * The resolver copies @p data, so callers may release their source memory
     * after this call succeeds. This is the preferred entry point for
     * CMake-generated embedded packs.
     *
     * @return true when the pack was copied, parsed, and mounted.
     */
    bool sdl3d_asset_resolver_mount_memory_pack(sdl3d_asset_resolver *resolver, const void *data, size_t size,
                                                const char *debug_name, char *error_buffer, int error_buffer_size);

    /**
     * @brief Return whether an asset path resolves to any mounted source.
     *
     * This is intended for validation and diagnostics. Use
     * sdl3d_asset_resolver_read_file() when the bytes are needed.
     */
    bool sdl3d_asset_resolver_exists(const sdl3d_asset_resolver *resolver, const char *asset_path);

    /**
     * @brief Read an asset by virtual path.
     *
     * @p asset_path may use the asset:// URI scheme or a plain relative virtual
     * path. The resolver rejects absolute paths, parent-directory traversal, and
     * unknown URI schemes to keep authored content portable.
     *
     * @return true when the file was found and copied into @p out_buffer.
     */
    bool sdl3d_asset_resolver_read_file(const sdl3d_asset_resolver *resolver, const char *asset_path,
                                        sdl3d_asset_buffer *out_buffer, char *error_buffer, int error_buffer_size);

    /**
     * @brief Free bytes returned by sdl3d_asset_resolver_read_file().
     *
     * Safe to call with NULL or an already empty buffer.
     */
    void sdl3d_asset_buffer_free(sdl3d_asset_buffer *buffer);

    /**
     * @brief Write an SDL3D asset pack from explicit source files.
     *
     * Entries are normalized, sorted by asset path for deterministic output,
     * checked for duplicates, and written with explicit little-endian integers.
     * The format is currently uncompressed and unencrypted by design; future
     * compression, encryption, and patch metadata can be added while preserving
     * this API shape and the resolver abstraction.
     *
     * @param pack_path Filesystem output path to create or replace.
     * @param entries Source files to include.
     * @param entry_count Number of entries in @p entries.
     * @param error_buffer Optional human-readable error output.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the pack was written successfully.
     */
    bool sdl3d_asset_pack_write_file(const char *pack_path, const sdl3d_asset_pack_source *entries, int entry_count,
                                     char *error_buffer, int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_ASSET_H */

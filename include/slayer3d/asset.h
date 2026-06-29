/**
 * @file asset.h
 * @brief Virtual asset resolver for source-tree, packed, and embedded data.
 *
 * SLAYER3D game data should refer to stable virtual paths such as
 * asset://scripts/pong.lua instead of assuming a particular filesystem layout.
 * A resolver maps those paths to mounted directories, packed archives, or
 * memory-backed archives. This keeps development builds convenient while
 * preserving a clean path toward single-file or archive-based shipping builds.
 */

#ifndef SLAYER3D_ASSET_H
#define SLAYER3D_ASSET_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque virtual asset resolver. */
    typedef struct slayer3d_asset_resolver slayer3d_asset_resolver;

    /**
     * @brief Owned bytes returned by an asset read.
     *
     * The buffer is not guaranteed to be text or null-terminated. Free it with
     * slayer3d_asset_buffer_free() when finished.
     */
    typedef struct slayer3d_asset_buffer
    {
        /** @brief Owned byte buffer, or NULL when empty or invalid. */
        void *data;
        /** @brief Number of valid bytes in @p data. */
        size_t size;
    } slayer3d_asset_buffer;

    /**
     * @brief Source file entry used when writing an SLAYER3D asset pack.
     *
     * @p asset_path is the stable virtual path stored in the pack, such as
     * scripts/pong.lua. @p source_path is the filesystem path read at build
     * time. The writer copies both strings only for the duration of the call.
     */
    typedef struct slayer3d_asset_pack_source
    {
        /** @brief Stable virtual path stored in the pack. */
        const char *asset_path;
        /** @brief Filesystem path whose bytes are stored for @p asset_path. */
        const char *source_path;
    } slayer3d_asset_pack_source;

    /** @brief Virtual asset directory entry type. */
    typedef enum slayer3d_asset_entry_type
    {
        /** @brief A regular readable asset file. */
        SLAYER3D_ASSET_ENTRY_FILE,
        /** @brief A virtual or filesystem subdirectory. */
        SLAYER3D_ASSET_ENTRY_DIRECTORY
    } slayer3d_asset_entry_type;

    /** @brief Result returned by asset enumeration callbacks. */
    typedef enum slayer3d_asset_enumeration_result
    {
        /** @brief Continue enumerating remaining entries. */
        SLAYER3D_ASSET_ENUM_CONTINUE,
        /** @brief Stop enumeration successfully. */
        SLAYER3D_ASSET_ENUM_STOP,
        /** @brief Stop enumeration and report failure. */
        SLAYER3D_ASSET_ENUM_FAILURE
    } slayer3d_asset_enumeration_result;

    /**
     * @brief Callback invoked once for each immediate child of an asset directory.
     *
     * @p directory is the virtual directory being enumerated, such as
     * asset://textures. @p name is the child name only, not a full path.
     */
    typedef slayer3d_asset_enumeration_result (*slayer3d_asset_enumerate_fn)(void *userdata, const char *directory,
                                                                             const char *name,
                                                                             slayer3d_asset_entry_type type);

    /**
     * @brief Create an empty asset resolver.
     *
     * Mount directories and packs before attempting to read assets. Later mounts
     * take precedence over earlier mounts, leaving room for future patch or mod
     * overlays without changing authored paths.
     */
    slayer3d_asset_resolver *slayer3d_asset_resolver_create(void);

    /**
     * @brief Destroy an asset resolver and all mounted pack metadata.
     *
     * Safe to call with NULL. Buffers already returned by
     * slayer3d_asset_resolver_read_file() remain caller-owned and must still be
     * freed by the caller.
     */
    void slayer3d_asset_resolver_destroy(slayer3d_asset_resolver *resolver);

    /**
     * @brief Mount a filesystem directory as an asset root.
     *
     * An authored path like asset://scripts/pong.lua resolves to
     * @p root_directory/scripts/pong.lua. The root path is copied by the
     * resolver.
     *
     * @return true when the directory mount was recorded.
     */
    bool slayer3d_asset_resolver_mount_directory(slayer3d_asset_resolver *resolver, const char *root_directory,
                                                 char *error_buffer, int error_buffer_size);

    /**
     * @brief Mount an SLAYER3D pack file from disk.
     *
     * The pack is loaded into memory and parsed immediately. Packs written by
     * the current build pipeline are compressed at rest by default and
     * transparently decompressed on mount. If SLAYER3D_PACK_PASSWORD is set at
     * build time, the pack writer also emits a lightly obfuscated wrapper
     * around the compressed bytes and the resolver unwraps it before parsing.
     * The original raw pack format is still accepted for compatibility.
     *
     * @return true when the pack was read and mounted.
     */
    bool slayer3d_asset_resolver_mount_pack_file(slayer3d_asset_resolver *resolver, const char *pack_path,
                                                 char *error_buffer, int error_buffer_size);

    /**
     * @brief Mount an SLAYER3D pack already present in memory.
     *
     * The resolver copies @p data, so callers may release their source memory
     * after this call succeeds. Packs may be stored compressed or raw; the
     * resolver normalizes either form to the same in-memory pack layout before
     * parsing. If SLAYER3D_PACK_PASSWORD is set at build time, obfuscated packs
     * are also supported. This is the preferred entry point for CMake-generated
     * embedded packs.
     *
     * @return true when the pack was copied, parsed, and mounted.
     */
    bool slayer3d_asset_resolver_mount_memory_pack(slayer3d_asset_resolver *resolver, const void *data, size_t size,
                                                   const char *debug_name, char *error_buffer, int error_buffer_size);

    /**
     * @brief Return whether an asset path resolves to any mounted source.
     *
     * This is intended for validation and diagnostics. Use
     * slayer3d_asset_resolver_read_file() when the bytes are needed.
     */
    bool slayer3d_asset_resolver_exists(const slayer3d_asset_resolver *resolver, const char *asset_path);

    /**
     * @brief Read an asset by virtual path.
     *
     * @p asset_path may use the asset:// URI scheme or a plain relative virtual
     * path. The resolver rejects absolute paths, parent-directory traversal, and
     * unknown URI schemes to keep authored content portable.
     *
     * @return true when the file was found and copied into @p out_buffer.
     */
    bool slayer3d_asset_resolver_read_file(const slayer3d_asset_resolver *resolver, const char *asset_path,
                                           slayer3d_asset_buffer *out_buffer, char *error_buffer,
                                           int error_buffer_size);

    /**
     * @brief Enumerate immediate children under a virtual asset directory.
     *
     * Directory mounts enumerate the host filesystem. Pack and memory-pack
     * mounts derive virtual directories from packed entry paths. Later mounts
     * take precedence over earlier mounts, so duplicate child names are only
     * reported once.
     *
     * @p asset_directory may use asset:// or a plain relative virtual path.
     *
     * @return true when enumeration completes or the callback asks to stop.
     */
    bool slayer3d_asset_resolver_enumerate(const slayer3d_asset_resolver *resolver, const char *asset_directory,
                                           slayer3d_asset_enumerate_fn callback, void *userdata, char *error_buffer,
                                           int error_buffer_size);

    /**
     * @brief Resolve an asset to a host filesystem path when it comes from a directory mount.
     *
     * This is intended for integrations with third-party loaders that require
     * a filesystem path and cannot read through @ref slayer3d_asset_resolver_read_file().
     * Pack and memory-pack mounts cannot produce stable paths, so this returns
     * false for assets that exist only inside archives.
     *
     * Free @p out_path with @ref slayer3d_asset_resolver_free_path().
     *
     * @return true when @p asset_path resolves to an existing file on a mounted directory.
     */
    bool slayer3d_asset_resolver_resolve_file_path(const slayer3d_asset_resolver *resolver, const char *asset_path,
                                                   char **out_path, char *error_buffer, int error_buffer_size);

    /**
     * @brief Free a path returned by @ref slayer3d_asset_resolver_resolve_file_path().
     */
    void slayer3d_asset_resolver_free_path(char *path);

    /**
     * @brief Free bytes returned by slayer3d_asset_resolver_read_file().
     *
     * Safe to call with NULL or an already empty buffer.
     */
    void slayer3d_asset_buffer_free(slayer3d_asset_buffer *buffer);

    /**
     * @brief Write an SLAYER3D asset pack from explicit source files.
     *
     * Entries are normalized, sorted by asset path for deterministic output,
     * checked for duplicates, and written with explicit little-endian integers.
     * The build defaults to compressing the resulting pack bytes before they
     * are written to disk, while still preserving the same resolver and asset
     * path model. Compression can be disabled at build time with the
     * SLAYER3D_COMPRESS_PACKS CMake option. If SLAYER3D_PACK_PASSWORD is set at build
     * time, the pack bytes are wrapped in a lightweight obfuscation layer after
     * compression.
     *
     * @param pack_path Filesystem output path to create or replace.
     * @param entries Source files to include.
     * @param entry_count Number of entries in @p entries.
     * @param error_buffer Optional human-readable error output.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the pack was written successfully.
     */
    bool slayer3d_asset_pack_write_file(const char *pack_path, const slayer3d_asset_pack_source *entries,
                                        int entry_count, char *error_buffer, int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_ASSET_H */

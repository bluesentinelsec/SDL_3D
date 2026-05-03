/**
 * @file asset.c
 * @brief Virtual asset resolver implementation.
 */

#include "sdl3d/asset.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include "miniz.h"
#include "sdl3d_crypto.h"

#define SDL3D_PACK_MAGIC "S3DPAK1"
#define SDL3D_PACK_MAGIC_SIZE 8u
#define SDL3D_PACK_VERSION 1u
#define SDL3D_PACK_HEADER_SIZE 24u
#define SDL3D_PACK_ENTRY_FIXED_SIZE 18u
#define SDL3D_PACK_COMPRESSED_MAGIC "S3DCPK1"
#define SDL3D_PACK_COMPRESSED_HEADER_SIZE 32u
#define SDL3D_PACK_COMPRESSION_LEVEL MZ_DEFAULT_LEVEL
#define SDL3D_PACK_OBFUSCATED_MAGIC "S3DOPK1"
#define SDL3D_PACK_OBFUSCATED_HEADER_SIZE 76u

typedef enum asset_mount_type
{
    ASSET_MOUNT_DIRECTORY,
    ASSET_MOUNT_PACK,
} asset_mount_type;

typedef struct asset_pack_entry
{
    char *path;
    uint64_t offset;
    uint64_t size;
} asset_pack_entry;

typedef struct asset_pack_write_entry
{
    char *asset_path;
    const char *source_path;
    uint8_t *data;
    size_t size;
    uint64_t offset;
} asset_pack_write_entry;

typedef struct asset_pack
{
    char *name;
    uint8_t *data;
    size_t size;
    asset_pack_entry *entries;
    int entry_count;
} asset_pack;

typedef struct asset_mount
{
    asset_mount_type type;
    char *directory;
    asset_pack pack;
} asset_mount;

struct sdl3d_asset_resolver
{
    asset_mount *mounts;
    int mount_count;
};

static void set_asset_error(char *buffer, int buffer_size, const char *message)
{
    if (buffer != NULL && buffer_size > 0)
        SDL_snprintf(buffer, (size_t)buffer_size, "%s", message != NULL ? message : "unknown asset error");
}

static uint16_t read_u16le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8u);
}

static uint32_t read_u32le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) | ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

static uint64_t read_u64le(const uint8_t *data)
{
    return (uint64_t)read_u32le(data) | ((uint64_t)read_u32le(data + 4) << 32u);
}

static void write_u16le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void write_u32le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8u) & 0xFFu);
    data[2] = (uint8_t)((value >> 16u) & 0xFFu);
    data[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void write_u64le(uint8_t *data, uint64_t value)
{
    write_u32le(data, (uint32_t)(value & UINT32_MAX));
    write_u32le(data + 4, (uint32_t)(value >> 32u));
}

static bool pack_magic_matches(const uint8_t *data, const char *magic)
{
    return data != NULL && magic != NULL && SDL_memcmp(data, magic, SDL3D_PACK_MAGIC_SIZE - 1u) == 0 &&
           data[SDL3D_PACK_MAGIC_SIZE - 1u] == '\0';
}

static bool has_uri_scheme(const char *path)
{
    return path != NULL && SDL_strstr(path, "://") != NULL;
}

static char *normalize_asset_path(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return NULL;

    const char *input = path;
    if (SDL_strncmp(input, "asset://", 8) == 0)
        input += 8;
    else if (has_uri_scheme(input))
        return NULL;

    while (*input == '/' || *input == '\\')
        ++input;
    if (input[0] == '\0')
        return NULL;

    const size_t input_len = SDL_strlen(input);
    char *normalized = (char *)SDL_malloc(input_len + 1u);
    if (normalized == NULL)
        return NULL;

    size_t out_len = 0;
    const char *segment = input;
    while (*segment != '\0')
    {
        while (*segment == '/' || *segment == '\\')
            ++segment;
        const char *end = segment;
        while (*end != '\0' && *end != '/' && *end != '\\')
            ++end;

        const size_t segment_len = (size_t)(end - segment);
        if (segment_len == 0u || (segment_len == 1u && segment[0] == '.'))
        {
            segment = end;
            continue;
        }
        if ((segment_len == 2u && segment[0] == '.' && segment[1] == '.') || (segment_len >= 2u && segment[1] == ':'))
        {
            SDL_free(normalized);
            return NULL;
        }

        if (out_len > 0u)
            normalized[out_len++] = '/';
        SDL_memcpy(normalized + out_len, segment, segment_len);
        out_len += segment_len;
        segment = end;
    }

    if (out_len == 0u)
    {
        SDL_free(normalized);
        return NULL;
    }
    normalized[out_len] = '\0';
    return normalized;
}

static char *join_directory_path(const char *root, const char *asset_path)
{
    if (root == NULL || root[0] == '\0' || asset_path == NULL || asset_path[0] == '\0')
        return NULL;

    const size_t root_len = SDL_strlen(root);
    const size_t path_len = SDL_strlen(asset_path);
    const bool needs_sep = root_len > 0u && root[root_len - 1u] != '/' && root[root_len - 1u] != '\\';
    char *joined = (char *)SDL_malloc(root_len + (needs_sep ? 1u : 0u) + path_len + 1u);
    if (joined == NULL)
        return NULL;

    SDL_memcpy(joined, root, root_len);
    size_t offset = root_len;
    if (needs_sep)
        joined[offset++] = '/';
    SDL_memcpy(joined + offset, asset_path, path_len);
    joined[offset + path_len] = '\0';
    return joined;
}

static void destroy_pack(asset_pack *pack)
{
    if (pack == NULL)
        return;
    for (int i = 0; i < pack->entry_count; ++i)
        SDL_free(pack->entries[i].path);
    SDL_free(pack->entries);
    SDL_free(pack->data);
    SDL_free(pack->name);
    SDL_zero(*pack);
}

static void destroy_write_entries(asset_pack_write_entry *entries, int count)
{
    if (entries == NULL)
        return;
    for (int i = 0; i < count; ++i)
    {
        SDL_free(entries[i].asset_path);
        SDL_free(entries[i].data);
    }
    SDL_free(entries);
}

static int compare_write_entries(const void *a, const void *b)
{
    const asset_pack_write_entry *left = (const asset_pack_write_entry *)a;
    const asset_pack_write_entry *right = (const asset_pack_write_entry *)b;
    return SDL_strcmp(left->asset_path, right->asset_path);
}

static bool build_raw_pack_bytes(const asset_pack_write_entry *entries, int entry_count, uint8_t **out_data,
                                 size_t *out_size, char *error_buffer, int error_buffer_size)
{
    if (out_data != NULL)
        *out_data = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (entries == NULL || entry_count <= 0 || out_data == NULL || out_size == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "invalid pack build arguments");
        return false;
    }

    uint64_t table_size = 0u;
    for (int i = 0; i < entry_count; ++i)
    {
        const size_t path_len = SDL_strlen(entries[i].asset_path);
        if (path_len == 0u || path_len > UINT16_MAX)
        {
            set_asset_error(error_buffer, error_buffer_size, "asset pack entry path is too long");
            return false;
        }
        if (table_size > UINT64_MAX - (uint64_t)SDL3D_PACK_ENTRY_FIXED_SIZE - (uint64_t)path_len)
        {
            set_asset_error(error_buffer, error_buffer_size, "asset pack table is too large");
            return false;
        }
        table_size += (uint64_t)SDL3D_PACK_ENTRY_FIXED_SIZE + (uint64_t)path_len;
    }

    uint64_t data_offset = SDL3D_PACK_HEADER_SIZE + table_size;
    if (data_offset > (uint64_t)SIZE_MAX)
    {
        set_asset_error(error_buffer, error_buffer_size, "asset pack is too large");
        return false;
    }

    for (int i = 0; i < entry_count; ++i)
    {
        if (data_offset > UINT64_MAX - (uint64_t)entries[i].size)
        {
            set_asset_error(error_buffer, error_buffer_size, "asset pack is too large");
            return false;
        }
        data_offset += (uint64_t)entries[i].size;
    }

    uint8_t *data = (uint8_t *)SDL_calloc(1u, (size_t)data_offset);
    if (data == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "failed to allocate asset pack bytes");
        return false;
    }

    uint64_t cursor = SDL3D_PACK_HEADER_SIZE;
    SDL_memcpy(data, SDL3D_PACK_MAGIC, SDL3D_PACK_MAGIC_SIZE - 1u);
    data[SDL3D_PACK_MAGIC_SIZE - 1u] = '\0';
    write_u32le(data + 8, SDL3D_PACK_VERSION);
    write_u32le(data + 12, (uint32_t)entry_count);
    write_u64le(data + 16, SDL3D_PACK_HEADER_SIZE);

    uint64_t data_cursor = SDL3D_PACK_HEADER_SIZE + table_size;
    for (int i = 0; i < entry_count; ++i)
    {
        const size_t path_len = SDL_strlen(entries[i].asset_path);
        write_u16le(data + (size_t)cursor, (uint16_t)path_len);
        write_u64le(data + (size_t)cursor + 2u, data_cursor);
        write_u64le(data + (size_t)cursor + 10u, (uint64_t)entries[i].size);
        cursor += SDL3D_PACK_ENTRY_FIXED_SIZE;

        SDL_memcpy(data + (size_t)cursor, entries[i].asset_path, path_len);
        cursor += (uint64_t)path_len;

        if (entries[i].size > 0u)
            SDL_memcpy(data + (size_t)data_cursor, entries[i].data, entries[i].size);
        data_cursor += (uint64_t)entries[i].size;
    }

    *out_data = data;
    *out_size = (size_t)data_offset;
    return true;
}

static bool build_compressed_pack_bytes(const uint8_t *raw_data, size_t raw_size, uint8_t **out_data, size_t *out_size,
                                        char *error_buffer, int error_buffer_size)
{
    if (out_data != NULL)
        *out_data = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (raw_data == NULL || raw_size == 0u || out_data == NULL || out_size == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "invalid pack compression arguments");
        return false;
    }

    if (raw_size > (size_t)ULONG_MAX)
    {
        set_asset_error(error_buffer, error_buffer_size, "asset pack is too large to compress");
        return false;
    }

    const mz_ulong compressed_bound = mz_compressBound((mz_ulong)raw_size);
    if (compressed_bound > (mz_ulong)(SIZE_MAX - SDL3D_PACK_COMPRESSED_HEADER_SIZE))
    {
        set_asset_error(error_buffer, error_buffer_size, "asset pack is too large");
        return false;
    }

    uint8_t *data = (uint8_t *)SDL_malloc(SDL3D_PACK_COMPRESSED_HEADER_SIZE + (size_t)compressed_bound);
    if (data == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "failed to allocate compressed asset pack");
        return false;
    }

    mz_ulong compressed_size = compressed_bound;
    const int status = mz_compress2(data + SDL3D_PACK_COMPRESSED_HEADER_SIZE, &compressed_size, raw_data,
                                    (mz_ulong)raw_size, SDL3D_PACK_COMPRESSION_LEVEL);
    if (status != MZ_OK)
    {
        SDL_free(data);
        set_asset_error(error_buffer, error_buffer_size, "failed to compress asset pack");
        return false;
    }

    SDL_memcpy(data, SDL3D_PACK_COMPRESSED_MAGIC, SDL3D_PACK_MAGIC_SIZE - 1u);
    data[SDL3D_PACK_MAGIC_SIZE - 1u] = '\0';
    write_u32le(data + 8, SDL3D_PACK_VERSION);
    write_u32le(data + 12, 0u);
    write_u64le(data + 16, (uint64_t)raw_size);
    write_u64le(data + 24, (uint64_t)compressed_size);

    *out_data = data;
    *out_size = SDL3D_PACK_COMPRESSED_HEADER_SIZE + (size_t)compressed_size;
    return true;
}

static void derive_pack_salt(const uint8_t *data, size_t size, uint8_t salt[SDL3D_CRYPTO_SALT_SIZE])
{
    uint8_t digest[SDL3D_CRYPTO_HASH_SIZE];
    sdl3d_crypto_hash32(data, size, digest);
    SDL_memcpy(salt, digest, SDL3D_CRYPTO_SALT_SIZE);
}

static void derive_pack_nonce(const uint8_t *data, size_t size, uint8_t nonce[SDL3D_CRYPTO_NONCE_SIZE])
{
    static const char label[] = "SDL3D pack nonce";
    sdl3d_crypto_hash32_state state;
    uint8_t digest[SDL3D_CRYPTO_HASH_SIZE];

    sdl3d_crypto_hash32_init(&state);
    sdl3d_crypto_hash32_update(&state, label, sizeof(label) - 1u);
    sdl3d_crypto_hash32_update(&state, data, size);
    sdl3d_crypto_hash32_final(&state, digest);
    SDL_memcpy(nonce, digest, SDL3D_CRYPTO_NONCE_SIZE);
}

static void derive_pack_key(const char *password, const uint8_t salt[SDL3D_CRYPTO_SALT_SIZE],
                            uint8_t key[SDL3D_CRYPTO_HASH_SIZE])
{
    sdl3d_crypto_hash32_state state;
    sdl3d_crypto_hash32_init(&state);
    if (password != NULL && password[0] != '\0')
        sdl3d_crypto_hash32_update(&state, password, SDL_strlen(password));
    sdl3d_crypto_hash32_update(&state, salt, SDL3D_CRYPTO_SALT_SIZE);
    sdl3d_crypto_hash32_final(&state, key);
}

static void derive_pack_tag(const uint8_t key[SDL3D_CRYPTO_HASH_SIZE], const uint8_t *header, size_t header_size,
                            const uint8_t *payload, size_t payload_size, uint8_t tag[SDL3D_CRYPTO_TAG_SIZE])
{
    sdl3d_crypto_hash32_state state;
    uint8_t digest[SDL3D_CRYPTO_HASH_SIZE];

    sdl3d_crypto_hash32_init_keyed(&state, key, SDL3D_CRYPTO_HASH_SIZE);
    sdl3d_crypto_hash32_update(&state, header, header_size);
    sdl3d_crypto_hash32_update(&state, payload, payload_size);
    sdl3d_crypto_hash32_final(&state, digest);
    SDL_memcpy(tag, digest, SDL3D_CRYPTO_TAG_SIZE);
}

static bool build_obfuscated_pack_bytes(const uint8_t *plain_data, size_t plain_size, uint8_t **out_data,
                                        size_t *out_size, char *error_buffer, int error_buffer_size)
{
    if (out_data != NULL)
        *out_data = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (plain_data == NULL || plain_size == 0u || out_data == NULL || out_size == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "invalid pack obfuscation arguments");
        return false;
    }

    if (plain_size > (size_t)ULONG_MAX)
    {
        set_asset_error(error_buffer, error_buffer_size, "asset pack is too large to obfuscate");
        return false;
    }

    uint8_t salt[SDL3D_CRYPTO_SALT_SIZE];
    uint8_t nonce[SDL3D_CRYPTO_NONCE_SIZE];
    uint8_t key[SDL3D_CRYPTO_HASH_SIZE];
    derive_pack_salt(plain_data, plain_size, salt);
    derive_pack_nonce(plain_data, plain_size, nonce);
    derive_pack_key(SDL3D_PACK_PASSWORD, salt, key);

    if (SDL3D_PACK_OBFUSCATED_HEADER_SIZE > (size_t)(SIZE_MAX - plain_size))
    {
        set_asset_error(error_buffer, error_buffer_size, "asset pack is too large");
        return false;
    }

    uint8_t *data = (uint8_t *)SDL_malloc(SDL3D_PACK_OBFUSCATED_HEADER_SIZE + plain_size);
    if (data == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "failed to allocate obfuscated asset pack");
        return false;
    }

    SDL_memcpy(data + SDL3D_PACK_OBFUSCATED_HEADER_SIZE, plain_data, plain_size);

    SDL_memcpy(data, SDL3D_PACK_OBFUSCATED_MAGIC, SDL3D_PACK_MAGIC_SIZE - 1u);
    data[SDL3D_PACK_MAGIC_SIZE - 1u] = '\0';
    write_u32le(data + 8, SDL3D_PACK_VERSION);
    write_u64le(data + 12, (uint64_t)plain_size);
    write_u64le(data + 20, (uint64_t)plain_size);
    SDL_memcpy(data + 28, salt, SDL3D_CRYPTO_SALT_SIZE);
    SDL_memcpy(data + 44, nonce, SDL3D_CRYPTO_NONCE_SIZE);

    sdl3d_crypto_xor_stream(data + SDL3D_PACK_OBFUSCATED_HEADER_SIZE, plain_size, key, nonce);

    uint8_t tag[SDL3D_CRYPTO_TAG_SIZE];
    derive_pack_tag(key, data, 60u, data + SDL3D_PACK_OBFUSCATED_HEADER_SIZE, plain_size, tag);
    SDL_memcpy(data + 60, tag, SDL3D_CRYPTO_TAG_SIZE);

    *out_data = data;
    *out_size = SDL3D_PACK_OBFUSCATED_HEADER_SIZE + plain_size;
    return true;
}

static bool normalize_pack_blob(uint8_t **data_ptr, size_t *size_ptr, const char *debug_name, char *error_buffer,
                                int error_buffer_size)
{
    if (data_ptr == NULL || size_ptr == NULL || *data_ptr == NULL || *size_ptr == 0u)
    {
        set_asset_error(error_buffer, error_buffer_size, "invalid pack blob");
        return false;
    }

    if (*size_ptr >= SDL3D_PACK_HEADER_SIZE && pack_magic_matches(*data_ptr, SDL3D_PACK_MAGIC))
        return true;

    if (*size_ptr < SDL3D_PACK_COMPRESSED_HEADER_SIZE || !pack_magic_matches(*data_ptr, SDL3D_PACK_COMPRESSED_MAGIC))
    {
        if (*size_ptr < SDL3D_PACK_OBFUSCATED_HEADER_SIZE ||
            !pack_magic_matches(*data_ptr, SDL3D_PACK_OBFUSCATED_MAGIC))
        {
            set_asset_error(error_buffer, error_buffer_size, "pack has invalid magic");
            return false;
        }

        const uint32_t version = read_u32le(*data_ptr + 8);
        const uint64_t plain_size = read_u64le(*data_ptr + 12);
        const uint64_t payload_size = read_u64le(*data_ptr + 20);
        if (version != SDL3D_PACK_VERSION)
        {
            set_asset_error(error_buffer, error_buffer_size, "unsupported obfuscated pack version");
            return false;
        }
        if (plain_size == 0u || payload_size == 0u || plain_size > (uint64_t)SIZE_MAX ||
            payload_size > (uint64_t)SIZE_MAX || SDL3D_PACK_OBFUSCATED_HEADER_SIZE + payload_size != *size_ptr)
        {
            set_asset_error(error_buffer, error_buffer_size, "obfuscated pack header is invalid");
            return false;
        }
        if (SDL3D_PACK_PASSWORD[0] == '\0')
        {
            set_asset_error(error_buffer, error_buffer_size, "obfuscated pack requires a password");
            return false;
        }

        uint8_t salt[SDL3D_CRYPTO_SALT_SIZE];
        uint8_t nonce[SDL3D_CRYPTO_NONCE_SIZE];
        uint8_t key[SDL3D_CRYPTO_HASH_SIZE];
        uint8_t expected_tag[SDL3D_CRYPTO_TAG_SIZE];
        uint8_t actual_tag[SDL3D_CRYPTO_TAG_SIZE];
        SDL_memcpy(salt, *data_ptr + 28, SDL3D_CRYPTO_SALT_SIZE);
        SDL_memcpy(nonce, *data_ptr + 44, SDL3D_CRYPTO_NONCE_SIZE);
        SDL_memcpy(actual_tag, *data_ptr + 60, SDL3D_CRYPTO_TAG_SIZE);
        derive_pack_key(SDL3D_PACK_PASSWORD, salt, key);
        derive_pack_tag(key, *data_ptr, 60u, *data_ptr + SDL3D_PACK_OBFUSCATED_HEADER_SIZE, (size_t)payload_size,
                        expected_tag);
        if (SDL_memcmp(actual_tag, expected_tag, SDL3D_CRYPTO_TAG_SIZE) != 0)
        {
            set_asset_error(error_buffer, error_buffer_size, "obfuscated pack password or tag is invalid");
            return false;
        }

        uint8_t *payload = (uint8_t *)SDL_malloc((size_t)payload_size);
        if (payload == NULL)
        {
            set_asset_error(error_buffer, error_buffer_size, "failed to allocate decrypted pack");
            return false;
        }

        SDL_memcpy(payload, *data_ptr + SDL3D_PACK_OBFUSCATED_HEADER_SIZE, (size_t)payload_size);
        sdl3d_crypto_xor_stream(payload, (size_t)payload_size, key, nonce);

        SDL_free(*data_ptr);
        *data_ptr = payload;
        *size_ptr = (size_t)payload_size;
        return normalize_pack_blob(data_ptr, size_ptr, debug_name, error_buffer, error_buffer_size);
    }

    const uint32_t version = read_u32le(*data_ptr + 8);
    const uint64_t uncompressed_size = read_u64le(*data_ptr + 16);
    const uint64_t compressed_size = read_u64le(*data_ptr + 24);
    if (version != SDL3D_PACK_VERSION)
    {
        set_asset_error(error_buffer, error_buffer_size, "unsupported compressed pack version");
        return false;
    }
    if (uncompressed_size == 0u || compressed_size == 0u || uncompressed_size > (uint64_t)SIZE_MAX ||
        compressed_size > (uint64_t)SIZE_MAX || SDL3D_PACK_COMPRESSED_HEADER_SIZE + compressed_size != *size_ptr ||
        uncompressed_size > (uint64_t)ULONG_MAX || compressed_size > (uint64_t)ULONG_MAX)
    {
        set_asset_error(error_buffer, error_buffer_size, "compressed pack header is invalid");
        return false;
    }

    uint8_t *raw = (uint8_t *)SDL_malloc((size_t)uncompressed_size);
    if (raw == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "failed to allocate decompressed pack");
        return false;
    }

    mz_ulong raw_size = (mz_ulong)uncompressed_size;
    const int status =
        mz_uncompress(raw, &raw_size, *data_ptr + SDL3D_PACK_COMPRESSED_HEADER_SIZE, (mz_ulong)compressed_size);
    if (status != MZ_OK || raw_size != (mz_ulong)uncompressed_size)
    {
        SDL_free(raw);
        set_asset_error(error_buffer, error_buffer_size, "failed to decompress pack");
        return false;
    }

    SDL_free(*data_ptr);
    *data_ptr = raw;
    *size_ptr = (size_t)raw_size;
    (void)debug_name;
    return true;
}

static bool append_mount(sdl3d_asset_resolver *resolver, asset_mount **out_mount)
{
    if (out_mount != NULL)
        *out_mount = NULL;
    if (resolver == NULL || out_mount == NULL)
        return false;

    asset_mount *mounts =
        (asset_mount *)SDL_realloc(resolver->mounts, (size_t)(resolver->mount_count + 1) * sizeof(*mounts));
    if (mounts == NULL)
        return false;
    resolver->mounts = mounts;

    asset_mount *mount = &resolver->mounts[resolver->mount_count];
    SDL_zero(*mount);
    resolver->mount_count++;
    *out_mount = mount;
    return true;
}

static bool write_all(SDL_IOStream *io, const void *data, size_t size)
{
    if (size == 0u)
        return true;
    return SDL_WriteIO(io, data, size) == size;
}

static bool parse_pack_entries(asset_pack *pack, char *error_buffer, int error_buffer_size)
{
    if (pack == NULL || pack->data == NULL || pack->size < SDL3D_PACK_HEADER_SIZE)
    {
        set_asset_error(error_buffer, error_buffer_size, "pack is too small");
        return false;
    }
    if (SDL_memcmp(pack->data, SDL3D_PACK_MAGIC, SDL3D_PACK_MAGIC_SIZE - 1u) != 0 || pack->data[7] != '\0')
    {
        set_asset_error(error_buffer, error_buffer_size, "pack has invalid magic");
        return false;
    }

    const uint32_t version = read_u32le(pack->data + 8);
    const uint32_t entry_count = read_u32le(pack->data + 12);
    const uint64_t table_offset = read_u64le(pack->data + 16);
    if (version != SDL3D_PACK_VERSION)
    {
        set_asset_error(error_buffer, error_buffer_size, "unsupported pack version");
        return false;
    }
    if (entry_count > (uint32_t)INT32_MAX || table_offset < SDL3D_PACK_HEADER_SIZE ||
        table_offset > (uint64_t)pack->size)
    {
        set_asset_error(error_buffer, error_buffer_size, "pack table header is invalid");
        return false;
    }

    pack->entry_count = (int)entry_count;
    pack->entries = (asset_pack_entry *)SDL_calloc((size_t)pack->entry_count, sizeof(*pack->entries));
    if (pack->entries == NULL && pack->entry_count > 0)
    {
        set_asset_error(error_buffer, error_buffer_size, "failed to allocate pack entries");
        return false;
    }

    size_t cursor = (size_t)table_offset;
    for (int i = 0; i < pack->entry_count; ++i)
    {
        if (cursor > pack->size || pack->size - cursor < SDL3D_PACK_ENTRY_FIXED_SIZE)
        {
            set_asset_error(error_buffer, error_buffer_size, "pack table is truncated");
            return false;
        }

        const uint16_t path_len = read_u16le(pack->data + cursor);
        const uint64_t offset = read_u64le(pack->data + cursor + 2u);
        const uint64_t size = read_u64le(pack->data + cursor + 10u);
        cursor += SDL3D_PACK_ENTRY_FIXED_SIZE;

        if (path_len == 0u || cursor > pack->size || pack->size - cursor < (size_t)path_len ||
            offset > (uint64_t)pack->size || size > (uint64_t)pack->size || offset > (uint64_t)pack->size - size ||
            offset > (uint64_t)SIZE_MAX || size > (uint64_t)SIZE_MAX)
        {
            set_asset_error(error_buffer, error_buffer_size, "pack entry is invalid");
            return false;
        }

        char *raw_path = (char *)SDL_malloc((size_t)path_len + 1u);
        if (raw_path == NULL)
        {
            set_asset_error(error_buffer, error_buffer_size, "failed to allocate pack path");
            return false;
        }
        SDL_memcpy(raw_path, pack->data + cursor, path_len);
        raw_path[path_len] = '\0';
        cursor += path_len;

        char *normalized = normalize_asset_path(raw_path);
        SDL_free(raw_path);
        if (normalized == NULL)
        {
            set_asset_error(error_buffer, error_buffer_size, "pack entry path is invalid");
            return false;
        }

        pack->entries[i].path = normalized;
        pack->entries[i].offset = offset;
        pack->entries[i].size = size;
    }

    return true;
}

static bool mount_owned_pack(sdl3d_asset_resolver *resolver, uint8_t *data, size_t size, const char *debug_name,
                             char *error_buffer, int error_buffer_size)
{
    asset_mount *mount = NULL;
    if (!append_mount(resolver, &mount))
    {
        SDL_free(data);
        set_asset_error(error_buffer, error_buffer_size, "failed to allocate asset mount");
        return false;
    }

    mount->type = ASSET_MOUNT_PACK;
    mount->pack.data = data;
    mount->pack.size = size;
    mount->pack.name = SDL_strdup(debug_name != NULL ? debug_name : "<memory-pack>");
    if (mount->pack.name == NULL)
    {
        resolver->mount_count--;
        SDL_free(data);
        set_asset_error(error_buffer, error_buffer_size, "failed to copy pack name");
        return false;
    }
    if (!parse_pack_entries(&mount->pack, error_buffer, error_buffer_size))
    {
        destroy_pack(&mount->pack);
        resolver->mount_count--;
        return false;
    }
    return true;
}

static const asset_pack_entry *find_pack_entry(const asset_pack *pack, const char *normalized_path)
{
    if (pack == NULL || normalized_path == NULL)
        return NULL;
    for (int i = 0; i < pack->entry_count; ++i)
    {
        if (SDL_strcmp(pack->entries[i].path, normalized_path) == 0)
            return &pack->entries[i];
    }
    return NULL;
}

static bool read_directory_asset(const asset_mount *mount, const char *normalized_path, sdl3d_asset_buffer *out_buffer)
{
    char *path = join_directory_path(mount->directory, normalized_path);
    if (path == NULL)
        return false;

    size_t bytes = 0u;
    void *data = SDL_LoadFile(path, &bytes);
    SDL_free(path);
    if (data == NULL)
        return false;

    out_buffer->data = data;
    out_buffer->size = bytes;
    return true;
}

static bool directory_asset_exists(const asset_mount *mount, const char *normalized_path)
{
    char *path = join_directory_path(mount->directory, normalized_path);
    if (path == NULL)
        return false;

    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    SDL_free(path);
    if (io == NULL)
        return false;

    SDL_CloseIO(io);
    return true;
}

sdl3d_asset_resolver *sdl3d_asset_resolver_create(void)
{
    return (sdl3d_asset_resolver *)SDL_calloc(1, sizeof(sdl3d_asset_resolver));
}

void sdl3d_asset_resolver_destroy(sdl3d_asset_resolver *resolver)
{
    if (resolver == NULL)
        return;
    for (int i = 0; i < resolver->mount_count; ++i)
    {
        if (resolver->mounts[i].type == ASSET_MOUNT_DIRECTORY)
            SDL_free(resolver->mounts[i].directory);
        else
            destroy_pack(&resolver->mounts[i].pack);
    }
    SDL_free(resolver->mounts);
    SDL_free(resolver);
}

bool sdl3d_asset_resolver_mount_directory(sdl3d_asset_resolver *resolver, const char *root_directory,
                                          char *error_buffer, int error_buffer_size)
{
    if (resolver == NULL || root_directory == NULL || root_directory[0] == '\0')
    {
        set_asset_error(error_buffer, error_buffer_size, "invalid directory mount arguments");
        return false;
    }

    asset_mount *mount = NULL;
    if (!append_mount(resolver, &mount))
    {
        set_asset_error(error_buffer, error_buffer_size, "failed to allocate directory mount");
        return false;
    }
    mount->type = ASSET_MOUNT_DIRECTORY;
    mount->directory = SDL_strdup(root_directory);
    if (mount->directory == NULL)
    {
        resolver->mount_count--;
        set_asset_error(error_buffer, error_buffer_size, "failed to copy directory mount path");
        return false;
    }
    return true;
}

bool sdl3d_asset_resolver_mount_pack_file(sdl3d_asset_resolver *resolver, const char *pack_path, char *error_buffer,
                                          int error_buffer_size)
{
    if (resolver == NULL || pack_path == NULL || pack_path[0] == '\0')
    {
        set_asset_error(error_buffer, error_buffer_size, "invalid pack file mount arguments");
        return false;
    }

    size_t bytes = 0u;
    uint8_t *data = (uint8_t *)SDL_LoadFile(pack_path, &bytes);
    if (data == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, SDL_GetError());
        return false;
    }
    if (!normalize_pack_blob(&data, &bytes, pack_path, error_buffer, error_buffer_size))
    {
        SDL_free(data);
        return false;
    }
    return mount_owned_pack(resolver, data, bytes, pack_path, error_buffer, error_buffer_size);
}

bool sdl3d_asset_resolver_mount_memory_pack(sdl3d_asset_resolver *resolver, const void *data, size_t size,
                                            const char *debug_name, char *error_buffer, int error_buffer_size)
{
    if (resolver == NULL || data == NULL || size == 0u)
    {
        set_asset_error(error_buffer, error_buffer_size, "invalid memory pack mount arguments");
        return false;
    }

    uint8_t *copy = (uint8_t *)SDL_malloc(size);
    if (copy == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "failed to copy memory pack");
        return false;
    }
    SDL_memcpy(copy, data, size);
    if (!normalize_pack_blob(&copy, &size, debug_name, error_buffer, error_buffer_size))
    {
        SDL_free(copy);
        return false;
    }
    return mount_owned_pack(resolver, copy, size, debug_name, error_buffer, error_buffer_size);
}

bool sdl3d_asset_resolver_exists(const sdl3d_asset_resolver *resolver, const char *asset_path)
{
    if (resolver == NULL)
        return false;

    char *normalized = normalize_asset_path(asset_path);
    if (normalized == NULL)
        return false;

    for (int i = resolver->mount_count - 1; i >= 0; --i)
    {
        const asset_mount *mount = &resolver->mounts[i];
        const bool exists = mount->type == ASSET_MOUNT_DIRECTORY ? directory_asset_exists(mount, normalized)
                                                                 : find_pack_entry(&mount->pack, normalized) != NULL;
        if (exists)
        {
            SDL_free(normalized);
            return true;
        }
    }

    SDL_free(normalized);
    return false;
}

bool sdl3d_asset_resolver_read_file(const sdl3d_asset_resolver *resolver, const char *asset_path,
                                    sdl3d_asset_buffer *out_buffer, char *error_buffer, int error_buffer_size)
{
    if (out_buffer != NULL)
        SDL_zero(*out_buffer);
    if (resolver == NULL || out_buffer == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "invalid asset read arguments");
        return false;
    }

    char *normalized = normalize_asset_path(asset_path);
    if (normalized == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "invalid asset path");
        return false;
    }

    for (int i = resolver->mount_count - 1; i >= 0; --i)
    {
        const asset_mount *mount = &resolver->mounts[i];
        if (mount->type == ASSET_MOUNT_DIRECTORY)
        {
            if (read_directory_asset(mount, normalized, out_buffer))
            {
                SDL_free(normalized);
                return true;
            }
            continue;
        }

        const asset_pack_entry *entry = find_pack_entry(&mount->pack, normalized);
        if (entry == NULL)
            continue;
        void *copy = SDL_malloc((size_t)entry->size);
        if (copy == NULL && entry->size > 0u)
        {
            SDL_free(normalized);
            set_asset_error(error_buffer, error_buffer_size, "failed to allocate asset buffer");
            return false;
        }
        if (entry->size > 0u)
            SDL_memcpy(copy, mount->pack.data + (size_t)entry->offset, (size_t)entry->size);
        out_buffer->data = copy;
        out_buffer->size = (size_t)entry->size;
        SDL_free(normalized);
        return true;
    }

    SDL_free(normalized);
    set_asset_error(error_buffer, error_buffer_size, "asset was not found");
    return false;
}

void sdl3d_asset_buffer_free(sdl3d_asset_buffer *buffer)
{
    if (buffer == NULL)
        return;
    SDL_free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0u;
}

bool sdl3d_asset_pack_write_file(const char *pack_path, const sdl3d_asset_pack_source *entries, int entry_count,
                                 char *error_buffer, int error_buffer_size)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        error_buffer[0] = '\0';
    if (pack_path == NULL || pack_path[0] == '\0' || entries == NULL || entry_count <= 0)
    {
        set_asset_error(error_buffer, error_buffer_size, "invalid asset pack write arguments");
        return false;
    }

    asset_pack_write_entry *write_entries =
        (asset_pack_write_entry *)SDL_calloc((size_t)entry_count, sizeof(*write_entries));
    if (write_entries == NULL)
    {
        set_asset_error(error_buffer, error_buffer_size, "failed to allocate asset pack entries");
        return false;
    }

    bool ok = true;
    for (int i = 0; i < entry_count && ok; ++i)
    {
        if (entries[i].asset_path == NULL || entries[i].source_path == NULL || entries[i].source_path[0] == '\0')
        {
            set_asset_error(error_buffer, error_buffer_size, "asset pack entry is missing a path");
            ok = false;
            break;
        }

        write_entries[i].asset_path = normalize_asset_path(entries[i].asset_path);
        write_entries[i].source_path = entries[i].source_path;
        if (write_entries[i].asset_path == NULL)
        {
            set_asset_error(error_buffer, error_buffer_size, "asset pack entry has an invalid asset path");
            ok = false;
            break;
        }

        const size_t path_len = SDL_strlen(write_entries[i].asset_path);
        if (path_len == 0u || path_len > UINT16_MAX)
        {
            set_asset_error(error_buffer, error_buffer_size, "asset pack entry path is too long");
            ok = false;
            break;
        }

        size_t bytes = 0u;
        write_entries[i].data = (uint8_t *)SDL_LoadFile(entries[i].source_path, &bytes);
        if (write_entries[i].data == NULL)
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to read asset source '%s': %s",
                             entries[i].source_path, SDL_GetError());
            }
            ok = false;
            break;
        }
        write_entries[i].size = bytes;
    }

    if (ok)
    {
        qsort(write_entries, (size_t)entry_count, sizeof(*write_entries), compare_write_entries);
        for (int i = 1; i < entry_count; ++i)
        {
            if (SDL_strcmp(write_entries[i - 1].asset_path, write_entries[i].asset_path) == 0)
            {
                set_asset_error(error_buffer, error_buffer_size, "asset pack contains duplicate asset paths");
                ok = false;
                break;
            }
        }
    }

    uint8_t *raw_data = NULL;
    size_t raw_size = 0u;
    if (ok)
    {
        ok = build_raw_pack_bytes(write_entries, entry_count, &raw_data, &raw_size, error_buffer, error_buffer_size);
    }

    uint8_t *output_data = raw_data;
    size_t output_size = raw_size;
    uint8_t *compressed_data = NULL;
    uint8_t *obfuscated_data = NULL;
    if (ok)
    {
#if SDL3D_PACK_COMPRESSION_ENABLED
        if (raw_size <= (size_t)ULONG_MAX)
        {
            ok = build_compressed_pack_bytes(raw_data, raw_size, &compressed_data, &output_size, error_buffer,
                                             error_buffer_size);
            SDL_free(raw_data);
            raw_data = NULL;
            if (ok)
                output_data = compressed_data;
        }
#endif
    }

    if (ok && SDL3D_PACK_PASSWORD[0] != '\0')
    {
        ok = build_obfuscated_pack_bytes(output_data, output_size, &obfuscated_data, &output_size, error_buffer,
                                         error_buffer_size);
        if (ok)
            output_data = obfuscated_data;
    }

    SDL_IOStream *io = NULL;
    if (ok)
    {
        io = SDL_IOFromFile(pack_path, "wb");
        if (io == NULL)
        {
            set_asset_error(error_buffer, error_buffer_size, SDL_GetError());
            ok = false;
        }
    }

    if (ok)
    {
        ok = write_all(io, output_data, output_size);
    }

    if (io != NULL && !SDL_CloseIO(io))
        ok = false;
    if (!ok && error_buffer != NULL && error_buffer_size > 0 && error_buffer[0] == '\0')
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to write asset pack '%s'", pack_path);

    SDL_free(raw_data);
    SDL_free(compressed_data);
    SDL_free(obfuscated_data);
    destroy_write_entries(write_entries, entry_count);
    return ok;
}

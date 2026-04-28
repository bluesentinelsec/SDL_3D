/**
 * @file asset.c
 * @brief Virtual asset resolver implementation.
 */

#include "sdl3d/asset.h"

#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#define SDL3D_PACK_MAGIC "S3DPAK1"
#define SDL3D_PACK_MAGIC_SIZE 8u
#define SDL3D_PACK_VERSION 1u
#define SDL3D_PACK_HEADER_SIZE 24u
#define SDL3D_PACK_ENTRY_FIXED_SIZE 18u

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
    uint64_t table_size = 0u;
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
        table_size += SDL3D_PACK_ENTRY_FIXED_SIZE + path_len;
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

    uint64_t data_offset = SDL3D_PACK_HEADER_SIZE + table_size;
    for (int i = 0; i < entry_count && ok; ++i)
    {
        if (data_offset > UINT64_MAX - (uint64_t)write_entries[i].size)
        {
            set_asset_error(error_buffer, error_buffer_size, "asset pack is too large");
            ok = false;
            break;
        }
        write_entries[i].offset = data_offset;
        data_offset += (uint64_t)write_entries[i].size;
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
        uint8_t header[SDL3D_PACK_HEADER_SIZE];
        SDL_zeroa(header);
        SDL_memcpy(header, SDL3D_PACK_MAGIC, SDL3D_PACK_MAGIC_SIZE - 1u);
        write_u32le(header + 8, SDL3D_PACK_VERSION);
        write_u32le(header + 12, (uint32_t)entry_count);
        write_u64le(header + 16, SDL3D_PACK_HEADER_SIZE);
        ok = write_all(io, header, sizeof(header));
    }

    for (int i = 0; i < entry_count && ok; ++i)
    {
        const size_t path_len = SDL_strlen(write_entries[i].asset_path);
        uint8_t entry_header[SDL3D_PACK_ENTRY_FIXED_SIZE];
        write_u16le(entry_header, (uint16_t)path_len);
        write_u64le(entry_header + 2, write_entries[i].offset);
        write_u64le(entry_header + 10, (uint64_t)write_entries[i].size);
        ok = write_all(io, entry_header, sizeof(entry_header)) && write_all(io, write_entries[i].asset_path, path_len);
    }

    for (int i = 0; i < entry_count && ok; ++i)
        ok = write_all(io, write_entries[i].data, write_entries[i].size);

    if (io != NULL && !SDL_CloseIO(io))
        ok = false;
    if (!ok && error_buffer != NULL && error_buffer_size > 0 && error_buffer[0] == '\0')
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to write asset pack '%s'", pack_path);

    destroy_write_entries(write_entries, entry_count);
    return ok;
}

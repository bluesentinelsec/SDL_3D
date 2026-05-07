/**
 * @file sdl3d_fused.c
 * @brief Footer reader/writer for SDL3D fused runner executables.
 */

#include "sdl3d_fused.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

enum
{
    SDL3D_FUSED_FOOTER_SIZE = 512,
    SDL3D_FUSED_MAGIC_SIZE = 16,
    SDL3D_FUSED_VERSION = 1,
    SDL3D_FUSED_PATH_OFFSET = 48,
};

static const unsigned char SDL3D_FUSED_MAGIC[SDL3D_FUSED_MAGIC_SIZE] = {
    'S', 'D', 'L', '3', 'D', 'F', 'U', 'S', 'E', 'D', 'P', 'A', 'K', '1', '\r', '\n',
};

static void fused_set_error(char *error_buffer, int error_buffer_size, const char *message)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s", message != NULL ? message : "unknown error");
}

static void fused_set_errorf(char *error_buffer, int error_buffer_size, const char *fmt, const char *value)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, fmt, value != NULL ? value : "");
}

static uint16_t fused_read_u16(const unsigned char *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t fused_read_u32(const unsigned char *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static uint64_t fused_read_u64(const unsigned char *src)
{
    uint64_t value = 0u;
    for (int i = 7; i >= 0; --i)
        value = (value << 8) | src[i];
    return value;
}

static void fused_write_u16(unsigned char *dst, uint16_t value)
{
    dst[0] = (unsigned char)(value & 0xffu);
    dst[1] = (unsigned char)((value >> 8) & 0xffu);
}

static void fused_write_u32(unsigned char *dst, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        dst[i] = (unsigned char)((value >> (i * 8)) & 0xffu);
}

static void fused_write_u64(unsigned char *dst, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        dst[i] = (unsigned char)((value >> (i * 8)) & 0xffu);
}

static bool fused_get_file_size(SDL_IOStream *io, uint64_t *out_size)
{
    const Sint64 end = SDL_SeekIO(io, 0, SDL_IO_SEEK_END);
    if (end < 0)
        return false;
    if (out_size != NULL)
        *out_size = (uint64_t)end;
    return true;
}

bool sdl3d_fused_read_footer(const char *executable_path, sdl3d_fused_pack *out_pack, char *error_buffer,
                             int error_buffer_size)
{
    if (out_pack != NULL)
        SDL_zero(*out_pack);
    if (executable_path == NULL || executable_path[0] == '\0' || out_pack == NULL)
    {
        fused_set_error(error_buffer, error_buffer_size, "invalid fused footer arguments");
        return false;
    }

    SDL_IOStream *io = SDL_IOFromFile(executable_path, "rb");
    if (io == NULL)
    {
        fused_set_error(error_buffer, error_buffer_size, SDL_GetError());
        return false;
    }

    uint64_t file_size = 0u;
    bool ok = fused_get_file_size(io, &file_size);
    unsigned char footer[SDL3D_FUSED_FOOTER_SIZE];
    if (ok && file_size < SDL3D_FUSED_FOOTER_SIZE)
    {
        fused_set_error(error_buffer, error_buffer_size, "executable does not contain an SDL3D fused pack footer");
        ok = false;
    }
    if (ok && SDL_SeekIO(io, (Sint64)(file_size - SDL3D_FUSED_FOOTER_SIZE), SDL_IO_SEEK_SET) < 0)
    {
        fused_set_error(error_buffer, error_buffer_size, SDL_GetError());
        ok = false;
    }
    if (ok && SDL_ReadIO(io, footer, sizeof(footer)) != sizeof(footer))
    {
        fused_set_error(error_buffer, error_buffer_size, "failed to read SDL3D fused pack footer");
        ok = false;
    }
    SDL_CloseIO(io);
    if (!ok)
        return false;

    if (SDL_memcmp(footer, SDL3D_FUSED_MAGIC, SDL3D_FUSED_MAGIC_SIZE) != 0)
    {
        fused_set_error(error_buffer, error_buffer_size, "executable does not contain an SDL3D fused pack footer");
        return false;
    }
    if (fused_read_u32(footer + 16) != SDL3D_FUSED_VERSION || fused_read_u32(footer + 20) != SDL3D_FUSED_FOOTER_SIZE)
    {
        fused_set_error(error_buffer, error_buffer_size, "unsupported SDL3D fused pack footer version");
        return false;
    }

    const uint64_t pack_offset = fused_read_u64(footer + 24);
    const uint64_t pack_size = fused_read_u64(footer + 32);
    const uint16_t path_len = fused_read_u16(footer + 40);
    if (path_len == 0u || path_len > SDL3D_FUSED_DATA_ASSET_MAX)
    {
        fused_set_error(error_buffer, error_buffer_size, "invalid SDL3D fused pack data asset path");
        return false;
    }
    if (pack_size == 0u || pack_offset > file_size - SDL3D_FUSED_FOOTER_SIZE ||
        pack_size > file_size - SDL3D_FUSED_FOOTER_SIZE - pack_offset || pack_offset > (uint64_t)INT64_MAX)
    {
        fused_set_error(error_buffer, error_buffer_size, "invalid SDL3D fused pack byte range");
        return false;
    }

    out_pack->pack_offset = pack_offset;
    out_pack->pack_size = pack_size;
    SDL_memcpy(out_pack->data_asset_path, footer + SDL3D_FUSED_PATH_OFFSET, path_len);
    out_pack->data_asset_path[path_len] = '\0';
    return true;
}

bool sdl3d_fused_read_pack_bytes(const char *executable_path, const sdl3d_fused_pack *pack, void **out_data,
                                 size_t *out_size, char *error_buffer, int error_buffer_size)
{
    if (out_data != NULL)
        *out_data = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (executable_path == NULL || pack == NULL || out_data == NULL || out_size == NULL || pack->pack_size == 0u ||
        pack->pack_size > SIZE_MAX || pack->pack_offset > (uint64_t)INT64_MAX)
    {
        fused_set_error(error_buffer, error_buffer_size, "invalid fused pack read arguments");
        return false;
    }

    SDL_IOStream *io = SDL_IOFromFile(executable_path, "rb");
    if (io == NULL)
    {
        fused_set_error(error_buffer, error_buffer_size, SDL_GetError());
        return false;
    }

    void *data = SDL_malloc((size_t)pack->pack_size);
    if (data == NULL)
    {
        SDL_CloseIO(io);
        fused_set_error(error_buffer, error_buffer_size, "failed to allocate fused pack bytes");
        return false;
    }

    bool ok = SDL_SeekIO(io, (Sint64)pack->pack_offset, SDL_IO_SEEK_SET) >= 0;
    if (ok && SDL_ReadIO(io, data, (size_t)pack->pack_size) != (size_t)pack->pack_size)
    {
        fused_set_error(error_buffer, error_buffer_size, "failed to read fused pack bytes");
        ok = false;
    }
    else if (!ok)
    {
        fused_set_error(error_buffer, error_buffer_size, SDL_GetError());
    }
    SDL_CloseIO(io);
    if (!ok)
    {
        SDL_free(data);
        return false;
    }

    *out_data = data;
    *out_size = (size_t)pack->pack_size;
    return true;
}

bool sdl3d_fused_mount_pack(sdl3d_asset_resolver *resolver, const char *executable_path, const sdl3d_fused_pack *pack,
                            char *error_buffer, int error_buffer_size)
{
    void *data = NULL;
    size_t size = 0u;
    if (!sdl3d_fused_read_pack_bytes(executable_path, pack, &data, &size, error_buffer, error_buffer_size))
        return false;
    const bool ok =
        sdl3d_asset_resolver_mount_memory_pack(resolver, data, size, executable_path, error_buffer, error_buffer_size);
    SDL_free(data);
    return ok;
}

bool sdl3d_fused_append_footer(const char *output_path, uint64_t pack_offset, uint64_t pack_size,
                               const char *data_asset_path, char *error_buffer, int error_buffer_size)
{
    if (output_path == NULL || output_path[0] == '\0' || pack_size == 0u || data_asset_path == NULL ||
        data_asset_path[0] == '\0')
    {
        fused_set_error(error_buffer, error_buffer_size, "invalid fused footer write arguments");
        return false;
    }
    const size_t path_len = SDL_strlen(data_asset_path);
    if (path_len > SDL3D_FUSED_DATA_ASSET_MAX)
    {
        fused_set_errorf(error_buffer, error_buffer_size, "fused data asset path is too long: '%s'", data_asset_path);
        return false;
    }

    unsigned char footer[SDL3D_FUSED_FOOTER_SIZE];
    SDL_zeroa(footer);
    SDL_memcpy(footer, SDL3D_FUSED_MAGIC, SDL3D_FUSED_MAGIC_SIZE);
    fused_write_u32(footer + 16, SDL3D_FUSED_VERSION);
    fused_write_u32(footer + 20, SDL3D_FUSED_FOOTER_SIZE);
    fused_write_u64(footer + 24, pack_offset);
    fused_write_u64(footer + 32, pack_size);
    fused_write_u16(footer + 40, (uint16_t)path_len);
    SDL_memcpy(footer + SDL3D_FUSED_PATH_OFFSET, data_asset_path, path_len);

    SDL_IOStream *io = SDL_IOFromFile(output_path, "ab");
    if (io == NULL)
    {
        fused_set_error(error_buffer, error_buffer_size, SDL_GetError());
        return false;
    }
    const bool ok = SDL_WriteIO(io, footer, sizeof(footer)) == sizeof(footer);
    SDL_CloseIO(io);
    if (!ok)
    {
        fused_set_error(error_buffer, error_buffer_size, "failed to write SDL3D fused pack footer");
        return false;
    }
    return true;
}

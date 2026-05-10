/**
 * @file slayer3d_fused.h
 * @brief Helpers for SLAYER3D Love2D-style fused runner executables.
 */

#ifndef SLAYER3D_FUSED_H
#define SLAYER3D_FUSED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "slayer3d/asset.h"

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        SLAYER3D_FUSED_DATA_ASSET_MAX = 464,
    };

    typedef struct slayer3d_fused_pack
    {
        uint64_t pack_offset;
        uint64_t pack_size;
        char data_asset_path[SLAYER3D_FUSED_DATA_ASSET_MAX + 1];
    } slayer3d_fused_pack;

    bool slayer3d_fused_read_footer(const char *executable_path, slayer3d_fused_pack *out_pack, char *error_buffer,
                                    int error_buffer_size);

    bool slayer3d_fused_read_pack_bytes(const char *executable_path, const slayer3d_fused_pack *pack, void **out_data,
                                        size_t *out_size, char *error_buffer, int error_buffer_size);

    bool slayer3d_fused_mount_pack(slayer3d_asset_resolver *resolver, const char *executable_path,
                                   const slayer3d_fused_pack *pack, char *error_buffer, int error_buffer_size);

    bool slayer3d_fused_append_footer(const char *output_path, uint64_t pack_offset, uint64_t pack_size,
                                      const char *data_asset_path, char *error_buffer, int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_FUSED_H */

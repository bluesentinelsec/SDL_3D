#ifndef SLAYER3D_TEXTURE_H
#define SLAYER3D_TEXTURE_H

#include <stdbool.h>

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/image.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum slayer3d_texture_filter
    {
        SLAYER3D_TEXTURE_FILTER_NEAREST = 0,
        SLAYER3D_TEXTURE_FILTER_BILINEAR = 1,
        SLAYER3D_TEXTURE_FILTER_TRILINEAR = 2
    } slayer3d_texture_filter;

    typedef enum slayer3d_texture_wrap
    {
        SLAYER3D_TEXTURE_WRAP_CLAMP = 0,
        SLAYER3D_TEXTURE_WRAP_REPEAT = 1
    } slayer3d_texture_wrap;

    /*
     * Software texture owned by SLAYER3D. Pixels are RGBA8, tightly packed,
     * row-major, and stored top-down exactly like slayer3d_image.
     *
     * Defaults:
     * - filter: bilinear
     * - wrap_u / wrap_v: clamp
     *
     * When filter is SLAYER3D_TEXTURE_FILTER_TRILINEAR, a mipmap chain is
     * generated automatically. Each level is a box-filtered half-size
     * copy of the previous level, down to 1x1. The chain is stored in
     * `mip_levels` (level 0 is the base image). `mip_count` is the
     * total number of levels including the base.
     */
    typedef struct slayer3d_texture_mip_level
    {
        Uint8 *pixels;
        int width;
        int height;
    } slayer3d_texture_mip_level;

    typedef struct slayer3d_texture2d
    {
        Uint8 *pixels;
        int width;
        int height;
        slayer3d_texture_filter filter;
        slayer3d_texture_wrap wrap_u;
        slayer3d_texture_wrap wrap_v;
        slayer3d_texture_mip_level *mip_levels;
        int mip_count;
        Uint32 generation; /* process-unique value refreshed on every content or
                              parameter change; backends key cached state by
                              texture address plus generation, and unique values
                              keep recycled texture storage from aliasing */
    } slayer3d_texture2d;

    /*
     * Copy an RGBA8 image into a software texture.
     */
    bool slayer3d_create_texture_from_image(const slayer3d_image *image, slayer3d_texture2d *out);

    /*
     * Convenience helper: load an image from disk, then copy it into a
     * software texture.
     */
    bool slayer3d_load_texture_from_file(const char *path, slayer3d_texture2d *out);

    /*
     * Release texture pixels. Safe on a zero-initialized struct.
     */
    void slayer3d_free_texture(slayer3d_texture2d *texture);

    bool slayer3d_set_texture_filter(slayer3d_texture2d *texture, slayer3d_texture_filter filter);
    bool slayer3d_set_texture_wrap(slayer3d_texture2d *texture, slayer3d_texture_wrap wrap_u,
                                   slayer3d_texture_wrap wrap_v);

#ifdef __cplusplus
}
#endif

#endif

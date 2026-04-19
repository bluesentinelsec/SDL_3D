#ifndef SDL3D_TEXTURE_H
#define SDL3D_TEXTURE_H

#include <stdbool.h>

#include <SDL3/SDL_stdinc.h>

#include "sdl3d/image.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum sdl3d_texture_filter
    {
        SDL3D_TEXTURE_FILTER_NEAREST = 0,
        SDL3D_TEXTURE_FILTER_BILINEAR = 1
    } sdl3d_texture_filter;

    typedef enum sdl3d_texture_wrap
    {
        SDL3D_TEXTURE_WRAP_CLAMP = 0,
        SDL3D_TEXTURE_WRAP_REPEAT = 1
    } sdl3d_texture_wrap;

    /*
     * Software texture owned by SDL3D. Pixels are RGBA8, tightly packed,
     * row-major, and stored top-down exactly like sdl3d_image.
     *
     * Defaults:
     * - filter: bilinear
     * - wrap_u / wrap_v: clamp
     */
    typedef struct sdl3d_texture2d
    {
        Uint8 *pixels;
        int width;
        int height;
        sdl3d_texture_filter filter;
        sdl3d_texture_wrap wrap_u;
        sdl3d_texture_wrap wrap_v;
    } sdl3d_texture2d;

    /*
     * Copy an RGBA8 image into a software texture.
     */
    bool sdl3d_create_texture_from_image(const sdl3d_image *image, sdl3d_texture2d *out);

    /*
     * Convenience helper: load an image from disk, then copy it into a
     * software texture.
     */
    bool sdl3d_load_texture_from_file(const char *path, sdl3d_texture2d *out);

    /*
     * Release texture pixels. Safe on a zero-initialized struct.
     */
    void sdl3d_free_texture(sdl3d_texture2d *texture);

    bool sdl3d_set_texture_filter(sdl3d_texture2d *texture, sdl3d_texture_filter filter);
    bool sdl3d_set_texture_wrap(sdl3d_texture2d *texture, sdl3d_texture_wrap wrap_u, sdl3d_texture_wrap wrap_v);

#ifdef __cplusplus
}
#endif

#endif

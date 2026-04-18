#ifndef SDL3D_IMAGE_H
#define SDL3D_IMAGE_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL_stdinc.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * 8-bit-per-channel image owned by SDL3D. Pixels are stored row-major,
     * top-down (row 0 is the top of the image), tightly packed in RGBA
     * order so there is always exactly `width * height * 4` bytes of data
     * regardless of the channel count of the source file.
     *
     * All loaders normalize to RGBA so downstream code (texture upload,
     * screenshot write, material sampling) never has to branch on the
     * decoded layout.
     */
    typedef struct sdl3d_image
    {
        Uint8 *pixels;
        int width;
        int height;
    } sdl3d_image;

    /*
     * Load an image from disk. Returns false on failure (missing file,
     * unsupported format, allocation failure). On success `out` is fully
     * populated and the caller owns the allocation until sdl3d_free_image.
     */
    bool sdl3d_load_image_from_file(const char *path, sdl3d_image *out);

    /*
     * Decode an image already in memory. `size` is the number of bytes
     * pointed to by `data`. Same ownership rules as the file variant.
     */
    bool sdl3d_load_image_from_memory(const void *data, size_t size, sdl3d_image *out);

    /*
     * Release pixel memory held by the image. Safe to call on a zero-
     * initialized struct; clears the fields so the image cannot be freed
     * twice by accident.
     */
    void sdl3d_free_image(sdl3d_image *image);

    /*
     * Write the image to disk as a PNG. `image` must hold RGBA8 pixels
     * as produced by sdl3d_load_image_*. Returns false on I/O failure.
     */
    bool sdl3d_save_image_png(const sdl3d_image *image, const char *path);

#ifdef __cplusplus
}
#endif

#endif

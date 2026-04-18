#include "sdl3d/image.h"

#include <limits.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include "stb_image.h"
#include "stb_image_write.h"

static const int SDL3D_IMAGE_FORCED_CHANNELS = 4;

static bool sdl3d_image_populate(sdl3d_image *out, Uint8 *pixels, int width, int height)
{
    if (pixels == NULL)
    {
        return SDL_SetError("Image decode failed: %s", stbi_failure_reason());
    }
    if (width <= 0 || height <= 0)
    {
        stbi_image_free(pixels);
        return SDL_SetError("Image decode produced invalid dimensions (%dx%d).", width, height);
    }

    out->pixels = pixels;
    out->width = width;
    out->height = height;
    return true;
}

bool sdl3d_load_image_from_file(const char *path, sdl3d_image *out)
{
    if (path == NULL)
    {
        return SDL_InvalidParamError("path");
    }
    if (out == NULL)
    {
        return SDL_InvalidParamError("out");
    }

    /*
     * Route through SDL_IOStream so the loader works unchanged on
     * platforms where stb's stdio fopen cannot see bundled assets
     * (Android APK, iOS .app, Emscripten virtual FS).
     */
    size_t bytes = 0;
    void *data = SDL_LoadFile(path, &bytes);
    if (data == NULL)
    {
        /* SDL_LoadFile already populated SDL_GetError(). */
        return false;
    }

    const bool ok = sdl3d_load_image_from_memory(data, bytes, out);
    SDL_free(data);
    return ok;
}

bool sdl3d_load_image_from_memory(const void *data, size_t size, sdl3d_image *out)
{
    if (data == NULL)
    {
        return SDL_InvalidParamError("data");
    }
    if (out == NULL)
    {
        return SDL_InvalidParamError("out");
    }
    if (size == 0 || size > (size_t)INT_MAX)
    {
        return SDL_SetError("Image buffer size %llu is out of range for stbi.", (unsigned long long)size);
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    Uint8 *pixels =
        stbi_load_from_memory((const stbi_uc *)data, (int)size, &w, &h, &channels, SDL3D_IMAGE_FORCED_CHANNELS);
    return sdl3d_image_populate(out, pixels, w, h);
}

void sdl3d_free_image(sdl3d_image *image)
{
    if (image == NULL)
    {
        return;
    }
    if (image->pixels != NULL)
    {
        stbi_image_free(image->pixels);
    }
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
}

bool sdl3d_save_image_png(const sdl3d_image *image, const char *path)
{
    if (image == NULL)
    {
        return SDL_InvalidParamError("image");
    }
    if (path == NULL)
    {
        return SDL_InvalidParamError("path");
    }
    if (image->pixels == NULL || image->width <= 0 || image->height <= 0)
    {
        return SDL_SetError("sdl3d_save_image_png called with an empty image.");
    }

    const int stride_bytes = image->width * SDL3D_IMAGE_FORCED_CHANNELS;
    if (stbi_write_png(path, image->width, image->height, SDL3D_IMAGE_FORCED_CHANNELS, image->pixels, stride_bytes) ==
        0)
    {
        return SDL_SetError("stbi_write_png failed to write '%s'.", path);
    }
    return true;
}

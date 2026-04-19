#include "sdl3d/texture.h"

#include <stddef.h>

#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "texture_internal.h"

static bool sdl3d_texture_filter_valid(sdl3d_texture_filter filter)
{
    return filter == SDL3D_TEXTURE_FILTER_NEAREST || filter == SDL3D_TEXTURE_FILTER_BILINEAR;
}

static bool sdl3d_texture_wrap_valid(sdl3d_texture_wrap wrap)
{
    return wrap == SDL3D_TEXTURE_WRAP_CLAMP || wrap == SDL3D_TEXTURE_WRAP_REPEAT;
}

static float sdl3d_texture_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static char *sdl3d_texture_strdup(const char *text)
{
    const size_t length = SDL_strlen(text);
    char *copy = (char *)SDL_malloc(length + 1);
    if (copy == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    SDL_memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static bool sdl3d_texture_set_defaults(sdl3d_texture2d *texture)
{
    if (texture == NULL)
    {
        return SDL_InvalidParamError("texture");
    }

    texture->filter = SDL3D_TEXTURE_FILTER_BILINEAR;
    texture->wrap_u = SDL3D_TEXTURE_WRAP_CLAMP;
    texture->wrap_v = SDL3D_TEXTURE_WRAP_CLAMP;
    return true;
}

bool sdl3d_create_texture_from_image(const sdl3d_image *image, sdl3d_texture2d *out)
{
    size_t bytes = 0;

    if (image == NULL)
    {
        return SDL_InvalidParamError("image");
    }
    if (out == NULL)
    {
        return SDL_InvalidParamError("out");
    }
    if (image->pixels == NULL || image->width <= 0 || image->height <= 0)
    {
        return SDL_SetError("sdl3d_create_texture_from_image requires a populated RGBA8 image.");
    }

    SDL_zerop(out);
    if (!sdl3d_texture_set_defaults(out))
    {
        return false;
    }

    bytes = (size_t)image->width * (size_t)image->height * 4U;
    out->pixels = (Uint8 *)SDL_malloc(bytes);
    if (out->pixels == NULL)
    {
        SDL_zerop(out);
        return SDL_OutOfMemory();
    }

    SDL_memcpy(out->pixels, image->pixels, bytes);
    out->width = image->width;
    out->height = image->height;
    return true;
}

bool sdl3d_load_texture_from_file(const char *path, sdl3d_texture2d *out)
{
    sdl3d_image image;
    bool ok = false;

    if (path == NULL)
    {
        return SDL_InvalidParamError("path");
    }
    if (out == NULL)
    {
        return SDL_InvalidParamError("out");
    }

    SDL_zerop(&image);
    if (!sdl3d_load_image_from_file(path, &image))
    {
        return false;
    }

    ok = sdl3d_create_texture_from_image(&image, out);
    sdl3d_free_image(&image);
    return ok;
}

void sdl3d_free_texture(sdl3d_texture2d *texture)
{
    if (texture == NULL)
    {
        return;
    }

    SDL_free(texture->pixels);
    SDL_zerop(texture);
}

bool sdl3d_set_texture_filter(sdl3d_texture2d *texture, sdl3d_texture_filter filter)
{
    if (texture == NULL)
    {
        return SDL_InvalidParamError("texture");
    }
    if (!sdl3d_texture_filter_valid(filter))
    {
        return SDL_SetError("Unknown texture filter value: %d.", (int)filter);
    }

    texture->filter = filter;
    return true;
}

bool sdl3d_set_texture_wrap(sdl3d_texture2d *texture, sdl3d_texture_wrap wrap_u, sdl3d_texture_wrap wrap_v)
{
    if (texture == NULL)
    {
        return SDL_InvalidParamError("texture");
    }
    if (!sdl3d_texture_wrap_valid(wrap_u) || !sdl3d_texture_wrap_valid(wrap_v))
    {
        return SDL_SetError("Unknown texture wrap value: (%d, %d).", (int)wrap_u, (int)wrap_v);
    }

    texture->wrap_u = wrap_u;
    texture->wrap_v = wrap_v;
    return true;
}

static float sdl3d_texture_clamp01(float value)
{
    if (value <= 0.0f)
    {
        return 0.0f;
    }
    if (value >= 1.0f)
    {
        return 1.0f;
    }
    return value;
}

static float sdl3d_texture_wrap_coord(float value, sdl3d_texture_wrap wrap)
{
    if (wrap == SDL3D_TEXTURE_WRAP_REPEAT)
    {
        const float floored = SDL_floorf(value);
        value -= floored;
        if (value < 0.0f)
        {
            value += 1.0f;
        }
        return value;
    }

    return sdl3d_texture_clamp01(value);
}

static int sdl3d_texture_resolve_index(int coord, int extent, sdl3d_texture_wrap wrap)
{
    if (extent <= 0)
    {
        return 0;
    }

    if (wrap == SDL3D_TEXTURE_WRAP_REPEAT)
    {
        int wrapped = coord % extent;
        if (wrapped < 0)
        {
            wrapped += extent;
        }
        return wrapped;
    }

    if (coord < 0)
    {
        return 0;
    }
    if (coord >= extent)
    {
        return extent - 1;
    }
    return coord;
}

static void sdl3d_texture_fetch_rgba(const sdl3d_texture2d *texture, int x, int y, float *out_r, float *out_g,
                                     float *out_b, float *out_a)
{
    const int ix = sdl3d_texture_resolve_index(x, texture->width, texture->wrap_u);
    const int iy = sdl3d_texture_resolve_index(y, texture->height, texture->wrap_v);
    const Uint8 *pixel = &texture->pixels[((iy * texture->width) + ix) * 4];

    *out_r = (float)pixel[0] / 255.0f;
    *out_g = (float)pixel[1] / 255.0f;
    *out_b = (float)pixel[2] / 255.0f;
    *out_a = (float)pixel[3] / 255.0f;
}

void sdl3d_texture_sample_rgba(const sdl3d_texture2d *texture, float u, float v, float *out_r, float *out_g,
                               float *out_b, float *out_a)
{
    float sample_u;
    float sample_v;
    float texel_x;
    float texel_y;

    SDL_assert(texture != NULL);
    SDL_assert(texture->pixels != NULL);
    SDL_assert(texture->width > 0);
    SDL_assert(texture->height > 0);
    SDL_assert(out_r != NULL);
    SDL_assert(out_g != NULL);
    SDL_assert(out_b != NULL);
    SDL_assert(out_a != NULL);

    sample_u = sdl3d_texture_wrap_coord(u, texture->wrap_u);
    sample_v = sdl3d_texture_wrap_coord(v, texture->wrap_v);

    /*
     * Texture coordinates follow the common 3D convention where (0, 0) is
     * the lower-left corner of the texture. Pixels are stored top-down, so
     * the Y mapping flips here.
     */
    texel_x = (sample_u * (float)texture->width) - 0.5f;
    texel_y = ((1.0f - sample_v) * (float)texture->height) - 0.5f;

    if (texture->filter == SDL3D_TEXTURE_FILTER_NEAREST)
    {
        const int ix = (int)SDL_floorf(texel_x + 0.5f);
        const int iy = (int)SDL_floorf(texel_y + 0.5f);
        sdl3d_texture_fetch_rgba(texture, ix, iy, out_r, out_g, out_b, out_a);
        return;
    }

    const int x0 = (int)SDL_floorf(texel_x);
    const int y0 = (int)SDL_floorf(texel_y);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float tx = texel_x - (float)x0;
    const float ty = texel_y - (float)y0;

    float c00_r = 0.0f, c00_g = 0.0f, c00_b = 0.0f, c00_a = 0.0f;
    float c10_r = 0.0f, c10_g = 0.0f, c10_b = 0.0f, c10_a = 0.0f;
    float c01_r = 0.0f, c01_g = 0.0f, c01_b = 0.0f, c01_a = 0.0f;
    float c11_r = 0.0f, c11_g = 0.0f, c11_b = 0.0f, c11_a = 0.0f;

    sdl3d_texture_fetch_rgba(texture, x0, y0, &c00_r, &c00_g, &c00_b, &c00_a);
    sdl3d_texture_fetch_rgba(texture, x1, y0, &c10_r, &c10_g, &c10_b, &c10_a);
    sdl3d_texture_fetch_rgba(texture, x0, y1, &c01_r, &c01_g, &c01_b, &c01_a);
    sdl3d_texture_fetch_rgba(texture, x1, y1, &c11_r, &c11_g, &c11_b, &c11_a);

    *out_r = sdl3d_texture_lerp(sdl3d_texture_lerp(c00_r, c10_r, tx), sdl3d_texture_lerp(c01_r, c11_r, tx), ty);
    *out_g = sdl3d_texture_lerp(sdl3d_texture_lerp(c00_g, c10_g, tx), sdl3d_texture_lerp(c01_g, c11_g, tx), ty);
    *out_b = sdl3d_texture_lerp(sdl3d_texture_lerp(c00_b, c10_b, tx), sdl3d_texture_lerp(c01_b, c11_b, tx), ty);
    *out_a = sdl3d_texture_lerp(sdl3d_texture_lerp(c00_a, c10_a, tx), sdl3d_texture_lerp(c01_a, c11_a, tx), ty);
}

static bool sdl3d_texture_path_is_absolute(const char *path)
{
    const char c0 = path[0];
    const char c1 = path[1];
    const char c2 = path[2];

    if (c0 == '/' || c0 == '\\')
    {
        return true;
    }

    if (((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z')) && c1 == ':' && (c2 == '/' || c2 == '\\'))
    {
        return true;
    }

    return false;
}

static char *sdl3d_texture_dirname_dup(const char *path)
{
    const char *slash = NULL;

    for (const char *cursor = path; *cursor != '\0'; ++cursor)
    {
        if (*cursor == '/' || *cursor == '\\')
        {
            slash = cursor;
        }
    }

    if (slash == NULL)
    {
        char *empty = (char *)SDL_malloc(1);
        if (empty == NULL)
        {
            SDL_OutOfMemory();
            return NULL;
        }
        empty[0] = '\0';
        return empty;
    }

    const size_t length = (size_t)(slash - path + 1);
    char *dirname = (char *)SDL_malloc(length + 1);
    if (dirname == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    SDL_memcpy(dirname, path, length);
    dirname[length] = '\0';
    return dirname;
}

static bool sdl3d_texture_resolve_path(const char *source_path, const char *texture_path, char **out_path)
{
    char *dirname = NULL;
    size_t dirname_length = 0;
    size_t texture_length = 0;
    char *resolved = NULL;

    if (texture_path == NULL)
    {
        return SDL_InvalidParamError("texture_path");
    }
    if (out_path == NULL)
    {
        return SDL_InvalidParamError("out_path");
    }

    if (sdl3d_texture_path_is_absolute(texture_path))
    {
        resolved = sdl3d_texture_strdup(texture_path);
        if (resolved == NULL)
        {
            return false;
        }
        *out_path = resolved;
        return true;
    }

    if (source_path == NULL || *source_path == '\0')
    {
        return SDL_SetError("Cannot resolve relative texture path '%s' without a model source path.", texture_path);
    }

    dirname = sdl3d_texture_dirname_dup(source_path);
    if (dirname == NULL)
    {
        return false;
    }

    dirname_length = SDL_strlen(dirname);
    texture_length = SDL_strlen(texture_path);
    resolved = (char *)SDL_malloc(dirname_length + texture_length + 1);
    if (resolved == NULL)
    {
        SDL_free(dirname);
        return SDL_OutOfMemory();
    }

    SDL_memcpy(resolved, dirname, dirname_length);
    SDL_memcpy(resolved + dirname_length, texture_path, texture_length);
    resolved[dirname_length + texture_length] = '\0';

    SDL_free(dirname);
    *out_path = resolved;
    return true;
}

void sdl3d_texture_cache_destroy(sdl3d_texture_cache_entry *cache)
{
    while (cache != NULL)
    {
        sdl3d_texture_cache_entry *next = cache->next;
        SDL_free(cache->path);
        sdl3d_free_texture(&cache->texture);
        SDL_free(cache);
        cache = next;
    }
}

bool sdl3d_texture_cache_get_or_load(sdl3d_texture_cache_entry **cache, const char *source_path,
                                     const char *texture_path, const sdl3d_texture2d **out_texture)
{
    sdl3d_texture_cache_entry *entry = NULL;
    char *resolved_path = NULL;

    if (cache == NULL)
    {
        return SDL_InvalidParamError("cache");
    }
    if (texture_path == NULL)
    {
        return SDL_InvalidParamError("texture_path");
    }
    if (out_texture == NULL)
    {
        return SDL_InvalidParamError("out_texture");
    }

    if (!sdl3d_texture_resolve_path(source_path, texture_path, &resolved_path))
    {
        return false;
    }

    for (entry = *cache; entry != NULL; entry = entry->next)
    {
        if (SDL_strcmp(entry->path, resolved_path) == 0)
        {
            SDL_free(resolved_path);
            *out_texture = &entry->texture;
            return true;
        }
    }

    entry = (sdl3d_texture_cache_entry *)SDL_calloc(1, sizeof(*entry));
    if (entry == NULL)
    {
        SDL_free(resolved_path);
        return SDL_OutOfMemory();
    }

    if (!sdl3d_load_texture_from_file(resolved_path, &entry->texture))
    {
        SDL_free(entry);
        SDL_free(resolved_path);
        return false;
    }

    entry->path = resolved_path;
    entry->next = *cache;
    *cache = entry;
    *out_texture = &entry->texture;
    return true;
}

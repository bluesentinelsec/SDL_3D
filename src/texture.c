#include "sdl3d/texture.h"

#include <stddef.h>

#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "texture_internal.h"

static bool sdl3d_texture_filter_valid(sdl3d_texture_filter filter)
{
    return filter == SDL3D_TEXTURE_FILTER_NEAREST || filter == SDL3D_TEXTURE_FILTER_BILINEAR ||
           filter == SDL3D_TEXTURE_FILTER_TRILINEAR;
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
    texture->mip_levels = NULL;
    texture->mip_count = 0;
    return true;
}

static void sdl3d_texture_free_mips(sdl3d_texture2d *texture)
{
    if (texture->mip_levels == NULL)
    {
        return;
    }
    /* Level 0 pixels alias texture->pixels — do not free separately. */
    for (int i = 1; i < texture->mip_count; ++i)
    {
        SDL_free(texture->mip_levels[i].pixels);
    }
    SDL_free(texture->mip_levels);
    texture->mip_levels = NULL;
    texture->mip_count = 0;
}

static int sdl3d_texture_compute_mip_count(int width, int height)
{
    int levels = 1;
    int w = width;
    int h = height;
    while (w > 1 || h > 1)
    {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        ++levels;
    }
    return levels;
}

static bool sdl3d_texture_generate_mips(sdl3d_texture2d *texture)
{
    const int count = sdl3d_texture_compute_mip_count(texture->width, texture->height);
    sdl3d_texture_mip_level *levels =
        (sdl3d_texture_mip_level *)SDL_calloc((size_t)count, sizeof(sdl3d_texture_mip_level));
    if (levels == NULL)
    {
        return SDL_OutOfMemory();
    }

    levels[0].pixels = texture->pixels;
    levels[0].width = texture->width;
    levels[0].height = texture->height;

    for (int i = 1; i < count; ++i)
    {
        const sdl3d_texture_mip_level *prev = &levels[i - 1];
        const int pw = prev->width;
        const int ph = prev->height;
        const int w = pw > 1 ? pw / 2 : 1;
        const int h = ph > 1 ? ph / 2 : 1;
        const size_t bytes = (size_t)w * (size_t)h * 4U;
        Uint8 *pixels = (Uint8 *)SDL_malloc(bytes);
        if (pixels == NULL)
        {
            /* Clean up already-allocated levels. */
            for (int j = 1; j < i; ++j)
            {
                SDL_free(levels[j].pixels);
            }
            SDL_free(levels);
            return SDL_OutOfMemory();
        }

        /* Box filter: average 2x2 blocks from the previous level. When a
         * dimension is already 1, the "block" collapses to fewer samples. */
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const int sx = x * 2;
                const int sy = y * 2;
                const int sx1 = (sx + 1 < pw) ? sx + 1 : sx;
                const int sy1 = (sy + 1 < ph) ? sy + 1 : sy;

                const Uint8 *p00 = &prev->pixels[(sy * pw + sx) * 4];
                const Uint8 *p10 = &prev->pixels[(sy * pw + sx1) * 4];
                const Uint8 *p01 = &prev->pixels[(sy1 * pw + sx) * 4];
                const Uint8 *p11 = &prev->pixels[(sy1 * pw + sx1) * 4];

                Uint8 *dst = &pixels[(y * w + x) * 4];
                for (int c = 0; c < 4; ++c)
                {
                    dst[c] =
                        (Uint8)(((unsigned)p00[c] + (unsigned)p10[c] + (unsigned)p01[c] + (unsigned)p11[c] + 2U) / 4U);
                }
            }
        }

        levels[i].pixels = pixels;
        levels[i].width = w;
        levels[i].height = h;
    }

    texture->mip_levels = levels;
    texture->mip_count = count;
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
    out->generation++;
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

    Uint32 next_gen = texture->generation + 1;
    sdl3d_texture_free_mips(texture);
    SDL_free(texture->pixels);
    SDL_zerop(texture);
    texture->generation = next_gen;
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
    texture->generation++;

    if (filter == SDL3D_TEXTURE_FILTER_TRILINEAR && texture->mip_levels == NULL && texture->pixels != NULL)
    {
        if (!sdl3d_texture_generate_mips(texture))
        {
            return false;
        }
    }
    else if (filter != SDL3D_TEXTURE_FILTER_TRILINEAR)
    {
        sdl3d_texture_free_mips(texture);
    }

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
    texture->generation++;
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

static void sdl3d_texture_fetch_rgba_mip(const sdl3d_texture_mip_level *mip, int x, int y, sdl3d_texture_wrap wrap_u,
                                         sdl3d_texture_wrap wrap_v, float *out_r, float *out_g, float *out_b,
                                         float *out_a)
{
    const int ix = sdl3d_texture_resolve_index(x, mip->width, wrap_u);
    const int iy = sdl3d_texture_resolve_index(y, mip->height, wrap_v);
    const Uint8 *pixel = &mip->pixels[((iy * mip->width) + ix) * 4];

    *out_r = (float)pixel[0] / 255.0f;
    *out_g = (float)pixel[1] / 255.0f;
    *out_b = (float)pixel[2] / 255.0f;
    *out_a = (float)pixel[3] / 255.0f;
}

static void sdl3d_texture_sample_bilinear_mip(const sdl3d_texture_mip_level *mip, sdl3d_texture_wrap wrap_u,
                                              sdl3d_texture_wrap wrap_v, float u, float v, float *out_r, float *out_g,
                                              float *out_b, float *out_a)
{
    const float sample_u = sdl3d_texture_wrap_coord(u, wrap_u);
    const float sample_v = sdl3d_texture_wrap_coord(v, wrap_v);
    const float texel_x = (sample_u * (float)mip->width) - 0.5f;
    const float texel_y = (sample_v * (float)mip->height) - 0.5f;

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

    sdl3d_texture_fetch_rgba_mip(mip, x0, y0, wrap_u, wrap_v, &c00_r, &c00_g, &c00_b, &c00_a);
    sdl3d_texture_fetch_rgba_mip(mip, x1, y0, wrap_u, wrap_v, &c10_r, &c10_g, &c10_b, &c10_a);
    sdl3d_texture_fetch_rgba_mip(mip, x0, y1, wrap_u, wrap_v, &c01_r, &c01_g, &c01_b, &c01_a);
    sdl3d_texture_fetch_rgba_mip(mip, x1, y1, wrap_u, wrap_v, &c11_r, &c11_g, &c11_b, &c11_a);

    *out_r = sdl3d_texture_lerp(sdl3d_texture_lerp(c00_r, c10_r, tx), sdl3d_texture_lerp(c01_r, c11_r, tx), ty);
    *out_g = sdl3d_texture_lerp(sdl3d_texture_lerp(c00_g, c10_g, tx), sdl3d_texture_lerp(c01_g, c11_g, tx), ty);
    *out_b = sdl3d_texture_lerp(sdl3d_texture_lerp(c00_b, c10_b, tx), sdl3d_texture_lerp(c01_b, c11_b, tx), ty);
    *out_a = sdl3d_texture_lerp(sdl3d_texture_lerp(c00_a, c10_a, tx), sdl3d_texture_lerp(c01_a, c11_a, tx), ty);
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

void sdl3d_texture_sample_rgba(const sdl3d_texture2d *texture, float u, float v, float lod, float *out_r, float *out_g,
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

    /* Trilinear: bilinear-sample two adjacent mip levels and lerp. */
    if (texture->filter == SDL3D_TEXTURE_FILTER_TRILINEAR && texture->mip_levels != NULL && texture->mip_count > 1)
    {
        float clamped_lod = lod;
        float level_f;
        int level0;
        int level1;
        float frac;

        if (clamped_lod < 0.0f)
        {
            clamped_lod = 0.0f;
        }
        if (clamped_lod > (float)(texture->mip_count - 1))
        {
            clamped_lod = (float)(texture->mip_count - 1);
        }

        level_f = clamped_lod;
        level0 = (int)SDL_floorf(level_f);
        level1 = level0 + 1;
        frac = level_f - (float)level0;

        if (level1 >= texture->mip_count)
        {
            level1 = texture->mip_count - 1;
            frac = 0.0f;
        }

        if (frac <= 0.0f)
        {
            sdl3d_texture_sample_bilinear_mip(&texture->mip_levels[level0], texture->wrap_u, texture->wrap_v, u, v,
                                              out_r, out_g, out_b, out_a);
        }
        else
        {
            float r0, g0, b0, a0;
            float r1, g1, b1, a1;
            sdl3d_texture_sample_bilinear_mip(&texture->mip_levels[level0], texture->wrap_u, texture->wrap_v, u, v, &r0,
                                              &g0, &b0, &a0);
            sdl3d_texture_sample_bilinear_mip(&texture->mip_levels[level1], texture->wrap_u, texture->wrap_v, u, v, &r1,
                                              &g1, &b1, &a1);
            *out_r = sdl3d_texture_lerp(r0, r1, frac);
            *out_g = sdl3d_texture_lerp(g0, g1, frac);
            *out_b = sdl3d_texture_lerp(b0, b1, frac);
            *out_a = sdl3d_texture_lerp(a0, a1, frac);
        }
        return;
    }

    sample_u = sdl3d_texture_wrap_coord(u, texture->wrap_u);
    sample_v = sdl3d_texture_wrap_coord(v, texture->wrap_v);

    /*
     * Texture coordinates follow the common 3D convention where (0, 0) is
     * the lower-left corner of the texture. Pixels are stored top-down, so
     * the Y mapping flips here.
     */
    texel_x = (sample_u * (float)texture->width) - 0.5f;
    texel_y = (sample_v * (float)texture->height) - 0.5f;

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

    /* Default to REPEAT wrapping to match the GL renderer's GL_REPEAT. */
    entry->texture.wrap_u = SDL3D_TEXTURE_WRAP_REPEAT;
    entry->texture.wrap_v = SDL3D_TEXTURE_WRAP_REPEAT;

    entry->path = resolved_path;
    entry->next = *cache;
    *cache = entry;
    *out_texture = &entry->texture;
    return true;
}

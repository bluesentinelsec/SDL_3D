/*
 * Font system implementation using stb_truetype.
 */

#include "sdl3d/font.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <stb_truetype.h>

#include "sdl3d/drawing3d.h"
#include "sdl3d/render_context.h"
#include "sdl3d/texture.h"

/* ------------------------------------------------------------------ */
/* Font loading                                                        */
/* ------------------------------------------------------------------ */

bool sdl3d_load_font_from_memory(const void *data, int data_size, float pixel_size, sdl3d_font *out)
{
    stbtt_fontinfo info;
    int atlas_w = 512, atlas_h = 512;
    unsigned char *atlas = NULL;
    float scale;
    int ascent, descent, line_gap;

    if (!data || data_size <= 0 || !out)
    {
        return SDL_InvalidParamError("data");
    }

    SDL_zerop(out);

    if (!stbtt_InitFont(&info, (const unsigned char *)data, 0))
    {
        return SDL_SetError("stb_truetype: failed to init font");
    }

    scale = stbtt_ScaleForPixelHeight(&info, pixel_size);
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    out->size = pixel_size;
    out->ascent = (float)ascent * scale;
    out->descent = (float)descent * scale;
    out->line_gap = (float)line_gap * scale;

    /* Allocate atlas. */
    atlas = (unsigned char *)SDL_calloc(1, (size_t)(atlas_w * atlas_h));
    if (!atlas)
    {
        return SDL_OutOfMemory();
    }

    /* Pack glyphs into atlas. */
    {
        stbtt_pack_context pc;
        stbtt_packedchar packed[SDL3D_FONT_CHAR_COUNT];

        if (!stbtt_PackBegin(&pc, atlas, atlas_w, atlas_h, 0, 1, NULL))
        {
            SDL_free(atlas);
            return SDL_SetError("stb_truetype: PackBegin failed");
        }

        stbtt_PackSetOversampling(&pc, 2, 2);
        stbtt_PackFontRange(&pc, (const unsigned char *)data, 0, pixel_size, SDL3D_FONT_FIRST_CHAR,
                            SDL3D_FONT_CHAR_COUNT, packed);
        stbtt_PackEnd(&pc);

        /* Convert packed chars to our glyph format. */
        for (int i = 0; i < SDL3D_FONT_CHAR_COUNT; i++)
        {
            sdl3d_glyph *g = &out->glyphs[i];
            g->x0 = (float)packed[i].x0;
            g->y0 = (float)packed[i].y0;
            g->x1 = (float)packed[i].x1;
            g->y1 = (float)packed[i].y1;
            g->u0 = g->x0 / (float)atlas_w;
            g->v0 = g->y0 / (float)atlas_h;
            g->u1 = g->x1 / (float)atlas_w;
            g->v1 = g->y1 / (float)atlas_h;
            g->xoff = packed[i].xoff;
            g->yoff = packed[i].yoff;
            g->xadvance = packed[i].xadvance;
        }
    }

    out->atlas_pixels = atlas;
    out->atlas_w = atlas_w;
    out->atlas_h = atlas_h;

    SDL_Log("SDL3D font: %.0fpx, atlas %dx%d, %d glyphs", pixel_size, atlas_w, atlas_h, SDL3D_FONT_CHAR_COUNT);
    return true;
}

bool sdl3d_load_font(const char *path, float pixel_size, sdl3d_font *out)
{
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    bool ok;

    if (!data)
    {
        return SDL_SetError("Failed to load font file '%s'", path);
    }

    ok = sdl3d_load_font_from_memory(data, (int)size, pixel_size, out);
    SDL_free(data);
    return ok;
}

void sdl3d_free_font(sdl3d_font *font)
{
    if (font)
    {
        SDL_free(font->atlas_pixels);
        font->atlas_pixels = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Text measurement                                                    */
/* ------------------------------------------------------------------ */

void sdl3d_measure_text(const sdl3d_font *font, const char *text, float *out_width, float *out_height)
{
    float x = 0, max_x = 0;
    float line_h = font->ascent - font->descent + font->line_gap;
    int lines = 1;

    if (!font || !text)
    {
        if (out_width)
            *out_width = 0;
        if (out_height)
            *out_height = 0;
        return;
    }

    for (const char *p = text; *p; p++)
    {
        if (*p == '\n')
        {
            if (x > max_x)
                max_x = x;
            x = 0;
            lines++;
            continue;
        }
        int ci = (int)*p - SDL3D_FONT_FIRST_CHAR;
        if (ci >= 0 && ci < SDL3D_FONT_CHAR_COUNT)
        {
            x += font->glyphs[ci].xadvance;
        }
    }
    if (x > max_x)
        max_x = x;

    if (out_width)
        *out_width = max_x;
    if (out_height)
        *out_height = (float)lines * line_h;
}

/* ------------------------------------------------------------------ */
/* Text rendering                                                      */
/* ------------------------------------------------------------------ */

bool sdl3d_draw_text(struct sdl3d_render_context *context, const sdl3d_font *font, const char *text, float x, float y,
                     sdl3d_color color)
{
    float cursor_x = x;
    float cursor_y = y + font->ascent;
    float line_h = font->ascent - font->descent + font->line_gap;
    int ctx_w, ctx_h;
    sdl3d_texture2d tex;
    unsigned char *rgba = NULL;

    if (!context || !font || !text || !font->atlas_pixels)
    {
        return SDL_InvalidParamError("context");
    }

    ctx_w = sdl3d_get_render_context_width(context);
    ctx_h = sdl3d_get_render_context_height(context);
    if (ctx_w <= 0 || ctx_h <= 0)
    {
        return SDL_SetError("Invalid render context dimensions");
    }

    /* Convert single-channel atlas to RGBA for the texture system. */
    rgba = (unsigned char *)SDL_malloc((size_t)(font->atlas_w * font->atlas_h * 4));
    if (!rgba)
    {
        return SDL_OutOfMemory();
    }
    for (int i = 0; i < font->atlas_w * font->atlas_h; i++)
    {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = font->atlas_pixels[i];
    }

    SDL_zerop(&tex);
    tex.width = font->atlas_w;
    tex.height = font->atlas_h;
    tex.pixels = rgba;
    tex.filter = SDL3D_TEXTURE_FILTER_BILINEAR;
    tex.wrap_u = SDL3D_TEXTURE_WRAP_CLAMP;
    tex.wrap_v = SDL3D_TEXTURE_WRAP_CLAMP;

    /* Draw each character as a textured quad in screen space. */
    for (const char *p = text; *p; p++)
    {
        if (*p == '\n')
        {
            cursor_x = x;
            cursor_y += line_h;
            continue;
        }

        int ci = (int)*p - SDL3D_FONT_FIRST_CHAR;
        if (ci < 0 || ci >= SDL3D_FONT_CHAR_COUNT)
        {
            continue;
        }

        const sdl3d_glyph *g = &font->glyphs[ci];
        float gw = g->x1 - g->x0;
        float gh = g->y1 - g->y0;
        if (gw <= 0 || gh <= 0)
        {
            cursor_x += g->xadvance;
            continue;
        }

        /* Screen-space quad corners in NDC. */
        float px0 = (cursor_x + g->xoff) / (float)ctx_w * 2.0f - 1.0f;
        float py0 = 1.0f - (cursor_y + g->yoff) / (float)ctx_h * 2.0f;
        float px1 = (cursor_x + g->xoff + gw) / (float)ctx_w * 2.0f - 1.0f;
        float py1 = 1.0f - (cursor_y + g->yoff + gh) / (float)ctx_h * 2.0f;

        /* Draw as two triangles via sdl3d_draw_triangle_3d or the mesh path.
         * For simplicity, use the unlit textured mesh path with an identity MVP. */
        {
            float positions[18] = {px0, py0, 0, px1, py0, 0, px1, py1, 0, px0, py0, 0, px1, py1, 0, px0, py1, 0};
            float uvs[12] = {g->u0, g->v0, g->u1, g->v0, g->u1, g->v1, g->u0, g->v0, g->u1, g->v1, g->u0, g->v1};
            sdl3d_mesh mesh;
            SDL_zerop(&mesh);
            mesh.positions = positions;
            mesh.uvs = uvs;
            mesh.vertex_count = 6;
            sdl3d_draw_mesh(context, &mesh, &tex, color);
        }

        cursor_x += g->xadvance;
    }

    SDL_free(rgba);
    return true;
}

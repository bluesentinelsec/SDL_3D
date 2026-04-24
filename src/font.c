/*
 * Font system implementation using stb_truetype.
 */

#include "sdl3d/font.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <stb_truetype.h>

#include "render_context_internal.h"
#include "sdl3d/camera.h"
#include "sdl3d/drawing3d.h"
#include "sdl3d/lighting.h"
#include "sdl3d/render_context.h"
#include "sdl3d/texture.h"

/* ------------------------------------------------------------------ */
/* Font loading                                                        */
/* ------------------------------------------------------------------ */

bool sdl3d_load_font_from_memory(const void *data, int data_size, float pixel_size, sdl3d_font *out)
{
    stbtt_fontinfo info;
    int atlas_w = 512, atlas_h = 512;
    Uint32 prev_generation = 0;

    /* Scale the atlas so all 95 ASCII glyphs fit at any pixel size.
     * With 2× oversampling each glyph cell is roughly (pixel_size*2)².
     * 512×512 is fine up to ~32px; beyond that we step up. */
    if (pixel_size > 64.0f)
    {
        atlas_w = 2048;
        atlas_h = 2048;
    }
    else if (pixel_size > 32.0f)
    {
        atlas_w = 1024;
        atlas_h = 1024;
    }
    unsigned char *atlas = NULL;
    unsigned char *rgba = NULL;
    float scale;
    int ascent, descent, line_gap;

    if (!data || data_size <= 0 || !out)
    {
        return SDL_InvalidParamError("data");
    }

    prev_generation = out->atlas_texture.generation;
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

    atlas = (unsigned char *)SDL_calloc(1, (size_t)(atlas_w * atlas_h));
    if (!atlas)
    {
        return SDL_OutOfMemory();
    }

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

        for (int i = 0; i < SDL3D_FONT_CHAR_COUNT; i++)
        {
            sdl3d_glyph *g = &out->glyphs[i];
            g->u0 = (float)packed[i].x0 / (float)atlas_w;
            g->v0 = (float)packed[i].y0 / (float)atlas_h;
            g->u1 = (float)packed[i].x1 / (float)atlas_w;
            g->v1 = (float)packed[i].y1 / (float)atlas_h;
            g->xoff = packed[i].xoff;
            g->yoff = packed[i].yoff;
            g->xoff2 = packed[i].xoff2;
            g->yoff2 = packed[i].yoff2;
            g->xadvance = packed[i].xadvance;
        }
    }

    /* Expand single-channel atlas to RGBA so it can flow through the
     * texture path. RGB stays white, alpha carries glyph coverage. */
    rgba = (unsigned char *)SDL_malloc((size_t)(atlas_w * atlas_h * 4));
    if (!rgba)
    {
        SDL_free(atlas);
        return SDL_OutOfMemory();
    }
    for (int i = 0; i < atlas_w * atlas_h; i++)
    {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = atlas[i];
    }

    out->atlas_pixels = atlas;
    out->atlas_w = atlas_w;
    out->atlas_h = atlas_h;

    SDL_zero(out->atlas_texture);
    out->atlas_texture.pixels = rgba;
    out->atlas_texture.width = atlas_w;
    out->atlas_texture.height = atlas_h;
    out->atlas_texture.filter = SDL3D_TEXTURE_FILTER_BILINEAR;
    out->atlas_texture.wrap_u = SDL3D_TEXTURE_WRAP_CLAMP;
    out->atlas_texture.wrap_v = SDL3D_TEXTURE_WRAP_CLAMP;
    out->atlas_texture.generation = prev_generation ? prev_generation + 1U : 1U;

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
        sdl3d_free_texture(&font->atlas_texture);
        SDL_zero(font->glyphs);
        font->atlas_w = 0;
        font->atlas_h = 0;
        font->size = 0.0f;
        font->ascent = 0.0f;
        font->descent = 0.0f;
        font->line_gap = 0.0f;
    }
}

/* ------------------------------------------------------------------ */
/* Text measurement                                                    */
/* ------------------------------------------------------------------ */

void sdl3d_measure_text(const sdl3d_font *font, const char *text, float *out_width, float *out_height)
{
    float x = 0, max_x = 0;
    int lines = 1;

    if (!font || !text)
    {
        if (out_width)
            *out_width = 0;
        if (out_height)
            *out_height = 0;
        return;
    }

    float line_h = font->ascent - font->descent + font->line_gap;

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

/*
 * Text is drawn as a second orthographic 3D pass layered on top of the
 * scene. The caller must be OUTSIDE begin_mode_3d / end_mode_3d.
 *
 * Pixel coordinates (x, y) follow screen convention: (0, 0) is the
 * top-left of the backbuffer, y grows downward. The ortho camera maps
 * world X = [-cw/2, cw/2], world Y = [-ch/2, ch/2] to the full screen,
 * and we feed glyph vertices in world-space = (sx - cw/2, ch/2 - sy).
 *
 * To guarantee text sits on top of the 3D scene, glyph quads are emitted
 * at z = 0 with the camera at z = 1 and default near plane 0.01, which
 * puts them at NDC z ≈ -1 (almost at the near plane). Depth test is
 * LEQUAL in the GL backend, so the text always wins.
 *
 * `digit_cell_width` lets callers force monospaced digit layout: when > 0,
 * every '0'..'9' glyph advances by that amount regardless of its own
 * proportional xadvance, and is centered inside the cell. Non-digit
 * characters use their native advance.
 */
static bool draw_text_internal(struct sdl3d_render_context *context, const sdl3d_font *font, const char *text, float x,
                               float y, sdl3d_color color, float digit_cell_width)
{
    int ctx_w, ctx_h;
    sdl3d_camera3d ortho;
    sdl3d_shading_mode prev_shading;
    bool prev_culling;
    sdl3d_mat4 saved_view, saved_projection, saved_view_projection;
    bool ok = true;

    if (!context || !font || !text)
    {
        return SDL_InvalidParamError("context");
    }
    if (font->atlas_texture.pixels == NULL)
    {
        return SDL_SetError("sdl3d_draw_text: font atlas not initialized");
    }
    if (sdl3d_is_in_mode_3d(context))
    {
        return SDL_SetError("sdl3d_draw_text must be called outside sdl3d_begin_mode_3d / sdl3d_end_mode_3d");
    }

    ctx_w = sdl3d_get_render_context_width(context);
    ctx_h = sdl3d_get_render_context_height(context);
    if (ctx_w <= 0 || ctx_h <= 0)
    {
        return SDL_SetError("Invalid render context dimensions");
    }

    ortho.position = sdl3d_vec3_make(0.0f, 0.0f, 1.0f);
    ortho.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    ortho.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    ortho.fovy = (float)ctx_h;
    ortho.projection = SDL3D_CAMERA_ORTHOGRAPHIC;

    prev_shading = sdl3d_get_shading_mode(context);
    prev_culling = sdl3d_is_backface_culling_enabled(context);
    /* Preserve the scene's view/projection so that deferred backends
     * (e.g., GL replay) see the camera that rendered the scene, not the
     * ortho overlay camera. Only per-draw MVPs need the ortho matrix,
     * which the in-flight entries already captured. */
    saved_view = context->view;
    saved_projection = context->projection;
    saved_view_projection = context->view_projection;

    sdl3d_set_shading_mode(context, SDL3D_SHADING_UNLIT);
    sdl3d_set_backface_culling_enabled(context, false);

    if (!sdl3d_begin_mode_3d(context, ortho))
    {
        sdl3d_set_shading_mode(context, prev_shading);
        sdl3d_set_backface_culling_enabled(context, prev_culling);
        return false;
    }

    float cursor_x = x;
    float cursor_y = y + font->ascent;
    float line_h = font->ascent - font->descent + font->line_gap;
    float hx = (float)ctx_w * 0.5f;
    float hy = (float)ctx_h * 0.5f;

    for (const char *p = text; *p && ok; p++)
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
        /* Treat digits and spaces as monospace cells when the caller opts
         * in. Spaces need the same treatment so a right-aligned number
         * like "  60" occupies the same width as "120". */
        bool is_mono_cell = digit_cell_width > 0.0f && ((*p >= '0' && *p <= '9') || *p == ' ');
        float advance = is_mono_cell ? digit_cell_width : g->xadvance;
        float glyph_shift = is_mono_cell ? (digit_cell_width - g->xadvance) * 0.5f : 0.0f;

        float gw = g->xoff2 - g->xoff;
        float gh = g->yoff2 - g->yoff;
        if (gw <= 0 || gh <= 0)
        {
            cursor_x += advance;
            continue;
        }

        /* Screen-space pixel rect for this glyph. */
        float sx0 = cursor_x + glyph_shift + g->xoff;
        float sy0 = cursor_y + g->yoff;
        float sx1 = cursor_x + glyph_shift + g->xoff2;
        float sy1 = cursor_y + g->yoff2;

        /* Map screen pixels to ortho world space (Y flipped). */
        float wx0 = sx0 - hx;
        float wx1 = sx1 - hx;
        float wy0 = hy - sy0; /* top of glyph (larger y in world) */
        float wy1 = hy - sy1; /* bottom of glyph */

        /* Wind CCW when viewed from +Z (the ortho camera looks down -Z),
         * so the quad is front-facing under the scene's standard CCW
         * front-face / cull-back policy. The GL backend applies culling
         * once at present time based on whatever flag is current then,
         * so we can't just toggle culling around this block — the quads
         * must be authored front-facing. */
        float positions[18] = {
            wx0, wy0, 0.0f, wx0, wy1, 0.0f, wx1, wy1, 0.0f, wx0, wy0, 0.0f, wx1, wy1, 0.0f, wx1, wy0, 0.0f,
        };
        float uvs[12] = {
            g->u0, g->v0, g->u0, g->v1, g->u1, g->v1, g->u0, g->v0, g->u1, g->v1, g->u1, g->v0,
        };

        sdl3d_mesh mesh;
        SDL_zero(mesh);
        mesh.positions = positions;
        mesh.uvs = uvs;
        mesh.vertex_count = 6;
        mesh.material_index = -1;

        if (!sdl3d_draw_mesh(context, &mesh, &font->atlas_texture, color))
        {
            ok = false;
            break;
        }

        cursor_x += advance;
    }

    sdl3d_end_mode_3d(context);
    context->view = saved_view;
    context->projection = saved_projection;
    context->view_projection = saved_view_projection;
    context->model_view_projection = saved_view_projection;
    sdl3d_set_shading_mode(context, prev_shading);
    sdl3d_set_backface_culling_enabled(context, prev_culling);
    return ok;
}

bool sdl3d_draw_text(struct sdl3d_render_context *context, const sdl3d_font *font, const char *text, float x, float y,
                     sdl3d_color color)
{
    return draw_text_internal(context, font, text, x, y, color, 0.0f);
}

/* ------------------------------------------------------------------ */
/* Convenience wrappers                                                */
/* ------------------------------------------------------------------ */

static float font_max_digit_advance(const sdl3d_font *font)
{
    float max_adv = 0.0f;
    for (int c = '0'; c <= '9'; c++)
    {
        int ci = c - SDL3D_FONT_FIRST_CHAR;
        float a = font->glyphs[ci].xadvance;
        if (a > max_adv)
            max_adv = a;
    }
    return max_adv;
}

bool sdl3d_draw_textfv(struct sdl3d_render_context *context, const sdl3d_font *font, float x, float y,
                       sdl3d_color color, const char *fmt, va_list args)
{
    char buf[512];
    if (!fmt)
    {
        return SDL_InvalidParamError("fmt");
    }
    SDL_vsnprintf(buf, sizeof(buf), fmt, args);
    return sdl3d_draw_text(context, font, buf, x, y, color);
}

bool sdl3d_draw_textf(struct sdl3d_render_context *context, const sdl3d_font *font, float x, float y, sdl3d_color color,
                      const char *fmt, ...)
{
    va_list args;
    bool ok;
    va_start(args, fmt);
    ok = sdl3d_draw_textfv(context, font, x, y, color, fmt, args);
    va_end(args);
    return ok;
}

bool sdl3d_draw_fps(struct sdl3d_render_context *context, const sdl3d_font *font, float dt)
{
    /* Exponential moving average on the instantaneous 1/dt, with the time
     * constant tied to real elapsed time so the smoothing behaves the same
     * whether the game runs at 60 or 600 FPS. tau = 0.5s. */
    static float smoothed = 0.0f;
    static bool primed = false;

    if (dt > 0.0f)
    {
        float instant = 1.0f / dt;
        if (!primed)
        {
            smoothed = instant;
            primed = true;
        }
        else
        {
            const float tau = 0.5f;
            float alpha = dt / (tau + dt);
            if (alpha > 1.0f)
                alpha = 1.0f;
            smoothed = smoothed + alpha * (instant - smoothed);
        }
    }

    /* Render "FPS: " with the font's native proportional spacing, then the
     * three-digit value in monospaced cells so the number's position and
     * width stays identical whether the reading is 7, 60, or 999. Missing
     * leading digits are emitted as spaces, which the internal path also
     * treats as monospace cells. */
    const sdl3d_color color = {0, 255, 0, 255};
    const float origin_x = 10.0f;
    const float origin_y = 10.0f;
    const char *prefix = "FPS: ";
    float prefix_w, prefix_h;
    sdl3d_measure_text(font, prefix, &prefix_w, &prefix_h);

    if (!draw_text_internal(context, font, prefix, origin_x, origin_y, color, 0.0f))
    {
        return false;
    }

    int ival = (int)(smoothed + 0.5f);
    if (ival < 0)
    {
        ival = 0;
    }
    if (ival > 999)
    {
        ival = 999;
    }
    char digits[4];
    digits[0] = (ival >= 100) ? (char)('0' + (ival / 100)) : ' ';
    digits[1] = (ival >= 10) ? (char)('0' + ((ival / 10) % 10)) : ' ';
    digits[2] = (char)('0' + (ival % 10));
    digits[3] = '\0';

    return draw_text_internal(context, font, digits, origin_x + prefix_w, origin_y, color,
                              font_max_digit_advance(font));
}

/* ------------------------------------------------------------------ */
/* Built-in font catalog                                               */
/* ------------------------------------------------------------------ */

typedef struct sdl3d_builtin_font_entry
{
    const char *name;
    const char *filename;
} sdl3d_builtin_font_entry;

static const sdl3d_builtin_font_entry sdl3d_builtin_fonts[SDL3D_BUILTIN_FONT_COUNT] = {
    [SDL3D_BUILTIN_FONT_ROBOTO] = {"Roboto", "Roboto.ttf"},
    [SDL3D_BUILTIN_FONT_INTER] = {"Inter", "Inter-Regular.ttf"},
    [SDL3D_BUILTIN_FONT_IBM_PLEX_SANS] = {"IBM Plex Sans", "IBMPlexSans-Regular.ttf"},
    [SDL3D_BUILTIN_FONT_NOTO_SANS] = {"Noto Sans", "NotoSans-Regular.ttf"},
    [SDL3D_BUILTIN_FONT_DM_SANS] = {"DM Sans", "DMSans-Regular.ttf"},
    [SDL3D_BUILTIN_FONT_SOURCE_SANS_3] = {"Source Sans 3", "SourceSans3-Regular.ttf"},
    [SDL3D_BUILTIN_FONT_EB_GARAMOND] = {"EB Garamond", "EBGaramond-Regular.ttf"},
    [SDL3D_BUILTIN_FONT_MERRIWEATHER] = {"Merriweather", "Merriweather-Regular.ttf"},
    [SDL3D_BUILTIN_FONT_SOURCE_SERIF_4] = {"Source Serif 4", "SourceSerif4-Regular.ttf"},
};

static bool builtin_font_valid(sdl3d_builtin_font id)
{
    return id >= 0 && id < SDL3D_BUILTIN_FONT_COUNT;
}

const char *sdl3d_builtin_font_name(sdl3d_builtin_font id)
{
    return builtin_font_valid(id) ? sdl3d_builtin_fonts[id].name : NULL;
}

const char *sdl3d_builtin_font_filename(sdl3d_builtin_font id)
{
    return builtin_font_valid(id) ? sdl3d_builtin_fonts[id].filename : NULL;
}

bool sdl3d_load_builtin_font(const char *media_dir, sdl3d_builtin_font id, float pixel_size, sdl3d_font *out)
{
    if (!builtin_font_valid(id))
    {
        return SDL_SetError("sdl3d_load_builtin_font: invalid font id %d", (int)id);
    }
    if (!media_dir)
    {
        return SDL_InvalidParamError("media_dir");
    }
    if (!out)
    {
        return SDL_InvalidParamError("out");
    }

    /* Compose "<media_dir>/fonts/<filename>". The engine convention is
     * that all bundled assets live under media_dir, with fonts in a
     * "fonts" subdirectory — the path the bundled LICENSE.md documents. */
    const char *filename = sdl3d_builtin_fonts[id].filename;
    char path[1024];
    int needed = SDL_snprintf(path, sizeof(path), "%s/fonts/%s", media_dir, filename);
    if (needed < 0 || needed >= (int)sizeof(path))
    {
        return SDL_SetError("sdl3d_load_builtin_font: path too long for media_dir='%s' filename='%s'", media_dir,
                            filename);
    }
    return sdl3d_load_font(path, pixel_size, out);
}

/* ------------------------------------------------------------------ */
/* Overlay text (UI layer)                                             */
/* ------------------------------------------------------------------ */

#include "gl_renderer.h"

bool sdl3d_draw_text_overlay(struct sdl3d_render_context *context, const sdl3d_font *font, const char *text, float x,
                             float y, sdl3d_color color)
{
    SDL_Rect scissor_rect = {0, 0, 0, 0};
    bool scissor_enabled = false;

    if (!context || !font || !text)
        return SDL_InvalidParamError("context");
    if (!font->atlas_texture.pixels)
        return SDL_SetError("sdl3d_draw_text_overlay: font atlas not initialized");

    /* Software backend: fall through to the normal path. */
    if (!context->gl)
        return sdl3d_draw_text(context, font, text, x, y, color);

    int ctx_w = sdl3d_get_render_context_width(context);
    int ctx_h = sdl3d_get_render_context_height(context);
    if (ctx_w <= 0 || ctx_h <= 0)
        return SDL_SetError("Invalid render context dimensions");

    scissor_enabled = sdl3d_is_scissor_enabled(context);
    if (scissor_enabled && !sdl3d_get_scissor_rect(context, &scissor_rect))
        return false;

    /* Count glyphs to size the batch. */
    int glyph_count = 0;
    for (const char *p = text; *p; p++)
    {
        int ci = (int)*p - SDL3D_FONT_FIRST_CHAR;
        if (*p == '\n' || ci < 0 || ci >= SDL3D_FONT_CHAR_COUNT)
            continue;
        const sdl3d_glyph *g = &font->glyphs[ci];
        if ((g->xoff2 - g->xoff) > 0 && (g->yoff2 - g->yoff) > 0)
            glyph_count++;
    }
    if (glyph_count == 0)
        return true;

    int vert_count = glyph_count * 6;
    float *positions = SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    float *uvs = SDL_malloc((size_t)vert_count * 2 * sizeof(float));
    if (!positions || !uvs)
    {
        SDL_free(positions);
        SDL_free(uvs);
        return SDL_OutOfMemory();
    }

    /* Build an orthographic MVP: maps screen pixels to clip space. */
    float hx = (float)ctx_w * 0.5f;
    float hy = (float)ctx_h * 0.5f;
    /* Simple ortho: x in [-hx, hx] → [-1, 1], y in [-hy, hy] → [-1, 1] */
    float mvp[16] = {0};
    mvp[0] = 1.0f / hx;
    mvp[5] = 1.0f / hy;
    mvp[10] = -1.0f;
    mvp[15] = 1.0f;

    float tint[4] = {(float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f,
                     (float)color.a / 255.0f};

    float cursor_x = x;
    float cursor_y = y + font->ascent;
    float line_h = font->ascent - font->descent + font->line_gap;
    int vi = 0;

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
            continue;

        const sdl3d_glyph *g = &font->glyphs[ci];
        float gw = g->xoff2 - g->xoff;
        float gh = g->yoff2 - g->yoff;
        if (gw <= 0 || gh <= 0)
        {
            cursor_x += g->xadvance;
            continue;
        }

        float sx0 = cursor_x + g->xoff;
        float sy0 = cursor_y + g->yoff;
        float sx1 = cursor_x + g->xoff2;
        float sy1 = cursor_y + g->yoff2;

        /* Map to ortho world space (Y flipped). */
        float wx0 = sx0 - hx, wx1 = sx1 - hx;
        float wy0 = hy - sy0, wy1 = hy - sy1;

        /* Two triangles, CCW. */
        int pi = vi * 3, ui = vi * 2;
        positions[pi + 0] = wx0;
        positions[pi + 1] = wy0;
        positions[pi + 2] = 0.0f;
        positions[pi + 3] = wx0;
        positions[pi + 4] = wy1;
        positions[pi + 5] = 0.0f;
        positions[pi + 6] = wx1;
        positions[pi + 7] = wy1;
        positions[pi + 8] = 0.0f;
        positions[pi + 9] = wx0;
        positions[pi + 10] = wy0;
        positions[pi + 11] = 0.0f;
        positions[pi + 12] = wx1;
        positions[pi + 13] = wy1;
        positions[pi + 14] = 0.0f;
        positions[pi + 15] = wx1;
        positions[pi + 16] = wy0;
        positions[pi + 17] = 0.0f;

        uvs[ui + 0] = g->u0;
        uvs[ui + 1] = g->v0;
        uvs[ui + 2] = g->u0;
        uvs[ui + 3] = g->v1;
        uvs[ui + 4] = g->u1;
        uvs[ui + 5] = g->v1;
        uvs[ui + 6] = g->u0;
        uvs[ui + 7] = g->v0;
        uvs[ui + 8] = g->u1;
        uvs[ui + 9] = g->v1;
        uvs[ui + 10] = g->u1;
        uvs[ui + 11] = g->v0;

        vi += 6;
        cursor_x += g->xadvance;
    }

    bool ok = sdl3d_gl_append_overlay(context->gl, positions, uvs, vi, mvp, tint, &font->atlas_texture, scissor_enabled,
                                      scissor_enabled ? &scissor_rect : NULL);
    SDL_free(positions);
    SDL_free(uvs);
    return ok;
}

bool sdl3d_draw_textf_overlay(struct sdl3d_render_context *context, const sdl3d_font *font, float x, float y,
                              sdl3d_color color, const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    SDL_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return sdl3d_draw_text_overlay(context, font, buf, x, y, color);
}

bool sdl3d_draw_fps_overlay(struct sdl3d_render_context *context, const sdl3d_font *font, float dt)
{
    static float smoothed = 0.0f;
    if (dt > 0.0f)
        smoothed += (1.0f / dt - smoothed) * 0.05f;
    int ival = (int)(smoothed + 0.5f);
    if (ival < 0)
        ival = 0;
    sdl3d_color green = {0, 255, 0, 255};
    return sdl3d_draw_textf_overlay(context, font, 10.0f, 10.0f, green, "FPS: %d", ival);
}

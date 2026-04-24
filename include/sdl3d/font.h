/*
 * Backend-agnostic font system using stb_truetype.
 *
 * Loads TTF/OTF fonts, generates glyph atlases, measures text,
 * and renders text as textured quads through the engine draw path.
 *
 * Usage:
 *   sdl3d_font font;
 *   sdl3d_load_font("media/fonts/Roboto.ttf", 24.0f, &font);
 *   // in render loop, after begin_mode_3d or in 2D overlay:
 *   sdl3d_draw_text(ctx, &font, "Hello World", 10, 10, white);
 *   // cleanup:
 *   sdl3d_free_font(&font);
 */

#ifndef SDL3D_FONT_H
#define SDL3D_FONT_H

#include "sdl3d/texture.h"
#include "sdl3d/types.h"

#include <SDL3/SDL_stdinc.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SDL3D_FONT_FIRST_CHAR 32
#define SDL3D_FONT_CHAR_COUNT 95 /* ASCII 32..126 */

    typedef struct sdl3d_glyph
    {
        float u0, v0, u1, v1; /* normalized atlas UVs */
        float xoff, yoff;     /* display-space offset from cursor to top-left */
        float xoff2, yoff2;   /* display-space offset from cursor to bottom-right */
        float xadvance;       /* horizontal advance after this glyph */
    } sdl3d_glyph;

    typedef struct sdl3d_font
    {
        sdl3d_glyph glyphs[SDL3D_FONT_CHAR_COUNT];
        unsigned char *atlas_pixels; /* single-channel alpha, atlas_w × atlas_h */
        int atlas_w, atlas_h;
        float size;                    /* font size in pixels */
        float ascent;                  /* distance from baseline to top */
        float descent;                 /* distance from baseline to bottom (negative) */
        float line_gap;                /* extra spacing between lines */
        sdl3d_texture2d atlas_texture; /* RGBA copy bound to the render backend */
    } sdl3d_font;

    /*
     * Load a TTF/OTF font from a file and generate a glyph atlas.
     * `pixel_size` is the font height in pixels.
     * Returns false with SDL_GetError on failure.
     */
    bool sdl3d_load_font(const char *path, float pixel_size, sdl3d_font *out);

    /*
     * Load a font from memory (e.g., embedded font data).
     */
    bool sdl3d_load_font_from_memory(const void *data, int data_size, float pixel_size, sdl3d_font *out);

    /*
     * Free font resources (atlas pixels).
     */
    void sdl3d_free_font(sdl3d_font *font);

    /*
     * Measure the pixel width and height of a string.
     * Handles \n for line breaks.
     */
    void sdl3d_measure_text(const sdl3d_font *font, const char *text, float *out_width, float *out_height);

    /*
     * Draw text at screen position (x, y) in pixels.
     * Must be called between sdl3d_clear_render_context and sdl3d_present.
     * Uses the 2D overlay path — does not require begin_mode_3d.
     */
    struct sdl3d_render_context;
    bool sdl3d_draw_text(struct sdl3d_render_context *context, const sdl3d_font *font, const char *text, float x,
                         float y, sdl3d_color color);

    /*
     * printf-style convenience wrapper around sdl3d_draw_text. The formatted
     * string is rendered into a fixed-size internal buffer (512 bytes); longer
     * output is truncated.
     */
    bool sdl3d_draw_textf(struct sdl3d_render_context *context, const sdl3d_font *font, float x, float y,
                          sdl3d_color color, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(6);

    bool sdl3d_draw_textfv(struct sdl3d_render_context *context, const sdl3d_font *font, float x, float y,
                           sdl3d_color color, const char *fmt, va_list args);

    /*
     * Convenience: render a smoothed FPS counter in bright green at the top-
     * left of the screen. Call once per frame, passing the current frame's
     * delta time in seconds. Internally maintains a smoothed running estimate
     * so the displayed value doesn't jitter frame-to-frame.
     *
     * Intended for quick diagnostics. For custom placement or styling, sample
     * dt yourself and call sdl3d_draw_textf directly.
     */
    bool sdl3d_draw_fps(struct sdl3d_render_context *context, const sdl3d_font *font, float dt);

    /* ------------------------------------------------------------------ */
    /* Overlay text (UI layer — bypasses 3D pipeline / post-processing)    */
    /* ------------------------------------------------------------------ */

    /*
     * Draw text on a dedicated UI overlay layer that is composited after
     * all 3D rendering and post-processing. This is the correct path for
     * HUD/editor/UI text. On GL, persistent overlay textures are refreshed
     * when the font atlas texture generation changes.
     *
     * On the software backend this is equivalent to sdl3d_draw_text.
     */
    bool sdl3d_draw_text_overlay(struct sdl3d_render_context *context, const sdl3d_font *font, const char *text,
                                 float x, float y, sdl3d_color color);

    bool sdl3d_draw_textf_overlay(struct sdl3d_render_context *context, const sdl3d_font *font, float x, float y,
                                  sdl3d_color color, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
        SDL_PRINTF_VARARG_FUNC(6);

    bool sdl3d_draw_fps_overlay(struct sdl3d_render_context *context, const sdl3d_font *font, float dt);

    /* ------------------------------------------------------------------ */
    /* Built-in font catalog                                               */
    /* ------------------------------------------------------------------ */

    /*
     * SDL_3D ships with a curated set of open-licensed fonts under
     * media/fonts/. The catalog below exposes them by stable ID so
     * applications and the UI system can reference a "built-in" font
     * without hard-coding filenames. Licensing details live alongside
     * the files (media/fonts/LICENSE.md + media/fonts/licenses/).
     *
     * Today the loader resolves each ID to a TTF path on disk. The
     * eventual plan is to embed the bytes directly in the library so
     * games don't need to ship the media directory at all —
     * sdl3d_load_builtin_font will then flip over transparently to
     * sdl3d_load_font_from_memory without any API change.
     */
    typedef enum sdl3d_builtin_font
    {
        SDL3D_BUILTIN_FONT_ROBOTO = 0,
        SDL3D_BUILTIN_FONT_INTER,
        SDL3D_BUILTIN_FONT_IBM_PLEX_SANS,
        SDL3D_BUILTIN_FONT_NOTO_SANS,
        SDL3D_BUILTIN_FONT_DM_SANS,
        SDL3D_BUILTIN_FONT_SOURCE_SANS_3,
        SDL3D_BUILTIN_FONT_EB_GARAMOND,
        SDL3D_BUILTIN_FONT_MERRIWEATHER,
        SDL3D_BUILTIN_FONT_SOURCE_SERIF_4,
        SDL3D_BUILTIN_FONT_COUNT
    } sdl3d_builtin_font;

    /*
     * Human-readable family name, e.g. "IBM Plex Sans".
     */
    const char *sdl3d_builtin_font_name(sdl3d_builtin_font id);

    /*
     * TTF filename (no directory component) under media/fonts/,
     * e.g. "IBMPlexSans-Regular.ttf".
     */
    const char *sdl3d_builtin_font_filename(sdl3d_builtin_font id);

    /*
     * Convenience loader that resolves a built-in ID to its TTF path
     * under `media_dir` and loads it at `pixel_size`. `media_dir` is
     * typically the project's SDL3D_MEDIA_DIR compile define, so a
     * typical call looks like:
     *
     *   sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, SDL3D_BUILTIN_FONT_INTER,
     *                           24.0f, &font);
     */
    bool sdl3d_load_builtin_font(const char *media_dir, sdl3d_builtin_font id, float pixel_size, sdl3d_font *out);

#ifdef __cplusplus
}
#endif

#endif

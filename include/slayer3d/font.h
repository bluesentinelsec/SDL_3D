/*
 * Backend-agnostic font system using stb_truetype.
 *
 * Loads TTF/OTF fonts, generates glyph atlases, measures text,
 * and renders text as textured quads through the engine draw path.
 *
 * Usage:
 *   slayer3d_font font;
 *   slayer3d_load_font("media/fonts/Roboto.ttf", 24.0f, &font);
 *   // in render loop, after begin_mode_3d or in 2D overlay:
 *   slayer3d_draw_text(ctx, &font, "Hello World", 10, 10, white);
 *   // cleanup:
 *   slayer3d_free_font(&font);
 */

#ifndef SLAYER3D_FONT_H
#define SLAYER3D_FONT_H

#include "slayer3d/texture.h"
#include "slayer3d/types.h"

#include <SDL3/SDL_stdinc.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SLAYER3D_FONT_FIRST_CHAR 32
#define SLAYER3D_FONT_CHAR_COUNT 95 /* ASCII 32..126 */

    typedef struct slayer3d_glyph
    {
        float u0, v0, u1, v1; /* normalized atlas UVs */
        float xoff, yoff;     /* display-space offset from cursor to top-left */
        float xoff2, yoff2;   /* display-space offset from cursor to bottom-right */
        float xadvance;       /* horizontal advance after this glyph */
    } slayer3d_glyph;

    typedef struct slayer3d_font
    {
        slayer3d_glyph glyphs[SLAYER3D_FONT_CHAR_COUNT];
        unsigned char *atlas_pixels; /* single-channel alpha, atlas_w × atlas_h */
        int atlas_w, atlas_h;
        float size;                       /* font size in pixels */
        float ascent;                     /* distance from baseline to top */
        float descent;                    /* distance from baseline to bottom (negative) */
        float line_gap;                   /* extra spacing between lines */
        slayer3d_texture2d atlas_texture; /* RGBA copy bound to the render backend */
    } slayer3d_font;

    /*
     * Load a TTF/OTF font from a file and generate a glyph atlas.
     * `pixel_size` is the font height in pixels.
     * Returns false with SDL_GetError on failure.
     */
    bool slayer3d_load_font(const char *path, float pixel_size, slayer3d_font *out);

    /*
     * Load a font from memory (e.g., embedded font data).
     */
    bool slayer3d_load_font_from_memory(const void *data, int data_size, float pixel_size, slayer3d_font *out);

    /*
     * Free font resources (atlas pixels).
     */
    void slayer3d_free_font(slayer3d_font *font);

    /*
     * Measure the pixel width and height of a string.
     * Handles \n for line breaks.
     */
    void slayer3d_measure_text(const slayer3d_font *font, const char *text, float *out_width, float *out_height);

    /*
     * Draw text at screen position (x, y) in pixels.
     * Must be called between slayer3d_clear_render_context and slayer3d_present.
     * Uses the 2D overlay path — does not require begin_mode_3d.
     */
    struct slayer3d_render_context;
    bool slayer3d_draw_text(struct slayer3d_render_context *context, const slayer3d_font *font, const char *text,
                            float x, float y, slayer3d_color color);

    /*
     * printf-style convenience wrapper around slayer3d_draw_text. The formatted
     * string is rendered into a fixed-size internal buffer (512 bytes); longer
     * output is truncated.
     */
    bool slayer3d_draw_textf(struct slayer3d_render_context *context, const slayer3d_font *font, float x, float y,
                             slayer3d_color color, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
        SDL_PRINTF_VARARG_FUNC(6);

    bool slayer3d_draw_textfv(struct slayer3d_render_context *context, const slayer3d_font *font, float x, float y,
                              slayer3d_color color, const char *fmt, va_list args);

    /*
     * Convenience: render a smoothed FPS counter in bright green at the top-
     * left of the screen. Call once per frame, passing the current frame's
     * delta time in seconds. Internally maintains a smoothed running estimate
     * so the displayed value doesn't jitter frame-to-frame.
     *
     * Intended for quick diagnostics. For custom placement or styling, sample
     * dt yourself and call slayer3d_draw_textf directly.
     */
    bool slayer3d_draw_fps(struct slayer3d_render_context *context, const slayer3d_font *font, float dt);

    /* ------------------------------------------------------------------ */
    /* Overlay text (UI layer — bypasses 3D pipeline / post-processing)    */
    /* ------------------------------------------------------------------ */

    /*
     * Draw text on a dedicated UI overlay layer that is composited after
     * all 3D rendering and post-processing. This is the correct path for
     * HUD/editor/UI text. On GL, persistent overlay textures are refreshed
     * when the font atlas texture generation changes.
     *
     * On the software backend this is equivalent to slayer3d_draw_text.
     */
    bool slayer3d_draw_text_overlay(struct slayer3d_render_context *context, const slayer3d_font *font,
                                    const char *text, float x, float y, slayer3d_color color);

    /**
     * @brief Draw text on the UI overlay layer with a pixel-space scale multiplier.
     *
     * A scale of 1.0 is equivalent to slayer3d_draw_text_overlay().
     */
    bool slayer3d_draw_text_overlay_scaled(struct slayer3d_render_context *context, const slayer3d_font *font,
                                           const char *text, float x, float y, float scale, slayer3d_color color);

    bool slayer3d_draw_textf_overlay(struct slayer3d_render_context *context, const slayer3d_font *font, float x,
                                     float y, slayer3d_color color, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
        SDL_PRINTF_VARARG_FUNC(6);

    bool slayer3d_draw_fps_overlay(struct slayer3d_render_context *context, const slayer3d_font *font, float dt);

    /* ------------------------------------------------------------------ */
    /* Built-in font catalog                                               */
    /* ------------------------------------------------------------------ */

    /*
     * Slayer3D exposes a curated set of open-licensed fonts by stable ID
     * so applications and the UI system can reference a "built-in" font
     * without hard-coding filenames. Licensing details live alongside the
     * source font files (media/fonts/LICENSE.md + media/fonts/licenses/).
     *
     * Inter is embedded in the library as the default editor/UI font. Other
     * catalog entries currently resolve from media/fonts/ until they are
     * intentionally moved to embedded data.
     */
    typedef enum slayer3d_builtin_font
    {
        SLAYER3D_BUILTIN_FONT_ROBOTO = 0,
        SLAYER3D_BUILTIN_FONT_INTER,
        SLAYER3D_BUILTIN_FONT_IBM_PLEX_SANS,
        SLAYER3D_BUILTIN_FONT_NOTO_SANS,
        SLAYER3D_BUILTIN_FONT_DM_SANS,
        SLAYER3D_BUILTIN_FONT_SOURCE_SANS_3,
        SLAYER3D_BUILTIN_FONT_EB_GARAMOND,
        SLAYER3D_BUILTIN_FONT_MERRIWEATHER,
        SLAYER3D_BUILTIN_FONT_SOURCE_SERIF_4,
        SLAYER3D_BUILTIN_FONT_COUNT
    } slayer3d_builtin_font;

    /*
     * Human-readable family name, e.g. "IBM Plex Sans".
     */
    const char *slayer3d_builtin_font_name(slayer3d_builtin_font id);

    /*
     * TTF filename (no directory component) under media/fonts/,
     * e.g. "IBMPlexSans-Regular.ttf".
     */
    const char *slayer3d_builtin_font_filename(slayer3d_builtin_font id);

    /*
     * Convenience loader that resolves a built-in ID and loads it at
     * `pixel_size`. Embedded built-ins ignore `media_dir`; disk-backed
     * catalog entries resolve under `media_dir/fonts`. `media_dir` is
     * typically the project's SLAYER3D_MEDIA_DIR compile define, so a
     * disk-backed call looks like:
     *
     *   slayer3d_load_builtin_font(SLAYER3D_MEDIA_DIR, SLAYER3D_BUILTIN_FONT_INTER,
     *                           24.0f, &font);
     */
    bool slayer3d_load_builtin_font(const char *media_dir, slayer3d_builtin_font id, float pixel_size,
                                    slayer3d_font *out);

#ifdef __cplusplus
}
#endif

#endif

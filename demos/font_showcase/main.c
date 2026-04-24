/*
 * Font showcase — renders every built-in font so you can visually
 * compare them side-by-side.  Uses the GL backend at native pixel
 * density for crisp HiDPI text.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/font.h"
#include "sdl3d/render_context.h"

#define WINDOW_W 1280
#define WINDOW_H 720
#define FONT_SIZE 28.0f

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;

    /* GL context for hardware-accelerated rendering. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    /* HIGH_PIXEL_DENSITY lets SDL expose the full Retina framebuffer. */
    SDL_Window *win = SDL_CreateWindow("SDL3D \xe2\x80\x94 Font Showcase", WINDOW_W, WINDOW_H,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (!win)
        return 1;

    /* Query actual pixel density (2.0 on Retina, 1.0 otherwise). */
    float dpi_scale = SDL_GetWindowPixelDensity(win);
    if (dpi_scale < 1.0f)
        dpi_scale = 1.0f;
    SDL_Log("Pixel density: %.1f", dpi_scale);

    sdl3d_render_context_config cfg;
    sdl3d_init_render_context_config(&cfg);
    cfg.backend = SDL3D_BACKEND_SDLGPU;

    sdl3d_render_context *ctx = NULL;
    if (!sdl3d_create_render_context(win, NULL, &cfg, &ctx))
    {
        SDL_Log("Render context failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        return 1;
    }

    int render_w = sdl3d_get_render_context_width(ctx);
    int render_h = sdl3d_get_render_context_height(ctx);
    SDL_Log("Render target: %dx%d", render_w, render_h);

    /* Rasterize fonts at physical pixel size for crisp glyphs. */
    float physical_font_size = FONT_SIZE * dpi_scale;
    sdl3d_font fonts[SDL3D_BUILTIN_FONT_COUNT];
    bool loaded[SDL3D_BUILTIN_FONT_COUNT];
    for (int i = 0; i < SDL3D_BUILTIN_FONT_COUNT; ++i)
    {
        loaded[i] = sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, (sdl3d_builtin_font)i, physical_font_size, &fonts[i]);
        if (!loaded[i])
            SDL_Log("Failed to load %s: %s", sdl3d_builtin_font_name(i), SDL_GetError());
    }

    sdl3d_color bg = {30, 30, 30, 255};
    sdl3d_color white = {255, 255, 255, 255};
    sdl3d_color label_color = {140, 180, 255, 255};
    bool running = true;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
                running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_ESCAPE)
                running = false;
        }

        sdl3d_clear_render_context(ctx, bg);

        /*
         * Coordinates are in physical pixels since the render context
         * operates at native resolution.  Scale logical positions by
         * dpi_scale so layout stays consistent across displays.
         */
        float y = 20.0f * dpi_scale;
        float line_spacing = (FONT_SIZE + 4.0f) * dpi_scale;
        float block_spacing = (FONT_SIZE + 16.0f) * dpi_scale;

        for (int i = 0; i < SDL3D_BUILTIN_FONT_COUNT; ++i)
        {
            if (!loaded[i])
                continue;
            const char *name = sdl3d_builtin_font_name(i);
            sdl3d_draw_textf(ctx, &fonts[i], 20.0f * dpi_scale, y, label_color, "%s:", name);
            y += line_spacing;
            sdl3d_draw_text(ctx, &fonts[i], "The quick brown fox jumps over the lazy dog.", 40.0f * dpi_scale, y,
                            white);
            y += block_spacing;
        }

        sdl3d_present_render_context(ctx);
    }

    for (int i = 0; i < SDL3D_BUILTIN_FONT_COUNT; ++i)
    {
        if (loaded[i])
            sdl3d_free_font(&fonts[i]);
    }
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

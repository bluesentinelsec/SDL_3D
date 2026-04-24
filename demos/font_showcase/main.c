/*
 * Font showcase — renders every built-in font so you can visually
 * compare them side-by-side.  Up/Down arrows adjust font size.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/font.h"
#include "sdl3d/render_context.h"

#define FONT_SIZE_DEFAULT 36.0f
#define FONT_SIZE_STEP 4.0f
#define FONT_SIZE_MIN 8.0f
#define FONT_SIZE_MAX 120.0f

static void reload_fonts(sdl3d_font fonts[], bool loaded[], float size)
{
    for (int i = 0; i < SDL3D_BUILTIN_FONT_COUNT; ++i)
    {
        if (loaded[i])
            sdl3d_free_font(&fonts[i]);
        loaded[i] = sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, (sdl3d_builtin_font)i, size, &fonts[i]);
        if (!loaded[i])
            SDL_Log("Failed to load %s: %s", sdl3d_builtin_font_name(i), SDL_GetError());
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;

    /* Match the primary display resolution. */
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display);
    int win_w = mode ? mode->w : 1280;
    int win_h = mode ? mode->h : 720;
    SDL_Log("Display: %dx%d", win_w, win_h);

    SDL_Window *win = SDL_CreateWindow("SDL3D \xe2\x80\x94 Font Showcase", win_w, win_h,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win)
        return 1;

    /* Query physical pixel density so we render at native resolution. */
    float dpi_scale = SDL_GetWindowPixelDensity(win);
    if (dpi_scale < 1.0f)
        dpi_scale = 1.0f;
    int phys_w = (int)((float)win_w * dpi_scale);
    int phys_h = (int)((float)win_h * dpi_scale);
    SDL_Log("Pixel density: %.1f  Physical: %dx%d", dpi_scale, phys_w, phys_h);

    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren)
    {
        SDL_DestroyWindow(win);
        return 1;
    }

    sdl3d_render_context_config cfg;
    sdl3d_init_render_context_config(&cfg);
    cfg.backend = SDL3D_BACKEND_SOFTWARE;
    cfg.logical_width = phys_w;
    cfg.logical_height = phys_h;

    sdl3d_render_context *ctx = NULL;
    if (!sdl3d_create_render_context(win, ren, &cfg, &ctx))
    {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        return 1;
    }

    float font_size = FONT_SIZE_DEFAULT;
    sdl3d_font fonts[SDL3D_BUILTIN_FONT_COUNT];
    bool loaded[SDL3D_BUILTIN_FONT_COUNT] = {false};
    reload_fonts(fonts, loaded, font_size * dpi_scale);

    sdl3d_color bg = {30, 30, 30, 255};
    sdl3d_color white = {255, 255, 255, 255};
    sdl3d_color label_color = {140, 180, 255, 255};
    sdl3d_color hint_color = {100, 100, 100, 255};
    bool running = true;

    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
                running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN)
            {
                if (ev.key.scancode == SDL_SCANCODE_ESCAPE)
                    running = false;
                if (ev.key.scancode == SDL_SCANCODE_UP && font_size < FONT_SIZE_MAX)
                {
                    font_size += FONT_SIZE_STEP;
                    reload_fonts(fonts, loaded, font_size * dpi_scale);
                    SDL_Log("Font size: %.0f", font_size);
                }
                if (ev.key.scancode == SDL_SCANCODE_DOWN && font_size > FONT_SIZE_MIN)
                {
                    font_size -= FONT_SIZE_STEP;
                    reload_fonts(fonts, loaded, font_size * dpi_scale);
                    SDL_Log("Font size: %.0f", font_size);
                }
            }
        }

        sdl3d_clear_render_context(ctx, bg);

        float scaled_size = font_size * dpi_scale;
        float y = 20.0f * dpi_scale;
        for (int i = 0; i < SDL3D_BUILTIN_FONT_COUNT; ++i)
        {
            if (!loaded[i])
                continue;
            const char *name = sdl3d_builtin_font_name(i);
            sdl3d_draw_textf(ctx, &fonts[i], 20.0f * dpi_scale, y, label_color, "%s:", name);
            y += scaled_size + 4.0f * dpi_scale;
            sdl3d_draw_text(ctx, &fonts[i], "The quick brown fox jumps over the lazy dog.", 40.0f * dpi_scale, y,
                            white);
            y += scaled_size + 16.0f * dpi_scale;
        }

        /* Size hint at bottom-left. */
        if (loaded[0])
            sdl3d_draw_textf(ctx, &fonts[0], 20.0f * dpi_scale, (float)phys_h - scaled_size - 10.0f * dpi_scale,
                             hint_color, "%.0fpx  [Up/Down to resize]", font_size);

        sdl3d_present_render_context(ctx);
    }

    for (int i = 0; i < SDL3D_BUILTIN_FONT_COUNT; ++i)
    {
        if (loaded[i])
            sdl3d_free_font(&fonts[i]);
    }
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

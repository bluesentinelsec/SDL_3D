/*
 * Font showcase — renders every built-in font so you can visually
 * compare them side-by-side.
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

    SDL_Window *win = SDL_CreateWindow("SDL3D \xe2\x80\x94 Font Showcase", WINDOW_W, WINDOW_H,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win)
        return 1;

    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren)
    {
        SDL_DestroyWindow(win);
        return 1;
    }

    sdl3d_render_context_config cfg;
    sdl3d_init_render_context_config(&cfg);
    cfg.backend = SDL3D_BACKEND_SOFTWARE;
    cfg.logical_width = WINDOW_W;
    cfg.logical_height = WINDOW_H;

    sdl3d_render_context *ctx = NULL;
    if (!sdl3d_create_render_context(win, ren, &cfg, &ctx))
    {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        return 1;
    }

    /* Load every built-in font. */
    sdl3d_font fonts[SDL3D_BUILTIN_FONT_COUNT];
    bool loaded[SDL3D_BUILTIN_FONT_COUNT];
    for (int i = 0; i < SDL3D_BUILTIN_FONT_COUNT; ++i)
    {
        loaded[i] = sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, (sdl3d_builtin_font)i, FONT_SIZE, &fonts[i]);
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

        float y = 20.0f;
        for (int i = 0; i < SDL3D_BUILTIN_FONT_COUNT; ++i)
        {
            if (!loaded[i])
                continue;
            const char *name = sdl3d_builtin_font_name(i);
            /* Label */
            sdl3d_draw_textf(ctx, &fonts[i], 20.0f, y, label_color, "%s:", name);
            y += FONT_SIZE + 4.0f;
            /* Sample text */
            sdl3d_draw_text(ctx, &fonts[i], "The quick brown fox jumps over the lazy dog.", 40.0f, y, white);
            y += FONT_SIZE + 16.0f;
        }

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

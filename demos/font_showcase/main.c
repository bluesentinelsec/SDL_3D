/*
 * Font showcase — renders every built-in font alongside a spinning
 * cube on the GL backend, with an FPS counter.  Up/Down arrows
 * adjust font size.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/camera.h"
#include "sdl3d/drawing3d.h"
#include "sdl3d/font.h"
#include "sdl3d/render_context.h"
#include "sdl3d/shapes.h"

#include <math.h>

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

    /* GL context setup. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *win = SDL_CreateWindow("SDL3D \xe2\x80\x94 Font Showcase", win_w, win_h,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win)
        return 1;

    float dpi_scale = SDL_GetWindowPixelDensity(win);
    if (dpi_scale < 1.0f)
        dpi_scale = 1.0f;
    SDL_Log("Display: %dx%d  DPI scale: %.1f", win_w, win_h, dpi_scale);

    sdl3d_render_context_config cfg;
    sdl3d_init_render_context_config(&cfg);
    cfg.backend = SDL3D_BACKEND_SDLGPU;
    cfg.logical_width = (int)((float)win_w * dpi_scale);
    cfg.logical_height = (int)((float)win_h * dpi_scale);

    sdl3d_render_context *ctx = NULL;
    if (!sdl3d_create_render_context(win, NULL, &cfg, &ctx))
    {
        SDL_Log("GL render context failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        return 1;
    }

    int render_w = sdl3d_get_render_context_width(ctx);
    int render_h = sdl3d_get_render_context_height(ctx);
    SDL_Log("Render: %dx%d  Backend: %s", render_w, render_h,
            sdl3d_get_backend_name(sdl3d_get_render_context_backend(ctx)));

    float font_size = FONT_SIZE_DEFAULT;
    sdl3d_font fonts[SDL3D_BUILTIN_FONT_COUNT];
    bool loaded[SDL3D_BUILTIN_FONT_COUNT] = {false};
    reload_fonts(fonts, loaded, font_size * dpi_scale);

    sdl3d_color bg = {30, 30, 40, 255};
    sdl3d_color white = {255, 255, 255, 255};
    sdl3d_color label_color = {140, 180, 255, 255};
    sdl3d_color hint_color = {100, 100, 100, 255};
    sdl3d_color cube_color = {80, 160, 255, 255};
    bool running = true;
    float elapsed = 0.0f;
    Uint64 last = SDL_GetPerformanceCounter();

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

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;
        elapsed += dt;

        sdl3d_clear_render_context(ctx, bg);

        /* 3D pass: spinning cube. */
        sdl3d_camera3d cam;
        cam.position = sdl3d_vec3_make(sinf(elapsed * 0.5f) * 4.0f, 2.0f, cosf(elapsed * 0.5f) * 4.0f);
        cam.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
        cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
        cam.fovy = 60.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        sdl3d_begin_mode_3d(ctx, cam);
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), sdl3d_vec3_make(1.5f, 1.5f, 1.5f), cube_color);
        sdl3d_end_mode_3d(ctx);

        /* 2D overlay: font samples + FPS. */
        float s = dpi_scale;
        float scaled_size = font_size * s;
        float y = 20.0f * s;
        for (int i = 0; i < SDL3D_BUILTIN_FONT_COUNT; ++i)
        {
            if (!loaded[i])
                continue;
            const char *name = sdl3d_builtin_font_name(i);
            sdl3d_draw_textf(ctx, &fonts[i], 20.0f * s, y, label_color, "%s:", name);
            y += scaled_size + 4.0f * s;
            sdl3d_draw_text(ctx, &fonts[i], "The quick brown fox jumps over the lazy dog.", 40.0f * s, y, white);
            y += scaled_size + 16.0f * s;
        }

        if (loaded[0])
        {
            sdl3d_draw_fps(ctx, &fonts[0], dt);
            sdl3d_draw_textf(ctx, &fonts[0], 20.0f * s, (float)render_h - scaled_size - 10.0f * s, hint_color,
                             "%.0fpx  [Up/Down to resize]", font_size);
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

#define SDL_MAIN_HANDLED
#include "sdl3d/app.h"
#include <SDL3/SDL.h>
#include <string.h>

#define SDL3D_APP_MAX_KEYS 512

static SDL_Window *s_window = NULL;
static SDL_Renderer *s_renderer = NULL;
static sdl3d_render_context *s_ctx = NULL;
static bool s_should_close = false;
static int s_target_fps = 60;
static Uint64 s_last_time = 0;
static float s_dt = 0.0f;
static int s_fps = 0;
static int s_frame_count = 0;
static Uint64 s_fps_timer = 0;
static float s_mouse_dx = 0, s_mouse_dy = 0;
static bool s_keys_down[SDL3D_APP_MAX_KEYS];
static bool s_keys_pressed[SDL3D_APP_MAX_KEYS];

bool sdl3d_init(int width, int height, const char *title)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        return false;
    }

    s_window = SDL_CreateWindow(title ? title : "SDL3D", width, height, SDL_WINDOW_RESIZABLE);
    if (!s_window)
    {
        SDL_Quit();
        return false;
    }

    s_renderer = SDL_CreateRenderer(s_window, NULL);
    if (!s_renderer)
    {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
        SDL_Quit();
        return false;
    }

    sdl3d_render_context_config cfg;
    sdl3d_init_render_context_config(&cfg);
    cfg.backend = SDL3D_BACKEND_SDLGPU;
    cfg.allow_backend_fallback = true;

    if (!sdl3d_create_render_context(s_window, s_renderer, &cfg, &s_ctx))
    {
        SDL_DestroyRenderer(s_renderer);
        s_renderer = NULL;
        SDL_DestroyWindow(s_window);
        s_window = NULL;
        SDL_Quit();
        return false;
    }

    s_should_close = false;
    s_target_fps = 60;
    s_dt = 0.0f;
    s_fps = 0;
    s_frame_count = 0;
    s_last_time = SDL_GetPerformanceCounter();
    s_fps_timer = s_last_time;
    s_mouse_dx = 0;
    s_mouse_dy = 0;
    memset(s_keys_down, 0, sizeof(s_keys_down));
    memset(s_keys_pressed, 0, sizeof(s_keys_pressed));

    return true;
}

void sdl3d_close(void)
{
    if (s_ctx)
    {
        sdl3d_destroy_render_context(s_ctx);
        s_ctx = NULL;
    }
    if (s_renderer)
    {
        SDL_DestroyRenderer(s_renderer);
        s_renderer = NULL;
    }
    if (s_window)
    {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}

bool sdl3d_should_close(void)
{
    return s_should_close;
}

void sdl3d_set_target_fps(int fps)
{
    s_target_fps = fps > 0 ? fps : 0;
}

float sdl3d_get_frame_time(void)
{
    return s_dt;
}

int sdl3d_get_fps(void)
{
    return s_fps;
}

sdl3d_render_context *sdl3d_get_context(void)
{
    return s_ctx;
}

SDL_Window *sdl3d_get_window(void)
{
    return s_window;
}

void sdl3d_begin_frame(sdl3d_color clear_color)
{
    (void)clear_color;

    memset(s_keys_pressed, 0, sizeof(s_keys_pressed));
    s_mouse_dx = 0;
    s_mouse_dy = 0;

    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
        switch (ev.type)
        {
        case SDL_EVENT_QUIT:
            s_should_close = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.scancode == SDL_SCANCODE_ESCAPE)
            {
                s_should_close = true;
            }
            if (ev.key.scancode < SDL3D_APP_MAX_KEYS)
            {
                if (!s_keys_down[ev.key.scancode])
                {
                    s_keys_pressed[ev.key.scancode] = true;
                }
                s_keys_down[ev.key.scancode] = true;
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (ev.key.scancode < SDL3D_APP_MAX_KEYS)
            {
                s_keys_down[ev.key.scancode] = false;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            s_mouse_dx += ev.motion.xrel;
            s_mouse_dy += ev.motion.yrel;
            break;
        default:
            break;
        }
    }

    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    s_dt = (float)(now - s_last_time) / (float)freq;
    s_last_time = now;
}

void sdl3d_end_frame(void)
{
    if (s_ctx)
    {
        sdl3d_present_render_context(s_ctx);
    }

    s_frame_count++;
    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    float elapsed = (float)(now - s_fps_timer) / (float)freq;
    if (elapsed >= 1.0f)
    {
        s_fps = s_frame_count;
        s_frame_count = 0;
        s_fps_timer = now;
    }

    if (s_target_fps > 0)
    {
        float target_frame_time = 1.0f / (float)s_target_fps;
        float frame_elapsed = (float)(SDL_GetPerformanceCounter() - s_last_time) / (float)freq;
        float remaining = target_frame_time - frame_elapsed;
        if (remaining > 0.0f)
        {
            SDL_Delay((Uint32)(remaining * 1000.0f));
        }
    }
}

bool sdl3d_is_key_down(int scancode)
{
    if (scancode < 0 || scancode >= SDL3D_APP_MAX_KEYS)
    {
        return false;
    }
    return s_keys_down[scancode];
}

bool sdl3d_is_key_pressed(int scancode)
{
    if (scancode < 0 || scancode >= SDL3D_APP_MAX_KEYS)
    {
        return false;
    }
    return s_keys_pressed[scancode];
}

void sdl3d_get_mouse_delta(float *dx, float *dy)
{
    if (dx)
    {
        *dx = s_mouse_dx;
    }
    if (dy)
    {
        *dy = s_mouse_dy;
    }
}

void sdl3d_set_mouse_captured(bool captured)
{
    if (s_window)
    {
        SDL_SetWindowRelativeMouseMode(s_window, captured);
    }
}

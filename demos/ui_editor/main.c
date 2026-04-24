/*
 * UI Editor Demo — TrenchBroom-style mockup that exercises every
 * available UI widget and draw primitive.
 *
 * Layout:
 *   ┌──────────────────────────────────────────────┐
 *   │  Menu Bar                                     │
 *   ├────────┬─────────────────────────┬────────────┤
 *   │        │                         │            │
 *   │  Tool  │   3D Viewport           │  Inspector │
 *   │  Panel │   (spinning cube)       │  Panel     │
 *   │        │                         │            │
 *   ├────────┴─────────────────────────┴────────────┤
 *   │  Status Bar                                   │
 *   └──────────────────────────────────────────────┘
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/camera.h"
#include "sdl3d/drawing3d.h"
#include "sdl3d/font.h"
#include "sdl3d/lighting.h"
#include "sdl3d/render_context.h"
#include "sdl3d/shapes.h"
#include "sdl3d/ui.h"

#include <math.h>

#define WINDOW_W 1280
#define WINDOW_H 720

/* Panel dimensions */
#define MENU_H 32.0f
#define STATUS_H 28.0f
#define TOOL_W 160.0f
#define INSPECTOR_W 220.0f
#define BTN_H 28.0f
#define BTN_PAD 4.0f

static void draw_menu_bar(sdl3d_ui_context *ui, float w, int *click_count)
{
    sdl3d_ui_draw_rect(ui, 0, 0, w, MENU_H, (sdl3d_color){45, 45, 48, 255});
    sdl3d_ui_draw_rect_outline(ui, 0, 0, w, MENU_H, 1.0f, (sdl3d_color){70, 70, 75, 255});

    float bx = 4.0f;
    float bw = 60.0f;
    if (sdl3d_ui_button(ui, bx, 2, bw, BTN_H, "File"))
        (*click_count)++;
    bx += bw + BTN_PAD;
    if (sdl3d_ui_button(ui, bx, 2, bw, BTN_H, "Edit"))
        (*click_count)++;
    bx += bw + BTN_PAD;
    if (sdl3d_ui_button(ui, bx, 2, bw, BTN_H, "View"))
        (*click_count)++;
    bx += bw + BTN_PAD;
    if (sdl3d_ui_button(ui, bx, 2, bw, BTN_H, "Map"))
        (*click_count)++;
    bx += bw + BTN_PAD;
    if (sdl3d_ui_button(ui, bx, 2, bw, BTN_H, "Help"))
        (*click_count)++;
}

static void draw_tool_panel(sdl3d_ui_context *ui, float y0, float h, const char **active_tool, int *click_count)
{
    sdl3d_ui_draw_rect(ui, 0, y0, TOOL_W, h, (sdl3d_color){38, 38, 42, 255});
    sdl3d_ui_draw_rect_outline(ui, 0, y0, TOOL_W, h, 1.0f, (sdl3d_color){70, 70, 75, 255});

    sdl3d_ui_push_clip(ui, 0, y0, TOOL_W, h);

    sdl3d_ui_label(ui, 8, y0 + 8, "Tools");

    float by = y0 + 30;
    float bw = TOOL_W - 16;
    const char *tools[] = {"Select", "Move", "Rotate", "Scale", "Clip", "Vertex", "Entity"};
    int tool_count = (int)(sizeof(tools) / sizeof(tools[0]));

    for (int i = 0; i < tool_count; i++)
    {
        if (sdl3d_ui_button(ui, 8, by, bw, BTN_H, tools[i]))
        {
            *active_tool = tools[i];
            (*click_count)++;
        }
        /* Highlight active tool */
        if (*active_tool == tools[i])
        {
            sdl3d_ui_draw_rect_outline(ui, 8, by, bw, BTN_H, 2.0f, (sdl3d_color){100, 180, 255, 255});
        }
        by += BTN_H + BTN_PAD;
    }

    sdl3d_ui_pop_clip(ui);
}

static void draw_inspector_panel(sdl3d_ui_context *ui, float x0, float y0, float h, const char *active_tool,
                                 int click_count)
{
    sdl3d_ui_draw_rect(ui, x0, y0, INSPECTOR_W, h, (sdl3d_color){38, 38, 42, 255});
    sdl3d_ui_draw_rect_outline(ui, x0, y0, INSPECTOR_W, h, 1.0f, (sdl3d_color){70, 70, 75, 255});

    sdl3d_ui_push_clip(ui, x0, y0, INSPECTOR_W, h);

    sdl3d_ui_label(ui, x0 + 8, y0 + 8, "Inspector");

    /* Property rows */
    float ry = y0 + 34;
    float rw = INSPECTOR_W - 16;

    /* Section: Entity */
    sdl3d_ui_draw_rect(ui, x0 + 8, ry, rw, 1, (sdl3d_color){70, 70, 75, 255});
    ry += 6;
    sdl3d_ui_label_colored(ui, x0 + 8, ry, (sdl3d_color){140, 180, 255, 255}, "Entity Properties");
    ry += 24;

    sdl3d_ui_label(ui, x0 + 12, ry, "Classname:");
    sdl3d_ui_label_colored(ui, x0 + 100, ry, (sdl3d_color){200, 200, 200, 255}, "worldspawn");
    ry += 20;

    sdl3d_ui_label(ui, x0 + 12, ry, "Origin:");
    sdl3d_ui_label_colored(ui, x0 + 100, ry, (sdl3d_color){200, 200, 200, 255}, "0 0 0");
    ry += 20;

    sdl3d_ui_label(ui, x0 + 12, ry, "Angle:");
    sdl3d_ui_label_colored(ui, x0 + 100, ry, (sdl3d_color){200, 200, 200, 255}, "0");
    ry += 28;

    /* Section: Active tool */
    sdl3d_ui_draw_rect(ui, x0 + 8, ry, rw, 1, (sdl3d_color){70, 70, 75, 255});
    ry += 6;
    sdl3d_ui_label_colored(ui, x0 + 8, ry, (sdl3d_color){140, 180, 255, 255}, "Tool State");
    ry += 24;

    sdl3d_ui_labelf(ui, x0 + 12, ry, "Active: %s", active_tool);
    ry += 20;
    sdl3d_ui_labelf(ui, x0 + 12, ry, "Clicks: %d", click_count);
    ry += 28;

    /* Action buttons */
    sdl3d_ui_draw_rect(ui, x0 + 8, ry, rw, 1, (sdl3d_color){70, 70, 75, 255});
    ry += 6;
    sdl3d_ui_label_colored(ui, x0 + 8, ry, (sdl3d_color){140, 180, 255, 255}, "Actions");
    ry += 24;

    sdl3d_ui_button(ui, x0 + 8, ry, rw, BTN_H, "Apply##insp1");
    ry += BTN_H + BTN_PAD;
    sdl3d_ui_button(ui, x0 + 8, ry, rw, BTN_H, "Reset##insp2");
    ry += BTN_H + BTN_PAD;
    sdl3d_ui_button(ui, x0 + 8, ry, rw, BTN_H, "Delete##insp3");

    sdl3d_ui_pop_clip(ui);
}

static void draw_status_bar(sdl3d_ui_context *ui, float y0, float w, float dt, const char *active_tool)
{
    static float smoothed_fps = 0.0f;
    sdl3d_ui_draw_rect(ui, 0, y0, w, STATUS_H, (sdl3d_color){35, 35, 38, 255});
    sdl3d_ui_draw_rect_outline(ui, 0, y0, w, STATUS_H, 1.0f, (sdl3d_color){70, 70, 75, 255});

    if (dt > 0.0f)
        smoothed_fps += (1.0f / dt - smoothed_fps) * 0.05f;
    int fps = (int)(smoothed_fps + 0.5f);
    sdl3d_ui_labelf(ui, 8, y0 + 6, "Tool: %-8s |  FPS: %4d  |  Grid: 16  |  Snap: On", active_tool, fps);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *win = SDL_CreateWindow("SDL3D \xe2\x80\x94 UI Editor Demo", WINDOW_W, WINDOW_H,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win)
        return 1;

    sdl3d_render_context_config cfg;
    sdl3d_init_render_context_config(&cfg);
    cfg.backend = SDL3D_BACKEND_SDLGPU;
    cfg.logical_width = WINDOW_W;
    cfg.logical_height = WINDOW_H;

    sdl3d_render_context *ctx = NULL;
    if (!sdl3d_create_render_context(win, NULL, &cfg, &ctx))
    {
        SDL_Log("Render context failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        return 1;
    }

    sdl3d_set_bloom_enabled(ctx, false);
    sdl3d_set_ssao_enabled(ctx, false);

    /* Load a UI font. */
    sdl3d_font ui_font;
    if (!sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, SDL3D_BUILTIN_FONT_INTER, 16.0f, &ui_font))
    {
        SDL_Log("Font load failed: %s", SDL_GetError());
        return 1;
    }

    sdl3d_ui_context *ui = NULL;
    sdl3d_ui_create(&ui_font, &ui);

    const char *active_tool = "Select";
    int click_count = 0;
    float elapsed = 0.0f;
    Uint64 last = SDL_GetPerformanceCounter();
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
            sdl3d_ui_process_event(ui, &ev);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;
        elapsed += dt;

        float sw = (float)sdl3d_get_render_context_width(ctx);
        float sh = (float)sdl3d_get_render_context_height(ctx);
        float viewport_y = MENU_H;
        float viewport_h = sh - MENU_H - STATUS_H;
        float viewport_x = TOOL_W;
        float viewport_w = sw - TOOL_W - INSPECTOR_W;

        sdl3d_clear_render_context(ctx, (sdl3d_color){50, 50, 55, 255});

        /* 3D viewport: spinning cube in the center area. */
        sdl3d_camera3d cam;
        cam.position = sdl3d_vec3_make(sinf(elapsed * 0.4f) * 5.0f, 3.0f, cosf(elapsed * 0.4f) * 5.0f);
        cam.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
        cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
        cam.fovy = 60.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        sdl3d_begin_mode_3d(ctx, cam);
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(1.5f, 1.5f, 1.5f),
                        (sdl3d_color){80, 160, 255, 255});
        sdl3d_draw_cube_wires(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(1.55f, 1.55f, 1.55f),
                              (sdl3d_color){200, 200, 200, 255});
        /* Grid floor */
        for (int i = -5; i <= 5; i++)
        {
            sdl3d_color gc = {60, 60, 65, 255};
            sdl3d_draw_line_3d(ctx, sdl3d_vec3_make((float)i, -1.0f, -5.0f), sdl3d_vec3_make((float)i, -1.0f, 5.0f),
                               gc);
            sdl3d_draw_line_3d(ctx, sdl3d_vec3_make(-5.0f, -1.0f, (float)i), sdl3d_vec3_make(5.0f, -1.0f, (float)i),
                               gc);
        }
        sdl3d_end_mode_3d(ctx);

        /* UI overlay — all panels rendered via the overlay layer. */
        sdl3d_ui_begin_frame(ui, (int)sw, (int)sh);

        /* Compute window-to-logical mouse mapping for letterbox scaling.
         * The GL renderer maps the logical FBO into a letterbox viewport;
         * SDL reports mouse in window points, so we reverse that mapping. */
        {
            int win_pw, win_ph;
            SDL_GetWindowSizeInPixels(win, &win_pw, &win_ph);
            float sx = (float)win_pw / sw;
            float sy = (float)win_ph / sh;
            float s = (sx < sy) ? sx : sy;
            float vp_w = sw * s;
            float vp_h = sh * s;
            float vp_x = ((float)win_pw - vp_w) * 0.5f;
            float vp_y = ((float)win_ph - vp_h) * 0.5f;
            /* SDL mouse coords are in window points (pixels / density). */
            float pdpi = SDL_GetWindowPixelDensity(win);
            if (pdpi < 1.0f)
                pdpi = 1.0f;
            sdl3d_ui_set_mouse_transform(ui, sw / (vp_w / pdpi), sh / (vp_h / pdpi), vp_x / pdpi, vp_y / pdpi);
        }

        draw_menu_bar(ui, sw, &click_count);
        draw_tool_panel(ui, viewport_y, viewport_h, &active_tool, &click_count);
        draw_inspector_panel(ui, sw - INSPECTOR_W, viewport_y, viewport_h, active_tool, click_count);
        draw_status_bar(ui, sh - STATUS_H, sw, dt, active_tool);

        /* Viewport border */
        sdl3d_ui_draw_rect_outline(ui, viewport_x, viewport_y, viewport_w, viewport_h, 1.0f,
                                   (sdl3d_color){70, 70, 75, 255});

        sdl3d_ui_end_frame(ui);
        sdl3d_ui_render(ui, ctx);

        sdl3d_present_render_context(ctx);
    }

    sdl3d_ui_destroy(ui);
    sdl3d_free_font(&ui_font);
    sdl3d_destroy_render_context(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

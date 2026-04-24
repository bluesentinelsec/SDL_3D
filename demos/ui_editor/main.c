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

static void update_ui_mouse_transform(sdl3d_ui_context *ui, SDL_Window *win, float logical_w, float logical_h)
{
    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSize(win, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0 || logical_w <= 0.0f || logical_h <= 0.0f)
    {
        sdl3d_ui_set_mouse_transform(ui, 1.0f, 1.0f, 0.0f, 0.0f);
        return;
    }

    const float sx = (float)win_w / logical_w;
    const float sy = (float)win_h / logical_h;
    const float s = (sx < sy) ? sx : sy;
    const float vp_w = logical_w * s;
    const float vp_h = logical_h * s;
    const float vp_x = ((float)win_w - vp_w) * 0.5f;
    const float vp_y = ((float)win_h - vp_h) * 0.5f;

    sdl3d_ui_set_mouse_transform(ui, logical_w / vp_w, logical_h / vp_h, vp_x, vp_y);
}

static void draw_menu_bar(sdl3d_ui_context *ui, float w, int *click_count)
{
    sdl3d_ui_draw_rect(ui, 0, 0, w, MENU_H, (sdl3d_color){45, 45, 48, 255});
    sdl3d_ui_draw_rect_outline(ui, 0, 0, w, MENU_H, 1.0f, (sdl3d_color){70, 70, 75, 255});

    sdl3d_ui_begin_hbox(ui, 4, 2, w - 8, BTN_H);
    if (sdl3d_ui_layout_button(ui, "File"))
        (*click_count)++;
    if (sdl3d_ui_layout_button(ui, "Edit"))
        (*click_count)++;
    if (sdl3d_ui_layout_button(ui, "View"))
        (*click_count)++;
    if (sdl3d_ui_layout_button(ui, "Map"))
        (*click_count)++;
    if (sdl3d_ui_layout_button(ui, "Help"))
        (*click_count)++;
    sdl3d_ui_end_hbox(ui);
}

static void draw_tool_panel(sdl3d_ui_context *ui, float y0, float h, const char **active_tool, int *click_count)
{
    float pad = 8.0f;
    sdl3d_ui_begin_panel(ui, 0, y0, TOOL_W, h);
    sdl3d_ui_begin_vbox(ui, pad, y0 + pad, TOOL_W - pad * 2, h - pad * 2);

    sdl3d_ui_layout_label(ui, "Tools");
    sdl3d_ui_separator(ui);

    const char *tools[] = {"Select", "Move", "Rotate", "Scale", "Clip", "Vertex", "Entity"};
    int tool_count = (int)(sizeof(tools) / sizeof(tools[0]));

    for (int i = 0; i < tool_count; i++)
    {
        if (sdl3d_ui_layout_button(ui, tools[i]))
        {
            *active_tool = tools[i];
            (*click_count)++;
        }
    }

    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_panel(ui);
}

typedef struct editor_state
{
    const char *active_tool;
    int click_count;
    bool *wireframe;
    bool *show_grid;
    bool *snap_enabled;
    float *orbit_speed;
    float *grid_size;
    float *light_intensity;
} editor_state;

static float inspector_scroll = 0.0f;

static void draw_inspector_panel(sdl3d_ui_context *ui, float x0, float y0, float h, const editor_state *st)
{
    float pad = 8.0f;
    float inner_w = INSPECTOR_W - pad * 2;
    float content_h = 500.0f; /* estimated total content height */

    sdl3d_ui_begin_panel(ui, x0, y0, INSPECTOR_W, h);
    sdl3d_ui_begin_scroll(ui, x0 + pad, y0 + pad, inner_w, h - pad * 2, &inspector_scroll, content_h);
    sdl3d_ui_begin_vbox(ui, x0 + pad, y0 + pad - inspector_scroll, inner_w, content_h);

    sdl3d_ui_layout_label(ui, "Inspector");
    sdl3d_ui_separator(ui);

    sdl3d_ui_layout_label(ui, "Display");
    sdl3d_ui_layout_checkbox(ui, "Wireframe", st->wireframe);
    sdl3d_ui_layout_checkbox(ui, "Show Grid", st->show_grid);
    sdl3d_ui_layout_checkbox(ui, "Snap", st->snap_enabled);
    sdl3d_ui_separator(ui);

    sdl3d_ui_layout_label(ui, "Properties");
    sdl3d_ui_layout_slider(ui, "Orbit Speed", st->orbit_speed, 0.1f, 2.0f);
    sdl3d_ui_layout_slider(ui, "Grid Size", st->grid_size, 1.0f, 64.0f);
    sdl3d_ui_layout_slider(ui, "Light", st->light_intensity, 0.0f, 2.0f);
    sdl3d_ui_separator(ui);

    sdl3d_ui_layout_label(ui, "Tool State");
    sdl3d_ui_layout_labelf(ui, "  Active: %s", st->active_tool);
    sdl3d_ui_layout_labelf(ui, "  Clicks: %d", st->click_count);
    sdl3d_ui_separator(ui);

    sdl3d_ui_layout_label(ui, "Actions");
    sdl3d_ui_layout_button(ui, "Apply##insp1");
    sdl3d_ui_layout_button(ui, "Reset##insp2");
    sdl3d_ui_layout_button(ui, "Delete##insp3");

    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_scroll(ui);
    sdl3d_ui_end_panel(ui);
}

static void draw_status_bar(sdl3d_ui_context *ui, float y0, float w, float dt, const char *active_tool, bool snap_on,
                            float grid_sz)
{
    static float smoothed_fps = 0.0f;
    sdl3d_ui_draw_rect(ui, 0, y0, w, STATUS_H, (sdl3d_color){35, 35, 38, 255});
    sdl3d_ui_draw_rect_outline(ui, 0, y0, w, STATUS_H, 1.0f, (sdl3d_color){70, 70, 75, 255});

    if (dt > 0.0f)
        smoothed_fps += (1.0f / dt - smoothed_fps) * 0.05f;
    int fps = (int)(smoothed_fps + 0.5f);

    float ty = y0 + 6;
    sdl3d_ui_labelf(ui, 8, ty, "Tool: %s", active_tool);
    sdl3d_ui_labelf(ui, 200, ty, "FPS: %d", fps);
    sdl3d_ui_labelf(ui, 320, ty, "Grid: %.0f", (double)grid_sz);
    sdl3d_ui_labelf(ui, 440, ty, "Snap: %s", snap_on ? "On" : "Off");
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
    bool wireframe = false;
    bool show_grid = true;
    bool snap_enabled = true;
    float orbit_speed = 0.4f;
    float grid_size = 16.0f;
    float light_intensity = 1.0f;
    float elapsed = 0.0f;
    Uint64 last = SDL_GetPerformanceCounter();
    bool running = true;

    while (running)
    {
        float sw = (float)sdl3d_get_render_context_width(ctx);
        float sh = (float)sdl3d_get_render_context_height(ctx);

        /* Begin the UI frame before routing events so edge-triggered input,
         * hover state, and window-to-logical mouse mapping are all computed
         * against the current frame's logical dimensions. SDL mouse events
         * are reported in window coordinates, not window pixels, so the
         * transform must use SDL_GetWindowSize() rather than pixel density. */
        sdl3d_ui_begin_frame(ui, (int)sw, (int)sh);
        update_ui_mouse_transform(ui, win, sw, sh);

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

        float viewport_y = MENU_H;
        float viewport_h = sh - MENU_H - STATUS_H;
        float viewport_x = TOOL_W;
        float viewport_w = sw - TOOL_W - INSPECTOR_W;

        sdl3d_clear_render_context(ctx, (sdl3d_color){50, 50, 55, 255});

        /* 3D viewport: spinning cube in the center area. */
        sdl3d_camera3d cam;
        cam.position = sdl3d_vec3_make(sinf(elapsed * orbit_speed) * 5.0f, 3.0f, cosf(elapsed * orbit_speed) * 5.0f);
        cam.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
        cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
        cam.fovy = 60.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        sdl3d_begin_mode_3d(ctx, cam);
        if (wireframe)
        {
            sdl3d_draw_cube_wires(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(1.5f, 1.5f, 1.5f),
                                  (sdl3d_color){80, 160, 255, 255});
        }
        else
        {
            sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(1.5f, 1.5f, 1.5f),
                            (sdl3d_color){80, 160, 255, 255});
            sdl3d_draw_cube_wires(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(1.55f, 1.55f, 1.55f),
                                  (sdl3d_color){200, 200, 200, 255});
        }
        /* Grid floor */
        if (show_grid)
        {
            int steps = (int)grid_size;
            if (steps < 1)
                steps = 1;
            if (steps > 64)
                steps = 64;
            float half = (float)steps * 0.5f;
            for (int i = 0; i <= steps; i++)
            {
                float pos = -half + (float)i;
                sdl3d_color gc = {60, 60, 65, 255};
                sdl3d_draw_line_3d(ctx, sdl3d_vec3_make(pos, -1.0f, -half), sdl3d_vec3_make(pos, -1.0f, half), gc);
                sdl3d_draw_line_3d(ctx, sdl3d_vec3_make(-half, -1.0f, pos), sdl3d_vec3_make(half, -1.0f, pos), gc);
            }
        }
        sdl3d_end_mode_3d(ctx);

        /* UI overlay — all panels rendered via the overlay layer. */
        editor_state st = {active_tool,   click_count,  &wireframe, &show_grid,
                           &snap_enabled, &orbit_speed, &grid_size, &light_intensity};

        draw_menu_bar(ui, sw, &click_count);
        draw_tool_panel(ui, viewport_y, viewport_h, &active_tool, &click_count);
        draw_inspector_panel(ui, sw - INSPECTOR_W, viewport_y, viewport_h, &st);
        draw_status_bar(ui, sh - STATUS_H, sw, dt, active_tool, snap_enabled, grid_size);

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

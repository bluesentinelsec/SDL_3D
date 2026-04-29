/*
 * UI Editor Demo — TrenchBroom-style mockup that exercises every
 * available UI widget and draw primitive.  All widgets are wired to
 * meaningful state so correctness can be validated visually.
 *
 * Layout:
 *   ┌──────────────────────────────────────────────┐
 *   │  Menu Bar                                     │
 *   ├────────┬─────────────────────────┬────────────┤
 *   │        │                         │            │
 *   │  Tool  │   3D Viewport           │  Inspector │
 *   │  Panel │                         │  Panel     │
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
#include <stdlib.h>

#define WINDOW_W 1280
#define WINDOW_H 720
#define MENU_H 32.0f
#define STATUS_H 28.0f
#define TOOL_W 160.0f
#define INSPECTOR_W 240.0f
#define BTN_H 28.0f

/* ------------------------------------------------------------------ */
/* Editor state                                                        */
/* ------------------------------------------------------------------ */

typedef struct editor_state
{
    /* Tool */
    const char *active_tool;
    int active_tool_index;
    int click_count;

    /* Entity */
    int entity_type;
    char classname[64];
    char origin[64];
    float cube_pos[3]; /* parsed from origin */
    bool entity_visible;

    /* Display */
    bool wireframe;
    bool show_grid;
    bool snap_enabled;
    bool show_axes;
    float orbit_speed;
    float grid_size;
    float cube_scale;
    float light_intensity;

    /* Inspector */
    int inspector_tab;
    char log_msg[128]; /* status feedback from actions */

    /* Scene hierarchy (tree view) */
    bool tree_root_expanded;
    bool tree_geometry_expanded;
    bool tree_lights_expanded;
    bool tree_triggers_expanded;
    int tree_selected;

    /* Texture list (list view) */
    int texture_selected;
    float texture_scroll;

    /* Backend display */
    const char *backend_name;
} editor_state;

static void editor_reset(editor_state *st)
{
    st->active_tool = "Select";
    st->active_tool_index = 0;
    st->click_count = 0;
    st->entity_type = 0;
    SDL_strlcpy(st->classname, "worldspawn", sizeof(st->classname));
    SDL_strlcpy(st->origin, "0 0 0", sizeof(st->origin));
    st->cube_pos[0] = st->cube_pos[1] = st->cube_pos[2] = 0.0f;
    st->entity_visible = true;
    st->wireframe = false;
    st->show_grid = true;
    st->snap_enabled = true;
    st->show_axes = true;
    st->orbit_speed = 0.4f;
    st->grid_size = 16.0f;
    st->cube_scale = 1.5f;
    st->light_intensity = 1.0f;
    st->inspector_tab = 0;
    st->log_msg[0] = '\0';
    st->tree_root_expanded = true;
    st->tree_geometry_expanded = true;
    st->tree_lights_expanded = false;
    st->tree_triggers_expanded = false;
    st->tree_selected = 1;
    st->texture_selected = 0;
    st->texture_scroll = 0.0f;
    st->backend_name = "GL";
}

static void parse_origin(editor_state *st)
{
    float x = 0, y = 0, z = 0;
    if (SDL_sscanf(st->origin, "%f %f %f", &x, &y, &z) >= 1)
    {
        st->cube_pos[0] = x;
        st->cube_pos[1] = y;
        st->cube_pos[2] = z;
    }
}

static const char *entity_types[] = {"worldspawn", "info_player_start", "light", "func_door", "trigger_once"};
static const sdl3d_color entity_colors[] = {
    {80, 160, 255, 255}, /* worldspawn: blue */
    {80, 255, 80, 255},  /* info_player_start: green */
    {255, 255, 80, 255}, /* light: yellow */
    {200, 100, 50, 255}, /* func_door: brown */
    {255, 80, 80, 255},  /* trigger_once: red */
};

/* ------------------------------------------------------------------ */
/* Menu bar                                                            */
/* ------------------------------------------------------------------ */

static void draw_menu_bar(sdl3d_ui_context *ui, float w, editor_state *st)
{
    sdl3d_ui_draw_rect(ui, 0, 0, w, MENU_H, (sdl3d_color){45, 45, 48, 255});
    sdl3d_ui_draw_rect_outline(ui, 0, 0, w, MENU_H, 1.0f, (sdl3d_color){70, 70, 75, 255});

    sdl3d_ui_begin_hbox(ui, 4, 2, w - 8, BTN_H);
    if (sdl3d_ui_layout_button(ui, "File"))
        SDL_strlcpy(st->log_msg, "File menu clicked", sizeof(st->log_msg));
    if (sdl3d_ui_layout_button(ui, "Edit"))
        SDL_strlcpy(st->log_msg, "Edit menu clicked", sizeof(st->log_msg));
    if (sdl3d_ui_layout_button(ui, "View"))
        SDL_strlcpy(st->log_msg, "View menu clicked", sizeof(st->log_msg));
    if (sdl3d_ui_layout_button(ui, "Map"))
        SDL_strlcpy(st->log_msg, "Map menu clicked", sizeof(st->log_msg));
    if (sdl3d_ui_layout_button(ui, "Help"))
        SDL_strlcpy(st->log_msg, "Help menu clicked", sizeof(st->log_msg));
    sdl3d_ui_end_hbox(ui);
}

/* ------------------------------------------------------------------ */
/* Tool panel (scrollable)                                             */
/* ------------------------------------------------------------------ */

static float tool_scroll = 0.0f;
static const char *tools[] = {"Select", "Move", "Rotate", "Scale", "Clip", "Vertex", "Entity"};
#define TOOL_COUNT 7

static void draw_tool_panel(sdl3d_ui_context *ui, float y0, float h, editor_state *st)
{
    float pad = 8.0f;
    float content_h = 400.0f;
    sdl3d_ui_begin_panel(ui, 0, y0, TOOL_W, h);
    sdl3d_ui_begin_scroll(ui, pad, y0 + pad, TOOL_W - pad * 2, h - pad * 2, &tool_scroll, content_h);
    sdl3d_ui_begin_vbox(ui, pad, y0 + pad - tool_scroll, TOOL_W - pad * 2, content_h);

    sdl3d_ui_layout_label(ui, "Tools");
    sdl3d_ui_separator(ui);

    for (int i = 0; i < TOOL_COUNT; i++)
    {
        if (sdl3d_ui_layout_button(ui, tools[i]))
        {
            st->active_tool = tools[i];
            st->active_tool_index = i;
            st->click_count++;
            SDL_snprintf(st->log_msg, sizeof(st->log_msg), "Tool: %s", tools[i]);
        }
    }

    sdl3d_ui_separator(ui);
    sdl3d_ui_layout_label(ui, "Quick Actions");
    if (sdl3d_ui_layout_button(ui, "Center View"))
    {
        SDL_strlcpy(st->origin, "0 0 0", sizeof(st->origin));
        parse_origin(st);
        SDL_strlcpy(st->log_msg, "View centered", sizeof(st->log_msg));
    }
    if (sdl3d_ui_layout_button(ui, "Toggle Wire"))
    {
        st->wireframe = !st->wireframe;
        SDL_strlcpy(st->log_msg, st->wireframe ? "Wireframe ON" : "Wireframe OFF", sizeof(st->log_msg));
    }

    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_scroll(ui);
    sdl3d_ui_end_panel(ui);
}

/* ------------------------------------------------------------------ */
/* Inspector panel (tabbed, scrollable)                                */
/* ------------------------------------------------------------------ */

static float inspector_scroll = 0.0f;
static const char *inspector_tabs[] = {"Entity", "Display", "Scene", "Textures"};
static const char *texture_names[] = {"brick_wall", "concrete",    "metal_floor",   "wood_planks", "tile_mosaic",
                                      "grass",      "stone_rough", "plaster_white", "rust_panel",  "glass_tint"};

static void draw_inspector_panel(sdl3d_ui_context *ui, float x0, float y0, float h, editor_state *st)
{
    float pad = 8.0f;
    float inner_w = INSPECTOR_W - pad * 2;
    float content_h = 600.0f;

    sdl3d_ui_begin_panel(ui, x0, y0, INSPECTOR_W, h);
    sdl3d_ui_tab_strip(ui, x0 + pad, y0 + pad, inner_w, 26.0f, inspector_tabs, 4, &st->inspector_tab);
    float tab_h = 26.0f + pad;

    sdl3d_ui_begin_scroll(ui, x0 + pad, y0 + pad + tab_h, inner_w, h - pad * 2 - tab_h, &inspector_scroll, content_h);
    sdl3d_ui_begin_vbox(ui, x0 + pad, y0 + pad + tab_h - inspector_scroll, inner_w, content_h);

    if (st->inspector_tab == 0)
    {
        /* Entity tab — uses inspector row layout */
        sdl3d_ui_layout_label(ui, "Entity Type");
        int prev_type = st->entity_type;
        sdl3d_ui_layout_dropdown(ui, entity_types, 5, &st->entity_type);
        if (st->entity_type != prev_type)
        {
            SDL_strlcpy(st->classname, entity_types[st->entity_type], sizeof(st->classname));
            SDL_snprintf(st->log_msg, sizeof(st->log_msg), "Entity: %s", entity_types[st->entity_type]);
        }
        sdl3d_ui_separator(ui);

        sdl3d_ui_layout_label(ui, "Properties");
        sdl3d_ui_row_text_field(ui, "Classname:", st->classname, sizeof(st->classname));
        if (sdl3d_ui_row_text_field(ui, "Origin:", st->origin, sizeof(st->origin)))
        {
            parse_origin(st);
            SDL_snprintf(st->log_msg, sizeof(st->log_msg), "Origin: %.1f %.1f %.1f", (double)st->cube_pos[0],
                         (double)st->cube_pos[1], (double)st->cube_pos[2]);
        }
        sdl3d_ui_row_checkbox(ui, "Visible:", &st->entity_visible);
        sdl3d_ui_separator(ui);

        sdl3d_ui_layout_label(ui, "Info");
        sdl3d_ui_row_label(ui, "Tool:", st->active_tool);
        {
            char buf[32];
            SDL_snprintf(buf, sizeof(buf), "%d", st->click_count);
            sdl3d_ui_row_label(ui, "Clicks:", buf);
        }
        {
            char buf[64];
            SDL_snprintf(buf, sizeof(buf), "%.1f %.1f %.1f", (double)st->cube_pos[0], (double)st->cube_pos[1],
                         (double)st->cube_pos[2]);
            sdl3d_ui_row_label(ui, "Position:", buf);
        }
        sdl3d_ui_separator(ui);

        sdl3d_ui_layout_label(ui, "Actions");
        if (sdl3d_ui_layout_button(ui, "Apply##insp1"))
        {
            parse_origin(st);
            SDL_snprintf(st->log_msg, sizeof(st->log_msg), "Applied: %s at %.1f,%.1f,%.1f", st->classname,
                         (double)st->cube_pos[0], (double)st->cube_pos[1], (double)st->cube_pos[2]);
        }
        if (sdl3d_ui_layout_button(ui, "Reset##insp2"))
        {
            int tab = st->inspector_tab;
            editor_reset(st);
            st->inspector_tab = tab;
            SDL_strlcpy(st->log_msg, "Reset to defaults", sizeof(st->log_msg));
        }
        if (sdl3d_ui_layout_button(ui, "Delete##insp3"))
        {
            st->entity_visible = false;
            SDL_strlcpy(st->log_msg, "Entity deleted (hidden)", sizeof(st->log_msg));
        }
    }
    else if (st->inspector_tab == 1)
    {
        /* Display tab — uses inspector rows for sliders and checkboxes */
        sdl3d_ui_layout_label(ui, "Rendering");
        sdl3d_ui_row_checkbox(ui, "Wireframe:", &st->wireframe);
        sdl3d_ui_row_checkbox(ui, "Show Grid:", &st->show_grid);
        sdl3d_ui_row_checkbox(ui, "Show Axes:", &st->show_axes);
        sdl3d_ui_row_checkbox(ui, "Snap:", &st->snap_enabled);
        sdl3d_ui_separator(ui);

        sdl3d_ui_layout_label(ui, "Camera");
        sdl3d_ui_row_slider(ui, "Orbit:", &st->orbit_speed, 0.1f, 2.0f);
        sdl3d_ui_separator(ui);

        sdl3d_ui_layout_label(ui, "Scene");
        sdl3d_ui_row_slider(ui, "Grid:", &st->grid_size, 2.0f, 64.0f);
        sdl3d_ui_row_slider(ui, "Scale:", &st->cube_scale, 0.5f, 5.0f);
        sdl3d_ui_row_slider(ui, "Light:", &st->light_intensity, 0.0f, 2.0f);
    }
    else if (st->inspector_tab == 2)
    {
        /* Scene tab — tree view hierarchy */
        sdl3d_ui_layout_label(ui, "Scene Hierarchy");
        sdl3d_ui_separator(ui);

        sdl3d_ui_tree_node(ui, "World", 1, &st->tree_root_expanded, &st->tree_selected);
        if (st->tree_root_expanded)
        {
            sdl3d_ui_tree_push(ui);

            sdl3d_ui_tree_node(ui, "Geometry", 10, &st->tree_geometry_expanded, &st->tree_selected);
            if (st->tree_geometry_expanded)
            {
                sdl3d_ui_tree_push(ui);
                bool leaf = false;
                sdl3d_ui_tree_node(ui, "Floor", 101, &leaf, &st->tree_selected);
                sdl3d_ui_tree_node(ui, "Walls", 102, &leaf, &st->tree_selected);
                sdl3d_ui_tree_node(ui, "Ceiling", 103, &leaf, &st->tree_selected);
                sdl3d_ui_tree_node(ui, "Cube", 104, &leaf, &st->tree_selected);
                sdl3d_ui_tree_pop(ui);
            }

            sdl3d_ui_tree_node(ui, "Lights", 20, &st->tree_lights_expanded, &st->tree_selected);
            if (st->tree_lights_expanded)
            {
                sdl3d_ui_tree_push(ui);
                bool leaf = false;
                sdl3d_ui_tree_node(ui, "Sun", 201, &leaf, &st->tree_selected);
                sdl3d_ui_tree_node(ui, "Point Light 1", 202, &leaf, &st->tree_selected);
                sdl3d_ui_tree_pop(ui);
            }

            sdl3d_ui_tree_node(ui, "Triggers", 30, &st->tree_triggers_expanded, &st->tree_selected);
            if (st->tree_triggers_expanded)
            {
                sdl3d_ui_tree_push(ui);
                bool leaf = false;
                sdl3d_ui_tree_node(ui, "trigger_once_1", 301, &leaf, &st->tree_selected);
                sdl3d_ui_tree_pop(ui);
            }

            sdl3d_ui_tree_pop(ui);
        }

        sdl3d_ui_separator(ui);
        {
            char buf[64];
            SDL_snprintf(buf, sizeof(buf), "Selected: %d", st->tree_selected);
            sdl3d_ui_layout_label(ui, buf);
        }
    }
    else
    {
        /* Textures tab — list view */
        sdl3d_ui_layout_label(ui, "Texture Browser");
        sdl3d_ui_separator(ui);

        if (sdl3d_ui_layout_list_view(ui, 200.0f, texture_names, 10, &st->texture_selected, &st->texture_scroll))
        {
            SDL_snprintf(st->log_msg, sizeof(st->log_msg), "Texture: %s", texture_names[st->texture_selected]);
        }
        sdl3d_ui_separator(ui);
        sdl3d_ui_row_label(ui, "Selected:", texture_names[st->texture_selected]);
    }

    sdl3d_ui_end_vbox(ui);
    sdl3d_ui_end_scroll(ui);
    sdl3d_ui_end_panel(ui);
}

/* ------------------------------------------------------------------ */
/* Status bar                                                          */
/* ------------------------------------------------------------------ */

static void draw_status_bar(sdl3d_ui_context *ui, float y0, float w, float dt, const editor_state *st)
{
    static float smoothed_fps = 0.0f;
    sdl3d_ui_draw_rect(ui, 0, y0, w, STATUS_H, (sdl3d_color){35, 35, 38, 255});
    sdl3d_ui_draw_rect_outline(ui, 0, y0, w, STATUS_H, 1.0f, (sdl3d_color){70, 70, 75, 255});

    if (dt > 0.0f)
        smoothed_fps += (1.0f / dt - smoothed_fps) * 0.05f;
    int fps = (int)(smoothed_fps + 0.5f);

    float ty = y0 + 6;
    sdl3d_ui_labelf(ui, 8, ty, "Tool: %s", st->active_tool);
    sdl3d_ui_labelf(ui, 180, ty, "FPS: %d", fps);
    sdl3d_ui_labelf(ui, 280, ty, "Grid: %.0f", (double)st->grid_size);
    sdl3d_ui_labelf(ui, 380, ty, "Snap: %s", st->snap_enabled ? "On" : "Off");
    sdl3d_ui_labelf(ui, 500, ty, "[%s]", st->backend_name);
    /* Show last action feedback */
    if (st->log_msg[0])
        sdl3d_ui_label_colored(ui, 580, ty, (sdl3d_color){140, 255, 140, 255}, st->log_msg);
}

/* ------------------------------------------------------------------ */
/* 3D scene                                                            */
/* ------------------------------------------------------------------ */

static void draw_scene(sdl3d_render_context *ctx, float elapsed, const editor_state *st)
{
    sdl3d_camera3d cam;
    cam.position =
        sdl3d_vec3_make(sinf(elapsed * st->orbit_speed) * 5.0f, 3.0f, cosf(elapsed * st->orbit_speed) * 5.0f);
    cam.target = sdl3d_vec3_make(st->cube_pos[0], st->cube_pos[1], st->cube_pos[2]);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_begin_mode_3d(ctx, cam);

    /* Entity cube */
    if (st->entity_visible)
    {
        sdl3d_vec3 pos = sdl3d_vec3_make(st->cube_pos[0], st->cube_pos[1], st->cube_pos[2]);
        sdl3d_vec3 size = sdl3d_vec3_make(st->cube_scale, st->cube_scale, st->cube_scale);
        sdl3d_color col = entity_colors[st->entity_type];

        /* Scale color by light intensity. */
        col.r = (Uint8)((float)col.r * st->light_intensity);
        col.g = (Uint8)((float)col.g * st->light_intensity);
        col.b = (Uint8)((float)col.b * st->light_intensity);

        if (st->wireframe)
        {
            sdl3d_draw_cube_wires(ctx, pos, size, col);
        }
        else
        {
            sdl3d_draw_cube(ctx, pos, size, col);
            sdl3d_vec3 wire_size =
                sdl3d_vec3_make(st->cube_scale + 0.05f, st->cube_scale + 0.05f, st->cube_scale + 0.05f);
            sdl3d_draw_cube_wires(ctx, pos, wire_size, (sdl3d_color){200, 200, 200, 255});
        }
    }

    /* Axes */
    if (st->show_axes)
    {
        sdl3d_draw_line_3d(ctx, sdl3d_vec3_make(0, -1, 0), sdl3d_vec3_make(3, -1, 0), (sdl3d_color){255, 60, 60, 255});
        sdl3d_draw_line_3d(ctx, sdl3d_vec3_make(0, -1, 0), sdl3d_vec3_make(0, 2, 0), (sdl3d_color){60, 255, 60, 255});
        sdl3d_draw_line_3d(ctx, sdl3d_vec3_make(0, -1, 0), sdl3d_vec3_make(0, -1, 3), (sdl3d_color){60, 60, 255, 255});
    }

    /* Grid floor */
    if (st->show_grid)
    {
        int steps = (int)st->grid_size;
        if (steps < 2)
            steps = 2;
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
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;

    sdl3d_window_config wcfg;
    sdl3d_init_window_config(&wcfg);
    wcfg.width = WINDOW_W;
    wcfg.height = WINDOW_H;
    wcfg.title = "SDL3D \xe2\x80\x94 UI Editor Demo";

    SDL_Window *win = NULL;
    sdl3d_render_context *ctx = NULL;
    if (!sdl3d_create_window(&wcfg, &win, &ctx))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Window creation failed: %s", SDL_GetError());
        return 1;
    }
    SDL_StartTextInput(win);
    sdl3d_set_bloom_enabled(ctx, false);
    sdl3d_set_ssao_enabled(ctx, false);

    sdl3d_font ui_font;
    if (!sdl3d_load_builtin_font(SDL3D_MEDIA_DIR, SDL3D_BUILTIN_FONT_INTER, 16.0f, &ui_font))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Font load failed: %s", SDL_GetError());
        return 1;
    }

    sdl3d_ui_context *ui = NULL;
    sdl3d_ui_create(&ui_font, &ui);

    editor_state st;
    editor_reset(&st);
    st.backend_name = sdl3d_get_backend_name(sdl3d_get_render_context_backend(ctx));

    float elapsed = 0.0f;
    Uint64 last = SDL_GetPerformanceCounter();
    bool running = true;

    while (running)
    {
        float sw = (float)sdl3d_get_render_context_width(ctx);
        float sh = (float)sdl3d_get_render_context_height(ctx);

        sdl3d_ui_begin_frame_ex(ui, ctx, win);

        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
            {
                running = false;
                continue;
            }
            bool ui_consumed = sdl3d_ui_process_event(ui, &ev);
            if (!ui_consumed && ev.type == SDL_EVENT_KEY_DOWN)
            {
                if (ev.key.scancode == SDL_SCANCODE_ESCAPE)
                    running = false;
                if (ev.key.scancode == SDL_SCANCODE_GRAVE)
                {
                    sdl3d_backend cur = sdl3d_get_render_context_backend(ctx);
                    sdl3d_backend next = (cur == SDL3D_BACKEND_OPENGL) ? SDL3D_BACKEND_SOFTWARE : SDL3D_BACKEND_OPENGL;
                    if (sdl3d_switch_backend(&win, &ctx, next))
                    {
                        SDL_StartTextInput(win);
                        sdl3d_set_bloom_enabled(ctx, false);
                        sdl3d_set_ssao_enabled(ctx, false);
                        st.backend_name = sdl3d_get_backend_name(sdl3d_get_render_context_backend(ctx));
                        SDL_snprintf(st.log_msg, sizeof(st.log_msg), "Switched to %s", st.backend_name);
                    }
                    else
                    {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Backend switch failed: %s", SDL_GetError());
                        running = false;
                    }
                }
            }
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
        draw_scene(ctx, elapsed, &st);

        /* UI overlay */
        draw_menu_bar(ui, sw, &st);
        draw_tool_panel(ui, viewport_y, viewport_h, &st);
        draw_inspector_panel(ui, sw - INSPECTOR_W, viewport_y, viewport_h, &st);
        draw_status_bar(ui, sh - STATUS_H, sw, dt, &st);

        sdl3d_ui_draw_rect_outline(ui, viewport_x, viewport_y, viewport_w, viewport_h, 1.0f,
                                   (sdl3d_color){70, 70, 75, 255});

        sdl3d_ui_end_frame(ui);
        sdl3d_ui_render(ui, ctx);
        sdl3d_present_render_context(ctx);
    }

    sdl3d_ui_destroy(ui);
    sdl3d_free_font(&ui_font);
    sdl3d_destroy_render_context(ctx);
    SDL_StopTextInput(win);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

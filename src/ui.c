/*
 * SDL_3D immediate-mode UI core.
 *
 * Phase 1 implementation of issue #62: context, event routing, theme,
 * screen-space draw primitives, and the label widget.
 *
 * Rendering model: widget calls append commands into a per-frame draw
 * list. sdl3d_ui_render walks the list in order and submits everything
 * through the dedicated overlay layer (rects via sdl3d_draw_rect_overlay,
 * text via sdl3d_draw_text_overlay). This keeps UI rendering outside the
 * main 3D pipeline and post-processing path, which is critical for crisp
 * editor/UI output and for avoiding deferred-draw lifetime bugs.
 */

#include "sdl3d/ui.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/camera.h"
#include "sdl3d/drawing3d.h"
#include "sdl3d/font.h"
#include "sdl3d/lighting.h"
#include "sdl3d/math.h"
#include "sdl3d/render_context.h"
#include "sdl3d/types.h"

/* ------------------------------------------------------------------ */
/* Draw command model                                                  */
/* ------------------------------------------------------------------ */

typedef enum sdl3d_ui_cmd_kind
{
    SDL3D_UI_CMD_RECT = 0,
    SDL3D_UI_CMD_TEXT,
    SDL3D_UI_CMD_CLIP_PUSH,
    SDL3D_UI_CMD_CLIP_POP,
} sdl3d_ui_cmd_kind;

typedef struct sdl3d_ui_cmd
{
    sdl3d_ui_cmd_kind kind;
    float x, y, w, h;
    sdl3d_color color;
    int text_offset; /* byte offset into ctx->text_arena (-1 if none) */
} sdl3d_ui_cmd;

/* ------------------------------------------------------------------ */
/* Clip stack                                                          */
/* ------------------------------------------------------------------ */

#define SDL3D_UI_MAX_CLIP_DEPTH 16

typedef struct sdl3d_ui_clip_rect
{
    float x, y, w, h;
} sdl3d_ui_clip_rect;

/* ------------------------------------------------------------------ */
/* Layout types                                                        */
/* ------------------------------------------------------------------ */

#define SDL3D_UI_MAX_LAYOUT_DEPTH 16

typedef enum sdl3d_ui_layout_dir
{
    SDL3D_UI_LAYOUT_VBOX = 0,
    SDL3D_UI_LAYOUT_HBOX = 1
} sdl3d_ui_layout_dir;

typedef struct sdl3d_ui_layout_entry
{
    sdl3d_ui_layout_dir direction;
    float x, y, w, h; /* bounding region */
    float cursor;     /* current position along the layout axis */
    float cross_max;  /* tallest child (vbox) or widest child (hbox); tracked for future use */
    float spacing;    /* gap between items */
    bool first_item;  /* suppress spacing before the first item */
} sdl3d_ui_layout_entry;

/* ------------------------------------------------------------------ */
/* Context                                                             */
/* ------------------------------------------------------------------ */

struct sdl3d_ui_context
{
    const sdl3d_font *font;
    sdl3d_ui_theme theme;

    int screen_w, screen_h;
    float mouse_scale_x, mouse_scale_y; /* window-to-logical coordinate mapping */
    float mouse_offset_x, mouse_offset_y;
    bool frame_open;

    sdl3d_ui_input_state input;
    sdl3d_ui_id hovered_id;
    sdl3d_ui_id active_id;
    sdl3d_ui_id focused_id;

    /* Growable command buffer. */
    sdl3d_ui_cmd *cmds;
    int cmd_count;
    int cmd_capacity;

    /* Popup command buffer. Popups render after the main UI command list
     * so dropdowns and future menus appear above ordinary widgets. */
    sdl3d_ui_cmd *popup_cmds;
    int popup_cmd_count;
    int popup_cmd_capacity;

    /* Growable text arena — we copy user-supplied strings so the caller
     * doesn't have to keep them alive until render time. */
    char *text_arena;
    int text_len;
    int text_capacity;

    /* Clip stack, tracked at command-submission time so later queries
     * (e.g., hit testing) could also respect it in future phases. */
    sdl3d_ui_clip_rect clip_stack[SDL3D_UI_MAX_CLIP_DEPTH];
    int clip_depth;

    /* Active popup capture region. While a popup is open, widgets other
     * than its owner must not react to pointer input that lands within
     * the popup's bounds. */
    sdl3d_ui_id popup_owner_id;
    sdl3d_ui_clip_rect popup_rect;

    /* Active scroll region — set by begin_scroll, consumed by end_scroll. */
#define SDL3D_UI_MAX_SCROLL_DEPTH 8
    struct
    {
        float x, y, w, h;
        float *offset;
        float content_height;
    } scroll_stack[SDL3D_UI_MAX_SCROLL_DEPTH];
    int scroll_depth;

    /* Layout stack — panels and containers push layout entries that
     * auto-position child widgets. */
    sdl3d_ui_layout_entry layout_stack[SDL3D_UI_MAX_LAYOUT_DEPTH];
    int layout_depth;
};

/* ------------------------------------------------------------------ */
/* Utility helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * Grow a command buffer (main or popup) to hold at least `needed` entries.
 * Doubles capacity each time. Operates via pointer indirection so the
 * same function serves both ui->cmds and ui->popup_cmds.
 */
static bool ui_ensure_cmd_capacity(sdl3d_ui_cmd **cmds, int *capacity, int needed)
{
    if (needed <= *capacity)
    {
        return true;
    }
    int new_cap = *capacity > 0 ? *capacity : 64;
    while (new_cap < needed)
    {
        new_cap *= 2;
    }
    sdl3d_ui_cmd *new_cmds = (sdl3d_ui_cmd *)SDL_realloc(*cmds, (size_t)new_cap * sizeof(*new_cmds));
    if (!new_cmds)
    {
        SDL_OutOfMemory();
        return false;
    }
    *cmds = new_cmds;
    *capacity = new_cap;
    return true;
}

/*
 * Copy `text` into the growable text arena. Returns the byte offset
 * into ui->text_arena where the NUL-terminated copy starts, or -1 on
 * failure. The arena is shared by both main and popup command buffers.
 */
static int ui_push_text(sdl3d_ui_context *ui, const char *text)
{
    if (!text)
    {
        return -1;
    }
    int len = (int)SDL_strlen(text);
    int offset = ui->text_len;
    int needed = offset + len + 1;
    if (needed > ui->text_capacity)
    {
        int new_cap = ui->text_capacity > 0 ? ui->text_capacity : 256;
        while (new_cap < needed)
        {
            new_cap *= 2;
        }
        char *buf = (char *)SDL_realloc(ui->text_arena, (size_t)new_cap);
        if (!buf)
        {
            SDL_OutOfMemory();
            return -1;
        }
        ui->text_arena = buf;
        ui->text_capacity = new_cap;
    }
    SDL_memcpy(ui->text_arena + offset, text, (size_t)len);
    ui->text_arena[offset + len] = '\0';
    ui->text_len = offset + len + 1;
    return offset;
}

/* Append a command to the main draw list (rendered first). */
static sdl3d_ui_cmd *ui_push_cmd(sdl3d_ui_context *ui)
{
    if (!ui_ensure_cmd_capacity(&ui->cmds, &ui->cmd_capacity, ui->cmd_count + 1))
    {
        return NULL;
    }
    sdl3d_ui_cmd *cmd = &ui->cmds[ui->cmd_count++];
    SDL_zerop(cmd);
    cmd->text_offset = -1;
    return cmd;
}

/* Append a command to the popup draw list (rendered after main, so
 * dropdown menus and future popups appear above ordinary widgets). */
static sdl3d_ui_cmd *ui_push_popup_cmd(sdl3d_ui_context *ui)
{
    if (!ui_ensure_cmd_capacity(&ui->popup_cmds, &ui->popup_cmd_capacity, ui->popup_cmd_count + 1))
    {
        return NULL;
    }
    sdl3d_ui_cmd *cmd = &ui->popup_cmds[ui->popup_cmd_count++];
    SDL_zerop(cmd);
    cmd->text_offset = -1;
    return cmd;
}

static void ui_popup_draw_rect(sdl3d_ui_context *ui, float x, float y, float w, float h, sdl3d_color color)
{
    if (!ui || !ui->frame_open || w <= 0.0f || h <= 0.0f)
    {
        return;
    }
    sdl3d_ui_cmd *cmd = ui_push_popup_cmd(ui);
    if (!cmd)
    {
        return;
    }
    cmd->kind = SDL3D_UI_CMD_RECT;
    cmd->x = x;
    cmd->y = y;
    cmd->w = w;
    cmd->h = h;
    cmd->color = color;
}

static void ui_popup_draw_rect_outline(sdl3d_ui_context *ui, float x, float y, float w, float h, float thickness,
                                       sdl3d_color color)
{
    if (!ui || w <= 0.0f || h <= 0.0f || thickness <= 0.0f)
    {
        return;
    }
    ui_popup_draw_rect(ui, x, y, w, thickness, color);
    ui_popup_draw_rect(ui, x, y + h - thickness, w, thickness, color);
    ui_popup_draw_rect(ui, x, y + thickness, thickness, h - 2.0f * thickness, color);
    ui_popup_draw_rect(ui, x + w - thickness, y + thickness, thickness, h - 2.0f * thickness, color);
}

static void ui_popup_draw_text(sdl3d_ui_context *ui, float x, float y, const char *text, sdl3d_color color)
{
    if (!ui || !ui->frame_open || !text || !*text)
    {
        return;
    }
    sdl3d_ui_cmd *cmd = ui_push_popup_cmd(ui);
    if (!cmd)
    {
        return;
    }
    cmd->kind = SDL3D_UI_CMD_TEXT;
    cmd->x = x;
    cmd->y = y;
    cmd->color = color;
    cmd->text_offset = ui_push_text(ui, text);
}

static void ui_popup_push_clip(sdl3d_ui_context *ui, float x, float y, float w, float h)
{
    if (!ui || !ui->frame_open)
    {
        return;
    }
    sdl3d_ui_cmd *cmd = ui_push_popup_cmd(ui);
    if (!cmd)
    {
        return;
    }
    cmd->kind = SDL3D_UI_CMD_CLIP_PUSH;
    cmd->x = x;
    cmd->y = y;
    cmd->w = w;
    cmd->h = h;
}

static void ui_popup_pop_clip(sdl3d_ui_context *ui)
{
    if (!ui || !ui->frame_open)
    {
        return;
    }
    sdl3d_ui_cmd *cmd = ui_push_popup_cmd(ui);
    if (!cmd)
    {
        return;
    }
    cmd->kind = SDL3D_UI_CMD_CLIP_POP;
}

/*
 * Mark a screen region as owned by a popup (e.g., an open dropdown list).
 * While active, ui_is_hovering_for_id blocks hover for any widget whose
 * ID doesn't match `owner_id` when the mouse is inside this region.
 * Cleared automatically at the start of each frame in begin_frame.
 */
static void ui_activate_popup_capture(sdl3d_ui_context *ui, sdl3d_ui_id owner_id, float x, float y, float w, float h)
{
    if (!ui)
    {
        return;
    }
    ui->popup_owner_id = owner_id;
    ui->popup_rect.x = x;
    ui->popup_rect.y = y;
    ui->popup_rect.w = w;
    ui->popup_rect.h = h;
}

/*
 * Popup-aware hit test. Returns true if the mouse is inside (x,y,w,h)
 * AND the popup capture region (if any) doesn't block this widget.
 * Pass id=0 for non-interactive queries (e.g., scroll hover checks).
 * The popup owner's own ID always passes through.
 */
static bool ui_is_hovering_for_id(const sdl3d_ui_context *ui, sdl3d_ui_id id, float x, float y, float w, float h)
{
    if (!ui)
    {
        return false;
    }
    if (ui->popup_owner_id != 0 && id != ui->popup_owner_id &&
        sdl3d_ui_point_in_rect(ui->input.mouse_x, ui->input.mouse_y, ui->popup_rect.x, ui->popup_rect.y,
                               ui->popup_rect.w, ui->popup_rect.h))
    {
        return false;
    }
    return sdl3d_ui_point_in_rect(ui->input.mouse_x, ui->input.mouse_y, x, y, w, h);
}

/* ------------------------------------------------------------------ */
/* Theme                                                               */
/* ------------------------------------------------------------------ */

sdl3d_ui_theme sdl3d_ui_default_theme(void)
{
    sdl3d_ui_theme t;
    t.text = (sdl3d_color){230, 230, 235, 255};
    t.text_muted = (sdl3d_color){150, 150, 160, 255};
    t.panel_bg = (sdl3d_color){36, 40, 48, 235};
    t.panel_border = (sdl3d_color){18, 20, 24, 255};
    t.widget_bg = (sdl3d_color){60, 66, 78, 255};
    t.widget_hover = (sdl3d_color){82, 92, 108, 255};
    t.widget_active = (sdl3d_color){110, 130, 170, 255};
    t.widget_border = (sdl3d_color){24, 26, 30, 255};
    t.focus_ring = (sdl3d_color){120, 170, 255, 255};
    t.padding = 6.0f;
    t.spacing = 4.0f;
    t.border_width = 1.0f;
    return t;
}

const sdl3d_ui_theme *sdl3d_ui_get_theme(const sdl3d_ui_context *ui)
{
    return ui ? &ui->theme : NULL;
}

void sdl3d_ui_set_theme(sdl3d_ui_context *ui, const sdl3d_ui_theme *theme)
{
    if (ui && theme)
    {
        ui->theme = *theme;
    }
}

const sdl3d_font *sdl3d_ui_get_font(const sdl3d_ui_context *ui)
{
    return ui ? ui->font : NULL;
}

void sdl3d_ui_set_font(sdl3d_ui_context *ui, const sdl3d_font *font)
{
    if (ui)
    {
        ui->font = font;
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

bool sdl3d_ui_create(const sdl3d_font *font, sdl3d_ui_context **out_ui)
{
    if (!out_ui)
    {
        return SDL_InvalidParamError("out_ui");
    }
    sdl3d_ui_context *ui = (sdl3d_ui_context *)SDL_calloc(1, sizeof(*ui));
    if (!ui)
    {
        return SDL_OutOfMemory();
    }
    ui->font = font;
    ui->theme = sdl3d_ui_default_theme();
    *out_ui = ui;
    return true;
}

void sdl3d_ui_destroy(sdl3d_ui_context *ui)
{
    if (!ui)
    {
        return;
    }
    SDL_free(ui->cmds);
    SDL_free(ui->popup_cmds);
    SDL_free(ui->text_arena);
    SDL_free(ui);
}

void sdl3d_ui_begin_frame(sdl3d_ui_context *ui, int screen_w, int screen_h)
{
    if (!ui)
    {
        return;
    }
    ui->screen_w = screen_w;
    ui->screen_h = screen_h;
    ui->mouse_scale_x = 1.0f;
    ui->mouse_scale_y = 1.0f;
    ui->mouse_offset_x = 0.0f;
    ui->mouse_offset_y = 0.0f;
    ui->cmd_count = 0;
    ui->popup_cmd_count = 0;
    ui->text_len = 0;
    ui->clip_depth = 0;
    ui->scroll_depth = 0;
    ui->layout_depth = 0;
    ui->frame_open = true;
    ui->hovered_id = 0;
    ui->popup_owner_id = 0;
    ui->popup_rect = (sdl3d_ui_clip_rect){0, 0, 0, 0};

    /* Edge-triggered flags are cleared at frame start; process_event
     * raises them during the frame, and they're consumed by widgets in
     * the current frame only. */
    for (int i = 0; i < 3; i++)
    {
        ui->input.mouse_pressed[i] = false;
        ui->input.mouse_released[i] = false;
    }
    ui->input.text_input[0] = '\0';
    ui->input.text_input_len = 0;
    ui->input.scroll_y = 0.0f;
    ui->input.key_backspace = false;
    ui->input.key_enter = false;
    ui->input.key_escape = false;
}

void sdl3d_ui_set_mouse_transform(sdl3d_ui_context *ui, float scale_x, float scale_y, float offset_x, float offset_y)
{
    if (!ui)
        return;
    ui->mouse_scale_x = scale_x;
    ui->mouse_scale_y = scale_y;
    ui->mouse_offset_x = offset_x;
    ui->mouse_offset_y = offset_y;
}

void sdl3d_ui_end_frame(sdl3d_ui_context *ui)
{
    if (!ui)
    {
        return;
    }
    if (ui->clip_depth != 0)
    {
        SDL_Log("sdl3d_ui_end_frame: clip stack not fully popped (depth=%d)", ui->clip_depth);
        ui->clip_depth = 0;
    }
    if (ui->layout_depth != 0)
    {
        SDL_Log("sdl3d_ui_end_frame: layout stack not fully popped (depth=%d)", ui->layout_depth);
        ui->layout_depth = 0;
    }
    ui->frame_open = false;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

/* Map SDL mouse button constant to our 3-element array index
 * (0=left, 1=middle, 2=right). Returns -1 for untracked buttons. */
static int ui_button_index(Uint8 sdl_button)
{
    switch (sdl_button)
    {
    case SDL_BUTTON_LEFT:
        return 0;
    case SDL_BUTTON_MIDDLE:
        return 1;
    case SDL_BUTTON_RIGHT:
        return 2;
    default:
        return -1;
    }
}

static float ui_map_mouse_x(const sdl3d_ui_context *ui, float wx)
{
    return (wx - ui->mouse_offset_x) * ui->mouse_scale_x;
}

static float ui_map_mouse_y(const sdl3d_ui_context *ui, float wy)
{
    return (wy - ui->mouse_offset_y) * ui->mouse_scale_y;
}

bool sdl3d_ui_process_event(sdl3d_ui_context *ui, const SDL_Event *event)
{
    if (!ui || !event)
    {
        return false;
    }
    switch (event->type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        ui->input.mouse_x = ui_map_mouse_x(ui, event->motion.x);
        ui->input.mouse_y = ui_map_mouse_y(ui, event->motion.y);
        return sdl3d_ui_wants_mouse(ui);
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int idx = ui_button_index(event->button.button);
        if (idx >= 0)
        {
            ui->input.mouse_down[idx] = true;
            ui->input.mouse_pressed[idx] = true;
        }
        ui->input.mouse_x = ui_map_mouse_x(ui, event->button.x);
        ui->input.mouse_y = ui_map_mouse_y(ui, event->button.y);
        return sdl3d_ui_wants_mouse(ui);
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int idx = ui_button_index(event->button.button);
        if (idx >= 0)
        {
            ui->input.mouse_down[idx] = false;
            ui->input.mouse_released[idx] = true;
        }
        ui->input.mouse_x = ui_map_mouse_x(ui, event->button.x);
        ui->input.mouse_y = ui_map_mouse_y(ui, event->button.y);
        return sdl3d_ui_wants_mouse(ui);
    }
    case SDL_EVENT_KEY_DOWN:
        if (event->key.scancode == SDL_SCANCODE_BACKSPACE)
            ui->input.key_backspace = true;
        else if (event->key.scancode == SDL_SCANCODE_RETURN || event->key.scancode == SDL_SCANCODE_KP_ENTER)
            ui->input.key_enter = true;
        else if (event->key.scancode == SDL_SCANCODE_ESCAPE)
            ui->input.key_escape = true;
        return sdl3d_ui_wants_keyboard(ui);
    case SDL_EVENT_TEXT_INPUT: {
        const char *text = event->text.text;
        if (text)
        {
            int room = (int)sizeof(ui->input.text_input) - 1 - ui->input.text_input_len;
            int tlen = (int)SDL_strlen(text);
            if (tlen > room)
            {
                tlen = room;
            }
            if (tlen > 0)
            {
                SDL_memcpy(ui->input.text_input + ui->input.text_input_len, text, (size_t)tlen);
                ui->input.text_input_len += tlen;
                ui->input.text_input[ui->input.text_input_len] = '\0';
            }
        }
        return sdl3d_ui_wants_keyboard(ui);
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        float dy = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            dy = -dy;
        ui->input.scroll_y += dy;
        ui->input.mouse_x = ui_map_mouse_x(ui, event->wheel.mouse_x);
        ui->input.mouse_y = ui_map_mouse_y(ui, event->wheel.mouse_y);
        return sdl3d_ui_wants_mouse(ui);
    }
    default:
        return false;
    }
}

bool sdl3d_ui_wants_mouse(const sdl3d_ui_context *ui)
{
    if (!ui)
        return false;
    return ui->hovered_id != 0 || ui->active_id != 0;
}

bool sdl3d_ui_wants_keyboard(const sdl3d_ui_context *ui)
{
    /* Same as above — no focused text widgets yet, so keyboard passes
     * through. Revisit when text fields land in Phase 3. */
    return ui && ui->focused_id != 0;
}

const sdl3d_ui_input_state *sdl3d_ui_get_input(const sdl3d_ui_context *ui)
{
    return ui ? &ui->input : NULL;
}

/* ------------------------------------------------------------------ */
/* IDs and hit testing                                                 */
/* ------------------------------------------------------------------ */

static sdl3d_ui_id ui_hash_string(const char *s)
{
    /* FNV-1a 32-bit. Good enough for widget IDs; collisions within a
     * single frame are extremely unlikely at UI scale, and IDs are
     * scoped per frame / per context. */
    sdl3d_ui_id h = 2166136261u;
    if (!s)
    {
        return h;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    {
        h ^= (sdl3d_ui_id)(*p);
        h *= 16777619u;
    }
    /* Reserve 0 as the "none" sentinel. */
    return h ? h : 1u;
}

sdl3d_ui_id sdl3d_ui_make_id(sdl3d_ui_context *ui, const char *label)
{
    (void)ui;
    return ui_hash_string(label);
}

bool sdl3d_ui_point_in_rect(float px, float py, float x, float y, float w, float h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

bool sdl3d_ui_is_hovering(const sdl3d_ui_context *ui, float x, float y, float w, float h)
{
    return ui_is_hovering_for_id(ui, 0, x, y, w, h);
}

/* ------------------------------------------------------------------ */
/* Draw primitives                                                     */
/* ------------------------------------------------------------------ */

void sdl3d_ui_draw_rect(sdl3d_ui_context *ui, float x, float y, float w, float h, sdl3d_color color)
{
    if (!ui || !ui->frame_open || w <= 0.0f || h <= 0.0f)
    {
        return;
    }
    sdl3d_ui_cmd *c = ui_push_cmd(ui);
    if (!c)
    {
        return;
    }
    c->kind = SDL3D_UI_CMD_RECT;
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    c->color = color;
}

void sdl3d_ui_draw_rect_outline(sdl3d_ui_context *ui, float x, float y, float w, float h, float thickness,
                                sdl3d_color color)
{
    if (!ui || w <= 0.0f || h <= 0.0f || thickness <= 0.0f)
    {
        return;
    }
    /* Four bordering rects. This is cheaper than submitting a stroked
     * primitive and composes cleanly with the solid-rect path. */
    sdl3d_ui_draw_rect(ui, x, y, w, thickness, color);                                                /* top */
    sdl3d_ui_draw_rect(ui, x, y + h - thickness, w, thickness, color);                                /* bottom */
    sdl3d_ui_draw_rect(ui, x, y + thickness, thickness, h - 2.0f * thickness, color);                 /* left */
    sdl3d_ui_draw_rect(ui, x + w - thickness, y + thickness, thickness, h - 2.0f * thickness, color); /* right */
}

void sdl3d_ui_draw_text(sdl3d_ui_context *ui, float x, float y, const char *text, sdl3d_color color)
{
    if (!ui || !ui->frame_open || !text || !*text)
    {
        return;
    }
    sdl3d_ui_cmd *c = ui_push_cmd(ui);
    if (!c)
    {
        return;
    }
    c->kind = SDL3D_UI_CMD_TEXT;
    c->x = x;
    c->y = y;
    c->color = color;
    c->text_offset = ui_push_text(ui, text);
}

void sdl3d_ui_push_clip(sdl3d_ui_context *ui, float x, float y, float w, float h)
{
    if (!ui || !ui->frame_open)
    {
        return;
    }
    if (ui->clip_depth >= SDL3D_UI_MAX_CLIP_DEPTH)
    {
        SDL_Log("sdl3d_ui_push_clip: clip stack full");
        return;
    }
    /* Intersect with parent so nested pushes can only shrink the clip. */
    float ix = x, iy = y, iw = w, ih = h;
    if (ui->clip_depth > 0)
    {
        sdl3d_ui_clip_rect parent = ui->clip_stack[ui->clip_depth - 1];
        float px1 = parent.x + parent.w;
        float py1 = parent.y + parent.h;
        float cx1 = x + w;
        float cy1 = y + h;
        if (x < parent.x)
            ix = parent.x;
        if (y < parent.y)
            iy = parent.y;
        float nx1 = cx1 < px1 ? cx1 : px1;
        float ny1 = cy1 < py1 ? cy1 : py1;
        iw = nx1 - ix;
        ih = ny1 - iy;
        if (iw < 0.0f)
            iw = 0.0f;
        if (ih < 0.0f)
            ih = 0.0f;
    }
    ui->clip_stack[ui->clip_depth].x = ix;
    ui->clip_stack[ui->clip_depth].y = iy;
    ui->clip_stack[ui->clip_depth].w = iw;
    ui->clip_stack[ui->clip_depth].h = ih;
    ui->clip_depth++;

    sdl3d_ui_cmd *c = ui_push_cmd(ui);
    if (c)
    {
        c->kind = SDL3D_UI_CMD_CLIP_PUSH;
        c->x = ix;
        c->y = iy;
        c->w = iw;
        c->h = ih;
    }
}

void sdl3d_ui_pop_clip(sdl3d_ui_context *ui)
{
    if (!ui || !ui->frame_open || ui->clip_depth <= 0)
    {
        return;
    }
    ui->clip_depth--;
    sdl3d_ui_cmd *c = ui_push_cmd(ui);
    if (c)
    {
        c->kind = SDL3D_UI_CMD_CLIP_POP;
    }
}

void sdl3d_ui_measure_text(const sdl3d_ui_context *ui, const char *text, float *out_w, float *out_h)
{
    if (!ui || !ui->font)
    {
        if (out_w)
            *out_w = 0;
        if (out_h)
            *out_h = 0;
        return;
    }
    sdl3d_measure_text(ui->font, text, out_w, out_h);
}

/* ------------------------------------------------------------------ */
/* Widgets                                                             */
/* ------------------------------------------------------------------ */

void sdl3d_ui_label(sdl3d_ui_context *ui, float x, float y, const char *text)
{
    if (!ui)
    {
        return;
    }
    sdl3d_ui_draw_text(ui, x, y, text, ui->theme.text);
}

void sdl3d_ui_label_colored(sdl3d_ui_context *ui, float x, float y, sdl3d_color color, const char *text)
{
    sdl3d_ui_draw_text(ui, x, y, text, color);
}

void sdl3d_ui_labelfv(sdl3d_ui_context *ui, float x, float y, const char *fmt, va_list args)
{
    if (!ui || !fmt)
    {
        return;
    }
    char buf[512];
    SDL_vsnprintf(buf, sizeof(buf), fmt, args);
    sdl3d_ui_label(ui, x, y, buf);
}

void sdl3d_ui_labelf(sdl3d_ui_context *ui, float x, float y, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    sdl3d_ui_labelfv(ui, x, y, fmt, args);
    va_end(args);
}

/* ------------------------------------------------------------------ */
/* Layout containers                                                   */
/* ------------------------------------------------------------------ */

static void ui_push_layout(sdl3d_ui_context *ui, sdl3d_ui_layout_dir direction, float x, float y, float w, float h)
{
    if (ui->layout_depth >= SDL3D_UI_MAX_LAYOUT_DEPTH)
    {
        SDL_Log("sdl3d_ui: layout stack overflow (max %d)", SDL3D_UI_MAX_LAYOUT_DEPTH);
        return;
    }
    int d = ui->layout_depth++;
    ui->layout_stack[d].direction = direction;
    ui->layout_stack[d].x = x;
    ui->layout_stack[d].y = y;
    ui->layout_stack[d].w = w;
    ui->layout_stack[d].h = h;
    ui->layout_stack[d].cursor = (direction == SDL3D_UI_LAYOUT_VBOX) ? y : x;
    ui->layout_stack[d].cross_max = 0.0f;
    ui->layout_stack[d].spacing = ui->theme.spacing;
    ui->layout_stack[d].first_item = true;
}

/*
 * Allocate a rect from the current layout. Returns false if no layout
 * is active. On success, fills out_x/out_y with the widget position.
 * `item_w` and `item_h` are the desired widget size.
 */
static bool ui_layout_alloc(sdl3d_ui_context *ui, float item_w, float item_h, float *out_x, float *out_y)
{
    if (ui->layout_depth <= 0)
        return false;

    int d = ui->layout_depth - 1;
    float gap = ui->layout_stack[d].first_item ? 0.0f : ui->layout_stack[d].spacing;
    ui->layout_stack[d].first_item = false;

    if (ui->layout_stack[d].direction == SDL3D_UI_LAYOUT_VBOX)
    {
        *out_x = ui->layout_stack[d].x;
        *out_y = ui->layout_stack[d].cursor + gap;
        ui->layout_stack[d].cursor = *out_y + item_h;
        if (item_w > ui->layout_stack[d].cross_max)
            ui->layout_stack[d].cross_max = item_w;
    }
    else
    {
        *out_x = ui->layout_stack[d].cursor + gap;
        *out_y = ui->layout_stack[d].y;
        ui->layout_stack[d].cursor = *out_x + item_w;
        if (item_h > ui->layout_stack[d].cross_max)
            ui->layout_stack[d].cross_max = item_h;
    }
    return true;
}

void sdl3d_ui_begin_panel(sdl3d_ui_context *ui, float x, float y, float w, float h)
{
    if (!ui || !ui->frame_open)
        return;
    const sdl3d_ui_theme *t = &ui->theme;
    sdl3d_ui_draw_rect(ui, x, y, w, h, t->panel_bg);
    sdl3d_ui_draw_rect_outline(ui, x, y, w, h, t->border_width, t->panel_border);
    sdl3d_ui_push_clip(ui, x, y, w, h);
}

void sdl3d_ui_end_panel(sdl3d_ui_context *ui)
{
    if (!ui)
        return;
    sdl3d_ui_pop_clip(ui);
}

void sdl3d_ui_begin_vbox(sdl3d_ui_context *ui, float x, float y, float w, float h)
{
    if (!ui || !ui->frame_open)
        return;
    ui_push_layout(ui, SDL3D_UI_LAYOUT_VBOX, x, y, w, h);
}

void sdl3d_ui_end_vbox(sdl3d_ui_context *ui)
{
    if (!ui || ui->layout_depth <= 0)
        return;
    if (ui->layout_stack[ui->layout_depth - 1].direction != SDL3D_UI_LAYOUT_VBOX)
    {
        SDL_Log("sdl3d_ui_end_vbox: top of layout stack is not a vbox");
        return;
    }
    ui->layout_depth--;
}

void sdl3d_ui_begin_hbox(sdl3d_ui_context *ui, float x, float y, float w, float h)
{
    if (!ui || !ui->frame_open)
        return;
    ui_push_layout(ui, SDL3D_UI_LAYOUT_HBOX, x, y, w, h);
}

void sdl3d_ui_end_hbox(sdl3d_ui_context *ui)
{
    if (!ui || ui->layout_depth <= 0)
        return;
    if (ui->layout_stack[ui->layout_depth - 1].direction != SDL3D_UI_LAYOUT_HBOX)
    {
        SDL_Log("sdl3d_ui_end_hbox: top of layout stack is not an hbox");
        return;
    }
    ui->layout_depth--;
}

void sdl3d_ui_separator(sdl3d_ui_context *ui)
{
    if (!ui || !ui->frame_open || ui->layout_depth <= 0)
        return;

    int d = ui->layout_depth - 1;
    float gap = ui->layout_stack[d].first_item ? 0.0f : ui->layout_stack[d].spacing;
    ui->layout_stack[d].first_item = false;

    sdl3d_color sep_color = ui->theme.panel_border;
    if (ui->layout_stack[d].direction == SDL3D_UI_LAYOUT_VBOX)
    {
        float sy = ui->layout_stack[d].cursor + gap;
        sdl3d_ui_draw_rect(ui, ui->layout_stack[d].x, sy, ui->layout_stack[d].w, 1.0f, sep_color);
        ui->layout_stack[d].cursor = sy + 1.0f;
    }
    else
    {
        float sx = ui->layout_stack[d].cursor + gap;
        sdl3d_ui_draw_rect(ui, sx, ui->layout_stack[d].y, 1.0f, ui->layout_stack[d].h, sep_color);
        ui->layout_stack[d].cursor = sx + 1.0f;
    }
}

/* ------------------------------------------------------------------ */
/* Scroll region                                                       */
/* ------------------------------------------------------------------ */

#define SDL3D_UI_SCROLL_SPEED 30.0f /* logical pixels per mouse wheel tick */

void sdl3d_ui_begin_scroll(sdl3d_ui_context *ui, float x, float y, float w, float h, float *scroll_offset,
                           float content_height)
{
    if (!ui || !ui->frame_open || !scroll_offset)
        return;

    /* Apply mouse wheel if hovering this region. */
    if (ui_is_hovering_for_id(ui, 0, x, y, w, h) && ui->input.scroll_y != 0.0f)
    {
        *scroll_offset -= ui->input.scroll_y * SDL3D_UI_SCROLL_SPEED;
    }

    /* Clamp scroll offset. */
    float max_scroll = content_height - h;
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    if (*scroll_offset < 0.0f)
        *scroll_offset = 0.0f;
    if (*scroll_offset > max_scroll)
        *scroll_offset = max_scroll;

    /* Push onto scroll stack. */
    if (ui->scroll_depth < SDL3D_UI_MAX_SCROLL_DEPTH)
    {
        int d = ui->scroll_depth++;
        ui->scroll_stack[d].x = x;
        ui->scroll_stack[d].y = y;
        ui->scroll_stack[d].w = w;
        ui->scroll_stack[d].h = h;
        ui->scroll_stack[d].offset = scroll_offset;
        ui->scroll_stack[d].content_height = content_height;
    }

    sdl3d_ui_push_clip(ui, x, y, w, h);
}

void sdl3d_ui_end_scroll(sdl3d_ui_context *ui)
{
    if (!ui)
        return;
    sdl3d_ui_pop_clip(ui);
    if (ui->scroll_depth > 0)
        ui->scroll_depth--;
}

/* ------------------------------------------------------------------ */
/* Auto-layout widgets                                                 */
/* ------------------------------------------------------------------ */

void sdl3d_ui_layout_label(sdl3d_ui_context *ui, const char *text)
{
    if (!ui || !ui->frame_open || !text)
        return;
    float tw = 0, th = 0;
    sdl3d_ui_measure_text(ui, text, &tw, &th);
    float x, y;
    if (!ui_layout_alloc(ui, tw, th, &x, &y))
        return;
    sdl3d_ui_label(ui, x, y, text);
}

void sdl3d_ui_layout_labelf(sdl3d_ui_context *ui, const char *fmt, ...)
{
    if (!ui || !ui->frame_open || !fmt)
        return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    SDL_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    sdl3d_ui_layout_label(ui, buf);
}

bool sdl3d_ui_layout_button(sdl3d_ui_context *ui, const char *label)
{
    if (!ui || !ui->frame_open || !label || ui->layout_depth <= 0)
        return false;

    int d = ui->layout_depth - 1;
    float bw, bh;
    float tw = 0, th = 0;
    sdl3d_ui_measure_text(ui, label, &tw, &th);
    float pad = ui->theme.padding;

    if (ui->layout_stack[d].direction == SDL3D_UI_LAYOUT_VBOX)
    {
        bw = ui->layout_stack[d].w;
        bh = th + pad * 2.0f;
    }
    else
    {
        bw = tw + pad * 2.0f;
        bh = ui->layout_stack[d].h;
    }

    float x, y;
    if (!ui_layout_alloc(ui, bw, bh, &x, &y))
        return false;
    return sdl3d_ui_button(ui, x, y, bw, bh, label);
}

/* ------------------------------------------------------------------ */
/* Checkbox                                                            */
/* ------------------------------------------------------------------ */

#define SDL3D_UI_CHECKBOX_SIZE 16.0f
#define SDL3D_UI_CHECKBOX_GAP 6.0f

bool sdl3d_ui_checkbox(sdl3d_ui_context *ui, float x, float y, const char *label, bool *value)
{
    if (!ui || !ui->frame_open || !label || !value)
        return false;

    sdl3d_ui_id id = sdl3d_ui_make_id(ui, label);
    const sdl3d_ui_theme *t = &ui->theme;
    float box_size = SDL3D_UI_CHECKBOX_SIZE;

    /* Hit test the full row (box + label). */
    float tw = 0, th = 0;
    sdl3d_ui_measure_text(ui, label, &tw, &th);
    float row_w = box_size + SDL3D_UI_CHECKBOX_GAP + tw;
    float row_h = (th > box_size) ? th : box_size;

    bool hovering = ui_is_hovering_for_id(ui, id, x, y, row_w, row_h);
    bool changed = false;

    if (hovering)
    {
        ui->hovered_id = id;
        if (ui->input.mouse_pressed[0])
            ui->active_id = id;
    }
    if (ui->active_id == id && ui->input.mouse_released[0])
    {
        if (hovering)
        {
            *value = !(*value);
            changed = true;
        }
        ui->active_id = 0;
    }

    /* Draw box. */
    float box_y = y + (row_h - box_size) * 0.5f;
    sdl3d_color bg = (hovering) ? t->widget_hover : t->widget_bg;
    sdl3d_ui_draw_rect(ui, x, box_y, box_size, box_size, bg);
    sdl3d_ui_draw_rect_outline(ui, x, box_y, box_size, box_size, t->border_width, t->widget_border);

    /* Draw check mark (filled inner rect). */
    if (*value)
    {
        float inset = 3.0f;
        sdl3d_ui_draw_rect(ui, x + inset, box_y + inset, box_size - inset * 2, box_size - inset * 2, t->widget_active);
    }

    /* Draw label. */
    float label_x = x + box_size + SDL3D_UI_CHECKBOX_GAP;
    float label_y = y + (row_h - th) * 0.5f;
    sdl3d_ui_draw_text(ui, label_x, label_y, label, t->text);

    return changed;
}

bool sdl3d_ui_layout_checkbox(sdl3d_ui_context *ui, const char *label, bool *value)
{
    if (!ui || !ui->frame_open || !label || !value || ui->layout_depth <= 0)
        return false;

    float tw = 0, th = 0;
    sdl3d_ui_measure_text(ui, label, &tw, &th);
    float row_h = (th > SDL3D_UI_CHECKBOX_SIZE) ? th : SDL3D_UI_CHECKBOX_SIZE;
    float row_w = (ui->layout_stack[ui->layout_depth - 1].direction == SDL3D_UI_LAYOUT_VBOX)
                      ? ui->layout_stack[ui->layout_depth - 1].w
                      : SDL3D_UI_CHECKBOX_SIZE + SDL3D_UI_CHECKBOX_GAP + tw;

    float x, y;
    if (!ui_layout_alloc(ui, row_w, row_h, &x, &y))
        return false;
    return sdl3d_ui_checkbox(ui, x, y, label, value);
}

/* ------------------------------------------------------------------ */
/* Slider                                                              */
/* ------------------------------------------------------------------ */

#define SDL3D_UI_SLIDER_TRACK_H 6.0f
#define SDL3D_UI_SLIDER_HANDLE_W 12.0f
#define SDL3D_UI_SLIDER_ROW_H 20.0f

bool sdl3d_ui_slider(sdl3d_ui_context *ui, float x, float y, float w, const char *label, float *value, float min,
                     float max)
{
    if (!ui || !ui->frame_open || !label || !value || max <= min)
        return false;

    sdl3d_ui_id id = sdl3d_ui_make_id(ui, label);
    const sdl3d_ui_theme *t = &ui->theme;
    bool changed = false;

    /* Track with centered "Label: value" text overlay. */
    float track_y = y;
    float track_w = w;
    float track_h = SDL3D_UI_SLIDER_ROW_H;

    bool hovering = ui_is_hovering_for_id(ui, id, x, y, track_w, track_h);

    if (hovering)
    {
        ui->hovered_id = id;
        if (ui->input.mouse_pressed[0])
            ui->active_id = id;
    }

    /* Drag: while active, update value from mouse x. */
    if (ui->active_id == id)
    {
        if (ui->input.mouse_down[0])
        {
            float rel = (ui->input.mouse_x - x) / track_w;
            if (rel < 0.0f)
                rel = 0.0f;
            if (rel > 1.0f)
                rel = 1.0f;
            float new_val = min + rel * (max - min);
            if (new_val != *value)
            {
                *value = new_val;
                changed = true;
            }
        }
        else
        {
            ui->active_id = 0;
        }
    }

    /* Clamp value. */
    if (*value < min)
        *value = min;
    if (*value > max)
        *value = max;

    /* Draw track background. */
    float ty = track_y + (track_h - SDL3D_UI_SLIDER_TRACK_H) * 0.5f;
    sdl3d_ui_draw_rect(ui, x, ty, track_w, SDL3D_UI_SLIDER_TRACK_H, t->widget_bg);
    sdl3d_ui_draw_rect_outline(ui, x, ty, track_w, SDL3D_UI_SLIDER_TRACK_H, 1.0f, t->widget_border);

    /* Draw filled portion. */
    float frac = (*value - min) / (max - min);
    float fill_w = frac * track_w;
    if (fill_w > 0.0f)
        sdl3d_ui_draw_rect(ui, x, ty, fill_w, SDL3D_UI_SLIDER_TRACK_H, t->widget_active);

    /* Draw handle. */
    float hx = x + frac * (track_w - SDL3D_UI_SLIDER_HANDLE_W);
    sdl3d_color handle_color = (ui->active_id == id) ? t->widget_active : (hovering ? t->widget_hover : t->text);
    sdl3d_ui_draw_rect(ui, hx, track_y, SDL3D_UI_SLIDER_HANDLE_W, track_h, handle_color);
    sdl3d_ui_draw_rect_outline(ui, hx, track_y, SDL3D_UI_SLIDER_HANDLE_W, track_h, 1.0f, t->widget_border);

    /* Draw label + value text. */
    char buf[128];
    SDL_snprintf(buf, sizeof(buf), "%s: %.2f", label, (double)*value);
    float tw = 0, th = 0;
    sdl3d_ui_measure_text(ui, buf, &tw, &th);
    sdl3d_ui_draw_text(ui, x + (track_w - tw) * 0.5f, track_y + (track_h - th) * 0.5f, buf, t->text);

    return changed;
}

bool sdl3d_ui_layout_slider(sdl3d_ui_context *ui, const char *label, float *value, float min, float max)
{
    if (!ui || !ui->frame_open || !label || !value || ui->layout_depth <= 0)
        return false;

    int d = ui->layout_depth - 1;
    float w = ui->layout_stack[d].w;
    float h = SDL3D_UI_SLIDER_ROW_H;

    float x, y;
    if (!ui_layout_alloc(ui, w, h, &x, &y))
        return false;
    return sdl3d_ui_slider(ui, x, y, w, label, value, min, max);
}

/* ------------------------------------------------------------------ */
/* Button                                                              */
/* ------------------------------------------------------------------ */

bool sdl3d_ui_button(sdl3d_ui_context *ui, float x, float y, float w, float h, const char *label)
{
    if (!ui || !ui->frame_open || !label)
        return false;

    sdl3d_ui_id id = sdl3d_ui_make_id(ui, label);
    bool hovering = ui_is_hovering_for_id(ui, id, x, y, w, h);
    bool pressed = false;

    /* Update interaction state. */
    if (hovering)
    {
        ui->hovered_id = id;
        if (ui->input.mouse_pressed[0])
            ui->active_id = id;
    }

    /* Click = mouse released while hovering, after a press on this widget. */
    if (ui->active_id == id && ui->input.mouse_released[0])
    {
        if (hovering)
            pressed = true;
        ui->active_id = 0;
    }

    /* Choose visual state. */
    const sdl3d_ui_theme *t = &ui->theme;
    sdl3d_color bg;
    if (ui->active_id == id && hovering)
        bg = t->widget_active;
    else if (hovering)
        bg = t->widget_hover;
    else
        bg = t->widget_bg;

    /* Draw background + border. */
    sdl3d_ui_draw_rect(ui, x, y, w, h, bg);
    sdl3d_ui_draw_rect_outline(ui, x, y, w, h, t->border_width, t->widget_border);

    /* Center label text. Strip "##id" suffix for display. */
    const char *display = label;
    const char *sep = SDL_strstr(label, "##");
    char buf[256];
    if (sep)
    {
        int len = (int)(sep - label);
        if (len >= (int)sizeof(buf))
            len = (int)sizeof(buf) - 1;
        SDL_memcpy(buf, label, (size_t)len);
        buf[len] = '\0';
        display = buf;
    }

    float tw = 0, th = 0;
    sdl3d_ui_measure_text(ui, display, &tw, &th);
    float tx = x + (w - tw) * 0.5f;
    float ty = y + (h - th) * 0.5f;
    sdl3d_ui_draw_text(ui, tx, ty, display, t->text);

    return pressed;
}

/* ------------------------------------------------------------------ */
/* Text field                                                          */
/* ------------------------------------------------------------------ */

bool sdl3d_ui_text_field(sdl3d_ui_context *ui, float x, float y, float w, float h, char *buf, int buf_size)
{
    if (!ui || !ui->frame_open || !buf || buf_size <= 0)
        return false;

    char id_buf[32];
    SDL_snprintf(id_buf, sizeof(id_buf), "##tf_%p", (const void *)buf);
    sdl3d_ui_id id = sdl3d_ui_make_id(ui, id_buf);
    const sdl3d_ui_theme *t = &ui->theme;
    bool hovering = ui_is_hovering_for_id(ui, id, x, y, w, h);
    bool focused = (ui->focused_id == id);
    bool committed = false;

    /* Click to focus / unfocus. */
    if (hovering)
        ui->hovered_id = id;
    if (ui->input.mouse_pressed[0])
    {
        if (hovering)
            ui->focused_id = id;
        else if (focused)
        {
            ui->focused_id = 0;
            committed = true;
        }
    }

    /* Keyboard input when focused. */
    if (ui->focused_id == id)
    {
        if (ui->input.key_enter || ui->input.key_escape)
        {
            ui->focused_id = 0;
            committed = ui->input.key_enter;
        }
        else
        {
            if (ui->input.key_backspace)
            {
                int len = (int)SDL_strlen(buf);
                if (len > 0)
                    buf[len - 1] = '\0';
            }
            if (ui->input.text_input_len > 0)
            {
                int len = (int)SDL_strlen(buf);
                int room = buf_size - 1 - len;
                int add = ui->input.text_input_len;
                if (add > room)
                    add = room;
                if (add > 0)
                {
                    SDL_memcpy(buf + len, ui->input.text_input, (size_t)add);
                    buf[len + add] = '\0';
                }
            }
        }
    }

    /* Draw. */
    sdl3d_color bg = (ui->focused_id == id) ? t->widget_active : (hovering ? t->widget_hover : t->widget_bg);
    sdl3d_ui_draw_rect(ui, x, y, w, h, bg);
    sdl3d_ui_draw_rect_outline(ui, x, y, w, h, t->border_width, t->widget_border);
    if (ui->focused_id == id)
        sdl3d_ui_draw_rect_outline(ui, x - 1, y - 1, w + 2, h + 2, 1.0f, t->focus_ring);

    /* Text + cursor. */
    float tw = 0, th = 0;
    sdl3d_ui_measure_text(ui, buf, &tw, &th);
    float tx = x + t->padding;
    float ty = y + (h - th) * 0.5f;
    sdl3d_ui_push_clip(ui, x + 1, y, w - 2, h);
    sdl3d_ui_draw_text(ui, tx, ty, buf, t->text);
    if (ui->focused_id == id)
        sdl3d_ui_draw_rect(ui, tx + tw, ty, 1.0f, th, t->text); /* cursor */
    sdl3d_ui_pop_clip(ui);

    return committed;
}

bool sdl3d_ui_layout_text_field(sdl3d_ui_context *ui, char *buf, int buf_size)
{
    if (!ui || !ui->frame_open || ui->layout_depth <= 0)
        return false;
    int d = ui->layout_depth - 1;
    float w = ui->layout_stack[d].w;
    float h = ui->theme.padding * 2.0f + 16.0f;
    float x, y;
    if (!ui_layout_alloc(ui, w, h, &x, &y))
        return false;
    return sdl3d_ui_text_field(ui, x, y, w, h, buf, buf_size);
}

/* ------------------------------------------------------------------ */
/* Dropdown                                                            */
/* ------------------------------------------------------------------ */

bool sdl3d_ui_dropdown(sdl3d_ui_context *ui, float x, float y, float w, float h, const char *const *items,
                       int item_count, int *selected)
{
    if (!ui || !ui->frame_open || !items || item_count <= 0 || !selected)
        return false;

    /* Use a stable ID from the pointer address of selected. */
    char id_buf[32];
    SDL_snprintf(id_buf, sizeof(id_buf), "##dd_%p", (const void *)selected);
    sdl3d_ui_id id = sdl3d_ui_make_id(ui, id_buf);
    const sdl3d_ui_theme *t = &ui->theme;
    bool hovering = ui_is_hovering_for_id(ui, id, x, y, w, h);
    bool open = (ui->focused_id == id);
    bool changed = false;

    /* Toggle open/close on click. */
    if (hovering)
        ui->hovered_id = id;
    if (hovering && ui->input.mouse_pressed[0] && !open)
    {
        ui->focused_id = id;
        ui->active_id = id;
    }

    /* Draw button showing current selection. */
    sdl3d_color bg = hovering ? t->widget_hover : t->widget_bg;
    sdl3d_ui_draw_rect(ui, x, y, w, h, bg);
    sdl3d_ui_draw_rect_outline(ui, x, y, w, h, t->border_width, t->widget_border);

    const char *label = (*selected >= 0 && *selected < item_count) ? items[*selected] : "";
    float tw = 0, th = 0;
    sdl3d_ui_measure_text(ui, label, &tw, &th);
    sdl3d_ui_draw_text(ui, x + t->padding, y + (h - th) * 0.5f, label, t->text);
    /* Down arrow indicator. */
    sdl3d_ui_draw_text(ui, x + w - t->padding - 8, y + (h - th) * 0.5f, "v", t->text_muted);

    /* Draw dropdown list when open. */
    if (open)
    {
        float list_y = y + h;
        float item_h = h;
        float list_h = item_h * (float)item_count;
        ui_activate_popup_capture(ui, id, x, y, w, h + list_h);
        if (ui->clip_depth > 0)
        {
            const sdl3d_ui_clip_rect clip = ui->clip_stack[ui->clip_depth - 1];
            ui_popup_push_clip(ui, clip.x, clip.y, clip.w, clip.h);
        }
        ui_popup_draw_rect(ui, x, list_y, w, list_h, t->panel_bg);
        ui_popup_draw_rect_outline(ui, x, list_y, w, list_h, 1.0f, t->panel_border);

        for (int i = 0; i < item_count; i++)
        {
            float iy = list_y + item_h * (float)i;
            bool item_hover = ui_is_hovering_for_id(ui, id, x, iy, w, item_h);
            if (item_hover)
            {
                ui->hovered_id = id;
                ui_popup_draw_rect(ui, x, iy, w, item_h, t->widget_hover);
                if (ui->input.mouse_pressed[0])
                {
                    *selected = i;
                    changed = true;
                    ui->focused_id = 0;
                    ui->active_id = 0;
                }
            }
            if (i == *selected)
                ui_popup_draw_rect(ui, x, iy, w, item_h, t->widget_active);

            float itw = 0, ith = 0;
            sdl3d_ui_measure_text(ui, items[i], &itw, &ith);
            ui_popup_draw_text(ui, x + t->padding, iy + (item_h - ith) * 0.5f, items[i], t->text);
        }

        /* Close if clicked outside. */
        if (ui->input.mouse_pressed[0] && !ui_is_hovering_for_id(ui, id, x, y, w, h + list_h))
        {
            ui->focused_id = 0;
            ui->active_id = 0;
        }
        if (ui->clip_depth > 0)
        {
            ui_popup_pop_clip(ui);
        }
    }

    return changed;
}

bool sdl3d_ui_layout_dropdown(sdl3d_ui_context *ui, const char *const *items, int item_count, int *selected)
{
    if (!ui || !ui->frame_open || ui->layout_depth <= 0)
        return false;
    int d = ui->layout_depth - 1;
    float w = ui->layout_stack[d].w;
    float h = ui->theme.padding * 2.0f + 16.0f;
    float x, y;
    if (!ui_layout_alloc(ui, w, h, &x, &y))
        return false;
    return sdl3d_ui_dropdown(ui, x, y, w, h, items, item_count, selected);
}

/* ------------------------------------------------------------------ */
/* Tab strip                                                           */
/* ------------------------------------------------------------------ */

bool sdl3d_ui_tab_strip(sdl3d_ui_context *ui, float x, float y, float w, float h, const char *const *tabs,
                        int tab_count, int *selected)
{
    if (!ui || !ui->frame_open || !tabs || tab_count <= 0 || !selected)
        return false;

    const sdl3d_ui_theme *t = &ui->theme;
    bool changed = false;
    float tab_w = w / (float)tab_count;

    /* Background. */
    sdl3d_ui_draw_rect(ui, x, y, w, h, t->panel_bg);

    for (int i = 0; i < tab_count; i++)
    {
        float tx = x + tab_w * (float)i;
        bool hovering = ui_is_hovering_for_id(ui, 0, tx, y, tab_w, h);
        bool active = (i == *selected);

        sdl3d_color bg;
        if (active)
            bg = t->widget_active;
        else if (hovering)
            bg = t->widget_hover;
        else
            bg = t->panel_bg;

        sdl3d_ui_draw_rect(ui, tx, y, tab_w, h, bg);
        sdl3d_ui_draw_rect_outline(ui, tx, y, tab_w, h, 1.0f, t->widget_border);

        float tw = 0, th = 0;
        sdl3d_ui_measure_text(ui, tabs[i], &tw, &th);
        sdl3d_ui_draw_text(ui, tx + (tab_w - tw) * 0.5f, y + (h - th) * 0.5f, tabs[i], t->text);

        if (hovering && ui->input.mouse_pressed[0] && !active)
        {
            *selected = i;
            changed = true;
        }
    }

    return changed;
}

bool sdl3d_ui_layout_tab_strip(sdl3d_ui_context *ui, const char *const *tabs, int tab_count, int *selected)
{
    if (!ui || !ui->frame_open || ui->layout_depth <= 0)
        return false;
    int d = ui->layout_depth - 1;
    float w = ui->layout_stack[d].w;
    float h = ui->theme.padding * 2.0f + 16.0f;
    float x, y;
    if (!ui_layout_alloc(ui, w, h, &x, &y))
        return false;
    return sdl3d_ui_tab_strip(ui, x, y, w, h, tabs, tab_count, selected);
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static bool ui_render_cmd_buffer(const sdl3d_ui_context *ui, sdl3d_render_context *context, const sdl3d_ui_cmd *cmds,
                                 int cmd_count)
{
    bool ok = true;
    for (int i = 0; i < cmd_count && ok; i++)
    {
        const sdl3d_ui_cmd *cmd = &cmds[i];
        switch (cmd->kind)
        {
        case SDL3D_UI_CMD_RECT:
            ok = sdl3d_draw_rect_overlay(context, cmd->x, cmd->y, cmd->w, cmd->h, cmd->color);
            break;
        case SDL3D_UI_CMD_TEXT: {
            if (!ui->font)
            {
                break;
            }
            const char *text = (cmd->text_offset >= 0) ? ui->text_arena + cmd->text_offset : "";
            ok = sdl3d_draw_text_overlay(context, ui->font, text, cmd->x, cmd->y, cmd->color);
            break;
        }
        case SDL3D_UI_CMD_CLIP_PUSH: {
            SDL_Rect r = {(int)cmd->x, (int)cmd->y, (int)cmd->w, (int)cmd->h};
            sdl3d_set_scissor_rect(context, &r);
            break;
        }
        case SDL3D_UI_CMD_CLIP_POP:
            sdl3d_set_scissor_rect(context, NULL);
            break;
        }
    }
    return ok;
}

bool sdl3d_ui_render(sdl3d_ui_context *ui, sdl3d_render_context *context)
{
    if (!ui)
    {
        return SDL_InvalidParamError("ui");
    }
    if (!context)
    {
        return SDL_InvalidParamError("context");
    }
    if (ui->frame_open)
    {
        return SDL_SetError("sdl3d_ui_render called before sdl3d_ui_end_frame");
    }

    /* Remember the caller's scissor state so we can leave it undisturbed. */
    bool had_scissor = sdl3d_is_scissor_enabled(context);
    SDL_Rect saved_scissor = {0, 0, 0, 0};
    if (had_scissor)
    {
        sdl3d_get_scissor_rect(context, &saved_scissor);
    }

    bool ok = ui_render_cmd_buffer(ui, context, ui->cmds, ui->cmd_count);
    if (ok && ui->popup_cmd_count > 0)
    {
        ok = ui_render_cmd_buffer(ui, context, ui->popup_cmds, ui->popup_cmd_count);
    }

    if (had_scissor)
    {
        sdl3d_set_scissor_rect(context, &saved_scissor);
    }
    else
    {
        sdl3d_set_scissor_rect(context, NULL);
    }

    return ok;
}

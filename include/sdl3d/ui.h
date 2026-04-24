/*
 * Backend-agnostic immediate-mode UI system for SDL_3D.
 *
 * Phase 1 of issue #62: core context, input routing, theme/font plumbing,
 * screen-space draw primitives, and a label widget. Designed to grow into
 * the widget set needed for a TrenchBroom-style level editor without
 * being tied to a specific renderer.
 *
 * Usage:
 *   sdl3d_ui_context *ui;
 *   sdl3d_ui_create(&font, &ui);
 *
 *   // per frame, BEFORE sdl3d_present_render_context:
 *   sdl3d_ui_begin_frame(ui, screen_w, screen_h);
 *   sdl3d_ui_label(ui, 10, 10, "Hello");
 *   sdl3d_ui_end_frame(ui);
 *   sdl3d_ui_render(ui, ctx);        // call outside begin_mode_3d / end_mode_3d
 *
 *   // per SDL event, BEFORE your own input handling:
 *   if (!sdl3d_ui_process_event(ui, &ev)) {
 *       // event wasn't consumed by UI, route to the game
 *   }
 */

#ifndef SDL3D_UI_H
#define SDL3D_UI_H

#include "sdl3d/font.h"
#include "sdl3d/render_context.h"
#include "sdl3d/types.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct sdl3d_ui_context sdl3d_ui_context;

    typedef uint32_t sdl3d_ui_id;

    /*
     * Visual defaults shared across widgets. Callers can copy the defaults
     * returned by sdl3d_ui_default_theme, mutate fields, and reinstall via
     * sdl3d_ui_set_theme. All colors are sRGB RGBA8.
     */
    typedef struct sdl3d_ui_theme
    {
        sdl3d_color text;
        sdl3d_color text_muted;
        sdl3d_color panel_bg;
        sdl3d_color panel_border;
        sdl3d_color widget_bg;
        sdl3d_color widget_hover;
        sdl3d_color widget_active;
        sdl3d_color widget_border;
        sdl3d_color focus_ring;
        float padding;      /* inner padding between a widget's border and its content */
        float spacing;      /* spacing between stacked widgets */
        float border_width; /* default border thickness in pixels */
    } sdl3d_ui_theme;

    /*
     * Retained input snapshot, refreshed by sdl3d_ui_process_event. Mostly
     * consumed internally by widgets, but exposed so callers can build
     * custom widgets against the same model.
     */
    typedef struct sdl3d_ui_input_state
    {
        float mouse_x, mouse_y;
        bool mouse_down[3];     /* left / middle / right, current frame */
        bool mouse_pressed[3];  /* edge-triggered: true only on the frame the press arrived */
        bool mouse_released[3]; /* edge-triggered: true only on the release frame */
        char text_input[32];    /* UTF-8 text from SDL_EVENT_TEXT_INPUT, accumulated per frame */
        int text_input_len;
    } sdl3d_ui_input_state;

    /* ------------------------------------------------------------------ */
    /* Lifecycle                                                           */
    /* ------------------------------------------------------------------ */

    bool sdl3d_ui_create(const sdl3d_font *font, sdl3d_ui_context **out_ui);
    void sdl3d_ui_destroy(sdl3d_ui_context *ui);

    /*
     * Call once per frame before issuing widget calls. screen_w/h should
     * be the target render context's logical dimensions.
     */
    void sdl3d_ui_begin_frame(sdl3d_ui_context *ui, int screen_w, int screen_h);
    void sdl3d_ui_end_frame(sdl3d_ui_context *ui);

    /*
     * Tell the UI how to map SDL window-space mouse coordinates to the
     * logical coordinate system used by widgets. Call after begin_frame
     * when the window size differs from the logical size (e.g., after a
     * resize or on HiDPI displays).
     *
     *   logical_x = (window_x - offset_x) * scale_x
     *   logical_y = (window_y - offset_y) * scale_y
     *
     * If never called, the mapping defaults to identity (1:1).
     */
    void sdl3d_ui_set_mouse_transform(sdl3d_ui_context *ui, float scale_x, float scale_y, float offset_x,
                                      float offset_y);

    /*
     * Flush the current frame's draw list onto `context`. Must be called
     * outside sdl3d_begin_mode_3d / sdl3d_end_mode_3d; typically the last
     * thing before sdl3d_present_render_context. The UI render path is
     * intentionally overlay-based so widgets stay out of the main 3D
     * pipeline and post-processing stack.
     */
    bool sdl3d_ui_render(sdl3d_ui_context *ui, sdl3d_render_context *context);

    /* ------------------------------------------------------------------ */
    /* Input                                                               */
    /* ------------------------------------------------------------------ */

    /*
     * Feed an SDL event into the UI. Returns true if the UI consumed the
     * event (mouse hovered a widget, text input when a field is focused,
     * etc.) so callers can gate their own handlers.
     */
    bool sdl3d_ui_process_event(sdl3d_ui_context *ui, const SDL_Event *event);

    bool sdl3d_ui_wants_mouse(const sdl3d_ui_context *ui);
    bool sdl3d_ui_wants_keyboard(const sdl3d_ui_context *ui);

    const sdl3d_ui_input_state *sdl3d_ui_get_input(const sdl3d_ui_context *ui);

    /* ------------------------------------------------------------------ */
    /* Theme / font                                                        */
    /* ------------------------------------------------------------------ */

    sdl3d_ui_theme sdl3d_ui_default_theme(void);
    const sdl3d_ui_theme *sdl3d_ui_get_theme(const sdl3d_ui_context *ui);
    void sdl3d_ui_set_theme(sdl3d_ui_context *ui, const sdl3d_ui_theme *theme);

    const sdl3d_font *sdl3d_ui_get_font(const sdl3d_ui_context *ui);
    void sdl3d_ui_set_font(sdl3d_ui_context *ui, const sdl3d_font *font);

    /* ------------------------------------------------------------------ */
    /* Identification / hit testing (used by widgets)                      */
    /* ------------------------------------------------------------------ */

    sdl3d_ui_id sdl3d_ui_make_id(sdl3d_ui_context *ui, const char *label);
    bool sdl3d_ui_point_in_rect(float px, float py, float x, float y, float w, float h);
    bool sdl3d_ui_is_hovering(const sdl3d_ui_context *ui, float x, float y, float w, float h);

    /* ------------------------------------------------------------------ */
    /* Screen-space draw primitives                                        */
    /* ------------------------------------------------------------------ */

    void sdl3d_ui_draw_rect(sdl3d_ui_context *ui, float x, float y, float w, float h, sdl3d_color color);
    void sdl3d_ui_draw_rect_outline(sdl3d_ui_context *ui, float x, float y, float w, float h, float thickness,
                                    sdl3d_color color);
    void sdl3d_ui_draw_text(sdl3d_ui_context *ui, float x, float y, const char *text, sdl3d_color color);

    /*
     * Clip rect stack. Draw commands issued while a clip is active are
     * restricted to the current rect (intersection of the stack).
     */
    void sdl3d_ui_push_clip(sdl3d_ui_context *ui, float x, float y, float w, float h);
    void sdl3d_ui_pop_clip(sdl3d_ui_context *ui);

    /* ------------------------------------------------------------------ */
    /* Measurement                                                         */
    /* ------------------------------------------------------------------ */

    void sdl3d_ui_measure_text(const sdl3d_ui_context *ui, const char *text, float *out_w, float *out_h);

    /* ------------------------------------------------------------------ */
    /* Widgets                                                             */
    /* ------------------------------------------------------------------ */

    /*
     * Draw a single-line (or \n-separated) text label at screen-space
     * (x, y), using the current theme's text color.
     */
    void sdl3d_ui_label(sdl3d_ui_context *ui, float x, float y, const char *text);

    void sdl3d_ui_label_colored(sdl3d_ui_context *ui, float x, float y, sdl3d_color color, const char *text);

    void sdl3d_ui_labelf(sdl3d_ui_context *ui, float x, float y, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
        SDL_PRINTF_VARARG_FUNC(4);

    void sdl3d_ui_labelfv(sdl3d_ui_context *ui, float x, float y, const char *fmt, va_list args);

    /*
     * Immediate-mode button. Draws a themed rectangle with centered label
     * text and returns true on the frame the user clicks it (mouse released
     * while hovering after a press that started on the button).
     *
     * The label string is also used to generate the widget ID, so each
     * button in a frame must have a unique label (or use "Label##id" to
     * disambiguate visually identical buttons).
     */
    bool sdl3d_ui_button(sdl3d_ui_context *ui, float x, float y, float w, float h, const char *label);

    /* ------------------------------------------------------------------ */
    /* Layout containers                                                   */
    /* ------------------------------------------------------------------ */

    /*
     * Panel: a filled, bordered rectangle that clips its children.
     * Pushes a clip rect and optionally a layout. Children issued
     * between begin/end are clipped to the panel bounds.
     */
    void sdl3d_ui_begin_panel(sdl3d_ui_context *ui, float x, float y, float w, float h);
    void sdl3d_ui_end_panel(sdl3d_ui_context *ui);

    /*
     * Vertical box: children are stacked top-to-bottom with theme spacing.
     * Must be inside a panel or another layout container.
     */
    void sdl3d_ui_begin_vbox(sdl3d_ui_context *ui, float x, float y, float w, float h);
    void sdl3d_ui_end_vbox(sdl3d_ui_context *ui);

    /*
     * Horizontal box: children are placed left-to-right with theme spacing.
     */
    void sdl3d_ui_begin_hbox(sdl3d_ui_context *ui, float x, float y, float w, float h);
    void sdl3d_ui_end_hbox(sdl3d_ui_context *ui);

    /*
     * Separator: a thin horizontal (in vbox) or vertical (in hbox) line
     * that advances the layout cursor. Only meaningful inside a layout.
     */
    void sdl3d_ui_separator(sdl3d_ui_context *ui);

    /* ------------------------------------------------------------------ */
    /* Auto-layout widgets                                                 */
    /* ------------------------------------------------------------------ */

    /*
     * These variants participate in the current layout — they consume
     * space from the active vbox/hbox cursor and don't take x/y params.
     * They are no-ops if no layout is active.
     */
    void sdl3d_ui_layout_label(sdl3d_ui_context *ui, const char *text);
    void sdl3d_ui_layout_labelf(sdl3d_ui_context *ui, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
        SDL_PRINTF_VARARG_FUNC(2);
    bool sdl3d_ui_layout_button(sdl3d_ui_context *ui, const char *label);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_UI_H */

/*
 * Backend-agnostic immediate-mode UI system for Slayer3D.
 *
 * Phase 1 of issue #62: core context, input routing, theme/font plumbing,
 * screen-space draw primitives, and a label widget. Designed to grow into
 * the widget set needed for a TrenchBroom-style level editor without
 * being tied to a specific renderer.
 *
 * Usage:
 *   slayer3d_ui_context *ui;
 *   slayer3d_ui_create(&font, &ui);
 *
 *   // per frame, BEFORE slayer3d_present_render_context:
 *   slayer3d_ui_begin_frame(ui, screen_w, screen_h);
 *   slayer3d_ui_label(ui, 10, 10, "Hello");
 *   slayer3d_ui_end_frame(ui);
 *   slayer3d_ui_render(ui, ctx);        // call outside begin_mode_3d / end_mode_3d
 *
 *   // per SDL event, BEFORE your own input handling:
 *   if (!slayer3d_ui_process_event(ui, &ev)) {
 *       // event wasn't consumed by UI, route to the game
 *   }
 */

#ifndef SLAYER3D_UI_H
#define SLAYER3D_UI_H

#include "slayer3d/font.h"
#include "slayer3d/render_context.h"
#include "slayer3d/types.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct slayer3d_ui_context slayer3d_ui_context;

    typedef uint32_t slayer3d_ui_id;

    /*
     * Visual defaults shared across widgets. Callers can copy the defaults
     * returned by slayer3d_ui_default_theme, mutate fields, and reinstall via
     * slayer3d_ui_set_theme. All colors are sRGB RGBA8.
     */
    typedef struct slayer3d_ui_theme
    {
        slayer3d_color text;
        slayer3d_color text_muted;
        slayer3d_color panel_bg;
        slayer3d_color panel_border;
        slayer3d_color widget_bg;
        slayer3d_color widget_hover;
        slayer3d_color widget_active;
        slayer3d_color widget_border;
        slayer3d_color focus_ring;
        float padding;      /* inner padding between a widget's border and its content */
        float spacing;      /* spacing between stacked widgets */
        float border_width; /* default border thickness in pixels */
    } slayer3d_ui_theme;

    /*
     * Retained input snapshot, refreshed by slayer3d_ui_process_event. Mostly
     * consumed internally by widgets, but exposed so callers can build
     * custom widgets against the same model.
     */
    typedef struct slayer3d_ui_input_state
    {
        float mouse_x, mouse_y;
        float scroll_y;         /* accumulated mouse wheel delta this frame (positive = up) */
        bool mouse_down[3];     /* left / middle / right, current frame */
        bool mouse_pressed[3];  /* edge-triggered: true only on the frame the press arrived */
        bool mouse_released[3]; /* edge-triggered: true only on the release frame */
        char text_input[32];    /* UTF-8 text from SDL_EVENT_TEXT_INPUT, accumulated per frame */
        int text_input_len;
        bool key_backspace; /* edge-triggered: backspace pressed this frame */
        bool key_enter;     /* edge-triggered: enter/return pressed this frame */
        bool key_escape;    /* edge-triggered: escape pressed this frame */
    } slayer3d_ui_input_state;

    /* ------------------------------------------------------------------ */
    /* Lifecycle                                                           */
    /* ------------------------------------------------------------------ */

    bool slayer3d_ui_create(const slayer3d_font *font, slayer3d_ui_context **out_ui);
    void slayer3d_ui_destroy(slayer3d_ui_context *ui);

    /*
     * Call once per frame before issuing widget calls. screen_w/h should
     * be the target render context's logical dimensions.
     */
    void slayer3d_ui_begin_frame(slayer3d_ui_context *ui, int screen_w, int screen_h);
    void slayer3d_ui_end_frame(slayer3d_ui_context *ui);

    /*
     * Convenience: begin a UI frame and auto-compute the mouse transform
     * from the render context's logical dimensions and the window's
     * current size. Equivalent to calling slayer3d_ui_begin_frame +
     * slayer3d_ui_set_mouse_transform with the correct letterbox mapping.
     */
    void slayer3d_ui_begin_frame_ex(slayer3d_ui_context *ui, slayer3d_render_context *context, SDL_Window *window);

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
    void slayer3d_ui_set_mouse_transform(slayer3d_ui_context *ui, float scale_x, float scale_y, float offset_x,
                                         float offset_y);

    /*
     * Flush the current frame's draw list onto `context`. Must be called
     * outside slayer3d_begin_mode_3d / slayer3d_end_mode_3d; typically the last
     * thing before slayer3d_present_render_context. The UI render path is
     * intentionally overlay-based so widgets stay out of the main 3D
     * pipeline and post-processing stack.
     */
    bool slayer3d_ui_render(slayer3d_ui_context *ui, slayer3d_render_context *context);

    /* ------------------------------------------------------------------ */
    /* Input                                                               */
    /* ------------------------------------------------------------------ */

    /*
     * Feed an SDL event into the UI. Returns true if the UI consumed the
     * event (mouse hovered a widget, text input when a field is focused,
     * etc.) so callers can gate their own handlers.
     */
    bool slayer3d_ui_process_event(slayer3d_ui_context *ui, const SDL_Event *event);

    bool slayer3d_ui_wants_mouse(const slayer3d_ui_context *ui);
    bool slayer3d_ui_wants_keyboard(const slayer3d_ui_context *ui);

    const slayer3d_ui_input_state *slayer3d_ui_get_input(const slayer3d_ui_context *ui);

    /* ------------------------------------------------------------------ */
    /* Theme / font                                                        */
    /* ------------------------------------------------------------------ */

    slayer3d_ui_theme slayer3d_ui_default_theme(void);
    const slayer3d_ui_theme *slayer3d_ui_get_theme(const slayer3d_ui_context *ui);
    void slayer3d_ui_set_theme(slayer3d_ui_context *ui, const slayer3d_ui_theme *theme);

    const slayer3d_font *slayer3d_ui_get_font(const slayer3d_ui_context *ui);
    void slayer3d_ui_set_font(slayer3d_ui_context *ui, const slayer3d_font *font);

    /* ------------------------------------------------------------------ */
    /* Identification / hit testing (used by widgets)                      */
    /* ------------------------------------------------------------------ */

    slayer3d_ui_id slayer3d_ui_make_id(slayer3d_ui_context *ui, const char *label);
    bool slayer3d_ui_point_in_rect(float px, float py, float x, float y, float w, float h);
    bool slayer3d_ui_is_hovering(const slayer3d_ui_context *ui, float x, float y, float w, float h);

    /* ------------------------------------------------------------------ */
    /* Screen-space draw primitives                                        */
    /* ------------------------------------------------------------------ */

    void slayer3d_ui_draw_rect(slayer3d_ui_context *ui, float x, float y, float w, float h, slayer3d_color color);
    void slayer3d_ui_draw_rect_outline(slayer3d_ui_context *ui, float x, float y, float w, float h, float thickness,
                                       slayer3d_color color);
    void slayer3d_ui_draw_text(slayer3d_ui_context *ui, float x, float y, const char *text, slayer3d_color color);

    /*
     * Clip rect stack. Draw commands issued while a clip is active are
     * restricted to the current rect (intersection of the stack).
     */
    void slayer3d_ui_push_clip(slayer3d_ui_context *ui, float x, float y, float w, float h);
    void slayer3d_ui_pop_clip(slayer3d_ui_context *ui);

    /* ------------------------------------------------------------------ */
    /* Measurement                                                         */
    /* ------------------------------------------------------------------ */

    void slayer3d_ui_measure_text(const slayer3d_ui_context *ui, const char *text, float *out_w, float *out_h);

    /* ------------------------------------------------------------------ */
    /* Widgets                                                             */
    /* ------------------------------------------------------------------ */

    /*
     * Draw a single-line (or \n-separated) text label at screen-space
     * (x, y), using the current theme's text color.
     */
    void slayer3d_ui_label(slayer3d_ui_context *ui, float x, float y, const char *text);

    void slayer3d_ui_label_colored(slayer3d_ui_context *ui, float x, float y, slayer3d_color color, const char *text);

    void slayer3d_ui_labelf(slayer3d_ui_context *ui, float x, float y, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
        SDL_PRINTF_VARARG_FUNC(4);

    void slayer3d_ui_labelfv(slayer3d_ui_context *ui, float x, float y, const char *fmt, va_list args);

    /*
     * Immediate-mode button. Draws a themed rectangle with centered label
     * text and returns true on the frame the user clicks it (mouse released
     * while hovering after a press that started on the button).
     *
     * The label string is also used to generate the widget ID, so each
     * button in a frame must have a unique label (or use "Label##id" to
     * disambiguate visually identical buttons).
     */
    bool slayer3d_ui_button(slayer3d_ui_context *ui, float x, float y, float w, float h, const char *label);

    /* ------------------------------------------------------------------ */
    /* Layout containers                                                   */
    /* ------------------------------------------------------------------ */

    /*
     * Panel: a filled, bordered rectangle that clips its children.
     * Pushes a clip rect and optionally a layout. Children issued
     * between begin/end are clipped to the panel bounds.
     */
    void slayer3d_ui_begin_panel(slayer3d_ui_context *ui, float x, float y, float w, float h);
    void slayer3d_ui_end_panel(slayer3d_ui_context *ui);

    /*
     * Vertical box: children are stacked top-to-bottom with theme spacing.
     * Must be inside a panel or another layout container.
     */
    void slayer3d_ui_begin_vbox(slayer3d_ui_context *ui, float x, float y, float w, float h);
    void slayer3d_ui_end_vbox(slayer3d_ui_context *ui);

    /*
     * Horizontal box: children are placed left-to-right with theme spacing.
     */
    void slayer3d_ui_begin_hbox(slayer3d_ui_context *ui, float x, float y, float w, float h);
    void slayer3d_ui_end_hbox(slayer3d_ui_context *ui);

    /*
     * Separator: a thin horizontal (in vbox) or vertical (in hbox) line
     * that advances the layout cursor. Only meaningful inside a layout.
     */
    void slayer3d_ui_separator(slayer3d_ui_context *ui);

    /* ------------------------------------------------------------------ */
    /* Auto-layout widgets                                                 */
    /* ------------------------------------------------------------------ */

    /*
     * These variants participate in the current layout — they consume
     * space from the active vbox/hbox cursor and don't take x/y params.
     * They are no-ops if no layout is active.
     */
    void slayer3d_ui_layout_label(slayer3d_ui_context *ui, const char *text);
    void slayer3d_ui_layout_labelf(slayer3d_ui_context *ui, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
        SDL_PRINTF_VARARG_FUNC(2);
    bool slayer3d_ui_layout_button(slayer3d_ui_context *ui, const char *label);

    /*
     * Checkbox: toggles *value on click. Draws a small box (filled when
     * checked) followed by the label text. Returns true on the frame the
     * value changed.
     */
    bool slayer3d_ui_checkbox(slayer3d_ui_context *ui, float x, float y, const char *label, bool *value);
    bool slayer3d_ui_layout_checkbox(slayer3d_ui_context *ui, const char *label, bool *value);

    /*
     * Slider: drags *value between min and max. Draws a labeled track
     * with a draggable handle and the current value. Returns true on
     * any frame the value changed (i.e., during drag).
     */
    bool slayer3d_ui_slider(slayer3d_ui_context *ui, float x, float y, float w, const char *label, float *value,
                            float min, float max);
    bool slayer3d_ui_layout_slider(slayer3d_ui_context *ui, const char *label, float *value, float min, float max);

    /* ------------------------------------------------------------------ */
    /* Scroll region                                                       */
    /* ------------------------------------------------------------------ */

    /*
     * Scrollable region. Clips children to (x, y, w, h) and offsets
     * them vertically by the current scroll amount. Mouse wheel events
     * while hovering adjust the scroll offset.
     *
     * `scroll_offset` is caller-owned persistent state (initialize to 0).
     * `content_height` is the total height of the content inside the
     * region — pass the cursor position from your vbox after end_vbox,
     * or estimate it. The scroll is clamped so the content doesn't
     * scroll past its bounds.
     *
     * Typical usage:
     *   static float scroll = 0;
     *   slayer3d_ui_begin_scroll(ui, x, y, w, h, &scroll, content_h);
     *     slayer3d_ui_begin_vbox(ui, x, y - scroll, w, content_h);
     *       ... widgets ...
     *     slayer3d_ui_end_vbox(ui);
     *   slayer3d_ui_end_scroll(ui);
     */
    void slayer3d_ui_begin_scroll(slayer3d_ui_context *ui, float x, float y, float w, float h, float *scroll_offset,
                                  float content_height);
    void slayer3d_ui_end_scroll(slayer3d_ui_context *ui);

    /* ------------------------------------------------------------------ */
    /* Text field                                                          */
    /* ------------------------------------------------------------------ */

    /*
     * Single-line editable text field. Click to focus, type to edit,
     * backspace to delete. Click elsewhere or press Enter/Escape to
     * unfocus. Returns true on the frame the user commits (Enter or
     * unfocus after editing).
     *
     * `buf` is a caller-owned buffer of `buf_size` bytes. The field
     * edits it in place.
     */
    bool slayer3d_ui_text_field(slayer3d_ui_context *ui, float x, float y, float w, float h, char *buf, int buf_size);
    bool slayer3d_ui_layout_text_field(slayer3d_ui_context *ui, char *buf, int buf_size);

    /* ------------------------------------------------------------------ */
    /* Dropdown                                                            */
    /* ------------------------------------------------------------------ */

    /*
     * Dropdown selector. Displays the currently selected item as a
     * button; click to open a selection list. Returns true on the
     * frame the selection changes.
     *
     * `selected` is a caller-owned index into `items`.
     */
    bool slayer3d_ui_dropdown(slayer3d_ui_context *ui, float x, float y, float w, float h, const char *const *items,
                              int item_count, int *selected);
    bool slayer3d_ui_layout_dropdown(slayer3d_ui_context *ui, const char *const *items, int item_count, int *selected);

    /* ------------------------------------------------------------------ */
    /* Tab strip                                                           */
    /* ------------------------------------------------------------------ */

    /*
     * Horizontal row of selectable tabs. Returns true on the frame
     * the selection changes. `selected` is a caller-owned index.
     */
    bool slayer3d_ui_tab_strip(slayer3d_ui_context *ui, float x, float y, float w, float h, const char *const *tabs,
                               int tab_count, int *selected);
    bool slayer3d_ui_layout_tab_strip(slayer3d_ui_context *ui, const char *const *tabs, int tab_count, int *selected);

    /* ------------------------------------------------------------------ */
    /* Inspector row                                                       */
    /* ------------------------------------------------------------------ */

    /*
     * A "label: widget" row for property inspectors. Splits the current
     * layout width into a label portion (left) and a widget portion
     * (right). `label_fraction` is typically 0.35–0.4.
     *
     * begin_row pushes an hbox sized to the current layout width.
     * The caller draws the label, then the value widget, then calls
     * end_row. Convenience helpers below handle common patterns.
     */
    void slayer3d_ui_begin_row(slayer3d_ui_context *ui, const char *label, float label_fraction);
    void slayer3d_ui_end_row(slayer3d_ui_context *ui);

    /* Inspector row with a text field value. */
    bool slayer3d_ui_row_text_field(slayer3d_ui_context *ui, const char *label, char *buf, int buf_size);

    /* Inspector row with a slider value. */
    bool slayer3d_ui_row_slider(slayer3d_ui_context *ui, const char *label, float *value, float min, float max);

    /* Inspector row with a checkbox value. */
    bool slayer3d_ui_row_checkbox(slayer3d_ui_context *ui, const char *label, bool *value);

    /* Inspector row with a read-only label value. */
    void slayer3d_ui_row_label(slayer3d_ui_context *ui, const char *label, const char *value);

    /* ------------------------------------------------------------------ */
    /* List view                                                           */
    /* ------------------------------------------------------------------ */

    /*
     * Scrollable list of selectable text items. Returns true on the
     * frame the selection changes. `selected` is a caller-owned index
     * (-1 for no selection). The list is drawn at the given position
     * with the given size; items that overflow are clipped and can be
     * scrolled with the mouse wheel.
     *
     * `scroll_offset` is caller-owned persistent state (initialize to 0).
     */
    bool slayer3d_ui_list_view(slayer3d_ui_context *ui, float x, float y, float w, float h, const char *const *items,
                               int item_count, int *selected, float *scroll_offset);
    bool slayer3d_ui_layout_list_view(slayer3d_ui_context *ui, float h, const char *const *items, int item_count,
                                      int *selected, float *scroll_offset);

    /* ------------------------------------------------------------------ */
    /* Tree view                                                           */
    /* ------------------------------------------------------------------ */

    /*
     * Collapsible tree node. Returns true if the node is expanded
     * (children should be drawn). `expanded` is caller-owned state.
     * `selected_id` is a caller-owned pointer to the currently selected
     * node ID; when the user clicks this node's label, `*selected_id`
     * is set to `node_id` and the function returns true for the
     * selection change.
     *
     * Typical usage (recursive):
     *   if (slayer3d_ui_tree_node(ui, "Root", 1, &expanded_root, &sel)) { ... }
     *     slayer3d_ui_tree_push(ui);
     *     if (slayer3d_ui_tree_node(ui, "Child", 2, &expanded_child, &sel)) { ... }
     *     slayer3d_ui_tree_pop(ui);
     *   (tree_node draws its own row; push/pop indent children)
     */
    bool slayer3d_ui_tree_node(slayer3d_ui_context *ui, const char *label, int node_id, bool *expanded,
                               int *selected_id);
    void slayer3d_ui_tree_push(slayer3d_ui_context *ui);
    void slayer3d_ui_tree_pop(slayer3d_ui_context *ui);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_UI_H */

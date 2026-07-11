/**
 * @file game_data_ui_runtime.h
 * @brief Runtime UI helpers for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_UI_RUNTIME_H
#define SLAYER3D_GAME_DATA_UI_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>

#include "slayer3d/game_data_render.h"
#include "slayer3d/game_data_ui.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

    /**
     * @brief Read the authored UI pulse phase.
     *
     * Returns @p fallback when no `presentation.ui_pulse_clock` is authored or
     * the named clock has no current value.
     */
    float slayer3d_game_data_ui_pulse_phase(const slayer3d_game_data_runtime *runtime, float fallback);

    /**
     * @brief Iterate authored UI text descriptors.
     *
     * This is equivalent to slayer3d_game_data_for_each_ui_text_for_metrics()
     * with a NULL metrics pointer. Use the metrics-aware iterator when menu
     * presenters or authored visibility conditions depend on frame state such
     * as pause, FPS, or active scene transition state.
     */
    bool slayer3d_game_data_for_each_ui_text(const slayer3d_game_data_runtime *runtime,
                                             slayer3d_game_data_ui_text_fn callback, void *userdata);

    /**
     * @brief Iterate authored UI text descriptors using current frame metrics.
     *
     * Iteration includes global `ui.text`, active-scene `ui.text`, and menu
     * presenters from global and active-scene `ui.menus`. Conditions on
     * generated menu presenters are evaluated with `metrics`, so app-state
     * dependent menus such as pause overlays can be rendered correctly.
     */
    bool slayer3d_game_data_for_each_ui_text_for_metrics(const slayer3d_game_data_runtime *runtime,
                                                         const slayer3d_game_data_ui_metrics *metrics,
                                                         slayer3d_game_data_ui_text_fn callback, void *userdata);

    /**
     * @brief Iterate authored UI images visible to the active scene.
     *
     * Iteration includes global `ui.images` followed by active-scene
     * `ui.images`. Visibility is evaluated separately by
     * slayer3d_game_data_ui_image_is_visible().
     */
    bool slayer3d_game_data_for_each_ui_image(const slayer3d_game_data_runtime *runtime,
                                              slayer3d_game_data_ui_image_fn callback, void *userdata);

    /**
     * @brief Iterate authored UI rectangles visible to the active scene.
     *
     * Iteration includes global `ui.rects` followed by active-scene
     * `ui.rects`. Visibility is evaluated separately by
     * slayer3d_game_data_ui_rect_is_visible().
     */
    bool slayer3d_game_data_for_each_ui_rect(const slayer3d_game_data_runtime *runtime,
                                             slayer3d_game_data_ui_rect_fn callback, void *userdata);

    /**
     * @brief Publish the logical UI viewport used to resolve retained widget layouts.
     *
     * Hosts call this with the render context's logical size (once at startup
     * and again when it changes) so every retained layout consumer — widget
     * rendering, hit testing, and editor tooling — resolves against the same
     * viewport. Until published, layouts resolve against the 1280×720 default.
     *
     * @return true when the viewport was stored; false for NULL runtime or
     * non-positive dimensions.
     */
    bool slayer3d_game_data_set_ui_viewport(slayer3d_game_data_runtime *runtime, float width, float height);

    /**
     * @brief Initialize runtime UI state to identity values.
     *
     * The initialized state has no override flags, zero offset, scale 1, alpha
     * 1, and white tint.
     */
    void slayer3d_game_data_ui_state_init(slayer3d_game_data_ui_state *state);

    /**
     * @brief Store runtime state for a named authored UI item.
     *
     * The runtime copies @p state and owns the name key internally. State
     * remains active until replaced, cleared by name, or all UI state is
     * cleared.
     */
    bool slayer3d_game_data_set_ui_state(slayer3d_game_data_runtime *runtime, const char *name,
                                         const slayer3d_game_data_ui_state *state);

    /**
     * @brief Read runtime state for a named authored UI item.
     *
     * Returns false when no state exists for @p name. @p out_state is
     * initialized to identity values before lookup.
     */
    bool slayer3d_game_data_get_ui_state(const slayer3d_game_data_runtime *runtime, const char *name,
                                         slayer3d_game_data_ui_state *out_state);

    /** @brief Clear runtime state for one named UI item. */
    bool slayer3d_game_data_clear_ui_state(slayer3d_game_data_runtime *runtime, const char *name);

    /** @brief Clear all runtime UI item state. */
    void slayer3d_game_data_clear_ui_states(slayer3d_game_data_runtime *runtime);

    /**
     * @brief Resolve authored text plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions
     * and runtime overrides. @p out_text may alias @p text.
     */
    bool slayer3d_game_data_resolve_ui_text(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_ui_text *text,
                                            const slayer3d_game_data_ui_metrics *metrics,
                                            slayer3d_game_data_ui_text *out_text, bool *out_visible);

    /**
     * @brief Resolve authored image plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions
     * and runtime overrides. @p out_image may alias @p image.
     */
    bool slayer3d_game_data_resolve_ui_image(const slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_ui_image *image,
                                             const slayer3d_game_data_ui_metrics *metrics,
                                             slayer3d_game_data_ui_image *out_image, bool *out_visible);

    /**
     * @brief Resolve authored rectangle plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions,
     * property-driven alpha, and runtime overrides. @p out_rect may alias
     * @p rect.
     */
    bool slayer3d_game_data_resolve_ui_rect(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_ui_rect *rect,
                                            const slayer3d_game_data_ui_metrics *metrics,
                                            slayer3d_game_data_ui_rect *out_rect, bool *out_visible);

    /**
     * @brief Evaluate a UI text descriptor's authored visibility condition.
     *
     * Supports camera-active checks, app pause checks, actor property
     * comparisons, and boolean all/any/not composition. Descriptors without a
     * condition are visible.
     */
    bool slayer3d_game_data_ui_text_is_visible(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_ui_text *text,
                                               const slayer3d_game_data_ui_metrics *metrics);

    /** @brief Evaluate a UI image's authored `visible_if` condition. */
    bool slayer3d_game_data_ui_image_is_visible(const slayer3d_game_data_runtime *runtime,
                                                const slayer3d_game_data_ui_image *image,
                                                const slayer3d_game_data_ui_metrics *metrics);

    /**
     * @brief Evaluate a UI rectangle descriptor's authored visibility condition.
     */
    bool slayer3d_game_data_ui_rect_is_visible(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_ui_rect *rect,
                                               const slayer3d_game_data_ui_metrics *metrics);

    /**
     * @brief Advance data-authored runtime animations.
     *
     * Animations are started by generic actions such as `ui.animate` and
     * `property.animate`. This function advances active tweens, applies eased
     * values to UI runtime state or actor properties, emits completion signals
     * for one-shot animations, and clears scene-scoped animation state when the
     * active scene changes.
     *
     * @param runtime Runtime created by slayer3d_game_data_load_file().
     * @param dt Delta time in seconds.
     * @return true when active animations were advanced successfully.
     */
    bool slayer3d_game_data_update_animations(slayer3d_game_data_runtime *runtime, float dt);

    /**
     * @brief Resolve UI text content from data-authored bindings.
     *
     * Literal `text` entries are copied directly. Entries with `bindings`
     * resolve engine metrics, brush diagnostics, scene state, and actor
     * properties, then format them using the descriptor's `format` string.
     */
    bool slayer3d_game_data_format_ui_text(const slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_ui_text *text,
                                           const slayer3d_game_data_ui_metrics *metrics, char *buffer,
                                           size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif

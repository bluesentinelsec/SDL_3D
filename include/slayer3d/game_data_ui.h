/**
 * @file game_data_ui.h
 * @brief JSON-authored game data UI descriptors.
 */

#ifndef SLAYER3D_GAME_DATA_UI_H
#define SLAYER3D_GAME_DATA_UI_H

#include <stdbool.h>

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Horizontal UI alignment for authored text and generated menu items. */
    typedef enum slayer3d_game_data_ui_align
    {
        /** @brief Anchor text at its left edge. */
        SLAYER3D_GAME_DATA_UI_ALIGN_LEFT = 0,
        /** @brief Anchor text at its center. */
        SLAYER3D_GAME_DATA_UI_ALIGN_CENTER = 1,
        /** @brief Anchor text at its right edge. */
        SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT = 2,
    } slayer3d_game_data_ui_align;

    /** @brief Vertical UI alignment for authored images. */
    typedef enum slayer3d_game_data_ui_valign
    {
        /** @brief Anchor at the top edge. */
        SLAYER3D_GAME_DATA_UI_VALIGN_TOP = 0,
        /** @brief Anchor at the center. */
        SLAYER3D_GAME_DATA_UI_VALIGN_CENTER = 1,
        /** @brief Anchor at the bottom edge. */
        SLAYER3D_GAME_DATA_UI_VALIGN_BOTTOM = 2,
    } slayer3d_game_data_ui_valign;

    /** @brief Authored UI text descriptor. */
    typedef struct slayer3d_game_data_ui_text
    {
        /** @brief Stable UI item name. */
        const char *name;
        /** @brief Font asset id. */
        const char *font;
        /** @brief Literal text, or NULL when @p format is used. */
        const char *text;
        /** @brief Format string interpreted by the caller. */
        const char *format;
        /** @brief Caller-defined source key for dynamic text. */
        const char *source;
        /** @brief Caller-defined visibility key. */
        const char *visible;
        /** @brief Horizontal position. For centered text, this is a normalized y-independent coordinate. */
        float x;
        /** @brief Vertical position. */
        float y;
        /** @brief Whether x/y are normalized to the current render size. */
        bool normalized;
        /** @brief Whether the text should be horizontally centered by the caller. */
        bool centered;
        /** @brief Horizontal alignment used by richer UI layouts. */
        slayer3d_game_data_ui_align align;
        /** @brief Runtime or authored scale multiplier applied during presentation. */
        float scale;
        /** @brief Whether alpha should pulse while visible. */
        bool pulse_alpha;
        /** @brief Text color. */
        slayer3d_color color;
    } slayer3d_game_data_ui_text;

    /** @brief Authored UI image descriptor. */
    typedef struct slayer3d_game_data_ui_image
    {
        /** @brief Stable UI item name. */
        const char *name;
        /** @brief Image asset id. */
        const char *image;
        /** @brief Caller-defined visibility key. */
        const char *visible;
        /** @brief Horizontal anchor position. */
        float x;
        /** @brief Vertical anchor position. */
        float y;
        /** @brief Desired width. */
        float w;
        /** @brief Desired height. */
        float h;
        /** @brief Whether x/y/w/h are normalized to the current render size. */
        bool normalized;
        /** @brief Whether to preserve the source image aspect ratio inside w/h. */
        bool preserve_aspect;
        /** @brief Horizontal alignment of the image rectangle around x. */
        slayer3d_game_data_ui_align align;
        /** @brief Vertical alignment of the image rectangle around y. */
        slayer3d_game_data_ui_valign valign;
        /** @brief Runtime or authored scale multiplier applied around the image anchor. */
        float scale;
        /** @brief Image tint color. */
        slayer3d_color color;
        /** @brief Optional UI image effect id, such as `melt`. */
        const char *effect;
        /** @brief Effect progression speed in effect-seconds per second. */
        float effect_speed;
    } slayer3d_game_data_ui_image;

    /** @brief Authored UI rectangle descriptor. */
    typedef struct slayer3d_game_data_ui_rect
    {
        /** @brief Stable UI item name. */
        const char *name;
        /** @brief Caller-defined visibility key. */
        const char *visible;
        /** @brief Horizontal position. */
        float x;
        /** @brief Vertical position. */
        float y;
        /** @brief Rectangle width. */
        float w;
        /** @brief Rectangle height. */
        float h;
        /** @brief Whether x/y/w/h are normalized to the current render size. */
        bool normalized;
        /** @brief Horizontal alignment of the rectangle around x. */
        slayer3d_game_data_ui_align align;
        /** @brief Vertical alignment of the rectangle around y. */
        slayer3d_game_data_ui_valign valign;
        /** @brief Runtime or authored scale multiplier applied around the rectangle anchor. */
        float scale;
        /** @brief Rectangle color. */
        slayer3d_color color;
        /** @brief Optional actor that supplies an alpha multiplier property. */
        const char *alpha_source_target;
        /** @brief Optional property on @ref alpha_source_target used as alpha source. */
        const char *alpha_source_key;
        /** @brief Multiplier applied to the alpha source property. */
        float alpha_source_scale;
        /** @brief Minimum alpha multiplier when an alpha source is authored. */
        float alpha_source_min;
        /** @brief Maximum alpha multiplier when an alpha source is authored. */
        float alpha_source_max;
        /** @brief True when alpha should pulse while visible. */
        bool pulse_alpha;
        /** @brief Pulse frequency in cycles per second. */
        float pulse_rate;
        /** @brief Minimum pulse alpha multiplier. */
        float pulse_min;
        /** @brief Maximum pulse alpha multiplier. */
        float pulse_max;
    } slayer3d_game_data_ui_rect;

    /** @brief Bit flags indicating which runtime UI state fields override authored descriptor values. */
    typedef enum slayer3d_game_data_ui_state_flags
    {
        /** @brief Override UI visibility. */
        SLAYER3D_GAME_DATA_UI_STATE_VISIBLE = 1u << 0,
        /** @brief Add a runtime x/y offset to the authored UI position. */
        SLAYER3D_GAME_DATA_UI_STATE_OFFSET = 1u << 1,
        /** @brief Multiply the authored UI scale. */
        SLAYER3D_GAME_DATA_UI_STATE_SCALE = 1u << 2,
        /** @brief Multiply the authored UI alpha. */
        SLAYER3D_GAME_DATA_UI_STATE_ALPHA = 1u << 3,
        /** @brief Multiply the authored UI tint/color. */
        SLAYER3D_GAME_DATA_UI_STATE_TINT = 1u << 4,
    } slayer3d_game_data_ui_state_flags;

    /**
     * @brief Runtime presentation state for an authored UI item.
     *
     * Runtime state is keyed by the UI item's authored `name`. It is layered on
     * top of static JSON descriptors during resolution, which lets timelines,
     * scripts, or host code animate UI elements without mutating game data.
     */
    typedef struct slayer3d_game_data_ui_state
    {
        /** @brief Combination of slayer3d_game_data_ui_state_flags values. */
        Uint32 flags;
        /** @brief Visibility override used when SLAYER3D_GAME_DATA_UI_STATE_VISIBLE is set. */
        bool visible;
        /** @brief Runtime x offset in the descriptor's coordinate space. */
        float offset_x;
        /** @brief Runtime y offset in the descriptor's coordinate space. */
        float offset_y;
        /** @brief Scale multiplier used when SLAYER3D_GAME_DATA_UI_STATE_SCALE is set. */
        float scale;
        /** @brief Alpha multiplier in [0, 1] used when SLAYER3D_GAME_DATA_UI_STATE_ALPHA is set. */
        float alpha;
        /** @brief Tint multiplier used when SLAYER3D_GAME_DATA_UI_STATE_TINT is set. */
        slayer3d_color tint;
    } slayer3d_game_data_ui_state;

    /**
     * @brief Callback for iterating authored UI text descriptors.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_ui_text_fn)(void *userdata, const slayer3d_game_data_ui_text *text);

    /**
     * @brief Callback for iterating authored UI image descriptors.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_ui_image_fn)(void *userdata, const slayer3d_game_data_ui_image *image);

    /**
     * @brief Callback for iterating authored UI rectangle descriptors.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_ui_rect_fn)(void *userdata, const slayer3d_game_data_ui_rect *rect);

#ifdef __cplusplus
}
#endif

#endif

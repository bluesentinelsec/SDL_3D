/**
 * @file game_presentation_ui.c
 * @brief UI overlay drawing helpers for JSON-authored game data.
 */

#include "game_presentation_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/drawing3d.h"

typedef struct ui_draw_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_render_context *renderer;
    slayer3d_game_data_font_cache *font_cache;
    const slayer3d_game_data_asset_warmup_queue *asset_warmup;
    const slayer3d_game_data_ui_metrics *metrics;
    float pulse_phase;
    bool ok;
} ui_draw_context;

typedef struct ui_image_draw_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_render_context *renderer;
    slayer3d_game_data_image_cache *image_cache;
    const slayer3d_game_data_asset_warmup_queue *asset_warmup;
    const slayer3d_game_data_ui_metrics *metrics;
    const slayer3d_game_data_render_eval *render_eval;
    bool ok;
} ui_image_draw_context;

typedef struct ui_rect_draw_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_render_context *renderer;
    const slayer3d_game_data_ui_metrics *metrics;
    const slayer3d_game_data_render_eval *render_eval;
    bool ok;
} ui_rect_draw_context;

typedef struct ui_scissor_scope
{
    bool active;
    bool had_previous;
    SDL_Rect previous;
} ui_scissor_scope;

static bool ui_rect_intersect_sdl(SDL_Rect a, SDL_Rect b, SDL_Rect *out_rect)
{
    const int min_x = SDL_max(a.x, b.x);
    const int min_y = SDL_max(a.y, b.y);
    const int max_x = SDL_min(a.x + a.w, b.x + b.w);
    const int max_y = SDL_min(a.y + a.h, b.y + b.h);
    if (max_x <= min_x || max_y <= min_y)
    {
        if (out_rect != NULL)
            *out_rect = (SDL_Rect){min_x, min_y, 0, 0};
        return false;
    }
    if (out_rect != NULL)
        *out_rect = (SDL_Rect){min_x, min_y, max_x - min_x, max_y - min_y};
    return true;
}

static bool ui_push_clip_rect(slayer3d_render_context *renderer, bool has_clip_rect, float clip_x, float clip_y,
                              float clip_w, float clip_h, bool clip_normalized, ui_scissor_scope *scope)
{
    if (scope != NULL)
        SDL_zero(*scope);
    if (renderer == NULL || scope == NULL || !has_clip_rect)
        return true;

    const int render_w = slayer3d_get_render_context_width(renderer);
    const int render_h = slayer3d_get_render_context_height(renderer);
    if (render_w <= 0 || render_h <= 0)
        return false;

    float x = clip_normalized ? clip_x * (float)render_w : clip_x;
    float y = clip_normalized ? clip_y * (float)render_h : clip_y;
    float w = clip_normalized ? clip_w * (float)render_w : clip_w;
    float h = clip_normalized ? clip_h * (float)render_h : clip_h;
    if (w <= 0.0f || h <= 0.0f)
        return false;

    SDL_Rect clip = {
        (int)SDL_floorf(x),
        (int)SDL_floorf(y),
        (int)SDL_ceilf(x + w) - (int)SDL_floorf(x),
        (int)SDL_ceilf(y + h) - (int)SDL_floorf(y),
    };
    if (clip.w <= 0 || clip.h <= 0)
        return false;

    scope->had_previous = slayer3d_is_scissor_enabled(renderer);
    if (scope->had_previous && !slayer3d_get_scissor_rect(renderer, &scope->previous))
        return false;
    if (scope->had_previous && !ui_rect_intersect_sdl(scope->previous, clip, &clip))
        return false;
    if (!slayer3d_set_scissor_rect(renderer, &clip))
        return false;
    scope->active = true;
    return true;
}

static void ui_pop_clip_rect(slayer3d_render_context *renderer, const ui_scissor_scope *scope)
{
    if (renderer == NULL || scope == NULL || !scope->active)
        return;
    if (scope->had_previous)
        (void)slayer3d_set_scissor_rect(renderer, &scope->previous);
    else
        (void)slayer3d_set_scissor_rect(renderer, NULL);
}

static Uint32 ui_image_hash_string(const char *s)
{
    Uint32 h = 2166136261u;
    if (s == NULL)
        return h;
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p)
    {
        h ^= (Uint32)(*p);
        h *= 16777619u;
    }
    return h != 0u ? h : 1u;
}

static slayer3d_overlay_effect ui_image_effect_from_name(const char *effect)
{
    if (effect == NULL)
        return SLAYER3D_OVERLAY_EFFECT_NONE;
    if (SDL_strcasecmp(effect, "melt") == 0)
        return SLAYER3D_OVERLAY_EFFECT_MELT;
    return SLAYER3D_OVERLAY_EFFECT_NONE;
}

static bool draw_ui_text(void *userdata, const slayer3d_game_data_ui_text *text)
{
    ui_draw_context *draw = (ui_draw_context *)userdata;
    if (draw == NULL || text == NULL)
        return false;

    slayer3d_game_data_ui_text resolved;
    bool visible = false;
    if (!slayer3d_game_data_resolve_ui_text(draw->runtime, text, draw->metrics, &resolved, &visible))
    {
        draw->ok = false;
        return true;
    }
    if (!visible)
        return true;

    char content[128];
    if (!slayer3d_game_data_format_ui_text(draw->runtime, &resolved, draw->metrics, content, sizeof(content)))
        return true;

    slayer3d_game_data_asset_warmup_state warmup_state;
    if (slayer3d_game_data_asset_warmup_request_state(draw->asset_warmup, SLAYER3D_GAME_DATA_ASSET_WARMUP_FONT, NULL,
                                                      resolved.font, &warmup_state) &&
        warmup_state != SLAYER3D_GAME_DATA_ASSET_WARMUP_READY)
    {
        return true;
    }

    slayer3d_font *font = slayer3d_game_data_find_or_load_font(draw->runtime, draw->font_cache, resolved.font);
    if (font == NULL)
    {
        draw->ok = false;
        return true;
    }

    slayer3d_color color = resolved.color;
    if (resolved.pulse_alpha)
    {
        const float pulse = 0.5f + 0.5f * SDL_sinf(draw->pulse_phase * SDL_PI_F * 2.0f);
        const float alpha = (120.0f + pulse * 135.0f) / 255.0f;
        color.a = (Uint8)SDL_clamp((int)((float)color.a * alpha + 0.5f), 0, 255);
    }

    const int width = slayer3d_get_render_context_width(draw->renderer);
    const int height = slayer3d_get_render_context_height(draw->renderer);
    const float scale = resolved.scale > 0.0f ? resolved.scale : 1.0f;
    float x = resolved.normalized ? resolved.x * (float)width : resolved.x;
    const float y = resolved.normalized ? resolved.y * (float)height : resolved.y;
    if (resolved.align == SLAYER3D_GAME_DATA_UI_ALIGN_CENTER || resolved.centered)
    {
        float text_w = 0.0f;
        float text_h = 0.0f;
        slayer3d_measure_text(font, content, &text_w, &text_h);
        x -= text_w * scale * 0.5f;
    }
    else if (resolved.align == SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT)
    {
        float text_w = 0.0f;
        float text_h = 0.0f;
        slayer3d_measure_text(font, content, &text_w, &text_h);
        x -= text_w * scale;
    }

    ui_scissor_scope clip_scope;
    if (!ui_push_clip_rect(draw->renderer, resolved.has_clip_rect, resolved.clip_x, resolved.clip_y, resolved.clip_w,
                           resolved.clip_h, resolved.clip_normalized, &clip_scope))
    {
        return true;
    }
    if (!slayer3d_draw_text_overlay_scaled(draw->renderer, font, content, x, y, scale, color))
        draw->ok = false;
    ui_pop_clip_rect(draw->renderer, &clip_scope);
    return true;
}

static void resolve_ui_image_rect(const slayer3d_game_data_ui_image *image, const slayer3d_texture2d *texture,
                                  int width, int height, float *out_x, float *out_y, float *out_w, float *out_h)
{
    float w = image->normalized ? image->w * (float)width : image->w;
    float h = image->normalized ? image->h * (float)height : image->h;
    const float texture_w = (float)texture->width;
    const float texture_h = (float)texture->height;
    const float scale = image->scale > 0.0f ? image->scale : 1.0f;

    if (w <= 0.0f && h <= 0.0f)
    {
        w = texture_w;
        h = texture_h;
    }
    else if (w <= 0.0f)
    {
        w = h * texture_w / texture_h;
    }
    else if (h <= 0.0f)
    {
        h = w * texture_h / texture_w;
    }
    else if (image->preserve_aspect)
    {
        const float fit = SDL_min(w / texture_w, h / texture_h);
        w = texture_w * fit;
        h = texture_h * fit;
    }
    w *= scale;
    h *= scale;

    float x = image->normalized ? image->x * (float)width : image->x;
    float y = image->normalized ? image->y * (float)height : image->y;
    if (image->align == SLAYER3D_GAME_DATA_UI_ALIGN_CENTER)
        x -= w * 0.5f;
    else if (image->align == SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT)
        x -= w;
    if (image->valign == SLAYER3D_GAME_DATA_UI_VALIGN_CENTER)
        y -= h * 0.5f;
    else if (image->valign == SLAYER3D_GAME_DATA_UI_VALIGN_BOTTOM)
        y -= h;

    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
}

static bool draw_ui_image(void *userdata, const slayer3d_game_data_ui_image *image)
{
    ui_image_draw_context *draw = (ui_image_draw_context *)userdata;
    if (draw == NULL || image == NULL)
        return false;

    slayer3d_game_data_ui_image resolved;
    bool visible = false;
    if (!slayer3d_game_data_resolve_ui_image(draw->runtime, image, draw->metrics, &resolved, &visible))
    {
        draw->ok = false;
        return true;
    }
    if (!visible)
        return true;

    slayer3d_game_data_asset_warmup_state warmup_state;
    if (slayer3d_game_data_asset_warmup_request_state(draw->asset_warmup, SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE,
                                                      NULL, resolved.image, &warmup_state) &&
        warmup_state != SLAYER3D_GAME_DATA_ASSET_WARMUP_READY)
    {
        return true;
    }

    slayer3d_game_data_image_cache_entry *entry =
        slayer3d_game_data_find_or_load_image_entry(draw->runtime, draw->image_cache, resolved.image);
    if (entry == NULL)
    {
        draw->ok = false;
        return true;
    }
    slayer3d_texture2d *texture = &entry->texture;

    const int width = slayer3d_get_render_context_width(draw->renderer);
    const int height = slayer3d_get_render_context_height(draw->renderer);
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    resolve_ui_image_rect(&resolved, texture, width, height, &x, &y, &w, &h);
    const char *effect_name = resolved.effect != NULL ? resolved.effect : entry->effect;
    const slayer3d_overlay_effect effect = ui_image_effect_from_name(effect_name);
    const float effect_progress =
        effect != SLAYER3D_OVERLAY_EFFECT_NONE && draw->render_eval != NULL
            ? SDL_clamp((draw->render_eval->time - entry->effect_delay) / SDL_max(entry->effect_duration, 0.0001f),
                        0.0f, 1.0f)
            : 0.0f;
    const Uint32 effect_seed = ui_image_hash_string(resolved.name);
    const bool has_custom_shader = (entry->shader_vertex_source != NULL && entry->shader_vertex_source[0] != '\0') ||
                                   (entry->shader_fragment_source != NULL && entry->shader_fragment_source[0] != '\0');
    ui_scissor_scope clip_scope;
    if (!ui_push_clip_rect(draw->renderer, resolved.has_clip_rect, resolved.clip_x, resolved.clip_y, resolved.clip_w,
                           resolved.clip_h, resolved.clip_normalized, &clip_scope))
    {
        return true;
    }
    const bool drawn = has_custom_shader
                           ? slayer3d_draw_texture_overlay_shader(
                                 draw->renderer, texture, x, y, w, h, resolved.color, effect, effect_progress,
                                 effect_seed, entry->shader_vertex_source, entry->shader_fragment_source)
                           : slayer3d_draw_texture_overlay(draw->renderer, texture, x, y, w, h, resolved.color, effect,
                                                           effect_progress, effect_seed);
    if (!drawn)
        draw->ok = false;
    ui_pop_clip_rect(draw->renderer, &clip_scope);
    return true;
}

static void resolve_ui_rect_rect(const slayer3d_game_data_ui_rect *rect, int width, int height, float *out_x,
                                 float *out_y, float *out_w, float *out_h)
{
    float w = rect->normalized ? rect->w * (float)width : rect->w;
    float h = rect->normalized ? rect->h * (float)height : rect->h;
    const float scale = rect->scale > 0.0f ? rect->scale : 1.0f;
    w *= scale;
    h *= scale;

    float x = rect->normalized ? rect->x * (float)width : rect->x;
    float y = rect->normalized ? rect->y * (float)height : rect->y;
    if (rect->align == SLAYER3D_GAME_DATA_UI_ALIGN_CENTER)
        x -= w * 0.5f;
    else if (rect->align == SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT)
        x -= w;
    if (rect->valign == SLAYER3D_GAME_DATA_UI_VALIGN_CENTER)
        y -= h * 0.5f;
    else if (rect->valign == SLAYER3D_GAME_DATA_UI_VALIGN_BOTTOM)
        y -= h;

    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
}

static bool draw_ui_rect(void *userdata, const slayer3d_game_data_ui_rect *rect)
{
    ui_rect_draw_context *draw = (ui_rect_draw_context *)userdata;
    if (draw == NULL || rect == NULL)
        return false;

    slayer3d_game_data_ui_rect resolved;
    bool visible = false;
    if (!slayer3d_game_data_resolve_ui_rect(draw->runtime, rect, draw->metrics, &resolved, &visible))
    {
        draw->ok = false;
        return true;
    }
    if (!visible)
        return true;

    slayer3d_color color = resolved.color;
    if (resolved.pulse_alpha)
    {
        const float time = draw->render_eval != NULL ? draw->render_eval->time : 0.0f;
        const float pulse = 0.5f + 0.5f * SDL_sinf(time * SDL_max(resolved.pulse_rate, 0.0f) * SDL_PI_F * 2.0f);
        const float alpha = resolved.pulse_min + (resolved.pulse_max - resolved.pulse_min) * pulse;
        color.a = (Uint8)SDL_clamp((int)((float)color.a * SDL_clamp(alpha, 0.0f, 1.0f) + 0.5f), 0, 255);
    }
    if (color.a == 0)
        return true;

    const int width = slayer3d_get_render_context_width(draw->renderer);
    const int height = slayer3d_get_render_context_height(draw->renderer);
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    resolve_ui_rect_rect(&resolved, width, height, &x, &y, &w, &h);
    ui_scissor_scope clip_scope;
    if (!ui_push_clip_rect(draw->renderer, resolved.has_clip_rect, resolved.clip_x, resolved.clip_y, resolved.clip_w,
                           resolved.clip_h, resolved.clip_normalized, &clip_scope))
    {
        return true;
    }
    if (!slayer3d_draw_rect_overlay(draw->renderer, x, y, w, h, color))
        draw->ok = false;
    ui_pop_clip_rect(draw->renderer, &clip_scope);
    return true;
}

bool slayer3d_game_data_draw_ui_text(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                     slayer3d_game_data_font_cache *font_cache,
                                     const slayer3d_game_data_asset_warmup_queue *asset_warmup,
                                     const slayer3d_game_data_ui_metrics *metrics, float pulse_phase)
{
    if (runtime == NULL || renderer == NULL || font_cache == NULL)
        return false;

    ui_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.font_cache = font_cache;
    context.asset_warmup = asset_warmup;
    context.metrics = metrics;
    context.pulse_phase = pulse_phase;
    context.ok = true;

    return slayer3d_game_data_for_each_ui_text_for_metrics(runtime, metrics, draw_ui_text, &context) && context.ok;
}

bool slayer3d_game_data_draw_ui_images(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                       slayer3d_game_data_image_cache *image_cache,
                                       const slayer3d_game_data_asset_warmup_queue *asset_warmup,
                                       const slayer3d_game_data_ui_metrics *metrics,
                                       const slayer3d_game_data_render_eval *render_eval)
{
    if (runtime == NULL || renderer == NULL || image_cache == NULL)
        return false;

    ui_image_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.image_cache = image_cache;
    context.asset_warmup = asset_warmup;
    context.metrics = metrics;
    context.render_eval = render_eval;
    context.ok = true;

    return slayer3d_game_data_for_each_ui_image(runtime, draw_ui_image, &context) && context.ok;
}

bool slayer3d_game_data_draw_ui_rects(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                      const slayer3d_game_data_ui_metrics *metrics,
                                      const slayer3d_game_data_render_eval *render_eval)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    ui_rect_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.metrics = metrics;
    context.render_eval = render_eval;
    context.ok = true;

    return slayer3d_game_data_for_each_ui_rect(runtime, draw_ui_rect, &context) && context.ok;
}

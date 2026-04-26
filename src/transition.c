#include "sdl3d/transition.h"

#include <SDL3/SDL_stdinc.h>

#include "render_context_internal.h"
#include "transition_gl.h"

#define SDL3D_TRANSITION_MIN_DURATION 0.001f
#define SDL3D_TRANSITION_EPSILON 0.00001f

static float sdl3d_transition_clampf(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static Uint32 sdl3d_transition_hash_u32(Uint32 value)
{
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

static void sdl3d_transition_seed_melt_offsets(sdl3d_transition *transition)
{
    for (int i = 0; i < SDL3D_TRANSITION_MELT_COLUMNS; ++i)
    {
        const Uint32 hash = sdl3d_transition_hash_u32((Uint32)i + 0x9e3779b9U);
        transition->melt_offsets[i] = (float)hash / 4294967295.0f;
    }
}

static float sdl3d_transition_cover_amount(const sdl3d_transition *transition)
{
    const float progress = sdl3d_transition_progress(transition);
    return transition->direction == SDL3D_TRANSITION_IN ? 1.0f - progress : progress;
}

static sdl3d_color sdl3d_transition_color_with_alpha(sdl3d_color color, float alpha_scale)
{
    const float alpha = sdl3d_transition_clampf(alpha_scale, 0.0f, 1.0f) * (float)color.a;
    color.a = (Uint8)(alpha + 0.5f);
    return color;
}

static Uint8 sdl3d_transition_blend_channel(Uint8 dst, Uint8 src, float alpha)
{
    const float blended = (float)src * alpha + (float)dst * (1.0f - alpha);
    return (Uint8)sdl3d_transition_clampf(blended + 0.5f, 0.0f, 255.0f);
}

static void sdl3d_transition_blend_pixel(sdl3d_render_context *context, int x, int y, sdl3d_color color)
{
    if (x < 0 || y < 0 || x >= context->width || y >= context->height || color.a == 0)
    {
        return;
    }

    Uint8 *pixel = &context->color_buffer[((y * context->width) + x) * 4];
    if (color.a == 255)
    {
        pixel[0] = color.r;
        pixel[1] = color.g;
        pixel[2] = color.b;
        pixel[3] = color.a;
        return;
    }

    const float alpha = (float)color.a / 255.0f;
    pixel[0] = sdl3d_transition_blend_channel(pixel[0], color.r, alpha);
    pixel[1] = sdl3d_transition_blend_channel(pixel[1], color.g, alpha);
    pixel[2] = sdl3d_transition_blend_channel(pixel[2], color.b, alpha);
    pixel[3] = 255;
}

static void sdl3d_transition_fill_rect_sw(sdl3d_render_context *context, int x, int y, int w, int h, sdl3d_color color)
{
    if (context == NULL || context->color_buffer == NULL || w <= 0 || h <= 0 || color.a == 0)
    {
        return;
    }

    int min_x = x;
    int min_y = y;
    int max_x = x + w;
    int max_y = y + h;

    if (min_x < 0)
    {
        min_x = 0;
    }
    if (min_y < 0)
    {
        min_y = 0;
    }
    if (max_x > context->width)
    {
        max_x = context->width;
    }
    if (max_y > context->height)
    {
        max_y = context->height;
    }

    for (int py = min_y; py < max_y; ++py)
    {
        for (int px = min_x; px < max_x; ++px)
        {
            sdl3d_transition_blend_pixel(context, px, py, color);
        }
    }
}

static void sdl3d_transition_draw_fade_sw(const sdl3d_transition *transition, sdl3d_render_context *context,
                                          float cover)
{
    const sdl3d_color color = sdl3d_transition_color_with_alpha(transition->color, cover);
    sdl3d_transition_fill_rect_sw(context, 0, 0, context->width, context->height, color);
}

static void sdl3d_transition_draw_circle_sw(const sdl3d_transition *transition, sdl3d_render_context *context,
                                            float cover)
{
    const float cx = (float)context->width * 0.5f;
    const float cy = (float)context->height * 0.5f;
    const float max_radius = SDL_sqrtf(cx * cx + cy * cy);
    const float radius = max_radius * (1.0f - cover);
    const int band_h = SDL_max(1, context->height / 200);

    if (radius <= SDL3D_TRANSITION_EPSILON)
    {
        sdl3d_transition_fill_rect_sw(context, 0, 0, context->width, context->height, transition->color);
        return;
    }

    if (radius >= max_radius - SDL3D_TRANSITION_EPSILON)
    {
        return;
    }

    for (int y = 0; y < context->height; y += band_h)
    {
        const int h = SDL_min(band_h, context->height - y);
        const float sample_y = (float)y + (float)h * 0.5f;
        const float dy = sample_y - cy;
        const float abs_dy = dy < 0.0f ? -dy : dy;
        if (abs_dy >= radius)
        {
            sdl3d_transition_fill_rect_sw(context, 0, y, context->width, h, transition->color);
            continue;
        }

        const float half_width = SDL_sqrtf(radius * radius - abs_dy * abs_dy);
        const int left_w = (int)(cx - half_width);
        const int right_x = (int)(cx + half_width + 0.5f);
        sdl3d_transition_fill_rect_sw(context, 0, y, left_w, h, transition->color);
        sdl3d_transition_fill_rect_sw(context, right_x, y, context->width - right_x, h, transition->color);
    }
}

static void sdl3d_transition_draw_melt_sw(const sdl3d_transition *transition, sdl3d_render_context *context,
                                          float cover)
{
    const int band_w = SDL_max(1, context->width / SDL3D_TRANSITION_MELT_COLUMNS);
    for (int x = 0; x < context->width; x += band_w)
    {
        const int column = SDL_min(SDL3D_TRANSITION_MELT_COLUMNS - 1,
                                   (x * SDL3D_TRANSITION_MELT_COLUMNS) / SDL_max(1, context->width));
        const float offset = transition->melt_offsets[column];
        const float column_progress = sdl3d_transition_clampf((cover - offset * 0.3f) / 0.7f, 0.0f, 1.0f);
        const int cover_h = (int)((float)context->height * column_progress + 0.5f);
        sdl3d_transition_fill_rect_sw(context, x, 0, band_w, cover_h, transition->color);
    }
}

static float sdl3d_transition_smoothstep(float edge0, float edge1, float value)
{
    const float t = sdl3d_transition_clampf((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static void sdl3d_transition_draw_pixelate_sw(const sdl3d_transition *transition, sdl3d_render_context *context,
                                              float cover)
{
    const float block_size_f = 1.0f + (63.0f * cover);
    const int block_size = SDL_max(1, (int)(block_size_f + 0.5f));
    const sdl3d_color color =
        sdl3d_transition_color_with_alpha(transition->color, sdl3d_transition_smoothstep(0.15f, 1.0f, cover));

    for (int y = 0; y < context->height; y += block_size)
    {
        for (int x = 0; x < context->width; x += block_size)
        {
            sdl3d_transition_fill_rect_sw(context, x, y, block_size, block_size, color);
        }
    }
}

static void sdl3d_transition_draw_sw(const sdl3d_transition *transition, sdl3d_render_context *context)
{
    const float cover = sdl3d_transition_clampf(sdl3d_transition_cover_amount(transition), 0.0f, 1.0f);
    if (cover <= 0.0f)
    {
        return;
    }

    switch (transition->type)
    {
    case SDL3D_TRANSITION_FADE:
        sdl3d_transition_draw_fade_sw(transition, context, cover);
        break;
    case SDL3D_TRANSITION_CIRCLE:
        sdl3d_transition_draw_circle_sw(transition, context, cover);
        break;
    case SDL3D_TRANSITION_MELT:
        sdl3d_transition_draw_melt_sw(transition, context, cover);
        break;
    case SDL3D_TRANSITION_PIXELATE:
        sdl3d_transition_draw_pixelate_sw(transition, context, cover);
        break;
    default:
        sdl3d_transition_draw_fade_sw(transition, context, cover);
        break;
    }
}

void sdl3d_transition_start(sdl3d_transition *transition, sdl3d_transition_type type,
                            sdl3d_transition_direction direction, sdl3d_color color, float duration, int done_signal_id)
{
    if (transition == NULL)
    {
        return;
    }

    SDL_zero(*transition);
    transition->type = type;
    transition->direction = direction;
    transition->color = color;
    transition->duration = duration > 0.0f ? duration : SDL3D_TRANSITION_MIN_DURATION;
    transition->active = true;
    transition->finished = false;
    transition->done_signal_id = done_signal_id;

    if (type == SDL3D_TRANSITION_MELT)
    {
        sdl3d_transition_seed_melt_offsets(transition);
    }
}

void sdl3d_transition_update(sdl3d_transition *transition, sdl3d_signal_bus *bus, float dt)
{
    if (transition == NULL || !transition->active)
    {
        return;
    }

    if (dt > 0.0f)
    {
        transition->elapsed += dt;
    }

    if (transition->elapsed >= transition->duration)
    {
        transition->elapsed = transition->duration;
        transition->active = false;
        transition->finished = true;
        if (transition->done_signal_id >= 0 && bus != NULL)
        {
            sdl3d_signal_emit(bus, transition->done_signal_id, NULL);
        }
    }
}

void sdl3d_transition_draw(const sdl3d_transition *transition, sdl3d_render_context *context)
{
    if (transition == NULL || context == NULL || !transition->active)
    {
        return;
    }

    if (context->gl != NULL)
    {
        (void)sdl3d_transition_draw_gl(transition, context);
        return;
    }

    sdl3d_transition_draw_sw(transition, context);
}

void sdl3d_transition_reset(sdl3d_transition *transition)
{
    if (transition == NULL)
    {
        return;
    }

    SDL_zero(*transition);
    transition->done_signal_id = -1;
}

float sdl3d_transition_progress(const sdl3d_transition *transition)
{
    if (transition == NULL || transition->duration <= 0.0f)
    {
        return 0.0f;
    }

    return sdl3d_transition_clampf(transition->elapsed / transition->duration, 0.0f, 1.0f);
}

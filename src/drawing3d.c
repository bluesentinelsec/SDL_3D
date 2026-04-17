#include "sdl3d/drawing3d.h"

#include <SDL3/SDL_error.h>

#include "rasterizer.h"
#include "render_context_internal.h"

bool sdl3d_begin_mode_3d(sdl3d_render_context *context, sdl3d_camera3d camera)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (context->in_mode_3d)
    {
        return SDL_SetError("sdl3d_begin_mode_3d called while already in 3D mode.");
    }

    if (!sdl3d_camera3d_compute_matrices(&camera, context->width, context->height, context->near_plane,
                                         context->far_plane, &context->view, &context->projection))
    {
        return false;
    }

    context->view_projection = sdl3d_mat4_multiply(context->projection, context->view);
    context->in_mode_3d = true;
    return true;
}

bool sdl3d_end_mode_3d(sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (!context->in_mode_3d)
    {
        return SDL_SetError("sdl3d_end_mode_3d called while not in 3D mode.");
    }

    context->in_mode_3d = false;
    return true;
}

bool sdl3d_is_in_mode_3d(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return false;
    }
    return context->in_mode_3d;
}

bool sdl3d_set_backface_culling_enabled(sdl3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->backface_culling_enabled = enabled;
    return true;
}

bool sdl3d_is_backface_culling_enabled(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return false;
    }

    return context->backface_culling_enabled;
}

bool sdl3d_set_wireframe_enabled(sdl3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->wireframe_enabled = enabled;
    return true;
}

bool sdl3d_is_wireframe_enabled(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return false;
    }

    return context->wireframe_enabled;
}

bool sdl3d_set_depth_planes(sdl3d_render_context *context, float near_plane, float far_plane)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (context->in_mode_3d)
    {
        return SDL_SetError("Depth planes cannot be changed while in 3D mode.");
    }

    if (!(near_plane > 0.0f) || !(far_plane > near_plane))
    {
        return SDL_SetError("Depth planes require 0 < near_plane < far_plane.");
    }

    context->near_plane = near_plane;
    context->far_plane = far_plane;
    return true;
}

static bool sdl3d_require_mode_3d(const sdl3d_render_context *context, const char *function_name)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (!context->in_mode_3d)
    {
        return SDL_SetError("%s called outside sdl3d_begin_mode_3d / sdl3d_end_mode_3d.", function_name);
    }
    return true;
}

bool sdl3d_draw_triangle_3d(sdl3d_render_context *context, sdl3d_vec3 v0, sdl3d_vec3 v1, sdl3d_vec3 v2,
                            sdl3d_color color)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_draw_triangle_3d"))
    {
        return false;
    }

    sdl3d_framebuffer framebuffer = sdl3d_framebuffer_from_context(context);
    sdl3d_rasterize_triangle(&framebuffer, context->view_projection, v0, v1, v2, color,
                             context->backface_culling_enabled, context->wireframe_enabled);
    return true;
}

bool sdl3d_draw_line_3d(sdl3d_render_context *context, sdl3d_vec3 start, sdl3d_vec3 end, sdl3d_color color)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_draw_line_3d"))
    {
        return false;
    }

    sdl3d_framebuffer framebuffer = sdl3d_framebuffer_from_context(context);
    sdl3d_rasterize_line(&framebuffer, context->view_projection, start, end, color);
    return true;
}

bool sdl3d_draw_point_3d(sdl3d_render_context *context, sdl3d_vec3 position, sdl3d_color color)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_draw_point_3d"))
    {
        return false;
    }

    sdl3d_framebuffer framebuffer = sdl3d_framebuffer_from_context(context);
    sdl3d_rasterize_point(&framebuffer, context->view_projection, position, color);
    return true;
}

bool sdl3d_get_framebuffer_pixel(const sdl3d_render_context *context, int x, int y, sdl3d_color *out_color)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (out_color == NULL)
    {
        return SDL_InvalidParamError("out_color");
    }
    if (x < 0 || x >= context->width || y < 0 || y >= context->height)
    {
        return SDL_SetError("Framebuffer pixel (%d, %d) is outside the %dx%d backbuffer.", x, y, context->width,
                            context->height);
    }

    const int index = (y * context->width) + x;
    const Uint8 *pixel = &context->color_buffer[index * 4];
    out_color->r = pixel[0];
    out_color->g = pixel[1];
    out_color->b = pixel[2];
    out_color->a = pixel[3];
    return true;
}

bool sdl3d_get_framebuffer_depth(const sdl3d_render_context *context, int x, int y, float *out_depth)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (out_depth == NULL)
    {
        return SDL_InvalidParamError("out_depth");
    }
    if (x < 0 || x >= context->width || y < 0 || y >= context->height)
    {
        return SDL_SetError("Framebuffer depth (%d, %d) is outside the %dx%d backbuffer.", x, y, context->width,
                            context->height);
    }

    *out_depth = context->depth_buffer[(y * context->width) + x];
    return true;
}

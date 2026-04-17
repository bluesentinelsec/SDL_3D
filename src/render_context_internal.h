/*
 * Internal layout of sdl3d_render_context, shared between render_context.c
 * and the software pipeline (rasterizer.c / drawing3d.c). Not part of the
 * public API.
 */

#ifndef SDL3D_INTERNAL_RENDER_CONTEXT_H
#define SDL3D_INTERNAL_RENDER_CONTEXT_H

#include <stdbool.h>

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

#include "rasterizer.h"
#include "sdl3d/math.h"
#include "sdl3d/render_context.h"

struct sdl3d_render_context
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *color_texture;
    Uint8 *color_buffer;
    float *depth_buffer;
    sdl3d_backend backend;
    int width;
    int height;

    float near_plane;
    float far_plane;

    bool in_mode_3d;
    bool backface_culling_enabled;
    bool wireframe_enabled;
    bool scissor_enabled;
    SDL_Rect scissor_rect;
    sdl3d_mat4 view;
    sdl3d_mat4 projection;
    sdl3d_mat4 view_projection;
};

static inline sdl3d_framebuffer sdl3d_framebuffer_from_context(sdl3d_render_context *context)
{
    sdl3d_framebuffer framebuffer;
    framebuffer.color_pixels = context->color_buffer;
    framebuffer.depth_pixels = context->depth_buffer;
    framebuffer.width = context->width;
    framebuffer.height = context->height;
    framebuffer.scissor_enabled = context->scissor_enabled;
    framebuffer.scissor_rect = context->scissor_rect;
    return framebuffer;
}

#endif

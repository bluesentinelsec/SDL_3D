/*
 * Software backend implementation for the sdl3d_backend_interface.
 *
 * Delegates to the existing CPU rasterizer and SDL2D texture upload path.
 */

#include "backend.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

#include "rasterizer.h"
#include "render_context_internal.h"

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void sw_destroy(sdl3d_render_context *context)
{
    sdl3d_parallel_rasterizer_destroy(context->parallel_rasterizer);
    context->parallel_rasterizer = NULL;
    SDL_DestroyTexture(context->color_texture);
    context->color_texture = NULL;
    SDL_free(context->color_buffer);
    context->color_buffer = NULL;
    SDL_free(context->depth_buffer);
    context->depth_buffer = NULL;
}

/* ------------------------------------------------------------------ */
/* Per-frame operations                                                */
/* ------------------------------------------------------------------ */

static bool sw_clear(sdl3d_render_context *context, sdl3d_color color)
{
    sdl3d_framebuffer framebuffer = sdl3d_framebuffer_from_context(context);
    sdl3d_framebuffer_clear(&framebuffer, color, 1.0f);
    return true;
}

static bool sw_present(sdl3d_render_context *context)
{
    if (!SDL_UpdateTexture(context->color_texture, NULL, context->color_buffer, context->width * 4))
    {
        return false;
    }

    if (!SDL_SetRenderDrawColor(context->renderer, 0, 0, 0, 255))
    {
        return false;
    }

    if (!SDL_RenderClear(context->renderer))
    {
        return false;
    }

    if (!SDL_RenderTexture(context->renderer, context->color_texture, NULL, NULL))
    {
        return false;
    }

    return SDL_RenderPresent(context->renderer);
}

/* ------------------------------------------------------------------ */
/* Drawing — software path returns false to signal "use inline path"   */
/*                                                                     */
/* The software rasterizer operates per-triangle with full pipeline    */
/* state from the render context (clipping, shading, fog, profiles).   */
/* That loop lives in drawing3d.c and is too tightly coupled to the    */
/* context internals to extract into a simple draw_mesh call today.    */
/*                                                                     */
/* Returning false tells the caller to fall through to the existing    */
/* software triangle loop.  This keeps the refactor incremental: the   */
/* vtable handles clear/present/destroy now, and the draw path can be  */
/* migrated in a future pass without a risky big-bang rewrite.         */
/* ------------------------------------------------------------------ */

static bool sw_draw_mesh_unlit(sdl3d_render_context *context, const sdl3d_draw_params_unlit *params)
{
    (void)context;
    (void)params;
    return false; /* fall through to inline software path */
}

static bool sw_draw_mesh_lit(sdl3d_render_context *context, const sdl3d_draw_params_lit *params)
{
    (void)context;
    (void)params;
    return false; /* fall through to inline software path */
}

/* ------------------------------------------------------------------ */
/* Interface initializer                                               */
/* ------------------------------------------------------------------ */

void sdl3d_sw_backend_init(sdl3d_backend_interface *iface)
{
    iface->destroy = sw_destroy;
    iface->clear = sw_clear;
    iface->present = sw_present;
    iface->draw_mesh_unlit = sw_draw_mesh_unlit;
    iface->draw_mesh_lit = sw_draw_mesh_lit;
}

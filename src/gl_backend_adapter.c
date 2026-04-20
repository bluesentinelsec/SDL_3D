/*
 * OpenGL backend implementation for the sdl3d_backend_interface.
 *
 * Adapts the existing sdl3d_gl_* functions to the backend vtable.
 */

#include "backend.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include "gl_backend.h"
#include "render_context_internal.h"

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void gl_destroy(sdl3d_render_context *context)
{
    sdl3d_gl_destroy(context->gl);
    context->gl = NULL;
}

/* ------------------------------------------------------------------ */
/* Per-frame operations                                                */
/* ------------------------------------------------------------------ */

static bool gl_clear(sdl3d_render_context *context, sdl3d_color color)
{
    sdl3d_gl_clear(context->gl, (float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f,
                   (float)color.a / 255.0f);
    return true;
}

static bool gl_present(sdl3d_render_context *context)
{
    bool swapped;

    sdl3d_gl_present(context->gl, context->window);
    if (!sdl3d_gl_is_doublebuffered(context->gl))
    {
        sdl3d_gl_finish(context->gl);
        (void)SDL_GL_SwapWindow(context->window);
        return true;
    }
    swapped = SDL_GL_SwapWindow(context->window);
    if (!swapped)
    {
        SDL_Log("SDL3D GL swap failed: %s", SDL_GetError());
    }
    return swapped;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static bool gl_draw_mesh_unlit(sdl3d_render_context *context, const sdl3d_draw_params_unlit *params)
{
    sdl3d_gl_draw_mesh_unlit(context->gl, params);
    return true;
}

static bool gl_draw_mesh_lit(sdl3d_render_context *context, const sdl3d_draw_params_lit *params)
{
    sdl3d_gl_draw_mesh_lit(context->gl, params);
    return true;
}

/* ------------------------------------------------------------------ */
/* Interface initializer                                               */
/* ------------------------------------------------------------------ */

void sdl3d_gl_backend_init(sdl3d_backend_interface *iface)
{
    iface->destroy = gl_destroy;
    iface->clear = gl_clear;
    iface->present = gl_present;
    iface->draw_mesh_unlit = gl_draw_mesh_unlit;
    iface->draw_mesh_lit = gl_draw_mesh_lit;
}

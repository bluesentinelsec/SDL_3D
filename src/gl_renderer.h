/*
 * Internal OpenGL 3.3 renderer for SDL3D. Not part of the public API.
 */

#ifndef SDL3D_GL_RENDERER_H
#define SDL3D_GL_RENDERER_H

#include <SDL3/SDL.h>

#include "backend.h"

typedef struct sdl3d_gl_context sdl3d_gl_context;

sdl3d_gl_context *sdl3d_gl_create(SDL_Window *window, int width, int height);
void sdl3d_gl_destroy(sdl3d_gl_context *ctx);

#endif

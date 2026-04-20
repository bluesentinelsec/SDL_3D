/*
 * Internal OpenGL backend interface. Not part of the public SDL3D API.
 */

#ifndef SDL3D_GL_BACKEND_H
#define SDL3D_GL_BACKEND_H

#include <SDL3/SDL.h>

#include "gl_funcs.h"

typedef struct sdl3d_gl_context sdl3d_gl_context;

sdl3d_gl_context *sdl3d_gl_create(SDL_Window *window, int width, int height);
void sdl3d_gl_destroy(sdl3d_gl_context *ctx);
void sdl3d_gl_clear(sdl3d_gl_context *ctx, float r, float g, float b, float a);

GLuint sdl3d_gl_get_unlit_program(const sdl3d_gl_context *ctx);
GLuint sdl3d_gl_get_lit_program(const sdl3d_gl_context *ctx);
const sdl3d_gl_funcs *sdl3d_gl_get_funcs(const sdl3d_gl_context *ctx);
GLuint sdl3d_gl_get_white_texture(const sdl3d_gl_context *ctx);

#endif

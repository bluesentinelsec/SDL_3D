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

void sdl3d_gl_post_process(sdl3d_gl_context *ctx, int effects, float bloom_threshold, float bloom_intensity,
                           float vignette_intensity, float contrast, float brightness, float saturation);

/* Read a pixel from the GL FBO at (x, y). Returns RGBA as 4 bytes.
 * Used for automated testing. */
void sdl3d_gl_read_pixel(sdl3d_gl_context *ctx, int x, int y, unsigned char *rgba);

/* Shadow pass control. */
void sdl3d_gl_begin_shadow_pass(sdl3d_gl_context *ctx, const float *light_vp, float bias);
void sdl3d_gl_end_shadow_pass(sdl3d_gl_context *ctx);

#endif

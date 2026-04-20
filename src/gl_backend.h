/*
 * Internal OpenGL backend interface. Not part of the public SDL3D API.
 */

#ifndef SDL3D_GL_BACKEND_H
#define SDL3D_GL_BACKEND_H

#include <SDL3/SDL.h>

#include "backend.h"

/* GL types needed for the public interface. */
typedef unsigned int GLuint;

typedef struct sdl3d_gl_context sdl3d_gl_context;

sdl3d_gl_context *sdl3d_gl_create(SDL_Window *window, int width, int height);
void sdl3d_gl_destroy(sdl3d_gl_context *ctx);
void sdl3d_gl_clear(sdl3d_gl_context *ctx, float r, float g, float b, float a);
void sdl3d_gl_present(sdl3d_gl_context *ctx, SDL_Window *window);
void sdl3d_gl_flush(sdl3d_gl_context *ctx);
void sdl3d_gl_finish(sdl3d_gl_context *ctx);
bool sdl3d_gl_is_doublebuffered(const sdl3d_gl_context *ctx);

GLuint sdl3d_gl_get_unlit_program(const sdl3d_gl_context *ctx);
GLuint sdl3d_gl_get_lit_program(const sdl3d_gl_context *ctx);
GLuint sdl3d_gl_get_white_texture(const sdl3d_gl_context *ctx);

/* Get the shader program for a given shading mode. */
GLuint sdl3d_gl_get_program_for_profile(const sdl3d_gl_context *ctx, int shading_mode, bool has_lights);

/*
 * Draw a mesh using the unlit shader. Positions, UVs, and colors are
 * uploaded as a dynamic VBO each call (immediate-mode style, matching
 * the software renderer's per-frame draw pattern).
 */
void sdl3d_gl_draw_mesh_unlit(sdl3d_gl_context *ctx, const sdl3d_draw_params_unlit *params);

void sdl3d_gl_draw_mesh_lit(sdl3d_gl_context *ctx, const sdl3d_draw_params_lit *params);

/* Apply post-processing effects to the GL FBO as a fullscreen shader pass. */
void sdl3d_gl_post_process(sdl3d_gl_context *ctx, int effects, float bloom_threshold, float bloom_intensity,
                           float vignette_intensity, float contrast, float brightness, float saturation);

#endif

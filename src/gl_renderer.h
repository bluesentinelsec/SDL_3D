/*
 * Internal OpenGL 3.3 renderer for SDL3D. Not part of the public API.
 */

#ifndef SDL3D_GL_RENDERER_H
#define SDL3D_GL_RENDERER_H

#include <SDL3/SDL.h>

#include "backend.h"
#include "sdl3d/drawing3d.h"
#include "sdl3d/transition.h"

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

/* IBL: load an HDRI environment map and generate irradiance/prefilter/BRDF LUT. */
bool sdl3d_gl_load_environment_map(sdl3d_gl_context *ctx, const char *hdr_path);

/* Append a textured quad to the overlay draw list. The overlay is rendered
 * after the FBO blit, bypassing all post-processing. `positions` (3 floats
 * per vertex) and `uvs` (2 floats per vertex) are copied. `tex_pixels` is
 * also copied so the caller can free/reload the texture before present. */
bool sdl3d_gl_append_overlay(sdl3d_gl_context *ctx, const float *positions, const float *uvs, int vertex_count,
                             const float *mvp, const float *tint, const sdl3d_texture2d *texture, bool scissor_enabled,
                             const SDL_Rect *scissor_rect, sdl3d_overlay_effect effect, float effect_progress,
                             Uint32 effect_seed);

bool sdl3d_gl_append_line(sdl3d_gl_context *ctx, const float *positions, const float *colors, const float *mvp);

/* Queue a transition to be applied during the next GL present after
 * post-processing and before the final window blit. */
bool sdl3d_gl_queue_transition(sdl3d_gl_context *ctx, const sdl3d_transition *transition);

#endif

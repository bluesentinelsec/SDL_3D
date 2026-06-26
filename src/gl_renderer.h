/*
 * Internal OpenGL 3.3 renderer for SLAYER3D. Not part of the public API.
 */

#ifndef SLAYER3D_GL_RENDERER_H
#define SLAYER3D_GL_RENDERER_H

#include <SDL3/SDL.h>

#include "backend.h"
#include "slayer3d/drawing3d.h"
#include "slayer3d/transition.h"

typedef struct slayer3d_gl_context slayer3d_gl_context;

slayer3d_gl_context *slayer3d_gl_create(SDL_Window *window, int width, int height);
void slayer3d_gl_destroy(slayer3d_gl_context *ctx);
bool slayer3d_gl_set_world_render_scale(slayer3d_gl_context *ctx, int logical_width, int logical_height, float scale,
                                        int *out_width, int *out_height);
bool slayer3d_gl_sync_world_render_target(slayer3d_gl_context *ctx, int logical_width, int logical_height, float scale,
                                          int *out_width, int *out_height);
float slayer3d_gl_world_render_scale(const slayer3d_gl_context *ctx);
void slayer3d_gl_world_render_size(const slayer3d_gl_context *ctx, int *out_width, int *out_height);

void slayer3d_gl_post_process(slayer3d_gl_context *ctx, int effects, float bloom_threshold, float bloom_intensity,
                              float vignette_intensity, float contrast, float brightness, float saturation);

/* Read a pixel from the GL FBO at (x, y). Returns RGBA as 4 bytes.
 * Used for automated testing. */
void slayer3d_gl_read_pixel(slayer3d_gl_context *ctx, int x, int y, unsigned char *rgba);
int slayer3d_gl_active_retro_profile(const slayer3d_gl_context *ctx);
void slayer3d_gl_active_retro_virtual_resolution(const slayer3d_gl_context *ctx, int *out_width, int *out_height);
int slayer3d_gl_active_retro_filter(const slayer3d_gl_context *ctx);

/* Shadow pass control. */
void slayer3d_gl_begin_shadow_pass(slayer3d_gl_context *ctx, const float *light_vp, float bias);
void slayer3d_gl_end_shadow_pass(slayer3d_gl_context *ctx);

/* IBL: load an HDRI environment map and generate irradiance/prefilter/BRDF LUT. */
bool slayer3d_gl_load_environment_map(slayer3d_gl_context *ctx, const char *hdr_path);

/* Append a textured quad to the overlay draw list. The overlay is rendered
 * after the FBO blit, bypassing all post-processing. `positions` (3 floats
 * per vertex) and `uvs` (2 floats per vertex) are copied. `tex_pixels` is
 * also copied so the caller can free/reload the texture before present. */
bool slayer3d_gl_append_overlay(slayer3d_gl_context *ctx, const float *positions, const float *uvs, int vertex_count,
                                const float *mvp, const float *tint, const slayer3d_texture2d *texture,
                                bool scissor_enabled, const SDL_Rect *scissor_rect, slayer3d_overlay_effect effect,
                                float effect_progress, Uint32 effect_seed, const char *shader_vertex_source,
                                const char *shader_fragment_source);

bool slayer3d_gl_append_line(slayer3d_gl_context *ctx, const float *positions, const float *colors, const float *mvp);

/* Queue a transition to be applied during the next GL present after
 * post-processing and before the final window blit. */
bool slayer3d_gl_queue_transition(slayer3d_gl_context *ctx, const slayer3d_transition *transition);

#endif

/*
 * Internal software rasterizer. Not part of the public SDL3D API.
 *
 * Pipeline summary:
 *   1. Transform vertices into clip space (vec4) via a 4x4 MVP.
 *   2. Polygon-clip against all six clip-space planes before perspective
 *      divide (Sutherland-Hodgman). This prevents divide-by-zero and
 *      coordinate wraparound for primitives that straddle the near plane
 *      or the edges of the view frustum.
 *   3. Perspective divide: (x, y, z)/w produces NDC in [-1, 1]^3.
 *   4. Viewport transform to pixel space with subpixel precision.
 *   5. Fixed-point edge-function rasterization using the D3D-style
 *      top-left fill rule. Adjacent triangles sharing an edge produce
 *      no gaps and no overlapping pixels.
 *   6. Linear depth interpolation in NDC space. Depth test compares
 *      against the depth buffer; writes both color and depth on pass.
 *
 * Coordinate conventions follow include/sdl3d/math.h: right-handed,
 * column-major, OpenGL-style NDC z in [-1, 1].
 */

#ifndef SDL3D_INTERNAL_RASTERIZER_H
#define SDL3D_INTERNAL_RASTERIZER_H

#include <stdbool.h>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/math.h"
#include "sdl3d/texture.h"
#include "sdl3d/types.h"

typedef struct sdl3d_parallel_rasterizer sdl3d_parallel_rasterizer;
struct sdl3d_lighting_params;

typedef struct sdl3d_framebuffer
{
    Uint8 *color_pixels; /* RGBA8888, width * height * 4 bytes */
    float *depth_pixels; /* width * height floats in [-1, 1] */
    int width;
    int height;
    sdl3d_parallel_rasterizer *parallel_rasterizer;
    bool scissor_enabled;
    SDL_Rect scissor_rect;
} sdl3d_framebuffer;

bool sdl3d_parallel_rasterizer_create(int worker_count, sdl3d_parallel_rasterizer **out_rasterizer);
void sdl3d_parallel_rasterizer_destroy(sdl3d_parallel_rasterizer *rasterizer);
int sdl3d_parallel_rasterizer_get_worker_count(const sdl3d_parallel_rasterizer *rasterizer);

void sdl3d_framebuffer_clear(sdl3d_framebuffer *framebuffer, sdl3d_color color, float depth);
void sdl3d_framebuffer_clear_rect(sdl3d_framebuffer *framebuffer, const SDL_Rect *rect, sdl3d_color color, float depth);

bool sdl3d_framebuffer_get_pixel(const sdl3d_framebuffer *framebuffer, int x, int y, sdl3d_color *out_color);
bool sdl3d_framebuffer_get_depth(const sdl3d_framebuffer *framebuffer, int x, int y, float *out_depth);

void sdl3d_rasterize_triangle(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 v0, sdl3d_vec3 v1,
                              sdl3d_vec3 v2, sdl3d_color color, bool backface_culling_enabled, bool wireframe_enabled);

/*
 * Triangle with per-vertex RGBA colors. Attributes are interpolated with
 * perspective correction in the fill loop (attr/w linearly in screen space,
 * divided by the linearly interpolated 1/w per pixel). Wireframe edges use
 * linear (screen-space) color interpolation along each edge.
 */
void sdl3d_rasterize_triangle_colored(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 v0, sdl3d_vec3 v1,
                                      sdl3d_vec3 v2, sdl3d_color c0, sdl3d_color c1, sdl3d_color c2,
                                      bool backface_culling_enabled, bool wireframe_enabled);

void sdl3d_rasterize_triangle_textured(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 v0, sdl3d_vec3 v1,
                                       sdl3d_vec3 v2, sdl3d_vec2 uv0, sdl3d_vec2 uv1, sdl3d_vec2 uv2,
                                       sdl3d_vec4 modulate0, sdl3d_vec4 modulate1, sdl3d_vec4 modulate2,
                                       const sdl3d_texture2d *texture, bool backface_culling_enabled,
                                       bool wireframe_enabled);

/*
 * Lit textured triangle. Carries per-vertex world-space normals and
 * world-space positions through clipping for per-fragment PBR shading.
 * Falls back to the unlit textured path when lighting_params is NULL.
 */
void sdl3d_rasterize_triangle_lit(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 v0, sdl3d_vec3 v1,
                                  sdl3d_vec3 v2, sdl3d_vec2 uv0, sdl3d_vec2 uv1, sdl3d_vec2 uv2, sdl3d_vec3 n0,
                                  sdl3d_vec3 n1, sdl3d_vec3 n2, sdl3d_vec3 wp0, sdl3d_vec3 wp1, sdl3d_vec3 wp2,
                                  sdl3d_vec4 modulate0, sdl3d_vec4 modulate1, sdl3d_vec4 modulate2,
                                  const sdl3d_texture2d *texture, const struct sdl3d_lighting_params *lighting_params,
                                  bool backface_culling_enabled, bool wireframe_enabled);

void sdl3d_rasterize_line(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 start, sdl3d_vec3 end,
                          sdl3d_color color);

void sdl3d_rasterize_point(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 position, sdl3d_color color);

#endif

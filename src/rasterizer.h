/*
 * Internal software rasterizer. Not part of the public SLAYER3D API.
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
 * Coordinate conventions follow include/slayer3d/math.h: right-handed,
 * column-major, OpenGL-style NDC z in [-1, 1].
 */

#ifndef SLAYER3D_INTERNAL_RASTERIZER_H
#define SLAYER3D_INTERNAL_RASTERIZER_H

#include <stdbool.h>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/math.h"
#include "slayer3d/texture.h"
#include "slayer3d/types.h"

typedef struct slayer3d_parallel_rasterizer slayer3d_parallel_rasterizer;
struct slayer3d_lighting_params;

typedef struct slayer3d_framebuffer
{
    Uint8 *color_pixels; /* RGBA8888, width * height * 4 bytes */
    float *depth_pixels; /* width * height floats in [-1, 1] */
    int width;
    int height;
    slayer3d_parallel_rasterizer *parallel_rasterizer;
    bool viewport_enabled;
    SDL_Rect viewport_rect;
    bool scissor_enabled;
    SDL_Rect scissor_rect;
    bool blend_enabled; /* when true, alpha-composite instead of overwrite */
} slayer3d_framebuffer;

bool slayer3d_parallel_rasterizer_create(int worker_count, slayer3d_parallel_rasterizer **out_rasterizer);
void slayer3d_parallel_rasterizer_destroy(slayer3d_parallel_rasterizer *rasterizer);
int slayer3d_parallel_rasterizer_get_worker_count(const slayer3d_parallel_rasterizer *rasterizer);

void slayer3d_framebuffer_clear(slayer3d_framebuffer *framebuffer, slayer3d_color color, float depth);
void slayer3d_framebuffer_clear_rect(slayer3d_framebuffer *framebuffer, const SDL_Rect *rect, slayer3d_color color,
                                     float depth);

bool slayer3d_framebuffer_get_pixel(const slayer3d_framebuffer *framebuffer, int x, int y, slayer3d_color *out_color);
bool slayer3d_framebuffer_get_depth(const slayer3d_framebuffer *framebuffer, int x, int y, float *out_depth);

void slayer3d_rasterize_triangle(slayer3d_framebuffer *framebuffer, slayer3d_mat4 mvp, slayer3d_vec3 v0,
                                 slayer3d_vec3 v1, slayer3d_vec3 v2, slayer3d_color color,
                                 bool backface_culling_enabled, bool wireframe_enabled);

/*
 * Triangle with per-vertex RGBA colors. Attributes are interpolated with
 * perspective correction in the fill loop (attr/w linearly in screen space,
 * divided by the linearly interpolated 1/w per pixel). Wireframe edges use
 * linear (screen-space) color interpolation along each edge.
 */
void slayer3d_rasterize_triangle_colored(slayer3d_framebuffer *framebuffer, slayer3d_mat4 mvp, slayer3d_vec3 v0,
                                         slayer3d_vec3 v1, slayer3d_vec3 v2, slayer3d_color c0, slayer3d_color c1,
                                         slayer3d_color c2, bool backface_culling_enabled, bool wireframe_enabled);

void slayer3d_rasterize_triangle_textured(slayer3d_framebuffer *framebuffer, slayer3d_mat4 mvp, slayer3d_vec3 v0,
                                          slayer3d_vec3 v1, slayer3d_vec3 v2, slayer3d_vec2 uv0, slayer3d_vec2 uv1,
                                          slayer3d_vec2 uv2, slayer3d_vec4 modulate0, slayer3d_vec4 modulate1,
                                          slayer3d_vec4 modulate2, const slayer3d_texture2d *texture,
                                          bool backface_culling_enabled, bool wireframe_enabled);

/*
 * Textured triangle with optional retro/profile rules. When
 * `lighting_params` is non-NULL, UV interpolation, vertex snap, and
 * final color quantization follow the active render profile. Lighting
 * itself is expected to be pre-baked into the per-vertex modulate
 * colors by the caller.
 */
void slayer3d_rasterize_triangle_textured_profiled(slayer3d_framebuffer *framebuffer, slayer3d_mat4 mvp,
                                                   slayer3d_vec3 v0, slayer3d_vec3 v1, slayer3d_vec3 v2,
                                                   slayer3d_vec2 uv0, slayer3d_vec2 uv1, slayer3d_vec2 uv2,
                                                   slayer3d_vec4 modulate0, slayer3d_vec4 modulate1,
                                                   slayer3d_vec4 modulate2, const slayer3d_texture2d *texture,
                                                   const struct slayer3d_lighting_params *lighting_params,
                                                   bool backface_culling_enabled, bool wireframe_enabled);

/*
 * Lit textured triangle. Carries per-vertex world-space normals and
 * world-space positions through clipping for per-fragment PBR shading.
 * Falls back to the unlit textured path when lighting_params is NULL.
 */
void slayer3d_rasterize_triangle_lit(slayer3d_framebuffer *framebuffer, slayer3d_mat4 mvp, slayer3d_vec3 v0,
                                     slayer3d_vec3 v1, slayer3d_vec3 v2, slayer3d_vec2 uv0, slayer3d_vec2 uv1,
                                     slayer3d_vec2 uv2, slayer3d_vec3 n0, slayer3d_vec3 n1, slayer3d_vec3 n2,
                                     slayer3d_vec3 wp0, slayer3d_vec3 wp1, slayer3d_vec3 wp2, slayer3d_vec4 modulate0,
                                     slayer3d_vec4 modulate1, slayer3d_vec4 modulate2,
                                     const slayer3d_texture2d *texture,
                                     const struct slayer3d_lighting_params *lighting_params,
                                     bool backface_culling_enabled, bool wireframe_enabled);

void slayer3d_rasterize_line(slayer3d_framebuffer *framebuffer, slayer3d_mat4 mvp, slayer3d_vec3 start,
                             slayer3d_vec3 end, slayer3d_color color);

void slayer3d_rasterize_point(slayer3d_framebuffer *framebuffer, slayer3d_mat4 mvp, slayer3d_vec3 position,
                              slayer3d_color color);

#endif

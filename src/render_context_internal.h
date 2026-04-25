/*
 * Internal layout of sdl3d_render_context, shared between render_context.c
 * and the software pipeline (rasterizer.c / drawing3d.c). Not part of the
 * public API.
 */

#ifndef SDL3D_INTERNAL_RENDER_CONTEXT_H
#define SDL3D_INTERNAL_RENDER_CONTEXT_H

#include <stdbool.h>

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

#include "backend.h"
#include "rasterizer.h"
#include "sdl3d/lighting.h"
#include "sdl3d/math.h"
#include "sdl3d/render_context.h"

struct sdl3d_gl_context;

struct sdl3d_render_context
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *color_texture;
    struct sdl3d_texture_cache_entry *texture_cache;
    Uint8 *color_buffer;
    float *depth_buffer;
    sdl3d_parallel_rasterizer *parallel_rasterizer;
    sdl3d_backend backend;
    sdl3d_backend_interface backend_iface;
    int width;
    int height;

    float near_plane;
    float far_plane;

    bool in_mode_3d;
    sdl3d_mat4 *model_stack;
    int model_stack_depth;
    int model_stack_capacity;
    bool backface_culling_enabled;
    bool wireframe_enabled;
    bool scissor_enabled;
    SDL_Rect scissor_rect;
    bool color_buffer_blend; /* software overlay: enable alpha compositing */
    sdl3d_mat4 model;
    sdl3d_mat4 view;
    sdl3d_mat4 projection;
    sdl3d_mat4 view_projection;
    sdl3d_mat4 model_view_projection;

    sdl3d_shading_mode shading_mode;
    sdl3d_light lights[SDL3D_MAX_LIGHTS];
    int light_count;
    float ambient[3];
    float emissive[3];

    bool bloom_enabled;
    bool ssao_enabled;
    bool point_shadows_enabled;

    /* Z-fighting detection callback. */
    void (*zfight_callback)(const char *message, void *userdata);
    void *zfight_userdata;

    sdl3d_fog fog;
    sdl3d_tonemap_mode tonemap_mode;

    /* Per-light shadow maps (only directional lights supported). */
    float *shadow_depth[SDL3D_MAX_LIGHTS];
    sdl3d_mat4 shadow_vp[SDL3D_MAX_LIGHTS];
    bool shadow_enabled[SDL3D_MAX_LIGHTS];
    float shadow_bias;

    bool in_shadow_pass;

    /* Render profile flags. */
    sdl3d_uv_mode uv_mode;
    sdl3d_fog_eval fog_eval;
    sdl3d_texture_filter texture_filter;
    bool vertex_snap;
    int vertex_snap_precision;
    bool color_quantize;
    int color_depth;

    /* OpenGL backend (NULL when using software backend). */
    struct sdl3d_gl_context *gl;

    /* Cached frustum planes extracted from view_projection. Updated once
     * per sdl3d_begin_mode_3d so per-mesh visibility tests avoid redundant
     * extraction. Order: left, right, bottom, top, near, far. */
    float frustum_planes[6][4]; /* {a,b,c,d} per plane, normalized */
    bool frustum_planes_valid;
};

/* Recompute the cached frustum planes from the supplied view-projection.
 * Defined in drawing3d.c; shared so the shadow pass can repurpose the
 * actor/mesh culling against the light frustum. */
void sdl3d_internal_extract_frustum_planes(sdl3d_render_context *context, sdl3d_mat4 view_projection);

static inline sdl3d_framebuffer sdl3d_framebuffer_from_context(sdl3d_render_context *context)
{
    sdl3d_framebuffer framebuffer;
    framebuffer.color_pixels = context->color_buffer;
    framebuffer.depth_pixels = context->depth_buffer;
    framebuffer.width = context->width;
    framebuffer.height = context->height;
    framebuffer.parallel_rasterizer = context->parallel_rasterizer;
    framebuffer.scissor_enabled = context->scissor_enabled;
    framebuffer.scissor_rect = context->scissor_rect;
    framebuffer.blend_enabled = context->color_buffer_blend;
    return framebuffer;
}

#endif

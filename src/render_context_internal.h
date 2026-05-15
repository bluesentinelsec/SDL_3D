/*
 * Internal layout of slayer3d_render_context, shared between render_context.c
 * and the software pipeline (rasterizer.c / drawing3d.c). Not part of the
 * public API.
 */

#ifndef SLAYER3D_INTERNAL_RENDER_CONTEXT_H
#define SLAYER3D_INTERNAL_RENDER_CONTEXT_H

#include <stdbool.h>

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

#include "backend.h"
#include "rasterizer.h"
#include "slayer3d/lighting.h"
#include "slayer3d/math.h"
#include "slayer3d/render_context.h"

struct slayer3d_gl_context;

struct slayer3d_render_context
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *color_texture;
    struct slayer3d_texture_cache_entry *texture_cache;
    Uint8 *color_buffer;
    float *depth_buffer;
    slayer3d_parallel_rasterizer *parallel_rasterizer;
    slayer3d_backend backend;
    slayer3d_backend_interface backend_iface;
    int width;
    int height;
    slayer3d_render_stats stats;

    float near_plane;
    float far_plane;

    bool in_mode_3d;
    slayer3d_mat4 *model_stack;
    int model_stack_depth;
    int model_stack_capacity;
    bool backface_culling_enabled;
    bool wireframe_enabled;
    bool scissor_enabled;
    SDL_Rect scissor_rect;
    bool color_buffer_blend; /* software overlay: enable alpha compositing */
    slayer3d_mat4 model;
    slayer3d_mat4 view;
    slayer3d_mat4 projection;
    slayer3d_mat4 view_projection;
    slayer3d_mat4 model_view_projection;

    slayer3d_shading_mode shading_mode;
    slayer3d_light lights[SLAYER3D_MAX_LIGHTS];
    int light_count;
    float ambient[3];
    float emissive[3];

    bool bloom_enabled;
    bool ssao_enabled;
    bool point_shadows_enabled;
    bool depth_prepass_enabled;
    bool render_sample_queries_enabled;
    bool depth_prepass_scope_enabled;

    /* Z-fighting detection callback. */
    void (*zfight_callback)(const char *message, void *userdata);
    void *zfight_userdata;

    slayer3d_fog fog;
    slayer3d_tonemap_mode tonemap_mode;

    /* Per-light shadow maps (only directional lights supported). */
    float *shadow_depth[SLAYER3D_MAX_LIGHTS];
    slayer3d_mat4 shadow_vp[SLAYER3D_MAX_LIGHTS];
    bool shadow_enabled[SLAYER3D_MAX_LIGHTS];
    float shadow_bias;

    bool in_shadow_pass;

    /* Render profile flags. */
    slayer3d_uv_mode uv_mode;
    slayer3d_fog_eval fog_eval;
    slayer3d_texture_filter texture_filter;
    bool vertex_snap;
    int vertex_snap_precision;
    bool color_quantize;
    int color_depth;
    slayer3d_display_profile display_profile;
    int display_width;
    int display_height;
    slayer3d_display_filter display_filter;

    /* OpenGL backend (NULL when using software backend). */
    struct slayer3d_gl_context *gl;

    /* Cached frustum planes extracted from view_projection. Updated once
     * per slayer3d_begin_mode_3d so per-mesh visibility tests avoid redundant
     * extraction. Order: left, right, bottom, top, near, far. */
    float frustum_planes[6][4]; /* {a,b,c,d} per plane, normalized */
    bool frustum_planes_valid;
};

/* Recompute the cached frustum planes from the supplied view-projection.
 * Defined in drawing3d.c; shared so the shadow pass can repurpose the
 * actor/mesh culling against the light frustum. */
void slayer3d_internal_extract_frustum_planes(slayer3d_render_context *context, slayer3d_mat4 view_projection);

static inline slayer3d_framebuffer slayer3d_framebuffer_from_context(slayer3d_render_context *context)
{
    slayer3d_framebuffer framebuffer;
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

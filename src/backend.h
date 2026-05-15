/*
 * Internal backend interface for SLAYER3D renderers.
 *
 * Each backend (software, OpenGL, future SDL_GPU) provides an
 * slayer3d_backend_interface populated with function pointers.  The render
 * context dispatches through this table instead of hard-coding backend
 * checks, keeping the public API backend-agnostic.
 *
 * Not part of the public SLAYER3D API.
 */

#ifndef SLAYER3D_BACKEND_H
#define SLAYER3D_BACKEND_H

#include <stdbool.h>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_video.h>

#include "slayer3d/types.h"

/* Forward declarations. */
typedef struct slayer3d_render_context slayer3d_render_context;
typedef struct slayer3d_texture2d slayer3d_texture2d;
struct slayer3d_light;

/* ------------------------------------------------------------------ */
/* Draw parameter bundles                                              */
/* ------------------------------------------------------------------ */

typedef struct slayer3d_draw_params_unlit
{
    const float *positions;
    const float *uvs;
    const float *colors;
    const char *shader_vertex_source;
    const char *shader_fragment_source;
    const unsigned int *indices;
    int vertex_count;
    int index_count;
    const slayer3d_texture2d *texture;
    const float *mvp;
    float tint[4];
    int texture_filter; /* 0=nearest, 1=bilinear */
    bool static_geometry;
} slayer3d_draw_params_unlit;

typedef struct slayer3d_draw_params_lit
{
    const float *positions;
    const float *normals;
    const float *uvs;
    const float *lightmap_uvs;
    const float *colors;
    const char *shader_vertex_source;
    const char *shader_fragment_source;
    const unsigned int *indices;
    int vertex_count;
    int index_count;
    const slayer3d_texture2d *texture;
    const slayer3d_texture2d *lightmap;
    const float *mvp;
    const float *model_matrix;
    float normal_matrix[9];
    float tint[4];
    float camera_pos[3];
    float ambient[3];
    float metallic;
    float roughness;
    float emissive[3];
    const struct slayer3d_light *lights;
    int light_count;
    int tonemap_mode;
    int fog_mode;
    float fog_color[3];
    float fog_start;
    float fog_end;
    float fog_density;
    int shading_mode;
    int uv_mode; /* 0=perspective, 1=affine */
    bool baked_light_mode;
    bool vertex_snap;
    int vertex_snap_precision;
    int texture_filter;
    bool depth_prepass_eligible;
    const float *shadow_depth_data; /* 512*512 floats, or NULL */
    const float *shadow_vp;         /* 16 floats (mat4), or NULL */
    float shadow_bias;
    bool static_geometry;
} slayer3d_draw_params_lit;

/* ------------------------------------------------------------------ */
/* Backend interface                                                   */
/* ------------------------------------------------------------------ */

typedef struct slayer3d_backend_interface
{
    /* Lifecycle. */
    void (*destroy)(slayer3d_render_context *context);

    /* Per-frame operations. */
    bool (*clear)(slayer3d_render_context *context, slayer3d_color color);
    bool (*present)(slayer3d_render_context *context);

    /* Drawing. */
    bool (*draw_mesh_unlit)(slayer3d_render_context *context, const slayer3d_draw_params_unlit *params);
    bool (*draw_mesh_lit)(slayer3d_render_context *context, const slayer3d_draw_params_lit *params);
} slayer3d_backend_interface;

/* Backend initializers — each populates the interface table. */
void slayer3d_sw_backend_init(slayer3d_backend_interface *iface);
void slayer3d_gl_backend_init(slayer3d_backend_interface *iface);

#endif

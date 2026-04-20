/*
 * Internal backend interface for SDL3D renderers.
 *
 * Each backend (software, OpenGL, future SDL_GPU) provides an
 * sdl3d_backend_interface populated with function pointers.  The render
 * context dispatches through this table instead of hard-coding backend
 * checks, keeping the public API backend-agnostic.
 *
 * Not part of the public SDL3D API.
 */

#ifndef SDL3D_BACKEND_H
#define SDL3D_BACKEND_H

#include <stdbool.h>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_video.h>

#include "sdl3d/types.h"

/* Forward declarations. */
typedef struct sdl3d_render_context sdl3d_render_context;
typedef struct sdl3d_texture2d sdl3d_texture2d;
struct sdl3d_light;

/* ------------------------------------------------------------------ */
/* Draw parameter bundles                                              */
/* ------------------------------------------------------------------ */

typedef struct sdl3d_draw_params_unlit
{
    const float *positions;
    const float *uvs;
    const float *colors;
    const unsigned int *indices;
    int vertex_count;
    int index_count;
    const sdl3d_texture2d *texture;
    const float *mvp;
    float tint[4];
} sdl3d_draw_params_unlit;

typedef struct sdl3d_draw_params_lit
{
    const float *positions;
    const float *normals;
    const float *uvs;
    const float *colors;
    const unsigned int *indices;
    int vertex_count;
    int index_count;
    const sdl3d_texture2d *texture;
    const float *mvp;
    const float *model_matrix;
    float normal_matrix[9];
    float tint[4];
    float camera_pos[3];
    float ambient[3];
    float metallic;
    float roughness;
    float emissive[3];
    const struct sdl3d_light *lights;
    int light_count;
    int tonemap_mode;
    int fog_mode;
    float fog_color[3];
    float fog_start;
    float fog_end;
    float fog_density;
} sdl3d_draw_params_lit;

/* ------------------------------------------------------------------ */
/* Backend interface                                                   */
/* ------------------------------------------------------------------ */

typedef struct sdl3d_backend_interface
{
    /* Lifecycle. */
    void (*destroy)(sdl3d_render_context *context);

    /* Per-frame operations. */
    bool (*clear)(sdl3d_render_context *context, sdl3d_color color);
    bool (*present)(sdl3d_render_context *context);

    /* Drawing. */
    bool (*draw_mesh_unlit)(sdl3d_render_context *context, const sdl3d_draw_params_unlit *params);
    bool (*draw_mesh_lit)(sdl3d_render_context *context, const sdl3d_draw_params_lit *params);
} sdl3d_backend_interface;

/* Backend initializers — each populates the interface table. */
void sdl3d_sw_backend_init(sdl3d_backend_interface *iface);
void sdl3d_gl_backend_init(sdl3d_backend_interface *iface);

#endif

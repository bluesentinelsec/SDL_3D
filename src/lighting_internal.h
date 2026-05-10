/*
 * Internal lighting types and per-fragment PBR shading. Not part of
 * the public SLAYER3D API.
 */

#ifndef SLAYER3D_INTERNAL_LIGHTING_H
#define SLAYER3D_INTERNAL_LIGHTING_H

#include "slayer3d/lighting.h"
#include "slayer3d/math.h"

/*
 * Packed lighting parameters passed from drawing3d.c into the
 * rasterizer so the fill loop can evaluate PBR shading per fragment.
 */
typedef struct slayer3d_lighting_params
{
    const slayer3d_light *lights;
    int light_count;
    float ambient[3];
    slayer3d_vec3 camera_pos;
    float metallic;
    float roughness;
    float emissive[3];

    /* Shadow maps (per-light, NULL when no shadow for that light). */
    const float *shadow_depth[SLAYER3D_MAX_LIGHTS];
    slayer3d_mat4 shadow_vp[SLAYER3D_MAX_LIGHTS];
    bool shadow_enabled[SLAYER3D_MAX_LIGHTS];
    float shadow_bias;

    /* Fog. */
    slayer3d_fog fog;

    /* Tonemapping. */
    slayer3d_tonemap_mode tonemap_mode;

    /* Render profile flags. */
    slayer3d_uv_mode uv_mode;
    slayer3d_fog_eval fog_eval;
    bool vertex_snap;
    int vertex_snap_precision;
    bool color_quantize;
    int color_depth;
    bool baked_light_mode;
} slayer3d_lighting_params;

/*
 * Evaluate PBR shading for a single fragment.
 *
 * `albedo` is the base color (texture × material factor) in linear RGB.
 * `world_normal` must be normalized.
 * `world_pos` is the fragment's world-space position.
 *
 * Writes the final lit linear RGB into out_r/g/b. Alpha is not affected
 * by lighting.
 */
void slayer3d_shade_fragment_pbr(const slayer3d_lighting_params *params, float albedo_r, float albedo_g, float albedo_b,
                                 float world_nx, float world_ny, float world_nz, float world_px, float world_py,
                                 float world_pz, float *out_r, float *out_g, float *out_b);

/*
 * Apply tonemapping to HDR linear RGB. Modifies values in-place.
 */
void slayer3d_tonemap(slayer3d_tonemap_mode mode, float *r, float *g, float *b);

/*
 * Compute fog factor in [0,1] where 0 = no fog, 1 = fully fogged.
 */
float slayer3d_compute_fog_factor(const slayer3d_fog *fog, float distance);

#endif

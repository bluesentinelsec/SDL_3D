/*
 * Internal lighting types and per-fragment PBR shading. Not part of
 * the public SDL3D API.
 */

#ifndef SDL3D_INTERNAL_LIGHTING_H
#define SDL3D_INTERNAL_LIGHTING_H

#include "sdl3d/lighting.h"
#include "sdl3d/math.h"

/*
 * Packed lighting parameters passed from drawing3d.c into the
 * rasterizer so the fill loop can evaluate PBR shading per fragment.
 */
typedef struct sdl3d_lighting_params
{
    const sdl3d_light *lights;
    int light_count;
    float ambient[3];
    sdl3d_vec3 camera_pos;
    float metallic;
    float roughness;
    float emissive[3];
} sdl3d_lighting_params;

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
void sdl3d_shade_fragment_pbr(const sdl3d_lighting_params *params, float albedo_r, float albedo_g, float albedo_b,
                              float world_nx, float world_ny, float world_nz, float world_px, float world_py,
                              float world_pz, float *out_r, float *out_g, float *out_b);

#endif

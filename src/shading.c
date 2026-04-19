/*
 * Per-fragment PBR shading: simplified Cook-Torrance BRDF.
 *
 * - GGX (Trowbridge-Reitz) normal distribution
 * - Schlick-GGX geometry (Smith method, k = (roughness+1)^2 / 8)
 * - Schlick Fresnel approximation
 * - Lambertian diffuse
 *
 * Supports directional, point, and spot lights.
 */

#include "lighting_internal.h"

#include <math.h>

static const float SDL3D_PI = 3.14159265358979323846f;

static float sdl3d_maxf(float a, float b)
{
    return a > b ? a : b;
}

static float sdl3d_clampf(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

/* GGX / Trowbridge-Reitz normal distribution function. */
static float sdl3d_distribution_ggx(float n_dot_h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = n_dot_h * n_dot_h * (a2 - 1.0f) + 1.0f;
    return a2 / (SDL3D_PI * denom * denom + 1e-7f);
}

/* Schlick-GGX geometry function (one direction). */
static float sdl3d_geometry_schlick_ggx(float n_dot_v, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return n_dot_v / (n_dot_v * (1.0f - k) + k + 1e-7f);
}

/* Smith geometry: product of two Schlick-GGX terms. */
static float sdl3d_geometry_smith(float n_dot_v, float n_dot_l, float roughness)
{
    return sdl3d_geometry_schlick_ggx(n_dot_v, roughness) * sdl3d_geometry_schlick_ggx(n_dot_l, roughness);
}

/* Schlick Fresnel approximation. */
static void sdl3d_fresnel_schlick(float cos_theta, float f0_r, float f0_g, float f0_b, float *out_r, float *out_g,
                                  float *out_b)
{
    float t = 1.0f - cos_theta;
    float t2 = t * t;
    float t5 = t2 * t2 * t;
    *out_r = f0_r + (1.0f - f0_r) * t5;
    *out_g = f0_g + (1.0f - f0_g) * t5;
    *out_b = f0_b + (1.0f - f0_b) * t5;
}

static float sdl3d_vec3_dot_raw(float ax, float ay, float az, float bx, float by, float bz)
{
    return ax * bx + ay * by + az * bz;
}

static float sdl3d_vec3_length_raw(float x, float y, float z)
{
    return sqrtf(x * x + y * y + z * z);
}

/*
 * Compute the light's radiance contribution and the light direction
 * (pointing FROM the fragment TOWARD the light).
 */
static bool sdl3d_compute_light_contribution(const sdl3d_light *light, float px, float py, float pz, float *out_lx,
                                             float *out_ly, float *out_lz, float *out_rad_r, float *out_rad_g,
                                             float *out_rad_b)
{
    float attenuation = 1.0f;

    if (light->type == SDL3D_LIGHT_DIRECTIONAL)
    {
        float len = sdl3d_vec3_length_raw(light->direction.x, light->direction.y, light->direction.z);
        if (len < 1e-7f)
        {
            return false;
        }
        float inv = 1.0f / len;
        *out_lx = -light->direction.x * inv;
        *out_ly = -light->direction.y * inv;
        *out_lz = -light->direction.z * inv;
    }
    else
    {
        /* Point or spot: direction from fragment to light. */
        float dx = light->position.x - px;
        float dy = light->position.y - py;
        float dz = light->position.z - pz;
        float dist = sdl3d_vec3_length_raw(dx, dy, dz);
        if (dist < 1e-7f)
        {
            return false;
        }
        float inv = 1.0f / dist;
        *out_lx = dx * inv;
        *out_ly = dy * inv;
        *out_lz = dz * inv;

        /* Distance attenuation. */
        if (light->range > 0.0f)
        {
            float ratio = dist / light->range;
            attenuation = sdl3d_maxf(1.0f - ratio * ratio, 0.0f);
            attenuation *= attenuation;
        }
        else
        {
            attenuation = 1.0f / (dist * dist + 1e-4f);
        }

        /* Spot cone falloff. */
        if (light->type == SDL3D_LIGHT_SPOT)
        {
            float spot_dir_len = sdl3d_vec3_length_raw(light->direction.x, light->direction.y, light->direction.z);
            if (spot_dir_len < 1e-7f)
            {
                return false;
            }
            float inv_sd = 1.0f / spot_dir_len;
            float sdx = light->direction.x * inv_sd;
            float sdy = light->direction.y * inv_sd;
            float sdz = light->direction.z * inv_sd;
            float cos_angle = sdl3d_vec3_dot_raw(-(*out_lx), -(*out_ly), -(*out_lz), sdx, sdy, sdz);
            float epsilon = light->inner_cutoff - light->outer_cutoff;
            if (fabsf(epsilon) < 1e-7f)
            {
                attenuation *= (cos_angle >= light->outer_cutoff) ? 1.0f : 0.0f;
            }
            else
            {
                float spot_intensity = sdl3d_clampf((cos_angle - light->outer_cutoff) / epsilon, 0.0f, 1.0f);
                attenuation *= spot_intensity;
            }
        }
    }

    float scale = light->intensity * attenuation;
    *out_rad_r = light->color[0] * scale;
    *out_rad_g = light->color[1] * scale;
    *out_rad_b = light->color[2] * scale;
    return true;
}

static float sdl3d_sample_shadow(const sdl3d_lighting_params *params, int light_index, float wpx, float wpy, float wpz)
{
    sdl3d_vec4 lp;
    float ndc_x, ndc_y, ndc_z;
    int sx, sy, idx;
    float map_depth;

    if (!params->shadow_enabled[light_index] || params->shadow_depth[light_index] == NULL)
    {
        return 1.0f;
    }

    /* Transform world position to light clip space. */
    lp = sdl3d_mat4_transform_vec4(params->shadow_vp[light_index], sdl3d_vec4_make(wpx, wpy, wpz, 1.0f));
    if (lp.w <= 0.0f)
    {
        return 1.0f;
    }

    ndc_x = lp.x / lp.w;
    ndc_y = lp.y / lp.w;
    ndc_z = lp.z / lp.w;

    /* Map NDC [-1,1] to [0, shadow_map_size). */
    sx = (int)((ndc_x * 0.5f + 0.5f) * (float)SDL3D_SHADOW_MAP_SIZE);
    sy = (int)((ndc_y * 0.5f + 0.5f) * (float)SDL3D_SHADOW_MAP_SIZE);

    if (sx < 0 || sx >= SDL3D_SHADOW_MAP_SIZE || sy < 0 || sy >= SDL3D_SHADOW_MAP_SIZE)
    {
        return 1.0f; /* Outside shadow map → lit. */
    }

    idx = sy * SDL3D_SHADOW_MAP_SIZE + sx;
    map_depth = params->shadow_depth[light_index][idx];

    /* Fragment depth in [0,1] range. */
    {
        float frag_depth = ndc_z * 0.5f + 0.5f;
        return (frag_depth - params->shadow_bias > map_depth) ? 0.0f : 1.0f;
    }
}

void sdl3d_shade_fragment_pbr(const sdl3d_lighting_params *params, float albedo_r, float albedo_g, float albedo_b,
                              float world_nx, float world_ny, float world_nz, float world_px, float world_py,
                              float world_pz, float *out_r, float *out_g, float *out_b)
{
    /* F0: base reflectance. Dielectrics ~0.04, metals use albedo. */
    float f0_r = 0.04f + (albedo_r - 0.04f) * params->metallic;
    float f0_g = 0.04f + (albedo_g - 0.04f) * params->metallic;
    float f0_b = 0.04f + (albedo_b - 0.04f) * params->metallic;

    /* View direction: from fragment toward camera. */
    float vx = params->camera_pos.x - world_px;
    float vy = params->camera_pos.y - world_py;
    float vz = params->camera_pos.z - world_pz;
    float v_len = sdl3d_vec3_length_raw(vx, vy, vz);
    if (v_len > 1e-7f)
    {
        float inv = 1.0f / v_len;
        vx *= inv;
        vy *= inv;
        vz *= inv;
    }

    float n_dot_v = sdl3d_maxf(sdl3d_vec3_dot_raw(world_nx, world_ny, world_nz, vx, vy, vz), 0.0f);

    /* Accumulate light contributions. */
    float lo_r = 0.0f, lo_g = 0.0f, lo_b = 0.0f;

    for (int i = 0; i < params->light_count; ++i)
    {
        float lx, ly, lz;
        float rad_r, rad_g, rad_b;

        if (!sdl3d_compute_light_contribution(&params->lights[i], world_px, world_py, world_pz, &lx, &ly, &lz, &rad_r,
                                              &rad_g, &rad_b))
        {
            continue;
        }

        float n_dot_l = sdl3d_maxf(sdl3d_vec3_dot_raw(world_nx, world_ny, world_nz, lx, ly, lz), 0.0f);
        if (n_dot_l <= 0.0f)
        {
            continue;
        }

        /* Half vector. */
        float hx = lx + vx;
        float hy = ly + vy;
        float hz = lz + vz;
        float h_len = sdl3d_vec3_length_raw(hx, hy, hz);
        if (h_len > 1e-7f)
        {
            float inv = 1.0f / h_len;
            hx *= inv;
            hy *= inv;
            hz *= inv;
        }

        float n_dot_h = sdl3d_maxf(sdl3d_vec3_dot_raw(world_nx, world_ny, world_nz, hx, hy, hz), 0.0f);
        float h_dot_v = sdl3d_maxf(sdl3d_vec3_dot_raw(hx, hy, hz, vx, vy, vz), 0.0f);

        /* Cook-Torrance specular BRDF. */
        float ndf = sdl3d_distribution_ggx(n_dot_h, params->roughness);
        float geo = sdl3d_geometry_smith(n_dot_v, n_dot_l, params->roughness);
        float fr, fg, fb;
        sdl3d_fresnel_schlick(h_dot_v, f0_r, f0_g, f0_b, &fr, &fg, &fb);

        float denom = 4.0f * n_dot_v * n_dot_l + 1e-4f;
        float spec_scale = ndf * geo / denom;
        float spec_r = fr * spec_scale;
        float spec_g = fg * spec_scale;
        float spec_b = fb * spec_scale;

        /* Energy-conserving diffuse: kD = (1 - F) * (1 - metallic). */
        float kd = (1.0f - params->metallic);
        float diff_r = (1.0f - fr) * kd * albedo_r / SDL3D_PI;
        float diff_g = (1.0f - fg) * kd * albedo_g / SDL3D_PI;
        float diff_b = (1.0f - fb) * kd * albedo_b / SDL3D_PI;

        {
            float shadow = sdl3d_sample_shadow(params, i, world_px, world_py, world_pz);
            lo_r += (diff_r + spec_r) * rad_r * n_dot_l * shadow;
            lo_g += (diff_g + spec_g) * rad_g * n_dot_l * shadow;
            lo_b += (diff_b + spec_b) * rad_b * n_dot_l * shadow;
        }
    }

    /* Ambient + emissive. */
    *out_r = params->ambient[0] * albedo_r + lo_r + params->emissive[0];
    *out_g = params->ambient[1] * albedo_g + lo_g + params->emissive[1];
    *out_b = params->ambient[2] * albedo_b + lo_b + params->emissive[2];
}

/* ------------------------------------------------------------------ */
/* Tonemapping                                                         */
/* ------------------------------------------------------------------ */

static float sdl3d_tonemap_reinhard(float x)
{
    return x / (1.0f + x);
}

void sdl3d_tonemap(sdl3d_tonemap_mode mode, float *r, float *g, float *b)
{
    if (mode == SDL3D_TONEMAP_REINHARD)
    {
        *r = sdl3d_tonemap_reinhard(*r);
        *g = sdl3d_tonemap_reinhard(*g);
        *b = sdl3d_tonemap_reinhard(*b);
    }
    else if (mode == SDL3D_TONEMAP_ACES)
    {
        /* ACES filmic approximation (Narkowicz 2015). */
        float a = 2.51f, bt = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
        float ri = *r, gi = *g, bi = *b;
        *r = sdl3d_clampf((ri * (a * ri + bt)) / (ri * (c * ri + d) + e), 0.0f, 1.0f);
        *g = sdl3d_clampf((gi * (a * gi + bt)) / (gi * (c * gi + d) + e), 0.0f, 1.0f);
        *b = sdl3d_clampf((bi * (a * bi + bt)) / (bi * (c * bi + d) + e), 0.0f, 1.0f);
    }

    /* Apply sRGB gamma curve when any tonemapping is active. */
    if (mode != SDL3D_TONEMAP_NONE)
    {
        float inv_gamma = 1.0f / 2.2f;
        *r = powf(sdl3d_clampf(*r, 0.0f, 1.0f), inv_gamma);
        *g = powf(sdl3d_clampf(*g, 0.0f, 1.0f), inv_gamma);
        *b = powf(sdl3d_clampf(*b, 0.0f, 1.0f), inv_gamma);
    }
}

/* ------------------------------------------------------------------ */
/* Fog                                                                 */
/* ------------------------------------------------------------------ */

float sdl3d_compute_fog_factor(const sdl3d_fog *fog, float distance)
{
    if (fog->mode == SDL3D_FOG_NONE)
    {
        return 0.0f;
    }

    if (fog->mode == SDL3D_FOG_LINEAR)
    {
        if (fog->end <= fog->start)
        {
            return 0.0f;
        }
        return sdl3d_clampf((distance - fog->start) / (fog->end - fog->start), 0.0f, 1.0f);
    }

    if (fog->mode == SDL3D_FOG_EXP)
    {
        float f = expf(-fog->density * distance);
        return sdl3d_clampf(1.0f - f, 0.0f, 1.0f);
    }

    /* SDL3D_FOG_EXP2 */
    {
        float d = fog->density * distance;
        float f = expf(-d * d);
        return sdl3d_clampf(1.0f - f, 0.0f, 1.0f);
    }
}

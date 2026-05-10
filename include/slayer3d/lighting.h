#ifndef SLAYER3D_LIGHTING_H
#define SLAYER3D_LIGHTING_H

#include <stdbool.h>

#include "slayer3d/math.h"
#include "slayer3d/render_context.h"
#include "slayer3d/texture.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    struct slayer3d_mesh;

#define SLAYER3D_MAX_LIGHTS 8

    /* ============================================================== */
    /* Shading modes                                                  */
    /* ============================================================== */

    typedef enum slayer3d_shading_mode
    {
        SLAYER3D_SHADING_UNLIT = 0,   /* texture × tint, no lighting */
        SLAYER3D_SHADING_FLAT = 1,    /* one PBR eval per triangle (face normal) */
        SLAYER3D_SHADING_GOURAUD = 2, /* PBR eval at vertices, interpolate color */
        SLAYER3D_SHADING_PHONG = 3    /* interpolate normals, PBR per pixel */
    } slayer3d_shading_mode;

    bool slayer3d_set_shading_mode(slayer3d_render_context *context, slayer3d_shading_mode mode);
    slayer3d_shading_mode slayer3d_get_shading_mode(const slayer3d_render_context *context);

    typedef enum slayer3d_light_type
    {
        SLAYER3D_LIGHT_DIRECTIONAL = 0,
        SLAYER3D_LIGHT_POINT = 1,
        SLAYER3D_LIGHT_SPOT = 2
    } slayer3d_light_type;

    /*
     * Unified light descriptor. Fields used depend on `type`:
     *
     * DIRECTIONAL: direction, color, intensity.
     * POINT:       position, color, intensity, range. Range is a soft falloff
     *              scale, not a hard cutoff.
     * SPOT:        position, direction, color, intensity, range,
     *              inner_cutoff, outer_cutoff (cosines of half-angles). The
     *              cone edge is smoothly blended between the cutoffs.
     */
    typedef struct slayer3d_light
    {
        slayer3d_light_type type;
        slayer3d_vec3 position;
        slayer3d_vec3 direction;
        float color[3];
        float intensity;
        float range;
        float inner_cutoff;
        float outer_cutoff;
    } slayer3d_light;

    /*
     * Add a light to the scene. Up to SLAYER3D_MAX_LIGHTS may be active.
     * Returns false if the light list is full or context is NULL.
     */
    bool slayer3d_add_light(slayer3d_render_context *context, const slayer3d_light *light);

    /*
     * Remove all lights from the scene.
     */
    bool slayer3d_clear_lights(slayer3d_render_context *context);

    /*
     * Convenience wrapper: enabled=true sets PHONG, enabled=false sets UNLIT.
     * Prefer slayer3d_set_shading_mode for finer control.
     */
    bool slayer3d_set_lighting_enabled(slayer3d_render_context *context, bool enabled);

    bool slayer3d_is_lighting_enabled(const slayer3d_render_context *context);

    /*
     * Set the scene ambient light color (linear RGB, default 0.03 each).
     */
    bool slayer3d_set_ambient_light(slayer3d_render_context *context, float r, float g, float b);

    /*
     * Set the per-object emissive color (linear RGB). Emissive light is
     * added on top of PBR shading and is not attenuated by distance.
     * Call with (0,0,0) to reset.
     */
    bool slayer3d_set_emissive(slayer3d_render_context *context, float r, float g, float b);

    /*
     * Enable or disable the bloom post-process pass (default: true).
     */
    bool slayer3d_set_bloom_enabled(slayer3d_render_context *context, bool enabled);

    /*
     * Enable or disable the SSAO post-process pass (default: true).
     */
    bool slayer3d_set_ssao_enabled(slayer3d_render_context *context, bool enabled);
    bool slayer3d_set_point_shadows_enabled(slayer3d_render_context *context, bool enabled);

    /*
     * Z-fighting detection callback. Called when two draw calls have
     * overlapping coplanar geometry. The message contains the AABB
     * coordinates of both conflicting surfaces.
     *
     * If no callback is set, z-fighting is silently ignored.
     * Set to NULL to disable detection.
     */
    typedef void (*slayer3d_zfight_callback)(const char *message, void *userdata);
    bool slayer3d_set_zfight_callback(slayer3d_render_context *context, slayer3d_zfight_callback callback,
                                      void *userdata);

    /*
     * Load an HDRI environment map for Image-Based Lighting (IBL).
     * Replaces the hemisphere ambient with physically correct diffuse
     * irradiance and specular reflections. Accepts .hdr files.
     */
    bool slayer3d_load_environment_map(slayer3d_render_context *context, const char *hdr_path);

    int slayer3d_get_light_count(const slayer3d_render_context *context);

    /* ============================================================== */
    /* Fog                                                            */
    /* ============================================================== */

    typedef enum slayer3d_fog_mode
    {
        SLAYER3D_FOG_NONE = 0,
        SLAYER3D_FOG_LINEAR = 1,
        SLAYER3D_FOG_EXP = 2,
        SLAYER3D_FOG_EXP2 = 3
    } slayer3d_fog_mode;

    typedef struct slayer3d_fog
    {
        slayer3d_fog_mode mode;
        float color[3];
        float start;   /* LINEAR: distance where fog begins */
        float end;     /* LINEAR: distance where fog is fully opaque */
        float density; /* EXP / EXP2: fog density coefficient */
    } slayer3d_fog;

    bool slayer3d_set_fog(slayer3d_render_context *context, const slayer3d_fog *fog);
    bool slayer3d_clear_fog(slayer3d_render_context *context);

    /* ============================================================== */
    /* Tonemapping                                                    */
    /* ============================================================== */

    typedef enum slayer3d_tonemap_mode
    {
        SLAYER3D_TONEMAP_NONE = 0,
        SLAYER3D_TONEMAP_REINHARD = 1,
        SLAYER3D_TONEMAP_ACES = 2
    } slayer3d_tonemap_mode;

    bool slayer3d_set_tonemap_mode(slayer3d_render_context *context, slayer3d_tonemap_mode mode);
    slayer3d_tonemap_mode slayer3d_get_tonemap_mode(const slayer3d_render_context *context);

    /* ============================================================== */
    /* UV mapping mode                                                */
    /* ============================================================== */

    typedef enum slayer3d_uv_mode
    {
        SLAYER3D_UV_PERSPECTIVE = 0,
        SLAYER3D_UV_AFFINE = 1
    } slayer3d_uv_mode;

    /* ============================================================== */
    /* Fog evaluation mode                                            */
    /* ============================================================== */

    typedef enum slayer3d_fog_eval
    {
        SLAYER3D_FOG_EVAL_FRAGMENT = 0,
        SLAYER3D_FOG_EVAL_VERTEX = 1
    } slayer3d_fog_eval;

    /**
     * @brief Full-screen display treatment selected by a render profile.
     *
     * Backends that support post-processing use this to apply named display
     * effects independently from lower-level shading, UV, and texture options.
     */
    typedef enum slayer3d_display_profile
    {
        SLAYER3D_DISPLAY_PROFILE_MODERN = 0,
        SLAYER3D_DISPLAY_PROFILE_PS1 = 1,
        SLAYER3D_DISPLAY_PROFILE_N64 = 2,
        SLAYER3D_DISPLAY_PROFILE_DOS = 3,
        SLAYER3D_DISPLAY_PROFILE_SNES = 4,
        SLAYER3D_DISPLAY_PROFILE_GRAYSCALE = 5,
        SLAYER3D_DISPLAY_PROFILE_GAMEBOY = 6
    } slayer3d_display_profile;

    /**
     * @brief Upscale filter for a render profile's virtual presentation resolution.
     */
    typedef enum slayer3d_display_filter
    {
        SLAYER3D_DISPLAY_FILTER_LINEAR = 0,
        SLAYER3D_DISPLAY_FILTER_NEAREST = 1
    } slayer3d_display_filter;

    /* ============================================================== */
    /* Render profile                                                 */
    /* ============================================================== */

    typedef struct slayer3d_render_profile
    {
        slayer3d_shading_mode shading;
        slayer3d_texture_filter texture_filter;
        slayer3d_uv_mode uv_mode;
        slayer3d_fog_eval fog_eval;
        slayer3d_tonemap_mode tonemap;
        bool vertex_snap;
        int vertex_snap_precision;
        bool color_quantize;
        int color_depth;
        /** Named display treatment for backends with full-screen profile effects. */
        slayer3d_display_profile display_profile;
        /** Virtual presentation width in pixels, or 0 for native render resolution. */
        int display_width;
        /** Virtual presentation height in pixels, or 0 for native render resolution. */
        int display_height;
        /** Upscale filter used when presenting a virtual resolution. */
        slayer3d_display_filter display_filter;
    } slayer3d_render_profile;

    bool slayer3d_set_render_profile(slayer3d_render_context *context, const slayer3d_render_profile *profile);
    bool slayer3d_get_render_profile(const slayer3d_render_context *context, slayer3d_render_profile *out);

    slayer3d_render_profile slayer3d_profile_modern(void);
    slayer3d_render_profile slayer3d_profile_ps1(void);
    slayer3d_render_profile slayer3d_profile_n64(void);
    slayer3d_render_profile slayer3d_profile_dos(void);
    slayer3d_render_profile slayer3d_profile_snes(void);
    slayer3d_render_profile slayer3d_profile_grayscale(void);
    slayer3d_render_profile slayer3d_profile_gameboy(void);

    /* ============================================================== */
    /* Shadow mapping                                                 */
    /* ============================================================== */

#define SLAYER3D_SHADOW_MAP_SIZE 512

    /*
     * Enable shadow casting for a directional light at the given index.
     * Allocates a depth-only shadow map and computes the light-space
     * orthographic projection covering the specified scene bounds.
     *
     * `light_index` must refer to a SLAYER3D_LIGHT_DIRECTIONAL light.
     * `scene_radius` defines the half-extent of the ortho projection.
     * `scene_center` is the world-space center the shadow map covers.
     */
    bool slayer3d_enable_shadow(slayer3d_render_context *context, int light_index, slayer3d_vec3 scene_center,
                                float scene_radius);

    bool slayer3d_disable_shadow(slayer3d_render_context *context, int light_index);

    /*
     * Render shadow maps for all shadow-enabled lights. Call after
     * adding lights and before drawing lit geometry. Renders the
     * provided meshes into each shadow map's depth buffer from the
     * light's point of view.
     */
    bool slayer3d_render_shadow_map(slayer3d_render_context *context, const struct slayer3d_mesh *meshes,
                                    int mesh_count, const slayer3d_mat4 *model_matrices);

    /*
     * Begin/end a GPU shadow pass. The application calls begin, draws
     * shadow-casting geometry with the normal draw calls, then calls end.
     * The shadow pass must run to completion before the geometry pass
     * (lesson #5). Requires a shadow-enabled directional light.
     */
    bool slayer3d_begin_shadow_pass(slayer3d_render_context *context);
    bool slayer3d_end_shadow_pass(slayer3d_render_context *context);

#ifdef __cplusplus
}
#endif

#endif

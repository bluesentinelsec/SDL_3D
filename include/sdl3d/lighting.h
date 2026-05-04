#ifndef SDL3D_LIGHTING_H
#define SDL3D_LIGHTING_H

#include <stdbool.h>

#include "sdl3d/math.h"
#include "sdl3d/render_context.h"
#include "sdl3d/texture.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    struct sdl3d_mesh;

#define SDL3D_MAX_LIGHTS 8

    /* ============================================================== */
    /* Shading modes                                                  */
    /* ============================================================== */

    typedef enum sdl3d_shading_mode
    {
        SDL3D_SHADING_UNLIT = 0,   /* texture × tint, no lighting */
        SDL3D_SHADING_FLAT = 1,    /* one PBR eval per triangle (face normal) */
        SDL3D_SHADING_GOURAUD = 2, /* PBR eval at vertices, interpolate color */
        SDL3D_SHADING_PHONG = 3    /* interpolate normals, PBR per pixel */
    } sdl3d_shading_mode;

    bool sdl3d_set_shading_mode(sdl3d_render_context *context, sdl3d_shading_mode mode);
    sdl3d_shading_mode sdl3d_get_shading_mode(const sdl3d_render_context *context);

    typedef enum sdl3d_light_type
    {
        SDL3D_LIGHT_DIRECTIONAL = 0,
        SDL3D_LIGHT_POINT = 1,
        SDL3D_LIGHT_SPOT = 2
    } sdl3d_light_type;

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
    typedef struct sdl3d_light
    {
        sdl3d_light_type type;
        sdl3d_vec3 position;
        sdl3d_vec3 direction;
        float color[3];
        float intensity;
        float range;
        float inner_cutoff;
        float outer_cutoff;
    } sdl3d_light;

    /*
     * Add a light to the scene. Up to SDL3D_MAX_LIGHTS may be active.
     * Returns false if the light list is full or context is NULL.
     */
    bool sdl3d_add_light(sdl3d_render_context *context, const sdl3d_light *light);

    /*
     * Remove all lights from the scene.
     */
    bool sdl3d_clear_lights(sdl3d_render_context *context);

    /*
     * Convenience wrapper: enabled=true sets PHONG, enabled=false sets UNLIT.
     * Prefer sdl3d_set_shading_mode for finer control.
     */
    bool sdl3d_set_lighting_enabled(sdl3d_render_context *context, bool enabled);

    bool sdl3d_is_lighting_enabled(const sdl3d_render_context *context);

    /*
     * Set the scene ambient light color (linear RGB, default 0.03 each).
     */
    bool sdl3d_set_ambient_light(sdl3d_render_context *context, float r, float g, float b);

    /*
     * Set the per-object emissive color (linear RGB). Emissive light is
     * added on top of PBR shading and is not attenuated by distance.
     * Call with (0,0,0) to reset.
     */
    bool sdl3d_set_emissive(sdl3d_render_context *context, float r, float g, float b);

    /*
     * Enable or disable the bloom post-process pass (default: true).
     */
    bool sdl3d_set_bloom_enabled(sdl3d_render_context *context, bool enabled);

    /*
     * Enable or disable the SSAO post-process pass (default: true).
     */
    bool sdl3d_set_ssao_enabled(sdl3d_render_context *context, bool enabled);
    bool sdl3d_set_point_shadows_enabled(sdl3d_render_context *context, bool enabled);

    /*
     * Z-fighting detection callback. Called when two draw calls have
     * overlapping coplanar geometry. The message contains the AABB
     * coordinates of both conflicting surfaces.
     *
     * If no callback is set, z-fighting is silently ignored.
     * Set to NULL to disable detection.
     */
    typedef void (*sdl3d_zfight_callback)(const char *message, void *userdata);
    bool sdl3d_set_zfight_callback(sdl3d_render_context *context, sdl3d_zfight_callback callback, void *userdata);

    /*
     * Load an HDRI environment map for Image-Based Lighting (IBL).
     * Replaces the hemisphere ambient with physically correct diffuse
     * irradiance and specular reflections. Accepts .hdr files.
     */
    bool sdl3d_load_environment_map(sdl3d_render_context *context, const char *hdr_path);

    int sdl3d_get_light_count(const sdl3d_render_context *context);

    /* ============================================================== */
    /* Fog                                                            */
    /* ============================================================== */

    typedef enum sdl3d_fog_mode
    {
        SDL3D_FOG_NONE = 0,
        SDL3D_FOG_LINEAR = 1,
        SDL3D_FOG_EXP = 2,
        SDL3D_FOG_EXP2 = 3
    } sdl3d_fog_mode;

    typedef struct sdl3d_fog
    {
        sdl3d_fog_mode mode;
        float color[3];
        float start;   /* LINEAR: distance where fog begins */
        float end;     /* LINEAR: distance where fog is fully opaque */
        float density; /* EXP / EXP2: fog density coefficient */
    } sdl3d_fog;

    bool sdl3d_set_fog(sdl3d_render_context *context, const sdl3d_fog *fog);
    bool sdl3d_clear_fog(sdl3d_render_context *context);

    /* ============================================================== */
    /* Tonemapping                                                    */
    /* ============================================================== */

    typedef enum sdl3d_tonemap_mode
    {
        SDL3D_TONEMAP_NONE = 0,
        SDL3D_TONEMAP_REINHARD = 1,
        SDL3D_TONEMAP_ACES = 2
    } sdl3d_tonemap_mode;

    bool sdl3d_set_tonemap_mode(sdl3d_render_context *context, sdl3d_tonemap_mode mode);
    sdl3d_tonemap_mode sdl3d_get_tonemap_mode(const sdl3d_render_context *context);

    /* ============================================================== */
    /* UV mapping mode                                                */
    /* ============================================================== */

    typedef enum sdl3d_uv_mode
    {
        SDL3D_UV_PERSPECTIVE = 0,
        SDL3D_UV_AFFINE = 1
    } sdl3d_uv_mode;

    /* ============================================================== */
    /* Fog evaluation mode                                            */
    /* ============================================================== */

    typedef enum sdl3d_fog_eval
    {
        SDL3D_FOG_EVAL_FRAGMENT = 0,
        SDL3D_FOG_EVAL_VERTEX = 1
    } sdl3d_fog_eval;

    /* ============================================================== */
    /* Render profile                                                 */
    /* ============================================================== */

    typedef struct sdl3d_render_profile
    {
        sdl3d_shading_mode shading;
        sdl3d_texture_filter texture_filter;
        sdl3d_uv_mode uv_mode;
        sdl3d_fog_eval fog_eval;
        sdl3d_tonemap_mode tonemap;
        bool vertex_snap;
        int vertex_snap_precision;
        bool color_quantize;
        int color_depth;
    } sdl3d_render_profile;

    bool sdl3d_set_render_profile(sdl3d_render_context *context, const sdl3d_render_profile *profile);
    bool sdl3d_get_render_profile(const sdl3d_render_context *context, sdl3d_render_profile *out);

    sdl3d_render_profile sdl3d_profile_modern(void);
    sdl3d_render_profile sdl3d_profile_ps1(void);
    sdl3d_render_profile sdl3d_profile_n64(void);
    sdl3d_render_profile sdl3d_profile_dos(void);
    sdl3d_render_profile sdl3d_profile_snes(void);

    /* ============================================================== */
    /* Shadow mapping                                                 */
    /* ============================================================== */

#define SDL3D_SHADOW_MAP_SIZE 512

    /*
     * Enable shadow casting for a directional light at the given index.
     * Allocates a depth-only shadow map and computes the light-space
     * orthographic projection covering the specified scene bounds.
     *
     * `light_index` must refer to a SDL3D_LIGHT_DIRECTIONAL light.
     * `scene_radius` defines the half-extent of the ortho projection.
     * `scene_center` is the world-space center the shadow map covers.
     */
    bool sdl3d_enable_shadow(sdl3d_render_context *context, int light_index, sdl3d_vec3 scene_center,
                             float scene_radius);

    bool sdl3d_disable_shadow(sdl3d_render_context *context, int light_index);

    /*
     * Render shadow maps for all shadow-enabled lights. Call after
     * adding lights and before drawing lit geometry. Renders the
     * provided meshes into each shadow map's depth buffer from the
     * light's point of view.
     */
    bool sdl3d_render_shadow_map(sdl3d_render_context *context, const struct sdl3d_mesh *meshes, int mesh_count,
                                 const sdl3d_mat4 *model_matrices);

    /*
     * Begin/end a GPU shadow pass. The application calls begin, draws
     * shadow-casting geometry with the normal draw calls, then calls end.
     * The shadow pass must run to completion before the geometry pass
     * (lesson #5). Requires a shadow-enabled directional light.
     */
    bool sdl3d_begin_shadow_pass(sdl3d_render_context *context);
    bool sdl3d_end_shadow_pass(sdl3d_render_context *context);

#ifdef __cplusplus
}
#endif

#endif

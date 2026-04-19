#ifndef SDL3D_LIGHTING_H
#define SDL3D_LIGHTING_H

#include <stdbool.h>

#include "sdl3d/math.h"
#include "sdl3d/render_context.h"
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
     * POINT:       position, color, intensity, range.
     * SPOT:        position, direction, color, intensity, range,
     *              inner_cutoff, outer_cutoff (cosines of half-angles).
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

#ifdef __cplusplus
}
#endif

#endif

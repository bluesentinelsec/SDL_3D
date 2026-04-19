#ifndef SDL3D_LIGHTING_H
#define SDL3D_LIGHTING_H

#include <stdbool.h>

#include "sdl3d/render_context.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SDL3D_MAX_LIGHTS 8

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
     * Enable or disable lighting. When disabled, DrawModel/DrawMesh
     * use the existing unlit path (texture × tint). When enabled,
     * per-fragment PBR shading is applied using the active lights
     * and the mesh's material properties.
     *
     * Lighting is disabled by default.
     */
    bool sdl3d_set_lighting_enabled(sdl3d_render_context *context, bool enabled);

    bool sdl3d_is_lighting_enabled(const sdl3d_render_context *context);

    /*
     * Set the scene ambient light color (linear RGB, default 0.03 each).
     */
    bool sdl3d_set_ambient_light(sdl3d_render_context *context, float r, float g, float b);

    int sdl3d_get_light_count(const sdl3d_render_context *context);

#ifdef __cplusplus
}
#endif

#endif

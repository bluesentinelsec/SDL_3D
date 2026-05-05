#ifndef SDL3D_SHAPES_H
#define SDL3D_SHAPES_H

#include <stdbool.h>

#include "sdl3d/render_context.h"
#include "sdl3d/texture.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Immediate-mode shape primitives. Every function in this header must
     * be called between sdl3d_begin_mode_3d and sdl3d_end_mode_3d, and
     * every primitive is transformed by the current model matrix stack.
     *
     * Solid primitives emit triangles with counter-clockwise winding
     * around the outward normal, so backface culling is safe to enable.
     * Wire primitives emit lines and ignore the backface-culling flag.
     *
     * Tessellated primitives accept explicit subdivision counts rather
     * than picking defaults for the caller. A slice count is the number
     * of longitudinal segments around an axis; a ring count is the number
     * of latitudinal segments along an axis. Both must be >= 3 where they
     * define a closed loop, and >= 2 where they define an open fan.
     */

    /*
     * Axis-aligned cube centered at `center` with per-axis extents given
     * by `size`. Size components must be non-negative.
     */
    bool sdl3d_draw_cube(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size, sdl3d_color color);
    bool sdl3d_draw_cube_wires(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size, sdl3d_color color);

    /**
     * @brief Draw a textured cube with optional rotation.
     *
     * Each face is UV-mapped to the full [0,1] range of the texture.
     * Pass a zero rotation_axis or zero angle for an axis-aligned cube.
     * The tint color modulates the texture (white = no change).
     *
     * @param context       Render context.
     * @param center        World-space center of the cube.
     * @param size          Per-axis extents (width, height, depth).
     * @param rotation_axis Axis to rotate around (e.g., {0,1,0} for Y).
     * @param rotation_angle Rotation angle in radians.
     * @param texture       Texture to apply (NULL for untextured).
     * @param tint          Color multiplier ({255,255,255,255} = no tint).
     */
    bool sdl3d_draw_cube_textured(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size,
                                  sdl3d_vec3 rotation_axis, float rotation_angle, const sdl3d_texture2d *texture,
                                  sdl3d_color tint);

    /*
     * Planar quad in the local XZ plane, centered at `center`, with
     * outward normal +Y. `size` gives the extent along local X and Z.
     */
    bool sdl3d_draw_plane(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec2 size, sdl3d_color color);

    /*
     * Grid in the local XZ plane centered at the origin. Draws
     * `slices + 1` lines per axis. `slices` must be >= 1 and `spacing`
     * must be positive.
     */
    bool sdl3d_draw_grid(sdl3d_render_context *context, int slices, float spacing, sdl3d_color color);

    /*
     * Line segment from ray.position to ray.position + ray.direction.
     * The direction is drawn as-is; callers control the displayed length
     * through its magnitude.
     */
    bool sdl3d_draw_ray(sdl3d_render_context *context, sdl3d_ray ray, sdl3d_color color);

    /*
     * Wireframe of an axis-aligned bounding box (12 edges). Requires
     * box.min.x <= box.max.x, box.min.y <= box.max.y, box.min.z <= box.max.z.
     */
    bool sdl3d_draw_bounding_box(sdl3d_render_context *context, sdl3d_bounding_box box, sdl3d_color color);

    /*
     * UV sphere centered at `center` with the given radius. `rings` is
     * the number of latitudinal bands between the poles (must be >= 2),
     * `slices` the number of longitudinal divisions (must be >= 3).
     */
    bool sdl3d_draw_sphere(sdl3d_render_context *context, sdl3d_vec3 center, float radius, int rings, int slices,
                           sdl3d_color color);
    /**
     * @brief Draw a solid sphere with optional albedo texture and local rotation.
     *
     * `rotation_axis` and `rotation_angle` rotate the sphere mesh before it is
     * translated to `center`. Pass NULL for `texture` to draw an untextured
     * tinted sphere.
     */
    bool sdl3d_draw_sphere_textured(sdl3d_render_context *context, sdl3d_vec3 center, float radius, int rings,
                                    int slices, sdl3d_vec3 rotation_axis, float rotation_angle,
                                    const sdl3d_texture2d *texture, sdl3d_color tint);
    bool sdl3d_draw_sphere_wires(sdl3d_render_context *context, sdl3d_vec3 center, float radius, int rings, int slices,
                                 sdl3d_color color);

    /*
     * Cylinder (or truncated cone when top and bottom radii differ)
     * aligned with the local +Y axis, centered vertically at `center`.
     * `slices` must be >= 3. Both radii must be non-negative; a zero
     * radius produces a cone endpoint. `height` must be non-negative.
     */
    bool sdl3d_draw_cylinder(sdl3d_render_context *context, sdl3d_vec3 center, float radius_top, float radius_bottom,
                             float height, int slices, sdl3d_color color);
    bool sdl3d_draw_cylinder_wires(sdl3d_render_context *context, sdl3d_vec3 center, float radius_top,
                                   float radius_bottom, float height, int slices, sdl3d_color color);

    /*
     * Capsule: a cylinder between `start` and `end` capped by two
     * hemispheres of `radius`. `slices` must be >= 3, `rings` must be
     * >= 1 (rings per hemisphere). If start == end, the capsule
     * collapses to a sphere.
     */
    bool sdl3d_draw_capsule(sdl3d_render_context *context, sdl3d_vec3 start, sdl3d_vec3 end, float radius, int slices,
                            int rings, sdl3d_color color);
    bool sdl3d_draw_capsule_wires(sdl3d_render_context *context, sdl3d_vec3 start, sdl3d_vec3 end, float radius,
                                  int slices, int rings, sdl3d_color color);

#ifdef __cplusplus
}
#endif

#endif

#ifndef SLAYER3D_SHAPES_H
#define SLAYER3D_SHAPES_H

#include <stdbool.h>

#include "slayer3d/render_context.h"
#include "slayer3d/texture.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Immediate-mode shape primitives. Every function in this header must
     * be called between slayer3d_begin_mode_3d and slayer3d_end_mode_3d, and
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
    bool slayer3d_draw_cube(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                            slayer3d_color color);
    bool slayer3d_draw_cube_wires(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                                  slayer3d_color color);

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
    bool slayer3d_draw_cube_textured(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                                     slayer3d_vec3 rotation_axis, float rotation_angle,
                                     const slayer3d_texture2d *texture, slayer3d_color tint);

    /*
     * Planar quad in the local XZ plane, centered at `center`, with
     * outward normal +Y. `size` gives the extent along local X and Z.
     */
    bool slayer3d_draw_plane(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec2 size,
                             slayer3d_color color);

    /*
     * Flat quad in the local XY plane, centered at `center`, with outward
     * normal +Z. `size` gives the extent along local X and Y.
     */
    bool slayer3d_draw_quad(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec2 size,
                            slayer3d_color color);
    bool slayer3d_draw_quad_wires(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec2 size,
                                  slayer3d_color color);

    /*
     * Flat disc in the local XY plane, centered at `center`, with outward
     * normal +Z. `segments` must be >= 3.
     */
    bool slayer3d_draw_disc(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int segments,
                            slayer3d_color color);
    bool slayer3d_draw_disc_wires(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int segments,
                                  slayer3d_color color);

    /*
     * Grid in the local XZ plane centered at the origin. Draws
     * `slices + 1` lines per axis. `slices` must be >= 1 and `spacing`
     * must be positive.
     */
    bool slayer3d_draw_grid(slayer3d_render_context *context, int slices, float spacing, slayer3d_color color);

    /*
     * Line segment from ray.position to ray.position + ray.direction.
     * The direction is drawn as-is; callers control the displayed length
     * through its magnitude.
     */
    bool slayer3d_draw_ray(slayer3d_render_context *context, slayer3d_ray ray, slayer3d_color color);

    /*
     * Wireframe of an axis-aligned bounding box (12 edges). Requires
     * box.min.x <= box.max.x, box.min.y <= box.max.y, box.min.z <= box.max.z.
     */
    bool slayer3d_draw_bounding_box(slayer3d_render_context *context, slayer3d_bounding_box box, slayer3d_color color);

    /*
     * UV sphere centered at `center` with the given radius. `rings` is
     * the number of latitudinal bands between the poles (must be >= 2),
     * `slices` the number of longitudinal divisions (must be >= 3).
     */
    bool slayer3d_draw_sphere(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int rings,
                              int slices, slayer3d_color color);
    /**
     * @brief Draw a solid sphere with optional albedo texture and local rotation.
     *
     * `rotation_axis` and `rotation_angle` rotate the sphere mesh before it is
     * translated to `center`. Pass NULL for `texture` to draw an untextured
     * tinted sphere.
     */
    bool slayer3d_draw_sphere_textured(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int rings,
                                       int slices, slayer3d_vec3 rotation_axis, float rotation_angle,
                                       const slayer3d_texture2d *texture, slayer3d_color tint);
    bool slayer3d_draw_sphere_wires(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int rings,
                                    int slices, slayer3d_color color);

    /*
     * Hemisphere dome aligned with +Y and centered around `center`. The
     * curved surface is capped by a flat base. `rings` must be >= 2 and
     * `slices` must be >= 3.
     */
    bool slayer3d_draw_hemisphere(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int rings,
                                  int slices, slayer3d_color color);
    bool slayer3d_draw_hemisphere_wires(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int rings,
                                        int slices, slayer3d_color color);

    /*
     * Cylinder (or truncated cone when top and bottom radii differ)
     * aligned with the local +Y axis, centered vertically at `center`.
     * `slices` must be >= 3. Both radii must be non-negative; a zero
     * radius produces a cone endpoint. `height` must be non-negative.
     */
    bool slayer3d_draw_cylinder(slayer3d_render_context *context, slayer3d_vec3 center, float radius_top,
                                float radius_bottom, float height, int slices, slayer3d_color color);
    bool slayer3d_draw_cylinder_wires(slayer3d_render_context *context, slayer3d_vec3 center, float radius_top,
                                      float radius_bottom, float height, int slices, slayer3d_color color);

    /*
     * Capsule: a cylinder between `start` and `end` capped by two
     * hemispheres of `radius`. `slices` must be >= 3, `rings` must be
     * >= 1 (rings per hemisphere). If start == end, the capsule
     * collapses to a sphere.
     */
    bool slayer3d_draw_capsule(slayer3d_render_context *context, slayer3d_vec3 start, slayer3d_vec3 end, float radius,
                               int slices, int rings, slayer3d_color color);
    bool slayer3d_draw_capsule_wires(slayer3d_render_context *context, slayer3d_vec3 start, slayer3d_vec3 end,
                                     float radius, int slices, int rings, slayer3d_color color);

    /*
     * Torus aligned around the local +Y axis. `major_radius` is the
     * distance from the origin to the tube centerline, and `minor_radius`
     * is the tube radius. Both segment counts must be >= 3.
     */
    bool slayer3d_draw_torus(slayer3d_render_context *context, slayer3d_vec3 center, float major_radius,
                             float minor_radius, int segments, int tube_segments, slayer3d_color color);
    bool slayer3d_draw_torus_wires(slayer3d_render_context *context, slayer3d_vec3 center, float major_radius,
                                   float minor_radius, int segments, int tube_segments, slayer3d_color color);

    /*
     * Curved tube segment around the local +Y axis. `arc_angle` is in
     * radians; values near 2*pi form a full torus-like pipe.
     */
    bool slayer3d_draw_tube_segment(slayer3d_render_context *context, slayer3d_vec3 center, float major_radius,
                                    float minor_radius, float arc_angle, int segments, int tube_segments,
                                    slayer3d_color color);
    bool slayer3d_draw_tube_segment_wires(slayer3d_render_context *context, slayer3d_vec3 center, float major_radius,
                                          float minor_radius, float arc_angle, int segments, int tube_segments,
                                          slayer3d_color color);

    /*
     * Rounded box centered in local space. `radius` is clamped to half the
     * smallest size axis. `segments` controls bevel tessellation.
     */
    bool slayer3d_draw_rounded_box(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                                   float radius, int segments, slayer3d_color color);
    bool slayer3d_draw_rounded_box_wires(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                                         float radius, int segments, slayer3d_color color);

    /*
     * Arrow marker aligned along local +Y, centered in local space.
     */
    bool slayer3d_draw_arrow(slayer3d_render_context *context, slayer3d_vec3 center, float radius, float height,
                             int segments, slayer3d_color color);
    bool slayer3d_draw_arrow_wires(slayer3d_render_context *context, slayer3d_vec3 center, float radius, float height,
                                   int segments, slayer3d_color color);

    /*
     * Square pyramid centered in local space. `size.x` and `size.z` define
     * the base extents, and `size.y` defines the height.
     */
    bool slayer3d_draw_pyramid(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                               slayer3d_color color);
    bool slayer3d_draw_pyramid_wires(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                                     slayer3d_color color);

    /*
     * Wedge/ramp triangular prism centered in local space. The low edge is
     * at local -Z and the high edge is at local +Z.
     */
    bool slayer3d_draw_wedge(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                             slayer3d_color color);
    bool slayer3d_draw_wedge_wires(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                                   slayer3d_color color);

#ifdef __cplusplus
}
#endif

#endif

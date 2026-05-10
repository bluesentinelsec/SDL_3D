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
     * Flat quad in the local XY plane, centered at `center`, with outward
     * normal +Z. `size` gives the extent along local X and Y.
     */
    bool sdl3d_draw_quad(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec2 size, sdl3d_color color);
    bool sdl3d_draw_quad_wires(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec2 size, sdl3d_color color);

    /*
     * Flat disc in the local XY plane, centered at `center`, with outward
     * normal +Z. `segments` must be >= 3.
     */
    bool sdl3d_draw_disc(sdl3d_render_context *context, sdl3d_vec3 center, float radius, int segments,
                         sdl3d_color color);
    bool sdl3d_draw_disc_wires(sdl3d_render_context *context, sdl3d_vec3 center, float radius, int segments,
                               sdl3d_color color);

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
     * Hemisphere dome aligned with +Y and centered around `center`. The
     * curved surface is capped by a flat base. `rings` must be >= 2 and
     * `slices` must be >= 3.
     */
    bool sdl3d_draw_hemisphere(sdl3d_render_context *context, sdl3d_vec3 center, float radius, int rings, int slices,
                               sdl3d_color color);
    bool sdl3d_draw_hemisphere_wires(sdl3d_render_context *context, sdl3d_vec3 center, float radius, int rings,
                                     int slices, sdl3d_color color);

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

    /*
     * Torus aligned around the local +Y axis. `major_radius` is the
     * distance from the origin to the tube centerline, and `minor_radius`
     * is the tube radius. Both segment counts must be >= 3.
     */
    bool sdl3d_draw_torus(sdl3d_render_context *context, sdl3d_vec3 center, float major_radius, float minor_radius,
                          int segments, int tube_segments, sdl3d_color color);
    bool sdl3d_draw_torus_wires(sdl3d_render_context *context, sdl3d_vec3 center, float major_radius,
                                float minor_radius, int segments, int tube_segments, sdl3d_color color);

    /*
     * Curved tube segment around the local +Y axis. `arc_angle` is in
     * radians; values near 2*pi form a full torus-like pipe.
     */
    bool sdl3d_draw_tube_segment(sdl3d_render_context *context, sdl3d_vec3 center, float major_radius,
                                 float minor_radius, float arc_angle, int segments, int tube_segments,
                                 sdl3d_color color);
    bool sdl3d_draw_tube_segment_wires(sdl3d_render_context *context, sdl3d_vec3 center, float major_radius,
                                       float minor_radius, float arc_angle, int segments, int tube_segments,
                                       sdl3d_color color);

    /*
     * Rounded box centered in local space. `radius` is clamped to half the
     * smallest size axis. `segments` controls bevel tessellation.
     */
    bool sdl3d_draw_rounded_box(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size, float radius,
                                int segments, sdl3d_color color);
    bool sdl3d_draw_rounded_box_wires(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size, float radius,
                                      int segments, sdl3d_color color);

    /*
     * Arrow marker aligned along local +Y, centered in local space.
     */
    bool sdl3d_draw_arrow(sdl3d_render_context *context, sdl3d_vec3 center, float radius, float height, int segments,
                          sdl3d_color color);
    bool sdl3d_draw_arrow_wires(sdl3d_render_context *context, sdl3d_vec3 center, float radius, float height,
                                int segments, sdl3d_color color);

    /*
     * Square pyramid centered in local space. `size.x` and `size.z` define
     * the base extents, and `size.y` defines the height.
     */
    bool sdl3d_draw_pyramid(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size, sdl3d_color color);
    bool sdl3d_draw_pyramid_wires(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size, sdl3d_color color);

    /*
     * Wedge/ramp triangular prism centered in local space. The low edge is
     * at local -Z and the high edge is at local +Z.
     */
    bool sdl3d_draw_wedge(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size, sdl3d_color color);
    bool sdl3d_draw_wedge_wires(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size, sdl3d_color color);

#ifdef __cplusplus
}
#endif

#endif

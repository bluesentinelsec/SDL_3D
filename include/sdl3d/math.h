#ifndef SDL3D_MATH_H
#define SDL3D_MATH_H

#include <stdbool.h>

#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Conventions
     * -----------
     * - Right-handed coordinate system. The camera looks down -Z by default.
     * - Angles are in radians unless a function name says "_degrees".
     * - Matrices are column-major (see sdl3d_mat4). Composition is right-to-left:
     *   M = P * V * Model applies Model first, then V, then P.
     * - Projection matrices map the canonical view frustum to NDC
     *   x in [-1, 1], y in [-1, 1], z in [-1, 1] (OpenGL convention).
     */

    float sdl3d_degrees_to_radians(float degrees);
    float sdl3d_radians_to_degrees(float radians);

    sdl3d_vec3 sdl3d_vec3_make(float x, float y, float z);
    sdl3d_vec3 sdl3d_vec3_add(sdl3d_vec3 a, sdl3d_vec3 b);
    sdl3d_vec3 sdl3d_vec3_sub(sdl3d_vec3 a, sdl3d_vec3 b);
    sdl3d_vec3 sdl3d_vec3_scale(sdl3d_vec3 v, float s);
    sdl3d_vec3 sdl3d_vec3_negate(sdl3d_vec3 v);
    float sdl3d_vec3_dot(sdl3d_vec3 a, sdl3d_vec3 b);
    sdl3d_vec3 sdl3d_vec3_cross(sdl3d_vec3 a, sdl3d_vec3 b);
    float sdl3d_vec3_length(sdl3d_vec3 v);
    float sdl3d_vec3_length_squared(sdl3d_vec3 v);
    sdl3d_vec3 sdl3d_vec3_normalize(sdl3d_vec3 v);
    sdl3d_vec3 sdl3d_vec3_lerp(sdl3d_vec3 a, sdl3d_vec3 b, float t);

    sdl3d_vec4 sdl3d_vec4_make(float x, float y, float z, float w);
    sdl3d_vec4 sdl3d_vec4_from_vec3(sdl3d_vec3 v, float w);
    sdl3d_vec4 sdl3d_vec4_add(sdl3d_vec4 a, sdl3d_vec4 b);
    sdl3d_vec4 sdl3d_vec4_scale(sdl3d_vec4 v, float s);
    sdl3d_vec4 sdl3d_vec4_lerp(sdl3d_vec4 a, sdl3d_vec4 b, float t);

    sdl3d_mat4 sdl3d_mat4_identity(void);
    sdl3d_mat4 sdl3d_mat4_multiply(sdl3d_mat4 a, sdl3d_mat4 b);
    sdl3d_vec4 sdl3d_mat4_transform_vec4(sdl3d_mat4 m, sdl3d_vec4 v);

    sdl3d_mat4 sdl3d_mat4_translate(sdl3d_vec3 translation);
    sdl3d_mat4 sdl3d_mat4_scale(sdl3d_vec3 scale);
    sdl3d_mat4 sdl3d_mat4_rotate(sdl3d_vec3 axis, float angle_radians);

    /*
     * Right-handed perspective projection with symmetric frustum.
     * fovy_radians is the full vertical field-of-view angle.
     * near_plane and far_plane must both be > 0 with far_plane > near_plane.
     */
    bool sdl3d_mat4_perspective(float fovy_radians, float aspect, float near_plane, float far_plane,
                                sdl3d_mat4 *out_matrix);

    /*
     * Right-handed orthographic projection. near_plane < far_plane required.
     */
    bool sdl3d_mat4_orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane,
                                 sdl3d_mat4 *out_matrix);

    /*
     * Right-handed view matrix. Up must not be parallel to (target - eye).
     */
    bool sdl3d_mat4_look_at(sdl3d_vec3 eye, sdl3d_vec3 target, sdl3d_vec3 up, sdl3d_mat4 *out_matrix);

#ifdef __cplusplus
}
#endif

#endif

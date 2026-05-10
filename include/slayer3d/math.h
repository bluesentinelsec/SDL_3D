#ifndef SLAYER3D_MATH_H
#define SLAYER3D_MATH_H

#include <stdbool.h>

#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Conventions
     * -----------
     * - Right-handed coordinate system. The camera looks down -Z by default.
     * - Angles are in radians unless a function name says "_degrees".
     * - Matrices are column-major (see slayer3d_mat4). Composition is right-to-left:
     *   M = P * V * Model applies Model first, then V, then P.
     * - Projection matrices map the canonical view frustum to NDC
     *   x in [-1, 1], y in [-1, 1], z in [-1, 1] (OpenGL convention).
     */

    float slayer3d_degrees_to_radians(float degrees);
    float slayer3d_radians_to_degrees(float radians);

    slayer3d_vec3 slayer3d_vec3_make(float x, float y, float z);
    slayer3d_vec3 slayer3d_vec3_add(slayer3d_vec3 a, slayer3d_vec3 b);
    slayer3d_vec3 slayer3d_vec3_sub(slayer3d_vec3 a, slayer3d_vec3 b);
    slayer3d_vec3 slayer3d_vec3_scale(slayer3d_vec3 v, float s);
    slayer3d_vec3 slayer3d_vec3_negate(slayer3d_vec3 v);
    float slayer3d_vec3_dot(slayer3d_vec3 a, slayer3d_vec3 b);
    slayer3d_vec3 slayer3d_vec3_cross(slayer3d_vec3 a, slayer3d_vec3 b);
    float slayer3d_vec3_length(slayer3d_vec3 v);
    float slayer3d_vec3_length_squared(slayer3d_vec3 v);
    slayer3d_vec3 slayer3d_vec3_normalize(slayer3d_vec3 v);
    slayer3d_vec3 slayer3d_vec3_lerp(slayer3d_vec3 a, slayer3d_vec3 b, float t);

    slayer3d_vec4 slayer3d_vec4_make(float x, float y, float z, float w);
    slayer3d_vec4 slayer3d_vec4_from_vec3(slayer3d_vec3 v, float w);
    slayer3d_vec4 slayer3d_vec4_add(slayer3d_vec4 a, slayer3d_vec4 b);
    slayer3d_vec4 slayer3d_vec4_scale(slayer3d_vec4 v, float s);
    slayer3d_vec4 slayer3d_vec4_lerp(slayer3d_vec4 a, slayer3d_vec4 b, float t);

    slayer3d_mat4 slayer3d_mat4_identity(void);
    slayer3d_mat4 slayer3d_mat4_multiply(slayer3d_mat4 a, slayer3d_mat4 b);
    slayer3d_vec4 slayer3d_mat4_transform_vec4(slayer3d_mat4 m, slayer3d_vec4 v);

    slayer3d_mat4 slayer3d_mat4_translate(slayer3d_vec3 translation);
    slayer3d_mat4 slayer3d_mat4_scale(slayer3d_vec3 scale);
    slayer3d_mat4 slayer3d_mat4_rotate(slayer3d_vec3 axis, float angle_radians);

    /*
     * Right-handed perspective projection with symmetric frustum.
     * fovy_radians is the full vertical field-of-view angle.
     * near_plane and far_plane must both be > 0 with far_plane > near_plane.
     */
    bool slayer3d_mat4_perspective(float fovy_radians, float aspect, float near_plane, float far_plane,
                                   slayer3d_mat4 *out_matrix);

    /*
     * Right-handed orthographic projection. near_plane < far_plane required.
     */
    bool slayer3d_mat4_orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane,
                                    slayer3d_mat4 *out_matrix);

    /*
     * Right-handed view matrix. Up must not be parallel to (target - eye).
     */
    bool slayer3d_mat4_look_at(slayer3d_vec3 eye, slayer3d_vec3 target, slayer3d_vec3 up, slayer3d_mat4 *out_matrix);

#ifdef __cplusplus
}
#endif

#endif

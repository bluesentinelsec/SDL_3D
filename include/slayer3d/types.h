#ifndef SLAYER3D_TYPES_H
#define SLAYER3D_TYPES_H

#include <SDL3/SDL_stdinc.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct slayer3d_vec2
    {
        float x;
        float y;
    } slayer3d_vec2;

    typedef struct slayer3d_vec3
    {
        float x;
        float y;
        float z;
    } slayer3d_vec3;

    typedef struct slayer3d_vec4
    {
        float x;
        float y;
        float z;
        float w;
    } slayer3d_vec4;

    typedef struct slayer3d_color
    {
        Uint8 r;
        Uint8 g;
        Uint8 b;
        Uint8 a;
    } slayer3d_color;

    /*
     * Column-major storage. m[0..3] is column 0 (the first basis vector).
     * Translation components live in m[12], m[13], m[14]. A matrix-vector
     * multiply treats the vector as a column: out = M * v.
     */
    typedef struct slayer3d_mat4
    {
        float m[16];
    } slayer3d_mat4;

    /*
     * Ray defined by an origin and a direction. The direction is drawn at
     * the magnitude the caller supplies; it is not normalized.
     */
    typedef struct slayer3d_ray
    {
        slayer3d_vec3 position;
        slayer3d_vec3 direction;
    } slayer3d_ray;

    /*
     * Axis-aligned bounding box in the coordinate frame where it is drawn.
     * The current model matrix is applied, so an AABB in local space can be
     * rendered in any world orientation.
     */
    typedef struct slayer3d_bounding_box
    {
        slayer3d_vec3 min;
        slayer3d_vec3 max;
    } slayer3d_bounding_box;

#ifdef __cplusplus
}
#endif

#endif

#ifndef SLAYER3D_CAMERA_H
#define SLAYER3D_CAMERA_H

#include <stdbool.h>

#include "slayer3d/math.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum slayer3d_camera_projection
    {
        SLAYER3D_CAMERA_PERSPECTIVE = 0,
        SLAYER3D_CAMERA_ORTHOGRAPHIC = 1
    } slayer3d_camera_projection;

    typedef enum slayer3d_camera_fov_axis
    {
        SLAYER3D_CAMERA_FOV_VERTICAL = 0,
        SLAYER3D_CAMERA_FOV_HORIZONTAL = 1
    } slayer3d_camera_fov_axis;

    typedef struct slayer3d_camera3d
    {
        slayer3d_vec3 position;
        slayer3d_vec3 target;
        slayer3d_vec3 up;
        /*
         * For SLAYER3D_CAMERA_PERSPECTIVE: field-of-view in degrees. fov_axis
         * selects whether this value is vertical or horizontal.
         * For SLAYER3D_CAMERA_ORTHOGRAPHIC: vertical view volume in world units.
         */
        float fovy;
        slayer3d_camera_projection projection;
        slayer3d_camera_fov_axis fov_axis;
    } slayer3d_camera3d;

    /*
     * Compute view and projection matrices for the camera against a backbuffer
     * of the given dimensions. backbuffer_width and backbuffer_height must be
     * positive; near_plane and far_plane must satisfy 0 < near < far.
     */
    bool slayer3d_camera3d_compute_matrices(const slayer3d_camera3d *camera, int backbuffer_width,
                                            int backbuffer_height, float near_plane, float far_plane,
                                            slayer3d_mat4 *out_view, slayer3d_mat4 *out_projection);

#ifdef __cplusplus
}
#endif

#endif

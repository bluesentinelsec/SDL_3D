#ifndef SDL3D_CAMERA_H
#define SDL3D_CAMERA_H

#include <stdbool.h>

#include "sdl3d/math.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum sdl3d_camera_projection
    {
        SDL3D_CAMERA_PERSPECTIVE = 0,
        SDL3D_CAMERA_ORTHOGRAPHIC = 1
    } sdl3d_camera_projection;

    typedef enum sdl3d_camera_fov_axis
    {
        SDL3D_CAMERA_FOV_VERTICAL = 0,
        SDL3D_CAMERA_FOV_HORIZONTAL = 1
    } sdl3d_camera_fov_axis;

    typedef struct sdl3d_camera3d
    {
        sdl3d_vec3 position;
        sdl3d_vec3 target;
        sdl3d_vec3 up;
        /*
         * For SDL3D_CAMERA_PERSPECTIVE: field-of-view in degrees. fov_axis
         * selects whether this value is vertical or horizontal.
         * For SDL3D_CAMERA_ORTHOGRAPHIC: vertical view volume in world units.
         */
        float fovy;
        sdl3d_camera_projection projection;
        sdl3d_camera_fov_axis fov_axis;
    } sdl3d_camera3d;

    /*
     * Compute view and projection matrices for the camera against a backbuffer
     * of the given dimensions. backbuffer_width and backbuffer_height must be
     * positive; near_plane and far_plane must satisfy 0 < near < far.
     */
    bool sdl3d_camera3d_compute_matrices(const sdl3d_camera3d *camera, int backbuffer_width, int backbuffer_height,
                                         float near_plane, float far_plane, sdl3d_mat4 *out_view,
                                         sdl3d_mat4 *out_projection);

#ifdef __cplusplus
}
#endif

#endif

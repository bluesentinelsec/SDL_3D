#include "slayer3d/camera.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

static float camera_vertical_fov_radians(const slayer3d_camera3d *camera, float aspect)
{
    const float fov_radians = slayer3d_degrees_to_radians(camera->fovy);
    if (camera->fov_axis != SLAYER3D_CAMERA_FOV_HORIZONTAL)
        return fov_radians;

    const float half_tan = SDL_tanf(fov_radians * 0.5f) / aspect;
    return 2.0f * SDL_atanf(half_tan);
}

bool slayer3d_camera3d_compute_matrices(const slayer3d_camera3d *camera, int backbuffer_width, int backbuffer_height,
                                        float near_plane, float far_plane, slayer3d_mat4 *out_view,
                                        slayer3d_mat4 *out_projection)
{
    if (camera == NULL)
    {
        return SDL_InvalidParamError("camera");
    }

    if (out_view == NULL)
    {
        return SDL_InvalidParamError("out_view");
    }

    if (out_projection == NULL)
    {
        return SDL_InvalidParamError("out_projection");
    }

    if (backbuffer_width <= 0 || backbuffer_height <= 0)
    {
        return SDL_SetError("Camera requires positive backbuffer dimensions.");
    }

    if (!slayer3d_mat4_look_at(camera->position, camera->target, camera->up, out_view))
    {
        return false;
    }

    const float aspect = (float)backbuffer_width / (float)backbuffer_height;

    switch (camera->projection)
    {
    case SLAYER3D_CAMERA_PERSPECTIVE: {
        const float fovy_radians = camera_vertical_fov_radians(camera, aspect);
        return slayer3d_mat4_perspective(fovy_radians, aspect, near_plane, far_plane, out_projection);
    }
    case SLAYER3D_CAMERA_ORTHOGRAPHIC: {
        const float half_height = camera->fovy * 0.5f;
        const float half_width = half_height * aspect;
        return slayer3d_mat4_orthographic(-half_width, half_width, -half_height, half_height, near_plane, far_plane,
                                          out_projection);
    }
    default:
        return SDL_SetError("Unknown camera projection: %d", (int)camera->projection);
    }
}

#include "sdl3d/camera.h"

#include <SDL3/SDL_error.h>

bool sdl3d_camera3d_compute_matrices(const sdl3d_camera3d *camera, int backbuffer_width, int backbuffer_height,
                                     float near_plane, float far_plane, sdl3d_mat4 *out_view,
                                     sdl3d_mat4 *out_projection)
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

    if (!sdl3d_mat4_look_at(camera->position, camera->target, camera->up, out_view))
    {
        return false;
    }

    const float aspect = (float)backbuffer_width / (float)backbuffer_height;

    switch (camera->projection)
    {
    case SDL3D_CAMERA_PERSPECTIVE: {
        const float fovy_radians = sdl3d_degrees_to_radians(camera->fovy);
        return sdl3d_mat4_perspective(fovy_radians, aspect, near_plane, far_plane, out_projection);
    }
    case SDL3D_CAMERA_ORTHOGRAPHIC: {
        const float half_height = camera->fovy * 0.5f;
        const float half_width = half_height * aspect;
        return sdl3d_mat4_orthographic(-half_width, half_width, -half_height, half_height, near_plane, far_plane,
                                       out_projection);
    }
    default:
        return SDL_SetError("Unknown camera projection: %d", (int)camera->projection);
    }
}

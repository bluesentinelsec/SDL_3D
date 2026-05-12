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

bool slayer3d_camera3d_screen_ray(const slayer3d_camera3d *camera, float viewport_width, float viewport_height,
                                  float screen_x, float screen_y, float near_distance, float far_distance,
                                  slayer3d_vec3 *out_start, slayer3d_vec3 *out_end)
{
    if (camera == NULL)
        return SDL_InvalidParamError("camera");
    if (out_start == NULL)
        return SDL_InvalidParamError("out_start");
    if (out_end == NULL)
        return SDL_InvalidParamError("out_end");
    if (viewport_width <= 0.0f || viewport_height <= 0.0f)
        return SDL_SetError("Camera screen ray requires positive viewport dimensions.");
    if (near_distance < 0.0f || far_distance <= near_distance)
        return SDL_SetError("Camera screen ray requires 0 <= near < far.");

    const float aspect = viewport_width / viewport_height;
    const float ndc_x = (screen_x / viewport_width) * 2.0f - 1.0f;
    const float ndc_y = 1.0f - (screen_y / viewport_height) * 2.0f;
    const slayer3d_vec3 forward = slayer3d_vec3_normalize(slayer3d_vec3_sub(camera->target, camera->position));
    const slayer3d_vec3 right = slayer3d_vec3_normalize(slayer3d_vec3_cross(forward, camera->up));
    const slayer3d_vec3 up = slayer3d_vec3_normalize(slayer3d_vec3_cross(right, forward));
    if (slayer3d_vec3_length_squared(forward) <= 0.0f || slayer3d_vec3_length_squared(right) <= 0.0f ||
        slayer3d_vec3_length_squared(up) <= 0.0f)
    {
        return SDL_SetError("Camera screen ray requires a valid view basis.");
    }

    if (camera->projection == SLAYER3D_CAMERA_ORTHOGRAPHIC)
    {
        const float half_height = SDL_max(camera->fovy, 0.0f) * 0.5f;
        const float half_width = half_height * aspect;
        const slayer3d_vec3 offset = slayer3d_vec3_add(slayer3d_vec3_scale(right, ndc_x * half_width),
                                                       slayer3d_vec3_scale(up, ndc_y * half_height));
        *out_start =
            slayer3d_vec3_add(slayer3d_vec3_add(camera->position, offset), slayer3d_vec3_scale(forward, near_distance));
        *out_end =
            slayer3d_vec3_add(slayer3d_vec3_add(camera->position, offset), slayer3d_vec3_scale(forward, far_distance));
        return true;
    }

    if (camera->projection != SLAYER3D_CAMERA_PERSPECTIVE)
        return SDL_SetError("Unknown camera projection: %d", (int)camera->projection);

    const float fov_radians = slayer3d_degrees_to_radians(camera->fovy);
    float tan_x = 0.0f;
    float tan_y = 0.0f;
    if (camera->fov_axis == SLAYER3D_CAMERA_FOV_HORIZONTAL)
    {
        tan_x = SDL_tanf(fov_radians * 0.5f);
        tan_y = tan_x / aspect;
    }
    else
    {
        tan_y = SDL_tanf(fov_radians * 0.5f);
        tan_x = tan_y * aspect;
    }

    const slayer3d_vec3 ray_dir = slayer3d_vec3_normalize(slayer3d_vec3_add(
        forward, slayer3d_vec3_add(slayer3d_vec3_scale(right, ndc_x * tan_x), slayer3d_vec3_scale(up, ndc_y * tan_y))));
    if (slayer3d_vec3_length_squared(ray_dir) <= 0.0f)
        return SDL_SetError("Camera screen ray produced an invalid direction.");

    *out_start = slayer3d_vec3_add(camera->position, slayer3d_vec3_scale(ray_dir, near_distance));
    *out_end = slayer3d_vec3_add(camera->position, slayer3d_vec3_scale(ray_dir, far_distance));
    return true;
}

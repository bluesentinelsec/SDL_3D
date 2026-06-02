/**
 * @file game_data_editor_screen_selection.c
 * @brief Shared editor screen-space selection helpers.
 */

#include "game_data_editor_vertex_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/camera.h"
#include "slayer3d/math.h"

bool editor_project_world_to_viewport(const slayer3d_camera3d *camera, const editor_trace_viewport_config *view,
                                      slayer3d_vec3 point, float *out_x, float *out_y)
{
    if (camera == NULL || view == NULL || out_x == NULL || out_y == NULL || view->width <= 0.0f || view->height <= 0.0f)
    {
        return false;
    }

    slayer3d_mat4 view_matrix;
    slayer3d_mat4 projection_matrix;
    if (!slayer3d_camera3d_compute_matrices(camera, (int)SDL_roundf(view->width), (int)SDL_roundf(view->height), 0.01f,
                                            1000.0f, &view_matrix, &projection_matrix))
    {
        return false;
    }

    const slayer3d_mat4 view_projection = slayer3d_mat4_multiply(projection_matrix, view_matrix);
    const slayer3d_vec4 clip = slayer3d_mat4_transform_vec4(view_projection, slayer3d_vec4_from_vec3(point, 1.0f));
    if (SDL_fabsf(clip.w) <= 0.000001f)
        return false;

    const float ndc_x = clip.x / clip.w;
    const float ndc_y = clip.y / clip.w;
    const float ndc_z = clip.z / clip.w;
    if (ndc_z < -1.0f || ndc_z > 1.0f)
        return false;

    *out_x = (ndc_x + 1.0f) * 0.5f * view->width;
    *out_y = (1.0f - ndc_y) * 0.5f * view->height;
    return true;
}

bool editor_lasso_contains_screen_point(const editor_drag_move_state *drag, const editor_trace_viewport_config *view,
                                        float screen_x, float screen_y)
{
    if (drag == NULL || view == NULL)
        return false;
    const float start_x = drag->start_mouse_x - view->x;
    const float start_y = drag->start_mouse_y - view->y;
    const float end_x = drag->current_mouse_x - view->x;
    const float end_y = drag->current_mouse_y - view->y;
    const float min_x = SDL_min(start_x, end_x);
    const float max_x = SDL_max(start_x, end_x);
    const float min_y = SDL_min(start_y, end_y);
    const float max_y = SDL_max(start_y, end_y);
    return screen_x >= min_x && screen_x <= max_x && screen_y >= min_y && screen_y <= max_y;
}

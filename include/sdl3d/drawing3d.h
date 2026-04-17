#ifndef SDL3D_DRAWING3D_H
#define SDL3D_DRAWING3D_H

#include <stdbool.h>

#include "sdl3d/camera.h"
#include "sdl3d/render_context.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Enter 3D drawing mode. Computes view and projection matrices for the
     * camera against the context backbuffer and stores them on the context.
     * All subsequent sdl3d_draw_*_3d calls use these matrices until
     * sdl3d_end_mode_3d is called. Calling begin while already in 3D mode is
     * an error.
     */
    bool sdl3d_begin_mode_3d(sdl3d_render_context *context, sdl3d_camera3d camera);
    bool sdl3d_end_mode_3d(sdl3d_render_context *context);

    bool sdl3d_is_in_mode_3d(const sdl3d_render_context *context);

    /*
     * Override the depth planes used by sdl3d_begin_mode_3d. Must be called
     * before begin_mode_3d. near_plane and far_plane must satisfy 0 < near < far.
     * Defaults: near=0.01, far=1000.
     */
    bool sdl3d_set_depth_planes(sdl3d_render_context *context, float near_plane, float far_plane);

    bool sdl3d_draw_triangle_3d(sdl3d_render_context *context, sdl3d_vec3 v0, sdl3d_vec3 v1, sdl3d_vec3 v2,
                                sdl3d_color color);
    bool sdl3d_draw_line_3d(sdl3d_render_context *context, sdl3d_vec3 start, sdl3d_vec3 end, sdl3d_color color);
    bool sdl3d_draw_point_3d(sdl3d_render_context *context, sdl3d_vec3 position, sdl3d_color color);

    /*
     * Read a single pixel from the color backbuffer. Returns false if x or y
     * fall outside the backbuffer. Intended for tests, screenshots, and
     * pixel-pick queries.
     */
    bool sdl3d_get_framebuffer_pixel(const sdl3d_render_context *context, int x, int y, sdl3d_color *out_color);

    /*
     * Read the depth value at a pixel. Depth is stored in NDC z in [-1, 1].
     * After sdl3d_clear_render_context, all depth values are +1 (far plane).
     */
    bool sdl3d_get_framebuffer_depth(const sdl3d_render_context *context, int x, int y, float *out_depth);

#ifdef __cplusplus
}
#endif

#endif

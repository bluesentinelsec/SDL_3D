#ifndef SDL3D_DRAWING3D_H
#define SDL3D_DRAWING3D_H

#include <stdbool.h>

#include "sdl3d/camera.h"
#include "sdl3d/model.h"
#include "sdl3d/render_context.h"
#include "sdl3d/texture.h"
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
    bool sdl3d_set_backface_culling_enabled(sdl3d_render_context *context, bool enabled);
    bool sdl3d_is_backface_culling_enabled(const sdl3d_render_context *context);
    bool sdl3d_set_wireframe_enabled(sdl3d_render_context *context, bool enabled);
    bool sdl3d_is_wireframe_enabled(const sdl3d_render_context *context);

    /*
     * Matrix stack for immediate-mode model transforms. These functions are
     * only valid between sdl3d_begin_mode_3d and sdl3d_end_mode_3d.
     *
     * The current model matrix starts as identity when 3D mode begins.
     * Transform mutators post-multiply the current model matrix, so calling
     * translate, then rotate, then scale produces Model = T * R * S.
     */
    bool sdl3d_push_matrix(sdl3d_render_context *context);
    bool sdl3d_pop_matrix(sdl3d_render_context *context);
    bool sdl3d_translate(sdl3d_render_context *context, float x, float y, float z);
    bool sdl3d_rotate(sdl3d_render_context *context, sdl3d_vec3 axis, float angle_radians);
    bool sdl3d_scale(sdl3d_render_context *context, float x, float y, float z);

    /*
     * Override the depth planes used by sdl3d_begin_mode_3d. Must be called
     * before begin_mode_3d. near_plane and far_plane must satisfy 0 < near < far.
     * Defaults: near=0.01, far=1000.
     */
    bool sdl3d_set_depth_planes(sdl3d_render_context *context, float near_plane, float far_plane);

    bool sdl3d_draw_triangle_3d(sdl3d_render_context *context, sdl3d_vec3 v0, sdl3d_vec3 v1, sdl3d_vec3 v2,
                                sdl3d_color color);

    /*
     * Triangle with per-vertex colors. Colors are interpolated across the
     * triangle with perspective correction: each attribute is linearly
     * interpolated in screen space as (attribute / w) and divided by the
     * linearly interpolated (1 / w) at each pixel, which reproduces the
     * correct 3D-space blend even under heavy perspective foreshortening.
     *
     * When wireframe is enabled, edges use linear screen-space color
     * interpolation between their endpoints.
     */
    bool sdl3d_draw_triangle_3d_ex(sdl3d_render_context *context, sdl3d_vec3 v0, sdl3d_vec3 v1, sdl3d_vec3 v2,
                                   sdl3d_color c0, sdl3d_color c1, sdl3d_color c2);
    bool sdl3d_draw_line_3d(sdl3d_render_context *context, sdl3d_vec3 start, sdl3d_vec3 end, sdl3d_color color);
    bool sdl3d_draw_point_3d(sdl3d_render_context *context, sdl3d_vec3 position, sdl3d_color color);

    /*
     * Draw a single mesh using the current model matrix stack. If `texture`
     * is NULL, the mesh is rendered untextured and `tint` supplies the flat
     * color. If `texture` is non-NULL, mesh UVs are interpreted as normalized
     * coordinates with (0, 0) at the lower-left of the texture.
     *
     * Mesh vertex colors, when present, modulate the texture/tint.
     */
    bool sdl3d_draw_mesh(sdl3d_render_context *context, const sdl3d_mesh *mesh, const sdl3d_texture2d *texture,
                         sdl3d_color tint);

    /*
     * Draw every mesh in a model with a uniform translation + scale. Material
     * albedo factors and albedo textures modulate the supplied tint. Texture
     * paths are resolved relative to the model's source path and cached on
     * the render context after first use.
     */
    bool sdl3d_draw_model(sdl3d_render_context *context, const sdl3d_model *model, sdl3d_vec3 position, float scale,
                          sdl3d_color tint);

    /*
     * Same as sdl3d_draw_model, but applies translation, axis-angle rotation,
     * and non-uniform scale before submitting the model. The transform
     * composes with the current matrix stack.
     */
    bool sdl3d_draw_model_ex(sdl3d_render_context *context, const sdl3d_model *model, sdl3d_vec3 position,
                             sdl3d_vec3 rotation_axis, float rotation_angle_radians, sdl3d_vec3 scale,
                             sdl3d_color tint);

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

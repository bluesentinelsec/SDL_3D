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

    typedef enum sdl3d_billboard_mode
    {
        /* Keep the sprite upright in world space while facing the camera. */
        SDL3D_BILLBOARD_UPRIGHT = 0,
        /* Fully face the camera using the camera's right/up vectors. */
        SDL3D_BILLBOARD_SPHERICAL = 1
    } sdl3d_billboard_mode;

    typedef enum sdl3d_overlay_effect
    {
        SDL3D_OVERLAY_EFFECT_NONE = 0,
        SDL3D_OVERLAY_EFFECT_MELT = 1
    } sdl3d_overlay_effect;

    /*
     * Draw a textured camera-facing quad. `position` is the billboard pivot
     * in world space, `size` is width/height in world units, and `anchor`
     * selects which point inside the sprite sits at `position`:
     * - x=0 left, 0.5 center, 1 right
     * - y=0 bottom, 0.5 center, 1 top
     *
     * Billboards use the lit textured path when scene lighting is enabled and
     * active, and otherwise fall back to the unlit textured path. This keeps
     * authored sprite shading intact in unlit scenes while allowing point
     * lights, such as projectiles, to affect billboard sprites.
     */
    bool sdl3d_draw_billboard_ex(sdl3d_render_context *context, const sdl3d_texture2d *texture, sdl3d_vec3 position,
                                 sdl3d_vec2 size, sdl3d_vec2 anchor, sdl3d_billboard_mode mode, sdl3d_color tint);

    /**
     * @brief Draw a billboard with an authored custom sprite shader.
     *
     * The shader sources use the same textured-quad vertex contract as the
     * built-in unlit path. The fragment source is required; the vertex source
     * is optional and falls back to the engine's default billboard vertex
     * shader when NULL. When @p lighting is true, the custom shader is routed
     * through the lit billboard path and receives the standard lighting
     * uniforms used by the built-in lit renderer. Software rendering ignores
     * the custom shader and falls back to the built-in billboard draw path.
     */
    bool sdl3d_draw_billboard_shader_ex(sdl3d_render_context *context, const sdl3d_texture2d *texture,
                                        sdl3d_vec3 position, sdl3d_vec2 size, sdl3d_vec2 anchor,
                                        sdl3d_billboard_mode mode, sdl3d_color tint, bool lighting,
                                        const char *shader_vertex_source, const char *shader_fragment_source);

    /*
     * Convenience wrapper for FPS-style sprites: upright billboard with a
     * bottom-center pivot.
     */
    bool sdl3d_draw_billboard(sdl3d_render_context *context, const sdl3d_texture2d *texture, sdl3d_vec3 position,
                              sdl3d_vec2 size, sdl3d_color tint);

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
     * Draw a model with skeletal animation applied. `joint_matrices` is
     * an array of skeleton->joint_count mat4 entries from
     * sdl3d_evaluate_animation. Pass NULL for bind pose (same as draw_model_ex).
     */
    bool sdl3d_draw_model_skinned(sdl3d_render_context *context, const sdl3d_model *model, sdl3d_vec3 position,
                                  sdl3d_vec3 rotation_axis, float rotation_angle_radians, sdl3d_vec3 scale,
                                  sdl3d_color tint, const sdl3d_mat4 *joint_matrices);

    /*
     * Draw a solid screen-space rectangle on the UI overlay layer.
     *
     * The overlay is composited after the main scene and all post-processing,
     * so this is the correct primitive for editor UI, debug panels, and
     * other HUD-like elements that must remain crisp and stable.
     *
     * Coordinates are in logical render-context pixels with (0, 0) at the
     * top-left. The current scissor rect, when enabled, is honored.
     */
    bool sdl3d_draw_rect_overlay(sdl3d_render_context *context, float x, float y, float w, float h, sdl3d_color color);

    /**
     * @brief Draw a textured screen-space rectangle on the UI overlay layer.
     *
     * Coordinates are in logical render-context pixels with (0, 0) at the
     * top-left. The texture is sampled across the full rectangle and modulated
     * by @p tint. @p effect, when not SDL3D_OVERLAY_EFFECT_NONE, applies a
     * reusable screen-space image effect before sampling. The current scissor
     * rect, when enabled, is honored.
     *
     * @param context Render context receiving the overlay draw.
     * @param texture RGBA texture to draw.
     * @param x Left edge in render-context pixels.
     * @param y Top edge in render-context pixels.
     * @param w Width in render-context pixels.
     * @param h Height in render-context pixels.
     * @param tint Color multiplier and alpha applied to the texture.
     * @param effect Optional overlay effect.
     * @param effect_progress Normalized effect progression in [0, 1].
     * @param effect_seed Deterministic seed used by the effect shader.
     * @return true on success.
     */
    bool sdl3d_draw_texture_overlay(sdl3d_render_context *context, const sdl3d_texture2d *texture, float x, float y,
                                    float w, float h, sdl3d_color tint, sdl3d_overlay_effect effect,
                                    float effect_progress, Uint32 effect_seed);

    /**
     * @brief Draw a textured overlay quad with an authored custom sprite shader.
     *
     * The shader sources use the same textured-quad vertex contract as the
     * built-in unlit overlay path. The fragment source is required; the
     * vertex source is optional and falls back to the engine's default
     * overlay vertex shader when NULL. When a custom shader is authored,
     * software rendering falls back to the built-in overlay effect path.
     */
    bool sdl3d_draw_texture_overlay_shader(sdl3d_render_context *context, const sdl3d_texture2d *texture, float x,
                                           float y, float w, float h, sdl3d_color tint, sdl3d_overlay_effect effect,
                                           float effect_progress, Uint32 effect_seed, const char *shader_vertex_source,
                                           const char *shader_fragment_source);

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

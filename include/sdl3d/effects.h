#ifndef SDL3D_EFFECTS_H
#define SDL3D_EFFECTS_H

#include <stdbool.h>

#include "sdl3d/render_context.h"
#include "sdl3d/texture.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ============================================================== */
    /* Particle system                                                */
    /* ============================================================== */

    typedef struct sdl3d_particle_config
    {
        sdl3d_vec3 position;
        sdl3d_vec3 direction;
        float spread; /* cone half-angle in radians */
        float speed_min;
        float speed_max;
        float lifetime_min;
        float lifetime_max;
        float size_start;
        float size_end;
        sdl3d_color color_start;
        sdl3d_color color_end;
        float gravity;
        int max_particles;
        float emit_rate; /* particles per second */
    } sdl3d_particle_config;

    typedef struct sdl3d_particle_emitter sdl3d_particle_emitter;

    sdl3d_particle_emitter *sdl3d_create_particle_emitter(const sdl3d_particle_config *config);
    void sdl3d_destroy_particle_emitter(sdl3d_particle_emitter *emitter);

    void sdl3d_particle_emitter_update(sdl3d_particle_emitter *emitter, float delta_time);
    void sdl3d_particle_emitter_emit(sdl3d_particle_emitter *emitter, int count);
    void sdl3d_particle_emitter_set_position(sdl3d_particle_emitter *emitter, sdl3d_vec3 position);

    /*
     * Draw all live particles as camera-facing billboards.
     * Must be called between begin_mode_3d / end_mode_3d.
     */
    bool sdl3d_draw_particles(sdl3d_render_context *context, const sdl3d_particle_emitter *emitter);

    int sdl3d_particle_emitter_get_count(const sdl3d_particle_emitter *emitter);

    /* ============================================================== */
    /* Skybox                                                         */
    /* ============================================================== */

    /*
     * Draw a solid-color gradient skybox. Call before drawing scene
     * geometry, after begin_mode_3d. Renders a large sphere with
     * top_color at the zenith and bottom_color at the nadir.
     */
    bool sdl3d_draw_skybox_gradient(sdl3d_render_context *context, sdl3d_color top_color, sdl3d_color bottom_color);

    typedef struct sdl3d_skybox_textured
    {
        const sdl3d_texture2d *pos_x;
        const sdl3d_texture2d *neg_x;
        const sdl3d_texture2d *pos_y;
        const sdl3d_texture2d *neg_y;
        const sdl3d_texture2d *pos_z;
        const sdl3d_texture2d *neg_z;
        float size;
    } sdl3d_skybox_textured;

    /*
     * Draw a textured skybox centered on the active camera.
     * All six face textures are required and should correspond to the
     * standard cubemap directions (+X, -X, +Y, -Y, +Z, -Z).
     */
    bool sdl3d_draw_skybox_textured(sdl3d_render_context *context, const sdl3d_skybox_textured *skybox);

    /* ============================================================== */
    /* Post-process effects                                           */
    /* ============================================================== */

    typedef enum sdl3d_post_effect
    {
        SDL3D_POST_NONE = 0,
        SDL3D_POST_BLOOM = 1,
        SDL3D_POST_VIGNETTE = 2,
        SDL3D_POST_COLOR_GRADE = 4
    } sdl3d_post_effect;

    typedef struct sdl3d_post_process_config
    {
        int effects;                  /* bitwise OR of sdl3d_post_effect */
        float bloom_threshold;        /* luminance threshold for bloom (default 0.8) */
        float bloom_intensity;        /* bloom blend strength (default 0.3) */
        float vignette_intensity;     /* 0 = none, 1 = full black corners */
        float color_grade_contrast;   /* 1.0 = neutral */
        float color_grade_brightness; /* 0.0 = neutral */
        float color_grade_saturation; /* 1.0 = neutral */
    } sdl3d_post_process_config;

    /*
     * Apply post-processing effects to the framebuffer. Call after
     * end_mode_3d and before present_render_context.
     */
    bool sdl3d_apply_post_process(sdl3d_render_context *context, const sdl3d_post_process_config *config);

#ifdef __cplusplus
}
#endif

#endif

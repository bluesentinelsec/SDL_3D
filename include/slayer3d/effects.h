#ifndef SLAYER3D_EFFECTS_H
#define SLAYER3D_EFFECTS_H

#include <stdbool.h>

#include "slayer3d/render_context.h"
#include "slayer3d/texture.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ============================================================== */
    /* Particle system                                                */
    /* ============================================================== */

    /**
     * @brief Particle spawn volume.
     *
     * Point emitters preserve the original cone-emitter behavior. Box and
     * circle emitters are useful for sector hazards, smoke banks, dust, and
     * other effects that should originate from an area instead of a single
     * point.
     */
    typedef enum slayer3d_particle_emitter_shape
    {
        SLAYER3D_PARTICLE_EMITTER_POINT = 0,  /**< Spawn at `position`. */
        SLAYER3D_PARTICLE_EMITTER_BOX = 1,    /**< Spawn inside `position +/- extents`. */
        SLAYER3D_PARTICLE_EMITTER_CIRCLE = 2, /**< Spawn inside an XZ circle centered on `position`. */
    } slayer3d_particle_emitter_shape;

    /**
     * @brief Particle render style.
     *
     * The default style draws the authored texture or tinted quad. Soft smoke
     * uses a procedural GPU fragment shader when available, with the default
     * quad path as the software fallback.
     */
    typedef enum slayer3d_particle_render_style
    {
        SLAYER3D_PARTICLE_RENDER_DEFAULT = 0,
        SLAYER3D_PARTICLE_RENDER_SOFT_SMOKE = 1,
    } slayer3d_particle_render_style;

    /**
     * @brief Particle emitter configuration.
     *
     * Existing point-emitter fields keep their original meaning. Newly added
     * fields default to conservative behavior when zero-initialized:
     * point emitter, no texture, upright camera-facing quads, depth tested,
     * no additive blending, and non-deterministic SDL randomness.
     */
    typedef struct slayer3d_particle_config
    {
        slayer3d_vec3 position;     /**< Emitter origin or volume center in world units. */
        slayer3d_vec3 direction;    /**< Preferred particle travel direction. Zero means straight up. */
        float spread;               /**< Cone half-angle in radians around `direction`. */
        float speed_min;            /**< Minimum spawn speed in world units per second. */
        float speed_max;            /**< Maximum spawn speed in world units per second. */
        float lifetime_min;         /**< Minimum particle lifetime in seconds. */
        float lifetime_max;         /**< Maximum particle lifetime in seconds. */
        float size_start;           /**< Particle width/height at birth in world units. */
        float size_end;             /**< Particle width/height at death in world units. */
        slayer3d_color color_start; /**< Particle tint at birth. */
        slayer3d_color color_end;   /**< Particle tint at death. */
        float gravity;              /**< Downward acceleration in world units per second squared. */
        int max_particles;          /**< Maximum live/storage particles; defaults to 128 when <= 0. */
        float emit_rate;            /**< Continuous emission rate in particles per second. */

        slayer3d_particle_emitter_shape shape;       /**< Spawn volume shape. */
        slayer3d_particle_render_style render_style; /**< Draw style for each particle billboard. */
        slayer3d_vec3 extents;                       /**< Half-size for SLAYER3D_PARTICLE_EMITTER_BOX. */
        float radius;                                /**< XZ radius for SLAYER3D_PARTICLE_EMITTER_CIRCLE. */
        float emissive_intensity;                    /**< Extra unlit brightness scale while drawing particles. */
        bool camera_facing;                /**< True for spherical billboards, false for upright billboards. */
        bool depth_test;                   /**< Reserved for future renderer support; currently depth-tested. */
        bool additive_blend;               /**< Reserved for future renderer support; currently normal geometry path. */
        const slayer3d_texture2d *texture; /**< Optional RGBA particle texture; NULL draws colored quads. */
        Uint32 random_seed;                /**< Nonzero seed makes particle spawning deterministic per emitter. */
    } slayer3d_particle_config;

    /**
     * @brief Opaque CPU particle emitter.
     */
    typedef struct slayer3d_particle_emitter slayer3d_particle_emitter;

    /**
     * @brief Read-only particle state for tests, tools, and debug views.
     */
    typedef struct slayer3d_particle_snapshot
    {
        slayer3d_vec3 position; /**< Current world-space particle center. */
        slayer3d_vec3 velocity; /**< Current world-space particle velocity. */
        float lifetime;         /**< Seconds since spawn. */
        float max_lifetime;     /**< Seconds before expiration. */
        bool alive;             /**< Whether this slot is currently live. */
    } slayer3d_particle_snapshot;

    /**
     * @brief Create a particle emitter from `config`.
     *
     * Returns NULL and sets SDL_GetError() on invalid input or allocation
     * failure. The config is copied, so callers may keep it on the stack.
     */
    slayer3d_particle_emitter *slayer3d_create_particle_emitter(const slayer3d_particle_config *config);

    /**
     * @brief Destroy an emitter. Safe with NULL.
     */
    void slayer3d_destroy_particle_emitter(slayer3d_particle_emitter *emitter);

    /**
     * @brief Replace an emitter's configuration.
     *
     * Reallocates particle storage if `config->max_particles` changes. On
     * failure the emitter remains unchanged and SDL_GetError() describes the
     * problem.
     */
    bool slayer3d_particle_emitter_set_config(slayer3d_particle_emitter *emitter,
                                              const slayer3d_particle_config *config);

    /**
     * @brief Return the emitter's current copied configuration, or NULL.
     */
    const slayer3d_particle_config *slayer3d_particle_emitter_get_config(const slayer3d_particle_emitter *emitter);

    /**
     * @brief Remove all live particles and reset continuous emission debt.
     */
    void slayer3d_particle_emitter_clear(slayer3d_particle_emitter *emitter);

    /**
     * @brief Advance particle simulation and continuous emission.
     */
    void slayer3d_particle_emitter_update(slayer3d_particle_emitter *emitter, float delta_time);

    /**
     * @brief Emit up to `count` particles immediately.
     */
    void slayer3d_particle_emitter_emit(slayer3d_particle_emitter *emitter, int count);

    /**
     * @brief Move the emitter origin or volume center without altering live particles.
     */
    void slayer3d_particle_emitter_set_position(slayer3d_particle_emitter *emitter, slayer3d_vec3 position);

    /**
     * @brief Draw all live particles as camera-facing quads.
     *
     * Must be called between slayer3d_begin_mode_3d() and slayer3d_end_mode_3d().
     * The current implementation is depth-tested through the regular geometry
     * path. `additive_blend` and `depth_test` are carried in the public config
     * so callers can author effects against stable API while backend blending
     * support evolves.
     */
    bool slayer3d_draw_particles(slayer3d_render_context *context, const slayer3d_particle_emitter *emitter);

    /**
     * @brief Return the number of currently live particles. Safe with NULL.
     */
    int slayer3d_particle_emitter_get_count(const slayer3d_particle_emitter *emitter);

    /**
     * @brief Copy live particle state into `out_particles`.
     *
     * Returns the total number of live particles, even when `max_particles` is
     * smaller than that count or `out_particles` is NULL. This lets callers
     * size a buffer with a first pass.
     */
    int slayer3d_particle_emitter_snapshot(const slayer3d_particle_emitter *emitter,
                                           slayer3d_particle_snapshot *out_particles, int max_particles);

    /* ============================================================== */
    /* Skybox                                                         */
    /* ============================================================== */

    /*
     * Draw a solid-color gradient skybox. Call before drawing scene
     * geometry, after begin_mode_3d. Renders a large sphere with
     * top_color at the zenith and bottom_color at the nadir.
     */
    bool slayer3d_draw_skybox_gradient(slayer3d_render_context *context, slayer3d_color top_color,
                                       slayer3d_color bottom_color);

    typedef struct slayer3d_skybox_textured
    {
        const slayer3d_texture2d *pos_x;
        const slayer3d_texture2d *neg_x;
        const slayer3d_texture2d *pos_y;
        const slayer3d_texture2d *neg_y;
        const slayer3d_texture2d *pos_z;
        const slayer3d_texture2d *neg_z;
        float size;
    } slayer3d_skybox_textured;

    /*
     * Draw a textured skybox centered on the active camera.
     * All six face textures are required and should correspond to the
     * standard cubemap directions (+X, -X, +Y, -Y, +Z, -Z).
     */
    bool slayer3d_draw_skybox_textured(slayer3d_render_context *context, const slayer3d_skybox_textured *skybox);

    /* ============================================================== */
    /* Post-process effects                                           */
    /* ============================================================== */

    typedef enum slayer3d_post_effect
    {
        SLAYER3D_POST_NONE = 0,
        SLAYER3D_POST_BLOOM = 1,
        SLAYER3D_POST_VIGNETTE = 2,
        SLAYER3D_POST_COLOR_GRADE = 4
    } slayer3d_post_effect;

    typedef struct slayer3d_post_process_config
    {
        int effects;                  /* bitwise OR of slayer3d_post_effect */
        float bloom_threshold;        /* luminance threshold for bloom (default 0.8) */
        float bloom_intensity;        /* bloom blend strength (default 0.3) */
        float vignette_intensity;     /* 0 = none, 1 = full black corners */
        float color_grade_contrast;   /* 1.0 = neutral */
        float color_grade_brightness; /* 0.0 = neutral */
        float color_grade_saturation; /* 1.0 = neutral */
    } slayer3d_post_process_config;

    /*
     * Apply post-processing effects to the framebuffer. Call after
     * end_mode_3d and before present_render_context.
     */
    bool slayer3d_apply_post_process(slayer3d_render_context *context, const slayer3d_post_process_config *config);

#ifdef __cplusplus
}
#endif

#endif

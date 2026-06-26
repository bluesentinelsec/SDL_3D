/**
 * @file game_data_render_runtime.h
 * @brief Runtime camera, lighting, render primitive, and presentation APIs for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_RENDER_RUNTIME_H
#define SLAYER3D_GAME_DATA_RENDER_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>

#include "slayer3d/camera.h"
#include "slayer3d/effects.h"
#include "slayer3d/game_data_render.h"
#include "slayer3d/game_data_scene.h"
#include "slayer3d/lighting.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

    /**
     * @brief Advance data-authored presentation clocks.
     *
     * Presentation clocks are generic data-driven oscillators/counters used by
     * UI, lights, and other render-facing effects. Authored clocks may write
     * into actor properties so scripts, UI bindings, and render evaluation can
     * share the same source of truth.
     */
    bool slayer3d_game_data_update_presentation_clocks(slayer3d_game_data_runtime *runtime, float dt, bool paused,
                                                       bool pause_entered);

    /**
     * @brief Read authored FPS metric sample duration in seconds.
     */
    float slayer3d_game_data_fps_sample_seconds(const slayer3d_game_data_runtime *runtime, float fallback);

    /**
     * @brief Return the currently active authored camera name.
     *
     * The returned pointer is owned by the parsed JSON document and remains
     * valid until the runtime is destroyed.
     */
    const char *slayer3d_game_data_active_camera(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read an authored non-adapter camera by name.
     *
     * Adapter cameras are game-specific and return false here because their
     * final pose is computed by game code or script. Orthographic cameras use
     * `size` as slayer3d_camera3d::fovy; perspective cameras use `fov` or the
     * legacy `fovy` field with an optional `fov_axis`.
     */
    bool slayer3d_game_data_get_camera(const slayer3d_game_data_runtime *runtime, const char *name,
                                       slayer3d_camera3d *out_camera);

    /**
     * @brief Read a numeric custom property from an authored camera.
     *
     * This lets games keep camera tuning data in JSON even when the camera pose
     * itself is adapter-driven.
     */
    bool slayer3d_game_data_get_camera_float(const slayer3d_game_data_runtime *runtime, const char *camera_name,
                                             const char *property_name, float *out_value);

    /**
     * @brief Read the authored world unit convention.
     *
     * Games may omit these fields; SLAYER3D then reports the engine default:
     * units="meters" and meters_per_unit=1.0. This is metadata for tools,
     * editors, physics tuning, and documentation. The renderer interprets all
     * authored positions consistently as world units.
     */
    bool slayer3d_game_data_get_world_units(const slayer3d_game_data_runtime *runtime, const char **out_units,
                                            float *out_meters_per_unit);

    /** @brief Runtime descriptor for one generated or authored world lighting artifact. */
    typedef struct slayer3d_game_data_lighting_artifact
    {
        /** @brief Stable artifact identifier, such as `lighting.static.default`. */
        const char *id;
        /** @brief Path to the artifact relative to the loaded game JSON file. */
        const char *path;
        /** @brief Artifact schema/format string, such as `slayer3d.lighting_static.v0`. */
        const char *format;
        /** @brief Optional bake group name used by editor/build tooling. */
        const char *bake_group;
        /** @brief True when the artifact is self-contained and does not require the source map. */
        bool self_contained;
    } slayer3d_game_data_lighting_artifact;

    /** @brief Lightweight parsed summary of one static-lighting artifact. */
    typedef struct slayer3d_game_data_static_lighting_summary
    {
        /** @brief Number of per-face irradiance samples in the artifact. */
        size_t sample_count;
        /** @brief Average RGB irradiance across all samples. */
        float average_rgb[3];
        /** @brief Average scalar intensity across all samples. */
        float average_intensity;
    } slayer3d_game_data_static_lighting_summary;

    /**
     * @brief Return the number of declared world lighting artifacts.
     *
     * Generated playable SlayerMap packages use `world.lighting_artifacts` to
     * point callers at static lighting payloads such as
     * `lighting/static.default.json`.
     */
    int slayer3d_game_data_lighting_artifact_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read one declared world lighting artifact by zero-based index.
     *
     * Returned string pointers are owned by the loaded game data document and
     * remain valid until the runtime is destroyed.
     */
    bool slayer3d_game_data_get_lighting_artifact(const slayer3d_game_data_runtime *runtime, int index,
                                                  slayer3d_game_data_lighting_artifact *out_artifact);

    /**
     * @brief Read and validate one declared lighting artifact JSON file.
     *
     * The artifact path is resolved relative to the loaded game JSON file. The
     * returned buffer is NUL-terminated for convenience, but @p out_size reports
     * the exact file byte count. The caller owns the returned buffer and must
     * release it with SDL_free().
     */
    bool slayer3d_game_data_read_lighting_artifact_json(const slayer3d_game_data_runtime *runtime, int index,
                                                        char **out_json, size_t *out_size, char *error_buffer,
                                                        int error_buffer_size);

    /**
     * @brief Read and summarize one declared static-lighting artifact.
     *
     * This is a renderer/tooling bridge for the current
     * `slayer3d.lighting_static.v0` payload. It validates and parses the
     * artifact, then reports the sample count and average irradiance without
     * exposing callers to the raw JSON representation.
     */
    bool slayer3d_game_data_get_static_lighting_summary(const slayer3d_game_data_runtime *runtime, int index,
                                                        slayer3d_game_data_static_lighting_summary *out_summary,
                                                        char *error_buffer, int error_buffer_size);

    /**
     * @brief Return the number of active world lights.
     *
     * In normal game runtimes this counts authored world/entity lights. Editor
     * runtimes may append preview lights from placed light Things when editor
     * lighting preview is enabled.
     */
    int slayer3d_game_data_world_light_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Return the light upload budget for active world-light rendering.
     *
     * Most game runtimes return `SLAYER3D_MAX_LIGHTS`. Editor runtimes may
     * lower this based on lighting preview quality so WYSIWYG editing remains
     * responsive with many placed lights.
     */
    int slayer3d_game_data_world_light_upload_limit(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read the authored world ambient light color.
     *
     * Values are linear RGB in the same range expected by
     * slayer3d_set_ambient_light().
     */
    bool slayer3d_game_data_get_world_ambient_light(const slayer3d_game_data_runtime *runtime, float out_rgb[3]);

    /**
     * @brief Read an active world light by zero-based index.
     *
     * The returned light is suitable for passing to slayer3d_add_light(). Lights
     * may target one entity with `target_entity`, or the first active-scene
     * entity in an ordered `target_entities` fallback list. Editor preview
     * lights are returned after authored lights.
     */
    bool slayer3d_game_data_get_world_light(const slayer3d_game_data_runtime *runtime, int index,
                                            slayer3d_light *out_light);

    /**
     * @brief Read an authored world light with generic visual effects evaluated.
     *
     * Supported light effects include `pulse`, `color_cycle`, `flash`,
     * `rotate_direction`, and `orbit_position`, allowing data to drive color
     * blends, intensity changes, range changes, direction changes, and moving
     * light positions over time or from actor properties. Passing NULL for
     * @p eval uses a zeroed evaluation context.
     */
    bool slayer3d_game_data_get_world_light_evaluated(const slayer3d_game_data_runtime *runtime, int index,
                                                      const slayer3d_game_data_render_eval *eval,
                                                      slayer3d_light *out_light);

    /**
     * @brief Iterate active authored render primitive components.
     *
     * Components currently supported by this iterator include `render.cube`,
     * `render.sphere`, `render.mesh_primitive`, `render.composite`,
     * `render.sprite`, and `render.model`. Iteration skips inactive actors.
     */
    bool slayer3d_game_data_for_each_render_primitive(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_render_primitive_fn callback, void *userdata);

    /**
     * @brief Iterate active authored render primitives with dynamic effects evaluated.
     *
     * This applies generic `effects` authored on render primitive components,
     * such as property-driven flash colors, size offsets, and time-driven
     * pulses. Passing NULL for @p eval uses a zeroed evaluation context.
     */
    bool slayer3d_game_data_for_each_render_primitive_evaluated(const slayer3d_game_data_runtime *runtime,
                                                                const slayer3d_game_data_render_eval *eval,
                                                                slayer3d_game_data_render_primitive_fn callback,
                                                                void *userdata);

    /**
     * @brief Read an authored particle emitter component from an entity.
     *
     * The returned config is ready for slayer3d_create_particle_emitter(). Texture
     * references are intentionally not resolved here yet, so config.texture is
     * always NULL.
     */
    bool slayer3d_game_data_get_particle_emitter(const slayer3d_game_data_runtime *runtime, const char *entity_name,
                                                 slayer3d_particle_config *out_config);

    /**
     * @brief Read optional draw-time emissive color for a particle emitter entity.
     *
     * The color is read from the emitter component's `draw_emissive` field and
     * defaults to zero when not authored.
     */
    bool slayer3d_game_data_get_particle_emitter_draw_emissive(const slayer3d_game_data_runtime *runtime,
                                                               const char *entity_name, slayer3d_vec3 *out_rgb);

    /**
     * @brief Iterate active authored particle emitter components.
     *
     * Iteration skips inactive actors and entities not included by the active
     * scene. The descriptor's config is ready for slayer3d_create_particle_emitter().
     */
    bool slayer3d_game_data_for_each_particle_emitter(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_particle_emitter_fn callback, void *userdata);

    /**
     * @brief Read authored render setup.
     *
     * Missing fields produce conservative defaults: black clear color, lighting
     * enabled, bloom/SSAO enabled, and ACES tonemapping.
     */
    bool slayer3d_game_data_get_render_settings(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_game_data_render_settings *out_settings);

    /**
     * @brief Read a named authored transition descriptor.
     *
     * @p name is looked up under the top-level `transitions` object.
     */
    bool slayer3d_game_data_get_transition(const slayer3d_game_data_runtime *runtime, const char *name,
                                           slayer3d_game_data_transition_desc *out_transition);

    /**
     * @brief Advance data-authored property animation components.
     *
     * Currently supports `property.decay` components, which move a numeric
     * actor property toward a target value at an authored rate. This is useful
     * for reusable presentation state such as flashes, glow weights, and other
     * transient values without per-game C code.
     */
    bool slayer3d_game_data_update_property_effects(slayer3d_game_data_runtime *runtime, float dt);

    /**
     * @brief Return the dt currently being processed by slayer3d_game_data_update().
     *
     * Adapter callbacks can use this for per-frame controller behavior. Outside
     * an update call, this returns the most recent non-negative update dt.
     */
    float slayer3d_game_data_delta_time(const slayer3d_game_data_runtime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_DATA_RENDER_RUNTIME_H */

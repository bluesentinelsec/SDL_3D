/**
 * @file game_presentation.h
 * @brief Optional helpers for presenting JSON-authored game data.
 *
 * The game-data runtime owns authored state and descriptors. This module is a
 * thin reusable bridge that applies those descriptors to SLAYER3D renderer-facing
 * APIs: simple render primitives, UI text, and scene transition sequencing.
 *
 * Applications may use these helpers when the default behavior fits, or ignore
 * them and provide custom rendering/flow code while still using the same data.
 */

#ifndef SLAYER3D_GAME_PRESENTATION_H
#define SLAYER3D_GAME_PRESENTATION_H

#include <stdbool.h>

#include "slayer3d/asset.h"
#include "slayer3d/camera.h"
#include "slayer3d/effects.h"
#include "slayer3d/font.h"
#include "slayer3d/game.h"
#include "slayer3d/game_data.h"
#include "slayer3d/input.h"
#include "slayer3d/model.h"
#include "slayer3d/render_context.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/texture.h"
#include "slayer3d/transition.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Runtime cache for fonts referenced by authored UI text.
     *
     * The cache owns loaded slayer3d_font instances. It does not own the
     * runtime-provided font id strings.
     */
    typedef struct slayer3d_game_data_font_cache
    {
        slayer3d_font *fonts;  /**< Loaded font instances. */
        const char **font_ids; /**< Runtime-owned authored font ids. */
        int count;             /**< Number of cached fonts. */
        int capacity;          /**< Allocated cache slots. */
        const char *media_dir; /**< SLAYER3D media directory used for built-in fonts. */
    } slayer3d_game_data_font_cache;

    /** @brief One cached texture referenced by authored UI image data. */
    typedef struct slayer3d_game_data_image_cache_entry
    {
        slayer3d_texture2d texture;   /**< Loaded texture. */
        const char *image_id;         /**< Runtime-owned image asset id. */
        const char *effect;           /**< Optional sprite-backed effect id. */
        float effect_delay;           /**< Seconds to wait before effect starts. */
        float effect_duration;        /**< Seconds used to ramp the effect. */
        char *shader_vertex_source;   /**< Optional owned sprite shader vertex source. */
        char *shader_fragment_source; /**< Optional owned sprite shader fragment source. */
        bool loaded;                  /**< True once the texture owns valid pixels. */
    } slayer3d_game_data_image_cache_entry;

    /**
     * @brief Runtime cache for images referenced by authored UI data.
     *
     * The cache owns loaded textures. It reads image bytes through an asset
     * resolver supplied by the host, so the same authored paths work from a
     * source directory, pack file, or embedded pack.
     */
    typedef struct slayer3d_game_data_image_cache
    {
        slayer3d_game_data_image_cache_entry *entries; /**< Cached image entries. */
        int count;                                     /**< Number of cached images. */
        int capacity;                                  /**< Allocated cache slots. */
        slayer3d_asset_resolver *assets;               /**< Resolver used for lazy image loads; not owned. */
    } slayer3d_game_data_image_cache;

    /**
     * @brief Scene transition flow driven by authored scene transition data.
     *
     * The flow owns only transition state and the pending target scene name.
     * Scene data remains owned by slayer3d_game_data_runtime.
     */
    typedef struct slayer3d_game_data_scene_flow
    {
        slayer3d_transition transition; /**< Active enter/exit transition. */
        const char *pending_scene;      /**< Runtime-owned target scene name, or NULL. */
        bool fading_out;                /**< True while the current scene is exiting. */
        bool fading_in;                 /**< True while the target scene is entering. */
    } slayer3d_game_data_scene_flow;

    /**
     * @brief One cached authored particle emitter.
     */
    typedef struct slayer3d_game_data_particle_cache_entry
    {
        const char *entity_name;            /**< Runtime-owned entity name. */
        slayer3d_particle_emitter *emitter; /**< Owned particle emitter instance. */
        slayer3d_vec3 draw_emissive;        /**< Draw-time emissive color. */
        bool visible;                       /**< True when active in the current scene/frame. */
    } slayer3d_game_data_particle_cache_entry;

    /**
     * @brief Runtime cache for particle emitters referenced by authored data.
     *
     * The cache owns emitter instances. It does not own entity name strings.
     */
    typedef struct slayer3d_game_data_particle_cache
    {
        slayer3d_game_data_particle_cache_entry *entries; /**< Cached emitter entries. */
        int count;                                        /**< Number of cache entries. */
        int capacity;                                     /**< Allocated entry slots. */
    } slayer3d_game_data_particle_cache;

    /** @brief One cached sprite asset referenced by authored render.sprite data. */
    typedef struct slayer3d_game_data_sprite_cache_entry
    {
        const char *sprite_id;                /**< Runtime-owned sprite asset id. */
        slayer3d_sprite_asset_runtime sprite; /**< Loaded sprite runtime. */
        bool loaded;                          /**< True once the sprite owns loaded textures. */
    } slayer3d_game_data_sprite_cache_entry;

    /**
     * @brief Runtime cache for sprite assets referenced by authored world sprites.
     *
     * The cache owns loaded sprite textures and reads through the host asset
     * resolver, so directory, pack, and embedded launches use the same authored
     * `asset://` paths.
     */
    typedef struct slayer3d_game_data_sprite_cache
    {
        slayer3d_game_data_sprite_cache_entry *entries; /**< Cached sprite entries. */
        int count;                                      /**< Number of cached sprite assets. */
        int capacity;                                   /**< Allocated cache slots. */
        slayer3d_asset_resolver *assets;                /**< Resolver used for lazy loads; not owned. */
    } slayer3d_game_data_sprite_cache;

    /** @brief One cached model asset referenced by authored render.model data. */
    typedef struct slayer3d_game_data_model_cache_entry
    {
        const char *model_id; /**< Runtime-owned model asset id. */
        slayer3d_model model; /**< Loaded model runtime. */
        bool loaded;          /**< True once the model owns loaded meshes/materials. */
    } slayer3d_game_data_model_cache_entry;

    /**
     * @brief Runtime cache for model assets referenced by authored world models.
     *
     * The cache owns loaded model data. Current model loaders require a
     * filesystem path, so model assets must resolve through a directory mount.
     */
    typedef struct slayer3d_game_data_model_cache
    {
        slayer3d_game_data_model_cache_entry *entries; /**< Cached model entries. */
        int count;                                     /**< Number of cached models. */
        int capacity;                                  /**< Allocated cache slots. */
        slayer3d_asset_resolver *assets;               /**< Resolver used for lazy loads; not owned. */
    } slayer3d_game_data_model_cache;

    /** @brief One cached procedural mesh generated from an authored render.mesh_primitive descriptor. */
    typedef struct slayer3d_game_data_mesh_primitive_cache_entry
    {
        slayer3d_game_data_mesh_primitive_kind primitive; /**< Cached primitive kind. */
        slayer3d_vec3 size;                               /**< Cached authored size. */
        float radius;                                     /**< Cached primary radius. */
        float radius_top;                                 /**< Cached top radius for cones/cylinders. */
        float radius_bottom;                              /**< Cached bottom radius for cones/cylinders. */
        float height;                                     /**< Cached height. */
        float major_radius;                               /**< Cached torus/tube major radius. */
        float minor_radius;                               /**< Cached torus/tube minor radius. */
        float bevel_radius;                               /**< Cached rounded-box bevel radius. */
        float arc_angle;                                  /**< Cached tube arc angle. */
        int slices;                                       /**< Cached longitudinal segment count. */
        int rings;                                        /**< Cached ring/bevel segment count. */
        int tube_segments;                                /**< Cached tube segment count. */
        slayer3d_mesh mesh;                               /**< Owned immutable mesh arrays. */
        bool loaded;                                      /**< True once @ref mesh owns geometry. */
    } slayer3d_game_data_mesh_primitive_cache_entry;

    /**
     * @brief Runtime cache for static procedural meshes referenced by authored render data.
     *
     * The cache owns generated immutable vertex/index arrays. Hardware backends can
     * then cache GPU buffers for these stable meshes instead of rebuilding and
     * uploading procedural geometry every frame.
     */
    typedef struct slayer3d_game_data_mesh_primitive_cache
    {
        slayer3d_game_data_mesh_primitive_cache_entry *entries; /**< Cached generated meshes. */
        int count;                                              /**< Number of cache entries. */
        int capacity;                                           /**< Allocated cache slots. */
        int hits;                                               /**< Lookup hits, useful for tests/profiling. */
        int misses;                                             /**< Mesh builds, useful for tests/profiling. */
    } slayer3d_game_data_mesh_primitive_cache;

    /**
     * @brief Result produced by the generic authored menu controller.
     *
     * The menu controller owns only navigation and selected-item resolution.
     * Callers decide how to apply the selected command, such as emitting a
     * signal, requesting a scene transition, or quitting the app.
     */
    typedef struct slayer3d_game_data_menu_update_result
    {
        const char *menu;              /**< Runtime-owned active menu name, or NULL. */
        const char *scene;             /**< Runtime-owned selected target scene, or NULL. */
        const char *return_to;         /**< Runtime-owned scene to store as return target, or NULL. */
        const char *scene_state_key;   /**< Runtime-owned scene-state key to set, or NULL. */
        const char *scene_state_value; /**< Runtime-owned scene-state string value to assign, or NULL. */
        /** @brief Storage backing scene_state_value when it is produced from a dynamic row. */
        char scene_state_value_storage[SLAYER3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
        int selected_index;   /**< Selected item index after this update, or -1. */
        int signal_id;        /**< Selected item signal id, including data-bound controls, or -1. */
        int move_signal_id;   /**< Menu navigation signal id emitted by the host, or -1. */
        int select_signal_id; /**< Menu activation signal id emitted by the host, or -1. */
        slayer3d_game_data_menu_pause_command pause_command; /**< Pause command requested by selected item. */
        bool handled_input;                                  /**< True when a menu action was consumed. */
        bool selected;                                       /**< True when the selected item was activated. */
        bool quit;                                           /**< True when the selected item requests quit. */
        bool return_scene;                  /**< True when the selected item requests the stored return scene. */
        bool has_return_paused;             /**< True when selected item stores a return pause state. */
        bool return_paused;                 /**< Pause state to store for a later return_scene item. */
        bool control_changed;               /**< True when selecting the item changed a data-bound control. */
        bool input_binding_capture_started; /**< True when an input-binding item entered capture mode. */
        bool input_binding_changed;         /**< True when a captured input changed authored action bindings. */
        bool input_binding_conflict;        /**< True when a captured input was rejected as a duplicate binding. */
        bool text_entry_capture_started;    /**< True when a text item entered capture mode. */
        bool text_entry_changed;            /**< True when captured text edited its bound value. */
        bool text_entry_submitted;          /**< True when text capture was submitted. */
        bool text_entry_canceled;           /**< True when text capture was canceled and restored. */
    } slayer3d_game_data_menu_update_result;

    /**
     * @brief Reusable application flow for JSON-authored lifecycle controls.
     *
     * The flow owns app-level transition state, scene transition state, menu
     * input arming, and quit intent. It reads controls from slayer3d_game_data_runtime
     * and applies them to an slayer3d_game_context.
     */
    typedef struct slayer3d_game_data_app_flow
    {
        slayer3d_game_data_scene_flow scene_flow;   /**< Data-authored scene transition flow. */
        slayer3d_transition transition;             /**< App-level startup/quit transition. */
        slayer3d_game_data_app_control app;         /**< Resolved app controls from game data. */
        bool quit_pending;                          /**< True after quit has been requested. */
        bool scene_input_armed;                     /**< True once menu input is idle after scene entry. */
        const char *skip_scene;                     /**< Active scene tracked for pending skip input. */
        bool skip_requested;                        /**< True once input asks the active scene to skip. */
        slayer3d_game_data_timeline_state timeline; /**< Runtime state for the active scene's authored timeline. */
    } slayer3d_game_data_app_flow;

    /**
     * @brief Reusable presentation and update clocks for data-authored games.
     *
     * Hosts keep one instance for the lifetime of a game. The state samples FPS
     * and frame counters, tracks real presentation time, evaluates authored
     * presentation clocks, and exposes ready-to-pass UI metrics/render inputs.
     */
    typedef struct slayer3d_game_data_frame_state
    {
        slayer3d_game_data_ui_metrics metrics;      /**< Latest UI metrics. */
        slayer3d_game_data_render_eval render_eval; /**< Latest render effect inputs. */
        float time;                                 /**< Presentation time in seconds. */
        float ui_pulse_phase;                       /**< Normalized phase for pulse_alpha UI. */
        float last_render_time;                     /**< Last sampled real render time. */
        float fps_sample_time;                      /**< Accumulated FPS sample time. */
        float displayed_fps;                        /**< Most recently sampled FPS. */
        int fps_sample_frames;                      /**< Frames accumulated in current FPS sample. */
        Uint64 rendered_frames;                     /**< Number of rendered frames. */
        bool was_paused;                            /**< Pause state from the previous update. */
    } slayer3d_game_data_frame_state;

    /**
     * @brief Inputs for the generic data-authored update-frame helper.
     */
    typedef struct slayer3d_game_data_update_frame_desc
    {
        slayer3d_game_context *ctx;                        /**< Managed game context. */
        slayer3d_game_data_runtime *runtime;               /**< Authored runtime to update. */
        slayer3d_game_data_app_flow *app_flow;             /**< Optional app flow to update. */
        slayer3d_game_data_particle_cache *particle_cache; /**< Optional authored particle cache. */
        float dt;                                          /**< Real or fixed delta time for this update. */
    } slayer3d_game_data_update_frame_desc;

    typedef struct slayer3d_game_data_frame_desc slayer3d_game_data_frame_desc;

    /**
     * @brief Optional callback invoked by slayer3d_game_data_draw_frame().
     *
     * Return false to report a draw failure while still allowing the frame
     * helper to finish restoring renderer state.
     */
    typedef bool (*slayer3d_game_data_frame_hook)(void *userdata, const slayer3d_game_data_frame_desc *frame);

    /**
     * @brief Descriptor for drawing one data-authored presentation frame.
     *
     * The helper applies authored render settings, configures lights, clears
     * the frame, draws active-scene world primitives/particles when enabled,
     * draws authored UI text, then draws active app/scene transitions. Hooks
     * are optional extension points for game-specific rendering.
     */
    struct slayer3d_game_data_frame_desc
    {
        const slayer3d_game_data_runtime *runtime;         /**< Authored runtime to render. */
        slayer3d_render_context *renderer;                 /**< Render context receiving draw calls. */
        slayer3d_game_data_font_cache *font_cache;         /**< Font cache used by authored UI text. */
        slayer3d_game_data_image_cache *image_cache;       /**< Image cache used by authored UI images. */
        slayer3d_game_data_particle_cache *particle_cache; /**< Particle cache used by authored emitters. */
        slayer3d_game_data_sprite_cache *sprite_cache;     /**< Sprite cache used by authored render.sprite data. */
        slayer3d_game_data_model_cache *model_cache;       /**< Model cache used by authored render.model data. */
        slayer3d_game_data_mesh_primitive_cache *mesh_primitive_cache; /**< Cache for authored procedural meshes. */
        const slayer3d_game_data_app_flow *app_flow;       /**< Optional app flow whose transitions are drawn. */
        const slayer3d_game_data_ui_metrics *metrics;      /**< Optional UI metrics. */
        const slayer3d_game_data_render_eval *render_eval; /**< Optional primitive effect evaluation inputs. */
        const slayer3d_camera3d *fallback_camera;          /**< Optional camera used when no active camera resolves. */
        float pulse_phase;                                 /**< Normalized UI pulse phase. */
        slayer3d_game_data_frame_hook before_world_3d;     /**< Optional hook inside the 3D pass before data draws. */
        slayer3d_game_data_frame_hook after_world_3d;      /**< Optional hook inside the 3D pass after data draws. */
        slayer3d_game_data_frame_hook before_ui;           /**< Optional hook before authored UI and transitions. */
        slayer3d_game_data_frame_hook after_ui;            /**< Optional hook after authored UI and transitions. */
        void *userdata;                                    /**< User pointer passed to hooks. */
    };

    /**
     * @brief Initialize a font cache.
     *
     * @param cache     Cache to initialize.
     * @param media_dir Directory containing SLAYER3D built-in media, usually SLAYER3D_MEDIA_DIR.
     */
    void slayer3d_game_data_font_cache_init(slayer3d_game_data_font_cache *cache, const char *media_dir);

    /**
     * @brief Free all fonts owned by a cache.
     *
     * Safe to call with NULL or an already-freed cache.
     */
    void slayer3d_game_data_font_cache_free(slayer3d_game_data_font_cache *cache);

    /**
     * @brief Initialize a UI image cache.
     *
     * @param cache Cache to initialize.
     * @param assets Asset resolver used to read authored image paths; not owned.
     */
    void slayer3d_game_data_image_cache_init(slayer3d_game_data_image_cache *cache, slayer3d_asset_resolver *assets);

    /**
     * @brief Free all textures owned by an image cache.
     *
     * Safe to call with NULL or an already-freed cache.
     */
    void slayer3d_game_data_image_cache_free(slayer3d_game_data_image_cache *cache);

    /**
     * @brief Draw authored render primitives for the active scene.
     *
     * This renders currently supported primitive components (`render.cube`,
     * `render.sphere`, `render.mesh_primitive`, `render.sprite`, and
     * `render.model`) using SLAYER3D's immediate drawing helpers. Call inside an
     * active 3D pass.
     */
    bool slayer3d_game_data_draw_render_primitives(const slayer3d_game_data_runtime *runtime,
                                                   slayer3d_render_context *renderer);

    /**
     * @brief Draw authored render primitives with evaluated time-based effects.
     *
     * @see slayer3d_game_data_draw_render_primitives
     */
    bool slayer3d_game_data_draw_render_primitives_evaluated(const slayer3d_game_data_runtime *runtime,
                                                             slayer3d_render_context *renderer,
                                                             const slayer3d_game_data_render_eval *eval);

    /**
     * @brief Draw editor/debug world-model overlay primitives.
     *
     * Draws the renderer-agnostic line primitives emitted by
     * @ref slayer3d_game_data_for_each_editor_debug_primitive. Call inside an
     * active 3D pass after the scene camera is configured.
     */
    bool slayer3d_game_data_draw_editor_debug_primitives(const slayer3d_game_data_runtime *runtime,
                                                         slayer3d_render_context *renderer,
                                                         const slayer3d_game_data_editor_debug_desc *desc);

    /**
     * @brief Draw active-scene data-authored editor/debug primitives.
     *
     * Scenes opt into this through their `editor.debug_overlay` block. The
     * helper remains game-agnostic and delegates primitive generation to
     * @ref slayer3d_game_data_for_each_active_editor_debug_primitive. Call
     * inside an active 3D pass after the scene camera is configured.
     */
    bool slayer3d_game_data_draw_active_editor_debug_primitives(const slayer3d_game_data_runtime *runtime,
                                                                slayer3d_render_context *renderer);

    /**
     * @brief Initialize a world sprite asset cache.
     *
     * @param cache Cache to initialize.
     * @param assets Asset resolver used to read authored sprite paths; not owned.
     */
    void slayer3d_game_data_sprite_cache_init(slayer3d_game_data_sprite_cache *cache, slayer3d_asset_resolver *assets);

    /**
     * @brief Free all sprite assets owned by a world sprite cache.
     */
    void slayer3d_game_data_sprite_cache_free(slayer3d_game_data_sprite_cache *cache);

    /**
     * @brief Initialize a world model asset cache.
     *
     * @param cache Cache to initialize.
     * @param assets Asset resolver used to resolve authored model paths; not owned.
     */
    void slayer3d_game_data_model_cache_init(slayer3d_game_data_model_cache *cache, slayer3d_asset_resolver *assets);

    /**
     * @brief Free all models owned by a world model cache.
     */
    void slayer3d_game_data_model_cache_free(slayer3d_game_data_model_cache *cache);

    /** @brief Initialize a procedural mesh primitive cache. */
    void slayer3d_game_data_mesh_primitive_cache_init(slayer3d_game_data_mesh_primitive_cache *cache);

    /** @brief Free all generated mesh primitive geometry owned by @p cache. */
    void slayer3d_game_data_mesh_primitive_cache_free(slayer3d_game_data_mesh_primitive_cache *cache);

    /**
     * @brief Draw active-scene authored sector levels.
     *
     * Scene files declare sector level instances under `world.sector_levels`.
     * Call inside an active 3D pass. When an instance enables portal culling,
     * this helper computes visibility from @p camera before drawing.
     */
    bool slayer3d_game_data_draw_sector_levels(const slayer3d_game_data_runtime *runtime,
                                               slayer3d_render_context *renderer, const slayer3d_camera3d *camera);

    /**
     * @brief Draw active-scene sector levels and resolve `asset://` material
     * textures through an asset resolver.
     *
     * This variant is intended for the generic data runner and pack/embedded
     * games. Passing NULL for @p assets preserves the filesystem-only behavior
     * of slayer3d_game_data_draw_sector_levels().
     */
    bool slayer3d_game_data_draw_sector_levels_with_assets(const slayer3d_game_data_runtime *runtime,
                                                           slayer3d_render_context *renderer,
                                                           const slayer3d_asset_resolver *assets,
                                                           const slayer3d_camera3d *camera);

    /**
     * @brief Draw active-scene authored brush worlds.
     *
     * Scene files declare brush world instances under `world.brush_worlds`.
     * Call inside an active 3D pass. Brush worlds are compiled to immutable
     * static meshes at game-data load time and are affected by the current
     * renderer lighting state.
     */
    bool slayer3d_game_data_draw_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                              slayer3d_render_context *renderer);

    /**
     * @brief Draw active-scene brush worlds and resolve `asset://` material
     * textures through an asset resolver.
     */
    bool slayer3d_game_data_draw_brush_worlds_with_assets(const slayer3d_game_data_runtime *runtime,
                                                          slayer3d_render_context *renderer,
                                                          const slayer3d_asset_resolver *assets);

    /**
     * @brief Draw authored UI text for the active scene.
     *
     * Built-in font assets are loaded on demand through @p font_cache. Text is
     * drawn on SLAYER3D's overlay path, after world rendering. @p pulse_phase is a
     * normalized phase used by UI items with `pulse_alpha`.
     */
    bool slayer3d_game_data_draw_ui_text(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                         slayer3d_game_data_font_cache *font_cache,
                                         const slayer3d_game_data_ui_metrics *metrics, float pulse_phase);

    /**
     * @brief Draw authored UI images for the active scene.
     *
     * Images are loaded lazily through @p image_cache and drawn on SLAYER3D's
     * overlay path after world rendering. @p render_eval supplies the current
     * presentation time for authored image effects such as `melt`.
     */
    bool slayer3d_game_data_draw_ui_images(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                           slayer3d_game_data_image_cache *image_cache,
                                           const slayer3d_game_data_ui_metrics *metrics,
                                           const slayer3d_game_data_render_eval *render_eval);

    /**
     * @brief Draw authored UI rectangles for the active scene.
     *
     * Rectangles are drawn on SLAYER3D's overlay path after world rendering.
     * @p render_eval supplies the current presentation time for pulse effects.
     */
    bool slayer3d_game_data_draw_ui_rects(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                          const slayer3d_game_data_ui_metrics *metrics,
                                          const slayer3d_game_data_render_eval *render_eval);

    /**
     * @brief Initialize a particle emitter cache.
     */
    void slayer3d_game_data_particle_cache_init(slayer3d_game_data_particle_cache *cache);

    /**
     * @brief Free all particle emitters owned by a cache.
     */
    void slayer3d_game_data_particle_cache_free(slayer3d_game_data_particle_cache *cache);

    /**
     * @brief Advance active authored particle emitters.
     *
     * Creates emitters lazily for active scene entities with `particles.emitter`
     * components, updates their authored positions, and advances particle
     * simulation by @p dt.
     */
    bool slayer3d_game_data_update_particles(const slayer3d_game_data_runtime *runtime,
                                             slayer3d_game_data_particle_cache *cache, float dt);

    /**
     * @brief Draw active authored particle emitters.
     *
     * Call inside an active 3D pass.
     */
    bool slayer3d_game_data_draw_particles(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                           slayer3d_game_data_particle_cache *cache);

    /**
     * @brief Consume authored active-menu input and resolve selected commands.
     *
     * @p input_armed should be persisted by the caller for the active scene.
     * The controller waits until all menu navigation actions are idle before
     * accepting new presses, preventing a held key from immediately selecting
     * the next scene's default menu item.
     */
    bool slayer3d_game_data_update_menus(slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                         bool *input_armed, slayer3d_game_data_menu_update_result *out_result);

    /**
     * @brief Update the active authored menu using current frame metrics.
     *
     * Use this when menu `active_if` conditions depend on metric-backed state
     * such as app pause status. The basic slayer3d_game_data_update_menus()
     * wrapper evaluates those metric-backed conditions with NULL metrics.
     */
    bool slayer3d_game_data_update_menus_for_metrics(slayer3d_game_data_runtime *runtime,
                                                     const slayer3d_input_manager *input, bool *input_armed,
                                                     const slayer3d_game_data_ui_metrics *metrics,
                                                     slayer3d_game_data_menu_update_result *out_result);

    /**
     * @brief Initialize reusable frame/update state.
     */
    void slayer3d_game_data_frame_state_init(slayer3d_game_data_frame_state *state);

    /**
     * @brief Advance authored app flow, update phases, presentation clocks, and simulation.
     *
     * This helper is intended for thin managed-loop hosts. It uses
     * data-authored update phase policy to decide whether to update app flow,
     * property effects, particles, and simulation while paused or unpaused.
     */
    bool slayer3d_game_data_update_frame(slayer3d_game_data_frame_state *state,
                                         const slayer3d_game_data_update_frame_desc *desc);

    /**
     * @brief Sample render metrics before drawing a frame.
     */
    void slayer3d_game_data_frame_state_record_render(slayer3d_game_data_frame_state *state,
                                                      const slayer3d_game_context *ctx,
                                                      const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Initialize a scene transition flow.
     */
    void slayer3d_game_data_scene_flow_init(slayer3d_game_data_scene_flow *flow);

    /**
     * @brief Return whether a scene transition is currently active.
     */
    bool slayer3d_game_data_scene_flow_is_transitioning(const slayer3d_game_data_scene_flow *flow);

    /**
     * @brief Request a transition to another authored scene.
     *
     * If the target is already active, unknown, or another transition is in
     * progress, the request is rejected and returns false. Otherwise the flow
     * starts the active scene's authored `exit` transition when one exists, or
     * switches scenes on the next update.
     */
    bool slayer3d_game_data_scene_flow_request(slayer3d_game_data_scene_flow *flow, slayer3d_game_data_runtime *runtime,
                                               const char *scene_name);

    /**
     * @brief Advance a scene transition flow.
     *
     * When the exit transition finishes, the flow activates the pending scene
     * through slayer3d_game_data_set_active_scene(), then starts that scene's
     * authored `enter` transition when one exists.
     */
    void slayer3d_game_data_scene_flow_update(slayer3d_game_data_scene_flow *flow, slayer3d_game_data_runtime *runtime,
                                              slayer3d_signal_bus *bus, float dt);

    /**
     * @brief Draw the active transition owned by a scene flow.
     */
    void slayer3d_game_data_scene_flow_draw(const slayer3d_game_data_scene_flow *flow,
                                            slayer3d_render_context *renderer);

    /**
     * @brief Initialize reusable app lifecycle flow state.
     */
    void slayer3d_game_data_app_flow_init(slayer3d_game_data_app_flow *flow);

    /**
     * @brief Start app lifecycle flow from authored data.
     *
     * Reads app controls and starts the authored startup transition when present.
     */
    bool slayer3d_game_data_app_flow_start(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime);

    /**
     * @brief Return true when quit has been requested and is waiting on a transition.
     */
    bool slayer3d_game_data_app_flow_quit_pending(const slayer3d_game_data_app_flow *flow);

    /**
     * @brief Return true while any app or scene transition is active.
     */
    bool slayer3d_game_data_app_flow_is_transitioning(const slayer3d_game_data_app_flow *flow);

    /**
     * @brief Consume authored app/menu/scene input and advance transitions.
     *
     * The function updates ctx->paused and ctx->quit_requested when authored
     * controls request those state changes. If `app.pause.allowed_if` is
     * authored, it is evaluated before entering pause; unpausing remains
     * available so games cannot trap the app in a paused state.
     */
    bool slayer3d_game_data_app_flow_update(slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                            slayer3d_game_data_runtime *runtime, float dt);

    /**
     * @brief Draw active app-level and scene-level transitions.
     */
    void slayer3d_game_data_app_flow_draw(const slayer3d_game_data_app_flow *flow, slayer3d_render_context *renderer);

    /**
     * @brief Draw one complete data-authored presentation frame.
     *
     * This is the high-level version of the lower-level draw helpers above.
     * Hosts that only need authored presentation can call this from their
     * render callback and reserve hooks for custom rendering.
     */
    bool slayer3d_game_data_draw_frame(const slayer3d_game_data_frame_desc *frame);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_PRESENTATION_H */

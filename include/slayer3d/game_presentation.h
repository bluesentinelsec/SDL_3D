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

#include <SDL3/SDL_stdinc.h>

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
        char *image_id;               /**< Owned image asset id. */
        char *source_path;            /**< Owned direct path or sprite id used to load this image. */
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

    /** @brief Asset kinds that can be requested through the presentation warmup queue. */
    typedef enum slayer3d_game_data_asset_warmup_kind
    {
        /** @brief Authored UI/image asset id. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE = 1,
        /** @brief Texture path resolved relative to an optional source path. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_TEXTURE = 2,
        /** @brief Authored sprite asset id. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_SPRITE = 3,
        /** @brief Authored model asset id. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_MODEL = 4,
        /** @brief Authored or filesystem audio file path. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_AUDIO_FILE = 5,
        /** @brief Authored font asset id. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_FONT = 6,
        /** @brief Procedural mesh generated from a render.mesh_primitive descriptor. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_MESH_PRIMITIVE = 7,
    } slayer3d_game_data_asset_warmup_kind;

    /** @brief Current state of one presentation asset warmup request. */
    typedef enum slayer3d_game_data_asset_warmup_state
    {
        /** @brief Request is queued and has not been serviced yet. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED = 0,
        /** @brief Request is being prepared by the warmup service or a worker. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING = 1,
        /** @brief Request was prepared off-thread and is waiting for main-thread finalization. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_READY_FOR_FINALIZE = 2,
        /** @brief Request was serviced successfully. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_READY = 3,
        /** @brief Request was serviced and failed. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_FAILED = 4,
        /** @brief Request was deprioritized before completion. */
        SLAYER3D_GAME_DATA_ASSET_WARMUP_CANCELED = 5,
    } slayer3d_game_data_asset_warmup_state;

    /** @brief One queued presentation asset warmup request. */
    typedef struct slayer3d_game_data_asset_warmup_entry
    {
        slayer3d_game_data_asset_warmup_kind kind;   /**< Asset kind. */
        slayer3d_game_data_asset_warmup_state state; /**< Current request state. */
        char *source_path;                           /**< Optional owned source path for relative texture requests. */
        char *id;                                    /**< Owned asset id or texture path. */
        void *prepared;                              /**< Private prepared payload for main-thread finalization. */
        unsigned int generation;                     /**< Monotonic token used to ignore stale worker completions. */
        slayer3d_game_data_render_primitive mesh_primitive; /**< Private descriptor for mesh primitive requests. */
    } slayer3d_game_data_asset_warmup_entry;

    /**
     * @brief Budgeted queue for presentation asset warmup.
     *
     * Requests are deduplicated by kind/source/id. Workers may prepare CPU-only
     * texture data, while renderer-owned cache insertion remains on the main
     * presentation path. Platforms without worker support continue to service
     * requests incrementally on the render thread.
     */
    typedef struct slayer3d_game_data_asset_warmup_queue
    {
        slayer3d_game_data_asset_warmup_entry *entries; /**< Owned request entries. */
        int count;                                      /**< Number of requests. */
        int capacity;                                   /**< Allocated request slots. */
        char *requested_scene;                          /**< Owned active scene most recently enumerated. */
        int max_jobs_per_frame;                         /**< Default service budget; <= 0 uses one job. */
        void *worker_state;                             /**< Private worker-thread state. */
        int service_calls;                              /**< Diagnostic count of warmup service drain calls. */
        int service_jobs;                               /**< Diagnostic count of warmup jobs completed by service. */
        float service_last_ms;                          /**< Diagnostic duration of the most recent service call. */
        float service_total_ms;                         /**< Diagnostic cumulative service time. */
        float service_max_ms;                           /**< Diagnostic maximum single service duration. */
        Uint64 first_request_counter;                   /**< Diagnostic timestamp for first request. */
        Uint64 last_activity_counter;                   /**< Diagnostic timestamp for latest state change. */
    } slayer3d_game_data_asset_warmup_queue;

    /** @brief Snapshot counts for a presentation asset warmup queue. */
    typedef struct slayer3d_game_data_asset_warmup_stats
    {
        int queued;             /**< Requests waiting to be serviced. */
        int loading;            /**< Requests currently being prepared or finalized. */
        int ready_for_finalize; /**< Requests prepared off-thread and waiting for main-thread finalization. */
        int pending;            /**< Requests not yet completed: queued + loading + ready_for_finalize. */
        int ready;              /**< Requests serviced successfully. */
        int failed;             /**< Requests serviced unsuccessfully. */
        int canceled;           /**< Requests deprioritized before completion. */
        int completed;          /**< Requests no longer pending: ready + failed + canceled. */
        int total;              /**< Total requests tracked by the queue. */
        int worker_threads;     /**< Active worker threads preparing CPU-side asset payloads. */
        int service_calls;      /**< Warmup service drain calls. */
        int service_jobs;       /**< Jobs completed by warmup service calls. */
        float progress;         /**< Completed/total in [0, 1], or 1 when total is zero. */
        float elapsed_ms;       /**< Milliseconds since first request until completion or now. */
        float service_last_ms;  /**< Milliseconds spent in the most recent service call. */
        float service_total_ms; /**< Cumulative milliseconds spent in warmup service calls. */
        float service_max_ms;   /**< Maximum milliseconds spent in one service call. */
    } slayer3d_game_data_asset_warmup_stats;

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
        bool view_space;                    /**< True when drawn with the viewmodel camera pass. */
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
        const char *sprite_id;                /**< Cache-owned sprite asset id. */
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
        const char *model_id; /**< Cache-owned model asset id. */
        slayer3d_model model; /**< Loaded model runtime. */
        bool loaded;          /**< True once the model owns loaded meshes/materials. */
    } slayer3d_game_data_model_cache_entry;

    /** @brief One frame-local cached skeletal pose for an authored render.model draw. */
    typedef struct slayer3d_game_data_model_pose_cache_entry
    {
        const slayer3d_model *model;   /**< Model that owns the skeleton/clip. */
        int animation_clip;            /**< Animation clip index on model. */
        float animation_time;          /**< Normalized animation time used for evaluation. */
        slayer3d_mat4 *joint_matrices; /**< Owned matrix palette reused across frames. */
        int joint_count;               /**< Number of valid joint matrices. */
        int joint_capacity;            /**< Allocated matrix capacity. */
    } slayer3d_game_data_model_pose_cache_entry;

    /**
     * @brief Runtime cache for model assets referenced by authored world models.
     *
     * The cache owns loaded model data. Current model loaders require a
     * filesystem path, so model assets must resolve through a directory mount.
     * It also owns a frame-local skeletal pose cache used by presentation
     * helpers to avoid re-evaluating identical model/clip/time poses.
     */
    typedef struct slayer3d_game_data_model_cache
    {
        slayer3d_game_data_model_cache_entry *entries;           /**< Cached model entries. */
        int count;                                               /**< Number of cached models. */
        int capacity;                                            /**< Allocated cache slots. */
        slayer3d_asset_resolver *assets;                         /**< Resolver used for lazy loads; not owned. */
        slayer3d_game_data_model_pose_cache_entry *pose_entries; /**< Frame-local cached skeletal poses. */
        int pose_count;                                          /**< Number of poses used this frame. */
        int pose_capacity;                                       /**< Allocated pose cache slots. */
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
        slayer3d_game_data_ui_metrics metrics;       /**< Latest UI metrics. */
        slayer3d_game_data_render_eval render_eval;  /**< Latest render effect inputs. */
        float time;                                  /**< Presentation time in seconds. */
        float ui_pulse_phase;                        /**< Normalized phase for pulse_alpha UI. */
        float last_render_time;                      /**< Last sampled real render time. */
        float fps_sample_time;                       /**< Accumulated FPS sample time. */
        float displayed_fps;                         /**< Most recently sampled FPS. */
        float frame_ms_sample_sum;                   /**< Accumulated wall-clock frame milliseconds. */
        float update_cpu_ms_sample_sum;              /**< Accumulated managed update CPU milliseconds. */
        float render_cpu_ms_sample_sum;              /**< Accumulated managed render CPU milliseconds. */
        float render_mesh_submissions_sample_sum;    /**< Accumulated render mesh submission deltas. */
        float render_mesh_draws_sample_sum;          /**< Accumulated render mesh draw deltas. */
        float render_triangles_sample_sum;           /**< Accumulated render triangle deltas. */
        float geometry_draw_calls_sample_sum;        /**< Accumulated backend geometry draw-call deltas. */
        float static_mesh_instanced_draw_sample_sum; /**< Accumulated static mesh instanced draw-call deltas. */
        float static_mesh_instances_batched_sum;     /**< Accumulated static mesh instance batching deltas. */
        float static_mesh_draw_calls_saved_sum;      /**< Accumulated static mesh saved draw-call deltas. */
        float procedural_lod_candidates_sample_sum;  /**< Accumulated procedural LOD candidate deltas. */
        float procedural_lod_reduced_sample_sum;     /**< Accumulated reduced procedural LOD deltas. */
        float procedural_lod_authored_triangles_sum; /**< Accumulated authored procedural LOD triangle deltas. */
        float procedural_lod_resolved_triangles_sum; /**< Accumulated resolved procedural LOD triangle deltas. */
        float procedural_lod_triangles_saved_sum;    /**< Accumulated procedural LOD saved triangle deltas. */
        float model_lod_candidates_sample_sum;       /**< Accumulated model LOD candidate deltas. */
        float model_lod_culled_sample_sum;           /**< Accumulated model LOD cull deltas. */
        float model_lod_triangles_saved_sum;         /**< Accumulated model LOD saved triangle deltas. */
        float depth_prepass_draws_sample_sum;        /**< Accumulated depth-prepass draw deltas. */
        float depth_prepass_triangles_sample_sum;    /**< Accumulated depth-prepass triangle deltas. */
        float depth_prepass_samples_sample_sum;      /**< Accumulated depth-prepass sample-query deltas. */
        float geometry_samples_sample_sum;           /**< Accumulated main geometry sample-query deltas. */
        float light_candidates_sample_sum;           /**< Accumulated light-candidate deltas. */
        float lights_selected_sample_sum;            /**< Accumulated selected-light deltas. */
        float light_selection_draws_sample_sum;      /**< Accumulated lit draw light-selection deltas. */
        slayer3d_render_stats last_render_stats;     /**< Previous cumulative render stats sample. */
        int fps_sample_frames;                       /**< Frames accumulated in current FPS sample. */
        Uint64 rendered_frames;                      /**< Number of rendered frames. */
        bool have_last_render_stats;                 /**< True once last_render_stats has been initialized. */
        bool was_paused;                             /**< Pause state from the previous update. */
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
        slayer3d_game_data_asset_warmup_queue *asset_warmup; /**< Optional budgeted presentation asset warmup queue. */
        const slayer3d_game_data_app_flow *app_flow;         /**< Optional app flow whose transitions are drawn. */
        const slayer3d_game_data_ui_metrics *metrics;        /**< Optional UI metrics. */
        const slayer3d_game_data_render_eval *render_eval;   /**< Optional primitive effect evaluation inputs. */
        const slayer3d_camera3d *fallback_camera;      /**< Optional camera used when no active camera resolves. */
        float pulse_phase;                             /**< Normalized UI pulse phase. */
        slayer3d_game_data_frame_hook before_world_3d; /**< Optional hook inside the 3D pass before data draws. */
        slayer3d_game_data_frame_hook after_world_3d;  /**< Optional hook inside the 3D pass after data draws. */
        slayer3d_game_data_frame_hook before_ui;       /**< Optional hook before authored UI and transitions. */
        slayer3d_game_data_frame_hook after_ui;        /**< Optional hook after authored UI and transitions. */
        void *userdata;                                /**< User pointer passed to hooks. */
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

    /** @brief Initialize a presentation asset warmup queue. */
    void slayer3d_game_data_asset_warmup_queue_init(slayer3d_game_data_asset_warmup_queue *queue,
                                                    int max_jobs_per_frame);

    /** @brief Free all entries owned by a presentation asset warmup queue. */
    void slayer3d_game_data_asset_warmup_queue_free(slayer3d_game_data_asset_warmup_queue *queue);

    /**
     * @brief Start background CPU warmup workers for CPU-safe asset requests when supported.
     *
     * The queue falls back to synchronous budgeted service when this returns
     * false. @p runtime may be NULL for texture-only warmup; direct UI image,
     * sprite, and model warmup require it. @p runtime and @p assets are only
     * read by workers; callers must keep them alive until the queue is stopped
     * or freed.
     */
    bool slayer3d_game_data_asset_warmup_queue_start_workers(slayer3d_game_data_asset_warmup_queue *queue,
                                                             const slayer3d_game_data_runtime *runtime,
                                                             slayer3d_asset_resolver *assets, int worker_count);

    /** @brief Stop and join any background warmup workers owned by the queue. */
    void slayer3d_game_data_asset_warmup_queue_stop_workers(slayer3d_game_data_asset_warmup_queue *queue);

    /**
     * @brief Cancel queued or in-flight warmup requests that have not completed.
     *
     * Ready and failed entries remain available for diagnostics and
     * deduplication. A later matching request revives a canceled entry and
     * queues it again.
     *
     * @return Number of entries moved to the canceled state.
     */
    int slayer3d_game_data_asset_warmup_queue_cancel_pending(slayer3d_game_data_asset_warmup_queue *queue);

    /** @brief Queue a UI/image asset id for warmup, deduplicating existing requests. */
    bool slayer3d_game_data_asset_warmup_request_ui_image(slayer3d_game_data_asset_warmup_queue *queue,
                                                          const char *image_id);

    /**
     * @brief Queue a UI/image asset using a caller-resolved source path.
     *
     * This is useful for image ids whose backing file can change at runtime.
     * The source path participates in deduplication so a stable image id can be
     * reloaded when it points at a different file.
     */
    bool slayer3d_game_data_asset_warmup_request_ui_image_source(slayer3d_game_data_asset_warmup_queue *queue,
                                                                 const char *source_path, const char *image_id);

    /** @brief Queue a font asset id for warmup, deduplicating existing requests. */
    bool slayer3d_game_data_asset_warmup_request_font(slayer3d_game_data_asset_warmup_queue *queue,
                                                      const char *font_id);

    /** @brief Queue a texture path for warmup, deduplicating existing requests. */
    bool slayer3d_game_data_asset_warmup_request_texture(slayer3d_game_data_asset_warmup_queue *queue,
                                                         const char *source_path, const char *texture_path);

    /** @brief Queue a sprite asset id for warmup, deduplicating existing requests. */
    bool slayer3d_game_data_asset_warmup_request_sprite(slayer3d_game_data_asset_warmup_queue *queue,
                                                        const char *sprite_id);

    /** @brief Queue a model asset id for warmup, deduplicating existing requests. */
    bool slayer3d_game_data_asset_warmup_request_model(slayer3d_game_data_asset_warmup_queue *queue,
                                                       const char *model_id);

    /** @brief Queue an audio file path for warmup, deduplicating existing requests. */
    bool slayer3d_game_data_asset_warmup_request_audio_file(slayer3d_game_data_asset_warmup_queue *queue,
                                                            const char *audio_path);

    /** @brief Queue a cacheable procedural mesh primitive for warmup, deduplicating existing requests. */
    bool slayer3d_game_data_asset_warmup_request_mesh_primitive(slayer3d_game_data_asset_warmup_queue *queue,
                                                                const slayer3d_game_data_render_primitive *primitive);

    /** @brief Read queue counts by state. */
    void slayer3d_game_data_asset_warmup_queue_stats(const slayer3d_game_data_asset_warmup_queue *queue,
                                                     slayer3d_game_data_asset_warmup_stats *out_stats);

    /**
     * @brief Query one queued warmup request state.
     *
     * @return true when a matching request exists and @p out_state was written.
     */
    bool slayer3d_game_data_asset_warmup_request_state(const slayer3d_game_data_asset_warmup_queue *queue,
                                                       slayer3d_game_data_asset_warmup_kind kind,
                                                       const char *source_path, const char *id,
                                                       slayer3d_game_data_asset_warmup_state *out_state);

    /**
     * @brief Service queued warmup requests within a bounded job budget.
     *
     * Passing @p max_jobs <= 0 uses queue->max_jobs_per_frame, or one job when
     * the queue does not define a positive default. The return value is the
     * number of queued requests serviced during this call.
     */
    int slayer3d_game_data_asset_warmup_queue_service(
        slayer3d_game_data_asset_warmup_queue *queue, const slayer3d_game_data_runtime *runtime,
        slayer3d_render_context *renderer, slayer3d_game_data_font_cache *font_cache,
        slayer3d_game_data_image_cache *image_cache, slayer3d_game_data_sprite_cache *sprite_cache,
        slayer3d_game_data_model_cache *model_cache, slayer3d_game_data_mesh_primitive_cache *mesh_primitive_cache,
        slayer3d_asset_resolver *assets, int max_jobs);

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
     * @brief Clear frame-local skeletal poses while keeping allocated matrix storage.
     *
     * Call once at the beginning of a presented frame before evaluating
     * render.model animation poses. slayer3d_game_data_draw_frame() does this
     * automatically for its model cache.
     */
    void slayer3d_game_data_model_cache_begin_pose_frame(slayer3d_game_data_model_cache *cache);

    /**
     * @brief Evaluate or reuse a cached model animation pose.
     *
     * The returned matrix palette is owned by @p cache and remains valid until
     * the next slayer3d_game_data_model_cache_begin_pose_frame() or
     * slayer3d_game_data_model_cache_free(). @p animation_time is wrapped by
     * clip duration when @p loop is true.
     */
    const slayer3d_mat4 *slayer3d_game_data_model_cache_evaluate_pose(slayer3d_game_data_model_cache *cache,
                                                                      slayer3d_render_context *renderer,
                                                                      const slayer3d_model *model, int animation_clip,
                                                                      float animation_time, bool loop,
                                                                      int *out_joint_count);

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
     * @brief Draw active-scene brush worlds with optional camera visibility culling.
     *
     * Passing NULL for @p camera preserves the baseline whole-world static mesh
     * path. Scene instances that opt into `visibility_occlusion` use @p camera
     * to flood-fill the brush world's compiled empty-space visibility grid and
     * conservatively skip brush submodels with no neighboring visible cell
     * before renderer submission.
     */
    bool slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(const slayer3d_game_data_runtime *runtime,
                                                                     slayer3d_render_context *renderer,
                                                                     const slayer3d_asset_resolver *assets,
                                                                     const slayer3d_camera3d *camera);

    /**
     * @brief Draw authored UI text for the active scene.
     *
     * Built-in font assets are loaded through @p font_cache. Text is drawn on
     * SLAYER3D's overlay path, after world rendering. When @p asset_warmup has a
     * matching pending or failed font request, matching text is skipped so the
     * draw path does not synchronously load a font that is already queued for
     * warmup. @p pulse_phase is a normalized phase used by UI items with
     * `pulse_alpha`.
     */
    bool slayer3d_game_data_draw_ui_text(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                         slayer3d_game_data_font_cache *font_cache,
                                         const slayer3d_game_data_asset_warmup_queue *asset_warmup,
                                         const slayer3d_game_data_ui_metrics *metrics, float pulse_phase);

    /**
     * @brief Draw authored UI images for the active scene.
     *
     * Images are loaded lazily through @p image_cache and drawn on SLAYER3D's
     * overlay path after world rendering. When @p asset_warmup has a matching
     * pending or failed UI image request, the draw is skipped so authored
     * placeholders can remain visible without blocking on a synchronous lazy
     * load. @p render_eval supplies the current presentation time for authored
     * image effects such as `melt`.
     */
    bool slayer3d_game_data_draw_ui_images(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                           slayer3d_game_data_image_cache *image_cache,
                                           const slayer3d_game_data_asset_warmup_queue *asset_warmup,
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
     * @brief Record CPU time spent updating managed data-game systems.
     *
     * The value is accumulated into the same authored metrics sample window as
     * FPS and exposed through `perf.update_cpu_ms` UI metric bindings.
     */
    void slayer3d_game_data_frame_state_record_update_cpu_time(slayer3d_game_data_frame_state *state, float seconds);

    /**
     * @brief Record CPU time spent drawing managed data-game systems.
     *
     * This excludes the backend present/swap. The sampled value is exposed
     * through `perf.render_cpu_ms` UI metric bindings.
     */
    void slayer3d_game_data_frame_state_record_render_cpu_time(slayer3d_game_data_frame_state *state, float seconds);

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

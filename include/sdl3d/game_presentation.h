/**
 * @file game_presentation.h
 * @brief Optional helpers for presenting JSON-authored game data.
 *
 * The game-data runtime owns authored state and descriptors. This module is a
 * thin reusable bridge that applies those descriptors to SDL3D renderer-facing
 * APIs: simple render primitives, UI text, and scene transition sequencing.
 *
 * Applications may use these helpers when the default behavior fits, or ignore
 * them and provide custom rendering/flow code while still using the same data.
 */

#ifndef SDL3D_GAME_PRESENTATION_H
#define SDL3D_GAME_PRESENTATION_H

#include <stdbool.h>

#include "sdl3d/camera.h"
#include "sdl3d/effects.h"
#include "sdl3d/font.h"
#include "sdl3d/game.h"
#include "sdl3d/game_data.h"
#include "sdl3d/input.h"
#include "sdl3d/render_context.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/transition.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Runtime cache for fonts referenced by authored UI text.
     *
     * The cache owns loaded sdl3d_font instances. It does not own the
     * runtime-provided font id strings.
     */
    typedef struct sdl3d_game_data_font_cache
    {
        sdl3d_font *fonts;     /**< Loaded font instances. */
        const char **font_ids; /**< Runtime-owned authored font ids. */
        int count;             /**< Number of cached fonts. */
        int capacity;          /**< Allocated cache slots. */
        const char *media_dir; /**< SDL3D media directory used for built-in fonts. */
    } sdl3d_game_data_font_cache;

    /**
     * @brief Scene transition flow driven by authored scene transition data.
     *
     * The flow owns only transition state and the pending target scene name.
     * Scene data remains owned by sdl3d_game_data_runtime.
     */
    typedef struct sdl3d_game_data_scene_flow
    {
        sdl3d_transition transition; /**< Active enter/exit transition. */
        const char *pending_scene;   /**< Runtime-owned target scene name, or NULL. */
        bool fading_out;             /**< True while the current scene is exiting. */
        bool fading_in;              /**< True while the target scene is entering. */
    } sdl3d_game_data_scene_flow;

    /**
     * @brief One cached authored particle emitter.
     */
    typedef struct sdl3d_game_data_particle_cache_entry
    {
        const char *entity_name;         /**< Runtime-owned entity name. */
        sdl3d_particle_emitter *emitter; /**< Owned particle emitter instance. */
        sdl3d_vec3 draw_emissive;        /**< Draw-time emissive color. */
        bool visible;                    /**< True when active in the current scene/frame. */
    } sdl3d_game_data_particle_cache_entry;

    /**
     * @brief Runtime cache for particle emitters referenced by authored data.
     *
     * The cache owns emitter instances. It does not own entity name strings.
     */
    typedef struct sdl3d_game_data_particle_cache
    {
        sdl3d_game_data_particle_cache_entry *entries; /**< Cached emitter entries. */
        int count;                                     /**< Number of cache entries. */
        int capacity;                                  /**< Allocated entry slots. */
    } sdl3d_game_data_particle_cache;

    /**
     * @brief Result produced by the generic authored menu controller.
     *
     * The menu controller owns only navigation and selected-item resolution.
     * Callers decide how to apply the selected command, such as emitting a
     * signal, requesting a scene transition, or quitting the app.
     */
    typedef struct sdl3d_game_data_menu_update_result
    {
        const char *menu;   /**< Runtime-owned active menu name, or NULL. */
        const char *scene;  /**< Runtime-owned selected target scene, or NULL. */
        int selected_index; /**< Selected item index after this update, or -1. */
        int signal_id;      /**< Selected signal id, or -1. */
        bool handled_input; /**< True when a menu action was consumed. */
        bool selected;      /**< True when the selected item was activated. */
        bool quit;          /**< True when the selected item requests quit. */
    } sdl3d_game_data_menu_update_result;

    /**
     * @brief Reusable application flow for JSON-authored lifecycle controls.
     *
     * The flow owns app-level transition state, scene transition state, menu
     * input arming, and quit intent. It reads controls from sdl3d_game_data_runtime
     * and applies them to an sdl3d_game_context.
     */
    typedef struct sdl3d_game_data_app_flow
    {
        sdl3d_game_data_scene_flow scene_flow; /**< Data-authored scene transition flow. */
        sdl3d_transition transition;           /**< App-level startup/quit transition. */
        sdl3d_game_data_app_control app;       /**< Resolved app controls from game data. */
        bool quit_pending;                     /**< True after quit has been requested. */
        bool scene_input_armed;                /**< True once menu input is idle after scene entry. */
    } sdl3d_game_data_app_flow;

    typedef struct sdl3d_game_data_frame_desc sdl3d_game_data_frame_desc;

    /**
     * @brief Optional callback invoked by sdl3d_game_data_draw_frame().
     *
     * Return false to report a draw failure while still allowing the frame
     * helper to finish restoring renderer state.
     */
    typedef bool (*sdl3d_game_data_frame_hook)(void *userdata, const sdl3d_game_data_frame_desc *frame);

    /**
     * @brief Descriptor for drawing one data-authored presentation frame.
     *
     * The helper applies authored render settings, configures lights, clears
     * the frame, draws active-scene world primitives/particles when enabled,
     * draws authored UI text, then draws active app/scene transitions. Hooks
     * are optional extension points for game-specific rendering.
     */
    struct sdl3d_game_data_frame_desc
    {
        const sdl3d_game_data_runtime *runtime;         /**< Authored runtime to render. */
        sdl3d_render_context *renderer;                 /**< Render context receiving draw calls. */
        sdl3d_game_data_font_cache *font_cache;         /**< Font cache used by authored UI text. */
        sdl3d_game_data_particle_cache *particle_cache; /**< Particle cache used by authored emitters. */
        const sdl3d_game_data_app_flow *app_flow;       /**< Optional app flow whose transitions are drawn. */
        const sdl3d_game_data_ui_metrics *metrics;      /**< Optional UI metrics. */
        const sdl3d_game_data_render_eval *render_eval; /**< Optional primitive effect evaluation inputs. */
        const sdl3d_camera3d *fallback_camera;          /**< Optional camera used when no active camera resolves. */
        float pulse_phase;                              /**< Normalized UI pulse phase. */
        sdl3d_game_data_frame_hook before_world_3d;     /**< Optional hook inside the 3D pass before data draws. */
        sdl3d_game_data_frame_hook after_world_3d;      /**< Optional hook inside the 3D pass after data draws. */
        sdl3d_game_data_frame_hook before_ui;           /**< Optional hook before authored UI and transitions. */
        sdl3d_game_data_frame_hook after_ui;            /**< Optional hook after authored UI and transitions. */
        void *userdata;                                 /**< User pointer passed to hooks. */
    };

    /**
     * @brief Initialize a font cache.
     *
     * @param cache     Cache to initialize.
     * @param media_dir Directory containing SDL3D built-in media, usually SDL3D_MEDIA_DIR.
     */
    void sdl3d_game_data_font_cache_init(sdl3d_game_data_font_cache *cache, const char *media_dir);

    /**
     * @brief Free all fonts owned by a cache.
     *
     * Safe to call with NULL or an already-freed cache.
     */
    void sdl3d_game_data_font_cache_free(sdl3d_game_data_font_cache *cache);

    /**
     * @brief Draw authored render primitives for the active scene.
     *
     * This renders currently supported primitive components (`render.cube` and
     * `render.sphere`) using SDL3D's immediate drawing helpers. Call inside an
     * active 3D pass.
     */
    bool sdl3d_game_data_draw_render_primitives(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer);

    /**
     * @brief Draw authored render primitives with evaluated time-based effects.
     *
     * @see sdl3d_game_data_draw_render_primitives
     */
    bool sdl3d_game_data_draw_render_primitives_evaluated(const sdl3d_game_data_runtime *runtime,
                                                          sdl3d_render_context *renderer,
                                                          const sdl3d_game_data_render_eval *eval);

    /**
     * @brief Draw authored UI text for the active scene.
     *
     * Built-in font assets are loaded on demand through @p font_cache. Text is
     * drawn on SDL3D's overlay path, after world rendering. @p pulse_phase is a
     * normalized phase used by UI items with `pulse_alpha`.
     */
    bool sdl3d_game_data_draw_ui_text(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer,
                                      sdl3d_game_data_font_cache *font_cache, const sdl3d_game_data_ui_metrics *metrics,
                                      float pulse_phase);

    /**
     * @brief Initialize a particle emitter cache.
     */
    void sdl3d_game_data_particle_cache_init(sdl3d_game_data_particle_cache *cache);

    /**
     * @brief Free all particle emitters owned by a cache.
     */
    void sdl3d_game_data_particle_cache_free(sdl3d_game_data_particle_cache *cache);

    /**
     * @brief Advance active authored particle emitters.
     *
     * Creates emitters lazily for active scene entities with `particles.emitter`
     * components, updates their authored positions, and advances particle
     * simulation by @p dt.
     */
    bool sdl3d_game_data_update_particles(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_particle_cache *cache,
                                          float dt);

    /**
     * @brief Draw active authored particle emitters.
     *
     * Call inside an active 3D pass.
     */
    bool sdl3d_game_data_draw_particles(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer,
                                        sdl3d_game_data_particle_cache *cache);

    /**
     * @brief Consume authored active-menu input and resolve selected commands.
     *
     * @p input_armed should be persisted by the caller for the active scene.
     * The controller waits until all menu navigation actions are idle before
     * accepting new presses, preventing a held key from immediately selecting
     * the next scene's default menu item.
     */
    bool sdl3d_game_data_update_menus(sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input,
                                      bool *input_armed, sdl3d_game_data_menu_update_result *out_result);

    /**
     * @brief Initialize a scene transition flow.
     */
    void sdl3d_game_data_scene_flow_init(sdl3d_game_data_scene_flow *flow);

    /**
     * @brief Return whether a scene transition is currently active.
     */
    bool sdl3d_game_data_scene_flow_is_transitioning(const sdl3d_game_data_scene_flow *flow);

    /**
     * @brief Request a transition to another authored scene.
     *
     * If the target is already active, unknown, or another transition is in
     * progress, the request is rejected and returns false. Otherwise the flow
     * starts the active scene's authored `exit` transition when one exists, or
     * switches scenes on the next update.
     */
    bool sdl3d_game_data_scene_flow_request(sdl3d_game_data_scene_flow *flow, sdl3d_game_data_runtime *runtime,
                                            const char *scene_name);

    /**
     * @brief Advance a scene transition flow.
     *
     * When the exit transition finishes, the flow activates the pending scene
     * through sdl3d_game_data_set_active_scene(), then starts that scene's
     * authored `enter` transition when one exists.
     */
    void sdl3d_game_data_scene_flow_update(sdl3d_game_data_scene_flow *flow, sdl3d_game_data_runtime *runtime,
                                           sdl3d_signal_bus *bus, float dt);

    /**
     * @brief Draw the active transition owned by a scene flow.
     */
    void sdl3d_game_data_scene_flow_draw(const sdl3d_game_data_scene_flow *flow, sdl3d_render_context *renderer);

    /**
     * @brief Initialize reusable app lifecycle flow state.
     */
    void sdl3d_game_data_app_flow_init(sdl3d_game_data_app_flow *flow);

    /**
     * @brief Start app lifecycle flow from authored data.
     *
     * Reads app controls and starts the authored startup transition when present.
     */
    bool sdl3d_game_data_app_flow_start(sdl3d_game_data_app_flow *flow, sdl3d_game_data_runtime *runtime);

    /**
     * @brief Return true when quit has been requested and is waiting on a transition.
     */
    bool sdl3d_game_data_app_flow_quit_pending(const sdl3d_game_data_app_flow *flow);

    /**
     * @brief Return true while any app or scene transition is active.
     */
    bool sdl3d_game_data_app_flow_is_transitioning(const sdl3d_game_data_app_flow *flow);

    /**
     * @brief Consume authored app/menu/scene input and advance transitions.
     *
     * @p pause_allowed lets game-specific state, such as a finished match,
     * suppress pause without duplicating menu, quit, or scene transition logic
     * in the host. The function updates ctx->paused and ctx->quit_requested
     * when authored controls request those state changes.
     */
    bool sdl3d_game_data_app_flow_update(sdl3d_game_data_app_flow *flow, sdl3d_game_context *ctx,
                                         sdl3d_game_data_runtime *runtime, bool pause_allowed, float dt);

    /**
     * @brief Draw active app-level and scene-level transitions.
     */
    void sdl3d_game_data_app_flow_draw(const sdl3d_game_data_app_flow *flow, sdl3d_render_context *renderer);

    /**
     * @brief Draw one complete data-authored presentation frame.
     *
     * This is the high-level version of the lower-level draw helpers above.
     * Hosts that only need authored presentation can call this from their
     * render callback and reserve hooks for custom rendering.
     */
    bool sdl3d_game_data_draw_frame(const sdl3d_game_data_frame_desc *frame);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_GAME_PRESENTATION_H */

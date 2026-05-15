#ifndef SLAYER3D_RENDER_CONTEXT_H
#define SLAYER3D_RENDER_CONTEXT_H

#include <stdbool.h>

#include <SDL3/SDL_render.h>

#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum slayer3d_backend
    {
        SLAYER3D_BACKEND_AUTO = 0,
        SLAYER3D_BACKEND_SOFTWARE = 1,
        SLAYER3D_BACKEND_OPENGL = 2
    } slayer3d_backend;

    /** @brief Window presentation mode used by high-level window creation. */
    typedef enum slayer3d_window_mode
    {
        /** @brief Use SLAYER3D's build/profile default window mode. */
        SLAYER3D_WINDOW_MODE_DEFAULT = 0,
        /** @brief Desktop window. */
        SLAYER3D_WINDOW_MODE_WINDOWED = 1,
        /** @brief Exclusive fullscreen display mode. */
        SLAYER3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE = 2,
        /** @brief Borderless desktop fullscreen. */
        SLAYER3D_WINDOW_MODE_FULLSCREEN_BORDERLESS = 3
    } slayer3d_window_mode;

    typedef struct slayer3d_render_context_config
    {
        slayer3d_backend backend;
        bool allow_backend_fallback;
        int logical_width;
        int logical_height;
        SDL_RendererLogicalPresentation logical_presentation;
    } slayer3d_render_context_config;

    /** @brief Cumulative render submission counters for profiling/debug UI. */
    typedef struct slayer3d_render_stats
    {
        /** @brief Model meshes submitted to the render path before visibility culling. */
        Uint64 model_mesh_submissions;
        /** @brief Model meshes rejected by render-context frustum culling. */
        Uint64 model_mesh_culled;
        /** @brief Model meshes accepted for drawing. */
        Uint64 model_mesh_draws;
        /** @brief Approximate triangles submitted by accepted model meshes. */
        Uint64 model_triangles_submitted;
        /** @brief Opaque draw calls replayed during the depth pre-pass. */
        Uint64 depth_prepass_draws;
        /** @brief Approximate triangles replayed during the depth pre-pass. */
        Uint64 depth_prepass_triangles;
        /** @brief Optional depth-prepass samples that passed the depth test. */
        Uint64 depth_prepass_samples_passed;
        /** @brief Optional main geometry samples that passed the depth test. */
        Uint64 geometry_samples_passed;
        /** @brief Light candidates considered for lit draws. */
        Uint64 light_candidates;
        /** @brief Lights selected and uploaded to the shader for lit draws. */
        Uint64 lights_selected;
        /** @brief Lit draws that performed light selection or global light upload. */
        Uint64 light_selection_draws;
        /** @brief Backend geometry draw calls replayed by capable renderers. */
        Uint64 geometry_draw_calls;
        /** @brief Backend geometry draw calls issued through static mesh instancing. */
        Uint64 static_mesh_instanced_draw_calls;
        /** @brief Static mesh instances rendered through instanced backend draw calls. */
        Uint64 static_mesh_instances_batched;
        /** @brief Backend draw calls avoided by static mesh instancing. */
        Uint64 static_mesh_draw_calls_saved;
    } slayer3d_render_stats;

    /**
     * @brief High-level window configuration.
     *
     * Use slayer3d_init_window_config() for sensible defaults, then override what
     * the game needs. Windowed mode is resizable by default. Games should
     * prefer logical_width/logical_height for authored layout and world scale;
     * width/height only describe the initial desktop window size.
     */
    typedef struct slayer3d_window_config
    {
        int width;                         /**< Initial window width in pixels. */
        int height;                        /**< Initial window height in pixels. */
        int logical_width;                 /**< Virtual render width used for layout and presentation. */
        int logical_height;                /**< Virtual render height used for layout and presentation. */
        const char *title;                 /**< Window title, or "SLAYER3D" when NULL. */
        const char *icon_path;             /**< Optional filesystem path to a window icon image. */
        slayer3d_backend backend;          /**< AUTO, SOFTWARE, or OPENGL. */
        bool allow_backend_fallback;       /**< Try the next backend if the preferred backend fails. */
        slayer3d_window_mode display_mode; /**< Windowed, exclusive fullscreen, or borderless fullscreen. */
        bool vsync;                        /**< Request synchronized presentation where supported. */
        bool maximized;                    /**< Create the desktop window maximized. */
        bool resizable;                    /**< Allow the user to resize desktop windowed mode. */
    } slayer3d_window_config;

    /*
     * Feature flags for slayer3d_is_feature_available. Lets callers query
     * capabilities without checking the backend type directly.
     */
    typedef enum slayer3d_feature
    {
        SLAYER3D_FEATURE_BLOOM = 0,
        SLAYER3D_FEATURE_SSAO,
        SLAYER3D_FEATURE_SHADOWS,
        SLAYER3D_FEATURE_IBL,
        SLAYER3D_FEATURE_POST_PROCESSING
    } slayer3d_feature;

    typedef struct slayer3d_render_context slayer3d_render_context;

    /* ---- Low-level API (advanced: caller manages window + renderer) ---- */

    void slayer3d_init_render_context_config(slayer3d_render_context_config *config);
    const char *slayer3d_get_backend_name(slayer3d_backend backend);
    bool slayer3d_get_backend_override_from_environment(slayer3d_backend *backend);
    bool slayer3d_create_render_context(SDL_Window *window, SDL_Renderer *renderer,
                                        const slayer3d_render_context_config *config,
                                        slayer3d_render_context **out_context);
    void slayer3d_destroy_render_context(slayer3d_render_context *context);

    /* ---- High-level API (recommended: library manages window setup) ---- */

    /**
     * @brief Fill a window config with sensible defaults.
     *
     * Defaults are width=1280, height=720, logical_width=1280,
     * logical_height=720, title="SLAYER3D", backend=AUTO,
     * display_mode=WINDOWED, vsync=true, maximized=false,
     * allow_backend_fallback=true, and resizable=true.
     */
    void slayer3d_init_window_config(slayer3d_window_config *config);

    /**
     * @brief Create a window and render context in one call.
     *
     * Handles backend-specific setup, SDL_Renderer creation for the software
     * path, window flags, optional title/icon metadata, presentation mode, and
     * logical resolution. High-level windows present the logical resolution
     * with letterboxing so resizable windows preserve the authored aspect
     * ratio. The caller receives an SDL_Window* for event polling and an
     * slayer3d_render_context* for rendering.
     *
     * @param config Optional window configuration. NULL selects defaults.
     * @param out_window Receives the SDL window on success.
     * @param out_context Receives the render context on success.
     * @return true on success, false on failure.
     */
    bool slayer3d_create_window(const slayer3d_window_config *config, SDL_Window **out_window,
                                slayer3d_render_context **out_context);

    /**
     * @brief Apply live window presentation settings to an existing window.
     *
     * The function applies display mode and V-sync in place when the backend is
     * unchanged. If @p config requests a different backend, it creates a new
     * window/render context using @p config before destroying the old pair, so
     * failure leaves the existing window usable. On success, @p window and
     * @p context are updated to the active objects.
     *
     * @param window  Pointer to the SDL window pointer to update.
     * @param context Pointer to the SLAYER3D render context pointer to update.
     * @param config  Desired window/backend settings.
     * @return true on success, false on failure.
     */
    bool slayer3d_apply_window_config(SDL_Window **window, slayer3d_render_context **context,
                                      const slayer3d_window_config *config);

    /**
     * @brief Destroy a window and render context created by slayer3d_create_window.
     *
     * This releases the SLAYER3D render context, the SDL_Renderer owned by the
     * software backend path, and the SDL_Window. Safe to call with NULL
     * arguments.
     *
     * @param window  SDL window returned by slayer3d_create_window.
     * @param context Render context returned by slayer3d_create_window.
     */
    void slayer3d_destroy_window(SDL_Window *window, slayer3d_render_context *context);

    /*
     * Switch to a different backend at runtime. Destroys the current
     * window and context, creates new ones with the same dimensions
     * and title. The caller's pointers are updated in place.
     *
     * Returns false if the switch fails; the old window/context are
     * already destroyed in that case (caller should retry or exit).
     */
    bool slayer3d_switch_backend(SDL_Window **window, slayer3d_render_context **context, slayer3d_backend new_backend);

    /*
     * Query whether a rendering feature is available on the current
     * backend. Use this instead of checking the backend type directly.
     */
    bool slayer3d_is_feature_available(const slayer3d_render_context *context, slayer3d_feature feature);

    /* ---- Context queries ---- */

    slayer3d_backend slayer3d_get_render_context_backend(const slayer3d_render_context *context);
    int slayer3d_get_render_context_width(const slayer3d_render_context *context);
    int slayer3d_get_render_context_height(const slayer3d_render_context *context);
    bool slayer3d_get_render_stats(const slayer3d_render_context *context, slayer3d_render_stats *out_stats);
    void slayer3d_reset_render_stats(slayer3d_render_context *context);
    /**
     * @brief Enable or disable the opaque depth pre-pass for capable backends.
     *
     * The OpenGL backend replays eligible opaque lit triangle meshes with a
     * position-only shader before the main geometry pass. This can reduce
     * expensive fragment shading in high-overdraw scenes.
     */
    bool slayer3d_set_depth_prepass_enabled(slayer3d_render_context *context, bool enabled);
    /** @brief Return whether the opaque depth pre-pass is enabled on this context. */
    bool slayer3d_is_depth_prepass_enabled(const slayer3d_render_context *context);
    /**
     * @brief Enable optional render sample queries for performance diagnostics.
     *
     * Capable OpenGL backends count depth-passing samples for the depth pre-pass
     * and main geometry pass. This can block on GPU results, so keep it for
     * profiling/debug overlays rather than shipping gameplay defaults.
     */
    bool slayer3d_set_render_sample_queries_enabled(slayer3d_render_context *context, bool enabled);
    /** @brief Return whether render sample queries are requested. */
    bool slayer3d_render_sample_queries_enabled(const slayer3d_render_context *context);
    /** @brief Enable per-object selection of the most relevant shader lights. */
    bool slayer3d_set_per_object_light_selection_enabled(slayer3d_render_context *context, bool enabled);
    /** @brief Return whether per-object light selection is enabled. */
    bool slayer3d_per_object_light_selection_enabled(const slayer3d_render_context *context);
    /** @brief Set the maximum number of lights uploaded per lit draw, clamped to shader capacity. */
    bool slayer3d_set_per_object_light_limit(slayer3d_render_context *context, int limit);
    /** @brief Return the current per-object light upload limit. */
    int slayer3d_per_object_light_limit(const slayer3d_render_context *context);
    bool slayer3d_clear_render_context(slayer3d_render_context *context, slayer3d_color color);
    bool slayer3d_clear_render_context_rect(slayer3d_render_context *context, const SDL_Rect *rect,
                                            slayer3d_color color);
    bool slayer3d_set_scissor_rect(slayer3d_render_context *context, const SDL_Rect *rect);
    bool slayer3d_is_scissor_enabled(const slayer3d_render_context *context);
    bool slayer3d_get_scissor_rect(const slayer3d_render_context *context, SDL_Rect *out_rect);
    bool slayer3d_present_render_context(slayer3d_render_context *context);

#ifdef __cplusplus
}
#endif

#endif

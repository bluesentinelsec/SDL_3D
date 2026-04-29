#ifndef SDL3D_RENDER_CONTEXT_H
#define SDL3D_RENDER_CONTEXT_H

#include <stdbool.h>

#include <SDL3/SDL_render.h>

#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum sdl3d_backend
    {
        SDL3D_BACKEND_AUTO = 0,
        SDL3D_BACKEND_SOFTWARE = 1,
        SDL3D_BACKEND_OPENGL = 2
    } sdl3d_backend;

    /** @brief Window presentation mode used by high-level window creation. */
    typedef enum sdl3d_window_mode
    {
        /** @brief Use SDL3D's build/profile default window mode. */
        SDL3D_WINDOW_MODE_DEFAULT = 0,
        /** @brief Desktop window. */
        SDL3D_WINDOW_MODE_WINDOWED = 1,
        /** @brief Exclusive fullscreen display mode. */
        SDL3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE = 2,
        /** @brief Borderless desktop fullscreen. */
        SDL3D_WINDOW_MODE_FULLSCREEN_BORDERLESS = 3
    } sdl3d_window_mode;

    typedef struct sdl3d_render_context_config
    {
        sdl3d_backend backend;
        bool allow_backend_fallback;
        int logical_width;
        int logical_height;
        SDL_RendererLogicalPresentation logical_presentation;
    } sdl3d_render_context_config;

    /**
     * @brief High-level window configuration.
     *
     * Use sdl3d_init_window_config() for sensible defaults, then override what
     * the game needs. Windowed mode is resizable by default. Games should
     * prefer logical_width/logical_height for authored layout and world scale;
     * width/height only describe the initial desktop window size.
     */
    typedef struct sdl3d_window_config
    {
        int width;                      /**< Initial window width in pixels. */
        int height;                     /**< Initial window height in pixels. */
        int logical_width;              /**< Virtual render width used for layout and presentation. */
        int logical_height;             /**< Virtual render height used for layout and presentation. */
        const char *title;              /**< Window title, or "SDL3D" when NULL. */
        const char *icon_path;          /**< Optional filesystem path to a window icon image. */
        sdl3d_backend backend;          /**< AUTO, SOFTWARE, or OPENGL. */
        bool allow_backend_fallback;    /**< Try the next backend if the preferred backend fails. */
        sdl3d_window_mode display_mode; /**< Windowed, exclusive fullscreen, or borderless fullscreen. */
        bool vsync;                     /**< Request synchronized presentation where supported. */
        bool maximized;                 /**< Create the desktop window maximized. */
        bool resizable;                 /**< Allow the user to resize desktop windowed mode. */
    } sdl3d_window_config;

    /*
     * Feature flags for sdl3d_is_feature_available. Lets callers query
     * capabilities without checking the backend type directly.
     */
    typedef enum sdl3d_feature
    {
        SDL3D_FEATURE_BLOOM = 0,
        SDL3D_FEATURE_SSAO,
        SDL3D_FEATURE_SHADOWS,
        SDL3D_FEATURE_IBL,
        SDL3D_FEATURE_POST_PROCESSING
    } sdl3d_feature;

    typedef struct sdl3d_render_context sdl3d_render_context;

    /* ---- Low-level API (advanced: caller manages window + renderer) ---- */

    void sdl3d_init_render_context_config(sdl3d_render_context_config *config);
    const char *sdl3d_get_backend_name(sdl3d_backend backend);
    bool sdl3d_get_backend_override_from_environment(sdl3d_backend *backend);
    bool sdl3d_create_render_context(SDL_Window *window, SDL_Renderer *renderer,
                                     const sdl3d_render_context_config *config, sdl3d_render_context **out_context);
    void sdl3d_destroy_render_context(sdl3d_render_context *context);

    /* ---- High-level API (recommended: library manages window setup) ---- */

    /**
     * @brief Fill a window config with sensible defaults.
     *
     * Defaults are width=1280, height=720, logical_width=1280,
     * logical_height=720, title="SDL3D", backend=AUTO,
     * display_mode=WINDOWED, vsync=true, maximized=false,
     * allow_backend_fallback=true, and resizable=true.
     */
    void sdl3d_init_window_config(sdl3d_window_config *config);

    /**
     * @brief Create a window and render context in one call.
     *
     * Handles backend-specific setup, SDL_Renderer creation for the software
     * path, window flags, optional title/icon metadata, presentation mode, and
     * logical resolution. The caller receives an SDL_Window* for event polling
     * and an sdl3d_render_context* for rendering.
     *
     * @param config Optional window configuration. NULL selects defaults.
     * @param out_window Receives the SDL window on success.
     * @param out_context Receives the render context on success.
     * @return true on success, false on failure.
     */
    bool sdl3d_create_window(const sdl3d_window_config *config, SDL_Window **out_window,
                             sdl3d_render_context **out_context);

    /**
     * @brief Destroy a window and render context created by sdl3d_create_window.
     *
     * This releases the SDL3D render context, the SDL_Renderer owned by the
     * software backend path, and the SDL_Window. Safe to call with NULL
     * arguments.
     *
     * @param window  SDL window returned by sdl3d_create_window.
     * @param context Render context returned by sdl3d_create_window.
     */
    void sdl3d_destroy_window(SDL_Window *window, sdl3d_render_context *context);

    /*
     * Switch to a different backend at runtime. Destroys the current
     * window and context, creates new ones with the same dimensions
     * and title. The caller's pointers are updated in place.
     *
     * Returns false if the switch fails; the old window/context are
     * already destroyed in that case (caller should retry or exit).
     */
    bool sdl3d_switch_backend(SDL_Window **window, sdl3d_render_context **context, sdl3d_backend new_backend);

    /*
     * Query whether a rendering feature is available on the current
     * backend. Use this instead of checking the backend type directly.
     */
    bool sdl3d_is_feature_available(const sdl3d_render_context *context, sdl3d_feature feature);

    /* ---- Context queries ---- */

    sdl3d_backend sdl3d_get_render_context_backend(const sdl3d_render_context *context);
    int sdl3d_get_render_context_width(const sdl3d_render_context *context);
    int sdl3d_get_render_context_height(const sdl3d_render_context *context);
    bool sdl3d_clear_render_context(sdl3d_render_context *context, sdl3d_color color);
    bool sdl3d_clear_render_context_rect(sdl3d_render_context *context, const SDL_Rect *rect, sdl3d_color color);
    bool sdl3d_set_scissor_rect(sdl3d_render_context *context, const SDL_Rect *rect);
    bool sdl3d_is_scissor_enabled(const sdl3d_render_context *context);
    bool sdl3d_get_scissor_rect(const sdl3d_render_context *context, SDL_Rect *out_rect);
    bool sdl3d_present_render_context(sdl3d_render_context *context);

#ifdef __cplusplus
}
#endif

#endif

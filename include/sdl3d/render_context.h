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
        SDL3D_BACKEND_SDLGPU = 2
    } sdl3d_backend;

    typedef struct sdl3d_render_context_config
    {
        sdl3d_backend backend;
        bool allow_backend_fallback;
        int logical_width;
        int logical_height;
        SDL_RendererLogicalPresentation logical_presentation;
    } sdl3d_render_context_config;

    typedef struct sdl3d_render_context sdl3d_render_context;

    void sdl3d_init_render_context_config(sdl3d_render_context_config *config);
    const char *sdl3d_get_backend_name(sdl3d_backend backend);
    bool sdl3d_get_backend_override_from_environment(sdl3d_backend *backend);
    bool sdl3d_create_render_context(SDL_Window *window, SDL_Renderer *renderer,
                                     const sdl3d_render_context_config *config, sdl3d_render_context **out_context);
    void sdl3d_destroy_render_context(sdl3d_render_context *context);
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

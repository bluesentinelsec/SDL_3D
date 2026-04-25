/* Window/backend creation and hot-switching. */
#include "backend.h"

bool backend_create(SDL_Window **out_win, sdl3d_render_context **out_ctx, sdl3d_backend backend)
{
    sdl3d_window_config wcfg;
    sdl3d_init_window_config(&wcfg);
    wcfg.width = (backend == SDL3D_BACKEND_SOFTWARE) ? SOFTWARE_W : WINDOW_W;
    wcfg.height = (backend == SDL3D_BACKEND_SOFTWARE) ? SOFTWARE_H : WINDOW_H;
    wcfg.title = "SDL3D \xe2\x80\x94 Doom Level";
    wcfg.backend = backend;
    wcfg.allow_backend_fallback = false;

    if (!sdl3d_create_window(&wcfg, out_win, out_ctx))
        return false;

    SDL_SetWindowRelativeMouseMode(*out_win, true);
    return true;
}

void backend_apply_defaults(sdl3d_render_context *ctx)
{
    sdl3d_set_bloom_enabled(ctx, sdl3d_is_feature_available(ctx, SDL3D_FEATURE_BLOOM));
    sdl3d_set_ssao_enabled(ctx, false);
    sdl3d_set_point_shadows_enabled(ctx, false);
    sdl3d_set_backface_culling_enabled(ctx, true);
    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG);
}

/* Doom-level render defaults. */
#include "backend.h"

void backend_apply_defaults(sdl3d_render_context *ctx)
{
    sdl3d_set_bloom_enabled(ctx, sdl3d_is_feature_available(ctx, SDL3D_FEATURE_BLOOM));
    sdl3d_set_ssao_enabled(ctx, false);
    sdl3d_set_point_shadows_enabled(ctx, false);
    sdl3d_set_backface_culling_enabled(ctx, true);
    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG);
}

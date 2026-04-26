/* Doom-level render defaults. */
#include "backend.h"

const char *backend_profile_name(doom_render_profile profile)
{
    switch (profile)
    {
    case DOOM_RENDER_PROFILE_MODERN:
        return "Modern";
    case DOOM_RENDER_PROFILE_PS1:
        return "PS1";
    case DOOM_RENDER_PROFILE_N64:
        return "N64";
    case DOOM_RENDER_PROFILE_DOS:
        return "DOS";
    case DOOM_RENDER_PROFILE_SNES:
        return "SNES Star Fox";
    default:
        return "Modern";
    }
}

sdl3d_render_profile backend_profile_descriptor(doom_render_profile profile)
{
    switch (profile)
    {
    case DOOM_RENDER_PROFILE_MODERN:
        return sdl3d_profile_modern();
    case DOOM_RENDER_PROFILE_PS1:
        return sdl3d_profile_ps1();
    case DOOM_RENDER_PROFILE_N64:
        return sdl3d_profile_n64();
    case DOOM_RENDER_PROFILE_DOS:
        return sdl3d_profile_dos();
    case DOOM_RENDER_PROFILE_SNES:
        return sdl3d_profile_snes();
    default:
        return sdl3d_profile_modern();
    }
}

bool backend_apply_profile(sdl3d_render_context *ctx, doom_render_profile profile)
{
    sdl3d_render_profile descriptor = backend_profile_descriptor(profile);
    return sdl3d_set_render_profile(ctx, &descriptor);
}

void backend_apply_defaults(sdl3d_render_context *ctx, doom_render_profile profile)
{
    sdl3d_set_bloom_enabled(ctx, sdl3d_is_feature_available(ctx, SDL3D_FEATURE_BLOOM));
    sdl3d_set_ssao_enabled(ctx, false);
    sdl3d_set_point_shadows_enabled(ctx, false);
    sdl3d_set_backface_culling_enabled(ctx, true);
    backend_apply_profile(ctx, profile);
}

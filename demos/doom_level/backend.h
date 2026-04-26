/* Doom-level render defaults and dimensions. */
#ifndef DOOM_BACKEND_H
#define DOOM_BACKEND_H

#include "sdl3d/sdl3d.h"

#define WINDOW_W 1280
#define WINDOW_H 720

typedef enum doom_render_profile
{
    DOOM_RENDER_PROFILE_MODERN = 0,
    DOOM_RENDER_PROFILE_PS1,
    DOOM_RENDER_PROFILE_N64,
    DOOM_RENDER_PROFILE_DOS,
    DOOM_RENDER_PROFILE_SNES,
    DOOM_RENDER_PROFILE_COUNT
} doom_render_profile;

/* Return a short display name for a Doom demo render profile. */
const char *backend_profile_name(doom_render_profile profile);

/* Return the SDL3D render profile descriptor for a Doom demo profile. */
sdl3d_render_profile backend_profile_descriptor(doom_render_profile profile);

/* Apply only the active render profile. */
bool backend_apply_profile(sdl3d_render_context *ctx, doom_render_profile profile);

/* Apply default render settings after creating or switching backends. */
void backend_apply_defaults(sdl3d_render_context *ctx, doom_render_profile profile);

#endif

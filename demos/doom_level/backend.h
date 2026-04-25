/* Doom-level render defaults and dimensions. */
#ifndef DOOM_BACKEND_H
#define DOOM_BACKEND_H

#include "sdl3d/sdl3d.h"

#define WINDOW_W 1280
#define WINDOW_H 720

/* Apply default render settings after creating or switching backends. */
void backend_apply_defaults(sdl3d_render_context *ctx);

#endif

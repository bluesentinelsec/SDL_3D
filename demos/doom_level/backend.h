/* Window/backend creation and hot-switching. */
#ifndef DOOM_BACKEND_H
#define DOOM_BACKEND_H

#include "sdl3d/sdl3d.h"

#include <SDL3/SDL_video.h>

#include <stdbool.h>

#define WINDOW_W 1280
#define WINDOW_H 720
#define SOFTWARE_W (WINDOW_W / 2)
#define SOFTWARE_H (WINDOW_H / 2)

bool backend_create(SDL_Window **out_win, sdl3d_render_context **out_ctx, sdl3d_backend backend);

/* Apply default render settings after creating or switching backends. */
void backend_apply_defaults(sdl3d_render_context *ctx);

#endif

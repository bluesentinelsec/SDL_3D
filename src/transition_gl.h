/*
 * Internal GL transition dispatch. Public callers use sdl3d_transition_draw.
 */

#ifndef SDL3D_TRANSITION_GL_H
#define SDL3D_TRANSITION_GL_H

#include <stdbool.h>

#include "sdl3d/transition.h"

bool sdl3d_transition_draw_gl(const sdl3d_transition *transition, sdl3d_render_context *context);

#endif /* SDL3D_TRANSITION_GL_H */

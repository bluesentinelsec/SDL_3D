/*
 * Internal GL transition dispatch. Public callers use slayer3d_transition_draw.
 */

#ifndef SLAYER3D_TRANSITION_GL_H
#define SLAYER3D_TRANSITION_GL_H

#include <stdbool.h>

#include "slayer3d/transition.h"

bool slayer3d_transition_draw_gl(const slayer3d_transition *transition, slayer3d_render_context *context);

#endif /* SLAYER3D_TRANSITION_GL_H */

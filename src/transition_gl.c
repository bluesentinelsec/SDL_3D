#include "transition_gl.h"

#include <SDL3/SDL_error.h>

#include "gl_renderer.h"
#include "render_context_internal.h"

bool slayer3d_transition_draw_gl(const slayer3d_transition *transition, slayer3d_render_context *context)
{
    if (transition == NULL)
    {
        return SDL_InvalidParamError("transition");
    }
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (context->gl == NULL)
    {
        return SDL_SetError("GL transition draw requires the GL backend.");
    }

    return slayer3d_gl_queue_transition(context->gl, transition);
}

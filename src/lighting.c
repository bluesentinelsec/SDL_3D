#include "sdl3d/lighting.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "render_context_internal.h"

bool sdl3d_add_light(sdl3d_render_context *context, const sdl3d_light *light)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (light == NULL)
    {
        return SDL_InvalidParamError("light");
    }
    if (context->light_count >= SDL3D_MAX_LIGHTS)
    {
        return SDL_SetError("Light list is full (max %d).", SDL3D_MAX_LIGHTS);
    }

    context->lights[context->light_count] = *light;
    context->light_count += 1;
    return true;
}

bool sdl3d_clear_lights(sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->light_count = 0;
    return true;
}

bool sdl3d_set_lighting_enabled(sdl3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->lighting_enabled = enabled;
    return true;
}

bool sdl3d_is_lighting_enabled(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return false;
    }
    return context->lighting_enabled;
}

bool sdl3d_set_ambient_light(sdl3d_render_context *context, float r, float g, float b)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->ambient[0] = r;
    context->ambient[1] = g;
    context->ambient[2] = b;
    return true;
}

int sdl3d_get_light_count(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return 0;
    }
    return context->light_count;
}

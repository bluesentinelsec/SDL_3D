#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_version.h>

#include "slayer3d/slayer3d.h"

static const char SLAYER3D_GREETING[] = "Hello from SLAYER3D.";
static const int SLAYER3D_LOG_CATEGORY = SDL_LOG_CATEGORY_CUSTOM;

const char *slayer3d_greet(void)
{
    return SLAYER3D_GREETING;
}

bool slayer3d_copy_greeting(char *buffer, size_t buffer_size)
{
    if (buffer == NULL)
    {
        return SDL_InvalidParamError("buffer");
    }

    if (buffer_size < sizeof(SLAYER3D_GREETING))
    {
        return SDL_SetError("Buffer is too small for the SLAYER3D greeting.");
    }

    SDL_memcpy(buffer, SLAYER3D_GREETING, sizeof(SLAYER3D_GREETING));
    return true;
}

int slayer3d_linked_sdl_version(void)
{
    return SDL_GetVersion();
}

int slayer3d_log_category(void)
{
    return SLAYER3D_LOG_CATEGORY;
}

SDL_LogPriority slayer3d_get_log_priority(void)
{
    return SDL_GetLogPriority(SLAYER3D_LOG_CATEGORY);
}

void slayer3d_set_log_priority(SDL_LogPriority priority)
{
    SDL_SetLogPriority(SLAYER3D_LOG_CATEGORY, priority);
}

bool slayer3d_log_message(SDL_LogPriority priority, const char *message)
{
    if (message == NULL)
    {
        return SDL_InvalidParamError("message");
    }

    SDL_LogMessage(SLAYER3D_LOG_CATEGORY, priority, "%s", message);
    return true;
}

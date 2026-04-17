#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_version.h>

#include "sdl3d/sdl3d.h"

static const char SDL3D_GREETING[] = "Hello from SDL3D.";
static const int SDL3D_LOG_CATEGORY = SDL_LOG_CATEGORY_CUSTOM;

const char *sdl3d_greet(void)
{
    return SDL3D_GREETING;
}

bool sdl3d_copy_greeting(char *buffer, size_t buffer_size)
{
    if (buffer == NULL)
    {
        return SDL_InvalidParamError("buffer");
    }

    if (buffer_size < sizeof(SDL3D_GREETING))
    {
        return SDL_SetError("Buffer is too small for the SDL3D greeting.");
    }

    SDL_memcpy(buffer, SDL3D_GREETING, sizeof(SDL3D_GREETING));
    return true;
}

int sdl3d_linked_sdl_version(void)
{
    return SDL_GetVersion();
}

int sdl3d_log_category(void)
{
    return SDL3D_LOG_CATEGORY;
}

SDL_LogPriority sdl3d_get_log_priority(void)
{
    return SDL_GetLogPriority(SDL3D_LOG_CATEGORY);
}

void sdl3d_set_log_priority(SDL_LogPriority priority)
{
    SDL_SetLogPriority(SDL3D_LOG_CATEGORY, priority);
}

bool sdl3d_log_message(SDL_LogPriority priority, const char *message)
{
    if (message == NULL)
    {
        return SDL_InvalidParamError("message");
    }

    SDL_LogMessage(SDL3D_LOG_CATEGORY, priority, "%s", message);
    return true;
}

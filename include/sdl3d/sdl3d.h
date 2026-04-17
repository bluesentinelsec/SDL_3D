#ifndef SDL3D_SDL3D_H
#define SDL3D_SDL3D_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL_log.h>

#include "sdl3d/camera.h"
#include "sdl3d/drawing3d.h"
#include "sdl3d/math.h"
#include "sdl3d/render_context.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    const char *sdl3d_greet(void);
    bool sdl3d_copy_greeting(char *buffer, size_t buffer_size);
    int sdl3d_linked_sdl_version(void);
    int sdl3d_log_category(void);
    SDL_LogPriority sdl3d_get_log_priority(void);
    void sdl3d_set_log_priority(SDL_LogPriority priority);
    bool sdl3d_log_message(SDL_LogPriority priority, const char *message);

#ifdef __cplusplus
}
#endif

#endif

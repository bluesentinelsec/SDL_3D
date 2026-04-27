#ifndef SDL3D_SDL3D_H
#define SDL3D_SDL3D_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL_log.h>

#include "sdl3d/actor_controller.h"
#include "sdl3d/animation.h"
#include "sdl3d/audio.h"
#include "sdl3d/camera.h"
#include "sdl3d/collision.h"
#include "sdl3d/door.h"
#include "sdl3d/drawing3d.h"
#include "sdl3d/effects.h"
#include "sdl3d/game.h"
#include "sdl3d/image.h"
#include "sdl3d/input.h"
#include "sdl3d/lighting.h"
#include "sdl3d/logic.h"
#include "sdl3d/math.h"
#include "sdl3d/model.h"
#include "sdl3d/render_context.h"
#include "sdl3d/scene.h"
#include "sdl3d/shapes.h"
#include "sdl3d/teleporter.h"
#include "sdl3d/texture.h"
#include "sdl3d/time.h"
#include "sdl3d/transition.h"
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

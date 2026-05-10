#ifndef SLAYER3D_SLAYER3D_H
#define SLAYER3D_SLAYER3D_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL_log.h>

#include "slayer3d/actor_controller.h"
#include "slayer3d/animation.h"
#include "slayer3d/asset.h"
#include "slayer3d/audio.h"
#include "slayer3d/camera.h"
#include "slayer3d/collision.h"
#include "slayer3d/data_game.h"
#include "slayer3d/door.h"
#include "slayer3d/drawing3d.h"
#include "slayer3d/effects.h"
#include "slayer3d/game.h"
#include "slayer3d/game_data.h"
#include "slayer3d/game_presentation.h"
#include "slayer3d/image.h"
#include "slayer3d/input.h"
#include "slayer3d/lighting.h"
#include "slayer3d/logic.h"
#include "slayer3d/math.h"
#include "slayer3d/model.h"
#include "slayer3d/network.h"
#include "slayer3d/network_replication.h"
#include "slayer3d/render_context.h"
#include "slayer3d/scene.h"
#include "slayer3d/script.h"
#include "slayer3d/shapes.h"
#include "slayer3d/storage.h"
#include "slayer3d/teleporter.h"
#include "slayer3d/texture.h"
#include "slayer3d/time.h"
#include "slayer3d/transition.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    const char *slayer3d_greet(void);
    bool slayer3d_copy_greeting(char *buffer, size_t buffer_size);
    int slayer3d_linked_sdl_version(void);
    int slayer3d_log_category(void);
    SDL_LogPriority slayer3d_get_log_priority(void);
    void slayer3d_set_log_priority(SDL_LogPriority priority);
    bool slayer3d_log_message(SDL_LogPriority priority, const char *message);

#ifdef __cplusplus
}
#endif

#endif

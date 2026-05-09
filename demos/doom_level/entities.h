/* Entity management for native model actors and textures not yet data-authored. */
#ifndef DOOM_ENTITIES_H
#define DOOM_ENTITIES_H

#include "sdl3d/actor_registry.h"
#include "sdl3d/model.h"
#include "sdl3d/scene.h"
#include "sdl3d/signal_bus.h"

#include "level_data.h"

#include <stdbool.h>

typedef struct entities
{
    /* Textures */
    sdl3d_texture2d sky[6]; /* px, nx, py, ny, pz, nz */

    /* 3D models + scene */
    sdl3d_model dragon_model;
    bool has_dragon;
    sdl3d_scene *scene;

    /* Actor registry and signal bus are provided by the managed game loop. */
    sdl3d_actor_registry *registry;
    sdl3d_signal_bus *bus;
} entities;

bool entities_init(entities *e, const sdl3d_level *level, sdl3d_actor_registry *registry, sdl3d_signal_bus *bus);
void entities_free(entities *e);

/* Advance model animations and registry triggers. */
void entities_update(entities *e, const sdl3d_level *level, float dt, sdl3d_vec3 player_position);

#endif

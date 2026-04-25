/* Entity management: sprite actors, 3D model actors, textures. */
#ifndef DOOM_ENTITIES_H
#define DOOM_ENTITIES_H

#include "sdl3d/actor_registry.h"
#include "sdl3d/model.h"
#include "sdl3d/scene.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/sprite_actor.h"
#include "sdl3d/texture.h"

#include "level_data.h"

#include <stdbool.h>

typedef struct entities
{
    /* Textures */
    sdl3d_texture2d enemy_rot_tex[SDL3D_SPRITE_ROTATION_COUNT];
    sdl3d_texture2d health_tex;
    sdl3d_texture2d crate_tex;
    sdl3d_texture2d sky[6]; /* px, nx, py, ny, pz, nz */
    sdl3d_sprite_rotation_set enemy_rotations;

    /* Sprites */
    sdl3d_sprite_scene sprites;

    /* 3D models + scene */
    sdl3d_model robot_model;
    sdl3d_model dragon_model;
    bool has_robot;
    bool has_dragon;
    sdl3d_scene *scene;

    /* Actor registry and signal bus are provided by the managed game loop. */
    sdl3d_actor_registry *registry;
    sdl3d_signal_bus *bus;
} entities;

bool entities_init(entities *e, sdl3d_actor_registry *registry, sdl3d_signal_bus *bus);
void entities_free(entities *e);

/* Advance sprite bobbing, model animations, and registry triggers. */
void entities_update(entities *e, float dt, sdl3d_vec3 player_position);

#endif

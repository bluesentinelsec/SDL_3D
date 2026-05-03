/* Entity management: sprite actors, 3D model actors, textures. */
#ifndef DOOM_ENTITIES_H
#define DOOM_ENTITIES_H

#include "sdl3d/actor_controller.h"
#include "sdl3d/actor_registry.h"
#include "sdl3d/model.h"
#include "sdl3d/scene.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/sprite_actor.h"
#include "sdl3d/sprite_asset.h"

#include "level_data.h"

#include <stdbool.h>

#define DOOM_ROBOT_WALK_FRAME_COUNT 6
#define DOOM_ROBOT_NPC_COUNT 5

typedef struct doom_robot_npc
{
    int sprite_index;
    int actor_id;
    sdl3d_actor_patrol_state last_visual_state;
    sdl3d_actor_patrol_controller patrol;
} doom_robot_npc;

typedef struct entities
{
    /* Textures */
    sdl3d_sprite_asset_runtime robot_sprite;
    sdl3d_texture2d health_tex;
    sdl3d_texture2d crate_tex;
    sdl3d_texture2d sky[6]; /* px, nx, py, ny, pz, nz */
    const sdl3d_sprite_rotation_set *enemy_rotations;
    const sdl3d_sprite_rotation_set *enemy_walk_rotations;

    /* Sprites */
    sdl3d_sprite_scene sprites;
    doom_robot_npc robots[DOOM_ROBOT_NPC_COUNT];
    int robot_count;

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

/* Advance sprite bobbing, model animations, and registry triggers. */
void entities_update(entities *e, const sdl3d_level *level, float dt, sdl3d_vec3 player_position);

#endif

/* Player controller: FPS mover, input, projectile. */
#ifndef DOOM_PLAYER_H
#define DOOM_PLAYER_H

#include "sdl3d/fps_mover.h"
#include "sdl3d/level.h"
#include "sdl3d/types.h"

#include <SDL3/SDL_events.h>

#include <stdbool.h>

#define PLAYER_HEIGHT 1.6f
#define PLAYER_RADIUS 0.35f
#define PLAYER_STEP_HEIGHT 1.1f
#define PLAYER_CEILING_CLEARANCE 0.1f
#define PLAYER_MIN_HEADROOM (PLAYER_HEIGHT + PLAYER_CEILING_CLEARANCE)
#define PLAYER_SPAWN_X 5.0f
#define PLAYER_SPAWN_Z 4.0f
#define PLAYER_SPAWN_YAW 3.14159f
#define MOUSE_SENS 0.002f

typedef struct player_state
{
    sdl3d_fps_mover mover;
    sdl3d_fps_mover_config config;

    /* Accumulated mouse delta, consumed each frame. */
    float frame_mdx;
    float frame_mdy;
    bool mouse_init;

    /* Projectile */
    bool proj_active;
    float proj_x, proj_y, proj_z;
    float proj_dx, proj_dy, proj_dz;
    float proj_life;
} player_state;

void player_init(player_state *p);

/* Process a single SDL event. Returns false if the event signals quit. */
bool player_handle_event(player_state *p, const SDL_Event *ev);

/* Advance physics: WASD, mouse look, gravity, projectile trace. */
void player_update(player_state *p, const sdl3d_level *level, const sdl3d_sector *sectors, float dt);

#endif

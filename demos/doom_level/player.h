/* Player controller: FPS mover, input, projectile. */
#ifndef DOOM_PLAYER_H
#define DOOM_PLAYER_H

#include "sdl3d/fps_mover.h"
#include "sdl3d/input.h"
#include "sdl3d/level.h"
#include "sdl3d/types.h"

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

    int action_move_forward;
    int action_move_back;
    int action_move_left;
    int action_move_right;
    int action_jump;
    int action_fire;
    int action_menu;
    int action_reset;

    /* Projectile */
    bool proj_active;
    float proj_x, proj_y, proj_z;
    float proj_dx, proj_dy, proj_dz;
    float proj_life;

    /* Demonstration damage bookkeeping; death rules are intentionally omitted. */
    float damage_taken;
    float last_damage_per_second;
} player_state;

void player_init(player_state *p, sdl3d_input_manager *input);

/* Advance physics from action input. Returns false when the menu action requests quit. */
bool player_update(player_state *p, const sdl3d_input_manager *input, const sdl3d_level *level,
                   const sdl3d_sector *sectors, float dt);

/* Apply current-sector floor damage and return damage dealt this tick. */
float player_apply_sector_damage(player_state *p, const sdl3d_sector *sectors, int sector_count, float dt);

#endif

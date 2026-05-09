/* Player controller: FPS mover and input. */
#include "player.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/math.h"

#define MOVE_SPEED 12.0f
#define JUMP_VELOCITY 6.0f
#define GRAVITY 14.0f

static void player_reset(player_state *p)
{
    sdl3d_fps_mover_init(&p->mover, &p->config, sdl3d_vec3_make(PLAYER_SPAWN_X, PLAYER_HEIGHT, PLAYER_SPAWN_Z),
                         PLAYER_SPAWN_YAW);
    p->damage_taken = 0.0f;
    p->last_damage_per_second = 0.0f;
}

void player_init(player_state *p, sdl3d_input_manager *input)
{
    SDL_zerop(p);
    p->config.move_speed = MOVE_SPEED;
    p->config.jump_velocity = JUMP_VELOCITY;
    p->config.gravity = GRAVITY;
    p->config.player_height = PLAYER_HEIGHT;
    p->config.player_radius = PLAYER_RADIUS;
    p->config.step_height = PLAYER_STEP_HEIGHT;
    p->config.ceiling_clearance = PLAYER_CEILING_CLEARANCE;

    p->action_move_forward = sdl3d_input_find_action(input, "move_forward");
    p->action_move_back = sdl3d_input_find_action(input, "move_back");
    p->action_move_left = sdl3d_input_find_action(input, "move_left");
    p->action_move_right = sdl3d_input_find_action(input, "move_right");
    p->action_jump = sdl3d_input_find_action(input, "jump");
    p->action_menu = sdl3d_input_find_action(input, "menu");
    p->action_reset = sdl3d_input_register_action(input, "reset_player");
    sdl3d_input_bind_key(input, p->action_reset, SDL_SCANCODE_BACKSPACE);
    sdl3d_input_bind_key(input, p->action_reset, SDL_SCANCODE_DELETE);

    player_reset(p);
}

bool player_update(player_state *p, const sdl3d_input_manager *input, const sdl3d_level *level,
                   const sdl3d_sector *sectors, float dt)
{
    if (sdl3d_input_is_pressed(input, p->action_menu))
    {
        return false;
    }

    if (sdl3d_input_is_pressed(input, p->action_reset))
    {
        player_reset(p);
    }

    if (sdl3d_input_is_pressed(input, p->action_jump))
    {
        sdl3d_fps_mover_jump(&p->mover);
    }

    /* Action movement in world XZ space. */
    float fwd_x = SDL_sinf(p->mover.yaw);
    float fwd_z = -SDL_cosf(p->mover.yaw);
    float right_x = SDL_cosf(p->mover.yaw);
    float right_z = SDL_sinf(p->mover.yaw);
    float forward =
        sdl3d_input_get_value(input, p->action_move_forward) - sdl3d_input_get_value(input, p->action_move_back);
    float side = sdl3d_input_get_value(input, p->action_move_right) - sdl3d_input_get_value(input, p->action_move_left);
    sdl3d_vec2 wish = {
        fwd_x * forward + right_x * side,
        fwd_z * forward + right_z * side,
    };

    sdl3d_fps_mover_update(&p->mover, level, sectors, wish, sdl3d_input_get_mouse_dx(input),
                           sdl3d_input_get_mouse_dy(input), MOUSE_SENS, dt);

    return true;
}

float player_apply_sector_damage(player_state *p, const sdl3d_sector *sectors, int sector_count, float dt)
{
    if (p == NULL || sectors == NULL || sector_count <= 0)
    {
        return 0.0f;
    }

    const int sector_id = p->mover.current_sector;
    if (sector_id < 0 || sector_id >= sector_count)
    {
        p->last_damage_per_second = 0.0f;
        return 0.0f;
    }

    const sdl3d_sector *sector = &sectors[sector_id];
    const float damage = sdl3d_sector_damage_for_delta(sector, dt);
    p->last_damage_per_second = sdl3d_sector_damage_per_second(sector);
    p->damage_taken += damage;
    return damage;
}

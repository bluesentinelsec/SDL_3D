/* Player controller: FPS mover, input, projectile. */
#include "player.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/math.h"

#define MOVE_SPEED 12.0f
#define JUMP_VELOCITY 6.0f
#define GRAVITY 14.0f
#define PROJ_SPEED 20.0f
#define PROJ_LIFETIME 3.0f

void player_init(player_state *p)
{
    SDL_zerop(p);
    p->config.move_speed = MOVE_SPEED;
    p->config.jump_velocity = JUMP_VELOCITY;
    p->config.gravity = GRAVITY;
    p->config.player_height = PLAYER_HEIGHT;
    p->config.player_radius = PLAYER_RADIUS;
    p->config.step_height = PLAYER_STEP_HEIGHT;
    p->config.ceiling_clearance = PLAYER_CEILING_CLEARANCE;
    sdl3d_fps_mover_init(&p->mover, &p->config, sdl3d_vec3_make(PLAYER_SPAWN_X, PLAYER_HEIGHT, PLAYER_SPAWN_Z),
                         PLAYER_SPAWN_YAW);
}

bool player_handle_event(player_state *p, const SDL_Event *ev)
{
    if (ev->type == SDL_EVENT_QUIT)
        return false;
    if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.scancode == SDL_SCANCODE_ESCAPE)
        return false;

    if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.scancode == SDL_SCANCODE_SPACE)
        sdl3d_fps_mover_jump(&p->mover);

    if (ev->type == SDL_EVENT_KEY_DOWN &&
        (ev->key.scancode == SDL_SCANCODE_BACKSPACE || ev->key.scancode == SDL_SCANCODE_DELETE))
    {
        sdl3d_fps_mover_init(&p->mover, &p->config, sdl3d_vec3_make(PLAYER_SPAWN_X, PLAYER_HEIGHT, PLAYER_SPAWN_Z),
                             PLAYER_SPAWN_YAW);
        p->proj_active = false;
        p->proj_life = 0.0f;
    }

    if (ev->type == SDL_EVENT_MOUSE_MOTION && p->mouse_init)
    {
        p->frame_mdx += ev->motion.xrel;
        p->frame_mdy += ev->motion.yrel;
    }
    if (ev->type == SDL_EVENT_MOUSE_MOTION)
        p->mouse_init = true;

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev->button.button == SDL_BUTTON_LEFT)
    {
        p->proj_active = true;
        p->proj_x = p->mover.position.x;
        p->proj_y = p->mover.position.y;
        p->proj_z = p->mover.position.z;
        p->proj_dx = SDL_sinf(p->mover.yaw) * SDL_cosf(p->mover.pitch);
        p->proj_dy = SDL_sinf(p->mover.pitch);
        p->proj_dz = -SDL_cosf(p->mover.yaw) * SDL_cosf(p->mover.pitch);
        p->proj_life = PROJ_LIFETIME;
    }

    return true;
}

void player_update(player_state *p, const sdl3d_level *level, const sdl3d_sector *sectors, float dt)
{
    /* WASD wish direction in world XZ space. */
    float fwd_x = SDL_sinf(p->mover.yaw);
    float fwd_z = -SDL_cosf(p->mover.yaw);
    float right_x = SDL_cosf(p->mover.yaw);
    float right_z = SDL_sinf(p->mover.yaw);
    sdl3d_vec2 wish = {0.0f, 0.0f};

    const Uint8 *keys = (const Uint8 *)SDL_GetKeyboardState(NULL);
    if (keys[SDL_SCANCODE_W])
    {
        wish.x += fwd_x;
        wish.y += fwd_z;
    }
    if (keys[SDL_SCANCODE_S])
    {
        wish.x -= fwd_x;
        wish.y -= fwd_z;
    }
    if (keys[SDL_SCANCODE_A])
    {
        wish.x -= right_x;
        wish.y -= right_z;
    }
    if (keys[SDL_SCANCODE_D])
    {
        wish.x += right_x;
        wish.y += right_z;
    }

    sdl3d_fps_mover_update(&p->mover, level, sectors, wish, p->frame_mdx, p->frame_mdy, MOUSE_SENS, dt);
    p->frame_mdx = 0.0f;
    p->frame_mdy = 0.0f;

    /* Projectile trace. */
    if (p->proj_active)
    {
        float travel = PROJ_SPEED * dt;
        sdl3d_vec3 dir = sdl3d_vec3_make(p->proj_dx, p->proj_dy, p->proj_dz);
        sdl3d_level_trace_result tr =
            sdl3d_level_trace_point(level, sectors, sdl3d_vec3_make(p->proj_x, p->proj_y, p->proj_z), dir, travel);
        p->proj_x = tr.end_point.x;
        p->proj_y = tr.end_point.y;
        p->proj_z = tr.end_point.z;
        if (tr.hit)
            p->proj_active = false;
        p->proj_life -= dt;
        if (p->proj_life <= 0.0f)
            p->proj_active = false;
    }
}

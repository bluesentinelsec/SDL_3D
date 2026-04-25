#include "sdl3d/fps_mover.h"

#include <SDL3/SDL_stdinc.h>

#include "sdl3d/math.h"

/* Hard limits / tuning constants kept private. The pitch limit prevents
 * gimbal lock at the poles. The view-smooth decay rate matches the
 * doom_level demo's tuning (higher = snappier). */
#define SDL3D_FPS_PITCH_LIMIT 1.4f
#define SDL3D_FPS_VIEW_SMOOTH_SPEED 12.0f

/* Stop polling smoothing once the residual is below this threshold; keeps
 * the eye from oscillating around 0 when dt is small. */
#define SDL3D_FPS_VIEW_SMOOTH_EPSILON 0.01f

static float fps_min_headroom(const sdl3d_fps_mover *mover)
{
    return mover->config.player_height + mover->config.ceiling_clearance;
}

static bool position_is_walkable(const sdl3d_fps_mover *mover, const sdl3d_level *level, const sdl3d_sector *sectors,
                                 float x, float z, float feet_y, int *out_sector)
{
    static const float sample_dirs[8][2] = {
        {1.0f, 0.0f},       {-1.0f, 0.0f},       {0.0f, 1.0f},        {0.0f, -1.0f},
        {0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, 0.7071f}, {-0.7071f, -0.7071f},
    };

    const float headroom = fps_min_headroom(mover);
    const float step = mover->config.step_height;
    const float radius = mover->config.player_radius;

    int center = sdl3d_level_find_walkable_sector(level, sectors, x, z, feet_y, step, headroom);
    if (center < 0)
    {
        return false;
    }

    float target_floor = sectors[center].floor_y;
    for (int i = 0; i < 8; ++i)
    {
        float sx = x + sample_dirs[i][0] * radius;
        float sz = z + sample_dirs[i][1] * radius;
        int sample = sdl3d_level_find_walkable_sector(level, sectors, sx, sz, feet_y, step, headroom);
        if (sample < 0)
        {
            return false;
        }
        if (SDL_fabsf(sectors[sample].floor_y - target_floor) > step)
        {
            return false;
        }
    }

    if (out_sector)
    {
        *out_sector = center;
    }
    return true;
}

void sdl3d_fps_mover_init(sdl3d_fps_mover *mover, const sdl3d_fps_mover_config *config, sdl3d_vec3 spawn_position,
                          float spawn_yaw)
{
    if (mover == NULL || config == NULL)
    {
        return;
    }
    SDL_zerop(mover);
    mover->config = *config;
    mover->position = spawn_position;
    mover->yaw = spawn_yaw;
    mover->pitch = 0.0f;
    mover->on_ground = true;
    mover->vertical_velocity = 0.0f;
    mover->view_smooth = 0.0f;
    mover->current_sector = -1;
    mover->last_good_position = spawn_position;
    mover->has_last_good = true;
}

void sdl3d_fps_mover_jump(sdl3d_fps_mover *mover)
{
    if (mover == NULL || !mover->on_ground)
    {
        return;
    }
    mover->vertical_velocity = mover->config.jump_velocity;
    mover->on_ground = false;
}

sdl3d_camera3d sdl3d_fps_mover_camera(const sdl3d_fps_mover *mover, float fovy)
{
    sdl3d_camera3d cam;
    SDL_zero(cam);

    if (mover == NULL)
    {
        cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
        cam.fovy = fovy;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;
        return cam;
    }

    float fx = SDL_sinf(mover->yaw) * SDL_cosf(mover->pitch);
    float fz = -SDL_cosf(mover->yaw) * SDL_cosf(mover->pitch);
    float fy = SDL_sinf(mover->pitch);

    float eye_y = mover->position.y + mover->view_smooth;
    cam.position = sdl3d_vec3_make(mover->position.x, eye_y, mover->position.z);
    cam.target = sdl3d_vec3_make(mover->position.x + fx, eye_y + fy, mover->position.z + fz);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = fovy;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;
    return cam;
}

void sdl3d_fps_mover_update(sdl3d_fps_mover *mover, const sdl3d_level *level, const sdl3d_sector *sectors,
                            sdl3d_vec2 wish_dir, float mouse_dx, float mouse_dy, float mouse_sensitivity, float dt)
{
    if (mover == NULL || level == NULL || sectors == NULL || dt <= 0.0f)
    {
        return;
    }

    const float player_height = mover->config.player_height;
    const float step_height = mover->config.step_height;
    const float headroom = fps_min_headroom(mover);
    const float ceiling_clearance = mover->config.ceiling_clearance;

    /* ---- Mouse look ---- */
    mover->yaw += mouse_dx * mouse_sensitivity;
    mover->pitch -= mouse_dy * mouse_sensitivity;
    if (mover->pitch > SDL3D_FPS_PITCH_LIMIT)
        mover->pitch = SDL3D_FPS_PITCH_LIMIT;
    if (mover->pitch < -SDL3D_FPS_PITCH_LIMIT)
        mover->pitch = -SDL3D_FPS_PITCH_LIMIT;

    /* ---- View smoothing decay ---- */
    if (SDL_fabsf(mover->view_smooth) > SDL3D_FPS_VIEW_SMOOTH_EPSILON)
    {
        mover->view_smooth -= mover->view_smooth * SDL3D_FPS_VIEW_SMOOTH_SPEED * dt;
    }
    else
    {
        mover->view_smooth = 0.0f;
    }

    float py_before_collision = mover->position.y;

    /* ---- Horizontal movement with sliding collision ---- */
    {
        float feet_y = mover->position.y - player_height;
        int current_sector = sdl3d_level_find_sector_at(level, sectors, mover->position.x, mover->position.z, feet_y);
        float current_floor = feet_y;
        if (current_sector < 0)
        {
            current_sector = sdl3d_level_find_walkable_sector(level, sectors, mover->position.x, mover->position.z,
                                                              feet_y, step_height, headroom);
        }
        if (current_sector >= 0)
        {
            current_floor = sectors[current_sector].floor_y;
        }

        float wish_len = SDL_sqrtf(wish_dir.x * wish_dir.x + wish_dir.y * wish_dir.y);
        if (wish_len > 1.0f)
        {
            wish_dir.x /= wish_len;
            wish_dir.y /= wish_len;
        }

        if (wish_len > 0.001f)
        {
            float move_x = wish_dir.x * mover->config.move_speed * dt;
            float move_z = wish_dir.y * mover->config.move_speed * dt;
            int candidate = -1;

            if (position_is_walkable(mover, level, sectors, mover->position.x + move_x, mover->position.z + move_z,
                                     feet_y, &candidate))
            {
                mover->position.x += move_x;
                mover->position.z += move_z;
                current_sector = candidate;
            }
            else if (position_is_walkable(mover, level, sectors, mover->position.x + move_x, mover->position.z, feet_y,
                                          &candidate))
            {
                mover->position.x += move_x;
                current_sector = candidate;
            }
            else if (position_is_walkable(mover, level, sectors, mover->position.x, mover->position.z + move_z, feet_y,
                                          &candidate))
            {
                mover->position.z += move_z;
                current_sector = candidate;
            }
        }

        /* Step-up onto a higher floor when on solid ground. */
        feet_y = mover->position.y - player_height;
        current_sector = sdl3d_level_find_sector_at(level, sectors, mover->position.x, mover->position.z, feet_y);
        if (current_sector < 0)
        {
            current_sector = sdl3d_level_find_walkable_sector(level, sectors, mover->position.x, mover->position.z,
                                                              feet_y, step_height, headroom);
        }
        if (mover->on_ground)
        {
            if (current_sector >= 0)
            {
                float target_floor = sectors[current_sector].floor_y;
                if (target_floor < current_floor - step_height)
                {
                    mover->on_ground = false;
                }
                else
                {
                    mover->position.y = target_floor + player_height;
                }
            }
            else
            {
                mover->on_ground = false;
            }
        }
    }

    /* ---- Vertical: substepped gravity, ceiling, floor snap, rescue ---- */
    if (!mover->on_ground)
    {
        mover->vertical_velocity -= mover->config.gravity * dt;
        float dy = mover->vertical_velocity * dt;

        const float max_substep = step_height * 0.5f;
        int substeps = 1;
        float abs_dy = SDL_fabsf(dy);
        if (abs_dy > max_substep && max_substep > 0.0f)
        {
            substeps = (int)SDL_ceilf(abs_dy / max_substep);
        }
        float sub_dy = dy / (float)substeps;

        for (int s = 0; s < substeps; ++s)
        {
            float prev_py = mover->position.y;
            float prev_feet_y = prev_py - player_height;
            mover->position.y += sub_dy;
            float feet_y = mover->position.y - player_height;

            /* Ceiling: head crossed downward into ceiling plane. */
            int containing = sdl3d_level_find_sector_at(level, sectors, mover->position.x, mover->position.z,
                                                        SDL_max(prev_feet_y, feet_y));
            if (containing >= 0)
            {
                float ceiling_y = sectors[containing].ceil_y - ceiling_clearance;
                if (prev_py <= ceiling_y && mover->position.y > ceiling_y)
                {
                    mover->position.y = ceiling_y;
                    if (mover->vertical_velocity > 0.0f)
                        mover->vertical_velocity = 0.0f;
                    break;
                }
            }

            /* Floor: feet crossed any support floor going down. */
            if (mover->vertical_velocity <= 0.0f)
            {
                int support = sdl3d_level_find_support_sector(level, sectors, mover->position.x, mover->position.z,
                                                              SDL_max(prev_feet_y, feet_y) + step_height, headroom);
                if (support >= 0)
                {
                    float floor_y = sectors[support].floor_y;
                    if (prev_feet_y >= floor_y && feet_y <= floor_y)
                    {
                        mover->position.y = floor_y + player_height;
                        mover->vertical_velocity = 0.0f;
                        mover->on_ground = true;
                        break;
                    }
                }
            }
        }

        /* Ground-trace rescue: outside any sector but a support is within
         * step height — Quake's PM_GroundTrace pattern. */
        if (!mover->on_ground && mover->vertical_velocity <= 0.0f)
        {
            float feet_y = mover->position.y - player_height;
            int containing_now =
                sdl3d_level_find_sector_at(level, sectors, mover->position.x, mover->position.z, feet_y);
            if (containing_now < 0)
            {
                int rescue = sdl3d_level_find_support_sector(level, sectors, mover->position.x, mover->position.z,
                                                             feet_y + step_height, headroom);
                if (rescue >= 0)
                {
                    float floor_y = sectors[rescue].floor_y;
                    if (floor_y >= feet_y && (floor_y - feet_y) <= step_height)
                    {
                        mover->position.y = floor_y + player_height;
                        mover->vertical_velocity = 0.0f;
                        mover->on_ground = true;
                    }
                }
            }
        }

        /* Last-known-good fallback: dropped through the world. */
        if (!mover->on_ground && mover->position.y < -20.0f && mover->has_last_good)
        {
            mover->position = mover->last_good_position;
            mover->vertical_velocity = 0.0f;
            mover->on_ground = true;
            mover->view_smooth = 0.0f;
        }
    }

    /* ---- Final ceiling clamp (handles ceilings encountered while
     * walking / step-up). ---- */
    {
        float feet_y = mover->position.y - player_height;
        int containing = sdl3d_level_find_sector_at(level, sectors, mover->position.x, mover->position.z, feet_y);
        if (containing >= 0)
        {
            float ceiling_y = sectors[containing].ceil_y - ceiling_clearance;
            if (mover->position.y > ceiling_y)
            {
                mover->position.y = ceiling_y;
                if (mover->vertical_velocity > 0.0f)
                    mover->vertical_velocity = 0.0f;
            }
        }
    }

    /* ---- View smoothing: accumulate from floor-snap delta (not jumps). ---- */
    {
        float py_delta = mover->position.y - py_before_collision;
        if (mover->on_ground && SDL_fabsf(py_delta) > SDL3D_FPS_VIEW_SMOOTH_EPSILON)
        {
            mover->view_smooth -= py_delta;
        }
    }

    /* ---- Update current_sector and last-known-good. ---- */
    {
        float feet_y = mover->position.y - player_height;
        mover->current_sector =
            sdl3d_level_find_sector_at(level, sectors, mover->position.x, mover->position.z, feet_y);
        if (mover->on_ground && mover->current_sector >= 0)
        {
            mover->last_good_position = mover->position;
            mover->has_last_good = true;
        }
    }
}

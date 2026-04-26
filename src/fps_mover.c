#include "sdl3d/fps_mover.h"

#include <SDL3/SDL_stdinc.h>

#include "sdl3d/math.h"

/* Hard limits / tuning constants kept private. The pitch limit prevents
 * gimbal lock at the poles. The view-smooth decay rate matches the
 * doom_level demo's tuning (higher = snappier). */
#define SDL3D_FPS_PITCH_LIMIT 1.4f
#define SDL3D_FPS_VIEW_SMOOTH_SPEED 12.0f
#define SDL3D_FPS_WALKABLE_NORMAL_Y 0.7f
#define SDL3D_FPS_COLLISION_EPSILON 0.001f
#define SDL3D_FPS_EDGE_SAMPLE_EPSILON 0.02f

/* Stop polling smoothing once the residual is below this threshold; keeps
 * the eye from oscillating around 0 when dt is small. */
#define SDL3D_FPS_VIEW_SMOOTH_EPSILON 0.01f

static float fps_min_headroom(const sdl3d_fps_mover *mover)
{
    return mover->config.player_height + mover->config.ceiling_clearance;
}

static bool sector_floor_is_walkable(const sdl3d_sector *sector)
{
    return sdl3d_sector_floor_normal(sector).y >= SDL3D_FPS_WALKABLE_NORMAL_Y;
}

static float clampf(float value, float min_value, float max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static bool position_has_air_space(const sdl3d_fps_mover *mover, const sdl3d_level *level, const sdl3d_sector *sectors,
                                   float x, float z, float feet_y, int *out_sector);
static bool find_blocking_edge(const sdl3d_fps_mover *mover, const sdl3d_level *level, const sdl3d_sector *sectors,
                               float x, float z, float feet_y, bool grounded, sdl3d_vec2 *out_normal);

static bool position_has_ground_support(const sdl3d_fps_mover *mover, const sdl3d_level *level,
                                        const sdl3d_sector *sectors, float x, float z, float feet_y, int *out_sector,
                                        float *out_collision_feet_y)
{
    const float headroom = fps_min_headroom(mover);
    const float step = mover->config.step_height;

    int center = sdl3d_level_find_walkable_sector(level, sectors, x, z, feet_y, step, headroom);
    if (center < 0)
    {
        return false;
    }
    if (!sector_floor_is_walkable(&sectors[center]))
    {
        return false;
    }

    /* Ground movement follows Build/Quake-style separation of concerns:
     * support and step height are decided at the mover's center, while the
     * player radius is used only to test whether the body has room. This
     * permits normal ledge overhang on raised slopes/ramparts without
     * weakening steep-slope or wall collision. */
    const float target_floor = sdl3d_sector_floor_at(&sectors[center], x, z);
    const float collision_feet_y = SDL_max(feet_y, target_floor);

    if (out_sector)
    {
        *out_sector = center;
    }
    if (out_collision_feet_y)
    {
        *out_collision_feet_y = collision_feet_y;
    }
    return true;
}

static bool position_is_walkable(const sdl3d_fps_mover *mover, const sdl3d_level *level, const sdl3d_sector *sectors,
                                 float x, float z, float feet_y, int *out_sector)
{
    float collision_feet_y = feet_y;
    if (!position_has_ground_support(mover, level, sectors, x, z, feet_y, out_sector, &collision_feet_y))
    {
        return false;
    }

    if (find_blocking_edge(mover, level, sectors, x, z, collision_feet_y, true, NULL))
    {
        return false;
    }

    return true;
}

static bool edge_has_passable_neighbor(const sdl3d_fps_mover *mover, const sdl3d_level *level,
                                       const sdl3d_sector *sectors, float edge_x, float edge_z, float outward_x,
                                       float outward_z, float feet_y, bool grounded)
{
    const float sample_x = edge_x + outward_x * SDL3D_FPS_EDGE_SAMPLE_EPSILON;
    const float sample_z = edge_z + outward_z * SDL3D_FPS_EDGE_SAMPLE_EPSILON;
    const float head_y = feet_y + fps_min_headroom(mover);
    int sector = -1;

    if (grounded)
    {
        sector = sdl3d_level_find_walkable_sector(level, sectors, sample_x, sample_z, feet_y, mover->config.step_height,
                                                  fps_min_headroom(mover));
        if (sector < 0 || !sector_floor_is_walkable(&sectors[sector]))
        {
            return false;
        }
    }
    else
    {
        sector = sdl3d_level_find_sector_at(level, sectors, sample_x, sample_z, feet_y);
        if (sector < 0)
        {
            return false;
        }
    }

    return sdl3d_level_point_inside(level, sectors, sample_x, head_y, sample_z);
}

static bool find_blocking_edge(const sdl3d_fps_mover *mover, const sdl3d_level *level, const sdl3d_sector *sectors,
                               float x, float z, float feet_y, bool grounded, sdl3d_vec2 *out_normal)
{
    bool blocked = false;
    float deepest_penetration = 0.0f;
    sdl3d_vec2 best_normal = {0.0f, 0.0f};

    if (level == NULL || sectors == NULL)
    {
        return false;
    }

    for (int s = 0; s < level->sector_count; ++s)
    {
        const sdl3d_sector *sector = &sectors[s];
        for (int e = 0; e < sector->num_points; ++e)
        {
            const int next = (e + 1) % sector->num_points;
            const float ax = sector->points[e][0];
            const float az = sector->points[e][1];
            const float bx = sector->points[next][0];
            const float bz = sector->points[next][1];
            const float edge_x = bx - ax;
            const float edge_z = bz - az;
            const float edge_len_sq = edge_x * edge_x + edge_z * edge_z;
            if (edge_len_sq <= SDL3D_FPS_COLLISION_EPSILON)
            {
                continue;
            }

            const float edge_len = SDL_sqrtf(edge_len_sq);
            const float inv_edge_len = 1.0f / edge_len;
            const float left_normal_x = -edge_z * inv_edge_len;
            const float left_normal_z = edge_x * inv_edge_len;
            const float signed_dist = ((x - ax) * left_normal_x) + ((z - az) * left_normal_z);
            if (signed_dist >= mover->config.player_radius - SDL3D_FPS_COLLISION_EPSILON)
            {
                continue;
            }

            const float t = clampf(((x - ax) * edge_x + (z - az) * edge_z) / edge_len_sq, 0.0f, 1.0f);
            const float closest_x = ax + edge_x * t;
            const float closest_z = az + edge_z * t;
            const float dx = x - closest_x;
            const float dz = z - closest_z;
            const float closest_dist_sq = dx * dx + dz * dz;
            if (closest_dist_sq > mover->config.player_radius * mover->config.player_radius)
            {
                continue;
            }

            const float outward_x = edge_z * inv_edge_len;
            const float outward_z = -edge_x * inv_edge_len;
            if (edge_has_passable_neighbor(mover, level, sectors, closest_x, closest_z, outward_x, outward_z, feet_y,
                                           grounded))
            {
                continue;
            }

            const float penetration = mover->config.player_radius - signed_dist;
            if (!blocked || penetration > deepest_penetration)
            {
                blocked = true;
                deepest_penetration = penetration;
                best_normal.x = left_normal_x;
                best_normal.y = left_normal_z;
            }
        }
    }

    if (blocked && out_normal != NULL)
    {
        *out_normal = best_normal;
    }
    return blocked;
}

static bool position_has_air_space(const sdl3d_fps_mover *mover, const sdl3d_level *level, const sdl3d_sector *sectors,
                                   float x, float z, float feet_y, int *out_sector)
{
    const float head_y = feet_y + fps_min_headroom(mover);
    int center = sdl3d_level_find_sector_at(level, sectors, x, z, feet_y);

    if (center < 0 || !sdl3d_level_point_inside(level, sectors, x, head_y, z))
    {
        return false;
    }

    if (find_blocking_edge(mover, level, sectors, x, z, feet_y, false, NULL))
    {
        return false;
    }

    if (out_sector)
    {
        *out_sector = center;
    }

    return true;
}

static bool position_has_center_support(const sdl3d_fps_mover *mover, const sdl3d_level *level,
                                        const sdl3d_sector *sectors, float x, float z, float feet_y, bool grounded,
                                        int *out_sector, float *out_collision_feet_y)
{
    if (grounded)
    {
        return position_has_ground_support(mover, level, sectors, x, z, feet_y, out_sector, out_collision_feet_y);
    }

    const float head_y = feet_y + fps_min_headroom(mover);
    int center = sdl3d_level_find_sector_at(level, sectors, x, z, feet_y);
    if (center < 0 || !sdl3d_level_point_inside(level, sectors, x, head_y, z))
    {
        return false;
    }

    if (out_sector)
    {
        *out_sector = center;
    }
    if (out_collision_feet_y)
    {
        *out_collision_feet_y = feet_y;
    }
    return true;
}

static sdl3d_vec2 project_wish_to_walkable_floor(sdl3d_vec2 wish_dir, const sdl3d_sector *sector)
{
    sdl3d_vec3 normal = sdl3d_sector_floor_normal(sector);
    float dot = wish_dir.x * normal.x + wish_dir.y * normal.z;
    sdl3d_vec2 projected = {
        wish_dir.x - normal.x * dot,
        wish_dir.y - normal.z * dot,
    };
    return projected;
}

static bool try_horizontal_move(sdl3d_fps_mover *mover, const sdl3d_level *level, const sdl3d_sector *sectors,
                                float feet_y, bool grounded, float move_x, float move_z, int *out_sector)
{
    bool (*position_is_valid)(const sdl3d_fps_mover *, const sdl3d_level *, const sdl3d_sector *, float, float, float,
                              int *) = grounded ? position_is_walkable : position_has_air_space;
    float clipped_x = move_x;
    float clipped_z = move_z;

    for (int i = 0; i < 3; ++i)
    {
        int candidate = -1;
        const float next_x = mover->position.x + clipped_x;
        const float next_z = mover->position.z + clipped_z;
        if (position_is_valid(mover, level, sectors, next_x, next_z, feet_y, &candidate))
        {
            mover->position.x = next_x;
            mover->position.z = next_z;
            if (out_sector != NULL)
            {
                *out_sector = candidate;
            }
            return true;
        }

        float collision_feet_y = feet_y;
        if (!position_has_center_support(mover, level, sectors, next_x, next_z, feet_y, grounded, &candidate,
                                         &collision_feet_y))
        {
            return false;
        }

        sdl3d_vec2 wall_normal = {0.0f, 0.0f};
        if (!find_blocking_edge(mover, level, sectors, next_x, next_z, collision_feet_y, grounded, &wall_normal))
        {
            return false;
        }

        const float into_wall = clipped_x * wall_normal.x + clipped_z * wall_normal.y;
        if (into_wall >= -SDL3D_FPS_COLLISION_EPSILON)
        {
            mover->position.x = next_x;
            mover->position.z = next_z;
            if (out_sector != NULL)
            {
                *out_sector = candidate;
            }
            return true;
        }

        clipped_x -= wall_normal.x * into_wall;
        clipped_z -= wall_normal.y * into_wall;
        if (clipped_x * clipped_x + clipped_z * clipped_z <= SDL3D_FPS_COLLISION_EPSILON)
        {
            return false;
        }
    }

    return false;
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

void sdl3d_fps_mover_teleport(sdl3d_fps_mover *mover, sdl3d_vec3 eye_position, bool set_yaw, float yaw, bool set_pitch,
                              float pitch)
{
    if (mover == NULL)
    {
        return;
    }

    mover->position = eye_position;
    if (set_yaw)
    {
        mover->yaw = yaw;
    }
    if (set_pitch)
    {
        mover->pitch = pitch;
    }
    mover->vertical_velocity = 0.0f;
    mover->view_smooth = 0.0f;
    mover->on_ground = false;
    mover->current_sector = -1;
    mover->last_good_position = eye_position;
    mover->has_last_good = true;
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
            current_floor = sdl3d_sector_floor_at(&sectors[current_sector], mover->position.x, mover->position.z);
        }

        float wish_len = SDL_sqrtf(wish_dir.x * wish_dir.x + wish_dir.y * wish_dir.y);
        if (wish_len > 1.0f)
        {
            wish_dir.x /= wish_len;
            wish_dir.y /= wish_len;
        }

        if (wish_len > 0.001f)
        {
            if (mover->on_ground && current_sector >= 0 && sector_floor_is_walkable(&sectors[current_sector]))
            {
                wish_dir = project_wish_to_walkable_floor(wish_dir, &sectors[current_sector]);
            }
            float move_x = wish_dir.x * mover->config.move_speed * dt;
            float move_z = wish_dir.y * mover->config.move_speed * dt;
            int candidate = -1;

            if (try_horizontal_move(mover, level, sectors, feet_y, mover->on_ground, move_x, move_z, &candidate))
            {
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
                float target_floor =
                    sdl3d_sector_floor_at(&sectors[current_sector], mover->position.x, mover->position.z);
                if (target_floor < current_floor - step_height)
                {
                    mover->on_ground = false;
                }
                else if (!sector_floor_is_walkable(&sectors[current_sector]))
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
                float ceiling_y = sdl3d_sector_ceil_at(&sectors[containing], mover->position.x, mover->position.z) -
                                  ceiling_clearance;
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
                    float floor_y = sdl3d_sector_floor_at(&sectors[support], mover->position.x, mover->position.z);
                    if (sector_floor_is_walkable(&sectors[support]) && prev_feet_y >= floor_y && feet_y <= floor_y)
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
                    float floor_y = sdl3d_sector_floor_at(&sectors[rescue], mover->position.x, mover->position.z);
                    if (sector_floor_is_walkable(&sectors[rescue]) && floor_y >= feet_y &&
                        (floor_y - feet_y) <= step_height)
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
            float ceiling_y =
                sdl3d_sector_ceil_at(&sectors[containing], mover->position.x, mover->position.z) - ceiling_clearance;
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

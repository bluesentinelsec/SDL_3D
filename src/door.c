/**
 * @file door.c
 * @brief Runtime sliding door primitive implementation.
 */

#include "slayer3d/door.h"

#include <SDL3/SDL_stdinc.h>

static float clamp01(float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

static float clamp_non_negative(float value)
{
    return value > 0.0f ? value : 0.0f;
}

static slayer3d_vec3 vec3_scale(slayer3d_vec3 value, float scale)
{
    return (slayer3d_vec3){value.x * scale, value.y * scale, value.z * scale};
}

static slayer3d_vec3 vec3_add(slayer3d_vec3 left, slayer3d_vec3 right)
{
    return (slayer3d_vec3){left.x + right.x, left.y + right.y, left.z + right.z};
}

static slayer3d_bounding_box bounds_translate(slayer3d_bounding_box bounds, slayer3d_vec3 offset)
{
    bounds.min = vec3_add(bounds.min, offset);
    bounds.max = vec3_add(bounds.max, offset);
    return bounds;
}

static slayer3d_bounding_box panel_current_bounds(const slayer3d_door *door, int panel_index)
{
    const slayer3d_door_panel_desc *panel = &door->panels[panel_index];
    return bounds_translate(panel->closed_bounds, vec3_scale(panel->open_offset, door->open_fraction));
}

static bool vertical_intervals_overlap(float a_min, float a_max, float b_min, float b_max)
{
    return a_min <= b_max && b_min <= a_max;
}

static float distance_to_closed_footprint_xz(const slayer3d_door *door, slayer3d_vec3 point)
{
    slayer3d_bounding_box footprint = door->panels[0].closed_bounds;
    for (int i = 1; i < door->panel_count; ++i)
    {
        const slayer3d_bounding_box panel = door->panels[i].closed_bounds;
        if (panel.min.x < footprint.min.x)
            footprint.min.x = panel.min.x;
        if (panel.max.x > footprint.max.x)
            footprint.max.x = panel.max.x;
        if (panel.min.z < footprint.min.z)
            footprint.min.z = panel.min.z;
        if (panel.max.z > footprint.max.z)
            footprint.max.z = panel.max.z;
    }

    float dx = 0.0f;
    if (point.x < footprint.min.x)
        dx = footprint.min.x - point.x;
    else if (point.x > footprint.max.x)
        dx = point.x - footprint.max.x;

    float dz = 0.0f;
    if (point.z < footprint.min.z)
        dz = footprint.min.z - point.z;
    else if (point.z > footprint.max.z)
        dz = point.z - footprint.max.z;

    return SDL_sqrtf(dx * dx + dz * dz);
}

void slayer3d_door_init(slayer3d_door *door, const slayer3d_door_desc *desc)
{
    if (door == NULL)
        return;

    SDL_zerop(door);
    if (desc == NULL)
        return;

    door->door_id = desc->door_id;
    door->name = desc->name;
    door->panel_count = desc->panel_count;
    if (door->panel_count < 1)
        door->panel_count = 1;
    if (door->panel_count > SLAYER3D_DOOR_MAX_PANELS)
        door->panel_count = SLAYER3D_DOOR_MAX_PANELS;

    for (int i = 0; i < door->panel_count; ++i)
        door->panels[i] = desc->panels[i];

    door->open_seconds = clamp_non_negative(desc->open_seconds);
    door->close_seconds = clamp_non_negative(desc->close_seconds);
    door->stay_open_seconds = clamp_non_negative(desc->stay_open_seconds);
    door->open_fraction = desc->start_open ? 1.0f : 0.0f;
    door->state = desc->start_open ? SLAYER3D_DOOR_OPEN : SLAYER3D_DOOR_CLOSED;
    door->hold_timer = desc->start_open ? door->stay_open_seconds : 0.0f;
    door->enabled = true;
}

bool slayer3d_door_open(slayer3d_door *door)
{
    if (door == NULL || !door->enabled)
        return false;

    if (door->open_fraction >= 1.0f || door->open_seconds <= 0.0f)
    {
        door->open_fraction = 1.0f;
        door->state = SLAYER3D_DOOR_OPEN;
        door->hold_timer = door->stay_open_seconds;
        return true;
    }

    door->state = SLAYER3D_DOOR_OPENING;
    return true;
}

bool slayer3d_door_close(slayer3d_door *door)
{
    if (door == NULL || !door->enabled)
        return false;

    if (door->open_fraction <= 0.0f || door->close_seconds <= 0.0f)
    {
        door->open_fraction = 0.0f;
        door->state = SLAYER3D_DOOR_CLOSED;
        door->hold_timer = 0.0f;
        return true;
    }

    door->state = SLAYER3D_DOOR_CLOSING;
    door->hold_timer = 0.0f;
    return true;
}

bool slayer3d_door_toggle(slayer3d_door *door)
{
    if (door == NULL)
        return false;

    if (door->state == SLAYER3D_DOOR_OPEN || door->state == SLAYER3D_DOOR_OPENING)
        return slayer3d_door_close(door);
    return slayer3d_door_open(door);
}

void slayer3d_door_set_auto_close_delay(slayer3d_door *door, float stay_open_seconds)
{
    if (door == NULL)
        return;

    door->stay_open_seconds = clamp_non_negative(stay_open_seconds);
    if (door->state == SLAYER3D_DOOR_OPEN)
        door->hold_timer = door->stay_open_seconds;
}

void slayer3d_door_update(slayer3d_door *door, float dt)
{
    if (door == NULL || !door->enabled || dt <= 0.0f)
        return;

    switch (door->state)
    {
    case SLAYER3D_DOOR_OPENING:
        if (door->open_seconds <= 0.0f)
        {
            door->open_fraction = 1.0f;
        }
        else
        {
            door->open_fraction = clamp01(door->open_fraction + dt / door->open_seconds);
        }

        if (door->open_fraction >= 1.0f)
        {
            door->state = SLAYER3D_DOOR_OPEN;
            door->hold_timer = door->stay_open_seconds;
        }
        break;

    case SLAYER3D_DOOR_OPEN:
        if (door->stay_open_seconds > 0.0f)
        {
            door->hold_timer -= dt;
            if (door->hold_timer <= 0.0f)
                (void)slayer3d_door_close(door);
        }
        break;

    case SLAYER3D_DOOR_CLOSING:
        if (door->close_seconds <= 0.0f)
        {
            door->open_fraction = 0.0f;
        }
        else
        {
            door->open_fraction = clamp01(door->open_fraction - dt / door->close_seconds);
        }

        if (door->open_fraction <= 0.0f)
        {
            door->state = SLAYER3D_DOOR_CLOSED;
            door->hold_timer = 0.0f;
        }
        break;

    case SLAYER3D_DOOR_CLOSED:
        break;
    }
}

slayer3d_door_state slayer3d_door_get_state(const slayer3d_door *door)
{
    return door != NULL ? door->state : SLAYER3D_DOOR_CLOSED;
}

float slayer3d_door_get_open_fraction(const slayer3d_door *door)
{
    return door != NULL ? clamp01(door->open_fraction) : 0.0f;
}

int slayer3d_door_panel_count(const slayer3d_door *door)
{
    return door != NULL ? door->panel_count : 0;
}

bool slayer3d_door_get_panel_bounds(const slayer3d_door *door, int panel_index, slayer3d_bounding_box *out_bounds)
{
    if (door == NULL || out_bounds == NULL || panel_index < 0 || panel_index >= door->panel_count)
        return false;

    *out_bounds = panel_current_bounds(door, panel_index);
    return true;
}

bool slayer3d_door_point_in_interaction_range(const slayer3d_door *door, slayer3d_vec3 point, float range)
{
    if (door == NULL || !door->enabled || door->panel_count <= 0 || range < 0.0f)
        return false;

    return distance_to_closed_footprint_xz(door, point) <= range;
}

bool slayer3d_door_intersects_cylinder(const slayer3d_door *door, slayer3d_vec3 eye_position, float height,
                                       float radius)
{
    if (door == NULL || door->panel_count <= 0 || height <= 0.0f || radius < 0.0f)
        return false;

    const float cyl_min_y = eye_position.y - height;
    const float cyl_max_y = eye_position.y;
    for (int i = 0; i < door->panel_count; ++i)
    {
        const slayer3d_bounding_box panel = panel_current_bounds(door, i);
        if (!vertical_intervals_overlap(cyl_min_y, cyl_max_y, panel.min.y, panel.max.y))
            continue;

        if (eye_position.x >= panel.min.x - radius && eye_position.x <= panel.max.x + radius &&
            eye_position.z >= panel.min.z - radius && eye_position.z <= panel.max.z + radius)
        {
            return true;
        }
    }
    return false;
}

bool slayer3d_door_resolve_cylinder(const slayer3d_door *door, slayer3d_vec3 *eye_position, float height, float radius)
{
    if (door == NULL || eye_position == NULL || door->panel_count <= 0 || height <= 0.0f || radius < 0.0f)
        return false;

    bool moved = false;
    const float cyl_min_y = eye_position->y - height;
    const float cyl_max_y = eye_position->y;
    for (int i = 0; i < door->panel_count; ++i)
    {
        const slayer3d_bounding_box panel = panel_current_bounds(door, i);
        if (!vertical_intervals_overlap(cyl_min_y, cyl_max_y, panel.min.y, panel.max.y))
            continue;

        const float min_x = panel.min.x - radius;
        const float max_x = panel.max.x + radius;
        const float min_z = panel.min.z - radius;
        const float max_z = panel.max.z + radius;
        if (eye_position->x < min_x || eye_position->x > max_x || eye_position->z < min_z || eye_position->z > max_z)
        {
            continue;
        }

        const float push_left = min_x - eye_position->x;
        const float push_right = max_x - eye_position->x;
        const float push_back = min_z - eye_position->z;
        const float push_forward = max_z - eye_position->z;
        float best_push = push_left;
        bool push_x = true;
        if (SDL_fabsf(push_right) < SDL_fabsf(best_push))
            best_push = push_right;
        if (SDL_fabsf(push_back) < SDL_fabsf(best_push))
        {
            best_push = push_back;
            push_x = false;
        }
        if (SDL_fabsf(push_forward) < SDL_fabsf(best_push))
        {
            best_push = push_forward;
            push_x = false;
        }

        const float slop = best_push < 0.0f ? -0.0001f : 0.0001f;
        if (push_x)
            eye_position->x += best_push + slop;
        else
            eye_position->z += best_push + slop;
        moved = true;
    }

    return moved;
}

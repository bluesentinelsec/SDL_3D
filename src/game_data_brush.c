/**
 * @file game_data_brush.c
 * @brief Authored brush-world trace and sliding helpers for game-data runtimes.
 */

#include "game_data_brush_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/math.h"

static bool brush_plane_normalized(const slayer3d_game_data_brush_face *face, slayer3d_vec3 *out_normal,
                                   float *out_distance)
{
    if (face == NULL || out_normal == NULL || out_distance == NULL)
        return false;
    const float len = slayer3d_vec3_length(face->normal);
    if (len <= 0.000001f)
        return false;
    *out_normal = slayer3d_vec3_scale(face->normal, 1.0f / len);
    *out_distance = face->distance / len;
    return true;
}

slayer3d_game_data_brush_trace_result slayer3d_game_data_brush_trace_default_result(
    const slayer3d_game_data_brush_trace_desc *desc)
{
    slayer3d_game_data_brush_trace_result result;
    SDL_zero(result);
    result.fraction = 1.0f;
    result.end_position = desc != NULL ? desc->end : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    result.point = result.end_position;
    result.brush_index = -1;
    result.face_index = -1;
    return result;
}

static float brush_trace_plane_support(const slayer3d_game_data_brush_trace_desc *desc, slayer3d_vec3 normal)
{
    if (desc == NULL)
        return 0.0f;
    if (desc->shape == SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE)
        return SDL_max(desc->extents.x, 0.0f);
    if (desc->shape == SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB)
    {
        return SDL_fabsf(normal.x) * SDL_max(desc->extents.x, 0.0f) +
               SDL_fabsf(normal.y) * SDL_max(desc->extents.y, 0.0f) +
               SDL_fabsf(normal.z) * SDL_max(desc->extents.z, 0.0f);
    }
    return 0.0f;
}

bool slayer3d_game_data_brush_trace_shape_valid(const slayer3d_game_data_brush_trace_desc *desc)
{
    if (desc == NULL || desc->contents_mask == 0u)
        return false;
    switch (desc->shape)
    {
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT:
        return true;
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE:
        return desc->extents.x >= 0.0f;
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB:
        return desc->extents.x >= 0.0f && desc->extents.y >= 0.0f && desc->extents.z >= 0.0f;
    default:
        return false;
    }
}

static bool brush_trace_one_brush(const slayer3d_game_data_brush *brush,
                                  const slayer3d_game_data_brush_trace_desc *desc,
                                  slayer3d_game_data_brush_trace_result *out_result)
{
    const float epsilon = 0.0005f;
    bool starts_outside = false;
    bool ends_outside = false;
    float enter_fraction = -1.0f;
    float leave_fraction = 1.0f;
    int enter_face = -1;
    slayer3d_vec3 enter_normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);

    if (brush == NULL || desc == NULL || out_result == NULL || (brush->contents & desc->contents_mask) == 0u)
        return false;

    for (int face_index = 0; face_index < brush->face_count; ++face_index)
    {
        const slayer3d_game_data_brush_face *face = &brush->faces[face_index];
        if ((face->surface_flags & SLAYER3D_GAME_DATA_BRUSH_SURFACE_NO_COLLIDE) != 0u)
            continue;

        slayer3d_vec3 normal;
        float distance = 0.0f;
        if (!brush_plane_normalized(face, &normal, &distance))
            continue;

        const float expanded_distance = distance + brush_trace_plane_support(desc, normal);
        const float start_distance = slayer3d_vec3_dot(normal, desc->start) - expanded_distance;
        const float end_distance = slayer3d_vec3_dot(normal, desc->end) - expanded_distance;

        if (start_distance > epsilon)
            starts_outside = true;
        if (end_distance > epsilon)
            ends_outside = true;

        if (start_distance > epsilon && end_distance > epsilon)
            return false;
        if (start_distance <= epsilon && end_distance <= epsilon)
            continue;

        if (start_distance > end_distance)
        {
            const float denom = start_distance - end_distance;
            if (denom > 0.000001f)
            {
                float fraction = (start_distance - epsilon) / denom;
                fraction = SDL_clamp(fraction, 0.0f, 1.0f);
                if (fraction > enter_fraction)
                {
                    enter_fraction = fraction;
                    enter_face = face_index;
                    enter_normal = normal;
                }
            }
        }
        else
        {
            const float denom = start_distance - end_distance;
            if (SDL_fabsf(denom) > 0.000001f)
            {
                float fraction = (start_distance + epsilon) / denom;
                fraction = SDL_clamp(fraction, 0.0f, 1.0f);
                if (fraction < leave_fraction)
                    leave_fraction = fraction;
            }
        }
    }

    if (!starts_outside)
    {
        out_result->hit = true;
        out_result->start_solid = true;
        out_result->all_solid = !ends_outside;
        out_result->fraction = 0.0f;
        out_result->end_position = desc->start;
        out_result->point = desc->start;
        out_result->normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        out_result->face_index = -1;
        return true;
    }

    if (enter_face >= 0 && enter_fraction >= 0.0f && enter_fraction <= leave_fraction && enter_fraction <= 1.0f)
    {
        out_result->hit = true;
        out_result->fraction = enter_fraction;
        out_result->end_position = slayer3d_vec3_lerp(desc->start, desc->end, enter_fraction);
        out_result->point = out_result->end_position;
        out_result->normal = enter_normal;
        out_result->face_index = enter_face;
        return true;
    }

    return false;
}

bool slayer3d_game_data_brush_world_trace_local(const slayer3d_game_data_brush_world *world,
                                                const slayer3d_game_data_brush_trace_desc *desc,
                                                slayer3d_game_data_brush_trace_result *out_result)
{
    slayer3d_game_data_brush_trace_result closest = slayer3d_game_data_brush_trace_default_result(desc);
    if (world == NULL || desc == NULL || out_result == NULL || !slayer3d_game_data_brush_trace_shape_valid(desc))
        return false;

    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        slayer3d_game_data_brush_trace_result candidate = slayer3d_game_data_brush_trace_default_result(desc);
        if (!brush_trace_one_brush(brush, desc, &candidate))
            continue;
        if (!closest.hit || candidate.fraction < closest.fraction || (candidate.start_solid && !closest.start_solid))
        {
            closest = candidate;
            closest.world_name = world->name;
            closest.brush_name = brush->name;
            closest.brush_index = brush_index;
            closest.contents = brush->contents;
            if (closest.face_index >= 0 && closest.face_index < brush->face_count)
            {
                closest.material_name = brush->faces[closest.face_index].material_name;
                closest.surface_flags = brush->faces[closest.face_index].surface_flags;
            }
            else
            {
                closest.material_name = NULL;
                closest.surface_flags = 0u;
            }
        }
    }

    *out_result = closest;
    return true;
}

static slayer3d_vec3 brush_clip_velocity(slayer3d_vec3 velocity, slayer3d_vec3 normal)
{
    const float overclip = 1.001f;
    float backoff = slayer3d_vec3_dot(velocity, normal);
    backoff = backoff < 0.0f ? backoff * overclip : backoff / overclip;
    return slayer3d_vec3_sub(velocity, slayer3d_vec3_scale(normal, backoff));
}

bool slayer3d_game_data_brush_slide_with_trace(slayer3d_game_data_brush_trace_fn trace_fn, void *trace_userdata,
                                               const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                               slayer3d_game_data_brush_trace_result *out_result)
{
    enum
    {
        MAX_CLIP_PLANES = 5
    };
    if (out_result != NULL)
        *out_result = slayer3d_game_data_brush_trace_default_result(desc);
    if (trace_fn == NULL || desc == NULL || out_result == NULL || !slayer3d_game_data_brush_trace_shape_valid(desc))
        return false;

    slayer3d_vec3 position = desc->start;
    slayer3d_vec3 velocity = slayer3d_vec3_sub(desc->end, desc->start);
    slayer3d_vec3 planes[MAX_CLIP_PLANES];
    int plane_count = 0;
    float time_left = 1.0f;
    slayer3d_game_data_brush_trace_result first_hit = slayer3d_game_data_brush_trace_default_result(desc);
    const int bump_count = SDL_clamp(max_bumps, 1, 8);

    if (slayer3d_vec3_length_squared(velocity) > 0.000001f)
        planes[plane_count++] = slayer3d_vec3_normalize(velocity);

    for (int bump = 0; bump < bump_count; ++bump)
    {
        slayer3d_game_data_brush_trace_desc step = *desc;
        step.start = position;
        step.end = slayer3d_vec3_add(position, slayer3d_vec3_scale(velocity, time_left));

        slayer3d_game_data_brush_trace_result trace;
        if (!trace_fn(trace_userdata, &step, &trace))
            return false;

        if (trace.fraction > 0.0f)
            position = trace.end_position;
        if (!trace.hit)
            break;
        if (!first_hit.hit)
            first_hit = trace;
        if (trace.start_solid)
        {
            position = trace.end_position;
            break;
        }
        position = slayer3d_vec3_add(position, slayer3d_vec3_scale(trace.normal, 0.001f));

        time_left -= time_left * SDL_clamp(trace.fraction, 0.0f, 1.0f);
        if (time_left <= 0.000001f)
            break;

        bool duplicate_plane = false;
        for (int i = 0; i < plane_count; ++i)
        {
            if (slayer3d_vec3_dot(trace.normal, planes[i]) > 0.99f)
            {
                velocity = slayer3d_vec3_add(velocity, trace.normal);
                duplicate_plane = true;
                break;
            }
        }
        if (duplicate_plane)
            continue;

        if (plane_count >= MAX_CLIP_PLANES)
        {
            velocity = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
            break;
        }
        planes[plane_count++] = trace.normal;

        slayer3d_vec3 clipped_velocity = velocity;
        for (int i = 0; i < plane_count; ++i)
        {
            if (slayer3d_vec3_dot(clipped_velocity, planes[i]) >= 0.1f)
                continue;

            clipped_velocity = brush_clip_velocity(velocity, planes[i]);

            for (int j = 0; j < plane_count; ++j)
            {
                if (j == i || slayer3d_vec3_dot(clipped_velocity, planes[j]) >= 0.1f)
                    continue;

                clipped_velocity = brush_clip_velocity(clipped_velocity, planes[j]);
                if (slayer3d_vec3_dot(clipped_velocity, planes[i]) >= 0.0f)
                    continue;

                slayer3d_vec3 crease = slayer3d_vec3_cross(planes[i], planes[j]);
                if (slayer3d_vec3_length_squared(crease) <= 0.000001f)
                {
                    clipped_velocity = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
                    break;
                }
                crease = slayer3d_vec3_normalize(crease);
                clipped_velocity = slayer3d_vec3_scale(crease, slayer3d_vec3_dot(crease, velocity));

                for (int k = 0; k < plane_count; ++k)
                {
                    if (k != i && k != j && slayer3d_vec3_dot(clipped_velocity, planes[k]) < 0.1f)
                    {
                        clipped_velocity = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
                        break;
                    }
                }
                break;
            }
            break;
        }

        velocity = clipped_velocity;
        if (slayer3d_vec3_length_squared(velocity) <= 0.000001f)
            break;
    }

    if (first_hit.hit)
    {
        *out_result = first_hit;
        out_result->end_position = position;
        out_result->point = position;
    }
    else
    {
        *out_result = slayer3d_game_data_brush_trace_default_result(desc);
        out_result->end_position = position;
        out_result->point = position;
    }
    return true;
}

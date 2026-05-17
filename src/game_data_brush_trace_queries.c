/**
 * @file game_data_brush_trace_queries.c
 * @brief Brush-world trace and slide query helpers.
 */

#include "game_data_internal.h"

#include "slayer3d/collision.h"

bool slayer3d_game_data_trace_brush_world(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                          const slayer3d_game_data_brush_trace_desc *desc,
                                          slayer3d_game_data_brush_trace_result *out_result)
{
    if (out_result != NULL)
        *out_result = slayer3d_game_data_brush_trace_default_result(desc);
    if (runtime == NULL || world_name == NULL || desc == NULL || out_result == NULL)
        return false;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
        return false;
    return slayer3d_game_data_brush_world_trace_local_with_diagnostics(
        &world_runtime->desc, desc, true, out_result, &((slayer3d_game_data_runtime *)runtime)->brush_diagnostics);
}

typedef struct brush_scene_trace_context
{
    const slayer3d_game_data_runtime *runtime;
    const slayer3d_game_data_brush_trace_desc *world_desc;
    slayer3d_game_data_brush_trace_result closest;
    bool ok;
} brush_scene_trace_context;

static bool trace_brush_world_instance(void *userdata, const slayer3d_game_data_brush_world_instance *instance)
{
    brush_scene_trace_context *context = (brush_scene_trace_context *)userdata;
    if (context == NULL || context->runtime == NULL || context->world_desc == NULL || instance == NULL ||
        instance->world_name == NULL)
    {
        if (context != NULL)
            context->ok = false;
        return false;
    }

    slayer3d_game_data_brush_trace_desc local_desc = *context->world_desc;
    local_desc.start = slayer3d_vec3_sub(local_desc.start, instance->position);
    local_desc.end = slayer3d_vec3_sub(local_desc.end, instance->position);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(context->runtime, instance->world_name);
    if (world_runtime == NULL)
    {
        context->ok = false;
        return false;
    }
    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)context->runtime;
    ++mutable_runtime->brush_diagnostics.world_instance_count;
    if (instance->acceleration_enabled && world_runtime->desc.has_bounds)
    {
        const slayer3d_bounding_box trace_bounds = slayer3d_game_data_brush_trace_bounds(&local_desc);
        if (!slayer3d_check_aabb_aabb(trace_bounds, world_runtime->desc.bounds))
        {
            ++mutable_runtime->brush_diagnostics.world_bounds_reject_count;
            return true;
        }
    }

    slayer3d_game_data_brush_trace_result local_result;
    if (!slayer3d_game_data_brush_world_trace_local_with_diagnostics(&world_runtime->desc, &local_desc,
                                                                     instance->acceleration_enabled, &local_result,
                                                                     &mutable_runtime->brush_diagnostics))
    {
        context->ok = false;
        return false;
    }

    if (local_result.hit && (!context->closest.hit || local_result.fraction < context->closest.fraction ||
                             (local_result.start_solid && !context->closest.start_solid)))
    {
        local_result.end_position = slayer3d_vec3_add(local_result.end_position, instance->position);
        local_result.point = slayer3d_vec3_add(local_result.point, instance->position);
        local_result.world_position = instance->position;
        context->closest = local_result;
    }
    return true;
}

bool slayer3d_game_data_trace_active_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_brush_trace_desc *desc,
                                                  slayer3d_game_data_brush_trace_result *out_result)
{
    if (out_result != NULL)
        *out_result = slayer3d_game_data_brush_trace_default_result(desc);
    if (runtime == NULL || desc == NULL || out_result == NULL || !slayer3d_game_data_brush_trace_shape_valid(desc))
        return false;

    brush_scene_trace_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.world_desc = desc;
    context.closest = slayer3d_game_data_brush_trace_default_result(desc);
    context.ok = true;
    if (!slayer3d_game_data_for_each_brush_world_instance(runtime, trace_brush_world_instance, &context) || !context.ok)
        return false;

    *out_result = context.closest;
    return true;
}

typedef struct brush_named_trace_context
{
    const slayer3d_game_data_runtime *runtime;
    const char *world_name;
} brush_named_trace_context;

static bool brush_slide_trace_named(void *userdata, const slayer3d_game_data_brush_trace_desc *desc,
                                    slayer3d_game_data_brush_trace_result *out_result)
{
    const brush_named_trace_context *context = (const brush_named_trace_context *)userdata;
    if (context == NULL)
        return false;
    return slayer3d_game_data_trace_brush_world(context->runtime, context->world_name, desc, out_result);
}

static bool brush_slide_trace_active(void *userdata, const slayer3d_game_data_brush_trace_desc *desc,
                                     slayer3d_game_data_brush_trace_result *out_result)
{
    const slayer3d_game_data_runtime *runtime = (const slayer3d_game_data_runtime *)userdata;
    return slayer3d_game_data_trace_active_brush_worlds(runtime, desc, out_result);
}

bool slayer3d_game_data_slide_brush_world(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                          const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                          slayer3d_game_data_brush_trace_result *out_result)
{
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0')
        return false;
    brush_named_trace_context context;
    context.runtime = runtime;
    context.world_name = world_name;
    return slayer3d_game_data_brush_slide_with_trace(brush_slide_trace_named, &context, desc, max_bumps, out_result);
}

bool slayer3d_game_data_slide_active_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                                  slayer3d_game_data_brush_trace_result *out_result)
{
    if (runtime == NULL)
        return false;
    return slayer3d_game_data_brush_slide_with_trace(brush_slide_trace_active, (void *)runtime, desc, max_bumps,
                                                     out_result);
}

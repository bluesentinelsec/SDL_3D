/**
 * @file game_data_world_diagnostics.c
 * @brief Brush and world-model diagnostic reporting helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

bool slayer3d_game_data_get_brush_diagnostics(const slayer3d_game_data_runtime *runtime,
                                              slayer3d_game_data_brush_diagnostics *out_diagnostics)
{
    if (runtime == NULL || out_diagnostics == NULL)
        return false;
    *out_diagnostics = runtime->brush_diagnostics;
    for (int i = 0; i < runtime->brush_world_count; ++i)
    {
        const slayer3d_game_data_brush_world *world = &runtime->brush_worlds[i].desc;
        out_diagnostics->compile_face_count += (Uint64)SDL_max(world->compile_face_count, 0);
        out_diagnostics->compile_rendered_face_count += (Uint64)SDL_max(world->compile_rendered_face_count, 0);
        out_diagnostics->compile_culled_face_count += (Uint64)SDL_max(world->compile_culled_face_count, 0);
        out_diagnostics->compile_triangle_count += (Uint64)SDL_max(world->compile_triangle_count, 0);
        out_diagnostics->compile_invalid_brush_count += (Uint64)SDL_max(world->compile_invalid_brush_count, 0);
        out_diagnostics->compile_degenerate_face_count += (Uint64)SDL_max(world->compile_degenerate_face_count, 0);
        out_diagnostics->compile_chunk_count += (Uint64)SDL_max(world->compile_chunk_count, 0);
    }
    return true;
}

void slayer3d_game_data_reset_brush_diagnostics(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    SDL_zero(runtime->brush_diagnostics);
}

void slayer3d_game_data_accumulate_brush_render_diagnostics(slayer3d_game_data_runtime *runtime,
                                                            const slayer3d_render_stats *before,
                                                            const slayer3d_render_stats *after)
{
    if (runtime == NULL || before == NULL || after == NULL)
        return;

    runtime->brush_diagnostics.render_mesh_submissions +=
        after->model_mesh_submissions >= before->model_mesh_submissions
            ? after->model_mesh_submissions - before->model_mesh_submissions
            : 0u;
    runtime->brush_diagnostics.render_mesh_culled += after->model_mesh_culled >= before->model_mesh_culled
                                                         ? after->model_mesh_culled - before->model_mesh_culled
                                                         : 0u;
    runtime->brush_diagnostics.render_mesh_draws +=
        after->model_mesh_draws >= before->model_mesh_draws ? after->model_mesh_draws - before->model_mesh_draws : 0u;
    runtime->brush_diagnostics.render_triangles_submitted +=
        after->model_triangles_submitted >= before->model_triangles_submitted
            ? after->model_triangles_submitted - before->model_triangles_submitted
            : 0u;
}

typedef struct world_model_diagnostics_count_context
{
    Uint64 sector_count;
    Uint64 brush_count;
} world_model_diagnostics_count_context;

static bool count_sector_world_instance(void *userdata, const slayer3d_game_data_sector_level_instance *instance)
{
    (void)instance;
    world_model_diagnostics_count_context *context = (world_model_diagnostics_count_context *)userdata;
    if (context != NULL)
        ++context->sector_count;
    return true;
}

static bool count_brush_world_instance(void *userdata, const slayer3d_game_data_brush_world_instance *instance)
{
    (void)instance;
    world_model_diagnostics_count_context *context = (world_model_diagnostics_count_context *)userdata;
    if (context != NULL)
        ++context->brush_count;
    return true;
}

bool slayer3d_game_data_get_world_model_diagnostics(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_world_model_diagnostics *out_diagnostics)
{
    if (runtime == NULL || out_diagnostics == NULL)
        return false;

    SDL_zero(*out_diagnostics);
    world_model_diagnostics_count_context context;
    SDL_zero(context);
    if (!slayer3d_game_data_for_each_sector_level_instance(runtime, count_sector_world_instance, &context) ||
        !slayer3d_game_data_for_each_brush_world_instance(runtime, count_brush_world_instance, &context))
    {
        return false;
    }
    out_diagnostics->active_sector_level_instances = context.sector_count;
    out_diagnostics->active_brush_world_instances = context.brush_count;
    out_diagnostics->world_trace_count = runtime->world_model_trace_count;
    out_diagnostics->point_query_count = runtime->world_model_point_query_count;
    out_diagnostics->brush = runtime->brush_diagnostics;
    return true;
}

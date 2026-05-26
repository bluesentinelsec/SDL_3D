/**
 * @file game_presentation_brush_worlds.c
 * @brief Brush-world rendering and visibility helpers for game presentation.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/collision.h"
#include "slayer3d/drawing3d.h"
#include "slayer3d/lighting.h"
#include "slayer3d/math.h"

#include "game_data_internal.h"
#include "render_context_internal.h"

typedef struct brush_world_draw_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_render_context *renderer;
    const slayer3d_asset_resolver *assets;
    const slayer3d_camera3d *camera;
    bool ok;
} brush_world_draw_context;

static bool draw_brush_world_instance(void *userdata, const slayer3d_game_data_brush_world_instance *instance)
{
    brush_world_draw_context *context = (brush_world_draw_context *)userdata;
    if (context == NULL || context->renderer == NULL || instance == NULL || instance->world == NULL)
        return false;
    if (instance->world->render_model == NULL || instance->world->render_model->mesh_count <= 0)
        return true;

    bool pushed = false;
    bool ok = true;
    if (instance->position.x != 0.0f || instance->position.y != 0.0f || instance->position.z != 0.0f)
    {
        if (!slayer3d_push_matrix(context->renderer))
        {
            context->ok = false;
            return false;
        }
        pushed = true;
        ok = slayer3d_translate(context->renderer, instance->position.x, instance->position.y, instance->position.z);
    }

    const bool restore_lighting = slayer3d_is_lighting_enabled(context->renderer);
    if (!instance->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, false);

    if (ok)
    {
        ok = slayer3d_draw_model_ex_with_assets(
            context->renderer, context->assets, instance->world->render_model, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
            slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
            (slayer3d_color){255, 255, 255, 255});
    }
    if (ok && instance->debug_wireframe && !slayer3d_is_wireframe_enabled(context->renderer))
    {
        if (slayer3d_set_wireframe_enabled(context->renderer, true))
        {
            ok = slayer3d_draw_model_ex_with_assets(
                context->renderer, context->assets, instance->world->render_model, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
                (slayer3d_color){30, 35, 40, 220});
            if (!slayer3d_set_wireframe_enabled(context->renderer, false))
                ok = false;
        }
        else
        {
            ok = false;
        }
    }
    if (!instance->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, restore_lighting);
    if (pushed && !slayer3d_pop_matrix(context->renderer))
        ok = false;
    if (!ok)
        context->ok = false;
    return context->ok;
}

static Uint64 brush_model_triangle_count(const slayer3d_model *model)
{
    if (model == NULL || model->meshes == NULL || model->mesh_count <= 0)
        return 0u;
    Uint64 triangles = 0u;
    for (int mesh_index = 0; mesh_index < model->mesh_count; ++mesh_index)
    {
        const slayer3d_mesh *mesh = &model->meshes[mesh_index];
        triangles += (Uint64)((mesh->index_count > 0 ? mesh->index_count : mesh->vertex_count) / 3);
    }
    return triangles;
}

static slayer3d_vec3 brush_bounds_sample(const slayer3d_bounding_box *bounds, int sample_index)
{
    if (bounds == NULL)
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (sample_index <= 0)
    {
        return slayer3d_vec3_scale(slayer3d_vec3_add(bounds->min, bounds->max), 0.5f);
    }
    const int corner = sample_index - 1;
    return slayer3d_vec3_make((corner & 1) != 0 ? bounds->max.x : bounds->min.x,
                              (corner & 2) != 0 ? bounds->max.y : bounds->min.y,
                              (corner & 4) != 0 ? bounds->max.z : bounds->min.z);
}

static bool point_inside_bounds(slayer3d_vec3 point, const slayer3d_bounding_box *bounds)
{
    if (bounds == NULL)
        return false;
    return point.x >= bounds->min.x && point.x <= bounds->max.x && point.y >= bounds->min.y &&
           point.y <= bounds->max.y && point.z >= bounds->min.z && point.z <= bounds->max.z;
}

static bool brush_visibility_sample_occluded(const slayer3d_game_data_brush_world_instance *instance,
                                             const slayer3d_game_data_brush *brush, slayer3d_vec3 local_camera,
                                             slayer3d_vec3 sample)
{
    if (instance == NULL || instance->world == NULL || brush == NULL)
        return false;
    if (slayer3d_vec3_length_squared(slayer3d_vec3_sub(sample, local_camera)) <= 0.0001f)
        return false;

    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.contents_mask = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER |
                          SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA | SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY;
    trace.start = local_camera;
    trace.end = sample;

    slayer3d_game_data_brush_trace_result result;
    if (!slayer3d_game_data_brush_world_trace_local_with_diagnostics(instance->world, &trace,
                                                                     instance->acceleration_enabled, &result, NULL) ||
        !result.hit)
    {
        return false;
    }
    if (brush->name != NULL && result.brush_name != NULL && SDL_strcmp(brush->name, result.brush_name) == 0)
        return false;
    return result.fraction < 0.995f;
}

static bool brush_occluded_from_camera(const slayer3d_game_data_brush_world_instance *instance,
                                       const slayer3d_game_data_brush *brush, const slayer3d_camera3d *camera)
{
    if (instance == NULL || brush == NULL || camera == NULL || !brush->has_bounds)
        return false;
    const slayer3d_vec3 local_camera = slayer3d_vec3_sub(camera->position, instance->position);
    if (point_inside_bounds(local_camera, &brush->bounds))
        return false;

    for (int sample_index = 0; sample_index < 9; ++sample_index)
    {
        const slayer3d_vec3 sample = brush_bounds_sample(&brush->bounds, sample_index);
        if (!brush_visibility_sample_occluded(instance, brush, local_camera, sample))
            return false;
    }
    return true;
}

static bool brush_visibility_grid_cell(const brush_world_runtime *world_runtime, slayer3d_vec3 point, int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (world_runtime == NULL || world_runtime->artifacts.visibility_grid_solid == NULL ||
        world_runtime->artifacts.visibility_grid_cell_count <= 0 ||
        world_runtime->artifacts.visibility_cell_size <= 0.0f)
    {
        return false;
    }
    const float cell_size = world_runtime->artifacts.visibility_cell_size;
    const int x = (int)SDL_floorf((point.x - world_runtime->artifacts.visibility_grid_bounds.min.x) / cell_size);
    const int y = (int)SDL_floorf((point.y - world_runtime->artifacts.visibility_grid_bounds.min.y) / cell_size);
    const int z = (int)SDL_floorf((point.z - world_runtime->artifacts.visibility_grid_bounds.min.z) / cell_size);
    if (x < 0 || y < 0 || z < 0 || x >= world_runtime->artifacts.visibility_grid_dim_x ||
        y >= world_runtime->artifacts.visibility_grid_dim_y || z >= world_runtime->artifacts.visibility_grid_dim_z)
    {
        return false;
    }
    const int index =
        x + y * world_runtime->artifacts.visibility_grid_dim_x +
        z * world_runtime->artifacts.visibility_grid_dim_x * world_runtime->artifacts.visibility_grid_dim_y;
    if (index < 0 || index >= world_runtime->artifacts.visibility_grid_cell_count)
        return false;
    if (out_index != NULL)
        *out_index = index;
    return true;
}

static void brush_visibility_grid_cell_coords(const brush_world_runtime *world_runtime, int index, int *out_x,
                                              int *out_y, int *out_z)
{
    const int dim_x = world_runtime != NULL ? world_runtime->artifacts.visibility_grid_dim_x : 1;
    const int dim_y = world_runtime != NULL ? world_runtime->artifacts.visibility_grid_dim_y : 1;
    const int z = index / (dim_x * dim_y);
    const int rem = index - z * dim_x * dim_y;
    const int y = rem / dim_x;
    const int x = rem - y * dim_x;
    if (out_x != NULL)
        *out_x = x;
    if (out_y != NULL)
        *out_y = y;
    if (out_z != NULL)
        *out_z = z;
}

static slayer3d_vec3 brush_visibility_grid_cell_center(const brush_world_runtime *world_runtime, int x, int y, int z)
{
    const float cell_size = world_runtime->artifacts.visibility_cell_size;
    return slayer3d_vec3_make(world_runtime->artifacts.visibility_grid_bounds.min.x + ((float)x + 0.5f) * cell_size,
                              world_runtime->artifacts.visibility_grid_bounds.min.y + ((float)y + 0.5f) * cell_size,
                              world_runtime->artifacts.visibility_grid_bounds.min.z + ((float)z + 0.5f) * cell_size);
}

static bool brush_visibility_grid_ray_clear(const brush_world_runtime *world_runtime, int start_index,
                                            slayer3d_vec3 local_camera, slayer3d_vec3 target)
{
    if (world_runtime == NULL || world_runtime->artifacts.visibility_grid_solid == NULL)
        return false;
    const slayer3d_vec3 delta = slayer3d_vec3_sub(target, local_camera);
    const float distance = slayer3d_vec3_length(delta);
    if (distance <= 0.0001f)
        return true;
    const float cell_size = world_runtime->artifacts.visibility_cell_size;
    const int steps = SDL_max(1, SDL_min(4096, (int)SDL_ceilf(distance / SDL_max(cell_size * 0.35f, 0.05f))));
    for (int step = 1; step <= steps; ++step)
    {
        const float t = (float)step / (float)steps;
        const slayer3d_vec3 point = slayer3d_vec3_add(local_camera, slayer3d_vec3_scale(delta, t));
        int index = -1;
        if (!brush_visibility_grid_cell(world_runtime, point, &index))
            return false;
        if (index == start_index)
            continue;
        if (world_runtime->artifacts.visibility_grid_solid[index])
            return false;
    }
    return true;
}

static void brush_visibility_grid_mark_neighborhood_visible(const brush_world_runtime *world_runtime, int start,
                                                            Uint8 *visible_cells)
{
    int sx = 0;
    int sy = 0;
    int sz = 0;
    brush_visibility_grid_cell_coords(world_runtime, start, &sx, &sy, &sz);
    for (int z = sz - 1; z <= sz + 1; ++z)
    {
        for (int y = sy - 1; y <= sy + 1; ++y)
        {
            for (int x = sx - 1; x <= sx + 1; ++x)
            {
                if (x < 0 || y < 0 || z < 0 || x >= world_runtime->artifacts.visibility_grid_dim_x ||
                    y >= world_runtime->artifacts.visibility_grid_dim_y ||
                    z >= world_runtime->artifacts.visibility_grid_dim_z)
                {
                    continue;
                }
                const int index =
                    x + y * world_runtime->artifacts.visibility_grid_dim_x +
                    z * world_runtime->artifacts.visibility_grid_dim_x * world_runtime->artifacts.visibility_grid_dim_y;
                if (index >= 0 && index < world_runtime->artifacts.visibility_grid_cell_count &&
                    !world_runtime->artifacts.visibility_grid_solid[index])
                {
                    visible_cells[index] = 1u;
                }
            }
        }
    }
}

static bool brush_visibility_grid_mark_visible_los(const brush_world_runtime *world_runtime, slayer3d_vec3 local_camera,
                                                   Uint8 *visible_cells)
{
    int start = -1;
    if (world_runtime == NULL || visible_cells == NULL ||
        !brush_visibility_grid_cell(world_runtime, local_camera, &start) || start < 0 ||
        world_runtime->artifacts.visibility_grid_solid[start])
    {
        return false;
    }

    static const int max_los_cells = 131072;
    if (world_runtime->artifacts.visibility_grid_cell_count > max_los_cells)
        return false;

    visible_cells[start] = 1u;
    brush_visibility_grid_mark_neighborhood_visible(world_runtime, start, visible_cells);
    const int dim_x = world_runtime->artifacts.visibility_grid_dim_x;
    const int dim_y = world_runtime->artifacts.visibility_grid_dim_y;
    const int dim_z = world_runtime->artifacts.visibility_grid_dim_z;
    for (int z = 0; z < dim_z; ++z)
    {
        for (int y = 0; y < dim_y; ++y)
        {
            for (int x = 0; x < dim_x; ++x)
            {
                const int index = x + y * dim_x + z * dim_x * dim_y;
                if (visible_cells[index] || world_runtime->artifacts.visibility_grid_solid[index])
                    continue;
                const slayer3d_vec3 target = brush_visibility_grid_cell_center(world_runtime, x, y, z);
                if (brush_visibility_grid_ray_clear(world_runtime, start, local_camera, target))
                    visible_cells[index] = 1u;
            }
        }
    }
    return true;
}

static bool brush_visibility_grid_mark_visible(const brush_world_runtime *world_runtime, slayer3d_vec3 local_camera,
                                               Uint8 *visible_cells)
{
    return brush_visibility_grid_mark_visible_los(world_runtime, local_camera, visible_cells);
}

static int brush_visibility_grid_cache_slot_for_start(brush_world_runtime *world_runtime, int start)
{
    if (world_runtime == NULL)
        return -1;
    for (int slot = 0; slot < SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS; ++slot)
    {
        if (world_runtime->artifacts.visibility_grid_visible_cache[slot] != NULL &&
            world_runtime->artifacts.visibility_grid_visible_cache_start[slot] == start)
        {
            return slot;
        }
    }
    return -1;
}

static int brush_visibility_grid_cache_slot_for_write(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return -1;
    int oldest_slot = 0;
    Uint64 oldest_tick = ~(Uint64)0;
    for (int slot = 0; slot < SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS; ++slot)
    {
        if (world_runtime->artifacts.visibility_grid_visible_cache[slot] == NULL)
            return slot;
        if (world_runtime->artifacts.visibility_grid_visible_cache_tick[slot] < oldest_tick)
        {
            oldest_tick = world_runtime->artifacts.visibility_grid_visible_cache_tick[slot];
            oldest_slot = slot;
        }
    }
    return oldest_slot;
}

static const Uint8 *brush_visibility_grid_visible_cells(brush_world_runtime *world_runtime, slayer3d_vec3 local_camera,
                                                        slayer3d_game_data_brush_diagnostics *diagnostics)
{
    int start = -1;
    if (world_runtime == NULL || world_runtime->artifacts.visibility_grid_solid == NULL ||
        world_runtime->artifacts.visibility_grid_cell_count <= 0 ||
        !brush_visibility_grid_cell(world_runtime, local_camera, &start) || start < 0 ||
        world_runtime->artifacts.visibility_grid_solid[start])
    {
        return NULL;
    }

    int cache_slot = brush_visibility_grid_cache_slot_for_start(world_runtime, start);
    if (cache_slot >= 0)
    {
        if (diagnostics != NULL)
            ++diagnostics->visibility_grid_cache_hits;
        world_runtime->artifacts.visibility_grid_visible_cache_tick[cache_slot] =
            ++world_runtime->artifacts.visibility_grid_visible_cache_clock;
        return world_runtime->artifacts.visibility_grid_visible_cache[cache_slot];
    }

    if (diagnostics != NULL)
        ++diagnostics->visibility_grid_cache_misses;
    cache_slot = brush_visibility_grid_cache_slot_for_write(world_runtime);
    if (cache_slot < 0)
        return NULL;
    if (world_runtime->artifacts.visibility_grid_visible_cache[cache_slot] == NULL)
    {
        world_runtime->artifacts.visibility_grid_visible_cache[cache_slot] =
            (Uint8 *)SDL_calloc((size_t)world_runtime->artifacts.visibility_grid_cell_count,
                                sizeof(*world_runtime->artifacts.visibility_grid_visible_cache[cache_slot]));
        if (world_runtime->artifacts.visibility_grid_visible_cache[cache_slot] == NULL)
            return NULL;
    }
    SDL_memset(world_runtime->artifacts.visibility_grid_visible_cache[cache_slot], 0,
               (size_t)world_runtime->artifacts.visibility_grid_cell_count *
                   sizeof(*world_runtime->artifacts.visibility_grid_visible_cache[cache_slot]));
    world_runtime->artifacts.visibility_grid_visible_cache_start[cache_slot] = -1;
    if (!brush_visibility_grid_mark_visible(world_runtime, local_camera,
                                            world_runtime->artifacts.visibility_grid_visible_cache[cache_slot]))
        return NULL;
    world_runtime->artifacts.visibility_grid_visible_cache_start[cache_slot] = start;
    world_runtime->artifacts.visibility_grid_visible_cache_tick[cache_slot] =
        ++world_runtime->artifacts.visibility_grid_visible_cache_clock;
    return world_runtime->artifacts.visibility_grid_visible_cache[cache_slot];
}

static bool brush_visibility_forced_visible(const slayer3d_game_data_brush *brush)
{
    return brush != NULL && brush->visibility == SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_ALWAYS;
}

static bool brush_visibility_trace_fallback_enabled(const slayer3d_game_data_brush *brush)
{
    return brush != NULL &&
           (brush->visibility == SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_TRACE || brush->visibility_cullable);
}

static bool brush_visible_from_visibility_grid(const brush_world_runtime *world_runtime,
                                               const slayer3d_game_data_brush *brush, const Uint8 *visible_cells)
{
    if (world_runtime == NULL || brush == NULL || visible_cells == NULL || !brush->has_bounds ||
        world_runtime->artifacts.visibility_cell_size <= 0.0f)
    {
        return true;
    }
    const float cell_size = world_runtime->artifacts.visibility_cell_size;
    const float expand = cell_size * 0.5f;
    const int min_x = SDL_max(
        0, (int)SDL_floorf((brush->bounds.min.x - expand - world_runtime->artifacts.visibility_grid_bounds.min.x) /
                           cell_size));
    const int min_y = SDL_max(
        0, (int)SDL_floorf((brush->bounds.min.y - expand - world_runtime->artifacts.visibility_grid_bounds.min.y) /
                           cell_size));
    const int min_z = SDL_max(
        0, (int)SDL_floorf((brush->bounds.min.z - expand - world_runtime->artifacts.visibility_grid_bounds.min.z) /
                           cell_size));
    const int max_x =
        SDL_min(world_runtime->artifacts.visibility_grid_dim_x - 1,
                (int)SDL_floorf((brush->bounds.max.x + expand - world_runtime->artifacts.visibility_grid_bounds.min.x) /
                                cell_size));
    const int max_y =
        SDL_min(world_runtime->artifacts.visibility_grid_dim_y - 1,
                (int)SDL_floorf((brush->bounds.max.y + expand - world_runtime->artifacts.visibility_grid_bounds.min.y) /
                                cell_size));
    const int max_z =
        SDL_min(world_runtime->artifacts.visibility_grid_dim_z - 1,
                (int)SDL_floorf((brush->bounds.max.z + expand - world_runtime->artifacts.visibility_grid_bounds.min.z) /
                                cell_size));
    if (min_x > max_x || min_y > max_y || min_z > max_z)
        return true;

    const int dim_x = world_runtime->artifacts.visibility_grid_dim_x;
    const int dim_y = world_runtime->artifacts.visibility_grid_dim_y;
    for (int z = min_z; z <= max_z; ++z)
    {
        for (int y = min_y; y <= max_y; ++y)
        {
            for (int x = min_x; x <= max_x; ++x)
            {
                const int index = x + y * dim_x + z * dim_x * dim_y;
                if (index >= 0 && index < world_runtime->artifacts.visibility_grid_cell_count && visible_cells[index])
                    return true;
            }
        }
    }
    return false;
}

static bool apply_brush_visibility_grid(brush_world_runtime *world_runtime,
                                        const slayer3d_game_data_brush_world_instance *instance,
                                        const slayer3d_camera3d *camera, bool *brush_visible, int brush_count,
                                        int *out_occluded_count, slayer3d_game_data_runtime *mutable_runtime)
{
    if (out_occluded_count != NULL)
        *out_occluded_count = 0;
    if (world_runtime == NULL || instance == NULL || camera == NULL || brush_visible == NULL ||
        mutable_runtime == NULL || world_runtime->artifacts.visibility_grid_solid == NULL ||
        world_runtime->artifacts.visibility_grid_cell_count <= 0)
    {
        return false;
    }
    const slayer3d_vec3 local_camera = slayer3d_vec3_sub(camera->position, instance->position);
    const Uint8 *visible_cells =
        brush_visibility_grid_visible_cells(world_runtime, local_camera, &mutable_runtime->brush_diagnostics);
    if (visible_cells == NULL)
        return false;

    int occluded_count = 0;
    for (int brush_index = 0; brush_index < brush_count; ++brush_index)
    {
        const slayer3d_model *brush_model = &world_runtime->artifacts.brush_render_models[brush_index];
        const slayer3d_game_data_brush *brush = &instance->world->brushes[brush_index];
        const Uint64 triangles = brush_model_triangle_count(brush_model);
        if (triangles == 0u)
            continue;
        if (!brush_visible[brush_index])
            continue;
        if (brush_visibility_forced_visible(brush))
            continue;

        ++mutable_runtime->brush_diagnostics.visibility_brush_candidates;
        if (brush_visible_from_visibility_grid(world_runtime, brush, visible_cells) ||
            !brush_occluded_from_camera(instance, brush, camera))
        {
            ++mutable_runtime->brush_diagnostics.visibility_brush_visible;
        }
        else
        {
            ++mutable_runtime->brush_diagnostics.visibility_brush_occluded;
            mutable_runtime->brush_diagnostics.visibility_triangles_culled += triangles;
            brush_visible[brush_index] = false;
            ++occluded_count;
        }
    }
    if (out_occluded_count != NULL)
        *out_occluded_count = occluded_count;
    return true;
}

static slayer3d_sphere brush_world_bounds_sphere(slayer3d_bounding_box bounds, slayer3d_vec3 world_offset)
{
    const slayer3d_vec3 center = slayer3d_vec3_make((bounds.min.x + bounds.max.x) * 0.5f + world_offset.x,
                                                    (bounds.min.y + bounds.max.y) * 0.5f + world_offset.y,
                                                    (bounds.min.z + bounds.max.z) * 0.5f + world_offset.z);
    const slayer3d_vec3 extents =
        slayer3d_vec3_make((bounds.max.x - bounds.min.x) * 0.5f, (bounds.max.y - bounds.min.y) * 0.5f,
                           (bounds.max.z - bounds.min.z) * 0.5f);
    slayer3d_sphere sphere;
    sphere.center = center;
    sphere.radius = slayer3d_vec3_length(extents);
    return sphere;
}

static int apply_brush_frustum_culling(brush_world_draw_context *context, brush_world_runtime *world_runtime,
                                       const slayer3d_game_data_brush_world_instance *instance, bool *brush_visible,
                                       int brush_count, slayer3d_game_data_runtime *mutable_runtime)
{
    if (context == NULL || context->renderer == NULL || !context->renderer->frustum_planes_valid ||
        world_runtime == NULL || instance == NULL || instance->world == NULL || brush_visible == NULL ||
        mutable_runtime == NULL)
    {
        return 0;
    }

    int culled_count = 0;
    for (int brush_index = 0; brush_index < brush_count; ++brush_index)
    {
        if (!brush_visible[brush_index])
            continue;
        const slayer3d_model *brush_model = &world_runtime->artifacts.brush_render_models[brush_index];
        const slayer3d_game_data_brush *brush = &instance->world->brushes[brush_index];
        const Uint64 triangles = brush_model_triangle_count(brush_model);
        if (brush == NULL || !brush->has_bounds || triangles == 0u)
            continue;

        ++mutable_runtime->brush_diagnostics.frustum_brush_candidates;
        const slayer3d_sphere sphere = brush_world_bounds_sphere(brush->bounds, instance->position);
        if (!slayer3d_sphere_intersects_frustum(sphere, context->renderer->frustum_planes))
        {
            brush_visible[brush_index] = false;
            ++mutable_runtime->brush_diagnostics.frustum_brush_culled;
            mutable_runtime->brush_diagnostics.frustum_triangles_culled += triangles;
            ++culled_count;
        }
    }
    return culled_count;
}

static bool brush_render_chunk_all_visible(const slayer3d_game_data_brush_compile_chunk *chunk,
                                           const bool *brush_visible, int brush_count)
{
    if (chunk == NULL || brush_visible == NULL || chunk->brush_count <= 1)
        return false;
    for (int i = 0; i < chunk->brush_count; ++i)
    {
        const int brush_index = chunk->brush_indices[i];
        if (brush_index < 0 || brush_index >= brush_count || !brush_visible[brush_index])
            return false;
    }
    return true;
}

static void brush_render_chunk_mark_drawn(const slayer3d_game_data_brush_compile_chunk *chunk, bool *brush_visible,
                                          int brush_count)
{
    if (chunk == NULL || brush_visible == NULL)
        return;
    for (int i = 0; i < chunk->brush_count; ++i)
    {
        const int brush_index = chunk->brush_indices[i];
        if (brush_index >= 0 && brush_index < brush_count)
            brush_visible[brush_index] = false;
    }
}

static bool draw_visible_brush_chunks(brush_world_draw_context *context, brush_world_runtime *world_runtime,
                                      bool *brush_visible, int brush_count)
{
    if (context == NULL || context->renderer == NULL || context->runtime == NULL || world_runtime == NULL ||
        world_runtime->artifacts.chunk_render_models == NULL || world_runtime->desc.compile_chunks == NULL)
    {
        return true;
    }

    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)context->runtime;
    const int chunk_count =
        SDL_min(world_runtime->desc.compile_chunk_count, world_runtime->artifacts.chunk_render_model_count);
    for (int chunk_index = 0; chunk_index < chunk_count; ++chunk_index)
    {
        const slayer3d_game_data_brush_compile_chunk *chunk = &world_runtime->desc.compile_chunks[chunk_index];
        if (!brush_render_chunk_all_visible(chunk, brush_visible, brush_count))
            continue;
        const slayer3d_model *chunk_model = &world_runtime->artifacts.chunk_render_models[chunk_index];
        if (brush_model_triangle_count(chunk_model) == 0u)
            continue;
        if (!slayer3d_draw_model_ex_with_assets(
                context->renderer, context->assets, chunk_model, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
                (slayer3d_color){255, 255, 255, 255}))
        {
            return false;
        }
        ++mutable_runtime->brush_diagnostics.render_chunk_draws;
        mutable_runtime->brush_diagnostics.render_chunk_brushes_drawn += (Uint64)chunk->brush_count;
        brush_render_chunk_mark_drawn(chunk, brush_visible, brush_count);
    }
    return true;
}

static bool draw_brush_world_instance_with_visibility(void *userdata,
                                                      const slayer3d_game_data_brush_world_instance *instance)
{
    brush_world_draw_context *context = (brush_world_draw_context *)userdata;
    if (context == NULL || context->renderer == NULL || context->runtime == NULL || instance == NULL ||
        instance->world == NULL)
        return false;
    if (!instance->visibility_occlusion_enabled || context->camera == NULL)
        return draw_brush_world_instance(userdata, instance);
    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)context->runtime;
    brush_world_runtime *world_runtime =
        (brush_world_runtime *)find_brush_world_runtime(mutable_runtime, instance->world_name);
    if (world_runtime == NULL || world_runtime->artifacts.brush_render_models == NULL ||
        instance->world->brushes == NULL || instance->world->brush_count <= 0)
    {
        return draw_brush_world_instance(userdata, instance);
    }
    const int brush_count = SDL_min(instance->world->brush_count, world_runtime->artifacts.brush_render_model_count);
    bool *brush_visible = (bool *)SDL_calloc((size_t)brush_count, sizeof(*brush_visible));
    if (brush_visible == NULL)
    {
        context->ok = false;
        return false;
    }
    for (int brush_index = 0; brush_index < brush_count; ++brush_index)
        brush_visible[brush_index] = true;

    const int frustum_culled_count =
        apply_brush_frustum_culling(context, world_runtime, instance, brush_visible, brush_count, mutable_runtime);
    int occluded_count = 0;
    const bool used_visibility_grid = apply_brush_visibility_grid(
        world_runtime, instance, context->camera, brush_visible, brush_count, &occluded_count, mutable_runtime);
    for (int brush_index = 0; !used_visibility_grid && brush_index < brush_count; ++brush_index)
    {
        const slayer3d_model *brush_model = &world_runtime->artifacts.brush_render_models[brush_index];
        const slayer3d_game_data_brush *brush = &instance->world->brushes[brush_index];
        const Uint64 triangles = brush_model_triangle_count(brush_model);
        if (triangles == 0u)
            continue;
        if (!brush_visibility_trace_fallback_enabled(brush))
            continue;

        ++mutable_runtime->brush_diagnostics.visibility_brush_candidates;
        if (brush_occluded_from_camera(instance, brush, context->camera))
        {
            ++mutable_runtime->brush_diagnostics.visibility_brush_occluded;
            mutable_runtime->brush_diagnostics.visibility_triangles_culled += triangles;
            brush_visible[brush_index] = false;
            ++occluded_count;
        }
        else
        {
            ++mutable_runtime->brush_diagnostics.visibility_brush_visible;
        }
    }
    if (occluded_count <= 0 && frustum_culled_count <= 0)
    {
        SDL_free(brush_visible);
        return draw_brush_world_instance(userdata, instance);
    }

    bool pushed = false;
    bool ok = true;
    if (instance->position.x != 0.0f || instance->position.y != 0.0f || instance->position.z != 0.0f)
    {
        if (!slayer3d_push_matrix(context->renderer))
        {
            context->ok = false;
            return false;
        }
        pushed = true;
        ok = slayer3d_translate(context->renderer, instance->position.x, instance->position.y, instance->position.z);
    }

    const bool restore_lighting = slayer3d_is_lighting_enabled(context->renderer);
    if (!instance->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, false);

    ok = draw_visible_brush_chunks(context, world_runtime, brush_visible, brush_count);
    for (int brush_index = 0; ok && brush_index < brush_count; ++brush_index)
    {
        if (!brush_visible[brush_index])
            continue;
        const slayer3d_model *brush_model = &world_runtime->artifacts.brush_render_models[brush_index];
        const Uint64 triangles = brush_model_triangle_count(brush_model);
        if (triangles == 0u)
            continue;

        ok = slayer3d_draw_model_ex_with_assets(
            context->renderer, context->assets, brush_model, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
            slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
            (slayer3d_color){255, 255, 255, 255});
    }

    if (!instance->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, restore_lighting);
    if (pushed && !slayer3d_pop_matrix(context->renderer))
        ok = false;
    SDL_free(brush_visible);
    if (!ok)
        context->ok = false;
    return context->ok;
}

bool slayer3d_game_data_draw_brush_worlds(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer)
{
    return slayer3d_game_data_draw_brush_worlds_with_assets(runtime, renderer, NULL);
}

bool slayer3d_game_data_draw_brush_worlds_with_assets(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_render_context *renderer,
                                                      const slayer3d_asset_resolver *assets)
{
    return slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(runtime, renderer, assets, NULL);
}

bool slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(const slayer3d_game_data_runtime *runtime,
                                                                 slayer3d_render_context *renderer,
                                                                 const slayer3d_asset_resolver *assets,
                                                                 const slayer3d_camera3d *camera)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    slayer3d_render_stats before_stats;
    SDL_zero(before_stats);
    const bool have_before_stats = slayer3d_get_render_stats(renderer, &before_stats);
    brush_world_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.assets = assets;
    context.camera = camera;
    context.ok = true;
    const bool previous_depth_prepass_scope = renderer->depth_prepass_scope_enabled;
    renderer->depth_prepass_scope_enabled = true;
    const bool iterated = slayer3d_game_data_for_each_brush_world_instance(
        runtime, camera != NULL ? draw_brush_world_instance_with_visibility : draw_brush_world_instance, &context);
    renderer->depth_prepass_scope_enabled = previous_depth_prepass_scope;
    slayer3d_render_stats after_stats;
    SDL_zero(after_stats);
    if (have_before_stats && slayer3d_get_render_stats(renderer, &after_stats))
    {
        slayer3d_game_data_accumulate_brush_render_diagnostics((slayer3d_game_data_runtime *)runtime, &before_stats,
                                                               &after_stats);
    }
    return iterated && context.ok;
}

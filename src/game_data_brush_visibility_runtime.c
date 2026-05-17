/**
 * @file game_data_brush_visibility_runtime.c
 * @brief Brush world visibility-grid and render artifact lifecycle helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/math.h"

static void free_brush_world_visibility_models(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return;
    for (int i = 0; i < world_runtime->artifacts.brush_render_model_count; ++i)
        slayer3d_free_model(&world_runtime->artifacts.brush_render_models[i]);
    SDL_free(world_runtime->artifacts.brush_render_models);
    world_runtime->artifacts.brush_render_models = NULL;
    world_runtime->artifacts.brush_render_model_count = 0;
    for (int i = 0; i < world_runtime->artifacts.chunk_render_model_count; ++i)
        slayer3d_free_model(&world_runtime->artifacts.chunk_render_models[i]);
    SDL_free(world_runtime->artifacts.chunk_render_models);
    world_runtime->artifacts.chunk_render_models = NULL;
    world_runtime->artifacts.chunk_render_model_count = 0;
}

void free_brush_world_visibility_grid(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return;
    SDL_free(world_runtime->artifacts.visibility_grid_solid);
    for (int i = 0; i < SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS; ++i)
    {
        SDL_free(world_runtime->artifacts.visibility_grid_visible_cache[i]);
        world_runtime->artifacts.visibility_grid_visible_cache[i] = NULL;
        world_runtime->artifacts.visibility_grid_visible_cache_start[i] = -1;
        world_runtime->artifacts.visibility_grid_visible_cache_tick[i] = 0u;
    }
    world_runtime->artifacts.visibility_grid_solid = NULL;
    world_runtime->artifacts.visibility_grid_visible_cache_clock = 0u;
    world_runtime->artifacts.visibility_cell_size = 0.0f;
    world_runtime->artifacts.visibility_grid_dim_x = 0;
    world_runtime->artifacts.visibility_grid_dim_y = 0;
    world_runtime->artifacts.visibility_grid_dim_z = 0;
    world_runtime->artifacts.visibility_grid_cell_count = 0;
    SDL_zero(world_runtime->artifacts.visibility_grid_bounds);
}

static bool brush_point_inside_contents(const slayer3d_game_data_brush *brush, slayer3d_vec3 point,
                                        unsigned int contents_mask)
{
    if (brush == NULL || (brush->contents & contents_mask) == 0u)
        return false;
    const float epsilon = 0.0005f;
    for (int face_index = 0; face_index < brush->face_count; ++face_index)
    {
        const slayer3d_game_data_brush_face *face = &brush->faces[face_index];
        if ((face->surface_flags & SLAYER3D_GAME_DATA_BRUSH_SURFACE_NO_COLLIDE) != 0u)
            continue;
        const float len = slayer3d_vec3_length(face->normal);
        if (len <= 0.000001f)
            continue;
        const slayer3d_vec3 normal = slayer3d_vec3_scale(face->normal, 1.0f / len);
        const float distance = face->distance / len;
        if (slayer3d_vec3_dot(normal, point) - distance > epsilon)
            return false;
    }
    return true;
}

static bool brush_world_point_inside_contents(const slayer3d_game_data_brush_world *world, slayer3d_vec3 point,
                                              unsigned int contents_mask)
{
    if (world == NULL)
        return false;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (brush->has_bounds &&
            (point.x < brush->bounds.min.x || point.x > brush->bounds.max.x || point.y < brush->bounds.min.y ||
             point.y > brush->bounds.max.y || point.z < brush->bounds.min.z || point.z > brush->bounds.max.z))
        {
            continue;
        }
        if (brush_point_inside_contents(brush, point, contents_mask))
            return true;
    }
    return false;
}

static int visibility_grid_index(int x, int y, int z, int dim_x, int dim_y)
{
    return x + y * dim_x + z * dim_x * dim_y;
}

bool compile_brush_world_visibility_grid(brush_world_runtime *world_runtime)
{
    static const int max_cells = 524288;
    static const float min_cell_size = 0.25f;
    static const float max_cell_size = 64.0f;
    free_brush_world_visibility_grid(world_runtime);
    if (world_runtime == NULL)
        return false;

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (world == NULL || !world->has_bounds || world->brush_count <= 0)
        return true;
    const float cell_size = SDL_clamp(world->visibility_cell_size > 0.0f ? world->visibility_cell_size : 2.0f,
                                      min_cell_size, max_cell_size);
    slayer3d_bounding_box bounds = world->bounds;
    bounds.min.x -= cell_size;
    bounds.min.y -= cell_size;
    bounds.min.z -= cell_size;
    bounds.max.x += cell_size;
    bounds.max.y += cell_size;
    bounds.max.z += cell_size;

    const int dim_x = SDL_max(1, (int)SDL_ceilf((bounds.max.x - bounds.min.x) / cell_size));
    const int dim_y = SDL_max(1, (int)SDL_ceilf((bounds.max.y - bounds.min.y) / cell_size));
    const int dim_z = SDL_max(1, (int)SDL_ceilf((bounds.max.z - bounds.min.z) / cell_size));
    if (dim_x <= 0 || dim_y <= 0 || dim_z <= 0 || dim_x > max_cells / SDL_max(dim_y, 1) / SDL_max(dim_z, 1))
        return true;
    const int cell_count = dim_x * dim_y * dim_z;
    if (cell_count <= 0 || cell_count > max_cells)
        return true;

    Uint8 *solid = (Uint8 *)SDL_calloc((size_t)cell_count, sizeof(*solid));
    if (solid == NULL)
        return false;

    const unsigned int blocker_mask =
        SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP;
    for (int z = 0; z < dim_z; ++z)
    {
        for (int y = 0; y < dim_y; ++y)
        {
            for (int x = 0; x < dim_x; ++x)
            {
                const slayer3d_vec3 point = slayer3d_vec3_make(bounds.min.x + ((float)x + 0.5f) * cell_size,
                                                               bounds.min.y + ((float)y + 0.5f) * cell_size,
                                                               bounds.min.z + ((float)z + 0.5f) * cell_size);
                if (brush_world_point_inside_contents(world, point, blocker_mask))
                    solid[visibility_grid_index(x, y, z, dim_x, dim_y)] = 1u;
            }
        }
    }

    world_runtime->artifacts.visibility_grid_bounds = bounds;
    world_runtime->artifacts.visibility_cell_size = cell_size;
    world_runtime->artifacts.visibility_grid_dim_x = dim_x;
    world_runtime->artifacts.visibility_grid_dim_y = dim_y;
    world_runtime->artifacts.visibility_grid_dim_z = dim_z;
    world_runtime->artifacts.visibility_grid_cell_count = cell_count;
    world_runtime->artifacts.visibility_grid_solid = solid;
    for (int i = 0; i < SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS; ++i)
        world_runtime->artifacts.visibility_grid_visible_cache_start[i] = -1;
    return true;
}

static bool compile_brush_world_visibility_models(brush_world_runtime *world_runtime, slayer3d_model **out_models,
                                                  int *out_model_count)
{
    if (out_models != NULL)
        *out_models = NULL;
    if (out_model_count != NULL)
        *out_model_count = 0;
    if (world_runtime == NULL || out_models == NULL || out_model_count == NULL)
        return false;

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (world->brush_count <= 0)
        return true;
    slayer3d_model *models = (slayer3d_model *)SDL_calloc((size_t)world->brush_count, sizeof(*models));
    if (models == NULL)
        return false;
    if (!slayer3d_game_data_brush_world_compile_brush_render_models(world, models, world->brush_count))
    {
        SDL_free(models);
        return false;
    }
    *out_models = models;
    *out_model_count = world->brush_count;
    return true;
}

static bool compile_brush_world_chunk_models(brush_world_runtime *world_runtime, slayer3d_model **out_models,
                                             int *out_model_count)
{
    if (out_models != NULL)
        *out_models = NULL;
    if (out_model_count != NULL)
        *out_model_count = 0;
    if (world_runtime == NULL || out_models == NULL || out_model_count == NULL)
        return false;

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (world->compile_chunk_count <= 0)
        return true;
    slayer3d_model *models = (slayer3d_model *)SDL_calloc((size_t)world->compile_chunk_count, sizeof(*models));
    if (models == NULL)
        return false;
    if (!slayer3d_game_data_brush_world_compile_chunk_render_models(world, models, world->compile_chunk_count))
    {
        SDL_free(models);
        return false;
    }
    *out_models = models;
    *out_model_count = world->compile_chunk_count;
    return true;
}

static void free_brush_world_model_array(slayer3d_model *models, int model_count)
{
    if (models == NULL)
        return;
    for (int i = 0; i < model_count; ++i)
        slayer3d_free_model(&models[i]);
    SDL_free(models);
}

bool rebuild_brush_world_runtime_artifacts(brush_world_runtime *world_runtime, char *error_buffer,
                                           int error_buffer_size)
{
    if (world_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "missing brush world runtime");
        return false;
    }

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    const slayer3d_model *old_render_model = world->render_model;
    char compile_error[256] = {0};
    if (!slayer3d_game_data_brush_world_build_acceleration_checked(world, compile_error, sizeof(compile_error)))
    {
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' acceleration data: %s",
                   world->name != NULL ? world->name : "<unnamed>",
                   compile_error[0] != '\0' ? compile_error : "unknown compile error");
        return false;
    }

    slayer3d_model render_model;
    slayer3d_model *brush_render_models = NULL;
    slayer3d_model *chunk_render_models = NULL;
    int brush_render_model_count = 0;
    int chunk_render_model_count = 0;
    SDL_zero(render_model);

    brush_world_runtime staged_grid;
    SDL_zero(staged_grid);
    staged_grid.desc = *world;

    bool ok = slayer3d_game_data_brush_world_compile_render_model(world, &render_model);
    if (!ok)
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' render mesh",
                   world->name != NULL ? world->name : "<unnamed>");
    if (ok && !compile_brush_world_visibility_models(world_runtime, &brush_render_models, &brush_render_model_count))
    {
        ok = false;
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' visibility meshes",
                   world->name != NULL ? world->name : "<unnamed>");
    }
    if (ok && !compile_brush_world_visibility_grid(&staged_grid))
    {
        ok = false;
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' visibility grid",
                   world->name != NULL ? world->name : "<unnamed>");
    }
    if (ok && !slayer3d_game_data_brush_world_build_compile_chunks(world))
    {
        ok = false;
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' spatial chunks",
                   world->name != NULL ? world->name : "<unnamed>");
    }
    if (ok && !compile_brush_world_chunk_models(world_runtime, &chunk_render_models, &chunk_render_model_count))
    {
        ok = false;
        set_errorf(error_buffer, error_buffer_size, "failed to compile brush world '%s' chunk meshes",
                   world->name != NULL ? world->name : "<unnamed>");
    }

    if (!ok)
    {
        slayer3d_free_model(&render_model);
        free_brush_world_model_array(brush_render_models, brush_render_model_count);
        free_brush_world_model_array(chunk_render_models, chunk_render_model_count);
        free_brush_world_visibility_grid(&staged_grid);
        world->render_model = old_render_model;
        return false;
    }

    slayer3d_free_model(&world_runtime->artifacts.render_model);
    free_brush_world_visibility_models(world_runtime);
    free_brush_world_visibility_grid(world_runtime);

    world_runtime->artifacts.render_model = render_model;
    world_runtime->artifacts.brush_render_models = brush_render_models;
    world_runtime->artifacts.brush_render_model_count = brush_render_model_count;
    world_runtime->artifacts.chunk_render_models = chunk_render_models;
    world_runtime->artifacts.chunk_render_model_count = chunk_render_model_count;
    world_runtime->artifacts.visibility_grid_bounds = staged_grid.artifacts.visibility_grid_bounds;
    world_runtime->artifacts.visibility_cell_size = staged_grid.artifacts.visibility_cell_size;
    world_runtime->artifacts.visibility_grid_dim_x = staged_grid.artifacts.visibility_grid_dim_x;
    world_runtime->artifacts.visibility_grid_dim_y = staged_grid.artifacts.visibility_grid_dim_y;
    world_runtime->artifacts.visibility_grid_dim_z = staged_grid.artifacts.visibility_grid_dim_z;
    world_runtime->artifacts.visibility_grid_cell_count = staged_grid.artifacts.visibility_grid_cell_count;
    world_runtime->artifacts.visibility_grid_solid = staged_grid.artifacts.visibility_grid_solid;
    for (int i = 0; i < SLAYER3D_BRUSH_VISIBILITY_CACHE_SLOTS; ++i)
        world_runtime->artifacts.visibility_grid_visible_cache_start[i] = -1;

    world->render_model = &world_runtime->artifacts.render_model;
    world->compile_artifact_hash = slayer3d_game_data_brush_world_compute_compile_artifact_hash(world);
    return true;
}

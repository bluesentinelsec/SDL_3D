/**
 * @file game_data_world_queries.c
 * @brief Scene, sector, brush, and generic world-model query helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/math.h"

const char *slayer3d_game_data_active_scene(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return scene != NULL ? scene->name : NULL;
}

int slayer3d_game_data_scene_count(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_count : 0;
}

const char *slayer3d_game_data_scene_name_at(const slayer3d_game_data_runtime *runtime, int index)
{
    if (runtime == NULL || index < 0 || index >= runtime->scene_count)
        return NULL;
    return runtime->scenes[index].name;
}

slayer3d_properties *slayer3d_game_data_mutable_scene_state(slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_state : NULL;
}

const slayer3d_properties *slayer3d_game_data_scene_state(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->scene_state : NULL;
}

sector_level_runtime *find_sector_level_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->sector_level_count; ++i)
    {
        sector_level_runtime *level = &runtime->sector_levels[i];
        if (level->name != NULL && SDL_strcmp(level->name, name) == 0)
            return level;
    }
    return NULL;
}

const sector_level_runtime *find_sector_level_runtime(const slayer3d_game_data_runtime *runtime, const char *name)
{
    return find_sector_level_runtime_mutable((slayer3d_game_data_runtime *)runtime, name);
}

int sector_level_find_sector_name(const sector_level_runtime *level, const char *sector_name)
{
    if (level == NULL || sector_name == NULL)
        return -1;
    for (int i = 0; i < level->sector_count; ++i)
    {
        if (level->sector_names[i] != NULL && SDL_strcmp(level->sector_names[i], sector_name) == 0)
            return i;
    }
    return -1;
}

bool slayer3d_game_data_get_sector_level(const slayer3d_game_data_runtime *runtime, const char *name,
                                         slayer3d_game_data_sector_level *out_level)
{
    if (out_level != NULL)
        SDL_zero(*out_level);
    if (runtime == NULL || name == NULL || out_level == NULL)
        return false;

    const sector_level_runtime *level = find_sector_level_runtime(runtime, name);
    if (level == NULL)
        return false;

    out_level->name = level->name;
    out_level->sectors = level->sectors;
    out_level->sector_names = level->sector_names;
    out_level->sector_count = level->sector_count;
    out_level->materials = level->materials;
    out_level->material_count = level->material_count;
    out_level->lights = level->lights;
    out_level->light_count = level->light_count;
    out_level->lightmapped = &level->lightmapped;
    out_level->vertex_baked = &level->vertex_baked;
    out_level->unlit = &level->unlit;
    return true;
}

brush_world_runtime *find_brush_world_runtime_mutable(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->brush_world_count; ++i)
    {
        brush_world_runtime *world = &runtime->brush_worlds[i];
        if (world->desc.name != NULL && SDL_strcmp(world->desc.name, name) == 0)
            return world;
    }
    return NULL;
}

const brush_world_runtime *find_brush_world_runtime(const slayer3d_game_data_runtime *runtime, const char *name)
{
    return find_brush_world_runtime_mutable((slayer3d_game_data_runtime *)runtime, name);
}

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

bool slayer3d_game_data_get_brush_world(const slayer3d_game_data_runtime *runtime, const char *name,
                                        slayer3d_game_data_brush_world *out_world)
{
    if (out_world != NULL)
        SDL_zero(*out_world);
    if (runtime == NULL || name == NULL || out_world == NULL)
        return false;

    const brush_world_runtime *world = find_brush_world_runtime(runtime, name);
    if (world == NULL)
        return false;

    *out_world = world->desc;
    return true;
}

static unsigned int world_model_filter_or_all(unsigned int model_filter)
{
    return model_filter != 0u ? model_filter : SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_ALL;
}

static unsigned int brush_contents_mask_or_all(unsigned int contents_mask)
{
    return contents_mask != 0u ? contents_mask
                               : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP |
                                     SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP |
                                     SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER | SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER |
                                     SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA | SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY;
}

slayer3d_bounding_box translated_bounds(slayer3d_bounding_box bounds, slayer3d_vec3 position)
{
    bounds.min = slayer3d_vec3_add(bounds.min, position);
    bounds.max = slayer3d_vec3_add(bounds.max, position);
    return bounds;
}

bool model_bounds(const slayer3d_model *model, slayer3d_bounding_box *out_bounds)
{
    if (out_bounds != NULL)
        SDL_zero(*out_bounds);
    if (model == NULL || out_bounds == NULL)
        return false;

    bool has_bounds = false;
    for (int i = 0; i < model->mesh_count; ++i)
    {
        const slayer3d_mesh *mesh = &model->meshes[i];
        if (!mesh->has_local_bounds)
            continue;
        if (!has_bounds)
        {
            *out_bounds = mesh->local_bounds;
            has_bounds = true;
            continue;
        }
        out_bounds->min.x = SDL_min(out_bounds->min.x, mesh->local_bounds.min.x);
        out_bounds->min.y = SDL_min(out_bounds->min.y, mesh->local_bounds.min.y);
        out_bounds->min.z = SDL_min(out_bounds->min.z, mesh->local_bounds.min.z);
        out_bounds->max.x = SDL_max(out_bounds->max.x, mesh->local_bounds.max.x);
        out_bounds->max.y = SDL_max(out_bounds->max.y, mesh->local_bounds.max.y);
        out_bounds->max.z = SDL_max(out_bounds->max.z, mesh->local_bounds.max.z);
    }
    return has_bounds;
}

static void init_world_trace_result(const slayer3d_game_data_world_trace_desc *desc,
                                    slayer3d_game_data_world_trace_result *out_result)
{
    if (out_result == NULL)
        return;
    SDL_zero(*out_result);
    out_result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID;
    out_result->element_index = -1;
    out_result->face_index = -1;
    out_result->fraction = 1.0f;
    if (desc != NULL)
    {
        out_result->end_position = desc->end;
        out_result->point = desc->end;
    }
}

static void init_world_point_result(slayer3d_game_data_world_point_result *out_result)
{
    if (out_result == NULL)
        return;
    SDL_zero(*out_result);
    out_result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID;
    out_result->element_index = -1;
}

static bool world_trace_shape_valid(slayer3d_game_data_brush_trace_shape shape, slayer3d_vec3 extents)
{
    switch (shape)
    {
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT:
        return true;
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE:
        return extents.x >= 0.0f;
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB:
        return extents.x >= 0.0f && extents.y >= 0.0f && extents.z >= 0.0f;
    default:
        return false;
    }
}

typedef struct world_sector_trace_context
{
    const slayer3d_game_data_runtime *runtime;
    const slayer3d_game_data_world_trace_desc *desc;
    slayer3d_game_data_world_trace_result *result;
    bool ok;
} world_sector_trace_context;

static bool trace_sector_world_instance(void *userdata, const slayer3d_game_data_sector_level_instance *instance)
{
    world_sector_trace_context *context = (world_sector_trace_context *)userdata;
    if (context == NULL || context->desc == NULL || context->result == NULL || instance == NULL ||
        instance->level == NULL || instance->sectors == NULL)
    {
        if (context != NULL)
            context->ok = false;
        return false;
    }
    if (context->desc->shape != SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT)
        return true;

    const slayer3d_vec3 local_start = slayer3d_vec3_sub(context->desc->start, instance->position);
    const slayer3d_vec3 local_end = slayer3d_vec3_sub(context->desc->end, instance->position);
    const slayer3d_vec3 direction = slayer3d_vec3_sub(local_end, local_start);
    const float distance = slayer3d_vec3_length(direction);
    if (distance <= 0.000001f)
        return true;

    const slayer3d_level_trace_result sector_hit =
        slayer3d_level_trace_point(instance->level, instance->sectors, local_start, direction, distance);
    if (!sector_hit.hit)
        return true;
    if (context->result->hit && sector_hit.fraction >= context->result->fraction)
        return true;

    context->result->hit = true;
    context->result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL;
    context->result->world_name = instance->level_name;
    context->result->world_position = instance->position;
    context->result->element_index = sector_hit.end_sector;
    context->result->face_index = -1;
    context->result->fraction = sector_hit.fraction;
    context->result->end_position = slayer3d_vec3_add(sector_hit.end_point, instance->position);
    context->result->point = context->result->end_position;
    context->result->normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const sector_level_runtime *level_runtime = find_sector_level_runtime(context->runtime, instance->level_name);
    if (level_runtime != NULL && sector_hit.end_sector >= 0 && sector_hit.end_sector < level_runtime->sector_count)
        context->result->element_name = level_runtime->sector_names[sector_hit.end_sector];
    return true;
}

bool slayer3d_game_data_trace_world_models(const slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_world_trace_desc *desc,
                                           slayer3d_game_data_world_trace_result *out_result)
{
    init_world_trace_result(desc, out_result);
    if (runtime == NULL || desc == NULL || out_result == NULL || !world_trace_shape_valid(desc->shape, desc->extents))
    {
        return false;
    }

    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)runtime;
    ++mutable_runtime->world_model_trace_count;
    const unsigned int filter = world_model_filter_or_all(desc->model_filter);

    if ((filter & SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_SECTOR_LEVELS) != 0u)
    {
        world_sector_trace_context context;
        SDL_zero(context);
        context.runtime = runtime;
        context.desc = desc;
        context.result = out_result;
        context.ok = true;
        if (!slayer3d_game_data_for_each_sector_level_instance(runtime, trace_sector_world_instance, &context) ||
            !context.ok)
        {
            return false;
        }
    }

    if ((filter & SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS) != 0u)
    {
        slayer3d_game_data_brush_trace_desc brush_desc;
        SDL_zero(brush_desc);
        brush_desc.start = desc->start;
        brush_desc.end = desc->end;
        brush_desc.shape = desc->shape;
        brush_desc.extents = desc->extents;
        brush_desc.contents_mask = brush_contents_mask_or_all(desc->contents_mask);
        slayer3d_game_data_brush_trace_result brush_result;
        if (!slayer3d_game_data_trace_active_brush_worlds(runtime, &brush_desc, &brush_result))
            return false;
        if (brush_result.hit && (!out_result->hit || brush_result.fraction < out_result->fraction ||
                                 (brush_result.start_solid && !out_result->start_solid)))
        {
            out_result->hit = true;
            out_result->start_solid = brush_result.start_solid;
            out_result->all_solid = brush_result.all_solid;
            out_result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
            out_result->world_name = brush_result.world_name;
            out_result->world_position = brush_result.world_position;
            out_result->element_name = brush_result.brush_name;
            out_result->material_name = brush_result.material_name;
            out_result->element_index = brush_result.brush_index;
            out_result->face_index = brush_result.face_index;
            out_result->fraction = brush_result.fraction;
            out_result->end_position = brush_result.end_position;
            out_result->point = brush_result.point;
            out_result->normal = brush_result.normal;
            out_result->contents = brush_result.contents;
            out_result->surface_flags = brush_result.surface_flags;
        }
    }
    return true;
}

bool slayer3d_game_data_query_world_model_point(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 point,
                                                unsigned int model_filter, unsigned int brush_contents_mask,
                                                slayer3d_game_data_world_point_result *out_result)
{
    init_world_point_result(out_result);
    if (runtime == NULL || out_result == NULL)
        return false;

    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)runtime;
    ++mutable_runtime->world_model_point_query_count;
    const unsigned int filter = world_model_filter_or_all(model_filter);
    if ((filter & SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_SECTOR_LEVELS) != 0u)
    {
        const scene_entry *scene = active_scene_entry_const(runtime);
        yyjson_val *instances = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "sector_levels");
        for (size_t i = 0; yyjson_is_arr(instances) && i < yyjson_arr_size(instances); ++i)
        {
            yyjson_val *entry = yyjson_arr_get(instances, i);
            const char *level_name = json_string(entry, "level", NULL);
            const sector_level_runtime *level_runtime = find_sector_level_runtime(runtime, level_name);
            if (level_runtime == NULL)
                return false;
            const bool sector_lighting_enabled = scene_state_bool(
                runtime, json_string(entry, "sector_lighting_key", NULL), json_bool(entry, "sector_lighting", true));
            const slayer3d_level *level = NULL;
            const char *variant_name = scene_state_string(runtime, json_string(entry, "variant_key", NULL),
                                                          json_string(entry, "variant", "lightmapped"));
            if (sector_level_variant_from_string(variant_name, &level, level_runtime, sector_lighting_enabled) == 0 ||
                level == NULL)
            {
                return false;
            }
            const slayer3d_vec3 position = json_vec3(entry, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            const slayer3d_vec3 local_point = slayer3d_vec3_sub(point, position);
            const int sector_index = slayer3d_level_find_sector_at(level, level_runtime->sectors, local_point.x,
                                                                   local_point.z, local_point.y);
            if (sector_index >= 0 && sector_index < level_runtime->sector_count)
            {
                out_result->inside = true;
                out_result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL;
                out_result->world_name = level_runtime->name;
                out_result->world_position = position;
                out_result->element_name = level_runtime->sector_names[sector_index];
                out_result->element_index = sector_index;
                return true;
            }
        }
    }

    if ((filter & SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS) != 0u)
    {
        slayer3d_game_data_brush_trace_desc trace;
        SDL_zero(trace);
        trace.start = point;
        trace.end = point;
        trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
        trace.contents_mask = brush_contents_mask_or_all(brush_contents_mask);
        slayer3d_game_data_brush_trace_result result;
        if (!slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, &result))
            return false;
        if (result.hit && result.start_solid)
        {
            out_result->inside = true;
            out_result->type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
            out_result->world_name = result.world_name;
            out_result->world_position = result.world_position;
            out_result->element_name = result.brush_name;
            out_result->element_index = result.brush_index;
            out_result->contents = result.contents;
            return true;
        }
    }

    return true;
}

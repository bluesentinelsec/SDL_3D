/**
 * @file game_data_world_model_queries.c
 * @brief Generic active-world point and trace query routing helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/math.h"

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

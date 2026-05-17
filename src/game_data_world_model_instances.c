/**
 * @file game_data_world_model_instances.c
 * @brief Active-scene sector, brush, and world-model instance enumeration helpers.
 */

#include "game_data_internal.h"

bool slayer3d_game_data_for_each_sector_level_instance(const slayer3d_game_data_runtime *runtime,
                                                       slayer3d_game_data_sector_level_instance_fn callback,
                                                       void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *instances = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "sector_levels");
    if (instances == NULL)
        return true;
    if (!yyjson_is_arr(instances))
        return false;

    for (size_t i = 0; i < yyjson_arr_size(instances); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(instances, i);
        const char *level_name = json_string(entry, "level", NULL);
        const char *variant_name = scene_state_string(runtime, json_string(entry, "variant_key", NULL),
                                                      json_string(entry, "variant", "lightmapped"));
        const sector_level_runtime *level_runtime = find_sector_level_runtime(runtime, level_name);
        const bool sector_lighting_enabled = scene_state_bool(runtime, json_string(entry, "sector_lighting_key", NULL),
                                                              json_bool(entry, "sector_lighting", true));
        const slayer3d_level *level = NULL;
        const slayer3d_game_data_sector_level_variant variant =
            sector_level_variant_from_string(variant_name, &level, level_runtime, sector_lighting_enabled);
        if (level_runtime == NULL || level == NULL || variant == 0)
            return false;

        slayer3d_game_data_sector_level_instance instance;
        SDL_zero(instance);
        instance.level_name = level_runtime->name;
        instance.variant_name = variant_name;
        instance.variant = variant;
        instance.level = level;
        instance.sectors = level_runtime->sectors;
        instance.sector_count = level_runtime->sector_count;
        instance.position = json_vec3(entry, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        instance.portal_culling = scene_state_bool(runtime, json_string(entry, "portal_culling_key", NULL),
                                                   json_bool(entry, "portal_culling", true));
        instance.sector_lighting_enabled = sector_lighting_enabled;
        if (!callback(userdata, &instance))
            return true;
    }
    return true;
}

bool slayer3d_game_data_for_each_brush_world_instance(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_brush_world_instance_fn callback,
                                                      void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *instances = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "brush_worlds");
    if (instances == NULL)
        return true;
    if (!yyjson_is_arr(instances))
        return false;

    for (size_t i = 0; i < yyjson_arr_size(instances); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(instances, i);
        const char *world_name = json_string(entry, "world", NULL);
        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
        if (world_runtime == NULL)
            return false;

        slayer3d_game_data_brush_world_instance instance;
        SDL_zero(instance);
        instance.world_name = world_runtime->desc.name;
        instance.world = &world_runtime->desc;
        instance.position = json_vec3(entry, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        instance.acceleration_enabled = scene_state_bool(runtime, json_string(entry, "acceleration_key", NULL),
                                                         json_bool(entry, "acceleration", true));
        instance.lighting_enabled =
            scene_state_bool(runtime, json_string(entry, "lighting_key", NULL), json_bool(entry, "lighting", true));
        instance.debug_wireframe = scene_state_bool(runtime, json_string(entry, "debug_wireframe_key", NULL),
                                                    json_bool(entry, "debug_wireframe", false));
        instance.visibility_occlusion_enabled =
            scene_state_bool(runtime, json_string(entry, "visibility_occlusion_key", NULL),
                             json_bool(entry, "visibility_occlusion", false));
        if (!callback(userdata, &instance))
            return true;
    }
    return true;
}

typedef struct world_model_iteration_context
{
    slayer3d_game_data_world_model_instance_fn callback;
    void *userdata;
    bool stopped;
} world_model_iteration_context;

static bool emit_sector_world_model_instance(void *userdata,
                                             const slayer3d_game_data_sector_level_instance *sector_instance)
{
    world_model_iteration_context *context = (world_model_iteration_context *)userdata;
    if (context == NULL || context->callback == NULL || sector_instance == NULL)
        return false;

    slayer3d_game_data_world_model_instance instance;
    SDL_zero(instance);
    instance.type = SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL;
    instance.name = sector_instance->level_name;
    instance.variant_name = sector_instance->variant_name;
    instance.position = sector_instance->position;
    instance.sector_level = sector_instance->level;
    instance.sectors = sector_instance->sectors;
    instance.sector_count = sector_instance->sector_count;
    slayer3d_bounding_box bounds;
    if (model_bounds(sector_instance->level != NULL ? &sector_instance->level->model : NULL, &bounds))
    {
        instance.bounds = translated_bounds(bounds, sector_instance->position);
        instance.has_bounds = true;
    }

    if (!context->callback(context->userdata, &instance))
    {
        context->stopped = true;
        return false;
    }
    return true;
}

static bool emit_brush_world_model_instance(void *userdata,
                                            const slayer3d_game_data_brush_world_instance *brush_instance)
{
    world_model_iteration_context *context = (world_model_iteration_context *)userdata;
    if (context == NULL || context->callback == NULL || brush_instance == NULL)
        return false;

    slayer3d_game_data_world_model_instance instance;
    SDL_zero(instance);
    instance.type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
    instance.name = brush_instance->world_name;
    instance.position = brush_instance->position;
    instance.brush_world = brush_instance->world;
    if (brush_instance->world != NULL && brush_instance->world->has_bounds)
    {
        instance.bounds = translated_bounds(brush_instance->world->bounds, brush_instance->position);
        instance.has_bounds = true;
    }

    if (!context->callback(context->userdata, &instance))
    {
        context->stopped = true;
        return false;
    }
    return true;
}

bool slayer3d_game_data_for_each_world_model_instance(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_world_model_instance_fn callback,
                                                      void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    world_model_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    if (!slayer3d_game_data_for_each_sector_level_instance(runtime, emit_sector_world_model_instance, &context))
        return false;
    if (context.stopped)
        return true;
    return slayer3d_game_data_for_each_brush_world_instance(runtime, emit_brush_world_model_instance, &context);
}

/**
 * @file game_data_editor_selection_queries.c
 * @brief Editor world-model picking and selection query helpers.
 */

#include "game_data_internal.h"

void init_editor_selection(slayer3d_game_data_editor_selection *selection)
{
    if (selection == NULL)
        return;
    SDL_zero(*selection);
    selection->type = SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID;
    selection->element_index = -1;
    selection->face_index = -1;
    selection->fraction = 1.0f;
}

typedef struct world_model_instance_lookup_context
{
    const char *world_name;
    slayer3d_vec3 position;
    slayer3d_game_data_world_model_type type;
    slayer3d_bounding_box bounds;
    bool has_bounds;
    bool found;
} world_model_instance_lookup_context;

static bool capture_matching_world_model_instance(void *userdata,
                                                  const slayer3d_game_data_world_model_instance *instance)
{
    world_model_instance_lookup_context *context = (world_model_instance_lookup_context *)userdata;
    if (context == NULL || context->world_name == NULL || instance == NULL || instance->name == NULL)
        return false;
    if (context->type != SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID && instance->type != context->type)
        return true;
    if (SDL_strcmp(context->world_name, instance->name) != 0)
        return true;
    if (slayer3d_vec3_length_squared(slayer3d_vec3_sub(context->position, instance->position)) > 0.000001f)
        return true;

    context->bounds = instance->bounds;
    context->has_bounds = instance->has_bounds;
    context->found = true;
    return false;
}

static bool active_world_model_bounds(const slayer3d_game_data_runtime *runtime,
                                      slayer3d_game_data_world_model_type type, const char *world_name,
                                      slayer3d_vec3 position, slayer3d_bounding_box *out_bounds)
{
    if (out_bounds != NULL)
        SDL_zero(*out_bounds);
    if (runtime == NULL || world_name == NULL || out_bounds == NULL)
        return false;

    world_model_instance_lookup_context context;
    SDL_zero(context);
    context.world_name = world_name;
    context.position = position;
    context.type = type;
    if (!slayer3d_game_data_for_each_world_model_instance(runtime, capture_matching_world_model_instance, &context))
        return false;
    if (!context.found || !context.has_bounds)
        return false;
    *out_bounds = context.bounds;
    return true;
}

static bool populate_brush_editor_selection(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_world_trace_result *trace,
                                            slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || trace == NULL || selection == NULL || trace->world_name == NULL)
        return false;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, trace->world_name);
    if (world_runtime == NULL)
        return true;

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    selection->world_editor = &world->editor;
    if (trace->element_index < 0 || trace->element_index >= world->brush_count)
        return true;

    const slayer3d_game_data_brush *brush = &world->brushes[trace->element_index];
    selection->element_editor = &brush->editor;
    if (brush->has_bounds)
    {
        selection->bounds = translated_bounds(brush->bounds, trace->world_position);
        selection->has_bounds = true;
    }

    if (trace->face_index >= 0 && trace->face_index < brush->face_count)
    {
        const slayer3d_game_data_brush_face *face = &brush->faces[trace->face_index];
        selection->face_editor = &face->editor;
        if (face->material_index >= 0 && face->material_index < world->material_count)
            selection->material_editor = &world->materials[face->material_index].editor;
    }
    return true;
}

bool slayer3d_game_data_pick_editor_world_model(const slayer3d_game_data_runtime *runtime,
                                                const slayer3d_game_data_world_trace_desc *desc,
                                                slayer3d_game_data_editor_selection *out_selection)
{
    init_editor_selection(out_selection);
    if (runtime == NULL || desc == NULL || out_selection == NULL)
        return false;

    slayer3d_game_data_world_trace_result trace;
    if (!slayer3d_game_data_trace_world_models(runtime, desc, &trace))
        return false;
    if (!trace.hit)
        return true;

    out_selection->hit = true;
    out_selection->type = trace.type;
    out_selection->world_name = trace.world_name;
    out_selection->world_position = trace.world_position;
    out_selection->element_name = trace.element_name;
    out_selection->material_name = trace.material_name;
    out_selection->element_index = trace.element_index;
    out_selection->face_index = trace.face_index;
    out_selection->fraction = trace.fraction;
    out_selection->point = trace.point;
    out_selection->normal = trace.normal;

    if (trace.type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
        return populate_brush_editor_selection(runtime, &trace, out_selection);

    if (active_world_model_bounds(runtime, trace.type, trace.world_name, trace.world_position, &out_selection->bounds))
        out_selection->has_bounds = true;
    return true;
}

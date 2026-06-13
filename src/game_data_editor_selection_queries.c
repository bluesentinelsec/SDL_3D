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
    selection->compiled_face_index = -1;
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
        for (int i = 0; i < world->compile_rendered_face_metadata_count; ++i)
        {
            const slayer3d_game_data_brush_compiled_face *compiled_face = &world->compile_rendered_faces[i];
            if (compiled_face->brush_index == trace->element_index && compiled_face->face_index == trace->face_index)
            {
                selection->compiled_face = compiled_face;
                selection->compiled_face_index = i;
                break;
            }
        }
    }
    return true;
}

static slayer3d_bounding_box editor_player_start_bounds(const editor_player_start_runtime *start)
{
    const float radius = 0.6f;
    const float height = 1.8f;
    slayer3d_bounding_box bounds;
    bounds.min = slayer3d_vec3_make(start->position.x - radius, start->position.y, start->position.z - radius);
    bounds.max = slayer3d_vec3_make(start->position.x + radius, start->position.y + height, start->position.z + radius);
    return bounds;
}

static slayer3d_bounding_box editor_actor_bounds(const editor_actor_runtime *actor)
{
    const slayer3d_vec3 scale = actor != NULL && slayer3d_vec3_length_squared(actor->scale) > 0.000001f
                                    ? actor->scale
                                    : slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
    slayer3d_vec3 half = slayer3d_vec3_make(0.5f * scale.x, 0.5f * scale.y, 0.5f * scale.z);
    const char *mesh = actor != NULL ? actor->mesh : NULL;
    if (mesh != NULL && SDL_strcmp(mesh, "capsule") == 0)
        half = slayer3d_vec3_make(0.35f * scale.x, 0.9f * scale.y, 0.35f * scale.z);
    else if (mesh != NULL && SDL_strcmp(mesh, "rectangle") == 0)
        half = slayer3d_vec3_make(0.45f * scale.x, 0.9f * scale.y, 0.25f * scale.z);

    slayer3d_bounding_box bounds;
    bounds.min = slayer3d_vec3_make(actor->position.x - half.x, actor->position.y, actor->position.z - half.z);
    bounds.max =
        slayer3d_vec3_make(actor->position.x + half.x, actor->position.y + half.y * 2.0f, actor->position.z + half.z);
    return bounds;
}

static bool editor_segment_aabb_intersection(slayer3d_vec3 start, slayer3d_vec3 end, slayer3d_bounding_box bounds,
                                             float *out_fraction)
{
    const slayer3d_vec3 direction = slayer3d_vec3_sub(end, start);
    float t_min = 0.0f;
    float t_max = 1.0f;

#define TEST_AXIS(component)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (SDL_fabsf(direction.component) <= 0.000001f)                                                               \
        {                                                                                                              \
            if (start.component < bounds.min.component || start.component > bounds.max.component)                      \
                return false;                                                                                          \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            float inv = 1.0f / direction.component;                                                                    \
            float t1 = (bounds.min.component - start.component) * inv;                                                 \
            float t2 = (bounds.max.component - start.component) * inv;                                                 \
            if (t1 > t2)                                                                                               \
            {                                                                                                          \
                const float temp = t1;                                                                                 \
                t1 = t2;                                                                                               \
                t2 = temp;                                                                                             \
            }                                                                                                          \
            t_min = SDL_max(t_min, t1);                                                                                \
            t_max = SDL_min(t_max, t2);                                                                                \
            if (t_min > t_max)                                                                                         \
                return false;                                                                                          \
        }                                                                                                              \
    } while (0)

    TEST_AXIS(x);
    TEST_AXIS(y);
    TEST_AXIS(z);
#undef TEST_AXIS

    if (out_fraction != NULL)
        *out_fraction = t_min;
    return true;
}

bool pick_editor_player_start(const slayer3d_game_data_runtime *runtime,
                              const slayer3d_game_data_world_trace_desc *trace,
                              slayer3d_game_data_editor_selection *out_selection)
{
    if (runtime == NULL || trace == NULL || out_selection == NULL)
        return false;

    float best_fraction = 1.0f;
    const editor_player_start_runtime *best_start = NULL;
    slayer3d_bounding_box best_bounds;
    SDL_zero(best_bounds);
    for (int i = 0; i < runtime->editor_player_start_count; ++i)
    {
        const editor_player_start_runtime *start = &runtime->editor_player_starts[i];
        if (start->name == NULL || start->name[0] == '\0')
            continue;
        const slayer3d_bounding_box bounds = editor_player_start_bounds(start);
        float fraction = 1.0f;
        if (!editor_segment_aabb_intersection(trace->start, trace->end, bounds, &fraction))
            continue;
        if (best_start == NULL || fraction < best_fraction)
        {
            best_start = start;
            best_fraction = fraction;
            best_bounds = bounds;
        }
    }

    if (best_start == NULL)
        return false;

    init_editor_selection(out_selection);
    out_selection->hit = true;
    out_selection->type = SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_PLAYER_START;
    out_selection->world_name = "editor_player_starts";
    out_selection->world_position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    out_selection->element_name = best_start->name;
    out_selection->element_index = (int)(best_start - runtime->editor_player_starts);
    out_selection->face_index = -1;
    out_selection->fraction = best_fraction;
    out_selection->point = slayer3d_vec3_add(
        trace->start, slayer3d_vec3_scale(slayer3d_vec3_sub(trace->end, trace->start), best_fraction));
    out_selection->normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    out_selection->bounds = best_bounds;
    out_selection->has_bounds = true;
    return true;
}

bool pick_editor_actor(const slayer3d_game_data_runtime *runtime, const slayer3d_game_data_world_trace_desc *trace,
                       slayer3d_game_data_editor_selection *out_selection)
{
    if (runtime == NULL || trace == NULL || out_selection == NULL)
        return false;

    float best_fraction = 1.0f;
    const editor_actor_runtime *best_actor = NULL;
    slayer3d_bounding_box best_bounds;
    SDL_zero(best_bounds);
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        const editor_actor_runtime *actor = &runtime->editor_actors[i];
        if (actor->name == NULL || actor->name[0] == '\0')
            continue;
        const slayer3d_bounding_box bounds = editor_actor_bounds(actor);
        float fraction = 1.0f;
        if (!editor_segment_aabb_intersection(trace->start, trace->end, bounds, &fraction))
            continue;
        if (best_actor == NULL || fraction < best_fraction)
        {
            best_actor = actor;
            best_fraction = fraction;
            best_bounds = bounds;
        }
    }

    if (best_actor == NULL)
        return false;

    init_editor_selection(out_selection);
    out_selection->hit = true;
    out_selection->type = SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR;
    out_selection->world_name = "editor_actors";
    out_selection->world_position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    out_selection->element_name = best_actor->name;
    out_selection->element_index = (int)(best_actor - runtime->editor_actors);
    out_selection->face_index = -1;
    out_selection->fraction = best_fraction;
    out_selection->point = slayer3d_vec3_add(
        trace->start, slayer3d_vec3_scale(slayer3d_vec3_sub(trace->end, trace->start), best_fraction));
    out_selection->normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    out_selection->bounds = best_bounds;
    out_selection->has_bounds = true;
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

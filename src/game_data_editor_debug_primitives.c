/**
 * @file game_data_editor_debug_primitives.c
 * @brief Editor debug primitive emission helpers.
 */

#include "game_data_internal.h"

typedef struct editor_debug_iteration_context
{
    slayer3d_game_data_editor_debug_primitive_fn callback;
    void *userdata;
    slayer3d_color color;
    slayer3d_game_data_editor_debug_primitive_type type;
    const char *world_name;
    const char *element_name;
    int face_index;
    bool stopped;
} editor_debug_iteration_context;

static slayer3d_color editor_debug_color_or_default(slayer3d_color color, slayer3d_color fallback)
{
    return color.a != 0 ? color : fallback;
}

static unsigned int editor_debug_flag_from_string(const char *value)
{
    if (SDL_strcmp(value != NULL ? value : "", "all") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if (SDL_strcmp(value != NULL ? value : "", "world_bounds") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORLD_BOUNDS;
    if (SDL_strcmp(value != NULL ? value : "", "selection_bounds") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_BOUNDS;
    if (SDL_strcmp(value != NULL ? value : "", "trace_ray") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_TRACE_RAY;
    if (SDL_strcmp(value != NULL ? value : "", "face_normal") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_FACE_NORMAL;
    if (SDL_strcmp(value != NULL ? value : "", "hit_marker") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_HIT_MARKER;
    if (SDL_strcmp(value != NULL ? value : "", "command_preview") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_COMMAND_PREVIEW;
    if (SDL_strcmp(value != NULL ? value : "", "work_plane_grid") == 0 ||
        SDL_strcmp(value != NULL ? value : "", "grid") == 0)
    {
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORK_PLANE_GRID;
    }
    return 0u;
}

static unsigned int editor_debug_flags_from_json(yyjson_val *value)
{
    if (yyjson_is_str(value))
        return editor_debug_flag_from_string(yyjson_get_str(value));
    if (!yyjson_is_arr(value))
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;

    unsigned int flags = 0u;
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (yyjson_is_str(entry))
            flags |= editor_debug_flag_from_string(yyjson_get_str(entry));
    }
    return flags != 0u ? flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
}

static bool active_editor_debug_desc_from_json(const slayer3d_game_data_runtime *runtime,
                                               slayer3d_game_data_editor_debug_desc *out_desc,
                                               slayer3d_game_data_world_trace_desc *out_trace,
                                               slayer3d_game_data_editor_selection *out_selection)
{
    if (runtime == NULL)
        return false;
    yyjson_val *editor = active_editor_tooling_root(runtime);
    yyjson_val *overlay = obj_get(editor, "debug_overlay");
    if (!yyjson_is_obj(overlay) || !json_bool(overlay, "enabled", true) || out_desc == NULL)
        return false;

    SDL_zero(*out_desc);
    out_desc->flags = editor_debug_flags_from_json(obj_get(overlay, "flags"));
    out_desc->world_bounds_color = json_color(overlay, "world_bounds_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->selection_bounds_color = json_color(overlay, "selection_bounds_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->trace_color = json_color(overlay, "trace_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->face_normal_color = json_color(overlay, "face_normal_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->hit_marker_color = json_color(overlay, "hit_marker_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->command_preview_color = json_color(overlay, "command_preview_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->work_plane_grid_color = json_color(overlay, "work_plane_grid_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->normal_length = json_float(overlay, "normal_length", 0.75f);
    out_desc->hit_marker_size = json_float(overlay, "hit_marker_size", 0.1f);
    out_desc->work_plane_grid_size = json_float(overlay, "work_plane_grid_size", 16.0f);
    out_desc->work_plane_grid_spacing = json_float(overlay, "work_plane_grid_spacing", 1.0f);

    yyjson_val *selection_json = obj_get(editor, "selection");
    if (editor_trace_desc_from_json(runtime, selection_json, out_trace))
    {
        out_desc->trace = out_trace;
        out_desc->has_work_plane_grid = editor_work_plane_desc_from_trace_json(
            obj_get(selection_json, "trace"), &out_desc->work_plane_normal, &out_desc->work_plane_distance);
        if (out_selection != NULL && editor_selection_mode_is_click(selection_json) &&
            editor_selection_active_for_scene(runtime) && runtime->editor_active_selection.hit)
        {
            *out_selection = runtime->editor_active_selection;
            out_desc->selection = out_selection;
        }
        else if (out_selection != NULL &&
                 editor_pick_selection_from_json(runtime, selection_json, out_trace, out_selection))
        {
            out_desc->selection = out_selection;
        }
    }
    return true;
}

static bool emit_editor_debug_line(editor_debug_iteration_context *context, slayer3d_vec3 start, slayer3d_vec3 end)
{
    if (context == NULL || context->callback == NULL)
        return false;

    slayer3d_game_data_editor_debug_primitive primitive;
    SDL_zero(primitive);
    primitive.type = context->type;
    primitive.start = start;
    primitive.end = end;
    primitive.color = context->color;
    primitive.world_name = context->world_name;
    primitive.element_name = context->element_name;
    primitive.face_index = context->face_index;
    if (!context->callback(context->userdata, &primitive))
    {
        context->stopped = true;
        return false;
    }
    return true;
}

bool slayer3d_game_data_for_each_active_editor_debug_primitive(const slayer3d_game_data_runtime *runtime,
                                                               slayer3d_game_data_editor_debug_primitive_fn callback,
                                                               void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    slayer3d_game_data_editor_debug_desc desc;
    slayer3d_game_data_world_trace_desc trace;
    slayer3d_game_data_editor_selection selection;
    if (!active_editor_debug_desc_from_json(runtime, &desc, &trace, &selection))
        return true;
    return slayer3d_game_data_for_each_editor_debug_primitive(runtime, &desc, callback, userdata);
}

static bool emit_editor_debug_bounds(editor_debug_iteration_context *context, slayer3d_bounding_box bounds)
{
    const slayer3d_vec3 min = bounds.min;
    const slayer3d_vec3 max = bounds.max;
    const slayer3d_vec3 corners[8] = {
        slayer3d_vec3_make(min.x, min.y, min.z), slayer3d_vec3_make(max.x, min.y, min.z),
        slayer3d_vec3_make(max.x, min.y, max.z), slayer3d_vec3_make(min.x, min.y, max.z),
        slayer3d_vec3_make(min.x, max.y, min.z), slayer3d_vec3_make(max.x, max.y, min.z),
        slayer3d_vec3_make(max.x, max.y, max.z), slayer3d_vec3_make(min.x, max.y, max.z),
    };
    static const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };

    for (int i = 0; i < 12; ++i)
    {
        if (!emit_editor_debug_line(context, corners[edges[i][0]], corners[edges[i][1]]))
            return false;
    }
    return true;
}

static bool emit_editor_debug_work_plane_grid(const slayer3d_game_data_editor_debug_desc *desc,
                                              slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata)
{
    if (desc == NULL || callback == NULL || !desc->has_work_plane_grid ||
        slayer3d_vec3_length_squared(desc->work_plane_normal) <= 0.000001f)
    {
        return true;
    }

    const slayer3d_vec3 normal = slayer3d_vec3_normalize(desc->work_plane_normal);
    slayer3d_vec3 reference =
        SDL_fabsf(normal.y) < 0.95f ? slayer3d_vec3_make(0.0f, 1.0f, 0.0f) : slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    slayer3d_vec3 tangent = slayer3d_vec3_cross(reference, normal);
    if (slayer3d_vec3_length_squared(tangent) <= 0.000001f)
        tangent = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    tangent = slayer3d_vec3_normalize(tangent);
    const slayer3d_vec3 bitangent = slayer3d_vec3_normalize(slayer3d_vec3_cross(normal, tangent));
    const slayer3d_vec3 center = slayer3d_vec3_scale(normal, desc->work_plane_distance);
    const float half_size = desc->work_plane_grid_size > 0.0f ? desc->work_plane_grid_size : 16.0f;
    const float spacing = desc->work_plane_grid_spacing > 0.0f ? desc->work_plane_grid_spacing : 1.0f;
    const int line_count = SDL_min((int)SDL_floorf(half_size / spacing), 512);

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.color = editor_debug_color_or_default(desc->work_plane_grid_color, (slayer3d_color){90, 160, 190, 120});
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_WORK_PLANE_GRID;
    context.face_index = -1;

    for (int i = -line_count; i <= line_count; ++i)
    {
        const float offset = (float)i * spacing;
        const slayer3d_vec3 tangent_offset = slayer3d_vec3_scale(tangent, offset);
        const slayer3d_vec3 bitangent_offset = slayer3d_vec3_scale(bitangent, offset);
        if (!emit_editor_debug_line(&context,
                                    slayer3d_vec3_add(slayer3d_vec3_add(center, tangent_offset),
                                                      slayer3d_vec3_scale(bitangent, -half_size)),
                                    slayer3d_vec3_add(slayer3d_vec3_add(center, tangent_offset),
                                                      slayer3d_vec3_scale(bitangent, half_size))) ||
            !emit_editor_debug_line(&context,
                                    slayer3d_vec3_add(slayer3d_vec3_add(center, bitangent_offset),
                                                      slayer3d_vec3_scale(tangent, -half_size)),
                                    slayer3d_vec3_add(slayer3d_vec3_add(center, bitangent_offset),
                                                      slayer3d_vec3_scale(tangent, half_size))))
        {
            return false;
        }
    }
    return true;
}

typedef struct editor_world_bounds_context
{
    const slayer3d_game_data_editor_debug_desc *desc;
    slayer3d_game_data_editor_debug_primitive_fn callback;
    void *userdata;
    bool stopped;
} editor_world_bounds_context;

static bool emit_editor_debug_world_bounds(void *userdata, const slayer3d_game_data_world_model_instance *instance)
{
    editor_world_bounds_context *bounds_context = (editor_world_bounds_context *)userdata;
    if (bounds_context == NULL || bounds_context->desc == NULL || instance == NULL || !instance->has_bounds)
        return true;

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = bounds_context->callback;
    context.userdata = bounds_context->userdata;
    context.color =
        editor_debug_color_or_default(bounds_context->desc->world_bounds_color, (slayer3d_color){80, 170, 255, 220});
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_WORLD_BOUNDS_EDGE;
    context.world_name = instance->name;
    context.face_index = -1;
    if (!emit_editor_debug_bounds(&context, instance->bounds))
    {
        bounds_context->stopped = context.stopped;
        return false;
    }
    return true;
}

bool slayer3d_game_data_for_each_editor_debug_primitive(const slayer3d_game_data_runtime *runtime,
                                                        const slayer3d_game_data_editor_debug_desc *desc,
                                                        slayer3d_game_data_editor_debug_primitive_fn callback,
                                                        void *userdata)
{
    if (runtime == NULL || desc == NULL || callback == NULL)
        return false;

    const unsigned int flags = desc->flags != 0u ? desc->flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORK_PLANE_GRID) != 0u &&
        !emit_editor_debug_work_plane_grid(desc, callback, userdata))
    {
        return true;
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORLD_BOUNDS) != 0u)
    {
        editor_world_bounds_context bounds_context;
        SDL_zero(bounds_context);
        bounds_context.desc = desc;
        bounds_context.callback = callback;
        bounds_context.userdata = userdata;
        if (!slayer3d_game_data_for_each_world_model_instance(runtime, emit_editor_debug_world_bounds, &bounds_context))
        {
            return false;
        }
        if (bounds_context.stopped)
            return true;
    }

    const slayer3d_game_data_editor_selection *selection = desc->selection;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_BOUNDS) != 0u && selection != NULL && selection->hit &&
        selection->has_bounds)
    {
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color =
            editor_debug_color_or_default(desc->selection_bounds_color, (slayer3d_color){255, 220, 40, 255});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_SELECTION_BOUNDS_EDGE;
        context.world_name = selection->world_name;
        context.element_name = selection->element_name;
        context.face_index = selection->face_index;
        if (!emit_editor_debug_bounds(&context, selection->bounds))
            return true;
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_TRACE_RAY) != 0u && desc->trace != NULL)
    {
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color = editor_debug_color_or_default(desc->trace_color, (slayer3d_color){255, 255, 255, 180});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_TRACE_RAY;
        context.world_name = selection != NULL ? selection->world_name : NULL;
        context.element_name = selection != NULL ? selection->element_name : NULL;
        context.face_index = selection != NULL ? selection->face_index : -1;
        if (!emit_editor_debug_line(&context, desc->trace->start, desc->trace->end))
            return true;
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_FACE_NORMAL) != 0u && selection != NULL && selection->hit &&
        slayer3d_vec3_length_squared(selection->normal) > 0.000001f)
    {
        const float length = desc->normal_length > 0.0f ? desc->normal_length : 0.75f;
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color = editor_debug_color_or_default(desc->face_normal_color, (slayer3d_color){40, 255, 120, 255});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_FACE_NORMAL;
        context.world_name = selection->world_name;
        context.element_name = selection->element_name;
        context.face_index = selection->face_index;
        if (!emit_editor_debug_line(
                &context, selection->point,
                slayer3d_vec3_add(selection->point,
                                  slayer3d_vec3_scale(slayer3d_vec3_normalize(selection->normal), length))))
        {
            return true;
        }
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_HIT_MARKER) != 0u && selection != NULL && selection->hit)
    {
        const float size = desc->hit_marker_size > 0.0f ? desc->hit_marker_size : 0.1f;
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color = editor_debug_color_or_default(desc->hit_marker_color, (slayer3d_color){255, 80, 80, 255});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_HIT_MARKER;
        context.world_name = selection->world_name;
        context.element_name = selection->element_name;
        context.face_index = selection->face_index;
        if (!emit_editor_debug_line(&context,
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(-size, 0.0f, 0.0f)),
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(size, 0.0f, 0.0f))) ||
            !emit_editor_debug_line(&context,
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(0.0f, -size, 0.0f)),
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(0.0f, size, 0.0f))) ||
            !emit_editor_debug_line(&context,
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(0.0f, 0.0f, -size)),
                                    slayer3d_vec3_add(selection->point, slayer3d_vec3_make(0.0f, 0.0f, size))))
        {
            return true;
        }
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_COMMAND_PREVIEW) != 0u &&
        editor_command_preview_active_for_scene(runtime) && runtime->editor_command_preview.has_bounds)
    {
        const editor_command_preview_state *preview = &runtime->editor_command_preview;
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color = editor_debug_color_or_default(desc->command_preview_color, (slayer3d_color){80, 255, 255, 220});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_COMMAND_PREVIEW_BOUNDS_EDGE;
        context.world_name = preview->world_name;
        context.element_name = preview->element_name;
        context.face_index = preview->face_index;
        if (!emit_editor_debug_bounds(&context, preview->bounds))
            return true;
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_COMMAND_PREVIEW) != 0u &&
        editor_placement_preview_active_for_scene(runtime) && runtime->editor_placement_preview.has_bounds)
    {
        const editor_placement_preview_state *preview = &runtime->editor_placement_preview;
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color = editor_debug_color_or_default(desc->command_preview_color, (slayer3d_color){80, 255, 255, 220});
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_COMMAND_PREVIEW_BOUNDS_EDGE;
        context.world_name = preview->world_name;
        context.element_name = preview->mode;
        context.face_index = -1;
        if (!emit_editor_debug_bounds(&context, preview->bounds))
            return true;
    }
    return true;
}

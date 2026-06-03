/**
 * @file game_data_editor_debug_primitives.c
 * @brief Editor debug primitive emission helpers.
 */

#include "game_data_internal.h"

#include <float.h>

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

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

static bool emit_editor_selected_brush_bounds(const slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_editor_debug_desc *desc,
                                              slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata);
static bool emit_editor_overlapping_source_brush_bounds(const slayer3d_game_data_runtime *runtime,
                                                        const slayer3d_game_data_editor_debug_desc *desc,
                                                        slayer3d_game_data_editor_debug_primitive_fn callback,
                                                        void *userdata);
static bool emit_editor_selected_source_vertex_handles(const slayer3d_game_data_runtime *runtime,
                                                       const slayer3d_game_data_editor_debug_desc *desc,
                                                       slayer3d_game_data_editor_debug_primitive_fn callback,
                                                       void *userdata);
static bool emit_editor_selected_source_edge_handles(const slayer3d_game_data_runtime *runtime,
                                                     const slayer3d_game_data_editor_debug_desc *desc,
                                                     slayer3d_game_data_editor_debug_primitive_fn callback,
                                                     void *userdata);
static bool emit_editor_source_vertex_drag_guides(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_editor_debug_desc *desc,
                                                  slayer3d_game_data_editor_debug_primitive_fn callback,
                                                  void *userdata);
static bool emit_editor_source_vertex_add_preview(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_editor_debug_desc *desc,
                                                  slayer3d_game_data_editor_debug_primitive_fn callback,
                                                  void *userdata);
static bool emit_editor_debug_overlay_markers(const slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_editor_debug_desc *desc,
                                              slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata);
static bool emit_editor_clip_tool_primitives(const slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_editor_debug_desc *desc,
                                             slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata);
static bool emit_editor_debug_marker_cross(editor_debug_iteration_context *context, slayer3d_vec3 center, float size);
static bool emit_editor_debug_marker_orb(editor_debug_iteration_context *context, slayer3d_vec3 center, float radius);
static bool emit_editor_debug_hover_vertex_outline(editor_debug_iteration_context *context, slayer3d_vec3 center);
static bool emit_editor_debug_selected_vertex_outline(editor_debug_iteration_context *context, slayer3d_vec3 center);
static bool emit_editor_debug_vertex_hover_label(editor_debug_iteration_context *context, slayer3d_vec3 center,
                                                 const editor_brush_source_vertex *vertex);
static bool emit_editor_debug_source_brush_edges(editor_debug_iteration_context *context,
                                                 const brush_world_runtime *world,
                                                 const slayer3d_game_data_editor_selection *selection);

static slayer3d_color editor_debug_color_or_default(slayer3d_color color, slayer3d_color fallback)
{
    return color.a != 0 ? color : fallback;
}

static Uint8 editor_debug_mix_channel(Uint8 a, Uint8 b, float t)
{
    return (Uint8)SDL_clamp((int)((float)a + ((float)b - (float)a) * t + 0.5f), 0, 255);
}

static slayer3d_color editor_debug_selection_flash_color(slayer3d_color color)
{
    const slayer3d_color base = editor_debug_color_or_default(color, (slayer3d_color){255, 220, 40, 255});
    const Uint64 ticks = SDL_GetTicks();
    const float phase = (float)(ticks % 650U) / 650.0f;
    const float wave = 0.5f + 0.5f * SDL_sinf(phase * SDL_PI_F * 2.0f);
    const float mix = 0.2f + wave * 0.8f;
    const slayer3d_color flash = {255, 255, 255, 255};
    return (slayer3d_color){
        editor_debug_mix_channel(base.r, flash.r, mix),
        editor_debug_mix_channel(base.g, flash.g, mix),
        editor_debug_mix_channel(base.b, flash.b, mix),
        editor_debug_mix_channel(base.a < 220 ? 220 : base.a, flash.a, mix),
    };
}

static unsigned int editor_debug_flag_from_string(const char *value)
{
    if (SDL_strcmp(value != NULL ? value : "", "all") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if (SDL_strcmp(value != NULL ? value : "", "world_bounds") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORLD_BOUNDS;
    if (SDL_strcmp(value != NULL ? value : "", "selection_bounds") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_BOUNDS;
    if (SDL_strcmp(value != NULL ? value : "", "selection_face") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_FACE;
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
    if (SDL_strcmp(value != NULL ? value : "", "player_starts") == 0 ||
        SDL_strcmp(value != NULL ? value : "", "game_objects") == 0)
    {
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_PLAYER_STARTS;
    }
    if (SDL_strcmp(value != NULL ? value : "", "markers") == 0 ||
        SDL_strcmp(value != NULL ? value : "", "diagnostic_markers") == 0)
    {
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_DIAGNOSTIC_MARKERS;
    }
    if (SDL_strcmp(value != NULL ? value : "", "vertex_handles") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_VERTEX_HANDLES;
    if (SDL_strcmp(value != NULL ? value : "", "edge_handles") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_EDGE_HANDLES;
    if (SDL_strcmp(value != NULL ? value : "", "rotate_handles") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ROTATE_HANDLES;
    if (SDL_strcmp(value != NULL ? value : "", "clip_preview") == 0)
        return SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_CLIP_PREVIEW;
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
    out_desc->selection_face_color = json_color(overlay, "selection_face_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->trace_color = json_color(overlay, "trace_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->face_normal_color = json_color(overlay, "face_normal_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->hit_marker_color = json_color(overlay, "hit_marker_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->command_preview_color = json_color(overlay, "command_preview_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->work_plane_grid_color = json_color(overlay, "work_plane_grid_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->player_start_color = json_color(overlay, "player_start_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->vertex_handle_color = json_color(overlay, "vertex_handle_color", (slayer3d_color){0, 0, 0, 0});
    out_desc->normal_length = json_float(overlay, "normal_length", 0.75f);
    out_desc->hit_marker_size = json_float(overlay, "hit_marker_size", 0.1f);
    out_desc->work_plane_grid_size = json_float(overlay, "work_plane_grid_size", 16.0f);
    out_desc->work_plane_grid_spacing = json_float(overlay, "work_plane_grid_spacing", 1.0f);
    out_desc->player_start_radius = json_float(overlay, "player_start_radius", 0.35f);
    out_desc->player_start_height = json_float(overlay, "player_start_height", 1.8f);

    yyjson_val *selection_json = obj_get(editor, "selection");
    if (editor_trace_desc_from_json(runtime, selection_json, out_trace))
    {
        out_desc->trace = out_trace;
        out_desc->has_work_plane_grid = editor_work_plane_desc_from_trace_json(
            runtime, obj_get(selection_json, "trace"), &out_desc->work_plane_normal, &out_desc->work_plane_distance);
        const bool prefer_hover_selection = eval_data_condition(runtime, obj_get(overlay, "hover_selection_if"), NULL);
        if (out_selection != NULL && !prefer_hover_selection && editor_selection_mode_is_click(selection_json) &&
            editor_selection_active_for_scene(runtime) && runtime->editor_active_selection.hit)
        {
            *out_selection = resolved_editor_selection(runtime, &runtime->editor_active_selection);
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

static bool emit_editor_debug_label(editor_debug_iteration_context *context, slayer3d_vec3 position, const char *text)
{
    if (context == NULL || context->callback == NULL || text == NULL)
        return false;

    slayer3d_game_data_editor_debug_primitive primitive;
    SDL_zero(primitive);
    primitive.type = context->type;
    primitive.start = position;
    primitive.end = position;
    primitive.color = context->color;
    primitive.world_name = context->world_name;
    primitive.element_name = context->element_name;
    primitive.face_index = context->face_index;
    SDL_strlcpy(primitive.text, text, sizeof(primitive.text));
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
    if (!slayer3d_game_data_for_each_editor_debug_primitive(runtime, &desc, callback, userdata))
        return false;
    if (!emit_editor_selected_brush_bounds(runtime, &desc, callback, userdata))
        return false;
    if (!emit_editor_selected_source_edge_handles(runtime, &desc, callback, userdata))
        return false;
    if (!emit_editor_selected_source_vertex_handles(runtime, &desc, callback, userdata))
        return false;
    if (!emit_editor_source_vertex_drag_guides(runtime, &desc, callback, userdata))
        return false;
    if (!emit_editor_source_vertex_add_preview(runtime, &desc, callback, userdata))
        return false;
    if (!emit_editor_overlapping_source_brush_bounds(runtime, &desc, callback, userdata))
        return false;

    return emit_editor_debug_overlay_markers(runtime, &desc, callback, userdata);
}

static bool editor_debug_mode_is_vertex(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "vertex") == 0;
}

static bool editor_debug_mode_is_edge(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "edge") == 0;
}

static slayer3d_vec3 editor_source_vertex_coord_meters(const brush_world_runtime *world,
                                                       const editor_brush_source_vertex *vertex)
{
    const float meters_per_unit =
        world != NULL && world->editor_source_meters_per_unit > 0.0f ? world->editor_source_meters_per_unit : 0.001f;
    return slayer3d_vec3_make((float)vertex->coord[0] * meters_per_unit, (float)vertex->coord[1] * meters_per_unit,
                              (float)vertex->coord[2] * meters_per_unit);
}

static slayer3d_vec3 editor_source_coord_meters(const brush_world_runtime *world, const int coord[3])
{
    const float meters_per_unit =
        world != NULL && world->editor_source_meters_per_unit > 0.0f ? world->editor_source_meters_per_unit : 0.001f;
    return coord != NULL ? slayer3d_vec3_make((float)coord[0] * meters_per_unit, (float)coord[1] * meters_per_unit,
                                              (float)coord[2] * meters_per_unit)
                         : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static int editor_debug_source_index_for_selection(const brush_world_runtime *world,
                                                   const slayer3d_game_data_editor_selection *selection)
{
    if (world == NULL || selection == NULL || !selection->hit ||
        selection->type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
        return -1;
    if (selection->element_index >= 0 && selection->element_index < world->desc.brush_count)
    {
        const slayer3d_game_data_brush *brush = &world->desc.brushes[selection->element_index];
        int source_index = editor_brush_world_find_source_box_index(world, brush->name);
        if (source_index >= 0)
            return source_index;
        source_index = editor_brush_world_find_source_box_index(world, brush->editor.stable_id);
        if (source_index >= 0)
            return source_index;
    }
    int source_index = editor_brush_world_find_source_box_index(world, selection->element_name);
    if (source_index >= 0)
        return source_index;
    return -1;
}

static bool editor_debug_vertex_is_selected(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                            const char *vertex_stable_id)
{
    if (runtime == NULL || world_name == NULL || vertex_stable_id == NULL ||
        !editor_selected_vertices_active_for_scene(runtime))
    {
        return false;
    }

    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        if (SDL_strcmp(selection->world_name, world_name) == 0 &&
            SDL_strcmp(selection->vertex_stable_id, vertex_stable_id) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool editor_debug_vertex_is_hovered(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                           const char *vertex_stable_id)
{
    if (runtime == NULL || runtime->scene_state == NULL || world_name == NULL || vertex_stable_id == NULL ||
        !slayer3d_properties_get_bool(runtime->scene_state, "editor.vertex.hover.hit", false))
    {
        return false;
    }

    const char *hover_vertex = slayer3d_properties_get_string(runtime->scene_state, "editor.vertex.hover.vertex", "");
    return SDL_strcmp(hover_vertex, vertex_stable_id) == 0;
}

static float editor_debug_point_segment_distance_sq(slayer3d_vec3 point, slayer3d_vec3 start, slayer3d_vec3 end)
{
    const slayer3d_vec3 segment = slayer3d_vec3_sub(end, start);
    const float length_sq = slayer3d_vec3_length_squared(segment);
    if (length_sq <= 0.0000001f)
        return slayer3d_vec3_length_squared(slayer3d_vec3_sub(point, start));
    const float t = SDL_clamp(slayer3d_vec3_dot(slayer3d_vec3_sub(point, start), segment) / length_sq, 0.0f, 1.0f);
    const slayer3d_vec3 nearest = slayer3d_vec3_add(start, slayer3d_vec3_scale(segment, t));
    return slayer3d_vec3_length_squared(slayer3d_vec3_sub(point, nearest));
}

static int editor_debug_hovered_edge_index(const slayer3d_game_data_editor_debug_desc *desc,
                                           const brush_world_runtime *world, int source_index,
                                           const editor_brush_source_vertex_model *model)
{
    const slayer3d_game_data_editor_selection *hover = desc != NULL ? desc->selection : NULL;
    if (hover == NULL || world == NULL || model == NULL || !hover->hit || hover->world_name == NULL ||
        hover->type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
    {
        return -1;
    }
    const int hover_source_index = editor_debug_source_index_for_selection(world, hover);
    if (hover_source_index != source_index)
        return -1;

    const slayer3d_vec3 local_point = slayer3d_vec3_sub(hover->point, hover->world_position);
    int nearest_edge = -1;
    float nearest_distance_sq = FLT_MAX;
    for (int edge = 0; edge < model->edge_count; ++edge)
    {
        const editor_brush_source_edge *source_edge = &model->edges[edge];
        const int a = source_edge->vertex_indices[0];
        const int b = source_edge->vertex_indices[1];
        if (a < 0 || a >= model->vertex_count || b < 0 || b >= model->vertex_count)
            continue;
        const slayer3d_vec3 start = editor_source_vertex_coord_meters(world, &model->vertices[a]);
        const slayer3d_vec3 end = editor_source_vertex_coord_meters(world, &model->vertices[b]);
        const float distance_sq = editor_debug_point_segment_distance_sq(local_point, start, end);
        if (distance_sq < nearest_distance_sq)
        {
            nearest_distance_sq = distance_sq;
            nearest_edge = edge;
        }
    }
    return nearest_edge;
}

static bool editor_debug_edge_is_selected(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                          const char *brush_stable_id, const char *edge_stable_id)
{
    if (runtime == NULL || world_name == NULL || brush_stable_id == NULL || edge_stable_id == NULL ||
        !editor_selected_edges_active_for_scene(runtime))
    {
        return false;
    }
    for (int i = 0; i < runtime->editor_selected_edge_count; ++i)
    {
        const editor_source_edge_selection *selection = &runtime->editor_selected_edges[i];
        if (SDL_strcmp(selection->world_name, world_name) == 0 &&
            SDL_strcmp(selection->brush_stable_id, brush_stable_id) == 0 &&
            SDL_strcmp(selection->edge_stable_id, edge_stable_id) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool emit_editor_selected_source_edge_handles(const slayer3d_game_data_runtime *runtime,
                                                     const slayer3d_game_data_editor_debug_desc *desc,
                                                     slayer3d_game_data_editor_debug_primitive_fn callback,
                                                     void *userdata)
{
    if (runtime == NULL || desc == NULL || callback == NULL || runtime->editor_selected_brush_count <= 0 ||
        !editor_debug_mode_is_edge(runtime))
    {
        return true;
    }
    const unsigned int flags = desc->flags != 0u ? desc->flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_EDGE_HANDLES) == 0u)
        return true;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (runtime->editor_selected_brush_scene == NULL || active_scene == NULL ||
        SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0)
    {
        return true;
    }

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.face_index = -1;

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!selection.hit || selection.world_name == NULL)
            continue;
        const brush_world_runtime *world = find_brush_world_runtime(runtime, selection.world_name);
        const int source_index = editor_debug_source_index_for_selection(world, &selection);
        if (world == NULL || source_index < 0)
            continue;

        editor_brush_source_vertex_model model;
        if (!editor_brush_source_box_build_vertex_model(world, source_index, &model, NULL, 0))
            continue;
        const int hovered_edge = editor_debug_hovered_edge_index(desc, world, source_index, &model);

        context.world_name = selection.world_name;
        for (int edge = 0; edge < model.edge_count; ++edge)
        {
            const editor_brush_source_edge *source_edge = &model.edges[edge];
            const int a = source_edge->vertex_indices[0];
            const int b = source_edge->vertex_indices[1];
            if (a < 0 || a >= model.vertex_count || b < 0 || b >= model.vertex_count)
                continue;
            const slayer3d_vec3 start = slayer3d_vec3_add(selection.world_position,
                                                          editor_source_vertex_coord_meters(world, &model.vertices[a]));
            const slayer3d_vec3 end = slayer3d_vec3_add(selection.world_position,
                                                        editor_source_vertex_coord_meters(world, &model.vertices[b]));
            const slayer3d_vec3 midpoint = slayer3d_vec3_scale(slayer3d_vec3_add(start, end), 0.5f);

            context.element_name = source_edge->stable_id;
            context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_EDGE_EDITABLE_EDGE;
            context.color = editor_debug_selection_flash_color((slayer3d_color){255, 224, 64, 255});
            if (!emit_editor_debug_line(&context, start, end))
                return false;

            const bool selected = editor_debug_edge_is_selected(runtime, selection.world_name, model.brush_stable_id,
                                                                source_edge->stable_id);
            if (selected)
            {
                context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_EDGE_SELECTED_HANDLE;
                context.color = (slayer3d_color){255, 48, 48, 255};
            }
            else if (edge == hovered_edge)
            {
                context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_EDGE_HOVER_HANDLE;
                context.color = (slayer3d_color){255, 48, 48, 255};
            }
            else
            {
                context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_EDGE_HANDLE;
                context.color = (slayer3d_color){255, 224, 64, 255};
            }
            if (!emit_editor_debug_marker_cross(&context, midpoint, selected || edge == hovered_edge ? 0.16f : 0.11f))
                return false;
        }
    }
    return true;
}

static bool emit_editor_selected_source_vertex_handles(const slayer3d_game_data_runtime *runtime,
                                                       const slayer3d_game_data_editor_debug_desc *desc,
                                                       slayer3d_game_data_editor_debug_primitive_fn callback,
                                                       void *userdata)
{
    if (runtime == NULL || desc == NULL || callback == NULL || runtime->editor_selected_brush_count <= 0 ||
        !editor_debug_mode_is_vertex(runtime))
    {
        return true;
    }
    const unsigned int flags = desc->flags != 0u ? desc->flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_VERTEX_HANDLES) == 0u)
        return true;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (runtime->editor_selected_brush_scene == NULL || active_scene == NULL ||
        SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0)
    {
        return true;
    }

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.color = editor_debug_selection_flash_color(
        editor_debug_color_or_default(desc->vertex_handle_color, (slayer3d_color){255, 224, 64, 255}));
    context.face_index = -1;

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!selection.hit || selection.world_name == NULL)
            continue;
        const brush_world_runtime *world = find_brush_world_runtime(runtime, selection.world_name);
        const int source_index = editor_debug_source_index_for_selection(world, &selection);
        if (world == NULL || source_index < 0)
            continue;

        editor_brush_source_vertex_model model;
        if (!editor_brush_source_box_build_vertex_model(world, source_index, &model, NULL, 0))
            continue;
        context.world_name = selection.world_name;
        for (int vertex = 0; vertex < model.vertex_count; ++vertex)
        {
            context.element_name = model.vertices[vertex].stable_id;
            const slayer3d_vec3 center = slayer3d_vec3_add(
                selection.world_position, editor_source_vertex_coord_meters(world, &model.vertices[vertex]));
            const bool selected =
                editor_debug_vertex_is_selected(runtime, selection.world_name, model.vertices[vertex].stable_id);
            const bool hovered =
                editor_debug_vertex_is_hovered(runtime, selection.world_name, model.vertices[vertex].stable_id);

            if (hovered)
            {
                context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_HOVER_HANDLE;
                context.color = (slayer3d_color){255, 48, 48, 255};
                if (!emit_editor_debug_hover_vertex_outline(&context, center) ||
                    !emit_editor_debug_vertex_hover_label(&context, center, &model.vertices[vertex]))
                {
                    return false;
                }
            }
            if (selected)
            {
                context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_SELECTED_HANDLE;
                context.color = (slayer3d_color){255, 32, 32, 255};
                if (!emit_editor_debug_selected_vertex_outline(&context, center))
                    return false;
            }
            else
            {
                context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_HANDLE;
                context.color = editor_debug_selection_flash_color(
                    editor_debug_color_or_default(desc->vertex_handle_color, (slayer3d_color){255, 224, 64, 255}));
                if (!emit_editor_debug_marker_cross(&context, center, 0.12f))
                    return false;
            }
        }
    }
    return true;
}

static slayer3d_vec3 editor_drag_origin_coord_meters(const brush_world_runtime *world,
                                                     const editor_drag_vertex_origin *origin)
{
    const float meters_per_unit =
        world != NULL && world->editor_source_meters_per_unit > 0.0f ? world->editor_source_meters_per_unit : 0.001f;
    return slayer3d_vec3_make((float)origin->coord[0] * meters_per_unit, (float)origin->coord[1] * meters_per_unit,
                              (float)origin->coord[2] * meters_per_unit);
}

static bool editor_drag_origin_world_position(const slayer3d_game_data_runtime *runtime,
                                              const editor_drag_vertex_origin *origin, slayer3d_vec3 *out_position)
{
    if (out_position != NULL)
        *out_position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (runtime == NULL || origin == NULL || out_position == NULL || !editor_selected_brushes_active_for_scene(runtime))
        return false;

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!selection.hit || selection.world_name == NULL || selection.element_name == NULL ||
            SDL_strcmp(selection.world_name, origin->world_name) != 0)
        {
            continue;
        }
        const bool name_match = SDL_strcmp(selection.element_name, origin->brush_name) == 0;
        const bool stable_match = selection.element_editor != NULL && selection.element_editor->stable_id != NULL &&
                                  SDL_strcmp(selection.element_editor->stable_id, origin->brush_name) == 0;
        if (name_match || stable_match)
        {
            *out_position = selection.world_position;
            return true;
        }
    }
    return false;
}

static bool emit_editor_source_vertex_drag_guides(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_editor_debug_desc *desc,
                                                  slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || desc == NULL || callback == NULL || !editor_debug_mode_is_vertex(runtime))
        return true;

    const editor_drag_move_state *drag = &runtime->editor_drag_move;
    if (!drag->active || !drag->vertex_drag || drag->vertex_origin_count <= 0 ||
        slayer3d_vec3_length_squared(drag->applied_offset) <= 0.0000001f)
    {
        return true;
    }

    const unsigned int flags = desc->flags != 0u ? desc->flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_VERTEX_HANDLES) == 0u)
        return true;

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.color = (drag->axis_lock_y || drag->axis_lock_dominant) ? (slayer3d_color){255, 96, 72, 255}
                                                                    : (slayer3d_color){255, 224, 64, 255};
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_DRAG_GUIDE;
    context.face_index = -1;

    for (int i = 0; i < drag->vertex_origin_count; ++i)
    {
        const editor_drag_vertex_origin *origin = &drag->vertex_origins[i];
        const brush_world_runtime *world = find_brush_world_runtime(runtime, origin->world_name);
        slayer3d_vec3 world_position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        (void)editor_drag_origin_world_position(runtime, origin, &world_position);
        const slayer3d_vec3 start = slayer3d_vec3_add(world_position, editor_drag_origin_coord_meters(world, origin));
        const slayer3d_vec3 end = slayer3d_vec3_add(start, drag->applied_offset);
        context.world_name = origin->world_name;
        context.element_name = origin->vertex_stable_id;
        if (!emit_editor_debug_line(&context, start, end))
            return false;
    }
    return true;
}

static bool emit_editor_source_vertex_add_preview(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_editor_debug_desc *desc,
                                                  slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || desc == NULL || callback == NULL || runtime->scene_state == NULL ||
        !editor_debug_mode_is_vertex(runtime) ||
        !slayer3d_properties_get_bool(runtime->scene_state, "editor.vertex.add.preview.active", false))
    {
        return true;
    }

    const unsigned int flags = desc->flags != 0u ? desc->flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_VERTEX_HANDLES) == 0u)
        return true;

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.color = slayer3d_properties_get_bool(runtime->scene_state, "editor.vertex.add.preview.valid", false)
                        ? (slayer3d_color){96, 255, 160, 200}
                        : (slayer3d_color){255, 80, 80, 220};
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_ADD_PREVIEW;
    context.world_name = slayer3d_properties_get_string(runtime->scene_state, "editor.vertex.add.preview.world", "");
    context.element_name = "editor.vertex.add.preview";
    context.face_index = -1;

    const slayer3d_vec3 position = slayer3d_properties_get_vec3(
        runtime->scene_state, "editor.vertex.add.preview.position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return emit_editor_debug_marker_cross(&context, position, 0.16f);
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

static int editor_preview_axis_index(const editor_placement_preview_state *preview)
{
    if (preview == NULL || preview->axis == NULL)
        return 1;
    if (SDL_strcmp(preview->axis, "x") == 0)
        return 0;
    if (SDL_strcmp(preview->axis, "z") == 0)
        return 2;
    return 1;
}

static float editor_vec3_component(slayer3d_vec3 value, int axis)
{
    if (axis == 0)
        return value.x;
    if (axis == 2)
        return value.z;
    return value.y;
}

static void editor_set_vec3_component(slayer3d_vec3 *value, int axis, float component)
{
    if (value == NULL)
        return;
    if (axis == 0)
        value->x = component;
    else if (axis == 2)
        value->z = component;
    else
        value->y = component;
}

static float editor_preview_base_plane_coordinate(const slayer3d_game_data_runtime *runtime,
                                                  const editor_placement_preview_state *preview, int axis)
{
    if (runtime != NULL && runtime->editor_drag_create.active && axis == runtime->editor_drag_create.extrusion_axis)
    {
        const editor_drag_create_state *drag = &runtime->editor_drag_create;
        const float base = (float)drag->start_cell[axis] * SDL_max(drag->grid_size, 0.001f);
        if (base >= editor_vec3_component(preview->bounds.min, axis) - 0.001f &&
            base <= editor_vec3_component(preview->bounds.max, axis) + 0.001f)
        {
            return base;
        }
    }
    return editor_vec3_component(preview->bounds.min, axis);
}

static bool emit_editor_debug_placement_footprint(editor_debug_iteration_context *context,
                                                  const slayer3d_game_data_runtime *runtime,
                                                  const editor_placement_preview_state *preview)
{
    if (context == NULL || preview == NULL || !preview->has_bounds)
        return false;

    const int axis = editor_preview_axis_index(preview);
    const int u_axis = axis == 0 ? 1 : 0;
    const int v_axis = axis == 2 ? 1 : 2;
    const float base = editor_preview_base_plane_coordinate(runtime, preview, axis);
    slayer3d_vec3 corners[4] = {preview->bounds.min, preview->bounds.min, preview->bounds.min, preview->bounds.min};
    for (int corner = 0; corner < 4; ++corner)
        editor_set_vec3_component(&corners[corner], axis, base);
    editor_set_vec3_component(&corners[1], u_axis, editor_vec3_component(preview->bounds.max, u_axis));
    editor_set_vec3_component(&corners[2], u_axis, editor_vec3_component(preview->bounds.max, u_axis));
    editor_set_vec3_component(&corners[2], v_axis, editor_vec3_component(preview->bounds.max, v_axis));
    editor_set_vec3_component(&corners[3], v_axis, editor_vec3_component(preview->bounds.max, v_axis));

    for (int i = 0; i < 4; ++i)
    {
        if (!emit_editor_debug_line(context, corners[i], corners[(i + 1) % 4]))
            return false;
    }
    return true;
}

static bool emit_editor_debug_placement_axis(editor_debug_iteration_context *context,
                                             const slayer3d_game_data_runtime *runtime,
                                             const editor_placement_preview_state *preview)
{
    if (context == NULL || preview == NULL || !preview->has_bounds)
        return false;

    const int axis = editor_preview_axis_index(preview);
    const int u_axis = axis == 0 ? 1 : 0;
    const int v_axis = axis == 2 ? 1 : 2;
    const float base = editor_preview_base_plane_coordinate(runtime, preview, axis);
    const float min_axis = editor_vec3_component(preview->bounds.min, axis);
    const float max_axis = editor_vec3_component(preview->bounds.max, axis);
    const float end = SDL_fabsf(base - min_axis) <= SDL_fabsf(base - max_axis) ? max_axis : min_axis;

    slayer3d_vec3 start = preview->bounds.min;
    slayer3d_vec3 stop = preview->bounds.min;
    editor_set_vec3_component(
        &start, u_axis,
        (editor_vec3_component(preview->bounds.min, u_axis) + editor_vec3_component(preview->bounds.max, u_axis)) *
            0.5f);
    editor_set_vec3_component(
        &start, v_axis,
        (editor_vec3_component(preview->bounds.min, v_axis) + editor_vec3_component(preview->bounds.max, v_axis)) *
            0.5f);
    editor_set_vec3_component(&start, axis, base);
    stop = start;
    editor_set_vec3_component(&stop, axis, end);
    return emit_editor_debug_line(context, start, stop);
}

static bool emit_editor_debug_source_brush_edges(editor_debug_iteration_context *context,
                                                 const brush_world_runtime *world,
                                                 const slayer3d_game_data_editor_selection *selection)
{
    if (context == NULL || world == NULL || selection == NULL || !selection->hit)
        return false;

    const int source_index = editor_debug_source_index_for_selection(world, selection);
    editor_brush_source_vertex_model model;
    if (source_index < 0 || !editor_brush_source_box_build_vertex_model(world, source_index, &model, NULL, 0))
        return false;

    context->world_name = selection->world_name;
    context->element_name = selection->element_name;
    for (int edge = 0; edge < model.edge_count; ++edge)
    {
        const editor_brush_source_edge *source_edge = &model.edges[edge];
        const int a = source_edge->vertex_indices[0];
        const int b = source_edge->vertex_indices[1];
        if (a < 0 || a >= model.vertex_count || b < 0 || b >= model.vertex_count)
            continue;
        const slayer3d_vec3 start =
            slayer3d_vec3_add(selection->world_position, editor_source_vertex_coord_meters(world, &model.vertices[a]));
        const slayer3d_vec3 end =
            slayer3d_vec3_add(selection->world_position, editor_source_vertex_coord_meters(world, &model.vertices[b]));
        if (!emit_editor_debug_line(context, start, end))
            return false;
    }
    return true;
}

static bool editor_clip_preview_origin(const slayer3d_game_data_runtime *runtime, const editor_clip_tool_state *tool,
                                       slayer3d_vec3 *out_origin)
{
    if (out_origin != NULL)
        *out_origin = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (runtime == NULL || tool == NULL || out_origin == NULL || !editor_selected_brushes_active_for_scene(runtime))
        return false;

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (selection.hit && selection.world_name != NULL && SDL_strcmp(selection.world_name, tool->world_name) == 0)
        {
            *out_origin = selection.world_position;
            return true;
        }
    }
    return false;
}

static const char *editor_debug_clip_keep_mode_name(editor_brush_source_clip_keep_mode mode)
{
    switch (mode)
    {
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT:
        return "front";
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK:
        return "back";
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH:
        return "both";
    default:
        return "front";
    }
}

static slayer3d_vec3 editor_clip_source_point_world(const brush_world_runtime *world, slayer3d_vec3 origin,
                                                    slayer3d_vec3 point)
{
    const float meters_per_unit =
        world != NULL && world->editor_source_meters_per_unit > 0.0f ? world->editor_source_meters_per_unit : 0.001f;
    return slayer3d_vec3_add(
        origin, slayer3d_vec3_make(point.x * meters_per_unit, point.y * meters_per_unit, point.z * meters_per_unit));
}

static slayer3d_vec3 editor_clip_source_normal(const editor_clip_tool_state *tool)
{
    if (tool == NULL)
        return slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    if (tool->point_count >= 3)
    {
        const slayer3d_vec3 ab = slayer3d_vec3_sub(tool->points[1], tool->points[0]);
        const slayer3d_vec3 ac = slayer3d_vec3_sub(tool->points[2], tool->points[0]);
        const slayer3d_vec3 normal = slayer3d_vec3_cross(ab, ac);
        if (slayer3d_vec3_length_squared(normal) > 0.000001f)
            return slayer3d_vec3_normalize(normal);
    }
    if (tool->has_work_plane_normal && slayer3d_vec3_length_squared(tool->work_plane_normal) > 0.000001f)
        return slayer3d_vec3_normalize(tool->work_plane_normal);
    return slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
}

static bool emit_editor_clip_point_handles(editor_debug_iteration_context *context, const brush_world_runtime *world,
                                           const editor_clip_tool_state *tool, slayer3d_vec3 origin)
{
    if (context == NULL || world == NULL || tool == NULL)
        return false;

    for (int i = 0; i < tool->point_count && i < SLAYER3D_EDITOR_CLIP_TOOL_MAX_POINTS; ++i)
    {
        char element_name[48];
        SDL_snprintf(element_name, sizeof(element_name), "clip.point.%d", i);
        context->element_name = element_name;
        context->type = i == tool->hovered_point ? SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_POINT_HOVER_HANDLE
                                                 : SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_POINT_HANDLE;
        context->color =
            i == tool->hovered_point ? (slayer3d_color){255, 230, 80, 255} : (slayer3d_color){255, 150, 40, 255};
        const float radius = i == tool->hovered_point ? 0.17f : 0.125f;
        if (!emit_editor_debug_marker_orb(context, editor_clip_source_point_world(world, origin, tool->points[i]),
                                          radius))
        {
            return false;
        }
    }
    return true;
}

static bool emit_editor_clip_point_lines(editor_debug_iteration_context *context, const brush_world_runtime *world,
                                         const editor_clip_tool_state *tool, slayer3d_vec3 origin)
{
    if (context == NULL || world == NULL || tool == NULL || tool->point_count < 2)
        return true;

    context->type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_LINE;
    context->color = (slayer3d_color){255, 200, 60, 255};
    context->element_name = "clip.line";
    const slayer3d_vec3 p0 = editor_clip_source_point_world(world, origin, tool->points[0]);
    const slayer3d_vec3 p1 = editor_clip_source_point_world(world, origin, tool->points[1]);
    if (!emit_editor_debug_line(context, p0, p1))
        return false;
    if (tool->point_count < 3)
        return true;
    const slayer3d_vec3 p2 = editor_clip_source_point_world(world, origin, tool->points[2]);
    return emit_editor_debug_line(context, p1, p2) && emit_editor_debug_line(context, p2, p0);
}

static bool emit_editor_clip_plane_indicator(editor_debug_iteration_context *context, const brush_world_runtime *world,
                                             const editor_clip_tool_state *tool, slayer3d_vec3 origin)
{
    if (context == NULL || world == NULL || tool == NULL || tool->point_count < 2)
        return true;

    const slayer3d_vec3 p0 = editor_clip_source_point_world(world, origin, tool->points[0]);
    const slayer3d_vec3 p1 = editor_clip_source_point_world(world, origin, tool->points[1]);
    const slayer3d_vec3 line = slayer3d_vec3_sub(p1, p0);
    const float line_length = slayer3d_vec3_length(line);
    if (line_length <= 0.0001f)
        return true;

    const slayer3d_vec3 line_dir = slayer3d_vec3_normalize(line);
    slayer3d_vec3 normal = editor_clip_source_normal(tool);
    if (slayer3d_vec3_length_squared(normal) <= 0.000001f)
        normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    slayer3d_vec3 side = slayer3d_vec3_cross(normal, line_dir);
    if (slayer3d_vec3_length_squared(side) <= 0.000001f)
        side = slayer3d_vec3_cross(slayer3d_vec3_make(0.0f, 1.0f, 0.0f), line_dir);
    if (slayer3d_vec3_length_squared(side) <= 0.000001f)
        side = slayer3d_vec3_cross(slayer3d_vec3_make(1.0f, 0.0f, 0.0f), line_dir);
    if (slayer3d_vec3_length_squared(side) <= 0.000001f)
        return true;
    side = slayer3d_vec3_normalize(side);

    const slayer3d_vec3 center = slayer3d_vec3_scale(slayer3d_vec3_add(p0, p1), 0.5f);
    const slayer3d_vec3 half_line = slayer3d_vec3_scale(line_dir, SDL_max(line_length * 0.55f, 0.45f));
    const slayer3d_vec3 half_side = slayer3d_vec3_scale(side, SDL_max(line_length * 0.35f, 0.35f));
    const slayer3d_vec3 a = slayer3d_vec3_add(slayer3d_vec3_sub(center, half_line), half_side);
    const slayer3d_vec3 b = slayer3d_vec3_add(slayer3d_vec3_add(center, half_line), half_side);
    const slayer3d_vec3 c = slayer3d_vec3_sub(slayer3d_vec3_add(center, half_line), half_side);
    const slayer3d_vec3 d = slayer3d_vec3_sub(slayer3d_vec3_sub(center, half_line), half_side);

    context->type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_PLANE_EDGE;
    context->color = (slayer3d_color){95, 220, 255, 210};
    context->element_name = "clip.plane";
    return emit_editor_debug_line(context, a, b) && emit_editor_debug_line(context, b, c) &&
           emit_editor_debug_line(context, c, d) && emit_editor_debug_line(context, d, a);
}

static bool emit_editor_clip_status_label(editor_debug_iteration_context *context, const brush_world_runtime *world,
                                          const editor_clip_tool_state *tool, slayer3d_vec3 origin)
{
    if (context == NULL || world == NULL || tool == NULL || tool->point_count <= 0)
        return true;

    char label[96];
    SDL_snprintf(label, sizeof(label), "Clip: %d pts  keep %s  %s", tool->point_count,
                 editor_debug_clip_keep_mode_name(tool->keep_mode), tool->preview_valid ? "valid" : "invalid");

    slayer3d_vec3 label_position = editor_clip_source_point_world(world, origin, tool->points[0]);
    if (tool->point_count >= 2)
    {
        const slayer3d_vec3 p1 = editor_clip_source_point_world(world, origin, tool->points[1]);
        label_position = slayer3d_vec3_scale(slayer3d_vec3_add(label_position, p1), 0.5f);
    }
    label_position = slayer3d_vec3_add(label_position, slayer3d_vec3_make(0.0f, 0.32f, 0.0f));

    context->type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_STATUS_LABEL;
    context->color = tool->preview_valid ? (slayer3d_color){220, 250, 255, 255} : (slayer3d_color){255, 105, 105, 255};
    context->element_name = "clip.status";
    return emit_editor_debug_label(context, label_position, label);
}

static slayer3d_color editor_clip_snap_target_color(const editor_clip_tool_state *tool)
{
    if (tool == NULL)
        return (slayer3d_color){180, 220, 255, 255};
    if (SDL_strcmp(tool->snap_kind, "vertex") == 0)
        return (slayer3d_color){255, 70, 70, 255};
    if (SDL_strcmp(tool->snap_kind, "edge") == 0)
        return (slayer3d_color){80, 220, 255, 255};
    if (SDL_strcmp(tool->snap_kind, "face") == 0)
        return (slayer3d_color){230, 120, 255, 255};
    return (slayer3d_color){180, 220, 255, 255};
}

static bool emit_editor_clip_snap_target(editor_debug_iteration_context *context, const brush_world_runtime *world,
                                         const editor_clip_tool_state *tool, slayer3d_vec3 origin)
{
    if (context == NULL || world == NULL || tool == NULL || !tool->has_snap_target ||
        SDL_strcmp(tool->snap_kind, "grid") == 0)
    {
        return true;
    }

    char label[64];
    SDL_snprintf(label, sizeof(label), "snap %s", tool->snap_kind);
    const slayer3d_vec3 target_position = editor_clip_source_point_world(world, origin, tool->snap_point);

    context->type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_SNAP_TARGET;
    context->color = editor_clip_snap_target_color(tool);
    context->element_name = tool->snap_target;
    if (!emit_editor_debug_marker_orb(context, target_position, 0.145f))
        return false;

    context->element_name = "clip.snap.label";
    return emit_editor_debug_label(context, slayer3d_vec3_add(target_position, slayer3d_vec3_make(0.0f, 0.22f, 0.0f)),
                                   label);
}

static bool emit_editor_clip_preview_box_edges(editor_debug_iteration_context *context,
                                               const brush_world_runtime *world,
                                               const editor_brush_source_box_runtime *box, slayer3d_vec3 origin)
{
    if (context == NULL || world == NULL || box == NULL)
        return false;

    editor_brush_source_vertex_model model;
    if (!editor_brush_source_box_runtime_build_vertex_model(world, -1, box, &model, NULL, 0))
        return false;

    context->element_name = box->stable_id != NULL ? box->stable_id : box->name;
    for (int edge = 0; edge < model.edge_count; ++edge)
    {
        const editor_brush_source_edge *source_edge = &model.edges[edge];
        const int a = source_edge->vertex_indices[0];
        const int b = source_edge->vertex_indices[1];
        if (a < 0 || a >= model.vertex_count || b < 0 || b >= model.vertex_count)
            continue;
        const slayer3d_vec3 start =
            slayer3d_vec3_add(origin, editor_source_coord_meters(world, model.vertices[a].coord));
        const slayer3d_vec3 end = slayer3d_vec3_add(origin, editor_source_coord_meters(world, model.vertices[b].coord));
        if (!emit_editor_debug_line(context, start, end))
            return false;
    }
    return true;
}

static bool emit_editor_clip_preview_edges(editor_debug_iteration_context *context, const brush_world_runtime *world,
                                           const editor_clip_tool_state *tool, slayer3d_vec3 origin)
{
    if (context == NULL || world == NULL || tool == NULL || !tool->preview_valid || !tool->preview_has_results)
        return true;

    context->type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_PREVIEW_KEPT_EDGE;
    context->color = (slayer3d_color){80, 255, 120, 245};
    for (int i = 0; i < tool->preview_kept_count && i < SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY; ++i)
    {
        if (!emit_editor_clip_preview_box_edges(context, world, &tool->preview_kept[i], origin))
            return true;
    }

    context->type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_PREVIEW_DISCARDED_EDGE;
    context->color = (slayer3d_color){255, 80, 80, 150};
    for (int i = 0; i < tool->preview_discarded_count && i < SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY; ++i)
    {
        if (!emit_editor_clip_preview_box_edges(context, world, &tool->preview_discarded[i], origin))
            return true;
    }
    return true;
}

static bool emit_editor_clip_tool_primitives(const slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_editor_debug_desc *desc,
                                             slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || desc == NULL || callback == NULL)
        return true;
    (void)desc;

    const editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    if (!tool->active)
        return true;

    const brush_world_runtime *world = find_brush_world_runtime(runtime, tool->world_name);
    if (world == NULL)
        return true;

    slayer3d_vec3 origin;
    editor_clip_preview_origin(runtime, tool, &origin);

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.world_name = tool->world_name;
    context.face_index = -1;

    if (!emit_editor_clip_point_handles(&context, world, tool, origin) ||
        !emit_editor_clip_point_lines(&context, world, tool, origin) ||
        !emit_editor_clip_plane_indicator(&context, world, tool, origin) ||
        !emit_editor_clip_status_label(&context, world, tool, origin) ||
        !emit_editor_clip_snap_target(&context, world, tool, origin) ||
        !emit_editor_clip_preview_edges(&context, world, tool, origin))
    {
        return true;
    }
    return true;
}

static bool editor_source_box_contents_are_structural(unsigned int contents)
{
    if (contents == 0u)
        contents = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    return (contents & (SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP |
                        SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP | SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY)) != 0u;
}

static bool editor_source_intervals_overlap_positive(int a_min, int a_max, int b_min, int b_max)
{
    return SDL_max(a_min, b_min) < SDL_min(a_max, b_max);
}

static bool editor_source_boxes_overlap_positive(const editor_brush_source_box_runtime *a,
                                                 const editor_brush_source_box_runtime *b)
{
    return a != NULL && b != NULL &&
           editor_source_intervals_overlap_positive(a->min[0], a->max[0], b->min[0], b->max[0]) &&
           editor_source_intervals_overlap_positive(a->min[1], a->max[1], b->min[1], b->max[1]) &&
           editor_source_intervals_overlap_positive(a->min[2], a->max[2], b->min[2], b->max[2]);
}

static bool emit_editor_selected_brush_bounds(const slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_editor_debug_desc *desc,
                                              slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || desc == NULL || callback == NULL || runtime->editor_selected_brush_count <= 0)
        return true;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (runtime->editor_selected_brush_scene == NULL || active_scene == NULL ||
        SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0)
    {
        return true;
    }

    const unsigned int flags = desc->flags != 0u ? desc->flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_BOUNDS) == 0u)
        return true;

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.color = editor_debug_selection_flash_color(desc->selection_bounds_color);
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_SELECTION_BOUNDS_EDGE;

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!selection.hit || selection.type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD || !selection.has_bounds)
            continue;
        if (desc->selection != NULL && desc->selection->hit && desc->selection->world_name != NULL &&
            desc->selection->element_name != NULL && selection.world_name != NULL && selection.element_name != NULL &&
            SDL_strcmp(desc->selection->world_name, selection.world_name) == 0 &&
            SDL_strcmp(desc->selection->element_name, selection.element_name) == 0)
        {
            continue;
        }
        context.world_name = selection.world_name;
        context.element_name = selection.element_name;
        context.face_index = selection.face_index;
        const brush_world_runtime *world = find_brush_world_runtime(runtime, selection.world_name);
        if (emit_editor_debug_source_brush_edges(&context, world, &selection))
            continue;
        if (!emit_editor_debug_bounds(&context, selection.bounds))
            return false;
    }
    return true;
}

static bool emit_editor_overlapping_source_brush_bounds(const slayer3d_game_data_runtime *runtime,
                                                        const slayer3d_game_data_editor_debug_desc *desc,
                                                        slayer3d_game_data_editor_debug_primitive_fn callback,
                                                        void *userdata)
{
    if (runtime == NULL || desc == NULL || callback == NULL)
        return true;
    const unsigned int flags = desc->flags != 0u ? desc->flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_BOUNDS) == 0u)
        return true;

    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        const brush_world_runtime *world = &runtime->brush_worlds[world_index];
        if (!world->editor_has_source_model || world->editor_source_box_count <= 1)
            continue;

        for (int a = 0; a < world->editor_source_box_count; ++a)
        {
            const editor_brush_source_box_runtime *box = &world->editor_source_boxes[a];
            if (!editor_source_box_contents_are_structural(box->contents))
                continue;

            bool overlaps = false;
            for (int b = 0; b < world->editor_source_box_count; ++b)
            {
                if (a == b)
                    continue;
                const editor_brush_source_box_runtime *other = &world->editor_source_boxes[b];
                if (editor_source_box_contents_are_structural(other->contents) &&
                    editor_source_boxes_overlap_positive(box, other))
                {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps)
                continue;

            editor_debug_iteration_context context;
            SDL_zero(context);
            context.callback = callback;
            context.userdata = userdata;
            context.color = editor_debug_selection_flash_color((slayer3d_color){255, 48, 48, 255});
            context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_SELECTION_BOUNDS_EDGE;
            context.world_name = world->desc.name;
            context.element_name = box->name != NULL ? box->name : box->stable_id;
            context.face_index = -1;
            if (!emit_editor_debug_bounds(&context, editor_brush_source_box_bounds_meters(world, box)))
                return false;
        }
    }
    return true;
}

static bool editor_selection_face_corners(const slayer3d_game_data_editor_selection *selection,
                                          slayer3d_vec3 out_corners[4])
{
    if (selection == NULL || out_corners == NULL || !selection->hit || !selection->has_bounds ||
        selection->type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD || selection->face_index < 0)
    {
        return false;
    }

    const slayer3d_vec3 min = selection->bounds.min;
    const slayer3d_vec3 max = selection->bounds.max;
    switch (selection->face_index)
    {
    case 0:
        out_corners[0] = slayer3d_vec3_make(max.x, min.y, min.z);
        out_corners[1] = slayer3d_vec3_make(max.x, min.y, max.z);
        out_corners[2] = slayer3d_vec3_make(max.x, max.y, max.z);
        out_corners[3] = slayer3d_vec3_make(max.x, max.y, min.z);
        return true;
    case 1:
        out_corners[0] = slayer3d_vec3_make(min.x, min.y, min.z);
        out_corners[1] = slayer3d_vec3_make(min.x, max.y, min.z);
        out_corners[2] = slayer3d_vec3_make(min.x, max.y, max.z);
        out_corners[3] = slayer3d_vec3_make(min.x, min.y, max.z);
        return true;
    case 2:
        out_corners[0] = slayer3d_vec3_make(min.x, max.y, min.z);
        out_corners[1] = slayer3d_vec3_make(max.x, max.y, min.z);
        out_corners[2] = slayer3d_vec3_make(max.x, max.y, max.z);
        out_corners[3] = slayer3d_vec3_make(min.x, max.y, max.z);
        return true;
    case 3:
        out_corners[0] = slayer3d_vec3_make(min.x, min.y, min.z);
        out_corners[1] = slayer3d_vec3_make(min.x, min.y, max.z);
        out_corners[2] = slayer3d_vec3_make(max.x, min.y, max.z);
        out_corners[3] = slayer3d_vec3_make(max.x, min.y, min.z);
        return true;
    case 4:
        out_corners[0] = slayer3d_vec3_make(min.x, min.y, max.z);
        out_corners[1] = slayer3d_vec3_make(min.x, max.y, max.z);
        out_corners[2] = slayer3d_vec3_make(max.x, max.y, max.z);
        out_corners[3] = slayer3d_vec3_make(max.x, min.y, max.z);
        return true;
    case 5:
        out_corners[0] = slayer3d_vec3_make(min.x, min.y, min.z);
        out_corners[1] = slayer3d_vec3_make(max.x, min.y, min.z);
        out_corners[2] = slayer3d_vec3_make(max.x, max.y, min.z);
        out_corners[3] = slayer3d_vec3_make(min.x, max.y, min.z);
        return true;
    default:
        return false;
    }
}

static bool emit_editor_debug_selection_face(const slayer3d_game_data_editor_debug_desc *desc,
                                             const slayer3d_game_data_editor_selection *selection,
                                             slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata)
{
    slayer3d_vec3 corners[4];
    if (desc == NULL || selection == NULL || callback == NULL || !editor_selection_face_corners(selection, corners))
        return true;

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.color = editor_debug_selection_flash_color(
        editor_debug_color_or_default(desc->selection_face_color, (slayer3d_color){110, 255, 180, 255}));
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_SELECTION_FACE_EDGE;
    context.world_name = selection->world_name;
    context.element_name = selection->element_name;
    context.face_index = selection->face_index;

    for (int i = 0; i < 4; ++i)
    {
        if (!emit_editor_debug_line(&context, corners[i], corners[(i + 1) % 4]))
            return false;
    }
    const slayer3d_vec3 normal = slayer3d_vec3_normalize(selection->normal);
    slayer3d_vec3 tangent = slayer3d_vec3_sub(corners[1], corners[0]);
    if (slayer3d_vec3_length_squared(tangent) <= 0.000001f)
        tangent = slayer3d_vec3_cross(slayer3d_vec3_make(0.0f, 1.0f, 0.0f), normal);
    tangent = slayer3d_vec3_normalize(tangent);
    slayer3d_vec3 bitangent = slayer3d_vec3_normalize(slayer3d_vec3_cross(normal, tangent));
    const float radius = 0.07f;
    const int segments = 12;
    for (int corner = 0; corner < 4; ++corner)
    {
        slayer3d_vec3 previous = slayer3d_vec3_add(corners[corner], slayer3d_vec3_scale(tangent, radius));
        for (int segment = 1; segment <= segments; ++segment)
        {
            const float angle = ((float)segment / (float)segments) * 6.28318530718f;
            const slayer3d_vec3 offset = slayer3d_vec3_add(slayer3d_vec3_scale(tangent, SDL_cosf(angle) * radius),
                                                           slayer3d_vec3_scale(bitangent, SDL_sinf(angle) * radius));
            const slayer3d_vec3 current = slayer3d_vec3_add(corners[corner], offset);
            if (!emit_editor_debug_line(&context, previous, current))
                return false;
            previous = current;
        }
    }
    return true;
}

static bool emit_editor_debug_marker_cross(editor_debug_iteration_context *context, slayer3d_vec3 center, float size)
{
    const float half_size = size > 0.0f ? size * 0.5f : 0.225f;
    const slayer3d_vec3 x = slayer3d_vec3_make(half_size, 0.0f, 0.0f);
    const slayer3d_vec3 y = slayer3d_vec3_make(0.0f, half_size, 0.0f);
    const slayer3d_vec3 z = slayer3d_vec3_make(0.0f, 0.0f, half_size);
    return emit_editor_debug_line(context, slayer3d_vec3_sub(center, x), slayer3d_vec3_add(center, x)) &&
           emit_editor_debug_line(context, slayer3d_vec3_sub(center, y), slayer3d_vec3_add(center, y)) &&
           emit_editor_debug_line(context, slayer3d_vec3_sub(center, z), slayer3d_vec3_add(center, z)) &&
           emit_editor_debug_line(context, slayer3d_vec3_sub(slayer3d_vec3_sub(center, x), z),
                                  slayer3d_vec3_add(slayer3d_vec3_add(center, x), z)) &&
           emit_editor_debug_line(context, slayer3d_vec3_add(slayer3d_vec3_sub(center, x), z),
                                  slayer3d_vec3_add(slayer3d_vec3_sub(center, z), x));
}

static bool emit_editor_debug_marker_orb(editor_debug_iteration_context *context, slayer3d_vec3 center, float radius)
{
    const float r = radius > 0.0f ? radius : 0.1f;
    const int segments = 18;
    const slayer3d_vec3 axes[3][2] = {
        {slayer3d_vec3_make(1.0f, 0.0f, 0.0f), slayer3d_vec3_make(0.0f, 1.0f, 0.0f)},
        {slayer3d_vec3_make(1.0f, 0.0f, 0.0f), slayer3d_vec3_make(0.0f, 0.0f, 1.0f)},
        {slayer3d_vec3_make(0.0f, 1.0f, 0.0f), slayer3d_vec3_make(0.0f, 0.0f, 1.0f)},
    };

    for (int plane = 0; plane < 3; ++plane)
    {
        slayer3d_vec3 previous = slayer3d_vec3_add(center, slayer3d_vec3_scale(axes[plane][0], r));
        for (int segment = 1; segment <= segments; ++segment)
        {
            const float angle = ((float)segment / (float)segments) * SDL_PI_F * 2.0f;
            const slayer3d_vec3 offset = slayer3d_vec3_add(slayer3d_vec3_scale(axes[plane][0], SDL_cosf(angle) * r),
                                                           slayer3d_vec3_scale(axes[plane][1], SDL_sinf(angle) * r));
            const slayer3d_vec3 current = slayer3d_vec3_add(center, offset);
            if (!emit_editor_debug_line(context, previous, current))
                return false;
            previous = current;
        }
    }

    const slayer3d_vec3 x = slayer3d_vec3_make(r, 0.0f, 0.0f);
    const slayer3d_vec3 y = slayer3d_vec3_make(0.0f, r, 0.0f);
    const slayer3d_vec3 z = slayer3d_vec3_make(0.0f, 0.0f, r);
    return emit_editor_debug_line(context, slayer3d_vec3_sub(center, x), slayer3d_vec3_add(center, x)) &&
           emit_editor_debug_line(context, slayer3d_vec3_sub(center, y), slayer3d_vec3_add(center, y)) &&
           emit_editor_debug_line(context, slayer3d_vec3_sub(center, z), slayer3d_vec3_add(center, z));
}

static bool emit_editor_debug_selected_vertex_outline(editor_debug_iteration_context *context, slayer3d_vec3 center)
{
    return emit_editor_debug_marker_orb(context, center, 0.16f) &&
           emit_editor_debug_marker_orb(context, center, 0.205f) &&
           emit_editor_debug_marker_cross(context, center, 0.23f);
}

static bool emit_editor_debug_hover_vertex_outline(editor_debug_iteration_context *context, slayer3d_vec3 center)
{
    return emit_editor_debug_marker_orb(context, center, 0.18f);
}

static bool emit_editor_debug_vertex_hover_label(editor_debug_iteration_context *context, slayer3d_vec3 center,
                                                 const editor_brush_source_vertex *vertex)
{
    if (context == NULL || vertex == NULL)
        return false;

    char label[64];
    SDL_snprintf(label, sizeof(label), "X %d  Y %d  Z %d", vertex->coord[0], vertex->coord[1], vertex->coord[2]);
    context->type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_HOVER_LABEL;
    context->color = (slayer3d_color){255, 245, 210, 255};
    const slayer3d_vec3 label_position = slayer3d_vec3_add(center, slayer3d_vec3_make(0.0f, 0.28f, 0.0f));
    return emit_editor_debug_label(context, label_position, label);
}

static bool emit_editor_debug_overlay_marker(const slayer3d_game_data_runtime *runtime, yyjson_val *marker,
                                             slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || !yyjson_is_obj(marker) || !eval_data_condition(runtime, obj_get(marker, "visible_if"), NULL))
        return true;

    const char *point_key = json_string(marker, "point_key", NULL);
    const slayer3d_value *point_value =
        point_key != NULL ? slayer3d_properties_get_value(runtime->scene_state, point_key) : NULL;
    if (point_value == NULL || point_value->type != SLAYER3D_VALUE_VEC3)
        return true;

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.color = editor_debug_color_or_default(json_color(marker, "color", (slayer3d_color){0, 0, 0, 0}),
                                                  (slayer3d_color){255, 60, 230, 255});
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_DIAGNOSTIC_MARKER;
    context.world_name = json_string(marker, "world", NULL);
    context.element_name = json_string(marker, "name", point_key);
    context.face_index = -1;
    return emit_editor_debug_marker_cross(&context, point_value->as_vec3, json_float(marker, "size", 0.45f));
}

static bool emit_editor_debug_overlay_markers(const slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_editor_debug_desc *desc,
                                              slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata)
{
    const unsigned int flags =
        desc != NULL && desc->flags != 0u ? desc->flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_DIAGNOSTIC_MARKERS) == 0u)
        return true;

    yyjson_val *overlay = obj_get(active_editor_tooling_root(runtime), "debug_overlay");
    yyjson_val *markers = obj_get(overlay, "markers");
    if (!yyjson_is_arr(markers))
        return true;

    for (size_t i = 0; i < yyjson_arr_size(markers); ++i)
    {
        if (!emit_editor_debug_overlay_marker(runtime, yyjson_arr_get(markers, i), callback, userdata))
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

static bool emit_editor_debug_player_start_marker(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_editor_debug_desc *desc,
                                                  slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || desc == NULL || callback == NULL)
        return false;

    const float radius = desc->player_start_radius > 0.0f ? desc->player_start_radius : 0.35f;
    const float height = desc->player_start_height > 0.0f ? desc->player_start_height : 1.8f;
    const int segments = 16;
    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.color = editor_debug_color_or_default(desc->player_start_color, (slayer3d_color){80, 255, 130, 255});
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_PLAYER_START_EDGE;
    context.world_name = "editor_player_starts";
    context.face_index = -1;

    for (int start_index = 0; start_index < runtime->editor_player_start_count; ++start_index)
    {
        const editor_player_start_runtime *start = &runtime->editor_player_starts[start_index];
        if (start->name == NULL || start->name[0] == '\0')
            continue;
        context.element_name = start->name;
        for (int i = 0; i < segments; ++i)
        {
            const float a0 = ((float)i / (float)segments) * SDL_PI_F * 2.0f;
            const float a1 = ((float)(i + 1) / (float)segments) * SDL_PI_F * 2.0f;
            const slayer3d_vec3 bottom0 =
                slayer3d_vec3_make(start->position.x + SDL_cosf(a0) * radius, start->position.y,
                                   start->position.z + SDL_sinf(a0) * radius);
            const slayer3d_vec3 bottom1 =
                slayer3d_vec3_make(start->position.x + SDL_cosf(a1) * radius, start->position.y,
                                   start->position.z + SDL_sinf(a1) * radius);
            const slayer3d_vec3 top0 = slayer3d_vec3_add(bottom0, slayer3d_vec3_make(0.0f, height, 0.0f));
            const slayer3d_vec3 top1 = slayer3d_vec3_add(bottom1, slayer3d_vec3_make(0.0f, height, 0.0f));
            if (!emit_editor_debug_line(&context, bottom0, bottom1) || !emit_editor_debug_line(&context, top0, top1))
                return true;
            if ((i % 4) == 0 && !emit_editor_debug_line(&context, bottom0, top0))
                return true;
        }
    }
    return true;
}

static bool editor_rotate_tool_selection_bounds(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_bounding_box *out_bounds)
{
    if (runtime == NULL || out_bounds == NULL || runtime->editor_selected_brush_count <= 0)
        return false;

    bool has_bounds = false;
    slayer3d_bounding_box bounds;
    SDL_zero(bounds);
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!selection.hit || !selection.has_bounds)
            continue;
        if (!has_bounds)
        {
            bounds = selection.bounds;
            has_bounds = true;
            continue;
        }
        bounds.min.x = SDL_min(bounds.min.x, selection.bounds.min.x);
        bounds.min.y = SDL_min(bounds.min.y, selection.bounds.min.y);
        bounds.min.z = SDL_min(bounds.min.z, selection.bounds.min.z);
        bounds.max.x = SDL_max(bounds.max.x, selection.bounds.max.x);
        bounds.max.y = SDL_max(bounds.max.y, selection.bounds.max.y);
        bounds.max.z = SDL_max(bounds.max.z, selection.bounds.max.z);
    }
    if (!has_bounds)
        return false;
    *out_bounds = bounds;
    return true;
}

static slayer3d_vec3 editor_rotate_tool_pivot(const slayer3d_game_data_runtime *runtime, slayer3d_bounding_box bounds)
{
    if (runtime != NULL && runtime->editor_drag_move.active && runtime->editor_drag_move.rotate_drag)
        return runtime->editor_drag_move.rotate_pivot;
    const slayer3d_value *pivot_value = runtime != NULL && runtime->scene_state != NULL
                                            ? slayer3d_properties_get_value(runtime->scene_state, "editor.rotate.pivot")
                                            : NULL;
    if (pivot_value != NULL && pivot_value->type == SLAYER3D_VALUE_VEC3)
        return pivot_value->as_vec3;
    return slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
}

static bool emit_editor_debug_rotate_ring(editor_debug_iteration_context *context, slayer3d_vec3 center, float radius,
                                          slayer3d_vec3 axis_a, slayer3d_vec3 axis_b,
                                          slayer3d_game_data_editor_debug_primitive_type type, float start_angle,
                                          float end_angle, int segments)
{
    if (context == NULL || radius <= 0.0f || segments <= 0)
        return false;
    const float angle_range = end_angle - start_angle;
    slayer3d_vec3 previous =
        slayer3d_vec3_add(center, slayer3d_vec3_add(slayer3d_vec3_scale(axis_a, SDL_cosf(start_angle) * radius),
                                                    slayer3d_vec3_scale(axis_b, SDL_sinf(start_angle) * radius)));
    const slayer3d_game_data_editor_debug_primitive_type old_type = context->type;
    context->type = type;
    for (int segment = 1; segment <= segments; ++segment)
    {
        const float angle = start_angle + angle_range * (float)segment / (float)segments;
        const slayer3d_vec3 current =
            slayer3d_vec3_add(center, slayer3d_vec3_add(slayer3d_vec3_scale(axis_a, SDL_cosf(angle) * radius),
                                                        slayer3d_vec3_scale(axis_b, SDL_sinf(angle) * radius)));
        if (!emit_editor_debug_line(context, previous, current))
        {
            context->type = old_type;
            return false;
        }
        previous = current;
    }
    context->type = old_type;
    return true;
}

static void editor_debug_rotate_axis_basis(slayer3d_vec3 axis, slayer3d_vec3 *out_a, slayer3d_vec3 *out_b)
{
    const float abs_x = SDL_fabsf(axis.x);
    const float abs_y = SDL_fabsf(axis.y);
    const float abs_z = SDL_fabsf(axis.z);
    if (abs_x >= abs_y && abs_x >= abs_z)
    {
        *out_a = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
        *out_b = slayer3d_vec3_make(0.0f, 0.0f, 1.0f);
    }
    else if (abs_y >= abs_z)
    {
        *out_a = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        *out_b = slayer3d_vec3_make(0.0f, 0.0f, 1.0f);
    }
    else
    {
        *out_a = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        *out_b = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    }
}

static bool editor_debug_rotate_axis_matches(slayer3d_vec3 a, slayer3d_vec3 b)
{
    if (slayer3d_vec3_length_squared(a) <= 0.000001f || slayer3d_vec3_length_squared(b) <= 0.000001f)
        return false;
    a = slayer3d_vec3_normalize(a);
    b = slayer3d_vec3_normalize(b);
    return SDL_fabsf(a.x - b.x) <= 0.001f && SDL_fabsf(a.y - b.y) <= 0.001f && SDL_fabsf(a.z - b.z) <= 0.001f;
}

static slayer3d_color editor_debug_rotate_ring_color(slayer3d_vec3 ring_axis, slayer3d_vec3 hover_axis,
                                                     slayer3d_vec3 active_axis, bool hovered, bool dragging,
                                                     slayer3d_color base)
{
    if (dragging && editor_debug_rotate_axis_matches(ring_axis, active_axis))
        return (slayer3d_color){255, 245, 80, 255};
    if (hovered && editor_debug_rotate_axis_matches(ring_axis, hover_axis))
        return (slayer3d_color){255, 255, 255, 255};
    return base;
}

static slayer3d_vec3 editor_debug_rotate_point(slayer3d_vec3 point, slayer3d_vec3 pivot, slayer3d_vec3 axis,
                                               float angle_radians)
{
    axis = slayer3d_vec3_normalize(axis);
    const float cos_angle = SDL_cosf(angle_radians);
    const float sin_angle = SDL_sinf(angle_radians);
    const slayer3d_vec3 relative = slayer3d_vec3_sub(point, pivot);
    const slayer3d_vec3 rotated =
        slayer3d_vec3_add(slayer3d_vec3_add(slayer3d_vec3_scale(relative, cos_angle),
                                            slayer3d_vec3_scale(slayer3d_vec3_cross(axis, relative), sin_angle)),
                          slayer3d_vec3_scale(axis, slayer3d_vec3_dot(axis, relative) * (1.0f - cos_angle)));
    return slayer3d_vec3_add(pivot, rotated);
}

static bool emit_editor_debug_rotate_preview_edges(editor_debug_iteration_context *context,
                                                   const slayer3d_game_data_runtime *runtime, slayer3d_vec3 pivot,
                                                   slayer3d_vec3 axis, float angle_radians)
{
    if (context == NULL || runtime == NULL || SDL_fabsf(angle_radians) <= 0.000001f ||
        slayer3d_vec3_length_squared(axis) <= 0.000001f)
    {
        return true;
    }

    const slayer3d_game_data_editor_debug_primitive_type old_type = context->type;
    const slayer3d_color old_color = context->color;
    context->type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_PREVIEW_EDGE;
    context->color = (slayer3d_color){255, 245, 80, 245};
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        const brush_world_runtime *world = find_brush_world_runtime(runtime, selection.world_name);
        const int source_index = editor_debug_source_index_for_selection(world, &selection);
        editor_brush_source_vertex_model model;
        if (source_index < 0 || !editor_brush_source_box_build_vertex_model(world, source_index, &model, NULL, 0))
            continue;

        context->world_name = selection.world_name;
        context->element_name = selection.element_name;
        context->face_index = -1;
        for (int edge = 0; edge < model.edge_count; ++edge)
        {
            const editor_brush_source_edge *source_edge = &model.edges[edge];
            const int a = source_edge->vertex_indices[0];
            const int b = source_edge->vertex_indices[1];
            if (a < 0 || a >= model.vertex_count || b < 0 || b >= model.vertex_count)
                continue;
            const slayer3d_vec3 start = slayer3d_vec3_add(selection.world_position,
                                                          editor_source_vertex_coord_meters(world, &model.vertices[a]));
            const slayer3d_vec3 end = slayer3d_vec3_add(selection.world_position,
                                                        editor_source_vertex_coord_meters(world, &model.vertices[b]));
            if (!emit_editor_debug_line(context, editor_debug_rotate_point(start, pivot, axis, angle_radians),
                                        editor_debug_rotate_point(end, pivot, axis, angle_radians)))
            {
                context->type = old_type;
                context->color = old_color;
                return false;
            }
        }
    }
    context->type = old_type;
    context->color = old_color;
    return true;
}

static bool emit_editor_debug_rotate_tool(const slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_editor_debug_desc *desc,
                                          slayer3d_game_data_editor_debug_primitive_fn callback, void *userdata)
{
    const unsigned int flags =
        desc != NULL && desc->flags != 0u ? desc->flags : SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    if (runtime == NULL || runtime->scene_state == NULL || callback == NULL ||
        (flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ROTATE_HANDLES) == 0u || !editor_mode_is_rotate(runtime))
    {
        return true;
    }

    slayer3d_bounding_box bounds;
    if (!editor_rotate_tool_selection_bounds(runtime, &bounds))
        return true;
    const slayer3d_vec3 pivot = editor_rotate_tool_pivot(runtime, bounds);
    const slayer3d_vec3 size = slayer3d_vec3_sub(bounds.max, bounds.min);
    const float radius = SDL_max(SDL_max(size.x, SDL_max(size.y, size.z)) * 0.75f, 0.75f);
    const bool dragging = runtime->editor_drag_move.active && runtime->editor_drag_move.rotate_drag;
    const bool hovered = scene_state_bool(runtime, "editor.rotate.hovered", false);
    const slayer3d_vec3 hover_axis = slayer3d_properties_get_vec3(runtime->scene_state, "editor.rotate.hover_axis",
                                                                  slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    const slayer3d_vec3 active_axis = dragging
                                          ? runtime->editor_drag_move.rotate_axis
                                          : slayer3d_properties_get_vec3(runtime->scene_state, "editor.rotate.axis",
                                                                         slayer3d_vec3_make(0.0f, 1.0f, 0.0f));

    editor_debug_iteration_context context;
    SDL_zero(context);
    context.callback = callback;
    context.userdata = userdata;
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_PIVOT;
    context.face_index = -1;
    context.color = (slayer3d_color){255, 230, 80, 255};
    if (!emit_editor_debug_marker_cross(&context, pivot, 0.45f))
        return false;

    const int segments = 48;
    context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_RING;
    context.color = editor_debug_rotate_ring_color(slayer3d_vec3_make(1.0f, 0.0f, 0.0f), hover_axis, active_axis,
                                                   hovered, dragging, (slayer3d_color){180, 50, 40, 210});
    if (!emit_editor_debug_rotate_ring(&context, pivot, radius, slayer3d_vec3_make(0.0f, 1.0f, 0.0f),
                                       slayer3d_vec3_make(0.0f, 0.0f, 1.0f),
                                       SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_RING, 0.0f, SDL_PI_F * 2.0f, segments))
        return false;
    context.color = editor_debug_rotate_ring_color(slayer3d_vec3_make(0.0f, 1.0f, 0.0f), hover_axis, active_axis,
                                                   hovered, dragging, (slayer3d_color){50, 165, 75, 210});
    if (!emit_editor_debug_rotate_ring(&context, pivot, radius, slayer3d_vec3_make(1.0f, 0.0f, 0.0f),
                                       slayer3d_vec3_make(0.0f, 0.0f, 1.0f),
                                       SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_RING, 0.0f, SDL_PI_F * 2.0f, segments))
        return false;
    context.color = editor_debug_rotate_ring_color(slayer3d_vec3_make(0.0f, 0.0f, 1.0f), hover_axis, active_axis,
                                                   hovered, dragging, (slayer3d_color){55, 110, 190, 210});
    if (!emit_editor_debug_rotate_ring(&context, pivot, radius, slayer3d_vec3_make(1.0f, 0.0f, 0.0f),
                                       slayer3d_vec3_make(0.0f, 1.0f, 0.0f),
                                       SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_RING, 0.0f, SDL_PI_F * 2.0f, segments))
        return false;

    if (runtime->editor_drag_move.active && runtime->editor_drag_move.rotate_drag &&
        SDL_fabsf(runtime->editor_drag_move.rotate_angle_radians) > 0.000001f)
    {
        slayer3d_vec3 arc_a;
        slayer3d_vec3 arc_b;
        editor_debug_rotate_axis_basis(runtime->editor_drag_move.rotate_axis, &arc_a, &arc_b);
        context.color = (slayer3d_color){255, 245, 110, 255};
        if (!emit_editor_debug_rotate_ring(&context, pivot, radius * 1.08f, arc_a, arc_b,
                                           SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_ARC, 0.0f,
                                           runtime->editor_drag_move.rotate_angle_radians, 24))
            return false;
        if (!emit_editor_debug_rotate_preview_edges(&context, runtime, pivot, runtime->editor_drag_move.rotate_axis,
                                                    runtime->editor_drag_move.rotate_angle_radians))
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

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_PLAYER_STARTS) != 0u &&
        !emit_editor_debug_player_start_marker(runtime, desc, callback, userdata))
    {
        return true;
    }
    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ROTATE_HANDLES) != 0u &&
        !emit_editor_debug_rotate_tool(runtime, desc, callback, userdata))
    {
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
        context.color = editor_debug_selection_flash_color(desc->selection_bounds_color);
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_SELECTION_BOUNDS_EDGE;
        context.world_name = selection->world_name;
        context.element_name = selection->element_name;
        context.face_index = selection->face_index;
        const brush_world_runtime *world = selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD
                                               ? find_brush_world_runtime(runtime, selection->world_name)
                                               : NULL;
        if (!emit_editor_debug_source_brush_edges(&context, world, selection) &&
            !emit_editor_debug_bounds(&context, selection->bounds))
        {
            return true;
        }
    }

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_FACE) != 0u &&
        !emit_editor_debug_selection_face(desc, selection, callback, userdata))
    {
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

    if ((flags & SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_CLIP_PREVIEW) != 0u &&
        !emit_editor_clip_tool_primitives(runtime, desc, callback, userdata))
    {
        return true;
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
        const slayer3d_color color = editor_debug_selection_flash_color(
            editor_debug_color_or_default(desc->command_preview_color, (slayer3d_color){80, 255, 255, 220}));
        editor_debug_iteration_context context;
        SDL_zero(context);
        context.callback = callback;
        context.userdata = userdata;
        context.color = (slayer3d_color){color.r, color.g, color.b, (Uint8)SDL_min((int)color.a, 170)};
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_COMMAND_PREVIEW_BOUNDS_EDGE;
        context.world_name = preview->world_name;
        context.element_name = preview->mode;
        context.face_index = -1;
        if (!emit_editor_debug_bounds(&context, preview->bounds))
            return true;
        context.color = (slayer3d_color){255, 220, 80, 230};
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_PLACEMENT_PREVIEW_FOOTPRINT_EDGE;
        if (!emit_editor_debug_placement_footprint(&context, runtime, preview))
            return true;
        context.color = (slayer3d_color){255, 145, 40, 245};
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_PLACEMENT_PREVIEW_AXIS;
        if (!emit_editor_debug_placement_axis(&context, runtime, preview))
            return true;
        context.color = color;
        context.type = SLAYER3D_GAME_DATA_EDITOR_DEBUG_COMMAND_PREVIEW_BOUNDS_EDGE;
        if (!emit_editor_debug_bounds(&context, preview->bounds))
            return true;
    }
    return true;
}

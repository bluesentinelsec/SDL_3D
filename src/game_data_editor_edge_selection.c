/**
 * @file game_data_editor_edge_selection.c
 * @brief Editor edge-tool hover and selection handling.
 */

#include "game_data_editor_vertex_internal.h"

#include <float.h>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/math.h"

static bool editor_source_edge_selection_matches(const editor_source_edge_selection *a,
                                                 const editor_source_edge_selection *b)
{
    return a != NULL && b != NULL && a->source_index == b->source_index && a->edge_index == b->edge_index &&
           SDL_strcmp(a->world_name, b->world_name) == 0 && SDL_strcmp(a->brush_stable_id, b->brush_stable_id) == 0 &&
           SDL_strcmp(a->edge_stable_id, b->edge_stable_id) == 0;
}

static bool editor_source_edge_selection_from_model(const brush_world_runtime *world_runtime,
                                                    const editor_brush_source_vertex_model *model, int edge_index,
                                                    editor_source_edge_selection *out_selection)
{
    if (world_runtime == NULL || model == NULL || out_selection == NULL || edge_index < 0 ||
        edge_index >= model->edge_count)
    {
        return false;
    }
    const editor_brush_source_edge *edge = &model->edges[edge_index];
    const int a = edge->vertex_indices[0];
    const int b = edge->vertex_indices[1];
    if (a < 0 || a >= model->vertex_count || b < 0 || b >= model->vertex_count)
        return false;

    init_editor_source_edge_selection(out_selection);
    const editor_brush_source_box_runtime *box = NULL;
    if (model->brush_index >= 0 && model->brush_index < world_runtime->editor_source_box_count)
        box = &world_runtime->editor_source_boxes[model->brush_index];
    SDL_strlcpy(out_selection->world_name, world_runtime->desc.name != NULL ? world_runtime->desc.name : "",
                sizeof(out_selection->world_name));
    SDL_strlcpy(out_selection->brush_name, box != NULL && box->name != NULL ? box->name : model->brush_stable_id,
                sizeof(out_selection->brush_name));
    SDL_strlcpy(out_selection->brush_stable_id, model->brush_stable_id, sizeof(out_selection->brush_stable_id));
    SDL_strlcpy(out_selection->edge_stable_id, edge->stable_id, sizeof(out_selection->edge_stable_id));
    out_selection->source_index = model->brush_index;
    out_selection->edge_index = edge->edge_index;
    for (int axis = 0; axis < 3; ++axis)
    {
        out_selection->coord[0][axis] = model->vertices[a].coord[axis];
        out_selection->coord[1][axis] = model->vertices[b].coord[axis];
    }
    return true;
}

int editor_selected_edge_index(const slayer3d_game_data_runtime *runtime, const editor_source_edge_selection *selection)
{
    if (runtime == NULL || selection == NULL || !editor_selected_edges_active_for_scene(runtime))
        return -1;
    for (int i = 0; i < runtime->editor_selected_edge_count; ++i)
    {
        if (editor_source_edge_selection_matches(&runtime->editor_selected_edges[i], selection))
            return i;
    }
    return -1;
}

static void remove_editor_selected_edge_at(slayer3d_game_data_runtime *runtime, int index)
{
    if (runtime == NULL || index < 0 || index >= runtime->editor_selected_edge_count)
        return;
    for (int i = index; i + 1 < runtime->editor_selected_edge_count; ++i)
        runtime->editor_selected_edges[i] = runtime->editor_selected_edges[i + 1];
    runtime->editor_selected_edge_count--;
    init_editor_source_edge_selection(&runtime->editor_selected_edges[runtime->editor_selected_edge_count]);
}

static bool add_editor_selected_edge(slayer3d_game_data_runtime *runtime, const editor_source_edge_selection *selection)
{
    if (runtime == NULL || selection == NULL ||
        runtime->editor_selected_edge_count >= SLAYER3D_EDITOR_SELECTED_EDGE_CAPACITY)
        return false;
    if (!editor_selected_edges_active_for_scene(runtime))
        clear_editor_selected_edges(runtime);
    if (editor_selected_edge_index(runtime, selection) >= 0)
        return true;
    runtime->editor_selected_edges[runtime->editor_selected_edge_count++] = *selection;
    runtime->editor_selected_edge_scene = slayer3d_game_data_active_scene(runtime);
    return true;
}

static bool toggle_editor_selected_edge(slayer3d_game_data_runtime *runtime,
                                        const editor_source_edge_selection *selection)
{
    if (runtime == NULL || selection == NULL)
        return false;
    const int selected_index = editor_selected_edge_index(runtime, selection);
    if (selected_index >= 0)
    {
        remove_editor_selected_edge_at(runtime, selected_index);
        return true;
    }
    return add_editor_selected_edge(runtime, selection);
}

static float editor_edge_handle_pick_radius(const slayer3d_game_data_runtime *runtime)
{
    return editor_vertex_handle_pick_radius(runtime);
}

static float editor_point_segment_distance_sq(slayer3d_vec3 point, slayer3d_vec3 start, slayer3d_vec3 end)
{
    const slayer3d_vec3 segment = slayer3d_vec3_sub(end, start);
    const float length_sq = slayer3d_vec3_length_squared(segment);
    if (length_sq <= 0.0000001f)
        return slayer3d_vec3_length_squared(slayer3d_vec3_sub(point, start));
    const float t = SDL_clamp(slayer3d_vec3_dot(slayer3d_vec3_sub(point, start), segment) / length_sq, 0.0f, 1.0f);
    const slayer3d_vec3 closest = slayer3d_vec3_add(start, slayer3d_vec3_scale(segment, t));
    return slayer3d_vec3_length_squared(slayer3d_vec3_sub(point, closest));
}

static bool editor_hover_source_edge_selection_with_distance(const slayer3d_game_data_runtime *runtime,
                                                             const slayer3d_game_data_editor_selection *hover_selection,
                                                             editor_source_edge_selection *out_selection,
                                                             float *out_distance_sq)
{
    if (out_selection != NULL)
        init_editor_source_edge_selection(out_selection);
    if (out_distance_sq != NULL)
        *out_distance_sq = FLT_MAX;
    if (runtime == NULL || out_selection == NULL || !editor_mode_is_edge(runtime) ||
        !editor_selection_is_selectable_brush(hover_selection))
    {
        return false;
    }

    slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, hover_selection);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, resolved.world_name);
    const int source_index = editor_source_index_for_selection(world_runtime, &resolved);
    editor_brush_source_vertex_model model;
    if (world_runtime == NULL || source_index < 0 ||
        !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
    {
        return false;
    }

    const slayer3d_vec3 local_point = slayer3d_vec3_sub(resolved.point, resolved.world_position);
    int nearest_edge = -1;
    float nearest_distance_sq = FLT_MAX;
    for (int i = 0; i < model.edge_count; ++i)
    {
        const editor_brush_source_edge *edge = &model.edges[i];
        const int a = edge->vertex_indices[0];
        const int b = edge->vertex_indices[1];
        if (a < 0 || a >= model.vertex_count || b < 0 || b >= model.vertex_count)
            continue;
        const slayer3d_vec3 start = editor_source_vertex_meters(world_runtime, &model.vertices[a]);
        const slayer3d_vec3 end = editor_source_vertex_meters(world_runtime, &model.vertices[b]);
        const float distance_sq = editor_point_segment_distance_sq(local_point, start, end);
        if (distance_sq < nearest_distance_sq)
        {
            nearest_distance_sq = distance_sq;
            nearest_edge = i;
        }
    }
    if (out_distance_sq != NULL)
        *out_distance_sq = nearest_distance_sq;
    return editor_source_edge_selection_from_model(world_runtime, &model, nearest_edge, out_selection);
}

void publish_editor_edge_hover_state(slayer3d_game_data_runtime *runtime,
                                     const slayer3d_game_data_editor_selection *hover_selection)
{
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_edge(runtime))
    {
        clear_editor_edge_hover_state(runtime);
        return;
    }

    editor_source_edge_selection hovered;
    float nearest_distance_sq = FLT_MAX;
    if (!editor_hover_source_edge_selection_with_distance(runtime, hover_selection, &hovered, &nearest_distance_sq) ||
        nearest_distance_sq > editor_edge_handle_pick_radius(runtime) * editor_edge_handle_pick_radius(runtime))
    {
        clear_editor_edge_hover_state(runtime);
        return;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, hovered.world_name);
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.hover.hit", true);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.hover.brush", hovered.brush_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.hover.brush_stable_id", hovered.brush_stable_id);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.hover.edge", hovered.edge_stable_id);
    slayer3d_properties_set_int(runtime->scene_state, "editor.edge.hover.index", hovered.edge_index);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.hover.selected",
                                 editor_selected_edge_index(runtime, &hovered) >= 0);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.hover.start",
                                 slayer3d_vec3_make((float)hovered.coord[0][0] * meters_per_unit,
                                                    (float)hovered.coord[0][1] * meters_per_unit,
                                                    (float)hovered.coord[0][2] * meters_per_unit));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.hover.end",
                                 slayer3d_vec3_make((float)hovered.coord[1][0] * meters_per_unit,
                                                    (float)hovered.coord[1][1] * meters_per_unit,
                                                    (float)hovered.coord[1][2] * meters_per_unit));
}

static bool editor_edge_toggle_modifier(SDL_Keymod modifiers)
{
    return (modifiers & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

static void editor_clear_edge_selection_from_click(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    if (runtime->editor_selected_edge_count > 0)
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "edge selection cleared");
    clear_editor_selected_edges(runtime);
    publish_editor_selected_edge_count(runtime);
}

bool editor_handle_edge_selection(slayer3d_game_data_runtime *runtime,
                                  const slayer3d_game_data_editor_selection *hover_selection, bool select_requested,
                                  bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || !editor_mode_is_edge(runtime) || !select_requested)
        return true;

    if (editor_selection_is_selectable_brush(hover_selection))
    {
        const slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, hover_selection);
        if (editor_selected_brush_index(runtime, &resolved) < 0)
        {
            if (!editor_select_mode_primary_click(runtime, &resolved))
                return false;
            editor_clear_edge_selection_from_click(runtime);
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "brush selected");
            if (out_consumed != NULL)
                *out_consumed = true;
            return true;
        }
    }

    editor_source_edge_selection hovered;
    float nearest_distance_sq = FLT_MAX;
    if (!editor_hover_source_edge_selection_with_distance(runtime, hover_selection, &hovered, &nearest_distance_sq) ||
        nearest_distance_sq > editor_edge_handle_pick_radius(runtime) * editor_edge_handle_pick_radius(runtime))
    {
        editor_clear_edge_selection_from_click(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    const bool additive_toggle = editor_edge_toggle_modifier(SDL_GetModState());
    const int selected_index = editor_selected_edge_index(runtime, &hovered);
    if (selected_index >= 0)
    {
        if (additive_toggle)
        {
            remove_editor_selected_edge_at(runtime, selected_index);
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "edge deselected");
        }
        else
        {
            clear_editor_selected_edges(runtime);
            if (!add_editor_selected_edge(runtime, &hovered))
                return false;
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "edge selected");
        }
    }
    else
    {
        if (!additive_toggle)
            clear_editor_selected_edges(runtime);
        if (!add_editor_selected_edge(runtime, &hovered))
            return false;
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                       additive_toggle ? "edge added to selection" : "edge selected");
    }

    publish_editor_selected_edge_count(runtime);
    publish_editor_edge_hover_state(runtime, hover_selection);
    if (out_consumed != NULL)
        *out_consumed = true;
    return true;
}

static int editor_select_edges_in_lasso(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                        const editor_drag_move_state *drag)
{
    if (runtime == NULL || selection_json == NULL || drag == NULL || !editor_selected_brushes_active_for_scene(runtime))
        return 0;

    yyjson_val *trace = obj_get(selection_json, "trace");
    editor_trace_viewport_config viewport;
    if (!editor_trace_select_viewport_at(runtime, trace, drag->start_mouse_x, drag->start_mouse_y, &viewport))
        return runtime->editor_selected_edge_count;

    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, viewport.camera, &camera))
        return runtime->editor_selected_edge_count;

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection.world_name);
        const int source_index = editor_source_index_for_selection(world_runtime, &selection);
        editor_brush_source_vertex_model model;
        if (world_runtime == NULL || source_index < 0 ||
            !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
        {
            continue;
        }

        for (int edge_index = 0; edge_index < model.edge_count; ++edge_index)
        {
            const editor_brush_source_edge *edge = &model.edges[edge_index];
            const int a = edge->vertex_indices[0];
            const int b = edge->vertex_indices[1];
            if (a < 0 || a >= model.vertex_count || b < 0 || b >= model.vertex_count)
                continue;

            const slayer3d_vec3 start = editor_source_vertex_meters(world_runtime, &model.vertices[a]);
            const slayer3d_vec3 end = editor_source_vertex_meters(world_runtime, &model.vertices[b]);
            const slayer3d_vec3 local_midpoint = slayer3d_vec3_scale(slayer3d_vec3_add(start, end), 0.5f);
            const slayer3d_vec3 world_midpoint = slayer3d_vec3_add(selection.world_position, local_midpoint);
            float screen_x = 0.0f;
            float screen_y = 0.0f;
            if (!editor_project_world_to_viewport(&camera, &viewport, world_midpoint, &screen_x, &screen_y) ||
                !editor_lasso_contains_screen_point(drag, &viewport, screen_x, screen_y))
            {
                continue;
            }

            editor_source_edge_selection candidate;
            if (!editor_source_edge_selection_from_model(world_runtime, &model, edge_index, &candidate))
                continue;
            if (drag->lasso_additive)
            {
                if (!add_editor_selected_edge(runtime, &candidate))
                    return runtime->editor_selected_edge_count;
            }
            else if (!toggle_editor_selected_edge(runtime, &candidate))
            {
                return runtime->editor_selected_edge_count;
            }
        }
    }

    publish_editor_selected_edge_count(runtime);
    return runtime->editor_selected_edge_count;
}

bool editor_handle_edge_lasso(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                              const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL)
        return true;

    editor_drag_move_state *drag = &runtime->editor_drag_move;
    if (!editor_mode_is_edge(runtime))
    {
        if (drag->active && drag->edge_lasso)
            clear_editor_drag_move(runtime);
        publish_editor_edge_lasso_state(runtime, NULL, 0);
        return true;
    }

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return true;

    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    float mouse_x = drag->current_mouse_x;
    float mouse_y = drag->current_mouse_y;
    (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);

    if (drag->active && drag->edge_lasso)
    {
        drag->current_mouse_x = mouse_x;
        drag->current_mouse_y = mouse_y;
        publish_editor_edge_lasso_state(runtime, drag, runtime->editor_selected_edge_count);
        if (out_consumed != NULL)
            *out_consumed = true;
        if (left_released || !left_down)
        {
            const float dx = drag->current_mouse_x - drag->start_mouse_x;
            const float dy = drag->current_mouse_y - drag->start_mouse_y;
            const bool meaningful_drag = dx * dx + dy * dy >= 16.0f;
            const int selected_count =
                meaningful_drag ? editor_select_edges_in_lasso(runtime, selection_json, drag) : 0;
            if (!meaningful_drag)
                editor_clear_edge_selection_from_click(runtime);
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                           meaningful_drag ? "edge lasso selected" : "edge selection cleared");
            publish_editor_selected_edge_count(runtime);
            publish_editor_edge_lasso_state(runtime, NULL,
                                            meaningful_drag ? selected_count : runtime->editor_selected_edge_count);
            clear_editor_drag_move(runtime);
        }
        return true;
    }

    const bool selectable_hover = editor_selection_is_selectable_brush(hover_selection);
    if (left_pressed && !selectable_hover)
    {
        SDL_zero(*drag);
        drag->active = true;
        drag->edge_lasso = true;
        drag->lasso_additive = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        drag->start_mouse_x = mouse_x;
        drag->start_mouse_y = mouse_y;
        drag->current_mouse_x = mouse_x;
        drag->current_mouse_y = mouse_y;
        publish_editor_edge_lasso_state(runtime, drag, runtime->editor_selected_edge_count);
        if (out_consumed != NULL)
            *out_consumed = true;
    }
    return true;
}

bool slayer3d_game_data_clear_editor_edge_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    clear_editor_selected_edges(runtime);
    publish_editor_selected_edge_count(runtime);
    return true;
}

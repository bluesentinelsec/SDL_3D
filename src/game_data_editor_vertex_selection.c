/**
 * @file game_data_editor_vertex_selection.c
 * @brief Editor vertex-mode hover, selection, and lasso handling.
 */

#include "game_data_editor_vertex_internal.h"

#include <float.h>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/camera.h"
#include "slayer3d/math.h"

void publish_editor_vertex_hover_state(slayer3d_game_data_runtime *runtime,
                                       const slayer3d_game_data_editor_selection *hover_selection)
{
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_vertex(runtime) ||
        !editor_selection_is_selectable_brush(hover_selection))
    {
        clear_editor_vertex_hover_state(runtime);
        return;
    }

    slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, hover_selection);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, resolved.world_name);
    const int source_index = editor_source_index_for_selection(world_runtime, &resolved);
    editor_brush_source_vertex_model model;
    if (world_runtime == NULL || source_index < 0 ||
        !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
    {
        clear_editor_vertex_hover_state(runtime);
        return;
    }

    const slayer3d_vec3 local_point = slayer3d_vec3_sub(resolved.point, resolved.world_position);
    int nearest_vertex = -1;
    float nearest_distance_sq = FLT_MAX;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        const slayer3d_vec3 vertex = editor_source_vertex_meters(world_runtime, &model.vertices[i]);
        const float distance_sq = slayer3d_vec3_length_squared(slayer3d_vec3_sub(vertex, local_point));
        if (distance_sq < nearest_distance_sq)
        {
            nearest_distance_sq = distance_sq;
            nearest_vertex = i;
        }
    }
    if (nearest_vertex < 0)
    {
        clear_editor_vertex_hover_state(runtime);
        return;
    }

    int selected_indices[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
    const int selected_count = editor_collect_selected_source_indices(
        runtime, world_runtime, source_index, selected_indices, SDL_arraysize(selected_indices));
    const editor_brush_source_vertex *vertex = &model.vertices[nearest_vertex];
    const int shared_count =
        editor_source_vertex_shared_count(world_runtime, vertex->coord, selected_indices, selected_count);
    const slayer3d_vec3 coord_meters = editor_source_vertex_meters(world_runtime, vertex);

    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.hover.hit", true);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.brush", resolved.element_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.brush_stable_id",
                                   editor_metadata_stable_id(resolved.element_editor));
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.vertex", vertex->stable_id);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.index", vertex->vertex_index);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.shared_count", shared_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.x", vertex->coord[0]);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.y", vertex->coord[1]);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.z", vertex->coord[2]);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.hover.coord", coord_meters);
    editor_source_vertex_selection hovered;
    if (editor_source_vertex_selection_from_model(world_runtime, &model, nearest_vertex, &hovered))
    {
        slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.hover.selected",
                                     editor_selected_vertex_index(runtime, &hovered) >= 0);
    }
}

bool editor_hover_source_vertex_selection_with_distance(const slayer3d_game_data_runtime *runtime,
                                                        const slayer3d_game_data_editor_selection *hover_selection,
                                                        editor_source_vertex_selection *out_selection,
                                                        float *out_distance_sq)
{
    if (out_selection != NULL)
        init_editor_source_vertex_selection(out_selection);
    if (out_distance_sq != NULL)
        *out_distance_sq = FLT_MAX;
    if (runtime == NULL || out_selection == NULL || !editor_mode_is_vertex(runtime) ||
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
    int nearest_vertex = -1;
    float nearest_distance_sq = FLT_MAX;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        const slayer3d_vec3 vertex = editor_source_vertex_meters(world_runtime, &model.vertices[i]);
        const float distance_sq = slayer3d_vec3_length_squared(slayer3d_vec3_sub(vertex, local_point));
        if (distance_sq < nearest_distance_sq)
        {
            nearest_distance_sq = distance_sq;
            nearest_vertex = i;
        }
    }
    if (out_distance_sq != NULL)
        *out_distance_sq = nearest_distance_sq;
    return editor_source_vertex_selection_from_model(world_runtime, &model, nearest_vertex, out_selection);
}

static bool editor_source_brush_is_selected(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || !editor_selected_brushes_active_for_scene(runtime) ||
        !editor_selection_is_selectable_brush(selection))
    {
        return false;
    }

    slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, selection);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, resolved.world_name);
    const int source_index = editor_source_index_for_selection(world_runtime, &resolved);
    if (world_runtime == NULL || source_index < 0)
        return false;

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selected =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (selected.world_name == NULL || SDL_strcmp(selected.world_name, resolved.world_name) != 0)
            continue;
        if (editor_source_index_for_selection(world_runtime, &selected) == source_index)
            return true;
    }
    return false;
}

bool editor_hover_source_vertex_selection(const slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_editor_selection *hover_selection,
                                          editor_source_vertex_selection *out_selection)
{
    return editor_hover_source_vertex_selection_with_distance(runtime, hover_selection, out_selection, NULL);
}

static bool editor_vertex_toggle_modifier(SDL_Keymod modifiers)
{
    return (modifiers & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

static void editor_clear_vertex_selection_from_click(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    if (runtime->editor_selected_vertex_count > 0)
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "vertex selection cleared");
    clear_editor_selected_vertices(runtime);
    publish_editor_selected_vertex_count(runtime);
}

static void editor_copy_vertex_selection_to_drag_origin(editor_drag_vertex_origin *origin,
                                                        const editor_source_vertex_selection *selection)
{
    if (origin == NULL || selection == NULL)
        return;
    SDL_zero(*origin);
    SDL_strlcpy(origin->world_name, selection->world_name, sizeof(origin->world_name));
    SDL_strlcpy(origin->brush_name, selection->brush_name, sizeof(origin->brush_name));
    SDL_strlcpy(origin->brush_stable_id, selection->brush_stable_id, sizeof(origin->brush_stable_id));
    SDL_strlcpy(origin->vertex_stable_id, selection->vertex_stable_id, sizeof(origin->vertex_stable_id));
    origin->source_index = selection->source_index;
    origin->vertex_index = selection->vertex_index;
    origin->coord[0] = selection->coord[0];
    origin->coord[1] = selection->coord[1];
    origin->coord[2] = selection->coord[2];
}

static void editor_mark_vertex_drag_toggle_on_click(slayer3d_game_data_runtime *runtime,
                                                    const editor_source_vertex_selection *selection)
{
    if (runtime == NULL || selection == NULL || !runtime->editor_drag_move.active ||
        !runtime->editor_drag_move.vertex_drag)
    {
        return;
    }
    runtime->editor_drag_move.vertex_toggle_on_click = true;
    editor_copy_vertex_selection_to_drag_origin(&runtime->editor_drag_move.vertex_toggle_origin, selection);
}

bool editor_handle_vertex_selection(slayer3d_game_data_runtime *runtime,
                                    const slayer3d_game_data_editor_selection *hover_selection, bool select_requested,
                                    bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || !editor_mode_is_vertex(runtime) || !select_requested)
        return true;

    if (editor_selection_is_selectable_brush(hover_selection))
    {
        const slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, hover_selection);
        if (editor_selected_brush_index(runtime, &resolved) < 0 && !editor_source_brush_is_selected(runtime, &resolved))
        {
            if (!editor_select_mode_primary_click(runtime, &resolved))
                return false;
            editor_clear_vertex_selection_from_click(runtime);
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "brush selected");
            if (out_consumed != NULL)
                *out_consumed = true;
            return true;
        }
    }

    editor_source_vertex_selection hovered;
    float nearest_distance_sq = FLT_MAX;
    if (!editor_hover_source_vertex_selection_with_distance(runtime, hover_selection, &hovered, &nearest_distance_sq))
    {
        editor_clear_vertex_selection_from_click(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }
    const float handle_radius = editor_vertex_handle_pick_radius(runtime);
    if (nearest_distance_sq > handle_radius * handle_radius)
    {
        editor_clear_vertex_selection_from_click(runtime);
        if (editor_selection_is_selectable_brush(hover_selection))
        {
            runtime->editor_active_selection = resolved_editor_selection(runtime, hover_selection);
            runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "brush selected");
        }
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    const SDL_Keymod modifiers = SDL_GetModState();
    const bool additive_toggle = editor_vertex_toggle_modifier(modifiers);
    const bool quick_merge = (modifiers & SDL_KMOD_ALT) != 0 && !additive_toggle &&
                             editor_selected_vertices_active_for_scene(runtime) &&
                             runtime->editor_selected_vertex_count > 0;
    if (quick_merge && editor_selected_vertex_index(runtime, &hovered) < 0)
    {
        if (!editor_merge_selected_vertices_to_target(runtime, NULL, &hovered))
            return false;
        publish_editor_selected_vertex_count(runtime);
        publish_editor_vertex_hover_state(runtime, hover_selection);
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    const bool already_selected = editor_selected_vertex_index(runtime, &hovered) >= 0;
    if (already_selected)
    {
        if (additive_toggle)
        {
            (void)editor_remove_shared_vertex_selection_group(runtime, &hovered);
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "vertex deselected");
        }
        else
        {
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "vertex selection drag");
            editor_begin_vertex_drag(runtime, runtime_input(runtime), hover_selection);
            editor_mark_vertex_drag_toggle_on_click(runtime, &hovered);
        }
    }
    else
    {
        if (!additive_toggle)
            clear_editor_selected_vertices(runtime);
        if (!editor_add_shared_vertex_selection_group(runtime, &hovered))
            return false;
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                       additive_toggle ? "vertex added to selection" : "vertex selected");
        editor_begin_vertex_drag(runtime, runtime_input(runtime), hover_selection);
    }

    publish_editor_selected_vertex_count(runtime);
    publish_editor_vertex_hover_state(runtime, hover_selection);
    if (out_consumed != NULL)
        *out_consumed = true;
    return true;
}

static bool editor_project_world_to_viewport(const slayer3d_camera3d *camera, const editor_trace_viewport_config *view,
                                             slayer3d_vec3 point, float *out_x, float *out_y)
{
    if (camera == NULL || view == NULL || out_x == NULL || out_y == NULL || view->width <= 0.0f || view->height <= 0.0f)
    {
        return false;
    }

    slayer3d_mat4 view_matrix;
    slayer3d_mat4 projection_matrix;
    if (!slayer3d_camera3d_compute_matrices(camera, (int)SDL_roundf(view->width), (int)SDL_roundf(view->height), 0.01f,
                                            1000.0f, &view_matrix, &projection_matrix))
    {
        return false;
    }

    const slayer3d_mat4 view_projection = slayer3d_mat4_multiply(projection_matrix, view_matrix);
    const slayer3d_vec4 clip = slayer3d_mat4_transform_vec4(view_projection, slayer3d_vec4_from_vec3(point, 1.0f));
    if (SDL_fabsf(clip.w) <= 0.000001f)
        return false;

    const float ndc_x = clip.x / clip.w;
    const float ndc_y = clip.y / clip.w;
    const float ndc_z = clip.z / clip.w;
    if (ndc_z < -1.0f || ndc_z > 1.0f)
        return false;

    *out_x = (ndc_x + 1.0f) * 0.5f * view->width;
    *out_y = (1.0f - ndc_y) * 0.5f * view->height;
    return true;
}

static bool editor_lasso_contains_point(const editor_drag_move_state *drag, const editor_trace_viewport_config *view,
                                        float screen_x, float screen_y)
{
    if (drag == NULL || view == NULL)
        return false;
    const float start_x = drag->start_mouse_x - view->x;
    const float start_y = drag->start_mouse_y - view->y;
    const float end_x = drag->current_mouse_x - view->x;
    const float end_y = drag->current_mouse_y - view->y;
    const float min_x = SDL_min(start_x, end_x);
    const float max_x = SDL_max(start_x, end_x);
    const float min_y = SDL_min(start_y, end_y);
    const float max_y = SDL_max(start_y, end_y);
    return screen_x >= min_x && screen_x <= max_x && screen_y >= min_y && screen_y <= max_y;
}

static int editor_select_vertices_in_lasso(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                           const editor_drag_move_state *drag)
{
    if (runtime == NULL || selection_json == NULL || drag == NULL || !editor_selected_brushes_active_for_scene(runtime))
        return 0;

    yyjson_val *trace = obj_get(selection_json, "trace");
    editor_trace_viewport_config viewport;
    if (!editor_trace_select_viewport_at(runtime, trace, drag->start_mouse_x, drag->start_mouse_y, &viewport))
        return 0;

    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, viewport.camera, &camera))
        return 0;

    if (!drag->lasso_additive)
        clear_editor_selected_vertices(runtime);

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

        for (int v = 0; v < model.vertex_count; ++v)
        {
            const slayer3d_vec3 local = editor_source_vertex_meters(world_runtime, &model.vertices[v]);
            const slayer3d_vec3 world = slayer3d_vec3_add(selection.world_position, local);
            float screen_x = 0.0f;
            float screen_y = 0.0f;
            if (!editor_project_world_to_viewport(&camera, &viewport, world, &screen_x, &screen_y) ||
                !editor_lasso_contains_point(drag, &viewport, screen_x, screen_y))
            {
                continue;
            }
            editor_source_vertex_selection candidate;
            if (editor_source_vertex_selection_from_model(world_runtime, &model, v, &candidate))
                (void)editor_add_shared_vertex_selection_group(runtime, &candidate);
        }
    }

    publish_editor_selected_vertex_count(runtime);
    return runtime->editor_selected_vertex_count;
}

bool editor_handle_vertex_lasso(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL)
        return true;

    editor_drag_move_state *drag = &runtime->editor_drag_move;
    if (!editor_mode_is_vertex(runtime))
    {
        if (drag->active && drag->vertex_lasso)
            clear_editor_drag_move(runtime);
        publish_editor_vertex_lasso_state(runtime, NULL, 0);
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

    if (drag->active && drag->vertex_lasso)
    {
        drag->current_mouse_x = mouse_x;
        drag->current_mouse_y = mouse_y;
        publish_editor_vertex_lasso_state(runtime, drag, 0);
        if (out_consumed != NULL)
            *out_consumed = true;
        if (left_released || !left_down)
        {
            const float dx = drag->current_mouse_x - drag->start_mouse_x;
            const float dy = drag->current_mouse_y - drag->start_mouse_y;
            const bool meaningful_drag = dx * dx + dy * dy >= 16.0f;
            const int selected_count =
                meaningful_drag ? editor_select_vertices_in_lasso(runtime, selection_json, drag) : 0;
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                           meaningful_drag ? "vertex lasso selected" : "vertex lasso canceled");
            publish_editor_vertex_lasso_state(runtime, NULL, selected_count);
            clear_editor_drag_move(runtime);
        }
        return true;
    }

    const bool selectable_hover = editor_selection_is_selectable_brush(hover_selection);
    if (left_pressed && !selectable_hover)
    {
        SDL_zero(*drag);
        drag->active = true;
        drag->vertex_lasso = true;
        drag->lasso_additive = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        drag->start_mouse_x = mouse_x;
        drag->start_mouse_y = mouse_y;
        drag->current_mouse_x = mouse_x;
        drag->current_mouse_y = mouse_y;
        publish_editor_vertex_lasso_state(runtime, drag, 0);
        if (out_consumed != NULL)
            *out_consumed = true;
    }
    return true;
}

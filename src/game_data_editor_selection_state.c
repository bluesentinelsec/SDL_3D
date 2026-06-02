/**
 * @file game_data_editor_selection_state.c
 * @brief Editor selection-state bookkeeping and published diagnostics.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/math.h"

bool editor_selected_brushes_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selected_brush_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) == 0;
}

void init_editor_source_vertex_selection(editor_source_vertex_selection *selection)
{
    if (selection == NULL)
        return;
    SDL_zero(*selection);
    selection->source_index = -1;
    selection->vertex_index = -1;
}

void init_editor_source_edge_selection(editor_source_edge_selection *selection)
{
    if (selection == NULL)
        return;
    SDL_zero(*selection);
    selection->source_index = -1;
    selection->edge_index = -1;
}

bool editor_selected_vertices_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selected_vertex_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selected_vertex_scene, active_scene) == 0;
}

bool editor_selected_edges_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selected_edge_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selected_edge_scene, active_scene) == 0;
}

void publish_editor_selected_vertex_count(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const int count = editor_selected_vertices_active_for_scene(runtime) ? runtime->editor_selected_vertex_count : 0;
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.count", count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.selection.multiple", count > 1);
    if (count <= 0)
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.world", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush_stable_id", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.vertex", "");
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.index", -1);
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.x", 0);
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.y", 0);
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.z", 0);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.selection.coord",
                                     slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        return;
    }

    const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[count - 1];
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.world", selection->world_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush", selection->brush_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush_stable_id",
                                   selection->brush_stable_id);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.vertex", selection->vertex_stable_id);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.index", selection->vertex_index);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.x", selection->coord[0]);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.y", selection->coord[1]);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.z", selection->coord[2]);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.selection.coord",
                                 slayer3d_vec3_make((float)selection->coord[0] * meters_per_unit,
                                                    (float)selection->coord[1] * meters_per_unit,
                                                    (float)selection->coord[2] * meters_per_unit));
}

void publish_editor_selected_edge_count(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const int count = editor_selected_edges_active_for_scene(runtime) ? runtime->editor_selected_edge_count : 0;
    slayer3d_properties_set_int(runtime->scene_state, "editor.edge.selection.count", count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.selection.multiple", count > 1);
    if (count <= 0)
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.world", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.brush", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.brush_stable_id", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.edge", "");
        slayer3d_properties_set_int(runtime->scene_state, "editor.edge.selection.index", -1);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.selection.start",
                                     slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.selection.end",
                                     slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        return;
    }

    const editor_source_edge_selection *selection = &runtime->editor_selected_edges[count - 1];
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.world", selection->world_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.brush", selection->brush_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.brush_stable_id",
                                   selection->brush_stable_id);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.edge", selection->edge_stable_id);
    slayer3d_properties_set_int(runtime->scene_state, "editor.edge.selection.index", selection->edge_index);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.selection.start",
                                 slayer3d_vec3_make((float)selection->coord[0][0] * meters_per_unit,
                                                    (float)selection->coord[0][1] * meters_per_unit,
                                                    (float)selection->coord[0][2] * meters_per_unit));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.selection.end",
                                 slayer3d_vec3_make((float)selection->coord[1][0] * meters_per_unit,
                                                    (float)selection->coord[1][1] * meters_per_unit,
                                                    (float)selection->coord[1][2] * meters_per_unit));
}

void clear_editor_selected_vertices(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY; ++i)
        init_editor_source_vertex_selection(&runtime->editor_selected_vertices[i]);
    runtime->editor_selected_vertex_count = 0;
    runtime->editor_selected_vertex_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_vertex_count(runtime);
}

void clear_editor_selected_edges(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < SLAYER3D_EDITOR_SELECTED_EDGE_CAPACITY; ++i)
        init_editor_source_edge_selection(&runtime->editor_selected_edges[i]);
    runtime->editor_selected_edge_count = 0;
    runtime->editor_selected_edge_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_edge_count(runtime);
}

void publish_editor_vertex_lasso_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                       int selected_count)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const bool active = drag != NULL && drag->active && drag->vertex_lasso;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.lasso.active", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.lasso.additive",
                                 active ? drag->lasso_additive : false);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.start_x",
                                  active ? drag->start_mouse_x : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.start_y",
                                  active ? drag->start_mouse_y : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.end_x",
                                  active ? drag->current_mouse_x : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.end_y",
                                  active ? drag->current_mouse_y : 0.0f);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.lasso.selected_count", selected_count);
}

void publish_editor_edge_lasso_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                     int selected_count)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const bool active = drag != NULL && drag->active && drag->edge_lasso;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.lasso.active", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.lasso.additive",
                                 active ? drag->lasso_additive : false);
    slayer3d_properties_set_float(runtime->scene_state, "editor.edge.lasso.start_x",
                                  active ? drag->start_mouse_x : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.edge.lasso.start_y",
                                  active ? drag->start_mouse_y : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.edge.lasso.end_x",
                                  active ? drag->current_mouse_x : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.edge.lasso.end_y",
                                  active ? drag->current_mouse_y : 0.0f);
    slayer3d_properties_set_int(runtime->scene_state, "editor.edge.lasso.selected_count", selected_count);
}

void publish_editor_selected_brush_count(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const int count = editor_selected_brushes_active_for_scene(runtime) ? runtime->editor_selected_brush_count : 0;
    slayer3d_properties_set_int(runtime->scene_state, "editor.selection.count", count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.selection.multiple", count > 1);
}

void clear_editor_selected_brushes(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    clear_editor_selected_vertices(runtime);
    clear_editor_selected_edges(runtime);
    for (int i = 0; i < SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY; ++i)
        init_editor_selection(&runtime->editor_selected_brushes[i]);
    runtime->editor_selected_brush_count = 0;
    runtime->editor_selected_brush_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_brush_count(runtime);
}

void clear_editor_active_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    init_editor_selection(&runtime->editor_active_selection);
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    clear_editor_selected_brushes(runtime);
    clear_editor_command_preview(runtime);
    clear_editor_placement_preview(runtime);
}

void clear_editor_vertex_hover_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.hover.hit", false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.brush", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.brush_stable_id", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.vertex", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.hover.selected", false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.index", -1);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.shared_count", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.x", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.y", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.z", 0);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.hover.coord",
                                 slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

void clear_editor_edge_hover_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.hover.hit", false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.hover.brush", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.hover.brush_stable_id", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.hover.edge", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.hover.selected", false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.edge.hover.index", -1);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.hover.start", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.hover.end", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

/**
 * @file game_data_editor_vertex_shared.c
 * @brief Shared editor vertex-tool lookup, state, and selection helpers.
 */

#include "game_data_editor_vertex_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"

void publish_editor_vertex_drag_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const bool active = drag != NULL && drag->active && drag->vertex_drag;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.drag.active", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.drag.moved", active ? drag->moved : false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.drag.axis_lock_y",
                                 active ? drag->axis_lock_y : false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.drag.axis_lock_dominant",
                                 active ? drag->axis_lock_dominant : false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.drag.guide_count",
                                active ? drag->vertex_origin_count : 0);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.drag.offset",
                                 active ? drag->applied_offset : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

void publish_editor_vertex_move_result(slayer3d_game_data_runtime *runtime, bool valid, int source_count,
                                       int changed_count, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.move.valid", valid);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.move.source_count", source_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.move.changed_count", changed_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.move.message", message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");
}

void clear_editor_drag_move(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    SDL_zero(runtime->editor_drag_move);
    publish_editor_vertex_drag_state(runtime, NULL);
    publish_editor_edge_drag_state(runtime, NULL);
}

const char *editor_metadata_stable_id(const slayer3d_game_data_editor_metadata *metadata)
{
    return metadata != NULL && metadata->stable_id != NULL ? metadata->stable_id : "";
}

int editor_source_index_for_selection(const brush_world_runtime *world_runtime,
                                      const slayer3d_game_data_editor_selection *selection)
{
    if (world_runtime == NULL || selection == NULL || !editor_selection_is_selectable_brush(selection))
        return -1;
    int source_index = editor_brush_world_find_source_box_index(world_runtime, selection->element_name);
    if (source_index >= 0)
        return source_index;
    return editor_brush_world_find_source_box_index(world_runtime,
                                                    editor_metadata_stable_id(selection->element_editor));
}

slayer3d_vec3 editor_source_vertex_meters(const brush_world_runtime *world_runtime,
                                          const editor_brush_source_vertex *vertex)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return slayer3d_vec3_make((float)vertex->coord[0] * meters_per_unit, (float)vertex->coord[1] * meters_per_unit,
                              (float)vertex->coord[2] * meters_per_unit);
}

int editor_collect_selected_source_indices(const slayer3d_game_data_runtime *runtime,
                                           const brush_world_runtime *world_runtime, int fallback_source_index,
                                           int *out_indices, int out_capacity)
{
    if (runtime == NULL || world_runtime == NULL || out_indices == NULL || out_capacity <= 0)
        return 0;

    int count = 0;
    for (int i = 0; i < runtime->editor_selected_brush_count && count < out_capacity; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (selection.world_name == NULL || SDL_strcmp(selection.world_name, world_runtime->desc.name) != 0)
            continue;
        const int source_index = editor_source_index_for_selection(world_runtime, &selection);
        if (source_index >= 0)
            out_indices[count++] = source_index;
    }
    if (count == 0 && fallback_source_index >= 0)
        out_indices[count++] = fallback_source_index;
    return count;
}

int editor_source_vertex_shared_count(const brush_world_runtime *world_runtime, const int coord[3],
                                      const int *selected_indices, int selected_count)
{
    const int shared_capacity = SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY * SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY;
    editor_brush_source_shared_vertex *shared =
        (editor_brush_source_shared_vertex *)SDL_calloc((size_t)shared_capacity, sizeof(*shared));
    int shared_count = 0;
    if (world_runtime == NULL || coord == NULL || selected_indices == NULL || selected_count <= 0 || shared == NULL ||
        !editor_brush_world_find_shared_source_vertices(world_runtime, selected_indices, selected_count, shared,
                                                        shared_capacity, &shared_count, NULL, 0))
    {
        SDL_free(shared);
        return 1;
    }
    for (int i = 0; i < shared_count; ++i)
    {
        if (shared[i].coord[0] == coord[0] && shared[i].coord[1] == coord[1] && shared[i].coord[2] == coord[2])
        {
            const int reference_count = SDL_max(shared[i].reference_count, 1);
            SDL_free(shared);
            return reference_count;
        }
    }
    SDL_free(shared);
    return 1;
}

static bool editor_source_vertex_selection_matches(const editor_source_vertex_selection *a,
                                                   const editor_source_vertex_selection *b)
{
    return a != NULL && b != NULL && a->source_index == b->source_index && a->vertex_index == b->vertex_index &&
           SDL_strcmp(a->world_name, b->world_name) == 0 && SDL_strcmp(a->brush_stable_id, b->brush_stable_id) == 0 &&
           SDL_strcmp(a->vertex_stable_id, b->vertex_stable_id) == 0;
}

int editor_selected_vertex_index(const slayer3d_game_data_runtime *runtime,
                                 const editor_source_vertex_selection *selection)
{
    if (runtime == NULL || selection == NULL || !editor_selected_vertices_active_for_scene(runtime))
        return -1;
    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        if (editor_source_vertex_selection_matches(&runtime->editor_selected_vertices[i], selection))
            return i;
    }
    return -1;
}

void remove_editor_selected_vertex_at(slayer3d_game_data_runtime *runtime, int index)
{
    if (runtime == NULL || index < 0 || index >= runtime->editor_selected_vertex_count)
        return;
    for (int i = index; i + 1 < runtime->editor_selected_vertex_count; ++i)
        runtime->editor_selected_vertices[i] = runtime->editor_selected_vertices[i + 1];
    runtime->editor_selected_vertex_count--;
    init_editor_source_vertex_selection(&runtime->editor_selected_vertices[runtime->editor_selected_vertex_count]);
}

bool add_editor_selected_vertex(slayer3d_game_data_runtime *runtime, const editor_source_vertex_selection *selection)
{
    if (runtime == NULL || selection == NULL ||
        runtime->editor_selected_vertex_count >= SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY)
    {
        return false;
    }
    if (!editor_selected_vertices_active_for_scene(runtime))
        clear_editor_selected_vertices(runtime);
    if (editor_selected_vertex_index(runtime, selection) >= 0)
        return true;
    runtime->editor_selected_vertices[runtime->editor_selected_vertex_count++] = *selection;
    runtime->editor_selected_vertex_scene = slayer3d_game_data_active_scene(runtime);
    return true;
}

bool editor_coord_equal(const int a[3], const int b[3])
{
    return a != NULL && b != NULL && a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

bool editor_source_vertex_selection_from_model(const brush_world_runtime *world_runtime,
                                               const editor_brush_source_vertex_model *model, int vertex_index,
                                               editor_source_vertex_selection *out_selection)
{
    if (world_runtime == NULL || model == NULL || out_selection == NULL || vertex_index < 0 ||
        vertex_index >= model->vertex_count)
    {
        return false;
    }

    init_editor_source_vertex_selection(out_selection);
    const editor_brush_source_vertex *vertex = &model->vertices[vertex_index];
    const editor_brush_source_box_runtime *box = NULL;
    if (model->brush_index >= 0 && model->brush_index < world_runtime->editor_source_box_count)
        box = &world_runtime->editor_source_boxes[model->brush_index];
    SDL_strlcpy(out_selection->world_name, world_runtime->desc.name != NULL ? world_runtime->desc.name : "",
                sizeof(out_selection->world_name));
    SDL_strlcpy(out_selection->brush_name, box != NULL && box->name != NULL ? box->name : model->brush_stable_id,
                sizeof(out_selection->brush_name));
    SDL_strlcpy(out_selection->brush_stable_id, model->brush_stable_id, sizeof(out_selection->brush_stable_id));
    SDL_strlcpy(out_selection->vertex_stable_id, vertex->stable_id, sizeof(out_selection->vertex_stable_id));
    out_selection->source_index = model->brush_index;
    out_selection->vertex_index = vertex->vertex_index;
    out_selection->coord[0] = vertex->coord[0];
    out_selection->coord[1] = vertex->coord[1];
    out_selection->coord[2] = vertex->coord[2];
    return true;
}

float editor_vertex_handle_pick_radius(const slayer3d_game_data_runtime *runtime)
{
    const float grid_size =
        runtime != NULL && runtime->scene_state != NULL
            ? SDL_max(slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f), 0.001f)
            : 1.0f;
    return SDL_max(grid_size * 0.75f, 0.05f);
}

bool editor_add_shared_vertex_selection_group(slayer3d_game_data_runtime *runtime,
                                              const editor_source_vertex_selection *selection)
{
    if (runtime == NULL || selection == NULL)
        return false;
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    if (world_runtime == NULL)
        return false;

    int selected_indices[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
    const int selected_count = editor_collect_selected_source_indices(
        runtime, world_runtime, selection->source_index, selected_indices, SDL_arraysize(selected_indices));
    bool added_any = false;
    for (int i = 0; i < selected_count; ++i)
    {
        editor_brush_source_vertex_model model;
        if (!editor_brush_source_box_build_vertex_model(world_runtime, selected_indices[i], &model, NULL, 0))
            continue;
        for (int v = 0; v < model.vertex_count; ++v)
        {
            if (!editor_coord_equal(model.vertices[v].coord, selection->coord))
                continue;
            editor_source_vertex_selection candidate;
            if (editor_source_vertex_selection_from_model(world_runtime, &model, v, &candidate) &&
                add_editor_selected_vertex(runtime, &candidate))
            {
                added_any = true;
            }
        }
    }
    return added_any;
}

bool editor_remove_shared_vertex_selection_group(slayer3d_game_data_runtime *runtime,
                                                 const editor_source_vertex_selection *selection)
{
    if (runtime == NULL || selection == NULL || !editor_selected_vertices_active_for_scene(runtime))
        return false;
    bool removed_any = false;
    for (int i = runtime->editor_selected_vertex_count - 1; i >= 0; --i)
    {
        const editor_source_vertex_selection *candidate = &runtime->editor_selected_vertices[i];
        if (SDL_strcmp(candidate->world_name, selection->world_name) == 0 &&
            editor_coord_equal(candidate->coord, selection->coord))
        {
            remove_editor_selected_vertex_at(runtime, i);
            removed_any = true;
        }
    }
    return removed_any;
}

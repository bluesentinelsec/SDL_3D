/**
 * @file game_data_editor_vertex_mode.c
 * @brief Editor vertex-mode drag handling and source vertex actions.
 */

#include "game_data_editor_vertex_internal.h"

#include <float.h>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/camera.h"
#include "slayer3d/math.h"

static float editor_snap_delta(float delta, float grid_size)
{
    const float grid = SDL_max(grid_size, 0.001f);
    return SDL_roundf(delta / grid) * grid;
}

static slayer3d_vec3 editor_lock_offset_to_dominant_axis(slayer3d_vec3 offset)
{
    const float abs_x = SDL_fabsf(offset.x);
    const float abs_y = SDL_fabsf(offset.y);
    const float abs_z = SDL_fabsf(offset.z);
    if (abs_x >= abs_y && abs_x >= abs_z)
        return slayer3d_vec3_make(offset.x, 0.0f, 0.0f);
    if (abs_y >= abs_z)
        return slayer3d_vec3_make(0.0f, offset.y, 0.0f);
    return slayer3d_vec3_make(0.0f, 0.0f, offset.z);
}

static void publish_editor_vertex_add_result(slayer3d_game_data_runtime *runtime, yyjson_val *action, bool valid,
                                             int vertex_count, int added_count, const char *message);
static void publish_editor_vertex_add_preview(slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_editor_selection *selection, const int coord[3],
                                              bool active, bool valid, const char *message);
static bool editor_preview_add_vertex_to_source_coord(brush_world_runtime *world, int source_index, const int coord[3],
                                                      int *out_vertex_count, char *message, size_t message_size);
static bool editor_add_vertex_to_source_coord(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              brush_world_runtime *world_runtime, int source_index, const int coord[3]);

static int editor_source_snap_units_from_grid(const brush_world_runtime *world_runtime, float grid_size)
{
    return SDL_max(editor_source_units_from_meters(world_runtime, grid_size), 1);
}

static int editor_snap_source_coord_to_units(int coord, int snap_units)
{
    if (snap_units <= 1)
        return coord;
    const int half = snap_units / 2;
    return coord >= 0 ? ((coord + half) / snap_units) * snap_units : ((coord - half) / snap_units) * snap_units;
}

static int editor_dominant_axis_from_normal(slayer3d_vec3 normal)
{
    const float abs_x = SDL_fabsf(normal.x);
    const float abs_y = SDL_fabsf(normal.y);
    const float abs_z = SDL_fabsf(normal.z);
    if (abs_x >= abs_y && abs_x >= abs_z)
        return 0;
    if (abs_y >= abs_z)
        return 1;
    return 2;
}

static bool editor_vertex_add_coord_from_selection(const brush_world_runtime *world_runtime,
                                                   const slayer3d_game_data_runtime *runtime,
                                                   const slayer3d_game_data_editor_selection *selection,
                                                   int out_coord[3])
{
    if (world_runtime == NULL || runtime == NULL || selection == NULL || out_coord == NULL || !selection->hit)
        return false;
    const float grid_size = SDL_max(slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f),
                                    world_runtime->editor_source_meters_per_unit);
    const int snap_units = editor_source_snap_units_from_grid(world_runtime, grid_size);
    const slayer3d_vec3 normal = slayer3d_vec3_length_squared(selection->normal) > 0.000001f
                                     ? slayer3d_vec3_normalize(selection->normal)
                                     : slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    const slayer3d_vec3 local_point = slayer3d_vec3_sub(selection->point, selection->world_position);
    out_coord[0] = editor_source_units_from_meters(world_runtime, local_point.x);
    out_coord[1] = editor_source_units_from_meters(world_runtime, local_point.y);
    out_coord[2] = editor_source_units_from_meters(world_runtime, local_point.z);

    const int fixed_axis = editor_dominant_axis_from_normal(normal);
    for (int axis = 0; axis < 3; ++axis)
    {
        if (axis != fixed_axis)
            out_coord[axis] = editor_snap_source_coord_to_units(out_coord[axis], snap_units);
    }
    return true;
}

bool editor_handle_vertex_add_to_source(slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_editor_selection *hover_selection,
                                        bool select_requested, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL)
        return true;

    if (!editor_mode_is_vertex(runtime) || (SDL_GetModState() & SDL_KMOD_SHIFT) == 0 ||
        !editor_selection_is_selectable_brush(hover_selection) || hover_selection->face_index < 0)
    {
        publish_editor_vertex_add_preview(runtime, NULL, NULL, false, false, NULL);
        return true;
    }

    editor_source_vertex_selection nearest;
    float nearest_distance_sq = FLT_MAX;
    (void)editor_hover_source_vertex_selection_with_distance(runtime, hover_selection, &nearest, &nearest_distance_sq);
    const float grid_size =
        SDL_max(slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f), 0.001f);
    const float handle_radius = SDL_max(grid_size * 0.75f, 0.05f);
    if (nearest_distance_sq <= handle_radius * handle_radius)
    {
        publish_editor_vertex_add_preview(runtime, NULL, NULL, false, false, NULL);
        return true;
    }

    const slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, hover_selection);
    if (editor_selected_brush_index(runtime, &resolved) < 0)
    {
        publish_editor_vertex_add_preview(runtime, NULL, NULL, false, false, NULL);
        return true;
    }
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, resolved.world_name);
    const int source_index = editor_source_index_for_selection(world_runtime, &resolved);
    int coord[3] = {0, 0, 0};
    if (world_runtime == NULL || source_index < 0 ||
        !editor_vertex_add_coord_from_selection(world_runtime, runtime, &resolved, coord))
    {
        publish_editor_vertex_add_preview(runtime, &resolved, coord, true, false, "vertex add requires a source face");
        if (select_requested)
        {
            publish_editor_vertex_add_result(runtime, NULL, false, 0, 0, "vertex add requires a source face");
            if (out_consumed != NULL)
                *out_consumed = true;
        }
        return true;
    }

    int preview_vertex_count = 0;
    char preview_message[256];
    SDL_zeroa(preview_message);
    const bool preview_valid = editor_preview_add_vertex_to_source_coord(
        world_runtime, source_index, coord, &preview_vertex_count, preview_message, sizeof(preview_message));
    publish_editor_vertex_add_preview(runtime, &resolved, coord, true, preview_valid, preview_message);
    if (!select_requested)
        return true;
    if (!preview_valid)
    {
        publish_editor_vertex_add_result(runtime, NULL, false, preview_vertex_count, 0, preview_message);
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    if (!editor_add_vertex_to_source_coord(runtime, NULL, world_runtime, source_index, coord))
        return false;
    clear_editor_vertex_hover_state(runtime);
    publish_editor_vertex_add_preview(runtime, NULL, NULL, false, false, NULL);
    if (out_consumed != NULL)
        *out_consumed = true;
    return true;
}

int editor_source_units_from_meters(const brush_world_runtime *world_runtime, float meters)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return (int)SDL_roundf(meters / meters_per_unit);
}

typedef struct editor_source_vertex_move_target
{
    brush_world_runtime *world_runtime;
    int source_index;
    int vertex_indices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int coords[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    int vertex_index_count;
} editor_source_vertex_move_target;

typedef struct editor_source_vertex_move_rollback
{
    brush_world_runtime *world_runtime;
    int source_index;
    editor_brush_source_box_runtime source_box;
    bool captured;
} editor_source_vertex_move_rollback;

static void editor_dispose_vertex_move_rollbacks(editor_source_vertex_move_rollback *rollbacks, int count)
{
    if (rollbacks == NULL || count <= 0)
        return;
    for (int i = 0; i < count; ++i)
    {
        if (rollbacks[i].captured)
            free_editor_brush_source_box_runtime(&rollbacks[i].source_box);
    }
}

static bool editor_capture_vertex_move_rollbacks(const editor_source_vertex_move_target *targets, int target_count,
                                                 editor_source_vertex_move_rollback *rollbacks, int rollback_capacity,
                                                 char *error_buffer, int error_buffer_size)
{
    if (targets == NULL || rollbacks == NULL || target_count < 0 || rollback_capacity < target_count)
    {
        set_error(error_buffer, error_buffer_size, "vertex move rollback capture requires valid targets");
        return false;
    }

    for (int i = 0; i < target_count; ++i)
    {
        if (targets[i].world_runtime == NULL || targets[i].source_index < 0 ||
            targets[i].source_index >= targets[i].world_runtime->editor_source_box_count)
        {
            set_error(error_buffer, error_buffer_size, "vertex move rollback source not found");
            return false;
        }
        rollbacks[i].world_runtime = targets[i].world_runtime;
        rollbacks[i].source_index = targets[i].source_index;
        if (!copy_editor_brush_source_box_runtime(
                &targets[i].world_runtime->editor_source_boxes[targets[i].source_index], &rollbacks[i].source_box))
        {
            set_error(error_buffer, error_buffer_size, "vertex move rollback copy failed");
            return false;
        }
        rollbacks[i].captured = true;
    }
    return true;
}

static void editor_restore_vertex_move_rollbacks(editor_source_vertex_move_rollback *rollbacks, int count)
{
    if (rollbacks == NULL || count <= 0)
        return;

    for (int i = 0; i < count; ++i)
    {
        if (!rollbacks[i].captured || rollbacks[i].world_runtime == NULL || rollbacks[i].source_index < 0 ||
            rollbacks[i].source_index >= rollbacks[i].world_runtime->editor_source_box_count)
        {
            continue;
        }
        free_editor_brush_source_box_runtime(
            &rollbacks[i].world_runtime->editor_source_boxes[rollbacks[i].source_index]);
        rollbacks[i].world_runtime->editor_source_boxes[rollbacks[i].source_index] = rollbacks[i].source_box;
        SDL_zero(rollbacks[i].source_box);
        rollbacks[i].captured = false;
    }

    for (int i = 0; i < count; ++i)
    {
        if (rollbacks[i].world_runtime == NULL)
            continue;
        bool already_rebuilt = false;
        for (int j = 0; j < i; ++j)
        {
            if (rollbacks[j].world_runtime == rollbacks[i].world_runtime)
                already_rebuilt = true;
        }
        if (!already_rebuilt)
            (void)editor_brush_world_rebuild_from_source(rollbacks[i].world_runtime, NULL, 0);
    }
}

static editor_source_vertex_move_target *editor_find_vertex_move_target(editor_source_vertex_move_target *targets,
                                                                        int count,
                                                                        const brush_world_runtime *world_runtime,
                                                                        int source_index)
{
    if (targets == NULL || world_runtime == NULL || source_index < 0)
        return NULL;
    for (int i = 0; i < count; ++i)
    {
        if (targets[i].world_runtime == world_runtime && targets[i].source_index == source_index)
            return &targets[i];
    }
    return NULL;
}

static bool editor_vertex_move_target_add(editor_source_vertex_move_target *target,
                                          const editor_source_vertex_selection *selection, const int delta[3])
{
    if (target == NULL || selection == NULL || delta == NULL || selection->vertex_index < 0)
        return false;

    for (int i = 0; i < target->vertex_index_count; ++i)
    {
        if (target->vertex_indices[i] == selection->vertex_index)
            return true;
    }
    if (target->vertex_index_count >= SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
        return false;

    const int index = target->vertex_index_count++;
    target->vertex_indices[index] = selection->vertex_index;
    for (int axis = 0; axis < 3; ++axis)
        target->coords[index][axis] = selection->coord[axis] + delta[axis];
    return true;
}

static bool editor_vertex_move_target_add_coord(editor_source_vertex_move_target *target,
                                                const editor_source_vertex_selection *selection,
                                                const int target_coord[3])
{
    if (target == NULL || selection == NULL || target_coord == NULL || selection->vertex_index < 0)
        return false;

    for (int i = 0; i < target->vertex_index_count; ++i)
    {
        if (target->vertex_indices[i] == selection->vertex_index)
            return true;
    }
    if (target->vertex_index_count >= SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
        return false;

    const int index = target->vertex_index_count++;
    target->vertex_indices[index] = selection->vertex_index;
    for (int axis = 0; axis < 3; ++axis)
        target->coords[index][axis] = target_coord[axis];
    return true;
}

static const char *editor_source_move_target_identity(const editor_source_vertex_move_target *target)
{
    if (target == NULL || target->world_runtime == NULL || target->source_index < 0 ||
        target->source_index >= target->world_runtime->editor_source_box_count)
    {
        return NULL;
    }
    const editor_brush_source_box_runtime *box = &target->world_runtime->editor_source_boxes[target->source_index];
    return box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;
}

static void editor_init_vertex_move_desc(const editor_source_vertex_move_target *target,
                                         editor_brush_source_vertex_operation_desc *desc)
{
    if (desc == NULL)
        return;
    SDL_zero(*desc);
    desc->brush_identity = editor_source_move_target_identity(target);
    desc->type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MOVE_MANY;
    if (target == NULL)
        return;
    desc->vertex_index_count = target->vertex_index_count;
    for (int i = 0; i < target->vertex_index_count; ++i)
    {
        desc->vertex_indices[i] = target->vertex_indices[i];
        for (int axis = 0; axis < 3; ++axis)
            desc->coords[i][axis] = target->coords[i][axis];
    }
}

static void editor_refresh_selected_brushes_after_source_edit(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || !editor_selected_brushes_active_for_scene(runtime))
        return;
    for (int i = runtime->editor_selected_brush_count - 1; i >= 0; --i)
    {
        const slayer3d_game_data_editor_selection stale = runtime->editor_selected_brushes[i];
        slayer3d_game_data_editor_selection refreshed;
        if (!editor_selection_from_brush_index(runtime, stale.world_name, stale.element_index, stale.face_index,
                                               &refreshed))
        {
            remove_editor_selected_brush_at(runtime, i);
            continue;
        }
        runtime->editor_selected_brushes[i] = resolved_editor_selection(runtime, &refreshed);
    }
    update_active_editor_selection_from_selected_brushes(runtime);
    publish_editor_selected_brush_count(runtime);
}

static void editor_refresh_selected_vertices_after_source_edit(slayer3d_game_data_runtime *runtime,
                                                               const brush_world_runtime *world_runtime)
{
    if (runtime == NULL || world_runtime == NULL || !editor_selected_vertices_active_for_scene(runtime))
        return;
    for (int i = runtime->editor_selected_vertex_count - 1; i >= 0; --i)
    {
        editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        if (SDL_strcmp(selection->world_name, world_runtime->desc.name) != 0)
            continue;
        editor_brush_source_vertex_model model;
        if (!editor_brush_source_box_build_vertex_model(world_runtime, selection->source_index, &model, NULL, 0) ||
            selection->vertex_index < 0 || selection->vertex_index >= model.vertex_count)
        {
            remove_editor_selected_vertex_at(runtime, i);
            continue;
        }
        editor_source_vertex_selection refreshed;
        if (editor_source_vertex_selection_from_model(world_runtime, &model, selection->vertex_index, &refreshed))
            *selection = refreshed;
    }
    publish_editor_selected_vertex_count(runtime);
}

static bool editor_add_world_vertex_selection_group(slayer3d_game_data_runtime *runtime,
                                                    const brush_world_runtime *world_runtime, const int coord[3])
{
    if (runtime == NULL || world_runtime == NULL || coord == NULL)
        return false;

    bool added_any = false;
    for (int source_index = 0; source_index < world_runtime->editor_source_box_count; ++source_index)
    {
        editor_brush_source_vertex_model model;
        if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
            continue;

        for (int vertex_index = 0; vertex_index < model.vertex_count; ++vertex_index)
        {
            if (!editor_coord_equal(model.vertices[vertex_index].coord, coord))
                continue;

            editor_source_vertex_selection selection;
            if (editor_source_vertex_selection_from_model(world_runtime, &model, vertex_index, &selection) &&
                add_editor_selected_vertex(runtime, &selection))
            {
                added_any = true;
            }
        }
    }
    return added_any;
}

static bool editor_target_coord_already_seen(const editor_source_vertex_move_target *targets, int target_count,
                                             const brush_world_runtime *world_runtime, const int coord[3])
{
    if (targets == NULL || world_runtime == NULL || coord == NULL)
        return false;
    for (int i = 0; i < target_count; ++i)
    {
        if (targets[i].world_runtime != world_runtime)
            continue;
        for (int coord_index = 0; coord_index < targets[i].vertex_index_count; ++coord_index)
        {
            if (editor_coord_equal(targets[i].coords[coord_index], coord))
                return true;
        }
    }
    return false;
}

static void editor_reselect_vertices_from_move_targets(slayer3d_game_data_runtime *runtime,
                                                       const editor_source_vertex_move_target *targets,
                                                       int target_count)
{
    if (runtime == NULL || targets == NULL || target_count <= 0)
        return;

    clear_editor_selected_vertices(runtime);
    for (int i = 0; i < target_count; ++i)
    {
        const editor_source_vertex_move_target *target = &targets[i];
        if (target->world_runtime == NULL)
            continue;
        for (int coord_index = 0; coord_index < target->vertex_index_count; ++coord_index)
        {
            if (editor_target_coord_already_seen(targets, i, target->world_runtime, target->coords[coord_index]))
                continue;
            (void)editor_add_world_vertex_selection_group(runtime, target->world_runtime, target->coords[coord_index]);
        }
    }
    publish_editor_selected_vertex_count(runtime);
}

static void editor_mark_dirty_and_refresh_after_vertex_targets(slayer3d_game_data_runtime *runtime,
                                                               const editor_source_vertex_move_target *targets,
                                                               int target_count)
{
    if (runtime == NULL || targets == NULL || target_count <= 0)
        return;

    for (int i = 0; i < target_count; ++i)
    {
        if (targets[i].world_runtime == NULL)
            continue;

        bool already_refreshed = false;
        for (int previous = 0; previous < i; ++previous)
        {
            if (targets[previous].world_runtime == targets[i].world_runtime)
                already_refreshed = true;
        }
        if (already_refreshed)
            continue;

        editor_brush_world_mark_dirty(targets[i].world_runtime);
        editor_refresh_selected_brushes_after_source_edit(runtime);
    }
}

static bool editor_apply_vertex_move_targets(const editor_source_vertex_move_target *targets, int target_count,
                                             int *out_changed_count, char *error, size_t error_size)
{
    if (out_changed_count != NULL)
        *out_changed_count = 0;
    if (error != NULL && error_size > 0)
        error[0] = '\0';
    if (targets == NULL || target_count <= 0)
    {
        set_error(error, (int)error_size, "vertex move requires target vertices");
        return false;
    }

    int total_changed = 0;
    for (int i = 0; i < target_count; ++i)
    {
        editor_brush_source_vertex_operation_desc desc;
        editor_init_vertex_move_desc(&targets[i], &desc);
        editor_brush_source_vertex_operation_result preview;
        SDL_zero(preview);
        if (!editor_brush_world_preview_source_vertex_operation(targets[i].world_runtime, &desc, &preview, error,
                                                                (int)error_size))
        {
            editor_brush_source_free_runtime_brush(&preview.brush);
            return false;
        }
        total_changed += preview.changed_count;
        editor_brush_source_free_runtime_brush(&preview.brush);
    }

    editor_source_vertex_move_rollback rollbacks[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(rollbacks);
    if (!editor_capture_vertex_move_rollbacks(targets, target_count, rollbacks, SDL_arraysize(rollbacks), error,
                                              (int)error_size))
    {
        editor_dispose_vertex_move_rollbacks(rollbacks, target_count);
        return false;
    }

    for (int i = 0; i < target_count; ++i)
    {
        editor_brush_source_vertex_operation_desc desc;
        editor_init_vertex_move_desc(&targets[i], &desc);
        editor_brush_source_vertex_operation_result result;
        SDL_zero(result);
        if (!editor_brush_world_apply_source_vertex_operation(targets[i].world_runtime, &desc, &result, error,
                                                              (int)error_size))
        {
            editor_restore_vertex_move_rollbacks(rollbacks, target_count);
            editor_brush_source_free_runtime_brush(&result.brush);
            return false;
        }
        editor_brush_source_free_runtime_brush(&result.brush);
    }
    editor_dispose_vertex_move_rollbacks(rollbacks, target_count);
    if (out_changed_count != NULL)
        *out_changed_count = total_changed;
    return true;
}

bool editor_translate_selected_vertices(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset)
{
    if (runtime == NULL || runtime->scene_state == NULL || !editor_selected_vertices_active_for_scene(runtime) ||
        runtime->editor_selected_vertex_count <= 0 || slayer3d_vec3_length_squared(offset) <= 0.0000001f)
    {
        return false;
    }

    const char *world_name = runtime->editor_selected_vertices[0].world_name;
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;

    const int delta[3] = {editor_source_units_from_meters(world_runtime, offset.x),
                          editor_source_units_from_meters(world_runtime, offset.y),
                          editor_source_units_from_meters(world_runtime, offset.z)};
    if (delta[0] == 0 && delta[1] == 0 && delta[2] == 0)
        return false;

    editor_source_vertex_move_target targets[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(targets);
    int target_count = 0;
    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        if (SDL_strcmp(selection->world_name, world_runtime->desc.name) != 0 || selection->source_index < 0 ||
            selection->source_index >= world_runtime->editor_source_box_count)
        {
            publish_editor_vertex_move_result(runtime, false, target_count, 0,
                                              "vertex move blocked: invalid source selection");
            return false;
        }

        editor_source_vertex_move_target *target =
            editor_find_vertex_move_target(targets, target_count, world_runtime, selection->source_index);
        if (target == NULL)
        {
            if (target_count >= (int)SDL_arraysize(targets))
                return false;
            target = &targets[target_count++];
            target->world_runtime = world_runtime;
            target->source_index = selection->source_index;
        }

        if (!editor_vertex_move_target_add(target, selection, delta))
        {
            publish_editor_vertex_move_result(runtime, false, target_count, 0,
                                              "vertex move blocked: too many selected vertices");
            return false;
        }
    }

    char error[256];
    SDL_zeroa(error);
    int total_changed = 0;
    if (!editor_apply_vertex_move_targets(targets, target_count, &total_changed, error, sizeof(error)))
    {
        char message[320];
        SDL_snprintf(message, sizeof(message), "vertex move blocked: %s",
                     error[0] != '\0' ? error : "invalid source edit");
        publish_editor_vertex_move_result(runtime, false, target_count, total_changed, message);
        for (int i = 0; i < target_count; ++i)
            editor_refresh_selected_vertices_after_source_edit(runtime, targets[i].world_runtime);
        editor_refresh_selected_brushes_after_source_edit(runtime);
        return false;
    }

    editor_mark_dirty_and_refresh_after_vertex_targets(runtime, targets, target_count);
    editor_reselect_vertices_from_move_targets(runtime, targets, target_count);
    publish_editor_vertex_move_result(runtime, true, target_count, total_changed, "moved selected vertices");
    return true;
}

typedef struct editor_source_snap_target
{
    brush_world_runtime *world_runtime;
    int source_index;
} editor_source_snap_target;

static bool editor_add_source_snap_target(editor_source_snap_target *targets, int *target_count,
                                          brush_world_runtime *world_runtime, int source_index)
{
    if (targets == NULL || target_count == NULL || world_runtime == NULL || source_index < 0)
        return false;
    for (int i = 0; i < *target_count; ++i)
    {
        if (targets[i].world_runtime == world_runtime && targets[i].source_index == source_index)
            return true;
    }
    if (*target_count >= SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY)
        return false;
    targets[*target_count].world_runtime = world_runtime;
    targets[*target_count].source_index = source_index;
    ++(*target_count);
    return true;
}

static int editor_collect_vertex_snap_targets(slayer3d_game_data_runtime *runtime, editor_source_snap_target *targets,
                                              int target_capacity)
{
    if (runtime == NULL || targets == NULL || target_capacity <= 0 ||
        !editor_selected_vertices_active_for_scene(runtime))
        return 0;
    int target_count = 0;
    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, selection->world_name);
        if (world_runtime == NULL || selection->source_index < 0 ||
            selection->source_index >= world_runtime->editor_source_box_count)
        {
            continue;
        }
        if (target_count >= target_capacity ||
            !editor_add_source_snap_target(targets, &target_count, world_runtime, selection->source_index))
            break;
    }
    return target_count;
}

static int editor_collect_brush_snap_targets(slayer3d_game_data_runtime *runtime, editor_source_snap_target *targets,
                                             int target_capacity)
{
    if (runtime == NULL || targets == NULL || target_capacity <= 0 ||
        !editor_selected_brushes_active_for_scene(runtime))
        return 0;
    int target_count = 0;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, selection.world_name);
        const int source_index = editor_source_index_for_selection(world_runtime, &selection);
        if (target_count >= target_capacity ||
            !editor_add_source_snap_target(targets, &target_count, world_runtime, source_index))
            break;
    }
    return target_count;
}

static int editor_snap_units_from_action(const brush_world_runtime *world_runtime,
                                         const slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const int authored_snap_units = json_int(action, "snap_units", 0);
    if (authored_snap_units > 0)
        return authored_snap_units;

    const float fallback_grid = json_float(action, "default_grid", 1.0f);
    float grid = json_float(action, "grid", fallback_grid);
    const char *grid_key = json_string(action, "grid_key", "editor.grid.size");
    if (runtime != NULL && grid_key != NULL && grid_key[0] != '\0')
        grid = slayer3d_properties_get_float(runtime->scene_state, grid_key, grid);
    return SDL_max(editor_source_units_from_meters(world_runtime, SDL_max(grid, 0.001f)), 1);
}

static void publish_editor_vertex_snap_result(slayer3d_game_data_runtime *runtime, yyjson_val *action, bool valid,
                                              int source_count, int changed_count, int snap_units, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.snap.valid", valid);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.snap.source_count", source_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.snap.changed_count", changed_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.snap.snap_units", snap_units);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.snap.message", message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");

    yyjson_val *outputs = obj_get(action, "outputs");
    if (!yyjson_is_obj(outputs))
        return;
    const char *valid_key = json_string(outputs, "valid_key", NULL);
    const char *message_key = json_string(outputs, "message_key", NULL);
    const char *source_count_key = json_string(outputs, "source_count_key", NULL);
    const char *changed_count_key = json_string(outputs, "changed_count_key", NULL);
    const char *snap_units_key = json_string(outputs, "snap_units_key", NULL);
    if (valid_key != NULL)
        slayer3d_properties_set_bool(runtime->scene_state, valid_key, valid);
    if (message_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, message_key, message != NULL ? message : "");
    if (source_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, source_count_key, source_count);
    if (changed_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, changed_count_key, changed_count);
    if (snap_units_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, snap_units_key, snap_units);
}

bool slayer3d_game_data_snap_selected_editor_vertices(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    editor_source_snap_target targets[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(targets);
    int target_count = editor_collect_vertex_snap_targets(runtime, targets, SDL_arraysize(targets));
    if (target_count <= 0)
        target_count = editor_collect_brush_snap_targets(runtime, targets, SDL_arraysize(targets));
    if (runtime == NULL || target_count <= 0)
    {
        publish_editor_vertex_snap_result(runtime, action, false, 0, 0, 0, "vertex snap requires a selection");
        return true;
    }

    int total_changed = 0;
    int last_snap_units = 0;
    char error[256];
    SDL_zeroa(error);
    for (int i = 0; i < target_count; ++i)
    {
        brush_world_runtime *world_runtime = targets[i].world_runtime;
        editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[targets[i].source_index];
        const char *identity = box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;
        last_snap_units = editor_snap_units_from_action(world_runtime, runtime, action);
        editor_brush_source_vertex_operation_desc desc;
        SDL_zero(desc);
        desc.brush_identity = identity;
        desc.type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_SNAP;
        desc.snap_units = last_snap_units;
        editor_brush_source_vertex_operation_result result;
        SDL_zero(result);
        if (!editor_brush_world_apply_source_vertex_operation(world_runtime, &desc, &result, error, sizeof(error)))
        {
            char message[320];
            SDL_snprintf(message, sizeof(message), "vertex snap blocked: %s",
                         error[0] != '\0' ? error : "invalid source edit");
            publish_editor_vertex_snap_result(runtime, action, false, target_count, total_changed, last_snap_units,
                                              message);
            editor_brush_source_free_runtime_brush(&result.brush);
            return true;
        }
        total_changed += result.changed_count;
        editor_brush_source_free_runtime_brush(&result.brush);
    }

    for (int i = 0; i < target_count; ++i)
    {
        editor_refresh_selected_vertices_after_source_edit(runtime, targets[i].world_runtime);
        editor_refresh_selected_brushes_after_source_edit(runtime);
    }

    char message[160];
    SDL_snprintf(message, sizeof(message), "snapped %d source brush%s to grid", target_count,
                 target_count == 1 ? "" : "es");
    publish_editor_vertex_snap_result(runtime, action, true, target_count, total_changed, last_snap_units, message);
    return true;
}

typedef struct editor_source_vertex_delete_target
{
    brush_world_runtime *world_runtime;
    int source_index;
    int vertex_indices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int vertex_index_count;
} editor_source_vertex_delete_target;

static editor_source_vertex_delete_target *editor_find_vertex_delete_target(editor_source_vertex_delete_target *targets,
                                                                            int target_count,
                                                                            brush_world_runtime *world_runtime,
                                                                            int source_index)
{
    if (targets == NULL || world_runtime == NULL || source_index < 0)
        return NULL;
    for (int i = 0; i < target_count; ++i)
    {
        if (targets[i].world_runtime == world_runtime && targets[i].source_index == source_index)
            return &targets[i];
    }
    return NULL;
}

static bool editor_vertex_delete_target_add_index(editor_source_vertex_delete_target *target, int vertex_index)
{
    if (target == NULL || vertex_index < 0)
        return false;
    for (int i = 0; i < target->vertex_index_count; ++i)
    {
        if (target->vertex_indices[i] == vertex_index)
            return true;
    }
    if (target->vertex_index_count >= SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
        return false;
    target->vertex_indices[target->vertex_index_count++] = vertex_index;
    return true;
}

static int editor_collect_vertex_delete_targets(slayer3d_game_data_runtime *runtime,
                                                editor_source_vertex_delete_target *targets, int target_capacity)
{
    if (runtime == NULL || targets == NULL || target_capacity <= 0 ||
        !editor_selected_vertices_active_for_scene(runtime))
    {
        return 0;
    }

    int target_count = 0;
    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, selection->world_name);
        if (world_runtime == NULL || selection->source_index < 0 ||
            selection->source_index >= world_runtime->editor_source_box_count)
        {
            continue;
        }
        editor_source_vertex_delete_target *target =
            editor_find_vertex_delete_target(targets, target_count, world_runtime, selection->source_index);
        if (target == NULL)
        {
            if (target_count >= target_capacity)
                break;
            target = &targets[target_count++];
            SDL_zero(*target);
            target->world_runtime = world_runtime;
            target->source_index = selection->source_index;
        }
        if (!editor_vertex_delete_target_add_index(target, selection->vertex_index))
            break;
    }
    return target_count;
}

static const char *editor_source_delete_target_identity(const editor_source_vertex_delete_target *target)
{
    if (target == NULL || target->world_runtime == NULL || target->source_index < 0 ||
        target->source_index >= target->world_runtime->editor_source_box_count)
    {
        return NULL;
    }
    const editor_brush_source_box_runtime *box = &target->world_runtime->editor_source_boxes[target->source_index];
    return box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;
}

static void publish_editor_vertex_delete_result(slayer3d_game_data_runtime *runtime, yyjson_val *action, bool valid,
                                                int source_count, int deleted_count, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.delete.valid", valid);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.delete.source_count", source_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.delete.deleted_count", deleted_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.delete.message",
                                   message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");

    yyjson_val *outputs = obj_get(action, "outputs");
    if (!yyjson_is_obj(outputs))
        return;
    const char *valid_key = json_string(outputs, "valid_key", NULL);
    const char *message_key = json_string(outputs, "message_key", NULL);
    const char *source_count_key = json_string(outputs, "source_count_key", NULL);
    const char *deleted_count_key = json_string(outputs, "deleted_count_key", NULL);
    if (valid_key != NULL)
        slayer3d_properties_set_bool(runtime->scene_state, valid_key, valid);
    if (message_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, message_key, message != NULL ? message : "");
    if (source_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, source_count_key, source_count);
    if (deleted_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, deleted_count_key, deleted_count);
}

bool slayer3d_game_data_delete_selected_editor_vertices(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    editor_source_vertex_delete_target targets[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(targets);
    const int target_count = editor_collect_vertex_delete_targets(runtime, targets, SDL_arraysize(targets));
    if (runtime == NULL || target_count <= 0)
    {
        publish_editor_vertex_delete_result(runtime, action, false, 0, 0, "vertex delete requires a selection");
        return true;
    }

    int total_deleted = 0;
    char error[256];
    SDL_zeroa(error);
    for (int i = 0; i < target_count; ++i)
    {
        const char *identity = editor_source_delete_target_identity(&targets[i]);
        editor_brush_source_vertex_operation_desc desc;
        SDL_zero(desc);
        desc.brush_identity = identity;
        desc.type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_DELETE_MANY;
        desc.vertex_index_count = targets[i].vertex_index_count;
        for (int v = 0; v < targets[i].vertex_index_count; ++v)
            desc.vertex_indices[v] = targets[i].vertex_indices[v];

        editor_brush_source_vertex_operation_result preview;
        SDL_zero(preview);
        if (!editor_brush_world_preview_source_vertex_operation(targets[i].world_runtime, &desc, &preview, error,
                                                                sizeof(error)))
        {
            char message[320];
            SDL_snprintf(message, sizeof(message), "vertex delete blocked: %s",
                         error[0] != '\0' ? error : "invalid source edit");
            publish_editor_vertex_delete_result(runtime, action, false, target_count, total_deleted, message);
            editor_brush_source_free_runtime_brush(&preview.brush);
            return true;
        }
        total_deleted += preview.changed_count;
        editor_brush_source_free_runtime_brush(&preview.brush);
    }

    total_deleted = 0;
    for (int i = 0; i < target_count; ++i)
    {
        const char *identity = editor_source_delete_target_identity(&targets[i]);
        editor_brush_source_vertex_operation_desc desc;
        SDL_zero(desc);
        desc.brush_identity = identity;
        desc.type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_DELETE_MANY;
        desc.vertex_index_count = targets[i].vertex_index_count;
        for (int v = 0; v < targets[i].vertex_index_count; ++v)
            desc.vertex_indices[v] = targets[i].vertex_indices[v];

        editor_brush_source_vertex_operation_result result;
        SDL_zero(result);
        if (!editor_brush_world_apply_source_vertex_operation(targets[i].world_runtime, &desc, &result, error,
                                                              sizeof(error)))
        {
            char message[320];
            SDL_snprintf(message, sizeof(message), "vertex delete blocked: %s",
                         error[0] != '\0' ? error : "invalid source edit");
            publish_editor_vertex_delete_result(runtime, action, false, target_count, total_deleted, message);
            editor_brush_source_free_runtime_brush(&result.brush);
            return true;
        }
        total_deleted += result.changed_count;
        editor_brush_source_free_runtime_brush(&result.brush);
    }

    for (int i = 0; i < target_count; ++i)
    {
        editor_refresh_selected_vertices_after_source_edit(runtime, targets[i].world_runtime);
        editor_refresh_selected_brushes_after_source_edit(runtime);
    }
    clear_editor_selected_vertices(runtime);

    char message[160];
    SDL_snprintf(message, sizeof(message), "deleted %d selected source vert%s", total_deleted,
                 total_deleted == 1 ? "ex" : "ices");
    publish_editor_vertex_delete_result(runtime, action, true, target_count, total_deleted, message);
    return true;
}

static bool editor_vertex_merge_target_from_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                   editor_source_vertex_selection *out_target)
{
    if (runtime == NULL || out_target == NULL)
        return false;

    const int target_vertex_index = json_int(action, "target_vertex_index", -1);
    if (target_vertex_index < 0)
        return editor_hover_source_vertex_selection(runtime, &runtime->editor_active_selection, out_target);

    const char *world_name = json_string(action, "world", NULL);
    if ((world_name == NULL || world_name[0] == '\0') && editor_selected_vertices_active_for_scene(runtime) &&
        runtime->editor_selected_vertex_count > 0)
    {
        world_name = runtime->editor_selected_vertices[0].world_name;
    }
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    if (world_runtime == NULL)
        return false;

    int source_index = -1;
    const char *brush_identity = json_string(action, "brush_stable_id", NULL);
    if (brush_identity == NULL || brush_identity[0] == '\0')
        brush_identity = json_string(action, "brush", NULL);
    if (brush_identity != NULL && brush_identity[0] != '\0')
        source_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
    if (source_index < 0 && editor_selected_vertices_active_for_scene(runtime) &&
        runtime->editor_selected_vertex_count > 0 &&
        SDL_strcmp(runtime->editor_selected_vertices[0].world_name, world_runtime->desc.name) == 0)
    {
        source_index = runtime->editor_selected_vertices[0].source_index;
    }
    if (source_index < 0)
        return false;

    editor_brush_source_vertex_model model;
    if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
        return false;
    return editor_source_vertex_selection_from_model(world_runtime, &model, target_vertex_index, out_target);
}

static bool editor_collect_vertex_merge_move_targets(slayer3d_game_data_runtime *runtime,
                                                     const editor_source_vertex_selection *merge_target,
                                                     editor_source_vertex_move_target *targets, int target_capacity,
                                                     int *out_target_count, char *message_buffer,
                                                     size_t message_buffer_size)
{
    if (out_target_count != NULL)
        *out_target_count = 0;
    if (runtime == NULL || merge_target == NULL || targets == NULL || target_capacity <= 0 ||
        !editor_selected_vertices_active_for_scene(runtime))
    {
        set_error(message_buffer, (int)message_buffer_size, "vertex merge requires selected vertices");
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, merge_target->world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(message_buffer, (int)message_buffer_size, "vertex merge source brush not found");
        return false;
    }

    int target_count = 0;
    int merge_count = 0;
    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        if (SDL_strcmp(selection->world_name, world_runtime->desc.name) != 0 || selection->source_index < 0 ||
            selection->source_index >= world_runtime->editor_source_box_count)
        {
            set_error(message_buffer, (int)message_buffer_size, "vertex merge blocked: invalid source selection");
            return false;
        }
        if (editor_coord_equal(selection->coord, merge_target->coord))
            continue;

        editor_source_vertex_move_target *target =
            editor_find_vertex_move_target(targets, target_count, world_runtime, selection->source_index);
        if (target == NULL)
        {
            if (target_count >= target_capacity)
            {
                set_error(message_buffer, (int)message_buffer_size, "vertex merge blocked: too many source brushes");
                return false;
            }
            target = &targets[target_count++];
            target->world_runtime = world_runtime;
            target->source_index = selection->source_index;
        }
        if (!editor_vertex_move_target_add_coord(target, selection, merge_target->coord))
        {
            set_error(message_buffer, (int)message_buffer_size, "vertex merge blocked: too many selected vertices");
            return false;
        }
        ++merge_count;
    }

    if (merge_count <= 0)
    {
        set_error(message_buffer, (int)message_buffer_size, "vertex merge requires selected vertices away from target");
        return false;
    }
    if (out_target_count != NULL)
        *out_target_count = target_count;
    return true;
}

static void publish_editor_vertex_merge_result(slayer3d_game_data_runtime *runtime, yyjson_val *action, bool valid,
                                               int merged_count, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.merge.valid", valid);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.merge.merged_count", merged_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.merge.message", message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");

    yyjson_val *outputs = obj_get(action, "outputs");
    if (!yyjson_is_obj(outputs))
        return;
    const char *valid_key = json_string(outputs, "valid_key", NULL);
    const char *message_key = json_string(outputs, "message_key", NULL);
    const char *merged_count_key = json_string(outputs, "merged_count_key", NULL);
    if (valid_key != NULL)
        slayer3d_properties_set_bool(runtime->scene_state, valid_key, valid);
    if (message_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, message_key, message != NULL ? message : "");
    if (merged_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, merged_count_key, merged_count);
}

bool slayer3d_game_data_merge_selected_editor_vertices_to_hover(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    editor_source_vertex_selection target;
    init_editor_source_vertex_selection(&target);
    if (runtime == NULL || !editor_vertex_merge_target_from_action(runtime, action, &target))
    {
        publish_editor_vertex_merge_result(runtime, action, false, 0, "vertex merge requires a target vertex");
        return true;
    }

    return editor_merge_selected_vertices_to_target(runtime, action, &target);
}

bool editor_merge_selected_vertices_to_target(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              const editor_source_vertex_selection *target)
{
    if (runtime == NULL || target == NULL)
    {
        publish_editor_vertex_merge_result(runtime, action, false, 0, "vertex merge requires a target vertex");
        return true;
    }

    editor_source_vertex_move_target targets[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(targets);
    int target_count = 0;
    char error[256];
    SDL_zeroa(error);
    if (!editor_collect_vertex_merge_move_targets(runtime, target, targets, SDL_arraysize(targets), &target_count,
                                                  error, sizeof(error)))
    {
        publish_editor_vertex_merge_result(runtime, action, false, 0,
                                           error[0] != '\0' ? error : "vertex merge requires selected vertices");
        return true;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, target->world_name);
    if (world_runtime == NULL || target->source_index < 0 ||
        target->source_index >= world_runtime->editor_source_box_count)
    {
        publish_editor_vertex_merge_result(runtime, action, false, 0, "vertex merge source brush not found");
        return true;
    }

    int changed_count = 0;
    if (!editor_apply_vertex_move_targets(targets, target_count, &changed_count, error, sizeof(error)))
    {
        char message[320];
        SDL_snprintf(message, sizeof(message), "vertex merge blocked: %s",
                     error[0] != '\0' ? error : "invalid source edit");
        publish_editor_vertex_merge_result(runtime, action, false, 0, message);
        return true;
    }

    editor_mark_dirty_and_refresh_after_vertex_targets(runtime, targets, target_count);
    clear_editor_selected_vertices(runtime);
    (void)editor_add_world_vertex_selection_group(runtime, world_runtime, target->coord);
    publish_editor_selected_vertex_count(runtime);

    char message[160];
    SDL_snprintf(message, sizeof(message), "merged %d selected source vert%s", changed_count,
                 changed_count == 1 ? "ex" : "ices");
    publish_editor_vertex_merge_result(runtime, action, true, changed_count, message);
    return true;
}

static bool editor_vertex_add_read_coord(const brush_world_runtime *world_runtime, yyjson_val *action, int out_coord[3])
{
    if (world_runtime == NULL || action == NULL || out_coord == NULL)
        return false;

    yyjson_val *coord = obj_get(action, "coord");
    if (yyjson_is_arr(coord) && yyjson_arr_size(coord) == 3)
    {
        for (size_t i = 0; i < 3; ++i)
        {
            yyjson_val *value = yyjson_arr_get(coord, i);
            if (!yyjson_is_int(value))
                return false;
            out_coord[i] = (int)yyjson_get_int(value);
        }
        return true;
    }

    yyjson_val *position = obj_get(action, "position");
    if (yyjson_is_arr(position) && yyjson_arr_size(position) >= 3)
    {
        yyjson_val *x = yyjson_arr_get(position, 0);
        yyjson_val *y = yyjson_arr_get(position, 1);
        yyjson_val *z = yyjson_arr_get(position, 2);
        if (!yyjson_is_num(x) || !yyjson_is_num(y) || !yyjson_is_num(z))
            return false;
        out_coord[0] = editor_source_units_from_meters(world_runtime, (float)yyjson_get_num(x));
        out_coord[1] = editor_source_units_from_meters(world_runtime, (float)yyjson_get_num(y));
        out_coord[2] = editor_source_units_from_meters(world_runtime, (float)yyjson_get_num(z));
        return true;
    }
    return false;
}

static bool editor_vertex_source_from_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                             brush_world_runtime **out_world_runtime, int *out_source_index)
{
    if (out_world_runtime != NULL)
        *out_world_runtime = NULL;
    if (out_source_index != NULL)
        *out_source_index = -1;
    if (runtime == NULL || out_world_runtime == NULL || out_source_index == NULL)
        return false;

    const char *world_name = json_string(action, "world", NULL);
    if ((world_name == NULL || world_name[0] == '\0') && editor_selected_vertices_active_for_scene(runtime) &&
        runtime->editor_selected_vertex_count > 0)
    {
        world_name = runtime->editor_selected_vertices[0].world_name;
    }
    if ((world_name == NULL || world_name[0] == '\0') && editor_selected_brushes_active_for_scene(runtime) &&
        runtime->editor_selected_brush_count > 0)
    {
        world_name = runtime->editor_selected_brushes[0].world_name;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    if (world_runtime == NULL)
        return false;

    int source_index = -1;
    const char *brush_identity = json_string(action, "brush_stable_id", NULL);
    if (brush_identity == NULL || brush_identity[0] == '\0')
        brush_identity = json_string(action, "brush", NULL);
    if (brush_identity != NULL && brush_identity[0] != '\0')
        source_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
    if (source_index < 0 && editor_selected_vertices_active_for_scene(runtime) &&
        runtime->editor_selected_vertex_count > 0 &&
        SDL_strcmp(runtime->editor_selected_vertices[0].world_name, world_runtime->desc.name) == 0)
    {
        source_index = runtime->editor_selected_vertices[0].source_index;
    }
    if (source_index < 0 && editor_selected_brushes_active_for_scene(runtime) &&
        runtime->editor_selected_brush_count > 0 &&
        SDL_strcmp(runtime->editor_selected_brushes[0].world_name, world_runtime->desc.name) == 0)
    {
        const slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[0]);
        source_index = editor_source_index_for_selection(world_runtime, &selection);
    }
    if (source_index < 0 || source_index >= world_runtime->editor_source_box_count)
        return false;

    *out_world_runtime = world_runtime;
    *out_source_index = source_index;
    return true;
}

static void publish_editor_vertex_add_result(slayer3d_game_data_runtime *runtime, yyjson_val *action, bool valid,
                                             int vertex_count, int added_count, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.add.valid", valid);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.add.vertex_count", vertex_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.add.added_count", added_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.add.message", message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");

    yyjson_val *outputs = obj_get(action, "outputs");
    if (!yyjson_is_obj(outputs))
        return;
    const char *valid_key = json_string(outputs, "valid_key", NULL);
    const char *message_key = json_string(outputs, "message_key", NULL);
    const char *vertex_count_key = json_string(outputs, "vertex_count_key", NULL);
    const char *added_count_key = json_string(outputs, "added_count_key", NULL);
    if (valid_key != NULL)
        slayer3d_properties_set_bool(runtime->scene_state, valid_key, valid);
    if (message_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, message_key, message != NULL ? message : "");
    if (vertex_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, vertex_count_key, vertex_count);
    if (added_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, added_count_key, added_count);
}

static slayer3d_vec3 editor_source_coord_to_world_position(const brush_world_runtime *world_runtime,
                                                           const slayer3d_game_data_editor_selection *selection,
                                                           const int coord[3])
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    const slayer3d_vec3 local =
        coord != NULL ? slayer3d_vec3_make((float)coord[0] * meters_per_unit, (float)coord[1] * meters_per_unit,
                                           (float)coord[2] * meters_per_unit)
                      : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const slayer3d_vec3 world_position =
        selection != NULL ? selection->world_position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    return slayer3d_vec3_add(world_position, local);
}

static void publish_editor_vertex_add_preview(slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_editor_selection *selection, const int coord[3],
                                              bool active, bool valid, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const brush_world_runtime *world_runtime =
        selection != NULL ? find_brush_world_runtime(runtime, selection->world_name) : NULL;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.add.preview.active", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.add.preview.valid", active ? valid : false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.add.preview.world",
                                   active && selection != NULL && selection->world_name != NULL ? selection->world_name
                                                                                                : "");
    slayer3d_properties_set_string(
        runtime->scene_state, "editor.vertex.add.preview.brush",
        active && selection != NULL && selection->element_name != NULL ? selection->element_name : "");
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.add.preview.x",
                                active && coord != NULL ? coord[0] : 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.add.preview.y",
                                active && coord != NULL ? coord[1] : 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.add.preview.z",
                                active && coord != NULL ? coord[2] : 0);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.add.preview.position",
                                 active ? editor_source_coord_to_world_position(world_runtime, selection, coord)
                                        : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.add.preview.message",
                                   active && message != NULL ? message : "");
}

static bool editor_preview_add_vertex_to_source_coord(brush_world_runtime *world, int source_index, const int coord[3],
                                                      int *out_vertex_count, char *message, size_t message_size)
{
    if (out_vertex_count != NULL)
        *out_vertex_count = 0;
    if (message != NULL && message_size > 0)
        message[0] = '\0';
    if (world == NULL || coord == NULL || source_index < 0 || source_index >= world->editor_source_box_count)
    {
        if (message != NULL && message_size > 0)
            SDL_strlcpy(message, "vertex add requires a source brush", message_size);
        return false;
    }

    const editor_brush_source_box_runtime *box = &world->editor_source_boxes[source_index];
    const char *identity = box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;
    editor_brush_source_vertex_operation_desc desc;
    SDL_zero(desc);
    desc.brush_identity = identity;
    desc.type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_ADD;
    for (int axis = 0; axis < 3; ++axis)
        desc.coord[axis] = coord[axis];

    char error[256];
    SDL_zeroa(error);
    editor_brush_source_vertex_operation_result preview;
    SDL_zero(preview);
    const bool valid = editor_brush_world_preview_source_vertex_operation(world, &desc, &preview, error, sizeof(error));
    if (out_vertex_count != NULL)
        *out_vertex_count = preview.vertex_count;
    if (message != NULL && message_size > 0)
        SDL_strlcpy(message, valid ? "vertex add preview" : (error[0] != '\0' ? error : "invalid source edit"),
                    message_size);
    editor_brush_source_free_runtime_brush(&preview.brush);
    return valid;
}

static bool editor_add_vertex_to_source_coord(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              brush_world_runtime *world_runtime, int source_index, const int coord[3])
{
    if (runtime == NULL || world_runtime == NULL || coord == NULL || source_index < 0 ||
        source_index >= world_runtime->editor_source_box_count)
    {
        publish_editor_vertex_add_result(runtime, action, false, 0, 0, "vertex add requires a source brush");
        return true;
    }

    const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    const char *identity = box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;
    editor_brush_source_vertex_operation_desc desc;
    SDL_zero(desc);
    desc.brush_identity = identity;
    desc.type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_ADD;
    for (int axis = 0; axis < 3; ++axis)
        desc.coord[axis] = coord[axis];

    char error[256];
    SDL_zeroa(error);
    editor_brush_source_vertex_operation_result result;
    SDL_zero(result);
    if (!editor_brush_world_apply_source_vertex_operation(world_runtime, &desc, &result, error, sizeof(error)))
    {
        char message[320];
        SDL_snprintf(message, sizeof(message), "vertex add blocked: %s",
                     error[0] != '\0' ? error : "invalid source edit");
        publish_editor_vertex_add_result(runtime, action, false, 0, 0, message);
        editor_brush_source_free_runtime_brush(&result.brush);
        return true;
    }
    const int vertex_count = result.vertex_count;
    editor_brush_source_free_runtime_brush(&result.brush);

    editor_brush_source_vertex_model model;
    if (editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
    {
        clear_editor_selected_vertices(runtime);
        for (int i = 0; i < model.vertex_count; ++i)
        {
            if (model.vertices[i].coord[0] != coord[0] || model.vertices[i].coord[1] != coord[1] ||
                model.vertices[i].coord[2] != coord[2])
            {
                continue;
            }
            editor_source_vertex_selection selection;
            if (editor_source_vertex_selection_from_model(world_runtime, &model, i, &selection))
                (void)add_editor_selected_vertex(runtime, &selection);
            break;
        }
        publish_editor_selected_vertex_count(runtime);
    }
    editor_refresh_selected_brushes_after_source_edit(runtime);

    char message[160];
    SDL_snprintf(message, sizeof(message), "added source vertex");
    publish_editor_vertex_add_result(runtime, action, true, vertex_count, 1, message);
    return true;
}

bool slayer3d_game_data_add_editor_vertex_to_source(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    brush_world_runtime *world_runtime = NULL;
    int source_index = -1;
    if (!editor_vertex_source_from_action(runtime, action, &world_runtime, &source_index))
    {
        publish_editor_vertex_add_result(runtime, action, false, 0, 0, "vertex add requires a source brush");
        return true;
    }

    int coord[3] = {0, 0, 0};
    if (!editor_vertex_add_read_coord(world_runtime, action, coord))
    {
        publish_editor_vertex_add_result(runtime, action, false, 0, 0, "vertex add requires a coordinate");
        return true;
    }

    return editor_add_vertex_to_source_coord(runtime, action, world_runtime, source_index, coord);
}

static brush_world_runtime *editor_vertex_diagnostics_world_from_action(slayer3d_game_data_runtime *runtime,
                                                                        yyjson_val *action)
{
    if (runtime == NULL)
        return NULL;

    const char *world_name = json_string(action, "world", NULL);
    if ((world_name == NULL || world_name[0] == '\0') && editor_selected_vertices_active_for_scene(runtime) &&
        runtime->editor_selected_vertex_count > 0)
    {
        world_name = runtime->editor_selected_vertices[0].world_name;
    }
    if ((world_name == NULL || world_name[0] == '\0') && editor_selected_brushes_active_for_scene(runtime) &&
        runtime->editor_selected_brush_count > 0)
    {
        world_name = runtime->editor_selected_brushes[0].world_name;
    }
    if (world_name != NULL && world_name[0] != '\0')
        return find_brush_world_runtime_mutable(runtime, world_name);

    for (int i = 0; i < runtime->brush_world_count; ++i)
    {
        if (runtime->brush_worlds[i].editor_has_source_model)
            return &runtime->brush_worlds[i];
    }
    return NULL;
}

static int editor_collect_vertex_diagnostic_targets(slayer3d_game_data_runtime *runtime,
                                                    brush_world_runtime *world_runtime,
                                                    editor_source_snap_target *targets, int target_capacity)
{
    if (runtime == NULL || world_runtime == NULL || targets == NULL || target_capacity <= 0)
        return 0;

    int target_count = 0;
    if (editor_selected_vertices_active_for_scene(runtime))
    {
        for (int i = 0; i < runtime->editor_selected_vertex_count && target_count < target_capacity; ++i)
        {
            const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
            if (SDL_strcmp(selection->world_name, world_runtime->desc.name) == 0)
                (void)editor_add_source_snap_target(targets, &target_count, world_runtime, selection->source_index);
        }
    }
    if (target_count > 0)
        return target_count;

    if (editor_selected_brushes_active_for_scene(runtime))
    {
        for (int i = 0; i < runtime->editor_selected_brush_count && target_count < target_capacity; ++i)
        {
            const slayer3d_game_data_editor_selection selection =
                resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
            if (SDL_strcmp(selection.world_name, world_runtime->desc.name) != 0)
                continue;
            const int source_index = editor_source_index_for_selection(world_runtime, &selection);
            (void)editor_add_source_snap_target(targets, &target_count, world_runtime, source_index);
        }
    }
    return target_count;
}

static void publish_editor_vertex_diagnostics_result(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                     const brush_world_runtime *world_runtime,
                                                     const editor_brush_source_vertex_diagnostics *diagnostics,
                                                     const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL || diagnostics == NULL)
        return;

    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.diagnostics.valid", diagnostics->valid);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.diagnostics.message",
                                   message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.diagnostics.world",
                                   world_runtime != NULL && world_runtime->desc.name != NULL ? world_runtime->desc.name
                                                                                             : "");
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.brush_count",
                                diagnostics->brush_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.vertex_count",
                                diagnostics->vertex_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.edge_count", diagnostics->edge_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.face_count", diagnostics->face_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.shared_vertex_count",
                                diagnostics->shared_vertex_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.off_snap_count",
                                diagnostics->off_snap_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.degenerate_count",
                                diagnostics->degenerate_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.concave_count",
                                diagnostics->concave_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.non_finite_count",
                                diagnostics->non_finite_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.diagnostics.first_issue",
                                   diagnostics->first_issue);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.diagnostics.first_issue_stable_id",
                                   diagnostics->first_issue_stable_id);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");

    yyjson_val *outputs = obj_get(action, "outputs");
    if (!yyjson_is_obj(outputs))
        return;
    const char *valid_key = json_string(outputs, "valid_key", NULL);
    const char *message_key = json_string(outputs, "message_key", NULL);
    const char *world_key = json_string(outputs, "world_key", NULL);
    const char *brush_count_key = json_string(outputs, "brush_count_key", NULL);
    const char *vertex_count_key = json_string(outputs, "vertex_count_key", NULL);
    const char *edge_count_key = json_string(outputs, "edge_count_key", NULL);
    const char *face_count_key = json_string(outputs, "face_count_key", NULL);
    const char *shared_vertex_count_key = json_string(outputs, "shared_vertex_count_key", NULL);
    const char *off_snap_count_key = json_string(outputs, "off_snap_count_key", NULL);
    const char *degenerate_count_key = json_string(outputs, "degenerate_count_key", NULL);
    const char *concave_count_key = json_string(outputs, "concave_count_key", NULL);
    const char *non_finite_count_key = json_string(outputs, "non_finite_count_key", NULL);
    const char *first_issue_key = json_string(outputs, "first_issue_key", NULL);
    const char *first_issue_stable_id_key = json_string(outputs, "first_issue_stable_id_key", NULL);

    if (valid_key != NULL)
        slayer3d_properties_set_bool(runtime->scene_state, valid_key, diagnostics->valid);
    if (message_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, message_key, message != NULL ? message : "");
    if (world_key != NULL)
        slayer3d_properties_set_string(
            runtime->scene_state, world_key,
            world_runtime != NULL && world_runtime->desc.name != NULL ? world_runtime->desc.name : "");
    if (brush_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, brush_count_key, diagnostics->brush_count);
    if (vertex_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, vertex_count_key, diagnostics->vertex_count);
    if (edge_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, edge_count_key, diagnostics->edge_count);
    if (face_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, face_count_key, diagnostics->face_count);
    if (shared_vertex_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, shared_vertex_count_key, diagnostics->shared_vertex_count);
    if (off_snap_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, off_snap_count_key, diagnostics->off_snap_count);
    if (degenerate_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, degenerate_count_key, diagnostics->degenerate_count);
    if (concave_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, concave_count_key, diagnostics->concave_count);
    if (non_finite_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, non_finite_count_key, diagnostics->non_finite_count);
    if (first_issue_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, first_issue_key, diagnostics->first_issue);
    if (first_issue_stable_id_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, first_issue_stable_id_key,
                                       diagnostics->first_issue_stable_id);
}

bool slayer3d_game_data_validate_editor_vertex_source(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    editor_brush_source_vertex_diagnostics diagnostics;
    SDL_zero(diagnostics);

    brush_world_runtime *world_runtime = editor_vertex_diagnostics_world_from_action(runtime, action);
    if (runtime == NULL || world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        SDL_strlcpy(diagnostics.first_issue, "source vertex diagnostics require an editable brush world",
                    sizeof(diagnostics.first_issue));
        publish_editor_vertex_diagnostics_result(runtime, action, world_runtime, &diagnostics, diagnostics.first_issue);
        return true;
    }

    int explicit_source_index = -1;
    const char *brush_identity = json_string(action, "brush_stable_id", NULL);
    if (brush_identity == NULL || brush_identity[0] == '\0')
        brush_identity = json_string(action, "brush", NULL);
    if (brush_identity != NULL && brush_identity[0] != '\0')
    {
        explicit_source_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
        if (explicit_source_index < 0)
        {
            SDL_strlcpy(diagnostics.first_issue, "source brush not found", sizeof(diagnostics.first_issue));
            publish_editor_vertex_diagnostics_result(runtime, action, world_runtime, &diagnostics,
                                                     diagnostics.first_issue);
            return true;
        }
    }

    editor_source_snap_target targets[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(targets);
    int source_indices[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(source_indices);
    int source_index_count = 0;
    if (explicit_source_index >= 0)
    {
        source_indices[source_index_count++] = explicit_source_index;
    }
    else
    {
        source_index_count =
            editor_collect_vertex_diagnostic_targets(runtime, world_runtime, targets, SDL_arraysize(targets));
        for (int i = 0; i < source_index_count; ++i)
            source_indices[i] = targets[i].source_index;
    }

    char error[256];
    SDL_zeroa(error);
    const int *indices = source_index_count > 0 ? source_indices : NULL;
    const int index_count = source_index_count > 0 ? source_index_count : 0;
    if (!editor_brush_world_validate_source_vertex_model(world_runtime, indices, index_count, &diagnostics, error,
                                                         sizeof(error)))
    {
        if (diagnostics.first_issue[0] == '\0')
            SDL_strlcpy(diagnostics.first_issue, error[0] != '\0' ? error : "source vertex diagnostics failed",
                        sizeof(diagnostics.first_issue));
    }

    const char *message = diagnostics.valid                    ? "source vertex model valid"
                          : diagnostics.first_issue[0] != '\0' ? diagnostics.first_issue
                                                               : "source vertex model invalid";
    publish_editor_vertex_diagnostics_result(runtime, action, world_runtime, &diagnostics, message);
    return true;
}

static void editor_capture_vertex_drag_origins(slayer3d_game_data_runtime *runtime, editor_drag_move_state *drag)
{
    if (runtime == NULL || drag == NULL || !editor_selected_vertices_active_for_scene(runtime))
        return;

    const int count = SDL_min(runtime->editor_selected_vertex_count, SLAYER3D_EDITOR_DRAG_VERTEX_ORIGIN_CAPACITY);
    drag->vertex_origin_count = count;
    for (int i = 0; i < count; ++i)
    {
        const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        editor_drag_vertex_origin *origin = &drag->vertex_origins[i];
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
}

static void editor_source_vertex_selection_from_drag_origin(const editor_drag_vertex_origin *origin,
                                                            editor_source_vertex_selection *selection)
{
    if (origin == NULL || selection == NULL)
        return;
    init_editor_source_vertex_selection(selection);
    SDL_strlcpy(selection->world_name, origin->world_name, sizeof(selection->world_name));
    SDL_strlcpy(selection->brush_name, origin->brush_name, sizeof(selection->brush_name));
    SDL_strlcpy(selection->brush_stable_id, origin->brush_stable_id, sizeof(selection->brush_stable_id));
    SDL_strlcpy(selection->vertex_stable_id, origin->vertex_stable_id, sizeof(selection->vertex_stable_id));
    selection->source_index = origin->source_index;
    selection->vertex_index = origin->vertex_index;
    selection->coord[0] = origin->coord[0];
    selection->coord[1] = origin->coord[1];
    selection->coord[2] = origin->coord[2];
}

void editor_begin_vertex_drag(slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                              const slayer3d_game_data_editor_selection *hover_selection)
{
    if (runtime == NULL || input == NULL || hover_selection == NULL)
        return;
    editor_drag_move_state *drag = &runtime->editor_drag_move;
    SDL_zero(*drag);
    drag->active = true;
    drag->scene = slayer3d_game_data_active_scene(runtime);
    drag->grid_size = slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f);
    drag->start_point = hover_selection->hit ? hover_selection->point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    drag->vertex_drag = true;
    editor_capture_vertex_drag_origins(runtime, drag);
    (void)slayer3d_input_get_mouse_position(input, &drag->start_mouse_x, &drag->start_mouse_y);
    drag->current_mouse_x = drag->start_mouse_x;
    drag->current_mouse_y = drag->start_mouse_y;
    publish_editor_vertex_drag_state(runtime, drag);
}

bool editor_handle_vertex_drag(slayer3d_game_data_runtime *runtime,
                               const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_vertex(runtime))
        return true;

    editor_drag_move_state *drag = &runtime->editor_drag_move;
    if (!drag->active || drag->face_resize || drag->vertex_lasso)
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return true;
    if (out_consumed != NULL)
        *out_consumed = true;

    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    float mouse_x = drag->current_mouse_x;
    float mouse_y = drag->current_mouse_y;
    (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);
    drag->current_mouse_x = mouse_x;
    drag->current_mouse_y = mouse_y;
    if (left_down)
    {
        const SDL_Keymod modifiers = SDL_GetModState();
        const bool y_axis_lock = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0;
        const bool dominant_axis_lock = !y_axis_lock && (modifiers & SDL_KMOD_SHIFT) != 0;
        drag->axis_lock_y = y_axis_lock;
        drag->axis_lock_dominant = dominant_axis_lock;
        slayer3d_vec3 desired = drag->applied_offset;
        if (y_axis_lock)
        {
            const float units_per_pixel = SDL_max(drag->grid_size, 0.001f) / 48.0f;
            desired = slayer3d_vec3_make(
                0.0f, editor_snap_delta((drag->start_mouse_y - mouse_y) * units_per_pixel, drag->grid_size), 0.0f);
        }
        else if (hover_selection != NULL && hover_selection->hit)
        {
            desired = slayer3d_vec3_make(
                editor_snap_delta(hover_selection->point.x - drag->start_point.x, drag->grid_size), 0.0f,
                editor_snap_delta(hover_selection->point.z - drag->start_point.z, drag->grid_size));
        }
        if (dominant_axis_lock)
            desired = editor_lock_offset_to_dominant_axis(desired);

        const slayer3d_vec3 incremental = slayer3d_vec3_sub(desired, drag->applied_offset);
        if (slayer3d_vec3_length_squared(incremental) > 0.0000001f &&
            editor_translate_selected_vertices(runtime, incremental))
        {
            drag->applied_offset = desired;
            drag->moved = true;
        }
        publish_editor_vertex_drag_state(runtime, drag);
    }

    if (left_released || !left_down)
    {
        const float dx = drag->current_mouse_x - drag->start_mouse_x;
        const float dy = drag->current_mouse_y - drag->start_mouse_y;
        const bool meaningful_drag = dx * dx + dy * dy >= 16.0f;
        if (drag->vertex_toggle_on_click && !meaningful_drag && !drag->moved)
        {
            editor_source_vertex_selection toggle_selection;
            editor_source_vertex_selection_from_drag_origin(&drag->vertex_toggle_origin, &toggle_selection);
            if (editor_remove_shared_vertex_selection_group(runtime, &toggle_selection))
            {
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "vertex deselected");
                publish_editor_selected_vertex_count(runtime);
            }
        }
        clear_editor_drag_move(runtime);
    }
    return true;
}

bool slayer3d_game_data_clear_editor_vertex_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    clear_editor_selected_vertices(runtime);
    publish_editor_selected_vertex_count(runtime);
    return true;
}

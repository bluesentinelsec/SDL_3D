#ifndef SLAYER3D_GAME_DATA_EDITOR_OBJECT_STATE_H
#define SLAYER3D_GAME_DATA_EDITOR_OBJECT_STATE_H

#ifndef SLAYER3D_GAME_DATA_INTERNAL_H
#error "game_data_editor_object_state.h is included by game_data_internal.h"
#endif

/*
 * Shared lookup and iteration helpers for editor-only object state
 * (lock/visibility flags on brush source boxes and Things).
 *
 * These helpers intentionally read the active selection directly from
 * runtime->editor_active_selection instead of routing through
 * slayer3d_game_data_get_active_editor_selection. The public getter resolves
 * brush metadata against source models that mutation code may be rebuilding,
 * which can surface stale selection state.
 */

typedef enum editor_object_state_flag
{
    EDITOR_OBJECT_STATE_LOCKED,
    EDITOR_OBJECT_STATE_HIDDEN,
} editor_object_state_flag;

bool editor_selection_is_brush(const slayer3d_game_data_editor_selection *selection);
bool editor_selection_is_actor(const slayer3d_game_data_editor_selection *selection);
const char *editor_selection_brush_identity(const slayer3d_game_data_editor_selection *selection);

const slayer3d_game_data_editor_selection *editor_runtime_active_selection_direct(
    const slayer3d_game_data_runtime *runtime);

editor_actor_runtime *editor_runtime_find_actor(const slayer3d_game_data_runtime *runtime, const char *name);
editor_actor_runtime *editor_runtime_active_selected_actor(const slayer3d_game_data_runtime *runtime);

editor_brush_source_box_runtime *editor_runtime_find_source_box_for_selection(
    slayer3d_game_data_runtime *runtime, const slayer3d_game_data_editor_selection *selection, int *out_world_index);

int editor_runtime_count_objects_with_state(const slayer3d_game_data_runtime *runtime, editor_object_state_flag flag);

bool editor_runtime_rebuild_changed_brush_worlds(slayer3d_game_data_runtime *runtime, const bool *changed_worlds);

#endif

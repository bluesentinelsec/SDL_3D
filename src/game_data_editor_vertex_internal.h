#ifndef SLAYER3D_GAME_DATA_EDITOR_VERTEX_INTERNAL_H
#define SLAYER3D_GAME_DATA_EDITOR_VERTEX_INTERNAL_H

/*
 * Shared internals for the editor vertex tool.
 *
 * The public editor module entry points remain in game_data_editor_internal.h.
 * This header is intentionally private to the vertex-tool implementation so
 * selection, lasso, drag, and source-mutation code can live in focused source
 * files without exposing these helpers to unrelated editor subsystems.
 */

#include "game_data_internal.h"

void publish_editor_vertex_drag_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag);
void publish_editor_vertex_move_result(slayer3d_game_data_runtime *runtime, bool valid, int source_count,
                                       int changed_count, const char *message);
void clear_editor_drag_move(slayer3d_game_data_runtime *runtime);

const char *editor_metadata_stable_id(const slayer3d_game_data_editor_metadata *metadata);
int editor_source_index_for_selection(const brush_world_runtime *world_runtime,
                                      const slayer3d_game_data_editor_selection *selection);
slayer3d_vec3 editor_source_vertex_meters(const brush_world_runtime *world_runtime,
                                          const editor_brush_source_vertex *vertex);
int editor_source_units_from_meters(const brush_world_runtime *world_runtime, float meters);
int editor_collect_selected_source_indices(const slayer3d_game_data_runtime *runtime,
                                           const brush_world_runtime *world_runtime, int fallback_source_index,
                                           int *out_indices, int out_capacity);
int editor_source_vertex_shared_count(const brush_world_runtime *world_runtime, const int coord[3],
                                      const int *selected_indices, int selected_count);
float editor_vertex_handle_pick_radius(const slayer3d_game_data_runtime *runtime);

int editor_selected_vertex_index(const slayer3d_game_data_runtime *runtime,
                                 const editor_source_vertex_selection *selection);
void remove_editor_selected_vertex_at(slayer3d_game_data_runtime *runtime, int index);
bool add_editor_selected_vertex(slayer3d_game_data_runtime *runtime, const editor_source_vertex_selection *selection);
bool editor_coord_equal(const int a[3], const int b[3]);
bool editor_source_vertex_selection_from_model(const brush_world_runtime *world_runtime,
                                               const editor_brush_source_vertex_model *model, int vertex_index,
                                               editor_source_vertex_selection *out_selection);
bool editor_add_shared_vertex_selection_group(slayer3d_game_data_runtime *runtime,
                                              const editor_source_vertex_selection *selection);
bool editor_remove_shared_vertex_selection_group(slayer3d_game_data_runtime *runtime,
                                                 const editor_source_vertex_selection *selection);

bool editor_hover_source_vertex_selection_with_distance(const slayer3d_game_data_runtime *runtime,
                                                        const slayer3d_game_data_editor_selection *hover_selection,
                                                        editor_source_vertex_selection *out_selection,
                                                        float *out_distance_sq);
bool editor_hover_source_vertex_selection(const slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_editor_selection *hover_selection,
                                          editor_source_vertex_selection *out_selection);

void editor_begin_vertex_drag(slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                              const slayer3d_game_data_editor_selection *hover_selection);
bool editor_merge_selected_vertices_to_target(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              const editor_source_vertex_selection *target);

#endif

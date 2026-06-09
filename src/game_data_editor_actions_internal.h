#ifndef SLAYER3D_GAME_DATA_EDITOR_ACTIONS_INTERNAL_H
#define SLAYER3D_GAME_DATA_EDITOR_ACTIONS_INTERNAL_H

#ifndef SLAYER3D_GAME_DATA_INTERNAL_H
#error "game_data_editor_actions_internal.h is included by game_data_internal.h"
#endif

bool slayer3d_game_data_clear_active_editor_selection(slayer3d_game_data_runtime *runtime);
bool slayer3d_game_data_clear_editor_vertex_selection(slayer3d_game_data_runtime *runtime);
bool slayer3d_game_data_clear_editor_edge_selection(slayer3d_game_data_runtime *runtime);
bool slayer3d_game_data_set_editor_tool_mode(slayer3d_game_data_runtime *runtime, const char *mode,
                                             const char *message_override);
bool slayer3d_game_data_enter_editor_clip_tool(slayer3d_game_data_runtime *runtime, const char *message_override);
bool slayer3d_game_data_cancel_editor_clip_tool(slayer3d_game_data_runtime *runtime, const char *message_override);
bool slayer3d_game_data_escape_editor_clip_tool(slayer3d_game_data_runtime *runtime);
bool slayer3d_game_data_cycle_editor_clip_keep_mode(slayer3d_game_data_runtime *runtime);
bool slayer3d_game_data_commit_editor_clip_tool(slayer3d_game_data_runtime *runtime);
void reset_editor_clip_tool_state(slayer3d_game_data_runtime *runtime, const char *message);
void reset_editor_rotate_tool_state(slayer3d_game_data_runtime *runtime, const char *message);
bool slayer3d_game_data_place_editor_clip_point_source(slayer3d_game_data_runtime *runtime, const int coord[3],
                                                       slayer3d_vec3 work_plane_normal);
bool slayer3d_game_data_move_editor_clip_point_source(slayer3d_game_data_runtime *runtime, int point_index,
                                                      const int coord[3], slayer3d_vec3 work_plane_normal);
slayer3d_game_data_editor_selection resolved_editor_selection(const slayer3d_game_data_runtime *runtime,
                                                              const slayer3d_game_data_editor_selection *selection);
slayer3d_properties *slayer3d_game_data_create_editor_selection_payload(
    const slayer3d_game_data_editor_selection *selection);
bool slayer3d_game_data_delete_selected_editor_brushes(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                       const slayer3d_properties *payload);
bool slayer3d_game_data_resize_selected_editor_brushes_y(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                         const slayer3d_properties *payload);
bool slayer3d_game_data_snap_selected_editor_vertices(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_delete_selected_editor_vertices(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_merge_selected_editor_vertices_to_hover(slayer3d_game_data_runtime *runtime,
                                                                yyjson_val *action);
bool slayer3d_game_data_add_editor_vertex_to_source(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_validate_editor_vertex_source(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_preview_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_clear_editor_command_preview(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_commit_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              const slayer3d_properties *payload);
bool slayer3d_game_data_undo_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload);
bool slayer3d_game_data_redo_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload);
bool slayer3d_game_data_export_editor_brush_world_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_export_editor_level_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_save_editor_level_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_load_editor_level_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_prepare_editor_test_run_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_save_editor_test_run_manifest_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_publish_editor_brush_world_status_action(slayer3d_game_data_runtime *runtime,
                                                                 yyjson_val *action);
bool slayer3d_game_data_create_box_brush_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_place_editor_player_start_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_apply_editor_player_start_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_delete_editor_player_start_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool publish_editor_brush_world_status(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, const char *world_name,
                                       const char *message, bool publish_result);
bool editor_brush_world_generate_brush_name(const brush_world_runtime *world_runtime, char *buffer, size_t buffer_size);
void free_brush_world_runtime(brush_world_runtime *world_runtime);
void free_editor_player_starts_runtime(slayer3d_game_data_runtime *runtime);
void free_editor_command_history(editor_command_history_state *history);
slayer3d_bounding_box editor_resized_preview_bounds(slayer3d_bounding_box bounds, slayer3d_vec3 normal, float distance);
void publish_editor_command_preview(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, bool valid,
                                    const char *command, const char *target, const char *message,
                                    const slayer3d_game_data_editor_selection *selection,
                                    const slayer3d_bounding_box *bounds);
bool compile_brush_world_visibility_grid(brush_world_runtime *world_runtime);
void free_brush_world_visibility_grid(brush_world_runtime *world_runtime);
bool rebuild_brush_world_runtime_artifacts(brush_world_runtime *world_runtime, char *error_buffer,
                                           int error_buffer_size);
void free_editor_brush_source_model(brush_world_runtime *world_runtime);
void free_editor_brush_source_box_runtime(editor_brush_source_box_runtime *box);
bool copy_editor_brush_source_box_runtime(const editor_brush_source_box_runtime *source,
                                          editor_brush_source_box_runtime *dest);
bool load_editor_brush_source_boxes(brush_world_runtime *world_runtime, yyjson_val *boxes, float meters_per_unit,
                                    int snap_units, char *error_buffer, int error_buffer_size);
bool editor_brush_world_rebuild_from_source(brush_world_runtime *world_runtime, char *error_buffer,
                                            int error_buffer_size);
int editor_brush_world_find_source_box_index(const brush_world_runtime *world_runtime, const char *brush_identity);
bool editor_brush_world_copy_source_box_by_identity(const brush_world_runtime *world_runtime,
                                                    const char *brush_identity,
                                                    editor_brush_source_box_runtime *out_box, int *out_index,
                                                    char *error_buffer, int error_buffer_size);
bool editor_brush_source_box_from_create_desc(const brush_world_runtime *world_runtime,
                                              const slayer3d_game_data_create_box_brush_desc *desc,
                                              const char *brush_name, editor_brush_source_box_runtime *out_box);
slayer3d_bounding_box editor_brush_source_box_bounds_meters(const brush_world_runtime *world_runtime,
                                                            const editor_brush_source_box_runtime *box);
bool editor_brush_world_validate_source_box_candidate(const brush_world_runtime *world_runtime,
                                                      const editor_brush_source_box_runtime *box, int exclude_index,
                                                      char *error_buffer, int error_buffer_size);
bool editor_brush_world_preview_source_box_create(const brush_world_runtime *world_runtime,
                                                  const editor_brush_source_box_runtime *box, int minimum_extent_units,
                                                  editor_brush_source_prefab_result *out_result, char *error_buffer,
                                                  int error_buffer_size);
bool editor_brush_world_apply_source_box_create(brush_world_runtime *world_runtime,
                                                const editor_brush_source_box_runtime *box, int minimum_extent_units,
                                                editor_brush_source_prefab_result *out_result, char *error_buffer,
                                                int error_buffer_size);
bool editor_brush_world_run_source_prefab_command(brush_world_runtime *world_runtime,
                                                  const editor_brush_source_prefab_desc *desc, const char *brush_name,
                                                  const int *source_min, const int *source_max, bool apply,
                                                  editor_brush_source_prefab_result *out_result, char *error_buffer,
                                                  int error_buffer_size);
bool editor_brush_world_insert_source_box_at_index(brush_world_runtime *world_runtime, int box_index,
                                                   const editor_brush_source_box_runtime *box, char *error_buffer,
                                                   int error_buffer_size);
bool editor_brush_world_insert_source_box_from_brush(brush_world_runtime *world_runtime, int box_index,
                                                     const slayer3d_game_data_brush *brush, char *error_buffer,
                                                     int error_buffer_size);
bool editor_brush_world_remove_source_box_at_index(brush_world_runtime *world_runtime, int box_index,
                                                   char *error_buffer, int error_buffer_size);
bool editor_brush_world_translate_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                             slayer3d_vec3 offset, char *error_buffer, int error_buffer_size);
bool editor_brush_world_rotate_source_box_y_quarter_turns(brush_world_runtime *world_runtime, const char *brush_name,
                                                          int quarter_turns, char *error_buffer, int error_buffer_size);
bool editor_brush_world_rotate_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                          slayer3d_vec3 pivot, slayer3d_vec3 axis, float angle_radians,
                                          char *error_buffer, int error_buffer_size);
bool editor_brush_world_scale_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                         slayer3d_vec3 anchor, slayer3d_vec3 factors, char *error_buffer,
                                         int error_buffer_size);
bool editor_brush_world_mirror_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                          slayer3d_vec3 plane_point, slayer3d_vec3 plane_normal, char *error_buffer,
                                          int error_buffer_size);
bool editor_brush_world_shear_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                         slayer3d_bounding_box bounds, slayer3d_vec3 side_normal, slayer3d_vec3 delta,
                                         char *error_buffer, int error_buffer_size);
int editor_brush_world_source_box_face_index_for_identity(const brush_world_runtime *world_runtime,
                                                          const char *brush_identity, int fallback_face_index,
                                                          const char *face_identity);
bool editor_brush_world_source_box_face_normal_for_identity(const brush_world_runtime *world_runtime,
                                                            const char *brush_identity, int fallback_face_index,
                                                            const char *face_identity, int *out_face_index,
                                                            slayer3d_vec3 *out_normal);
bool editor_brush_world_resize_source_box_face(brush_world_runtime *world_runtime, const char *brush_name,
                                               slayer3d_vec3 face_normal, float distance, char *error_buffer,
                                               int error_buffer_size);
bool editor_brush_world_preview_resize_source_face(const brush_world_runtime *world_runtime, const char *brush_name,
                                                   int fallback_face_index, const char *face_identity, float distance,
                                                   editor_brush_source_vertex_operation_result *out_result,
                                                   char *error_buffer, int error_buffer_size);
bool editor_brush_world_resize_source_face(brush_world_runtime *world_runtime, const char *brush_name,
                                           int fallback_face_index, const char *face_identity, float distance,
                                           editor_brush_source_vertex_operation_result *out_result, char *error_buffer,
                                           int error_buffer_size);
bool editor_brush_world_update_source_box_bounds_batch(brush_world_runtime *world_runtime,
                                                       const editor_source_box_bounds_update *updates, int update_count,
                                                       char *error_buffer, int error_buffer_size);
bool editor_brush_world_build_source_convex_brush_from_vertices(const brush_world_runtime *world_runtime,
                                                                const char *brush_identity, const int *vertices,
                                                                int vertex_count, slayer3d_game_data_brush *out_brush,
                                                                char *error_buffer, int error_buffer_size);
void editor_brush_source_free_runtime_brush(slayer3d_game_data_brush *brush);

/*
 * Shared source-vertex mutation boundary for editor tools.
 *
 * Vertex mode, face/edge tools, clip tools, and future CSG tooling should route source-brush edits through this
 * preview/apply pair instead of mutating editor_source_boxes directly. Preview and apply use the same validation and
 * convex rebuild path, so callers can render diagnostics/handles from dry-run output and then commit the exact same
 * operation when the user accepts it.
 */
bool editor_brush_world_preview_source_vertex_operation(const brush_world_runtime *world_runtime,
                                                        const editor_brush_source_vertex_operation_desc *desc,
                                                        editor_brush_source_vertex_operation_result *out_result,
                                                        char *error_buffer, int error_buffer_size);
bool editor_brush_world_apply_source_vertex_operation(brush_world_runtime *world_runtime,
                                                      const editor_brush_source_vertex_operation_desc *desc,
                                                      editor_brush_source_vertex_operation_result *out_result,
                                                      char *error_buffer, int error_buffer_size);
void editor_brush_world_free_source_clip_result(editor_brush_source_clip_result *result);
bool editor_brush_world_preview_source_clip_operation(const brush_world_runtime *world_runtime,
                                                      const editor_brush_source_clip_desc *desc,
                                                      editor_brush_source_clip_result *out_result, char *error_buffer,
                                                      int error_buffer_size);
bool editor_brush_world_apply_source_clip_operation(brush_world_runtime *world_runtime,
                                                    const editor_brush_source_clip_desc *desc,
                                                    editor_brush_source_clip_result *out_result, char *error_buffer,
                                                    int error_buffer_size);
bool slayer3d_game_data_commit_editor_source_clip(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                  const editor_brush_source_clip_desc *desc,
                                                  editor_brush_source_clip_result *out_result, char *error_buffer,
                                                  int error_buffer_size);
bool editor_brush_world_set_source_box_face_material(brush_world_runtime *world_runtime, const char *brush_name,
                                                     int face_index, const char *material_name, char *error_buffer,
                                                     int error_buffer_size);
bool editor_brush_source_validate_box_vertex_topology(const int *vertices, int snap_units,
                                                      editor_brush_source_vertex_diagnostics *out_diagnostics,
                                                      char *error_buffer, int error_buffer_size);
bool editor_brush_source_box_runtime_build_vertex_model(const brush_world_runtime *world_runtime, int source_index,
                                                        const editor_brush_source_box_runtime *box,
                                                        editor_brush_source_vertex_model *out_model, char *error_buffer,
                                                        int error_buffer_size);
bool editor_brush_source_box_build_vertex_model(const brush_world_runtime *world_runtime, int source_index,
                                                editor_brush_source_vertex_model *out_model, char *error_buffer,
                                                int error_buffer_size);
bool editor_brush_world_validate_source_vertex_model(const brush_world_runtime *world_runtime,
                                                     const int *source_indices, int source_index_count,
                                                     editor_brush_source_vertex_diagnostics *out_diagnostics,
                                                     char *error_buffer, int error_buffer_size);
bool editor_brush_world_find_shared_source_vertices(const brush_world_runtime *world_runtime, const int *source_indices,
                                                    int source_index_count,
                                                    editor_brush_source_shared_vertex *out_vertices, int out_capacity,
                                                    int *out_count, char *error_buffer, int error_buffer_size);
bool slayer3d_game_data_validate_editor_brush_source_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_validate_editor_brush_enclosure_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);

#endif

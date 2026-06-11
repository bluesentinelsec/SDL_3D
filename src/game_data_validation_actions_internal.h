/**
 * @file game_data_validation_actions_internal.h
 * @brief Shared declarations for data action validation modules.
 */

#ifndef SLAYER3D_GAME_DATA_VALIDATION_ACTIONS_INTERNAL_H
#define SLAYER3D_GAME_DATA_VALIDATION_ACTIONS_INTERNAL_H

#include "game_data_validation_internal.h"

bool validate_editor_vertex_delete_selected_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type);
bool validate_editor_vertex_merge_selected_to_hover_action(validation_context *ctx, yyjson_val *action,
                                                           const char *json_path, validation_names *names,
                                                           const char *type);
bool validate_editor_vertex_add_to_source_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type);
bool validate_editor_vertex_validate_source_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type);
bool validate_editor_vertex_snap_selected_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type);
bool validate_editor_tool_set_mode_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type);
bool validate_editor_selection_select_brush_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type);
bool validate_editor_selection_delete_selected_action(validation_context *ctx, yyjson_val *action,
                                                      const char *json_path, validation_names *names, const char *type);
bool validate_editor_selection_resize_y_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type);
bool validate_editor_selection_rotate_selected_action(validation_context *ctx, yyjson_val *action,
                                                      const char *json_path, validation_names *names, const char *type);
bool validate_editor_selection_scale_selected_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                     validation_names *names, const char *type);
bool validate_editor_selection_flip_vertical_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type);
bool validate_editor_selection_flip_horizontal_action(validation_context *ctx, yyjson_val *action,
                                                      const char *json_path, validation_names *names, const char *type);
bool validate_editor_brush_duplicate_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type);
bool validate_editor_selection_shear_selected_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                     validation_names *names, const char *type);
bool validate_editor_selection_run_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type);
bool validate_editor_command_preview_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type);
bool validate_editor_command_clear_preview_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                  validation_names *names, const char *type);
bool validate_editor_command_history_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type);
bool validate_editor_brush_world_export_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type);
bool validate_editor_level_export_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         validation_names *names, const char *type);
bool validate_editor_level_save_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type);
bool validate_editor_level_load_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type);
bool validate_editor_test_run_prepare_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                             validation_names *names, const char *type);
bool validate_editor_test_run_save_manifest_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type);
bool validate_editor_brush_world_status_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type);
bool validate_editor_brush_world_validate_source_action(validation_context *ctx, yyjson_val *action,
                                                        const char *json_path, validation_names *names,
                                                        const char *type);
bool validate_editor_brush_world_validate_enclosure_action(validation_context *ctx, yyjson_val *action,
                                                           const char *json_path, validation_names *names,
                                                           const char *type);
bool validate_editor_brush_world_create_box_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type);
bool validate_editor_player_start_place_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type);
bool validate_editor_player_start_apply_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type);
bool validate_editor_player_start_delete_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names, const char *type);

#endif

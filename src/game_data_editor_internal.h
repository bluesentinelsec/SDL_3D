#ifndef SLAYER3D_GAME_DATA_EDITOR_INTERNAL_H
#define SLAYER3D_GAME_DATA_EDITOR_INTERNAL_H

/*
 * Editor-only declarations shared across game_data editor modules.
 *
 * This header is included from game_data_internal.h after the core runtime
 * types have been declared, so it intentionally does not include that header.
 */

yyjson_val *active_editor_tooling_root(const slayer3d_game_data_runtime *runtime);

typedef struct editor_trace_viewport_config
{
    const char *camera;
    float x;
    float y;
    float width;
    float height;
    float screen_x;
    float screen_y;
    yyjson_val *work_plane;
} editor_trace_viewport_config;

void init_editor_selection(slayer3d_game_data_editor_selection *selection);
bool editor_selection_active_for_scene(const slayer3d_game_data_runtime *runtime);
bool editor_command_preview_active_for_scene(const slayer3d_game_data_runtime *runtime);
bool editor_placement_preview_active_for_scene(const slayer3d_game_data_runtime *runtime);
bool editor_trace_desc_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                 slayer3d_game_data_world_trace_desc *out_trace);
bool editor_trace_select_viewport_at(const slayer3d_game_data_runtime *runtime, yyjson_val *trace, float screen_x,
                                     float screen_y, editor_trace_viewport_config *out_viewport);
bool editor_work_plane_desc_from_trace_json(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                            slayer3d_vec3 *out_normal, float *out_distance);
bool pick_editor_player_start(const slayer3d_game_data_runtime *runtime,
                              const slayer3d_game_data_world_trace_desc *trace,
                              slayer3d_game_data_editor_selection *out_selection);
bool editor_pick_selection_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                     const slayer3d_game_data_world_trace_desc *trace,
                                     slayer3d_game_data_editor_selection *out_selection);
bool editor_selection_mode_is_click(yyjson_val *selection);
bool editor_mouse_in_rect(float mouse_x, float mouse_y, yyjson_val *rect);
bool editor_handle_grid_widget(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed);
void clear_editor_command_preview(slayer3d_game_data_runtime *runtime);
void clear_editor_placement_preview(slayer3d_game_data_runtime *runtime);
void update_editor_placement_preview(slayer3d_game_data_runtime *runtime, yyjson_val *editor,
                                     const slayer3d_game_data_editor_selection *hover_selection);
bool update_editor_drag_create(slayer3d_game_data_runtime *runtime, yyjson_val *editor,
                               const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed);
void publish_editor_selection(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                              const slayer3d_game_data_editor_selection *selection);
bool slayer3d_game_data_select_editor_brush_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_create_editor_source_box_brush(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                       const char *material_name, unsigned int contents,
                                                       const int source_min[3], const int source_max[3],
                                                       editor_brush_source_prefab_result *out_result);
bool slayer3d_game_data_translate_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset);
bool slayer3d_game_data_rotate_selected_editor_brushes_y(slayer3d_game_data_runtime *runtime, int quarter_turns);
void editor_set_string_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, const char *value);
void editor_set_bool_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, bool value);
void editor_set_int_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, int value);
void editor_set_float_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, float value);
void editor_set_vec3_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, slayer3d_vec3 value);

#endif

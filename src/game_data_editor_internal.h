#ifndef SLAYER3D_GAME_DATA_EDITOR_INTERNAL_H
#define SLAYER3D_GAME_DATA_EDITOR_INTERNAL_H

/*
 * Editor-only declarations shared across game_data editor modules.
 *
 * This header is included from game_data_internal.h after the core runtime
 * types have been declared, so it intentionally does not include that header.
 */

#define SLAYER3D_EDITOR_PROPERTY_SLOT_CAP 64

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
bool editor_selected_brushes_active_for_scene(const slayer3d_game_data_runtime *runtime);
bool editor_selected_vertices_active_for_scene(const slayer3d_game_data_runtime *runtime);
bool editor_selected_edges_active_for_scene(const slayer3d_game_data_runtime *runtime);
bool editor_command_preview_active_for_scene(const slayer3d_game_data_runtime *runtime);
bool editor_placement_preview_active_for_scene(const slayer3d_game_data_runtime *runtime);
void init_editor_source_vertex_selection(editor_source_vertex_selection *selection);
void init_editor_source_edge_selection(editor_source_edge_selection *selection);
void clear_editor_vertex_hover_state(slayer3d_game_data_runtime *runtime);
void clear_editor_edge_hover_state(slayer3d_game_data_runtime *runtime);
void publish_editor_selected_vertex_count(slayer3d_game_data_runtime *runtime);
void publish_editor_selected_edge_count(slayer3d_game_data_runtime *runtime);
void clear_editor_selected_vertices(slayer3d_game_data_runtime *runtime);
void clear_editor_selected_edges(slayer3d_game_data_runtime *runtime);
void publish_editor_vertex_lasso_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                       int selected_count);
void publish_editor_edge_lasso_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                     int selected_count);
void publish_editor_edge_drag_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag);
void publish_editor_edge_move_result(slayer3d_game_data_runtime *runtime, bool valid, int edge_count,
                                     const char *message);
void publish_editor_selected_brush_count(slayer3d_game_data_runtime *runtime);
void publish_editor_visibility_state(slayer3d_game_data_runtime *runtime);
void publish_editor_lock_state(slayer3d_game_data_runtime *runtime);
void clear_editor_selected_brushes(slayer3d_game_data_runtime *runtime);
void clear_editor_active_selection(slayer3d_game_data_runtime *runtime);
bool editor_selection_is_selectable_brush(const slayer3d_game_data_editor_selection *selection);
bool editor_selection_from_brush_index(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                       int brush_index, int face_index,
                                       slayer3d_game_data_editor_selection *out_selection);
int editor_selected_brush_index(const slayer3d_game_data_runtime *runtime,
                                const slayer3d_game_data_editor_selection *selection);
void remove_editor_selected_brush_at(slayer3d_game_data_runtime *runtime, int index);
bool add_editor_selected_brush(slayer3d_game_data_runtime *runtime,
                               const slayer3d_game_data_editor_selection *selection);
void update_active_editor_selection_from_selected_brushes(slayer3d_game_data_runtime *runtime);
bool editor_select_mode_primary_click(slayer3d_game_data_runtime *runtime,
                                      const slayer3d_game_data_editor_selection *hover_selection);
bool editor_select_mode_secondary_click(slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_editor_selection *hover_selection);
bool editor_mode_is_select(const slayer3d_game_data_runtime *runtime);
bool editor_mode_is_brush(const slayer3d_game_data_runtime *runtime);
bool editor_mode_is_liquid(const slayer3d_game_data_runtime *runtime);
bool editor_mode_is_face(const slayer3d_game_data_runtime *runtime);
bool editor_mode_is_edge(const slayer3d_game_data_runtime *runtime);
bool editor_mode_is_vertex(const slayer3d_game_data_runtime *runtime);
bool editor_mode_is_rotate(const slayer3d_game_data_runtime *runtime);
bool editor_mode_is_scale(const slayer3d_game_data_runtime *runtime);
bool editor_mode_is_shear(const slayer3d_game_data_runtime *runtime);
bool editor_mode_is_paint(const slayer3d_game_data_runtime *runtime);
bool editor_selection_matches_brush(const slayer3d_game_data_editor_selection *selection, const char *world_name,
                                    const char *element_name);
bool editor_hover_is_selected_brush(const slayer3d_game_data_runtime *runtime,
                                    const slayer3d_game_data_editor_selection *hover_selection);
void publish_editor_vertex_hover_state(slayer3d_game_data_runtime *runtime,
                                       const slayer3d_game_data_editor_selection *selection);
void publish_editor_edge_hover_state(slayer3d_game_data_runtime *runtime,
                                     const slayer3d_game_data_editor_selection *selection);
int editor_selected_edge_index(const slayer3d_game_data_runtime *runtime,
                               const editor_source_edge_selection *selection);
bool editor_source_edge_selection_from_model(const brush_world_runtime *world_runtime,
                                             const editor_brush_source_vertex_model *model, int edge_index,
                                             editor_source_edge_selection *out_selection);
bool editor_add_shared_edge_selection_group(slayer3d_game_data_runtime *runtime,
                                            const editor_source_edge_selection *selection);
bool editor_remove_shared_edge_selection_group(slayer3d_game_data_runtime *runtime,
                                               const editor_source_edge_selection *selection);
bool editor_project_world_to_viewport(const slayer3d_camera3d *camera, const editor_trace_viewport_config *view,
                                      slayer3d_vec3 point, float *out_x, float *out_y);
bool editor_lasso_contains_screen_point(const editor_drag_move_state *drag, const editor_trace_viewport_config *view,
                                        float screen_x, float screen_y);
bool editor_handle_vertex_lasso(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed);
bool editor_handle_vertex_drag(slayer3d_game_data_runtime *runtime,
                               const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed);
bool editor_handle_vertex_add_to_source(slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_editor_selection *hover_selection,
                                        bool select_requested, bool *out_consumed);
void reset_editor_rotate_tool_state(slayer3d_game_data_runtime *runtime, const char *message);
void reset_editor_scale_tool_state(slayer3d_game_data_runtime *runtime, const char *message);
void reset_editor_shear_tool_state(slayer3d_game_data_runtime *runtime, const char *message);
bool editor_handle_vertex_selection(slayer3d_game_data_runtime *runtime,
                                    const slayer3d_game_data_editor_selection *hover_selection, bool select_requested,
                                    bool *out_consumed);
bool editor_handle_edge_selection(slayer3d_game_data_runtime *runtime,
                                  const slayer3d_game_data_editor_selection *hover_selection, bool select_requested,
                                  bool *out_consumed);
bool editor_handle_edge_drag(slayer3d_game_data_runtime *runtime,
                             const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed);
bool editor_handle_edge_lasso(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                              const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed);
bool editor_translate_selected_vertices(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset);
bool editor_translate_selected_edges(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset);
bool editor_trace_desc_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                 slayer3d_game_data_world_trace_desc *out_trace);
bool editor_trace_select_viewport_at(const slayer3d_game_data_runtime *runtime, yyjson_val *trace, float screen_x,
                                     float screen_y, editor_trace_viewport_config *out_viewport);
bool editor_work_plane_desc_from_trace_json(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                            slayer3d_vec3 *out_normal, float *out_distance);
bool pick_editor_player_start(const slayer3d_game_data_runtime *runtime,
                              const slayer3d_game_data_world_trace_desc *trace,
                              slayer3d_game_data_editor_selection *out_selection);
bool pick_editor_actor(const slayer3d_game_data_runtime *runtime, const slayer3d_game_data_world_trace_desc *trace,
                       slayer3d_game_data_editor_selection *out_selection);
bool editor_pick_selection_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                     const slayer3d_game_data_world_trace_desc *trace,
                                     slayer3d_game_data_editor_selection *out_selection);
bool editor_selection_mode_is_click(yyjson_val *selection);
bool editor_mouse_in_rect(float mouse_x, float mouse_y, yyjson_val *rect);
bool editor_handle_grid_widget(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed);
bool editor_handle_shape_widget(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed);
bool editor_handle_tool_mode_buttons(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed);
bool editor_handle_clip_tool_input(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                   const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed);
void clear_editor_command_preview(slayer3d_game_data_runtime *runtime);
void clear_editor_placement_preview(slayer3d_game_data_runtime *runtime);
bool editor_cancel_pending_brush_preview(slayer3d_game_data_runtime *runtime, const char *message);
void update_editor_placement_preview(slayer3d_game_data_runtime *runtime, yyjson_val *editor,
                                     const slayer3d_game_data_editor_selection *hover_selection);
bool update_editor_drag_create(slayer3d_game_data_runtime *runtime, yyjson_val *editor,
                               const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed);
const char *editor_selection_type_name(slayer3d_game_data_world_model_type type);
slayer3d_game_data_editor_selection resolved_editor_selection(const slayer3d_game_data_runtime *runtime,
                                                              const slayer3d_game_data_editor_selection *selection);
bool editor_camera_basis(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 *out_forward,
                         slayer3d_vec3 *out_right, slayer3d_vec3 *out_up, bool *out_orthographic);
void publish_editor_selection(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                              const slayer3d_game_data_editor_selection *selection);
void publish_editor_selection_properties(slayer3d_game_data_runtime *runtime,
                                         const slayer3d_game_data_editor_selection *selection, int slot_count);
bool slayer3d_game_data_select_editor_brush_action(slayer3d_game_data_runtime *runtime, yyjson_val *action);
bool slayer3d_game_data_translate_selected_editor_actor(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset);
bool slayer3d_game_data_create_editor_source_box_brush(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                       const char *material_name, unsigned int contents,
                                                       const int source_min[3], const int source_max[3],
                                                       const char *prefab,
                                                       editor_brush_source_prefab_result *out_result);
bool slayer3d_game_data_translate_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset);
bool slayer3d_game_data_rotate_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 pivot,
                                                       slayer3d_vec3 axis, float angle_radians);
bool slayer3d_game_data_scale_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 anchor,
                                                      slayer3d_vec3 factors);
bool slayer3d_game_data_flip_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 plane_point,
                                                     slayer3d_vec3 plane_normal);
bool slayer3d_game_data_flip_selected_editor_brushes_horizontal(slayer3d_game_data_runtime *runtime,
                                                                slayer3d_vec3 plane_point, slayer3d_vec3 plane_normal);
bool slayer3d_game_data_shear_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_bounding_box bounds,
                                                      slayer3d_vec3 side_normal, slayer3d_vec3 delta);
bool slayer3d_game_data_rotate_selected_editor_brushes_y(slayer3d_game_data_runtime *runtime, int quarter_turns);
bool slayer3d_game_data_duplicate_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset,
                                                          bool use_last_offset);
bool slayer3d_game_data_paint_selected_editor_brushes(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload);
bool slayer3d_game_data_color_selected_editor_brushes(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload);
int editor_source_units_from_meters(const brush_world_runtime *world_runtime, float meters);
void editor_set_string_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, const char *value);
void editor_set_bool_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, bool value);
void editor_set_int_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, int value);
void editor_set_float_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, float value);
void editor_set_vec3_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, slayer3d_vec3 value);
void editor_publish_console_message(slayer3d_game_data_runtime *runtime, const char *message);
void editor_refresh_console_lines(slayer3d_game_data_runtime *runtime);
bool editor_emit_signal_by_name(slayer3d_game_data_runtime *runtime, const char *signal_name);
const slayer3d_ui_layout_hit_region *editor_find_layout_hit_by_id(const slayer3d_ui_layout_model *layout,
                                                                  const char *id);
const slayer3d_ui_layout_render_command *editor_find_layout_render_by_id(const slayer3d_ui_layout_model *layout,
                                                                         const char *id);
float editor_clamp_float(float value, float min_value, float max_value);
bool editor_set_console_scroll(slayer3d_game_data_runtime *runtime, int scroll);
bool editor_scroll_console_by(slayer3d_game_data_runtime *runtime, int delta);
void editor_set_console_focus(slayer3d_game_data_runtime *runtime, bool focused);
void editor_clear_console_selection(slayer3d_game_data_runtime *runtime);
bool editor_console_set_selection_cursor(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_model *layout,
                                         float mouse_x, float mouse_y);
bool editor_console_begin_selection(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_model *layout,
                                    float mouse_x, float mouse_y);
bool editor_console_copy_selection(slayer3d_game_data_runtime *runtime);
bool editor_console_copy_selection_if_requested(slayer3d_game_data_runtime *runtime,
                                                const slayer3d_input_manager *input);
int editor_console_wheel_scroll_delta(float wheel_y);
bool editor_update_console_scroll_drag(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_model *layout,
                                       float mouse_y);
bool editor_property_edit_has_focus(const slayer3d_game_data_runtime *runtime);
bool editor_texture_edit_has_focus(const slayer3d_game_data_runtime *runtime);
bool editor_sky_edit_has_focus(const slayer3d_game_data_runtime *runtime);
bool editor_update_sky_text_edit(slayer3d_game_data_runtime *runtime);
void editor_update_sky_edit_display(slayer3d_game_data_runtime *runtime);
bool editor_global_edit_has_focus(const slayer3d_game_data_runtime *runtime);
bool editor_update_property_text_edit(slayer3d_game_data_runtime *runtime);
bool editor_update_texture_text_edit(slayer3d_game_data_runtime *runtime);
bool editor_update_global_text_edit(slayer3d_game_data_runtime *runtime);
void editor_update_texture_edit_display(slayer3d_game_data_runtime *runtime);
void editor_update_global_edit_display(slayer3d_game_data_runtime *runtime);

#endif

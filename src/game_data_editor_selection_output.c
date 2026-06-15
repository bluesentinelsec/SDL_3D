/**
 * @file game_data_editor_selection_output.c
 * @brief Editor selection scene-state publishing helpers.
 */

#include "game_data_internal.h"

static const char *selection_metadata_stable_id(const slayer3d_game_data_editor_metadata *metadata)
{
    return metadata != NULL && metadata->stable_id != NULL ? metadata->stable_id : "";
}

const char *editor_selection_type_name(slayer3d_game_data_world_model_type type)
{
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL)
        return "sector_level";
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
        return "brush_world";
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_PLAYER_START)
        return "editor_player_start";
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR)
        return "editor_actor";
    return "none";
}

void publish_editor_selection(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                              const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || outputs == NULL || selection == NULL)
        return;

    slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, selection);
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "hit_key", resolved.hit);
    editor_set_string_output(scene_state, outputs, "type_key", editor_selection_type_name(resolved.type));
    editor_set_string_output(scene_state, outputs, "world_key", resolved.hit ? resolved.world_name : "");
    editor_set_string_output(scene_state, outputs, "element_key", resolved.hit ? resolved.element_name : "");
    editor_set_string_output(scene_state, outputs, "material_key", resolved.hit ? resolved.material_name : "");
    editor_set_string_output(scene_state, outputs, "world_stable_id_key",
                             resolved.hit ? selection_metadata_stable_id(resolved.world_editor) : "");
    editor_set_string_output(scene_state, outputs, "element_stable_id_key",
                             resolved.hit ? selection_metadata_stable_id(resolved.element_editor) : "");
    editor_set_string_output(scene_state, outputs, "material_stable_id_key",
                             resolved.hit ? selection_metadata_stable_id(resolved.material_editor) : "");
    editor_set_string_output(scene_state, outputs, "face_stable_id_key",
                             resolved.hit ? selection_metadata_stable_id(resolved.face_editor) : "");
    editor_set_int_output(scene_state, outputs, "element_index_key", resolved.hit ? resolved.element_index : -1);
    editor_set_int_output(scene_state, outputs, "face_index_key", resolved.hit ? resolved.face_index : -1);
    editor_set_bool_output(scene_state, outputs, "face_rendered_key", resolved.hit && resolved.compiled_face != NULL);
    editor_set_int_output(scene_state, outputs, "compiled_face_index_key",
                          resolved.hit ? resolved.compiled_face_index : -1);
    editor_set_int_output(scene_state, outputs, "compiled_mesh_index_key",
                          resolved.hit && resolved.compiled_face != NULL ? resolved.compiled_face->mesh_index : -1);
    editor_set_int_output(scene_state, outputs, "compiled_first_vertex_key",
                          resolved.hit && resolved.compiled_face != NULL ? resolved.compiled_face->first_vertex : -1);
    editor_set_int_output(scene_state, outputs, "compiled_vertex_count_key",
                          resolved.hit && resolved.compiled_face != NULL ? resolved.compiled_face->vertex_count : 0);
    editor_set_int_output(scene_state, outputs, "compiled_triangle_count_key",
                          resolved.hit && resolved.compiled_face != NULL ? resolved.compiled_face->triangle_count : 0);
    editor_set_float_output(scene_state, outputs, "fraction_key", resolved.hit ? resolved.fraction : 1.0f);
    const slayer3d_vec3 bounds_min =
        resolved.hit && resolved.has_bounds ? resolved.bounds.min : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const slayer3d_vec3 bounds_max =
        resolved.hit && resolved.has_bounds ? resolved.bounds.max : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const slayer3d_vec3 dimensions = resolved.hit && resolved.has_bounds ? slayer3d_vec3_sub(bounds_max, bounds_min)
                                                                         : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    editor_set_vec3_output(scene_state, outputs, "bounds_min_key", bounds_min);
    editor_set_vec3_output(scene_state, outputs, "bounds_max_key", bounds_max);
    editor_set_vec3_output(scene_state, outputs, "dimensions_key", dimensions);
    editor_set_float_output(scene_state, outputs, "dimension_x_key", dimensions.x);
    editor_set_float_output(scene_state, outputs, "dimension_y_key", dimensions.y);
    editor_set_float_output(scene_state, outputs, "dimension_z_key", dimensions.z);
    editor_set_vec3_output(scene_state, outputs, "point_key",
                           resolved.hit ? resolved.point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_vec3_output(scene_state, outputs, "normal_key",
                           resolved.hit ? resolved.normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    if (obj_get(outputs, "type_key") != NULL)
        publish_editor_selection_properties(runtime, &resolved, SLAYER3D_EDITOR_PROPERTY_SLOT_CAP);
}

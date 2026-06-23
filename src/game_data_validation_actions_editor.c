/**
 * @file game_data_validation_actions_editor.c
 * @brief Editor data action validation.
 */

#include "game_data_validation_actions_internal.h"

#include <SDL3/SDL_stdinc.h>

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static bool is_non_empty_string(yyjson_val *object, const char *key)
{
    const char *value = json_string(object, key);
    return value != NULL && value[0] != '\0';
}

static bool validation_float_finite(float value)
{
    return !SDL_isnan(value) && !SDL_isinf(value);
}

static bool editor_command_name_valid(const char *value)
{
    return value != NULL &&
           (SDL_strcmp(value, "translate") == 0 || SDL_strcmp(value, "rotate") == 0 ||
            SDL_strcmp(value, "paint") == 0 || SDL_strcmp(value, "color") == 0 || SDL_strcmp(value, "resize") == 0 ||
            SDL_strcmp(value, "extrude") == 0 || SDL_strcmp(value, "delete") == 0);
}

static bool editor_command_target_name_valid(const char *value)
{
    return value != NULL &&
           (SDL_strcmp(value, "selection") == 0 || SDL_strcmp(value, "world") == 0 ||
            SDL_strcmp(value, "element") == 0 || SDL_strcmp(value, "face") == 0 || SDL_strcmp(value, "material") == 0);
}

bool validate_editor_vertex_delete_selected_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type)
{
    (void)names;
    const char *output_fields[] = {"valid_key", "message_key", "deleted_count_key", "source_count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_fields, SDL_arraysize(output_fields));
}

bool validate_editor_vertex_merge_selected_to_hover_action(validation_context *ctx, yyjson_val *action,
                                                           const char *json_path, validation_names *names,
                                                           const char *type)
{
    (void)names;
    yyjson_val *target_vertex_index = obj_get(action, "target_vertex_index");
    if (target_vertex_index != NULL && (!yyjson_is_int(target_vertex_index) || yyjson_get_int(target_vertex_index) < 0))
        return validation_error(ctx, json_path,
                                "editor.vertex.merge_selected_to_hover target_vertex_index must be non-negative");
    yyjson_val *world = obj_get(action, "world");
    if (world != NULL && (!yyjson_is_str(world) || yyjson_get_str(world)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.vertex.merge_selected_to_hover world must be non-empty");
    yyjson_val *brush = obj_get(action, "brush");
    if (brush != NULL && (!yyjson_is_str(brush) || yyjson_get_str(brush)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.vertex.merge_selected_to_hover brush must be non-empty");
    yyjson_val *brush_stable_id = obj_get(action, "brush_stable_id");
    if (brush_stable_id != NULL && (!yyjson_is_str(brush_stable_id) || yyjson_get_str(brush_stable_id)[0] == '\0'))
        return validation_error(ctx, json_path,
                                "editor.vertex.merge_selected_to_hover brush_stable_id must be non-empty");
    const char *output_fields[] = {"valid_key", "message_key", "merged_count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_fields, SDL_arraysize(output_fields));
}

bool validate_editor_vertex_add_to_source_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type)
{
    (void)names;
    yyjson_val *coord = obj_get(action, "coord");
    yyjson_val *position = obj_get(action, "position");
    if ((coord == NULL) == (position == NULL))
        return validation_error(ctx, json_path,
                                "editor.vertex.add_to_source requires exactly one of coord or position");
    if (coord != NULL)
    {
        if (!yyjson_is_arr(coord) || yyjson_arr_size(coord) != 3)
            return validation_error(ctx, json_path, "editor.vertex.add_to_source coord must be an int[3]");
        for (size_t i = 0; i < 3; ++i)
        {
            if (!yyjson_is_int(yyjson_arr_get(coord, i)))
                return validation_error(ctx, json_path, "editor.vertex.add_to_source coord must be an int[3]");
        }
    }
    if (position != NULL && !is_vec_array(position, 3))
        return validation_error(ctx, json_path, "editor.vertex.add_to_source position must be a vec3");
    yyjson_val *world = obj_get(action, "world");
    if (world != NULL && (!yyjson_is_str(world) || yyjson_get_str(world)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.vertex.add_to_source world must be non-empty");
    yyjson_val *brush = obj_get(action, "brush");
    if (brush != NULL && (!yyjson_is_str(brush) || yyjson_get_str(brush)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.vertex.add_to_source brush must be non-empty");
    yyjson_val *brush_stable_id = obj_get(action, "brush_stable_id");
    if (brush_stable_id != NULL && (!yyjson_is_str(brush_stable_id) || yyjson_get_str(brush_stable_id)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.vertex.add_to_source brush_stable_id must be non-empty");
    const char *output_fields[] = {"valid_key", "message_key", "vertex_count_key", "added_count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_fields, SDL_arraysize(output_fields));
}

bool validate_editor_vertex_validate_source_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type)
{
    (void)names;
    yyjson_val *world = obj_get(action, "world");
    if (world != NULL && (!yyjson_is_str(world) || yyjson_get_str(world)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.vertex.validate_source world must be non-empty");
    yyjson_val *brush = obj_get(action, "brush");
    if (brush != NULL && (!yyjson_is_str(brush) || yyjson_get_str(brush)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.vertex.validate_source brush must be non-empty");
    yyjson_val *brush_stable_id = obj_get(action, "brush_stable_id");
    if (brush_stable_id != NULL && (!yyjson_is_str(brush_stable_id) || yyjson_get_str(brush_stable_id)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.vertex.validate_source brush_stable_id must be non-empty");
    if (brush != NULL && brush_stable_id != NULL)
        return validation_error(ctx, json_path, "editor.vertex.validate_source accepts only one brush identity");
    const char *output_fields[] = {"valid_key",          "message_key",
                                   "world_key",          "brush_count_key",
                                   "vertex_count_key",   "edge_count_key",
                                   "face_count_key",     "shared_vertex_count_key",
                                   "off_snap_count_key", "degenerate_count_key",
                                   "concave_count_key",  "non_finite_count_key",
                                   "first_issue_key",    "first_issue_stable_id_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_fields, SDL_arraysize(output_fields));
}

bool validate_editor_vertex_snap_selected_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type)
{
    (void)names;
    yyjson_val *snap_units = obj_get(action, "snap_units");
    if (snap_units != NULL && (!yyjson_is_int(snap_units) || yyjson_get_int(snap_units) <= 0))
        return validation_error(ctx, json_path, "editor.vertex.snap_selected snap_units must be positive");
    yyjson_val *grid = obj_get(action, "grid");
    if (grid != NULL && (!yyjson_is_num(grid) || yyjson_get_num(grid) <= 0.0))
        return validation_error(ctx, json_path, "editor.vertex.snap_selected grid must be positive");
    yyjson_val *default_grid = obj_get(action, "default_grid");
    if (default_grid != NULL && (!yyjson_is_num(default_grid) || yyjson_get_num(default_grid) <= 0.0))
        return validation_error(ctx, json_path, "editor.vertex.snap_selected default_grid must be positive");
    yyjson_val *grid_key = obj_get(action, "grid_key");
    if (grid_key != NULL && (!yyjson_is_str(grid_key) || yyjson_get_str(grid_key)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.vertex.snap_selected grid_key must be non-empty");
    const char *output_fields[] = {"valid_key", "message_key", "changed_count_key", "source_count_key",
                                   "snap_units_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_fields, SDL_arraysize(output_fields));
}

static bool editor_tool_mode_valid(const char *mode)
{
    return mode != NULL &&
           (SDL_strcmp(mode, "select") == 0 || SDL_strcmp(mode, "brush") == 0 || SDL_strcmp(mode, "face") == 0 ||
            SDL_strcmp(mode, "edge") == 0 || SDL_strcmp(mode, "clip") == 0 || SDL_strcmp(mode, "vertex") == 0 ||
            SDL_strcmp(mode, "rotate") == 0 || SDL_strcmp(mode, "scale") == 0 || SDL_strcmp(mode, "shear") == 0);
}

bool validate_editor_tool_set_mode_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    const char *mode = json_string(action, "mode");
    if (!editor_tool_mode_valid(mode))
        return validation_error(ctx, json_path,
                                "editor.tool.set_mode mode must be one of select, brush, face, edge, clip, vertex, "
                                "rotate, scale, shear");
    yyjson_val *message = obj_get(action, "message");
    if (message != NULL && (!yyjson_is_str(message) || yyjson_get_str(message)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.tool.set_mode message must be non-empty");
    return true;
}

static bool validate_optional_action_branches(validation_context *ctx, yyjson_val *action, const char *json_path,
                                              validation_names *names, bool require_actions)
{
    char actions_path[PATH_BUFFER_SIZE];
    char else_path[PATH_BUFFER_SIZE];
    format_path(actions_path, sizeof(actions_path), "%s.actions", json_path);
    format_path(else_path, sizeof(else_path), "%s.else", json_path);
    yyjson_val *actions = obj_get(action, "actions");
    yyjson_val *else_actions = obj_get(action, "else");
    if (require_actions)
    {
        if (!validate_action_array(ctx, actions, actions_path, names))
            return false;
    }
    else if (actions != NULL && !validate_action_array(ctx, actions, actions_path, names))
    {
        return false;
    }
    return else_actions == NULL || validate_action_array(ctx, else_actions, else_path, names);
}

bool validate_editor_selection_select_brush_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type)
{
    (void)type;
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
        return false;

    yyjson_val *element = obj_get(action, "element");
    yyjson_val *element_from_state = obj_get(action, "element_from_state");
    yyjson_val *element_stable_id = obj_get(action, "element_stable_id");
    yyjson_val *element_stable_id_from_state = obj_get(action, "element_stable_id_from_state");
    const int element_identity_count = (element != NULL ? 1 : 0) + (element_from_state != NULL ? 1 : 0) +
                                       (element_stable_id != NULL ? 1 : 0) +
                                       (element_stable_id_from_state != NULL ? 1 : 0);
    if (element_identity_count != 1)
        return validation_error(ctx, json_path,
                                "editor.selection.select_brush requires exactly one brush identity field");
    if (element != NULL && (!yyjson_is_str(element) || yyjson_get_str(element)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.selection.select_brush element must be non-empty");
    if (element_from_state != NULL &&
        (!yyjson_is_str(element_from_state) || yyjson_get_str(element_from_state)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "editor.selection.select_brush element_from_state must be non-empty");
    }
    if (element_stable_id != NULL &&
        (!yyjson_is_str(element_stable_id) || yyjson_get_str(element_stable_id)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.selection.select_brush element_stable_id must be non-empty");
    if (element_stable_id_from_state != NULL &&
        (!yyjson_is_str(element_stable_id_from_state) || yyjson_get_str(element_stable_id_from_state)[0] == '\0'))
    {
        return validation_error(ctx, json_path,
                                "editor.selection.select_brush element_stable_id_from_state must be non-empty");
    }

    yyjson_val *face = obj_get(action, "face");
    yyjson_val *face_from_state = obj_get(action, "face_from_state");
    yyjson_val *face_stable_id = obj_get(action, "face_stable_id");
    yyjson_val *face_stable_id_from_state = obj_get(action, "face_stable_id_from_state");
    const int face_identity_count = (face != NULL ? 1 : 0) + (face_from_state != NULL ? 1 : 0) +
                                    (face_stable_id != NULL ? 1 : 0) + (face_stable_id_from_state != NULL ? 1 : 0);
    if (face_identity_count > 1)
        return validation_error(ctx, json_path,
                                "editor.selection.select_brush accepts at most one face identity field");
    if (face != NULL && (!yyjson_is_str(face) || yyjson_get_str(face)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.selection.select_brush face must be non-empty");
    if (face_from_state != NULL && (!yyjson_is_str(face_from_state) || yyjson_get_str(face_from_state)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.selection.select_brush face_from_state must be non-empty");
    if (face_stable_id != NULL && (!yyjson_is_str(face_stable_id) || yyjson_get_str(face_stable_id)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.selection.select_brush face_stable_id must be non-empty");
    if (face_stable_id_from_state != NULL &&
        (!yyjson_is_str(face_stable_id_from_state) || yyjson_get_str(face_stable_id_from_state)[0] == '\0'))
    {
        return validation_error(ctx, json_path,
                                "editor.selection.select_brush face_stable_id_from_state must be non-empty");
    }

    yyjson_val *message = obj_get(action, "message");
    if (message != NULL && !yyjson_is_str(message))
        return validation_error(ctx, json_path, "editor.selection.select_brush message must be a string");
    yyjson_val *invalid_message = obj_get(action, "invalid_message");
    if (invalid_message != NULL && !yyjson_is_str(invalid_message))
        return validation_error(ctx, json_path, "editor.selection.select_brush invalid_message must be a string");
    yyjson_val *additive = obj_get(action, "additive");
    if (additive != NULL && !yyjson_is_bool(additive))
        return validation_error(ctx, json_path, "editor.selection.select_brush additive must be a boolean");

    const char *output_keys[] = {"valid_key", "message_key"};
    return validate_optional_output_keys(ctx, action, json_path, "editor.selection.select_brush", output_keys,
                                         SDL_arraysize(output_keys));
}

bool validate_editor_selection_delete_selected_action(validation_context *ctx, yyjson_val *action,
                                                      const char *json_path, validation_names *names, const char *type)
{
    (void)type;
    return validate_optional_action_branches(ctx, action, json_path, names, false);
}

bool validate_editor_selection_resize_y_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    (void)type;
    yyjson_val *direction = obj_get(action, "direction");
    if (direction != NULL && (!yyjson_is_int(direction) || yyjson_get_int(direction) == 0))
        return validation_error(ctx, json_path, "editor.selection.resize_y direction must be a non-zero integer");
    yyjson_val *distance = obj_get(action, "distance");
    if (distance != NULL && (!yyjson_is_num(distance) || yyjson_get_num(distance) <= 0.0))
        return validation_error(ctx, json_path, "editor.selection.resize_y distance must be positive");
    yyjson_val *default_distance = obj_get(action, "default_distance");
    if (default_distance != NULL && (!yyjson_is_num(default_distance) || yyjson_get_num(default_distance) <= 0.0))
        return validation_error(ctx, json_path, "editor.selection.resize_y default_distance must be positive");
    yyjson_val *distance_key = obj_get(action, "distance_key");
    if (distance_key != NULL && (!yyjson_is_str(distance_key) || yyjson_get_str(distance_key)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.selection.resize_y distance_key must be non-empty");
    yyjson_val *grid_key = obj_get(action, "grid_key");
    if (grid_key != NULL && (!yyjson_is_str(grid_key) || yyjson_get_str(grid_key)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.selection.resize_y grid_key must be non-empty");
    yyjson_val *min_height = obj_get(action, "min_height");
    if (min_height != NULL && (!yyjson_is_num(min_height) || yyjson_get_num(min_height) <= 0.0))
        return validation_error(ctx, json_path, "editor.selection.resize_y min_height must be positive");
    yyjson_val *min_elevation = obj_get(action, "min_elevation");
    if (min_elevation != NULL && !yyjson_is_num(min_elevation))
        return validation_error(ctx, json_path, "editor.selection.resize_y min_elevation must be numeric");
    yyjson_val *max_elevation = obj_get(action, "max_elevation");
    if (max_elevation != NULL && !yyjson_is_num(max_elevation))
        return validation_error(ctx, json_path, "editor.selection.resize_y max_elevation must be numeric");
    if (min_elevation != NULL && max_elevation != NULL &&
        yyjson_get_num(min_elevation) >= yyjson_get_num(max_elevation))
        return validation_error(ctx, json_path,
                                "editor.selection.resize_y min_elevation must be less than max_elevation");
    yyjson_val *slab_max_height = obj_get(action, "slab_max_height");
    if (slab_max_height != NULL && (!yyjson_is_num(slab_max_height) || yyjson_get_num(slab_max_height) <= 0.0))
        return validation_error(ctx, json_path, "editor.selection.resize_y slab_max_height must be positive");
    yyjson_val *fill_thickness = obj_get(action, "fill_thickness");
    if (fill_thickness != NULL && (!yyjson_is_num(fill_thickness) || yyjson_get_num(fill_thickness) <= 0.0))
        return validation_error(ctx, json_path, "editor.selection.resize_y fill_thickness must be positive");
    yyjson_val *floor_material = obj_get(action, "floor_material");
    if (floor_material != NULL && (!yyjson_is_str(floor_material) || yyjson_get_str(floor_material)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.selection.resize_y floor_material must be non-empty");
    yyjson_val *fill_material = obj_get(action, "fill_material");
    if (fill_material != NULL && (!yyjson_is_str(fill_material) || yyjson_get_str(fill_material)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.selection.resize_y fill_material must be non-empty");
    yyjson_val *outputs = obj_get(action, "outputs");
    if (outputs != NULL && !yyjson_is_obj(outputs))
        return validation_error(ctx, json_path, "editor.selection.resize_y outputs must be an object");
    return validate_optional_action_branches(ctx, action, json_path, names, false);
}

bool validate_editor_selection_rotate_selected_action(validation_context *ctx, yyjson_val *action,
                                                      const char *json_path, validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    yyjson_val *pivot = obj_get(action, "pivot");
    if (pivot != NULL && !is_vec_array(pivot, 3))
        return validation_error(ctx, json_path, "editor.selection.rotate_selected pivot must be a numeric vec3");
    yyjson_val *axis = obj_get(action, "axis");
    if (!is_vec_array(axis, 3))
        return validation_error(ctx, json_path, "editor.selection.rotate_selected axis must be a numeric vec3");
    const float axis_x = (float)yyjson_get_num(yyjson_arr_get(axis, 0));
    const float axis_y = (float)yyjson_get_num(yyjson_arr_get(axis, 1));
    const float axis_z = (float)yyjson_get_num(yyjson_arr_get(axis, 2));
    if (!validation_float_finite(axis_x) || !validation_float_finite(axis_y) || !validation_float_finite(axis_z) ||
        axis_x * axis_x + axis_y * axis_y + axis_z * axis_z <= 0.000001f)
    {
        return validation_error(ctx, json_path, "editor.selection.rotate_selected axis must be non-zero and finite");
    }
    yyjson_val *angle_radians = obj_get(action, "angle_radians");
    yyjson_val *angle_degrees = obj_get(action, "angle_degrees");
    if ((angle_radians == NULL) == (angle_degrees == NULL))
    {
        return validation_error(ctx, json_path,
                                "editor.selection.rotate_selected requires exactly one angle_radians or angle_degrees");
    }
    if (angle_radians != NULL &&
        (!yyjson_is_num(angle_radians) || !validation_float_finite((float)yyjson_get_num(angle_radians))))
        return validation_error(ctx, json_path, "editor.selection.rotate_selected angle_radians must be finite");
    if (angle_degrees != NULL &&
        (!yyjson_is_num(angle_degrees) || !validation_float_finite((float)yyjson_get_num(angle_degrees))))
        return validation_error(ctx, json_path, "editor.selection.rotate_selected angle_degrees must be finite");
    return true;
}

bool validate_editor_selection_scale_selected_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                     validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    yyjson_val *anchor = obj_get(action, "anchor");
    if (anchor != NULL && !is_vec_array(anchor, 3))
        return validation_error(ctx, json_path, "editor.selection.scale_selected anchor must be a numeric vec3");
    yyjson_val *factors = obj_get(action, "factors");
    if (!is_vec_array(factors, 3))
        return validation_error(ctx, json_path, "editor.selection.scale_selected factors must be a numeric vec3");
    const float factor_x = (float)yyjson_get_num(yyjson_arr_get(factors, 0));
    const float factor_y = (float)yyjson_get_num(yyjson_arr_get(factors, 1));
    const float factor_z = (float)yyjson_get_num(yyjson_arr_get(factors, 2));
    if (!validation_float_finite(factor_x) || !validation_float_finite(factor_y) ||
        !validation_float_finite(factor_z) || factor_x <= 0.0f || factor_y <= 0.0f || factor_z <= 0.0f)
    {
        return validation_error(ctx, json_path, "editor.selection.scale_selected factors must be positive and finite");
    }
    return true;
}

bool validate_editor_selection_flip_vertical_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    yyjson_val *pivot = obj_get(action, "pivot");
    if (pivot != NULL && !is_vec_array(pivot, 3))
        return validation_error(ctx, json_path, "editor.selection.flip_vertical pivot must be a numeric vec3");
    yyjson_val *normal = obj_get(action, "normal");
    if (normal != NULL)
    {
        if (!is_vec_array(normal, 3))
            return validation_error(ctx, json_path, "editor.selection.flip_vertical normal must be a numeric vec3");
        const float normal_x = (float)yyjson_get_num(yyjson_arr_get(normal, 0));
        const float normal_y = (float)yyjson_get_num(yyjson_arr_get(normal, 1));
        const float normal_z = (float)yyjson_get_num(yyjson_arr_get(normal, 2));
        if (!validation_float_finite(normal_x) || !validation_float_finite(normal_y) ||
            !validation_float_finite(normal_z) ||
            normal_x * normal_x + normal_y * normal_y + normal_z * normal_z <= 0.000001f)
        {
            return validation_error(ctx, json_path,
                                    "editor.selection.flip_vertical normal must be non-zero and finite");
        }
    }
    static const char *const string_fields[] = {"normal_key", "view_mode_key", "top_mode",
                                                "front_mode", "side_mode",     "flyby_mode"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        yyjson_val *field = obj_get(action, string_fields[i]);
        if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.selection.flip_vertical %s must be a non-empty string",
                                    string_fields[i]);
    }
    static const char *const output_keys[] = {"valid_key", "message_key", "axis_key", "center_key"};
    return validate_optional_output_keys(ctx, action, json_path, "editor.selection.flip_vertical", output_keys,
                                         SDL_arraysize(output_keys));
}

bool validate_editor_selection_flip_horizontal_action(validation_context *ctx, yyjson_val *action,
                                                      const char *json_path, validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    yyjson_val *pivot = obj_get(action, "pivot");
    if (pivot != NULL && !is_vec_array(pivot, 3))
        return validation_error(ctx, json_path, "editor.selection.flip_horizontal pivot must be a numeric vec3");
    yyjson_val *normal = obj_get(action, "normal");
    if (normal != NULL)
    {
        if (!is_vec_array(normal, 3))
            return validation_error(ctx, json_path, "editor.selection.flip_horizontal normal must be a numeric vec3");
        const float normal_x = (float)yyjson_get_num(yyjson_arr_get(normal, 0));
        const float normal_y = (float)yyjson_get_num(yyjson_arr_get(normal, 1));
        const float normal_z = (float)yyjson_get_num(yyjson_arr_get(normal, 2));
        if (!validation_float_finite(normal_x) || !validation_float_finite(normal_y) ||
            !validation_float_finite(normal_z) ||
            normal_x * normal_x + normal_y * normal_y + normal_z * normal_z <= 0.000001f)
        {
            return validation_error(ctx, json_path,
                                    "editor.selection.flip_horizontal normal must be non-zero and finite");
        }
    }
    static const char *const string_fields[] = {"normal_key", "view_mode_key", "top_mode",
                                                "front_mode", "side_mode",     "flyby_mode"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        yyjson_val *field = obj_get(action, string_fields[i]);
        if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.selection.flip_horizontal %s must be a non-empty string",
                                    string_fields[i]);
    }
    static const char *const output_keys[] = {"valid_key", "message_key", "axis_key", "center_key"};
    return validate_optional_output_keys(ctx, action, json_path, "editor.selection.flip_horizontal", output_keys,
                                         SDL_arraysize(output_keys));
}

bool validate_editor_brush_duplicate_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    const char *mode = json_string(action, "mode");
    if (mode == NULL)
        mode = "in_place";
    if (SDL_strcmp(mode, "in_place") != 0 && SDL_strcmp(mode, "last_offset") != 0 && SDL_strcmp(mode, "offset") != 0)
    {
        return validation_error(ctx, json_path,
                                "editor.brush.duplicate mode must be one of in_place, last_offset, offset");
    }

    yyjson_val *offset = obj_get(action, "offset");
    if (offset != NULL && !is_vec_array(offset, 3))
        return validation_error(ctx, json_path, "editor.brush.duplicate offset must be a numeric vec3");
    if (offset != NULL)
    {
        const float offset_x = (float)yyjson_get_num(yyjson_arr_get(offset, 0));
        const float offset_y = (float)yyjson_get_num(yyjson_arr_get(offset, 1));
        const float offset_z = (float)yyjson_get_num(yyjson_arr_get(offset, 2));
        if (!validation_float_finite(offset_x) || !validation_float_finite(offset_y) ||
            !validation_float_finite(offset_z))
        {
            return validation_error(ctx, json_path, "editor.brush.duplicate offset must be finite");
        }
    }

    static const char *const output_keys[] = {"valid_key", "message_key", "count_key", "mode_key", "offset_key"};
    return validate_optional_output_keys(ctx, action, json_path, "editor.brush.duplicate", output_keys,
                                         SDL_arraysize(output_keys));
}

bool validate_editor_brush_paint_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type)
{
    const char *target = json_string(action, "target");
    if (target == NULL)
        target = "selection";
    if (SDL_strcmp(target, "selection") != 0 && SDL_strcmp(target, "face") != 0)
        return validation_error(ctx, json_path, "%s target must be selection or face", type);

    yyjson_val *material = obj_get(action, "material");
    yyjson_val *material_key = obj_get(action, "material_key");
    if (material != NULL && (!yyjson_is_str(material) || yyjson_get_str(material)[0] == '\0'))
        return validation_error(ctx, json_path, "%s material must be a non-empty string", type);
    if (material_key != NULL && (!yyjson_is_str(material_key) || yyjson_get_str(material_key)[0] == '\0'))
        return validation_error(ctx, json_path, "%s material_key must be a non-empty string", type);
    if (material == NULL && material_key == NULL)
        return validation_error(ctx, json_path, "%s requires material or material_key", type);

    const char *output_keys[] = {
        "valid_key",      "event_key",       "message_key", "transaction_id_key", "undo_count_key",    "redo_count_key",
        "command_key",    "target_key",      "world_key",   "element_key",        "face_index_key",    "bounds_min_key",
        "bounds_max_key", "source_path_key", "dirty_key",   "revision_key",       "saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys)) &&
           validate_optional_action_branches(ctx, action, json_path, names, false);
}

bool validate_editor_brush_color_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type)
{
    const char *target = json_string(action, "target");
    if (target == NULL)
        target = "selection";
    if (SDL_strcmp(target, "selection") != 0 && SDL_strcmp(target, "face") != 0)
        return validation_error(ctx, json_path, "%s target must be selection or face", type);

    yyjson_val *role = obj_get(action, "role");
    yyjson_val *color = obj_get(action, "color");
    yyjson_val *color_key = obj_get(action, "color_key");
    yyjson_val *tint = obj_get(action, "tint");
    yyjson_val *tint_key = obj_get(action, "tint_key");
    yyjson_val *tint_enabled = obj_get(action, "tint_enabled");
    if (role != NULL && (!yyjson_is_str(role) || yyjson_get_str(role)[0] == '\0'))
        return validation_error(ctx, json_path, "%s role must be a non-empty string", type);
    if (color != NULL && (!is_exact_vec3_or_vec4_array(color) || !numeric_array_values_in_range(color, 0.0, 255.0)))
        return validation_error(ctx, json_path, "%s color must be a numeric RGB or RGBA array in 0..255", type);
    if (color_key != NULL && (!yyjson_is_str(color_key) || yyjson_get_str(color_key)[0] == '\0'))
        return validation_error(ctx, json_path, "%s color_key must be a non-empty string", type);
    if (tint != NULL && (!is_exact_vec3_or_vec4_array(tint) || !numeric_array_values_in_range(tint, 0.0, 255.0)))
        return validation_error(ctx, json_path, "%s tint must be a numeric RGB or RGBA array in 0..255", type);
    if (tint_key != NULL && (!yyjson_is_str(tint_key) || yyjson_get_str(tint_key)[0] == '\0'))
        return validation_error(ctx, json_path, "%s tint_key must be a non-empty string", type);
    if (tint_enabled != NULL && !yyjson_is_bool(tint_enabled))
        return validation_error(ctx, json_path, "%s tint_enabled must be a boolean", type);
    if (role == NULL && color == NULL && color_key == NULL && tint == NULL && tint_key == NULL && tint_enabled == NULL)
        return validation_error(ctx, json_path, "%s requires color, color_key, role, tint, tint_key, or tint_enabled",
                                type);

    const char *output_keys[] = {
        "valid_key",      "event_key",       "message_key", "transaction_id_key", "undo_count_key",    "redo_count_key",
        "command_key",    "target_key",      "world_key",   "element_key",        "face_index_key",    "bounds_min_key",
        "bounds_max_key", "source_path_key", "dirty_key",   "revision_key",       "saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys)) &&
           validate_optional_action_branches(ctx, action, json_path, names, false);
}

bool validate_editor_inspector_brush_color_channel_action(validation_context *ctx, yyjson_val *action,
                                                          const char *json_path, validation_names *names,
                                                          const char *type)
{
    (void)names;
    yyjson_val *color_key = obj_get(action, "color_key");
    if (color_key != NULL && (!yyjson_is_str(color_key) || yyjson_get_str(color_key)[0] == '\0'))
        return validation_error(ctx, json_path, "%s color_key must be a non-empty string", type);
    yyjson_val *dirty_key = obj_get(action, "dirty_key");
    if (dirty_key != NULL && (!yyjson_is_str(dirty_key) || yyjson_get_str(dirty_key)[0] == '\0'))
        return validation_error(ctx, json_path, "%s dirty_key must be a non-empty string", type);
    yyjson_val *message = obj_get(action, "message");
    if (message != NULL && (!yyjson_is_str(message) || yyjson_get_str(message)[0] == '\0'))
        return validation_error(ctx, json_path, "%s message must be a non-empty string", type);

    yyjson_val *color = obj_get(action, "color");
    yyjson_val *channel = obj_get(action, "channel");
    yyjson_val *delta = obj_get(action, "delta");
    if (color != NULL)
    {
        if (!is_exact_vec3_or_vec4_array(color) || !numeric_array_values_in_range(color, 0.0, 255.0))
            return validation_error(ctx, json_path, "%s color must be a numeric RGB or RGBA array in 0..255", type);
        if (channel != NULL || delta != NULL)
            return validation_error(ctx, json_path, "%s color cannot be combined with channel or delta", type);
        return true;
    }

    if (channel == NULL || !yyjson_is_str(channel))
        return validation_error(ctx, json_path, "%s channel must be r, g, b, or a", type);
    const char *channel_text = yyjson_get_str(channel);
    if (channel_text == NULL || channel_text[1] != '\0' ||
        (channel_text[0] != 'r' && channel_text[0] != 'g' && channel_text[0] != 'b' && channel_text[0] != 'a'))
    {
        return validation_error(ctx, json_path, "%s channel must be r, g, b, or a", type);
    }
    if (delta == NULL || !yyjson_is_int(delta))
        return validation_error(ctx, json_path, "%s delta must be an integer", type);
    return true;
}

bool validate_editor_texture_scan_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         validation_names *names, const char *type)
{
    (void)names;
    static const char *const optional_strings[] = {"directory_key",
                                                   "relative_directory_key",
                                                   "directory",
                                                   "relative_directory",
                                                   "collection",
                                                   "catalog_collection",
                                                   "world",
                                                   "material_prefix",
                                                   "scroll_index_key",
                                                   "scroll_y_key"};
    for (size_t i = 0; i < SDL_arraysize(optional_strings); ++i)
    {
        yyjson_val *value = obj_get(action, optional_strings[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, json_path, "%s %s must be a non-empty string", type, optional_strings[i]);
    }
    yyjson_val *slot_count = obj_get(action, "slot_count");
    if (slot_count != NULL && (!yyjson_is_int(slot_count) || yyjson_get_int(slot_count) < 0))
        return validation_error(ctx, json_path, "%s slot_count must be non-negative", type);
    const char *output_keys[] = {"count_key", "registered_count_key", "status_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_texture_path_apply_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    (void)names;
    static const char *const optional_strings[] = {"path_key", "directory_key", "relative_directory_key",
                                                   "available_key", "status_key"};
    for (size_t i = 0; i < SDL_arraysize(optional_strings); ++i)
    {
        yyjson_val *value = obj_get(action, optional_strings[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, json_path, "%s %s must be a non-empty string", type, optional_strings[i]);
    }
    return true;
}

bool validate_editor_texture_select_index_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type)
{
    (void)names;
    yyjson_val *index = obj_get(action, "index");
    if (index != NULL && (!yyjson_is_int(index) || yyjson_get_int(index) < 0))
        return validation_error(ctx, json_path, "%s index must be non-negative", type);
    yyjson_val *index_key = obj_get(action, "index_key");
    if (index_key != NULL && (!yyjson_is_str(index_key) || yyjson_get_str(index_key)[0] == '\0'))
        return validation_error(ctx, json_path, "%s index_key must be a non-empty string", type);
    yyjson_val *index_offset_key = obj_get(action, "index_offset_key");
    if (index_offset_key != NULL && (!yyjson_is_str(index_offset_key) || yyjson_get_str(index_offset_key)[0] == '\0'))
        return validation_error(ctx, json_path, "%s index_offset_key must be a non-empty string", type);
    yyjson_val *collection = obj_get(action, "collection");
    if (collection != NULL && (!yyjson_is_str(collection) || yyjson_get_str(collection)[0] == '\0'))
        return validation_error(ctx, json_path, "%s collection must be a non-empty string", type);
    return true;
}

bool validate_editor_actor_scan_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type)
{
    (void)names;
    static const char *const optional_strings[] = {
        "directory_key", "relative_directory_key", "directory", "relative_directory", "collection", "model_prefix"};
    for (size_t i = 0; i < SDL_arraysize(optional_strings); ++i)
    {
        yyjson_val *value = obj_get(action, optional_strings[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, json_path, "%s %s must be a non-empty string", type, optional_strings[i]);
    }
    yyjson_val *slot_count = obj_get(action, "slot_count");
    if (slot_count != NULL && (!yyjson_is_int(slot_count) || yyjson_get_int(slot_count) < 0))
        return validation_error(ctx, json_path, "%s slot_count must be non-negative", type);
    const char *output_keys[] = {"count_key", "status_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_actor_select_index_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    return validate_editor_texture_select_index_action(ctx, action, json_path, names, type);
}

bool validate_editor_actor_place_selected_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type)
{
    yyjson_val *index_key = obj_get(action, "index_key");
    if (index_key != NULL && (!yyjson_is_str(index_key) || yyjson_get_str(index_key)[0] == '\0'))
        return validation_error(ctx, json_path, "%s index_key must be a non-empty string", type);
    yyjson_val *collection = obj_get(action, "collection");
    if (collection != NULL && (!yyjson_is_str(collection) || yyjson_get_str(collection)[0] == '\0'))
        return validation_error(ctx, json_path, "%s collection must be a non-empty string", type);
    const char *position_from = json_string(action, "position_from");
    if (position_from != NULL && SDL_strcmp(position_from, "selection_point") != 0 &&
        SDL_strcmp(position_from, "placement_preview") != 0)
    {
        return validation_error(ctx, json_path, "%s position_from must be selection_point or placement_preview", type);
    }
    const char *scene = json_string(action, "scene");
    if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
        return false;
    const char *output_keys[] = {"valid_key", "message_key", "actor_key", "dirty_key", "revision_key", "count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_connection_mark_source_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type)
{
    (void)names;
    static const char *const optional_strings[] = {"target", "target_from_state", "event", "event_from_state",
                                                   "message"};
    char field_path[PATH_BUFFER_SIZE];
    for (size_t i = 0; i < SDL_arraysize(optional_strings); ++i)
    {
        format_path(field_path, sizeof(field_path), "%s.%s", json_path, optional_strings[i]);
        if (!validate_optional_non_empty_string_value(ctx, obj_get(action, optional_strings[i]), field_path, type))
            return false;
    }
    if (obj_get(action, "target") != NULL && obj_get(action, "target_from_state") != NULL)
        return validation_error(ctx, json_path, "%s requires target or target_from_state, not both", type);
    if (obj_get(action, "event") != NULL && obj_get(action, "event_from_state") != NULL)
        return validation_error(ctx, json_path, "%s requires event or event_from_state, not both", type);
    const char *output_keys[] = {"valid_key",  "message_key", "connection_key", "source_key",
                                 "target_key", "dirty_key",   "revision_key",   "count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_connection_add_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                           validation_names *names, const char *type)
{
    (void)names;
    static const char *const optional_strings[] = {"id",          "id_prefix",
                                                   "from_entity", "from_entity_from_state",
                                                   "from_event",  "from_event_from_state",
                                                   "to_entity",   "to_entity_from_state",
                                                   "to_action",   "to_action_from_state",
                                                   "message"};
    char field_path[PATH_BUFFER_SIZE];
    for (size_t i = 0; i < SDL_arraysize(optional_strings); ++i)
    {
        format_path(field_path, sizeof(field_path), "%s.%s", json_path, optional_strings[i]);
        if (!validate_optional_non_empty_string_value(ctx, obj_get(action, optional_strings[i]), field_path, type))
            return false;
    }
    if (obj_get(action, "from_entity") != NULL && obj_get(action, "from_entity_from_state") != NULL)
        return validation_error(ctx, json_path, "%s requires from_entity or from_entity_from_state, not both", type);
    if (obj_get(action, "from_event") != NULL && obj_get(action, "from_event_from_state") != NULL)
        return validation_error(ctx, json_path, "%s requires from_event or from_event_from_state, not both", type);
    if (obj_get(action, "to_entity") != NULL && obj_get(action, "to_entity_from_state") != NULL)
        return validation_error(ctx, json_path, "%s requires to_entity or to_entity_from_state, not both", type);
    if (obj_get(action, "to_action") != NULL && obj_get(action, "to_action_from_state") != NULL)
        return validation_error(ctx, json_path, "%s requires to_action or to_action_from_state, not both", type);
    yyjson_val *to_from_selection = obj_get(action, "to_from_selection");
    if (to_from_selection != NULL && !yyjson_is_bool(to_from_selection))
        return validation_error(ctx, json_path, "%s to_from_selection must be bool", type);
    yyjson_val *from_external = obj_get(action, "from_external");
    yyjson_val *to_external = obj_get(action, "to_external");
    if (from_external != NULL && !yyjson_is_bool(from_external))
        return validation_error(ctx, json_path, "%s from_external must be bool", type);
    if (to_external != NULL && !yyjson_is_bool(to_external))
        return validation_error(ctx, json_path, "%s to_external must be bool", type);
    yyjson_val *properties = obj_get(action, "properties");
    if (properties != NULL && !yyjson_is_obj(properties))
        return validation_error(ctx, json_path, "%s properties must be an object", type);
    const char *output_keys[] = {"valid_key",  "message_key", "connection_key", "source_key",
                                 "target_key", "dirty_key",   "revision_key",   "count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

static bool validate_editor_property_target(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            const char *type)
{
    const char *target_type = json_string(action, "target_type");
    if (target_type == NULL)
        target_type = "selection";
    if (SDL_strcmp(target_type, "selection") != 0 && SDL_strcmp(target_type, "editor_actor") != 0)
        return validation_error(ctx, json_path, "%s target_type must be selection or editor_actor", type);

    yyjson_val *target = obj_get(action, "target");
    yyjson_val *target_from_state = obj_get(action, "target_from_state");
    if (target != NULL && (!yyjson_is_str(target) || yyjson_get_str(target)[0] == '\0'))
        return validation_error(ctx, json_path, "%s target must be a non-empty string", type);
    if (target_from_state != NULL &&
        (!yyjson_is_str(target_from_state) || yyjson_get_str(target_from_state)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "%s target_from_state must be a non-empty string", type);
    }
    if (target != NULL && target_from_state != NULL)
        return validation_error(ctx, json_path, "%s requires target or target_from_state, not both", type);
    if (SDL_strcmp(target_type, "editor_actor") == 0 && target == NULL && target_from_state == NULL)
        return validation_error(ctx, json_path, "%s editor_actor target requires target or target_from_state", type);
    return true;
}

static bool validate_editor_property_key(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         const char *type)
{
    yyjson_val *key = obj_get(action, "key");
    yyjson_val *key_from_state = obj_get(action, "key_from_state");
    if (key != NULL && (!yyjson_is_str(key) || yyjson_get_str(key)[0] == '\0'))
        return validation_error(ctx, json_path, "%s key must be a non-empty string", type);
    if (key_from_state != NULL && (!yyjson_is_str(key_from_state) || yyjson_get_str(key_from_state)[0] == '\0'))
        return validation_error(ctx, json_path, "%s key_from_state must be a non-empty string", type);
    if (key != NULL && key_from_state != NULL)
        return validation_error(ctx, json_path, "%s requires key or key_from_state, not both", type);
    if (key == NULL && key_from_state == NULL)
        return validation_error(ctx, json_path, "%s requires key or key_from_state", type);
    return true;
}

static bool validate_editor_property_value(validation_context *ctx, yyjson_val *action, const char *json_path,
                                           const char *type)
{
    yyjson_val *value = obj_get(action, "value");
    yyjson_val *value_from_state = obj_get(action, "value_from_state");
    yyjson_val *value_from_payload = obj_get(action, "value_from_payload");
    const int value_sources =
        (value != NULL ? 1 : 0) + (value_from_state != NULL ? 1 : 0) + (value_from_payload != NULL ? 1 : 0);
    if (value_sources != 1)
        return validation_error(ctx, json_path, "%s requires exactly one value source", type);
    if (value_from_state != NULL && (!yyjson_is_str(value_from_state) || yyjson_get_str(value_from_state)[0] == '\0'))
        return validation_error(ctx, json_path, "%s value_from_state must be a non-empty string", type);
    if (value_from_payload != NULL &&
        (!yyjson_is_str(value_from_payload) || yyjson_get_str(value_from_payload)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "%s value_from_payload must be a non-empty string", type);
    }
    if (value == NULL)
        return true;
    if (yyjson_is_bool(value) || yyjson_is_int(value) || yyjson_is_num(value) || yyjson_is_str(value))
        return true;
    if (is_exact_vec_array(value, 3) || is_exact_vec_array(value, 4))
        return true;
    return validation_error(ctx, json_path, "%s value must be a scalar, vec3, color, or value source", type);
}

bool validate_editor_property_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         validation_names *names, const char *type)
{
    (void)names;
    if (!validate_editor_property_target(ctx, action, json_path, type) ||
        !validate_editor_property_key(ctx, action, json_path, type) ||
        !validate_editor_property_value(ctx, action, json_path, type))
    {
        return false;
    }
    const char *output_keys[] = {"valid_key", "message_key",  "target_key", "key_key",
                                 "dirty_key", "revision_key", "count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_property_remove_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type)
{
    (void)names;
    if (!validate_editor_property_target(ctx, action, json_path, type) ||
        !validate_editor_property_key(ctx, action, json_path, type))
    {
        return false;
    }
    if (obj_get(action, "value") != NULL || obj_get(action, "value_from_state") != NULL ||
        obj_get(action, "value_from_payload") != NULL)
    {
        return validation_error(ctx, json_path, "%s cannot include a value source", type);
    }
    const char *output_keys[] = {"valid_key", "message_key",  "target_key", "key_key",
                                 "dirty_key", "revision_key", "count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_property_select_slot_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type)
{
    (void)names;
    yyjson_val *slot = obj_get(action, "slot");
    if (!yyjson_is_int(slot) || yyjson_get_int(slot) < 0)
        return validation_error(ctx, json_path, "%s requires non-negative integer slot", type);
    const char *focus = json_string(action, "focus");
    if (focus != NULL && SDL_strcmp(focus, "key") != 0 && SDL_strcmp(focus, "value") != 0)
        return validation_error(ctx, json_path, "%s focus must be key or value", type);
    return true;
}

bool validate_editor_selection_shear_selected_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                     validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    yyjson_val *bounds_min = obj_get(action, "bounds_min");
    yyjson_val *bounds_max = obj_get(action, "bounds_max");
    if ((bounds_min == NULL) != (bounds_max == NULL))
        return validation_error(ctx, json_path,
                                "editor.selection.shear_selected requires both bounds_min and bounds_max");
    if (bounds_min != NULL && !is_vec_array(bounds_min, 3))
        return validation_error(ctx, json_path, "editor.selection.shear_selected bounds_min must be a numeric vec3");
    if (bounds_max != NULL && !is_vec_array(bounds_max, 3))
        return validation_error(ctx, json_path, "editor.selection.shear_selected bounds_max must be a numeric vec3");
    yyjson_val *side_normal = obj_get(action, "side_normal");
    if (!is_vec_array(side_normal, 3))
        return validation_error(ctx, json_path, "editor.selection.shear_selected side_normal must be a numeric vec3");
    const float normal_x = (float)yyjson_get_num(yyjson_arr_get(side_normal, 0));
    const float normal_y = (float)yyjson_get_num(yyjson_arr_get(side_normal, 1));
    const float normal_z = (float)yyjson_get_num(yyjson_arr_get(side_normal, 2));
    if (!validation_float_finite(normal_x) || !validation_float_finite(normal_y) ||
        !validation_float_finite(normal_z) ||
        normal_x * normal_x + normal_y * normal_y + normal_z * normal_z <= 0.000001f)
    {
        return validation_error(ctx, json_path,
                                "editor.selection.shear_selected side_normal must be non-zero and finite");
    }
    yyjson_val *delta = obj_get(action, "delta");
    if (!is_vec_array(delta, 3))
        return validation_error(ctx, json_path, "editor.selection.shear_selected delta must be a numeric vec3");
    const float delta_x = (float)yyjson_get_num(yyjson_arr_get(delta, 0));
    const float delta_y = (float)yyjson_get_num(yyjson_arr_get(delta, 1));
    const float delta_z = (float)yyjson_get_num(yyjson_arr_get(delta, 2));
    if (!validation_float_finite(delta_x) || !validation_float_finite(delta_y) || !validation_float_finite(delta_z) ||
        delta_x * delta_x + delta_y * delta_y + delta_z * delta_z <= 0.000001f)
    {
        return validation_error(ctx, json_path, "editor.selection.shear_selected delta must be non-zero and finite");
    }
    return true;
}

bool validate_editor_selection_run_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type)
{
    (void)type;
    return validate_optional_action_branches(ctx, action, json_path, names, true);
}

bool validate_editor_command_preview_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    const char *command = json_string(action, "command");
    const char *target = json_string(action, "target");
    if (target == NULL)
        target = "selection";
    if (!editor_command_name_valid(command))
        return validation_error(ctx, json_path,
                                "editor.command.preview command must be translate, rotate, paint, resize, extrude, or "
                                "delete");
    if (!editor_command_target_name_valid(target))
        return validation_error(ctx, json_path,
                                "editor.command.preview target must be selection, world, element, face, or material");
    if ((SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0) && SDL_strcmp(target, "face") != 0)
    {
        return validation_error(ctx, json_path, "editor.command.preview resize/extrude target must be face");
    }
    yyjson_val *material = obj_get(action, "material");
    yyjson_val *material_key = obj_get(action, "material_key");
    if (SDL_strcmp(command, "paint") == 0 && material == NULL && material_key == NULL)
        return validation_error(ctx, json_path, "editor.command.preview paint requires material or material_key");
    if (material != NULL && !yyjson_is_str(material))
        return validation_error(ctx, json_path, "editor.command.preview material must be a string");
    if (material_key != NULL && (!yyjson_is_str(material_key) || yyjson_get_str(material_key)[0] == '\0'))
        return validation_error(ctx, json_path, "editor.command.preview material_key must be a non-empty string");
    yyjson_val *offset = obj_get(action, "offset");
    if (offset != NULL && !is_vec_array(offset, 3))
        return validation_error(ctx, json_path, "editor.command.preview offset must be a vec3");
    yyjson_val *distance = obj_get(action, "distance");
    if (distance != NULL && !yyjson_is_num(distance))
        return validation_error(ctx, json_path, "editor.command.preview distance must be numeric");
    yyjson_val *message = obj_get(action, "message");
    if (message != NULL && !yyjson_is_str(message))
        return validation_error(ctx, json_path, "editor.command.preview message must be a string");
    const char *output_keys[] = {"active_key", "valid_key",   "command_key",    "target_key",     "message_key",
                                 "world_key",  "element_key", "face_index_key", "bounds_min_key", "bounds_max_key"};
    return validate_optional_output_keys(ctx, action, json_path, "editor.command.preview", output_keys,
                                         SDL_arraysize(output_keys));
}

bool validate_editor_command_clear_preview_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                  validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    yyjson_val *message = obj_get(action, "message");
    if (message != NULL && !yyjson_is_str(message))
        return validation_error(ctx, json_path, "editor.command.clear_preview message must be a string");
    const char *output_keys[] = {"active_key", "valid_key",   "command_key",    "target_key",     "message_key",
                                 "world_key",  "element_key", "face_index_key", "bounds_min_key", "bounds_max_key"};
    return validate_optional_output_keys(ctx, action, json_path, "editor.command.clear_preview", output_keys,
                                         SDL_arraysize(output_keys));
}

bool validate_editor_command_history_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type)
{
    yyjson_val *message = obj_get(action, "message");
    yyjson_val *invalid_message = obj_get(action, "invalid_message");
    if (message != NULL && !yyjson_is_str(message))
        return validation_error(ctx, json_path, "%s message must be a string", type);
    if (invalid_message != NULL && !yyjson_is_str(invalid_message))
        return validation_error(ctx, json_path, "%s invalid_message must be a string", type);
    const char *output_keys[] = {
        "valid_key",      "event_key",       "message_key", "transaction_id_key", "undo_count_key",    "redo_count_key",
        "command_key",    "target_key",      "world_key",   "element_key",        "face_index_key",    "bounds_min_key",
        "bounds_max_key", "source_path_key", "dirty_key",   "revision_key",       "saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys)) &&
           validate_optional_action_branches(ctx, action, json_path, names, false);
}

bool validate_editor_brush_world_export_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
        return false;
    const char *output_keys[] = {"valid_key",       "message_key", "json_key",     "size_key",          "world_key",
                                 "source_path_key", "dirty_key",   "revision_key", "saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_level_export_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         validation_names *names, const char *type)
{
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
        return false;
    const char *output_keys[] = {"valid_key",
                                 "message_key",
                                 "json_key",
                                 "size_key",
                                 "brush_world_key",
                                 "brush_source_path_key",
                                 "brush_dirty_key",
                                 "brush_revision_key",
                                 "brush_saved_revision_key",
                                 "player_start_source_path_key",
                                 "player_start_count_key",
                                 "player_start_dirty_key",
                                 "player_start_revision_key",
                                 "player_start_saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_level_path_pair(validation_context *ctx, yyjson_val *action, const char *json_path,
                                     const char *type)
{
    yyjson_val *path = obj_get(action, "path");
    yyjson_val *path_from_state = obj_get(action, "path_from_state");
    if ((path == NULL && path_from_state == NULL) || (path != NULL && path_from_state != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of path or path_from_state", type);
    if (path != NULL && (!yyjson_is_str(path) || yyjson_get_str(path)[0] == '\0'))
        return validation_error(ctx, json_path, "%s path must be a non-empty string", type);
    if (path_from_state != NULL && (!yyjson_is_str(path_from_state) || yyjson_get_str(path_from_state)[0] == '\0'))
        return validation_error(ctx, json_path, "%s path_from_state must be a non-empty string", type);
    return true;
}

bool validate_editor_level_save_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type)
{
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path) ||
        !validate_editor_level_path_pair(ctx, action, json_path, type))
    {
        return false;
    }
    const char *output_keys[] = {"valid_key",
                                 "message_key",
                                 "path_key",
                                 "size_key",
                                 "brush_world_key",
                                 "brush_source_path_key",
                                 "brush_dirty_key",
                                 "brush_revision_key",
                                 "brush_saved_revision_key",
                                 "player_start_source_path_key",
                                 "player_start_count_key",
                                 "player_start_dirty_key",
                                 "player_start_revision_key",
                                 "player_start_saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_level_load_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type)
{
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path) ||
        !validate_editor_level_path_pair(ctx, action, json_path, type))
    {
        return false;
    }
    yyjson_val *optional = obj_get(action, "optional");
    if (optional != NULL && !yyjson_is_bool(optional))
        return validation_error(ctx, json_path, "editor.level.load optional must be a boolean");
    const char *output_keys[] = {"valid_key",
                                 "message_key",
                                 "path_key",
                                 "brush_world_key",
                                 "brush_source_path_key",
                                 "brush_dirty_key",
                                 "brush_revision_key",
                                 "brush_saved_revision_key",
                                 "player_start_source_path_key",
                                 "player_start_count_key",
                                 "player_start_dirty_key",
                                 "player_start_revision_key",
                                 "player_start_saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_map_export_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type)
{
    return validate_editor_level_export_action(ctx, action, json_path, names, type);
}

bool validate_editor_map_new_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                    validation_names *names, const char *type)
{
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
        return false;
    if (!is_non_empty_string(action, "fragment"))
        return validation_error(ctx, json_path, "%s requires a non-empty fragment string", type);
    const char *output_keys[] = {"valid_key",
                                 "message_key",
                                 "path_key",
                                 "brush_world_key",
                                 "brush_source_path_key",
                                 "brush_dirty_key",
                                 "brush_revision_key",
                                 "brush_saved_revision_key",
                                 "player_start_source_path_key",
                                 "player_start_count_key",
                                 "player_start_dirty_key",
                                 "player_start_revision_key",
                                 "player_start_saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_map_save_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                     validation_names *names, const char *type)
{
    return validate_editor_level_save_action(ctx, action, json_path, names, type);
}

bool validate_editor_map_load_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                     validation_names *names, const char *type)
{
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path) ||
        !validate_editor_level_path_pair(ctx, action, json_path, type))
    {
        return false;
    }
    yyjson_val *optional = obj_get(action, "optional");
    if (optional != NULL && !yyjson_is_bool(optional))
        return validation_error(ctx, json_path, "%s optional must be a boolean", type);
    const char *output_keys[] = {"valid_key",
                                 "message_key",
                                 "path_key",
                                 "brush_world_key",
                                 "brush_source_path_key",
                                 "brush_dirty_key",
                                 "brush_revision_key",
                                 "brush_saved_revision_key",
                                 "player_start_source_path_key",
                                 "player_start_count_key",
                                 "player_start_dirty_key",
                                 "player_start_revision_key",
                                 "player_start_saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_map_validate_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         validation_names *names, const char *type)
{
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
        return false;
    yyjson_val *treat_warnings_as_errors = obj_get(action, "treat_warnings_as_errors");
    if (treat_warnings_as_errors != NULL && !yyjson_is_bool(treat_warnings_as_errors))
        return validation_error(ctx, json_path, "%s treat_warnings_as_errors must be a boolean", type);
    const char *output_keys[] = {"valid_key",
                                 "message_key",
                                 "warning_count_key",
                                 "error_count_key",
                                 "diagnostic0_key",
                                 "diagnostic1_key",
                                 "diagnostic2_key",
                                 "size_key",
                                 "brush_world_key",
                                 "brush_source_path_key",
                                 "brush_dirty_key",
                                 "brush_revision_key",
                                 "brush_saved_revision_key",
                                 "player_start_source_path_key",
                                 "player_start_count_key",
                                 "player_start_dirty_key",
                                 "player_start_revision_key",
                                 "player_start_saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_test_run_common(validation_context *ctx, yyjson_val *action, const char *json_path,
                                     validation_names *names, const char *type, bool require_path)
{
    if (!is_non_empty_string(action, "data_asset"))
        return validation_error(ctx, json_path, "%s requires a non-empty data_asset", type);
    const char *string_fields[] = {"runner", "root", "pack", "media"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        yyjson_val *field = obj_get(action, string_fields[i]);
        if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
            return validation_error(ctx, json_path, "%s command fields must be non-empty strings", type);
    }
    yyjson_val *embedded = obj_get(action, "embedded");
    if (embedded != NULL && !yyjson_is_bool(embedded))
        return validation_error(ctx, json_path, "%s embedded must be a boolean", type);
    const int mount_count = (obj_get(action, "root") != NULL ? 1 : 0) + (obj_get(action, "pack") != NULL ? 1 : 0) +
                            (embedded != NULL && yyjson_get_bool(embedded) ? 1 : 0);
    if (mount_count > 1)
        return validation_error(ctx, json_path, "%s accepts at most one of root, pack, or embedded", type);
    if (require_path && !validate_editor_level_path_pair(ctx, action, json_path, type))
        return false;
    const char *scene = json_string(action, "scene");
    if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
        return false;
    yyjson_val *player_start = obj_get(action, "player_start");
    if (player_start != NULL && (!yyjson_is_str(player_start) || yyjson_get_str(player_start)[0] == '\0'))
        return validation_error(ctx, json_path, "%s player_start must be a non-empty string", type);
    if (player_start != NULL &&
        !require_ref(ctx, &names->editor_player_starts, "editor player start", yyjson_get_str(player_start), json_path))
    {
        return false;
    }
    if (scene == NULL && player_start == NULL)
        return validation_error(ctx, json_path, "%s requires scene or player_start", type);
    return true;
}

bool validate_editor_test_run_prepare_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                             validation_names *names, const char *type)
{
    if (!validate_editor_test_run_common(ctx, action, json_path, names, type, false))
        return false;
    const char *output_keys[] = {"valid_key", "message_key",      "manifest_json_key", "size_key",   "data_asset_key",
                                 "scene_key", "player_start_key", "target_key",        "command_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_test_run_save_manifest_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type)
{
    if (!validate_editor_test_run_common(ctx, action, json_path, names, type, true))
        return false;
    const char *output_keys[] = {"valid_key",  "message_key",    "path_key",  "manifest_json_key",
                                 "size_key",   "data_asset_key", "scene_key", "player_start_key",
                                 "target_key", "command_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_brush_world_status_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
        return false;
    yyjson_val *message = obj_get(action, "message");
    if (message != NULL && !yyjson_is_str(message))
        return validation_error(ctx, json_path, "%s message must be a string", type);
    const char *output_keys[] = {"valid_key", "message_key",  "world_key",         "source_path_key",
                                 "dirty_key", "revision_key", "saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_brush_world_validate_source_action(validation_context *ctx, yyjson_val *action,
                                                        const char *json_path, validation_names *names,
                                                        const char *type)
{
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
        return false;
    yyjson_val *message = obj_get(action, "message");
    if (message != NULL && !yyjson_is_str(message))
        return validation_error(ctx, json_path, "%s message must be a string", type);
    yyjson_val *near_gap_units = obj_get(action, "near_gap_units");
    if (near_gap_units != NULL && (!yyjson_is_int(near_gap_units) || yyjson_get_int(near_gap_units) < 0))
        return validation_error(ctx, json_path, "%s near_gap_units must be a non-negative integer", type);
    if (obj_get(action, "allow_missing_source") != NULL)
        return validation_error(ctx, json_path, "%s allow_missing_source is no longer supported", type);
    const char *output_keys[] = {"valid_key",
                                 "message_key",
                                 "box_count_key",
                                 "snap_units_key",
                                 "off_snap_count_key",
                                 "overlap_count_key",
                                 "near_gap_count_key",
                                 "face_contact_count_key",
                                 "edge_contact_count_key",
                                 "vertex_contact_count_key",
                                 "partial_face_contact_count_key",
                                 "runtime_brush_count_key",
                                 "runtime_source_mismatch_count_key",
                                 "compiled_face_count_key",
                                 "compiled_face_missing_source_count_key",
                                 "compiled_face_unknown_source_count_key",
                                 "first_issue_kind_key",
                                 "first_issue_source_name_key",
                                 "first_issue_source_stable_id_key",
                                 "first_issue_related_source_name_key",
                                 "first_issue_related_source_stable_id_key",
                                 "first_issue_source_face_key",
                                 "first_issue_runtime_brush_name_key",
                                 "first_issue_runtime_brush_index_key",
                                 "first_issue_compiled_face_index_key",
                                 "world_key",
                                 "source_path_key",
                                 "dirty_key",
                                 "revision_key",
                                 "saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_brush_world_validate_enclosure_action(validation_context *ctx, yyjson_val *action,
                                                           const char *json_path, validation_names *names,
                                                           const char *type)
{
    if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
        return false;
    const char *player_start = json_string(action, "player_start");
    if (player_start == NULL || player_start[0] == '\0')
        return validation_error(ctx, json_path, "%s requires a player_start", type);
    yyjson_val *allow_missing_player_start = obj_get(action, "allow_missing_player_start");
    if (allow_missing_player_start != NULL && !yyjson_is_bool(allow_missing_player_start))
        return validation_error(ctx, json_path, "%s allow_missing_player_start must be a boolean", type);
    const bool permits_missing_player_start =
        allow_missing_player_start != NULL && yyjson_get_bool(allow_missing_player_start);
    if (!permits_missing_player_start &&
        !require_ref(ctx, &names->editor_player_starts, "editor player start", player_start, json_path))
    {
        return false;
    }
    yyjson_val *message = obj_get(action, "message");
    if (message != NULL && !yyjson_is_str(message))
        return validation_error(ctx, json_path, "%s message must be a string", type);
    if (obj_get(action, "allow_missing_source") != NULL)
        return validation_error(ctx, json_path, "%s allow_missing_source is no longer supported", type);
    yyjson_val *max_cells = obj_get(action, "max_cells");
    if (max_cells != NULL && (!yyjson_is_int(max_cells) || yyjson_get_int(max_cells) <= 0))
        return validation_error(ctx, json_path, "%s max_cells must be a positive integer", type);
    const char *output_keys[] = {"valid_key",
                                 "message_key",
                                 "has_source_key",
                                 "has_player_start_key",
                                 "box_count_key",
                                 "grid_cell_count_key",
                                 "solid_cell_count_key",
                                 "visited_cell_count_key",
                                 "open_boundary_count_key",
                                 "leak_point_key",
                                 "leak_axis_key",
                                 "leak_side_key",
                                 "candidate_name_key",
                                 "candidate_stable_id_key",
                                 "candidate_face_key",
                                 "candidate_point_key",
                                 "candidate_distance_key",
                                 "world_key",
                                 "source_path_key",
                                 "dirty_key",
                                 "revision_key",
                                 "saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_brush_world_create_box_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type)
{
    const char *position_from = json_string(action, "position_from");
    const bool from_preview = position_from != NULL && SDL_strcmp(position_from, "placement_preview") == 0;
    if (!from_preview &&
        !require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
        return false;
    if (!from_preview && !is_non_empty_string(action, "material"))
        return validation_error(ctx, json_path, "%s requires a non-empty material", type);
    if (!validate_non_empty_string_field(ctx, action, json_path, type, "name") ||
        !validate_non_empty_string_field(ctx, action, json_path, type, "preview_mode"))
    {
        return false;
    }
    yyjson_val *min = obj_get(action, "min");
    yyjson_val *max = obj_get(action, "max");
    if (!from_preview && (!is_exact_vec_array(min, 3) || !is_exact_vec_array(max, 3)))
        return validation_error(ctx, json_path, "%s requires min and max vec3 values", type);
    if (position_from != NULL && SDL_strcmp(position_from, "selection_point") != 0 &&
        SDL_strcmp(position_from, "placement_preview") != 0)
    {
        return validation_error(ctx, json_path, "%s position_from must be selection_point or placement_preview", type);
    }
    yyjson_val *position_offset = obj_get(action, "position_offset");
    if (position_offset != NULL && !is_exact_vec_array(position_offset, 3))
        return validation_error(ctx, json_path, "%s position_offset must be a vec3", type);
    yyjson_val *snap = obj_get(action, "snap");
    if (snap != NULL && (!yyjson_is_num(snap) || yyjson_get_num(snap) <= 0.0))
        return validation_error(ctx, json_path, "%s snap must be a positive number", type);
    char contents_path[PATH_BUFFER_SIZE];
    format_path(contents_path, sizeof(contents_path), "%s.contents", json_path);
    if (!validate_brush_string_or_string_array(ctx, obj_get(action, "contents"), contents_path,
                                               "editor.brush_world.create_box contents", brush_content_name_valid,
                                               false))
    {
        return false;
    }
    if (!from_preview)
    {
        const double min_x = yyjson_get_num(yyjson_arr_get(min, 0));
        const double min_y = yyjson_get_num(yyjson_arr_get(min, 1));
        const double min_z = yyjson_get_num(yyjson_arr_get(min, 2));
        const double max_x = yyjson_get_num(yyjson_arr_get(max, 0));
        const double max_y = yyjson_get_num(yyjson_arr_get(max, 1));
        const double max_z = yyjson_get_num(yyjson_arr_get(max, 2));
        if (!(min_x < max_x && min_y < max_y && min_z < max_z))
            return validation_error(ctx, json_path, "%s bounds require min < max", type);
    }
    const char *output_keys[] = {"valid_key", "message_key",  "brush_key",          "world_key",      "source_path_key",
                                 "dirty_key", "revision_key", "saved_revision_key", "bounds_min_key", "bounds_max_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_player_start_outputs(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          const char *type)
{
    const char *output_keys[] = {"valid_key",  "message_key",  "player_start_key",  "scene_key",
                                 "target_key", "position_key", "yaw_key",           "pitch_key",
                                 "dirty_key",  "revision_key", "saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_player_start_place_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    if (!is_non_empty_string(action, "name"))
        return validation_error(ctx, json_path, "%s requires a non-empty name", type);
    const char *scene = json_string(action, "scene");
    const char *target = json_string(action, "target");
    if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
        return false;
    if (target != NULL && !require_actor_ref(ctx, names, target, json_path))
        return false;
    yyjson_val *position = obj_get(action, "position");
    if (position != NULL && !is_exact_vec_array(position, 3))
        return validation_error(ctx, json_path, "%s position must be a vec3", type);
    const char *position_from = json_string(action, "position_from");
    if (position != NULL && position_from != NULL)
        return validation_error(ctx, json_path, "%s requires position or position_from, not both", type);
    if (!validate_non_empty_string_field(ctx, action, json_path, type, "preview_mode"))
        return false;
    if (position_from != NULL && SDL_strcmp(position_from, "selection_point") != 0 &&
        SDL_strcmp(position_from, "placement_preview") != 0)
    {
        return validation_error(ctx, json_path, "%s position_from must be selection_point or placement_preview", type);
    }
    yyjson_val *yaw = obj_get(action, "yaw");
    yyjson_val *pitch = obj_get(action, "pitch");
    yyjson_val *apply_to_target = obj_get(action, "apply_to_target");
    if ((yaw != NULL && !yyjson_is_num(yaw)) || (pitch != NULL && !yyjson_is_num(pitch)) ||
        (apply_to_target != NULL && !yyjson_is_bool(apply_to_target)))
    {
        return validation_error(ctx, json_path, "%s yaw/pitch must be numeric and apply_to_target bool", type);
    }
    return validate_editor_player_start_outputs(ctx, action, json_path, type);
}

bool validate_editor_player_start_apply_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    if (!is_non_empty_string(action, "name"))
        return validation_error(ctx, json_path, "%s requires a non-empty name", type);
    const char *name = json_string(action, "name");
    yyjson_val *allow_missing_player_start = obj_get(action, "allow_missing_player_start");
    if (allow_missing_player_start != NULL && !yyjson_is_bool(allow_missing_player_start))
        return validation_error(ctx, json_path, "%s allow_missing_player_start must be a boolean", type);
    const bool permits_missing_player_start =
        allow_missing_player_start != NULL && yyjson_get_bool(allow_missing_player_start);
    if (!permits_missing_player_start &&
        !require_ref(ctx, &names->editor_player_starts, "editor player start", name, json_path))
    {
        return false;
    }
    return validate_editor_player_start_outputs(ctx, action, json_path, type);
}

bool validate_editor_player_start_delete_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names, const char *type)
{
    const char *name = json_string(action, "name");
    yyjson_val *name_from_selection_value = obj_get(action, "name_from_selection");
    if (name_from_selection_value != NULL && !yyjson_is_bool(name_from_selection_value))
        return validation_error(ctx, json_path, "%s name_from_selection must be bool", type);
    const bool name_from_selection = name_from_selection_value != NULL && yyjson_get_bool(name_from_selection_value);
    if ((name == NULL || name[0] == '\0') && !name_from_selection)
        return validation_error(ctx, json_path, "%s requires name or name_from_selection", type);
    if (name != NULL && !require_ref(ctx, &names->editor_player_starts, "editor player start", name, json_path))
        return false;
    const char *output_keys[] = {"valid_key", "message_key",  "player_start_key",
                                 "dirty_key", "revision_key", "saved_revision_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_actor_place_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type)
{
    const char *name = json_string(action, "name");
    const char *name_prefix = json_string(action, "name_prefix");
    if ((name == NULL || name[0] == '\0') && (name_prefix == NULL || name_prefix[0] == '\0'))
        return validation_error(ctx, json_path, "%s requires name or name_prefix", type);
    const char *scene = json_string(action, "scene");
    if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
        return false;
    static const char *const optional_string_fields[] = {"name",  "name_prefix", "display_name",  "archetype", "mesh",
                                                         "model", "group",       "position_from", "message"};
    char field_path[PATH_BUFFER_SIZE];
    for (size_t i = 0; i < SDL_arraysize(optional_string_fields); ++i)
    {
        format_path(field_path, sizeof(field_path), "%s.%s", json_path, optional_string_fields[i]);
        if (!validate_optional_non_empty_string_value(ctx, obj_get(action, optional_string_fields[i]), field_path,
                                                      "editor.actor.place field"))
            return false;
    }
    const char *position_from = json_string(action, "position_from");
    if (position_from != NULL && SDL_strcmp(position_from, "selection_point") != 0 &&
        SDL_strcmp(position_from, "placement_preview") != 0)
    {
        return validation_error(ctx, json_path, "%s position_from must be selection_point or placement_preview", type);
    }
    yyjson_val *position = obj_get(action, "position");
    if (position != NULL && !is_exact_vec_array(position, 3))
        return validation_error(ctx, json_path, "%s position must be a vec3", type);
    if (position != NULL && position_from != NULL)
        return validation_error(ctx, json_path, "%s requires position or position_from, not both", type);
    yyjson_val *rotation = obj_get(action, "rotation");
    yyjson_val *scale = obj_get(action, "scale");
    yyjson_val *color = obj_get(action, "color");
    yyjson_val *properties = obj_get(action, "properties");
    if (rotation != NULL && !is_exact_vec_array(rotation, 3))
        return validation_error(ctx, json_path, "%s rotation must be a vec3", type);
    if (scale != NULL &&
        (!is_exact_vec_array(scale, 3) || yyjson_get_num(yyjson_arr_get(scale, 0)) <= 0.0 ||
         yyjson_get_num(yyjson_arr_get(scale, 1)) <= 0.0 || yyjson_get_num(yyjson_arr_get(scale, 2)) <= 0.0))
        return validation_error(ctx, json_path, "%s scale must be a positive vec3", type);
    if (color != NULL && !is_exact_vec3_or_vec4_array(color))
        return validation_error(ctx, json_path, "%s color must be a color array", type);
    if (properties != NULL && !yyjson_is_obj(properties))
        return validation_error(ctx, json_path, "%s properties must be an object", type);
    const char *output_keys[] = {"valid_key", "message_key", "actor_key", "dirty_key", "revision_key", "count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_actor_update_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         validation_names *names, const char *type)
{
    (void)names;
    const char *target_type = json_string(action, "target_type");
    if (target_type == NULL || target_type[0] == '\0')
        target_type = "selection";
    if (target_type == NULL ||
        (SDL_strcmp(target_type, "selection") != 0 && SDL_strcmp(target_type, "editor_actor") != 0))
        return validation_error(ctx, json_path, "%s target_type must be selection or editor_actor", type);
    const char *target = json_string(action, "target");
    const char *target_from_state = json_string(action, "target_from_state");
    if (SDL_strcmp(target_type, "editor_actor") == 0 && (target == NULL || target[0] == '\0') &&
        (target_from_state == NULL || target_from_state[0] == '\0'))
        return validation_error(ctx, json_path, "%s requires target or target_from_state for editor_actor", type);
    static const char *const optional_string_fields[] = {"target",    "target_from_state", "target_type",
                                                         "color_key", "display_mode",      "message"};
    char field_path[PATH_BUFFER_SIZE];
    for (size_t i = 0; i < SDL_arraysize(optional_string_fields); ++i)
    {
        format_path(field_path, sizeof(field_path), "%s.%s", json_path, optional_string_fields[i]);
        if (!validate_optional_non_empty_string_value(ctx, obj_get(action, optional_string_fields[i]), field_path,
                                                      "editor.actor.update field"))
            return false;
    }
    yyjson_val *position = obj_get(action, "position");
    yyjson_val *rotation = obj_get(action, "rotation");
    yyjson_val *scale = obj_get(action, "scale");
    yyjson_val *color = obj_get(action, "color");
    const char *color_key = json_string(action, "color_key");
    if (position != NULL && !is_exact_vec_array(position, 3))
        return validation_error(ctx, json_path, "%s position must be a vec3", type);
    if (rotation != NULL && !is_exact_vec_array(rotation, 3))
        return validation_error(ctx, json_path, "%s rotation must be a vec3", type);
    if (scale != NULL &&
        (!is_exact_vec_array(scale, 3) || yyjson_get_num(yyjson_arr_get(scale, 0)) <= 0.0 ||
         yyjson_get_num(yyjson_arr_get(scale, 1)) <= 0.0 || yyjson_get_num(yyjson_arr_get(scale, 2)) <= 0.0))
        return validation_error(ctx, json_path, "%s scale must be a positive vec3", type);
    if (color != NULL && !is_exact_vec3_or_vec4_array(color))
        return validation_error(ctx, json_path, "%s color must be a color array", type);
    if (color != NULL && color_key != NULL)
        return validation_error(ctx, json_path, "%s requires color or color_key, not both", type);
    const char *display_mode = json_string(action, "display_mode");
    if (display_mode != NULL && SDL_strcmp(display_mode, "solid") != 0 && SDL_strcmp(display_mode, "wireframe") != 0)
        return validation_error(ctx, json_path, "%s display_mode must be solid or wireframe", type);
    const char *output_keys[] = {"valid_key", "message_key", "actor_key", "dirty_key", "revision_key", "count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

static bool validate_editor_prefab_common_fields(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 const char *type, bool require_id)
{
    const char *id = json_string(action, "id");
    const char *prefab = json_string(action, "prefab");
    if (require_id && (id == NULL || id[0] == '\0'))
        return validation_error(ctx, json_path, "%s requires a non-empty id", type);
    if (!require_id && (prefab == NULL || prefab[0] == '\0') && (id == NULL || id[0] == '\0'))
        return validation_error(ctx, json_path, "%s requires prefab or id", type);
    static const char *const optional_string_fields[] = {"id",        "prefab",   "name",  "name_prefix",
                                                         "label",     "category", "kind",  "group",
                                                         "archetype", "mesh",     "model", "message"};
    char field_path[PATH_BUFFER_SIZE];
    for (size_t i = 0; i < SDL_arraysize(optional_string_fields); ++i)
    {
        format_path(field_path, sizeof(field_path), "%s.%s", json_path, optional_string_fields[i]);
        if (!validate_optional_non_empty_string_value(ctx, obj_get(action, optional_string_fields[i]), field_path,
                                                      "editor prefab field"))
            return false;
    }
    yyjson_val *position = obj_get(action, "position");
    yyjson_val *rotation = obj_get(action, "rotation");
    yyjson_val *scale = obj_get(action, "scale");
    yyjson_val *color = obj_get(action, "color");
    yyjson_val *properties = obj_get(action, "properties");
    yyjson_val *overrides = obj_get(action, "prefab_overrides");
    if (position != NULL && !is_exact_vec_array(position, 3))
        return validation_error(ctx, json_path, "%s position must be a vec3", type);
    if (rotation != NULL && !is_exact_vec_array(rotation, 3))
        return validation_error(ctx, json_path, "%s rotation must be a vec3", type);
    if (scale != NULL &&
        (!is_exact_vec_array(scale, 3) || yyjson_get_num(yyjson_arr_get(scale, 0)) <= 0.0 ||
         yyjson_get_num(yyjson_arr_get(scale, 1)) <= 0.0 || yyjson_get_num(yyjson_arr_get(scale, 2)) <= 0.0))
        return validation_error(ctx, json_path, "%s scale must be a positive vec3", type);
    if (color != NULL && !is_exact_vec3_or_vec4_array(color))
        return validation_error(ctx, json_path, "%s color must be a color array", type);
    if (properties != NULL && !yyjson_is_obj(properties))
        return validation_error(ctx, json_path, "%s properties must be an object", type);
    if (overrides != NULL && !yyjson_is_obj(overrides))
        return validation_error(ctx, json_path, "%s prefab_overrides must be an object", type);
    const char *output_keys[] = {"valid_key", "message_key",  "prefab_key", "actor_key",
                                 "dirty_key", "revision_key", "count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

bool validate_editor_prefab_define_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type)
{
    (void)names;
    return validate_editor_prefab_common_fields(ctx, action, json_path, type, true);
}

bool validate_editor_prefab_instantiate_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    const char *scene = json_string(action, "scene");
    if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
        return false;
    return validate_editor_prefab_common_fields(ctx, action, json_path, type, false);
}

bool validate_editor_prefab_unlink_actor_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names, const char *type)
{
    (void)names;
    const char *actor = json_string(action, "actor");
    const char *target = json_string(action, "target");
    yyjson_val *actor_from_selection = obj_get(action, "actor_from_selection");
    if ((actor == NULL || actor[0] == '\0') && (target == NULL || target[0] == '\0') &&
        (actor_from_selection == NULL || !yyjson_get_bool(actor_from_selection)))
    {
        return validation_error(ctx, json_path, "%s requires actor, target, or actor_from_selection", type);
    }
    if (actor_from_selection != NULL && !yyjson_is_bool(actor_from_selection))
        return validation_error(ctx, json_path, "%s actor_from_selection must be a boolean", type);
    const char *output_keys[] = {"valid_key", "message_key",  "prefab_key", "actor_key",
                                 "dirty_key", "revision_key", "count_key"};
    return validate_optional_output_keys(ctx, action, json_path, type, output_keys, SDL_arraysize(output_keys));
}

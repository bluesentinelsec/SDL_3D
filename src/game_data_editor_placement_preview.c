#include "game_data_internal.h"

void clear_editor_placement_preview(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    SDL_zero(runtime->editor_placement_preview);
}

static void clear_editor_drag_create(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    SDL_zero(runtime->editor_drag_create);
}

bool editor_placement_preview_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || !runtime->editor_placement_preview.active)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return active_scene != NULL && runtime->editor_placement_preview.scene != NULL &&
           SDL_strcmp(runtime->editor_placement_preview.scene, active_scene) == 0;
}

static yyjson_val *find_editor_placement_preview_json(yyjson_val *placement, const char *mode)
{
    yyjson_val *previews = obj_get(placement, "previews");
    for (size_t i = 0; yyjson_is_arr(previews) && i < yyjson_arr_size(previews); ++i)
    {
        yyjson_val *preview = yyjson_arr_get(previews, i);
        if (SDL_strcmp(json_string(preview, "mode", ""), mode != NULL ? mode : "") == 0)
            return preview;
    }
    return NULL;
}

static yyjson_val *editor_drag_create_json(yyjson_val *placement)
{
    yyjson_val *drag = obj_get(placement, "drag_create");
    return yyjson_is_obj(drag) ? drag : NULL;
}

static void publish_editor_placement_preview(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, bool valid,
                                             yyjson_val *preview_json, const editor_placement_preview_state *preview,
                                             const char *message)
{
    if (runtime == NULL || outputs == NULL)
        return;
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "active_key", valid);
    editor_set_bool_output(scene_state, outputs, "valid_key", valid);
    editor_set_string_output(scene_state, outputs, "mode_key", valid && preview != NULL ? preview->mode : "");
    editor_set_string_output(scene_state, outputs, "kind_key", valid && preview != NULL ? preview->kind : "");
    editor_set_string_output(scene_state, outputs, "axis_key", valid && preview != NULL ? preview->axis : "");
    editor_set_string_output(scene_state, outputs, "world_key", valid && preview != NULL ? preview->world_name : "");
    editor_set_string_output(scene_state, outputs, "material_key",
                             valid && preview != NULL ? preview->material_name : "");
    editor_set_string_output(scene_state, outputs, "message_key", message != NULL ? message : "");
    editor_set_vec3_output(scene_state, outputs, "anchor_key",
                           valid && preview != NULL ? preview->anchor : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_float_output(scene_state, outputs, "snap_key", valid && preview != NULL ? preview->snap : 0.0f);
    editor_set_vec3_output(scene_state, outputs, "bounds_min_key",
                           valid && preview != NULL && preview->has_bounds ? preview->bounds.min
                                                                           : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_vec3_output(scene_state, outputs, "bounds_max_key",
                           valid && preview != NULL && preview->has_bounds ? preview->bounds.max
                                                                           : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_int_output(scene_state, outputs, "overlap_count_key",
                          valid && preview != NULL ? preview->source_positive_overlap_count : 0);
    const char *last_action_key = json_string(preview_json, "last_action_key", NULL);
    if (last_action_key != NULL && last_action_key[0] != '\0')
        slayer3d_properties_set_string(scene_state, last_action_key, message != NULL ? message : "");
}

static float editor_placement_snap(slayer3d_game_data_runtime *runtime, yyjson_val *placement, yyjson_val *preview)
{
    const float authored_snap = json_float(preview, "snap", json_float(placement, "default_snap", 0.0f));
    const char *snap_key = json_string(preview, "snap_key", NULL);
    if (snap_key == NULL && obj_get(preview, "snap") == NULL)
        snap_key = json_string(placement, "snap_key", NULL);
    if (runtime != NULL && snap_key != NULL && snap_key[0] != '\0')
        return slayer3d_properties_get_float(slayer3d_game_data_scene_state(runtime), snap_key, authored_snap);
    return authored_snap;
}

static float editor_placement_grid_size(slayer3d_game_data_runtime *runtime, yyjson_val *placement, yyjson_val *preview,
                                        float fallback)
{
    const float authored_grid_size =
        json_float(preview, "grid_size", json_float(placement, "default_grid_size", fallback));
    const char *grid_size_key = json_string(preview, "grid_size_key", json_string(placement, "grid_size_key", NULL));
    if (runtime != NULL && grid_size_key != NULL && grid_size_key[0] != '\0')
    {
        return slayer3d_properties_get_float(slayer3d_game_data_scene_state(runtime), grid_size_key,
                                             authored_grid_size);
    }
    return authored_grid_size;
}

static int editor_source_units_from_meters_public(const brush_world_runtime *world_runtime, float value)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return (int)SDL_lroundf(value / meters_per_unit);
}

static int editor_drag_cell(float value, float grid_size)
{
    return (int)SDL_floorf(value / SDL_max(grid_size, 0.001f));
}

static void editor_drag_source_bounds(const brush_world_runtime *world_runtime, const editor_drag_create_state *drag,
                                      int out_min[3], int out_max[3])
{
    const int min_cell[3] = {SDL_min(drag->start_cell[0], drag->current_cell[0]),
                             SDL_min(drag->start_cell[1], drag->current_cell[1]),
                             SDL_min(drag->start_cell[2], drag->current_cell[2])};
    const int max_cell[3] = {SDL_max(drag->start_cell[0], drag->current_cell[0]) + 1,
                             SDL_max(drag->start_cell[1], drag->current_cell[1]) + 1,
                             SDL_max(drag->start_cell[2], drag->current_cell[2]) + 1};
    for (int axis = 0; axis < 3; ++axis)
    {
        out_min[axis] = editor_source_units_from_meters_public(world_runtime, (float)min_cell[axis] * drag->grid_size);
        out_max[axis] = editor_source_units_from_meters_public(world_runtime, (float)max_cell[axis] * drag->grid_size);
    }
}

static void editor_drag_update_current_cell(editor_drag_create_state *drag,
                                            const slayer3d_game_data_editor_selection *hover_selection)
{
    if (drag == NULL || hover_selection == NULL || !hover_selection->hit)
        return;
    const int x = editor_drag_cell(hover_selection->point.x, drag->grid_size);
    const int y = drag->start_cell[1];
    const int z = editor_drag_cell(hover_selection->point.z, drag->grid_size);
    if (drag->current_cell[0] != x || drag->current_cell[1] != y || drag->current_cell[2] != z)
        drag->moved = true;
    drag->current_cell[0] = x;
    drag->current_cell[1] = y;
    drag->current_cell[2] = z;
}

static bool editor_drag_preview_source_box(slayer3d_game_data_runtime *runtime, yyjson_val *drag_json,
                                           editor_drag_create_state *drag,
                                           editor_brush_source_prefab_result *out_result)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, drag->world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;

    editor_drag_source_bounds(world_runtime, drag, drag->source_min, drag->source_max);

    editor_brush_source_prefab_desc desc;
    SDL_zero(desc);
    desc.prefab = json_string(drag_json, "prefab", "editor.box");
    desc.material = drag->material_name;
    desc.contents = drag->contents;

    char error_buffer[256] = {0};
    const bool ok =
        editor_brush_world_run_source_prefab_command(world_runtime, &desc, NULL, drag->source_min, drag->source_max,
                                                     false, out_result, error_buffer, sizeof(error_buffer));
    if (!ok && out_result != NULL && error_buffer[0] != '\0')
        SDL_strlcpy(out_result->warning, error_buffer, sizeof(out_result->warning));
    return ok && (out_result == NULL || out_result->valid);
}

static void editor_drag_publish_preview(slayer3d_game_data_runtime *runtime, yyjson_val *placement,
                                        yyjson_val *drag_json, editor_drag_create_state *drag,
                                        const editor_brush_source_prefab_result *result, const char *message,
                                        bool valid)
{
    yyjson_val *outputs = obj_get(placement, "outputs");
    editor_placement_preview_state *preview = &runtime->editor_placement_preview;
    SDL_zero(*preview);
    if (valid && drag != NULL && result != NULL)
    {
        preview->active = true;
        preview->scene = slayer3d_game_data_active_scene(runtime);
        preview->mode = json_string(drag_json, "mode", "drag_box");
        preview->kind = json_string(drag_json, "kind", "box");
        preview->axis = "xyz";
        preview->world_name = drag->world_name;
        preview->material_name = drag->material_name;
        preview->contents = drag->contents;
        preview->snap = drag->grid_size;
        preview->has_bounds = true;
        preview->bounds = result->bounds;
        preview->has_source_candidate = true;
        for (int axis = 0; axis < 3; ++axis)
        {
            preview->source_min[axis] = drag->source_min[axis];
            preview->source_max[axis] = drag->source_max[axis];
        }
        preview->source_positive_overlap_count = result->positive_overlap_count;
        SDL_strlcpy(preview->source_warning, result->warning, sizeof(preview->source_warning));
    }
    publish_editor_placement_preview(runtime, outputs, valid, drag_json, valid ? preview : NULL, message);
}

static float editor_placement_elevation(slayer3d_game_data_runtime *runtime, yyjson_val *placement)
{
    const float authored_elevation = json_float(placement, "default_elevation", 0.0f);
    const char *elevation_key = json_string(placement, "elevation_key", NULL);
    if (runtime != NULL && elevation_key != NULL && elevation_key[0] != '\0')
        return slayer3d_properties_get_float(slayer3d_game_data_scene_state(runtime), elevation_key,
                                             authored_elevation);
    return authored_elevation;
}

static bool editor_placement_auto_axis_enabled(slayer3d_game_data_runtime *runtime, yyjson_val *preview)
{
    if (runtime == NULL || SDL_strcmp(json_string(preview, "auto_axis", ""), "camera_cardinal") != 0)
        return false;

    const char *view_key = json_string(preview, "auto_axis_view_key", NULL);
    const char *required_view = json_string(preview, "auto_axis_view", NULL);
    if (view_key == NULL || view_key[0] == '\0' || required_view == NULL || required_view[0] == '\0')
        return true;

    const char *view = slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), view_key, "");
    return SDL_strcmp(view, required_view) == 0;
}

static const char *editor_placement_camera_cardinal_axis(slayer3d_game_data_runtime *runtime, yyjson_val *preview,
                                                         const char *fallback)
{
    const char *camera_name = json_string(preview, "auto_axis_camera", slayer3d_game_data_active_camera(runtime));
    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, camera_name, &camera))
        return fallback;

    slayer3d_vec3 forward = slayer3d_vec3_sub(camera.target, camera.position);
    forward.y = 0.0f;
    if (slayer3d_vec3_length_squared(forward) <= 0.000001f)
        return fallback;
    return SDL_fabsf(forward.x) >= SDL_fabsf(forward.z) ? "x" : "z";
}

static const char *editor_placement_axis(slayer3d_game_data_runtime *runtime, yyjson_val *preview)
{
    const char *authored_axis = json_string(preview, "axis", "z");
    const char *axis_key = json_string(preview, "axis_key", NULL);
    if (editor_placement_auto_axis_enabled(runtime, preview))
    {
        const char *axis = editor_placement_camera_cardinal_axis(runtime, preview, authored_axis);
        if (axis_key != NULL && axis_key[0] != '\0')
            slayer3d_properties_set_string(slayer3d_game_data_mutable_scene_state(runtime), axis_key, axis);
        return axis;
    }
    if (runtime != NULL && axis_key != NULL && axis_key[0] != '\0')
        return slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), axis_key, authored_axis);
    return authored_axis;
}

static float editor_placement_snap_floor(float value, float snap)
{
    return SDL_floorf(value / snap) * snap;
}

static float editor_placement_snap_value(float value, float snap, const char *mode)
{
    if (mode != NULL && SDL_strcmp(mode, "nearest") == 0)
        return SDL_roundf(value / snap) * snap;
    return editor_placement_snap_floor(value, snap);
}

static bool editor_placement_uses_connected_grid(yyjson_val *preview)
{
    return SDL_strcmp(json_string(preview, "elevation_mode", ""), "connected_grid") == 0;
}

static float editor_placement_connected_grid_elevation(slayer3d_game_data_runtime *runtime, yyjson_val *placement,
                                                       const slayer3d_game_data_editor_selection *selection,
                                                       float grid_size)
{
    float elevation = editor_placement_elevation(runtime, placement);
    if (selection == NULL || !selection->hit || !selection->has_bounds)
    {
        if (selection != NULL && selection->hit && !selection->has_bounds &&
            selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID && selection->normal.y > 0.5f)
        {
            elevation = selection->point.y;
        }
        return elevation;
    }

    if (selection->type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
        return elevation;

    const char *material = selection->material_name != NULL ? selection->material_name : "";
    const char *floor_material = json_string(placement, "floor_material", "mat.editor.floor");
    const char *ceiling_material = json_string(placement, "ceiling_material", "mat.editor.ceiling");
    if (floor_material != NULL && floor_material[0] != '\0' && SDL_strcmp(material, floor_material) == 0)
        elevation = selection->bounds.max.y;
    else if (ceiling_material != NULL && ceiling_material[0] != '\0' && SDL_strcmp(material, ceiling_material) == 0)
        elevation = selection->bounds.min.y - grid_size;
    else if (selection->normal.y > 0.5f)
        elevation = selection->bounds.max.y;
    else if (selection->normal.y < -0.5f)
        elevation = selection->bounds.min.y - grid_size;
    else
        elevation = selection->bounds.min.y;
    return elevation;
}

static void publish_editor_placement_elevation(slayer3d_game_data_runtime *runtime, yyjson_val *placement,
                                               float elevation)
{
    if (runtime == NULL || placement == NULL)
        return;

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    const char *elevation_key = json_string(placement, "elevation_key", NULL);
    if (elevation_key != NULL && elevation_key[0] != '\0')
        slayer3d_properties_set_float(scene_state, elevation_key, elevation);
    const char *work_plane_distance_key = json_string(placement, "work_plane_distance_key", NULL);
    if (work_plane_distance_key != NULL && work_plane_distance_key[0] != '\0')
        slayer3d_properties_set_float(scene_state, work_plane_distance_key, elevation);
}

static editor_brush_source_prefab_desc editor_placement_source_prefab_desc(
    slayer3d_game_data_runtime *runtime, yyjson_val *placement, yyjson_val *preview_json,
    const editor_placement_preview_state *preview)
{
    editor_brush_source_prefab_desc desc;
    SDL_zero(desc);
    desc.prefab = preview != NULL ? preview->mode : json_string(preview_json, "mode", NULL);
    desc.material = preview != NULL ? preview->material_name : json_string(preview_json, "material", NULL);
    desc.axis = preview != NULL ? preview->axis : json_string(preview_json, "axis", "z");
    desc.contents = preview != NULL ? preview->contents : 0u;
    desc.anchor = preview != NULL ? preview->anchor : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    desc.grid_size = editor_placement_grid_size(runtime, placement, preview_json, desc.grid_size);
    desc.use_grid_bounds = obj_get(preview_json, "grid_min") != NULL || obj_get(preview_json, "grid_max") != NULL;
    desc.grid_min = json_vec3(preview_json, "grid_min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    desc.grid_max = json_vec3(preview_json, "grid_max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    desc.min = json_vec3(preview_json, "min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    desc.max = json_vec3(preview_json, "max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return desc;
}

static slayer3d_vec3 editor_placement_anchor(slayer3d_game_data_runtime *runtime, yyjson_val *placement,
                                             yyjson_val *preview, const slayer3d_game_data_editor_selection *selection,
                                             float snap)
{
    slayer3d_vec3 anchor = selection != NULL ? selection->point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    anchor = slayer3d_vec3_add(anchor, json_vec3(preview, "position_offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
    if (snap > 0.0f)
    {
        const char *snap_mode = json_string(preview, "snap_mode", json_string(placement, "snap_mode", "floor"));
        anchor.x = editor_placement_snap_value(anchor.x, snap, snap_mode);
        anchor.y = editor_placement_snap_value(anchor.y, snap, snap_mode);
        anchor.z = editor_placement_snap_value(anchor.z, snap, snap_mode);
    }
    if (editor_placement_uses_connected_grid(preview))
    {
        const float grid_size = editor_placement_grid_size(runtime, placement, preview, snap);
        anchor.y = editor_placement_connected_grid_elevation(runtime, placement, selection, grid_size);
        publish_editor_placement_elevation(runtime, placement, anchor.y);
    }
    return anchor;
}

static bool editor_placement_preview_validate_source_box(slayer3d_game_data_runtime *runtime, yyjson_val *placement,
                                                         yyjson_val *preview_json,
                                                         editor_placement_preview_state *preview, char *error_buffer,
                                                         int error_buffer_size)
{
    if (runtime == NULL || preview == NULL || SDL_strcmp(preview->kind, "box") != 0 || !preview->has_bounds ||
        preview->world_name == NULL || preview->world_name[0] == '\0')
    {
        return true;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, preview->world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "brush prefab placement requires a source-backed brush world");
        return false;
    }

    const editor_brush_source_prefab_desc desc =
        editor_placement_source_prefab_desc(runtime, placement, preview_json, preview);
    editor_brush_source_prefab_result result;
    const bool ok = editor_brush_world_run_source_prefab_command(world_runtime, &desc, NULL, NULL, NULL, false, &result,
                                                                 error_buffer, error_buffer_size);
    if (ok)
    {
        preview->bounds = result.bounds;
        preview->has_source_candidate = true;
        preview->source_positive_overlap_count = result.positive_overlap_count;
        SDL_strlcpy(preview->source_warning, result.warning, sizeof(preview->source_warning));
        for (int axis = 0; axis < 3; ++axis)
        {
            preview->source_min[axis] = result.source_min[axis];
            preview->source_max[axis] = result.source_max[axis];
        }
    }
    return ok;
}

bool update_editor_drag_create(slayer3d_game_data_runtime *runtime, yyjson_val *editor,
                               const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || editor == NULL)
        return false;

    const char *mode = scene_state_string(runtime, "editor.mode", "select");
    if (SDL_strcmp(mode, "select") != 0)
    {
        clear_editor_drag_create(runtime);
        return true;
    }

    yyjson_val *placement = obj_get(editor, "placement");
    yyjson_val *drag_json = editor_drag_create_json(placement);
    if (drag_json == NULL)
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    editor_drag_create_state *drag = &runtime->editor_drag_create;

    if (!drag->active && left_pressed && hover_selection != NULL && hover_selection->hit &&
        hover_selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID)
    {
        const float grid_size = editor_placement_grid_size(runtime, placement, drag_json,
                                                           json_float(placement, "default_grid_size", 16.0f));
        if (grid_size <= 0.0f)
            return true;
        (void)slayer3d_game_data_clear_active_editor_selection(runtime);
        SDL_zero(*drag);
        drag->active = true;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        drag->world_name = json_string(drag_json, "world", "brush.editor_shell.target");
        drag->material_name =
            scene_state_string(runtime, json_string(drag_json, "material_key", "editor.brush.material"),
                               json_string(drag_json, "material", "mat.editor.floor"));
        drag->contents = brush_flags_from_json(obj_get(drag_json, "contents"), brush_content_flag_from_string,
                                               SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID);
        drag->grid_size = grid_size;
        drag->start_cell[0] = editor_drag_cell(hover_selection->point.x, grid_size);
        drag->start_cell[1] = editor_drag_cell(hover_selection->point.y, grid_size);
        drag->start_cell[2] = editor_drag_cell(hover_selection->point.z, grid_size);
        drag->current_cell[0] = drag->start_cell[0];
        drag->current_cell[1] = drag->start_cell[1];
        drag->current_cell[2] = drag->start_cell[2];
        if (out_consumed != NULL)
            *out_consumed = true;
    }

    if (!drag->active)
        return true;
    if (out_consumed != NULL)
        *out_consumed = true;

    editor_drag_update_current_cell(drag, hover_selection);
    if (SDL_fabsf(slayer3d_input_get_mouse_dx(input)) > 0.0f || SDL_fabsf(slayer3d_input_get_mouse_dy(input)) > 0.0f)
        drag->moved = true;

    editor_brush_source_prefab_result result;
    const bool valid = editor_drag_preview_source_box(runtime, drag_json, drag, &result);
    const char *base_message = json_string(drag_json, "message", "drag brush preview");
    char message[256];
    if (valid && result.warning[0] != '\0')
        SDL_snprintf(message, sizeof(message), "%s; %s", base_message, result.warning);
    else if (!valid && result.warning[0] != '\0')
        SDL_strlcpy(message, result.warning, sizeof(message));
    else
        SDL_strlcpy(message, base_message, sizeof(message));
    editor_drag_publish_preview(runtime, placement, drag_json, drag, &result, message, valid);

    if (left_released || !left_down)
    {
        const bool should_commit = valid && drag->moved;
        if (should_commit)
        {
            editor_brush_source_prefab_result commit_result;
            if (slayer3d_game_data_create_editor_source_box_brush(runtime, drag->world_name, drag->material_name,
                                                                  drag->contents, drag->source_min, drag->source_max,
                                                                  &commit_result) &&
                runtime->scene_state != NULL)
            {
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "drag brush created");
            }
        }
        clear_editor_drag_create(runtime);
        clear_editor_placement_preview(runtime);
        publish_editor_placement_preview(runtime, obj_get(placement, "outputs"), false, drag_json, NULL,
                                         should_commit ? "drag brush created" : "drag cancelled");
    }
    return true;
}

void update_editor_placement_preview(slayer3d_game_data_runtime *runtime, yyjson_val *editor,
                                     const slayer3d_game_data_editor_selection *hover_selection)
{
    yyjson_val *placement = obj_get(editor, "placement");
    if (runtime == NULL || !yyjson_is_obj(placement))
    {
        clear_editor_placement_preview(runtime);
        return;
    }

    const char *tool_key = json_string(placement, "tool_key", "editor.tool.mode");
    const char *mode = slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), tool_key,
                                                      json_string(placement, "default_tool", "select"));
    yyjson_val *preview_json = find_editor_placement_preview_json(placement, mode);
    yyjson_val *outputs = obj_get(placement, "outputs");
    if (preview_json == NULL || hover_selection == NULL || !hover_selection->hit)
    {
        clear_editor_placement_preview(runtime);
        publish_editor_placement_preview(runtime, outputs, false, preview_json, NULL,
                                         preview_json == NULL ? "" : "placement requires a hovered point");
        return;
    }

    const float snap = editor_placement_snap(runtime, placement, preview_json);
    const slayer3d_vec3 anchor = editor_placement_anchor(runtime, placement, preview_json, hover_selection, snap);
    editor_placement_preview_state *preview = &runtime->editor_placement_preview;
    SDL_zero(*preview);
    preview->active = true;
    preview->scene = slayer3d_game_data_active_scene(runtime);
    preview->mode = json_string(preview_json, "mode", mode);
    preview->kind = json_string(preview_json, "kind", "box");
    preview->axis = editor_placement_axis(runtime, preview_json);
    preview->world_name = json_string(preview_json, "world", hover_selection->world_name);
    preview->material_name = json_string(preview_json, "material", hover_selection->material_name);
    preview->contents = brush_flags_from_json(obj_get(preview_json, "contents"), brush_content_flag_from_string,
                                              SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID);
    preview->anchor = anchor;
    preview->snap = snap;
    preview->has_bounds = true;
    if (SDL_strcmp(preview->kind, "player_start") == 0)
    {
        const slayer3d_vec3 size = json_vec3(preview_json, "size", slayer3d_vec3_make(0.5f, 1.8f, 0.5f));
        preview->bounds.min = slayer3d_vec3_make(anchor.x - size.x * 0.5f, anchor.y, anchor.z - size.z * 0.5f);
        preview->bounds.max = slayer3d_vec3_make(anchor.x + size.x * 0.5f, anchor.y + size.y, anchor.z + size.z * 0.5f);
    }
    else
    {
        preview->bounds.min = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        preview->bounds.max = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    }
    char validation_error[256] = {0};
    if (!editor_placement_preview_validate_source_box(runtime, placement, preview_json, preview, validation_error,
                                                      sizeof(validation_error)))
    {
        clear_editor_placement_preview(runtime);
        publish_editor_placement_preview(runtime, outputs, false, preview_json, NULL,
                                         validation_error[0] != '\0' ? validation_error
                                                                     : "placement preview is invalid");
        return;
    }
    char message[256];
    const char *base_message = json_string(preview_json, "message", "placement preview");
    if (preview->source_warning[0] != '\0')
        SDL_snprintf(message, sizeof(message), "%s; %s", base_message, preview->source_warning);
    else
        SDL_strlcpy(message, base_message, sizeof(message));
    publish_editor_placement_preview(runtime, outputs, true, preview_json, preview, message);
}

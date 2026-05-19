#include "game_data_internal.h"

void clear_editor_placement_preview(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    SDL_zero(runtime->editor_placement_preview);
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

static float editor_placement_elevation(slayer3d_game_data_runtime *runtime, yyjson_val *placement)
{
    const float authored_elevation = json_float(placement, "default_elevation", 0.0f);
    const char *elevation_key = json_string(placement, "elevation_key", NULL);
    if (runtime != NULL && elevation_key != NULL && elevation_key[0] != '\0')
        return slayer3d_properties_get_float(slayer3d_game_data_scene_state(runtime), elevation_key,
                                             authored_elevation);
    return authored_elevation;
}

static const char *editor_placement_axis(slayer3d_game_data_runtime *runtime, yyjson_val *preview)
{
    const char *authored_axis = json_string(preview, "axis", "z");
    const char *axis_key = json_string(preview, "axis_key", NULL);
    if (runtime != NULL && axis_key != NULL && axis_key[0] != '\0')
        return slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), axis_key, authored_axis);
    return authored_axis;
}

static slayer3d_vec3 editor_grid_scaled_vec3(yyjson_val *obj, const char *key, float grid_size)
{
    return slayer3d_vec3_scale(json_vec3(obj, key, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)), grid_size);
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
        return elevation;

    const char *material = selection->material_name != NULL ? selection->material_name : "";
    const char *floor_material = json_string(placement, "floor_material", "mat.editor.floor");
    const char *ceiling_material = json_string(placement, "ceiling_material", "mat.editor.ceiling");
    if (floor_material != NULL && floor_material[0] != '\0' && SDL_strcmp(material, floor_material) == 0)
        elevation = selection->bounds.max.y;
    else if (ceiling_material != NULL && ceiling_material[0] != '\0' && SDL_strcmp(material, ceiling_material) == 0)
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

static slayer3d_bounding_box editor_placement_oriented_box_bounds(slayer3d_game_data_runtime *runtime,
                                                                  yyjson_val *placement, yyjson_val *preview_json,
                                                                  slayer3d_vec3 anchor, const char *axis, float snap)
{
    const bool uses_grid_bounds =
        obj_get(preview_json, "grid_min") != NULL || obj_get(preview_json, "grid_max") != NULL;
    const float grid_size = editor_placement_grid_size(runtime, placement, preview_json, snap);
    const slayer3d_vec3 source_min = uses_grid_bounds
                                         ? editor_grid_scaled_vec3(preview_json, "grid_min", grid_size)
                                         : json_vec3(preview_json, "min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    const slayer3d_vec3 source_max = uses_grid_bounds
                                         ? editor_grid_scaled_vec3(preview_json, "grid_max", grid_size)
                                         : json_vec3(preview_json, "max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_bounding_box bounds;
    if (axis != NULL && SDL_strcmp(axis, "x") == 0)
    {
        bounds.min = slayer3d_vec3_make(anchor.x + source_min.z, anchor.y + source_min.y, anchor.z + source_min.x);
        bounds.max = slayer3d_vec3_make(anchor.x + source_max.z, anchor.y + source_max.y, anchor.z + source_max.x);
    }
    else
    {
        bounds.min = slayer3d_vec3_add(anchor, source_min);
        bounds.max = slayer3d_vec3_add(anchor, source_max);
    }
    return bounds;
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
        preview->bounds =
            editor_placement_oriented_box_bounds(runtime, placement, preview_json, anchor, preview->axis, snap);
    }
    publish_editor_placement_preview(runtime, outputs, true, preview_json, preview,
                                     json_string(preview_json, "message", "placement preview"));
}

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

static float *editor_bounds_min_axis(slayer3d_bounding_box *bounds, int axis)
{
    if (axis == 0)
        return &bounds->min.x;
    if (axis == 1)
        return &bounds->min.y;
    return &bounds->min.z;
}

static float *editor_bounds_max_axis(slayer3d_bounding_box *bounds, int axis)
{
    if (axis == 0)
        return &bounds->max.x;
    if (axis == 1)
        return &bounds->max.y;
    return &bounds->max.z;
}

static float editor_bounds_axis_min(slayer3d_bounding_box bounds, int axis)
{
    if (axis == 0)
        return bounds.min.x;
    if (axis == 1)
        return bounds.min.y;
    return bounds.min.z;
}

static float editor_bounds_axis_max(slayer3d_bounding_box bounds, int axis)
{
    if (axis == 0)
        return bounds.max.x;
    if (axis == 1)
        return bounds.max.y;
    return bounds.max.z;
}

static bool editor_bounds_overlap_positive_volume(slayer3d_bounding_box a, slayer3d_bounding_box b)
{
    const float epsilon = 0.00001f;
    return a.min.x < b.max.x - epsilon && b.min.x < a.max.x - epsilon && a.min.y < b.max.y - epsilon &&
           b.min.y < a.max.y - epsilon && a.min.z < b.max.z - epsilon && b.min.z < a.max.z - epsilon;
}

static bool editor_preview_brush_contents_are_structural(unsigned int contents)
{
    if (contents == 0u)
        contents = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    return (contents & (SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP |
                        SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP | SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY)) != 0u;
}

static bool editor_bounds_overlap_on_axis(slayer3d_bounding_box a, slayer3d_bounding_box b, int axis)
{
    const float epsilon = 0.00001f;
    return editor_bounds_axis_min(a, axis) < editor_bounds_axis_max(b, axis) - epsilon &&
           editor_bounds_axis_min(b, axis) < editor_bounds_axis_max(a, axis) - epsilon;
}

static bool editor_placement_trim_wall_overlap(slayer3d_bounding_box *bounds, slayer3d_bounding_box existing,
                                               int run_axis, int thin_axis, float max_trim)
{
    if (bounds == NULL || !editor_bounds_overlap_positive_volume(*bounds, existing))
        return true;

    if (!editor_bounds_overlap_on_axis(*bounds, existing, 1) ||
        !editor_bounds_overlap_on_axis(*bounds, existing, thin_axis))
        return false;

    const float candidate_min = editor_bounds_axis_min(*bounds, run_axis);
    const float candidate_max = editor_bounds_axis_max(*bounds, run_axis);
    const float existing_min = editor_bounds_axis_min(existing, run_axis);
    const float existing_max = editor_bounds_axis_max(existing, run_axis);
    const float overlap_min = SDL_max(candidate_min, existing_min);
    const float overlap_max = SDL_min(candidate_max, existing_max);
    const float overlap = overlap_max - overlap_min;
    const float epsilon = 0.0001f;
    if (overlap <= epsilon || overlap > max_trim + epsilon)
        return false;

    if (existing_min <= candidate_min + epsilon && existing_max > candidate_min + epsilon &&
        existing_max < candidate_max - epsilon)
    {
        *editor_bounds_min_axis(bounds, run_axis) = SDL_max(*editor_bounds_min_axis(bounds, run_axis), existing_max);
        return *editor_bounds_min_axis(bounds, run_axis) < *editor_bounds_max_axis(bounds, run_axis) - epsilon;
    }

    if (existing_max >= candidate_max - epsilon && existing_min < candidate_max - epsilon &&
        existing_min > candidate_min + epsilon)
    {
        *editor_bounds_max_axis(bounds, run_axis) = SDL_min(*editor_bounds_max_axis(bounds, run_axis), existing_min);
        return *editor_bounds_min_axis(bounds, run_axis) < *editor_bounds_max_axis(bounds, run_axis) - epsilon;
    }

    return false;
}

static slayer3d_bounding_box editor_placement_trim_wall_endpoint_overlaps(slayer3d_game_data_runtime *runtime,
                                                                          yyjson_val *preview_json,
                                                                          const editor_placement_preview_state *preview)
{
    slayer3d_bounding_box bounds = preview != NULL ? preview->bounds
                                                   : (slayer3d_bounding_box){slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                                             slayer3d_vec3_make(0.0f, 0.0f, 0.0f)};
    if (runtime == NULL || preview_json == NULL || preview == NULL || SDL_strcmp(preview->kind, "box") != 0 ||
        SDL_strcmp(preview->mode, "wall") != 0 || preview->world_name == NULL)
    {
        return bounds;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, preview->world_name);
    if (world_runtime == NULL)
        return bounds;

    const int thin_axis = preview->axis != NULL && SDL_strcmp(preview->axis, "x") == 0 ? 0 : 2;
    const int run_axis = thin_axis == 0 ? 2 : 0;
    const float wall_thickness = editor_bounds_axis_max(bounds, thin_axis) - editor_bounds_axis_min(bounds, thin_axis);
    const float max_trim = SDL_max(wall_thickness * 1.25f, 0.001f);
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->brush_count; ++i)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[i];
        if (brush == NULL || !brush->has_bounds || !editor_preview_brush_contents_are_structural(brush->contents))
            continue;
        if (!editor_bounds_overlap_positive_volume(bounds, brush->bounds))
            continue;
        if (!editor_placement_trim_wall_overlap(&bounds, brush->bounds, run_axis, thin_axis, max_trim))
            return preview->bounds;
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
        preview->bounds =
            editor_placement_oriented_box_bounds(runtime, placement, preview_json, anchor, preview->axis, snap);
        preview->bounds = editor_placement_trim_wall_endpoint_overlaps(runtime, preview_json, preview);
    }
    publish_editor_placement_preview(runtime, outputs, true, preview_json, preview,
                                     json_string(preview_json, "message", "placement preview"));
}

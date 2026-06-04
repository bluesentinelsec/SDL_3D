/**
 * @file game_data_validation_scene_editor.c
 * @brief Validation for scene editor tooling configuration.
 */

#include "game_data_validation_internal.h"

#include <float.h>

#include <SDL3/SDL_stdinc.h>

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static const char *json_string_default(yyjson_val *object, const char *key, const char *default_value)
{
    const char *value = validation_json_string(object, key);
    return value != NULL ? value : default_value;
}

static bool is_non_empty_string(yyjson_val *object, const char *key)
{
    const char *value = json_string(object, key);
    return value != NULL && value[0] != '\0';
}

static bool editor_model_filter_name_valid(const char *value)
{
    return value != NULL && (SDL_strcmp(value, "all") == 0 || SDL_strcmp(value, "sector_levels") == 0 ||
                             SDL_strcmp(value, "sector") == 0 || SDL_strcmp(value, "brush_worlds") == 0 ||
                             SDL_strcmp(value, "brush") == 0);
}

static bool editor_debug_flag_name_valid(const char *value)
{
    return value != NULL && (SDL_strcmp(value, "all") == 0 || SDL_strcmp(value, "world_bounds") == 0 ||
                             SDL_strcmp(value, "selection_bounds") == 0 || SDL_strcmp(value, "trace_ray") == 0 ||
                             SDL_strcmp(value, "face_normal") == 0 || SDL_strcmp(value, "hit_marker") == 0 ||
                             SDL_strcmp(value, "command_preview") == 0 || SDL_strcmp(value, "work_plane_grid") == 0 ||
                             SDL_strcmp(value, "grid") == 0 || SDL_strcmp(value, "player_starts") == 0 ||
                             SDL_strcmp(value, "game_objects") == 0 || SDL_strcmp(value, "markers") == 0 ||
                             SDL_strcmp(value, "diagnostic_markers") == 0 || SDL_strcmp(value, "selection_face") == 0 ||
                             SDL_strcmp(value, "vertex_handles") == 0 || SDL_strcmp(value, "edge_handles") == 0 ||
                             SDL_strcmp(value, "rotate_handles") == 0 || SDL_strcmp(value, "scale_handles") == 0 ||
                             SDL_strcmp(value, "clip_preview") == 0);
}

static bool validate_string_or_string_array_names(validation_context *ctx, yyjson_val *value, const char *path,
                                                  const char *label, bool (*name_valid)(const char *name))
{
    if (value == NULL)
        return true;
    if (yyjson_is_str(value))
        return name_valid(yyjson_get_str(value))
                   ? true
                   : validation_error(ctx, path, "unsupported %s '%s'", label, yyjson_get_str(value));
    if (!yyjson_is_arr(value))
        return validation_error(ctx, path, "%s must be a string or string array", label);
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        char entry_path[PATH_BUFFER_SIZE];
        format_path(entry_path, sizeof(entry_path), "%s[%zu]", path, i);
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (!yyjson_is_str(entry) || !name_valid(yyjson_get_str(entry)))
            return validation_error(ctx, entry_path, "unsupported %s '%s'", label,
                                    yyjson_is_str(entry) ? yyjson_get_str(entry) : "<non-string>");
    }
    return true;
}

static bool validate_scene_editor_outputs(validation_context *ctx, yyjson_val *outputs, const char *json_path)
{
    if (outputs == NULL)
        return true;
    if (!yyjson_is_obj(outputs))
        return validation_error(ctx, json_path, "scene editor selection outputs must be an object");

    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(outputs, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        if (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0')
            return validation_error(ctx, json_path, "scene editor selection output values must be non-empty strings");
    }
    return true;
}

static bool validate_scene_editor_work_plane(validation_context *ctx, yyjson_val *trace, const char *trace_path);

static bool validate_scene_editor_trace_screen(validation_context *ctx, yyjson_val *screen, const char *json_path)
{
    if (screen == NULL)
        return true;
    if (yyjson_is_arr(screen) && is_exact_vec_array(screen, 2))
        return true;
    if (yyjson_is_str(screen) && SDL_strcmp(yyjson_get_str(screen), "center") == 0)
        return true;
    return validation_error(ctx, json_path, "scene editor camera_screen trace screen must be a vec2 or 'center'");
}

static bool validate_scene_editor_camera_screen_trace(validation_context *ctx, yyjson_val *trace,
                                                      const char *trace_path, validation_names *names)
{
    yyjson_val *camera_value = obj_get(trace, "camera");
    if (!validate_optional_non_empty_string_value(ctx, camera_value, trace_path,
                                                  "scene editor camera_screen trace camera"))
        return false;
    const char *camera = json_string(trace, "camera");
    if (camera != NULL && !require_ref(ctx, &names->cameras, "camera", camera, trace_path))
        return false;

    char field_path[PATH_BUFFER_SIZE];
    yyjson_val *screen = obj_get(trace, "screen");
    format_path(field_path, sizeof(field_path), "%s.screen", trace_path);
    if (!validate_scene_editor_trace_screen(ctx, screen, field_path))
        return false;
    yyjson_val *viewport = obj_get(trace, "viewport");
    if (viewport != NULL &&
        (!is_exact_vec_array(viewport, 2) || !numeric_array_values_in_range(viewport, 0.000001, DBL_MAX)))
        return validation_error(ctx, trace_path, "scene editor camera_screen trace viewport must be a positive vec2");

    static const char *const numeric_keys[] = {"screen_x", "screen_y"};
    for (size_t i = 0; i < SDL_arraysize(numeric_keys); ++i)
    {
        format_path(field_path, sizeof(field_path), "%s.%s", trace_path, numeric_keys[i]);
        if (!validate_optional_number_value(ctx, obj_get(trace, numeric_keys[i]), field_path,
                                            "scene editor camera_screen trace coordinate"))
            return false;
    }

    static const char *const positive_keys[] = {"viewport_width", "viewport_height", "far"};
    for (size_t i = 0; i < SDL_arraysize(positive_keys); ++i)
    {
        format_path(field_path, sizeof(field_path), "%s.%s", trace_path, positive_keys[i]);
        if (!validate_optional_positive_number_value(ctx, obj_get(trace, positive_keys[i]), field_path,
                                                     "scene editor camera_screen trace value"))
            return false;
    }
    format_path(field_path, sizeof(field_path), "%s.near", trace_path);
    if (!validate_optional_non_negative_number_value(ctx, obj_get(trace, "near"), field_path,
                                                     "scene editor camera_screen trace near"))
        return false;

    static const char *const string_keys[] = {"screen_x_key", "screen_y_key", "viewport_width_key",
                                              "viewport_height_key"};
    for (size_t i = 0; i < SDL_arraysize(string_keys); ++i)
    {
        format_path(field_path, sizeof(field_path), "%s.%s", trace_path, string_keys[i]);
        if (!validate_optional_non_empty_string_value(ctx, obj_get(trace, string_keys[i]), field_path,
                                                      "scene editor camera_screen trace key"))
            return false;
    }

    yyjson_val *near_value = obj_get(trace, "near");
    yyjson_val *far_value = obj_get(trace, "far");
    if (near_value != NULL && far_value != NULL && yyjson_get_num(far_value) <= yyjson_get_num(near_value))
        return validation_error(ctx, trace_path, "scene editor camera_screen trace far must be greater than near");

    yyjson_val *viewports = obj_get(trace, "viewports");
    if (viewports != NULL)
    {
        if (!yyjson_is_arr(viewports))
            return validation_error(ctx, trace_path, "scene editor camera_screen trace viewports must be an array");
        for (size_t i = 0; i < yyjson_arr_size(viewports); ++i)
        {
            char viewport_path[PATH_BUFFER_SIZE];
            format_path(viewport_path, sizeof(viewport_path), "%s.viewports[%zu]", trace_path, i);
            yyjson_val *viewport_entry = yyjson_arr_get(viewports, i);
            if (!yyjson_is_obj(viewport_entry))
                return validation_error(ctx, viewport_path,
                                        "scene editor camera_screen trace viewport entries must be objects");
            if (!require_ref(ctx, &names->cameras, "camera", json_string(viewport_entry, "camera"), viewport_path))
                return false;
            yyjson_val *rect = obj_get(viewport_entry, "rect");
            if (!is_exact_vec_array(rect, 4) || !numeric_array_values_in_range(rect, 0.0, DBL_MAX) ||
                yyjson_get_num(yyjson_arr_get(rect, 2)) <= 0.0 || yyjson_get_num(yyjson_arr_get(rect, 3)) <= 0.0)
            {
                return validation_error(ctx, viewport_path,
                                        "scene editor camera_screen trace viewport rect must be [x, y, width, height] "
                                        "with positive dimensions");
            }
            if (!validate_scene_editor_work_plane(ctx, viewport_entry, viewport_path))
                return false;
            format_path(field_path, sizeof(field_path), "%s.screen", viewport_path);
            if (!validate_scene_editor_trace_screen(ctx, obj_get(viewport_entry, "screen"), field_path))
                return false;
        }
    }
    return true;
}

static bool validate_scene_editor_work_plane(validation_context *ctx, yyjson_val *trace, const char *trace_path)
{
    yyjson_val *work_plane = obj_get(trace, "work_plane");
    if (work_plane == NULL)
        return true;
    char work_plane_path[PATH_BUFFER_SIZE];
    format_path(work_plane_path, sizeof(work_plane_path), "%s.work_plane", trace_path);
    if (!yyjson_is_obj(work_plane))
        return validation_error(ctx, work_plane_path, "scene editor selection work_plane must be an object");

    yyjson_val *enabled = obj_get(work_plane, "enabled");
    if (enabled != NULL && !yyjson_is_bool(enabled))
        return validation_error(ctx, work_plane_path, "scene editor selection work_plane enabled must be a boolean");

    yyjson_val *normal = obj_get(work_plane, "normal");
    if (normal != NULL)
    {
        if (!is_exact_vec_array(normal, 3))
            return validation_error(ctx, work_plane_path, "scene editor selection work_plane normal must be a vec3");
        const double x = yyjson_get_num(yyjson_arr_get(normal, 0));
        const double y = yyjson_get_num(yyjson_arr_get(normal, 1));
        const double z = yyjson_get_num(yyjson_arr_get(normal, 2));
        if ((x * x + y * y + z * z) <= 0.000001)
            return validation_error(ctx, work_plane_path, "scene editor selection work_plane normal must be non-zero");
    }

    char distance_path[PATH_BUFFER_SIZE];
    format_path(distance_path, sizeof(distance_path), "%s.distance", work_plane_path);
    if (!validate_optional_number_value(ctx, obj_get(work_plane, "distance"), distance_path,
                                        "scene editor selection work_plane distance"))
    {
        return false;
    }

    char key_path[PATH_BUFFER_SIZE];
    static const char *const string_keys[] = {"normal_key", "distance_key"};
    for (size_t i = 0; i < SDL_arraysize(string_keys); ++i)
    {
        format_path(key_path, sizeof(key_path), "%s.%s", work_plane_path, string_keys[i]);
        if (!validate_optional_non_empty_string_value(ctx, obj_get(work_plane, string_keys[i]), key_path,
                                                      "scene editor selection work_plane key"))
            return false;
    }
    return true;
}

static bool validate_scene_editor_trace(validation_context *ctx, yyjson_val *trace, const char *trace_path,
                                        validation_names *names)
{
    yyjson_val *source_value = obj_get(trace, "source");
    if (!validate_optional_non_empty_string_value(ctx, source_value, trace_path, "scene editor selection trace source"))
        return false;
    const char *source = source_value != NULL ? yyjson_get_str(source_value) : "world";
    if (SDL_strcmp(source, "world") == 0)
    {
        if (!is_exact_vec_array(obj_get(trace, "start"), 3))
            return validation_error(ctx, trace_path, "scene editor selection trace requires a start vec3");
        if (!is_exact_vec_array(obj_get(trace, "end"), 3))
            return validation_error(ctx, trace_path, "scene editor selection trace requires an end vec3");
    }
    else if (SDL_strcmp(source, "camera_screen") == 0)
    {
        if (!validate_scene_editor_camera_screen_trace(ctx, trace, trace_path, names))
            return false;
    }
    else
    {
        return validation_error(ctx, trace_path,
                                "scene editor selection trace source must be 'world' or 'camera_screen'");
    }

    char contents_path[PATH_BUFFER_SIZE];
    format_path(contents_path, sizeof(contents_path), "%s.contents_mask", trace_path);
    if (!validate_brush_string_or_string_array(ctx, obj_get(trace, "contents_mask"), contents_path, "brush content",
                                               brush_content_name_valid, false))
        return false;
    char filter_path[PATH_BUFFER_SIZE];
    format_path(filter_path, sizeof(filter_path), "%s.model_filter", trace_path);
    if (!validate_string_or_string_array_names(ctx, obj_get(trace, "model_filter"), filter_path, "world model filter",
                                               editor_model_filter_name_valid))
        return false;
    if (!validate_scene_editor_work_plane(ctx, trace, trace_path))
        return false;
    return true;
}

static bool validate_scene_editor_selection_options(validation_context *ctx, yyjson_val *selection,
                                                    const char *selection_path)
{
    yyjson_val *mode = obj_get(selection, "mode");
    if (mode != NULL)
    {
        if (!yyjson_is_str(mode) || yyjson_get_str(mode)[0] == '\0')
            return validation_error(ctx, selection_path, "scene editor selection mode must be a non-empty string");
        const char *mode_name = yyjson_get_str(mode);
        if (SDL_strcmp(mode_name, "hover") != 0 && SDL_strcmp(mode_name, "click") != 0)
            return validation_error(ctx, selection_path, "scene editor selection mode must be 'hover' or 'click'");
    }

    static const char *const button_keys[] = {"select_button", "secondary_select_button"};
    for (size_t i = 0; i < SDL_arraysize(button_keys); ++i)
    {
        yyjson_val *button = obj_get(selection, button_keys[i]);
        if (button != NULL && (!yyjson_is_str(button) || !validation_mouse_button_name_valid(yyjson_get_str(button))))
            return validation_error(ctx, selection_path, "scene editor selection %s must be a mouse button",
                                    button_keys[i]);
    }

    yyjson_val *clear_on_miss = obj_get(selection, "clear_on_miss");
    if (clear_on_miss != NULL && !yyjson_is_bool(clear_on_miss))
        return validation_error(ctx, selection_path, "scene editor selection clear_on_miss must be a boolean");
    return true;
}

static bool validate_scene_editor_placement(validation_context *ctx, yyjson_val *placement, const char *placement_path,
                                            validation_names *names)
{
    if (placement == NULL)
        return true;
    if (!yyjson_is_obj(placement))
        return validation_error(ctx, placement_path, "scene editor placement must be an object");

    const char *string_fields[] = {"tool_key",       "snap_key",        "grid_size_key",
                                   "default_tool",   "elevation_key",   "work_plane_distance_key",
                                   "floor_material", "ceiling_material"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        char field_path[PATH_BUFFER_SIZE];
        format_path(field_path, sizeof(field_path), "%s.%s", placement_path, string_fields[i]);
        if (!validate_optional_non_empty_string_value(ctx, obj_get(placement, string_fields[i]), field_path,
                                                      "scene editor placement field"))
            return false;
    }
    yyjson_val *default_snap = obj_get(placement, "default_snap");
    if (default_snap != NULL && (!yyjson_is_num(default_snap) || yyjson_get_num(default_snap) <= 0.0))
        return validation_error(ctx, placement_path, "scene editor placement default_snap must be positive");
    yyjson_val *default_grid_size = obj_get(placement, "default_grid_size");
    if (default_grid_size != NULL && (!yyjson_is_num(default_grid_size) || yyjson_get_num(default_grid_size) <= 0.0))
        return validation_error(ctx, placement_path, "scene editor placement default_grid_size must be positive");
    char default_elevation_path[PATH_BUFFER_SIZE];
    format_path(default_elevation_path, sizeof(default_elevation_path), "%s.default_elevation", placement_path);
    if (!validate_optional_number_value(ctx, obj_get(placement, "default_elevation"), default_elevation_path,
                                        "scene editor placement default_elevation"))
        return false;

    static const char *const output_keys[] = {
        "active_key",  "valid_key",  "mode_key", "kind_key",       "axis_key",       "world_key", "material_key",
        "message_key", "anchor_key", "snap_key", "bounds_min_key", "bounds_max_key", "state_key"};
    if (!validate_optional_output_keys(ctx, placement, placement_path, "scene editor placement", output_keys,
                                       SDL_arraysize(output_keys)))
        return false;

    yyjson_val *previews = obj_get(placement, "previews");
    if (!yyjson_is_arr(previews) || yyjson_arr_size(previews) == 0)
        return validation_error(ctx, placement_path, "scene editor placement previews must be a non-empty array");
    for (size_t i = 0; i < yyjson_arr_size(previews); ++i)
    {
        char preview_path[PATH_BUFFER_SIZE];
        format_path(preview_path, sizeof(preview_path), "%s.previews[%zu]", placement_path, i);
        yyjson_val *preview = yyjson_arr_get(previews, i);
        if (!yyjson_is_obj(preview))
            return validation_error(ctx, preview_path, "scene editor placement preview must be an object");
        if (!is_non_empty_string(preview, "mode"))
            return validation_error(ctx, preview_path, "scene editor placement preview requires a non-empty mode");
        const char *kind = json_string(preview, "kind");
        if (kind == NULL)
            kind = "box";
        if (SDL_strcmp(kind, "box") != 0 && SDL_strcmp(kind, "player_start") != 0)
            return validation_error(ctx, preview_path,
                                    "scene editor placement preview kind must be box or player_start");
        static const char *const preview_string_fields[] = {"axis_key", "grid_size_key", "auto_axis_camera",
                                                            "auto_axis_view_key", "auto_axis_view"};
        for (size_t field_index = 0; field_index < SDL_arraysize(preview_string_fields); ++field_index)
        {
            char field_path[PATH_BUFFER_SIZE];
            format_path(field_path, sizeof(field_path), "%s.%s", preview_path, preview_string_fields[field_index]);
            if (!validate_optional_non_empty_string_value(ctx, obj_get(preview, preview_string_fields[field_index]),
                                                          field_path, "scene editor placement preview field"))
            {
                return false;
            }
        }
        const char *axis = json_string(preview, "axis");
        if (axis != NULL && SDL_strcmp(axis, "x") != 0 && SDL_strcmp(axis, "z") != 0)
            return validation_error(ctx, preview_path, "scene editor placement preview axis must be x or z");
        const char *auto_axis = json_string(preview, "auto_axis");
        if (auto_axis != NULL && SDL_strcmp(auto_axis, "camera_cardinal") != 0)
        {
            return validation_error(ctx, preview_path,
                                    "scene editor placement preview auto_axis must be camera_cardinal");
        }
        const char *auto_axis_camera = json_string(preview, "auto_axis_camera");
        if (auto_axis_camera != NULL && !require_ref(ctx, &names->cameras, "camera", auto_axis_camera, preview_path))
            return false;
        yyjson_val *offset = obj_get(preview, "position_offset");
        if (offset != NULL && !is_exact_vec_array(offset, 3))
            return validation_error(ctx, preview_path, "scene editor placement preview position_offset must be a vec3");
        yyjson_val *snap = obj_get(preview, "snap");
        if (snap != NULL && (!yyjson_is_num(snap) || yyjson_get_num(snap) <= 0.0))
            return validation_error(ctx, preview_path, "scene editor placement preview snap must be positive");
        yyjson_val *snap_key = obj_get(preview, "snap_key");
        if (snap_key != NULL && (!yyjson_is_str(snap_key) || yyjson_get_str(snap_key)[0] == '\0'))
            return validation_error(ctx, preview_path, "scene editor placement preview snap_key must be non-empty");
        const char *snap_mode = json_string(preview, "snap_mode");
        if (snap_mode != NULL && SDL_strcmp(snap_mode, "floor") != 0 && SDL_strcmp(snap_mode, "nearest") != 0)
            return validation_error(ctx, preview_path,
                                    "scene editor placement preview snap_mode must be floor or nearest");
        const char *elevation_mode = json_string(preview, "elevation_mode");
        if (elevation_mode != NULL && SDL_strcmp(elevation_mode, "connected_grid") != 0)
        {
            return validation_error(ctx, preview_path,
                                    "scene editor placement preview elevation_mode must be connected_grid");
        }
        yyjson_val *grid_size = obj_get(preview, "grid_size");
        if (grid_size != NULL && (!yyjson_is_num(grid_size) || yyjson_get_num(grid_size) <= 0.0))
            return validation_error(ctx, preview_path, "scene editor placement preview grid_size must be positive");
        if (SDL_strcmp(kind, "player_start") == 0)
        {
            yyjson_val *size = obj_get(preview, "size");
            if (size != NULL &&
                (!is_exact_vec_array(size, 3) || yyjson_get_num(yyjson_arr_get(size, 0)) <= 0.0 ||
                 yyjson_get_num(yyjson_arr_get(size, 1)) <= 0.0 || yyjson_get_num(yyjson_arr_get(size, 2)) <= 0.0))
            {
                return validation_error(ctx, preview_path,
                                        "scene editor placement player_start size must be a positive vec3");
            }
            continue;
        }
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(preview, "world"), preview_path))
            return false;
        if (!is_non_empty_string(preview, "material"))
            return validation_error(ctx, preview_path, "scene editor placement box preview requires a material");
        char contents_path[PATH_BUFFER_SIZE];
        format_path(contents_path, sizeof(contents_path), "%s.contents", preview_path);
        if (!validate_brush_string_or_string_array(ctx, obj_get(preview, "contents"), contents_path,
                                                   "scene editor placement preview contents", brush_content_name_valid,
                                                   false))
        {
            return false;
        }
        yyjson_val *min = obj_get(preview, "min");
        yyjson_val *max = obj_get(preview, "max");
        yyjson_val *grid_min = obj_get(preview, "grid_min");
        yyjson_val *grid_max = obj_get(preview, "grid_max");
        const bool has_static_bounds = min != NULL || max != NULL;
        const bool has_grid_bounds = grid_min != NULL || grid_max != NULL;
        if (has_static_bounds == has_grid_bounds)
        {
            return validation_error(ctx, preview_path,
                                    "scene editor placement box preview requires exactly one of min/max or "
                                    "grid_min/grid_max bounds");
        }
        yyjson_val *bounds_min = has_grid_bounds ? grid_min : min;
        yyjson_val *bounds_max = has_grid_bounds ? grid_max : max;
        if (!is_exact_vec_array(bounds_min, 3) || !is_exact_vec_array(bounds_max, 3))
        {
            return validation_error(ctx, preview_path,
                                    has_grid_bounds
                                        ? "scene editor placement box preview requires grid_min and grid_max vec3"
                                        : "scene editor placement box preview requires min and max vec3");
        }
        if (!(yyjson_get_num(yyjson_arr_get(bounds_min, 0)) < yyjson_get_num(yyjson_arr_get(bounds_max, 0)) &&
              yyjson_get_num(yyjson_arr_get(bounds_min, 1)) < yyjson_get_num(yyjson_arr_get(bounds_max, 1)) &&
              yyjson_get_num(yyjson_arr_get(bounds_min, 2)) < yyjson_get_num(yyjson_arr_get(bounds_max, 2))))
        {
            return validation_error(ctx, preview_path, "scene editor placement box preview bounds require min < max");
        }
    }
    return true;
}

static bool scene_editor_placement_has_preview_mode(yyjson_val *placement, const char *mode)
{
    yyjson_val *previews = obj_get(placement, "previews");
    for (size_t i = 0; yyjson_is_arr(previews) && i < yyjson_arr_size(previews); ++i)
    {
        yyjson_val *preview = yyjson_arr_get(previews, i);
        if (SDL_strcmp(json_string_default(preview, "mode", ""), mode != NULL ? mode : "") == 0)
            return true;
    }
    return false;
}

static bool validate_scene_editor_palette(validation_context *ctx, yyjson_val *palette, yyjson_val *placement,
                                          const char *palette_path)
{
    if (palette == NULL)
        return true;
    if (!yyjson_is_obj(palette))
        return validation_error(ctx, palette_path, "scene editor palette must be an object");

    char field_path[PATH_BUFFER_SIZE];
    format_path(field_path, sizeof(field_path), "%s.selected_key", palette_path);
    if (!validate_optional_non_empty_string_value(ctx, obj_get(palette, "selected_key"), field_path,
                                                  "scene editor palette selected_key"))
        return false;

    yyjson_val *entries = obj_get(palette, "entries");
    if (!yyjson_is_arr(entries) || yyjson_arr_size(entries) == 0)
        return validation_error(ctx, palette_path, "scene editor palette entries must be a non-empty array");

    for (size_t i = 0; i < yyjson_arr_size(entries); ++i)
    {
        char entry_path[PATH_BUFFER_SIZE];
        format_path(entry_path, sizeof(entry_path), "%s.entries[%zu]", palette_path, i);
        yyjson_val *entry = yyjson_arr_get(entries, i);
        if (!yyjson_is_obj(entry))
            return validation_error(ctx, entry_path, "scene editor palette entry must be an object");
        if (!is_non_empty_string(entry, "mode"))
            return validation_error(ctx, entry_path, "scene editor palette entry requires a non-empty mode");
        if (!is_non_empty_string(entry, "label"))
            return validation_error(ctx, entry_path, "scene editor palette entry requires a non-empty label");

        static const char *const optional_string_fields[] = {"shortcut", "category", "description"};
        for (size_t field_index = 0; field_index < SDL_arraysize(optional_string_fields); ++field_index)
        {
            format_path(field_path, sizeof(field_path), "%s.%s", entry_path, optional_string_fields[field_index]);
            if (!validate_optional_non_empty_string_value(ctx, obj_get(entry, optional_string_fields[field_index]),
                                                          field_path, "scene editor palette entry field"))
                return false;
        }

        const char *kind = json_string_default(entry, "kind", "box");
        if (SDL_strcmp(kind, "box") != 0 && SDL_strcmp(kind, "player_start") != 0 && SDL_strcmp(kind, "thing") != 0)
            return validation_error(ctx, entry_path,
                                    "scene editor palette entry kind must be box, player_start, or thing");

        const char *preview = json_string_default(entry, "preview", json_string(entry, "mode"));
        if (!scene_editor_placement_has_preview_mode(placement, preview))
            return validation_error(ctx, entry_path, "scene editor palette entry references unknown placement preview");
    }
    return true;
}

bool validate_scene_editor_tooling(validation_context *ctx, yyjson_val *scene_root, const char *json_path,
                                   validation_names *names)
{
    yyjson_val *editor = obj_get(scene_root, "editor");
    if (editor == NULL)
        return true;
    if (!yyjson_is_obj(editor))
        return validation_error(ctx, json_path, "scene editor must be an object");

    yyjson_val *selection = obj_get(editor, "selection");
    if (selection != NULL)
    {
        char selection_path[PATH_BUFFER_SIZE];
        format_path(selection_path, sizeof(selection_path), "%s.editor.selection", json_path);
        if (!yyjson_is_obj(selection))
            return validation_error(ctx, selection_path, "scene editor selection must be an object");
        if (!validate_scene_editor_selection_options(ctx, selection, selection_path))
            return false;
        yyjson_val *trace = obj_get(selection, "trace");
        if (!yyjson_is_obj(trace))
            return validation_error(ctx, selection_path, "scene editor selection requires a trace object");
        char trace_path[PATH_BUFFER_SIZE];
        format_path(trace_path, sizeof(trace_path), "%s.trace", selection_path);
        if (!validate_scene_editor_trace(ctx, trace, trace_path, names))
            return false;
        char outputs_path[PATH_BUFFER_SIZE];
        format_path(outputs_path, sizeof(outputs_path), "%s.outputs", selection_path);
        if (!validate_scene_editor_outputs(ctx, obj_get(selection, "outputs"), outputs_path))
            return false;
        char hover_outputs_path[PATH_BUFFER_SIZE];
        format_path(hover_outputs_path, sizeof(hover_outputs_path), "%s.hover_outputs", selection_path);
        if (!validate_scene_editor_outputs(ctx, obj_get(selection, "hover_outputs"), hover_outputs_path))
            return false;
        static const char *const signal_keys[] = {"on_select", "on_secondary_select"};
        for (size_t i = 0; i < SDL_arraysize(signal_keys); ++i)
        {
            yyjson_val *signal = obj_get(selection, signal_keys[i]);
            if (signal == NULL)
                continue;
            char signal_path[PATH_BUFFER_SIZE];
            format_path(signal_path, sizeof(signal_path), "%s.%s", selection_path, signal_keys[i]);
            if (!yyjson_is_str(signal) || yyjson_get_str(signal)[0] == '\0')
                return validation_error(ctx, signal_path, "scene editor selection %s must be a signal", signal_keys[i]);
            if (!require_ref(ctx, &names->signals, "signal", yyjson_get_str(signal), signal_path))
                return false;
        }
    }

    char placement_path[PATH_BUFFER_SIZE];
    format_path(placement_path, sizeof(placement_path), "%s.editor.placement", json_path);
    yyjson_val *placement = obj_get(editor, "placement");
    if (!validate_scene_editor_placement(ctx, placement, placement_path, names))
        return false;

    char palette_path[PATH_BUFFER_SIZE];
    format_path(palette_path, sizeof(palette_path), "%s.editor.palette", json_path);
    if (!validate_scene_editor_palette(ctx, obj_get(editor, "palette"), placement, palette_path))
        return false;

    yyjson_val *overlay = obj_get(editor, "debug_overlay");
    if (overlay != NULL)
    {
        char overlay_path[PATH_BUFFER_SIZE];
        format_path(overlay_path, sizeof(overlay_path), "%s.editor.debug_overlay", json_path);
        if (!yyjson_is_obj(overlay))
            return validation_error(ctx, overlay_path, "scene editor debug_overlay must be an object");
        yyjson_val *enabled = obj_get(overlay, "enabled");
        if (enabled != NULL && !yyjson_is_bool(enabled))
            return validation_error(ctx, overlay_path, "scene editor debug_overlay enabled must be a boolean");
        char flags_path[PATH_BUFFER_SIZE];
        format_path(flags_path, sizeof(flags_path), "%s.flags", overlay_path);
        if (!validate_string_or_string_array_names(ctx, obj_get(overlay, "flags"), flags_path, "editor debug flag",
                                                   editor_debug_flag_name_valid))
            return false;
        char hover_selection_path[PATH_BUFFER_SIZE];
        format_path(hover_selection_path, sizeof(hover_selection_path), "%s.hover_selection_if", overlay_path);
        if (!validate_data_condition(ctx, obj_get(overlay, "hover_selection_if"), hover_selection_path, names))
            return false;
        static const char *const color_keys[] = {
            "world_bounds_color", "selection_bounds_color", "selection_face_color",  "trace_color",
            "face_normal_color",  "hit_marker_color",       "command_preview_color", "work_plane_grid_color",
            "player_start_color", "vertex_handle_color"};
        for (size_t i = 0; i < SDL_arraysize(color_keys); ++i)
        {
            yyjson_val *color = obj_get(overlay, color_keys[i]);
            if (color != NULL && !is_exact_vec3_or_vec4_array(color))
                return validation_error(ctx, overlay_path, "scene editor debug_overlay %s must be a vec3 or vec4 color",
                                        color_keys[i]);
        }
        yyjson_val *normal_length = obj_get(overlay, "normal_length");
        if (normal_length != NULL && (!yyjson_is_num(normal_length) || yyjson_get_num(normal_length) <= 0.0))
            return validation_error(ctx, overlay_path, "scene editor debug_overlay normal_length must be positive");
        yyjson_val *hit_marker_size = obj_get(overlay, "hit_marker_size");
        if (hit_marker_size != NULL && (!yyjson_is_num(hit_marker_size) || yyjson_get_num(hit_marker_size) <= 0.0))
            return validation_error(ctx, overlay_path, "scene editor debug_overlay hit_marker_size must be positive");
        yyjson_val *grid_size = obj_get(overlay, "work_plane_grid_size");
        if (grid_size != NULL && (!yyjson_is_num(grid_size) || yyjson_get_num(grid_size) <= 0.0))
            return validation_error(ctx, overlay_path,
                                    "scene editor debug_overlay work_plane_grid_size must be positive");
        yyjson_val *grid_spacing = obj_get(overlay, "work_plane_grid_spacing");
        if (grid_spacing != NULL && (!yyjson_is_num(grid_spacing) || yyjson_get_num(grid_spacing) <= 0.0))
            return validation_error(ctx, overlay_path,
                                    "scene editor debug_overlay work_plane_grid_spacing must be positive");
        yyjson_val *player_start_radius = obj_get(overlay, "player_start_radius");
        if (player_start_radius != NULL &&
            (!yyjson_is_num(player_start_radius) || yyjson_get_num(player_start_radius) <= 0.0))
            return validation_error(ctx, overlay_path,
                                    "scene editor debug_overlay player_start_radius must be positive");
        yyjson_val *player_start_height = obj_get(overlay, "player_start_height");
        if (player_start_height != NULL &&
            (!yyjson_is_num(player_start_height) || yyjson_get_num(player_start_height) <= 0.0))
            return validation_error(ctx, overlay_path,
                                    "scene editor debug_overlay player_start_height must be positive");
        yyjson_val *markers = obj_get(overlay, "markers");
        if (markers != NULL)
        {
            if (!yyjson_is_arr(markers))
                return validation_error(ctx, overlay_path, "scene editor debug_overlay markers must be an array");
            for (size_t i = 0; i < yyjson_arr_size(markers); ++i)
            {
                char marker_path[PATH_BUFFER_SIZE];
                format_path(marker_path, sizeof(marker_path), "%s.markers[%zu]", overlay_path, i);
                yyjson_val *marker = yyjson_arr_get(markers, i);
                if (!yyjson_is_obj(marker))
                    return validation_error(ctx, marker_path, "scene editor debug marker must be an object");
                if (!is_non_empty_string(marker, "point_key"))
                    return validation_error(ctx, marker_path, "scene editor debug marker requires a point_key");
                yyjson_val *name = obj_get(marker, "name");
                if (name != NULL && (!yyjson_is_str(name) || yyjson_get_str(name)[0] == '\0'))
                    return validation_error(ctx, marker_path, "scene editor debug marker name must be non-empty");
                yyjson_val *world = obj_get(marker, "world");
                if (world != NULL && (!yyjson_is_str(world) || yyjson_get_str(world)[0] == '\0'))
                    return validation_error(ctx, marker_path, "scene editor debug marker world must be non-empty");
                yyjson_val *color = obj_get(marker, "color");
                if (color != NULL && !is_exact_vec3_or_vec4_array(color))
                    return validation_error(ctx, marker_path, "scene editor debug marker color must be a vec3 or vec4");
                yyjson_val *size = obj_get(marker, "size");
                if (size != NULL && (!yyjson_is_num(size) || yyjson_get_num(size) <= 0.0))
                    return validation_error(ctx, marker_path, "scene editor debug marker size must be positive");
                char condition_path[PATH_BUFFER_SIZE];
                format_path(condition_path, sizeof(condition_path), "%s.visible_if", marker_path);
                if (!validate_data_condition(ctx, obj_get(marker, "visible_if"), condition_path, names))
                    return false;
            }
        }
    }
    return true;
}

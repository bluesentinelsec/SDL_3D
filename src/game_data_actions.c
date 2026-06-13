/**
 * @file game_data_actions.c
 * @brief Data-authored action dispatcher.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>

static bool debug_write_all(SDL_IOStream *stream, const char *text)
{
    if (stream == NULL || text == NULL)
        return false;
    const size_t size = SDL_strlen(text);
    return SDL_WriteIO(stream, text, size) == size;
}

static bool debug_make_directory_recursive(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;

    SDL_PathInfo info;
    SDL_zero(info);
    if (SDL_GetPathInfo(path, &info))
        return info.type == SDL_PATHTYPE_DIRECTORY;

    char *copy = SDL_strdup(path);
    if (copy == NULL)
        return false;

    bool ok = true;
    for (char *p = copy + 1; *p != '\0'; ++p)
    {
        if (*p != '/' && *p != '\\')
            continue;
        const char saved = *p;
        *p = '\0';
        if (copy[0] != '\0')
        {
            SDL_zero(info);
            if (!SDL_GetPathInfo(copy, &info))
                ok = SDL_CreateDirectory(copy);
            else
                ok = info.type == SDL_PATHTYPE_DIRECTORY;
        }
        *p = saved;
        if (!ok)
            break;
    }

    if (ok)
    {
        SDL_zero(info);
        if (!SDL_GetPathInfo(copy, &info))
            ok = SDL_CreateDirectory(copy);
        else
            ok = info.type == SDL_PATHTYPE_DIRECTORY;
    }

    SDL_free(copy);
    return ok;
}

static char *debug_parent_directory(const char *path)
{
    if (path == NULL)
        return NULL;
    const char *slash = SDL_strrchr(path, '/');
    const char *backslash = SDL_strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
    if (slash == NULL)
        return SDL_strdup(".");
    const size_t len = (size_t)(slash - path);
    if (len == 0u)
        return SDL_strdup("/");
    char *parent = (char *)SDL_malloc(len + 1u);
    if (parent == NULL)
        return NULL;
    SDL_memcpy(parent, path, len);
    parent[len] = '\0';
    return parent;
}

static bool debug_ensure_parent_directory(const char *path)
{
    char *parent = debug_parent_directory(path);
    if (parent == NULL)
        return false;
    const bool ok = debug_make_directory_recursive(parent);
    SDL_free(parent);
    return ok;
}

static SDL_IOStream *debug_open_property_dump(const char *path, bool append)
{
    if (path == NULL || path[0] == '\0')
        return NULL;
    if (!debug_ensure_parent_directory(path))
        return NULL;
    if (!append)
        return SDL_IOFromFile(path, "wb");

    SDL_IOStream *stream = SDL_IOFromFile(path, "r+b");
    if (stream == NULL)
        return SDL_IOFromFile(path, "wb");
    if (SDL_SeekIO(stream, 0, SDL_IO_SEEK_END) < 0)
    {
        (void)SDL_CloseIO(stream);
        return NULL;
    }
    return stream;
}

static bool debug_write_property_value(SDL_IOStream *stream, const char *key, const slayer3d_value *value)
{
    char line[256];
    if (stream == NULL || key == NULL)
        return false;
    if (value == NULL)
        SDL_snprintf(line, sizeof(line), "%s=<missing>\n", key);
    else if (value->type == SLAYER3D_VALUE_INT)
        SDL_snprintf(line, sizeof(line), "%s=%d\n", key, value->as_int);
    else if (value->type == SLAYER3D_VALUE_FLOAT)
        SDL_snprintf(line, sizeof(line), "%s=%.6f\n", key, value->as_float);
    else if (value->type == SLAYER3D_VALUE_BOOL)
        SDL_snprintf(line, sizeof(line), "%s=%s\n", key, value->as_bool ? "true" : "false");
    else if (value->type == SLAYER3D_VALUE_STRING)
        SDL_snprintf(line, sizeof(line), "%s=%s\n", key, value->as_string != NULL ? value->as_string : "");
    else if (value->type == SLAYER3D_VALUE_VEC3)
        SDL_snprintf(line, sizeof(line), "%s=[%.6f, %.6f, %.6f]\n", key, value->as_vec3.x, value->as_vec3.y,
                     value->as_vec3.z);
    else if (value->type == SLAYER3D_VALUE_COLOR)
        SDL_snprintf(line, sizeof(line), "%s=[%u, %u, %u, %u]\n", key, value->as_color.r, value->as_color.g,
                     value->as_color.b, value->as_color.a);
    else
        SDL_snprintf(line, sizeof(line), "%s=<unsupported>\n", key);
    return debug_write_all(stream, line);
}

static bool debug_write_actor_properties(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                         const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    const char *path = json_string(action, "path", NULL);
    yyjson_val *properties = obj_get(action, "properties");
    if (actor == NULL || path == NULL || path[0] == '\0' || !yyjson_is_arr(properties))
        return false;

    const bool append = json_bool(action, "append", false);
    SDL_IOStream *stream = debug_open_property_dump(path, append);
    if (stream == NULL)
        return false;

    bool ok = debug_write_all(stream, "# Slayer 3D actor property dump\n");
    char line[256];
    SDL_snprintf(line, sizeof(line), "actor=%s\n", actor->name != NULL ? actor->name : "");
    ok = debug_write_all(stream, line) && ok;
    const char *label = json_string(action, "label", NULL);
    if (label != NULL)
    {
        SDL_snprintf(line, sizeof(line), "label=%s\n", label);
        ok = debug_write_all(stream, line) && ok;
    }
    ok = debug_write_all(stream, "\n") && ok;

    for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
    {
        const char *key = yyjson_get_str(yyjson_arr_get(properties, i));
        if (key != NULL)
            ok = debug_write_property_value(stream, key, slayer3d_properties_get_value(actor->props, key)) && ok;
    }

    ok = SDL_CloseIO(stream) && ok;
    if (ok)
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "wrote actor property dump: %s", path);
    return ok;
}

static bool console_write_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                 const slayer3d_properties *payload)
{
    if (runtime == NULL || runtime->scene_state == NULL || action == NULL)
        return false;

    char message[512];
    const char *message_template = json_string(action, "message", NULL);
    const char *message_from_state = json_string(action, "message_from_state", NULL);
    if (message_template != NULL)
    {
        if (!format_payload_string(payload, message_template, message, sizeof(message)))
            return false;
    }
    else if (message_from_state != NULL)
    {
        SDL_strlcpy(message, slayer3d_properties_get_string(runtime->scene_state, message_from_state, ""),
                    sizeof(message));
    }
    else
    {
        return false;
    }

    if (message[0] == '\0')
        return false;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message);

    const char *line_key_prefix = json_string(action, "line_key_prefix", "editor.console.line");
    int line_count = json_int(action, "line_count", 5);
    line_count = SDL_clamp(line_count, 1, 8);

    char previous[8][512];
    SDL_zeroa(previous);
    for (int i = 0; i < line_count; ++i)
    {
        char key[128];
        SDL_snprintf(key, sizeof(key), "%s%d", line_key_prefix, i);
        SDL_strlcpy(previous[i], slayer3d_properties_get_string(runtime->scene_state, key, ""), sizeof(previous[i]));
    }

    for (int i = line_count - 1; i > 0; --i)
    {
        char key[128];
        SDL_snprintf(key, sizeof(key), "%s%d", line_key_prefix, i);
        slayer3d_properties_set_string(runtime->scene_state, key, previous[i - 1]);
    }

    char key[128];
    SDL_snprintf(key, sizeof(key), "%s0", line_key_prefix);
    slayer3d_properties_set_string(runtime->scene_state, key, message);
    const char *count_key = json_string(action, "count_key", "editor.console.count");
    if (count_key != NULL && count_key[0] != '\0')
    {
        slayer3d_properties_set_int(
            runtime->scene_state, count_key,
            SDL_min(line_count, slayer3d_properties_get_int(runtime->scene_state, count_key, 0) + 1));
    }
    return true;
}

static bool editor_selected_brushes_bounds(const slayer3d_game_data_runtime *runtime, slayer3d_bounding_box *out_bounds)
{
    if (runtime == NULL || out_bounds == NULL || runtime->editor_selected_brush_count <= 0)
        return false;

    bool has_bounds = false;
    slayer3d_bounding_box bounds;
    SDL_zero(bounds);
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!selection.hit || !selection.has_bounds)
            continue;
        if (!has_bounds)
        {
            bounds = selection.bounds;
            has_bounds = true;
            continue;
        }
        bounds.min.x = SDL_min(bounds.min.x, selection.bounds.min.x);
        bounds.min.y = SDL_min(bounds.min.y, selection.bounds.min.y);
        bounds.min.z = SDL_min(bounds.min.z, selection.bounds.min.z);
        bounds.max.x = SDL_max(bounds.max.x, selection.bounds.max.x);
        bounds.max.y = SDL_max(bounds.max.y, selection.bounds.max.y);
        bounds.max.z = SDL_max(bounds.max.z, selection.bounds.max.z);
    }
    if (!has_bounds)
        return false;

    *out_bounds = bounds;
    return true;
}

static bool editor_selected_brushes_bounds_center(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 *out_center)
{
    if (out_center == NULL)
        return false;
    slayer3d_bounding_box bounds;
    if (!editor_selected_brushes_bounds(runtime, &bounds))
        return false;
    *out_center = slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
    return true;
}

static bool editor_selection_rotate_selected_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_vec3 pivot = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (obj_get(action, "pivot") != NULL)
        pivot = json_vec3(action, "pivot", pivot);
    else if (!editor_selected_brushes_bounds_center(runtime, &pivot))
        return false;

    const slayer3d_vec3 axis = json_vec3(action, "axis", slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    const float angle_radians = obj_get(action, "angle_radians") != NULL
                                    ? json_float(action, "angle_radians", 0.0f)
                                    : slayer3d_degrees_to_radians(json_float(action, "angle_degrees", 0.0f));
    return slayer3d_game_data_rotate_selected_editor_brushes(runtime, pivot, axis, angle_radians);
}

static bool editor_selection_scale_selected_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_vec3 anchor = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (obj_get(action, "anchor") != NULL)
        anchor = json_vec3(action, "anchor", anchor);
    else if (!editor_selected_brushes_bounds_center(runtime, &anchor))
        return false;

    const slayer3d_vec3 factors = json_vec3(action, "factors", slayer3d_vec3_make(1.0f, 1.0f, 1.0f));
    return slayer3d_game_data_scale_selected_editor_brushes(runtime, anchor, factors);
}

static bool editor_selection_shear_selected_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_bounding_box bounds;
    if (obj_get(action, "bounds_min") != NULL && obj_get(action, "bounds_max") != NULL)
    {
        bounds.min = json_vec3(action, "bounds_min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        bounds.max = json_vec3(action, "bounds_max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    }
    else if (!editor_selected_brushes_bounds(runtime, &bounds))
    {
        return false;
    }

    const slayer3d_vec3 side_normal = json_vec3(action, "side_normal", slayer3d_vec3_make(1.0f, 0.0f, 0.0f));
    const slayer3d_vec3 delta = json_vec3(action, "delta", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return slayer3d_game_data_shear_selected_editor_brushes(runtime, bounds, side_normal, delta);
}

static const char *editor_flip_axis_label(slayer3d_vec3 normal)
{
    const float abs_x = SDL_fabsf(normal.x);
    const float abs_y = SDL_fabsf(normal.y);
    const float abs_z = SDL_fabsf(normal.z);
    if (abs_x >= abs_y && abs_x >= abs_z)
        return normal.x >= 0.0f ? "+X" : "-X";
    if (abs_y >= abs_x && abs_y >= abs_z)
        return normal.y >= 0.0f ? "+Y" : "-Y";
    return normal.z >= 0.0f ? "+Z" : "-Z";
}

static slayer3d_vec3 editor_flip_dominant_axis_vector(slayer3d_vec3 v)
{
    const float abs_x = SDL_fabsf(v.x);
    const float abs_y = SDL_fabsf(v.y);
    const float abs_z = SDL_fabsf(v.z);
    if (abs_x >= abs_y && abs_x >= abs_z)
        return slayer3d_vec3_make(v.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
    if (abs_y >= abs_x && abs_y >= abs_z)
        return slayer3d_vec3_make(0.0f, v.y < 0.0f ? -1.0f : 1.0f, 0.0f);
    return slayer3d_vec3_make(0.0f, 0.0f, v.z < 0.0f ? -1.0f : 1.0f);
}

static bool editor_flip_vec3_same_direction(slayer3d_vec3 a, slayer3d_vec3 b)
{
    return SDL_fabsf(a.x - b.x) <= 0.0001f && SDL_fabsf(a.y - b.y) <= 0.0001f && SDL_fabsf(a.z - b.z) <= 0.0001f;
}

static slayer3d_vec3 editor_flip_perspective_forward_axis(slayer3d_vec3 forward, slayer3d_vec3 up)
{
    slayer3d_vec3 projected = slayer3d_vec3_make(forward.x, 0.0f, forward.z);
    if (slayer3d_vec3_length_squared(projected) <= 0.000001f)
    {
        projected = slayer3d_vec3_make(up.x, 0.0f, up.z);
        if (forward.y > 0.0f)
            projected = slayer3d_vec3_negate(projected);
    }
    if (slayer3d_vec3_length_squared(projected) <= 0.000001f)
        return slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    return editor_flip_dominant_axis_vector(projected);
}

static slayer3d_vec3 editor_flip_perspective_right_axis(slayer3d_vec3 right, slayer3d_vec3 forward_axis)
{
    slayer3d_vec3 axis = editor_flip_dominant_axis_vector(right);
    if (editor_flip_vec3_same_direction(axis, forward_axis))
    {
        axis = slayer3d_vec3_cross(axis, slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
        if (slayer3d_vec3_length_squared(axis) <= 0.000001f)
            axis = slayer3d_vec3_make(0.0f, 0.0f, 1.0f);
    }
    return editor_flip_dominant_axis_vector(axis);
}

static bool editor_flip_normal_valid(slayer3d_vec3 normal, const char *adjective, char *message, size_t message_size)
{
    if (SDL_isnan(normal.x) || SDL_isinf(normal.x) || SDL_isnan(normal.y) || SDL_isinf(normal.y) ||
        SDL_isnan(normal.z) || SDL_isinf(normal.z) || slayer3d_vec3_length_squared(normal) <= 0.000001f)
    {
        if (message != NULL && message_size > 0u)
            SDL_snprintf(message, message_size, "%s flip requires a finite non-zero normal", adjective);
        return false;
    }
    return true;
}

static bool editor_selection_flip_vertical_normal(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                  slayer3d_vec3 *out_normal, const char **out_axis_label, char *message,
                                                  size_t message_size)
{
    if (out_normal == NULL)
        return false;
    if (message != NULL && message_size > 0u)
        message[0] = '\0';

    slayer3d_vec3 normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    if (obj_get(action, "normal") != NULL)
    {
        normal = json_vec3(action, "normal", normal);
    }
    else
    {
        const char *normal_key = json_string(action, "normal_key", NULL);
        const slayer3d_value *normal_value =
            runtime != NULL && runtime->scene_state != NULL && normal_key != NULL && normal_key[0] != '\0'
                ? slayer3d_properties_get_value(runtime->scene_state, normal_key)
                : NULL;
        if (normal_value != NULL && normal_value->type == SLAYER3D_VALUE_VEC3)
        {
            normal = normal_value->as_vec3;
        }
        else
        {
            const char *view_mode_key = json_string(action, "view_mode_key", "editor.view.mode");
            const char *view_mode =
                runtime != NULL && runtime->scene_state != NULL && view_mode_key != NULL && view_mode_key[0] != '\0'
                    ? slayer3d_properties_get_string(runtime->scene_state, view_mode_key, "flyby_3d")
                    : "flyby_3d";
            const char *top_mode = json_string(action, "top_mode", "orthographic_top");
            const char *front_mode = json_string(action, "front_mode", "orthographic_front");
            const char *side_mode = json_string(action, "side_mode", "orthographic_side");
            const char *flyby_mode = json_string(action, "flyby_mode", "flyby_3d");
            if (SDL_strcmp(view_mode, top_mode) == 0)
                normal = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
            else if (SDL_strcmp(view_mode, front_mode) == 0 || SDL_strcmp(view_mode, side_mode) == 0 ||
                     SDL_strcmp(view_mode, flyby_mode) == 0)
                normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
            else
            {
                if (message != NULL && message_size > 0u)
                    SDL_snprintf(message, message_size, "vertical flip unsupported for view '%s'", view_mode);
                return false;
            }
        }
    }

    if (!editor_flip_normal_valid(normal, "vertical", message, message_size))
        return false;
    normal = slayer3d_vec3_normalize(normal);
    *out_normal = normal;
    if (out_axis_label != NULL)
        *out_axis_label = editor_flip_axis_label(normal);
    return true;
}

static bool editor_selection_flip_horizontal_normal(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                    slayer3d_vec3 *out_normal, const char **out_axis_label,
                                                    char *message, size_t message_size)
{
    if (out_normal == NULL)
        return false;
    if (message != NULL && message_size > 0u)
        message[0] = '\0';

    slayer3d_vec3 normal = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    if (obj_get(action, "normal") != NULL)
    {
        normal = json_vec3(action, "normal", normal);
    }
    else
    {
        const char *normal_key = json_string(action, "normal_key", NULL);
        const slayer3d_value *normal_value =
            runtime != NULL && runtime->scene_state != NULL && normal_key != NULL && normal_key[0] != '\0'
                ? slayer3d_properties_get_value(runtime->scene_state, normal_key)
                : NULL;
        if (normal_value != NULL && normal_value->type == SLAYER3D_VALUE_VEC3)
        {
            normal = normal_value->as_vec3;
        }
        else
        {
            slayer3d_vec3 forward;
            slayer3d_vec3 right;
            slayer3d_vec3 up;
            bool orthographic = false;
            if (editor_camera_basis(runtime, &forward, &right, &up, &orthographic))
            {
                if (orthographic)
                    normal = editor_flip_dominant_axis_vector(right);
                else
                {
                    const slayer3d_vec3 forward_axis = editor_flip_perspective_forward_axis(forward, up);
                    normal = editor_flip_perspective_right_axis(right, forward_axis);
                }
            }
            else
            {
                const char *view_mode_key = json_string(action, "view_mode_key", "editor.view.mode");
                const char *view_mode =
                    runtime != NULL && runtime->scene_state != NULL && view_mode_key != NULL && view_mode_key[0] != '\0'
                        ? slayer3d_properties_get_string(runtime->scene_state, view_mode_key, "flyby_3d")
                        : "flyby_3d";
                const char *top_mode = json_string(action, "top_mode", "orthographic_top");
                const char *front_mode = json_string(action, "front_mode", "orthographic_front");
                const char *side_mode = json_string(action, "side_mode", "orthographic_side");
                const char *flyby_mode = json_string(action, "flyby_mode", "flyby_3d");
                if (SDL_strcmp(view_mode, top_mode) == 0 || SDL_strcmp(view_mode, front_mode) == 0 ||
                    SDL_strcmp(view_mode, flyby_mode) == 0)
                {
                    normal = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
                }
                else if (SDL_strcmp(view_mode, side_mode) == 0)
                {
                    normal = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
                }
                else
                {
                    if (message != NULL && message_size > 0u)
                        SDL_snprintf(message, message_size, "horizontal flip unsupported for view '%s'", view_mode);
                    return false;
                }
            }
        }
    }

    if (!editor_flip_normal_valid(normal, "horizontal", message, message_size))
        return false;
    normal = slayer3d_vec3_normalize(normal);
    *out_normal = normal;
    if (out_axis_label != NULL)
        *out_axis_label = editor_flip_axis_label(normal);
    return true;
}

static bool editor_selection_flip_vertical_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    slayer3d_vec3 pivot = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (obj_get(action, "pivot") != NULL)
        pivot = json_vec3(action, "pivot", pivot);
    else if (!editor_selected_brushes_bounds_center(runtime, &pivot))
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", false);
        editor_set_string_output(scene_state, outputs, "message_key", "select brushes before vertical flip");
        if (scene_state != NULL)
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action",
                                           "select brushes before vertical flip");
        return false;
    }

    slayer3d_vec3 normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    const char *axis_label = "+Y";
    char message[128];
    if (!editor_selection_flip_vertical_normal(runtime, action, &normal, &axis_label, message, sizeof(message)))
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", false);
        editor_set_string_output(scene_state, outputs, "message_key",
                                 message[0] != '\0' ? message : "vertical flip failed");
        if (scene_state != NULL)
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action",
                                           message[0] != '\0' ? message : "vertical flip failed");
        return false;
    }

    const bool flipped = slayer3d_game_data_flip_selected_editor_brushes(runtime, pivot, normal);
    SDL_snprintf(message, sizeof(message),
                 flipped ? "flipped selected brushes vertically around %s" : "vertical flip failed around %s",
                 axis_label);
    editor_set_bool_output(scene_state, outputs, "valid_key", flipped);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    editor_set_string_output(scene_state, outputs, "axis_key", axis_label);
    editor_set_vec3_output(scene_state, outputs, "center_key", pivot);
    if (scene_state != NULL && !flipped)
        slayer3d_properties_set_string(scene_state, "editor.tool.last_action", message);
    return flipped;
}

static bool editor_selection_flip_horizontal_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    slayer3d_vec3 pivot = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (obj_get(action, "pivot") != NULL)
        pivot = json_vec3(action, "pivot", pivot);
    else if (!editor_selected_brushes_bounds_center(runtime, &pivot))
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", false);
        editor_set_string_output(scene_state, outputs, "message_key", "select brushes before horizontal flip");
        if (scene_state != NULL)
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action",
                                           "select brushes before horizontal flip");
        return false;
    }

    slayer3d_vec3 normal = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    const char *axis_label = "+X";
    char message[128];
    if (!editor_selection_flip_horizontal_normal(runtime, action, &normal, &axis_label, message, sizeof(message)))
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", false);
        editor_set_string_output(scene_state, outputs, "message_key",
                                 message[0] != '\0' ? message : "horizontal flip failed");
        if (scene_state != NULL)
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action",
                                           message[0] != '\0' ? message : "horizontal flip failed");
        return false;
    }

    const bool flipped = slayer3d_game_data_flip_selected_editor_brushes_horizontal(runtime, pivot, normal);
    SDL_snprintf(message, sizeof(message),
                 flipped ? "flipped selected brushes horizontally around %s" : "horizontal flip failed around %s",
                 axis_label);
    editor_set_bool_output(scene_state, outputs, "valid_key", flipped);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    editor_set_string_output(scene_state, outputs, "axis_key", axis_label);
    editor_set_vec3_output(scene_state, outputs, "center_key", pivot);
    if (scene_state != NULL && !flipped)
        slayer3d_properties_set_string(scene_state, "editor.tool.last_action", message);
    return flipped;
}

static bool editor_brush_duplicate_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_properties *scene_state = runtime != NULL ? runtime->scene_state : NULL;
    const char *mode = json_string(action, "mode", "in_place");
    slayer3d_vec3 offset = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    bool use_last_offset = false;

    if (SDL_strcmp(mode, "last_offset") == 0)
    {
        use_last_offset = true;
    }
    else if (SDL_strcmp(mode, "offset") == 0)
    {
        offset = json_vec3(action, "offset", offset);
    }
    else
    {
        mode = "in_place";
    }

    if (runtime == NULL || runtime->editor_selected_brush_count <= 0)
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", false);
        editor_set_int_output(scene_state, outputs, "count_key", 0);
        editor_set_string_output(scene_state, outputs, "mode_key", mode);
        editor_set_vec3_output(scene_state, outputs, "offset_key", offset);
        editor_set_string_output(scene_state, outputs, "message_key", "select brushes before duplicating");
        if (scene_state != NULL)
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action", "select brushes before duplicating");
        return false;
    }

    const bool duplicated = slayer3d_game_data_duplicate_selected_editor_brushes(runtime, offset, use_last_offset);
    const int count = duplicated ? runtime->editor_selected_brush_count : 0;
    const char *message = duplicated && scene_state != NULL
                              ? slayer3d_properties_get_string(scene_state, "editor.tool.last_action", "")
                              : "selected brushes could not be duplicated";
    if (duplicated && use_last_offset && runtime->editor_has_last_duplicate_offset)
        offset = runtime->editor_last_duplicate_offset;

    editor_set_bool_output(scene_state, outputs, "valid_key", duplicated);
    editor_set_int_output(scene_state, outputs, "count_key", count);
    editor_set_string_output(scene_state, outputs, "mode_key", mode);
    editor_set_vec3_output(scene_state, outputs, "offset_key", offset);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    return duplicated;
}

bool execute_one_action(slayer3d_game_data_runtime *runtime, yyjson_val *action, const slayer3d_properties *payload)
{
    const char *type = json_string(action, "type", "");
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    slayer3d_timer_pool *timers = runtime_timers(runtime);

    if (SDL_strcmp(type, "signal.emit") == 0)
    {
        const int signal_id = action_signal_id(runtime, action, "signal");
        if (signal_id >= 0 && bus != NULL)
            slayer3d_signal_emit(bus, signal_id, payload);
        return signal_id >= 0;
    }

    if (SDL_strcmp(type, "timer.start") == 0)
    {
        const int timer_index = find_timer_index(runtime, json_string(action, "timer", NULL));
        if (timer_index < 0 || timers == NULL)
            return false;
        const named_timer *timer = &runtime->timers[timer_index];
        return slayer3d_timer_start(timers, json_float(action, "delay", timer->delay), timer->signal_id,
                                    timer->repeating, timer->interval) != 0;
    }

    if (SDL_strcmp(type, "property.set") == 0 || SDL_strcmp(type, "property.add") == 0)
    {
        slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
        const char *key = json_string(action, "key", NULL);
        yyjson_val *value = obj_get(action, "value");
        const char *value_from_payload = json_string(action, "value_from_payload", NULL);
        const slayer3d_value *payload_value = value_from_payload != NULL && payload != NULL
                                                  ? slayer3d_properties_get_value(payload, value_from_payload)
                                                  : NULL;
        if (actor == NULL || key == NULL || (value == NULL && payload_value == NULL))
            return false;

        if (SDL_strcmp(type, "property.add") == 0)
        {
            const slayer3d_value *existing = slayer3d_properties_get_value(actor->props, key);
            if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && payload_value != NULL &&
                payload_value->type == SLAYER3D_VALUE_INT)
            {
                slayer3d_properties_set_int(actor->props, key, existing->as_int + payload_value->as_int);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && payload_value != NULL &&
                     payload_value->type == SLAYER3D_VALUE_FLOAT)
            {
                slayer3d_properties_set_float(actor->props, key, existing->as_float + payload_value->as_float);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && payload_value != NULL &&
                     payload_value->type == SLAYER3D_VALUE_INT)
            {
                slayer3d_properties_set_float(actor->props, key, existing->as_float + (float)payload_value->as_int);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && payload_value != NULL &&
                     payload_value->type == SLAYER3D_VALUE_FLOAT)
            {
                slayer3d_properties_set_float(actor->props, key, (float)existing->as_int + payload_value->as_float);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && yyjson_is_num(value))
                slayer3d_properties_set_int(actor->props, key, existing->as_int + (int)yyjson_get_int(value));
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && yyjson_is_num(value))
                slayer3d_properties_set_float(actor->props, key, existing->as_float + (float)yyjson_get_real(value));
            else
                return false;
        }
        else if (payload_value != NULL)
        {
            copy_property_value(actor->props, key, payload_value);
        }
        else
        {
            set_actor_property_from_json(actor, key, value);
        }
        return true;
    }

    if (SDL_strcmp(type, "property.snapshot") == 0)
        return snapshot_actor_properties(runtime, action);

    if (SDL_strcmp(type, "property.restore_snapshot") == 0)
        return restore_actor_property_snapshot(runtime, action);

    if (SDL_strcmp(type, "property.animate") == 0)
        return start_property_animation_from_json(runtime, action, payload);

    if (SDL_strcmp(type, "property.reset_defaults") == 0)
        return reset_actor_properties_to_authored_defaults(runtime, action);

    if (SDL_strcmp(type, "debug.write_actor_properties") == 0)
        return debug_write_actor_properties(runtime, action, payload);

    if (SDL_strcmp(type, "input.reset_bindings") == 0)
        return slayer3d_game_data_reset_menu_input_bindings(runtime, json_string(action, "menu", NULL));

    if (SDL_strcmp(type, "input.apply_profile") == 0)
    {
        char error[256] = {0};
        slayer3d_input_manager *input = runtime_input(runtime);
        const char *profile = json_string(action, "profile", NULL);
        if (!slayer3d_game_data_apply_input_profile(runtime, input, profile, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data input profile apply failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        return true;
    }

    if (SDL_strcmp(type, "input.apply_active_profile") == 0)
    {
        char error[256] = {0};
        const char *profile = NULL;
        slayer3d_input_manager *input = runtime_input(runtime);
        if (!slayer3d_game_data_apply_active_input_profile(runtime, input, &profile, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data active input profile apply failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "game data active input profile applied: profile=%s",
                    profile != NULL ? profile : "<none>");
        return true;
    }

    if (SDL_strcmp(type, "input.clear_network_input_overrides") == 0)
    {
        char error[256] = {0};
        slayer3d_input_manager *input = runtime_input(runtime);
        const char *channel = json_string(action, "channel", NULL);
        if (!slayer3d_game_data_clear_network_input_overrides(runtime, channel, input, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data network input override clear failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        return true;
    }

    if (SDL_strcmp(type, "scene_state.set") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        yyjson_val *value = obj_get(action, "value");
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0' || value == NULL)
            return false;
        return set_property_from_json_with_payload(runtime->scene_state, key, value, payload);
    }

    if (SDL_strcmp(type, "scene_state.toggle") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
            return false;
        const bool current = scene_state_bool(runtime, key, json_bool(action, "default", false));
        slayer3d_properties_set_bool(runtime->scene_state, key, !current);
        return true;
    }

    if (SDL_strcmp(type, "scene_state.cycle") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        yyjson_val *values = obj_get(action, "values");
        const size_t count = yyjson_is_arr(values) ? yyjson_arr_size(values) : 0U;
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0' || count == 0U)
            return false;

        const slayer3d_value *current = slayer3d_properties_get_value(runtime->scene_state, key);
        slayer3d_value fallback;
        if (current == NULL && json_scalar_to_value(obj_get(action, "default"), &fallback))
            current = &fallback;
        const int direction = json_int(action, "direction", 1);
        size_t next = 0U;
        for (size_t i = 0; i < count; ++i)
        {
            yyjson_val *value = yyjson_arr_get(values, i);
            if (json_value_matches_property(value, current))
            {
                const int signed_count = (int)count;
                int signed_next = ((int)i + direction) % signed_count;
                if (signed_next < 0)
                    signed_next += signed_count;
                next = (size_t)signed_next;
                break;
            }
        }

        return set_property_from_json(runtime->scene_state, key, yyjson_arr_get(values, next));
    }

    if (SDL_strcmp(type, "console.write") == 0)
        return console_write_action(runtime, action, payload);

    if (SDL_strcmp(type, "editor.selection.clear") == 0)
        return slayer3d_game_data_clear_active_editor_selection(runtime);

    if (SDL_strcmp(type, "editor.vertex.selection.clear") == 0)
        return slayer3d_game_data_clear_editor_vertex_selection(runtime);

    if (SDL_strcmp(type, "editor.edge.selection.clear") == 0)
        return slayer3d_game_data_clear_editor_edge_selection(runtime);

    if (SDL_strcmp(type, "editor.tool.set_mode") == 0)
        return slayer3d_game_data_set_editor_tool_mode(runtime, json_string(action, "mode", NULL),
                                                       json_string(action, "message", NULL));

    if (SDL_strcmp(type, "editor.escape") == 0)
    {
        if (runtime == NULL || runtime->scene_state == NULL)
            return false;
        if (editor_mode_is_paint(runtime) ||
            slayer3d_properties_get_bool(runtime->scene_state, "editor.texture.viewer.active", false) ||
            slayer3d_properties_get_bool(runtime->scene_state, "editor.actor.viewer.active", false))
        {
            slayer3d_properties_set_string(runtime->scene_state, "editor.palette.active", "");
            slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.viewer.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.viewer.collapsed", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.viewer.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.viewer.collapsed", false);
            (void)slayer3d_game_data_set_editor_tool_mode(runtime, "select", NULL);
            return true;
        }
        if (SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.palette.active", ""), "") != 0)
        {
            slayer3d_properties_set_string(runtime->scene_state, "editor.palette.active", "");
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "palette closed");
            return true;
        }
        if (slayer3d_properties_get_int(runtime->scene_state, "editor.vertex.selection.count", 0) > 0)
        {
            if (!slayer3d_game_data_clear_editor_vertex_selection(runtime))
                return false;
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "vertex selection cleared");
            return true;
        }

        const char *mode = slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select");
        if (SDL_strcmp(mode, "clip") == 0)
            return slayer3d_game_data_escape_editor_clip_tool(runtime);
        if (SDL_strcmp(mode, "select") != 0)
            return slayer3d_game_data_set_editor_tool_mode(runtime, "select", NULL);
        if (slayer3d_properties_get_int(runtime->scene_state, "editor.selection.count", 0) > 0)
        {
            if (!slayer3d_game_data_clear_active_editor_selection(runtime))
                return false;
            (void)slayer3d_game_data_clear_editor_command_preview(runtime, action);
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "selection cleared");
            return true;
        }
        return slayer3d_game_data_clear_editor_command_preview(runtime, action);
    }

    if (SDL_strcmp(type, "editor.clip.cancel") == 0)
        return slayer3d_game_data_cancel_editor_clip_tool(runtime, json_string(action, "message", NULL));

    if (SDL_strcmp(type, "editor.clip.escape") == 0)
        return slayer3d_game_data_escape_editor_clip_tool(runtime);

    if (SDL_strcmp(type, "editor.clip.cycle_keep_mode") == 0)
        return slayer3d_game_data_cycle_editor_clip_keep_mode(runtime);

    if (SDL_strcmp(type, "editor.clip.commit") == 0)
        return slayer3d_game_data_commit_editor_clip_tool(runtime);

    if (SDL_strcmp(type, "editor.placement_preview.cancel") == 0)
        return editor_cancel_pending_brush_preview(runtime, json_string(action, "message", NULL));

    if (SDL_strcmp(type, "editor.vertex.snap_selected") == 0)
        return slayer3d_game_data_snap_selected_editor_vertices(runtime, action);

    if (SDL_strcmp(type, "editor.vertex.delete_selected") == 0)
        return slayer3d_game_data_delete_selected_editor_vertices(runtime, action);

    if (SDL_strcmp(type, "editor.vertex.merge_selected_to_hover") == 0)
        return slayer3d_game_data_merge_selected_editor_vertices_to_hover(runtime, action);

    if (SDL_strcmp(type, "editor.vertex.add_to_source") == 0)
        return slayer3d_game_data_add_editor_vertex_to_source(runtime, action);

    if (SDL_strcmp(type, "editor.vertex.validate_source") == 0)
        return slayer3d_game_data_validate_editor_vertex_source(runtime, action);

    if (SDL_strcmp(type, "editor.selection.select_brush") == 0)
        return slayer3d_game_data_select_editor_brush_action(runtime, action);

    if (SDL_strcmp(type, "editor.selection.delete_selected") == 0)
        return slayer3d_game_data_delete_selected_editor_brushes(runtime, action, payload);

    if (SDL_strcmp(type, "editor.selection.resize_y") == 0)
        return slayer3d_game_data_resize_selected_editor_brushes_y(runtime, action, payload);

    if (SDL_strcmp(type, "editor.selection.rotate_selected") == 0)
        return editor_selection_rotate_selected_action(runtime, action);

    if (SDL_strcmp(type, "editor.selection.scale_selected") == 0)
        return editor_selection_scale_selected_action(runtime, action);

    if (SDL_strcmp(type, "editor.selection.flip_vertical") == 0)
        return editor_selection_flip_vertical_action(runtime, action);

    if (SDL_strcmp(type, "editor.selection.flip_horizontal") == 0)
        return editor_selection_flip_horizontal_action(runtime, action);

    if (SDL_strcmp(type, "editor.brush.duplicate") == 0)
        return editor_brush_duplicate_action(runtime, action);

    if (SDL_strcmp(type, "editor.brush.paint") == 0)
        return slayer3d_game_data_paint_selected_editor_brushes(runtime, action, payload);

    if (SDL_strcmp(type, "editor.selection.shear_selected") == 0)
        return editor_selection_shear_selected_action(runtime, action);

    if (SDL_strcmp(type, "editor.selection.run") == 0)
    {
        slayer3d_game_data_editor_selection selection;
        if (slayer3d_game_data_get_active_editor_selection(runtime, &selection))
        {
            slayer3d_properties *selection_payload = slayer3d_game_data_create_editor_selection_payload(&selection);
            if (selection_payload == NULL)
                return false;
            const bool ok = execute_action_array(runtime, obj_get(action, "actions"), selection_payload);
            slayer3d_properties_destroy(selection_payload);
            return ok;
        }
        return execute_optional_action_array(runtime, obj_get(action, "else"), payload);
    }

    if (SDL_strcmp(type, "editor.command.preview") == 0)
        return slayer3d_game_data_preview_editor_command(runtime, action);

    if (SDL_strcmp(type, "editor.command.clear_preview") == 0)
        return slayer3d_game_data_clear_editor_command_preview(runtime, action);

    if (SDL_strcmp(type, "editor.command.commit") == 0)
    {
        if (SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", ""), "clip") == 0)
            return slayer3d_game_data_commit_editor_clip_tool(runtime);
        return slayer3d_game_data_commit_editor_command(runtime, action, payload);
    }

    if (SDL_strcmp(type, "editor.command.undo") == 0)
        return slayer3d_game_data_undo_editor_command(runtime, action, payload);

    if (SDL_strcmp(type, "editor.command.redo") == 0)
        return slayer3d_game_data_redo_editor_command(runtime, action, payload);

    if (SDL_strcmp(type, "editor.brush_world.export") == 0)
        return slayer3d_game_data_export_editor_brush_world_action(runtime, action);

    if (SDL_strcmp(type, "editor.level.export") == 0)
        return slayer3d_game_data_export_editor_level_action(runtime, action);

    if (SDL_strcmp(type, "editor.level.save") == 0)
        return slayer3d_game_data_save_editor_level_action(runtime, action);

    if (SDL_strcmp(type, "editor.level.load") == 0)
        return slayer3d_game_data_load_editor_level_action(runtime, action);

    if (SDL_strcmp(type, "editor.test_run.prepare") == 0)
        return slayer3d_game_data_prepare_editor_test_run_action(runtime, action);

    if (SDL_strcmp(type, "editor.test_run.save_manifest") == 0)
        return slayer3d_game_data_save_editor_test_run_manifest_action(runtime, action);

    if (SDL_strcmp(type, "editor.brush_world.status") == 0)
        return slayer3d_game_data_publish_editor_brush_world_status_action(runtime, action);

    if (SDL_strcmp(type, "editor.brush_world.validate_source") == 0)
        return slayer3d_game_data_validate_editor_brush_source_action(runtime, action);

    if (SDL_strcmp(type, "editor.brush_world.validate_enclosure") == 0)
        return slayer3d_game_data_validate_editor_brush_enclosure_action(runtime, action);

    if (SDL_strcmp(type, "editor.brush_world.create_box") == 0)
        return slayer3d_game_data_create_box_brush_action(runtime, action);

    if (SDL_strcmp(type, "editor.player_start.place") == 0)
        return slayer3d_game_data_place_editor_player_start_action(runtime, action);

    if (SDL_strcmp(type, "editor.player_start.apply") == 0)
        return slayer3d_game_data_apply_editor_player_start_action(runtime, action);

    if (SDL_strcmp(type, "editor.player_start.delete") == 0)
        return slayer3d_game_data_delete_editor_player_start_action(runtime, action);

    if (SDL_strcmp(type, "editor.actor.place") == 0)
        return slayer3d_game_data_place_editor_actor_action(runtime, action);

    if (SDL_strcmp(type, "network.direct_connect.start") == 0)
    {
        const char *name = json_string(action, "name", NULL);
        const char *host_key = json_string(action, "host_key", NULL);
        const char *port_key = json_string(action, "port_key", NULL);
        const char *host = runtime != NULL && host_key != NULL
                               ? slayer3d_properties_get_string(runtime->scene_state, host_key,
                                                                json_string(action, "default_host", "127.0.0.1"))
                               : json_string(action, "host", json_string(action, "default_host", "127.0.0.1"));
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const char *port_text = runtime != NULL && port_key != NULL
                                    ? slayer3d_properties_get_string(runtime->scene_state, port_key, NULL)
                                    : NULL;
        const int port = port_text != NULL ? SDL_atoi(port_text) : json_int_or_string(action, "port", default_port);
        return slayer3d_game_data_network_direct_connect_start(
            runtime, name, host, port, json_string(action, "status_key", NULL), json_string(action, "state_key", NULL),
            json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.direct_connect.cancel") == 0)
    {
        char status[256];
        const char *status_text = json_string(action, "status", "Disconnected");
        (void)format_payload_string(payload, status_text, status, sizeof(status));
        return slayer3d_game_data_network_direct_connect_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL), status);
    }

    if (SDL_strcmp(type, "network.direct_connect.observe") == 0)
    {
        return slayer3d_game_data_network_direct_connect_publish_status(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.host.start") == 0)
    {
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const int port = json_int_or_string(action, "port", default_port);
        return slayer3d_game_data_network_host_start(
            runtime, json_string(action, "name", NULL), port,
            json_string(action, "session_name", json_string(action, "advertised_name", NULL)),
            json_string(action, "status_key", NULL), json_string(action, "endpoint_key", NULL),
            json_string(action, "peer_key", NULL), json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.host.cancel") == 0)
    {
        char status[256];
        const char *status_text = json_string(action, "status", "Not hosting");
        (void)format_payload_string(payload, status_text, status, sizeof(status));
        return slayer3d_game_data_network_host_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "endpoint_key", NULL), json_string(action, "peer_key", NULL),
            json_string(action, "connected_key", NULL), status);
    }

    if (SDL_strcmp(type, "network.host.observe") == 0)
    {
        return slayer3d_game_data_network_host_publish_status(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "endpoint_key", NULL), json_string(action, "peer_key", NULL),
            json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.start") == 0 || SDL_strcmp(type, "network.discovery.refresh") == 0)
    {
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const int port = json_int_or_string(action, "port", default_port);
        const int local_port = json_int_or_string(action, "local_port", 0);
        return slayer3d_game_data_network_discovery_start(
            runtime, json_string(action, "name", NULL), json_string(action, "host", NULL), port, local_port,
            json_string(action, "collection", NULL), json_string(action, "status_key", NULL),
            json_string(action, "count_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.observe") == 0)
    {
        return slayer3d_game_data_network_discovery_update(
            runtime, json_string(action, "name", NULL),
            json_float(action, "dt", json_float(action, "update_seconds", 0.016f)),
            json_string(action, "collection", NULL), json_string(action, "status_key", NULL),
            json_string(action, "count_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.cancel") == 0)
    {
        return slayer3d_game_data_network_discovery_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "collection", NULL),
            json_string(action, "status_key", NULL), json_string(action, "count_key", NULL),
            json_string(action, "status", "Discovery canceled"));
    }

    if (SDL_strcmp(type, "network.discovery.connect_selected") == 0)
    {
        const char *index_key = json_string(action, "selected_index_key", NULL);
        const int selected_index =
            runtime != NULL && runtime->scene_state != NULL && index_key != NULL
                ? slayer3d_properties_get_int(runtime->scene_state, index_key, json_int(action, "selected_index", 0))
                : json_int(action, "selected_index", 0);
        return slayer3d_game_data_network_discovery_connect_selected(
            runtime, json_string(action, "name", NULL), json_string(action, "collection", NULL), selected_index,
            json_string(action, "direct_connect_name", NULL), json_string(action, "host_key", NULL),
            json_string(action, "port_key", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL),
            json_string(action, "connecting_status", NULL));
    }

    if (SDL_strcmp(type, "ui.animate") == 0)
        return start_ui_animation_from_json(runtime, action);

    if (SDL_strncmp(type, "audio.", 6) == 0)
        return execute_audio_action(runtime, action, payload, type);

    if (SDL_strncmp(type, "persistence.", 12) == 0)
        return execute_persistence_action(runtime, action, type);

    if (SDL_strcmp(type, "entity.set_active") == 0)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        yyjson_val *active = obj_get(action, "active");
        if (actor == NULL || !yyjson_is_bool(active))
            return false;
        actor->active = yyjson_get_bool(active);
        return true;
    }

    if (SDL_strcmp(type, "actor.spawn") == 0)
        return execute_actor_spawn_action(runtime, action, payload);

    if (SDL_strcmp(type, "actor.despawn") == 0)
        return execute_actor_despawn_action_with_payload(runtime, action, payload);

    if (SDL_strcmp(type, "actor.despawn_by_tag") == 0)
        return execute_actor_despawn_by_tag_action(runtime, action);

    if (SDL_strcmp(type, "combat.damage") == 0)
        return execute_combat_damage_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.heal") == 0)
        return execute_combat_heal_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.kill") == 0)
        return execute_combat_kill_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.revive") == 0)
        return execute_combat_revive_action(runtime, action, payload);
    if (SDL_strcmp(type, "resource.add") == 0)
        return execute_resource_amount_action(runtime, action, payload, false);
    if (SDL_strcmp(type, "resource.consume") == 0)
        return execute_resource_amount_action(runtime, action, payload, true);
    if (SDL_strcmp(type, "resource.set") == 0)
        return execute_resource_set_action(runtime, action, payload);
    if (SDL_strcmp(type, "pickup.collect") == 0)
        return execute_pickup_collect_action(runtime, action, payload);
    if (SDL_strcmp(type, "resource.station.use") == 0)
        return execute_resource_station_use_action(runtime, action, payload);
    if (SDL_strcmp(type, "status_effect.apply") == 0)
        return execute_status_effect_apply_action(runtime, action, payload);
    if (SDL_strcmp(type, "weapon.reload") == 0)
        return execute_weapon_reload_action(runtime, action, payload);
    if (SDL_strcmp(type, "weapon.hitscan") == 0)
        return execute_weapon_hitscan_action(runtime, action, payload);
    if (SDL_strcmp(type, "interaction.use") == 0)
        return execute_interaction_use_action(runtime, action, payload);
    if (SDL_strcmp(type, "effect.explosion") == 0)
        return execute_effect_explosion_action(runtime, action, payload);
    if (SDL_strcmp(type, "noise.emit") == 0)
        return execute_noise_emit_action(runtime, action, payload);

    if (SDL_strcmp(type, "projectile.fire") == 0)
        return execute_projectile_fire_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.launch") == 0 || SDL_strcmp(type, "controller.fps_sector.launch") == 0)
        return execute_fps_controller_launch_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.teleport") == 0 || SDL_strcmp(type, "controller.fps_sector.teleport") == 0)
        return execute_fps_controller_teleport_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.push") == 0)
        return execute_fps_controller_push_action(runtime, action, payload);
    if (SDL_strcmp(type, "grid.spawn_from_glyphs") == 0)
        return execute_grid_spawn_from_glyphs_action(runtime, action);
    if (SDL_strcmp(type, "grid.spawn_runs_from_glyphs") == 0)
        return execute_grid_spawn_runs_from_glyphs_action(runtime, action);
    if (SDL_strcmp(type, "grid.pickup_layer.reset") == 0)
        return execute_grid_pickup_layer_reset_action(runtime, action);

    if (SDL_strcmp(type, "sector_door.open") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "open");
    if (SDL_strcmp(type, "sector_door.close") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "close");
    if (SDL_strcmp(type, "sector_door.toggle") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "toggle");
    if (SDL_strcmp(type, "sector_door.interact") == 0)
        return execute_sector_door_interact_action(runtime, action);
    if (SDL_strcmp(type, "sector_lighting.set") == 0)
    {
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        yyjson_val *color_json = obj_get(action, "color");
        if (yyjson_is_arr(color_json) && yyjson_arr_size(color_json) == 3U)
        {
            (void)json_float_array(color_json, color, 3, color);
            color[3] = 1.0f;
        }
        else
        {
            (void)json_float_array(color_json, color, 4, color);
        }
        char error[256] = {0};
        char sector_index_text[32];
        const char *sector = json_string(action, "sector", NULL);
        yyjson_val *sector_index_value = obj_get(action, "sector_index");
        if (sector == NULL && yyjson_is_int(sector_index_value))
        {
            SDL_snprintf(sector_index_text, sizeof(sector_index_text), "%d", (int)yyjson_get_int(sector_index_value));
            sector = sector_index_text;
        }
        const bool ok = slayer3d_game_data_set_sector_lighting(runtime, json_string(action, "sector_level", NULL),
                                                               sector, json_float(action, "level", 255.0f), color,
                                                               error, (int)sizeof(error));
        if (!ok)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sector_lighting.set failed: %s",
                        error[0] != '\0' ? error : "unknown error");
        return ok;
    }

    if (SDL_strcmp(type, "transform.set_position") == 0)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        if (actor == NULL)
            return false;
        actor_set_position(actor, json_vec3(action, "position", actor->position));
        return true;
    }

    if (SDL_strcmp(type, "camera.toggle") == 0)
    {
        const char *camera = json_string(action, "camera", NULL);
        const char *fallback = json_string(action, "fallback", NULL);
        runtime->active_camera =
            runtime->active_camera != NULL && camera != NULL && SDL_strcmp(runtime->active_camera, camera) == 0
                ? fallback
                : camera;
        return runtime->active_camera != NULL;
    }

    if (SDL_strcmp(type, "camera.set") == 0)
    {
        const char *camera = json_string(action, "camera", NULL);
        if (camera == NULL)
            return false;
        runtime->active_camera = camera;
        return true;
    }

    if (SDL_strcmp(type, "scene.set") == 0)
    {
        yyjson_val *authored_payload = obj_get(action, "payload");
        if (authored_payload == NULL)
            return slayer3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL),
                                                                    payload);

        slayer3d_properties *scene_payload = properties_from_json_payload(authored_payload, payload);
        if (scene_payload == NULL)
            return false;
        const bool ok = slayer3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL),
                                                                         scene_payload);
        slayer3d_properties_destroy(scene_payload);
        return ok;
    }

    if (SDL_strcmp(type, "adapter.invoke") == 0)
    {
        const char *adapter_name = json_string(action, "adapter", NULL);
        adapter_entry *adapter = find_adapter(runtime, adapter_name);
        if (adapter == NULL)
            return false;
        const char *target_name = json_string(action, "target", NULL);
        if ((target_name == NULL || target_name[0] == '\0') && payload != NULL)
            target_name = slayer3d_properties_get_string(payload, "actor_name", NULL);
        slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, target_name);
        return invoke_adapter(runtime, adapter, target, payload);
    }

    if (SDL_strcmp(type, "branch") == 0)
    {
        yyjson_val *condition = obj_get(action, "if");
        const bool passed = eval_data_condition_with_payload(runtime, condition, NULL, payload);
        return execute_optional_action_array(runtime, obj_get(action, passed ? "then" : "else"), payload);
    }

    return false;
}

bool execute_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions, const slayer3d_properties *payload)
{
    if (!yyjson_is_arr(actions))
        return false;

    actor_lifecycle_defer_begin(runtime);
    bool ok = true;
    for (size_t i = 0; i < yyjson_arr_size(actions); ++i)
    {
        yyjson_val *action = yyjson_arr_get(actions, i);
        if (yyjson_is_obj(action))
            ok = execute_one_action(runtime, action, payload) && ok;
    }
    actor_lifecycle_defer_end(runtime);
    return ok;
}

bool execute_optional_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                   const slayer3d_properties *payload)
{
    if (actions == NULL)
        return true;
    return execute_action_array(runtime, actions, payload);
}

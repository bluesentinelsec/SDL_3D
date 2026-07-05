/**
 * @file game_data_editor_active_selection.c
 * @brief Active editor selection and selection payload helpers.
 */

#include "game_data_internal.h"

#include <float.h>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/camera.h"
#include "slayer3d/math.h"

static bool editor_selection_button_requested(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                              const char *key, const char *fallback)
{
    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return false;

    const Uint8 button = mouse_button_from_json(json_string(selection, key, fallback));
    return button != 0 && slayer3d_input_get_pressed_mouse_button(input) == button;
}

static bool emit_editor_selection_signal(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                         const char *signal_key, const slayer3d_game_data_editor_selection *selection)
{
    const char *signal = json_string(selection_json, signal_key, NULL);
    if (signal == NULL)
        return true;

    slayer3d_signal_bus *bus = runtime_bus(runtime);
    const int signal_id = slayer3d_game_data_find_signal(runtime, signal);
    if (bus == NULL || signal_id < 0)
        return false;

    slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, selection);
    slayer3d_properties *payload = slayer3d_game_data_create_editor_selection_payload(&resolved);
    if (payload == NULL)
        return false;
    slayer3d_signal_emit(bus, signal_id, payload);
    slayer3d_properties_destroy(payload);
    return true;
}

static float editor_snap_delta(float delta, float grid_size)
{
    const float grid = SDL_max(grid_size, 0.001f);
    return SDL_roundf(delta / grid) * grid;
}

static void clear_editor_drag_move(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    SDL_zero(runtime->editor_drag_move);
}

static const char *editor_metadata_stable_id(const slayer3d_game_data_editor_metadata *metadata);
static int editor_dominant_axis_index(slayer3d_vec3 v);

static void editor_set_face_drag_state(slayer3d_game_data_runtime *runtime, bool ready, bool dragging)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.face.drag.ready", ready);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.face.drag.active", dragging);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.face.drag.shift",
                                 (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
}

static slayer3d_vec3 editor_bounds_dimensions(slayer3d_bounding_box bounds)
{
    return slayer3d_vec3_make(SDL_max(0.0f, bounds.max.x - bounds.min.x), SDL_max(0.0f, bounds.max.y - bounds.min.y),
                              SDL_max(0.0f, bounds.max.z - bounds.min.z));
}

static void publish_editor_face_resize_feedback(slayer3d_game_data_runtime *runtime, float distance,
                                                slayer3d_bounding_box bounds, bool has_bounds)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const slayer3d_vec3 dimensions =
        has_bounds ? editor_bounds_dimensions(bounds) : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.brush.resize.distance", distance);
    slayer3d_properties_set_float(runtime->scene_state, "editor.brush.resize.dimension_x", dimensions.x);
    slayer3d_properties_set_float(runtime->scene_state, "editor.brush.resize.dimension_y", dimensions.y);
    slayer3d_properties_set_float(runtime->scene_state, "editor.brush.resize.dimension_z", dimensions.z);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.brush.resize.dimensions", dimensions);
}

static void publish_editor_drag_move_feedback(slayer3d_game_data_runtime *runtime, slayer3d_vec3 delta)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_float(runtime->scene_state, "editor.brush.drag.delta_x", delta.x);
    slayer3d_properties_set_float(runtime->scene_state, "editor.brush.drag.delta_y", delta.y);
    slayer3d_properties_set_float(runtime->scene_state, "editor.brush.drag.delta_z", delta.z);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.brush.drag.delta", delta);
}

static bool editor_reject_locked_transform_start(slayer3d_game_data_runtime *runtime, bool *out_consumed)
{
    if (!slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
        return false;

    if (out_consumed != NULL)
        *out_consumed = true;
    clear_editor_drag_move(runtime);
    (void)slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
    return true;
}

static bool editor_selected_bounds(const slayer3d_game_data_runtime *runtime, slayer3d_bounding_box *out_bounds)
{
    if (runtime == NULL || out_bounds == NULL || !editor_selected_brushes_active_for_scene(runtime) ||
        runtime->editor_selected_brush_count <= 0)
    {
        return false;
    }

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

static bool editor_selected_bounds_center(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 *out_center)
{
    if (out_center == NULL)
        return false;
    slayer3d_bounding_box bounds;
    if (!editor_selected_bounds(runtime, &bounds))
        return false;
    *out_center = slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
    return true;
}

static float editor_rotate_tool_radius(slayer3d_bounding_box bounds)
{
    const slayer3d_vec3 size = slayer3d_vec3_sub(bounds.max, bounds.min);
    return SDL_max(SDL_max(size.x, SDL_max(size.y, size.z)) * 0.75f, 0.75f);
}

static void editor_rotate_axis_basis(slayer3d_vec3 axis, slayer3d_vec3 *out_a, slayer3d_vec3 *out_b)
{
    const int axis_index = editor_dominant_axis_index(axis);
    if (axis_index == 0)
    {
        *out_a = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
        *out_b = slayer3d_vec3_make(0.0f, 0.0f, 1.0f);
    }
    else if (axis_index == 1)
    {
        *out_a = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        *out_b = slayer3d_vec3_make(0.0f, 0.0f, 1.0f);
    }
    else
    {
        *out_a = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        *out_b = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    }
}

static float editor_screen_segment_distance_squared(float px, float py, float ax, float ay, float bx, float by)
{
    const float ab_x = bx - ax;
    const float ab_y = by - ay;
    const float ab_len_squared = ab_x * ab_x + ab_y * ab_y;
    float t = 0.0f;
    if (ab_len_squared > 0.000001f)
        t = SDL_clamp(((px - ax) * ab_x + (py - ay) * ab_y) / ab_len_squared, 0.0f, 1.0f);
    const float closest_x = ax + ab_x * t;
    const float closest_y = ay + ab_y * t;
    const float dx = px - closest_x;
    const float dy = py - closest_y;
    return dx * dx + dy * dy;
}

static float editor_screen_vector_length(float x, float y)
{
    return SDL_sqrtf(x * x + y * y);
}

static bool editor_rotate_screen_angle(float pivot_x, float pivot_y, float axis_a_x, float axis_a_y, float axis_b_x,
                                       float axis_b_y, float mouse_x, float mouse_y, float *out_angle)
{
    if (out_angle == NULL)
        return false;
    const float a_len = editor_screen_vector_length(axis_a_x, axis_a_y);
    const float b_len = editor_screen_vector_length(axis_b_x, axis_b_y);
    if (a_len <= 0.000001f || b_len <= 0.000001f)
        return false;
    const float rel_x = mouse_x - pivot_x;
    const float rel_y = mouse_y - pivot_y;
    const float a = rel_x * (axis_a_x / a_len) + rel_y * (axis_a_y / a_len);
    const float b = rel_x * (axis_b_x / b_len) + rel_y * (axis_b_y / b_len);
    if (a * a + b * b <= 0.000001f)
        return false;
    *out_angle = SDL_atan2f(b, a);
    return true;
}

static bool editor_rotate_axis_screen_basis(const slayer3d_camera3d *camera, const editor_trace_viewport_config *view,
                                            slayer3d_vec3 pivot, float radius, slayer3d_vec3 axis,
                                            editor_drag_move_state *drag)
{
    if (camera == NULL || view == NULL || drag == NULL)
        return false;
    slayer3d_vec3 axis_a;
    slayer3d_vec3 axis_b;
    editor_rotate_axis_basis(axis, &axis_a, &axis_b);
    float pivot_x = 0.0f;
    float pivot_y = 0.0f;
    float axis_a_x = 0.0f;
    float axis_a_y = 0.0f;
    float axis_b_x = 0.0f;
    float axis_b_y = 0.0f;
    if (!editor_project_world_to_viewport(camera, view, pivot, &pivot_x, &pivot_y) ||
        !editor_project_world_to_viewport(camera, view, slayer3d_vec3_add(pivot, slayer3d_vec3_scale(axis_a, radius)),
                                          &axis_a_x, &axis_a_y) ||
        !editor_project_world_to_viewport(camera, view, slayer3d_vec3_add(pivot, slayer3d_vec3_scale(axis_b, radius)),
                                          &axis_b_x, &axis_b_y))
    {
        return false;
    }
    drag->rotate_pivot_screen_x = view->x + pivot_x;
    drag->rotate_pivot_screen_y = view->y + pivot_y;
    drag->rotate_axis_screen_a_x = axis_a_x - pivot_x;
    drag->rotate_axis_screen_a_y = axis_a_y - pivot_y;
    drag->rotate_axis_screen_b_x = axis_b_x - pivot_x;
    drag->rotate_axis_screen_b_y = axis_b_y - pivot_y;
    drag->rotate_screen_basis_valid =
        editor_screen_vector_length(drag->rotate_axis_screen_a_x, drag->rotate_axis_screen_a_y) > 0.000001f &&
        editor_screen_vector_length(drag->rotate_axis_screen_b_x, drag->rotate_axis_screen_b_y) > 0.000001f;
    return drag->rotate_screen_basis_valid;
}

static float editor_rotate_ring_screen_distance_squared(const slayer3d_camera3d *camera,
                                                        const editor_trace_viewport_config *view, slayer3d_vec3 pivot,
                                                        float radius, slayer3d_vec3 axis, float mouse_x, float mouse_y)
{
    slayer3d_vec3 axis_a;
    slayer3d_vec3 axis_b;
    editor_rotate_axis_basis(axis, &axis_a, &axis_b);
    const int segments = 48;
    float best = FLT_MAX;
    float previous_x = 0.0f;
    float previous_y = 0.0f;
    bool has_previous = false;
    for (int segment = 0; segment <= segments; ++segment)
    {
        const float angle = ((float)segment / (float)segments) * SDL_PI_F * 2.0f;
        const slayer3d_vec3 point =
            slayer3d_vec3_add(pivot, slayer3d_vec3_add(slayer3d_vec3_scale(axis_a, SDL_cosf(angle) * radius),
                                                       slayer3d_vec3_scale(axis_b, SDL_sinf(angle) * radius)));
        float x = 0.0f;
        float y = 0.0f;
        if (!editor_project_world_to_viewport(camera, view, point, &x, &y))
        {
            has_previous = false;
            continue;
        }
        if (has_previous)
        {
            const float distance =
                editor_screen_segment_distance_squared(mouse_x, mouse_y, previous_x, previous_y, x, y);
            if (distance < best)
                best = distance;
        }
        previous_x = x;
        previous_y = y;
        has_previous = true;
    }
    return best;
}

static bool editor_pick_rotate_axis_at(slayer3d_game_data_runtime *runtime, float mouse_x, float mouse_y,
                                       slayer3d_vec3 pivot, float radius, slayer3d_vec3 *out_axis,
                                       editor_drag_move_state *drag)
{
    if (runtime == NULL || out_axis == NULL || drag == NULL)
        return false;
    yyjson_val *editor = active_editor_tooling_root(runtime);
    yyjson_val *selection = obj_get(editor, "selection");
    yyjson_val *trace = obj_get(selection, "trace");
    editor_trace_viewport_config viewport;
    if (!editor_trace_select_viewport_at(runtime, trace, mouse_x, mouse_y, &viewport))
        return false;
    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, viewport.camera, &camera))
        return false;

    const float local_mouse_x = mouse_x - viewport.x;
    const float local_mouse_y = mouse_y - viewport.y;
    const slayer3d_vec3 axes[3] = {
        slayer3d_vec3_make(1.0f, 0.0f, 0.0f),
        slayer3d_vec3_make(0.0f, 1.0f, 0.0f),
        slayer3d_vec3_make(0.0f, 0.0f, 1.0f),
    };
    int best_axis = -1;
    float best_distance = FLT_MAX;
    for (int i = 0; i < 3; ++i)
    {
        const float distance = editor_rotate_ring_screen_distance_squared(&camera, &viewport, pivot, radius, axes[i],
                                                                          local_mouse_x, local_mouse_y);
        if (distance < best_distance)
        {
            best_distance = distance;
            best_axis = i;
        }
    }

    const float handle_pick_radius_pixels = 28.0f;
    if (best_axis < 0 || best_distance > handle_pick_radius_pixels * handle_pick_radius_pixels)
        return false;
    slayer3d_vec3 axis = axes[best_axis];
    *out_axis = axis;
    if (!editor_rotate_axis_screen_basis(&camera, &viewport, pivot, radius, axis, drag))
        return false;
    float start_angle = 0.0f;
    if (editor_rotate_screen_angle(drag->rotate_pivot_screen_x, drag->rotate_pivot_screen_y,
                                   drag->rotate_axis_screen_a_x, drag->rotate_axis_screen_a_y,
                                   drag->rotate_axis_screen_b_x, drag->rotate_axis_screen_b_y, mouse_x, mouse_y,
                                   &start_angle))
    {
        drag->rotate_start_angle_radians = start_angle;
    }
    else
    {
        drag->rotate_screen_basis_valid = false;
    }
    return true;
}

static float editor_rotate_drag_angle(const editor_drag_move_state *drag, float mouse_x, float mouse_y)
{
    float raw_degrees = 0.0f;
    if (drag != NULL && drag->rotate_screen_basis_valid)
    {
        const float tangent_x = -drag->rotate_axis_screen_a_x * SDL_sinf(drag->rotate_start_angle_radians) +
                                drag->rotate_axis_screen_b_x * SDL_cosf(drag->rotate_start_angle_radians);
        const float tangent_y = -drag->rotate_axis_screen_a_y * SDL_sinf(drag->rotate_start_angle_radians) +
                                drag->rotate_axis_screen_b_y * SDL_cosf(drag->rotate_start_angle_radians);
        const float tangent_len = editor_screen_vector_length(tangent_x, tangent_y);
        if (tangent_len > 0.000001f)
        {
            const float mouse_dx = mouse_x - drag->start_mouse_x;
            const float mouse_dy = mouse_y - drag->start_mouse_y;
            raw_degrees = (mouse_dx * (tangent_x / tangent_len) + mouse_dy * (tangent_y / tangent_len)) * 0.75f;
        }
    }
    else if (drag != NULL)
    {
        raw_degrees = (mouse_x - drag->start_mouse_x) * 0.75f;
    }
    const float snap_degrees = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 1.0f : 5.0f;
    return slayer3d_degrees_to_radians(SDL_roundf(raw_degrees / snap_degrees) * snap_degrees);
}

static bool editor_update_rotate_hover_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_rotate(runtime) ||
        runtime->editor_drag_move.rotate_drag)
    {
        return false;
    }

    slayer3d_bounding_box bounds;
    slayer3d_vec3 axis = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    bool hovered = false;
    if (editor_selected_bounds(runtime, &bounds))
    {
        slayer3d_input_manager *input = runtime_input(runtime);
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        if (input != NULL && slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        {
            const slayer3d_vec3 pivot = slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
            editor_drag_move_state pick;
            SDL_zero(pick);
            hovered = editor_pick_rotate_axis_at(runtime, mouse_x, mouse_y, pivot, editor_rotate_tool_radius(bounds),
                                                 &axis, &pick);
        }
    }

    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.hovered", hovered);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.rotate.hover_axis", axis);
    if (hovered)
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.rotate.axis", axis);
    return hovered;
}

void reset_editor_rotate_tool_state(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_vec3 pivot = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const bool has_pivot = editor_selected_bounds_center(runtime, &pivot);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.ready", has_pivot);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.dragging", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.hovered", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.live_preview", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.valid", has_pivot);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.rotate.pivot", pivot);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.rotate.axis", slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.rotate.hover_axis",
                                 slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_float(runtime->scene_state, "editor.rotate.angle_radians", 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.rotate.angle_degrees", 0.0f);
    slayer3d_properties_set_string(runtime->scene_state, "editor.rotate.message",
                                   message != NULL ? message
                                                   : (has_pivot ? "rotate tool" : "select a brush before rotate tool"));
}

void reset_editor_scale_tool_state(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_bounding_box bounds;
    const bool has_bounds = editor_selected_bounds(runtime, &bounds);
    const slayer3d_vec3 anchor = has_bounds ? slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f)
                                            : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.ready", has_bounds);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.dragging", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.hovered", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.live_preview", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.valid", has_bounds);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.proportional", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.center_anchor", false);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.scale.anchor", anchor);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.scale.factors", slayer3d_vec3_make(1.0f, 1.0f, 1.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.scale.handle_axes",
                                 slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_string(runtime->scene_state, "editor.scale.handle", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.scale.message",
                                   message != NULL ? message
                                                   : (has_bounds ? "scale tool" : "select a brush before scale tool"));
}

void reset_editor_shear_tool_state(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_bounding_box bounds;
    const bool has_bounds = editor_selected_bounds(runtime, &bounds);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.ready", has_bounds);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.dragging", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.hovered", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.live_preview", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.valid", has_bounds);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.vertical", false);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.shear.side_normal",
                                 slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.shear.delta", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.shear.axis", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_string(runtime->scene_state, "editor.shear.side", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.shear.message",
                                   message != NULL ? message
                                                   : (has_bounds ? "shear tool" : "select a brush before shear tool"));
}

static slayer3d_vec3 editor_scale_bounds_point(slayer3d_bounding_box bounds, slayer3d_vec3 signs)
{
    const slayer3d_vec3 center = slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
    return slayer3d_vec3_make(signs.x < -0.5f ? bounds.min.x : (signs.x > 0.5f ? bounds.max.x : center.x),
                              signs.y < -0.5f ? bounds.min.y : (signs.y > 0.5f ? bounds.max.y : center.y),
                              signs.z < -0.5f ? bounds.min.z : (signs.z > 0.5f ? bounds.max.z : center.z));
}

static int editor_scale_active_axis_count(slayer3d_vec3 signs)
{
    int count = 0;
    if (SDL_fabsf(signs.x) > 0.5f)
        count++;
    if (SDL_fabsf(signs.y) > 0.5f)
        count++;
    if (SDL_fabsf(signs.z) > 0.5f)
        count++;
    return count;
}

static const char *editor_scale_handle_name(slayer3d_vec3 signs)
{
    const int axes = editor_scale_active_axis_count(signs);
    return axes == 1 ? "side" : (axes == 2 ? "edge" : (axes == 3 ? "corner" : ""));
}

static bool editor_pick_scale_handle_at(slayer3d_game_data_runtime *runtime, float mouse_x, float mouse_y,
                                        slayer3d_bounding_box bounds, slayer3d_vec3 *out_signs)
{
    if (runtime == NULL || out_signs == NULL)
        return false;
    yyjson_val *editor = active_editor_tooling_root(runtime);
    yyjson_val *selection = obj_get(editor, "selection");
    yyjson_val *trace = obj_get(selection, "trace");
    editor_trace_viewport_config viewport;
    if (!editor_trace_select_viewport_at(runtime, trace, mouse_x, mouse_y, &viewport))
        return false;
    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, viewport.camera, &camera))
        return false;

    int best_x = 0;
    int best_y = 0;
    int best_z = 0;
    float best_distance = FLT_MAX;
    for (int sx = -1; sx <= 1; ++sx)
    {
        for (int sy = -1; sy <= 1; ++sy)
        {
            for (int sz = -1; sz <= 1; ++sz)
            {
                if (sx == 0 && sy == 0 && sz == 0)
                    continue;
                float screen_x = 0.0f;
                float screen_y = 0.0f;
                const slayer3d_vec3 point =
                    editor_scale_bounds_point(bounds, slayer3d_vec3_make((float)sx, (float)sy, (float)sz));
                if (!editor_project_world_to_viewport(&camera, &viewport, point, &screen_x, &screen_y))
                    continue;
                const float dx = mouse_x - (viewport.x + screen_x);
                const float dy = mouse_y - (viewport.y + screen_y);
                const float distance = dx * dx + dy * dy;
                if (distance < best_distance)
                {
                    best_distance = distance;
                    best_x = sx;
                    best_y = sy;
                    best_z = sz;
                }
            }
        }
    }
    const float handle_pick_radius_pixels = 18.0f;
    if (best_distance > handle_pick_radius_pixels * handle_pick_radius_pixels)
        return false;
    *out_signs = slayer3d_vec3_make((float)best_x, (float)best_y, (float)best_z);
    return true;
}

static slayer3d_vec3 editor_scale_anchor_for_handle(slayer3d_bounding_box bounds, slayer3d_vec3 signs,
                                                    bool center_anchor)
{
    if (center_anchor)
        return slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
    return editor_scale_bounds_point(bounds, slayer3d_vec3_scale(signs, -1.0f));
}

static slayer3d_vec3 editor_shear_side_center(slayer3d_bounding_box bounds, slayer3d_vec3 normal)
{
    const slayer3d_vec3 center = slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
    if (SDL_fabsf(normal.x) > 0.5f)
        return slayer3d_vec3_make(normal.x > 0.0f ? bounds.max.x : bounds.min.x, center.y, center.z);
    if (SDL_fabsf(normal.y) > 0.5f)
        return slayer3d_vec3_make(center.x, normal.y > 0.0f ? bounds.max.y : bounds.min.y, center.z);
    return slayer3d_vec3_make(center.x, center.y, normal.z > 0.0f ? bounds.max.z : bounds.min.z);
}

static const char *editor_shear_side_name(slayer3d_vec3 normal)
{
    if (normal.x > 0.5f)
        return "right";
    if (normal.x < -0.5f)
        return "left";
    if (normal.y > 0.5f)
        return "top";
    if (normal.y < -0.5f)
        return "bottom";
    if (normal.z > 0.5f)
        return "front";
    if (normal.z < -0.5f)
        return "back";
    return "";
}

static bool editor_shear_axis_side_normal(slayer3d_vec3 normal, slayer3d_vec3 *out_normal)
{
    if (out_normal == NULL || slayer3d_vec3_length_squared(normal) <= 0.000001f)
        return false;

    const float ax = SDL_fabsf(normal.x);
    const float ay = SDL_fabsf(normal.y);
    const float az = SDL_fabsf(normal.z);
    if (ax >= ay && ax >= az)
        *out_normal = slayer3d_vec3_make(normal.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
    else if (ay >= az)
        *out_normal = slayer3d_vec3_make(0.0f, normal.y < 0.0f ? -1.0f : 1.0f, 0.0f);
    else
        *out_normal = slayer3d_vec3_make(0.0f, 0.0f, normal.z < 0.0f ? -1.0f : 1.0f);
    return true;
}

static bool editor_pick_shear_side_from_hover(slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_editor_selection *hover_selection,
                                              slayer3d_vec3 *out_normal)
{
    if (runtime == NULL || out_normal == NULL || editor_selected_brush_index(runtime, hover_selection) < 0 ||
        hover_selection->face_index < 0)
    {
        return false;
    }
    return editor_shear_axis_side_normal(hover_selection->normal, out_normal);
}

static bool editor_pick_shear_side_at(slayer3d_game_data_runtime *runtime, float mouse_x, float mouse_y,
                                      slayer3d_bounding_box bounds, slayer3d_vec3 *out_normal)
{
    if (runtime == NULL || out_normal == NULL)
        return false;
    yyjson_val *editor = active_editor_tooling_root(runtime);
    yyjson_val *selection = obj_get(editor, "selection");
    yyjson_val *trace = obj_get(selection, "trace");
    editor_trace_viewport_config viewport;
    if (!editor_trace_select_viewport_at(runtime, trace, mouse_x, mouse_y, &viewport))
        return false;
    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, viewport.camera, &camera))
        return false;

    const slayer3d_vec3 normals[] = {
        slayer3d_vec3_make(1.0f, 0.0f, 0.0f), slayer3d_vec3_make(-1.0f, 0.0f, 0.0f),
        slayer3d_vec3_make(0.0f, 1.0f, 0.0f), slayer3d_vec3_make(0.0f, -1.0f, 0.0f),
        slayer3d_vec3_make(0.0f, 0.0f, 1.0f), slayer3d_vec3_make(0.0f, 0.0f, -1.0f),
    };
    slayer3d_vec3 best_normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    float best_distance = FLT_MAX;
    for (int i = 0; i < (int)SDL_arraysize(normals); ++i)
    {
        float screen_x = 0.0f;
        float screen_y = 0.0f;
        if (!editor_project_world_to_viewport(&camera, &viewport, editor_shear_side_center(bounds, normals[i]),
                                              &screen_x, &screen_y))
            continue;
        const float dx = mouse_x - (viewport.x + screen_x);
        const float dy = mouse_y - (viewport.y + screen_y);
        const float distance = dx * dx + dy * dy;
        if (distance < best_distance)
        {
            best_distance = distance;
            best_normal = normals[i];
        }
    }
    const float side_pick_radius_pixels = 28.0f;
    if (best_distance > side_pick_radius_pixels * side_pick_radius_pixels)
        return false;
    *out_normal = best_normal;
    return true;
}

static slayer3d_vec3 editor_shear_axis_for_side(slayer3d_vec3 side_normal, bool vertical, slayer3d_vec3 camera_right)
{
    const slayer3d_vec3 world_up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    if (vertical && SDL_fabsf(side_normal.y) <= 0.5f)
        return world_up;
    if (SDL_fabsf(side_normal.y) > 0.5f)
    {
        slayer3d_vec3 horizontal_right = slayer3d_vec3_make(camera_right.x, 0.0f, camera_right.z);
        if (slayer3d_vec3_length_squared(horizontal_right) > 0.000001f)
            return slayer3d_vec3_normalize(horizontal_right);
        return slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    }
    slayer3d_vec3 sideways = slayer3d_vec3_cross(side_normal, world_up);
    if (slayer3d_vec3_length_squared(sideways) <= 0.000001f)
        sideways = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    return slayer3d_vec3_normalize(sideways);
}

static slayer3d_vec3 editor_shear_drag_delta(const slayer3d_game_data_runtime *runtime,
                                             const editor_drag_move_state *drag, float mouse_x, float mouse_y)
{
    if (drag == NULL)
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    slayer3d_vec3 camera_forward;
    slayer3d_vec3 camera_right;
    slayer3d_vec3 camera_up;
    bool orthographic = false;
    if (!editor_camera_basis(runtime, &camera_forward, &camera_right, &camera_up, &orthographic))
    {
        camera_right = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        camera_up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    }
    (void)camera_forward;
    (void)orthographic;
    const slayer3d_vec3 axis = editor_shear_axis_for_side(drag->shear_side_normal, drag->shear_vertical, camera_right);
    const float projected_x = slayer3d_vec3_dot(axis, camera_right);
    const float projected_y = -slayer3d_vec3_dot(axis, camera_up);
    const float projected_length_squared = projected_x * projected_x + projected_y * projected_y;
    if (projected_length_squared <= 0.000001f)
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const float inverse_projected_length = 1.0f / SDL_sqrtf(projected_length_squared);
    const float mouse_dx = mouse_x - drag->start_mouse_x;
    const float mouse_dy = mouse_y - drag->start_mouse_y;
    const float pixel_distance = (mouse_dx * projected_x + mouse_dy * projected_y) * inverse_projected_length;
    const float units_per_pixel = SDL_max(drag->grid_size, 0.001f) / 48.0f;
    const float distance = editor_snap_delta(pixel_distance * units_per_pixel, drag->grid_size);
    return slayer3d_vec3_scale(axis, distance);
}

static void publish_editor_shear_drag_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                            bool valid, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const bool active = drag != NULL && drag->active && drag->shear_drag;
    const slayer3d_vec3 side_normal = drag != NULL ? drag->shear_side_normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const slayer3d_vec3 delta = active ? drag->shear_delta : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.ready", runtime->editor_selected_brush_count > 0);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.dragging", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.hovered", drag != NULL && drag->shear_hovered);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.live_preview",
                                 active && slayer3d_vec3_length_squared(delta) > 0.0000001f);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.valid", valid);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.vertical", active && drag->shear_vertical);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.shear.side_normal", side_normal);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.shear.delta", delta);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.shear.axis",
                                 active ? drag->shear_axis : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_string(runtime->scene_state, "editor.shear.side",
                                   drag != NULL && drag->shear_hovered ? editor_shear_side_name(side_normal) : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.shear.message", message != NULL ? message : "");
}

static slayer3d_vec3 editor_scale_drag_factors(const editor_drag_move_state *drag, float mouse_x, float mouse_y)
{
    slayer3d_vec3 factors = slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
    if (drag == NULL)
        return factors;
    const float sx = drag->scale_start_handle.x - drag->scale_anchor.x;
    const float sy = drag->scale_start_handle.y - drag->scale_anchor.y;
    const float sz = drag->scale_start_handle.z - drag->scale_anchor.z;
    const float start_len = SDL_sqrtf(sx * sx + sy * sy + sz * sz);
    if (start_len <= 0.000001f)
        return factors;

    const float mouse_delta = ((mouse_x - drag->start_mouse_x) - (mouse_y - drag->start_mouse_y)) * 0.01f;
    float factor = 1.0f + mouse_delta;
    if (factor < 0.05f)
        factor = 0.05f;
    factor = SDL_roundf(factor * 20.0f) / 20.0f;

    const bool proportional = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 || drag->scale_proportional;
    if (proportional)
    {
        factors = slayer3d_vec3_make(factor, factor, factor);
    }
    else
    {
        if (SDL_fabsf(drag->scale_handle_signs.x) > 0.5f)
            factors.x = factor;
        if (SDL_fabsf(drag->scale_handle_signs.y) > 0.5f)
            factors.y = factor;
        if (SDL_fabsf(drag->scale_handle_signs.z) > 0.5f)
            factors.z = factor;
    }
    return factors;
}

static void publish_editor_scale_drag_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                            bool valid, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const bool active = drag != NULL && drag->active && drag->scale_drag;
    const slayer3d_vec3 factors = active ? drag->scale_factors : slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.ready", runtime->editor_selected_brush_count > 0);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.dragging", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.hovered", drag != NULL && drag->scale_hovered);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.live_preview",
                                 active && (SDL_fabsf(factors.x - 1.0f) > 0.000001f ||
                                            SDL_fabsf(factors.y - 1.0f) > 0.000001f ||
                                            SDL_fabsf(factors.z - 1.0f) > 0.000001f));
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.valid", valid);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.proportional", active && drag->scale_proportional);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.center_anchor",
                                 active && drag->scale_center_anchor);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.scale.anchor",
                                 active ? drag->scale_anchor : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.scale.factors", factors);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.scale.handle_axes",
                                 drag != NULL ? drag->scale_handle_signs : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_string(
        runtime->scene_state, "editor.scale.handle",
        drag != NULL && drag->scale_hovered ? editor_scale_handle_name(drag->scale_handle_signs) : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.scale.message", message != NULL ? message : "");
}

static void publish_editor_rotate_drag_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                             bool valid, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const bool active = drag != NULL && drag->active && drag->rotate_drag;
    const float angle = active ? drag->rotate_angle_radians : 0.0f;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.ready", runtime->editor_selected_brush_count > 0);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.dragging", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.hovered", drag != NULL && drag->rotate_hovered);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.live_preview",
                                 active && SDL_fabsf(drag->rotate_preview_angle_radians) > 0.000001f);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.valid", valid);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.rotate.pivot",
                                 active ? drag->rotate_pivot : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.rotate.axis",
                                 active ? drag->rotate_axis : slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.rotate.hover_axis",
                                 drag != NULL && drag->rotate_hovered ? drag->rotate_hover_axis
                                                                      : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_float(runtime->scene_state, "editor.rotate.angle_radians", angle);
    slayer3d_properties_set_float(runtime->scene_state, "editor.rotate.angle_degrees",
                                  slayer3d_radians_to_degrees(angle));
    slayer3d_properties_set_string(runtime->scene_state, "editor.rotate.message", message != NULL ? message : "");
}

static int editor_dominant_axis_index(slayer3d_vec3 v)
{
    const float abs_x = SDL_fabsf(v.x);
    const float abs_y = SDL_fabsf(v.y);
    const float abs_z = SDL_fabsf(v.z);
    if (abs_x >= abs_y && abs_x >= abs_z)
        return 0;
    if (abs_y >= abs_z)
        return 1;
    return 2;
}

static slayer3d_vec3 editor_dominant_axis_vector(slayer3d_vec3 v)
{
    const int axis = editor_dominant_axis_index(v);
    const float value = axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
    const float sign = value < 0.0f ? -1.0f : 1.0f;
    switch (axis)
    {
    case 0:
        return slayer3d_vec3_make(sign, 0.0f, 0.0f);
    case 1:
        return slayer3d_vec3_make(0.0f, sign, 0.0f);
    default:
        return slayer3d_vec3_make(0.0f, 0.0f, sign);
    }
}

static bool editor_vec3_same_direction(slayer3d_vec3 a, slayer3d_vec3 b)
{
    return SDL_fabsf(a.x - b.x) <= 0.0001f && SDL_fabsf(a.y - b.y) <= 0.0001f && SDL_fabsf(a.z - b.z) <= 0.0001f;
}

bool editor_camera_basis(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 *out_forward,
                         slayer3d_vec3 *out_right, slayer3d_vec3 *out_up, bool *out_orthographic)
{
    if (out_forward != NULL)
        *out_forward = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (out_right != NULL)
        *out_right = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (out_up != NULL)
        *out_up = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (out_orthographic != NULL)
        *out_orthographic = false;
    if (runtime == NULL)
        return false;

    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, slayer3d_game_data_active_camera(runtime), &camera))
        return false;

    const slayer3d_vec3 forward = slayer3d_vec3_normalize(slayer3d_vec3_sub(camera.target, camera.position));
    const slayer3d_vec3 right = slayer3d_vec3_normalize(slayer3d_vec3_cross(forward, camera.up));
    const slayer3d_vec3 up = slayer3d_vec3_normalize(slayer3d_vec3_cross(right, forward));
    if (slayer3d_vec3_length_squared(forward) <= 0.000001f || slayer3d_vec3_length_squared(right) <= 0.000001f ||
        slayer3d_vec3_length_squared(up) <= 0.000001f)
    {
        return false;
    }

    if (out_forward != NULL)
        *out_forward = forward;
    if (out_right != NULL)
        *out_right = right;
    if (out_up != NULL)
        *out_up = up;
    if (out_orthographic != NULL)
        *out_orthographic = camera.projection == SLAYER3D_CAMERA_ORTHOGRAPHIC;
    return true;
}

static slayer3d_vec3 editor_perspective_camera_forward_nudge_axis(slayer3d_vec3 forward, slayer3d_vec3 up)
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
    return editor_dominant_axis_vector(projected);
}

static slayer3d_vec3 editor_perspective_camera_right_nudge_axis(slayer3d_vec3 right, slayer3d_vec3 forward_axis)
{
    slayer3d_vec3 axis = editor_dominant_axis_vector(right);
    if (editor_vec3_same_direction(axis, forward_axis))
    {
        axis = slayer3d_vec3_cross(axis, slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
        if (slayer3d_vec3_length_squared(axis) <= 0.000001f)
            axis = slayer3d_vec3_make(0.0f, 0.0f, 1.0f);
    }
    return editor_dominant_axis_vector(axis);
}

static slayer3d_vec3 editor_camera_relative_brush_nudge_axis(const slayer3d_game_data_runtime *runtime,
                                                             SDL_Scancode scancode)
{
    slayer3d_vec3 forward;
    slayer3d_vec3 right;
    slayer3d_vec3 up;
    bool orthographic = false;
    if (!editor_camera_basis(runtime, &forward, &right, &up, &orthographic))
    {
        if (scancode == SDL_SCANCODE_LEFT)
            return slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
        if (scancode == SDL_SCANCODE_RIGHT)
            return slayer3d_vec3_make(0.0f, 0.0f, 1.0f);
        if (scancode == SDL_SCANCODE_UP)
            return slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        if (scancode == SDL_SCANCODE_DOWN)
            return slayer3d_vec3_make(-1.0f, 0.0f, 0.0f);
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    }

    if (orthographic)
    {
        if (scancode == SDL_SCANCODE_UP)
            return editor_dominant_axis_vector(up);
        if (scancode == SDL_SCANCODE_DOWN)
            return slayer3d_vec3_negate(editor_dominant_axis_vector(up));
        if (scancode == SDL_SCANCODE_RIGHT)
            return editor_dominant_axis_vector(right);
        if (scancode == SDL_SCANCODE_LEFT)
            return slayer3d_vec3_negate(editor_dominant_axis_vector(right));
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    }

    const slayer3d_vec3 forward_axis = editor_perspective_camera_forward_nudge_axis(forward, up);
    const slayer3d_vec3 right_axis = editor_perspective_camera_right_nudge_axis(right, forward_axis);
    if (scancode == SDL_SCANCODE_UP)
        return forward_axis;
    if (scancode == SDL_SCANCODE_DOWN)
        return slayer3d_vec3_negate(forward_axis);
    if (scancode == SDL_SCANCODE_RIGHT)
        return right_axis;
    if (scancode == SDL_SCANCODE_LEFT)
        return slayer3d_vec3_negate(right_axis);
    return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static bool editor_set_face_resize_preview(slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_editor_selection *selection, float distance)
{
    if (runtime == NULL || selection == NULL || !selection->hit || selection->face_index < 0)
        return false;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    const char *brush_identity = editor_metadata_stable_id(selection->element_editor);
    if (brush_identity == NULL || brush_identity[0] == '\0')
        brush_identity = selection->element_name;
    const char *face_identity = editor_metadata_stable_id(selection->face_editor);
    editor_brush_source_vertex_operation_result result;
    char error[256];
    SDL_zeroa(error);
    if (!editor_brush_world_preview_resize_source_face(world_runtime, brush_identity, selection->face_index,
                                                       face_identity, distance, &result, error, sizeof(error)))
    {
        clear_editor_command_preview(runtime);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                       error[0] != '\0' ? error : "brush face resize invalid");
        return false;
    }

    editor_command_preview_state *preview = &runtime->editor_command_preview;
    SDL_zero(*preview);
    preview->active = true;
    preview->scene = slayer3d_game_data_active_scene(runtime);
    preview->command = "resize";
    preview->target = "face";
    preview->world_name = selection->world_name;
    preview->element_name = selection->element_name;
    preview->element_stable_id = editor_metadata_stable_id(selection->element_editor);
    preview->material_name = selection->material_name;
    preview->previous_material_name = selection->material_name;
    preview->face_stable_id = editor_metadata_stable_id(selection->face_editor);
    preview->face_index = selection->face_index;
    preview->material_index = -1;
    preview->previous_material_index = -1;
    preview->offset = slayer3d_vec3_scale(slayer3d_vec3_normalize(selection->normal), distance);
    preview->has_bounds = result.brush.has_bounds;
    preview->bounds =
        result.brush.has_bounds ? result.brush.bounds : (slayer3d_bounding_box){selection->point, selection->point};
    publish_editor_face_resize_feedback(runtime, distance, preview->bounds, preview->has_bounds);
    editor_brush_source_free_runtime_brush(&result.brush);
    return true;
}

static float editor_face_drag_distance(const slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                       float mouse_x, float mouse_y)
{
    if (drag == NULL)
        return 0.0f;

    const float units_per_pixel = SDL_max(drag->grid_size, 0.001f) / 48.0f;
    const slayer3d_vec3 normal = slayer3d_vec3_normalize(drag->face_selection.normal);
    slayer3d_camera3d camera;
    if (runtime != NULL && slayer3d_game_data_get_camera(runtime, slayer3d_game_data_active_camera(runtime), &camera))
    {
        const slayer3d_vec3 forward = slayer3d_vec3_normalize(slayer3d_vec3_sub(camera.target, camera.position));
        const slayer3d_vec3 right = slayer3d_vec3_normalize(slayer3d_vec3_cross(forward, camera.up));
        const slayer3d_vec3 up = slayer3d_vec3_normalize(slayer3d_vec3_cross(right, forward));
        if (slayer3d_vec3_length_squared(normal) > 0.000001f && slayer3d_vec3_length_squared(right) > 0.000001f &&
            slayer3d_vec3_length_squared(up) > 0.000001f)
        {
            const float projected_x = slayer3d_vec3_dot(normal, right);
            const float projected_y = -slayer3d_vec3_dot(normal, up);
            const float projected_length_squared = projected_x * projected_x + projected_y * projected_y;
            if (projected_length_squared > 0.000001f)
            {
                const float inverse_projected_length = 1.0f / SDL_sqrtf(projected_length_squared);
                const float mouse_dx = mouse_x - drag->start_mouse_x;
                const float mouse_dy = mouse_y - drag->start_mouse_y;
                const float pixel_distance =
                    (mouse_dx * projected_x + mouse_dy * projected_y) * inverse_projected_length;
                return editor_snap_delta(pixel_distance * units_per_pixel, drag->grid_size);
            }
        }
    }

    return editor_snap_delta((drag->start_mouse_y - mouse_y) * units_per_pixel, drag->grid_size);
}

static bool editor_handle_face_drag(slayer3d_game_data_runtime *runtime,
                                    const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL ||
        !(editor_mode_is_face(runtime) || editor_mode_is_brush(runtime)))
    {
        editor_set_face_drag_state(runtime, false, false);
        return true;
    }

    const bool brush_face_resize_modifier =
        editor_mode_is_face(runtime) || (editor_mode_is_brush(runtime) && (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
    const bool can_resize_face = brush_face_resize_modifier && editor_selection_is_selectable_brush(hover_selection) &&
                                 hover_selection->face_index >= 0 && hover_selection->has_bounds;
    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
    {
        editor_set_face_drag_state(runtime, can_resize_face, false);
        return true;
    }

    editor_drag_move_state *drag = &runtime->editor_drag_move;
    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);

    if (!drag->active && left_pressed && can_resize_face)
    {
        runtime->editor_active_selection = resolved_editor_selection(runtime, hover_selection);
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
        if (editor_reject_locked_transform_start(runtime, out_consumed))
        {
            editor_set_face_drag_state(runtime, can_resize_face, false);
            return true;
        }

        SDL_zero(*drag);
        drag->active = true;
        drag->face_resize = true;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        drag->grid_size = slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f);
        drag->face_selection = runtime->editor_active_selection;
        (void)slayer3d_input_get_mouse_position(input, &drag->start_mouse_x, &drag->start_mouse_y);
        if (out_consumed != NULL)
            *out_consumed = true;
    }

    if (!drag->active || !drag->face_resize)
    {
        editor_set_face_drag_state(runtime, can_resize_face, false);
        return true;
    }

    if (out_consumed != NULL)
        *out_consumed = true;

    float mouse_x = drag->start_mouse_x;
    float mouse_y = drag->start_mouse_y;
    (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);
    const float distance = editor_face_drag_distance(runtime, drag, mouse_x, mouse_y);
    if (SDL_fabsf(distance) > 0.000001f)
    {
        drag->moved = editor_set_face_resize_preview(runtime, &drag->face_selection, distance);
    }
    editor_set_face_drag_state(runtime, true, true);

    if (left_released || !left_down)
    {
        if (drag->moved)
        {
            (void)slayer3d_game_data_commit_editor_command(runtime, NULL, NULL);
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "brush face resized");
        }
        else
        {
            clear_editor_command_preview(runtime);
        }
        clear_editor_drag_move(runtime);
        editor_set_face_drag_state(runtime, can_resize_face, false);
    }
    return true;
}

static bool editor_emit_texture_paint_preview(slayer3d_game_data_runtime *runtime)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    const int signal_id = slayer3d_game_data_find_signal(runtime, "signal.editor.command.preview");
    if (bus == NULL || signal_id < 0)
        return false;
    slayer3d_signal_emit(bus, signal_id, NULL);
    return true;
}

static bool editor_emit_texture_paint_commit(slayer3d_game_data_runtime *runtime, const char *mode)
{
    const char *signal = "signal.editor.texture.paint.face";
    if (mode != NULL && SDL_strcmp(mode, "brush") == 0)
        signal = "signal.editor.texture.paint.selection";

    slayer3d_signal_bus *bus = runtime_bus(runtime);
    const int signal_id = slayer3d_game_data_find_signal(runtime, signal);
    if (bus == NULL || signal_id < 0)
        return false;
    slayer3d_signal_emit(bus, signal_id, NULL);
    return true;
}

static bool editor_handle_texture_paint(slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_editor_selection *hover_selection,
                                        bool select_requested, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_paint(runtime))
        return true;

    const char *paint_mode = slayer3d_properties_get_string(runtime->scene_state, "editor.texture.paint.mode", "face");
    const bool brush_mode = paint_mode != NULL && SDL_strcmp(paint_mode, "brush") == 0;
    const bool can_paint =
        editor_selection_is_selectable_brush(hover_selection) && (brush_mode || hover_selection->face_index >= 0);

    if (!can_paint)
    {
        clear_editor_command_preview(runtime);
        return true;
    }

    const slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, hover_selection);
    runtime->editor_active_selection = resolved;
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);

    if (!editor_emit_texture_paint_preview(runtime))
        return false;

    if (select_requested)
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        if (brush_mode)
        {
            clear_editor_selected_brushes(runtime);
            if (!add_editor_selected_brush(runtime, &resolved))
                return false;
            update_active_editor_selection_from_selected_brushes(runtime);
        }
        if (!editor_emit_texture_paint_commit(runtime, brush_mode ? "brush" : "face"))
            return false;
        clear_editor_command_preview(runtime);
    }
    return true;
}

static bool editor_handle_rotate_drag(slayer3d_game_data_runtime *runtime,
                                      const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    (void)hover_selection;
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_rotate(runtime))
    {
        if (runtime != NULL && runtime->editor_drag_move.active && runtime->editor_drag_move.rotate_drag)
            clear_editor_drag_move(runtime);
        return true;
    }

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return true;
    (void)editor_update_rotate_hover_state(runtime);

    editor_drag_move_state *drag = &runtime->editor_drag_move;
    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);

    if (!drag->active && left_pressed && editor_selected_brushes_active_for_scene(runtime) &&
        runtime->editor_selected_brush_count > 0)
    {
        if (editor_reject_locked_transform_start(runtime, out_consumed))
            return true;

        slayer3d_bounding_box bounds;
        if (!editor_selected_bounds(runtime, &bounds))
            return true;
        slayer3d_vec3 pivot =
            slayer3d_properties_get_vec3(runtime->scene_state, "editor.rotate.pivot",
                                         slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f));
        pivot = slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
        SDL_zero(*drag);
        drag->active = true;
        drag->rotate_drag = true;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        drag->rotate_pivot = pivot;
        drag->rotate_axis = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
        (void)slayer3d_input_get_mouse_position(input, &drag->start_mouse_x, &drag->start_mouse_y);
        const float radius = editor_rotate_tool_radius(bounds);
        if (!editor_pick_rotate_axis_at(runtime, drag->start_mouse_x, drag->start_mouse_y, pivot, radius,
                                        &drag->rotate_axis, drag))
        {
            clear_editor_drag_move(runtime);
            return true;
        }
        drag->rotate_hovered = true;
        drag->rotate_hover_axis = drag->rotate_axis;
        drag->rotate_preview_valid = true;
        publish_editor_rotate_drag_state(runtime, drag, true, "rotate drag");
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    if (!drag->active || !drag->rotate_drag)
        return true;
    if (out_consumed != NULL)
        *out_consumed = true;

    float mouse_x = drag->start_mouse_x;
    float mouse_y = drag->start_mouse_y;
    (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);
    drag->rotate_angle_radians = editor_rotate_drag_angle(drag, mouse_x, mouse_y);
    drag->rotate_preview_angle_radians = drag->rotate_angle_radians;
    drag->moved = SDL_fabsf(drag->rotate_angle_radians) > 0.000001f;
    publish_editor_rotate_drag_state(runtime, drag, true, drag->moved ? "rotate preview" : "rotate drag");

    if (left_released || !left_down)
    {
        const slayer3d_vec3 pivot = drag->rotate_pivot;
        const slayer3d_vec3 axis = drag->rotate_axis;
        const float angle = drag->rotate_angle_radians;
        const bool moved = drag->moved;
        if (moved)
        {
            if (slayer3d_game_data_rotate_selected_editor_brushes(runtime, pivot, axis, angle))
            {
                update_active_editor_selection_from_selected_brushes(runtime);
                slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.valid", true);
                slayer3d_properties_set_string(runtime->scene_state, "editor.rotate.message", "rotated selection");
                editor_publish_console_message(runtime, "rotated selection");
            }
            else
            {
                slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.valid", false);
                slayer3d_properties_set_string(runtime->scene_state, "editor.rotate.message", "rotation rejected");
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "rotation rejected");
            }
        }
        clear_editor_drag_move(runtime);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.dragging", false);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.rotate.live_preview", false);
        slayer3d_properties_set_float(runtime->scene_state, "editor.rotate.angle_radians", 0.0f);
        slayer3d_properties_set_float(runtime->scene_state, "editor.rotate.angle_degrees", 0.0f);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.rotate.pivot", pivot);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.rotate.axis", axis);
    }

    return true;
}

static bool editor_update_scale_hover_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_scale(runtime) ||
        runtime->editor_drag_move.scale_drag)
    {
        return false;
    }
    slayer3d_vec3 signs = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    bool hovered = false;
    slayer3d_bounding_box bounds;
    slayer3d_input_manager *input = runtime_input(runtime);
    if (input != NULL && editor_selected_bounds(runtime, &bounds))
    {
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        if (slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
            hovered = editor_pick_scale_handle_at(runtime, mouse_x, mouse_y, bounds, &signs);
    }
    slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.hovered", hovered);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.scale.handle_axes", signs);
    slayer3d_properties_set_string(runtime->scene_state, "editor.scale.handle",
                                   hovered ? editor_scale_handle_name(signs) : "");
    return hovered;
}

static bool editor_handle_scale_drag(slayer3d_game_data_runtime *runtime,
                                     const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    (void)hover_selection;
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_scale(runtime))
    {
        if (runtime != NULL && runtime->editor_drag_move.active && runtime->editor_drag_move.scale_drag)
            clear_editor_drag_move(runtime);
        return true;
    }

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return true;
    (void)editor_update_scale_hover_state(runtime);

    editor_drag_move_state *drag = &runtime->editor_drag_move;
    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);

    if (!drag->active && left_pressed && editor_selected_brushes_active_for_scene(runtime) &&
        runtime->editor_selected_brush_count > 0)
    {
        if (editor_reject_locked_transform_start(runtime, out_consumed))
            return true;

        slayer3d_bounding_box bounds;
        if (!editor_selected_bounds(runtime, &bounds))
            return true;
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);
        slayer3d_vec3 signs = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        if (!editor_pick_scale_handle_at(runtime, mouse_x, mouse_y, bounds, &signs))
            return true;

        SDL_zero(*drag);
        drag->active = true;
        drag->scale_drag = true;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        drag->scale_start_bounds = bounds;
        drag->scale_handle_signs = signs;
        drag->scale_center_anchor = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
        drag->scale_proportional = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        drag->scale_anchor = editor_scale_anchor_for_handle(bounds, signs, drag->scale_center_anchor);
        drag->scale_start_handle = editor_scale_bounds_point(bounds, signs);
        drag->scale_factors = slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
        drag->scale_hovered = true;
        drag->scale_preview_valid = true;
        drag->start_mouse_x = mouse_x;
        drag->start_mouse_y = mouse_y;
        publish_editor_scale_drag_state(runtime, drag, true, "scale drag");
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    if (!drag->active || !drag->scale_drag)
        return true;
    if (out_consumed != NULL)
        *out_consumed = true;

    drag->scale_center_anchor = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    drag->scale_proportional = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    drag->scale_anchor =
        editor_scale_anchor_for_handle(drag->scale_start_bounds, drag->scale_handle_signs, drag->scale_center_anchor);
    drag->scale_start_handle = editor_scale_bounds_point(drag->scale_start_bounds, drag->scale_handle_signs);

    float mouse_x = drag->start_mouse_x;
    float mouse_y = drag->start_mouse_y;
    (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);
    drag->scale_factors = editor_scale_drag_factors(drag, mouse_x, mouse_y);
    drag->moved = SDL_fabsf(drag->scale_factors.x - 1.0f) > 0.000001f ||
                  SDL_fabsf(drag->scale_factors.y - 1.0f) > 0.000001f ||
                  SDL_fabsf(drag->scale_factors.z - 1.0f) > 0.000001f;
    publish_editor_scale_drag_state(runtime, drag, true, drag->moved ? "scale preview" : "scale drag");

    if (left_released || !left_down)
    {
        const slayer3d_vec3 anchor = drag->scale_anchor;
        const slayer3d_vec3 factors = drag->scale_factors;
        const bool moved = drag->moved;
        if (moved)
        {
            if (slayer3d_game_data_scale_selected_editor_brushes(runtime, anchor, factors))
            {
                update_active_editor_selection_from_selected_brushes(runtime);
                slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.valid", true);
                slayer3d_properties_set_string(runtime->scene_state, "editor.scale.message", "scaled selection");
                editor_publish_console_message(runtime, "scaled selection");
            }
            else
            {
                slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.valid", false);
                slayer3d_properties_set_string(runtime->scene_state, "editor.scale.message", "scale rejected");
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "scale rejected");
            }
        }
        clear_editor_drag_move(runtime);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.dragging", false);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.scale.live_preview", false);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.scale.anchor", anchor);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.scale.factors", factors);
    }

    return true;
}

static bool editor_update_shear_hover_state(slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_editor_selection *hover_selection)
{
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_shear(runtime) ||
        runtime->editor_drag_move.shear_drag)
    {
        return false;
    }
    slayer3d_vec3 normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    bool hovered = editor_pick_shear_side_from_hover(runtime, hover_selection, &normal);
    slayer3d_bounding_box bounds;
    slayer3d_input_manager *input = runtime_input(runtime);
    if (!hovered && input != NULL && editor_selected_bounds(runtime, &bounds))
    {
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        if (slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
            hovered = editor_pick_shear_side_at(runtime, mouse_x, mouse_y, bounds, &normal);
    }
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.hovered", hovered);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.shear.side_normal", normal);
    slayer3d_properties_set_string(runtime->scene_state, "editor.shear.side",
                                   hovered ? editor_shear_side_name(normal) : "");
    return hovered;
}

static bool editor_handle_shear_drag(slayer3d_game_data_runtime *runtime,
                                     const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    (void)hover_selection;
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_shear(runtime))
    {
        if (runtime != NULL && runtime->editor_drag_move.active && runtime->editor_drag_move.shear_drag)
            clear_editor_drag_move(runtime);
        return true;
    }

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return true;
    (void)editor_update_shear_hover_state(runtime, hover_selection);

    editor_drag_move_state *drag = &runtime->editor_drag_move;
    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);

    if (!drag->active && left_pressed && editor_selected_brushes_active_for_scene(runtime) &&
        runtime->editor_selected_brush_count > 0)
    {
        if (editor_reject_locked_transform_start(runtime, out_consumed))
            return true;

        slayer3d_bounding_box bounds;
        if (!editor_selected_bounds(runtime, &bounds))
            return true;
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);
        slayer3d_vec3 normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        if (!editor_pick_shear_side_from_hover(runtime, hover_selection, &normal) &&
            !editor_pick_shear_side_at(runtime, mouse_x, mouse_y, bounds, &normal))
        {
            return true;
        }

        SDL_zero(*drag);
        drag->active = true;
        drag->shear_drag = true;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        drag->grid_size = slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f);
        drag->shear_start_bounds = bounds;
        drag->shear_side_normal = normal;
        drag->shear_vertical = (SDL_GetModState() & SDL_KMOD_ALT) != 0 && SDL_fabsf(normal.y) <= 0.5f;
        slayer3d_vec3 camera_forward;
        slayer3d_vec3 camera_right;
        slayer3d_vec3 camera_up;
        bool orthographic = false;
        if (!editor_camera_basis(runtime, &camera_forward, &camera_right, &camera_up, &orthographic))
            camera_right = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        (void)camera_forward;
        (void)camera_up;
        (void)orthographic;
        drag->shear_axis = editor_shear_axis_for_side(normal, drag->shear_vertical, camera_right);
        drag->shear_delta = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        drag->shear_hovered = true;
        drag->shear_preview_valid = true;
        drag->start_mouse_x = mouse_x;
        drag->start_mouse_y = mouse_y;
        publish_editor_shear_drag_state(runtime, drag, true, "shear drag");
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    if (!drag->active || !drag->shear_drag)
        return true;
    if (out_consumed != NULL)
        *out_consumed = true;

    drag->shear_vertical = (SDL_GetModState() & SDL_KMOD_ALT) != 0 && SDL_fabsf(drag->shear_side_normal.y) <= 0.5f;
    slayer3d_vec3 camera_forward;
    slayer3d_vec3 camera_right;
    slayer3d_vec3 camera_up;
    bool orthographic = false;
    if (!editor_camera_basis(runtime, &camera_forward, &camera_right, &camera_up, &orthographic))
        camera_right = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    (void)camera_forward;
    (void)camera_up;
    (void)orthographic;
    drag->shear_axis = editor_shear_axis_for_side(drag->shear_side_normal, drag->shear_vertical, camera_right);

    float mouse_x = drag->start_mouse_x;
    float mouse_y = drag->start_mouse_y;
    (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);
    drag->shear_delta = editor_shear_drag_delta(runtime, drag, mouse_x, mouse_y);
    drag->moved = slayer3d_vec3_length_squared(drag->shear_delta) > 0.0000001f;
    publish_editor_shear_drag_state(runtime, drag, true, drag->moved ? "shear preview" : "shear drag");

    if (left_released || !left_down)
    {
        const slayer3d_bounding_box bounds = drag->shear_start_bounds;
        const slayer3d_vec3 side_normal = drag->shear_side_normal;
        const slayer3d_vec3 delta = drag->shear_delta;
        const bool moved = drag->moved;
        if (moved)
        {
            if (slayer3d_game_data_shear_selected_editor_brushes(runtime, bounds, side_normal, delta))
            {
                update_active_editor_selection_from_selected_brushes(runtime);
                slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.valid", true);
                slayer3d_properties_set_string(runtime->scene_state, "editor.shear.message", "sheared selection");
                editor_publish_console_message(runtime, "sheared selection");
            }
            else
            {
                slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.valid", false);
                slayer3d_properties_set_string(runtime->scene_state, "editor.shear.message", "shear rejected");
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "shear rejected");
            }
        }
        clear_editor_drag_move(runtime);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.dragging", false);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.shear.live_preview", false);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.shear.side_normal", side_normal);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.shear.delta", delta);
    }

    return true;
}

static bool editor_handle_drag_move(slayer3d_game_data_runtime *runtime,
                                    const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    const bool movement_mode = editor_mode_is_select(runtime) || editor_mode_is_brush(runtime);
    if (runtime == NULL || runtime->scene_state == NULL || !movement_mode)
    {
        clear_editor_drag_move(runtime);
        return true;
    }

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return true;

    editor_drag_move_state *drag = &runtime->editor_drag_move;
    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    const SDL_Keymod modifiers = SDL_GetModState();
    const bool y_axis_lock = (modifiers & SDL_KMOD_ALT) != 0;
    const bool duplicate_modifier = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    const bool hover_is_brush = editor_selection_is_selectable_brush(hover_selection);
    const bool hover_is_actor = hover_selection != NULL && hover_selection->hit &&
                                hover_selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR;
    const bool active_is_actor = editor_selection_active_for_scene(runtime) && runtime->editor_active_selection.hit &&
                                 runtime->editor_active_selection.type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR;
    const bool can_start_from_hover = hover_is_brush || hover_is_actor;
    const bool can_start_y_axis_drag =
        y_axis_lock &&
        ((editor_selected_brushes_active_for_scene(runtime) && runtime->editor_selected_brush_count > 0) ||
         active_is_actor);
    if (!drag->active && left_pressed && (can_start_from_hover || can_start_y_axis_drag))
    {
        const bool drag_targets_actor = hover_is_actor || (!can_start_from_hover && active_is_actor);
        const bool hover_was_selected = hover_is_brush && editor_hover_is_selected_brush(runtime, hover_selection);
        const bool additive = (modifiers & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        if (hover_is_brush && !hover_was_selected)
        {
            const slayer3d_game_data_editor_selection resolved_hover =
                resolved_editor_selection(runtime, hover_selection);
            if (!additive || duplicate_modifier)
                clear_editor_selected_brushes(runtime);
            if (!add_editor_selected_brush(runtime, &resolved_hover))
                return false;
            update_active_editor_selection_from_selected_brushes(runtime);
        }
        else if (hover_is_actor)
        {
            clear_editor_selected_brushes(runtime);
            runtime->editor_active_selection = resolved_editor_selection(runtime, hover_selection);
            runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
        }
        if (editor_reject_locked_transform_start(runtime, out_consumed))
            return true;

        if (duplicate_modifier && runtime->editor_selected_brush_count > 0 && !active_is_actor)
            (void)slayer3d_game_data_duplicate_selected_editor_brushes(runtime, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                                       false);
        SDL_zero(*drag);
        drag->active = true;
        drag->axis_lock_y = y_axis_lock;
        drag->duplicate_drag = duplicate_modifier;
        drag->target_actor = drag_targets_actor;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        const slayer3d_game_data_editor_selection *drag_selection =
            duplicate_modifier ? &runtime->editor_active_selection
                               : (can_start_from_hover ? hover_selection : &runtime->editor_active_selection);
        const slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, drag_selection);
        drag->start_point = resolved.hit ? resolved.point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        drag->grid_size = slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f);
        (void)slayer3d_input_get_mouse_position(input, &drag->start_mouse_x, &drag->start_mouse_y);
        if (out_consumed != NULL)
            *out_consumed = true;
    }

    if (!drag->active)
        return true;
    if (out_consumed != NULL)
        *out_consumed = true;

    if (drag->axis_lock_y)
    {
        float mouse_x = drag->start_mouse_x;
        float mouse_y = drag->start_mouse_y;
        (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);
        const float units_per_pixel = drag->grid_size / 48.0f;
        const slayer3d_vec3 desired = slayer3d_vec3_make(
            0.0f, editor_snap_delta((drag->start_mouse_y - mouse_y) * units_per_pixel, drag->grid_size), 0.0f);
        const slayer3d_vec3 incremental = slayer3d_vec3_sub(desired, drag->applied_offset);
        if (slayer3d_vec3_length_squared(incremental) > 0.0000001f)
        {
            const bool moved = drag->target_actor
                                   ? slayer3d_game_data_translate_selected_editor_actor(runtime, incremental)
                                   : slayer3d_game_data_translate_selected_editor_brushes(runtime, incremental);
            if (moved)
            {
                drag->applied_offset = desired;
                drag->moved = true;
                if (!drag->target_actor)
                    update_active_editor_selection_from_selected_brushes(runtime);
                publish_editor_drag_move_feedback(runtime, drag->applied_offset);
            }
        }
    }
    else if (hover_selection != NULL && hover_selection->hit)
    {
        const slayer3d_vec3 desired =
            slayer3d_vec3_make(editor_snap_delta(hover_selection->point.x - drag->start_point.x, drag->grid_size), 0.0f,
                               editor_snap_delta(hover_selection->point.z - drag->start_point.z, drag->grid_size));
        const slayer3d_vec3 incremental = slayer3d_vec3_sub(desired, drag->applied_offset);
        if (slayer3d_vec3_length_squared(incremental) > 0.0000001f)
        {
            const bool moved = drag->target_actor
                                   ? slayer3d_game_data_translate_selected_editor_actor(runtime, incremental)
                                   : slayer3d_game_data_translate_selected_editor_brushes(runtime, incremental);
            if (moved)
            {
                drag->applied_offset = desired;
                drag->moved = true;
                if (!drag->target_actor)
                    update_active_editor_selection_from_selected_brushes(runtime);
                publish_editor_drag_move_feedback(runtime, drag->applied_offset);
            }
        }
    }

    if (left_released || !left_down)
    {
        if (runtime->scene_state != NULL && drag->moved)
        {
            const bool moved_actor =
                editor_selection_active_for_scene(runtime) && runtime->editor_active_selection.hit &&
                runtime->editor_active_selection.type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR;
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                           moved_actor ? "drag moved thing" : "drag moved brush");
            editor_publish_console_message(runtime, moved_actor ? "drag moved thing" : "drag moved brush");
        }
        clear_editor_drag_move(runtime);
    }
    return true;
}

static bool editor_handle_grid_nudge(slayer3d_game_data_runtime *runtime, bool *out_changed)
{
    if (out_changed != NULL)
        *out_changed = false;
    if (runtime == NULL || runtime->scene_state == NULL)
        return true;
    const bool vertex_mode = editor_mode_is_vertex(runtime);
    const bool edge_mode = editor_mode_is_edge(runtime);
    const bool source_handle_mode = vertex_mode || edge_mode;
    const bool brush_transform_mode = editor_mode_is_select(runtime) || editor_mode_is_brush(runtime) ||
                                      editor_mode_is_rotate(runtime) || editor_mode_is_scale(runtime) ||
                                      editor_mode_is_shear(runtime);
    if (!brush_transform_mode && !source_handle_mode)
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    const float grid_size = slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 16.0f);
    if (input == NULL || grid_size <= 0.0f)
        return true;

    const SDL_Keymod modifiers = SDL_GetModState();
    const bool transform_modifier = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0;
    const bool duplicate_modifier = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    if (!source_handle_mode && duplicate_modifier && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_D))
    {
        (void)slayer3d_game_data_duplicate_selected_editor_brushes(runtime, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), true);
        if (out_changed != NULL)
            *out_changed = true;
        return true;
    }
    if (!source_handle_mode && (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_HOME) ||
                                (transform_modifier && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_LEFT))))
    {
        (void)slayer3d_game_data_rotate_selected_editor_brushes_y(runtime, 1);
        if (out_changed != NULL)
            *out_changed = true;
        return true;
    }
    if (!source_handle_mode && (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_END) ||
                                (transform_modifier && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_RIGHT))))
    {
        (void)slayer3d_game_data_rotate_selected_editor_brushes_y(runtime, -1);
        if (out_changed != NULL)
            *out_changed = true;
        return true;
    }

    slayer3d_vec3 offset = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_PAGEUP) ||
        (transform_modifier && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_UP)))
    {
        offset.y = grid_size;
    }
    else if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_PAGEDOWN) ||
             (transform_modifier && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_DOWN)))
    {
        offset.y = -grid_size;
    }
    else if (!source_handle_mode && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_LEFT) && !transform_modifier)
    {
        offset = slayer3d_vec3_scale(editor_camera_relative_brush_nudge_axis(runtime, SDL_SCANCODE_LEFT), grid_size);
    }
    else if (!source_handle_mode && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_RIGHT) &&
             !transform_modifier)
    {
        offset = slayer3d_vec3_scale(editor_camera_relative_brush_nudge_axis(runtime, SDL_SCANCODE_RIGHT), grid_size);
    }
    else if (!source_handle_mode && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_UP) && !transform_modifier)
    {
        offset = slayer3d_vec3_scale(editor_camera_relative_brush_nudge_axis(runtime, SDL_SCANCODE_UP), grid_size);
    }
    else if (!source_handle_mode && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_DOWN) && !transform_modifier)
    {
        offset = slayer3d_vec3_scale(editor_camera_relative_brush_nudge_axis(runtime, SDL_SCANCODE_DOWN), grid_size);
    }
    else if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_LEFT) && !transform_modifier)
    {
        offset.z = -grid_size;
    }
    else if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_RIGHT) && !transform_modifier)
    {
        offset.z = grid_size;
    }
    else if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_UP) && !transform_modifier)
    {
        offset.x = grid_size;
    }
    else if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_DOWN) && !transform_modifier)
    {
        offset.x = -grid_size;
    }

    if (slayer3d_vec3_length_squared(offset) <= 0.0000001f)
        return true;
    bool moved = false;
    if (vertex_mode)
        moved = editor_translate_selected_vertices(runtime, offset);
    else if (edge_mode)
        moved = editor_translate_selected_edges(runtime, offset);
    else if (editor_selection_active_for_scene(runtime) && runtime->editor_active_selection.hit &&
             runtime->editor_active_selection.type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR)
    {
        moved = slayer3d_game_data_translate_selected_editor_actor(runtime, offset);
    }
    else
        moved = slayer3d_game_data_translate_selected_editor_brushes(runtime, offset);
    if (out_changed != NULL)
        *out_changed = moved;
    return true;
}

static const char *editor_metadata_stable_id(const slayer3d_game_data_editor_metadata *metadata)
{
    return metadata != NULL && metadata->stable_id != NULL ? metadata->stable_id : "";
}

static int editor_box_face_key_index(const char *face_key)
{
    static const char *const face_keys[] = {"px", "nx", "py", "ny", "pz", "nz"};
    for (size_t i = 0; face_key != NULL && i < SDL_arraysize(face_keys); ++i)
    {
        if (SDL_strcmp(face_key, face_keys[i]) == 0)
            return (int)i;
    }
    return -1;
}

static slayer3d_vec3 editor_selection_face_center(const slayer3d_game_data_brush *brush, int face_index)
{
    if (brush == NULL || !brush->has_bounds)
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);

    slayer3d_vec3 center = slayer3d_vec3_scale(slayer3d_vec3_add(brush->bounds.min, brush->bounds.max), 0.5f);
    if (face_index == 0)
        center.x = brush->bounds.max.x;
    else if (face_index == 1)
        center.x = brush->bounds.min.x;
    else if (face_index == 2)
        center.y = brush->bounds.max.y;
    else if (face_index == 3)
        center.y = brush->bounds.min.y;
    else if (face_index == 4)
        center.z = brush->bounds.max.z;
    else if (face_index == 5)
        center.z = brush->bounds.min.z;
    return center;
}

static void resolve_brush_editor_selection_metadata(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || selection == NULL || !editor_selection_is_selectable_brush(selection))
        return;

    selection->world_editor = NULL;
    selection->element_editor = NULL;
    selection->face_editor = NULL;
    selection->material_editor = NULL;
    selection->compiled_face = NULL;
    selection->compiled_face_index = -1;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    if (world_runtime == NULL)
        return;

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    selection->world_editor = &world->editor;
    selection->element_index = -1;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (brush->name == NULL || SDL_strcmp(brush->name, selection->element_name) != 0)
            continue;

        selection->element_index = brush_index;
        selection->element_editor = &brush->editor;
        selection->has_bounds = brush->has_bounds;
        if (brush->has_bounds)
            selection->bounds = brush->bounds;

        if (selection->face_index >= 0 && selection->face_index < brush->face_count)
        {
            const slayer3d_game_data_brush_face *face = &brush->faces[selection->face_index];
            selection->face_editor = &face->editor;
            selection->material_name = face->material_name;
            if (face->material_index >= 0 && face->material_index < world->material_count)
                selection->material_editor = &world->materials[face->material_index].editor;
            for (int i = 0; i < world->compile_rendered_face_metadata_count; ++i)
            {
                const slayer3d_game_data_brush_compiled_face *compiled_face = &world->compile_rendered_faces[i];
                if (compiled_face->brush_index == brush_index && compiled_face->face_index == selection->face_index)
                {
                    selection->compiled_face = compiled_face;
                    selection->compiled_face_index = i;
                    break;
                }
            }
        }
        return;
    }
}

slayer3d_game_data_editor_selection resolved_editor_selection(const slayer3d_game_data_runtime *runtime,
                                                              const slayer3d_game_data_editor_selection *selection)
{
    slayer3d_game_data_editor_selection resolved;
    init_editor_selection(&resolved);
    if (selection == NULL)
        return resolved;
    resolved = *selection;
    resolve_brush_editor_selection_metadata(runtime, &resolved);
    return resolved;
}

static bool editor_metadata_matches_stable_id(const slayer3d_game_data_editor_metadata *metadata, const char *stable_id)
{
    return stable_id != NULL && stable_id[0] != '\0' && metadata != NULL && metadata->stable_id != NULL &&
           SDL_strcmp(metadata->stable_id, stable_id) == 0;
}

static int editor_face_index_from_key_or_stable_id(const slayer3d_game_data_brush *brush, const char *face_key,
                                                   const char *face_stable_id)
{
    if (brush == NULL)
        return -1;
    if (face_stable_id != NULL && face_stable_id[0] != '\0')
    {
        for (int i = 0; i < brush->face_count; ++i)
        {
            if (editor_metadata_matches_stable_id(&brush->faces[i].editor, face_stable_id))
                return i;
        }
        return -1;
    }
    return editor_box_face_key_index(face_key);
}

static bool editor_brush_matches_name_or_stable_id(const slayer3d_game_data_brush *brush, const char *brush_name,
                                                   const char *brush_stable_id)
{
    if (brush == NULL)
        return false;
    if (brush_stable_id != NULL && brush_stable_id[0] != '\0')
        return editor_metadata_matches_stable_id(&brush->editor, brush_stable_id);
    return brush_name != NULL && brush_name[0] != '\0' && brush->name != NULL &&
           SDL_strcmp(brush->name, brush_name) == 0;
}

static bool editor_select_brush_by_identity(slayer3d_game_data_runtime *runtime, const char *world_name,
                                            const char *brush_name, const char *brush_stable_id, const char *face_key,
                                            const char *face_stable_id,
                                            slayer3d_game_data_editor_selection *out_selection)
{
    init_editor_selection(out_selection);
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || out_selection == NULL ||
        ((brush_name == NULL || brush_name[0] == '\0') && (brush_stable_id == NULL || brush_stable_id[0] == '\0')))
    {
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
        return false;

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (!editor_brush_matches_name_or_stable_id(brush, brush_name, brush_stable_id))
            continue;

        const int face_index = editor_face_index_from_key_or_stable_id(brush, face_key, face_stable_id);
        const bool requested_face =
            (face_key != NULL && face_key[0] != '\0') || (face_stable_id != NULL && face_stable_id[0] != '\0');
        if (requested_face && face_index < 0)
            return false;
        const slayer3d_game_data_brush_face *face =
            face_index >= 0 && face_index < brush->face_count ? &brush->faces[face_index] : NULL;

        out_selection->hit = true;
        out_selection->type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
        out_selection->world_name = world->name;
        out_selection->world_position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        out_selection->element_name = brush->name;
        out_selection->material_name = face != NULL ? face->material_name : NULL;
        out_selection->element_index = brush_index;
        out_selection->face_index = face != NULL ? face_index : -1;
        out_selection->fraction = 0.0f;
        out_selection->point = editor_selection_face_center(brush, face_index);
        out_selection->normal = face != NULL ? slayer3d_vec3_normalize(face->normal) : slayer3d_vec3_make(0, 1, 0);
        out_selection->has_bounds = brush->has_bounds;
        if (brush->has_bounds)
            out_selection->bounds = brush->bounds;
        resolve_brush_editor_selection_metadata(runtime, out_selection);
        return true;
    }
    return false;
}

bool slayer3d_game_data_select_editor_brush_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    const char *brush_name = json_string(action, "element", NULL);
    const char *brush_name_key = json_string(action, "element_from_state", NULL);
    const char *brush_stable_id = json_string(action, "element_stable_id", NULL);
    const char *brush_stable_id_key = json_string(action, "element_stable_id_from_state", NULL);
    const char *face_key = json_string(action, "face", NULL);
    const char *face_from_state = json_string(action, "face_from_state", NULL);
    const char *face_stable_id = json_string(action, "face_stable_id", NULL);
    const char *face_stable_id_from_state = json_string(action, "face_stable_id_from_state", NULL);
    const bool additive = json_bool(action, "additive", false);
    if (runtime != NULL && runtime->scene_state != NULL)
    {
        if ((brush_name == NULL || brush_name[0] == '\0') && brush_name_key != NULL && brush_name_key[0] != '\0')
            brush_name = slayer3d_properties_get_string(runtime->scene_state, brush_name_key, "");
        if ((brush_stable_id == NULL || brush_stable_id[0] == '\0') && brush_stable_id_key != NULL &&
            brush_stable_id_key[0] != '\0')
        {
            brush_stable_id = slayer3d_properties_get_string(runtime->scene_state, brush_stable_id_key, "");
        }
        if ((face_key == NULL || face_key[0] == '\0') && face_from_state != NULL && face_from_state[0] != '\0')
            face_key = slayer3d_properties_get_string(runtime->scene_state, face_from_state, "");
        if ((face_stable_id == NULL || face_stable_id[0] == '\0') && face_stable_id_from_state != NULL &&
            face_stable_id_from_state[0] != '\0')
        {
            face_stable_id = slayer3d_properties_get_string(runtime->scene_state, face_stable_id_from_state, "");
        }
    }

    slayer3d_game_data_editor_selection selection;
    const bool ok = editor_select_brush_by_identity(runtime, world_name, brush_name, brush_stable_id, face_key,
                                                    face_stable_id, &selection);
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "brush selected")
                                : json_string(action, "invalid_message", "brush selection failed"));
    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    if (ok)
    {
        if (!additive)
            clear_editor_selected_brushes(runtime);
        else if (!editor_selected_brushes_active_for_scene(runtime))
            clear_editor_selected_brushes(runtime);
        (void)add_editor_selected_brush(runtime, &selection);
        update_active_editor_selection_from_selected_brushes(runtime);
        publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
    }
    else
    {
        clear_editor_active_selection(runtime);
        publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
    }
    return true;
}

bool slayer3d_game_data_update_active_editor_tooling(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    yyjson_val *editor = active_editor_tooling_root(runtime);
    yyjson_val *selection_json = obj_get(editor, "selection");
    yyjson_val *outputs = obj_get(selection_json, "outputs");
    if (!yyjson_is_obj(selection_json))
    {
        clear_editor_active_selection(runtime);
        return true;
    }
    if (!editor_selection_active_for_scene(runtime))
        clear_editor_active_selection(runtime);

    slayer3d_game_data_world_trace_desc trace;
    slayer3d_game_data_editor_selection hover_selection;
    init_editor_selection(&hover_selection);
    if (!editor_trace_desc_from_json(runtime, selection_json, &trace) ||
        !editor_pick_selection_from_json(runtime, selection_json, &trace, &hover_selection))
    {
        clear_editor_vertex_hover_state(runtime);
        clear_editor_edge_hover_state(runtime);
        return false;
    }

    if (!editor_selection_mode_is_click(selection_json))
    {
        runtime->editor_active_selection = hover_selection;
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
        update_editor_placement_preview(runtime, editor, &hover_selection);
        publish_editor_selection(runtime, outputs, &hover_selection);
        return true;
    }

    publish_editor_selection(runtime, obj_get(selection_json, "hover_outputs"), &hover_selection);
    publish_editor_vertex_hover_state(runtime, &hover_selection);
    publish_editor_edge_hover_state(runtime, &hover_selection);
    bool ui_consumed = false;
    if (!editor_handle_shape_widget(runtime, editor, &ui_consumed))
        return false;
    if (ui_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    if (!editor_handle_grid_widget(runtime, editor, &ui_consumed))
        return false;
    if (ui_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    if (!editor_handle_tool_mode_buttons(runtime, editor, &ui_consumed))
        return false;
    if (ui_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    bool clip_tool_consumed = false;
    if (!editor_handle_clip_tool_input(runtime, selection_json, &hover_selection, &clip_tool_consumed))
        return false;
    if (clip_tool_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    const bool select_requested = editor_selection_button_requested(runtime, selection_json, "select_button", "LEFT");
    const bool secondary_select_requested =
        editor_selection_button_requested(runtime, selection_json, "secondary_select_button", NULL);
    bool texture_paint_consumed = false;
    if (!editor_handle_texture_paint(runtime, &hover_selection, select_requested, &texture_paint_consumed))
        return false;
    if (texture_paint_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    bool vertex_lasso_consumed = false;
    if (!editor_handle_vertex_lasso(runtime, selection_json, &hover_selection, &vertex_lasso_consumed))
        return false;
    if (vertex_lasso_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        publish_editor_selected_vertex_count(runtime);
        return true;
    }
    bool vertex_drag_consumed = false;
    if (!editor_handle_vertex_drag(runtime, &hover_selection, &vertex_drag_consumed))
        return false;
    if (vertex_drag_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        publish_editor_selected_vertex_count(runtime);
        return true;
    }
    bool vertex_add_consumed = false;
    if (!editor_handle_vertex_add_to_source(runtime, &hover_selection, select_requested, &vertex_add_consumed))
        return false;
    if (vertex_add_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        publish_editor_selected_vertex_count(runtime);
        return true;
    }
    bool vertex_selection_consumed = false;
    if (!editor_handle_vertex_selection(runtime, &hover_selection, select_requested, &vertex_selection_consumed))
        return false;
    if (vertex_selection_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        publish_editor_selected_vertex_count(runtime);
        return true;
    }
    bool edge_drag_consumed = false;
    if (!editor_handle_edge_drag(runtime, &hover_selection, &edge_drag_consumed))
        return false;
    if (edge_drag_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        publish_editor_selected_edge_count(runtime);
        return true;
    }
    bool edge_lasso_consumed = false;
    if (!editor_handle_edge_lasso(runtime, selection_json, &hover_selection, &edge_lasso_consumed))
        return false;
    if (edge_lasso_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        publish_editor_selected_edge_count(runtime);
        return true;
    }
    bool edge_selection_consumed = false;
    if (!editor_handle_edge_selection(runtime, &hover_selection, select_requested, &edge_selection_consumed))
        return false;
    if (edge_selection_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        publish_editor_selected_edge_count(runtime);
        return true;
    }
    bool rotate_drag_consumed = false;
    if (!editor_handle_rotate_drag(runtime, &hover_selection, &rotate_drag_consumed))
        return false;
    if (rotate_drag_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    bool scale_drag_consumed = false;
    if (!editor_handle_scale_drag(runtime, &hover_selection, &scale_drag_consumed))
        return false;
    if (scale_drag_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    bool shear_drag_consumed = false;
    if (!editor_handle_shear_drag(runtime, &hover_selection, &shear_drag_consumed))
        return false;
    if (shear_drag_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    bool face_drag_consumed = false;
    if (!editor_handle_face_drag(runtime, &hover_selection, &face_drag_consumed))
        return false;
    if (face_drag_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    bool drag_move_consumed = false;
    if (runtime->editor_drag_move.active)
    {
        if (!editor_handle_drag_move(runtime, &hover_selection, &drag_move_consumed))
            return false;
        if (drag_move_consumed)
        {
            publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
            publish_editor_selected_brush_count(runtime);
            return true;
        }
    }
    bool drag_consumed = false;
    if (!update_editor_drag_create(runtime, editor, &hover_selection, &drag_consumed))
        return false;
    if (!drag_consumed)
    {
        if (!editor_handle_drag_move(runtime, &hover_selection, &drag_move_consumed))
            return false;
        if (drag_move_consumed)
        {
            publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
            publish_editor_selected_brush_count(runtime);
            return true;
        }
        update_editor_placement_preview(runtime, editor, &hover_selection);
    }
    bool grid_nudge_changed = false;
    if (!editor_handle_grid_nudge(runtime, &grid_nudge_changed))
        return false;
    if (grid_nudge_changed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        publish_editor_selected_vertex_count(runtime);
        publish_editor_selected_edge_count(runtime);
        return true;
    }
    if (drag_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    if (editor_mode_is_select(runtime))
    {
        if (select_requested)
        {
            if (hover_selection.hit)
            {
                if (!editor_select_mode_primary_click(runtime, &hover_selection))
                    return false;
            }
            else if (json_bool(selection_json, "clear_on_miss", true))
            {
                clear_editor_active_selection(runtime);
            }
        }
        if (secondary_select_requested && hover_selection.hit)
        {
            if (!editor_select_mode_secondary_click(runtime, &hover_selection))
                return false;
        }
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        if (secondary_select_requested && !emit_editor_selection_signal(runtime, selection_json, "on_secondary_select",
                                                                        &runtime->editor_active_selection))
        {
            return false;
        }
        return true;
    }

    if ((editor_mode_is_brush(runtime) || editor_mode_is_face(runtime) || editor_mode_is_edge(runtime) ||
         editor_mode_is_vertex(runtime) || editor_mode_is_rotate(runtime) || editor_mode_is_scale(runtime) ||
         editor_mode_is_shear(runtime)) &&
        hover_selection.hit)
    {
        runtime->editor_active_selection = hover_selection;
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    }
    publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
    publish_editor_selected_brush_count(runtime);
    if (secondary_select_requested &&
        !emit_editor_selection_signal(runtime, selection_json, "on_secondary_select", &hover_selection))
    {
        return false;
    }
    if (select_requested && !emit_editor_selection_signal(runtime, selection_json, "on_select", &hover_selection))
    {
        return false;
    }
    return true;
}

bool slayer3d_game_data_get_active_editor_selection(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_editor_selection *out_selection)
{
    if (out_selection != NULL)
        init_editor_selection(out_selection);
    if (runtime == NULL || out_selection == NULL || !editor_selection_active_for_scene(runtime) ||
        !runtime->editor_active_selection.hit)
    {
        return false;
    }
    *out_selection = resolved_editor_selection(runtime, &runtime->editor_active_selection);
    return true;
}

bool slayer3d_game_data_clear_active_editor_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    clear_editor_active_selection(runtime);
    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
    return true;
}

slayer3d_properties *slayer3d_game_data_create_editor_selection_payload(
    const slayer3d_game_data_editor_selection *selection)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    const bool hit = selection != NULL && selection->hit;
    slayer3d_properties_set_bool(payload, "selection_hit", hit);
    slayer3d_properties_set_string(payload, "selection_type",
                                   hit ? editor_selection_type_name(selection->type) : "none");
    slayer3d_properties_set_string(payload, "selection_world",
                                   hit && selection->world_name != NULL ? selection->world_name : "");
    slayer3d_properties_set_string(payload, "selection_element",
                                   hit && selection->element_name != NULL ? selection->element_name : "");
    slayer3d_properties_set_string(payload, "selection_material",
                                   hit && selection->material_name != NULL ? selection->material_name : "");
    slayer3d_properties_set_string(payload, "selection_world_stable_id",
                                   hit ? editor_metadata_stable_id(selection->world_editor) : "");
    slayer3d_properties_set_string(payload, "selection_element_stable_id",
                                   hit ? editor_metadata_stable_id(selection->element_editor) : "");
    slayer3d_properties_set_string(payload, "selection_material_stable_id",
                                   hit ? editor_metadata_stable_id(selection->material_editor) : "");
    slayer3d_properties_set_string(payload, "selection_face_stable_id",
                                   hit ? editor_metadata_stable_id(selection->face_editor) : "");
    slayer3d_properties_set_int(payload, "selection_element_index", hit ? selection->element_index : -1);
    slayer3d_properties_set_int(payload, "selection_face_index", hit ? selection->face_index : -1);
    slayer3d_properties_set_bool(payload, "selection_face_rendered", hit && selection->compiled_face != NULL);
    slayer3d_properties_set_int(payload, "selection_compiled_face_index", hit ? selection->compiled_face_index : -1);
    slayer3d_properties_set_int(payload, "selection_compiled_mesh_index",
                                hit && selection->compiled_face != NULL ? selection->compiled_face->mesh_index : -1);
    slayer3d_properties_set_int(payload, "selection_compiled_first_vertex",
                                hit && selection->compiled_face != NULL ? selection->compiled_face->first_vertex : -1);
    slayer3d_properties_set_int(payload, "selection_compiled_vertex_count",
                                hit && selection->compiled_face != NULL ? selection->compiled_face->vertex_count : 0);
    slayer3d_properties_set_int(payload, "selection_compiled_triangle_count",
                                hit && selection->compiled_face != NULL ? selection->compiled_face->triangle_count : 0);
    slayer3d_properties_set_float(payload, "selection_fraction", hit ? selection->fraction : 1.0f);
    slayer3d_properties_set_vec3(payload, "selection_world_position",
                                 hit ? selection->world_position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(payload, "selection_point",
                                 hit ? selection->point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(payload, "selection_normal",
                                 hit ? selection->normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return payload;
}

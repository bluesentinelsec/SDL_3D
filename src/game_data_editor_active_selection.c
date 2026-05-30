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

static void editor_set_face_drag_state(slayer3d_game_data_runtime *runtime, bool ready, bool dragging)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.face.drag.ready", ready);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.face.drag.active", dragging);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.face.drag.shift",
                                 (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
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
    editor_brush_source_free_runtime_brush(&result.brush);
    return true;
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
        SDL_zero(*drag);
        drag->active = true;
        drag->face_resize = true;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        drag->grid_size = slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f);
        drag->face_selection = resolved_editor_selection(runtime, hover_selection);
        (void)slayer3d_input_get_mouse_position(input, &drag->start_mouse_x, &drag->start_mouse_y);
        runtime->editor_active_selection = drag->face_selection;
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
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
    const float units_per_pixel = SDL_max(drag->grid_size, 0.001f) / 48.0f;
    const float distance = editor_snap_delta((mouse_y - drag->start_mouse_y) * units_per_pixel, drag->grid_size);
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
    const bool can_start_from_hover = editor_selection_is_selectable_brush(hover_selection);
    const bool can_start_y_axis_drag =
        y_axis_lock && editor_selected_brushes_active_for_scene(runtime) && runtime->editor_selected_brush_count > 0;
    if (!drag->active && left_pressed && (can_start_from_hover || can_start_y_axis_drag))
    {
        const bool hover_was_selected =
            can_start_from_hover && editor_hover_is_selected_brush(runtime, hover_selection);
        const bool additive = (modifiers & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        if (can_start_from_hover && !hover_was_selected)
        {
            const slayer3d_game_data_editor_selection resolved_hover =
                resolved_editor_selection(runtime, hover_selection);
            if (!additive || duplicate_modifier)
                clear_editor_selected_brushes(runtime);
            if (!add_editor_selected_brush(runtime, &resolved_hover))
                return false;
            update_active_editor_selection_from_selected_brushes(runtime);
        }
        if (duplicate_modifier && runtime->editor_selected_brush_count > 0)
            (void)slayer3d_game_data_duplicate_selected_editor_brushes(runtime, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                                       false);
        SDL_zero(*drag);
        drag->active = true;
        drag->axis_lock_y = y_axis_lock;
        drag->duplicate_drag = duplicate_modifier;
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
            if (slayer3d_game_data_translate_selected_editor_brushes(runtime, incremental))
            {
                drag->applied_offset = desired;
                drag->moved = true;
                update_active_editor_selection_from_selected_brushes(runtime);
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
            if (slayer3d_game_data_translate_selected_editor_brushes(runtime, incremental))
            {
                drag->applied_offset = desired;
                drag->moved = true;
                update_active_editor_selection_from_selected_brushes(runtime);
            }
        }
    }

    if (left_released || !left_down)
    {
        if (runtime->scene_state != NULL && drag->moved)
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "drag moved brush");
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
    const bool brush_transform_mode = editor_mode_is_select(runtime) || editor_mode_is_brush(runtime);
    if (!brush_transform_mode && !vertex_mode)
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    const float grid_size = slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 16.0f);
    if (input == NULL || grid_size <= 0.0f)
        return true;

    const SDL_Keymod modifiers = SDL_GetModState();
    const bool transform_modifier = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0;
    const bool duplicate_modifier = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    if (!vertex_mode && duplicate_modifier && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_D))
    {
        (void)slayer3d_game_data_duplicate_selected_editor_brushes(runtime, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), true);
        if (out_changed != NULL)
            *out_changed = true;
        return true;
    }
    if (!vertex_mode && (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_HOME) ||
                         (transform_modifier && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_LEFT))))
    {
        (void)slayer3d_game_data_rotate_selected_editor_brushes_y(runtime, 1);
        if (out_changed != NULL)
            *out_changed = true;
        return true;
    }
    if (!vertex_mode && (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_END) ||
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
    if (vertex_mode)
        (void)editor_translate_selected_vertices(runtime, offset);
    else
        (void)slayer3d_game_data_translate_selected_editor_brushes(runtime, offset);
    if (out_changed != NULL)
        *out_changed = true;
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
    bool ui_consumed = false;
    if (!editor_handle_grid_widget(runtime, editor, &ui_consumed))
        return false;
    if (ui_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    if (!editor_handle_prefabs_widget(runtime, editor, &ui_consumed))
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
    const bool select_requested = editor_selection_button_requested(runtime, selection_json, "select_button", "LEFT");
    const bool secondary_select_requested =
        editor_selection_button_requested(runtime, selection_json, "secondary_select_button", NULL);
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
    if (!editor_handle_drag_move(runtime, &hover_selection, &drag_move_consumed))
        return false;
    if (drag_move_consumed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        return true;
    }
    bool drag_consumed = false;
    if (!update_editor_drag_create(runtime, editor, &hover_selection, &drag_consumed))
        return false;
    if (!drag_consumed)
        update_editor_placement_preview(runtime, editor, &hover_selection);
    bool grid_nudge_changed = false;
    if (!editor_handle_grid_nudge(runtime, &grid_nudge_changed))
        return false;
    if (grid_nudge_changed)
    {
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        publish_editor_selected_vertex_count(runtime);
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

    if ((editor_mode_is_brush(runtime) || editor_mode_is_face(runtime) || editor_mode_is_vertex(runtime)) &&
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

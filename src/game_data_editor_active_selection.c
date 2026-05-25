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

static bool editor_selected_brushes_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selected_brush_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) == 0;
}

static void init_editor_source_vertex_selection(editor_source_vertex_selection *selection)
{
    if (selection == NULL)
        return;
    SDL_zero(*selection);
    selection->source_index = -1;
    selection->vertex_index = -1;
}

static bool editor_selected_vertices_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selected_vertex_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selected_vertex_scene, active_scene) == 0;
}

static void publish_editor_selected_vertex_count(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const int count = editor_selected_vertices_active_for_scene(runtime) ? runtime->editor_selected_vertex_count : 0;
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.count", count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.selection.multiple", count > 1);
    if (count <= 0)
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.world", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush_stable_id", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.vertex", "");
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.index", -1);
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.x", 0);
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.y", 0);
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.z", 0);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.selection.coord",
                                     slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        return;
    }

    const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[count - 1];
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.world", selection->world_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush", selection->brush_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush_stable_id",
                                   selection->brush_stable_id);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.vertex", selection->vertex_stable_id);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.index", selection->vertex_index);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.x", selection->coord[0]);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.y", selection->coord[1]);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.z", selection->coord[2]);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.selection.coord",
                                 slayer3d_vec3_make((float)selection->coord[0] * meters_per_unit,
                                                    (float)selection->coord[1] * meters_per_unit,
                                                    (float)selection->coord[2] * meters_per_unit));
}

static void clear_editor_selected_vertices(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY; ++i)
        init_editor_source_vertex_selection(&runtime->editor_selected_vertices[i]);
    runtime->editor_selected_vertex_count = 0;
    runtime->editor_selected_vertex_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_vertex_count(runtime);
}

static void publish_editor_vertex_lasso_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                              int selected_count)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const bool active = drag != NULL && drag->active && drag->vertex_lasso;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.lasso.active", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.lasso.additive",
                                 active ? drag->lasso_additive : false);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.start_x",
                                  active ? drag->start_mouse_x : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.start_y",
                                  active ? drag->start_mouse_y : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.end_x",
                                  active ? drag->current_mouse_x : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.end_y",
                                  active ? drag->current_mouse_y : 0.0f);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.lasso.selected_count", selected_count);
}

static void publish_editor_selected_brush_count(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const int count = editor_selected_brushes_active_for_scene(runtime) ? runtime->editor_selected_brush_count : 0;
    slayer3d_properties_set_int(runtime->scene_state, "editor.selection.count", count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.selection.multiple", count > 1);
}

static void clear_editor_selected_brushes(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    clear_editor_selected_vertices(runtime);
    for (int i = 0; i < SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY; ++i)
        init_editor_selection(&runtime->editor_selected_brushes[i]);
    runtime->editor_selected_brush_count = 0;
    runtime->editor_selected_brush_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_brush_count(runtime);
}

static void clear_editor_active_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    init_editor_selection(&runtime->editor_active_selection);
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    clear_editor_selected_brushes(runtime);
    clear_editor_command_preview(runtime);
    clear_editor_placement_preview(runtime);
}

static bool editor_selection_button_requested(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                              const char *key, const char *fallback)
{
    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return false;

    const Uint8 button = mouse_button_from_json(json_string(selection, key, fallback));
    return button != 0 && slayer3d_input_get_pressed_mouse_button(input) == button;
}

static bool editor_mouse_in_rect(float mouse_x, float mouse_y, yyjson_val *rect)
{
    if (!yyjson_is_obj(rect))
        return false;
    const float x = json_float(rect, "x", 0.0f);
    const float y = json_float(rect, "y", 0.0f);
    const float w = json_float(rect, "w", 0.0f);
    const float h = json_float(rect, "h", 0.0f);
    return w > 0.0f && h > 0.0f && mouse_x >= x && mouse_y >= y && mouse_x < x + w && mouse_y < y + h;
}

static void editor_set_grid_value(slayer3d_game_data_runtime *runtime, yyjson_val *widget, float value)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const char *grid_key = json_string(widget, "grid_key", "editor.grid.size");
    const char *brush_grid_key = json_string(widget, "brush_grid_key", "editor.brush.grid_size");
    slayer3d_properties_set_float(runtime->scene_state, grid_key, value);
    if (brush_grid_key != NULL && brush_grid_key[0] != '\0')
        slayer3d_properties_set_float(runtime->scene_state, brush_grid_key, value);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "grid size selected");
}

static bool editor_handle_grid_widget(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    yyjson_val *widget = obj_get(editor, "grid_widget");
    yyjson_val *values = obj_get(widget, "values");
    if (runtime == NULL || runtime->scene_state == NULL || !yyjson_is_obj(widget) || !yyjson_is_arr(values))
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (input == NULL || !slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        return true;

    const char *open_key = json_string(widget, "open_key", "editor.grid.menu.open");
    const bool opened = slayer3d_properties_get_bool(runtime->scene_state, open_key, false);
    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool over_button = editor_mouse_in_rect(mouse_x, mouse_y, obj_get(widget, "button"));
    const bool over_popup = editor_mouse_in_rect(mouse_x, mouse_y, obj_get(widget, "popup"));
    if (!left_pressed)
    {
        if (out_consumed != NULL)
            *out_consumed = over_button || (opened && over_popup);
        return true;
    }

    if (over_button)
    {
        slayer3d_properties_set_bool(runtime->scene_state, open_key, !opened);
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    if (opened && over_popup)
    {
        yyjson_val *popup = obj_get(widget, "popup");
        const float y = json_float(popup, "y", 0.0f);
        const float row_height = SDL_max(json_float(widget, "row_height", 24.0f), 1.0f);
        const int index = (int)SDL_floorf((mouse_y - y) / row_height);
        if (index >= 0 && index < (int)yyjson_arr_size(values))
        {
            yyjson_val *value = yyjson_arr_get(values, (size_t)index);
            if (yyjson_is_num(value))
                editor_set_grid_value(runtime, widget, (float)yyjson_get_num(value));
        }
        slayer3d_properties_set_bool(runtime->scene_state, open_key, false);
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    if (opened)
        slayer3d_properties_set_bool(runtime->scene_state, open_key, false);
    return true;
}

slayer3d_game_data_editor_selection resolved_editor_selection(const slayer3d_game_data_runtime *runtime,
                                                              const slayer3d_game_data_editor_selection *selection);

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

static const char *editor_selection_type_name(slayer3d_game_data_world_model_type type)
{
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL)
        return "sector_level";
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
        return "brush_world";
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_PLAYER_START)
        return "editor_player_start";
    return "none";
}

static bool editor_selection_is_selectable_brush(const slayer3d_game_data_editor_selection *selection)
{
    return selection != NULL && selection->hit && selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD &&
           selection->world_name != NULL && selection->world_name[0] != '\0' && selection->element_name != NULL &&
           selection->element_name[0] != '\0';
}

static bool editor_selection_from_brush_index(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                              int brush_index, int face_index,
                                              slayer3d_game_data_editor_selection *out_selection)
{
    if (out_selection == NULL)
        return false;
    init_editor_selection(out_selection);
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || brush_index < 0)
        return false;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
        return false;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (brush_index >= world->brush_count)
        return false;

    const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
    if (brush->name == NULL || brush->name[0] == '\0')
        return false;

    out_selection->hit = true;
    out_selection->type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
    out_selection->world_name = world->name;
    out_selection->world_position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    out_selection->element_name = brush->name;
    out_selection->element_index = brush_index;
    out_selection->face_index = face_index >= 0 && face_index < brush->face_count ? face_index : -1;
    out_selection->has_bounds = brush->has_bounds;
    if (brush->has_bounds)
        out_selection->bounds = brush->bounds;
    return true;
}

static int editor_selected_brush_index(const slayer3d_game_data_runtime *runtime,
                                       const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || !editor_selected_brushes_active_for_scene(runtime) ||
        !editor_selection_is_selectable_brush(selection))
    {
        return -1;
    }
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection *candidate = &runtime->editor_selected_brushes[i];
        if (editor_selection_is_selectable_brush(candidate) &&
            SDL_strcmp(candidate->world_name, selection->world_name) == 0 &&
            SDL_strcmp(candidate->element_name, selection->element_name) == 0)
        {
            return i;
        }
    }
    return -1;
}

static void remove_editor_selected_brush_at(slayer3d_game_data_runtime *runtime, int index)
{
    if (runtime == NULL || index < 0 || index >= runtime->editor_selected_brush_count)
        return;
    for (int i = index; i + 1 < runtime->editor_selected_brush_count; ++i)
        runtime->editor_selected_brushes[i] = runtime->editor_selected_brushes[i + 1];
    runtime->editor_selected_brush_count--;
    init_editor_selection(&runtime->editor_selected_brushes[runtime->editor_selected_brush_count]);
    publish_editor_selected_brush_count(runtime);
}

static bool add_editor_selected_brush(slayer3d_game_data_runtime *runtime,
                                      const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || !editor_selection_is_selectable_brush(selection) ||
        runtime->editor_selected_brush_count >= SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY)
    {
        return false;
    }
    if (!editor_selected_brushes_active_for_scene(runtime))
        clear_editor_selected_brushes(runtime);
    runtime->editor_selected_brushes[runtime->editor_selected_brush_count++] = *selection;
    runtime->editor_selected_brush_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_brush_count(runtime);
    return true;
}

static void update_active_editor_selection_from_selected_brushes(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    if (editor_selected_brushes_active_for_scene(runtime) && runtime->editor_selected_brush_count > 0)
        runtime->editor_active_selection = runtime->editor_selected_brushes[runtime->editor_selected_brush_count - 1];
    else
        init_editor_selection(&runtime->editor_active_selection);
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
}

static bool editor_select_mode_primary_click(slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_editor_selection *hover_selection)
{
    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    if (!editor_selection_is_selectable_brush(hover_selection))
    {
        if (hover_selection != NULL && hover_selection->hit &&
            hover_selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_PLAYER_START)
        {
            clear_editor_selected_brushes(runtime);
            runtime->editor_active_selection = *hover_selection;
            runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
            return true;
        }
        if (!shift)
            clear_editor_active_selection(runtime);
        return true;
    }

    const int selected_index = editor_selected_brush_index(runtime, hover_selection);
    if (selected_index >= 0)
    {
        remove_editor_selected_brush_at(runtime, selected_index);
        update_active_editor_selection_from_selected_brushes(runtime);
        return true;
    }

    if (!shift)
        clear_editor_selected_brushes(runtime);
    if (!add_editor_selected_brush(runtime, hover_selection))
        return false;
    update_active_editor_selection_from_selected_brushes(runtime);
    return true;
}

static bool editor_select_mode_secondary_click(slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_editor_selection *hover_selection)
{
    if (editor_selection_is_selectable_brush(hover_selection) &&
        editor_selected_brush_index(runtime, hover_selection) < 0)
    {
        clear_editor_selected_brushes(runtime);
        if (!add_editor_selected_brush(runtime, hover_selection))
            return false;
        update_active_editor_selection_from_selected_brushes(runtime);
        return true;
    }

    if (!editor_selection_is_selectable_brush(hover_selection) && hover_selection != NULL && hover_selection->hit)
    {
        clear_editor_selected_brushes(runtime);
        runtime->editor_active_selection = *hover_selection;
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    }
    return true;
}

static bool editor_mode_is_select(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "select") == 0;
}

static bool editor_mode_is_brush(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "brush") == 0;
}

static bool editor_mode_is_face(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "face") == 0;
}

static bool editor_mode_is_vertex(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "vertex") == 0;
}

static bool editor_selection_matches_brush(const slayer3d_game_data_editor_selection *selection, const char *world_name,
                                           const char *element_name)
{
    return selection != NULL && selection->hit && selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD &&
           selection->world_name != NULL && selection->element_name != NULL && world_name != NULL &&
           element_name != NULL && SDL_strcmp(selection->world_name, world_name) == 0 &&
           SDL_strcmp(selection->element_name, element_name) == 0;
}

static bool editor_hover_is_selected_brush(const slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_editor_selection *hover_selection)
{
    if (runtime == NULL || hover_selection == NULL ||
        hover_selection->type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
        return false;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        if (editor_selection_matches_brush(&runtime->editor_selected_brushes[i], hover_selection->world_name,
                                           hover_selection->element_name))
        {
            return true;
        }
    }
    return false;
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

static void clear_editor_vertex_hover_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.hover.hit", false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.brush", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.brush_stable_id", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.vertex", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.hover.selected", false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.index", -1);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.shared_count", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.x", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.y", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.z", 0);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.hover.coord",
                                 slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

static int editor_source_index_for_selection(const brush_world_runtime *world_runtime,
                                             const slayer3d_game_data_editor_selection *selection)
{
    if (world_runtime == NULL || selection == NULL || !editor_selection_is_selectable_brush(selection))
        return -1;
    int source_index = editor_brush_world_find_source_box_index(world_runtime, selection->element_name);
    if (source_index >= 0)
        return source_index;
    return editor_brush_world_find_source_box_index(world_runtime,
                                                    editor_metadata_stable_id(selection->element_editor));
}

static slayer3d_vec3 editor_source_vertex_meters(const brush_world_runtime *world_runtime,
                                                 const editor_brush_source_vertex *vertex)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return slayer3d_vec3_make((float)vertex->coord[0] * meters_per_unit, (float)vertex->coord[1] * meters_per_unit,
                              (float)vertex->coord[2] * meters_per_unit);
}

static int editor_collect_selected_source_indices(const slayer3d_game_data_runtime *runtime,
                                                  const brush_world_runtime *world_runtime, int fallback_source_index,
                                                  int *out_indices, int out_capacity)
{
    if (runtime == NULL || world_runtime == NULL || out_indices == NULL || out_capacity <= 0)
        return 0;

    int count = 0;
    for (int i = 0; i < runtime->editor_selected_brush_count && count < out_capacity; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (selection.world_name == NULL || SDL_strcmp(selection.world_name, world_runtime->desc.name) != 0)
            continue;
        const int source_index = editor_source_index_for_selection(world_runtime, &selection);
        if (source_index >= 0)
            out_indices[count++] = source_index;
    }
    if (count == 0 && fallback_source_index >= 0)
        out_indices[count++] = fallback_source_index;
    return count;
}

static int editor_source_vertex_shared_count(const brush_world_runtime *world_runtime, const int coord[3],
                                             const int *selected_indices, int selected_count)
{
    editor_brush_source_shared_vertex
        shared[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY * SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int shared_count = 0;
    if (world_runtime == NULL || coord == NULL || selected_indices == NULL || selected_count <= 0 ||
        !editor_brush_world_find_shared_source_vertices(world_runtime, selected_indices, selected_count, shared,
                                                        (int)SDL_arraysize(shared), &shared_count, NULL, 0))
    {
        return 1;
    }
    for (int i = 0; i < shared_count; ++i)
    {
        if (shared[i].coord[0] == coord[0] && shared[i].coord[1] == coord[1] && shared[i].coord[2] == coord[2])
            return SDL_max(shared[i].reference_count, 1);
    }
    return 1;
}

static bool editor_source_vertex_selection_matches(const editor_source_vertex_selection *a,
                                                   const editor_source_vertex_selection *b)
{
    return a != NULL && b != NULL && a->source_index == b->source_index && a->vertex_index == b->vertex_index &&
           SDL_strcmp(a->world_name, b->world_name) == 0 && SDL_strcmp(a->brush_stable_id, b->brush_stable_id) == 0 &&
           SDL_strcmp(a->vertex_stable_id, b->vertex_stable_id) == 0;
}

static int editor_selected_vertex_index(const slayer3d_game_data_runtime *runtime,
                                        const editor_source_vertex_selection *selection)
{
    if (runtime == NULL || selection == NULL || !editor_selected_vertices_active_for_scene(runtime))
        return -1;
    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        if (editor_source_vertex_selection_matches(&runtime->editor_selected_vertices[i], selection))
            return i;
    }
    return -1;
}

static void remove_editor_selected_vertex_at(slayer3d_game_data_runtime *runtime, int index)
{
    if (runtime == NULL || index < 0 || index >= runtime->editor_selected_vertex_count)
        return;
    for (int i = index; i + 1 < runtime->editor_selected_vertex_count; ++i)
        runtime->editor_selected_vertices[i] = runtime->editor_selected_vertices[i + 1];
    runtime->editor_selected_vertex_count--;
    init_editor_source_vertex_selection(&runtime->editor_selected_vertices[runtime->editor_selected_vertex_count]);
}

static bool add_editor_selected_vertex(slayer3d_game_data_runtime *runtime,
                                       const editor_source_vertex_selection *selection)
{
    if (runtime == NULL || selection == NULL ||
        runtime->editor_selected_vertex_count >= SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY)
    {
        return false;
    }
    if (!editor_selected_vertices_active_for_scene(runtime))
        clear_editor_selected_vertices(runtime);
    if (editor_selected_vertex_index(runtime, selection) >= 0)
        return true;
    runtime->editor_selected_vertices[runtime->editor_selected_vertex_count++] = *selection;
    runtime->editor_selected_vertex_scene = slayer3d_game_data_active_scene(runtime);
    return true;
}

static bool editor_coord_equal(const int a[3], const int b[3])
{
    return a != NULL && b != NULL && a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static bool editor_source_vertex_selection_from_model(const brush_world_runtime *world_runtime,
                                                      const editor_brush_source_vertex_model *model, int vertex_index,
                                                      editor_source_vertex_selection *out_selection)
{
    if (world_runtime == NULL || model == NULL || out_selection == NULL || vertex_index < 0 ||
        vertex_index >= model->vertex_count)
    {
        return false;
    }

    init_editor_source_vertex_selection(out_selection);
    const editor_brush_source_vertex *vertex = &model->vertices[vertex_index];
    const editor_brush_source_box_runtime *box = NULL;
    if (model->brush_index >= 0 && model->brush_index < world_runtime->editor_source_box_count)
        box = &world_runtime->editor_source_boxes[model->brush_index];
    SDL_strlcpy(out_selection->world_name, world_runtime->desc.name != NULL ? world_runtime->desc.name : "",
                sizeof(out_selection->world_name));
    SDL_strlcpy(out_selection->brush_name, box != NULL && box->name != NULL ? box->name : model->brush_stable_id,
                sizeof(out_selection->brush_name));
    SDL_strlcpy(out_selection->brush_stable_id, model->brush_stable_id, sizeof(out_selection->brush_stable_id));
    SDL_strlcpy(out_selection->vertex_stable_id, vertex->stable_id, sizeof(out_selection->vertex_stable_id));
    out_selection->source_index = model->brush_index;
    out_selection->vertex_index = vertex->vertex_index;
    out_selection->coord[0] = vertex->coord[0];
    out_selection->coord[1] = vertex->coord[1];
    out_selection->coord[2] = vertex->coord[2];
    return true;
}

static bool editor_hover_source_vertex_selection(const slayer3d_game_data_runtime *runtime,
                                                 const slayer3d_game_data_editor_selection *hover_selection,
                                                 editor_source_vertex_selection *out_selection)
{
    if (out_selection != NULL)
        init_editor_source_vertex_selection(out_selection);
    if (runtime == NULL || out_selection == NULL || !editor_mode_is_vertex(runtime) ||
        !editor_selection_is_selectable_brush(hover_selection))
    {
        return false;
    }

    slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, hover_selection);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, resolved.world_name);
    const int source_index = editor_source_index_for_selection(world_runtime, &resolved);
    editor_brush_source_vertex_model model;
    if (world_runtime == NULL || source_index < 0 ||
        !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
    {
        return false;
    }

    const slayer3d_vec3 local_point = slayer3d_vec3_sub(resolved.point, resolved.world_position);
    int nearest_vertex = -1;
    float nearest_distance_sq = FLT_MAX;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        const slayer3d_vec3 vertex = editor_source_vertex_meters(world_runtime, &model.vertices[i]);
        const float distance_sq = slayer3d_vec3_length_squared(slayer3d_vec3_sub(vertex, local_point));
        if (distance_sq < nearest_distance_sq)
        {
            nearest_distance_sq = distance_sq;
            nearest_vertex = i;
        }
    }
    return editor_source_vertex_selection_from_model(world_runtime, &model, nearest_vertex, out_selection);
}

static bool editor_add_shared_vertex_selection_group(slayer3d_game_data_runtime *runtime,
                                                     const editor_source_vertex_selection *selection)
{
    if (runtime == NULL || selection == NULL)
        return false;
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    if (world_runtime == NULL)
        return false;

    int selected_indices[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
    const int selected_count = editor_collect_selected_source_indices(
        runtime, world_runtime, selection->source_index, selected_indices, SDL_arraysize(selected_indices));
    bool added_any = false;
    for (int i = 0; i < selected_count; ++i)
    {
        editor_brush_source_vertex_model model;
        if (!editor_brush_source_box_build_vertex_model(world_runtime, selected_indices[i], &model, NULL, 0))
            continue;
        for (int v = 0; v < model.vertex_count; ++v)
        {
            if (!editor_coord_equal(model.vertices[v].coord, selection->coord))
                continue;
            editor_source_vertex_selection candidate;
            if (editor_source_vertex_selection_from_model(world_runtime, &model, v, &candidate) &&
                add_editor_selected_vertex(runtime, &candidate))
            {
                added_any = true;
            }
        }
    }
    return added_any;
}

static bool editor_remove_shared_vertex_selection_group(slayer3d_game_data_runtime *runtime,
                                                        const editor_source_vertex_selection *selection)
{
    if (runtime == NULL || selection == NULL || !editor_selected_vertices_active_for_scene(runtime))
        return false;
    bool removed_any = false;
    for (int i = runtime->editor_selected_vertex_count - 1; i >= 0; --i)
    {
        const editor_source_vertex_selection *candidate = &runtime->editor_selected_vertices[i];
        if (SDL_strcmp(candidate->world_name, selection->world_name) == 0 &&
            editor_coord_equal(candidate->coord, selection->coord))
        {
            remove_editor_selected_vertex_at(runtime, i);
            removed_any = true;
        }
    }
    return removed_any;
}

static void publish_editor_vertex_hover_state(slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_editor_selection *hover_selection)
{
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_vertex(runtime) ||
        !editor_selection_is_selectable_brush(hover_selection))
    {
        clear_editor_vertex_hover_state(runtime);
        return;
    }

    slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, hover_selection);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, resolved.world_name);
    const int source_index = editor_source_index_for_selection(world_runtime, &resolved);
    editor_brush_source_vertex_model model;
    if (world_runtime == NULL || source_index < 0 ||
        !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
    {
        clear_editor_vertex_hover_state(runtime);
        return;
    }

    const slayer3d_vec3 local_point = slayer3d_vec3_sub(resolved.point, resolved.world_position);
    int nearest_vertex = -1;
    float nearest_distance_sq = FLT_MAX;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        const slayer3d_vec3 vertex = editor_source_vertex_meters(world_runtime, &model.vertices[i]);
        const float distance_sq = slayer3d_vec3_length_squared(slayer3d_vec3_sub(vertex, local_point));
        if (distance_sq < nearest_distance_sq)
        {
            nearest_distance_sq = distance_sq;
            nearest_vertex = i;
        }
    }
    if (nearest_vertex < 0)
    {
        clear_editor_vertex_hover_state(runtime);
        return;
    }

    int selected_indices[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
    const int selected_count = editor_collect_selected_source_indices(
        runtime, world_runtime, source_index, selected_indices, SDL_arraysize(selected_indices));
    const editor_brush_source_vertex *vertex = &model.vertices[nearest_vertex];
    const int shared_count =
        editor_source_vertex_shared_count(world_runtime, vertex->coord, selected_indices, selected_count);
    const slayer3d_vec3 coord_meters = editor_source_vertex_meters(world_runtime, vertex);

    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.hover.hit", true);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.brush", resolved.element_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.brush_stable_id",
                                   editor_metadata_stable_id(resolved.element_editor));
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.vertex", vertex->stable_id);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.index", vertex->vertex_index);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.shared_count", shared_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.x", vertex->coord[0]);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.y", vertex->coord[1]);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.z", vertex->coord[2]);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.hover.coord", coord_meters);
    editor_source_vertex_selection hovered;
    if (editor_source_vertex_selection_from_model(world_runtime, &model, nearest_vertex, &hovered))
    {
        slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.hover.selected",
                                     editor_selected_vertex_index(runtime, &hovered) >= 0);
    }
}

static void editor_begin_vertex_drag(slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                     const slayer3d_game_data_editor_selection *hover_selection);
static void publish_editor_vertex_add_result(slayer3d_game_data_runtime *runtime, yyjson_val *action, bool valid,
                                             int vertex_count, int added_count, const char *message);
static bool editor_add_vertex_to_source_coord(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              brush_world_runtime *world_runtime, int source_index, const int coord[3]);
static int editor_source_units_from_meters(const brush_world_runtime *world_runtime, float meters);

static bool editor_hover_source_vertex_selection_with_distance(
    const slayer3d_game_data_runtime *runtime, const slayer3d_game_data_editor_selection *hover_selection,
    editor_source_vertex_selection *out_selection, float *out_distance_sq)
{
    if (out_selection != NULL)
        init_editor_source_vertex_selection(out_selection);
    if (out_distance_sq != NULL)
        *out_distance_sq = FLT_MAX;
    if (runtime == NULL || out_selection == NULL || !editor_mode_is_vertex(runtime) ||
        !editor_selection_is_selectable_brush(hover_selection))
    {
        return false;
    }

    slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, hover_selection);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, resolved.world_name);
    const int source_index = editor_source_index_for_selection(world_runtime, &resolved);
    editor_brush_source_vertex_model model;
    if (world_runtime == NULL || source_index < 0 ||
        !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
    {
        return false;
    }

    const slayer3d_vec3 local_point = slayer3d_vec3_sub(resolved.point, resolved.world_position);
    int nearest_vertex = -1;
    float nearest_distance_sq = FLT_MAX;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        const slayer3d_vec3 vertex = editor_source_vertex_meters(world_runtime, &model.vertices[i]);
        const float distance_sq = slayer3d_vec3_length_squared(slayer3d_vec3_sub(vertex, local_point));
        if (distance_sq < nearest_distance_sq)
        {
            nearest_distance_sq = distance_sq;
            nearest_vertex = i;
        }
    }
    if (out_distance_sq != NULL)
        *out_distance_sq = nearest_distance_sq;
    return editor_source_vertex_selection_from_model(world_runtime, &model, nearest_vertex, out_selection);
}

static bool editor_handle_vertex_selection(slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_editor_selection *hover_selection,
                                           bool select_requested, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || !editor_mode_is_vertex(runtime) || !select_requested)
        return true;

    editor_source_vertex_selection hovered;
    if (!editor_hover_source_vertex_selection(runtime, hover_selection, &hovered))
    {
        if ((SDL_GetModState() & SDL_KMOD_SHIFT) == 0)
            clear_editor_selected_vertices(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    const bool already_selected = editor_selected_vertex_index(runtime, &hovered) >= 0;
    if (shift && already_selected)
    {
        (void)editor_remove_shared_vertex_selection_group(runtime, &hovered);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "vertex deselected");
    }
    else
    {
        if (!shift)
            clear_editor_selected_vertices(runtime);
        if (!editor_add_shared_vertex_selection_group(runtime, &hovered))
            return false;
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                       shift ? "vertex added to selection" : "vertex selected");
        editor_begin_vertex_drag(runtime, runtime_input(runtime), hover_selection);
    }

    publish_editor_selected_vertex_count(runtime);
    publish_editor_vertex_hover_state(runtime, hover_selection);
    if (out_consumed != NULL)
        *out_consumed = true;
    return true;
}

static int editor_source_snap_units_from_grid(const brush_world_runtime *world_runtime, float grid_size)
{
    return SDL_max(editor_source_units_from_meters(world_runtime, grid_size), 1);
}

static int editor_snap_source_coord_to_units(int coord, int snap_units)
{
    if (snap_units <= 1)
        return coord;
    const int half = snap_units / 2;
    return coord >= 0 ? ((coord + half) / snap_units) * snap_units : ((coord - half) / snap_units) * snap_units;
}

static bool editor_vertex_add_coord_from_selection(const brush_world_runtime *world_runtime,
                                                   const slayer3d_game_data_runtime *runtime,
                                                   const slayer3d_game_data_editor_selection *selection,
                                                   int out_coord[3])
{
    if (world_runtime == NULL || runtime == NULL || selection == NULL || out_coord == NULL || !selection->hit)
        return false;
    const float grid_size = SDL_max(slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f),
                                    world_runtime->editor_source_meters_per_unit);
    const int snap_units = editor_source_snap_units_from_grid(world_runtime, grid_size);
    const slayer3d_vec3 normal = slayer3d_vec3_length_squared(selection->normal) > 0.000001f
                                     ? slayer3d_vec3_normalize(selection->normal)
                                     : slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    const slayer3d_vec3 local_point = slayer3d_vec3_sub(
        slayer3d_vec3_add(selection->point, slayer3d_vec3_scale(normal, grid_size)), selection->world_position);
    out_coord[0] =
        editor_snap_source_coord_to_units(editor_source_units_from_meters(world_runtime, local_point.x), snap_units);
    out_coord[1] =
        editor_snap_source_coord_to_units(editor_source_units_from_meters(world_runtime, local_point.y), snap_units);
    out_coord[2] =
        editor_snap_source_coord_to_units(editor_source_units_from_meters(world_runtime, local_point.z), snap_units);
    return true;
}

static bool editor_handle_vertex_add_to_source(slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_editor_selection *hover_selection,
                                               bool select_requested, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_vertex(runtime) || !select_requested ||
        (SDL_GetModState() & SDL_KMOD_SHIFT) == 0 || !editor_selection_is_selectable_brush(hover_selection) ||
        hover_selection->face_index < 0)
    {
        return true;
    }

    editor_source_vertex_selection nearest;
    float nearest_distance_sq = FLT_MAX;
    (void)editor_hover_source_vertex_selection_with_distance(runtime, hover_selection, &nearest, &nearest_distance_sq);
    const float grid_size =
        SDL_max(slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f), 0.001f);
    const float handle_radius = SDL_max(grid_size * 0.75f, 0.05f);
    if (nearest_distance_sq <= handle_radius * handle_radius)
        return true;

    const slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, hover_selection);
    if (editor_selected_brush_index(runtime, &resolved) < 0)
        return true;
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, resolved.world_name);
    const int source_index = editor_source_index_for_selection(world_runtime, &resolved);
    int coord[3] = {0, 0, 0};
    if (world_runtime == NULL || source_index < 0 ||
        !editor_vertex_add_coord_from_selection(world_runtime, runtime, &resolved, coord))
    {
        publish_editor_vertex_add_result(runtime, NULL, false, 0, 0, "vertex add requires a source face");
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    if (!editor_add_vertex_to_source_coord(runtime, NULL, world_runtime, source_index, coord))
        return false;
    clear_editor_vertex_hover_state(runtime);
    if (out_consumed != NULL)
        *out_consumed = true;
    return true;
}

static bool editor_project_world_to_viewport(const slayer3d_camera3d *camera, const editor_trace_viewport_config *view,
                                             slayer3d_vec3 point, float *out_x, float *out_y)
{
    if (camera == NULL || view == NULL || out_x == NULL || out_y == NULL || view->width <= 0.0f || view->height <= 0.0f)
    {
        return false;
    }

    slayer3d_mat4 view_matrix;
    slayer3d_mat4 projection_matrix;
    if (!slayer3d_camera3d_compute_matrices(camera, (int)SDL_roundf(view->width), (int)SDL_roundf(view->height), 0.01f,
                                            1000.0f, &view_matrix, &projection_matrix))
    {
        return false;
    }

    const slayer3d_mat4 view_projection = slayer3d_mat4_multiply(projection_matrix, view_matrix);
    const slayer3d_vec4 clip = slayer3d_mat4_transform_vec4(view_projection, slayer3d_vec4_from_vec3(point, 1.0f));
    if (SDL_fabsf(clip.w) <= 0.000001f)
        return false;

    const float ndc_x = clip.x / clip.w;
    const float ndc_y = clip.y / clip.w;
    const float ndc_z = clip.z / clip.w;
    if (ndc_z < -1.0f || ndc_z > 1.0f)
        return false;

    *out_x = (ndc_x + 1.0f) * 0.5f * view->width;
    *out_y = (1.0f - ndc_y) * 0.5f * view->height;
    return true;
}

static bool editor_lasso_contains_point(const editor_drag_move_state *drag, const editor_trace_viewport_config *view,
                                        float screen_x, float screen_y)
{
    if (drag == NULL || view == NULL)
        return false;
    const float start_x = drag->start_mouse_x - view->x;
    const float start_y = drag->start_mouse_y - view->y;
    const float end_x = drag->current_mouse_x - view->x;
    const float end_y = drag->current_mouse_y - view->y;
    const float min_x = SDL_min(start_x, end_x);
    const float max_x = SDL_max(start_x, end_x);
    const float min_y = SDL_min(start_y, end_y);
    const float max_y = SDL_max(start_y, end_y);
    return screen_x >= min_x && screen_x <= max_x && screen_y >= min_y && screen_y <= max_y;
}

static int editor_select_vertices_in_lasso(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                           const editor_drag_move_state *drag)
{
    if (runtime == NULL || selection_json == NULL || drag == NULL || !editor_selected_brushes_active_for_scene(runtime))
        return 0;

    yyjson_val *trace = obj_get(selection_json, "trace");
    editor_trace_viewport_config viewport;
    if (!editor_trace_select_viewport_at(runtime, trace, drag->start_mouse_x, drag->start_mouse_y, &viewport))
        return 0;

    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, viewport.camera, &camera))
        return 0;

    if (!drag->lasso_additive)
        clear_editor_selected_vertices(runtime);

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection.world_name);
        const int source_index = editor_source_index_for_selection(world_runtime, &selection);
        editor_brush_source_vertex_model model;
        if (world_runtime == NULL || source_index < 0 ||
            !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
        {
            continue;
        }

        for (int v = 0; v < model.vertex_count; ++v)
        {
            const slayer3d_vec3 local = editor_source_vertex_meters(world_runtime, &model.vertices[v]);
            const slayer3d_vec3 world = slayer3d_vec3_add(selection.world_position, local);
            float screen_x = 0.0f;
            float screen_y = 0.0f;
            if (!editor_project_world_to_viewport(&camera, &viewport, world, &screen_x, &screen_y) ||
                !editor_lasso_contains_point(drag, &viewport, screen_x, screen_y))
            {
                continue;
            }
            editor_source_vertex_selection candidate;
            if (editor_source_vertex_selection_from_model(world_runtime, &model, v, &candidate))
                (void)add_editor_selected_vertex(runtime, &candidate);
        }
    }

    publish_editor_selected_vertex_count(runtime);
    return runtime->editor_selected_vertex_count;
}

static bool editor_handle_vertex_lasso(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                       const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL)
        return true;

    editor_drag_move_state *drag = &runtime->editor_drag_move;
    if (!editor_mode_is_vertex(runtime))
    {
        if (drag->active && drag->vertex_lasso)
            clear_editor_drag_move(runtime);
        publish_editor_vertex_lasso_state(runtime, NULL, 0);
        return true;
    }

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return true;

    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    float mouse_x = drag->current_mouse_x;
    float mouse_y = drag->current_mouse_y;
    (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);

    if (drag->active && drag->vertex_lasso)
    {
        drag->current_mouse_x = mouse_x;
        drag->current_mouse_y = mouse_y;
        publish_editor_vertex_lasso_state(runtime, drag, 0);
        if (out_consumed != NULL)
            *out_consumed = true;
        if (left_released || !left_down)
        {
            const float dx = drag->current_mouse_x - drag->start_mouse_x;
            const float dy = drag->current_mouse_y - drag->start_mouse_y;
            const bool meaningful_drag = dx * dx + dy * dy >= 16.0f;
            const int selected_count =
                meaningful_drag ? editor_select_vertices_in_lasso(runtime, selection_json, drag) : 0;
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                           meaningful_drag ? "vertex lasso selected" : "vertex lasso canceled");
            publish_editor_vertex_lasso_state(runtime, NULL, selected_count);
            clear_editor_drag_move(runtime);
        }
        return true;
    }

    const bool selectable_hover = editor_selection_is_selectable_brush(hover_selection);
    if (left_pressed && !selectable_hover)
    {
        SDL_zero(*drag);
        drag->active = true;
        drag->vertex_lasso = true;
        drag->lasso_additive = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        drag->start_mouse_x = mouse_x;
        drag->start_mouse_y = mouse_y;
        drag->current_mouse_x = mouse_x;
        drag->current_mouse_y = mouse_y;
        publish_editor_vertex_lasso_state(runtime, drag, 0);
        if (out_consumed != NULL)
            *out_consumed = true;
    }
    return true;
}

typedef struct editor_vertex_bounds_accumulator
{
    editor_source_box_bounds_update update;
    bool min_touched[3];
    bool max_touched[3];
} editor_vertex_bounds_accumulator;

static int editor_source_units_from_meters(const brush_world_runtime *world_runtime, float meters)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return (int)SDL_roundf(meters / meters_per_unit);
}

static editor_vertex_bounds_accumulator *editor_find_vertex_bounds_accumulator(
    editor_vertex_bounds_accumulator *accumulators, int count, int source_index)
{
    for (int i = 0; i < count; ++i)
    {
        if (accumulators[i].update.source_index == source_index)
            return &accumulators[i];
    }
    return NULL;
}

static void editor_refresh_selected_brushes_after_source_edit(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || !editor_selected_brushes_active_for_scene(runtime))
        return;
    for (int i = runtime->editor_selected_brush_count - 1; i >= 0; --i)
    {
        const slayer3d_game_data_editor_selection stale = runtime->editor_selected_brushes[i];
        slayer3d_game_data_editor_selection refreshed;
        if (!editor_selection_from_brush_index(runtime, stale.world_name, stale.element_index, stale.face_index,
                                               &refreshed))
        {
            remove_editor_selected_brush_at(runtime, i);
            continue;
        }
        runtime->editor_selected_brushes[i] = resolved_editor_selection(runtime, &refreshed);
    }
    update_active_editor_selection_from_selected_brushes(runtime);
    publish_editor_selected_brush_count(runtime);
}

static void editor_refresh_selected_vertices_after_source_edit(slayer3d_game_data_runtime *runtime,
                                                               const brush_world_runtime *world_runtime)
{
    if (runtime == NULL || world_runtime == NULL || !editor_selected_vertices_active_for_scene(runtime))
        return;
    for (int i = runtime->editor_selected_vertex_count - 1; i >= 0; --i)
    {
        editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        if (SDL_strcmp(selection->world_name, world_runtime->desc.name) != 0)
            continue;
        editor_brush_source_vertex_model model;
        if (!editor_brush_source_box_build_vertex_model(world_runtime, selection->source_index, &model, NULL, 0) ||
            selection->vertex_index < 0 || selection->vertex_index >= model.vertex_count)
        {
            remove_editor_selected_vertex_at(runtime, i);
            continue;
        }
        editor_source_vertex_selection refreshed;
        if (editor_source_vertex_selection_from_model(world_runtime, &model, selection->vertex_index, &refreshed))
            *selection = refreshed;
    }
    publish_editor_selected_vertex_count(runtime);
}

static bool editor_translate_selected_vertices(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset)
{
    if (runtime == NULL || runtime->scene_state == NULL || !editor_selected_vertices_active_for_scene(runtime) ||
        runtime->editor_selected_vertex_count <= 0 || slayer3d_vec3_length_squared(offset) <= 0.0000001f)
    {
        return false;
    }

    const char *world_name = runtime->editor_selected_vertices[0].world_name;
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;

    const int delta[3] = {editor_source_units_from_meters(world_runtime, offset.x),
                          editor_source_units_from_meters(world_runtime, offset.y),
                          editor_source_units_from_meters(world_runtime, offset.z)};
    if (delta[0] == 0 && delta[1] == 0 && delta[2] == 0)
        return false;

    editor_vertex_bounds_accumulator accumulators[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(accumulators);
    int accumulator_count = 0;
    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        if (SDL_strcmp(selection->world_name, world_runtime->desc.name) != 0 || selection->source_index < 0 ||
            selection->source_index >= world_runtime->editor_source_box_count)
        {
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                           "vertex move blocked: invalid source selection");
            return false;
        }

        editor_vertex_bounds_accumulator *accumulator =
            editor_find_vertex_bounds_accumulator(accumulators, accumulator_count, selection->source_index);
        if (accumulator == NULL)
        {
            if (accumulator_count >= (int)SDL_arraysize(accumulators))
                return false;
            accumulator = &accumulators[accumulator_count++];
            accumulator->update.source_index = selection->source_index;
            const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[selection->source_index];
            for (int axis = 0; axis < 3; ++axis)
            {
                accumulator->update.min[axis] = box->min[axis];
                accumulator->update.max[axis] = box->max[axis];
            }
        }

        const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[selection->source_index];
        for (int axis = 0; axis < 3; ++axis)
        {
            if (delta[axis] == 0)
                continue;
            if (selection->coord[axis] == box->min[axis] && !accumulator->min_touched[axis])
            {
                accumulator->update.min[axis] += delta[axis];
                accumulator->min_touched[axis] = true;
            }
            if (selection->coord[axis] == box->max[axis] && !accumulator->max_touched[axis])
            {
                accumulator->update.max[axis] += delta[axis];
                accumulator->max_touched[axis] = true;
            }
        }
    }

    editor_source_box_bounds_update updates[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    for (int i = 0; i < accumulator_count; ++i)
        updates[i] = accumulators[i].update;

    char error[256];
    if (!editor_brush_world_update_source_box_bounds_batch(world_runtime, updates, accumulator_count, error,
                                                           sizeof(error)))
    {
        char message[320];
        SDL_snprintf(message, sizeof(message), "vertex move blocked: %s", error[0] != '\0' ? error : "invalid edit");
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
        editor_refresh_selected_vertices_after_source_edit(runtime, world_runtime);
        editor_refresh_selected_brushes_after_source_edit(runtime);
        return false;
    }

    editor_refresh_selected_vertices_after_source_edit(runtime, world_runtime);
    editor_refresh_selected_brushes_after_source_edit(runtime);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "moved selected vertices");
    return true;
}

typedef struct editor_source_snap_target
{
    brush_world_runtime *world_runtime;
    int source_index;
} editor_source_snap_target;

static bool editor_add_source_snap_target(editor_source_snap_target *targets, int *target_count,
                                          brush_world_runtime *world_runtime, int source_index)
{
    if (targets == NULL || target_count == NULL || world_runtime == NULL || source_index < 0)
        return false;
    for (int i = 0; i < *target_count; ++i)
    {
        if (targets[i].world_runtime == world_runtime && targets[i].source_index == source_index)
            return true;
    }
    if (*target_count >= SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY)
        return false;
    targets[*target_count].world_runtime = world_runtime;
    targets[*target_count].source_index = source_index;
    ++(*target_count);
    return true;
}

static int editor_collect_vertex_snap_targets(slayer3d_game_data_runtime *runtime, editor_source_snap_target *targets,
                                              int target_capacity)
{
    if (runtime == NULL || targets == NULL || target_capacity <= 0 ||
        !editor_selected_vertices_active_for_scene(runtime))
        return 0;
    int target_count = 0;
    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, selection->world_name);
        if (world_runtime == NULL || selection->source_index < 0 ||
            selection->source_index >= world_runtime->editor_source_box_count)
        {
            continue;
        }
        if (target_count >= target_capacity ||
            !editor_add_source_snap_target(targets, &target_count, world_runtime, selection->source_index))
            break;
    }
    return target_count;
}

static int editor_collect_brush_snap_targets(slayer3d_game_data_runtime *runtime, editor_source_snap_target *targets,
                                             int target_capacity)
{
    if (runtime == NULL || targets == NULL || target_capacity <= 0 ||
        !editor_selected_brushes_active_for_scene(runtime))
        return 0;
    int target_count = 0;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, selection.world_name);
        const int source_index = editor_source_index_for_selection(world_runtime, &selection);
        if (target_count >= target_capacity ||
            !editor_add_source_snap_target(targets, &target_count, world_runtime, source_index))
            break;
    }
    return target_count;
}

static int editor_snap_units_from_action(const brush_world_runtime *world_runtime,
                                         const slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const int authored_snap_units = json_int(action, "snap_units", 0);
    if (authored_snap_units > 0)
        return authored_snap_units;

    const float fallback_grid = json_float(action, "default_grid", 1.0f);
    float grid = json_float(action, "grid", fallback_grid);
    const char *grid_key = json_string(action, "grid_key", "editor.grid.size");
    if (runtime != NULL && grid_key != NULL && grid_key[0] != '\0')
        grid = slayer3d_properties_get_float(runtime->scene_state, grid_key, grid);
    return SDL_max(editor_source_units_from_meters(world_runtime, SDL_max(grid, 0.001f)), 1);
}

static void publish_editor_vertex_snap_result(slayer3d_game_data_runtime *runtime, yyjson_val *action, bool valid,
                                              int source_count, int changed_count, int snap_units, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.snap.valid", valid);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.snap.source_count", source_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.snap.changed_count", changed_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.snap.snap_units", snap_units);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.snap.message", message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");

    yyjson_val *outputs = obj_get(action, "outputs");
    if (!yyjson_is_obj(outputs))
        return;
    const char *valid_key = json_string(outputs, "valid_key", NULL);
    const char *message_key = json_string(outputs, "message_key", NULL);
    const char *source_count_key = json_string(outputs, "source_count_key", NULL);
    const char *changed_count_key = json_string(outputs, "changed_count_key", NULL);
    const char *snap_units_key = json_string(outputs, "snap_units_key", NULL);
    if (valid_key != NULL)
        slayer3d_properties_set_bool(runtime->scene_state, valid_key, valid);
    if (message_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, message_key, message != NULL ? message : "");
    if (source_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, source_count_key, source_count);
    if (changed_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, changed_count_key, changed_count);
    if (snap_units_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, snap_units_key, snap_units);
}

bool slayer3d_game_data_snap_selected_editor_vertices(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    editor_source_snap_target targets[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(targets);
    int target_count = editor_collect_vertex_snap_targets(runtime, targets, SDL_arraysize(targets));
    if (target_count <= 0)
        target_count = editor_collect_brush_snap_targets(runtime, targets, SDL_arraysize(targets));
    if (runtime == NULL || target_count <= 0)
    {
        publish_editor_vertex_snap_result(runtime, action, false, 0, 0, 0, "vertex snap requires a selection");
        return true;
    }

    int total_changed = 0;
    int last_snap_units = 0;
    char error[256];
    SDL_zeroa(error);
    for (int i = 0; i < target_count; ++i)
    {
        brush_world_runtime *world_runtime = targets[i].world_runtime;
        editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[targets[i].source_index];
        const char *identity = box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;
        last_snap_units = editor_snap_units_from_action(world_runtime, runtime, action);
        editor_brush_source_vertex_operation_desc desc;
        SDL_zero(desc);
        desc.brush_identity = identity;
        desc.type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_SNAP;
        desc.snap_units = last_snap_units;
        editor_brush_source_vertex_operation_result result;
        SDL_zero(result);
        if (!editor_brush_world_apply_source_vertex_operation(world_runtime, &desc, &result, error, sizeof(error)))
        {
            char message[320];
            SDL_snprintf(message, sizeof(message), "vertex snap blocked: %s",
                         error[0] != '\0' ? error : "invalid source edit");
            publish_editor_vertex_snap_result(runtime, action, false, target_count, total_changed, last_snap_units,
                                              message);
            editor_brush_source_free_runtime_brush(&result.brush);
            return true;
        }
        total_changed += result.changed_count;
        editor_brush_source_free_runtime_brush(&result.brush);
    }

    for (int i = 0; i < target_count; ++i)
    {
        editor_refresh_selected_vertices_after_source_edit(runtime, targets[i].world_runtime);
        editor_refresh_selected_brushes_after_source_edit(runtime);
    }

    char message[160];
    SDL_snprintf(message, sizeof(message), "snapped %d source brush%s to grid", target_count,
                 target_count == 1 ? "" : "es");
    publish_editor_vertex_snap_result(runtime, action, true, target_count, total_changed, last_snap_units, message);
    return true;
}

typedef struct editor_source_vertex_delete_target
{
    brush_world_runtime *world_runtime;
    int source_index;
    int vertex_indices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int vertex_index_count;
} editor_source_vertex_delete_target;

static editor_source_vertex_delete_target *editor_find_vertex_delete_target(editor_source_vertex_delete_target *targets,
                                                                            int target_count,
                                                                            brush_world_runtime *world_runtime,
                                                                            int source_index)
{
    if (targets == NULL || world_runtime == NULL || source_index < 0)
        return NULL;
    for (int i = 0; i < target_count; ++i)
    {
        if (targets[i].world_runtime == world_runtime && targets[i].source_index == source_index)
            return &targets[i];
    }
    return NULL;
}

static bool editor_vertex_delete_target_add_index(editor_source_vertex_delete_target *target, int vertex_index)
{
    if (target == NULL || vertex_index < 0)
        return false;
    for (int i = 0; i < target->vertex_index_count; ++i)
    {
        if (target->vertex_indices[i] == vertex_index)
            return true;
    }
    if (target->vertex_index_count >= SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
        return false;
    target->vertex_indices[target->vertex_index_count++] = vertex_index;
    return true;
}

static int editor_collect_vertex_delete_targets(slayer3d_game_data_runtime *runtime,
                                                editor_source_vertex_delete_target *targets, int target_capacity)
{
    if (runtime == NULL || targets == NULL || target_capacity <= 0 ||
        !editor_selected_vertices_active_for_scene(runtime))
    {
        return 0;
    }

    int target_count = 0;
    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, selection->world_name);
        if (world_runtime == NULL || selection->source_index < 0 ||
            selection->source_index >= world_runtime->editor_source_box_count)
        {
            continue;
        }
        editor_source_vertex_delete_target *target =
            editor_find_vertex_delete_target(targets, target_count, world_runtime, selection->source_index);
        if (target == NULL)
        {
            if (target_count >= target_capacity)
                break;
            target = &targets[target_count++];
            SDL_zero(*target);
            target->world_runtime = world_runtime;
            target->source_index = selection->source_index;
        }
        if (!editor_vertex_delete_target_add_index(target, selection->vertex_index))
            break;
    }
    return target_count;
}

static const char *editor_source_delete_target_identity(const editor_source_vertex_delete_target *target)
{
    if (target == NULL || target->world_runtime == NULL || target->source_index < 0 ||
        target->source_index >= target->world_runtime->editor_source_box_count)
    {
        return NULL;
    }
    const editor_brush_source_box_runtime *box = &target->world_runtime->editor_source_boxes[target->source_index];
    return box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;
}

static void publish_editor_vertex_delete_result(slayer3d_game_data_runtime *runtime, yyjson_val *action, bool valid,
                                                int source_count, int deleted_count, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.delete.valid", valid);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.delete.source_count", source_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.delete.deleted_count", deleted_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.delete.message",
                                   message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");

    yyjson_val *outputs = obj_get(action, "outputs");
    if (!yyjson_is_obj(outputs))
        return;
    const char *valid_key = json_string(outputs, "valid_key", NULL);
    const char *message_key = json_string(outputs, "message_key", NULL);
    const char *source_count_key = json_string(outputs, "source_count_key", NULL);
    const char *deleted_count_key = json_string(outputs, "deleted_count_key", NULL);
    if (valid_key != NULL)
        slayer3d_properties_set_bool(runtime->scene_state, valid_key, valid);
    if (message_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, message_key, message != NULL ? message : "");
    if (source_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, source_count_key, source_count);
    if (deleted_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, deleted_count_key, deleted_count);
}

bool slayer3d_game_data_delete_selected_editor_vertices(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    editor_source_vertex_delete_target targets[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(targets);
    const int target_count = editor_collect_vertex_delete_targets(runtime, targets, SDL_arraysize(targets));
    if (runtime == NULL || target_count <= 0)
    {
        publish_editor_vertex_delete_result(runtime, action, false, 0, 0, "vertex delete requires a selection");
        return true;
    }

    int total_deleted = 0;
    char error[256];
    SDL_zeroa(error);
    for (int i = 0; i < target_count; ++i)
    {
        const char *identity = editor_source_delete_target_identity(&targets[i]);
        editor_brush_source_vertex_operation_desc desc;
        SDL_zero(desc);
        desc.brush_identity = identity;
        desc.type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_DELETE_MANY;
        desc.vertex_index_count = targets[i].vertex_index_count;
        for (int v = 0; v < targets[i].vertex_index_count; ++v)
            desc.vertex_indices[v] = targets[i].vertex_indices[v];

        editor_brush_source_vertex_operation_result preview;
        SDL_zero(preview);
        if (!editor_brush_world_preview_source_vertex_operation(targets[i].world_runtime, &desc, &preview, error,
                                                                sizeof(error)))
        {
            char message[320];
            SDL_snprintf(message, sizeof(message), "vertex delete blocked: %s",
                         error[0] != '\0' ? error : "invalid source edit");
            publish_editor_vertex_delete_result(runtime, action, false, target_count, total_deleted, message);
            editor_brush_source_free_runtime_brush(&preview.brush);
            return true;
        }
        total_deleted += preview.changed_count;
        editor_brush_source_free_runtime_brush(&preview.brush);
    }

    total_deleted = 0;
    for (int i = 0; i < target_count; ++i)
    {
        const char *identity = editor_source_delete_target_identity(&targets[i]);
        editor_brush_source_vertex_operation_desc desc;
        SDL_zero(desc);
        desc.brush_identity = identity;
        desc.type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_DELETE_MANY;
        desc.vertex_index_count = targets[i].vertex_index_count;
        for (int v = 0; v < targets[i].vertex_index_count; ++v)
            desc.vertex_indices[v] = targets[i].vertex_indices[v];

        editor_brush_source_vertex_operation_result result;
        SDL_zero(result);
        if (!editor_brush_world_apply_source_vertex_operation(targets[i].world_runtime, &desc, &result, error,
                                                              sizeof(error)))
        {
            char message[320];
            SDL_snprintf(message, sizeof(message), "vertex delete blocked: %s",
                         error[0] != '\0' ? error : "invalid source edit");
            publish_editor_vertex_delete_result(runtime, action, false, target_count, total_deleted, message);
            editor_brush_source_free_runtime_brush(&result.brush);
            return true;
        }
        total_deleted += result.changed_count;
        editor_brush_source_free_runtime_brush(&result.brush);
    }

    for (int i = 0; i < target_count; ++i)
    {
        editor_refresh_selected_vertices_after_source_edit(runtime, targets[i].world_runtime);
        editor_refresh_selected_brushes_after_source_edit(runtime);
    }
    clear_editor_selected_vertices(runtime);

    char message[160];
    SDL_snprintf(message, sizeof(message), "deleted %d selected source vert%s", total_deleted,
                 total_deleted == 1 ? "ex" : "ices");
    publish_editor_vertex_delete_result(runtime, action, true, target_count, total_deleted, message);
    return true;
}

static bool editor_vertex_merge_target_from_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                   editor_source_vertex_selection *out_target)
{
    if (runtime == NULL || out_target == NULL)
        return false;

    const int target_vertex_index = json_int(action, "target_vertex_index", -1);
    if (target_vertex_index < 0)
        return editor_hover_source_vertex_selection(runtime, &runtime->editor_active_selection, out_target);

    const char *world_name = json_string(action, "world", NULL);
    if ((world_name == NULL || world_name[0] == '\0') && editor_selected_vertices_active_for_scene(runtime) &&
        runtime->editor_selected_vertex_count > 0)
    {
        world_name = runtime->editor_selected_vertices[0].world_name;
    }
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    if (world_runtime == NULL)
        return false;

    int source_index = -1;
    const char *brush_identity = json_string(action, "brush_stable_id", NULL);
    if (brush_identity == NULL || brush_identity[0] == '\0')
        brush_identity = json_string(action, "brush", NULL);
    if (brush_identity != NULL && brush_identity[0] != '\0')
        source_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
    if (source_index < 0 && editor_selected_vertices_active_for_scene(runtime) &&
        runtime->editor_selected_vertex_count > 0 &&
        SDL_strcmp(runtime->editor_selected_vertices[0].world_name, world_runtime->desc.name) == 0)
    {
        source_index = runtime->editor_selected_vertices[0].source_index;
    }
    if (source_index < 0)
        return false;

    editor_brush_source_vertex_model model;
    if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
        return false;
    return editor_source_vertex_selection_from_model(world_runtime, &model, target_vertex_index, out_target);
}

static int editor_collect_vertex_merge_indices(const slayer3d_game_data_runtime *runtime,
                                               const editor_source_vertex_selection *target, int *vertex_indices,
                                               int vertex_capacity)
{
    if (runtime == NULL || target == NULL || vertex_indices == NULL || vertex_capacity <= 0 ||
        !editor_selected_vertices_active_for_scene(runtime))
    {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < runtime->editor_selected_vertex_count; ++i)
    {
        const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
        if (SDL_strcmp(selection->world_name, target->world_name) != 0 ||
            selection->source_index != target->source_index || selection->vertex_index == target->vertex_index)
        {
            continue;
        }
        bool duplicate = false;
        for (int v = 0; v < count; ++v)
            duplicate = duplicate || vertex_indices[v] == selection->vertex_index;
        if (duplicate)
            continue;
        if (count >= vertex_capacity)
            break;
        vertex_indices[count++] = selection->vertex_index;
    }
    return count;
}

static void publish_editor_vertex_merge_result(slayer3d_game_data_runtime *runtime, yyjson_val *action, bool valid,
                                               int merged_count, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.merge.valid", valid);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.merge.merged_count", merged_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.merge.message", message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");

    yyjson_val *outputs = obj_get(action, "outputs");
    if (!yyjson_is_obj(outputs))
        return;
    const char *valid_key = json_string(outputs, "valid_key", NULL);
    const char *message_key = json_string(outputs, "message_key", NULL);
    const char *merged_count_key = json_string(outputs, "merged_count_key", NULL);
    if (valid_key != NULL)
        slayer3d_properties_set_bool(runtime->scene_state, valid_key, valid);
    if (message_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, message_key, message != NULL ? message : "");
    if (merged_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, merged_count_key, merged_count);
}

bool slayer3d_game_data_merge_selected_editor_vertices_to_hover(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    editor_source_vertex_selection target;
    init_editor_source_vertex_selection(&target);
    if (runtime == NULL || !editor_vertex_merge_target_from_action(runtime, action, &target))
    {
        publish_editor_vertex_merge_result(runtime, action, false, 0, "vertex merge requires a target vertex");
        return true;
    }

    int vertex_indices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    SDL_zeroa(vertex_indices);
    const int vertex_index_count =
        editor_collect_vertex_merge_indices(runtime, &target, vertex_indices, SDL_arraysize(vertex_indices));
    if (vertex_index_count <= 0)
    {
        publish_editor_vertex_merge_result(runtime, action, false, 0,
                                           "vertex merge requires selected vertices in the target source brush");
        return true;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, target.world_name);
    if (world_runtime == NULL || target.source_index < 0 ||
        target.source_index >= world_runtime->editor_source_box_count)
    {
        publish_editor_vertex_merge_result(runtime, action, false, 0, "vertex merge source brush not found");
        return true;
    }
    const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[target.source_index];
    const char *identity = box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;

    editor_brush_source_vertex_operation_desc desc;
    SDL_zero(desc);
    desc.brush_identity = identity;
    desc.type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MERGE_MANY_TO_TARGET;
    desc.target_vertex_index = target.vertex_index;
    desc.vertex_index_count = vertex_index_count;
    for (int i = 0; i < vertex_index_count; ++i)
        desc.vertex_indices[i] = vertex_indices[i];

    char error[256];
    SDL_zeroa(error);
    editor_brush_source_vertex_operation_result result;
    SDL_zero(result);
    if (!editor_brush_world_apply_source_vertex_operation(world_runtime, &desc, &result, error, sizeof(error)))
    {
        char message[320];
        SDL_snprintf(message, sizeof(message), "vertex merge blocked: %s",
                     error[0] != '\0' ? error : "invalid source edit");
        publish_editor_vertex_merge_result(runtime, action, false, 0, message);
        editor_brush_source_free_runtime_brush(&result.brush);
        return true;
    }
    const int merged_count = result.changed_count;
    editor_brush_source_free_runtime_brush(&result.brush);

    editor_refresh_selected_vertices_after_source_edit(runtime, world_runtime);
    editor_refresh_selected_brushes_after_source_edit(runtime);
    clear_editor_selected_vertices(runtime);

    char message[160];
    SDL_snprintf(message, sizeof(message), "merged %d selected source vert%s", merged_count,
                 merged_count == 1 ? "ex" : "ices");
    publish_editor_vertex_merge_result(runtime, action, true, merged_count, message);
    return true;
}

static bool editor_vertex_add_read_coord(const brush_world_runtime *world_runtime, yyjson_val *action, int out_coord[3])
{
    if (world_runtime == NULL || action == NULL || out_coord == NULL)
        return false;

    yyjson_val *coord = obj_get(action, "coord");
    if (yyjson_is_arr(coord) && yyjson_arr_size(coord) == 3)
    {
        for (size_t i = 0; i < 3; ++i)
        {
            yyjson_val *value = yyjson_arr_get(coord, i);
            if (!yyjson_is_int(value))
                return false;
            out_coord[i] = (int)yyjson_get_int(value);
        }
        return true;
    }

    yyjson_val *position = obj_get(action, "position");
    if (yyjson_is_arr(position) && yyjson_arr_size(position) >= 3)
    {
        yyjson_val *x = yyjson_arr_get(position, 0);
        yyjson_val *y = yyjson_arr_get(position, 1);
        yyjson_val *z = yyjson_arr_get(position, 2);
        if (!yyjson_is_num(x) || !yyjson_is_num(y) || !yyjson_is_num(z))
            return false;
        out_coord[0] = editor_source_units_from_meters(world_runtime, (float)yyjson_get_num(x));
        out_coord[1] = editor_source_units_from_meters(world_runtime, (float)yyjson_get_num(y));
        out_coord[2] = editor_source_units_from_meters(world_runtime, (float)yyjson_get_num(z));
        return true;
    }
    return false;
}

static bool editor_vertex_source_from_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                             brush_world_runtime **out_world_runtime, int *out_source_index)
{
    if (out_world_runtime != NULL)
        *out_world_runtime = NULL;
    if (out_source_index != NULL)
        *out_source_index = -1;
    if (runtime == NULL || out_world_runtime == NULL || out_source_index == NULL)
        return false;

    const char *world_name = json_string(action, "world", NULL);
    if ((world_name == NULL || world_name[0] == '\0') && editor_selected_vertices_active_for_scene(runtime) &&
        runtime->editor_selected_vertex_count > 0)
    {
        world_name = runtime->editor_selected_vertices[0].world_name;
    }
    if ((world_name == NULL || world_name[0] == '\0') && editor_selected_brushes_active_for_scene(runtime) &&
        runtime->editor_selected_brush_count > 0)
    {
        world_name = runtime->editor_selected_brushes[0].world_name;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    if (world_runtime == NULL)
        return false;

    int source_index = -1;
    const char *brush_identity = json_string(action, "brush_stable_id", NULL);
    if (brush_identity == NULL || brush_identity[0] == '\0')
        brush_identity = json_string(action, "brush", NULL);
    if (brush_identity != NULL && brush_identity[0] != '\0')
        source_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
    if (source_index < 0 && editor_selected_vertices_active_for_scene(runtime) &&
        runtime->editor_selected_vertex_count > 0 &&
        SDL_strcmp(runtime->editor_selected_vertices[0].world_name, world_runtime->desc.name) == 0)
    {
        source_index = runtime->editor_selected_vertices[0].source_index;
    }
    if (source_index < 0 && editor_selected_brushes_active_for_scene(runtime) &&
        runtime->editor_selected_brush_count > 0 &&
        SDL_strcmp(runtime->editor_selected_brushes[0].world_name, world_runtime->desc.name) == 0)
    {
        const slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[0]);
        source_index = editor_source_index_for_selection(world_runtime, &selection);
    }
    if (source_index < 0 || source_index >= world_runtime->editor_source_box_count)
        return false;

    *out_world_runtime = world_runtime;
    *out_source_index = source_index;
    return true;
}

static void publish_editor_vertex_add_result(slayer3d_game_data_runtime *runtime, yyjson_val *action, bool valid,
                                             int vertex_count, int added_count, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.add.valid", valid);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.add.vertex_count", vertex_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.add.added_count", added_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.add.message", message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");

    yyjson_val *outputs = obj_get(action, "outputs");
    if (!yyjson_is_obj(outputs))
        return;
    const char *valid_key = json_string(outputs, "valid_key", NULL);
    const char *message_key = json_string(outputs, "message_key", NULL);
    const char *vertex_count_key = json_string(outputs, "vertex_count_key", NULL);
    const char *added_count_key = json_string(outputs, "added_count_key", NULL);
    if (valid_key != NULL)
        slayer3d_properties_set_bool(runtime->scene_state, valid_key, valid);
    if (message_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, message_key, message != NULL ? message : "");
    if (vertex_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, vertex_count_key, vertex_count);
    if (added_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, added_count_key, added_count);
}

static bool editor_add_vertex_to_source_coord(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              brush_world_runtime *world_runtime, int source_index, const int coord[3])
{
    if (runtime == NULL || world_runtime == NULL || coord == NULL || source_index < 0 ||
        source_index >= world_runtime->editor_source_box_count)
    {
        publish_editor_vertex_add_result(runtime, action, false, 0, 0, "vertex add requires a source brush");
        return true;
    }

    const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    const char *identity = box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;
    editor_brush_source_vertex_operation_desc desc;
    SDL_zero(desc);
    desc.brush_identity = identity;
    desc.type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_ADD;
    for (int axis = 0; axis < 3; ++axis)
        desc.coord[axis] = coord[axis];

    char error[256];
    SDL_zeroa(error);
    editor_brush_source_vertex_operation_result result;
    SDL_zero(result);
    if (!editor_brush_world_apply_source_vertex_operation(world_runtime, &desc, &result, error, sizeof(error)))
    {
        char message[320];
        SDL_snprintf(message, sizeof(message), "vertex add blocked: %s",
                     error[0] != '\0' ? error : "invalid source edit");
        publish_editor_vertex_add_result(runtime, action, false, 0, 0, message);
        editor_brush_source_free_runtime_brush(&result.brush);
        return true;
    }
    const int vertex_count = result.vertex_count;
    editor_brush_source_free_runtime_brush(&result.brush);

    editor_brush_source_vertex_model model;
    if (editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
    {
        clear_editor_selected_vertices(runtime);
        for (int i = 0; i < model.vertex_count; ++i)
        {
            if (model.vertices[i].coord[0] != coord[0] || model.vertices[i].coord[1] != coord[1] ||
                model.vertices[i].coord[2] != coord[2])
            {
                continue;
            }
            editor_source_vertex_selection selection;
            if (editor_source_vertex_selection_from_model(world_runtime, &model, i, &selection))
                (void)add_editor_selected_vertex(runtime, &selection);
            break;
        }
        publish_editor_selected_vertex_count(runtime);
    }
    editor_refresh_selected_brushes_after_source_edit(runtime);

    char message[160];
    SDL_snprintf(message, sizeof(message), "added source vertex");
    publish_editor_vertex_add_result(runtime, action, true, vertex_count, 1, message);
    return true;
}

bool slayer3d_game_data_add_editor_vertex_to_source(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    brush_world_runtime *world_runtime = NULL;
    int source_index = -1;
    if (!editor_vertex_source_from_action(runtime, action, &world_runtime, &source_index))
    {
        publish_editor_vertex_add_result(runtime, action, false, 0, 0, "vertex add requires a source brush");
        return true;
    }

    int coord[3] = {0, 0, 0};
    if (!editor_vertex_add_read_coord(world_runtime, action, coord))
    {
        publish_editor_vertex_add_result(runtime, action, false, 0, 0, "vertex add requires a coordinate");
        return true;
    }

    return editor_add_vertex_to_source_coord(runtime, action, world_runtime, source_index, coord);
}

static brush_world_runtime *editor_vertex_diagnostics_world_from_action(slayer3d_game_data_runtime *runtime,
                                                                        yyjson_val *action)
{
    if (runtime == NULL)
        return NULL;

    const char *world_name = json_string(action, "world", NULL);
    if ((world_name == NULL || world_name[0] == '\0') && editor_selected_vertices_active_for_scene(runtime) &&
        runtime->editor_selected_vertex_count > 0)
    {
        world_name = runtime->editor_selected_vertices[0].world_name;
    }
    if ((world_name == NULL || world_name[0] == '\0') && editor_selected_brushes_active_for_scene(runtime) &&
        runtime->editor_selected_brush_count > 0)
    {
        world_name = runtime->editor_selected_brushes[0].world_name;
    }
    if (world_name != NULL && world_name[0] != '\0')
        return find_brush_world_runtime_mutable(runtime, world_name);

    for (int i = 0; i < runtime->brush_world_count; ++i)
    {
        if (runtime->brush_worlds[i].editor_has_source_model)
            return &runtime->brush_worlds[i];
    }
    return NULL;
}

static int editor_collect_vertex_diagnostic_targets(slayer3d_game_data_runtime *runtime,
                                                    brush_world_runtime *world_runtime,
                                                    editor_source_snap_target *targets, int target_capacity)
{
    if (runtime == NULL || world_runtime == NULL || targets == NULL || target_capacity <= 0)
        return 0;

    int target_count = 0;
    if (editor_selected_vertices_active_for_scene(runtime))
    {
        for (int i = 0; i < runtime->editor_selected_vertex_count && target_count < target_capacity; ++i)
        {
            const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[i];
            if (SDL_strcmp(selection->world_name, world_runtime->desc.name) == 0)
                (void)editor_add_source_snap_target(targets, &target_count, world_runtime, selection->source_index);
        }
    }
    if (target_count > 0)
        return target_count;

    if (editor_selected_brushes_active_for_scene(runtime))
    {
        for (int i = 0; i < runtime->editor_selected_brush_count && target_count < target_capacity; ++i)
        {
            const slayer3d_game_data_editor_selection selection =
                resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
            if (SDL_strcmp(selection.world_name, world_runtime->desc.name) != 0)
                continue;
            const int source_index = editor_source_index_for_selection(world_runtime, &selection);
            (void)editor_add_source_snap_target(targets, &target_count, world_runtime, source_index);
        }
    }
    return target_count;
}

static void publish_editor_vertex_diagnostics_result(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                     const brush_world_runtime *world_runtime,
                                                     const editor_brush_source_vertex_diagnostics *diagnostics,
                                                     const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL || diagnostics == NULL)
        return;

    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.diagnostics.valid", diagnostics->valid);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.diagnostics.message",
                                   message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.diagnostics.world",
                                   world_runtime != NULL && world_runtime->desc.name != NULL ? world_runtime->desc.name
                                                                                             : "");
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.brush_count",
                                diagnostics->brush_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.vertex_count",
                                diagnostics->vertex_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.edge_count", diagnostics->edge_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.face_count", diagnostics->face_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.shared_vertex_count",
                                diagnostics->shared_vertex_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.off_snap_count",
                                diagnostics->off_snap_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.degenerate_count",
                                diagnostics->degenerate_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.concave_count",
                                diagnostics->concave_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.diagnostics.non_finite_count",
                                diagnostics->non_finite_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.diagnostics.first_issue",
                                   diagnostics->first_issue);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.diagnostics.first_issue_stable_id",
                                   diagnostics->first_issue_stable_id);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");

    yyjson_val *outputs = obj_get(action, "outputs");
    if (!yyjson_is_obj(outputs))
        return;
    const char *valid_key = json_string(outputs, "valid_key", NULL);
    const char *message_key = json_string(outputs, "message_key", NULL);
    const char *world_key = json_string(outputs, "world_key", NULL);
    const char *brush_count_key = json_string(outputs, "brush_count_key", NULL);
    const char *vertex_count_key = json_string(outputs, "vertex_count_key", NULL);
    const char *edge_count_key = json_string(outputs, "edge_count_key", NULL);
    const char *face_count_key = json_string(outputs, "face_count_key", NULL);
    const char *shared_vertex_count_key = json_string(outputs, "shared_vertex_count_key", NULL);
    const char *off_snap_count_key = json_string(outputs, "off_snap_count_key", NULL);
    const char *degenerate_count_key = json_string(outputs, "degenerate_count_key", NULL);
    const char *concave_count_key = json_string(outputs, "concave_count_key", NULL);
    const char *non_finite_count_key = json_string(outputs, "non_finite_count_key", NULL);
    const char *first_issue_key = json_string(outputs, "first_issue_key", NULL);
    const char *first_issue_stable_id_key = json_string(outputs, "first_issue_stable_id_key", NULL);

    if (valid_key != NULL)
        slayer3d_properties_set_bool(runtime->scene_state, valid_key, diagnostics->valid);
    if (message_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, message_key, message != NULL ? message : "");
    if (world_key != NULL)
        slayer3d_properties_set_string(
            runtime->scene_state, world_key,
            world_runtime != NULL && world_runtime->desc.name != NULL ? world_runtime->desc.name : "");
    if (brush_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, brush_count_key, diagnostics->brush_count);
    if (vertex_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, vertex_count_key, diagnostics->vertex_count);
    if (edge_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, edge_count_key, diagnostics->edge_count);
    if (face_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, face_count_key, diagnostics->face_count);
    if (shared_vertex_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, shared_vertex_count_key, diagnostics->shared_vertex_count);
    if (off_snap_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, off_snap_count_key, diagnostics->off_snap_count);
    if (degenerate_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, degenerate_count_key, diagnostics->degenerate_count);
    if (concave_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, concave_count_key, diagnostics->concave_count);
    if (non_finite_count_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, non_finite_count_key, diagnostics->non_finite_count);
    if (first_issue_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, first_issue_key, diagnostics->first_issue);
    if (first_issue_stable_id_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, first_issue_stable_id_key,
                                       diagnostics->first_issue_stable_id);
}

bool slayer3d_game_data_validate_editor_vertex_source(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    editor_brush_source_vertex_diagnostics diagnostics;
    SDL_zero(diagnostics);

    brush_world_runtime *world_runtime = editor_vertex_diagnostics_world_from_action(runtime, action);
    if (runtime == NULL || world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        SDL_strlcpy(diagnostics.first_issue, "source vertex diagnostics require an editable brush world",
                    sizeof(diagnostics.first_issue));
        publish_editor_vertex_diagnostics_result(runtime, action, world_runtime, &diagnostics, diagnostics.first_issue);
        return true;
    }

    int explicit_source_index = -1;
    const char *brush_identity = json_string(action, "brush_stable_id", NULL);
    if (brush_identity == NULL || brush_identity[0] == '\0')
        brush_identity = json_string(action, "brush", NULL);
    if (brush_identity != NULL && brush_identity[0] != '\0')
    {
        explicit_source_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
        if (explicit_source_index < 0)
        {
            SDL_strlcpy(diagnostics.first_issue, "source brush not found", sizeof(diagnostics.first_issue));
            publish_editor_vertex_diagnostics_result(runtime, action, world_runtime, &diagnostics,
                                                     diagnostics.first_issue);
            return true;
        }
    }

    editor_source_snap_target targets[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(targets);
    int source_indices[SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY];
    SDL_zeroa(source_indices);
    int source_index_count = 0;
    if (explicit_source_index >= 0)
    {
        source_indices[source_index_count++] = explicit_source_index;
    }
    else
    {
        source_index_count =
            editor_collect_vertex_diagnostic_targets(runtime, world_runtime, targets, SDL_arraysize(targets));
        for (int i = 0; i < source_index_count; ++i)
            source_indices[i] = targets[i].source_index;
    }

    char error[256];
    SDL_zeroa(error);
    const int *indices = source_index_count > 0 ? source_indices : NULL;
    const int index_count = source_index_count > 0 ? source_index_count : 0;
    if (!editor_brush_world_validate_source_vertex_model(world_runtime, indices, index_count, &diagnostics, error,
                                                         sizeof(error)))
    {
        if (diagnostics.first_issue[0] == '\0')
            SDL_strlcpy(diagnostics.first_issue, error[0] != '\0' ? error : "source vertex diagnostics failed",
                        sizeof(diagnostics.first_issue));
    }

    const char *message = diagnostics.valid                    ? "source vertex model valid"
                          : diagnostics.first_issue[0] != '\0' ? diagnostics.first_issue
                                                               : "source vertex model invalid";
    publish_editor_vertex_diagnostics_result(runtime, action, world_runtime, &diagnostics, message);
    return true;
}

static bool editor_try_merge_selected_vertices_to_hover(slayer3d_game_data_runtime *runtime,
                                                        const slayer3d_game_data_editor_selection *hover_selection)
{
    if (runtime == NULL || hover_selection == NULL || !editor_selected_vertices_active_for_scene(runtime) ||
        runtime->editor_selected_vertex_count <= 0)
    {
        return false;
    }

    editor_source_vertex_selection target;
    init_editor_source_vertex_selection(&target);
    if (!editor_hover_source_vertex_selection(runtime, hover_selection, &target))
        return false;
    if (editor_selected_vertex_index(runtime, &target) >= 0)
        return false;

    return slayer3d_game_data_merge_selected_editor_vertices_to_hover(runtime, NULL);
}

static void editor_begin_vertex_drag(slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                     const slayer3d_game_data_editor_selection *hover_selection)
{
    if (runtime == NULL || input == NULL || hover_selection == NULL)
        return;
    editor_drag_move_state *drag = &runtime->editor_drag_move;
    SDL_zero(*drag);
    drag->active = true;
    drag->scene = slayer3d_game_data_active_scene(runtime);
    drag->grid_size = slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f);
    drag->start_point = hover_selection->hit ? hover_selection->point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    (void)slayer3d_input_get_mouse_position(input, &drag->start_mouse_x, &drag->start_mouse_y);
}

static bool editor_handle_vertex_drag(slayer3d_game_data_runtime *runtime,
                                      const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_vertex(runtime))
        return true;

    editor_drag_move_state *drag = &runtime->editor_drag_move;
    if (!drag->active || drag->face_resize || drag->vertex_lasso)
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return true;
    if (out_consumed != NULL)
        *out_consumed = true;

    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    float mouse_x = drag->current_mouse_x;
    float mouse_y = drag->current_mouse_y;
    (void)slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y);
    drag->current_mouse_x = mouse_x;
    drag->current_mouse_y = mouse_y;
    if (left_down)
    {
        const SDL_Keymod modifiers = SDL_GetModState();
        const bool y_axis_lock = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0;
        slayer3d_vec3 desired = drag->applied_offset;
        if (y_axis_lock)
        {
            const float units_per_pixel = SDL_max(drag->grid_size, 0.001f) / 48.0f;
            desired = slayer3d_vec3_make(
                0.0f, editor_snap_delta((drag->start_mouse_y - mouse_y) * units_per_pixel, drag->grid_size), 0.0f);
        }
        else if (hover_selection != NULL && hover_selection->hit)
        {
            desired = slayer3d_vec3_make(
                editor_snap_delta(hover_selection->point.x - drag->start_point.x, drag->grid_size), 0.0f,
                editor_snap_delta(hover_selection->point.z - drag->start_point.z, drag->grid_size));
        }

        const slayer3d_vec3 incremental = slayer3d_vec3_sub(desired, drag->applied_offset);
        if (slayer3d_vec3_length_squared(incremental) > 0.0000001f &&
            editor_translate_selected_vertices(runtime, incremental))
        {
            drag->applied_offset = desired;
            drag->moved = true;
        }
    }

    if (left_released || !left_down)
    {
        const float dx = drag->current_mouse_x - drag->start_mouse_x;
        const float dy = drag->current_mouse_y - drag->start_mouse_y;
        const bool meaningful_drag = dx * dx + dy * dy >= 16.0f;
        if (meaningful_drag)
            (void)editor_try_merge_selected_vertices_to_hover(runtime, hover_selection);
        clear_editor_drag_move(runtime);
    }
    return true;
}

static void editor_set_face_drag_state(slayer3d_game_data_runtime *runtime, bool ready, bool dragging)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.face.drag.ready", ready);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.face.drag.active", dragging);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.face.drag.shift",
                                 (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
}

static void editor_set_face_resize_preview(slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_editor_selection *selection, float distance)
{
    if (runtime == NULL || selection == NULL || !selection->hit || selection->face_index < 0)
        return;

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
    preview->has_bounds = selection->has_bounds;
    preview->bounds = selection->has_bounds
                          ? editor_resized_preview_bounds(selection->bounds, selection->normal, distance)
                          : (slayer3d_bounding_box){selection->point, selection->point};
}

static bool editor_handle_face_drag(slayer3d_game_data_runtime *runtime,
                                    const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_face(runtime))
    {
        editor_set_face_drag_state(runtime, false, false);
        return true;
    }

    const bool can_resize_face = editor_selection_is_selectable_brush(hover_selection) &&
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
        editor_set_face_resize_preview(runtime, &drag->face_selection, distance);
        drag->moved = true;
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

static bool editor_handle_prefabs_widget(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    yyjson_val *widget = obj_get(editor, "prefabs_widget");
    if (runtime == NULL || runtime->scene_state == NULL || !yyjson_is_obj(widget))
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (input == NULL || !slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        return true;

    const bool over_button = editor_mouse_in_rect(mouse_x, mouse_y, obj_get(widget, "button"));
    if (out_consumed != NULL)
        *out_consumed = over_button;
    if (!over_button || !slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT))
        return true;

    const char *active_key = json_string(widget, "active_key", "editor.palette.active");
    const char *cursor_key = json_string(widget, "cursor_key", "editor.palette.brush.cursor");
    const char *active_value = json_string(widget, "active_value", "brush");
    const char *default_cursor = json_string(widget, "default_cursor", "floor");
    slayer3d_properties_set_string(runtime->scene_state, active_key, active_value);
    slayer3d_properties_set_string(runtime->scene_state, cursor_key, default_cursor);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "prefabs palette opened");
    return true;
}

static bool editor_handle_tool_mode_buttons(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    yyjson_val *buttons = obj_get(editor, "tool_mode_buttons");
    if (runtime == NULL || runtime->scene_state == NULL || !yyjson_is_arr(buttons))
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (input == NULL || !slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        return true;

    const bool clicked = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    for (size_t i = 0, count = yyjson_arr_size(buttons); i < count; ++i)
    {
        yyjson_val *button = yyjson_arr_get(buttons, i);
        if (!yyjson_is_obj(button) || !editor_mouse_in_rect(mouse_x, mouse_y, obj_get(button, "button")))
            continue;
        if (out_consumed != NULL)
            *out_consumed = true;
        if (!clicked)
            return true;

        const char *mode = json_string(button, "mode", NULL);
        const char *tool_mode = json_string(button, "tool_mode", mode);
        if (mode == NULL || mode[0] == '\0' || tool_mode == NULL || tool_mode[0] == '\0')
            return true;
        slayer3d_properties_set_string(runtime->scene_state, "editor.mode", mode);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.mode", tool_mode);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                       json_string(button, "message", "tool mode"));
        clear_editor_command_preview(runtime);
        return true;
    }
    return true;
}

static bool editor_handle_drag_move(slayer3d_game_data_runtime *runtime,
                                    const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !editor_mode_is_select(runtime))
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
    const bool can_start_from_hover = editor_selection_is_selectable_brush(hover_selection);
    const bool can_start_y_axis_drag =
        y_axis_lock && editor_selected_brushes_active_for_scene(runtime) && runtime->editor_selected_brush_count > 0;
    bool just_started_without_consuming_click = false;
    if (!drag->active && left_pressed && (can_start_from_hover || can_start_y_axis_drag))
    {
        const bool hover_was_selected =
            can_start_from_hover && editor_hover_is_selected_brush(runtime, hover_selection);
        const bool consume_start_click = hover_was_selected || can_start_y_axis_drag;
        SDL_zero(*drag);
        drag->active = true;
        drag->axis_lock_y = y_axis_lock;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        const slayer3d_game_data_editor_selection resolved = resolved_editor_selection(
            runtime, can_start_from_hover ? hover_selection : &runtime->editor_active_selection);
        drag->start_point = resolved.hit ? resolved.point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        drag->grid_size = slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f);
        (void)slayer3d_input_get_mouse_position(input, &drag->start_mouse_x, &drag->start_mouse_y);
        if (out_consumed != NULL)
            *out_consumed = consume_start_click;
        just_started_without_consuming_click = !consume_start_click;
    }

    if (!drag->active)
        return true;
    if (just_started_without_consuming_click)
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
    if (!editor_mode_is_select(runtime) && !vertex_mode)
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    const float grid_size = slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 16.0f);
    if (input == NULL || grid_size <= 0.0f)
        return true;

    const SDL_Keymod modifiers = SDL_GetModState();
    const bool transform_modifier = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0;
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
                             resolved.hit ? editor_metadata_stable_id(resolved.world_editor) : "");
    editor_set_string_output(scene_state, outputs, "element_stable_id_key",
                             resolved.hit ? editor_metadata_stable_id(resolved.element_editor) : "");
    editor_set_string_output(scene_state, outputs, "material_stable_id_key",
                             resolved.hit ? editor_metadata_stable_id(resolved.material_editor) : "");
    editor_set_string_output(scene_state, outputs, "face_stable_id_key",
                             resolved.hit ? editor_metadata_stable_id(resolved.face_editor) : "");
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

bool slayer3d_game_data_clear_editor_vertex_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    clear_editor_selected_vertices(runtime);
    publish_editor_selected_vertex_count(runtime);
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

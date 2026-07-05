#include "game_data_internal.h"

#include <SDL3/SDL_keyboard.h>

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

static const char *editor_drag_create_phase_name(editor_drag_create_phase phase)
{
    switch (phase)
    {
    case EDITOR_DRAG_CREATE_DRAWING_FOOTPRINT:
        return "drawing_footprint";
    case EDITOR_DRAG_CREATE_PENDING_FOOTPRINT:
        return "pending_footprint";
    case EDITOR_DRAG_CREATE_ADJUSTING_DEPTH:
        return "adjusting_depth";
    case EDITOR_DRAG_CREATE_COMMITTED:
        return "committed";
    case EDITOR_DRAG_CREATE_CANCELED:
        return "canceled";
    case EDITOR_DRAG_CREATE_IDLE:
    default:
        return "idle";
    }
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

static slayer3d_vec3 editor_preview_dimensions(const editor_placement_preview_state *preview)
{
    if (preview == NULL || !preview->has_bounds)
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    return slayer3d_vec3_make(SDL_max(0.0f, preview->bounds.max.x - preview->bounds.min.x),
                              SDL_max(0.0f, preview->bounds.max.y - preview->bounds.min.y),
                              SDL_max(0.0f, preview->bounds.max.z - preview->bounds.min.z));
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
    const slayer3d_vec3 dimensions = valid ? editor_preview_dimensions(preview) : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    editor_set_vec3_output(scene_state, outputs, "dimensions_key", dimensions);
    editor_set_float_output(scene_state, outputs, "dimension_x_key", dimensions.x);
    editor_set_float_output(scene_state, outputs, "dimension_y_key", dimensions.y);
    editor_set_float_output(scene_state, outputs, "dimension_z_key", dimensions.z);
    editor_set_string_output(scene_state, outputs, "warning_key",
                             valid && preview != NULL ? preview->source_warning : "");
    editor_set_string_output(scene_state, outputs, "state_key",
                             runtime != NULL ? editor_drag_create_phase_name(runtime->editor_drag_create.phase)
                                             : "idle");
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

static int editor_drag_extrusion_axis(const editor_drag_create_state *drag)
{
    if (drag == NULL || drag->extrusion_axis < 0 || drag->extrusion_axis > 2)
        return 1;
    return drag->extrusion_axis;
}

static const char *editor_drag_axis_name(int axis)
{
    switch (axis)
    {
    case 0:
        return "x";
    case 2:
        return "z";
    case 1:
    default:
        return "y";
    }
}

static bool editor_drag_axis_from_name(const char *name, int *out_axis)
{
    if (name == NULL || out_axis == NULL)
        return false;
    if (SDL_strcmp(name, "x") == 0)
        *out_axis = 0;
    else if (SDL_strcmp(name, "y") == 0)
        *out_axis = 1;
    else if (SDL_strcmp(name, "z") == 0)
        *out_axis = 2;
    else
        return false;
    return true;
}

static int editor_drag_axis_from_normal(slayer3d_vec3 normal)
{
    normal.x = SDL_fabsf(normal.x);
    normal.y = SDL_fabsf(normal.y);
    normal.z = SDL_fabsf(normal.z);
    if (normal.x >= normal.y && normal.x >= normal.z)
        return 0;
    if (normal.z >= normal.y)
        return 2;
    return 1;
}

static float editor_drag_normal_component(slayer3d_vec3 normal, int axis)
{
    switch (axis)
    {
    case 0:
        return normal.x;
    case 2:
        return normal.z;
    case 1:
    default:
        return normal.y;
    }
}

static int editor_drag_extrusion_axis_from_work_plane(slayer3d_game_data_runtime *runtime, yyjson_val *editor,
                                                      yyjson_val *drag_json)
{
    int axis = 1;
    const char *authored_axis = json_string(drag_json, "extrusion_axis", NULL);
    if (editor_drag_axis_from_name(authored_axis, &axis))
        return axis;

    const char *axis_key = json_string(drag_json, "extrusion_axis_key", NULL);
    if (runtime != NULL && axis_key != NULL && axis_key[0] != '\0' &&
        editor_drag_axis_from_name(
            slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), axis_key, ""), &axis))
    {
        return axis;
    }

    yyjson_val *selection_json = obj_get(editor, "selection");
    slayer3d_vec3 work_plane_normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    float work_plane_distance = 0.0f;
    if (editor_work_plane_desc_from_trace_json(runtime, obj_get(selection_json, "trace"), &work_plane_normal,
                                               &work_plane_distance))
    {
        axis = editor_drag_axis_from_normal(work_plane_normal);
    }
    return axis;
}

static bool editor_drag_selection_can_start(const slayer3d_game_data_editor_selection *selection)
{
    return selection != NULL && selection->hit &&
           (selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID ||
            selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD);
}

static bool editor_drag_mode_supports_creation(const char *mode)
{
    return SDL_strcmp(mode, "select") == 0 || SDL_strcmp(mode, "brush") == 0;
}

static bool editor_drag_selection_can_start_for_mode(const char *mode,
                                                     const slayer3d_game_data_editor_selection *selection)
{
    if (selection == NULL || !selection->hit)
        return false;

    if (SDL_strcmp(mode, "select") == 0)
    {
        /* Select mode mirrors TrenchBroom's default draw-shape behavior: empty/work-plane drags
           can create a cuboid, but brush-face drags remain available for selection/move tools. */
        return selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID;
    }

    if (SDL_strcmp(mode, "brush") == 0)
    {
        /* Brush Tool mirrors TrenchBroom's assemble-brush behavior: an existing brush face is a
           valid reference surface for drawing a secondary brush footprint. Empty work-plane drags
           remain valid for the current simple blockout workflow. */
        return editor_drag_selection_can_start(selection);
    }

    return false;
}

static int editor_drag_extrusion_axis_from_selection(slayer3d_game_data_runtime *runtime, yyjson_val *editor,
                                                     yyjson_val *drag_json,
                                                     const slayer3d_game_data_editor_selection *selection)
{
    if (selection != NULL && selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD &&
        slayer3d_vec3_length_squared(selection->normal) > 0.000001f)
    {
        return editor_drag_axis_from_normal(selection->normal);
    }
    return editor_drag_extrusion_axis_from_work_plane(runtime, editor, drag_json);
}

static int editor_drag_initial_depth_cells(const slayer3d_game_data_editor_selection *selection, int extrusion_axis)
{
    if (selection != NULL && selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
    {
        const float normal_component = editor_drag_normal_component(selection->normal, extrusion_axis);
        if (normal_component < -0.0001f)
            return -1;
    }
    return 1;
}

static int editor_drag_depth_cells(const editor_drag_create_state *drag)
{
    if (drag == NULL || drag->depth_cells == 0)
        return 1;
    return drag->depth_cells;
}

static void editor_drag_source_bounds(const brush_world_runtime *world_runtime, const editor_drag_create_state *drag,
                                      int out_min[3], int out_max[3])
{
    const int extrusion_axis = editor_drag_extrusion_axis(drag);
    const int depth_cells = editor_drag_depth_cells(drag);
    int min_cell[3] = {0, 0, 0};
    int max_cell[3] = {0, 0, 0};
    for (int axis = 0; axis < 3; ++axis)
    {
        if (axis == extrusion_axis)
        {
            min_cell[axis] = SDL_min(drag->start_cell[axis], drag->start_cell[axis] + depth_cells);
            max_cell[axis] = SDL_max(drag->start_cell[axis], drag->start_cell[axis] + depth_cells);
        }
        else
        {
            min_cell[axis] = SDL_min(drag->start_cell[axis], drag->current_cell[axis]);
            max_cell[axis] = SDL_max(drag->start_cell[axis], drag->current_cell[axis]) + 1;
        }
        out_min[axis] = editor_source_units_from_meters_public(world_runtime, (float)min_cell[axis] * drag->grid_size);
        out_max[axis] = editor_source_units_from_meters_public(world_runtime, (float)max_cell[axis] * drag->grid_size);
    }
}

static void editor_drag_update_current_cell(editor_drag_create_state *drag,
                                            const slayer3d_game_data_editor_selection *hover_selection)
{
    if (drag == NULL || hover_selection == NULL || !hover_selection->hit)
        return;

    const int extrusion_axis = editor_drag_extrusion_axis(drag);
    const int next_cell[3] = {
        extrusion_axis == 0 ? drag->start_cell[0] : editor_drag_cell(hover_selection->point.x, drag->grid_size),
        extrusion_axis == 1 ? drag->start_cell[1] : editor_drag_cell(hover_selection->point.y, drag->grid_size),
        extrusion_axis == 2 ? drag->start_cell[2] : editor_drag_cell(hover_selection->point.z, drag->grid_size)};
    if (drag->current_cell[0] != next_cell[0] || drag->current_cell[1] != next_cell[1] ||
        drag->current_cell[2] != next_cell[2])
    {
        drag->moved = true;
    }
    drag->current_cell[0] = next_cell[0];
    drag->current_cell[1] = next_cell[1];
    drag->current_cell[2] = next_cell[2];
}

static float editor_drag_mouse_y(slayer3d_input_manager *input, float fallback)
{
    float mouse_x = 0.0f;
    float mouse_y = fallback;
    return input != NULL && slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y) ? mouse_y : fallback;
}

static void editor_drag_set_adjusting_depth(editor_drag_create_state *drag, slayer3d_input_manager *input)
{
    if (drag == NULL)
        return;
    drag->depth_drag_start_mouse_y = editor_drag_mouse_y(input, 0.0f);
    drag->depth_drag_start_cell = editor_drag_depth_cells(drag);
    drag->phase = EDITOR_DRAG_CREATE_ADJUSTING_DEPTH;
}

static void editor_drag_update_depth(editor_drag_create_state *drag, slayer3d_input_manager *input)
{
    if (drag == NULL || input == NULL)
        return;
    const float mouse_y = editor_drag_mouse_y(input, drag->depth_drag_start_mouse_y);
    const float units_per_pixel = SDL_max(drag->grid_size, 0.001f) / 48.0f;
    const float meters_delta = (drag->depth_drag_start_mouse_y - mouse_y) * units_per_pixel;
    const int cell_delta = (int)SDL_lroundf(meters_delta / SDL_max(drag->grid_size, 0.001f));
    const int depth = drag->depth_drag_start_cell + cell_delta;
    drag->depth_cells = depth != 0 ? depth : (cell_delta < 0 ? -1 : 1);
}

static bool editor_drag_preview_source_box(slayer3d_game_data_runtime *runtime, yyjson_val *drag_json,
                                           editor_drag_create_state *drag,
                                           editor_brush_source_prefab_result *out_result);
static void editor_drag_publish_preview(slayer3d_game_data_runtime *runtime, yyjson_val *placement,
                                        yyjson_val *drag_json, editor_drag_create_state *drag,
                                        const editor_brush_source_prefab_result *result, const char *message,
                                        bool valid);

static bool editor_drag_select_created_brush(slayer3d_game_data_runtime *runtime, const char *world_name,
                                             const char *brush_name)
{
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || brush_name == NULL || brush_name[0] == '\0')
    {
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    const slayer3d_game_data_brush_world *world = world_runtime != NULL ? &world_runtime->desc : NULL;
    if (world == NULL)
        return false;

    for (int i = 0; i < world->brush_count; ++i)
    {
        if (world->brushes[i].name == NULL || SDL_strcmp(world->brushes[i].name, brush_name) != 0)
            continue;

        slayer3d_game_data_editor_selection selection;
        if (!editor_selection_from_brush_index(runtime, world_name, i, -1, &selection))
            return false;
        selection = resolved_editor_selection(runtime, &selection);
        clear_editor_selected_brushes(runtime);
        if (!add_editor_selected_brush(runtime, &selection))
            return false;
        update_active_editor_selection_from_selected_brushes(runtime);

        yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
        publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
        return true;
    }
    return false;
}

static const char *editor_shape_prefab_override(const char *shape)
{
    if (shape == NULL || shape[0] == '\0' || SDL_strcmp(shape, "cuboid") == 0 || SDL_strcmp(shape, "box") == 0 ||
        SDL_strcmp(shape, "editor.box") == 0)
    {
        return NULL;
    }
    return shape;
}

static bool editor_drag_commit_source_box(slayer3d_game_data_runtime *runtime, yyjson_val *placement,
                                          yyjson_val *drag_json, editor_drag_create_state *drag,
                                          editor_brush_source_prefab_result *preview_result)
{
    if (runtime == NULL || drag_json == NULL || drag == NULL)
        return false;

    editor_brush_source_prefab_result result;
    SDL_zero(result);
    if (preview_result == NULL)
        preview_result = &result;
    if (!editor_drag_preview_source_box(runtime, drag_json, drag, preview_result))
    {
        const char *message =
            preview_result->warning[0] != '\0' ? preview_result->warning : "Invalid brush: preview cannot be created";
        editor_drag_publish_preview(runtime, placement, drag_json, drag, preview_result, message, false);
        editor_publish_console_message(runtime, message);
        return false;
    }

    const char *shape_prefab = editor_shape_prefab_override(drag->shape);
    if (shape_prefab == NULL)
        shape_prefab = json_string(drag_json, "prefab", "editor.box");
    if (!slayer3d_game_data_create_editor_source_box_brush(runtime, drag->world_name, drag->material_name,
                                                           drag->contents, drag->source_min, drag->source_max,
                                                           shape_prefab, preview_result))
    {
        const char *message = preview_result->warning[0] != '\0' ? preview_result->warning : "brush create failed";
        editor_drag_publish_preview(runtime, placement, drag_json, drag, preview_result, message, false);
        editor_publish_console_message(runtime, message);
        return false;
    }
    (void)editor_drag_select_created_brush(runtime, drag->world_name, preview_result->brush_name);

    drag->phase = EDITOR_DRAG_CREATE_COMMITTED;
    clear_editor_placement_preview(runtime);
    publish_editor_placement_preview(runtime, obj_get(placement, "outputs"), false, drag_json, NULL, "Brush created");
    editor_publish_console_message(runtime, "Brush created");
    clear_editor_drag_create(runtime);
    return true;
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
    desc.prefab = editor_shape_prefab_override(drag->shape);
    if (desc.prefab == NULL)
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
        preview->axis = editor_drag_axis_name(editor_drag_extrusion_axis(drag));
        preview->shape = drag->shape;
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

static void editor_drag_cancel_pending(slayer3d_game_data_runtime *runtime, yyjson_val *placement,
                                       yyjson_val *drag_json, const char *message)
{
    if (runtime == NULL)
        return;
    runtime->editor_drag_create.phase = EDITOR_DRAG_CREATE_CANCELED;
    clear_editor_placement_preview(runtime);
    publish_editor_placement_preview(runtime, obj_get(placement, "outputs"), false, drag_json, NULL, message);
    clear_editor_drag_create(runtime);
}

bool editor_cancel_pending_brush_preview(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL || !runtime->editor_drag_create.active)
        return false;

    yyjson_val *editor = active_editor_tooling_root(runtime);
    yyjson_val *placement = obj_get(editor, "placement");
    yyjson_val *drag_json = editor_drag_create_json(placement);
    editor_drag_cancel_pending(runtime, placement, drag_json,
                               message != NULL && message[0] != '\0' ? message : "brush preview cancelled");
    return true;
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
    const char *shape_prefab = preview != NULL ? editor_shape_prefab_override(preview->shape) : NULL;
    if (shape_prefab != NULL)
        desc.prefab = shape_prefab;
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
    if (!editor_drag_mode_supports_creation(mode))
    {
        (void)editor_cancel_pending_brush_preview(runtime, "brush preview cancelled");
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
    const bool cancel_pressed = slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_ESCAPE);
    const bool commit_pressed = slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_RETURN) ||
                                slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_KP_ENTER);
    const bool shift_held = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    editor_drag_create_state *drag = &runtime->editor_drag_create;

    if (drag->active && cancel_pressed)
    {
        editor_drag_cancel_pending(runtime, placement, drag_json, "brush preview cancelled");
        if (out_consumed != NULL)
            *out_consumed = true;
        return true;
    }

    if (drag->active && drag->phase == EDITOR_DRAG_CREATE_PENDING_FOOTPRINT && left_pressed)
    {
        editor_drag_cancel_pending(runtime, placement, drag_json, "brush preview replaced");
    }

    if (!drag->active && left_pressed && editor_drag_selection_can_start_for_mode(mode, hover_selection))
    {
        const float grid_size = editor_placement_grid_size(runtime, placement, drag_json,
                                                           json_float(placement, "default_grid_size", 16.0f));
        if (grid_size <= 0.0f)
            return true;
        (void)slayer3d_game_data_clear_active_editor_selection(runtime);
        SDL_zero(*drag);
        drag->active = true;
        drag->commit_on_release = SDL_strcmp(mode, "select") == 0;
        drag->phase = EDITOR_DRAG_CREATE_DRAWING_FOOTPRINT;
        drag->scene = slayer3d_game_data_active_scene(runtime);
        drag->world_name = json_string(drag_json, "world", "brush.editor_shell.target");
        drag->material_name =
            scene_state_string(runtime, json_string(drag_json, "material_key", "editor.brush.material"),
                               json_string(drag_json, "material", "mat.editor.floor"));
        drag->shape = scene_state_string(runtime, json_string(drag_json, "shape_key", "editor.shape.id"),
                                         json_string(drag_json, "prefab", "editor.box"));
        drag->contents = brush_flags_from_json(obj_get(drag_json, "contents"), brush_content_flag_from_string,
                                               SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID);
        drag->grid_size = grid_size;
        drag->extrusion_axis = editor_drag_extrusion_axis_from_selection(runtime, editor, drag_json, hover_selection);
        drag->depth_cells = editor_drag_initial_depth_cells(hover_selection, drag->extrusion_axis);
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

    if (drag->phase == EDITOR_DRAG_CREATE_DRAWING_FOOTPRINT)
        editor_drag_update_current_cell(drag, hover_selection);
    else if (drag->phase == EDITOR_DRAG_CREATE_PENDING_FOOTPRINT && shift_held)
        editor_drag_set_adjusting_depth(drag, input);
    else if (drag->phase == EDITOR_DRAG_CREATE_ADJUSTING_DEPTH)
    {
        if (shift_held)
            editor_drag_update_depth(drag, input);
        else
            drag->phase = EDITOR_DRAG_CREATE_PENDING_FOOTPRINT;
    }

    editor_brush_source_prefab_result result;
    const bool valid = editor_drag_preview_source_box(runtime, drag_json, drag, &result);
    const char *base_message =
        drag->phase == EDITOR_DRAG_CREATE_ADJUSTING_DEPTH     ? "Release Shift to keep depth, Enter to create brush"
        : drag->phase == EDITOR_DRAG_CREATE_PENDING_FOOTPRINT ? "Hold Shift and move mouse to set brush depth"
                                                              : json_string(drag_json, "message", "drag brush preview");
    char message[256];
    if (valid && result.warning[0] != '\0')
        SDL_snprintf(message, sizeof(message), "%s; %s", base_message, result.warning);
    else if (!valid && result.warning[0] != '\0')
        SDL_strlcpy(message, result.warning, sizeof(message));
    else
        SDL_strlcpy(message, base_message, sizeof(message));
    editor_drag_publish_preview(runtime, placement, drag_json, drag, &result, message, valid);

    if ((drag->phase == EDITOR_DRAG_CREATE_PENDING_FOOTPRINT || drag->phase == EDITOR_DRAG_CREATE_ADJUSTING_DEPTH) &&
        commit_pressed)
    {
        (void)editor_drag_commit_source_box(runtime, placement, drag_json, drag, &result);
        return true;
    }

    if (drag->phase == EDITOR_DRAG_CREATE_DRAWING_FOOTPRINT && (left_released || !left_down))
    {
        if (valid && drag->moved)
        {
            if (drag->commit_on_release)
            {
                (void)editor_drag_commit_source_box(runtime, placement, drag_json, drag, &result);
                return true;
            }
            drag->phase = EDITOR_DRAG_CREATE_PENDING_FOOTPRINT;
            editor_drag_publish_preview(runtime, placement, drag_json, drag, &result,
                                        "Hold Shift and move mouse to set brush depth", valid);
        }
        else
        {
            if (drag->moved && message[0] != '\0')
                editor_publish_console_message(runtime, message);
            editor_drag_cancel_pending(runtime, placement, drag_json, "drag cancelled");
        }
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
    preview->shape = scene_state_string(runtime, json_string(preview_json, "shape_key", "editor.shape.id"),
                                        json_string(preview_json, "prefab", "editor.box"));
    preview->world_name = json_string(preview_json, "world", hover_selection->world_name);
    preview->material_name = json_string(preview_json, "material", hover_selection->material_name);
    preview->contents = brush_flags_from_json(obj_get(preview_json, "contents"), brush_content_flag_from_string,
                                              SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID);
    preview->anchor = anchor;
    preview->snap = snap;
    preview->has_bounds = true;
    if (SDL_strcmp(preview->kind, "player_start") == 0 || SDL_strcmp(preview->kind, "actor") == 0)
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

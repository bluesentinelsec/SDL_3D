/**
 * @file game_data_editor_clip_tool.c
 * @brief Modal editor clip-tool state and command entry points.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

static const char *editor_clip_keep_mode_name(editor_brush_source_clip_keep_mode mode)
{
    switch (mode)
    {
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT:
        return "front";
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK:
        return "back";
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH:
        return "both";
    default:
        return "front";
    }
}

static editor_brush_source_clip_keep_mode editor_clip_next_keep_mode(editor_brush_source_clip_keep_mode mode)
{
    switch (mode)
    {
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT:
        return EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK;
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK:
        return EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH;
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH:
    default:
        return EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT;
    }
}

static bool editor_clip_tool_operation_active(const editor_clip_tool_state *tool)
{
    return tool != NULL && (tool->point_count > 0 || tool->dragged_point >= 0);
}

static slayer3d_vec3 editor_clip_tool_work_plane_normal(const editor_clip_tool_state *tool)
{
    if (tool != NULL && tool->has_work_plane_normal &&
        slayer3d_vec3_length_squared(tool->work_plane_normal) > 0.000001f)
    {
        return slayer3d_vec3_normalize(tool->work_plane_normal);
    }
    return slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
}

static void publish_editor_clip_tool_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.clip.active", tool->active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.clip.operation_active",
                                 editor_clip_tool_operation_active(tool));
    slayer3d_properties_set_string(runtime->scene_state, "editor.clip.scene", tool->active ? tool->scene : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.clip.world", tool->active ? tool->world_name : "");
    slayer3d_properties_set_int(runtime->scene_state, "editor.clip.selected_count",
                                tool->active ? tool->selected_brush_count : 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.clip.point_count", tool->active ? tool->point_count : 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.clip.hovered_point",
                                tool->active ? tool->hovered_point : -1);
    slayer3d_properties_set_int(runtime->scene_state, "editor.clip.dragged_point",
                                tool->active ? tool->dragged_point : -1);
    slayer3d_properties_set_string(runtime->scene_state, "editor.clip.keep_mode",
                                   editor_clip_keep_mode_name(tool->keep_mode));
    slayer3d_properties_set_bool(runtime->scene_state, "editor.clip.valid", tool->active ? tool->preview_valid : false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.clip.message", tool->message);
    for (int i = 0; i < SLAYER3D_EDITOR_CLIP_TOOL_MAX_POINTS; ++i)
    {
        char key[64];
        SDL_snprintf(key, sizeof(key), "editor.clip.point%d", i);
        slayer3d_properties_set_vec3(runtime->scene_state, key,
                                     tool->active && i < tool->point_count ? tool->points[i]
                                                                           : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    }
}

void reset_editor_clip_tool_state(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL)
        return;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    SDL_zero(*tool);
    tool->hovered_point = -1;
    tool->dragged_point = -1;
    tool->has_work_plane_normal = false;
    tool->work_plane_normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    tool->keep_mode = EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT;
    SDL_snprintf(tool->message, sizeof(tool->message), "%s", message != NULL ? message : "");
    publish_editor_clip_tool_state(runtime);
}

static int editor_clip_source_snap_units(const slayer3d_game_data_runtime *runtime,
                                         const brush_world_runtime *world_runtime)
{
    const float fallback = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                               ? world_runtime->editor_source_meters_per_unit
                               : 0.001f;
    const float grid_size =
        runtime != NULL && runtime->scene_state != NULL
            ? SDL_max(slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f), fallback)
            : fallback;
    return SDL_max(editor_source_units_from_meters(world_runtime, grid_size), 1);
}

static int editor_clip_snap_source_coord(int coord, int snap_units)
{
    if (snap_units <= 1)
        return coord;
    const int half = snap_units / 2;
    return coord >= 0 ? ((coord + half) / snap_units) * snap_units : ((coord - half) / snap_units) * snap_units;
}

static void editor_clip_snap_source_coord3(int coord[3], int snap_units)
{
    if (coord == NULL)
        return;
    for (int axis = 0; axis < 3; ++axis)
        coord[axis] = editor_clip_snap_source_coord(coord[axis], snap_units);
}

static slayer3d_vec3 editor_clip_point_from_coord(const int coord[3])
{
    return coord != NULL ? slayer3d_vec3_make((float)coord[0], (float)coord[1], (float)coord[2])
                         : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static void editor_clip_coord_from_point(slayer3d_vec3 point, int out_coord[3])
{
    if (out_coord == NULL)
        return;
    out_coord[0] = (int)SDL_roundf(point.x);
    out_coord[1] = (int)SDL_roundf(point.y);
    out_coord[2] = (int)SDL_roundf(point.z);
}

static slayer3d_vec3 editor_clip_world_origin_for_points(const slayer3d_game_data_runtime *runtime,
                                                         const editor_clip_tool_state *tool,
                                                         const slayer3d_game_data_editor_selection *hover_selection)
{
    if (hover_selection != NULL && hover_selection->world_name != NULL && tool != NULL &&
        SDL_strcmp(hover_selection->world_name, tool->world_name) == 0)
    {
        return hover_selection->world_position;
    }

    if (runtime != NULL && tool != NULL && editor_selected_brushes_active_for_scene(runtime))
    {
        for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
        {
            const slayer3d_game_data_editor_selection selection =
                resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
            if (selection.world_name != NULL && SDL_strcmp(selection.world_name, tool->world_name) == 0)
                return selection.world_position;
        }
    }
    return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static bool editor_clip_coord_from_selection(const slayer3d_game_data_runtime *runtime,
                                             const editor_clip_tool_state *tool,
                                             const slayer3d_game_data_editor_selection *hover_selection,
                                             const brush_world_runtime *world_runtime, int out_coord[3])
{
    if (runtime == NULL || tool == NULL || hover_selection == NULL || !hover_selection->hit || world_runtime == NULL ||
        out_coord == NULL)
    {
        return false;
    }
    const slayer3d_vec3 local_point =
        slayer3d_vec3_sub(hover_selection->point, editor_clip_world_origin_for_points(runtime, tool, hover_selection));
    out_coord[0] = editor_source_units_from_meters(world_runtime, local_point.x);
    out_coord[1] = editor_source_units_from_meters(world_runtime, local_point.y);
    out_coord[2] = editor_source_units_from_meters(world_runtime, local_point.z);
    editor_clip_snap_source_coord3(out_coord, editor_clip_source_snap_units(runtime, world_runtime));
    return true;
}

static int editor_clip_hovered_point_index(const editor_clip_tool_state *tool, const int coord[3], int snap_units)
{
    if (tool == NULL || coord == NULL)
        return -1;
    const int threshold = SDL_max(snap_units / 2, 1);
    const int max_distance_sq = threshold * threshold * 3;
    int best_index = -1;
    int best_distance_sq = max_distance_sq + 1;
    for (int i = 0; i < tool->point_count; ++i)
    {
        int point_coord[3];
        editor_clip_coord_from_point(tool->points[i], point_coord);
        const int dx = point_coord[0] - coord[0];
        const int dy = point_coord[1] - coord[1];
        const int dz = point_coord[2] - coord[2];
        const int distance_sq = dx * dx + dy * dy + dz * dz;
        if (distance_sq <= max_distance_sq && distance_sq < best_distance_sq)
        {
            best_index = i;
            best_distance_sq = distance_sq;
        }
    }
    return best_index;
}

static const char *editor_selection_clip_identity(const slayer3d_game_data_editor_selection *selection)
{
    if (selection == NULL)
        return NULL;
    if (selection->element_editor != NULL && selection->element_editor->stable_id != NULL &&
        selection->element_editor->stable_id[0] != '\0')
    {
        return selection->element_editor->stable_id;
    }
    return selection->element_name;
}

static bool capture_editor_clip_tool_selection(slayer3d_game_data_runtime *runtime, editor_clip_tool_state *tool)
{
    if (runtime == NULL || tool == NULL || !editor_selected_brushes_active_for_scene(runtime))
        return true;

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!selection.hit || selection.type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD ||
            selection.world_name == NULL)
        {
            continue;
        }

        if (tool->world_name[0] == '\0')
            SDL_snprintf(tool->world_name, sizeof(tool->world_name), "%s", selection.world_name);
        else if (SDL_strcmp(tool->world_name, selection.world_name) != 0)
        {
            SDL_snprintf(tool->message, sizeof(tool->message), "Clip Tool: select brushes from one world");
            return false;
        }

        const char *identity = editor_selection_clip_identity(&selection);
        if (identity == NULL || identity[0] == '\0')
            continue;
        if (tool->selected_brush_count >= SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY)
        {
            SDL_snprintf(tool->message, sizeof(tool->message), "Clip Tool: too many selected brushes");
            return false;
        }
        SDL_snprintf(tool->brush_identities[tool->selected_brush_count],
                     sizeof(tool->brush_identities[tool->selected_brush_count]), "%s", identity);
        tool->brush_identity_refs[tool->selected_brush_count] = tool->brush_identities[tool->selected_brush_count];
        tool->selected_brush_count++;
    }
    return true;
}

bool slayer3d_game_data_enter_editor_clip_tool(slayer3d_game_data_runtime *runtime, const char *message_override)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    reset_editor_clip_tool_state(runtime, "");
    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    tool->active = true;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    SDL_snprintf(tool->scene, sizeof(tool->scene), "%s", active_scene != NULL ? active_scene : "");
    tool->hovered_point = -1;
    tool->dragged_point = -1;
    tool->keep_mode = EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT;

    const bool selection_valid = capture_editor_clip_tool_selection(runtime, tool);
    if (!selection_valid)
    {
        tool->preview_valid = false;
    }
    else if (tool->selected_brush_count <= 0)
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s",
                     message_override != NULL && message_override[0] != '\0' ? message_override
                                                                             : "Clip Tool: select brushes to clip");
    }
    else
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s",
                     message_override != NULL && message_override[0] != '\0' ? message_override
                                                                             : "Clip Tool: click to place points");
    }

    publish_editor_clip_tool_state(runtime);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", tool->message);
    return selection_valid;
}

bool slayer3d_game_data_cancel_editor_clip_tool(slayer3d_game_data_runtime *runtime, const char *message_override)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    if (!tool->active)
    {
        reset_editor_clip_tool_state(runtime, message_override != NULL ? message_override : "Clip Tool inactive");
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                       runtime->editor_clip_tool.message);
        return true;
    }

    tool->point_count = 0;
    tool->hovered_point = -1;
    tool->dragged_point = -1;
    tool->preview_valid = false;
    SDL_snprintf(tool->message, sizeof(tool->message), "%s",
                 message_override != NULL && message_override[0] != '\0' ? message_override : "Clip Tool cancelled");
    publish_editor_clip_tool_state(runtime);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", tool->message);
    return true;
}

bool slayer3d_game_data_escape_editor_clip_tool(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    if (runtime->editor_clip_tool.active && editor_clip_tool_operation_active(&runtime->editor_clip_tool))
        return slayer3d_game_data_cancel_editor_clip_tool(runtime, NULL);
    return slayer3d_game_data_set_editor_tool_mode(runtime, "select", NULL);
}

bool slayer3d_game_data_cycle_editor_clip_keep_mode(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    if (!tool->active)
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s", "Clip Tool inactive");
        publish_editor_clip_tool_state(runtime);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", tool->message);
        return true;
    }

    tool->keep_mode = editor_clip_next_keep_mode(tool->keep_mode);
    SDL_snprintf(tool->message, sizeof(tool->message), "Clip Tool keep %s",
                 editor_clip_keep_mode_name(tool->keep_mode));
    publish_editor_clip_tool_state(runtime);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", tool->message);
    return true;
}

static bool editor_clip_tool_build_desc(editor_clip_tool_state *tool, editor_brush_source_clip_desc *out_desc,
                                        char *error_buffer, int error_buffer_size)
{
    if (tool == NULL || out_desc == NULL)
        return false;
    SDL_zero(*out_desc);
    if (!tool->active || tool->selected_brush_count <= 0)
    {
        set_error(error_buffer, error_buffer_size, "Clip Tool: select brushes to clip");
        return false;
    }
    if (tool->point_count < 2)
    {
        set_error(error_buffer, error_buffer_size, "Clip Tool: place at least two clip points");
        return false;
    }

    slayer3d_vec3 normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (tool->point_count >= 3)
    {
        normal = slayer3d_vec3_cross(slayer3d_vec3_sub(tool->points[1], tool->points[0]),
                                     slayer3d_vec3_sub(tool->points[2], tool->points[0]));
    }
    else
        normal = slayer3d_vec3_cross(slayer3d_vec3_sub(tool->points[1], tool->points[0]),
                                     editor_clip_tool_work_plane_normal(tool));
    if (slayer3d_vec3_length_squared(normal) <= 0.000001f)
    {
        set_error(error_buffer, error_buffer_size, "Clip Tool: clip points do not define a plane");
        return false;
    }

    normal = slayer3d_vec3_normalize(normal);
    out_desc->brush_identities = tool->brush_identity_refs;
    out_desc->brush_count = tool->selected_brush_count;
    out_desc->normal = normal;
    out_desc->distance_source_units = slayer3d_vec3_dot(normal, tool->points[0]);
    out_desc->keep_mode = tool->keep_mode;
    return true;
}

static void editor_clip_tool_refresh_preview(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    tool->preview_valid = false;
    if (!tool->active)
    {
        publish_editor_clip_tool_state(runtime);
        return;
    }
    if (tool->selected_brush_count <= 0)
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s", "Clip Tool: select brushes to clip");
        publish_editor_clip_tool_state(runtime);
        return;
    }
    if (tool->point_count == 0)
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s", "Clip Tool: click to place points");
        publish_editor_clip_tool_state(runtime);
        return;
    }
    if (tool->point_count == 1)
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s", "Clip Tool: place second point");
        publish_editor_clip_tool_state(runtime);
        return;
    }

    editor_brush_source_clip_desc desc;
    char error_buffer[256];
    if (!editor_clip_tool_build_desc(tool, &desc, error_buffer, sizeof(error_buffer)))
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s", error_buffer);
        publish_editor_clip_tool_state(runtime);
        return;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, tool->world_name);
    editor_brush_source_clip_result result;
    SDL_zero(result);
    if (editor_brush_world_preview_source_clip_operation(world_runtime, &desc, &result, error_buffer,
                                                         sizeof(error_buffer)))
    {
        tool->preview_valid = true;
        SDL_snprintf(tool->message, sizeof(tool->message), "Clip Tool: %s, Enter applies",
                     result.diagnostic[0] != '\0' ? result.diagnostic : "valid clip");
    }
    else
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s",
                     error_buffer[0] != '\0' ? error_buffer : result.diagnostic);
    }
    editor_brush_world_free_source_clip_result(&result);
    publish_editor_clip_tool_state(runtime);
}

static void editor_clip_tool_set_work_plane_normal(editor_clip_tool_state *tool, slayer3d_vec3 work_plane_normal)
{
    if (tool == NULL || slayer3d_vec3_length_squared(work_plane_normal) <= 0.000001f)
        return;
    tool->has_work_plane_normal = true;
    tool->work_plane_normal = slayer3d_vec3_normalize(work_plane_normal);
}

bool slayer3d_game_data_place_editor_clip_point_source(slayer3d_game_data_runtime *runtime, const int coord[3],
                                                       slayer3d_vec3 work_plane_normal)
{
    if (runtime == NULL || coord == NULL)
        return false;
    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    if (!tool->active)
        return true;
    if (tool->point_count >= SLAYER3D_EDITOR_CLIP_TOOL_MAX_POINTS)
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s", "Clip Tool: maximum clip points placed");
        publish_editor_clip_tool_state(runtime);
        return true;
    }

    editor_clip_tool_set_work_plane_normal(tool, work_plane_normal);
    tool->points[tool->point_count] = editor_clip_point_from_coord(coord);
    tool->dragged_point = tool->point_count;
    tool->hovered_point = tool->point_count;
    tool->point_count++;
    editor_clip_tool_refresh_preview(runtime);
    return true;
}

bool slayer3d_game_data_move_editor_clip_point_source(slayer3d_game_data_runtime *runtime, int point_index,
                                                      const int coord[3], slayer3d_vec3 work_plane_normal)
{
    if (runtime == NULL || coord == NULL)
        return false;
    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    if (!tool->active || point_index < 0 || point_index >= tool->point_count)
        return true;

    editor_clip_tool_set_work_plane_normal(tool, work_plane_normal);
    tool->points[point_index] = editor_clip_point_from_coord(coord);
    tool->hovered_point = point_index;
    editor_clip_tool_refresh_preview(runtime);
    return true;
}

static slayer3d_vec3 editor_clip_work_plane_normal_from_selection_json(
    const slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
    const slayer3d_game_data_editor_selection *hover_selection)
{
    slayer3d_vec3 normal;
    float distance = 0.0f;
    if (editor_work_plane_desc_from_trace_json(runtime, obj_get(selection_json, "trace"), &normal, &distance))
        return normal;
    if (hover_selection != NULL && slayer3d_vec3_length_squared(hover_selection->normal) > 0.000001f)
        return slayer3d_vec3_normalize(hover_selection->normal);
    return slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
}

bool editor_handle_clip_tool_input(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                   const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !runtime->editor_clip_tool.active)
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return true;

    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    const bool has_left_event = left_pressed || left_down || left_released;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, tool->world_name);
    int coord[3] = {0, 0, 0};
    const bool has_coord = editor_clip_coord_from_selection(runtime, tool, hover_selection, world_runtime, coord);
    const int snap_units = editor_clip_source_snap_units(runtime, world_runtime);
    const slayer3d_vec3 work_plane_normal =
        editor_clip_work_plane_normal_from_selection_json(runtime, selection_json, hover_selection);

    if (has_coord)
        tool->hovered_point = editor_clip_hovered_point_index(tool, coord, snap_units);
    else
        tool->hovered_point = -1;

    if (left_pressed)
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        if (!has_coord || world_runtime == NULL)
        {
            SDL_snprintf(tool->message, sizeof(tool->message), "%s", "Clip Tool: point placement requires a hit");
            publish_editor_clip_tool_state(runtime);
            return true;
        }
        if (tool->hovered_point >= 0)
            tool->dragged_point = tool->hovered_point;
        else if (!slayer3d_game_data_place_editor_clip_point_source(runtime, coord, work_plane_normal))
            return false;
    }
    else if (left_down && tool->dragged_point >= 0)
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        if (has_coord &&
            !slayer3d_game_data_move_editor_clip_point_source(runtime, tool->dragged_point, coord, work_plane_normal))
        {
            return false;
        }
    }
    else if (left_released && tool->dragged_point >= 0)
    {
        tool->dragged_point = -1;
        editor_clip_tool_refresh_preview(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
    }

    if (!has_left_event)
        publish_editor_clip_tool_state(runtime);
    return true;
}

bool slayer3d_game_data_commit_editor_clip_tool(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    editor_brush_source_clip_desc desc;
    char error_buffer[256];
    if (!editor_clip_tool_build_desc(tool, &desc, error_buffer, sizeof(error_buffer)))
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s", error_buffer);
        tool->preview_valid = false;
        publish_editor_clip_tool_state(runtime);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", tool->message);
        return true;
    }

    editor_brush_source_clip_result result;
    SDL_zero(result);
    if (!slayer3d_game_data_commit_editor_source_clip(runtime, tool->world_name, &desc, &result, error_buffer,
                                                      sizeof(error_buffer)))
    {
        SDL_snprintf(tool->message, sizeof(tool->message), "%s", error_buffer);
        tool->preview_valid = false;
        editor_brush_world_free_source_clip_result(&result);
        publish_editor_clip_tool_state(runtime);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", tool->message);
        return true;
    }

    char message[256];
    SDL_snprintf(message, sizeof(message), "Clip Tool committed %d brush%s", result.output_brush_count,
                 result.output_brush_count == 1 ? "" : "es");
    editor_brush_world_free_source_clip_result(&result);
    reset_editor_clip_tool_state(runtime, message);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", runtime->editor_clip_tool.message);
    return true;
}

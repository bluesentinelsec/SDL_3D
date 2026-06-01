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
}

void reset_editor_clip_tool_state(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL)
        return;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    SDL_zero(*tool);
    tool->hovered_point = -1;
    tool->dragged_point = -1;
    tool->keep_mode = EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT;
    SDL_snprintf(tool->message, sizeof(tool->message), "%s", message != NULL ? message : "");
    publish_editor_clip_tool_state(runtime);
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
    {
        normal = slayer3d_vec3_cross(slayer3d_vec3_sub(tool->points[1], tool->points[0]),
                                     slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    }
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

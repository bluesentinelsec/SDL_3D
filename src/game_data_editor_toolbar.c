/**
 * @file game_data_editor_toolbar.c
 * @brief Editor toolbar widget handling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

static bool editor_retained_ui_hit(const slayer3d_game_data_runtime *runtime, float mouse_x, float mouse_y,
                                   slayer3d_ui_layout_model **out_layout, const slayer3d_ui_layout_hit_region **out_hit)
{
    if (out_layout != NULL)
        *out_layout = NULL;
    if (out_hit != NULL)
        *out_hit = NULL;

    slayer3d_ui_layout_model *layout = NULL;
    if (!slayer3d_ui_layout_create(&layout))
        return false;
    if (!slayer3d_game_data_build_active_ui_widget_layout(runtime, 1280.0f, 720.0f, NULL, layout))
    {
        slayer3d_ui_layout_destroy(layout);
        return false;
    }

    const slayer3d_ui_layout_hit_region *hit = slayer3d_ui_layout_hit_test(layout, mouse_x, mouse_y);
    if (out_hit != NULL)
        *out_hit = hit;
    if (out_layout != NULL)
        *out_layout = layout;
    else
        slayer3d_ui_layout_destroy(layout);
    return hit != NULL;
}

static bool editor_hit_id_has_prefix(const slayer3d_ui_layout_hit_region *hit, const char *prefix)
{
    if (hit == NULL || prefix == NULL)
        return false;
    return SDL_strncmp(hit->id, prefix, SDL_strlen(prefix)) == 0;
}

static bool editor_hit_is_toolbar(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.toolbar.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.tool_toolbar.");
}

static bool editor_hit_is_palette(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.palette.");
}

static bool editor_hit_is_texture_viewer(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.");
}

static bool editor_hit_is_actor_viewer(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.actor_viewer.");
}

static bool editor_hit_is_left_inspector(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.left_inspector.");
}

static bool editor_hit_is_console(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.console.");
}

static bool editor_hit_is_inspector_scrollbar(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.left_inspector.scroll.track") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.left_inspector.scroll.thumb.");
}

static const slayer3d_ui_layout_hit_region *editor_find_layout_hit_by_id(const slayer3d_ui_layout_model *layout,
                                                                         const char *id)
{
    if (layout == NULL || id == NULL)
        return NULL;

    for (int i = 0, count = slayer3d_ui_layout_hit_region_count(layout); i < count; ++i)
    {
        const slayer3d_ui_layout_hit_region *candidate = slayer3d_ui_layout_hit_region_at(layout, i);
        if (candidate != NULL && SDL_strcmp(candidate->id, id) == 0)
            return candidate;
    }
    return NULL;
}

static float editor_clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static float editor_round_to_step(float value, float step)
{
    if (step <= 0.0f)
        return value;
    const float normalized = value / step;
    const float rounded = normalized >= 0.0f ? SDL_floorf(normalized + 0.5f) : SDL_ceilf(normalized - 0.5f);
    return rounded * step;
}

static bool editor_update_inspector_scroll_drag(slayer3d_game_data_runtime *runtime,
                                                const slayer3d_ui_layout_model *layout, float mouse_y)
{
    if (runtime == NULL || runtime->scene_state == NULL || layout == NULL)
        return false;

    const slayer3d_ui_layout_hit_region *track =
        editor_find_layout_hit_by_id(layout, "ui.editor_shell.left_inspector.scroll.track");
    if (track == NULL)
        return false;

    const float scroll_min = 0.0f;
    const float scroll_max = 180.0f;
    const float scroll_step = 60.0f;
    const float thumb_h = 66.0f;
    const float travel = track->rect.h - thumb_h;
    if (travel <= 0.0f)
        return false;

    const float local_y = editor_clamp_float(mouse_y - track->rect.y - thumb_h * 0.5f, 0.0f, travel);
    const float ratio = local_y / travel;
    float scroll = scroll_min + ratio * (scroll_max - scroll_min);
    scroll = editor_round_to_step(scroll, scroll_step);
    scroll = editor_clamp_float(scroll, scroll_min, scroll_max);
    slayer3d_properties_set_float(runtime->scene_state, "editor.inspector.scroll", scroll);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "inspector scrolled");
    return true;
}

static bool editor_actor_select_action(const char *action)
{
    static const char *prefix = "editor.actor.select.";
    static const char *slot_prefix = "editor.actor.select_slot.";
    return action != NULL &&
           ((SDL_strncmp(action, prefix, SDL_strlen(prefix)) == 0 && action[SDL_strlen(prefix)] != '\0') ||
            (SDL_strncmp(action, slot_prefix, SDL_strlen(slot_prefix)) == 0 &&
             action[SDL_strlen(slot_prefix)] != '\0'));
}

static bool editor_actor_placement_mode(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const char *mode = slayer3d_properties_get_string(runtime->scene_state, "editor.tool.mode", "");
    return mode != NULL && SDL_strncmp(mode, "actor_", 6) == 0;
}

static bool editor_emit_signal(slayer3d_game_data_runtime *runtime, const char *signal)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    const int signal_id = slayer3d_game_data_find_signal(runtime, signal);
    if (bus == NULL || signal_id < 0)
        return false;
    slayer3d_signal_emit(bus, signal_id, NULL);
    return true;
}

static bool editor_texture_select_action_is_blocked(slayer3d_game_data_runtime *runtime, const char *action)
{
    static const char *prefix = "editor.texture.select.";
    if (runtime == NULL || runtime->scene_state == NULL || action == NULL ||
        SDL_strncmp(action, prefix, SDL_strlen(prefix)) != 0)
    {
        return false;
    }

    const char *texture_id = action + SDL_strlen(prefix);
    if (texture_id[0] == '\0')
        return false;

    char pending_key[160];
    char failed_key[160];
    SDL_snprintf(pending_key, sizeof(pending_key), "asset_warmup.ui_image.image.editor_shell.texture.%s.pending",
                 texture_id);
    SDL_snprintf(failed_key, sizeof(failed_key), "asset_warmup.ui_image.image.editor_shell.texture.%s.failed",
                 texture_id);

    if (slayer3d_properties_get_bool(runtime->scene_state, pending_key, false))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "texture loading");
        return true;
    }
    if (slayer3d_properties_get_bool(runtime->scene_state, failed_key, false))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "texture unavailable");
        return true;
    }
    return false;
}

static const char *editor_mode_for_tool_action(const char *action)
{
    if (SDL_strcmp(action, "editor.tool.select") == 0)
        return "select";
    if (SDL_strcmp(action, "editor.tool.brush") == 0)
        return "brush";
    if (SDL_strcmp(action, "editor.tool.face") == 0)
        return "face";
    if (SDL_strcmp(action, "editor.tool.edge") == 0)
        return "edge";
    if (SDL_strcmp(action, "editor.tool.clip") == 0)
        return "clip";
    if (SDL_strcmp(action, "editor.tool.vertex") == 0)
        return "vertex";
    if (SDL_strcmp(action, "editor.tool.rotate") == 0)
        return "rotate";
    if (SDL_strcmp(action, "editor.tool.scale") == 0)
        return "scale";
    if (SDL_strcmp(action, "editor.tool.shear") == 0)
        return "shear";
    return NULL;
}

bool slayer3d_game_data_set_editor_tool_mode(slayer3d_game_data_runtime *runtime, const char *mode,
                                             const char *message_override)
{
    if (runtime == NULL || runtime->scene_state == NULL || mode == NULL || mode[0] == '\0')
        return false;

    const char *tool_mode = mode;
    const char *message = message_override;
    if (SDL_strcmp(mode, "select") == 0)
    {
        tool_mode = "select";
        if (message == NULL || message[0] == '\0')
            message = "select mode";
    }
    else if (SDL_strcmp(mode, "brush") == 0)
    {
        tool_mode = "brush";
        if (message == NULL || message[0] == '\0')
            message = "brush tool";
    }
    else if (SDL_strcmp(mode, "face") == 0)
    {
        tool_mode = "face";
        if (message == NULL || message[0] == '\0')
            message = "face tool";
    }
    else if (SDL_strcmp(mode, "edge") == 0)
    {
        tool_mode = "edge";
        if (message == NULL || message[0] == '\0')
        {
            const int selected_count = slayer3d_properties_get_int(runtime->scene_state, "editor.selection.count", 0);
            message = selected_count > 0 ? "edge tool" : "select a brush before edge tool";
        }
    }
    else if (SDL_strcmp(mode, "clip") == 0)
    {
        tool_mode = "clip";
        if (message == NULL || message[0] == '\0')
            message = "";
    }
    else if (SDL_strcmp(mode, "vertex") == 0)
    {
        tool_mode = "vertex";
        if (message == NULL || message[0] == '\0')
        {
            const int selected_count = slayer3d_properties_get_int(runtime->scene_state, "editor.selection.count", 0);
            message = selected_count > 0 ? "vertex tool" : "select a brush before vertex tool";
        }
    }
    else if (SDL_strcmp(mode, "rotate") == 0)
    {
        tool_mode = "rotate";
        if (message == NULL || message[0] == '\0')
        {
            const int selected_count = slayer3d_properties_get_int(runtime->scene_state, "editor.selection.count", 0);
            message = selected_count > 0 ? "rotate tool" : "select a brush before rotate tool";
        }
    }
    else if (SDL_strcmp(mode, "scale") == 0)
    {
        tool_mode = "scale";
        if (message == NULL || message[0] == '\0')
        {
            const int selected_count = slayer3d_properties_get_int(runtime->scene_state, "editor.selection.count", 0);
            message = selected_count > 0 ? "scale tool" : "select a brush before scale tool";
        }
    }
    else if (SDL_strcmp(mode, "shear") == 0)
    {
        tool_mode = "shear";
        if (message == NULL || message[0] == '\0')
        {
            const int selected_count = slayer3d_properties_get_int(runtime->scene_state, "editor.selection.count", 0);
            message = selected_count > 0 ? "shear tool" : "select a brush before shear tool";
        }
    }
    else
    {
        return false;
    }

    const bool entering_clip = SDL_strcmp(mode, "clip") == 0;
    const bool leaving_clip =
        SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", ""), "clip") == 0 &&
        !entering_clip;

    if (SDL_strcmp(mode, "vertex") != 0)
        (void)slayer3d_game_data_clear_editor_vertex_selection(runtime);
    if (SDL_strcmp(mode, "edge") != 0)
        (void)slayer3d_game_data_clear_editor_edge_selection(runtime);
    if (leaving_clip)
        reset_editor_clip_tool_state(runtime, "");
    (void)editor_cancel_pending_brush_preview(runtime, "brush preview cancelled");
    slayer3d_properties_set_string(runtime->scene_state, "editor.mode", mode);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.mode", tool_mode);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    slayer3d_properties_set_string(runtime->scene_state, "editor.palette.active", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.viewer.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.viewer.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.grid.menu.open", false);
    clear_editor_command_preview(runtime);
    if (entering_clip)
        return slayer3d_game_data_enter_editor_clip_tool(runtime, message);
    if (SDL_strcmp(mode, "rotate") == 0)
        reset_editor_rotate_tool_state(runtime, message);
    if (SDL_strcmp(mode, "scale") == 0)
        reset_editor_scale_tool_state(runtime, message);
    if (SDL_strcmp(mode, "shear") == 0)
        reset_editor_shear_tool_state(runtime, message);
    return true;
}

static bool editor_apply_tool_action(slayer3d_game_data_runtime *runtime, const char *action)
{
    const char *mode = editor_mode_for_tool_action(action);
    if (mode != NULL)
        return slayer3d_game_data_set_editor_tool_mode(runtime, mode, NULL);
    if (runtime != NULL &&
        (SDL_strcmp(action, "editor.brush.duplicate") == 0 || SDL_strcmp(action, "editor.brush.flip_vertical") == 0 ||
         SDL_strcmp(action, "editor.brush.flip_horizontal") == 0))
    {
        const char *signal = "signal.editor.brush.duplicate";
        if (SDL_strcmp(action, "editor.brush.flip_horizontal") == 0)
            signal = "signal.editor.brush.flip_horizontal";
        else if (SDL_strcmp(action, "editor.brush.flip_vertical") == 0)
            signal = "signal.editor.brush.flip_vertical";
        if (editor_emit_signal(runtime, signal))
            return true;
    }
    if (runtime != NULL &&
        (SDL_strncmp(action, "editor.texture.", 15) == 0 || SDL_strncmp(action, "editor.palette.", 15) == 0 ||
         SDL_strncmp(action, "editor.actor.", 13) == 0 || SDL_strncmp(action, "editor.inspector.", 17) == 0))
    {
        char signal[128];
        SDL_snprintf(signal, sizeof(signal), "signal.%s", action);
        if (editor_emit_signal(runtime, signal))
            return true;
    }
    return false;
}

bool editor_handle_tool_mode_buttons(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed)
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
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    slayer3d_ui_layout_model *layout = NULL;
    const slayer3d_ui_layout_hit_region *hit = NULL;
    (void)editor_retained_ui_hit(runtime, mouse_x, mouse_y, &layout, &hit);
    if (slayer3d_properties_get_bool(runtime->scene_state, "editor.inspector.scroll.drag.active", false))
    {
        if (released || !left_down)
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.inspector.scroll.drag.active", false);
        }
        else
        {
            if (out_consumed != NULL)
                *out_consumed = true;
            (void)editor_update_inspector_scroll_drag(runtime, layout, mouse_y);
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
    }
    if (editor_hit_is_toolbar(hit) || editor_hit_is_palette(hit) || editor_hit_is_texture_viewer(hit) ||
        editor_hit_is_actor_viewer(hit) || editor_hit_is_left_inspector(hit) || editor_hit_is_console(hit))
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        if (released || !left_down)
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.inspector.scroll.drag.active", false);
        }
        if (clicked && editor_hit_is_inspector_scrollbar(hit))
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.inspector.scroll.drag.active", true);
            (void)editor_update_inspector_scroll_drag(runtime, layout, mouse_y);
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
        if (clicked && hit->action[0] != '\0' && !editor_texture_select_action_is_blocked(runtime, hit->action))
        {
            (void)editor_apply_tool_action(runtime, hit->action);
            if (editor_actor_select_action(hit->action))
            {
                slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", true);
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                               "drag actor into viewport");
            }
        }
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    slayer3d_ui_layout_destroy(layout);

    if (slayer3d_properties_get_bool(runtime->scene_state, "editor.actor.drag.active", false))
    {
        if (released || !left_down)
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", false);
            if (released && editor_actor_placement_mode(runtime) &&
                slayer3d_properties_get_bool(runtime->scene_state, "editor.placement_preview.active", false))
            {
                if (out_consumed != NULL)
                    *out_consumed = true;
                (void)editor_emit_signal(runtime, "signal.editor.command.commit");
                return true;
            }
        }
    }

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
        if (mode == NULL || mode[0] == '\0')
            return true;
        (void)slayer3d_game_data_set_editor_tool_mode(runtime, mode, json_string(button, "message", NULL));
        return true;
    }
    return true;
}

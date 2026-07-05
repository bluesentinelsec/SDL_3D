/**
 * @file game_data_editor_toolbar.c
 * @brief Editor toolbar widget handling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
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

static bool editor_hit_is_texture_viewer(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.");
}

static bool editor_hit_is_file_menu(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.file_menu.");
}

static bool editor_hit_is_global_panel(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.global_panel.");
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

static bool editor_hit_is_texture_scrollbar(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.scroll.track") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.scroll.thumb");
}

static bool editor_hit_is_console_scrollbar(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.console.scroll.track") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.console.scroll.thumb");
}

static bool editor_hit_is_property_control(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.left_inspector.property.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.left_inspector.row.property");
}

static bool editor_hit_is_texture_edit_control(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.search.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.path.");
}

static bool editor_hit_is_global_edit_control(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.global_panel.data.key.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.global_panel.data.value.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.global_panel.data.row");
}

static const char *editor_property_row_select_action(const slayer3d_ui_layout_hit_region *hit, char *buffer,
                                                     size_t buffer_size)
{
    if (hit == NULL || buffer == NULL || buffer_size == 0U || hit->action[0] != '\0' ||
        !editor_hit_id_has_prefix(hit, "ui.editor_shell.left_inspector.row.property") ||
        SDL_strstr(hit->id, ".delete") != NULL)
    {
        return hit != NULL ? hit->action : "";
    }
    const char *slot_text = hit->id + SDL_strlen("ui.editor_shell.left_inspector.row.property");
    if (slot_text[0] < '0' || slot_text[0] > '9')
        return "";
    const int slot = slot_text[0] - '0';
    SDL_snprintf(buffer, buffer_size, "editor.property.select_slot.%d", slot);
    return buffer;
}

const slayer3d_ui_layout_hit_region *editor_find_layout_hit_by_id(const slayer3d_ui_layout_model *layout,
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

const slayer3d_ui_layout_render_command *editor_find_layout_render_by_id(const slayer3d_ui_layout_model *layout,
                                                                         const char *id)
{
    if (layout == NULL || id == NULL)
        return NULL;

    for (int i = 0, count = slayer3d_ui_layout_render_command_count(layout); i < count; ++i)
    {
        const slayer3d_ui_layout_render_command *candidate = slayer3d_ui_layout_render_command_at(layout, i);
        if (candidate != NULL && SDL_strcmp(candidate->id, id) == 0)
            return candidate;
    }
    return NULL;
}

float editor_clamp_float(float value, float min_value, float max_value)
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
    const float scroll_max = 300.0f;
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

static bool editor_update_texture_scroll_drag(slayer3d_game_data_runtime *runtime,
                                              const slayer3d_ui_layout_model *layout, float mouse_y)
{
    if (runtime == NULL || runtime->scene_state == NULL || layout == NULL)
        return false;

    const slayer3d_ui_layout_hit_region *track =
        editor_find_layout_hit_by_id(layout, "ui.editor_shell.texture_viewer.scroll.track");
    if (track == NULL)
        return false;

    const int texture_count = slayer3d_properties_get_int(runtime->scene_state, "editor.texture.count", 0);
    const int visible_slots = 6;
    const int max_scroll_index = SDL_max(0, texture_count - visible_slots);
    if (max_scroll_index <= 0)
    {
        slayer3d_properties_set_int(runtime->scene_state, "editor.texture.scroll.index", 0);
        return true;
    }

    const float thumb_h = 72.0f;
    const float travel = track->rect.h - thumb_h;
    if (travel <= 0.0f)
        return false;

    const float local_y = editor_clamp_float(mouse_y - track->rect.y - thumb_h * 0.5f, 0.0f, travel);
    const float ratio = local_y / travel;
    const int scroll_index = SDL_clamp((int)SDL_floorf(ratio * (float)max_scroll_index + 0.5f), 0, max_scroll_index);
    const int previous = slayer3d_properties_get_int(runtime->scene_state, "editor.texture.scroll.index", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.texture.scroll.index", scroll_index);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "texture browser scrolled");
    if (scroll_index != previous)
        (void)editor_emit_signal_by_name(runtime, "signal.editor.texture.filter");
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

typedef struct editor_tool_mode_def
{
    const char *mode;
    const char *default_message;
    const char *no_selection_message; /* used instead when nothing is selected */
} editor_tool_mode_def;

static const editor_tool_mode_def editor_tool_mode_defs[] = {
    {"select", "select mode", NULL},
    {"brush", "brush tool", NULL},
    {"face", "face tool", NULL},
    {"edge", "edge tool", "select a brush before edge tool"},
    {"clip", "", NULL},
    {"vertex", "vertex tool", "select a brush before vertex tool"},
    {"rotate", "rotate tool", "select a brush before rotate tool"},
    {"scale", "scale tool", "select a brush before scale tool"},
    {"shear", "shear tool", "select a brush before shear tool"},
};

static const editor_tool_mode_def *editor_find_tool_mode_def(const char *mode)
{
    if (mode == NULL || mode[0] == '\0')
        return NULL;
    for (size_t i = 0; i < SDL_arraysize(editor_tool_mode_defs); ++i)
    {
        if (SDL_strcmp(editor_tool_mode_defs[i].mode, mode) == 0)
            return &editor_tool_mode_defs[i];
    }
    return NULL;
}

static const char *editor_mode_for_tool_action(const char *action)
{
    static const char *prefix = "editor.tool.";
    if (action == NULL || SDL_strncmp(action, prefix, SDL_strlen(prefix)) != 0)
        return NULL;
    const editor_tool_mode_def *def = editor_find_tool_mode_def(action + SDL_strlen(prefix));
    return def != NULL ? def->mode : NULL;
}

bool slayer3d_game_data_set_editor_tool_mode(slayer3d_game_data_runtime *runtime, const char *mode,
                                             const char *message_override)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    const editor_tool_mode_def *def = editor_find_tool_mode_def(mode);
    if (def == NULL)
        return false;

    const char *message = message_override;
    if (message == NULL || message[0] == '\0')
    {
        message = def->default_message;
        if (def->no_selection_message != NULL &&
            slayer3d_properties_get_int(runtime->scene_state, "editor.selection.count", 0) <= 0)
        {
            message = def->no_selection_message;
        }
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
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.mode", mode);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    slayer3d_properties_set_string(runtime->scene_state, "editor.palette.active", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.viewer.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.viewer.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.file.menu.open", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.global.panel.open", false);
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

/* Toolbar actions forwarded verbatim as "signal.<action>". */
static const char *const editor_signal_action_names[] = {
    "editor.brush.duplicate",
    "editor.brush.flip_horizontal",
    "editor.brush.flip_vertical",
};

/* Action families whose members are all forwarded as "signal.<action>". */
static const char *const editor_signal_action_prefixes[] = {
    "editor.texture.",   "editor.palette.",  "editor.actor.",  "editor.things.",     "editor.file.",
    "editor.inspector.", "editor.property.", "editor.global.", "editor.visibility.", "editor.lock.",
};

static bool editor_action_routes_to_signal(const char *action)
{
    for (size_t i = 0; i < SDL_arraysize(editor_signal_action_names); ++i)
    {
        if (SDL_strcmp(action, editor_signal_action_names[i]) == 0)
            return true;
    }
    for (size_t i = 0; i < SDL_arraysize(editor_signal_action_prefixes); ++i)
    {
        if (SDL_strncmp(action, editor_signal_action_prefixes[i], SDL_strlen(editor_signal_action_prefixes[i])) == 0)
            return true;
    }
    return false;
}

static bool editor_apply_tool_action(slayer3d_game_data_runtime *runtime, const char *action)
{
    const char *mode = editor_mode_for_tool_action(action);
    if (mode != NULL)
        return slayer3d_game_data_set_editor_tool_mode(runtime, mode, NULL);
    if (runtime != NULL && editor_action_routes_to_signal(action))
    {
        char signal[128];
        SDL_snprintf(signal, sizeof(signal), "signal.%s", action);
        if (editor_emit_signal_by_name(runtime, signal))
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
    editor_update_texture_edit_display(runtime);
    editor_update_global_edit_display(runtime);

    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (input == NULL || !slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        return true;

    const bool clicked = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    const float wheel_y = slayer3d_input_get_mouse_wheel_y(input);
    slayer3d_ui_layout_model *layout = NULL;
    const slayer3d_ui_layout_hit_region *hit = NULL;
    (void)editor_retained_ui_hit(runtime, mouse_x, mouse_y, &layout, &hit);
    const bool property_focus_active = editor_property_edit_has_focus(runtime);
    const bool texture_focus_active = editor_texture_edit_has_focus(runtime);
    const bool global_focus_active = editor_global_edit_has_focus(runtime);
    if (property_focus_active && clicked && !editor_hit_is_property_control(hit))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.property.edit.focus", "");
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (texture_focus_active && clicked && !editor_hit_is_texture_edit_control(hit))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.texture.edit.focus", "");
        editor_update_texture_edit_display(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (global_focus_active && clicked && !editor_hit_is_global_edit_control(hit))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.edit.focus", "");
        editor_update_global_edit_display(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (property_focus_active && !clicked)
    {
        (void)editor_update_property_text_edit(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (texture_focus_active && !clicked)
    {
        (void)editor_update_texture_text_edit(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (global_focus_active && !clicked)
    {
        (void)editor_update_global_text_edit(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (slayer3d_properties_get_bool(runtime->scene_state, "editor.console.focused", false))
    {
        if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_ESCAPE))
        {
            editor_set_console_focus(runtime, false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.console.selection.active", false);
            editor_refresh_console_lines(runtime);
            if (out_consumed != NULL)
                *out_consumed = true;
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
        if (editor_console_copy_selection_if_requested(runtime, input))
        {
            if (out_consumed != NULL)
                *out_consumed = true;
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
    }
    if (slayer3d_properties_get_bool(runtime->scene_state, "editor.console.selection.active", false))
    {
        if (released || !left_down)
        {
            (void)editor_console_set_selection_cursor(runtime, layout, mouse_x, mouse_y);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.console.selection.active", false);
            editor_refresh_console_lines(runtime);
            (void)editor_console_copy_selection(runtime);
        }
        else
        {
            (void)editor_console_set_selection_cursor(runtime, layout, mouse_x, mouse_y);
        }
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
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
    if (slayer3d_properties_get_bool(runtime->scene_state, "editor.texture.scroll.drag.active", false))
    {
        if (released || !left_down)
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.scroll.drag.active", false);
        }
        else
        {
            if (out_consumed != NULL)
                *out_consumed = true;
            (void)editor_update_texture_scroll_drag(runtime, layout, mouse_y);
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
    }
    if (slayer3d_properties_get_bool(runtime->scene_state, "editor.console.scroll.drag.active", false))
    {
        if (released || !left_down)
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.console.scroll.drag.active", false);
        }
        else
        {
            if (out_consumed != NULL)
                *out_consumed = true;
            (void)editor_update_console_scroll_drag(runtime, layout, mouse_y);
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
    }
    if (clicked && !editor_hit_is_console(hit) &&
        slayer3d_properties_get_bool(runtime->scene_state, "editor.console.focused", false))
    {
        editor_set_console_focus(runtime, false);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.console.selection.active", false);
        editor_refresh_console_lines(runtime);
    }
    const bool console_event_active =
        editor_hit_is_console(hit) && (clicked || released || left_down || wheel_y != 0.0f);
    if (editor_hit_is_toolbar(hit) || editor_hit_is_texture_viewer(hit) || editor_hit_is_file_menu(hit) ||
        editor_hit_is_global_panel(hit) || editor_hit_is_actor_viewer(hit) || editor_hit_is_left_inspector(hit) ||
        console_event_active)
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        if (editor_hit_is_console(hit))
        {
            editor_set_console_focus(runtime, true);
            const int wheel_delta = editor_console_wheel_scroll_delta(wheel_y);
            if (wheel_delta != 0)
            {
                (void)editor_scroll_console_by(runtime, wheel_delta);
                slayer3d_ui_layout_destroy(layout);
                return true;
            }
            if (clicked && !editor_hit_is_console_scrollbar(hit))
            {
                if (editor_console_begin_selection(runtime, layout, mouse_x, mouse_y))
                {
                    slayer3d_ui_layout_destroy(layout);
                    return true;
                }
            }
        }
        if (released || !left_down)
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.inspector.scroll.drag.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.scroll.drag.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.console.scroll.drag.active", false);
        }
        if (clicked && editor_hit_is_inspector_scrollbar(hit))
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.inspector.scroll.drag.active", true);
            (void)editor_update_inspector_scroll_drag(runtime, layout, mouse_y);
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
        if (clicked && editor_hit_is_texture_scrollbar(hit))
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.scroll.drag.active", true);
            (void)editor_update_texture_scroll_drag(runtime, layout, mouse_y);
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
        if (clicked && editor_hit_is_console_scrollbar(hit))
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.console.scroll.drag.active", true);
            (void)editor_update_console_scroll_drag(runtime, layout, mouse_y);
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
        char derived_action[96];
        const char *hit_action = editor_property_row_select_action(hit, derived_action, sizeof(derived_action));
        if (clicked && hit_action[0] != '\0' && !editor_texture_select_action_is_blocked(runtime, hit_action))
        {
            if (SDL_strcmp(hit_action, "editor.console.scroll.up") == 0)
            {
                (void)editor_scroll_console_by(runtime, -1);
                slayer3d_ui_layout_destroy(layout);
                return true;
            }
            if (SDL_strcmp(hit_action, "editor.console.scroll.down") == 0)
            {
                (void)editor_scroll_console_by(runtime, 1);
                slayer3d_ui_layout_destroy(layout);
                return true;
            }
            (void)editor_apply_tool_action(runtime, hit_action);
            if (editor_actor_select_action(hit_action))
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
                (void)editor_emit_signal_by_name(runtime, "signal.editor.command.commit");
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

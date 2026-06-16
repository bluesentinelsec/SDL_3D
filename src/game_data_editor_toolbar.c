/**
 * @file game_data_editor_toolbar.c
 * @brief Editor toolbar widget handling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

static bool editor_emit_signal(slayer3d_game_data_runtime *runtime, const char *signal);

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

static bool editor_hit_is_texture_scrollbar(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.scroll.track") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.scroll.thumb");
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

static bool editor_property_edit_has_focus(const slayer3d_game_data_runtime *runtime)
{
    const char *focus = runtime != NULL && runtime->scene_state != NULL
                            ? slayer3d_properties_get_string(runtime->scene_state, "editor.property.edit.focus", "")
                            : "";
    return SDL_strcmp(focus, "key") == 0 || SDL_strcmp(focus, "value") == 0;
}

static bool editor_texture_edit_has_focus(const slayer3d_game_data_runtime *runtime)
{
    const char *focus = runtime != NULL && runtime->scene_state != NULL
                            ? slayer3d_properties_get_string(runtime->scene_state, "editor.texture.edit.focus", "")
                            : "";
    return SDL_strcmp(focus, "search") == 0 || SDL_strcmp(focus, "path") == 0;
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

static bool editor_property_edit_append_text(slayer3d_properties *scene_state, const char *key, const char *text,
                                             size_t max_len)
{
    if (scene_state == NULL || key == NULL || text == NULL || text[0] == '\0' || max_len == 0U)
        return false;
    char buffer[256];
    SDL_strlcpy(buffer, slayer3d_properties_get_string(scene_state, key, ""), sizeof(buffer));
    const size_t len = SDL_strlen(buffer);
    if (len >= max_len)
        return false;
    SDL_strlcpy(buffer + len, text, SDL_min(sizeof(buffer) - len, max_len - len + 1U));
    slayer3d_properties_set_string(scene_state, key, buffer);
    return true;
}

static bool editor_property_edit_backspace(slayer3d_properties *scene_state, const char *key)
{
    if (scene_state == NULL || key == NULL)
        return false;
    if (slayer3d_properties_get_bool(scene_state, "editor.property.edit.replace_on_text", false))
    {
        slayer3d_properties_set_string(scene_state, key, "");
        slayer3d_properties_set_bool(scene_state, "editor.property.edit.replace_on_text", false);
        return true;
    }
    char buffer[256];
    SDL_strlcpy(buffer, slayer3d_properties_get_string(scene_state, key, ""), sizeof(buffer));
    size_t len = SDL_strlen(buffer);
    if (len == 0U)
        return false;
    do
    {
        --len;
    } while (len > 0U && ((unsigned char)buffer[len] & 0xC0U) == 0x80U);
    buffer[len] = '\0';
    slayer3d_properties_set_string(scene_state, key, buffer);
    return true;
}

static bool editor_texture_edit_backspace(slayer3d_properties *scene_state, const char *key)
{
    if (scene_state == NULL || key == NULL)
        return false;
    if (slayer3d_properties_get_bool(scene_state, "editor.texture.edit.replace_on_text", false))
    {
        slayer3d_properties_set_string(scene_state, key, "");
        slayer3d_properties_set_bool(scene_state, "editor.texture.edit.replace_on_text", false);
        return true;
    }
    char buffer[256];
    SDL_strlcpy(buffer, slayer3d_properties_get_string(scene_state, key, ""), sizeof(buffer));
    size_t len = SDL_strlen(buffer);
    if (len == 0U)
        return false;
    do
    {
        --len;
    } while (len > 0U && ((unsigned char)buffer[len] & 0xC0U) == 0x80U);
    buffer[len] = '\0';
    slayer3d_properties_set_string(scene_state, key, buffer);
    return true;
}

static void editor_property_edit_sync_selected_slot(slayer3d_properties *scene_state, const char *edit_key,
                                                    const char *slot_suffix)
{
    if (scene_state == NULL || edit_key == NULL || slot_suffix == NULL)
        return;
    const int slot = slayer3d_properties_get_int(scene_state, "editor.property.edit.selected_slot", -1);
    if (slot < 0)
        return;
    char slot_key[96];
    SDL_snprintf(slot_key, sizeof(slot_key), "editor.property.slot.%d.%s", slot, slot_suffix);
    slayer3d_properties_set_string(scene_state, slot_key, slayer3d_properties_get_string(scene_state, edit_key, ""));
}

static bool editor_update_property_text_edit(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const char *focus = slayer3d_properties_get_string(runtime->scene_state, "editor.property.edit.focus", "");
    const bool key_focus = SDL_strcmp(focus, "key") == 0;
    const bool value_focus = SDL_strcmp(focus, "value") == 0;
    if (!key_focus && !value_focus)
        return false;

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return false;
    if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_ESCAPE))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.property.edit.focus", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "property edit cancelled");
        return true;
    }
    if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_RETURN) ||
        slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_KP_ENTER))
    {
        (void)editor_emit_signal(runtime, "signal.editor.property.apply");
        slayer3d_properties_set_string(runtime->scene_state, "editor.property.edit.focus", "");
        return true;
    }

    const char *edit_key = key_focus ? "editor.property.edit.key" : "editor.property.edit.value";
    bool changed = false;
    if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_BACKSPACE))
        changed = editor_property_edit_backspace(runtime->scene_state, edit_key) || changed;
    const char *input_text = slayer3d_input_get_text_input(input);
    if (input_text != NULL && input_text[0] != '\0' &&
        slayer3d_properties_get_bool(runtime->scene_state, "editor.property.edit.replace_on_text", false))
    {
        slayer3d_properties_set_string(runtime->scene_state, edit_key, "");
        slayer3d_properties_set_bool(runtime->scene_state, "editor.property.edit.replace_on_text", false);
    }
    changed = editor_property_edit_append_text(runtime->scene_state, edit_key, slayer3d_input_get_text_input(input),
                                               key_focus ? 64U : 192U) ||
              changed;
    if (changed)
    {
        editor_property_edit_sync_selected_slot(runtime->scene_state, edit_key, key_focus ? "key" : "value");
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "editing property");
    }
    return true;
}

static bool editor_update_texture_text_edit(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const char *focus = slayer3d_properties_get_string(runtime->scene_state, "editor.texture.edit.focus", "");
    const bool search_focus = SDL_strcmp(focus, "search") == 0;
    const bool path_focus = SDL_strcmp(focus, "path") == 0;
    if (!search_focus && !path_focus)
        return false;

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return false;
    if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_ESCAPE))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.texture.edit.focus", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "texture edit cancelled");
        return true;
    }
    if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_RETURN) ||
        slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_KP_ENTER))
    {
        (void)editor_emit_signal(runtime,
                                 path_focus ? "signal.editor.texture.path.apply" : "signal.editor.texture.refresh");
        slayer3d_properties_set_string(runtime->scene_state, "editor.texture.edit.focus", "");
        return true;
    }

    const char *edit_key = search_focus ? "editor.texture.search" : "editor.texture.path.input";
    bool changed = false;
    if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_BACKSPACE))
        changed = editor_texture_edit_backspace(runtime->scene_state, edit_key) || changed;
    const char *input_text = slayer3d_input_get_text_input(input);
    if (input_text != NULL && input_text[0] != '\0' &&
        slayer3d_properties_get_bool(runtime->scene_state, "editor.texture.edit.replace_on_text", false))
    {
        slayer3d_properties_set_string(runtime->scene_state, edit_key, "");
        slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.edit.replace_on_text", false);
    }
    changed = editor_property_edit_append_text(runtime->scene_state, edit_key, input_text, search_focus ? 96U : 240U) ||
              changed;
    if (changed)
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                       search_focus ? "editing texture search" : "editing texture path");
        if (search_focus)
        {
            slayer3d_properties_set_int(runtime->scene_state, "editor.texture.scroll.index", 0);
            (void)editor_emit_signal(runtime, "signal.editor.texture.refresh");
        }
    }
    return true;
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
    const float scroll_max = 240.0f;
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
        (void)editor_emit_signal(runtime, "signal.editor.texture.refresh");
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
         SDL_strncmp(action, "editor.actor.", 13) == 0 || SDL_strncmp(action, "editor.inspector.", 17) == 0 ||
         SDL_strncmp(action, "editor.property.", 16) == 0))
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
    const bool property_focus_active = editor_property_edit_has_focus(runtime);
    const bool texture_focus_active = editor_texture_edit_has_focus(runtime);
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
    if (editor_hit_is_toolbar(hit) || editor_hit_is_palette(hit) || editor_hit_is_texture_viewer(hit) ||
        editor_hit_is_actor_viewer(hit) || editor_hit_is_left_inspector(hit) || editor_hit_is_console(hit))
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        if (released || !left_down)
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.inspector.scroll.drag.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.scroll.drag.active", false);
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
        char derived_action[96];
        const char *hit_action = editor_property_row_select_action(hit, derived_action, sizeof(derived_action));
        if (clicked && hit_action[0] != '\0' && !editor_texture_select_action_is_blocked(runtime, hit_action))
        {
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

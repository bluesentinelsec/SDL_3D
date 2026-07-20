#include "game_data_editor_dialog_internal.h"
#include "game_data_internal.h"

#include <SDL3/SDL_log.h>

#define EDITOR_CONSOLE_HISTORY_COUNT 64
#define EDITOR_CONSOLE_THUMB_TRAVEL 24.0f

static void editor_clear_console_selection_state(slayer3d_properties *scene_state)
{
    if (scene_state == NULL)
        return;
    slayer3d_properties_set_bool(scene_state, "editor.console.selection.active", false);
    slayer3d_properties_set_bool(scene_state, "editor.console.selection.has", false);
    slayer3d_properties_set_string(scene_state, "editor.console.selection.clipboard", "");
    for (int i = 0; i < EDITOR_CONSOLE_VISIBLE_MAX; ++i)
    {
        char selected_key[64];
        SDL_snprintf(selected_key, sizeof(selected_key), "editor.console.line%d.selected", i);
        slayer3d_properties_set_bool(scene_state, selected_key, false);
    }
}

static void editor_refresh_console_selection_rows(slayer3d_properties *scene_state, int scroll, int count)
{
    if (scene_state == NULL)
        return;
    const bool selection_visible = slayer3d_properties_get_bool(scene_state, "editor.console.selection.has", false) ||
                                   slayer3d_properties_get_bool(scene_state, "editor.console.selection.active", false);
    int min_index = -1;
    int max_index = -1;
    if (selection_visible)
    {
        const int anchor = slayer3d_properties_get_int(scene_state, "editor.console.selection.anchor", -1);
        const int cursor = slayer3d_properties_get_int(scene_state, "editor.console.selection.cursor", anchor);
        if (anchor >= 0 && cursor >= 0)
        {
            min_index = SDL_min(anchor, cursor);
            max_index = SDL_max(anchor, cursor);
        }
    }

    const int visible_count = editor_console_visible_count(scene_state);
    for (int i = 0; i < EDITOR_CONSOLE_VISIBLE_MAX; ++i)
    {
        char selected_key[64];
        const int history_index = scroll + i;
        const bool selected = i < visible_count && history_index < count && min_index >= 0 &&
                              history_index >= min_index && history_index <= max_index;
        SDL_snprintf(selected_key, sizeof(selected_key), "editor.console.line%d.selected", i);
        slayer3d_properties_set_bool(scene_state, selected_key, selected);
    }
}

void editor_set_string_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, const char *value)
{
    const char *key = json_string(outputs, key_name, NULL);
    if (props != NULL && key != NULL)
        slayer3d_properties_set_string(props, key, value != NULL ? value : "");
}

void editor_set_bool_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, bool value)
{
    const char *key = json_string(outputs, key_name, NULL);
    if (props != NULL && key != NULL)
        slayer3d_properties_set_bool(props, key, value);
}

void editor_set_int_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, int value)
{
    const char *key = json_string(outputs, key_name, NULL);
    if (props != NULL && key != NULL)
        slayer3d_properties_set_int(props, key, value);
}

void editor_set_float_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, float value)
{
    const char *key = json_string(outputs, key_name, NULL);
    if (props != NULL && key != NULL)
        slayer3d_properties_set_float(props, key, value);
}

void editor_set_vec3_output(slayer3d_properties *props, yyjson_val *outputs, const char *key_name, slayer3d_vec3 value)
{
    const char *key = json_string(outputs, key_name, NULL);
    if (props != NULL && key != NULL)
        slayer3d_properties_set_vec3(props, key, value);
}

bool editor_emit_signal_by_name(slayer3d_game_data_runtime *runtime, const char *signal_name)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    const int signal_id = slayer3d_game_data_find_signal(runtime, signal_name);
    if (bus == NULL || signal_id < 0)
        return false;
    slayer3d_signal_emit(bus, signal_id, NULL);
    return true;
}

void editor_publish_console_message(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL || message == NULL || message[0] == '\0')
        return;

    char previous[EDITOR_CONSOLE_HISTORY_COUNT][512];
    SDL_zeroa(previous);
    for (int i = 0; i < EDITOR_CONSOLE_HISTORY_COUNT; ++i)
    {
        char key[64];
        SDL_snprintf(key, sizeof(key), "editor.console.history%d", i);
        SDL_strlcpy(previous[i], slayer3d_properties_get_string(runtime->scene_state, key, ""), sizeof(previous[i]));
    }
    for (int i = EDITOR_CONSOLE_HISTORY_COUNT - 1; i > 0; --i)
    {
        char key[64];
        SDL_snprintf(key, sizeof(key), "editor.console.history%d", i);
        slayer3d_properties_set_string(runtime->scene_state, key, previous[i - 1]);
    }
    slayer3d_properties_set_string(runtime->scene_state, "editor.console.history0", message);
    const int count = SDL_min(EDITOR_CONSOLE_HISTORY_COUNT,
                              slayer3d_properties_get_int(runtime->scene_state, "editor.console.count", 0) + 1);
    slayer3d_properties_set_int(runtime->scene_state, "editor.console.count", count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.console.scroll", 0);
    editor_clear_console_selection_state(runtime->scene_state);
    editor_refresh_console_lines(runtime);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
}

void slayer3d_game_data_editor_publish_console_message(struct slayer3d_game_data_runtime *runtime, const char *message)
{
    editor_publish_console_message(runtime, message);
}

void editor_refresh_console_lines(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const int count = SDL_clamp(slayer3d_properties_get_int(runtime->scene_state, "editor.console.count", 0), 0,
                                EDITOR_CONSOLE_HISTORY_COUNT);
    const int visible_count = editor_console_visible_count(runtime->scene_state);
    const int max_scroll = SDL_max(0, count - visible_count);
    const int scroll =
        SDL_clamp(slayer3d_properties_get_int(runtime->scene_state, "editor.console.scroll", 0), 0, max_scroll);
    slayer3d_properties_set_int(runtime->scene_state, "editor.console.scroll", scroll);

    for (int i = 0; i < EDITOR_CONSOLE_VISIBLE_MAX; ++i)
    {
        char line_key[64];
        SDL_snprintf(line_key, sizeof(line_key), "editor.console.line%d", i);
        if (i < visible_count)
        {
            char source_key[64];
            SDL_snprintf(source_key, sizeof(source_key), "editor.console.history%d", scroll + i);
            slayer3d_properties_set_string(runtime->scene_state, line_key,
                                           slayer3d_properties_get_string(runtime->scene_state, source_key, ""));
        }
        else
        {
            slayer3d_properties_set_string(runtime->scene_state, line_key, "");
        }
    }

    const float thumb_offset =
        max_scroll > 0 ? -((float)scroll / (float)max_scroll) * EDITOR_CONSOLE_THUMB_TRAVEL : 0.0f;
    slayer3d_properties_set_float(runtime->scene_state, "editor.console.scroll.y", thumb_offset);
    editor_refresh_console_selection_rows(runtime->scene_state, scroll, count);
}

static int editor_revision_to_int(Uint64 revision)
{
    return revision > (Uint64)SDL_MAX_SINT32 ? SDL_MAX_SINT32 : (int)revision;
}

static const char *editor_action_path(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *fallback)
{
    const char *path_key = json_string(action, "path_from_state", NULL);
    if (path_key != NULL && path_key[0] != '\0' && runtime != NULL && runtime->scene_state != NULL)
    {
        const char *path = slayer3d_properties_get_string(runtime->scene_state, path_key, NULL);
        if (path != NULL && path[0] != '\0')
            return path;
    }
    return json_string(action, "path", fallback);
}

static bool editor_state_key_has_prefix(const char *key, const char *prefix)
{
    return key != NULL && prefix != NULL && SDL_strncmp(key, prefix, SDL_strlen(prefix)) == 0;
}

static bool editor_state_key_preserved_for_new_document(const char *key)
{
    if (editor_state_key_has_prefix(key, "editor.asset_source."))
        return true;
    if (SDL_strcmp(key, "editor.media.path") == 0 || SDL_strcmp(key, "editor.media.relative") == 0 ||
        SDL_strcmp(key, "editor.media.available") == 0)
        return true;

    return false;
}

static void reset_editor_scene_state_for_new_document(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    for (int i = slayer3d_properties_count(runtime->scene_state) - 1; i >= 0; --i)
    {
        const char *key = NULL;
        if (!slayer3d_properties_get_key_at(runtime->scene_state, i, &key, NULL) || key == NULL)
            continue;
        if (editor_state_key_has_prefix(key, "editor.") && !editor_state_key_preserved_for_new_document(key))
            slayer3d_properties_remove(runtime->scene_state, key);
    }

    slayer3d_properties_set_string(runtime->scene_state, "editor.mode", "select");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.mode", "select");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.palette.active", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.file.menu.open", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.grid.menu.open", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.viewer.active", false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.texture.search", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.texture.search.display", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.texture.edit.focus", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.search.pending", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.viewer.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.actor.selected_index", -1);
    slayer3d_properties_set_string(runtime->scene_state, "editor.actor.selected", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.actor.selected.label", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.palette.game_object.cursor", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.palette.material.cursor", "");
    slayer3d_properties_set_int(runtime->scene_state, "editor.texture.selected_index", -1);
    slayer3d_properties_set_int(runtime->scene_state, "editor.texture.selected_slot", -1);
    slayer3d_properties_set_string(runtime->scene_state, "editor.texture.material", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.inspector.open", true);
    slayer3d_properties_set_string(runtime->scene_state, "editor.inspector.tab", "Object");
    slayer3d_properties_set_float(runtime->scene_state, "editor.inspector.scroll", 0.0f);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.inspector.scroll.drag.active", false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.property.edit.focus", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.property.edit.original_key", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.property.edit.key", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.property.edit.value", "");
    slayer3d_properties_set_int(runtime->scene_state, "editor.property.edit.selected_slot", -1);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.property.edit.replace_on_text", false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.property.count", 0);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.console.open", true);
    slayer3d_properties_set_float(runtime->scene_state, "editor.console.height", 120.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.console.visible_height", 120.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.console.x", 160.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.console.y", 520.0f);
    slayer3d_properties_set_string(runtime->scene_state, "editor.console.dock", "bottom");
    slayer3d_properties_set_string(runtime->scene_state, "editor.ui.window.pointer.id", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.ui.window.pointer.mode", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.ui.window.pointer.preview", "none");
    slayer3d_properties_set_int(runtime->scene_state, "editor.transaction.undo_count", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.transaction.redo_count", 0);
}

static void reset_editor_runtime_state_for_new_document(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;

    clear_editor_active_selection(runtime);
    clear_editor_vertex_hover_state(runtime);
    clear_editor_edge_hover_state(runtime);
    clear_editor_command_preview(runtime);
    clear_editor_placement_preview(runtime);
    reset_editor_clip_tool_state(runtime, "");
    reset_editor_rotate_tool_state(runtime, "");
    reset_editor_scale_tool_state(runtime, "");
    reset_editor_shear_tool_state(runtime, "");
    free_editor_command_history(&runtime->editor_command_history);
    SDL_zero(runtime->editor_drag_create);
    SDL_zero(runtime->editor_drag_move);
    SDL_zero(runtime->editor_camera_orbit);
    SDL_zero(runtime->editor_camera_move);
    runtime->editor_has_last_duplicate_offset = false;
    runtime->editor_last_duplicate_offset = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    (void)slayer3d_game_data_set_editor_tool_mode(runtime, "select", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "");
}

static void reset_editor_workspace_for_new_document(slayer3d_game_data_runtime *runtime)
{
    reset_editor_scene_state_for_new_document(runtime);
    reset_editor_runtime_state_for_new_document(runtime);
}

static bool append_command_text(char *buffer, size_t buffer_size, size_t *offset, const char *text)
{
    if (buffer == NULL || buffer_size == 0u || offset == NULL || text == NULL)
        return false;
    if (*offset >= buffer_size)
    {
        buffer[buffer_size - 1u] = '\0';
        return false;
    }
    const int written = SDL_snprintf(buffer + *offset, buffer_size - *offset, "%s", text);
    if (written < 0 || (size_t)written >= buffer_size - *offset)
    {
        buffer[buffer_size - 1u] = '\0';
        return false;
    }
    *offset += (size_t)written;
    return true;
}

static bool append_command_arg(char *buffer, size_t buffer_size, size_t *offset, const char *arg)
{
    if (arg == NULL || arg[0] == '\0')
        return true;
    if (!append_command_text(buffer, buffer_size, offset, " \""))
        return false;
    for (const char *cursor = arg; *cursor != '\0'; ++cursor)
    {
        char escaped[3] = {*cursor, '\0', '\0'};
        if (*cursor == '"' || *cursor == '\\')
        {
            escaped[0] = '\\';
            escaped[1] = *cursor;
        }
        if (!append_command_text(buffer, buffer_size, offset, escaped))
            return false;
    }
    return append_command_text(buffer, buffer_size, offset, "\"");
}

static bool append_test_run_mount_args(char *buffer, size_t buffer_size, size_t *offset, yyjson_val *action)
{
    const char *root = json_string(action, "root", NULL);
    const char *pack = json_string(action, "pack", NULL);
    if (root != NULL && (!append_command_text(buffer, buffer_size, offset, " --root") ||
                         !append_command_arg(buffer, buffer_size, offset, root)))
    {
        return false;
    }
    if (pack != NULL && (!append_command_text(buffer, buffer_size, offset, " --pack") ||
                         !append_command_arg(buffer, buffer_size, offset, pack)))
    {
        return false;
    }
    if (json_bool(action, "embedded", false) && !append_command_text(buffer, buffer_size, offset, " --embedded"))
        return false;
    const char *media = json_string(action, "media", NULL);
    if (media != NULL && (!append_command_text(buffer, buffer_size, offset, " --media") ||
                          !append_command_arg(buffer, buffer_size, offset, media)))
    {
        return false;
    }
    return true;
}

static bool format_test_run_direct_command(char *buffer, size_t buffer_size, yyjson_val *action,
                                           const slayer3d_game_data_editor_test_run_desc *desc,
                                           const char *resolved_scene)
{
    size_t offset = 0u;
    const char *runner = json_string(action, "runner", "slayer3d_runner");
    if (!append_command_text(buffer, buffer_size, &offset, runner) ||
        !append_test_run_mount_args(buffer, buffer_size, &offset, action) ||
        !append_command_text(buffer, buffer_size, &offset, " --data") ||
        !append_command_arg(buffer, buffer_size, &offset, desc != NULL ? desc->data_asset_path : NULL))
    {
        return false;
    }
    if (resolved_scene != NULL && resolved_scene[0] != '\0' &&
        (!append_command_text(buffer, buffer_size, &offset, " --scene") ||
         !append_command_arg(buffer, buffer_size, &offset, resolved_scene)))
    {
        return false;
    }
    if (desc != NULL && desc->player_start != NULL && desc->player_start[0] != '\0' &&
        (!append_command_text(buffer, buffer_size, &offset, " --player-start") ||
         !append_command_arg(buffer, buffer_size, &offset, desc->player_start)))
    {
        return false;
    }
    return true;
}

static bool format_test_run_manifest_command(char *buffer, size_t buffer_size, yyjson_val *action, const char *path)
{
    size_t offset = 0u;
    const char *runner = json_string(action, "runner", "slayer3d_runner");
    return append_command_text(buffer, buffer_size, &offset, runner) &&
           append_test_run_mount_args(buffer, buffer_size, &offset, action) &&
           append_command_text(buffer, buffer_size, &offset, " --test-run-manifest") &&
           append_command_arg(buffer, buffer_size, &offset, path);
}

bool publish_editor_brush_world_status(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, const char *world_name,
                                       const char *message, bool publish_result)
{
    if (runtime == NULL || outputs == NULL)
        return true;

    slayer3d_game_data_brush_world_editor_state state;
    const bool ok = slayer3d_game_data_get_brush_world_editor_state(runtime, world_name, &state);
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    if (publish_result)
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", ok);
        editor_set_string_output(scene_state, outputs, "message_key",
                                 ok ? (message != NULL ? message : "brush world status published")
                                    : "brush world status unavailable");
    }
    editor_set_string_output(scene_state, outputs, "world_key", ok && state.world_name != NULL ? state.world_name : "");
    editor_set_string_output(scene_state, outputs, "source_path_key",
                             ok && state.source_path != NULL ? state.source_path : "");
    editor_set_bool_output(scene_state, outputs, "dirty_key", ok && state.dirty);
    editor_set_int_output(scene_state, outputs, "revision_key", ok ? editor_revision_to_int(state.revision) : 0);
    editor_set_int_output(scene_state, outputs, "saved_revision_key",
                          ok ? editor_revision_to_int(state.saved_revision) : 0);
    return ok;
}

static void publish_editor_player_start_state(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                                              const editor_player_start_runtime *start)
{
    if (runtime == NULL || outputs == NULL)
        return;
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    slayer3d_game_data_player_start_editor_state state;
    SDL_zero(state);
    (void)slayer3d_game_data_get_player_start_editor_state(runtime, &state);
    editor_set_string_output(scene_state, outputs, "player_start_key", start != NULL ? start->name : "");
    editor_set_string_output(scene_state, outputs, "scene_key",
                             start != NULL && start->scene != NULL ? start->scene : "");
    editor_set_string_output(scene_state, outputs, "target_key",
                             start != NULL && start->target != NULL ? start->target : "");
    editor_set_vec3_output(scene_state, outputs, "position_key",
                           start != NULL ? start->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_float_output(scene_state, outputs, "yaw_key", start != NULL ? start->yaw : 0.0f);
    editor_set_float_output(scene_state, outputs, "pitch_key", start != NULL ? start->pitch : 0.0f);
    editor_set_bool_output(scene_state, outputs, "dirty_key", state.dirty);
    editor_set_int_output(scene_state, outputs, "revision_key", editor_revision_to_int(state.revision));
    editor_set_int_output(scene_state, outputs, "saved_revision_key", editor_revision_to_int(state.saved_revision));
}

static void publish_editor_level_state_outputs(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                                               const char *world_name)
{
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    slayer3d_game_data_brush_world_editor_state brush_state;
    SDL_zero(brush_state);
    const bool has_brush_state = slayer3d_game_data_get_brush_world_editor_state(runtime, world_name, &brush_state);
    slayer3d_game_data_player_start_editor_state start_state;
    SDL_zero(start_state);
    const bool has_start_state = slayer3d_game_data_get_player_start_editor_state(runtime, &start_state);
    editor_set_string_output(scene_state, outputs, "brush_world_key",
                             has_brush_state && brush_state.world_name != NULL ? brush_state.world_name : "");
    editor_set_bool_output(scene_state, outputs, "brush_dirty_key", has_brush_state && brush_state.dirty);
    editor_set_int_output(scene_state, outputs, "brush_revision_key",
                          has_brush_state ? editor_revision_to_int(brush_state.revision) : 0);
    editor_set_int_output(scene_state, outputs, "brush_saved_revision_key",
                          has_brush_state ? editor_revision_to_int(brush_state.saved_revision) : 0);
    editor_set_string_output(scene_state, outputs, "brush_source_path_key",
                             has_brush_state && brush_state.source_path != NULL ? brush_state.source_path : "");
    editor_set_bool_output(scene_state, outputs, "player_start_dirty_key", has_start_state && start_state.dirty);
    editor_set_int_output(scene_state, outputs, "player_start_count_key", has_start_state ? start_state.count : 0);
    editor_set_int_output(scene_state, outputs, "player_start_revision_key",
                          has_start_state ? editor_revision_to_int(start_state.revision) : 0);
    editor_set_int_output(scene_state, outputs, "player_start_saved_revision_key",
                          has_start_state ? editor_revision_to_int(start_state.saved_revision) : 0);
    editor_set_string_output(scene_state, outputs, "player_start_source_path_key",
                             has_start_state && start_state.source_path != NULL ? start_state.source_path : "");
}

bool slayer3d_game_data_export_editor_brush_world_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    char error[256];
    error[0] = '\0';
    char *json = NULL;
    size_t size = 0u;
    const bool ok = slayer3d_game_data_export_brush_world_fragment_json(runtime, world_name, &json, &size, error,
                                                                        (int)sizeof(error));
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "brush world exported")
                                : (error[0] != '\0' ? error : "brush world export failed"));
    editor_set_int_output(scene_state, outputs, "size_key", ok ? (int)size : 0);
    editor_set_string_output(scene_state, outputs, "json_key", ok && json != NULL ? json : "");
    (void)publish_editor_brush_world_status(runtime, outputs, world_name, NULL, false);
    SDL_free(json);
    return true;
}

bool slayer3d_game_data_export_editor_level_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    char error[256];
    error[0] = '\0';
    char *json = NULL;
    size_t size = 0u;
    const bool ok = slayer3d_game_data_export_editable_level_fragment_json(runtime, world_name, &json, &size, error,
                                                                           (int)sizeof(error));
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "editable level exported")
                                : (error[0] != '\0' ? error : "editable level export failed"));
    editor_set_int_output(scene_state, outputs, "size_key", ok ? (int)size : 0);
    editor_set_string_output(scene_state, outputs, "json_key", ok && json != NULL ? json : "");
    publish_editor_level_state_outputs(runtime, outputs, world_name);
    SDL_free(json);
    return true;
}

bool slayer3d_game_data_save_editor_level_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    const char *path = editor_action_path(runtime, action, NULL);
    char error[256];
    error[0] = '\0';
    size_t size = 0u;
    const bool ok = slayer3d_game_data_save_editable_level_fragment_file(runtime, world_name, path, &size, error,
                                                                         (int)sizeof(error));

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "editable level saved")
                                : (error[0] != '\0' ? error : "editable level save failed"));
    editor_set_int_output(scene_state, outputs, "size_key", ok ? (int)size : 0);
    editor_set_string_output(scene_state, outputs, "path_key", ok && path != NULL ? path : "");
    publish_editor_level_state_outputs(runtime, outputs, world_name);
    return true;
}

bool slayer3d_game_data_load_editor_level_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    const char *path = editor_action_path(runtime, action, NULL);
    const bool optional = json_bool(action, "optional", false);
    char error[256];
    error[0] = '\0';

    bool ok = true;
    bool skipped = false;
    if (path == NULL || path[0] == '\0')
    {
        ok = optional;
        skipped = optional;
        if (!ok)
            SDL_snprintf(error, sizeof(error), "editable level load path is required");
    }
    else
    {
        ok = slayer3d_game_data_load_editable_level_fragment_file(runtime, world_name, path, error, (int)sizeof(error));
    }

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? (skipped ? json_string(action, "skipped_message", "no editable level input")
                                           : json_string(action, "message", "editable level loaded"))
                                : (error[0] != '\0' ? error : "editable level load failed"));
    editor_set_string_output(scene_state, outputs, "path_key", ok && path != NULL ? path : "");
    publish_editor_level_state_outputs(runtime, outputs, world_name);
    return true;
}

bool slayer3d_game_data_export_editor_map_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    char error[256];
    error[0] = '\0';
    char *json = NULL;
    size_t size = 0u;
    const bool ok =
        slayer3d_game_data_export_editable_level_map_json(runtime, world_name, &json, &size, error, (int)sizeof(error));
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "map exported")
                                : (error[0] != '\0' ? error : "map export failed"));
    editor_set_int_output(scene_state, outputs, "size_key",
                          ok ? (int)(size > (size_t)SDL_MAX_SINT32 ? (size_t)SDL_MAX_SINT32 : size) : 0);
    editor_set_string_output(scene_state, outputs, "json_key", ok && json != NULL ? json : "");
    publish_editor_level_state_outputs(runtime, outputs, world_name);
    SDL_free(json);
    return true;
}

bool slayer3d_game_data_new_editor_map_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    const char *fragment = json_string(action, "fragment", NULL);
    char error[256];
    error[0] = '\0';
    const bool ok =
        fragment != NULL && slayer3d_game_data_load_editable_level_fragment_json(
                                runtime, world_name, fragment, SDL_strlen(fragment), NULL, error, (int)sizeof(error));

    if (ok)
        reset_editor_workspace_for_new_document(runtime);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "new map")
                                : (error[0] != '\0' ? error : "new map failed"));
    editor_set_string_output(scene_state, outputs, "path_key", "");
    publish_editor_level_state_outputs(runtime, outputs, world_name);
    return true;
}

bool slayer3d_game_data_save_editor_map_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    const char *path = editor_action_path(runtime, action, NULL);
    char error[256];
    error[0] = '\0';
    size_t size = 0u;
    const bool ok =
        slayer3d_game_data_save_editable_level_map_file(runtime, world_name, path, &size, error, (int)sizeof(error));

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "map saved")
                                : (error[0] != '\0' ? error : "map save failed"));
    editor_set_int_output(scene_state, outputs, "size_key",
                          ok ? (int)(size > (size_t)SDL_MAX_SINT32 ? (size_t)SDL_MAX_SINT32 : size) : 0);
    editor_set_string_output(scene_state, outputs, "path_key", ok && path != NULL ? path : "");
    publish_editor_level_state_outputs(runtime, outputs, world_name);
    return true;
}

bool slayer3d_game_data_load_editor_map_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    const char *path = editor_action_path(runtime, action, NULL);
    const bool optional = json_bool(action, "optional", false);
    char error[256];
    error[0] = '\0';

    bool ok = true;
    bool skipped = false;
    if (path == NULL || path[0] == '\0')
    {
        ok = optional;
        skipped = optional;
        if (!ok)
            SDL_snprintf(error, sizeof(error), "map load path is required");
    }
    else
    {
        ok = slayer3d_game_data_load_editable_level_map_file(runtime, world_name, path, error, (int)sizeof(error));
    }

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? (skipped ? json_string(action, "skipped_message", "no map input")
                                           : json_string(action, "message", "map loaded"))
                                : (error[0] != '\0' ? error : "map load failed"));
    editor_set_string_output(scene_state, outputs, "path_key", ok && path != NULL ? path : "");
    publish_editor_level_state_outputs(runtime, outputs, world_name);
    return true;
}

typedef struct editor_map_validation_diagnostics
{
    int warnings;
    int errors;
    char messages[3][256];
} editor_map_validation_diagnostics;

static void editor_map_validation_diagnostic(void *userdata, slayer3d_map_diagnostic_severity severity,
                                             const char *json_path, const char *message)
{
    editor_map_validation_diagnostics *diagnostics = (editor_map_validation_diagnostics *)userdata;
    if (diagnostics == NULL)
        return;
    if (severity == SLAYER3D_MAP_DIAGNOSTIC_WARNING)
        diagnostics->warnings += 1;
    else
        diagnostics->errors += 1;

    const int index = diagnostics->warnings + diagnostics->errors - 1;
    if (index < 0 || index >= (int)SDL_arraysize(diagnostics->messages))
        return;
    SDL_snprintf(diagnostics->messages[index], sizeof(diagnostics->messages[index]), "%s %s: %s",
                 severity == SLAYER3D_MAP_DIAGNOSTIC_WARNING ? "warning" : "error",
                 json_path != NULL && json_path[0] != '\0' ? json_path : "$",
                 message != NULL && message[0] != '\0' ? message : "map validation diagnostic");
}

bool slayer3d_game_data_validate_editor_map_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    char error[256];
    error[0] = '\0';
    char *json = NULL;
    size_t size = 0u;
    bool ok =
        slayer3d_game_data_export_editable_level_map_json(runtime, world_name, &json, &size, error, (int)sizeof(error));

    editor_map_validation_diagnostics diagnostics;
    SDL_zero(diagnostics);
    if (ok)
    {
        slayer3d_map_validation_options options;
        SDL_zero(options);
        options.diagnostic = editor_map_validation_diagnostic;
        options.userdata = &diagnostics;
        options.treat_warnings_as_errors = json_bool(action, "treat_warnings_as_errors", false);
        ok = slayer3d_map_validate_json(json, size, &options, error, (int)sizeof(error));
        if (!ok && diagnostics.errors == 0)
            diagnostics.errors = 1;
    }
    else
    {
        diagnostics.errors = 1;
    }

    if (diagnostics.messages[0][0] == '\0' && error[0] != '\0')
        SDL_snprintf(diagnostics.messages[0], sizeof(diagnostics.messages[0]), "error $: %s", error);

    char message[128];
    if (ok && diagnostics.warnings == 0)
        SDL_strlcpy(message, json_string(action, "message", "map validation passed"), sizeof(message));
    else if (ok)
        SDL_snprintf(message, sizeof(message), "map validation passed with %d warning%s", diagnostics.warnings,
                     diagnostics.warnings == 1 ? "" : "s");
    else
        SDL_snprintf(message, sizeof(message), "map validation failed with %d error%s", diagnostics.errors,
                     diagnostics.errors == 1 ? "" : "s");

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    editor_set_int_output(scene_state, outputs, "warning_count_key", diagnostics.warnings);
    editor_set_int_output(scene_state, outputs, "error_count_key", diagnostics.errors);
    editor_set_string_output(scene_state, outputs, "diagnostic0_key", diagnostics.messages[0]);
    editor_set_string_output(scene_state, outputs, "diagnostic1_key", diagnostics.messages[1]);
    editor_set_string_output(scene_state, outputs, "diagnostic2_key", diagnostics.messages[2]);
    editor_set_int_output(scene_state, outputs, "size_key",
                          ok ? (int)(size > (size_t)SDL_MAX_SINT32 ? (size_t)SDL_MAX_SINT32 : size) : 0);
    publish_editor_level_state_outputs(runtime, outputs, world_name);
    SDL_free(json);
    return true;
}

static slayer3d_map_lighting_build_quality editor_lighting_build_quality_from_map_preview(const char *quality)
{
    if (quality != NULL && SDL_strcmp(quality, "performance") == 0)
        return SLAYER3D_MAP_LIGHTING_BUILD_PREVIEW;
    if (quality != NULL && SDL_strcmp(quality, "quality") == 0)
        return SLAYER3D_MAP_LIGHTING_BUILD_FINAL;
    return SLAYER3D_MAP_LIGHTING_BUILD_BALANCED;
}

static const char *editor_lighting_build_quality_name(slayer3d_map_lighting_build_quality quality)
{
    switch (quality)
    {
    case SLAYER3D_MAP_LIGHTING_BUILD_PREVIEW:
        return "preview";
    case SLAYER3D_MAP_LIGHTING_BUILD_FINAL:
        return "final";
    case SLAYER3D_MAP_LIGHTING_BUILD_BALANCED:
    default:
        return "balanced";
    }
}

static int editor_size_to_int(size_t value)
{
    return value > (size_t)SDL_MAX_SINT32 ? SDL_MAX_SINT32 : (int)value;
}

bool slayer3d_game_data_plan_editor_map_lighting_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    char error[256];
    error[0] = '\0';
    char *json = NULL;
    size_t size = 0u;
    bool ok =
        slayer3d_game_data_export_editable_level_map_json(runtime, world_name, &json, &size, error, (int)sizeof(error));

    slayer3d_map_document *document = NULL;
    slayer3d_map_lighting_build_plan plan;
    SDL_zero(plan);
    slayer3d_map_global_state global;
    SDL_zero(global);
    if (ok)
        ok = slayer3d_map_load_json(json, size, NULL, &document, error, (int)sizeof(error));
    if (ok && !slayer3d_map_get_global_state(document, &global))
    {
        ok = false;
        SDL_snprintf(error, sizeof(error), "$.global: failed to read map global lighting state");
    }
    if (ok)
    {
        slayer3d_map_lighting_build_options options;
        slayer3d_map_init_lighting_build_options(&options);
        options.quality = editor_lighting_build_quality_from_map_preview(global.lighting_preview_quality);
        options.include_dynamic_preview = json_bool(action, "include_dynamic_preview", options.include_dynamic_preview);
        yyjson_val *max_dynamic_lights = obj_get(action, "max_dynamic_lights");
        if (yyjson_is_uint(max_dynamic_lights))
            options.max_dynamic_lights = (size_t)yyjson_get_uint(max_dynamic_lights);
        yyjson_val *max_static_lights = obj_get(action, "max_static_lights");
        if (yyjson_is_uint(max_static_lights))
            options.max_static_lights = (size_t)yyjson_get_uint(max_static_lights);
        ok = slayer3d_map_build_lighting_plan(document, &options, &plan, error, (int)sizeof(error));
    }

    char message[160];
    char line0[160];
    char line1[160];
    char line2[160];
    char line3[160];
    message[0] = '\0';
    line0[0] = '\0';
    line1[0] = '\0';
    line2[0] = '\0';
    line3[0] = '\0';
    if (ok)
    {
        SDL_snprintf(message, sizeof(message), "lighting plan: %llu lights, %llu runtime, %llu bake",
                     (unsigned long long)plan.total_light_count, (unsigned long long)plan.runtime_light_count,
                     (unsigned long long)plan.bake_light_count);
        SDL_snprintf(line0, sizeof(line0), "lighting plan quality %s",
                     editor_lighting_build_quality_name(plan.quality));
        SDL_snprintf(line1, sizeof(line1), "lighting plan counts total %llu dynamic %llu static %llu area %llu",
                     (unsigned long long)plan.total_light_count, (unsigned long long)plan.dynamic_light_count,
                     (unsigned long long)plan.static_light_count, (unsigned long long)plan.area_light_count);
        SDL_snprintf(line2, sizeof(line2), "lighting plan work runtime %llu/%llu bake %llu/%llu",
                     (unsigned long long)plan.runtime_light_count, (unsigned long long)plan.max_dynamic_lights,
                     (unsigned long long)plan.bake_light_count, (unsigned long long)plan.max_static_lights);
        SDL_snprintf(line3, sizeof(line3), "lighting plan %s%s%s",
                     plan.requires_static_bake ? "requires static bake" : "runtime only",
                     plan.dynamic_light_budget_exceeded ? ", runtime budget exceeded" : "",
                     plan.static_light_budget_exceeded ? ", static budget exceeded" : "");
    }
    else
    {
        SDL_snprintf(message, sizeof(message), "lighting plan failed: %s",
                     error[0] != '\0' ? error : "map lighting plan failed");
        SDL_strlcpy(line0, message, sizeof(line0));
    }

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    editor_set_string_output(scene_state, outputs, "quality_key",
                             ok ? editor_lighting_build_quality_name(plan.quality) : "");
    editor_set_int_output(scene_state, outputs, "total_count_key", ok ? editor_size_to_int(plan.total_light_count) : 0);
    editor_set_int_output(scene_state, outputs, "dynamic_count_key",
                          ok ? editor_size_to_int(plan.dynamic_light_count) : 0);
    editor_set_int_output(scene_state, outputs, "static_count_key",
                          ok ? editor_size_to_int(plan.static_light_count) : 0);
    editor_set_int_output(scene_state, outputs, "area_count_key", ok ? editor_size_to_int(plan.area_light_count) : 0);
    editor_set_int_output(scene_state, outputs, "runtime_count_key",
                          ok ? editor_size_to_int(plan.runtime_light_count) : 0);
    editor_set_int_output(scene_state, outputs, "bake_count_key", ok ? editor_size_to_int(plan.bake_light_count) : 0);
    editor_set_int_output(scene_state, outputs, "max_dynamic_key",
                          ok ? editor_size_to_int(plan.max_dynamic_lights) : 0);
    editor_set_int_output(scene_state, outputs, "max_static_key", ok ? editor_size_to_int(plan.max_static_lights) : 0);
    editor_set_bool_output(scene_state, outputs, "requires_bake_key", ok && plan.requires_static_bake);
    editor_set_bool_output(scene_state, outputs, "dynamic_preview_key", ok && plan.has_dynamic_preview);
    editor_set_bool_output(scene_state, outputs, "dynamic_budget_exceeded_key",
                           ok && plan.dynamic_light_budget_exceeded);
    editor_set_bool_output(scene_state, outputs, "static_budget_exceeded_key", ok && plan.static_light_budget_exceeded);
    editor_set_string_output(scene_state, outputs, "line0_key", line0);
    editor_set_string_output(scene_state, outputs, "line1_key", line1);
    editor_set_string_output(scene_state, outputs, "line2_key", line2);
    editor_set_string_output(scene_state, outputs, "line3_key", line3);
    publish_editor_level_state_outputs(runtime, outputs, world_name);
    slayer3d_map_destroy(document);
    SDL_free(json);
    return true;
}

bool slayer3d_game_data_prepare_editor_test_run_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_game_data_editor_test_run_desc desc;
    SDL_zero(desc);
    desc.data_asset_path = json_string(action, "data_asset", NULL);
    desc.scene = json_string(action, "scene", NULL);
    desc.player_start = json_string(action, "player_start", NULL);

    char error[256];
    error[0] = '\0';
    char *json = NULL;
    size_t size = 0u;
    const bool ok = slayer3d_game_data_export_editor_test_run_manifest_json(runtime, &desc, &json, &size, error,
                                                                            (int)sizeof(error));

    slayer3d_game_data_editor_player_start start;
    SDL_zero(start);
    const bool has_start = ok && desc.player_start != NULL && desc.player_start[0] != '\0' &&
                           slayer3d_game_data_get_editor_player_start(runtime, desc.player_start, &start);
    const char *resolved_scene = desc.scene != NULL && desc.scene[0] != '\0'
                                     ? desc.scene
                                     : (has_start && start.scene != NULL ? start.scene : "");
    char command[1024];
    command[0] = '\0';
    const bool command_ok =
        ok && format_test_run_direct_command(command, sizeof(command), action, &desc, resolved_scene);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "editor test run prepared")
                                : (error[0] != '\0' ? error : "editor test run preparation failed"));
    editor_set_int_output(scene_state, outputs, "size_key", ok ? (int)size : 0);
    editor_set_string_output(scene_state, outputs, "manifest_json_key", ok && json != NULL ? json : "");
    editor_set_string_output(scene_state, outputs, "data_asset_key", ok ? desc.data_asset_path : "");
    editor_set_string_output(scene_state, outputs, "scene_key", ok ? resolved_scene : "");
    editor_set_string_output(scene_state, outputs, "player_start_key",
                             ok && desc.player_start != NULL ? desc.player_start : "");
    editor_set_string_output(scene_state, outputs, "target_key", has_start && start.target != NULL ? start.target : "");
    editor_set_string_output(scene_state, outputs, "command_key", command_ok ? command : "");
    SDL_free(json);
    return true;
}

bool slayer3d_game_data_save_editor_test_run_manifest_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_game_data_editor_test_run_desc desc;
    SDL_zero(desc);
    desc.data_asset_path = json_string(action, "data_asset", NULL);
    desc.scene = json_string(action, "scene", NULL);
    desc.player_start = json_string(action, "player_start", NULL);
    const char *path = editor_action_path(runtime, action, NULL);

    char error[256];
    error[0] = '\0';
    char *json = NULL;
    size_t size = 0u;
    bool ok = slayer3d_game_data_export_editor_test_run_manifest_json(runtime, &desc, &json, &size, error,
                                                                      (int)sizeof(error));
    if (ok)
        ok = editor_save_bytes_atomic(path, json, size, "editor", error, (int)sizeof(error));

    slayer3d_game_data_editor_player_start start;
    SDL_zero(start);
    const bool has_start = ok && desc.player_start != NULL && desc.player_start[0] != '\0' &&
                           slayer3d_game_data_get_editor_player_start(runtime, desc.player_start, &start);
    const char *resolved_scene = desc.scene != NULL && desc.scene[0] != '\0'
                                     ? desc.scene
                                     : (has_start && start.scene != NULL ? start.scene : "");
    char command[1024];
    command[0] = '\0';
    const bool command_ok = ok && format_test_run_manifest_command(command, sizeof(command), action, path);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "editor test run manifest saved")
                                : (error[0] != '\0' ? error : "editor test run manifest save failed"));
    editor_set_int_output(scene_state, outputs, "size_key", ok ? (int)size : 0);
    editor_set_string_output(scene_state, outputs, "path_key", ok && path != NULL ? path : "");
    editor_set_string_output(scene_state, outputs, "manifest_json_key", ok && json != NULL ? json : "");
    editor_set_string_output(scene_state, outputs, "data_asset_key", ok ? desc.data_asset_path : "");
    editor_set_string_output(scene_state, outputs, "scene_key", ok ? resolved_scene : "");
    editor_set_string_output(scene_state, outputs, "player_start_key",
                             ok && desc.player_start != NULL ? desc.player_start : "");
    editor_set_string_output(scene_state, outputs, "target_key", has_start && start.target != NULL ? start.target : "");
    editor_set_string_output(scene_state, outputs, "command_key", command_ok ? command : "");
    SDL_free(json);
    return true;
}

bool slayer3d_game_data_publish_editor_brush_world_status_action(slayer3d_game_data_runtime *runtime,
                                                                 yyjson_val *action)
{
    return publish_editor_brush_world_status(runtime, obj_get(action, "outputs"), json_string(action, "world", NULL),
                                             json_string(action, "message", "brush world status published"), true);
}

bool slayer3d_game_data_validate_editor_brush_source_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    const int near_gap_units = json_int(action, "near_gap_units", 1);
    slayer3d_game_data_editor_brush_source_diagnostics diagnostics;
    char error[256];
    error[0] = '\0';
    const bool ok = slayer3d_game_data_validate_editor_brush_source_model(runtime, world_name, near_gap_units,
                                                                          &diagnostics, error, (int)sizeof(error));
    const bool valid = ok && diagnostics.structurally_valid;
    const char *message = NULL;
    if (valid && ok && diagnostics.first_issue[0] != '\0')
        message = diagnostics.first_issue;
    else if (valid)
        message = json_string(action, "message", "editor brush source model is structurally valid");
    else if (ok && diagnostics.first_issue[0] != '\0')
        message = diagnostics.first_issue;
    else
        message = error[0] != '\0' ? error : "editor brush source validation failed";

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", valid);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    editor_set_int_output(scene_state, outputs, "box_count_key", ok ? diagnostics.source_box_count : 0);
    editor_set_int_output(scene_state, outputs, "snap_units_key", ok ? diagnostics.source_snap_units : 0);
    editor_set_int_output(scene_state, outputs, "off_snap_count_key", ok ? diagnostics.off_snap_count : 0);
    editor_set_int_output(scene_state, outputs, "overlap_count_key", ok ? diagnostics.positive_overlap_count : 0);
    editor_set_int_output(scene_state, outputs, "near_gap_count_key", ok ? diagnostics.near_gap_count : 0);
    editor_set_int_output(scene_state, outputs, "face_contact_count_key", ok ? diagnostics.face_contact_count : 0);
    editor_set_int_output(scene_state, outputs, "edge_contact_count_key", ok ? diagnostics.edge_contact_count : 0);
    editor_set_int_output(scene_state, outputs, "vertex_contact_count_key", ok ? diagnostics.vertex_contact_count : 0);
    editor_set_int_output(scene_state, outputs, "partial_face_contact_count_key",
                          ok ? diagnostics.partial_face_contact_count : 0);
    editor_set_int_output(scene_state, outputs, "runtime_brush_count_key", ok ? diagnostics.runtime_brush_count : 0);
    editor_set_int_output(scene_state, outputs, "runtime_source_mismatch_count_key",
                          ok ? diagnostics.runtime_source_mismatch_count : 0);
    editor_set_int_output(scene_state, outputs, "compiled_face_count_key", ok ? diagnostics.compiled_face_count : 0);
    editor_set_int_output(scene_state, outputs, "compiled_face_missing_source_count_key",
                          ok ? diagnostics.compiled_face_missing_source_count : 0);
    editor_set_int_output(scene_state, outputs, "compiled_face_unknown_source_count_key",
                          ok ? diagnostics.compiled_face_unknown_source_count : 0);
    editor_set_string_output(scene_state, outputs, "first_issue_kind_key", ok ? diagnostics.first_issue_kind : "");
    editor_set_string_output(scene_state, outputs, "first_issue_source_name_key",
                             ok ? diagnostics.first_issue_source_name : "");
    editor_set_string_output(scene_state, outputs, "first_issue_source_stable_id_key",
                             ok ? diagnostics.first_issue_source_stable_id : "");
    editor_set_string_output(scene_state, outputs, "first_issue_related_source_name_key",
                             ok ? diagnostics.first_issue_related_source_name : "");
    editor_set_string_output(scene_state, outputs, "first_issue_related_source_stable_id_key",
                             ok ? diagnostics.first_issue_related_source_stable_id : "");
    editor_set_string_output(scene_state, outputs, "first_issue_source_face_key",
                             ok ? diagnostics.first_issue_source_face : "");
    editor_set_string_output(scene_state, outputs, "first_issue_runtime_brush_name_key",
                             ok ? diagnostics.first_issue_runtime_brush_name : "");
    editor_set_int_output(scene_state, outputs, "first_issue_runtime_brush_index_key",
                          ok ? diagnostics.first_issue_runtime_brush_index : -1);
    editor_set_int_output(scene_state, outputs, "first_issue_compiled_face_index_key",
                          ok ? diagnostics.first_issue_compiled_face_index : -1);
    (void)publish_editor_brush_world_status(runtime, outputs, world_name, NULL, false);
    return true;
}

bool slayer3d_game_data_validate_editor_brush_enclosure_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    const char *player_start_name = json_string(action, "player_start", NULL);
    const int max_cells = json_int(action, "max_cells", 0);
    slayer3d_game_data_editor_brush_enclosure_diagnostics diagnostics;
    char error[256];
    error[0] = '\0';
    const bool ok = slayer3d_game_data_validate_editor_brush_source_enclosure(
        runtime, world_name, player_start_name, max_cells, &diagnostics, error, (int)sizeof(error));
    const bool valid = ok && diagnostics.enclosed;
    const char *message = NULL;
    if (valid)
        message = json_string(action, "message", "editor brush source enclosure is sealed");
    else if (ok && diagnostics.first_issue[0] != '\0')
        message = diagnostics.first_issue;
    else
        message = error[0] != '\0' ? error : "editor brush source enclosure validation failed";

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", valid);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    editor_set_bool_output(scene_state, outputs, "has_source_key", ok ? diagnostics.has_source_model : false);
    editor_set_bool_output(scene_state, outputs, "has_player_start_key", ok ? diagnostics.has_player_start : false);
    editor_set_int_output(scene_state, outputs, "box_count_key", ok ? diagnostics.source_box_count : 0);
    editor_set_int_output(scene_state, outputs, "grid_cell_count_key", ok ? diagnostics.grid_cell_count : 0);
    editor_set_int_output(scene_state, outputs, "solid_cell_count_key", ok ? diagnostics.solid_cell_count : 0);
    editor_set_int_output(scene_state, outputs, "visited_cell_count_key", ok ? diagnostics.visited_cell_count : 0);
    editor_set_int_output(scene_state, outputs, "open_boundary_count_key",
                          ok ? diagnostics.open_boundary_cell_count : 0);
    editor_set_vec3_output(scene_state, outputs, "leak_point_key",
                           ok ? diagnostics.first_leak_point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_string_output(scene_state, outputs, "leak_axis_key", ok ? diagnostics.first_leak_axis : "");
    editor_set_string_output(scene_state, outputs, "leak_side_key", ok ? diagnostics.first_leak_side : "");
    editor_set_string_output(scene_state, outputs, "candidate_name_key", ok ? diagnostics.candidate_source_name : "");
    editor_set_string_output(scene_state, outputs, "candidate_stable_id_key",
                             ok ? diagnostics.candidate_source_stable_id : "");
    editor_set_string_output(scene_state, outputs, "candidate_face_key", ok ? diagnostics.candidate_source_face : "");
    editor_set_vec3_output(scene_state, outputs, "candidate_point_key",
                           ok ? diagnostics.candidate_source_point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_float_output(scene_state, outputs, "candidate_distance_key",
                            ok ? diagnostics.candidate_source_distance : 0.0f);
    (void)publish_editor_brush_world_status(runtime, outputs, world_name, NULL, false);
    return true;
}

bool slayer3d_game_data_place_editor_player_start_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_game_data_place_player_start_desc desc;
    SDL_zero(desc);
    desc.name = json_string(action, "name", NULL);
    desc.scene = json_string(action, "scene", NULL);
    desc.target = json_string(action, "target", NULL);
    yyjson_val *position = obj_get(action, "position");
    const char *position_from = json_string(action, "position_from", NULL);
    desc.has_position = position != NULL && position_from == NULL;
    desc.position = json_vec3(action, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    char error[256];
    error[0] = '\0';
    if (position_from != NULL && SDL_strcmp(position_from, "placement_preview") == 0)
    {
        if (!editor_placement_preview_active_for_scene(runtime))
        {
            set_error(error, (int)sizeof(error), "player start placement requires an active placement preview");
        }
        else
        {
            const editor_placement_preview_state *preview = &runtime->editor_placement_preview;
            const char *preview_mode = json_string(action, "preview_mode", NULL);
            if (preview_mode != NULL && preview->mode != NULL && SDL_strcmp(preview_mode, preview->mode) != 0)
                set_error(error, (int)sizeof(error), "player start placement preview mode does not match action");
            else
            {
                desc.has_position = true;
                desc.position = preview->anchor;
            }
        }
    }
    yyjson_val *yaw = obj_get(action, "yaw");
    yyjson_val *pitch = obj_get(action, "pitch");
    desc.has_yaw = yaw != NULL;
    desc.yaw = json_float(action, "yaw", 0.0f);
    desc.has_pitch = pitch != NULL;
    desc.pitch = json_float(action, "pitch", 0.0f);
    desc.apply_to_target = json_bool(action, "apply_to_target", true);

    const bool ok =
        error[0] == '\0' && slayer3d_game_data_place_editor_player_start(runtime, &desc, error, (int)sizeof(error));
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "player start placed")
                                : (error[0] != '\0' ? error : "player start placement failed"));
    publish_editor_player_start_state(runtime, outputs, ok ? find_editor_player_start(runtime, desc.name) : NULL);
    return true;
}

bool slayer3d_game_data_apply_editor_player_start_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *name = json_string(action, "name", NULL);
    char error[256];
    error[0] = '\0';
    const bool ok = slayer3d_game_data_apply_editor_player_start(runtime, name, error, (int)sizeof(error));
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "player start applied")
                                : (error[0] != '\0' ? error : "player start apply failed"));
    publish_editor_player_start_state(runtime, outputs, ok ? find_editor_player_start(runtime, name) : NULL);
    return true;
}

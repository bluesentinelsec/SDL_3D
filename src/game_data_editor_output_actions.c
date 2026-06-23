#include "game_data_internal.h"

#include <SDL3/SDL_log.h>

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

void editor_publish_console_message(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL || message == NULL || message[0] == '\0')
        return;

    const char *old_line0 = slayer3d_properties_get_string(runtime->scene_state, "editor.console.line0", "");
    const char *old_line1 = slayer3d_properties_get_string(runtime->scene_state, "editor.console.line1", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.console.line2", old_line1);
    slayer3d_properties_set_string(runtime->scene_state, "editor.console.line1", old_line0);
    slayer3d_properties_set_string(runtime->scene_state, "editor.console.line0", message);
    slayer3d_properties_set_int(runtime->scene_state, "editor.console.count",
                                slayer3d_properties_get_int(runtime->scene_state, "editor.console.count", 0) + 1);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
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

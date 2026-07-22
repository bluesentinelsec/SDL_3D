/**
 * @file game_data_validation_scenes.c
 * @brief Scene and menu validation for JSON-authored game data.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_standard_options.h"
#include "slayer3d/game_presentation.h"

#define GAME_DATA_MENU_TEXT_MAX_BYTES 255

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static bool is_non_empty_string(yyjson_val *object, const char *key)
{
    const char *value = json_string(object, key);
    return value != NULL && value[0] != '\0';
}

static int scene_json_int(yyjson_val *object, const char *key, int fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_int(value) ? (int)yyjson_get_sint(value) : fallback;
}

static bool validate_scene_world_viewport_docks(validation_context *ctx, yyjson_val *value, const char *path)
{
    if (value == NULL || yyjson_is_bool(value))
        return true;
    if (!yyjson_is_arr(value))
        return validation_error(ctx, path, "avoid_docked_ui must be a boolean or dock-name array");
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        const char *dock = yyjson_is_str(entry) ? yyjson_get_str(entry) : NULL;
        if (dock == NULL ||
            (SDL_strcmp(dock, "left") != 0 && SDL_strcmp(dock, "right") != 0 && SDL_strcmp(dock, "bottom") != 0))
        {
            return validation_error(ctx, path, "avoid_docked_ui entries must be left, right, or bottom");
        }
    }
    return true;
}

static bool validate_timeline_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                     validation_names *names)
{
    if (!yyjson_is_obj(action))
        return validation_error(ctx, json_path, "timeline action must be an object");
    const char *type = json_string(action, "type");
    if (SDL_strcmp(type != NULL ? type : "", "scene.request") == 0)
        return require_ref(ctx, &names->scenes, "scene", json_string(action, "scene"), json_path);
    return validate_one_action(ctx, action, json_path, names);
}

static bool validate_skip_policy(validation_context *ctx, yyjson_val *policy, const char *json_path,
                                 validation_names *names)
{
    if (policy == NULL)
        return true;
    if (!yyjson_is_obj(policy))
        return validation_error(ctx, json_path, "scene skip_policy must be an object");

    yyjson_val *enabled = obj_get(policy, "enabled");
    if (enabled != NULL && !yyjson_is_bool(enabled))
        return validation_error(ctx, json_path, "skip_policy enabled must be a boolean");
    if (yyjson_is_bool(enabled) && !yyjson_get_bool(enabled))
        return true;

    const char *input = json_string(policy, "input");
    const bool input_missing = input == NULL || input[0] == '\0';
    const bool has_action = json_string(policy, "action") != NULL;
    const bool input_any = (input_missing && !has_action) || SDL_strcmp(input != NULL ? input : "", "any") == 0 ||
                           SDL_strcmp(input != NULL ? input : "", "any_input") == 0;
    const bool input_action = SDL_strcmp(input != NULL ? input : "", "action") == 0 || (input_missing && has_action);
    const bool input_disabled =
        SDL_strcmp(input != NULL ? input : "", "none") == 0 || SDL_strcmp(input != NULL ? input : "", "disabled") == 0;
    if (!input_any && !input_action && !input_disabled)
        return validation_error(ctx, json_path, "skip_policy input must be any, any_input, action, none, or disabled");

    yyjson_val *preserve = obj_get(policy, "preserve_exit_transition");
    if (preserve != NULL && !yyjson_is_bool(preserve))
        return validation_error(ctx, json_path, "skip_policy preserve_exit_transition must be a boolean");
    yyjson_val *consume = obj_get(policy, "consume_input");
    if (consume != NULL && !yyjson_is_bool(consume))
        return validation_error(ctx, json_path, "skip_policy consume_input must be a boolean");
    yyjson_val *block_menus = obj_get(policy, "block_menus");
    if (block_menus != NULL && !yyjson_is_bool(block_menus))
        return validation_error(ctx, json_path, "skip_policy block_menus must be a boolean");
    yyjson_val *block_shortcuts = obj_get(policy, "block_scene_shortcuts");
    if (block_shortcuts != NULL && !yyjson_is_bool(block_shortcuts))
        return validation_error(ctx, json_path, "skip_policy block_scene_shortcuts must be a boolean");
    block_shortcuts = obj_get(policy, "block_shortcuts");
    if (block_shortcuts != NULL && !yyjson_is_bool(block_shortcuts))
        return validation_error(ctx, json_path, "skip_policy block_shortcuts must be a boolean");

    if (input_disabled)
        return true;

    const char *scene = json_string(policy, "scene");
    if (scene == NULL)
        scene = json_string(policy, "target_scene");
    if (!require_ref(ctx, &names->scenes, "scene", scene, json_path))
        return false;
    if (input_action && !require_ref(ctx, &names->actions, "input action", json_string(policy, "action"), json_path))
        return false;
    return true;
}

static bool validate_scene_activity(validation_context *ctx, yyjson_val *activity, const char *json_path,
                                    validation_names *names)
{
    if (activity == NULL)
        return true;
    if (!yyjson_is_obj(activity))
        return validation_error(ctx, json_path, "scene activity must be an object");

    yyjson_val *enabled = obj_get(activity, "enabled");
    if (enabled != NULL && !yyjson_is_bool(enabled))
        return validation_error(ctx, json_path, "scene activity enabled must be a boolean");
    yyjson_val *idle_after = obj_get(activity, "idle_after");
    if (idle_after == NULL)
        idle_after = obj_get(activity, "idle_seconds");
    if (idle_after != NULL && (!yyjson_is_num(idle_after) || yyjson_get_num(idle_after) < 0.0))
        return validation_error(ctx, json_path, "scene activity idle_after must be a non-negative number");
    yyjson_val *reset_periodic = obj_get(activity, "reset_periodic_on_input");
    if (reset_periodic != NULL && !yyjson_is_bool(reset_periodic))
        return validation_error(ctx, json_path, "scene activity reset_periodic_on_input must be a boolean");
    yyjson_val *consume_wake = obj_get(activity, "consume_wake_input");
    if (consume_wake != NULL && !yyjson_is_bool(consume_wake))
        return validation_error(ctx, json_path, "scene activity consume_wake_input must be a boolean");
    yyjson_val *block_menus = obj_get(activity, "block_menus_on_wake");
    if (block_menus != NULL && !yyjson_is_bool(block_menus))
        return validation_error(ctx, json_path, "scene activity block_menus_on_wake must be a boolean");
    yyjson_val *block_shortcuts = obj_get(activity, "block_scene_shortcuts_on_wake");
    if (block_shortcuts != NULL && !yyjson_is_bool(block_shortcuts))
        return validation_error(ctx, json_path, "scene activity block_scene_shortcuts_on_wake must be a boolean");

    const char *input = json_string(activity, "input");
    if (input != NULL && SDL_strcmp(input, "any") != 0 && SDL_strcmp(input, "action") != 0 &&
        SDL_strcmp(input, "disabled") != 0 && SDL_strcmp(input, "none") != 0)
        return validation_error(ctx, json_path, "scene activity input must be any, action, disabled, or none");
    if (input != NULL && SDL_strcmp(input, "action") == 0 &&
        !require_ref(ctx, &names->actions, "input action", json_string(activity, "action"), json_path))
        return false;

    const char *action_lists[] = {"on_enter", "on_idle", "on_active"};
    for (size_t i = 0; i < SDL_arraysize(action_lists); ++i)
    {
        yyjson_val *actions = obj_get(activity, action_lists[i]);
        if (actions == NULL)
            continue;
        char action_path[PATH_BUFFER_SIZE];
        format_path(action_path, sizeof(action_path), "%s.%s", json_path, action_lists[i]);
        if (!validate_action_array(ctx, actions, action_path, names))
            return false;
    }

    yyjson_val *periodic = obj_get(activity, "periodic");
    if (periodic != NULL && !yyjson_is_arr(periodic))
        return validation_error(ctx, json_path, "scene activity periodic must be an array");
    for (size_t i = 0; yyjson_is_arr(periodic) && i < yyjson_arr_size(periodic); ++i)
    {
        char entry_path[PATH_BUFFER_SIZE];
        format_path(entry_path, sizeof(entry_path), "%s.periodic[%zu]", json_path, i);
        yyjson_val *entry = yyjson_arr_get(periodic, i);
        if (!yyjson_is_obj(entry))
            return validation_error(ctx, entry_path, "scene activity periodic entry must be an object");
        yyjson_val *interval = obj_get(entry, "interval");
        if (!yyjson_is_num(interval) || yyjson_get_num(interval) <= 0.0)
            return validation_error(ctx, entry_path, "scene activity periodic interval must be positive");
        yyjson_val *reset_idle = obj_get(entry, "reset_idle");
        if (reset_idle != NULL && !yyjson_is_bool(reset_idle))
            return validation_error(ctx, entry_path, "scene activity periodic reset_idle must be a boolean");

        char actions_path[PATH_BUFFER_SIZE];
        format_path(actions_path, sizeof(actions_path), "%s.actions", entry_path);
        if (!validate_action_array(ctx, obj_get(entry, "actions"), actions_path, names))
            return false;
    }
    return true;
}

typedef struct validation_scene_doc
{
    yyjson_doc *doc;
    yyjson_val *root;
} validation_scene_doc;

static void validation_scene_docs_destroy(validation_scene_doc *docs, int count)
{
    if (docs == NULL)
        return;
    for (int i = 0; i < count; ++i)
        yyjson_doc_free(docs[i].doc);
    SDL_free(docs);
}

static bool validate_scene_file(validation_context *ctx, validation_names *names, const char *scene_path,
                                int scene_index, validation_scene_doc *out_doc)
{
    char path[PATH_BUFFER_SIZE];
    format_path(path, sizeof(path), "$.scenes.files[%d]", scene_index);
    if (scene_path == NULL || scene_path[0] == '\0')
        return validation_error(ctx, path, "scene file entries must be non-empty strings");

    char *resolved = validation_path_join(ctx->base_dir, scene_path);
    if (resolved == NULL)
        return validation_error(ctx, path, "failed to resolve scene file '%s'", scene_path);

    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    const bool read_ok =
        ctx->assets != NULL &&
        slayer3d_asset_resolver_read_file(ctx->assets, resolved, &buffer, asset_error, (int)sizeof(asset_error));
    SDL_free(resolved);
    if (!read_ok)
        return validation_error(ctx, path, "scene asset '%s' does not exist or cannot be read", scene_path);

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    slayer3d_asset_buffer_free(&buffer);
    if (doc == NULL)
    {
        return validation_error(ctx, path, "scene yyjson error %u at byte %llu: %s", err.code,
                                (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
    }

    yyjson_val *scene_root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(scene_root) ||
        SDL_strcmp(json_string(scene_root, "schema") != NULL ? json_string(scene_root, "schema") : "",
                   "slayer3d.scene.v0") != 0)
    {
        yyjson_doc_free(doc);
        return validation_error(ctx, path, "scene file must use schema slayer3d.scene.v0");
    }

    const char *name = json_string(scene_root, "name");
    if (!require_unique_name(ctx, &names->scenes, "scene", name, path))
    {
        yyjson_doc_free(doc);
        return false;
    }

    out_doc->doc = doc;
    out_doc->root = scene_root;
    return true;
}

static const char *scene_file_entry_package(yyjson_val *entry)
{
    yyjson_val *package = obj_get(entry, "package");
    return yyjson_is_str(package) ? yyjson_get_str(package) : NULL;
}

static bool validate_generated_scene_doc(validation_context *ctx, validation_names *names, yyjson_doc *doc,
                                         const char *json_path, validation_scene_doc *out_doc)
{
    yyjson_val *scene_root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(scene_root) ||
        SDL_strcmp(json_string(scene_root, "schema") != NULL ? json_string(scene_root, "schema") : "",
                   "slayer3d.scene.v0") != 0)
    {
        yyjson_doc_free(doc);
        return validation_error(ctx, json_path, "generated scene must use schema slayer3d.scene.v0");
    }

    const char *name = json_string(scene_root, "name");
    if (!require_unique_name(ctx, &names->scenes, "scene", name, json_path))
    {
        yyjson_doc_free(doc);
        return false;
    }

    out_doc->doc = doc;
    out_doc->root = scene_root;
    return true;
}

static int scene_source_doc_capacity(yyjson_val *files)
{
    int count = 0;
    for (size_t i = 0; yyjson_is_arr(files) && i < yyjson_arr_size(files); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(files, i);
        if (yyjson_is_str(entry))
        {
            count++;
            continue;
        }

        const char *package = scene_file_entry_package(entry);
        if (package != NULL && SDL_strcmp(package, "standard_options") == 0)
        {
            count += SLAYER3D_STANDARD_OPTIONS_SCENE_COUNT;
            continue;
        }

        return -1;
    }
    return count;
}

static bool validate_scene_ui_condition(validation_context *ctx, yyjson_val *condition, const char *path,
                                        validation_names *names)
{
    if (condition == NULL)
        return true;
    const char *type = json_string(condition, "type");
    if (SDL_strcmp(type != NULL ? type : "", "menu.selected") == 0)
    {
        if (!is_non_empty_string(condition, "menu"))
            return validation_error(ctx, path, "menu.selected condition requires a menu name");
        if (!yyjson_is_int(obj_get(condition, "index")))
            return validation_error(ctx, path, "menu.selected condition requires an integer index");
        return true;
    }
    return validate_data_condition(ctx, condition, path, names);
}

static bool validate_menu_item_control(validation_context *ctx, yyjson_val *control, const char *path,
                                       validation_names *names)
{
    if (control == NULL)
        return true;
    if (!yyjson_is_obj(control))
        return validation_error(ctx, path, "menu item control must be an object");

    const char *type = json_string(control, "type");
    if (type == NULL ||
        (SDL_strcmp(type, "toggle") != 0 && SDL_strcmp(type, "choice") != 0 && SDL_strcmp(type, "range") != 0 &&
         SDL_strcmp(type, "input_binding") != 0 && SDL_strcmp(type, "text") != 0))
        return validation_error(ctx, path,
                                "menu item control requires type toggle, choice, range, input_binding, or text");

    if (SDL_strcmp(type, "input_binding") != 0)
    {
        const char *target = json_string(control, "target");
        if (target != NULL && SDL_strcmp(target, "scene_state") != 0 &&
            !require_ref(ctx, &names->entities, "entity", target, path))
            return false;
        if (!is_non_empty_string(control, "key"))
            return validation_error(ctx, path, "menu item control requires a non-empty key");
    }

    if (SDL_strcmp(type, "choice") == 0)
    {
        yyjson_val *choices = obj_get(control, "choices");
        if (!yyjson_is_arr(choices) || yyjson_arr_size(choices) == 0)
            return validation_error(ctx, path, "choice control requires at least one choice");
    }
    if (SDL_strcmp(type, "range") == 0)
    {
        if (!yyjson_is_num(obj_get(control, "min")) || !yyjson_is_num(obj_get(control, "max")) ||
            !yyjson_is_num(obj_get(control, "step")))
            return validation_error(ctx, path, "range control requires numeric min, max, and step");
    }
    if (SDL_strcmp(type, "input_binding") == 0)
    {
        yyjson_val *bindings = obj_get(control, "bindings");
        if (!yyjson_is_arr(bindings) || yyjson_arr_size(bindings) == 0)
            return validation_error(ctx, path, "input_binding control requires at least one binding");
        if (!is_non_empty_string(control, "default"))
            return validation_error(ctx, path, "input_binding control requires a default input");
        for (size_t i = 0; i < yyjson_arr_size(bindings); ++i)
        {
            yyjson_val *binding = yyjson_arr_get(bindings, i);
            if (!require_ref(ctx, &names->actions, "input action", json_string(binding, "action"), path))
                return false;
            const char *device = json_string(binding, "device");
            if (device != NULL && SDL_strcmp(device, "keyboard") != 0 && SDL_strcmp(device, "gamepad") != 0 &&
                SDL_strcmp(device, "mouse") != 0)
                return validation_error(ctx, path,
                                        "input_binding controls support keyboard, mouse, or gamepad bindings");
        }
    }
    if (SDL_strcmp(type, "text") == 0)
    {
        yyjson_val *max_length = obj_get(control, "max_length");
        if (max_length != NULL && (!yyjson_is_int(max_length) || yyjson_get_sint(max_length) < 0))
            return validation_error(ctx, path, "text control max_length must be a non-negative integer");
        if (max_length != NULL && yyjson_get_sint(max_length) > GAME_DATA_MENU_TEXT_MAX_BYTES)
            return validation_error(ctx, path, "text control max_length must be 255 bytes or fewer");
        if (obj_get(control, "default") != NULL && !yyjson_is_str(obj_get(control, "default")))
            return validation_error(ctx, path, "text control default must be a string");
        if (obj_get(control, "placeholder") != NULL && !yyjson_is_str(obj_get(control, "placeholder")))
            return validation_error(ctx, path, "text control placeholder must be a string");
        const char *charset = json_string(control, "charset");
        if (charset == NULL)
            charset = json_string(control, "allow");
        if (charset != NULL && SDL_strcmp(charset, "text") != 0 && SDL_strcmp(charset, "utf8") != 0 &&
            SDL_strcmp(charset, "ascii") != 0 && SDL_strcmp(charset, "integer") != 0 &&
            SDL_strcmp(charset, "digits") != 0 && SDL_strcmp(charset, "numeric") != 0 &&
            SDL_strcmp(charset, "hostname") != 0)
            return validation_error(ctx, path,
                                    "text control charset must be text, utf8, ascii, integer, digits, numeric, or "
                                    "hostname");
    }
    return true;
}

static bool dynamic_list_key_format_valid(const char *format)
{
    if (format == NULL || format[0] == '\0')
        return false;

    const char *placeholder = SDL_strstr(format, "%d");
    if (placeholder == NULL)
        return false;
    for (const char *scan = format; *scan != '\0'; ++scan)
    {
        if (*scan != '%')
            continue;
        if (scan == placeholder)
        {
            ++scan;
            continue;
        }
        return false;
    }
    return SDL_strstr(placeholder + 2, "%d") == NULL;
}

static bool validate_dynamic_menu_list_source(validation_context *ctx, yyjson_val *source, const char *path)
{
    if (!yyjson_is_obj(source))
        return validation_error(ctx, path, "dynamic_list menu item requires a source object");
    const char *type = json_string(source, "type");
    if (type != NULL && SDL_strcmp(type, "scene_state_indexed") == 0)
    {
        if (!is_non_empty_string(source, "count_key"))
            return validation_error(ctx, path, "dynamic_list source requires a non-empty count_key");
        if (!is_non_empty_string(source, "label_key_format"))
            return validation_error(ctx, path, "dynamic_list source requires a non-empty label_key_format");
        if (!dynamic_list_key_format_valid(json_string(source, "label_key_format")))
            return validation_error(ctx, path,
                                    "dynamic_list source label_key_format must contain exactly one %%d token");
        yyjson_val *value_format = obj_get(source, "value_key_format");
        if (value_format != NULL && (!yyjson_is_str(value_format) || yyjson_get_str(value_format)[0] == '\0'))
            return validation_error(ctx, path, "dynamic_list source value_key_format must be a non-empty string");
        if (value_format != NULL && !dynamic_list_key_format_valid(yyjson_get_str(value_format)))
            return validation_error(ctx, path,
                                    "dynamic_list source value_key_format must contain exactly one %%d token");
        return true;
    }
    if (type != NULL && SDL_strcmp(type, "runtime_collection") == 0)
    {
        if (!is_non_empty_string(source, "collection"))
            return validation_error(ctx, path,
                                    "dynamic_list runtime_collection source requires a non-empty collection");
        if (!is_non_empty_string(source, "label_field"))
            return validation_error(ctx, path,
                                    "dynamic_list runtime_collection source requires a non-empty label_field");
        yyjson_val *value_field = obj_get(source, "value_field");
        if (value_field != NULL && (!yyjson_is_str(value_field) || yyjson_get_str(value_field)[0] == '\0'))
            return validation_error(ctx, path, "dynamic_list runtime_collection source value_field must be non-empty");
        return true;
    }
    return validation_error(ctx, path, "dynamic_list source type must be scene_state_indexed or runtime_collection");
}

static bool validate_dynamic_menu_list_item(validation_context *ctx, yyjson_val *item, const char *path,
                                            validation_names *names)
{
    if (!is_non_empty_string(item, "name"))
        return validation_error(ctx, path, "dynamic_list menu item requires a non-empty name");
    char source_path[PATH_BUFFER_SIZE];
    format_path(source_path, sizeof(source_path), "%s.source", path);
    if (!validate_dynamic_menu_list_source(ctx, obj_get(item, "source"), source_path))
        return false;

    yyjson_val *empty_label = obj_get(item, "empty_label");
    if (empty_label != NULL && !yyjson_is_str(empty_label))
        return validation_error(ctx, path, "dynamic_list empty_label must be a string");
    yyjson_val *label_format = obj_get(item, "label_format");
    if (label_format != NULL && !yyjson_is_str(label_format))
        return validation_error(ctx, path, "dynamic_list label_format must be a string");
    yyjson_val *selected_index_key = obj_get(item, "selected_index_key");
    if (selected_index_key != NULL &&
        (!yyjson_is_str(selected_index_key) || yyjson_get_str(selected_index_key)[0] == '\0'))
        return validation_error(ctx, path, "dynamic_list selected_index_key must be a non-empty string");
    yyjson_val *selected_value_key = obj_get(item, "selected_value_key");
    if (selected_value_key != NULL &&
        (!yyjson_is_str(selected_value_key) || yyjson_get_str(selected_value_key)[0] == '\0'))
        return validation_error(ctx, path, "dynamic_list selected_value_key must be a non-empty string");

    const char *scene = json_string(item, "scene");
    if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, path))
        return false;
    const char *return_to = json_string(item, "return_to");
    if (return_to != NULL && !require_ref(ctx, &names->scenes, "scene", return_to, path))
        return false;
    const char *signal = json_string(item, "signal");
    if (signal != NULL && !require_ref(ctx, &names->signals, "signal", signal, path))
        return false;
    yyjson_val *scene_state = obj_get(item, "scene_state");
    if (scene_state != NULL)
    {
        if (!yyjson_is_obj(scene_state))
            return validation_error(ctx, path, "dynamic_list scene_state must be an object");
        if (!is_non_empty_string(scene_state, "key"))
            return validation_error(ctx, path, "dynamic_list scene_state requires a non-empty key");
        const char *value = json_string(scene_state, "value");
        const char *value_from = json_string(scene_state, "value_from");
        if (value != NULL && value_from != NULL)
            return validation_error(ctx, path, "dynamic_list scene_state may use value or value_from, not both");
        if (value == NULL && value_from == NULL)
            return validation_error(ctx, path, "dynamic_list scene_state requires value or value_from");
        if (value != NULL && value[0] == '\0')
            return validation_error(ctx, path, "dynamic_list scene_state value must be non-empty");
        if (value_from != NULL && SDL_strcmp(value_from, "value") != 0 && SDL_strcmp(value_from, "label") != 0 &&
            SDL_strcmp(value_from, "index") != 0)
            return validation_error(ctx, path, "dynamic_list scene_state value_from must be value, label, or index");
    }
    yyjson_val *return_scene = obj_get(item, "return_scene");
    if (return_scene != NULL && !yyjson_is_bool(return_scene))
        return validation_error(ctx, path, "dynamic_list return_scene must be a boolean");
    yyjson_val *return_paused = obj_get(item, "return_paused");
    if (return_paused != NULL && !yyjson_is_bool(return_paused))
        return validation_error(ctx, path, "dynamic_list return_paused must be a boolean");
    const char *pause = json_string(item, "pause");
    if (pause != NULL && SDL_strcmp(pause, "pause") != 0 && SDL_strcmp(pause, "resume") != 0 &&
        SDL_strcmp(pause, "unpause") != 0 && SDL_strcmp(pause, "toggle") != 0)
        return validation_error(ctx, path, "dynamic_list pause must be pause, resume, unpause, or toggle");
    return true;
}

static bool scene_has_menu_name(yyjson_val *scene_root, const char *name)
{
    yyjson_val *menus = obj_get(scene_root, "menus");
    for (size_t i = 0; name != NULL && yyjson_is_arr(menus) && i < yyjson_arr_size(menus); ++i)
    {
        yyjson_val *menu = yyjson_arr_get(menus, i);
        const char *menu_name = json_string(menu, "name");
        if (menu_name != NULL && SDL_strcmp(menu_name, name) == 0)
            return true;
    }
    return false;
}

static bool validate_scene_sector_levels(validation_context *ctx, yyjson_val *scene_root, const char *json_path,
                                         validation_names *names)
{
    yyjson_val *world = obj_get(scene_root, "world");
    if (world == NULL)
        return true;
    if (!yyjson_is_obj(world))
        return validation_error(ctx, json_path, "scene world must be an object");

    yyjson_val *sector_levels = obj_get(world, "sector_levels");
    if (sector_levels == NULL)
        return true;
    char levels_path[PATH_BUFFER_SIZE];
    format_path(levels_path, sizeof(levels_path), "%s.world.sector_levels", json_path);
    if (!yyjson_is_arr(sector_levels))
        return validation_error(ctx, levels_path, "scene world.sector_levels must be an array");

    for (size_t i = 0; i < yyjson_arr_size(sector_levels); ++i)
    {
        char entry_path[PATH_BUFFER_SIZE];
        format_path(entry_path, sizeof(entry_path), "%s[%zu]", levels_path, i);
        yyjson_val *entry = yyjson_arr_get(sector_levels, i);
        if (!yyjson_is_obj(entry))
            return validation_error(ctx, entry_path, "scene sector level entries must be objects");
        if (!require_ref(ctx, &names->sector_levels, "sector level", json_string(entry, "level"), entry_path))
            return false;

        const char *variant = json_string(entry, "variant");
        if (variant != NULL && SDL_strcmp(variant, "lightmapped") != 0 && SDL_strcmp(variant, "vertex_baked") != 0 &&
            SDL_strcmp(variant, "unlit") != 0)
        {
            return validation_error(ctx, entry_path,
                                    "scene sector level variant must be lightmapped, vertex_baked, or unlit");
        }
        yyjson_val *variant_key = obj_get(entry, "variant_key");
        if (variant_key != NULL && !is_non_empty_string(entry, "variant_key"))
            return validation_error(ctx, entry_path, "scene sector level variant_key must be non-empty");
        yyjson_val *position = obj_get(entry, "position");
        if (position != NULL && !is_exact_vec_array(position, 3))
            return validation_error(ctx, entry_path, "scene sector level position must be a vec3 array");
        yyjson_val *portal_culling = obj_get(entry, "portal_culling");
        if (portal_culling != NULL && !yyjson_is_bool(portal_culling))
            return validation_error(ctx, entry_path, "scene sector level portal_culling must be a boolean");
        yyjson_val *portal_culling_key = obj_get(entry, "portal_culling_key");
        if (portal_culling_key != NULL && !is_non_empty_string(entry, "portal_culling_key"))
            return validation_error(ctx, entry_path, "scene sector level portal_culling_key must be non-empty");
        yyjson_val *sector_lighting = obj_get(entry, "sector_lighting");
        if (sector_lighting != NULL && !yyjson_is_bool(sector_lighting))
            return validation_error(ctx, entry_path, "scene sector level sector_lighting must be a boolean");
        yyjson_val *sector_lighting_key = obj_get(entry, "sector_lighting_key");
        if (sector_lighting_key != NULL && !is_non_empty_string(entry, "sector_lighting_key"))
            return validation_error(ctx, entry_path, "scene sector level sector_lighting_key must be non-empty");
    }
    return true;
}

static bool validate_scene_brush_worlds(validation_context *ctx, yyjson_val *scene_root, const char *json_path,
                                        validation_names *names)
{
    yyjson_val *world = obj_get(scene_root, "world");
    if (world == NULL)
        return true;
    if (!yyjson_is_obj(world))
        return validation_error(ctx, json_path, "scene world must be an object");

    yyjson_val *brush_worlds = obj_get(world, "brush_worlds");
    if (brush_worlds == NULL)
        return true;
    char worlds_path[PATH_BUFFER_SIZE];
    format_path(worlds_path, sizeof(worlds_path), "%s.world.brush_worlds", json_path);
    if (!yyjson_is_arr(brush_worlds))
        return validation_error(ctx, worlds_path, "scene world.brush_worlds must be an array");

    for (size_t i = 0; i < yyjson_arr_size(brush_worlds); ++i)
    {
        char entry_path[PATH_BUFFER_SIZE];
        format_path(entry_path, sizeof(entry_path), "%s[%zu]", worlds_path, i);
        yyjson_val *entry = yyjson_arr_get(brush_worlds, i);
        if (!yyjson_is_obj(entry))
            return validation_error(ctx, entry_path, "scene brush world entries must be objects");
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(entry, "world"), entry_path))
            return false;

        yyjson_val *position = obj_get(entry, "position");
        if (position != NULL && !is_exact_vec_array(position, 3))
            return validation_error(ctx, entry_path, "scene brush world position must be a vec3 array");
        yyjson_val *acceleration = obj_get(entry, "acceleration");
        if (acceleration != NULL && !yyjson_is_bool(acceleration))
            return validation_error(ctx, entry_path, "scene brush world acceleration must be a boolean");
        yyjson_val *lighting = obj_get(entry, "lighting");
        if (lighting != NULL && !yyjson_is_bool(lighting))
            return validation_error(ctx, entry_path, "scene brush world lighting must be a boolean");
        yyjson_val *debug_wireframe = obj_get(entry, "debug_wireframe");
        if (debug_wireframe != NULL && !yyjson_is_bool(debug_wireframe))
            return validation_error(ctx, entry_path, "scene brush world debug_wireframe must be a boolean");
        yyjson_val *visibility_occlusion = obj_get(entry, "visibility_occlusion");
        if (visibility_occlusion != NULL && !yyjson_is_bool(visibility_occlusion))
            return validation_error(ctx, entry_path, "scene brush world visibility_occlusion must be a boolean");
        yyjson_val *acceleration_key = obj_get(entry, "acceleration_key");
        if (acceleration_key != NULL && !is_non_empty_string(entry, "acceleration_key"))
            return validation_error(ctx, entry_path, "scene brush world acceleration_key must be non-empty");
        yyjson_val *lighting_key = obj_get(entry, "lighting_key");
        if (lighting_key != NULL && !is_non_empty_string(entry, "lighting_key"))
            return validation_error(ctx, entry_path, "scene brush world lighting_key must be non-empty");
        yyjson_val *debug_wireframe_key = obj_get(entry, "debug_wireframe_key");
        if (debug_wireframe_key != NULL && !is_non_empty_string(entry, "debug_wireframe_key"))
            return validation_error(ctx, entry_path, "scene brush world debug_wireframe_key must be non-empty");
        yyjson_val *visibility_occlusion_key = obj_get(entry, "visibility_occlusion_key");
        if (visibility_occlusion_key != NULL && !is_non_empty_string(entry, "visibility_occlusion_key"))
            return validation_error(ctx, entry_path, "scene brush world visibility_occlusion_key must be non-empty");
    }
    return true;
}

static bool validate_scene_skybox(validation_context *ctx, yyjson_val *scene_root, const char *json_path,
                                  validation_names *names)
{
    yyjson_val *world = obj_get(scene_root, "world");
    if (world == NULL)
        return true;
    if (!yyjson_is_obj(world))
        return validation_error(ctx, json_path, "scene world must be an object");

    yyjson_val *skybox = obj_get(world, "skybox");
    if (skybox == NULL)
        return true;

    char skybox_path[PATH_BUFFER_SIZE];
    format_path(skybox_path, sizeof(skybox_path), "%s.world.skybox", json_path);
    if (!yyjson_is_obj(skybox))
        return validation_error(ctx, skybox_path, "scene world.skybox must be an object");

    const char *mode = json_string(skybox, "mode");
    if (mode != NULL && SDL_strcmp(mode, "none") != 0 && SDL_strcmp(mode, "sphere") != 0 &&
        SDL_strcmp(mode, "cubemap") != 0 && SDL_strcmp(mode, "layers") != 0)
    {
        return validation_error(ctx, skybox_path, "scene world.skybox mode must be none, sphere, cubemap, or layers");
    }
    yyjson_val *preset = obj_get(skybox, "preset");
    if (preset != NULL && (!yyjson_is_str(preset) || yyjson_get_str(preset)[0] == '\0'))
        return validation_error(ctx, skybox_path, "scene world.skybox preset must be a non-empty string");

    /* Faces reference declared image assets by id or arbitrary asset paths
     * (which contain a path separator). All six are required when any face
     * is authored; preset- or layer-only skies may omit them. */
    static const char *const faces[] = {"pos_x", "neg_x", "pos_y", "neg_y", "pos_z", "neg_z"};
    bool any_face = false;
    for (size_t i = 0; i < SDL_arraysize(faces); ++i)
        any_face = any_face || obj_get(skybox, faces[i]) != NULL;
    for (size_t i = 0; any_face && i < SDL_arraysize(faces); ++i)
    {
        char face_path[PATH_BUFFER_SIZE];
        format_path(face_path, sizeof(face_path), "%s.%s", skybox_path, faces[i]);
        const char *face = json_string(skybox, faces[i]);
        if (face != NULL && (SDL_strchr(face, '/') != NULL || SDL_strchr(face, '\\') != NULL))
            continue;
        if (!require_ref(ctx, &names->images, "image asset", face, face_path))
            return false;
    }

    yyjson_val *layers = obj_get(skybox, "layers");
    if (layers != NULL)
    {
        if (!yyjson_is_arr(layers) || yyjson_arr_size(layers) == 0u)
            return validation_error(ctx, skybox_path, "scene world.skybox layers must be a non-empty array");
        for (size_t i = 0; i < yyjson_arr_size(layers); ++i)
        {
            char layer_path[PATH_BUFFER_SIZE];
            format_path(layer_path, sizeof(layer_path), "%s.layers[%zu]", skybox_path, i);
            yyjson_val *layer = yyjson_arr_get(layers, i);
            if (!yyjson_is_obj(layer))
                return validation_error(ctx, layer_path, "sky layer must be an object");
            const char *texture = json_string(layer, "texture");
            if (texture == NULL || texture[0] == '\0')
                return validation_error(ctx, layer_path, "sky layer requires a non-empty texture reference");
            if (SDL_strchr(texture, '/') == NULL && SDL_strchr(texture, '\\') == NULL &&
                !require_ref(ctx, &names->images, "image asset", texture, layer_path))
            {
                return false;
            }
            yyjson_val *scroll = obj_get(layer, "scroll");
            if (scroll != NULL &&
                (!yyjson_is_arr(scroll) || yyjson_arr_size(scroll) != 2u || !yyjson_is_num(yyjson_arr_get(scroll, 0)) ||
                 !yyjson_is_num(yyjson_arr_get(scroll, 1))))
            {
                return validation_error(ctx, layer_path, "sky layer scroll must be a [u, v] number pair");
            }
            yyjson_val *scale = obj_get(layer, "scale");
            if (scale != NULL && (!yyjson_is_num(scale) || yyjson_get_num(scale) <= 0.0))
                return validation_error(ctx, layer_path, "sky layer scale must be greater than 0");
            static const char *const unit_fields[] = {"opacity", "depth"};
            for (size_t field = 0; field < SDL_arraysize(unit_fields); ++field)
            {
                yyjson_val *value = obj_get(layer, unit_fields[field]);
                if (value != NULL &&
                    (!yyjson_is_num(value) || yyjson_get_num(value) <= 0.0 || yyjson_get_num(value) > 1.0))
                {
                    return validation_error(ctx, layer_path, "sky layer %s must be a number in (0, 1]",
                                            unit_fields[field]);
                }
            }
        }
    }

    yyjson_val *sphere_value = obj_get(skybox, "sphere");
    const char *sphere = json_string(skybox, "sphere");
    if (sphere_value != NULL)
    {
        char sphere_path[PATH_BUFFER_SIZE];
        format_path(sphere_path, sizeof(sphere_path), "%s.sphere", skybox_path);
        if (sphere == NULL || sphere[0] == '\0')
            return validation_error(ctx, sphere_path,
                                    "scene world.skybox sphere must be a non-empty texture reference");
        if (SDL_strchr(sphere, '/') == NULL && SDL_strchr(sphere, '\\') == NULL &&
            !require_ref(ctx, &names->images, "image asset", sphere, sphere_path))
        {
            return false;
        }
    }

    if (!any_face && preset == NULL && layers == NULL && sphere == NULL &&
        (mode == NULL || SDL_strcmp(mode, "none") != 0))
    {
        return validation_error(ctx, skybox_path, "scene world.skybox requires preset, sphere, faces, or layers");
    }

    yyjson_val *size = obj_get(skybox, "size");
    if (size != NULL && (!yyjson_is_num(size) || yyjson_get_num(size) <= 1.0))
        return validation_error(ctx, skybox_path, "scene world.skybox size must be greater than 1");
    return true;
}

static bool validate_scene_ui_condition(validation_context *ctx, yyjson_val *condition, const char *path,
                                        validation_names *names);

static bool validate_scene_world_viewports(validation_context *ctx, yyjson_val *scene_root, const char *json_path,
                                           validation_names *names)
{
    yyjson_val *viewports = obj_get(scene_root, "world_viewports");
    if (viewports == NULL)
        return true;
    if (yyjson_is_arr(viewports))
    {
        for (size_t i = 0; i < yyjson_arr_size(viewports); ++i)
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "%s.world_viewports[%zu]", json_path, i);
            yyjson_val *viewport = yyjson_arr_get(viewports, i);
            if (!yyjson_is_obj(viewport))
                return validation_error(ctx, path, "scene world viewport entries must be objects");
            const char *name = json_string(viewport, "name");
            if (name == NULL || name[0] == '\0')
                return validation_error(ctx, path, "scene world viewport requires a non-empty name");
            if (!require_ref(ctx, &names->cameras, "camera", json_string(viewport, "camera"), path))
                return false;
            yyjson_val *rect = obj_get(viewport, "rect");
            if (!is_exact_vec_array(rect, 4) || !numeric_array_values_in_range(rect, 0.0, DBL_MAX) ||
                yyjson_get_num(yyjson_arr_get(rect, 2)) <= 0.0 || yyjson_get_num(yyjson_arr_get(rect, 3)) <= 0.0)
            {
                return validation_error(
                    ctx, path, "scene world viewport rect must be [x, y, width, height] with positive dimensions");
            }
            yyjson_val *viewmodel = obj_get(viewport, "viewmodel");
            if (viewmodel != NULL && !yyjson_is_bool(viewmodel))
                return validation_error(ctx, path, "scene world viewport viewmodel must be a boolean");
            yyjson_val *skybox = obj_get(viewport, "skybox");
            if (skybox != NULL && !yyjson_is_bool(skybox))
                return validation_error(ctx, path, "scene world viewport skybox must be a boolean");
            char condition_path[PATH_BUFFER_SIZE];
            format_path(condition_path, sizeof(condition_path), "%s.active_if", path);
            if (!validate_scene_ui_condition(ctx, obj_get(viewport, "active_if"), condition_path, names))
                return false;
        }
        return true;
    }
    if (!yyjson_is_obj(viewports))
        return validation_error(ctx, json_path, "scene world_viewports must be an array or layout object");

    const char *layout_key = json_string(viewports, "layout_key");
    const char *default_layout = json_string(viewports, "default_layout");
    if (layout_key == NULL || layout_key[0] == '\0' || default_layout == NULL || default_layout[0] == '\0')
        return validation_error(ctx, json_path,
                                "scene world_viewports layout requires non-empty layout_key and default_layout");
    yyjson_val *gap = obj_get(viewports, "gap");
    if (gap != NULL && (!yyjson_is_num(gap) || yyjson_get_num(gap) < 0.0))
        return validation_error(ctx, json_path, "scene world_viewports gap must be a non-negative number");
    yyjson_val *avoid = obj_get(viewports, "avoid_docked_ui");
    if (!validate_scene_world_viewport_docks(ctx, avoid, json_path))
        return false;

    yyjson_val *bounds = obj_get(viewports, "bounds");
    if (bounds != NULL && !yyjson_is_obj(bounds))
        return validation_error(ctx, json_path, "scene world_viewports bounds must be an object");
    static const char *const bound_keys[] = {"x", "y", "width", "height"};
    for (size_t i = 0; yyjson_is_obj(bounds) && i < SDL_arraysize(bound_keys); ++i)
    {
        yyjson_val *value = obj_get(bounds, bound_keys[i]);
        if (value == NULL)
            continue;
        const bool fill = yyjson_is_str(value) && SDL_strcmp(yyjson_get_str(value), "fill") == 0;
        const bool number =
            yyjson_is_num(value) && yyjson_get_num(value) >= 0.0 && ((i < 2) || yyjson_get_num(value) > 0.0);
        if (!fill && !number)
            return validation_error(ctx, json_path,
                                    "scene world_viewports bounds values must be non-negative numbers or 'fill'");
    }

    yyjson_val *views = obj_get(viewports, "views");
    if (!yyjson_is_arr(views) || yyjson_arr_size(views) == 0 ||
        yyjson_arr_size(views) > SLAYER3D_GAME_DATA_WORLD_VIEWPORT_MAX)
    {
        return validation_error(ctx, json_path, "scene world_viewports views must be a non-empty bounded array");
    }
    for (size_t i = 0; i < yyjson_arr_size(views); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.world_viewports.views[%zu]", json_path, i);
        yyjson_val *view = yyjson_arr_get(views, i);
        const char *name = json_string(view, "name");
        if (!yyjson_is_obj(view) || name == NULL || name[0] == '\0')
            return validation_error(ctx, path, "scene world viewport view requires a non-empty name");
        for (size_t prior = 0; prior < i; ++prior)
        {
            if (SDL_strcmp(json_string(yyjson_arr_get(views, prior), "name"), name) == 0)
                return validation_error(ctx, path, "scene world viewport view name '%s' is duplicated", name);
        }
        if (!require_ref(ctx, &names->cameras, "camera", json_string(view, "camera"), path))
            return false;
        yyjson_val *viewmodel = obj_get(view, "viewmodel");
        if (viewmodel != NULL && !yyjson_is_bool(viewmodel))
            return validation_error(ctx, path, "scene world viewport viewmodel must be a boolean");
        yyjson_val *skybox = obj_get(view, "skybox");
        if (skybox != NULL && !yyjson_is_bool(skybox))
            return validation_error(ctx, path, "scene world viewport skybox must be a boolean");
        yyjson_val *work_plane = obj_get(view, "work_plane");
        yyjson_val *normal = obj_get(work_plane, "normal");
        if (work_plane != NULL && !yyjson_is_obj(work_plane))
            return validation_error(ctx, path, "scene world viewport work_plane must be an object");
        if (normal != NULL && !is_exact_vec_array(normal, 3))
            return validation_error(ctx, path, "scene world viewport work_plane normal must be a vec3");
        if (is_exact_vec_array(normal, 3))
        {
            const double x = yyjson_get_num(yyjson_arr_get(normal, 0));
            const double y = yyjson_get_num(yyjson_arr_get(normal, 1));
            const double z = yyjson_get_num(yyjson_arr_get(normal, 2));
            if (x * x + y * y + z * z <= 0.000001)
                return validation_error(ctx, path, "scene world viewport work_plane normal must be non-zero");
        }
        yyjson_val *distance = obj_get(work_plane, "distance");
        if (distance != NULL && !yyjson_is_num(distance))
            return validation_error(ctx, path, "scene world viewport work_plane distance must be a number");
        yyjson_val *work_plane_enabled = obj_get(work_plane, "enabled");
        if (work_plane_enabled != NULL && !yyjson_is_bool(work_plane_enabled))
            return validation_error(ctx, path, "scene world viewport work_plane enabled must be a boolean");
        yyjson_val *grid = obj_get(view, "grid");
        if (grid != NULL && !yyjson_is_obj(grid))
            return validation_error(ctx, path, "scene world viewport grid must be an object");
        yyjson_val *enabled = obj_get(grid, "enabled");
        if (enabled != NULL && !yyjson_is_bool(enabled))
            return validation_error(ctx, path, "scene world viewport grid enabled must be a boolean");
        yyjson_val *grid_color = obj_get(grid, "color");
        if (grid_color != NULL && !is_exact_vec3_or_vec4_array(grid_color))
            return validation_error(ctx, path, "scene world viewport grid color must be a vec3 or vec4 color");
        const char *spacing_key = json_string(grid, "spacing_key");
        if (obj_get(grid, "spacing_key") != NULL && (spacing_key == NULL || spacing_key[0] == '\0'))
            return validation_error(ctx, path, "scene world viewport grid spacing_key must be a non-empty string");
        static const char *const positive_grid_keys[] = {"spacing", "extent"};
        for (size_t key = 0; yyjson_is_obj(grid) && key < SDL_arraysize(positive_grid_keys); ++key)
        {
            yyjson_val *value = obj_get(grid, positive_grid_keys[key]);
            if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) <= 0.0))
                return validation_error(ctx, path, "scene world viewport grid %s must be positive",
                                        positive_grid_keys[key]);
        }
    }

    yyjson_val *layouts = obj_get(viewports, "layouts");
    if (!yyjson_is_arr(layouts) || yyjson_arr_size(layouts) == 0)
        return validation_error(ctx, json_path, "scene world_viewports layouts must be a non-empty array");
    bool found_default = false;
    for (size_t i = 0; i < yyjson_arr_size(layouts); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.world_viewports.layouts[%zu]", json_path, i);
        yyjson_val *layout = yyjson_arr_get(layouts, i);
        const char *name = json_string(layout, "name");
        const int columns = scene_json_int(layout, "columns", 0);
        const int rows = scene_json_int(layout, "rows", 0);
        if (!yyjson_is_obj(layout) || name == NULL || name[0] == '\0' || columns < 1 || rows < 1)
            return validation_error(ctx, path, "scene world viewport layout requires name, columns, and rows");
        for (size_t prior = 0; prior < i; ++prior)
        {
            const char *prior_name = json_string(yyjson_arr_get(layouts, prior), "name");
            if (prior_name != NULL && SDL_strcmp(prior_name, name) == 0)
                return validation_error(ctx, path, "scene world viewport layout name '%s' is duplicated", name);
        }
        found_default = found_default || SDL_strcmp(name, default_layout) == 0;
        yyjson_val *layout_avoid = obj_get(layout, "avoid_docked_ui");
        if (!validate_scene_world_viewport_docks(ctx, layout_avoid, path))
            return false;
        yyjson_val *panes = obj_get(layout, "panes");
        if (!yyjson_is_arr(panes) || yyjson_arr_size(panes) == 0 ||
            yyjson_arr_size(panes) > SLAYER3D_GAME_DATA_WORLD_VIEWPORT_MAX)
        {
            return validation_error(ctx, path, "scene world viewport layout panes must be a non-empty bounded array");
        }
        for (size_t pane_index = 0; pane_index < yyjson_arr_size(panes); ++pane_index)
        {
            yyjson_val *pane = yyjson_arr_get(panes, pane_index);
            const char *view_name = json_string(pane, "view");
            bool found_view = false;
            for (size_t view_index = 0; view_index < yyjson_arr_size(views); ++view_index)
            {
                const char *candidate = json_string(yyjson_arr_get(views, view_index), "name");
                found_view =
                    found_view || (candidate != NULL && view_name != NULL && SDL_strcmp(candidate, view_name) == 0);
            }
            const int column = scene_json_int(pane, "column", 0);
            const int row = scene_json_int(pane, "row", 0);
            const int column_span = scene_json_int(pane, "column_span", 1);
            const int row_span = scene_json_int(pane, "row_span", 1);
            if (!yyjson_is_obj(pane) || !found_view || column < 0 || row < 0 || column_span < 1 || row_span < 1 ||
                column + column_span > columns || row + row_span > rows)
            {
                return validation_error(ctx, path, "scene world viewport pane has invalid view or grid placement");
            }
        }
    }
    if (!found_default)
        return validation_error(ctx, json_path, "scene world_viewports default_layout must name an authored layout");
    return true;
}

static bool validate_scene_details(validation_context *ctx, yyjson_val *root, yyjson_val *game_root,
                                   validation_names *names, const char *json_path)
{
    const char *enter_signal = json_string(root, "on_enter_signal");
    if (enter_signal != NULL && !require_ref(ctx, &names->signals, "signal", enter_signal, json_path))
        return false;

    yyjson_val *transitions = obj_get(root, "transitions");
    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(transitions, &iter);
    while (yyjson_is_obj(transitions) && (key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *transition = yyjson_get_str(yyjson_obj_iter_get_val(key));
        if (transition != NULL && !yyjson_is_obj(obj_get(obj_get(game_root, "transitions"), transition)))
            return validation_error(ctx, json_path, "scene references unknown transition '%s'", transition);
    }

    yyjson_val *camera_value = obj_get(root, "camera");
    if (camera_value != NULL && !yyjson_is_str(camera_value))
        return validation_error(ctx, json_path, "scene camera must be a string");
    const char *camera = json_string(root, "camera");
    if (camera != NULL && !require_ref(ctx, &names->cameras, "camera", camera, json_path))
        return false;

    if (!validate_scene_sector_levels(ctx, root, json_path, names))
        return false;
    if (!validate_scene_brush_worlds(ctx, root, json_path, names))
        return false;
    if (!validate_scene_skybox(ctx, root, json_path, names))
        return false;
    if (!validate_scene_world_viewports(ctx, root, json_path, names))
        return false;
    if (!validate_scene_editor_tooling(ctx, root, json_path, names))
        return false;

    char phases_path[PATH_BUFFER_SIZE];
    format_path(phases_path, sizeof(phases_path), "%s.update_phases", json_path);
    if (!validate_update_phases(ctx, obj_get(root, "update_phases"), phases_path, names))
        return false;

    yyjson_val *timeline = obj_get(root, "timeline");
    if (timeline != NULL)
    {
        if (!yyjson_is_obj(timeline))
            return validation_error(ctx, json_path, "scene timeline must be an object");
        yyjson_val *timeline_block_menus = obj_get(timeline, "block_menus");
        if (timeline_block_menus != NULL && !yyjson_is_bool(timeline_block_menus))
            return validation_error(ctx, json_path, "scene timeline block_menus must be a boolean");
        yyjson_val *timeline_block_shortcuts = obj_get(timeline, "block_scene_shortcuts");
        if (timeline_block_shortcuts != NULL && !yyjson_is_bool(timeline_block_shortcuts))
            return validation_error(ctx, json_path, "scene timeline block_scene_shortcuts must be a boolean");
        timeline_block_shortcuts = obj_get(timeline, "block_shortcuts");
        if (timeline_block_shortcuts != NULL && !yyjson_is_bool(timeline_block_shortcuts))
            return validation_error(ctx, json_path, "scene timeline block_shortcuts must be a boolean");
        char timeline_skip_path[PATH_BUFFER_SIZE];
        format_path(timeline_skip_path, sizeof(timeline_skip_path), "%s.timeline.skip_policy", json_path);
        if (!validate_skip_policy(ctx, obj_get(timeline, "skip_policy"), timeline_skip_path, names))
            return false;
        yyjson_val *events = obj_get(timeline, "events");
        if (events == NULL)
            events = obj_get(timeline, "tracks");
        if (events != NULL && !yyjson_is_arr(events))
            return validation_error(ctx, json_path, "scene timeline events must be an array");

        double previous_time = 0.0;
        for (size_t i = 0; yyjson_is_arr(events) && i < yyjson_arr_size(events); ++i)
        {
            char event_path[PATH_BUFFER_SIZE];
            format_path(event_path, sizeof(event_path), "%s.timeline.events[%zu]", json_path, i);
            yyjson_val *event = yyjson_arr_get(events, i);
            if (!yyjson_is_obj(event))
                return validation_error(ctx, event_path, "timeline event must be an object");
            yyjson_val *time = obj_get(event, "time");
            if (!yyjson_is_num(time) || yyjson_get_num(time) < 0.0)
                return validation_error(ctx, event_path, "timeline event requires a non-negative time");
            if (yyjson_get_num(time) < previous_time)
                return validation_error(ctx, event_path, "timeline event times must be non-decreasing");
            previous_time = yyjson_get_num(time);

            char action_path[PATH_BUFFER_SIZE];
            format_path(action_path, sizeof(action_path), "%s.action", event_path);
            if (!validate_timeline_action(ctx, obj_get(event, "action"), action_path, names))
                return false;
        }
    }

    char skip_path[PATH_BUFFER_SIZE];
    format_path(skip_path, sizeof(skip_path), "%s.skip_policy", json_path);
    if (!validate_skip_policy(ctx, obj_get(root, "skip_policy"), skip_path, names))
        return false;

    char activity_path[PATH_BUFFER_SIZE];
    format_path(activity_path, sizeof(activity_path), "%s.activity", json_path);
    if (!validate_scene_activity(ctx, obj_get(root, "activity"), activity_path, names))
        return false;

    if (obj_get(root, "splash") != NULL)
        return validation_error(ctx, json_path,
                                "scene splash is no longer supported; use scene.timeline, skip_policy, UI images, "
                                "and scene transitions");

    yyjson_val *scene_input = obj_get(root, "input");
    if (scene_input != NULL && !yyjson_is_obj(scene_input))
        return validation_error(ctx, json_path, "scene input must be an object");
    const char *mouse_capture = json_string(scene_input, "mouse_capture");
    if (mouse_capture != NULL && SDL_strcmp(mouse_capture, "never") != 0 &&
        SDL_strcmp(mouse_capture, "unpaused") != 0 && SDL_strcmp(mouse_capture, "always") != 0)
        return validation_error(ctx, json_path, "scene input.mouse_capture must be never, unpaused, or always");
    char mouse_capture_if_path[PATH_BUFFER_SIZE];
    format_path(mouse_capture_if_path, sizeof(mouse_capture_if_path), "%s.input.mouse_capture_if", json_path);
    if (!validate_data_condition(ctx, obj_get(scene_input, "mouse_capture_if"), mouse_capture_if_path, names))
        return false;
    yyjson_val *scene_actions = obj_get(scene_input, "actions");
    if (scene_actions != NULL && !yyjson_is_arr(scene_actions))
        return validation_error(ctx, json_path, "scene input.actions must be an array");
    for (size_t i = 0; yyjson_is_arr(scene_actions) && i < yyjson_arr_size(scene_actions); ++i)
    {
        char action_path[PATH_BUFFER_SIZE];
        format_path(action_path, sizeof(action_path), "%s.input.actions[%zu]", json_path, i);
        yyjson_val *action = yyjson_arr_get(scene_actions, i);
        if (!yyjson_is_str(action) ||
            !require_ref(ctx, &names->actions, "input action", yyjson_get_str(action), action_path))
            return false;
    }

    yyjson_val *entities = obj_get(root, "entities");
    if (entities != NULL && !yyjson_is_arr(entities))
        return validation_error(ctx, json_path, "scene entities must be an array");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        char entity_path[PATH_BUFFER_SIZE];
        format_path(entity_path, sizeof(entity_path), "%s.entities[%zu]", json_path, i);
        yyjson_val *entity = yyjson_arr_get(entities, i);
        if (!yyjson_is_str(entity) || yyjson_get_str(entity)[0] == '\0')
            return validation_error(ctx, entity_path, "scene entity entries must be non-empty strings");
        if (!require_ref(ctx, &names->entities, "entity", yyjson_get_str(entity), entity_path))
            return false;
    }

    yyjson_val *menus = obj_get(root, "menus");
    for (size_t m = 0; yyjson_is_arr(menus) && m < yyjson_arr_size(menus); ++m)
    {
        char menu_path[PATH_BUFFER_SIZE];
        format_path(menu_path, sizeof(menu_path), "%s.menus[%zu]", json_path, m);
        yyjson_val *menu = yyjson_arr_get(menus, m);
        if (!yyjson_is_obj(menu))
            return validation_error(ctx, menu_path, "scene menu must be an object");
        if (!is_non_empty_string(menu, "name"))
            return validation_error(ctx, menu_path, "scene menu requires a non-empty name");
        if (!require_ref(ctx, &names->actions, "input action", json_string(menu, "up_action"), menu_path) ||
            !require_ref(ctx, &names->actions, "input action", json_string(menu, "down_action"), menu_path) ||
            !require_ref(ctx, &names->actions, "input action", json_string(menu, "select_action"), menu_path))
            return false;
        const char *left_action = json_string(menu, "left_action");
        if (left_action != NULL && !require_ref(ctx, &names->actions, "input action", left_action, menu_path))
            return false;
        const char *right_action = json_string(menu, "right_action");
        if (right_action != NULL && !require_ref(ctx, &names->actions, "input action", right_action, menu_path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.active_if", menu_path);
        if (!validate_scene_ui_condition(ctx, obj_get(menu, "active_if"), condition_path, names))
            return false;
        const char *move_signal = json_string(menu, "move_signal");
        if (move_signal != NULL && !require_ref(ctx, &names->signals, "signal", move_signal, menu_path))
            return false;
        const char *select_signal = json_string(menu, "select_signal");
        if (select_signal != NULL && !require_ref(ctx, &names->signals, "signal", select_signal, menu_path))
            return false;

        yyjson_val *items = obj_get(menu, "items");
        if (!yyjson_is_arr(items) || yyjson_arr_size(items) == 0)
            return validation_error(ctx, menu_path, "scene menu requires at least one item");
        for (size_t i = 0; i < yyjson_arr_size(items); ++i)
        {
            char item_path[PATH_BUFFER_SIZE];
            format_path(item_path, sizeof(item_path), "%s.items[%zu]", menu_path, i);
            yyjson_val *item = yyjson_arr_get(items, i);
            const char *item_type = json_string(item, "type");
            if (item_type != NULL)
            {
                if (SDL_strcmp(item_type, "dynamic_list") != 0)
                    return validation_error(ctx, item_path, "scene menu item type must be dynamic_list when present");
                if (!validate_dynamic_menu_list_item(ctx, item, item_path, names))
                    return false;
                continue;
            }
            if (!is_non_empty_string(item, "label"))
                return validation_error(ctx, item_path, "scene menu item requires a label");
            const char *scene = json_string(item, "scene");
            if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, item_path))
                return false;
            const char *return_to = json_string(item, "return_to");
            if (return_to != NULL && !require_ref(ctx, &names->scenes, "scene", return_to, item_path))
                return false;
            const char *signal = json_string(item, "signal");
            if (signal != NULL && !require_ref(ctx, &names->signals, "signal", signal, item_path))
                return false;
            yyjson_val *scene_state = obj_get(item, "scene_state");
            if (scene_state != NULL)
            {
                if (!yyjson_is_obj(scene_state))
                    return validation_error(ctx, item_path, "scene menu item scene_state must be an object");
                if (!is_non_empty_string(scene_state, "key") || !is_non_empty_string(scene_state, "value"))
                    return validation_error(ctx, item_path,
                                            "scene menu item scene_state requires non-empty key and value");
            }
            yyjson_val *return_scene = obj_get(item, "return_scene");
            if (return_scene != NULL && !yyjson_is_bool(return_scene))
                return validation_error(ctx, item_path, "scene menu item return_scene must be a boolean");
            yyjson_val *return_paused = obj_get(item, "return_paused");
            if (return_paused != NULL && !yyjson_is_bool(return_paused))
                return validation_error(ctx, item_path, "scene menu item return_paused must be a boolean");
            const char *pause = json_string(item, "pause");
            if (pause != NULL && SDL_strcmp(pause, "pause") != 0 && SDL_strcmp(pause, "resume") != 0 &&
                SDL_strcmp(pause, "unpause") != 0 && SDL_strcmp(pause, "toggle") != 0)
                return validation_error(ctx, item_path,
                                        "scene menu item pause must be pause, resume, unpause, or toggle");
            char control_path[PATH_BUFFER_SIZE];
            format_path(control_path, sizeof(control_path), "%s.control", item_path);
            if (!validate_menu_item_control(ctx, obj_get(item, "control"), control_path, names))
                return false;
        }
    }

    yyjson_val *texts = obj_get(obj_get(root, "ui"), "text");
    yyjson_val *images = obj_get(obj_get(root, "ui"), "images");
    yyjson_val *rects = obj_get(obj_get(root, "ui"), "rects");
    yyjson_val *ui_menus = obj_get(obj_get(root, "ui"), "menus");
    yyjson_val *panels = obj_get(obj_get(root, "ui"), "panels");
    yyjson_val *inspectors = obj_get(obj_get(root, "ui"), "inspectors");
    if (images != NULL && !yyjson_is_arr(images))
        return validation_error(ctx, json_path, "scene UI images must be an array");
    if (rects != NULL && !yyjson_is_arr(rects))
        return validation_error(ctx, json_path, "scene UI rectangles must be an array");
    if (ui_menus != NULL && !yyjson_is_arr(ui_menus))
        return validation_error(ctx, json_path, "scene UI menus must be an array");
    char panels_path[PATH_BUFFER_SIZE];
    format_path(panels_path, sizeof(panels_path), "%s.ui.panels", json_path);
    char inspectors_path[PATH_BUFFER_SIZE];
    format_path(inspectors_path, sizeof(inspectors_path), "%s.ui.inspectors", json_path);
    if (!validate_ui_panels(ctx, panels, panels_path, names) ||
        !validate_ui_inspectors(ctx, inspectors, inspectors_path, names))
        return false;
    for (size_t i = 0; yyjson_is_arr(ui_menus) && i < yyjson_arr_size(ui_menus); ++i)
    {
        char menu_path[PATH_BUFFER_SIZE];
        format_path(menu_path, sizeof(menu_path), "%s.ui.menus[%zu]", json_path, i);
        yyjson_val *presenter = yyjson_arr_get(ui_menus, i);
        if (!yyjson_is_obj(presenter))
            return validation_error(ctx, menu_path, "scene UI menu presenters must be objects");
        if (!is_non_empty_string(presenter, "name"))
            return validation_error(ctx, menu_path, "scene UI menu presenter requires a non-empty name");
        const char *menu_name = json_string(presenter, "menu");
        if (!scene_has_menu_name(root, menu_name))
            return validation_error(ctx, menu_path, "scene UI menu presenter references unknown menu '%s'",
                                    menu_name != NULL ? menu_name : "<missing>");
        yyjson_val *visible_count = obj_get(presenter, "visible_count");
        if (visible_count != NULL && (!yyjson_is_int(visible_count) || yyjson_get_sint(visible_count) <= 0))
            return validation_error(ctx, menu_path, "scene UI menu presenter visible_count must be a positive integer");
        if (!require_ref(ctx, &names->fonts, "font asset", json_string(presenter, "font"), menu_path))
            return false;
        yyjson_val *cursor = obj_get(presenter, "cursor");
        if (yyjson_is_obj(cursor) && json_string(cursor, "font") != NULL &&
            !require_ref(ctx, &names->fonts, "font asset", json_string(cursor, "font"), menu_path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", menu_path);
        if (!validate_scene_ui_condition(ctx, obj_get(presenter, "visible_if"), condition_path, names))
            return false;
        format_path(condition_path, sizeof(condition_path), "%s.active_if", menu_path);
        if (!validate_scene_ui_condition(ctx, obj_get(presenter, "active_if"), condition_path, names))
            return false;
    }

    for (size_t i = 0; yyjson_is_arr(images) && i < yyjson_arr_size(images); ++i)
    {
        char image_path[PATH_BUFFER_SIZE];
        format_path(image_path, sizeof(image_path), "%s.ui.images[%zu]", json_path, i);
        yyjson_val *image = yyjson_arr_get(images, i);
        if (!yyjson_is_obj(image))
            return validation_error(ctx, image_path, "scene UI image entries must be objects");
        if (!is_non_empty_string(image, "name"))
            return validation_error(ctx, image_path, "scene UI image requires a non-empty name");
        if (!require_ref(ctx, &names->images, "image asset", json_string(image, "image"), image_path))
            return false;
        const char *effect = json_string(image, "effect");
        if (effect != NULL && effect[0] != '\0' && SDL_strcasecmp(effect, "melt") != 0)
            return validation_error(ctx, image_path, "unsupported scene UI image effect '%s'", effect);
        yyjson_val *effect_speed = obj_get(image, "effect_speed");
        if (yyjson_is_num(effect_speed) && (float)yyjson_get_real(effect_speed) < 0.0f)
            return validation_error(ctx, image_path, "scene UI image effect_speed must be non-negative");
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", image_path);
        if (!validate_scene_ui_condition(ctx, obj_get(image, "visible_if"), condition_path, names))
            return false;
    }

    for (size_t i = 0; yyjson_is_arr(rects) && i < yyjson_arr_size(rects); ++i)
    {
        char rect_path[PATH_BUFFER_SIZE];
        format_path(rect_path, sizeof(rect_path), "%s.ui.rects[%zu]", json_path, i);
        yyjson_val *rect = yyjson_arr_get(rects, i);
        if (!yyjson_is_obj(rect))
            return validation_error(ctx, rect_path, "scene UI rectangle entries must be objects");
        if (!is_non_empty_string(rect, "name"))
            return validation_error(ctx, rect_path, "scene UI rectangle requires a non-empty name");
        yyjson_val *alpha_source = obj_get(rect, "alpha_source");
        if (alpha_source != NULL)
        {
            if (!yyjson_is_obj(alpha_source))
                return validation_error(ctx, rect_path, "scene UI rectangle alpha_source must be an object");
            if (!require_ref(ctx, &names->entities, "entity", json_string(alpha_source, "target"), rect_path))
                return false;
            if (!is_non_empty_string(alpha_source, "key"))
                return validation_error(ctx, rect_path, "scene UI rectangle alpha_source requires a non-empty key");
            yyjson_val *min_value = obj_get(alpha_source, "min");
            yyjson_val *max_value = obj_get(alpha_source, "max");
            if (min_value != NULL && !yyjson_is_num(min_value))
                return validation_error(ctx, rect_path, "scene UI rectangle alpha_source min must be numeric");
            if (max_value != NULL && !yyjson_is_num(max_value))
                return validation_error(ctx, rect_path, "scene UI rectangle alpha_source max must be numeric");
            if (yyjson_is_num(min_value) && yyjson_is_num(max_value) &&
                yyjson_get_real(max_value) < yyjson_get_real(min_value))
                return validation_error(ctx, rect_path, "scene UI rectangle alpha_source max must be >= min");
        }
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", rect_path);
        if (!validate_scene_ui_condition(ctx, obj_get(rect, "visible_if"), condition_path, names))
            return false;
    }

    for (size_t i = 0; yyjson_is_arr(texts) && i < yyjson_arr_size(texts); ++i)
    {
        char text_path[PATH_BUFFER_SIZE];
        format_path(text_path, sizeof(text_path), "%s.ui.text[%zu]", json_path, i);
        yyjson_val *text = yyjson_arr_get(texts, i);
        if (!yyjson_is_obj(text))
            return validation_error(ctx, text_path, "scene UI text entries must be objects");
        if (json_string(text, "font") != NULL &&
            !require_ref(ctx, &names->fonts, "font asset", json_string(text, "font"), text_path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", text_path);
        if (!validate_scene_ui_condition(ctx, obj_get(text, "visible_if"), condition_path, names))
            return false;

        yyjson_val *bindings = obj_get(text, "bindings");
        for (size_t b = 0; yyjson_is_arr(bindings) && b < yyjson_arr_size(bindings); ++b)
        {
            char binding_path[PATH_BUFFER_SIZE];
            format_path(binding_path, sizeof(binding_path), "%s.bindings[%zu]", text_path, b);
            yyjson_val *binding = yyjson_arr_get(bindings, b);
            const char *type = json_string(binding, "type");
            if (SDL_strcmp(type != NULL ? type : "", "property") == 0)
            {
                if (!require_ref(ctx, &names->entities, "entity", json_string(binding, "entity"), binding_path))
                    return false;
                if (!is_non_empty_string(binding, "key"))
                    return validation_error(ctx, binding_path, "scene UI property binding requires a non-empty key");
            }
            else if (SDL_strcmp(type != NULL ? type : "", "metric") == 0)
            {
                const char *metric = json_string(binding, "metric");
                if (!ui_metric_name_valid(metric))
                    return validation_error(ctx, binding_path, "unsupported scene UI metric '%s'",
                                            metric != NULL ? metric : "<missing>");
            }
            else
            {
                if (SDL_strcmp(type != NULL ? type : "", "scene_state") != 0)
                    return validation_error(ctx, binding_path, "unsupported scene UI binding type '%s'",
                                            type != NULL ? type : "<missing>");
                if (!is_non_empty_string(binding, "key"))
                    return validation_error(ctx, binding_path, "scene UI scene_state binding requires a non-empty key");
                yyjson_val *fallback = obj_get(binding, "default");
                if (fallback != NULL && !yyjson_is_str(fallback) && !yyjson_is_int(fallback) &&
                    !yyjson_is_real(fallback) && !yyjson_is_bool(fallback))
                    return validation_error(ctx, binding_path, "scene UI scene_state binding default must be scalar");
            }
        }
    }
    return true;
}

bool validate_scenes(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *scenes = obj_get(root, "scenes");
    if (scenes == NULL)
        return true;
    if (!yyjson_is_obj(scenes))
        return validation_error(ctx, "$.scenes", "scenes must be an object");

    yyjson_val *files = obj_get(scenes, "files");
    if (!yyjson_is_arr(files))
        return validation_error(ctx, "$.scenes.files", "scenes.files must be an array");

    const int count = scene_source_doc_capacity(files);
    if (count < 0)
        return validation_error(ctx, "$.scenes.files", "scene files must be strings or known package objects");
    validation_scene_doc *docs = (validation_scene_doc *)SDL_calloc((size_t)count, sizeof(*docs));
    if (docs == NULL && count > 0)
        return validation_error(ctx, "$.scenes.files", "failed to allocate scene validation docs");

    int doc_count = 0;
    for (size_t i = 0; i < yyjson_arr_size(files); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(files, i);
        if (yyjson_is_str(entry))
        {
            if (!validate_scene_file(ctx, names, yyjson_get_str(entry), (int)i, &docs[doc_count]))
            {
                validation_scene_docs_destroy(docs, count);
                return false;
            }
            doc_count++;
            continue;
        }

        const char *package = scene_file_entry_package(entry);
        if (package != NULL)
        {
            slayer3d_standard_options_scene_docs generated;
            char package_error[256];
            if (!slayer3d_standard_options_build_scene_docs(root, package, &generated, package_error,
                                                            (int)sizeof(package_error)))
            {
                validation_scene_docs_destroy(docs, count);
                return validation_error(ctx, "$.scenes.files", "%s", package_error);
            }
            for (int generated_index = 0; generated_index < generated.count; ++generated_index)
            {
                char path[PATH_BUFFER_SIZE];
                format_path(path, sizeof(path), "$.scenes.files[%zu].package[%d]", i, generated_index);
                yyjson_doc *doc = generated.docs[generated_index];
                generated.docs[generated_index] = NULL;
                if (!validate_generated_scene_doc(ctx, names, doc, path, &docs[doc_count]))
                {
                    slayer3d_standard_options_scene_docs_free(&generated);
                    validation_scene_docs_destroy(docs, count);
                    return false;
                }
                doc_count++;
            }
            slayer3d_standard_options_scene_docs_free(&generated);
            continue;
        }

        validation_scene_docs_destroy(docs, count);
        return validation_error(ctx, "$.scenes.files", "scene file entries must be strings or known package objects");
    }

    const char *initial = json_string(scenes, "initial");
    bool ok = initial == NULL || require_ref(ctx, &names->scenes, "scene", initial, "$.scenes.initial");
    for (int i = 0; ok && i < doc_count; ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.scenes.resolved[%d]", i);
        ok = validate_scene_details(ctx, docs[i].root, root, names, path);
    }

    validation_scene_docs_destroy(docs, count);
    return ok;
}

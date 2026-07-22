/**
 * @file game_data_validation_app_world.c
 * @brief App, presentation, camera, and world metadata JSON game data validation.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

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

bool validate_app_refs(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *app = obj_get(root, "app");
    if (!yyjson_is_obj(app))
        return true;
    static const char *const app_dimension_fields[] = {"width",         "height",        "window_width",
                                                       "window_height", "logical_width", "logical_height"};
    for (size_t i = 0; i < SDL_arraysize(app_dimension_fields); ++i)
    {
        yyjson_val *dimension = obj_get(app, app_dimension_fields[i]);
        if (dimension != NULL && (!yyjson_is_int(dimension) || yyjson_get_int(dimension) <= 0))
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.app.%s", app_dimension_fields[i]);
            return validation_error(ctx, path, "app dimensions must be positive integers");
        }
    }
    const char *start_signal = json_string(app, "start_signal");
    if (start_signal != NULL && !require_ref(ctx, &names->signals, "signal", start_signal, "$.app.start_signal"))
        return false;
    yyjson_val *pause = obj_get(app, "pause");
    if (pause != NULL && !yyjson_is_obj(pause))
        return validation_error(ctx, "$.app.pause", "pause must be an object");
    const char *pause_action = json_string(pause, "action");
    if (pause_action != NULL && !require_ref(ctx, &names->actions, "input action", pause_action, "$.app.pause.action"))
        return false;
    if (!validate_data_condition(ctx, obj_get(pause, "allowed_if"), "$.app.pause.allowed_if", names))
        return false;
    const char *startup_transition = json_string(app, "startup_transition");
    if (startup_transition != NULL && !yyjson_is_obj(obj_get(obj_get(root, "transitions"), startup_transition)))
        return validation_error(ctx, "$.app.startup_transition", "unknown transition reference '%s'",
                                startup_transition);

    yyjson_val *window = obj_get(app, "window");
    if (window != NULL && !yyjson_is_obj(window))
        return validation_error(ctx, "$.app.window", "window must be an object");
    for (size_t i = 0; i < SDL_arraysize(app_dimension_fields); ++i)
    {
        yyjson_val *dimension = obj_get(window, app_dimension_fields[i]);
        if (dimension != NULL && (!yyjson_is_int(dimension) || yyjson_get_int(dimension) <= 0))
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.app.window.%s", app_dimension_fields[i]);
            return validation_error(ctx, path, "app window dimensions must be positive integers");
        }
    }
    yyjson_val *high_pixel_density = obj_get(window, "high_pixel_density");
    if (high_pixel_density != NULL && !yyjson_is_bool(high_pixel_density))
        return validation_error(ctx, "$.app.window.high_pixel_density", "high_pixel_density must be a boolean");
    const char *window_apply_signal = json_string(window, "apply_signal");
    if (window_apply_signal != NULL &&
        !require_ref(ctx, &names->signals, "signal", window_apply_signal, "$.app.window.apply_signal"))
        return false;
    yyjson_val *window_apply_signals = obj_get(window, "apply_signals");
    if (window_apply_signals != NULL && !yyjson_is_arr(window_apply_signals))
        return validation_error(ctx, "$.app.window.apply_signals", "apply_signals must be an array");
    for (size_t i = 0; yyjson_is_arr(window_apply_signals) && i < yyjson_arr_size(window_apply_signals); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.app.window.apply_signals[%zu]", i);
        if (!require_ref(ctx, &names->signals, "signal", yyjson_get_str(yyjson_arr_get(window_apply_signals, i)), path))
            return false;
    }
    yyjson_val *window_settings = obj_get(window, "settings");
    if (window_settings != NULL && !yyjson_is_obj(window_settings))
        return validation_error(ctx, "$.app.window.settings", "window settings must be an object");
    const char *window_settings_target = json_string(window_settings, "target");
    if (window_settings_target != NULL &&
        !require_ref(ctx, &names->entities, "entity", window_settings_target, "$.app.window.settings.target"))
        return false;

    yyjson_val *quit = obj_get(app, "quit");
    if (!yyjson_is_obj(quit))
        return true;
    const char *action = json_string(quit, "action");
    if (action != NULL && !require_ref(ctx, &names->actions, "input action", action, "$.app.quit.action"))
        return false;
    if (!validate_data_condition(ctx, obj_get(quit, "enabled_if"), "$.app.quit.enabled_if", names))
        return false;
    const char *request_signal = json_string(quit, "request_signal");
    if (request_signal != NULL &&
        !require_ref(ctx, &names->signals, "signal", request_signal, "$.app.quit.request_signal"))
        return false;
    const char *quit_signal = json_string(quit, "quit_signal");
    if (quit_signal != NULL && !require_ref(ctx, &names->signals, "signal", quit_signal, "$.app.quit.quit_signal"))
        return false;
    const char *transition = json_string(quit, "transition");
    if (transition != NULL && !yyjson_is_obj(obj_get(obj_get(root, "transitions"), transition)))
        return validation_error(ctx, "$.app.quit.transition", "unknown transition reference '%s'", transition);

    yyjson_val *shortcuts = obj_get(app, "scene_shortcuts");
    if (shortcuts != NULL && !yyjson_is_arr(shortcuts))
        return validation_error(ctx, "$.app.scene_shortcuts", "scene_shortcuts must be an array");
    for (size_t i = 0; yyjson_is_arr(shortcuts) && i < yyjson_arr_size(shortcuts); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.app.scene_shortcuts[%zu]", i);
        yyjson_val *shortcut = yyjson_arr_get(shortcuts, i);
        if (!yyjson_is_obj(shortcut))
            return validation_error(ctx, path, "scene shortcut must be an object");
        if (!require_ref(ctx, &names->actions, "input action", json_string(shortcut, "action"), path))
            return false;
        if (!require_ref(ctx, &names->scenes, "scene", json_string(shortcut, "scene"), path))
            return false;
    }

    yyjson_val *input_policy = obj_get(app, "input_policy");
    if (input_policy != NULL && !yyjson_is_obj(input_policy))
        return validation_error(ctx, "$.app.input_policy", "input_policy must be an object");
    yyjson_val *global_actions = obj_get(input_policy, "global_actions");
    if (global_actions != NULL && !yyjson_is_arr(global_actions))
        return validation_error(ctx, "$.app.input_policy.global_actions", "global_actions must be an array");
    for (size_t i = 0; yyjson_is_arr(global_actions) && i < yyjson_arr_size(global_actions); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.app.input_policy.global_actions[%zu]", i);
        yyjson_val *global_action = yyjson_arr_get(global_actions, i);
        if (!yyjson_is_str(global_action) ||
            !require_ref(ctx, &names->actions, "input action", yyjson_get_str(global_action), path))
            return false;
    }

    yyjson_val *transition_policy = obj_get(app, "scene_transition_policy");
    if (transition_policy != NULL && !yyjson_is_obj(transition_policy))
        return validation_error(ctx, "$.app.scene_transition_policy", "scene_transition_policy must be an object");
    return true;
}

bool validate_update_phases(validation_context *ctx, yyjson_val *phases, const char *json_path, validation_names *names)
{
    if (phases == NULL)
        return true;
    if (!yyjson_is_obj(phases))
        return validation_error(ctx, json_path, "update_phases must be an object");

    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(phases, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        yyjson_val *entry = yyjson_obj_iter_get_val(key);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.%s", json_path, yyjson_get_str(key));
        if (!yyjson_is_bool(entry) && !yyjson_is_obj(entry))
            return validation_error(ctx, path, "update phase must be a bool or object");
        if (yyjson_is_obj(entry))
        {
            char condition_path[PATH_BUFFER_SIZE];
            format_path(condition_path, sizeof(condition_path), "%s.active_if", path);
            if (!validate_data_condition(ctx, obj_get(entry, "active_if"), condition_path, names))
                return false;
        }
    }
    return true;
}

bool validate_presentation(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *presentation = obj_get(root, "presentation");
    if (presentation == NULL)
        return true;
    if (!yyjson_is_obj(presentation))
        return validation_error(ctx, "$.presentation", "presentation must be an object");

    yyjson_val *metrics = obj_get(presentation, "metrics");
    if (metrics != NULL && !yyjson_is_obj(metrics))
        return validation_error(ctx, "$.presentation.metrics", "presentation metrics must be an object");
    yyjson_val *fps_sample_seconds = obj_get(metrics, "fps_sample_seconds");
    if (fps_sample_seconds != NULL && (!yyjson_is_num(fps_sample_seconds) || yyjson_get_num(fps_sample_seconds) <= 0.0))
        return validation_error(ctx, "$.presentation.metrics.fps_sample_seconds",
                                "fps_sample_seconds must be positive");

    yyjson_val *clocks = obj_get(presentation, "clocks");
    if (clocks != NULL && !yyjson_is_arr(clocks))
        return validation_error(ctx, "$.presentation.clocks", "clocks must be an array");
    for (size_t i = 0; yyjson_is_arr(clocks) && i < yyjson_arr_size(clocks); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.presentation.clocks[%zu]", i);
        yyjson_val *clock = yyjson_arr_get(clocks, i);
        if (!yyjson_is_obj(clock))
            return validation_error(ctx, path, "presentation clock must be an object");
        if (!is_non_empty_string(clock, "name"))
            return validation_error(ctx, path, "presentation clock requires a non-empty name");
        if (!require_ref(ctx, &names->entities, "entity", json_string(clock, "target"), path))
            return false;
        if (!is_non_empty_string(clock, "key"))
            return validation_error(ctx, path, "presentation clock requires a non-empty key");
        yyjson_val *speed_property = obj_get(clock, "speed_property");
        if (speed_property != NULL)
        {
            if (!yyjson_is_obj(speed_property))
                return validation_error(ctx, path, "speed_property must be an object");
            if (!require_ref(ctx, &names->entities, "entity", json_string(speed_property, "target"), path))
                return false;
            if (!is_non_empty_string(speed_property, "key"))
                return validation_error(ctx, path, "speed_property requires a non-empty key");
        }
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.active_if", path);
        if (!validate_data_condition(ctx, obj_get(clock, "active_if"), condition_path, names))
            return false;
    }
    return true;
}

bool validate_cameras(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *cameras = obj_get(obj_get(root, "world"), "cameras");
    for (size_t i = 0; yyjson_is_arr(cameras) && i < yyjson_arr_size(cameras); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.world.cameras[%zu]", i);
        yyjson_val *camera = yyjson_arr_get(cameras, i);
        const char *type = json_string(camera, "type");
        yyjson_val *fov = obj_get(camera, "fov");
        yyjson_val *fovy = obj_get(camera, "fovy");
        if (fov != NULL && fovy != NULL)
            return validation_error(ctx, path, "camera must not define both fov and fovy");
        if (fov != NULL && (!yyjson_is_num(fov) || yyjson_get_num(fov) <= 0.0 || yyjson_get_num(fov) >= 180.0))
            return validation_error(ctx, path, "camera fov must be in the range (0, 180)");
        if (fovy != NULL && (!yyjson_is_num(fovy) || yyjson_get_num(fovy) <= 0.0 || yyjson_get_num(fovy) >= 180.0))
            return validation_error(ctx, path, "camera fovy must be in the range (0, 180)");
        yyjson_val *fov_axis = obj_get(camera, "fov_axis");
        if (fov_axis != NULL && (!yyjson_is_str(fov_axis) || (SDL_strcmp(yyjson_get_str(fov_axis), "vertical") != 0 &&
                                                              SDL_strcmp(yyjson_get_str(fov_axis), "horizontal") != 0)))
            return validation_error(ctx, path, "camera fov_axis must be 'vertical' or 'horizontal'");
        yyjson_val *fov_key = obj_get(camera, "fov_key");
        if (fov_key != NULL && (!yyjson_is_str(fov_key) || yyjson_get_str(fov_key)[0] == '\0'))
            return validation_error(ctx, path, "camera fov_key must be a non-empty string");
        yyjson_val *fov_axis_key = obj_get(camera, "fov_axis_key");
        if (fov_axis_key != NULL && (!yyjson_is_str(fov_axis_key) || yyjson_get_str(fov_axis_key)[0] == '\0'))
            return validation_error(ctx, path, "camera fov_axis_key must be a non-empty string");
        yyjson_val *size = obj_get(camera, "size");
        if (size != NULL && (!yyjson_is_num(size) || yyjson_get_num(size) <= 0.0))
            return validation_error(ctx, path, "camera size must be positive");
        yyjson_val *size_key = obj_get(camera, "size_key");
        if (size_key != NULL && (!yyjson_is_str(size_key) || yyjson_get_str(size_key)[0] == '\0'))
            return validation_error(ctx, path, "camera size_key must be a non-empty string");
        yyjson_val *position = obj_get(camera, "position");
        if (position != NULL && !is_vec_array(position, 3))
            return validation_error(ctx, path, "camera position must be a vec3");
        yyjson_val *target = obj_get(camera, "target");
        if (target != NULL && !is_vec_array(target, 3))
            return validation_error(ctx, path, "camera target must be a vec3");
        yyjson_val *up = obj_get(camera, "up");
        if (up != NULL && !is_vec_array(up, 3))
            return validation_error(ctx, path, "camera up must be a vec3");
        yyjson_val *position_offset = obj_get(camera, "position_offset");
        if (position_offset != NULL && !is_vec_array(position_offset, 3))
            return validation_error(ctx, path, "camera position_offset must be a vec3");
        yyjson_val *target_offset = obj_get(camera, "target_offset");
        if (target_offset != NULL && !is_vec_array(target_offset, 3))
            return validation_error(ctx, path, "camera target_offset must be a vec3");
        const char *position_entity = json_string(camera, "position_entity");
        if (position_entity != NULL && !require_ref(ctx, &names->entities, "entity", position_entity, path))
            return false;
        const char *target_entity = json_string(camera, "target_entity");
        if (target_entity != NULL && !require_ref(ctx, &names->entities, "entity", target_entity, path))
            return false;
        if (SDL_strcmp(type != NULL ? type : "", "adapter") == 0)
        {
            const char *adapter = json_string(camera, "adapter");
            if (!require_ref(ctx, &names->adapters, "adapter", adapter, path))
                return false;
            if (!note_name(&names->used_adapters, adapter, path))
                return validation_error(ctx, path, "failed to record adapter use");
        }
        else if (SDL_strcmp(type != NULL ? type : "", "chase") == 0)
        {
            if (!require_ref(ctx, &names->entities, "entity", json_string(camera, "target_entity"), path))
                return false;
        }
        else if (SDL_strcmp(type != NULL ? type : "", "fps") == 0)
        {
            if (!require_ref(ctx, &names->entities, "entity", json_string(camera, "target_entity"), path))
                return false;
        }
    }
    return true;
}

bool validate_world_metadata(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *world = obj_get(root, "world");
    if (world == NULL)
        return true;
    if (!yyjson_is_obj(world))
        return validation_error(ctx, "$.world", "world must be an object");

    yyjson_val *units = obj_get(world, "units");
    if (units != NULL && (!yyjson_is_str(units) || yyjson_get_str(units)[0] == '\0'))
        return validation_error(ctx, "$.world.units", "world units must be a non-empty string");
    yyjson_val *meters_per_unit = obj_get(world, "meters_per_unit");
    if (meters_per_unit != NULL && (!yyjson_is_num(meters_per_unit) || yyjson_get_num(meters_per_unit) <= 0.0))
        return validation_error(ctx, "$.world.meters_per_unit", "world meters_per_unit must be positive");
    return true;
}

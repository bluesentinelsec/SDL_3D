/**
 * @file game_data_validation_logic.c
 * @brief Logic, sensor, timer, adapter, and script validation.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static bool is_axis_name(const char *axis)
{
    return axis != NULL && (SDL_strcmp(axis, "x") == 0 || SDL_strcmp(axis, "y") == 0 || SDL_strcmp(axis, "z") == 0);
}

static bool is_side_name(const char *side)
{
    return side != NULL && (SDL_strcmp(side, "min") == 0 || SDL_strcmp(side, "max") == 0);
}

static bool is_wave_axis_value(yyjson_val *value)
{
    if (value == NULL || yyjson_is_num(value))
        return true;
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 2)
        return false;
    return yyjson_is_num(yyjson_arr_get(value, 0)) && yyjson_is_num(yyjson_arr_get(value, 1));
}

static bool script_path_exists(validation_context *ctx, const char *script_path, const char *json_path)
{
    char *resolved = validation_path_join(ctx->base_dir, script_path);
    if (resolved == NULL)
    {
        return validation_error(ctx, json_path, "failed to resolve script path '%s'", script_path);
    }

    if (ctx->assets != NULL)
    {
        const bool exists = slayer3d_asset_resolver_exists(ctx->assets, resolved);
        SDL_free(resolved);
        if (!exists)
            return validation_error(ctx, json_path, "script asset '%s' does not exist", script_path);
        return true;
    }

    SDL_IOStream *io = SDL_IOFromFile(resolved, "rb");
    if (io == NULL)
    {
        const bool ok = validation_error(ctx, json_path, "script file '%s' does not exist", script_path);
        SDL_free(resolved);
        return ok;
    }

    SDL_CloseIO(io);
    SDL_free(resolved);
    return true;
}

static bool validate_script_cycle(validation_context *ctx, validation_names *names, script_manifest *script);

static script_manifest *find_script_manifest(validation_names *names, const char *id)
{
    if (names == NULL || id == NULL)
        return NULL;
    for (int i = 0; i < names->script_count; ++i)
    {
        if (SDL_strcmp(names->script_manifests[i].id, id) == 0)
            return &names->script_manifests[i];
    }
    return NULL;
}

static bool validate_script_dependency(validation_context *ctx, validation_names *names, script_manifest *script,
                                       const char *dependency)
{
    script_manifest *target = find_script_manifest(names, dependency);
    if (target == NULL)
    {
        return validation_error(ctx, script->json_path, "script '%s' depends on unknown script '%s'", script->id,
                                dependency);
    }
    return validate_script_cycle(ctx, names, target);
}

static bool validate_script_cycle(validation_context *ctx, validation_names *names, script_manifest *script)
{
    if (script->visited)
        return true;
    if (script->visiting)
    {
        return validation_error(ctx, script->json_path, "script dependency cycle reaches '%s'", script->id);
    }

    script->visiting = true;
    for (int i = 0; i < script->dependency_count; ++i)
    {
        if (!validate_script_dependency(ctx, names, script, script->dependencies[i]))
            return false;
    }
    script->visiting = false;
    script->visited = true;
    return true;
}

bool collect_timers(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *timers = obj_get(obj_get(root, "logic"), "timers");
    if (timers == NULL)
        return true;
    if (!yyjson_is_arr(timers))
        return validation_error(ctx, "$.logic.timers", "logic timers must be an array");

    for (size_t i = 0; i < yyjson_arr_size(timers); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.logic.timers[%zu]", i);
        yyjson_val *timer = yyjson_arr_get(timers, i);
        if (!yyjson_is_obj(timer))
            return validation_error(ctx, path, "timer entries must be objects");
        if (!require_unique_name(ctx, &names->timers, "timer", json_string(timer, "name"), path))
            return false;
    }
    return true;
}

bool collect_adapters(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *adapters = obj_get(root, "adapters");
    if (adapters == NULL)
        return true;
    if (!yyjson_is_arr(adapters))
        return validation_error(ctx, "$.adapters", "adapters must be an array");

    for (size_t i = 0; i < yyjson_arr_size(adapters); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.adapters[%zu]", i);
        yyjson_val *adapter = yyjson_arr_get(adapters, i);
        if (!yyjson_is_obj(adapter))
            return validation_error(ctx, path, "adapter entries must be objects");
        if (!require_unique_name(ctx, &names->adapters, "adapter", json_string(adapter, "name"), path))
            return false;
    }
    return true;
}

bool collect_scripts(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *scripts = obj_get(root, "scripts");
    if (scripts == NULL)
        return true;
    if (!yyjson_is_arr(scripts))
        return validation_error(ctx, "$.scripts", "scripts must be an array");

    names->script_count = (int)yyjson_arr_size(scripts);
    names->script_manifests =
        (script_manifest *)SDL_calloc((size_t)names->script_count, sizeof(*names->script_manifests));
    if (names->script_manifests == NULL && names->script_count > 0)
        return validation_error(ctx, "$.scripts", "failed to allocate script validation table");

    for (int i = 0; i < names->script_count; ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.scripts[%d]", i);
        yyjson_val *script = yyjson_arr_get(scripts, (size_t)i);
        script_manifest *manifest = &names->script_manifests[i];
        if (!yyjson_is_obj(script))
            return validation_error(ctx, path, "script entries must be objects");

        manifest->id = json_string(script, "id");
        manifest->path = json_string(script, "path");
        manifest->module = json_string(script, "module");

        if (!require_unique_name(ctx, &names->scripts, "script id", manifest->id, path))
            return false;
        manifest->json_path = names->scripts.paths[names->scripts.count - 1];
        if (!require_unique_name(ctx, &names->script_modules, "script module", manifest->module, path))
            return false;
        if (manifest->path == NULL || manifest->path[0] == '\0')
            return validation_error(ctx, path, "script '%s' requires a non-empty path", manifest->id);
        if (!script_path_exists(ctx, manifest->path, path))
            return false;

        yyjson_val *dependencies = obj_get(script, "dependencies");
        if (dependencies == NULL)
            continue;
        if (!yyjson_is_arr(dependencies))
            return validation_error(ctx, path, "script '%s' dependencies must be an array", manifest->id);

        manifest->dependency_count = (int)yyjson_arr_size(dependencies);
        manifest->dependencies =
            (const char **)SDL_calloc((size_t)manifest->dependency_count, sizeof(*manifest->dependencies));
        if (manifest->dependencies == NULL && manifest->dependency_count > 0)
            return validation_error(ctx, path, "failed to allocate dependencies for script '%s'", manifest->id);
        for (int d = 0; d < manifest->dependency_count; ++d)
        {
            char dep_path[PATH_BUFFER_SIZE];
            format_path(dep_path, sizeof(dep_path), "%s.dependencies[%d]", path, d);
            yyjson_val *dependency = yyjson_arr_get(dependencies, (size_t)d);
            if (!yyjson_is_str(dependency) || yyjson_get_str(dependency)[0] == '\0')
                return validation_error(ctx, dep_path, "script dependencies must be non-empty strings");
            manifest->dependencies[d] = yyjson_get_str(dependency);
        }
    }

    for (int i = 0; i < names->script_count; ++i)
    {
        if (!validate_script_cycle(ctx, names, &names->script_manifests[i]))
            return false;
    }
    return true;
}

bool collect_sensors(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *sensors = obj_get(obj_get(root, "logic"), "sensors");
    if (sensors == NULL)
        return true;
    if (!yyjson_is_arr(sensors))
        return validation_error(ctx, "$.logic.sensors", "logic sensors must be an array");

    for (size_t i = 0; i < yyjson_arr_size(sensors); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.logic.sensors[%zu]", i);
        yyjson_val *sensor = yyjson_arr_get(sensors, i);
        if (!yyjson_is_obj(sensor))
            return validation_error(ctx, path, "sensor entries must be objects");
        const char *name = json_string(sensor, "name");
        if (name != NULL && name[0] != '\0' && !require_unique_name(ctx, &names->sensors, "sensor", name, path))
            return false;
    }
    return true;
}

bool validate_logic(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *logic = obj_get(root, "logic");
    yyjson_val *timers = obj_get(logic, "timers");
    for (size_t i = 0; yyjson_is_arr(timers) && i < yyjson_arr_size(timers); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.logic.timers[%zu]", i);
        yyjson_val *timer = yyjson_arr_get(timers, i);
        if (!require_ref(ctx, &names->signals, "signal", json_string(timer, "signal"), path))
            return false;
    }

    yyjson_val *sensors = obj_get(logic, "sensors");
    for (size_t i = 0; yyjson_is_arr(sensors) && i < yyjson_arr_size(sensors); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.logic.sensors[%zu]", i);
        yyjson_val *sensor = yyjson_arr_get(sensors, i);
        const char *type = json_string(sensor, "type");
        if (type == NULL || type[0] == '\0')
            return validation_error(ctx, path, "sensor requires a non-empty type");
        if (SDL_strcmp(type, "sensor.bounds_exit") == 0)
        {
            if (!require_actor_ref(ctx, names, json_string(sensor, "entity"), path) ||
                !require_ref(ctx, &names->signals, "signal", json_string(sensor, "on_enter"), path) ||
                (!is_axis_name(json_string(sensor, "axis")) &&
                 !validation_error(ctx, path, "sensor.bounds_exit requires axis x, y, or z")) ||
                (!is_side_name(json_string(sensor, "side")) &&
                 !validation_error(ctx, path, "sensor.bounds_exit requires side min or max")))
                return false;
            continue;
        }
        if (SDL_strcmp(type, "sensor.bounds_reflect") == 0)
        {
            if (!require_actor_ref(ctx, names, json_string(sensor, "entity"), path) ||
                !require_ref(ctx, &names->signals, "signal", json_string(sensor, "on_reflect"), path) ||
                (!is_axis_name(json_string(sensor, "axis")) &&
                 !validation_error(ctx, path, "sensor.bounds_reflect requires axis x, y, or z")))
                return false;
            continue;
        }
        if (SDL_strcmp(type, "sensor.contact_2d") == 0 || SDL_strcmp(type, "collision.on_overlap") == 0)
        {
            const char *a = json_string(sensor, "a");
            const char *b = json_string(sensor, "b");
            const char *a_tag = json_string(sensor, "a_tag");
            const char *b_tag = json_string(sensor, "b_tag");
            if ((a == NULL && a_tag == NULL) || (a != NULL && a_tag != NULL))
                return validation_error(ctx, path, "sensor.contact_2d requires exactly one of a or a_tag");
            if ((b == NULL && b_tag == NULL) || (b != NULL && b_tag != NULL))
                return validation_error(ctx, path, "sensor.contact_2d requires exactly one of b or b_tag");
            if (a != NULL && !require_actor_ref(ctx, names, a, path))
                return false;
            if (b != NULL && !require_actor_ref(ctx, names, b, path))
                return false;
            if (a_tag != NULL && a_tag[0] == '\0')
                return validation_error(ctx, path, "sensor.contact_2d a_tag must be non-empty");
            if (b_tag != NULL && b_tag[0] == '\0')
                return validation_error(ctx, path, "sensor.contact_2d b_tag must be non-empty");
            yyjson_val *actions = obj_get(sensor, "actions");
            const char *on_enter = json_string(sensor, "on_enter");
            if (actions != NULL && !validate_action_array(ctx, actions, path, names))
                return false;
            if (actions == NULL && !require_ref(ctx, &names->signals, "signal", on_enter, path))
                return false;
            yyjson_val *edge = obj_get(sensor, "edge");
            if (edge != NULL && (!yyjson_is_str(edge) || (SDL_strcmp(yyjson_get_str(edge), "enter") != 0 &&
                                                          SDL_strcmp(yyjson_get_str(edge), "stay") != 0 &&
                                                          SDL_strcmp(yyjson_get_str(edge), "overlap") != 0)))
            {
                return validation_error(ctx, path, "collision edge must be enter, stay, or overlap");
            }
            if (!validate_target_filter_fields(ctx, sensor, path, "sensor.contact_2d"))
                return false;
            continue;
        }
        if (SDL_strcmp(type, "sensor.input_pressed") == 0)
        {
            if (!require_ref(ctx, &names->actions, "input action", json_string(sensor, "action"), path) ||
                !require_ref(ctx, &names->signals, "signal", json_string(sensor, "on_pressed"), path))
                return false;
            continue;
        }
        if (SDL_strcmp(type, "sensor.hearing") == 0)
        {
            const char *actor = json_string(sensor, "actor");
            if (actor == NULL)
                actor = json_string(sensor, "entity");
            const char *actor_tag = json_string(sensor, "actor_tag");
            if (actor_tag == NULL)
                actor_tag = json_string(sensor, "observer_tag");
            yyjson_val *actions = obj_get(sensor, "actions");
            const char *edge = json_string(sensor, "edge");
            const char *signal = json_string(sensor, "on_enter");
            if (edge != NULL && (SDL_strcmp(edge, "stay") == 0 || SDL_strcmp(edge, "overlap") == 0))
            {
                const char *on_stay = json_string(sensor, "on_stay");
                signal = on_stay != NULL ? on_stay : signal;
            }
            else if (edge != NULL && SDL_strcmp(edge, "exit") == 0)
            {
                const char *on_exit = json_string(sensor, "on_exit");
                signal = on_exit != NULL ? on_exit : signal;
            }

            if ((actor == NULL && actor_tag == NULL) || (actor != NULL && actor_tag != NULL))
                return validation_error(ctx, path, "sensor.hearing requires exactly one of actor or actor_tag");
            if (actor != NULL && !require_actor_ref(ctx, names, actor, path))
                return false;
            if (actor_tag != NULL && actor_tag[0] == '\0')
                return validation_error(ctx, path, "sensor.hearing actor_tag must be non-empty");
            yyjson_val *target_tag = obj_get(sensor, "target_tag");
            if (target_tag != NULL && (!yyjson_is_str(target_tag) || yyjson_get_str(target_tag)[0] == '\0'))
                return validation_error(ctx, path, "sensor.hearing target_tag must be non-empty");
            yyjson_val *range = obj_get(sensor, "range");
            if (range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) <= 0.0))
                return validation_error(ctx, path, "sensor.hearing range must be positive");
            if (edge != NULL && SDL_strcmp(edge, "enter") != 0 && SDL_strcmp(edge, "stay") != 0 &&
                SDL_strcmp(edge, "overlap") != 0 && SDL_strcmp(edge, "exit") != 0)
            {
                return validation_error(ctx, path, "sensor.hearing edge must be enter, stay, overlap, or exit");
            }
            if (actions != NULL && !validate_action_array(ctx, actions, path, names))
                return false;
            if (actions == NULL && !require_ref(ctx, &names->signals, "signal", signal, path))
                return false;
            if (!validate_target_filter_fields(ctx, sensor, path, "sensor.hearing"))
                return false;
            continue;
        }
        if (SDL_strcmp(type, "sensor.brush_contents") == 0)
        {
            const char *actor = json_string(sensor, "actor");
            if (actor == NULL)
                actor = json_string(sensor, "entity");
            const char *actor_tag = json_string(sensor, "actor_tag");
            if (actor_tag == NULL)
                actor_tag = json_string(sensor, "a_tag");
            yyjson_val *actions = obj_get(sensor, "actions");
            const char *edge = json_string(sensor, "edge");
            const char *signal = json_string(sensor, "on_enter");
            if (edge != NULL && (SDL_strcmp(edge, "stay") == 0 || SDL_strcmp(edge, "overlap") == 0))
            {
                const char *on_stay = json_string(sensor, "on_stay");
                signal = on_stay != NULL ? on_stay : signal;
            }
            else if (edge != NULL && SDL_strcmp(edge, "exit") == 0)
            {
                const char *on_exit = json_string(sensor, "on_exit");
                signal = on_exit != NULL ? on_exit : signal;
            }

            if ((actor == NULL && actor_tag == NULL) || (actor != NULL && actor_tag != NULL))
                return validation_error(ctx, path, "sensor.brush_contents requires exactly one of actor or actor_tag");
            if (actor != NULL && !require_actor_ref(ctx, names, actor, path))
                return false;
            if (actor_tag != NULL && actor_tag[0] == '\0')
                return validation_error(ctx, path, "sensor.brush_contents actor_tag must be non-empty");
            char contents_path[PATH_BUFFER_SIZE];
            format_path(contents_path, sizeof(contents_path), "%s.contents_mask", path);
            if (!validate_brush_string_or_string_array(ctx, obj_get(sensor, "contents_mask"), contents_path,
                                                       "brush content", brush_content_name_valid, false))
                return false;
            if (edge != NULL && SDL_strcmp(edge, "enter") != 0 && SDL_strcmp(edge, "stay") != 0 &&
                SDL_strcmp(edge, "overlap") != 0 && SDL_strcmp(edge, "exit") != 0)
            {
                return validation_error(ctx, path, "sensor.brush_contents edge must be enter, stay, overlap, or exit");
            }
            if (actions != NULL && !validate_action_array(ctx, actions, path, names))
                return false;
            if (actions == NULL && !require_ref(ctx, &names->signals, "signal", signal, path))
                return false;
            if (!validate_target_filter_fields(ctx, sensor, path, "sensor.brush_contents"))
                return false;
            continue;
        }
        if (SDL_strcmp(type, "sensor.brush_perception") == 0)
        {
            const char *observer = json_string(sensor, "observer");
            if (observer == NULL)
                observer = json_string(sensor, "entity");
            if (observer == NULL)
                observer = json_string(sensor, "actor");
            const char *observer_tag = json_string(sensor, "observer_tag");
            if (observer_tag == NULL)
                observer_tag = json_string(sensor, "actor_tag");
            const char *target = json_string(sensor, "target");
            if (target == NULL)
                target = json_string(sensor, "b");
            const char *target_tag = json_string(sensor, "target_tag");
            if (target_tag == NULL)
                target_tag = json_string(sensor, "b_tag");
            yyjson_val *actions = obj_get(sensor, "actions");
            const char *edge = json_string(sensor, "edge");
            const char *signal = json_string(sensor, "on_enter");
            if (edge != NULL && (SDL_strcmp(edge, "stay") == 0 || SDL_strcmp(edge, "overlap") == 0))
            {
                const char *on_stay = json_string(sensor, "on_stay");
                signal = on_stay != NULL ? on_stay : signal;
            }
            else if (edge != NULL && SDL_strcmp(edge, "exit") == 0)
            {
                const char *on_exit = json_string(sensor, "on_exit");
                signal = on_exit != NULL ? on_exit : signal;
            }

            if ((observer == NULL && observer_tag == NULL) || (observer != NULL && observer_tag != NULL))
                return validation_error(ctx, path,
                                        "sensor.brush_perception requires exactly one of observer or observer_tag");
            if ((target == NULL && target_tag == NULL) || (target != NULL && target_tag != NULL))
                return validation_error(ctx, path,
                                        "sensor.brush_perception requires exactly one of target or target_tag");
            if (observer != NULL && !require_actor_ref(ctx, names, observer, path))
                return false;
            if (target != NULL && !require_actor_ref(ctx, names, target, path))
                return false;
            if (observer_tag != NULL && observer_tag[0] == '\0')
                return validation_error(ctx, path, "sensor.brush_perception observer_tag must be non-empty");
            if (target_tag != NULL && target_tag[0] == '\0')
                return validation_error(ctx, path, "sensor.brush_perception target_tag must be non-empty");
            yyjson_val *range = obj_get(sensor, "range");
            if (range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) <= 0.0))
                return validation_error(ctx, path, "sensor.brush_perception range must be positive");
            yyjson_val *min_dot = obj_get(sensor, "min_dot");
            if (min_dot != NULL &&
                (!yyjson_is_num(min_dot) || yyjson_get_num(min_dot) < -1.0 || yyjson_get_num(min_dot) > 1.0))
            {
                return validation_error(ctx, path, "sensor.brush_perception min_dot must be between -1 and 1");
            }
            yyjson_val *fov_degrees = obj_get(sensor, "fov_degrees");
            if (fov_degrees != NULL && (!yyjson_is_num(fov_degrees) || yyjson_get_num(fov_degrees) <= 0.0 ||
                                        yyjson_get_num(fov_degrees) > 360.0))
            {
                return validation_error(ctx, path, "sensor.brush_perception fov_degrees must be in the range (0, 360]");
            }
            yyjson_val *observer_eye_height = obj_get(sensor, "observer_eye_height");
            yyjson_val *target_eye_height = obj_get(sensor, "target_eye_height");
            yyjson_val *eye_height = obj_get(sensor, "eye_height");
            if ((observer_eye_height != NULL && !yyjson_is_num(observer_eye_height)) ||
                (target_eye_height != NULL && !yyjson_is_num(target_eye_height)) ||
                (eye_height != NULL && !yyjson_is_num(eye_height)))
            {
                return validation_error(ctx, path, "sensor.brush_perception eye heights must be numeric");
            }
            yyjson_val *yaw_property = obj_get(sensor, "yaw_property");
            if (yaw_property != NULL && (!yyjson_is_str(yaw_property) || yyjson_get_str(yaw_property)[0] == '\0'))
                return validation_error(ctx, path, "sensor.brush_perception yaw_property must be a non-empty string");
            char contents_path[PATH_BUFFER_SIZE];
            format_path(contents_path, sizeof(contents_path), "%s.contents_mask", path);
            if (!validate_brush_string_or_string_array(ctx, obj_get(sensor, "contents_mask"), contents_path,
                                                       "brush content", brush_content_name_valid, false))
                return false;
            if (edge != NULL && SDL_strcmp(edge, "enter") != 0 && SDL_strcmp(edge, "stay") != 0 &&
                SDL_strcmp(edge, "overlap") != 0 && SDL_strcmp(edge, "exit") != 0)
            {
                return validation_error(ctx, path,
                                        "sensor.brush_perception edge must be enter, stay, overlap, or exit");
            }
            if (actions != NULL && !validate_action_array(ctx, actions, path, names))
                return false;
            if (actions == NULL && !require_ref(ctx, &names->signals, "signal", signal, path))
                return false;
            if (!validate_target_filter_fields(ctx, sensor, path, "sensor.brush_perception"))
                return false;
            continue;
        }
        if (SDL_strcmp(type, "sensor.perception") == 0)
        {
            const char *observer = json_string(sensor, "observer");
            if (observer == NULL)
                observer = json_string(sensor, "entity");
            if (observer == NULL)
                observer = json_string(sensor, "actor");
            const char *observer_tag = json_string(sensor, "observer_tag");
            if (observer_tag == NULL)
                observer_tag = json_string(sensor, "actor_tag");
            const char *target = json_string(sensor, "target");
            if (target == NULL)
                target = json_string(sensor, "b");
            const char *target_tag = json_string(sensor, "target_tag");
            if (target_tag == NULL)
                target_tag = json_string(sensor, "b_tag");
            yyjson_val *actions = obj_get(sensor, "actions");
            const char *edge = json_string(sensor, "edge");
            const char *signal = json_string(sensor, "on_enter");
            if (edge != NULL && (SDL_strcmp(edge, "stay") == 0 || SDL_strcmp(edge, "overlap") == 0))
            {
                const char *on_stay = json_string(sensor, "on_stay");
                signal = on_stay != NULL ? on_stay : signal;
            }
            else if (edge != NULL && SDL_strcmp(edge, "exit") == 0)
            {
                const char *on_exit = json_string(sensor, "on_exit");
                signal = on_exit != NULL ? on_exit : signal;
            }

            if ((observer == NULL && observer_tag == NULL) || (observer != NULL && observer_tag != NULL))
                return validation_error(ctx, path,
                                        "sensor.perception requires exactly one of observer or observer_tag");
            if ((target == NULL && target_tag == NULL) || (target != NULL && target_tag != NULL))
                return validation_error(ctx, path, "sensor.perception requires exactly one of target or target_tag");
            if (observer != NULL && !require_actor_ref(ctx, names, observer, path))
                return false;
            if (target != NULL && !require_actor_ref(ctx, names, target, path))
                return false;
            if (observer_tag != NULL && observer_tag[0] == '\0')
                return validation_error(ctx, path, "sensor.perception observer_tag must be non-empty");
            if (target_tag != NULL && target_tag[0] == '\0')
                return validation_error(ctx, path, "sensor.perception target_tag must be non-empty");
            if (!require_ref(ctx, &names->sector_levels, "sector level", json_string(sensor, "sector_level"), path))
                return false;
            yyjson_val *range = obj_get(sensor, "range");
            if (range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) <= 0.0))
                return validation_error(ctx, path, "sensor.perception range must be positive");
            yyjson_val *min_dot = obj_get(sensor, "min_dot");
            if (min_dot != NULL &&
                (!yyjson_is_num(min_dot) || yyjson_get_num(min_dot) < -1.0 || yyjson_get_num(min_dot) > 1.0))
            {
                return validation_error(ctx, path, "sensor.perception min_dot must be between -1 and 1");
            }
            yyjson_val *fov_degrees = obj_get(sensor, "fov_degrees");
            if (fov_degrees != NULL && (!yyjson_is_num(fov_degrees) || yyjson_get_num(fov_degrees) <= 0.0 ||
                                        yyjson_get_num(fov_degrees) > 360.0))
            {
                return validation_error(ctx, path, "sensor.perception fov_degrees must be in the range (0, 360]");
            }
            yyjson_val *observer_eye_height = obj_get(sensor, "observer_eye_height");
            yyjson_val *target_eye_height = obj_get(sensor, "target_eye_height");
            yyjson_val *eye_height = obj_get(sensor, "eye_height");
            if ((observer_eye_height != NULL && !yyjson_is_num(observer_eye_height)) ||
                (target_eye_height != NULL && !yyjson_is_num(target_eye_height)) ||
                (eye_height != NULL && !yyjson_is_num(eye_height)))
            {
                return validation_error(ctx, path, "sensor.perception eye heights must be numeric");
            }
            yyjson_val *yaw_property = obj_get(sensor, "yaw_property");
            if (yaw_property != NULL && (!yyjson_is_str(yaw_property) || yyjson_get_str(yaw_property)[0] == '\0'))
                return validation_error(ctx, path, "sensor.perception yaw_property must be a non-empty string");
            if (edge != NULL && SDL_strcmp(edge, "enter") != 0 && SDL_strcmp(edge, "stay") != 0 &&
                SDL_strcmp(edge, "overlap") != 0 && SDL_strcmp(edge, "exit") != 0)
            {
                return validation_error(ctx, path, "sensor.perception edge must be enter, stay, overlap, or exit");
            }
            if (actions != NULL && !validate_action_array(ctx, actions, path, names))
                return false;
            if (actions == NULL && !require_ref(ctx, &names->signals, "signal", signal, path))
                return false;
            if (!validate_target_filter_fields(ctx, sensor, path, "sensor.perception"))
                return false;
            continue;
        }
        if (SDL_strcmp(type, "sensor.sector") == 0)
        {
            const char *actor = json_string(sensor, "actor");
            if (actor == NULL)
                actor = json_string(sensor, "entity");
            const char *actor_tag = json_string(sensor, "actor_tag");
            if (actor_tag == NULL)
                actor_tag = json_string(sensor, "a_tag");
            const char *sector_level = json_string(sensor, "sector_level");
            const char *sector = json_string(sensor, "sector");
            yyjson_val *sector_index = obj_get(sensor, "sector_index");
            yyjson_val *actions = obj_get(sensor, "actions");
            const char *edge = json_string(sensor, "edge");
            const char *signal = json_string(sensor, "on_enter");
            if (edge != NULL && (SDL_strcmp(edge, "stay") == 0 || SDL_strcmp(edge, "overlap") == 0))
            {
                const char *on_stay = json_string(sensor, "on_stay");
                signal = on_stay != NULL ? on_stay : signal;
            }
            else if (edge != NULL && SDL_strcmp(edge, "exit") == 0)
            {
                const char *on_exit = json_string(sensor, "on_exit");
                signal = on_exit != NULL ? on_exit : signal;
            }

            if ((actor == NULL && actor_tag == NULL) || (actor != NULL && actor_tag != NULL))
                return validation_error(ctx, path, "sensor.sector requires exactly one of actor or actor_tag");
            if (actor != NULL && !require_actor_ref(ctx, names, actor, path))
                return false;
            if (actor_tag != NULL && actor_tag[0] == '\0')
                return validation_error(ctx, path, "sensor.sector actor_tag must be non-empty");
            if (!require_ref(ctx, &names->sector_levels, "sector level", sector_level, path))
                return false;
            if ((sector == NULL && sector_index == NULL) || (sector != NULL && sector_index != NULL))
                return validation_error(ctx, path, "sensor.sector requires exactly one of sector or sector_index");
            if (sector != NULL && !sector_level_has_sector_name(root, sector_level, sector))
                return validation_error(ctx, path, "unknown sensor.sector sector '%s'", sector);
            if (sector_index != NULL &&
                (!yyjson_is_int(sector_index) ||
                 !sector_level_has_sector_index(root, sector_level, (int)yyjson_get_int(sector_index))))
            {
                return validation_error(ctx, path,
                                        "sensor.sector sector_index must reference a sector in sector_level");
            }
            if (edge != NULL && SDL_strcmp(edge, "enter") != 0 && SDL_strcmp(edge, "stay") != 0 &&
                SDL_strcmp(edge, "overlap") != 0 && SDL_strcmp(edge, "exit") != 0)
            {
                return validation_error(ctx, path, "sensor.sector edge must be enter, stay, overlap, or exit");
            }
            yyjson_val *sector_property = obj_get(sensor, "sector_property");
            if (sector_property != NULL &&
                (!yyjson_is_str(sector_property) || yyjson_get_str(sector_property)[0] == '\0'))
                return validation_error(ctx, path, "sensor.sector sector_property must be a non-empty string");
            if (actions != NULL && !validate_action_array(ctx, actions, path, names))
                return false;
            if (actions == NULL && !require_ref(ctx, &names->signals, "signal", signal, path))
                return false;
            if (!validate_target_filter_fields(ctx, sensor, path, "sensor.sector"))
                return false;
            continue;
        }
        if (SDL_strcmp(type, "sensor.volume") == 0)
        {
            const char *actor = json_string(sensor, "actor");
            if (actor == NULL)
                actor = json_string(sensor, "entity");
            const char *actor_tag = json_string(sensor, "actor_tag");
            if (actor_tag == NULL)
                actor_tag = json_string(sensor, "a_tag");
            yyjson_val *min_value = obj_get(sensor, "min");
            yyjson_val *max_value = obj_get(sensor, "max");
            yyjson_val *actions = obj_get(sensor, "actions");
            const char *edge = json_string(sensor, "edge");
            const char *signal = json_string(sensor, "on_enter");
            if (edge != NULL && (SDL_strcmp(edge, "stay") == 0 || SDL_strcmp(edge, "overlap") == 0))
            {
                const char *on_stay = json_string(sensor, "on_stay");
                signal = on_stay != NULL ? on_stay : signal;
            }
            else if (edge != NULL && SDL_strcmp(edge, "exit") == 0)
            {
                const char *on_exit = json_string(sensor, "on_exit");
                signal = on_exit != NULL ? on_exit : signal;
            }

            if ((actor == NULL && actor_tag == NULL) || (actor != NULL && actor_tag != NULL))
                return validation_error(ctx, path, "sensor.volume requires exactly one of actor or actor_tag");
            if (actor != NULL && !require_actor_ref(ctx, names, actor, path))
                return false;
            if (actor_tag != NULL && actor_tag[0] == '\0')
                return validation_error(ctx, path, "sensor.volume actor_tag must be non-empty");
            if (!is_vec_array(min_value, 3) || !is_vec_array(max_value, 3))
                return validation_error(ctx, path, "sensor.volume min and max must be vec3 values");
            if (yyjson_get_num(yyjson_arr_get(min_value, 0)) > yyjson_get_num(yyjson_arr_get(max_value, 0)) ||
                yyjson_get_num(yyjson_arr_get(min_value, 1)) > yyjson_get_num(yyjson_arr_get(max_value, 1)) ||
                yyjson_get_num(yyjson_arr_get(min_value, 2)) > yyjson_get_num(yyjson_arr_get(max_value, 2)))
            {
                return validation_error(ctx, path, "sensor.volume min must be less than or equal to max");
            }
            if (edge != NULL && SDL_strcmp(edge, "enter") != 0 && SDL_strcmp(edge, "stay") != 0 &&
                SDL_strcmp(edge, "overlap") != 0 && SDL_strcmp(edge, "exit") != 0)
            {
                return validation_error(ctx, path, "sensor.volume edge must be enter, stay, overlap, or exit");
            }
            if (actions != NULL && !validate_action_array(ctx, actions, path, names))
                return false;
            if (actions == NULL && !require_ref(ctx, &names->signals, "signal", signal, path))
                return false;
            if (!validate_target_filter_fields(ctx, sensor, path, "sensor.volume"))
                return false;
            continue;
        }
        return validation_error(ctx, path, "unsupported sensor type '%s'", type);
    }

    yyjson_val *wave_schedules = obj_get(logic, "wave_schedules");
    if (wave_schedules != NULL && !yyjson_is_arr(wave_schedules))
        return validation_error(ctx, "$.logic.wave_schedules", "logic wave_schedules must be an array");
    for (size_t i = 0; yyjson_is_arr(wave_schedules) && i < yyjson_arr_size(wave_schedules); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.logic.wave_schedules[%zu]", i);
        yyjson_val *schedule = yyjson_arr_get(wave_schedules, i);
        if (!yyjson_is_obj(schedule))
            return validation_error(ctx, path, "wave schedule must be an object");
        if (!require_ref(ctx, &names->actor_pools, "actor pool", json_string(schedule, "pool"), path))
            return false;
        if (!yyjson_is_num(obj_get(schedule, "interval")))
            return validation_error(ctx, path, "wave schedule requires numeric interval");
        yyjson_val *active_if = obj_get(schedule, "active_if");
        if (active_if != NULL && !validate_data_condition(ctx, active_if, path, names))
            return false;
        yyjson_val *position = obj_get(schedule, "position");
        if (position != NULL && !yyjson_is_arr(position) && !yyjson_is_obj(position))
            return validation_error(ctx, path, "wave schedule position must be a vec3 array or axis object");
        if (yyjson_is_arr(position) && !is_vec_array(position, 3))
            return validation_error(ctx, path, "wave schedule position array must be a vec3");
        if (yyjson_is_obj(position) &&
            (!is_wave_axis_value(obj_get(position, "x")) || !is_wave_axis_value(obj_get(position, "y")) ||
             !is_wave_axis_value(obj_get(position, "z"))))
        {
            return validation_error(ctx, path, "wave schedule position axes must be numbers or [min, max] arrays");
        }
        yyjson_val *velocity = obj_get(schedule, "velocity");
        if (velocity != NULL && !is_vec_array(velocity, 3))
            return validation_error(ctx, path, "wave schedule velocity must be a vec3");
        yyjson_val *max_active_tag = obj_get(schedule, "max_active_tag");
        if (max_active_tag != NULL && (!yyjson_is_str(max_active_tag) || yyjson_get_str(max_active_tag)[0] == '\0'))
            return validation_error(ctx, path, "wave schedule max_active_tag must be a non-empty string");
        yyjson_val *max_active = obj_get(schedule, "max_active");
        if (max_active != NULL && !yyjson_is_int(max_active))
            return validation_error(ctx, path, "wave schedule max_active must be an integer");
        yyjson_val *initial_delay = obj_get(schedule, "initial_delay");
        if (initial_delay != NULL && !yyjson_is_num(initial_delay))
            return validation_error(ctx, path, "wave schedule initial_delay must be numeric");
        yyjson_val *catch_up = obj_get(schedule, "catch_up");
        if (catch_up != NULL && !yyjson_is_bool(catch_up))
            return validation_error(ctx, path, "wave schedule catch_up must be bool");
        yyjson_val *properties = obj_get(schedule, "properties");
        if (properties != NULL && !yyjson_is_obj(properties))
            return validation_error(ctx, path, "wave schedule properties must be an object");
    }

    yyjson_val *bindings = obj_get(logic, "bindings");
    if (bindings != NULL && !yyjson_is_arr(bindings))
        return validation_error(ctx, "$.logic.bindings", "logic bindings must be an array");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.logic.bindings[%zu]", i);
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        if (!yyjson_is_obj(binding))
            return validation_error(ctx, path, "logic binding must be an object");
        if (!require_ref(ctx, &names->signals, "signal", json_string(binding, "signal"), path))
            return false;
        char actions_path[PATH_BUFFER_SIZE];
        format_path(actions_path, sizeof(actions_path), "%s.actions", path);
        if (!validate_action_array(ctx, obj_get(binding, "actions"), actions_path, names))
            return false;
    }
    return true;
}

static bool note_script_use_recursive(validation_context *ctx, validation_names *names, const char *script_id,
                                      const char *json_path)
{
    script_manifest *script = find_script_manifest(names, script_id);
    if (script == NULL)
        return validation_error(ctx, json_path, "unknown script reference '%s'", script_id);
    if (!note_name(&names->used_scripts, script_id, json_path))
        return validation_error(ctx, json_path, "failed to record script use");
    for (int i = 0; i < script->dependency_count; ++i)
    {
        if (!note_script_use_recursive(ctx, names, script->dependencies[i], json_path))
            return false;
    }
    return true;
}

bool validate_adapters(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *adapters = obj_get(root, "adapters");
    for (size_t i = 0; yyjson_is_arr(adapters) && i < yyjson_arr_size(adapters); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.adapters[%zu]", i);
        yyjson_val *adapter = yyjson_arr_get(adapters, i);
        const char *script = json_string(adapter, "script");
        const char *function = json_string(adapter, "function");
        const char *name = json_string(adapter, "name");
        if (function != NULL && function[0] != '\0')
        {
            if (!require_ref(ctx, &names->scripts, "script", script, path))
                return false;
            if (name_table_contains(&names->used_adapters, name) &&
                !note_script_use_recursive(ctx, names, script, path))
                return false;
        }
    }
    return true;
}

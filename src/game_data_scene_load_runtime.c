/**
 * @file game_data_scene_load_runtime.c
 * @brief Scene activity, bindings, sensors, waves, and scene loading helpers.
 */

#include "game_data_internal.h"

#include "game_data_standard_options.h"

#include <SDL3/SDL_log.h>

float slayer3d_game_data_delta_time(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->current_dt : 0.0f;
}

static yyjson_val *active_scene_activity_json(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *activity = obj_get(scene != NULL ? scene->root : NULL, "activity");
    if (!yyjson_is_obj(activity) || !json_bool(activity, "enabled", true))
        return NULL;
    return activity;
}

static bool ensure_activity_periodic_capacity(scene_activity_state *state, int required)
{
    if (state == NULL)
        return false;
    if (required <= state->periodic_capacity)
        return true;

    int next_capacity = state->periodic_capacity < 4 ? 4 : state->periodic_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    float *elapsed = (float *)SDL_realloc(state->periodic_elapsed, (size_t)next_capacity * sizeof(*elapsed));
    if (elapsed == NULL)
        return false;
    SDL_memset(elapsed + state->periodic_capacity, 0,
               (size_t)(next_capacity - state->periodic_capacity) * sizeof(*elapsed));
    state->periodic_elapsed = elapsed;
    state->periodic_capacity = next_capacity;
    return true;
}

static bool activity_reset_for_scene(slayer3d_game_data_runtime *runtime, const char *scene, yyjson_val *activity)
{
    scene_activity_state *state = &runtime->activity;
    state->scene = scene;
    state->idle_elapsed = 0.0f;
    state->idle = false;
    state->entered = false;

    const int periodic_count = (int)yyjson_arr_size(obj_get(activity, "periodic"));
    if (!ensure_activity_periodic_capacity(state, periodic_count))
        return false;
    state->periodic_count = periodic_count;
    if (periodic_count > 0)
        SDL_memset(state->periodic_elapsed, 0, (size_t)periodic_count * sizeof(*state->periodic_elapsed));
    return true;
}

static bool activity_input_matches(const slayer3d_game_data_runtime *runtime, yyjson_val *activity,
                                   const slayer3d_input_manager *input)
{
    if (runtime == NULL || activity == NULL || input == NULL)
        return false;

    const char *mode = json_string(activity, "input", "any");
    if (SDL_strcmp(mode, "disabled") == 0 || SDL_strcmp(mode, "none") == 0)
        return false;
    if (SDL_strcmp(mode, "action") == 0)
    {
        const int action_id = slayer3d_game_data_find_action(runtime, json_string(activity, "action", NULL));
        return action_id >= 0 && slayer3d_input_is_pressed(input, action_id);
    }
    return slayer3d_input_any_pressed(input);
}

bool slayer3d_game_data_scene_activity_consumes_wake_input(const slayer3d_game_data_runtime *runtime,
                                                           const slayer3d_input_manager *input, bool *out_block_menus,
                                                           bool *out_block_scene_shortcuts)
{
    if (out_block_menus != NULL)
        *out_block_menus = false;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = false;
    if (runtime == NULL || input == NULL)
        return false;

    yyjson_val *activity = active_scene_activity_json(runtime);
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    const scene_activity_state *state = &runtime->activity;
    if (activity == NULL || state->scene != active_scene || !state->idle ||
        !activity_input_matches(runtime, activity, input))
    {
        return false;
    }

    const bool consume = json_bool(activity, "consume_wake_input", false);
    if (out_block_menus != NULL)
        *out_block_menus = json_bool(activity, "block_menus_on_wake", consume);
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = json_bool(activity, "block_scene_shortcuts_on_wake", consume);
    return consume;
}

bool slayer3d_game_data_update_scene_activity(slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                              float dt)
{
    if (runtime == NULL)
        return false;

    if (dt < 0.0f)
        dt = 0.0f;

    yyjson_val *activity = active_scene_activity_json(runtime);
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    scene_activity_state *state = &runtime->activity;
    if (activity == NULL)
    {
        state->scene = active_scene;
        state->idle_elapsed = 0.0f;
        state->idle = false;
        state->entered = false;
        state->periodic_count = 0;
        return true;
    }

    if (state->scene != active_scene)
    {
        if (!activity_reset_for_scene(runtime, active_scene, activity))
            return false;
    }

    bool ok = true;
    if (!state->entered)
    {
        ok = execute_optional_action_array(runtime, obj_get(activity, "on_enter"), NULL) && ok;
        state->entered = true;
    }

    const bool input_active = activity_input_matches(runtime, activity, input);
    if (input_active)
    {
        state->idle_elapsed = 0.0f;
        if (json_bool(activity, "reset_periodic_on_input", true) && state->periodic_count > 0)
            SDL_memset(state->periodic_elapsed, 0, (size_t)state->periodic_count * sizeof(*state->periodic_elapsed));
        if (state->idle)
        {
            state->idle = false;
            ok = execute_optional_action_array(runtime, obj_get(activity, "on_active"), NULL) && ok;
        }
    }
    else
    {
        state->idle_elapsed += dt;
    }

    const float idle_after = json_float(activity, "idle_after", json_float(activity, "idle_seconds", -1.0f));
    if (idle_after >= 0.0f && !state->idle && state->idle_elapsed >= idle_after)
    {
        state->idle = true;
        ok = execute_optional_action_array(runtime, obj_get(activity, "on_idle"), NULL) && ok;
    }

    yyjson_val *periodic = obj_get(activity, "periodic");
    const int periodic_count = (int)yyjson_arr_size(periodic);
    if (periodic_count != state->periodic_count && !activity_reset_for_scene(runtime, active_scene, activity))
        return false;
    for (int i = 0; i < state->periodic_count; ++i)
    {
        yyjson_val *entry = yyjson_arr_get(periodic, (size_t)i);
        if (!yyjson_is_obj(entry))
            continue;
        const float interval = json_float(entry, "interval", 0.0f);
        if (interval <= 0.0f)
            continue;
        state->periodic_elapsed[i] += dt;
        if (state->periodic_elapsed[i] < interval)
            continue;

        state->periodic_elapsed[i] = SDL_fmodf(state->periodic_elapsed[i], interval);
        ok = execute_optional_action_array(runtime, obj_get(entry, "actions"), NULL) && ok;
        if (json_bool(entry, "reset_idle", false))
        {
            state->idle_elapsed = 0.0f;
            state->idle = false;
        }
    }
    return ok;
}

static void execute_binding(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    binding_entry *binding = (binding_entry *)userdata;
    (void)signal_id;
    if (binding != NULL)
        execute_action_array(binding->runtime, binding->actions, payload);
}

bool load_bindings(slayer3d_game_data_runtime *runtime, yyjson_val *logic, char *error_buffer, int error_buffer_size)
{
    yyjson_val *bindings = obj_get(logic, "bindings");
    if (!yyjson_is_arr(bindings))
        return true;

    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data logic bindings require a signal bus");
        return false;
    }

    const int count = (int)yyjson_arr_size(bindings);
    runtime->bindings = (binding_entry *)SDL_calloc((size_t)count, sizeof(*runtime->bindings));
    if (runtime->bindings == NULL && count > 0)
        return false;
    runtime->binding_count = count;

    for (int i = 0; i < count; ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, (size_t)i);
        const int signal_id = slayer3d_game_data_find_signal(runtime, json_string(binding, "signal", NULL));
        if (signal_id < 0)
            continue;
        runtime->bindings[i].runtime = runtime;
        runtime->bindings[i].actions = obj_get(binding, "actions");
        runtime->bindings[i].connection_id =
            slayer3d_signal_connect(bus, signal_id, execute_binding, &runtime->bindings[i]);
        if (runtime->bindings[i].connection_id == 0)
            return false;
    }
    return true;
}

bool load_sensors(slayer3d_game_data_runtime *runtime, yyjson_val *logic)
{
    yyjson_val *sensors = obj_get(logic, "sensors");
    if (!yyjson_is_arr(sensors))
        return true;

    const int count = (int)yyjson_arr_size(sensors);
    runtime->sensors = (sensor_entry *)SDL_calloc((size_t)count, sizeof(*runtime->sensors));
    if (runtime->sensors == NULL && count > 0)
        return false;
    runtime->sensor_count = count;

    for (int i = 0; i < count; ++i)
    {
        yyjson_val *sensor = yyjson_arr_get(sensors, (size_t)i);
        sensor_entry *entry = &runtime->sensors[i];
        entry->json = sensor;
        const char *type = json_string(sensor, "type", "");
        if (SDL_strcmp(type, "sensor.bounds_exit") == 0)
            entry->type = GAME_DATA_SENSOR_BOUNDS_EXIT;
        else if (SDL_strcmp(type, "sensor.bounds_reflect") == 0)
            entry->type = GAME_DATA_SENSOR_BOUNDS_REFLECT;
        else if (SDL_strcmp(type, "sensor.contact_2d") == 0)
            entry->type = GAME_DATA_SENSOR_CONTACT_2D;
        else if (SDL_strcmp(type, "collision.on_overlap") == 0)
            entry->type = GAME_DATA_SENSOR_CONTACT_2D;
        else if (SDL_strcmp(type, "sensor.hearing") == 0)
            entry->type = GAME_DATA_SENSOR_HEARING;
        else if (SDL_strcmp(type, "sensor.input_pressed") == 0)
            entry->type = GAME_DATA_SENSOR_INPUT_PRESSED;
        else if (SDL_strcmp(type, "sensor.brush_contents") == 0)
            entry->type = GAME_DATA_SENSOR_BRUSH_CONTENTS;
        else if (SDL_strcmp(type, "sensor.brush_perception") == 0)
            entry->type = GAME_DATA_SENSOR_BRUSH_PERCEPTION;
        else if (SDL_strcmp(type, "sensor.perception") == 0)
            entry->type = GAME_DATA_SENSOR_PERCEPTION;
        else if (SDL_strcmp(type, "sensor.sector") == 0)
            entry->type = GAME_DATA_SENSOR_SECTOR;
        else if (SDL_strcmp(type, "sensor.volume") == 0)
            entry->type = GAME_DATA_SENSOR_VOLUME;

        entry->name = json_string(sensor, "name", NULL);
        entry->entity = json_string(sensor, "observer", json_string(sensor, "entity", json_string(sensor, "a", NULL)));
        if (entry->entity == NULL)
            entry->entity = json_string(sensor, "actor", NULL);
        entry->other = json_string(sensor, "target", json_string(sensor, "b", NULL));
        entry->entity_tag = json_string(sensor, "observer_tag", json_string(sensor, "a_tag", NULL));
        if (entry->entity_tag == NULL)
            entry->entity_tag = json_string(sensor, "actor_tag", NULL);
        entry->other_tag = json_string(sensor, "target_tag", json_string(sensor, "b_tag", NULL));
        entry->sector_level = json_string(sensor, "sector_level", NULL);
        entry->sector = json_string(sensor, "sector", NULL);
        entry->sector_property = json_string(sensor, "sector_property", "current_sector");
        entry->sector_index = json_int(sensor, "sector_index", -1);
        entry->action = json_string(sensor, "action", NULL);
        entry->axis = json_string(sensor, "axis", NULL);
        entry->side = json_string(sensor, "side", NULL);
        entry->min_value = json_float(sensor, "min", 0.0f);
        entry->max_value = json_float(sensor, "max", 0.0f);
        entry->threshold = json_float(sensor, "threshold", 0.0f);
        entry->range = json_float(sensor, "range", 64.0f);
        entry->min_dot = SDL_clamp(json_float(sensor, "min_dot", -1.0f), -1.0f, 1.0f);
        yyjson_val *fov_degrees = obj_get(sensor, "fov_degrees");
        if (yyjson_is_num(fov_degrees))
            entry->min_dot = SDL_cosf(SDL_clamp((float)yyjson_get_num(fov_degrees), 0.0f, 360.0f) * SDL_PI_F / 360.0f);
        entry->observer_eye_height = json_float(sensor, "observer_eye_height", json_float(sensor, "eye_height", 0.0f));
        entry->target_eye_height = json_float(sensor, "target_eye_height", entry->observer_eye_height);
        entry->yaw_property = json_string(sensor, "yaw_property", "yaw");
        entry->volume_min = json_vec3(sensor, "min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        entry->volume_max = json_vec3(sensor, "max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        entry->actions = obj_get(sensor, "actions");
        entry->edge = json_string(sensor, "edge", "enter");
        const char *signal_name =
            json_string(sensor, "on_enter", json_string(sensor, "on_pressed", json_string(sensor, "on_reflect", NULL)));
        if (SDL_strcmp(entry->edge, "stay") == 0 || SDL_strcmp(entry->edge, "overlap") == 0)
            signal_name = json_string(sensor, "on_stay", signal_name);
        else if (SDL_strcmp(entry->edge, "exit") == 0)
            signal_name = json_string(sensor, "on_exit", signal_name);
        entry->signal_id = slayer3d_game_data_find_signal(runtime, signal_name);
        entry->active_if = obj_get(sensor, "active_if");
    }
    return true;
}

bool load_wave_schedules(slayer3d_game_data_runtime *runtime, yyjson_val *logic)
{
    yyjson_val *schedules = obj_get(logic, "wave_schedules");
    if (!yyjson_is_arr(schedules))
        return true;

    const int count = (int)yyjson_arr_size(schedules);
    runtime->wave_schedules = (wave_schedule_entry *)SDL_calloc((size_t)count, sizeof(*runtime->wave_schedules));
    if (runtime->wave_schedules == NULL && count > 0)
        return false;
    runtime->wave_schedule_count = count;

    for (int i = 0; i < count; ++i)
    {
        runtime->wave_schedules[i].schedule = yyjson_arr_get(schedules, (size_t)i);
        runtime->wave_schedules[i].elapsed = 0.0f;
        runtime->wave_schedules[i].initialized = false;
    }
    return true;
}

void load_active_camera(slayer3d_game_data_runtime *runtime, yyjson_val *root)
{
    yyjson_val *cameras = obj_get(obj_get(root, "world"), "cameras");
    for (size_t i = 0; yyjson_is_arr(cameras) && i < yyjson_arr_size(cameras); ++i)
    {
        yyjson_val *camera = yyjson_arr_get(cameras, i);
        if (json_bool(camera, "active", false))
        {
            runtime->active_camera = json_string(camera, "name", NULL);
            return;
        }
    }
}

static bool load_scene_menus(scene_entry *scene, char *error_buffer, int error_buffer_size)
{
    yyjson_val *menus = obj_get(scene->root, "menus");
    if (!yyjson_is_arr(menus))
        return true;

    scene->menu_count = (int)yyjson_arr_size(menus);
    scene->menus = (scene_menu_state *)SDL_calloc((size_t)scene->menu_count, sizeof(*scene->menus));
    if (scene->menus == NULL && scene->menu_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate scene menus");
        return false;
    }

    for (int i = 0; i < scene->menu_count; ++i)
    {
        yyjson_val *menu = yyjson_arr_get(menus, (size_t)i);
        if (!yyjson_is_obj(menu) || json_string(menu, "name", NULL) == NULL)
        {
            set_error(error_buffer, error_buffer_size, "scene menu requires a non-empty name");
            return false;
        }

        yyjson_val *items = obj_get(menu, "items");
        if (!yyjson_is_arr(items) || yyjson_arr_size(items) <= 0)
        {
            set_error(error_buffer, error_buffer_size, "scene menu requires at least one item");
            return false;
        }

        scene->menus[i].menu = menu;
        scene->menus[i].item_count = (int)yyjson_arr_size(items);
        scene->menus[i].selected_index = SDL_clamp(json_int(menu, "selected", 0), 0, scene->menus[i].item_count - 1);
    }
    return true;
}

static bool load_scene_entities(scene_entry *scene, char *error_buffer, int error_buffer_size)
{
    yyjson_val *entities = obj_get(scene->root, "entities");
    if (entities == NULL)
        return true;
    if (!yyjson_is_arr(entities))
    {
        set_error(error_buffer, error_buffer_size, "scene entities must be an array");
        return false;
    }

    scene->has_entity_filter = true;
    scene->entity_count = (int)yyjson_arr_size(entities);
    if (scene->entity_count <= 0)
        return true;

    scene->entities = (const char **)SDL_calloc((size_t)scene->entity_count, sizeof(*scene->entities));
    if (scene->entities == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate scene entity list");
        return false;
    }

    for (int i = 0; i < scene->entity_count; ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, (size_t)i);
        if (!yyjson_is_str(entity) || yyjson_get_str(entity)[0] == '\0')
        {
            set_error(error_buffer, error_buffer_size, "scene entity entries must be non-empty strings");
            return false;
        }
        scene->entities[i] = yyjson_get_str(entity);
    }
    return true;
}

static const char *scene_file_entry_package(yyjson_val *entry)
{
    yyjson_val *package = obj_get(entry, "package");
    return yyjson_is_str(package) ? yyjson_get_str(package) : NULL;
}

static int scene_source_count(yyjson_val *files, char *error_buffer, int error_buffer_size)
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

        set_error(error_buffer, error_buffer_size, "scene files must be strings or known package objects");
        return -1;
    }
    return count;
}

static bool install_scene_doc(slayer3d_game_data_runtime *runtime, yyjson_doc *doc, int scene_index, char *error_buffer,
                              int error_buffer_size)
{
    yyjson_val *scene_root = yyjson_doc_get_root(doc);
    const char *schema = json_string(scene_root, "schema", NULL);
    const char *name = json_string(scene_root, "name", NULL);
    if (!yyjson_is_obj(scene_root) || schema == NULL || SDL_strcmp(schema, "slayer3d.scene.v0") != 0 || name == NULL ||
        name[0] == '\0')
    {
        yyjson_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "scene file has unsupported schema or missing name");
        return false;
    }
    for (int prior = 0; prior < scene_index; ++prior)
    {
        if (SDL_strcmp(runtime->scenes[prior].name, name) == 0)
        {
            yyjson_doc_free(doc);
            set_error(error_buffer, error_buffer_size, "duplicate scene name");
            return false;
        }
    }

    runtime->scenes[scene_index].doc = doc;
    runtime->scenes[scene_index].root = scene_root;
    runtime->scenes[scene_index].name = name;
    if (!load_scene_entities(&runtime->scenes[scene_index], error_buffer, error_buffer_size) ||
        !load_scene_menus(&runtime->scenes[scene_index], error_buffer, error_buffer_size))
        return false;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "SLAYER3D game data scene loaded: name=%s updates_game=%d renders_world=%d entities=%d menus=%d", name,
                 json_bool(scene_root, "updates_game", true) ? 1 : 0,
                 json_bool(scene_root, "renders_world", true) ? 1 : 0, runtime->scenes[scene_index].entity_count,
                 runtime->scenes[scene_index].menu_count);
    return true;
}

static bool load_scene_file(slayer3d_game_data_runtime *runtime, const char *file_path, int scene_index,
                            char *error_buffer, int error_buffer_size)
{
    if (file_path == NULL || file_path[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "scene files must be non-empty strings");
        return false;
    }

    char *resolved_path = path_join(runtime->base_dir, file_path);
    if (resolved_path == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to resolve scene path");
        return false;
    }

    slayer3d_asset_buffer scene_buffer;
    SDL_zero(scene_buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(runtime->assets, resolved_path, &scene_buffer, asset_error,
                                           (int)sizeof(asset_error)))
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "scene asset %s could not be read: %s", file_path,
                         asset_error);
        }
        SDL_free(resolved_path);
        return false;
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)scene_buffer.data, scene_buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    slayer3d_asset_buffer_free(&scene_buffer);
    SDL_free(resolved_path);
    if (doc == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "scene yyjson error %u at byte %llu: %s", err.code,
                         (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
        }
        return false;
    }

    return install_scene_doc(runtime, doc, scene_index, error_buffer, error_buffer_size);
}

static bool load_scene_package(slayer3d_game_data_runtime *runtime, yyjson_val *root, const char *package,
                               int *scene_index, char *error_buffer, int error_buffer_size)
{
    slayer3d_standard_options_scene_docs docs;
    if (!slayer3d_standard_options_build_scene_docs(root, package, &docs, error_buffer, error_buffer_size))
        return false;

    bool ok = true;
    for (int i = 0; ok && i < docs.count; ++i)
    {
        yyjson_doc *doc = docs.docs[i];
        docs.docs[i] = NULL;
        ok = install_scene_doc(runtime, doc, *scene_index, error_buffer, error_buffer_size);
        if (ok)
            (*scene_index)++;
    }
    slayer3d_standard_options_scene_docs_free(&docs);
    return ok;
}

static void copy_all_properties(slayer3d_properties *target, const slayer3d_properties *source)
{
    const int count = slayer3d_properties_count(source);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (slayer3d_properties_get_key_at(source, i, &key, NULL))
            copy_property_value(target, key, slayer3d_properties_get_value(source, key));
    }
}

bool load_scenes(slayer3d_game_data_runtime *runtime, yyjson_val *root, const slayer3d_game_data_load_options *options,
                 char *error_buffer, int error_buffer_size)
{
    yyjson_val *scenes = obj_get(root, "scenes");
    yyjson_val *files = obj_get(scenes, "files");
    if (!yyjson_is_arr(files))
    {
        runtime->active_scene_index = -1;
        return true;
    }

    runtime->scene_count = scene_source_count(files, error_buffer, error_buffer_size);
    if (runtime->scene_count < 0)
        return false;
    runtime->scenes = (scene_entry *)SDL_calloc((size_t)runtime->scene_count, sizeof(*runtime->scenes));
    if (runtime->scenes == NULL && runtime->scene_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate scene table");
        return false;
    }

    int scene_index = 0;
    for (size_t i = 0; i < yyjson_arr_size(files); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(files, i);
        if (yyjson_is_str(entry))
        {
            if (!load_scene_file(runtime, yyjson_get_str(entry), scene_index, error_buffer, error_buffer_size))
                return false;
            scene_index++;
            continue;
        }

        const char *package = scene_file_entry_package(entry);
        if (package != NULL)
        {
            if (!load_scene_package(runtime, root, package, &scene_index, error_buffer, error_buffer_size))
                return false;
            continue;
        }

        set_error(error_buffer, error_buffer_size, "scene files must be strings or known package objects");
        return false;
    }

    if (scene_index != runtime->scene_count)
    {
        set_error(error_buffer, error_buffer_size, "scene package generated an unexpected scene count");
        return false;
    }

    runtime->active_scene_index = runtime->scene_count > 0 ? 0 : -1;
    const bool using_initial_override =
        options != NULL && options->initial_scene_override != NULL && options->initial_scene_override[0] != '\0';
    const char *initial_player_start_name =
        options != NULL && options->initial_player_start != NULL && options->initial_player_start[0] != '\0'
            ? options->initial_player_start
            : NULL;
    slayer3d_game_data_editor_player_start initial_player_start;
    SDL_zero(initial_player_start);
    const bool using_initial_player_start = initial_player_start_name != NULL;
    if (using_initial_player_start &&
        !slayer3d_game_data_get_editor_player_start(runtime, initial_player_start_name, &initial_player_start))
    {
        set_error(error_buffer, error_buffer_size, "initial player start does not reference a loaded player start");
        return false;
    }
    if (using_initial_override && using_initial_player_start && initial_player_start.scene != NULL &&
        initial_player_start.scene[0] != '\0' &&
        SDL_strcmp(options->initial_scene_override, initial_player_start.scene) != 0)
    {
        set_error(error_buffer, error_buffer_size, "initial scene override conflicts with player start scene");
        return false;
    }
    const char *initial =
        using_initial_override ? options->initial_scene_override
        : using_initial_player_start && initial_player_start.scene != NULL && initial_player_start.scene[0] != '\0'
            ? initial_player_start.scene
            : json_string(scenes, "initial", NULL);
    if (initial != NULL)
    {
        scene_entry *scene = find_scene(runtime, initial);
        if (scene == NULL)
        {
            set_error(error_buffer, error_buffer_size,
                      using_initial_override       ? "initial scene override does not reference a loaded scene"
                      : using_initial_player_start ? "initial player start scene does not reference a loaded scene"
                                                   : "initial scene does not reference a loaded scene");
            return false;
        }
        runtime->active_scene_index = (int)(scene - runtime->scenes);
    }
    if (options != NULL && options->initial_scene_state != NULL)
        copy_all_properties(runtime->scene_state, options->initial_scene_state);
    if (using_initial_player_start && !slayer3d_game_data_apply_editor_player_start(runtime, initial_player_start_name,
                                                                                    error_buffer, error_buffer_size))
    {
        return false;
    }
    apply_scene_camera(runtime, active_scene_entry(runtime));
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D game data initial scene: %s",
                 slayer3d_game_data_active_scene(runtime) != NULL ? slayer3d_game_data_active_scene(runtime)
                                                                  : "<none>");
    emit_scene_enter_signal(runtime, active_scene_entry(runtime), NULL,
                            options != NULL ? options->initial_scene_payload : NULL);
    return true;
}

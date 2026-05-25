/**
 * @file game_data_validation.c
 * @brief Validation for JSON-authored game data.
 */

#include "game_data_validation_internal.h"

#include <float.h>
#include <stdarg.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_standard_options.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/input.h"
#include "slayer3d/lighting.h"
#include "slayer3d/sprite_actor.h"

#define GAME_DATA_MENU_TEXT_MAX_BYTES 255
yyjson_val *validation_obj_get(yyjson_val *object, const char *key)
{
    return yyjson_is_obj(object) ? yyjson_obj_get(object, key) : NULL;
}

const char *validation_json_string(yyjson_val *object, const char *key)
{
    yyjson_val *value = validation_obj_get(object, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}

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

static void set_first_error(validation_context *ctx, const char *json_path, const char *message)
{
    if (ctx->error_buffer == NULL || ctx->error_buffer_size <= 0 || ctx->error_buffer[0] != '\0')
    {
        return;
    }

    char source[PATH_BUFFER_SIZE];
    char path[PATH_BUFFER_SIZE];
    validation_resolve_source_location(ctx, json_path, source, sizeof(source), path, sizeof(path));
    SDL_snprintf(ctx->error_buffer, (size_t)ctx->error_buffer_size, "%s: %s: %s", source, path,
                 message != NULL ? message : "unknown validation error");
}

static bool emit_diagnostic(validation_context *ctx, slayer3d_game_data_diagnostic_severity severity,
                            const char *json_path, const char *format, ...)
{
    char message[384];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (ctx->options != NULL && ctx->options->diagnostic != NULL)
    {
        ctx->options->diagnostic(ctx->options->userdata, severity, json_path != NULL ? json_path : "$", message);
    }

    if (severity == SLAYER3D_GAME_DATA_DIAGNOSTIC_ERROR ||
        (severity == SLAYER3D_GAME_DATA_DIAGNOSTIC_WARNING && ctx->options != NULL &&
         ctx->options->treat_warnings_as_errors))
    {
        ctx->failed = true;
        set_first_error(ctx, json_path, message);
        return false;
    }
    return true;
}

bool validation_error(validation_context *ctx, const char *json_path, const char *format, ...)
{
    char message[384];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    return emit_diagnostic(ctx, SLAYER3D_GAME_DATA_DIAGNOSTIC_ERROR, json_path, "%s", message);
}

bool validation_warning(validation_context *ctx, const char *json_path, const char *format, ...)
{
    char message[384];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    return emit_diagnostic(ctx, SLAYER3D_GAME_DATA_DIAGNOSTIC_WARNING, json_path, "%s", message);
}

void format_path(char *buffer, size_t buffer_size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(buffer, buffer_size, format, args);
    va_end(args);
}

void name_table_destroy(name_table *table)
{
    if (table == NULL)
        return;
    for (int i = 0; i < table->count; ++i)
        SDL_free((void *)table->names[i]);
    for (int i = 0; i < table->count; ++i)
        SDL_free((void *)table->paths[i]);
    SDL_free(table->names);
    SDL_free(table->paths);
    table->names = NULL;
    table->paths = NULL;
    table->count = 0;
}

bool name_table_contains(const name_table *table, const char *name)
{
    if (table == NULL || name == NULL)
        return false;
    for (int i = 0; i < table->count; ++i)
    {
        if (SDL_strcmp(table->names[i], name) == 0)
            return true;
    }
    return false;
}

static const char *name_table_path(const name_table *table, const char *name)
{
    if (table == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < table->count; ++i)
    {
        if (SDL_strcmp(table->names[i], name) == 0)
            return table->paths[i];
    }
    return NULL;
}

static bool name_table_add(name_table *table, const char *name, const char *json_path)
{
    char *name_copy = SDL_strdup(name != NULL ? name : "");
    char *path_copy = SDL_strdup(json_path != NULL ? json_path : "$");
    if (name_copy == NULL || path_copy == NULL)
    {
        SDL_free(name_copy);
        SDL_free(path_copy);
        return false;
    }

    const int next_count = table->count + 1;
    const char **names = (const char **)SDL_realloc(table->names, (size_t)next_count * sizeof(*names));
    if (names == NULL)
    {
        SDL_free(name_copy);
        SDL_free(path_copy);
        return false;
    }
    table->names = names;

    const char **paths = (const char **)SDL_realloc(table->paths, (size_t)next_count * sizeof(*paths));
    if (paths == NULL)
    {
        SDL_free(name_copy);
        SDL_free(path_copy);
        return false;
    }
    table->paths = paths;

    table->names[table->count] = name_copy;
    table->paths[table->count] = path_copy;
    table->count = next_count;
    return true;
}

bool require_unique_name(validation_context *ctx, name_table *table, const char *kind, const char *name,
                         const char *json_path)
{
    if (name == NULL || name[0] == '\0')
    {
        return validation_error(ctx, json_path, "%s requires a non-empty name", kind);
    }
    if (name_table_contains(table, name))
    {
        return validation_error(ctx, json_path, "duplicate %s '%s' previously declared at %s", kind, name,
                                name_table_path(table, name));
    }
    if (!name_table_add(table, name, json_path))
    {
        return validation_error(ctx, json_path, "failed to allocate validation name table for %s '%s'", kind, name);
    }
    return true;
}

bool require_ref(validation_context *ctx, const name_table *table, const char *kind, const char *name,
                 const char *json_path)
{
    if (name == NULL || name[0] == '\0')
    {
        return validation_error(ctx, json_path, "missing %s reference", kind);
    }
    if (!name_table_contains(table, name))
    {
        return validation_error(ctx, json_path, "unknown %s reference '%s'", kind, name);
    }
    return true;
}

bool require_actor_ref(validation_context *ctx, const validation_names *names, const char *name, const char *json_path)
{
    if (name == NULL || name[0] == '\0')
        return validation_error(ctx, json_path, "missing actor reference");
    if (!name_table_contains(&names->entities, name) && !name_table_contains(&names->actor_pool_actors, name))
        return validation_error(ctx, json_path, "unknown actor reference '%s'", name);
    return true;
}

bool note_name(name_table *table, const char *name, const char *json_path)
{
    if (name == NULL || name[0] == '\0' || name_table_contains(table, name))
        return true;
    return name_table_add(table, name, json_path);
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

static bool is_axis_name(const char *axis)
{
    return axis != NULL && (SDL_strcmp(axis, "x") == 0 || SDL_strcmp(axis, "y") == 0 || SDL_strcmp(axis, "z") == 0);
}

static bool is_side_name(const char *side)
{
    return side != NULL && (SDL_strcmp(side, "min") == 0 || SDL_strcmp(side, "max") == 0);
}

bool is_vec_array(yyjson_val *value, size_t min_count)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < min_count)
        return false;
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        if (!yyjson_is_num(yyjson_arr_get(value, i)))
            return false;
    }
    return true;
}

bool is_exact_vec_array(yyjson_val *value, size_t count)
{
    return yyjson_is_arr(value) && yyjson_arr_size(value) == count && is_vec_array(value, count);
}

bool is_exact_vec3_or_vec4_array(yyjson_val *value)
{
    return is_exact_vec_array(value, 3) || is_exact_vec_array(value, 4);
}

bool numeric_array_values_positive(yyjson_val *value)
{
    if (!yyjson_is_arr(value))
        return false;
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (!yyjson_is_num(entry) || yyjson_get_num(entry) <= 0.0)
            return false;
    }
    return true;
}

bool numeric_array_values_in_range(yyjson_val *value, double min_value, double max_value)
{
    if (!yyjson_is_arr(value))
        return false;
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (!yyjson_is_num(entry) || yyjson_get_num(entry) < min_value || yyjson_get_num(entry) > max_value)
            return false;
    }
    return true;
}

static bool is_wave_axis_value(yyjson_val *value)
{
    if (value == NULL || yyjson_is_num(value))
        return true;
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 2)
        return false;
    return yyjson_is_num(yyjson_arr_get(value, 0)) && yyjson_is_num(yyjson_arr_get(value, 1));
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

static bool collect_signals(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *signals = obj_get(root, "signals");
    if (signals == NULL)
        return true;
    if (!yyjson_is_arr(signals))
        return validation_error(ctx, "$.signals", "signals must be an array");

    for (size_t i = 0; i < yyjson_arr_size(signals); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.signals[%zu]", i);
        yyjson_val *signal = yyjson_arr_get(signals, i);
        if (!yyjson_is_str(signal) || yyjson_get_str(signal)[0] == '\0')
            return validation_error(ctx, path, "signal entries must be non-empty strings");
        if (!require_unique_name(ctx, &names->signals, "signal", yyjson_get_str(signal), path))
            return false;
    }
    return true;
}

static bool collect_entities(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *entities = obj_get(root, "entities");
    if (entities == NULL)
        return true;
    if (!yyjson_is_arr(entities))
        return validation_error(ctx, "$.entities", "entities must be an array");

    for (size_t i = 0; i < yyjson_arr_size(entities); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.entities[%zu]", i);
        yyjson_val *entity = yyjson_arr_get(entities, i);
        if (!yyjson_is_obj(entity))
            return validation_error(ctx, path, "entity entries must be objects");
        if (!require_unique_name(ctx, &names->entities, "entity", json_string(entity, "name"), path))
            return false;
    }
    return true;
}

static bool collect_brush_worlds(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *worlds = obj_get(root, "brush_worlds");
    if (worlds == NULL)
        return true;
    if (!yyjson_is_arr(worlds))
        return validation_error(ctx, "$.brush_worlds", "brush_worlds must be an array");

    for (size_t i = 0; i < yyjson_arr_size(worlds); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.brush_worlds[%zu]", i);
        yyjson_val *world = yyjson_arr_get(worlds, i);
        if (!yyjson_is_obj(world))
            return validation_error(ctx, path, "brush world entries must be objects");
        if (!require_unique_name(ctx, &names->brush_worlds, "brush world", json_string(world, "name"), path))
            return false;
    }
    return true;
}

static bool collect_editor_player_starts(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *starts = obj_get(root, "editor_player_starts");
    if (starts == NULL)
        return true;
    if (!yyjson_is_arr(starts))
        return validation_error(ctx, "$.editor_player_starts", "editor_player_starts must be an array");

    for (size_t i = 0; i < yyjson_arr_size(starts); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.editor_player_starts[%zu]", i);
        yyjson_val *start = yyjson_arr_get(starts, i);
        if (!yyjson_is_obj(start))
            return validation_error(ctx, path, "editor player start entries must be objects");
        if (!require_unique_name(ctx, &names->editor_player_starts, "editor player start", json_string(start, "name"),
                                 path))
        {
            return false;
        }
    }
    return true;
}

static bool collect_actor_archetypes(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *archetypes = obj_get(root, "actor_archetypes");
    if (archetypes == NULL)
        return true;
    if (!yyjson_is_arr(archetypes))
        return validation_error(ctx, "$.actor_archetypes", "actor_archetypes must be an array");

    for (size_t i = 0; i < yyjson_arr_size(archetypes); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_archetypes[%zu]", i);
        yyjson_val *archetype = yyjson_arr_get(archetypes, i);
        if (!yyjson_is_obj(archetype))
            return validation_error(ctx, path, "actor archetype entries must be objects");
        if (!require_unique_name(ctx, &names->actor_archetypes, "actor archetype", json_string(archetype, "name"),
                                 path))
            return false;
    }
    return true;
}

static bool collect_actor_pools(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *pools = obj_get(root, "actor_pools");
    if (pools == NULL)
        return true;
    if (!yyjson_is_arr(pools))
        return validation_error(ctx, "$.actor_pools", "actor_pools must be an array");

    for (size_t i = 0; i < yyjson_arr_size(pools); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_pools[%zu]", i);
        yyjson_val *pool = yyjson_arr_get(pools, i);
        if (!yyjson_is_obj(pool))
            return validation_error(ctx, path, "actor pool entries must be objects");
        const char *pool_name = json_string(pool, "name");
        if (!require_unique_name(ctx, &names->actor_pools, "actor pool", pool_name, path))
            return false;
        yyjson_val *capacity_json = obj_get(pool, "capacity");
        if (!yyjson_is_int(capacity_json))
            continue;
        const int capacity = yyjson_get_int(capacity_json);
        if (capacity <= 0 || capacity > 4096)
            continue;
        for (int actor_index = 0; actor_index < capacity; ++actor_index)
        {
            char actor_name[256];
            SDL_snprintf(actor_name, sizeof(actor_name), "%s.%d", pool_name, actor_index);
            if (name_table_contains(&names->entities, actor_name))
            {
                return validation_error(ctx, path, "actor pool generated actor '%s' collides with entity at %s",
                                        actor_name, name_table_path(&names->entities, actor_name));
            }
            if (name_table_contains(&names->actor_pool_actors, actor_name))
            {
                return validation_error(ctx, path,
                                        "actor pool generated actor '%s' collides with generated actor at %s",
                                        actor_name, name_table_path(&names->actor_pool_actors, actor_name));
            }
            if (!name_table_add(&names->actor_pool_actors, actor_name, path))
                return validation_error(ctx, path, "failed to allocate validation name table for actor pool actor");
        }
    }
    return true;
}

static bool collect_cameras(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *cameras = obj_get(obj_get(root, "world"), "cameras");
    if (cameras == NULL)
        return true;
    if (!yyjson_is_arr(cameras))
        return validation_error(ctx, "$.world.cameras", "world cameras must be an array");

    for (size_t i = 0; i < yyjson_arr_size(cameras); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.world.cameras[%zu]", i);
        yyjson_val *camera = yyjson_arr_get(cameras, i);
        if (!yyjson_is_obj(camera))
            return validation_error(ctx, path, "camera entries must be objects");
        if (!require_unique_name(ctx, &names->cameras, "camera", json_string(camera, "name"), path))
            return false;
    }
    return true;
}

bool asset_path_exists(validation_context *ctx, const char *asset_path, const char *json_path, const char *asset_kind)
{
    if (ctx->assets == NULL)
        return true;

    char *resolved = asset_path != NULL && SDL_strncmp(asset_path, "asset://", 8) == 0
                         ? SDL_strdup(asset_path)
                         : validation_path_join(ctx->base_dir, asset_path);
    if (resolved == NULL)
        return validation_error(ctx, json_path, "failed to resolve %s asset path", asset_kind);
    const bool exists = slayer3d_asset_resolver_exists(ctx->assets, resolved);
    SDL_free(resolved);
    if (!exists)
        return validation_error(ctx, json_path, "%s asset path '%s' does not exist", asset_kind, asset_path);
    return true;
}

static bool collect_timers(validation_context *ctx, yyjson_val *root, validation_names *names)
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

static bool collect_adapters(validation_context *ctx, yyjson_val *root, validation_names *names)
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

static bool collect_scripts(validation_context *ctx, yyjson_val *root, validation_names *names)
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

static bool collect_sensors(validation_context *ctx, yyjson_val *root, validation_names *names)
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

static bool collect_network_input_channels(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *replication = obj_get(obj_get(root, "network"), "replication");
    if (replication == NULL)
        return true;
    if (!yyjson_is_arr(replication))
        return validation_error(ctx, "$.network.replication", "network replication must be an array");

    for (size_t i = 0; i < yyjson_arr_size(replication); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        yyjson_val *entry = yyjson_arr_get(replication, i);
        format_path(path, sizeof(path), "$.network.replication[%zu]", i);
        if (!yyjson_is_obj(entry))
            return validation_error(ctx, path, "network replication entries must be objects");
        if (SDL_strcmp(json_string(entry, "direction") != NULL ? json_string(entry, "direction") : "",
                       "client_to_host") != 0)
        {
            continue;
        }
        const char *name = json_string(entry, "name");
        if (name != NULL && name[0] != '\0' &&
            !require_unique_name(ctx, &names->network_input_channels, "network input channel", name, path))
        {
            return false;
        }
    }
    return true;
}

static bool collect_names(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    return collect_signals(ctx, root, names) && collect_entities(ctx, root, names) &&
           collect_grid_maps(ctx, root, names) && collect_grid_pickup_layers(ctx, root, names) &&
           collect_sector_levels(ctx, root, names) && collect_brush_worlds(ctx, root, names) &&
           collect_sector_navigation(ctx, root, names) && collect_sector_doors(ctx, root, names) &&
           collect_sector_platforms(ctx, root, names) && collect_editor_player_starts(ctx, root, names) &&
           collect_actor_archetypes(ctx, root, names) && collect_actor_pools(ctx, root, names) &&
           collect_scripts(ctx, root, names) && collect_adapters(ctx, root, names) &&
           collect_input_actions(ctx, root, names) && collect_input_assignment_sets(ctx, root, names) &&
           collect_input_profiles(ctx, root, names) && collect_network_input_channels(ctx, root, names) &&
           collect_cameras(ctx, root, names) && collect_font_assets(ctx, root, names) &&
           collect_sprite_assets(ctx, root, names) && collect_image_assets(ctx, root, names) &&
           collect_model_assets(ctx, root, names) && collect_audio_assets(ctx, root, names) &&
           collect_timers(ctx, root, names) && collect_sensors(ctx, root, names);
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

static bool validate_logic(validation_context *ctx, yyjson_val *root, validation_names *names)
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

static bool validate_adapters(validation_context *ctx, yyjson_val *root, validation_names *names)
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

    static const char *const faces[] = {"pos_x", "neg_x", "pos_y", "neg_y", "pos_z", "neg_z"};
    for (size_t i = 0; i < SDL_arraysize(faces); ++i)
    {
        char face_path[PATH_BUFFER_SIZE];
        format_path(face_path, sizeof(face_path), "%s.%s", skybox_path, faces[i]);
        if (!require_ref(ctx, &names->images, "image asset", json_string(skybox, faces[i]), face_path))
            return false;
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
    if (!yyjson_is_arr(viewports))
        return validation_error(ctx, json_path, "scene world_viewports must be an array");

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
            return validation_error(ctx, path,
                                    "scene world viewport rect must be [x, y, width, height] with positive dimensions");
        }
        yyjson_val *viewmodel = obj_get(viewport, "viewmodel");
        if (viewmodel != NULL && !yyjson_is_bool(viewmodel))
            return validation_error(ctx, path, "scene world viewport viewmodel must be a boolean");
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.active_if", path);
        if (!validate_scene_ui_condition(ctx, obj_get(viewport, "active_if"), condition_path, names))
            return false;
    }
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

static bool validate_scenes(validation_context *ctx, yyjson_val *root, validation_names *names)
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

static bool warn_unused(validation_context *ctx, const name_table *declared, const name_table *used, const char *kind)
{
    for (int i = 0; i < declared->count; ++i)
    {
        if (!name_table_contains(used, declared->names[i]) &&
            !validation_warning(ctx, declared->paths[i], "unused %s '%s'", kind, declared->names[i]))
        {
            return false;
        }
    }
    return true;
}

static bool validate_details(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    return validate_storage(ctx, root) && validate_persistence(ctx, root, names) && validate_factions(ctx, root) &&
           validate_world_metadata(ctx, root) && validate_input_bindings(ctx, root) &&
           validate_input_assignment_sets(ctx, root) && validate_input_profiles(ctx, root, names) &&
           validate_grid_maps(ctx, root) && validate_grid_pickup_layers(ctx, root, names) &&
           validate_sector_levels(ctx, root) && validate_brush_worlds(ctx, root, names) &&
           validate_sector_navigation(ctx, root, names) && validate_components(ctx, root, names) &&
           validate_update_phases(ctx, obj_get(root, "update_phases"), "$.update_phases", names) &&
           validate_transitions(ctx, root, names) && validate_scenes(ctx, root, names) &&
           validate_editor_brush_sources(ctx, root, names) && validate_editor_player_starts(ctx, root, names) &&
           validate_sector_doors(ctx, root, names) && validate_sector_platforms(ctx, root, names) &&
           validate_actor_archetypes_and_pools(ctx, root, names) && validate_network(ctx, root, names) &&
           validate_app_refs(ctx, root, names) && validate_cameras(ctx, root, names) && validate_ui(ctx, root, names) &&
           validate_presentation(ctx, root, names) && validate_render_settings(ctx, root) &&
           validate_render_effects(ctx, root, names) && validate_lights(ctx, root, names) &&
           validate_haptics(ctx, root, names) && validate_editor_metadata_tree(ctx, root, names) &&
           validate_logic(ctx, root, names) && validate_adapters(ctx, root, names) &&
           warn_unused(ctx, &names->adapters, &names->used_adapters, "adapter") &&
           warn_unused(ctx, &names->scripts, &names->used_scripts, "script");
}

static void validation_names_destroy(validation_names *names)
{
    if (names == NULL)
        return;
    for (int i = 0; i < names->script_count; ++i)
        SDL_free(names->script_manifests[i].dependencies);
    SDL_free(names->script_manifests);
    name_table_destroy(&names->entities);
    name_table_destroy(&names->actor_archetypes);
    name_table_destroy(&names->actor_pools);
    name_table_destroy(&names->actor_pool_actors);
    name_table_destroy(&names->grid_maps);
    name_table_destroy(&names->grid_pickup_layers);
    name_table_destroy(&names->sector_levels);
    name_table_destroy(&names->brush_worlds);
    name_table_destroy(&names->sector_navigation);
    name_table_destroy(&names->sector_doors);
    name_table_destroy(&names->sector_platforms);
    name_table_destroy(&names->editor_player_starts);
    name_table_destroy(&names->signals);
    name_table_destroy(&names->scripts);
    name_table_destroy(&names->script_modules);
    name_table_destroy(&names->adapters);
    name_table_destroy(&names->actions);
    name_table_destroy(&names->input_assignment_sets);
    name_table_destroy(&names->input_profiles);
    name_table_destroy(&names->network_input_channels);
    name_table_destroy(&names->timers);
    name_table_destroy(&names->cameras);
    name_table_destroy(&names->fonts);
    name_table_destroy(&names->images);
    name_table_destroy(&names->models);
    name_table_destroy(&names->sprites);
    name_table_destroy(&names->sounds);
    name_table_destroy(&names->music);
    name_table_destroy(&names->ambient);
    name_table_destroy(&names->scenes);
    name_table_destroy(&names->sensors);
    name_table_destroy(&names->persistence);
    name_table_destroy(&names->used_adapters);
    name_table_destroy(&names->used_scripts);
}

bool slayer3d_game_data_validate_document_with_source_map(yyjson_val *root, const char *source_path,
                                                          const char *base_dir, const slayer3d_asset_resolver *assets,
                                                          const slayer3d_game_data_source_map *source_map,
                                                          const slayer3d_game_data_validation_options *options,
                                                          char *error_buffer, int error_buffer_size)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        error_buffer[0] = '\0';

    validation_context ctx = {
        .options = options,
        .source_path = source_path,
        .base_dir = base_dir,
        .assets = assets,
        .source_map = source_map,
        .error_buffer = error_buffer,
        .error_buffer_size = error_buffer_size,
        .failed = false,
    };

    if (!yyjson_is_obj(root))
        return validation_error(&ctx, "$", "root must be an object");
    if (SDL_strcmp(json_string(root, "schema") != NULL ? json_string(root, "schema") : "", "slayer3d.game.v0") != 0)
        return validation_error(&ctx, "$.schema", "unsupported or missing game data schema");
    if (!validate_imports(&ctx, root))
        return false;

    validation_names names;
    SDL_zero(names);
    const bool ok = collect_names(&ctx, root, &names) && validate_details(&ctx, root, &names) && !ctx.failed;
    validation_names_destroy(&names);
    return ok;
}

bool slayer3d_game_data_validate_document(yyjson_val *root, const char *source_path, const char *base_dir,
                                          const slayer3d_asset_resolver *assets,
                                          const slayer3d_game_data_validation_options *options, char *error_buffer,
                                          int error_buffer_size)
{
    return slayer3d_game_data_validate_document_with_source_map(root, source_path, base_dir, assets, NULL, options,
                                                                error_buffer, error_buffer_size);
}

bool slayer3d_game_data_validate_file(const char *path, const slayer3d_game_data_validation_options *options,
                                      char *error_buffer, int error_buffer_size)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        error_buffer[0] = '\0';
    if (path == NULL || path[0] == '\0')
    {
        validation_context ctx = {
            .options = options,
            .source_path = "<game-data>",
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        return validation_error(&ctx, "$", "invalid game data validation path");
    }

    char *base_dir = validation_path_dirname(path);
    const char *asset_name = path;
    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            asset_name = p + 1;
    }
    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    if (base_dir == NULL || assets == NULL)
    {
        SDL_free(base_dir);
        slayer3d_asset_resolver_destroy(assets);
        validation_context ctx = {
            .options = options,
            .source_path = path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        return validation_error(&ctx, "$", "failed to create validation asset resolver");
    }

    char asset_error[256];
    if (!slayer3d_asset_resolver_mount_directory(assets, base_dir, asset_error, (int)sizeof(asset_error)))
    {
        SDL_free(base_dir);
        slayer3d_asset_resolver_destroy(assets);
        validation_context ctx = {
            .options = options,
            .source_path = path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        return validation_error(&ctx, "$", "failed to mount validation directory: %s", asset_error);
    }

    const bool ok = slayer3d_game_data_validate_asset(assets, asset_name, options, error_buffer, error_buffer_size);
    SDL_free(base_dir);
    slayer3d_asset_resolver_destroy(assets);
    return ok;
}

bool slayer3d_game_data_validate_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                       const slayer3d_game_data_validation_options *options, char *error_buffer,
                                       int error_buffer_size)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        error_buffer[0] = '\0';
    if (assets == NULL || asset_path == NULL || asset_path[0] == '\0')
    {
        validation_context ctx = {
            .options = options,
            .source_path = asset_path != NULL ? asset_path : "<game-data>",
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        return validation_error(&ctx, "$", "invalid game data asset validation arguments");
    }

    slayer3d_game_data_source_map *source_map = NULL;
    yyjson_doc *doc =
        slayer3d_game_data_compose_asset(assets, asset_path, &source_map, error_buffer, error_buffer_size);
    if (doc == NULL)
        return false;

    char *base_dir = validation_path_dirname(validation_asset_path_without_scheme(asset_path));
    const bool ok = slayer3d_game_data_validate_document_with_source_map(
        yyjson_doc_get_root(doc), asset_path, base_dir, assets, source_map, options, error_buffer, error_buffer_size);
    SDL_free(base_dir);
    slayer3d_game_data_source_map_destroy(source_map);
    yyjson_doc_free(doc);
    return ok;
}

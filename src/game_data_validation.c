/**
 * @file game_data_validation.c
 * @brief Validation for JSON-authored game data.
 */

#include "game_data_validation.h"

#include <stdarg.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#define PATH_BUFFER_SIZE 256

typedef struct name_table
{
    const char **names;
    const char **paths;
    int count;
} name_table;

typedef struct script_manifest
{
    const char *id;
    const char *path;
    const char *module;
    const char *json_path;
    const char **dependencies;
    int dependency_count;
    bool visiting;
    bool visited;
} script_manifest;

typedef struct validation_context
{
    const sdl3d_game_data_validation_options *options;
    const char *source_path;
    const char *base_dir;
    const sdl3d_asset_resolver *assets;
    char *error_buffer;
    int error_buffer_size;
    bool failed;
} validation_context;

typedef struct validation_names
{
    name_table entities;
    name_table signals;
    name_table scripts;
    name_table script_modules;
    name_table adapters;
    name_table actions;
    name_table timers;
    name_table cameras;
    name_table fonts;
    name_table images;
    name_table sounds;
    name_table music;
    name_table scenes;
    name_table sensors;
    name_table used_adapters;
    name_table used_scripts;
    script_manifest *script_manifests;
    int script_count;
} validation_names;

static bool validate_data_condition(validation_context *ctx, yyjson_val *condition, const char *path,
                                    validation_names *names);
static bool validate_storage(validation_context *ctx, yyjson_val *root);

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return yyjson_is_obj(object) ? yyjson_obj_get(object, key) : NULL;
}

static const char *json_string(yyjson_val *object, const char *key)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}

static bool is_non_empty_string(yyjson_val *object, const char *key)
{
    const char *value = json_string(object, key);
    return value != NULL && value[0] != '\0';
}

static bool is_storage_path_segment(const char *value)
{
    if (value == NULL || value[0] == '\0')
        return false;
    if (SDL_strcmp(value, ".") == 0 || SDL_strcmp(value, "..") == 0)
        return false;
    return SDL_strchr(value, '/') == NULL && SDL_strchr(value, '\\') == NULL && SDL_strchr(value, ':') == NULL;
}

static void set_first_error(validation_context *ctx, const char *json_path, const char *message)
{
    if (ctx->error_buffer == NULL || ctx->error_buffer_size <= 0 || ctx->error_buffer[0] != '\0')
    {
        return;
    }

    SDL_snprintf(ctx->error_buffer, (size_t)ctx->error_buffer_size, "%s: %s: %s",
                 ctx->source_path != NULL ? ctx->source_path : "<game-data>", json_path != NULL ? json_path : "$",
                 message != NULL ? message : "unknown validation error");
}

static bool emit_diagnostic(validation_context *ctx, sdl3d_game_data_diagnostic_severity severity,
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

    if (severity == SDL3D_GAME_DATA_DIAGNOSTIC_ERROR ||
        (severity == SDL3D_GAME_DATA_DIAGNOSTIC_WARNING && ctx->options != NULL &&
         ctx->options->treat_warnings_as_errors))
    {
        ctx->failed = true;
        set_first_error(ctx, json_path, message);
        return false;
    }
    return true;
}

static bool validation_error(validation_context *ctx, const char *json_path, const char *format, ...)
{
    char message[384];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    return emit_diagnostic(ctx, SDL3D_GAME_DATA_DIAGNOSTIC_ERROR, json_path, "%s", message);
}

static bool validation_warning(validation_context *ctx, const char *json_path, const char *format, ...)
{
    char message[384];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    return emit_diagnostic(ctx, SDL3D_GAME_DATA_DIAGNOSTIC_WARNING, json_path, "%s", message);
}

static bool validate_storage_string(validation_context *ctx, yyjson_val *storage, const char *key,
                                    const char *json_path, bool path_segment)
{
    yyjson_val *value = obj_get(storage, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0')
        return validation_error(ctx, json_path, "storage field must be a non-empty string");
    if (path_segment && !is_storage_path_segment(yyjson_get_str(value)))
        return validation_error(ctx, json_path, "storage field must be a safe path segment");
    return true;
}

static bool validate_storage(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *storage = obj_get(root, "storage");
    if (storage == NULL)
        return true;
    if (!yyjson_is_obj(storage))
        return validation_error(ctx, "$.storage", "storage must be an object");

    return validate_storage_string(ctx, storage, "organization", "$.storage.organization", true) &&
           validate_storage_string(ctx, storage, "application", "$.storage.application", true) &&
           validate_storage_string(ctx, storage, "profile", "$.storage.profile", true) &&
           validate_storage_string(ctx, storage, "user_root_override", "$.storage.user_root_override", false) &&
           validate_storage_string(ctx, storage, "cache_root_override", "$.storage.cache_root_override", false);
}

static void format_path(char *buffer, size_t buffer_size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(buffer, buffer_size, format, args);
    va_end(args);
}

static void name_table_destroy(name_table *table)
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

static bool name_table_contains(const name_table *table, const char *name)
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

static bool require_unique_name(validation_context *ctx, name_table *table, const char *kind, const char *name,
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

static bool require_ref(validation_context *ctx, const name_table *table, const char *kind, const char *name,
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

static bool note_name(name_table *table, const char *name, const char *json_path)
{
    if (name == NULL || name[0] == '\0' || name_table_contains(table, name))
        return true;
    return name_table_add(table, name, json_path);
}

static bool path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return SDL_strlen(path) > 2 && path[1] == ':';
}

static char *path_dirname(const char *path)
{
    if (path == NULL)
        return NULL;

    const char *last = NULL;
    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            last = p;
    }

    if (last == NULL)
        return SDL_strdup(".");

    const size_t length = (size_t)(last - path);
    if (length == 0)
        return SDL_strdup(path[0] == '\\' ? "\\" : "/");

    char *dir = (char *)SDL_malloc(length + 1);
    if (dir == NULL)
        return NULL;
    SDL_memcpy(dir, path, length);
    dir[length] = '\0';
    return dir;
}

static char *path_join(const char *base_dir, const char *path)
{
    if (path == NULL)
        return NULL;
    if (path_is_absolute(path) || base_dir == NULL || base_dir[0] == '\0')
        return SDL_strdup(path);

    const size_t base_len = SDL_strlen(base_dir);
    const size_t path_len = SDL_strlen(path);
    const bool needs_sep = base_len > 0 && base_dir[base_len - 1] != '/' && base_dir[base_len - 1] != '\\';
    char *joined = (char *)SDL_malloc(base_len + (needs_sep ? 1u : 0u) + path_len + 1u);
    if (joined == NULL)
        return NULL;

    SDL_memcpy(joined, base_dir, base_len);
    size_t offset = base_len;
    if (needs_sep)
        joined[offset++] = '/';
    SDL_memcpy(joined + offset, path, path_len);
    joined[offset + path_len] = '\0';
    return joined;
}

static const char *asset_path_without_scheme(const char *path)
{
    return path != NULL && SDL_strncmp(path, "asset://", 8) == 0 ? path + 8 : path;
}

static bool script_path_exists(validation_context *ctx, const char *script_path, const char *json_path)
{
    char *resolved = path_join(ctx->base_dir, script_path);
    if (resolved == NULL)
    {
        return validation_error(ctx, json_path, "failed to resolve script path '%s'", script_path);
    }

    if (ctx->assets != NULL)
    {
        const bool exists = sdl3d_asset_resolver_exists(ctx->assets, resolved);
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

static bool is_compare_op(const char *op)
{
    return op != NULL && (SDL_strcmp(op, ">=") == 0 || SDL_strcmp(op, ">") == 0 || SDL_strcmp(op, "<=") == 0 ||
                          SDL_strcmp(op, "<") == 0 || SDL_strcmp(op, "==") == 0);
}

static bool is_vec_array(yyjson_val *value, size_t min_count)
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

static bool is_supported_component_type(const char *type)
{
    const char *known[] = {
        "adapter.controller", "collision.aabb", "collision.circle", "control.axis_1d", "motion.velocity_2d",
        "particles.emitter",  "property.decay", "render.cube",      "render.sphere",
    };

    if (type == NULL)
        return false;
    for (size_t i = 0; i < SDL_arraysize(known); ++i)
    {
        if (SDL_strcmp(type, known[i]) == 0)
            return true;
    }
    return false;
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

static bool collect_fonts(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *fonts = obj_get(obj_get(root, "assets"), "fonts");
    if (fonts == NULL)
        return true;
    if (!yyjson_is_arr(fonts))
        return validation_error(ctx, "$.assets.fonts", "font assets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(fonts); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.fonts[%zu]", i);
        yyjson_val *font = yyjson_arr_get(fonts, i);
        if (!yyjson_is_obj(font))
            return validation_error(ctx, path, "font asset entries must be objects");
        if (!require_unique_name(ctx, &names->fonts, "font asset", json_string(font, "id"), path))
            return false;
        if (!is_non_empty_string(font, "builtin") && !is_non_empty_string(font, "path"))
            return validation_error(ctx, path, "font asset requires builtin or path");
    }
    return true;
}

static bool collect_images(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *images = obj_get(obj_get(root, "assets"), "images");
    if (images == NULL)
        return true;
    if (!yyjson_is_arr(images))
        return validation_error(ctx, "$.assets.images", "image assets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(images); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.images[%zu]", i);
        yyjson_val *image = yyjson_arr_get(images, i);
        if (!yyjson_is_obj(image))
            return validation_error(ctx, path, "image asset entries must be objects");
        if (!require_unique_name(ctx, &names->images, "image asset", json_string(image, "id"), path))
            return false;
        if (!is_non_empty_string(image, "path"))
            return validation_error(ctx, path, "image asset requires a non-empty path");

        if (ctx->assets != NULL)
        {
            const char *image_path = json_string(image, "path");
            char *resolved = image_path != NULL && SDL_strncmp(image_path, "asset://", 8) == 0
                                 ? SDL_strdup(image_path)
                                 : path_join(ctx->base_dir, image_path);
            if (resolved == NULL)
                return validation_error(ctx, path, "failed to resolve image asset path");
            const bool exists = sdl3d_asset_resolver_exists(ctx->assets, resolved);
            SDL_free(resolved);
            if (!exists)
                return validation_error(ctx, path, "image asset path '%s' does not exist", image_path);
        }
    }
    return true;
}

static bool is_audio_bus_name(const char *bus)
{
    return bus == NULL || SDL_strcmp(bus, "sound_effects") == 0 || SDL_strcmp(bus, "sfx") == 0 ||
           SDL_strcmp(bus, "music") == 0 || SDL_strcmp(bus, "dialogue") == 0 || SDL_strcmp(bus, "dialog") == 0 ||
           SDL_strcmp(bus, "ambience") == 0 || SDL_strcmp(bus, "ambiance") == 0 || SDL_strcmp(bus, "ambient") == 0;
}

static bool asset_path_exists(validation_context *ctx, const char *asset_path, const char *json_path,
                              const char *asset_kind)
{
    if (ctx->assets == NULL)
        return true;

    char *resolved = asset_path != NULL && SDL_strncmp(asset_path, "asset://", 8) == 0
                         ? SDL_strdup(asset_path)
                         : path_join(ctx->base_dir, asset_path);
    if (resolved == NULL)
        return validation_error(ctx, json_path, "failed to resolve %s asset path", asset_kind);
    const bool exists = sdl3d_asset_resolver_exists(ctx->assets, resolved);
    SDL_free(resolved);
    if (!exists)
        return validation_error(ctx, json_path, "%s asset path '%s' does not exist", asset_kind, asset_path);
    return true;
}

static bool collect_audio_assets(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *assets = obj_get(root, "assets");
    yyjson_val *sounds = obj_get(assets, "sounds");
    if (sounds != NULL && !yyjson_is_arr(sounds))
        return validation_error(ctx, "$.assets.sounds", "sound assets must be an array");
    for (size_t i = 0; yyjson_is_arr(sounds) && i < yyjson_arr_size(sounds); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.sounds[%zu]", i);
        yyjson_val *sound = yyjson_arr_get(sounds, i);
        if (!yyjson_is_obj(sound))
            return validation_error(ctx, path, "sound asset entries must be objects");
        if (!require_unique_name(ctx, &names->sounds, "sound asset", json_string(sound, "id"), path))
            return false;
        if (!is_non_empty_string(sound, "path"))
            return validation_error(ctx, path, "sound asset requires a non-empty path");
        if (!is_audio_bus_name(json_string(sound, "bus")))
            return validation_error(ctx, path, "sound asset bus must be sfx, music, dialogue, or ambience");
        if (!asset_path_exists(ctx, json_string(sound, "path"), path, "sound"))
            return false;
    }

    yyjson_val *music = obj_get(assets, "music");
    if (music != NULL && !yyjson_is_arr(music))
        return validation_error(ctx, "$.assets.music", "music assets must be an array");
    for (size_t i = 0; yyjson_is_arr(music) && i < yyjson_arr_size(music); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.music[%zu]", i);
        yyjson_val *track = yyjson_arr_get(music, i);
        if (!yyjson_is_obj(track))
            return validation_error(ctx, path, "music asset entries must be objects");
        if (!require_unique_name(ctx, &names->music, "music asset", json_string(track, "id"), path))
            return false;
        if (!is_non_empty_string(track, "path"))
            return validation_error(ctx, path, "music asset requires a non-empty path");
        if (!asset_path_exists(ctx, json_string(track, "path"), path, "music"))
            return false;
    }
    return true;
}

static bool collect_input_actions(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *contexts = obj_get(obj_get(root, "input"), "contexts");
    if (contexts == NULL)
        return true;
    if (!yyjson_is_arr(contexts))
        return validation_error(ctx, "$.input.contexts", "input contexts must be an array");

    for (size_t c = 0; c < yyjson_arr_size(contexts); ++c)
    {
        yyjson_val *context = yyjson_arr_get(contexts, c);
        yyjson_val *actions = obj_get(context, "actions");
        if (actions == NULL)
            continue;
        if (!yyjson_is_arr(actions))
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.input.contexts[%zu].actions", c);
            return validation_error(ctx, path, "input context actions must be an array");
        }
        for (size_t a = 0; a < yyjson_arr_size(actions); ++a)
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.input.contexts[%zu].actions[%zu]", c, a);
            yyjson_val *action = yyjson_arr_get(actions, a);
            if (!yyjson_is_obj(action))
                return validation_error(ctx, path, "input actions must be objects");
            if (!require_unique_name(ctx, &names->actions, "input action", json_string(action, "name"), path))
                return false;
        }
    }
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

static bool collect_names(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    return collect_signals(ctx, root, names) && collect_entities(ctx, root, names) &&
           collect_scripts(ctx, root, names) && collect_adapters(ctx, root, names) &&
           collect_input_actions(ctx, root, names) && collect_cameras(ctx, root, names) &&
           collect_fonts(ctx, root, names) && collect_images(ctx, root, names) &&
           collect_audio_assets(ctx, root, names) && collect_timers(ctx, root, names) &&
           collect_sensors(ctx, root, names);
}

static bool validate_input_bindings(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *contexts = obj_get(obj_get(root, "input"), "contexts");
    for (size_t c = 0; yyjson_is_arr(contexts) && c < yyjson_arr_size(contexts); ++c)
    {
        yyjson_val *actions = obj_get(yyjson_arr_get(contexts, c), "actions");
        for (size_t a = 0; yyjson_is_arr(actions) && a < yyjson_arr_size(actions); ++a)
        {
            yyjson_val *action = yyjson_arr_get(actions, a);
            yyjson_val *bindings = obj_get(action, "bindings");
            if (bindings == NULL)
                continue;
            if (!yyjson_is_arr(bindings))
            {
                char path[PATH_BUFFER_SIZE];
                format_path(path, sizeof(path), "$.input.contexts[%zu].actions[%zu].bindings", c, a);
                return validation_error(ctx, path, "input action bindings must be an array");
            }

            for (size_t b = 0; b < yyjson_arr_size(bindings); ++b)
            {
                char path[PATH_BUFFER_SIZE];
                format_path(path, sizeof(path), "$.input.contexts[%zu].actions[%zu].bindings[%zu]", c, a, b);
                yyjson_val *binding = yyjson_arr_get(bindings, b);
                const char *device = json_string(binding, "device");
                if (SDL_strcmp(device != NULL ? device : "", "keyboard") == 0)
                {
                    if (!is_non_empty_string(binding, "key"))
                        return validation_error(ctx, path, "keyboard binding requires a non-empty key");
                }
                else if (SDL_strcmp(device != NULL ? device : "", "gamepad") == 0)
                {
                    if (!is_non_empty_string(binding, "axis") && !is_non_empty_string(binding, "button"))
                        return validation_error(ctx, path, "gamepad binding requires an axis or button");
                }
                else
                {
                    return validation_error(ctx, path, "unsupported input binding device '%s'",
                                            device != NULL ? device : "<missing>");
                }
            }
        }
    }
    return true;
}

static bool validate_components(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *entities = obj_get(root, "entities");
    for (size_t e = 0; yyjson_is_arr(entities) && e < yyjson_arr_size(entities); ++e)
    {
        yyjson_val *entity = yyjson_arr_get(entities, e);
        yyjson_val *components = obj_get(entity, "components");
        if (components == NULL)
            continue;
        if (!yyjson_is_arr(components))
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.entities[%zu].components", e);
            return validation_error(ctx, path, "entity components must be an array");
        }

        for (size_t c = 0; c < yyjson_arr_size(components); ++c)
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.entities[%zu].components[%zu]", e, c);
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type");
            if (type == NULL || type[0] == '\0')
                return validation_error(ctx, path, "component requires a non-empty type");
            if (!is_supported_component_type(type) &&
                !validation_warning(ctx, path, "unsupported component type '%s'", type))
            {
                return false;
            }
            if (SDL_strcmp(type, "control.axis_1d") == 0)
            {
                if (!is_axis_name(json_string(component, "axis")))
                    return validation_error(ctx, path, "control.axis_1d requires axis x, y, or z");
                if (!require_ref(ctx, &names->actions, "input action", json_string(component, "negative"), path) ||
                    !require_ref(ctx, &names->actions, "input action", json_string(component, "positive"), path))
                    return false;
            }
            else if (SDL_strcmp(type, "adapter.controller") == 0)
            {
                const char *adapter = json_string(component, "adapter");
                if (!require_ref(ctx, &names->adapters, "adapter", adapter, path))
                    return false;
                if (!note_name(&names->used_adapters, adapter, path))
                    return validation_error(ctx, path, "failed to record adapter use");
                if (json_string(component, "target") != NULL &&
                    !require_ref(ctx, &names->entities, "entity", json_string(component, "target"), path))
                    return false;
            }
            else if (SDL_strcmp(type, "property.decay") == 0)
            {
                if (!is_non_empty_string(component, "property"))
                    return validation_error(ctx, path, "property.decay requires a non-empty property");
                if (json_string(component, "rate_property") == NULL && !yyjson_is_num(obj_get(component, "rate")))
                    return validation_error(ctx, path, "property.decay requires rate or rate_property");
            }
        }
    }
    return true;
}

static bool validate_action_array(validation_context *ctx, yyjson_val *actions, const char *json_path,
                                  validation_names *names);

static bool is_tween_easing(const char *easing)
{
    return easing == NULL || SDL_strcmp(easing, "linear") == 0 || SDL_strcmp(easing, "in_quad") == 0 ||
           SDL_strcmp(easing, "out_quad") == 0 || SDL_strcmp(easing, "in_out_quad") == 0;
}

static bool is_tween_repeat(const char *repeat)
{
    return repeat == NULL || SDL_strcmp(repeat, "none") == 0 || SDL_strcmp(repeat, "loop") == 0 ||
           SDL_strcmp(repeat, "ping_pong") == 0;
}

static bool is_tween_value_type(const char *value_type)
{
    return value_type == NULL || SDL_strcmp(value_type, "int") == 0 || SDL_strcmp(value_type, "float") == 0 ||
           SDL_strcmp(value_type, "number") == 0 || SDL_strcmp(value_type, "vec3") == 0 ||
           SDL_strcmp(value_type, "color") == 0;
}

static bool is_ui_tween_property(const char *property)
{
    return property != NULL && (SDL_strcmp(property, "alpha") == 0 || SDL_strcmp(property, "scale") == 0 ||
                                SDL_strcmp(property, "offset_x") == 0 || SDL_strcmp(property, "offset_y") == 0 ||
                                SDL_strcmp(property, "x") == 0 || SDL_strcmp(property, "y") == 0 ||
                                SDL_strcmp(property, "tint") == 0 || SDL_strcmp(property, "color") == 0);
}

static bool validate_tween_value(validation_context *ctx, yyjson_val *value, const char *json_path,
                                 const char *field_name)
{
    if (yyjson_is_num(value) || is_vec_array(value, 2))
        return true;
    return validation_error(ctx, json_path, "%s must be a number or numeric array", field_name);
}

static bool validate_animation_common(validation_context *ctx, yyjson_val *action, const char *json_path,
                                      validation_names *names)
{
    yyjson_val *to = obj_get(action, "to");
    if (to == NULL)
        to = obj_get(action, "value");
    if (to == NULL)
        return validation_error(ctx, json_path, "animation action requires to or value");
    if (!validate_tween_value(ctx, to, json_path, "animation target value"))
        return false;
    yyjson_val *from = obj_get(action, "from");
    if (from != NULL && !validate_tween_value(ctx, from, json_path, "animation start value"))
        return false;
    yyjson_val *duration = obj_get(action, "duration");
    if (duration != NULL && (!yyjson_is_num(duration) || yyjson_get_num(duration) < 0.0))
        return validation_error(ctx, json_path, "animation duration must be a non-negative number");
    if (!is_tween_easing(json_string(action, "easing")))
        return validation_error(ctx, json_path, "animation easing must be linear, in_quad, out_quad, or in_out_quad");
    if (!is_tween_repeat(json_string(action, "repeat")))
        return validation_error(ctx, json_path, "animation repeat must be none, loop, or ping_pong");
    if (!is_tween_value_type(json_string(action, "value_type")))
        return validation_error(ctx, json_path, "animation value_type must be int, float, number, vec3, or color");
    const char *done_signal = json_string(action, "done_signal");
    if (done_signal != NULL && !require_ref(ctx, &names->signals, "signal", done_signal, json_path))
        return false;
    return true;
}

static bool validate_audio_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                  validation_names *names, const char *type)
{
    if (SDL_strcmp(type, "audio.play_sfx") == 0)
    {
        const char *sound = json_string(action, "sound");
        const char *asset = json_string(action, "asset");
        const char *path = json_string(action, "path");
        if (sound != NULL && !require_ref(ctx, &names->sounds, "sound asset", sound, json_path))
            return false;
        if (asset != NULL && !require_ref(ctx, &names->sounds, "sound asset", asset, json_path))
            return false;
        if (sound == NULL && asset == NULL && (path == NULL || path[0] == '\0'))
            return validation_error(ctx, json_path, "audio.play_sfx requires sound, asset, or path");
        if (path != NULL && !asset_path_exists(ctx, path, json_path, "sound"))
            return false;
    }
    else if (SDL_strcmp(type, "audio.play_music") == 0)
    {
        const char *music = json_string(action, "music");
        const char *asset = json_string(action, "asset");
        const char *path = json_string(action, "path");
        if (music != NULL && !require_ref(ctx, &names->music, "music asset", music, json_path))
            return false;
        if (asset != NULL && !require_ref(ctx, &names->music, "music asset", asset, json_path))
            return false;
        if (music == NULL && asset == NULL && (path == NULL || path[0] == '\0'))
            return validation_error(ctx, json_path, "audio.play_music requires music, asset, or path");
        if (path != NULL && !asset_path_exists(ctx, path, json_path, "music"))
            return false;
    }
    else if (SDL_strcmp(type, "audio.stop_sfx") != 0 && SDL_strcmp(type, "audio.stop_music") != 0 &&
             SDL_strcmp(type, "audio.fade_music") != 0 && SDL_strcmp(type, "audio.set_bus_volume") != 0)
    {
        return validation_error(ctx, json_path, "unknown audio action type '%s'", type);
    }

    if (SDL_strcmp(type, "audio.set_bus_volume") == 0 && json_string(action, "bus") == NULL)
        return validation_error(ctx, json_path, "audio.set_bus_volume requires a bus");
    if (!is_audio_bus_name(json_string(action, "bus")))
        return validation_error(ctx, json_path, "audio bus must be sfx, music, dialogue, or ambience");
    yyjson_val *volume = obj_get(action, "volume");
    if (volume != NULL && !yyjson_is_num(volume))
        return validation_error(ctx, json_path, "audio volume must be numeric");
    yyjson_val *source = obj_get(action, "source");
    if (source != NULL)
    {
        if (!yyjson_is_obj(source))
            return validation_error(ctx, json_path, "audio source must be an object");
        if (!require_ref(ctx, &names->entities, "entity", json_string(source, "target"), json_path))
            return false;
        if (!is_non_empty_string(source, "key"))
            return validation_error(ctx, json_path, "audio source requires a non-empty key");
    }
    yyjson_val *fade = obj_get(action, "fade");
    if (fade != NULL && !yyjson_is_num(fade))
        return validation_error(ctx, json_path, "audio fade must be numeric");
    yyjson_val *duration = obj_get(action, "duration");
    if (duration != NULL && !yyjson_is_num(duration))
        return validation_error(ctx, json_path, "audio duration must be numeric");
    return true;
}

static bool validate_one_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                validation_names *names)
{
    if (!yyjson_is_obj(action))
        return validation_error(ctx, json_path, "logic action must be an object");
    const char *type = json_string(action, "type");
    if (type == NULL || type[0] == '\0')
        return validation_error(ctx, json_path, "logic action requires a non-empty type");

    if (SDL_strcmp(type, "signal.emit") == 0)
        return require_ref(ctx, &names->signals, "signal", json_string(action, "signal"), json_path);
    if (SDL_strcmp(type, "timer.start") == 0)
        return require_ref(ctx, &names->timers, "timer", json_string(action, "timer"), json_path);
    if (SDL_strcmp(type, "property.set") == 0 || SDL_strcmp(type, "property.add") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        if (!is_non_empty_string(action, "key"))
            return validation_error(ctx, json_path, "%s requires a non-empty key", type);
        if (obj_get(action, "value") == NULL)
            return validation_error(ctx, json_path, "%s requires a value", type);
        return true;
    }
    if (SDL_strcmp(type, "property.snapshot") == 0 || SDL_strcmp(type, "property.restore_snapshot") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "%s requires a non-empty name", type);
        yyjson_val *keys = obj_get(action, "keys");
        if (keys != NULL && !yyjson_is_arr(keys))
            return validation_error(ctx, json_path, "%s keys must be an array", type);
        for (size_t i = 0; yyjson_is_arr(keys) && i < yyjson_arr_size(keys); ++i)
        {
            if (!yyjson_is_str(yyjson_arr_get(keys, i)))
                return validation_error(ctx, json_path, "%s keys must be strings", type);
        }
        return true;
    }
    if (SDL_strcmp(type, "property.animate") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        if (!is_non_empty_string(action, "key"))
            return validation_error(ctx, json_path, "property.animate requires a non-empty key");
        return validate_animation_common(ctx, action, json_path, names);
    }
    if (SDL_strcmp(type, "property.reset_defaults") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        yyjson_val *keys = obj_get(action, "keys");
        if (keys != NULL && !yyjson_is_arr(keys))
            return validation_error(ctx, json_path, "property.reset_defaults keys must be an array");
        for (size_t i = 0; yyjson_is_arr(keys) && i < yyjson_arr_size(keys); ++i)
        {
            if (!yyjson_is_str(yyjson_arr_get(keys, i)))
                return validation_error(ctx, json_path, "property.reset_defaults keys must be strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "input.reset_bindings") == 0)
    {
        if (!is_non_empty_string(action, "menu"))
            return validation_error(ctx, json_path, "input.reset_bindings requires a non-empty menu");
        return true;
    }
    if (SDL_strcmp(type, "ui.animate") == 0)
    {
        if (!is_non_empty_string(action, "target") && !is_non_empty_string(action, "ui"))
            return validation_error(ctx, json_path, "ui.animate requires a non-empty target");
        if (!is_non_empty_string(action, "property"))
            return validation_error(ctx, json_path, "ui.animate requires a non-empty property");
        if (!is_ui_tween_property(json_string(action, "property")))
            return validation_error(
                ctx, json_path, "ui.animate property must be alpha, scale, offset_x, offset_y, x, y, tint, or color");
        return validate_animation_common(ctx, action, json_path, names);
    }
    if (SDL_strncmp(type, "audio.", 6) == 0)
        return validate_audio_action(ctx, action, json_path, names, type);
    if (SDL_strcmp(type, "entity.set_active") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        if (!yyjson_is_bool(obj_get(action, "active")))
            return validation_error(ctx, json_path, "entity.set_active requires a boolean active value");
        return true;
    }
    if (SDL_strcmp(type, "transform.set_position") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        if (!is_vec_array(obj_get(action, "position"), 2))
            return validation_error(ctx, json_path, "transform.set_position requires a numeric position array");
        return true;
    }
    if (SDL_strcmp(type, "camera.toggle") == 0)
    {
        return require_ref(ctx, &names->cameras, "camera", json_string(action, "camera"), json_path) &&
               require_ref(ctx, &names->cameras, "camera", json_string(action, "fallback"), json_path);
    }
    if (SDL_strcmp(type, "camera.set") == 0)
        return require_ref(ctx, &names->cameras, "camera", json_string(action, "camera"), json_path);
    if (SDL_strcmp(type, "scene.set") == 0)
        return require_ref(ctx, &names->scenes, "scene", json_string(action, "scene"), json_path);
    if (SDL_strcmp(type, "adapter.invoke") == 0)
    {
        const char *adapter = json_string(action, "adapter");
        if (!require_ref(ctx, &names->adapters, "adapter", adapter, json_path))
            return false;
        if (!note_name(&names->used_adapters, adapter, json_path))
            return validation_error(ctx, json_path, "failed to record adapter use");
        if (json_string(action, "target") != NULL &&
            !require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        return true;
    }
    if (SDL_strcmp(type, "branch") == 0)
    {
        yyjson_val *condition = obj_get(action, "if");
        if (!yyjson_is_obj(condition))
            return validation_error(ctx, json_path, "branch requires an object 'if' condition");
        if (SDL_strcmp(json_string(condition, "type") != NULL ? json_string(condition, "type") : "",
                       "property.compare") != 0)
            return validation_error(ctx, json_path, "branch currently supports only property.compare conditions");
        if (!require_ref(ctx, &names->entities, "entity", json_string(condition, "target"), json_path))
            return false;
        if (!is_non_empty_string(condition, "key"))
            return validation_error(ctx, json_path, "property.compare requires a non-empty key");
        if (!is_compare_op(json_string(condition, "op")))
            return validation_error(ctx, json_path, "property.compare requires a supported comparison operator");
        if (obj_get(condition, "value") == NULL)
            return validation_error(ctx, json_path, "property.compare requires a value");
        char then_path[PATH_BUFFER_SIZE];
        char else_path[PATH_BUFFER_SIZE];
        format_path(then_path, sizeof(then_path), "%s.then", json_path);
        format_path(else_path, sizeof(else_path), "%s.else", json_path);
        return validate_action_array(ctx, obj_get(action, "then"), then_path, names) &&
               validate_action_array(ctx, obj_get(action, "else"), else_path, names);
    }

    return validation_error(ctx, json_path, "unsupported logic action type '%s'", type);
}

static bool validate_action_array(validation_context *ctx, yyjson_val *actions, const char *json_path,
                                  validation_names *names)
{
    if (!yyjson_is_arr(actions))
        return validation_error(ctx, json_path, "logic action list must be an array");
    for (size_t i = 0; i < yyjson_arr_size(actions); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        if (!validate_one_action(ctx, yyjson_arr_get(actions, i), path, names))
            return false;
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
            if (!require_ref(ctx, &names->entities, "entity", json_string(sensor, "entity"), path) ||
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
            if (!require_ref(ctx, &names->entities, "entity", json_string(sensor, "entity"), path) ||
                !require_ref(ctx, &names->signals, "signal", json_string(sensor, "on_reflect"), path) ||
                (!is_axis_name(json_string(sensor, "axis")) &&
                 !validation_error(ctx, path, "sensor.bounds_reflect requires axis x, y, or z")))
                return false;
            continue;
        }
        if (SDL_strcmp(type, "sensor.contact_2d") == 0)
        {
            if (!require_ref(ctx, &names->entities, "entity", json_string(sensor, "a"), path) ||
                !require_ref(ctx, &names->entities, "entity", json_string(sensor, "b"), path) ||
                !require_ref(ctx, &names->signals, "signal", json_string(sensor, "on_enter"), path))
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
        return validation_error(ctx, path, "unsupported sensor type '%s'", type);
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

static bool validate_app_refs(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *app = obj_get(root, "app");
    if (!yyjson_is_obj(app))
        return true;
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

static bool validate_update_phases(validation_context *ctx, yyjson_val *phases, const char *json_path)
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
    }
    return true;
}

static bool validate_presentation(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *presentation = obj_get(root, "presentation");
    if (presentation == NULL)
        return true;
    if (!yyjson_is_obj(presentation))
        return validation_error(ctx, "$.presentation", "presentation must be an object");

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

static bool validate_cameras(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *cameras = obj_get(obj_get(root, "world"), "cameras");
    for (size_t i = 0; yyjson_is_arr(cameras) && i < yyjson_arr_size(cameras); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.world.cameras[%zu]", i);
        yyjson_val *camera = yyjson_arr_get(cameras, i);
        const char *type = json_string(camera, "type");
        if (SDL_strcmp(type != NULL ? type : "", "adapter") == 0)
        {
            const char *adapter = json_string(camera, "adapter");
            if (!require_ref(ctx, &names->adapters, "adapter", adapter, path))
                return false;
            if (!note_name(&names->used_adapters, adapter, path))
                return validation_error(ctx, path, "failed to record adapter use");
            if (json_string(camera, "target_entity") != NULL &&
                !require_ref(ctx, &names->entities, "entity", json_string(camera, "target_entity"), path))
                return false;
        }
        else if (SDL_strcmp(type != NULL ? type : "", "chase") == 0)
        {
            if (!require_ref(ctx, &names->entities, "entity", json_string(camera, "target_entity"), path))
                return false;
        }
    }
    return true;
}

static bool validate_data_condition(validation_context *ctx, yyjson_val *condition, const char *path,
                                    validation_names *names)
{
    if (condition == NULL)
        return true;
    if (!yyjson_is_obj(condition))
        return validation_error(ctx, path, "UI condition must be an object");

    const char *type = json_string(condition, "type");
    if (SDL_strcmp(type != NULL ? type : "", "always") == 0 || SDL_strcmp(type != NULL ? type : "", "app.paused") == 0)
        return true;
    if (SDL_strcmp(type != NULL ? type : "", "camera.active") == 0)
        return require_ref(ctx, &names->cameras, "camera", json_string(condition, "camera"), path);
    if (SDL_strcmp(type != NULL ? type : "", "property.compare") == 0 ||
        SDL_strcmp(type != NULL ? type : "", "property.bool") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(condition, "target"), path))
            return false;
        if (!is_non_empty_string(condition, "key"))
            return validation_error(ctx, path, "property condition requires a non-empty key");
        if (SDL_strcmp(type, "property.compare") == 0 && !is_compare_op(json_string(condition, "op")))
            return validation_error(ctx, path, "property.compare condition requires a supported comparison operator");
        if (SDL_strcmp(type, "property.compare") == 0 && obj_get(condition, "value") == NULL)
            return validation_error(ctx, path, "property.compare condition requires a value");
        return true;
    }
    if (SDL_strcmp(type != NULL ? type : "", "not") == 0)
    {
        char child_path[PATH_BUFFER_SIZE];
        format_path(child_path, sizeof(child_path), "%s.condition", path);
        return validate_data_condition(ctx, obj_get(condition, "condition"), child_path, names);
    }
    if (SDL_strcmp(type != NULL ? type : "", "all") == 0 || SDL_strcmp(type != NULL ? type : "", "any") == 0)
    {
        yyjson_val *conditions = obj_get(condition, "conditions");
        if (!yyjson_is_arr(conditions))
            return validation_error(ctx, path, "%s condition requires a conditions array", type);
        for (size_t i = 0; i < yyjson_arr_size(conditions); ++i)
        {
            char child_path[PATH_BUFFER_SIZE];
            format_path(child_path, sizeof(child_path), "%s.conditions[%zu]", path, i);
            if (!validate_data_condition(ctx, yyjson_arr_get(conditions, i), child_path, names))
                return false;
        }
        return true;
    }
    return validation_error(ctx, path, "unsupported condition type '%s'", type != NULL ? type : "<missing>");
}

static bool validate_ui(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *ui = obj_get(root, "ui");
    yyjson_val *texts = obj_get(ui, "text");
    yyjson_val *images = obj_get(ui, "images");
    yyjson_val *menus = obj_get(ui, "menus");
    if (texts == NULL && images == NULL && menus == NULL)
        return true;
    if (texts != NULL && !yyjson_is_arr(texts))
        return validation_error(ctx, "$.ui.text", "UI text must be an array");
    if (images != NULL && !yyjson_is_arr(images))
        return validation_error(ctx, "$.ui.images", "UI images must be an array");
    if (menus != NULL && !yyjson_is_arr(menus))
        return validation_error(ctx, "$.ui.menus", "UI menus must be an array");

    for (size_t i = 0; i < yyjson_arr_size(texts); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.ui.text[%zu]", i);
        yyjson_val *text = yyjson_arr_get(texts, i);
        if (!yyjson_is_obj(text))
            return validation_error(ctx, path, "UI text entries must be objects");
        if (json_string(text, "font") != NULL &&
            !require_ref(ctx, &names->fonts, "font asset", json_string(text, "font"), path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", path);
        if (!validate_data_condition(ctx, obj_get(text, "visible_if"), condition_path, names))
            return false;

        yyjson_val *bindings = obj_get(text, "bindings");
        for (size_t b = 0; yyjson_is_arr(bindings) && b < yyjson_arr_size(bindings); ++b)
        {
            char binding_path[PATH_BUFFER_SIZE];
            format_path(binding_path, sizeof(binding_path), "%s.bindings[%zu]", path, b);
            yyjson_val *binding = yyjson_arr_get(bindings, b);
            const char *type = json_string(binding, "type");
            if (SDL_strcmp(type != NULL ? type : "", "property") == 0)
            {
                if (!require_ref(ctx, &names->entities, "entity", json_string(binding, "entity"), binding_path))
                    return false;
                if (!is_non_empty_string(binding, "key"))
                    return validation_error(ctx, binding_path, "UI property binding requires a non-empty key");
            }
            else if (SDL_strcmp(type != NULL ? type : "", "metric") != 0)
            {
                if (SDL_strcmp(type != NULL ? type : "", "scene_state") != 0)
                    return validation_error(ctx, binding_path, "unsupported UI binding type '%s'",
                                            type != NULL ? type : "<missing>");
                if (!is_non_empty_string(binding, "key"))
                    return validation_error(ctx, binding_path, "UI scene_state binding requires a non-empty key");
            }
        }
    }

    for (size_t i = 0; yyjson_is_arr(images) && i < yyjson_arr_size(images); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.ui.images[%zu]", i);
        yyjson_val *image = yyjson_arr_get(images, i);
        if (!yyjson_is_obj(image))
            return validation_error(ctx, path, "UI image entries must be objects");
        if (!is_non_empty_string(image, "name"))
            return validation_error(ctx, path, "UI image requires a non-empty name");
        if (!require_ref(ctx, &names->images, "image asset", json_string(image, "image"), path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", path);
        if (!validate_data_condition(ctx, obj_get(image, "visible_if"), condition_path, names))
            return false;
    }

    for (size_t i = 0; yyjson_is_arr(menus) && i < yyjson_arr_size(menus); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.ui.menus[%zu]", i);
        yyjson_val *menu = yyjson_arr_get(menus, i);
        if (!yyjson_is_obj(menu))
            return validation_error(ctx, path, "UI menu presenters must be objects");
        if (!is_non_empty_string(menu, "name"))
            return validation_error(ctx, path, "UI menu presenter requires a non-empty name");
        if (!is_non_empty_string(menu, "menu"))
            return validation_error(ctx, path, "UI menu presenter requires a menu reference");
        if (!require_ref(ctx, &names->fonts, "font asset", json_string(menu, "font"), path))
            return false;
        yyjson_val *cursor = obj_get(menu, "cursor");
        if (yyjson_is_obj(cursor) && json_string(cursor, "font") != NULL &&
            !require_ref(ctx, &names->fonts, "font asset", json_string(cursor, "font"), path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", path);
        if (!validate_data_condition(ctx, obj_get(menu, "visible_if"), condition_path, names))
            return false;
    }
    return true;
}

static bool validate_render_effects(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *entities = obj_get(root, "entities");
    for (size_t e = 0; yyjson_is_arr(entities) && e < yyjson_arr_size(entities); ++e)
    {
        yyjson_val *components = obj_get(yyjson_arr_get(entities, e), "components");
        for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *effects = obj_get(yyjson_arr_get(components, c), "effects");
            for (size_t i = 0; yyjson_is_arr(effects) && i < yyjson_arr_size(effects); ++i)
            {
                char path[PATH_BUFFER_SIZE];
                format_path(path, sizeof(path), "$.entities[%zu].components[%zu].effects[%zu]", e, c, i);
                yyjson_val *effect = yyjson_arr_get(effects, i);
                const char *type = json_string(effect, "type");
                if (SDL_strcmp(type != NULL ? type : "", "flash") == 0)
                {
                    if (!require_ref(ctx, &names->entities, "entity", json_string(effect, "source"), path))
                        return false;
                    if (!is_non_empty_string(effect, "property"))
                        return validation_error(ctx, path, "flash effect requires a non-empty property");
                }
                else if (SDL_strcmp(type != NULL ? type : "", "pulse") != 0 &&
                         SDL_strcmp(type != NULL ? type : "", "emissive") != 0)
                {
                    return validation_error(ctx, path, "unsupported render effect type '%s'",
                                            type != NULL ? type : "<missing>");
                }
            }
        }
    }
    return true;
}

static bool validate_lights(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *lights = obj_get(obj_get(root, "world"), "lights");
    if (lights == NULL)
        return true;
    if (!yyjson_is_arr(lights))
        return validation_error(ctx, "$.world.lights", "world lights must be an array");

    for (size_t i = 0; i < yyjson_arr_size(lights); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.world.lights[%zu]", i);
        yyjson_val *light = yyjson_arr_get(lights, i);
        const char *target_entity = json_string(light, "target_entity");
        if (target_entity != NULL && !require_ref(ctx, &names->entities, "entity", target_entity, path))
            return false;

        yyjson_val *effects = obj_get(light, "effects");
        for (size_t e = 0; yyjson_is_arr(effects) && e < yyjson_arr_size(effects); ++e)
        {
            char effect_path[PATH_BUFFER_SIZE];
            format_path(effect_path, sizeof(effect_path), "%s.effects[%zu]", path, e);
            yyjson_val *effect = yyjson_arr_get(effects, e);
            const char *type = json_string(effect, "type");
            if (SDL_strcmp(type != NULL ? type : "", "flash") == 0)
            {
                if (!require_ref(ctx, &names->entities, "entity", json_string(effect, "source"), effect_path))
                    return false;
                if (!is_non_empty_string(effect, "property"))
                    return validation_error(ctx, effect_path, "light flash effect requires a non-empty property");
            }
            else if (SDL_strcmp(type != NULL ? type : "", "pulse") != 0)
            {
                return validation_error(ctx, effect_path, "unsupported light effect type '%s'",
                                        type != NULL ? type : "<missing>");
            }
        }
    }
    return true;
}

static bool validate_transitions(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *transitions = obj_get(root, "transitions");
    if (transitions == NULL)
        return true;
    if (!yyjson_is_obj(transitions))
        return validation_error(ctx, "$.transitions", "transitions must be an object");

    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(transitions, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        yyjson_val *transition = yyjson_obj_iter_get_val(key);
        const char *name = yyjson_get_str(key);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.transitions.%s", name != NULL ? name : "<unknown>");
        if (!yyjson_is_obj(transition))
            return validation_error(ctx, path, "transition entries must be objects");
        const char *done_signal = json_string(transition, "done_signal");
        if (done_signal != NULL && !require_ref(ctx, &names->signals, "signal", done_signal, path))
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

    char *resolved = path_join(ctx->base_dir, scene_path);
    if (resolved == NULL)
        return validation_error(ctx, path, "failed to resolve scene file '%s'", scene_path);

    sdl3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    const bool read_ok = ctx->assets != NULL && sdl3d_asset_resolver_read_file(ctx->assets, resolved, &buffer,
                                                                               asset_error, (int)sizeof(asset_error));
    SDL_free(resolved);
    if (!read_ok)
        return validation_error(ctx, path, "scene asset '%s' does not exist or cannot be read", scene_path);

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    sdl3d_asset_buffer_free(&buffer);
    if (doc == NULL)
    {
        return validation_error(ctx, path, "scene yyjson error %u at byte %llu: %s", err.code,
                                (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
    }

    yyjson_val *scene_root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(scene_root) ||
        SDL_strcmp(json_string(scene_root, "schema") != NULL ? json_string(scene_root, "schema") : "",
                   "sdl3d.scene.v0") != 0)
    {
        yyjson_doc_free(doc);
        return validation_error(ctx, path, "scene file must use schema sdl3d.scene.v0");
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
    if (type == NULL || (SDL_strcmp(type, "toggle") != 0 && SDL_strcmp(type, "choice") != 0 &&
                         SDL_strcmp(type, "range") != 0 && SDL_strcmp(type, "key_binding") != 0))
        return validation_error(ctx, path, "menu item control requires type toggle, choice, range, or key_binding");

    if (SDL_strcmp(type, "key_binding") != 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(control, "target"), path))
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
    if (SDL_strcmp(type, "key_binding") == 0)
    {
        yyjson_val *bindings = obj_get(control, "bindings");
        if (!yyjson_is_arr(bindings) || yyjson_arr_size(bindings) == 0)
            return validation_error(ctx, path, "key_binding control requires at least one binding");
        if (!is_non_empty_string(control, "default"))
            return validation_error(ctx, path, "key_binding control requires a default input");
        for (size_t i = 0; i < yyjson_arr_size(bindings); ++i)
        {
            yyjson_val *binding = yyjson_arr_get(bindings, i);
            if (!require_ref(ctx, &names->actions, "input action", json_string(binding, "action"), path))
                return false;
            const char *device = json_string(binding, "device");
            if (device != NULL && SDL_strcmp(device, "keyboard") != 0 && SDL_strcmp(device, "gamepad") != 0)
                return validation_error(ctx, path, "key_binding controls support keyboard or gamepad bindings");
        }
    }
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

    char phases_path[PATH_BUFFER_SIZE];
    format_path(phases_path, sizeof(phases_path), "%s.update_phases", json_path);
    if (!validate_update_phases(ctx, obj_get(root, "update_phases"), phases_path))
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
    yyjson_val *ui_menus = obj_get(obj_get(root, "ui"), "menus");
    if (images != NULL && !yyjson_is_arr(images))
        return validation_error(ctx, json_path, "scene UI images must be an array");
    if (ui_menus != NULL && !yyjson_is_arr(ui_menus))
        return validation_error(ctx, json_path, "scene UI menus must be an array");
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
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", image_path);
        if (!validate_scene_ui_condition(ctx, obj_get(image, "visible_if"), condition_path, names))
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
            else if (SDL_strcmp(type != NULL ? type : "", "metric") != 0)
            {
                if (SDL_strcmp(type != NULL ? type : "", "scene_state") != 0)
                    return validation_error(ctx, binding_path, "unsupported scene UI binding type '%s'",
                                            type != NULL ? type : "<missing>");
                if (!is_non_empty_string(binding, "key"))
                    return validation_error(ctx, binding_path, "scene UI scene_state binding requires a non-empty key");
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

    const int count = (int)yyjson_arr_size(files);
    validation_scene_doc *docs = (validation_scene_doc *)SDL_calloc((size_t)count, sizeof(*docs));
    if (docs == NULL && count > 0)
        return validation_error(ctx, "$.scenes.files", "failed to allocate scene validation docs");

    for (int i = 0; i < count; ++i)
    {
        yyjson_val *file = yyjson_arr_get(files, (size_t)i);
        if (!validate_scene_file(ctx, names, yyjson_is_str(file) ? yyjson_get_str(file) : NULL, i, &docs[i]))
        {
            validation_scene_docs_destroy(docs, count);
            return false;
        }
    }

    const char *initial = json_string(scenes, "initial");
    bool ok = initial == NULL || require_ref(ctx, &names->scenes, "scene", initial, "$.scenes.initial");
    for (int i = 0; ok && i < count; ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.scenes.files[%d]", i);
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
    return validate_storage(ctx, root) && validate_input_bindings(ctx, root) && validate_components(ctx, root, names) &&
           validate_update_phases(ctx, obj_get(root, "update_phases"), "$.update_phases") &&
           validate_transitions(ctx, root, names) && validate_scenes(ctx, root, names) &&
           validate_app_refs(ctx, root, names) && validate_cameras(ctx, root, names) && validate_ui(ctx, root, names) &&
           validate_presentation(ctx, root, names) && validate_render_effects(ctx, root, names) &&
           validate_lights(ctx, root, names) && validate_logic(ctx, root, names) &&
           validate_adapters(ctx, root, names) &&
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
    name_table_destroy(&names->signals);
    name_table_destroy(&names->scripts);
    name_table_destroy(&names->script_modules);
    name_table_destroy(&names->adapters);
    name_table_destroy(&names->actions);
    name_table_destroy(&names->timers);
    name_table_destroy(&names->cameras);
    name_table_destroy(&names->fonts);
    name_table_destroy(&names->images);
    name_table_destroy(&names->sounds);
    name_table_destroy(&names->music);
    name_table_destroy(&names->scenes);
    name_table_destroy(&names->sensors);
    name_table_destroy(&names->used_adapters);
    name_table_destroy(&names->used_scripts);
}

bool sdl3d_game_data_validate_document(yyjson_val *root, const char *source_path, const char *base_dir,
                                       const sdl3d_asset_resolver *assets,
                                       const sdl3d_game_data_validation_options *options, char *error_buffer,
                                       int error_buffer_size)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        error_buffer[0] = '\0';

    validation_context ctx = {
        .options = options,
        .source_path = source_path,
        .base_dir = base_dir,
        .assets = assets,
        .error_buffer = error_buffer,
        .error_buffer_size = error_buffer_size,
        .failed = false,
    };

    if (!yyjson_is_obj(root))
        return validation_error(&ctx, "$", "root must be an object");
    if (SDL_strcmp(json_string(root, "schema") != NULL ? json_string(root, "schema") : "", "sdl3d.game.v0") != 0)
        return validation_error(&ctx, "$.schema", "unsupported or missing game data schema");

    validation_names names;
    SDL_zero(names);
    const bool ok = collect_names(&ctx, root, &names) && validate_details(&ctx, root, &names) && !ctx.failed;
    validation_names_destroy(&names);
    return ok;
}

bool sdl3d_game_data_validate_file(const char *path, const sdl3d_game_data_validation_options *options,
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

    char *base_dir = path_dirname(path);
    const char *asset_name = path;
    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            asset_name = p + 1;
    }
    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    if (base_dir == NULL || assets == NULL)
    {
        SDL_free(base_dir);
        sdl3d_asset_resolver_destroy(assets);
        validation_context ctx = {
            .options = options,
            .source_path = path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        return validation_error(&ctx, "$", "failed to create validation asset resolver");
    }

    char asset_error[256];
    if (!sdl3d_asset_resolver_mount_directory(assets, base_dir, asset_error, (int)sizeof(asset_error)))
    {
        SDL_free(base_dir);
        sdl3d_asset_resolver_destroy(assets);
        validation_context ctx = {
            .options = options,
            .source_path = path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        return validation_error(&ctx, "$", "failed to mount validation directory: %s", asset_error);
    }

    const bool ok = sdl3d_game_data_validate_asset(assets, asset_name, options, error_buffer, error_buffer_size);
    SDL_free(base_dir);
    sdl3d_asset_resolver_destroy(assets);
    return ok;
}

bool sdl3d_game_data_validate_asset(sdl3d_asset_resolver *assets, const char *asset_path,
                                    const sdl3d_game_data_validation_options *options, char *error_buffer,
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

    sdl3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!sdl3d_asset_resolver_read_file(assets, asset_path, &buffer, asset_error, (int)sizeof(asset_error)))
    {
        validation_context ctx = {
            .options = options,
            .source_path = asset_path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        return validation_error(&ctx, "$", "failed to read game data asset: %s", asset_error);
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    if (doc == NULL)
    {
        sdl3d_asset_buffer_free(&buffer);
        validation_context ctx = {
            .options = options,
            .source_path = asset_path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        return validation_error(&ctx, "$", "yyjson error %u at byte %llu: %s", err.code, (unsigned long long)err.pos,
                                err.msg != NULL ? err.msg : "");
    }

    char *base_dir = path_dirname(asset_path_without_scheme(asset_path));
    const bool ok = sdl3d_game_data_validate_document(yyjson_doc_get_root(doc), asset_path, base_dir, assets, options,
                                                      error_buffer, error_buffer_size);
    SDL_free(base_dir);
    yyjson_doc_free(doc);
    sdl3d_asset_buffer_free(&buffer);
    return ok;
}

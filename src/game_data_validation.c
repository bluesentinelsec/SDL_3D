/**
 * @file game_data_validation.c
 * @brief Validation for JSON-authored game data.
 */

#include "game_data_validation.h"

#include <stdarg.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_standard_options.h"
#include "network_replication_schema.h"
#include "sdl3d/input.h"
#include "sdl3d_crypto.h"

#define PATH_BUFFER_SIZE 256
#define GAME_DATA_MENU_TEXT_MAX_BYTES 255

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
    name_table actor_archetypes;
    name_table actor_pools;
    name_table actor_pool_actors;
    name_table signals;
    name_table scripts;
    name_table script_modules;
    name_table adapters;
    name_table actions;
    name_table input_assignment_sets;
    name_table input_profiles;
    name_table network_input_channels;
    name_table timers;
    name_table cameras;
    name_table fonts;
    name_table images;
    name_table sprites;
    name_table sounds;
    name_table music;
    name_table scenes;
    name_table sensors;
    name_table persistence;
    name_table used_adapters;
    name_table used_scripts;
    script_manifest *script_manifests;
    int script_count;
} validation_names;

static bool validate_data_condition(validation_context *ctx, yyjson_val *condition, const char *path,
                                    validation_names *names);
static bool validate_storage(validation_context *ctx, yyjson_val *root);
static bool require_unique_name(validation_context *ctx, name_table *table, const char *kind, const char *name,
                                const char *json_path);
static bool require_ref(validation_context *ctx, const name_table *table, const char *kind, const char *name,
                        const char *json_path);
static bool require_network_string_entry(validation_context *ctx, yyjson_val *map, const char *path, const char *label,
                                         const char *name);
static bool asset_path_exists(validation_context *ctx, const char *asset_path, const char *json_path,
                              const char *asset_kind);
static void format_path(char *buffer, size_t buffer_size, const char *format, ...);
static bool validation_error(validation_context *ctx, const char *json_path, const char *format, ...);

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

static bool is_virtual_storage_path(const char *value)
{
    return value != NULL && (SDL_strncmp(value, "user://", 7) == 0 || SDL_strncmp(value, "cache://", 8) == 0);
}

static bool validate_network_port_value(validation_context *ctx, yyjson_val *value, const char *json_path,
                                        const char *label)
{
    if (value == NULL)
        return true;
    if (yyjson_is_int(value))
    {
        const int port = (int)yyjson_get_int(value);
        if (port > 0 && port <= 65535)
            return true;
    }
    else if (yyjson_is_str(value) && yyjson_get_str(value) != NULL && yyjson_get_str(value)[0] != '\0')
    {
        return true;
    }
    return validation_error(ctx, json_path, "%s must be a non-empty string or integer 1..65535", label);
}

static bool validation_key_name_valid(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return false;
    if (SDL_strcmp(name, "UP") == 0 || SDL_strcmp(name, "DOWN") == 0 || SDL_strcmp(name, "LEFT") == 0 ||
        SDL_strcmp(name, "RIGHT") == 0 || SDL_strcmp(name, "RETURN") == 0 || SDL_strcmp(name, "ESCAPE") == 0 ||
        SDL_strcmp(name, "BACKSPACE") == 0 || SDL_strcmp(name, "DELETE") == 0)
    {
        return true;
    }
    if (SDL_strlen(name) == 1)
        return SDL_GetScancodeFromKey(SDL_GetKeyFromName(name), NULL) != SDL_SCANCODE_UNKNOWN;
    return SDL_GetScancodeFromName(name) != SDL_SCANCODE_UNKNOWN;
}

static bool validation_mouse_button_name_valid(const char *name)
{
    return name != NULL &&
           (SDL_strcmp(name, "LEFT") == 0 || SDL_strcmp(name, "MIDDLE") == 0 || SDL_strcmp(name, "RIGHT") == 0 ||
            SDL_strcmp(name, "X1") == 0 || SDL_strcmp(name, "X2") == 0);
}

static bool validation_mouse_axis_name_valid(const char *name)
{
    return name != NULL && (SDL_strcmp(name, "x") == 0 || SDL_strcmp(name, "y") == 0 ||
                            SDL_strcmp(name, "wheel") == 0 || SDL_strcmp(name, "wheel_x") == 0);
}

static bool validation_gamepad_axis_name_valid(const char *name)
{
    return name != NULL && (SDL_strcmp(name, "left_x") == 0 || SDL_strcmp(name, "left_y") == 0 ||
                            SDL_strcmp(name, "right_x") == 0 || SDL_strcmp(name, "right_y") == 0 ||
                            SDL_strcmp(name, "left_trigger") == 0 || SDL_strcmp(name, "right_trigger") == 0);
}

static bool validation_gamepad_button_name_valid(const char *name)
{
    static const char *const valid[] = {
        "START",       "BACK",          "SOUTH",          "NORTH",        "EAST",          "WEST",         "LEFT_STICK",
        "RIGHT_STICK", "LEFT_SHOULDER", "RIGHT_SHOULDER", "DPAD_UP",      "DPAD_DOWN",     "DPAD_LEFT",    "DPAD_RIGHT",
        "GUIDE",       "MISC1",         "RIGHT_PADDLE1",  "LEFT_PADDLE1", "RIGHT_PADDLE2", "LEFT_PADDLE2", "TOUCHPAD"};
    for (size_t i = 0; name != NULL && i < SDL_arraysize(valid); ++i)
    {
        if (SDL_strcmp(name, valid[i]) == 0)
            return true;
    }
    return false;
}

static bool input_device_name_valid(const char *device)
{
    return SDL_strcmp(device != NULL ? device : "", "keyboard") == 0 ||
           SDL_strcmp(device != NULL ? device : "", "gamepad") == 0 ||
           SDL_strcmp(device != NULL ? device : "", "mouse") == 0;
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

static bool validate_persistence_properties(validation_context *ctx, yyjson_val *properties, const char *json_path)
{
    if (!yyjson_is_arr(properties) || yyjson_arr_size(properties) == 0)
        return validation_error(ctx, json_path, "persistence properties must be a non-empty array");

    for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *property = yyjson_arr_get(properties, i);
        if (yyjson_is_str(property) && yyjson_get_str(property)[0] != '\0')
            continue;
        if (yyjson_is_obj(property) && is_non_empty_string(property, "key"))
            continue;
        return validation_error(ctx, path, "persistence property must be a non-empty string or object with key");
    }
    return true;
}

static bool validate_persistence(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *persistence = obj_get(root, "persistence");
    if (persistence == NULL)
        return true;
    if (!yyjson_is_obj(persistence))
        return validation_error(ctx, "$.persistence", "persistence must be an object");

    yyjson_val *entries = obj_get(persistence, "entries");
    if (entries == NULL)
        return true;
    if (!yyjson_is_arr(entries))
        return validation_error(ctx, "$.persistence.entries", "persistence.entries must be an array");

    for (size_t i = 0; i < yyjson_arr_size(entries); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.persistence.entries[%zu]", i);
        yyjson_val *entry = yyjson_arr_get(entries, i);
        if (!yyjson_is_obj(entry))
            return validation_error(ctx, path, "persistence entry must be an object");
        if (!require_unique_name(ctx, &names->persistence, "persistence entry", json_string(entry, "name"), path))
            return false;

        char field_path[PATH_BUFFER_SIZE];
        format_path(field_path, sizeof(field_path), "%s.path", path);
        const char *storage_path = json_string(entry, "path");
        if (storage_path == NULL || storage_path[0] == '\0' || !is_virtual_storage_path(storage_path))
            return validation_error(ctx, field_path, "persistence path must use user:// or cache://");
        if (!require_ref(ctx, &names->entities, "entity", json_string(entry, "target"), path))
            return false;
        format_path(field_path, sizeof(field_path), "%s.properties", path);
        if (!validate_persistence_properties(ctx, obj_get(entry, "properties"), field_path))
            return false;
        yyjson_val *schema = obj_get(entry, "schema");
        if (schema != NULL && (!yyjson_is_str(schema) || yyjson_get_str(schema)[0] == '\0'))
            return validation_error(ctx, path, "persistence schema must be a non-empty string");
        yyjson_val *version = obj_get(entry, "version");
        if (version != NULL && !yyjson_is_int(version))
            return validation_error(ctx, path, "persistence version must be an integer");
        yyjson_val *condition = obj_get(entry, "enabled_if");
        if (condition != NULL)
        {
            format_path(field_path, sizeof(field_path), "%s.enabled_if", path);
            if (!validate_data_condition(ctx, condition, field_path, names))
                return false;
        }
    }
    return true;
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

static bool require_actor_ref(validation_context *ctx, const validation_names *names, const char *name,
                              const char *json_path)
{
    if (name == NULL || name[0] == '\0')
        return validation_error(ctx, json_path, "missing actor reference");
    if (!name_table_contains(&names->entities, name) && !name_table_contains(&names->actor_pool_actors, name))
        return validation_error(ctx, json_path, "unknown actor reference '%s'", name);
    return true;
}

static bool note_name(name_table *table, const char *name, const char *json_path)
{
    if (name == NULL || name[0] == '\0' || name_table_contains(table, name))
        return true;
    return name_table_add(table, name, json_path);
}

static bool is_replication_direction(const char *direction, bool allow_bidirectional)
{
    return direction != NULL &&
           (SDL_strcmp(direction, "host_to_client") == 0 || SDL_strcmp(direction, "client_to_host") == 0 ||
            (allow_bidirectional && SDL_strcmp(direction, "bidirectional") == 0));
}

static bool is_replication_property_path(const char *path)
{
    if (path == NULL || path[0] == '\0' || path[0] == '.' || path[SDL_strlen(path) - 1u] == '.')
        return false;

    bool previous_dot = false;
    for (const char *p = path; *p != '\0'; ++p)
    {
        const char c = *p;
        const bool valid =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!valid)
            return false;
        if (c == '.' && previous_dot)
            return false;
        previous_dot = c == '.';
    }
    return true;
}

static void network_hash_update(sdl3d_crypto_hash32_state *state, const char *label, const char *value)
{
    static const char sep = '\0';
    static const char null_marker = '\1';
    sdl3d_crypto_hash32_update(state, label, SDL_strlen(label));
    sdl3d_crypto_hash32_update(state, &sep, 1u);
    if (value != NULL)
    {
        sdl3d_crypto_hash32_update(state, value, SDL_strlen(value));
    }
    else
    {
        sdl3d_crypto_hash32_update(state, &null_marker, 1u);
    }
    sdl3d_crypto_hash32_update(state, &sep, 1u);
}

static void network_hash_update_int(sdl3d_crypto_hash32_state *state, const char *label, Sint64 value)
{
    char buffer[32];
    SDL_snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    network_hash_update(state, label, buffer);
}

static bool validate_network_actor_fields(validation_context *ctx, yyjson_val *fields, const char *json_path)
{
    if (!yyjson_is_arr(fields) || yyjson_arr_size(fields) == 0)
        return validation_error(ctx, json_path, "network actor fields must be a non-empty array");

    name_table field_names;
    SDL_zero(field_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(fields); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *field = yyjson_arr_get(fields, i);
        sdl3d_replication_field_descriptor descriptor;
        if (!sdl3d_replication_field_descriptor_from_json(field, &descriptor))
        {
            ok = validation_error(ctx, path,
                                  "network actor field must be a built-in field string or object with path and type");
            break;
        }
        if (!is_replication_property_path(descriptor.path))
        {
            ok = validation_error(ctx, path, "network actor field path '%s' is invalid", descriptor.path);
            break;
        }
        if (sdl3d_replication_field_wire_size(descriptor.type) == 0U)
        {
            ok = validation_error(ctx, path, "unsupported network actor field type");
            break;
        }
        if (!require_unique_name(ctx, &field_names, "network actor field", descriptor.path, path))
        {
            ok = false;
            break;
        }
    }
    name_table_destroy(&field_names);
    return ok;
}

static bool validate_network_actors(validation_context *ctx, yyjson_val *actors, const char *json_path,
                                    validation_names *names)
{
    if (!yyjson_is_arr(actors) || yyjson_arr_size(actors) == 0)
        return validation_error(ctx, json_path, "network actors must be a non-empty array");

    name_table actor_names;
    SDL_zero(actor_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(actors); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *actor = yyjson_arr_get(actors, i);
        if (!yyjson_is_obj(actor))
        {
            ok = validation_error(ctx, path, "network actor entry must be an object");
            break;
        }
        const char *entity = json_string(actor, "entity");
        if (!require_ref(ctx, &names->entities, "entity", entity, path) ||
            !require_unique_name(ctx, &actor_names, "network actor", entity, path))
        {
            ok = false;
            break;
        }
        char fields_path[PATH_BUFFER_SIZE];
        format_path(fields_path, sizeof(fields_path), "%s.fields", path);
        ok = validate_network_actor_fields(ctx, obj_get(actor, "fields"), fields_path);
    }
    name_table_destroy(&actor_names);
    return ok;
}

static bool validate_network_pools(validation_context *ctx, yyjson_val *pools, const char *json_path,
                                   validation_names *names)
{
    if (!yyjson_is_arr(pools) || yyjson_arr_size(pools) == 0)
        return validation_error(ctx, json_path, "network pools must be a non-empty array");

    name_table pool_names;
    SDL_zero(pool_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(pools); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *pool = yyjson_arr_get(pools, i);
        if (!yyjson_is_obj(pool))
        {
            ok = validation_error(ctx, path, "network pool entry must be an object");
            break;
        }
        const char *pool_name = json_string(pool, "pool");
        if (!require_ref(ctx, &names->actor_pools, "actor pool", pool_name, path) ||
            !require_unique_name(ctx, &pool_names, "network pool", pool_name, path))
        {
            ok = false;
            break;
        }
        char fields_path[PATH_BUFFER_SIZE];
        format_path(fields_path, sizeof(fields_path), "%s.fields", path);
        ok = validate_network_actor_fields(ctx, obj_get(pool, "fields"), fields_path);
    }
    name_table_destroy(&pool_names);
    return ok;
}

static bool validate_network_inputs(validation_context *ctx, yyjson_val *inputs, const char *json_path,
                                    validation_names *names)
{
    if (!yyjson_is_arr(inputs) || yyjson_arr_size(inputs) == 0)
        return validation_error(ctx, json_path, "network inputs must be a non-empty array");

    name_table input_names;
    SDL_zero(input_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(inputs); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *input = yyjson_arr_get(inputs, i);
        if (!yyjson_is_obj(input))
        {
            ok = validation_error(ctx, path, "network input entry must be an object");
            break;
        }
        const char *action = json_string(input, "action");
        if (!require_ref(ctx, &names->actions, "input action", action, path) ||
            !require_unique_name(ctx, &input_names, "network input action", action, path))
        {
            ok = false;
            break;
        }
    }
    name_table_destroy(&input_names);
    return ok;
}

static bool validate_network_scene_state(validation_context *ctx, yyjson_val *network)
{
    yyjson_val *scene_state = obj_get(network, "scene_state");
    if (scene_state == NULL)
        return true;
    if (!yyjson_is_obj(scene_state))
        return validation_error(ctx, "$.network.scene_state", "network scene_state must be an object");

    yyjson_val *scope_key;
    yyjson_obj_iter scope_iter;
    yyjson_obj_iter_init(scene_state, &scope_iter);
    while ((scope_key = yyjson_obj_iter_next(&scope_iter)) != NULL)
    {
        const char *scope_name = yyjson_get_str(scope_key);
        yyjson_val *scope = yyjson_obj_iter_get_val(scope_key);
        char scope_path[PATH_BUFFER_SIZE];
        format_path(scope_path, sizeof(scope_path), "$.network.scene_state.%s",
                    scope_name != NULL ? scope_name : "<invalid>");
        if (scope_name == NULL || scope_name[0] == '\0')
            return validation_error(ctx, scope_path, "network scene_state scope must have a non-empty name");
        if (!yyjson_is_obj(scope))
            return validation_error(ctx, scope_path, "network scene_state scope must be an object");

        yyjson_val *key;
        yyjson_obj_iter key_iter;
        yyjson_obj_iter_init(scope, &key_iter);
        while ((key = yyjson_obj_iter_next(&key_iter)) != NULL)
        {
            const char *name = yyjson_get_str(key);
            yyjson_val *value = yyjson_obj_iter_get_val(key);
            char key_path[PATH_BUFFER_SIZE];
            format_path(key_path, sizeof(key_path), "%s.%s", scope_path, name != NULL ? name : "<invalid>");
            if (name == NULL || name[0] == '\0')
                return validation_error(ctx, key_path, "network scene_state key name must be non-empty");
            if (!yyjson_is_str(value) || yyjson_get_len(value) == 0)
                return validation_error(ctx, key_path, "network scene_state key value must be a non-empty string");
        }
    }

    return true;
}

static bool network_managed_runtime_enabled_json(yyjson_val *network)
{
    yyjson_val *enabled = obj_get(obj_get(obj_get(network, "session_flow"), "managed_runtime"), "enabled");
    return yyjson_is_bool(enabled) && yyjson_get_bool(enabled);
}

static bool validate_managed_network_scene_state(validation_context *ctx, yyjson_val *network)
{
    if (!network_managed_runtime_enabled_json(network))
        return true;

    yyjson_val *scene_state = obj_get(network, "scene_state");
    yyjson_val *host = obj_get(scene_state, "host");
    yyjson_val *direct_connect = obj_get(scene_state, "direct_connect");
    const char *host_keys[] = {"status", "endpoint", "peer", "connected"};
    const char *direct_connect_keys[] = {"status", "state", "connected"};

    for (size_t i = 0U; i < SDL_arraysize(host_keys); ++i)
    {
        if (!require_network_string_entry(ctx, host, "$.network.scene_state.host", "scene_state host key",
                                          host_keys[i]))
        {
            return false;
        }
    }
    for (size_t i = 0U; i < SDL_arraysize(direct_connect_keys); ++i)
    {
        if (!require_network_string_entry(ctx, direct_connect, "$.network.scene_state.direct_connect",
                                          "scene_state direct_connect key", direct_connect_keys[i]))
        {
            return false;
        }
    }

    return true;
}

static bool validate_action_array(validation_context *ctx, yyjson_val *actions, const char *json_path,
                                  validation_names *names);

static bool validate_network_session_string_map(validation_context *ctx, yyjson_val *map, const char *json_path,
                                                const char *label, const name_table *scene_names)
{
    if (map == NULL)
        return true;
    if (!yyjson_is_obj(map))
        return validation_error(ctx, json_path, "network session_flow %s must be an object", label);

    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(map, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.%s", json_path, name != NULL ? name : "<invalid>");
        if (name == NULL || name[0] == '\0')
            return validation_error(ctx, path, "network session_flow %s key must be non-empty", label);
        if (!yyjson_is_str(value) || yyjson_get_len(value) == 0)
            return validation_error(ctx, path, "network session_flow %s value must be a non-empty string", label);
        if (scene_names != NULL && !require_ref(ctx, scene_names, "scene", yyjson_get_str(value), path))
            return false;
    }

    return true;
}

static bool require_network_string_entry(validation_context *ctx, yyjson_val *map, const char *path, const char *label,
                                         const char *name)
{
    char entry_path[PATH_BUFFER_SIZE];
    format_path(entry_path, sizeof(entry_path), "%s.%s", path, name);
    if (map == NULL || !yyjson_is_obj(map) || !is_non_empty_string(map, name))
        return validation_error(ctx, entry_path, "managed network requires %s '%s'", label, name);
    return true;
}

static bool require_network_group_string_entry(validation_context *ctx, yyjson_val *groups, const char *path,
                                               const char *label, const char *group_name, const char *name)
{
    yyjson_val *group = obj_get(groups, group_name);
    char entry_path[PATH_BUFFER_SIZE];
    format_path(entry_path, sizeof(entry_path), "%s.%s.%s", path, group_name, name);
    if (group == NULL || !yyjson_is_obj(group) || !is_non_empty_string(group, name))
        return validation_error(ctx, entry_path, "managed network requires %s '%s.%s'", label, group_name, name);
    return true;
}

static bool validate_network_managed_keep_alive_scenes(validation_context *ctx, yyjson_val *managed, yyjson_val *scenes)
{
    yyjson_val *keep_alive = obj_get(managed, "keep_alive_scenes");
    if (keep_alive == NULL)
        return true;
    if (!yyjson_is_obj(keep_alive))
        return validation_error(ctx, "$.network.session_flow.managed_runtime.keep_alive_scenes",
                                "managed network keep_alive_scenes must be an object");

    yyjson_val *session_key;
    yyjson_obj_iter session_iter;
    yyjson_obj_iter_init(keep_alive, &session_iter);
    while ((session_key = yyjson_obj_iter_next(&session_iter)) != NULL)
    {
        const char *session_name = yyjson_get_str(session_key);
        yyjson_val *list = yyjson_obj_iter_get_val(session_key);
        char session_path[PATH_BUFFER_SIZE];
        format_path(session_path, sizeof(session_path), "$.network.session_flow.managed_runtime.keep_alive_scenes.%s",
                    session_name != NULL ? session_name : "<invalid>");
        if (session_name == NULL || session_name[0] == '\0')
            return validation_error(ctx, session_path, "managed network keep-alive session name must be non-empty");
        if (!yyjson_is_arr(list) || yyjson_arr_size(list) == 0)
            return validation_error(ctx, session_path, "managed network keep-alive scenes must be a non-empty array");

        for (size_t i = 0U; i < yyjson_arr_size(list); ++i)
        {
            yyjson_val *entry = yyjson_arr_get(list, i);
            const char *scene_semantic = yyjson_is_str(entry) ? yyjson_get_str(entry) : NULL;
            char entry_path[PATH_BUFFER_SIZE];
            format_path(entry_path, sizeof(entry_path), "%s[%zu]", session_path, i);
            if (scene_semantic == NULL || scene_semantic[0] == '\0')
                return validation_error(ctx, entry_path, "managed network keep-alive scene must be a non-empty string");
            if (!is_non_empty_string(scenes, scene_semantic))
                return validation_error(ctx, entry_path,
                                        "managed network keep-alive scene must reference session_flow.scenes");
        }
    }

    return true;
}

static bool validate_network_managed_runtime(validation_context *ctx, yyjson_val *flow)
{
    yyjson_val *managed = obj_get(flow, "managed_runtime");
    if (managed == NULL)
        return true;
    if (!yyjson_is_obj(managed))
        return validation_error(ctx, "$.network.session_flow.managed_runtime",
                                "managed network runtime must be an object");

    yyjson_val *enabled = obj_get(managed, "enabled");
    if (enabled != NULL && !yyjson_is_bool(enabled))
        return validation_error(ctx, "$.network.session_flow.managed_runtime.enabled",
                                "managed network enabled must be boolean");

    yyjson_val *ack_delay = obj_get(managed, "termination_ack_delay_seconds");
    if (ack_delay != NULL && (!yyjson_is_num(ack_delay) || yyjson_get_real(ack_delay) < 0.0))
    {
        return validation_error(ctx, "$.network.session_flow.managed_runtime.termination_ack_delay_seconds",
                                "managed network termination_ack_delay_seconds must be a non-negative number");
    }

    yyjson_val *scenes = obj_get(flow, "scenes");
    if (!validate_network_managed_keep_alive_scenes(ctx, managed, scenes))
        return false;

    if (enabled == NULL || !yyjson_get_bool(enabled))
        return true;

    yyjson_val *state_keys = obj_get(flow, "state_keys");
    yyjson_val *state_values = obj_get(flow, "state_values");
    yyjson_val *events = obj_get(flow, "events");
    const char *required_scenes[] = {"play", "host_lobby", "direct_connect", "discovery"};
    const char *required_state_keys[] = {"match_mode", "network_role", "network_flow", "match_termination_active"};
    const char *required_events[] = {"host_start_game",           "client_start_game",
                                     "client_state_before_start", "host_match_terminated",
                                     "client_match_terminated",   "host_client_disconnected",
                                     "client_connection_closed",  "network_match_termination_ack"};

    for (size_t i = 0U; i < SDL_arraysize(required_scenes); ++i)
    {
        if (!require_network_string_entry(ctx, scenes, "$.network.session_flow.scenes", "session scene",
                                          required_scenes[i]))
        {
            return false;
        }
    }
    for (size_t i = 0U; i < SDL_arraysize(required_state_keys); ++i)
    {
        if (!require_network_string_entry(ctx, state_keys, "$.network.session_flow.state_keys", "session state key",
                                          required_state_keys[i]))
        {
            return false;
        }
    }

    if (!require_network_group_string_entry(ctx, state_values, "$.network.session_flow.state_values",
                                            "session state value", "match_mode", "network") ||
        !require_network_group_string_entry(ctx, state_values, "$.network.session_flow.state_values",
                                            "session state value", "network_role", "host") ||
        !require_network_group_string_entry(ctx, state_values, "$.network.session_flow.state_values",
                                            "session state value", "network_role", "client") ||
        !require_network_group_string_entry(ctx, state_values, "$.network.session_flow.state_values",
                                            "session state value", "network_flow", "host") ||
        !require_network_group_string_entry(ctx, state_values, "$.network.session_flow.state_values",
                                            "session state value", "network_flow", "direct"))
    {
        return false;
    }

    for (size_t i = 0U; i < SDL_arraysize(required_events); ++i)
    {
        char event_path[PATH_BUFFER_SIZE];
        format_path(event_path, sizeof(event_path), "$.network.session_flow.events.%s", required_events[i]);
        if (events == NULL || !yyjson_is_obj(events) || obj_get(events, required_events[i]) == NULL)
            return validation_error(ctx, event_path, "managed network requires session flow event '%s'",
                                    required_events[i]);
    }
    if (obj_get(managed, "keep_alive_scenes") == NULL)
    {
        return validation_error(ctx, "$.network.session_flow.managed_runtime.keep_alive_scenes",
                                "managed network requires keep_alive_scenes");
    }
    if (ack_delay == NULL)
    {
        return validation_error(ctx, "$.network.session_flow.managed_runtime.termination_ack_delay_seconds",
                                "managed network requires termination_ack_delay_seconds");
    }

    return true;
}

static bool validate_network_session_flow(validation_context *ctx, yyjson_val *network, validation_names *names)
{
    yyjson_val *flow = obj_get(network, "session_flow");
    if (flow == NULL)
        return true;
    if (!yyjson_is_obj(flow))
        return validation_error(ctx, "$.network.session_flow", "network session_flow must be an object");

    if (!validate_network_session_string_map(ctx, obj_get(flow, "scenes"), "$.network.session_flow.scenes", "scenes",
                                             &names->scenes) ||
        !validate_network_session_string_map(ctx, obj_get(flow, "state_keys"), "$.network.session_flow.state_keys",
                                             "state_keys", NULL))
    {
        return false;
    }

    yyjson_val *state_values = obj_get(flow, "state_values");
    yyjson_val *messages = obj_get(flow, "messages");
    const struct
    {
        yyjson_val *root;
        const char *path;
        const char *label;
    } grouped_maps[] = {
        {state_values, "$.network.session_flow.state_values", "state_values"},
        {messages, "$.network.session_flow.messages", "messages"},
    };

    for (size_t map_index = 0; map_index < SDL_arraysize(grouped_maps); ++map_index)
    {
        if (grouped_maps[map_index].root == NULL)
            continue;
        if (!yyjson_is_obj(grouped_maps[map_index].root))
            return validation_error(ctx, grouped_maps[map_index].path, "network session_flow %s must be an object",
                                    grouped_maps[map_index].label);

        yyjson_val *group_key;
        yyjson_obj_iter group_iter;
        yyjson_obj_iter_init(grouped_maps[map_index].root, &group_iter);
        while ((group_key = yyjson_obj_iter_next(&group_iter)) != NULL)
        {
            const char *group_name = yyjson_get_str(group_key);
            yyjson_val *group = yyjson_obj_iter_get_val(group_key);
            char group_path[PATH_BUFFER_SIZE];
            format_path(group_path, sizeof(group_path), "%s.%s", grouped_maps[map_index].path,
                        group_name != NULL ? group_name : "<invalid>");
            if (group_name == NULL || group_name[0] == '\0')
                return validation_error(ctx, group_path, "network session_flow %s group must be non-empty",
                                        grouped_maps[map_index].label);
            if (!validate_network_session_string_map(ctx, group, group_path, grouped_maps[map_index].label, NULL))
                return false;
        }
    }

    yyjson_val *events = obj_get(flow, "events");
    if (events != NULL)
    {
        if (!yyjson_is_obj(events))
            return validation_error(ctx, "$.network.session_flow.events",
                                    "network session_flow events must be an object");
        yyjson_val *event_key;
        yyjson_obj_iter event_iter;
        yyjson_obj_iter_init(events, &event_iter);
        while ((event_key = yyjson_obj_iter_next(&event_iter)) != NULL)
        {
            const char *event_name = yyjson_get_str(event_key);
            yyjson_val *event = yyjson_obj_iter_get_val(event_key);
            char event_path[PATH_BUFFER_SIZE];
            format_path(event_path, sizeof(event_path), "$.network.session_flow.events.%s",
                        event_name != NULL ? event_name : "<invalid>");
            if (event_name == NULL || event_name[0] == '\0')
                return validation_error(ctx, event_path, "network session_flow event name must be non-empty");
            if (yyjson_is_arr(event))
            {
                if (!validate_action_array(ctx, event, event_path, names))
                    return false;
            }
            else if (yyjson_is_obj(event))
            {
                yyjson_val *pause = obj_get(event, "pause");
                if (pause != NULL && !yyjson_is_bool(pause))
                    return validation_error(ctx, event_path, "network session_flow event pause must be boolean");
                yyjson_val *actions = obj_get(event, "actions");
                if (actions != NULL)
                {
                    char actions_path[PATH_BUFFER_SIZE];
                    format_path(actions_path, sizeof(actions_path), "%s.actions", event_path);
                    if (!validate_action_array(ctx, actions, actions_path, names))
                        return false;
                }
            }
            else
            {
                return validation_error(ctx, event_path,
                                        "network session_flow event must be an action array or object");
            }
        }
    }

    return validate_network_managed_runtime(ctx, flow);
}

static bool validate_network_runtime_binding_map(validation_context *ctx, yyjson_val *map, const char *json_path,
                                                 const char *label, const name_table *references,
                                                 bool require_unique_values)
{
    if (map == NULL)
        return true;
    if (!yyjson_is_obj(map))
        return validation_error(ctx, json_path, "network runtime_bindings %s must be an object", label);

    name_table values = {0};
    bool ok = true;
    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(map, &iter);
    while (ok && (key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.%s", json_path, name != NULL ? name : "<invalid>");
        if (name == NULL || name[0] == '\0')
        {
            ok = validation_error(ctx, path, "network runtime_bindings %s key must be non-empty", label);
        }
        else if (!yyjson_is_str(value) || yyjson_get_len(value) == 0)
        {
            ok = validation_error(ctx, path, "network runtime_bindings %s value must be a non-empty string", label);
        }
        else if (!require_ref(ctx, references, label, yyjson_get_str(value), path))
        {
            ok = false;
        }
        else if (require_unique_values &&
                 !require_unique_name(ctx, &values, "network runtime binding value", yyjson_get_str(value), path))
        {
            ok = false;
        }
    }

    name_table_destroy(&values);
    return ok;
}

static bool validate_network_runtime_pause_binding(validation_context *ctx, yyjson_val *pause, validation_names *names)
{
    if (pause == NULL)
        return true;
    if (!yyjson_is_obj(pause))
        return validation_error(ctx, "$.network.runtime_bindings.pause",
                                "network runtime_bindings pause must be an object");

    if (!require_ref(ctx, &names->actions, "input action", json_string(pause, "action"),
                     "$.network.runtime_bindings.pause.action"))
    {
        return false;
    }

    yyjson_val *state = obj_get(pause, "state");
    if (!yyjson_is_obj(state))
        return validation_error(ctx, "$.network.runtime_bindings.pause.state",
                                "network runtime_bindings pause state must be an object");
    if (!require_ref(ctx, &names->entities, "entity", json_string(state, "actor"),
                     "$.network.runtime_bindings.pause.state.actor"))
    {
        return false;
    }
    if (!is_non_empty_string(state, "property"))
        return validation_error(ctx, "$.network.runtime_bindings.pause.state.property",
                                "network runtime_bindings pause state property must be a non-empty string");

    return true;
}

static bool require_network_runtime_binding(validation_context *ctx, yyjson_val *bindings, const char *section,
                                            const char *name, const char *label, const name_table *references)
{
    yyjson_val *map = obj_get(bindings, section);
    char path[PATH_BUFFER_SIZE];
    format_path(path, sizeof(path), "$.network.runtime_bindings.%s.%s", section, name);
    const char *value = json_string(map, name);
    if (value == NULL || value[0] == '\0')
        return validation_error(ctx, path, "managed network requires runtime binding '%s.%s'", section, name);
    return require_ref(ctx, references, label, value, path);
}

static bool validate_managed_network_runtime_bindings(validation_context *ctx, yyjson_val *bindings,
                                                      const name_table *replication_names,
                                                      const name_table *control_names, validation_names *names)
{
    const char *replication_bindings[] = {"state_snapshot", "client_input"};
    const char *control_bindings[] = {"start_game", "pause_request", "resume_request", "disconnect"};
    const char *action_bindings[] = {"menu_select", "camera_toggle"};
    const char *signal_bindings[] = {"lobby_start", "camera_toggle"};

    for (size_t i = 0U; i < SDL_arraysize(replication_bindings); ++i)
    {
        if (!require_network_runtime_binding(ctx, bindings, "replication", replication_bindings[i],
                                             "network replication", replication_names))
        {
            return false;
        }
    }
    for (size_t i = 0U; i < SDL_arraysize(control_bindings); ++i)
    {
        if (!require_network_runtime_binding(ctx, bindings, "controls", control_bindings[i], "network control message",
                                             control_names))
        {
            return false;
        }
    }
    for (size_t i = 0U; i < SDL_arraysize(action_bindings); ++i)
    {
        if (!require_network_runtime_binding(ctx, bindings, "actions", action_bindings[i], "input action",
                                             &names->actions))
        {
            return false;
        }
    }
    for (size_t i = 0U; i < SDL_arraysize(signal_bindings); ++i)
    {
        if (!require_network_runtime_binding(ctx, bindings, "signals", signal_bindings[i], "signal", &names->signals))
            return false;
    }
    if (obj_get(bindings, "pause") == NULL)
    {
        return validation_error(ctx, "$.network.runtime_bindings.pause",
                                "managed network requires runtime_bindings.pause");
    }

    return true;
}

static bool validate_network_runtime_bindings(validation_context *ctx, yyjson_val *network,
                                              const name_table *replication_names, const name_table *control_names,
                                              validation_names *names)
{
    yyjson_val *bindings = obj_get(network, "runtime_bindings");
    const bool managed_required = network_managed_runtime_enabled_json(network);
    if (bindings == NULL)
    {
        if (managed_required)
            return validation_error(ctx, "$.network.runtime_bindings", "managed network requires runtime_bindings");
        return true;
    }
    if (!yyjson_is_obj(bindings))
        return validation_error(ctx, "$.network.runtime_bindings", "network runtime_bindings must be an object");

    if (!validate_network_runtime_binding_map(ctx, obj_get(bindings, "replication"),
                                              "$.network.runtime_bindings.replication", "network replication",
                                              replication_names, false) ||
        !validate_network_runtime_binding_map(ctx, obj_get(bindings, "controls"), "$.network.runtime_bindings.controls",
                                              "network control message", control_names, true) ||
        !validate_network_runtime_binding_map(ctx, obj_get(bindings, "actions"), "$.network.runtime_bindings.actions",
                                              "input action", &names->actions, false) ||
        !validate_network_runtime_binding_map(ctx, obj_get(bindings, "signals"), "$.network.runtime_bindings.signals",
                                              "signal", &names->signals, false) ||
        !validate_network_runtime_pause_binding(ctx, obj_get(bindings, "pause"), names))
    {
        return false;
    }

    return !managed_required ||
           validate_managed_network_runtime_bindings(ctx, bindings, replication_names, control_names, names);
}

static bool is_network_diagnostic_level(const char *level)
{
    return level == NULL || SDL_strcmp(level, "debug") == 0 || SDL_strcmp(level, "info") == 0 ||
           SDL_strcmp(level, "warn") == 0 || SDL_strcmp(level, "warning") == 0 || SDL_strcmp(level, "error") == 0 ||
           SDL_strcmp(level, "critical") == 0;
}

static bool validate_network_diagnostics(validation_context *ctx, yyjson_val *network,
                                         const name_table *replication_names)
{
    yyjson_val *diagnostics = obj_get(network, "diagnostics");
    if (diagnostics == NULL)
        return true;
    if (!yyjson_is_obj(diagnostics))
        return validation_error(ctx, "$.network.diagnostics", "network diagnostics must be an object");

    yyjson_val *snapshots = obj_get(diagnostics, "snapshots");
    if (snapshots == NULL)
        return true;
    if (!yyjson_is_arr(snapshots))
        return validation_error(ctx, "$.network.diagnostics.snapshots",
                                "network diagnostics snapshots must be an array");

    name_table diagnostic_names = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(snapshots); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.network.diagnostics.snapshots[%zu]", i);
        yyjson_val *entry = yyjson_arr_get(snapshots, i);
        if (!yyjson_is_obj(entry))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic must be an object");
            break;
        }
        if (!require_unique_name(ctx, &diagnostic_names, "network snapshot diagnostic", json_string(entry, "name"),
                                 path) ||
            !require_ref(ctx, replication_names, "network replication", json_string(entry, "replication"), path))
        {
            ok = false;
            break;
        }
        yyjson_val *enabled = obj_get(entry, "enabled");
        if (enabled != NULL && !yyjson_is_bool(enabled))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic enabled must be boolean");
            break;
        }
        yyjson_val *include_session_state = obj_get(entry, "include_session_state");
        if (include_session_state != NULL && !yyjson_is_bool(include_session_state))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic include_session_state must be boolean");
            break;
        }
        yyjson_val *cadence = obj_get(entry, "cadence_seconds");
        if (cadence != NULL && (!yyjson_is_num(cadence) || yyjson_get_real(cadence) < 0.0))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic cadence_seconds must be non-negative");
            break;
        }
        const char *level = json_string(entry, "level");
        if (!is_network_diagnostic_level(level))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic level is unsupported");
            break;
        }
        yyjson_val *message = obj_get(entry, "message");
        if (message != NULL && (!yyjson_is_str(message) || yyjson_get_len(message) == 0))
        {
            ok = validation_error(ctx, path, "network snapshot diagnostic message must be a non-empty string");
            break;
        }
    }

    name_table_destroy(&diagnostic_names);
    return ok;
}

static bool validate_network(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *network = obj_get(root, "network");
    if (network == NULL)
        return true;
    if (!yyjson_is_obj(network))
        return validation_error(ctx, "$.network", "network must be an object");

    yyjson_val *protocol = obj_get(network, "protocol");
    if (!yyjson_is_obj(protocol))
        return validation_error(ctx, "$.network.protocol", "network protocol must be an object");
    if (!is_non_empty_string(protocol, "id"))
        return validation_error(ctx, "$.network.protocol.id", "network protocol id must be a non-empty string");
    yyjson_val *version = obj_get(protocol, "version");
    if (!yyjson_is_int(version) || yyjson_get_sint(version) < 1)
        return validation_error(ctx, "$.network.protocol.version",
                                "network protocol version must be a positive integer");
    const char *transport = json_string(protocol, "transport");
    if (transport == NULL || SDL_strcmp(transport, "udp") != 0)
        return validation_error(ctx, "$.network.protocol.transport", "network protocol transport must be udp");
    yyjson_val *tick_rate = obj_get(protocol, "tick_rate");
    if (!yyjson_is_int(tick_rate) || yyjson_get_sint(tick_rate) <= 0)
        return validation_error(ctx, "$.network.protocol.tick_rate",
                                "network protocol tick_rate must be a positive integer");
    if (!validate_network_scene_state(ctx, network))
        return false;
    if (!validate_network_session_flow(ctx, network, names))
        return false;
    if (!validate_managed_network_scene_state(ctx, network))
        return false;

    yyjson_val *replication = obj_get(network, "replication");
    if (!yyjson_is_arr(replication) || yyjson_arr_size(replication) == 0)
        return validation_error(ctx, "$.network.replication", "network replication must be a non-empty array");

    name_table replication_names;
    SDL_zero(replication_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(replication); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.network.replication[%zu]", i);
        yyjson_val *entry = yyjson_arr_get(replication, i);
        if (!yyjson_is_obj(entry))
        {
            ok = validation_error(ctx, path, "network replication entry must be an object");
            break;
        }
        if (!require_unique_name(ctx, &replication_names, "network replication", json_string(entry, "name"), path))
        {
            ok = false;
            break;
        }
        const char *direction = json_string(entry, "direction");
        if (!is_replication_direction(direction, false))
        {
            ok = validation_error(ctx, path, "network replication direction must be host_to_client or client_to_host");
            break;
        }
        yyjson_val *rate = obj_get(entry, "rate");
        if (!yyjson_is_int(rate) || yyjson_get_sint(rate) <= 0)
        {
            ok = validation_error(ctx, path, "network replication rate must be a positive integer");
            break;
        }
        yyjson_val *actors = obj_get(entry, "actors");
        yyjson_val *pools = obj_get(entry, "pools");
        yyjson_val *inputs = obj_get(entry, "inputs");
        if (SDL_strcmp(direction, "host_to_client") == 0)
        {
            if (actors == NULL && pools == NULL)
            {
                ok = validation_error(ctx, path, "host_to_client network replication must declare actors or pools");
                break;
            }
            if (inputs != NULL)
            {
                ok = validation_error(ctx, path, "host_to_client network replication must not declare inputs");
                break;
            }
            char actors_path[PATH_BUFFER_SIZE];
            format_path(actors_path, sizeof(actors_path), "%s.actors", path);
            if (actors != NULL)
                ok = validate_network_actors(ctx, actors, actors_path, names);
            if (ok && pools != NULL)
            {
                char pools_path[PATH_BUFFER_SIZE];
                format_path(pools_path, sizeof(pools_path), "%s.pools", path);
                ok = validate_network_pools(ctx, pools, pools_path, names);
            }
        }
        else
        {
            if (inputs == NULL)
            {
                ok = validation_error(ctx, path, "client_to_host network replication must declare inputs");
                break;
            }
            if (actors != NULL)
            {
                ok = validation_error(ctx, path, "client_to_host network replication must not declare actors");
                break;
            }
            if (pools != NULL)
            {
                ok = validation_error(ctx, path, "client_to_host network replication must not declare pools");
                break;
            }
            char inputs_path[PATH_BUFFER_SIZE];
            format_path(inputs_path, sizeof(inputs_path), "%s.inputs", path);
            ok = validate_network_inputs(ctx, inputs, inputs_path, names);
        }
    }
    if (!ok)
    {
        name_table_destroy(&replication_names);
        return false;
    }

    yyjson_val *controls = obj_get(network, "control_messages");
    if (controls != NULL && !yyjson_is_arr(controls))
    {
        name_table_destroy(&replication_names);
        return validation_error(ctx, "$.network.control_messages", "network control_messages must be an array");
    }

    name_table control_names;
    SDL_zero(control_names);
    for (size_t i = 0; ok && yyjson_is_arr(controls) && i < yyjson_arr_size(controls); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.network.control_messages[%zu]", i);
        yyjson_val *control = yyjson_arr_get(controls, i);
        if (!yyjson_is_obj(control))
        {
            ok = validation_error(ctx, path, "network control message must be an object");
            break;
        }
        if (!require_unique_name(ctx, &control_names, "network control message", json_string(control, "name"), path))
        {
            ok = false;
            break;
        }
        if (!is_replication_direction(json_string(control, "direction"), true))
        {
            ok = validation_error(ctx, path,
                                  "network control message direction must be host_to_client, client_to_host, or "
                                  "bidirectional");
            break;
        }
        if (!require_ref(ctx, &names->signals, "signal", json_string(control, "signal"), path))
        {
            ok = false;
            break;
        }
    }
    if (ok)
        ok = validate_network_runtime_bindings(ctx, network, &replication_names, &control_names, names);
    if (ok)
        ok = validate_network_diagnostics(ctx, network, &replication_names);
    name_table_destroy(&control_names);
    name_table_destroy(&replication_names);
    return ok;
}

static bool validate_haptics_actor_filter(validation_context *ctx, yyjson_val *filter, const char *path,
                                          validation_names *names)
{
    if (!yyjson_is_obj(filter))
        return validation_error(ctx, path, "haptics payload actor filter must be an object");
    if (!is_non_empty_string(filter, "key"))
        return validation_error(ctx, path, "haptics payload actor filter requires a non-empty key");

    const char *actor = json_string(filter, "actor");
    yyjson_val *tags = obj_get(filter, "tags");
    if (actor == NULL && tags == NULL)
        return validation_error(ctx, path, "haptics payload actor filter requires actor or tags");
    if (actor != NULL && !require_ref(ctx, &names->entities, "entity", actor, path))
        return false;
    if (tags != NULL)
    {
        if (!yyjson_is_arr(tags) || yyjson_arr_size(tags) == 0)
            return validation_error(ctx, path, "haptics payload actor filter tags must be a non-empty array");
        for (size_t i = 0; i < yyjson_arr_size(tags); ++i)
        {
            yyjson_val *tag = yyjson_arr_get(tags, i);
            if (!yyjson_is_str(tag) || yyjson_get_len(tag) == 0)
                return validation_error(ctx, path, "haptics payload actor filter tags must be non-empty strings");
        }
    }

    char condition_path[PATH_BUFFER_SIZE];
    format_path(condition_path, sizeof(condition_path), "%s.active_if", path);
    return validate_data_condition(ctx, obj_get(filter, "active_if"), condition_path, names);
}

static bool validate_haptics(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *haptics = obj_get(root, "haptics");
    if (haptics == NULL)
        return true;
    if (!yyjson_is_obj(haptics))
        return validation_error(ctx, "$.haptics", "haptics must be an object");

    yyjson_val *policies = obj_get(haptics, "policies");
    if (policies == NULL)
        return true;
    if (!yyjson_is_arr(policies))
        return validation_error(ctx, "$.haptics.policies", "haptics policies must be an array");

    name_table policy_names;
    SDL_zero(policy_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(policies); ++i)
    {
        yyjson_val *policy = yyjson_arr_get(policies, i);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.haptics.policies[%zu]", i);
        if (!yyjson_is_obj(policy))
        {
            ok = validation_error(ctx, path, "haptics policy must be an object");
            break;
        }
        if (!require_unique_name(ctx, &policy_names, "haptics policy", json_string(policy, "name"), path) ||
            !require_ref(ctx, &names->signals, "signal", json_string(policy, "signal"), path))
        {
            ok = false;
            break;
        }

        yyjson_val *low = obj_get(policy, "low_frequency");
        yyjson_val *high = obj_get(policy, "high_frequency");
        yyjson_val *duration = obj_get(policy, "duration_ms");
        if (!yyjson_is_num(low) || yyjson_get_num(low) < 0.0 || yyjson_get_num(low) > 1.0)
        {
            ok = validation_error(ctx, path, "haptics low_frequency must be a number from 0 to 1");
            break;
        }
        if (!yyjson_is_num(high) || yyjson_get_num(high) < 0.0 || yyjson_get_num(high) > 1.0)
        {
            ok = validation_error(ctx, path, "haptics high_frequency must be a number from 0 to 1");
            break;
        }
        if (!yyjson_is_int(duration) || yyjson_get_sint(duration) <= 0)
        {
            ok = validation_error(ctx, path, "haptics duration_ms must be a positive integer");
            break;
        }

        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.enabled_if", path);
        if (!validate_data_condition(ctx, obj_get(policy, "enabled_if"), condition_path, names))
        {
            ok = false;
            break;
        }

        yyjson_val *filters = obj_get(policy, "payload_actor_filters");
        if (filters == NULL)
            continue;
        if (!yyjson_is_arr(filters) || yyjson_arr_size(filters) == 0)
        {
            ok = validation_error(ctx, path, "haptics payload_actor_filters must be a non-empty array");
            break;
        }
        for (size_t filter_index = 0; filter_index < yyjson_arr_size(filters); ++filter_index)
        {
            char filter_path[PATH_BUFFER_SIZE];
            format_path(filter_path, sizeof(filter_path), "%s.payload_actor_filters[%zu]", path, filter_index);
            if (!validate_haptics_actor_filter(ctx, yyjson_arr_get(filters, filter_index), filter_path, names))
            {
                ok = false;
                break;
            }
        }
    }

    name_table_destroy(&policy_names);
    return ok;
}

static void hash_network_actor_fields(sdl3d_crypto_hash32_state *state, yyjson_val *fields)
{
    network_hash_update_int(state, "field_count", (Sint64)yyjson_arr_size(fields));
    for (size_t i = 0; yyjson_is_arr(fields) && i < yyjson_arr_size(fields); ++i)
    {
        sdl3d_replication_field_descriptor descriptor;
        if (sdl3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, i), &descriptor))
        {
            network_hash_update(state, "field.path", descriptor.path);
            network_hash_update(state, "field.type", sdl3d_replication_field_type_name(descriptor.type));
        }
    }
}

static Sint64 network_actor_pool_capacity(yyjson_val *root, const char *pool_name)
{
    yyjson_val *pools = obj_get(root, "actor_pools");
    for (size_t i = 0; yyjson_is_arr(pools) && i < yyjson_arr_size(pools); ++i)
    {
        yyjson_val *pool = yyjson_arr_get(pools, i);
        if (SDL_strcmp(json_string(pool, "name"), pool_name != NULL ? pool_name : "") == 0)
            return yyjson_get_sint(obj_get(pool, "capacity"));
    }
    return 0;
}

bool sdl3d_game_data_network_schema_hash(yyjson_val *root, Uint8 out_hash[SDL3D_REPLICATION_SCHEMA_HASH_SIZE],
                                         bool *out_present)
{
    if (out_present != NULL)
        *out_present = false;
    if (out_hash != NULL)
        SDL_memset(out_hash, 0, SDL3D_REPLICATION_SCHEMA_HASH_SIZE);
    if (!yyjson_is_obj(root))
        return false;

    yyjson_val *network = obj_get(root, "network");
    if (network == NULL)
        return true;
    if (!yyjson_is_obj(network) || out_hash == NULL)
        return false;

    if (out_present != NULL)
        *out_present = true;

    sdl3d_crypto_hash32_state state;
    sdl3d_crypto_hash32_init(&state);
    network_hash_update(&state, "schema", "sdl3d.network.replication.v0");

    yyjson_val *protocol = obj_get(network, "protocol");
    network_hash_update(&state, "protocol.id", json_string(protocol, "id"));
    network_hash_update_int(&state, "protocol.version", yyjson_get_sint(obj_get(protocol, "version")));
    network_hash_update(&state, "protocol.transport", json_string(protocol, "transport"));
    network_hash_update_int(&state, "protocol.tick_rate", yyjson_get_sint(obj_get(protocol, "tick_rate")));

    yyjson_val *replication = obj_get(network, "replication");
    network_hash_update_int(&state, "replication_count", (Sint64)yyjson_arr_size(replication));
    for (size_t i = 0; yyjson_is_arr(replication) && i < yyjson_arr_size(replication); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(replication, i);
        network_hash_update(&state, "replication.name", json_string(entry, "name"));
        network_hash_update(&state, "replication.direction", json_string(entry, "direction"));
        network_hash_update_int(&state, "replication.rate", yyjson_get_sint(obj_get(entry, "rate")));

        yyjson_val *actors = obj_get(entry, "actors");
        network_hash_update_int(&state, "actor_count", (Sint64)yyjson_arr_size(actors));
        for (size_t a = 0; yyjson_is_arr(actors) && a < yyjson_arr_size(actors); ++a)
        {
            yyjson_val *actor = yyjson_arr_get(actors, a);
            network_hash_update(&state, "actor.entity", json_string(actor, "entity"));
            hash_network_actor_fields(&state, obj_get(actor, "fields"));
        }

        yyjson_val *pools = obj_get(entry, "pools");
        network_hash_update_int(&state, "pool_count", (Sint64)yyjson_arr_size(pools));
        for (size_t p = 0; yyjson_is_arr(pools) && p < yyjson_arr_size(pools); ++p)
        {
            yyjson_val *pool = yyjson_arr_get(pools, p);
            const char *pool_name = json_string(pool, "pool");
            network_hash_update(&state, "pool.name", pool_name);
            network_hash_update_int(&state, "pool.capacity", network_actor_pool_capacity(root, pool_name));
            hash_network_actor_fields(&state, obj_get(pool, "fields"));
        }

        yyjson_val *inputs = obj_get(entry, "inputs");
        network_hash_update_int(&state, "input_count", (Sint64)yyjson_arr_size(inputs));
        for (size_t input_index = 0; yyjson_is_arr(inputs) && input_index < yyjson_arr_size(inputs); ++input_index)
        {
            yyjson_val *input = yyjson_arr_get(inputs, input_index);
            network_hash_update(&state, "input.action", json_string(input, "action"));
        }
    }

    yyjson_val *controls = obj_get(network, "control_messages");
    network_hash_update_int(&state, "control_count", (Sint64)yyjson_arr_size(controls));
    for (size_t i = 0; yyjson_is_arr(controls) && i < yyjson_arr_size(controls); ++i)
    {
        yyjson_val *control = yyjson_arr_get(controls, i);
        network_hash_update(&state, "control.name", json_string(control, "name"));
        network_hash_update(&state, "control.direction", json_string(control, "direction"));
        network_hash_update(&state, "control.signal", json_string(control, "signal"));
    }

    sdl3d_crypto_hash32_final(&state, out_hash);
    return true;
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
        "adapter.controller", "collision.aabb", "collision.circle",   "control.axis_1d",
        "motion.oscillate",   "motion.spin",    "motion.velocity_2d", "particles.emitter",
        "property.decay",     "render.cube",    "render.sphere",
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

static bool collect_sprite_assets(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *sprites = obj_get(obj_get(root, "assets"), "sprites");
    if (sprites == NULL)
        return true;
    if (!yyjson_is_arr(sprites))
        return validation_error(ctx, "$.assets.sprites", "sprite assets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(sprites); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.sprites[%zu]", i);
        yyjson_val *sprite = yyjson_arr_get(sprites, i);
        if (!yyjson_is_obj(sprite))
            return validation_error(ctx, path, "sprite asset entries must be objects");
        if (!require_unique_name(ctx, &names->sprites, "sprite asset", json_string(sprite, "id"), path))
            return false;
        if (!is_non_empty_string(sprite, "path"))
            return validation_error(ctx, path, "sprite asset requires a non-empty path");
        if (!asset_path_exists(ctx, json_string(sprite, "path"), path, "sprite"))
            return false;
        const char *shader_vertex_path = json_string(sprite, "shader_vertex_path");
        const char *shader_fragment_path = json_string(sprite, "shader_fragment_path");
        if (shader_vertex_path != NULL && shader_vertex_path[0] != '\0' &&
            (shader_fragment_path == NULL || shader_fragment_path[0] == '\0'))
            return validation_error(ctx, path, "sprite shader_vertex_path requires shader_fragment_path");
        if (shader_vertex_path != NULL && shader_vertex_path[0] != '\0' &&
            !asset_path_exists(ctx, shader_vertex_path, path, "sprite shader"))
            return false;
        if (shader_fragment_path != NULL && shader_fragment_path[0] != '\0' &&
            !asset_path_exists(ctx, shader_fragment_path, path, "sprite shader"))
            return false;
        const char *effect = json_string(sprite, "effect");
        if (effect != NULL && effect[0] != '\0' && SDL_strcasecmp(effect, "melt") != 0)
            return validation_error(ctx, path, "unsupported sprite asset effect '%s'", effect);
        yyjson_val *effect_delay = obj_get(sprite, "effect_delay");
        yyjson_val *effect_duration = obj_get(sprite, "effect_duration");
        if (yyjson_is_num(effect_delay) && (float)yyjson_get_real(effect_delay) < 0.0f)
            return validation_error(ctx, path, "sprite asset effect_delay must be non-negative");
        if (yyjson_is_num(effect_duration) && (float)yyjson_get_real(effect_duration) <= 0.0f)
            return validation_error(ctx, path, "sprite asset effect_duration must be positive");
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
        const char *image_path = json_string(image, "path");
        const char *sprite_id = json_string(image, "sprite");
        if (!is_non_empty_string(image, "path") && !is_non_empty_string(image, "sprite"))
            return validation_error(ctx, path, "image asset requires path or sprite");

        if (sprite_id != NULL && !require_ref(ctx, &names->sprites, "sprite asset", sprite_id, path))
            return false;

        if (ctx->assets != NULL && image_path != NULL)
        {
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

static bool collect_input_assignment_sets(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *sets = obj_get(obj_get(root, "input"), "device_assignment_sets");
    if (sets == NULL)
        return true;
    if (!yyjson_is_arr(sets))
        return validation_error(ctx, "$.input.device_assignment_sets", "input device assignment sets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(sets); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        yyjson_val *set = yyjson_arr_get(sets, i);
        format_path(path, sizeof(path), "$.input.device_assignment_sets[%zu]", i);
        if (!yyjson_is_obj(set))
            return validation_error(ctx, path, "input device assignment sets must be objects");
        const char *name = json_string(set, "name");
        if (name != NULL && name[0] != '\0' &&
            !require_unique_name(ctx, &names->input_assignment_sets, "input device assignment set", name, path))
        {
            return false;
        }
    }
    return true;
}

static bool collect_input_profiles(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *profiles = obj_get(obj_get(root, "input"), "profiles");
    if (profiles == NULL)
        return true;
    if (!yyjson_is_arr(profiles))
        return validation_error(ctx, "$.input.profiles", "input profiles must be an array");

    for (size_t i = 0; i < yyjson_arr_size(profiles); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        yyjson_val *profile = yyjson_arr_get(profiles, i);
        format_path(path, sizeof(path), "$.input.profiles[%zu]", i);
        if (!yyjson_is_obj(profile))
            return validation_error(ctx, path, "input profiles must be objects");
        const char *name = json_string(profile, "name");
        if (name != NULL && name[0] != '\0' &&
            !require_unique_name(ctx, &names->input_profiles, "input profile", name, path))
        {
            return false;
        }
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
           collect_actor_archetypes(ctx, root, names) && collect_actor_pools(ctx, root, names) &&
           collect_scripts(ctx, root, names) && collect_adapters(ctx, root, names) &&
           collect_input_actions(ctx, root, names) && collect_input_assignment_sets(ctx, root, names) &&
           collect_input_profiles(ctx, root, names) && collect_network_input_channels(ctx, root, names) &&
           collect_cameras(ctx, root, names) && collect_fonts(ctx, root, names) &&
           collect_sprite_assets(ctx, root, names) && collect_images(ctx, root, names) &&
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
                    yyjson_val *slot = obj_get(binding, "slot");
                    if (slot != NULL && (!yyjson_is_num(slot) || yyjson_get_sint(slot) < -1 ||
                                         yyjson_get_sint(slot) >= SDL3D_INPUT_MAX_GAMEPADS))
                        return validation_error(ctx, path, "gamepad binding slot must be -1 or a valid slot index");
                }
                else if (SDL_strcmp(device != NULL ? device : "", "mouse") == 0)
                {
                    if (!is_non_empty_string(binding, "axis") && !is_non_empty_string(binding, "button"))
                        return validation_error(ctx, path, "mouse binding requires an axis or button");
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

static bool validate_input_profile_binding(validation_context *ctx, yyjson_val *binding, const char *path,
                                           validation_names *names)
{
    if (!yyjson_is_obj(binding))
        return validation_error(ctx, path, "input profile bindings must be objects");
    const char *action = json_string(binding, "action");
    const char *device = json_string(binding, "device");
    if (!require_ref(ctx, &names->actions, "input action", action, path))
        return false;

    if (SDL_strcmp(device != NULL ? device : "", "keyboard") == 0)
    {
        const char *key = json_string(binding, "key");
        if (!validation_key_name_valid(key))
            return validation_error(ctx, path, "keyboard input profile binding requires a valid key");
    }
    else if (SDL_strcmp(device != NULL ? device : "", "gamepad") == 0)
    {
        const char *axis = json_string(binding, "axis");
        const char *button = json_string(binding, "button");
        if (axis == NULL && button == NULL)
            return validation_error(ctx, path, "gamepad input profile binding requires an axis or button");
        if (axis != NULL && !validation_gamepad_axis_name_valid(axis))
            return validation_error(ctx, path, "gamepad input profile binding requires a valid axis");
        if (button != NULL && !validation_gamepad_button_name_valid(button))
            return validation_error(ctx, path, "gamepad input profile binding requires a valid button");
        yyjson_val *slot = obj_get(binding, "slot");
        if (slot != NULL &&
            (!yyjson_is_num(slot) || yyjson_get_sint(slot) < -1 || yyjson_get_sint(slot) >= SDL3D_INPUT_MAX_GAMEPADS))
        {
            return validation_error(ctx, path, "gamepad input profile binding slot must be -1 or a valid slot index");
        }
    }
    else if (SDL_strcmp(device != NULL ? device : "", "mouse") == 0)
    {
        const char *axis = json_string(binding, "axis");
        const char *button = json_string(binding, "button");
        if (axis == NULL && button == NULL)
            return validation_error(ctx, path, "mouse input profile binding requires an axis or button");
        if (axis != NULL && !validation_mouse_axis_name_valid(axis))
            return validation_error(ctx, path, "mouse input profile binding requires a valid axis");
        if (button != NULL && !validation_mouse_button_name_valid(button))
            return validation_error(ctx, path, "mouse input profile binding requires a valid button");
    }
    else
    {
        return validation_error(ctx, path, "unsupported input profile binding device '%s'",
                                device != NULL ? device : "<missing>");
    }
    return true;
}

yyjson_val *sdl3d_game_data_find_input_assignment_set_json(yyjson_val *root, const char *set_name)
{
    yyjson_val *sets = obj_get(obj_get(root, "input"), "device_assignment_sets");
    for (size_t i = 0; set_name != NULL && yyjson_is_arr(sets) && i < yyjson_arr_size(sets); ++i)
    {
        yyjson_val *set = yyjson_arr_get(sets, i);
        const char *name = json_string(set, "name");
        if (name != NULL && SDL_strcmp(name, set_name) == 0)
            return set;
    }
    return NULL;
}

static bool input_assignment_set_has_semantic(yyjson_val *set, const char *semantic)
{
    yyjson_val *bindings = obj_get(set, "bindings");
    for (size_t i = 0; semantic != NULL && yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        const char *binding_semantic = json_string(binding, "semantic");
        if (binding_semantic != NULL && SDL_strcmp(binding_semantic, semantic) == 0)
            return true;
    }
    return false;
}

static bool validate_input_assignment_set_binding(validation_context *ctx, yyjson_val *binding, const char *device,
                                                  const char *path)
{
    if (!yyjson_is_obj(binding))
        return validation_error(ctx, path, "input device assignment bindings must be objects");
    if (!is_non_empty_string(binding, "semantic"))
        return validation_error(ctx, path, "input device assignment binding requires a non-empty semantic");

    if (SDL_strcmp(device != NULL ? device : "", "keyboard") == 0)
    {
        if (!validation_key_name_valid(json_string(binding, "key")))
            return validation_error(ctx, path, "keyboard input device assignment binding requires a valid key");
    }
    else if (SDL_strcmp(device != NULL ? device : "", "gamepad") == 0)
    {
        const char *axis = json_string(binding, "axis");
        const char *button = json_string(binding, "button");
        if (axis == NULL && button == NULL)
            return validation_error(ctx, path, "gamepad input device assignment binding requires an axis or button");
        if (axis != NULL && !validation_gamepad_axis_name_valid(axis))
            return validation_error(ctx, path, "gamepad input device assignment binding requires a valid axis");
        if (button != NULL && !validation_gamepad_button_name_valid(button))
            return validation_error(ctx, path, "gamepad input device assignment binding requires a valid button");
    }
    else if (SDL_strcmp(device != NULL ? device : "", "mouse") == 0)
    {
        const char *axis = json_string(binding, "axis");
        const char *button = json_string(binding, "button");
        if (axis == NULL && button == NULL)
            return validation_error(ctx, path, "mouse input device assignment binding requires an axis or button");
        if (axis != NULL && !validation_mouse_axis_name_valid(axis))
            return validation_error(ctx, path, "mouse input device assignment binding requires a valid axis");
        if (button != NULL && !validation_mouse_button_name_valid(button))
            return validation_error(ctx, path, "mouse input device assignment binding requires a valid button");
    }
    else
    {
        return validation_error(ctx, path, "unsupported input device assignment device '%s'",
                                device != NULL ? device : "<missing>");
    }
    return true;
}

static bool validate_input_assignment_sets(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *sets = obj_get(obj_get(root, "input"), "device_assignment_sets");
    if (sets == NULL)
        return true;
    if (!yyjson_is_arr(sets))
        return validation_error(ctx, "$.input.device_assignment_sets", "input device assignment sets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(sets); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        yyjson_val *set = yyjson_arr_get(sets, i);
        format_path(path, sizeof(path), "$.input.device_assignment_sets[%zu]", i);
        if (!yyjson_is_obj(set))
            return validation_error(ctx, path, "input device assignment sets must be objects");
        const char *device = json_string(set, "device");
        if (!is_non_empty_string(set, "name"))
            return validation_error(ctx, path, "input device assignment set requires a non-empty name");
        if (!input_device_name_valid(device))
            return validation_error(ctx, path, "input device assignment set has unsupported device '%s'",
                                    device != NULL ? device : "<missing>");

        yyjson_val *bindings = obj_get(set, "bindings");
        if (!yyjson_is_arr(bindings) || yyjson_arr_size(bindings) == 0)
            return validation_error(ctx, path, "input device assignment set requires non-empty bindings");
        for (size_t b = 0; b < yyjson_arr_size(bindings); ++b)
        {
            char binding_path[PATH_BUFFER_SIZE];
            format_path(binding_path, sizeof(binding_path), "%s.bindings[%zu]", path, b);
            if (!validate_input_assignment_set_binding(ctx, yyjson_arr_get(bindings, b), device, binding_path))
                return false;
        }
    }
    return true;
}

static bool validate_input_profile_assignment(validation_context *ctx, yyjson_val *root, yyjson_val *assignment,
                                              const char *path, validation_names *names)
{
    if (!yyjson_is_obj(assignment))
        return validation_error(ctx, path, "input profile assignments must be objects");
    const char *set_name = json_string(assignment, "set");
    if (!require_ref(ctx, &names->input_assignment_sets, "input device assignment set", set_name, path))
        return false;

    yyjson_val *set = sdl3d_game_data_find_input_assignment_set_json(root, set_name);
    const char *device = json_string(set, "device");
    yyjson_val *slot = obj_get(assignment, "slot");
    if (slot != NULL && SDL_strcmp(device != NULL ? device : "", "gamepad") != 0)
    {
        return validation_error(ctx, path, "input profile assignment slot is only valid for gamepad assignment sets");
    }
    if (slot != NULL &&
        (!yyjson_is_num(slot) || yyjson_get_sint(slot) < -1 || yyjson_get_sint(slot) >= SDL3D_INPUT_MAX_GAMEPADS))
    {
        return validation_error(ctx, path, "input profile assignment slot must be -1 or a valid slot index");
    }

    yyjson_val *actions = obj_get(assignment, "actions");
    if (!yyjson_is_obj(actions))
        return validation_error(ctx, path, "input profile assignment requires an actions object");

    yyjson_val *bindings = obj_get(set, "bindings");
    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        char action_path[PATH_BUFFER_SIZE];
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        const char *semantic = json_string(binding, "semantic");
        const char *action = semantic != NULL ? json_string(actions, semantic) : NULL;
        format_path(action_path, sizeof(action_path), "%s.actions.%s", path, semantic != NULL ? semantic : "<missing>");
        if (!require_ref(ctx, &names->actions, "input action", action, action_path))
            return false;
    }
    size_t idx, max;
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_foreach(actions, idx, max, key, value)
    {
        char action_path[PATH_BUFFER_SIZE];
        const char *semantic = yyjson_is_str(key) ? yyjson_get_str(key) : NULL;
        const char *action = yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
        format_path(action_path, sizeof(action_path), "%s.actions.%s", path, semantic != NULL ? semantic : "<invalid>");
        if (!input_assignment_set_has_semantic(set, semantic))
            return validation_error(ctx, action_path, "input profile assignment maps unknown semantic '%s'",
                                    semantic != NULL ? semantic : "<invalid>");
        if (!require_ref(ctx, &names->actions, "input action", action, action_path))
            return false;
    }
    return true;
}

static bool validate_input_profiles(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *profiles = obj_get(obj_get(root, "input"), "profiles");
    if (profiles == NULL)
        return true;
    if (!yyjson_is_arr(profiles))
        return validation_error(ctx, "$.input.profiles", "input profiles must be an array");

    name_table profile_names = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(profiles); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        yyjson_val *profile = yyjson_arr_get(profiles, i);
        format_path(path, sizeof(path), "$.input.profiles[%zu]", i);
        if (!yyjson_is_obj(profile))
        {
            ok = validation_error(ctx, path, "input profiles must be objects");
            break;
        }
        ok = require_unique_name(ctx, &profile_names, "input profile", json_string(profile, "name"), path);

        yyjson_val *min_gamepads = obj_get(profile, "min_gamepads");
        yyjson_val *max_gamepads = obj_get(profile, "max_gamepads");
        if (ok && min_gamepads != NULL &&
            (!yyjson_is_num(min_gamepads) || yyjson_get_sint(min_gamepads) < 0 ||
             yyjson_get_sint(min_gamepads) > SDL3D_INPUT_MAX_GAMEPADS))
        {
            ok = validation_error(ctx, path, "input profile min_gamepads must be a valid gamepad count");
        }
        if (ok && max_gamepads != NULL &&
            (!yyjson_is_num(max_gamepads) || yyjson_get_sint(max_gamepads) < 0 ||
             yyjson_get_sint(max_gamepads) > SDL3D_INPUT_MAX_GAMEPADS))
        {
            ok = validation_error(ctx, path, "input profile max_gamepads must be a valid gamepad count");
        }
        if (ok && yyjson_is_num(min_gamepads) && yyjson_is_num(max_gamepads) &&
            yyjson_get_sint(min_gamepads) > yyjson_get_sint(max_gamepads))
        {
            ok = validation_error(ctx, path, "input profile min_gamepads cannot exceed max_gamepads");
        }
        if (ok && obj_get(profile, "active_if") != NULL)
        {
            char condition_path[PATH_BUFFER_SIZE];
            format_path(condition_path, sizeof(condition_path), "%s.active_if", path);
            ok = validate_data_condition(ctx, obj_get(profile, "active_if"), condition_path, names);
        }

        yyjson_val *unbind = obj_get(profile, "unbind");
        if (ok && unbind != NULL && !yyjson_is_arr(unbind))
            ok = validation_error(ctx, path, "input profile unbind must be an array");
        for (size_t u = 0; ok && yyjson_is_arr(unbind) && u < yyjson_arr_size(unbind); ++u)
        {
            char item_path[PATH_BUFFER_SIZE];
            format_path(item_path, sizeof(item_path), "%s.unbind[%zu]", path, u);
            ok =
                require_ref(ctx, &names->actions, "input action", yyjson_get_str(yyjson_arr_get(unbind, u)), item_path);
        }

        yyjson_val *bindings = obj_get(profile, "bindings");
        if (ok && bindings != NULL && !yyjson_is_arr(bindings))
            ok = validation_error(ctx, path, "input profile bindings must be an array");
        for (size_t b = 0; ok && yyjson_is_arr(bindings) && b < yyjson_arr_size(bindings); ++b)
        {
            char binding_path[PATH_BUFFER_SIZE];
            format_path(binding_path, sizeof(binding_path), "%s.bindings[%zu]", path, b);
            ok = validate_input_profile_binding(ctx, yyjson_arr_get(bindings, b), binding_path, names);
        }

        yyjson_val *assignments = obj_get(profile, "assignments");
        if (ok && bindings != NULL && assignments != NULL)
            ok = validation_error(ctx, path, "input profile cannot mix bindings and assignments");
        if (ok && assignments != NULL && !yyjson_is_arr(assignments))
            ok = validation_error(ctx, path, "input profile assignments must be an array");
        for (size_t a = 0; ok && yyjson_is_arr(assignments) && a < yyjson_arr_size(assignments); ++a)
        {
            char assignment_path[PATH_BUFFER_SIZE];
            format_path(assignment_path, sizeof(assignment_path), "%s.assignments[%zu]", path, a);
            ok = validate_input_profile_assignment(ctx, root, yyjson_arr_get(assignments, a), assignment_path, names);
        }
    }
    name_table_destroy(&profile_names);
    return ok;
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
            else if (SDL_strcmp(type, "motion.oscillate") == 0)
            {
                yyjson_val *origin = obj_get(component, "origin");
                yyjson_val *amplitude = obj_get(component, "amplitude");
                yyjson_val *rate = obj_get(component, "rate");
                yyjson_val *phase = obj_get(component, "phase");
                if (origin != NULL && !is_vec_array(origin, 3))
                    return validation_error(ctx, path, "motion.oscillate origin must be a vec3");
                if (amplitude != NULL && !is_vec_array(amplitude, 3))
                    return validation_error(ctx, path, "motion.oscillate amplitude must be a vec3");
                if (rate != NULL && !yyjson_is_num(rate))
                    return validation_error(ctx, path, "motion.oscillate rate must be a number");
                if (phase != NULL && !yyjson_is_num(phase))
                    return validation_error(ctx, path, "motion.oscillate phase must be a number");
            }
            else if (SDL_strcmp(type, "motion.spin") == 0)
            {
                yyjson_val *property = obj_get(component, "property");
                if (property != NULL && !is_non_empty_string(component, "property"))
                    return validation_error(ctx, path, "motion.spin property must be non-empty");
                yyjson_val *rate = obj_get(component, "rate");
                if (rate != NULL && !yyjson_is_num(rate))
                    return validation_error(ctx, path, "motion.spin rate must be a number");
            }
            else if (SDL_strcmp(type, "render.cube") == 0 || SDL_strcmp(type, "render.sphere") == 0)
            {
                yyjson_val *lighting = obj_get(component, "lighting");
                if (lighting != NULL && !yyjson_is_bool(lighting))
                    return validation_error(ctx, path, "render primitive lighting must be a boolean");
                if (SDL_strcmp(type, "render.sphere") == 0)
                {
                    yyjson_val *rotation_axis = obj_get(component, "rotation_axis");
                    if (rotation_axis != NULL && !is_vec_array(rotation_axis, 3))
                        return validation_error(ctx, path, "render.sphere rotation_axis must be a vec3");
                    yyjson_val *rotation_angle = obj_get(component, "rotation_angle");
                    if (rotation_angle != NULL && !yyjson_is_num(rotation_angle))
                        return validation_error(ctx, path, "render.sphere rotation_angle must be a number");
                    yyjson_val *rotation_property = obj_get(component, "rotation_property");
                    if (rotation_property != NULL && !is_non_empty_string(component, "rotation_property"))
                        return validation_error(ctx, path, "render.sphere rotation_property must be non-empty");
                    yyjson_val *texture_value = obj_get(component, "texture");
                    if (texture_value != NULL && !is_non_empty_string(component, "texture"))
                        return validation_error(ctx, path, "render.sphere texture must be a non-empty image asset id");
                    const char *texture = json_string(component, "texture");
                    if (texture != NULL && !require_ref(ctx, &names->images, "image asset", texture, path))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool validate_actor_archetypes_and_pools(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *archetypes = obj_get(root, "actor_archetypes");
    for (size_t i = 0; yyjson_is_arr(archetypes) && i < yyjson_arr_size(archetypes); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_archetypes[%zu]", i);
        yyjson_val *archetype = yyjson_arr_get(archetypes, i);
        yyjson_val *components = obj_get(archetype, "components");
        if (components != NULL && !yyjson_is_arr(components))
            return validation_error(ctx, path, "actor archetype components must be an array");
        for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
        {
            char component_path[PATH_BUFFER_SIZE];
            format_path(component_path, sizeof(component_path), "%s.components[%zu]", path, c);
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type");
            if (type == NULL || type[0] == '\0')
                return validation_error(ctx, component_path, "component requires a non-empty type");
            if (!is_supported_component_type(type) &&
                !validation_warning(ctx, component_path, "unsupported component type '%s'", type))
            {
                return false;
            }
        }
    }

    yyjson_val *pools = obj_get(root, "actor_pools");
    for (size_t i = 0; yyjson_is_arr(pools) && i < yyjson_arr_size(pools); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_pools[%zu]", i);
        yyjson_val *pool = yyjson_arr_get(pools, i);
        if (!require_ref(ctx, &names->actor_archetypes, "actor archetype", json_string(pool, "archetype"), path))
            return false;
        yyjson_val *capacity = obj_get(pool, "capacity");
        if (!yyjson_is_int(capacity) || yyjson_get_int(capacity) <= 0 || yyjson_get_int(capacity) > 4096)
            return validation_error(ctx, path, "actor pool capacity must be an integer in 1..4096");
        const char *scene = json_string(pool, "scene");
        yyjson_val *scenes = obj_get(pool, "scenes");
        if (scene != NULL && scenes != NULL)
            return validation_error(ctx, path, "actor pool must use either scene or scenes, not both");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, path))
            return false;
        if (scenes != NULL)
        {
            if (!yyjson_is_arr(scenes) || yyjson_arr_size(scenes) <= 0)
                return validation_error(ctx, path, "actor pool scenes must be a non-empty array");
            for (size_t scene_index = 0; scene_index < yyjson_arr_size(scenes); ++scene_index)
            {
                char scene_path[PATH_BUFFER_SIZE];
                format_path(scene_path, sizeof(scene_path), "$.actor_pools[%zu].scenes[%zu]", i, scene_index);
                yyjson_val *scene_value = yyjson_arr_get(scenes, scene_index);
                if (!yyjson_is_str(scene_value) || yyjson_get_str(scene_value)[0] == '\0')
                    return validation_error(ctx, scene_path, "actor pool scenes entries must be non-empty strings");
                if (!require_ref(ctx, &names->scenes, "scene", yyjson_get_str(scene_value), scene_path))
                    return false;
            }
        }
        const char *policy = json_string(pool, "on_exhausted");
        if (policy != NULL && SDL_strcmp(policy, "fail") != 0 && SDL_strcmp(policy, "reuse_oldest") != 0)
            return validation_error(ctx, path, "actor pool on_exhausted must be fail or reuse_oldest");
        const char *scene_policy = json_string(pool, "on_scene_exit");
        if (scene_policy != NULL && SDL_strcmp(scene_policy, "reset") != 0 &&
            SDL_strcmp(scene_policy, "despawn") != 0 && SDL_strcmp(scene_policy, "preserve") != 0)
        {
            return validation_error(ctx, path, "actor pool on_scene_exit must be reset, despawn, or preserve");
        }
    }
    return true;
}

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
    if (SDL_strcmp(type, "actor.spawn") == 0)
    {
        if (!require_ref(ctx, &names->actor_pools, "actor pool", json_string(action, "pool"), json_path))
            return false;
        const char *from = json_string(action, "from");
        if (from != NULL && from[0] == '\0')
            return validation_error(ctx, json_path, "actor.spawn from requires a non-empty actor reference");
        if (from != NULL && !name_table_contains(&names->entities, from) &&
            !name_table_contains(&names->actor_pool_actors, from))
        {
            return validation_error(ctx, json_path, "unknown actor.spawn from actor reference '%s'", from);
        }
        yyjson_val *properties = obj_get(action, "properties");
        if (properties != NULL && !yyjson_is_obj(properties))
            return validation_error(ctx, json_path, "actor.spawn properties must be an object");
        return true;
    }
    if (SDL_strcmp(type, "actor.despawn") == 0)
    {
        const char *target = json_string(action, "target");
        if (target == NULL || target[0] == '\0')
            return validation_error(ctx, json_path, "actor.despawn requires a non-empty target");
        if (!name_table_contains(&names->entities, target) && !name_table_contains(&names->actor_pool_actors, target))
            return validation_error(ctx, json_path, "unknown actor.despawn target '%s'", target);
        return true;
    }
    if (SDL_strcmp(type, "actor.despawn_by_tag") == 0)
    {
        if (!is_non_empty_string(action, "tag"))
            return validation_error(ctx, json_path, "actor.despawn_by_tag requires a non-empty tag");
        return true;
    }
    if (SDL_strcmp(type, "input.reset_bindings") == 0)
    {
        if (!is_non_empty_string(action, "menu"))
            return validation_error(ctx, json_path, "input.reset_bindings requires a non-empty menu");
        return true;
    }
    if (SDL_strcmp(type, "input.apply_profile") == 0)
    {
        const char *profile = json_string(action, "profile");
        return require_ref(ctx, &names->input_profiles, "input profile", profile, json_path);
    }
    if (SDL_strcmp(type, "input.apply_active_profile") == 0)
    {
        if (names->input_profiles.count <= 0)
            return validation_error(ctx, json_path, "input.apply_active_profile requires at least one input profile");
        return true;
    }
    if (SDL_strcmp(type, "input.clear_network_input_overrides") == 0)
    {
        return require_ref(ctx, &names->network_input_channels, "network input channel", json_string(action, "channel"),
                           json_path);
    }
    if (SDL_strcmp(type, "scene_state.set") == 0)
    {
        if (!is_non_empty_string(action, "key"))
            return validation_error(ctx, json_path, "scene_state.set requires a non-empty key");
        yyjson_val *value = obj_get(action, "value");
        if (value == NULL || !(yyjson_is_bool(value) || yyjson_is_num(value) || yyjson_is_str(value)))
            return validation_error(ctx, json_path, "scene_state.set requires a scalar value");
        return true;
    }
    if (SDL_strcmp(type, "network.direct_connect.start") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "network.direct_connect.start requires a non-empty name");
        if (!is_non_empty_string(action, "host_key") && !is_non_empty_string(action, "host") &&
            !is_non_empty_string(action, "default_host"))
            return validation_error(ctx, json_path,
                                    "network.direct_connect.start requires host_key, host, or default_host");
        if (!is_non_empty_string(action, "port_key") && obj_get(action, "port") == NULL &&
            obj_get(action, "default_port") == NULL)
            return validation_error(ctx, json_path,
                                    "network.direct_connect.start requires port_key, port, or default_port");
        if (!validate_network_port_value(ctx, obj_get(action, "port"), json_path, "network.direct_connect.start port"))
            return false;
        yyjson_val *default_port = obj_get(action, "default_port");
        if (default_port != NULL &&
            (!yyjson_is_int(default_port) || yyjson_get_int(default_port) <= 0 || yyjson_get_int(default_port) > 65535))
            return validation_error(ctx, json_path,
                                    "network.direct_connect.start default_port must be integer 1..65535");
        return true;
    }
    if (SDL_strcmp(type, "network.direct_connect.cancel") == 0 ||
        SDL_strcmp(type, "network.direct_connect.observe") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "%s requires a non-empty name", type);
        return true;
    }
    if (SDL_strcmp(type, "network.host.start") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "network.host.start requires a non-empty name");
        if (!validate_network_port_value(ctx, obj_get(action, "port"), json_path, "network.host.start port"))
            return false;
        yyjson_val *default_port = obj_get(action, "default_port");
        if (default_port != NULL &&
            (!yyjson_is_int(default_port) || yyjson_get_int(default_port) <= 0 || yyjson_get_int(default_port) > 65535))
            return validation_error(ctx, json_path, "network.host.start default_port must be integer 1..65535");
        return true;
    }
    if (SDL_strcmp(type, "network.host.cancel") == 0 || SDL_strcmp(type, "network.host.observe") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "%s requires a non-empty name", type);
        return true;
    }
    if (SDL_strcmp(type, "network.discovery.start") == 0 || SDL_strcmp(type, "network.discovery.refresh") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "%s requires a non-empty name", type);
        if (!is_non_empty_string(action, "collection"))
            return validation_error(ctx, json_path, "%s requires a non-empty collection", type);
        if (!validate_network_port_value(ctx, obj_get(action, "port"), json_path, "network.discovery port"))
            return false;
        yyjson_val *default_port = obj_get(action, "default_port");
        if (default_port != NULL &&
            (!yyjson_is_int(default_port) || yyjson_get_int(default_port) <= 0 || yyjson_get_int(default_port) > 65535))
            return validation_error(ctx, json_path, "network.discovery default_port must be integer 1..65535");
        yyjson_val *local_port = obj_get(action, "local_port");
        if (local_port != NULL &&
            (!yyjson_is_int(local_port) || yyjson_get_int(local_port) < 0 || yyjson_get_int(local_port) > 65535))
            return validation_error(ctx, json_path, "network.discovery local_port must be integer 0..65535");
        return true;
    }
    if (SDL_strcmp(type, "network.discovery.observe") == 0 || SDL_strcmp(type, "network.discovery.cancel") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "%s requires a non-empty name", type);
        if (!is_non_empty_string(action, "collection"))
            return validation_error(ctx, json_path, "%s requires a non-empty collection", type);
        return true;
    }
    if (SDL_strcmp(type, "network.discovery.connect_selected") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "network.discovery.connect_selected requires a non-empty name");
        if (!is_non_empty_string(action, "collection"))
            return validation_error(ctx, json_path,
                                    "network.discovery.connect_selected requires a non-empty collection");
        if (!is_non_empty_string(action, "selected_index_key") && obj_get(action, "selected_index") == NULL)
            return validation_error(ctx, json_path,
                                    "network.discovery.connect_selected requires selected_index_key or selected_index");
        yyjson_val *selected_index = obj_get(action, "selected_index");
        if (selected_index != NULL && (!yyjson_is_int(selected_index) || yyjson_get_int(selected_index) < 0))
            return validation_error(ctx, json_path,
                                    "network.discovery.connect_selected selected_index must be an integer >= 0");
        if (!is_non_empty_string(action, "direct_connect_name"))
            return validation_error(ctx, json_path,
                                    "network.discovery.connect_selected requires a non-empty direct_connect_name");
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
    if (SDL_strcmp(type, "persistence.load") == 0 || SDL_strcmp(type, "persistence.save") == 0)
    {
        const char *entry = json_string(action, "entry");
        if (entry == NULL)
            entry = json_string(action, "name");
        return require_ref(ctx, &names->persistence, "persistence entry", entry, json_path);
    }
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
    {
        if (!require_ref(ctx, &names->scenes, "scene", json_string(action, "scene"), json_path))
            return false;
        yyjson_val *payload = obj_get(action, "payload");
        if (payload != NULL)
        {
            if (!yyjson_is_obj(payload))
                return validation_error(ctx, json_path, "scene.set payload must be an object");
            yyjson_val *key;
            yyjson_obj_iter iter;
            yyjson_obj_iter_init(payload, &iter);
            while ((key = yyjson_obj_iter_next(&iter)) != NULL)
            {
                const char *name = yyjson_get_str(key);
                yyjson_val *value = yyjson_obj_iter_get_val(key);
                if (name == NULL || name[0] == '\0')
                    return validation_error(ctx, json_path, "scene.set payload keys must be non-empty");
                if (!(yyjson_is_bool(value) || yyjson_is_num(value) || yyjson_is_str(value)))
                    return validation_error(ctx, json_path, "scene.set payload values must be scalar");
            }
        }
        return true;
    }
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
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.if", json_path);
        if (!validate_data_condition(ctx, condition, condition_path, names))
            return false;
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
        if (SDL_strcmp(type, "sensor.contact_2d") == 0)
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
            if (!require_ref(ctx, &names->signals, "signal", json_string(sensor, "on_enter"), path))
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

static bool validate_update_phases(validation_context *ctx, yyjson_val *phases, const char *json_path,
                                   validation_names *names)
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
    if (SDL_strcmp(type != NULL ? type : "", "scene_state.compare") == 0)
    {
        if (!is_non_empty_string(condition, "key"))
            return validation_error(ctx, path, "scene_state.compare condition requires a non-empty key");
        if (!is_compare_op(json_string(condition, "op")))
            return validation_error(ctx, path,
                                    "scene_state.compare condition requires a supported comparison operator");
        if (obj_get(condition, "value") == NULL)
            return validation_error(ctx, path, "scene_state.compare condition requires a value");
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
                         SDL_strcmp(type != NULL ? type : "", "drift") != 0 &&
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
        yyjson_val *target_entities = obj_get(light, "target_entities");
        if (target_entities != NULL && !yyjson_is_arr(target_entities))
            return validation_error(ctx, path, "light target_entities must be an array");
        for (size_t target_index = 0; yyjson_is_arr(target_entities) && target_index < yyjson_arr_size(target_entities);
             ++target_index)
        {
            yyjson_val *target = yyjson_arr_get(target_entities, target_index);
            if (!yyjson_is_str(target))
                return validation_error(ctx, path, "light target_entities entries must be entity names");
            if (!require_ref(ctx, &names->entities, "entity", yyjson_get_str(target), path))
                return false;
        }

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
            else if (SDL_strcmp(type != NULL ? type : "", "color_cycle") == 0)
            {
                yyjson_val *colors = obj_get(effect, "colors");
                if (!yyjson_is_arr(colors) || yyjson_arr_size(colors) < 2)
                    return validation_error(ctx, effect_path, "light color_cycle effect requires at least two colors");
                for (size_t color_index = 0; color_index < yyjson_arr_size(colors); ++color_index)
                {
                    if (!is_vec_array(yyjson_arr_get(colors, color_index), 3))
                        return validation_error(ctx, effect_path, "light color_cycle colors must be vec3 arrays");
                }
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
                   "sdl3d.scene.v0") != 0)
    {
        yyjson_doc_free(doc);
        return validation_error(ctx, json_path, "generated scene must use schema sdl3d.scene.v0");
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
            count += SDL3D_STANDARD_OPTIONS_SCENE_COUNT;
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
            sdl3d_standard_options_scene_docs generated;
            char package_error[256];
            if (!sdl3d_standard_options_build_scene_docs(root, package, &generated, package_error,
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
                    sdl3d_standard_options_scene_docs_free(&generated);
                    validation_scene_docs_destroy(docs, count);
                    return false;
                }
                doc_count++;
            }
            sdl3d_standard_options_scene_docs_free(&generated);
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
    return validate_storage(ctx, root) && validate_persistence(ctx, root, names) &&
           validate_input_bindings(ctx, root) && validate_input_assignment_sets(ctx, root) &&
           validate_input_profiles(ctx, root, names) && validate_components(ctx, root, names) &&
           validate_update_phases(ctx, obj_get(root, "update_phases"), "$.update_phases", names) &&
           validate_transitions(ctx, root, names) && validate_scenes(ctx, root, names) &&
           validate_actor_archetypes_and_pools(ctx, root, names) && validate_network(ctx, root, names) &&
           validate_app_refs(ctx, root, names) && validate_cameras(ctx, root, names) && validate_ui(ctx, root, names) &&
           validate_presentation(ctx, root, names) && validate_render_effects(ctx, root, names) &&
           validate_lights(ctx, root, names) && validate_haptics(ctx, root, names) &&
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
    name_table_destroy(&names->sprites);
    name_table_destroy(&names->sounds);
    name_table_destroy(&names->music);
    name_table_destroy(&names->scenes);
    name_table_destroy(&names->sensors);
    name_table_destroy(&names->persistence);
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

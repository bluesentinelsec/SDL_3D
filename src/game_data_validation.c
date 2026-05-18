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
#include "network_replication_schema.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/door.h"
#include "slayer3d/input.h"
#include "slayer3d/level.h"
#include "slayer3d/lighting.h"
#include "slayer3d/sprite_actor.h"
#include "slayer3d_crypto.h"

#define GAME_DATA_MENU_TEXT_MAX_BYTES 255
#define GAME_DATA_IMPORT_MAX_DEPTH 16

typedef struct game_data_source_map_entry
{
    char *composed_path;
    char *source_path;
    char *source_json_path;
} game_data_source_map_entry;

struct slayer3d_game_data_source_map
{
    game_data_source_map_entry *entries;
    int count;
    int capacity;
};

typedef struct import_validation_stack
{
    const char *paths[GAME_DATA_IMPORT_MAX_DEPTH];
    int count;
} import_validation_stack;

bool validate_data_condition(validation_context *ctx, yyjson_val *condition, const char *path, validation_names *names);
static bool validate_storage(validation_context *ctx, yyjson_val *root);
static bool require_network_string_entry(validation_context *ctx, yyjson_val *map, const char *path, const char *label,
                                         const char *name);
static bool validate_target_filter_fields(validation_context *ctx, yyjson_val *json, const char *json_path,
                                          const char *type);
static bool validate_imports_with_stack(validation_context *ctx, yyjson_val *root, import_validation_stack *stack);
static bool validate_imports(validation_context *ctx, yyjson_val *root);
static bool compose_document_into(validation_context *ctx, yyjson_val *root, yyjson_val *sections,
                                  const char *json_path, import_validation_stack *stack, yyjson_mut_doc *doc,
                                  yyjson_mut_val *target, bool is_root);

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
        SDL_strcmp(name, "BACKSPACE") == 0 || SDL_strcmp(name, "DELETE") == 0 || SDL_strcmp(name, "COMMA") == 0 ||
        SDL_strcmp(name, "PERIOD") == 0 || SDL_strcmp(name, "LEFTBRACKET") == 0 ||
        SDL_strcmp(name, "RIGHTBRACKET") == 0)
    {
        return true;
    }
    if (SDL_strlen(name) == 1)
        return SDL_GetScancodeFromKey(SDL_GetKeyFromName(name), NULL) != SDL_SCANCODE_UNKNOWN;
    return SDL_GetScancodeFromName(name) != SDL_SCANCODE_UNKNOWN;
}

static bool validation_input_modifier_name_valid(const char *name)
{
    return name != NULL && (SDL_strcasecmp(name, "shift") == 0 || SDL_strcasecmp(name, "ctrl") == 0 ||
                            SDL_strcasecmp(name, "control") == 0 || SDL_strcasecmp(name, "alt") == 0 ||
                            SDL_strcasecmp(name, "option") == 0 || SDL_strcasecmp(name, "gui") == 0 ||
                            SDL_strcasecmp(name, "cmd") == 0 || SDL_strcasecmp(name, "command") == 0 ||
                            SDL_strcasecmp(name, "meta") == 0);
}

static int validation_input_modifier_mask(const char *name)
{
    if (name == NULL)
        return -1;
    if (SDL_strcasecmp(name, "shift") == 0)
        return SLAYER3D_INPUT_MOD_SHIFT;
    if (SDL_strcasecmp(name, "ctrl") == 0 || SDL_strcasecmp(name, "control") == 0)
        return SLAYER3D_INPUT_MOD_CTRL;
    if (SDL_strcasecmp(name, "alt") == 0 || SDL_strcasecmp(name, "option") == 0)
        return SLAYER3D_INPUT_MOD_ALT;
    if (SDL_strcasecmp(name, "gui") == 0 || SDL_strcasecmp(name, "cmd") == 0 || SDL_strcasecmp(name, "command") == 0 ||
        SDL_strcasecmp(name, "meta") == 0)
    {
        return SLAYER3D_INPUT_MOD_GUI;
    }
    return -1;
}

static bool validate_keyboard_modifier_mask(validation_context *ctx, yyjson_val *value, const char *path,
                                            const char *field, int *out_mask)
{
    int mask = SLAYER3D_INPUT_MOD_NONE;
    if (value == NULL)
    {
        if (out_mask != NULL)
            *out_mask = mask;
        return true;
    }

    if (yyjson_is_str(value))
    {
        const char *modifier = yyjson_get_str(value);
        if (!validation_input_modifier_name_valid(modifier))
            return validation_error(ctx, path, "%s contains unsupported modifier '%s'", field,
                                    modifier != NULL ? modifier : "<missing>");
        mask |= validation_input_modifier_mask(modifier);
    }
    else if (yyjson_is_arr(value))
    {
        for (size_t i = 0; i < yyjson_arr_size(value); ++i)
        {
            yyjson_val *entry = yyjson_arr_get(value, i);
            if (!yyjson_is_str(entry))
                return validation_error(ctx, path, "%s entries must be modifier strings", field);
            const char *modifier = yyjson_get_str(entry);
            if (!validation_input_modifier_name_valid(modifier))
                return validation_error(ctx, path, "%s contains unsupported modifier '%s'", field,
                                        modifier != NULL ? modifier : "<missing>");
            mask |= validation_input_modifier_mask(modifier);
        }
    }
    else
    {
        return validation_error(ctx, path, "%s must be a modifier string or array", field);
    }

    if (out_mask != NULL)
        *out_mask = mask;
    return true;
}

static bool validate_keyboard_modifiers(validation_context *ctx, yyjson_val *binding, const char *path)
{
    yyjson_val *modifiers = obj_get(binding, "modifiers");
    yyjson_val *required = obj_get(binding, "required_modifiers");
    yyjson_val *excluded = obj_get(binding, "excluded_modifiers");
    int required_mask = SLAYER3D_INPUT_MOD_NONE;
    int excluded_mask = SLAYER3D_INPUT_MOD_NONE;

    if (modifiers != NULL && required != NULL)
        return validation_error(ctx, path, "keyboard binding must not define both modifiers and required_modifiers");
    if (!validate_keyboard_modifier_mask(ctx, modifiers != NULL ? modifiers : required, path,
                                         "keyboard binding required_modifiers", &required_mask) ||
        !validate_keyboard_modifier_mask(ctx, excluded, path, "keyboard binding excluded_modifiers", &excluded_mask))
    {
        return false;
    }
    if ((required_mask & excluded_mask) != 0)
        return validation_error(ctx, path,
                                "keyboard binding required_modifiers and excluded_modifiers must not overlap");
    return true;
}

bool validation_mouse_button_name_valid(const char *name)
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

static bool source_path_prefix_matches(const char *entry_path, const char *json_path)
{
    if (entry_path == NULL || json_path == NULL)
        return false;
    const size_t length = SDL_strlen(entry_path);
    if (SDL_strncmp(entry_path, json_path, length) != 0)
        return false;
    return json_path[length] == '\0' || json_path[length] == '.' || json_path[length] == '[';
}

static const game_data_source_map_entry *source_map_find_best(const slayer3d_game_data_source_map *map,
                                                              const char *json_path)
{
    const game_data_source_map_entry *best = NULL;
    size_t best_length = 0U;
    for (int i = 0; map != NULL && i < map->count; ++i)
    {
        const game_data_source_map_entry *entry = &map->entries[i];
        if (!source_path_prefix_matches(entry->composed_path, json_path))
            continue;
        const size_t length = SDL_strlen(entry->composed_path);
        if (length > best_length)
        {
            best = entry;
            best_length = length;
        }
    }
    return best;
}

static void resolve_source_location(const validation_context *ctx, const char *json_path, char *source_buffer,
                                    size_t source_buffer_size, char *path_buffer, size_t path_buffer_size)
{
    const char *path = json_path != NULL ? json_path : "$";
    const char *source = ctx != NULL && ctx->source_path != NULL ? ctx->source_path : "<game-data>";
    if (ctx != NULL)
    {
        const game_data_source_map_entry *entry = source_map_find_best(ctx->source_map, path);
        if (entry != NULL)
        {
            source = entry->source_path != NULL ? entry->source_path : source;
            const size_t prefix_length = SDL_strlen(entry->composed_path);
            const char *suffix = path + prefix_length;
            SDL_snprintf(path_buffer, path_buffer_size, "%s%s",
                         entry->source_json_path != NULL ? entry->source_json_path : "$", suffix);
            SDL_snprintf(source_buffer, source_buffer_size, "%s", source);
            return;
        }
    }
    SDL_snprintf(source_buffer, source_buffer_size, "%s", source);
    SDL_snprintf(path_buffer, path_buffer_size, "%s", path);
}

static void set_first_error(validation_context *ctx, const char *json_path, const char *message)
{
    if (ctx->error_buffer == NULL || ctx->error_buffer_size <= 0 || ctx->error_buffer[0] != '\0')
    {
        return;
    }

    char source[PATH_BUFFER_SIZE];
    char path[PATH_BUFFER_SIZE];
    resolve_source_location(ctx, json_path, source, sizeof(source), path, sizeof(path));
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

static bool validation_warning(validation_context *ctx, const char *json_path, const char *format, ...)
{
    char message[384];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    return emit_diagnostic(ctx, SLAYER3D_GAME_DATA_DIAGNOSTIC_WARNING, json_path, "%s", message);
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

static void network_hash_update(slayer3d_crypto_hash32_state *state, const char *label, const char *value)
{
    static const char sep = '\0';
    static const char null_marker = '\1';
    slayer3d_crypto_hash32_update(state, label, SDL_strlen(label));
    slayer3d_crypto_hash32_update(state, &sep, 1u);
    if (value != NULL)
    {
        slayer3d_crypto_hash32_update(state, value, SDL_strlen(value));
    }
    else
    {
        slayer3d_crypto_hash32_update(state, &null_marker, 1u);
    }
    slayer3d_crypto_hash32_update(state, &sep, 1u);
}

static void network_hash_update_int(slayer3d_crypto_hash32_state *state, const char *label, Sint64 value)
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
        slayer3d_replication_field_descriptor descriptor;
        if (!slayer3d_replication_field_descriptor_from_json(field, &descriptor))
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
        if (slayer3d_replication_field_wire_size(descriptor.type) == 0U)
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
static bool editor_command_name_valid(const char *value);
static bool editor_command_target_name_valid(const char *value);

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

static void hash_network_actor_fields(slayer3d_crypto_hash32_state *state, yyjson_val *fields)
{
    network_hash_update_int(state, "field_count", (Sint64)yyjson_arr_size(fields));
    for (size_t i = 0; yyjson_is_arr(fields) && i < yyjson_arr_size(fields); ++i)
    {
        slayer3d_replication_field_descriptor descriptor;
        if (slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, i), &descriptor))
        {
            network_hash_update(state, "field.path", descriptor.path);
            network_hash_update(state, "field.type", slayer3d_replication_field_type_name(descriptor.type));
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

bool slayer3d_game_data_network_schema_hash(yyjson_val *root, Uint8 out_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE],
                                            bool *out_present)
{
    if (out_present != NULL)
        *out_present = false;
    if (out_hash != NULL)
        SDL_memset(out_hash, 0, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE);
    if (!yyjson_is_obj(root))
        return false;

    yyjson_val *network = obj_get(root, "network");
    if (network == NULL)
        return true;
    if (!yyjson_is_obj(network) || out_hash == NULL)
        return false;

    if (out_present != NULL)
        *out_present = true;

    slayer3d_crypto_hash32_state state;
    slayer3d_crypto_hash32_init(&state);
    network_hash_update(&state, "schema", "slayer3d.network.replication.v0");

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

    slayer3d_crypto_hash32_final(&state, out_hash);
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

static const char *import_path_compare_start(const char *path)
{
    while (path != NULL && path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
        path += 2;
    return path != NULL ? path : "";
}

static char *import_path_join(const char *base_dir, const char *path)
{
    if (path == NULL)
        return NULL;
    if (base_dir == NULL || base_dir[0] == '\0' || SDL_strcmp(base_dir, ".") == 0)
        return SDL_strdup(path);
    return path_join(base_dir, path);
}

static bool import_stack_contains(const import_validation_stack *stack, const char *path)
{
    const char *target = import_path_compare_start(asset_path_without_scheme(path));
    for (int i = 0; stack != NULL && i < stack->count; ++i)
    {
        const char *existing = import_path_compare_start(asset_path_without_scheme(stack->paths[i]));
        if (SDL_strcmp(existing, target) == 0)
            return true;
    }
    return false;
}

static bool import_path_is_safe_relative(const char *path)
{
    if (path == NULL || path[0] == '\0' || path_is_absolute(path) || SDL_strstr(path, "://") != NULL)
        return false;
    if (SDL_strchr(path, '\\') != NULL || SDL_strchr(path, ':') != NULL)
        return false;

    const char *segment = path;
    while (*segment != '\0')
    {
        const char *end = SDL_strchr(segment, '/');
        const size_t length = end != NULL ? (size_t)(end - segment) : SDL_strlen(segment);
        if (length == 0U)
            return false;
        if ((length == 1U && segment[0] == '.') || (length == 2U && segment[0] == '.' && segment[1] == '.'))
        {
            return false;
        }
        if (end == NULL)
            break;
        segment = end + 1;
    }
    return true;
}

static bool import_section_name_allowed(const char *name)
{
    static const char *const allowed[] = {"storage",
                                          "persistence",
                                          "profiles",
                                          "assets",
                                          "scripts",
                                          "input",
                                          "render",
                                          "transitions",
                                          "ui",
                                          "editor",
                                          "factions",
                                          "entities",
                                          "grid_maps",
                                          "grid_pickup_layers",
                                          "sector_levels",
                                          "sector_level_fragments",
                                          "brush_worlds",
                                          "editor_player_starts",
                                          "sector_navigation",
                                          "sector_doors",
                                          "sector_platforms",
                                          "actor_archetypes",
                                          "actor_instances",
                                          "actor_pools",
                                          "signals",
                                          "logic",
                                          "adapters",
                                          "network",
                                          "haptics",
                                          "presentation",
                                          "update_phases"};
    for (size_t i = 0; name != NULL && i < SDL_arraysize(allowed); ++i)
    {
        if (SDL_strcmp(name, allowed[i]) == 0)
            return true;
    }
    return false;
}

static bool import_fragment_key_allowed(const char *name)
{
    return SDL_strcmp(name != NULL ? name : "", "schema") == 0 ||
           SDL_strcmp(name != NULL ? name : "", "imports") == 0 || import_section_name_allowed(name);
}

static bool import_sections_contains(yyjson_val *sections, const char *name)
{
    if (!yyjson_is_arr(sections))
        return true;
    for (size_t i = 0; i < yyjson_arr_size(sections); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(sections, i);
        if (yyjson_is_str(entry) && SDL_strcmp(yyjson_get_str(entry), name) == 0)
            return true;
    }
    return false;
}

static bool validate_import_sections(validation_context *ctx, yyjson_val *sections, yyjson_val *fragment_root,
                                     const char *json_path)
{
    if (sections == NULL)
        return true;
    if (!yyjson_is_arr(sections) || yyjson_arr_size(sections) == 0)
        return validation_error(ctx, json_path, "import sections must be a non-empty array when present");

    for (size_t i = 0; i < yyjson_arr_size(sections); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.sections[%zu]", json_path, i);
        yyjson_val *entry = yyjson_arr_get(sections, i);
        if (!yyjson_is_str(entry) || yyjson_get_str(entry)[0] == '\0')
            return validation_error(ctx, path, "import section must be a non-empty string");
        const char *section = yyjson_get_str(entry);
        if (!import_section_name_allowed(section))
            return validation_error(ctx, path, "import section '%s' is not mergeable", section);
        for (size_t prior = 0; prior < i; ++prior)
        {
            yyjson_val *prior_entry = yyjson_arr_get(sections, prior);
            if (yyjson_is_str(prior_entry) && SDL_strcmp(yyjson_get_str(prior_entry), section) == 0)
                return validation_error(ctx, path, "duplicate import section '%s'", section);
        }
        if (obj_get(fragment_root, section) == NULL)
            return validation_error(ctx, path, "import section '%s' is not present in fragment", section);
    }
    return true;
}

static bool validate_fragment_keys(validation_context *ctx, yyjson_val *fragment_root, yyjson_val *sections)
{
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(fragment_root, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        value = yyjson_obj_iter_get_val(key);
        (void)value;
        const char *name = yyjson_get_str(key);
        if (!import_fragment_key_allowed(name))
            return validation_error(ctx, "$", "fragment contains root-only or unsupported section '%s'", name);
        if (import_section_name_allowed(name) && !import_sections_contains(sections, name))
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.%s", name);
            return validation_error(ctx, path, "fragment section is not selected by the import filter");
        }
    }
    return true;
}

static bool validate_import_document(validation_context *parent_ctx, const char *asset_path, yyjson_val *sections,
                                     const char *json_path, import_validation_stack *stack)
{
    if (parent_ctx->assets == NULL)
        return validation_error(parent_ctx, json_path, "imports require an asset resolver");
    if (stack->count >= GAME_DATA_IMPORT_MAX_DEPTH)
        return validation_error(parent_ctx, json_path, "import depth exceeds %d", GAME_DATA_IMPORT_MAX_DEPTH);
    if (import_stack_contains(stack, asset_path))
        return validation_error(parent_ctx, json_path, "import cycle detected for '%s'", asset_path);

    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(parent_ctx->assets, asset_path, &buffer, asset_error,
                                           (int)sizeof(asset_error)))
    {
        return validation_error(parent_ctx, json_path, "import fragment '%s' does not exist or cannot be read",
                                asset_path);
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    slayer3d_asset_buffer_free(&buffer);
    if (doc == NULL)
    {
        return validation_error(parent_ctx, json_path, "import yyjson error %u at byte %llu: %s", err.code,
                                (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
    }

    yyjson_val *fragment_root = yyjson_doc_get_root(doc);
    char *base_dir = path_dirname(asset_path_without_scheme(asset_path));
    validation_context child_ctx = *parent_ctx;
    child_ctx.source_path = asset_path;
    child_ctx.base_dir = base_dir;

    bool ok = true;
    if (!yyjson_is_obj(fragment_root))
    {
        ok = validation_error(&child_ctx, "$", "fragment root must be an object");
    }
    else if (SDL_strcmp(json_string(fragment_root, "schema") != NULL ? json_string(fragment_root, "schema") : "",
                        "slayer3d.fragment.v0") != 0)
    {
        ok = validation_error(&child_ctx, "$.schema", "import fragment must use schema slayer3d.fragment.v0");
    }
    else
    {
        stack->paths[stack->count++] = asset_path;
        ok = validate_import_sections(parent_ctx, sections, fragment_root, json_path) &&
             validate_fragment_keys(&child_ctx, fragment_root, sections) &&
             validate_imports_with_stack(&child_ctx, fragment_root, stack);
        stack->count--;
    }

    SDL_free(base_dir);
    yyjson_doc_free(doc);
    return ok;
}

static bool validate_import_entry(validation_context *ctx, yyjson_val *entry, size_t index,
                                  import_validation_stack *stack)
{
    char path[PATH_BUFFER_SIZE];
    format_path(path, sizeof(path), "$.imports[%zu]", index);
    if (!yyjson_is_obj(entry))
        return validation_error(ctx, path, "import entries must be objects");

    const char *import_path = json_string(entry, "path");
    char path_field[PATH_BUFFER_SIZE];
    format_path(path_field, sizeof(path_field), "%s.path", path);
    if (!import_path_is_safe_relative(import_path))
        return validation_error(ctx, path_field, "import path must be a safe relative path");

    yyjson_val *sections = obj_get(entry, "sections");
    if (sections != NULL && !yyjson_is_arr(sections))
        return validation_error(ctx, path, "import sections must be an array");

    char *resolved = import_path_join(ctx->base_dir, import_path);
    if (resolved == NULL)
        return validation_error(ctx, path_field, "failed to resolve import path '%s'", import_path);

    const bool ok = validate_import_document(ctx, resolved, sections, path, stack);
    SDL_free(resolved);
    return ok;
}

static bool validate_imports_with_stack(validation_context *ctx, yyjson_val *root, import_validation_stack *stack)
{
    yyjson_val *imports = obj_get(root, "imports");
    if (imports == NULL)
        return true;
    if (!yyjson_is_arr(imports))
        return validation_error(ctx, "$.imports", "imports must be an array");
    for (size_t i = 0; i < yyjson_arr_size(imports); ++i)
    {
        if (!validate_import_entry(ctx, yyjson_arr_get(imports, i), i, stack))
            return false;
    }
    return true;
}

static bool validate_imports(validation_context *ctx, yyjson_val *root)
{
    import_validation_stack stack;
    SDL_zero(stack);
    stack.paths[stack.count++] = ctx->source_path != NULL ? asset_path_without_scheme(ctx->source_path) : "<root>";
    return validate_imports_with_stack(ctx, root, &stack);
}

static bool source_map_reserve(slayer3d_game_data_source_map *map, int required)
{
    if (map == NULL)
        return false;
    if (required <= map->capacity)
        return true;
    int next_capacity = map->capacity < 16 ? 16 : map->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;
    game_data_source_map_entry *entries =
        (game_data_source_map_entry *)SDL_realloc(map->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;
    SDL_memset(entries + map->capacity, 0, (size_t)(next_capacity - map->capacity) * sizeof(*entries));
    map->entries = entries;
    map->capacity = next_capacity;
    return true;
}

static bool source_map_add(validation_context *ctx, const char *composed_path, const char *source_json_path)
{
    if (ctx == NULL || ctx->source_map == NULL || composed_path == NULL || source_json_path == NULL)
        return true;
    slayer3d_game_data_source_map *map = (slayer3d_game_data_source_map *)ctx->source_map;
    if (!source_map_reserve(map, map->count + 1))
        return validation_error(ctx, composed_path, "failed to allocate import source map");
    game_data_source_map_entry *entry = &map->entries[map->count];
    entry->composed_path = SDL_strdup(composed_path);
    entry->source_path = SDL_strdup(ctx->source_path != NULL ? ctx->source_path : "<game-data>");
    entry->source_json_path = SDL_strdup(source_json_path);
    if (entry->composed_path == NULL || entry->source_path == NULL || entry->source_json_path == NULL)
        return validation_error(ctx, composed_path, "failed to allocate import source map entry");
    map->count++;
    return true;
}

void slayer3d_game_data_source_map_destroy(slayer3d_game_data_source_map *map)
{
    if (map == NULL)
        return;
    for (int i = 0; i < map->count; ++i)
    {
        SDL_free(map->entries[i].composed_path);
        SDL_free(map->entries[i].source_path);
        SDL_free(map->entries[i].source_json_path);
    }
    SDL_free(map->entries);
    SDL_free(map);
}

static bool compose_merge_value(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_object,
                                const char *key, yyjson_val *source_value, const char *target_path,
                                const char *source_path);

static bool compose_merge_object_body(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_object,
                                      yyjson_val *source_object, const char *target_path, const char *source_path)
{
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(source_object, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        value = yyjson_obj_iter_get_val(key);
        const char *name = yyjson_get_str(key);
        char target_child_path[PATH_BUFFER_SIZE];
        char source_child_path[PATH_BUFFER_SIZE];
        format_path(target_child_path, sizeof(target_child_path), "%s.%s", target_path, name);
        format_path(source_child_path, sizeof(source_child_path), "%s.%s", source_path, name);
        if (!compose_merge_value(ctx, doc, target_object, name, value, target_child_path, source_child_path))
            return false;
    }
    return true;
}

static bool compose_append_array(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_array,
                                 yyjson_val *source_array, const char *target_path, const char *source_path)
{
    for (size_t i = 0; i < yyjson_arr_size(source_array); ++i)
    {
        const size_t target_index = yyjson_mut_arr_size(target_array);
        yyjson_mut_val *copy = yyjson_val_mut_copy(doc, yyjson_arr_get(source_array, i));
        if (copy == NULL || !yyjson_mut_arr_append(target_array, copy))
            return validation_error(ctx, target_path, "failed to append imported array item");
        char target_item_path[PATH_BUFFER_SIZE];
        char source_item_path[PATH_BUFFER_SIZE];
        format_path(target_item_path, sizeof(target_item_path), "%s[%zu]", target_path, target_index);
        format_path(source_item_path, sizeof(source_item_path), "%s[%zu]", source_path, i);
        if (!source_map_add(ctx, target_item_path, source_item_path))
            return false;
    }
    return true;
}

static bool compose_merge_existing(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_value,
                                   yyjson_val *source_value, const char *target_path, const char *source_path)
{
    if (yyjson_mut_is_arr(target_value) && yyjson_is_arr(source_value))
        return compose_append_array(ctx, doc, target_value, source_value, target_path, source_path);
    if (yyjson_mut_is_obj(target_value) && yyjson_is_obj(source_value))
        return compose_merge_object_body(ctx, doc, target_value, source_value, target_path, source_path);
    return validation_error(ctx, target_path, "import merge conflict for '%s'", target_path);
}

static bool compose_merge_value(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_object,
                                const char *key, yyjson_val *source_value, const char *target_path,
                                const char *source_path)
{
    yyjson_mut_val *existing = yyjson_mut_obj_get(target_object, key);
    if (existing != NULL)
        return compose_merge_existing(ctx, doc, existing, source_value, target_path, source_path);

    yyjson_mut_val *key_copy = yyjson_mut_strcpy(doc, key);
    if (key_copy == NULL)
        return validation_error(ctx, target_path, "failed to copy imported key");
    if (yyjson_is_arr(source_value))
    {
        yyjson_mut_val *array = yyjson_mut_arr(doc);
        if (array == NULL || !yyjson_mut_obj_add(target_object, key_copy, array) ||
            !source_map_add(ctx, target_path, source_path))
            return validation_error(ctx, target_path, "failed to copy imported array");
        return compose_append_array(ctx, doc, array, source_value, target_path, source_path);
    }
    if (yyjson_is_obj(source_value))
    {
        yyjson_mut_val *object = yyjson_mut_obj(doc);
        if (object == NULL || !yyjson_mut_obj_add(target_object, key_copy, object) ||
            !source_map_add(ctx, target_path, source_path))
            return validation_error(ctx, target_path, "failed to copy imported object");
        return compose_merge_object_body(ctx, doc, object, source_value, target_path, source_path);
    }

    yyjson_mut_val *copy = yyjson_val_mut_copy(doc, source_value);
    if (copy == NULL || !yyjson_mut_obj_add(target_object, key_copy, copy) ||
        !source_map_add(ctx, target_path, source_path))
        return validation_error(ctx, target_path, "failed to copy imported value");
    return true;
}

static bool compose_local_sections(validation_context *ctx, yyjson_val *root, yyjson_val *sections, yyjson_mut_doc *doc,
                                   yyjson_mut_val *target, bool is_root)
{
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        value = yyjson_obj_iter_get_val(key);
        const char *name = yyjson_get_str(key);
        if (SDL_strcmp(name, "imports") == 0)
            continue;
        if (!is_root && SDL_strcmp(name, "schema") == 0)
            continue;
        if (!is_root && !import_section_name_allowed(name))
            continue;
        if (!is_root && !import_sections_contains(sections, name))
            continue;

        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.%s", name);
        if (!compose_merge_value(ctx, doc, target, name, value, path, path))
            return false;
    }
    return true;
}

static bool compose_import_entry(validation_context *ctx, yyjson_val *entry, size_t index,
                                 import_validation_stack *stack, yyjson_mut_doc *doc, yyjson_mut_val *target)
{
    char path[PATH_BUFFER_SIZE];
    format_path(path, sizeof(path), "$.imports[%zu]", index);
    if (!yyjson_is_obj(entry))
        return validation_error(ctx, path, "import entries must be objects");

    const char *import_path = json_string(entry, "path");
    char path_field[PATH_BUFFER_SIZE];
    format_path(path_field, sizeof(path_field), "%s.path", path);
    if (!import_path_is_safe_relative(import_path))
        return validation_error(ctx, path_field, "import path must be a safe relative path");

    yyjson_val *sections = obj_get(entry, "sections");
    if (sections != NULL && !yyjson_is_arr(sections))
        return validation_error(ctx, path, "import sections must be an array");

    char *resolved = import_path_join(ctx->base_dir, import_path);
    if (resolved == NULL)
        return validation_error(ctx, path_field, "failed to resolve import path '%s'", import_path);
    if (ctx->assets == NULL)
    {
        SDL_free(resolved);
        return validation_error(ctx, path, "imports require an asset resolver");
    }
    if (stack->count >= GAME_DATA_IMPORT_MAX_DEPTH)
    {
        SDL_free(resolved);
        return validation_error(ctx, path, "import depth exceeds %d", GAME_DATA_IMPORT_MAX_DEPTH);
    }
    if (import_stack_contains(stack, resolved))
    {
        const bool ok = validation_error(ctx, path, "import cycle detected for '%s'", resolved);
        SDL_free(resolved);
        return ok;
    }

    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(ctx->assets, resolved, &buffer, asset_error, (int)sizeof(asset_error)))
    {
        const bool ok = validation_error(ctx, path, "import fragment '%s' does not exist or cannot be read", resolved);
        SDL_free(resolved);
        return ok;
    }

    yyjson_read_err err;
    yyjson_doc *fragment_doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    slayer3d_asset_buffer_free(&buffer);
    if (fragment_doc == NULL)
    {
        const bool ok = validation_error(ctx, path, "import yyjson error %u at byte %llu: %s", err.code,
                                         (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
        SDL_free(resolved);
        return ok;
    }

    char *base_dir = path_dirname(asset_path_without_scheme(resolved));
    validation_context child_ctx = *ctx;
    child_ctx.source_path = resolved;
    child_ctx.base_dir = base_dir;

    stack->paths[stack->count++] = resolved;
    const bool ok =
        compose_document_into(&child_ctx, yyjson_doc_get_root(fragment_doc), sections, path, stack, doc, target, false);
    stack->count--;

    SDL_free(base_dir);
    yyjson_doc_free(fragment_doc);
    SDL_free(resolved);
    return ok;
}

static bool compose_imports_into(validation_context *ctx, yyjson_val *root, import_validation_stack *stack,
                                 yyjson_mut_doc *doc, yyjson_mut_val *target)
{
    yyjson_val *imports = obj_get(root, "imports");
    if (imports == NULL)
        return true;
    if (!yyjson_is_arr(imports))
        return validation_error(ctx, "$.imports", "imports must be an array");
    for (size_t i = 0; i < yyjson_arr_size(imports); ++i)
    {
        if (!compose_import_entry(ctx, yyjson_arr_get(imports, i), i, stack, doc, target))
            return false;
    }
    return true;
}

static bool compose_document_into(validation_context *ctx, yyjson_val *root, yyjson_val *sections,
                                  const char *json_path, import_validation_stack *stack, yyjson_mut_doc *doc,
                                  yyjson_mut_val *target, bool is_root)
{
    if (!yyjson_is_obj(root))
        return validation_error(ctx, "$", is_root ? "root must be an object" : "fragment root must be an object");

    const char *schema = json_string(root, "schema");
    if (is_root)
    {
        if (SDL_strcmp(schema != NULL ? schema : "", "slayer3d.game.v0") != 0)
            return validation_error(ctx, "$.schema", "unsupported or missing game data schema");
    }
    else
    {
        if (SDL_strcmp(schema != NULL ? schema : "", "slayer3d.fragment.v0") != 0)
            return validation_error(ctx, "$.schema", "import fragment must use schema slayer3d.fragment.v0");
        if (!validate_import_sections(ctx, sections, root, json_path) || !validate_fragment_keys(ctx, root, sections))
            return false;
    }

    return compose_imports_into(ctx, root, stack, doc, target) &&
           compose_local_sections(ctx, root, sections, doc, target, is_root);
}

static const char *compose_mut_string(yyjson_mut_val *object, const char *key)
{
    yyjson_mut_val *value = yyjson_mut_obj_get(object, key);
    return yyjson_mut_is_str(value) ? yyjson_mut_get_str(value) : NULL;
}

static yyjson_mut_val *compose_find_actor_archetype(yyjson_mut_val *root, const char *name)
{
    yyjson_mut_val *archetypes = yyjson_mut_obj_get(root, "actor_archetypes");
    for (size_t i = 0; name != NULL && yyjson_mut_is_arr(archetypes) && i < yyjson_mut_arr_size(archetypes); ++i)
    {
        yyjson_mut_val *archetype = yyjson_mut_arr_get(archetypes, i);
        const char *archetype_name = compose_mut_string(archetype, "name");
        if (archetype_name != NULL && SDL_strcmp(archetype_name, name) == 0)
            return archetype;
    }
    return NULL;
}

static bool compose_merge_mut_override(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target,
                                       yyjson_mut_val *source, const char *path)
{
    yyjson_mut_obj_iter iter;
    yyjson_mut_obj_iter_init(source, &iter);
    yyjson_mut_val *key;
    while ((key = yyjson_mut_obj_iter_next(&iter)) != NULL)
    {
        yyjson_mut_val *value = yyjson_mut_obj_iter_get_val(key);
        const char *name = yyjson_mut_get_str(key);
        if (name == NULL || SDL_strcmp(name, "archetype") == 0)
            continue;

        yyjson_mut_val *existing = yyjson_mut_obj_get(target, name);
        if (yyjson_mut_is_obj(existing) && yyjson_mut_is_obj(value))
        {
            char child_path[PATH_BUFFER_SIZE];
            format_path(child_path, sizeof(child_path), "%s.%s", path, name);
            if (!compose_merge_mut_override(ctx, doc, existing, value, child_path))
                return false;
            continue;
        }

        yyjson_mut_val *key_copy = yyjson_mut_strcpy(doc, name);
        yyjson_mut_val *value_copy = yyjson_mut_val_mut_copy(doc, value);
        if (key_copy == NULL || value_copy == NULL || !yyjson_mut_obj_put(target, key_copy, value_copy))
            return validation_error(ctx, path, "failed to apply actor instance override '%s'", name);
    }
    return true;
}

static yyjson_mut_val *compose_ensure_entities_array(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    yyjson_mut_val *entities = yyjson_mut_obj_get(root, "entities");
    if (entities == NULL)
    {
        entities = yyjson_mut_arr(doc);
        yyjson_mut_val *key = yyjson_mut_strcpy(doc, "entities");
        if (entities == NULL || key == NULL || !yyjson_mut_obj_add(root, key, entities))
        {
            (void)validation_error(ctx, "$.entities", "failed to allocate generated entities array");
            return NULL;
        }
    }
    if (!yyjson_mut_is_arr(entities))
    {
        (void)validation_error(ctx, "$.entities", "entities must be an array");
        return NULL;
    }
    return entities;
}

static bool compose_expand_actor_instances(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    yyjson_mut_val *instances = yyjson_mut_obj_get(root, "actor_instances");
    if (instances == NULL)
        return true;
    if (!yyjson_mut_is_arr(instances))
        return validation_error(ctx, "$.actor_instances", "actor_instances must be an array");

    yyjson_mut_val *entities = compose_ensure_entities_array(ctx, doc, root);
    if (entities == NULL)
        return false;

    for (size_t i = 0; i < yyjson_mut_arr_size(instances); ++i)
    {
        yyjson_mut_val *instance = yyjson_mut_arr_get(instances, i);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_instances[%zu]", i);
        if (!yyjson_mut_is_obj(instance))
            return validation_error(ctx, path, "actor instance entries must be objects");

        const char *name = compose_mut_string(instance, "name");
        const char *archetype_name = compose_mut_string(instance, "archetype");
        if (name == NULL || name[0] == '\0')
            return validation_error(ctx, path, "actor instance requires a non-empty name");
        if (archetype_name == NULL || archetype_name[0] == '\0')
            return validation_error(ctx, path, "actor instance requires a non-empty archetype");

        yyjson_mut_val *archetype = compose_find_actor_archetype(root, archetype_name);
        if (archetype == NULL)
            return validation_error(ctx, path, "actor instance references unknown actor archetype '%s'",
                                    archetype_name);

        yyjson_mut_val *entity = yyjson_mut_val_mut_copy(doc, archetype);
        if (entity == NULL || !yyjson_mut_is_obj(entity))
            return validation_error(ctx, path, "failed to copy actor archetype '%s'", archetype_name);
        if (!compose_merge_mut_override(ctx, doc, entity, instance, path))
            return false;

        const size_t target_index = yyjson_mut_arr_size(entities);
        if (!yyjson_mut_arr_append(entities, entity))
            return validation_error(ctx, path, "failed to append generated actor instance");

        char target_path[PATH_BUFFER_SIZE];
        format_path(target_path, sizeof(target_path), "$.entities[%zu]", target_index);
        if (!source_map_add(ctx, target_path, path))
            return false;
    }
    return true;
}

static yyjson_mut_val *compose_find_sector_level(yyjson_mut_val *root, const char *name)
{
    yyjson_mut_val *levels = yyjson_mut_obj_get(root, "sector_levels");
    for (size_t i = 0; name != NULL && yyjson_mut_is_arr(levels) && i < yyjson_mut_arr_size(levels); ++i)
    {
        yyjson_mut_val *level = yyjson_mut_arr_get(levels, i);
        const char *level_name = compose_mut_string(level, "name");
        if (level_name != NULL && SDL_strcmp(level_name, name) == 0)
            return level;
    }
    return NULL;
}

static yyjson_mut_val *compose_ensure_sector_levels_array(validation_context *ctx, yyjson_mut_doc *doc,
                                                          yyjson_mut_val *root)
{
    yyjson_mut_val *levels = yyjson_mut_obj_get(root, "sector_levels");
    if (levels == NULL)
    {
        levels = yyjson_mut_arr(doc);
        yyjson_mut_val *key = yyjson_mut_strcpy(doc, "sector_levels");
        if (levels == NULL || key == NULL || !yyjson_mut_obj_add(root, key, levels))
        {
            (void)validation_error(ctx, "$.sector_levels", "failed to allocate generated sector_levels array");
            return NULL;
        }
    }
    if (!yyjson_mut_is_arr(levels))
    {
        (void)validation_error(ctx, "$.sector_levels", "sector_levels must be an array");
        return NULL;
    }
    return levels;
}

static yyjson_mut_val *compose_create_sector_level(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *root,
                                                   const char *name)
{
    yyjson_mut_val *levels = compose_ensure_sector_levels_array(ctx, doc, root);
    yyjson_mut_val *level = yyjson_mut_obj(doc);
    yyjson_mut_val *name_key = yyjson_mut_strcpy(doc, "name");
    yyjson_mut_val *name_value = yyjson_mut_strcpy(doc, name);
    if (levels == NULL || level == NULL || name_key == NULL || name_value == NULL ||
        !yyjson_mut_obj_add(level, name_key, name_value) || !yyjson_mut_arr_append(levels, level))
    {
        (void)validation_error(ctx, "$.sector_levels", "failed to allocate generated sector level '%s'", name);
        return NULL;
    }
    return level;
}

static yyjson_mut_val *compose_ensure_mut_array_property(validation_context *ctx, yyjson_mut_doc *doc,
                                                         yyjson_mut_val *object, const char *key, const char *path)
{
    yyjson_mut_val *array = yyjson_mut_obj_get(object, key);
    if (array == NULL)
    {
        array = yyjson_mut_arr(doc);
        yyjson_mut_val *key_value = yyjson_mut_strcpy(doc, key);
        if (array == NULL || key_value == NULL || !yyjson_mut_obj_add(object, key_value, array))
        {
            (void)validation_error(ctx, path, "failed to allocate generated array '%s'", key);
            return NULL;
        }
    }
    if (!yyjson_mut_is_arr(array))
    {
        (void)validation_error(ctx, path, "generated array '%s' conflicts with a non-array value", key);
        return NULL;
    }
    return array;
}

static bool compose_append_mut_array_items(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_array,
                                           yyjson_mut_val *source_array, const char *path)
{
    if (source_array == NULL)
        return true;
    if (!yyjson_mut_is_arr(source_array))
        return validation_error(ctx, path, "sector level fragment arrays must be arrays");
    for (size_t i = 0; i < yyjson_mut_arr_size(source_array); ++i)
    {
        yyjson_mut_val *copy = yyjson_mut_val_mut_copy(doc, yyjson_mut_arr_get(source_array, i));
        if (copy == NULL || !yyjson_mut_arr_append(target_array, copy))
            return validation_error(ctx, path, "failed to append sector level fragment item");
    }
    return true;
}

static bool compose_expand_sector_level_fragments(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    yyjson_mut_val *fragments = yyjson_mut_obj_get(root, "sector_level_fragments");
    if (fragments == NULL)
        return true;
    if (!yyjson_mut_is_arr(fragments))
        return validation_error(ctx, "$.sector_level_fragments", "sector_level_fragments must be an array");

    for (size_t i = 0; i < yyjson_mut_arr_size(fragments); ++i)
    {
        yyjson_mut_val *fragment = yyjson_mut_arr_get(fragments, i);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_level_fragments[%zu]", i);
        if (!yyjson_mut_is_obj(fragment))
            return validation_error(ctx, path, "sector level fragment entries must be objects");

        const char *level_name = compose_mut_string(fragment, "level");
        if (level_name == NULL || level_name[0] == '\0')
            return validation_error(ctx, path, "sector level fragment requires a non-empty level");

        yyjson_mut_val *level = compose_find_sector_level(root, level_name);
        if (level == NULL)
            level = compose_create_sector_level(ctx, doc, root, level_name);
        if (level == NULL)
            return false;

        static const char *const array_keys[] = {"materials", "sectors", "lights"};
        for (size_t key_index = 0; key_index < SDL_arraysize(array_keys); ++key_index)
        {
            const char *key = array_keys[key_index];
            yyjson_mut_val *source = yyjson_mut_obj_get(fragment, key);
            if (source == NULL)
                continue;
            char child_path[PATH_BUFFER_SIZE];
            format_path(child_path, sizeof(child_path), "%s.%s", path, key);
            yyjson_mut_val *target = compose_ensure_mut_array_property(ctx, doc, level, key, child_path);
            if (target == NULL || !compose_append_mut_array_items(ctx, doc, target, source, child_path))
                return false;
        }
    }
    return true;
}

yyjson_doc *slayer3d_game_data_compose_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                             slayer3d_game_data_source_map **out_source_map, char *error_buffer,
                                             int error_buffer_size)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        error_buffer[0] = '\0';
    if (out_source_map != NULL)
        *out_source_map = NULL;
    if (assets == NULL || asset_path == NULL || asset_path[0] == '\0')
    {
        validation_context ctx = {
            .source_path = asset_path != NULL ? asset_path : "<game-data>",
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        (void)validation_error(&ctx, "$", "invalid game data composition arguments");
        return NULL;
    }

    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(assets, asset_path, &buffer, asset_error, (int)sizeof(asset_error)))
    {
        validation_context ctx = {
            .source_path = asset_path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        (void)validation_error(&ctx, "$", "failed to read game data asset: %s", asset_error);
        return NULL;
    }

    yyjson_read_err err;
    yyjson_doc *source_doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    slayer3d_asset_buffer_free(&buffer);
    if (source_doc == NULL)
    {
        validation_context ctx = {
            .source_path = asset_path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        (void)validation_error(&ctx, "$", "yyjson error %u at byte %llu: %s", err.code, (unsigned long long)err.pos,
                               err.msg != NULL ? err.msg : "");
        return NULL;
    }

    char *base_dir = path_dirname(asset_path_without_scheme(asset_path));
    yyjson_mut_doc *mut_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *mut_root = mut_doc != NULL ? yyjson_mut_obj(mut_doc) : NULL;
    slayer3d_game_data_source_map *source_map =
        out_source_map != NULL ? (slayer3d_game_data_source_map *)SDL_calloc(1, sizeof(*source_map)) : NULL;
    if (base_dir == NULL || mut_doc == NULL || mut_root == NULL || (out_source_map != NULL && source_map == NULL))
    {
        slayer3d_game_data_source_map_destroy(source_map);
        yyjson_mut_doc_free(mut_doc);
        yyjson_doc_free(source_doc);
        SDL_free(base_dir);
        validation_context ctx = {
            .source_path = asset_path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        (void)validation_error(&ctx, "$", "failed to allocate composed game data document");
        return NULL;
    }

    yyjson_mut_doc_set_root(mut_doc, mut_root);
    validation_context ctx = {
        .source_path = asset_path,
        .base_dir = base_dir,
        .assets = assets,
        .source_map = source_map,
        .error_buffer = error_buffer,
        .error_buffer_size = error_buffer_size,
    };
    import_validation_stack stack;
    SDL_zero(stack);
    stack.paths[stack.count++] = asset_path_without_scheme(asset_path);

    yyjson_doc *composed_doc = NULL;
    if (compose_document_into(&ctx, yyjson_doc_get_root(source_doc), NULL, "$", &stack, mut_doc, mut_root, true) &&
        compose_expand_sector_level_fragments(&ctx, mut_doc, mut_root) &&
        compose_expand_actor_instances(&ctx, mut_doc, mut_root))
    {
        composed_doc = yyjson_mut_doc_imut_copy(mut_doc, NULL);
    }
    if (composed_doc == NULL && (error_buffer == NULL || error_buffer_size <= 0 || error_buffer[0] == '\0'))
        (void)validation_error(&ctx, "$", "failed to compose game data document");

    if (composed_doc != NULL && out_source_map != NULL)
    {
        *out_source_map = source_map;
        source_map = NULL;
    }
    slayer3d_game_data_source_map_destroy(source_map);
    yyjson_mut_doc_free(mut_doc);
    yyjson_doc_free(source_doc);
    SDL_free(base_dir);
    return composed_doc;
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

static bool is_compare_op(const char *op)
{
    return op != NULL && (SDL_strcmp(op, ">=") == 0 || SDL_strcmp(op, ">") == 0 || SDL_strcmp(op, "<=") == 0 ||
                          SDL_strcmp(op, "<") == 0 || SDL_strcmp(op, "==") == 0 || SDL_strcmp(op, "!=") == 0);
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

static bool is_supported_component_type(const char *type)
{
    const char *known[] = {
        "adapter.controller",
        "collision.aabb",
        "collision.circle",
        "combat.health",
        "control.axis_1d",
        "controller.editor_camera",
        "controller.fps_brush",
        "controller.fps_sector",
        "lifecycle.ttl",
        "light.directional",
        "light.point",
        "light.spot",
        "motion.brush_velocity_3d",
        "motion.grid_agent",
        "motion.oscillate",
        "motion.patrol",
        "motion.scroll_wrap",
        "motion.sector_velocity_3d",
        "motion.spin",
        "motion.velocity_2d",
        "motion.velocity_3d",
        "interactable",
        "particles.emitter",
        "pickup.respawn",
        "property.decay",
        "render.composite",
        "render.cube",
        "render.mesh_primitive",
        "render.model",
        "render.sphere",
        "render.sprite",
        "status_effect.timer",
        "viewmodel.bob",
        "weapon.projectile",
        "weapon.state",
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

static bool validate_non_empty_string_field(validation_context *ctx, yyjson_val *json, const char *json_path,
                                            const char *type, const char *field);
static bool validate_optional_signal_field(validation_context *ctx, yyjson_val *json, const char *json_path,
                                           validation_names *names, const char *field);

static bool brush_velocity_shape_valid(const char *shape)
{
    return shape == NULL || SDL_strcmp(shape, "point") == 0 || SDL_strcmp(shape, "sphere") == 0 ||
           SDL_strcmp(shape, "aabb") == 0;
}

static bool validate_brush_velocity_component(validation_context *ctx, yyjson_val *component, const char *path,
                                              validation_names *names)
{
    const char *shape = json_string(component, "shape");
    if (!brush_velocity_shape_valid(shape))
        return validation_error(ctx, path, "motion.brush_velocity_3d shape must be point, sphere, or aabb");

    const char *string_fields[] = {"property",
                                   "extents_property",
                                   "reason",
                                   "last_impact_brush_property",
                                   "last_impact_world_property",
                                   "last_impact_material_property",
                                   "last_impact_position_property",
                                   "last_impact_normal_property",
                                   "last_impact_contents_property",
                                   "last_impact_surface_flags_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, component, path, "motion.brush_velocity_3d", string_fields[i]))
            return false;
    }

    yyjson_val *radius = obj_get(component, "radius");
    if (radius != NULL && (!yyjson_is_num(radius) || yyjson_get_num(radius) < 0.0))
        return validation_error(ctx, path, "motion.brush_velocity_3d radius must be non-negative");
    yyjson_val *extents = obj_get(component, "extents");
    if (extents != NULL && (!is_exact_vec_array(extents, 3) || !numeric_array_values_in_range(extents, 0.0, DBL_MAX)))
        return validation_error(ctx, path, "motion.brush_velocity_3d extents must be a non-negative vec3");

    yyjson_val *slide = obj_get(component, "slide");
    yyjson_val *despawn_on_hit = obj_get(component, "despawn_on_hit");
    if ((slide != NULL && !yyjson_is_bool(slide)) || (despawn_on_hit != NULL && !yyjson_is_bool(despawn_on_hit)))
        return validation_error(ctx, path, "motion.brush_velocity_3d slide and despawn_on_hit must be booleans");

    char contents_path[PATH_BUFFER_SIZE];
    format_path(contents_path, sizeof(contents_path), "%s.contents_mask", path);
    yyjson_val *impact_actions = obj_get(component, "impact_actions");
    return validate_brush_string_or_string_array(ctx, obj_get(component, "contents_mask"), contents_path,
                                                 "brush content", brush_content_name_valid, false) &&
           validate_optional_signal_field(ctx, component, path, names, "on_impact") &&
           (impact_actions == NULL || validate_action_array(ctx, impact_actions, path, names));
}

static bool validate_motion_patrol_collision(validation_context *ctx, yyjson_val *collision, const char *path)
{
    if (collision == NULL)
        return true;
    if (!yyjson_is_obj(collision))
        return validation_error(ctx, path, "motion.patrol collision must be an object");

    const char *type = json_string(collision, "type");
    if (type != NULL && SDL_strcmp(type, "brush") != 0)
        return validation_error(ctx, path, "motion.patrol collision type must be brush");
    if (!brush_velocity_shape_valid(json_string(collision, "shape")))
        return validation_error(ctx, path, "motion.patrol collision shape must be point, sphere, or aabb");

    yyjson_val *extents = obj_get(collision, "extents");
    if (extents != NULL && (!is_exact_vec_array(extents, 3) || !numeric_array_values_in_range(extents, 0.0, DBL_MAX)))
        return validation_error(ctx, path, "motion.patrol collision extents must be a non-negative vec3");
    yyjson_val *center_offset = obj_get(collision, "center_offset");
    if (center_offset != NULL && !is_exact_vec_array(center_offset, 3))
        return validation_error(ctx, path, "motion.patrol collision center_offset must be a vec3");
    yyjson_val *radius = obj_get(collision, "radius");
    if (radius != NULL && (!yyjson_is_num(radius) || yyjson_get_num(radius) < 0.0))
        return validation_error(ctx, path, "motion.patrol collision radius must be non-negative");
    yyjson_val *slide_iterations = obj_get(collision, "slide_iterations");
    if (slide_iterations != NULL && (!yyjson_is_int(slide_iterations) || yyjson_get_int(slide_iterations) < 1 ||
                                     yyjson_get_int(slide_iterations) > 8))
        return validation_error(ctx, path, "motion.patrol collision slide_iterations must be an integer in [1, 8]");
    yyjson_val *contact_skin = obj_get(collision, "contact_skin");
    if (contact_skin != NULL && (!yyjson_is_num(contact_skin) || yyjson_get_num(contact_skin) < 0.0))
        return validation_error(ctx, path, "motion.patrol collision contact_skin must be non-negative");
    yyjson_val *ground_probe_distance = obj_get(collision, "ground_probe_distance");
    if (ground_probe_distance != NULL &&
        (!yyjson_is_num(ground_probe_distance) || yyjson_get_num(ground_probe_distance) < 0.0))
        return validation_error(ctx, path, "motion.patrol collision ground_probe_distance must be non-negative");
    yyjson_val *walkable_normal_y = obj_get(collision, "walkable_normal_y");
    if (walkable_normal_y != NULL && (!yyjson_is_num(walkable_normal_y) || yyjson_get_num(walkable_normal_y) < 0.0 ||
                                      yyjson_get_num(walkable_normal_y) > 1.0))
        return validation_error(ctx, path, "motion.patrol collision walkable_normal_y must be in [0, 1]");
    yyjson_val *on_ground_property = obj_get(collision, "on_ground_property");
    if (on_ground_property != NULL && !is_non_empty_string(collision, "on_ground_property"))
        return validation_error(ctx, path, "motion.patrol collision on_ground_property must be non-empty");

    char contents_path[PATH_BUFFER_SIZE];
    format_path(contents_path, sizeof(contents_path), "%s.contents_mask", path);
    return validate_brush_string_or_string_array(ctx, obj_get(collision, "contents_mask"), contents_path,
                                                 "brush content", brush_content_name_valid, false);
}

static bool validate_combat_health_component(validation_context *ctx, yyjson_val *component, const char *path)
{
    const char *property_keys[] = {"health_property", "max_health_property", "armor_property", "armor_absorb_property",
                                   "alive_property"};
    for (size_t i = 0; i < SDL_arraysize(property_keys); ++i)
    {
        yyjson_val *value = obj_get(component, property_keys[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, path, "combat.health property names must be non-empty strings");
    }

    yyjson_val *health = obj_get(component, "health");
    yyjson_val *max_health = obj_get(component, "max_health");
    yyjson_val *armor = obj_get(component, "armor");
    yyjson_val *armor_absorb = obj_get(component, "armor_absorb");
    if ((health != NULL && (!yyjson_is_num(health) || yyjson_get_num(health) < 0.0)) ||
        (max_health != NULL && (!yyjson_is_num(max_health) || yyjson_get_num(max_health) < 0.0)) ||
        (armor != NULL && (!yyjson_is_num(armor) || yyjson_get_num(armor) < 0.0)))
    {
        return validation_error(ctx, path, "combat.health numeric values must be non-negative");
    }
    if (armor_absorb != NULL &&
        (!yyjson_is_num(armor_absorb) || yyjson_get_num(armor_absorb) < 0.0 || yyjson_get_num(armor_absorb) > 1.0))
    {
        return validation_error(ctx, path, "combat.health armor_absorb must be in 0..1");
    }
    return true;
}

static bool validate_pickup_respawn_component(validation_context *ctx, yyjson_val *component, const char *path)
{
    if (!validate_non_empty_string_field(ctx, component, path, "pickup.respawn", "timer_property") ||
        !validate_non_empty_string_field(ctx, component, path, "pickup.respawn", "available_property"))
    {
        return false;
    }
    return true;
}

static bool validate_status_effect_timer_component(validation_context *ctx, yyjson_val *component, const char *path,
                                                   validation_names *names)
{
    (void)names;
    if (!is_non_empty_string(component, "property"))
        return validation_error(ctx, path, "status_effect.timer requires a non-empty property");
    if (!validate_non_empty_string_field(ctx, component, path, "status_effect.timer", "duration_property") ||
        !validate_non_empty_string_field(ctx, component, path, "status_effect.timer", "active_property"))
    {
        return false;
    }
    yyjson_val *expired = obj_get(component, "expired_value");
    if (expired != NULL && !(yyjson_is_bool(expired) || yyjson_is_num(expired) || yyjson_is_str(expired)))
        return validation_error(ctx, path, "status_effect.timer expired_value must be scalar");
    return validate_optional_signal_field(ctx, component, path, names, "on_expire");
}

static bool validate_particle_emitter_component(validation_context *ctx, yyjson_val *component, const char *path,
                                                const validation_names *names)
{
    const char *render_style = json_string(component, "render_style");
    if (render_style != NULL && SDL_strcmp(render_style, "default") != 0 &&
        SDL_strcmp(render_style, "soft_smoke") != 0 && SDL_strcmp(render_style, "soft_fire") != 0 &&
        SDL_strcmp(render_style, "muzzle_flash") != 0)
    {
        return validation_error(
            ctx, path,
            "particles.emitter render_style must be 'default', 'soft_smoke', 'soft_fire', or 'muzzle_flash'");
    }

    yyjson_val *position_offset = obj_get(component, "position_offset");
    if (position_offset != NULL && !is_vec_array(position_offset, 3))
        return validation_error(ctx, path, "particles.emitter position_offset must be a vec3");
    const char *space = json_string(component, "space");
    if (space != NULL && SDL_strcmp(space, "world") != 0 && SDL_strcmp(space, "camera") != 0)
        return validation_error(ctx, path, "particles.emitter space must be 'world' or 'camera'");
    if (!validate_render_camera_visibility_field(ctx, component, path, names, "visible_to_cameras") ||
        !validate_render_camera_visibility_field(ctx, component, path, names, "hidden_from_cameras"))
    {
        return false;
    }

    const char *property_fields[] = {
        "position_offset_property",    "position_offset_x_property", "position_offset_y_property",
        "position_offset_z_property",  "size_start_property",        "size_end_property",
        "size_scale_property",         "alpha_scale_property",       "emit_rate_property",
        "emissive_intensity_property",
    };
    for (size_t i = 0; i < SDL_arraysize(property_fields); ++i)
    {
        yyjson_val *value = obj_get(component, property_fields[i]);
        if (value != NULL && !is_non_empty_string(component, property_fields[i]))
            return validation_error(ctx, path, "particles.emitter %s must be non-empty", property_fields[i]);
    }

    return true;
}

static bool validate_weapon_state_component(validation_context *ctx, yyjson_val *component, const char *path)
{
    const char *string_fields[] = {"clip_property",         "clip_size_property",      "reserve_property",
                                   "reload_timer_property", "reload_pending_property", "cooldown_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, component, path, "weapon.state", string_fields[i]))
            return false;
    }
    yyjson_val *clip_size = obj_get(component, "clip_size");
    yyjson_val *cooldown_rate = obj_get(component, "cooldown_rate");
    if ((clip_size != NULL && (!yyjson_is_num(clip_size) || yyjson_get_num(clip_size) < 0.0)) ||
        (cooldown_rate != NULL && (!yyjson_is_num(cooldown_rate) || yyjson_get_num(cooldown_rate) < 0.0)))
    {
        return validation_error(ctx, path, "weapon.state numeric values must be non-negative");
    }
    yyjson_val *consume_reserve = obj_get(component, "consume_reserve");
    if (consume_reserve != NULL && !yyjson_is_bool(consume_reserve))
        return validation_error(ctx, path, "weapon.state consume_reserve must be a boolean");
    return true;
}

static bool validate_interactable_requires(validation_context *ctx, yyjson_val *requires, const char *path)
{
    if (requires == NULL)
        return true;
    if (!yyjson_is_obj(requires))
        return validation_error(ctx, path, "interactable requires must be an object");
    const char *property = json_string(requires, "property");
    if (property == NULL)
        property = json_string(requires, "resource");
    if (property == NULL || property[0] == '\0')
        return validation_error(ctx, path, "interactable requires needs a non-empty property or resource");
    yyjson_val *amount = obj_get(requires, "amount");
    if (amount != NULL && (!yyjson_is_num(amount) || yyjson_get_num(amount) < 0.0))
        return validation_error(ctx, path, "interactable requires amount must be non-negative");
    yyjson_val *consume = obj_get(requires, "consume");
    if (consume != NULL && !yyjson_is_bool(consume))
        return validation_error(ctx, path, "interactable requires consume must be a boolean");
    return true;
}

static bool validate_interactable_component(validation_context *ctx, yyjson_val *component, const char *path,
                                            validation_names *names)
{
    yyjson_val *range = obj_get(component, "range");
    yyjson_val *min_dot = obj_get(component, "min_dot");
    yyjson_val *cooldown = obj_get(component, "cooldown");
    if ((range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) < 0.0)) ||
        (cooldown != NULL && (!yyjson_is_num(cooldown) || yyjson_get_num(cooldown) < 0.0)) ||
        (min_dot != NULL &&
         (!yyjson_is_num(min_dot) || yyjson_get_num(min_dot) < -1.0 || yyjson_get_num(min_dot) > 1.0)))
    {
        return validation_error(ctx, path, "interactable range, min_dot, and cooldown values are invalid");
    }
    const char *string_fields[] = {"prompt", "prompt_key", "yaw_property", "cooldown_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, component, path, "interactable", string_fields[i]))
            return false;
    }
    if (!validate_interactable_requires(ctx, obj_get(component, "requires"), path))
        return false;
    const char *signal_keys[] = {"signal", "on_locked", "on_cooldown"};
    for (size_t i = 0; i < SDL_arraysize(signal_keys); ++i)
    {
        if (!validate_optional_signal_field(ctx, component, path, names, signal_keys[i]))
            return false;
    }
    const char *action_keys[] = {"actions", "locked_actions", "cooldown_actions"};
    for (size_t i = 0; i < SDL_arraysize(action_keys); ++i)
    {
        yyjson_val *actions = obj_get(component, action_keys[i]);
        if (actions != NULL && !validate_action_array(ctx, actions, path, names))
            return false;
    }
    if (obj_get(component, "actions") == NULL && json_string(component, "signal") == NULL &&
        obj_get(component, "locked_actions") == NULL && json_string(component, "on_locked") == NULL &&
        obj_get(component, "cooldown_actions") == NULL && json_string(component, "on_cooldown") == NULL)
    {
        return validation_error(ctx, path,
                                "interactable requires actions, signal, locked_actions, on_locked, cooldown_actions, "
                                "or on_cooldown");
    }
    return true;
}

static bool validate_projectile_fire_shape(validation_context *ctx, yyjson_val *value, const char *path,
                                           validation_names *names, bool require_target)
{
    if (!require_ref(ctx, &names->actor_pools, "actor pool", json_string(value, "pool"), path))
        return false;

    yyjson_val *target_value = obj_get(value, "target");
    yyjson_val *target_from_payload_value = obj_get(value, "target_from_payload");
    const char *target = json_string(value, "target");
    const char *target_from_payload = json_string(value, "target_from_payload");
    if (require_target)
    {
        if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
            return validation_error(ctx, path, "projectile.fire requires exactly one of target or target_from_payload");
    }
    else if (target != NULL || target_from_payload != NULL)
    {
        return validation_error(ctx, path,
                                "weapon.projectile uses its owning actor and must not declare target fields");
    }
    if (target_value != NULL && !yyjson_is_str(target_value))
        return validation_error(ctx, path, "projectile target must be a string");
    if (target != NULL && !require_actor_ref(ctx, names, target, path))
        return false;
    if (target_from_payload_value != NULL &&
        (!yyjson_is_str(target_from_payload_value) || yyjson_get_str(target_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, path, "projectile target_from_payload must be a non-empty string");
    }

    yyjson_val *offset = obj_get(value, "offset");
    if (offset != NULL && !is_vec_array(offset, 3))
        return validation_error(ctx, path, "projectile offset must be a vec3");
    yyjson_val *directional_offset = obj_get(value, "directional_offset");
    if (directional_offset != NULL)
    {
        if (!yyjson_is_obj(directional_offset))
            return validation_error(ctx, path, "projectile directional_offset must be an object");
        yyjson_val *property = obj_get(directional_offset, "property");
        yyjson_val *distance = obj_get(directional_offset, "distance");
        if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
            return validation_error(ctx, path, "projectile directional_offset property must be a non-empty string");
        if (!yyjson_is_num(distance))
            return validation_error(ctx, path, "projectile directional_offset distance must be numeric");
    }
    yyjson_val *velocity = obj_get(value, "velocity");
    if (velocity != NULL && !is_vec_array(velocity, 3))
        return validation_error(ctx, path, "projectile velocity must be a vec3");
    yyjson_val *velocity_from_property = obj_get(value, "velocity_from_property");
    if (velocity_from_property != NULL &&
        (!yyjson_is_str(velocity_from_property) || yyjson_get_str(velocity_from_property)[0] == '\0'))
    {
        return validation_error(ctx, path, "projectile velocity_from_property must be a non-empty string");
    }
    yyjson_val *speed = obj_get(value, "speed");
    if (speed != NULL && (!yyjson_is_num(speed) || yyjson_get_num(speed) < 0.0))
        return validation_error(ctx, path, "projectile speed must be non-negative");
    yyjson_val *cooldown = obj_get(value, "cooldown");
    if (cooldown != NULL && !yyjson_is_num(cooldown))
        return validation_error(ctx, path, "projectile cooldown must be numeric");
    yyjson_val *cooldown_property = obj_get(value, "cooldown_property");
    if (cooldown_property != NULL &&
        (!yyjson_is_str(cooldown_property) || yyjson_get_str(cooldown_property)[0] == '\0'))
    {
        return validation_error(ctx, path, "projectile cooldown_property must be a non-empty string");
    }
    const char *weapon_string_fields[] = {"clip_property", "ammo_resource", "ammo_property", "reload_timer_property",
                                          "direction_from_property"};
    for (size_t i = 0; i < SDL_arraysize(weapon_string_fields); ++i)
    {
        yyjson_val *field = obj_get(value, weapon_string_fields[i]);
        if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
            return validation_error(ctx, path, "projectile weapon fields must be non-empty strings");
    }
    yyjson_val *ammo_per_shot = obj_get(value, "ammo_per_shot");
    if (ammo_per_shot != NULL && (!yyjson_is_num(ammo_per_shot) || yyjson_get_num(ammo_per_shot) < 0.0))
        return validation_error(ctx, path, "projectile ammo_per_shot must be non-negative");
    const char *signal_keys[] = {"on_fire", "on_empty", "on_cooldown", "on_reloading"};
    for (size_t i = 0; i < SDL_arraysize(signal_keys); ++i)
    {
        const char *signal = json_string(value, signal_keys[i]);
        if (signal != NULL && !require_ref(ctx, &names->signals, "signal", signal, path))
            return false;
    }
    yyjson_val *properties = obj_get(value, "properties");
    if (properties != NULL && !yyjson_is_obj(properties))
        return validation_error(ctx, path, "projectile properties must be an object");
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

static bool collect_grid_maps(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *maps = obj_get(root, "grid_maps");
    if (maps == NULL)
        return true;
    if (!yyjson_is_arr(maps))
        return validation_error(ctx, "$.grid_maps", "grid_maps must be an array");

    for (size_t i = 0; i < yyjson_arr_size(maps); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.grid_maps[%zu]", i);
        yyjson_val *map = yyjson_arr_get(maps, i);
        if (!yyjson_is_obj(map))
            return validation_error(ctx, path, "grid map entries must be objects");
        if (!require_unique_name(ctx, &names->grid_maps, "grid map", json_string(map, "name"), path))
            return false;
    }
    return true;
}

static bool collect_grid_pickup_layers(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *layers = obj_get(root, "grid_pickup_layers");
    if (layers == NULL)
        return true;
    if (!yyjson_is_arr(layers))
        return validation_error(ctx, "$.grid_pickup_layers", "grid_pickup_layers must be an array");

    for (size_t i = 0; i < yyjson_arr_size(layers); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.grid_pickup_layers[%zu]", i);
        yyjson_val *layer = yyjson_arr_get(layers, i);
        if (!yyjson_is_obj(layer))
            return validation_error(ctx, path, "grid pickup layer entries must be objects");
        if (!require_unique_name(ctx, &names->grid_pickup_layers, "grid pickup layer", json_string(layer, "name"),
                                 path))
            return false;
    }
    return true;
}

static bool collect_sector_levels(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *levels = obj_get(root, "sector_levels");
    if (levels == NULL)
        return true;
    if (!yyjson_is_arr(levels))
        return validation_error(ctx, "$.sector_levels", "sector_levels must be an array");

    for (size_t i = 0; i < yyjson_arr_size(levels); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_levels[%zu]", i);
        yyjson_val *level = yyjson_arr_get(levels, i);
        if (!yyjson_is_obj(level))
            return validation_error(ctx, path, "sector level entries must be objects");
        if (!require_unique_name(ctx, &names->sector_levels, "sector level", json_string(level, "name"), path))
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

static bool collect_sector_navigation(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *graphs = obj_get(root, "sector_navigation");
    for (size_t i = 0; yyjson_is_arr(graphs) && i < yyjson_arr_size(graphs); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_navigation[%zu]", i);
        yyjson_val *graph = yyjson_arr_get(graphs, i);
        if (!require_unique_name(ctx, &names->sector_navigation, "sector navigation graph", json_string(graph, "name"),
                                 path))
        {
            return false;
        }
    }
    return true;
}

static bool collect_sector_doors(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *doors = obj_get(root, "sector_doors");
    if (doors == NULL)
        return true;
    if (!yyjson_is_arr(doors))
        return validation_error(ctx, "$.sector_doors", "sector_doors must be an array");

    for (size_t i = 0; i < yyjson_arr_size(doors); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_doors[%zu]", i);
        yyjson_val *door = yyjson_arr_get(doors, i);
        if (!yyjson_is_obj(door))
            return validation_error(ctx, path, "sector door entries must be objects");
        if (!require_unique_name(ctx, &names->sector_doors, "sector door", json_string(door, "name"), path))
            return false;
    }
    return true;
}

static bool collect_sector_platforms(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *platforms = obj_get(root, "sector_platforms");
    if (platforms == NULL)
        return true;
    if (!yyjson_is_arr(platforms))
        return validation_error(ctx, "$.sector_platforms", "sector_platforms must be an array");

    for (size_t i = 0; i < yyjson_arr_size(platforms); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_platforms[%zu]", i);
        yyjson_val *platform = yyjson_arr_get(platforms, i);
        if (!yyjson_is_obj(platform))
            return validation_error(ctx, path, "sector platform entries must be objects");
        if (!require_unique_name(ctx, &names->sector_platforms, "sector platform", json_string(platform, "name"), path))
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
        const char *kind = json_string(sprite, "kind");
        if (kind == NULL)
            kind = "sheet";
        if (SDL_strcmp(kind, "sheet") != 0 && SDL_strcmp(kind, "files") != 0)
            return validation_error(ctx, path, "sprite asset kind must be 'sheet' or 'files'");
        if (SDL_strcmp(kind, "sheet") == 0)
        {
            if (!is_non_empty_string(sprite, "path"))
                return validation_error(ctx, path, "sprite sheet asset requires a non-empty path");
            if (!asset_path_exists(ctx, json_string(sprite, "path"), path, "sprite"))
                return false;
        }
        else
        {
            yyjson_val *base_paths = obj_get(sprite, "base_paths");
            yyjson_val *frame_paths = obj_get(sprite, "frame_paths");
            yyjson_val *frame_count_value = obj_get(sprite, "frame_count");
            yyjson_val *direction_count_value = obj_get(sprite, "direction_count");
            const int frame_count = yyjson_is_int(frame_count_value) ? (int)yyjson_get_int(frame_count_value) : 0;
            const int direction_count =
                yyjson_is_int(direction_count_value) ? (int)yyjson_get_int(direction_count_value) : 0;
            if (!yyjson_is_arr(base_paths) || !yyjson_is_arr(frame_paths))
                return validation_error(ctx, path, "sprite file-list assets require base_paths and frame_paths arrays");
            if (direction_count <= 0 || frame_count <= 0)
                return validation_error(ctx, path,
                                        "sprite file-list assets require positive frame_count and direction_count");
            if ((int)yyjson_arr_size(base_paths) != direction_count)
                return validation_error(ctx, path, "sprite file-list base_paths count must match direction_count");
            if ((int)yyjson_arr_size(frame_paths) != frame_count * direction_count)
                return validation_error(ctx, path,
                                        "sprite file-list frame_paths count must match frame_count * direction_count");
            for (size_t path_index = 0; path_index < yyjson_arr_size(base_paths); ++path_index)
            {
                yyjson_val *entry = yyjson_arr_get(base_paths, path_index);
                const char *asset_path = yyjson_get_str(entry);
                if (asset_path == NULL || asset_path[0] == '\0')
                    return validation_error(ctx, path, "sprite file-list base paths must be non-empty strings");
                if (!asset_path_exists(ctx, asset_path, path, "sprite"))
                    return false;
            }
            for (size_t path_index = 0; path_index < yyjson_arr_size(frame_paths); ++path_index)
            {
                yyjson_val *entry = yyjson_arr_get(frame_paths, path_index);
                const char *asset_path = yyjson_get_str(entry);
                if (asset_path == NULL || asset_path[0] == '\0')
                    return validation_error(ctx, path, "sprite file-list frame paths must be non-empty strings");
                if (!asset_path_exists(ctx, asset_path, path, "sprite"))
                    return false;
            }
        }
        yyjson_val *direction_count_value = obj_get(sprite, "direction_count");
        const int direction_count =
            yyjson_is_int(direction_count_value) ? (int)yyjson_get_int(direction_count_value) : 1;
        if (direction_count <= 0 || direction_count > SLAYER3D_SPRITE_ROTATION_COUNT)
            return validation_error(ctx, path, "sprite direction_count must be between 1 and 8");
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
            const bool exists = slayer3d_asset_resolver_exists(ctx->assets, resolved);
            SDL_free(resolved);
            if (!exists)
                return validation_error(ctx, path, "image asset path '%s' does not exist", image_path);
        }
    }
    return true;
}

static bool collect_models(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *models = obj_get(obj_get(root, "assets"), "models");
    if (models == NULL)
        return true;
    if (!yyjson_is_arr(models))
        return validation_error(ctx, "$.assets.models", "model assets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(models); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.models[%zu]", i);
        yyjson_val *model = yyjson_arr_get(models, i);
        if (!yyjson_is_obj(model))
            return validation_error(ctx, path, "model asset entries must be objects");
        if (!require_unique_name(ctx, &names->models, "model asset", json_string(model, "id"), path))
            return false;
        if (!is_non_empty_string(model, "path"))
            return validation_error(ctx, path, "model asset requires path");
        if (!asset_path_exists(ctx, json_string(model, "path"), path, "model"))
            return false;
    }
    return true;
}

static bool is_audio_bus_name(const char *bus)
{
    return bus == NULL || SDL_strcmp(bus, "sound_effects") == 0 || SDL_strcmp(bus, "sfx") == 0 ||
           SDL_strcmp(bus, "music") == 0 || SDL_strcmp(bus, "dialogue") == 0 || SDL_strcmp(bus, "dialog") == 0 ||
           SDL_strcmp(bus, "ambience") == 0 || SDL_strcmp(bus, "ambiance") == 0 || SDL_strcmp(bus, "ambient") == 0;
}

bool asset_path_exists(validation_context *ctx, const char *asset_path, const char *json_path, const char *asset_kind)
{
    if (ctx->assets == NULL)
        return true;

    char *resolved = asset_path != NULL && SDL_strncmp(asset_path, "asset://", 8) == 0
                         ? SDL_strdup(asset_path)
                         : path_join(ctx->base_dir, asset_path);
    if (resolved == NULL)
        return validation_error(ctx, json_path, "failed to resolve %s asset path", asset_kind);
    const bool exists = slayer3d_asset_resolver_exists(ctx->assets, resolved);
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

    yyjson_val *ambient = obj_get(assets, "ambient");
    if (ambient != NULL && !yyjson_is_arr(ambient))
        return validation_error(ctx, "$.assets.ambient", "ambient assets must be an array");
    for (size_t i = 0; yyjson_is_arr(ambient) && i < yyjson_arr_size(ambient); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.ambient[%zu]", i);
        yyjson_val *zone = yyjson_arr_get(ambient, i);
        if (!yyjson_is_obj(zone))
            return validation_error(ctx, path, "ambient asset entries must be objects");
        if (!require_unique_name(ctx, &names->ambient, "ambient asset", json_string(zone, "id"), path))
            return false;
        yyjson_val *ambient_id = obj_get(zone, "ambient_id");
        if (!yyjson_is_int(ambient_id) || yyjson_get_int(ambient_id) < 0)
            return validation_error(ctx, path, "ambient asset requires a non-negative ambient_id");
        for (size_t previous = 0; previous < i; ++previous)
        {
            yyjson_val *previous_id = obj_get(yyjson_arr_get(ambient, previous), "ambient_id");
            if (yyjson_is_int(previous_id) && yyjson_get_int(previous_id) == yyjson_get_int(ambient_id))
                return validation_error(ctx, path, "duplicate ambient asset ambient_id %d",
                                        (int)yyjson_get_int(ambient_id));
        }
        if (!is_non_empty_string(zone, "path"))
            return validation_error(ctx, path, "ambient asset requires a non-empty path");
        if (!asset_path_exists(ctx, json_string(zone, "path"), path, "ambient"))
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
            const char *name = json_string(action, "name");
            if (name != NULL && SDL_strlen(name) >= SLAYER3D_INPUT_ACTION_NAME_MAX)
                return validation_error(ctx, path, "input action name must be shorter than %d bytes",
                                        SLAYER3D_INPUT_ACTION_NAME_MAX);
            if (!require_unique_name(ctx, &names->actions, "input action", name, path))
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
           collect_grid_maps(ctx, root, names) && collect_grid_pickup_layers(ctx, root, names) &&
           collect_sector_levels(ctx, root, names) && collect_brush_worlds(ctx, root, names) &&
           collect_sector_navigation(ctx, root, names) && collect_sector_doors(ctx, root, names) &&
           collect_sector_platforms(ctx, root, names) && collect_editor_player_starts(ctx, root, names) &&
           collect_actor_archetypes(ctx, root, names) && collect_actor_pools(ctx, root, names) &&
           collect_scripts(ctx, root, names) && collect_adapters(ctx, root, names) &&
           collect_input_actions(ctx, root, names) && collect_input_assignment_sets(ctx, root, names) &&
           collect_input_profiles(ctx, root, names) && collect_network_input_channels(ctx, root, names) &&
           collect_cameras(ctx, root, names) && collect_fonts(ctx, root, names) &&
           collect_sprite_assets(ctx, root, names) && collect_images(ctx, root, names) &&
           collect_models(ctx, root, names) && collect_audio_assets(ctx, root, names) &&
           collect_timers(ctx, root, names) && collect_sensors(ctx, root, names);
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
                    if (!validate_keyboard_modifiers(ctx, binding, path))
                        return false;
                }
                else if (SDL_strcmp(device != NULL ? device : "", "gamepad") == 0)
                {
                    if (!is_non_empty_string(binding, "axis") && !is_non_empty_string(binding, "button"))
                        return validation_error(ctx, path, "gamepad binding requires an axis or button");
                    yyjson_val *slot = obj_get(binding, "slot");
                    if (slot != NULL && (!yyjson_is_num(slot) || yyjson_get_sint(slot) < -1 ||
                                         yyjson_get_sint(slot) >= SLAYER3D_INPUT_MAX_GAMEPADS))
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
        if (!validate_keyboard_modifiers(ctx, binding, path))
            return false;
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
        if (slot != NULL && (!yyjson_is_num(slot) || yyjson_get_sint(slot) < -1 ||
                             yyjson_get_sint(slot) >= SLAYER3D_INPUT_MAX_GAMEPADS))
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

yyjson_val *slayer3d_game_data_find_input_assignment_set_json(yyjson_val *root, const char *set_name)
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
        if (!validate_keyboard_modifiers(ctx, binding, path))
            return false;
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

    yyjson_val *set = slayer3d_game_data_find_input_assignment_set_json(root, set_name);
    const char *device = json_string(set, "device");
    yyjson_val *slot = obj_get(assignment, "slot");
    if (slot != NULL && SDL_strcmp(device != NULL ? device : "", "gamepad") != 0)
    {
        return validation_error(ctx, path, "input profile assignment slot is only valid for gamepad assignment sets");
    }
    if (slot != NULL &&
        (!yyjson_is_num(slot) || yyjson_get_sint(slot) < -1 || yyjson_get_sint(slot) >= SLAYER3D_INPUT_MAX_GAMEPADS))
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
             yyjson_get_sint(min_gamepads) > SLAYER3D_INPUT_MAX_GAMEPADS))
        {
            ok = validation_error(ctx, path, "input profile min_gamepads must be a valid gamepad count");
        }
        if (ok && max_gamepads != NULL &&
            (!yyjson_is_num(max_gamepads) || yyjson_get_sint(max_gamepads) < 0 ||
             yyjson_get_sint(max_gamepads) > SLAYER3D_INPUT_MAX_GAMEPADS))
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

static bool is_single_byte_string(yyjson_val *value)
{
    return yyjson_is_str(value) && SDL_strlen(yyjson_get_str(value)) == 1U;
}

static bool validate_grid_maps(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *maps = obj_get(root, "grid_maps");
    for (size_t i = 0; yyjson_is_arr(maps) && i < yyjson_arr_size(maps); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.grid_maps[%zu]", i);
        yyjson_val *map = yyjson_arr_get(maps, i);
        yyjson_val *rows = obj_get(map, "rows");
        if (!yyjson_is_arr(rows) || yyjson_arr_size(rows) <= 0)
            return validation_error(ctx, path, "grid map rows must be a non-empty array");
        yyjson_val *first = yyjson_arr_get(rows, 0);
        if (!yyjson_is_str(first) || SDL_strlen(yyjson_get_str(first)) <= 0)
        {
            char row_path[PATH_BUFFER_SIZE];
            format_path(row_path, sizeof(row_path), "%s.rows[0]", path);
            return validation_error(ctx, row_path, "grid map rows must be non-empty strings");
        }
        const size_t width = SDL_strlen(yyjson_get_str(first));
        for (size_t row = 0; row < yyjson_arr_size(rows); ++row)
        {
            yyjson_val *row_value = yyjson_arr_get(rows, row);
            if (!yyjson_is_str(row_value) || SDL_strlen(yyjson_get_str(row_value)) != width)
                return validation_error(ctx, path, "grid map rows must have identical widths");
        }
        yyjson_val *cell_size = obj_get(map, "cell_size");
        if (cell_size != NULL && !is_vec_array(cell_size, 2))
            return validation_error(ctx, path, "grid map cell_size must be a vec2");
        if (cell_size != NULL && (yyjson_get_num(yyjson_arr_get(cell_size, 0)) <= 0.0 ||
                                  yyjson_get_num(yyjson_arr_get(cell_size, 1)) <= 0.0))
            return validation_error(ctx, path, "grid map cell_size values must be positive");
        yyjson_val *origin = obj_get(map, "origin");
        if (origin != NULL && !is_vec_array(origin, 3))
            return validation_error(ctx, path, "grid map origin must be a vec3");
        yyjson_val *row_direction = obj_get(map, "row_direction");
        if (row_direction != NULL && !yyjson_is_num(row_direction))
            return validation_error(ctx, path, "grid map row_direction must be numeric");
        yyjson_val *walkable = obj_get(map, "walkable");
        if (!yyjson_is_arr(walkable) || yyjson_arr_size(walkable) <= 0)
            return validation_error(ctx, path, "grid map walkable must be a non-empty array");
        for (size_t glyph = 0; glyph < yyjson_arr_size(walkable); ++glyph)
        {
            if (!is_single_byte_string(yyjson_arr_get(walkable, glyph)))
                return validation_error(ctx, path, "grid map walkable entries must be single-byte glyph strings");
        }
        yyjson_val *wrap_x = obj_get(map, "wrap_x");
        yyjson_val *wrap_y = obj_get(map, "wrap_y");
        if ((wrap_x != NULL && !yyjson_is_bool(wrap_x)) || (wrap_y != NULL && !yyjson_is_bool(wrap_y)))
            return validation_error(ctx, path, "grid map wrap flags must be booleans");
    }
    return true;
}

static bool validate_grid_pickup_layers(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *layers = obj_get(root, "grid_pickup_layers");
    if (layers == NULL)
        return true;
    if (!yyjson_is_arr(layers))
        return validation_error(ctx, "$.grid_pickup_layers", "grid_pickup_layers must be an array");

    for (size_t i = 0; i < yyjson_arr_size(layers); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.grid_pickup_layers[%zu]", i);
        yyjson_val *layer = yyjson_arr_get(layers, i);
        if (!yyjson_is_obj(layer))
            return validation_error(ctx, path, "grid pickup layer entries must be objects");
        if (!require_ref(ctx, &names->grid_maps, "grid map", json_string(layer, "map"), path))
            return false;
        yyjson_val *kinds = obj_get(layer, "kinds");
        if (!yyjson_is_arr(kinds) || yyjson_arr_size(kinds) <= 0)
            return validation_error(ctx, path, "grid pickup layer kinds must be a non-empty array");
        name_table glyphs;
        SDL_zero(glyphs);
        bool ok = true;
        for (size_t k = 0; ok && k < yyjson_arr_size(kinds); ++k)
        {
            char kind_path[PATH_BUFFER_SIZE];
            format_path(kind_path, sizeof(kind_path), "%s.kinds[%zu]", path, k);
            yyjson_val *kind = yyjson_arr_get(kinds, k);
            if (!yyjson_is_obj(kind))
            {
                ok = validation_error(ctx, kind_path, "grid pickup kind entries must be objects");
                break;
            }
            yyjson_val *glyph = obj_get(kind, "glyph");
            if (!is_single_byte_string(glyph))
            {
                ok = validation_error(ctx, kind_path, "grid pickup kind glyph must be a single-byte string");
                break;
            }
            if (!require_unique_name(ctx, &glyphs, "grid pickup glyph", yyjson_get_str(glyph), kind_path))
            {
                ok = false;
                break;
            }
            if (!is_non_empty_string(kind, "kind"))
            {
                ok = validation_error(ctx, kind_path, "grid pickup kind requires a non-empty kind");
                break;
            }
            yyjson_val *points = obj_get(kind, "points");
            if (points != NULL && !yyjson_is_int(points))
            {
                ok = validation_error(ctx, kind_path, "grid pickup points must be an integer");
                break;
            }
            yyjson_val *z = obj_get(kind, "z");
            if (z != NULL && !yyjson_is_num(z))
            {
                ok = validation_error(ctx, kind_path, "grid pickup z must be numeric");
                break;
            }
            yyjson_val *radius = obj_get(kind, "radius");
            if (radius != NULL && (!yyjson_is_num(radius) || yyjson_get_num(radius) <= 0.0))
            {
                ok = validation_error(ctx, kind_path, "grid pickup radius must be positive");
                break;
            }
            yyjson_val *rings = obj_get(kind, "rings");
            yyjson_val *slices = obj_get(kind, "slices");
            if ((rings != NULL && (!yyjson_is_int(rings) || yyjson_get_int(rings) < 3)) ||
                (slices != NULL && (!yyjson_is_int(slices) || yyjson_get_int(slices) < 3)))
            {
                ok = validation_error(ctx, kind_path, "grid pickup rings and slices must be integers >= 3");
                break;
            }
            yyjson_val *color = obj_get(kind, "color");
            if (color != NULL && !is_vec_array(color, 4))
            {
                ok = validation_error(ctx, kind_path, "grid pickup color must be a color vec4");
                break;
            }
            yyjson_val *lighting = obj_get(kind, "lighting");
            yyjson_val *emissive = obj_get(kind, "emissive");
            if ((lighting != NULL && !yyjson_is_bool(lighting)) || (emissive != NULL && !yyjson_is_bool(emissive)))
            {
                ok = validation_error(ctx, kind_path, "grid pickup lighting and emissive must be booleans");
                break;
            }
        }
        name_table_destroy(&glyphs);
        if (!ok)
            return false;
    }
    return true;
}

static bool sector_level_material_ref_valid(yyjson_val *materials, const name_table *material_names, yyjson_val *ref,
                                            bool allow_none)
{
    if (allow_none && (ref == NULL || yyjson_is_null(ref)))
        return true;
    if (yyjson_is_int(ref))
    {
        const int index = (int)yyjson_get_int(ref);
        return index >= (allow_none ? -1 : 0) && index < (int)yyjson_arr_size(materials);
    }
    if (yyjson_is_str(ref))
    {
        const char *name = yyjson_get_str(ref);
        if (allow_none && SDL_strcmp(name != NULL ? name : "", "none") == 0)
            return true;
        return name_table_contains(material_names, name);
    }
    return false;
}

static bool validate_sector_levels(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *levels = obj_get(root, "sector_levels");
    if (levels == NULL)
        return true;
    if (!yyjson_is_arr(levels))
        return validation_error(ctx, "$.sector_levels", "sector_levels must be an array");

    for (size_t level_index = 0; level_index < yyjson_arr_size(levels); ++level_index)
    {
        char level_path[PATH_BUFFER_SIZE];
        format_path(level_path, sizeof(level_path), "$.sector_levels[%zu]", level_index);
        yyjson_val *level = yyjson_arr_get(levels, level_index);
        yyjson_val *materials = obj_get(level, "materials");
        yyjson_val *sectors = obj_get(level, "sectors");
        yyjson_val *lights = obj_get(level, "lights");
        name_table material_names;
        name_table sector_names;
        SDL_zero(material_names);
        SDL_zero(sector_names);
        bool ok = true;

        if (!yyjson_is_obj(level))
        {
            ok = validation_error(ctx, level_path, "sector level entries must be objects");
            goto done;
        }
        if (!yyjson_is_arr(materials) || yyjson_arr_size(materials) <= 0)
        {
            ok = validation_error(ctx, level_path, "sector level materials must be a non-empty array");
            goto done;
        }
        if (!yyjson_is_arr(sectors) || yyjson_arr_size(sectors) <= 0)
        {
            ok = validation_error(ctx, level_path, "sector level sectors must be a non-empty array");
            goto done;
        }

        for (size_t material_index = 0; ok && material_index < yyjson_arr_size(materials); ++material_index)
        {
            char material_path[PATH_BUFFER_SIZE];
            format_path(material_path, sizeof(material_path), "%s.materials[%zu]", level_path, material_index);
            yyjson_val *material = yyjson_arr_get(materials, material_index);
            if (!yyjson_is_obj(material))
            {
                ok = validation_error(ctx, material_path, "sector material entries must be objects");
                break;
            }
            if (!require_unique_name(ctx, &material_names, "sector material", json_string(material, "name"),
                                     material_path))
            {
                ok = false;
                break;
            }
            yyjson_val *albedo = obj_get(material, "albedo");
            if (albedo != NULL && !is_vec_array(albedo, 3))
            {
                ok = validation_error(ctx, material_path, "sector material albedo must be a vec3 or vec4");
                break;
            }
            yyjson_val *metallic = obj_get(material, "metallic");
            yyjson_val *roughness = obj_get(material, "roughness");
            yyjson_val *tex_scale = obj_get(material, "tex_scale");
            if ((metallic != NULL && !yyjson_is_num(metallic)) || (roughness != NULL && !yyjson_is_num(roughness)) ||
                (tex_scale != NULL && !yyjson_is_num(tex_scale)))
            {
                ok = validation_error(ctx, material_path, "sector material numeric fields must be numbers");
                break;
            }
            if (tex_scale != NULL && yyjson_get_num(tex_scale) <= 0.0)
            {
                ok = validation_error(ctx, material_path, "sector material tex_scale must be positive");
                break;
            }
            const char *texture = json_string(material, "texture");
            if (texture != NULL && texture[0] == '\0')
            {
                ok = validation_error(ctx, material_path, "sector material texture must be non-empty when present");
                break;
            }
            if (texture != NULL && !asset_path_exists(ctx, texture, material_path, "sector material texture"))
            {
                ok = false;
                break;
            }
        }

        for (size_t sector_index = 0; ok && sector_index < yyjson_arr_size(sectors); ++sector_index)
        {
            char sector_path[PATH_BUFFER_SIZE];
            format_path(sector_path, sizeof(sector_path), "%s.sectors[%zu]", level_path, sector_index);
            yyjson_val *sector = yyjson_arr_get(sectors, sector_index);
            yyjson_val *points = obj_get(sector, "points");
            if (!yyjson_is_obj(sector))
            {
                ok = validation_error(ctx, sector_path, "sector entries must be objects");
                break;
            }
            const char *sector_name = json_string(sector, "name");
            if (sector_name != NULL && sector_name[0] != '\0' &&
                !require_unique_name(ctx, &sector_names, "sector", sector_name, sector_path))
            {
                ok = false;
                break;
            }
            if (!yyjson_is_arr(points) || yyjson_arr_size(points) < 3 ||
                yyjson_arr_size(points) > SLAYER3D_SECTOR_MAX_POINTS)
            {
                ok = validation_error(ctx, sector_path, "sector points must contain 3..%d vec2 entries",
                                      SLAYER3D_SECTOR_MAX_POINTS);
                break;
            }
            for (size_t point_index = 0; point_index < yyjson_arr_size(points); ++point_index)
            {
                if (!is_exact_vec_array(yyjson_arr_get(points, point_index), 2))
                {
                    ok = validation_error(ctx, sector_path, "sector points must be vec2 arrays");
                    break;
                }
            }
            if (!ok)
                break;
            yyjson_val *floor_y = obj_get(sector, "floor_y");
            yyjson_val *ceil_y = obj_get(sector, "ceil_y");
            if (!yyjson_is_num(floor_y) || !yyjson_is_num(ceil_y))
            {
                ok = validation_error(ctx, sector_path, "sector floor_y and ceil_y must be numbers");
                break;
            }
            if (yyjson_get_num(ceil_y) <= yyjson_get_num(floor_y))
            {
                ok = validation_error(ctx, sector_path, "sector ceil_y must be greater than floor_y");
                break;
            }
            if (!sector_level_material_ref_valid(materials, &material_names, obj_get(sector, "floor_material"), true) ||
                !sector_level_material_ref_valid(materials, &material_names, obj_get(sector, "ceil_material"), true) ||
                !sector_level_material_ref_valid(materials, &material_names, obj_get(sector, "wall_material"), false))
            {
                ok = validation_error(ctx, sector_path, "sector material refs must reference a declared material");
                break;
            }
            yyjson_val *floor_normal = obj_get(sector, "floor_normal");
            yyjson_val *ceil_normal = obj_get(sector, "ceil_normal");
            yyjson_val *push_velocity = obj_get(sector, "push_velocity");
            if ((floor_normal != NULL && !is_exact_vec_array(floor_normal, 3)) ||
                (ceil_normal != NULL && !is_exact_vec_array(ceil_normal, 3)) ||
                (push_velocity != NULL && !is_exact_vec_array(push_velocity, 3)))
            {
                ok = validation_error(ctx, sector_path, "sector normals and push_velocity must be vec3 arrays");
                break;
            }
            yyjson_val *ambient = obj_get(sector, "ambient_sound_id");
            if (ambient != NULL && (!yyjson_is_int(ambient) || yyjson_get_int(ambient) < 0))
            {
                ok = validation_error(ctx, sector_path, "sector ambient_sound_id must be a non-negative integer");
                break;
            }
            yyjson_val *damage = obj_get(sector, "damage_per_second");
            if (damage != NULL && (!yyjson_is_num(damage) || yyjson_get_num(damage) < 0.0))
            {
                ok = validation_error(ctx, sector_path, "sector damage_per_second must be non-negative");
                break;
            }
            yyjson_val *lighting = obj_get(sector, "lighting");
            if (lighting != NULL)
            {
                if (!yyjson_is_obj(lighting))
                {
                    ok = validation_error(ctx, sector_path, "sector lighting must be an object");
                    break;
                }
                yyjson_val *lighting_level = obj_get(lighting, "level");
                if (lighting_level != NULL && (!yyjson_is_int(lighting_level) || yyjson_get_int(lighting_level) < 0 ||
                                               yyjson_get_int(lighting_level) > 255))
                {
                    ok = validation_error(ctx, sector_path, "sector lighting level must be an integer in [0, 255]");
                    break;
                }
                yyjson_val *color = obj_get(lighting, "color");
                if (color != NULL &&
                    (!is_exact_vec3_or_vec4_array(color) || !numeric_array_values_in_range(color, 0.0, 1.0)))
                {
                    ok = validation_error(ctx, sector_path,
                                          "sector lighting color must be a vec3 or vec4 with values in [0, 1]");
                    break;
                }
            }
        }

        if (ok && lights != NULL)
        {
            if (!yyjson_is_arr(lights))
            {
                ok = validation_error(ctx, level_path, "sector level lights must be an array");
                goto done;
            }
            for (size_t light_index = 0; ok && light_index < yyjson_arr_size(lights); ++light_index)
            {
                char light_path[PATH_BUFFER_SIZE];
                format_path(light_path, sizeof(light_path), "%s.lights[%zu]", level_path, light_index);
                yyjson_val *light = yyjson_arr_get(lights, light_index);
                if (!yyjson_is_obj(light) || !is_exact_vec_array(obj_get(light, "position"), 3))
                {
                    ok = validation_error(ctx, light_path, "sector light requires position vec3");
                    break;
                }
                yyjson_val *color = obj_get(light, "color");
                if (color != NULL && !is_exact_vec_array(color, 3))
                {
                    ok = validation_error(ctx, light_path, "sector light color must be a vec3");
                    break;
                }
                yyjson_val *intensity = obj_get(light, "intensity");
                yyjson_val *range = obj_get(light, "range");
                if ((intensity != NULL && (!yyjson_is_num(intensity) || yyjson_get_num(intensity) < 0.0)) ||
                    (range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) <= 0.0)))
                {
                    ok = validation_error(ctx, light_path,
                                          "sector light intensity must be non-negative and range positive");
                    break;
                }
            }
        }

    done:
        name_table_destroy(&material_names);
        name_table_destroy(&sector_names);
        if (!ok)
            return false;
    }
    return true;
}

static bool validate_sector_doors(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *doors = obj_get(root, "sector_doors");
    if (doors == NULL)
        return true;
    if (!yyjson_is_arr(doors))
        return validation_error(ctx, "$.sector_doors", "sector_doors must be an array");

    for (size_t door_index = 0; door_index < yyjson_arr_size(doors); ++door_index)
    {
        char door_path[PATH_BUFFER_SIZE];
        format_path(door_path, sizeof(door_path), "$.sector_doors[%zu]", door_index);
        yyjson_val *door = yyjson_arr_get(doors, door_index);
        if (!yyjson_is_obj(door))
            return validation_error(ctx, door_path, "sector door entries must be objects");
        const char *scene = json_string(door, "scene");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, door_path))
            return false;

        yyjson_val *id = obj_get(door, "id");
        if (id != NULL && !yyjson_is_int(id))
            return validation_error(ctx, door_path, "sector door id must be an integer");
        const char *numeric_fields[] = {"open_seconds", "close_seconds", "stay_open_seconds"};
        for (size_t n = 0; n < SDL_arraysize(numeric_fields); ++n)
        {
            yyjson_val *value = obj_get(door, numeric_fields[n]);
            if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0))
                return validation_error(ctx, door_path, "sector door timing values must be non-negative");
        }
        yyjson_val *start_open = obj_get(door, "start_open");
        if (start_open != NULL && !yyjson_is_bool(start_open))
            return validation_error(ctx, door_path, "sector door start_open must be a boolean");

        yyjson_val *panels = obj_get(door, "panels");
        if (!yyjson_is_arr(panels) || yyjson_arr_size(panels) < 1 || yyjson_arr_size(panels) > SLAYER3D_DOOR_MAX_PANELS)
        {
            return validation_error(ctx, door_path, "sector door panels must be an array with 1 or 2 entries");
        }
        for (size_t panel_index = 0; panel_index < yyjson_arr_size(panels); ++panel_index)
        {
            char panel_path[PATH_BUFFER_SIZE];
            format_path(panel_path, sizeof(panel_path), "%s.panels[%zu]", door_path, panel_index);
            yyjson_val *panel = yyjson_arr_get(panels, panel_index);
            yyjson_val *bounds = obj_get(panel, "bounds");
            yyjson_val *bounds_source = bounds != NULL ? bounds : panel;
            if (!yyjson_is_obj(panel))
                return validation_error(ctx, panel_path, "sector door panel entries must be objects");
            if (!is_exact_vec_array(obj_get(bounds_source, "min"), 3) ||
                !is_exact_vec_array(obj_get(bounds_source, "max"), 3))
            {
                return validation_error(ctx, panel_path, "sector door panel requires bounds min and max vec3 values");
            }
            if (!is_exact_vec_array(obj_get(panel, "open_offset"), 3))
                return validation_error(ctx, panel_path, "sector door panel requires open_offset vec3");
        }

        yyjson_val *render = obj_get(door, "render");
        if (render != NULL)
        {
            if (!yyjson_is_obj(render))
                return validation_error(ctx, door_path, "sector door render must be an object");
            yyjson_val *color = obj_get(render, "color");
            if (color != NULL && !is_vec_array(color, 3))
                return validation_error(ctx, door_path, "sector door render color must be a vec3 or vec4");
            yyjson_val *lighting = obj_get(render, "lighting");
            yyjson_val *emissive = obj_get(render, "emissive");
            if ((lighting != NULL && !yyjson_is_bool(lighting)) || (emissive != NULL && !yyjson_is_bool(emissive)))
                return validation_error(ctx, door_path, "sector door render lighting flags must be booleans");
            const char *texture = json_string(render, "texture");
            if (texture != NULL && !require_ref(ctx, &names->images, "image asset", texture, door_path))
                return false;
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
            else if (SDL_strcmp(type, "controller.fps_sector") == 0)
            {
                if (!validate_fps_sector_component(ctx, component, path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "controller.fps_brush") == 0)
            {
                if (!validate_fps_brush_component(ctx, component, path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "controller.editor_camera") == 0)
            {
                if (!validate_editor_camera_component(ctx, component, path, names))
                    return false;
                char condition_path[PATH_BUFFER_SIZE];
                format_path(condition_path, sizeof(condition_path), "%s.orthographic_controls_if", path);
                if (!validate_data_condition(ctx, obj_get(component, "orthographic_controls_if"), condition_path,
                                             names))
                    return false;
            }
            else if (SDL_strcmp(type, "combat.health") == 0)
            {
                if (!validate_combat_health_component(ctx, component, path))
                    return false;
            }
            else if (SDL_strcmp(type, "pickup.respawn") == 0)
            {
                if (!validate_pickup_respawn_component(ctx, component, path))
                    return false;
            }
            else if (SDL_strcmp(type, "status_effect.timer") == 0)
            {
                if (!validate_status_effect_timer_component(ctx, component, path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "weapon.state") == 0)
            {
                if (!validate_weapon_state_component(ctx, component, path))
                    return false;
            }
            else if (SDL_strcmp(type, "interactable") == 0)
            {
                if (!validate_interactable_component(ctx, component, path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "weapon.projectile") == 0)
            {
                if (!require_ref(ctx, &names->actions, "input action", json_string(component, "action"), path) ||
                    !validate_projectile_fire_shape(ctx, component, path, names, false))
                {
                    return false;
                }
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
            else if (SDL_strcmp(type, "motion.patrol") == 0)
            {
                yyjson_val *waypoints = obj_get(component, "waypoints");
                if (!yyjson_is_arr(waypoints) || yyjson_arr_size(waypoints) < 2 ||
                    yyjson_arr_size(waypoints) > SLAYER3D_ACTOR_PATROL_MAX_WAYPOINTS)
                    return validation_error(ctx, path, "motion.patrol requires 2 to 16 waypoints");
                for (size_t waypoint_index = 0; waypoint_index < yyjson_arr_size(waypoints); ++waypoint_index)
                {
                    if (!is_vec_array(yyjson_arr_get(waypoints, waypoint_index), 3))
                        return validation_error(ctx, path, "motion.patrol waypoints must be vec3 values");
                }
                yyjson_val *speed = obj_get(component, "speed");
                yyjson_val *wait_time = obj_get(component, "wait_time");
                yyjson_val *arrival_radius = obj_get(component, "arrival_radius");
                if (speed != NULL && (!yyjson_is_num(speed) || yyjson_get_num(speed) <= 0.0))
                    return validation_error(ctx, path, "motion.patrol speed must be positive");
                if (wait_time != NULL && (!yyjson_is_num(wait_time) || yyjson_get_num(wait_time) < 0.0))
                    return validation_error(ctx, path, "motion.patrol wait_time must be non-negative");
                if (arrival_radius != NULL && (!yyjson_is_num(arrival_radius) || yyjson_get_num(arrival_radius) <= 0.0))
                    return validation_error(ctx, path, "motion.patrol arrival_radius must be positive");
                const char *mode = json_string(component, "mode");
                if (mode != NULL && SDL_strcmp(mode, "loop") != 0 && SDL_strcmp(mode, "ping_pong") != 0)
                    return validation_error(ctx, path, "motion.patrol mode must be 'loop' or 'ping_pong'");
                yyjson_val *start_idle = obj_get(component, "start_idle");
                if (start_idle != NULL && !yyjson_is_bool(start_idle))
                    return validation_error(ctx, path, "motion.patrol start_idle must be a boolean");
                yyjson_val *yaw_property = obj_get(component, "yaw_property");
                if (yaw_property != NULL && !is_non_empty_string(component, "yaw_property"))
                    return validation_error(ctx, path, "motion.patrol yaw_property must be non-empty");
                yyjson_val *animation_time_property = obj_get(component, "animation_time_property");
                if (animation_time_property != NULL && !is_non_empty_string(component, "animation_time_property"))
                    return validation_error(ctx, path, "motion.patrol animation_time_property must be non-empty");
                yyjson_val *animation_rate = obj_get(component, "animation_rate");
                if (animation_rate != NULL && !yyjson_is_num(animation_rate))
                    return validation_error(ctx, path, "motion.patrol animation_rate must be a number");
                yyjson_val *animate_when_idle = obj_get(component, "animate_when_idle");
                if (animate_when_idle != NULL && !yyjson_is_bool(animate_when_idle))
                    return validation_error(ctx, path, "motion.patrol animate_when_idle must be a boolean");
                yyjson_val *face_target = obj_get(component, "face_target");
                if (face_target != NULL && !yyjson_is_bool(face_target))
                    return validation_error(ctx, path, "motion.patrol face_target must be a boolean");
                const char *yaw_forward = json_string(component, "yaw_forward");
                if (yaw_forward != NULL && SDL_strcmp(yaw_forward, "-z") != 0 &&
                    SDL_strcmp(yaw_forward, "negative_z") != 0 && SDL_strcmp(yaw_forward, "+z") != 0 &&
                    SDL_strcmp(yaw_forward, "positive_z") != 0)
                {
                    return validation_error(ctx, path,
                                            "motion.patrol yaw_forward must be '-z', 'negative_z', '+z', or "
                                            "'positive_z'");
                }
                char collision_path[PATH_BUFFER_SIZE];
                format_path(collision_path, sizeof(collision_path), "%s.collision", path);
                if (!validate_motion_patrol_collision(ctx, obj_get(component, "collision"), collision_path))
                    return false;
                yyjson_val *signals = obj_get(component, "signals");
                if (signals != NULL)
                {
                    if (!yyjson_is_obj(signals))
                        return validation_error(ctx, path, "motion.patrol signals must be an object");
                    const char *signal_keys[] = {"waypoint_reached", "loop_completed", "idle_started", "walk_started"};
                    for (size_t signal_index = 0; signal_index < SDL_arraysize(signal_keys); ++signal_index)
                    {
                        const char *signal_name = json_string(signals, signal_keys[signal_index]);
                        if (signal_name != NULL && !require_ref(ctx, &names->signals, "signal", signal_name, path))
                            return false;
                    }
                }
            }
            else if (SDL_strcmp(type, "motion.scroll_wrap") == 0)
            {
                if (!is_axis_name(json_string(component, "axis")))
                    return validation_error(ctx, path, "motion.scroll_wrap requires axis x, y, or z");
                if (!yyjson_is_num(obj_get(component, "speed")))
                    return validation_error(ctx, path, "motion.scroll_wrap requires numeric speed");
                if (!yyjson_is_num(obj_get(component, "min")) || !yyjson_is_num(obj_get(component, "max")))
                    return validation_error(ctx, path, "motion.scroll_wrap requires numeric min and max");
            }
            else if (SDL_strcmp(type, "motion.grid_agent") == 0)
            {
                if (!require_ref(ctx, &names->grid_maps, "grid map", json_string(component, "map"), path))
                    return false;
                yyjson_val *speed = obj_get(component, "speed");
                if (speed != NULL && (!yyjson_is_num(speed) || yyjson_get_num(speed) < 0.0))
                    return validation_error(ctx, path, "motion.grid_agent speed must be a non-negative number");
            }
            else if (SDL_strcmp(type, "motion.velocity_2d") == 0 || SDL_strcmp(type, "motion.velocity_3d") == 0)
            {
                yyjson_val *property = obj_get(component, "property");
                if (property != NULL && !is_non_empty_string(component, "property"))
                    return validation_error(ctx, path, "%s property must be non-empty", type);
            }
            else if (SDL_strcmp(type, "motion.brush_velocity_3d") == 0)
            {
                if (!validate_brush_velocity_component(ctx, component, path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "lifecycle.ttl") == 0)
            {
                yyjson_val *ttl = obj_get(component, "ttl");
                if (ttl != NULL && (!yyjson_is_num(ttl) || yyjson_get_num(ttl) <= 0.0))
                    return validation_error(ctx, path, "lifecycle.ttl ttl must be positive");
                yyjson_val *age_property = obj_get(component, "age_property");
                yyjson_val *ttl_property = obj_get(component, "ttl_property");
                yyjson_val *reason = obj_get(component, "reason");
                if ((age_property != NULL && !is_non_empty_string(component, "age_property")) ||
                    (ttl_property != NULL && !is_non_empty_string(component, "ttl_property")) ||
                    (reason != NULL && !is_non_empty_string(component, "reason")))
                {
                    return validation_error(ctx, path,
                                            "lifecycle.ttl property names and reason must be non-empty strings");
                }
            }
            else if (SDL_strcmp(type, "particles.emitter") == 0)
            {
                if (!validate_particle_emitter_component(ctx, component, path, names))
                    return false;
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
            else if (SDL_strcmp(type, "motion.sector_velocity_3d") == 0)
            {
                if (!require_ref(ctx, &names->sector_levels, "sector level", json_string(component, "sector_level"),
                                 path))
                    return false;
                yyjson_val *property = obj_get(component, "property");
                if (property != NULL && !is_non_empty_string(component, "property"))
                    return validation_error(ctx, path, "motion.sector_velocity_3d property must be non-empty");
                yyjson_val *despawn_on_hit = obj_get(component, "despawn_on_hit");
                if (despawn_on_hit != NULL && !yyjson_is_bool(despawn_on_hit))
                    return validation_error(ctx, path, "motion.sector_velocity_3d despawn_on_hit must be a boolean");
                yyjson_val *reason = obj_get(component, "reason");
                if (reason != NULL && !is_non_empty_string(component, "reason"))
                    return validation_error(ctx, path, "motion.sector_velocity_3d reason must be non-empty");
            }
            else if (SDL_strncmp(type, "light.", 6) == 0)
            {
                yyjson_val *color = obj_get(component, "color");
                if (color != NULL && !is_vec_array(color, 3))
                    return validation_error(ctx, path, "light component color must be a vec3");
                yyjson_val *enabled = obj_get(component, "enabled");
                if (enabled != NULL && !yyjson_is_bool(enabled))
                    return validation_error(ctx, path, "light component enabled must be a boolean");
                yyjson_val *enabled_key = obj_get(component, "enabled_key");
                if (enabled_key != NULL && !is_non_empty_string(component, "enabled_key"))
                    return validation_error(ctx, path, "light component enabled_key must be non-empty");
            }
            else if (SDL_strcmp(type, "render.cube") == 0 || SDL_strcmp(type, "render.sphere") == 0 ||
                     SDL_strcmp(type, "render.mesh_primitive") == 0 || SDL_strcmp(type, "render.composite") == 0 ||
                     SDL_strcmp(type, "render.sprite") == 0 || SDL_strcmp(type, "render.model") == 0)
            {
                yyjson_val *lighting = obj_get(component, "lighting");
                if (lighting != NULL && !yyjson_is_bool(lighting))
                    return validation_error(ctx, path, "render primitive lighting must be a boolean");
                yyjson_val *lighting_key = obj_get(component, "lighting_key");
                if (lighting_key != NULL && !is_non_empty_string(component, "lighting_key"))
                    return validation_error(ctx, path, "render primitive lighting_key must be non-empty");
                yyjson_val *lod = obj_get(component, "lod");
                if (lod != NULL && !yyjson_is_bool(lod))
                    return validation_error(ctx, path, "render primitive lod must be a boolean");
                yyjson_val *lod_bias = obj_get(component, "lod_bias");
                if (lod_bias != NULL && (!yyjson_is_num(lod_bias) || yyjson_get_num(lod_bias) <= 0.0))
                    return validation_error(ctx, path, "render primitive lod_bias must be a positive number");
                yyjson_val *lod_cull_pixels = obj_get(component, "lod_cull_pixels");
                if (lod_cull_pixels != NULL &&
                    (!yyjson_is_num(lod_cull_pixels) || yyjson_get_num(lod_cull_pixels) < 0.0))
                    return validation_error(ctx, path,
                                            "render primitive lod_cull_pixels must be a non-negative number");
                const char *offset_properties[] = {"offset_x_property",     "offset_y_property",
                                                   "offset_z_property",     "offset_x_add_property",
                                                   "offset_y_add_property", "offset_z_add_property"};
                for (size_t i = 0; i < SDL_arraysize(offset_properties); ++i)
                {
                    yyjson_val *offset_property = obj_get(component, offset_properties[i]);
                    if (offset_property != NULL && !is_non_empty_string(component, offset_properties[i]))
                        return validation_error(ctx, path, "render primitive offset property must be non-empty");
                }
                const char *offset_property_arrays[] = {"offset_x_add_properties", "offset_y_add_properties",
                                                        "offset_z_add_properties"};
                for (size_t i = 0; i < SDL_arraysize(offset_property_arrays); ++i)
                {
                    if (!validate_property_name_array_field(ctx, component, path, offset_property_arrays[i],
                                                            "render primitive offset property arrays"))
                        return false;
                }
                if (!validate_render_camera_visibility_field(ctx, component, path, names, "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, path, names, "hidden_from_cameras"))
                {
                    return false;
                }
                if (SDL_strcmp(type, "render.cube") == 0 || SDL_strcmp(type, "render.sphere") == 0 ||
                    SDL_strcmp(type, "render.mesh_primitive") == 0 || SDL_strcmp(type, "render.composite") == 0)
                {
                    yyjson_val *texture_value = obj_get(component, "texture");
                    if (texture_value != NULL && !is_non_empty_string(component, "texture"))
                        return validation_error(ctx, path,
                                                "render primitive texture must be a non-empty image asset id");
                    const char *texture = json_string(component, "texture");
                    if (texture != NULL && !require_ref(ctx, &names->images, "image asset", texture, path))
                        return false;
                }
                if (SDL_strcmp(type, "render.cube") == 0)
                {
                    yyjson_val *size = obj_get(component, "size");
                    if (size != NULL && !is_vec_array(size, 3))
                        return validation_error(ctx, path, "render.cube size must be a vec3");
                    yyjson_val *size_property = obj_get(component, "size_property");
                    if (size_property != NULL && !is_non_empty_string(component, "size_property"))
                        return validation_error(ctx, path, "render.cube size_property must be non-empty");
                }
                if (SDL_strcmp(type, "render.mesh_primitive") == 0)
                {
                    if (!validate_render_mesh_primitive_component(ctx, component, path, names))
                        return false;
                }
                if (SDL_strcmp(type, "render.composite") == 0)
                {
                    if (!validate_render_composite_component(ctx, component, path, names))
                        return false;
                }
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
                }
                if (SDL_strcmp(type, "render.sprite") == 0)
                {
                    if (!require_ref(ctx, &names->sprites, "sprite asset", json_string(component, "sprite"), path))
                        return false;
                    yyjson_val *size = obj_get(component, "size");
                    if (size != NULL && !is_vec_array(size, 2))
                        return validation_error(ctx, path, "render.sprite size must be a vec2");
                    yyjson_val *facing_yaw = obj_get(component, "facing_yaw");
                    if (facing_yaw != NULL && !yyjson_is_num(facing_yaw))
                        return validation_error(ctx, path, "render.sprite facing_yaw must be a number");
                    yyjson_val *facing_yaw_property = obj_get(component, "facing_yaw_property");
                    if (facing_yaw_property != NULL && !is_non_empty_string(component, "facing_yaw_property"))
                        return validation_error(ctx, path, "render.sprite facing_yaw_property must be non-empty");
                }
                if (SDL_strcmp(type, "render.model") == 0)
                {
                    if (!require_ref(ctx, &names->models, "model asset", json_string(component, "model"), path))
                        return false;
                    yyjson_val *scale = obj_get(component, "scale");
                    if (scale != NULL && !is_vec_array(scale, 3))
                        return validation_error(ctx, path, "render.model scale must be a vec3");
                    yyjson_val *space = obj_get(component, "space");
                    if (space != NULL && (!yyjson_is_str(space) || (SDL_strcmp(yyjson_get_str(space), "world") != 0 &&
                                                                    SDL_strcmp(yyjson_get_str(space), "camera") != 0)))
                        return validation_error(ctx, path, "render.model space must be 'world' or 'camera'");
                    yyjson_val *rotation = obj_get(component, "rotation");
                    if (rotation != NULL && !is_vec_array(rotation, 3))
                        return validation_error(ctx, path, "render.model rotation must be a vec3");
                    const char *property_fields[] = {"scale_property",   "pitch_property",     "yaw_property",
                                                     "roll_property",    "pitch_add_property", "yaw_add_property",
                                                     "roll_add_property"};
                    for (size_t property_index = 0; property_index < SDL_arraysize(property_fields); ++property_index)
                    {
                        yyjson_val *property = obj_get(component, property_fields[property_index]);
                        if (property != NULL && !is_non_empty_string(component, property_fields[property_index]))
                            return validation_error(ctx, path, "render.model property fields must be non-empty");
                    }
                    const char *property_arrays[] = {"pitch_add_properties", "yaw_add_properties",
                                                     "roll_add_properties"};
                    for (size_t property_index = 0; property_index < SDL_arraysize(property_arrays); ++property_index)
                    {
                        if (!validate_property_name_array_field(ctx, component, path, property_arrays[property_index],
                                                                "render.model property arrays"))
                            return false;
                    }
                    yyjson_val *animation_clip = obj_get(component, "animation_clip");
                    if (animation_clip != NULL &&
                        (!yyjson_is_int(animation_clip) || yyjson_get_int(animation_clip) < 0))
                        return validation_error(ctx, path,
                                                "render.model animation_clip must be a non-negative integer");
                    yyjson_val *animation_time = obj_get(component, "animation_time");
                    if (animation_time != NULL && !yyjson_is_num(animation_time))
                        return validation_error(ctx, path, "render.model animation_time must be a number");
                    yyjson_val *animation_time_property = obj_get(component, "animation_time_property");
                    if (animation_time_property != NULL && !is_non_empty_string(component, "animation_time_property"))
                        return validation_error(ctx, path, "render.model animation_time_property must be non-empty");
                    yyjson_val *animation_loop = obj_get(component, "animation_loop");
                    if (animation_loop != NULL && !yyjson_is_bool(animation_loop))
                        return validation_error(ctx, path, "render.model animation_loop must be a boolean");
                }
            }
            else if (SDL_strcmp(type, "viewmodel.bob") == 0)
            {
                if (!require_actor_ref(ctx, names, json_string(component, "source"), path))
                    return false;
                const char *property_fields[] = {
                    "previous_position_property", "phase_property", "offset_x_property", "offset_y_property",
                    "offset_z_property",          "pitch_property", "yaw_property",      "roll_property"};
                for (size_t property_index = 0; property_index < SDL_arraysize(property_fields); ++property_index)
                {
                    yyjson_val *property = obj_get(component, property_fields[property_index]);
                    if (property != NULL && !is_non_empty_string(component, property_fields[property_index]))
                        return validation_error(ctx, path, "viewmodel.bob property fields must be non-empty");
                }
                yyjson_val *offset_amplitude = obj_get(component, "offset_amplitude");
                if (offset_amplitude != NULL && !is_vec_array(offset_amplitude, 3))
                    return validation_error(ctx, path, "viewmodel.bob offset_amplitude must be a vec3");
                const char *non_negative[] = {"frequency", "speed_scale", "min_speed", "settle_rate"};
                for (size_t tuning_index = 0; tuning_index < SDL_arraysize(non_negative); ++tuning_index)
                {
                    yyjson_val *value = obj_get(component, non_negative[tuning_index]);
                    if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0))
                        return validation_error(ctx, path, "viewmodel.bob numeric tuning values must be non-negative");
                }
                const char *numeric[] = {"pitch_amplitude", "yaw_amplitude", "roll_amplitude"};
                for (size_t tuning_index = 0; tuning_index < SDL_arraysize(numeric); ++tuning_index)
                {
                    yyjson_val *value = obj_get(component, numeric[tuning_index]);
                    if (value != NULL && !yyjson_is_num(value))
                        return validation_error(ctx, path, "viewmodel.bob angular amplitudes must be numbers");
                }
            }
        }
    }
    return true;
}

static yyjson_val *find_sector_level_json(yyjson_val *root, const char *level_name)
{
    yyjson_val *levels = obj_get(root, "sector_levels");
    for (size_t i = 0; level_name != NULL && yyjson_is_arr(levels) && i < yyjson_arr_size(levels); ++i)
    {
        yyjson_val *level = yyjson_arr_get(levels, i);
        const char *name = json_string(level, "name");
        if (name != NULL && SDL_strcmp(name, level_name) == 0)
            return level;
    }
    return NULL;
}

static bool sector_level_has_sector_name(yyjson_val *root, const char *level_name, const char *sector_name)
{
    yyjson_val *level = find_sector_level_json(root, level_name);
    yyjson_val *sectors = obj_get(level, "sectors");
    for (size_t i = 0; sector_name != NULL && yyjson_is_arr(sectors) && i < yyjson_arr_size(sectors); ++i)
    {
        const char *name = json_string(yyjson_arr_get(sectors, i), "name");
        if (name != NULL && SDL_strcmp(name, sector_name) == 0)
            return true;
    }
    return false;
}

static bool sector_level_has_sector_index(yyjson_val *root, const char *level_name, int sector_index)
{
    yyjson_val *level = find_sector_level_json(root, level_name);
    yyjson_val *sectors = obj_get(level, "sectors");
    return yyjson_is_arr(sectors) && sector_index >= 0 && (size_t)sector_index < yyjson_arr_size(sectors);
}

static bool validate_sector_navigation(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *graphs = obj_get(root, "sector_navigation");
    if (graphs == NULL)
        return true;
    if (!yyjson_is_arr(graphs))
        return validation_error(ctx, "$.sector_navigation", "sector_navigation must be an array");

    for (size_t graph_index = 0; graph_index < yyjson_arr_size(graphs); ++graph_index)
    {
        char graph_path[PATH_BUFFER_SIZE];
        format_path(graph_path, sizeof(graph_path), "$.sector_navigation[%zu]", graph_index);
        yyjson_val *graph = yyjson_arr_get(graphs, graph_index);
        yyjson_val *nodes = obj_get(graph, "nodes");
        yyjson_val *links = obj_get(graph, "links");
        name_table node_names;
        SDL_zero(node_names);
        bool ok = true;

        if (!yyjson_is_obj(graph))
        {
            ok = validation_error(ctx, graph_path, "sector navigation graph entries must be objects");
            goto done;
        }
        const char *level = json_string(graph, "sector_level");
        if (!require_ref(ctx, &names->sector_levels, "sector level", level, graph_path))
        {
            ok = false;
            goto done;
        }
        if (!yyjson_is_arr(nodes) || yyjson_arr_size(nodes) <= 0 || yyjson_arr_size(nodes) > 1024)
        {
            ok = validation_error(ctx, graph_path, "sector navigation nodes must be a non-empty array of <=1024 nodes");
            goto done;
        }
        if (links != NULL && !yyjson_is_arr(links))
        {
            ok = validation_error(ctx, graph_path, "sector navigation links must be an array");
            goto done;
        }

        for (size_t node_index = 0; ok && node_index < yyjson_arr_size(nodes); ++node_index)
        {
            char node_path[PATH_BUFFER_SIZE];
            format_path(node_path, sizeof(node_path), "%s.nodes[%zu]", graph_path, node_index);
            yyjson_val *node = yyjson_arr_get(nodes, node_index);
            if (!yyjson_is_obj(node))
            {
                ok = validation_error(ctx, node_path, "sector navigation nodes must be objects");
                break;
            }
            if (!require_unique_name(ctx, &node_names, "sector navigation node", json_string(node, "name"), node_path))
            {
                ok = false;
                break;
            }
            if (!is_exact_vec_array(obj_get(node, "position"), 3))
            {
                ok = validation_error(ctx, node_path, "sector navigation node position must be a vec3");
                break;
            }
            const char *sector = json_string(node, "sector");
            yyjson_val *sector_index = obj_get(node, "sector_index");
            if ((sector == NULL && sector_index == NULL) || (sector != NULL && sector_index != NULL))
            {
                ok = validation_error(ctx, node_path,
                                      "sector navigation node requires exactly one of sector or sector_index");
                break;
            }
            if (sector != NULL && !sector_level_has_sector_name(root, level, sector))
            {
                ok = validation_error(ctx, node_path, "unknown sector navigation node sector '%s'", sector);
                break;
            }
            if (sector_index != NULL &&
                (!yyjson_is_int(sector_index) ||
                 !sector_level_has_sector_index(root, level, (int)yyjson_get_int(sector_index))))
            {
                ok = validation_error(ctx, node_path, "sector navigation node sector_index is out of range");
                break;
            }
        }

        for (size_t link_index = 0; ok && yyjson_is_arr(links) && link_index < yyjson_arr_size(links); ++link_index)
        {
            char link_path[PATH_BUFFER_SIZE];
            format_path(link_path, sizeof(link_path), "%s.links[%zu]", graph_path, link_index);
            yyjson_val *link = yyjson_arr_get(links, link_index);
            if (!yyjson_is_obj(link))
            {
                ok = validation_error(ctx, link_path, "sector navigation links must be objects");
                break;
            }
            if (!require_ref(ctx, &node_names, "sector navigation node", json_string(link, "from"), link_path) ||
                !require_ref(ctx, &node_names, "sector navigation node", json_string(link, "to"), link_path))
            {
                ok = false;
                break;
            }
            yyjson_val *bidirectional = obj_get(link, "bidirectional");
            yyjson_val *cost = obj_get(link, "cost");
            if (bidirectional != NULL && !yyjson_is_bool(bidirectional))
            {
                ok = validation_error(ctx, link_path, "sector navigation link bidirectional must be a boolean");
                break;
            }
            if (cost != NULL && (!yyjson_is_num(cost) || yyjson_get_num(cost) <= 0.0))
            {
                ok = validation_error(ctx, link_path, "sector navigation link cost must be positive");
                break;
            }
        }

    done:
        name_table_destroy(&node_names);
        if (!ok)
            return false;
    }
    return true;
}

static bool validate_sector_platforms(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *platforms = obj_get(root, "sector_platforms");
    if (platforms == NULL)
        return true;
    if (!yyjson_is_arr(platforms))
        return validation_error(ctx, "$.sector_platforms", "sector_platforms must be an array");

    for (size_t i = 0; i < yyjson_arr_size(platforms); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_platforms[%zu]", i);
        yyjson_val *platform = yyjson_arr_get(platforms, i);
        if (!yyjson_is_obj(platform))
            return validation_error(ctx, path, "sector platform entries must be objects");

        const char *scene = json_string(platform, "scene");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, path))
            return false;
        const char *sector_level = json_string(platform, "sector_level");
        if (!require_ref(ctx, &names->sector_levels, "sector level", sector_level, path))
            return false;

        const char *sector = json_string(platform, "sector");
        yyjson_val *sector_index = obj_get(platform, "sector_index");
        if ((sector == NULL && sector_index == NULL) || (sector != NULL && sector_index != NULL))
            return validation_error(ctx, path, "sector platform requires exactly one of sector or sector_index");
        if (sector != NULL && !sector_level_has_sector_name(root, sector_level, sector))
            return validation_error(ctx, path, "unknown sector platform sector '%s'", sector);
        if (sector_index != NULL &&
            (!yyjson_is_int(sector_index) ||
             !sector_level_has_sector_index(root, sector_level, (int)yyjson_get_int(sector_index))))
        {
            return validation_error(ctx, path, "sector platform sector_index must reference a sector in sector_level");
        }

        yyjson_val *min_floor_y = obj_get(platform, "min_floor_y");
        yyjson_val *max_floor_y = obj_get(platform, "max_floor_y");
        yyjson_val *ceil_y = obj_get(platform, "ceil_y");
        yyjson_val *cycle_seconds = obj_get(platform, "cycle_seconds");
        yyjson_val *rebuild_min_delta = obj_get(platform, "rebuild_min_delta");
        yyjson_val *crush_damage = obj_get(platform, "crush_damage_per_second");
        yyjson_val *crush_clearance = obj_get(platform, "crush_clearance");
        if (!yyjson_is_num(min_floor_y) || !yyjson_is_num(max_floor_y) || !yyjson_is_num(ceil_y))
            return validation_error(ctx, path, "sector platform requires numeric min_floor_y, max_floor_y, and ceil_y");
        const double min_y = yyjson_get_num(min_floor_y);
        const double max_y = yyjson_get_num(max_floor_y);
        const double ceiling = yyjson_get_num(ceil_y);
        if (max_y < min_y)
            return validation_error(ctx, path,
                                    "sector platform max_floor_y must be greater than or equal to min_floor_y");
        if (ceiling <= max_y)
            return validation_error(ctx, path, "sector platform ceil_y must be greater than max_floor_y");
        if (!yyjson_is_num(cycle_seconds) || yyjson_get_num(cycle_seconds) <= 0.0)
            return validation_error(ctx, path, "sector platform cycle_seconds must be positive");
        if (rebuild_min_delta != NULL && (!yyjson_is_num(rebuild_min_delta) || yyjson_get_num(rebuild_min_delta) < 0.0))
            return validation_error(ctx, path, "sector platform rebuild_min_delta must be non-negative");
        if (crush_damage != NULL && (!yyjson_is_num(crush_damage) || yyjson_get_num(crush_damage) < 0.0))
            return validation_error(ctx, path, "sector platform crush_damage_per_second must be non-negative");
        if (crush_clearance != NULL && (!yyjson_is_num(crush_clearance) || yyjson_get_num(crush_clearance) < 0.0))
            return validation_error(ctx, path, "sector platform crush_clearance must be non-negative");
        yyjson_val *enabled = obj_get(platform, "enabled");
        if (enabled != NULL && !yyjson_is_bool(enabled))
            return validation_error(ctx, path, "sector platform enabled must be a boolean");
        yyjson_val *crush_when_descending = obj_get(platform, "crush_when_descending");
        if (crush_when_descending != NULL && !yyjson_is_bool(crush_when_descending))
            return validation_error(ctx, path, "sector platform crush_when_descending must be a boolean");
        yyjson_val *deactivate_on_death = obj_get(platform, "deactivate_on_death");
        if (deactivate_on_death != NULL && !yyjson_is_bool(deactivate_on_death))
            return validation_error(ctx, path, "sector platform deactivate_on_death must be a boolean");
        yyjson_val *armor_absorb = obj_get(platform, "armor_absorb");
        if (armor_absorb != NULL &&
            (!yyjson_is_num(armor_absorb) || yyjson_get_num(armor_absorb) < 0.0 || yyjson_get_num(armor_absorb) > 1.0))
        {
            return validation_error(ctx, path, "sector platform armor_absorb must be in 0..1");
        }
        if (!validate_non_empty_string_field(ctx, platform, path, "sector platform", "crush_actor_tag") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "actor_tag") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "damage_type") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "health_property") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "max_health_property") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "armor_property") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "armor_absorb_property") ||
            !validate_non_empty_string_field(ctx, platform, path, "sector platform", "alive_property"))
        {
            return false;
        }
        if (!validate_target_filter_fields(ctx, platform, path, "sector platform"))
            return false;
        yyjson_val *actions = obj_get(platform, "crush_actions");
        if (actions != NULL && !validate_action_array(ctx, actions, path, names))
            return false;
        if (!validate_optional_signal_field(ctx, platform, path, names, "on_crush") ||
            !validate_optional_signal_field(ctx, platform, path, names, "on_damage") ||
            !validate_optional_signal_field(ctx, platform, path, names, "on_death"))
            return false;
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
            if (SDL_strcmp(type, "controller.fps_sector") == 0 || SDL_strcmp(type, "controller.fps_brush") == 0 ||
                SDL_strcmp(type, "controller.editor_camera") == 0)
            {
                return validation_error(ctx, component_path,
                                        "camera and first-person controllers are only supported on static entities");
            }
            else if (SDL_strcmp(type, "combat.health") == 0)
            {
                if (!validate_combat_health_component(ctx, component, component_path))
                    return false;
            }
            else if (SDL_strcmp(type, "pickup.respawn") == 0)
            {
                if (!validate_pickup_respawn_component(ctx, component, component_path))
                    return false;
            }
            else if (SDL_strcmp(type, "status_effect.timer") == 0)
            {
                if (!validate_status_effect_timer_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "weapon.state") == 0)
            {
                if (!validate_weapon_state_component(ctx, component, component_path))
                    return false;
            }
            else if (SDL_strcmp(type, "interactable") == 0)
            {
                if (!validate_interactable_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "weapon.projectile") == 0)
            {
                if (!require_ref(ctx, &names->actions, "input action", json_string(component, "action"),
                                 component_path) ||
                    !validate_projectile_fire_shape(ctx, component, component_path, names, false))
                {
                    return false;
                }
            }
            else if (SDL_strcmp(type, "particles.emitter") == 0)
            {
                if (!validate_particle_emitter_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "motion.grid_agent") == 0)
            {
                if (!require_ref(ctx, &names->grid_maps, "grid map", json_string(component, "map"), component_path))
                    return false;
                yyjson_val *speed = obj_get(component, "speed");
                if (speed != NULL && (!yyjson_is_num(speed) || yyjson_get_num(speed) < 0.0))
                    return validation_error(ctx, component_path,
                                            "motion.grid_agent speed must be a non-negative number");
            }
            else if (SDL_strcmp(type, "motion.velocity_2d") == 0 || SDL_strcmp(type, "motion.velocity_3d") == 0)
            {
                yyjson_val *property = obj_get(component, "property");
                if (property != NULL && !is_non_empty_string(component, "property"))
                    return validation_error(ctx, component_path, "%s property must be non-empty", type);
            }
            else if (SDL_strcmp(type, "motion.brush_velocity_3d") == 0)
            {
                if (!validate_brush_velocity_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "motion.sector_velocity_3d") == 0)
            {
                if (!require_ref(ctx, &names->sector_levels, "sector level", json_string(component, "sector_level"),
                                 component_path))
                    return false;
                yyjson_val *property = obj_get(component, "property");
                if (property != NULL && !is_non_empty_string(component, "property"))
                    return validation_error(ctx, component_path,
                                            "motion.sector_velocity_3d property must be non-empty");
                yyjson_val *despawn_on_hit = obj_get(component, "despawn_on_hit");
                if (despawn_on_hit != NULL && !yyjson_is_bool(despawn_on_hit))
                    return validation_error(ctx, component_path,
                                            "motion.sector_velocity_3d despawn_on_hit must be a boolean");
                yyjson_val *reason = obj_get(component, "reason");
                if (reason != NULL && !is_non_empty_string(component, "reason"))
                    return validation_error(ctx, component_path, "motion.sector_velocity_3d reason must be non-empty");
            }
            else if (SDL_strcmp(type, "lifecycle.ttl") == 0)
            {
                yyjson_val *ttl = obj_get(component, "ttl");
                if (ttl != NULL && (!yyjson_is_num(ttl) || yyjson_get_num(ttl) <= 0.0))
                    return validation_error(ctx, component_path, "lifecycle.ttl ttl must be positive");
                yyjson_val *age_property = obj_get(component, "age_property");
                yyjson_val *ttl_property = obj_get(component, "ttl_property");
                yyjson_val *reason = obj_get(component, "reason");
                if ((age_property != NULL && !is_non_empty_string(component, "age_property")) ||
                    (ttl_property != NULL && !is_non_empty_string(component, "ttl_property")) ||
                    (reason != NULL && !is_non_empty_string(component, "reason")))
                {
                    return validation_error(ctx, component_path,
                                            "lifecycle.ttl property names and reason must be non-empty strings");
                }
            }
            else if (SDL_strncmp(type, "light.", 6) == 0)
            {
                yyjson_val *color = obj_get(component, "color");
                if (color != NULL && !is_vec_array(color, 3))
                    return validation_error(ctx, component_path, "light component color must be a vec3");
                yyjson_val *enabled = obj_get(component, "enabled");
                if (enabled != NULL && !yyjson_is_bool(enabled))
                    return validation_error(ctx, component_path, "light component enabled must be a boolean");
                yyjson_val *enabled_key = obj_get(component, "enabled_key");
                if (enabled_key != NULL && !is_non_empty_string(component, "enabled_key"))
                    return validation_error(ctx, component_path, "light component enabled_key must be non-empty");
            }
            else if (SDL_strcmp(type, "render.cube") == 0)
            {
                yyjson_val *lighting = obj_get(component, "lighting");
                if (lighting != NULL && !yyjson_is_bool(lighting))
                    return validation_error(ctx, component_path, "render primitive lighting must be a boolean");
                yyjson_val *lighting_key = obj_get(component, "lighting_key");
                if (lighting_key != NULL && !is_non_empty_string(component, "lighting_key"))
                    return validation_error(ctx, component_path, "render primitive lighting_key must be non-empty");
                if (!validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "hidden_from_cameras"))
                {
                    return false;
                }
                yyjson_val *size = obj_get(component, "size");
                if (size != NULL && !is_vec_array(size, 3))
                    return validation_error(ctx, component_path, "render.cube size must be a vec3");
                yyjson_val *size_property = obj_get(component, "size_property");
                if (size_property != NULL && !is_non_empty_string(component, "size_property"))
                    return validation_error(ctx, component_path, "render.cube size_property must be non-empty");
                yyjson_val *texture_value = obj_get(component, "texture");
                if (texture_value != NULL && !is_non_empty_string(component, "texture"))
                    return validation_error(ctx, component_path,
                                            "render.cube texture must be a non-empty image asset id");
                const char *texture = json_string(component, "texture");
                if (texture != NULL && !require_ref(ctx, &names->images, "image asset", texture, component_path))
                    return false;
            }
            else if (SDL_strcmp(type, "render.mesh_primitive") == 0)
            {
                if (!validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "hidden_from_cameras"))
                {
                    return false;
                }
                if (!validate_render_mesh_primitive_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "render.composite") == 0)
            {
                if (!validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "hidden_from_cameras"))
                {
                    return false;
                }
                if (!validate_render_composite_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "render.sprite") == 0)
            {
                if (!validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "hidden_from_cameras"))
                {
                    return false;
                }
                if (!require_ref(ctx, &names->sprites, "sprite asset", json_string(component, "sprite"),
                                 component_path))
                    return false;
                yyjson_val *size = obj_get(component, "size");
                if (size != NULL && !is_vec_array(size, 2))
                    return validation_error(ctx, component_path, "render.sprite size must be a vec2");
                yyjson_val *facing_yaw = obj_get(component, "facing_yaw");
                if (facing_yaw != NULL && !yyjson_is_num(facing_yaw))
                    return validation_error(ctx, component_path, "render.sprite facing_yaw must be a number");
                yyjson_val *facing_yaw_property = obj_get(component, "facing_yaw_property");
                if (facing_yaw_property != NULL && !is_non_empty_string(component, "facing_yaw_property"))
                    return validation_error(ctx, component_path, "render.sprite facing_yaw_property must be non-empty");
            }
            else if (SDL_strcmp(type, "render.model") == 0)
            {
                if (!validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "hidden_from_cameras"))
                {
                    return false;
                }
                if (!require_ref(ctx, &names->models, "model asset", json_string(component, "model"), component_path))
                    return false;
                yyjson_val *scale = obj_get(component, "scale");
                if (scale != NULL && !is_vec_array(scale, 3))
                    return validation_error(ctx, component_path, "render.model scale must be a vec3");
                yyjson_val *space = obj_get(component, "space");
                if (space != NULL && (!yyjson_is_str(space) || (SDL_strcmp(yyjson_get_str(space), "world") != 0 &&
                                                                SDL_strcmp(yyjson_get_str(space), "camera") != 0)))
                    return validation_error(ctx, component_path, "render.model space must be 'world' or 'camera'");
                yyjson_val *rotation = obj_get(component, "rotation");
                if (rotation != NULL && !is_vec_array(rotation, 3))
                    return validation_error(ctx, component_path, "render.model rotation must be a vec3");
                yyjson_val *lod_cull_pixels = obj_get(component, "lod_cull_pixels");
                if (lod_cull_pixels != NULL &&
                    (!yyjson_is_num(lod_cull_pixels) || yyjson_get_num(lod_cull_pixels) < 0.0))
                    return validation_error(ctx, component_path,
                                            "render.model lod_cull_pixels must be a non-negative number");
                const char *property_fields[] = {
                    "scale_property",       "pitch_property",    "yaw_property",          "roll_property",
                    "pitch_add_property",   "yaw_add_property",  "roll_add_property",     "offset_x_property",
                    "offset_y_property",    "offset_z_property", "offset_x_add_property", "offset_y_add_property",
                    "offset_z_add_property"};
                for (size_t property_index = 0; property_index < SDL_arraysize(property_fields); ++property_index)
                {
                    yyjson_val *property = obj_get(component, property_fields[property_index]);
                    if (property != NULL && !is_non_empty_string(component, property_fields[property_index]))
                        return validation_error(ctx, component_path, "render.model property fields must be non-empty");
                }
                const char *property_arrays[] = {"offset_x_add_properties", "offset_y_add_properties",
                                                 "offset_z_add_properties", "pitch_add_properties",
                                                 "yaw_add_properties",      "roll_add_properties"};
                for (size_t property_index = 0; property_index < SDL_arraysize(property_arrays); ++property_index)
                {
                    if (!validate_property_name_array_field(ctx, component, component_path,
                                                            property_arrays[property_index],
                                                            "render.model property arrays"))
                        return false;
                }
                yyjson_val *animation_clip = obj_get(component, "animation_clip");
                if (animation_clip != NULL && (!yyjson_is_int(animation_clip) || yyjson_get_int(animation_clip) < 0))
                    return validation_error(ctx, component_path,
                                            "render.model animation_clip must be a non-negative integer");
                yyjson_val *animation_time = obj_get(component, "animation_time");
                if (animation_time != NULL && !yyjson_is_num(animation_time))
                    return validation_error(ctx, component_path, "render.model animation_time must be a number");
                yyjson_val *animation_time_property = obj_get(component, "animation_time_property");
                if (animation_time_property != NULL && !is_non_empty_string(component, "animation_time_property"))
                    return validation_error(ctx, component_path,
                                            "render.model animation_time_property must be non-empty");
                yyjson_val *animation_loop = obj_get(component, "animation_loop");
                if (animation_loop != NULL && !yyjson_is_bool(animation_loop))
                    return validation_error(ctx, component_path, "render.model animation_loop must be a boolean");
            }
            else if (SDL_strcmp(type, "viewmodel.bob") == 0)
            {
                if (!require_actor_ref(ctx, names, json_string(component, "source"), component_path))
                    return false;
                const char *property_fields[] = {
                    "previous_position_property", "phase_property", "offset_x_property", "offset_y_property",
                    "offset_z_property",          "pitch_property", "yaw_property",      "roll_property"};
                for (size_t property_index = 0; property_index < SDL_arraysize(property_fields); ++property_index)
                {
                    yyjson_val *property = obj_get(component, property_fields[property_index]);
                    if (property != NULL && !is_non_empty_string(component, property_fields[property_index]))
                        return validation_error(ctx, component_path, "viewmodel.bob property fields must be non-empty");
                }
                yyjson_val *offset_amplitude = obj_get(component, "offset_amplitude");
                if (offset_amplitude != NULL && !is_vec_array(offset_amplitude, 3))
                    return validation_error(ctx, component_path, "viewmodel.bob offset_amplitude must be a vec3");
                const char *non_negative[] = {"frequency", "speed_scale", "min_speed", "settle_rate"};
                for (size_t bob_tuning_index = 0; bob_tuning_index < SDL_arraysize(non_negative); ++bob_tuning_index)
                {
                    yyjson_val *value = obj_get(component, non_negative[bob_tuning_index]);
                    if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0))
                        return validation_error(ctx, component_path,
                                                "viewmodel.bob numeric tuning values must be non-negative");
                }
                const char *numeric[] = {"pitch_amplitude", "yaw_amplitude", "roll_amplitude"};
                for (size_t bob_tuning_index = 0; bob_tuning_index < SDL_arraysize(numeric); ++bob_tuning_index)
                {
                    yyjson_val *value = obj_get(component, numeric[bob_tuning_index]);
                    if (value != NULL && !yyjson_is_num(value))
                        return validation_error(ctx, component_path,
                                                "viewmodel.bob angular amplitudes must be numbers");
                }
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
    else if (SDL_strcmp(type, "audio.set_ambient") == 0)
    {
        yyjson_val *ambient_id = obj_get(action, "ambient_id");
        const char *ambient_id_from_payload = json_string(action, "ambient_id_from_payload");
        if ((ambient_id == NULL) == (ambient_id_from_payload == NULL))
            return validation_error(ctx, json_path,
                                    "audio.set_ambient requires exactly one of ambient_id or ambient_id_from_payload");
        if (ambient_id != NULL && (!yyjson_is_int(ambient_id) || yyjson_get_int(ambient_id) < 0))
            return validation_error(ctx, json_path, "audio.set_ambient ambient_id must be non-negative");
        if (ambient_id_from_payload != NULL && ambient_id_from_payload[0] == '\0')
            return validation_error(ctx, json_path, "audio.set_ambient ambient_id_from_payload must be non-empty");
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

static bool validate_combat_target_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type)
{
    yyjson_val *target_value = obj_get(action, "target");
    yyjson_val *target_from_payload_value = obj_get(action, "target_from_payload");
    const char *target = json_string(action, "target");
    const char *target_from_payload = json_string(action, "target_from_payload");
    if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of target or target_from_payload", type);
    if (target_value != NULL && !yyjson_is_str(target_value))
        return validation_error(ctx, json_path, "%s target must be a string", type);
    if (target_from_payload_value != NULL && !yyjson_is_str(target_from_payload_value))
        return validation_error(ctx, json_path, "%s target_from_payload must be a string", type);
    if (target != NULL && target[0] == '\0')
        return validation_error(ctx, json_path, "%s target must be non-empty", type);
    if (target_from_payload != NULL && target_from_payload[0] == '\0')
        return validation_error(ctx, json_path, "%s target_from_payload must be non-empty", type);
    if (target != NULL && !name_table_contains(&names->entities, target) &&
        !name_table_contains(&names->actor_pool_actors, target))
    {
        return validation_error(ctx, json_path, "unknown %s target '%s'", type, target);
    }

    const char *source = json_string(action, "source");
    if (source != NULL && source[0] == '\0')
        return validation_error(ctx, json_path, "%s source must be non-empty", type);
    if (source != NULL && !name_table_contains(&names->entities, source) &&
        !name_table_contains(&names->actor_pool_actors, source))
    {
        return validation_error(ctx, json_path, "unknown %s source '%s'", type, source);
    }
    yyjson_val *source_from_payload = obj_get(action, "source_from_payload");
    if (source_from_payload != NULL &&
        (!yyjson_is_str(source_from_payload) || yyjson_get_str(source_from_payload)[0] == '\0'))
        return validation_error(ctx, json_path, "%s source_from_payload must be a non-empty string", type);

    const char *property_keys[] = {"health_property", "max_health_property", "armor_property", "armor_absorb_property",
                                   "alive_property"};
    for (size_t i = 0; i < SDL_arraysize(property_keys); ++i)
    {
        yyjson_val *value = obj_get(action, property_keys[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, json_path, "%s property names must be non-empty strings", type);
    }

    const char *signal_keys[] = {"on_damage", "on_death", "on_heal", "on_revive"};
    for (size_t i = 0; i < SDL_arraysize(signal_keys); ++i)
    {
        const char *signal = json_string(action, signal_keys[i]);
        if (signal != NULL && !require_ref(ctx, &names->signals, "signal", signal, json_path))
            return false;
    }

    yyjson_val *deactivate_on_death = obj_get(action, "deactivate_on_death");
    yyjson_val *deactivate = obj_get(action, "deactivate");
    yyjson_val *revive = obj_get(action, "revive");
    if ((deactivate_on_death != NULL && !yyjson_is_bool(deactivate_on_death)) ||
        (deactivate != NULL && !yyjson_is_bool(deactivate)) || (revive != NULL && !yyjson_is_bool(revive)))
    {
        return validation_error(ctx, json_path, "%s boolean fields must be booleans", type);
    }

    yyjson_val *armor_absorb = obj_get(action, "armor_absorb");
    if (armor_absorb != NULL &&
        (!yyjson_is_num(armor_absorb) || yyjson_get_num(armor_absorb) < 0.0 || yyjson_get_num(armor_absorb) > 1.0))
    {
        return validation_error(ctx, json_path, "%s armor_absorb must be in 0..1", type);
    }
    yyjson_val *damage_type = obj_get(action, "damage_type");
    if (damage_type != NULL && !yyjson_is_str(damage_type))
        return validation_error(ctx, json_path, "%s damage_type must be a string", type);
    return validate_target_filter_fields(ctx, action, json_path, type);
}

static bool validate_combat_amount_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type, const char *field,
                                          const char *payload_field)
{
    if (!validate_combat_target_action(ctx, action, json_path, names, type))
        return false;
    yyjson_val *amount = obj_get(action, field);
    yyjson_val *amount_from_payload_value = obj_get(action, payload_field);
    const char *amount_from_payload = json_string(action, payload_field);
    if ((amount == NULL && amount_from_payload == NULL) || (amount != NULL && amount_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of %s or %s", type, field, payload_field);
    if (amount != NULL && (!yyjson_is_num(amount) || yyjson_get_num(amount) < 0.0))
        return validation_error(ctx, json_path, "%s %s must be a non-negative number", type, field);
    if (amount_from_payload_value != NULL &&
        (!yyjson_is_str(amount_from_payload_value) || yyjson_get_str(amount_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "%s %s must be a non-empty string", type, payload_field);
    }
    return true;
}

static bool validate_actor_target_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         validation_names *names, const char *type, const char *target_key,
                                         const char *payload_key)
{
    yyjson_val *target_value = obj_get(action, target_key);
    yyjson_val *target_from_payload_value = obj_get(action, payload_key);
    const char *target = json_string(action, target_key);
    const char *target_from_payload = json_string(action, payload_key);
    if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of %s or %s", type, target_key, payload_key);
    if (target_value != NULL && !yyjson_is_str(target_value))
        return validation_error(ctx, json_path, "%s %s must be a string", type, target_key);
    if (target_from_payload_value != NULL && !yyjson_is_str(target_from_payload_value))
        return validation_error(ctx, json_path, "%s %s must be a string", type, payload_key);
    if (target != NULL && target[0] == '\0')
        return validation_error(ctx, json_path, "%s %s must be non-empty", type, target_key);
    if (target_from_payload != NULL && target_from_payload[0] == '\0')
        return validation_error(ctx, json_path, "%s %s must be non-empty", type, payload_key);
    if (target != NULL && !name_table_contains(&names->entities, target) &&
        !name_table_contains(&names->actor_pool_actors, target))
    {
        return validation_error(ctx, json_path, "unknown %s %s '%s'", type, target_key, target);
    }
    return true;
}

static bool validate_non_empty_string_field(validation_context *ctx, yyjson_val *json, const char *json_path,
                                            const char *type, const char *field)
{
    yyjson_val *value = obj_get(json, field);
    if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
        return validation_error(ctx, json_path, "%s %s must be a non-empty string", type, field);
    return true;
}

static bool validate_optional_signal_field(validation_context *ctx, yyjson_val *json, const char *json_path,
                                           validation_names *names, const char *field)
{
    const char *signal = json_string(json, field);
    return signal == NULL || require_ref(ctx, &names->signals, "signal", signal, json_path);
}

static bool faction_relationship_valid(const char *value)
{
    return value != NULL && (SDL_strcmp(value, "friendly") == 0 || SDL_strcmp(value, "hostile") == 0 ||
                             SDL_strcmp(value, "neutral") == 0 || SDL_strcmp(value, "ignored") == 0);
}

static bool target_filter_relationship_valid(const char *value)
{
    return value != NULL && (SDL_strcmp(value, "any") == 0 || faction_relationship_valid(value));
}

static bool validate_string_or_string_array(validation_context *ctx, yyjson_val *json, const char *json_path,
                                            const char *type, const char *field)
{
    yyjson_val *value = obj_get(json, field);
    if (value == NULL)
        return true;
    if (yyjson_is_str(value))
        return yyjson_get_str(value)[0] != '\0' ||
               validation_error(ctx, json_path, "%s %s must contain non-empty strings", type, field);
    if (!yyjson_is_arr(value))
        return validation_error(ctx, json_path, "%s %s must be a string or string array", type, field);
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (!yyjson_is_str(entry) || yyjson_get_str(entry)[0] == '\0')
            return validation_error(ctx, json_path, "%s %s must contain non-empty strings", type, field);
    }
    return true;
}

static bool validate_target_filter_fields(validation_context *ctx, yyjson_val *json, const char *json_path,
                                          const char *type)
{
    if (json == NULL)
        return true;
    yyjson_val *filter = obj_get(json, "target_filter");
    if (filter != NULL && !yyjson_is_obj(filter))
        return validation_error(ctx, json_path, "%s target_filter must be an object", type);
    yyjson_val *sources[] = {json, filter};
    for (size_t s = 0; s < SDL_arraysize(sources); ++s)
    {
        yyjson_val *source = sources[s];
        if (source == NULL)
            continue;
        const char *string_fields[] = {"target_tag",       "affected_tag",
                                       "hit_tag",          "target_faction",
                                       "source_faction",   "source_faction_from_payload",
                                       "faction_property", "source_faction_property",
                                       "owner_property",   "owner_actor_property"};
        for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
        {
            if (!validate_non_empty_string_field(ctx, source, json_path, type, string_fields[i]))
                return false;
        }
        const char *array_fields[] = {"include_tags", "exclude_tags", "include_factions", "exclude_factions"};
        for (size_t i = 0; i < SDL_arraysize(array_fields); ++i)
        {
            if (!validate_string_or_string_array(ctx, source, json_path, type, array_fields[i]))
                return false;
        }
        const char *bool_fields[] = {"exclude_source", "exclude_self", "exclude_owner"};
        for (size_t i = 0; i < SDL_arraysize(bool_fields); ++i)
        {
            yyjson_val *value = obj_get(source, bool_fields[i]);
            if (value != NULL && !yyjson_is_bool(value))
                return validation_error(ctx, json_path, "%s %s must be a boolean", type, bool_fields[i]);
        }
        const char *relationship = json_string(source, "relationship");
        if (relationship != NULL && !target_filter_relationship_valid(relationship))
            return validation_error(ctx, json_path,
                                    "%s relationship must be any, friendly, hostile, neutral, or ignored", type);
    }
    return true;
}

static bool validate_factions(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *factions = obj_get(root, "factions");
    if (factions == NULL)
        return true;
    if (!yyjson_is_obj(factions))
        return validation_error(ctx, "$.factions", "factions must be an object");
    const char *default_relationship = json_string(factions, "default_relationship");
    if (default_relationship != NULL && !faction_relationship_valid(default_relationship))
    {
        return validation_error(ctx, "$.factions",
                                "factions default_relationship must be friendly, hostile, neutral, or ignored");
    }

    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(factions, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        value = yyjson_obj_iter_get_val(key);
        if (SDL_strcmp(name != NULL ? name : "", "default_relationship") == 0)
            continue;
        if (name == NULL || name[0] == '\0')
            return validation_error(ctx, "$.factions", "faction names must be non-empty");
        if (!yyjson_is_obj(value))
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.factions.%s", name);
            return validation_error(ctx, path, "faction entries must be objects");
        }
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.factions.%s", name);
        yyjson_val *rel_key;
        yyjson_val *rel_value;
        yyjson_obj_iter rel_iter;
        yyjson_obj_iter_init(value, &rel_iter);
        while ((rel_key = yyjson_obj_iter_next(&rel_iter)) != NULL)
        {
            const char *target = yyjson_get_str(rel_key);
            rel_value = yyjson_obj_iter_get_val(rel_key);
            if (target == NULL || target[0] == '\0' || !yyjson_is_str(rel_value) ||
                !faction_relationship_valid(yyjson_get_str(rel_value)))
            {
                return validation_error(
                    ctx, path,
                    "faction relationships must map non-empty faction names to friendly, hostile, neutral, or ignored");
            }
        }
    }
    return true;
}

static bool validate_resource_grant(validation_context *ctx, yyjson_val *grant, const char *json_path,
                                    validation_names *names, const char *owner_type)
{
    (void)names;
    if (!yyjson_is_obj(grant))
        return validation_error(ctx, json_path, "%s resources entries must be objects", owner_type);
    if (!is_non_empty_string(grant, "resource"))
        return validation_error(ctx, json_path, "%s resource grants require a non-empty resource", owner_type);
    if (!validate_non_empty_string_field(ctx, grant, json_path, owner_type, "property") ||
        !validate_non_empty_string_field(ctx, grant, json_path, owner_type, "max_property"))
    {
        return false;
    }

    yyjson_val *amount = obj_get(grant, "amount");
    yyjson_val *amount_from_payload_value = obj_get(grant, "amount_from_payload");
    const char *amount_from_payload = json_string(grant, "amount_from_payload");
    if ((amount == NULL && amount_from_payload == NULL) || (amount != NULL && amount_from_payload != NULL))
        return validation_error(ctx, json_path,
                                "%s resource grants require exactly one of amount or amount_from_payload", owner_type);
    if (amount != NULL && (!yyjson_is_num(amount) || yyjson_get_num(amount) < 0.0))
        return validation_error(ctx, json_path, "%s resource grant amount must be a non-negative number", owner_type);
    if (amount_from_payload_value != NULL &&
        (!yyjson_is_str(amount_from_payload_value) || yyjson_get_str(amount_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "%s amount_from_payload must be a non-empty string", owner_type);
    }
    yyjson_val *max = obj_get(grant, "max");
    yyjson_val *min = obj_get(grant, "min");
    yyjson_val *clamp = obj_get(grant, "clamp");
    if ((max != NULL && !yyjson_is_num(max)) || (min != NULL && !yyjson_is_num(min)))
        return validation_error(ctx, json_path, "%s resource grant min/max must be numbers", owner_type);
    if (clamp != NULL && !yyjson_is_bool(clamp))
        return validation_error(ctx, json_path, "%s resource grant clamp must be a boolean", owner_type);
    return true;
}

static bool validate_resource_grants(validation_context *ctx, yyjson_val *action, const char *json_path,
                                     validation_names *names, const char *type)
{
    yyjson_val *resources = obj_get(action, "resources");
    if (!yyjson_is_arr(resources) || yyjson_arr_size(resources) == 0)
        return validation_error(ctx, json_path, "%s requires a non-empty resources array", type);
    for (size_t i = 0; i < yyjson_arr_size(resources); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.resources[%zu]", json_path, i);
        if (!validate_resource_grant(ctx, yyjson_arr_get(resources, i), path, names, type))
            return false;
    }
    return true;
}

static bool validate_resource_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                     validation_names *names, const char *type, const char *value_key,
                                     const char *payload_key)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, type, "target", "target_from_payload"))
        return false;
    if (!is_non_empty_string(action, "resource"))
        return validation_error(ctx, json_path, "%s requires a non-empty resource", type);
    if (!validate_non_empty_string_field(ctx, action, json_path, type, "property") ||
        !validate_non_empty_string_field(ctx, action, json_path, type, "max_property"))
    {
        return false;
    }

    yyjson_val *value = obj_get(action, value_key);
    yyjson_val *value_from_payload_value = obj_get(action, payload_key);
    const char *value_from_payload = json_string(action, payload_key);
    if ((value == NULL && value_from_payload == NULL) || (value != NULL && value_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of %s or %s", type, value_key, payload_key);
    if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0))
        return validation_error(ctx, json_path, "%s %s must be a non-negative number", type, value_key);
    if (value_from_payload_value != NULL &&
        (!yyjson_is_str(value_from_payload_value) || yyjson_get_str(value_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "%s %s must be a non-empty string", type, payload_key);
    }

    yyjson_val *max = obj_get(action, "max");
    yyjson_val *min = obj_get(action, "min");
    yyjson_val *clamp = obj_get(action, "clamp");
    yyjson_val *allow_partial = obj_get(action, "allow_partial");
    if ((max != NULL && !yyjson_is_num(max)) || (min != NULL && !yyjson_is_num(min)))
        return validation_error(ctx, json_path, "%s min/max must be numbers", type);
    if ((clamp != NULL && !yyjson_is_bool(clamp)) || (allow_partial != NULL && !yyjson_is_bool(allow_partial)))
        return validation_error(ctx, json_path, "%s boolean fields must be booleans", type);
    return validate_optional_signal_field(ctx, action, json_path, names, "on_success") &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_failure");
}

static bool validate_pickup_collect_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                           validation_names *names)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, "pickup.collect", "target", "target_from_payload"))
        return false;
    if (!validate_actor_target_action(ctx, action, json_path, names, "pickup.collect", "pickup", "pickup_from_payload"))
        return false;
    yyjson_val *deactivate = obj_get(action, "deactivate");
    yyjson_val *respawn = obj_get(action, "respawn_seconds");
    if (deactivate != NULL && !yyjson_is_bool(deactivate))
        return validation_error(ctx, json_path, "pickup.collect deactivate must be a boolean");
    if (respawn != NULL && (!yyjson_is_num(respawn) || yyjson_get_num(respawn) < 0.0))
        return validation_error(ctx, json_path, "pickup.collect respawn_seconds must be non-negative");
    if (!validate_non_empty_string_field(ctx, action, json_path, "pickup.collect", "timer_property") ||
        !validate_non_empty_string_field(ctx, action, json_path, "pickup.collect", "available_property"))
    {
        return false;
    }
    return validate_resource_grants(ctx, action, json_path, names, "pickup.collect") &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_collected");
}

static bool validate_resource_station_use_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, "resource.station.use", "target",
                                      "target_from_payload"))
        return false;
    if (!validate_actor_target_action(ctx, action, json_path, names, "resource.station.use", "station",
                                      "station_from_payload"))
        return false;
    yyjson_val *cooldown = obj_get(action, "cooldown");
    yyjson_val *consume_charge = obj_get(action, "consume_charge");
    if (cooldown != NULL && (!yyjson_is_num(cooldown) || yyjson_get_num(cooldown) < 0.0))
        return validation_error(ctx, json_path, "resource.station.use cooldown must be non-negative");
    if (consume_charge != NULL && !yyjson_is_bool(consume_charge))
        return validation_error(ctx, json_path, "resource.station.use consume_charge must be a boolean");
    if (!validate_non_empty_string_field(ctx, action, json_path, "resource.station.use", "cooldown_property") ||
        !validate_non_empty_string_field(ctx, action, json_path, "resource.station.use", "charges_property"))
    {
        return false;
    }
    return validate_resource_grants(ctx, action, json_path, names, "resource.station.use") &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_success") &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_failure");
}

static bool validate_status_effect_apply_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, "status_effect.apply", "target",
                                      "target_from_payload"))
        return false;
    if (!is_non_empty_string(action, "property"))
        return validation_error(ctx, json_path, "status_effect.apply requires a non-empty property");
    if (!validate_non_empty_string_field(ctx, action, json_path, "status_effect.apply", "duration_property") ||
        !validate_non_empty_string_field(ctx, action, json_path, "status_effect.apply", "active_property"))
    {
        return false;
    }
    yyjson_val *value = obj_get(action, "value");
    if (!(yyjson_is_bool(value) || yyjson_is_num(value) || yyjson_is_str(value)))
        return validation_error(ctx, json_path, "status_effect.apply value must be scalar");
    yyjson_val *duration = obj_get(action, "duration");
    yyjson_val *duration_from_payload_value = obj_get(action, "duration_from_payload");
    const char *duration_from_payload = json_string(action, "duration_from_payload");
    if ((duration == NULL && duration_from_payload == NULL) || (duration != NULL && duration_from_payload != NULL))
    {
        return validation_error(ctx, json_path,
                                "status_effect.apply requires exactly one of duration or duration_from_payload");
    }
    if (duration != NULL && (!yyjson_is_num(duration) || yyjson_get_num(duration) < 0.0))
        return validation_error(ctx, json_path, "status_effect.apply duration must be non-negative");
    if (duration_from_payload_value != NULL &&
        (!yyjson_is_str(duration_from_payload_value) || yyjson_get_str(duration_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "status_effect.apply duration_from_payload must be non-empty");
    }
    return validate_optional_signal_field(ctx, action, json_path, names, "on_apply");
}

static bool validate_weapon_common_fire_fields(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    const char *string_fields[] = {"cooldown_property", "reload_timer_property", "clip_property",
                                   "ammo_resource",     "ammo_property",         "direction_from_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, action, json_path, type, string_fields[i]))
            return false;
    }
    yyjson_val *cooldown = obj_get(action, "cooldown");
    yyjson_val *ammo_per_shot = obj_get(action, "ammo_per_shot");
    if ((cooldown != NULL && (!yyjson_is_num(cooldown) || yyjson_get_num(cooldown) < 0.0)) ||
        (ammo_per_shot != NULL && (!yyjson_is_num(ammo_per_shot) || yyjson_get_num(ammo_per_shot) < 0.0)))
    {
        return validation_error(ctx, json_path, "%s cooldown and ammo_per_shot must be non-negative", type);
    }
    const char *signal_keys[] = {"on_fire", "on_empty", "on_cooldown", "on_reloading"};
    for (size_t i = 0; i < SDL_arraysize(signal_keys); ++i)
    {
        if (!validate_optional_signal_field(ctx, action, json_path, names, signal_keys[i]))
            return false;
    }
    return true;
}

static bool validate_weapon_reload_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, "weapon.reload", "target", "target_from_payload"))
        return false;
    const char *string_fields[] = {"clip_property", "clip_size_property", "reserve_property", "reload_timer_property",
                                   "reload_pending_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, action, json_path, "weapon.reload", string_fields[i]))
            return false;
    }
    yyjson_val *clip_size = obj_get(action, "clip_size");
    yyjson_val *reload_seconds = obj_get(action, "reload_seconds");
    if ((clip_size != NULL && (!yyjson_is_num(clip_size) || yyjson_get_num(clip_size) < 0.0)) ||
        (reload_seconds != NULL && (!yyjson_is_num(reload_seconds) || yyjson_get_num(reload_seconds) < 0.0)))
    {
        return validation_error(ctx, json_path, "weapon.reload numeric values must be non-negative");
    }
    yyjson_val *consume_reserve = obj_get(action, "consume_reserve");
    if (consume_reserve != NULL && !yyjson_is_bool(consume_reserve))
        return validation_error(ctx, json_path, "weapon.reload consume_reserve must be a boolean");
    return validate_optional_signal_field(ctx, action, json_path, names, "on_reload") &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_failure");
}

static bool validate_weapon_hitscan_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                           validation_names *names)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, "weapon.hitscan", "target",
                                      "target_from_payload") ||
        !validate_weapon_common_fire_fields(ctx, action, json_path, names, "weapon.hitscan"))
    {
        return false;
    }
    const char *sector_level = json_string(action, "sector_level");
    if (sector_level != NULL && !require_ref(ctx, &names->sector_levels, "sector level", sector_level, json_path))
        return false;
    yyjson_val *trace_brush_worlds = obj_get(action, "trace_brush_worlds");
    if (trace_brush_worlds != NULL && !yyjson_is_bool(trace_brush_worlds))
        return validation_error(ctx, json_path, "weapon.hitscan trace_brush_worlds must be a boolean");
    char contents_path[PATH_BUFFER_SIZE];
    format_path(contents_path, sizeof(contents_path), "%s.brush_contents_mask", json_path);
    if (!validate_brush_string_or_string_array(ctx, obj_get(action, "brush_contents_mask"), contents_path,
                                               "brush content", brush_content_name_valid, false))
    {
        return false;
    }
    if (!validate_non_empty_string_field(ctx, action, json_path, "weapon.hitscan", "target_tag") ||
        !validate_non_empty_string_field(ctx, action, json_path, "weapon.hitscan", "hit_tag"))
    {
        return false;
    }
    yyjson_val *direction = obj_get(action, "direction");
    yyjson_val *offset = obj_get(action, "offset");
    if (direction != NULL && !is_vec_array(direction, 3))
        return validation_error(ctx, json_path, "weapon.hitscan direction must be a vec3");
    if (offset != NULL && !is_vec_array(offset, 3))
        return validation_error(ctx, json_path, "weapon.hitscan offset must be a vec3");
    yyjson_val *range = obj_get(action, "range");
    yyjson_val *hit_radius = obj_get(action, "hit_radius");
    if ((range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) < 0.0)) ||
        (hit_radius != NULL && (!yyjson_is_num(hit_radius) || yyjson_get_num(hit_radius) < 0.0)))
    {
        return validation_error(ctx, json_path, "weapon.hitscan range and hit_radius must be non-negative");
    }
    yyjson_val *exclude_source = obj_get(action, "exclude_source");
    yyjson_val *run_actions_on_miss = obj_get(action, "run_actions_on_miss");
    if ((exclude_source != NULL && !yyjson_is_bool(exclude_source)) ||
        (run_actions_on_miss != NULL && !yyjson_is_bool(run_actions_on_miss)))
    {
        return validation_error(ctx, json_path, "weapon.hitscan boolean fields must be booleans");
    }
    yyjson_val *actions = obj_get(action, "actions");
    yyjson_val *miss_actions = obj_get(action, "miss_actions");
    return validate_target_filter_fields(ctx, action, json_path, "weapon.hitscan") &&
           (actions == NULL || validate_action_array(ctx, actions, json_path, names)) &&
           (miss_actions == NULL || validate_action_array(ctx, miss_actions, json_path, names));
}

static bool validate_interaction_use_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names)
{
    yyjson_val *actor_value = obj_get(action, "actor");
    yyjson_val *actor_from_payload_value = obj_get(action, "actor_from_payload");
    const char *actor = json_string(action, "actor");
    const char *actor_from_payload = json_string(action, "actor_from_payload");
    if ((actor == NULL && actor_from_payload == NULL) || (actor != NULL && actor_from_payload != NULL))
        return validation_error(ctx, json_path, "interaction.use requires exactly one of actor or actor_from_payload");
    if (actor_value != NULL && !yyjson_is_str(actor_value))
        return validation_error(ctx, json_path, "interaction.use actor must be a string");
    if (actor_from_payload_value != NULL && !yyjson_is_str(actor_from_payload_value))
        return validation_error(ctx, json_path, "interaction.use actor_from_payload must be a string");
    if (actor != NULL && !require_actor_ref(ctx, names, actor, json_path))
        return false;
    if (actor_from_payload != NULL && actor_from_payload[0] == '\0')
        return validation_error(ctx, json_path, "interaction.use actor_from_payload must be non-empty");

    yyjson_val *range = obj_get(action, "range");
    yyjson_val *min_dot = obj_get(action, "min_dot");
    if ((range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) < 0.0)) ||
        (min_dot != NULL &&
         (!yyjson_is_num(min_dot) || yyjson_get_num(min_dot) < -1.0 || yyjson_get_num(min_dot) > 1.0)))
    {
        return validation_error(ctx, json_path, "interaction.use range/min_dot are invalid");
    }
    if (!validate_non_empty_string_field(ctx, action, json_path, "interaction.use", "yaw_property") ||
        !validate_non_empty_string_field(ctx, action, json_path, "interaction.use", "target_tag") ||
        !validate_non_empty_string_field(ctx, action, json_path, "interaction.use", "interactable_tag"))
    {
        return false;
    }
    yyjson_val *miss_actions = obj_get(action, "miss_actions");
    return miss_actions == NULL || validate_action_array(ctx, miss_actions, json_path, names);
}

static bool validate_effect_explosion_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                             validation_names *names)
{
    yyjson_val *radius = obj_get(action, "radius");
    if (radius == NULL || !yyjson_is_num(radius) || yyjson_get_num(radius) < 0.0)
        return validation_error(ctx, json_path, "effect.explosion radius must be a non-negative number");

    yyjson_val *inner_radius = obj_get(action, "inner_radius");
    if (inner_radius != NULL && (!yyjson_is_num(inner_radius) || yyjson_get_num(inner_radius) < 0.0))
        return validation_error(ctx, json_path, "effect.explosion inner_radius must be non-negative");
    if (inner_radius != NULL && yyjson_get_num(inner_radius) > yyjson_get_num(radius))
        return validation_error(ctx, json_path, "effect.explosion inner_radius must not exceed radius");

    yyjson_val *damage = obj_get(action, "damage");
    yyjson_val *amount = obj_get(action, "amount");
    if (damage != NULL && amount != NULL)
        return validation_error(ctx, json_path, "effect.explosion requires at most one of damage or amount");
    if ((damage != NULL && (!yyjson_is_num(damage) || yyjson_get_num(damage) < 0.0)) ||
        (amount != NULL && (!yyjson_is_num(amount) || yyjson_get_num(amount) < 0.0)))
    {
        return validation_error(ctx, json_path, "effect.explosion damage must be non-negative");
    }

    yyjson_val *impulse = obj_get(action, "impulse");
    if (impulse != NULL && (!yyjson_is_num(impulse) || yyjson_get_num(impulse) < 0.0))
        return validation_error(ctx, json_path, "effect.explosion impulse must be non-negative");

    yyjson_val *position = obj_get(action, "position");
    yyjson_val *offset = obj_get(action, "offset");
    if (position != NULL && !is_vec_array(position, 3))
        return validation_error(ctx, json_path, "effect.explosion position must be a vec3");
    if (offset != NULL && !is_vec_array(offset, 3))
        return validation_error(ctx, json_path, "effect.explosion offset must be a vec3");

    const char *actor_ref_fields[] = {"source", "from"};
    for (size_t i = 0; i < SDL_arraysize(actor_ref_fields); ++i)
    {
        yyjson_val *value = obj_get(action, actor_ref_fields[i]);
        const char *name = json_string(action, actor_ref_fields[i]);
        if (value != NULL && !yyjson_is_str(value))
            return validation_error(ctx, json_path, "effect.explosion %s must be a string", actor_ref_fields[i]);
        if (name != NULL && !require_actor_ref(ctx, names, name, json_path))
            return false;
    }

    const char *payload_fields[] = {"source_from_payload", "from_payload"};
    for (size_t i = 0; i < SDL_arraysize(payload_fields); ++i)
    {
        yyjson_val *value = obj_get(action, payload_fields[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, json_path, "effect.explosion %s must be a non-empty string",
                                    payload_fields[i]);
    }

    const char *string_fields[] = {"target_tag", "affected_tag", "damage_type", "velocity_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, action, json_path, "effect.explosion", string_fields[i]))
            return false;
    }

    yyjson_val *falloff = obj_get(action, "falloff");
    if (falloff != NULL)
    {
        const char *value = yyjson_is_str(falloff) ? yyjson_get_str(falloff) : NULL;
        if (value == NULL ||
            (SDL_strcmp(value, "linear") != 0 && SDL_strcmp(value, "constant") != 0 && SDL_strcmp(value, "none") != 0))
        {
            return validation_error(ctx, json_path, "effect.explosion falloff must be linear, constant, or none");
        }
    }

    yyjson_val *exclude_source = obj_get(action, "exclude_source");
    if (exclude_source != NULL && !yyjson_is_bool(exclude_source))
        return validation_error(ctx, json_path, "effect.explosion exclude_source must be a boolean");
    yyjson_val *max_targets = obj_get(action, "max_targets");
    if (max_targets != NULL && (!yyjson_is_int(max_targets) || yyjson_get_sint(max_targets) < 0))
        return validation_error(ctx, json_path, "effect.explosion max_targets must be a non-negative integer");

    yyjson_val *actions = obj_get(action, "actions");
    return validate_target_filter_fields(ctx, action, json_path, "effect.explosion") &&
           (actions == NULL || validate_action_array(ctx, actions, json_path, names)) &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_hit");
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
        yyjson_val *target_value = obj_get(action, "target");
        yyjson_val *target_from_payload_value = obj_get(action, "target_from_payload");
        const char *target = json_string(action, "target");
        const char *target_from_payload = json_string(action, "target_from_payload");
        if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
            return validation_error(ctx, json_path, "%s requires exactly one of target or target_from_payload", type);
        if (target_value != NULL && !yyjson_is_str(target_value))
            return validation_error(ctx, json_path, "%s target must be a string", type);
        if (target_from_payload_value != NULL && !yyjson_is_str(target_from_payload_value))
            return validation_error(ctx, json_path, "%s target_from_payload must be a string", type);
        if (target != NULL && !require_ref(ctx, &names->entities, "entity", target, json_path))
            return false;
        if (target_from_payload != NULL && target_from_payload[0] == '\0')
            return validation_error(ctx, json_path, "%s target_from_payload must be non-empty", type);
        if (!is_non_empty_string(action, "key"))
            return validation_error(ctx, json_path, "%s requires a non-empty key", type);
        yyjson_val *value = obj_get(action, "value");
        yyjson_val *value_from_payload_value = obj_get(action, "value_from_payload");
        const char *value_from_payload = json_string(action, "value_from_payload");
        if ((value == NULL && value_from_payload == NULL) || (value != NULL && value_from_payload != NULL))
            return validation_error(ctx, json_path, "%s requires exactly one of value or value_from_payload", type);
        if (value_from_payload_value != NULL && !yyjson_is_str(value_from_payload_value))
            return validation_error(ctx, json_path, "%s value_from_payload must be a string", type);
        if (value_from_payload != NULL && value_from_payload[0] == '\0')
            return validation_error(ctx, json_path, "%s value_from_payload must be non-empty", type);
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
        if (!validate_actor_target_action(ctx, action, json_path, names, "property.animate", "target",
                                          "target_from_payload"))
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
    if (SDL_strcmp(type, "debug.write_actor_properties") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        if (!is_non_empty_string(action, "path"))
            return validation_error(ctx, json_path, "debug.write_actor_properties requires a non-empty path");
        yyjson_val *append = obj_get(action, "append");
        if (append != NULL && !yyjson_is_bool(append))
            return validation_error(ctx, json_path, "debug.write_actor_properties append must be a boolean");
        yyjson_val *properties = obj_get(action, "properties");
        if (!yyjson_is_arr(properties) || yyjson_arr_size(properties) == 0)
            return validation_error(ctx, json_path,
                                    "debug.write_actor_properties requires a non-empty properties array");
        for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
        {
            yyjson_val *property = yyjson_arr_get(properties, i);
            if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
                return validation_error(ctx, json_path,
                                        "debug.write_actor_properties properties must be non-empty strings");
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
        yyjson_val *from_payload = obj_get(action, "from_payload");
        if (from_payload != NULL && (!yyjson_is_str(from_payload) || yyjson_get_str(from_payload)[0] == '\0'))
            return validation_error(ctx, json_path, "actor.spawn from_payload must be a non-empty string");
        yyjson_val *position = obj_get(action, "position");
        if (position != NULL && !is_vec_array(position, 3))
            return validation_error(ctx, json_path, "actor.spawn position must be a vec3");
        yyjson_val *position_from_payload = obj_get(action, "position_from_payload");
        if (position_from_payload != NULL &&
            (!yyjson_is_str(position_from_payload) || yyjson_get_str(position_from_payload)[0] == '\0'))
        {
            return validation_error(ctx, json_path, "actor.spawn position_from_payload must be a non-empty string");
        }
        yyjson_val *position_from_actor_properties = obj_get(action, "position_from_actor_properties");
        if (position_from_actor_properties != NULL)
        {
            if (!yyjson_is_obj(position_from_actor_properties))
                return validation_error(ctx, json_path, "actor.spawn position_from_actor_properties must be an object");
            if (!require_actor_ref(ctx, names, json_string(position_from_actor_properties, "source"), json_path))
                return false;
            const char *x = json_string(position_from_actor_properties, "x");
            const char *y = json_string(position_from_actor_properties, "y");
            const char *z = json_string(position_from_actor_properties, "z");
            if (x == NULL || x[0] == '\0' || y == NULL || y[0] == '\0' || z == NULL || z[0] == '\0')
            {
                return validation_error(ctx, json_path,
                                        "actor.spawn position_from_actor_properties requires non-empty x, y, and z "
                                        "property names");
            }
            yyjson_val *property_offset = obj_get(position_from_actor_properties, "offset");
            if (property_offset != NULL && !is_vec_array(property_offset, 3))
                return validation_error(ctx, json_path,
                                        "actor.spawn position_from_actor_properties offset must be a vec3");
            const char *additive_fields[] = {"x_add", "y_add", "z_add"};
            for (size_t additive_index = 0; additive_index < SDL_arraysize(additive_fields); ++additive_index)
            {
                yyjson_val *additive = obj_get(position_from_actor_properties, additive_fields[additive_index]);
                if (additive == NULL)
                    continue;
                if (!yyjson_is_arr(additive))
                    return validation_error(
                        ctx, json_path, "actor.spawn position_from_actor_properties additive fields must be arrays");
                for (size_t property_index = 0; property_index < yyjson_arr_size(additive); ++property_index)
                {
                    yyjson_val *property = yyjson_arr_get(additive, property_index);
                    if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
                    {
                        return validation_error(ctx, json_path,
                                                "actor.spawn position_from_actor_properties additive fields must "
                                                "contain non-empty strings");
                    }
                }
            }
        }
        yyjson_val *offset = obj_get(action, "offset");
        if (offset != NULL && !is_vec_array(offset, 3))
            return validation_error(ctx, json_path, "actor.spawn offset must be a vec3");
        yyjson_val *directional_offset = obj_get(action, "directional_offset");
        if (directional_offset != NULL)
        {
            if (!yyjson_is_obj(directional_offset))
                return validation_error(ctx, json_path, "actor.spawn directional_offset must be an object");
            yyjson_val *property = obj_get(directional_offset, "property");
            yyjson_val *distance = obj_get(directional_offset, "distance");
            if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
                return validation_error(ctx, json_path,
                                        "actor.spawn directional_offset property must be a non-empty string");
            if (!yyjson_is_num(distance))
                return validation_error(ctx, json_path, "actor.spawn directional_offset distance must be numeric");
        }
        yyjson_val *payload_directional_offset = obj_get(action, "payload_directional_offset");
        if (payload_directional_offset != NULL)
        {
            if (!yyjson_is_obj(payload_directional_offset))
                return validation_error(ctx, json_path, "actor.spawn payload_directional_offset must be an object");
            yyjson_val *property = obj_get(payload_directional_offset, "property");
            yyjson_val *distance = obj_get(payload_directional_offset, "distance");
            if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
                return validation_error(ctx, json_path,
                                        "actor.spawn payload_directional_offset property must be a non-empty string");
            if (!yyjson_is_num(distance))
                return validation_error(ctx, json_path,
                                        "actor.spawn payload_directional_offset distance must be numeric");
        }
        yyjson_val *velocity_from_payload = obj_get(action, "velocity_from_payload");
        if (velocity_from_payload != NULL &&
            (!yyjson_is_str(velocity_from_payload) || yyjson_get_str(velocity_from_payload)[0] == '\0'))
        {
            return validation_error(ctx, json_path, "actor.spawn velocity_from_payload must be a non-empty string");
        }
        yyjson_val *velocity_property = obj_get(action, "velocity_property");
        if (velocity_property != NULL &&
            (!yyjson_is_str(velocity_property) || yyjson_get_str(velocity_property)[0] == '\0'))
            return validation_error(ctx, json_path, "actor.spawn velocity_property must be a non-empty string");
        yyjson_val *speed = obj_get(action, "speed");
        if (speed != NULL && (!yyjson_is_num(speed) || yyjson_get_num(speed) < 0.0))
            return validation_error(ctx, json_path, "actor.spawn speed must be non-negative");
        yyjson_val *properties = obj_get(action, "properties");
        if (properties != NULL && !yyjson_is_obj(properties))
            return validation_error(ctx, json_path, "actor.spawn properties must be an object");
        yyjson_val *properties_from_actor = obj_get(action, "properties_from_actor");
        if (properties_from_actor != NULL)
        {
            if (!yyjson_is_obj(properties_from_actor))
                return validation_error(ctx, json_path, "actor.spawn properties_from_actor must be an object");
            if (!require_actor_ref(ctx, names, json_string(properties_from_actor, "source"), json_path))
                return false;
            yyjson_val *keys = obj_get(properties_from_actor, "keys");
            if (!yyjson_is_arr(keys) || yyjson_arr_size(keys) == 0)
                return validation_error(ctx, json_path,
                                        "actor.spawn properties_from_actor keys must be a non-empty array");
            for (size_t i = 0; i < yyjson_arr_size(keys); ++i)
            {
                yyjson_val *key = yyjson_arr_get(keys, i);
                if (!yyjson_is_str(key) || yyjson_get_str(key)[0] == '\0')
                    return validation_error(ctx, json_path,
                                            "actor.spawn properties_from_actor keys must be non-empty strings");
            }
        }
        return true;
    }
    if (SDL_strcmp(type, "actor.despawn") == 0)
    {
        yyjson_val *target_value = obj_get(action, "target");
        yyjson_val *target_from_payload_value = obj_get(action, "target_from_payload");
        const char *target = json_string(action, "target");
        const char *target_from_payload = json_string(action, "target_from_payload");
        if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
            return validation_error(ctx, json_path,
                                    "actor.despawn requires exactly one of target or target_from_payload");
        if (target_value != NULL && !yyjson_is_str(target_value))
            return validation_error(ctx, json_path, "actor.despawn target must be a string");
        if (target_from_payload_value != NULL && !yyjson_is_str(target_from_payload_value))
            return validation_error(ctx, json_path, "actor.despawn target_from_payload must be a string");
        if (target != NULL && target[0] == '\0')
            return validation_error(ctx, json_path, "actor.despawn target must be non-empty");
        if (target_from_payload != NULL && target_from_payload[0] == '\0')
            return validation_error(ctx, json_path, "actor.despawn target_from_payload must be non-empty");
        if (target != NULL && !name_table_contains(&names->entities, target) &&
            !name_table_contains(&names->actor_pool_actors, target))
            return validation_error(ctx, json_path, "unknown actor.despawn target '%s'", target);
        yyjson_val *reason = obj_get(action, "reason");
        if (reason != NULL && !yyjson_is_str(reason))
            return validation_error(ctx, json_path, "actor.despawn reason must be a string");
        return true;
    }
    if (SDL_strcmp(type, "actor.despawn_by_tag") == 0)
    {
        if (!is_non_empty_string(action, "tag"))
            return validation_error(ctx, json_path, "actor.despawn_by_tag requires a non-empty tag");
        yyjson_val *reason = obj_get(action, "reason");
        if (reason != NULL && !yyjson_is_str(reason))
            return validation_error(ctx, json_path, "actor.despawn_by_tag reason must be a string");
        return true;
    }
    if (SDL_strcmp(type, "combat.damage") == 0)
        return validate_combat_amount_action(ctx, action, json_path, names, type, "amount", "amount_from_payload");
    if (SDL_strcmp(type, "combat.heal") == 0)
        return validate_combat_amount_action(ctx, action, json_path, names, type, "amount", "amount_from_payload");
    if (SDL_strcmp(type, "combat.kill") == 0)
        return validate_combat_target_action(ctx, action, json_path, names, type);
    if (SDL_strcmp(type, "combat.revive") == 0)
    {
        if (!validate_combat_target_action(ctx, action, json_path, names, type))
            return false;
        yyjson_val *health = obj_get(action, "health");
        yyjson_val *health_from_payload_value = obj_get(action, "health_from_payload");
        const char *health_from_payload = json_string(action, "health_from_payload");
        if (health != NULL && health_from_payload != NULL)
            return validation_error(ctx, json_path,
                                    "combat.revive requires at most one of health or health_from_payload");
        if (health != NULL && (!yyjson_is_num(health) || yyjson_get_num(health) < 0.0))
            return validation_error(ctx, json_path, "combat.revive health must be a non-negative number");
        if (health_from_payload_value != NULL &&
            (!yyjson_is_str(health_from_payload_value) || yyjson_get_str(health_from_payload_value)[0] == '\0'))
        {
            return validation_error(ctx, json_path, "combat.revive health_from_payload must be a non-empty string");
        }
        return true;
    }
    if (SDL_strcmp(type, "resource.add") == 0)
        return validate_resource_action(ctx, action, json_path, names, type, "amount", "amount_from_payload");
    if (SDL_strcmp(type, "resource.consume") == 0)
        return validate_resource_action(ctx, action, json_path, names, type, "amount", "amount_from_payload");
    if (SDL_strcmp(type, "resource.set") == 0)
        return validate_resource_action(ctx, action, json_path, names, type, "value", "value_from_payload");
    if (SDL_strcmp(type, "pickup.collect") == 0)
        return validate_pickup_collect_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "resource.station.use") == 0)
        return validate_resource_station_use_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "status_effect.apply") == 0)
        return validate_status_effect_apply_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "weapon.reload") == 0)
        return validate_weapon_reload_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "weapon.hitscan") == 0)
        return validate_weapon_hitscan_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "interaction.use") == 0)
        return validate_interaction_use_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "effect.explosion") == 0)
        return validate_effect_explosion_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "noise.emit") == 0)
    {
        const char *source = json_string(action, "source");
        const char *actor = json_string(action, "actor");
        const char *target = json_string(action, "target");
        if (source != NULL && !require_actor_ref(ctx, names, source, json_path))
            return false;
        if (actor != NULL && !require_actor_ref(ctx, names, actor, json_path))
            return false;
        if (target != NULL && !require_actor_ref(ctx, names, target, json_path))
            return false;

        const char *payload_fields[] = {"source_from_payload", "actor_from_payload", "target_from_payload",
                                        "from_payload"};
        for (size_t i = 0; i < SDL_arraysize(payload_fields); ++i)
        {
            yyjson_val *value = obj_get(action, payload_fields[i]);
            if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
                return validation_error(ctx, json_path, "noise.emit %s must be a non-empty string", payload_fields[i]);
        }

        yyjson_val *from = obj_get(action, "from");
        if (from != NULL && (!yyjson_is_str(from) || yyjson_get_str(from)[0] == '\0'))
            return validation_error(ctx, json_path, "noise.emit from must be a non-empty actor reference");
        const char *from_actor = json_string(action, "from");
        if (from_actor != NULL && !require_actor_ref(ctx, names, from_actor, json_path))
            return false;
        yyjson_val *position = obj_get(action, "position");
        yyjson_val *offset = obj_get(action, "offset");
        if (position != NULL && !is_vec_array(position, 3))
            return validation_error(ctx, json_path, "noise.emit position must be a vec3");
        if (offset != NULL && !is_vec_array(offset, 3))
            return validation_error(ctx, json_path, "noise.emit offset must be a vec3");
        yyjson_val *radius = obj_get(action, "radius");
        yyjson_val *range = obj_get(action, "range");
        yyjson_val *loudness = obj_get(action, "loudness");
        yyjson_val *duration = obj_get(action, "duration");
        yyjson_val *duration_seconds = obj_get(action, "duration_seconds");
        if (radius != NULL && (!yyjson_is_num(radius) || yyjson_get_num(radius) <= 0.0))
            return validation_error(ctx, json_path, "noise.emit radius must be positive");
        if (range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) <= 0.0))
            return validation_error(ctx, json_path, "noise.emit range must be positive");
        if (loudness != NULL && (!yyjson_is_num(loudness) || yyjson_get_num(loudness) < 0.0))
            return validation_error(ctx, json_path, "noise.emit loudness must be non-negative");
        if (duration != NULL && (!yyjson_is_num(duration) || yyjson_get_num(duration) <= 0.0))
            return validation_error(ctx, json_path, "noise.emit duration must be positive");
        if (duration_seconds != NULL && (!yyjson_is_num(duration_seconds) || yyjson_get_num(duration_seconds) <= 0.0))
            return validation_error(ctx, json_path, "noise.emit duration_seconds must be positive");
        return true;
    }
    if (SDL_strcmp(type, "sector_door.open") == 0 || SDL_strcmp(type, "sector_door.close") == 0 ||
        SDL_strcmp(type, "sector_door.toggle") == 0)
    {
        const char *target = json_string(action, "target");
        const char *target_from_payload = json_string(action, "target_from_payload");
        if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
        {
            return validation_error(ctx, json_path,
                                    "sector door action requires exactly one of target or target_from_payload");
        }
        if (target != NULL && !require_ref(ctx, &names->sector_doors, "sector door", target, json_path))
            return false;
        if (target_from_payload != NULL && target_from_payload[0] == '\0')
            return validation_error(ctx, json_path, "sector door target_from_payload must be non-empty");
        yyjson_val *stay = obj_get(action, "stay_open_seconds");
        yyjson_val *auto_close = obj_get(action, "auto_close_seconds");
        if ((stay != NULL && (!yyjson_is_num(stay) || yyjson_get_num(stay) < 0.0)) ||
            (auto_close != NULL && (!yyjson_is_num(auto_close) || yyjson_get_num(auto_close) < 0.0)))
        {
            return validation_error(ctx, json_path, "sector door auto-close timing must be non-negative");
        }
        return true;
    }
    if (SDL_strcmp(type, "sector_door.interact") == 0)
    {
        if (!require_actor_ref(ctx, names, json_string(action, "actor"), json_path))
            return false;
        yyjson_val *range = obj_get(action, "range");
        yyjson_val *min_dot = obj_get(action, "min_dot");
        if ((range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) < 0.0)) ||
            (min_dot != NULL &&
             (!yyjson_is_num(min_dot) || yyjson_get_num(min_dot) < -1.0 || yyjson_get_num(min_dot) > 1.0)))
        {
            return validation_error(ctx, json_path, "sector door interaction range/min_dot are invalid");
        }
        yyjson_val *yaw_property = obj_get(action, "yaw_property");
        if (yaw_property != NULL && !is_non_empty_string(action, "yaw_property"))
            return validation_error(ctx, json_path, "sector door yaw_property must be non-empty");
        yyjson_val *actions = obj_get(action, "actions");
        const char *signal = json_string(action, "signal");
        if ((actions == NULL && signal == NULL) || (actions != NULL && signal != NULL))
            return validation_error(ctx, json_path, "sector_door.interact requires exactly one of actions or signal");
        if (actions != NULL)
            return validate_action_array(ctx, actions, json_path, names);
        return require_ref(ctx, &names->signals, "signal", signal, json_path);
    }
    if (SDL_strcmp(type, "sector_lighting.set") == 0)
    {
        const char *sector_level = json_string(action, "sector_level");
        const char *sector = json_string(action, "sector");
        yyjson_val *sector_index = obj_get(action, "sector_index");
        if (!require_ref(ctx, &names->sector_levels, "sector level", sector_level, json_path))
            return false;
        if ((sector == NULL && sector_index == NULL) || (sector != NULL && sector_index != NULL))
            return validation_error(ctx, json_path,
                                    "sector_lighting.set requires exactly one of sector or sector_index");
        if (sector != NULL && sector[0] == '\0')
            return validation_error(ctx, json_path, "sector_lighting.set sector must be non-empty");
        if (sector_index != NULL && (!yyjson_is_int(sector_index) || yyjson_get_int(sector_index) < 0))
            return validation_error(ctx, json_path, "sector_lighting.set sector_index must be non-negative");
        yyjson_val *level = obj_get(action, "level");
        if (level != NULL && (!yyjson_is_num(level) || yyjson_get_num(level) < 0.0 || yyjson_get_num(level) > 255.0))
            return validation_error(ctx, json_path, "sector_lighting.set level must be in [0, 255]");
        yyjson_val *color = obj_get(action, "color");
        if (color != NULL && (!is_exact_vec3_or_vec4_array(color) || !numeric_array_values_in_range(color, 0.0, 1.0)))
        {
            return validation_error(ctx, json_path,
                                    "sector_lighting.set color must be a vec3 or vec4 with values in [0, 1]");
        }
        return true;
    }
    if (SDL_strcmp(type, "projectile.fire") == 0)
    {
        return validate_projectile_fire_shape(ctx, action, json_path, names, true);
    }
    if (SDL_strcmp(type, "controller.fps.launch") == 0 || SDL_strcmp(type, "controller.fps.teleport") == 0 ||
        SDL_strcmp(type, "controller.fps.push") == 0 || SDL_strcmp(type, "controller.fps_sector.launch") == 0 ||
        SDL_strcmp(type, "controller.fps_sector.teleport") == 0)
    {
        yyjson_val *target_value = obj_get(action, "target");
        yyjson_val *target_from_payload_value = obj_get(action, "target_from_payload");
        const char *target = json_string(action, "target");
        const char *target_from_payload = json_string(action, "target_from_payload");
        if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
            return validation_error(ctx, json_path, "%s requires exactly one of target or target_from_payload", type);
        if (target_value != NULL && !yyjson_is_str(target_value))
            return validation_error(ctx, json_path, "%s target must be a string", type);
        if (target != NULL && !require_ref(ctx, &names->entities, "entity", target, json_path))
            return false;
        if (target_from_payload_value != NULL &&
            (!yyjson_is_str(target_from_payload_value) || yyjson_get_str(target_from_payload_value)[0] == '\0'))
            return validation_error(ctx, json_path, "%s target_from_payload must be a non-empty string", type);
        if (SDL_strcmp(type, "controller.fps.launch") == 0 || SDL_strcmp(type, "controller.fps_sector.launch") == 0)
        {
            yyjson_val *vertical_velocity = obj_get(action, "vertical_velocity");
            if (!yyjson_is_num(vertical_velocity) || yyjson_get_num(vertical_velocity) <= 0.0)
                return validation_error(ctx, json_path, "%s requires positive vertical_velocity", type);
            return true;
        }
        if (SDL_strcmp(type, "controller.fps.push") == 0)
        {
            if (!is_vec_array(obj_get(action, "velocity"), 3))
                return validation_error(ctx, json_path, "%s requires a vec3 velocity", type);
            yyjson_val *scale_by_dt = obj_get(action, "scale_by_dt");
            if (scale_by_dt != NULL && !yyjson_is_bool(scale_by_dt))
                return validation_error(ctx, json_path, "%s scale_by_dt must be a boolean", type);
            return true;
        }

        if (!is_vec_array(obj_get(action, "position"), 3))
            return validation_error(ctx, json_path, "%s requires a vec3 position", type);
        yyjson_val *yaw = obj_get(action, "yaw");
        yyjson_val *pitch = obj_get(action, "pitch");
        if ((yaw != NULL && !yyjson_is_num(yaw)) || (pitch != NULL && !yyjson_is_num(pitch)))
            return validation_error(ctx, json_path, "%s yaw and pitch must be numeric", type);
        return true;
    }
    if (SDL_strcmp(type, "grid.spawn_from_glyphs") == 0 || SDL_strcmp(type, "grid.spawn_runs_from_glyphs") == 0)
    {
        if (!require_ref(ctx, &names->grid_maps, "grid map", json_string(action, "map"), json_path))
            return false;
        yyjson_val *spawns = obj_get(action, "spawns");
        if (!yyjson_is_arr(spawns) || yyjson_arr_size(spawns) <= 0)
            return validation_error(ctx, json_path, "%s requires a non-empty spawns array", type);
        yyjson_val *properties = obj_get(action, "properties");
        if (properties != NULL && !yyjson_is_obj(properties))
            return validation_error(ctx, json_path, "%s properties must be an object", type);
        yyjson_val *z = obj_get(action, "z");
        if (z != NULL && !yyjson_is_num(z))
            return validation_error(ctx, json_path, "%s z must be numeric", type);
        yyjson_val *depth = obj_get(action, "depth");
        if (depth != NULL && !yyjson_is_num(depth))
            return validation_error(ctx, json_path, "%s depth must be numeric", type);
        yyjson_val *inset = obj_get(action, "inset");
        if (inset != NULL && !yyjson_is_num(inset))
            return validation_error(ctx, json_path, "%s inset must be numeric", type);
        yyjson_val *size = obj_get(action, "size");
        if (size != NULL && !is_vec_array(size, 3))
            return validation_error(ctx, json_path, "%s size must be a vec3", type);
        const char *axis = json_string(action, "axis");
        if (axis != NULL && SDL_strcmp(axis, "x") != 0 && SDL_strcmp(axis, "horizontal") != 0 &&
            SDL_strcmp(axis, "row") != 0 && SDL_strcmp(axis, "y") != 0 && SDL_strcmp(axis, "vertical") != 0 &&
            SDL_strcmp(axis, "column") != 0)
        {
            return validation_error(ctx, json_path, "%s axis must be x, y, horizontal, vertical, row, or column", type);
        }
        yyjson_val *output_count_key = obj_get(action, "output_count_key");
        if (output_count_key != NULL && !is_non_empty_string(action, "output_count_key"))
            return validation_error(ctx, json_path, "%s output_count_key must be non-empty", type);
        for (size_t i = 0; i < yyjson_arr_size(spawns); ++i)
        {
            char spawn_path[PATH_BUFFER_SIZE];
            format_path(spawn_path, sizeof(spawn_path), "%s.spawns[%zu]", json_path, i);
            yyjson_val *spawn = yyjson_arr_get(spawns, i);
            if (!yyjson_is_obj(spawn))
                return validation_error(ctx, spawn_path, "grid spawn entries must be objects");
            if (!is_single_byte_string(obj_get(spawn, "glyph")))
                return validation_error(ctx, spawn_path, "grid spawn glyph must be a single-byte string");
            if (!require_ref(ctx, &names->actor_pools, "actor pool", json_string(spawn, "pool"), spawn_path))
                return false;
            yyjson_val *spawn_properties = obj_get(spawn, "properties");
            if (spawn_properties != NULL && !yyjson_is_obj(spawn_properties))
                return validation_error(ctx, spawn_path, "grid spawn properties must be an object");
            yyjson_val *spawn_z = obj_get(spawn, "z");
            if (spawn_z != NULL && !yyjson_is_num(spawn_z))
                return validation_error(ctx, spawn_path, "grid spawn z must be numeric");
            yyjson_val *spawn_depth = obj_get(spawn, "depth");
            if (spawn_depth != NULL && !yyjson_is_num(spawn_depth))
                return validation_error(ctx, spawn_path, "grid spawn depth must be numeric");
            yyjson_val *spawn_inset = obj_get(spawn, "inset");
            if (spawn_inset != NULL && !yyjson_is_num(spawn_inset))
                return validation_error(ctx, spawn_path, "grid spawn inset must be numeric");
            yyjson_val *spawn_size = obj_get(spawn, "size");
            if (spawn_size != NULL && !is_vec_array(spawn_size, 3))
                return validation_error(ctx, spawn_path, "grid spawn size must be a vec3");
            const char *spawn_axis = json_string(spawn, "axis");
            if (spawn_axis != NULL && SDL_strcmp(spawn_axis, "x") != 0 && SDL_strcmp(spawn_axis, "horizontal") != 0 &&
                SDL_strcmp(spawn_axis, "row") != 0 && SDL_strcmp(spawn_axis, "y") != 0 &&
                SDL_strcmp(spawn_axis, "vertical") != 0 && SDL_strcmp(spawn_axis, "column") != 0)
            {
                return validation_error(ctx, spawn_path,
                                        "grid spawn axis must be x, y, horizontal, vertical, row, or column");
            }
        }
        return true;
    }
    if (SDL_strcmp(type, "grid.pickup_layer.reset") == 0)
    {
        if (!require_ref(ctx, &names->grid_pickup_layers, "grid pickup layer", json_string(action, "layer"), json_path))
            return false;
        yyjson_val *output_count_key = obj_get(action, "output_count_key");
        if (output_count_key != NULL && !is_non_empty_string(action, "output_count_key"))
            return validation_error(ctx, json_path, "grid.pickup_layer.reset output_count_key must be non-empty");
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
        if (value == NULL ||
            !(yyjson_is_bool(value) || yyjson_is_num(value) || yyjson_is_str(value) || is_exact_vec_array(value, 3)))
        {
            return validation_error(ctx, json_path, "scene_state.set requires a scalar or vec3 value");
        }
        return true;
    }
    if (SDL_strcmp(type, "scene_state.toggle") == 0)
    {
        if (!is_non_empty_string(action, "key"))
            return validation_error(ctx, json_path, "scene_state.toggle requires a non-empty key");
        yyjson_val *default_value = obj_get(action, "default");
        if (default_value != NULL && !yyjson_is_bool(default_value))
            return validation_error(ctx, json_path, "scene_state.toggle default must be a boolean");
        return true;
    }
    if (SDL_strcmp(type, "scene_state.cycle") == 0)
    {
        if (!is_non_empty_string(action, "key"))
            return validation_error(ctx, json_path, "scene_state.cycle requires a non-empty key");
        yyjson_val *values = obj_get(action, "values");
        if (!yyjson_is_arr(values) || yyjson_arr_size(values) == 0)
            return validation_error(ctx, json_path, "scene_state.cycle requires a non-empty values array");
        for (size_t i = 0; i < yyjson_arr_size(values); ++i)
        {
            yyjson_val *value = yyjson_arr_get(values, i);
            if (!(yyjson_is_bool(value) || yyjson_is_num(value) || yyjson_is_str(value)))
                return validation_error(ctx, json_path, "scene_state.cycle values must be scalar");
        }
        yyjson_val *default_value = obj_get(action, "default");
        if (default_value != NULL &&
            !(yyjson_is_bool(default_value) || yyjson_is_num(default_value) || yyjson_is_str(default_value)))
            return validation_error(ctx, json_path, "scene_state.cycle default must be scalar");
        yyjson_val *direction = obj_get(action, "direction");
        if (direction != NULL && (!yyjson_is_int(direction) || yyjson_get_int(direction) == 0))
            return validation_error(ctx, json_path, "scene_state.cycle direction must be a non-zero integer");
        return true;
    }
    if (SDL_strcmp(type, "editor.selection.clear") == 0)
        return true;
    if (SDL_strcmp(type, "editor.selection.run") == 0)
    {
        char actions_path[PATH_BUFFER_SIZE];
        char else_path[PATH_BUFFER_SIZE];
        format_path(actions_path, sizeof(actions_path), "%s.actions", json_path);
        format_path(else_path, sizeof(else_path), "%s.else", json_path);
        if (!validate_action_array(ctx, obj_get(action, "actions"), actions_path, names))
            return false;
        yyjson_val *else_actions = obj_get(action, "else");
        return else_actions == NULL || validate_action_array(ctx, else_actions, else_path, names);
    }
    if (SDL_strcmp(type, "editor.command.preview") == 0)
    {
        const char *command = json_string(action, "command");
        const char *target = json_string(action, "target");
        if (target == NULL)
            target = "selection";
        if (!editor_command_name_valid(command))
            return validation_error(
                ctx, json_path, "editor.command.preview command must be translate, paint, resize, extrude, or delete");
        if (!editor_command_target_name_valid(target))
            return validation_error(
                ctx, json_path, "editor.command.preview target must be selection, world, element, face, or material");
        if ((SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0) &&
            SDL_strcmp(target, "face") != 0)
        {
            return validation_error(ctx, json_path, "editor.command.preview resize/extrude target must be face");
        }
        yyjson_val *material = obj_get(action, "material");
        if (SDL_strcmp(command, "paint") == 0 && (!yyjson_is_str(material) || yyjson_get_str(material)[0] == '\0'))
        {
            return validation_error(ctx, json_path, "editor.command.preview paint requires a non-empty material");
        }
        if (material != NULL && !yyjson_is_str(material))
            return validation_error(ctx, json_path, "editor.command.preview material must be a string");
        yyjson_val *offset = obj_get(action, "offset");
        if (offset != NULL && !is_vec_array(offset, 3))
            return validation_error(ctx, json_path, "editor.command.preview offset must be a vec3");
        yyjson_val *distance = obj_get(action, "distance");
        if (distance != NULL && !yyjson_is_num(distance))
            return validation_error(ctx, json_path, "editor.command.preview distance must be numeric");
        yyjson_val *message = obj_get(action, "message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "editor.command.preview message must be a string");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.command.preview outputs must be an object");
        static const char *const output_keys[] = {"active_key",     "valid_key",     "command_key", "target_key",
                                                  "message_key",    "world_key",     "element_key", "face_index_key",
                                                  "bounds_min_key", "bounds_max_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path, "editor.command.preview output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.command.clear_preview") == 0)
    {
        yyjson_val *message = obj_get(action, "message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "editor.command.clear_preview message must be a string");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.command.clear_preview outputs must be an object");
        static const char *const output_keys[] = {"active_key",     "valid_key",     "command_key", "target_key",
                                                  "message_key",    "world_key",     "element_key", "face_index_key",
                                                  "bounds_min_key", "bounds_max_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.command.clear_preview output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.command.commit") == 0 || SDL_strcmp(type, "editor.command.undo") == 0 ||
        SDL_strcmp(type, "editor.command.redo") == 0)
    {
        yyjson_val *message = obj_get(action, "message");
        yyjson_val *invalid_message = obj_get(action, "invalid_message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "%s message must be a string", type);
        if (invalid_message != NULL && !yyjson_is_str(invalid_message))
            return validation_error(ctx, json_path, "%s invalid_message must be a string", type);
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "%s outputs must be an object", type);
        static const char *const output_keys[] = {
            "valid_key",      "event_key",         "message_key",    "transaction_id_key", "undo_count_key",
            "redo_count_key", "command_key",       "target_key",     "world_key",          "element_key",
            "face_index_key", "bounds_min_key",    "bounds_max_key", "source_path_key",    "dirty_key",
            "revision_key",   "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path, "%s output keys must be non-empty strings", type);
        }
        char actions_path[PATH_BUFFER_SIZE];
        char else_path[PATH_BUFFER_SIZE];
        format_path(actions_path, sizeof(actions_path), "%s.actions", json_path);
        format_path(else_path, sizeof(else_path), "%s.else", json_path);
        yyjson_val *actions = obj_get(action, "actions");
        yyjson_val *else_actions = obj_get(action, "else");
        if (actions != NULL && !validate_action_array(ctx, actions, actions_path, names))
            return false;
        return else_actions == NULL || validate_action_array(ctx, else_actions, else_path, names);
    }
    if (SDL_strcmp(type, "editor.brush_world.export") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.brush_world.export outputs must be an object");
        static const char *const output_keys[] = {"valid_key", "message_key",  "json_key",
                                                  "size_key",  "world_key",    "source_path_key",
                                                  "dirty_key", "revision_key", "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.brush_world.export output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.level.export") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.level.export outputs must be an object");
        static const char *const output_keys[] = {
            "valid_key",
            "message_key",
            "json_key",
            "size_key",
            "brush_world_key",
            "brush_source_path_key",
            "brush_dirty_key",
            "brush_revision_key",
            "brush_saved_revision_key",
            "player_start_source_path_key",
            "player_start_count_key",
            "player_start_dirty_key",
            "player_start_revision_key",
            "player_start_saved_revision_key",
        };
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path, "editor.level.export output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.level.save") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *path = obj_get(action, "path");
        yyjson_val *path_from_state = obj_get(action, "path_from_state");
        if ((path == NULL && path_from_state == NULL) || (path != NULL && path_from_state != NULL))
            return validation_error(ctx, json_path,
                                    "editor.level.save requires exactly one of path or path_from_state");
        if (path != NULL && (!yyjson_is_str(path) || yyjson_get_str(path)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.level.save path must be a non-empty string");
        if (path_from_state != NULL && (!yyjson_is_str(path_from_state) || yyjson_get_str(path_from_state)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.level.save path_from_state must be a non-empty string");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.level.save outputs must be an object");
        static const char *const output_keys[] = {
            "valid_key",
            "message_key",
            "path_key",
            "size_key",
            "brush_world_key",
            "brush_source_path_key",
            "brush_dirty_key",
            "brush_revision_key",
            "brush_saved_revision_key",
            "player_start_source_path_key",
            "player_start_count_key",
            "player_start_dirty_key",
            "player_start_revision_key",
            "player_start_saved_revision_key",
        };
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path, "editor.level.save output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.level.load") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *path = obj_get(action, "path");
        yyjson_val *path_from_state = obj_get(action, "path_from_state");
        if ((path == NULL && path_from_state == NULL) || (path != NULL && path_from_state != NULL))
            return validation_error(ctx, json_path,
                                    "editor.level.load requires exactly one of path or path_from_state");
        if (path != NULL && (!yyjson_is_str(path) || yyjson_get_str(path)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.level.load path must be a non-empty string");
        if (path_from_state != NULL && (!yyjson_is_str(path_from_state) || yyjson_get_str(path_from_state)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.level.load path_from_state must be a non-empty string");
        yyjson_val *optional = obj_get(action, "optional");
        if (optional != NULL && !yyjson_is_bool(optional))
            return validation_error(ctx, json_path, "editor.level.load optional must be a boolean");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.level.load outputs must be an object");
        static const char *const output_keys[] = {
            "valid_key",
            "message_key",
            "path_key",
            "brush_world_key",
            "brush_source_path_key",
            "brush_dirty_key",
            "brush_revision_key",
            "brush_saved_revision_key",
            "player_start_source_path_key",
            "player_start_count_key",
            "player_start_dirty_key",
            "player_start_revision_key",
            "player_start_saved_revision_key",
        };
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path, "editor.level.load output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.test_run.prepare") == 0)
    {
        if (!is_non_empty_string(action, "data_asset"))
            return validation_error(ctx, json_path, "editor.test_run.prepare requires a non-empty data_asset");
        static const char *const string_fields[] = {"runner", "root", "pack", "media"};
        for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
        {
            yyjson_val *field = obj_get(action, string_fields[i]);
            if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.test_run.prepare command fields must be non-empty strings");
        }
        yyjson_val *embedded = obj_get(action, "embedded");
        if (embedded != NULL && !yyjson_is_bool(embedded))
            return validation_error(ctx, json_path, "editor.test_run.prepare embedded must be a boolean");
        const int mount_count = (obj_get(action, "root") != NULL ? 1 : 0) + (obj_get(action, "pack") != NULL ? 1 : 0) +
                                (embedded != NULL && yyjson_get_bool(embedded) ? 1 : 0);
        if (mount_count > 1)
            return validation_error(ctx, json_path,
                                    "editor.test_run.prepare accepts at most one of root, pack, or embedded");
        const char *scene = json_string(action, "scene");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
            return false;
        yyjson_val *player_start = obj_get(action, "player_start");
        if (player_start != NULL && (!yyjson_is_str(player_start) || yyjson_get_str(player_start)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.test_run.prepare player_start must be a non-empty string");
        if (player_start != NULL && !require_ref(ctx, &names->editor_player_starts, "editor player start",
                                                 yyjson_get_str(player_start), json_path))
        {
            return false;
        }
        if (scene == NULL && player_start == NULL)
            return validation_error(ctx, json_path, "editor.test_run.prepare requires scene or player_start");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.test_run.prepare outputs must be an object");
        static const char *const output_keys[] = {"valid_key",        "message_key",    "manifest_json_key",
                                                  "size_key",         "data_asset_key", "scene_key",
                                                  "player_start_key", "target_key",     "command_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.test_run.prepare output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.test_run.save_manifest") == 0)
    {
        if (!is_non_empty_string(action, "data_asset"))
            return validation_error(ctx, json_path, "editor.test_run.save_manifest requires a non-empty data_asset");
        static const char *const string_fields[] = {"runner", "root", "pack", "media"};
        for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
        {
            yyjson_val *field = obj_get(action, string_fields[i]);
            if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.test_run.save_manifest command fields must be non-empty strings");
        }
        yyjson_val *embedded = obj_get(action, "embedded");
        if (embedded != NULL && !yyjson_is_bool(embedded))
            return validation_error(ctx, json_path, "editor.test_run.save_manifest embedded must be a boolean");
        const int mount_count = (obj_get(action, "root") != NULL ? 1 : 0) + (obj_get(action, "pack") != NULL ? 1 : 0) +
                                (embedded != NULL && yyjson_get_bool(embedded) ? 1 : 0);
        if (mount_count > 1)
            return validation_error(ctx, json_path,
                                    "editor.test_run.save_manifest accepts at most one of root, pack, or embedded");
        yyjson_val *path = obj_get(action, "path");
        yyjson_val *path_from_state = obj_get(action, "path_from_state");
        if ((path == NULL && path_from_state == NULL) || (path != NULL && path_from_state != NULL))
            return validation_error(ctx, json_path,
                                    "editor.test_run.save_manifest requires exactly one of path or path_from_state");
        if (path != NULL && (!yyjson_is_str(path) || yyjson_get_str(path)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.test_run.save_manifest path must be a non-empty string");
        if (path_from_state != NULL && (!yyjson_is_str(path_from_state) || yyjson_get_str(path_from_state)[0] == '\0'))
            return validation_error(ctx, json_path,
                                    "editor.test_run.save_manifest path_from_state must be a non-empty string");
        const char *scene = json_string(action, "scene");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
            return false;
        yyjson_val *player_start = obj_get(action, "player_start");
        if (player_start != NULL && (!yyjson_is_str(player_start) || yyjson_get_str(player_start)[0] == '\0'))
            return validation_error(ctx, json_path,
                                    "editor.test_run.save_manifest player_start must be a non-empty string");
        if (player_start != NULL && !require_ref(ctx, &names->editor_player_starts, "editor player start",
                                                 yyjson_get_str(player_start), json_path))
        {
            return false;
        }
        if (scene == NULL && player_start == NULL)
            return validation_error(ctx, json_path, "editor.test_run.save_manifest requires scene or player_start");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.test_run.save_manifest outputs must be an object");
        static const char *const output_keys[] = {"valid_key",  "message_key",    "path_key",  "manifest_json_key",
                                                  "size_key",   "data_asset_key", "scene_key", "player_start_key",
                                                  "target_key", "command_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.test_run.save_manifest output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.brush_world.status") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *message = obj_get(action, "message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "editor.brush_world.status message must be a string");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.brush_world.status outputs must be an object");
        static const char *const output_keys[] = {"valid_key", "message_key",  "world_key",         "source_path_key",
                                                  "dirty_key", "revision_key", "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.brush_world.status output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.brush_world.create_box") == 0)
    {
        const char *position_from = json_string(action, "position_from");
        const bool from_preview = position_from != NULL && SDL_strcmp(position_from, "placement_preview") == 0;
        if (!from_preview &&
            !require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        if (!from_preview && !is_non_empty_string(action, "material"))
            return validation_error(ctx, json_path, "editor.brush_world.create_box requires a non-empty material");
        yyjson_val *name = obj_get(action, "name");
        if (name != NULL && (!yyjson_is_str(name) || yyjson_get_str(name)[0] == '\0'))
            return validation_error(ctx, json_path,
                                    "editor.brush_world.create_box name must be non-empty when present");
        yyjson_val *preview_mode = obj_get(action, "preview_mode");
        if (preview_mode != NULL && (!yyjson_is_str(preview_mode) || yyjson_get_str(preview_mode)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.brush_world.create_box preview_mode must be non-empty");
        yyjson_val *min = obj_get(action, "min");
        yyjson_val *max = obj_get(action, "max");
        if (!from_preview && (!is_exact_vec_array(min, 3) || !is_exact_vec_array(max, 3)))
            return validation_error(ctx, json_path, "editor.brush_world.create_box requires min and max vec3 values");
        if (position_from != NULL && SDL_strcmp(position_from, "selection_point") != 0 &&
            SDL_strcmp(position_from, "placement_preview") != 0)
            return validation_error(ctx, json_path,
                                    "editor.brush_world.create_box position_from must be selection_point or "
                                    "placement_preview");
        yyjson_val *position_offset = obj_get(action, "position_offset");
        if (position_offset != NULL && !is_exact_vec_array(position_offset, 3))
            return validation_error(ctx, json_path, "editor.brush_world.create_box position_offset must be a vec3");
        yyjson_val *snap = obj_get(action, "snap");
        if (snap != NULL && (!yyjson_is_num(snap) || yyjson_get_num(snap) <= 0.0))
            return validation_error(ctx, json_path, "editor.brush_world.create_box snap must be a positive number");
        if (!from_preview)
        {
            const double min_x = yyjson_get_num(yyjson_arr_get(min, 0));
            const double min_y = yyjson_get_num(yyjson_arr_get(min, 1));
            const double min_z = yyjson_get_num(yyjson_arr_get(min, 2));
            const double max_x = yyjson_get_num(yyjson_arr_get(max, 0));
            const double max_y = yyjson_get_num(yyjson_arr_get(max, 1));
            const double max_z = yyjson_get_num(yyjson_arr_get(max, 2));
            if (!(min_x < max_x && min_y < max_y && min_z < max_z))
                return validation_error(ctx, json_path, "editor.brush_world.create_box bounds require min < max");
        }
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.brush_world.create_box outputs must be an object");
        static const char *const output_keys[] = {
            "valid_key", "message_key",  "brush_key",          "world_key",      "source_path_key",
            "dirty_key", "revision_key", "saved_revision_key", "bounds_min_key", "bounds_max_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.brush_world.create_box output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.player_start.place") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "editor.player_start.place requires a non-empty name");
        const char *scene = json_string(action, "scene");
        const char *target = json_string(action, "target");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
            return false;
        if (target != NULL && !require_actor_ref(ctx, names, target, json_path))
            return false;
        yyjson_val *position = obj_get(action, "position");
        if (position != NULL && !is_exact_vec_array(position, 3))
            return validation_error(ctx, json_path, "editor.player_start.place position must be a vec3");
        const char *position_from = json_string(action, "position_from");
        if (position != NULL && position_from != NULL)
            return validation_error(ctx, json_path,
                                    "editor.player_start.place requires position or position_from, not both");
        yyjson_val *preview_mode = obj_get(action, "preview_mode");
        if (preview_mode != NULL && (!yyjson_is_str(preview_mode) || yyjson_get_str(preview_mode)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.player_start.place preview_mode must be non-empty");
        if (position_from != NULL && SDL_strcmp(position_from, "selection_point") != 0 &&
            SDL_strcmp(position_from, "placement_preview") != 0)
            return validation_error(ctx, json_path,
                                    "editor.player_start.place position_from must be selection_point or "
                                    "placement_preview");
        yyjson_val *yaw = obj_get(action, "yaw");
        yyjson_val *pitch = obj_get(action, "pitch");
        yyjson_val *apply_to_target = obj_get(action, "apply_to_target");
        if ((yaw != NULL && !yyjson_is_num(yaw)) || (pitch != NULL && !yyjson_is_num(pitch)) ||
            (apply_to_target != NULL && !yyjson_is_bool(apply_to_target)))
        {
            return validation_error(ctx, json_path,
                                    "editor.player_start.place yaw/pitch must be numeric and apply_to_target bool");
        }
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.player_start.place outputs must be an object");
        static const char *const output_keys[] = {"valid_key",  "message_key",  "player_start_key",  "scene_key",
                                                  "target_key", "position_key", "yaw_key",           "pitch_key",
                                                  "dirty_key",  "revision_key", "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.player_start.place output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.player_start.apply") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "editor.player_start.apply requires a non-empty name");
        const char *name = json_string(action, "name");
        if (!require_ref(ctx, &names->editor_player_starts, "editor player start", name, json_path))
            return false;
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.player_start.apply outputs must be an object");
        static const char *const output_keys[] = {"valid_key",  "message_key",  "player_start_key",  "scene_key",
                                                  "target_key", "position_key", "yaw_key",           "pitch_key",
                                                  "dirty_key",  "revision_key", "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.player_start.apply output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.player_start.delete") == 0)
    {
        const char *name = json_string(action, "name");
        yyjson_val *name_from_selection_value = obj_get(action, "name_from_selection");
        if (name_from_selection_value != NULL && !yyjson_is_bool(name_from_selection_value))
            return validation_error(ctx, json_path, "editor.player_start.delete name_from_selection must be bool");
        const bool name_from_selection =
            name_from_selection_value != NULL && yyjson_get_bool(name_from_selection_value);
        if ((name == NULL || name[0] == '\0') && !name_from_selection)
            return validation_error(ctx, json_path, "editor.player_start.delete requires name or name_from_selection");
        if (name != NULL && !require_ref(ctx, &names->editor_player_starts, "editor player start", name, json_path))
            return false;
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.player_start.delete outputs must be an object");
        static const char *const output_keys[] = {"valid_key", "message_key",  "player_start_key",
                                                  "dirty_key", "revision_key", "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.player_start.delete output keys must be non-empty strings");
        }
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
        yyjson_val *then_actions = obj_get(action, "then");
        yyjson_val *else_actions = obj_get(action, "else");
        return (then_actions == NULL || validate_action_array(ctx, then_actions, then_path, names)) &&
               (else_actions == NULL || validate_action_array(ctx, else_actions, else_path, names));
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

static bool validate_app_refs(validation_context *ctx, yyjson_val *root, validation_names *names)
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

static bool validate_cameras(validation_context *ctx, yyjson_val *root, validation_names *names)
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

static bool validate_world_metadata(validation_context *ctx, yyjson_val *root)
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

bool validate_data_condition(validation_context *ctx, yyjson_val *condition, const char *path, validation_names *names)
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
    if (SDL_strcmp(type != NULL ? type : "", "payload.compare") == 0)
    {
        if (!is_non_empty_string(condition, "key"))
            return validation_error(ctx, path, "payload.compare condition requires a non-empty key");
        if (!is_compare_op(json_string(condition, "op")))
            return validation_error(ctx, path, "payload.compare condition requires a supported comparison operator");
        if (obj_get(condition, "value") == NULL)
            return validation_error(ctx, path, "payload.compare condition requires a value");
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

static bool ui_metric_name_valid(const char *metric)
{
    if (metric == NULL || metric[0] == '\0')
        return false;

    static const char *const metrics[] = {
        "fps",
        "frame",
        "paused",
        "perf.frame_ms",
        "perf.update_cpu_ms",
        "perf.render_cpu_ms",
        "render.model_mesh_submissions_per_frame",
        "render.model_mesh_draws_per_frame",
        "render.model_triangles_per_frame",
        "render.geometry_draw_calls_per_frame",
        "render.static_mesh_instanced_draw_calls_per_frame",
        "render.static_mesh_instances_batched_per_frame",
        "render.static_mesh_draw_calls_saved_per_frame",
        "render.procedural_lod_candidates_per_frame",
        "render.procedural_lod_reduced_per_frame",
        "render.procedural_lod_authored_triangles_per_frame",
        "render.procedural_lod_resolved_triangles_per_frame",
        "render.procedural_lod_triangles_saved_per_frame",
        "render.model_lod_candidates_per_frame",
        "render.model_lod_culled_per_frame",
        "render.model_lod_triangles_saved_per_frame",
        "render.depth_prepass_draws_per_frame",
        "render.depth_prepass_triangles_per_frame",
        "render.depth_prepass_samples_per_frame",
        "render.geometry_samples_per_frame",
        "render.light_candidates_per_frame",
        "render.lights_selected_per_frame",
        "render.light_selection_draws_per_frame",
        "render.light_selection_ratio",
        "render.world_scale",
        "render.world_width",
        "render.world_height",
        "render.window_pixel_width",
        "render.window_pixel_height",
        "render.window_pixel_density",
        "brush.trace_count",
        "brush.world_instance_count",
        "brush.world_bounds_reject_count",
        "brush.brush_count",
        "brush.contents_reject_count",
        "brush.bounds_reject_count",
        "brush.collision_candidate_count",
        "brush.hit_count",
        "brush.render_mesh_submissions",
        "brush.render_mesh_culled",
        "brush.render_mesh_draws",
        "brush.render_triangles_submitted",
        "brush.compile_face_count",
        "brush.compile_rendered_face_count",
        "brush.compile_culled_face_count",
        "brush.compile_triangle_count",
        "brush.compile_chunk_count",
        "brush.collision_chunk_count",
        "brush.collision_chunk_reject_count",
        "brush.render_chunk_draws",
        "brush.render_chunk_brushes_drawn",
        "brush.frustum_brush_candidates",
        "brush.frustum_brush_culled",
        "brush.frustum_triangles_culled",
        "brush.visibility_brush_candidates",
        "brush.visibility_brush_visible",
        "brush.visibility_brush_occluded",
        "brush.visibility_triangles_culled",
        "brush.visibility_grid_cache_hits",
        "brush.visibility_grid_cache_misses",
    };

    for (size_t i = 0; i < SDL_arraysize(metrics); ++i)
    {
        if (SDL_strcmp(metric, metrics[i]) == 0)
            return true;
    }
    return false;
}

static bool is_json_scalar(yyjson_val *value)
{
    return yyjson_is_str(value) || yyjson_is_int(value) || yyjson_is_real(value) || yyjson_is_bool(value);
}

static bool validate_ui_tool_color(validation_context *ctx, yyjson_val *object, const char *key, const char *path)
{
    yyjson_val *value = obj_get(object, key);
    if (value != NULL && !is_exact_vec3_or_vec4_array(value))
        return validation_error(ctx, path, "UI tooling %s must be a vec3 or vec4 color", key);
    return true;
}

static bool validate_ui_tool_binding(validation_context *ctx, yyjson_val *binding, const char *path,
                                     validation_names *names)
{
    if (!yyjson_is_obj(binding))
        return validation_error(ctx, path, "UI tooling binding must be an object");
    const char *type = json_string(binding, "type");
    if (SDL_strcmp(type != NULL ? type : "", "scene_state") == 0)
    {
        if (!is_non_empty_string(binding, "key"))
            return validation_error(ctx, path, "UI tooling scene_state binding requires a non-empty key");
    }
    else if (SDL_strcmp(type != NULL ? type : "", "property") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(binding, "entity"), path))
            return false;
        if (!is_non_empty_string(binding, "key"))
            return validation_error(ctx, path, "UI tooling property binding requires a non-empty key");
    }
    else if (SDL_strcmp(type != NULL ? type : "", "metric") == 0)
    {
        const char *metric = json_string(binding, "metric");
        if (!ui_metric_name_valid(metric))
            return validation_error(ctx, path, "unsupported UI tooling metric '%s'",
                                    metric != NULL ? metric : "<missing>");
    }
    else
    {
        return validation_error(ctx, path, "unsupported UI tooling binding type '%s'",
                                type != NULL ? type : "<missing>");
    }

    yyjson_val *fallback = obj_get(binding, "default");
    if (fallback != NULL && !is_json_scalar(fallback))
        return validation_error(ctx, path, "UI tooling binding default must be scalar");
    return true;
}

static bool validate_ui_panels(validation_context *ctx, yyjson_val *panels, const char *path, validation_names *names)
{
    if (panels == NULL)
        return true;
    if (!yyjson_is_arr(panels))
        return validation_error(ctx, path, "UI panels must be an array");
    for (size_t i = 0; i < yyjson_arr_size(panels); ++i)
    {
        char panel_path[PATH_BUFFER_SIZE];
        format_path(panel_path, sizeof(panel_path), "%s[%zu]", path, i);
        yyjson_val *panel = yyjson_arr_get(panels, i);
        if (!yyjson_is_obj(panel))
            return validation_error(ctx, panel_path, "UI panel entries must be objects");
        if (!is_non_empty_string(panel, "name"))
            return validation_error(ctx, panel_path, "UI panel requires a non-empty name");
        yyjson_val *width = obj_get(panel, "w");
        if (width == NULL)
            width = obj_get(panel, "width");
        yyjson_val *height = obj_get(panel, "h");
        if (height == NULL)
            height = obj_get(panel, "height");
        if (!yyjson_is_num(width) || yyjson_get_num(width) <= 0.0)
            return validation_error(ctx, panel_path, "UI panel width must be positive");
        if (!yyjson_is_num(height) || yyjson_get_num(height) <= 0.0)
            return validation_error(ctx, panel_path, "UI panel height must be positive");
        yyjson_val *border = obj_get(panel, "border_thickness");
        if (border != NULL && (!yyjson_is_num(border) || yyjson_get_num(border) < 0.0))
            return validation_error(ctx, panel_path, "UI panel border_thickness must be non-negative");
        if (!validate_ui_tool_color(ctx, panel, "color", panel_path) ||
            !validate_ui_tool_color(ctx, panel, "border_color", panel_path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", panel_path);
        if (!validate_data_condition(ctx, obj_get(panel, "visible_if"), condition_path, names))
            return false;
    }
    return true;
}

static bool validate_ui_inspectors(validation_context *ctx, yyjson_val *inspectors, const char *path,
                                   validation_names *names)
{
    if (inspectors == NULL)
        return true;
    if (!yyjson_is_arr(inspectors))
        return validation_error(ctx, path, "UI inspectors must be an array");
    for (size_t i = 0; i < yyjson_arr_size(inspectors); ++i)
    {
        char inspector_path[PATH_BUFFER_SIZE];
        format_path(inspector_path, sizeof(inspector_path), "%s[%zu]", path, i);
        yyjson_val *inspector = yyjson_arr_get(inspectors, i);
        if (!yyjson_is_obj(inspector))
            return validation_error(ctx, inspector_path, "UI inspector entries must be objects");
        if (!is_non_empty_string(inspector, "name"))
            return validation_error(ctx, inspector_path, "UI inspector requires a non-empty name");
        if (json_string(inspector, "font") != NULL &&
            !require_ref(ctx, &names->fonts, "font asset", json_string(inspector, "font"), inspector_path))
            return false;
        yyjson_val *width = obj_get(inspector, "w");
        if (width == NULL)
            width = obj_get(inspector, "width");
        if (width != NULL && (!yyjson_is_num(width) || yyjson_get_num(width) <= 0.0))
            return validation_error(ctx, inspector_path, "UI inspector width must be positive");
        yyjson_val *row_height = obj_get(inspector, "row_height");
        if (row_height != NULL && (!yyjson_is_num(row_height) || yyjson_get_num(row_height) <= 0.0))
            return validation_error(ctx, inspector_path, "UI inspector row_height must be positive");
        yyjson_val *rows = obj_get(inspector, "rows");
        if (!yyjson_is_arr(rows))
            return validation_error(ctx, inspector_path, "UI inspector requires a rows array");
        if (!validate_ui_tool_color(ctx, inspector, "background_color", inspector_path) ||
            !validate_ui_tool_color(ctx, inspector, "row_color", inspector_path) ||
            !validate_ui_tool_color(ctx, inspector, "title_color", inspector_path) ||
            !validate_ui_tool_color(ctx, inspector, "label_color", inspector_path) ||
            !validate_ui_tool_color(ctx, inspector, "value_color", inspector_path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", inspector_path);
        if (!validate_data_condition(ctx, obj_get(inspector, "visible_if"), condition_path, names))
            return false;
        for (size_t row_index = 0; row_index < yyjson_arr_size(rows); ++row_index)
        {
            char row_path[PATH_BUFFER_SIZE];
            format_path(row_path, sizeof(row_path), "%s.rows[%zu]", inspector_path, row_index);
            yyjson_val *row = yyjson_arr_get(rows, row_index);
            if (!yyjson_is_obj(row))
                return validation_error(ctx, row_path, "UI inspector rows must be objects");
            if (!is_non_empty_string(row, "label"))
                return validation_error(ctx, row_path, "UI inspector row requires a non-empty label");
            yyjson_val *binding = obj_get(row, "binding");
            yyjson_val *value = obj_get(row, "value");
            if ((binding == NULL) == (value == NULL))
                return validation_error(ctx, row_path, "UI inspector row requires exactly one of binding or value");
            if (binding != NULL)
            {
                char binding_path[PATH_BUFFER_SIZE];
                format_path(binding_path, sizeof(binding_path), "%s.binding", row_path);
                if (!validate_ui_tool_binding(ctx, binding, binding_path, names))
                    return false;
            }
            else if (!is_json_scalar(value))
            {
                return validation_error(ctx, row_path, "UI inspector row value must be scalar");
            }
            yyjson_val *empty = obj_get(row, "empty");
            if (empty != NULL && !yyjson_is_str(empty))
                return validation_error(ctx, row_path, "UI inspector row empty value must be a string");
        }
    }
    return true;
}

static bool validate_ui(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *ui = obj_get(root, "ui");
    yyjson_val *texts = obj_get(ui, "text");
    yyjson_val *images = obj_get(ui, "images");
    yyjson_val *rects = obj_get(ui, "rects");
    yyjson_val *menus = obj_get(ui, "menus");
    yyjson_val *panels = obj_get(ui, "panels");
    yyjson_val *inspectors = obj_get(ui, "inspectors");
    if (texts == NULL && images == NULL && rects == NULL && menus == NULL && panels == NULL && inspectors == NULL)
        return true;
    if (texts != NULL && !yyjson_is_arr(texts))
        return validation_error(ctx, "$.ui.text", "UI text must be an array");
    if (images != NULL && !yyjson_is_arr(images))
        return validation_error(ctx, "$.ui.images", "UI images must be an array");
    if (rects != NULL && !yyjson_is_arr(rects))
        return validation_error(ctx, "$.ui.rects", "UI rectangles must be an array");
    if (menus != NULL && !yyjson_is_arr(menus))
        return validation_error(ctx, "$.ui.menus", "UI menus must be an array");
    if (!validate_ui_panels(ctx, panels, "$.ui.panels", names) ||
        !validate_ui_inspectors(ctx, inspectors, "$.ui.inspectors", names))
        return false;

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
            else if (SDL_strcmp(type != NULL ? type : "", "metric") == 0)
            {
                const char *metric = json_string(binding, "metric");
                if (!ui_metric_name_valid(metric))
                    return validation_error(ctx, binding_path, "unsupported UI metric '%s'",
                                            metric != NULL ? metric : "<missing>");
            }
            else
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

    for (size_t i = 0; yyjson_is_arr(rects) && i < yyjson_arr_size(rects); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.ui.rects[%zu]", i);
        yyjson_val *rect = yyjson_arr_get(rects, i);
        if (!yyjson_is_obj(rect))
            return validation_error(ctx, path, "UI rectangle entries must be objects");
        if (!is_non_empty_string(rect, "name"))
            return validation_error(ctx, path, "UI rectangle requires a non-empty name");
        yyjson_val *alpha_source = obj_get(rect, "alpha_source");
        if (alpha_source != NULL)
        {
            if (!yyjson_is_obj(alpha_source))
                return validation_error(ctx, path, "UI rectangle alpha_source must be an object");
            if (!require_ref(ctx, &names->entities, "entity", json_string(alpha_source, "target"), path))
                return false;
            if (!is_non_empty_string(alpha_source, "key"))
                return validation_error(ctx, path, "UI rectangle alpha_source requires a non-empty key");
            yyjson_val *min_value = obj_get(alpha_source, "min");
            yyjson_val *max_value = obj_get(alpha_source, "max");
            if (min_value != NULL && !yyjson_is_num(min_value))
                return validation_error(ctx, path, "UI rectangle alpha_source min must be numeric");
            if (max_value != NULL && !yyjson_is_num(max_value))
                return validation_error(ctx, path, "UI rectangle alpha_source max must be numeric");
            if (yyjson_is_num(min_value) && yyjson_is_num(max_value) &&
                yyjson_get_real(max_value) < yyjson_get_real(min_value))
                return validation_error(ctx, path, "UI rectangle alpha_source max must be >= min");
        }
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", path);
        if (!validate_data_condition(ctx, obj_get(rect, "visible_if"), condition_path, names))
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
        yyjson_val *enabled = obj_get(light, "enabled");
        if (enabled != NULL && !yyjson_is_bool(enabled))
            return validation_error(ctx, path, "light enabled must be a boolean");
        yyjson_val *enabled_key = obj_get(light, "enabled_key");
        if (enabled_key != NULL && !is_non_empty_string(light, "enabled_key"))
            return validation_error(ctx, path, "light enabled_key must be non-empty");
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

static bool editor_command_name_valid(const char *value)
{
    return value != NULL &&
           (SDL_strcmp(value, "translate") == 0 || SDL_strcmp(value, "paint") == 0 ||
            SDL_strcmp(value, "resize") == 0 || SDL_strcmp(value, "extrude") == 0 || SDL_strcmp(value, "delete") == 0);
}

static bool editor_command_target_name_valid(const char *value)
{
    return value != NULL &&
           (SDL_strcmp(value, "selection") == 0 || SDL_strcmp(value, "world") == 0 ||
            SDL_strcmp(value, "element") == 0 || SDL_strcmp(value, "face") == 0 || SDL_strcmp(value, "material") == 0);
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

static bool valid_render_profile_name(const char *name)
{
    return name != NULL &&
           (SDL_strcasecmp(name, "modern") == 0 || SDL_strcasecmp(name, "ps1") == 0 ||
            SDL_strcasecmp(name, "n64") == 0 || SDL_strcasecmp(name, "dos") == 0 || SDL_strcasecmp(name, "snes") == 0 ||
            SDL_strcasecmp(name, "grayscale") == 0 || SDL_strcasecmp(name, "gameboy") == 0);
}

static bool valid_tonemap_name(const char *name)
{
    return name != NULL && (SDL_strcasecmp(name, "none") == 0 || SDL_strcasecmp(name, "reinhard") == 0 ||
                            SDL_strcasecmp(name, "aces") == 0);
}

static bool validate_render_tunable_values(validation_context *ctx, yyjson_val *object, const char *path)
{
    yyjson_val *lighting = obj_get(object, "lighting");
    yyjson_val *bloom = obj_get(object, "bloom");
    yyjson_val *ssao = obj_get(object, "ssao");
    yyjson_val *depth_prepass = obj_get(object, "depth_prepass");
    yyjson_val *per_object_light_selection = obj_get(object, "per_object_light_selection");
    yyjson_val *procedural_lod = obj_get(object, "procedural_lod");
    yyjson_val *model_lod_culling = obj_get(object, "model_lod_culling");
    yyjson_val *performance_queries = obj_get(object, "performance_queries");
    if ((lighting != NULL && !yyjson_is_bool(lighting)) || (bloom != NULL && !yyjson_is_bool(bloom)) ||
        (ssao != NULL && !yyjson_is_bool(ssao)) || (depth_prepass != NULL && !yyjson_is_bool(depth_prepass)) ||
        (per_object_light_selection != NULL && !yyjson_is_bool(per_object_light_selection)) ||
        (procedural_lod != NULL && !yyjson_is_bool(procedural_lod)) ||
        (model_lod_culling != NULL && !yyjson_is_bool(model_lod_culling)) ||
        (performance_queries != NULL && !yyjson_is_bool(performance_queries)))
    {
        return validation_error(ctx, path,
                                "render lighting, bloom, ssao, depth_prepass, per_object_light_selection, "
                                "procedural_lod, model_lod_culling, and performance_queries must be booleans");
    }
    yyjson_val *per_object_light_limit = obj_get(object, "per_object_light_limit");
    if (per_object_light_limit != NULL &&
        (!yyjson_is_int(per_object_light_limit) || yyjson_get_int(per_object_light_limit) < 0 ||
         yyjson_get_int(per_object_light_limit) > SLAYER3D_MAX_SHADER_LIGHTS))
    {
        return validation_error(ctx, path, "render per_object_light_limit must be an integer from 0 to %d",
                                SLAYER3D_MAX_SHADER_LIGHTS);
    }
    yyjson_val *world_render_scale = obj_get(object, "world_render_scale");
    if (world_render_scale != NULL &&
        (!yyjson_is_num(world_render_scale) || yyjson_get_num(world_render_scale) < 0.25 ||
         yyjson_get_num(world_render_scale) > 1.0))
    {
        return validation_error(ctx, path, "render world_render_scale must be a number from 0.25 to 1.0");
    }
    yyjson_val *procedural_lod_near_pixels = obj_get(object, "procedural_lod_near_pixels");
    yyjson_val *procedural_lod_far_pixels = obj_get(object, "procedural_lod_far_pixels");
    if ((procedural_lod_near_pixels != NULL &&
         (!yyjson_is_num(procedural_lod_near_pixels) || yyjson_get_num(procedural_lod_near_pixels) <= 0.0)) ||
        (procedural_lod_far_pixels != NULL &&
         (!yyjson_is_num(procedural_lod_far_pixels) || yyjson_get_num(procedural_lod_far_pixels) <= 0.0)))
    {
        return validation_error(ctx, path, "render procedural_lod pixel thresholds must be positive numbers");
    }
    if (procedural_lod_near_pixels != NULL && procedural_lod_far_pixels != NULL &&
        yyjson_get_num(procedural_lod_far_pixels) > yyjson_get_num(procedural_lod_near_pixels))
    {
        return validation_error(ctx, path,
                                "render procedural_lod_far_pixels must be less than or equal to "
                                "procedural_lod_near_pixels");
    }
    yyjson_val *procedural_lod_min_segments = obj_get(object, "procedural_lod_min_segments");
    if (procedural_lod_min_segments != NULL &&
        (!yyjson_is_int(procedural_lod_min_segments) || yyjson_get_int(procedural_lod_min_segments) < 3 ||
         yyjson_get_int(procedural_lod_min_segments) > 64))
    {
        return validation_error(ctx, path, "render procedural_lod_min_segments must be an integer from 3 to 64");
    }
    yyjson_val *model_lod_cull_pixels = obj_get(object, "model_lod_cull_pixels");
    if (model_lod_cull_pixels != NULL &&
        (!yyjson_is_num(model_lod_cull_pixels) || yyjson_get_num(model_lod_cull_pixels) < 0.0))
    {
        return validation_error(ctx, path, "render model_lod_cull_pixels must be a non-negative number");
    }
    return true;
}

static bool validate_render_settings(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *render = obj_get(root, "render");
    if (render == NULL)
        return true;
    if (!yyjson_is_obj(render))
        return validation_error(ctx, "$.render", "render must be an object");

    if (!validate_render_tunable_values(ctx, render, "$.render"))
        return false;
    if (obj_get(render, "clear_color") != NULL && !is_vec_array(obj_get(render, "clear_color"), 3))
        return validation_error(ctx, "$.render.clear_color", "render clear_color must be a vec3 or vec4 color");
    const char *tonemap = json_string(render, "tonemap");
    if (tonemap != NULL && !valid_tonemap_name(tonemap))
        return validation_error(ctx, "$.render.tonemap", "render tonemap must be none, reinhard, or aces");
    const char *profile = json_string(render, "profile");
    if (profile != NULL && !valid_render_profile_name(profile))
        return validation_error(ctx, "$.render.profile", "render profile is unknown");

    const char *key_fields[] = {"lighting_key",
                                "bloom_key",
                                "ssao_key",
                                "depth_prepass_key",
                                "tonemap_key",
                                "profile_key",
                                "quality_key",
                                "performance_queries_key",
                                "world_render_scale_key",
                                "per_object_light_selection_key",
                                "per_object_light_limit_key",
                                "procedural_lod_key",
                                "procedural_lod_near_pixels_key",
                                "procedural_lod_far_pixels_key",
                                "procedural_lod_min_segments_key",
                                "model_lod_culling_key",
                                "model_lod_cull_pixels_key"};
    for (size_t i = 0; i < SDL_arraysize(key_fields); ++i)
    {
        if (obj_get(render, key_fields[i]) != NULL && !is_non_empty_string(render, key_fields[i]))
            return validation_error(ctx, "$.render", "render scene-state key fields must be non-empty strings");
    }
    yyjson_val *quality_presets = obj_get(render, "quality_presets");
    if (quality_presets != NULL && !yyjson_is_arr(quality_presets))
        return validation_error(ctx, "$.render.quality_presets", "render quality_presets must be an array");
    if (obj_get(render, "quality") != NULL && !is_non_empty_string(render, "quality"))
        return validation_error(ctx, "$.render.quality", "render quality must be a non-empty string");
    name_table quality_names;
    SDL_zero(quality_names);
    bool ok = true;
    for (size_t i = 0; ok && yyjson_is_arr(quality_presets) && i < yyjson_arr_size(quality_presets); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.render.quality_presets[%zu]", i);
        yyjson_val *preset = yyjson_arr_get(quality_presets, i);
        if (!yyjson_is_obj(preset))
        {
            ok = validation_error(ctx, path, "render quality preset must be an object");
            break;
        }
        ok = require_unique_name(ctx, &quality_names, "render quality preset", json_string(preset, "name"), path) &&
             (obj_get(preset, "label") == NULL || is_non_empty_string(preset, "label") ||
              validation_error(ctx, path, "render quality preset label must be a non-empty string")) &&
             validate_render_tunable_values(ctx, preset, path);
    }
    const char *quality = json_string(render, "quality");
    if (ok && quality != NULL && !name_table_contains(&quality_names, quality))
        ok = validation_error(ctx, "$.render.quality", "unknown render quality preset '%s'", quality);
    name_table_destroy(&quality_names);
    if (!ok)
        return false;
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
           validate_editor_player_starts(ctx, root, names) && validate_sector_doors(ctx, root, names) &&
           validate_sector_platforms(ctx, root, names) && validate_actor_archetypes_and_pools(ctx, root, names) &&
           validate_network(ctx, root, names) && validate_app_refs(ctx, root, names) &&
           validate_cameras(ctx, root, names) && validate_ui(ctx, root, names) &&
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

    char *base_dir = path_dirname(path);
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

    char *base_dir = path_dirname(asset_path_without_scheme(asset_path));
    const bool ok = slayer3d_game_data_validate_document_with_source_map(
        yyjson_doc_get_root(doc), asset_path, base_dir, assets, source_map, options, error_buffer, error_buffer_size);
    SDL_free(base_dir);
    slayer3d_game_data_source_map_destroy(source_map);
    yyjson_doc_free(doc);
    return ok;
}

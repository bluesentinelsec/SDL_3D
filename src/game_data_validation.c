/**
 * @file game_data_validation.c
 * @brief Validation for JSON-authored game data.
 */

#include "game_data_validation_internal.h"

#include <float.h>
#include <stdarg.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/actor_controller.h"
#include "slayer3d/input.h"
#include "slayer3d/lighting.h"
#include "slayer3d/sprite_actor.h"

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

static bool validation_asset_path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;

    const char c0 = path[0];
    const char c1 = path[1];
    const char c2 = path[2];

    if (c0 == '/' || c0 == '\\')
        return true;

    return (((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z')) && c1 == ':' && (c2 == '/' || c2 == '\\'));
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

bool validate_optional_non_empty_string_value(validation_context *ctx, yyjson_val *value, const char *json_path,
                                              const char *description)
{
    if (value == NULL)
        return true;
    if (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0')
        return validation_error(ctx, json_path, "%s must be a non-empty string", description);
    return true;
}

bool validate_optional_number_value(validation_context *ctx, yyjson_val *value, const char *json_path,
                                    const char *description)
{
    if (value == NULL)
        return true;
    if (!yyjson_is_num(value))
        return validation_error(ctx, json_path, "%s must be a number", description);
    return true;
}

bool validate_optional_positive_number_value(validation_context *ctx, yyjson_val *value, const char *json_path,
                                             const char *description)
{
    if (value == NULL)
        return true;
    if (!yyjson_is_num(value) || yyjson_get_num(value) <= 0.0)
        return validation_error(ctx, json_path, "%s must be positive", description);
    return true;
}

bool validate_optional_non_negative_number_value(validation_context *ctx, yyjson_val *value, const char *json_path,
                                                 const char *description)
{
    if (value == NULL)
        return true;
    if (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0)
        return validation_error(ctx, json_path, "%s must be non-negative", description);
    return true;
}

bool validate_non_empty_string_field(validation_context *ctx, yyjson_val *json, const char *json_path, const char *type,
                                     const char *field)
{
    yyjson_val *value = obj_get(json, field);
    if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
        return validation_error(ctx, json_path, "%s %s must be a non-empty string", type, field);
    return true;
}

bool validate_exactly_one_field_present(validation_context *ctx, yyjson_val *json, const char *json_path,
                                        const char *type, const char *first_field, const char *second_field)
{
    const bool has_first = obj_get(json, first_field) != NULL;
    const bool has_second = obj_get(json, second_field) != NULL;
    if (has_first == has_second)
    {
        return validation_error(ctx, json_path, "%s requires exactly one of %s or %s", type, first_field, second_field);
    }
    return true;
}

bool validate_optional_signal_field(validation_context *ctx, yyjson_val *json, const char *json_path,
                                    validation_names *names, const char *field)
{
    const char *signal = json_string(json, field);
    return signal == NULL || require_ref(ctx, &names->signals, "signal", signal, json_path);
}

bool validate_optional_output_keys(validation_context *ctx, yyjson_val *json, const char *json_path, const char *type,
                                   const char *const *fields, size_t field_count)
{
    yyjson_val *outputs = obj_get(json, "outputs");
    if (outputs == NULL)
        return true;
    if (!yyjson_is_obj(outputs))
        return validation_error(ctx, json_path, "%s outputs must be an object", type);
    for (size_t i = 0; i < field_count; ++i)
    {
        yyjson_val *field = obj_get(outputs, fields[i]);
        if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
            return validation_error(ctx, json_path, "%s output keys must be non-empty strings", type);
    }
    return true;
}

bool note_name(name_table *table, const char *name, const char *json_path)
{
    if (name == NULL || name[0] == '\0' || name_table_contains(table, name))
        return true;
    return name_table_add(table, name, json_path);
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

    if (validation_asset_path_is_absolute(asset_path))
    {
        SDL_PathInfo info;
        if (SDL_GetPathInfo(asset_path, &info) && info.type == SDL_PATHTYPE_FILE)
            return true;
        return validation_error(ctx, json_path, "%s asset path '%s' does not exist", asset_kind, asset_path);
    }

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
           validate_editor_actors(ctx, root, names) && validate_sector_doors(ctx, root, names) &&
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

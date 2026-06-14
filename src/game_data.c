/**
 * @file game_data.c
 * @brief JSON-authored game data runtime implementation.
 */

#include "slayer3d/game_data.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include "game_data_brush_internal.h"
#include "game_data_standard_options.h"
#include "game_data_validation.h"
#include "lauxlib.h"
#include "lua.h"
#include "network_replication_schema.h"
#include "script_internal.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/asset.h"
#include "slayer3d/collision.h"
#include "slayer3d/door.h"
#include "slayer3d/fps_mover.h"
#include "slayer3d/input.h"
#include "slayer3d/level.h"
#include "slayer3d/math.h"
#include "slayer3d/network.h"
#include "slayer3d/script.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/timer_pool.h"
#include "yyjson.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include "game_data_internal.h"

void set_error(char *buffer, int buffer_size, const char *message)
{
    if (buffer != NULL && buffer_size > 0)
    {
        SDL_snprintf(buffer, (size_t)buffer_size, "%s", message != NULL ? message : "unknown game data error");
    }
}

void set_errorf(char *buffer, int buffer_size, const char *format, ...)
{
    if (buffer == NULL || buffer_size <= 0)
        return;

    va_list args;
    va_start(args, format);
    SDL_vsnprintf(buffer, (size_t)buffer_size, format != NULL ? format : "unknown game data error", args);
    va_end(args);
}

void clear_menu_text_entry_capture(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;

    runtime->text_capture.active = false;
    runtime->text_capture.menu = NULL;
    runtime->text_capture.item_index = -1;
    SDL_free(runtime->text_capture.original);
    runtime->text_capture.original = NULL;
}

bool append_format(char *buffer, size_t buffer_size, size_t *offset, const char *format, ...)
{
    if (buffer == NULL || buffer_size == 0U || offset == NULL || *offset >= buffer_size || format == NULL)
        return false;

    va_list args;
    va_start(args, format);
    const int written = SDL_vsnprintf(buffer + *offset, buffer_size - *offset, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= buffer_size - *offset)
    {
        buffer[buffer_size - 1U] = '\0';
        return false;
    }

    *offset += (size_t)written;
    return true;
}

void free_brush_world_runtime(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return;
    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    slayer3d_free_model(&world_runtime->artifacts.render_model);
    for (int brush_model_index = 0; brush_model_index < world_runtime->artifacts.brush_render_model_count;
         ++brush_model_index)
    {
        slayer3d_free_model(&world_runtime->artifacts.brush_render_models[brush_model_index]);
    }
    for (int chunk_model_index = 0; chunk_model_index < world_runtime->artifacts.chunk_render_model_count;
         ++chunk_model_index)
    {
        slayer3d_free_model(&world_runtime->artifacts.chunk_render_models[chunk_model_index]);
    }
    SDL_free(world_runtime->artifacts.brush_render_models);
    SDL_free(world_runtime->artifacts.chunk_render_models);
    free_brush_world_visibility_grid(world_runtime);
    slayer3d_game_data_brush_world_free_compiled_faces(world);
    slayer3d_game_data_brush_world_free_compile_chunks(world);
    free_editor_brush_source_model(world_runtime);
    SDL_free(world_runtime->editor_source_path);
    free_editor_metadata(&world->editor);
    SDL_free((void *)world->name);
    SDL_free((void *)world->units);
    for (int material_index = 0; material_index < world->material_count; ++material_index)
    {
        slayer3d_game_data_brush_material *material =
            (slayer3d_game_data_brush_material *)&world->materials[material_index];
        free_editor_metadata(&material->editor);
        SDL_free((void *)material->name);
        SDL_free((void *)material->texture);
    }
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        slayer3d_game_data_brush *brush = (slayer3d_game_data_brush *)&world->brushes[brush_index];
        SDL_free((void *)brush->name);
        free_editor_metadata(&brush->editor);
        for (int tag_index = 0; tag_index < brush->tag_count; ++tag_index)
            SDL_free((void *)brush->tags[tag_index]);
        SDL_free((void *)brush->tags);
        for (int face_index = 0; face_index < brush->face_count; ++face_index)
        {
            slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[face_index];
            free_editor_metadata(&face->editor);
        }
        SDL_free((void *)brush->faces);
    }
    SDL_free((void *)world->materials);
    SDL_free((void *)world->brushes);
    SDL_zero(*world_runtime);
}

void free_editor_player_starts_runtime(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < runtime->editor_player_start_count; ++i)
    {
        SDL_free(runtime->editor_player_starts[i].name);
        SDL_free(runtime->editor_player_starts[i].scene);
        SDL_free(runtime->editor_player_starts[i].target);
    }
    SDL_free(runtime->editor_player_starts);
    runtime->editor_player_starts = NULL;
    runtime->editor_player_start_count = 0;
    runtime->editor_player_start_capacity = 0;
}

bool eval_data_condition(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                         const slayer3d_game_data_ui_metrics *metrics);
bool runtime_actor_is_active(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *actor);
bool sensor_actor_list_add(sensor_actor_list *list, slayer3d_registered_actor *actor);
void sensor_actor_list_free(sensor_actor_list *list);
bool collect_sensor_endpoint_actors(slayer3d_game_data_runtime *runtime, const char *actor_name, const char *tag,
                                    sensor_actor_list *out_list);

float game_data_random01(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return 0.0f;
    runtime->rng_state = runtime->rng_state * 1664525u + 1013904223u;
    return (float)((runtime->rng_state >> 8) & 0x00FFFFFFu) / (float)0x01000000u;
}

static bool path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return SDL_strlen(path) > 2 && path[1] == ':';
}

char *path_dirname(const char *path)
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

char *path_basename(const char *path)
{
    if (path == NULL)
        return NULL;

    const char *base = path;
    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    return SDL_strdup(base);
}

const char *asset_path_without_scheme(const char *path)
{
    return path != NULL && SDL_strncmp(path, "asset://", 8) == 0 ? path + 8 : path;
}

char *path_join(const char *base_dir, const char *path)
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

slayer3d_actor_registry *runtime_registry(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? slayer3d_game_session_get_registry(runtime->session) : NULL;
}

slayer3d_signal_bus *runtime_bus(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? slayer3d_game_session_get_signal_bus(runtime->session) : NULL;
}

slayer3d_timer_pool *runtime_timers(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? slayer3d_game_session_get_timer_pool(runtime->session) : NULL;
}

slayer3d_input_manager *runtime_input(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? slayer3d_game_session_get_input(runtime->session) : NULL;
}

void actor_set_position(slayer3d_registered_actor *actor, slayer3d_vec3 position);
void copy_property_value(slayer3d_properties *target, const char *key, const slayer3d_value *value);
bool actor_pool_in_scene(const actor_pool_runtime *pool, const char *scene_name);
bool actor_matches_target_filter(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *target,
                                 const slayer3d_registered_actor *source, yyjson_val *json,
                                 const slayer3d_properties *payload, const char *fallback_tag,
                                 bool fallback_exclude_source);
void actor_lifecycle_defer_begin(slayer3d_game_data_runtime *runtime);
void actor_lifecycle_defer_end(slayer3d_game_data_runtime *runtime);
void slayer3d_game_data_destroy(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;

    slayer3d_signal_bus *bus = runtime_bus(runtime);
    for (int i = 0; i < runtime->binding_count; ++i)
    {
        if (runtime->bindings[i].connection_id != 0)
            slayer3d_signal_disconnect(bus, runtime->bindings[i].connection_id);
    }
    for (int i = 0; i < runtime->adapter_count; ++i)
    {
        SDL_free(runtime->adapters[i].name);
        SDL_free(runtime->adapters[i].lua_script_id);
        SDL_free(runtime->adapters[i].lua_function);
        slayer3d_script_engine_unref(runtime->scripts, runtime->adapters[i].lua_function_ref);
    }
    for (int i = 0; i < runtime->script_count; ++i)
    {
        slayer3d_script_engine_unref(runtime->scripts, runtime->script_entries[i].module_ref);
        SDL_free(runtime->script_entries[i].dependencies);
    }
    for (int i = 0; i < runtime->scene_count; ++i)
    {
        SDL_free(runtime->scenes[i].entities);
        SDL_free(runtime->scenes[i].menus);
        yyjson_doc_free(runtime->scenes[i].doc);
    }
    for (int i = 0; i < runtime->ui_state_count; ++i)
        SDL_free(runtime->ui_states[i].name);
    for (int i = 0; i < runtime->audio_file_count; ++i)
    {
        SDL_free(runtime->audio_files[i].asset_path);
        SDL_free(runtime->audio_files[i].file_path);
    }
    for (int i = 0; i < runtime->property_snapshot_count; ++i)
    {
        SDL_free(runtime->property_snapshots[i].name);
        SDL_free(runtime->property_snapshots[i].target);
        slayer3d_properties_destroy(runtime->property_snapshots[i].properties);
    }
    for (int i = 0; i < runtime->collection_count; ++i)
    {
        SDL_free(runtime->collections[i].name);
        for (int row = 0; row < runtime->collections[i].row_count; ++row)
            slayer3d_properties_destroy(runtime->collections[i].rows[row]);
        SDL_free(runtime->collections[i].rows);
    }
    for (int i = 0; i < runtime->grid_map_count; ++i)
    {
        SDL_free(runtime->grid_maps[i].name);
        SDL_free(runtime->grid_maps[i].cells);
        SDL_free(runtime->grid_maps[i].walkable);
    }
    for (int i = 0; i < runtime->grid_actor_index_count; ++i)
    {
        SDL_free(runtime->grid_actor_indices[i].map);
        SDL_free(runtime->grid_actor_indices[i].pool);
        SDL_free(runtime->grid_actor_indices[i].actors);
    }
    for (int i = 0; i < runtime->grid_pickup_layer_count; ++i)
    {
        SDL_free(runtime->grid_pickup_layers[i].name);
        for (int kind_index = 0; kind_index < runtime->grid_pickup_layers[i].kind_count; ++kind_index)
            SDL_free(runtime->grid_pickup_layers[i].kinds[kind_index].kind);
        SDL_free(runtime->grid_pickup_layers[i].kinds);
        SDL_free(runtime->grid_pickup_layers[i].cells);
        SDL_free(runtime->grid_pickup_layers[i].render_positions);
    }
    clear_menu_text_entry_capture(runtime);
    reset_editor_clip_tool_state(runtime, "");
    free_editor_command_history(&runtime->editor_command_history);

    for (int i = 0; i < runtime->sector_level_count; ++i)
    {
        SDL_free(runtime->sector_levels[i].name);
        SDL_free(runtime->sector_levels[i].sectors);
        SDL_free(runtime->sector_levels[i].sector_names);
        SDL_free(runtime->sector_levels[i].materials);
        SDL_free(runtime->sector_levels[i].lights);
        slayer3d_free_level(&runtime->sector_levels[i].lightmapped);
        slayer3d_free_level(&runtime->sector_levels[i].vertex_baked);
        slayer3d_free_level(&runtime->sector_levels[i].unlit);
        slayer3d_free_level(&runtime->sector_levels[i].lightmapped_without_sector_lighting);
        slayer3d_free_level(&runtime->sector_levels[i].vertex_baked_without_sector_lighting);
        slayer3d_free_level(&runtime->sector_levels[i].unlit_without_sector_lighting);
    }
    for (int i = 0; i < runtime->brush_world_count; ++i)
        free_brush_world_runtime(&runtime->brush_worlds[i]);
    free_editor_player_starts_runtime(runtime);
    free_editor_actors_runtime(runtime);
    free_editor_prefabs_runtime(runtime);
    free_editor_connections_runtime(runtime);
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        SDL_free(runtime->actor_pools[i].name);
        for (int actor_index = 0; actor_index < runtime->actor_pools[i].capacity; ++actor_index)
            SDL_free(runtime->actor_pools[i].actor_names[actor_index]);
        SDL_free(runtime->actor_pools[i].scenes);
        SDL_free(runtime->actor_pools[i].actor_names);
        SDL_free(runtime->actor_pools[i].spawn_generations);
        SDL_free(runtime->actor_pools[i].lifecycle_states);
    }
    for (int i = 0; i < runtime->direct_connect_session_count; ++i)
    {
        SDL_free(runtime->direct_connect_sessions[i].name);
        slayer3d_network_session_destroy(runtime->direct_connect_sessions[i].session);
    }
    for (int i = 0; i < runtime->host_session_count; ++i)
    {
        SDL_free(runtime->host_sessions[i].name);
        slayer3d_network_session_destroy(runtime->host_sessions[i].session);
    }
    for (int i = 0; i < runtime->discovery_session_count; ++i)
    {
        SDL_free(runtime->discovery_sessions[i].name);
        slayer3d_network_discovery_session_destroy(runtime->discovery_sessions[i].session);
    }
    for (int i = 0; i < runtime->network_diagnostic_count; ++i)
        SDL_free(runtime->network_diagnostics[i].name);

    slayer3d_script_engine_destroy(runtime->scripts);
    slayer3d_properties_destroy(runtime->scene_state);
    slayer3d_storage_destroy(runtime->storage);
    if (runtime->owns_assets)
        slayer3d_asset_resolver_destroy(runtime->assets);
    SDL_free(runtime->base_dir);
    SDL_free(runtime->file_base_dir);
    SDL_free(runtime->scenes);
    SDL_free(runtime->script_entries);
    SDL_free(runtime->signals);
    SDL_free(runtime->timers);
    SDL_free(runtime->actions);
    SDL_free(runtime->adapters);
    SDL_free(runtime->bindings);
    for (int i = 0; i < runtime->sensor_count; ++i)
    {
        for (int pair_index = 0; pair_index < runtime->sensors[i].contact_pair_count; ++pair_index)
            sensor_contact_pair_destroy(&runtime->sensors[i].contact_pairs[pair_index]);
        SDL_free(runtime->sensors[i].contact_pairs);
    }
    SDL_free(runtime->sensors);
    SDL_free(runtime->wave_schedules);
    SDL_free(runtime->noise_events);
    SDL_free(runtime->input_bindings);
    SDL_free(runtime->ui_states);
    SDL_free(runtime->animations);
    SDL_free(runtime->audio_files);
    SDL_free(runtime->property_snapshots);
    SDL_free(runtime->collections);
    SDL_free(runtime->grid_maps);
    SDL_free(runtime->grid_actor_indices);
    SDL_free(runtime->grid_pickup_layers);
    SDL_free(runtime->sector_levels);
    SDL_free(runtime->brush_worlds);
    SDL_free(runtime->editor_player_start_source_path);
    SDL_free(runtime->sector_doors);
    SDL_free(runtime->sector_platforms);
    SDL_free(runtime->fps_controllers);
    SDL_free(runtime->patrol_controllers);
    SDL_free(runtime->actor_pools);
    SDL_free(runtime->direct_connect_sessions);
    SDL_free(runtime->host_sessions);
    SDL_free(runtime->discovery_sessions);
    SDL_free(runtime->network_diagnostics);
    SDL_free(runtime->activity.periodic_elapsed);
    yyjson_doc_free(runtime->doc);
    SDL_free(runtime);
}

/**
 * @file game_data_load_runtime.c
 * @brief Runtime registration, script reload, app config, asset loading, and storage helpers.
 */

#include "game_data_internal.h"

#include "game_data_validation.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

static void storage_config_from_root(yyjson_val *root, slayer3d_storage_config *out_config);
static void load_storage_config(slayer3d_game_data_runtime *runtime, yyjson_val *root);

bool slayer3d_game_data_register_adapter(slayer3d_game_data_runtime *runtime, const char *name,
                                         slayer3d_game_data_adapter_fn callback, void *userdata)
{
    if (runtime == NULL || name == NULL || name[0] == '\0' || callback == NULL)
        return false;
    adapter_entry *entry = find_adapter(runtime, name);
    if (entry != NULL)
    {
        SDL_free(entry->lua_script_id);
        entry->lua_script_id = NULL;
        SDL_free(entry->lua_function);
        entry->lua_function = NULL;
        if (entry->lua_function_ref != SLAYER3D_SCRIPT_REF_INVALID)
        {
            slayer3d_script_engine_unref(runtime->scripts, entry->lua_function_ref);
            entry->lua_function_ref = SLAYER3D_SCRIPT_REF_INVALID;
        }
        entry->callback = callback;
        entry->userdata = userdata;
        return true;
    }
    return append_adapter(runtime, name, callback, userdata);
}

bool slayer3d_game_data_reload_scripts(slayer3d_game_data_runtime *runtime, slayer3d_asset_resolver *assets,
                                       char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || assets == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid Lua reload arguments");
        return false;
    }
    if (runtime->script_count == 0)
        return true;

    slayer3d_script_engine *new_engine = slayer3d_script_engine_create();
    if (new_engine == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to create Lua script engine");
        return false;
    }
    register_lua_api(runtime, new_engine);

    slayer3d_script_ref *module_refs =
        (slayer3d_script_ref *)SDL_calloc((size_t)runtime->script_count, sizeof(*module_refs));
    slayer3d_script_ref *function_refs =
        (slayer3d_script_ref *)SDL_calloc((size_t)runtime->adapter_count, sizeof(*function_refs));
    bool *loading = (bool *)SDL_calloc((size_t)runtime->script_count, sizeof(*loading));
    bool *loaded = (bool *)SDL_calloc((size_t)runtime->script_count, sizeof(*loaded));
    if (module_refs == NULL || function_refs == NULL || loading == NULL || loaded == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate Lua reload state");
        SDL_free(module_refs);
        SDL_free(function_refs);
        SDL_free(loading);
        SDL_free(loaded);
        slayer3d_script_engine_destroy(new_engine);
        return false;
    }

    bool ok = true;
    for (int i = 0; i < runtime->script_count && ok; ++i)
    {
        if (runtime->script_entries[i].autoload)
        {
            ok = load_script_index_into_engine(runtime, assets, new_engine, i, module_refs, loading, loaded,
                                               error_buffer, error_buffer_size);
        }
    }

    for (int i = 0; i < runtime->adapter_count && ok; ++i)
    {
        adapter_entry *adapter = &runtime->adapters[i];
        if (adapter->callback != NULL || adapter->lua_script_id == NULL || adapter->lua_function == NULL)
            continue;

        script_entry *script = find_script(runtime, adapter->lua_script_id);
        const int script_index = script != NULL ? (int)(script - runtime->script_entries) : -1;
        if (script_index < 0)
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua adapter %s references missing script %s",
                             adapter->name, adapter->lua_script_id);
            }
            ok = false;
            break;
        }
        if (!load_script_index_into_engine(runtime, assets, new_engine, script_index, module_refs, loading, loaded,
                                           error_buffer, error_buffer_size))
        {
            ok = false;
            break;
        }

        char script_error[256];
        if (!slayer3d_script_engine_ref_module_function(new_engine, module_refs[script_index], adapter->lua_function,
                                                        &function_refs[i], script_error, (int)sizeof(script_error)))
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size,
                             "Lua adapter %s function %s in script %s failed: %s", adapter->name, adapter->lua_function,
                             script->id, script_error);
            }
            ok = false;
        }
    }

    if (!ok)
    {
        for (int i = 0; i < runtime->adapter_count; ++i)
            slayer3d_script_engine_unref(new_engine, function_refs[i]);
        for (int i = 0; i < runtime->script_count; ++i)
            slayer3d_script_engine_unref(new_engine, module_refs[i]);
        SDL_free(module_refs);
        SDL_free(function_refs);
        SDL_free(loading);
        SDL_free(loaded);
        slayer3d_script_engine_destroy(new_engine);
        return false;
    }

    slayer3d_script_engine *old_engine = runtime->scripts;
    for (int i = 0; i < runtime->script_count; ++i)
    {
        runtime->script_entries[i].module_ref = module_refs[i];
        runtime->script_entries[i].loaded = loaded[i];
        runtime->script_entries[i].loading = false;
    }
    runtime->scripts = new_engine;
    for (int i = 0; i < runtime->adapter_count; ++i)
    {
        if (runtime->adapters[i].callback == NULL && runtime->adapters[i].lua_function != NULL)
            runtime->adapters[i].lua_function_ref = function_refs[i];
        else
            runtime->adapters[i].lua_function_ref = SLAYER3D_SCRIPT_REF_INVALID;
    }

    slayer3d_script_engine_destroy(old_engine);
    SDL_free(module_refs);
    SDL_free(function_refs);
    SDL_free(loading);
    SDL_free(loaded);
    return true;
}

static bool apply_app_config_from_root(yyjson_val *root, slayer3d_game_config *out_config, char *title_buffer,
                                       int title_buffer_size, char *error_buffer, int error_buffer_size)
{
    if (!yyjson_is_obj(root) || SDL_strcmp(json_string(root, "schema", ""), "slayer3d.game.v0") != 0)
    {
        set_error(error_buffer, error_buffer_size, "unsupported or missing game data schema");
        return false;
    }

    yyjson_val *app = obj_get(root, "app");
    if (!yyjson_is_obj(app))
        return true;

    const char *title = json_string(app, "title", NULL);
    if (title != NULL && title_buffer != NULL && title_buffer_size > 0)
    {
        SDL_snprintf(title_buffer, (size_t)title_buffer_size, "%s", title);
        out_config->title = title_buffer;
    }
    out_config->width = json_int(app, "window_width", json_int(app, "width", out_config->width));
    out_config->height = json_int(app, "window_height", json_int(app, "height", out_config->height));
    out_config->logical_width = json_int(app, "logical_width", json_int(app, "width", out_config->logical_width));
    out_config->logical_height = json_int(app, "logical_height", json_int(app, "height", out_config->logical_height));
    out_config->icon_path = json_string(app, "icon_path", json_string(app, "icon", out_config->icon_path));
    out_config->backend = parse_backend(json_string(app, "backend", NULL), out_config->backend);
    yyjson_val *window = obj_get(app, "window");
    if (yyjson_is_obj(window))
    {
        const char *window_title = json_string(window, "title", NULL);
        if (window_title != NULL && title_buffer != NULL && title_buffer_size > 0)
        {
            SDL_snprintf(title_buffer, (size_t)title_buffer_size, "%s", window_title);
            out_config->title = title_buffer;
        }
        out_config->width = json_int(window, "window_width", json_int(window, "width", out_config->width));
        out_config->height = json_int(window, "window_height", json_int(window, "height", out_config->height));
        out_config->logical_width = json_int(window, "logical_width", out_config->logical_width);
        out_config->logical_height = json_int(window, "logical_height", out_config->logical_height);
#if defined(SLAYER3D_PRODUCTION_BUILD)
        const char *mode = json_string(window, "production_display_mode", json_string(window, "display_mode", NULL));
#else
        const char *mode = json_string(window, "development_display_mode", json_string(window, "display_mode", NULL));
#endif
        out_config->display_mode = parse_window_mode(mode, out_config->display_mode);
        yyjson_val *vsync = obj_get(window, "vsync");
        if (yyjson_is_bool(vsync))
            out_config->vsync = yyjson_get_bool(vsync) ? 1 : -1;
        yyjson_val *maximized = obj_get(window, "maximized");
        if (yyjson_is_bool(maximized))
            out_config->maximized = yyjson_get_bool(maximized) ? 1 : -1;
        yyjson_val *high_pixel_density = obj_get(window, "high_pixel_density");
        if (yyjson_is_bool(high_pixel_density))
            out_config->high_pixel_density = yyjson_get_bool(high_pixel_density) ? 1 : -1;
        out_config->backend = parse_backend(json_string(window, "renderer", NULL), out_config->backend);
        out_config->icon_path = json_string(window, "icon_path", json_string(window, "icon", out_config->icon_path));
    }
    out_config->tick_rate = json_float(app, "tick_rate", out_config->tick_rate);
    out_config->max_ticks_per_frame = json_int(app, "max_ticks_per_frame", out_config->max_ticks_per_frame);
    out_config->enable_audio = json_bool(app, "enable_audio", out_config->enable_audio);

    yyjson_val *render = obj_get(root, "render");
    if (yyjson_is_obj(render))
    {
        yyjson_val *dynamic_world_render_scale = obj_get(render, "dynamic_world_render_scale");
        if (yyjson_is_bool(dynamic_world_render_scale))
            out_config->dynamic_world_render_scale = yyjson_get_bool(dynamic_world_render_scale);
        out_config->dynamic_world_render_min_scale =
            json_float(render, "dynamic_world_render_min_scale", out_config->dynamic_world_render_min_scale);
        out_config->dynamic_world_render_max_scale =
            json_float(render, "dynamic_world_render_max_scale", out_config->dynamic_world_render_max_scale);
        out_config->dynamic_world_render_target_fps =
            json_float(render, "dynamic_world_render_target_fps", out_config->dynamic_world_render_target_fps);
    }
    return true;
}

static void apply_persisted_app_settings(yyjson_val *root, slayer3d_game_config *out_config)
{
    yyjson_val *app = obj_get(root, "app");
    const char *settings_path = json_string(app, "settings_path", NULL);
    if (settings_path == NULL || settings_path[0] == '\0')
        return;

    slayer3d_storage_config storage_config;
    storage_config_from_root(root, &storage_config);

    char storage_error[256];
    slayer3d_storage *storage = NULL;
    if (!slayer3d_storage_create(&storage_config, &storage, storage_error, (int)sizeof(storage_error)))
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D app settings storage unavailable: %s", storage_error);
        return;
    }

    slayer3d_storage_buffer buffer;
    SDL_zero(buffer);
    if (!slayer3d_storage_read_file(storage, settings_path, &buffer, storage_error, (int)sizeof(storage_error)))
    {
        slayer3d_storage_destroy(storage);
        return;
    }

    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, NULL);
    yyjson_val *settings = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    if (yyjson_is_obj(settings))
    {
        out_config->display_mode =
            parse_window_mode(json_string(settings, "display_mode", NULL), out_config->display_mode);
        out_config->backend = parse_backend(json_string(settings, "renderer", NULL), out_config->backend);
        yyjson_val *vsync = obj_get(settings, "vsync");
        if (yyjson_is_bool(vsync))
            out_config->vsync = yyjson_get_bool(vsync) ? 1 : -1;
    }

    yyjson_doc_free(doc);
    slayer3d_storage_buffer_free(&buffer);
    slayer3d_storage_destroy(storage);
}

bool slayer3d_game_data_load_app_config_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                              slayer3d_game_config *out_config, char *title_buffer,
                                              int title_buffer_size, char *error_buffer, int error_buffer_size)
{
    if (assets == NULL || asset_path == NULL || asset_path[0] == '\0' || out_config == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid app config load arguments");
        return false;
    }

    yyjson_doc *doc = slayer3d_game_data_compose_asset(assets, asset_path, NULL, error_buffer, error_buffer_size);
    if (doc == NULL)
        return false;

    yyjson_val *root = yyjson_doc_get_root(doc);
    const bool ok =
        apply_app_config_from_root(root, out_config, title_buffer, title_buffer_size, error_buffer, error_buffer_size);
    if (ok)
        apply_persisted_app_settings(root, out_config);
    yyjson_doc_free(doc);
    return ok;
}

bool slayer3d_game_data_load_app_config_file(const char *path, slayer3d_game_config *out_config, char *title_buffer,
                                             int title_buffer_size, char *error_buffer, int error_buffer_size)
{
    if (path == NULL || path[0] == '\0' || out_config == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid app config load arguments");
        return false;
    }

    char *base_dir = path_dirname(path);
    char *asset_name = path_basename(path);
    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    if (base_dir == NULL || asset_name == NULL || assets == NULL)
    {
        SDL_free(base_dir);
        SDL_free(asset_name);
        slayer3d_asset_resolver_destroy(assets);
        set_error(error_buffer, error_buffer_size, "failed to allocate app config loader");
        return false;
    }

    char asset_error[256];
    const bool mounted =
        slayer3d_asset_resolver_mount_directory(assets, base_dir, asset_error, (int)sizeof(asset_error));
    bool ok = false;
    if (!mounted)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to mount game data directory %s: %s",
                         base_dir, asset_error);
    }
    else
    {
        ok = slayer3d_game_data_load_app_config_asset(assets, asset_name, out_config, title_buffer, title_buffer_size,
                                                      error_buffer, error_buffer_size);
    }

    slayer3d_asset_resolver_destroy(assets);
    SDL_free(base_dir);
    SDL_free(asset_name);
    return ok;
}

bool slayer3d_game_data_load_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                   slayer3d_game_session *session, slayer3d_game_data_runtime **out_runtime,
                                   char *error_buffer, int error_buffer_size)
{
    slayer3d_game_data_load_options options;
    SDL_zero(options);
    options.session = session;
    return slayer3d_game_data_load_asset_with_options(assets, asset_path, &options, out_runtime, error_buffer,
                                                      error_buffer_size);
}

bool slayer3d_game_data_load_asset_with_options(slayer3d_asset_resolver *assets, const char *asset_path,
                                                const slayer3d_game_data_load_options *options,
                                                slayer3d_game_data_runtime **out_runtime, char *error_buffer,
                                                int error_buffer_size)
{
    if (out_runtime != NULL)
        *out_runtime = NULL;
    if (assets == NULL || asset_path == NULL || asset_path[0] == '\0' || options == NULL || options->session == NULL ||
        out_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid game data load arguments");
        return false;
    }

    slayer3d_game_data_source_map *source_map = NULL;
    yyjson_doc *doc =
        slayer3d_game_data_compose_asset(assets, asset_path, &source_map, error_buffer, error_buffer_size);
    if (doc == NULL)
        return false;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root) || SDL_strcmp(json_string(root, "schema", ""), "slayer3d.game.v0") != 0)
    {
        slayer3d_game_data_source_map_destroy(source_map);
        yyjson_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "unsupported or missing game data schema");
        return false;
    }

    slayer3d_game_data_runtime *runtime = (slayer3d_game_data_runtime *)SDL_calloc(1, sizeof(*runtime));
    if (runtime == NULL)
    {
        slayer3d_game_data_source_map_destroy(source_map);
        yyjson_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to allocate game data runtime");
        return false;
    }
    runtime->doc = doc;
    runtime->session = options->session;
    runtime->assets = assets;
    runtime->base_dir = path_dirname(asset_path_without_scheme(asset_path));
    if (options->source_base_dir != NULL && options->source_base_dir[0] != '\0')
        runtime->file_base_dir = SDL_strdup(options->source_base_dir);
    runtime->scene_state = slayer3d_properties_create();
    runtime->rng_state = 0xC0FFEEu;
    if (runtime->base_dir == NULL || runtime->scene_state == NULL || !game_data_presentation_cache_create(runtime) ||
        (options->source_base_dir != NULL && options->source_base_dir[0] != '\0' && runtime->file_base_dir == NULL))
    {
        slayer3d_game_data_source_map_destroy(source_map);
        slayer3d_game_data_destroy(runtime);
        set_error(error_buffer, error_buffer_size, "failed to allocate game data runtime state");
        return false;
    }
    if (!slayer3d_game_data_validate_document_with_source_map(root, asset_path, runtime->base_dir, assets, source_map,
                                                              NULL, error_buffer, error_buffer_size))
    {
        slayer3d_game_data_source_map_destroy(source_map);
        slayer3d_game_data_destroy(runtime);
        return false;
    }
    slayer3d_game_data_source_map_destroy(source_map);
    if (!slayer3d_game_data_network_schema_hash(root, runtime->network_schema_hash, &runtime->has_network_schema))
    {
        slayer3d_game_data_destroy(runtime);
        set_error(error_buffer, error_buffer_size, "failed to compute network schema hash");
        return false;
    }
    load_storage_config(runtime, root);

    yyjson_val *logic = obj_get(root, "logic");
    load_active_camera(runtime, root);
    bool ok = load_signals(runtime, root, error_buffer, error_buffer_size) &&
              load_entities(runtime, root, error_buffer, error_buffer_size) &&
              load_editor_player_starts(runtime, root, error_buffer, error_buffer_size) &&
              load_editor_prefabs(runtime, root, error_buffer, error_buffer_size) &&
              load_editor_actors(runtime, root, error_buffer, error_buffer_size) &&
              load_editor_connections(runtime, root, error_buffer, error_buffer_size) &&
              load_grid_maps(runtime, root, error_buffer, error_buffer_size) &&
              load_grid_pickup_layers(runtime, root, error_buffer, error_buffer_size) &&
              load_sector_levels(runtime, root, error_buffer, error_buffer_size) &&
              load_brush_worlds(runtime, root, error_buffer, error_buffer_size) &&
              load_sector_doors(runtime, root, error_buffer, error_buffer_size) &&
              load_sector_platforms(runtime, root, error_buffer, error_buffer_size) &&
              load_actor_pools(runtime, root, error_buffer, error_buffer_size) &&
              load_input(runtime, root, error_buffer, error_buffer_size) &&
              load_timers(runtime, logic, error_buffer, error_buffer_size) && load_sensors(runtime, logic) &&
              load_wave_schedules(runtime, logic) && load_scripts(runtime, root, error_buffer, error_buffer_size) &&
              load_lua_adapters(runtime, root, error_buffer, error_buffer_size) &&
              load_bindings(runtime, logic, error_buffer, error_buffer_size) &&
              load_scenes(runtime, root, options, error_buffer, error_buffer_size);
    if (!ok)
    {
        slayer3d_game_data_destroy(runtime);
        return false;
    }

    *out_runtime = runtime;
    return true;
}

bool slayer3d_game_data_load_file(const char *path, slayer3d_game_session *session,
                                  slayer3d_game_data_runtime **out_runtime, char *error_buffer, int error_buffer_size)
{
    if (out_runtime != NULL)
        *out_runtime = NULL;
    if (path == NULL || path[0] == '\0' || session == NULL || out_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid game data load arguments");
        return false;
    }

    char *base_dir = path_dirname(path);
    char *asset_name = path_basename(path);
    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    if (base_dir == NULL || asset_name == NULL || assets == NULL)
    {
        SDL_free(base_dir);
        SDL_free(asset_name);
        slayer3d_asset_resolver_destroy(assets);
        set_error(error_buffer, error_buffer_size, "failed to create game data asset resolver");
        return false;
    }

    char asset_error[256];
    const bool mounted =
        slayer3d_asset_resolver_mount_directory(assets, base_dir, asset_error, (int)sizeof(asset_error));
    if (!mounted)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to mount game data directory: %s",
                         asset_error);
        SDL_free(base_dir);
        SDL_free(asset_name);
        slayer3d_asset_resolver_destroy(assets);
        return false;
    }

    slayer3d_game_data_load_options options;
    SDL_zero(options);
    options.session = session;
    options.source_base_dir = base_dir;
    const bool ok = slayer3d_game_data_load_asset_with_options(assets, asset_name, &options, out_runtime, error_buffer,
                                                               error_buffer_size);
    if (ok && out_runtime != NULL && *out_runtime != NULL)
    {
        (*out_runtime)->owns_assets = true;
        char *file_base_dir = SDL_strdup(base_dir);
        if (file_base_dir != NULL)
        {
            SDL_free((*out_runtime)->file_base_dir);
            (*out_runtime)->file_base_dir = file_base_dir;
        }
    }
    else
        slayer3d_asset_resolver_destroy(assets);
    SDL_free(base_dir);
    SDL_free(asset_name);
    return ok;
}

static void storage_config_from_root(yyjson_val *root, slayer3d_storage_config *out_config)
{
    if (out_config == NULL)
        return;
    slayer3d_storage_config_init(out_config);

    yyjson_val *storage = obj_get(root, "storage");
    yyjson_val *metadata = obj_get(root, "metadata");
    yyjson_val *app = obj_get(root, "app");

    out_config->organization =
        first_non_empty_string(json_string(storage, "organization", NULL), json_string(metadata, "organization", NULL),
                               out_config->organization);
    out_config->application = first_non_empty_string(
        json_string(storage, "application", NULL), json_string(app, "title", NULL),
        first_non_empty_string(json_string(metadata, "name", NULL), NULL, out_config->application));
    out_config->profile = json_string(storage, "profile", NULL);
    out_config->user_root_override = json_string(storage, "user_root_override", NULL);
    out_config->cache_root_override = json_string(storage, "cache_root_override", NULL);
}

static void load_storage_config(slayer3d_game_data_runtime *runtime, yyjson_val *root)
{
    if (runtime == NULL)
        return;
    storage_config_from_root(root, &runtime->storage_config);
}

bool slayer3d_game_data_get_storage_config(const slayer3d_game_data_runtime *runtime,
                                           slayer3d_storage_config *out_config)
{
    if (runtime == NULL || out_config == NULL)
        return false;

    *out_config = runtime->storage_config;
    return true;
}

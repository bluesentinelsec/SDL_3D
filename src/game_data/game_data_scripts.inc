/* Lua script manifest and adapter loading helpers.
 * Included by src/game_data.c to keep private runtime helpers in one translation unit. */

static bool load_timers(slayer3d_game_data_runtime *runtime, yyjson_val *logic, char *error_buffer,
                        int error_buffer_size)
{
    yyjson_val *timers = obj_get(logic, "timers");
    if (!yyjson_is_arr(timers))
        return true;

    const int count = (int)yyjson_arr_size(timers);
    runtime->timers = (named_timer *)SDL_calloc((size_t)count, sizeof(*runtime->timers));
    if (runtime->timers == NULL && count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate timer table");
        return false;
    }
    runtime->timer_count = count;

    for (int i = 0; i < count; ++i)
    {
        yyjson_val *timer = yyjson_arr_get(timers, (size_t)i);
        runtime->timers[i].name = json_string(timer, "name", NULL);
        runtime->timers[i].delay = json_float(timer, "delay", 0.0f);
        runtime->timers[i].signal_id = slayer3d_game_data_find_signal(runtime, json_string(timer, "signal", NULL));
        runtime->timers[i].repeating = json_bool(timer, "repeating", false);
        runtime->timers[i].interval = json_float(timer, "interval", runtime->timers[i].delay);
    }
    return true;
}

static bool load_script_entry(slayer3d_game_data_runtime *runtime, script_entry *entry, char *error_buffer,
                              int error_buffer_size)
{
    if (entry == NULL)
        return false;
    if (entry->loaded)
        return true;
    if (entry->loading)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script dependency cycle at %s", entry->id);
        }
        return false;
    }

    entry->loading = true;
    for (int i = 0; i < entry->dependency_count; ++i)
    {
        script_entry *dependency = find_script(runtime, entry->dependencies[i]);
        if (dependency == NULL)
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s depends on missing script %s",
                             entry->id, entry->dependencies[i]);
            }
            entry->loading = false;
            return false;
        }
        if (!load_script_entry(runtime, dependency, error_buffer, error_buffer_size))
        {
            entry->loading = false;
            return false;
        }
    }

    char *resolved_path = path_join(runtime->base_dir, entry->path);
    if (resolved_path == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to resolve script path for %s", entry->id);
        }
        entry->loading = false;
        return false;
    }

    slayer3d_asset_buffer script_buffer;
    SDL_zero(script_buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(runtime->assets, resolved_path, &script_buffer, asset_error,
                                           (int)sizeof(asset_error)))
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s (%s) could not be read: %s", entry->id,
                         entry->path, asset_error);
        }
        SDL_free(resolved_path);
        entry->loading = false;
        return false;
    }

    char script_error[512];
    const bool ok = slayer3d_script_engine_load_module_buffer(runtime->scripts, script_buffer.data, script_buffer.size,
                                                              resolved_path, entry->module, &entry->module_ref,
                                                              script_error, (int)sizeof(script_error));
    slayer3d_asset_buffer_free(&script_buffer);
    SDL_free(resolved_path);
    if (!ok)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s (%s) failed: %s", entry->id,
                         entry->path, script_error);
        }
        entry->loading = false;
        return false;
    }

    entry->loaded = true;
    entry->loading = false;
    return true;
}

static bool load_script_index_into_engine(slayer3d_game_data_runtime *runtime, slayer3d_asset_resolver *assets,
                                          slayer3d_script_engine *engine, int index, slayer3d_script_ref *module_refs,
                                          bool *loading, bool *loaded, char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || assets == NULL || engine == NULL || index < 0 || index >= runtime->script_count ||
        module_refs == NULL || loading == NULL || loaded == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid Lua reload arguments");
        return false;
    }
    if (loaded[index])
        return true;
    if (loading[index])
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script dependency cycle at %s",
                         runtime->script_entries[index].id);
        }
        return false;
    }

    script_entry *entry = &runtime->script_entries[index];
    loading[index] = true;
    for (int dep = 0; dep < entry->dependency_count; ++dep)
    {
        script_entry *dependency = find_script(runtime, entry->dependencies[dep]);
        const int dependency_index = dependency != NULL ? (int)(dependency - runtime->script_entries) : -1;
        if (dependency_index < 0 ||
            !load_script_index_into_engine(runtime, assets, engine, dependency_index, module_refs, loading, loaded,
                                           error_buffer, error_buffer_size))
        {
            loading[index] = false;
            return false;
        }
    }

    char *resolved_path = path_join(runtime->base_dir, entry->path);
    if (resolved_path == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "failed to resolve script path for %s", entry->id);
        }
        loading[index] = false;
        return false;
    }

    slayer3d_asset_buffer script_buffer;
    SDL_zero(script_buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(assets, resolved_path, &script_buffer, asset_error,
                                           (int)sizeof(asset_error)))
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s (%s) could not be read: %s", entry->id,
                         entry->path, asset_error);
        }
        SDL_free(resolved_path);
        loading[index] = false;
        return false;
    }

    char script_error[512];
    const bool ok = slayer3d_script_engine_load_module_buffer(engine, script_buffer.data, script_buffer.size,
                                                              resolved_path, entry->module, &module_refs[index],
                                                              script_error, (int)sizeof(script_error));
    slayer3d_asset_buffer_free(&script_buffer);
    SDL_free(resolved_path);
    if (!ok)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s (%s) failed: %s", entry->id,
                         entry->path, script_error);
        }
        loading[index] = false;
        return false;
    }

    loaded[index] = true;
    loading[index] = false;
    return true;
}

static bool load_scripts(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                         int error_buffer_size)
{
    yyjson_val *scripts = obj_get(root, "scripts");
    if (!yyjson_is_arr(scripts) || yyjson_arr_size(scripts) == 0)
        return true;

    runtime->script_count = (int)yyjson_arr_size(scripts);
    runtime->script_entries =
        (script_entry *)SDL_calloc((size_t)runtime->script_count, sizeof(*runtime->script_entries));
    if (runtime->script_entries == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate script manifest");
        return false;
    }

    for (int i = 0; i < runtime->script_count; ++i)
    {
        yyjson_val *script = yyjson_arr_get(scripts, (size_t)i);
        script_entry *entry = &runtime->script_entries[i];
        entry->id = json_string(script, "id", NULL);
        entry->path = json_string(script, "path", NULL);
        entry->module = json_string(script, "module", NULL);
        entry->autoload = json_bool(script, "autoload", true);
        entry->module_ref = SLAYER3D_SCRIPT_REF_INVALID;
        if (entry->id == NULL || entry->id[0] == '\0' || entry->path == NULL || entry->path[0] == '\0' ||
            entry->module == NULL || entry->module[0] == '\0')
        {
            set_error(error_buffer, error_buffer_size, "script entry requires non-empty id, path, and module");
            return false;
        }

        for (int prior = 0; prior < i; ++prior)
        {
            if (SDL_strcmp(runtime->script_entries[prior].id, entry->id) == 0)
            {
                set_error(error_buffer, error_buffer_size, "duplicate script id");
                return false;
            }
            if (SDL_strcmp(runtime->script_entries[prior].module, entry->module) == 0)
            {
                set_error(error_buffer, error_buffer_size, "duplicate script module");
                return false;
            }
        }

        yyjson_val *dependencies = obj_get(script, "dependencies");
        if (yyjson_is_arr(dependencies) && yyjson_arr_size(dependencies) > 0)
        {
            entry->dependency_count = (int)yyjson_arr_size(dependencies);
            entry->dependencies =
                (const char **)SDL_calloc((size_t)entry->dependency_count, sizeof(*entry->dependencies));
            if (entry->dependencies == NULL)
            {
                set_error(error_buffer, error_buffer_size, "failed to allocate script dependencies");
                return false;
            }
            for (int dep = 0; dep < entry->dependency_count; ++dep)
            {
                yyjson_val *dependency = yyjson_arr_get(dependencies, (size_t)dep);
                if (!yyjson_is_str(dependency) || yyjson_get_str(dependency)[0] == '\0')
                {
                    set_error(error_buffer, error_buffer_size, "script dependencies must be non-empty strings");
                    return false;
                }
                entry->dependencies[dep] = yyjson_get_str(dependency);
            }
        }
    }

    for (int i = 0; i < runtime->script_count; ++i)
    {
        script_entry *entry = &runtime->script_entries[i];
        for (int dep = 0; dep < entry->dependency_count; ++dep)
        {
            if (find_script(runtime, entry->dependencies[dep]) == NULL)
            {
                if (error_buffer != NULL && error_buffer_size > 0)
                {
                    SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua script %s depends on missing script %s",
                                 entry->id, entry->dependencies[dep]);
                }
                return false;
            }
        }
    }

    runtime->scripts = slayer3d_script_engine_create();
    if (runtime->scripts == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to create Lua script engine");
        return false;
    }

    register_lua_api(runtime, runtime->scripts);
    for (int i = 0; i < runtime->script_count; ++i)
    {
        if (runtime->script_entries[i].autoload &&
            !load_script_entry(runtime, &runtime->script_entries[i], error_buffer, error_buffer_size))
        {
            return false;
        }
    }
    return true;
}

static bool load_lua_adapters(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                              int error_buffer_size)
{
    yyjson_val *adapters = obj_get(root, "adapters");
    if (!yyjson_is_arr(adapters))
        return true;

    for (size_t i = 0; i < yyjson_arr_size(adapters); ++i)
    {
        yyjson_val *adapter = yyjson_arr_get(adapters, i);
        const char *function_name = json_string(adapter, "function", NULL);
        if (function_name == NULL)
            continue;
        if (runtime->scripts == NULL)
        {
            set_error(error_buffer, error_buffer_size, "Lua adapter declared without any loaded scripts");
            return false;
        }

        const char *name = json_string(adapter, "name", NULL);
        const char *script_id = json_string(adapter, "script", NULL);
        script_entry *script = find_script(runtime, script_id);
        if (script == NULL)
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size, "Lua adapter %s references missing script %s",
                             name != NULL ? name : "<unnamed>", script_id != NULL ? script_id : "<null>");
            }
            return false;
        }
        if (!load_script_entry(runtime, script, error_buffer, error_buffer_size))
            return false;

        slayer3d_script_ref function_ref = SLAYER3D_SCRIPT_REF_INVALID;
        char script_error[256];
        if (!slayer3d_script_engine_ref_module_function(runtime->scripts, script->module_ref, function_name,
                                                        &function_ref, script_error, (int)sizeof(script_error)))
        {
            if (error_buffer != NULL && error_buffer_size > 0)
            {
                SDL_snprintf(error_buffer, (size_t)error_buffer_size,
                             "Lua adapter %s function %s in script %s failed: %s", name != NULL ? name : "<unnamed>",
                             function_name, script->id, script_error);
            }
            return false;
        }

        if (!set_adapter_lua_function(runtime, name, script->id, function_name, function_ref))
        {
            slayer3d_script_engine_unref(runtime->scripts, function_ref);
            set_error(error_buffer, error_buffer_size, "failed to register Lua adapter");
            return false;
        }
    }
    return true;
}

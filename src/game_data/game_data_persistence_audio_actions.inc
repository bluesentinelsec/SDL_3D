/* Persistence and audio action helpers. Included by game_data_actions.inc to preserve internal linkage. */

static bool ensure_audio_file_capacity(slayer3d_game_data_runtime *runtime, int required)
{
    if (runtime == NULL)
        return false;
    if (required <= runtime->audio_file_capacity)
        return true;

    int next_capacity = runtime->audio_file_capacity < 8 ? 8 : runtime->audio_file_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    materialized_audio_file *files =
        (materialized_audio_file *)SDL_realloc(runtime->audio_files, (size_t)next_capacity * sizeof(*files));
    if (files == NULL)
        return false;

    SDL_memset(files + runtime->audio_file_capacity, 0,
               (size_t)(next_capacity - runtime->audio_file_capacity) * sizeof(*files));
    runtime->audio_files = files;
    runtime->audio_file_capacity = next_capacity;
    return true;
}

static uint32_t hash_audio_asset_path(const char *path)
{
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)path; p != NULL && *p != '\0'; ++p)
    {
        hash ^= (uint32_t)*p;
        hash *= 16777619u;
    }
    return hash;
}

static char *make_safe_audio_filename(const char *path)
{
    char *base = path_basename(asset_path_without_scheme(path));
    if (base == NULL)
        return NULL;

    for (char *p = base; *p != '\0'; ++p)
    {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' ||
              *p == '_' || *p == '-'))
        {
            *p = '_';
        }
    }

    char hash[16];
    SDL_snprintf(hash, sizeof(hash), "%08x-", (unsigned int)hash_audio_asset_path(path));
    const size_t hash_len = SDL_strlen(hash);
    const size_t base_len = SDL_strlen(base);
    char *filename = (char *)SDL_malloc(hash_len + base_len + 1u);
    if (filename == NULL)
    {
        SDL_free(base);
        return NULL;
    }
    SDL_memcpy(filename, hash, hash_len);
    SDL_memcpy(filename + hash_len, base, base_len + 1u);
    SDL_free(base);
    return filename;
}

static bool ensure_runtime_storage(slayer3d_game_data_runtime *runtime, char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data runtime is required");
        return false;
    }
    if (runtime->storage != NULL)
        return true;

    return slayer3d_storage_create(&runtime->storage_config, &runtime->storage, error_buffer, error_buffer_size);
}

static yyjson_val *find_persistence_entry(const slayer3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *entries = obj_get(obj_get(runtime_root(runtime), "persistence"), "entries");
    for (size_t i = 0; name != NULL && yyjson_is_arr(entries) && i < yyjson_arr_size(entries); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(entries, i);
        const char *entry_name = json_string(entry, "name", NULL);
        if (entry_name != NULL && SDL_strcmp(entry_name, name) == 0)
            return entry;
    }
    return NULL;
}

static const char *action_persistence_entry_name(yyjson_val *action)
{
    const char *entry = json_string(action, "entry", NULL);
    return entry != NULL ? entry : json_string(action, "name", NULL);
}

static bool persistence_entry_is_enabled(slayer3d_game_data_runtime *runtime, yyjson_val *entry)
{
    yyjson_val *condition = obj_get(entry, "enabled_if");
    return condition == NULL || eval_data_condition(runtime, condition, NULL);
}

static const char *persistence_property_key(yyjson_val *property)
{
    if (yyjson_is_str(property))
        return yyjson_get_str(property);
    return json_string(property, "key", NULL);
}

static slayer3d_registered_actor *action_target_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload)
{
    const char *target_from_payload = json_string(action, "target_from_payload", NULL);
    if (target_from_payload != NULL)
        return actor_from_payload_key(runtime, payload, target_from_payload);
    return slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
}

static bool json_add_property_value(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key,
                                    const slayer3d_value *value)
{
    if (doc == NULL || object == NULL || key == NULL || value == NULL)
        return false;

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        return yyjson_mut_obj_add_int(doc, object, key, value->as_int);
    case SLAYER3D_VALUE_FLOAT:
        return yyjson_mut_obj_add_real(doc, object, key, value->as_float);
    case SLAYER3D_VALUE_BOOL:
        return yyjson_mut_obj_add_bool(doc, object, key, value->as_bool);
    case SLAYER3D_VALUE_STRING:
        return yyjson_mut_obj_add_strcpy(doc, object, key, value->as_string != NULL ? value->as_string : "");
    case SLAYER3D_VALUE_VEC3: {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        return arr != NULL && yyjson_mut_arr_add_real(doc, arr, value->as_vec3.x) &&
               yyjson_mut_arr_add_real(doc, arr, value->as_vec3.y) &&
               yyjson_mut_arr_add_real(doc, arr, value->as_vec3.z) && yyjson_mut_obj_add_val(doc, object, key, arr);
    }
    case SLAYER3D_VALUE_COLOR: {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        return arr != NULL && yyjson_mut_arr_add_int(doc, arr, value->as_color.r) &&
               yyjson_mut_arr_add_int(doc, arr, value->as_color.g) &&
               yyjson_mut_arr_add_int(doc, arr, value->as_color.b) &&
               yyjson_mut_arr_add_int(doc, arr, value->as_color.a) && yyjson_mut_obj_add_val(doc, object, key, arr);
    }
    }
    return false;
}

static bool execute_persistence_save(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *entry = find_persistence_entry(runtime, action_persistence_entry_name(action));
    const char *path = json_string(entry, "path", NULL);
    const char *target = json_string(entry, "target", NULL);
    yyjson_val *properties = obj_get(entry, "properties");
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    if (entry == NULL || path == NULL || actor == NULL || !yyjson_is_arr(properties))
        return false;
    if (!persistence_entry_is_enabled(runtime, entry))
        return true;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    if (root == NULL)
    {
        yyjson_mut_doc_free(doc);
        return false;
    }
    yyjson_mut_doc_set_root(doc, root);

    const char *schema = json_string(entry, "schema", NULL);
    if (schema != NULL && !yyjson_mut_obj_add_strcpy(doc, root, "schema", schema))
    {
        yyjson_mut_doc_free(doc);
        return false;
    }
    yyjson_val *version = obj_get(entry, "version");
    if (yyjson_is_int(version) && !yyjson_mut_obj_add_int(doc, root, "version", (int)yyjson_get_int(version)))
    {
        yyjson_mut_doc_free(doc);
        return false;
    }

    for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
    {
        const char *key = persistence_property_key(yyjson_arr_get(properties, i));
        if (key == NULL || key[0] == '\0')
            continue;
        const slayer3d_value *value = slayer3d_properties_get_value(actor->props, key);
        if (value != NULL && !json_add_property_value(doc, root, key, value))
        {
            yyjson_mut_doc_free(doc);
            return false;
        }
    }

    size_t size = 0u;
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size);
    yyjson_mut_doc_free(doc);
    if (json == NULL)
        return false;

    char storage_error[256];
    const bool ok =
        ensure_runtime_storage(runtime, storage_error, (int)sizeof(storage_error)) &&
        slayer3d_storage_write_file(runtime->storage, path, json, size, storage_error, (int)sizeof(storage_error));
    free(json);
    if (!ok)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D persistence save failed: %s", storage_error);
    return ok;
}

static bool execute_persistence_load(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *entry = find_persistence_entry(runtime, action_persistence_entry_name(action));
    const char *path = json_string(entry, "path", NULL);
    const char *target = json_string(entry, "target", NULL);
    yyjson_val *properties = obj_get(entry, "properties");
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    if (entry == NULL || path == NULL || actor == NULL || !yyjson_is_arr(properties))
        return false;
    if (!persistence_entry_is_enabled(runtime, entry))
        return true;

    char storage_error[256];
    if (!ensure_runtime_storage(runtime, storage_error, (int)sizeof(storage_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D persistence storage unavailable: %s", storage_error);
        return false;
    }

    slayer3d_storage_buffer buffer;
    SDL_zero(buffer);
    if (!slayer3d_storage_read_file(runtime->storage, path, &buffer, storage_error, (int)sizeof(storage_error)))
        return true;

    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, NULL);
    yyjson_val *root = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    const char *schema = json_string(entry, "schema", NULL);
    yyjson_val *version = obj_get(entry, "version");
    const bool schema_ok = schema == NULL || SDL_strcmp(json_string(root, "schema", ""), schema) == 0;
    const bool version_ok =
        !yyjson_is_int(version) || (yyjson_is_int(obj_get(root, "version")) &&
                                    yyjson_get_int(obj_get(root, "version")) == yyjson_get_int(version));
    if (yyjson_is_obj(root) && schema_ok && version_ok)
    {
        for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
        {
            const char *key = persistence_property_key(yyjson_arr_get(properties, i));
            yyjson_val *value = key != NULL ? obj_get(root, key) : NULL;
            if (value != NULL)
                set_actor_property_from_json(actor, key, value);
        }
    }
    else
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D persistence ignored incompatible file: %s", path);
    }

    yyjson_doc_free(doc);
    slayer3d_storage_buffer_free(&buffer);
    return true;
}

static bool execute_persistence_action(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *type)
{
    if (SDL_strcmp(type, "persistence.save") == 0)
        return execute_persistence_save(runtime, action);
    if (SDL_strcmp(type, "persistence.load") == 0)
        return execute_persistence_load(runtime, action);
    return false;
}

static bool ensure_audio_cache_storage(slayer3d_game_data_runtime *runtime)
{
    char storage_error[256];
    if (!ensure_runtime_storage(runtime, storage_error, (int)sizeof(storage_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio cache storage unavailable: %s", storage_error);
        return false;
    }

    if (!slayer3d_storage_create_directory(runtime->storage, "cache://audio", storage_error,
                                           (int)sizeof(storage_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio cache directory unavailable: %s", storage_error);
        return false;
    }

    char path[4096];
    if (slayer3d_storage_resolve_path(runtime->storage, "cache://audio", path, sizeof(path)))
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio cache directory: %s", path);
    return true;
}

static char *make_audio_cache_path(const slayer3d_game_data_runtime *runtime, const char *filename)
{
    char virtual_path[512];
    char resolved[4096];
    if (runtime == NULL || filename == NULL ||
        SDL_snprintf(virtual_path, sizeof(virtual_path), "cache://audio/%s", filename) >= (int)sizeof(virtual_path) ||
        !slayer3d_storage_resolve_path(runtime->storage, virtual_path, resolved, sizeof(resolved)))
    {
        return NULL;
    }
    return SDL_strdup(resolved);
}

static char *resolve_authored_audio_path(const slayer3d_game_data_runtime *runtime, const char *path)
{
    if (path == NULL || path[0] == '\0')
        return NULL;
    if (SDL_strncmp(path, "asset://", 8) == 0)
        return SDL_strdup(path);
    return path_join(runtime != NULL ? runtime->base_dir : NULL, path);
}

static char *materialize_audio_asset(slayer3d_game_data_runtime *runtime, const char *path)
{
    char *resolved = resolve_authored_audio_path(runtime, path);
    if (runtime == NULL || resolved == NULL)
        return resolved;

    for (int i = 0; i < runtime->audio_file_count; ++i)
    {
        if (SDL_strcmp(runtime->audio_files[i].asset_path, resolved) == 0)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio asset cache hit: %s -> %s", resolved,
                        runtime->audio_files[i].file_path);
            SDL_free(resolved);
            return SDL_strdup(runtime->audio_files[i].file_path);
        }
    }

    if (runtime->assets == NULL || !slayer3d_asset_resolver_exists(runtime->assets, resolved))
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio using filesystem path: %s", resolved);
        return resolved;
    }

    if (!ensure_audio_cache_storage(runtime))
    {
        SDL_free(resolved);
        return NULL;
    }

    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(runtime->assets, resolved, &buffer, asset_error, (int)sizeof(asset_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio asset read failed: %s", asset_error);
        SDL_free(resolved);
        return NULL;
    }

    char *filename = make_safe_audio_filename(resolved);
    char virtual_path[512];
    const bool has_virtual_path =
        filename != NULL &&
        SDL_snprintf(virtual_path, sizeof(virtual_path), "cache://audio/%s", filename) < (int)sizeof(virtual_path);
    char *file_path = has_virtual_path ? make_audio_cache_path(runtime, filename) : NULL;
    SDL_free(filename);
    if (file_path == NULL)
    {
        slayer3d_asset_buffer_free(&buffer);
        SDL_free(resolved);
        return NULL;
    }

    char storage_error[256];
    const bool wrote = slayer3d_storage_write_file(runtime->storage, virtual_path, buffer.data, buffer.size,
                                                   storage_error, (int)sizeof(storage_error));
    slayer3d_asset_buffer_free(&buffer);
    if (!wrote)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio asset materialization failed: %s: %s", file_path,
                    storage_error);
        SDL_free(file_path);
        SDL_free(resolved);
        return NULL;
    }

    if (!ensure_audio_file_capacity(runtime, runtime->audio_file_count + 1))
    {
        SDL_free(file_path);
        SDL_free(resolved);
        return NULL;
    }
    runtime->audio_files[runtime->audio_file_count].asset_path = SDL_strdup(resolved);
    runtime->audio_files[runtime->audio_file_count].file_path = SDL_strdup(file_path);
    if (runtime->audio_files[runtime->audio_file_count].asset_path == NULL ||
        runtime->audio_files[runtime->audio_file_count].file_path == NULL)
    {
        SDL_free(runtime->audio_files[runtime->audio_file_count].asset_path);
        SDL_free(runtime->audio_files[runtime->audio_file_count].file_path);
        SDL_free(file_path);
        SDL_free(resolved);
        return NULL;
    }
    ++runtime->audio_file_count;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio asset materialized: %s -> %s", resolved, file_path);
    SDL_free(resolved);
    return file_path;
}

bool slayer3d_game_data_prepare_audio_file(slayer3d_game_data_runtime *runtime, const char *path, char *out_path,
                                           int out_path_size)
{
    if (out_path != NULL && out_path_size > 0)
        out_path[0] = '\0';
    if (runtime == NULL || path == NULL || path[0] == '\0' || out_path == NULL || out_path_size <= 0)
        return false;

    char *file_path = materialize_audio_asset(runtime, path);
    if (file_path == NULL)
        return false;

    const size_t len = SDL_strlen(file_path);
    const bool ok = len < (size_t)out_path_size;
    if (ok)
        SDL_memcpy(out_path, file_path, len + 1u);
    SDL_free(file_path);
    return ok;
}

static bool execute_audio_play_sfx(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_audio_engine *audio = slayer3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "SLAYER3D audio.play_sfx ignored because audio engine is unavailable");
        return true;
    }

    slayer3d_game_data_sound_asset sound;
    const char *sound_id = json_string(action, "sound", json_string(action, "asset", NULL));
    const bool has_asset = slayer3d_game_data_get_sound_asset(runtime, sound_id, &sound);
    const char *path = json_string(action, "path", has_asset ? sound.path : NULL);
    if (path == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_sfx missing sound asset/path: %s",
                    sound_id != NULL ? sound_id : "<none>");
        return false;
    }

    slayer3d_audio_play_desc desc = slayer3d_audio_play_desc_default();
    if (has_asset)
    {
        desc.volume = sound.volume;
        desc.pitch = sound.pitch;
        desc.pan = sound.pan;
        desc.bus = sound.bus;
    }
    desc.volume = json_float(action, "volume", desc.volume);
    desc.pitch = json_float(action, "pitch", desc.pitch);
    desc.pan = json_float(action, "pan", desc.pan);
    desc.bus = parse_audio_bus(json_string(action, "bus", NULL), desc.bus);

    char file_path[4096];
    if (!slayer3d_game_data_prepare_audio_file(runtime, path, file_path, sizeof(file_path)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_sfx failed to resolve: %s", path);
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_sfx sound=%s path=%s resolved=%s volume=%.3f bus=%d",
                sound_id != NULL ? sound_id : "<path>", path, file_path, desc.volume, (int)desc.bus);
    const bool ok = slayer3d_audio_play_sound_file(audio, file_path, &desc);
    if (!ok)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_sfx failed: %s", SDL_GetError());
    return ok;
}

static bool execute_audio_play_music(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_audio_engine *audio = slayer3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "SLAYER3D audio.play_music ignored because audio engine is unavailable");
        return true;
    }

    slayer3d_game_data_music_asset music;
    const char *music_id = json_string(action, "music", json_string(action, "asset", NULL));
    const bool has_asset = slayer3d_game_data_get_music_asset(runtime, music_id, &music);
    const char *path = json_string(action, "path", has_asset ? music.path : NULL);
    if (path == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_music missing music asset/path: %s",
                    music_id != NULL ? music_id : "<none>");
        return false;
    }

    const float volume = json_float(action, "volume", has_asset ? music.volume : 1.0f);
    const bool loop = json_bool(action, "loop", has_asset ? music.loop : true);
    const float fade = json_float(action, "fade", json_float(action, "fade_seconds", 0.0f));

    char file_path[4096];
    if (!slayer3d_game_data_prepare_audio_file(runtime, path, file_path, sizeof(file_path)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_music failed to resolve: %s", path);
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SLAYER3D audio.play_music music=%s path=%s resolved=%s volume=%.3f loop=%d",
                music_id != NULL ? music_id : "<path>", path, file_path, volume, loop ? 1 : 0);
    const bool ok = slayer3d_audio_play_music(audio, file_path, loop, volume, fade);
    if (!ok)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_music failed: %s", SDL_GetError());
    return ok;
}

static bool execute_audio_set_ambient(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                      const slayer3d_properties *payload)
{
    slayer3d_audio_engine *audio = slayer3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "SLAYER3D audio.set_ambient ignored because audio engine is unavailable");
        return true;
    }

    int ambient_id = json_int(action, "ambient_id", 0);
    const char *ambient_id_from_payload = json_string(action, "ambient_id_from_payload", NULL);
    if (ambient_id_from_payload != NULL)
    {
        const slayer3d_value *value =
            payload != NULL ? slayer3d_properties_get_value(payload, ambient_id_from_payload) : NULL;
        if (value == NULL || value->type != SLAYER3D_VALUE_INT || value->as_int < 0)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SLAYER3D audio.set_ambient missing non-negative int payload value: %s",
                        ambient_id_from_payload);
            return false;
        }
        ambient_id = value->as_int;
    }

    yyjson_val *ambient_assets = obj_get(obj_get(runtime_root(runtime), "assets"), "ambient");
    const int authored_count = yyjson_is_arr(ambient_assets) ? (int)yyjson_arr_size(ambient_assets) : 0;
    const int zone_count = authored_count + 1;
    slayer3d_audio_ambient *zones = (slayer3d_audio_ambient *)SDL_calloc((size_t)zone_count, sizeof(*zones));
    char **resolved_paths = (char **)SDL_calloc((size_t)zone_count, sizeof(*resolved_paths));
    if (zones == NULL || resolved_paths == NULL)
    {
        SDL_free(zones);
        SDL_free(resolved_paths);
        return false;
    }

    zones[0].ambient_id = 0;
    zones[0].path = NULL;
    zones[0].volume = 0.0f;
    zones[0].loop = false;

    bool ok = true;
    for (int i = 0; i < authored_count; ++i)
    {
        yyjson_val *zone = yyjson_arr_get(ambient_assets, (size_t)i);
        const char *path = json_string(zone, "path", NULL);
        char file_path[4096];
        if (path == NULL || !slayer3d_game_data_prepare_audio_file(runtime, path, file_path, sizeof(file_path)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.set_ambient failed to resolve: %s",
                        path != NULL ? path : "<none>");
            ok = false;
            break;
        }

        resolved_paths[i + 1] = SDL_strdup(file_path);
        if (resolved_paths[i + 1] == NULL)
        {
            ok = false;
            break;
        }
        zones[i + 1].ambient_id = json_int(zone, "ambient_id", 0);
        zones[i + 1].path = resolved_paths[i + 1];
        zones[i + 1].volume = json_float(zone, "volume", 1.0f);
        zones[i + 1].loop = json_bool(zone, "loop", true);
    }

    if (ok)
    {
        const float fade = json_float(action, "fade", json_float(action, "fade_seconds", 0.0f));
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.set_ambient ambient_id=%d fade=%.3f", ambient_id,
                    fade);
        ok = slayer3d_audio_set_ambient(audio, zones, zone_count, ambient_id, fade);
        if (!ok)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.set_ambient failed: %s", SDL_GetError());
    }

    for (int i = 0; i < zone_count; ++i)
        SDL_free(resolved_paths[i]);
    SDL_free(resolved_paths);
    SDL_free(zones);
    return ok;
}

static float action_float_or_property(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *key,
                                      float fallback)
{
    yyjson_val *source = obj_get(action, "source");
    if (yyjson_is_obj(source))
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(source, "target", NULL));
        const char *property = json_string(source, "key", NULL);
        const slayer3d_value *value =
            actor != NULL && property != NULL ? slayer3d_properties_get_value(actor->props, property) : NULL;
        if (value != NULL && value->type == SLAYER3D_VALUE_FLOAT)
            return value->as_float * json_float(source, "scale", 1.0f);
        if (value != NULL && value->type == SLAYER3D_VALUE_INT)
            return (float)value->as_int * json_float(source, "scale", 1.0f);
    }
    return json_float(action, key, fallback);
}

static bool execute_audio_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                 const slayer3d_properties *payload, const char *type)
{
    slayer3d_audio_engine *audio = slayer3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (SDL_strcmp(type, "audio.play_sfx") == 0)
        return execute_audio_play_sfx(runtime, action);
    if (SDL_strcmp(type, "audio.play_music") == 0)
        return execute_audio_play_music(runtime, action);
    if (SDL_strcmp(type, "audio.set_ambient") == 0)
        return execute_audio_set_ambient(runtime, action, payload);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D %s ignored because audio engine is unavailable", type);
        return true;
    }
    if (SDL_strcmp(type, "audio.stop_sfx") == 0)
    {
        slayer3d_audio_stop_bus(audio,
                                parse_audio_bus(json_string(action, "bus", NULL), SLAYER3D_AUDIO_BUS_SOUND_EFFECTS));
        return true;
    }
    if (SDL_strcmp(type, "audio.stop_music") == 0)
    {
        slayer3d_audio_stop_music(audio, json_float(action, "fade", json_float(action, "fade_seconds", 0.0f)));
        return true;
    }
    if (SDL_strcmp(type, "audio.fade_music") == 0)
    {
        slayer3d_audio_fade_music(audio, json_float(action, "volume", 1.0f),
                                  json_float(action, "duration", json_float(action, "fade_seconds", 0.0f)));
        return true;
    }
    if (SDL_strcmp(type, "audio.set_bus_volume") == 0)
    {
        slayer3d_audio_set_bus_volume(audio,
                                      parse_audio_bus(json_string(action, "bus", NULL), SLAYER3D_AUDIO_BUS_COUNT),
                                      action_float_or_property(runtime, action, "volume", 1.0f));
        return true;
    }
    return false;
}

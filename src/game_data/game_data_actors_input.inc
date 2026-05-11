/* Actor, pool, and input-profile runtime helpers.
 * Included by src/game_data.c to keep private runtime helpers in one translation unit. */

static void set_actor_property_from_json(slayer3d_registered_actor *actor, const char *key, yyjson_val *value)
{
    if (actor == NULL || key == NULL || value == NULL)
        return;

    if (yyjson_is_bool(value))
        slayer3d_properties_set_bool(actor->props, key, yyjson_get_bool(value));
    else if (yyjson_is_int(value))
        slayer3d_properties_set_int(actor->props, key, (int)yyjson_get_int(value));
    else if (yyjson_is_num(value))
        slayer3d_properties_set_float(actor->props, key, (float)yyjson_get_real(value));
    else if (yyjson_is_str(value))
        slayer3d_properties_set_string(actor->props, key, yyjson_get_str(value));
    else if (yyjson_is_arr(value))
        slayer3d_properties_set_vec3(actor->props, key, json_vec3_value(value, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
}

static bool format_payload_string(const slayer3d_properties *payload, const char *format, char *buffer,
                                  size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    buffer[0] = '\0';
    if (format == NULL)
        return true;
    if (payload == NULL || SDL_strchr(format, '{') == NULL)
    {
        SDL_strlcpy(buffer, format, buffer_size);
        return SDL_strlen(format) < buffer_size;
    }

    size_t offset = 0U;
    const char *cursor = format;
    while (*cursor != '\0' && offset + 1U < buffer_size)
    {
        const char *open = SDL_strchr(cursor, '{');
        if (open == NULL)
        {
            const size_t remaining = buffer_size - offset;
            const size_t copied = SDL_strlcpy(buffer + offset, cursor, remaining);
            offset += SDL_min(copied, remaining > 0U ? remaining - 1U : 0U);
            if (copied < remaining)
                cursor += copied;
            break;
        }

        const size_t literal_len = (size_t)(open - cursor);
        const size_t literal_copy = SDL_min(literal_len, buffer_size - offset - 1U);
        SDL_memcpy(buffer + offset, cursor, literal_copy);
        offset += literal_copy;
        buffer[offset] = '\0';
        if (literal_copy < literal_len)
            break;

        const char *close = SDL_strchr(open + 1, '}');
        if (close == NULL)
        {
            const size_t remaining = buffer_size - offset;
            const size_t copied = SDL_strlcpy(buffer + offset, open, remaining);
            offset += SDL_min(copied, remaining > 0U ? remaining - 1U : 0U);
            if (copied < remaining)
                cursor = open + copied;
            break;
        }

        char key[64];
        const size_t key_len = (size_t)(close - open - 1);
        if (key_len > 0U && key_len < sizeof(key))
        {
            SDL_memcpy(key, open + 1, key_len);
            key[key_len] = '\0';
            const char *replacement = slayer3d_properties_get_string(payload, key, "");
            const size_t remaining = buffer_size - offset;
            const size_t copied = SDL_strlcpy(buffer + offset, replacement, remaining);
            offset += SDL_min(copied, remaining > 0U ? remaining - 1U : 0U);
        }
        cursor = close + 1;
    }
    buffer[buffer_size - 1U] = '\0';
    return cursor[0] == '\0';
}

static bool set_property_from_json_with_payload(slayer3d_properties *props, const char *key, yyjson_val *value,
                                                const slayer3d_properties *payload)
{
    if (props == NULL || key == NULL || value == NULL)
        return false;
    if (yyjson_is_str(value))
    {
        char formatted[256];
        if (!format_payload_string(payload, yyjson_get_str(value), formatted, sizeof(formatted)))
            return false;
        slayer3d_properties_set_string(props, key, formatted);
        return true;
    }
    return set_property_from_json(props, key, value);
}

static slayer3d_properties *properties_from_json_payload(yyjson_val *json, const slayer3d_properties *source_payload)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    if (json == NULL)
        return payload;
    if (!yyjson_is_obj(json))
    {
        slayer3d_properties_destroy(payload);
        return NULL;
    }

    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(json, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        value = yyjson_obj_iter_get_val(key);
        if (name == NULL || name[0] == '\0' ||
            !set_property_from_json_with_payload(payload, name, value, source_payload))
        {
            slayer3d_properties_destroy(payload);
            return NULL;
        }
    }
    return payload;
}

static void load_actor_properties(slayer3d_registered_actor *actor, yyjson_val *properties)
{
    if (actor == NULL || !yyjson_is_obj(properties))
        return;

    yyjson_val *key;
    yyjson_val *entry;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        entry = yyjson_obj_iter_get_val(key);
        const char *name = yyjson_get_str(key);
        const char *type = json_string(entry, "type", NULL);
        yyjson_val *value = obj_get(entry, "value");
        if (name == NULL || value == NULL)
            continue;

        if (type != NULL && (SDL_strcmp(type, "vec2") == 0 || SDL_strcmp(type, "vec3") == 0))
            slayer3d_properties_set_vec3(actor->props, name,
                                         json_vec3_value(value, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
        else
            set_actor_property_from_json(actor, name, value);
    }
}

static void initialize_actor_from_json(slayer3d_registered_actor *actor, yyjson_val *entity, const char *classname,
                                       bool active)
{
    if (actor == NULL)
        return;

    actor->active = active;
    slayer3d_properties_clear(actor->props);
    slayer3d_properties_set_string(actor->props, "classname", classname != NULL ? classname : actor->name);

    yyjson_val *transform = obj_get(entity, "transform");
    actor_set_position(actor, json_vec3(transform, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
    load_actor_properties(actor, obj_get(entity, "properties"));
}

static bool load_entities(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                          int error_buffer_size)
{
    slayer3d_actor_registry *registry = runtime_registry(runtime);
    yyjson_val *entities = obj_get(root, "entities");
    if (!yyjson_is_arr(entities))
        return true;
    if (registry == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data entities require an actor registry");
        return false;
    }

    const size_t count = yyjson_arr_size(entities);
    for (size_t i = 0; i < count; ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *name = json_string(entity, "name", NULL);
        if (name == NULL || name[0] == '\0')
        {
            set_error(error_buffer, error_buffer_size, "entity is missing a non-empty name");
            return false;
        }

        slayer3d_registered_actor *actor = slayer3d_actor_registry_add(registry, name);
        if (actor == NULL)
        {
            set_error(error_buffer, error_buffer_size, "failed to register JSON actor");
            return false;
        }
        initialize_actor_from_json(actor, entity, name, json_bool(entity, "active", true));
    }

    return true;
}

static yyjson_val *find_actor_archetype_json(yyjson_val *root, const char *name)
{
    yyjson_val *archetypes = obj_get(root, "actor_archetypes");
    for (size_t i = 0; yyjson_is_arr(archetypes) && i < yyjson_arr_size(archetypes); ++i)
    {
        yyjson_val *archetype = yyjson_arr_get(archetypes, i);
        const char *archetype_name = json_string(archetype, "name", NULL);
        if (archetype_name != NULL && name != NULL && SDL_strcmp(archetype_name, name) == 0)
            return archetype;
    }
    return NULL;
}

static actor_pool_exhaustion_policy actor_pool_exhaustion_from_string(const char *value)
{
    if (value != NULL && SDL_strcmp(value, "reuse_oldest") == 0)
        return ACTOR_POOL_EXHAUST_REUSE_OLDEST;
    return ACTOR_POOL_EXHAUST_FAIL;
}

static const char *actor_lifecycle_state_name(actor_lifecycle_state state)
{
    switch (state)
    {
    case ACTOR_LIFECYCLE_SPAWNING:
        return "spawning";
    case ACTOR_LIFECYCLE_ACTIVE:
        return "active";
    case ACTOR_LIFECYCLE_DESPAWNING:
        return "despawning";
    case ACTOR_LIFECYCLE_INACTIVE:
    default:
        return "inactive";
    }
}

static actor_lifecycle_state actor_pool_lifecycle_state(const actor_pool_runtime *pool, int index)
{
    if (pool == NULL || index < 0 || index >= pool->capacity || pool->lifecycle_states == NULL)
        return ACTOR_LIFECYCLE_INACTIVE;
    return pool->lifecycle_states[index];
}

static actor_pool_scene_exit_policy actor_pool_scene_exit_policy_from_string(const char *policy,
                                                                             actor_pool_scene_exit_policy fallback)
{
    if (policy == NULL)
        return fallback;
    if (SDL_strcmp(policy, "reset") == 0)
        return ACTOR_POOL_SCENE_EXIT_RESET;
    if (SDL_strcmp(policy, "despawn") == 0)
        return ACTOR_POOL_SCENE_EXIT_DESPAWN;
    if (SDL_strcmp(policy, "preserve") == 0)
        return ACTOR_POOL_SCENE_EXIT_PRESERVE;
    return fallback;
}

static bool actor_pool_in_scene(const actor_pool_runtime *pool, const char *scene_name)
{
    if (pool == NULL || scene_name == NULL)
        return false;
    for (int i = 0; i < pool->scene_count; ++i)
    {
        if (pool->scenes[i] != NULL && SDL_strcmp(pool->scenes[i], scene_name) == 0)
            return true;
    }
    return false;
}

static void actor_pool_set_lifecycle_state(actor_pool_runtime *pool, slayer3d_registered_actor *actor, int index,
                                           actor_lifecycle_state state)
{
    if (pool == NULL || actor == NULL || index < 0 || index >= pool->capacity)
        return;
    if (pool->lifecycle_states != NULL)
        pool->lifecycle_states[index] = state;
    slayer3d_properties_set_string(actor->props, "pool_lifecycle", actor_lifecycle_state_name(state));
}

static bool actor_pool_actor_is_active(const actor_pool_runtime *pool, const slayer3d_registered_actor *actor,
                                       int index)
{
    return actor != NULL && actor->active &&
           (pool == NULL || pool->lifecycle_states == NULL ||
            actor_pool_lifecycle_state(pool, index) == ACTOR_LIFECYCLE_ACTIVE);
}

static bool actor_pool_actor_is_available(const actor_pool_runtime *pool, const slayer3d_registered_actor *actor,
                                          int index)
{
    return actor != NULL && !actor->active &&
           (pool == NULL || pool->lifecycle_states == NULL ||
            actor_pool_lifecycle_state(pool, index) == ACTOR_LIFECYCLE_INACTIVE);
}

static void actor_pool_copy_reason(char *target, size_t target_size, const char *reason)
{
    if (target == NULL || target_size == 0U)
        return;
    SDL_strlcpy(target, reason != NULL && reason[0] != '\0' ? reason : "none", target_size);
}

static int actor_pool_active_count(const slayer3d_game_data_runtime *runtime, const actor_pool_runtime *pool)
{
    int count = 0;
    for (int i = 0; runtime != NULL && pool != NULL && i < pool->capacity; ++i)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[i]);
        if (actor_pool_actor_is_active(pool, actor, i))
            ++count;
    }
    return count;
}

static int actor_pool_available_count(const slayer3d_game_data_runtime *runtime, const actor_pool_runtime *pool)
{
    int count = 0;
    for (int i = 0; runtime != NULL && pool != NULL && i < pool->capacity; ++i)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[i]);
        if (actor_pool_actor_is_available(pool, actor, i))
            ++count;
    }
    return count;
}

static void actor_pool_update_peak_active_count(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool)
{
    if (runtime == NULL || pool == NULL)
        return;
    const int active_count = actor_pool_active_count(runtime, pool);
    if (active_count > pool->peak_active_count)
        pool->peak_active_count = active_count;
}

static void actor_pool_note_spawn_attempt(actor_pool_runtime *pool)
{
    if (pool != NULL)
        pool->spawn_attempt_count++;
}

static void actor_pool_note_spawn_success(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool)
{
    if (pool == NULL)
        return;
    pool->spawn_success_count++;
    actor_pool_copy_reason(pool->last_spawn_failure_reason, sizeof(pool->last_spawn_failure_reason), "none");
    actor_pool_update_peak_active_count(runtime, pool);
}

static void actor_pool_note_spawn_failure(actor_pool_runtime *pool, const char *reason)
{
    if (pool == NULL)
        return;
    pool->spawn_failure_count++;
    actor_pool_copy_reason(pool->last_spawn_failure_reason, sizeof(pool->last_spawn_failure_reason), reason);
}

static void actor_lifecycle_defer_begin(slayer3d_game_data_runtime *runtime)
{
    if (runtime != NULL)
        runtime->actor_lifecycle_defer_depth++;
}

static void actor_lifecycle_flush(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->actor_lifecycle_defer_depth > 0 || !runtime->actor_lifecycle_flush_pending)
        return;

    runtime->actor_lifecycle_flush_pending = false;
    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            if (actor_pool_lifecycle_state(pool, actor_index) != ACTOR_LIFECYCLE_DESPAWNING)
                continue;
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor != NULL)
            {
                if (initialize_pooled_actor(pool, actor, actor_index, false))
                    slayer3d_properties_set_string(actor->props, "pool_last_despawn_reason", pool->last_despawn_reason);
            }
        }
    }
}

static void actor_lifecycle_defer_end(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->actor_lifecycle_defer_depth <= 0)
        return;
    runtime->actor_lifecycle_defer_depth--;
    actor_lifecycle_flush(runtime);
}

static bool actor_pool_initialize_slot(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool, int index,
                                       bool active)
{
    if (runtime == NULL || pool == NULL || index < 0 || index >= pool->capacity)
        return false;
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[index]);
    if (actor == NULL || !initialize_pooled_actor(pool, actor, index, active))
        return false;
    if (active && pool->spawn_generations != NULL)
    {
        pool->spawn_generations[index] = ++pool->spawn_generation_counter;
        slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                    (int)SDL_min(pool->spawn_generations[index], (Uint64)SDL_MAX_SINT32));
    }
    if (active)
        actor_pool_update_peak_active_count(runtime, pool);
    return true;
}

static bool initialize_pooled_actor(actor_pool_runtime *pool, slayer3d_registered_actor *actor, int index, bool active)
{
    if (pool == NULL || actor == NULL || index < 0 || index >= pool->capacity)
        return false;
    initialize_actor_from_json(actor, pool->archetype_json, pool->archetype, active);
    slayer3d_properties_set_string(actor->props, "pool", pool->name);
    slayer3d_properties_set_int(actor->props, "pool_index", index);
    if (pool->scene != NULL)
        slayer3d_properties_set_string(actor->props, "pool_scene", pool->scene);
    if (!active && pool->spawn_generations != NULL)
        pool->spawn_generations[index] = 0U;
    actor_pool_set_lifecycle_state(pool, actor, index, active ? ACTOR_LIFECYCLE_ACTIVE : ACTOR_LIFECYCLE_INACTIVE);
    return true;
}

static bool apply_actor_pool_scene_exit_policies(slayer3d_game_data_runtime *runtime, const char *from_scene,
                                                 const char *to_scene)
{
    if (runtime == NULL || from_scene == NULL)
        return true;

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, from_scene) || actor_pool_in_scene(pool, to_scene))
            continue;
        if (pool->scene_exit_policy == ACTOR_POOL_SCENE_EXIT_PRESERVE)
            continue;

        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor == NULL)
                return false;
            if (pool->scene_exit_policy == ACTOR_POOL_SCENE_EXIT_DESPAWN)
            {
                if (actor_pool_lifecycle_state(pool, actor_index) != ACTOR_LIFECYCLE_INACTIVE &&
                    !actor_pool_request_despawn(runtime, pool, actor, actor_index, "scene_exit"))
                {
                    return false;
                }
                continue;
            }
            if (!actor_pool_initialize_slot(runtime, pool, actor_index, pool->initial_active))
                return false;
        }
    }
    return true;
}

static bool load_actor_pools(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                             int error_buffer_size)
{
    slayer3d_actor_registry *registry = runtime_registry(runtime);
    yyjson_val *pools = obj_get(root, "actor_pools");
    if (!yyjson_is_arr(pools))
        return true;
    if (registry == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data actor pools require an actor registry");
        return false;
    }

    runtime->actor_pool_count = (int)yyjson_arr_size(pools);
    runtime->actor_pools =
        (actor_pool_runtime *)SDL_calloc((size_t)runtime->actor_pool_count, sizeof(*runtime->actor_pools));
    if (runtime->actor_pools == NULL && runtime->actor_pool_count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate actor pools");
        return false;
    }

    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        yyjson_val *pool_json = yyjson_arr_get(pools, (size_t)i);
        actor_pool_runtime *pool = &runtime->actor_pools[i];
        const char *name = json_string(pool_json, "name", NULL);
        const char *archetype = json_string(pool_json, "archetype", NULL);
        const int capacity = json_int(pool_json, "capacity", 0);
        yyjson_val *archetype_json = find_actor_archetype_json(root, archetype);
        if (name == NULL || archetype == NULL || capacity <= 0 || archetype_json == NULL)
        {
            set_error(error_buffer, error_buffer_size, "invalid actor pool declaration");
            return false;
        }

        pool->name = SDL_strdup(name);
        pool->archetype = archetype;
        yyjson_val *scene_list = obj_get(pool_json, "scenes");
        const char *single_scene = json_string(pool_json, "scene", NULL);
        if (yyjson_is_arr(scene_list))
        {
            pool->scene_count = (int)yyjson_arr_size(scene_list);
            pool->scenes = (const char **)SDL_calloc((size_t)pool->scene_count, sizeof(*pool->scenes));
            if (pool->scenes == NULL && pool->scene_count > 0)
            {
                set_error(error_buffer, error_buffer_size, "failed to allocate actor pool scenes");
                return false;
            }
            for (int scene_index = 0; scene_index < pool->scene_count; ++scene_index)
            {
                yyjson_val *scene_value = yyjson_arr_get(scene_list, (size_t)scene_index);
                if (!yyjson_is_str(scene_value) || yyjson_get_str(scene_value)[0] == '\0')
                {
                    set_error(error_buffer, error_buffer_size, "invalid actor pool scene declaration");
                    return false;
                }
                pool->scenes[scene_index] = yyjson_get_str(scene_value);
            }
        }
        else if (single_scene != NULL)
        {
            pool->scene_count = 1;
            pool->scenes = (const char **)SDL_calloc(1U, sizeof(*pool->scenes));
            if (pool->scenes == NULL)
            {
                set_error(error_buffer, error_buffer_size, "failed to allocate actor pool scenes");
                return false;
            }
            pool->scenes[0] = single_scene;
        }
        pool->scene = pool->scene_count > 0 ? pool->scenes[0] : NULL;
        pool->archetype_json = archetype_json;
        pool->capacity = capacity;
        pool->initial_active = json_bool(pool_json, "initial_active", false);
        pool->exhaustion = actor_pool_exhaustion_from_string(json_string(pool_json, "on_exhausted", "fail"));
        pool->scene_exit_policy = actor_pool_scene_exit_policy_from_string(
            json_string(pool_json, "on_scene_exit", NULL),
            pool->scene_count > 0 ? ACTOR_POOL_SCENE_EXIT_RESET : ACTOR_POOL_SCENE_EXIT_PRESERVE);
        actor_pool_copy_reason(pool->last_spawn_failure_reason, sizeof(pool->last_spawn_failure_reason), "none");
        actor_pool_copy_reason(pool->last_despawn_reason, sizeof(pool->last_despawn_reason), "none");
        pool->actor_names = (char **)SDL_calloc((size_t)capacity, sizeof(*pool->actor_names));
        pool->spawn_generations = (Uint64 *)SDL_calloc((size_t)capacity, sizeof(*pool->spawn_generations));
        pool->lifecycle_states = (actor_lifecycle_state *)SDL_calloc((size_t)capacity, sizeof(*pool->lifecycle_states));
        if (pool->name == NULL || pool->actor_names == NULL || pool->spawn_generations == NULL ||
            pool->lifecycle_states == NULL)
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate actor pool entries");
            return false;
        }

        for (int actor_index = 0; actor_index < capacity; ++actor_index)
        {
            char actor_name[256];
            SDL_snprintf(actor_name, sizeof(actor_name), "%s.%d", name, actor_index);
            slayer3d_registered_actor *actor = slayer3d_actor_registry_add(registry, actor_name);
            if (actor == NULL)
            {
                set_error(error_buffer, error_buffer_size, "failed to register pooled actor");
                return false;
            }
            pool->actor_names[actor_index] = SDL_strdup(actor_name);
            if (pool->actor_names[actor_index] == NULL ||
                !initialize_pooled_actor(pool, actor, actor_index, pool->initial_active))
            {
                set_error(error_buffer, error_buffer_size, "failed to initialize pooled actor");
                return false;
            }
            if (pool->initial_active)
            {
                pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
                slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                            (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
                actor_pool_update_peak_active_count(runtime, pool);
            }
        }
    }

    return true;
}

static bool load_signals(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                         int error_buffer_size)
{
    yyjson_val *signals = obj_get(root, "signals");
    if (!yyjson_is_arr(signals))
        return true;

    const int count = (int)yyjson_arr_size(signals);
    runtime->signals = (named_signal *)SDL_calloc((size_t)count, sizeof(*runtime->signals));
    if (runtime->signals == NULL && count > 0)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate signal table");
        return false;
    }
    runtime->signal_count = count;

    for (int i = 0; i < count; ++i)
    {
        yyjson_val *signal = yyjson_arr_get(signals, (size_t)i);
        if (!yyjson_is_str(signal))
        {
            set_error(error_buffer, error_buffer_size, "signal entries must be strings");
            return false;
        }
        runtime->signals[i].name = yyjson_get_str(signal);
        runtime->signals[i].id = SLAYER3D_GAME_DATA_SIGNAL_BASE + i;
    }
    return true;
}

static bool append_input_binding_spec(slayer3d_game_data_runtime *runtime, const input_binding_spec *spec)
{
    if (runtime == NULL || spec == NULL)
        return false;
    if (runtime->input_binding_count >= runtime->input_binding_capacity)
    {
        const int next_capacity = runtime->input_binding_capacity > 0 ? runtime->input_binding_capacity * 2 : 32;
        input_binding_spec *next = (input_binding_spec *)SDL_realloc(
            runtime->input_bindings, (size_t)next_capacity * sizeof(*runtime->input_bindings));
        if (next == NULL)
            return false;
        SDL_memset(next + runtime->input_binding_capacity, 0,
                   (size_t)(next_capacity - runtime->input_binding_capacity) * sizeof(*runtime->input_bindings));
        runtime->input_bindings = next;
        runtime->input_binding_capacity = next_capacity;
    }
    runtime->input_bindings[runtime->input_binding_count++] = *spec;
    return true;
}

static void bind_input_spec(slayer3d_input_manager *input, const input_binding_spec *spec)
{
    if (input == NULL || spec == NULL || spec->action_id < 0)
        return;

    switch (spec->source)
    {
    case SLAYER3D_INPUT_KEYBOARD:
        slayer3d_input_bind_key_mod_mask(input, spec->action_id, spec->scancode, spec->required_modifiers,
                                         spec->excluded_modifiers);
        break;
    case SLAYER3D_INPUT_MOUSE_BUTTON:
        slayer3d_input_bind_mouse_button(input, spec->action_id, spec->mouse_button);
        break;
    case SLAYER3D_INPUT_MOUSE_AXIS:
        slayer3d_input_bind_mouse_axis(input, spec->action_id, spec->mouse_axis, spec->scale);
        break;
    case SLAYER3D_INPUT_GAMEPAD_BUTTON:
        slayer3d_input_bind_gamepad_button_at(input, spec->action_id, spec->gamepad_index, spec->gamepad_button);
        break;
    case SLAYER3D_INPUT_GAMEPAD_AXIS:
        slayer3d_input_bind_gamepad_axis_at(input, spec->action_id, spec->gamepad_index, spec->gamepad_axis,
                                            spec->scale);
        break;
    }
}

static void rebind_action_from_specs(slayer3d_game_data_runtime *runtime, int action_id)
{
    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL || action_id < 0)
        return;

    slayer3d_input_unbind_action(input, action_id);
    for (int i = 0; i < runtime->input_binding_count; ++i)
    {
        if (runtime->input_bindings[i].action_id == action_id)
            bind_input_spec(input, &runtime->input_bindings[i]);
    }
}

static bool set_action_keyboard_binding(slayer3d_game_data_runtime *runtime, const char *action, SDL_Scancode scancode)
{
    const int action_id = slayer3d_game_data_find_action(runtime, action);
    if (runtime == NULL || action == NULL || action_id < 0 || scancode == SDL_SCANCODE_UNKNOWN)
        return false;

    for (int i = 0; i < runtime->input_binding_count;)
    {
        input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SLAYER3D_INPUT_KEYBOARD)
        {
            *spec = runtime->input_bindings[runtime->input_binding_count - 1];
            runtime->input_binding_count--;
        }
        else
        {
            i++;
        }
    }

    input_binding_spec spec;
    SDL_zero(spec);
    spec.action = action;
    spec.action_id = action_id;
    spec.source = SLAYER3D_INPUT_KEYBOARD;
    spec.gamepad_index = -1;
    spec.scancode = scancode;
    spec.scale = 1.0f;
    if (!append_input_binding_spec(runtime, &spec))
        return false;
    rebind_action_from_specs(runtime, action_id);
    return true;
}

static bool set_action_mouse_button_binding(slayer3d_game_data_runtime *runtime, const char *action, Uint8 button)
{
    const int action_id = slayer3d_game_data_find_action(runtime, action);
    if (runtime == NULL || action == NULL || action_id < 0 || button == 0)
        return false;

    for (int i = 0; i < runtime->input_binding_count;)
    {
        input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SLAYER3D_INPUT_MOUSE_BUTTON)
        {
            *spec = runtime->input_bindings[runtime->input_binding_count - 1];
            runtime->input_binding_count--;
        }
        else
        {
            i++;
        }
    }

    input_binding_spec spec;
    SDL_zero(spec);
    spec.action = action;
    spec.action_id = action_id;
    spec.source = SLAYER3D_INPUT_MOUSE_BUTTON;
    spec.gamepad_index = -1;
    spec.mouse_button = button;
    spec.scale = 1.0f;
    if (!append_input_binding_spec(runtime, &spec))
        return false;
    rebind_action_from_specs(runtime, action_id);
    return true;
}

static bool set_action_gamepad_button_binding(slayer3d_game_data_runtime *runtime, const char *action,
                                              SDL_GamepadButton button)
{
    const int action_id = slayer3d_game_data_find_action(runtime, action);
    if (runtime == NULL || action == NULL || action_id < 0 || button == SDL_GAMEPAD_BUTTON_INVALID)
        return false;

    for (int i = 0; i < runtime->input_binding_count;)
    {
        input_binding_spec *spec = &runtime->input_bindings[i];
        if (spec->action_id == action_id && spec->source == SLAYER3D_INPUT_GAMEPAD_BUTTON)
        {
            *spec = runtime->input_bindings[runtime->input_binding_count - 1];
            runtime->input_binding_count--;
        }
        else
        {
            i++;
        }
    }

    input_binding_spec spec;
    SDL_zero(spec);
    spec.action = action;
    spec.action_id = action_id;
    spec.source = SLAYER3D_INPUT_GAMEPAD_BUTTON;
    spec.gamepad_index = -1;
    spec.gamepad_button = button;
    spec.scale = 1.0f;
    if (!append_input_binding_spec(runtime, &spec))
        return false;
    rebind_action_from_specs(runtime, action_id);
    return true;
}

static bool load_input(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer, int error_buffer_size)
{
    slayer3d_input_manager *input = runtime_input(runtime);
    yyjson_val *input_root = obj_get(root, "input");
    yyjson_val *contexts = obj_get(input_root, "contexts");
    if (!yyjson_is_arr(contexts))
        return true;
    if (input == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data input requires an input manager");
        return false;
    }

    for (size_t c = 0; c < yyjson_arr_size(contexts); ++c)
    {
        yyjson_val *context = yyjson_arr_get(contexts, c);
        yyjson_val *actions = obj_get(context, "actions");
        if (!yyjson_is_arr(actions))
            continue;

        for (size_t a = 0; a < yyjson_arr_size(actions); ++a)
        {
            yyjson_val *action = yyjson_arr_get(actions, a);
            const char *name = json_string(action, "name", NULL);
            if (name == NULL)
                continue;

            const int action_id = slayer3d_input_register_action(input, name);
            named_action *entries =
                (named_action *)SDL_realloc(runtime->actions, (size_t)(runtime->action_count + 1) * sizeof(*entries));
            if (entries == NULL)
                return false;
            runtime->actions = entries;
            runtime->actions[runtime->action_count++] = (named_action){name, action_id};

            yyjson_val *bindings = obj_get(action, "bindings");
            for (size_t b = 0; yyjson_is_arr(bindings) && b < yyjson_arr_size(bindings); ++b)
            {
                yyjson_val *binding = yyjson_arr_get(bindings, b);
                const char *device = json_string(binding, "device", "");
                const float scale = json_float(binding, "scale", 1.0f);
                if (SDL_strcmp(device, "keyboard") == 0)
                {
                    SDL_Scancode code = scancode_from_json(json_string(binding, "key", NULL));
                    if (code != SDL_SCANCODE_UNKNOWN)
                    {
                        input_binding_spec spec;
                        SDL_zero(spec);
                        spec.action = name;
                        spec.action_id = action_id;
                        spec.source = SLAYER3D_INPUT_KEYBOARD;
                        spec.gamepad_index = -1;
                        spec.scancode = code;
                        spec.scale = 1.0f;
                        if (!append_input_binding_spec(runtime, &spec))
                            return false;
                        bind_input_spec(input, &spec);
                    }
                }
                else if (SDL_strcmp(device, "mouse") == 0)
                {
                    const char *axis = json_string(binding, "axis", NULL);
                    const char *button = json_string(binding, "button", NULL);
                    if (axis != NULL)
                    {
                        bool valid_axis = false;
                        const slayer3d_mouse_axis mouse_axis = mouse_axis_from_json(axis, &valid_axis);
                        if (valid_axis)
                        {
                            input_binding_spec spec;
                            SDL_zero(spec);
                            spec.action = name;
                            spec.action_id = action_id;
                            spec.source = SLAYER3D_INPUT_MOUSE_AXIS;
                            spec.gamepad_index = -1;
                            spec.mouse_axis = mouse_axis;
                            spec.scale = scale;
                            if (!append_input_binding_spec(runtime, &spec))
                                return false;
                            bind_input_spec(input, &spec);
                        }
                    }
                    else if (button != NULL)
                    {
                        const Uint8 mouse_button = mouse_button_from_json(button);
                        if (mouse_button != 0)
                        {
                            input_binding_spec spec;
                            SDL_zero(spec);
                            spec.action = name;
                            spec.action_id = action_id;
                            spec.source = SLAYER3D_INPUT_MOUSE_BUTTON;
                            spec.gamepad_index = -1;
                            spec.mouse_button = mouse_button;
                            spec.scale = 1.0f;
                            if (!append_input_binding_spec(runtime, &spec))
                                return false;
                            bind_input_spec(input, &spec);
                        }
                    }
                }
                else if (SDL_strcmp(device, "gamepad") == 0)
                {
                    const char *axis = json_string(binding, "axis", NULL);
                    const char *button = json_string(binding, "button", NULL);
                    if (axis != NULL)
                    {
                        const SDL_GamepadAxis gamepad_axis = gamepad_axis_from_json(axis);
                        if (gamepad_axis != SDL_GAMEPAD_AXIS_INVALID)
                        {
                            input_binding_spec spec;
                            SDL_zero(spec);
                            spec.action = name;
                            spec.action_id = action_id;
                            spec.source = SLAYER3D_INPUT_GAMEPAD_AXIS;
                            spec.gamepad_index = json_int(binding, "slot", -1);
                            spec.gamepad_axis = gamepad_axis;
                            spec.scale = scale;
                            if (!append_input_binding_spec(runtime, &spec))
                                return false;
                            bind_input_spec(input, &spec);
                        }
                    }
                    else if (button != NULL)
                    {
                        const SDL_GamepadButton gamepad_button = gamepad_button_from_json(button);
                        if (gamepad_button != SDL_GAMEPAD_BUTTON_INVALID)
                        {
                            input_binding_spec spec;
                            SDL_zero(spec);
                            spec.action = name;
                            spec.action_id = action_id;
                            spec.source = SLAYER3D_INPUT_GAMEPAD_BUTTON;
                            spec.gamepad_index = json_int(binding, "slot", -1);
                            spec.gamepad_button = gamepad_button;
                            spec.scale = 1.0f;
                            if (!append_input_binding_spec(runtime, &spec))
                                return false;
                            bind_input_spec(input, &spec);
                        }
                    }
                }
            }
        }
    }
    return true;
}

static yyjson_val *find_input_profile_json(const slayer3d_game_data_runtime *runtime, const char *profile_name)
{
    yyjson_val *profiles = obj_get(obj_get(runtime_root(runtime), "input"), "profiles");
    for (size_t i = 0; profile_name != NULL && yyjson_is_arr(profiles) && i < yyjson_arr_size(profiles); ++i)
    {
        yyjson_val *profile = yyjson_arr_get(profiles, i);
        const char *name = json_string(profile, "name", NULL);
        if (name != NULL && SDL_strcmp(name, profile_name) == 0)
            return profile;
    }
    return NULL;
}

static bool input_profile_binding_to_spec(const slayer3d_game_data_runtime *runtime, yyjson_val *binding,
                                          input_binding_spec *out_spec, char *error_buffer, int error_buffer_size)
{
    const char *action = json_string(binding, "action", NULL);
    const char *device = json_string(binding, "device", "");
    const int action_id = slayer3d_game_data_find_action(runtime, action);
    const float scale = json_float(binding, "scale", 1.0f);
    if (binding == NULL || out_spec == NULL || action == NULL || action_id < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "input profile binding references unknown action '%s'",
                   action != NULL ? action : "<missing>");
        return false;
    }

    SDL_zero(*out_spec);
    out_spec->action = action;
    out_spec->action_id = action_id;
    out_spec->gamepad_index = -1;
    out_spec->scale = scale;

    if (SDL_strcmp(device, "keyboard") == 0)
    {
        const SDL_Scancode code = scancode_from_json(json_string(binding, "key", NULL));
        if (code == SDL_SCANCODE_UNKNOWN)
        {
            set_error(error_buffer, error_buffer_size, "input profile keyboard binding has an invalid key");
            return false;
        }
        out_spec->source = SLAYER3D_INPUT_KEYBOARD;
        out_spec->scancode = code;
        out_spec->scale = 1.0f;
        return true;
    }
    if (SDL_strcmp(device, "mouse") == 0)
    {
        const char *axis = json_string(binding, "axis", NULL);
        const char *button = json_string(binding, "button", NULL);
        if (axis != NULL)
        {
            bool valid_axis = false;
            out_spec->mouse_axis = mouse_axis_from_json(axis, &valid_axis);
            if (!valid_axis)
            {
                set_error(error_buffer, error_buffer_size, "input profile mouse binding has an invalid axis");
                return false;
            }
            out_spec->source = SLAYER3D_INPUT_MOUSE_AXIS;
            return true;
        }
        out_spec->mouse_button = mouse_button_from_json(button);
        if (out_spec->mouse_button == 0)
        {
            set_error(error_buffer, error_buffer_size, "input profile mouse binding has an invalid button");
            return false;
        }
        out_spec->source = SLAYER3D_INPUT_MOUSE_BUTTON;
        out_spec->scale = 1.0f;
        return true;
    }
    if (SDL_strcmp(device, "gamepad") == 0)
    {
        const char *axis = json_string(binding, "axis", NULL);
        const char *button = json_string(binding, "button", NULL);
        out_spec->gamepad_index = json_int(binding, "slot", -1);
        if (axis != NULL)
        {
            out_spec->gamepad_axis = gamepad_axis_from_json(axis);
            if (out_spec->gamepad_axis == SDL_GAMEPAD_AXIS_INVALID)
            {
                set_error(error_buffer, error_buffer_size, "input profile gamepad binding has an invalid axis");
                return false;
            }
            out_spec->source = SLAYER3D_INPUT_GAMEPAD_AXIS;
            return true;
        }
        out_spec->gamepad_button = gamepad_button_from_json(button);
        if (out_spec->gamepad_button == SDL_GAMEPAD_BUTTON_INVALID)
        {
            set_error(error_buffer, error_buffer_size, "input profile gamepad binding has an invalid button");
            return false;
        }
        out_spec->source = SLAYER3D_INPUT_GAMEPAD_BUTTON;
        out_spec->scale = 1.0f;
        return true;
    }

    set_errorf(error_buffer, error_buffer_size, "input profile binding uses unsupported device '%s'",
               device != NULL ? device : "<missing>");
    return false;
}

static bool input_assignment_binding_to_spec(const slayer3d_game_data_runtime *runtime, const char *device,
                                             yyjson_val *binding, const char *action, int gamepad_slot,
                                             input_binding_spec *out_spec, char *error_buffer, int error_buffer_size)
{
    const int action_id = slayer3d_game_data_find_action(runtime, action);
    const float scale = json_float(binding, "scale", 1.0f);
    if (runtime == NULL || binding == NULL || out_spec == NULL || action == NULL || action_id < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "input assignment references unknown action '%s'",
                   action != NULL ? action : "<missing>");
        return false;
    }

    SDL_zero(*out_spec);
    out_spec->action = action;
    out_spec->action_id = action_id;
    out_spec->gamepad_index = -1;
    out_spec->scale = scale;

    if (SDL_strcmp(device != NULL ? device : "", "keyboard") == 0)
    {
        const SDL_Scancode code = scancode_from_json(json_string(binding, "key", NULL));
        if (code == SDL_SCANCODE_UNKNOWN)
        {
            set_error(error_buffer, error_buffer_size, "input assignment keyboard binding has an invalid key");
            return false;
        }
        out_spec->source = SLAYER3D_INPUT_KEYBOARD;
        out_spec->scancode = code;
        out_spec->scale = 1.0f;
        return true;
    }

    if (SDL_strcmp(device != NULL ? device : "", "mouse") == 0)
    {
        const char *axis = json_string(binding, "axis", NULL);
        const char *button = json_string(binding, "button", NULL);
        if (axis != NULL)
        {
            bool valid_axis = false;
            out_spec->mouse_axis = mouse_axis_from_json(axis, &valid_axis);
            if (!valid_axis)
            {
                set_error(error_buffer, error_buffer_size, "input assignment mouse binding has an invalid axis");
                return false;
            }
            out_spec->source = SLAYER3D_INPUT_MOUSE_AXIS;
            return true;
        }
        out_spec->mouse_button = mouse_button_from_json(button);
        if (out_spec->mouse_button == 0)
        {
            set_error(error_buffer, error_buffer_size, "input assignment mouse binding has an invalid button");
            return false;
        }
        out_spec->source = SLAYER3D_INPUT_MOUSE_BUTTON;
        out_spec->scale = 1.0f;
        return true;
    }

    if (SDL_strcmp(device != NULL ? device : "", "gamepad") == 0)
    {
        const char *axis = json_string(binding, "axis", NULL);
        const char *button = json_string(binding, "button", NULL);
        out_spec->gamepad_index = gamepad_slot;
        if (axis != NULL)
        {
            out_spec->gamepad_axis = gamepad_axis_from_json(axis);
            if (out_spec->gamepad_axis == SDL_GAMEPAD_AXIS_INVALID)
            {
                set_error(error_buffer, error_buffer_size, "input assignment gamepad binding has an invalid axis");
                return false;
            }
            out_spec->source = SLAYER3D_INPUT_GAMEPAD_AXIS;
            return true;
        }
        out_spec->gamepad_button = gamepad_button_from_json(button);
        if (out_spec->gamepad_button == SDL_GAMEPAD_BUTTON_INVALID)
        {
            set_error(error_buffer, error_buffer_size, "input assignment gamepad binding has an invalid button");
            return false;
        }
        out_spec->source = SLAYER3D_INPUT_GAMEPAD_BUTTON;
        out_spec->scale = 1.0f;
        return true;
    }

    set_errorf(error_buffer, error_buffer_size, "input assignment set uses unsupported device '%s'",
               device != NULL ? device : "<missing>");
    return false;
}

static bool apply_input_assignment_json(slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                        yyjson_val *assignment, char *error_buffer, int error_buffer_size)
{
    const char *set_name = json_string(assignment, "set", NULL);
    yyjson_val *set = slayer3d_game_data_find_input_assignment_set_json(runtime_root(runtime), set_name);
    yyjson_val *actions = obj_get(assignment, "actions");
    yyjson_val *bindings = obj_get(set, "bindings");
    const char *device = json_string(set, "device", NULL);
    const int gamepad_slot = json_int(assignment, "slot", -1);
    if (runtime == NULL || input == NULL || assignment == NULL || set == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "input assignment set '%s' was not found",
                   set_name != NULL ? set_name : "<missing>");
        return false;
    }
    if (!yyjson_is_obj(actions))
    {
        set_errorf(error_buffer, error_buffer_size, "input assignment '%s' requires an actions object",
                   set_name != NULL ? set_name : "<missing>");
        return false;
    }

    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        const char *semantic = json_string(binding, "semantic", NULL);
        const char *action = semantic != NULL ? json_string(actions, semantic, NULL) : NULL;
        input_binding_spec spec;
        if (semantic == NULL || action == NULL)
        {
            set_errorf(error_buffer, error_buffer_size, "input assignment '%s' is missing action for semantic '%s'",
                       set_name != NULL ? set_name : "<missing>", semantic != NULL ? semantic : "<missing>");
            return false;
        }
        if (!input_assignment_binding_to_spec(runtime, device, binding, action, gamepad_slot, &spec, error_buffer,
                                              error_buffer_size))
        {
            return false;
        }
        bind_input_spec(input, &spec);
    }
    return true;
}

static bool apply_input_profile_json(slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                     yyjson_val *profile, char *error_buffer, int error_buffer_size)
{
    yyjson_val *unbind = obj_get(profile, "unbind");
    yyjson_val *bindings = obj_get(profile, "bindings");
    yyjson_val *assignments = obj_get(profile, "assignments");
    if (runtime == NULL || input == NULL || profile == NULL)
    {
        set_error(error_buffer, error_buffer_size, "input profile apply requires runtime, input, and profile");
        return false;
    }
    if (bindings != NULL && assignments != NULL)
    {
        set_error(error_buffer, error_buffer_size, "input profile cannot mix bindings and assignments");
        return false;
    }

    for (size_t i = 0; yyjson_is_arr(unbind) && i < yyjson_arr_size(unbind); ++i)
    {
        const char *action = yyjson_get_str(yyjson_arr_get(unbind, i));
        const int action_id = slayer3d_game_data_find_action(runtime, action);
        if (action_id < 0)
        {
            set_errorf(error_buffer, error_buffer_size, "input profile unbind references unknown action '%s'",
                       action != NULL ? action : "<invalid>");
            return false;
        }
        slayer3d_input_unbind_action(input, action_id);
    }

    for (size_t i = 0; yyjson_is_arr(bindings) && i < yyjson_arr_size(bindings); ++i)
    {
        input_binding_spec spec;
        if (!input_profile_binding_to_spec(runtime, yyjson_arr_get(bindings, i), &spec, error_buffer,
                                           error_buffer_size))
        {
            return false;
        }
        bind_input_spec(input, &spec);
    }

    for (size_t i = 0; yyjson_is_arr(assignments) && i < yyjson_arr_size(assignments); ++i)
    {
        if (!apply_input_assignment_json(runtime, input, yyjson_arr_get(assignments, i), error_buffer,
                                         error_buffer_size))
        {
            return false;
        }
    }
    return true;
}

static bool input_profile_matches(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                  yyjson_val *profile)
{
    const int gamepads = slayer3d_input_gamepad_count(input);
    yyjson_val *min_gamepads = obj_get(profile, "min_gamepads");
    yyjson_val *max_gamepads = obj_get(profile, "max_gamepads");
    if (yyjson_is_num(min_gamepads) && gamepads < (int)yyjson_get_sint(min_gamepads))
        return false;
    if (yyjson_is_num(max_gamepads) && gamepads > (int)yyjson_get_sint(max_gamepads))
        return false;
    return eval_data_condition(runtime, obj_get(profile, "active_if"), NULL);
}

static yyjson_val *find_active_input_profile_json(const slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_input_manager *input)
{
    if (runtime == NULL || input == NULL)
        return NULL;

    yyjson_val *profiles = obj_get(obj_get(runtime_root(runtime), "input"), "profiles");
    for (size_t i = 0; yyjson_is_arr(profiles) && i < yyjson_arr_size(profiles); ++i)
    {
        yyjson_val *profile = yyjson_arr_get(profiles, i);
        if (input_profile_matches(runtime, input, profile))
            return profile;
    }
    return NULL;
}

bool slayer3d_game_data_apply_input_profile(slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                            const char *profile_name, char *error_buffer, int error_buffer_size)
{
    yyjson_val *profile = find_input_profile_json(runtime, profile_name);
    if (profile == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "input profile '%s' was not found",
                   profile_name != NULL ? profile_name : "<null>");
        return false;
    }
    return apply_input_profile_json(runtime, input, profile, error_buffer, error_buffer_size);
}

bool slayer3d_game_data_apply_active_input_profile(slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                                   const char **out_profile_name, char *error_buffer,
                                                   int error_buffer_size)
{
    if (out_profile_name != NULL)
        *out_profile_name = NULL;
    if (runtime == NULL || input == NULL)
    {
        set_error(error_buffer, error_buffer_size, "active input profile apply requires runtime and input");
        return false;
    }

    yyjson_val *profile = find_active_input_profile_json(runtime, input);
    if (profile != NULL)
    {
        const char *name = json_string(profile, "name", NULL);
        if (!apply_input_profile_json(runtime, input, profile, error_buffer, error_buffer_size))
            return false;
        if (out_profile_name != NULL)
            *out_profile_name = name;
        return true;
    }

    set_error(error_buffer, error_buffer_size, "no authored input profile matched the current state");
    return false;
}

bool slayer3d_game_data_get_active_input_profile_name(const slayer3d_game_data_runtime *runtime,
                                                      const slayer3d_input_manager *input,
                                                      const char **out_profile_name)
{
    if (out_profile_name != NULL)
        *out_profile_name = NULL;

    yyjson_val *profile = find_active_input_profile_json(runtime, input);
    if (profile == NULL)
        return false;

    if (out_profile_name != NULL)
        *out_profile_name = json_string(profile, "name", NULL);
    return true;
}

void slayer3d_game_data_input_profile_refresh_state_init(slayer3d_game_data_input_profile_refresh_state *state)
{
    if (state == NULL)
        return;

    state->gamepad_count = -1;
    state->initialized = false;
}

bool slayer3d_game_data_apply_active_input_profile_on_device_change(
    slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
    slayer3d_game_data_input_profile_refresh_state *state, const char **out_profile_name, bool *out_applied,
    char *error_buffer, int error_buffer_size)
{
    if (out_profile_name != NULL)
        *out_profile_name = NULL;
    if (out_applied != NULL)
        *out_applied = false;
    if (runtime == NULL || input == NULL || state == NULL)
    {
        set_error(error_buffer, error_buffer_size,
                  "active input profile device refresh requires runtime, input, and refresh state");
        return false;
    }

    const int gamepad_count = slayer3d_input_gamepad_count(input);
    if (state->initialized && state->gamepad_count == gamepad_count)
        return true;

    const char *profile_name = NULL;
    if (!slayer3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error_buffer, error_buffer_size))
        return false;

    state->gamepad_count = gamepad_count;
    state->initialized = true;
    if (out_profile_name != NULL)
        *out_profile_name = profile_name;
    if (out_applied != NULL)
        *out_applied = true;
    return true;
}

/* Timer, adapter, script, and Lua invocation helpers. Included by src/game_data.c to preserve internal linkage. */

static int find_timer_index(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return -1;
    for (int i = 0; i < runtime->timer_count; ++i)
    {
        if (SDL_strcmp(runtime->timers[i].name, name) == 0)
            return i;
    }
    return -1;
}

static adapter_entry *find_adapter(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->adapter_count; ++i)
    {
        if (SDL_strcmp(runtime->adapters[i].name, name) == 0)
            return &runtime->adapters[i];
    }
    return NULL;
}

static script_entry *find_script(slayer3d_game_data_runtime *runtime, const char *id)
{
    if (runtime == NULL || id == NULL)
        return NULL;
    for (int i = 0; i < runtime->script_count; ++i)
    {
        if (SDL_strcmp(runtime->script_entries[i].id, id) == 0)
            return &runtime->script_entries[i];
    }
    return NULL;
}

static bool append_adapter(slayer3d_game_data_runtime *runtime, const char *name,
                           slayer3d_game_data_adapter_fn callback, void *userdata)
{
    adapter_entry *entries =
        (adapter_entry *)SDL_realloc(runtime->adapters, (size_t)(runtime->adapter_count + 1) * sizeof(*entries));
    if (entries == NULL)
        return false;
    runtime->adapters = entries;

    adapter_entry *entry = &runtime->adapters[runtime->adapter_count];
    SDL_zero(*entry);
    entry->name = SDL_strdup(name);
    if (entry->name == NULL)
        return false;
    entry->callback = callback;
    entry->userdata = userdata;
    runtime->adapter_count++;
    return true;
}

static bool set_adapter_lua_function(slayer3d_game_data_runtime *runtime, const char *name, const char *script_id,
                                     const char *function_name, slayer3d_script_ref function_ref)
{
    if (runtime == NULL || name == NULL || name[0] == '\0' || script_id == NULL || script_id[0] == '\0' ||
        function_name == NULL || function_name[0] == '\0' || function_ref == SLAYER3D_SCRIPT_REF_INVALID)
        return false;

    adapter_entry *entry = find_adapter(runtime, name);
    if (entry == NULL)
    {
        if (!append_adapter(runtime, name, NULL, NULL))
            return false;
        entry = find_adapter(runtime, name);
    }
    if (entry == NULL)
        return false;

    char *script_copy = SDL_strdup(script_id);
    char *function_copy = SDL_strdup(function_name);
    if (script_copy == NULL || function_copy == NULL)
    {
        SDL_free(script_copy);
        SDL_free(function_copy);
        return false;
    }

    SDL_free(entry->lua_script_id);
    SDL_free(entry->lua_function);
    entry->lua_script_id = script_copy;
    entry->lua_function = function_copy;
    if (entry->lua_function_ref != SLAYER3D_SCRIPT_REF_INVALID)
        slayer3d_script_engine_unref(runtime->scripts, entry->lua_function_ref);
    entry->lua_function_ref = function_ref;
    return true;
}

static void lua_push_property_value(lua_State *lua, const slayer3d_value *value)
{
    if (value == NULL)
    {
        lua_pushnil(lua);
        return;
    }

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        lua_pushinteger(lua, value->as_int);
        break;
    case SLAYER3D_VALUE_FLOAT:
        lua_pushnumber(lua, value->as_float);
        break;
    case SLAYER3D_VALUE_BOOL:
        lua_pushboolean(lua, value->as_bool);
        break;
    case SLAYER3D_VALUE_STRING:
        lua_pushstring(lua, value->as_string != NULL ? value->as_string : "");
        break;
    case SLAYER3D_VALUE_VEC3:
        lua_newtable(lua);
        lua_pushnumber(lua, value->as_vec3.x);
        lua_setfield(lua, -2, "x");
        lua_pushnumber(lua, value->as_vec3.y);
        lua_setfield(lua, -2, "y");
        lua_pushnumber(lua, value->as_vec3.z);
        lua_setfield(lua, -2, "z");
        break;
    case SLAYER3D_VALUE_COLOR:
        lua_newtable(lua);
        lua_pushinteger(lua, value->as_color.r);
        lua_setfield(lua, -2, "r");
        lua_pushinteger(lua, value->as_color.g);
        lua_setfield(lua, -2, "g");
        lua_pushinteger(lua, value->as_color.b);
        lua_setfield(lua, -2, "b");
        lua_pushinteger(lua, value->as_color.a);
        lua_setfield(lua, -2, "a");
        break;
    }
}

static void lua_push_payload(lua_State *lua, const slayer3d_properties *payload)
{
    lua_newtable(lua);
    if (payload == NULL)
        return;

    const int count = slayer3d_properties_count(payload);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (!slayer3d_properties_get_key_at(payload, i, &key, NULL) || key == NULL)
            continue;
        lua_push_property_value(lua, slayer3d_properties_get_value(payload, key));
        lua_setfield(lua, -2, key);
    }
}

static void lua_push_actor_wrapper(lua_State *lua, const slayer3d_registered_actor *actor)
{
    if (actor == NULL)
    {
        lua_pushnil(lua);
        return;
    }

    lua_getglobal(lua, "slayer3d");
    if (!lua_istable(lua, -1))
    {
        lua_pop(lua, 1);
        lua_pushnil(lua);
        return;
    }
    lua_getfield(lua, -1, "actor");
    if (!lua_isfunction(lua, -1))
    {
        lua_pop(lua, 2);
        lua_pushnil(lua);
        return;
    }
    lua_pushstring(lua, actor->name);
    if (lua_pcall(lua, 1, 1, 0) != LUA_OK)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[lua] actor wrapper creation failed: %s", lua_tostring(lua, -1));
        lua_pop(lua, 2);
        lua_pushnil(lua);
        return;
    }
    lua_remove(lua, -2);
}

static void lua_push_adapter_context(lua_State *lua, const slayer3d_game_data_runtime *runtime,
                                     const adapter_entry *adapter)
{
    lua_getglobal(lua, "slayer3d");
    if (!lua_istable(lua, -1))
    {
        lua_pop(lua, 1);
        lua_newtable(lua);
        return;
    }
    lua_getfield(lua, -1, "_context");
    if (!lua_isfunction(lua, -1))
    {
        lua_pop(lua, 2);
        lua_newtable(lua);
        return;
    }
    lua_pushstring(lua, adapter != NULL ? adapter->name : "");
    lua_pushnumber(lua, runtime != NULL ? runtime->current_dt : 0.0f);
    if (lua_pcall(lua, 2, 1, 0) != LUA_OK)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[lua] adapter context creation failed: %s", lua_tostring(lua, -1));
        lua_pop(lua, 2);
        lua_newtable(lua);
        return;
    }
    lua_remove(lua, -2);
}

static bool call_lua_adapter(slayer3d_game_data_runtime *runtime, const adapter_entry *adapter,
                             slayer3d_registered_actor *target, const slayer3d_properties *payload)
{
    if (runtime == NULL || runtime->scripts == NULL || adapter == NULL ||
        adapter->lua_function_ref == SLAYER3D_SCRIPT_REF_INVALID)
        return false;

    lua_State *lua = slayer3d_script_engine_lua_state(runtime->scripts);
    if (lua == NULL || !slayer3d_script_engine_push_ref(runtime->scripts, adapter->lua_function_ref))
        return false;

    lua_push_actor_wrapper(lua, target);
    lua_push_payload(lua, payload);
    lua_push_adapter_context(lua, runtime, adapter);

    if (lua_pcall(lua, 3, 1, 0) != LUA_OK)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[lua] adapter %s failed: %s", adapter->name, lua_tostring(lua, -1));
        lua_pop(lua, 1);
        return false;
    }

    const bool ok = lua_isboolean(lua, -1) ? lua_toboolean(lua, -1) : true;
    lua_pop(lua, 1);
    return ok;
}

static bool invoke_adapter(slayer3d_game_data_runtime *runtime, adapter_entry *adapter,
                           slayer3d_registered_actor *target, const slayer3d_properties *payload)
{
    if (adapter == NULL)
        return false;
    if (adapter->callback != NULL)
        return adapter->callback(adapter->userdata, runtime, adapter->name, target, payload);
    return call_lua_adapter(runtime, adapter, target, payload);
}

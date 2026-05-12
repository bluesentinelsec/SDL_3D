/* Lua scripting bridge helpers. Included by src/game_data.c to preserve internal linkage. */

static slayer3d_game_data_runtime *lua_runtime(lua_State *lua)
{
    return (slayer3d_game_data_runtime *)lua_touserdata(lua, lua_upvalueindex(1));
}

static lua_Integer lua_counter_value(Uint64 value)
{
    return (lua_Integer)SDL_min(value, (Uint64)LUA_MAXINTEGER);
}

static slayer3d_registered_actor *lua_actor_arg(lua_State *lua, slayer3d_game_data_runtime *runtime, int index)
{
    const char *actor_name = NULL;
    if (lua_istable(lua, index))
    {
        lua_getfield(lua, index, "_name");
        actor_name = lua_tostring(lua, -1);
        lua_pop(lua, 1);
    }
    else
    {
        actor_name = luaL_checkstring(lua, index);
    }
    return slayer3d_game_data_find_actor(runtime, actor_name);
}

static bool ensure_runtime_storage(slayer3d_game_data_runtime *runtime, char *error_buffer, int error_buffer_size);

static int lua_get_position(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor == NULL)
    {
        lua_pushnil(lua);
        return 1;
    }

    lua_pushnumber(lua, actor->position.x);
    lua_pushnumber(lua, actor->position.y);
    lua_pushnumber(lua, actor->position.z);
    return 3;
}

static int lua_set_position(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor == NULL)
        return 0;

    const slayer3d_vec3 position = slayer3d_vec3_make((float)luaL_checknumber(lua, 2), (float)luaL_checknumber(lua, 3),
                                                      (float)luaL_optnumber(lua, 4, actor->position.z));
    actor_set_position(actor, position);
    return 0;
}

static int lua_get_float(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const float fallback = (float)luaL_optnumber(lua, 3, 0.0);
    lua_pushnumber(lua, actor != NULL ? slayer3d_properties_get_float(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_float(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        slayer3d_properties_set_float(actor->props, luaL_checkstring(lua, 2), (float)luaL_checknumber(lua, 3));
    return 0;
}

static int lua_get_int(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const int fallback = (int)luaL_optinteger(lua, 3, 0);
    lua_pushinteger(lua, actor != NULL ? slayer3d_properties_get_int(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_int(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        slayer3d_properties_set_int(actor->props, luaL_checkstring(lua, 2), (int)luaL_checkinteger(lua, 3));
    return 0;
}

static int lua_get_bool(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const bool fallback = lua_toboolean(lua, 3);
    lua_pushboolean(lua, actor != NULL ? slayer3d_properties_get_bool(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_bool(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        slayer3d_properties_set_bool(actor->props, luaL_checkstring(lua, 2), lua_toboolean(lua, 3));
    return 0;
}

static int lua_get_string(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    const char *fallback = luaL_optstring(lua, 3, "");
    lua_pushstring(lua, actor != NULL ? slayer3d_properties_get_string(actor->props, key, fallback) : fallback);
    return 1;
}

static int lua_set_string(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
        slayer3d_properties_set_string(actor->props, luaL_checkstring(lua, 2), luaL_checkstring(lua, 3));
    return 0;
}

static int lua_get_vec3(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    const char *key = luaL_checkstring(lua, 2);
    if (actor == NULL)
    {
        lua_pushnil(lua);
        return 1;
    }
    const slayer3d_vec3 value = slayer3d_properties_get_vec3(actor->props, key, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    lua_pushnumber(lua, value.x);
    lua_pushnumber(lua, value.y);
    lua_pushnumber(lua, value.z);
    return 3;
}

static int lua_set_vec3(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    if (actor != NULL)
    {
        slayer3d_properties_set_vec3(actor->props, luaL_checkstring(lua, 2),
                                     slayer3d_vec3_make((float)luaL_checknumber(lua, 3),
                                                        (float)luaL_checknumber(lua, 4),
                                                        (float)luaL_optnumber(lua, 5, 0.0)));
    }
    return 0;
}

static int lua_actor_active(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_arg(lua, runtime, 1);
    lua_pushboolean(lua, actor != NULL && actor->active);
    return 1;
}

static int lua_get_dt(lua_State *lua)
{
    lua_pushnumber(lua, slayer3d_game_data_delta_time(lua_runtime(lua)));
    return 1;
}

static int lua_state_get(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const slayer3d_value *value =
        slayer3d_properties_get_value(slayer3d_game_data_scene_state(runtime), luaL_checkstring(lua, 1));
    if (value == NULL)
    {
        lua_pushvalue(lua, 2);
        return 1;
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
    return 1;
}

static int lua_state_set(lua_State *lua)
{
    slayer3d_properties *state = slayer3d_game_data_mutable_scene_state(lua_runtime(lua));
    const char *key = luaL_checkstring(lua, 1);
    if (state == NULL)
        return 0;

    if (lua_isnoneornil(lua, 2))
    {
        slayer3d_properties_remove(state, key);
    }
    else if (lua_isboolean(lua, 2))
    {
        slayer3d_properties_set_bool(state, key, lua_toboolean(lua, 2));
    }
    else if (lua_isinteger(lua, 2))
    {
        slayer3d_properties_set_int(state, key, (int)lua_tointeger(lua, 2));
    }
    else if (lua_isnumber(lua, 2))
    {
        slayer3d_properties_set_float(state, key, (float)lua_tonumber(lua, 2));
    }
    else if (lua_isstring(lua, 2))
    {
        slayer3d_properties_set_string(state, key, lua_tostring(lua, 2));
    }
    else if (lua_istable(lua, 2))
    {
        lua_getfield(lua, 2, "x");
        lua_getfield(lua, 2, "y");
        lua_getfield(lua, 2, "z");
        const bool has_xy = lua_isnumber(lua, -3) && lua_isnumber(lua, -2);
        const slayer3d_vec3 value = slayer3d_vec3_make((float)lua_tonumber(lua, -3), (float)lua_tonumber(lua, -2),
                                                       lua_isnumber(lua, -1) ? (float)lua_tonumber(lua, -1) : 0.0f);
        lua_pop(lua, 3);
        if (has_xy)
            slayer3d_properties_set_vec3(state, key, value);
    }
    return 0;
}

static int lua_random(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    if (runtime == NULL)
    {
        lua_pushnumber(lua, 0.0);
        return 1;
    }

    lua_pushnumber(lua, (lua_Number)game_data_random01(runtime));
    return 1;
}

static int lua_actor_with_tags(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const int arg_count = lua_gettop(lua);
    const char *tags[16];
    int tag_count = 0;
    if (lua_istable(lua, 1))
    {
        const lua_Integer count = luaL_len(lua, 1);
        for (lua_Integer i = 1; i <= count && tag_count < (int)SDL_arraysize(tags); ++i)
        {
            lua_geti(lua, 1, i);
            const char *tag = lua_tostring(lua, -1);
            if (tag != NULL && tag[0] != '\0')
                tags[tag_count++] = tag;
            lua_pop(lua, 1);
        }
    }
    else
    {
        for (int i = 1; i <= arg_count && tag_count < (int)SDL_arraysize(tags); ++i)
        {
            const char *tag = lua_tostring(lua, i);
            if (tag != NULL && tag[0] != '\0')
                tags[tag_count++] = tag;
        }
    }

    lua_push_actor_wrapper(lua, slayer3d_game_data_find_actor_with_tags(runtime, tags, tag_count));
    return 1;
}

static int lua_collect_tags(lua_State *lua, int start_index, const char **tags, int max_tags)
{
    const int arg_count = lua_gettop(lua);
    int tag_count = 0;
    if (lua_istable(lua, start_index))
    {
        const lua_Integer count = luaL_len(lua, start_index);
        for (lua_Integer i = 1; i <= count && tag_count < max_tags; ++i)
        {
            lua_geti(lua, start_index, i);
            const char *tag = lua_tostring(lua, -1);
            if (tag != NULL && tag[0] != '\0')
                tags[tag_count++] = tag;
            lua_pop(lua, 1);
        }
        return tag_count;
    }

    for (int i = start_index; i <= arg_count && tag_count < max_tags; ++i)
    {
        const char *tag = lua_tostring(lua, i);
        if (tag != NULL && tag[0] != '\0')
            tags[tag_count++] = tag;
    }
    return tag_count;
}

static bool lua_read_vec3_value(lua_State *lua, int index, slayer3d_vec3 fallback, slayer3d_vec3 *out_value)
{
    if (out_value != NULL)
        *out_value = fallback;
    if (!lua_istable(lua, index))
        return false;

    index = lua_absindex(lua, index);
    lua_getfield(lua, index, "x");
    lua_getfield(lua, index, "y");
    lua_getfield(lua, index, "z");
    bool has_xy = lua_isnumber(lua, -3) && lua_isnumber(lua, -2);
    slayer3d_vec3 value = slayer3d_vec3_make(has_xy ? (float)lua_tonumber(lua, -3) : fallback.x,
                                             has_xy ? (float)lua_tonumber(lua, -2) : fallback.y,
                                             lua_isnumber(lua, -1) ? (float)lua_tonumber(lua, -1) : fallback.z);
    lua_pop(lua, 3);

    if (!has_xy)
    {
        lua_geti(lua, index, 1);
        lua_geti(lua, index, 2);
        lua_geti(lua, index, 3);
        has_xy = lua_isnumber(lua, -3) && lua_isnumber(lua, -2);
        value = slayer3d_vec3_make(has_xy ? (float)lua_tonumber(lua, -3) : fallback.x,
                                   has_xy ? (float)lua_tonumber(lua, -2) : fallback.y,
                                   lua_isnumber(lua, -1) ? (float)lua_tonumber(lua, -1) : fallback.z);
        lua_pop(lua, 3);
    }

    if (!has_xy)
        return false;
    if (out_value != NULL)
        *out_value = value;
    return true;
}

static bool lua_read_vec3_field(lua_State *lua, int table_index, const char *field, slayer3d_vec3 fallback,
                                slayer3d_vec3 *out_value)
{
    if (!lua_istable(lua, table_index) || field == NULL)
        return false;
    table_index = lua_absindex(lua, table_index);
    lua_getfield(lua, table_index, field);
    const bool ok = lua_read_vec3_value(lua, -1, fallback, out_value);
    lua_pop(lua, 1);
    return ok;
}

static void lua_set_actor_property_from_value(lua_State *lua, slayer3d_registered_actor *actor, const char *key,
                                              int index)
{
    if (actor == NULL || key == NULL)
        return;
    index = lua_absindex(lua, index);
    if (lua_isboolean(lua, index))
        slayer3d_properties_set_bool(actor->props, key, lua_toboolean(lua, index));
    else if (lua_isinteger(lua, index))
        slayer3d_properties_set_int(actor->props, key, (int)lua_tointeger(lua, index));
    else if (lua_isnumber(lua, index))
        slayer3d_properties_set_float(actor->props, key, (float)lua_tonumber(lua, index));
    else if (lua_isstring(lua, index))
        slayer3d_properties_set_string(actor->props, key, lua_tostring(lua, index));
    else if (lua_istable(lua, index))
    {
        slayer3d_vec3 value;
        if (lua_read_vec3_value(lua, index, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), &value))
            slayer3d_properties_set_vec3(actor->props, key, value);
    }
}

static slayer3d_registered_actor *lua_actor_from_value(lua_State *lua, slayer3d_game_data_runtime *runtime, int index)
{
    if (lua_isnoneornil(lua, index))
        return NULL;
    if (lua_istable(lua, index))
    {
        lua_getfield(lua, index, "_name");
        const char *name = lua_tostring(lua, -1);
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, name);
        lua_pop(lua, 1);
        return actor;
    }
    if (lua_isstring(lua, index))
        return slayer3d_game_data_find_actor(runtime, lua_tostring(lua, index));
    return NULL;
}

static int lua_spawn_actor(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    actor_pool_runtime *pool = find_actor_pool(runtime, luaL_checkstring(lua, 1));
    actor_pool_note_spawn_attempt(pool);
    int actor_index = -1;
    slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
    if (pool == NULL || actor == NULL || actor_index < 0)
    {
        actor_pool_note_spawn_failure(pool, "exhausted");
        lua_pushnil(lua);
        lua_pushstring(lua, "actor pool exhausted or missing");
        return 2;
    }

    actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
    if (!initialize_pooled_actor(pool, actor, actor_index, true))
    {
        actor_pool_note_spawn_failure(pool, "initialize_failed");
        lua_pushnil(lua);
        lua_pushstring(lua, "failed to initialize pooled actor");
        return 2;
    }

    if (pool->spawn_generations != NULL)
    {
        pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
        slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                    (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
    }

    slayer3d_vec3 position = actor->position;
    if (lua_istable(lua, 2))
    {
        lua_read_vec3_field(lua, 2, "position", position, &position);
        lua_getfield(lua, 2, "from");
        slayer3d_registered_actor *from_actor = lua_actor_from_value(lua, runtime, -1);
        lua_pop(lua, 1);
        if (from_actor != NULL)
            position = from_actor->position;

        slayer3d_vec3 offset = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        if (lua_read_vec3_field(lua, 2, "offset", offset, &offset))
        {
            position.x += offset.x;
            position.y += offset.y;
            position.z += offset.z;
        }

        lua_getfield(lua, 2, "properties");
        if (lua_istable(lua, -1))
        {
            lua_pushnil(lua);
            while (lua_next(lua, -2) != 0)
            {
                const char *key = lua_tostring(lua, -2);
                if (key != NULL && key[0] != '\0')
                    lua_set_actor_property_from_value(lua, actor, key, -1);
                lua_pop(lua, 1);
            }
        }
        lua_pop(lua, 1);
    }

    actor_set_position(actor, position);
    actor_pool_note_spawn_success(runtime, pool);
    lua_push_actor_wrapper(lua, actor);
    lua_pushinteger(lua, actor->id);
    lua_pushinteger(lua, actor_index);
    return 3;
}

static int lua_despawn_actor(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    slayer3d_registered_actor *actor = lua_actor_from_value(lua, runtime, 1);
    if (actor == NULL)
    {
        lua_pushboolean(lua, false);
        return 1;
    }

    int actor_index = -1;
    actor_pool_runtime *pool = find_actor_pool_for_actor(runtime, actor->name, &actor_index);
    const char *reason = lua_isstring(lua, 2) ? lua_tostring(lua, 2) : "lua";
    const bool ok = pool != NULL && actor_index >= 0
                        ? actor_pool_request_despawn(runtime, pool, actor, actor_index, reason)
                        : (actor->active = false, true);
    lua_pushboolean(lua, ok);
    return 1;
}

static int lua_despawn_actors_by_tag(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *tag = luaL_checkstring(lua, 1);
    const char *reason = lua_isstring(lua, 2) ? lua_tostring(lua, 2) : "lua_tag";
    int despawned = 0;
    if (runtime != NULL && tag != NULL && tag[0] != '\0')
    {
        for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
        {
            actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
            if (!entity_json_has_tags(pool->archetype_json, &tag, 1))
                continue;
            for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
            {
                slayer3d_registered_actor *actor =
                    slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
                if (actor_pool_actor_is_active(pool, actor, actor_index) &&
                    actor_pool_request_despawn(runtime, pool, actor, actor_index, reason))
                    ++despawned;
            }
        }
    }
    lua_pushinteger(lua, despawned);
    return 1;
}

static int lua_pool_capacity(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? pool->capacity : 0);
    return 1;
}

static int lua_pool_active_count(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    actor_pool_runtime *pool = find_actor_pool(runtime, luaL_checkstring(lua, 1));
    lua_pushinteger(lua, actor_pool_active_count(runtime, pool));
    return 1;
}

static int lua_pool_available_count(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    actor_pool_runtime *pool = find_actor_pool(runtime, luaL_checkstring(lua, 1));
    lua_pushinteger(lua, actor_pool_available_count(runtime, pool));
    return 1;
}

static int lua_pool_peak_active_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? pool->peak_active_count : 0);
    return 1;
}

static int lua_pool_spawn_attempt_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->spawn_attempt_count) : 0);
    return 1;
}

static int lua_pool_spawn_success_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->spawn_success_count) : 0);
    return 1;
}

static int lua_pool_spawn_failure_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->spawn_failure_count) : 0);
    return 1;
}

static int lua_pool_exhaustion_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->exhaustion_count) : 0);
    return 1;
}

static int lua_pool_reuse_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->reuse_count) : 0);
    return 1;
}

static int lua_pool_despawn_count(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, pool != NULL ? lua_counter_value(pool->despawn_count) : 0);
    return 1;
}

static int lua_pool_last_spawn_failure_reason(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushstring(lua, pool != NULL && pool->last_spawn_failure_reason[0] != '\0' ? pool->last_spawn_failure_reason
                                                                                   : "none");
    return 1;
}

static int lua_pool_last_despawn_reason(lua_State *lua)
{
    actor_pool_runtime *pool = find_actor_pool(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushstring(lua, pool != NULL && pool->last_despawn_reason[0] != '\0' ? pool->last_despawn_reason : "none");
    return 1;
}

static int lua_active_actors_with_tags(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *tags[16];
    const int tag_count = lua_collect_tags(lua, 1, tags, (int)SDL_arraysize(tags));
    lua_newtable(lua);
    if (runtime == NULL || tag_count <= 0)
        return 1;

    int out_index = 1;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        if (!entity_json_has_tags(entity, tags, tag_count))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(entity, "name", NULL));
        if (actor != NULL && actor->active)
        {
            lua_push_actor_wrapper(lua, actor);
            lua_seti(lua, -2, out_index++);
        }
    }

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!entity_json_has_tags(pool->archetype_json, tags, tag_count))
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index))
            {
                lua_push_actor_wrapper(lua, actor);
                lua_seti(lua, -2, out_index++);
            }
        }
    }
    return 1;
}

static int lua_grid_actor_at(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *map_name = luaL_checkstring(lua, 1);
    const char *pool_name = luaL_checkstring(lua, 2);
    const int col = (int)luaL_checkinteger(lua, 3);
    const int row = (int)luaL_checkinteger(lua, 4);
    lua_push_actor_wrapper(lua, grid_actor_index_find(runtime, map_name, pool_name, col, row));
    return 1;
}

static void lua_push_grid_pickup(lua_State *lua, const grid_pickup_kind_runtime *kind, int col, int row)
{
    if (kind == NULL)
    {
        lua_pushnil(lua);
        return;
    }
    lua_newtable(lua);
    lua_pushstring(lua, kind->kind != NULL ? kind->kind : "");
    lua_setfield(lua, -2, "kind");
    lua_pushinteger(lua, kind->points);
    lua_setfield(lua, -2, "points");
    lua_pushinteger(lua, col);
    lua_setfield(lua, -2, "col");
    lua_pushinteger(lua, row);
    lua_setfield(lua, -2, "row");
}

static int lua_grid_pickup_at(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    grid_pickup_layer_runtime *layer = find_grid_pickup_layer(runtime, luaL_checkstring(lua, 1));
    int col = (int)luaL_checkinteger(lua, 2);
    int row = (int)luaL_checkinteger(lua, 3);
    if (layer == NULL || layer->cells == NULL || !grid_map_normalize_cell(layer->map, &col, &row))
    {
        lua_pushnil(lua);
        return 1;
    }
    const Uint8 kind_cell = layer->cells[row * layer->map->width + col];
    lua_push_grid_pickup(
        lua, kind_cell > 0 && kind_cell <= (Uint8)layer->kind_count ? &layer->kinds[kind_cell - 1] : NULL, col, row);
    return 1;
}

static int lua_grid_collect_at(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    grid_pickup_layer_runtime *layer = find_grid_pickup_layer(runtime, luaL_checkstring(lua, 1));
    int col = (int)luaL_checkinteger(lua, 2);
    int row = (int)luaL_checkinteger(lua, 3);
    grid_pickup_kind_runtime kind;
    SDL_zero(kind);
    if (!grid_pickup_layer_collect_at(runtime, layer, col, row, &kind))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_push_grid_pickup(lua, &kind, col, row);
    return 1;
}

static int lua_grid_pickup_count(lua_State *lua)
{
    grid_pickup_layer_runtime *layer = find_grid_pickup_layer(lua_runtime(lua), luaL_checkstring(lua, 1));
    lua_pushinteger(lua, layer != NULL ? layer->active_count : 0);
    return 1;
}

static int lua_log(lua_State *lua)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[lua] %s", luaL_checkstring(lua, 1));
    return 0;
}

static int lua_storage_read(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    if (!ensure_runtime_storage(runtime, error, (int)sizeof(error)))
    {
        lua_pushnil(lua);
        lua_pushstring(lua, error);
        return 2;
    }

    slayer3d_storage_buffer buffer;
    SDL_zero(buffer);
    if (!slayer3d_storage_read_file(runtime->storage, path, &buffer, error, (int)sizeof(error)))
    {
        lua_pushnil(lua);
        lua_pushstring(lua, error);
        return 2;
    }

    lua_pushlstring(lua, (const char *)buffer.data, buffer.size);
    slayer3d_storage_buffer_free(&buffer);
    return 1;
}

static int lua_storage_write(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);
    size_t size = 0u;
    const char *data = luaL_checklstring(lua, 2, &size);

    char error[256];
    const bool ok = ensure_runtime_storage(runtime, error, (int)sizeof(error)) &&
                    slayer3d_storage_write_file(runtime->storage, path, data, size, error, (int)sizeof(error));
    lua_pushboolean(lua, ok);
    if (!ok)
    {
        lua_pushstring(lua, error);
        return 2;
    }
    return 1;
}

static int lua_storage_exists(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    const bool ok =
        ensure_runtime_storage(runtime, error, (int)sizeof(error)) && slayer3d_storage_exists(runtime->storage, path);
    lua_pushboolean(lua, ok);
    return 1;
}

static int lua_storage_mkdir(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    const bool ok = ensure_runtime_storage(runtime, error, (int)sizeof(error)) &&
                    slayer3d_storage_create_directory(runtime->storage, path, error, (int)sizeof(error));
    lua_pushboolean(lua, ok);
    if (!ok)
    {
        lua_pushstring(lua, error);
        return 2;
    }
    return 1;
}

static int lua_storage_delete(lua_State *lua)
{
    slayer3d_game_data_runtime *runtime = lua_runtime(lua);
    const char *path = luaL_checkstring(lua, 1);

    char error[256];
    const bool ok = ensure_runtime_storage(runtime, error, (int)sizeof(error)) &&
                    slayer3d_storage_delete(runtime->storage, path, error, (int)sizeof(error));
    lua_pushboolean(lua, ok);
    if (!ok)
    {
        lua_pushstring(lua, error);
        return 2;
    }
    return 1;
}

static void lua_push_json_value(lua_State *lua, yyjson_val *value, int depth)
{
    if (value == NULL || depth > 64)
    {
        lua_pushnil(lua);
        return;
    }
    if (yyjson_is_null(value))
    {
        lua_pushnil(lua);
    }
    else if (yyjson_is_bool(value))
    {
        lua_pushboolean(lua, yyjson_get_bool(value));
    }
    else if (yyjson_is_int(value))
    {
        lua_pushinteger(lua, (lua_Integer)yyjson_get_sint(value));
    }
    else if (yyjson_is_num(value))
    {
        lua_pushnumber(lua, (lua_Number)yyjson_get_real(value));
    }
    else if (yyjson_is_str(value))
    {
        lua_pushlstring(lua, yyjson_get_str(value), yyjson_get_len(value));
    }
    else if (yyjson_is_arr(value))
    {
        lua_newtable(lua);
        size_t idx, max;
        yyjson_val *entry;
        yyjson_arr_foreach(value, idx, max, entry)
        {
            lua_push_json_value(lua, entry, depth + 1);
            lua_seti(lua, -2, (lua_Integer)idx + 1);
        }
    }
    else if (yyjson_is_obj(value))
    {
        lua_newtable(lua);
        size_t idx, max;
        yyjson_val *key;
        yyjson_val *val;
        yyjson_obj_foreach(value, idx, max, key, val)
        {
            lua_push_json_value(lua, val, depth + 1);
            lua_setfield(lua, -2, yyjson_get_str(key));
        }
    }
    else
    {
        lua_pushnil(lua);
    }
}

static bool lua_table_is_json_array(lua_State *lua, int index, lua_Integer *out_count)
{
    lua_Integer max_index = 0;
    lua_Integer count = 0;
    bool array = true;

    lua_pushnil(lua);
    while (lua_next(lua, index) != 0)
    {
        if (lua_isinteger(lua, -2))
        {
            const lua_Integer key = lua_tointeger(lua, -2);
            if (key <= 0)
                array = false;
            else
            {
                ++count;
                if (key > max_index)
                    max_index = key;
            }
        }
        else
        {
            array = false;
        }
        lua_pop(lua, 1);
    }

    if (out_count != NULL)
        *out_count = array && max_index == count ? count : 0;
    return array && max_index == count;
}

static yyjson_mut_val *lua_value_to_json(lua_State *lua, yyjson_mut_doc *doc, int index, int depth, char *error,
                                         int error_size)
{
    if (depth > 64)
    {
        set_error(error, error_size, "JSON value is too deeply nested");
        return NULL;
    }

    index = lua_absindex(lua, index);
    if (lua_isnoneornil(lua, index))
        return yyjson_mut_null(doc);
    if (lua_isboolean(lua, index))
        return yyjson_mut_bool(doc, lua_toboolean(lua, index));
    if (lua_isinteger(lua, index))
        return yyjson_mut_sint(doc, (int64_t)lua_tointeger(lua, index));
    if (lua_isnumber(lua, index))
        return yyjson_mut_real(doc, (double)lua_tonumber(lua, index));
    if (lua_isstring(lua, index))
    {
        size_t len = 0u;
        const char *text = lua_tolstring(lua, index, &len);
        return yyjson_mut_strncpy(doc, text, len);
    }
    if (!lua_istable(lua, index))
    {
        set_error(error, error_size, "JSON encode supports nil, bool, number, string, and table values");
        return NULL;
    }

    lua_Integer count = 0;
    if (lua_table_is_json_array(lua, index, &count))
    {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        if (arr == NULL)
            return NULL;
        for (lua_Integer i = 1; i <= count; ++i)
        {
            lua_geti(lua, index, i);
            yyjson_mut_val *item = lua_value_to_json(lua, doc, -1, depth + 1, error, error_size);
            lua_pop(lua, 1);
            if (item == NULL || !yyjson_mut_arr_add_val(arr, item))
                return NULL;
        }
        return arr;
    }

    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL)
        return NULL;

    lua_pushnil(lua);
    while (lua_next(lua, index) != 0)
    {
        if (!lua_isstring(lua, -2))
        {
            lua_pop(lua, 2);
            set_error(error, error_size, "JSON object keys must be strings");
            return NULL;
        }
        size_t key_len = 0u;
        const char *key_text = lua_tolstring(lua, -2, &key_len);
        yyjson_mut_val *key = yyjson_mut_strncpy(doc, key_text, key_len);
        yyjson_mut_val *val = lua_value_to_json(lua, doc, -1, depth + 1, error, error_size);
        lua_pop(lua, 1);
        if (key == NULL || val == NULL || !yyjson_mut_obj_add(obj, key, val))
            return NULL;
    }
    return obj;
}

static int lua_json_decode(lua_State *lua)
{
    size_t len = 0u;
    const char *text = luaL_checklstring(lua, 1, &len);
    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)(void *)(size_t)(const void *)text, len, YYJSON_READ_NOFLAG, NULL, &err);
    if (doc == NULL)
    {
        lua_pushnil(lua);
        lua_pushfstring(lua, "yyjson error %d at byte %d: %s", (int)err.code, (int)err.pos,
                        err.msg != NULL ? err.msg : "");
        return 2;
    }

    lua_push_json_value(lua, yyjson_doc_get_root(doc), 0);
    yyjson_doc_free(doc);
    return 1;
}

static int lua_json_encode(lua_State *lua)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL)
    {
        lua_pushnil(lua);
        lua_pushstring(lua, "failed to allocate JSON document");
        return 2;
    }

    char error[256];
    error[0] = '\0';
    yyjson_mut_val *root = lua_value_to_json(lua, doc, 1, 0, error, (int)sizeof(error));
    if (root == NULL)
    {
        yyjson_mut_doc_free(doc);
        lua_pushnil(lua);
        lua_pushstring(lua, error[0] != '\0' ? error : "failed to encode JSON value");
        return 2;
    }

    yyjson_mut_doc_set_root(doc, root);
    size_t len = 0u;
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &len);
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        lua_pushnil(lua);
        lua_pushstring(lua, "failed to write JSON");
        return 2;
    }

    lua_pushlstring(lua, json, len);
    free(json);
    return 1;
}

static int lua_grid_cell_to_world(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const int col = (int)luaL_checkinteger(lua, 2);
    const int row = (int)luaL_checkinteger(lua, 3);
    slayer3d_vec3 position;
    if (!grid_map_cell_to_world(map, col, row, &position))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_pushnumber(lua, position.x);
    lua_pushnumber(lua, position.y);
    lua_pushnumber(lua, position.z);
    return 3;
}

static int lua_grid_world_to_cell(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const float x = (float)luaL_checknumber(lua, 2);
    const float y = (float)luaL_checknumber(lua, 3);
    int col = 0;
    int row = 0;
    if (!grid_map_world_to_cell(map, x, y, &col, &row))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_pushinteger(lua, col);
    lua_pushinteger(lua, row);
    return 2;
}

static int lua_grid_tile(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const int col = (int)luaL_checkinteger(lua, 2);
    const int row = (int)luaL_checkinteger(lua, 3);
    const char cell = grid_map_cell(map, col, row);
    if (cell == '\0')
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_pushlstring(lua, &cell, 1);
    return 1;
}

static int lua_grid_walkable(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const int col = (int)luaL_checkinteger(lua, 2);
    const int row = (int)luaL_checkinteger(lua, 3);
    lua_pushboolean(lua, grid_map_is_walkable(map, col, row));
    return 1;
}

static void lua_push_grid_cell(lua_State *lua, const grid_map_runtime *map, int col, int row)
{
    lua_newtable(lua);
    lua_pushinteger(lua, col);
    lua_setfield(lua, -2, "col");
    lua_pushinteger(lua, row);
    lua_setfield(lua, -2, "row");
    const char cell = grid_map_cell(map, col, row);
    if (cell != '\0')
    {
        lua_pushlstring(lua, &cell, 1);
        lua_setfield(lua, -2, "tile");
    }
}

static int lua_grid_neighbors(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const int col = (int)luaL_checkinteger(lua, 2);
    const int row = (int)luaL_checkinteger(lua, 3);
    lua_newtable(lua);
    if (map == NULL)
        return 1;

    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, -1}, {0, 1}};
    int output_index = 1;
    for (size_t d = 0; d < SDL_arraysize(dirs); ++d)
    {
        int next_col = col + dirs[d][0];
        int next_row = row + dirs[d][1];
        if (!grid_map_normalize_cell(map, &next_col, &next_row) || !grid_map_is_walkable(map, next_col, next_row))
            continue;
        lua_push_grid_cell(lua, map, next_col, next_row);
        lua_rawseti(lua, -2, output_index++);
    }
    return 1;
}

static int lua_grid_next_step(lua_State *lua)
{
    const grid_map_runtime *map = find_grid_map(lua_runtime(lua), luaL_checkstring(lua, 1));
    const int start_col = (int)luaL_checkinteger(lua, 2);
    const int start_row = (int)luaL_checkinteger(lua, 3);
    const int goal_col = (int)luaL_checkinteger(lua, 4);
    const int goal_row = (int)luaL_checkinteger(lua, 5);
    int next_col = start_col;
    int next_row = start_row;
    if (!grid_map_next_step(map, start_col, start_row, goal_col, goal_row, &next_col, &next_row))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_pushinteger(lua, next_col);
    lua_pushinteger(lua, next_row);
    return 2;
}

static void lua_push_sector_nav_node(lua_State *lua, const slayer3d_game_data_sector_nav_node *node)
{
    if (node == NULL || node->name == NULL)
    {
        lua_pushnil(lua);
        return;
    }
    lua_newtable(lua);
    lua_pushstring(lua, node->name);
    lua_setfield(lua, -2, "name");
    lua_pushinteger(lua, node->sector_index);
    lua_setfield(lua, -2, "sector_index");
    lua_pushnumber(lua, (lua_Number)node->position.x);
    lua_setfield(lua, -2, "x");
    lua_pushnumber(lua, (lua_Number)node->position.y);
    lua_setfield(lua, -2, "y");
    lua_pushnumber(lua, (lua_Number)node->position.z);
    lua_setfield(lua, -2, "z");
    lua_pushnumber(lua, (lua_Number)node->position.x);
    lua_rawseti(lua, -2, 1);
    lua_pushnumber(lua, (lua_Number)node->position.y);
    lua_rawseti(lua, -2, 2);
    lua_pushnumber(lua, (lua_Number)node->position.z);
    lua_rawseti(lua, -2, 3);
}

static slayer3d_vec3 lua_vec3_args(lua_State *lua, int first_index)
{
    return slayer3d_vec3_make((float)luaL_checknumber(lua, first_index), (float)luaL_checknumber(lua, first_index + 1),
                              (float)luaL_checknumber(lua, first_index + 2));
}

static int lua_sector_nav_nearest(lua_State *lua)
{
    slayer3d_game_data_sector_nav_node node;
    const char *graph = luaL_checkstring(lua, 1);
    const slayer3d_vec3 position = lua_vec3_args(lua, 2);
    if (!slayer3d_game_data_sector_nav_nearest_node(lua_runtime(lua), graph, position, &node))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_push_sector_nav_node(lua, &node);
    return 1;
}

static int lua_sector_nav_path_available(lua_State *lua)
{
    const char *graph = luaL_checkstring(lua, 1);
    const slayer3d_vec3 start = lua_vec3_args(lua, 2);
    const slayer3d_vec3 goal = lua_vec3_args(lua, 5);
    lua_pushboolean(lua, slayer3d_game_data_sector_nav_path_available(lua_runtime(lua), graph, start, goal));
    return 1;
}

static int lua_sector_nav_next_node(lua_State *lua)
{
    slayer3d_game_data_sector_nav_node node;
    const char *graph = luaL_checkstring(lua, 1);
    const slayer3d_vec3 start = lua_vec3_args(lua, 2);
    const slayer3d_vec3 goal = lua_vec3_args(lua, 5);
    if (!slayer3d_game_data_sector_nav_next_node(lua_runtime(lua), graph, start, goal, &node))
    {
        lua_pushnil(lua);
        return 1;
    }
    lua_push_sector_nav_node(lua, &node);
    return 1;
}

static int lua_sector_nav_path(lua_State *lua)
{
    const char *graph = luaL_checkstring(lua, 1);
    const slayer3d_vec3 start = lua_vec3_args(lua, 2);
    const slayer3d_vec3 goal = lua_vec3_args(lua, 5);
    int node_count = 0;
    float cost = 0.0f;
    if (!slayer3d_game_data_sector_nav_path(lua_runtime(lua), graph, start, goal, NULL, 0, &node_count, &cost) ||
        node_count <= 0)
    {
        lua_pushnil(lua);
        return 1;
    }

    slayer3d_game_data_sector_nav_node *nodes = (slayer3d_game_data_sector_nav_node *)SDL_malloc(
        sizeof(slayer3d_game_data_sector_nav_node) * (size_t)node_count);
    if (nodes == NULL || !slayer3d_game_data_sector_nav_path(lua_runtime(lua), graph, start, goal, nodes, node_count,
                                                             &node_count, &cost))
    {
        SDL_free(nodes);
        lua_pushnil(lua);
        return 1;
    }

    lua_newtable(lua);
    for (int i = 0; i < node_count; ++i)
    {
        lua_push_sector_nav_node(lua, &nodes[i]);
        lua_rawseti(lua, -2, i + 1);
    }
    lua_pushnumber(lua, (lua_Number)cost);
    lua_setfield(lua, -2, "cost");
    SDL_free(nodes);
    return 1;
}

static void install_lua_helpers(lua_State *lua)
{
    static const char *source_parts[] = {
        "local Actor = {}\n"
        "local Vec3 = {}\n"
        "local Vec3_mt = { __index = Vec3 }\n"
        "local function as_vec3(value, fallback)\n"
        "    if value == nil then return fallback or Vec3(0, 0, 0) end\n"
        "    if getmetatable(value) == Vec3_mt then return value end\n"
        "    return Vec3(value.x or value[1] or 0, value.y or value[2] or 0, value.z or value[3] or 0)\n"
        "end\n"
        "function Vec3.new(x, y, z)\n"
        "    return setmetatable({ x = x or 0, y = y or 0, z = z or 0 }, Vec3_mt)\n"
        "end\n"
        "setmetatable(Vec3, { __call = function(_, x, y, z) return Vec3.new(x, y, z) end })\n"
        "function Vec3.length(v)\n"
        "    v = as_vec3(v)\n"
        "    return math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)\n"
        "end\n"
        "function Vec3.normalize(v)\n"
        "    v = as_vec3(v)\n"
        "    local len = Vec3.length(v)\n"
        "    if len <= 0.000001 then return Vec3(0, 0, 0) end\n"
        "    return Vec3(v.x / len, v.y / len, v.z / len)\n"
        "end\n"
        "function Vec3.clamp(v, lo, hi)\n"
        "    v = as_vec3(v)\n"
        "    return Vec3(math.clamp(v.x, lo, hi), math.clamp(v.y, lo, hi), math.clamp(v.z, lo, hi))\n"
        "end\n"
        "function Vec3_mt.__add(a, b)\n"
        "    a, b = as_vec3(a), as_vec3(b)\n"
        "    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z)\n"
        "end\n"
        "function Vec3_mt.__sub(a, b)\n"
        "    a, b = as_vec3(a), as_vec3(b)\n"
        "    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z)\n"
        "end\n"
        "function Vec3_mt.__mul(a, b)\n"
        "    if type(a) == 'number' then return Vec3(a * b.x, a * b.y, a * b.z) end\n"
        "    if type(b) == 'number' then return Vec3(a.x * b, a.y * b, a.z * b) end\n"
        "    return Vec3(a.x * b.x, a.y * b.y, a.z * b.z)\n"
        "end\n"
        "function math.clamp(value, lo, hi)\n"
        "    if value < lo then return lo end\n"
        "    if value > hi then return hi end\n"
        "    return value\n"
        "end\n"
        "function math.lerp(a, b, t)\n"
        "    return a + (b - a) * t\n"
        "end\n",
        "function Actor:get_float(key, fallback) return slayer3d.get_float(self, key, fallback or 0) end\n"
        "function Actor:set_float(key, value) slayer3d.set_float(self, key, value) end\n"
        "function Actor:get_int(key, fallback) return slayer3d.get_int(self, key, fallback or 0) end\n"
        "function Actor:set_int(key, value) slayer3d.set_int(self, key, value) end\n"
        "function Actor:get_bool(key, fallback) return slayer3d.get_bool(self, key, fallback or false) end\n"
        "function Actor:set_bool(key, value) slayer3d.set_bool(self, key, value and true or false) end\n"
        "function Actor:get_string(key, fallback) return slayer3d.get_string(self, key, fallback or '') end\n"
        "function Actor:set_string(key, value) slayer3d.set_string(self, key, value or '') end\n"
        "function Actor:get_vec3(key, fallback)\n"
        "    local x, y, z = slayer3d.get_vec3(self, key)\n"
        "    if x == nil then return fallback end\n"
        "    return Vec3(x, y, z)\n"
        "end\n"
        "function Actor:set_vec3(key, value)\n"
        "    value = as_vec3(value)\n"
        "    slayer3d.set_vec3(self, key, value.x, value.y, value.z)\n"
        "end\n"
        "function Actor:get_position()\n"
        "    local x, y, z = slayer3d.get_position(self)\n"
        "    if x == nil then return nil end\n"
        "    return Vec3(x, y, z)\n"
        "end\n"
        "function Actor:set_position(value)\n"
        "    value = as_vec3(value)\n"
        "    slayer3d.set_position(self, value.x, value.y, value.z)\n"
        "end\n"
        "Actor.__index = function(self, key)\n"
        "    if key == 'name' then return rawget(self, '_name') end\n"
        "    if key == 'active' then return slayer3d.actor_active(self) end\n"
        "    if key == 'position' then return Actor.get_position(self) end\n"
        "    if key == 'velocity' then return Actor.get_vec3(self, 'velocity', Vec3(0, 0, 0)) end\n"
        "    return Actor[key]\n"
        "end\n"
        "Actor.__newindex = function(self, key, value)\n"
        "    if key == 'position' then Actor.set_position(self, value); return end\n"
        "    if key == 'velocity' then Actor.set_vec3(self, 'velocity', value); return end\n"
        "    rawset(self, key, value)\n"
        "end\n",
        "function slayer3d.actor(name)\n"
        "    if name == nil or name == '' then return nil end\n"
        "    return setmetatable({ _name = name }, Actor)\n"
        "end\n"
        "function Actor:is_active() return slayer3d.actor_active(self) end\n"
        "function Actor:despawn() return slayer3d.despawn(self) end\n"
        "function slayer3d._context(adapter, dt)\n"
        "    return {\n"
        "        adapter = adapter,\n"
        "        name = adapter,\n"
        "        dt = dt or 0,\n"
        "        actor = function(self_or_name, maybe_name) return slayer3d.actor(maybe_name or self_or_name) end,\n"
        "        spawn = function(self_or_pool, maybe_pool, maybe_options)\n"
        "            if type(self_or_pool) == 'table' and self_or_pool.adapter ~= nil then\n"
        "                return slayer3d.spawn(maybe_pool, maybe_options)\n"
        "            end\n"
        "            return slayer3d.spawn(self_or_pool, maybe_pool)\n"
        "        end,\n"
        "        despawn = function(self_or_actor, maybe_actor, maybe_reason)\n"
        "            if type(self_or_actor) == 'table' and self_or_actor.adapter ~= nil then\n"
        "                return slayer3d.despawn(maybe_actor, maybe_reason)\n"
        "            end\n"
        "            return slayer3d.despawn(self_or_actor, maybe_actor)\n"
        "        end,\n"
        "        despawn_by_tag = function(self_or_tag, maybe_tag, maybe_reason)\n"
        "            if type(self_or_tag) == 'table' and self_or_tag.adapter ~= nil then\n"
        "                return slayer3d.despawn_by_tag(maybe_tag, maybe_reason)\n"
        "            end\n"
        "            return slayer3d.despawn_by_tag(self_or_tag, maybe_tag)\n"
        "        end,\n"
        "        actor_with_tags = function(self_or_tag, maybe_tag, ...)\n"
        "            if type(self_or_tag) == 'table' and self_or_tag.adapter ~= nil then\n"
        "                return slayer3d.actor_with_tags(maybe_tag, ...)\n"
        "            end\n"
        "            if type(self_or_tag) == 'table' then return slayer3d.actor_with_tags(self_or_tag) end\n"
        "            return slayer3d.actor_with_tags(self_or_tag, maybe_tag, ...)\n"
        "        end,\n",
        "        active_actors_with_tags = function(self_or_tag, maybe_tag, ...)\n"
        "            if type(self_or_tag) == 'table' and self_or_tag.adapter ~= nil then\n"
        "                return slayer3d.active_actors_with_tags(maybe_tag, ...)\n"
        "            end\n"
        "            if type(self_or_tag) == 'table' then return slayer3d.active_actors_with_tags(self_or_tag) end\n"
        "            return slayer3d.active_actors_with_tags(self_or_tag, maybe_tag, ...)\n"
        "        end,\n"
        "        pool_capacity = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_capacity(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_active_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_active_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_available_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_available_count(maybe_pool or self_or_pool)\n"
        "        end,\n",
        "        pool_peak_active_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_peak_active_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_spawn_attempt_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_spawn_attempt_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_spawn_success_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_spawn_success_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_spawn_failure_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_spawn_failure_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_exhaustion_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_exhaustion_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_reuse_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_reuse_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_despawn_count = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_despawn_count(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_last_spawn_failure_reason = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_last_spawn_failure_reason(maybe_pool or self_or_pool)\n"
        "        end,\n"
        "        pool_last_despawn_reason = function(self_or_pool, maybe_pool)\n"
        "            return slayer3d.pool_last_despawn_reason(maybe_pool or self_or_pool)\n"
        "        end,\n",
        "        grid_cell_to_world = function(self_or_map, maybe_map, maybe_col, maybe_row)\n"
        "            local map, col, row\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                map, col, row = maybe_map, maybe_col, maybe_row\n"
        "            else\n"
        "                map, col, row = self_or_map, maybe_map, maybe_col\n"
        "            end\n"
        "            local x, y, z = slayer3d.grid_cell_to_world(map, col, row)\n"
        "            if x == nil then return nil end\n"
        "            return Vec3(x, y, z)\n"
        "        end,\n"
        "        grid_world_to_cell = function(self_or_map, maybe_map, maybe_position)\n"
        "            local map, position\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                map, position = maybe_map, maybe_position\n"
        "            else\n"
        "                map, position = self_or_map, maybe_map\n"
        "            end\n"
        "            position = as_vec3(position)\n"
        "            local col, row = slayer3d.grid_world_to_cell(map, position.x, position.y)\n"
        "            if col == nil then return nil end\n"
        "            return { col = col, row = row }\n"
        "        end,\n"
        "        grid_tile = function(self_or_map, maybe_map, maybe_col, maybe_row)\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                return slayer3d.grid_tile(maybe_map, maybe_col, maybe_row)\n"
        "            end\n"
        "            return slayer3d.grid_tile(self_or_map, maybe_map, maybe_col)\n"
        "        end,\n"
        "        grid_walkable = function(self_or_map, maybe_map, maybe_col, maybe_row)\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                return slayer3d.grid_walkable(maybe_map, maybe_col, maybe_row)\n"
        "            end\n"
        "            return slayer3d.grid_walkable(self_or_map, maybe_map, maybe_col)\n"
        "        end,\n"
        "        grid_neighbors = function(self_or_map, maybe_map, maybe_col, maybe_row)\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                return slayer3d.grid_neighbors(maybe_map, maybe_col, maybe_row)\n"
        "            end\n"
        "            return slayer3d.grid_neighbors(self_or_map, maybe_map, maybe_col)\n"
        "        end,\n"
        "        grid_next_step = function(self_or_map, maybe_map, maybe_start_col, maybe_start_row, maybe_goal_col, "
        "maybe_goal_row)\n"
        "            local map, start_col, start_row, goal_col, goal_row\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                map, start_col, start_row, goal_col, goal_row = maybe_map, maybe_start_col, maybe_start_row, "
        "maybe_goal_col, maybe_goal_row\n"
        "            else\n"
        "                map, start_col, start_row, goal_col, goal_row = self_or_map, maybe_map, maybe_start_col, "
        "maybe_start_row, maybe_goal_col\n"
        "            end\n"
        "            local col, row = slayer3d.grid_next_step(map, start_col, start_row, goal_col, goal_row)\n"
        "            if col == nil then return nil end\n"
        "            return { col = col, row = row }\n"
        "        end,\n",
        "        grid_actor_at = function(self_or_map, maybe_map, maybe_pool, maybe_col, maybe_row)\n"
        "            local map, pool, col, row\n"
        "            if type(self_or_map) == 'table' and self_or_map.adapter ~= nil then\n"
        "                map, pool, col, row = maybe_map, maybe_pool, maybe_col, maybe_row\n"
        "            else\n"
        "                map, pool, col, row = self_or_map, maybe_map, maybe_pool, maybe_col\n"
        "            end\n"
        "            return slayer3d.grid_actor_at(map, pool, col, row)\n"
        "        end,\n",
        "        grid_pickup_at = function(self_or_layer, maybe_layer, maybe_col, maybe_row)\n"
        "            if type(self_or_layer) == 'table' and self_or_layer.adapter ~= nil then\n"
        "                return slayer3d.grid_pickup_at(maybe_layer, maybe_col, maybe_row)\n"
        "            end\n"
        "            return slayer3d.grid_pickup_at(self_or_layer, maybe_layer, maybe_col)\n"
        "        end,\n"
        "        grid_collect_at = function(self_or_layer, maybe_layer, maybe_col, maybe_row)\n"
        "            if type(self_or_layer) == 'table' and self_or_layer.adapter ~= nil then\n"
        "                return slayer3d.grid_collect_at(maybe_layer, maybe_col, maybe_row)\n"
        "            end\n"
        "            return slayer3d.grid_collect_at(self_or_layer, maybe_layer, maybe_col)\n"
        "        end,\n"
        "        grid_pickup_count = function(self_or_layer, maybe_layer)\n"
        "            return slayer3d.grid_pickup_count(maybe_layer or self_or_layer)\n"
        "        end,\n",
        "        sector_nav_nearest = function(self_or_graph, maybe_graph, maybe_position)\n"
        "            local graph, position\n"
        "            if type(self_or_graph) == 'table' and self_or_graph.adapter ~= nil then\n"
        "                graph, position = maybe_graph, maybe_position\n"
        "            else\n"
        "                graph, position = self_or_graph, maybe_graph\n"
        "            end\n"
        "            if type(position) == 'table' and position.position ~= nil then position = position.position end\n"
        "            position = as_vec3(position)\n"
        "            return slayer3d.sector_nav_nearest(graph, position.x, position.y, position.z)\n"
        "        end,\n"
        "        sector_nav_path_available = function(self_or_graph, maybe_graph, maybe_start, maybe_goal)\n"
        "            local graph, start, goal\n"
        "            if type(self_or_graph) == 'table' and self_or_graph.adapter ~= nil then\n"
        "                graph, start, goal = maybe_graph, maybe_start, maybe_goal\n"
        "            else\n"
        "                graph, start, goal = self_or_graph, maybe_graph, maybe_start\n"
        "            end\n"
        "            if type(start) == 'table' and start.position ~= nil then start = start.position end\n"
        "            if type(goal) == 'table' and goal.position ~= nil then goal = goal.position end\n"
        "            start, goal = as_vec3(start), as_vec3(goal)\n"
        "            return slayer3d.sector_nav_path_available(graph, start.x, start.y, start.z, goal.x, goal.y, "
        "goal.z)\n"
        "        end,\n"
        "        sector_nav_next_node = function(self_or_graph, maybe_graph, maybe_start, maybe_goal)\n"
        "            local graph, start, goal\n"
        "            if type(self_or_graph) == 'table' and self_or_graph.adapter ~= nil then\n"
        "                graph, start, goal = maybe_graph, maybe_start, maybe_goal\n"
        "            else\n"
        "                graph, start, goal = self_or_graph, maybe_graph, maybe_start\n"
        "            end\n"
        "            if type(start) == 'table' and start.position ~= nil then start = start.position end\n"
        "            if type(goal) == 'table' and goal.position ~= nil then goal = goal.position end\n"
        "            start, goal = as_vec3(start), as_vec3(goal)\n"
        "            return slayer3d.sector_nav_next_node(graph, start.x, start.y, start.z, goal.x, goal.y, goal.z)\n"
        "        end,\n"
        "        sector_nav_path = function(self_or_graph, maybe_graph, maybe_start, maybe_goal)\n"
        "            local graph, start, goal\n"
        "            if type(self_or_graph) == 'table' and self_or_graph.adapter ~= nil then\n"
        "                graph, start, goal = maybe_graph, maybe_start, maybe_goal\n"
        "            else\n"
        "                graph, start, goal = self_or_graph, maybe_graph, maybe_start\n"
        "            end\n"
        "            if type(start) == 'table' and start.position ~= nil then start = start.position end\n"
        "            if type(goal) == 'table' and goal.position ~= nil then goal = goal.position end\n"
        "            start, goal = as_vec3(start), as_vec3(goal)\n"
        "            return slayer3d.sector_nav_path(graph, start.x, start.y, start.z, goal.x, goal.y, goal.z)\n"
        "        end,\n",
        "        state_get = function(self_or_key, maybe_key, fallback)\n"
        "            if type(self_or_key) == 'table' and self_or_key.adapter ~= nil then\n"
        "                return slayer3d.state_get(maybe_key, fallback)\n"
        "            end\n"
        "            return slayer3d.state_get(self_or_key, maybe_key)\n"
        "        end,\n"
        "        state_set = function(self_or_key, maybe_key, maybe_value)\n"
        "            if type(self_or_key) == 'table' and self_or_key.adapter ~= nil then\n"
        "                slayer3d.state_set(maybe_key, maybe_value); return\n"
        "            end\n"
        "            slayer3d.state_set(self_or_key, maybe_key)\n"
        "        end,\n"
        "        random = function(_) return slayer3d.random() end,\n"
        "        log = function(self_or_message, maybe_message) slayer3d.log(maybe_message or self_or_message) end,\n"
        "        storage = slayer3d.storage,\n"
        "    }\n"
        "end\n"
        "slayer3d.storage = {\n"
        "    read = slayer3d.storage_read,\n"
        "    write = slayer3d.storage_write,\n"
        "    exists = slayer3d.storage_exists,\n"
        "    mkdir = slayer3d.storage_mkdir,\n"
        "    delete = slayer3d.storage_delete,\n"
        "}\n"
        "slayer3d.json = {\n"
        "    decode = slayer3d.json_decode,\n"
        "    encode = slayer3d.json_encode,\n"
        "}\n"
        "slayer3d.Actor = Actor\n"
        "slayer3d.Vec3 = Vec3\n"
        "slayer3d.api = 'slayer3d.lua.v1'\n"
        "_G.Vec3 = Vec3\n",
    };

    size_t source_len = 0;
    for (size_t i = 0; i < SDL_arraysize(source_parts); ++i)
        source_len += SDL_strlen(source_parts[i]);

    char *source = (char *)SDL_malloc(source_len + 1u);
    if (source == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[lua] failed to allocate gameplay API source");
        return;
    }

    size_t offset = 0;
    for (size_t i = 0; i < SDL_arraysize(source_parts); ++i)
    {
        const size_t part_len = SDL_strlen(source_parts[i]);
        SDL_memcpy(source + offset, source_parts[i], part_len);
        offset += part_len;
    }
    source[offset] = '\0';

    if (luaL_dostring(lua, source) != LUA_OK)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[lua] failed to install gameplay API: %s", lua_tostring(lua, -1));
        lua_pop(lua, 1);
    }
    SDL_free(source);
}

void register_lua_api(slayer3d_game_data_runtime *runtime, slayer3d_script_engine *engine)
{
    if (runtime == NULL || engine == NULL)
        return;

    lua_State *lua = slayer3d_script_engine_lua_state(engine);
    if (lua == NULL)
        return;

    lua_newtable(lua);
#define SLAYER3D_LUA_BIND(name, fn)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        lua_pushlightuserdata(lua, runtime);                                                                           \
        lua_pushcclosure(lua, (fn), 1);                                                                                \
        lua_setfield(lua, -2, (name));                                                                                 \
    } while (0)
    SLAYER3D_LUA_BIND("get_position", lua_get_position);
    SLAYER3D_LUA_BIND("set_position", lua_set_position);
    SLAYER3D_LUA_BIND("get_float", lua_get_float);
    SLAYER3D_LUA_BIND("set_float", lua_set_float);
    SLAYER3D_LUA_BIND("get_int", lua_get_int);
    SLAYER3D_LUA_BIND("set_int", lua_set_int);
    SLAYER3D_LUA_BIND("get_bool", lua_get_bool);
    SLAYER3D_LUA_BIND("set_bool", lua_set_bool);
    SLAYER3D_LUA_BIND("get_string", lua_get_string);
    SLAYER3D_LUA_BIND("set_string", lua_set_string);
    SLAYER3D_LUA_BIND("get_vec3", lua_get_vec3);
    SLAYER3D_LUA_BIND("set_vec3", lua_set_vec3);
    SLAYER3D_LUA_BIND("actor_active", lua_actor_active);
    SLAYER3D_LUA_BIND("dt", lua_get_dt);
    SLAYER3D_LUA_BIND("state_get", lua_state_get);
    SLAYER3D_LUA_BIND("state_set", lua_state_set);
    SLAYER3D_LUA_BIND("random", lua_random);
    SLAYER3D_LUA_BIND("actor_with_tags", lua_actor_with_tags);
    SLAYER3D_LUA_BIND("active_actors_with_tags", lua_active_actors_with_tags);
    SLAYER3D_LUA_BIND("spawn", lua_spawn_actor);
    SLAYER3D_LUA_BIND("despawn", lua_despawn_actor);
    SLAYER3D_LUA_BIND("despawn_by_tag", lua_despawn_actors_by_tag);
    SLAYER3D_LUA_BIND("pool_capacity", lua_pool_capacity);
    SLAYER3D_LUA_BIND("pool_active_count", lua_pool_active_count);
    SLAYER3D_LUA_BIND("pool_available_count", lua_pool_available_count);
    SLAYER3D_LUA_BIND("pool_peak_active_count", lua_pool_peak_active_count);
    SLAYER3D_LUA_BIND("pool_spawn_attempt_count", lua_pool_spawn_attempt_count);
    SLAYER3D_LUA_BIND("pool_spawn_success_count", lua_pool_spawn_success_count);
    SLAYER3D_LUA_BIND("pool_spawn_failure_count", lua_pool_spawn_failure_count);
    SLAYER3D_LUA_BIND("pool_exhaustion_count", lua_pool_exhaustion_count);
    SLAYER3D_LUA_BIND("pool_reuse_count", lua_pool_reuse_count);
    SLAYER3D_LUA_BIND("pool_despawn_count", lua_pool_despawn_count);
    SLAYER3D_LUA_BIND("pool_last_spawn_failure_reason", lua_pool_last_spawn_failure_reason);
    SLAYER3D_LUA_BIND("pool_last_despawn_reason", lua_pool_last_despawn_reason);
    SLAYER3D_LUA_BIND("grid_cell_to_world", lua_grid_cell_to_world);
    SLAYER3D_LUA_BIND("grid_world_to_cell", lua_grid_world_to_cell);
    SLAYER3D_LUA_BIND("grid_tile", lua_grid_tile);
    SLAYER3D_LUA_BIND("grid_walkable", lua_grid_walkable);
    SLAYER3D_LUA_BIND("grid_neighbors", lua_grid_neighbors);
    SLAYER3D_LUA_BIND("grid_next_step", lua_grid_next_step);
    SLAYER3D_LUA_BIND("grid_actor_at", lua_grid_actor_at);
    SLAYER3D_LUA_BIND("grid_pickup_at", lua_grid_pickup_at);
    SLAYER3D_LUA_BIND("grid_collect_at", lua_grid_collect_at);
    SLAYER3D_LUA_BIND("grid_pickup_count", lua_grid_pickup_count);
    SLAYER3D_LUA_BIND("sector_nav_nearest", lua_sector_nav_nearest);
    SLAYER3D_LUA_BIND("sector_nav_path_available", lua_sector_nav_path_available);
    SLAYER3D_LUA_BIND("sector_nav_next_node", lua_sector_nav_next_node);
    SLAYER3D_LUA_BIND("sector_nav_path", lua_sector_nav_path);
    SLAYER3D_LUA_BIND("log", lua_log);
    SLAYER3D_LUA_BIND("storage_read", lua_storage_read);
    SLAYER3D_LUA_BIND("storage_write", lua_storage_write);
    SLAYER3D_LUA_BIND("storage_exists", lua_storage_exists);
    SLAYER3D_LUA_BIND("storage_mkdir", lua_storage_mkdir);
    SLAYER3D_LUA_BIND("storage_delete", lua_storage_delete);
    SLAYER3D_LUA_BIND("json_decode", lua_json_decode);
    SLAYER3D_LUA_BIND("json_encode", lua_json_encode);
#undef SLAYER3D_LUA_BIND
    lua_setglobal(lua, "slayer3d");
    install_lua_helpers(lua);
}

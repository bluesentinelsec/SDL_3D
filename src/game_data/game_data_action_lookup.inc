/* Signal and action lookup helpers. Included by src/game_data.c to preserve internal linkage. */

int slayer3d_game_data_find_signal(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return -1;
    for (int i = 0; i < runtime->signal_count; ++i)
    {
        if (SDL_strcmp(runtime->signals[i].name, name) == 0)
            return runtime->signals[i].id;
    }
    return -1;
}

int slayer3d_game_data_find_action(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return -1;
    for (int i = 0; i < runtime->action_count; ++i)
    {
        if (SDL_strcmp(runtime->actions[i].name, name) == 0)
            return runtime->actions[i].id;
    }
    return -1;
}

static const char *find_action_name(const slayer3d_game_data_runtime *runtime, int action_id)
{
    if (runtime == NULL || action_id < 0)
        return NULL;
    for (int i = 0; i < runtime->action_count; ++i)
    {
        if (runtime->actions[i].id == action_id)
            return runtime->actions[i].name;
    }
    return NULL;
}

slayer3d_registered_actor *slayer3d_game_data_find_actor(const slayer3d_game_data_runtime *runtime, const char *name)
{
    return slayer3d_actor_registry_find(runtime_registry(runtime), name);
}

slayer3d_registered_actor *slayer3d_game_data_find_actor_with_tag(const slayer3d_game_data_runtime *runtime,
                                                                  const char *tag)
{
    const char *tags[1] = {tag};
    return slayer3d_game_data_find_actor_with_tags(runtime, tags, 1);
}

slayer3d_registered_actor *slayer3d_game_data_find_actor_with_tags(const slayer3d_game_data_runtime *runtime,
                                                                   const char *const *tags, int tag_count)
{
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    if (runtime == NULL || tags == NULL || tag_count <= 0 || !yyjson_is_arr(entities))
        return NULL;

    for (size_t i = 0; i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        if (!entity_json_has_tags(entity, tags, tag_count))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(entity, "name", NULL));
        if (actor != NULL)
            return actor;
    }
    return NULL;
}

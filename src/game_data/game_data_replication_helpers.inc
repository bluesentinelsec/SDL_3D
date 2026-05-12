/* Network replication and control codec helpers. Included by src/game_data.c to preserve internal linkage. */

static yyjson_val *game_data_find_replication_channel_by_name(const slayer3d_game_data_runtime *runtime,
                                                              const char *replication_name, int *out_index)
{
    yyjson_val *replication = obj_get(obj_get(runtime_root(runtime), "network"), "replication");
    for (size_t i = 0; yyjson_is_arr(replication) && i < yyjson_arr_size(replication); ++i)
    {
        yyjson_val *channel = yyjson_arr_get(replication, i);
        if (SDL_strcmp(json_string(channel, "name", ""), replication_name != NULL ? replication_name : "") == 0)
        {
            if (out_index != NULL)
                *out_index = (int)i;
            return channel;
        }
    }
    return NULL;
}

static yyjson_val *game_data_find_replication_channel_by_index(const slayer3d_game_data_runtime *runtime, Uint32 index)
{
    yyjson_val *replication = obj_get(obj_get(runtime_root(runtime), "network"), "replication");
    return yyjson_is_arr(replication) && index < yyjson_arr_size(replication) ? yyjson_arr_get(replication, index)
                                                                              : NULL;
}

static bool game_data_replication_channel_is_host_to_client(yyjson_val *channel)
{
    return SDL_strcmp(json_string(channel, "direction", ""), "host_to_client") == 0;
}

static bool game_data_replication_channel_is_client_to_host(yyjson_val *channel)
{
    return SDL_strcmp(json_string(channel, "direction", ""), "client_to_host") == 0;
}

static size_t game_data_replication_field_array_count(yyjson_val *fields)
{
    return yyjson_is_arr(fields) ? yyjson_arr_size(fields) : 0U;
}

static size_t game_data_replication_channel_field_count(const slayer3d_game_data_runtime *runtime, yyjson_val *channel)
{
    size_t count = 0U;
    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t i = 0; yyjson_is_arr(actors) && i < yyjson_arr_size(actors); ++i)
    {
        count += game_data_replication_field_array_count(obj_get(yyjson_arr_get(actors, i), "fields"));
    }

    yyjson_val *pools = obj_get(channel, "pools");
    for (size_t i = 0; yyjson_is_arr(pools) && i < yyjson_arr_size(pools); ++i)
    {
        yyjson_val *pool_schema = yyjson_arr_get(pools, i);
        const actor_pool_runtime *pool = find_actor_pool_const(runtime, json_string(pool_schema, "pool", NULL));
        if (pool != NULL && pool->capacity > 0)
            count += (size_t)pool->capacity * game_data_replication_field_array_count(obj_get(pool_schema, "fields"));
    }
    return count;
}

static bool game_data_add_replication_fields_packet_size(yyjson_val *fields, size_t multiplier, size_t *size)
{
    if (size == NULL)
        return false;

    for (size_t repeat = 0U; repeat < multiplier; ++repeat)
    {
        for (size_t field_index = 0U; yyjson_is_arr(fields) && field_index < yyjson_arr_size(fields); ++field_index)
        {
            slayer3d_replication_field_descriptor field;
            if (!slayer3d_replication_field_descriptor_from_json(yyjson_arr_get(fields, field_index), &field))
                return false;
            const size_t field_size = slayer3d_replication_field_wire_size(field.type);
            if (field_size == 0U || *size > SIZE_MAX - 1U - field_size)
                return false;
            *size += 1U + field_size;
        }
    }
    return true;
}

static bool game_data_replication_channel_packet_size(const slayer3d_game_data_runtime *runtime, yyjson_val *channel,
                                                      size_t *out_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (channel == NULL)
        return false;

    size_t size = 4U + 4U + 4U + 4U + SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE + 4U;
    yyjson_val *actors = obj_get(channel, "actors");
    for (size_t actor_index = 0U; yyjson_is_arr(actors) && actor_index < yyjson_arr_size(actors); ++actor_index)
    {
        if (!game_data_add_replication_fields_packet_size(obj_get(yyjson_arr_get(actors, actor_index), "fields"), 1U,
                                                          &size))
            return false;
    }

    yyjson_val *pools = obj_get(channel, "pools");
    for (size_t pool_index = 0U; yyjson_is_arr(pools) && pool_index < yyjson_arr_size(pools); ++pool_index)
    {
        yyjson_val *pool_schema = yyjson_arr_get(pools, pool_index);
        const actor_pool_runtime *pool = find_actor_pool_const(runtime, json_string(pool_schema, "pool", NULL));
        if (pool == NULL || pool->capacity < 0)
            return false;
        if (!game_data_add_replication_fields_packet_size(obj_get(pool_schema, "fields"), (size_t)pool->capacity,
                                                          &size))
            return false;
    }

    if (out_size != NULL)
        *out_size = size;
    return true;
}

static size_t game_data_replication_channel_input_count(yyjson_val *channel)
{
    yyjson_val *inputs = obj_get(channel, "inputs");
    return yyjson_is_arr(inputs) ? yyjson_arr_size(inputs) : 0U;
}

static bool game_data_replication_input_packet_size(yyjson_val *channel, size_t *out_size)
{
    if (out_size != NULL)
        *out_size = 0U;
    if (channel == NULL)
        return false;

    const size_t input_count = game_data_replication_channel_input_count(channel);
    const size_t input_value_size = 1U + slayer3d_replication_field_wire_size(SLAYER3D_REPLICATION_FIELD_FLOAT32);
    size_t size = 4U + 4U + 4U + 4U + SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE + 4U;
    if (input_value_size == 1U || input_count > (SIZE_MAX - size) / input_value_size)
        return false;
    size += input_count * input_value_size;

    if (out_size != NULL)
        *out_size = size;
    return true;
}

static const char *game_data_replication_input_action(yyjson_val *input)
{
    return json_string(input, "action", NULL);
}

static int game_data_replication_action_id(const slayer3d_game_data_runtime *runtime, yyjson_val *input)
{
    return slayer3d_game_data_find_action(runtime, game_data_replication_input_action(input));
}

static slayer3d_game_data_network_direction game_data_network_direction_from_string(const char *direction)
{
    if (direction == NULL)
        return SLAYER3D_GAME_DATA_NETWORK_DIRECTION_INVALID;
    if (SDL_strcmp(direction, "host_to_client") == 0)
        return SLAYER3D_GAME_DATA_NETWORK_DIRECTION_HOST_TO_CLIENT;
    if (SDL_strcmp(direction, "client_to_host") == 0)
        return SLAYER3D_GAME_DATA_NETWORK_DIRECTION_CLIENT_TO_HOST;
    if (SDL_strcmp(direction, "bidirectional") == 0)
        return SLAYER3D_GAME_DATA_NETWORK_DIRECTION_BIDIRECTIONAL;
    return SLAYER3D_GAME_DATA_NETWORK_DIRECTION_INVALID;
}

static const char *game_data_network_direction_name(slayer3d_game_data_network_direction direction)
{
    switch (direction)
    {
    case SLAYER3D_GAME_DATA_NETWORK_DIRECTION_HOST_TO_CLIENT:
        return "host_to_client";
    case SLAYER3D_GAME_DATA_NETWORK_DIRECTION_CLIENT_TO_HOST:
        return "client_to_host";
    case SLAYER3D_GAME_DATA_NETWORK_DIRECTION_BIDIRECTIONAL:
        return "bidirectional";
    default:
        return "invalid";
    }
}

static yyjson_val *game_data_find_network_control_by_name(const slayer3d_game_data_runtime *runtime,
                                                          const char *control_name, int *out_index)
{
    yyjson_val *controls = obj_get(obj_get(runtime_root(runtime), "network"), "control_messages");
    for (size_t i = 0; yyjson_is_arr(controls) && i < yyjson_arr_size(controls); ++i)
    {
        yyjson_val *control = yyjson_arr_get(controls, i);
        if (SDL_strcmp(json_string(control, "name", ""), control_name != NULL ? control_name : "") == 0)
        {
            if (out_index != NULL)
                *out_index = (int)i;
            return control;
        }
    }
    return NULL;
}

static yyjson_val *game_data_find_network_control_by_index(const slayer3d_game_data_runtime *runtime, Uint32 index)
{
    yyjson_val *controls = obj_get(obj_get(runtime_root(runtime), "network"), "control_messages");
    return yyjson_is_arr(controls) && index < yyjson_arr_size(controls) ? yyjson_arr_get(controls, index) : NULL;
}

static bool game_data_network_control_packet_size(size_t *out_size)
{
    const size_t size = 4U + 4U + 4U + 4U + SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

static int game_data_network_control_signal_id(const slayer3d_game_data_runtime *runtime, yyjson_val *control)
{
    return slayer3d_game_data_find_signal(runtime, json_string(control, "signal", NULL));
}

static const char *game_data_replication_property_key(const char *path)
{
    static const char prefix[] = "properties.";
    const size_t prefix_len = sizeof(prefix) - 1U;
    return path != NULL && SDL_strncmp(path, prefix, prefix_len) == 0 ? path + prefix_len : NULL;
}

static bool game_data_read_actor_replication_field(const slayer3d_registered_actor *actor,
                                                   const slayer3d_replication_field_descriptor *field,
                                                   game_data_snapshot_value *out_value)
{
    if (actor == NULL || field == NULL || out_value == NULL || field->path == NULL)
        return false;

    out_value->type = field->type;
    if (SDL_strcmp(field->path, "active") == 0)
    {
        if (field->type != SLAYER3D_REPLICATION_FIELD_BOOL)
            return false;
        out_value->value.as_bool = actor->active;
        return true;
    }
    if (SDL_strcmp(field->path, "position") == 0)
    {
        if (field->type != SLAYER3D_REPLICATION_FIELD_VEC3)
            return false;
        out_value->value.as_vec3 = actor->position;
        return true;
    }
    if (SDL_strcmp(field->path, "rotation") == 0 || SDL_strcmp(field->path, "scale") == 0)
    {
        if (field->type != SLAYER3D_REPLICATION_FIELD_VEC3)
            return false;
        out_value->value.as_vec3 =
            slayer3d_properties_get_vec3(actor->props, field->path, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        return true;
    }

    const char *key = game_data_replication_property_key(field->path);
    const slayer3d_value *property = key != NULL ? slayer3d_properties_get_value(actor->props, key) : NULL;
    if (property == NULL)
        return false;

    switch (field->type)
    {
    case SLAYER3D_REPLICATION_FIELD_BOOL:
        if (property->type != SLAYER3D_VALUE_BOOL)
            return false;
        out_value->value.as_bool = property->as_bool;
        return true;
    case SLAYER3D_REPLICATION_FIELD_INT32:
    case SLAYER3D_REPLICATION_FIELD_ENUM_ID:
        if (property->type != SLAYER3D_VALUE_INT)
            return false;
        out_value->value.as_int32 = (Sint32)property->as_int;
        return true;
    case SLAYER3D_REPLICATION_FIELD_FLOAT32:
        if (property->type != SLAYER3D_VALUE_FLOAT)
            return false;
        out_value->value.as_float32 = property->as_float;
        return true;
    case SLAYER3D_REPLICATION_FIELD_VEC2:
        if (property->type != SLAYER3D_VALUE_VEC3)
            return false;
        out_value->value.as_vec2 = (slayer3d_vec2){property->as_vec3.x, property->as_vec3.y};
        return true;
    case SLAYER3D_REPLICATION_FIELD_VEC3:
        if (property->type != SLAYER3D_VALUE_VEC3)
            return false;
        out_value->value.as_vec3 = property->as_vec3;
        return true;
    default:
        return false;
    }
}

static bool game_data_write_snapshot_value(slayer3d_replication_writer *writer, const game_data_snapshot_value *value)
{
    if (writer == NULL || value == NULL || !slayer3d_replication_write_field_type(writer, value->type))
        return false;

    switch (value->type)
    {
    case SLAYER3D_REPLICATION_FIELD_BOOL:
        return slayer3d_replication_write_bool(writer, value->value.as_bool);
    case SLAYER3D_REPLICATION_FIELD_INT32:
        return slayer3d_replication_write_int32(writer, value->value.as_int32);
    case SLAYER3D_REPLICATION_FIELD_FLOAT32:
        return slayer3d_replication_write_float32(writer, value->value.as_float32);
    case SLAYER3D_REPLICATION_FIELD_ENUM_ID:
        return slayer3d_replication_write_enum_id(writer, value->value.as_int32);
    case SLAYER3D_REPLICATION_FIELD_VEC2:
        return slayer3d_replication_write_vec2(writer, value->value.as_vec2);
    case SLAYER3D_REPLICATION_FIELD_VEC3:
        return slayer3d_replication_write_vec3(writer, value->value.as_vec3);
    default:
        return false;
    }
}

static bool game_data_read_snapshot_value(slayer3d_replication_reader *reader,
                                          slayer3d_replication_field_type expected_type,
                                          game_data_snapshot_value *out_value)
{
    if (reader == NULL || out_value == NULL)
        return false;

    slayer3d_replication_field_type packet_type = SLAYER3D_REPLICATION_FIELD_BOOL;
    if (!slayer3d_replication_read_field_type(reader, &packet_type) || packet_type != expected_type)
        return false;

    out_value->type = expected_type;
    switch (expected_type)
    {
    case SLAYER3D_REPLICATION_FIELD_BOOL:
        return slayer3d_replication_read_bool(reader, &out_value->value.as_bool);
    case SLAYER3D_REPLICATION_FIELD_INT32:
        return slayer3d_replication_read_int32(reader, &out_value->value.as_int32);
    case SLAYER3D_REPLICATION_FIELD_FLOAT32:
        return slayer3d_replication_read_float32(reader, &out_value->value.as_float32);
    case SLAYER3D_REPLICATION_FIELD_ENUM_ID:
        return slayer3d_replication_read_enum_id(reader, &out_value->value.as_int32);
    case SLAYER3D_REPLICATION_FIELD_VEC2:
        return slayer3d_replication_read_vec2(reader, &out_value->value.as_vec2);
    case SLAYER3D_REPLICATION_FIELD_VEC3:
        return slayer3d_replication_read_vec3(reader, &out_value->value.as_vec3);
    default:
        return false;
    }
}

static bool game_data_apply_actor_replication_field(slayer3d_game_data_runtime *runtime,
                                                    slayer3d_registered_actor *actor,
                                                    const slayer3d_replication_field_descriptor *field,
                                                    const game_data_snapshot_value *value)
{
    if (actor == NULL || field == NULL || value == NULL || field->path == NULL || field->type != value->type)
        return false;

    if (SDL_strcmp(field->path, "active") == 0)
    {
        if (value->type != SLAYER3D_REPLICATION_FIELD_BOOL)
            return false;
        int actor_index = -1;
        actor_pool_runtime *pool = find_actor_pool_for_actor(runtime, actor->name, &actor_index);
        if (pool != NULL && actor_index >= 0)
        {
            if (value->value.as_bool)
            {
                if (actor_pool_actor_is_active(pool, actor, actor_index))
                    return true;
                return actor_pool_initialize_slot(runtime, pool, actor_index, true);
            }
            if (actor_pool_lifecycle_state(pool, actor_index) == ACTOR_LIFECYCLE_INACTIVE)
                return true;
            return actor_pool_request_despawn(runtime, pool, actor, actor_index, "network_snapshot");
        }
        actor->active = value->value.as_bool;
        return true;
    }
    if (SDL_strcmp(field->path, "position") == 0)
    {
        if (value->type != SLAYER3D_REPLICATION_FIELD_VEC3)
            return false;
        actor_set_position(actor, value->value.as_vec3);
        return true;
    }
    if (SDL_strcmp(field->path, "rotation") == 0 || SDL_strcmp(field->path, "scale") == 0)
    {
        if (value->type != SLAYER3D_REPLICATION_FIELD_VEC3)
            return false;
        slayer3d_properties_set_vec3(actor->props, field->path, value->value.as_vec3);
        return true;
    }

    const char *key = game_data_replication_property_key(field->path);
    if (key == NULL)
        return false;

    switch (value->type)
    {
    case SLAYER3D_REPLICATION_FIELD_BOOL:
        slayer3d_properties_set_bool(actor->props, key, value->value.as_bool);
        return true;
    case SLAYER3D_REPLICATION_FIELD_INT32:
    case SLAYER3D_REPLICATION_FIELD_ENUM_ID:
        slayer3d_properties_set_int(actor->props, key, value->value.as_int32);
        return true;
    case SLAYER3D_REPLICATION_FIELD_FLOAT32:
        slayer3d_properties_set_float(actor->props, key, value->value.as_float32);
        return true;
    case SLAYER3D_REPLICATION_FIELD_VEC2: {
        const slayer3d_vec3 current =
            slayer3d_properties_get_vec3(actor->props, key, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(actor->props, key,
                                     slayer3d_vec3_make(value->value.as_vec2.x, value->value.as_vec2.y, current.z));
        return true;
    }
    case SLAYER3D_REPLICATION_FIELD_VEC3:
        slayer3d_properties_set_vec3(actor->props, key, value->value.as_vec3);
        return true;
    default:
        return false;
    }
}

void copy_property_value(slayer3d_properties *target, const char *key, const slayer3d_value *value)
{
    if (target == NULL || key == NULL || value == NULL)
        return;

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        slayer3d_properties_set_int(target, key, value->as_int);
        break;
    case SLAYER3D_VALUE_FLOAT:
        slayer3d_properties_set_float(target, key, value->as_float);
        break;
    case SLAYER3D_VALUE_BOOL:
        slayer3d_properties_set_bool(target, key, value->as_bool);
        break;
    case SLAYER3D_VALUE_VEC3:
        slayer3d_properties_set_vec3(target, key, value->as_vec3);
        break;
    case SLAYER3D_VALUE_STRING:
        slayer3d_properties_set_string(target, key, value->as_string);
        break;
    case SLAYER3D_VALUE_COLOR:
        slayer3d_properties_set_color(target, key, value->as_color);
        break;
    }
}

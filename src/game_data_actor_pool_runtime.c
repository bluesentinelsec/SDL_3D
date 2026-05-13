/**
 * @file game_data_actor_pool_runtime.c
 * @brief Actor property snapshots, actor pool allocation, and actor pool data actions.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_log.h>

static bool json_string_array_contains(yyjson_val *array, const char *value)
{
    if (!yyjson_is_arr(array) || value == NULL)
        return false;
    for (size_t i = 0; i < yyjson_arr_size(array); ++i)
    {
        const char *entry = yyjson_get_str(yyjson_arr_get(array, i));
        if (entry != NULL && SDL_strcmp(entry, value) == 0)
            return true;
    }
    return false;
}

static property_snapshot *find_property_snapshot(slayer3d_game_data_runtime *runtime, const char *name,
                                                 const char *target)
{
    if (runtime == NULL || name == NULL || target == NULL)
        return NULL;
    for (int i = 0; i < runtime->property_snapshot_count; ++i)
    {
        property_snapshot *snapshot = &runtime->property_snapshots[i];
        if (snapshot->name != NULL && snapshot->target != NULL && SDL_strcmp(snapshot->name, name) == 0 &&
            SDL_strcmp(snapshot->target, target) == 0)
            return snapshot;
    }
    return NULL;
}

static property_snapshot *get_or_create_property_snapshot(slayer3d_game_data_runtime *runtime, const char *name,
                                                          const char *target)
{
    property_snapshot *existing = find_property_snapshot(runtime, name, target);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || name == NULL || name[0] == '\0' || target == NULL || target[0] == '\0')
        return NULL;
    if (runtime->property_snapshot_count >= runtime->property_snapshot_capacity)
    {
        const int next_capacity = runtime->property_snapshot_capacity > 0 ? runtime->property_snapshot_capacity * 2 : 4;
        property_snapshot *next = (property_snapshot *)SDL_realloc(
            runtime->property_snapshots, (size_t)next_capacity * sizeof(*runtime->property_snapshots));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->property_snapshot_capacity, 0,
                   (size_t)(next_capacity - runtime->property_snapshot_capacity) *
                       sizeof(*runtime->property_snapshots));
        runtime->property_snapshots = next;
        runtime->property_snapshot_capacity = next_capacity;
    }

    property_snapshot *snapshot = &runtime->property_snapshots[runtime->property_snapshot_count];
    snapshot->name = SDL_strdup(name);
    snapshot->target = SDL_strdup(target);
    snapshot->properties = slayer3d_properties_create();
    if (snapshot->name == NULL || snapshot->target == NULL || snapshot->properties == NULL)
    {
        SDL_free(snapshot->name);
        SDL_free(snapshot->target);
        slayer3d_properties_destroy(snapshot->properties);
        SDL_zero(*snapshot);
        return NULL;
    }
    runtime->property_snapshot_count++;
    return snapshot;
}

static void copy_selected_properties(slayer3d_properties *target, const slayer3d_properties *source, yyjson_val *keys)
{
    if (target == NULL || source == NULL)
        return;
    if (yyjson_is_arr(keys))
    {
        for (size_t i = 0; i < yyjson_arr_size(keys); ++i)
        {
            const char *key = yyjson_get_str(yyjson_arr_get(keys, i));
            copy_property_value(target, key, slayer3d_properties_get_value(source, key));
        }
        return;
    }

    const int count = slayer3d_properties_count(source);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (slayer3d_properties_get_key_at(source, i, &key, NULL))
            copy_property_value(target, key, slayer3d_properties_get_value(source, key));
    }
}

bool snapshot_actor_properties(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    const char *name = json_string(action, "name", NULL);
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    property_snapshot *snapshot = get_or_create_property_snapshot(runtime, name, target);
    if (actor == NULL || snapshot == NULL)
        return false;

    slayer3d_properties_clear(snapshot->properties);
    copy_selected_properties(snapshot->properties, actor->props, obj_get(action, "keys"));
    return true;
}

bool restore_actor_property_snapshot(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    const char *name = json_string(action, "name", NULL);
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    property_snapshot *snapshot = find_property_snapshot(runtime, name, target);
    if (actor == NULL || snapshot == NULL)
        return false;

    copy_selected_properties(actor->props, snapshot->properties, obj_get(action, "keys"));
    return true;
}

bool reset_actor_properties_to_authored_defaults(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    yyjson_val *entity = find_entity_json(runtime, target);
    yyjson_val *properties = obj_get(entity, "properties");
    yyjson_val *keys = obj_get(action, "keys");
    if (actor == NULL || !yyjson_is_obj(properties))
        return false;

    yyjson_val *key;
    yyjson_val *entry;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        if (yyjson_is_arr(keys) && !json_string_array_contains(keys, name))
            continue;
        entry = yyjson_obj_iter_get_val(key);
        yyjson_val *value = obj_get(entry, "value");
        if (value != NULL)
            set_actor_property_from_json(actor, name, value);
    }
    return true;
}

actor_pool_runtime *find_actor_pool(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        if (runtime->actor_pools[i].name != NULL && SDL_strcmp(runtime->actor_pools[i].name, name) == 0)
            return &runtime->actor_pools[i];
    }
    return NULL;
}

const actor_pool_runtime *find_actor_pool_const(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        if (runtime->actor_pools[i].name != NULL && SDL_strcmp(runtime->actor_pools[i].name, name) == 0)
            return &runtime->actor_pools[i];
    }
    return NULL;
}

static int actor_pool_index_for_actor(const actor_pool_runtime *pool, const char *actor_name)
{
    if (pool == NULL || actor_name == NULL)
        return -1;
    for (int i = 0; i < pool->capacity; ++i)
    {
        if (pool->actor_names[i] != NULL && SDL_strcmp(pool->actor_names[i], actor_name) == 0)
            return i;
    }
    return -1;
}

actor_pool_runtime *find_actor_pool_for_actor(slayer3d_game_data_runtime *runtime, const char *actor_name,
                                              int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (runtime == NULL || actor_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        const int index = actor_pool_index_for_actor(&runtime->actor_pools[i], actor_name);
        if (index >= 0)
        {
            if (out_index != NULL)
                *out_index = index;
            return &runtime->actor_pools[i];
        }
    }
    return NULL;
}

const actor_pool_runtime *find_actor_pool_for_actor_const(const slayer3d_game_data_runtime *runtime,
                                                          const char *actor_name, int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (runtime == NULL || actor_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        const int index = actor_pool_index_for_actor(&runtime->actor_pools[i], actor_name);
        if (index >= 0)
        {
            if (out_index != NULL)
                *out_index = index;
            return &runtime->actor_pools[i];
        }
    }
    return NULL;
}

slayer3d_registered_actor *actor_pool_allocate(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                               int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (runtime == NULL || pool == NULL)
        return NULL;

    for (int i = 0; i < pool->capacity; ++i)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[i]);
        if (actor_pool_actor_is_available(pool, actor, i))
        {
            if (out_index != NULL)
                *out_index = i;
            return actor;
        }
    }

    if (pool->exhaustion != ACTOR_POOL_EXHAUST_REUSE_OLDEST || pool->capacity <= 0)
    {
        if (pool != NULL)
        {
            pool->exhaustion_count++;
            actor_pool_copy_reason(pool->last_spawn_failure_reason, sizeof(pool->last_spawn_failure_reason),
                                   "exhausted");
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D actor pool exhausted: pool=%s capacity=%d policy=fail",
                        pool->name != NULL ? pool->name : "<unnamed>", pool->capacity);
        }
        return NULL;
    }

    int index = -1;
    Uint64 oldest_generation = SDL_MAX_UINT64;
    for (int i = 0; i < pool->capacity; ++i)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[i]);
        if (!actor_pool_actor_is_active(pool, actor, i))
            continue;
        const Uint64 generation = pool->spawn_generations != NULL ? pool->spawn_generations[i] : 0U;
        if (index < 0 || generation < oldest_generation)
        {
            index = i;
            oldest_generation = generation;
        }
    }
    if (index < 0)
    {
        pool->exhaustion_count++;
        actor_pool_copy_reason(pool->last_spawn_failure_reason, sizeof(pool->last_spawn_failure_reason), "exhausted");
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SLAYER3D actor pool exhausted: pool=%s capacity=%d no reusable actor",
                    pool->name != NULL ? pool->name : "<unnamed>", pool->capacity);
        return NULL;
    }
    if (out_index != NULL)
        *out_index = index;
    pool->exhaustion_count++;
    pool->reuse_count++;
    pool->despawn_count++;
    actor_pool_copy_reason(pool->last_despawn_reason, sizeof(pool->last_despawn_reason), "reuse_oldest");
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D actor pool exhausted: pool=%s capacity=%d reusing actor=%s",
                pool->name != NULL ? pool->name : "<unnamed>", pool->capacity,
                pool->actor_names[index] != NULL ? pool->actor_names[index] : "<unnamed>");
    return slayer3d_game_data_find_actor(runtime, pool->actor_names[index]);
}

bool actor_pool_request_despawn(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                slayer3d_registered_actor *actor, int index, const char *reason)
{
    if (runtime == NULL || pool == NULL || actor == NULL || index < 0 || index >= pool->capacity)
        return false;

    if (!actor->active && actor_pool_lifecycle_state(pool, index) == ACTOR_LIFECYCLE_INACTIVE)
        return true;

    pool->despawn_count++;
    actor_pool_copy_reason(pool->last_despawn_reason, sizeof(pool->last_despawn_reason), reason);
    actor->active = false;
    actor_pool_set_lifecycle_state(pool, actor, index, ACTOR_LIFECYCLE_DESPAWNING);
    if (runtime->actor_lifecycle_defer_depth > 0)
    {
        runtime->actor_lifecycle_flush_pending = true;
        return true;
    }
    if (!initialize_pooled_actor(pool, actor, index, false))
        return false;
    slayer3d_properties_set_string(actor->props, "pool_last_despawn_reason", pool->last_despawn_reason);
    return true;
}

void apply_actor_spawn_properties(slayer3d_registered_actor *actor, yyjson_val *properties)
{
    if (actor == NULL || !yyjson_is_obj(properties))
        return;

    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        value = yyjson_obj_iter_get_val(key);
        if (name != NULL)
            set_actor_property_from_json(actor, name, value);
    }
}

static void apply_actor_spawn_properties_from_actor(slayer3d_game_data_runtime *runtime,
                                                    slayer3d_registered_actor *actor, yyjson_val *properties_from_actor)
{
    if (runtime == NULL || actor == NULL || !yyjson_is_obj(properties_from_actor))
        return;

    slayer3d_registered_actor *source =
        slayer3d_game_data_find_actor(runtime, json_string(properties_from_actor, "source", NULL));
    yyjson_val *keys = obj_get(properties_from_actor, "keys");
    if (source == NULL || source->props == NULL || actor->props == NULL || !yyjson_is_arr(keys))
        return;

    for (size_t i = 0; i < yyjson_arr_size(keys); ++i)
    {
        const char *key = yyjson_get_str(yyjson_arr_get(keys, i));
        const slayer3d_value *value = key != NULL ? slayer3d_properties_get_value(source->props, key) : NULL;
        if (key != NULL && value != NULL)
            (void)set_property_from_value(actor->props, key, value);
    }
}

static bool payload_vec3_value(const slayer3d_properties *payload, const char *key, slayer3d_vec3 *out_value)
{
    const slayer3d_value *value = payload != NULL && key != NULL ? slayer3d_properties_get_value(payload, key) : NULL;
    if (value == NULL || value->type != SLAYER3D_VALUE_VEC3 || out_value == NULL)
        return false;
    *out_value = value->as_vec3;
    return true;
}

static bool actor_property_float_value(const slayer3d_properties *props, const char *key, float *out_value)
{
    const slayer3d_value *value = props != NULL && key != NULL ? slayer3d_properties_get_value(props, key) : NULL;
    if (value == NULL || out_value == NULL)
        return false;
    if (value->type == SLAYER3D_VALUE_FLOAT)
    {
        *out_value = value->as_float;
        return true;
    }
    if (value->type == SLAYER3D_VALUE_INT)
    {
        *out_value = (float)value->as_int;
        return true;
    }
    return false;
}

static float actor_property_float_sum(const slayer3d_properties *props, yyjson_val *keys)
{
    float sum = 0.0f;
    for (size_t i = 0; props != NULL && yyjson_is_arr(keys) && i < yyjson_arr_size(keys); ++i)
    {
        yyjson_val *key = yyjson_arr_get(keys, i);
        float value = 0.0f;
        if (yyjson_is_str(key) && actor_property_float_value(props, yyjson_get_str(key), &value))
            sum += value;
    }
    return sum;
}

static const slayer3d_registered_actor *actor_property_position_from_json(slayer3d_game_data_runtime *runtime,
                                                                          yyjson_val *position_from_actor_properties,
                                                                          slayer3d_vec3 *out_position)
{
    if (runtime == NULL || !yyjson_is_obj(position_from_actor_properties) || out_position == NULL)
        return NULL;

    const slayer3d_registered_actor *source =
        slayer3d_game_data_find_actor(runtime, json_string(position_from_actor_properties, "source", NULL));
    const char *x_key = json_string(position_from_actor_properties, "x", NULL);
    const char *y_key = json_string(position_from_actor_properties, "y", NULL);
    const char *z_key = json_string(position_from_actor_properties, "z", NULL);
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (source == NULL || source->props == NULL || !actor_property_float_value(source->props, x_key, &x) ||
        !actor_property_float_value(source->props, y_key, &y) || !actor_property_float_value(source->props, z_key, &z))
    {
        return NULL;
    }

    slayer3d_vec3 position = slayer3d_vec3_make(x, y, z);
    yyjson_val *offset_json = obj_get(position_from_actor_properties, "offset");
    if (offset_json != NULL)
    {
        const slayer3d_vec3 offset = json_vec3_value(offset_json, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        position.x += offset.x;
        position.y += offset.y;
        position.z += offset.z;
    }
    position.x += actor_property_float_sum(source->props, obj_get(position_from_actor_properties, "x_add"));
    position.y += actor_property_float_sum(source->props, obj_get(position_from_actor_properties, "y_add"));
    position.z += actor_property_float_sum(source->props, obj_get(position_from_actor_properties, "z_add"));
    *out_position = position;
    return source;
}

slayer3d_registered_actor *actor_from_payload_key(slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_properties *payload, const char *key)
{
    const char *actor_name = payload != NULL && key != NULL ? slayer3d_properties_get_string(payload, key, NULL) : NULL;
    return slayer3d_game_data_find_actor(runtime, actor_name);
}

slayer3d_vec3 actor_spawn_position_from_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                               const slayer3d_properties *payload, slayer3d_vec3 fallback,
                                               const slayer3d_registered_actor *source_actor)
{
    slayer3d_vec3 position = fallback;
    const slayer3d_registered_actor *reference_actor = source_actor;
    yyjson_val *position_json = obj_get(action, "position");
    if (position_json != NULL)
        position = json_vec3_value(position_json, position);

    const char *position_payload_key = json_string(action, "position_from_payload", NULL);
    slayer3d_vec3 payload_position;
    if (payload_vec3_value(payload, position_payload_key, &payload_position))
        position = payload_position;

    slayer3d_vec3 actor_property_position;
    const slayer3d_registered_actor *actor_property_source = actor_property_position_from_json(
        runtime, obj_get(action, "position_from_actor_properties"), &actor_property_position);
    if (actor_property_source != NULL)
    {
        position = actor_property_position;
        reference_actor = actor_property_source;
    }

    const char *from_payload_key = json_string(action, "from_payload", NULL);
    slayer3d_registered_actor *from_payload_actor = actor_from_payload_key(runtime, payload, from_payload_key);
    if (from_payload_actor != NULL)
    {
        position = from_payload_actor->position;
        reference_actor = from_payload_actor;
    }

    const char *from_actor_name = json_string(action, "from", NULL);
    slayer3d_registered_actor *from_actor = slayer3d_game_data_find_actor(runtime, from_actor_name);
    if (from_actor != NULL)
    {
        position = from_actor->position;
        reference_actor = from_actor;
    }

    yyjson_val *offset_json = obj_get(action, "offset");
    if (offset_json != NULL)
    {
        const slayer3d_vec3 offset = json_vec3_value(offset_json, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        position.x += offset.x;
        position.y += offset.y;
        position.z += offset.z;
    }
    yyjson_val *directional_offset = obj_get(action, "directional_offset");
    if (yyjson_is_obj(directional_offset) && reference_actor != NULL && reference_actor->props != NULL)
    {
        const char *property = json_string(directional_offset, "property", NULL);
        const float distance = json_float(directional_offset, "distance", 0.0f);
        slayer3d_vec3 direction =
            slayer3d_properties_get_vec3(reference_actor->props, property, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        if (distance != 0.0f && slayer3d_vec3_length_squared(direction) > 0.000001f)
        {
            direction = slayer3d_vec3_normalize(direction);
            position.x += direction.x * distance;
            position.y += direction.y * distance;
            position.z += direction.z * distance;
        }
    }
    yyjson_val *payload_directional_offset = obj_get(action, "payload_directional_offset");
    if (yyjson_is_obj(payload_directional_offset))
    {
        const char *property = json_string(payload_directional_offset, "property", NULL);
        const float distance = json_float(payload_directional_offset, "distance", 0.0f);
        slayer3d_vec3 direction;
        if (distance != 0.0f && payload_vec3_value(payload, property, &direction) &&
            slayer3d_vec3_length_squared(direction) > 0.000001f)
        {
            direction = slayer3d_vec3_normalize(direction);
            position.x += direction.x * distance;
            position.y += direction.y * distance;
            position.z += direction.z * distance;
        }
    }
    return position;
}

static void apply_actor_spawn_velocity_from_payload(slayer3d_registered_actor *actor, yyjson_val *action,
                                                    const slayer3d_properties *payload)
{
    slayer3d_vec3 direction;
    const char *payload_key = json_string(action, "velocity_from_payload", NULL);
    if (actor == NULL || !payload_vec3_value(payload, payload_key, &direction) ||
        slayer3d_vec3_length_squared(direction) <= 0.000001f)
    {
        return;
    }

    direction = slayer3d_vec3_normalize(direction);
    const float speed = json_float(action, "speed", 1.0f);
    const slayer3d_vec3 velocity = slayer3d_vec3_make(direction.x * speed, direction.y * speed, direction.z * speed);
    slayer3d_properties_set_vec3(actor->props, json_string(action, "velocity_property", "velocity"), velocity);
}

bool execute_actor_spawn_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                const slayer3d_properties *payload)
{
    actor_pool_runtime *pool = find_actor_pool(runtime, json_string(action, "pool", NULL));
    actor_pool_note_spawn_attempt(pool);
    int actor_index = -1;
    slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
    if (pool == NULL || actor == NULL || actor_index < 0)
    {
        actor_pool_note_spawn_failure(pool, "exhausted");
        return false;
    }

    actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
    if (!initialize_pooled_actor(pool, actor, actor_index, true))
    {
        actor_pool_note_spawn_failure(pool, "initialize_failed");
        return false;
    }
    if (pool->spawn_generations != NULL)
    {
        pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
        slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                    (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
    }
    actor_set_position(actor, actor_spawn_position_from_action(runtime, action, payload, actor->position, actor));
    apply_actor_spawn_properties_from_actor(runtime, actor, obj_get(action, "properties_from_actor"));
    apply_actor_spawn_properties(actor, obj_get(action, "properties"));
    apply_actor_spawn_velocity_from_payload(actor, action, payload);
    actor_pool_note_spawn_success(runtime, pool);

    const char *actor_key = json_string(action, "output_actor_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && actor_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, actor_key, actor->name);
    const char *id_key = json_string(action, "output_id_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && id_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, id_key, actor->id);
    const char *pool_index_key = json_string(action, "output_pool_index_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && pool_index_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, pool_index_key, actor_index);
    return true;
}

bool execute_actor_despawn_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    if (actor == NULL)
        return false;

    int actor_index = -1;
    actor_pool_runtime *pool = find_actor_pool_for_actor(runtime, actor->name, &actor_index);
    if (pool != NULL && actor_index >= 0)
        return actor_pool_request_despawn(runtime, pool, actor, actor_index, json_string(action, "reason", "action"));

    actor->active = false;
    return true;
}

bool execute_actor_despawn_action_with_payload(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                               const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor =
        actor_from_payload_key(runtime, payload, json_string(action, "target_from_payload", NULL));
    if (actor == NULL)
        return execute_actor_despawn_action(runtime, action);

    int actor_index = -1;
    actor_pool_runtime *pool = find_actor_pool_for_actor(runtime, actor->name, &actor_index);
    if (pool != NULL && actor_index >= 0)
        return actor_pool_request_despawn(runtime, pool, actor, actor_index, json_string(action, "reason", "action"));

    actor->active = false;
    return true;
}

bool execute_actor_despawn_by_tag_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *tag = json_string(action, "tag", NULL);
    if (runtime == NULL || tag == NULL || tag[0] == '\0')
        return false;

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!entity_json_has_tags(pool->archetype_json, &tag, 1))
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index))
            {
                (void)actor_pool_request_despawn(runtime, pool, actor, actor_index,
                                                 json_string(action, "reason", "action_tag"));
            }
        }
    }
    return true;
}

typedef enum weapon_fire_status
{
    WEAPON_FIRE_READY = 0,
    WEAPON_FIRE_COOLDOWN,
    WEAPON_FIRE_RELOADING,
    WEAPON_FIRE_EMPTY
} weapon_fire_status;

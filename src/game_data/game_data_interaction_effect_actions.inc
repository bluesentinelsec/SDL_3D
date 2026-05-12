/* Target filtering, interaction, and effect action helpers. Included by game_data_actions.inc to preserve internal
 * linkage. */

static yyjson_val *actor_component_json(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_registered_actor *actor, const char *type)
{
    if (runtime == NULL || actor == NULL || type == NULL)
        return NULL;

    yyjson_val *entity = find_entity_json(runtime, actor->name);
    if (entity == NULL)
    {
        int actor_index = -1;
        const actor_pool_runtime *pool = find_actor_pool_for_actor_const(runtime, actor->name, &actor_index);
        if (pool != NULL && actor_index >= 0)
            entity = pool->archetype_json;
    }

    yyjson_val *components = obj_get(entity, "components");
    for (size_t i = 0; yyjson_is_arr(components) && i < yyjson_arr_size(components); ++i)
    {
        yyjson_val *component = yyjson_arr_get(components, i);
        if (SDL_strcmp(json_string(component, "type", ""), type) != 0)
            continue;
        yyjson_val *enabled_if = obj_get(component, "enabled_if");
        if (enabled_if != NULL && !eval_data_condition(runtime, enabled_if, NULL))
            continue;
        return component;
    }
    return NULL;
}

static float json_float_override(yyjson_val *primary, yyjson_val *fallback_json, const char *key, float fallback)
{
    yyjson_val *value = obj_get(primary, key);
    if (yyjson_is_num(value))
        return (float)yyjson_get_num(value);
    return json_float(fallback_json, key, fallback);
}

static bool actor_has_authored_tag(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *actor,
                                   const char *tag)
{
    if (tag == NULL || tag[0] == '\0')
        return true;
    yyjson_val *entity = find_entity_json(runtime, actor != NULL ? actor->name : NULL);
    if (entity_json_has_tags(entity, &tag, 1))
        return true;
    int actor_index = -1;
    const actor_pool_runtime *pool =
        find_actor_pool_for_actor_const(runtime, actor != NULL ? actor->name : NULL, &actor_index);
    return pool != NULL && actor_index >= 0 && entity_json_has_tags(pool->archetype_json, &tag, 1);
}

static yyjson_val *target_filter_json(yyjson_val *json)
{
    yyjson_val *filter = obj_get(json, "target_filter");
    return yyjson_is_obj(filter) ? filter : json;
}

static const char *target_filter_string(yyjson_val *json, const char *key, const char *fallback)
{
    yyjson_val *filter = target_filter_json(json);
    const char *value = json_string(filter, key, NULL);
    if (value != NULL)
        return value;
    return filter != json ? json_string(json, key, fallback) : fallback;
}

static bool target_filter_bool(yyjson_val *json, const char *key, bool fallback)
{
    yyjson_val *filter = target_filter_json(json);
    yyjson_val *value = obj_get(filter, key);
    if (yyjson_is_bool(value))
        return yyjson_get_bool(value);
    return filter != json ? json_bool(json, key, fallback) : fallback;
}

static bool target_filter_string_array_contains(yyjson_val *json, const char *key, const char *value)
{
    if (value == NULL || value[0] == '\0')
        return false;
    yyjson_val *filter = target_filter_json(json);
    yyjson_val *array = obj_get(filter, key);
    if (array == NULL && filter != json)
        array = obj_get(json, key);
    if (yyjson_is_str(array))
        return SDL_strcmp(yyjson_get_str(array), value) == 0;
    for (size_t i = 0; yyjson_is_arr(array) && i < yyjson_arr_size(array); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(array, i);
        if (yyjson_is_str(entry) && SDL_strcmp(yyjson_get_str(entry), value) == 0)
            return true;
    }
    return false;
}

static bool target_filter_actor_has_any_tag(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_registered_actor *actor, yyjson_val *json, const char *key)
{
    yyjson_val *filter = target_filter_json(json);
    yyjson_val *tags = obj_get(filter, key);
    if (tags == NULL && filter != json)
        tags = obj_get(json, key);
    if (yyjson_is_str(tags))
        return actor_has_authored_tag(runtime, actor, yyjson_get_str(tags));
    for (size_t i = 0; yyjson_is_arr(tags) && i < yyjson_arr_size(tags); ++i)
    {
        yyjson_val *tag = yyjson_arr_get(tags, i);
        if (yyjson_is_str(tag) && actor_has_authored_tag(runtime, actor, yyjson_get_str(tag)))
            return true;
    }
    return false;
}

static bool target_filter_actor_has_blocked_tag(const slayer3d_game_data_runtime *runtime,
                                                const slayer3d_registered_actor *actor, yyjson_val *json,
                                                const char *key)
{
    yyjson_val *filter = target_filter_json(json);
    yyjson_val *tags = obj_get(filter, key);
    if (tags == NULL && filter != json)
        tags = obj_get(json, key);
    if (yyjson_is_str(tags))
        return actor_has_authored_tag(runtime, actor, yyjson_get_str(tags));
    for (size_t i = 0; yyjson_is_arr(tags) && i < yyjson_arr_size(tags); ++i)
    {
        yyjson_val *tag = yyjson_arr_get(tags, i);
        if (yyjson_is_str(tag) && actor_has_authored_tag(runtime, actor, yyjson_get_str(tag)))
            return true;
    }
    return false;
}

static const char *actor_faction(const slayer3d_registered_actor *actor, const char *property)
{
    return actor != NULL && property != NULL && property[0] != '\0'
               ? slayer3d_properties_get_string(actor->props, property, "")
               : "";
}

static const char *faction_relationship(const slayer3d_game_data_runtime *runtime, const char *source_faction,
                                        const char *target_faction)
{
    if (source_faction == NULL || source_faction[0] == '\0' || target_faction == NULL || target_faction[0] == '\0')
        return "neutral";

    yyjson_val *factions = obj_get(runtime_root(runtime), "factions");
    yyjson_val *source = obj_get(factions, source_faction);
    const char *authored = json_string(source, target_faction, NULL);
    if (authored != NULL)
        return authored;
    authored = json_string(source, "default", NULL);
    if (authored != NULL)
        return authored;
    authored = json_string(factions, "default_relationship", NULL);
    if (authored != NULL)
        return authored;
    return SDL_strcmp(source_faction, target_faction) == 0 ? "friendly" : "hostile";
}

static slayer3d_registered_actor *action_source_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source =
        actor_from_payload_key(runtime, payload, target_filter_string(action, "source_from_payload", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, target_filter_string(action, "source", NULL));
    return source;
}

bool actor_matches_target_filter(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *target,
                                 const slayer3d_registered_actor *source, yyjson_val *json,
                                 const slayer3d_properties *payload, const char *fallback_tag,
                                 bool fallback_exclude_source)
{
    if (target == NULL || !target->active)
        return false;
    const char *tag = target_filter_string(json, "target_tag", fallback_tag);
    if (tag == NULL)
        tag = target_filter_string(json, "affected_tag", NULL);
    if (tag == NULL)
        tag = target_filter_string(json, "hit_tag", NULL);
    if (tag != NULL && tag[0] != '\0' && !actor_has_authored_tag(runtime, target, tag))
        return false;
    if (target_filter_actor_has_any_tag(runtime, target, json, "include_tags") == false &&
        (obj_get(target_filter_json(json), "include_tags") != NULL || obj_get(json, "include_tags") != NULL))
        return false;
    if (target_filter_actor_has_blocked_tag(runtime, target, json, "exclude_tags"))
        return false;

    const bool exclude_source =
        target_filter_bool(json, "exclude_source", target_filter_bool(json, "exclude_self", fallback_exclude_source));
    if (exclude_source && source != NULL && target == source)
        return false;

    const char *owner_property =
        target_filter_string(json, "owner_property", target_filter_string(json, "owner_actor_property", "owner"));
    const bool exclude_owner = target_filter_bool(json, "exclude_owner", false);
    if (exclude_owner && source != NULL && owner_property != NULL && owner_property[0] != '\0')
    {
        const char *source_owner = slayer3d_properties_get_string(source->props, owner_property, NULL);
        if (source_owner != NULL && target->name != NULL && SDL_strcmp(source_owner, target->name) == 0)
            return false;
        const char *target_owner = slayer3d_properties_get_string(target->props, owner_property, NULL);
        if (target_owner != NULL && source->name != NULL && SDL_strcmp(target_owner, source->name) == 0)
            return false;
    }

    const char *faction_property = target_filter_string(json, "faction_property", "faction");
    const char *source_faction_property = target_filter_string(json, "source_faction_property", faction_property);
    const char *target_faction = actor_faction(target, faction_property);
    const char *source_faction = target_filter_string(json, "source_faction", NULL);
    const char *source_faction_from_payload = target_filter_string(json, "source_faction_from_payload", NULL);
    if (source_faction_from_payload != NULL && payload != NULL)
        source_faction = slayer3d_properties_get_string(payload, source_faction_from_payload, source_faction);
    if ((source_faction == NULL || source_faction[0] == '\0') && source != NULL)
        source_faction = actor_faction(source, source_faction_property);

    const char *required_target_faction = target_filter_string(json, "target_faction", NULL);
    if (required_target_faction != NULL && required_target_faction[0] != '\0' &&
        SDL_strcmp(target_faction, required_target_faction) != 0)
        return false;
    if (target_filter_string_array_contains(json, "include_factions", target_faction) == false &&
        (obj_get(target_filter_json(json), "include_factions") != NULL || obj_get(json, "include_factions") != NULL))
        return false;
    if (target_filter_string_array_contains(json, "exclude_factions", target_faction))
        return false;

    const char *relationship = target_filter_string(json, "relationship", NULL);
    if (relationship != NULL && relationship[0] != '\0' && SDL_strcmp(relationship, "any") != 0)
    {
        const char *actual = faction_relationship(runtime, source_faction, target_faction);
        if (SDL_strcmp(actual, relationship) != 0)
            return false;
    }
    return true;
}

static bool interaction_actor_in_front(const slayer3d_registered_actor *source, const slayer3d_registered_actor *target,
                                       yyjson_val *action, yyjson_val *component)
{
    if (source == NULL || target == NULL)
        return false;
    const float min_dot = SDL_clamp(json_float_override(action, component, "min_dot", 0.15f), -1.0f, 1.0f);
    if (min_dot <= -1.0f)
        return true;

    const char *yaw_property = json_string(action, "yaw_property", json_string(component, "yaw_property", "yaw"));
    const float yaw = slayer3d_properties_get_float(source->props, yaw_property, 0.0f);
    const slayer3d_vec3 forward = slayer3d_vec3_make(SDL_sinf(yaw), 0.0f, -SDL_cosf(yaw));
    slayer3d_vec3 to_target =
        slayer3d_vec3_make(target->position.x - source->position.x, 0.0f, target->position.z - source->position.z);
    const float len_sq = slayer3d_vec3_length_squared(to_target);
    if (len_sq <= 0.000001f)
        return true;
    to_target = slayer3d_vec3_normalize(to_target);
    return forward.x * to_target.x + forward.z * to_target.z >= min_dot;
}

static slayer3d_registered_actor *find_interactable_actor(slayer3d_game_data_runtime *runtime,
                                                          slayer3d_registered_actor *source, yyjson_val *action,
                                                          yyjson_val **out_component, float *out_distance)
{
    if (out_component != NULL)
        *out_component = NULL;
    if (out_distance != NULL)
        *out_distance = 0.0f;
    if (runtime == NULL || source == NULL)
        return NULL;

    const char *tag = json_string(action, "target_tag", json_string(action, "interactable_tag", NULL));
    slayer3d_registered_actor *best = NULL;
    yyjson_val *best_component = NULL;
    float best_distance_sq = 0.0f;

    sensor_actor_list candidates;
    SDL_zero(candidates);
    if (!collect_sensor_endpoint_actors(runtime, NULL, tag, &candidates))
    {
        sensor_actor_list_free(&candidates);
        return NULL;
    }

    if (tag == NULL)
    {
        sensor_actor_list_free(&candidates);
        yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
        for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
        {
            const char *entity_name = json_string(yyjson_arr_get(entities, i), "name", NULL);
            if (!active_scene_has_entity_internal(runtime, entity_name))
                continue;
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
            if (runtime_actor_is_active(runtime, actor) && !sensor_actor_list_add(&candidates, actor))
            {
                sensor_actor_list_free(&candidates);
                return NULL;
            }
        }
        for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
        {
            actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
            if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
                continue;
            for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
            {
                slayer3d_registered_actor *actor =
                    slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
                if (actor_pool_actor_is_active(pool, actor, actor_index) && !sensor_actor_list_add(&candidates, actor))
                {
                    sensor_actor_list_free(&candidates);
                    return NULL;
                }
            }
        }
    }

    for (int i = 0; i < candidates.count; ++i)
    {
        slayer3d_registered_actor *candidate = candidates.items[i];
        if (candidate == source || !actor_has_authored_tag(runtime, candidate, tag))
            continue;
        yyjson_val *component = actor_component_json(runtime, candidate, "interactable");
        if (component == NULL)
            continue;
        const float range = SDL_max(json_float_override(action, component, "range", 2.25f), 0.0f);
        const float dx = candidate->position.x - source->position.x;
        const float dy = candidate->position.y - source->position.y;
        const float dz = candidate->position.z - source->position.z;
        const float distance_sq = dx * dx + dy * dy + dz * dz;
        if (distance_sq > range * range || !interaction_actor_in_front(source, candidate, action, component))
            continue;
        if (best == NULL || distance_sq < best_distance_sq)
        {
            best = candidate;
            best_component = component;
            best_distance_sq = distance_sq;
        }
    }

    sensor_actor_list_free(&candidates);
    if (out_component != NULL)
        *out_component = best_component;
    if (out_distance != NULL)
        *out_distance = best != NULL ? SDL_sqrtf(best_distance_sq) : 0.0f;
    return best;
}

static const char *interaction_prompt_key(yyjson_val *component)
{
    return json_string(component, "prompt_key", json_string(component, "prompt", ""));
}

static bool interaction_requirement_met(slayer3d_registered_actor *source, yyjson_val *component)
{
    yyjson_val *requirement = obj_get(component, "requires");
    if (!yyjson_is_obj(requirement))
        return true;
    const char *property = json_string(requirement, "property", json_string(requirement, "resource", NULL));
    if (source == NULL || property == NULL || property[0] == '\0')
        return false;
    const float amount = SDL_max(json_float(requirement, "amount", 1.0f), 0.0f);
    return weapon_actor_numeric_property(source, property, 0.0f) + 0.0001f >= amount;
}

static void interaction_consume_requirement(slayer3d_registered_actor *source, yyjson_val *component)
{
    yyjson_val *requirement = obj_get(component, "requires");
    if (!yyjson_is_obj(requirement) || !json_bool(requirement, "consume", false))
        return;
    const char *property = json_string(requirement, "property", json_string(requirement, "resource", NULL));
    if (property == NULL || property[0] == '\0')
        return;
    const float amount = SDL_max(json_float(requirement, "amount", 1.0f), 0.0f);
    const float current = weapon_actor_numeric_property(source, property, 0.0f);
    weapon_set_actor_numeric_property(source, property, SDL_max(current - amount, 0.0f));
}

static slayer3d_properties *interaction_payload(const slayer3d_registered_actor *source,
                                                const slayer3d_registered_actor *interactable, yyjson_val *component,
                                                const char *status, float distance)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    slayer3d_properties_set_string(payload, "actor_name", source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_string(payload, "source_actor_name",
                                   source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_string(payload, "interactable_actor_name",
                                   interactable != NULL && interactable->name != NULL ? interactable->name : "");
    slayer3d_properties_set_string(payload, "target_actor_name",
                                   interactable != NULL && interactable->name != NULL ? interactable->name : "");
    slayer3d_properties_set_string(payload, "prompt_key", interaction_prompt_key(component));
    slayer3d_properties_set_string(payload, "interaction_status", status != NULL ? status : "success");
    slayer3d_properties_set_bool(payload, "locked", status != NULL && SDL_strcmp(status, "locked") == 0);
    slayer3d_properties_set_float(payload, "distance", distance);
    return payload;
}

static bool interaction_emit_or_run(slayer3d_game_data_runtime *runtime, yyjson_val *component, const char *actions_key,
                                    const char *signal_key, const slayer3d_properties *payload)
{
    yyjson_val *actions = obj_get(component, actions_key);
    if (yyjson_is_arr(actions))
        return execute_action_array(runtime, actions, payload);
    const int signal_id = action_signal_id(runtime, component, signal_key);
    if (signal_id < 0)
        return true;
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL)
        return false;
    slayer3d_signal_emit(bus, signal_id, payload);
    return true;
}

static bool execute_interaction_use_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                           const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source =
        actor_from_payload_key(runtime, payload, json_string(action, "actor_from_payload", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "actor", NULL));
    if (!runtime_actor_is_active(runtime, source))
        return false;

    yyjson_val *component = NULL;
    float distance = 0.0f;
    slayer3d_registered_actor *target = find_interactable_actor(runtime, source, action, &component, &distance);
    if (target == NULL || component == NULL)
        return execute_optional_action_array(runtime, obj_get(action, "miss_actions"), NULL);

    const char *cooldown_property = json_string(component, "cooldown_property", NULL);
    if (cooldown_property != NULL && cooldown_property[0] != '\0' &&
        slayer3d_properties_get_float(target->props, cooldown_property, 0.0f) > 0.0f)
    {
        slayer3d_properties *event_payload = interaction_payload(source, target, component, "cooldown", distance);
        const bool ok = interaction_emit_or_run(runtime, component, "cooldown_actions", "on_cooldown", event_payload);
        slayer3d_properties_destroy(event_payload);
        return ok;
    }

    if (!interaction_requirement_met(source, component))
    {
        slayer3d_properties *event_payload = interaction_payload(source, target, component, "locked", distance);
        const bool ok = interaction_emit_or_run(runtime, component, "locked_actions", "on_locked", event_payload);
        slayer3d_properties_destroy(event_payload);
        return ok;
    }

    interaction_consume_requirement(source, component);
    const float cooldown = SDL_max(json_float(component, "cooldown", 0.0f), 0.0f);
    if (cooldown_property != NULL && cooldown_property[0] != '\0' && cooldown > 0.0f)
        slayer3d_properties_set_float(target->props, cooldown_property, cooldown);

    slayer3d_properties *event_payload = interaction_payload(source, target, component, "success", distance);
    const bool ok = interaction_emit_or_run(runtime, component, "actions", "signal", event_payload);
    slayer3d_properties_destroy(event_payload);
    return ok;
}

static slayer3d_registered_actor *effect_source_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source =
        actor_from_payload_key(runtime, payload, json_string(action, "source_from_payload", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "source", NULL));
    if (source == NULL)
        source = actor_from_payload_key(runtime, payload, json_string(action, "from_payload", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "from", NULL));
    return source;
}

bool collect_effect_targets(slayer3d_game_data_runtime *runtime, const char *tag, sensor_actor_list *out_list)
{
    if (runtime == NULL || out_list == NULL)
        return false;
    if (tag != NULL && tag[0] != '\0')
        return collect_sensor_endpoint_actors(runtime, NULL, tag, out_list);

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        const char *entity_name = json_string(yyjson_arr_get(entities, i), "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (runtime_actor_is_active(runtime, actor) && !sensor_actor_list_add(out_list, actor))
            return false;
    }

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index) && !sensor_actor_list_add(out_list, actor))
                return false;
        }
    }
    return true;
}

static float effect_explosion_falloff(yyjson_val *action, float distance)
{
    const float radius = SDL_max(json_float(action, "radius", 0.0f), 0.0f);
    const float inner_radius = SDL_clamp(json_float(action, "inner_radius", 0.0f), 0.0f, radius);
    if (distance <= inner_radius)
        return 1.0f;
    if (radius <= inner_radius)
        return distance <= radius ? 1.0f : 0.0f;
    const char *falloff = json_string(action, "falloff", "linear");
    if (falloff != NULL && (SDL_strcmp(falloff, "none") == 0 || SDL_strcmp(falloff, "constant") == 0))
        return 1.0f;
    return SDL_clamp(1.0f - ((distance - inner_radius) / (radius - inner_radius)), 0.0f, 1.0f);
}

static slayer3d_properties *effect_explosion_payload(const slayer3d_registered_actor *source,
                                                     const slayer3d_registered_actor *target, slayer3d_vec3 origin,
                                                     float distance, float radius, float falloff, float damage,
                                                     float impulse)
{
    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload == NULL)
        return NULL;
    slayer3d_properties_set_string(event_payload, "actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(event_payload, "target_actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(event_payload, "source_actor_name",
                                   source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_vec3(event_payload, "origin", origin);
    slayer3d_properties_set_float(event_payload, "distance", distance);
    slayer3d_properties_set_float(event_payload, "radius", radius);
    slayer3d_properties_set_float(event_payload, "falloff", falloff);
    slayer3d_properties_set_float(event_payload, "damage", damage);
    slayer3d_properties_set_float(event_payload, "amount", damage);
    slayer3d_properties_set_float(event_payload, "impulse", impulse);
    return event_payload;
}

static void effect_apply_impulse(slayer3d_registered_actor *actor, slayer3d_vec3 origin, float strength,
                                 const char *velocity_property)
{
    if (actor == NULL || strength <= 0.0f || velocity_property == NULL || velocity_property[0] == '\0')
        return;
    slayer3d_vec3 delta =
        slayer3d_vec3_make(actor->position.x - origin.x, actor->position.y - origin.y, actor->position.z - origin.z);
    if (slayer3d_vec3_length_squared(delta) <= 0.000001f)
        delta = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    else
        delta = slayer3d_vec3_normalize(delta);
    const slayer3d_vec3 velocity =
        slayer3d_properties_get_vec3(actor->props, velocity_property, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(actor->props, velocity_property,
                                 slayer3d_vec3_make(velocity.x + delta.x * strength, velocity.y + delta.y * strength,
                                                    velocity.z + delta.z * strength));
}

static bool execute_effect_explosion_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload)
{
    if (runtime == NULL || action == NULL)
        return false;

    const float radius = SDL_max(json_float(action, "radius", 0.0f), 0.0f);
    if (radius <= 0.0f)
        return true;
    slayer3d_registered_actor *source = effect_source_actor(runtime, action, payload);
    const slayer3d_vec3 fallback = source != NULL ? source->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const slayer3d_vec3 origin = actor_spawn_position_from_action(runtime, action, payload, fallback);
    const char *tag = json_string(action, "target_tag", json_string(action, "affected_tag", NULL));
    const bool exclude_source = json_bool(action, "exclude_source", true);
    const int max_targets = SDL_max(json_int(action, "max_targets", SDL_MAX_SINT32), 0);
    const float base_damage = SDL_max(json_float(action, "damage", json_float(action, "amount", 0.0f)), 0.0f);
    const float base_impulse = SDL_max(json_float(action, "impulse", 0.0f), 0.0f);
    const char *velocity_property = json_string(action, "velocity_property", "velocity");

    sensor_actor_list targets;
    SDL_zero(targets);
    if (!collect_effect_targets(runtime, tag, &targets))
    {
        sensor_actor_list_free(&targets);
        return false;
    }

    bool ok = true;
    int affected = 0;
    const float radius_sq = radius * radius;
    for (int i = 0; i < targets.count && affected < max_targets; ++i)
    {
        slayer3d_registered_actor *target = targets.items[i];
        if (!actor_matches_target_filter(runtime, target, source, action, payload, tag, exclude_source))
        {
            continue;
        }
        const float dx = target->position.x - origin.x;
        const float dy = target->position.y - origin.y;
        const float dz = target->position.z - origin.z;
        const float distance_sq = dx * dx + dy * dy + dz * dz;
        if (distance_sq > radius_sq)
            continue;
        const float distance = SDL_sqrtf(distance_sq);
        const float falloff = effect_explosion_falloff(action, distance);
        const float damage = base_damage * falloff;
        const float impulse = base_impulse * falloff;
        if (damage > 0.0f)
            ok = apply_combat_damage_to_actor(runtime, action, payload, target, damage) && ok;
        effect_apply_impulse(target, origin, impulse, velocity_property);

        slayer3d_properties *event_payload =
            effect_explosion_payload(source, target, origin, distance, radius, falloff, damage, impulse);
        ok = execute_optional_action_array(runtime, obj_get(action, "actions"), event_payload) && ok;
        emit_optional_signal(runtime, action, "on_hit", event_payload);
        slayer3d_properties_destroy(event_payload);
        ++affected;
    }

    sensor_actor_list_free(&targets);
    return ok;
}

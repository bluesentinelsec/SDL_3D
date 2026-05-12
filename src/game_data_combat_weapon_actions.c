/* Combat, resource, pickup, and weapon action helpers. */

#include "game_data_internal.h"

typedef enum weapon_fire_status
{
    WEAPON_FIRE_READY,
    WEAPON_FIRE_COOLDOWN,
    WEAPON_FIRE_RELOADING,
    WEAPON_FIRE_EMPTY
} weapon_fire_status;

static bool weapon_value_as_float(const slayer3d_value *value, float *out_value)
{
    if (value == NULL || out_value == NULL)
        return false;
    if (value->type == SLAYER3D_VALUE_INT)
    {
        *out_value = (float)value->as_int;
        return true;
    }
    if (value->type == SLAYER3D_VALUE_FLOAT)
    {
        *out_value = value->as_float;
        return true;
    }
    return false;
}

static float weapon_actor_numeric_property(const slayer3d_registered_actor *actor, const char *key, float fallback)
{
    float value = fallback;
    return actor != NULL && actor->props != NULL &&
                   weapon_value_as_float(slayer3d_properties_get_value(actor->props, key), &value)
               ? value
               : fallback;
}

static void weapon_set_actor_numeric_property(slayer3d_registered_actor *actor, const char *key, float value)
{
    if (actor == NULL || actor->props == NULL || key == NULL || key[0] == '\0')
        return;
    const slayer3d_value *existing = slayer3d_properties_get_value(actor->props, key);
    const float rounded = SDL_roundf(value);
    if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && SDL_fabsf(value - rounded) <= 0.0001f)
        slayer3d_properties_set_int(actor->props, key, (int)rounded);
    else
        slayer3d_properties_set_float(actor->props, key, value);
}

static slayer3d_properties *weapon_event_payload(const slayer3d_registered_actor *source, yyjson_val *action,
                                                 weapon_fire_status status)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    slayer3d_properties_set_string(payload, "actor_name", source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_string(payload, "source_actor_name",
                                   source != NULL && source->name != NULL ? source->name : "");
    const char *status_text = "ready";
    if (status == WEAPON_FIRE_COOLDOWN)
        status_text = "cooldown";
    else if (status == WEAPON_FIRE_RELOADING)
        status_text = "reloading";
    else if (status == WEAPON_FIRE_EMPTY)
        status_text = "empty";
    slayer3d_properties_set_string(payload, "weapon_status", status_text);
    slayer3d_properties_set_float(payload, "ammo_per_shot", json_float(action, "ammo_per_shot", 1.0f));
    return payload;
}

static void weapon_emit_signal(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *signal_key,
                               const slayer3d_registered_actor *source, weapon_fire_status status)
{
    const int signal_id = action_signal_id(runtime, action, signal_key);
    if (signal_id < 0 || runtime_bus(runtime) == NULL)
        return;
    slayer3d_properties *payload = weapon_event_payload(source, action, status);
    slayer3d_signal_emit(runtime_bus(runtime), signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static weapon_fire_status weapon_fire_status_for_actor(const slayer3d_registered_actor *actor, yyjson_val *action)
{
    if (actor == NULL || action == NULL)
        return WEAPON_FIRE_EMPTY;

    const char *cooldown_property = json_string(action, "cooldown_property", "fire_timer");
    if (cooldown_property != NULL && cooldown_property[0] != '\0' &&
        slayer3d_properties_get_float(actor->props, cooldown_property, 0.0f) > 0.0f)
    {
        return WEAPON_FIRE_COOLDOWN;
    }

    const char *reload_timer_property = json_string(action, "reload_timer_property", NULL);
    if (reload_timer_property != NULL && reload_timer_property[0] != '\0' &&
        slayer3d_properties_get_float(actor->props, reload_timer_property, 0.0f) > 0.0f)
    {
        return WEAPON_FIRE_RELOADING;
    }

    const float ammo_per_shot = SDL_max(json_float(action, "ammo_per_shot", 1.0f), 0.0f);
    const char *clip_property = json_string(action, "clip_property", NULL);
    if (clip_property != NULL && clip_property[0] != '\0' &&
        weapon_actor_numeric_property(actor, clip_property, 0.0f) + 0.0001f < ammo_per_shot)
    {
        return WEAPON_FIRE_EMPTY;
    }

    const char *ammo_resource = json_string(action, "ammo_resource", NULL);
    const char *ammo_property = json_string(action, "ammo_property", ammo_resource);
    if (clip_property == NULL && ammo_property != NULL && ammo_property[0] != '\0' &&
        weapon_actor_numeric_property(actor, ammo_property, 0.0f) + 0.0001f < ammo_per_shot)
    {
        return WEAPON_FIRE_EMPTY;
    }
    return WEAPON_FIRE_READY;
}

static bool weapon_fire_prepare(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                const slayer3d_registered_actor *actor)
{
    const weapon_fire_status status = weapon_fire_status_for_actor(actor, action);
    if (status == WEAPON_FIRE_READY)
        return true;
    if (status == WEAPON_FIRE_COOLDOWN)
        weapon_emit_signal(runtime, action, "on_cooldown", actor, status);
    else if (status == WEAPON_FIRE_RELOADING)
        weapon_emit_signal(runtime, action, "on_reloading", actor, status);
    else
        weapon_emit_signal(runtime, action, "on_empty", actor, status);
    return false;
}

static void weapon_fire_commit(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                               slayer3d_registered_actor *actor)
{
    if (actor == NULL || action == NULL)
        return;

    const float ammo_per_shot = SDL_max(json_float(action, "ammo_per_shot", 1.0f), 0.0f);
    const char *clip_property = json_string(action, "clip_property", NULL);
    if (clip_property != NULL && clip_property[0] != '\0')
    {
        const float clip = weapon_actor_numeric_property(actor, clip_property, 0.0f);
        weapon_set_actor_numeric_property(actor, clip_property, SDL_max(clip - ammo_per_shot, 0.0f));
    }
    else
    {
        const char *ammo_resource = json_string(action, "ammo_resource", NULL);
        const char *ammo_property = json_string(action, "ammo_property", ammo_resource);
        if (ammo_property != NULL && ammo_property[0] != '\0')
        {
            const float ammo = weapon_actor_numeric_property(actor, ammo_property, 0.0f);
            weapon_set_actor_numeric_property(actor, ammo_property, SDL_max(ammo - ammo_per_shot, 0.0f));
        }
    }

    const char *cooldown_property = json_string(action, "cooldown_property", "fire_timer");
    if (cooldown_property != NULL && cooldown_property[0] != '\0')
    {
        const float cooldown =
            json_float(action, "cooldown", slayer3d_properties_get_float(actor->props, "fire_cooldown", 0.0f));
        slayer3d_properties_set_float(actor->props, cooldown_property, cooldown);
    }
    weapon_emit_signal(runtime, action, "on_fire", actor, WEAPON_FIRE_READY);
}

bool execute_projectile_fire_action_for_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              const slayer3d_properties *payload,
                                              slayer3d_registered_actor *source_actor)
{
    const char *target_name = json_string(action, "target", NULL);
    slayer3d_registered_actor *target =
        source_actor != NULL ? source_actor : slayer3d_game_data_find_actor(runtime, target_name);
    if (target == NULL)
        target = actor_from_payload_key(runtime, payload, json_string(action, "target_from_payload", NULL));
    if (target == NULL || !runtime_actor_is_active(runtime, target))
        return false;

    if (!weapon_fire_prepare(runtime, action, target))
        return true;

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

    actor_set_position(actor, actor_spawn_position_from_action(runtime, action, payload, target->position, target));
    apply_actor_spawn_properties(actor, obj_get(action, "properties"));
    yyjson_val *velocity = obj_get(action, "velocity");
    const slayer3d_vec3 fallback_velocity = json_vec3_value(velocity, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    const char *velocity_property = json_string(action, "velocity_from_property", NULL);
    if (velocity_property != NULL || velocity != NULL)
    {
        slayer3d_vec3 projectile_velocity =
            velocity_property != NULL
                ? slayer3d_properties_get_vec3(target->props, velocity_property, fallback_velocity)
                : fallback_velocity;
        yyjson_val *speed_value = obj_get(action, "speed");
        if (yyjson_is_num(speed_value))
        {
            const float speed = (float)yyjson_get_num(speed_value);
            if (slayer3d_vec3_length_squared(projectile_velocity) > 0.000001f)
                projectile_velocity = slayer3d_vec3_scale(slayer3d_vec3_normalize(projectile_velocity), speed);
            else
                projectile_velocity = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        }
        slayer3d_properties_set_vec3(actor->props, "velocity", projectile_velocity);
    }
    actor_pool_note_spawn_success(runtime, pool);
    weapon_fire_commit(runtime, action, target);
    return true;
}

static bool value_as_float(const slayer3d_value *value, float *out_value)
{
    if (value == NULL || out_value == NULL)
        return false;
    if (value->type == SLAYER3D_VALUE_INT)
    {
        *out_value = (float)value->as_int;
        return true;
    }
    if (value->type == SLAYER3D_VALUE_FLOAT)
    {
        *out_value = value->as_float;
        return true;
    }
    return false;
}

static bool action_float_value(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                               const slayer3d_properties *payload, const char *key, const char *payload_key,
                               float *out_value)
{
    (void)runtime;
    if (action == NULL || out_value == NULL)
        return false;

    yyjson_val *value = obj_get(action, key);
    if (yyjson_is_num(value))
    {
        *out_value = (float)yyjson_get_num(value);
        return true;
    }

    const char *payload_field = json_string(action, payload_key, NULL);
    if (payload_field != NULL && payload != NULL)
        return value_as_float(slayer3d_properties_get_value(payload, payload_field), out_value);
    return false;
}

void set_actor_numeric_property(slayer3d_registered_actor *actor, const char *key, float value)
{
    if (actor == NULL || actor->props == NULL || key == NULL)
        return;

    const slayer3d_value *existing = slayer3d_properties_get_value(actor->props, key);
    const float rounded = SDL_roundf(value);
    if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && SDL_fabsf(value - rounded) <= 0.0001f)
        slayer3d_properties_set_int(actor->props, key, (int)rounded);
    else
        slayer3d_properties_set_float(actor->props, key, value);
}

float actor_numeric_property(const slayer3d_registered_actor *actor, const char *key, float fallback)
{
    float value = fallback;
    return actor != NULL && actor->props != NULL &&
                   value_as_float(slayer3d_properties_get_value(actor->props, key), &value)
               ? value
               : fallback;
}

static const char *combat_action_property(yyjson_val *action, const char *key, const char *fallback)
{
    const char *value = json_string(action, key, NULL);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static slayer3d_properties *combat_event_payload(slayer3d_registered_actor *target, yyjson_val *action,
                                                 const slayer3d_properties *source_payload, float amount,
                                                 float armor_delta, float health_delta, float health, float max_health,
                                                 float armor, bool alive)
{
    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload == NULL)
        return NULL;

    slayer3d_properties_set_string(event_payload, "actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    const char *source = json_string(action, "source", NULL);
    const char *source_from_payload = json_string(action, "source_from_payload", NULL);
    if (source_from_payload != NULL && source_payload != NULL)
        source = slayer3d_properties_get_string(source_payload, source_from_payload, source);
    slayer3d_properties_set_string(event_payload, "source_actor_name", source != NULL ? source : "");
    slayer3d_properties_set_string(event_payload, "damage_type", json_string(action, "damage_type", ""));
    slayer3d_properties_set_float(event_payload, "amount", amount);
    slayer3d_properties_set_float(event_payload, "armor_delta", armor_delta);
    slayer3d_properties_set_float(event_payload, "health_delta", health_delta);
    slayer3d_properties_set_float(event_payload, "health", health);
    slayer3d_properties_set_float(event_payload, "max_health", max_health);
    slayer3d_properties_set_float(event_payload, "armor", armor);
    slayer3d_properties_set_bool(event_payload, "alive", alive);
    slayer3d_properties_set_bool(event_payload, "dead", !alive);
    return event_payload;
}

static void emit_combat_signal(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *signal_key,
                               const slayer3d_properties *payload)
{
    const int signal_id = action_signal_id(runtime, action, signal_key);
    if (signal_id >= 0 && runtime_bus(runtime) != NULL)
        slayer3d_signal_emit(runtime_bus(runtime), signal_id, payload);
}

bool apply_combat_damage_to_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                  const slayer3d_properties *payload, slayer3d_registered_actor *actor, float amount);

bool execute_combat_damage_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                  const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    float amount = 0.0f;
    if (actor == NULL || !action_float_value(runtime, action, payload, "amount", "amount_from_payload", &amount))
        return false;
    amount = SDL_max(amount, 0.0f);

    return apply_combat_damage_to_actor(runtime, action, payload, actor, amount);
}

bool apply_combat_damage_to_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                  const slayer3d_properties *payload, slayer3d_registered_actor *actor, float amount)
{
    if (actor == NULL)
        return false;
    slayer3d_registered_actor *source = action_source_actor(runtime, action, payload);
    if (!actor_matches_target_filter(runtime, actor, source, action, payload, NULL, false))
        return true;
    amount = SDL_max(amount, 0.0f);

    const char *health_key = combat_action_property(action, "health_property", "health");
    const char *max_health_key = combat_action_property(action, "max_health_property", "max_health");
    const char *armor_key = combat_action_property(action, "armor_property", "armor");
    const char *armor_absorb_key = combat_action_property(action, "armor_absorb_property", "armor_absorb");
    const char *alive_key = combat_action_property(action, "alive_property", "alive");

    const float max_health = SDL_max(actor_numeric_property(actor, max_health_key, 100.0f), 0.0f);
    const float old_health = SDL_clamp(actor_numeric_property(actor, health_key, max_health), 0.0f, max_health);
    const float old_armor = SDL_max(actor_numeric_property(actor, armor_key, 0.0f), 0.0f);
    const float authored_absorb =
        json_float(action, "armor_absorb", actor_numeric_property(actor, armor_absorb_key, 1.0f));
    const float armor_absorb = old_armor > 0.0f ? SDL_clamp(authored_absorb, 0.0f, 1.0f) : 0.0f;
    const float armor_delta = SDL_min(old_armor, amount * armor_absorb);
    const float health_delta = SDL_max(amount - armor_delta, 0.0f);
    const float health = SDL_max(old_health - health_delta, 0.0f);
    const float armor = SDL_max(old_armor - armor_delta, 0.0f);
    const bool was_alive = slayer3d_properties_get_bool(actor->props, alive_key, old_health > 0.0f);
    const bool alive = health > 0.0f;

    set_actor_numeric_property(actor, health_key, health);
    set_actor_numeric_property(actor, armor_key, armor);
    slayer3d_properties_set_bool(actor->props, alive_key, alive);
    if (!alive && json_bool(action, "deactivate_on_death", false))
        actor->active = false;

    slayer3d_properties *event_payload = combat_event_payload(actor, action, payload, amount, armor_delta, health_delta,
                                                              health, max_health, armor, alive);
    if (event_payload != NULL)
    {
        emit_combat_signal(runtime, action, "on_damage", event_payload);
        if (was_alive && !alive)
            emit_combat_signal(runtime, action, "on_death", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

bool execute_combat_heal_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    float amount = 0.0f;
    if (actor == NULL || !action_float_value(runtime, action, payload, "amount", "amount_from_payload", &amount))
        return false;
    amount = SDL_max(amount, 0.0f);

    const char *health_key = combat_action_property(action, "health_property", "health");
    const char *max_health_key = combat_action_property(action, "max_health_property", "max_health");
    const char *alive_key = combat_action_property(action, "alive_property", "alive");
    const float max_health = SDL_max(actor_numeric_property(actor, max_health_key, 100.0f), 0.0f);
    const float old_health = SDL_clamp(actor_numeric_property(actor, health_key, max_health), 0.0f, max_health);
    const bool was_alive = slayer3d_properties_get_bool(actor->props, alive_key, old_health > 0.0f);
    const bool can_revive = json_bool(action, "revive", false);
    const float health = SDL_min(old_health + amount, max_health);
    const bool alive = (was_alive || can_revive) && health > 0.0f;

    set_actor_numeric_property(actor, health_key, health);
    slayer3d_properties_set_bool(actor->props, alive_key, alive);

    slayer3d_properties *event_payload = combat_event_payload(
        actor, action, payload, amount, 0.0f, -amount, health, max_health,
        actor_numeric_property(actor, combat_action_property(action, "armor_property", "armor"), 0.0f), alive);
    if (event_payload != NULL)
    {
        emit_combat_signal(runtime, action, "on_heal", event_payload);
        if (!was_alive && alive)
            emit_combat_signal(runtime, action, "on_revive", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

bool execute_combat_kill_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    if (actor == NULL)
        return false;

    const char *health_key = combat_action_property(action, "health_property", "health");
    const char *max_health_key = combat_action_property(action, "max_health_property", "max_health");
    const char *armor_key = combat_action_property(action, "armor_property", "armor");
    const char *alive_key = combat_action_property(action, "alive_property", "alive");
    const float old_health =
        actor_numeric_property(actor, health_key, actor_numeric_property(actor, max_health_key, 100.0f));
    const bool was_alive = slayer3d_properties_get_bool(actor->props, alive_key, old_health > 0.0f);

    set_actor_numeric_property(actor, health_key, 0.0f);
    slayer3d_properties_set_bool(actor->props, alive_key, false);
    if (json_bool(action, "deactivate", json_bool(action, "deactivate_on_death", false)))
        actor->active = false;

    if (was_alive)
    {
        slayer3d_properties *event_payload =
            combat_event_payload(actor, action, payload, old_health, 0.0f, old_health, 0.0f,
                                 actor_numeric_property(actor, max_health_key, 100.0f),
                                 actor_numeric_property(actor, armor_key, 0.0f), false);
        if (event_payload != NULL)
        {
            emit_combat_signal(runtime, action, "on_death", event_payload);
            slayer3d_properties_destroy(event_payload);
        }
    }
    return true;
}

bool execute_combat_revive_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                  const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    if (actor == NULL)
        return false;

    const char *health_key = combat_action_property(action, "health_property", "health");
    const char *max_health_key = combat_action_property(action, "max_health_property", "max_health");
    const char *armor_key = combat_action_property(action, "armor_property", "armor");
    const char *alive_key = combat_action_property(action, "alive_property", "alive");
    const float max_health = SDL_max(actor_numeric_property(actor, max_health_key, 100.0f), 0.0f);
    float health = max_health;
    (void)action_float_value(runtime, action, payload, "health", "health_from_payload", &health);
    health = SDL_clamp(health, 0.0f, max_health);

    set_actor_numeric_property(actor, health_key, health);
    slayer3d_properties_set_bool(actor->props, alive_key, health > 0.0f);
    actor->active = true;

    slayer3d_properties *event_payload =
        combat_event_payload(actor, action, payload, health, 0.0f, -health, health, max_health,
                             actor_numeric_property(actor, armor_key, 0.0f), health > 0.0f);
    if (event_payload != NULL)
    {
        emit_combat_signal(runtime, action, "on_revive", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

static const char *resource_name(yyjson_val *action)
{
    const char *resource = json_string(action, "resource", NULL);
    return resource != NULL && resource[0] != '\0' ? resource : NULL;
}

static const char *resource_property_name(yyjson_val *json, const char *fallback_resource)
{
    const char *property = json_string(json, "property", NULL);
    if (property != NULL && property[0] != '\0')
        return property;
    return fallback_resource;
}

static const char *resource_max_property_name(yyjson_val *json, const char *resource, char *buffer, size_t buffer_size)
{
    const char *property = json_string(json, "max_property", NULL);
    if (property != NULL && property[0] != '\0')
        return property;
    if (resource == NULL || resource[0] == '\0' || buffer == NULL || buffer_size == 0U)
        return NULL;
    SDL_snprintf(buffer, buffer_size, "max_%s", resource);
    return buffer;
}

static bool resource_numeric_value(yyjson_val *json, const slayer3d_properties *payload, const char *value_key,
                                   const char *payload_key, float *out_value)
{
    return action_float_value(NULL, json, payload, value_key, payload_key, out_value);
}

static slayer3d_properties *resource_event_payload(slayer3d_registered_actor *target, yyjson_val *json,
                                                   const char *resource, float old_value, float value, float max_value,
                                                   float amount, bool success)
{
    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload == NULL)
        return NULL;
    slayer3d_properties_set_string(event_payload, "actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(event_payload, "resource", resource != NULL ? resource : "");
    slayer3d_properties_set_string(
        event_payload, "resource_property",
        resource_property_name(json, resource) != NULL ? resource_property_name(json, resource) : "");
    slayer3d_properties_set_float(event_payload, "old_value", old_value);
    slayer3d_properties_set_float(event_payload, "value", value);
    slayer3d_properties_set_float(event_payload, "max_value", max_value);
    slayer3d_properties_set_float(event_payload, "amount", amount);
    slayer3d_properties_set_bool(event_payload, "success", success);
    return event_payload;
}

void emit_optional_signal(slayer3d_game_data_runtime *runtime, yyjson_val *json, const char *signal_key,
                          const slayer3d_properties *payload)
{
    const int signal_id = action_signal_id(runtime, json, signal_key);
    if (signal_id >= 0 && runtime_bus(runtime) != NULL)
        slayer3d_signal_emit(runtime_bus(runtime), signal_id, payload);
}

static bool apply_resource_delta_to_actor(slayer3d_game_data_runtime *runtime, slayer3d_registered_actor *target,
                                          yyjson_val *json, const slayer3d_properties *payload, float amount,
                                          bool consume)
{
    const char *resource = resource_name(json);
    const char *property = resource_property_name(json, resource);
    char max_property_buffer[128];
    const char *max_property =
        resource_max_property_name(json, resource, max_property_buffer, sizeof(max_property_buffer));
    if (target == NULL || resource == NULL || property == NULL)
        return false;

    const float old_value = actor_numeric_property(target, property, 0.0f);
    const float min_value = json_float(json, "min", 0.0f);
    float max_value = 0.0f;
    const bool has_authored_max = yyjson_is_num(obj_get(json, "max"));
    const bool has_max_property =
        max_property != NULL && slayer3d_properties_get_value(target->props, max_property) != NULL;
    if (has_authored_max)
        max_value = json_float(json, "max", old_value);
    else if (has_max_property)
        max_value = actor_numeric_property(target, max_property, old_value);

    bool success = true;
    float applied = SDL_max(amount, 0.0f);
    float value = old_value;
    if (consume)
    {
        const bool allow_partial = json_bool(json, "allow_partial", false);
        if (old_value + 0.0001f < applied && !allow_partial)
        {
            success = false;
            applied = 0.0f;
        }
        else
        {
            applied = SDL_min(applied, SDL_max(old_value - min_value, 0.0f));
            value = old_value - applied;
        }
    }
    else
    {
        value = old_value + applied;
    }

    if (success && json_bool(json, "clamp", true))
    {
        value = SDL_max(value, min_value);
        if (has_authored_max || has_max_property)
            value = SDL_min(value, max_value);
    }

    if (success)
        set_actor_numeric_property(target, property, value);

    slayer3d_properties *event_payload =
        resource_event_payload(target, json, resource, old_value, success ? value : old_value,
                               (has_authored_max || has_max_property) ? max_value : old_value, applied, success);
    if (event_payload != NULL)
    {
        emit_optional_signal(runtime, json, success ? "on_success" : "on_failure", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    (void)payload;
    return true;
}

bool execute_resource_amount_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                    const slayer3d_properties *payload, bool consume)
{
    slayer3d_registered_actor *target = action_target_actor(runtime, action, payload);
    float amount = 0.0f;
    if (target == NULL || !resource_numeric_value(action, payload, "amount", "amount_from_payload", &amount))
        return false;
    return apply_resource_delta_to_actor(runtime, target, action, payload, amount, consume);
}

bool execute_resource_set_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                 const slayer3d_properties *payload)
{
    slayer3d_registered_actor *target = action_target_actor(runtime, action, payload);
    const char *resource = resource_name(action);
    const char *property = resource_property_name(action, resource);
    float value = 0.0f;
    if (target == NULL || resource == NULL || property == NULL ||
        !resource_numeric_value(action, payload, "value", "value_from_payload", &value))
    {
        return false;
    }

    char max_property_buffer[128];
    const char *max_property =
        resource_max_property_name(action, resource, max_property_buffer, sizeof(max_property_buffer));
    const bool has_authored_max = yyjson_is_num(obj_get(action, "max"));
    const bool has_max_property =
        max_property != NULL && slayer3d_properties_get_value(target->props, max_property) != NULL;
    const float max_value = has_authored_max   ? json_float(action, "max", value)
                            : has_max_property ? actor_numeric_property(target, max_property, value)
                                               : value;
    const float min_value = json_float(action, "min", 0.0f);
    const float old_value = actor_numeric_property(target, property, 0.0f);
    if (json_bool(action, "clamp", true))
    {
        value = SDL_max(value, min_value);
        if (has_authored_max || has_max_property)
            value = SDL_min(value, max_value);
    }
    set_actor_numeric_property(target, property, value);

    slayer3d_properties *event_payload =
        resource_event_payload(target, action, resource, old_value, value,
                               (has_authored_max || has_max_property) ? max_value : value, value - old_value, true);
    if (event_payload != NULL)
    {
        emit_optional_signal(runtime, action, "on_success", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

static bool apply_resource_grants(slayer3d_game_data_runtime *runtime, slayer3d_registered_actor *target,
                                  yyjson_val *grants, const slayer3d_properties *payload)
{
    if (runtime == NULL || target == NULL || !yyjson_is_arr(grants))
        return false;

    bool ok = true;
    for (size_t i = 0; i < yyjson_arr_size(grants); ++i)
    {
        yyjson_val *grant = yyjson_arr_get(grants, i);
        float amount = 0.0f;
        if (!resource_numeric_value(grant, payload, "amount", "amount_from_payload", &amount) ||
            !apply_resource_delta_to_actor(runtime, target, grant, payload, amount, false))
        {
            ok = false;
        }
    }
    return ok;
}

bool execute_pickup_collect_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                   const slayer3d_properties *payload)
{
    slayer3d_registered_actor *collector = action_target_actor(runtime, action, payload);
    slayer3d_registered_actor *pickup = slayer3d_game_data_find_actor(runtime, json_string(action, "pickup", NULL));
    const char *pickup_from_payload = json_string(action, "pickup_from_payload", NULL);
    if (pickup == NULL && pickup_from_payload != NULL)
        pickup = actor_from_payload_key(runtime, payload, pickup_from_payload);
    if (collector == NULL || pickup == NULL || !pickup->active)
        return false;

    if (!apply_resource_grants(runtime, collector, obj_get(action, "resources"), payload))
        return false;

    if (json_bool(action, "deactivate", true))
    {
        pickup->active = false;
        slayer3d_properties_set_bool(pickup->props, json_string(action, "available_property", "pickup_available"),
                                     false);
        const float respawn_seconds = json_float(action, "respawn_seconds", 0.0f);
        if (respawn_seconds > 0.0f)
        {
            slayer3d_properties_set_float(
                pickup->props, json_string(action, "timer_property", "pickup_respawn_remaining"), respawn_seconds);
        }
    }

    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload != NULL)
    {
        slayer3d_properties_set_string(event_payload, "actor_name", collector->name != NULL ? collector->name : "");
        slayer3d_properties_set_string(event_payload, "pickup_actor_name", pickup->name != NULL ? pickup->name : "");
        emit_optional_signal(runtime, action, "on_collected", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

bool execute_resource_station_use_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                         const slayer3d_properties *payload)
{
    slayer3d_registered_actor *target = action_target_actor(runtime, action, payload);
    slayer3d_registered_actor *station = slayer3d_game_data_find_actor(runtime, json_string(action, "station", NULL));
    const char *station_from_payload = json_string(action, "station_from_payload", NULL);
    if (station == NULL && station_from_payload != NULL)
        station = actor_from_payload_key(runtime, payload, station_from_payload);
    if (target == NULL || station == NULL)
        return false;

    const char *cooldown_property = json_string(action, "cooldown_property", "cooldown");
    const char *charges_property = json_string(action, "charges_property", "charges");
    const float cooldown = slayer3d_properties_get_float(station->props, cooldown_property, 0.0f);
    const float charges = actor_numeric_property(station, charges_property, 1.0f);
    const bool use_charge = json_bool(action, "consume_charge", true);
    const bool success = cooldown <= 0.0f && (!use_charge || charges > 0.0f);
    if (success)
    {
        if (!apply_resource_grants(runtime, target, obj_get(action, "resources"), payload))
            return false;
        if (use_charge)
            set_actor_numeric_property(station, charges_property, SDL_max(charges - 1.0f, 0.0f));
        slayer3d_properties_set_float(station->props, cooldown_property, json_float(action, "cooldown", cooldown));
    }

    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload != NULL)
    {
        slayer3d_properties_set_string(event_payload, "actor_name", target->name != NULL ? target->name : "");
        slayer3d_properties_set_string(event_payload, "station_actor_name", station->name != NULL ? station->name : "");
        slayer3d_properties_set_float(event_payload, "charges",
                                      actor_numeric_property(station, charges_property, 0.0f));
        slayer3d_properties_set_float(event_payload, "cooldown",
                                      slayer3d_properties_get_float(station->props, cooldown_property, 0.0f));
        slayer3d_properties_set_bool(event_payload, "success", success);
        emit_optional_signal(runtime, action, success ? "on_success" : "on_failure", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

bool execute_status_effect_apply_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                        const slayer3d_properties *payload)
{
    slayer3d_registered_actor *target = action_target_actor(runtime, action, payload);
    const char *property = json_string(action, "property", NULL);
    const char *duration_property = json_string(action, "duration_property", NULL);
    char duration_buffer[128];
    if (duration_property == NULL && property != NULL)
    {
        SDL_snprintf(duration_buffer, sizeof(duration_buffer), "%s_remaining", property);
        duration_property = duration_buffer;
    }

    slayer3d_value value;
    float duration = 0.0f;
    if (target == NULL || property == NULL || property[0] == '\0' ||
        !json_scalar_to_value(obj_get(action, "value"), &value) ||
        !resource_numeric_value(action, payload, "duration", "duration_from_payload", &duration))
    {
        return false;
    }

    if (!set_property_from_value(target->props, property, &value))
        return false;
    slayer3d_properties_set_float(target->props, duration_property, SDL_max(duration, 0.0f));
    const char *active_property = json_string(action, "active_property", NULL);
    if (active_property != NULL && active_property[0] != '\0')
        slayer3d_properties_set_bool(target->props, active_property, duration > 0.0f);

    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload != NULL)
    {
        slayer3d_properties_set_string(event_payload, "actor_name", target->name != NULL ? target->name : "");
        slayer3d_properties_set_string(event_payload, "property", property);
        slayer3d_properties_set_float(event_payload, "duration", duration);
        emit_optional_signal(runtime, action, "on_apply", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

static bool actor_matches_weapon_target(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_registered_actor *actor, const slayer3d_registered_actor *source,
                                        yyjson_val *action, const char *tag, bool exclude_source)
{
    return actor_matches_target_filter(runtime, actor, source, action, NULL, tag, exclude_source);
}

static bool ray_sphere_intersection(slayer3d_vec3 origin, slayer3d_vec3 direction, slayer3d_vec3 center, float radius,
                                    float max_distance, float *out_distance)
{
    const slayer3d_vec3 oc = slayer3d_vec3_make(origin.x - center.x, origin.y - center.y, origin.z - center.z);
    const float b = oc.x * direction.x + oc.y * direction.y + oc.z * direction.z;
    const float c = slayer3d_vec3_length_squared(oc) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return false;
    const float sqrt_discriminant = SDL_sqrtf(discriminant);
    float t = -b - sqrt_discriminant;
    if (t < 0.0f)
        t = -b + sqrt_discriminant;
    if (t < 0.0f || t > max_distance)
        return false;
    if (out_distance != NULL)
        *out_distance = t;
    return true;
}

static slayer3d_registered_actor *find_hitscan_actor_target(slayer3d_game_data_runtime *runtime,
                                                            const slayer3d_registered_actor *source, yyjson_val *action,
                                                            slayer3d_vec3 origin, slayer3d_vec3 direction,
                                                            float max_distance, float *out_distance)
{
    const char *tag = json_string(action, "target_tag", json_string(action, "hit_tag", NULL));
    const bool exclude_source = json_bool(action, "exclude_source", true);
    float best_distance = max_distance;
    slayer3d_registered_actor *best = NULL;

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (!actor_matches_weapon_target(runtime, actor, source, action, tag, exclude_source))
            continue;
        const float radius =
            SDL_max(weapon_actor_numeric_property(
                        actor, "hit_radius",
                        weapon_actor_numeric_property(actor, "radius", json_float(action, "hit_radius", 0.5f))),
                    0.001f);
        float distance = 0.0f;
        if (ray_sphere_intersection(origin, direction, actor->position, radius, best_distance, &distance))
        {
            best = actor;
            best_distance = distance;
        }
    }

    for (int pool_index = 0; runtime != NULL && pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (!actor_pool_actor_is_active(pool, actor, actor_index) ||
                !actor_matches_weapon_target(runtime, actor, source, action, tag, exclude_source))
            {
                continue;
            }
            const float radius =
                SDL_max(weapon_actor_numeric_property(
                            actor, "hit_radius",
                            weapon_actor_numeric_property(actor, "radius", json_float(action, "hit_radius", 0.5f))),
                        0.001f);
            float distance = 0.0f;
            if (ray_sphere_intersection(origin, direction, actor->position, radius, best_distance, &distance))
            {
                best = actor;
                best_distance = distance;
            }
        }
    }

    if (out_distance != NULL)
        *out_distance = best_distance;
    return best;
}

static slayer3d_vec3 weapon_direction_from_action(const slayer3d_registered_actor *source, yyjson_val *action)
{
    const slayer3d_vec3 fallback = json_vec3_value(obj_get(action, "direction"), slayer3d_vec3_make(0.0f, 0.0f, -1.0f));
    const char *direction_property = json_string(action, "direction_from_property", "camera_forward");
    slayer3d_vec3 direction = direction_property != NULL && source != NULL
                                  ? slayer3d_properties_get_vec3(source->props, direction_property, fallback)
                                  : fallback;
    if (slayer3d_vec3_length_squared(direction) <= 0.000001f)
        direction = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
    return slayer3d_vec3_normalize(direction);
}

static slayer3d_properties *weapon_hitscan_payload(const slayer3d_registered_actor *source,
                                                   const slayer3d_registered_actor *hit, slayer3d_vec3 origin,
                                                   slayer3d_vec3 direction, slayer3d_vec3 hit_position, float distance,
                                                   bool wall_hit,
                                                   const slayer3d_game_data_brush_trace_result *brush_hit)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    slayer3d_properties_set_string(payload, "source_actor_name",
                                   source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_string(payload, "actor_name", hit != NULL && hit->name != NULL ? hit->name : "");
    slayer3d_properties_set_string(payload, "hit_actor_name", hit != NULL && hit->name != NULL ? hit->name : "");
    slayer3d_properties_set_vec3(payload, "origin", origin);
    slayer3d_properties_set_vec3(payload, "direction", direction);
    slayer3d_properties_set_vec3(payload, "hit_position", hit_position);
    slayer3d_properties_set_float(payload, "hit_distance", distance);
    slayer3d_properties_set_bool(payload, "hit_actor", hit != NULL);
    slayer3d_properties_set_bool(payload, "hit_wall", wall_hit);
    slayer3d_properties_set_bool(payload, "hit_brush", brush_hit != NULL && brush_hit->hit);
    slayer3d_properties_set_string(payload, "hit_brush_world",
                                   brush_hit != NULL && brush_hit->world_name != NULL ? brush_hit->world_name : "");
    slayer3d_properties_set_string(payload, "hit_brush_name",
                                   brush_hit != NULL && brush_hit->brush_name != NULL ? brush_hit->brush_name : "");
    slayer3d_properties_set_string(
        payload, "hit_material", brush_hit != NULL && brush_hit->material_name != NULL ? brush_hit->material_name : "");
    slayer3d_properties_set_vec3(payload, "hit_normal",
                                 brush_hit != NULL ? brush_hit->normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_int(payload, "hit_contents",
                                brush_hit != NULL ? (int)SDL_min(brush_hit->contents, (unsigned int)SDL_MAX_SINT32)
                                                  : 0);
    slayer3d_properties_set_int(payload, "hit_surface_flags",
                                brush_hit != NULL ? (int)SDL_min(brush_hit->surface_flags, (unsigned int)SDL_MAX_SINT32)
                                                  : 0);
    return payload;
}

bool execute_weapon_hitscan_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                   const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source = action_target_actor(runtime, action, payload);
    if (source == NULL || !runtime_actor_is_active(runtime, source))
        return false;
    if (!weapon_fire_prepare(runtime, action, source))
        return true;

    const float range = SDL_max(json_float(action, "range", 64.0f), 0.0f);
    slayer3d_vec3 origin = actor_spawn_position_from_action(runtime, action, payload, source->position, source);
    slayer3d_vec3 direction = weapon_direction_from_action(source, action);
    float wall_distance = range;
    bool wall_hit = false;
    slayer3d_vec3 hit_position = slayer3d_vec3_make(origin.x + direction.x * range, origin.y + direction.y * range,
                                                    origin.z + direction.z * range);

    const sector_level_runtime *level = find_sector_level_runtime(runtime, json_string(action, "sector_level", NULL));
    slayer3d_game_data_brush_trace_result brush_result;
    SDL_zero(brush_result);
    bool has_brush_result = false;
    if (level != NULL)
    {
        const slayer3d_level_trace_result trace =
            slayer3d_level_trace_point(&level->lightmapped, level->sectors, origin, direction, range);
        wall_distance = SDL_clamp(trace.fraction, 0.0f, 1.0f) * range;
        wall_hit = trace.hit;
        hit_position = trace.end_point;
    }
    if (obj_get(action, "brush_contents_mask") != NULL || json_bool(action, "trace_brush_worlds", false))
    {
        slayer3d_game_data_brush_trace_desc brush_trace;
        SDL_zero(brush_trace);
        brush_trace.start = origin;
        brush_trace.end = slayer3d_vec3_make(origin.x + direction.x * range, origin.y + direction.y * range,
                                             origin.z + direction.z * range);
        brush_trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
        brush_trace.contents_mask = brush_flags_from_json(
            obj_get(action, "brush_contents_mask"), brush_content_flag_from_string,
            SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP);
        if (slayer3d_game_data_trace_active_brush_worlds(runtime, &brush_trace, &brush_result) && brush_result.hit)
        {
            const float brush_distance = SDL_clamp(brush_result.fraction, 0.0f, 1.0f) * range;
            if (brush_distance < wall_distance)
            {
                wall_distance = brush_distance;
                wall_hit = true;
                hit_position = brush_result.end_position;
                has_brush_result = true;
            }
        }
    }

    float actor_distance = wall_distance;
    slayer3d_registered_actor *hit_actor =
        find_hitscan_actor_target(runtime, source, action, origin, direction, wall_distance, &actor_distance);
    if (hit_actor != NULL)
    {
        hit_position =
            slayer3d_vec3_make(origin.x + direction.x * actor_distance, origin.y + direction.y * actor_distance,
                               origin.z + direction.z * actor_distance);
        wall_hit = false;
    }

    weapon_fire_commit(runtime, action, source);
    slayer3d_properties *hit_payload = weapon_hitscan_payload(
        source, hit_actor, origin, direction, hit_position, hit_actor != NULL ? actor_distance : wall_distance,
        wall_hit, has_brush_result && hit_actor == NULL ? &brush_result : NULL);
    const bool ok =
        execute_optional_action_array(runtime,
                                      hit_actor != NULL || wall_hit || json_bool(action, "run_actions_on_miss", false)
                                          ? obj_get(action, "actions")
                                          : obj_get(action, "miss_actions"),
                                      hit_payload);
    slayer3d_properties_destroy(hit_payload);
    return ok;
}

void weapon_complete_reload(slayer3d_registered_actor *actor, yyjson_val *json)
{
    if (actor == NULL || json == NULL)
        return;

    const char *clip_property = json_string(json, "clip_property", "clip");
    const char *clip_size_property = json_string(json, "clip_size_property", "clip_size");
    const char *reserve_property = json_string(json, "reserve_property", "ammo_reserve");
    const char *pending_property = json_string(json, "reload_pending_property", "reload_pending");
    const char *timer_property = json_string(json, "reload_timer_property", "reload_timer");
    const float clip_size =
        SDL_max(json_float(json, "clip_size", weapon_actor_numeric_property(actor, clip_size_property, 0.0f)), 0.0f);
    const float clip = SDL_clamp(weapon_actor_numeric_property(actor, clip_property, 0.0f), 0.0f, clip_size);
    const bool consume_reserve = json_bool(json, "consume_reserve", true);
    const float reserve =
        consume_reserve ? SDL_max(weapon_actor_numeric_property(actor, reserve_property, 0.0f), 0.0f) : clip_size;
    const float needed = SDL_max(clip_size - clip, 0.0f);
    const float loaded = consume_reserve ? SDL_min(needed, reserve) : needed;
    weapon_set_actor_numeric_property(actor, clip_property, clip + loaded);
    if (consume_reserve)
        weapon_set_actor_numeric_property(actor, reserve_property, reserve - loaded);
    slayer3d_properties_set_bool(actor->props, pending_property, false);
    slayer3d_properties_set_float(actor->props, timer_property, 0.0f);
}

bool execute_weapon_reload_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                  const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    if (actor == NULL)
        return false;

    const char *clip_property = json_string(action, "clip_property", "clip");
    const char *clip_size_property = json_string(action, "clip_size_property", "clip_size");
    const char *reserve_property = json_string(action, "reserve_property", "ammo_reserve");
    const char *pending_property = json_string(action, "reload_pending_property", "reload_pending");
    const char *timer_property = json_string(action, "reload_timer_property", "reload_timer");
    const float clip_size =
        SDL_max(json_float(action, "clip_size", weapon_actor_numeric_property(actor, clip_size_property, 0.0f)), 0.0f);
    const float clip = SDL_clamp(weapon_actor_numeric_property(actor, clip_property, 0.0f), 0.0f, clip_size);
    const bool consume_reserve = json_bool(action, "consume_reserve", true);
    const float reserve = consume_reserve ? weapon_actor_numeric_property(actor, reserve_property, 0.0f) : clip_size;
    if (clip >= clip_size || reserve <= 0.0f)
    {
        weapon_emit_signal(runtime, action, "on_failure", actor, WEAPON_FIRE_EMPTY);
        return true;
    }

    const float reload_seconds = SDL_max(json_float(action, "reload_seconds", 0.0f), 0.0f);
    slayer3d_properties_set_bool(actor->props, pending_property, true);
    slayer3d_properties_set_float(actor->props, timer_property, reload_seconds);
    weapon_emit_signal(runtime, action, "on_reload", actor, WEAPON_FIRE_RELOADING);
    if (reload_seconds <= 0.0f)
        weapon_complete_reload(actor, action);
    return true;
}

bool execute_projectile_fire_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                    const slayer3d_properties *payload)
{
    return execute_projectile_fire_action_for_actor(runtime, action, payload, NULL);
}

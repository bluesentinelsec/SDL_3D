/* Sector door and noise action helpers. */

#include "game_data_internal.h"

static const char *sector_door_action_target_name(yyjson_val *action, const slayer3d_properties *payload)
{
    const char *target_from_payload = json_string(action, "target_from_payload", NULL);
    if (target_from_payload != NULL && payload != NULL)
    {
        const char *target = slayer3d_properties_get_string(payload, target_from_payload, NULL);
        if (target != NULL && target[0] != '\0')
            return target;
    }
    return json_string(action, "target", NULL);
}

bool execute_sector_door_state_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                      const slayer3d_properties *payload, const char *kind)
{
    sector_door_runtime *door = find_sector_door(runtime, sector_door_action_target_name(action, payload));
    if (door == NULL || !sector_door_in_active_scene(runtime, door))
        return false;

    yyjson_val *auto_close = obj_get(action, "stay_open_seconds");
    if (auto_close == NULL)
        auto_close = obj_get(action, "auto_close_seconds");
    if (yyjson_is_num(auto_close))
        slayer3d_door_set_auto_close_delay(&door->door, (float)yyjson_get_num(auto_close));

    if (SDL_strcmp(kind, "open") == 0)
        return slayer3d_door_open(&door->door);
    if (SDL_strcmp(kind, "close") == 0)
        return slayer3d_door_close(&door->door);
    return slayer3d_door_toggle(&door->door);
}

static sector_door_runtime *find_interactable_sector_door(slayer3d_game_data_runtime *runtime,
                                                          const slayer3d_registered_actor *actor, yyjson_val *action)
{
    if (runtime == NULL || actor == NULL)
        return NULL;

    const float range = json_float(action, "range", 2.25f);
    const float min_dot = json_float(action, "min_dot", 0.15f);
    const float yaw = slayer3d_properties_get_float(actor->props, json_string(action, "yaw_property", "yaw"), 0.0f);
    sector_door_runtime *best = NULL;
    float best_distance = 0.0f;
    for (int i = 0; i < runtime->sector_door_count; ++i)
    {
        sector_door_runtime *door = &runtime->sector_doors[i];
        if (!sector_door_in_active_scene(runtime, door) ||
            !slayer3d_door_point_in_interaction_range(&door->door, actor->position, range) ||
            !sector_door_is_in_front(&door->door, actor->position, yaw, min_dot))
        {
            continue;
        }

        const float distance = sector_door_distance_sq_xz(&door->door, actor->position);
        if (best == NULL || distance < best_distance)
        {
            best = door;
            best_distance = distance;
        }
    }
    return best;
}

bool execute_sector_door_interact_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "actor", NULL));
    if (actor == NULL)
        return false;

    sector_door_runtime *door = find_interactable_sector_door(runtime, actor, action);
    if (door == NULL)
        return true;

    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return false;
    slayer3d_properties_set_string(payload, "actor_name", actor->name);
    slayer3d_properties_set_string(payload, "door_name", door->door.name != NULL ? door->door.name : "");
    slayer3d_properties_set_int(payload, "door_id", door->door.door_id);

    bool ok = true;
    yyjson_val *actions = obj_get(action, "actions");
    if (yyjson_is_arr(actions))
    {
        ok = execute_action_array(runtime, actions, payload);
    }
    else
    {
        const int signal_id = action_signal_id(runtime, action, "signal");
        ok = signal_id >= 0 && runtime_bus(runtime) != NULL;
        if (ok)
            slayer3d_signal_emit(runtime_bus(runtime), signal_id, payload);
    }
    slayer3d_properties_destroy(payload);
    return ok;
}

static slayer3d_registered_actor *noise_source_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                     const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source =
        actor_from_payload_key(runtime, payload, json_string(action, "source_from_payload", NULL));
    if (source == NULL)
        source = actor_from_payload_key(runtime, payload, json_string(action, "actor_from_payload", NULL));
    if (source == NULL)
        source = actor_from_payload_key(runtime, payload, json_string(action, "target_from_payload", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "source", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "actor", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
    return source;
}

static bool runtime_add_noise_event(slayer3d_game_data_runtime *runtime, const char *source_actor_name,
                                    slayer3d_vec3 position, float radius, float loudness, float duration)
{
    if (runtime == NULL || radius <= 0.0f || duration <= 0.0f)
        return false;
    if (runtime->noise_event_count >= runtime->noise_event_capacity)
    {
        const int new_capacity = runtime->noise_event_capacity > 0 ? runtime->noise_event_capacity * 2 : 16;
        noise_event_runtime *events =
            (noise_event_runtime *)SDL_realloc(runtime->noise_events, (size_t)new_capacity * sizeof(*events));
        if (events == NULL)
            return false;
        SDL_memset(events + runtime->noise_event_capacity, 0,
                   (size_t)(new_capacity - runtime->noise_event_capacity) * sizeof(*events));
        runtime->noise_events = events;
        runtime->noise_event_capacity = new_capacity;
    }

    noise_event_runtime *event = &runtime->noise_events[runtime->noise_event_count++];
    SDL_zero(*event);
    event->id = ++runtime->next_noise_event_id;
    if (event->id == 0U)
        event->id = ++runtime->next_noise_event_id;
    SDL_snprintf(event->key, sizeof(event->key), "noise.%u", event->id);
    event->source_actor_name = source_actor_name;
    event->position = position;
    event->radius = radius;
    event->loudness = loudness;
    event->remaining_seconds = duration;
    return true;
}

bool execute_noise_emit_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                               const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source = noise_source_actor(runtime, action, payload);
    const slayer3d_vec3 fallback = source != NULL ? source->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const slayer3d_vec3 position = actor_spawn_position_from_action(runtime, action, payload, fallback, source);
    const float radius = SDL_max(json_float(action, "radius", json_float(action, "range", 16.0f)), 0.0f);
    const float loudness = SDL_max(json_float(action, "loudness", 1.0f), 0.0f);
    const float duration = SDL_max(json_float(action, "duration", json_float(action, "duration_seconds", 0.1f)), 0.0f);
    return runtime_add_noise_event(runtime, source != NULL ? source->name : NULL, position, radius, loudness, duration);
}

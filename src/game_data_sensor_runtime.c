/* Sensor runtime update helpers. */

#include "game_data_internal.h"

static void emit_sensor_signal(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                               slayer3d_registered_actor *a, slayer3d_registered_actor *b)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;

    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload != NULL)
    {
        if (a != NULL)
            slayer3d_properties_set_string(payload, "actor_name", a->name);
        if (b != NULL)
            slayer3d_properties_set_string(payload, "other_actor_name", b->name);
    }
    slayer3d_signal_emit(bus, sensor->signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static void execute_sensor_actions(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                   slayer3d_registered_actor *a, slayer3d_registered_actor *b)
{
    if (runtime == NULL || sensor == NULL || !yyjson_is_arr(sensor->actions))
        return;

    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload != NULL)
    {
        if (a != NULL)
            slayer3d_properties_set_string(payload, "actor_name", a->name);
        if (b != NULL)
            slayer3d_properties_set_string(payload, "other_actor_name", b->name);
    }

    for (size_t i = 0; i < yyjson_arr_size(sensor->actions); ++i)
        (void)execute_one_action(runtime, yyjson_arr_get(sensor->actions, i), payload);
    slayer3d_properties_destroy(payload);
}

static void emit_sensor_payload_signal(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                       const slayer3d_properties *payload)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;
    slayer3d_signal_emit(bus, sensor->signal_id, payload);
}

static void execute_sensor_payload_actions(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                           const slayer3d_properties *payload)
{
    if (runtime == NULL || sensor == NULL || !yyjson_is_arr(sensor->actions))
        return;
    (void)execute_action_array(runtime, sensor->actions, payload);
}

bool runtime_actor_is_active(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *actor)
{
    if (runtime == NULL || actor == NULL)
        return false;
    int actor_index = -1;
    const actor_pool_runtime *pool = find_actor_pool_for_actor_const(runtime, actor->name, &actor_index);
    if (pool != NULL)
        return actor_pool_actor_is_active(pool, actor, actor_index);
    return actor->active;
}

bool sensor_actor_list_add(sensor_actor_list *list, slayer3d_registered_actor *actor)
{
    if (list == NULL || actor == NULL)
        return false;
    if (list->count >= list->capacity)
    {
        const int new_capacity = list->capacity > 0 ? list->capacity * 2 : 8;
        slayer3d_registered_actor **items =
            (slayer3d_registered_actor **)SDL_realloc(list->items, (size_t)new_capacity * sizeof(*items));
        if (items == NULL)
            return false;
        list->items = items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = actor;
    return true;
}

void sensor_actor_list_free(sensor_actor_list *list)
{
    if (list == NULL)
        return;
    SDL_free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

bool collect_sensor_endpoint_actors(slayer3d_game_data_runtime *runtime, const char *actor_name, const char *tag,
                                    sensor_actor_list *out_list)
{
    if (runtime == NULL || out_list == NULL)
        return false;

    if (actor_name != NULL)
    {
        if (!active_scene_has_entity_internal(runtime, actor_name))
            return true;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, actor_name);
        if (runtime_actor_is_active(runtime, actor))
            return sensor_actor_list_add(out_list, actor);
        return true;
    }

    if (tag == NULL || tag[0] == '\0')
        return true;

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *tags[1] = {tag};
        if (!entity_json_has_tags(entity, tags, 1))
            continue;
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (runtime_actor_is_active(runtime, actor) && !sensor_actor_list_add(out_list, actor))
            return false;
    }

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        const char *tags[1] = {tag};
        if (!entity_json_has_tags(pool->archetype_json, tags, 1) ||
            !actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
        {
            continue;
        }
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index) && !sensor_actor_list_add(out_list, actor))
                return false;
        }
    }
    return true;
}

static bool sensor_contact_name_is_transient(const char *name)
{
    return name != NULL && SDL_strncmp(name, "noise.", 6) == 0;
}

void sensor_contact_pair_destroy(sensor_contact_pair_state *state)
{
    if (state == NULL)
        return;
    if (state->owns_actor_name)
        SDL_free((char *)state->actor_name);
    if (state->owns_other_actor_name)
        SDL_free((char *)state->other_actor_name);
    SDL_zero(*state);
}

static sensor_contact_pair_state *sensor_contact_pair_state_for(sensor_entry *sensor, const char *actor_name,
                                                                const char *other_actor_name)
{
    if (sensor == NULL || actor_name == NULL || other_actor_name == NULL)
        return NULL;
    for (int i = 0; i < sensor->contact_pair_count; ++i)
    {
        sensor_contact_pair_state *state = &sensor->contact_pairs[i];
        if (state->actor_name != NULL && state->other_actor_name != NULL &&
            SDL_strcmp(state->actor_name, actor_name) == 0 &&
            SDL_strcmp(state->other_actor_name, other_actor_name) == 0)
        {
            return state;
        }
    }

    if (sensor->contact_pair_count >= sensor->contact_pair_capacity)
    {
        const int new_capacity = sensor->contact_pair_capacity > 0 ? sensor->contact_pair_capacity * 2 : 8;
        sensor_contact_pair_state *pairs = (sensor_contact_pair_state *)SDL_realloc(
            sensor->contact_pairs, (size_t)new_capacity * sizeof(*sensor->contact_pairs));
        if (pairs == NULL)
            return NULL;
        SDL_memset(pairs + sensor->contact_pair_capacity, 0,
                   (size_t)(new_capacity - sensor->contact_pair_capacity) * sizeof(*pairs));
        sensor->contact_pairs = pairs;
        sensor->contact_pair_capacity = new_capacity;
    }

    sensor_contact_pair_state *state = &sensor->contact_pairs[sensor->contact_pair_count++];
    SDL_zero(*state);
    if (sensor_contact_name_is_transient(actor_name))
    {
        state->actor_name = SDL_strdup(actor_name);
        state->owns_actor_name = true;
    }
    else
    {
        state->actor_name = actor_name;
    }
    if (sensor_contact_name_is_transient(other_actor_name))
    {
        state->other_actor_name = SDL_strdup(other_actor_name);
        state->owns_other_actor_name = true;
    }
    else
    {
        state->other_actor_name = other_actor_name;
    }
    if ((state->owns_actor_name && state->actor_name == NULL) ||
        (state->owns_other_actor_name && state->other_actor_name == NULL))
    {
        sensor_contact_pair_destroy(state);
        --sensor->contact_pair_count;
        return NULL;
    }
    return state;
}

static bool actors_contact_2d(const slayer3d_registered_actor *a, const slayer3d_registered_actor *b)
{
    if (a == NULL || b == NULL)
        return false;

    const float a_radius = slayer3d_properties_get_float(a->props, "radius", 0.0f);
    const float b_radius = slayer3d_properties_get_float(b->props, "radius", 0.0f);
    const float a_half_width = slayer3d_properties_get_float(a->props, "half_width", 0.0f);
    const float a_half_height = slayer3d_properties_get_float(a->props, "half_height", 0.0f);
    const float b_half_width = slayer3d_properties_get_float(b->props, "half_width", 0.0f);
    const float b_half_height = slayer3d_properties_get_float(b->props, "half_height", 0.0f);

    if (a_radius > 0.0f && b_half_width > 0.0f && b_half_height > 0.0f)
    {
        const float closest_x = SDL_clamp(a->position.x, b->position.x - b_half_width, b->position.x + b_half_width);
        const float closest_y = SDL_clamp(a->position.y, b->position.y - b_half_height, b->position.y + b_half_height);
        const float dx = a->position.x - closest_x;
        const float dy = a->position.y - closest_y;
        return dx * dx + dy * dy <= a_radius * a_radius;
    }
    if (a_radius > 0.0f && b_radius > 0.0f)
    {
        const float dx = a->position.x - b->position.x;
        const float dy = a->position.y - b->position.y;
        const float radius = a_radius + b_radius;
        return dx * dx + dy * dy <= radius * radius;
    }
    if (a_half_width > 0.0f && a_half_height > 0.0f && b_half_width > 0.0f && b_half_height > 0.0f)
    {
        return SDL_fabsf(a->position.x - b->position.x) <= a_half_width + b_half_width &&
               SDL_fabsf(a->position.y - b->position.y) <= a_half_height + b_half_height;
    }
    if (a_half_width > 0.0f && a_half_height > 0.0f && b_radius > 0.0f)
    {
        return actors_contact_2d(b, a);
    }
    return false;
}

static void sensor_contact_states_begin(sensor_entry *sensor)
{
    if (sensor == NULL)
        return;
    for (int i = 0; i < sensor->contact_pair_count; ++i)
        sensor->contact_pairs[i].seen = false;
}

static void sensor_contact_states_end(sensor_entry *sensor)
{
    if (sensor == NULL)
        return;
    bool any_active = false;
    int write_index = 0;
    for (int i = 0; i < sensor->contact_pair_count; ++i)
    {
        sensor_contact_pair_state *state = &sensor->contact_pairs[i];
        if (!state->seen)
        {
            if (sensor_contact_name_is_transient(state->actor_name) ||
                sensor_contact_name_is_transient(state->other_actor_name))
            {
                sensor_contact_pair_destroy(state);
                continue;
            }
            state->active = false;
        }
        any_active = any_active || state->active;
        if (write_index != i)
            sensor->contact_pairs[write_index] = *state;
        ++write_index;
    }
    sensor->contact_pair_count = write_index;
    sensor->was_active = any_active;
}

static void update_sensor_contact_pair(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                       slayer3d_registered_actor *actor, slayer3d_registered_actor *other)
{
    if (!runtime_actor_is_active(runtime, actor) || !runtime_actor_is_active(runtime, other))
        return;

    sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, actor->name, other->name);
    if (state == NULL)
        return;

    const bool active = actors_contact_2d(actor, other);
    if (active && !actor_matches_target_filter(runtime, other, actor, sensor->json, NULL, sensor->other_tag, false))
    {
        state->active = false;
        state->seen = true;
        return;
    }
    const bool stay =
        sensor->edge != NULL && (SDL_strcmp(sensor->edge, "stay") == 0 || SDL_strcmp(sensor->edge, "overlap") == 0);
    if (active && (stay || !state->active))
    {
        emit_sensor_signal(runtime, sensor, actor, other);
        execute_sensor_actions(runtime, sensor, actor, other);
    }
    state->active = active;
    state->seen = true;
}

static void update_contact_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list actors;
    sensor_actor_list others;
    SDL_zero(actors);
    SDL_zero(others);

    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &actors) &&
        collect_sensor_endpoint_actors(runtime, sensor->other, sensor->other_tag, &others))
    {
        for (int a = 0; a < actors.count; ++a)
        {
            for (int b = 0; b < others.count; ++b)
            {
                if (actors.items[a] != others.items[b])
                    update_sensor_contact_pair(runtime, sensor, actors.items[a], others.items[b]);
            }
        }
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&actors);
    sensor_actor_list_free(&others);
}

static bool sensor_edge_is_stay(const sensor_entry *sensor)
{
    return sensor != NULL && sensor->edge != NULL &&
           (SDL_strcmp(sensor->edge, "stay") == 0 || SDL_strcmp(sensor->edge, "overlap") == 0);
}

static bool sensor_edge_is_exit(const sensor_entry *sensor)
{
    return sensor != NULL && sensor->edge != NULL && SDL_strcmp(sensor->edge, "exit") == 0;
}

static unsigned int sensor_brush_contents_mask(const sensor_entry *sensor, unsigned int fallback)
{
    return brush_flags_from_json(sensor != NULL ? obj_get(sensor->json, "contents_mask") : NULL,
                                 brush_content_flag_from_string, fallback);
}

static bool trace_active_brush_point_contents(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 point,
                                              unsigned int contents_mask,
                                              slayer3d_game_data_brush_trace_result *out_result)
{
    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.start = point;
    trace.end = point;
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.contents_mask = contents_mask;
    return slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, out_result);
}

static void brush_trace_payload_fields(slayer3d_properties *payload,
                                       const slayer3d_game_data_brush_trace_result *result)
{
    if (payload == NULL)
        return;
    const bool hit = result != NULL && result->hit;
    slayer3d_properties_set_bool(payload, "hit_brush", hit);
    slayer3d_properties_set_string(payload, "brush_world", hit && result->world_name != NULL ? result->world_name : "");
    slayer3d_properties_set_string(payload, "hit_brush_world",
                                   hit && result->world_name != NULL ? result->world_name : "");
    slayer3d_properties_set_string(payload, "brush_name", hit && result->brush_name != NULL ? result->brush_name : "");
    slayer3d_properties_set_string(payload, "hit_brush_name",
                                   hit && result->brush_name != NULL ? result->brush_name : "");
    slayer3d_properties_set_string(payload, "material",
                                   hit && result->material_name != NULL ? result->material_name : "");
    slayer3d_properties_set_string(payload, "hit_material",
                                   hit && result->material_name != NULL ? result->material_name : "");
    slayer3d_properties_set_vec3(payload, "hit_position",
                                 hit ? result->end_position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(payload, "hit_normal", hit ? result->normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_float(payload, "hit_fraction", hit ? result->fraction : 1.0f);
    slayer3d_properties_set_int(payload, "contents",
                                hit ? (int)SDL_min(result->contents, (unsigned int)SDL_MAX_SINT32) : 0);
    slayer3d_properties_set_int(payload, "hit_contents",
                                hit ? (int)SDL_min(result->contents, (unsigned int)SDL_MAX_SINT32) : 0);
    slayer3d_properties_set_int(payload, "surface_flags",
                                hit ? (int)SDL_min(result->surface_flags, (unsigned int)SDL_MAX_SINT32) : 0);
    slayer3d_properties_set_int(payload, "hit_surface_flags",
                                hit ? (int)SDL_min(result->surface_flags, (unsigned int)SDL_MAX_SINT32) : 0);
}

static slayer3d_properties *create_brush_contents_sensor_payload(const sensor_entry *sensor,
                                                                 const slayer3d_registered_actor *actor,
                                                                 const slayer3d_game_data_brush_trace_result *result)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    slayer3d_properties_set_string(payload, "actor_name", actor != NULL && actor->name != NULL ? actor->name : "");
    slayer3d_properties_set_string(payload, "sensor_name", sensor != NULL && sensor->name != NULL ? sensor->name : "");
    slayer3d_properties_set_vec3(payload, "point",
                                 actor != NULL ? actor->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    brush_trace_payload_fields(payload, result);
    return payload;
}

static void update_brush_contents_sensor_actor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                               slayer3d_registered_actor *actor)
{
    if (!runtime_actor_is_active(runtime, actor))
        return;

    slayer3d_game_data_brush_trace_result result;
    const unsigned int mask = sensor_brush_contents_mask(sensor, SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER);
    const bool traced = trace_active_brush_point_contents(runtime, actor->position, mask, &result);
    const bool active =
        traced && result.hit && result.start_solid &&
        actor_matches_target_filter(runtime, actor, NULL, sensor->json, NULL, sensor->entity_tag, false);
    sensor_contact_pair_state *state = sensor_contact_pair_state_for(
        sensor, actor != NULL ? actor->name : NULL, sensor->name != NULL ? sensor->name : "brush_contents");
    if (state == NULL)
        return;

    const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                             : sensor_edge_is_stay(sensor) ? active
                                                           : (active && !state->active);
    if (should_emit)
    {
        slayer3d_properties *payload = create_brush_contents_sensor_payload(sensor, actor, active ? &result : NULL);
        emit_sensor_payload_signal(runtime, sensor, payload);
        execute_sensor_payload_actions(runtime, sensor, payload);
        slayer3d_properties_destroy(payload);
    }
    state->active = active;
    state->seen = true;
}

static void update_brush_contents_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list actors;
    SDL_zero(actors);
    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &actors))
    {
        for (int i = 0; i < actors.count; ++i)
            update_brush_contents_sensor_actor(runtime, sensor, actors.items[i]);
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&actors);
}

static int sensor_target_sector_index(const sector_level_runtime *level, const sensor_entry *sensor)
{
    if (level == NULL || sensor == NULL)
        return -1;
    if (sensor->sector_index >= 0)
        return sensor->sector_index < level->sector_count ? sensor->sector_index : -1;
    return sector_level_find_sector_name(level, sensor->sector);
}

int actor_sector_index_for_sensor(const sector_level_runtime *level, const sensor_entry *sensor,
                                  const slayer3d_registered_actor *actor)
{
    if (level == NULL || actor == NULL)
        return -1;

    const char *sector_property =
        sensor != NULL && sensor->sector_property != NULL ? sensor->sector_property : "current_sector";
    const slayer3d_value *current_sector = slayer3d_properties_get_value(actor->props, sector_property);
    if (current_sector != NULL && current_sector->type == SLAYER3D_VALUE_INT && current_sector->as_int >= 0 &&
        current_sector->as_int < level->sector_count)
    {
        return current_sector->as_int;
    }

    return slayer3d_level_find_sector_at(&level->lightmapped, level->sectors, actor->position.x, actor->position.z,
                                         actor->position.y);
}

static slayer3d_properties *create_sector_sensor_payload(const slayer3d_game_data_runtime *runtime,
                                                         const sensor_entry *sensor, const sector_level_runtime *level,
                                                         const slayer3d_registered_actor *actor, int sector_index)
{
    (void)sensor;
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    slayer3d_properties_set_string(payload, "actor_name", actor != NULL && actor->name != NULL ? actor->name : "");
    slayer3d_properties_set_string(payload, "sector_level", level != NULL && level->name != NULL ? level->name : "");
    slayer3d_properties_set_int(payload, "sector_index", sector_index);
    slayer3d_properties_set_string(payload, "sector_name",
                                   level != NULL && sector_index >= 0 && sector_index < level->sector_count &&
                                           level->sector_names[sector_index] != NULL
                                       ? level->sector_names[sector_index]
                                       : "");
    const slayer3d_sector *sector =
        level != NULL && sector_index >= 0 && sector_index < level->sector_count ? &level->sectors[sector_index] : NULL;
    const float damage_per_second = slayer3d_sector_damage_per_second(sector);
    slayer3d_properties_set_float(payload, "sector_damage_per_second", damage_per_second);
    slayer3d_properties_set_float(
        payload, "sector_damage_delta",
        slayer3d_sector_damage_for_delta(sector, runtime != NULL ? runtime->current_dt : 0.0f));
    slayer3d_properties_set_int(payload, "ambient_sound_id", sector != NULL ? sector->ambient_sound_id : -1);
    return payload;
}

static void emit_sector_sensor_signal(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                      const sector_level_runtime *level, slayer3d_registered_actor *actor,
                                      int sector_index)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;

    slayer3d_properties *payload = create_sector_sensor_payload(runtime, sensor, level, actor, sector_index);
    slayer3d_signal_emit(bus, sensor->signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static void execute_sector_sensor_actions(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                          const sector_level_runtime *level, slayer3d_registered_actor *actor,
                                          int sector_index)
{
    if (runtime == NULL || sensor == NULL || !yyjson_is_arr(sensor->actions))
        return;

    slayer3d_properties *payload = create_sector_sensor_payload(runtime, sensor, level, actor, sector_index);
    if (payload != NULL)
        (void)execute_action_array(runtime, sensor->actions, payload);
    slayer3d_properties_destroy(payload);
}

static void update_sector_sensor_actor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                       const sector_level_runtime *level, int target_sector,
                                       slayer3d_registered_actor *actor)
{
    if (!runtime_actor_is_active(runtime, actor))
        return;

    const int current_sector = actor_sector_index_for_sensor(level, sensor, actor);
    const bool active =
        current_sector == target_sector &&
        actor_matches_target_filter(runtime, actor, NULL, sensor->json, NULL, sensor->entity_tag, false);
    const char *sector_name = "";
    if (target_sector >= 0 && target_sector < level->sector_count && level->sector_names[target_sector] != NULL)
        sector_name = level->sector_names[target_sector];
    sensor_contact_pair_state *state =
        sensor_contact_pair_state_for(sensor, actor != NULL ? actor->name : NULL, sector_name);
    if (state == NULL)
        return;

    const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                             : sensor_edge_is_stay(sensor) ? active
                                                           : (active && !state->active);
    if (should_emit)
    {
        const int event_sector = active ? current_sector : target_sector;
        emit_sector_sensor_signal(runtime, sensor, level, actor, event_sector);
        execute_sector_sensor_actions(runtime, sensor, level, actor, event_sector);
    }
    state->active = active;
    state->seen = true;
}

static void update_sector_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    const sector_level_runtime *level = find_sector_level_runtime(runtime, sensor->sector_level);
    const int target_sector = sensor_target_sector_index(level, sensor);
    if (level == NULL || target_sector < 0)
        return;

    sensor_actor_list actors;
    SDL_zero(actors);
    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &actors))
    {
        for (int i = 0; i < actors.count; ++i)
            update_sector_sensor_actor(runtime, sensor, level, target_sector, actors.items[i]);
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&actors);
}

static bool point_in_sensor_volume(const sensor_entry *sensor, slayer3d_vec3 position)
{
    if (sensor == NULL)
        return false;
    return position.x >= sensor->volume_min.x && position.x <= sensor->volume_max.x &&
           position.y >= sensor->volume_min.y && position.y <= sensor->volume_max.y &&
           position.z >= sensor->volume_min.z && position.z <= sensor->volume_max.z;
}

static void update_volume_sensor_actor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                       slayer3d_registered_actor *actor)
{
    if (!runtime_actor_is_active(runtime, actor))
        return;

    sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, actor != NULL ? actor->name : NULL,
                                                                     sensor->name != NULL ? sensor->name : "volume");
    if (state == NULL)
        return;

    const bool active =
        point_in_sensor_volume(sensor, actor->position) &&
        actor_matches_target_filter(runtime, actor, NULL, sensor->json, NULL, sensor->entity_tag, false);
    const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                             : sensor_edge_is_stay(sensor) ? active
                                                           : (active && !state->active);
    if (should_emit)
    {
        emit_sensor_signal(runtime, sensor, actor, NULL);
        execute_sensor_actions(runtime, sensor, actor, NULL);
    }
    state->active = active;
    state->seen = true;
}

static void update_volume_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list actors;
    SDL_zero(actors);
    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &actors))
    {
        for (int i = 0; i < actors.count; ++i)
            update_volume_sensor_actor(runtime, sensor, actors.items[i]);
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&actors);
}

static bool perception_actor_in_field_of_view(const sensor_entry *sensor, const slayer3d_registered_actor *observer,
                                              const slayer3d_registered_actor *target)
{
    if (sensor == NULL || observer == NULL || target == NULL)
        return false;
    if (sensor->min_dot <= -1.0f)
        return true;

    const float yaw = slayer3d_properties_get_float(observer->props, sensor->yaw_property, 0.0f);
    const slayer3d_vec3 forward = slayer3d_vec3_make(SDL_sinf(yaw), 0.0f, -SDL_cosf(yaw));
    slayer3d_vec3 to_target =
        slayer3d_vec3_make(target->position.x - observer->position.x, 0.0f, target->position.z - observer->position.z);
    if (slayer3d_vec3_length_squared(to_target) <= 0.000001f)
        return true;
    to_target = slayer3d_vec3_normalize(to_target);
    return slayer3d_vec3_dot(forward, to_target) >= sensor->min_dot;
}

static bool perception_sensor_has_line_of_sight(const sensor_entry *sensor, const sector_level_runtime *level,
                                                const slayer3d_registered_actor *observer,
                                                const slayer3d_registered_actor *target, float *out_distance,
                                                slayer3d_vec3 *out_origin, slayer3d_vec3 *out_target)
{
    if (sensor == NULL || level == NULL || observer == NULL || target == NULL)
        return false;

    const slayer3d_vec3 origin = slayer3d_vec3_make(
        observer->position.x, observer->position.y + sensor->observer_eye_height, observer->position.z);
    const slayer3d_vec3 target_point =
        slayer3d_vec3_make(target->position.x, target->position.y + sensor->target_eye_height, target->position.z);
    const slayer3d_vec3 delta = slayer3d_vec3_sub(target_point, origin);
    const float distance = slayer3d_vec3_length(delta);
    if (distance <= 0.000001f || distance > sensor->range)
        return false;

    if (out_distance != NULL)
        *out_distance = distance;
    if (out_origin != NULL)
        *out_origin = origin;
    if (out_target != NULL)
        *out_target = target_point;

    const slayer3d_vec3 direction = slayer3d_vec3_scale(delta, 1.0f / distance);
    const slayer3d_level_trace_result trace =
        slayer3d_level_trace_point(&level->lightmapped, level->sectors, origin, direction, distance);
    return !trace.hit || SDL_clamp(trace.fraction, 0.0f, 1.0f) >= 0.995f;
}

static bool brush_perception_sensor_has_line_of_sight(const slayer3d_game_data_runtime *runtime,
                                                      const sensor_entry *sensor,
                                                      const slayer3d_registered_actor *observer,
                                                      const slayer3d_registered_actor *target, float *out_distance,
                                                      slayer3d_vec3 *out_origin, slayer3d_vec3 *out_target,
                                                      slayer3d_game_data_brush_trace_result *out_trace)
{
    if (runtime == NULL || sensor == NULL || observer == NULL || target == NULL)
        return false;
    if (out_trace != NULL)
        SDL_zero(*out_trace);

    const slayer3d_vec3 origin = slayer3d_vec3_make(
        observer->position.x, observer->position.y + sensor->observer_eye_height, observer->position.z);
    const slayer3d_vec3 target_point =
        slayer3d_vec3_make(target->position.x, target->position.y + sensor->target_eye_height, target->position.z);
    const slayer3d_vec3 delta = slayer3d_vec3_sub(target_point, origin);
    const float distance = slayer3d_vec3_length(delta);
    if (distance <= 0.000001f || distance > sensor->range)
        return false;

    if (out_distance != NULL)
        *out_distance = distance;
    if (out_origin != NULL)
        *out_origin = origin;
    if (out_target != NULL)
        *out_target = target_point;

    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.start = origin;
    trace.end = target_point;
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.contents_mask = sensor_brush_contents_mask(sensor, SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID |
                                                                 SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP);
    slayer3d_game_data_brush_trace_result result;
    if (!slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, &result))
        return false;
    if (out_trace != NULL)
        *out_trace = result;
    return !result.hit || SDL_clamp(result.fraction, 0.0f, 1.0f) >= 0.995f;
}

static slayer3d_properties *create_perception_sensor_payload(const sensor_entry *sensor,
                                                             const sector_level_runtime *level,
                                                             const slayer3d_registered_actor *observer,
                                                             const slayer3d_registered_actor *target, float distance,
                                                             slayer3d_vec3 origin, slayer3d_vec3 target_point)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    slayer3d_properties_set_string(payload, "actor_name",
                                   observer != NULL && observer->name != NULL ? observer->name : "");
    slayer3d_properties_set_string(payload, "observer_actor_name",
                                   observer != NULL && observer->name != NULL ? observer->name : "");
    slayer3d_properties_set_string(payload, "target_actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(payload, "other_actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(payload, "sector_level", level != NULL && level->name != NULL ? level->name : "");
    slayer3d_properties_set_float(payload, "distance", distance);
    slayer3d_properties_set_float(payload, "perception_distance", distance);
    slayer3d_properties_set_float(payload, "range", sensor != NULL ? sensor->range : 0.0f);
    slayer3d_properties_set_vec3(payload, "origin", origin);
    slayer3d_properties_set_vec3(payload, "target_position", target_point);
    return payload;
}

static void emit_perception_sensor_signal(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                          const sector_level_runtime *level, slayer3d_registered_actor *observer,
                                          slayer3d_registered_actor *target, float distance, slayer3d_vec3 origin,
                                          slayer3d_vec3 target_point)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;

    slayer3d_properties *payload =
        create_perception_sensor_payload(sensor, level, observer, target, distance, origin, target_point);
    slayer3d_signal_emit(bus, sensor->signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static void execute_perception_sensor_actions(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                              const sector_level_runtime *level, slayer3d_registered_actor *observer,
                                              slayer3d_registered_actor *target, float distance, slayer3d_vec3 origin,
                                              slayer3d_vec3 target_point)
{
    if (runtime == NULL || sensor == NULL || !yyjson_is_arr(sensor->actions))
        return;

    slayer3d_properties *payload =
        create_perception_sensor_payload(sensor, level, observer, target, distance, origin, target_point);
    if (payload != NULL)
        (void)execute_action_array(runtime, sensor->actions, payload);
    slayer3d_properties_destroy(payload);
}

static slayer3d_properties *create_brush_perception_sensor_payload(const sensor_entry *sensor,
                                                                   const slayer3d_registered_actor *observer,
                                                                   const slayer3d_registered_actor *target,
                                                                   float distance, slayer3d_vec3 origin,
                                                                   slayer3d_vec3 target_point, bool visible,
                                                                   const slayer3d_game_data_brush_trace_result *trace)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    slayer3d_properties_set_string(payload, "actor_name",
                                   observer != NULL && observer->name != NULL ? observer->name : "");
    slayer3d_properties_set_string(payload, "observer_actor_name",
                                   observer != NULL && observer->name != NULL ? observer->name : "");
    slayer3d_properties_set_string(payload, "target_actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(payload, "other_actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(payload, "sensor_name", sensor != NULL && sensor->name != NULL ? sensor->name : "");
    slayer3d_properties_set_bool(payload, "line_of_sight", visible);
    slayer3d_properties_set_bool(payload, "brush_line_of_sight", visible);
    slayer3d_properties_set_float(payload, "distance", distance);
    slayer3d_properties_set_float(payload, "perception_distance", distance);
    slayer3d_properties_set_float(payload, "range", sensor != NULL ? sensor->range : 0.0f);
    slayer3d_properties_set_vec3(payload, "origin", origin);
    slayer3d_properties_set_vec3(payload, "target_position", target_point);
    brush_trace_payload_fields(payload, trace);
    return payload;
}

static void update_brush_perception_sensor_pair(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                                slayer3d_registered_actor *observer, slayer3d_registered_actor *target)
{
    if (!runtime_actor_is_active(runtime, observer) || !runtime_actor_is_active(runtime, target) || observer == target)
        return;

    sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, observer->name, target->name);
    if (state == NULL)
        return;

    float distance = 0.0f;
    slayer3d_vec3 origin = observer->position;
    slayer3d_vec3 target_point = target->position;
    slayer3d_game_data_brush_trace_result trace;
    SDL_zero(trace);
    bool active = actor_matches_target_filter(runtime, target, observer, sensor->json, NULL, sensor->other_tag, true) &&
                  perception_actor_in_field_of_view(sensor, observer, target) &&
                  brush_perception_sensor_has_line_of_sight(runtime, sensor, observer, target, &distance, &origin,
                                                            &target_point, &trace);

    const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                             : sensor_edge_is_stay(sensor) ? active
                                                           : (active && !state->active);
    if (should_emit)
    {
        slayer3d_properties *payload = create_brush_perception_sensor_payload(
            sensor, observer, target, distance, origin, target_point, active, trace.hit ? &trace : NULL);
        emit_sensor_payload_signal(runtime, sensor, payload);
        execute_sensor_payload_actions(runtime, sensor, payload);
        slayer3d_properties_destroy(payload);
    }
    state->active = active;
    state->seen = true;
}

static void update_brush_perception_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list observers;
    sensor_actor_list targets;
    SDL_zero(observers);
    SDL_zero(targets);

    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &observers) &&
        collect_sensor_endpoint_actors(runtime, sensor->other, sensor->other_tag, &targets))
    {
        for (int observer_index = 0; observer_index < observers.count; ++observer_index)
        {
            for (int target_index = 0; target_index < targets.count; ++target_index)
            {
                update_brush_perception_sensor_pair(runtime, sensor, observers.items[observer_index],
                                                    targets.items[target_index]);
            }
        }
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&observers);
    sensor_actor_list_free(&targets);
}

static void update_perception_sensor_pair(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                          const sector_level_runtime *level, slayer3d_registered_actor *observer,
                                          slayer3d_registered_actor *target)
{
    if (!runtime_actor_is_active(runtime, observer) || !runtime_actor_is_active(runtime, target) || observer == target)
        return;

    sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, observer->name, target->name);
    if (state == NULL)
        return;

    float distance = 0.0f;
    slayer3d_vec3 origin = observer->position;
    slayer3d_vec3 target_point = target->position;
    bool active =
        actor_matches_target_filter(runtime, target, observer, sensor->json, NULL, sensor->other_tag, true) &&
        perception_actor_in_field_of_view(sensor, observer, target) &&
        perception_sensor_has_line_of_sight(sensor, level, observer, target, &distance, &origin, &target_point);

    const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                             : sensor_edge_is_stay(sensor) ? active
                                                           : (active && !state->active);
    if (should_emit)
    {
        emit_perception_sensor_signal(runtime, sensor, level, observer, target, distance, origin, target_point);
        execute_perception_sensor_actions(runtime, sensor, level, observer, target, distance, origin, target_point);
    }
    state->active = active;
    state->seen = true;
}

static void update_perception_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    const sector_level_runtime *level = find_sector_level_runtime(runtime, sensor->sector_level);
    if (level == NULL)
        return;

    sensor_actor_list observers;
    sensor_actor_list targets;
    SDL_zero(observers);
    SDL_zero(targets);

    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &observers) &&
        collect_sensor_endpoint_actors(runtime, sensor->other, sensor->other_tag, &targets))
    {
        for (int observer_index = 0; observer_index < observers.count; ++observer_index)
        {
            for (int target_index = 0; target_index < targets.count; ++target_index)
            {
                update_perception_sensor_pair(runtime, sensor, level, observers.items[observer_index],
                                              targets.items[target_index]);
            }
        }
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&observers);
    sensor_actor_list_free(&targets);
}

static float noise_event_audibility(const sensor_entry *sensor, const noise_event_runtime *event,
                                    const slayer3d_registered_actor *listener, float *out_distance)
{
    if (sensor == NULL || event == NULL || listener == NULL)
        return 0.0f;
    const slayer3d_vec3 delta = slayer3d_vec3_sub(event->position, listener->position);
    const float distance = slayer3d_vec3_length(delta);
    if (out_distance != NULL)
        *out_distance = distance;
    const float radius = SDL_min(sensor->range, event->radius);
    if (radius <= 0.0f || distance > radius)
        return 0.0f;
    return event->loudness * SDL_clamp(1.0f - distance / radius, 0.0f, 1.0f);
}

static slayer3d_properties *create_hearing_sensor_payload(const sensor_entry *sensor, const noise_event_runtime *event,
                                                          const slayer3d_registered_actor *listener,
                                                          const slayer3d_registered_actor *source, float distance,
                                                          float audibility)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    slayer3d_properties_set_string(payload, "actor_name",
                                   listener != NULL && listener->name != NULL ? listener->name : "");
    slayer3d_properties_set_string(payload, "listener_actor_name",
                                   listener != NULL && listener->name != NULL ? listener->name : "");
    slayer3d_properties_set_string(payload, "source_actor_name",
                                   event != NULL && event->source_actor_name != NULL ? event->source_actor_name : "");
    slayer3d_properties_set_string(payload, "target_actor_name",
                                   source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_int(payload, "noise_id", event != NULL ? (int)event->id : 0);
    slayer3d_properties_set_vec3(payload, "noise_position",
                                 event != NULL ? event->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_float(payload, "noise_radius", event != NULL ? event->radius : 0.0f);
    slayer3d_properties_set_float(payload, "noise_loudness", event != NULL ? event->loudness : 0.0f);
    slayer3d_properties_set_float(payload, "distance", distance);
    slayer3d_properties_set_float(payload, "hearing_distance", distance);
    slayer3d_properties_set_float(payload, "audibility", audibility);
    slayer3d_properties_set_float(payload, "range", sensor != NULL ? sensor->range : 0.0f);
    return payload;
}

static void emit_hearing_sensor_signal(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                       const noise_event_runtime *event, slayer3d_registered_actor *listener,
                                       slayer3d_registered_actor *source, float distance, float audibility)
{
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL || sensor == NULL || sensor->signal_id < 0)
        return;

    slayer3d_properties *payload = create_hearing_sensor_payload(sensor, event, listener, source, distance, audibility);
    slayer3d_signal_emit(bus, sensor->signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static void execute_hearing_sensor_actions(slayer3d_game_data_runtime *runtime, const sensor_entry *sensor,
                                           const noise_event_runtime *event, slayer3d_registered_actor *listener,
                                           slayer3d_registered_actor *source, float distance, float audibility)
{
    if (runtime == NULL || sensor == NULL || !yyjson_is_arr(sensor->actions))
        return;

    slayer3d_properties *payload = create_hearing_sensor_payload(sensor, event, listener, source, distance, audibility);
    if (payload != NULL)
        (void)execute_action_array(runtime, sensor->actions, payload);
    slayer3d_properties_destroy(payload);
}

static void update_hearing_sensor_listener(slayer3d_game_data_runtime *runtime, sensor_entry *sensor,
                                           slayer3d_registered_actor *listener)
{
    if (!runtime_actor_is_active(runtime, listener))
        return;

    for (int event_index = 0; event_index < runtime->noise_event_count; ++event_index)
    {
        const noise_event_runtime *event = &runtime->noise_events[event_index];
        if (event->remaining_seconds <= 0.0f)
            continue;

        slayer3d_registered_actor *source = slayer3d_game_data_find_actor(runtime, event->source_actor_name);
        if (event->source_actor_name != NULL &&
            !actor_matches_target_filter(runtime, source, listener, sensor->json, NULL, sensor->other_tag, true))
        {
            continue;
        }

        float distance = 0.0f;
        const float audibility = noise_event_audibility(sensor, event, listener, &distance);
        const bool active = audibility > 0.0f;
        sensor_contact_pair_state *state = sensor_contact_pair_state_for(sensor, listener->name, event->key);
        if (state == NULL)
            continue;

        const bool should_emit = sensor_edge_is_exit(sensor)   ? (!active && state->active)
                                 : sensor_edge_is_stay(sensor) ? active
                                                               : (active && !state->active);
        if (should_emit)
        {
            emit_hearing_sensor_signal(runtime, sensor, event, listener, source, distance, audibility);
            execute_hearing_sensor_actions(runtime, sensor, event, listener, source, distance, audibility);
        }
        state->active = active;
        state->seen = true;
    }
}

static void update_hearing_sensor(slayer3d_game_data_runtime *runtime, sensor_entry *sensor)
{
    sensor_actor_list listeners;
    SDL_zero(listeners);

    sensor_contact_states_begin(sensor);
    if (collect_sensor_endpoint_actors(runtime, sensor->entity, sensor->entity_tag, &listeners))
    {
        for (int i = 0; i < listeners.count; ++i)
            update_hearing_sensor_listener(runtime, sensor, listeners.items[i]);
    }
    sensor_contact_states_end(sensor);
    sensor_actor_list_free(&listeners);
}

void update_sensors(slayer3d_game_data_runtime *runtime)
{
    for (int i = 0; i < runtime->sensor_count; ++i)
    {
        sensor_entry *sensor = &runtime->sensors[i];
        if (sensor->active_if != NULL && !eval_data_condition(runtime, sensor->active_if, NULL))
        {
            sensor->was_active = false;
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_CONTACT_2D)
        {
            update_contact_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_SECTOR)
        {
            update_sector_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_VOLUME)
        {
            update_volume_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_BRUSH_CONTENTS)
        {
            update_brush_contents_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_BRUSH_PERCEPTION)
        {
            update_brush_perception_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_PERCEPTION)
        {
            update_perception_sensor(runtime, sensor);
            continue;
        }
        if (sensor->type == GAME_DATA_SENSOR_HEARING)
        {
            update_hearing_sensor(runtime, sensor);
            continue;
        }

        if (sensor->entity != NULL && !active_scene_has_entity_internal(runtime, sensor->entity))
        {
            sensor->was_active = false;
            continue;
        }

        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, sensor->entity);
        if (sensor->type == GAME_DATA_SENSOR_INPUT_PRESSED)
        {
            const int action_id = slayer3d_game_data_find_action(runtime, sensor->action);
            if (slayer3d_input_is_pressed(runtime_input(runtime), action_id))
                emit_sensor_signal(runtime, sensor, NULL, NULL);
            continue;
        }
        if (actor == NULL)
            continue;

        if (sensor->type == GAME_DATA_SENSOR_BOUNDS_EXIT)
        {
            const int axis = axis_index(sensor->axis);
            const float value = vec_axis(actor->position, axis);
            const bool active =
                SDL_strcmp(sensor->side, "min") == 0 ? value < sensor->threshold : value > sensor->threshold;
            if (active && !sensor->was_active)
                emit_sensor_signal(runtime, sensor, actor, NULL);
            sensor->was_active = active;
        }
        else if (sensor->type == GAME_DATA_SENSOR_BOUNDS_REFLECT)
        {
            const int axis = axis_index(sensor->axis);
            const float value = vec_axis(actor->position, axis);
            if (value < sensor->min_value || value > sensor->max_value)
            {
                slayer3d_vec3 position = actor->position;
                slayer3d_vec3 velocity = actor_vec_property(actor, "velocity");
                set_vec_axis(&position, axis, SDL_clamp(value, sensor->min_value, sensor->max_value));
                set_vec_axis(&velocity, axis, -vec_axis(velocity, axis));
                actor_set_position(actor, position);
                slayer3d_properties_set_vec3(actor->props, "velocity", velocity);
                emit_sensor_signal(runtime, sensor, actor, NULL);
            }
        }
    }
}

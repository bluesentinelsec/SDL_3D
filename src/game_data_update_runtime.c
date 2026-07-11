/* Controller, motion, sensor, and schedule update helpers. */

#include "game_data_internal.h"

#include <SDL3/SDL_log.h>

static void update_noise_events(slayer3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL || runtime->noise_event_count <= 0)
        return;

    int write_index = 0;
    for (int read_index = 0; read_index < runtime->noise_event_count; ++read_index)
    {
        noise_event_runtime event = runtime->noise_events[read_index];
        event.remaining_seconds -= dt;
        if (event.remaining_seconds > 0.0f)
        {
            if (write_index != read_index)
                runtime->noise_events[write_index] = event;
            ++write_index;
        }
    }
    runtime->noise_event_count = write_index;
}

static float json_random_axis(slayer3d_game_data_runtime *runtime, yyjson_val *value, float fallback)
{
    if (yyjson_is_num(value))
        return (float)yyjson_get_num(value);
    if (yyjson_is_arr(value) && yyjson_arr_size(value) >= 2 && yyjson_is_num(yyjson_arr_get(value, 0)) &&
        yyjson_is_num(yyjson_arr_get(value, 1)))
    {
        const float lo = (float)yyjson_get_num(yyjson_arr_get(value, 0));
        const float hi = (float)yyjson_get_num(yyjson_arr_get(value, 1));
        return lo + (hi - lo) * game_data_random01(runtime);
    }
    return fallback;
}

static slayer3d_vec3 wave_schedule_position(slayer3d_game_data_runtime *runtime, yyjson_val *schedule,
                                            slayer3d_vec3 fallback)
{
    yyjson_val *position = obj_get(schedule, "position");
    if (yyjson_is_arr(position))
        return json_vec3_value(position, fallback);
    if (yyjson_is_obj(position))
    {
        return slayer3d_vec3_make(json_random_axis(runtime, obj_get(position, "x"), fallback.x),
                                  json_random_axis(runtime, obj_get(position, "y"), fallback.y),
                                  json_random_axis(runtime, obj_get(position, "z"), fallback.z));
    }
    return fallback;
}

static void update_wave_schedules(slayer3d_game_data_runtime *runtime, float dt)
{
    for (int i = 0; runtime != NULL && i < runtime->wave_schedule_count; ++i)
    {
        wave_schedule_entry *entry = &runtime->wave_schedules[i];
        yyjson_val *schedule = entry->schedule;
        yyjson_val *active_if = obj_get(schedule, "active_if");
        if (active_if != NULL && !eval_data_condition(runtime, active_if, NULL))
        {
            entry->initialized = false;
            entry->elapsed = 0.0f;
            continue;
        }

        const float interval = SDL_max(json_float(schedule, "interval", 1.0f), 0.001f);
        if (!entry->initialized)
        {
            entry->elapsed = -json_float(schedule, "initial_delay", interval);
            entry->initialized = true;
        }
        entry->elapsed += dt;
        if (entry->elapsed < 0.0f)
            continue;

        const char *max_active_tag = json_string(schedule, "max_active_tag", NULL);
        if (max_active_tag != NULL)
        {
            sensor_actor_list active;
            SDL_zero(active);
            (void)collect_sensor_endpoint_actors(runtime, NULL, max_active_tag, &active);
            const int max_active = json_int(schedule, "max_active", 0);
            if (max_active > 0 && active.count >= max_active)
            {
                sensor_actor_list_free(&active);
                entry->elapsed = SDL_min(entry->elapsed, interval);
                continue;
            }
            sensor_actor_list_free(&active);
        }

        while (entry->elapsed >= 0.0f)
        {
            actor_pool_runtime *pool = find_actor_pool(runtime, json_string(schedule, "pool", NULL));
            actor_pool_note_spawn_attempt(pool);
            int actor_index = -1;
            slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
            if (pool == NULL || actor == NULL || actor_index < 0)
            {
                actor_pool_note_spawn_failure(pool, "exhausted");
                break;
            }
            actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
            if (!initialize_pooled_actor(pool, actor, actor_index, true))
            {
                actor_pool_note_spawn_failure(pool, "initialize_failed");
                break;
            }
            actor_set_position(actor, wave_schedule_position(runtime, schedule, actor->position));
            apply_actor_spawn_properties(actor, obj_get(schedule, "properties"));
            yyjson_val *velocity = obj_get(schedule, "velocity");
            if (velocity != NULL)
                slayer3d_properties_set_vec3(actor->props, "velocity",
                                             json_vec3_value(velocity, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
            actor_pool_note_spawn_success(runtime, pool);
            entry->elapsed -= interval;
            if (!json_bool(schedule, "catch_up", false))
                break;
        }
    }
}

bool slayer3d_game_data_update(slayer3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL || runtime->doc == NULL)
        return false;
    if (dt < 0.0f)
        dt = 0.0f;
    runtime->current_dt = dt;

    yyjson_val *root = yyjson_doc_get_root(runtime->doc);
    actor_lifecycle_defer_begin(runtime);
    update_sector_doors(runtime, dt);
    if (!update_sector_platforms(runtime, dt))
    {
        actor_lifecycle_defer_end(runtime);
        return false;
    }
    update_control_components(runtime, root, dt);
    update_motion_components(runtime, root, dt);
    update_wave_schedules(runtime, dt);
    /* One authoritative "a text field owns the keyboard" fact, refreshed
     * from the live focus keys before sensors evaluate so bare-key shortcut
     * guards never read a stale frame. */
    if (runtime->scene_state != NULL)
    {
        slayer3d_properties_set_bool(runtime->scene_state, "editor.ui.text_entry.active",
                                     editor_property_edit_has_focus(runtime) ||
                                         editor_texture_edit_has_focus(runtime) ||
                                         editor_global_edit_has_focus(runtime));
    }
    update_sensors(runtime);
    update_noise_events(runtime, dt);
    actor_lifecycle_defer_end(runtime);
    return true;
}

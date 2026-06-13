/**
 * @file game_data_validation_actions.c
 * @brief Data action validation.
 */

#include "game_data_validation_actions_internal.h"

#include <SDL3/SDL_stdinc.h>
#include <float.h>

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static bool is_non_empty_string(yyjson_val *object, const char *key)
{
    const char *value = json_string(object, key);
    return value != NULL && value[0] != '\0';
}

static bool is_tween_easing(const char *easing)
{
    return easing == NULL || SDL_strcmp(easing, "linear") == 0 || SDL_strcmp(easing, "in_quad") == 0 ||
           SDL_strcmp(easing, "out_quad") == 0 || SDL_strcmp(easing, "in_out_quad") == 0;
}

static bool is_tween_repeat(const char *repeat)
{
    return repeat == NULL || SDL_strcmp(repeat, "none") == 0 || SDL_strcmp(repeat, "loop") == 0 ||
           SDL_strcmp(repeat, "ping_pong") == 0;
}

static bool is_tween_value_type(const char *value_type)
{
    return value_type == NULL || SDL_strcmp(value_type, "int") == 0 || SDL_strcmp(value_type, "float") == 0 ||
           SDL_strcmp(value_type, "number") == 0 || SDL_strcmp(value_type, "vec3") == 0 ||
           SDL_strcmp(value_type, "color") == 0;
}

static bool is_ui_tween_property(const char *property)
{
    return property != NULL && (SDL_strcmp(property, "alpha") == 0 || SDL_strcmp(property, "scale") == 0 ||
                                SDL_strcmp(property, "offset_x") == 0 || SDL_strcmp(property, "offset_y") == 0 ||
                                SDL_strcmp(property, "x") == 0 || SDL_strcmp(property, "y") == 0 ||
                                SDL_strcmp(property, "tint") == 0 || SDL_strcmp(property, "color") == 0);
}

static bool validate_tween_value(validation_context *ctx, yyjson_val *value, const char *json_path,
                                 const char *field_name)
{
    if (yyjson_is_num(value) || is_vec_array(value, 2))
        return true;
    return validation_error(ctx, json_path, "%s must be a number or numeric array", field_name);
}

static bool validate_animation_common(validation_context *ctx, yyjson_val *action, const char *json_path,
                                      validation_names *names)
{
    yyjson_val *to = obj_get(action, "to");
    if (to == NULL)
        to = obj_get(action, "value");
    if (to == NULL)
        return validation_error(ctx, json_path, "animation action requires to or value");
    if (!validate_tween_value(ctx, to, json_path, "animation target value"))
        return false;
    yyjson_val *from = obj_get(action, "from");
    if (from != NULL && !validate_tween_value(ctx, from, json_path, "animation start value"))
        return false;
    yyjson_val *duration = obj_get(action, "duration");
    if (duration != NULL && (!yyjson_is_num(duration) || yyjson_get_num(duration) < 0.0))
        return validation_error(ctx, json_path, "animation duration must be a non-negative number");
    if (!is_tween_easing(json_string(action, "easing")))
        return validation_error(ctx, json_path, "animation easing must be linear, in_quad, out_quad, or in_out_quad");
    if (!is_tween_repeat(json_string(action, "repeat")))
        return validation_error(ctx, json_path, "animation repeat must be none, loop, or ping_pong");
    if (!is_tween_value_type(json_string(action, "value_type")))
        return validation_error(ctx, json_path, "animation value_type must be int, float, number, vec3, or color");
    const char *done_signal = json_string(action, "done_signal");
    if (done_signal != NULL && !require_ref(ctx, &names->signals, "signal", done_signal, json_path))
        return false;
    return true;
}

static bool validate_audio_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                  validation_names *names, const char *type)
{
    if (SDL_strcmp(type, "audio.play_sfx") == 0)
    {
        const char *sound = json_string(action, "sound");
        const char *asset = json_string(action, "asset");
        const char *path = json_string(action, "path");
        if (sound != NULL && !require_ref(ctx, &names->sounds, "sound asset", sound, json_path))
            return false;
        if (asset != NULL && !require_ref(ctx, &names->sounds, "sound asset", asset, json_path))
            return false;
        if (sound == NULL && asset == NULL && (path == NULL || path[0] == '\0'))
            return validation_error(ctx, json_path, "audio.play_sfx requires sound, asset, or path");
        if (path != NULL && !asset_path_exists(ctx, path, json_path, "sound"))
            return false;
    }
    else if (SDL_strcmp(type, "audio.play_music") == 0)
    {
        const char *music = json_string(action, "music");
        const char *asset = json_string(action, "asset");
        const char *path = json_string(action, "path");
        if (music != NULL && !require_ref(ctx, &names->music, "music asset", music, json_path))
            return false;
        if (asset != NULL && !require_ref(ctx, &names->music, "music asset", asset, json_path))
            return false;
        if (music == NULL && asset == NULL && (path == NULL || path[0] == '\0'))
            return validation_error(ctx, json_path, "audio.play_music requires music, asset, or path");
        if (path != NULL && !asset_path_exists(ctx, path, json_path, "music"))
            return false;
    }
    else if (SDL_strcmp(type, "audio.set_ambient") == 0)
    {
        yyjson_val *ambient_id = obj_get(action, "ambient_id");
        yyjson_val *ambient_id_from_payload_value = obj_get(action, "ambient_id_from_payload");
        const char *ambient_id_from_payload = json_string(action, "ambient_id_from_payload");
        if (!validate_exactly_one_field_present(ctx, action, json_path, "audio.set_ambient", "ambient_id",
                                                "ambient_id_from_payload"))
            return false;
        if (ambient_id != NULL && (!yyjson_is_int(ambient_id) || yyjson_get_int(ambient_id) < 0))
            return validation_error(ctx, json_path, "audio.set_ambient ambient_id must be non-negative");
        if (ambient_id_from_payload_value != NULL && !yyjson_is_str(ambient_id_from_payload_value))
            return validation_error(ctx, json_path, "audio.set_ambient ambient_id_from_payload must be a string");
        if (ambient_id_from_payload != NULL && ambient_id_from_payload[0] == '\0')
            return validation_error(ctx, json_path, "audio.set_ambient ambient_id_from_payload must be non-empty");
    }
    else if (SDL_strcmp(type, "audio.stop_sfx") != 0 && SDL_strcmp(type, "audio.stop_music") != 0 &&
             SDL_strcmp(type, "audio.fade_music") != 0 && SDL_strcmp(type, "audio.set_bus_volume") != 0)
    {
        return validation_error(ctx, json_path, "unknown audio action type '%s'", type);
    }

    if (SDL_strcmp(type, "audio.set_bus_volume") == 0 && json_string(action, "bus") == NULL)
        return validation_error(ctx, json_path, "audio.set_bus_volume requires a bus");
    if (!validation_audio_bus_name_valid(json_string(action, "bus")))
        return validation_error(ctx, json_path, "audio bus must be sfx, music, dialogue, or ambience");
    yyjson_val *volume = obj_get(action, "volume");
    if (volume != NULL && !yyjson_is_num(volume))
        return validation_error(ctx, json_path, "audio volume must be numeric");
    yyjson_val *source = obj_get(action, "source");
    if (source != NULL)
    {
        if (!yyjson_is_obj(source))
            return validation_error(ctx, json_path, "audio source must be an object");
        if (!require_ref(ctx, &names->entities, "entity", json_string(source, "target"), json_path))
            return false;
        if (!is_non_empty_string(source, "key"))
            return validation_error(ctx, json_path, "audio source requires a non-empty key");
    }
    yyjson_val *fade = obj_get(action, "fade");
    if (fade != NULL && !yyjson_is_num(fade))
        return validation_error(ctx, json_path, "audio fade must be numeric");
    yyjson_val *duration = obj_get(action, "duration");
    if (duration != NULL && !yyjson_is_num(duration))
        return validation_error(ctx, json_path, "audio duration must be numeric");
    return true;
}

static bool validate_combat_target_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type)
{
    yyjson_val *target_value = obj_get(action, "target");
    yyjson_val *target_from_payload_value = obj_get(action, "target_from_payload");
    const char *target = json_string(action, "target");
    const char *target_from_payload = json_string(action, "target_from_payload");
    if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of target or target_from_payload", type);
    if (target_value != NULL && !yyjson_is_str(target_value))
        return validation_error(ctx, json_path, "%s target must be a string", type);
    if (target_from_payload_value != NULL && !yyjson_is_str(target_from_payload_value))
        return validation_error(ctx, json_path, "%s target_from_payload must be a string", type);
    if (target != NULL && target[0] == '\0')
        return validation_error(ctx, json_path, "%s target must be non-empty", type);
    if (target_from_payload != NULL && target_from_payload[0] == '\0')
        return validation_error(ctx, json_path, "%s target_from_payload must be non-empty", type);
    if (target != NULL && !name_table_contains(&names->entities, target) &&
        !name_table_contains(&names->actor_pool_actors, target))
    {
        return validation_error(ctx, json_path, "unknown %s target '%s'", type, target);
    }

    const char *source = json_string(action, "source");
    if (source != NULL && source[0] == '\0')
        return validation_error(ctx, json_path, "%s source must be non-empty", type);
    if (source != NULL && !name_table_contains(&names->entities, source) &&
        !name_table_contains(&names->actor_pool_actors, source))
    {
        return validation_error(ctx, json_path, "unknown %s source '%s'", type, source);
    }
    yyjson_val *source_from_payload = obj_get(action, "source_from_payload");
    if (source_from_payload != NULL &&
        (!yyjson_is_str(source_from_payload) || yyjson_get_str(source_from_payload)[0] == '\0'))
        return validation_error(ctx, json_path, "%s source_from_payload must be a non-empty string", type);

    const char *property_keys[] = {"health_property", "max_health_property", "armor_property", "armor_absorb_property",
                                   "alive_property"};
    for (size_t i = 0; i < SDL_arraysize(property_keys); ++i)
    {
        yyjson_val *value = obj_get(action, property_keys[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, json_path, "%s property names must be non-empty strings", type);
    }

    const char *signal_keys[] = {"on_damage", "on_death", "on_heal", "on_revive"};
    for (size_t i = 0; i < SDL_arraysize(signal_keys); ++i)
    {
        const char *signal = json_string(action, signal_keys[i]);
        if (signal != NULL && !require_ref(ctx, &names->signals, "signal", signal, json_path))
            return false;
    }

    yyjson_val *deactivate_on_death = obj_get(action, "deactivate_on_death");
    yyjson_val *deactivate = obj_get(action, "deactivate");
    yyjson_val *revive = obj_get(action, "revive");
    if ((deactivate_on_death != NULL && !yyjson_is_bool(deactivate_on_death)) ||
        (deactivate != NULL && !yyjson_is_bool(deactivate)) || (revive != NULL && !yyjson_is_bool(revive)))
    {
        return validation_error(ctx, json_path, "%s boolean fields must be booleans", type);
    }

    yyjson_val *armor_absorb = obj_get(action, "armor_absorb");
    if (armor_absorb != NULL &&
        (!yyjson_is_num(armor_absorb) || yyjson_get_num(armor_absorb) < 0.0 || yyjson_get_num(armor_absorb) > 1.0))
    {
        return validation_error(ctx, json_path, "%s armor_absorb must be in 0..1", type);
    }
    yyjson_val *damage_type = obj_get(action, "damage_type");
    if (damage_type != NULL && !yyjson_is_str(damage_type))
        return validation_error(ctx, json_path, "%s damage_type must be a string", type);
    return validate_target_filter_fields(ctx, action, json_path, type);
}

static bool validate_combat_amount_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type, const char *field,
                                          const char *payload_field)
{
    if (!validate_combat_target_action(ctx, action, json_path, names, type))
        return false;
    yyjson_val *amount = obj_get(action, field);
    yyjson_val *amount_from_payload_value = obj_get(action, payload_field);
    if (!validate_exactly_one_field_present(ctx, action, json_path, type, field, payload_field))
        return false;
    if (amount != NULL && (!yyjson_is_num(amount) || yyjson_get_num(amount) < 0.0))
        return validation_error(ctx, json_path, "%s %s must be a non-negative number", type, field);
    if (amount_from_payload_value != NULL &&
        (!yyjson_is_str(amount_from_payload_value) || yyjson_get_str(amount_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "%s %s must be a non-empty string", type, payload_field);
    }
    return true;
}

static bool validate_actor_target_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         validation_names *names, const char *type, const char *target_key,
                                         const char *payload_key)
{
    yyjson_val *target_value = obj_get(action, target_key);
    yyjson_val *target_from_payload_value = obj_get(action, payload_key);
    const char *target = json_string(action, target_key);
    const char *target_from_payload = json_string(action, payload_key);
    if (!validate_exactly_one_field_present(ctx, action, json_path, type, target_key, payload_key))
        return false;
    if (target_value != NULL && !yyjson_is_str(target_value))
        return validation_error(ctx, json_path, "%s %s must be a string", type, target_key);
    if (target_from_payload_value != NULL && !yyjson_is_str(target_from_payload_value))
        return validation_error(ctx, json_path, "%s %s must be a string", type, payload_key);
    if (target != NULL && target[0] == '\0')
        return validation_error(ctx, json_path, "%s %s must be non-empty", type, target_key);
    if (target_from_payload != NULL && target_from_payload[0] == '\0')
        return validation_error(ctx, json_path, "%s %s must be non-empty", type, payload_key);
    if (target != NULL && !name_table_contains(&names->entities, target) &&
        !name_table_contains(&names->actor_pool_actors, target))
    {
        return validation_error(ctx, json_path, "unknown %s %s '%s'", type, target_key, target);
    }
    return true;
}

static bool faction_relationship_valid(const char *value)
{
    return value != NULL && (SDL_strcmp(value, "friendly") == 0 || SDL_strcmp(value, "hostile") == 0 ||
                             SDL_strcmp(value, "neutral") == 0 || SDL_strcmp(value, "ignored") == 0);
}

static bool target_filter_relationship_valid(const char *value)
{
    return value != NULL && (SDL_strcmp(value, "any") == 0 || faction_relationship_valid(value));
}

static bool validate_string_or_string_array(validation_context *ctx, yyjson_val *json, const char *json_path,
                                            const char *type, const char *field)
{
    yyjson_val *value = obj_get(json, field);
    if (value == NULL)
        return true;
    if (yyjson_is_str(value))
        return yyjson_get_str(value)[0] != '\0' ||
               validation_error(ctx, json_path, "%s %s must contain non-empty strings", type, field);
    if (!yyjson_is_arr(value))
        return validation_error(ctx, json_path, "%s %s must be a string or string array", type, field);
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (!yyjson_is_str(entry) || yyjson_get_str(entry)[0] == '\0')
            return validation_error(ctx, json_path, "%s %s must contain non-empty strings", type, field);
    }
    return true;
}

bool validate_target_filter_fields(validation_context *ctx, yyjson_val *json, const char *json_path, const char *type)
{
    if (json == NULL)
        return true;
    yyjson_val *filter = obj_get(json, "target_filter");
    if (filter != NULL && !yyjson_is_obj(filter))
        return validation_error(ctx, json_path, "%s target_filter must be an object", type);
    yyjson_val *sources[] = {json, filter};
    for (size_t s = 0; s < SDL_arraysize(sources); ++s)
    {
        yyjson_val *source = sources[s];
        if (source == NULL)
            continue;
        const char *string_fields[] = {"target_tag",       "affected_tag",
                                       "hit_tag",          "target_faction",
                                       "source_faction",   "source_faction_from_payload",
                                       "faction_property", "source_faction_property",
                                       "owner_property",   "owner_actor_property"};
        for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
        {
            if (!validate_non_empty_string_field(ctx, source, json_path, type, string_fields[i]))
                return false;
        }
        const char *array_fields[] = {"include_tags", "exclude_tags", "include_factions", "exclude_factions"};
        for (size_t i = 0; i < SDL_arraysize(array_fields); ++i)
        {
            if (!validate_string_or_string_array(ctx, source, json_path, type, array_fields[i]))
                return false;
        }
        const char *bool_fields[] = {"exclude_source", "exclude_self", "exclude_owner"};
        for (size_t i = 0; i < SDL_arraysize(bool_fields); ++i)
        {
            yyjson_val *value = obj_get(source, bool_fields[i]);
            if (value != NULL && !yyjson_is_bool(value))
                return validation_error(ctx, json_path, "%s %s must be a boolean", type, bool_fields[i]);
        }
        const char *relationship = json_string(source, "relationship");
        if (relationship != NULL && !target_filter_relationship_valid(relationship))
            return validation_error(ctx, json_path,
                                    "%s relationship must be any, friendly, hostile, neutral, or ignored", type);
    }
    return true;
}

static bool validate_resource_grant(validation_context *ctx, yyjson_val *grant, const char *json_path,
                                    validation_names *names, const char *owner_type)
{
    (void)names;
    if (!yyjson_is_obj(grant))
        return validation_error(ctx, json_path, "%s resources entries must be objects", owner_type);
    if (!is_non_empty_string(grant, "resource"))
        return validation_error(ctx, json_path, "%s resource grants require a non-empty resource", owner_type);
    if (!validate_non_empty_string_field(ctx, grant, json_path, owner_type, "property") ||
        !validate_non_empty_string_field(ctx, grant, json_path, owner_type, "max_property"))
    {
        return false;
    }

    yyjson_val *amount = obj_get(grant, "amount");
    yyjson_val *amount_from_payload_value = obj_get(grant, "amount_from_payload");
    const char *amount_from_payload = json_string(grant, "amount_from_payload");
    if ((amount == NULL && amount_from_payload == NULL) || (amount != NULL && amount_from_payload != NULL))
        return validation_error(ctx, json_path,
                                "%s resource grants require exactly one of amount or amount_from_payload", owner_type);
    if (amount != NULL && (!yyjson_is_num(amount) || yyjson_get_num(amount) < 0.0))
        return validation_error(ctx, json_path, "%s resource grant amount must be a non-negative number", owner_type);
    if (amount_from_payload_value != NULL &&
        (!yyjson_is_str(amount_from_payload_value) || yyjson_get_str(amount_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "%s amount_from_payload must be a non-empty string", owner_type);
    }
    yyjson_val *max = obj_get(grant, "max");
    yyjson_val *min = obj_get(grant, "min");
    yyjson_val *clamp = obj_get(grant, "clamp");
    if ((max != NULL && !yyjson_is_num(max)) || (min != NULL && !yyjson_is_num(min)))
        return validation_error(ctx, json_path, "%s resource grant min/max must be numbers", owner_type);
    if (clamp != NULL && !yyjson_is_bool(clamp))
        return validation_error(ctx, json_path, "%s resource grant clamp must be a boolean", owner_type);
    return true;
}

static bool validate_resource_grants(validation_context *ctx, yyjson_val *action, const char *json_path,
                                     validation_names *names, const char *type)
{
    yyjson_val *resources = obj_get(action, "resources");
    if (!yyjson_is_arr(resources) || yyjson_arr_size(resources) == 0)
        return validation_error(ctx, json_path, "%s requires a non-empty resources array", type);
    for (size_t i = 0; i < yyjson_arr_size(resources); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.resources[%zu]", json_path, i);
        if (!validate_resource_grant(ctx, yyjson_arr_get(resources, i), path, names, type))
            return false;
    }
    return true;
}

static bool validate_resource_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                     validation_names *names, const char *type, const char *value_key,
                                     const char *payload_key)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, type, "target", "target_from_payload"))
        return false;
    if (!is_non_empty_string(action, "resource"))
        return validation_error(ctx, json_path, "%s requires a non-empty resource", type);
    if (!validate_non_empty_string_field(ctx, action, json_path, type, "property") ||
        !validate_non_empty_string_field(ctx, action, json_path, type, "max_property"))
    {
        return false;
    }

    yyjson_val *value = obj_get(action, value_key);
    yyjson_val *value_from_payload_value = obj_get(action, payload_key);
    const char *value_from_payload = json_string(action, payload_key);
    if ((value == NULL && value_from_payload == NULL) || (value != NULL && value_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of %s or %s", type, value_key, payload_key);
    if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0))
        return validation_error(ctx, json_path, "%s %s must be a non-negative number", type, value_key);
    if (value_from_payload_value != NULL &&
        (!yyjson_is_str(value_from_payload_value) || yyjson_get_str(value_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "%s %s must be a non-empty string", type, payload_key);
    }

    yyjson_val *max = obj_get(action, "max");
    yyjson_val *min = obj_get(action, "min");
    yyjson_val *clamp = obj_get(action, "clamp");
    yyjson_val *allow_partial = obj_get(action, "allow_partial");
    if ((max != NULL && !yyjson_is_num(max)) || (min != NULL && !yyjson_is_num(min)))
        return validation_error(ctx, json_path, "%s min/max must be numbers", type);
    if ((clamp != NULL && !yyjson_is_bool(clamp)) || (allow_partial != NULL && !yyjson_is_bool(allow_partial)))
        return validation_error(ctx, json_path, "%s boolean fields must be booleans", type);
    return validate_optional_signal_field(ctx, action, json_path, names, "on_success") &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_failure");
}

static bool validate_pickup_collect_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                           validation_names *names)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, "pickup.collect", "target", "target_from_payload"))
        return false;
    if (!validate_actor_target_action(ctx, action, json_path, names, "pickup.collect", "pickup", "pickup_from_payload"))
        return false;
    yyjson_val *deactivate = obj_get(action, "deactivate");
    yyjson_val *respawn = obj_get(action, "respawn_seconds");
    if (deactivate != NULL && !yyjson_is_bool(deactivate))
        return validation_error(ctx, json_path, "pickup.collect deactivate must be a boolean");
    if (respawn != NULL && (!yyjson_is_num(respawn) || yyjson_get_num(respawn) < 0.0))
        return validation_error(ctx, json_path, "pickup.collect respawn_seconds must be non-negative");
    if (!validate_non_empty_string_field(ctx, action, json_path, "pickup.collect", "timer_property") ||
        !validate_non_empty_string_field(ctx, action, json_path, "pickup.collect", "available_property"))
    {
        return false;
    }
    return validate_resource_grants(ctx, action, json_path, names, "pickup.collect") &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_collected");
}

static bool validate_resource_station_use_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, "resource.station.use", "target",
                                      "target_from_payload"))
        return false;
    if (!validate_actor_target_action(ctx, action, json_path, names, "resource.station.use", "station",
                                      "station_from_payload"))
        return false;
    yyjson_val *cooldown = obj_get(action, "cooldown");
    yyjson_val *consume_charge = obj_get(action, "consume_charge");
    if (cooldown != NULL && (!yyjson_is_num(cooldown) || yyjson_get_num(cooldown) < 0.0))
        return validation_error(ctx, json_path, "resource.station.use cooldown must be non-negative");
    if (consume_charge != NULL && !yyjson_is_bool(consume_charge))
        return validation_error(ctx, json_path, "resource.station.use consume_charge must be a boolean");
    if (!validate_non_empty_string_field(ctx, action, json_path, "resource.station.use", "cooldown_property") ||
        !validate_non_empty_string_field(ctx, action, json_path, "resource.station.use", "charges_property"))
    {
        return false;
    }
    return validate_resource_grants(ctx, action, json_path, names, "resource.station.use") &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_success") &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_failure");
}

static bool validate_status_effect_apply_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, "status_effect.apply", "target",
                                      "target_from_payload"))
        return false;
    if (!is_non_empty_string(action, "property"))
        return validation_error(ctx, json_path, "status_effect.apply requires a non-empty property");
    if (!validate_non_empty_string_field(ctx, action, json_path, "status_effect.apply", "duration_property") ||
        !validate_non_empty_string_field(ctx, action, json_path, "status_effect.apply", "active_property"))
    {
        return false;
    }
    yyjson_val *value = obj_get(action, "value");
    if (!(yyjson_is_bool(value) || yyjson_is_num(value) || yyjson_is_str(value)))
        return validation_error(ctx, json_path, "status_effect.apply value must be scalar");
    yyjson_val *duration = obj_get(action, "duration");
    yyjson_val *duration_from_payload_value = obj_get(action, "duration_from_payload");
    const char *duration_from_payload = json_string(action, "duration_from_payload");
    if ((duration == NULL && duration_from_payload == NULL) || (duration != NULL && duration_from_payload != NULL))
    {
        return validation_error(ctx, json_path,
                                "status_effect.apply requires exactly one of duration or duration_from_payload");
    }
    if (duration != NULL && (!yyjson_is_num(duration) || yyjson_get_num(duration) < 0.0))
        return validation_error(ctx, json_path, "status_effect.apply duration must be non-negative");
    if (duration_from_payload_value != NULL &&
        (!yyjson_is_str(duration_from_payload_value) || yyjson_get_str(duration_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "status_effect.apply duration_from_payload must be non-empty");
    }
    return validate_optional_signal_field(ctx, action, json_path, names, "on_apply");
}

static bool validate_weapon_common_fire_fields(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    const char *string_fields[] = {"cooldown_property", "reload_timer_property", "clip_property",
                                   "ammo_resource",     "ammo_property",         "direction_from_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, action, json_path, type, string_fields[i]))
            return false;
    }
    yyjson_val *cooldown = obj_get(action, "cooldown");
    yyjson_val *ammo_per_shot = obj_get(action, "ammo_per_shot");
    if ((cooldown != NULL && (!yyjson_is_num(cooldown) || yyjson_get_num(cooldown) < 0.0)) ||
        (ammo_per_shot != NULL && (!yyjson_is_num(ammo_per_shot) || yyjson_get_num(ammo_per_shot) < 0.0)))
    {
        return validation_error(ctx, json_path, "%s cooldown and ammo_per_shot must be non-negative", type);
    }
    const char *signal_keys[] = {"on_fire", "on_empty", "on_cooldown", "on_reloading"};
    for (size_t i = 0; i < SDL_arraysize(signal_keys); ++i)
    {
        if (!validate_optional_signal_field(ctx, action, json_path, names, signal_keys[i]))
            return false;
    }
    return true;
}

static bool validate_weapon_reload_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, "weapon.reload", "target", "target_from_payload"))
        return false;
    const char *string_fields[] = {"clip_property", "clip_size_property", "reserve_property", "reload_timer_property",
                                   "reload_pending_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, action, json_path, "weapon.reload", string_fields[i]))
            return false;
    }
    yyjson_val *clip_size = obj_get(action, "clip_size");
    yyjson_val *reload_seconds = obj_get(action, "reload_seconds");
    if ((clip_size != NULL && (!yyjson_is_num(clip_size) || yyjson_get_num(clip_size) < 0.0)) ||
        (reload_seconds != NULL && (!yyjson_is_num(reload_seconds) || yyjson_get_num(reload_seconds) < 0.0)))
    {
        return validation_error(ctx, json_path, "weapon.reload numeric values must be non-negative");
    }
    yyjson_val *consume_reserve = obj_get(action, "consume_reserve");
    if (consume_reserve != NULL && !yyjson_is_bool(consume_reserve))
        return validation_error(ctx, json_path, "weapon.reload consume_reserve must be a boolean");
    return validate_optional_signal_field(ctx, action, json_path, names, "on_reload") &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_failure");
}

static bool validate_weapon_hitscan_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                           validation_names *names)
{
    if (!validate_actor_target_action(ctx, action, json_path, names, "weapon.hitscan", "target",
                                      "target_from_payload") ||
        !validate_weapon_common_fire_fields(ctx, action, json_path, names, "weapon.hitscan"))
    {
        return false;
    }
    const char *sector_level = json_string(action, "sector_level");
    if (sector_level != NULL && !require_ref(ctx, &names->sector_levels, "sector level", sector_level, json_path))
        return false;
    yyjson_val *trace_brush_worlds = obj_get(action, "trace_brush_worlds");
    if (trace_brush_worlds != NULL && !yyjson_is_bool(trace_brush_worlds))
        return validation_error(ctx, json_path, "weapon.hitscan trace_brush_worlds must be a boolean");
    char contents_path[PATH_BUFFER_SIZE];
    format_path(contents_path, sizeof(contents_path), "%s.brush_contents_mask", json_path);
    if (!validate_brush_string_or_string_array(ctx, obj_get(action, "brush_contents_mask"), contents_path,
                                               "brush content", brush_content_name_valid, false))
    {
        return false;
    }
    if (!validate_non_empty_string_field(ctx, action, json_path, "weapon.hitscan", "target_tag") ||
        !validate_non_empty_string_field(ctx, action, json_path, "weapon.hitscan", "hit_tag"))
    {
        return false;
    }
    yyjson_val *direction = obj_get(action, "direction");
    yyjson_val *offset = obj_get(action, "offset");
    if (direction != NULL && !is_vec_array(direction, 3))
        return validation_error(ctx, json_path, "weapon.hitscan direction must be a vec3");
    if (offset != NULL && !is_vec_array(offset, 3))
        return validation_error(ctx, json_path, "weapon.hitscan offset must be a vec3");
    yyjson_val *range = obj_get(action, "range");
    yyjson_val *hit_radius = obj_get(action, "hit_radius");
    if ((range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) < 0.0)) ||
        (hit_radius != NULL && (!yyjson_is_num(hit_radius) || yyjson_get_num(hit_radius) < 0.0)))
    {
        return validation_error(ctx, json_path, "weapon.hitscan range and hit_radius must be non-negative");
    }
    yyjson_val *exclude_source = obj_get(action, "exclude_source");
    yyjson_val *run_actions_on_miss = obj_get(action, "run_actions_on_miss");
    if ((exclude_source != NULL && !yyjson_is_bool(exclude_source)) ||
        (run_actions_on_miss != NULL && !yyjson_is_bool(run_actions_on_miss)))
    {
        return validation_error(ctx, json_path, "weapon.hitscan boolean fields must be booleans");
    }
    yyjson_val *actions = obj_get(action, "actions");
    yyjson_val *miss_actions = obj_get(action, "miss_actions");
    return validate_target_filter_fields(ctx, action, json_path, "weapon.hitscan") &&
           (actions == NULL || validate_action_array(ctx, actions, json_path, names)) &&
           (miss_actions == NULL || validate_action_array(ctx, miss_actions, json_path, names));
}

static bool validate_interaction_use_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names)
{
    yyjson_val *actor_value = obj_get(action, "actor");
    yyjson_val *actor_from_payload_value = obj_get(action, "actor_from_payload");
    const char *actor = json_string(action, "actor");
    const char *actor_from_payload = json_string(action, "actor_from_payload");
    if ((actor == NULL && actor_from_payload == NULL) || (actor != NULL && actor_from_payload != NULL))
        return validation_error(ctx, json_path, "interaction.use requires exactly one of actor or actor_from_payload");
    if (actor_value != NULL && !yyjson_is_str(actor_value))
        return validation_error(ctx, json_path, "interaction.use actor must be a string");
    if (actor_from_payload_value != NULL && !yyjson_is_str(actor_from_payload_value))
        return validation_error(ctx, json_path, "interaction.use actor_from_payload must be a string");
    if (actor != NULL && !require_actor_ref(ctx, names, actor, json_path))
        return false;
    if (actor_from_payload != NULL && actor_from_payload[0] == '\0')
        return validation_error(ctx, json_path, "interaction.use actor_from_payload must be non-empty");

    yyjson_val *range = obj_get(action, "range");
    yyjson_val *min_dot = obj_get(action, "min_dot");
    if ((range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) < 0.0)) ||
        (min_dot != NULL &&
         (!yyjson_is_num(min_dot) || yyjson_get_num(min_dot) < -1.0 || yyjson_get_num(min_dot) > 1.0)))
    {
        return validation_error(ctx, json_path, "interaction.use range/min_dot are invalid");
    }
    if (!validate_non_empty_string_field(ctx, action, json_path, "interaction.use", "yaw_property") ||
        !validate_non_empty_string_field(ctx, action, json_path, "interaction.use", "target_tag") ||
        !validate_non_empty_string_field(ctx, action, json_path, "interaction.use", "interactable_tag"))
    {
        return false;
    }
    yyjson_val *miss_actions = obj_get(action, "miss_actions");
    return miss_actions == NULL || validate_action_array(ctx, miss_actions, json_path, names);
}

static bool validate_effect_explosion_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                             validation_names *names)
{
    yyjson_val *radius = obj_get(action, "radius");
    if (radius == NULL || !yyjson_is_num(radius) || yyjson_get_num(radius) < 0.0)
        return validation_error(ctx, json_path, "effect.explosion radius must be a non-negative number");

    yyjson_val *inner_radius = obj_get(action, "inner_radius");
    if (inner_radius != NULL && (!yyjson_is_num(inner_radius) || yyjson_get_num(inner_radius) < 0.0))
        return validation_error(ctx, json_path, "effect.explosion inner_radius must be non-negative");
    if (inner_radius != NULL && yyjson_get_num(inner_radius) > yyjson_get_num(radius))
        return validation_error(ctx, json_path, "effect.explosion inner_radius must not exceed radius");

    yyjson_val *damage = obj_get(action, "damage");
    yyjson_val *amount = obj_get(action, "amount");
    if (damage != NULL && amount != NULL)
        return validation_error(ctx, json_path, "effect.explosion requires at most one of damage or amount");
    if ((damage != NULL && (!yyjson_is_num(damage) || yyjson_get_num(damage) < 0.0)) ||
        (amount != NULL && (!yyjson_is_num(amount) || yyjson_get_num(amount) < 0.0)))
    {
        return validation_error(ctx, json_path, "effect.explosion damage must be non-negative");
    }

    yyjson_val *impulse = obj_get(action, "impulse");
    if (impulse != NULL && (!yyjson_is_num(impulse) || yyjson_get_num(impulse) < 0.0))
        return validation_error(ctx, json_path, "effect.explosion impulse must be non-negative");

    yyjson_val *position = obj_get(action, "position");
    yyjson_val *offset = obj_get(action, "offset");
    if (position != NULL && !is_vec_array(position, 3))
        return validation_error(ctx, json_path, "effect.explosion position must be a vec3");
    if (offset != NULL && !is_vec_array(offset, 3))
        return validation_error(ctx, json_path, "effect.explosion offset must be a vec3");

    const char *actor_ref_fields[] = {"source", "from"};
    for (size_t i = 0; i < SDL_arraysize(actor_ref_fields); ++i)
    {
        yyjson_val *value = obj_get(action, actor_ref_fields[i]);
        const char *name = json_string(action, actor_ref_fields[i]);
        if (value != NULL && !yyjson_is_str(value))
            return validation_error(ctx, json_path, "effect.explosion %s must be a string", actor_ref_fields[i]);
        if (name != NULL && !require_actor_ref(ctx, names, name, json_path))
            return false;
    }

    const char *payload_fields[] = {"source_from_payload", "from_payload"};
    for (size_t i = 0; i < SDL_arraysize(payload_fields); ++i)
    {
        yyjson_val *value = obj_get(action, payload_fields[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, json_path, "effect.explosion %s must be a non-empty string",
                                    payload_fields[i]);
    }

    const char *string_fields[] = {"target_tag", "affected_tag", "damage_type", "velocity_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, action, json_path, "effect.explosion", string_fields[i]))
            return false;
    }

    yyjson_val *falloff = obj_get(action, "falloff");
    if (falloff != NULL)
    {
        const char *value = yyjson_is_str(falloff) ? yyjson_get_str(falloff) : NULL;
        if (value == NULL ||
            (SDL_strcmp(value, "linear") != 0 && SDL_strcmp(value, "constant") != 0 && SDL_strcmp(value, "none") != 0))
        {
            return validation_error(ctx, json_path, "effect.explosion falloff must be linear, constant, or none");
        }
    }

    yyjson_val *exclude_source = obj_get(action, "exclude_source");
    if (exclude_source != NULL && !yyjson_is_bool(exclude_source))
        return validation_error(ctx, json_path, "effect.explosion exclude_source must be a boolean");
    yyjson_val *max_targets = obj_get(action, "max_targets");
    if (max_targets != NULL && (!yyjson_is_int(max_targets) || yyjson_get_sint(max_targets) < 0))
        return validation_error(ctx, json_path, "effect.explosion max_targets must be a non-negative integer");

    yyjson_val *actions = obj_get(action, "actions");
    return validate_target_filter_fields(ctx, action, json_path, "effect.explosion") &&
           (actions == NULL || validate_action_array(ctx, actions, json_path, names)) &&
           validate_optional_signal_field(ctx, action, json_path, names, "on_hit");
}

static bool validate_noop_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                 validation_names *names, const char *type)
{
    (void)ctx;
    (void)action;
    (void)json_path;
    (void)names;
    (void)type;
    return true;
}

typedef enum action_validation_rule_match
{
    ACTION_RULE_EXACT,
    ACTION_RULE_PREFIX
} action_validation_rule_match;

typedef bool (*action_validator_fn)(validation_context *ctx, yyjson_val *action, const char *json_path,
                                    validation_names *names, const char *type);

typedef struct action_validation_rule
{
    const char *name;
    action_validation_rule_match match;
    action_validator_fn validate;
} action_validation_rule;

static bool validate_combat_damage_or_heal_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                  validation_names *names, const char *type);
static bool validate_signal_emit_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type);
static bool validate_timer_start_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type);
static bool validate_property_set_or_add_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names, const char *type);
static bool validate_property_snapshot_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                              validation_names *names, const char *type);
static bool validate_property_animate_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                             validation_names *names, const char *type);
static bool validate_property_reset_defaults_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type);
static bool validate_debug_write_actor_properties_action(validation_context *ctx, yyjson_val *action,
                                                         const char *json_path, validation_names *names,
                                                         const char *type);
static bool validate_actor_spawn_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type);
static bool validate_actor_despawn_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type);
static bool validate_actor_despawn_by_tag_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type);
static bool validate_noise_emit_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type);
static bool validate_sector_door_motion_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type);
static bool validate_sector_door_interact_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type);
static bool validate_sector_lighting_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names, const char *type);
static bool validate_projectile_fire_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type);
static bool validate_controller_fps_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                           validation_names *names, const char *type);
static bool validate_grid_spawn_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type);
static bool validate_grid_pickup_layer_reset_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type);
static bool validate_input_reset_bindings_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type);
static bool validate_input_apply_profile_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names, const char *type);
static bool validate_input_apply_active_profile_action(validation_context *ctx, yyjson_val *action,
                                                       const char *json_path, validation_names *names,
                                                       const char *type);
static bool validate_input_clear_network_input_overrides_action(validation_context *ctx, yyjson_val *action,
                                                                const char *json_path, validation_names *names,
                                                                const char *type);
static bool validate_scene_state_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type);
static bool validate_scene_state_toggle_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type);
static bool validate_scene_state_cycle_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                              validation_names *names, const char *type);
static bool validate_console_write_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type);
static bool validate_network_direct_connect_start_action(validation_context *ctx, yyjson_val *action,
                                                         const char *json_path, validation_names *names,
                                                         const char *type);
static bool validate_network_named_session_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                  validation_names *names, const char *type);
static bool validate_network_host_start_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type);
static bool validate_network_discovery_start_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type);
static bool validate_network_discovery_collection_action(validation_context *ctx, yyjson_val *action,
                                                         const char *json_path, validation_names *names,
                                                         const char *type);
static bool validate_network_discovery_connect_selected_action(validation_context *ctx, yyjson_val *action,
                                                               const char *json_path, validation_names *names,
                                                               const char *type);
static bool validate_ui_animate_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type);
static bool validate_combat_kill_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type);
static bool validate_combat_revive_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type);
static bool validate_resource_add_or_consume_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type);
static bool validate_resource_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         validation_names *names, const char *type);
static bool validate_pickup_collect_action_dispatch(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type);
static bool validate_resource_station_use_action_dispatch(validation_context *ctx, yyjson_val *action,
                                                          const char *json_path, validation_names *names,
                                                          const char *type);
static bool validate_status_effect_apply_action_dispatch(validation_context *ctx, yyjson_val *action,
                                                         const char *json_path, validation_names *names,
                                                         const char *type);
static bool validate_weapon_reload_action_dispatch(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type);
static bool validate_weapon_hitscan_action_dispatch(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type);
static bool validate_interaction_use_action_dispatch(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                     validation_names *names, const char *type);
static bool validate_effect_explosion_action_dispatch(validation_context *ctx, yyjson_val *action,
                                                      const char *json_path, validation_names *names, const char *type);
static bool validate_persistence_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type);
static bool validate_entity_set_active_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                              validation_names *names, const char *type);
static bool validate_transform_set_position_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type);
static bool validate_camera_toggle_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type);
static bool validate_camera_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type);
static bool validate_scene_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                      validation_names *names, const char *type);
static bool validate_adapter_invoke_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                           validation_names *names, const char *type);
static bool validate_branch_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                   validation_names *names, const char *type);

// clang-format off
#define ACTION_RULE_EXACT_HANDLER(name, handler) {name, ACTION_RULE_EXACT, handler}
#define ACTION_RULE_PREFIX_HANDLER(name, handler) {name, ACTION_RULE_PREFIX, handler}
// clang-format on

static bool action_rule_matches(const action_validation_rule *rule, const char *type)
{
    if (rule->match == ACTION_RULE_PREFIX)
        return SDL_strncmp(type, rule->name, SDL_strlen(rule->name)) == 0;
    return SDL_strcmp(type, rule->name) == 0;
}

static const action_validation_rule *find_action_validation_rule(const char *type)
{
    static const action_validation_rule rules[] = {
        ACTION_RULE_EXACT_HANDLER("signal.emit", validate_signal_emit_action),
        ACTION_RULE_EXACT_HANDLER("timer.start", validate_timer_start_action),
        ACTION_RULE_EXACT_HANDLER("property.set", validate_property_set_or_add_action),
        ACTION_RULE_EXACT_HANDLER("property.add", validate_property_set_or_add_action),
        ACTION_RULE_EXACT_HANDLER("property.snapshot", validate_property_snapshot_action),
        ACTION_RULE_EXACT_HANDLER("property.restore_snapshot", validate_property_snapshot_action),
        ACTION_RULE_EXACT_HANDLER("property.animate", validate_property_animate_action),
        ACTION_RULE_EXACT_HANDLER("property.reset_defaults", validate_property_reset_defaults_action),
        ACTION_RULE_EXACT_HANDLER("debug.write_actor_properties", validate_debug_write_actor_properties_action),
        ACTION_RULE_EXACT_HANDLER("actor.spawn", validate_actor_spawn_action),
        ACTION_RULE_EXACT_HANDLER("actor.despawn", validate_actor_despawn_action),
        ACTION_RULE_EXACT_HANDLER("actor.despawn_by_tag", validate_actor_despawn_by_tag_action),
        ACTION_RULE_EXACT_HANDLER("combat.damage", validate_combat_damage_or_heal_action),
        ACTION_RULE_EXACT_HANDLER("combat.heal", validate_combat_damage_or_heal_action),
        ACTION_RULE_EXACT_HANDLER("combat.kill", validate_combat_kill_action),
        ACTION_RULE_EXACT_HANDLER("combat.revive", validate_combat_revive_action),
        ACTION_RULE_EXACT_HANDLER("resource.add", validate_resource_add_or_consume_action),
        ACTION_RULE_EXACT_HANDLER("resource.consume", validate_resource_add_or_consume_action),
        ACTION_RULE_EXACT_HANDLER("resource.set", validate_resource_set_action),
        ACTION_RULE_EXACT_HANDLER("pickup.collect", validate_pickup_collect_action_dispatch),
        ACTION_RULE_EXACT_HANDLER("resource.station.use", validate_resource_station_use_action_dispatch),
        ACTION_RULE_EXACT_HANDLER("status_effect.apply", validate_status_effect_apply_action_dispatch),
        ACTION_RULE_EXACT_HANDLER("weapon.reload", validate_weapon_reload_action_dispatch),
        ACTION_RULE_EXACT_HANDLER("weapon.hitscan", validate_weapon_hitscan_action_dispatch),
        ACTION_RULE_EXACT_HANDLER("interaction.use", validate_interaction_use_action_dispatch),
        ACTION_RULE_EXACT_HANDLER("effect.explosion", validate_effect_explosion_action_dispatch),
        ACTION_RULE_EXACT_HANDLER("noise.emit", validate_noise_emit_action),
        ACTION_RULE_EXACT_HANDLER("sector_door.open", validate_sector_door_motion_action),
        ACTION_RULE_EXACT_HANDLER("sector_door.close", validate_sector_door_motion_action),
        ACTION_RULE_EXACT_HANDLER("sector_door.toggle", validate_sector_door_motion_action),
        ACTION_RULE_EXACT_HANDLER("sector_door.interact", validate_sector_door_interact_action),
        ACTION_RULE_EXACT_HANDLER("sector_lighting.set", validate_sector_lighting_set_action),
        ACTION_RULE_EXACT_HANDLER("projectile.fire", validate_projectile_fire_action),
        ACTION_RULE_EXACT_HANDLER("controller.fps.launch", validate_controller_fps_action),
        ACTION_RULE_EXACT_HANDLER("controller.fps.teleport", validate_controller_fps_action),
        ACTION_RULE_EXACT_HANDLER("controller.fps.push", validate_controller_fps_action),
        ACTION_RULE_EXACT_HANDLER("controller.fps_sector.launch", validate_controller_fps_action),
        ACTION_RULE_EXACT_HANDLER("controller.fps_sector.teleport", validate_controller_fps_action),
        ACTION_RULE_EXACT_HANDLER("grid.spawn_from_glyphs", validate_grid_spawn_action),
        ACTION_RULE_EXACT_HANDLER("grid.spawn_runs_from_glyphs", validate_grid_spawn_action),
        ACTION_RULE_EXACT_HANDLER("grid.pickup_layer.reset", validate_grid_pickup_layer_reset_action),
        ACTION_RULE_EXACT_HANDLER("input.reset_bindings", validate_input_reset_bindings_action),
        ACTION_RULE_EXACT_HANDLER("input.apply_profile", validate_input_apply_profile_action),
        ACTION_RULE_EXACT_HANDLER("input.apply_active_profile", validate_input_apply_active_profile_action),
        ACTION_RULE_EXACT_HANDLER("input.clear_network_input_overrides",
                                  validate_input_clear_network_input_overrides_action),
        ACTION_RULE_EXACT_HANDLER("scene_state.set", validate_scene_state_set_action),
        ACTION_RULE_EXACT_HANDLER("scene_state.toggle", validate_scene_state_toggle_action),
        ACTION_RULE_EXACT_HANDLER("scene_state.cycle", validate_scene_state_cycle_action),
        ACTION_RULE_EXACT_HANDLER("console.write", validate_console_write_action),
        ACTION_RULE_EXACT_HANDLER("editor.selection.clear", validate_noop_action),
        ACTION_RULE_EXACT_HANDLER("editor.vertex.selection.clear", validate_noop_action),
        ACTION_RULE_EXACT_HANDLER("editor.edge.selection.clear", validate_noop_action),
        ACTION_RULE_EXACT_HANDLER("editor.tool.set_mode", validate_editor_tool_set_mode_action),
        ACTION_RULE_EXACT_HANDLER("editor.escape", validate_noop_action),
        ACTION_RULE_EXACT_HANDLER("editor.clip.cancel", validate_noop_action),
        ACTION_RULE_EXACT_HANDLER("editor.clip.escape", validate_noop_action),
        ACTION_RULE_EXACT_HANDLER("editor.clip.cycle_keep_mode", validate_noop_action),
        ACTION_RULE_EXACT_HANDLER("editor.clip.commit", validate_noop_action),
        ACTION_RULE_EXACT_HANDLER("editor.placement_preview.cancel", validate_noop_action),
        ACTION_RULE_EXACT_HANDLER("editor.vertex.delete_selected", validate_editor_vertex_delete_selected_action),
        ACTION_RULE_EXACT_HANDLER("editor.vertex.merge_selected_to_hover",
                                  validate_editor_vertex_merge_selected_to_hover_action),
        ACTION_RULE_EXACT_HANDLER("editor.vertex.add_to_source", validate_editor_vertex_add_to_source_action),
        ACTION_RULE_EXACT_HANDLER("editor.vertex.validate_source", validate_editor_vertex_validate_source_action),
        ACTION_RULE_EXACT_HANDLER("editor.vertex.snap_selected", validate_editor_vertex_snap_selected_action),
        ACTION_RULE_EXACT_HANDLER("editor.selection.select_brush", validate_editor_selection_select_brush_action),
        ACTION_RULE_EXACT_HANDLER("editor.selection.delete_selected", validate_editor_selection_delete_selected_action),
        ACTION_RULE_EXACT_HANDLER("editor.selection.resize_y", validate_editor_selection_resize_y_action),
        ACTION_RULE_EXACT_HANDLER("editor.selection.rotate_selected", validate_editor_selection_rotate_selected_action),
        ACTION_RULE_EXACT_HANDLER("editor.selection.scale_selected", validate_editor_selection_scale_selected_action),
        ACTION_RULE_EXACT_HANDLER("editor.selection.flip_vertical", validate_editor_selection_flip_vertical_action),
        ACTION_RULE_EXACT_HANDLER("editor.selection.flip_horizontal", validate_editor_selection_flip_horizontal_action),
        ACTION_RULE_EXACT_HANDLER("editor.brush.duplicate", validate_editor_brush_duplicate_action),
        ACTION_RULE_EXACT_HANDLER("editor.brush.paint", validate_editor_brush_paint_action),
        ACTION_RULE_EXACT_HANDLER("editor.selection.shear_selected", validate_editor_selection_shear_selected_action),
        ACTION_RULE_EXACT_HANDLER("editor.selection.run", validate_editor_selection_run_action),
        ACTION_RULE_EXACT_HANDLER("editor.command.preview", validate_editor_command_preview_action),
        ACTION_RULE_EXACT_HANDLER("editor.command.clear_preview", validate_editor_command_clear_preview_action),
        ACTION_RULE_EXACT_HANDLER("editor.command.commit", validate_editor_command_history_action),
        ACTION_RULE_EXACT_HANDLER("editor.command.undo", validate_editor_command_history_action),
        ACTION_RULE_EXACT_HANDLER("editor.command.redo", validate_editor_command_history_action),
        ACTION_RULE_EXACT_HANDLER("editor.brush_world.export", validate_editor_brush_world_export_action),
        ACTION_RULE_EXACT_HANDLER("editor.level.export", validate_editor_level_export_action),
        ACTION_RULE_EXACT_HANDLER("editor.level.save", validate_editor_level_save_action),
        ACTION_RULE_EXACT_HANDLER("editor.level.load", validate_editor_level_load_action),
        ACTION_RULE_EXACT_HANDLER("editor.test_run.prepare", validate_editor_test_run_prepare_action),
        ACTION_RULE_EXACT_HANDLER("editor.test_run.save_manifest", validate_editor_test_run_save_manifest_action),
        ACTION_RULE_EXACT_HANDLER("editor.brush_world.status", validate_editor_brush_world_status_action),
        ACTION_RULE_EXACT_HANDLER("editor.brush_world.validate_source",
                                  validate_editor_brush_world_validate_source_action),
        ACTION_RULE_EXACT_HANDLER("editor.brush_world.validate_enclosure",
                                  validate_editor_brush_world_validate_enclosure_action),
        ACTION_RULE_EXACT_HANDLER("editor.brush_world.create_box", validate_editor_brush_world_create_box_action),
        ACTION_RULE_EXACT_HANDLER("editor.player_start.place", validate_editor_player_start_place_action),
        ACTION_RULE_EXACT_HANDLER("editor.player_start.apply", validate_editor_player_start_apply_action),
        ACTION_RULE_EXACT_HANDLER("editor.player_start.delete", validate_editor_player_start_delete_action),
        ACTION_RULE_EXACT_HANDLER("editor.actor.place", validate_editor_actor_place_action),
        ACTION_RULE_EXACT_HANDLER("network.direct_connect.start", validate_network_direct_connect_start_action),
        ACTION_RULE_EXACT_HANDLER("network.direct_connect.cancel", validate_network_named_session_action),
        ACTION_RULE_EXACT_HANDLER("network.direct_connect.observe", validate_network_named_session_action),
        ACTION_RULE_EXACT_HANDLER("network.host.start", validate_network_host_start_action),
        ACTION_RULE_EXACT_HANDLER("network.host.cancel", validate_network_named_session_action),
        ACTION_RULE_EXACT_HANDLER("network.host.observe", validate_network_named_session_action),
        ACTION_RULE_EXACT_HANDLER("network.discovery.start", validate_network_discovery_start_action),
        ACTION_RULE_EXACT_HANDLER("network.discovery.refresh", validate_network_discovery_start_action),
        ACTION_RULE_EXACT_HANDLER("network.discovery.observe", validate_network_discovery_collection_action),
        ACTION_RULE_EXACT_HANDLER("network.discovery.cancel", validate_network_discovery_collection_action),
        ACTION_RULE_EXACT_HANDLER("network.discovery.connect_selected",
                                  validate_network_discovery_connect_selected_action),
        ACTION_RULE_EXACT_HANDLER("ui.animate", validate_ui_animate_action),
        ACTION_RULE_PREFIX_HANDLER("audio.", validate_audio_action),
        ACTION_RULE_EXACT_HANDLER("persistence.load", validate_persistence_action),
        ACTION_RULE_EXACT_HANDLER("persistence.save", validate_persistence_action),
        ACTION_RULE_EXACT_HANDLER("entity.set_active", validate_entity_set_active_action),
        ACTION_RULE_EXACT_HANDLER("transform.set_position", validate_transform_set_position_action),
        ACTION_RULE_EXACT_HANDLER("camera.toggle", validate_camera_toggle_action),
        ACTION_RULE_EXACT_HANDLER("camera.set", validate_camera_set_action),
        ACTION_RULE_EXACT_HANDLER("scene.set", validate_scene_set_action),
        ACTION_RULE_EXACT_HANDLER("adapter.invoke", validate_adapter_invoke_action),
        ACTION_RULE_EXACT_HANDLER("branch", validate_branch_action),
    };
    for (size_t i = 0; i < SDL_arraysize(rules); ++i)
    {
        if (action_rule_matches(&rules[i], type))
            return &rules[i];
    }
    return NULL;
}

static bool validate_action_with_rule(validation_context *ctx, yyjson_val *action, const char *json_path,
                                      validation_names *names, const char *type, const action_validation_rule *rule)
{
    if (rule == NULL || rule->validate == NULL)
        return validation_error(ctx, json_path, "unsupported logic action type '%s'", type);
    return rule->validate(ctx, action, json_path, names, type);
}

bool validate_one_action(validation_context *ctx, yyjson_val *action, const char *json_path, validation_names *names)
{
    if (!yyjson_is_obj(action))
        return validation_error(ctx, json_path, "logic action must be an object");
    const char *type = json_string(action, "type");
    if (type == NULL || type[0] == '\0')
        return validation_error(ctx, json_path, "logic action requires a non-empty type");
    return validate_action_with_rule(ctx, action, json_path, names, type, find_action_validation_rule(type));
}

bool validate_action_array(validation_context *ctx, yyjson_val *actions, const char *json_path, validation_names *names)
{
    if (!yyjson_is_arr(actions))
        return validation_error(ctx, json_path, "logic action list must be an array");
    for (size_t i = 0; i < yyjson_arr_size(actions); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        if (!validate_one_action(ctx, yyjson_arr_get(actions, i), path, names))
            return false;
    }
    return true;
}

static bool validate_signal_emit_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type)
{
    (void)type;
    return require_ref(ctx, &names->signals, "signal", json_string(action, "signal"), json_path);
}

static bool validate_timer_start_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type)
{
    (void)type;
    return require_ref(ctx, &names->timers, "timer", json_string(action, "timer"), json_path);
}

static bool validate_property_set_or_add_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names, const char *type)
{
    yyjson_val *target_value = obj_get(action, "target");
    yyjson_val *target_from_payload_value = obj_get(action, "target_from_payload");
    const char *target = json_string(action, "target");
    const char *target_from_payload = json_string(action, "target_from_payload");
    if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of target or target_from_payload", type);
    if (target_value != NULL && !yyjson_is_str(target_value))
        return validation_error(ctx, json_path, "%s target must be a string", type);
    if (target_from_payload_value != NULL && !yyjson_is_str(target_from_payload_value))
        return validation_error(ctx, json_path, "%s target_from_payload must be a string", type);
    if (target != NULL && !require_ref(ctx, &names->entities, "entity", target, json_path))
        return false;
    if (target_from_payload != NULL && target_from_payload[0] == '\0')
        return validation_error(ctx, json_path, "%s target_from_payload must be non-empty", type);
    if (!is_non_empty_string(action, "key"))
        return validation_error(ctx, json_path, "%s requires a non-empty key", type);

    yyjson_val *value = obj_get(action, "value");
    yyjson_val *value_from_payload_value = obj_get(action, "value_from_payload");
    const char *value_from_payload = json_string(action, "value_from_payload");
    if ((value == NULL && value_from_payload == NULL) || (value != NULL && value_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of value or value_from_payload", type);
    if (value_from_payload_value != NULL && !yyjson_is_str(value_from_payload_value))
        return validation_error(ctx, json_path, "%s value_from_payload must be a string", type);
    if (value_from_payload != NULL && value_from_payload[0] == '\0')
        return validation_error(ctx, json_path, "%s value_from_payload must be non-empty", type);
    return true;
}

static bool validate_property_snapshot_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                              validation_names *names, const char *type)
{
    if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
        return false;
    if (!is_non_empty_string(action, "name"))
        return validation_error(ctx, json_path, "%s requires a non-empty name", type);
    yyjson_val *keys = obj_get(action, "keys");
    if (keys != NULL && !yyjson_is_arr(keys))
        return validation_error(ctx, json_path, "%s keys must be an array", type);
    for (size_t i = 0; yyjson_is_arr(keys) && i < yyjson_arr_size(keys); ++i)
    {
        if (!yyjson_is_str(yyjson_arr_get(keys, i)))
            return validation_error(ctx, json_path, "%s keys must be strings", type);
    }
    return true;
}

static bool validate_property_animate_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                             validation_names *names, const char *type)
{
    (void)type;
    if (!validate_actor_target_action(ctx, action, json_path, names, "property.animate", "target",
                                      "target_from_payload"))
    {
        return false;
    }
    if (!is_non_empty_string(action, "key"))
        return validation_error(ctx, json_path, "property.animate requires a non-empty key");
    return validate_animation_common(ctx, action, json_path, names);
}

static bool validate_property_reset_defaults_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type)
{
    (void)type;
    if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
        return false;
    yyjson_val *keys = obj_get(action, "keys");
    if (keys != NULL && !yyjson_is_arr(keys))
        return validation_error(ctx, json_path, "property.reset_defaults keys must be an array");
    for (size_t i = 0; yyjson_is_arr(keys) && i < yyjson_arr_size(keys); ++i)
    {
        if (!yyjson_is_str(yyjson_arr_get(keys, i)))
            return validation_error(ctx, json_path, "property.reset_defaults keys must be strings");
    }
    return true;
}

static bool validate_debug_write_actor_properties_action(validation_context *ctx, yyjson_val *action,
                                                         const char *json_path, validation_names *names,
                                                         const char *type)
{
    (void)type;
    if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
        return false;
    if (!is_non_empty_string(action, "path"))
        return validation_error(ctx, json_path, "debug.write_actor_properties requires a non-empty path");
    yyjson_val *append = obj_get(action, "append");
    if (append != NULL && !yyjson_is_bool(append))
        return validation_error(ctx, json_path, "debug.write_actor_properties append must be a boolean");
    yyjson_val *properties = obj_get(action, "properties");
    if (!yyjson_is_arr(properties) || yyjson_arr_size(properties) == 0)
        return validation_error(ctx, json_path, "debug.write_actor_properties requires a non-empty properties array");
    for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
    {
        yyjson_val *property = yyjson_arr_get(properties, i);
        if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
            return validation_error(ctx, json_path,
                                    "debug.write_actor_properties properties must be non-empty strings");
    }
    return true;
}

static bool validate_actor_spawn_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type)
{
    (void)type;
    if (!require_ref(ctx, &names->actor_pools, "actor pool", json_string(action, "pool"), json_path))
        return false;
    const char *from = json_string(action, "from");
    if (from != NULL && from[0] == '\0')
        return validation_error(ctx, json_path, "actor.spawn from requires a non-empty actor reference");
    if (from != NULL && !name_table_contains(&names->entities, from) &&
        !name_table_contains(&names->actor_pool_actors, from))
    {
        return validation_error(ctx, json_path, "unknown actor.spawn from actor reference '%s'", from);
    }
    yyjson_val *from_payload = obj_get(action, "from_payload");
    if (from_payload != NULL && (!yyjson_is_str(from_payload) || yyjson_get_str(from_payload)[0] == '\0'))
        return validation_error(ctx, json_path, "actor.spawn from_payload must be a non-empty string");
    yyjson_val *position = obj_get(action, "position");
    if (position != NULL && !is_vec_array(position, 3))
        return validation_error(ctx, json_path, "actor.spawn position must be a vec3");
    yyjson_val *position_from_payload = obj_get(action, "position_from_payload");
    if (position_from_payload != NULL &&
        (!yyjson_is_str(position_from_payload) || yyjson_get_str(position_from_payload)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "actor.spawn position_from_payload must be a non-empty string");
    }
    yyjson_val *position_from_actor_properties = obj_get(action, "position_from_actor_properties");
    if (position_from_actor_properties != NULL)
    {
        if (!yyjson_is_obj(position_from_actor_properties))
            return validation_error(ctx, json_path, "actor.spawn position_from_actor_properties must be an object");
        if (!require_actor_ref(ctx, names, json_string(position_from_actor_properties, "source"), json_path))
            return false;
        const char *x = json_string(position_from_actor_properties, "x");
        const char *y = json_string(position_from_actor_properties, "y");
        const char *z = json_string(position_from_actor_properties, "z");
        if (x == NULL || x[0] == '\0' || y == NULL || y[0] == '\0' || z == NULL || z[0] == '\0')
        {
            return validation_error(ctx, json_path,
                                    "actor.spawn position_from_actor_properties requires non-empty x, y, and z "
                                    "property names");
        }
        yyjson_val *property_offset = obj_get(position_from_actor_properties, "offset");
        if (property_offset != NULL && !is_vec_array(property_offset, 3))
            return validation_error(ctx, json_path, "actor.spawn position_from_actor_properties offset must be a vec3");
        const char *additive_fields[] = {"x_add", "y_add", "z_add"};
        for (size_t additive_index = 0; additive_index < SDL_arraysize(additive_fields); ++additive_index)
        {
            yyjson_val *additive = obj_get(position_from_actor_properties, additive_fields[additive_index]);
            if (additive == NULL)
                continue;
            if (!yyjson_is_arr(additive))
                return validation_error(ctx, json_path,
                                        "actor.spawn position_from_actor_properties additive fields must be arrays");
            for (size_t property_index = 0; property_index < yyjson_arr_size(additive); ++property_index)
            {
                yyjson_val *property = yyjson_arr_get(additive, property_index);
                if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
                {
                    return validation_error(ctx, json_path,
                                            "actor.spawn position_from_actor_properties additive fields must "
                                            "contain non-empty strings");
                }
            }
        }
    }
    yyjson_val *offset = obj_get(action, "offset");
    if (offset != NULL && !is_vec_array(offset, 3))
        return validation_error(ctx, json_path, "actor.spawn offset must be a vec3");
    yyjson_val *directional_offset = obj_get(action, "directional_offset");
    if (directional_offset != NULL)
    {
        if (!yyjson_is_obj(directional_offset))
            return validation_error(ctx, json_path, "actor.spawn directional_offset must be an object");
        yyjson_val *property = obj_get(directional_offset, "property");
        yyjson_val *distance = obj_get(directional_offset, "distance");
        if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
            return validation_error(ctx, json_path,
                                    "actor.spawn directional_offset property must be a non-empty string");
        if (!yyjson_is_num(distance))
            return validation_error(ctx, json_path, "actor.spawn directional_offset distance must be numeric");
    }
    yyjson_val *payload_directional_offset = obj_get(action, "payload_directional_offset");
    if (payload_directional_offset != NULL)
    {
        if (!yyjson_is_obj(payload_directional_offset))
            return validation_error(ctx, json_path, "actor.spawn payload_directional_offset must be an object");
        yyjson_val *property = obj_get(payload_directional_offset, "property");
        yyjson_val *distance = obj_get(payload_directional_offset, "distance");
        if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
            return validation_error(ctx, json_path,
                                    "actor.spawn payload_directional_offset property must be a non-empty string");
        if (!yyjson_is_num(distance))
            return validation_error(ctx, json_path, "actor.spawn payload_directional_offset distance must be numeric");
    }
    yyjson_val *velocity_from_payload = obj_get(action, "velocity_from_payload");
    if (velocity_from_payload != NULL &&
        (!yyjson_is_str(velocity_from_payload) || yyjson_get_str(velocity_from_payload)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "actor.spawn velocity_from_payload must be a non-empty string");
    }
    yyjson_val *velocity_property = obj_get(action, "velocity_property");
    if (velocity_property != NULL &&
        (!yyjson_is_str(velocity_property) || yyjson_get_str(velocity_property)[0] == '\0'))
        return validation_error(ctx, json_path, "actor.spawn velocity_property must be a non-empty string");
    yyjson_val *speed = obj_get(action, "speed");
    if (speed != NULL && (!yyjson_is_num(speed) || yyjson_get_num(speed) < 0.0))
        return validation_error(ctx, json_path, "actor.spawn speed must be non-negative");
    yyjson_val *properties = obj_get(action, "properties");
    if (properties != NULL && !yyjson_is_obj(properties))
        return validation_error(ctx, json_path, "actor.spawn properties must be an object");
    yyjson_val *properties_from_actor = obj_get(action, "properties_from_actor");
    if (properties_from_actor != NULL)
    {
        if (!yyjson_is_obj(properties_from_actor))
            return validation_error(ctx, json_path, "actor.spawn properties_from_actor must be an object");
        if (!require_actor_ref(ctx, names, json_string(properties_from_actor, "source"), json_path))
            return false;
        yyjson_val *keys = obj_get(properties_from_actor, "keys");
        if (!yyjson_is_arr(keys) || yyjson_arr_size(keys) == 0)
            return validation_error(ctx, json_path, "actor.spawn properties_from_actor keys must be a non-empty array");
        for (size_t i = 0; i < yyjson_arr_size(keys); ++i)
        {
            yyjson_val *key = yyjson_arr_get(keys, i);
            if (!yyjson_is_str(key) || yyjson_get_str(key)[0] == '\0')
                return validation_error(ctx, json_path,
                                        "actor.spawn properties_from_actor keys must be non-empty strings");
        }
    }
    return true;
}

static bool validate_actor_despawn_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type)
{
    (void)type;
    yyjson_val *target_value = obj_get(action, "target");
    yyjson_val *target_from_payload_value = obj_get(action, "target_from_payload");
    const char *target = json_string(action, "target");
    const char *target_from_payload = json_string(action, "target_from_payload");
    if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
        return validation_error(ctx, json_path, "actor.despawn requires exactly one of target or target_from_payload");
    if (target_value != NULL && !yyjson_is_str(target_value))
        return validation_error(ctx, json_path, "actor.despawn target must be a string");
    if (target_from_payload_value != NULL && !yyjson_is_str(target_from_payload_value))
        return validation_error(ctx, json_path, "actor.despawn target_from_payload must be a string");
    if (target != NULL && target[0] == '\0')
        return validation_error(ctx, json_path, "actor.despawn target must be non-empty");
    if (target_from_payload != NULL && target_from_payload[0] == '\0')
        return validation_error(ctx, json_path, "actor.despawn target_from_payload must be non-empty");
    if (target != NULL && !name_table_contains(&names->entities, target) &&
        !name_table_contains(&names->actor_pool_actors, target))
        return validation_error(ctx, json_path, "unknown actor.despawn target '%s'", target);
    yyjson_val *reason = obj_get(action, "reason");
    if (reason != NULL && !yyjson_is_str(reason))
        return validation_error(ctx, json_path, "actor.despawn reason must be a string");
    return true;
}

static bool validate_actor_despawn_by_tag_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    if (!is_non_empty_string(action, "tag"))
        return validation_error(ctx, json_path, "actor.despawn_by_tag requires a non-empty tag");
    yyjson_val *reason = obj_get(action, "reason");
    if (reason != NULL && !yyjson_is_str(reason))
        return validation_error(ctx, json_path, "actor.despawn_by_tag reason must be a string");
    return true;
}

static bool validate_noise_emit_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type)
{
    (void)type;
    const char *source = json_string(action, "source");
    const char *actor = json_string(action, "actor");
    const char *target = json_string(action, "target");
    if (source != NULL && !require_actor_ref(ctx, names, source, json_path))
        return false;
    if (actor != NULL && !require_actor_ref(ctx, names, actor, json_path))
        return false;
    if (target != NULL && !require_actor_ref(ctx, names, target, json_path))
        return false;

    const char *payload_fields[] = {"source_from_payload", "actor_from_payload", "target_from_payload", "from_payload"};
    for (size_t i = 0; i < SDL_arraysize(payload_fields); ++i)
    {
        yyjson_val *value = obj_get(action, payload_fields[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, json_path, "noise.emit %s must be a non-empty string", payload_fields[i]);
    }

    yyjson_val *from = obj_get(action, "from");
    if (from != NULL && (!yyjson_is_str(from) || yyjson_get_str(from)[0] == '\0'))
        return validation_error(ctx, json_path, "noise.emit from must be a non-empty actor reference");
    const char *from_actor = json_string(action, "from");
    if (from_actor != NULL && !require_actor_ref(ctx, names, from_actor, json_path))
        return false;
    yyjson_val *position = obj_get(action, "position");
    yyjson_val *offset = obj_get(action, "offset");
    if (position != NULL && !is_vec_array(position, 3))
        return validation_error(ctx, json_path, "noise.emit position must be a vec3");
    if (offset != NULL && !is_vec_array(offset, 3))
        return validation_error(ctx, json_path, "noise.emit offset must be a vec3");
    yyjson_val *radius = obj_get(action, "radius");
    yyjson_val *range = obj_get(action, "range");
    yyjson_val *loudness = obj_get(action, "loudness");
    yyjson_val *duration = obj_get(action, "duration");
    yyjson_val *duration_seconds = obj_get(action, "duration_seconds");
    if (radius != NULL && (!yyjson_is_num(radius) || yyjson_get_num(radius) <= 0.0))
        return validation_error(ctx, json_path, "noise.emit radius must be positive");
    if (range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) <= 0.0))
        return validation_error(ctx, json_path, "noise.emit range must be positive");
    if (loudness != NULL && (!yyjson_is_num(loudness) || yyjson_get_num(loudness) < 0.0))
        return validation_error(ctx, json_path, "noise.emit loudness must be non-negative");
    if (duration != NULL && (!yyjson_is_num(duration) || yyjson_get_num(duration) <= 0.0))
        return validation_error(ctx, json_path, "noise.emit duration must be positive");
    if (duration_seconds != NULL && (!yyjson_is_num(duration_seconds) || yyjson_get_num(duration_seconds) <= 0.0))
        return validation_error(ctx, json_path, "noise.emit duration_seconds must be positive");
    return true;
}

static bool validate_sector_door_motion_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    (void)type;
    const char *target = json_string(action, "target");
    const char *target_from_payload = json_string(action, "target_from_payload");
    if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
    {
        return validation_error(ctx, json_path,
                                "sector door action requires exactly one of target or target_from_payload");
    }
    if (target != NULL && !require_ref(ctx, &names->sector_doors, "sector door", target, json_path))
        return false;
    if (target_from_payload != NULL && target_from_payload[0] == '\0')
        return validation_error(ctx, json_path, "sector door target_from_payload must be non-empty");
    yyjson_val *stay = obj_get(action, "stay_open_seconds");
    yyjson_val *auto_close = obj_get(action, "auto_close_seconds");
    if ((stay != NULL && (!yyjson_is_num(stay) || yyjson_get_num(stay) < 0.0)) ||
        (auto_close != NULL && (!yyjson_is_num(auto_close) || yyjson_get_num(auto_close) < 0.0)))
    {
        return validation_error(ctx, json_path, "sector door auto-close timing must be non-negative");
    }
    return true;
}

static bool validate_sector_door_interact_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type)
{
    (void)type;
    if (!require_actor_ref(ctx, names, json_string(action, "actor"), json_path))
        return false;
    yyjson_val *range = obj_get(action, "range");
    yyjson_val *min_dot = obj_get(action, "min_dot");
    if ((range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) < 0.0)) ||
        (min_dot != NULL &&
         (!yyjson_is_num(min_dot) || yyjson_get_num(min_dot) < -1.0 || yyjson_get_num(min_dot) > 1.0)))
    {
        return validation_error(ctx, json_path, "sector door interaction range/min_dot are invalid");
    }
    yyjson_val *yaw_property = obj_get(action, "yaw_property");
    if (yaw_property != NULL && !is_non_empty_string(action, "yaw_property"))
        return validation_error(ctx, json_path, "sector door yaw_property must be non-empty");
    yyjson_val *actions = obj_get(action, "actions");
    const char *signal = json_string(action, "signal");
    if ((actions == NULL && signal == NULL) || (actions != NULL && signal != NULL))
        return validation_error(ctx, json_path, "sector_door.interact requires exactly one of actions or signal");
    if (actions != NULL)
        return validate_action_array(ctx, actions, json_path, names);
    return require_ref(ctx, &names->signals, "signal", signal, json_path);
}

static bool validate_sector_lighting_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names, const char *type)
{
    (void)type;
    const char *sector_level = json_string(action, "sector_level");
    const char *sector = json_string(action, "sector");
    yyjson_val *sector_index = obj_get(action, "sector_index");
    if (!require_ref(ctx, &names->sector_levels, "sector level", sector_level, json_path))
        return false;
    if ((sector == NULL && sector_index == NULL) || (sector != NULL && sector_index != NULL))
        return validation_error(ctx, json_path, "sector_lighting.set requires exactly one of sector or sector_index");
    if (sector != NULL && sector[0] == '\0')
        return validation_error(ctx, json_path, "sector_lighting.set sector must be non-empty");
    if (sector_index != NULL && (!yyjson_is_int(sector_index) || yyjson_get_int(sector_index) < 0))
        return validation_error(ctx, json_path, "sector_lighting.set sector_index must be non-negative");
    yyjson_val *level = obj_get(action, "level");
    if (level != NULL && (!yyjson_is_num(level) || yyjson_get_num(level) < 0.0 || yyjson_get_num(level) > 255.0))
        return validation_error(ctx, json_path, "sector_lighting.set level must be in [0, 255]");
    yyjson_val *color = obj_get(action, "color");
    if (color != NULL && (!is_exact_vec3_or_vec4_array(color) || !numeric_array_values_in_range(color, 0.0, 1.0)))
    {
        return validation_error(ctx, json_path,
                                "sector_lighting.set color must be a vec3 or vec4 with values in [0, 1]");
    }
    return true;
}

static bool validate_projectile_fire_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type)
{
    (void)type;
    return validate_projectile_fire_shape(ctx, action, json_path, names, true);
}

static bool validate_controller_fps_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                           validation_names *names, const char *type)
{
    yyjson_val *target_value = obj_get(action, "target");
    yyjson_val *target_from_payload_value = obj_get(action, "target_from_payload");
    const char *target = json_string(action, "target");
    const char *target_from_payload = json_string(action, "target_from_payload");
    if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of target or target_from_payload", type);
    if (target_value != NULL && !yyjson_is_str(target_value))
        return validation_error(ctx, json_path, "%s target must be a string", type);
    if (target != NULL && !require_ref(ctx, &names->entities, "entity", target, json_path))
        return false;
    if (target_from_payload_value != NULL &&
        (!yyjson_is_str(target_from_payload_value) || yyjson_get_str(target_from_payload_value)[0] == '\0'))
        return validation_error(ctx, json_path, "%s target_from_payload must be a non-empty string", type);
    if (SDL_strcmp(type, "controller.fps.launch") == 0 || SDL_strcmp(type, "controller.fps_sector.launch") == 0)
    {
        yyjson_val *vertical_velocity = obj_get(action, "vertical_velocity");
        if (!yyjson_is_num(vertical_velocity) || yyjson_get_num(vertical_velocity) <= 0.0)
            return validation_error(ctx, json_path, "%s requires positive vertical_velocity", type);
        return true;
    }
    if (SDL_strcmp(type, "controller.fps.push") == 0)
    {
        if (!is_vec_array(obj_get(action, "velocity"), 3))
            return validation_error(ctx, json_path, "%s requires a vec3 velocity", type);
        yyjson_val *scale_by_dt = obj_get(action, "scale_by_dt");
        if (scale_by_dt != NULL && !yyjson_is_bool(scale_by_dt))
            return validation_error(ctx, json_path, "%s scale_by_dt must be a boolean", type);
        return true;
    }

    if (!is_vec_array(obj_get(action, "position"), 3))
        return validation_error(ctx, json_path, "%s requires a vec3 position", type);
    yyjson_val *yaw = obj_get(action, "yaw");
    yyjson_val *pitch = obj_get(action, "pitch");
    if ((yaw != NULL && !yyjson_is_num(yaw)) || (pitch != NULL && !yyjson_is_num(pitch)))
        return validation_error(ctx, json_path, "%s yaw and pitch must be numeric", type);
    return true;
}

static bool validate_grid_spawn_axis(validation_context *ctx, const char *json_path, const char *action_type,
                                     const char *axis, const char *message_prefix)
{
    if (axis == NULL || SDL_strcmp(axis, "x") == 0 || SDL_strcmp(axis, "horizontal") == 0 ||
        SDL_strcmp(axis, "row") == 0 || SDL_strcmp(axis, "y") == 0 || SDL_strcmp(axis, "vertical") == 0 ||
        SDL_strcmp(axis, "column") == 0)
    {
        return true;
    }
    return validation_error(ctx, json_path, "%s %saxis must be x, y, horizontal, vertical, row, or column", action_type,
                            message_prefix);
}

static bool validate_grid_spawn_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type)
{
    if (!require_ref(ctx, &names->grid_maps, "grid map", json_string(action, "map"), json_path))
        return false;
    yyjson_val *spawns = obj_get(action, "spawns");
    if (!yyjson_is_arr(spawns) || yyjson_arr_size(spawns) <= 0)
        return validation_error(ctx, json_path, "%s requires a non-empty spawns array", type);
    yyjson_val *properties = obj_get(action, "properties");
    if (properties != NULL && !yyjson_is_obj(properties))
        return validation_error(ctx, json_path, "%s properties must be an object", type);
    yyjson_val *z = obj_get(action, "z");
    if (z != NULL && !yyjson_is_num(z))
        return validation_error(ctx, json_path, "%s z must be numeric", type);
    yyjson_val *depth = obj_get(action, "depth");
    if (depth != NULL && !yyjson_is_num(depth))
        return validation_error(ctx, json_path, "%s depth must be numeric", type);
    yyjson_val *inset = obj_get(action, "inset");
    if (inset != NULL && !yyjson_is_num(inset))
        return validation_error(ctx, json_path, "%s inset must be numeric", type);
    yyjson_val *size = obj_get(action, "size");
    if (size != NULL && !is_vec_array(size, 3))
        return validation_error(ctx, json_path, "%s size must be a vec3", type);
    if (!validate_grid_spawn_axis(ctx, json_path, type, json_string(action, "axis"), ""))
        return false;
    yyjson_val *output_count_key = obj_get(action, "output_count_key");
    if (output_count_key != NULL && !is_non_empty_string(action, "output_count_key"))
        return validation_error(ctx, json_path, "%s output_count_key must be non-empty", type);
    for (size_t i = 0; i < yyjson_arr_size(spawns); ++i)
    {
        char spawn_path[PATH_BUFFER_SIZE];
        format_path(spawn_path, sizeof(spawn_path), "%s.spawns[%zu]", json_path, i);
        yyjson_val *spawn = yyjson_arr_get(spawns, i);
        if (!yyjson_is_obj(spawn))
            return validation_error(ctx, spawn_path, "grid spawn entries must be objects");
        if (!is_single_byte_string(obj_get(spawn, "glyph")))
            return validation_error(ctx, spawn_path, "grid spawn glyph must be a single-byte string");
        if (!require_ref(ctx, &names->actor_pools, "actor pool", json_string(spawn, "pool"), spawn_path))
            return false;
        yyjson_val *spawn_properties = obj_get(spawn, "properties");
        if (spawn_properties != NULL && !yyjson_is_obj(spawn_properties))
            return validation_error(ctx, spawn_path, "grid spawn properties must be an object");
        yyjson_val *spawn_z = obj_get(spawn, "z");
        if (spawn_z != NULL && !yyjson_is_num(spawn_z))
            return validation_error(ctx, spawn_path, "grid spawn z must be numeric");
        yyjson_val *spawn_depth = obj_get(spawn, "depth");
        if (spawn_depth != NULL && !yyjson_is_num(spawn_depth))
            return validation_error(ctx, spawn_path, "grid spawn depth must be numeric");
        yyjson_val *spawn_inset = obj_get(spawn, "inset");
        if (spawn_inset != NULL && !yyjson_is_num(spawn_inset))
            return validation_error(ctx, spawn_path, "grid spawn inset must be numeric");
        yyjson_val *spawn_size = obj_get(spawn, "size");
        if (spawn_size != NULL && !is_vec_array(spawn_size, 3))
            return validation_error(ctx, spawn_path, "grid spawn size must be a vec3");
        if (!validate_grid_spawn_axis(ctx, spawn_path, "grid spawn", json_string(spawn, "axis"), ""))
            return false;
    }
    return true;
}

static bool validate_grid_pickup_layer_reset_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type)
{
    (void)type;
    if (!require_ref(ctx, &names->grid_pickup_layers, "grid pickup layer", json_string(action, "layer"), json_path))
        return false;
    yyjson_val *output_count_key = obj_get(action, "output_count_key");
    if (output_count_key != NULL && !is_non_empty_string(action, "output_count_key"))
        return validation_error(ctx, json_path, "grid.pickup_layer.reset output_count_key must be non-empty");
    return true;
}

static bool validate_input_reset_bindings_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                 validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    if (!is_non_empty_string(action, "menu"))
        return validation_error(ctx, json_path, "input.reset_bindings requires a non-empty menu");
    return true;
}

static bool validate_input_apply_profile_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                validation_names *names, const char *type)
{
    (void)type;
    return require_ref(ctx, &names->input_profiles, "input profile", json_string(action, "profile"), json_path);
}

static bool validate_input_apply_active_profile_action(validation_context *ctx, yyjson_val *action,
                                                       const char *json_path, validation_names *names, const char *type)
{
    (void)action;
    (void)type;
    if (names->input_profiles.count <= 0)
        return validation_error(ctx, json_path, "input.apply_active_profile requires at least one input profile");
    return true;
}

static bool validate_input_clear_network_input_overrides_action(validation_context *ctx, yyjson_val *action,
                                                                const char *json_path, validation_names *names,
                                                                const char *type)
{
    (void)type;
    return require_ref(ctx, &names->network_input_channels, "network input channel", json_string(action, "channel"),
                       json_path);
}

static bool validate_scene_state_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                            validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    if (!is_non_empty_string(action, "key"))
        return validation_error(ctx, json_path, "scene_state.set requires a non-empty key");
    yyjson_val *value = obj_get(action, "value");
    if (value == NULL ||
        !(yyjson_is_bool(value) || yyjson_is_num(value) || yyjson_is_str(value) || is_exact_vec_array(value, 3)))
    {
        return validation_error(ctx, json_path, "scene_state.set requires a scalar or vec3 value");
    }
    return true;
}

static bool validate_scene_state_toggle_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    if (!is_non_empty_string(action, "key"))
        return validation_error(ctx, json_path, "scene_state.toggle requires a non-empty key");
    yyjson_val *default_value = obj_get(action, "default");
    if (default_value != NULL && !yyjson_is_bool(default_value))
        return validation_error(ctx, json_path, "scene_state.toggle default must be a boolean");
    return true;
}

static bool validate_scene_state_cycle_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                              validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    if (!is_non_empty_string(action, "key"))
        return validation_error(ctx, json_path, "scene_state.cycle requires a non-empty key");
    yyjson_val *values = obj_get(action, "values");
    if (!yyjson_is_arr(values) || yyjson_arr_size(values) == 0)
        return validation_error(ctx, json_path, "scene_state.cycle requires a non-empty values array");
    for (size_t i = 0; i < yyjson_arr_size(values); ++i)
    {
        yyjson_val *value = yyjson_arr_get(values, i);
        if (!(yyjson_is_bool(value) || yyjson_is_num(value) || yyjson_is_str(value)))
            return validation_error(ctx, json_path, "scene_state.cycle values must be scalar");
    }
    yyjson_val *default_value = obj_get(action, "default");
    if (default_value != NULL &&
        !(yyjson_is_bool(default_value) || yyjson_is_num(default_value) || yyjson_is_str(default_value)))
        return validation_error(ctx, json_path, "scene_state.cycle default must be scalar");
    yyjson_val *direction = obj_get(action, "direction");
    if (direction != NULL && (!yyjson_is_int(direction) || yyjson_get_int(direction) == 0))
        return validation_error(ctx, json_path, "scene_state.cycle direction must be a non-zero integer");
    return true;
}

static bool validate_console_write_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    yyjson_val *message = obj_get(action, "message");
    yyjson_val *message_from_state = obj_get(action, "message_from_state");
    const int message_fields = (message != NULL ? 1 : 0) + (message_from_state != NULL ? 1 : 0);
    if (message_fields != 1)
        return validation_error(ctx, json_path, "console.write requires exactly one of message or message_from_state");
    if (message != NULL && (!yyjson_is_str(message) || yyjson_get_str(message)[0] == '\0'))
        return validation_error(ctx, json_path, "console.write message must be a non-empty string");
    if (message_from_state != NULL &&
        (!yyjson_is_str(message_from_state) || yyjson_get_str(message_from_state)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "console.write message_from_state must be a non-empty string");
    }
    yyjson_val *line_key_prefix = obj_get(action, "line_key_prefix");
    if (line_key_prefix != NULL && (!yyjson_is_str(line_key_prefix) || yyjson_get_str(line_key_prefix)[0] == '\0'))
        return validation_error(ctx, json_path, "console.write line_key_prefix must be a non-empty string");
    yyjson_val *count_key = obj_get(action, "count_key");
    if (count_key != NULL && (!yyjson_is_str(count_key) || yyjson_get_str(count_key)[0] == '\0'))
        return validation_error(ctx, json_path, "console.write count_key must be a non-empty string");
    yyjson_val *line_count = obj_get(action, "line_count");
    if (line_count != NULL &&
        (!yyjson_is_int(line_count) || yyjson_get_int(line_count) < 1 || yyjson_get_int(line_count) > 8))
    {
        return validation_error(ctx, json_path, "console.write line_count must be an integer from 1 to 8");
    }
    return true;
}

static bool validate_network_direct_connect_start_action(validation_context *ctx, yyjson_val *action,
                                                         const char *json_path, validation_names *names,
                                                         const char *type)
{
    (void)names;
    (void)type;
    if (!is_non_empty_string(action, "name"))
        return validation_error(ctx, json_path, "network.direct_connect.start requires a non-empty name");
    if (!is_non_empty_string(action, "host_key") && !is_non_empty_string(action, "host") &&
        !is_non_empty_string(action, "default_host"))
        return validation_error(ctx, json_path,
                                "network.direct_connect.start requires host_key, host, or default_host");
    if (!is_non_empty_string(action, "port_key") && obj_get(action, "port") == NULL &&
        obj_get(action, "default_port") == NULL)
        return validation_error(ctx, json_path,
                                "network.direct_connect.start requires port_key, port, or default_port");
    if (!validate_network_port_value(ctx, obj_get(action, "port"), json_path, "network.direct_connect.start port"))
        return false;
    yyjson_val *default_port = obj_get(action, "default_port");
    if (default_port != NULL &&
        (!yyjson_is_int(default_port) || yyjson_get_int(default_port) <= 0 || yyjson_get_int(default_port) > 65535))
        return validation_error(ctx, json_path, "network.direct_connect.start default_port must be integer 1..65535");
    return true;
}

static bool validate_network_named_session_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                  validation_names *names, const char *type)
{
    (void)names;
    if (!is_non_empty_string(action, "name"))
        return validation_error(ctx, json_path, "%s requires a non-empty name", type);
    return true;
}

static bool validate_network_host_start_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                               validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    if (!is_non_empty_string(action, "name"))
        return validation_error(ctx, json_path, "network.host.start requires a non-empty name");
    if (!validate_network_port_value(ctx, obj_get(action, "port"), json_path, "network.host.start port"))
        return false;
    yyjson_val *default_port = obj_get(action, "default_port");
    if (default_port != NULL &&
        (!yyjson_is_int(default_port) || yyjson_get_int(default_port) <= 0 || yyjson_get_int(default_port) > 65535))
        return validation_error(ctx, json_path, "network.host.start default_port must be integer 1..65535");
    return true;
}

static bool validate_network_discovery_start_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type)
{
    (void)names;
    if (!is_non_empty_string(action, "name"))
        return validation_error(ctx, json_path, "%s requires a non-empty name", type);
    if (!is_non_empty_string(action, "collection"))
        return validation_error(ctx, json_path, "%s requires a non-empty collection", type);
    if (!validate_network_port_value(ctx, obj_get(action, "port"), json_path, "network.discovery port"))
        return false;
    yyjson_val *default_port = obj_get(action, "default_port");
    if (default_port != NULL &&
        (!yyjson_is_int(default_port) || yyjson_get_int(default_port) <= 0 || yyjson_get_int(default_port) > 65535))
        return validation_error(ctx, json_path, "network.discovery default_port must be integer 1..65535");
    yyjson_val *local_port = obj_get(action, "local_port");
    if (local_port != NULL &&
        (!yyjson_is_int(local_port) || yyjson_get_int(local_port) < 0 || yyjson_get_int(local_port) > 65535))
        return validation_error(ctx, json_path, "network.discovery local_port must be integer 0..65535");
    return true;
}

static bool validate_network_discovery_collection_action(validation_context *ctx, yyjson_val *action,
                                                         const char *json_path, validation_names *names,
                                                         const char *type)
{
    (void)names;
    if (!is_non_empty_string(action, "name"))
        return validation_error(ctx, json_path, "%s requires a non-empty name", type);
    if (!is_non_empty_string(action, "collection"))
        return validation_error(ctx, json_path, "%s requires a non-empty collection", type);
    return true;
}

static bool validate_network_discovery_connect_selected_action(validation_context *ctx, yyjson_val *action,
                                                               const char *json_path, validation_names *names,
                                                               const char *type)
{
    (void)names;
    (void)type;
    if (!is_non_empty_string(action, "name"))
        return validation_error(ctx, json_path, "network.discovery.connect_selected requires a non-empty name");
    if (!is_non_empty_string(action, "collection"))
        return validation_error(ctx, json_path, "network.discovery.connect_selected requires a non-empty collection");
    if (!is_non_empty_string(action, "selected_index_key") && obj_get(action, "selected_index") == NULL)
        return validation_error(ctx, json_path,
                                "network.discovery.connect_selected requires selected_index_key or selected_index");
    yyjson_val *selected_index = obj_get(action, "selected_index");
    if (selected_index != NULL && (!yyjson_is_int(selected_index) || yyjson_get_int(selected_index) < 0))
        return validation_error(ctx, json_path,
                                "network.discovery.connect_selected selected_index must be an integer >= 0");
    if (!is_non_empty_string(action, "direct_connect_name"))
        return validation_error(ctx, json_path,
                                "network.discovery.connect_selected requires a non-empty direct_connect_name");
    return true;
}

static bool validate_ui_animate_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type)
{
    (void)type;
    if (!is_non_empty_string(action, "target") && !is_non_empty_string(action, "ui"))
        return validation_error(ctx, json_path, "ui.animate requires a non-empty target");
    if (!is_non_empty_string(action, "property"))
        return validation_error(ctx, json_path, "ui.animate requires a non-empty property");
    if (!is_ui_tween_property(json_string(action, "property")))
        return validation_error(ctx, json_path,
                                "ui.animate property must be alpha, scale, offset_x, offset_y, x, y, tint, or color");
    return validate_animation_common(ctx, action, json_path, names);
}

static bool validate_combat_damage_or_heal_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                  validation_names *names, const char *type)
{
    return validate_combat_amount_action(ctx, action, json_path, names, type, "amount", "amount_from_payload");
}

static bool validate_combat_kill_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type)
{
    return validate_combat_target_action(ctx, action, json_path, names, type);
}

static bool validate_combat_revive_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type)
{
    if (!validate_combat_target_action(ctx, action, json_path, names, type))
        return false;
    yyjson_val *health = obj_get(action, "health");
    yyjson_val *health_from_payload_value = obj_get(action, "health_from_payload");
    const char *health_from_payload = json_string(action, "health_from_payload");
    if (health != NULL && health_from_payload != NULL)
        return validation_error(ctx, json_path, "combat.revive requires at most one of health or health_from_payload");
    if (health != NULL && (!yyjson_is_num(health) || yyjson_get_num(health) < 0.0))
        return validation_error(ctx, json_path, "combat.revive health must be a non-negative number");
    if (health_from_payload_value != NULL &&
        (!yyjson_is_str(health_from_payload_value) || yyjson_get_str(health_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, json_path, "combat.revive health_from_payload must be a non-empty string");
    }
    return true;
}

static bool validate_resource_add_or_consume_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type)
{
    return validate_resource_action(ctx, action, json_path, names, type, "amount", "amount_from_payload");
}

static bool validate_resource_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                         validation_names *names, const char *type)
{
    return validate_resource_action(ctx, action, json_path, names, type, "value", "value_from_payload");
}

static bool validate_pickup_collect_action_dispatch(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type)
{
    (void)type;
    return validate_pickup_collect_action(ctx, action, json_path, names);
}

static bool validate_resource_station_use_action_dispatch(validation_context *ctx, yyjson_val *action,
                                                          const char *json_path, validation_names *names,
                                                          const char *type)
{
    (void)type;
    return validate_resource_station_use_action(ctx, action, json_path, names);
}

static bool validate_status_effect_apply_action_dispatch(validation_context *ctx, yyjson_val *action,
                                                         const char *json_path, validation_names *names,
                                                         const char *type)
{
    (void)type;
    return validate_status_effect_apply_action(ctx, action, json_path, names);
}

static bool validate_weapon_reload_action_dispatch(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type)
{
    (void)type;
    return validate_weapon_reload_action(ctx, action, json_path, names);
}

static bool validate_weapon_hitscan_action_dispatch(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                    validation_names *names, const char *type)
{
    (void)type;
    return validate_weapon_hitscan_action(ctx, action, json_path, names);
}

static bool validate_interaction_use_action_dispatch(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                     validation_names *names, const char *type)
{
    (void)type;
    return validate_interaction_use_action(ctx, action, json_path, names);
}

static bool validate_effect_explosion_action_dispatch(validation_context *ctx, yyjson_val *action,
                                                      const char *json_path, validation_names *names, const char *type)
{
    (void)type;
    return validate_effect_explosion_action(ctx, action, json_path, names);
}

static bool validate_persistence_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                        validation_names *names, const char *type)
{
    (void)type;
    const char *entry = json_string(action, "entry");
    if (entry == NULL)
        entry = json_string(action, "name");
    return require_ref(ctx, &names->persistence, "persistence entry", entry, json_path);
}

static bool validate_entity_set_active_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                              validation_names *names, const char *type)
{
    (void)type;
    if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
        return false;
    if (!yyjson_is_bool(obj_get(action, "active")))
        return validation_error(ctx, json_path, "entity.set_active requires a boolean active value");
    return true;
}

static bool validate_transform_set_position_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                                   validation_names *names, const char *type)
{
    (void)type;
    if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
        return false;
    if (!is_vec_array(obj_get(action, "position"), 2))
        return validation_error(ctx, json_path, "transform.set_position requires a numeric position array");
    return true;
}

static bool validate_camera_toggle_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                          validation_names *names, const char *type)
{
    (void)type;
    return require_ref(ctx, &names->cameras, "camera", json_string(action, "camera"), json_path) &&
           require_ref(ctx, &names->cameras, "camera", json_string(action, "fallback"), json_path);
}

static bool validate_camera_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                       validation_names *names, const char *type)
{
    (void)type;
    return require_ref(ctx, &names->cameras, "camera", json_string(action, "camera"), json_path);
}

static bool validate_scene_set_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                      validation_names *names, const char *type)
{
    (void)type;
    if (!require_ref(ctx, &names->scenes, "scene", json_string(action, "scene"), json_path))
        return false;
    yyjson_val *payload = obj_get(action, "payload");
    if (payload == NULL)
        return true;
    if (!yyjson_is_obj(payload))
        return validation_error(ctx, json_path, "scene.set payload must be an object");

    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(payload, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        if (name == NULL || name[0] == '\0')
            return validation_error(ctx, json_path, "scene.set payload keys must be non-empty");
        if (!(yyjson_is_bool(value) || yyjson_is_num(value) || yyjson_is_str(value)))
            return validation_error(ctx, json_path, "scene.set payload values must be scalar");
    }
    return true;
}

static bool validate_adapter_invoke_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                           validation_names *names, const char *type)
{
    (void)type;
    const char *adapter = json_string(action, "adapter");
    if (!require_ref(ctx, &names->adapters, "adapter", adapter, json_path))
        return false;
    if (!note_name(&names->used_adapters, adapter, json_path))
        return validation_error(ctx, json_path, "failed to record adapter use");
    if (json_string(action, "target") != NULL &&
        !require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
    {
        return false;
    }
    return true;
}

static bool validate_branch_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                   validation_names *names, const char *type)
{
    (void)type;
    yyjson_val *condition = obj_get(action, "if");
    if (!yyjson_is_obj(condition))
        return validation_error(ctx, json_path, "branch requires an object 'if' condition");
    char condition_path[PATH_BUFFER_SIZE];
    format_path(condition_path, sizeof(condition_path), "%s.if", json_path);
    if (!validate_data_condition(ctx, condition, condition_path, names))
        return false;

    char then_path[PATH_BUFFER_SIZE];
    char else_path[PATH_BUFFER_SIZE];
    format_path(then_path, sizeof(then_path), "%s.then", json_path);
    format_path(else_path, sizeof(else_path), "%s.else", json_path);
    yyjson_val *then_actions = obj_get(action, "then");
    yyjson_val *else_actions = obj_get(action, "else");
    return (then_actions == NULL || validate_action_array(ctx, then_actions, then_path, names)) &&
           (else_actions == NULL || validate_action_array(ctx, else_actions, else_path, names));
}

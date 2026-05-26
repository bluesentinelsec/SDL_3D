/**
 * @file game_data_validation_actions.c
 * @brief Data action validation.
 */

#include "game_data_validation_internal.h"

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

static bool editor_command_name_valid(const char *value)
{
    return value != NULL &&
           (SDL_strcmp(value, "translate") == 0 || SDL_strcmp(value, "paint") == 0 ||
            SDL_strcmp(value, "resize") == 0 || SDL_strcmp(value, "extrude") == 0 || SDL_strcmp(value, "delete") == 0);
}

static bool editor_command_target_name_valid(const char *value)
{
    return value != NULL &&
           (SDL_strcmp(value, "selection") == 0 || SDL_strcmp(value, "world") == 0 ||
            SDL_strcmp(value, "element") == 0 || SDL_strcmp(value, "face") == 0 || SDL_strcmp(value, "material") == 0);
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
        const char *ambient_id_from_payload = json_string(action, "ambient_id_from_payload");
        if ((ambient_id == NULL) == (ambient_id_from_payload == NULL))
            return validation_error(ctx, json_path,
                                    "audio.set_ambient requires exactly one of ambient_id or ambient_id_from_payload");
        if (ambient_id != NULL && (!yyjson_is_int(ambient_id) || yyjson_get_int(ambient_id) < 0))
            return validation_error(ctx, json_path, "audio.set_ambient ambient_id must be non-negative");
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
    const char *amount_from_payload = json_string(action, payload_field);
    if ((amount == NULL && amount_from_payload == NULL) || (amount != NULL && amount_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of %s or %s", type, field, payload_field);
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
    if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
        return validation_error(ctx, json_path, "%s requires exactly one of %s or %s", type, target_key, payload_key);
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

bool validate_non_empty_string_field(validation_context *ctx, yyjson_val *json, const char *json_path, const char *type,
                                     const char *field)
{
    yyjson_val *value = obj_get(json, field);
    if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
        return validation_error(ctx, json_path, "%s %s must be a non-empty string", type, field);
    return true;
}

bool validate_optional_signal_field(validation_context *ctx, yyjson_val *json, const char *json_path,
                                    validation_names *names, const char *field)
{
    const char *signal = json_string(json, field);
    return signal == NULL || require_ref(ctx, &names->signals, "signal", signal, json_path);
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

static bool validate_known_action(validation_context *ctx, yyjson_val *action, const char *json_path,
                                  validation_names *names, const char *type)
{
    if (SDL_strcmp(type, "signal.emit") == 0)
        return require_ref(ctx, &names->signals, "signal", json_string(action, "signal"), json_path);
    if (SDL_strcmp(type, "timer.start") == 0)
        return require_ref(ctx, &names->timers, "timer", json_string(action, "timer"), json_path);
    if (SDL_strcmp(type, "property.set") == 0 || SDL_strcmp(type, "property.add") == 0)
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
    if (SDL_strcmp(type, "property.snapshot") == 0 || SDL_strcmp(type, "property.restore_snapshot") == 0)
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
    if (SDL_strcmp(type, "property.animate") == 0)
    {
        if (!validate_actor_target_action(ctx, action, json_path, names, "property.animate", "target",
                                          "target_from_payload"))
            return false;
        if (!is_non_empty_string(action, "key"))
            return validation_error(ctx, json_path, "property.animate requires a non-empty key");
        return validate_animation_common(ctx, action, json_path, names);
    }
    if (SDL_strcmp(type, "property.reset_defaults") == 0)
    {
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
    if (SDL_strcmp(type, "debug.write_actor_properties") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        if (!is_non_empty_string(action, "path"))
            return validation_error(ctx, json_path, "debug.write_actor_properties requires a non-empty path");
        yyjson_val *append = obj_get(action, "append");
        if (append != NULL && !yyjson_is_bool(append))
            return validation_error(ctx, json_path, "debug.write_actor_properties append must be a boolean");
        yyjson_val *properties = obj_get(action, "properties");
        if (!yyjson_is_arr(properties) || yyjson_arr_size(properties) == 0)
            return validation_error(ctx, json_path,
                                    "debug.write_actor_properties requires a non-empty properties array");
        for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
        {
            yyjson_val *property = yyjson_arr_get(properties, i);
            if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
                return validation_error(ctx, json_path,
                                        "debug.write_actor_properties properties must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "actor.spawn") == 0)
    {
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
                return validation_error(ctx, json_path,
                                        "actor.spawn position_from_actor_properties offset must be a vec3");
            const char *additive_fields[] = {"x_add", "y_add", "z_add"};
            for (size_t additive_index = 0; additive_index < SDL_arraysize(additive_fields); ++additive_index)
            {
                yyjson_val *additive = obj_get(position_from_actor_properties, additive_fields[additive_index]);
                if (additive == NULL)
                    continue;
                if (!yyjson_is_arr(additive))
                    return validation_error(
                        ctx, json_path, "actor.spawn position_from_actor_properties additive fields must be arrays");
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
                return validation_error(ctx, json_path,
                                        "actor.spawn payload_directional_offset distance must be numeric");
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
                return validation_error(ctx, json_path,
                                        "actor.spawn properties_from_actor keys must be a non-empty array");
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
    if (SDL_strcmp(type, "actor.despawn") == 0)
    {
        yyjson_val *target_value = obj_get(action, "target");
        yyjson_val *target_from_payload_value = obj_get(action, "target_from_payload");
        const char *target = json_string(action, "target");
        const char *target_from_payload = json_string(action, "target_from_payload");
        if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
            return validation_error(ctx, json_path,
                                    "actor.despawn requires exactly one of target or target_from_payload");
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
    if (SDL_strcmp(type, "actor.despawn_by_tag") == 0)
    {
        if (!is_non_empty_string(action, "tag"))
            return validation_error(ctx, json_path, "actor.despawn_by_tag requires a non-empty tag");
        yyjson_val *reason = obj_get(action, "reason");
        if (reason != NULL && !yyjson_is_str(reason))
            return validation_error(ctx, json_path, "actor.despawn_by_tag reason must be a string");
        return true;
    }
    if (SDL_strcmp(type, "combat.damage") == 0)
        return validate_combat_amount_action(ctx, action, json_path, names, type, "amount", "amount_from_payload");
    if (SDL_strcmp(type, "combat.heal") == 0)
        return validate_combat_amount_action(ctx, action, json_path, names, type, "amount", "amount_from_payload");
    if (SDL_strcmp(type, "combat.kill") == 0)
        return validate_combat_target_action(ctx, action, json_path, names, type);
    if (SDL_strcmp(type, "combat.revive") == 0)
    {
        if (!validate_combat_target_action(ctx, action, json_path, names, type))
            return false;
        yyjson_val *health = obj_get(action, "health");
        yyjson_val *health_from_payload_value = obj_get(action, "health_from_payload");
        const char *health_from_payload = json_string(action, "health_from_payload");
        if (health != NULL && health_from_payload != NULL)
            return validation_error(ctx, json_path,
                                    "combat.revive requires at most one of health or health_from_payload");
        if (health != NULL && (!yyjson_is_num(health) || yyjson_get_num(health) < 0.0))
            return validation_error(ctx, json_path, "combat.revive health must be a non-negative number");
        if (health_from_payload_value != NULL &&
            (!yyjson_is_str(health_from_payload_value) || yyjson_get_str(health_from_payload_value)[0] == '\0'))
        {
            return validation_error(ctx, json_path, "combat.revive health_from_payload must be a non-empty string");
        }
        return true;
    }
    if (SDL_strcmp(type, "resource.add") == 0)
        return validate_resource_action(ctx, action, json_path, names, type, "amount", "amount_from_payload");
    if (SDL_strcmp(type, "resource.consume") == 0)
        return validate_resource_action(ctx, action, json_path, names, type, "amount", "amount_from_payload");
    if (SDL_strcmp(type, "resource.set") == 0)
        return validate_resource_action(ctx, action, json_path, names, type, "value", "value_from_payload");
    if (SDL_strcmp(type, "pickup.collect") == 0)
        return validate_pickup_collect_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "resource.station.use") == 0)
        return validate_resource_station_use_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "status_effect.apply") == 0)
        return validate_status_effect_apply_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "weapon.reload") == 0)
        return validate_weapon_reload_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "weapon.hitscan") == 0)
        return validate_weapon_hitscan_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "interaction.use") == 0)
        return validate_interaction_use_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "effect.explosion") == 0)
        return validate_effect_explosion_action(ctx, action, json_path, names);
    if (SDL_strcmp(type, "noise.emit") == 0)
    {
        const char *source = json_string(action, "source");
        const char *actor = json_string(action, "actor");
        const char *target = json_string(action, "target");
        if (source != NULL && !require_actor_ref(ctx, names, source, json_path))
            return false;
        if (actor != NULL && !require_actor_ref(ctx, names, actor, json_path))
            return false;
        if (target != NULL && !require_actor_ref(ctx, names, target, json_path))
            return false;

        const char *payload_fields[] = {"source_from_payload", "actor_from_payload", "target_from_payload",
                                        "from_payload"};
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
    if (SDL_strcmp(type, "sector_door.open") == 0 || SDL_strcmp(type, "sector_door.close") == 0 ||
        SDL_strcmp(type, "sector_door.toggle") == 0)
    {
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
    if (SDL_strcmp(type, "sector_door.interact") == 0)
    {
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
    if (SDL_strcmp(type, "sector_lighting.set") == 0)
    {
        const char *sector_level = json_string(action, "sector_level");
        const char *sector = json_string(action, "sector");
        yyjson_val *sector_index = obj_get(action, "sector_index");
        if (!require_ref(ctx, &names->sector_levels, "sector level", sector_level, json_path))
            return false;
        if ((sector == NULL && sector_index == NULL) || (sector != NULL && sector_index != NULL))
            return validation_error(ctx, json_path,
                                    "sector_lighting.set requires exactly one of sector or sector_index");
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
    if (SDL_strcmp(type, "projectile.fire") == 0)
    {
        return validate_projectile_fire_shape(ctx, action, json_path, names, true);
    }
    if (SDL_strcmp(type, "controller.fps.launch") == 0 || SDL_strcmp(type, "controller.fps.teleport") == 0 ||
        SDL_strcmp(type, "controller.fps.push") == 0 || SDL_strcmp(type, "controller.fps_sector.launch") == 0 ||
        SDL_strcmp(type, "controller.fps_sector.teleport") == 0)
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
    if (SDL_strcmp(type, "grid.spawn_from_glyphs") == 0 || SDL_strcmp(type, "grid.spawn_runs_from_glyphs") == 0)
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
        const char *axis = json_string(action, "axis");
        if (axis != NULL && SDL_strcmp(axis, "x") != 0 && SDL_strcmp(axis, "horizontal") != 0 &&
            SDL_strcmp(axis, "row") != 0 && SDL_strcmp(axis, "y") != 0 && SDL_strcmp(axis, "vertical") != 0 &&
            SDL_strcmp(axis, "column") != 0)
        {
            return validation_error(ctx, json_path, "%s axis must be x, y, horizontal, vertical, row, or column", type);
        }
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
            const char *spawn_axis = json_string(spawn, "axis");
            if (spawn_axis != NULL && SDL_strcmp(spawn_axis, "x") != 0 && SDL_strcmp(spawn_axis, "horizontal") != 0 &&
                SDL_strcmp(spawn_axis, "row") != 0 && SDL_strcmp(spawn_axis, "y") != 0 &&
                SDL_strcmp(spawn_axis, "vertical") != 0 && SDL_strcmp(spawn_axis, "column") != 0)
            {
                return validation_error(ctx, spawn_path,
                                        "grid spawn axis must be x, y, horizontal, vertical, row, or column");
            }
        }
        return true;
    }
    if (SDL_strcmp(type, "grid.pickup_layer.reset") == 0)
    {
        if (!require_ref(ctx, &names->grid_pickup_layers, "grid pickup layer", json_string(action, "layer"), json_path))
            return false;
        yyjson_val *output_count_key = obj_get(action, "output_count_key");
        if (output_count_key != NULL && !is_non_empty_string(action, "output_count_key"))
            return validation_error(ctx, json_path, "grid.pickup_layer.reset output_count_key must be non-empty");
        return true;
    }
    if (SDL_strcmp(type, "input.reset_bindings") == 0)
    {
        if (!is_non_empty_string(action, "menu"))
            return validation_error(ctx, json_path, "input.reset_bindings requires a non-empty menu");
        return true;
    }
    if (SDL_strcmp(type, "input.apply_profile") == 0)
    {
        const char *profile = json_string(action, "profile");
        return require_ref(ctx, &names->input_profiles, "input profile", profile, json_path);
    }
    if (SDL_strcmp(type, "input.apply_active_profile") == 0)
    {
        if (names->input_profiles.count <= 0)
            return validation_error(ctx, json_path, "input.apply_active_profile requires at least one input profile");
        return true;
    }
    if (SDL_strcmp(type, "input.clear_network_input_overrides") == 0)
    {
        return require_ref(ctx, &names->network_input_channels, "network input channel", json_string(action, "channel"),
                           json_path);
    }
    if (SDL_strcmp(type, "scene_state.set") == 0)
    {
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
    if (SDL_strcmp(type, "scene_state.toggle") == 0)
    {
        if (!is_non_empty_string(action, "key"))
            return validation_error(ctx, json_path, "scene_state.toggle requires a non-empty key");
        yyjson_val *default_value = obj_get(action, "default");
        if (default_value != NULL && !yyjson_is_bool(default_value))
            return validation_error(ctx, json_path, "scene_state.toggle default must be a boolean");
        return true;
    }
    if (SDL_strcmp(type, "scene_state.cycle") == 0)
    {
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
    if (SDL_strcmp(type, "console.write") == 0)
    {
        yyjson_val *message = obj_get(action, "message");
        yyjson_val *message_from_state = obj_get(action, "message_from_state");
        const int message_fields = (message != NULL ? 1 : 0) + (message_from_state != NULL ? 1 : 0);
        if (message_fields != 1)
            return validation_error(ctx, json_path,
                                    "console.write requires exactly one of message or message_from_state");
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
    if (SDL_strcmp(type, "editor.selection.clear") == 0)
        return true;
    if (SDL_strcmp(type, "editor.vertex.selection.clear") == 0)
        return true;
    if (SDL_strcmp(type, "editor.vertex.delete_selected") == 0)
    {
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.vertex.delete_selected outputs must be an object");
        if (outputs != NULL)
        {
            const char *output_fields[] = {"valid_key", "message_key", "deleted_count_key", "source_count_key"};
            for (int i = 0; i < (int)SDL_arraysize(output_fields); ++i)
            {
                yyjson_val *field = obj_get(outputs, output_fields[i]);
                if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
                    return validation_error(ctx, json_path,
                                            "editor.vertex.delete_selected output keys must be non-empty strings");
            }
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.vertex.merge_selected_to_hover") == 0)
    {
        yyjson_val *target_vertex_index = obj_get(action, "target_vertex_index");
        if (target_vertex_index != NULL &&
            (!yyjson_is_int(target_vertex_index) || yyjson_get_int(target_vertex_index) < 0))
            return validation_error(ctx, json_path,
                                    "editor.vertex.merge_selected_to_hover target_vertex_index must be non-negative");
        yyjson_val *world = obj_get(action, "world");
        if (world != NULL && (!yyjson_is_str(world) || yyjson_get_str(world)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.vertex.merge_selected_to_hover world must be non-empty");
        yyjson_val *brush = obj_get(action, "brush");
        if (brush != NULL && (!yyjson_is_str(brush) || yyjson_get_str(brush)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.vertex.merge_selected_to_hover brush must be non-empty");
        yyjson_val *brush_stable_id = obj_get(action, "brush_stable_id");
        if (brush_stable_id != NULL && (!yyjson_is_str(brush_stable_id) || yyjson_get_str(brush_stable_id)[0] == '\0'))
            return validation_error(ctx, json_path,
                                    "editor.vertex.merge_selected_to_hover brush_stable_id must be non-empty");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.vertex.merge_selected_to_hover outputs must be an object");
        if (outputs != NULL)
        {
            const char *output_fields[] = {"valid_key", "message_key", "merged_count_key"};
            for (int i = 0; i < (int)SDL_arraysize(output_fields); ++i)
            {
                yyjson_val *field = obj_get(outputs, output_fields[i]);
                if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
                    return validation_error(
                        ctx, json_path, "editor.vertex.merge_selected_to_hover output keys must be non-empty strings");
            }
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.vertex.add_to_source") == 0)
    {
        yyjson_val *coord = obj_get(action, "coord");
        yyjson_val *position = obj_get(action, "position");
        if ((coord == NULL) == (position == NULL))
            return validation_error(ctx, json_path,
                                    "editor.vertex.add_to_source requires exactly one of coord or position");
        if (coord != NULL)
        {
            if (!yyjson_is_arr(coord) || yyjson_arr_size(coord) != 3)
                return validation_error(ctx, json_path, "editor.vertex.add_to_source coord must be an int[3]");
            for (size_t i = 0; i < 3; ++i)
            {
                if (!yyjson_is_int(yyjson_arr_get(coord, i)))
                    return validation_error(ctx, json_path, "editor.vertex.add_to_source coord must be an int[3]");
            }
        }
        if (position != NULL)
        {
            if (!is_vec_array(position, 3))
                return validation_error(ctx, json_path, "editor.vertex.add_to_source position must be a vec3");
        }
        yyjson_val *world = obj_get(action, "world");
        if (world != NULL && (!yyjson_is_str(world) || yyjson_get_str(world)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.vertex.add_to_source world must be non-empty");
        yyjson_val *brush = obj_get(action, "brush");
        if (brush != NULL && (!yyjson_is_str(brush) || yyjson_get_str(brush)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.vertex.add_to_source brush must be non-empty");
        yyjson_val *brush_stable_id = obj_get(action, "brush_stable_id");
        if (brush_stable_id != NULL && (!yyjson_is_str(brush_stable_id) || yyjson_get_str(brush_stable_id)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.vertex.add_to_source brush_stable_id must be non-empty");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.vertex.add_to_source outputs must be an object");
        if (outputs != NULL)
        {
            const char *output_fields[] = {"valid_key", "message_key", "vertex_count_key", "added_count_key"};
            for (int i = 0; i < (int)SDL_arraysize(output_fields); ++i)
            {
                yyjson_val *field = obj_get(outputs, output_fields[i]);
                if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
                    return validation_error(ctx, json_path,
                                            "editor.vertex.add_to_source output keys must be non-empty strings");
            }
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.vertex.validate_source") == 0)
    {
        yyjson_val *world = obj_get(action, "world");
        if (world != NULL && (!yyjson_is_str(world) || yyjson_get_str(world)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.vertex.validate_source world must be non-empty");
        yyjson_val *brush = obj_get(action, "brush");
        if (brush != NULL && (!yyjson_is_str(brush) || yyjson_get_str(brush)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.vertex.validate_source brush must be non-empty");
        yyjson_val *brush_stable_id = obj_get(action, "brush_stable_id");
        if (brush_stable_id != NULL && (!yyjson_is_str(brush_stable_id) || yyjson_get_str(brush_stable_id)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.vertex.validate_source brush_stable_id must be non-empty");
        if (brush != NULL && brush_stable_id != NULL)
            return validation_error(ctx, json_path, "editor.vertex.validate_source accepts only one brush identity");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.vertex.validate_source outputs must be an object");
        if (outputs != NULL)
        {
            const char *output_fields[] = {"valid_key",          "message_key",
                                           "world_key",          "brush_count_key",
                                           "vertex_count_key",   "edge_count_key",
                                           "face_count_key",     "shared_vertex_count_key",
                                           "off_snap_count_key", "degenerate_count_key",
                                           "concave_count_key",  "non_finite_count_key",
                                           "first_issue_key",    "first_issue_stable_id_key"};
            for (int i = 0; i < (int)SDL_arraysize(output_fields); ++i)
            {
                yyjson_val *field = obj_get(outputs, output_fields[i]);
                if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
                    return validation_error(ctx, json_path,
                                            "editor.vertex.validate_source output keys must be non-empty strings");
            }
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.vertex.snap_selected") == 0)
    {
        yyjson_val *snap_units = obj_get(action, "snap_units");
        if (snap_units != NULL && (!yyjson_is_int(snap_units) || yyjson_get_int(snap_units) <= 0))
            return validation_error(ctx, json_path, "editor.vertex.snap_selected snap_units must be positive");
        yyjson_val *grid = obj_get(action, "grid");
        if (grid != NULL && (!yyjson_is_num(grid) || yyjson_get_num(grid) <= 0.0))
            return validation_error(ctx, json_path, "editor.vertex.snap_selected grid must be positive");
        yyjson_val *default_grid = obj_get(action, "default_grid");
        if (default_grid != NULL && (!yyjson_is_num(default_grid) || yyjson_get_num(default_grid) <= 0.0))
            return validation_error(ctx, json_path, "editor.vertex.snap_selected default_grid must be positive");
        yyjson_val *grid_key = obj_get(action, "grid_key");
        if (grid_key != NULL && (!yyjson_is_str(grid_key) || yyjson_get_str(grid_key)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.vertex.snap_selected grid_key must be non-empty");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.vertex.snap_selected outputs must be an object");
        if (outputs != NULL)
        {
            const char *output_fields[] = {"valid_key", "message_key", "changed_count_key", "source_count_key",
                                           "snap_units_key"};
            for (int i = 0; i < (int)SDL_arraysize(output_fields); ++i)
            {
                yyjson_val *field = obj_get(outputs, output_fields[i]);
                if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
                    return validation_error(ctx, json_path,
                                            "editor.vertex.snap_selected output keys must be non-empty strings");
            }
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.selection.select_brush") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *element = obj_get(action, "element");
        yyjson_val *element_from_state = obj_get(action, "element_from_state");
        yyjson_val *element_stable_id = obj_get(action, "element_stable_id");
        yyjson_val *element_stable_id_from_state = obj_get(action, "element_stable_id_from_state");
        const int element_identity_count = (element != NULL ? 1 : 0) + (element_from_state != NULL ? 1 : 0) +
                                           (element_stable_id != NULL ? 1 : 0) +
                                           (element_stable_id_from_state != NULL ? 1 : 0);
        if (element_identity_count != 1)
            return validation_error(ctx, json_path,
                                    "editor.selection.select_brush requires exactly one brush identity field");
        if (element != NULL && (!yyjson_is_str(element) || yyjson_get_str(element)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.selection.select_brush element must be non-empty");
        if (element_from_state != NULL &&
            (!yyjson_is_str(element_from_state) || yyjson_get_str(element_from_state)[0] == '\0'))
        {
            return validation_error(ctx, json_path,
                                    "editor.selection.select_brush element_from_state must be non-empty");
        }
        if (element_stable_id != NULL &&
            (!yyjson_is_str(element_stable_id) || yyjson_get_str(element_stable_id)[0] == '\0'))
            return validation_error(ctx, json_path,
                                    "editor.selection.select_brush element_stable_id must be non-empty");
        if (element_stable_id_from_state != NULL &&
            (!yyjson_is_str(element_stable_id_from_state) || yyjson_get_str(element_stable_id_from_state)[0] == '\0'))
        {
            return validation_error(ctx, json_path,
                                    "editor.selection.select_brush element_stable_id_from_state must be non-empty");
        }

        yyjson_val *face = obj_get(action, "face");
        yyjson_val *face_from_state = obj_get(action, "face_from_state");
        yyjson_val *face_stable_id = obj_get(action, "face_stable_id");
        yyjson_val *face_stable_id_from_state = obj_get(action, "face_stable_id_from_state");
        const int face_identity_count = (face != NULL ? 1 : 0) + (face_from_state != NULL ? 1 : 0) +
                                        (face_stable_id != NULL ? 1 : 0) + (face_stable_id_from_state != NULL ? 1 : 0);
        if (face_identity_count > 1)
            return validation_error(ctx, json_path,
                                    "editor.selection.select_brush accepts at most one face identity field");
        if (face != NULL && (!yyjson_is_str(face) || yyjson_get_str(face)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.selection.select_brush face must be non-empty");
        if (face_from_state != NULL && (!yyjson_is_str(face_from_state) || yyjson_get_str(face_from_state)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.selection.select_brush face_from_state must be non-empty");
        if (face_stable_id != NULL && (!yyjson_is_str(face_stable_id) || yyjson_get_str(face_stable_id)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.selection.select_brush face_stable_id must be non-empty");
        if (face_stable_id_from_state != NULL &&
            (!yyjson_is_str(face_stable_id_from_state) || yyjson_get_str(face_stable_id_from_state)[0] == '\0'))
        {
            return validation_error(ctx, json_path,
                                    "editor.selection.select_brush face_stable_id_from_state must be non-empty");
        }
        yyjson_val *message = obj_get(action, "message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "editor.selection.select_brush message must be a string");
        yyjson_val *invalid_message = obj_get(action, "invalid_message");
        if (invalid_message != NULL && !yyjson_is_str(invalid_message))
            return validation_error(ctx, json_path, "editor.selection.select_brush invalid_message must be a string");
        yyjson_val *additive = obj_get(action, "additive");
        if (additive != NULL && !yyjson_is_bool(additive))
            return validation_error(ctx, json_path, "editor.selection.select_brush additive must be a boolean");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.selection.select_brush outputs must be an object");
        static const char *const output_keys[] = {"valid_key", "message_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.selection.select_brush output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.selection.delete_selected") == 0)
    {
        char actions_path[PATH_BUFFER_SIZE];
        char else_path[PATH_BUFFER_SIZE];
        format_path(actions_path, sizeof(actions_path), "%s.actions", json_path);
        format_path(else_path, sizeof(else_path), "%s.else", json_path);
        yyjson_val *actions = obj_get(action, "actions");
        yyjson_val *else_actions = obj_get(action, "else");
        return (actions == NULL || validate_action_array(ctx, actions, actions_path, names)) &&
               (else_actions == NULL || validate_action_array(ctx, else_actions, else_path, names));
    }
    if (SDL_strcmp(type, "editor.selection.resize_y") == 0)
    {
        yyjson_val *direction = obj_get(action, "direction");
        if (direction != NULL && (!yyjson_is_int(direction) || yyjson_get_int(direction) == 0))
            return validation_error(ctx, json_path, "editor.selection.resize_y direction must be a non-zero integer");
        yyjson_val *distance = obj_get(action, "distance");
        if (distance != NULL && (!yyjson_is_num(distance) || yyjson_get_num(distance) <= 0.0))
            return validation_error(ctx, json_path, "editor.selection.resize_y distance must be positive");
        yyjson_val *default_distance = obj_get(action, "default_distance");
        if (default_distance != NULL && (!yyjson_is_num(default_distance) || yyjson_get_num(default_distance) <= 0.0))
            return validation_error(ctx, json_path, "editor.selection.resize_y default_distance must be positive");
        yyjson_val *distance_key = obj_get(action, "distance_key");
        if (distance_key != NULL && (!yyjson_is_str(distance_key) || yyjson_get_str(distance_key)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.selection.resize_y distance_key must be non-empty");
        yyjson_val *grid_key = obj_get(action, "grid_key");
        if (grid_key != NULL && (!yyjson_is_str(grid_key) || yyjson_get_str(grid_key)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.selection.resize_y grid_key must be non-empty");
        yyjson_val *min_height = obj_get(action, "min_height");
        if (min_height != NULL && (!yyjson_is_num(min_height) || yyjson_get_num(min_height) <= 0.0))
            return validation_error(ctx, json_path, "editor.selection.resize_y min_height must be positive");
        yyjson_val *min_elevation = obj_get(action, "min_elevation");
        if (min_elevation != NULL && !yyjson_is_num(min_elevation))
            return validation_error(ctx, json_path, "editor.selection.resize_y min_elevation must be numeric");
        yyjson_val *max_elevation = obj_get(action, "max_elevation");
        if (max_elevation != NULL && !yyjson_is_num(max_elevation))
            return validation_error(ctx, json_path, "editor.selection.resize_y max_elevation must be numeric");
        if (min_elevation != NULL && max_elevation != NULL &&
            yyjson_get_num(min_elevation) >= yyjson_get_num(max_elevation))
        {
            return validation_error(ctx, json_path,
                                    "editor.selection.resize_y min_elevation must be less than max_elevation");
        }
        yyjson_val *slab_max_height = obj_get(action, "slab_max_height");
        if (slab_max_height != NULL && (!yyjson_is_num(slab_max_height) || yyjson_get_num(slab_max_height) <= 0.0))
            return validation_error(ctx, json_path, "editor.selection.resize_y slab_max_height must be positive");
        yyjson_val *fill_thickness = obj_get(action, "fill_thickness");
        if (fill_thickness != NULL && (!yyjson_is_num(fill_thickness) || yyjson_get_num(fill_thickness) <= 0.0))
            return validation_error(ctx, json_path, "editor.selection.resize_y fill_thickness must be positive");
        yyjson_val *floor_material = obj_get(action, "floor_material");
        if (floor_material != NULL && (!yyjson_is_str(floor_material) || yyjson_get_str(floor_material)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.selection.resize_y floor_material must be non-empty");
        yyjson_val *fill_material = obj_get(action, "fill_material");
        if (fill_material != NULL && (!yyjson_is_str(fill_material) || yyjson_get_str(fill_material)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.selection.resize_y fill_material must be non-empty");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.selection.resize_y outputs must be an object");
        char actions_path[PATH_BUFFER_SIZE];
        char else_path[PATH_BUFFER_SIZE];
        format_path(actions_path, sizeof(actions_path), "%s.actions", json_path);
        format_path(else_path, sizeof(else_path), "%s.else", json_path);
        yyjson_val *actions = obj_get(action, "actions");
        yyjson_val *else_actions = obj_get(action, "else");
        return (actions == NULL || validate_action_array(ctx, actions, actions_path, names)) &&
               (else_actions == NULL || validate_action_array(ctx, else_actions, else_path, names));
    }
    if (SDL_strcmp(type, "editor.selection.run") == 0)
    {
        char actions_path[PATH_BUFFER_SIZE];
        char else_path[PATH_BUFFER_SIZE];
        format_path(actions_path, sizeof(actions_path), "%s.actions", json_path);
        format_path(else_path, sizeof(else_path), "%s.else", json_path);
        if (!validate_action_array(ctx, obj_get(action, "actions"), actions_path, names))
            return false;
        yyjson_val *else_actions = obj_get(action, "else");
        return else_actions == NULL || validate_action_array(ctx, else_actions, else_path, names);
    }
    if (SDL_strcmp(type, "editor.command.preview") == 0)
    {
        const char *command = json_string(action, "command");
        const char *target = json_string(action, "target");
        if (target == NULL)
            target = "selection";
        if (!editor_command_name_valid(command))
            return validation_error(
                ctx, json_path, "editor.command.preview command must be translate, paint, resize, extrude, or delete");
        if (!editor_command_target_name_valid(target))
            return validation_error(
                ctx, json_path, "editor.command.preview target must be selection, world, element, face, or material");
        if ((SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0) &&
            SDL_strcmp(target, "face") != 0)
        {
            return validation_error(ctx, json_path, "editor.command.preview resize/extrude target must be face");
        }
        yyjson_val *material = obj_get(action, "material");
        if (SDL_strcmp(command, "paint") == 0 && (!yyjson_is_str(material) || yyjson_get_str(material)[0] == '\0'))
        {
            return validation_error(ctx, json_path, "editor.command.preview paint requires a non-empty material");
        }
        if (material != NULL && !yyjson_is_str(material))
            return validation_error(ctx, json_path, "editor.command.preview material must be a string");
        yyjson_val *offset = obj_get(action, "offset");
        if (offset != NULL && !is_vec_array(offset, 3))
            return validation_error(ctx, json_path, "editor.command.preview offset must be a vec3");
        yyjson_val *distance = obj_get(action, "distance");
        if (distance != NULL && !yyjson_is_num(distance))
            return validation_error(ctx, json_path, "editor.command.preview distance must be numeric");
        yyjson_val *message = obj_get(action, "message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "editor.command.preview message must be a string");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.command.preview outputs must be an object");
        static const char *const output_keys[] = {"active_key",     "valid_key",     "command_key", "target_key",
                                                  "message_key",    "world_key",     "element_key", "face_index_key",
                                                  "bounds_min_key", "bounds_max_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path, "editor.command.preview output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.command.clear_preview") == 0)
    {
        yyjson_val *message = obj_get(action, "message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "editor.command.clear_preview message must be a string");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.command.clear_preview outputs must be an object");
        static const char *const output_keys[] = {"active_key",     "valid_key",     "command_key", "target_key",
                                                  "message_key",    "world_key",     "element_key", "face_index_key",
                                                  "bounds_min_key", "bounds_max_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.command.clear_preview output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.command.commit") == 0 || SDL_strcmp(type, "editor.command.undo") == 0 ||
        SDL_strcmp(type, "editor.command.redo") == 0)
    {
        yyjson_val *message = obj_get(action, "message");
        yyjson_val *invalid_message = obj_get(action, "invalid_message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "%s message must be a string", type);
        if (invalid_message != NULL && !yyjson_is_str(invalid_message))
            return validation_error(ctx, json_path, "%s invalid_message must be a string", type);
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "%s outputs must be an object", type);
        static const char *const output_keys[] = {
            "valid_key",      "event_key",         "message_key",    "transaction_id_key", "undo_count_key",
            "redo_count_key", "command_key",       "target_key",     "world_key",          "element_key",
            "face_index_key", "bounds_min_key",    "bounds_max_key", "source_path_key",    "dirty_key",
            "revision_key",   "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path, "%s output keys must be non-empty strings", type);
        }
        char actions_path[PATH_BUFFER_SIZE];
        char else_path[PATH_BUFFER_SIZE];
        format_path(actions_path, sizeof(actions_path), "%s.actions", json_path);
        format_path(else_path, sizeof(else_path), "%s.else", json_path);
        yyjson_val *actions = obj_get(action, "actions");
        yyjson_val *else_actions = obj_get(action, "else");
        if (actions != NULL && !validate_action_array(ctx, actions, actions_path, names))
            return false;
        return else_actions == NULL || validate_action_array(ctx, else_actions, else_path, names);
    }
    if (SDL_strcmp(type, "editor.brush_world.export") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.brush_world.export outputs must be an object");
        static const char *const output_keys[] = {"valid_key", "message_key",  "json_key",
                                                  "size_key",  "world_key",    "source_path_key",
                                                  "dirty_key", "revision_key", "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.brush_world.export output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.level.export") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.level.export outputs must be an object");
        static const char *const output_keys[] = {
            "valid_key",
            "message_key",
            "json_key",
            "size_key",
            "brush_world_key",
            "brush_source_path_key",
            "brush_dirty_key",
            "brush_revision_key",
            "brush_saved_revision_key",
            "player_start_source_path_key",
            "player_start_count_key",
            "player_start_dirty_key",
            "player_start_revision_key",
            "player_start_saved_revision_key",
        };
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path, "editor.level.export output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.level.save") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *path = obj_get(action, "path");
        yyjson_val *path_from_state = obj_get(action, "path_from_state");
        if ((path == NULL && path_from_state == NULL) || (path != NULL && path_from_state != NULL))
            return validation_error(ctx, json_path,
                                    "editor.level.save requires exactly one of path or path_from_state");
        if (path != NULL && (!yyjson_is_str(path) || yyjson_get_str(path)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.level.save path must be a non-empty string");
        if (path_from_state != NULL && (!yyjson_is_str(path_from_state) || yyjson_get_str(path_from_state)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.level.save path_from_state must be a non-empty string");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.level.save outputs must be an object");
        static const char *const output_keys[] = {
            "valid_key",
            "message_key",
            "path_key",
            "size_key",
            "brush_world_key",
            "brush_source_path_key",
            "brush_dirty_key",
            "brush_revision_key",
            "brush_saved_revision_key",
            "player_start_source_path_key",
            "player_start_count_key",
            "player_start_dirty_key",
            "player_start_revision_key",
            "player_start_saved_revision_key",
        };
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path, "editor.level.save output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.level.load") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *path = obj_get(action, "path");
        yyjson_val *path_from_state = obj_get(action, "path_from_state");
        if ((path == NULL && path_from_state == NULL) || (path != NULL && path_from_state != NULL))
            return validation_error(ctx, json_path,
                                    "editor.level.load requires exactly one of path or path_from_state");
        if (path != NULL && (!yyjson_is_str(path) || yyjson_get_str(path)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.level.load path must be a non-empty string");
        if (path_from_state != NULL && (!yyjson_is_str(path_from_state) || yyjson_get_str(path_from_state)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.level.load path_from_state must be a non-empty string");
        yyjson_val *optional = obj_get(action, "optional");
        if (optional != NULL && !yyjson_is_bool(optional))
            return validation_error(ctx, json_path, "editor.level.load optional must be a boolean");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.level.load outputs must be an object");
        static const char *const output_keys[] = {
            "valid_key",
            "message_key",
            "path_key",
            "brush_world_key",
            "brush_source_path_key",
            "brush_dirty_key",
            "brush_revision_key",
            "brush_saved_revision_key",
            "player_start_source_path_key",
            "player_start_count_key",
            "player_start_dirty_key",
            "player_start_revision_key",
            "player_start_saved_revision_key",
        };
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path, "editor.level.load output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.test_run.prepare") == 0)
    {
        if (!is_non_empty_string(action, "data_asset"))
            return validation_error(ctx, json_path, "editor.test_run.prepare requires a non-empty data_asset");
        static const char *const string_fields[] = {"runner", "root", "pack", "media"};
        for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
        {
            yyjson_val *field = obj_get(action, string_fields[i]);
            if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.test_run.prepare command fields must be non-empty strings");
        }
        yyjson_val *embedded = obj_get(action, "embedded");
        if (embedded != NULL && !yyjson_is_bool(embedded))
            return validation_error(ctx, json_path, "editor.test_run.prepare embedded must be a boolean");
        const int mount_count = (obj_get(action, "root") != NULL ? 1 : 0) + (obj_get(action, "pack") != NULL ? 1 : 0) +
                                (embedded != NULL && yyjson_get_bool(embedded) ? 1 : 0);
        if (mount_count > 1)
            return validation_error(ctx, json_path,
                                    "editor.test_run.prepare accepts at most one of root, pack, or embedded");
        const char *scene = json_string(action, "scene");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
            return false;
        yyjson_val *player_start = obj_get(action, "player_start");
        if (player_start != NULL && (!yyjson_is_str(player_start) || yyjson_get_str(player_start)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.test_run.prepare player_start must be a non-empty string");
        if (player_start != NULL && !require_ref(ctx, &names->editor_player_starts, "editor player start",
                                                 yyjson_get_str(player_start), json_path))
        {
            return false;
        }
        if (scene == NULL && player_start == NULL)
            return validation_error(ctx, json_path, "editor.test_run.prepare requires scene or player_start");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.test_run.prepare outputs must be an object");
        static const char *const output_keys[] = {"valid_key",        "message_key",    "manifest_json_key",
                                                  "size_key",         "data_asset_key", "scene_key",
                                                  "player_start_key", "target_key",     "command_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.test_run.prepare output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.test_run.save_manifest") == 0)
    {
        if (!is_non_empty_string(action, "data_asset"))
            return validation_error(ctx, json_path, "editor.test_run.save_manifest requires a non-empty data_asset");
        static const char *const string_fields[] = {"runner", "root", "pack", "media"};
        for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
        {
            yyjson_val *field = obj_get(action, string_fields[i]);
            if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.test_run.save_manifest command fields must be non-empty strings");
        }
        yyjson_val *embedded = obj_get(action, "embedded");
        if (embedded != NULL && !yyjson_is_bool(embedded))
            return validation_error(ctx, json_path, "editor.test_run.save_manifest embedded must be a boolean");
        const int mount_count = (obj_get(action, "root") != NULL ? 1 : 0) + (obj_get(action, "pack") != NULL ? 1 : 0) +
                                (embedded != NULL && yyjson_get_bool(embedded) ? 1 : 0);
        if (mount_count > 1)
            return validation_error(ctx, json_path,
                                    "editor.test_run.save_manifest accepts at most one of root, pack, or embedded");
        yyjson_val *path = obj_get(action, "path");
        yyjson_val *path_from_state = obj_get(action, "path_from_state");
        if ((path == NULL && path_from_state == NULL) || (path != NULL && path_from_state != NULL))
            return validation_error(ctx, json_path,
                                    "editor.test_run.save_manifest requires exactly one of path or path_from_state");
        if (path != NULL && (!yyjson_is_str(path) || yyjson_get_str(path)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.test_run.save_manifest path must be a non-empty string");
        if (path_from_state != NULL && (!yyjson_is_str(path_from_state) || yyjson_get_str(path_from_state)[0] == '\0'))
            return validation_error(ctx, json_path,
                                    "editor.test_run.save_manifest path_from_state must be a non-empty string");
        const char *scene = json_string(action, "scene");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
            return false;
        yyjson_val *player_start = obj_get(action, "player_start");
        if (player_start != NULL && (!yyjson_is_str(player_start) || yyjson_get_str(player_start)[0] == '\0'))
            return validation_error(ctx, json_path,
                                    "editor.test_run.save_manifest player_start must be a non-empty string");
        if (player_start != NULL && !require_ref(ctx, &names->editor_player_starts, "editor player start",
                                                 yyjson_get_str(player_start), json_path))
        {
            return false;
        }
        if (scene == NULL && player_start == NULL)
            return validation_error(ctx, json_path, "editor.test_run.save_manifest requires scene or player_start");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.test_run.save_manifest outputs must be an object");
        static const char *const output_keys[] = {"valid_key",  "message_key",    "path_key",  "manifest_json_key",
                                                  "size_key",   "data_asset_key", "scene_key", "player_start_key",
                                                  "target_key", "command_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.test_run.save_manifest output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.brush_world.status") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *message = obj_get(action, "message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "editor.brush_world.status message must be a string");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.brush_world.status outputs must be an object");
        static const char *const output_keys[] = {"valid_key", "message_key",  "world_key",         "source_path_key",
                                                  "dirty_key", "revision_key", "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.brush_world.status output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.brush_world.validate_source") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        yyjson_val *message = obj_get(action, "message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "editor.brush_world.validate_source message must be a string");
        yyjson_val *near_gap_units = obj_get(action, "near_gap_units");
        if (near_gap_units != NULL && (!yyjson_is_int(near_gap_units) || yyjson_get_int(near_gap_units) < 0))
            return validation_error(ctx, json_path,
                                    "editor.brush_world.validate_source near_gap_units must be a non-negative integer");
        if (obj_get(action, "allow_missing_source") != NULL)
            return validation_error(ctx, json_path,
                                    "editor.brush_world.validate_source allow_missing_source is no longer supported");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.brush_world.validate_source outputs must be an object");
        static const char *const output_keys[] = {"valid_key",
                                                  "message_key",
                                                  "box_count_key",
                                                  "snap_units_key",
                                                  "off_snap_count_key",
                                                  "overlap_count_key",
                                                  "near_gap_count_key",
                                                  "face_contact_count_key",
                                                  "edge_contact_count_key",
                                                  "vertex_contact_count_key",
                                                  "partial_face_contact_count_key",
                                                  "runtime_brush_count_key",
                                                  "runtime_source_mismatch_count_key",
                                                  "compiled_face_count_key",
                                                  "compiled_face_missing_source_count_key",
                                                  "compiled_face_unknown_source_count_key",
                                                  "first_issue_kind_key",
                                                  "first_issue_source_name_key",
                                                  "first_issue_source_stable_id_key",
                                                  "first_issue_related_source_name_key",
                                                  "first_issue_related_source_stable_id_key",
                                                  "first_issue_source_face_key",
                                                  "first_issue_runtime_brush_name_key",
                                                  "first_issue_runtime_brush_index_key",
                                                  "first_issue_compiled_face_index_key",
                                                  "world_key",
                                                  "source_path_key",
                                                  "dirty_key",
                                                  "revision_key",
                                                  "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.brush_world.validate_source output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.brush_world.validate_enclosure") == 0)
    {
        if (!require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        const char *player_start = json_string(action, "player_start");
        if (player_start == NULL || player_start[0] == '\0')
            return validation_error(ctx, json_path, "editor.brush_world.validate_enclosure requires a player_start");
        yyjson_val *allow_missing_player_start = obj_get(action, "allow_missing_player_start");
        if (allow_missing_player_start != NULL && !yyjson_is_bool(allow_missing_player_start))
        {
            return validation_error(ctx, json_path,
                                    "editor.brush_world.validate_enclosure allow_missing_player_start must be a "
                                    "boolean");
        }
        const bool permits_missing_player_start =
            allow_missing_player_start != NULL && yyjson_get_bool(allow_missing_player_start);
        if (!permits_missing_player_start &&
            !require_ref(ctx, &names->editor_player_starts, "editor player start", player_start, json_path))
        {
            return false;
        }
        yyjson_val *message = obj_get(action, "message");
        if (message != NULL && !yyjson_is_str(message))
            return validation_error(ctx, json_path, "editor.brush_world.validate_enclosure message must be a string");
        if (obj_get(action, "allow_missing_source") != NULL)
            return validation_error(
                ctx, json_path, "editor.brush_world.validate_enclosure allow_missing_source is no longer supported");
        yyjson_val *max_cells = obj_get(action, "max_cells");
        if (max_cells != NULL && (!yyjson_is_int(max_cells) || yyjson_get_int(max_cells) <= 0))
            return validation_error(ctx, json_path,
                                    "editor.brush_world.validate_enclosure max_cells must be a positive integer");
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.brush_world.validate_enclosure outputs must be an object");
        static const char *const output_keys[] = {"valid_key",
                                                  "message_key",
                                                  "has_source_key",
                                                  "has_player_start_key",
                                                  "box_count_key",
                                                  "grid_cell_count_key",
                                                  "solid_cell_count_key",
                                                  "visited_cell_count_key",
                                                  "open_boundary_count_key",
                                                  "leak_point_key",
                                                  "leak_axis_key",
                                                  "leak_side_key",
                                                  "candidate_name_key",
                                                  "candidate_stable_id_key",
                                                  "candidate_face_key",
                                                  "candidate_point_key",
                                                  "candidate_distance_key",
                                                  "world_key",
                                                  "source_path_key",
                                                  "dirty_key",
                                                  "revision_key",
                                                  "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL &&
                (!yyjson_is_str(output) || yyjson_get_str(output) == NULL || yyjson_get_str(output)[0] == '\0'))
            {
                return validation_error(ctx, json_path,
                                        "editor.brush_world.validate_enclosure output keys must be non-empty strings");
            }
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.brush_world.create_box") == 0)
    {
        const char *position_from = json_string(action, "position_from");
        const bool from_preview = position_from != NULL && SDL_strcmp(position_from, "placement_preview") == 0;
        if (!from_preview &&
            !require_ref(ctx, &names->brush_worlds, "brush world", json_string(action, "world"), json_path))
            return false;
        if (!from_preview && !is_non_empty_string(action, "material"))
            return validation_error(ctx, json_path, "editor.brush_world.create_box requires a non-empty material");
        yyjson_val *name = obj_get(action, "name");
        if (name != NULL && (!yyjson_is_str(name) || yyjson_get_str(name)[0] == '\0'))
            return validation_error(ctx, json_path,
                                    "editor.brush_world.create_box name must be non-empty when present");
        yyjson_val *preview_mode = obj_get(action, "preview_mode");
        if (preview_mode != NULL && (!yyjson_is_str(preview_mode) || yyjson_get_str(preview_mode)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.brush_world.create_box preview_mode must be non-empty");
        yyjson_val *min = obj_get(action, "min");
        yyjson_val *max = obj_get(action, "max");
        if (!from_preview && (!is_exact_vec_array(min, 3) || !is_exact_vec_array(max, 3)))
            return validation_error(ctx, json_path, "editor.brush_world.create_box requires min and max vec3 values");
        if (position_from != NULL && SDL_strcmp(position_from, "selection_point") != 0 &&
            SDL_strcmp(position_from, "placement_preview") != 0)
            return validation_error(ctx, json_path,
                                    "editor.brush_world.create_box position_from must be selection_point or "
                                    "placement_preview");
        yyjson_val *position_offset = obj_get(action, "position_offset");
        if (position_offset != NULL && !is_exact_vec_array(position_offset, 3))
            return validation_error(ctx, json_path, "editor.brush_world.create_box position_offset must be a vec3");
        yyjson_val *snap = obj_get(action, "snap");
        if (snap != NULL && (!yyjson_is_num(snap) || yyjson_get_num(snap) <= 0.0))
            return validation_error(ctx, json_path, "editor.brush_world.create_box snap must be a positive number");
        char contents_path[PATH_BUFFER_SIZE];
        format_path(contents_path, sizeof(contents_path), "%s.contents", json_path);
        if (!validate_brush_string_or_string_array(ctx, obj_get(action, "contents"), contents_path,
                                                   "editor.brush_world.create_box contents", brush_content_name_valid,
                                                   false))
        {
            return false;
        }
        if (!from_preview)
        {
            const double min_x = yyjson_get_num(yyjson_arr_get(min, 0));
            const double min_y = yyjson_get_num(yyjson_arr_get(min, 1));
            const double min_z = yyjson_get_num(yyjson_arr_get(min, 2));
            const double max_x = yyjson_get_num(yyjson_arr_get(max, 0));
            const double max_y = yyjson_get_num(yyjson_arr_get(max, 1));
            const double max_z = yyjson_get_num(yyjson_arr_get(max, 2));
            if (!(min_x < max_x && min_y < max_y && min_z < max_z))
                return validation_error(ctx, json_path, "editor.brush_world.create_box bounds require min < max");
        }
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.brush_world.create_box outputs must be an object");
        static const char *const output_keys[] = {
            "valid_key", "message_key",  "brush_key",          "world_key",      "source_path_key",
            "dirty_key", "revision_key", "saved_revision_key", "bounds_min_key", "bounds_max_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.brush_world.create_box output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.player_start.place") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "editor.player_start.place requires a non-empty name");
        const char *scene = json_string(action, "scene");
        const char *target = json_string(action, "target");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, json_path))
            return false;
        if (target != NULL && !require_actor_ref(ctx, names, target, json_path))
            return false;
        yyjson_val *position = obj_get(action, "position");
        if (position != NULL && !is_exact_vec_array(position, 3))
            return validation_error(ctx, json_path, "editor.player_start.place position must be a vec3");
        const char *position_from = json_string(action, "position_from");
        if (position != NULL && position_from != NULL)
            return validation_error(ctx, json_path,
                                    "editor.player_start.place requires position or position_from, not both");
        yyjson_val *preview_mode = obj_get(action, "preview_mode");
        if (preview_mode != NULL && (!yyjson_is_str(preview_mode) || yyjson_get_str(preview_mode)[0] == '\0'))
            return validation_error(ctx, json_path, "editor.player_start.place preview_mode must be non-empty");
        if (position_from != NULL && SDL_strcmp(position_from, "selection_point") != 0 &&
            SDL_strcmp(position_from, "placement_preview") != 0)
            return validation_error(ctx, json_path,
                                    "editor.player_start.place position_from must be selection_point or "
                                    "placement_preview");
        yyjson_val *yaw = obj_get(action, "yaw");
        yyjson_val *pitch = obj_get(action, "pitch");
        yyjson_val *apply_to_target = obj_get(action, "apply_to_target");
        if ((yaw != NULL && !yyjson_is_num(yaw)) || (pitch != NULL && !yyjson_is_num(pitch)) ||
            (apply_to_target != NULL && !yyjson_is_bool(apply_to_target)))
        {
            return validation_error(ctx, json_path,
                                    "editor.player_start.place yaw/pitch must be numeric and apply_to_target bool");
        }
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.player_start.place outputs must be an object");
        static const char *const output_keys[] = {"valid_key",  "message_key",  "player_start_key",  "scene_key",
                                                  "target_key", "position_key", "yaw_key",           "pitch_key",
                                                  "dirty_key",  "revision_key", "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.player_start.place output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.player_start.apply") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "editor.player_start.apply requires a non-empty name");
        const char *name = json_string(action, "name");
        yyjson_val *allow_missing_player_start = obj_get(action, "allow_missing_player_start");
        if (allow_missing_player_start != NULL && !yyjson_is_bool(allow_missing_player_start))
        {
            return validation_error(ctx, json_path,
                                    "editor.player_start.apply allow_missing_player_start must be a boolean");
        }
        const bool permits_missing_player_start =
            allow_missing_player_start != NULL && yyjson_get_bool(allow_missing_player_start);
        if (!permits_missing_player_start &&
            !require_ref(ctx, &names->editor_player_starts, "editor player start", name, json_path))
        {
            return false;
        }
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.player_start.apply outputs must be an object");
        static const char *const output_keys[] = {"valid_key",  "message_key",  "player_start_key",  "scene_key",
                                                  "target_key", "position_key", "yaw_key",           "pitch_key",
                                                  "dirty_key",  "revision_key", "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.player_start.apply output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "editor.player_start.delete") == 0)
    {
        const char *name = json_string(action, "name");
        yyjson_val *name_from_selection_value = obj_get(action, "name_from_selection");
        if (name_from_selection_value != NULL && !yyjson_is_bool(name_from_selection_value))
            return validation_error(ctx, json_path, "editor.player_start.delete name_from_selection must be bool");
        const bool name_from_selection =
            name_from_selection_value != NULL && yyjson_get_bool(name_from_selection_value);
        if ((name == NULL || name[0] == '\0') && !name_from_selection)
            return validation_error(ctx, json_path, "editor.player_start.delete requires name or name_from_selection");
        if (name != NULL && !require_ref(ctx, &names->editor_player_starts, "editor player start", name, json_path))
            return false;
        yyjson_val *outputs = obj_get(action, "outputs");
        if (outputs != NULL && !yyjson_is_obj(outputs))
            return validation_error(ctx, json_path, "editor.player_start.delete outputs must be an object");
        static const char *const output_keys[] = {"valid_key", "message_key",  "player_start_key",
                                                  "dirty_key", "revision_key", "saved_revision_key"};
        for (size_t i = 0; yyjson_is_obj(outputs) && i < SDL_arraysize(output_keys); ++i)
        {
            yyjson_val *output = obj_get(outputs, output_keys[i]);
            if (output != NULL && (!yyjson_is_str(output) || yyjson_get_str(output)[0] == '\0'))
                return validation_error(ctx, json_path,
                                        "editor.player_start.delete output keys must be non-empty strings");
        }
        return true;
    }
    if (SDL_strcmp(type, "network.direct_connect.start") == 0)
    {
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
            return validation_error(ctx, json_path,
                                    "network.direct_connect.start default_port must be integer 1..65535");
        return true;
    }
    if (SDL_strcmp(type, "network.direct_connect.cancel") == 0 ||
        SDL_strcmp(type, "network.direct_connect.observe") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "%s requires a non-empty name", type);
        return true;
    }
    if (SDL_strcmp(type, "network.host.start") == 0)
    {
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
    if (SDL_strcmp(type, "network.host.cancel") == 0 || SDL_strcmp(type, "network.host.observe") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "%s requires a non-empty name", type);
        return true;
    }
    if (SDL_strcmp(type, "network.discovery.start") == 0 || SDL_strcmp(type, "network.discovery.refresh") == 0)
    {
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
    if (SDL_strcmp(type, "network.discovery.observe") == 0 || SDL_strcmp(type, "network.discovery.cancel") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "%s requires a non-empty name", type);
        if (!is_non_empty_string(action, "collection"))
            return validation_error(ctx, json_path, "%s requires a non-empty collection", type);
        return true;
    }
    if (SDL_strcmp(type, "network.discovery.connect_selected") == 0)
    {
        if (!is_non_empty_string(action, "name"))
            return validation_error(ctx, json_path, "network.discovery.connect_selected requires a non-empty name");
        if (!is_non_empty_string(action, "collection"))
            return validation_error(ctx, json_path,
                                    "network.discovery.connect_selected requires a non-empty collection");
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
    if (SDL_strcmp(type, "ui.animate") == 0)
    {
        if (!is_non_empty_string(action, "target") && !is_non_empty_string(action, "ui"))
            return validation_error(ctx, json_path, "ui.animate requires a non-empty target");
        if (!is_non_empty_string(action, "property"))
            return validation_error(ctx, json_path, "ui.animate requires a non-empty property");
        if (!is_ui_tween_property(json_string(action, "property")))
            return validation_error(
                ctx, json_path, "ui.animate property must be alpha, scale, offset_x, offset_y, x, y, tint, or color");
        return validate_animation_common(ctx, action, json_path, names);
    }
    if (SDL_strncmp(type, "audio.", 6) == 0)
        return validate_audio_action(ctx, action, json_path, names, type);
    if (SDL_strcmp(type, "persistence.load") == 0 || SDL_strcmp(type, "persistence.save") == 0)
    {
        const char *entry = json_string(action, "entry");
        if (entry == NULL)
            entry = json_string(action, "name");
        return require_ref(ctx, &names->persistence, "persistence entry", entry, json_path);
    }
    if (SDL_strcmp(type, "entity.set_active") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        if (!yyjson_is_bool(obj_get(action, "active")))
            return validation_error(ctx, json_path, "entity.set_active requires a boolean active value");
        return true;
    }
    if (SDL_strcmp(type, "transform.set_position") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        if (!is_vec_array(obj_get(action, "position"), 2))
            return validation_error(ctx, json_path, "transform.set_position requires a numeric position array");
        return true;
    }
    if (SDL_strcmp(type, "camera.toggle") == 0)
    {
        return require_ref(ctx, &names->cameras, "camera", json_string(action, "camera"), json_path) &&
               require_ref(ctx, &names->cameras, "camera", json_string(action, "fallback"), json_path);
    }
    if (SDL_strcmp(type, "camera.set") == 0)
        return require_ref(ctx, &names->cameras, "camera", json_string(action, "camera"), json_path);
    if (SDL_strcmp(type, "scene.set") == 0)
    {
        if (!require_ref(ctx, &names->scenes, "scene", json_string(action, "scene"), json_path))
            return false;
        yyjson_val *payload = obj_get(action, "payload");
        if (payload != NULL)
        {
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
        }
        return true;
    }
    if (SDL_strcmp(type, "adapter.invoke") == 0)
    {
        const char *adapter = json_string(action, "adapter");
        if (!require_ref(ctx, &names->adapters, "adapter", adapter, json_path))
            return false;
        if (!note_name(&names->used_adapters, adapter, json_path))
            return validation_error(ctx, json_path, "failed to record adapter use");
        if (json_string(action, "target") != NULL &&
            !require_ref(ctx, &names->entities, "entity", json_string(action, "target"), json_path))
            return false;
        return true;
    }
    if (SDL_strcmp(type, "branch") == 0)
    {
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

    return validation_error(ctx, json_path, "unsupported logic action type '%s'", type);
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

#define ACTION_RULE_EXACT_ENTRY(name) {name, ACTION_RULE_EXACT, validate_known_action}
#define ACTION_RULE_PREFIX_ENTRY(name) {name, ACTION_RULE_PREFIX, validate_known_action}

static bool action_rule_matches(const action_validation_rule *rule, const char *type)
{
    if (rule->match == ACTION_RULE_PREFIX)
        return SDL_strncmp(type, rule->name, SDL_strlen(rule->name)) == 0;
    return SDL_strcmp(type, rule->name) == 0;
}

static const action_validation_rule *find_action_validation_rule(const char *type)
{
    static const action_validation_rule rules[] = {
        ACTION_RULE_EXACT_ENTRY("signal.emit"),
        ACTION_RULE_EXACT_ENTRY("timer.start"),
        ACTION_RULE_EXACT_ENTRY("property.set"),
        ACTION_RULE_EXACT_ENTRY("property.add"),
        ACTION_RULE_EXACT_ENTRY("property.snapshot"),
        ACTION_RULE_EXACT_ENTRY("property.restore_snapshot"),
        ACTION_RULE_EXACT_ENTRY("property.animate"),
        ACTION_RULE_EXACT_ENTRY("property.reset_defaults"),
        ACTION_RULE_EXACT_ENTRY("debug.write_actor_properties"),
        ACTION_RULE_EXACT_ENTRY("actor.spawn"),
        ACTION_RULE_EXACT_ENTRY("actor.despawn"),
        ACTION_RULE_EXACT_ENTRY("actor.despawn_by_tag"),
        ACTION_RULE_EXACT_ENTRY("combat.damage"),
        ACTION_RULE_EXACT_ENTRY("combat.heal"),
        ACTION_RULE_EXACT_ENTRY("combat.kill"),
        ACTION_RULE_EXACT_ENTRY("combat.revive"),
        ACTION_RULE_EXACT_ENTRY("resource.add"),
        ACTION_RULE_EXACT_ENTRY("resource.consume"),
        ACTION_RULE_EXACT_ENTRY("resource.set"),
        ACTION_RULE_EXACT_ENTRY("pickup.collect"),
        ACTION_RULE_EXACT_ENTRY("resource.station.use"),
        ACTION_RULE_EXACT_ENTRY("status_effect.apply"),
        ACTION_RULE_EXACT_ENTRY("weapon.reload"),
        ACTION_RULE_EXACT_ENTRY("weapon.hitscan"),
        ACTION_RULE_EXACT_ENTRY("interaction.use"),
        ACTION_RULE_EXACT_ENTRY("effect.explosion"),
        ACTION_RULE_EXACT_ENTRY("noise.emit"),
        ACTION_RULE_EXACT_ENTRY("sector_door.open"),
        ACTION_RULE_EXACT_ENTRY("sector_door.close"),
        ACTION_RULE_EXACT_ENTRY("sector_door.toggle"),
        ACTION_RULE_EXACT_ENTRY("sector_door.interact"),
        ACTION_RULE_EXACT_ENTRY("sector_lighting.set"),
        ACTION_RULE_EXACT_ENTRY("projectile.fire"),
        ACTION_RULE_EXACT_ENTRY("controller.fps.launch"),
        ACTION_RULE_EXACT_ENTRY("controller.fps.teleport"),
        ACTION_RULE_EXACT_ENTRY("controller.fps.push"),
        ACTION_RULE_EXACT_ENTRY("controller.fps_sector.launch"),
        ACTION_RULE_EXACT_ENTRY("controller.fps_sector.teleport"),
        ACTION_RULE_EXACT_ENTRY("grid.spawn_from_glyphs"),
        ACTION_RULE_EXACT_ENTRY("grid.spawn_runs_from_glyphs"),
        ACTION_RULE_EXACT_ENTRY("grid.pickup_layer.reset"),
        ACTION_RULE_EXACT_ENTRY("input.reset_bindings"),
        ACTION_RULE_EXACT_ENTRY("input.apply_profile"),
        ACTION_RULE_EXACT_ENTRY("input.apply_active_profile"),
        ACTION_RULE_EXACT_ENTRY("input.clear_network_input_overrides"),
        ACTION_RULE_EXACT_ENTRY("scene_state.set"),
        ACTION_RULE_EXACT_ENTRY("scene_state.toggle"),
        ACTION_RULE_EXACT_ENTRY("scene_state.cycle"),
        ACTION_RULE_EXACT_ENTRY("console.write"),
        ACTION_RULE_EXACT_ENTRY("editor.selection.clear"),
        ACTION_RULE_EXACT_ENTRY("editor.vertex.selection.clear"),
        ACTION_RULE_EXACT_ENTRY("editor.vertex.delete_selected"),
        ACTION_RULE_EXACT_ENTRY("editor.vertex.merge_selected_to_hover"),
        ACTION_RULE_EXACT_ENTRY("editor.vertex.add_to_source"),
        ACTION_RULE_EXACT_ENTRY("editor.vertex.validate_source"),
        ACTION_RULE_EXACT_ENTRY("editor.vertex.snap_selected"),
        ACTION_RULE_EXACT_ENTRY("editor.selection.select_brush"),
        ACTION_RULE_EXACT_ENTRY("editor.selection.delete_selected"),
        ACTION_RULE_EXACT_ENTRY("editor.selection.resize_y"),
        ACTION_RULE_EXACT_ENTRY("editor.selection.run"),
        ACTION_RULE_EXACT_ENTRY("editor.command.preview"),
        ACTION_RULE_EXACT_ENTRY("editor.command.clear_preview"),
        ACTION_RULE_EXACT_ENTRY("editor.command.commit"),
        ACTION_RULE_EXACT_ENTRY("editor.command.undo"),
        ACTION_RULE_EXACT_ENTRY("editor.command.redo"),
        ACTION_RULE_EXACT_ENTRY("editor.brush_world.export"),
        ACTION_RULE_EXACT_ENTRY("editor.level.export"),
        ACTION_RULE_EXACT_ENTRY("editor.level.save"),
        ACTION_RULE_EXACT_ENTRY("editor.level.load"),
        ACTION_RULE_EXACT_ENTRY("editor.test_run.prepare"),
        ACTION_RULE_EXACT_ENTRY("editor.test_run.save_manifest"),
        ACTION_RULE_EXACT_ENTRY("editor.brush_world.status"),
        ACTION_RULE_EXACT_ENTRY("editor.brush_world.validate_source"),
        ACTION_RULE_EXACT_ENTRY("editor.brush_world.validate_enclosure"),
        ACTION_RULE_EXACT_ENTRY("editor.brush_world.create_box"),
        ACTION_RULE_EXACT_ENTRY("editor.player_start.place"),
        ACTION_RULE_EXACT_ENTRY("editor.player_start.apply"),
        ACTION_RULE_EXACT_ENTRY("editor.player_start.delete"),
        ACTION_RULE_EXACT_ENTRY("network.direct_connect.start"),
        ACTION_RULE_EXACT_ENTRY("network.direct_connect.cancel"),
        ACTION_RULE_EXACT_ENTRY("network.direct_connect.observe"),
        ACTION_RULE_EXACT_ENTRY("network.host.start"),
        ACTION_RULE_EXACT_ENTRY("network.host.cancel"),
        ACTION_RULE_EXACT_ENTRY("network.host.observe"),
        ACTION_RULE_EXACT_ENTRY("network.discovery.start"),
        ACTION_RULE_EXACT_ENTRY("network.discovery.refresh"),
        ACTION_RULE_EXACT_ENTRY("network.discovery.observe"),
        ACTION_RULE_EXACT_ENTRY("network.discovery.cancel"),
        ACTION_RULE_EXACT_ENTRY("network.discovery.connect_selected"),
        ACTION_RULE_EXACT_ENTRY("ui.animate"),
        ACTION_RULE_PREFIX_ENTRY("audio."),
        ACTION_RULE_EXACT_ENTRY("persistence.load"),
        ACTION_RULE_EXACT_ENTRY("persistence.save"),
        ACTION_RULE_EXACT_ENTRY("entity.set_active"),
        ACTION_RULE_EXACT_ENTRY("transform.set_position"),
        ACTION_RULE_EXACT_ENTRY("camera.toggle"),
        ACTION_RULE_EXACT_ENTRY("camera.set"),
        ACTION_RULE_EXACT_ENTRY("scene.set"),
        ACTION_RULE_EXACT_ENTRY("adapter.invoke"),
        ACTION_RULE_EXACT_ENTRY("branch"),
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
    if (rule == NULL)
        return validation_error(ctx, json_path, "unsupported logic action type '%s'", type);
    return rule->validate != NULL ? rule->validate(ctx, action, json_path, names, type)
                                  : validate_known_action(ctx, action, json_path, names, type);
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

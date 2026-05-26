/**
 * @file game_data_validation_components.c
 * @brief Entity component validation.
 */

#include "game_data_validation_internal.h"

#include <slayer3d/actor_controller.h>

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

static bool is_axis_name(const char *axis)
{
    return axis != NULL && (SDL_strcmp(axis, "x") == 0 || SDL_strcmp(axis, "y") == 0 || SDL_strcmp(axis, "z") == 0);
}

bool is_supported_component_type(const char *type)
{
    const char *known[] = {
        "adapter.controller",
        "collision.aabb",
        "collision.circle",
        "combat.health",
        "control.axis_1d",
        "controller.editor_camera",
        "controller.fps_brush",
        "controller.fps_sector",
        "lifecycle.ttl",
        "light.directional",
        "light.point",
        "light.spot",
        "motion.brush_velocity_3d",
        "motion.grid_agent",
        "motion.oscillate",
        "motion.patrol",
        "motion.scroll_wrap",
        "motion.sector_velocity_3d",
        "motion.spin",
        "motion.velocity_2d",
        "motion.velocity_3d",
        "interactable",
        "particles.emitter",
        "pickup.respawn",
        "property.decay",
        "render.composite",
        "render.cube",
        "render.mesh_primitive",
        "render.model",
        "render.sphere",
        "render.sprite",
        "status_effect.timer",
        "viewmodel.bob",
        "weapon.projectile",
        "weapon.state",
    };

    if (type == NULL)
        return false;
    for (size_t i = 0; i < SDL_arraysize(known); ++i)
    {
        if (SDL_strcmp(type, known[i]) == 0)
            return true;
    }
    return false;
}

static bool brush_velocity_shape_valid(const char *shape)
{
    return shape == NULL || SDL_strcmp(shape, "point") == 0 || SDL_strcmp(shape, "sphere") == 0 ||
           SDL_strcmp(shape, "aabb") == 0;
}

bool validate_brush_velocity_component(validation_context *ctx, yyjson_val *component, const char *path,
                                       validation_names *names)
{
    const char *shape = json_string(component, "shape");
    if (!brush_velocity_shape_valid(shape))
        return validation_error(ctx, path, "motion.brush_velocity_3d shape must be point, sphere, or aabb");

    const char *string_fields[] = {"property",
                                   "extents_property",
                                   "reason",
                                   "last_impact_brush_property",
                                   "last_impact_world_property",
                                   "last_impact_material_property",
                                   "last_impact_position_property",
                                   "last_impact_normal_property",
                                   "last_impact_contents_property",
                                   "last_impact_surface_flags_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, component, path, "motion.brush_velocity_3d", string_fields[i]))
            return false;
    }

    yyjson_val *radius = obj_get(component, "radius");
    if (radius != NULL && (!yyjson_is_num(radius) || yyjson_get_num(radius) < 0.0))
        return validation_error(ctx, path, "motion.brush_velocity_3d radius must be non-negative");
    yyjson_val *extents = obj_get(component, "extents");
    if (extents != NULL && (!is_exact_vec_array(extents, 3) || !numeric_array_values_in_range(extents, 0.0, DBL_MAX)))
        return validation_error(ctx, path, "motion.brush_velocity_3d extents must be a non-negative vec3");

    yyjson_val *slide = obj_get(component, "slide");
    yyjson_val *despawn_on_hit = obj_get(component, "despawn_on_hit");
    if ((slide != NULL && !yyjson_is_bool(slide)) || (despawn_on_hit != NULL && !yyjson_is_bool(despawn_on_hit)))
        return validation_error(ctx, path, "motion.brush_velocity_3d slide and despawn_on_hit must be booleans");

    char contents_path[PATH_BUFFER_SIZE];
    format_path(contents_path, sizeof(contents_path), "%s.contents_mask", path);
    yyjson_val *impact_actions = obj_get(component, "impact_actions");
    return validate_brush_string_or_string_array(ctx, obj_get(component, "contents_mask"), contents_path,
                                                 "brush content", brush_content_name_valid, false) &&
           validate_optional_signal_field(ctx, component, path, names, "on_impact") &&
           (impact_actions == NULL || validate_action_array(ctx, impact_actions, path, names));
}

static bool validate_motion_patrol_collision(validation_context *ctx, yyjson_val *collision, const char *path)
{
    if (collision == NULL)
        return true;
    if (!yyjson_is_obj(collision))
        return validation_error(ctx, path, "motion.patrol collision must be an object");

    const char *type = json_string(collision, "type");
    if (type != NULL && SDL_strcmp(type, "brush") != 0)
        return validation_error(ctx, path, "motion.patrol collision type must be brush");
    if (!brush_velocity_shape_valid(json_string(collision, "shape")))
        return validation_error(ctx, path, "motion.patrol collision shape must be point, sphere, or aabb");

    yyjson_val *extents = obj_get(collision, "extents");
    if (extents != NULL && (!is_exact_vec_array(extents, 3) || !numeric_array_values_in_range(extents, 0.0, DBL_MAX)))
        return validation_error(ctx, path, "motion.patrol collision extents must be a non-negative vec3");
    yyjson_val *center_offset = obj_get(collision, "center_offset");
    if (center_offset != NULL && !is_exact_vec_array(center_offset, 3))
        return validation_error(ctx, path, "motion.patrol collision center_offset must be a vec3");
    yyjson_val *radius = obj_get(collision, "radius");
    if (radius != NULL && (!yyjson_is_num(radius) || yyjson_get_num(radius) < 0.0))
        return validation_error(ctx, path, "motion.patrol collision radius must be non-negative");
    yyjson_val *slide_iterations = obj_get(collision, "slide_iterations");
    if (slide_iterations != NULL && (!yyjson_is_int(slide_iterations) || yyjson_get_int(slide_iterations) < 1 ||
                                     yyjson_get_int(slide_iterations) > 8))
        return validation_error(ctx, path, "motion.patrol collision slide_iterations must be an integer in [1, 8]");
    yyjson_val *contact_skin = obj_get(collision, "contact_skin");
    if (contact_skin != NULL && (!yyjson_is_num(contact_skin) || yyjson_get_num(contact_skin) < 0.0))
        return validation_error(ctx, path, "motion.patrol collision contact_skin must be non-negative");
    yyjson_val *ground_probe_distance = obj_get(collision, "ground_probe_distance");
    if (ground_probe_distance != NULL &&
        (!yyjson_is_num(ground_probe_distance) || yyjson_get_num(ground_probe_distance) < 0.0))
        return validation_error(ctx, path, "motion.patrol collision ground_probe_distance must be non-negative");
    yyjson_val *walkable_normal_y = obj_get(collision, "walkable_normal_y");
    if (walkable_normal_y != NULL && (!yyjson_is_num(walkable_normal_y) || yyjson_get_num(walkable_normal_y) < 0.0 ||
                                      yyjson_get_num(walkable_normal_y) > 1.0))
        return validation_error(ctx, path, "motion.patrol collision walkable_normal_y must be in [0, 1]");
    yyjson_val *on_ground_property = obj_get(collision, "on_ground_property");
    if (on_ground_property != NULL && !is_non_empty_string(collision, "on_ground_property"))
        return validation_error(ctx, path, "motion.patrol collision on_ground_property must be non-empty");

    char contents_path[PATH_BUFFER_SIZE];
    format_path(contents_path, sizeof(contents_path), "%s.contents_mask", path);
    return validate_brush_string_or_string_array(ctx, obj_get(collision, "contents_mask"), contents_path,
                                                 "brush content", brush_content_name_valid, false);
}

bool validate_combat_health_component(validation_context *ctx, yyjson_val *component, const char *path)
{
    const char *property_keys[] = {"health_property", "max_health_property", "armor_property", "armor_absorb_property",
                                   "alive_property"};
    for (size_t i = 0; i < SDL_arraysize(property_keys); ++i)
    {
        yyjson_val *value = obj_get(component, property_keys[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, path, "combat.health property names must be non-empty strings");
    }

    yyjson_val *health = obj_get(component, "health");
    yyjson_val *max_health = obj_get(component, "max_health");
    yyjson_val *armor = obj_get(component, "armor");
    yyjson_val *armor_absorb = obj_get(component, "armor_absorb");
    if ((health != NULL && (!yyjson_is_num(health) || yyjson_get_num(health) < 0.0)) ||
        (max_health != NULL && (!yyjson_is_num(max_health) || yyjson_get_num(max_health) < 0.0)) ||
        (armor != NULL && (!yyjson_is_num(armor) || yyjson_get_num(armor) < 0.0)))
    {
        return validation_error(ctx, path, "combat.health numeric values must be non-negative");
    }
    if (armor_absorb != NULL &&
        (!yyjson_is_num(armor_absorb) || yyjson_get_num(armor_absorb) < 0.0 || yyjson_get_num(armor_absorb) > 1.0))
    {
        return validation_error(ctx, path, "combat.health armor_absorb must be in 0..1");
    }
    return true;
}

bool validate_pickup_respawn_component(validation_context *ctx, yyjson_val *component, const char *path)
{
    if (!validate_non_empty_string_field(ctx, component, path, "pickup.respawn", "timer_property") ||
        !validate_non_empty_string_field(ctx, component, path, "pickup.respawn", "available_property"))
    {
        return false;
    }
    return true;
}

bool validate_status_effect_timer_component(validation_context *ctx, yyjson_val *component, const char *path,
                                            validation_names *names)
{
    (void)names;
    if (!is_non_empty_string(component, "property"))
        return validation_error(ctx, path, "status_effect.timer requires a non-empty property");
    if (!validate_non_empty_string_field(ctx, component, path, "status_effect.timer", "duration_property") ||
        !validate_non_empty_string_field(ctx, component, path, "status_effect.timer", "active_property"))
    {
        return false;
    }
    yyjson_val *expired = obj_get(component, "expired_value");
    if (expired != NULL && !(yyjson_is_bool(expired) || yyjson_is_num(expired) || yyjson_is_str(expired)))
        return validation_error(ctx, path, "status_effect.timer expired_value must be scalar");
    return validate_optional_signal_field(ctx, component, path, names, "on_expire");
}

bool validate_particle_emitter_component(validation_context *ctx, yyjson_val *component, const char *path,
                                         const validation_names *names)
{
    const char *render_style = json_string(component, "render_style");
    if (render_style != NULL && SDL_strcmp(render_style, "default") != 0 &&
        SDL_strcmp(render_style, "soft_smoke") != 0 && SDL_strcmp(render_style, "soft_fire") != 0 &&
        SDL_strcmp(render_style, "muzzle_flash") != 0)
    {
        return validation_error(
            ctx, path,
            "particles.emitter render_style must be 'default', 'soft_smoke', 'soft_fire', or 'muzzle_flash'");
    }

    yyjson_val *position_offset = obj_get(component, "position_offset");
    if (position_offset != NULL && !is_vec_array(position_offset, 3))
        return validation_error(ctx, path, "particles.emitter position_offset must be a vec3");
    const char *space = json_string(component, "space");
    if (space != NULL && SDL_strcmp(space, "world") != 0 && SDL_strcmp(space, "camera") != 0)
        return validation_error(ctx, path, "particles.emitter space must be 'world' or 'camera'");
    if (!validate_render_camera_visibility_field(ctx, component, path, names, "visible_to_cameras") ||
        !validate_render_camera_visibility_field(ctx, component, path, names, "hidden_from_cameras"))
    {
        return false;
    }

    const char *property_fields[] = {
        "position_offset_property",    "position_offset_x_property", "position_offset_y_property",
        "position_offset_z_property",  "size_start_property",        "size_end_property",
        "size_scale_property",         "alpha_scale_property",       "emit_rate_property",
        "emissive_intensity_property",
    };
    for (size_t i = 0; i < SDL_arraysize(property_fields); ++i)
    {
        yyjson_val *value = obj_get(component, property_fields[i]);
        if (value != NULL && !is_non_empty_string(component, property_fields[i]))
            return validation_error(ctx, path, "particles.emitter %s must be non-empty", property_fields[i]);
    }

    return true;
}

bool validate_weapon_state_component(validation_context *ctx, yyjson_val *component, const char *path)
{
    const char *string_fields[] = {"clip_property",         "clip_size_property",      "reserve_property",
                                   "reload_timer_property", "reload_pending_property", "cooldown_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, component, path, "weapon.state", string_fields[i]))
            return false;
    }
    yyjson_val *clip_size = obj_get(component, "clip_size");
    yyjson_val *cooldown_rate = obj_get(component, "cooldown_rate");
    if ((clip_size != NULL && (!yyjson_is_num(clip_size) || yyjson_get_num(clip_size) < 0.0)) ||
        (cooldown_rate != NULL && (!yyjson_is_num(cooldown_rate) || yyjson_get_num(cooldown_rate) < 0.0)))
    {
        return validation_error(ctx, path, "weapon.state numeric values must be non-negative");
    }
    yyjson_val *consume_reserve = obj_get(component, "consume_reserve");
    if (consume_reserve != NULL && !yyjson_is_bool(consume_reserve))
        return validation_error(ctx, path, "weapon.state consume_reserve must be a boolean");
    return true;
}

static bool validate_interactable_requires(validation_context *ctx, yyjson_val *requires, const char *path)
{
    if (requires == NULL)
        return true;
    if (!yyjson_is_obj(requires))
        return validation_error(ctx, path, "interactable requires must be an object");
    const char *property = json_string(requires, "property");
    if (property == NULL)
        property = json_string(requires, "resource");
    if (property == NULL || property[0] == '\0')
        return validation_error(ctx, path, "interactable requires needs a non-empty property or resource");
    yyjson_val *amount = obj_get(requires, "amount");
    if (amount != NULL && (!yyjson_is_num(amount) || yyjson_get_num(amount) < 0.0))
        return validation_error(ctx, path, "interactable requires amount must be non-negative");
    yyjson_val *consume = obj_get(requires, "consume");
    if (consume != NULL && !yyjson_is_bool(consume))
        return validation_error(ctx, path, "interactable requires consume must be a boolean");
    return true;
}

bool validate_interactable_component(validation_context *ctx, yyjson_val *component, const char *path,
                                     validation_names *names)
{
    yyjson_val *range = obj_get(component, "range");
    yyjson_val *min_dot = obj_get(component, "min_dot");
    yyjson_val *cooldown = obj_get(component, "cooldown");
    if ((range != NULL && (!yyjson_is_num(range) || yyjson_get_num(range) < 0.0)) ||
        (cooldown != NULL && (!yyjson_is_num(cooldown) || yyjson_get_num(cooldown) < 0.0)) ||
        (min_dot != NULL &&
         (!yyjson_is_num(min_dot) || yyjson_get_num(min_dot) < -1.0 || yyjson_get_num(min_dot) > 1.0)))
    {
        return validation_error(ctx, path, "interactable range, min_dot, and cooldown values are invalid");
    }
    const char *string_fields[] = {"prompt", "prompt_key", "yaw_property", "cooldown_property"};
    for (size_t i = 0; i < SDL_arraysize(string_fields); ++i)
    {
        if (!validate_non_empty_string_field(ctx, component, path, "interactable", string_fields[i]))
            return false;
    }
    if (!validate_interactable_requires(ctx, obj_get(component, "requires"), path))
        return false;
    const char *signal_keys[] = {"signal", "on_locked", "on_cooldown"};
    for (size_t i = 0; i < SDL_arraysize(signal_keys); ++i)
    {
        if (!validate_optional_signal_field(ctx, component, path, names, signal_keys[i]))
            return false;
    }
    const char *action_keys[] = {"actions", "locked_actions", "cooldown_actions"};
    for (size_t i = 0; i < SDL_arraysize(action_keys); ++i)
    {
        yyjson_val *actions = obj_get(component, action_keys[i]);
        if (actions != NULL && !validate_action_array(ctx, actions, path, names))
            return false;
    }
    if (obj_get(component, "actions") == NULL && json_string(component, "signal") == NULL &&
        obj_get(component, "locked_actions") == NULL && json_string(component, "on_locked") == NULL &&
        obj_get(component, "cooldown_actions") == NULL && json_string(component, "on_cooldown") == NULL)
    {
        return validation_error(ctx, path,
                                "interactable requires actions, signal, locked_actions, on_locked, cooldown_actions, "
                                "or on_cooldown");
    }
    return true;
}

bool validate_projectile_fire_shape(validation_context *ctx, yyjson_val *value, const char *path,
                                    validation_names *names, bool require_target)
{
    if (!require_ref(ctx, &names->actor_pools, "actor pool", json_string(value, "pool"), path))
        return false;

    yyjson_val *target_value = obj_get(value, "target");
    yyjson_val *target_from_payload_value = obj_get(value, "target_from_payload");
    const char *target = json_string(value, "target");
    const char *target_from_payload = json_string(value, "target_from_payload");
    if (require_target)
    {
        if ((target == NULL && target_from_payload == NULL) || (target != NULL && target_from_payload != NULL))
            return validation_error(ctx, path, "projectile.fire requires exactly one of target or target_from_payload");
    }
    else if (target != NULL || target_from_payload != NULL)
    {
        return validation_error(ctx, path,
                                "weapon.projectile uses its owning actor and must not declare target fields");
    }
    if (target_value != NULL && !yyjson_is_str(target_value))
        return validation_error(ctx, path, "projectile target must be a string");
    if (target != NULL && !require_actor_ref(ctx, names, target, path))
        return false;
    if (target_from_payload_value != NULL &&
        (!yyjson_is_str(target_from_payload_value) || yyjson_get_str(target_from_payload_value)[0] == '\0'))
    {
        return validation_error(ctx, path, "projectile target_from_payload must be a non-empty string");
    }

    yyjson_val *offset = obj_get(value, "offset");
    if (offset != NULL && !is_vec_array(offset, 3))
        return validation_error(ctx, path, "projectile offset must be a vec3");
    yyjson_val *directional_offset = obj_get(value, "directional_offset");
    if (directional_offset != NULL)
    {
        if (!yyjson_is_obj(directional_offset))
            return validation_error(ctx, path, "projectile directional_offset must be an object");
        yyjson_val *property = obj_get(directional_offset, "property");
        yyjson_val *distance = obj_get(directional_offset, "distance");
        if (!yyjson_is_str(property) || yyjson_get_str(property)[0] == '\0')
            return validation_error(ctx, path, "projectile directional_offset property must be a non-empty string");
        if (!yyjson_is_num(distance))
            return validation_error(ctx, path, "projectile directional_offset distance must be numeric");
    }
    yyjson_val *velocity = obj_get(value, "velocity");
    if (velocity != NULL && !is_vec_array(velocity, 3))
        return validation_error(ctx, path, "projectile velocity must be a vec3");
    yyjson_val *velocity_from_property = obj_get(value, "velocity_from_property");
    if (velocity_from_property != NULL &&
        (!yyjson_is_str(velocity_from_property) || yyjson_get_str(velocity_from_property)[0] == '\0'))
    {
        return validation_error(ctx, path, "projectile velocity_from_property must be a non-empty string");
    }
    yyjson_val *speed = obj_get(value, "speed");
    if (speed != NULL && (!yyjson_is_num(speed) || yyjson_get_num(speed) < 0.0))
        return validation_error(ctx, path, "projectile speed must be non-negative");
    yyjson_val *cooldown = obj_get(value, "cooldown");
    if (cooldown != NULL && !yyjson_is_num(cooldown))
        return validation_error(ctx, path, "projectile cooldown must be numeric");
    yyjson_val *cooldown_property = obj_get(value, "cooldown_property");
    if (cooldown_property != NULL &&
        (!yyjson_is_str(cooldown_property) || yyjson_get_str(cooldown_property)[0] == '\0'))
    {
        return validation_error(ctx, path, "projectile cooldown_property must be a non-empty string");
    }
    const char *weapon_string_fields[] = {"clip_property", "ammo_resource", "ammo_property", "reload_timer_property",
                                          "direction_from_property"};
    for (size_t i = 0; i < SDL_arraysize(weapon_string_fields); ++i)
    {
        yyjson_val *field = obj_get(value, weapon_string_fields[i]);
        if (field != NULL && (!yyjson_is_str(field) || yyjson_get_str(field)[0] == '\0'))
            return validation_error(ctx, path, "projectile weapon fields must be non-empty strings");
    }
    yyjson_val *ammo_per_shot = obj_get(value, "ammo_per_shot");
    if (ammo_per_shot != NULL && (!yyjson_is_num(ammo_per_shot) || yyjson_get_num(ammo_per_shot) < 0.0))
        return validation_error(ctx, path, "projectile ammo_per_shot must be non-negative");
    const char *signal_keys[] = {"on_fire", "on_empty", "on_cooldown", "on_reloading"};
    for (size_t i = 0; i < SDL_arraysize(signal_keys); ++i)
    {
        const char *signal = json_string(value, signal_keys[i]);
        if (signal != NULL && !require_ref(ctx, &names->signals, "signal", signal, path))
            return false;
    }
    yyjson_val *properties = obj_get(value, "properties");
    if (properties != NULL && !yyjson_is_obj(properties))
        return validation_error(ctx, path, "projectile properties must be an object");
    return true;
}

typedef bool (*component_validator_fn)(validation_context *ctx, yyjson_val *component, const char *path,
                                       validation_names *names, const char *type);

typedef struct component_validation_handler
{
    const char *type;
    component_validator_fn validate;
} component_validation_handler;

static bool validate_fps_sector_component_handler(validation_context *ctx, yyjson_val *component, const char *path,
                                                  validation_names *names, const char *type)
{
    (void)type;
    return validate_fps_sector_component(ctx, component, path, names);
}

static bool validate_fps_brush_component_handler(validation_context *ctx, yyjson_val *component, const char *path,
                                                 validation_names *names, const char *type)
{
    (void)type;
    return validate_fps_brush_component(ctx, component, path, names);
}

static bool validate_editor_camera_component_handler(validation_context *ctx, yyjson_val *component, const char *path,
                                                     validation_names *names, const char *type)
{
    (void)type;
    if (!validate_editor_camera_component(ctx, component, path, names))
        return false;
    char condition_path[PATH_BUFFER_SIZE];
    format_path(condition_path, sizeof(condition_path), "%s.orthographic_controls_if", path);
    return validate_data_condition(ctx, obj_get(component, "orthographic_controls_if"), condition_path, names);
}

static bool validate_combat_health_component_handler(validation_context *ctx, yyjson_val *component, const char *path,
                                                     validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    return validate_combat_health_component(ctx, component, path);
}

static bool validate_pickup_respawn_component_handler(validation_context *ctx, yyjson_val *component, const char *path,
                                                      validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    return validate_pickup_respawn_component(ctx, component, path);
}

static bool validate_status_effect_timer_component_handler(validation_context *ctx, yyjson_val *component,
                                                           const char *path, validation_names *names, const char *type)
{
    (void)type;
    return validate_status_effect_timer_component(ctx, component, path, names);
}

static bool validate_weapon_state_component_handler(validation_context *ctx, yyjson_val *component, const char *path,
                                                    validation_names *names, const char *type)
{
    (void)names;
    (void)type;
    return validate_weapon_state_component(ctx, component, path);
}

static bool validate_interactable_component_handler(validation_context *ctx, yyjson_val *component, const char *path,
                                                    validation_names *names, const char *type)
{
    (void)type;
    return validate_interactable_component(ctx, component, path, names);
}

static bool validate_weapon_projectile_component_handler(validation_context *ctx, yyjson_val *component,
                                                         const char *path, validation_names *names, const char *type)
{
    (void)type;
    return require_ref(ctx, &names->actions, "input action", json_string(component, "action"), path) &&
           validate_projectile_fire_shape(ctx, component, path, names, false);
}

static bool validate_brush_velocity_component_handler(validation_context *ctx, yyjson_val *component, const char *path,
                                                      validation_names *names, const char *type)
{
    (void)type;
    return validate_brush_velocity_component(ctx, component, path, names);
}

static bool validate_particle_emitter_component_handler(validation_context *ctx, yyjson_val *component,
                                                        const char *path, validation_names *names, const char *type)
{
    (void)type;
    return validate_particle_emitter_component(ctx, component, path, names);
}

static bool validate_known_component(validation_context *ctx, yyjson_val *component, const char *path,
                                     validation_names *names, const char *type)
{
    if (SDL_strcmp(type, "control.axis_1d") == 0)
    {
        if (!is_axis_name(json_string(component, "axis")))
            return validation_error(ctx, path, "control.axis_1d requires axis x, y, or z");
        if (!require_ref(ctx, &names->actions, "input action", json_string(component, "negative"), path) ||
            !require_ref(ctx, &names->actions, "input action", json_string(component, "positive"), path))
            return false;
    }
    else if (SDL_strcmp(type, "adapter.controller") == 0)
    {
        const char *adapter = json_string(component, "adapter");
        if (!require_ref(ctx, &names->adapters, "adapter", adapter, path))
            return false;
        if (!note_name(&names->used_adapters, adapter, path))
            return validation_error(ctx, path, "failed to record adapter use");
        if (json_string(component, "target") != NULL &&
            !require_ref(ctx, &names->entities, "entity", json_string(component, "target"), path))
            return false;
    }
    else if (SDL_strcmp(type, "property.decay") == 0)
    {
        if (!is_non_empty_string(component, "property"))
            return validation_error(ctx, path, "property.decay requires a non-empty property");
        if (json_string(component, "rate_property") == NULL && !yyjson_is_num(obj_get(component, "rate")))
            return validation_error(ctx, path, "property.decay requires rate or rate_property");
    }
    else if (SDL_strcmp(type, "motion.oscillate") == 0)
    {
        yyjson_val *origin = obj_get(component, "origin");
        yyjson_val *amplitude = obj_get(component, "amplitude");
        yyjson_val *rate = obj_get(component, "rate");
        yyjson_val *phase = obj_get(component, "phase");
        if (origin != NULL && !is_vec_array(origin, 3))
            return validation_error(ctx, path, "motion.oscillate origin must be a vec3");
        if (amplitude != NULL && !is_vec_array(amplitude, 3))
            return validation_error(ctx, path, "motion.oscillate amplitude must be a vec3");
        if (rate != NULL && !yyjson_is_num(rate))
            return validation_error(ctx, path, "motion.oscillate rate must be a number");
        if (phase != NULL && !yyjson_is_num(phase))
            return validation_error(ctx, path, "motion.oscillate phase must be a number");
    }
    else if (SDL_strcmp(type, "motion.patrol") == 0)
    {
        yyjson_val *waypoints = obj_get(component, "waypoints");
        if (!yyjson_is_arr(waypoints) || yyjson_arr_size(waypoints) < 2 ||
            yyjson_arr_size(waypoints) > SLAYER3D_ACTOR_PATROL_MAX_WAYPOINTS)
            return validation_error(ctx, path, "motion.patrol requires 2 to 16 waypoints");
        for (size_t waypoint_index = 0; waypoint_index < yyjson_arr_size(waypoints); ++waypoint_index)
        {
            if (!is_vec_array(yyjson_arr_get(waypoints, waypoint_index), 3))
                return validation_error(ctx, path, "motion.patrol waypoints must be vec3 values");
        }
        yyjson_val *speed = obj_get(component, "speed");
        yyjson_val *wait_time = obj_get(component, "wait_time");
        yyjson_val *arrival_radius = obj_get(component, "arrival_radius");
        if (speed != NULL && (!yyjson_is_num(speed) || yyjson_get_num(speed) <= 0.0))
            return validation_error(ctx, path, "motion.patrol speed must be positive");
        if (wait_time != NULL && (!yyjson_is_num(wait_time) || yyjson_get_num(wait_time) < 0.0))
            return validation_error(ctx, path, "motion.patrol wait_time must be non-negative");
        if (arrival_radius != NULL && (!yyjson_is_num(arrival_radius) || yyjson_get_num(arrival_radius) <= 0.0))
            return validation_error(ctx, path, "motion.patrol arrival_radius must be positive");
        const char *mode = json_string(component, "mode");
        if (mode != NULL && SDL_strcmp(mode, "loop") != 0 && SDL_strcmp(mode, "ping_pong") != 0)
            return validation_error(ctx, path, "motion.patrol mode must be 'loop' or 'ping_pong'");
        yyjson_val *start_idle = obj_get(component, "start_idle");
        if (start_idle != NULL && !yyjson_is_bool(start_idle))
            return validation_error(ctx, path, "motion.patrol start_idle must be a boolean");
        yyjson_val *yaw_property = obj_get(component, "yaw_property");
        if (yaw_property != NULL && !is_non_empty_string(component, "yaw_property"))
            return validation_error(ctx, path, "motion.patrol yaw_property must be non-empty");
        yyjson_val *animation_time_property = obj_get(component, "animation_time_property");
        if (animation_time_property != NULL && !is_non_empty_string(component, "animation_time_property"))
            return validation_error(ctx, path, "motion.patrol animation_time_property must be non-empty");
        yyjson_val *animation_rate = obj_get(component, "animation_rate");
        if (animation_rate != NULL && !yyjson_is_num(animation_rate))
            return validation_error(ctx, path, "motion.patrol animation_rate must be a number");
        yyjson_val *animate_when_idle = obj_get(component, "animate_when_idle");
        if (animate_when_idle != NULL && !yyjson_is_bool(animate_when_idle))
            return validation_error(ctx, path, "motion.patrol animate_when_idle must be a boolean");
        yyjson_val *face_target = obj_get(component, "face_target");
        if (face_target != NULL && !yyjson_is_bool(face_target))
            return validation_error(ctx, path, "motion.patrol face_target must be a boolean");
        const char *yaw_forward = json_string(component, "yaw_forward");
        if (yaw_forward != NULL && SDL_strcmp(yaw_forward, "-z") != 0 && SDL_strcmp(yaw_forward, "negative_z") != 0 &&
            SDL_strcmp(yaw_forward, "+z") != 0 && SDL_strcmp(yaw_forward, "positive_z") != 0)
        {
            return validation_error(ctx, path,
                                    "motion.patrol yaw_forward must be '-z', 'negative_z', '+z', or "
                                    "'positive_z'");
        }
        char collision_path[PATH_BUFFER_SIZE];
        format_path(collision_path, sizeof(collision_path), "%s.collision", path);
        if (!validate_motion_patrol_collision(ctx, obj_get(component, "collision"), collision_path))
            return false;
        yyjson_val *signals = obj_get(component, "signals");
        if (signals != NULL)
        {
            if (!yyjson_is_obj(signals))
                return validation_error(ctx, path, "motion.patrol signals must be an object");
            const char *signal_keys[] = {"waypoint_reached", "loop_completed", "idle_started", "walk_started"};
            for (size_t signal_index = 0; signal_index < SDL_arraysize(signal_keys); ++signal_index)
            {
                const char *signal_name = json_string(signals, signal_keys[signal_index]);
                if (signal_name != NULL && !require_ref(ctx, &names->signals, "signal", signal_name, path))
                    return false;
            }
        }
    }
    else if (SDL_strcmp(type, "motion.scroll_wrap") == 0)
    {
        if (!is_axis_name(json_string(component, "axis")))
            return validation_error(ctx, path, "motion.scroll_wrap requires axis x, y, or z");
        if (!yyjson_is_num(obj_get(component, "speed")))
            return validation_error(ctx, path, "motion.scroll_wrap requires numeric speed");
        if (!yyjson_is_num(obj_get(component, "min")) || !yyjson_is_num(obj_get(component, "max")))
            return validation_error(ctx, path, "motion.scroll_wrap requires numeric min and max");
    }
    else if (SDL_strcmp(type, "motion.grid_agent") == 0)
    {
        if (!require_ref(ctx, &names->grid_maps, "grid map", json_string(component, "map"), path))
            return false;
        yyjson_val *speed = obj_get(component, "speed");
        if (speed != NULL && (!yyjson_is_num(speed) || yyjson_get_num(speed) < 0.0))
            return validation_error(ctx, path, "motion.grid_agent speed must be a non-negative number");
    }
    else if (SDL_strcmp(type, "motion.velocity_2d") == 0 || SDL_strcmp(type, "motion.velocity_3d") == 0)
    {
        yyjson_val *property = obj_get(component, "property");
        if (property != NULL && !is_non_empty_string(component, "property"))
            return validation_error(ctx, path, "%s property must be non-empty", type);
    }
    else if (SDL_strcmp(type, "lifecycle.ttl") == 0)
    {
        yyjson_val *ttl = obj_get(component, "ttl");
        if (ttl != NULL && (!yyjson_is_num(ttl) || yyjson_get_num(ttl) <= 0.0))
            return validation_error(ctx, path, "lifecycle.ttl ttl must be positive");
        yyjson_val *age_property = obj_get(component, "age_property");
        yyjson_val *ttl_property = obj_get(component, "ttl_property");
        yyjson_val *reason = obj_get(component, "reason");
        if ((age_property != NULL && !is_non_empty_string(component, "age_property")) ||
            (ttl_property != NULL && !is_non_empty_string(component, "ttl_property")) ||
            (reason != NULL && !is_non_empty_string(component, "reason")))
        {
            return validation_error(ctx, path, "lifecycle.ttl property names and reason must be non-empty strings");
        }
    }
    else if (SDL_strcmp(type, "motion.spin") == 0)
    {
        yyjson_val *property = obj_get(component, "property");
        if (property != NULL && !is_non_empty_string(component, "property"))
            return validation_error(ctx, path, "motion.spin property must be non-empty");
        yyjson_val *rate = obj_get(component, "rate");
        if (rate != NULL && !yyjson_is_num(rate))
            return validation_error(ctx, path, "motion.spin rate must be a number");
    }
    else if (SDL_strcmp(type, "motion.sector_velocity_3d") == 0)
    {
        if (!require_ref(ctx, &names->sector_levels, "sector level", json_string(component, "sector_level"), path))
            return false;
        yyjson_val *property = obj_get(component, "property");
        if (property != NULL && !is_non_empty_string(component, "property"))
            return validation_error(ctx, path, "motion.sector_velocity_3d property must be non-empty");
        yyjson_val *despawn_on_hit = obj_get(component, "despawn_on_hit");
        if (despawn_on_hit != NULL && !yyjson_is_bool(despawn_on_hit))
            return validation_error(ctx, path, "motion.sector_velocity_3d despawn_on_hit must be a boolean");
        yyjson_val *reason = obj_get(component, "reason");
        if (reason != NULL && !is_non_empty_string(component, "reason"))
            return validation_error(ctx, path, "motion.sector_velocity_3d reason must be non-empty");
    }
    else if (SDL_strncmp(type, "light.", 6) == 0)
    {
        yyjson_val *color = obj_get(component, "color");
        if (color != NULL && !is_vec_array(color, 3))
            return validation_error(ctx, path, "light component color must be a vec3");
        yyjson_val *enabled = obj_get(component, "enabled");
        if (enabled != NULL && !yyjson_is_bool(enabled))
            return validation_error(ctx, path, "light component enabled must be a boolean");
        yyjson_val *enabled_key = obj_get(component, "enabled_key");
        if (enabled_key != NULL && !is_non_empty_string(component, "enabled_key"))
            return validation_error(ctx, path, "light component enabled_key must be non-empty");
    }
    else if (SDL_strcmp(type, "render.cube") == 0 || SDL_strcmp(type, "render.sphere") == 0 ||
             SDL_strcmp(type, "render.mesh_primitive") == 0 || SDL_strcmp(type, "render.composite") == 0 ||
             SDL_strcmp(type, "render.sprite") == 0 || SDL_strcmp(type, "render.model") == 0)
    {
        yyjson_val *lighting = obj_get(component, "lighting");
        if (lighting != NULL && !yyjson_is_bool(lighting))
            return validation_error(ctx, path, "render primitive lighting must be a boolean");
        yyjson_val *lighting_key = obj_get(component, "lighting_key");
        if (lighting_key != NULL && !is_non_empty_string(component, "lighting_key"))
            return validation_error(ctx, path, "render primitive lighting_key must be non-empty");
        yyjson_val *lod = obj_get(component, "lod");
        if (lod != NULL && !yyjson_is_bool(lod))
            return validation_error(ctx, path, "render primitive lod must be a boolean");
        yyjson_val *lod_bias = obj_get(component, "lod_bias");
        if (lod_bias != NULL && (!yyjson_is_num(lod_bias) || yyjson_get_num(lod_bias) <= 0.0))
            return validation_error(ctx, path, "render primitive lod_bias must be a positive number");
        yyjson_val *lod_cull_pixels = obj_get(component, "lod_cull_pixels");
        if (lod_cull_pixels != NULL && (!yyjson_is_num(lod_cull_pixels) || yyjson_get_num(lod_cull_pixels) < 0.0))
            return validation_error(ctx, path, "render primitive lod_cull_pixels must be a non-negative number");
        const char *offset_properties[] = {"offset_x_property",     "offset_y_property",     "offset_z_property",
                                           "offset_x_add_property", "offset_y_add_property", "offset_z_add_property"};
        for (size_t i = 0; i < SDL_arraysize(offset_properties); ++i)
        {
            yyjson_val *offset_property = obj_get(component, offset_properties[i]);
            if (offset_property != NULL && !is_non_empty_string(component, offset_properties[i]))
                return validation_error(ctx, path, "render primitive offset property must be non-empty");
        }
        const char *offset_property_arrays[] = {"offset_x_add_properties", "offset_y_add_properties",
                                                "offset_z_add_properties"};
        for (size_t i = 0; i < SDL_arraysize(offset_property_arrays); ++i)
        {
            if (!validate_property_name_array_field(ctx, component, path, offset_property_arrays[i],
                                                    "render primitive offset property arrays"))
                return false;
        }
        if (!validate_render_camera_visibility_field(ctx, component, path, names, "visible_to_cameras") ||
            !validate_render_camera_visibility_field(ctx, component, path, names, "hidden_from_cameras"))
        {
            return false;
        }
        if (SDL_strcmp(type, "render.cube") == 0 || SDL_strcmp(type, "render.sphere") == 0 ||
            SDL_strcmp(type, "render.mesh_primitive") == 0 || SDL_strcmp(type, "render.composite") == 0)
        {
            yyjson_val *texture_value = obj_get(component, "texture");
            if (texture_value != NULL && !is_non_empty_string(component, "texture"))
                return validation_error(ctx, path, "render primitive texture must be a non-empty image asset id");
            const char *texture = json_string(component, "texture");
            if (texture != NULL && !require_ref(ctx, &names->images, "image asset", texture, path))
                return false;
        }
        if (SDL_strcmp(type, "render.cube") == 0)
        {
            yyjson_val *size = obj_get(component, "size");
            if (size != NULL && !is_vec_array(size, 3))
                return validation_error(ctx, path, "render.cube size must be a vec3");
            yyjson_val *size_property = obj_get(component, "size_property");
            if (size_property != NULL && !is_non_empty_string(component, "size_property"))
                return validation_error(ctx, path, "render.cube size_property must be non-empty");
        }
        if (SDL_strcmp(type, "render.mesh_primitive") == 0)
        {
            if (!validate_render_mesh_primitive_component(ctx, component, path, names))
                return false;
        }
        if (SDL_strcmp(type, "render.composite") == 0)
        {
            if (!validate_render_composite_component(ctx, component, path, names))
                return false;
        }
        if (SDL_strcmp(type, "render.sphere") == 0)
        {
            yyjson_val *rotation_axis = obj_get(component, "rotation_axis");
            if (rotation_axis != NULL && !is_vec_array(rotation_axis, 3))
                return validation_error(ctx, path, "render.sphere rotation_axis must be a vec3");
            yyjson_val *rotation_angle = obj_get(component, "rotation_angle");
            if (rotation_angle != NULL && !yyjson_is_num(rotation_angle))
                return validation_error(ctx, path, "render.sphere rotation_angle must be a number");
            yyjson_val *rotation_property = obj_get(component, "rotation_property");
            if (rotation_property != NULL && !is_non_empty_string(component, "rotation_property"))
                return validation_error(ctx, path, "render.sphere rotation_property must be non-empty");
        }
        if (SDL_strcmp(type, "render.sprite") == 0)
        {
            if (!require_ref(ctx, &names->sprites, "sprite asset", json_string(component, "sprite"), path))
                return false;
            yyjson_val *size = obj_get(component, "size");
            if (size != NULL && !is_vec_array(size, 2))
                return validation_error(ctx, path, "render.sprite size must be a vec2");
            yyjson_val *facing_yaw = obj_get(component, "facing_yaw");
            if (facing_yaw != NULL && !yyjson_is_num(facing_yaw))
                return validation_error(ctx, path, "render.sprite facing_yaw must be a number");
            yyjson_val *facing_yaw_property = obj_get(component, "facing_yaw_property");
            if (facing_yaw_property != NULL && !is_non_empty_string(component, "facing_yaw_property"))
                return validation_error(ctx, path, "render.sprite facing_yaw_property must be non-empty");
        }
        if (SDL_strcmp(type, "render.model") == 0)
        {
            if (!require_ref(ctx, &names->models, "model asset", json_string(component, "model"), path))
                return false;
            yyjson_val *scale = obj_get(component, "scale");
            if (scale != NULL && !is_vec_array(scale, 3))
                return validation_error(ctx, path, "render.model scale must be a vec3");
            yyjson_val *space = obj_get(component, "space");
            if (space != NULL && (!yyjson_is_str(space) || (SDL_strcmp(yyjson_get_str(space), "world") != 0 &&
                                                            SDL_strcmp(yyjson_get_str(space), "camera") != 0)))
                return validation_error(ctx, path, "render.model space must be 'world' or 'camera'");
            yyjson_val *rotation = obj_get(component, "rotation");
            if (rotation != NULL && !is_vec_array(rotation, 3))
                return validation_error(ctx, path, "render.model rotation must be a vec3");
            const char *property_fields[] = {"scale_property",   "pitch_property",     "yaw_property",
                                             "roll_property",    "pitch_add_property", "yaw_add_property",
                                             "roll_add_property"};
            for (size_t property_index = 0; property_index < SDL_arraysize(property_fields); ++property_index)
            {
                yyjson_val *property = obj_get(component, property_fields[property_index]);
                if (property != NULL && !is_non_empty_string(component, property_fields[property_index]))
                    return validation_error(ctx, path, "render.model property fields must be non-empty");
            }
            const char *property_arrays[] = {"pitch_add_properties", "yaw_add_properties", "roll_add_properties"};
            for (size_t property_index = 0; property_index < SDL_arraysize(property_arrays); ++property_index)
            {
                if (!validate_property_name_array_field(ctx, component, path, property_arrays[property_index],
                                                        "render.model property arrays"))
                    return false;
            }
            yyjson_val *animation_clip = obj_get(component, "animation_clip");
            if (animation_clip != NULL && (!yyjson_is_int(animation_clip) || yyjson_get_int(animation_clip) < 0))
                return validation_error(ctx, path, "render.model animation_clip must be a non-negative integer");
            yyjson_val *animation_time = obj_get(component, "animation_time");
            if (animation_time != NULL && !yyjson_is_num(animation_time))
                return validation_error(ctx, path, "render.model animation_time must be a number");
            yyjson_val *animation_time_property = obj_get(component, "animation_time_property");
            if (animation_time_property != NULL && !is_non_empty_string(component, "animation_time_property"))
                return validation_error(ctx, path, "render.model animation_time_property must be non-empty");
            yyjson_val *animation_loop = obj_get(component, "animation_loop");
            if (animation_loop != NULL && !yyjson_is_bool(animation_loop))
                return validation_error(ctx, path, "render.model animation_loop must be a boolean");
        }
    }
    else if (SDL_strcmp(type, "viewmodel.bob") == 0)
    {
        if (!require_actor_ref(ctx, names, json_string(component, "source"), path))
            return false;
        const char *property_fields[] = {
            "previous_position_property", "phase_property", "offset_x_property", "offset_y_property",
            "offset_z_property",          "pitch_property", "yaw_property",      "roll_property"};
        for (size_t property_index = 0; property_index < SDL_arraysize(property_fields); ++property_index)
        {
            yyjson_val *property = obj_get(component, property_fields[property_index]);
            if (property != NULL && !is_non_empty_string(component, property_fields[property_index]))
                return validation_error(ctx, path, "viewmodel.bob property fields must be non-empty");
        }
        yyjson_val *offset_amplitude = obj_get(component, "offset_amplitude");
        if (offset_amplitude != NULL && !is_vec_array(offset_amplitude, 3))
            return validation_error(ctx, path, "viewmodel.bob offset_amplitude must be a vec3");
        const char *non_negative[] = {"frequency", "speed_scale", "min_speed", "settle_rate"};
        for (size_t tuning_index = 0; tuning_index < SDL_arraysize(non_negative); ++tuning_index)
        {
            yyjson_val *value = obj_get(component, non_negative[tuning_index]);
            if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0))
                return validation_error(ctx, path, "viewmodel.bob numeric tuning values must be non-negative");
        }
        const char *numeric[] = {"pitch_amplitude", "yaw_amplitude", "roll_amplitude"};
        for (size_t tuning_index = 0; tuning_index < SDL_arraysize(numeric); ++tuning_index)
        {
            yyjson_val *value = obj_get(component, numeric[tuning_index]);
            if (value != NULL && !yyjson_is_num(value))
                return validation_error(ctx, path, "viewmodel.bob angular amplitudes must be numbers");
        }
    }

    return true;
}

static const component_validation_handler *component_validation_handlers(void)
{
    static const component_validation_handler handlers[] = {
        {"adapter.controller", validate_known_component},
        {"collision.aabb", validate_known_component},
        {"collision.circle", validate_known_component},
        {"combat.health", validate_combat_health_component_handler},
        {"control.axis_1d", validate_known_component},
        {"controller.editor_camera", validate_editor_camera_component_handler},
        {"controller.fps_brush", validate_fps_brush_component_handler},
        {"controller.fps_sector", validate_fps_sector_component_handler},
        {"lifecycle.ttl", validate_known_component},
        {"light.directional", validate_known_component},
        {"light.point", validate_known_component},
        {"light.spot", validate_known_component},
        {"motion.brush_velocity_3d", validate_brush_velocity_component_handler},
        {"motion.grid_agent", validate_known_component},
        {"motion.oscillate", validate_known_component},
        {"motion.patrol", validate_known_component},
        {"motion.scroll_wrap", validate_known_component},
        {"motion.sector_velocity_3d", validate_known_component},
        {"motion.spin", validate_known_component},
        {"motion.velocity_2d", validate_known_component},
        {"motion.velocity_3d", validate_known_component},
        {"interactable", validate_interactable_component_handler},
        {"particles.emitter", validate_particle_emitter_component_handler},
        {"pickup.respawn", validate_pickup_respawn_component_handler},
        {"property.decay", validate_known_component},
        {"render.composite", validate_known_component},
        {"render.cube", validate_known_component},
        {"render.mesh_primitive", validate_known_component},
        {"render.model", validate_known_component},
        {"render.sphere", validate_known_component},
        {"render.sprite", validate_known_component},
        {"status_effect.timer", validate_status_effect_timer_component_handler},
        {"viewmodel.bob", validate_known_component},
        {"weapon.projectile", validate_weapon_projectile_component_handler},
        {"weapon.state", validate_weapon_state_component_handler},
        {NULL, NULL},
    };
    return handlers;
}

static const component_validation_handler *find_component_validation_handler(const char *type)
{
    const component_validation_handler *handlers = component_validation_handlers();
    for (size_t i = 0; handlers[i].type != NULL; ++i)
    {
        if (SDL_strcmp(type, handlers[i].type) == 0)
            return &handlers[i];
    }
    return NULL;
}

bool validate_components(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *entities = obj_get(root, "entities");
    for (size_t e = 0; yyjson_is_arr(entities) && e < yyjson_arr_size(entities); ++e)
    {
        yyjson_val *entity = yyjson_arr_get(entities, e);
        yyjson_val *components = obj_get(entity, "components");
        if (components == NULL)
            continue;
        if (!yyjson_is_arr(components))
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.entities[%zu].components", e);
            return validation_error(ctx, path, "entity components must be an array");
        }

        for (size_t c = 0; c < yyjson_arr_size(components); ++c)
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.entities[%zu].components[%zu]", e, c);
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type");
            if (type == NULL || type[0] == '\0')
                return validation_error(ctx, path, "component requires a non-empty type");
            const component_validation_handler *handler = find_component_validation_handler(type);
            if (handler == NULL)
            {
                if (!validation_warning(ctx, path, "unsupported component type '%s'", type))
                    return false;
                continue;
            }
            if (!handler->validate(ctx, component, path, names, type))
                return false;
        }
    }
    return true;
}

/**
 * @file game_data_validation_actors.c
 * @brief Actor archetype and actor pool validation.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

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

bool validate_actor_archetypes_and_pools(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *archetypes = obj_get(root, "actor_archetypes");
    for (size_t i = 0; yyjson_is_arr(archetypes) && i < yyjson_arr_size(archetypes); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_archetypes[%zu]", i);
        yyjson_val *archetype = yyjson_arr_get(archetypes, i);
        yyjson_val *components = obj_get(archetype, "components");
        if (components != NULL && !yyjson_is_arr(components))
            return validation_error(ctx, path, "actor archetype components must be an array");
        for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
        {
            char component_path[PATH_BUFFER_SIZE];
            format_path(component_path, sizeof(component_path), "%s.components[%zu]", path, c);
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type");
            if (type == NULL || type[0] == '\0')
                return validation_error(ctx, component_path, "component requires a non-empty type");
            if (!is_supported_component_type(type) &&
                !validation_warning(ctx, component_path, "unsupported component type '%s'", type))
            {
                return false;
            }
            if (SDL_strcmp(type, "controller.fps_sector") == 0 || SDL_strcmp(type, "controller.fps_brush") == 0 ||
                SDL_strcmp(type, "controller.editor_camera") == 0)
            {
                return validation_error(ctx, component_path,
                                        "camera and first-person controllers are only supported on static entities");
            }
            else if (SDL_strcmp(type, "combat.health") == 0)
            {
                if (!validate_combat_health_component(ctx, component, component_path))
                    return false;
            }
            else if (SDL_strcmp(type, "pickup.respawn") == 0)
            {
                if (!validate_pickup_respawn_component(ctx, component, component_path))
                    return false;
            }
            else if (SDL_strcmp(type, "status_effect.timer") == 0)
            {
                if (!validate_status_effect_timer_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "weapon.state") == 0)
            {
                if (!validate_weapon_state_component(ctx, component, component_path))
                    return false;
            }
            else if (SDL_strcmp(type, "interactable") == 0)
            {
                if (!validate_interactable_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "weapon.projectile") == 0)
            {
                if (!require_ref(ctx, &names->actions, "input action", json_string(component, "action"),
                                 component_path) ||
                    !validate_projectile_fire_shape(ctx, component, component_path, names, false))
                {
                    return false;
                }
            }
            else if (SDL_strcmp(type, "particles.emitter") == 0)
            {
                if (!validate_particle_emitter_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "motion.grid_agent") == 0)
            {
                if (!require_ref(ctx, &names->grid_maps, "grid map", json_string(component, "map"), component_path))
                    return false;
                yyjson_val *speed = obj_get(component, "speed");
                if (speed != NULL && (!yyjson_is_num(speed) || yyjson_get_num(speed) < 0.0))
                    return validation_error(ctx, component_path,
                                            "motion.grid_agent speed must be a non-negative number");
            }
            else if (SDL_strcmp(type, "motion.velocity_2d") == 0 || SDL_strcmp(type, "motion.velocity_3d") == 0)
            {
                yyjson_val *property = obj_get(component, "property");
                if (property != NULL && !is_non_empty_string(component, "property"))
                    return validation_error(ctx, component_path, "%s property must be non-empty", type);
            }
            else if (SDL_strcmp(type, "motion.brush_velocity_3d") == 0)
            {
                if (!validate_brush_velocity_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "motion.sector_velocity_3d") == 0)
            {
                if (!require_ref(ctx, &names->sector_levels, "sector level", json_string(component, "sector_level"),
                                 component_path))
                    return false;
                yyjson_val *property = obj_get(component, "property");
                if (property != NULL && !is_non_empty_string(component, "property"))
                    return validation_error(ctx, component_path,
                                            "motion.sector_velocity_3d property must be non-empty");
                yyjson_val *despawn_on_hit = obj_get(component, "despawn_on_hit");
                if (despawn_on_hit != NULL && !yyjson_is_bool(despawn_on_hit))
                    return validation_error(ctx, component_path,
                                            "motion.sector_velocity_3d despawn_on_hit must be a boolean");
                yyjson_val *reason = obj_get(component, "reason");
                if (reason != NULL && !is_non_empty_string(component, "reason"))
                    return validation_error(ctx, component_path, "motion.sector_velocity_3d reason must be non-empty");
            }
            else if (SDL_strcmp(type, "lifecycle.ttl") == 0)
            {
                yyjson_val *ttl = obj_get(component, "ttl");
                if (ttl != NULL && (!yyjson_is_num(ttl) || yyjson_get_num(ttl) <= 0.0))
                    return validation_error(ctx, component_path, "lifecycle.ttl ttl must be positive");
                yyjson_val *age_property = obj_get(component, "age_property");
                yyjson_val *ttl_property = obj_get(component, "ttl_property");
                yyjson_val *reason = obj_get(component, "reason");
                if ((age_property != NULL && !is_non_empty_string(component, "age_property")) ||
                    (ttl_property != NULL && !is_non_empty_string(component, "ttl_property")) ||
                    (reason != NULL && !is_non_empty_string(component, "reason")))
                {
                    return validation_error(ctx, component_path,
                                            "lifecycle.ttl property names and reason must be non-empty strings");
                }
            }
            else if (SDL_strncmp(type, "light.", 6) == 0)
            {
                yyjson_val *color = obj_get(component, "color");
                if (color != NULL && !is_vec_array(color, 3))
                    return validation_error(ctx, component_path, "light component color must be a vec3");
                yyjson_val *enabled = obj_get(component, "enabled");
                if (enabled != NULL && !yyjson_is_bool(enabled))
                    return validation_error(ctx, component_path, "light component enabled must be a boolean");
                yyjson_val *enabled_key = obj_get(component, "enabled_key");
                if (enabled_key != NULL && !is_non_empty_string(component, "enabled_key"))
                    return validation_error(ctx, component_path, "light component enabled_key must be non-empty");
            }
            else if (SDL_strcmp(type, "render.cube") == 0)
            {
                yyjson_val *lighting = obj_get(component, "lighting");
                if (lighting != NULL && !yyjson_is_bool(lighting))
                    return validation_error(ctx, component_path, "render primitive lighting must be a boolean");
                yyjson_val *lighting_key = obj_get(component, "lighting_key");
                if (lighting_key != NULL && !is_non_empty_string(component, "lighting_key"))
                    return validation_error(ctx, component_path, "render primitive lighting_key must be non-empty");
                if (!validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "hidden_from_cameras"))
                {
                    return false;
                }
                yyjson_val *size = obj_get(component, "size");
                if (size != NULL && !is_vec_array(size, 3))
                    return validation_error(ctx, component_path, "render.cube size must be a vec3");
                yyjson_val *size_property = obj_get(component, "size_property");
                if (size_property != NULL && !is_non_empty_string(component, "size_property"))
                    return validation_error(ctx, component_path, "render.cube size_property must be non-empty");
                yyjson_val *texture_value = obj_get(component, "texture");
                if (texture_value != NULL && !is_non_empty_string(component, "texture"))
                    return validation_error(ctx, component_path,
                                            "render.cube texture must be a non-empty image asset id");
                const char *texture = json_string(component, "texture");
                if (texture != NULL && !require_ref(ctx, &names->images, "image asset", texture, component_path))
                    return false;
            }
            else if (SDL_strcmp(type, "render.mesh_primitive") == 0)
            {
                if (!validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "hidden_from_cameras"))
                {
                    return false;
                }
                if (!validate_render_mesh_primitive_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "render.composite") == 0)
            {
                if (!validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "hidden_from_cameras"))
                {
                    return false;
                }
                if (!validate_render_composite_component(ctx, component, component_path, names))
                    return false;
            }
            else if (SDL_strcmp(type, "render.sprite") == 0)
            {
                if (!validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "hidden_from_cameras"))
                {
                    return false;
                }
                if (!require_ref(ctx, &names->sprites, "sprite asset", json_string(component, "sprite"),
                                 component_path))
                    return false;
                yyjson_val *size = obj_get(component, "size");
                if (size != NULL && !is_vec_array(size, 2))
                    return validation_error(ctx, component_path, "render.sprite size must be a vec2");
                yyjson_val *facing_yaw = obj_get(component, "facing_yaw");
                if (facing_yaw != NULL && !yyjson_is_num(facing_yaw))
                    return validation_error(ctx, component_path, "render.sprite facing_yaw must be a number");
                yyjson_val *facing_yaw_property = obj_get(component, "facing_yaw_property");
                if (facing_yaw_property != NULL && !is_non_empty_string(component, "facing_yaw_property"))
                    return validation_error(ctx, component_path, "render.sprite facing_yaw_property must be non-empty");
            }
            else if (SDL_strcmp(type, "render.model") == 0)
            {
                if (!validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "visible_to_cameras") ||
                    !validate_render_camera_visibility_field(ctx, component, component_path, names,
                                                             "hidden_from_cameras"))
                {
                    return false;
                }
                if (!require_ref(ctx, &names->models, "model asset", json_string(component, "model"), component_path))
                    return false;
                yyjson_val *scale = obj_get(component, "scale");
                if (scale != NULL && !is_vec_array(scale, 3))
                    return validation_error(ctx, component_path, "render.model scale must be a vec3");
                yyjson_val *space = obj_get(component, "space");
                if (space != NULL && (!yyjson_is_str(space) || (SDL_strcmp(yyjson_get_str(space), "world") != 0 &&
                                                                SDL_strcmp(yyjson_get_str(space), "camera") != 0)))
                    return validation_error(ctx, component_path, "render.model space must be 'world' or 'camera'");
                yyjson_val *rotation = obj_get(component, "rotation");
                if (rotation != NULL && !is_vec_array(rotation, 3))
                    return validation_error(ctx, component_path, "render.model rotation must be a vec3");
                yyjson_val *lod_cull_pixels = obj_get(component, "lod_cull_pixels");
                if (lod_cull_pixels != NULL &&
                    (!yyjson_is_num(lod_cull_pixels) || yyjson_get_num(lod_cull_pixels) < 0.0))
                    return validation_error(ctx, component_path,
                                            "render.model lod_cull_pixels must be a non-negative number");
                const char *property_fields[] = {
                    "scale_property",       "pitch_property",    "yaw_property",          "roll_property",
                    "pitch_add_property",   "yaw_add_property",  "roll_add_property",     "offset_x_property",
                    "offset_y_property",    "offset_z_property", "offset_x_add_property", "offset_y_add_property",
                    "offset_z_add_property"};
                for (size_t property_index = 0; property_index < SDL_arraysize(property_fields); ++property_index)
                {
                    yyjson_val *property = obj_get(component, property_fields[property_index]);
                    if (property != NULL && !is_non_empty_string(component, property_fields[property_index]))
                        return validation_error(ctx, component_path, "render.model property fields must be non-empty");
                }
                const char *property_arrays[] = {"offset_x_add_properties", "offset_y_add_properties",
                                                 "offset_z_add_properties", "pitch_add_properties",
                                                 "yaw_add_properties",      "roll_add_properties"};
                for (size_t property_index = 0; property_index < SDL_arraysize(property_arrays); ++property_index)
                {
                    if (!validate_property_name_array_field(ctx, component, component_path,
                                                            property_arrays[property_index],
                                                            "render.model property arrays"))
                        return false;
                }
                yyjson_val *animation_clip = obj_get(component, "animation_clip");
                if (animation_clip != NULL && (!yyjson_is_int(animation_clip) || yyjson_get_int(animation_clip) < 0))
                    return validation_error(ctx, component_path,
                                            "render.model animation_clip must be a non-negative integer");
                yyjson_val *animation_time = obj_get(component, "animation_time");
                if (animation_time != NULL && !yyjson_is_num(animation_time))
                    return validation_error(ctx, component_path, "render.model animation_time must be a number");
                yyjson_val *animation_time_property = obj_get(component, "animation_time_property");
                if (animation_time_property != NULL && !is_non_empty_string(component, "animation_time_property"))
                    return validation_error(ctx, component_path,
                                            "render.model animation_time_property must be non-empty");
                yyjson_val *animation_loop = obj_get(component, "animation_loop");
                if (animation_loop != NULL && !yyjson_is_bool(animation_loop))
                    return validation_error(ctx, component_path, "render.model animation_loop must be a boolean");
            }
            else if (SDL_strcmp(type, "viewmodel.bob") == 0)
            {
                if (!require_actor_ref(ctx, names, json_string(component, "source"), component_path))
                    return false;
                const char *property_fields[] = {
                    "previous_position_property", "phase_property", "offset_x_property", "offset_y_property",
                    "offset_z_property",          "pitch_property", "yaw_property",      "roll_property"};
                for (size_t property_index = 0; property_index < SDL_arraysize(property_fields); ++property_index)
                {
                    yyjson_val *property = obj_get(component, property_fields[property_index]);
                    if (property != NULL && !is_non_empty_string(component, property_fields[property_index]))
                        return validation_error(ctx, component_path, "viewmodel.bob property fields must be non-empty");
                }
                yyjson_val *offset_amplitude = obj_get(component, "offset_amplitude");
                if (offset_amplitude != NULL && !is_vec_array(offset_amplitude, 3))
                    return validation_error(ctx, component_path, "viewmodel.bob offset_amplitude must be a vec3");
                const char *non_negative[] = {"frequency", "speed_scale", "min_speed", "settle_rate"};
                for (size_t bob_tuning_index = 0; bob_tuning_index < SDL_arraysize(non_negative); ++bob_tuning_index)
                {
                    yyjson_val *value = obj_get(component, non_negative[bob_tuning_index]);
                    if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0))
                        return validation_error(ctx, component_path,
                                                "viewmodel.bob numeric tuning values must be non-negative");
                }
                const char *numeric[] = {"pitch_amplitude", "yaw_amplitude", "roll_amplitude"};
                for (size_t bob_tuning_index = 0; bob_tuning_index < SDL_arraysize(numeric); ++bob_tuning_index)
                {
                    yyjson_val *value = obj_get(component, numeric[bob_tuning_index]);
                    if (value != NULL && !yyjson_is_num(value))
                        return validation_error(ctx, component_path,
                                                "viewmodel.bob angular amplitudes must be numbers");
                }
            }
        }
    }

    yyjson_val *pools = obj_get(root, "actor_pools");
    for (size_t i = 0; yyjson_is_arr(pools) && i < yyjson_arr_size(pools); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_pools[%zu]", i);
        yyjson_val *pool = yyjson_arr_get(pools, i);
        if (!require_ref(ctx, &names->actor_archetypes, "actor archetype", json_string(pool, "archetype"), path))
            return false;
        yyjson_val *capacity = obj_get(pool, "capacity");
        if (!yyjson_is_int(capacity) || yyjson_get_int(capacity) <= 0 || yyjson_get_int(capacity) > 4096)
            return validation_error(ctx, path, "actor pool capacity must be an integer in 1..4096");
        const char *scene = json_string(pool, "scene");
        yyjson_val *scenes = obj_get(pool, "scenes");
        if (scene != NULL && scenes != NULL)
            return validation_error(ctx, path, "actor pool must use either scene or scenes, not both");
        if (scene != NULL && !require_ref(ctx, &names->scenes, "scene", scene, path))
            return false;
        if (scenes != NULL)
        {
            if (!yyjson_is_arr(scenes) || yyjson_arr_size(scenes) <= 0)
                return validation_error(ctx, path, "actor pool scenes must be a non-empty array");
            for (size_t scene_index = 0; scene_index < yyjson_arr_size(scenes); ++scene_index)
            {
                char scene_path[PATH_BUFFER_SIZE];
                format_path(scene_path, sizeof(scene_path), "$.actor_pools[%zu].scenes[%zu]", i, scene_index);
                yyjson_val *scene_value = yyjson_arr_get(scenes, scene_index);
                if (!yyjson_is_str(scene_value) || yyjson_get_str(scene_value)[0] == '\0')
                    return validation_error(ctx, scene_path, "actor pool scenes entries must be non-empty strings");
                if (!require_ref(ctx, &names->scenes, "scene", yyjson_get_str(scene_value), scene_path))
                    return false;
            }
        }
        const char *policy = json_string(pool, "on_exhausted");
        if (policy != NULL && SDL_strcmp(policy, "fail") != 0 && SDL_strcmp(policy, "reuse_oldest") != 0)
            return validation_error(ctx, path, "actor pool on_exhausted must be fail or reuse_oldest");
        const char *scene_policy = json_string(pool, "on_scene_exit");
        if (scene_policy != NULL && SDL_strcmp(scene_policy, "reset") != 0 &&
            SDL_strcmp(scene_policy, "despawn") != 0 && SDL_strcmp(scene_policy, "preserve") != 0)
        {
            return validation_error(ctx, path, "actor pool on_scene_exit must be reset, despawn, or preserve");
        }
    }
    return true;
}

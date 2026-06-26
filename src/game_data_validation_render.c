/**
 * @file game_data_validation_render.c
 * @brief Render settings, light, effect, and transition JSON game data validation.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/lighting.h"

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

bool validate_render_effects(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *entities = obj_get(root, "entities");
    for (size_t e = 0; yyjson_is_arr(entities) && e < yyjson_arr_size(entities); ++e)
    {
        yyjson_val *components = obj_get(yyjson_arr_get(entities, e), "components");
        for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *effects = obj_get(yyjson_arr_get(components, c), "effects");
            for (size_t i = 0; yyjson_is_arr(effects) && i < yyjson_arr_size(effects); ++i)
            {
                char path[PATH_BUFFER_SIZE];
                format_path(path, sizeof(path), "$.entities[%zu].components[%zu].effects[%zu]", e, c, i);
                yyjson_val *effect = yyjson_arr_get(effects, i);
                const char *type = json_string(effect, "type");
                if (SDL_strcmp(type != NULL ? type : "", "flash") == 0)
                {
                    if (!require_ref(ctx, &names->entities, "entity", json_string(effect, "source"), path))
                        return false;
                    if (!is_non_empty_string(effect, "property"))
                        return validation_error(ctx, path, "flash effect requires a non-empty property");
                }
                else if (SDL_strcmp(type != NULL ? type : "", "pulse") != 0 &&
                         SDL_strcmp(type != NULL ? type : "", "drift") != 0 &&
                         SDL_strcmp(type != NULL ? type : "", "emissive") != 0)
                {
                    return validation_error(ctx, path, "unsupported render effect type '%s'",
                                            type != NULL ? type : "<missing>");
                }
            }
        }
    }
    return true;
}

bool validate_lights(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *lights = obj_get(obj_get(root, "world"), "lights");
    if (lights == NULL)
        return true;
    if (!yyjson_is_arr(lights))
        return validation_error(ctx, "$.world.lights", "world lights must be an array");

    for (size_t i = 0; i < yyjson_arr_size(lights); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.world.lights[%zu]", i);
        yyjson_val *light = yyjson_arr_get(lights, i);
        yyjson_val *enabled = obj_get(light, "enabled");
        if (enabled != NULL && !yyjson_is_bool(enabled))
            return validation_error(ctx, path, "light enabled must be a boolean");
        yyjson_val *enabled_key = obj_get(light, "enabled_key");
        if (enabled_key != NULL && !is_non_empty_string(light, "enabled_key"))
            return validation_error(ctx, path, "light enabled_key must be non-empty");
        const char *target_entity = json_string(light, "target_entity");
        if (target_entity != NULL && !require_ref(ctx, &names->entities, "entity", target_entity, path))
            return false;
        yyjson_val *target_entities = obj_get(light, "target_entities");
        if (target_entities != NULL && !yyjson_is_arr(target_entities))
            return validation_error(ctx, path, "light target_entities must be an array");
        for (size_t target_index = 0; yyjson_is_arr(target_entities) && target_index < yyjson_arr_size(target_entities);
             ++target_index)
        {
            yyjson_val *target = yyjson_arr_get(target_entities, target_index);
            if (!yyjson_is_str(target))
                return validation_error(ctx, path, "light target_entities entries must be entity names");
            if (!require_ref(ctx, &names->entities, "entity", yyjson_get_str(target), path))
                return false;
        }

        yyjson_val *effects = obj_get(light, "effects");
        for (size_t e = 0; yyjson_is_arr(effects) && e < yyjson_arr_size(effects); ++e)
        {
            char effect_path[PATH_BUFFER_SIZE];
            format_path(effect_path, sizeof(effect_path), "%s.effects[%zu]", path, e);
            yyjson_val *effect = yyjson_arr_get(effects, e);
            const char *type = json_string(effect, "type");
            if (SDL_strcmp(type != NULL ? type : "", "flash") == 0)
            {
                if (!require_ref(ctx, &names->entities, "entity", json_string(effect, "source"), effect_path))
                    return false;
                if (!is_non_empty_string(effect, "property"))
                    return validation_error(ctx, effect_path, "light flash effect requires a non-empty property");
            }
            else if (SDL_strcmp(type != NULL ? type : "", "color_cycle") == 0)
            {
                yyjson_val *colors = obj_get(effect, "colors");
                if (!yyjson_is_arr(colors) || yyjson_arr_size(colors) < 2)
                    return validation_error(ctx, effect_path, "light color_cycle effect requires at least two colors");
                for (size_t color_index = 0; color_index < yyjson_arr_size(colors); ++color_index)
                {
                    if (!is_vec_array(yyjson_arr_get(colors, color_index), 3))
                        return validation_error(ctx, effect_path, "light color_cycle colors must be vec3 arrays");
                }
            }
            else if (SDL_strcmp(type != NULL ? type : "", "rotate_direction") == 0)
            {
                yyjson_val *axis = obj_get(effect, "axis");
                if (axis != NULL && !is_vec_array(axis, 3))
                    return validation_error(ctx, effect_path, "light rotate_direction axis must be a vec3 array");
            }
            else if (SDL_strcmp(type != NULL ? type : "", "pulse") != 0)
            {
                return validation_error(ctx, effect_path, "unsupported light effect type '%s'",
                                        type != NULL ? type : "<missing>");
            }
        }
    }
    return true;
}

bool validate_transitions(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *transitions = obj_get(root, "transitions");
    if (transitions == NULL)
        return true;
    if (!yyjson_is_obj(transitions))
        return validation_error(ctx, "$.transitions", "transitions must be an object");

    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(transitions, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        yyjson_val *transition = yyjson_obj_iter_get_val(key);
        const char *name = yyjson_get_str(key);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.transitions.%s", name != NULL ? name : "<unknown>");
        if (!yyjson_is_obj(transition))
            return validation_error(ctx, path, "transition entries must be objects");
        const char *done_signal = json_string(transition, "done_signal");
        if (done_signal != NULL && !require_ref(ctx, &names->signals, "signal", done_signal, path))
            return false;
    }
    return true;
}

static bool valid_render_profile_name(const char *name)
{
    return name != NULL &&
           (SDL_strcasecmp(name, "modern") == 0 || SDL_strcasecmp(name, "ps1") == 0 ||
            SDL_strcasecmp(name, "n64") == 0 || SDL_strcasecmp(name, "dos") == 0 || SDL_strcasecmp(name, "snes") == 0 ||
            SDL_strcasecmp(name, "grayscale") == 0 || SDL_strcasecmp(name, "gameboy") == 0);
}

static bool valid_tonemap_name(const char *name)
{
    return name != NULL && (SDL_strcasecmp(name, "none") == 0 || SDL_strcasecmp(name, "reinhard") == 0 ||
                            SDL_strcasecmp(name, "aces") == 0);
}

static bool validate_render_tunable_values(validation_context *ctx, yyjson_val *object, const char *path)
{
    yyjson_val *lighting = obj_get(object, "lighting");
    yyjson_val *bloom = obj_get(object, "bloom");
    yyjson_val *ssao = obj_get(object, "ssao");
    yyjson_val *depth_prepass = obj_get(object, "depth_prepass");
    yyjson_val *per_object_light_selection = obj_get(object, "per_object_light_selection");
    yyjson_val *procedural_lod = obj_get(object, "procedural_lod");
    yyjson_val *model_lod_culling = obj_get(object, "model_lod_culling");
    yyjson_val *performance_queries = obj_get(object, "performance_queries");
    yyjson_val *dynamic_world_render_scale = obj_get(object, "dynamic_world_render_scale");
    if ((lighting != NULL && !yyjson_is_bool(lighting)) || (bloom != NULL && !yyjson_is_bool(bloom)) ||
        (ssao != NULL && !yyjson_is_bool(ssao)) || (depth_prepass != NULL && !yyjson_is_bool(depth_prepass)) ||
        (per_object_light_selection != NULL && !yyjson_is_bool(per_object_light_selection)) ||
        (procedural_lod != NULL && !yyjson_is_bool(procedural_lod)) ||
        (model_lod_culling != NULL && !yyjson_is_bool(model_lod_culling)) ||
        (performance_queries != NULL && !yyjson_is_bool(performance_queries)) ||
        (dynamic_world_render_scale != NULL && !yyjson_is_bool(dynamic_world_render_scale)))
    {
        return validation_error(ctx, path,
                                "render lighting, bloom, ssao, depth_prepass, per_object_light_selection, "
                                "procedural_lod, model_lod_culling, performance_queries, and "
                                "dynamic_world_render_scale must be booleans");
    }
    yyjson_val *per_object_light_limit = obj_get(object, "per_object_light_limit");
    if (per_object_light_limit != NULL &&
        (!yyjson_is_int(per_object_light_limit) || yyjson_get_int(per_object_light_limit) < 0 ||
         yyjson_get_int(per_object_light_limit) > SLAYER3D_MAX_SHADER_LIGHTS))
    {
        return validation_error(ctx, path, "render per_object_light_limit must be an integer from 0 to %d",
                                SLAYER3D_MAX_SHADER_LIGHTS);
    }
    yyjson_val *world_render_scale = obj_get(object, "world_render_scale");
    if (world_render_scale != NULL &&
        (!yyjson_is_num(world_render_scale) || yyjson_get_num(world_render_scale) < 0.25 ||
         yyjson_get_num(world_render_scale) > 1.0))
    {
        return validation_error(ctx, path, "render world_render_scale must be a number from 0.25 to 1.0");
    }
    yyjson_val *dynamic_world_render_min_scale = obj_get(object, "dynamic_world_render_min_scale");
    yyjson_val *dynamic_world_render_max_scale = obj_get(object, "dynamic_world_render_max_scale");
    yyjson_val *dynamic_world_render_target_fps = obj_get(object, "dynamic_world_render_target_fps");
    if ((dynamic_world_render_min_scale != NULL &&
         (!yyjson_is_num(dynamic_world_render_min_scale) || yyjson_get_num(dynamic_world_render_min_scale) < 0.25 ||
          yyjson_get_num(dynamic_world_render_min_scale) > 1.0)) ||
        (dynamic_world_render_max_scale != NULL &&
         (!yyjson_is_num(dynamic_world_render_max_scale) || yyjson_get_num(dynamic_world_render_max_scale) < 0.25 ||
          yyjson_get_num(dynamic_world_render_max_scale) > 1.0)))
    {
        return validation_error(ctx, path,
                                "render dynamic_world_render_min_scale and dynamic_world_render_max_scale must be "
                                "numbers from 0.25 to 1.0");
    }
    if (dynamic_world_render_min_scale != NULL && dynamic_world_render_max_scale != NULL &&
        yyjson_get_num(dynamic_world_render_min_scale) > yyjson_get_num(dynamic_world_render_max_scale))
    {
        return validation_error(ctx, path,
                                "render dynamic_world_render_min_scale must be less than or equal to "
                                "dynamic_world_render_max_scale");
    }
    if (dynamic_world_render_target_fps != NULL &&
        (!yyjson_is_num(dynamic_world_render_target_fps) || yyjson_get_num(dynamic_world_render_target_fps) < 15.0 ||
         yyjson_get_num(dynamic_world_render_target_fps) > 500.0))
    {
        return validation_error(ctx, path, "render dynamic_world_render_target_fps must be a number from 15 to 500");
    }
    yyjson_val *procedural_lod_near_pixels = obj_get(object, "procedural_lod_near_pixels");
    yyjson_val *procedural_lod_far_pixels = obj_get(object, "procedural_lod_far_pixels");
    if ((procedural_lod_near_pixels != NULL &&
         (!yyjson_is_num(procedural_lod_near_pixels) || yyjson_get_num(procedural_lod_near_pixels) <= 0.0)) ||
        (procedural_lod_far_pixels != NULL &&
         (!yyjson_is_num(procedural_lod_far_pixels) || yyjson_get_num(procedural_lod_far_pixels) <= 0.0)))
    {
        return validation_error(ctx, path, "render procedural_lod pixel thresholds must be positive numbers");
    }
    if (procedural_lod_near_pixels != NULL && procedural_lod_far_pixels != NULL &&
        yyjson_get_num(procedural_lod_far_pixels) > yyjson_get_num(procedural_lod_near_pixels))
    {
        return validation_error(ctx, path,
                                "render procedural_lod_far_pixels must be less than or equal to "
                                "procedural_lod_near_pixels");
    }
    yyjson_val *procedural_lod_min_segments = obj_get(object, "procedural_lod_min_segments");
    if (procedural_lod_min_segments != NULL &&
        (!yyjson_is_int(procedural_lod_min_segments) || yyjson_get_int(procedural_lod_min_segments) < 3 ||
         yyjson_get_int(procedural_lod_min_segments) > 64))
    {
        return validation_error(ctx, path, "render procedural_lod_min_segments must be an integer from 3 to 64");
    }
    yyjson_val *model_lod_cull_pixels = obj_get(object, "model_lod_cull_pixels");
    if (model_lod_cull_pixels != NULL &&
        (!yyjson_is_num(model_lod_cull_pixels) || yyjson_get_num(model_lod_cull_pixels) < 0.0))
    {
        return validation_error(ctx, path, "render model_lod_cull_pixels must be a non-negative number");
    }
    return true;
}

bool validate_render_settings(validation_context *ctx, yyjson_val *root)
{
    yyjson_val *render = obj_get(root, "render");
    if (render == NULL)
        return true;
    if (!yyjson_is_obj(render))
        return validation_error(ctx, "$.render", "render must be an object");

    if (!validate_render_tunable_values(ctx, render, "$.render"))
        return false;
    if (obj_get(render, "clear_color") != NULL && !is_vec_array(obj_get(render, "clear_color"), 3))
        return validation_error(ctx, "$.render.clear_color", "render clear_color must be a vec3 or vec4 color");
    const char *tonemap = json_string(render, "tonemap");
    if (tonemap != NULL && !valid_tonemap_name(tonemap))
        return validation_error(ctx, "$.render.tonemap", "render tonemap must be none, reinhard, or aces");
    const char *profile = json_string(render, "profile");
    if (profile != NULL && !valid_render_profile_name(profile))
        return validation_error(ctx, "$.render.profile", "render profile is unknown");

    const char *key_fields[] = {"lighting_key",
                                "bloom_key",
                                "ssao_key",
                                "depth_prepass_key",
                                "tonemap_key",
                                "profile_key",
                                "quality_key",
                                "performance_queries_key",
                                "world_render_scale_key",
                                "per_object_light_selection_key",
                                "per_object_light_limit_key",
                                "procedural_lod_key",
                                "procedural_lod_near_pixels_key",
                                "procedural_lod_far_pixels_key",
                                "procedural_lod_min_segments_key",
                                "model_lod_culling_key",
                                "model_lod_cull_pixels_key"};
    for (size_t i = 0; i < SDL_arraysize(key_fields); ++i)
    {
        if (obj_get(render, key_fields[i]) != NULL && !is_non_empty_string(render, key_fields[i]))
            return validation_error(ctx, "$.render", "render scene-state key fields must be non-empty strings");
    }
    yyjson_val *quality_presets = obj_get(render, "quality_presets");
    if (quality_presets != NULL && !yyjson_is_arr(quality_presets))
        return validation_error(ctx, "$.render.quality_presets", "render quality_presets must be an array");
    if (obj_get(render, "quality") != NULL && !is_non_empty_string(render, "quality"))
        return validation_error(ctx, "$.render.quality", "render quality must be a non-empty string");
    name_table quality_names;
    SDL_zero(quality_names);
    bool ok = true;
    for (size_t i = 0; ok && yyjson_is_arr(quality_presets) && i < yyjson_arr_size(quality_presets); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.render.quality_presets[%zu]", i);
        yyjson_val *preset = yyjson_arr_get(quality_presets, i);
        if (!yyjson_is_obj(preset))
        {
            ok = validation_error(ctx, path, "render quality preset must be an object");
            break;
        }
        ok = require_unique_name(ctx, &quality_names, "render quality preset", json_string(preset, "name"), path) &&
             (obj_get(preset, "label") == NULL || is_non_empty_string(preset, "label") ||
              validation_error(ctx, path, "render quality preset label must be a non-empty string")) &&
             validate_render_tunable_values(ctx, preset, path);
    }
    const char *quality = json_string(render, "quality");
    if (ok && quality != NULL && !name_table_contains(&quality_names, quality))
        ok = validation_error(ctx, "$.render.quality", "unknown render quality preset '%s'", quality);
    name_table_destroy(&quality_names);
    if (!ok)
        return false;
    return true;
}

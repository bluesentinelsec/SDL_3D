/**
 * @file game_data_validation_render_components.c
 * @brief Validation helpers for authored render components.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL.h>

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

static bool render_mesh_primitive_kind_valid(const char *primitive)
{
    const char *known[] = {"cube",        "sphere",       "capsule", "cylinder", "cone",           "torus",
                           "pyramid",     "wedge",        "plane",   "quad",     "disc",           "hemisphere",
                           "rounded_box", "tube_segment", "pipe",    "arrow",    "billboard_plane"};
    for (size_t i = 0; primitive != NULL && i < SDL_arraysize(known); ++i)
    {
        if (SDL_strcmp(primitive, known[i]) == 0)
            return true;
    }
    return false;
}

static bool render_draw_mode_valid(const char *draw_mode)
{
    return draw_mode != NULL && (SDL_strcmp(draw_mode, "solid") == 0 || SDL_strcmp(draw_mode, "wire") == 0 ||
                                 SDL_strcmp(draw_mode, "solid_wire") == 0);
}

bool validate_render_mesh_primitive_component(validation_context *ctx, yyjson_val *component, const char *path,
                                              const validation_names *names)
{
    const char *primitive = json_string(component, "primitive");
    if (!render_mesh_primitive_kind_valid(primitive))
        return validation_error(ctx, path, "render.mesh_primitive primitive is unknown");
    yyjson_val *draw_mode = obj_get(component, "draw_mode");
    if (draw_mode != NULL && (!yyjson_is_str(draw_mode) || !render_draw_mode_valid(yyjson_get_str(draw_mode))))
        return validation_error(ctx, path, "render.mesh_primitive draw_mode is unknown");
    yyjson_val *texture_value = obj_get(component, "texture");
    if (texture_value != NULL && !is_non_empty_string(component, "texture"))
        return validation_error(ctx, path, "render.mesh_primitive texture must be a non-empty image asset id");
    const char *texture = json_string(component, "texture");
    if (texture != NULL && !require_ref(ctx, &names->images, "image asset", texture, path))
        return false;
    yyjson_val *wire_color = obj_get(component, "wire_color");
    if (wire_color != NULL && !is_vec_array(wire_color, 3))
        return validation_error(ctx, path, "render.mesh_primitive wire_color must be a vec3 or vec4");
    yyjson_val *lighting = obj_get(component, "lighting");
    if (lighting != NULL && !yyjson_is_bool(lighting))
        return validation_error(ctx, path, "render.mesh_primitive lighting must be a boolean");
    yyjson_val *lighting_key = obj_get(component, "lighting_key");
    if (lighting_key != NULL && !is_non_empty_string(component, "lighting_key"))
        return validation_error(ctx, path, "render.mesh_primitive lighting_key must be non-empty");
    yyjson_val *lod = obj_get(component, "lod");
    if (lod != NULL && !yyjson_is_bool(lod))
        return validation_error(ctx, path, "render.mesh_primitive lod must be a boolean");
    yyjson_val *lod_bias = obj_get(component, "lod_bias");
    if (lod_bias != NULL && (!yyjson_is_num(lod_bias) || yyjson_get_num(lod_bias) <= 0.0))
        return validation_error(ctx, path, "render.mesh_primitive lod_bias must be a positive number");
    yyjson_val *space = obj_get(component, "space");
    if (space != NULL && (!yyjson_is_str(space) || (SDL_strcmp(yyjson_get_str(space), "world") != 0 &&
                                                    SDL_strcmp(yyjson_get_str(space), "camera") != 0)))
        return validation_error(ctx, path, "render.mesh_primitive space must be 'world' or 'camera'");
    yyjson_val *size = obj_get(component, "size");
    if (size != NULL && !is_vec_array(size, 3))
        return validation_error(ctx, path, "render.mesh_primitive size must be a vec3");
    const char *property_fields[] = {
        "size_property",          "radius_property",     "height_property",      "radius_top_property",
        "radius_bottom_property", "size_scale_property", "alpha_scale_property", "emissive_intensity_property"};
    for (size_t property_index = 0; property_index < SDL_arraysize(property_fields); ++property_index)
    {
        yyjson_val *property = obj_get(component, property_fields[property_index]);
        if (property != NULL && !is_non_empty_string(component, property_fields[property_index]))
            return validation_error(ctx, path, "render.mesh_primitive property fields must be non-empty");
    }
    yyjson_val *emissive_color = obj_get(component, "emissive_color");
    if (emissive_color != NULL && !is_vec_array(emissive_color, 3))
        return validation_error(ctx, path, "render.mesh_primitive emissive_color must be a vec3 or vec4");
    yyjson_val *emissive_intensity = obj_get(component, "emissive_intensity");
    if (emissive_intensity != NULL && (!yyjson_is_num(emissive_intensity) || yyjson_get_num(emissive_intensity) < 0.0))
        return validation_error(ctx, path, "render.mesh_primitive emissive_intensity must be non-negative");
    const char *positive_numbers[] = {"radius", "height", "major_radius", "minor_radius"};
    for (size_t i = 0; i < SDL_arraysize(positive_numbers); ++i)
    {
        yyjson_val *value = obj_get(component, positive_numbers[i]);
        if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) <= 0.0))
            return validation_error(ctx, path, "render.mesh_primitive dimensions must be positive numbers");
    }
    const char *non_negative_numbers[] = {"radius_top", "radius_bottom"};
    for (size_t i = 0; i < SDL_arraysize(non_negative_numbers); ++i)
    {
        yyjson_val *value = obj_get(component, non_negative_numbers[i]);
        if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0))
            return validation_error(ctx, path, "render.mesh_primitive radii must be non-negative numbers");
    }
    const char *positive_ints[] = {"segments", "slices", "rings", "tube_segments"};
    for (size_t i = 0; i < SDL_arraysize(positive_ints); ++i)
    {
        yyjson_val *value = obj_get(component, positive_ints[i]);
        if (value != NULL && (!yyjson_is_int(value) || yyjson_get_int(value) < 3))
            return validation_error(ctx, path, "render.mesh_primitive tessellation values must be integers >= 3");
    }
    yyjson_val *rotation_axis = obj_get(component, "rotation_axis");
    if (rotation_axis != NULL && !is_vec_array(rotation_axis, 3))
        return validation_error(ctx, path, "render.mesh_primitive rotation_axis must be a vec3");
    yyjson_val *rotation_angle = obj_get(component, "rotation_angle");
    if (rotation_angle != NULL && !yyjson_is_num(rotation_angle))
        return validation_error(ctx, path, "render.mesh_primitive rotation_angle must be a number");
    yyjson_val *rotation_property = obj_get(component, "rotation_property");
    if (rotation_property != NULL && !is_non_empty_string(component, "rotation_property"))
        return validation_error(ctx, path, "render.mesh_primitive rotation_property must be non-empty");
    yyjson_val *bevel_radius = obj_get(component, "bevel_radius");
    if (bevel_radius != NULL && (!yyjson_is_num(bevel_radius) || yyjson_get_num(bevel_radius) < 0.0))
        return validation_error(ctx, path, "render.mesh_primitive bevel_radius must be a non-negative number");
    yyjson_val *arc_angle = obj_get(component, "arc_angle");
    if (arc_angle != NULL && (!yyjson_is_num(arc_angle) || yyjson_get_num(arc_angle) <= 0.0))
        return validation_error(ctx, path, "render.mesh_primitive arc_angle must be a positive number");
    return true;
}

bool validate_render_composite_component(validation_context *ctx, yyjson_val *component, const char *path,
                                         const validation_names *names)
{
    yyjson_val *parts = obj_get(component, "parts");
    if (!yyjson_is_arr(parts) || yyjson_arr_size(parts) == 0)
        return validation_error(ctx, path, "render.composite parts must be a non-empty array");
    for (size_t i = 0; i < yyjson_arr_size(parts); ++i)
    {
        yyjson_val *part = yyjson_arr_get(parts, i);
        if (!yyjson_is_obj(part))
            return validation_error(ctx, path, "render.composite parts must be objects");
        const char *type = json_string(part, "type");
        if (type == NULL)
            type = "render.mesh_primitive";
        if (SDL_strcmp(type, "render.mesh_primitive") != 0)
            return validation_error(ctx, path, "render.composite parts must be render.mesh_primitive descriptors");
        if (!validate_render_mesh_primitive_component(ctx, part, path, names))
            return false;
    }
    return true;
}

bool validate_render_camera_visibility_field(validation_context *ctx, yyjson_val *component, const char *path,
                                             const validation_names *names, const char *field)
{
    yyjson_val *value = obj_get(component, field);
    if (value == NULL)
        return true;
    if (yyjson_is_str(value))
        return require_ref(ctx, &names->cameras, "camera", yyjson_get_str(value), path);
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) == 0)
        return validation_error(ctx, path, "camera visibility must be a string or non-empty array");
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (!yyjson_is_str(entry) || yyjson_get_str(entry)[0] == '\0')
            return validation_error(ctx, path, "camera visibility must contain camera names");
        if (!require_ref(ctx, &names->cameras, "camera", yyjson_get_str(entry), path))
            return false;
    }
    return true;
}

bool validate_property_name_array_field(validation_context *ctx, yyjson_val *component, const char *path,
                                        const char *field, const char *label)
{
    yyjson_val *value = obj_get(component, field);
    if (value == NULL)
        return true;
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) == 0)
        return validation_error(ctx, path, "%s must be a non-empty string array", label);
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (!yyjson_is_str(entry) || yyjson_get_str(entry)[0] == '\0')
            return validation_error(ctx, path, "%s arrays must contain non-empty strings", label);
    }
    return true;
}

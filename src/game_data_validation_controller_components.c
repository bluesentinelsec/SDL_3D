/**
 * @file game_data_validation_controller_components.c
 * @brief Validation helpers for authored controller components.
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

static bool validate_fps_controller_common(validation_context *ctx, yyjson_val *component, const char *path,
                                           validation_names *names, const char *type_name)
{
    yyjson_val *actions = obj_get(component, "actions");
    if (!yyjson_is_obj(actions))
        return validation_error(ctx, path, "%s requires an actions object", type_name);
    const char *action_keys[] = {"forward", "back", "left", "right"};
    for (size_t i = 0; i < SDL_arraysize(action_keys); ++i)
    {
        const char *action = json_string(actions, action_keys[i]);
        if (!require_ref(ctx, &names->actions, "input action", action, path))
            return false;
    }
    const char *jump = json_string(actions, "jump");
    if (jump != NULL && !require_ref(ctx, &names->actions, "input action", jump, path))
        return false;

    const char *property_keys[] = {"yaw_property",
                                   "pitch_property",
                                   "forward_property",
                                   "view_smooth_property",
                                   "vertical_velocity_property",
                                   "on_ground_property",
                                   "sector_property"};
    for (size_t i = 0; i < SDL_arraysize(property_keys); ++i)
    {
        yyjson_val *value = obj_get(component, property_keys[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, path, "%s property names must be non-empty strings", type_name);
    }

    const char *non_negative[] = {"move_speed",    "jump_velocity", "gravity",           "player_height",
                                  "player_radius", "step_height",   "ceiling_clearance", "mouse_sensitivity"};
    for (size_t i = 0; i < SDL_arraysize(non_negative); ++i)
    {
        yyjson_val *value = obj_get(component, non_negative[i]);
        if (value != NULL && (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0))
            return validation_error(ctx, path, "%s numeric tuning values must be non-negative", type_name);
    }
    yyjson_val *mouse_look = obj_get(component, "mouse_look");
    if (mouse_look != NULL && !yyjson_is_bool(mouse_look))
        return validation_error(ctx, path, "%s mouse_look must be a boolean", type_name);
    yyjson_val *spawn_yaw = obj_get(component, "spawn_yaw");
    yyjson_val *spawn_pitch = obj_get(component, "spawn_pitch");
    if ((spawn_yaw != NULL && !yyjson_is_num(spawn_yaw)) || (spawn_pitch != NULL && !yyjson_is_num(spawn_pitch)))
    {
        return validation_error(ctx, path, "%s spawn yaw and pitch values must be numbers", type_name);
    }
    return true;
}

bool validate_fps_sector_component(validation_context *ctx, yyjson_val *component, const char *path,
                                   validation_names *names)
{
    const char *level = json_string(component, "sector_level");
    return require_ref(ctx, &names->sector_levels, "sector level", level, path) &&
           validate_fps_controller_common(ctx, component, path, names, "controller.fps_sector");
}

bool validate_fps_brush_component(validation_context *ctx, yyjson_val *component, const char *path,
                                  validation_names *names)
{
    const char *world = json_string(component, "brush_world");
    if (!require_ref(ctx, &names->brush_worlds, "brush world", world, path))
        return false;
    char contents_path[PATH_BUFFER_SIZE];
    format_path(contents_path, sizeof(contents_path), "%s.contents_mask", path);
    if (!validate_brush_string_or_string_array(ctx, obj_get(component, "contents_mask"), contents_path, "brush content",
                                               brush_content_name_valid, false) ||
        !validate_fps_controller_common(ctx, component, path, names, "controller.fps_brush"))
    {
        return false;
    }
    yyjson_val *walkable = obj_get(component, "walkable_normal_y");
    if (walkable != NULL &&
        (!yyjson_is_num(walkable) || yyjson_get_num(walkable) < 0.0 || yyjson_get_num(walkable) > 1.0))
    {
        return validation_error(ctx, path, "controller.fps_brush walkable_normal_y must be in [0, 1]");
    }
    const char *property_keys[] = {"brush_collision_kind_property",
                                   "brush_collision_normal_property",
                                   "brush_collision_brush_property",
                                   "brush_collision_material_property",
                                   "brush_collision_contents_property",
                                   "brush_collision_surface_flags_property",
                                   "brush_floor_normal_property",
                                   "brush_floor_brush_property",
                                   "brush_step_up_property"};
    for (size_t i = 0; i < SDL_arraysize(property_keys); ++i)
    {
        yyjson_val *value = obj_get(component, property_keys[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, path,
                                    "controller.fps_brush diagnostic property names must be non-empty strings");
    }
    return true;
}

bool validate_editor_camera_component(validation_context *ctx, yyjson_val *component, const char *path,
                                      validation_names *names)
{
    yyjson_val *actions = obj_get(component, "actions");
    if (!yyjson_is_obj(actions))
        return validation_error(ctx, path, "controller.editor_camera requires an actions object");

    const char *action_keys[] = {"forward", "back",     "left",    "right",    "up",
                                 "down",    "look",     "fast",    "pan_left", "pan_right",
                                 "pan_up",  "pan_down", "zoom_in", "zoom_out", "zoom_wheel"};
    bool has_action = false;
    for (size_t i = 0; i < SDL_arraysize(action_keys); ++i)
    {
        const char *action = json_string(actions, action_keys[i]);
        if (action == NULL)
            continue;
        has_action = true;
        if (!require_ref(ctx, &names->actions, "input action", action, path))
            return false;
    }
    if (!has_action)
        return validation_error(ctx, path, "controller.editor_camera actions must reference at least one input action");

    const char *property_keys[] = {
        "yaw_property", "pitch_property", "forward_property", "mode_key", "orthographic_size_key",
        "flyby_mode",   "top_mode",       "front_mode",       "side_mode"};
    for (size_t i = 0; i < SDL_arraysize(property_keys); ++i)
    {
        yyjson_val *value = obj_get(component, property_keys[i]);
        if (value != NULL && (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0'))
            return validation_error(ctx, path, "controller.editor_camera property names must be non-empty strings");
    }

    const char *numeric_keys[] = {"move_speed",
                                  "fast_speed",
                                  "mouse_sensitivity",
                                  "spawn_yaw",
                                  "spawn_pitch",
                                  "pitch_min",
                                  "pitch_max",
                                  "orthographic_size",
                                  "orthographic_pan_speed",
                                  "orthographic_zoom_speed",
                                  "orthographic_wheel_zoom_step",
                                  "orthographic_min_size",
                                  "orthographic_max_size"};
    for (size_t i = 0; i < SDL_arraysize(numeric_keys); ++i)
    {
        yyjson_val *value = obj_get(component, numeric_keys[i]);
        if (value != NULL && !yyjson_is_num(value))
            return validation_error(ctx, path, "controller.editor_camera numeric tuning values must be numbers");
    }
    const char *non_negative[] = {"move_speed",
                                  "fast_speed",
                                  "mouse_sensitivity",
                                  "orthographic_size",
                                  "orthographic_pan_speed",
                                  "orthographic_zoom_speed",
                                  "orthographic_wheel_zoom_step",
                                  "orthographic_min_size",
                                  "orthographic_max_size"};
    for (size_t i = 0; i < SDL_arraysize(non_negative); ++i)
    {
        yyjson_val *value = obj_get(component, non_negative[i]);
        if (value != NULL && yyjson_get_num(value) < 0.0)
            return validation_error(ctx, path,
                                    "controller.editor_camera speed and sensitivity values must be non-negative");
    }
    yyjson_val *pitch_min = obj_get(component, "pitch_min");
    yyjson_val *pitch_max = obj_get(component, "pitch_max");
    if (pitch_min != NULL && pitch_max != NULL && yyjson_get_num(pitch_min) >= yyjson_get_num(pitch_max))
        return validation_error(ctx, path, "controller.editor_camera pitch_min must be less than pitch_max");
    yyjson_val *orthographic_min_size = obj_get(component, "orthographic_min_size");
    yyjson_val *orthographic_max_size = obj_get(component, "orthographic_max_size");
    if (orthographic_min_size != NULL && orthographic_max_size != NULL &&
        yyjson_get_num(orthographic_min_size) > yyjson_get_num(orthographic_max_size))
    {
        return validation_error(ctx, path,
                                "controller.editor_camera orthographic_min_size must not exceed orthographic_max_size");
    }
    yyjson_val *mouse_look = obj_get(component, "mouse_look");
    if (mouse_look != NULL && !yyjson_is_bool(mouse_look))
        return validation_error(ctx, path, "controller.editor_camera mouse_look must be a boolean");
    return true;
}

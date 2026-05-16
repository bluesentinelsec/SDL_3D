/**
 * @file game_data_render_runtime.c
 * @brief App control, asset, camera, light, render, particle, and transition helpers.
 */

#include "game_data_internal.h"

#include "game_data_standard_options.h"

static bool particle_emitter_component_is_view_space(yyjson_val *component)
{
    const char *space = json_string(component, "space", "world");
    return space != NULL && SDL_strcmp(space, "camera") == 0;
}

bool slayer3d_game_data_get_app_control(const slayer3d_game_data_runtime *runtime,
                                        slayer3d_game_data_app_control *out_control)
{
    if (out_control != NULL)
    {
        out_control->start_signal_id = -1;
        out_control->quit_action_id = -1;
        out_control->pause_action_id = -1;
        out_control->startup_transition = NULL;
        out_control->quit_transition = NULL;
        out_control->quit_signal_id = -1;
        out_control->window_apply_signal_id = -1;
        out_control->window_settings_target = NULL;
        out_control->window_display_mode_key = NULL;
        out_control->window_renderer_key = NULL;
        out_control->window_vsync_key = NULL;
    }
    if (runtime == NULL || out_control == NULL)
        return false;

    yyjson_val *app = obj_get(runtime_root(runtime), "app");
    yyjson_val *pause = obj_get(app, "pause");
    yyjson_val *quit = obj_get(app, "quit");
    yyjson_val *window = obj_get(app, "window");
    yyjson_val *window_settings = obj_get(window, "settings");
    out_control->start_signal_id = slayer3d_game_data_find_signal(runtime, json_string(app, "start_signal", NULL));
    out_control->pause_action_id = slayer3d_game_data_find_action(runtime, json_string(pause, "action", NULL));
    out_control->startup_transition = json_string(app, "startup_transition", NULL);
    out_control->quit_action_id = slayer3d_game_data_find_action(runtime, json_string(quit, "action", NULL));
    out_control->quit_transition = json_string(quit, "transition", NULL);
    out_control->quit_signal_id = slayer3d_game_data_find_signal(runtime, json_string(quit, "quit_signal", NULL));
    out_control->window_apply_signal_id =
        slayer3d_game_data_find_signal(runtime, json_string(window, "apply_signal", NULL));
    out_control->window_settings_target = json_string(window_settings, "target", "entity.settings");
    out_control->window_display_mode_key = json_string(window_settings, "display_mode", "display_mode");
    out_control->window_renderer_key = json_string(window_settings, "renderer", "renderer");
    out_control->window_vsync_key = json_string(window_settings, "vsync", "vsync");
    return true;
}

bool slayer3d_game_data_app_signal_applies_window_settings(const slayer3d_game_data_runtime *runtime, int signal_id)
{
    if (runtime == NULL || signal_id < 0)
        return false;

    yyjson_val *window = obj_get(obj_get(runtime_root(runtime), "app"), "window");
    const char *apply_signal = json_string(window, "apply_signal", NULL);
    if (apply_signal != NULL && slayer3d_game_data_find_signal(runtime, apply_signal) == signal_id)
        return true;

    yyjson_val *apply_signals = obj_get(window, "apply_signals");
    if (!yyjson_is_arr(apply_signals))
        return false;

    for (size_t i = 0; i < yyjson_arr_size(apply_signals); ++i)
    {
        yyjson_val *signal = yyjson_arr_get(apply_signals, i);
        if (yyjson_is_str(signal) && slayer3d_game_data_find_signal(runtime, yyjson_get_str(signal)) == signal_id)
            return true;
    }
    return false;
}

bool slayer3d_game_data_get_font_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                       slayer3d_game_data_font_asset *out_font)
{
    if (out_font != NULL)
    {
        SDL_zero(*out_font);
        out_font->builtin_id = SLAYER3D_BUILTIN_FONT_INTER;
        out_font->size = 16.0f;
    }
    if (runtime == NULL || id == NULL || out_font == NULL)
        return false;

    yyjson_val *font = find_font_json(runtime, id);
    if (!yyjson_is_obj(font))
        return false;

    out_font->id = json_string(font, "id", NULL);
    out_font->path = json_string(font, "path", NULL);
    out_font->size = json_float(font, "size", out_font->size);
    const char *builtin = json_string(font, "builtin", NULL);
    out_font->builtin = builtin != NULL;
    out_font->builtin_id = parse_builtin_font(builtin, out_font->builtin_id);
    return true;
}

bool slayer3d_game_data_get_image_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                        slayer3d_game_data_image_asset *out_image)
{
    if (out_image != NULL)
        SDL_zero(*out_image);
    if (runtime == NULL || id == NULL || out_image == NULL)
        return false;

    yyjson_val *image = find_image_json(runtime, id);
    if (!yyjson_is_obj(image))
        return false;

    out_image->id = json_string(image, "id", NULL);
    out_image->path = json_string(image, "path", NULL);
    out_image->sprite = json_string(image, "sprite", NULL);
    return out_image->id != NULL && (out_image->path != NULL || out_image->sprite != NULL);
}

bool slayer3d_game_data_get_model_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                        slayer3d_game_data_model_asset *out_model)
{
    if (out_model != NULL)
        SDL_zero(*out_model);
    if (runtime == NULL || id == NULL || out_model == NULL)
        return false;

    yyjson_val *model = find_model_json(runtime, id);
    if (!yyjson_is_obj(model))
        return false;

    out_model->id = json_string(model, "id", NULL);
    out_model->path = json_string(model, "path", NULL);
    return out_model->id != NULL && out_model->path != NULL;
}

bool slayer3d_game_data_get_sound_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                        slayer3d_game_data_sound_asset *out_sound)
{
    if (out_sound != NULL)
    {
        SDL_zero(*out_sound);
        out_sound->volume = 1.0f;
        out_sound->pitch = 1.0f;
        out_sound->bus = SLAYER3D_AUDIO_BUS_SOUND_EFFECTS;
    }
    if (runtime == NULL || id == NULL || out_sound == NULL)
        return false;

    yyjson_val *sound = find_sound_json(runtime, id);
    if (!yyjson_is_obj(sound))
        return false;

    out_sound->id = json_string(sound, "id", NULL);
    out_sound->path = json_string(sound, "path", NULL);
    out_sound->volume = json_float(sound, "volume", out_sound->volume);
    out_sound->pitch = json_float(sound, "pitch", out_sound->pitch);
    out_sound->pan = json_float(sound, "pan", out_sound->pan);
    out_sound->bus = parse_audio_bus(json_string(sound, "bus", NULL), out_sound->bus);
    return out_sound->id != NULL && out_sound->path != NULL;
}

bool slayer3d_game_data_get_music_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                        slayer3d_game_data_music_asset *out_music)
{
    if (out_music != NULL)
    {
        SDL_zero(*out_music);
        out_music->volume = 1.0f;
        out_music->loop = true;
    }
    if (runtime == NULL || id == NULL || out_music == NULL)
        return false;

    yyjson_val *music = find_music_json(runtime, id);
    if (!yyjson_is_obj(music))
        return false;

    out_music->id = json_string(music, "id", NULL);
    out_music->path = json_string(music, "path", NULL);
    out_music->volume = json_float(music, "volume", out_music->volume);
    out_music->loop = json_bool(music, "loop", out_music->loop);
    return out_music->id != NULL && out_music->path != NULL;
}

bool slayer3d_game_data_get_ambient_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                          slayer3d_game_data_ambient_asset *out_ambient)
{
    if (out_ambient != NULL)
    {
        SDL_zero(*out_ambient);
        out_ambient->volume = 1.0f;
        out_ambient->loop = true;
    }
    if (runtime == NULL || id == NULL || out_ambient == NULL)
        return false;

    yyjson_val *ambient = find_ambient_json(runtime, id);
    if (!yyjson_is_obj(ambient))
        return false;

    out_ambient->id = json_string(ambient, "id", NULL);
    out_ambient->ambient_id = json_int(ambient, "ambient_id", -1);
    out_ambient->path = json_string(ambient, "path", NULL);
    out_ambient->volume = json_float(ambient, "volume", out_ambient->volume);
    out_ambient->loop = json_bool(ambient, "loop", out_ambient->loop);
    return out_ambient->id != NULL && out_ambient->ambient_id >= 0 && out_ambient->path != NULL;
}

bool slayer3d_game_data_get_sprite_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                         slayer3d_game_data_sprite_asset *out_sprite)
{
    if (out_sprite != NULL)
    {
        SDL_zero(*out_sprite);
        out_sprite->source_kind = SLAYER3D_SPRITE_ASSET_SOURCE_SHEET;
        out_sprite->columns = 1;
        out_sprite->rows = 1;
        out_sprite->frame_count = 1;
        out_sprite->direction_count = 1;
        out_sprite->loop = true;
        out_sprite->lighting = true;
    }
    if (runtime == NULL || id == NULL || out_sprite == NULL)
        return false;

    yyjson_val *sprite = find_sprite_json(runtime, id);
    if (!yyjson_is_obj(sprite))
        return false;

    out_sprite->id = json_string(sprite, "id", NULL);
    const char *kind = json_string(sprite, "kind", "sheet");
    out_sprite->source_kind = kind != NULL && SDL_strcmp(kind, "files") == 0 ? SLAYER3D_SPRITE_ASSET_SOURCE_FILES
                                                                             : SLAYER3D_SPRITE_ASSET_SOURCE_SHEET;
    out_sprite->path = json_string(sprite, "path", NULL);
    out_sprite->shader_vertex_path = json_string(sprite, "shader_vertex_path", NULL);
    out_sprite->shader_fragment_path = json_string(sprite, "shader_fragment_path", NULL);
    out_sprite->frame_width = json_int(sprite, "frame_width", out_sprite->frame_width);
    out_sprite->frame_height = json_int(sprite, "frame_height", out_sprite->frame_height);
    out_sprite->columns = json_int(sprite, "columns", out_sprite->columns);
    out_sprite->rows = json_int(sprite, "rows", out_sprite->rows);
    out_sprite->frame_count = json_int(sprite, "frame_count", out_sprite->frame_count);
    out_sprite->direction_count = json_int(sprite, "direction_count", out_sprite->direction_count);
    out_sprite->fps = json_float(sprite, "fps", out_sprite->fps);
    out_sprite->loop = json_bool(sprite, "loop", out_sprite->loop);
    out_sprite->lighting = json_bool(sprite, "lighting", out_sprite->lighting);
    out_sprite->emissive = json_bool(sprite, "emissive", out_sprite->emissive);
    out_sprite->visual_ground_offset = json_float(sprite, "visual_ground_offset", out_sprite->visual_ground_offset);
    out_sprite->effect = parse_ui_image_effect(json_string(sprite, "effect", NULL));
    out_sprite->effect_delay = json_float(sprite, "effect_delay", 0.0f);
    out_sprite->effect_duration = json_float(sprite, "effect_duration", 1.0f);

    if (out_sprite->id == NULL || out_sprite->frame_count <= 0 || out_sprite->direction_count <= 0 ||
        (out_sprite->source_kind == SLAYER3D_SPRITE_ASSET_SOURCE_SHEET &&
         (out_sprite->path == NULL || out_sprite->frame_width <= 0 || out_sprite->frame_height <= 0 ||
          out_sprite->columns <= 0 || out_sprite->rows <= 0)))
    {
        SDL_zero(*out_sprite);
        return false;
    }
    return true;
}

static bool read_sprite_path_array(yyjson_val *array, const char ***out_paths, int *out_count)
{
    if (out_paths == NULL || out_count == NULL)
        return false;

    *out_paths = NULL;
    *out_count = 0;
    if (!yyjson_is_arr(array) || yyjson_arr_size(array) <= 0)
        return false;

    const size_t count = yyjson_arr_size(array);
    const char **paths = (const char **)SDL_calloc(count, sizeof(*paths));
    if (paths == NULL)
        return false;

    for (size_t i = 0; i < count; ++i)
    {
        yyjson_val *value = yyjson_arr_get(array, i);
        const char *path = yyjson_get_str(value);
        if (path == NULL || path[0] == '\0')
        {
            SDL_free(paths);
            return false;
        }
        paths[i] = path;
    }

    *out_paths = paths;
    *out_count = (int)count;
    return true;
}

bool slayer3d_game_data_load_sprite_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                          slayer3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                          int error_buffer_size)
{
    slayer3d_game_data_sprite_asset sprite;
    slayer3d_sprite_asset_source source;
    const char **base_paths = NULL;
    const char **frame_paths = NULL;
    int base_path_count = 0;
    int frame_path_count = 0;

    if (out_sprite != NULL)
        SDL_zero(*out_sprite);
    if (runtime == NULL || id == NULL || out_sprite == NULL)
        return false;

    if (!slayer3d_game_data_get_sprite_asset(runtime, id, &sprite))
    {
        set_error(error_buffer, error_buffer_size, "sprite asset not found");
        return false;
    }

    SDL_zero(source);
    source.kind = sprite.source_kind;
    source.sheet_path = sprite.path;
    source.shader_vertex_path = sprite.shader_vertex_path;
    source.shader_fragment_path = sprite.shader_fragment_path;
    source.frame_width = sprite.frame_width;
    source.frame_height = sprite.frame_height;
    source.columns = sprite.columns;
    source.rows = sprite.rows;
    source.frame_count = sprite.frame_count;
    source.direction_count = sprite.direction_count;
    source.fps = sprite.fps;
    source.loop = sprite.loop;
    source.lighting = sprite.lighting;
    source.emissive = sprite.emissive;
    source.visual_ground_offset = sprite.visual_ground_offset;
    source.effect = sprite.effect;
    source.effect_delay = sprite.effect_delay;
    source.effect_duration = sprite.effect_duration;

    if (sprite.source_kind == SLAYER3D_SPRITE_ASSET_SOURCE_FILES)
    {
        yyjson_val *sprite_json = find_sprite_json(runtime, id);
        if (!read_sprite_path_array(obj_get(sprite_json, "base_paths"), &base_paths, &base_path_count) ||
            !read_sprite_path_array(obj_get(sprite_json, "frame_paths"), &frame_paths, &frame_path_count))
        {
            SDL_free(base_paths);
            SDL_free(frame_paths);
            set_error(error_buffer, error_buffer_size, "invalid sprite file-list paths");
            return false;
        }
        if (base_path_count != sprite.direction_count ||
            frame_path_count != sprite.frame_count * sprite.direction_count)
        {
            SDL_free(base_paths);
            SDL_free(frame_paths);
            set_error(error_buffer, error_buffer_size, "sprite file-list path count does not match metadata");
            return false;
        }
        source.base_paths = base_paths;
        source.frame_paths = frame_paths;
    }

    const bool loaded =
        slayer3d_sprite_asset_load(runtime->assets, &source, out_sprite, error_buffer, error_buffer_size);
    SDL_free(base_paths);
    SDL_free(frame_paths);
    if (!loaded)
        return false;
    return true;
}

const char *slayer3d_game_data_active_camera(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL ? runtime->active_camera : NULL;
}

static slayer3d_vec3 camera_vec3_from_entity_or_json(const slayer3d_game_data_runtime *runtime, yyjson_val *camera_json,
                                                     const char *entity_key, const char *offset_key,
                                                     const char *json_key, slayer3d_vec3 fallback)
{
    slayer3d_registered_actor *actor =
        slayer3d_game_data_find_actor(runtime, json_string(camera_json, entity_key, NULL));
    if (actor != NULL)
    {
        return slayer3d_vec3_add(actor->position,
                                 json_vec3(camera_json, offset_key, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
    }
    return json_vec3(camera_json, json_key, fallback);
}

bool slayer3d_game_data_get_camera(const slayer3d_game_data_runtime *runtime, const char *name,
                                   slayer3d_camera3d *out_camera)
{
    if (out_camera != NULL)
        SDL_zero(*out_camera);
    if (runtime == NULL || name == NULL || out_camera == NULL)
        return false;

    yyjson_val *camera_json = find_camera_json(runtime, name);
    if (camera_json == NULL)
        return false;

    const char *type = json_string(camera_json, "type", "perspective");
    if (SDL_strcmp(type, "adapter") == 0)
        return false;

    if (SDL_strcmp(type, "fps") == 0)
    {
        slayer3d_registered_actor *target =
            slayer3d_game_data_find_actor(runtime, json_string(camera_json, "target_entity", NULL));
        if (target == NULL)
            return false;

        const float fov = camera_fov_degrees(runtime, camera_json, SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES);
        const slayer3d_camera_fov_axis fov_axis = camera_fov_axis(runtime, camera_json);
        const fps_controller_runtime *controller = find_fps_controller_const(runtime, target->name);
        if (controller != NULL && controller->initialized)
        {
            *out_camera = slayer3d_fps_mover_camera(&controller->mover, fov);
            out_camera->fov_axis = fov_axis;
            return true;
        }

        slayer3d_fps_mover fallback_mover;
        SDL_zero(fallback_mover);
        fallback_mover.position = target->position;
        fallback_mover.yaw = slayer3d_properties_get_float(
            target->props, json_string(camera_json, "yaw_property", "yaw"), json_float(camera_json, "yaw", 0.0f));
        fallback_mover.pitch = slayer3d_properties_get_float(
            target->props, json_string(camera_json, "pitch_property", "pitch"), json_float(camera_json, "pitch", 0.0f));
        fallback_mover.view_smooth = slayer3d_properties_get_float(
            target->props, json_string(camera_json, "view_smooth_property", "view_smooth"), 0.0f);
        *out_camera = slayer3d_fps_mover_camera(&fallback_mover, fov);
        out_camera->fov_axis = fov_axis;
        return true;
    }

    if (SDL_strcmp(type, "chase") == 0)
    {
        slayer3d_registered_actor *target =
            slayer3d_game_data_find_actor(runtime, json_string(camera_json, "target_entity", NULL));
        if (target == NULL)
            return false;

        const char *velocity_property = json_string(camera_json, "velocity_property", "velocity");
        slayer3d_vec3 velocity =
            slayer3d_properties_get_vec3(target->props, velocity_property, slayer3d_vec3_make(1.0f, 0.0f, 0.0f));
        velocity.z = 0.0f;
        float velocity_len = SDL_sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
        if (velocity_len < 0.001f)
        {
            velocity = json_vec3(camera_json, "fallback_forward", slayer3d_vec3_make(1.0f, 0.0f, 0.0f));
            velocity.z = 0.0f;
            velocity_len = SDL_sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
        }
        if (velocity_len < 0.001f)
            velocity = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        else
        {
            velocity.x /= velocity_len;
            velocity.y /= velocity_len;
        }

        const float target_z_offset = json_float(camera_json, "target_z_offset", 0.0f);
        const float camera_height = json_float(camera_json, "height", 1.0f);
        const float chase_distance = json_float(camera_json, "chase_distance", 2.0f);
        const float lookahead = json_float(camera_json, "lookahead", 1.0f);
        const slayer3d_vec3 target_offset =
            json_vec3(camera_json, "target_offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        const slayer3d_vec3 eye_offset = json_vec3(camera_json, "eye_offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        const slayer3d_vec3 anchor =
            slayer3d_vec3_make(target->position.x + target_offset.x, target->position.y + target_offset.y,
                               target->position.z + target_offset.z);

        out_camera->position = slayer3d_vec3_make(anchor.x - velocity.x * chase_distance + eye_offset.x,
                                                  anchor.y - velocity.y * chase_distance + eye_offset.y,
                                                  anchor.z + camera_height + eye_offset.z);
        out_camera->target = slayer3d_vec3_make(anchor.x + velocity.x * lookahead, anchor.y + velocity.y * lookahead,
                                                anchor.z + target_z_offset);
        out_camera->up = json_vec3(camera_json, "up", slayer3d_vec3_make(0.0f, 0.0f, 1.0f));
        out_camera->fovy = camera_fov_degrees(runtime, camera_json, SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES);
        out_camera->fov_axis = camera_fov_axis(runtime, camera_json);
        out_camera->projection = SLAYER3D_CAMERA_PERSPECTIVE;
        return true;
    }

    out_camera->position = camera_vec3_from_entity_or_json(runtime, camera_json, "position_entity", "position_offset",
                                                           "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    out_camera->target = camera_vec3_from_entity_or_json(runtime, camera_json, "target_entity", "target_offset",
                                                         "target", slayer3d_vec3_make(0.0f, 0.0f, -1.0f));
    out_camera->up = json_vec3(camera_json, "up", slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    if (SDL_strcmp(type, "orthographic") == 0)
    {
        out_camera->projection = SLAYER3D_CAMERA_ORTHOGRAPHIC;
        out_camera->fovy = json_float(camera_json, "size", 10.0f);
    }
    else
    {
        out_camera->projection = SLAYER3D_CAMERA_PERSPECTIVE;
        out_camera->fovy = camera_fov_degrees(runtime, camera_json, SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES);
        out_camera->fov_axis = camera_fov_axis(runtime, camera_json);
    }
    return true;
}

bool slayer3d_game_data_get_camera_float(const slayer3d_game_data_runtime *runtime, const char *camera_name,
                                         const char *property_name, float *out_value)
{
    if (out_value != NULL)
        *out_value = 0.0f;
    if (runtime == NULL || camera_name == NULL || property_name == NULL || out_value == NULL)
        return false;

    yyjson_val *camera = find_camera_json(runtime, camera_name);
    yyjson_val *value = obj_get(obj_get(camera, "properties"), property_name);
    if (!yyjson_is_num(value))
        value = obj_get(camera, property_name);
    if (!yyjson_is_num(value))
        return false;

    *out_value = (float)yyjson_get_num(value);
    return true;
}

bool slayer3d_game_data_get_world_units(const slayer3d_game_data_runtime *runtime, const char **out_units,
                                        float *out_meters_per_unit)
{
    if (out_units != NULL)
        *out_units = NULL;
    if (out_meters_per_unit != NULL)
        *out_meters_per_unit = 0.0f;
    if (runtime == NULL || out_units == NULL || out_meters_per_unit == NULL)
        return false;

    yyjson_val *world = obj_get(runtime_root(runtime), "world");
    *out_units = json_string(world, "units", SLAYER3D_GAME_DATA_DEFAULT_WORLD_UNITS);
    *out_meters_per_unit = json_float(world, "meters_per_unit", SLAYER3D_GAME_DATA_DEFAULT_METERS_PER_UNIT);
    return *out_units != NULL && (*out_units)[0] != '\0' && *out_meters_per_unit > 0.0f;
}

int slayer3d_game_data_world_light_count(const slayer3d_game_data_runtime *runtime)
{
    yyjson_val *lights = obj_get(obj_get(runtime_root(runtime), "world"), "lights");
    int count = 0;
    for (size_t i = 0; yyjson_is_arr(lights) && i < yyjson_arr_size(lights); ++i)
    {
        yyjson_val *light = yyjson_arr_get(lights, i);
        if (scene_state_bool(runtime, json_string(light, "enabled_key", NULL), json_bool(light, "enabled", true)))
            count++;
    }
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; runtime != NULL && yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (!active_scene_has_entity_internal(runtime, entity_name) || actor == NULL || !actor->active)
            continue;
        yyjson_val *components = obj_get(entity, "components");
        for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type", "");
            if (SDL_strncmp(type, "light.", 6) == 0 &&
                scene_state_bool(runtime, json_string(component, "enabled_key", NULL),
                                 json_bool(component, "enabled", true)))
            {
                count++;
            }
        }
    }
    for (int pool_index = 0; runtime != NULL && pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
            continue;
        yyjson_val *components = obj_get(pool->archetype_json, "components");
        int light_components = 0;
        for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type", "");
            if (SDL_strncmp(type, "light.", 6) == 0 &&
                scene_state_bool(runtime, json_string(component, "enabled_key", NULL),
                                 json_bool(component, "enabled", true)))
            {
                light_components++;
            }
        }
        if (light_components <= 0)
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index))
                count += light_components;
        }
    }
    return count;
}

bool slayer3d_game_data_get_world_ambient_light(const slayer3d_game_data_runtime *runtime, float out_rgb[3])
{
    if (out_rgb != NULL)
    {
        out_rgb[0] = 0.0f;
        out_rgb[1] = 0.0f;
        out_rgb[2] = 0.0f;
    }
    if (runtime == NULL || out_rgb == NULL)
        return false;

    yyjson_val *ambient = obj_get(obj_get(runtime_root(runtime), "world"), "ambient_light");
    if (!yyjson_is_arr(ambient) || yyjson_arr_size(ambient) < 3)
        return false;

    for (int i = 0; i < 3; ++i)
    {
        yyjson_val *channel = yyjson_arr_get(ambient, (size_t)i);
        if (!yyjson_is_num(channel))
            return false;
        out_rgb[i] = (float)yyjson_get_num(channel);
    }
    return true;
}

static bool read_light_json(const slayer3d_game_data_runtime *runtime, yyjson_val *light_json,
                            const slayer3d_registered_actor *component_actor, slayer3d_light *out_light)
{
    if (out_light != NULL)
        SDL_zero(*out_light);
    if (runtime == NULL || light_json == NULL || out_light == NULL)
        return false;

    const char *type = json_string(light_json, "type", "point");
    if (SDL_strcmp(type, "directional") == 0 || SDL_strcmp(type, "light.directional") == 0)
        out_light->type = SLAYER3D_LIGHT_DIRECTIONAL;
    else if (SDL_strcmp(type, "spot") == 0 || SDL_strcmp(type, "light.spot") == 0)
        out_light->type = SLAYER3D_LIGHT_SPOT;
    else
        out_light->type = SLAYER3D_LIGHT_POINT;

    out_light->position = json_vec3(light_json, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    if (component_actor != NULL)
        out_light->position = component_actor->position;
    const char *target_entity = json_string(light_json, "target_entity", NULL);
    slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, target_entity);
    yyjson_val *target_entities = obj_get(light_json, "target_entities");
    for (size_t i = 0; target == NULL && yyjson_is_arr(target_entities) && i < yyjson_arr_size(target_entities); ++i)
    {
        const char *candidate = yyjson_get_str(yyjson_arr_get(target_entities, i));
        if (candidate != NULL && active_scene_has_entity_internal(runtime, candidate))
            target = slayer3d_game_data_find_actor(runtime, candidate);
    }
    for (size_t i = 0; target == NULL && yyjson_is_arr(target_entities) && i < yyjson_arr_size(target_entities); ++i)
    {
        const char *candidate = yyjson_get_str(yyjson_arr_get(target_entities, i));
        if (candidate != NULL)
            target = slayer3d_game_data_find_actor(runtime, candidate);
    }
    if (target != NULL)
        out_light->position = target->position;
    if (target != NULL || component_actor != NULL)
    {
        const slayer3d_vec3 offset = json_vec3(light_json, "offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        out_light->position = slayer3d_vec3_make(out_light->position.x + offset.x, out_light->position.y + offset.y,
                                                 out_light->position.z + offset.z);
    }
    out_light->direction = json_vec3(light_json, "direction", slayer3d_vec3_make(0.0f, -1.0f, 0.0f));
    yyjson_val *color = obj_get(light_json, "color");
    out_light->color[0] = 1.0f;
    out_light->color[1] = 1.0f;
    out_light->color[2] = 1.0f;
    for (int i = 0; yyjson_is_arr(color) && i < 3; ++i)
    {
        yyjson_val *channel = yyjson_arr_get(color, (size_t)i);
        if (yyjson_is_num(channel))
            out_light->color[i] = (float)yyjson_get_num(channel);
    }
    out_light->intensity = json_float(light_json, "intensity", 1.0f);
    out_light->range = json_float(light_json, "range", 10.0f);
    out_light->inner_cutoff = json_float(light_json, "inner_cutoff", 0.0f);
    out_light->outer_cutoff = json_float(light_json, "outer_cutoff", 0.0f);
    return true;
}

static bool slayer3d_game_data_get_world_light_internal(const slayer3d_game_data_runtime *runtime, int index,
                                                        slayer3d_light *out_light, yyjson_val **out_light_json)
{
    yyjson_val *lights = obj_get(obj_get(runtime_root(runtime), "world"), "lights");
    if (out_light_json != NULL)
        *out_light_json = NULL;
    if (runtime == NULL || index < 0 || out_light == NULL)
        return false;

    int remaining = index;
    for (size_t i = 0; yyjson_is_arr(lights) && i < yyjson_arr_size(lights); ++i)
    {
        yyjson_val *light = yyjson_arr_get(lights, i);
        if (!scene_state_bool(runtime, json_string(light, "enabled_key", NULL), json_bool(light, "enabled", true)))
            continue;
        if (remaining-- == 0)
        {
            if (out_light_json != NULL)
                *out_light_json = light;
            return read_light_json(runtime, light, NULL, out_light);
        }
    }

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (!active_scene_has_entity_internal(runtime, entity_name) || actor == NULL || !actor->active)
            continue;
        yyjson_val *components = obj_get(entity, "components");
        for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            const char *type = json_string(component, "type", "");
            if (SDL_strncmp(type, "light.", 6) != 0)
                continue;
            if (!scene_state_bool(runtime, json_string(component, "enabled_key", NULL),
                                  json_bool(component, "enabled", true)))
            {
                continue;
            }
            if (remaining-- == 0)
            {
                if (out_light_json != NULL)
                    *out_light_json = component;
                return read_light_json(runtime, component, actor, out_light);
            }
        }
    }
    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
            continue;
        yyjson_val *components = obj_get(pool->archetype_json, "components");
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (!actor_pool_actor_is_active(pool, actor, actor_index))
                continue;
            for (size_t c = 0; yyjson_is_arr(components) && c < yyjson_arr_size(components); ++c)
            {
                yyjson_val *component = yyjson_arr_get(components, c);
                const char *type = json_string(component, "type", "");
                if (SDL_strncmp(type, "light.", 6) != 0)
                    continue;
                if (!scene_state_bool(runtime, json_string(component, "enabled_key", NULL),
                                      json_bool(component, "enabled", true)))
                {
                    continue;
                }
                if (remaining-- == 0)
                {
                    if (out_light_json != NULL)
                        *out_light_json = component;
                    return read_light_json(runtime, component, actor, out_light);
                }
            }
        }
    }
    return false;
}

bool slayer3d_game_data_get_world_light(const slayer3d_game_data_runtime *runtime, int index, slayer3d_light *out_light)
{
    return slayer3d_game_data_get_world_light_internal(runtime, index, out_light, NULL);
}

static float game_data_clampf(float value, float lo, float hi);

static void light_color_lerp(float color[3], const slayer3d_vec3 target, float t)
{
    if (color == NULL)
        return;
    t = game_data_clampf(t, 0.0f, 1.0f);
    color[0] = color[0] + (target.x - color[0]) * t;
    color[1] = color[1] + (target.y - color[1]) * t;
    color[2] = color[2] + (target.z - color[2]) * t;
}

static bool light_effect_sample_color_cycle(yyjson_val *effect, float time, slayer3d_vec3 fallback,
                                            slayer3d_vec3 *out_color)
{
    yyjson_val *colors = obj_get(effect, "colors");
    if (out_color == NULL || !yyjson_is_arr(colors) || yyjson_arr_size(colors) == 0)
        return false;

    const size_t count = yyjson_arr_size(colors);
    if (count == 1)
    {
        *out_color = json_vec3_value(yyjson_arr_get(colors, 0), fallback);
        return true;
    }

    const float duration = SDL_max(json_float(effect, "duration", 4.0f), 0.001f);
    float phase = json_float(effect, "phase", 0.0f);
    float cycle = SDL_fmodf(time / duration + phase, 1.0f);
    if (cycle < 0.0f)
        cycle += 1.0f;

    const float scaled = cycle * (float)count;
    size_t index = (size_t)SDL_floorf(scaled);
    if (index >= count)
        index = count - 1;
    const size_t next_index = (index + 1U) % count;

    float t = scaled - (float)index;
    if (json_bool(effect, "smooth", true))
        t = t * t * (3.0f - 2.0f * t);

    const slayer3d_vec3 a = json_vec3_value(yyjson_arr_get(colors, index), fallback);
    const slayer3d_vec3 b = json_vec3_value(yyjson_arr_get(colors, next_index), a);
    *out_color = slayer3d_vec3_make(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    return true;
}

static void apply_light_effects(const slayer3d_game_data_runtime *runtime, yyjson_val *light_json,
                                const slayer3d_game_data_render_eval *eval, slayer3d_light *light)
{
    yyjson_val *effects = obj_get(light_json, "effects");
    if (runtime == NULL || light == NULL || !yyjson_is_arr(effects))
        return;

    for (size_t i = 0; i < yyjson_arr_size(effects); ++i)
    {
        yyjson_val *effect = yyjson_arr_get(effects, i);
        const char *type = json_string(effect, "type", "");
        float value = 0.0f;
        if (SDL_strcmp(type, "pulse") == 0)
        {
            const float time = eval != NULL ? eval->time : 0.0f;
            const float rate = json_float(effect, "rate", 1.0f);
            const float phase = json_float(effect, "phase", 0.0f);
            value = 0.5f + 0.5f * SDL_sinf(time * rate + phase);
        }
        else if (SDL_strcmp(type, "color_cycle") == 0)
        {
            const float time = eval != NULL ? eval->time : 0.0f;
            slayer3d_vec3 target;
            if (light_effect_sample_color_cycle(
                    effect, time, slayer3d_vec3_make(light->color[0], light->color[1], light->color[2]), &target))
            {
                light_color_lerp(light->color, target, json_float(effect, "color_blend", 1.0f));
            }
            continue;
        }
        else if (SDL_strcmp(type, "flash") == 0)
        {
            slayer3d_registered_actor *source =
                slayer3d_game_data_find_actor(runtime, json_string(effect, "source", NULL));
            const char *property = json_string(effect, "property", NULL);
            value = source != NULL && property != NULL ? slayer3d_properties_get_float(source->props, property, 0.0f)
                                                       : 0.0f;
            value = game_data_clampf(value, 0.0f, 1.0f);
        }
        else
        {
            continue;
        }

        yyjson_val *color = obj_get(effect, "color");
        if (yyjson_is_arr(color))
        {
            const slayer3d_vec3 target =
                json_vec3_value(color, slayer3d_vec3_make(light->color[0], light->color[1], light->color[2]));
            light_color_lerp(light->color, target, value * json_float(effect, "color_blend", 1.0f));
        }
        light->intensity += json_float(effect, "intensity_add", 0.0f) * value;
        light->range += json_float(effect, "range_add", 0.0f) * value;
    }
}

bool slayer3d_game_data_get_world_light_evaluated(const slayer3d_game_data_runtime *runtime, int index,
                                                  const slayer3d_game_data_render_eval *eval, slayer3d_light *out_light)
{
    yyjson_val *light_json = NULL;
    if (!slayer3d_game_data_get_world_light_internal(runtime, index, out_light, &light_json))
        return false;

    apply_light_effects(runtime, light_json, eval, out_light);
    return true;
}

static float game_data_clampf(float value, float lo, float hi)
{
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

static slayer3d_color game_data_color_lerp(slayer3d_color a, slayer3d_color b, float t)
{
    t = game_data_clampf(t, 0.0f, 1.0f);
    return (slayer3d_color){
        (Uint8)((float)a.r + ((float)b.r - (float)a.r) * t),
        (Uint8)((float)a.g + ((float)b.g - (float)a.g) * t),
        (Uint8)((float)a.b + ((float)b.b - (float)a.b) * t),
        (Uint8)((float)a.a + ((float)b.a - (float)a.a) * t),
    };
}

static void apply_render_effects(const slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                 const slayer3d_game_data_render_eval *eval,
                                 slayer3d_game_data_render_primitive *primitive)
{
    yyjson_val *effects = obj_get(component, "effects");
    if (!yyjson_is_arr(effects) || primitive == NULL)
        return;

    for (size_t i = 0; i < yyjson_arr_size(effects); ++i)
    {
        yyjson_val *effect = yyjson_arr_get(effects, i);
        const char *type = json_string(effect, "type", "");
        if (SDL_strcmp(type, "flash") == 0)
        {
            slayer3d_registered_actor *source =
                slayer3d_game_data_find_actor(runtime, json_string(effect, "source", NULL));
            const char *property = json_string(effect, "property", NULL);
            const float value = game_data_clampf(source != NULL && property != NULL
                                                     ? slayer3d_properties_get_float(source->props, property, 0.0f)
                                                     : 0.0f,
                                                 0.0f, 1.0f);
            primitive->color =
                game_data_color_lerp(primitive->color, json_color(effect, "color", primitive->color), value);

            const slayer3d_vec3 size_add = json_vec3(effect, "size_add", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            const char *size_mode = json_string(effect, "size_mode", "vector");
            if (SDL_strcmp(size_mode, "minor_axis") == 0 && primitive->type == SLAYER3D_GAME_DATA_RENDER_CUBE)
            {
                if (primitive->size.x <= primitive->size.y)
                    primitive->size.x += size_add.x * value;
                else
                    primitive->size.y += size_add.y * value;
            }
            else
            {
                primitive->size.x += size_add.x * value;
                primitive->size.y += size_add.y * value;
                primitive->size.z += size_add.z * value;
            }

            const slayer3d_vec3 emissive = json_vec3(effect, "emissive", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            primitive->emissive_color.x += emissive.x * value;
            primitive->emissive_color.y += emissive.y * value;
            primitive->emissive_color.z += emissive.z * value;
        }
        else if (SDL_strcmp(type, "pulse") == 0)
        {
            const float time = eval != NULL ? eval->time : 0.0f;
            const float rate = json_float(effect, "rate", 1.0f);
            const float pulse = 0.5f + 0.5f * SDL_sinf(time * rate);
            primitive->color =
                game_data_color_lerp(primitive->color, json_color(effect, "color", primitive->color), pulse);

            const slayer3d_vec3 base = json_vec3(effect, "emissive_base", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            const slayer3d_vec3 add = json_vec3(effect, "emissive_add", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            primitive->emissive_color.x += base.x + add.x * pulse;
            primitive->emissive_color.y += base.y + add.y * pulse;
            primitive->emissive_color.z += base.z + add.z * pulse;

            const slayer3d_vec3 size_add = json_vec3(effect, "size_add", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            primitive->size.x += size_add.x * pulse;
            primitive->size.y += size_add.y * pulse;
            primitive->size.z += size_add.z * pulse;
            primitive->radius += json_float(effect, "radius_add", 0.0f) * pulse;
        }
        else if (SDL_strcmp(type, "drift") == 0)
        {
            const float time = eval != NULL ? eval->time : 0.0f;
            const float phase = json_float(effect, "phase", 0.0f);
            const slayer3d_vec3 offset = json_vec3(effect, "offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            const slayer3d_vec3 rates =
                json_vec3(effect, "rates",
                          slayer3d_vec3_make(json_float(effect, "rate", 1.0f), json_float(effect, "rate", 1.0f),
                                             json_float(effect, "rate", 1.0f)));
            primitive->position.x += offset.x * SDL_sinf(time * rates.x + phase);
            primitive->position.y += offset.y * SDL_cosf(time * rates.y + phase * 1.37f);
            primitive->position.z += offset.z * SDL_sinf(time * rates.z + phase * 0.73f);
        }
        else if (SDL_strcmp(type, "emissive") == 0)
        {
            const slayer3d_vec3 rgb = json_vec3(effect, "color", slayer3d_vec3_make(0.2f, 0.2f, 0.2f));
            primitive->emissive_color.x += rgb.x;
            primitive->emissive_color.y += rgb.y;
            primitive->emissive_color.z += rgb.z;
        }
    }
}

static slayer3d_game_data_mesh_primitive_kind mesh_primitive_kind_from_string(const char *name)
{
    if (name == NULL)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID;
    if (SDL_strcmp(name, "cube") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE;
    if (SDL_strcmp(name, "sphere") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE;
    if (SDL_strcmp(name, "capsule") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE;
    if (SDL_strcmp(name, "cylinder") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER;
    if (SDL_strcmp(name, "cone") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE;
    if (SDL_strcmp(name, "torus") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS;
    if (SDL_strcmp(name, "pyramid") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID;
    if (SDL_strcmp(name, "wedge") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE;
    if (SDL_strcmp(name, "plane") == 0 || SDL_strcmp(name, "quad") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE;
    if (SDL_strcmp(name, "disc") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC;
    if (SDL_strcmp(name, "hemisphere") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE;
    if (SDL_strcmp(name, "rounded_box") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX;
    if (SDL_strcmp(name, "tube_segment") == 0 || SDL_strcmp(name, "pipe") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT;
    if (SDL_strcmp(name, "arrow") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW;
    if (SDL_strcmp(name, "billboard_plane") == 0)
        return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE;
    return SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID;
}

static bool render_component_lighting_enabled(const slayer3d_game_data_runtime *runtime, yyjson_val *component,
                                              bool fallback)
{
    return scene_state_bool(runtime, json_string(component, "lighting_key", NULL),
                            json_bool(component, "lighting", fallback));
}

static slayer3d_game_data_render_draw_mode render_draw_mode_from_string(const char *name)
{
    if (name == NULL || SDL_strcmp(name, "solid") == 0)
        return SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID;
    if (SDL_strcmp(name, "wire") == 0)
        return SLAYER3D_GAME_DATA_RENDER_DRAW_WIRE;
    if (SDL_strcmp(name, "solid_wire") == 0)
        return SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID_WIRE;
    return SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID;
}

static bool json_string_or_array_contains(yyjson_val *value, const char *needle)
{
    if (needle == NULL || value == NULL)
        return false;
    if (yyjson_is_str(value))
        return SDL_strcmp(yyjson_get_str(value), needle) == 0;
    for (size_t i = 0; yyjson_is_arr(value) && i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (yyjson_is_str(entry) && SDL_strcmp(yyjson_get_str(entry), needle) == 0)
            return true;
    }
    return false;
}

static bool component_visible_for_active_camera(const slayer3d_game_data_runtime *runtime, yyjson_val *component)
{
    const char *active_camera = slayer3d_game_data_active_camera(runtime);
    yyjson_val *visible_to = obj_get(component, "visible_to_cameras");
    if (visible_to != NULL && !json_string_or_array_contains(visible_to, active_camera))
        return false;
    yyjson_val *hidden_from = obj_get(component, "hidden_from_cameras");
    return !json_string_or_array_contains(hidden_from, active_camera);
}

static void populate_mesh_primitive_descriptor(const slayer3d_registered_actor *actor, yyjson_val *component,
                                               slayer3d_game_data_render_primitive *primitive)
{
    if (component == NULL || primitive == NULL)
        return;
    primitive->type = SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE;
    primitive->mesh_primitive = mesh_primitive_kind_from_string(json_string(component, "primitive", NULL));
    primitive->draw_mode = render_draw_mode_from_string(json_string(component, "draw_mode", NULL));
    primitive->size = json_vec3(component, "size", primitive->size);
    const char *size_property = json_string(component, "size_property", NULL);
    if (actor != NULL && size_property != NULL)
        primitive->size = slayer3d_properties_get_vec3(actor->props, size_property, primitive->size);
    primitive->radius = json_float(component, "radius", primitive->radius);
    primitive->radius = slayer3d_properties_get_float(
        actor != NULL ? actor->props : NULL, json_string(component, "radius_property", NULL), primitive->radius);
    primitive->height =
        json_float(component, "height", primitive->height > 0.0f ? primitive->height : primitive->size.y);
    primitive->height = slayer3d_properties_get_float(
        actor != NULL ? actor->props : NULL, json_string(component, "height_property", NULL), primitive->height);
    primitive->radius_top = primitive->mesh_primitive == SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE
                                ? json_float(component, "radius_top", 0.0f)
                                : json_float(component, "radius_top", primitive->radius);
    primitive->radius_top =
        slayer3d_properties_get_float(actor != NULL ? actor->props : NULL,
                                      json_string(component, "radius_top_property", NULL), primitive->radius_top);
    primitive->radius_bottom = json_float(component, "radius_bottom", primitive->radius);
    primitive->radius_bottom =
        slayer3d_properties_get_float(actor != NULL ? actor->props : NULL,
                                      json_string(component, "radius_bottom_property", NULL), primitive->radius_bottom);
    primitive->major_radius = json_float(component, "major_radius", primitive->major_radius);
    primitive->minor_radius = json_float(component, "minor_radius", primitive->minor_radius);
    primitive->bevel_radius =
        json_float(component, "bevel_radius", json_float(component, "radius", primitive->bevel_radius));
    primitive->arc_angle = json_float(component, "arc_angle", primitive->arc_angle);
    primitive->slices = SDL_max(json_int(component, "slices", json_int(component, "segments", primitive->slices)), 3);
    primitive->rings = SDL_max(json_int(component, "rings", primitive->rings), 3);
    primitive->tube_segments = SDL_max(json_int(component, "tube_segments", primitive->tube_segments), 3);
    const float size_scale = slayer3d_properties_get_float(actor != NULL ? actor->props : NULL,
                                                           json_string(component, "size_scale_property", NULL), 1.0f);
    if (size_scale > 0.0f)
    {
        primitive->size = slayer3d_vec3_scale(primitive->size, size_scale);
        primitive->radius *= size_scale;
        primitive->height *= size_scale;
        primitive->radius_top *= size_scale;
        primitive->radius_bottom *= size_scale;
        primitive->major_radius *= size_scale;
        primitive->minor_radius *= size_scale;
        primitive->bevel_radius *= size_scale;
    }
}

static const slayer3d_sector *active_scene_sector_for_position(const slayer3d_game_data_runtime *runtime,
                                                               slayer3d_vec3 position)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *instances = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "sector_levels");
    if (!yyjson_is_arr(instances))
        return NULL;

    for (size_t i = 0; i < yyjson_arr_size(instances); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(instances, i);
        if (!scene_state_bool(runtime, json_string(entry, "sector_lighting_key", NULL),
                              json_bool(entry, "sector_lighting", true)))
        {
            continue;
        }
        const sector_level_runtime *level = find_sector_level_runtime(runtime, json_string(entry, "level", NULL));
        if (level == NULL)
            continue;
        const slayer3d_vec3 origin = json_vec3(entry, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        const slayer3d_vec3 local =
            slayer3d_vec3_make(position.x - origin.x, position.y - origin.y, position.z - origin.z);
        const int sector_index =
            slayer3d_level_find_sector_at(&level->lightmapped, level->sectors, local.x, local.z, local.y);
        if (sector_index >= 0 && sector_index < level->sector_count && level->sectors[sector_index].has_lighting)
            return &level->sectors[sector_index];
    }
    return NULL;
}

static void apply_sector_lighting_to_render_primitive(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_render_primitive *primitive)
{
    if (runtime == NULL || primitive == NULL || !primitive->lighting_enabled || primitive->view_space ||
        primitive->instances != NULL)
    {
        return;
    }
    modulate_color_by_sector_lighting(&primitive->color,
                                      active_scene_sector_for_position(runtime, primitive->position));
}

static slayer3d_vec3 render_component_offset(const slayer3d_registered_actor *actor, yyjson_val *component)
{
    slayer3d_vec3 offset = json_vec3(component, "offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    if (actor == NULL || actor->props == NULL)
        return offset;

    const char *x_property = json_string(component, "offset_x_property", NULL);
    const char *y_property = json_string(component, "offset_y_property", NULL);
    const char *z_property = json_string(component, "offset_z_property", NULL);
    const char *x_add_property = json_string(component, "offset_x_add_property", NULL);
    const char *y_add_property = json_string(component, "offset_y_add_property", NULL);
    const char *z_add_property = json_string(component, "offset_z_add_property", NULL);
    if (x_property != NULL)
        offset.x += slayer3d_properties_get_float(actor->props, x_property, 0.0f);
    if (y_property != NULL)
        offset.y += slayer3d_properties_get_float(actor->props, y_property, 0.0f);
    if (z_property != NULL)
        offset.z += slayer3d_properties_get_float(actor->props, z_property, 0.0f);
    if (x_add_property != NULL)
        offset.x += slayer3d_properties_get_float(actor->props, x_add_property, 0.0f);
    if (y_add_property != NULL)
        offset.y += slayer3d_properties_get_float(actor->props, y_add_property, 0.0f);
    if (z_add_property != NULL)
        offset.z += slayer3d_properties_get_float(actor->props, z_add_property, 0.0f);
    yyjson_val *x_add_properties = obj_get(component, "offset_x_add_properties");
    for (size_t i = 0; yyjson_is_arr(x_add_properties) && i < yyjson_arr_size(x_add_properties); ++i)
    {
        yyjson_val *property = yyjson_arr_get(x_add_properties, i);
        if (yyjson_is_str(property))
            offset.x += slayer3d_properties_get_float(actor->props, yyjson_get_str(property), 0.0f);
    }
    yyjson_val *y_add_properties = obj_get(component, "offset_y_add_properties");
    for (size_t i = 0; yyjson_is_arr(y_add_properties) && i < yyjson_arr_size(y_add_properties); ++i)
    {
        yyjson_val *property = yyjson_arr_get(y_add_properties, i);
        if (yyjson_is_str(property))
            offset.y += slayer3d_properties_get_float(actor->props, yyjson_get_str(property), 0.0f);
    }
    yyjson_val *z_add_properties = obj_get(component, "offset_z_add_properties");
    for (size_t i = 0; yyjson_is_arr(z_add_properties) && i < yyjson_arr_size(z_add_properties); ++i)
    {
        yyjson_val *property = yyjson_arr_get(z_add_properties, i);
        if (yyjson_is_str(property))
            offset.z += slayer3d_properties_get_float(actor->props, yyjson_get_str(property), 0.0f);
    }
    return offset;
}

static float render_component_property_list_sum(const slayer3d_registered_actor *actor, yyjson_val *component,
                                                const char *field)
{
    if (actor == NULL || actor->props == NULL || component == NULL || field == NULL)
        return 0.0f;
    float sum = 0.0f;
    yyjson_val *properties = obj_get(component, field);
    for (size_t i = 0; yyjson_is_arr(properties) && i < yyjson_arr_size(properties); ++i)
    {
        yyjson_val *property = yyjson_arr_get(properties, i);
        if (yyjson_is_str(property))
            sum += slayer3d_properties_get_float(actor->props, yyjson_get_str(property), 0.0f);
    }
    return sum;
}

static bool emit_actor_render_primitives(const slayer3d_game_data_runtime *runtime,
                                         const slayer3d_game_data_render_eval *eval,
                                         const slayer3d_registered_actor *actor, yyjson_val *components,
                                         slayer3d_game_data_render_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || actor == NULL || callback == NULL || !yyjson_is_arr(components))
        return true;

    for (size_t c = 0; c < yyjson_arr_size(components); ++c)
    {
        yyjson_val *component = yyjson_arr_get(components, c);
        if (!component_visible_for_active_camera(runtime, component))
            continue;
        const char *type = json_string(component, "type", "");
        slayer3d_game_data_render_primitive primitive;
        SDL_zero(primitive);
        primitive.entity_name = actor->name;
        primitive.position = actor->position;
        const slayer3d_vec3 offset = render_component_offset(actor, component);
        primitive.position.x += offset.x;
        primitive.position.y += offset.y;
        primitive.position.z += offset.z;
        const char *space = json_string(component, "space", "world");
        primitive.view_space = space != NULL && SDL_strcmp(space, "camera") == 0;
        if (primitive.view_space)
            primitive.position.z = -primitive.position.z;
        primitive.rotation_axis = json_vec3(component, "rotation_axis", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        primitive.rotation_angle = json_float(component, "rotation_angle", 0.0f);
        const char *rotation_property = json_string(component, "rotation_property", NULL);
        if (rotation_property != NULL)
            primitive.rotation_angle += slayer3d_properties_get_float(actor->props, rotation_property, 0.0f);
        primitive.color = json_color(component, "color", (slayer3d_color){255, 255, 255, 255});
        primitive.texture_image = json_string(component, "texture", NULL);
        primitive.lighting_enabled = render_component_lighting_enabled(runtime, component, true);
        primitive.emissive = json_bool(component, "emissive", false);
        primitive.emissive_color = primitive.emissive
                                       ? json_vec3(component, "emissive_color", slayer3d_vec3_make(0.2f, 0.2f, 0.2f))
                                       : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        if (primitive.emissive)
        {
            float emissive_intensity = json_float(component, "emissive_intensity", 1.0f);
            emissive_intensity = slayer3d_properties_get_float(
                actor->props, json_string(component, "emissive_intensity_property", NULL), emissive_intensity);
            primitive.emissive_color = slayer3d_vec3_scale(primitive.emissive_color, SDL_max(emissive_intensity, 0.0f));
        }
        const float alpha_scale =
            slayer3d_properties_get_float(actor->props, json_string(component, "alpha_scale_property", NULL), 1.0f);
        primitive.color.a = (Uint8)SDL_clamp((int)((float)primitive.color.a * alpha_scale + 0.5f), 0, 255);
        primitive.wire_color = json_color(component, "wire_color", (slayer3d_color){0, 0, 0, 255});
        primitive.size = slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
        primitive.radius = 0.5f;
        primitive.height = primitive.size.y;
        primitive.radius_top = primitive.radius;
        primitive.radius_bottom = primitive.radius;
        primitive.major_radius = 0.5f;
        primitive.minor_radius = 0.15f;
        primitive.bevel_radius = 0.15f;
        primitive.arc_angle = 3.1415927f;
        primitive.slices = 24;
        primitive.rings = 8;
        primitive.tube_segments = primitive.rings;
        primitive.lod_enabled = json_bool(component, "lod", true);
        primitive.lod_bias = SDL_max(json_float(component, "lod_bias", 1.0f), 0.001f);

        if (SDL_strcmp(type, "render.cube") == 0)
        {
            primitive.type = SLAYER3D_GAME_DATA_RENDER_CUBE;
            primitive.size = json_vec3(component, "size", slayer3d_vec3_make(1.0f, 1.0f, 1.0f));
            const char *size_property = json_string(component, "size_property", NULL);
            if (size_property != NULL)
                primitive.size = slayer3d_properties_get_vec3(actor->props, size_property, primitive.size);
        }
        else if (SDL_strcmp(type, "render.sphere") == 0)
        {
            primitive.type = SLAYER3D_GAME_DATA_RENDER_SPHERE;
            primitive.radius = json_float(component, "radius", 0.5f);
            primitive.slices = json_int(component, "slices", 16);
            primitive.rings = json_int(component, "rings", 8);
        }
        else if (SDL_strcmp(type, "render.mesh_primitive") == 0)
        {
            populate_mesh_primitive_descriptor(actor, component, &primitive);
        }
        else if (SDL_strcmp(type, "render.composite") == 0)
        {
            yyjson_val *parts = obj_get(component, "parts");
            if (!yyjson_is_arr(parts))
                continue;
            for (size_t p = 0; p < yyjson_arr_size(parts); ++p)
            {
                yyjson_val *part = yyjson_arr_get(parts, p);
                slayer3d_game_data_render_primitive part_primitive = primitive;
                const slayer3d_vec3 part_offset = json_vec3(part, "offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
                part_primitive.position.x += part_offset.x;
                part_primitive.position.y += part_offset.y;
                part_primitive.position.z += part_offset.z;
                part_primitive.rotation_axis = json_vec3(part, "rotation_axis", part_primitive.rotation_axis);
                part_primitive.rotation_angle = json_float(part, "rotation_angle", part_primitive.rotation_angle);
                const char *part_rotation_property = json_string(part, "rotation_property", NULL);
                if (part_rotation_property != NULL)
                    part_primitive.rotation_angle +=
                        slayer3d_properties_get_float(actor->props, part_rotation_property, 0.0f);
                part_primitive.color = json_color(part, "color", part_primitive.color);
                part_primitive.texture_image = json_string(part, "texture", part_primitive.texture_image);
                part_primitive.lighting_enabled =
                    render_component_lighting_enabled(runtime, part, part_primitive.lighting_enabled);
                part_primitive.emissive = json_bool(part, "emissive", part_primitive.emissive);
                part_primitive.emissive_color =
                    part_primitive.emissive ? slayer3d_vec3_make(0.2f, 0.2f, 0.2f) : part_primitive.emissive_color;
                part_primitive.wire_color = json_color(part, "wire_color", part_primitive.wire_color);
                part_primitive.lod_enabled = json_bool(part, "lod", part_primitive.lod_enabled);
                part_primitive.lod_bias = SDL_max(json_float(part, "lod_bias", part_primitive.lod_bias), 0.001f);
                populate_mesh_primitive_descriptor(actor, part, &part_primitive);
                if (eval != NULL)
                {
                    apply_render_effects(runtime, component, eval, &part_primitive);
                    apply_render_effects(runtime, part, eval, &part_primitive);
                }
                apply_sector_lighting_to_render_primitive(runtime, &part_primitive);
                if (!callback(userdata, &part_primitive))
                    return false;
            }
            continue;
        }
        else if (SDL_strcmp(type, "render.sprite") == 0)
        {
            primitive.type = SLAYER3D_GAME_DATA_RENDER_SPRITE;
            primitive.sprite_asset = json_string(component, "sprite", NULL);
            (void)json_vec2_value(obj_get(component, "size"), 1.0f, 1.0f, &primitive.sprite_size.x,
                                  &primitive.sprite_size.y);
            primitive.sprite_facing_yaw = json_float(component, "facing_yaw", 0.0f);
            const char *facing_yaw_property = json_string(component, "facing_yaw_property", NULL);
            if (facing_yaw_property != NULL)
                primitive.sprite_facing_yaw += slayer3d_properties_get_float(actor->props, facing_yaw_property, 0.0f);
        }
        else if (SDL_strcmp(type, "render.model") == 0)
        {
            primitive.type = SLAYER3D_GAME_DATA_RENDER_MODEL;
            primitive.model_asset = json_string(component, "model", NULL);
            primitive.view_space = SDL_strcmp(space, "camera") == 0;
            if (primitive.view_space)
                primitive.position = offset;
            primitive.model_scale = json_vec3(component, "scale", slayer3d_vec3_make(1.0f, 1.0f, 1.0f));
            const char *scale_property = json_string(component, "scale_property", NULL);
            if (scale_property != NULL)
            {
                const float scale = slayer3d_properties_get_float(actor->props, scale_property, 1.0f);
                primitive.model_scale.x *= scale;
                primitive.model_scale.y *= scale;
                primitive.model_scale.z *= scale;
            }
            primitive.euler_rotation = json_vec3(component, "rotation", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
            const char *pitch_property = json_string(component, "pitch_property", NULL);
            const char *yaw_property = json_string(component, "yaw_property", NULL);
            const char *roll_property = json_string(component, "roll_property", NULL);
            const char *pitch_add_property = json_string(component, "pitch_add_property", NULL);
            const char *yaw_add_property = json_string(component, "yaw_add_property", NULL);
            const char *roll_add_property = json_string(component, "roll_add_property", NULL);
            if (pitch_property != NULL)
                primitive.euler_rotation.x += slayer3d_properties_get_float(actor->props, pitch_property, 0.0f);
            if (yaw_property != NULL)
                primitive.euler_rotation.y += slayer3d_properties_get_float(actor->props, yaw_property, 0.0f);
            if (roll_property != NULL)
                primitive.euler_rotation.z += slayer3d_properties_get_float(actor->props, roll_property, 0.0f);
            if (pitch_add_property != NULL)
                primitive.euler_rotation.x += slayer3d_properties_get_float(actor->props, pitch_add_property, 0.0f);
            if (yaw_add_property != NULL)
                primitive.euler_rotation.y += slayer3d_properties_get_float(actor->props, yaw_add_property, 0.0f);
            if (roll_add_property != NULL)
                primitive.euler_rotation.z += slayer3d_properties_get_float(actor->props, roll_add_property, 0.0f);
            primitive.euler_rotation.x += render_component_property_list_sum(actor, component, "pitch_add_properties");
            primitive.euler_rotation.y += render_component_property_list_sum(actor, component, "yaw_add_properties");
            primitive.euler_rotation.z += render_component_property_list_sum(actor, component, "roll_add_properties");
            primitive.animation_clip = json_int(component, "animation_clip", -1);
            primitive.animation_time = json_float(component, "animation_time", 0.0f);
            primitive.animation_loop = json_bool(component, "animation_loop", true);
            const char *animation_time_property = json_string(component, "animation_time_property", NULL);
            if (animation_time_property != NULL)
                primitive.animation_time =
                    slayer3d_properties_get_float(actor->props, animation_time_property, primitive.animation_time);
        }
        else
        {
            continue;
        }

        if (eval != NULL)
            apply_render_effects(runtime, component, eval, &primitive);
        apply_sector_lighting_to_render_primitive(runtime, &primitive);
        if (!callback(userdata, &primitive))
            return false;
    }

    return true;
}

static bool emit_grid_pickup_layer_render_primitives(const slayer3d_game_data_runtime *runtime,
                                                     grid_pickup_layer_runtime *layer,
                                                     slayer3d_game_data_render_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || layer == NULL || layer->map == NULL || layer->cells == NULL || callback == NULL ||
        layer->active_count <= 0)
    {
        return true;
    }

    if (layer->render_position_capacity < layer->active_count)
    {
        slayer3d_vec3 *positions =
            (slayer3d_vec3 *)SDL_realloc(layer->render_positions, (size_t)layer->active_count * sizeof(*positions));
        if (positions == NULL)
            return false;
        layer->render_positions = positions;
        layer->render_position_capacity = layer->active_count;
    }

    for (int kind_index = 0; kind_index < layer->kind_count; ++kind_index)
    {
        const grid_pickup_kind_runtime *kind = &layer->kinds[kind_index];
        int count = 0;
        for (int row = 0; row < layer->map->height; ++row)
        {
            for (int col = 0; col < layer->map->width; ++col)
            {
                if (layer->cells[row * layer->map->width + col] != (Uint8)(kind_index + 1))
                    continue;
                slayer3d_vec3 position;
                if (!grid_map_cell_to_world(layer->map, col, row, &position))
                    continue;
                position.z = kind->z;
                layer->render_positions[count++] = position;
            }
        }
        if (count <= 0)
            continue;

        slayer3d_game_data_render_primitive primitive;
        SDL_zero(primitive);
        primitive.entity_name = layer->name;
        primitive.type = SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH;
        primitive.radius = kind->radius;
        primitive.rings = kind->rings;
        primitive.slices = kind->slices;
        primitive.color = kind->color;
        primitive.lighting_enabled = kind->lighting;
        primitive.emissive = kind->emissive;
        primitive.emissive_color =
            kind->emissive ? slayer3d_vec3_make(0.25f, 0.22f, 0.08f) : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        primitive.lod_enabled = true;
        primitive.lod_bias = 1.0f;
        primitive.instances = layer->render_positions;
        primitive.instance_count = count;
        if (!callback(userdata, &primitive))
            return false;
    }
    return true;
}

static bool emit_sector_door_render_primitives(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_render_eval *eval,
                                               slayer3d_game_data_render_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return true;

    for (int door_index = 0; door_index < runtime->sector_door_count; ++door_index)
    {
        const sector_door_runtime *door = &runtime->sector_doors[door_index];
        if (!sector_door_in_active_scene(runtime, door))
            continue;

        yyjson_val *render = obj_get(door->json, "render");
        for (int panel_index = 0; panel_index < slayer3d_door_panel_count(&door->door); ++panel_index)
        {
            slayer3d_bounding_box bounds;
            if (!slayer3d_door_get_panel_bounds(&door->door, panel_index, &bounds))
                continue;

            slayer3d_game_data_render_primitive primitive;
            SDL_zero(primitive);
            primitive.entity_name = door->door.name;
            primitive.type = SLAYER3D_GAME_DATA_RENDER_CUBE;
            primitive.position =
                slayer3d_vec3_make((bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f,
                                   (bounds.min.z + bounds.max.z) * 0.5f);
            primitive.size = slayer3d_vec3_make(SDL_max(bounds.max.x - bounds.min.x, 0.001f),
                                                SDL_max(bounds.max.y - bounds.min.y, 0.001f),
                                                SDL_max(bounds.max.z - bounds.min.z, 0.001f));
            primitive.color = json_color(render, "color", (slayer3d_color){170, 185, 205, 255});
            primitive.texture_image = json_string(render, "texture", NULL);
            primitive.lighting_enabled = render_component_lighting_enabled(runtime, render, true);
            primitive.emissive = json_bool(render, "emissive", false);
            primitive.emissive_color =
                primitive.emissive ? json_vec3(render, "emissive_color", slayer3d_vec3_make(0.12f, 0.15f, 0.18f))
                                   : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
            if (eval != NULL)
                apply_render_effects(runtime, render, eval, &primitive);
            apply_sector_lighting_to_render_primitive(runtime, &primitive);
            if (!callback(userdata, &primitive))
                return false;
        }
    }
    return true;
}

static bool for_each_render_primitive_internal(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_render_eval *eval,
                                               slayer3d_game_data_render_primitive_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)runtime;
    actor_lifecycle_defer_begin(mutable_runtime);
    bool keep_iterating = true;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; keep_iterating && yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (actor == NULL || !actor->active)
            continue;
        keep_iterating =
            emit_actor_render_primitives(runtime, eval, actor, obj_get(entity, "components"), callback, userdata);
    }
    for (int pool_index = 0; keep_iterating && pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
            continue;
        yyjson_val *components = obj_get(pool->archetype_json, "components");
        for (int actor_index = 0; keep_iterating && actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (!actor_pool_actor_is_active(pool, actor, actor_index))
                continue;
            keep_iterating = emit_actor_render_primitives(runtime, eval, actor, components, callback, userdata);
        }
    }
    for (int layer_index = 0; keep_iterating && layer_index < runtime->grid_pickup_layer_count; ++layer_index)
    {
        keep_iterating = emit_grid_pickup_layer_render_primitives(
            runtime, &mutable_runtime->grid_pickup_layers[layer_index], callback, userdata);
    }
    if (keep_iterating)
        keep_iterating = emit_sector_door_render_primitives(runtime, eval, callback, userdata);
    actor_lifecycle_defer_end(mutable_runtime);
    return true;
}

bool slayer3d_game_data_for_each_render_primitive(const slayer3d_game_data_runtime *runtime,
                                                  slayer3d_game_data_render_primitive_fn callback, void *userdata)
{
    return for_each_render_primitive_internal(runtime, NULL, callback, userdata);
}

bool slayer3d_game_data_for_each_render_primitive_evaluated(const slayer3d_game_data_runtime *runtime,
                                                            const slayer3d_game_data_render_eval *eval,
                                                            slayer3d_game_data_render_primitive_fn callback,
                                                            void *userdata)
{
    return for_each_render_primitive_internal(runtime, eval, callback, userdata);
}

bool slayer3d_game_data_get_particle_emitter(const slayer3d_game_data_runtime *runtime, const char *entity_name,
                                             slayer3d_particle_config *out_config)
{
    if (out_config != NULL)
        SDL_zero(*out_config);
    if (runtime == NULL || entity_name == NULL || out_config == NULL)
        return false;

    yyjson_val *entity = find_actor_definition_json(runtime, entity_name);
    yyjson_val *component = find_component_json(entity, "particles.emitter");
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
    if (component == NULL || actor == NULL)
        return false;

    slayer3d_vec3 position_offset = json_vec3(component, "position_offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    const char *position_offset_property = json_string(component, "position_offset_property", NULL);
    const slayer3d_value *position_offset_value =
        position_offset_property != NULL ? slayer3d_properties_get_value(actor->props, position_offset_property) : NULL;
    if (position_offset_value != NULL && position_offset_value->type == SLAYER3D_VALUE_VEC3)
        position_offset = position_offset_value->as_vec3;
    position_offset.x +=
        slayer3d_properties_get_float(actor->props, json_string(component, "position_offset_x_property", NULL), 0.0f);
    position_offset.y +=
        slayer3d_properties_get_float(actor->props, json_string(component, "position_offset_y_property", NULL), 0.0f);
    position_offset.z +=
        slayer3d_properties_get_float(actor->props, json_string(component, "position_offset_z_property", NULL), 0.0f);
    out_config->position = slayer3d_vec3_add(actor->position, position_offset);
    if (particle_emitter_component_is_view_space(component))
        out_config->position.z = -out_config->position.z;
    out_config->direction = json_vec3(component, "direction", slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    out_config->spread = json_float(component, "spread", 0.0f);
    out_config->speed_min = json_float(component, "speed_min", 0.0f);
    out_config->speed_max = json_float(component, "speed_max", 0.0f);
    out_config->lifetime_min = json_float(component, "lifetime_min", 1.0f);
    out_config->lifetime_max = json_float(component, "lifetime_max", 1.0f);
    out_config->size_start = json_float(component, "size_start", 0.05f);
    out_config->size_end = json_float(component, "size_end", 0.01f);
    out_config->size_start = slayer3d_properties_get_float(
        actor->props, json_string(component, "size_start_property", NULL), out_config->size_start);
    out_config->size_end = slayer3d_properties_get_float(
        actor->props, json_string(component, "size_end_property", NULL), out_config->size_end);
    float size_scale =
        slayer3d_properties_get_float(actor->props, json_string(component, "size_scale_property", NULL), 1.0f);
    if (size_scale < 0.0f)
        size_scale = 0.0f;
    out_config->size_start *= size_scale;
    out_config->size_end *= size_scale;
    out_config->color_start = json_color(component, "color_start", (slayer3d_color){255, 255, 255, 255});
    out_config->color_end = json_color(component, "color_end", (slayer3d_color){255, 255, 255, 0});
    const float alpha_scale =
        slayer3d_properties_get_float(actor->props, json_string(component, "alpha_scale_property", NULL), 1.0f);
    out_config->color_start.a = (Uint8)SDL_clamp((int)((float)out_config->color_start.a * alpha_scale + 0.5f), 0, 255);
    out_config->color_end.a = (Uint8)SDL_clamp((int)((float)out_config->color_end.a * alpha_scale + 0.5f), 0, 255);
    out_config->gravity = json_float(component, "gravity", 0.0f);
    out_config->max_particles = json_int(component, "max_particles", 128);
    out_config->emit_rate = json_float(component, "emit_rate", 0.0f);
    out_config->emit_rate = slayer3d_properties_get_float(
        actor->props, json_string(component, "emit_rate_property", NULL), out_config->emit_rate);
    if (out_config->emit_rate < 0.0f)
        out_config->emit_rate = 0.0f;
    const char *shape = json_string(component, "shape", "point");
    if (SDL_strcmp(shape, "box") == 0)
        out_config->shape = SLAYER3D_PARTICLE_EMITTER_BOX;
    else if (SDL_strcmp(shape, "circle") == 0)
        out_config->shape = SLAYER3D_PARTICLE_EMITTER_CIRCLE;
    else
        out_config->shape = SLAYER3D_PARTICLE_EMITTER_POINT;
    out_config->extents = json_vec3(component, "extents", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    out_config->radius = json_float(component, "radius", 0.0f);
    out_config->emissive_intensity = json_float(component, "emissive_intensity", 1.0f);
    out_config->emissive_intensity = slayer3d_properties_get_float(
        actor->props, json_string(component, "emissive_intensity_property", NULL), out_config->emissive_intensity);
    const char *render_style = json_string(component, "render_style", "default");
    if (SDL_strcmp(render_style, "soft_smoke") == 0)
        out_config->render_style = SLAYER3D_PARTICLE_RENDER_SOFT_SMOKE;
    else if (SDL_strcmp(render_style, "soft_fire") == 0)
        out_config->render_style = SLAYER3D_PARTICLE_RENDER_SOFT_FIRE;
    else if (SDL_strcmp(render_style, "muzzle_flash") == 0)
        out_config->render_style = SLAYER3D_PARTICLE_RENDER_MUZZLE_FLASH;
    else
        out_config->render_style = SLAYER3D_PARTICLE_RENDER_DEFAULT;
    out_config->camera_facing = json_bool(component, "camera_facing", true);
    out_config->depth_test = json_bool(component, "depth_test", true);
    out_config->additive_blend = json_bool(component, "additive_blend", false);
    out_config->texture = NULL;
    out_config->random_seed = (Uint32)json_int(component, "random_seed", 0);
    return true;
}

bool slayer3d_game_data_get_particle_emitter_draw_emissive(const slayer3d_game_data_runtime *runtime,
                                                           const char *entity_name, slayer3d_vec3 *out_rgb)
{
    if (out_rgb != NULL)
        *out_rgb = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (runtime == NULL || entity_name == NULL || out_rgb == NULL)
        return false;

    yyjson_val *entity = find_actor_definition_json(runtime, entity_name);
    yyjson_val *component = find_component_json(entity, "particles.emitter");
    if (component == NULL)
        return false;

    *out_rgb = json_vec3(component, "draw_emissive", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return true;
}

bool slayer3d_game_data_for_each_particle_emitter(const slayer3d_game_data_runtime *runtime,
                                                  slayer3d_game_data_particle_emitter_fn callback, void *userdata)
{
    if (runtime == NULL || callback == NULL)
        return false;

    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)runtime;
    actor_lifecycle_defer_begin(mutable_runtime);
    bool keep_iterating = true;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; keep_iterating && yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;

        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        yyjson_val *component = find_component_json(entity, "particles.emitter");
        if (actor == NULL || !actor->active || component == NULL)
            continue;
        if (!component_visible_for_active_camera(runtime, component))
            continue;

        slayer3d_game_data_particle_emitter emitter;
        SDL_zero(emitter);
        emitter.entity_name = entity_name;
        if (!slayer3d_game_data_get_particle_emitter(runtime, entity_name, &emitter.config))
            continue;
        emitter.view_space = particle_emitter_component_is_view_space(component);
        (void)slayer3d_game_data_get_particle_emitter_draw_emissive(runtime, entity_name, &emitter.draw_emissive);

        if (!callback(userdata, &emitter))
            keep_iterating = false;
    }
    for (int pool_index = 0; keep_iterating && pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        yyjson_val *pool_component = find_component_json(pool->archetype_json, "particles.emitter");
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)) || pool_component == NULL)
        {
            continue;
        }
        if (!component_visible_for_active_camera(runtime, pool_component))
            continue;

        for (int actor_index = 0; keep_iterating && actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (!actor_pool_actor_is_active(pool, actor, actor_index))
                continue;

            slayer3d_game_data_particle_emitter emitter;
            SDL_zero(emitter);
            emitter.entity_name = actor->name;
            if (!slayer3d_game_data_get_particle_emitter(runtime, actor->name, &emitter.config))
                continue;
            emitter.view_space = particle_emitter_component_is_view_space(pool_component);
            (void)slayer3d_game_data_get_particle_emitter_draw_emissive(runtime, actor->name, &emitter.draw_emissive);

            if (!callback(userdata, &emitter))
                keep_iterating = false;
        }
    }
    actor_lifecycle_defer_end(mutable_runtime);
    return true;
}

bool slayer3d_game_data_get_render_settings(const slayer3d_game_data_runtime *runtime,
                                            slayer3d_game_data_render_settings *out_settings)
{
    if (out_settings != NULL)
    {
        SDL_zero(*out_settings);
        out_settings->clear_color = (slayer3d_color){0, 0, 0, 255};
        out_settings->lighting_enabled = true;
        out_settings->bloom_enabled = true;
        out_settings->ssao_enabled = true;
        out_settings->depth_prepass_enabled = false;
        out_settings->per_object_light_selection_enabled = false;
        out_settings->per_object_light_limit = SLAYER3D_MAX_SHADER_LIGHTS;
        out_settings->procedural_lod_enabled = false;
        out_settings->procedural_lod_near_pixels = 128.0f;
        out_settings->procedural_lod_far_pixels = 24.0f;
        out_settings->procedural_lod_min_segments = 8;
        out_settings->performance_queries_enabled = false;
        out_settings->world_render_scale = 1.0f;
        out_settings->tonemap = SLAYER3D_TONEMAP_ACES;
    }
    if (runtime == NULL || out_settings == NULL)
        return false;

    yyjson_val *render = obj_get(runtime_root(runtime), "render");
    if (!yyjson_is_obj(render))
        return true;

    out_settings->clear_color = json_color(render, "clear_color", out_settings->clear_color);
    out_settings->lighting_enabled = scene_state_bool(runtime, json_string(render, "lighting_key", NULL),
                                                      json_bool(render, "lighting", out_settings->lighting_enabled));
    out_settings->bloom_enabled = scene_state_bool(runtime, json_string(render, "bloom_key", NULL),
                                                   json_bool(render, "bloom", out_settings->bloom_enabled));
    out_settings->ssao_enabled = scene_state_bool(runtime, json_string(render, "ssao_key", NULL),
                                                  json_bool(render, "ssao", out_settings->ssao_enabled));
    out_settings->depth_prepass_enabled =
        scene_state_bool(runtime, json_string(render, "depth_prepass_key", NULL),
                         json_bool(render, "depth_prepass", out_settings->depth_prepass_enabled));
    out_settings->per_object_light_selection_enabled = scene_state_bool(
        runtime, json_string(render, "per_object_light_selection_key", NULL),
        json_bool(render, "per_object_light_selection", out_settings->per_object_light_selection_enabled));
    out_settings->per_object_light_limit =
        scene_state_int(runtime, json_string(render, "per_object_light_limit_key", NULL),
                        json_int(render, "per_object_light_limit", out_settings->per_object_light_limit));
    out_settings->per_object_light_limit =
        SDL_clamp(out_settings->per_object_light_limit, 0, SLAYER3D_MAX_SHADER_LIGHTS);
    out_settings->procedural_lod_enabled =
        scene_state_bool(runtime, json_string(render, "procedural_lod_key", NULL),
                         json_bool(render, "procedural_lod", out_settings->procedural_lod_enabled));
    out_settings->procedural_lod_near_pixels =
        scene_state_float(runtime, json_string(render, "procedural_lod_near_pixels_key", NULL),
                          json_float(render, "procedural_lod_near_pixels", out_settings->procedural_lod_near_pixels));
    out_settings->procedural_lod_far_pixels =
        scene_state_float(runtime, json_string(render, "procedural_lod_far_pixels_key", NULL),
                          json_float(render, "procedural_lod_far_pixels", out_settings->procedural_lod_far_pixels));
    out_settings->procedural_lod_min_segments =
        scene_state_int(runtime, json_string(render, "procedural_lod_min_segments_key", NULL),
                        json_int(render, "procedural_lod_min_segments", out_settings->procedural_lod_min_segments));
    out_settings->procedural_lod_near_pixels = SDL_max(out_settings->procedural_lod_near_pixels, 1.0f);
    out_settings->procedural_lod_far_pixels =
        SDL_clamp(out_settings->procedural_lod_far_pixels, 1.0f, out_settings->procedural_lod_near_pixels);
    out_settings->procedural_lod_min_segments = SDL_clamp(out_settings->procedural_lod_min_segments, 3, 64);
    out_settings->performance_queries_enabled =
        scene_state_bool(runtime, json_string(render, "performance_queries_key", NULL),
                         json_bool(render, "performance_queries", out_settings->performance_queries_enabled));
    out_settings->world_render_scale =
        scene_state_float(runtime, json_string(render, "world_render_scale_key", NULL),
                          json_float(render, "world_render_scale", out_settings->world_render_scale));
    out_settings->world_render_scale = SDL_clamp(out_settings->world_render_scale, 0.25f, 1.0f);
    const char *tonemap_name =
        scene_state_string(runtime, json_string(render, "tonemap_key", NULL), json_string(render, "tonemap", NULL));
    out_settings->tonemap = parse_tonemap(tonemap_name, out_settings->tonemap);

    const char *profile_name =
        scene_state_string(runtime, json_string(render, "profile_key", NULL), json_string(render, "profile", NULL));
    slayer3d_render_profile profile;
    if (parse_render_profile(profile_name, &profile))
    {
        out_settings->has_profile = true;
        out_settings->profile = profile;
        out_settings->profile_name = profile_name;
        if (tonemap_name == NULL)
            out_settings->tonemap = profile.tonemap;
    }
    return true;
}

bool slayer3d_game_data_get_transition(const slayer3d_game_data_runtime *runtime, const char *name,
                                       slayer3d_game_data_transition_desc *out_transition)
{
    if (out_transition != NULL)
    {
        SDL_zero(*out_transition);
        out_transition->type = SLAYER3D_TRANSITION_FADE;
        out_transition->direction = SLAYER3D_TRANSITION_IN;
        out_transition->color = (slayer3d_color){0, 0, 0, 255};
        out_transition->duration = 0.5f;
        out_transition->done_signal_id = -1;
    }
    if (runtime == NULL || name == NULL || out_transition == NULL)
        return false;

    yyjson_val *transition = obj_get(obj_get(runtime_root(runtime), "transitions"), name);
    if (!yyjson_is_obj(transition))
        return false;

    out_transition->type = parse_transition_type(json_string(transition, "type", NULL), out_transition->type);
    out_transition->direction =
        parse_transition_direction(json_string(transition, "direction", NULL), out_transition->direction);
    out_transition->color = json_color(transition, "color", out_transition->color);
    out_transition->duration = json_float(transition, "duration", out_transition->duration);
    const char *done_signal = json_string(transition, "done_signal", NULL);
    out_transition->done_signal_id =
        done_signal != NULL ? slayer3d_game_data_find_signal(runtime, done_signal) : out_transition->done_signal_id;
    return true;
}

/**
 * @file game_data_validation_assets.c
 * @brief Asset validation for JSON-authored game data.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/sprite_actor.h"

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static bool asset_is_non_empty_string(yyjson_val *object, const char *key)
{
    const char *value = json_string(object, key);
    return value != NULL && value[0] != '\0';
}

bool validation_audio_bus_name_valid(const char *bus)
{
    return bus == NULL || SDL_strcmp(bus, "sound_effects") == 0 || SDL_strcmp(bus, "sfx") == 0 ||
           SDL_strcmp(bus, "music") == 0 || SDL_strcmp(bus, "dialogue") == 0 || SDL_strcmp(bus, "dialog") == 0 ||
           SDL_strcmp(bus, "ambience") == 0 || SDL_strcmp(bus, "ambiance") == 0 || SDL_strcmp(bus, "ambient") == 0;
}

bool collect_font_assets(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *fonts = obj_get(obj_get(root, "assets"), "fonts");
    if (fonts == NULL)
        return true;
    if (!yyjson_is_arr(fonts))
        return validation_error(ctx, "$.assets.fonts", "font assets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(fonts); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.fonts[%zu]", i);
        yyjson_val *font = yyjson_arr_get(fonts, i);
        if (!yyjson_is_obj(font))
            return validation_error(ctx, path, "font asset entries must be objects");
        if (!require_unique_name(ctx, &names->fonts, "font asset", json_string(font, "id"), path))
            return false;
        if (!asset_is_non_empty_string(font, "builtin") && !asset_is_non_empty_string(font, "path"))
            return validation_error(ctx, path, "font asset requires builtin or path");
    }
    return true;
}

bool collect_sprite_assets(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *sprites = obj_get(obj_get(root, "assets"), "sprites");
    if (sprites == NULL)
        return true;
    if (!yyjson_is_arr(sprites))
        return validation_error(ctx, "$.assets.sprites", "sprite assets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(sprites); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.sprites[%zu]", i);
        yyjson_val *sprite = yyjson_arr_get(sprites, i);
        if (!yyjson_is_obj(sprite))
            return validation_error(ctx, path, "sprite asset entries must be objects");
        if (!require_unique_name(ctx, &names->sprites, "sprite asset", json_string(sprite, "id"), path))
            return false;
        const char *kind = json_string(sprite, "kind");
        if (kind == NULL)
            kind = "sheet";
        if (SDL_strcmp(kind, "sheet") != 0 && SDL_strcmp(kind, "files") != 0)
            return validation_error(ctx, path, "sprite asset kind must be 'sheet' or 'files'");
        if (SDL_strcmp(kind, "sheet") == 0)
        {
            if (!asset_is_non_empty_string(sprite, "path"))
                return validation_error(ctx, path, "sprite sheet asset requires a non-empty path");
            if (!asset_path_exists(ctx, json_string(sprite, "path"), path, "sprite"))
                return false;
        }
        else
        {
            yyjson_val *base_paths = obj_get(sprite, "base_paths");
            yyjson_val *frame_paths = obj_get(sprite, "frame_paths");
            yyjson_val *frame_count_value = obj_get(sprite, "frame_count");
            yyjson_val *direction_count_value = obj_get(sprite, "direction_count");
            const int frame_count = yyjson_is_int(frame_count_value) ? (int)yyjson_get_int(frame_count_value) : 0;
            const int direction_count =
                yyjson_is_int(direction_count_value) ? (int)yyjson_get_int(direction_count_value) : 0;
            if (!yyjson_is_arr(base_paths) || !yyjson_is_arr(frame_paths))
                return validation_error(ctx, path, "sprite file-list assets require base_paths and frame_paths arrays");
            if (direction_count <= 0 || frame_count <= 0)
                return validation_error(ctx, path,
                                        "sprite file-list assets require positive frame_count and direction_count");
            if ((int)yyjson_arr_size(base_paths) != direction_count)
                return validation_error(ctx, path, "sprite file-list base_paths count must match direction_count");
            if ((int)yyjson_arr_size(frame_paths) != frame_count * direction_count)
                return validation_error(ctx, path,
                                        "sprite file-list frame_paths count must match frame_count * direction_count");
            for (size_t path_index = 0; path_index < yyjson_arr_size(base_paths); ++path_index)
            {
                yyjson_val *entry = yyjson_arr_get(base_paths, path_index);
                const char *asset_path = yyjson_get_str(entry);
                if (asset_path == NULL || asset_path[0] == '\0')
                    return validation_error(ctx, path, "sprite file-list base paths must be non-empty strings");
                if (!asset_path_exists(ctx, asset_path, path, "sprite"))
                    return false;
            }
            for (size_t path_index = 0; path_index < yyjson_arr_size(frame_paths); ++path_index)
            {
                yyjson_val *entry = yyjson_arr_get(frame_paths, path_index);
                const char *asset_path = yyjson_get_str(entry);
                if (asset_path == NULL || asset_path[0] == '\0')
                    return validation_error(ctx, path, "sprite file-list frame paths must be non-empty strings");
                if (!asset_path_exists(ctx, asset_path, path, "sprite"))
                    return false;
            }
        }
        yyjson_val *direction_count_value = obj_get(sprite, "direction_count");
        const int direction_count =
            yyjson_is_int(direction_count_value) ? (int)yyjson_get_int(direction_count_value) : 1;
        if (direction_count <= 0 || direction_count > SLAYER3D_SPRITE_ROTATION_COUNT)
            return validation_error(ctx, path, "sprite direction_count must be between 1 and 8");
        const char *shader_vertex_path = json_string(sprite, "shader_vertex_path");
        const char *shader_fragment_path = json_string(sprite, "shader_fragment_path");
        if (shader_vertex_path != NULL && shader_vertex_path[0] != '\0' &&
            (shader_fragment_path == NULL || shader_fragment_path[0] == '\0'))
        {
            return validation_error(ctx, path, "sprite shader_vertex_path requires shader_fragment_path");
        }
        if (shader_vertex_path != NULL && shader_vertex_path[0] != '\0' &&
            !asset_path_exists(ctx, shader_vertex_path, path, "sprite shader"))
        {
            return false;
        }
        if (shader_fragment_path != NULL && shader_fragment_path[0] != '\0' &&
            !asset_path_exists(ctx, shader_fragment_path, path, "sprite shader"))
        {
            return false;
        }
        const char *effect = json_string(sprite, "effect");
        if (effect != NULL && effect[0] != '\0' && SDL_strcasecmp(effect, "melt") != 0)
            return validation_error(ctx, path, "unsupported sprite asset effect '%s'", effect);
        yyjson_val *effect_delay = obj_get(sprite, "effect_delay");
        yyjson_val *effect_duration = obj_get(sprite, "effect_duration");
        if (yyjson_is_num(effect_delay) && (float)yyjson_get_real(effect_delay) < 0.0f)
            return validation_error(ctx, path, "sprite asset effect_delay must be non-negative");
        if (yyjson_is_num(effect_duration) && (float)yyjson_get_real(effect_duration) <= 0.0f)
            return validation_error(ctx, path, "sprite asset effect_duration must be positive");
    }
    return true;
}

bool collect_image_assets(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *images = obj_get(obj_get(root, "assets"), "images");
    if (images == NULL)
        return true;
    if (!yyjson_is_arr(images))
        return validation_error(ctx, "$.assets.images", "image assets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(images); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.images[%zu]", i);
        yyjson_val *image = yyjson_arr_get(images, i);
        if (!yyjson_is_obj(image))
            return validation_error(ctx, path, "image asset entries must be objects");
        if (!require_unique_name(ctx, &names->images, "image asset", json_string(image, "id"), path))
            return false;
        const char *image_path = json_string(image, "path");
        const char *sprite_id = json_string(image, "sprite");
        if (!asset_is_non_empty_string(image, "path") && !asset_is_non_empty_string(image, "sprite"))
            return validation_error(ctx, path, "image asset requires path or sprite");

        if (sprite_id != NULL && !require_ref(ctx, &names->sprites, "sprite asset", sprite_id, path))
            return false;
        if (image_path != NULL && !asset_path_exists(ctx, image_path, path, "image"))
            return false;
    }
    return true;
}

bool collect_model_assets(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *models = obj_get(obj_get(root, "assets"), "models");
    if (models == NULL)
        return true;
    if (!yyjson_is_arr(models))
        return validation_error(ctx, "$.assets.models", "model assets must be an array");

    for (size_t i = 0; i < yyjson_arr_size(models); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.models[%zu]", i);
        yyjson_val *model = yyjson_arr_get(models, i);
        if (!yyjson_is_obj(model))
            return validation_error(ctx, path, "model asset entries must be objects");
        if (!require_unique_name(ctx, &names->models, "model asset", json_string(model, "id"), path))
            return false;
        if (!asset_is_non_empty_string(model, "path"))
            return validation_error(ctx, path, "model asset requires path");
        if (!asset_path_exists(ctx, json_string(model, "path"), path, "model"))
            return false;
    }
    return true;
}

bool collect_audio_assets(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *assets = obj_get(root, "assets");
    yyjson_val *sounds = obj_get(assets, "sounds");
    if (sounds != NULL && !yyjson_is_arr(sounds))
        return validation_error(ctx, "$.assets.sounds", "sound assets must be an array");
    for (size_t i = 0; yyjson_is_arr(sounds) && i < yyjson_arr_size(sounds); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.sounds[%zu]", i);
        yyjson_val *sound = yyjson_arr_get(sounds, i);
        if (!yyjson_is_obj(sound))
            return validation_error(ctx, path, "sound asset entries must be objects");
        if (!require_unique_name(ctx, &names->sounds, "sound asset", json_string(sound, "id"), path))
            return false;
        if (!asset_is_non_empty_string(sound, "path"))
            return validation_error(ctx, path, "sound asset requires a non-empty path");
        if (!validation_audio_bus_name_valid(json_string(sound, "bus")))
            return validation_error(ctx, path, "sound asset bus must be sfx, music, dialogue, or ambience");
        if (!asset_path_exists(ctx, json_string(sound, "path"), path, "sound"))
            return false;
    }

    yyjson_val *music = obj_get(assets, "music");
    if (music != NULL && !yyjson_is_arr(music))
        return validation_error(ctx, "$.assets.music", "music assets must be an array");
    for (size_t i = 0; yyjson_is_arr(music) && i < yyjson_arr_size(music); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.music[%zu]", i);
        yyjson_val *track = yyjson_arr_get(music, i);
        if (!yyjson_is_obj(track))
            return validation_error(ctx, path, "music asset entries must be objects");
        if (!require_unique_name(ctx, &names->music, "music asset", json_string(track, "id"), path))
            return false;
        if (!asset_is_non_empty_string(track, "path"))
            return validation_error(ctx, path, "music asset requires a non-empty path");
        if (!asset_path_exists(ctx, json_string(track, "path"), path, "music"))
            return false;
    }

    yyjson_val *ambient = obj_get(assets, "ambient");
    if (ambient != NULL && !yyjson_is_arr(ambient))
        return validation_error(ctx, "$.assets.ambient", "ambient assets must be an array");
    for (size_t i = 0; yyjson_is_arr(ambient) && i < yyjson_arr_size(ambient); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.assets.ambient[%zu]", i);
        yyjson_val *zone = yyjson_arr_get(ambient, i);
        if (!yyjson_is_obj(zone))
            return validation_error(ctx, path, "ambient asset entries must be objects");
        if (!require_unique_name(ctx, &names->ambient, "ambient asset", json_string(zone, "id"), path))
            return false;
        yyjson_val *ambient_id = obj_get(zone, "ambient_id");
        if (!yyjson_is_int(ambient_id) || yyjson_get_int(ambient_id) < 0)
            return validation_error(ctx, path, "ambient asset requires a non-negative ambient_id");
        for (size_t previous = 0; previous < i; ++previous)
        {
            yyjson_val *previous_id = obj_get(yyjson_arr_get(ambient, previous), "ambient_id");
            if (yyjson_is_int(previous_id) && yyjson_get_int(previous_id) == yyjson_get_int(ambient_id))
                return validation_error(ctx, path, "duplicate ambient asset ambient_id %d",
                                        (int)yyjson_get_int(ambient_id));
        }
        if (!asset_is_non_empty_string(zone, "path"))
            return validation_error(ctx, path, "ambient asset requires a non-empty path");
        if (!asset_path_exists(ctx, json_string(zone, "path"), path, "ambient"))
            return false;
    }
    return true;
}

/**
 * @file map.c
 * @brief Slayer3D standalone map file validation.
 */

#include "slayer3d/map.h"

#include <stdarg.h>
#include <stdlib.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include "yyjson.h"

#define MAP_PATH_MAX 256

struct slayer3d_map_document
{
    yyjson_doc *doc;
    char *source_path;
};

typedef struct map_name_table
{
    char **names;
    char **paths;
    int count;
} map_name_table;

typedef struct map_validation_context
{
    const slayer3d_map_validation_options *options;
    char *error_buffer;
    int error_buffer_size;
    bool failed;
    map_name_table asset_ids;
    map_name_table material_ids;
    map_name_table object_ids;
    map_name_table connection_ids;
} map_validation_context;

static void map_clear_error(char *error_buffer, int error_buffer_size)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        error_buffer[0] = '\0';
}

static void map_set_error(char *error_buffer, int error_buffer_size, const char *format, ...)
{
    if (error_buffer == NULL || error_buffer_size <= 0)
        return;
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(error_buffer, (size_t)error_buffer_size, format, args);
    va_end(args);
}

static yyjson_val *map_obj_get(yyjson_val *object, const char *key)
{
    return yyjson_is_obj(object) ? yyjson_obj_get(object, key) : NULL;
}

static const char *map_json_string(yyjson_val *object, const char *key)
{
    yyjson_val *value = map_obj_get(object, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}

static void map_set_first_error(map_validation_context *ctx, const char *json_path, const char *message)
{
    if (ctx == NULL || ctx->error_buffer == NULL || ctx->error_buffer_size <= 0 || ctx->error_buffer[0] != '\0')
        return;
    SDL_snprintf(ctx->error_buffer, (size_t)ctx->error_buffer_size, "%s: %s", json_path != NULL ? json_path : "$",
                 message != NULL ? message : "unknown map validation error");
}

static bool map_emit(map_validation_context *ctx, slayer3d_map_diagnostic_severity severity, const char *json_path,
                     const char *format, ...)
{
    char message[384];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (ctx != NULL && ctx->options != NULL && ctx->options->diagnostic != NULL)
        ctx->options->diagnostic(ctx->options->userdata, severity, json_path != NULL ? json_path : "$", message);

    const bool fatal = severity == SLAYER3D_MAP_DIAGNOSTIC_ERROR ||
                       (ctx != NULL && ctx->options != NULL && ctx->options->treat_warnings_as_errors);
    if (fatal && ctx != NULL)
    {
        ctx->failed = true;
        map_set_first_error(ctx, json_path, message);
        return false;
    }
    return true;
}

static bool map_error(map_validation_context *ctx, const char *json_path, const char *format, ...)
{
    char message[384];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    return map_emit(ctx, SLAYER3D_MAP_DIAGNOSTIC_ERROR, json_path, "%s", message);
}

static bool map_warning(map_validation_context *ctx, const char *json_path, const char *format, ...)
{
    char message[384];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    return map_emit(ctx, SLAYER3D_MAP_DIAGNOSTIC_WARNING, json_path, "%s", message);
}

static void map_format_path(char *buffer, size_t buffer_size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(buffer, buffer_size, format, args);
    va_end(args);
}

static void map_name_table_destroy(map_name_table *table)
{
    if (table == NULL)
        return;
    for (int i = 0; i < table->count; ++i)
    {
        SDL_free(table->names[i]);
        SDL_free(table->paths[i]);
    }
    SDL_free(table->names);
    SDL_free(table->paths);
    table->names = NULL;
    table->paths = NULL;
    table->count = 0;
}

static const char *map_name_table_path(const map_name_table *table, const char *name)
{
    if (table == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < table->count; ++i)
    {
        if (SDL_strcmp(table->names[i], name) == 0)
            return table->paths[i];
    }
    return NULL;
}

static bool map_name_table_contains(const map_name_table *table, const char *name)
{
    return map_name_table_path(table, name) != NULL;
}

static bool map_name_table_add(map_name_table *table, const char *name, const char *json_path)
{
    char *name_copy = SDL_strdup(name != NULL ? name : "");
    char *path_copy = SDL_strdup(json_path != NULL ? json_path : "$");
    if (name_copy == NULL || path_copy == NULL)
    {
        SDL_free(name_copy);
        SDL_free(path_copy);
        return false;
    }

    const int next_count = table->count + 1;
    char **names = (char **)SDL_realloc(table->names, (size_t)next_count * sizeof(*table->names));
    if (names == NULL)
    {
        SDL_free(name_copy);
        SDL_free(path_copy);
        return false;
    }
    table->names = names;

    char **paths = (char **)SDL_realloc(table->paths, (size_t)next_count * sizeof(*table->paths));
    if (paths == NULL)
    {
        SDL_free(name_copy);
        SDL_free(path_copy);
        return false;
    }
    table->paths = paths;

    table->names[table->count] = name_copy;
    table->paths[table->count] = path_copy;
    table->count = next_count;
    return true;
}

static bool map_require_non_empty_string(map_validation_context *ctx, yyjson_val *object, const char *key,
                                         const char *json_path, const char *description)
{
    yyjson_val *value = map_obj_get(object, key);
    if (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0')
        return map_error(ctx, json_path, "%s must be a non-empty string", description);
    return true;
}

static bool map_optional_non_empty_string(map_validation_context *ctx, yyjson_val *object, const char *key,
                                          const char *json_path, const char *description)
{
    yyjson_val *value = map_obj_get(object, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0')
        return map_error(ctx, json_path, "%s must be a non-empty string", description);
    return true;
}

static bool map_add_unique(map_validation_context *ctx, map_name_table *table, const char *kind, const char *name,
                           const char *json_path)
{
    if (name == NULL || name[0] == '\0')
        return map_error(ctx, json_path, "%s requires a non-empty id", kind);
    const char *existing = map_name_table_path(table, name);
    if (existing != NULL)
        return map_error(ctx, json_path, "duplicate %s id '%s' previously declared at %s", kind, name, existing);
    if (!map_name_table_add(table, name, json_path))
        return map_error(ctx, json_path, "failed to allocate validation table for %s id '%s'", kind, name);
    return true;
}

static bool map_is_vec3(yyjson_val *value)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 3u)
        return false;
    for (size_t i = 0; i < 3u; ++i)
    {
        if (!yyjson_is_num(yyjson_arr_get(value, i)))
            return false;
    }
    return true;
}

static bool map_validate_vec3(map_validation_context *ctx, yyjson_val *value, const char *json_path,
                              const char *description)
{
    if (!map_is_vec3(value))
        return map_error(ctx, json_path, "%s must be a vec3 array [x, y, z]", description);
    return true;
}

static bool map_validate_positive_vec3(map_validation_context *ctx, yyjson_val *value, const char *json_path,
                                       const char *description)
{
    if (!map_validate_vec3(ctx, value, json_path, description))
        return false;
    for (size_t i = 0; i < 3u; ++i)
    {
        if (yyjson_get_num(yyjson_arr_get(value, i)) <= 0.0)
            return map_error(ctx, json_path, "%s values must be positive", description);
    }
    return true;
}

static bool map_validate_optional_vec3(map_validation_context *ctx, yyjson_val *object, const char *key,
                                       const char *json_path, const char *description)
{
    yyjson_val *value = map_obj_get(object, key);
    return value == NULL || map_validate_vec3(ctx, value, json_path, description);
}

static bool map_validate_color(map_validation_context *ctx, yyjson_val *value, const char *json_path,
                               const char *description)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 3u || yyjson_arr_size(value) > 4u)
        return map_error(ctx, json_path, "%s must be an RGBA array with 3 or 4 numeric channels", description);
    for (size_t i = 0, count = yyjson_arr_size(value); i < count; ++i)
    {
        yyjson_val *channel = yyjson_arr_get(value, i);
        if (!yyjson_is_num(channel) || yyjson_get_num(channel) < 0.0 || yyjson_get_num(channel) > 255.0)
            return map_error(ctx, json_path, "%s channels must be numbers in the 0..255 range", description);
    }
    return true;
}

static bool map_validate_optional_color(map_validation_context *ctx, yyjson_val *object, const char *key,
                                        const char *json_path, const char *description)
{
    yyjson_val *value = map_obj_get(object, key);
    return value == NULL || map_validate_color(ctx, value, json_path, description);
}

static bool map_validate_properties(map_validation_context *ctx, yyjson_val *properties, const char *json_path)
{
    if (properties == NULL)
        return true;
    if (!yyjson_is_obj(properties))
        return map_error(ctx, json_path, "properties must be an object");
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        value = yyjson_obj_iter_get_val(key);
        if (name == NULL || name[0] == '\0')
            return map_error(ctx, json_path, "properties keys must be non-empty strings");
        (void)value;
    }
    return true;
}

static bool map_validate_project_relative_reference(map_validation_context *ctx, const char *value,
                                                    const char *json_path, const char *description)
{
    if (value == NULL || value[0] == '\0')
        return map_error(ctx, json_path, "%s must be a non-empty asset id or project-relative path", description);
    if (value[0] == '/' || SDL_strstr(value, "://") != NULL || SDL_strstr(value, ":\\") != NULL)
        return map_warning(ctx, json_path, "%s should be a project-relative path or stable asset id", description);
    return true;
}

static bool map_validate_asset_array(map_validation_context *ctx, yyjson_val *array, const char *section_name,
                                     const char *json_path)
{
    if (array == NULL)
        return true;
    if (!yyjson_is_arr(array))
        return map_error(ctx, json_path, "assets.%s must be an array", section_name);

    for (size_t i = 0, count = yyjson_arr_size(array); i < count; ++i)
    {
        char path[MAP_PATH_MAX];
        map_format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *entry = yyjson_arr_get(array, i);
        if (!yyjson_is_obj(entry))
            return map_error(ctx, path, "asset entry must be an object");
        char id_path[MAP_PATH_MAX];
        char source_path[MAP_PATH_MAX];
        map_format_path(id_path, sizeof(id_path), "%s.id", path);
        map_format_path(source_path, sizeof(source_path), "%s.path", path);
        if (!map_require_non_empty_string(ctx, entry, "id", id_path, "asset id") ||
            !map_require_non_empty_string(ctx, entry, "path", source_path, "asset path"))
            return false;
        const char *id = map_json_string(entry, "id");
        const char *source = map_json_string(entry, "path");
        if (!map_add_unique(ctx, &ctx->asset_ids, "asset", id, path) ||
            !map_validate_project_relative_reference(ctx, source, source_path, "asset path") ||
            !map_validate_properties(ctx, map_obj_get(entry, "properties"), path))
            return false;
    }
    return true;
}

static bool map_validate_assets(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *assets = map_obj_get(root, "assets");
    if (assets == NULL)
        return true;
    if (!yyjson_is_obj(assets))
        return map_error(ctx, "$.assets", "assets must be an object");
    return map_validate_asset_array(ctx, map_obj_get(assets, "textures"), "textures", "$.assets.textures") &&
           map_validate_asset_array(ctx, map_obj_get(assets, "models"), "models", "$.assets.models") &&
           map_validate_asset_array(ctx, map_obj_get(assets, "sprites"), "sprites", "$.assets.sprites") &&
           map_validate_asset_array(ctx, map_obj_get(assets, "skyboxes"), "skyboxes", "$.assets.skyboxes");
}

static bool map_validate_material_reference(map_validation_context *ctx, yyjson_val *object, const char *key,
                                            const char *json_path)
{
    yyjson_val *value = map_obj_get(object, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0')
        return map_error(ctx, json_path, "material reference must be a non-empty string");
    if (!map_name_table_contains(&ctx->material_ids, yyjson_get_str(value)))
        return map_error(ctx, json_path, "unknown material reference '%s'", yyjson_get_str(value));
    return true;
}

static bool map_validate_asset_reference(map_validation_context *ctx, yyjson_val *object, const char *key,
                                         const char *json_path, const char *description)
{
    yyjson_val *value = map_obj_get(object, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_str(value))
        return map_error(ctx, json_path, "%s must be a non-empty asset id or project-relative path", description);
    const char *text = yyjson_get_str(value);
    if (text == NULL || text[0] == '\0')
        return map_error(ctx, json_path, "%s must be a non-empty asset id or project-relative path", description);
    if (!map_name_table_contains(&ctx->asset_ids, text))
        return map_validate_project_relative_reference(ctx, text, json_path, description);
    return true;
}

static bool map_validate_materials(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *materials = map_obj_get(root, "materials");
    if (materials == NULL)
        return true;
    if (!yyjson_is_arr(materials))
        return map_error(ctx, "$.materials", "materials must be an array");

    for (size_t i = 0, count = yyjson_arr_size(materials); i < count; ++i)
    {
        char path[MAP_PATH_MAX];
        map_format_path(path, sizeof(path), "$.materials[%zu]", i);
        yyjson_val *material = yyjson_arr_get(materials, i);
        if (!yyjson_is_obj(material))
            return map_error(ctx, path, "material entry must be an object");
        char id_path[MAP_PATH_MAX];
        char texture_path[MAP_PATH_MAX];
        char color_path[MAP_PATH_MAX];
        char tint_path[MAP_PATH_MAX];
        map_format_path(id_path, sizeof(id_path), "%s.id", path);
        map_format_path(texture_path, sizeof(texture_path), "%s.texture", path);
        map_format_path(color_path, sizeof(color_path), "%s.color", path);
        map_format_path(tint_path, sizeof(tint_path), "%s.tint", path);
        if (!map_require_non_empty_string(ctx, material, "id", id_path, "material id"))
            return false;
        if (!map_add_unique(ctx, &ctx->material_ids, "material", map_json_string(material, "id"), path) ||
            !map_validate_asset_reference(ctx, material, "texture", texture_path, "material texture") ||
            !map_validate_optional_color(ctx, material, "color", color_path, "material color") ||
            !map_validate_optional_color(ctx, material, "tint", tint_path, "material tint") ||
            !map_validate_properties(ctx, map_obj_get(material, "properties"), path))
            return false;
    }
    return true;
}

static double map_vec3_component(yyjson_val *value, size_t index)
{
    yyjson_val *entry = yyjson_arr_get(value, index);
    return yyjson_is_num(entry) ? yyjson_get_num(entry) : 0.0;
}

static bool map_validate_box_geometry(map_validation_context *ctx, yyjson_val *geometry, const char *path)
{
    char min_path[MAP_PATH_MAX];
    char max_path[MAP_PATH_MAX];
    map_format_path(min_path, sizeof(min_path), "%s.min", path);
    map_format_path(max_path, sizeof(max_path), "%s.max", path);
    yyjson_val *min = map_obj_get(geometry, "min");
    yyjson_val *max = map_obj_get(geometry, "max");
    if (!map_validate_vec3(ctx, min, min_path, "box min") || !map_validate_vec3(ctx, max, max_path, "box max"))
        return false;
    if (map_vec3_component(max, 0) <= map_vec3_component(min, 0) ||
        map_vec3_component(max, 1) <= map_vec3_component(min, 1) ||
        map_vec3_component(max, 2) <= map_vec3_component(min, 2))
        return map_error(ctx, path, "box geometry max must be greater than min on every axis");
    return true;
}

static bool map_validate_plane_geometry(map_validation_context *ctx, yyjson_val *geometry, const char *path)
{
    yyjson_val *planes = map_obj_get(geometry, "planes");
    char planes_path[MAP_PATH_MAX];
    map_format_path(planes_path, sizeof(planes_path), "%s.planes", path);
    if (!yyjson_is_arr(planes) || yyjson_arr_size(planes) < 4u)
        return map_error(ctx, planes_path, "plane brush geometry requires at least four planes");
    for (size_t i = 0, count = yyjson_arr_size(planes); i < count; ++i)
    {
        char plane_path[MAP_PATH_MAX];
        char normal_path[MAP_PATH_MAX];
        char distance_path[MAP_PATH_MAX];
        char color_path[MAP_PATH_MAX];
        char tint_path[MAP_PATH_MAX];
        map_format_path(plane_path, sizeof(plane_path), "%s[%zu]", planes_path, i);
        map_format_path(normal_path, sizeof(normal_path), "%s.normal", plane_path);
        map_format_path(distance_path, sizeof(distance_path), "%s.distance", plane_path);
        map_format_path(color_path, sizeof(color_path), "%s.color", plane_path);
        map_format_path(tint_path, sizeof(tint_path), "%s.tint", plane_path);
        yyjson_val *plane = yyjson_arr_get(planes, i);
        if (!yyjson_is_obj(plane))
            return map_error(ctx, plane_path, "plane entry must be an object");
        if (!map_validate_vec3(ctx, map_obj_get(plane, "normal"), normal_path, "plane normal"))
            return false;
        yyjson_val *distance = map_obj_get(plane, "distance");
        if (!yyjson_is_num(distance))
            return map_error(ctx, distance_path, "plane distance must be a number");
        if (!map_validate_material_reference(ctx, plane, "material", plane_path) ||
            !map_validate_optional_color(ctx, plane, "color", color_path, "plane color") ||
            !map_validate_optional_color(ctx, plane, "tint", tint_path, "plane tint"))
            return false;
    }
    return true;
}

static bool map_validate_brush_geometry(map_validation_context *ctx, yyjson_val *geometry, const char *path)
{
    if (!yyjson_is_obj(geometry))
        return map_error(ctx, path, "brush geometry must be an object");
    const char *kind = map_json_string(geometry, "kind");
    if (kind == NULL || kind[0] == '\0')
        return map_error(ctx, path, "brush geometry requires a non-empty kind");
    if (SDL_strcmp(kind, "box") == 0)
        return map_validate_box_geometry(ctx, geometry, path);
    if (SDL_strcmp(kind, "planes") == 0)
        return map_validate_plane_geometry(ctx, geometry, path);
    return map_error(ctx, path, "brush geometry kind must be 'box' or 'planes'");
}

static bool map_validate_brush_faces(map_validation_context *ctx, yyjson_val *faces, const char *path)
{
    if (faces == NULL)
        return true;
    if (!yyjson_is_arr(faces))
        return map_error(ctx, path, "brush faces must be an array");
    for (size_t i = 0, count = yyjson_arr_size(faces); i < count; ++i)
    {
        char face_path[MAP_PATH_MAX];
        char normal_path[MAP_PATH_MAX];
        char color_path[MAP_PATH_MAX];
        char tint_path[MAP_PATH_MAX];
        map_format_path(face_path, sizeof(face_path), "%s[%zu]", path, i);
        map_format_path(normal_path, sizeof(normal_path), "%s.normal", face_path);
        map_format_path(color_path, sizeof(color_path), "%s.color", face_path);
        map_format_path(tint_path, sizeof(tint_path), "%s.tint", face_path);
        yyjson_val *face = yyjson_arr_get(faces, i);
        if (!yyjson_is_obj(face))
            return map_error(ctx, face_path, "brush face entry must be an object");
        if (!map_optional_non_empty_string(ctx, face, "side", face_path, "brush face side") ||
            !map_validate_material_reference(ctx, face, "material", face_path) ||
            !map_validate_optional_vec3(ctx, face, "normal", normal_path, "brush face normal") ||
            !map_validate_optional_color(ctx, face, "color", color_path, "brush face color") ||
            !map_validate_optional_color(ctx, face, "tint", tint_path, "brush face tint") ||
            !map_validate_properties(ctx, map_obj_get(face, "properties"), face_path))
            return false;
    }
    return true;
}

static bool map_validate_brushes(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *brushes = map_obj_get(root, "brushes");
    if (brushes == NULL)
        return true;
    if (!yyjson_is_arr(brushes))
        return map_error(ctx, "$.brushes", "brushes must be an array");
    for (size_t i = 0, count = yyjson_arr_size(brushes); i < count; ++i)
    {
        char path[MAP_PATH_MAX];
        char id_path[MAP_PATH_MAX];
        char geometry_path[MAP_PATH_MAX];
        char material_path[MAP_PATH_MAX];
        char color_path[MAP_PATH_MAX];
        map_format_path(path, sizeof(path), "$.brushes[%zu]", i);
        map_format_path(id_path, sizeof(id_path), "%s.id", path);
        map_format_path(geometry_path, sizeof(geometry_path), "%s.geometry", path);
        map_format_path(material_path, sizeof(material_path), "%s.material", path);
        map_format_path(color_path, sizeof(color_path), "%s.color", path);
        yyjson_val *brush = yyjson_arr_get(brushes, i);
        if (!yyjson_is_obj(brush))
            return map_error(ctx, path, "brush entry must be an object");
        if (!map_require_non_empty_string(ctx, brush, "id", id_path, "brush id") ||
            !map_add_unique(ctx, &ctx->object_ids, "object", map_json_string(brush, "id"), path) ||
            !map_validate_brush_geometry(ctx, map_obj_get(brush, "geometry"), geometry_path) ||
            !map_validate_material_reference(ctx, brush, "material", material_path) ||
            !map_validate_optional_color(ctx, brush, "color", color_path, "brush color") ||
            !map_validate_brush_faces(ctx, map_obj_get(brush, "faces"), path) ||
            !map_validate_properties(ctx, map_obj_get(brush, "properties"), path))
            return false;
    }
    return true;
}

static bool map_validate_transform(map_validation_context *ctx, yyjson_val *transform, const char *path)
{
    if (transform == NULL)
        return true;
    if (!yyjson_is_obj(transform))
        return map_error(ctx, path, "transform must be an object");
    char position_path[MAP_PATH_MAX];
    char rotation_path[MAP_PATH_MAX];
    char scale_path[MAP_PATH_MAX];
    char facing_path[MAP_PATH_MAX];
    map_format_path(position_path, sizeof(position_path), "%s.position", path);
    map_format_path(rotation_path, sizeof(rotation_path), "%s.rotation", path);
    map_format_path(scale_path, sizeof(scale_path), "%s.scale", path);
    map_format_path(facing_path, sizeof(facing_path), "%s.facing", path);
    yyjson_val *scale = map_obj_get(transform, "scale");
    return map_validate_optional_vec3(ctx, transform, "position", position_path, "transform position") &&
           map_validate_optional_vec3(ctx, transform, "rotation", rotation_path, "transform rotation") &&
           (scale == NULL || map_validate_positive_vec3(ctx, scale, scale_path, "transform scale")) &&
           map_validate_optional_vec3(ctx, transform, "facing", facing_path, "transform facing");
}

static bool map_validate_primitive(map_validation_context *ctx, yyjson_val *actor, const char *path)
{
    yyjson_val *primitive = map_obj_get(actor, "primitive");
    if (primitive == NULL)
        return true;
    if (!yyjson_is_str(primitive) || yyjson_get_str(primitive)[0] == '\0')
        return map_error(ctx, path, "actor primitive must be a non-empty string");
    const char *value = yyjson_get_str(primitive);
    if (SDL_strcmp(value, "box") == 0 || SDL_strcmp(value, "cube") == 0 || SDL_strcmp(value, "capsule") == 0 ||
        SDL_strcmp(value, "sphere") == 0 || SDL_strcmp(value, "cylinder") == 0 || SDL_strcmp(value, "rectangle") == 0 ||
        SDL_strcmp(value, "billboard") == 0 || SDL_strcmp(value, "trigger") == 0)
        return true;
    return map_error(ctx, path,
                     "actor primitive must be box, cube, capsule, sphere, cylinder, rectangle, billboard, or trigger");
}

static bool map_validate_actors(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *actors = map_obj_get(root, "actors");
    if (actors == NULL)
        return true;
    if (!yyjson_is_arr(actors))
        return map_error(ctx, "$.actors", "actors must be an array");
    for (size_t i = 0, count = yyjson_arr_size(actors); i < count; ++i)
    {
        char path[MAP_PATH_MAX];
        char id_path[MAP_PATH_MAX];
        char transform_path[MAP_PATH_MAX];
        char color_path[MAP_PATH_MAX];
        char model_path[MAP_PATH_MAX];
        char sprite_path[MAP_PATH_MAX];
        char primitive_path[MAP_PATH_MAX];
        char prefab_path[MAP_PATH_MAX];
        map_format_path(path, sizeof(path), "$.actors[%zu]", i);
        map_format_path(id_path, sizeof(id_path), "%s.id", path);
        map_format_path(transform_path, sizeof(transform_path), "%s.transform", path);
        map_format_path(color_path, sizeof(color_path), "%s.color", path);
        map_format_path(model_path, sizeof(model_path), "%s.model", path);
        map_format_path(sprite_path, sizeof(sprite_path), "%s.sprite", path);
        map_format_path(primitive_path, sizeof(primitive_path), "%s.primitive", path);
        map_format_path(prefab_path, sizeof(prefab_path), "%s.prefab", path);
        yyjson_val *actor = yyjson_arr_get(actors, i);
        if (!yyjson_is_obj(actor))
            return map_error(ctx, path, "actor entry must be an object");
        if (!map_require_non_empty_string(ctx, actor, "id", id_path, "actor id") ||
            !map_add_unique(ctx, &ctx->object_ids, "object", map_json_string(actor, "id"), path) ||
            !map_optional_non_empty_string(ctx, actor, "archetype", path, "actor archetype") ||
            !map_validate_asset_reference(ctx, actor, "model", model_path, "actor model") ||
            !map_validate_asset_reference(ctx, actor, "sprite", sprite_path, "actor sprite") ||
            !map_optional_non_empty_string(ctx, actor, "prefab", prefab_path, "actor prefab") ||
            !map_validate_primitive(ctx, actor, primitive_path) ||
            !map_validate_transform(ctx, map_obj_get(actor, "transform"), transform_path) ||
            !map_validate_optional_color(ctx, actor, "color", color_path, "actor color") ||
            !map_validate_properties(ctx, map_obj_get(actor, "properties"), path))
            return false;
    }
    return true;
}

static bool map_validate_prefabs(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *prefabs = map_obj_get(root, "prefabs");
    if (prefabs == NULL)
        return true;
    if (!yyjson_is_arr(prefabs))
        return map_error(ctx, "$.prefabs", "prefabs must be an array");
    map_name_table ids;
    SDL_zero(ids);
    bool ok = true;
    for (size_t i = 0, count = yyjson_arr_size(prefabs); ok && i < count; ++i)
    {
        char path[MAP_PATH_MAX];
        char id_path[MAP_PATH_MAX];
        char kind_path[MAP_PATH_MAX];
        char position_path[MAP_PATH_MAX];
        char rotation_path[MAP_PATH_MAX];
        char scale_path[MAP_PATH_MAX];
        char color_path[MAP_PATH_MAX];
        map_format_path(path, sizeof(path), "$.prefabs[%zu]", i);
        map_format_path(id_path, sizeof(id_path), "%s.id", path);
        map_format_path(kind_path, sizeof(kind_path), "%s.kind", path);
        map_format_path(position_path, sizeof(position_path), "%s.position", path);
        map_format_path(rotation_path, sizeof(rotation_path), "%s.rotation", path);
        map_format_path(scale_path, sizeof(scale_path), "%s.scale", path);
        map_format_path(color_path, sizeof(color_path), "%s.color", path);
        yyjson_val *prefab = yyjson_arr_get(prefabs, i);
        yyjson_val *scale = map_obj_get(prefab, "scale");
        if (!yyjson_is_obj(prefab))
        {
            ok = map_error(ctx, path, "prefab entry must be an object");
            break;
        }
        const char *kind = map_json_string(prefab, "kind");
        if (!map_require_non_empty_string(ctx, prefab, "id", id_path, "prefab id") ||
            !map_add_unique(ctx, &ids, "prefab", map_json_string(prefab, "id"), path) ||
            !map_optional_non_empty_string(ctx, prefab, "kind", kind_path, "prefab kind") ||
            (kind != NULL && SDL_strcmp(kind, "actor") != 0 && SDL_strcmp(kind, "brush") != 0 &&
             SDL_strcmp(kind, "mixed") != 0 &&
             !map_error(ctx, kind_path, "prefab kind must be actor, brush, or mixed")) ||
            !map_validate_optional_vec3(ctx, prefab, "position", position_path, "prefab position") ||
            !map_validate_optional_vec3(ctx, prefab, "rotation", rotation_path, "prefab rotation") ||
            (scale != NULL && !map_validate_positive_vec3(ctx, scale, scale_path, "prefab scale")) ||
            !map_validate_optional_color(ctx, prefab, "color", color_path, "prefab color") ||
            !map_validate_properties(ctx, map_obj_get(prefab, "properties"), path))
        {
            ok = false;
        }
    }
    map_name_table_destroy(&ids);
    return ok;
}

static bool map_validate_connection_endpoint(map_validation_context *ctx, yyjson_val *endpoint, const char *path,
                                             const char *label, const char *event_or_action_key)
{
    if (!yyjson_is_obj(endpoint))
        return map_error(ctx, path, "connection %s endpoint must be an object", label);
    char entity_path[MAP_PATH_MAX];
    char action_path[MAP_PATH_MAX];
    map_format_path(entity_path, sizeof(entity_path), "%s.entity", path);
    map_format_path(action_path, sizeof(action_path), "%s.%s", path, event_or_action_key);
    if (!map_require_non_empty_string(ctx, endpoint, "entity", entity_path, "connection endpoint entity"))
        return false;
    const char *entity = map_json_string(endpoint, "entity");
    if (!map_name_table_contains(&ctx->object_ids, entity) && !yyjson_get_bool(map_obj_get(endpoint, "external")))
        return map_error(ctx, entity_path, "unknown connection endpoint entity '%s'", entity);
    return map_optional_non_empty_string(ctx, endpoint, event_or_action_key, action_path,
                                         "connection endpoint event/action");
}

static bool map_validate_connections(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *connections = map_obj_get(root, "connections");
    if (connections == NULL)
        return true;
    if (!yyjson_is_arr(connections))
        return map_error(ctx, "$.connections", "connections must be an array");
    for (size_t i = 0, count = yyjson_arr_size(connections); i < count; ++i)
    {
        char path[MAP_PATH_MAX];
        char from_path[MAP_PATH_MAX];
        char to_path[MAP_PATH_MAX];
        map_format_path(path, sizeof(path), "$.connections[%zu]", i);
        map_format_path(from_path, sizeof(from_path), "%s.from", path);
        map_format_path(to_path, sizeof(to_path), "%s.to", path);
        yyjson_val *connection = yyjson_arr_get(connections, i);
        if (!yyjson_is_obj(connection))
            return map_error(ctx, path, "connection entry must be an object");
        const char *id = map_json_string(connection, "id");
        if (id != NULL && !map_add_unique(ctx, &ctx->connection_ids, "connection", id, path))
            return false;
        if (!map_validate_connection_endpoint(ctx, map_obj_get(connection, "from"), from_path, "from", "event") ||
            !map_validate_connection_endpoint(ctx, map_obj_get(connection, "to"), to_path, "to", "action") ||
            !map_validate_properties(ctx, map_obj_get(connection, "properties"), path))
            return false;
    }
    return true;
}

static bool map_validate_metadata(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *metadata = map_obj_get(root, "metadata");
    if (metadata == NULL)
        return true;
    if (!yyjson_is_obj(metadata))
        return map_error(ctx, "$.metadata", "metadata must be an object");
    return map_optional_non_empty_string(ctx, metadata, "id", "$.metadata.id", "metadata id") &&
           map_optional_non_empty_string(ctx, metadata, "name", "$.metadata.name", "metadata name") &&
           map_optional_non_empty_string(ctx, metadata, "author", "$.metadata.author", "metadata author") &&
           map_optional_non_empty_string(ctx, metadata, "description", "$.metadata.description",
                                         "metadata description");
}

static bool map_validate_root(map_validation_context *ctx, yyjson_val *root)
{
    if (!yyjson_is_obj(root))
        return map_error(ctx, "$", "Slayer3D map root must be an object");

    yyjson_val *format = map_obj_get(root, "format");
    if (!yyjson_is_str(format) || SDL_strcmp(yyjson_get_str(format), SLAYER3D_MAP_FORMAT_ID) != 0)
        return map_error(ctx, "$.format", "format must be '%s'", SLAYER3D_MAP_FORMAT_ID);

    yyjson_val *version = map_obj_get(root, "version");
    if (!yyjson_is_int(version))
        return map_error(ctx, "$.version", "version must be integer %d", SLAYER3D_MAP_FORMAT_VERSION);
    if ((int)yyjson_get_int(version) != SLAYER3D_MAP_FORMAT_VERSION)
        return map_error(ctx, "$.version", "unsupported Slayer3D map version %d", (int)yyjson_get_int(version));

    const char *units = map_json_string(root, "units");
    if (units != NULL && SDL_strcmp(units, "meters") != 0 && SDL_strcmp(units, "source_units") != 0)
        return map_error(ctx, "$.units", "units must be 'meters' or 'source_units'");

    const char *coordinate_system = map_json_string(root, "coordinate_system");
    if (coordinate_system != NULL && SDL_strcmp(coordinate_system, "y_up") != 0)
        return map_error(ctx, "$.coordinate_system", "coordinate_system must be 'y_up'");

    return map_validate_metadata(ctx, root) && map_validate_assets(ctx, root) && map_validate_materials(ctx, root) &&
           map_validate_brushes(ctx, root) && map_validate_actors(ctx, root) && map_validate_prefabs(ctx, root) &&
           map_validate_connections(ctx, root) &&
           map_validate_properties(ctx, map_obj_get(root, "properties"), "$.properties");
}

static bool map_validate_document(yyjson_doc *doc, const slayer3d_map_validation_options *options, char *error_buffer,
                                  int error_buffer_size)
{
    if (doc == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: map JSON document is empty");
        return false;
    }

    map_validation_context ctx;
    SDL_zero(ctx);
    ctx.options = options;
    ctx.error_buffer = error_buffer;
    ctx.error_buffer_size = error_buffer_size;
    const bool ok = map_validate_root(&ctx, yyjson_doc_get_root(doc)) && !ctx.failed;
    map_name_table_destroy(&ctx.asset_ids);
    map_name_table_destroy(&ctx.material_ids);
    map_name_table_destroy(&ctx.object_ids);
    map_name_table_destroy(&ctx.connection_ids);
    return ok;
}

static yyjson_doc *map_parse_json(const char *json, size_t json_size, char *error_buffer, int error_buffer_size)
{
    if (json == NULL || json_size == 0U)
    {
        map_set_error(error_buffer, error_buffer_size, "$: map JSON is empty");
        return NULL;
    }

    yyjson_read_err read_error;
    yyjson_doc *doc = yyjson_read_opts((char *)(void *)json, json_size, YYJSON_READ_NOFLAG, NULL, &read_error);
    if (doc == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: failed to parse map JSON at byte %zu: %s", read_error.pos,
                      read_error.msg != NULL ? read_error.msg : "unknown parse error");
        return NULL;
    }
    return doc;
}

bool slayer3d_map_validate_json(const char *json, size_t json_size, const slayer3d_map_validation_options *options,
                                char *error_buffer, int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    yyjson_doc *doc = map_parse_json(json, json_size, error_buffer, error_buffer_size);
    if (doc == NULL)
        return false;
    const bool ok = map_validate_document(doc, options, error_buffer, error_buffer_size);
    yyjson_doc_free(doc);
    return ok;
}

bool slayer3d_map_validate_file(const char *path, const slayer3d_map_validation_options *options, char *error_buffer,
                                int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    if (path == NULL || path[0] == '\0')
    {
        map_set_error(error_buffer, error_buffer_size, "$: map path must be non-empty");
        return false;
    }

    size_t size = 0U;
    char *json = (char *)SDL_LoadFile(path, &size);
    if (json == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "%s: failed to read map file: %s", path, SDL_GetError());
        return false;
    }
    const bool ok = slayer3d_map_validate_json(json, size, options, error_buffer, error_buffer_size);
    SDL_free(json);
    return ok;
}

bool slayer3d_map_load_json(const char *json, size_t json_size, const slayer3d_map_validation_options *options,
                            slayer3d_map_document **out_document, char *error_buffer, int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    if (out_document == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: output map document pointer is required");
        return false;
    }
    *out_document = NULL;

    yyjson_doc *doc = map_parse_json(json, json_size, error_buffer, error_buffer_size);
    if (doc == NULL)
        return false;
    if (!map_validate_document(doc, options, error_buffer, error_buffer_size))
    {
        yyjson_doc_free(doc);
        return false;
    }

    slayer3d_map_document *document = (slayer3d_map_document *)SDL_calloc(1, sizeof(*document));
    if (document == NULL)
    {
        yyjson_doc_free(doc);
        map_set_error(error_buffer, error_buffer_size, "$: failed to allocate map document");
        return false;
    }
    document->doc = doc;
    *out_document = document;
    return true;
}

bool slayer3d_map_load_file(const char *path, const slayer3d_map_validation_options *options,
                            slayer3d_map_document **out_document, char *error_buffer, int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    if (out_document == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: output map document pointer is required");
        return false;
    }
    *out_document = NULL;
    if (path == NULL || path[0] == '\0')
    {
        map_set_error(error_buffer, error_buffer_size, "$: map path must be non-empty");
        return false;
    }

    size_t size = 0U;
    char *json = (char *)SDL_LoadFile(path, &size);
    if (json == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "%s: failed to read map file: %s", path, SDL_GetError());
        return false;
    }

    slayer3d_map_document *document = NULL;
    const bool ok = slayer3d_map_load_json(json, size, options, &document, error_buffer, error_buffer_size);
    SDL_free(json);
    if (!ok)
        return false;

    document->source_path = SDL_strdup(path);
    if (document->source_path == NULL)
    {
        slayer3d_map_destroy(document);
        map_set_error(error_buffer, error_buffer_size, "%s: failed to store map source path", path);
        return false;
    }

    *out_document = document;
    return true;
}

bool slayer3d_map_to_json(const slayer3d_map_document *document, char **out_json, size_t *out_json_size,
                          char *error_buffer, int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    if (out_json == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: output JSON pointer is required");
        return false;
    }
    *out_json = NULL;
    if (out_json_size != NULL)
        *out_json_size = 0U;
    if (document == NULL || document->doc == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: map document is required");
        return false;
    }
    if (!map_validate_document(document->doc, NULL, error_buffer, error_buffer_size))
        return false;

    yyjson_write_err write_error;
    size_t json_size = 0U;
    char *json = yyjson_write_opts(document->doc,
                                   YYJSON_WRITE_PRETTY | YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END,
                                   NULL, &json_size, &write_error);
    if (json == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: failed to serialize map JSON: %s",
                      write_error.msg != NULL ? write_error.msg : "unknown write error");
        return false;
    }

    *out_json = json;
    if (out_json_size != NULL)
        *out_json_size = json_size;
    return true;
}

bool slayer3d_map_write_file(const slayer3d_map_document *document, const char *path, char *error_buffer,
                             int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    if (path == NULL || path[0] == '\0')
    {
        map_set_error(error_buffer, error_buffer_size, "$: map path must be non-empty");
        return false;
    }

    char *json = NULL;
    size_t json_size = 0U;
    if (!slayer3d_map_to_json(document, &json, &json_size, error_buffer, error_buffer_size))
        return false;

    SDL_IOStream *stream = SDL_IOFromFile(path, "wb");
    if (stream == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "%s: failed to open map file for writing: %s", path,
                      SDL_GetError());
        slayer3d_map_free_string(json);
        return false;
    }

    const bool wrote = SDL_WriteIO(stream, json, json_size) == json_size;
    const bool closed = SDL_CloseIO(stream);
    slayer3d_map_free_string(json);
    if (!wrote)
    {
        map_set_error(error_buffer, error_buffer_size, "%s: failed to write map file: %s", path, SDL_GetError());
        return false;
    }
    if (!closed)
    {
        map_set_error(error_buffer, error_buffer_size, "%s: failed to close map file after writing: %s", path,
                      SDL_GetError());
        return false;
    }
    return true;
}

void slayer3d_map_destroy(slayer3d_map_document *document)
{
    if (document == NULL)
        return;
    yyjson_doc_free(document->doc);
    SDL_free(document->source_path);
    SDL_free(document);
}

void slayer3d_map_free_string(char *text)
{
    free(text);
}

int slayer3d_map_get_version(const slayer3d_map_document *document)
{
    if (document == NULL || document->doc == NULL)
        return 0;
    yyjson_val *root = yyjson_doc_get_root(document->doc);
    yyjson_val *version = map_obj_get(root, "version");
    return yyjson_is_int(version) ? (int)yyjson_get_int(version) : 0;
}

const char *slayer3d_map_get_source_path(const slayer3d_map_document *document)
{
    return document != NULL ? document->source_path : NULL;
}

static const char *map_metadata_string(const slayer3d_map_document *document, const char *key)
{
    if (document == NULL || document->doc == NULL)
        return NULL;
    yyjson_val *root = yyjson_doc_get_root(document->doc);
    return map_json_string(map_obj_get(root, "metadata"), key);
}

const char *slayer3d_map_get_metadata_id(const slayer3d_map_document *document)
{
    return map_metadata_string(document, "id");
}

const char *slayer3d_map_get_metadata_name(const slayer3d_map_document *document)
{
    return map_metadata_string(document, "name");
}

static size_t map_array_count(const slayer3d_map_document *document, const char *key)
{
    if (document == NULL || document->doc == NULL)
        return 0U;
    yyjson_val *root = yyjson_doc_get_root(document->doc);
    yyjson_val *array = map_obj_get(root, key);
    return yyjson_is_arr(array) ? yyjson_arr_size(array) : 0U;
}

size_t slayer3d_map_get_material_count(const slayer3d_map_document *document)
{
    return map_array_count(document, "materials");
}

size_t slayer3d_map_get_brush_count(const slayer3d_map_document *document)
{
    return map_array_count(document, "brushes");
}

size_t slayer3d_map_get_actor_count(const slayer3d_map_document *document)
{
    return map_array_count(document, "actors");
}

size_t slayer3d_map_get_connection_count(const slayer3d_map_document *document)
{
    return map_array_count(document, "connections");
}

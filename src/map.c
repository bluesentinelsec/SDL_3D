/**
 * @file map.c
 * @brief Slayer3D standalone map file validation.
 */

#include "slayer3d/map.h"

#include <stdarg.h>
#include <stdlib.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include "yyjson.h"

#define MAP_PATH_MAX 256
#define MAP_LIGHTING_DEFAULT_DYNAMIC_LIGHT_BUDGET 8u
#define MAP_LIGHTING_DEFAULT_STATIC_LIGHT_BUDGET 256u

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

static bool map_light_kind_bakes(const char *kind);
static bool map_light_kind_runs_dynamically(const char *kind);
static bool map_light_type_is_area(const char *type);

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
           map_validate_asset_array(ctx, map_obj_get(assets, "skyboxes"), "skyboxes", "$.assets.skyboxes") &&
           map_validate_asset_array(ctx, map_obj_get(assets, "effects"), "effects", "$.assets.effects");
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
        SDL_strcmp(value, "billboard") == 0 || SDL_strcmp(value, "trigger") == 0 || SDL_strcmp(value, "light") == 0 ||
        SDL_strcmp(value, "effect") == 0)
        return true;
    return map_error(ctx, path,
                     "actor primitive must be box, cube, capsule, sphere, cylinder, rectangle, billboard, trigger, or "
                     "light, or effect");
}

static bool map_validate_actor_display_mode(map_validation_context *ctx, yyjson_val *actor, const char *path)
{
    yyjson_val *display_mode = map_obj_get(actor, "display_mode");
    if (display_mode == NULL)
        return true;
    if (!yyjson_is_str(display_mode))
        return map_error(ctx, path, "actor display_mode must be solid or wireframe");
    const char *value = yyjson_get_str(display_mode);
    if (SDL_strcmp(value, "solid") == 0 || SDL_strcmp(value, "wireframe") == 0)
        return true;
    return map_error(ctx, path, "actor display_mode must be solid or wireframe");
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
        char display_mode_path[MAP_PATH_MAX];
        map_format_path(path, sizeof(path), "$.actors[%zu]", i);
        map_format_path(id_path, sizeof(id_path), "%s.id", path);
        map_format_path(transform_path, sizeof(transform_path), "%s.transform", path);
        map_format_path(color_path, sizeof(color_path), "%s.color", path);
        map_format_path(model_path, sizeof(model_path), "%s.model", path);
        map_format_path(sprite_path, sizeof(sprite_path), "%s.sprite", path);
        map_format_path(primitive_path, sizeof(primitive_path), "%s.primitive", path);
        map_format_path(prefab_path, sizeof(prefab_path), "%s.prefab", path);
        map_format_path(display_mode_path, sizeof(display_mode_path), "%s.display_mode", path);
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
            !map_validate_actor_display_mode(ctx, actor, display_mode_path) ||
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

static bool map_validate_optional_non_negative_number(map_validation_context *ctx, yyjson_val *object, const char *key,
                                                      const char *json_path, const char *description);
static bool map_validate_optional_positive_number(map_validation_context *ctx, yyjson_val *object, const char *key,
                                                  const char *json_path, const char *description);
static bool map_validate_optional_bool(map_validation_context *ctx, yyjson_val *object, const char *key,
                                       const char *json_path, const char *description);

static bool map_validate_light_kind(map_validation_context *ctx, yyjson_val *light, const char *path)
{
    yyjson_val *kind = map_obj_get(light, "kind");
    if (kind == NULL)
        return true;
    if (!yyjson_is_str(kind) || yyjson_get_str(kind)[0] == '\0')
        return map_error(ctx, path, "light kind must be dynamic, baked, static, or both");
    const char *value = yyjson_get_str(kind);
    if (SDL_strcmp(value, "dynamic") == 0 || SDL_strcmp(value, "baked") == 0 || SDL_strcmp(value, "static") == 0 ||
        SDL_strcmp(value, "both") == 0)
        return true;
    return map_error(ctx, path, "light kind must be dynamic, baked, static, or both");
}

static bool map_validate_light_type(map_validation_context *ctx, yyjson_val *light, const char *path)
{
    yyjson_val *type = map_obj_get(light, "type");
    if (!yyjson_is_str(type) || yyjson_get_str(type)[0] == '\0')
        return map_error(ctx, path, "light type must be directional, point, spot, area_rect, or area_sphere");
    const char *value = yyjson_get_str(type);
    if (SDL_strcmp(value, "directional") == 0 || SDL_strcmp(value, "point") == 0 || SDL_strcmp(value, "spot") == 0 ||
        SDL_strcmp(value, "area_rect") == 0 || SDL_strcmp(value, "area_sphere") == 0)
        return true;
    return map_error(ctx, path, "light type must be directional, point, spot, area_rect, or area_sphere");
}

static bool map_validate_light_shadow_mode(map_validation_context *ctx, yyjson_val *light, const char *path)
{
    const char *value = map_json_string(light, "shadow_mode");
    if (value == NULL)
        return map_obj_get(light, "shadow_mode") == NULL || map_error(ctx, path, "light shadow_mode must be a string");
    if (SDL_strcmp(value, "none") == 0 || SDL_strcmp(value, "baked") == 0 || SDL_strcmp(value, "dynamic") == 0 ||
        SDL_strcmp(value, "both") == 0)
    {
        return true;
    }
    return map_error(ctx, path, "light shadow_mode must be none, baked, dynamic, or both");
}

static bool map_validate_light_falloff(map_validation_context *ctx, yyjson_val *light, const char *path)
{
    const char *value = map_json_string(light, "falloff");
    if (value == NULL)
        return map_obj_get(light, "falloff") == NULL || map_error(ctx, path, "light falloff must be a string");
    if (SDL_strcmp(value, "inverse_square") == 0 || SDL_strcmp(value, "linear") == 0 || SDL_strcmp(value, "none") == 0)
    {
        return true;
    }
    return map_error(ctx, path, "light falloff must be inverse_square, linear, or none");
}

static bool map_validate_light_animation_type(map_validation_context *ctx, yyjson_val *animation, const char *path)
{
    const char *value = map_json_string(animation, "type");
    if (value == NULL)
        return map_obj_get(animation, "type") == NULL || map_error(ctx, path, "light animation type must be a string");
    if (SDL_strcmp(value, "none") == 0 || SDL_strcmp(value, "flicker") == 0 || SDL_strcmp(value, "pulse") == 0 ||
        SDL_strcmp(value, "rotate") == 0 || SDL_strcmp(value, "orbit") == 0 || SDL_strcmp(value, "sweep") == 0)
    {
        return true;
    }
    return map_error(ctx, path, "light animation type must be none, flicker, pulse, rotate, orbit, or sweep");
}

static bool map_validate_light_animation_preset(map_validation_context *ctx, yyjson_val *animation, const char *path)
{
    const char *value = map_json_string(animation, "preset");
    if (value == NULL)
        return map_obj_get(animation, "preset") == NULL ||
               map_error(ctx, path, "light animation preset must be a string");
    if (SDL_strcmp(value, "torch_fire") == 0 || SDL_strcmp(value, "fluorescent_flicker") == 0 ||
        SDL_strcmp(value, "warning_alarm") == 0 || SDL_strcmp(value, "rotating_siren") == 0 ||
        SDL_strcmp(value, "projectile_fireball") == 0 || SDL_strcmp(value, "muzzle_flash") == 0 ||
        SDL_strcmp(value, "steady_room") == 0)
    {
        return true;
    }
    return map_error(ctx, path,
                     "light animation preset must be torch_fire, fluorescent_flicker, warning_alarm, "
                     "rotating_siren, projectile_fireball, muzzle_flash, or steady_room");
}

static bool map_validate_light_animation(map_validation_context *ctx, yyjson_val *animation, const char *path)
{
    if (animation == NULL)
        return true;
    if (!yyjson_is_obj(animation))
        return map_error(ctx, path, "light animation must be an object");
    char enabled_path[MAP_PATH_MAX];
    char type_path[MAP_PATH_MAX];
    char preset_path[MAP_PATH_MAX];
    char rate_path[MAP_PATH_MAX];
    char amplitude_path[MAP_PATH_MAX];
    char min_path[MAP_PATH_MAX];
    char max_path[MAP_PATH_MAX];
    char phase_path[MAP_PATH_MAX];
    char axis_path[MAP_PATH_MAX];
    char radius_path[MAP_PATH_MAX];
    char properties_path[MAP_PATH_MAX];
    map_format_path(enabled_path, sizeof(enabled_path), "%s.enabled", path);
    map_format_path(type_path, sizeof(type_path), "%s.type", path);
    map_format_path(preset_path, sizeof(preset_path), "%s.preset", path);
    map_format_path(rate_path, sizeof(rate_path), "%s.rate_hz", path);
    map_format_path(amplitude_path, sizeof(amplitude_path), "%s.amplitude", path);
    map_format_path(min_path, sizeof(min_path), "%s.min_intensity", path);
    map_format_path(max_path, sizeof(max_path), "%s.max_intensity", path);
    map_format_path(phase_path, sizeof(phase_path), "%s.phase", path);
    map_format_path(axis_path, sizeof(axis_path), "%s.axis", path);
    map_format_path(radius_path, sizeof(radius_path), "%s.radius", path);
    map_format_path(properties_path, sizeof(properties_path), "%s.properties", path);

    yyjson_val *min_intensity = map_obj_get(animation, "min_intensity");
    yyjson_val *max_intensity = map_obj_get(animation, "max_intensity");
    if (!map_validate_optional_bool(ctx, animation, "enabled", enabled_path, "light animation enabled") ||
        !map_validate_light_animation_type(ctx, animation, type_path) ||
        !map_validate_light_animation_preset(ctx, animation, preset_path) ||
        !map_validate_optional_non_negative_number(ctx, animation, "rate_hz", rate_path, "light animation rate") ||
        !map_validate_optional_non_negative_number(ctx, animation, "amplitude", amplitude_path,
                                                   "light animation amplitude") ||
        !map_validate_optional_non_negative_number(ctx, animation, "min_intensity", min_path,
                                                   "light animation min intensity") ||
        !map_validate_optional_non_negative_number(ctx, animation, "max_intensity", max_path,
                                                   "light animation max intensity") ||
        !map_validate_optional_non_negative_number(ctx, animation, "phase", phase_path, "light animation phase") ||
        !map_validate_optional_vec3(ctx, animation, "axis", axis_path, "light animation axis") ||
        !map_validate_optional_positive_number(ctx, animation, "radius", radius_path, "light animation radius") ||
        !map_validate_properties(ctx, map_obj_get(animation, "properties"), properties_path))
    {
        return false;
    }
    if (min_intensity != NULL && max_intensity != NULL && yyjson_is_num(min_intensity) &&
        yyjson_is_num(max_intensity) && yyjson_get_num(max_intensity) < yyjson_get_num(min_intensity))
    {
        return map_error(ctx, max_path, "light animation max_intensity must be greater than or equal to min_intensity");
    }
    return true;
}

static bool map_validate_optional_non_negative_number(map_validation_context *ctx, yyjson_val *object, const char *key,
                                                      const char *json_path, const char *description)
{
    yyjson_val *value = map_obj_get(object, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_num(value) || yyjson_get_num(value) < 0.0)
        return map_error(ctx, json_path, "%s must be a non-negative number", description);
    return true;
}

static bool map_validate_optional_positive_number(map_validation_context *ctx, yyjson_val *object, const char *key,
                                                  const char *json_path, const char *description)
{
    yyjson_val *value = map_obj_get(object, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_num(value) || yyjson_get_num(value) <= 0.0)
        return map_error(ctx, json_path, "%s must be a positive number", description);
    return true;
}

static bool map_validate_optional_non_negative_int(map_validation_context *ctx, yyjson_val *object, const char *key,
                                                   const char *json_path, const char *description)
{
    yyjson_val *value = map_obj_get(object, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_int(value) || yyjson_get_int(value) < 0)
        return map_error(ctx, json_path, "%s must be a non-negative integer", description);
    return true;
}

static bool map_validate_optional_bool(map_validation_context *ctx, yyjson_val *object, const char *key,
                                       const char *json_path, const char *description)
{
    yyjson_val *value = map_obj_get(object, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_bool(value))
        return map_error(ctx, json_path, "%s must be a boolean", description);
    return true;
}

static bool map_validate_tonemap(map_validation_context *ctx, yyjson_val *object, const char *key,
                                 const char *json_path)
{
    const char *value = map_json_string(object, key);
    if (value == NULL)
        return map_obj_get(object, key) == NULL || map_error(ctx, json_path, "global tonemap must be a string");
    if (SDL_strcmp(value, "none") == 0 || SDL_strcmp(value, "reinhard") == 0 || SDL_strcmp(value, "aces") == 0)
        return true;
    return map_error(ctx, json_path, "global tonemap must be none, reinhard, or aces");
}

static bool map_validate_lighting_preview_quality(map_validation_context *ctx, yyjson_val *object, const char *key,
                                                  const char *json_path)
{
    const char *value = map_json_string(object, key);
    if (value == NULL)
        return map_obj_get(object, key) == NULL ||
               map_error(ctx, json_path, "global lighting_preview_quality must be a string");
    if (SDL_strcmp(value, "performance") == 0 || SDL_strcmp(value, "balanced") == 0 ||
        SDL_strcmp(value, "quality") == 0)
    {
        return true;
    }
    return map_error(ctx, json_path, "global lighting_preview_quality must be performance, balanced, or quality");
}

static bool map_validate_fog_mode(map_validation_context *ctx, yyjson_val *fog, const char *path)
{
    const char *value = map_json_string(fog, "mode");
    if (value == NULL)
        return map_obj_get(fog, "mode") == NULL || map_error(ctx, path, "global fog mode must be a string");
    if (SDL_strcmp(value, "none") == 0 || SDL_strcmp(value, "linear") == 0 || SDL_strcmp(value, "exp") == 0 ||
        SDL_strcmp(value, "exp2") == 0)
    {
        return true;
    }
    return map_error(ctx, path, "global fog mode must be none, linear, exp, or exp2");
}

static bool map_validate_global_fog(map_validation_context *ctx, yyjson_val *fog)
{
    if (fog == NULL)
        return true;
    if (!yyjson_is_obj(fog))
        return map_error(ctx, "$.global.fog", "global fog must be an object");
    yyjson_val *start = map_obj_get(fog, "start");
    yyjson_val *end = map_obj_get(fog, "end");
    if (!map_validate_optional_bool(ctx, fog, "enabled", "$.global.fog.enabled", "global fog enabled") ||
        !map_validate_fog_mode(ctx, fog, "$.global.fog.mode") ||
        !map_validate_optional_color(ctx, fog, "color", "$.global.fog.color", "global fog color") ||
        !map_validate_optional_non_negative_number(ctx, fog, "start", "$.global.fog.start", "global fog start") ||
        !map_validate_optional_non_negative_number(ctx, fog, "end", "$.global.fog.end", "global fog end") ||
        !map_validate_optional_non_negative_number(ctx, fog, "density", "$.global.fog.density", "global fog density") ||
        !map_validate_properties(ctx, map_obj_get(fog, "properties"), "$.global.fog.properties"))
    {
        return false;
    }
    if (start != NULL && end != NULL && yyjson_is_num(start) && yyjson_is_num(end) &&
        yyjson_get_num(end) <= yyjson_get_num(start))
    {
        return map_error(ctx, "$.global.fog.end", "global fog end must be greater than start");
    }
    return true;
}

static bool map_validate_global_state(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *global = map_obj_get(root, "global");
    if (global == NULL)
        return true;
    if (!yyjson_is_obj(global))
        return map_error(ctx, "$.global", "global map state must be an object");
    return map_validate_optional_color(ctx, global, "ambient_light", "$.global.ambient_light",
                                       "global ambient light") &&
           map_validate_optional_color(ctx, global, "clear_color", "$.global.clear_color", "global clear color") &&
           map_validate_optional_non_negative_number(ctx, global, "exposure", "$.global.exposure", "global exposure") &&
           map_validate_tonemap(ctx, global, "tonemap", "$.global.tonemap") &&
           map_validate_lighting_preview_quality(ctx, global, "lighting_preview_quality",
                                                 "$.global.lighting_preview_quality") &&
           map_validate_global_fog(ctx, map_obj_get(global, "fog")) &&
           map_validate_properties(ctx, map_obj_get(global, "properties"), "$.global.properties");
}

static bool map_validate_lights(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *lights = map_obj_get(root, "lights");
    if (lights == NULL)
        return true;
    if (!yyjson_is_arr(lights))
        return map_error(ctx, "$.lights", "lights must be an array");
    size_t runtime_light_count = 0u;
    size_t bake_light_count = 0u;
    for (size_t i = 0, count = yyjson_arr_size(lights); i < count; ++i)
    {
        char path[MAP_PATH_MAX];
        char id_path[MAP_PATH_MAX];
        char source_actor_path[MAP_PATH_MAX];
        char kind_path[MAP_PATH_MAX];
        char type_path[MAP_PATH_MAX];
        char transform_path[MAP_PATH_MAX];
        char direction_path[MAP_PATH_MAX];
        char color_path[MAP_PATH_MAX];
        char intensity_path[MAP_PATH_MAX];
        char range_path[MAP_PATH_MAX];
        char inner_path[MAP_PATH_MAX];
        char outer_path[MAP_PATH_MAX];
        char width_path[MAP_PATH_MAX];
        char height_path[MAP_PATH_MAX];
        char radius_path[MAP_PATH_MAX];
        char shadow_mode_path[MAP_PATH_MAX];
        char falloff_path[MAP_PATH_MAX];
        char animation_path[MAP_PATH_MAX];
        map_format_path(path, sizeof(path), "$.lights[%zu]", i);
        map_format_path(id_path, sizeof(id_path), "%s.id", path);
        map_format_path(source_actor_path, sizeof(source_actor_path), "%s.source_actor", path);
        map_format_path(kind_path, sizeof(kind_path), "%s.kind", path);
        map_format_path(type_path, sizeof(type_path), "%s.type", path);
        map_format_path(transform_path, sizeof(transform_path), "%s.transform", path);
        map_format_path(direction_path, sizeof(direction_path), "%s.direction", path);
        map_format_path(color_path, sizeof(color_path), "%s.color", path);
        map_format_path(intensity_path, sizeof(intensity_path), "%s.intensity", path);
        map_format_path(range_path, sizeof(range_path), "%s.range", path);
        map_format_path(inner_path, sizeof(inner_path), "%s.inner_angle_degrees", path);
        map_format_path(outer_path, sizeof(outer_path), "%s.outer_angle_degrees", path);
        map_format_path(width_path, sizeof(width_path), "%s.width", path);
        map_format_path(height_path, sizeof(height_path), "%s.height", path);
        map_format_path(radius_path, sizeof(radius_path), "%s.radius", path);
        map_format_path(shadow_mode_path, sizeof(shadow_mode_path), "%s.shadow_mode", path);
        map_format_path(falloff_path, sizeof(falloff_path), "%s.falloff", path);
        map_format_path(animation_path, sizeof(animation_path), "%s.animation", path);
        yyjson_val *light = yyjson_arr_get(lights, i);
        if (!yyjson_is_obj(light))
            return map_error(ctx, path, "light entry must be an object");
        yyjson_val *casts_shadow = map_obj_get(light, "casts_shadow");
        const char *source_actor = map_json_string(light, "source_actor");
        const char *kind = map_json_string(light, "kind");
        const char *type = map_json_string(light, "type");
        if (!map_require_non_empty_string(ctx, light, "id", id_path, "light id") ||
            !map_add_unique(ctx, &ctx->object_ids, "object", map_json_string(light, "id"), path) ||
            !map_optional_non_empty_string(ctx, light, "source_actor", source_actor_path, "light source actor") ||
            (source_actor != NULL && !map_name_table_contains(&ctx->object_ids, source_actor) &&
             !map_error(ctx, source_actor_path, "unknown light source_actor '%s'", source_actor)) ||
            !map_validate_light_kind(ctx, light, kind_path) || !map_validate_light_type(ctx, light, type_path) ||
            !map_validate_transform(ctx, map_obj_get(light, "transform"), transform_path) ||
            !map_validate_optional_vec3(ctx, light, "direction", direction_path, "light direction") ||
            !map_validate_optional_color(ctx, light, "color", color_path, "light color") ||
            !map_validate_optional_non_negative_number(ctx, light, "intensity", intensity_path, "light intensity") ||
            !map_validate_optional_positive_number(ctx, light, "range", range_path, "light range") ||
            !map_validate_optional_positive_number(ctx, light, "inner_angle_degrees", inner_path,
                                                   "light inner angle") ||
            !map_validate_optional_positive_number(ctx, light, "outer_angle_degrees", outer_path,
                                                   "light outer angle") ||
            !map_validate_optional_positive_number(ctx, light, "width", width_path, "light width") ||
            !map_validate_optional_positive_number(ctx, light, "height", height_path, "light height") ||
            !map_validate_optional_positive_number(ctx, light, "radius", radius_path, "light radius") ||
            (casts_shadow != NULL && !yyjson_is_bool(casts_shadow) &&
             !map_error(ctx, path, "light casts_shadow must be a boolean")) ||
            !map_validate_light_shadow_mode(ctx, light, shadow_mode_path) ||
            !map_validate_light_falloff(ctx, light, falloff_path) ||
            !map_optional_non_empty_string(ctx, light, "bake_group", path, "light bake group") ||
            !map_validate_light_animation(ctx, map_obj_get(light, "animation"), animation_path) ||
            !map_validate_properties(ctx, map_obj_get(light, "properties"), path))
        {
            return false;
        }
        yyjson_val *inner_angle = map_obj_get(light, "inner_angle_degrees");
        yyjson_val *outer_angle = map_obj_get(light, "outer_angle_degrees");
        yyjson_val *width = map_obj_get(light, "width");
        yyjson_val *height = map_obj_get(light, "height");
        yyjson_val *radius = map_obj_get(light, "radius");
        if (SDL_strcmp(type, "spot") == 0 && yyjson_is_num(inner_angle) && yyjson_is_num(outer_angle) &&
            yyjson_get_num(outer_angle) < yyjson_get_num(inner_angle))
        {
            return map_error(ctx, outer_path,
                             "spot light outer_angle_degrees must be greater than or equal to inner_angle_degrees");
        }
        if (SDL_strcmp(type, "area_rect") == 0 && (!yyjson_is_num(width) || !yyjson_is_num(height)))
            return map_error(ctx, path, "area_rect light requires positive width and height");
        if (SDL_strcmp(type, "area_sphere") == 0 && !yyjson_is_num(radius))
            return map_error(ctx, radius_path, "area_sphere light requires a positive radius");

        const bool static_light = map_light_kind_bakes(kind) || map_light_type_is_area(type);
        const bool dynamic_light = map_light_kind_runs_dynamically(kind);
        bake_light_count += static_light ? 1u : 0u;
        runtime_light_count += dynamic_light || static_light ? 1u : 0u;
    }
    if (runtime_light_count > MAP_LIGHTING_DEFAULT_DYNAMIC_LIGHT_BUDGET &&
        !map_warning(ctx, "$.lights",
                     "map has %zu runtime-preview lights; default dynamic light budget is %u. Reduce dynamic lights, "
                     "mark lights as baked/static where practical, or compute/bake static lighting.",
                     runtime_light_count, MAP_LIGHTING_DEFAULT_DYNAMIC_LIGHT_BUDGET))
    {
        return false;
    }
    if (bake_light_count > MAP_LIGHTING_DEFAULT_STATIC_LIGHT_BUDGET &&
        !map_warning(ctx, "$.lights",
                     "map has %zu static/baked lights; default static light budget is %u. Split bake groups, reduce "
                     "authored static lights, or raise the bake budget for offline builds.",
                     bake_light_count, MAP_LIGHTING_DEFAULT_STATIC_LIGHT_BUDGET))
    {
        return false;
    }
    return true;
}

static bool map_validate_effects(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *effects = map_obj_get(root, "effects");
    if (effects == NULL)
        return true;
    if (!yyjson_is_arr(effects))
        return map_error(ctx, "$.effects", "effects must be an array");
    for (size_t i = 0, count = yyjson_arr_size(effects); i < count; ++i)
    {
        char path[MAP_PATH_MAX];
        char id_path[MAP_PATH_MAX];
        char source_actor_path[MAP_PATH_MAX];
        char kind_path[MAP_PATH_MAX];
        char type_path[MAP_PATH_MAX];
        char transform_path[MAP_PATH_MAX];
        char color_path[MAP_PATH_MAX];
        char asset_path[MAP_PATH_MAX];
        char texture_path[MAP_PATH_MAX];
        char sprite_path[MAP_PATH_MAX];
        char radius_path[MAP_PATH_MAX];
        char duration_path[MAP_PATH_MAX];
        char density_path[MAP_PATH_MAX];
        char max_particles_path[MAP_PATH_MAX];
        char loop_path[MAP_PATH_MAX];
        char emissive_path[MAP_PATH_MAX];
        char preview_path[MAP_PATH_MAX];
        map_format_path(path, sizeof(path), "$.effects[%zu]", i);
        map_format_path(id_path, sizeof(id_path), "%s.id", path);
        map_format_path(source_actor_path, sizeof(source_actor_path), "%s.source_actor", path);
        map_format_path(kind_path, sizeof(kind_path), "%s.kind", path);
        map_format_path(type_path, sizeof(type_path), "%s.type", path);
        map_format_path(transform_path, sizeof(transform_path), "%s.transform", path);
        map_format_path(color_path, sizeof(color_path), "%s.color", path);
        map_format_path(asset_path, sizeof(asset_path), "%s.asset", path);
        map_format_path(texture_path, sizeof(texture_path), "%s.texture", path);
        map_format_path(sprite_path, sizeof(sprite_path), "%s.sprite", path);
        map_format_path(radius_path, sizeof(radius_path), "%s.radius", path);
        map_format_path(duration_path, sizeof(duration_path), "%s.duration", path);
        map_format_path(density_path, sizeof(density_path), "%s.density", path);
        map_format_path(max_particles_path, sizeof(max_particles_path), "%s.max_particles", path);
        map_format_path(loop_path, sizeof(loop_path), "%s.loop", path);
        map_format_path(emissive_path, sizeof(emissive_path), "%s.emissive", path);
        map_format_path(preview_path, sizeof(preview_path), "%s.preview", path);
        yyjson_val *effect = yyjson_arr_get(effects, i);
        if (!yyjson_is_obj(effect))
            return map_error(ctx, path, "effect entry must be an object");
        const char *source_actor = map_json_string(effect, "source_actor");
        if (!map_require_non_empty_string(ctx, effect, "id", id_path, "effect id") ||
            !map_add_unique(ctx, &ctx->object_ids, "object", map_json_string(effect, "id"), path) ||
            !map_optional_non_empty_string(ctx, effect, "source_actor", source_actor_path, "effect source actor") ||
            (source_actor != NULL && !map_name_table_contains(&ctx->object_ids, source_actor) &&
             !map_error(ctx, source_actor_path, "unknown effect source_actor '%s'", source_actor)) ||
            !map_require_non_empty_string(ctx, effect, "kind", kind_path, "effect kind") ||
            !map_optional_non_empty_string(ctx, effect, "type", type_path, "effect type") ||
            !map_validate_transform(ctx, map_obj_get(effect, "transform"), transform_path) ||
            !map_validate_optional_color(ctx, effect, "color", color_path, "effect color") ||
            !map_validate_asset_reference(ctx, effect, "asset", asset_path, "effect asset") ||
            !map_validate_asset_reference(ctx, effect, "texture", texture_path, "effect texture") ||
            !map_validate_asset_reference(ctx, effect, "sprite", sprite_path, "effect sprite") ||
            !map_validate_optional_positive_number(ctx, effect, "radius", radius_path, "effect radius") ||
            !map_validate_optional_non_negative_number(ctx, effect, "duration", duration_path, "effect duration") ||
            !map_validate_optional_non_negative_number(ctx, effect, "density", density_path, "effect density") ||
            !map_validate_optional_non_negative_int(ctx, effect, "max_particles", max_particles_path,
                                                    "effect max_particles") ||
            !map_validate_optional_bool(ctx, effect, "loop", loop_path, "effect loop") ||
            !map_validate_optional_bool(ctx, effect, "emissive", emissive_path, "effect emissive") ||
            !map_validate_optional_bool(ctx, effect, "preview", preview_path, "effect preview") ||
            !map_validate_properties(ctx, map_obj_get(effect, "properties"), path))
        {
            return false;
        }
    }
    return true;
}

static bool map_validate_skybox_faces(map_validation_context *ctx, yyjson_val *faces, const char *path)
{
    if (!yyjson_is_obj(faces))
        return map_error(ctx, path, "skybox faces must be an object");
    static const char *const keys[] = {"pos_x", "neg_x", "pos_y", "neg_y", "pos_z", "neg_z"};
    for (size_t i = 0; i < SDL_arraysize(keys); ++i)
    {
        char face_path[MAP_PATH_MAX];
        map_format_path(face_path, sizeof(face_path), "%s.%s", path, keys[i]);
        if (!map_validate_asset_reference(ctx, faces, keys[i], face_path, "skybox face"))
            return false;
        if (map_obj_get(faces, keys[i]) == NULL)
            return map_error(ctx, face_path, "skybox face is required when faces are authored");
    }
    return true;
}

static bool map_validate_skybox(map_validation_context *ctx, yyjson_val *root)
{
    yyjson_val *skybox = map_obj_get(root, "skybox");
    if (skybox == NULL)
        return true;
    if (yyjson_is_str(skybox))
        return map_validate_project_relative_reference(ctx, yyjson_get_str(skybox), "$.skybox", "skybox");
    if (!yyjson_is_obj(skybox))
        return map_error(ctx, "$.skybox", "skybox must be an object or non-empty asset id/path");
    yyjson_val *size = map_obj_get(skybox, "size");
    yyjson_val *faces = map_obj_get(skybox, "faces");
    if (!map_optional_non_empty_string(ctx, skybox, "id", "$.skybox.id", "skybox id") ||
        !map_validate_asset_reference(ctx, skybox, "asset", "$.skybox.asset", "skybox asset") ||
        (faces != NULL && !map_validate_skybox_faces(ctx, faces, "$.skybox.faces")) ||
        (size != NULL && (!yyjson_is_num(size) || yyjson_get_num(size) <= 1.0) &&
         !map_error(ctx, "$.skybox.size", "skybox size must be greater than 1")) ||
        !map_validate_properties(ctx, map_obj_get(skybox, "properties"), "$.skybox"))
    {
        return false;
    }
    if (faces == NULL && map_obj_get(skybox, "asset") == NULL)
        return map_error(ctx, "$.skybox", "skybox requires either asset or faces");
    return true;
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

    return map_validate_metadata(ctx, root) && map_validate_global_state(ctx, root) && map_validate_assets(ctx, root) &&
           map_validate_materials(ctx, root) && map_validate_brushes(ctx, root) && map_validate_actors(ctx, root) &&
           map_validate_prefabs(ctx, root) && map_validate_lights(ctx, root) && map_validate_effects(ctx, root) &&
           map_validate_skybox(ctx, root) && map_validate_connections(ctx, root) &&
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

size_t slayer3d_map_get_prefab_count(const slayer3d_map_document *document)
{
    return map_array_count(document, "prefabs");
}

size_t slayer3d_map_get_light_count(const slayer3d_map_document *document)
{
    return map_array_count(document, "lights");
}

size_t slayer3d_map_get_effect_count(const slayer3d_map_document *document)
{
    return map_array_count(document, "effects");
}

bool slayer3d_map_has_skybox(const slayer3d_map_document *document)
{
    if (document == NULL || document->doc == NULL)
        return false;
    yyjson_val *root = yyjson_doc_get_root(document->doc);
    return map_obj_get(root, "skybox") != NULL;
}

size_t slayer3d_map_get_connection_count(const slayer3d_map_document *document)
{
    return map_array_count(document, "connections");
}

static yyjson_val *map_root_array_item(const slayer3d_map_document *document, const char *key, size_t index)
{
    if (document == NULL || document->doc == NULL || key == NULL)
        return NULL;
    yyjson_val *root = yyjson_doc_get_root(document->doc);
    yyjson_val *array = map_obj_get(root, key);
    return yyjson_is_arr(array) && index < yyjson_arr_size(array) ? yyjson_arr_get(array, index) : NULL;
}

static const char *map_asset_kind_key(slayer3d_map_asset_kind kind)
{
    switch (kind)
    {
    case SLAYER3D_MAP_ASSET_TEXTURE:
        return "textures";
    case SLAYER3D_MAP_ASSET_MODEL:
        return "models";
    case SLAYER3D_MAP_ASSET_SPRITE:
        return "sprites";
    case SLAYER3D_MAP_ASSET_SKYBOX:
        return "skyboxes";
    case SLAYER3D_MAP_ASSET_EFFECT:
        return "effects";
    default:
        return NULL;
    }
}

static yyjson_val *map_asset_item(const slayer3d_map_document *document, slayer3d_map_asset_kind kind, size_t index)
{
    if (document == NULL || document->doc == NULL)
        return NULL;
    const char *key = map_asset_kind_key(kind);
    if (key == NULL)
        return NULL;
    yyjson_val *assets = map_obj_get(yyjson_doc_get_root(document->doc), "assets");
    yyjson_val *array = map_obj_get(assets, key);
    return yyjson_is_arr(array) && index < yyjson_arr_size(array) ? yyjson_arr_get(array, index) : NULL;
}

static yyjson_val *map_properties_object(yyjson_val *object)
{
    yyjson_val *properties = map_obj_get(object, "properties");
    return yyjson_is_obj(properties) ? properties : NULL;
}

static size_t map_properties_count(yyjson_val *object)
{
    yyjson_val *properties = map_properties_object(object);
    return properties != NULL ? yyjson_obj_size(properties) : 0U;
}

static const char *map_property_key_at(yyjson_val *object, size_t property_index)
{
    yyjson_val *properties = map_properties_object(object);
    if (properties == NULL || property_index >= yyjson_obj_size(properties))
        return NULL;
    yyjson_val *key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    for (size_t i = 0; (key = yyjson_obj_iter_next(&iter)) != NULL; ++i)
    {
        if (i == property_index)
            return yyjson_get_str(key);
    }
    return NULL;
}

static bool map_write_value_json(yyjson_val *value, char **out_json, size_t *out_json_size, char *error_buffer,
                                 int error_buffer_size)
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
    if (value == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: map property value was not found");
        return false;
    }
    size_t size = 0U;
    char *json = yyjson_val_write(value, YYJSON_WRITE_NOFLAG, &size);
    if (json == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: failed to serialize map property JSON");
        return false;
    }
    *out_json = json;
    if (out_json_size != NULL)
        *out_json_size = size;
    return true;
}

static bool map_get_property_json_from_object(yyjson_val *object, const char *key, char **out_json,
                                              size_t *out_json_size, char *error_buffer, int error_buffer_size)
{
    if (key == NULL || key[0] == '\0')
    {
        map_clear_error(error_buffer, error_buffer_size);
        map_set_error(error_buffer, error_buffer_size, "$: map property key is required");
        return false;
    }
    yyjson_val *properties = map_properties_object(object);
    return map_write_value_json(map_obj_get(properties, key), out_json, out_json_size, error_buffer, error_buffer_size);
}

static bool map_read_vec3_value(yyjson_val *array, slayer3d_vec3 *out_value)
{
    if (!yyjson_is_arr(array) || yyjson_arr_size(array) != 3U || out_value == NULL)
        return false;
    yyjson_val *x = yyjson_arr_get(array, 0);
    yyjson_val *y = yyjson_arr_get(array, 1);
    yyjson_val *z = yyjson_arr_get(array, 2);
    if (!yyjson_is_num(x) || !yyjson_is_num(y) || !yyjson_is_num(z))
        return false;
    out_value->x = (float)yyjson_get_num(x);
    out_value->y = (float)yyjson_get_num(y);
    out_value->z = (float)yyjson_get_num(z);
    return true;
}

static bool map_read_color_value(yyjson_val *array, slayer3d_color *out_color)
{
    if (!yyjson_is_arr(array) || yyjson_arr_size(array) != 4U || out_color == NULL)
        return false;
    yyjson_val *r = yyjson_arr_get(array, 0);
    yyjson_val *g = yyjson_arr_get(array, 1);
    yyjson_val *b = yyjson_arr_get(array, 2);
    yyjson_val *a = yyjson_arr_get(array, 3);
    if (!yyjson_is_num(r) || !yyjson_is_num(g) || !yyjson_is_num(b) || !yyjson_is_num(a))
        return false;
    out_color->r = (Uint8)SDL_clamp((int)yyjson_get_num(r), 0, 255);
    out_color->g = (Uint8)SDL_clamp((int)yyjson_get_num(g), 0, 255);
    out_color->b = (Uint8)SDL_clamp((int)yyjson_get_num(b), 0, 255);
    out_color->a = (Uint8)SDL_clamp((int)yyjson_get_num(a), 0, 255);
    return true;
}

static bool map_read_optional_color(yyjson_val *object, const char *key, slayer3d_color *out_color)
{
    yyjson_val *value = map_obj_get(object, key);
    return value != NULL && map_read_color_value(value, out_color);
}

static bool map_read_optional_float(yyjson_val *object, const char *key, float *out_value)
{
    yyjson_val *value = map_obj_get(object, key);
    if (!yyjson_is_num(value) || out_value == NULL)
        return false;
    *out_value = (float)yyjson_get_num(value);
    return true;
}

static bool map_read_optional_bool_value(yyjson_val *object, const char *key, bool *out_value)
{
    yyjson_val *value = map_obj_get(object, key);
    if (!yyjson_is_bool(value) || out_value == NULL)
        return false;
    *out_value = yyjson_get_bool(value);
    return true;
}

size_t slayer3d_map_get_asset_count(const slayer3d_map_document *document, slayer3d_map_asset_kind kind)
{
    if (document == NULL || document->doc == NULL)
        return 0U;
    const char *key = map_asset_kind_key(kind);
    if (key == NULL)
        return 0U;
    yyjson_val *assets = map_obj_get(yyjson_doc_get_root(document->doc), "assets");
    yyjson_val *array = map_obj_get(assets, key);
    return yyjson_is_arr(array) ? yyjson_arr_size(array) : 0U;
}

bool slayer3d_map_get_asset(const slayer3d_map_document *document, slayer3d_map_asset_kind kind, size_t index,
                            slayer3d_map_asset *out_asset)
{
    yyjson_val *asset = map_asset_item(document, kind, index);
    if (!yyjson_is_obj(asset) || out_asset == NULL)
        return false;
    SDL_zero(*out_asset);
    out_asset->id = map_json_string(asset, "id");
    out_asset->path = map_json_string(asset, "path");
    out_asset->property_count = map_properties_count(asset);
    return true;
}

static void map_read_transform(yyjson_val *object, slayer3d_map_transform *out_transform)
{
    if (out_transform == NULL)
        return;
    SDL_zero(*out_transform);
    yyjson_val *transform = map_obj_get(object, "transform");
    if (!yyjson_is_obj(transform))
        return;
    out_transform->has_position = map_read_vec3_value(map_obj_get(transform, "position"), &out_transform->position);
    out_transform->has_rotation = map_read_vec3_value(map_obj_get(transform, "rotation"), &out_transform->rotation);
    out_transform->has_scale = map_read_vec3_value(map_obj_get(transform, "scale"), &out_transform->scale);
    out_transform->has_facing = map_read_vec3_value(map_obj_get(transform, "facing"), &out_transform->facing);
}

bool slayer3d_map_get_material(const slayer3d_map_document *document, size_t index, slayer3d_map_material *out_material)
{
    yyjson_val *material = map_root_array_item(document, "materials", index);
    if (!yyjson_is_obj(material) || out_material == NULL)
        return false;
    SDL_zero(*out_material);
    out_material->id = map_json_string(material, "id");
    out_material->texture = map_json_string(material, "texture");
    out_material->has_color = map_read_optional_color(material, "color", &out_material->color);
    out_material->has_tint = map_read_optional_color(material, "tint", &out_material->tint);
    out_material->property_count = map_properties_count(material);
    return true;
}

bool slayer3d_map_get_brush(const slayer3d_map_document *document, size_t index, slayer3d_map_brush *out_brush)
{
    yyjson_val *brush = map_root_array_item(document, "brushes", index);
    if (!yyjson_is_obj(brush) || out_brush == NULL)
        return false;
    SDL_zero(*out_brush);
    out_brush->id = map_json_string(brush, "id");
    yyjson_val *geometry = map_obj_get(brush, "geometry");
    out_brush->geometry_kind = map_json_string(geometry, "kind");
    if (out_brush->geometry_kind != NULL && SDL_strcmp(out_brush->geometry_kind, "box") == 0)
    {
        out_brush->box.valid = map_read_vec3_value(map_obj_get(geometry, "min"), &out_brush->box.min) &&
                               map_read_vec3_value(map_obj_get(geometry, "max"), &out_brush->box.max);
    }
    out_brush->material = map_json_string(brush, "material");
    out_brush->has_color = map_read_optional_color(brush, "color", &out_brush->color);
    yyjson_val *faces = map_obj_get(brush, "faces");
    out_brush->face_count = yyjson_is_arr(faces) ? yyjson_arr_size(faces) : 0U;
    out_brush->property_count = map_properties_count(brush);
    return true;
}

bool slayer3d_map_get_actor(const slayer3d_map_document *document, size_t index, slayer3d_map_actor *out_actor)
{
    yyjson_val *actor = map_root_array_item(document, "actors", index);
    if (!yyjson_is_obj(actor) || out_actor == NULL)
        return false;
    SDL_zero(*out_actor);
    out_actor->id = map_json_string(actor, "id");
    out_actor->archetype = map_json_string(actor, "archetype");
    out_actor->model = map_json_string(actor, "model");
    out_actor->sprite = map_json_string(actor, "sprite");
    out_actor->primitive = map_json_string(actor, "primitive");
    out_actor->prefab = map_json_string(actor, "prefab");
    yyjson_val *prefab_linked = map_obj_get(actor, "prefab_linked");
    out_actor->prefab_linked = yyjson_is_bool(prefab_linked) && yyjson_get_bool(prefab_linked);
    out_actor->display_mode = map_json_string(actor, "display_mode");
    map_read_transform(actor, &out_actor->transform);
    out_actor->has_color = map_read_optional_color(actor, "color", &out_actor->color);
    out_actor->property_count = map_properties_count(actor);
    return true;
}

static void map_read_light_animation(yyjson_val *light, slayer3d_map_light_animation *out_animation)
{
    if (out_animation == NULL)
        return;
    SDL_zero(*out_animation);
    out_animation->type = "none";

    yyjson_val *animation = map_obj_get(light, "animation");
    if (!yyjson_is_obj(animation))
        return;

    map_read_optional_bool_value(animation, "enabled", &out_animation->enabled);
    const char *type = map_json_string(animation, "type");
    if (type != NULL)
        out_animation->type = type;
    out_animation->preset = map_json_string(animation, "preset");
    out_animation->has_rate_hz = map_read_optional_float(animation, "rate_hz", &out_animation->rate_hz);
    out_animation->has_amplitude = map_read_optional_float(animation, "amplitude", &out_animation->amplitude);
    out_animation->has_phase = map_read_optional_float(animation, "phase", &out_animation->phase);
    out_animation->has_min_intensity =
        map_read_optional_float(animation, "min_intensity", &out_animation->min_intensity);
    out_animation->has_max_intensity =
        map_read_optional_float(animation, "max_intensity", &out_animation->max_intensity);
    out_animation->has_axis = map_read_vec3_value(map_obj_get(animation, "axis"), &out_animation->axis);
    out_animation->has_radius = map_read_optional_float(animation, "radius", &out_animation->radius);
    out_animation->property_count = map_properties_count(animation);
}

bool slayer3d_map_get_light(const slayer3d_map_document *document, size_t index, slayer3d_map_light *out_light)
{
    yyjson_val *light = map_root_array_item(document, "lights", index);
    if (!yyjson_is_obj(light) || out_light == NULL)
        return false;
    SDL_zero(*out_light);
    out_light->id = map_json_string(light, "id");
    out_light->source_actor = map_json_string(light, "source_actor");
    out_light->kind = map_json_string(light, "kind");
    out_light->type = map_json_string(light, "type");
    map_read_transform(light, &out_light->transform);
    out_light->has_direction = map_read_vec3_value(map_obj_get(light, "direction"), &out_light->direction);
    out_light->has_color = map_read_optional_color(light, "color", &out_light->color);
    out_light->has_intensity = map_read_optional_float(light, "intensity", &out_light->intensity);
    out_light->has_range = map_read_optional_float(light, "range", &out_light->range);
    out_light->has_inner_angle_degrees =
        map_read_optional_float(light, "inner_angle_degrees", &out_light->inner_angle_degrees);
    out_light->has_outer_angle_degrees =
        map_read_optional_float(light, "outer_angle_degrees", &out_light->outer_angle_degrees);
    out_light->has_width = map_read_optional_float(light, "width", &out_light->width);
    out_light->has_height = map_read_optional_float(light, "height", &out_light->height);
    out_light->has_radius = map_read_optional_float(light, "radius", &out_light->radius);
    out_light->has_casts_shadow = map_read_optional_bool_value(light, "casts_shadow", &out_light->casts_shadow);
    out_light->shadow_mode = map_json_string(light, "shadow_mode");
    out_light->falloff = map_json_string(light, "falloff");
    out_light->bake_group = map_json_string(light, "bake_group");
    map_read_light_animation(light, &out_light->animation);
    out_light->property_count = map_properties_count(light);
    return true;
}

bool slayer3d_map_get_global_state(const slayer3d_map_document *document, slayer3d_map_global_state *out_global)
{
    if (out_global == NULL)
        return false;
    SDL_zero(*out_global);

    out_global->ambient_light = (slayer3d_color){54, 56, 64, 255};
    out_global->clear_color = (slayer3d_color){12, 14, 18, 255};
    out_global->exposure = 1.0f;
    out_global->tonemap = "aces";
    out_global->lighting_preview_quality = "balanced";
    out_global->fog.mode = "none";

    if (document == NULL || document->doc == NULL)
        return false;

    yyjson_val *root = yyjson_doc_get_root(document->doc);
    yyjson_val *global = map_obj_get(root, "global");
    if (!yyjson_is_obj(global))
        return true;

    out_global->has_ambient_light = map_read_optional_color(global, "ambient_light", &out_global->ambient_light);
    out_global->has_clear_color = map_read_optional_color(global, "clear_color", &out_global->clear_color);
    out_global->has_exposure = map_read_optional_float(global, "exposure", &out_global->exposure);

    const char *tonemap = map_json_string(global, "tonemap");
    if (tonemap != NULL)
        out_global->tonemap = tonemap;
    const char *quality = map_json_string(global, "lighting_preview_quality");
    if (quality != NULL)
        out_global->lighting_preview_quality = quality;

    yyjson_val *fog = map_obj_get(global, "fog");
    if (yyjson_is_obj(fog))
    {
        yyjson_val *enabled = map_obj_get(fog, "enabled");
        out_global->fog.enabled = yyjson_is_bool(enabled) && yyjson_get_bool(enabled);
        const char *mode = map_json_string(fog, "mode");
        if (mode != NULL)
            out_global->fog.mode = mode;
        out_global->fog.has_color = map_read_optional_color(fog, "color", &out_global->fog.color);
        out_global->fog.has_start = map_read_optional_float(fog, "start", &out_global->fog.start);
        out_global->fog.has_end = map_read_optional_float(fog, "end", &out_global->fog.end);
        out_global->fog.has_density = map_read_optional_float(fog, "density", &out_global->fog.density);
    }

    out_global->property_count = map_properties_count(global);
    return true;
}

size_t slayer3d_map_get_property_count(const slayer3d_map_document *document)
{
    if (document == NULL || document->doc == NULL)
        return 0U;
    return map_properties_count(yyjson_doc_get_root(document->doc));
}

const char *slayer3d_map_get_property_key(const slayer3d_map_document *document, size_t property_index)
{
    if (document == NULL || document->doc == NULL)
        return NULL;
    return map_property_key_at(yyjson_doc_get_root(document->doc), property_index);
}

bool slayer3d_map_get_property_json(const slayer3d_map_document *document, const char *key, char **out_json,
                                    size_t *out_json_size, char *error_buffer, int error_buffer_size)
{
    yyjson_val *root = document != NULL && document->doc != NULL ? yyjson_doc_get_root(document->doc) : NULL;
    return map_get_property_json_from_object(root, key, out_json, out_json_size, error_buffer, error_buffer_size);
}

const char *slayer3d_map_get_asset_property_key(const slayer3d_map_document *document, slayer3d_map_asset_kind kind,
                                                size_t asset_index, size_t property_index)
{
    return map_property_key_at(map_asset_item(document, kind, asset_index), property_index);
}

bool slayer3d_map_get_asset_property_json(const slayer3d_map_document *document, slayer3d_map_asset_kind kind,
                                          size_t asset_index, const char *key, char **out_json, size_t *out_json_size,
                                          char *error_buffer, int error_buffer_size)
{
    return map_get_property_json_from_object(map_asset_item(document, kind, asset_index), key, out_json, out_json_size,
                                             error_buffer, error_buffer_size);
}

const char *slayer3d_map_get_material_property_key(const slayer3d_map_document *document, size_t material_index,
                                                   size_t property_index)
{
    return map_property_key_at(map_root_array_item(document, "materials", material_index), property_index);
}

bool slayer3d_map_get_material_property_json(const slayer3d_map_document *document, size_t material_index,
                                             const char *key, char **out_json, size_t *out_json_size,
                                             char *error_buffer, int error_buffer_size)
{
    return map_get_property_json_from_object(map_root_array_item(document, "materials", material_index), key, out_json,
                                             out_json_size, error_buffer, error_buffer_size);
}

const char *slayer3d_map_get_brush_property_key(const slayer3d_map_document *document, size_t brush_index,
                                                size_t property_index)
{
    return map_property_key_at(map_root_array_item(document, "brushes", brush_index), property_index);
}

bool slayer3d_map_get_brush_property_json(const slayer3d_map_document *document, size_t brush_index, const char *key,
                                          char **out_json, size_t *out_json_size, char *error_buffer,
                                          int error_buffer_size)
{
    return map_get_property_json_from_object(map_root_array_item(document, "brushes", brush_index), key, out_json,
                                             out_json_size, error_buffer, error_buffer_size);
}

const char *slayer3d_map_get_actor_property_key(const slayer3d_map_document *document, size_t actor_index,
                                                size_t property_index)
{
    return map_property_key_at(map_root_array_item(document, "actors", actor_index), property_index);
}

bool slayer3d_map_get_actor_property_json(const slayer3d_map_document *document, size_t actor_index, const char *key,
                                          char **out_json, size_t *out_json_size, char *error_buffer,
                                          int error_buffer_size)
{
    return map_get_property_json_from_object(map_root_array_item(document, "actors", actor_index), key, out_json,
                                             out_json_size, error_buffer, error_buffer_size);
}

const char *slayer3d_map_get_light_property_key(const slayer3d_map_document *document, size_t light_index,
                                                size_t property_index)
{
    return map_property_key_at(map_root_array_item(document, "lights", light_index), property_index);
}

bool slayer3d_map_get_light_property_json(const slayer3d_map_document *document, size_t light_index, const char *key,
                                          char **out_json, size_t *out_json_size, char *error_buffer,
                                          int error_buffer_size)
{
    return map_get_property_json_from_object(map_root_array_item(document, "lights", light_index), key, out_json,
                                             out_json_size, error_buffer, error_buffer_size);
}

static yyjson_val *map_global_object(const slayer3d_map_document *document)
{
    yyjson_val *root = document != NULL && document->doc != NULL ? yyjson_doc_get_root(document->doc) : NULL;
    yyjson_val *global = map_obj_get(root, "global");
    return yyjson_is_obj(global) ? global : NULL;
}

const char *slayer3d_map_get_global_property_key(const slayer3d_map_document *document, size_t property_index)
{
    return map_property_key_at(map_global_object(document), property_index);
}

bool slayer3d_map_get_global_property_json(const slayer3d_map_document *document, const char *key, char **out_json,
                                           size_t *out_json_size, char *error_buffer, int error_buffer_size)
{
    return map_get_property_json_from_object(map_global_object(document), key, out_json, out_json_size, error_buffer,
                                             error_buffer_size);
}

void slayer3d_map_init_lighting_build_options(slayer3d_map_lighting_build_options *options)
{
    if (options == NULL)
        return;
    SDL_zero(*options);
    options->quality = SLAYER3D_MAP_LIGHTING_BUILD_BALANCED;
    options->max_dynamic_lights = MAP_LIGHTING_DEFAULT_DYNAMIC_LIGHT_BUDGET;
    options->max_static_lights = MAP_LIGHTING_DEFAULT_STATIC_LIGHT_BUDGET;
    options->include_dynamic_preview = true;
}

static bool map_light_kind_bakes(const char *kind)
{
    return kind != NULL &&
           (SDL_strcmp(kind, "baked") == 0 || SDL_strcmp(kind, "static") == 0 || SDL_strcmp(kind, "both") == 0);
}

static bool map_light_kind_runs_dynamically(const char *kind)
{
    return kind == NULL || kind[0] == '\0' || SDL_strcmp(kind, "dynamic") == 0 || SDL_strcmp(kind, "both") == 0;
}

static bool map_light_type_is_area(const char *type)
{
    return type != NULL && (SDL_strcmp(type, "area_rect") == 0 || SDL_strcmp(type, "area_sphere") == 0);
}

static const char *map_lighting_quality_name(slayer3d_map_lighting_build_quality quality)
{
    switch (quality)
    {
    case SLAYER3D_MAP_LIGHTING_BUILD_PREVIEW:
        return "preview";
    case SLAYER3D_MAP_LIGHTING_BUILD_FINAL:
        return "final";
    case SLAYER3D_MAP_LIGHTING_BUILD_BALANCED:
    default:
        return "balanced";
    }
}

bool slayer3d_map_build_lighting_plan(const slayer3d_map_document *document,
                                      const slayer3d_map_lighting_build_options *options,
                                      slayer3d_map_lighting_build_plan *out_plan, char *error_buffer,
                                      int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    if (out_plan == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: output lighting build plan is required");
        return false;
    }
    SDL_zero(*out_plan);
    if (document == NULL || document->doc == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: map document is required");
        return false;
    }

    slayer3d_map_lighting_build_options defaults;
    slayer3d_map_init_lighting_build_options(&defaults);
    const slayer3d_map_lighting_build_options *effective = options != NULL ? options : &defaults;
    out_plan->quality = effective->quality;
    out_plan->max_dynamic_lights =
        effective->max_dynamic_lights > 0u ? effective->max_dynamic_lights : MAP_LIGHTING_DEFAULT_DYNAMIC_LIGHT_BUDGET;
    out_plan->max_static_lights =
        effective->max_static_lights > 0u ? effective->max_static_lights : MAP_LIGHTING_DEFAULT_STATIC_LIGHT_BUDGET;
    out_plan->has_dynamic_preview = effective->include_dynamic_preview;

    const size_t light_count = slayer3d_map_get_light_count(document);
    out_plan->total_light_count = light_count;
    for (size_t i = 0; i < light_count; ++i)
    {
        slayer3d_map_light light;
        if (!slayer3d_map_get_light(document, i, &light))
        {
            map_set_error(error_buffer, error_buffer_size, "$.lights[%zu]: failed to read map light", i);
            return false;
        }

        const bool area = map_light_type_is_area(light.type);
        const bool static_light = map_light_kind_bakes(light.kind) || area;
        const bool dynamic_light = map_light_kind_runs_dynamically(light.kind);

        out_plan->area_light_count += area ? 1u : 0u;
        out_plan->static_light_count += static_light ? 1u : 0u;
        out_plan->dynamic_light_count += dynamic_light ? 1u : 0u;
        out_plan->bake_light_count += static_light ? 1u : 0u;
        out_plan->runtime_light_count +=
            dynamic_light || (static_light && effective->include_dynamic_preview) ? 1u : 0u;
    }

    out_plan->requires_static_bake = out_plan->bake_light_count > 0u;
    out_plan->dynamic_light_budget_exceeded = out_plan->runtime_light_count > out_plan->max_dynamic_lights;
    out_plan->static_light_budget_exceeded = out_plan->bake_light_count > out_plan->max_static_lights;
    return true;
}

static bool map_lighting_manifest_add_counts(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                             const slayer3d_map_lighting_build_plan *plan)
{
    yyjson_mut_val *counts = yyjson_mut_obj(doc);
    return counts != NULL && yyjson_mut_obj_add_val(doc, root, "counts", counts) &&
           yyjson_mut_obj_add_uint(doc, counts, "total_lights", plan->total_light_count) &&
           yyjson_mut_obj_add_uint(doc, counts, "dynamic_lights", plan->dynamic_light_count) &&
           yyjson_mut_obj_add_uint(doc, counts, "static_lights", plan->static_light_count) &&
           yyjson_mut_obj_add_uint(doc, counts, "area_lights", plan->area_light_count) &&
           yyjson_mut_obj_add_uint(doc, counts, "runtime_preview_lights", plan->runtime_light_count) &&
           yyjson_mut_obj_add_uint(doc, counts, "bake_lights", plan->bake_light_count);
}

static bool map_lighting_manifest_add_budgets(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                              const slayer3d_map_lighting_build_plan *plan)
{
    yyjson_mut_val *budgets = yyjson_mut_obj(doc);
    return budgets != NULL && yyjson_mut_obj_add_val(doc, root, "budgets", budgets) &&
           yyjson_mut_obj_add_uint(doc, budgets, "max_dynamic_lights", plan->max_dynamic_lights) &&
           yyjson_mut_obj_add_uint(doc, budgets, "max_static_lights", plan->max_static_lights) &&
           yyjson_mut_obj_add_bool(doc, budgets, "dynamic_light_budget_exceeded",
                                   plan->dynamic_light_budget_exceeded) &&
           yyjson_mut_obj_add_bool(doc, budgets, "static_light_budget_exceeded", plan->static_light_budget_exceeded);
}

static bool map_lighting_manifest_add_artifacts(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                                const slayer3d_map_document *document,
                                                const slayer3d_map_lighting_build_plan *plan)
{
    yyjson_mut_val *artifacts = yyjson_mut_arr(doc);
    if (artifacts == NULL || !yyjson_mut_obj_add_val(doc, root, "artifacts", artifacts))
        return false;
    if (plan->bake_light_count == 0u)
        return true;

    yyjson_mut_val *artifact = yyjson_mut_obj(doc);
    return artifact != NULL && yyjson_mut_arr_add_val(artifacts, artifact) &&
           yyjson_mut_obj_add_strcpy(doc, artifact, "id", "lighting.static.default") &&
           yyjson_mut_obj_add_strcpy(doc, artifact, "type", "static_light_contribution") &&
           yyjson_mut_obj_add_strcpy(doc, artifact, "format", "slayer3d.lighting_static.v0") &&
           yyjson_mut_obj_add_strcpy(doc, artifact, "storage", "embedded_json") &&
           yyjson_mut_obj_add_strcpy(doc, artifact, "status", "planned") &&
           yyjson_mut_obj_add_strcpy(doc, artifact, "bake_group", "default") &&
           yyjson_mut_obj_add_uint(doc, artifact, "light_count", plan->bake_light_count) &&
           yyjson_mut_obj_add_uint(doc, artifact, "area_light_count", plan->area_light_count) &&
           yyjson_mut_obj_add_uint(doc, artifact, "brush_count", slayer3d_map_get_brush_count(document)) &&
           yyjson_mut_obj_add_bool(doc, artifact, "self_contained", true);
}

bool slayer3d_map_build_lighting_artifact_manifest_json(const slayer3d_map_document *document,
                                                        const slayer3d_map_lighting_build_options *options,
                                                        char **out_json, size_t *out_json_size, char *error_buffer,
                                                        int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    if (out_json == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: output lighting artifact manifest JSON pointer is required");
        return false;
    }
    *out_json = NULL;
    if (out_json_size != NULL)
        *out_json_size = 0u;

    slayer3d_map_lighting_build_plan plan;
    if (!slayer3d_map_build_lighting_plan(document, options, &plan, error_buffer, error_buffer_size))
        return false;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *map = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    const char *metadata_id = slayer3d_map_get_metadata_id(document);
    const char *metadata_name = slayer3d_map_get_metadata_name(document);
    const char *source_path = slayer3d_map_get_source_path(document);
    bool ok = doc != NULL && root != NULL && map != NULL;
    if (ok)
    {
        yyjson_mut_doc_set_root(doc, root);
        ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.lighting_artifact_manifest.v0") &&
             yyjson_mut_obj_add_strcpy(doc, root, "quality", map_lighting_quality_name(plan.quality)) &&
             yyjson_mut_obj_add_bool(doc, root, "requires_static_bake", plan.requires_static_bake) &&
             yyjson_mut_obj_add_bool(doc, root, "dynamic_preview", plan.has_dynamic_preview) &&
             yyjson_mut_obj_add_val(doc, root, "map", map) &&
             yyjson_mut_obj_add_strcpy(doc, map, "id", metadata_id != NULL ? metadata_id : "") &&
             yyjson_mut_obj_add_strcpy(doc, map, "name", metadata_name != NULL ? metadata_name : "") &&
             yyjson_mut_obj_add_strcpy(doc, map, "source_path", source_path != NULL ? source_path : "") &&
             map_lighting_manifest_add_counts(doc, root, &plan) &&
             map_lighting_manifest_add_budgets(doc, root, &plan) &&
             map_lighting_manifest_add_artifacts(doc, root, document, &plan);
    }

    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (!ok || json == NULL)
    {
        free(json);
        map_set_error(error_buffer, error_buffer_size, "$: failed to build lighting artifact manifest JSON");
        return false;
    }

    *out_json = json;
    if (out_json_size != NULL)
        *out_json_size = size;
    return true;
}

static slayer3d_vec3 map_lighting_vec3_sub(slayer3d_vec3 a, slayer3d_vec3 b)
{
    return (slayer3d_vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static slayer3d_vec3 map_lighting_vec3_negate(slayer3d_vec3 value)
{
    return (slayer3d_vec3){-value.x, -value.y, -value.z};
}

static float map_lighting_vec3_dot(slayer3d_vec3 a, slayer3d_vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float map_lighting_vec3_length(slayer3d_vec3 value)
{
    return SDL_sqrtf(map_lighting_vec3_dot(value, value));
}

static slayer3d_vec3 map_lighting_vec3_normalize_or(slayer3d_vec3 value, slayer3d_vec3 fallback)
{
    const float length = map_lighting_vec3_length(value);
    if (length <= 0.00001f)
        return fallback;
    const float inv_length = 1.0f / length;
    return (slayer3d_vec3){value.x * inv_length, value.y * inv_length, value.z * inv_length};
}

static double map_lighting_clamp01(double value)
{
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

static bool map_lighting_add_vec3(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, slayer3d_vec3 value)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    return array != NULL && yyjson_mut_arr_add_real(doc, array, value.x) &&
           yyjson_mut_arr_add_real(doc, array, value.y) && yyjson_mut_arr_add_real(doc, array, value.z) &&
           yyjson_mut_obj_add_val(doc, object, key, array);
}

static bool map_lighting_add_rgb(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, double r, double g,
                                 double b)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    return array != NULL && yyjson_mut_arr_add_real(doc, array, map_lighting_clamp01(r)) &&
           yyjson_mut_arr_add_real(doc, array, map_lighting_clamp01(g)) &&
           yyjson_mut_arr_add_real(doc, array, map_lighting_clamp01(b)) &&
           yyjson_mut_obj_add_val(doc, object, key, array);
}

static slayer3d_vec3 map_lighting_box_center(const slayer3d_map_box_geometry *box)
{
    return (slayer3d_vec3){(box->min.x + box->max.x) * 0.5f, (box->min.y + box->max.y) * 0.5f,
                           (box->min.z + box->max.z) * 0.5f};
}

static slayer3d_vec3 map_lighting_box_face_position(const slayer3d_map_box_geometry *box, int face_index)
{
    slayer3d_vec3 position = map_lighting_box_center(box);
    switch (face_index)
    {
    case 0:
        position.x = box->max.x;
        break;
    case 1:
        position.x = box->min.x;
        break;
    case 2:
        position.y = box->max.y;
        break;
    case 3:
        position.y = box->min.y;
        break;
    case 4:
        position.z = box->max.z;
        break;
    case 5:
        position.z = box->min.z;
        break;
    default:
        break;
    }
    return position;
}

static float map_lighting_spot_factor(const slayer3d_map_light *light, slayer3d_vec3 light_to_sample)
{
    if (light == NULL || light->type == NULL || SDL_strcmp(light->type, "spot") != 0)
        return 1.0f;

    const slayer3d_vec3 direction =
        map_lighting_vec3_normalize_or(light->has_direction ? light->direction : (slayer3d_vec3){0.0f, -1.0f, 0.0f},
                                       (slayer3d_vec3){0.0f, -1.0f, 0.0f});
    const float outer_degrees = light->has_outer_angle_degrees ? light->outer_angle_degrees : 35.0f;
    const float inner_degrees = light->has_inner_angle_degrees ? light->inner_angle_degrees : outer_degrees * 0.5f;
    const float outer_cos = SDL_cosf(SDL_clamp(outer_degrees, 0.0f, 179.0f) * 0.01745329251994329577f);
    const float inner_cos = SDL_cosf(SDL_clamp(inner_degrees, 0.0f, 179.0f) * 0.01745329251994329577f);
    const float cone = map_lighting_vec3_dot(direction, map_lighting_vec3_normalize_or(light_to_sample, direction));
    if (cone <= outer_cos)
        return 0.0f;
    if (cone >= inner_cos || SDL_fabsf(inner_cos - outer_cos) <= 0.00001f)
        return 1.0f;
    return SDL_clamp((cone - outer_cos) / (inner_cos - outer_cos), 0.0f, 1.0f);
}

static void map_lighting_accumulate_light(const slayer3d_map_light *light, slayer3d_vec3 sample_position,
                                          slayer3d_vec3 sample_normal, double *out_r, double *out_g, double *out_b)
{
    if (light == NULL || out_r == NULL || out_g == NULL || out_b == NULL)
        return;
    if (!(map_light_kind_bakes(light->kind) || map_light_type_is_area(light->type)))
        return;

    const slayer3d_color color = light->has_color ? light->color : (slayer3d_color){255, 255, 255, 255};
    const float intensity = light->has_intensity ? SDL_max(light->intensity, 0.0f) : 1.0f;
    float contribution = 0.0f;

    if (light->type != NULL && SDL_strcmp(light->type, "directional") == 0)
    {
        const slayer3d_vec3 direction =
            map_lighting_vec3_normalize_or(light->has_direction ? light->direction : (slayer3d_vec3){0.0f, -1.0f, 0.0f},
                                           (slayer3d_vec3){0.0f, -1.0f, 0.0f});
        contribution = SDL_max(0.0f, map_lighting_vec3_dot(sample_normal, map_lighting_vec3_negate(direction)));
    }
    else
    {
        const slayer3d_vec3 light_position =
            light->transform.has_position ? light->transform.position : (slayer3d_vec3){0.0f, 4.0f, 0.0f};
        const slayer3d_vec3 sample_to_light = map_lighting_vec3_sub(light_position, sample_position);
        const float distance = map_lighting_vec3_length(sample_to_light);
        const slayer3d_vec3 to_light =
            map_lighting_vec3_normalize_or(sample_to_light, (slayer3d_vec3){0.0f, 1.0f, 0.0f});
        const float lambert = SDL_max(0.0f, map_lighting_vec3_dot(sample_normal, to_light));
        const float range = light->has_range ? SDL_max(light->range, 0.001f) : 8.0f;
        const float normalized_distance = distance / range;
        const float attenuation = 1.0f / (1.0f + normalized_distance * normalized_distance);
        const float spot = map_lighting_spot_factor(light, map_lighting_vec3_sub(sample_position, light_position));
        contribution = lambert * attenuation * spot;
    }

    const double scaled = (double)(contribution * intensity);
    *out_r += ((double)color.r / 255.0) * scaled;
    *out_g += ((double)color.g / 255.0) * scaled;
    *out_b += ((double)color.b / 255.0) * scaled;
}

static bool map_lighting_add_static_sample(yyjson_mut_doc *doc, yyjson_mut_val *samples,
                                           const slayer3d_map_document *document, const slayer3d_map_brush *brush,
                                           int face_index, const char *face_name, slayer3d_vec3 normal,
                                           const slayer3d_map_global_state *global)
{
    yyjson_mut_val *sample = yyjson_mut_obj(doc);
    if (sample == NULL || !yyjson_mut_arr_add_val(samples, sample))
        return false;

    const slayer3d_vec3 position = map_lighting_box_face_position(&brush->box, face_index);
    const slayer3d_color ambient = global != NULL ? global->ambient_light : (slayer3d_color){54, 56, 64, 255};
    double r = (double)ambient.r / 255.0;
    double g = (double)ambient.g / 255.0;
    double b = (double)ambient.b / 255.0;

    const size_t light_count = slayer3d_map_get_light_count(document);
    for (size_t i = 0; i < light_count; ++i)
    {
        slayer3d_map_light light;
        if (slayer3d_map_get_light(document, i, &light))
            map_lighting_accumulate_light(&light, position, normal, &r, &g, &b);
    }

    const double peak = SDL_max(SDL_max(r, g), b);
    return yyjson_mut_obj_add_strcpy(doc, sample, "brush", brush->id != NULL ? brush->id : "") &&
           yyjson_mut_obj_add_strcpy(doc, sample, "face", face_name) &&
           map_lighting_add_vec3(doc, sample, "position", position) &&
           map_lighting_add_vec3(doc, sample, "normal", normal) &&
           map_lighting_add_rgb(doc, sample, "color", r, g, b) &&
           yyjson_mut_obj_add_real(doc, sample, "intensity", peak);
}

static bool map_lighting_add_static_samples(yyjson_mut_doc *doc, yyjson_mut_val *samples,
                                            const slayer3d_map_document *document,
                                            const slayer3d_map_global_state *global, size_t *out_sample_count)
{
    static const struct
    {
        const char *name;
        slayer3d_vec3 normal;
    } faces[] = {
        {"positive_x", {1.0f, 0.0f, 0.0f}},  {"negative_x", {-1.0f, 0.0f, 0.0f}}, {"positive_y", {0.0f, 1.0f, 0.0f}},
        {"negative_y", {0.0f, -1.0f, 0.0f}}, {"positive_z", {0.0f, 0.0f, 1.0f}},  {"negative_z", {0.0f, 0.0f, -1.0f}},
    };

    size_t sample_count = 0u;
    const size_t brush_count = slayer3d_map_get_brush_count(document);
    for (size_t brush_index = 0; brush_index < brush_count; ++brush_index)
    {
        slayer3d_map_brush brush;
        if (!slayer3d_map_get_brush(document, brush_index, &brush) || !brush.box.valid)
            continue;
        for (size_t face_index = 0; face_index < SDL_arraysize(faces); ++face_index)
        {
            if (!map_lighting_add_static_sample(doc, samples, document, &brush, (int)face_index, faces[face_index].name,
                                                faces[face_index].normal, global))
            {
                return false;
            }
            sample_count += 1u;
        }
    }
    if (out_sample_count != NULL)
        *out_sample_count = sample_count;
    return true;
}

bool slayer3d_map_build_static_lighting_artifact_json(const slayer3d_map_document *document,
                                                      const slayer3d_map_lighting_build_options *options,
                                                      char **out_json, size_t *out_json_size, char *error_buffer,
                                                      int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    if (out_json == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: output static lighting artifact JSON pointer is required");
        return false;
    }
    *out_json = NULL;
    if (out_json_size != NULL)
        *out_json_size = 0u;

    slayer3d_map_lighting_build_plan plan;
    if (!slayer3d_map_build_lighting_plan(document, options, &plan, error_buffer, error_buffer_size))
        return false;

    slayer3d_map_global_state global;
    if (!slayer3d_map_get_global_state(document, &global))
        SDL_zero(global);

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *map = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *counts = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *samples = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    const char *metadata_id = slayer3d_map_get_metadata_id(document);
    const char *metadata_name = slayer3d_map_get_metadata_name(document);
    const char *source_path = slayer3d_map_get_source_path(document);
    size_t sample_count = 0u;

    bool ok = doc != NULL && root != NULL && map != NULL && counts != NULL && samples != NULL;
    if (ok)
    {
        yyjson_mut_doc_set_root(doc, root);
        ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.lighting_static.v0") &&
             yyjson_mut_obj_add_strcpy(doc, root, "quality", map_lighting_quality_name(plan.quality)) &&
             yyjson_mut_obj_add_strcpy(doc, root, "bake_group", "default") &&
             yyjson_mut_obj_add_bool(doc, root, "self_contained", true) &&
             yyjson_mut_obj_add_strcpy(doc, root, "sample_model", "box_face_irradiance_preview") &&
             yyjson_mut_obj_add_val(doc, root, "map", map) &&
             yyjson_mut_obj_add_strcpy(doc, map, "id", metadata_id != NULL ? metadata_id : "") &&
             yyjson_mut_obj_add_strcpy(doc, map, "name", metadata_name != NULL ? metadata_name : "") &&
             yyjson_mut_obj_add_strcpy(doc, map, "source_path", source_path != NULL ? source_path : "") &&
             yyjson_mut_obj_add_val(doc, root, "counts", counts) &&
             yyjson_mut_obj_add_val(doc, root, "samples", samples) &&
             map_lighting_add_static_samples(doc, samples, document, &global, &sample_count) &&
             yyjson_mut_obj_add_uint(doc, counts, "bake_lights", plan.bake_light_count) &&
             yyjson_mut_obj_add_uint(doc, counts, "brushes", slayer3d_map_get_brush_count(document)) &&
             yyjson_mut_obj_add_uint(doc, counts, "box_brushes", sample_count / 6u) &&
             yyjson_mut_obj_add_uint(doc, counts, "samples", sample_count);
    }

    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (!ok || json == NULL)
    {
        free(json);
        map_set_error(error_buffer, error_buffer_size, "$: failed to build static lighting artifact JSON");
        return false;
    }

    *out_json = json;
    if (out_json_size != NULL)
        *out_json_size = size;
    return true;
}

static bool map_actor_is_player_character(yyjson_val *actor)
{
    yyjson_val *properties = map_properties_object(actor);
    static const char *const keys[] = {"type", "actor-type", "actor_type"};
    for (size_t i = 0; i < SDL_arraysize(keys); ++i)
    {
        yyjson_val *type = map_obj_get(properties, keys[i]);
        if (yyjson_is_str(type) && SDL_strcmp(yyjson_get_str(type), "player-character") == 0)
            return true;
    }
    return false;
}

static bool map_brush_is_playable(const slayer3d_map_brush *brush)
{
    if (brush == NULL || brush->geometry_kind == NULL)
        return false;
    return brush->box.valid || SDL_strcmp(brush->geometry_kind, "planes") == 0;
}

bool slayer3d_map_build_playable_scene_desc(const slayer3d_map_document *document,
                                            slayer3d_map_playable_scene_desc *out_desc, char *error_buffer,
                                            int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    if (out_desc == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: output playable scene descriptor is required");
        return false;
    }
    SDL_zero(*out_desc);
    if (document == NULL || document->doc == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "$: map document is required");
        return false;
    }

    out_desc->texture_asset_count = slayer3d_map_get_asset_count(document, SLAYER3D_MAP_ASSET_TEXTURE);
    out_desc->model_asset_count = slayer3d_map_get_asset_count(document, SLAYER3D_MAP_ASSET_MODEL);
    out_desc->material_count = slayer3d_map_get_material_count(document);
    out_desc->light_count = slayer3d_map_get_light_count(document);
    out_desc->actor_count = slayer3d_map_get_actor_count(document);
    out_desc->player_actor_index = (size_t)-1;

    const size_t brush_count = slayer3d_map_get_brush_count(document);
    for (size_t i = 0; i < brush_count; ++i)
    {
        slayer3d_map_brush brush;
        if (!slayer3d_map_get_brush(document, i, &brush))
            continue;
        if (map_brush_is_playable(&brush))
            out_desc->playable_brush_count += 1U;
        if (brush.box.valid)
            out_desc->box_brush_count += 1U;
    }

    for (size_t i = 0; i < out_desc->actor_count; ++i)
    {
        yyjson_val *actor_value = map_root_array_item(document, "actors", i);
        if (!map_actor_is_player_character(actor_value))
            continue;

        slayer3d_map_actor actor;
        if (!slayer3d_map_get_actor(document, i, &actor))
            continue;
        out_desc->has_player_character = true;
        out_desc->player_actor_index = i;
        out_desc->player_actor_id = actor.id;
        out_desc->player_position = actor.transform.has_position ? actor.transform.position : (slayer3d_vec3){0};
        return true;
    }

    map_set_error(error_buffer, error_buffer_size,
                  "$.actors: playable map requires an actor/object with property type, actor-type, or actor_type = "
                  "player-character");
    return false;
}

static bool map_make_directory_recursive(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;

    SDL_PathInfo info;
    SDL_zero(info);
    if (SDL_GetPathInfo(path, &info))
        return info.type == SDL_PATHTYPE_DIRECTORY;

    char *copy = SDL_strdup(path);
    if (copy == NULL)
        return false;

    bool ok = true;
    for (char *p = copy + 1; *p != '\0'; ++p)
    {
        if (*p != '/' && *p != '\\')
            continue;
        const char saved = *p;
        *p = '\0';
        const size_t prefix_len = SDL_strlen(copy);
        bool creatable_prefix = prefix_len > 0u;
        if (creatable_prefix && prefix_len == 1u && (copy[0] == '/' || copy[0] == '\\'))
            creatable_prefix = false;
        if (creatable_prefix && prefix_len == 2u && copy[1] == ':')
            creatable_prefix = false;
        if (creatable_prefix && prefix_len >= 2u && (copy[0] == '/' || copy[0] == '\\') &&
            (copy[1] == '/' || copy[1] == '\\'))
        {
            int components = 0;
            bool in_component = false;
            for (const char *component = copy + 2; *component != '\0'; ++component)
            {
                if (*component == '/' || *component == '\\')
                {
                    if (in_component)
                        ++components;
                    in_component = false;
                }
                else
                {
                    in_component = true;
                }
            }
            if (in_component)
                ++components;
            creatable_prefix = components >= 3;
        }
        if (creatable_prefix)
        {
            SDL_zero(info);
            if (!SDL_GetPathInfo(copy, &info))
                ok = SDL_CreateDirectory(copy);
            else
                ok = info.type == SDL_PATHTYPE_DIRECTORY;
        }
        *p = saved;
        if (!ok)
            break;
    }

    if (ok)
    {
        SDL_zero(info);
        if (!SDL_GetPathInfo(copy, &info))
            ok = SDL_CreateDirectory(copy);
        else
            ok = info.type == SDL_PATHTYPE_DIRECTORY;
    }

    SDL_free(copy);
    return ok;
}

static char *map_join_path(const char *directory, const char *leaf)
{
    if (directory == NULL || leaf == NULL)
        return NULL;
    const size_t dir_len = SDL_strlen(directory);
    const bool needs_sep = dir_len > 0u && directory[dir_len - 1u] != '/' && directory[dir_len - 1u] != '\\' &&
                           leaf[0] != '/' && leaf[0] != '\\';
    const size_t leaf_len = SDL_strlen(leaf);
    char *path = (char *)SDL_malloc(dir_len + leaf_len + (needs_sep ? 2u : 1u));
    if (path == NULL)
        return NULL;
    SDL_snprintf(path, dir_len + leaf_len + (needs_sep ? 2u : 1u), "%s%s%s", directory, needs_sep ? "/" : "", leaf);
    return path;
}

static bool map_write_text_file(const char *path, const char *text, size_t text_size, char *error_buffer,
                                int error_buffer_size)
{
    SDL_IOStream *stream = SDL_IOFromFile(path, "wb");
    if (stream == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "failed to open '%s' for writing", path);
        return false;
    }
    const bool ok = SDL_WriteIO(stream, text, text_size) == text_size;
    SDL_CloseIO(stream);
    if (!ok)
        map_set_error(error_buffer, error_buffer_size, "failed to write '%s'", path);
    return ok;
}

static bool map_game_add_vec3(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, slayer3d_vec3 value)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    return array != NULL && yyjson_mut_arr_add_real(doc, array, value.x) &&
           yyjson_mut_arr_add_real(doc, array, value.y) && yyjson_mut_arr_add_real(doc, array, value.z) &&
           yyjson_mut_obj_add_val(doc, object, key, array);
}

static bool map_game_add_color(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, slayer3d_color color)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    return array != NULL && yyjson_mut_arr_add_real(doc, array, (double)color.r / 255.0) &&
           yyjson_mut_arr_add_real(doc, array, (double)color.g / 255.0) &&
           yyjson_mut_arr_add_real(doc, array, (double)color.b / 255.0) &&
           yyjson_mut_arr_add_real(doc, array, (double)color.a / 255.0) &&
           yyjson_mut_obj_add_val(doc, object, key, array);
}

static bool map_game_add_rgb_color(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, slayer3d_color color)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    return array != NULL && yyjson_mut_arr_add_real(doc, array, (double)color.r / 255.0) &&
           yyjson_mut_arr_add_real(doc, array, (double)color.g / 255.0) &&
           yyjson_mut_arr_add_real(doc, array, (double)color.b / 255.0) &&
           yyjson_mut_obj_add_val(doc, object, key, array);
}

static bool map_game_add_vec2(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, float x, float y)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    return array != NULL && yyjson_mut_arr_add_real(doc, array, x) && yyjson_mut_arr_add_real(doc, array, y) &&
           yyjson_mut_obj_add_val(doc, object, key, array);
}

static float map_game_degrees_to_spot_cutoff(float degrees)
{
    const float clamped = SDL_clamp(degrees, 0.0f, 179.0f);
    return SDL_cosf(clamped * 0.01745329251994329577f);
}

static bool map_game_add_string_array_entry(yyjson_mut_doc *doc, yyjson_mut_val *array, const char *value)
{
    return yyjson_mut_arr_add_strcpy(doc, array, value != NULL ? value : "");
}

static bool map_game_add_keyboard_action(yyjson_mut_doc *doc, yyjson_mut_val *actions, const char *name,
                                         const char *key)
{
    yyjson_mut_val *action = yyjson_mut_obj(doc);
    yyjson_mut_val *bindings = yyjson_mut_arr(doc);
    yyjson_mut_val *binding = yyjson_mut_obj(doc);
    return action != NULL && bindings != NULL && binding != NULL && yyjson_mut_arr_add_val(actions, action) &&
           yyjson_mut_obj_add_strcpy(doc, action, "name", name) &&
           yyjson_mut_obj_add_val(doc, action, "bindings", bindings) && yyjson_mut_arr_add_val(bindings, binding) &&
           yyjson_mut_obj_add_strcpy(doc, binding, "device", "keyboard") &&
           yyjson_mut_obj_add_strcpy(doc, binding, "key", key);
}

static bool map_game_add_plane(yyjson_mut_doc *doc, yyjson_mut_val *faces, float nx, float ny, float nz, float distance,
                               const char *material)
{
    yyjson_mut_val *face = yyjson_mut_obj(doc);
    yyjson_mut_val *plane = yyjson_mut_obj(doc);
    if (face == NULL || plane == NULL || !yyjson_mut_arr_add_val(faces, face) ||
        !yyjson_mut_obj_add_val(doc, face, "plane", plane) ||
        !map_game_add_vec3(doc, plane, "normal", (slayer3d_vec3){nx, ny, nz}) ||
        !yyjson_mut_obj_add_real(doc, plane, "distance", distance))
    {
        return false;
    }
    return material == NULL || material[0] == '\0' || yyjson_mut_obj_add_strcpy(doc, face, "material", material);
}

static bool map_game_add_box_brush(yyjson_mut_doc *doc, yyjson_mut_val *brushes, const slayer3d_map_brush *brush,
                                   int fallback_index)
{
    const char *material = brush->material != NULL && brush->material[0] != '\0' ? brush->material : "mat.default";
    char fallback_name[64];
    SDL_snprintf(fallback_name, sizeof(fallback_name), "brush.map.%d", fallback_index);

    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *contents = yyjson_mut_arr(doc);
    yyjson_mut_val *faces = yyjson_mut_arr(doc);
    if (obj == NULL || contents == NULL || faces == NULL || !yyjson_mut_arr_add_val(brushes, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "name",
                                   brush->id != NULL && brush->id[0] != '\0' ? brush->id : fallback_name) ||
        !yyjson_mut_obj_add_val(doc, obj, "contents", contents) ||
        !map_game_add_string_array_entry(doc, contents, "solid") ||
        !map_game_add_string_array_entry(doc, contents, "player_clip") ||
        !yyjson_mut_obj_add_val(doc, obj, "faces", faces))
    {
        return false;
    }

    return map_game_add_plane(doc, faces, 1.0f, 0.0f, 0.0f, brush->box.max.x, material) &&
           map_game_add_plane(doc, faces, -1.0f, 0.0f, 0.0f, -brush->box.min.x, material) &&
           map_game_add_plane(doc, faces, 0.0f, 1.0f, 0.0f, brush->box.max.y, material) &&
           map_game_add_plane(doc, faces, 0.0f, -1.0f, 0.0f, -brush->box.min.y, material) &&
           map_game_add_plane(doc, faces, 0.0f, 0.0f, 1.0f, brush->box.max.z, material) &&
           map_game_add_plane(doc, faces, 0.0f, 0.0f, -1.0f, -brush->box.min.z, material);
}

static bool map_game_add_plane_brush_face(yyjson_mut_doc *doc, yyjson_mut_val *faces, yyjson_val *plane,
                                          const char *fallback_material)
{
    slayer3d_vec3 normal;
    yyjson_val *normal_value = map_obj_get(plane, "normal");
    yyjson_val *distance_value = map_obj_get(plane, "distance");
    if (!map_read_vec3_value(normal_value, &normal) || !yyjson_is_num(distance_value))
        return false;

    const char *material = map_json_string(plane, "material");
    if (material == NULL || material[0] == '\0')
        material = fallback_material;
    return map_game_add_plane(doc, faces, normal.x, normal.y, normal.z, (float)yyjson_get_num(distance_value),
                              material);
}

static bool map_game_add_planes_brush(yyjson_mut_doc *doc, yyjson_mut_val *brushes,
                                      const slayer3d_map_document *document, size_t brush_index, int fallback_index)
{
    yyjson_val *brush_value = map_root_array_item(document, "brushes", brush_index);
    yyjson_val *geometry = map_obj_get(brush_value, "geometry");
    yyjson_val *planes = map_obj_get(geometry, "planes");
    if (!yyjson_is_arr(planes) || yyjson_arr_size(planes) < 4u)
        return false;

    slayer3d_map_brush brush;
    if (!slayer3d_map_get_brush(document, brush_index, &brush))
        return false;

    const char *material = brush.material != NULL && brush.material[0] != '\0' ? brush.material : "mat.default";
    char fallback_name[64];
    SDL_snprintf(fallback_name, sizeof(fallback_name), "brush.map.%d", fallback_index);

    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *contents = yyjson_mut_arr(doc);
    yyjson_mut_val *faces = yyjson_mut_arr(doc);
    if (obj == NULL || contents == NULL || faces == NULL || !yyjson_mut_arr_add_val(brushes, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "name",
                                   brush.id != NULL && brush.id[0] != '\0' ? brush.id : fallback_name) ||
        !yyjson_mut_obj_add_val(doc, obj, "contents", contents) ||
        !map_game_add_string_array_entry(doc, contents, "solid") ||
        !map_game_add_string_array_entry(doc, contents, "player_clip") ||
        !yyjson_mut_obj_add_val(doc, obj, "faces", faces))
    {
        return false;
    }

    for (size_t i = 0, count = yyjson_arr_size(planes); i < count; ++i)
    {
        if (!map_game_add_plane_brush_face(doc, faces, yyjson_arr_get(planes, i), material))
            return false;
    }
    return true;
}

static bool map_material_name_matches(const char *lhs, const char *rhs)
{
    return lhs != NULL && rhs != NULL && lhs[0] != '\0' && rhs[0] != '\0' && SDL_strcmp(lhs, rhs) == 0;
}

static bool map_brush_references_material(yyjson_val *brush, const char *material_id)
{
    if (!yyjson_is_obj(brush) || material_id == NULL || material_id[0] == '\0')
        return false;
    if (map_material_name_matches(map_json_string(brush, "material"), material_id))
        return true;

    yyjson_val *geometry = map_obj_get(brush, "geometry");
    yyjson_val *planes = map_obj_get(geometry, "planes");
    if (yyjson_is_arr(planes))
    {
        for (size_t i = 0, count = yyjson_arr_size(planes); i < count; ++i)
        {
            if (map_material_name_matches(map_json_string(yyjson_arr_get(planes, i), "material"), material_id))
                return true;
        }
    }

    yyjson_val *faces = map_obj_get(brush, "faces");
    if (yyjson_is_arr(faces))
    {
        for (size_t i = 0, count = yyjson_arr_size(faces); i < count; ++i)
        {
            if (map_material_name_matches(map_json_string(yyjson_arr_get(faces, i), "material"), material_id))
                return true;
        }
    }
    return false;
}

static bool map_material_is_used_by_playable_brushes(const slayer3d_map_document *document, const char *material_id)
{
    const size_t brush_count = slayer3d_map_get_brush_count(document);
    for (size_t i = 0; i < brush_count; ++i)
    {
        if (map_brush_references_material(map_root_array_item(document, "brushes", i), material_id))
            return true;
    }
    return false;
}

static bool map_game_add_default_material(yyjson_mut_doc *doc, yyjson_mut_val *materials)
{
    yyjson_mut_val *material = yyjson_mut_obj(doc);
    return material != NULL && yyjson_mut_arr_add_val(materials, material) &&
           yyjson_mut_obj_add_strcpy(doc, material, "name", "mat.default") &&
           map_game_add_color(doc, material, "albedo", (slayer3d_color){180, 184, 192, 255});
}

static bool map_game_add_materials(yyjson_mut_doc *doc, yyjson_mut_val *materials,
                                   const slayer3d_map_document *document)
{
    const size_t count = slayer3d_map_get_material_count(document);
    if (count == 0u)
        return map_game_add_default_material(doc, materials);

    size_t emitted = 0u;
    for (size_t i = 0; i < count; ++i)
    {
        slayer3d_map_material map_material;
        if (!slayer3d_map_get_material(document, i, &map_material))
            return false;
        if (!map_material_is_used_by_playable_brushes(document, map_material.id))
            continue;
        yyjson_mut_val *material = yyjson_mut_obj(doc);
        const slayer3d_color color = map_material.has_color ? map_material.color : (slayer3d_color){180, 184, 192, 255};
        if (material == NULL || !yyjson_mut_arr_add_val(materials, material) ||
            !yyjson_mut_obj_add_strcpy(doc, material, "name",
                                       map_material.id != NULL && map_material.id[0] != '\0' ? map_material.id
                                                                                             : "mat.default") ||
            !map_game_add_color(doc, material, "albedo", color))
        {
            return false;
        }
        if (map_material.texture != NULL && map_material.texture[0] != '\0' &&
            !yyjson_mut_obj_add_strcpy(doc, material, "texture", map_material.texture))
        {
            return false;
        }
        ++emitted;
    }
    return emitted > 0u || map_game_add_default_material(doc, materials);
}

static bool map_game_add_brush_world(yyjson_mut_doc *doc, yyjson_mut_val *root, const slayer3d_map_document *document)
{
    yyjson_mut_val *worlds = yyjson_mut_arr(doc);
    yyjson_mut_val *world = yyjson_mut_obj(doc);
    yyjson_mut_val *materials = yyjson_mut_arr(doc);
    yyjson_mut_val *brushes = yyjson_mut_arr(doc);
    if (worlds == NULL || world == NULL || materials == NULL || brushes == NULL ||
        !yyjson_mut_obj_add_val(doc, root, "brush_worlds", worlds) || !yyjson_mut_arr_add_val(worlds, world) ||
        !yyjson_mut_obj_add_strcpy(doc, world, "name", "brush.slayermap") ||
        !yyjson_mut_obj_add_strcpy(doc, world, "units", "meters") ||
        !yyjson_mut_obj_add_real(doc, world, "meters_per_unit", 1.0) ||
        !yyjson_mut_obj_add_real(doc, world, "visibility_cell_size", 2.0) ||
        !yyjson_mut_obj_add_val(doc, world, "materials", materials) ||
        !map_game_add_materials(doc, materials, document) || !yyjson_mut_obj_add_val(doc, world, "brushes", brushes))
    {
        return false;
    }

    int emitted = 0;
    const size_t count = slayer3d_map_get_brush_count(document);
    for (size_t i = 0; i < count; ++i)
    {
        slayer3d_map_brush brush;
        if (!slayer3d_map_get_brush(document, i, &brush))
            continue;
        if (brush.box.valid)
        {
            if (!map_game_add_box_brush(doc, brushes, &brush, emitted))
                return false;
            ++emitted;
        }
        else if (brush.geometry_kind != NULL && SDL_strcmp(brush.geometry_kind, "planes") == 0)
        {
            if (!map_game_add_planes_brush(doc, brushes, document, i, emitted))
                return false;
            ++emitted;
        }
    }
    return emitted > 0;
}

static const char *map_game_runtime_light_type(const char *type)
{
    if (type != NULL && SDL_strcmp(type, "directional") == 0)
        return "directional";
    if (type != NULL && SDL_strcmp(type, "spot") == 0)
        return "spot";
    return "point";
}

static bool map_game_light_animation_active(const slayer3d_map_light_animation *animation)
{
    if (animation == NULL || animation->type == NULL || SDL_strcmp(animation->type, "none") == 0)
        return false;
    return animation->enabled || animation->type[0] != '\0';
}

static bool map_game_light_animation_uses_pulse(const slayer3d_map_light_animation *animation)
{
    return map_game_light_animation_active(animation) &&
           (SDL_strcmp(animation->type, "pulse") == 0 || SDL_strcmp(animation->type, "flicker") == 0);
}

static bool map_game_light_animation_rotates_direction(const slayer3d_map_light_animation *animation)
{
    return map_game_light_animation_active(animation) &&
           (SDL_strcmp(animation->type, "rotate") == 0 || SDL_strcmp(animation->type, "sweep") == 0);
}

static bool map_game_light_animation_orbits_position(const slayer3d_map_light_animation *animation)
{
    return map_game_light_animation_active(animation) && SDL_strcmp(animation->type, "orbit") == 0;
}

static float map_game_light_base_intensity(const slayer3d_map_light *map_light, float fallback)
{
    if (map_light != NULL && map_game_light_animation_uses_pulse(&map_light->animation) &&
        map_light->animation.has_min_intensity)
    {
        return map_light->animation.min_intensity;
    }
    return fallback;
}

static float map_game_light_pulse_intensity_add(const slayer3d_map_light *map_light, float base_intensity)
{
    if (map_light == NULL || !map_game_light_animation_uses_pulse(&map_light->animation))
        return 0.0f;
    if (map_light->animation.has_min_intensity && map_light->animation.has_max_intensity)
        return SDL_max(0.0f, map_light->animation.max_intensity - map_light->animation.min_intensity);
    const float amplitude = map_light->animation.has_amplitude ? map_light->animation.amplitude : 0.25f;
    return SDL_max(0.0f, base_intensity * amplitude);
}

static yyjson_mut_val *map_game_ensure_light_effects(yyjson_mut_doc *doc, yyjson_mut_val *light,
                                                     yyjson_mut_val **effects)
{
    if (effects == NULL)
        return NULL;
    if (*effects != NULL)
        return *effects;
    *effects = yyjson_mut_arr(doc);
    if (*effects == NULL || !yyjson_mut_obj_add_val(doc, light, "effects", *effects))
        return NULL;
    return *effects;
}

static bool map_game_add_light_animation(yyjson_mut_doc *doc, yyjson_mut_val *light,
                                         const slayer3d_map_light *map_light, float base_intensity)
{
    if (map_light == NULL || !map_game_light_animation_active(&map_light->animation))
        return true;

    const float rate_hz = map_light->animation.has_rate_hz ? map_light->animation.rate_hz : 1.0f;
    const float phase = map_light->animation.has_phase ? map_light->animation.phase : 0.0f;
    const float rate = rate_hz * 6.28318530717958647692f;
    yyjson_mut_val *effects = NULL;

    if (map_game_light_animation_uses_pulse(&map_light->animation))
    {
        const float intensity_add = map_game_light_pulse_intensity_add(map_light, base_intensity);
        if (intensity_add > 0.0f)
        {
            yyjson_mut_val *effect = yyjson_mut_obj(doc);
            yyjson_mut_val *effect_list = map_game_ensure_light_effects(doc, light, &effects);
            if (effect == NULL || effect_list == NULL || !yyjson_mut_arr_add_val(effects, effect) ||
                !yyjson_mut_obj_add_strcpy(doc, effect, "type", "pulse") ||
                !yyjson_mut_obj_add_real(doc, effect, "rate", rate) ||
                !yyjson_mut_obj_add_real(doc, effect, "phase", phase) ||
                !yyjson_mut_obj_add_real(doc, effect, "intensity_add", intensity_add))
            {
                return false;
            }
        }
    }

    if (map_game_light_animation_rotates_direction(&map_light->animation))
    {
        yyjson_mut_val *effect = yyjson_mut_obj(doc);
        yyjson_mut_val *effect_list = map_game_ensure_light_effects(doc, light, &effects);
        const slayer3d_vec3 axis =
            map_light->animation.has_axis ? map_light->animation.axis : (slayer3d_vec3){0.0f, 1.0f, 0.0f};
        if (effect == NULL || effect_list == NULL || !yyjson_mut_arr_add_val(effects, effect) ||
            !yyjson_mut_obj_add_strcpy(doc, effect, "type", "rotate_direction") ||
            !yyjson_mut_obj_add_real(doc, effect, "rate", rate) ||
            !yyjson_mut_obj_add_real(doc, effect, "phase", phase) || !map_game_add_vec3(doc, effect, "axis", axis))
        {
            return false;
        }
    }

    if (map_game_light_animation_orbits_position(&map_light->animation))
    {
        yyjson_mut_val *effect = yyjson_mut_obj(doc);
        yyjson_mut_val *effect_list = map_game_ensure_light_effects(doc, light, &effects);
        const slayer3d_vec3 axis =
            map_light->animation.has_axis ? map_light->animation.axis : (slayer3d_vec3){0.0f, 1.0f, 0.0f};
        const float radius = map_light->animation.has_radius ? map_light->animation.radius : 1.0f;
        if (effect == NULL || effect_list == NULL || !yyjson_mut_arr_add_val(effects, effect) ||
            !yyjson_mut_obj_add_strcpy(doc, effect, "type", "orbit_position") ||
            !yyjson_mut_obj_add_real(doc, effect, "rate", rate) ||
            !yyjson_mut_obj_add_real(doc, effect, "phase", phase) || !map_game_add_vec3(doc, effect, "axis", axis) ||
            !yyjson_mut_obj_add_real(doc, effect, "radius", radius))
        {
            return false;
        }
    }

    return true;
}

static bool map_game_add_light(yyjson_mut_doc *doc, yyjson_mut_val *lights, const slayer3d_map_light *map_light)
{
    yyjson_mut_val *light = yyjson_mut_obj(doc);
    const slayer3d_vec3 position =
        map_light != NULL ? map_light->transform.position : (slayer3d_vec3){0.0f, 0.0f, 0.0f};
    const slayer3d_vec3 direction =
        map_light != NULL && map_light->has_direction ? map_light->direction : (slayer3d_vec3){0.0f, -1.0f, 0.0f};
    const slayer3d_color color =
        map_light != NULL && map_light->has_color ? map_light->color : (slayer3d_color){255, 255, 255, 255};
    const float authored_intensity = map_light != NULL && map_light->has_intensity ? map_light->intensity : 1.0f;
    const float intensity = map_game_light_base_intensity(map_light, authored_intensity);
    const float range = map_light != NULL && map_light->has_range ? map_light->range : 10.0f;
    if (light == NULL || !yyjson_mut_arr_add_val(lights, light) ||
        !yyjson_mut_obj_add_strcpy(doc, light, "type",
                                   map_game_runtime_light_type(map_light != NULL ? map_light->type : NULL)) ||
        !map_game_add_vec3(doc, light, "position", position) ||
        !map_game_add_vec3(doc, light, "direction", direction) || !map_game_add_rgb_color(doc, light, "color", color) ||
        !yyjson_mut_obj_add_real(doc, light, "intensity", intensity) ||
        !yyjson_mut_obj_add_real(doc, light, "range", range))
    {
        return false;
    }
    if (map_light != NULL && map_light->has_inner_angle_degrees &&
        !yyjson_mut_obj_add_real(doc, light, "inner_cutoff",
                                 map_game_degrees_to_spot_cutoff(map_light->inner_angle_degrees)))
    {
        return false;
    }
    if (map_light != NULL && map_light->has_outer_angle_degrees &&
        !yyjson_mut_obj_add_real(doc, light, "outer_cutoff",
                                 map_game_degrees_to_spot_cutoff(map_light->outer_angle_degrees)))
    {
        return false;
    }
    return map_game_add_light_animation(doc, light, map_light, intensity);
}

static bool map_game_add_lights(yyjson_mut_doc *doc, yyjson_mut_val *world, const slayer3d_map_document *document)
{
    yyjson_mut_val *lights = yyjson_mut_arr(doc);
    if (lights == NULL || !yyjson_mut_obj_add_val(doc, world, "lights", lights))
        return false;

    const size_t count = slayer3d_map_get_light_count(document);
    for (size_t i = 0; i < count; ++i)
    {
        slayer3d_map_light map_light;
        if (!slayer3d_map_get_light(document, i, &map_light))
            return false;
        if (!map_game_add_light(doc, lights, &map_light))
            return false;
    }
    return true;
}

static bool map_game_add_input(yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    yyjson_mut_val *input = yyjson_mut_obj(doc);
    yyjson_mut_val *contexts = yyjson_mut_arr(doc);
    yyjson_mut_val *context = yyjson_mut_obj(doc);
    yyjson_mut_val *actions = yyjson_mut_arr(doc);
    if (input == NULL || contexts == NULL || context == NULL || actions == NULL ||
        !yyjson_mut_obj_add_val(doc, root, "input", input) ||
        !yyjson_mut_obj_add_val(doc, input, "contexts", contexts) || !yyjson_mut_arr_add_val(contexts, context) ||
        !yyjson_mut_obj_add_strcpy(doc, context, "name", "input.play") ||
        !yyjson_mut_obj_add_val(doc, context, "actions", actions))
    {
        return false;
    }
    return map_game_add_keyboard_action(doc, actions, "action.move.forward", "W") &&
           map_game_add_keyboard_action(doc, actions, "action.move.back", "S") &&
           map_game_add_keyboard_action(doc, actions, "action.move.left", "A") &&
           map_game_add_keyboard_action(doc, actions, "action.move.right", "D") &&
           map_game_add_keyboard_action(doc, actions, "action.jump", "SPACE") &&
           map_game_add_keyboard_action(doc, actions, "action.exit", "ESCAPE");
}

static bool map_game_add_world(yyjson_mut_doc *doc, yyjson_mut_val *root, const slayer3d_map_document *document,
                               const slayer3d_map_global_state *global)
{
    yyjson_mut_val *world = yyjson_mut_obj(doc);
    yyjson_mut_val *cameras = yyjson_mut_arr(doc);
    yyjson_mut_val *camera = yyjson_mut_obj(doc);
    if (world == NULL || cameras == NULL || camera == NULL || !yyjson_mut_obj_add_val(doc, root, "world", world) ||
        !yyjson_mut_obj_add_strcpy(doc, world, "name", "world.slayermap") ||
        !yyjson_mut_obj_add_strcpy(doc, world, "kind", "brush") ||
        !map_game_add_rgb_color(doc, world, "ambient_light",
                                global != NULL ? global->ambient_light : (slayer3d_color){54, 56, 64, 255}) ||
        !yyjson_mut_obj_add_val(doc, world, "cameras", cameras) || !yyjson_mut_arr_add_val(cameras, camera) ||
        !yyjson_mut_obj_add_strcpy(doc, camera, "name", "camera.player") ||
        !yyjson_mut_obj_add_strcpy(doc, camera, "type", "fps") ||
        !yyjson_mut_obj_add_strcpy(doc, camera, "target_entity", "entity.player") ||
        !yyjson_mut_obj_add_real(doc, camera, "fov", 90.0) || !yyjson_mut_obj_add_bool(doc, camera, "active", true))
    {
        return false;
    }
    return map_game_add_lights(doc, world, document);
}

static bool map_game_add_render(yyjson_mut_doc *doc, yyjson_mut_val *root, const slayer3d_map_global_state *global)
{
    yyjson_mut_val *render = yyjson_mut_obj(doc);
    const slayer3d_color clear_color = global != NULL ? global->clear_color : (slayer3d_color){12, 14, 18, 255};
    const char *tonemap = global != NULL && global->tonemap != NULL ? global->tonemap : "aces";
    return render != NULL && yyjson_mut_obj_add_val(doc, root, "render", render) &&
           yyjson_mut_obj_add_bool(doc, render, "lighting", true) &&
           map_game_add_rgb_color(doc, render, "clear_color", clear_color) &&
           yyjson_mut_obj_add_strcpy(doc, render, "tonemap", tonemap);
}

static bool map_game_add_player_entity(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                       const slayer3d_map_playable_scene_desc *scene)
{
    const float player_height = 1.6f;
    slayer3d_vec3 position = scene->player_position;
    position.y += player_height;

    yyjson_mut_val *entities = yyjson_mut_arr(doc);
    yyjson_mut_val *entity = yyjson_mut_obj(doc);
    yyjson_mut_val *transform = yyjson_mut_obj(doc);
    yyjson_mut_val *properties = yyjson_mut_obj(doc);
    yyjson_mut_val *yaw = yyjson_mut_obj(doc);
    yyjson_mut_val *pitch = yyjson_mut_obj(doc);
    yyjson_mut_val *components = yyjson_mut_arr(doc);
    yyjson_mut_val *controller = yyjson_mut_obj(doc);
    yyjson_mut_val *actions = yyjson_mut_obj(doc);
    yyjson_mut_val *mask = yyjson_mut_arr(doc);
    if (entities == NULL || entity == NULL || transform == NULL || properties == NULL || yaw == NULL || pitch == NULL ||
        components == NULL || controller == NULL || actions == NULL || mask == NULL ||
        !yyjson_mut_obj_add_val(doc, root, "entities", entities) || !yyjson_mut_arr_add_val(entities, entity) ||
        !yyjson_mut_obj_add_strcpy(doc, entity, "name", "entity.player") ||
        !yyjson_mut_obj_add_bool(doc, entity, "active", true) ||
        !yyjson_mut_obj_add_val(doc, entity, "transform", transform) ||
        !map_game_add_vec3(doc, transform, "position", position) ||
        !yyjson_mut_obj_add_val(doc, entity, "properties", properties) ||
        !yyjson_mut_obj_add_strcpy(doc, yaw, "type", "float") || !yyjson_mut_obj_add_real(doc, yaw, "value", 0.0) ||
        !yyjson_mut_obj_add_val(doc, properties, "yaw", yaw) ||
        !yyjson_mut_obj_add_strcpy(doc, pitch, "type", "float") || !yyjson_mut_obj_add_real(doc, pitch, "value", 0.0) ||
        !yyjson_mut_obj_add_val(doc, properties, "pitch", pitch) ||
        !yyjson_mut_obj_add_val(doc, entity, "components", components) ||
        !yyjson_mut_arr_add_val(components, controller) ||
        !yyjson_mut_obj_add_strcpy(doc, controller, "type", "controller.fps_brush") ||
        !yyjson_mut_obj_add_strcpy(doc, controller, "brush_world", "brush.slayermap") ||
        !yyjson_mut_obj_add_val(doc, controller, "contents_mask", mask) ||
        !map_game_add_string_array_entry(doc, mask, "solid") ||
        !map_game_add_string_array_entry(doc, mask, "player_clip") ||
        !yyjson_mut_obj_add_val(doc, controller, "actions", actions) ||
        !yyjson_mut_obj_add_strcpy(doc, actions, "forward", "action.move.forward") ||
        !yyjson_mut_obj_add_strcpy(doc, actions, "back", "action.move.back") ||
        !yyjson_mut_obj_add_strcpy(doc, actions, "left", "action.move.left") ||
        !yyjson_mut_obj_add_strcpy(doc, actions, "right", "action.move.right") ||
        !yyjson_mut_obj_add_strcpy(doc, actions, "jump", "action.jump") ||
        !yyjson_mut_obj_add_real(doc, controller, "move_speed", 6.0) ||
        !yyjson_mut_obj_add_real(doc, controller, "jump_velocity", 5.8) ||
        !yyjson_mut_obj_add_real(doc, controller, "gravity", 14.0) ||
        !yyjson_mut_obj_add_real(doc, controller, "player_height", player_height) ||
        !yyjson_mut_obj_add_real(doc, controller, "player_radius", 0.35) ||
        !yyjson_mut_obj_add_real(doc, controller, "step_height", 0.55) ||
        !yyjson_mut_obj_add_real(doc, controller, "ceiling_clearance", 0.1) ||
        !yyjson_mut_obj_add_real(doc, controller, "mouse_sensitivity", 0.002))
    {
        return false;
    }
    return true;
}

static bool map_game_add_scenes_section(yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    yyjson_mut_val *scenes = yyjson_mut_obj(doc);
    yyjson_mut_val *files = yyjson_mut_arr(doc);
    return scenes != NULL && files != NULL && yyjson_mut_obj_add_val(doc, root, "scenes", scenes) &&
           yyjson_mut_obj_add_strcpy(doc, scenes, "initial", "scene.play") &&
           yyjson_mut_obj_add_val(doc, scenes, "files", files) &&
           yyjson_mut_arr_add_strcpy(doc, files, "scenes/play.scene.json");
}

static bool map_scene_add_lighting_debug_line(yyjson_mut_doc *doc, yyjson_mut_val *texts, const char *name,
                                              const char *text, float y, slayer3d_color color, float scale)
{
    yyjson_mut_val *entry = yyjson_mut_obj(doc);
    return entry != NULL && yyjson_mut_arr_add_val(texts, entry) &&
           yyjson_mut_obj_add_strcpy(doc, entry, "name", name) && yyjson_mut_obj_add_strcpy(doc, entry, "text", text) &&
           map_game_add_vec2(doc, entry, "position", 0.02f, y) &&
           yyjson_mut_obj_add_strcpy(doc, entry, "anchor", "top_left") &&
           map_game_add_color(doc, entry, "color", color) && yyjson_mut_obj_add_real(doc, entry, "scale", scale);
}

static bool map_scene_add_lighting_debug_ui(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                            const slayer3d_map_lighting_build_plan *plan)
{
    yyjson_mut_val *ui = yyjson_mut_obj(doc);
    yyjson_mut_val *texts = yyjson_mut_arr(doc);
    if (ui == NULL || texts == NULL || !yyjson_mut_obj_add_val(doc, root, "ui", ui) ||
        !yyjson_mut_obj_add_val(doc, ui, "text", texts))
    {
        return false;
    }

    char counts[128];
    char classes[128];
    char status[160];
    SDL_snprintf(counts, sizeof(counts), "Lights total %zu runtime %zu bake %zu",
                 plan != NULL ? plan->total_light_count : 0u, plan != NULL ? plan->runtime_light_count : 0u,
                 plan != NULL ? plan->bake_light_count : 0u);
    SDL_snprintf(classes, sizeof(classes), "Dynamic %zu static %zu area %zu",
                 plan != NULL ? plan->dynamic_light_count : 0u, plan != NULL ? plan->static_light_count : 0u,
                 plan != NULL ? plan->area_light_count : 0u);
    SDL_snprintf(status, sizeof(status), "Lighting %s%s%s",
                 plan != NULL && plan->requires_static_bake ? "requires static bake" : "runtime only",
                 plan != NULL && plan->dynamic_light_budget_exceeded ? ", runtime budget exceeded" : "",
                 plan != NULL && plan->static_light_budget_exceeded ? ", static budget exceeded" : "");

    return map_scene_add_lighting_debug_line(doc, texts, "ui.slayermap.lighting.title", "SlayerMap Lighting", 0.03f,
                                             (slayer3d_color){220, 235, 255, 230}, 0.72f) &&
           map_scene_add_lighting_debug_line(doc, texts, "ui.slayermap.lighting.counts", counts, 0.07f,
                                             (slayer3d_color){190, 255, 210, 230}, 0.58f) &&
           map_scene_add_lighting_debug_line(doc, texts, "ui.slayermap.lighting.classes", classes, 0.10f,
                                             (slayer3d_color){210, 220, 255, 230}, 0.58f) &&
           map_scene_add_lighting_debug_line(doc, texts, "ui.slayermap.lighting.status", status, 0.13f,
                                             (slayer3d_color){255, 220, 150, 230}, 0.58f);
}

static bool map_game_add_app(yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    yyjson_mut_val *app = yyjson_mut_obj(doc);
    yyjson_mut_val *window = yyjson_mut_obj(doc);
    return app != NULL && window != NULL && yyjson_mut_obj_add_val(doc, root, "app", app) &&
           yyjson_mut_obj_add_strcpy(doc, app, "title", "SlayerMap Playable") &&
           yyjson_mut_obj_add_int(doc, app, "logical_width", 1280) &&
           yyjson_mut_obj_add_int(doc, app, "logical_height", 720) &&
           yyjson_mut_obj_add_strcpy(doc, app, "backend", "opengl") &&
           yyjson_mut_obj_add_val(doc, app, "window", window) &&
           yyjson_mut_obj_add_strcpy(doc, window, "renderer", "opengl") &&
           yyjson_mut_obj_add_bool(doc, window, "vsync", true);
}

static char *map_build_playable_game_json(const slayer3d_map_document *document,
                                          const slayer3d_map_playable_scene_desc *scene, size_t *out_size,
                                          char *error_buffer, int error_buffer_size)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *metadata = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    if (doc == NULL || root == NULL || metadata == NULL)
    {
        yyjson_mut_doc_free(doc);
        map_set_error(error_buffer, error_buffer_size, "failed to allocate playable game JSON");
        return NULL;
    }

    yyjson_mut_doc_set_root(doc, root);
    slayer3d_map_global_state global;
    SDL_zero(global);
    if (!slayer3d_map_get_global_state(document, &global))
    {
        map_set_error(error_buffer, error_buffer_size, "failed to read playable map global state");
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    bool ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.game.v0") &&
              yyjson_mut_obj_add_val(doc, root, "metadata", metadata) &&
              yyjson_mut_obj_add_strcpy(doc, metadata, "name", "SlayerMap Playable") && map_game_add_app(doc, root) &&
              map_game_add_world(doc, root, document, &global) && map_game_add_render(doc, root, &global) &&
              map_game_add_input(doc, root) && map_game_add_player_entity(doc, root, scene) &&
              map_game_add_brush_world(doc, root, document) && map_game_add_scenes_section(doc, root);
    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "failed to write playable game JSON");
        return NULL;
    }
    if (out_size != NULL)
        *out_size = size;
    return json;
}

static char *map_build_playable_scene_json(const slayer3d_map_lighting_build_plan *lighting_plan, size_t *out_size,
                                           char *error_buffer, int error_buffer_size)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *entities = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *input = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *actions = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *world = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *worlds = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *brush_world = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    if (doc == NULL || root == NULL || entities == NULL || input == NULL || actions == NULL || world == NULL ||
        worlds == NULL || brush_world == NULL)
    {
        yyjson_mut_doc_free(doc);
        map_set_error(error_buffer, error_buffer_size, "failed to allocate playable scene JSON");
        return NULL;
    }
    yyjson_mut_doc_set_root(doc, root);
    bool ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.scene.v0") &&
              yyjson_mut_obj_add_strcpy(doc, root, "name", "scene.play") &&
              yyjson_mut_obj_add_strcpy(doc, root, "camera", "camera.player") &&
              yyjson_mut_obj_add_bool(doc, root, "updates_game", true) &&
              yyjson_mut_obj_add_bool(doc, root, "renders_world", true) &&
              yyjson_mut_obj_add_val(doc, root, "entities", entities) &&
              yyjson_mut_arr_add_strcpy(doc, entities, "entity.player") &&
              yyjson_mut_obj_add_val(doc, root, "input", input) &&
              yyjson_mut_obj_add_strcpy(doc, input, "mouse_capture", "unpaused") &&
              yyjson_mut_obj_add_val(doc, input, "actions", actions) &&
              map_game_add_string_array_entry(doc, actions, "action.move.forward") &&
              map_game_add_string_array_entry(doc, actions, "action.move.back") &&
              map_game_add_string_array_entry(doc, actions, "action.move.left") &&
              map_game_add_string_array_entry(doc, actions, "action.move.right") &&
              map_game_add_string_array_entry(doc, actions, "action.jump") &&
              map_game_add_string_array_entry(doc, actions, "action.exit") &&
              yyjson_mut_obj_add_val(doc, root, "world", world) &&
              yyjson_mut_obj_add_val(doc, world, "brush_worlds", worlds) &&
              yyjson_mut_arr_add_val(worlds, brush_world) &&
              yyjson_mut_obj_add_strcpy(doc, brush_world, "world", "brush.slayermap") &&
              map_game_add_vec3(doc, brush_world, "position", (slayer3d_vec3){0.0f, 0.0f, 0.0f}) &&
              yyjson_mut_obj_add_bool(doc, brush_world, "acceleration", true) &&
              yyjson_mut_obj_add_bool(doc, brush_world, "lighting", true) &&
              map_scene_add_lighting_debug_ui(doc, root, lighting_plan);
    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        map_set_error(error_buffer, error_buffer_size, "failed to write playable scene JSON");
        return NULL;
    }
    if (out_size != NULL)
        *out_size = size;
    return json;
}

bool slayer3d_map_write_playable_game_files(const slayer3d_map_document *document, const char *output_dir,
                                            char *error_buffer, int error_buffer_size)
{
    map_clear_error(error_buffer, error_buffer_size);
    if (document == NULL || output_dir == NULL || output_dir[0] == '\0')
    {
        map_set_error(error_buffer, error_buffer_size,
                      "playable game export requires a map document and output directory");
        return false;
    }

    slayer3d_map_playable_scene_desc scene;
    if (!slayer3d_map_build_playable_scene_desc(document, &scene, error_buffer, error_buffer_size))
        return false;
    if (scene.playable_brush_count == 0u)
    {
        map_set_error(error_buffer, error_buffer_size,
                      "$.brushes: playable game export requires at least one box or plane brush");
        return false;
    }
    slayer3d_map_lighting_build_plan lighting_plan;
    if (!slayer3d_map_build_lighting_plan(document, NULL, &lighting_plan, error_buffer, error_buffer_size))
        return false;

    char *scenes_dir = map_join_path(output_dir, "scenes");
    char *game_path = map_join_path(output_dir, "playable_map.game.json");
    char *scene_path = map_join_path(output_dir, "scenes/play.scene.json");
    if (scenes_dir == NULL || game_path == NULL || scene_path == NULL)
    {
        SDL_free(scenes_dir);
        SDL_free(game_path);
        SDL_free(scene_path);
        map_set_error(error_buffer, error_buffer_size, "failed to allocate playable game output paths");
        return false;
    }
    if (!map_make_directory_recursive(output_dir) || !map_make_directory_recursive(scenes_dir))
    {
        SDL_free(scenes_dir);
        SDL_free(game_path);
        SDL_free(scene_path);
        map_set_error(error_buffer, error_buffer_size, "failed to create playable game output directory '%s'",
                      output_dir);
        return false;
    }

    size_t game_size = 0u;
    size_t scene_size = 0u;
    char *game_json = map_build_playable_game_json(document, &scene, &game_size, error_buffer, error_buffer_size);
    char *scene_json = game_json != NULL
                           ? map_build_playable_scene_json(&lighting_plan, &scene_size, error_buffer, error_buffer_size)
                           : NULL;
    bool ok = game_json != NULL && scene_json != NULL &&
              map_write_text_file(game_path, game_json, game_size, error_buffer, error_buffer_size) &&
              map_write_text_file(scene_path, scene_json, scene_size, error_buffer, error_buffer_size);

    free(game_json);
    free(scene_json);
    SDL_free(scenes_dir);
    SDL_free(game_path);
    SDL_free(scene_path);
    return ok;
}

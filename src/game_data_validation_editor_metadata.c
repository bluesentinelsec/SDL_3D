/**
 * @file game_data_validation_editor_metadata.c
 * @brief Validation for editor metadata and editor player starts.
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

static bool editor_string_field_valid(validation_context *ctx, yyjson_val *metadata, const char *key,
                                      const char *json_path)
{
    yyjson_val *value = obj_get(metadata, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0')
        return validation_error(ctx, json_path, "editor %s must be a non-empty string", key);
    return true;
}

static bool editor_tags_valid(validation_context *ctx, yyjson_val *tags, const char *json_path)
{
    if (tags == NULL)
        return true;
    if (!yyjson_is_arr(tags))
        return validation_error(ctx, json_path, "editor tags must be an array");
    for (size_t i = 0; i < yyjson_arr_size(tags); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *tag = yyjson_arr_get(tags, i);
        if (!yyjson_is_str(tag) || yyjson_get_str(tag)[0] == '\0')
            return validation_error(ctx, path, "editor tag entries must be non-empty strings");
    }
    return true;
}

static bool editor_vec_positive(yyjson_val *value)
{
    if (!is_vec_array(value, 3))
        return false;
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        if (yyjson_get_num(yyjson_arr_get(value, i)) <= 0.0)
            return false;
    }
    return true;
}

static bool editor_preview_primitive_valid(const char *primitive)
{
    static const char *const valid[] = {"none",  "cube",  "sphere", "capsule", "sprite",
                                        "model", "light", "volume", "sector"};
    for (size_t i = 0; primitive != NULL && i < SDL_arraysize(valid); ++i)
    {
        if (SDL_strcmp(primitive, valid[i]) == 0)
            return true;
    }
    return false;
}

static bool editor_exposed_property_type_valid(const char *type)
{
    static const char *const valid[] = {"bool", "int", "float", "string", "vec2", "vec3", "color", "enum"};
    for (size_t i = 0; type != NULL && i < SDL_arraysize(valid); ++i)
    {
        if (SDL_strcmp(type, valid[i]) == 0)
            return true;
    }
    return false;
}

static bool editor_exposed_property_default_valid(yyjson_val *value, const char *type)
{
    if (value == NULL)
        return true;
    if (type == NULL)
    {
        return yyjson_is_str(value) || yyjson_is_int(value) || yyjson_is_real(value) || yyjson_is_bool(value) ||
               is_exact_vec_array(value, 2) || is_exact_vec_array(value, 3) || is_exact_vec_array(value, 4);
    }
    if (SDL_strcmp(type, "bool") == 0)
        return yyjson_is_bool(value);
    if (SDL_strcmp(type, "int") == 0)
        return yyjson_is_int(value);
    if (SDL_strcmp(type, "float") == 0)
        return yyjson_is_num(value);
    if (SDL_strcmp(type, "string") == 0 || SDL_strcmp(type, "enum") == 0)
        return yyjson_is_str(value);
    if (SDL_strcmp(type, "vec2") == 0)
        return is_exact_vec_array(value, 2);
    if (SDL_strcmp(type, "vec3") == 0)
        return is_exact_vec_array(value, 3);
    if (SDL_strcmp(type, "color") == 0)
        return is_exact_vec_array(value, 3) || is_exact_vec_array(value, 4);
    return false;
}

static bool validate_editor_preview(validation_context *ctx, yyjson_val *preview, const char *json_path)
{
    if (preview == NULL)
        return true;
    if (!yyjson_is_obj(preview))
        return validation_error(ctx, json_path, "editor preview must be an object");

    const char *primitive = json_string(preview, "primitive");
    if (primitive != NULL && !editor_preview_primitive_valid(primitive))
        return validation_error(ctx, json_path, "editor preview primitive is unknown");
    yyjson_val *asset = obj_get(preview, "asset");
    if (asset != NULL && (!yyjson_is_str(asset) || yyjson_get_str(asset)[0] == '\0'))
        return validation_error(ctx, json_path, "editor preview asset must be a non-empty string");
    yyjson_val *color = obj_get(preview, "color");
    if (color != NULL && !is_vec_array(color, 3))
        return validation_error(ctx, json_path, "editor preview color must be a vec3 or vec4");
    return true;
}

static bool validate_editor_bounds(validation_context *ctx, yyjson_val *bounds, const char *json_path)
{
    if (bounds == NULL)
        return true;
    if (!yyjson_is_obj(bounds))
        return validation_error(ctx, json_path, "editor bounds must be an object");
    yyjson_val *center = obj_get(bounds, "center");
    if (center != NULL && !is_vec_array(center, 3))
        return validation_error(ctx, json_path, "editor bounds center must be a vec3");
    yyjson_val *size = obj_get(bounds, "size");
    if (size != NULL && !editor_vec_positive(size))
        return validation_error(ctx, json_path, "editor bounds size must be a positive vec3");
    yyjson_val *half_extents = obj_get(bounds, "half_extents");
    if (half_extents != NULL && !editor_vec_positive(half_extents))
        return validation_error(ctx, json_path, "editor bounds half_extents must be a positive vec3");
    yyjson_val *radius = obj_get(bounds, "radius");
    if (radius != NULL && (!yyjson_is_num(radius) || yyjson_get_num(radius) <= 0.0))
        return validation_error(ctx, json_path, "editor bounds radius must be positive");
    return true;
}

static bool validate_editor_snap(validation_context *ctx, yyjson_val *snap, const char *json_path)
{
    if (snap == NULL)
        return true;
    if (!yyjson_is_obj(snap))
        return validation_error(ctx, json_path, "editor snap must be an object");
    yyjson_val *grid = obj_get(snap, "grid");
    if (grid != NULL)
    {
        if (yyjson_is_num(grid))
        {
            if (yyjson_get_num(grid) <= 0.0)
                return validation_error(ctx, json_path, "editor snap grid must be positive");
        }
        else if (!editor_vec_positive(grid))
        {
            return validation_error(ctx, json_path, "editor snap grid must be a positive number or vec3");
        }
    }
    yyjson_val *rotation = obj_get(snap, "rotation_degrees");
    if (rotation != NULL && (!yyjson_is_num(rotation) || yyjson_get_num(rotation) <= 0.0))
        return validation_error(ctx, json_path, "editor snap rotation_degrees must be positive");
    yyjson_val *align = obj_get(snap, "align_to_floor");
    if (align != NULL && !yyjson_is_bool(align))
        return validation_error(ctx, json_path, "editor snap align_to_floor must be a boolean");
    return true;
}

static bool validate_editor_exposed_properties(validation_context *ctx, yyjson_val *properties, const char *json_path)
{
    if (properties == NULL)
        return true;
    if (!yyjson_is_arr(properties))
        return validation_error(ctx, json_path, "editor exposed_properties must be an array");
    name_table names;
    SDL_zero(names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(properties); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s[%zu]", json_path, i);
        yyjson_val *property = yyjson_arr_get(properties, i);
        if (!yyjson_is_obj(property))
        {
            ok = validation_error(ctx, path, "editor exposed property entries must be objects");
            break;
        }
        ok = require_unique_name(ctx, &names, "editor exposed property", json_string(property, "name"), path) &&
             editor_string_field_valid(ctx, property, "display_name", path) &&
             editor_string_field_valid(ctx, property, "description", path);
        const char *type = json_string(property, "type");
        if (ok && type != NULL && !editor_exposed_property_type_valid(type))
            ok = validation_error(ctx, path, "editor exposed property type is unknown");
        yyjson_val *min_value = obj_get(property, "min");
        yyjson_val *max_value = obj_get(property, "max");
        if (ok &&
            ((min_value != NULL && !yyjson_is_num(min_value)) || (max_value != NULL && !yyjson_is_num(max_value))))
            ok = validation_error(ctx, path, "editor exposed property min and max must be numbers");
        yyjson_val *default_value = obj_get(property, "default");
        if (ok && !editor_exposed_property_default_valid(default_value, type))
            ok = validation_error(ctx, path, "editor exposed property default does not match its type");
    }
    name_table_destroy(&names);
    return ok;
}

bool validate_editor_metadata(validation_context *ctx, yyjson_val *metadata, const char *json_path,
                              validation_names *names, bool allow_templates)
{
    if (metadata == NULL)
        return true;
    if (!yyjson_is_obj(metadata))
        return validation_error(ctx, json_path, "editor metadata must be an object");

    if (!editor_string_field_valid(ctx, metadata, "display_name", json_path) ||
        !editor_string_field_valid(ctx, metadata, "stable_id", json_path) ||
        !editor_string_field_valid(ctx, metadata, "description", json_path) ||
        !editor_string_field_valid(ctx, metadata, "category", json_path) ||
        !editor_string_field_valid(ctx, metadata, "group", json_path) ||
        !editor_string_field_valid(ctx, metadata, "prefab", json_path) ||
        !editor_string_field_valid(ctx, metadata, "archetype", json_path) ||
        !editor_string_field_valid(ctx, metadata, "icon", json_path) ||
        !editor_string_field_valid(ctx, metadata, "preview_asset", json_path) ||
        !editor_tags_valid(ctx, obj_get(metadata, "tags"), json_path) ||
        !validate_editor_preview(ctx, obj_get(metadata, "preview"), json_path) ||
        !validate_editor_bounds(ctx, obj_get(metadata, "bounds"), json_path) ||
        !validate_editor_snap(ctx, obj_get(metadata, "snap"), json_path) ||
        !validate_editor_exposed_properties(ctx, obj_get(metadata, "exposed_properties"), json_path))
    {
        return false;
    }

    const char *test_scene = json_string(metadata, "test_scene");
    if (test_scene != NULL && !require_ref(ctx, &names->scenes, "scene", test_scene, json_path))
        return false;

    yyjson_val *templates = obj_get(metadata, "templates");
    if (templates == NULL)
        return true;
    if (!allow_templates)
        return validation_error(ctx, json_path, "editor templates are only allowed on root editor metadata");
    if (!yyjson_is_arr(templates))
        return validation_error(ctx, json_path, "editor templates must be an array");

    name_table template_names;
    SDL_zero(template_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(templates); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.templates[%zu]", json_path, i);
        yyjson_val *entry = yyjson_arr_get(templates, i);
        if (!yyjson_is_obj(entry))
        {
            ok = validation_error(ctx, path, "editor template entries must be objects");
            break;
        }
        ok = require_unique_name(ctx, &template_names, "editor template", json_string(entry, "name"), path) &&
             editor_string_field_valid(ctx, entry, "source", path) &&
             editor_string_field_valid(ctx, entry, "source_kind", path) &&
             validate_editor_metadata(ctx, entry, path, names, false);
    }
    name_table_destroy(&template_names);
    return ok;
}

bool validate_editor_metadata_tree(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    if (!validate_editor_metadata(ctx, obj_get(root, "editor"), "$.editor", names, true))
        return false;

    yyjson_val *entities = obj_get(root, "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.entities[%zu].editor", i);
        if (!validate_editor_metadata(ctx, obj_get(yyjson_arr_get(entities, i), "editor"), path, names, false))
            return false;
    }

    yyjson_val *archetypes = obj_get(root, "actor_archetypes");
    for (size_t i = 0; yyjson_is_arr(archetypes) && i < yyjson_arr_size(archetypes); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_archetypes[%zu].editor", i);
        if (!validate_editor_metadata(ctx, obj_get(yyjson_arr_get(archetypes, i), "editor"), path, names, false))
            return false;
    }

    yyjson_val *instances = obj_get(root, "actor_instances");
    for (size_t i = 0; yyjson_is_arr(instances) && i < yyjson_arr_size(instances); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_instances[%zu].editor", i);
        if (!validate_editor_metadata(ctx, obj_get(yyjson_arr_get(instances, i), "editor"), path, names, false))
            return false;
    }

    yyjson_val *pools = obj_get(root, "actor_pools");
    for (size_t i = 0; yyjson_is_arr(pools) && i < yyjson_arr_size(pools); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_pools[%zu].editor", i);
        if (!validate_editor_metadata(ctx, obj_get(yyjson_arr_get(pools, i), "editor"), path, names, false))
            return false;
    }
    return true;
}

bool validate_editor_player_starts(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *starts = obj_get(root, "editor_player_starts");
    if (starts == NULL)
        return true;
    if (!yyjson_is_arr(starts))
        return validation_error(ctx, "$.editor_player_starts", "editor_player_starts must be an array");

    name_table start_names;
    SDL_zero(start_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(starts); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.editor_player_starts[%zu]", i);
        yyjson_val *start = yyjson_arr_get(starts, i);
        if (!yyjson_is_obj(start))
        {
            ok = validation_error(ctx, path, "editor player start entries must be objects");
            break;
        }
        const char *scene = json_string(start, "scene");
        const char *target = json_string(start, "target");
        yyjson_val *yaw = obj_get(start, "yaw");
        yyjson_val *pitch = obj_get(start, "pitch");
        ok = require_unique_name(ctx, &start_names, "editor player start", json_string(start, "name"), path) &&
             (scene == NULL || require_ref(ctx, &names->scenes, "scene", scene, path)) &&
             (target == NULL || require_actor_ref(ctx, names, target, path)) &&
             is_exact_vec_array(obj_get(start, "position"), 3) && (yaw == NULL || yyjson_is_num(yaw)) &&
             (pitch == NULL || yyjson_is_num(pitch));
        if (ok)
            continue;
        if (!ctx->failed)
            ok = validation_error(ctx, path, "editor player start requires position vec3 and numeric yaw/pitch");
    }
    name_table_destroy(&start_names);
    return ok;
}

bool validate_editor_actors(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *actors = obj_get(root, "editor_actors");
    if (actors == NULL)
        return true;
    if (!yyjson_is_arr(actors))
        return validation_error(ctx, "$.editor_actors", "editor_actors must be an array");

    name_table actor_names;
    SDL_zero(actor_names);
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(actors); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.editor_actors[%zu]", i);
        yyjson_val *actor = yyjson_arr_get(actors, i);
        if (!yyjson_is_obj(actor))
        {
            ok = validation_error(ctx, path, "editor actor entries must be objects");
            break;
        }
        const char *scene = json_string(actor, "scene");
        const char *archetype = json_string(actor, "archetype");
        const char *model = json_string(actor, "model");
        yyjson_val *position = obj_get(actor, "position");
        yyjson_val *rotation = obj_get(actor, "rotation");
        yyjson_val *scale = obj_get(actor, "scale");
        yyjson_val *color = obj_get(actor, "color");
        yyjson_val *properties = obj_get(actor, "properties");
        ok = require_unique_name(ctx, &actor_names, "editor actor", json_string(actor, "name"), path) &&
             (scene == NULL || require_ref(ctx, &names->scenes, "scene", scene, path)) &&
             (archetype == NULL || require_ref(ctx, &names->actor_archetypes, "actor archetype", archetype, path)) &&
             (model == NULL || require_ref(ctx, &names->models, "model", model, path)) &&
             (position == NULL || is_exact_vec_array(position, 3)) &&
             (rotation == NULL || is_exact_vec_array(rotation, 3)) &&
             (scale == NULL ||
              (is_exact_vec_array(scale, 3) && yyjson_get_num(yyjson_arr_get(scale, 0)) > 0.0 &&
               yyjson_get_num(yyjson_arr_get(scale, 1)) > 0.0 && yyjson_get_num(yyjson_arr_get(scale, 2)) > 0.0)) &&
             (color == NULL || is_exact_vec3_or_vec4_array(color)) && (properties == NULL || yyjson_is_obj(properties));
        if (!ok && !ctx->failed)
            ok = validation_error(ctx, path,
                                  "editor actor requires valid name, transform, color, properties, and references");
    }
    name_table_destroy(&actor_names);
    return ok;
}

static bool is_exact_int_vec3_array(yyjson_val *value)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 3u)
        return false;
    for (size_t i = 0; i < 3u; ++i)
    {
        if (!yyjson_is_int(yyjson_arr_get(value, i)))
            return false;
    }
    return true;
}

static bool editor_brush_source_box_has_positive_extent(yyjson_val *min_value, yyjson_val *max_value)
{
    if (!is_exact_int_vec3_array(min_value) || !is_exact_int_vec3_array(max_value))
        return false;
    for (size_t i = 0; i < 3u; ++i)
    {
        if (yyjson_get_sint(yyjson_arr_get(min_value, i)) >= yyjson_get_sint(yyjson_arr_get(max_value, i)))
            return false;
    }
    return true;
}

static bool editor_brush_source_box_aligned_to_snap(yyjson_val *min_value, yyjson_val *max_value, int snap_units)
{
    if (snap_units <= 1)
        return true;
    if (!is_exact_int_vec3_array(min_value) || !is_exact_int_vec3_array(max_value))
        return false;
    for (size_t i = 0; i < 3u; ++i)
    {
        if ((yyjson_get_sint(yyjson_arr_get(min_value, i)) % snap_units) != 0 ||
            (yyjson_get_sint(yyjson_arr_get(max_value, i)) % snap_units) != 0)
        {
            return false;
        }
    }
    return true;
}

static bool editor_brush_source_vertices_valid(yyjson_val *vertices, int snap_units)
{
    if (!yyjson_is_arr(vertices))
        return false;
    const size_t count = yyjson_arr_size(vertices);
    if (count < 4u || count > 16u)
        return false;
    for (size_t vertex = 0; vertex < count; ++vertex)
    {
        yyjson_val *coord = yyjson_arr_get(vertices, vertex);
        if (!is_exact_int_vec3_array(coord))
            return false;
        if (snap_units > 1)
        {
            for (size_t axis = 0; axis < 3u; ++axis)
            {
                if ((yyjson_get_sint(yyjson_arr_get(coord, axis)) % snap_units) != 0)
                    return false;
            }
        }
    }
    return true;
}

static bool editor_brush_source_face_key_valid(const char *key)
{
    static const char *const valid[] = {"px", "nx", "py", "ny", "pz", "nz"};
    for (size_t i = 0; key != NULL && i < SDL_arraysize(valid); ++i)
    {
        if (SDL_strcmp(key, valid[i]) == 0)
            return true;
    }
    return false;
}

static yyjson_val *editor_brush_source_world_materials(yyjson_val *root, const char *world_name)
{
    yyjson_val *worlds = obj_get(root, "brush_worlds");
    if (!yyjson_is_arr(worlds))
        return NULL;
    for (size_t i = 0; i < yyjson_arr_size(worlds); ++i)
    {
        yyjson_val *world = yyjson_arr_get(worlds, i);
        const char *name = json_string(world, "name");
        if (name != NULL && world_name != NULL && SDL_strcmp(name, world_name) == 0)
            return obj_get(world, "materials");
    }
    return NULL;
}

static bool editor_brush_source_material_table(validation_context *ctx, yyjson_val *materials, const char *source_path,
                                               name_table *out_material_names)
{
    SDL_zero(*out_material_names);
    if (!yyjson_is_arr(materials))
        return validation_error(ctx, source_path, "editor brush source world materials must be available");
    for (size_t i = 0; i < yyjson_arr_size(materials); ++i)
    {
        char material_path[PATH_BUFFER_SIZE];
        format_path(material_path, sizeof(material_path), "%s.materials[%zu]", source_path, i);
        if (!require_unique_name(ctx, out_material_names, "editor brush source material",
                                 json_string(yyjson_arr_get(materials, i), "name"), material_path))
            return false;
    }
    return true;
}

static bool validate_editor_brush_source_face_materials(validation_context *ctx, yyjson_val *face_materials,
                                                        const char *box_path, const name_table *material_names)
{
    if (face_materials == NULL)
        return true;
    if (!yyjson_is_obj(face_materials))
        return validation_error(ctx, box_path, "editor brush source face_materials must be an object");

    size_t index = 0;
    size_t max = 0;
    yyjson_val *key = NULL;
    yyjson_val *value = NULL;
    yyjson_obj_foreach(face_materials, index, max, key, value)
    {
        const char *face_key = yyjson_get_str(key);
        char face_path[PATH_BUFFER_SIZE];
        format_path(face_path, sizeof(face_path), "%s.face_materials.%s", box_path, face_key != NULL ? face_key : "");
        if (!editor_brush_source_face_key_valid(face_key) || !yyjson_is_str(value) || yyjson_get_str(value) == NULL ||
            yyjson_get_str(value)[0] == '\0')
        {
            return validation_error(ctx, face_path,
                                    "editor brush source face material keys must be px/nx/py/ny/pz/nz with non-empty "
                                    "string material refs");
        }
        if (!name_table_contains(material_names, yyjson_get_str(value)))
        {
            return validation_error(ctx, face_path,
                                    "editor brush source face material must reference a declared brush material");
        }
    }
    return true;
}

bool validate_editor_brush_sources(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *sources = obj_get(root, "editor_brush_sources");
    if (sources == NULL)
        return true;
    if (!yyjson_is_arr(sources))
        return validation_error(ctx, "$.editor_brush_sources", "editor_brush_sources must be an array");

    name_table source_worlds;
    SDL_zero(source_worlds);
    bool ok = true;
    for (size_t source_index = 0; ok && source_index < yyjson_arr_size(sources); ++source_index)
    {
        char source_path[PATH_BUFFER_SIZE];
        format_path(source_path, sizeof(source_path), "$.editor_brush_sources[%zu]", source_index);
        yyjson_val *source = yyjson_arr_get(sources, source_index);
        if (!yyjson_is_obj(source))
        {
            ok = validation_error(ctx, source_path, "editor brush source entries must be objects");
            break;
        }

        yyjson_val *meters_per_unit = obj_get(source, "meters_per_unit");
        yyjson_val *snap_units = obj_get(source, "snap_units");
        yyjson_val *boxes = obj_get(source, "boxes");
        const int source_snap_units =
            snap_units != NULL && yyjson_is_int(snap_units) ? (int)yyjson_get_int(snap_units) : 1;
        const char *coordinate_system = json_string(source, "coordinate_system");
        const char *source_world = json_string(source, "world");
        name_table material_names;
        SDL_zero(material_names);
        ok = require_unique_name(ctx, &source_worlds, "editor brush source world", json_string(source, "world"),
                                 source_path) &&
             require_ref(ctx, &names->brush_worlds, "brush world", source_world, source_path) &&
             (coordinate_system == NULL || SDL_strcmp(coordinate_system, "fixed_millimeters") == 0) &&
             (meters_per_unit == NULL || (yyjson_is_num(meters_per_unit) && yyjson_get_real(meters_per_unit) > 0.0)) &&
             (snap_units == NULL || (yyjson_is_int(snap_units) && yyjson_get_int(snap_units) > 0)) &&
             yyjson_is_arr(boxes) &&
             editor_brush_source_material_table(ctx, editor_brush_source_world_materials(root, source_world),
                                                source_path, &material_names);
        if (!ok)
        {
            if (!ctx->failed)
                ok = validation_error(ctx, source_path,
                                      "editor brush source requires world ref, optional fixed_millimeters coordinate "
                                      "system, positive meters_per_unit, positive snap_units, and boxes array");
            name_table_destroy(&material_names);
            break;
        }

        name_table box_ids;
        name_table box_names;
        SDL_zero(box_ids);
        SDL_zero(box_names);
        for (size_t box_index = 0; ok && box_index < yyjson_arr_size(boxes); ++box_index)
        {
            char box_path[PATH_BUFFER_SIZE];
            format_path(box_path, sizeof(box_path), "%s.boxes[%zu]", source_path, box_index);
            yyjson_val *box = yyjson_arr_get(boxes, box_index);
            if (!yyjson_is_obj(box))
            {
                ok = validation_error(ctx, box_path, "editor brush source boxes must be objects");
                break;
            }
            const char *kind = json_string(box, "kind");
            const char *prefab = json_string(box, "prefab");
            const char *material = json_string(box, "material");
            const char *stable_id = json_string(box, "stable_id");
            const char *explicit_name = json_string(box, "name");
            const char *name = explicit_name != NULL ? explicit_name : stable_id;
            yyjson_val *contents = obj_get(box, "contents");
            yyjson_val *min_value = obj_get(box, "min");
            yyjson_val *max_value = obj_get(box, "max");
            yyjson_val *vertices = obj_get(box, "vertices");
            const bool is_convex = kind != NULL && SDL_strcmp(kind, "convex") == 0;
            ok = require_unique_name(ctx, &box_ids, "editor brush source stable id", stable_id, box_path) &&
                 require_unique_name(ctx, &box_names, "editor brush source name", name, box_path) &&
                 (kind == NULL || SDL_strcmp(kind, "box") == 0 || is_convex) && (prefab == NULL || prefab[0] != '\0') &&
                 material != NULL && material[0] != '\0' && name_table_contains(&material_names, material) &&
                 (is_convex ? editor_brush_source_vertices_valid(vertices, source_snap_units)
                            : (editor_brush_source_box_has_positive_extent(min_value, max_value) &&
                               editor_brush_source_box_aligned_to_snap(min_value, max_value, source_snap_units))) &&
                 validate_brush_string_or_string_array(ctx, contents, box_path, "editor brush source contents",
                                                       brush_content_name_valid, false) &&
                 validate_editor_brush_source_face_materials(ctx, obj_get(box, "face_materials"), box_path,
                                                             &material_names);
            if (!ok && !ctx->failed)
            {
                ok = validation_error(ctx, box_path,
                                      "editor brush source requires stable_id, kind 'box' or 'convex', material ref, "
                                      "integer min/max vec3 for boxes or convex vertices with positive snap-aligned "
                                      "extent, valid contents, and valid face_materials");
            }
        }
        name_table_destroy(&box_names);
        name_table_destroy(&box_ids);
        name_table_destroy(&material_names);
    }
    name_table_destroy(&source_worlds);
    return ok;
}

bool require_unique_editor_stable_id(validation_context *ctx, name_table *stable_ids, yyjson_val *json,
                                     const char *json_path)
{
    yyjson_val *editor = obj_get(json, "editor");
    const char *stable_id = json_string(editor, "stable_id");
    if (stable_id == NULL)
        return true;
    char path[PATH_BUFFER_SIZE];
    format_path(path, sizeof(path), "%s.editor", json_path);
    return require_unique_name(ctx, stable_ids, "editor stable id", stable_id, path);
}

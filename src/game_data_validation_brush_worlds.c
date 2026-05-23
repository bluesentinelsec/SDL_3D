/**
 * @file game_data_validation_brush_worlds.c
 * @brief Validation for JSON-authored brush worlds.
 */

#include "game_data_validation_internal.h"

#include <float.h>

#include <SDL3/SDL_stdinc.h>

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static bool brush_material_ref_valid(yyjson_val *materials, const name_table *material_names, yyjson_val *ref)
{
    if (yyjson_is_int(ref))
    {
        const int index = (int)yyjson_get_int(ref);
        return index >= 0 && index < (int)yyjson_arr_size(materials);
    }
    if (yyjson_is_str(ref))
        return name_table_contains(material_names, yyjson_get_str(ref));
    return false;
}

static bool vec3_nonzero(yyjson_val *value)
{
    if (!is_exact_vec_array(value, 3))
        return false;
    const double x = yyjson_get_num(yyjson_arr_get(value, 0));
    const double y = yyjson_get_num(yyjson_arr_get(value, 1));
    const double z = yyjson_get_num(yyjson_arr_get(value, 2));
    return x * x + y * y + z * z > 0.000001;
}

bool validate_brush_worlds(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *worlds = obj_get(root, "brush_worlds");
    if (worlds == NULL)
        return true;
    if (!yyjson_is_arr(worlds))
        return validation_error(ctx, "$.brush_worlds", "brush_worlds must be an array");

    for (size_t world_index = 0; world_index < yyjson_arr_size(worlds); ++world_index)
    {
        char world_path[PATH_BUFFER_SIZE];
        format_path(world_path, sizeof(world_path), "$.brush_worlds[%zu]", world_index);
        yyjson_val *world = yyjson_arr_get(worlds, world_index);
        yyjson_val *materials = obj_get(world, "materials");
        yyjson_val *brushes = obj_get(world, "brushes");
        name_table material_names;
        name_table brush_names;
        name_table editor_stable_ids;
        SDL_zero(material_names);
        SDL_zero(brush_names);
        SDL_zero(editor_stable_ids);
        bool ok = true;

        if (!yyjson_is_obj(world))
        {
            ok = validation_error(ctx, world_path, "brush world entries must be objects");
            goto done;
        }

        const char *units = json_string(world, "units");
        if (units != NULL && SDL_strcmp(units, "meters") != 0)
        {
            ok = validation_error(ctx, world_path, "brush world units must be meters");
            goto done;
        }
        yyjson_val *meters_per_unit = obj_get(world, "meters_per_unit");
        if (meters_per_unit != NULL && (!yyjson_is_num(meters_per_unit) || yyjson_get_num(meters_per_unit) <= 0.0))
        {
            ok = validation_error(ctx, world_path, "brush world meters_per_unit must be positive");
            goto done;
        }
        yyjson_val *visibility_cell_size = obj_get(world, "visibility_cell_size");
        if (visibility_cell_size != NULL &&
            (!yyjson_is_num(visibility_cell_size) || yyjson_get_num(visibility_cell_size) <= 0.0))
        {
            ok = validation_error(ctx, world_path, "brush world visibility_cell_size must be positive");
            goto done;
        }
        yyjson_val *compile = obj_get(world, "compile");
        if (compile != NULL)
        {
            char compile_path[PATH_BUFFER_SIZE];
            format_path(compile_path, sizeof(compile_path), "%s.compile", world_path);
            if (!yyjson_is_obj(compile))
            {
                ok = validation_error(ctx, compile_path, "brush world compile must be an object");
                goto done;
            }
            yyjson_val *hidden_face_culling = obj_get(compile, "hidden_face_culling");
            if (hidden_face_culling != NULL && !yyjson_is_bool(hidden_face_culling))
            {
                ok = validation_error(ctx, compile_path, "brush world compile hidden_face_culling must be a boolean");
                goto done;
            }
            yyjson_val *chunk_cell_size = obj_get(compile, "chunk_cell_size");
            if (chunk_cell_size != NULL && (!yyjson_is_num(chunk_cell_size) || yyjson_get_num(chunk_cell_size) <= 0.0))
            {
                ok = validation_error(ctx, compile_path, "brush world compile chunk_cell_size must be positive");
                goto done;
            }
        }
        {
            char editor_path[PATH_BUFFER_SIZE];
            format_path(editor_path, sizeof(editor_path), "%s.editor", world_path);
            if (!validate_editor_metadata(ctx, obj_get(world, "editor"), editor_path, names, false) ||
                !require_unique_editor_stable_id(ctx, &editor_stable_ids, world, world_path))
            {
                ok = false;
                goto done;
            }
        }
        if (!yyjson_is_arr(materials) || yyjson_arr_size(materials) <= 0)
        {
            ok = validation_error(ctx, world_path, "brush world materials must be a non-empty array");
            goto done;
        }
        if (!yyjson_is_arr(brushes))
        {
            ok = validation_error(ctx, world_path, "brush world brushes must be an array");
            goto done;
        }

        for (size_t material_index = 0; ok && material_index < yyjson_arr_size(materials); ++material_index)
        {
            char material_path[PATH_BUFFER_SIZE];
            format_path(material_path, sizeof(material_path), "%s.materials[%zu]", world_path, material_index);
            yyjson_val *material = yyjson_arr_get(materials, material_index);
            if (!yyjson_is_obj(material))
            {
                ok = validation_error(ctx, material_path, "brush material entries must be objects");
                break;
            }
            if (!require_unique_name(ctx, &material_names, "brush material", json_string(material, "name"),
                                     material_path))
            {
                ok = false;
                break;
            }
            {
                char editor_path[PATH_BUFFER_SIZE];
                format_path(editor_path, sizeof(editor_path), "%s.editor", material_path);
                if (!validate_editor_metadata(ctx, obj_get(material, "editor"), editor_path, names, false) ||
                    !require_unique_editor_stable_id(ctx, &editor_stable_ids, material, material_path))
                {
                    ok = false;
                    break;
                }
            }
            yyjson_val *albedo = obj_get(material, "albedo");
            if (albedo != NULL &&
                (!is_exact_vec3_or_vec4_array(albedo) || !numeric_array_values_in_range(albedo, 0.0, 1.0)))
            {
                ok = validation_error(ctx, material_path,
                                      "brush material albedo must be a vec3 or vec4 with values in [0, 1]");
                break;
            }
            yyjson_val *metallic = obj_get(material, "metallic");
            yyjson_val *roughness = obj_get(material, "roughness");
            yyjson_val *emissive = obj_get(material, "emissive");
            yyjson_val *tex_scale = obj_get(material, "tex_scale");
            if ((metallic != NULL && (!yyjson_is_num(metallic) || yyjson_get_num(metallic) < 0.0)) ||
                (roughness != NULL && (!yyjson_is_num(roughness) || yyjson_get_num(roughness) < 0.0)))
            {
                ok = validation_error(ctx, material_path, "brush material metallic and roughness must be non-negative");
                break;
            }
            if (emissive != NULL &&
                (!is_exact_vec_array(emissive, 3) || !numeric_array_values_in_range(emissive, 0.0, DBL_MAX)))
            {
                ok = validation_error(ctx, material_path, "brush material emissive must be a non-negative vec3");
                break;
            }
            if (tex_scale != NULL && (!yyjson_is_num(tex_scale) || yyjson_get_num(tex_scale) <= 0.0))
            {
                ok = validation_error(ctx, material_path, "brush material tex_scale must be positive");
                break;
            }
            const char *texture = json_string(material, "texture");
            if (texture != NULL && texture[0] == '\0')
            {
                ok = validation_error(ctx, material_path, "brush material texture must be non-empty when present");
                break;
            }
            if (texture != NULL && !asset_path_exists(ctx, texture, material_path, "brush material texture"))
            {
                ok = false;
                break;
            }
        }

        for (size_t brush_index = 0; ok && brush_index < yyjson_arr_size(brushes); ++brush_index)
        {
            char brush_path[PATH_BUFFER_SIZE];
            format_path(brush_path, sizeof(brush_path), "%s.brushes[%zu]", world_path, brush_index);
            yyjson_val *brush = yyjson_arr_get(brushes, brush_index);
            yyjson_val *faces = obj_get(brush, "faces");
            if (!yyjson_is_obj(brush))
            {
                ok = validation_error(ctx, brush_path, "brush entries must be objects");
                break;
            }
            if (!require_unique_name(ctx, &brush_names, "brush", json_string(brush, "name"), brush_path))
            {
                ok = false;
                break;
            }
            {
                char editor_path[PATH_BUFFER_SIZE];
                format_path(editor_path, sizeof(editor_path), "%s.editor", brush_path);
                if (!validate_editor_metadata(ctx, obj_get(brush, "editor"), editor_path, names, false) ||
                    !require_unique_editor_stable_id(ctx, &editor_stable_ids, brush, brush_path))
                {
                    ok = false;
                    break;
                }
            }

            yyjson_val *tags = obj_get(brush, "tags");
            if (tags != NULL)
            {
                if (!yyjson_is_arr(tags))
                {
                    ok = validation_error(ctx, brush_path, "brush tags must be an array");
                    break;
                }
                name_table tag_names;
                SDL_zero(tag_names);
                for (size_t tag_index = 0; ok && tag_index < yyjson_arr_size(tags); ++tag_index)
                {
                    char tag_path[PATH_BUFFER_SIZE];
                    format_path(tag_path, sizeof(tag_path), "%s.tags[%zu]", brush_path, tag_index);
                    yyjson_val *tag = yyjson_arr_get(tags, tag_index);
                    if (!yyjson_is_str(tag) || yyjson_get_str(tag) == NULL || yyjson_get_str(tag)[0] == '\0')
                    {
                        ok = validation_error(ctx, tag_path, "brush tags must be non-empty strings");
                        break;
                    }
                    if (!require_unique_name(ctx, &tag_names, "brush tag", yyjson_get_str(tag), tag_path))
                        ok = false;
                }
                name_table_destroy(&tag_names);
                if (!ok)
                    break;
            }

            char contents_path[PATH_BUFFER_SIZE];
            format_path(contents_path, sizeof(contents_path), "%s.contents", brush_path);
            if (!validate_brush_string_or_string_array(ctx, obj_get(brush, "contents"), contents_path, "brush content",
                                                       brush_content_name_valid, false))
            {
                ok = false;
                break;
            }
            yyjson_val *visibility_cullable = obj_get(brush, "visibility_cullable");
            if (visibility_cullable != NULL && !yyjson_is_bool(visibility_cullable))
            {
                ok = validation_error(ctx, brush_path, "brush visibility_cullable must be a boolean");
                break;
            }
            yyjson_val *visibility = obj_get(brush, "visibility");
            if (visibility != NULL &&
                (!yyjson_is_str(visibility) || (SDL_strcmp(yyjson_get_str(visibility), "auto") != 0 &&
                                                SDL_strcmp(yyjson_get_str(visibility), "always") != 0 &&
                                                SDL_strcmp(yyjson_get_str(visibility), "trace") != 0)))
            {
                ok = validation_error(ctx, brush_path, "brush visibility must be 'auto', 'always', or 'trace'");
                break;
            }
            if (!yyjson_is_arr(faces) || yyjson_arr_size(faces) < 4)
            {
                ok = validation_error(ctx, brush_path, "brush faces must contain at least 4 entries");
                break;
            }
            for (size_t face_index = 0; ok && face_index < yyjson_arr_size(faces); ++face_index)
            {
                char face_path[PATH_BUFFER_SIZE];
                char plane_path[PATH_BUFFER_SIZE];
                char flags_path[PATH_BUFFER_SIZE];
                format_path(face_path, sizeof(face_path), "%s.faces[%zu]", brush_path, face_index);
                format_path(plane_path, sizeof(plane_path), "%s.plane", face_path);
                format_path(flags_path, sizeof(flags_path), "%s.surface_flags", face_path);
                yyjson_val *face = yyjson_arr_get(faces, face_index);
                yyjson_val *plane = obj_get(face, "plane");
                yyjson_val *uv = obj_get(face, "uv");
                if (!yyjson_is_obj(face))
                {
                    ok = validation_error(ctx, face_path, "brush face entries must be objects");
                    break;
                }
                if (!yyjson_is_obj(plane) || !vec3_nonzero(obj_get(plane, "normal")) ||
                    !yyjson_is_num(obj_get(plane, "distance")))
                {
                    ok = validation_error(ctx, plane_path,
                                          "brush face plane requires non-zero normal vec3 and numeric distance");
                    break;
                }
                if (!brush_material_ref_valid(materials, &material_names, obj_get(face, "material")))
                {
                    ok = validation_error(ctx, face_path, "brush face material must reference a declared material");
                    break;
                }
                {
                    char editor_path[PATH_BUFFER_SIZE];
                    format_path(editor_path, sizeof(editor_path), "%s.editor", face_path);
                    if (!validate_editor_metadata(ctx, obj_get(face, "editor"), editor_path, names, false) ||
                        !require_unique_editor_stable_id(ctx, &editor_stable_ids, face, face_path))
                    {
                        ok = false;
                        break;
                    }
                }
                if (!validate_brush_string_or_string_array(ctx, obj_get(face, "surface_flags"), flags_path,
                                                           "brush surface flag", brush_surface_flag_name_valid, true))
                {
                    ok = false;
                    break;
                }
                if (uv != NULL)
                {
                    if (!yyjson_is_obj(uv))
                    {
                        ok = validation_error(ctx, face_path, "brush face uv must be an object");
                        break;
                    }
                    yyjson_val *scale = obj_get(uv, "scale");
                    yyjson_val *offset = obj_get(uv, "offset");
                    yyjson_val *rotation = obj_get(uv, "rotation_degrees");
                    if (scale != NULL && (!is_exact_vec_array(scale, 2) || !numeric_array_values_positive(scale)))
                    {
                        ok = validation_error(ctx, face_path, "brush face uv scale must be a positive vec2");
                        break;
                    }
                    if (offset != NULL && !is_exact_vec_array(offset, 2))
                    {
                        ok = validation_error(ctx, face_path, "brush face uv offset must be a vec2");
                        break;
                    }
                    if (rotation != NULL && !yyjson_is_num(rotation))
                    {
                        ok = validation_error(ctx, face_path, "brush face uv rotation_degrees must be a number");
                        break;
                    }
                }
            }
        }

    done:
        name_table_destroy(&material_names);
        name_table_destroy(&brush_names);
        name_table_destroy(&editor_stable_ids);
        if (!ok)
            return false;
    }
    return true;
}

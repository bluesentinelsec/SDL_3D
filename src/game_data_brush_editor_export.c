/**
 * @file game_data_brush_editor_export.c
 * @brief Brush-world and editable-level JSON export helpers for editor tooling.
 */

#include "game_data_brush_source_model_internal.h"
#include "game_data_internal.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <stdlib.h>

static bool export_add_color(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, slayer3d_color value);

static bool export_add_vec3(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, slayer3d_vec3 value)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_real(doc, arr, value.x) && yyjson_mut_arr_add_real(doc, arr, value.y) &&
           yyjson_mut_arr_add_real(doc, arr, value.z) && yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_vec4(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, slayer3d_vec4 value)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_real(doc, arr, value.x) && yyjson_mut_arr_add_real(doc, arr, value.y) &&
           yyjson_mut_arr_add_real(doc, arr, value.z) && yyjson_mut_arr_add_real(doc, arr, value.w) &&
           yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_vec2_values(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, float x, float y)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_real(doc, arr, x) && yyjson_mut_arr_add_real(doc, arr, y) &&
           yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_vec3i_values(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, int x, int y, int z)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_int(doc, arr, x) && yyjson_mut_arr_add_int(doc, arr, y) &&
           yyjson_mut_arr_add_int(doc, arr, z) && yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_source_vertices(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                       const editor_brush_source_box_runtime *box)
{
    if (box == NULL || box->vertex_count <= 0)
        return true;
    yyjson_mut_val *vertices = yyjson_mut_arr(doc);
    if (vertices == NULL)
        return false;
    for (int i = 0; i < box->vertex_count; ++i)
    {
        yyjson_mut_val *vertex = yyjson_mut_arr(doc);
        if (vertex == NULL || !yyjson_mut_arr_add_int(doc, vertex, box->vertices[i][0]) ||
            !yyjson_mut_arr_add_int(doc, vertex, box->vertices[i][1]) ||
            !yyjson_mut_arr_add_int(doc, vertex, box->vertices[i][2]) || !yyjson_mut_arr_add_val(vertices, vertex))
        {
            return false;
        }
    }
    return yyjson_mut_obj_add_val(doc, obj, "vertices", vertices);
}

static bool export_add_optional_string(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, const char *value)
{
    return value == NULL || value[0] == '\0' || yyjson_mut_obj_add_strcpy(doc, obj, key, value);
}

static bool export_add_property_value(yyjson_mut_doc *doc, yyjson_mut_val *properties, const char *key,
                                      const slayer3d_value *value)
{
    if (doc == NULL || properties == NULL || key == NULL || value == NULL)
        return false;
    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        return yyjson_mut_obj_add_int(doc, properties, key, value->as_int);
    case SLAYER3D_VALUE_FLOAT:
        return yyjson_mut_obj_add_real(doc, properties, key, value->as_float);
    case SLAYER3D_VALUE_BOOL:
        return yyjson_mut_obj_add_bool(doc, properties, key, value->as_bool);
    case SLAYER3D_VALUE_VEC3:
        return export_add_vec3(doc, properties, key, value->as_vec3);
    case SLAYER3D_VALUE_STRING:
        return yyjson_mut_obj_add_strcpy(doc, properties, key, value->as_string != NULL ? value->as_string : "");
    case SLAYER3D_VALUE_COLOR: {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        return arr != NULL && yyjson_mut_arr_add_uint(doc, arr, value->as_color.r) &&
               yyjson_mut_arr_add_uint(doc, arr, value->as_color.g) &&
               yyjson_mut_arr_add_uint(doc, arr, value->as_color.b) &&
               yyjson_mut_arr_add_uint(doc, arr, value->as_color.a) &&
               yyjson_mut_obj_add_val(doc, properties, key, arr);
    }
    }
    return false;
}

static bool export_add_properties(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key,
                                  const slayer3d_properties *properties)
{
    yyjson_mut_val *json = yyjson_mut_obj(doc);
    if (json == NULL || !yyjson_mut_obj_add_val(doc, obj, key, json))
        return false;
    const int count = properties != NULL ? slayer3d_properties_count(properties) : 0;
    for (int i = 0; i < count; ++i)
    {
        const char *property_key = NULL;
        slayer3d_value_type type = SLAYER3D_VALUE_STRING;
        if (!slayer3d_properties_get_key_at(properties, i, &property_key, &type) || property_key == NULL)
            continue;
        const slayer3d_value *value = slayer3d_properties_get_value(properties, property_key);
        if (!export_add_property_value(doc, json, property_key, value))
            return false;
    }
    return true;
}

static bool export_add_string_array(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key,
                                    const char *const *values, int count)
{
    if (count <= 0)
        return true;
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (arr == NULL)
        return false;
    for (int i = 0; i < count; ++i)
    {
        if (values[i] == NULL || !yyjson_mut_arr_add_strcpy(doc, arr, values[i]))
            return false;
    }
    return yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_editor_metadata(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                       const slayer3d_game_data_editor_metadata *metadata)
{
    if (metadata == NULL)
        return true;
    const bool has_snap =
        metadata->has_snap_grid || metadata->snap_rotation_degrees > 0.0f || metadata->snap_align_to_floor;
    if (metadata->stable_id == NULL && metadata->display_name == NULL && metadata->description == NULL &&
        metadata->category == NULL && metadata->group == NULL && metadata->prefab == NULL &&
        metadata->archetype == NULL && metadata->icon == NULL && metadata->preview_asset == NULL &&
        metadata->tag_count <= 0 && !has_snap)
    {
        return true;
    }

    yyjson_mut_val *editor = yyjson_mut_obj(doc);
    if (editor == NULL || !yyjson_mut_obj_add_val(doc, obj, "editor", editor))
        return false;
    if (!export_add_optional_string(doc, editor, "stable_id", metadata->stable_id) ||
        !export_add_optional_string(doc, editor, "display_name", metadata->display_name) ||
        !export_add_optional_string(doc, editor, "description", metadata->description) ||
        !export_add_optional_string(doc, editor, "category", metadata->category) ||
        !export_add_optional_string(doc, editor, "group", metadata->group) ||
        !export_add_optional_string(doc, editor, "prefab", metadata->prefab) ||
        !export_add_optional_string(doc, editor, "archetype", metadata->archetype) ||
        !export_add_optional_string(doc, editor, "icon", metadata->icon) ||
        !export_add_optional_string(doc, editor, "preview_asset", metadata->preview_asset) ||
        !export_add_string_array(doc, editor, "tags", metadata->tags, metadata->tag_count))
    {
        return false;
    }
    if (has_snap)
    {
        yyjson_mut_val *snap = yyjson_mut_obj(doc);
        if (snap == NULL || !yyjson_mut_obj_add_val(doc, editor, "snap", snap))
            return false;
        if (metadata->has_snap_grid && !export_add_vec3(doc, snap, "grid", metadata->snap_grid))
            return false;
        if (metadata->snap_rotation_degrees > 0.0f &&
            !yyjson_mut_obj_add_real(doc, snap, "rotation_degrees", metadata->snap_rotation_degrees))
        {
            return false;
        }
        if (metadata->snap_align_to_floor && !yyjson_mut_obj_add_bool(doc, snap, "align_to_floor", true))
            return false;
    }
    return true;
}

typedef struct brush_flag_export_entry
{
    unsigned int flag;
    const char *name;
} brush_flag_export_entry;

static bool export_add_flag_array(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, unsigned int flags,
                                  const brush_flag_export_entry *entries, size_t entry_count)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (arr == NULL)
        return false;
    for (size_t i = 0; i < entry_count; ++i)
    {
        if ((flags & entries[i].flag) != 0u && !yyjson_mut_arr_add_strcpy(doc, arr, entries[i].name))
            return false;
    }
    return yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_brush_contents(yyjson_mut_doc *doc, yyjson_mut_val *brush, unsigned int flags)
{
    static const brush_flag_export_entry entries[] = {
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID, "solid"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP, "player_clip"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP, "projectile_clip"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER, "trigger"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER, "water"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA, "lava"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY, "sky"},
    };
    return export_add_flag_array(doc, brush, "contents", flags != 0u ? flags : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID,
                                 entries, SDL_arraysize(entries));
}

static const char *const editor_source_face_keys[6] = {"px", "nx", "py", "ny", "pz", "nz"};

static const char *editor_source_face_export_key(size_t face_index, char *buffer, size_t buffer_size)
{
    if (face_index < SDL_arraysize(editor_source_face_keys))
        return editor_source_face_keys[face_index];
    SDL_snprintf(buffer, buffer_size, "%llu", (unsigned long long)face_index);
    return buffer;
}

static bool export_add_source_face_material_overrides(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                                      const char *base_material, const char *const *face_materials,
                                                      size_t face_material_count)
{
    yyjson_mut_val *face_materials_obj = NULL;
    for (size_t i = 0; i < face_material_count; ++i)
    {
        const char *face_material = face_materials[i];
        if (face_material == NULL || face_material[0] == '\0' ||
            (base_material != NULL && SDL_strcmp(face_material, base_material) == 0))
        {
            continue;
        }
        if (face_materials_obj == NULL)
        {
            face_materials_obj = yyjson_mut_obj(doc);
            if (face_materials_obj == NULL)
                return false;
        }
        char face_key_buffer[16];
        if (!yyjson_mut_obj_add_strcpy(doc, face_materials_obj,
                                       editor_source_face_export_key(i, face_key_buffer, sizeof(face_key_buffer)),
                                       face_material))
            return false;
    }
    return face_materials_obj == NULL || yyjson_mut_obj_add_val(doc, obj, "face_materials", face_materials_obj);
}

static bool export_add_source_face_visuals(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                           const editor_brush_source_box_runtime *box, bool tint)
{
    yyjson_mut_val *values_obj = NULL;
    for (size_t i = 0; box != NULL && i < SDL_arraysize(box->face_visuals); ++i)
    {
        const editor_brush_visual_override_runtime *visual = &box->face_visuals[i];
        if ((tint && !visual->tint_enabled) || (!tint && !visual->has_color))
            continue;
        if (values_obj == NULL)
        {
            values_obj = yyjson_mut_obj(doc);
            if (values_obj == NULL)
                return false;
        }
        char face_key_buffer[16];
        if (!export_add_color(doc, values_obj,
                              editor_source_face_export_key(i, face_key_buffer, sizeof(face_key_buffer)),
                              tint ? visual->tint : visual->color))
            return false;
    }
    return values_obj == NULL || yyjson_mut_obj_add_val(doc, obj, tint ? "face_tints" : "face_colors", values_obj);
}

static bool export_add_source_model_box(yyjson_mut_doc *doc, yyjson_mut_val *boxes,
                                        const editor_brush_source_box_runtime *box)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || !yyjson_mut_arr_add_val(boxes, obj))
        return false;
    return yyjson_mut_obj_add_strcpy(doc, obj, "stable_id", box->stable_id != NULL ? box->stable_id : "") &&
           yyjson_mut_obj_add_strcpy(doc, obj, "name", box->name != NULL ? box->name : "") &&
           yyjson_mut_obj_add_strcpy(doc, obj, "kind", box->vertex_count > 0 ? "convex" : "box") &&
           yyjson_mut_obj_add_strcpy(doc, obj, "prefab", box->prefab != NULL ? box->prefab : "box") &&
           yyjson_mut_obj_add_strcpy(doc, obj, "material", box->material != NULL ? box->material : "") &&
           (!box->visual.has_color || export_add_color(doc, obj, "color", box->visual.color)) &&
           (!box->visual.tint_enabled || (yyjson_mut_obj_add_bool(doc, obj, "tint_enabled", true) &&
                                          export_add_color(doc, obj, "tint", box->visual.tint))) &&
           (box->vertex_count > 0 ||
            (export_add_vec3i_values(doc, obj, "min", box->min[0], box->min[1], box->min[2]) &&
             export_add_vec3i_values(doc, obj, "max", box->max[0], box->max[1], box->max[2]))) &&
           export_add_source_vertices(doc, obj, box) &&
           export_add_source_face_material_overrides(doc, obj, box->material, (const char *const *)box->face_materials,
                                                     SDL_arraysize(box->face_materials)) &&
           export_add_source_face_visuals(doc, obj, box, false) &&
           export_add_source_face_visuals(doc, obj, box, true) &&
           export_add_properties(doc, obj, "properties", box->properties) &&
           export_add_brush_contents(doc, obj,
                                     box->contents != 0u ? box->contents : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID);
}

static bool export_add_editor_brush_source_world(yyjson_mut_doc *doc, yyjson_mut_val *sources,
                                                 const brush_world_runtime *world_runtime)
{
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *boxes = yyjson_mut_arr(doc);
    if (obj == NULL || boxes == NULL || !yyjson_mut_arr_add_val(sources, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "world", world->name != NULL ? world->name : "") ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "coordinate_system", "fixed_millimeters") ||
        !yyjson_mut_obj_add_real(doc, obj, "meters_per_unit",
                                 world_runtime->editor_source_meters_per_unit > 0.0f
                                     ? world_runtime->editor_source_meters_per_unit
                                     : 0.001f) ||
        !yyjson_mut_obj_add_int(doc, obj, "snap_units",
                                world_runtime->editor_source_snap_units > 0 ? world_runtime->editor_source_snap_units
                                                                            : 1) ||
        !yyjson_mut_obj_add_val(doc, obj, "boxes", boxes))
    {
        return false;
    }

    if (!world_runtime->editor_has_source_model)
    {
        return false;
    }
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
    {
        if (!export_add_source_model_box(doc, boxes, &world_runtime->editor_source_boxes[i]))
            return false;
    }
    return true;
}

static bool export_add_surface_flags(yyjson_mut_doc *doc, yyjson_mut_val *face, unsigned int flags)
{
    if (flags == 0u)
        return true;
    static const brush_flag_export_entry entries[] = {
        {SLAYER3D_GAME_DATA_BRUSH_SURFACE_NO_COLLIDE, "nocollide"},
        {SLAYER3D_GAME_DATA_BRUSH_SURFACE_SLICK, "slick"},
        {SLAYER3D_GAME_DATA_BRUSH_SURFACE_LADDER, "ladder"},
        {SLAYER3D_GAME_DATA_BRUSH_SURFACE_EMISSIVE, "emissive"},
        {SLAYER3D_GAME_DATA_BRUSH_SURFACE_PORTAL_CANDIDATE, "portal_candidate"},
    };
    return export_add_flag_array(doc, face, "surface_flags", flags, entries, SDL_arraysize(entries));
}

static bool export_add_brush_material(yyjson_mut_doc *doc, yyjson_mut_val *materials,
                                      const slayer3d_game_data_brush_material *material)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    return obj != NULL && yyjson_mut_arr_add_val(materials, obj) &&
           yyjson_mut_obj_add_strcpy(doc, obj, "name", material->name != NULL ? material->name : "") &&
           export_add_optional_string(doc, obj, "texture", material->texture) &&
           export_add_vec4(doc, obj, "albedo", material->albedo) &&
           yyjson_mut_obj_add_real(doc, obj, "metallic", material->metallic) &&
           yyjson_mut_obj_add_real(doc, obj, "roughness", material->roughness) &&
           export_add_vec3(doc, obj, "emissive", material->emissive) &&
           yyjson_mut_obj_add_real(doc, obj, "tex_scale", material->tex_scale) &&
           export_add_editor_metadata(doc, obj, &material->editor);
}

static bool export_add_brush_face(yyjson_mut_doc *doc, yyjson_mut_val *faces,
                                  const slayer3d_game_data_brush_world *world,
                                  const slayer3d_game_data_brush_face *face)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *plane = yyjson_mut_obj(doc);
    yyjson_mut_val *uv = yyjson_mut_obj(doc);
    const char *material = face->material_name;
    if (material == NULL && face->material_index >= 0 && face->material_index < world->material_count)
        material = world->materials[face->material_index].name;
    return obj != NULL && plane != NULL && uv != NULL && yyjson_mut_arr_add_val(faces, obj) &&
           yyjson_mut_obj_add_val(doc, obj, "plane", plane) && export_add_vec3(doc, plane, "normal", face->normal) &&
           yyjson_mut_obj_add_real(doc, plane, "distance", face->distance) &&
           yyjson_mut_obj_add_strcpy(doc, obj, "material", material != NULL ? material : "") &&
           yyjson_mut_obj_add_val(doc, obj, "uv", uv) &&
           export_add_vec2_values(doc, uv, "scale", face->uv_scale[0], face->uv_scale[1]) &&
           export_add_vec2_values(doc, uv, "offset", face->uv_offset[0], face->uv_offset[1]) &&
           yyjson_mut_obj_add_real(doc, uv, "rotation_degrees", face->uv_rotation_degrees) &&
           export_add_surface_flags(doc, obj, face->surface_flags) &&
           (!face->has_color || export_add_color(doc, obj, "color", face->color)) &&
           (!face->tint_enabled || (yyjson_mut_obj_add_bool(doc, obj, "tint_enabled", true) &&
                                    export_add_color(doc, obj, "tint", face->tint))) &&
           export_add_editor_metadata(doc, obj, &face->editor);
}

static const char *brush_visibility_name(slayer3d_game_data_brush_visibility visibility)
{
    switch (visibility)
    {
    case SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_ALWAYS:
        return "always";
    case SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_TRACE:
        return "trace";
    case SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_AUTO:
    default:
        return "auto";
    }
}

static bool export_add_brush(yyjson_mut_doc *doc, yyjson_mut_val *brushes, const slayer3d_game_data_brush_world *world,
                             const slayer3d_game_data_brush *brush)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *faces = yyjson_mut_arr(doc);
    if (obj == NULL || faces == NULL || !yyjson_mut_arr_add_val(brushes, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "name", brush->name != NULL ? brush->name : "") ||
        !export_add_brush_contents(doc, obj, brush->contents) ||
        !export_add_string_array(doc, obj, "tags", brush->tags, brush->tag_count) ||
        (brush->visibility != SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_AUTO &&
         !yyjson_mut_obj_add_strcpy(doc, obj, "visibility", brush_visibility_name(brush->visibility))) ||
        (brush->visibility_cullable && !yyjson_mut_obj_add_bool(doc, obj, "visibility_cullable", true)) ||
        !export_add_editor_metadata(doc, obj, &brush->editor) || !yyjson_mut_obj_add_val(doc, obj, "faces", faces))
    {
        return false;
    }
    for (int i = 0; i < brush->face_count; ++i)
    {
        if (!export_add_brush_face(doc, faces, world, &brush->faces[i]))
            return false;
    }
    return true;
}

static bool export_add_brush_world(yyjson_mut_doc *doc, yyjson_mut_val *worlds,
                                   const slayer3d_game_data_brush_world *world)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *materials = yyjson_mut_arr(doc);
    yyjson_mut_val *brushes = yyjson_mut_arr(doc);
    yyjson_mut_val *compile = yyjson_mut_obj(doc);
    if (obj == NULL || materials == NULL || brushes == NULL || !yyjson_mut_arr_add_val(worlds, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "name", world->name != NULL ? world->name : "") ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "units", world->units != NULL ? world->units : "meters") ||
        !yyjson_mut_obj_add_real(doc, obj, "meters_per_unit", world->meters_per_unit) ||
        !yyjson_mut_obj_add_real(doc, obj, "visibility_cell_size", world->visibility_cell_size) ||
        !export_add_editor_metadata(doc, obj, &world->editor) ||
        !yyjson_mut_obj_add_val(doc, obj, "materials", materials) ||
        !yyjson_mut_obj_add_val(doc, obj, "brushes", brushes))
    {
        return false;
    }
    if (world->compile_hidden_face_culling == false || world->compile_chunk_cell_size_hint > 0.0f)
    {
        if (compile == NULL ||
            !yyjson_mut_obj_add_bool(doc, compile, "hidden_face_culling", world->compile_hidden_face_culling) ||
            (world->compile_chunk_cell_size_hint > 0.0f &&
             !yyjson_mut_obj_add_real(doc, compile, "chunk_cell_size", world->compile_chunk_cell_size_hint)) ||
            !yyjson_mut_obj_add_val(doc, obj, "compile", compile))
        {
            return false;
        }
    }
    for (int i = 0; i < world->material_count; ++i)
    {
        if (!export_add_brush_material(doc, materials, &world->materials[i]))
            return false;
    }
    for (int i = 0; i < world->brush_count; ++i)
    {
        if (!export_add_brush(doc, brushes, world, &world->brushes[i]))
            return false;
    }
    return true;
}

static bool validate_editable_source_for_export(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                                char *error_buffer, int error_buffer_size)
{
    slayer3d_game_data_editor_brush_source_diagnostics diagnostics;
    if (!slayer3d_game_data_validate_editor_brush_source_model(runtime, world_name, 1, &diagnostics, error_buffer,
                                                               error_buffer_size))
    {
        return false;
    }
    if (!diagnostics.structurally_valid)
    {
        set_errorf(error_buffer, error_buffer_size, "editable level source model for '%s' is invalid: %s",
                   world_name != NULL ? world_name : "<unnamed>",
                   diagnostics.first_issue[0] != '\0' ? diagnostics.first_issue : "source integrity check failed");
        return false;
    }
    return true;
}

bool slayer3d_game_data_export_brush_world_fragment_json(const slayer3d_game_data_runtime *runtime,
                                                         const char *world_name, char **out_json, size_t *out_size,
                                                         char *error_buffer, int error_buffer_size)
{
    if (out_json != NULL)
        *out_json = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || out_json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush world export requires runtime, world name, and output");
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "brush world '%s' not found", world_name);
        return false;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *worlds = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    if (doc == NULL || root == NULL || worlds == NULL)
    {
        yyjson_mut_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to allocate brush world export document");
        return false;
    }

    yyjson_mut_doc_set_root(doc, root);
    bool ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.fragment.v0") &&
              yyjson_mut_obj_add_val(doc, root, "brush_worlds", worlds) &&
              export_add_brush_world(doc, worlds, &world_runtime->desc);
    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to write brush world export JSON");
        return false;
    }

    char *copy = (char *)SDL_malloc(size + 1u);
    if (copy == NULL)
    {
        free(json);
        set_error(error_buffer, error_buffer_size, "failed to allocate brush world export JSON");
        return false;
    }
    SDL_memcpy(copy, json, size + 1u);
    free(json);
    *out_json = copy;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

static bool export_add_player_start(yyjson_mut_doc *doc, yyjson_mut_val *starts,
                                    const editor_player_start_runtime *start)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || !yyjson_mut_arr_add_val(starts, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "name", start->name != NULL ? start->name : "") ||
        !export_add_vec3(doc, obj, "position", start->position) ||
        !yyjson_mut_obj_add_real(doc, obj, "yaw", start->yaw) ||
        !yyjson_mut_obj_add_real(doc, obj, "pitch", start->pitch))
    {
        return false;
    }
    if (start->scene != NULL && !yyjson_mut_obj_add_strcpy(doc, obj, "scene", start->scene))
        return false;
    if (start->target != NULL && !yyjson_mut_obj_add_strcpy(doc, obj, "target", start->target))
        return false;
    return true;
}

static bool export_add_color(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, slayer3d_color value)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_uint(doc, arr, value.r) && yyjson_mut_arr_add_uint(doc, arr, value.g) &&
           yyjson_mut_arr_add_uint(doc, arr, value.b) && yyjson_mut_arr_add_uint(doc, arr, value.a) &&
           yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool export_add_global_properties(yyjson_mut_doc *doc, yyjson_mut_val *global,
                                         const slayer3d_properties *scene_state)
{
    yyjson_mut_val *properties = yyjson_mut_obj(doc);
    if (properties == NULL || !yyjson_mut_obj_add_val(doc, global, "properties", properties))
        return false;

    int count = slayer3d_properties_get_int(scene_state, "editor.global.property.count", 0);
    if (count < 0)
        count = 0;
    if (count > 64)
        count = 64;
    for (int i = 0; i < count; ++i)
    {
        char key_path[96];
        char value_path[96];
        SDL_snprintf(key_path, sizeof(key_path), "editor.global.property.%d.key", i);
        SDL_snprintf(value_path, sizeof(value_path), "editor.global.property.%d.value", i);
        const char *key = slayer3d_properties_get_string(scene_state, key_path, "");
        const char *value = slayer3d_properties_get_string(scene_state, value_path, "");
        if (key == NULL || key[0] == '\0' || value == NULL || value[0] == '\0')
            continue;
        if (!yyjson_mut_obj_add_strcpy(doc, properties, key, value))
            return false;
    }
    return true;
}

static bool export_add_map_global_state(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                        const slayer3d_game_data_runtime *runtime)
{
    yyjson_mut_val *global = yyjson_mut_obj(doc);
    yyjson_mut_val *fog = yyjson_mut_obj(doc);
    yyjson_mut_val *fog_properties = yyjson_mut_obj(doc);
    if (global == NULL || fog == NULL || fog_properties == NULL || !yyjson_mut_obj_add_val(doc, root, "global", global))
        return false;

    const slayer3d_properties *scene_state = runtime != NULL ? runtime->scene_state : NULL;
    const slayer3d_color ambient =
        slayer3d_properties_get_color(scene_state, "editor.global.ambient_light", (slayer3d_color){54, 56, 64, 255});
    const slayer3d_color clear =
        slayer3d_properties_get_color(scene_state, "editor.global.clear_color", (slayer3d_color){12, 14, 18, 255});
    const float exposure = slayer3d_properties_get_float(scene_state, "editor.global.exposure", 1.0f);
    const char *tonemap = slayer3d_properties_get_string(scene_state, "editor.global.tonemap", "aces");
    const char *quality =
        slayer3d_properties_get_string(scene_state, "editor.global.lighting_preview_quality", "balanced");
    const char *fog_mode = slayer3d_properties_get_string(scene_state, "editor.global.fog", "none");
    if (fog_mode == NULL || fog_mode[0] == '\0')
        fog_mode = "none";

    if (!export_add_color(doc, global, "ambient_light", ambient) ||
        !export_add_color(doc, global, "clear_color", clear) ||
        !yyjson_mut_obj_add_real(doc, global, "exposure", exposure) ||
        !yyjson_mut_obj_add_strcpy(doc, global, "tonemap", tonemap != NULL ? tonemap : "aces") ||
        !yyjson_mut_obj_add_strcpy(doc, global, "lighting_preview_quality", quality != NULL ? quality : "balanced") ||
        !yyjson_mut_obj_add_val(doc, global, "fog", fog) ||
        !yyjson_mut_obj_add_bool(doc, fog, "enabled", SDL_strcmp(fog_mode, "none") != 0) ||
        !yyjson_mut_obj_add_strcpy(doc, fog, "mode", fog_mode) ||
        !yyjson_mut_obj_add_val(doc, fog, "properties", fog_properties) ||
        !export_add_global_properties(doc, global, scene_state))
    {
        return false;
    }
    return true;
}

static bool export_add_editor_actor(yyjson_mut_doc *doc, yyjson_mut_val *actors, const editor_actor_runtime *actor)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || !yyjson_mut_arr_add_val(actors, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "name", actor->name != NULL ? actor->name : "") ||
        !export_add_optional_string(doc, obj, "scene", actor->scene) ||
        !export_add_optional_string(doc, obj, "display_name", actor->display_name) ||
        !export_add_optional_string(doc, obj, "archetype", actor->archetype) ||
        !export_add_optional_string(doc, obj, "mesh", actor->mesh) ||
        !export_add_optional_string(doc, obj, "model", actor->model) ||
        !export_add_optional_string(doc, obj, "group", actor->group) ||
        !export_add_vec3(doc, obj, "position", actor->position) ||
        !export_add_vec3(doc, obj, "rotation", actor->rotation) || !export_add_vec3(doc, obj, "scale", actor->scale) ||
        !export_add_color(doc, obj, "color", actor->color) ||
        !export_add_optional_string(doc, obj, "prefab", actor->prefab) ||
        !((actor->prefab != NULL && actor->prefab[0] != '\0')
              ? yyjson_mut_obj_add_bool(doc, obj, "prefab_linked", actor->prefab_linked)
              : true) ||
        !export_add_properties(doc, obj, "prefab_overrides", actor->prefab_overrides) ||
        !export_add_properties(doc, obj, "properties", actor->properties))
    {
        return false;
    }
    return true;
}

static bool export_add_editor_prefab(yyjson_mut_doc *doc, yyjson_mut_val *prefabs, const editor_prefab_runtime *prefab)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || !yyjson_mut_arr_add_val(prefabs, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "id", prefab->id != NULL ? prefab->id : "") ||
        !export_add_optional_string(doc, obj, "label", prefab->label) ||
        !export_add_optional_string(doc, obj, "category", prefab->category) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "kind", prefab->kind != NULL ? prefab->kind : "actor") ||
        !export_add_optional_string(doc, obj, "archetype", prefab->archetype) ||
        !export_add_optional_string(doc, obj, "mesh", prefab->mesh) ||
        !export_add_optional_string(doc, obj, "model", prefab->model) ||
        !export_add_optional_string(doc, obj, "group", prefab->group) ||
        !export_add_vec3(doc, obj, "position", prefab->position) ||
        !export_add_vec3(doc, obj, "rotation", prefab->rotation) ||
        !export_add_vec3(doc, obj, "scale", prefab->scale) || !export_add_color(doc, obj, "color", prefab->color) ||
        !export_add_properties(doc, obj, "properties", prefab->properties))
    {
        return false;
    }
    return true;
}

static bool export_add_connection_endpoint(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key,
                                           const editor_connection_endpoint_runtime *endpoint)
{
    yyjson_mut_val *json = yyjson_mut_obj(doc);
    if (json == NULL || !yyjson_mut_obj_add_val(doc, obj, key, json) ||
        !yyjson_mut_obj_add_strcpy(doc, json, "entity", endpoint->entity != NULL ? endpoint->entity : "") ||
        !export_add_optional_string(doc, json, "event", endpoint->event) ||
        !export_add_optional_string(doc, json, "action", endpoint->action))
    {
        return false;
    }
    return !endpoint->external || yyjson_mut_obj_add_bool(doc, json, "external", true);
}

static bool export_add_editor_connection(yyjson_mut_doc *doc, yyjson_mut_val *connections,
                                         const editor_connection_runtime *connection)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || !yyjson_mut_arr_add_val(connections, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "id", connection->id != NULL ? connection->id : "") ||
        !export_add_connection_endpoint(doc, obj, "from", &connection->from) ||
        !export_add_connection_endpoint(doc, obj, "to", &connection->to) ||
        !export_add_properties(doc, obj, "properties", connection->properties))
    {
        return false;
    }
    return true;
}

bool slayer3d_game_data_export_player_starts_fragment_json(const slayer3d_game_data_runtime *runtime, char **out_json,
                                                           size_t *out_size, char *error_buffer, int error_buffer_size)
{
    if (out_json != NULL)
        *out_json = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (runtime == NULL || out_json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "player start export requires runtime and output");
        return false;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *starts = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    if (doc == NULL || root == NULL || starts == NULL)
    {
        yyjson_mut_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to allocate player start export document");
        return false;
    }

    yyjson_mut_doc_set_root(doc, root);
    bool ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.fragment.v0") &&
              yyjson_mut_obj_add_val(doc, root, "editor_player_starts", starts);
    for (int i = 0; ok && i < runtime->editor_player_start_count; ++i)
        ok = export_add_player_start(doc, starts, &runtime->editor_player_starts[i]);

    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to write player start export JSON");
        return false;
    }

    char *copy = (char *)SDL_malloc(size + 1u);
    if (copy == NULL)
    {
        free(json);
        set_error(error_buffer, error_buffer_size, "failed to allocate player start export JSON");
        return false;
    }
    SDL_memcpy(copy, json, size + 1u);
    free(json);
    *out_json = copy;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

bool slayer3d_game_data_export_editable_level_fragment_json(const slayer3d_game_data_runtime *runtime,
                                                            const char *world_name, char **out_json, size_t *out_size,
                                                            char *error_buffer, int error_buffer_size)
{
    if (out_json != NULL)
        *out_json = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (runtime == NULL || out_json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "editable level export requires runtime and output");
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "brush world '%s' not found", world_name);
        return false;
    }
    if (!validate_editable_source_for_export(runtime, world_name, error_buffer, error_buffer_size))
        return false;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *worlds = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *sources = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *starts = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *actors = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *prefabs = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *connections = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    if (doc == NULL || root == NULL || worlds == NULL || sources == NULL || starts == NULL || actors == NULL ||
        prefabs == NULL || connections == NULL)
    {
        yyjson_mut_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to allocate editable level export document");
        return false;
    }

    yyjson_mut_doc_set_root(doc, root);
    bool ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.fragment.v0") &&
              yyjson_mut_obj_add_val(doc, root, "brush_worlds", worlds) &&
              export_add_brush_world(doc, worlds, &world_runtime->desc) &&
              yyjson_mut_obj_add_val(doc, root, "editor_brush_sources", sources) &&
              export_add_editor_brush_source_world(doc, sources, world_runtime) &&
              yyjson_mut_obj_add_val(doc, root, "editor_player_starts", starts) &&
              yyjson_mut_obj_add_val(doc, root, "editor_actors", actors) &&
              yyjson_mut_obj_add_val(doc, root, "editor_prefabs", prefabs) &&
              yyjson_mut_obj_add_val(doc, root, "editor_connections", connections);
    for (int i = 0; ok && i < runtime->editor_player_start_count; ++i)
        ok = export_add_player_start(doc, starts, &runtime->editor_player_starts[i]);
    for (int i = 0; ok && i < runtime->editor_actor_count; ++i)
        ok = export_add_editor_actor(doc, actors, &runtime->editor_actors[i]);
    for (int i = 0; ok && i < runtime->editor_prefab_count; ++i)
        ok = export_add_editor_prefab(doc, prefabs, &runtime->editor_prefabs[i]);
    for (int i = 0; ok && i < runtime->editor_connection_count; ++i)
        ok = export_add_editor_connection(doc, connections, &runtime->editor_connections[i]);

    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to write editable level export JSON");
        return false;
    }

    char *copy = (char *)SDL_malloc(size + 1u);
    if (copy == NULL)
    {
        free(json);
        set_error(error_buffer, error_buffer_size, "failed to allocate editable level export JSON");
        return false;
    }
    SDL_memcpy(copy, json, size + 1u);
    free(json);
    *out_json = copy;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

static bool editor_save_write_all(SDL_IOStream *stream, const void *data, size_t size)
{
    const Uint8 *bytes = (const Uint8 *)data;
    size_t written = 0u;
    while (written < size)
    {
        const size_t chunk = SDL_WriteIO(stream, bytes + written, size - written);
        if (chunk == 0u)
            return false;
        written += chunk;
    }
    return true;
}

static bool editor_save_make_directory_recursive(const char *path)
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
        if (copy[0] != '\0')
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

static char *editor_save_parent_directory(const char *path)
{
    if (path == NULL)
        return NULL;
    const char *slash = SDL_strrchr(path, '/');
    const char *backslash = SDL_strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
    if (slash == NULL)
        return SDL_strdup(".");
    const size_t len = (size_t)(slash - path);
    if (len == 0u)
        return SDL_strdup("/");
    char *parent = (char *)SDL_malloc(len + 1u);
    if (parent == NULL)
        return NULL;
    SDL_memcpy(parent, path, len);
    parent[len] = '\0';
    return parent;
}

bool editor_save_bytes_atomic(const char *path, const void *data, size_t size, const char *kind, char *error_buffer,
                              int error_buffer_size)
{
    if (path == NULL || path[0] == '\0' || SDL_strstr(path, "://") != NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "%s save requires a filesystem path", kind != NULL ? kind : "file");
        return false;
    }

    char *parent = editor_save_parent_directory(path);
    if (parent == NULL || !editor_save_make_directory_recursive(parent))
    {
        SDL_free(parent);
        set_errorf(error_buffer, error_buffer_size, "failed to create %s save directory", kind != NULL ? kind : "file");
        return false;
    }
    SDL_free(parent);

    bool ok = false;
    char temp_path[4096];
    for (int attempt = 0; attempt < 16 && !ok; ++attempt)
    {
        SDL_snprintf(temp_path, sizeof(temp_path), "%s.tmp.%llu.%d", path, (unsigned long long)SDL_GetTicksNS(),
                     attempt);
        SDL_IOStream *stream = SDL_IOFromFile(temp_path, "wb");
        if (stream == NULL)
            continue;
        ok = editor_save_write_all(stream, data, size) && SDL_FlushIO(stream);
        ok = SDL_CloseIO(stream) && ok;
        if (!ok)
        {
            SDL_RemovePath(temp_path);
            continue;
        }
        if (!SDL_RenamePath(temp_path, path))
        {
            SDL_RemovePath(path);
            ok = SDL_RenamePath(temp_path, path);
        }
        if (!ok)
            SDL_RemovePath(temp_path);
    }

    if (!ok)
        set_errorf(error_buffer, error_buffer_size, "failed to save %s", kind != NULL ? kind : "file");
    return ok;
}

bool slayer3d_game_data_save_brush_world_fragment_file(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                       const char *path, size_t *out_size, char *error_buffer,
                                                       int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0u;

    char *json = NULL;
    size_t size = 0u;
    if (!slayer3d_game_data_export_brush_world_fragment_json(runtime, world_name, &json, &size, error_buffer,
                                                             error_buffer_size))
    {
        return false;
    }

    const bool ok = editor_save_bytes_atomic(path, json, size, "brush world fragment", error_buffer, error_buffer_size);
    SDL_free(json);
    if (!ok)
        return false;
    if (!slayer3d_game_data_mark_brush_world_saved(runtime, world_name, path, error_buffer, error_buffer_size))
        return false;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

bool slayer3d_game_data_save_editable_level_fragment_file(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                          const char *path, size_t *out_size, char *error_buffer,
                                                          int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0u;

    char *json = NULL;
    size_t size = 0u;
    if (!slayer3d_game_data_export_editable_level_fragment_json(runtime, world_name, &json, &size, error_buffer,
                                                                error_buffer_size))
    {
        return false;
    }

    const bool ok =
        editor_save_bytes_atomic(path, json, size, "editable level fragment", error_buffer, error_buffer_size);
    SDL_free(json);
    if (!ok)
        return false;
    if (!slayer3d_game_data_mark_brush_world_saved(runtime, world_name, path, error_buffer, error_buffer_size))
        return false;
    if (!slayer3d_game_data_mark_player_starts_saved(runtime, path, error_buffer, error_buffer_size))
        return false;
    if (runtime != NULL)
    {
        runtime->editor_actor_dirty = false;
        runtime->editor_prefab_dirty = false;
        runtime->editor_connection_dirty = false;
    }
    if (out_size != NULL)
        *out_size = size;
    return true;
}

static double map_color_channel(float value)
{
    if (value < 0.0f)
        value = 0.0f;
    if (value > 1.0f)
        value = 1.0f;
    return (double)(int)((double)value * 255.0 + 0.5);
}

static bool export_add_map_color(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, slayer3d_vec4 value)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_real(doc, arr, map_color_channel(value.x)) &&
           yyjson_mut_arr_add_real(doc, arr, map_color_channel(value.y)) &&
           yyjson_mut_arr_add_real(doc, arr, map_color_channel(value.z)) &&
           yyjson_mut_arr_add_real(doc, arr, map_color_channel(value.w)) && yyjson_mut_obj_add_val(doc, obj, key, arr);
}

/* Saved maps are self-contained: exported asset references are rewritten to
 * map-relative paths and the referenced files are copied next to the map. */
#define EXPORT_ASSET_COPY_CAP 96

typedef struct export_asset_copy
{
    char relative[512];
    char source[1024];
} export_asset_copy;

typedef struct export_asset_copies
{
    export_asset_copy entries[EXPORT_ASSET_COPY_CAP];
    int count;
} export_asset_copies;

static void export_asset_copies_add(export_asset_copies *copies, const char *relative, const char *source)
{
    if (copies == NULL || relative == NULL || relative[0] == '\0' || source == NULL || source[0] == '\0')
        return;
    for (int i = 0; i < copies->count; ++i)
    {
        if (SDL_strcmp(copies->entries[i].relative, relative) == 0)
            return;
    }
    if (copies->count >= EXPORT_ASSET_COPY_CAP)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Map export asset copy list is full; skipping '%s'", relative);
        return;
    }
    export_asset_copy *entry = &copies->entries[copies->count++];
    SDL_strlcpy(entry->relative, relative, sizeof(entry->relative));
    SDL_strlcpy(entry->source, source, sizeof(entry->source));
}

static bool export_reference_is_absolute(const char *reference)
{
    if (reference == NULL || reference[0] == '\0')
        return false;
    if (reference[0] == '/' || reference[0] == '\\')
        return true;
    return SDL_strlen(reference) > 2u && reference[1] == ':';
}

static const char *export_reference_basename(const char *reference)
{
    const char *base = reference;
    for (const char *p = reference; p != NULL && *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    return base;
}

/* Rewrite an editor asset reference to a map-relative path and record the
 * file for copying beside the saved map. Absolute sources are placed under
 * @p fallback_dir using their file name. Returns NULL for empty references. */
static const char *export_map_asset_reference(const char *reference, const char *fallback_dir,
                                              export_asset_copies *copies, char *buffer, size_t buffer_size)
{
    if (reference == NULL || reference[0] == '\0')
        return NULL;
    if (SDL_strncmp(reference, "asset://", 8u) == 0)
    {
        SDL_strlcpy(buffer, reference + 8u, buffer_size);
        export_asset_copies_add(copies, buffer, reference);
        return buffer[0] != '\0' ? buffer : NULL;
    }
    if (export_reference_is_absolute(reference))
    {
        SDL_snprintf(buffer, buffer_size, "%s/%s", fallback_dir, export_reference_basename(reference));
        export_asset_copies_add(copies, buffer, reference);
        return buffer;
    }
    char source[1024];
    SDL_snprintf(source, sizeof(source), "asset://%s", reference);
    SDL_strlcpy(buffer, reference, buffer_size);
    export_asset_copies_add(copies, buffer, source);
    return buffer;
}

static bool export_add_map_materials(yyjson_mut_doc *doc, yyjson_mut_val *materials,
                                     const slayer3d_game_data_brush_world *world, export_asset_copies *copies)
{
    if (doc == NULL || materials == NULL || world == NULL)
        return false;
    for (int i = 0; i < world->material_count; ++i)
    {
        const slayer3d_game_data_brush_material *material = &world->materials[i];
        if (material->name == NULL || material->name[0] == '\0')
            continue;
        char texture_buffer[512];
        const char *texture =
            export_map_asset_reference(material->texture, "textures", copies, texture_buffer, sizeof(texture_buffer));
        yyjson_mut_val *obj = yyjson_mut_obj(doc);
        if (obj == NULL || !yyjson_mut_arr_add_val(materials, obj) ||
            !yyjson_mut_obj_add_strcpy(doc, obj, "id", material->name) ||
            !export_add_optional_string(doc, obj, "texture", texture) ||
            !export_add_map_color(doc, obj, "color", material->albedo))
        {
            return false;
        }
    }
    return true;
}

static bool export_add_map_plane(yyjson_mut_doc *doc, yyjson_mut_val *planes,
                                 const slayer3d_game_data_brush_world *world, const slayer3d_game_data_brush_face *face)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || !yyjson_mut_arr_add_val(planes, obj) || !export_add_vec3(doc, obj, "normal", face->normal) ||
        !yyjson_mut_obj_add_real(doc, obj, "distance", face->distance))
    {
        return false;
    }

    const char *material = face->material_name;
    if ((material == NULL || material[0] == '\0') && face->material_index >= 0 &&
        face->material_index < world->material_count)
    {
        material = world->materials[face->material_index].name;
    }
    return (material == NULL || material[0] == '\0' || yyjson_mut_obj_add_strcpy(doc, obj, "material", material)) &&
           (!face->has_color || export_add_color(doc, obj, "color", face->color)) &&
           (!face->tint_enabled || (yyjson_mut_obj_add_bool(doc, obj, "tint_enabled", true) &&
                                    export_add_color(doc, obj, "tint", face->tint)));
}

static bool export_add_map_brush(yyjson_mut_doc *doc, yyjson_mut_val *brushes,
                                 const slayer3d_game_data_brush_world *world, const slayer3d_game_data_brush *brush)
{
    if (doc == NULL || brushes == NULL || world == NULL || brush == NULL)
        return false;
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *geometry = yyjson_mut_obj(doc);
    yyjson_mut_val *planes = yyjson_mut_arr(doc);
    yyjson_mut_val *properties = yyjson_mut_obj(doc);
    const char *id = brush->name != NULL && brush->name[0] != '\0' ? brush->name : "brush";
    if (obj == NULL || geometry == NULL || planes == NULL || properties == NULL ||
        !yyjson_mut_arr_add_val(brushes, obj) || !yyjson_mut_obj_add_strcpy(doc, obj, "id", id) ||
        !export_add_brush_contents(doc, obj, brush->contents) ||
        !yyjson_mut_obj_add_val(doc, obj, "geometry", geometry) ||
        !yyjson_mut_obj_add_strcpy(doc, geometry, "kind", "planes") ||
        !yyjson_mut_obj_add_val(doc, geometry, "planes", planes) ||
        !yyjson_mut_obj_add_val(doc, obj, "properties", properties) ||
        !yyjson_mut_obj_add_strcpy(doc, properties, "brush_world", world->name != NULL ? world->name : ""))
    {
        return false;
    }
    for (int face_index = 0; face_index < brush->face_count; ++face_index)
    {
        if (!export_add_map_plane(doc, planes, world, &brush->faces[face_index]))
            return false;
    }
    return true;
}

static bool export_add_map_brushes(yyjson_mut_doc *doc, yyjson_mut_val *brushes,
                                   const slayer3d_game_data_brush_world *world)
{
    if (doc == NULL || brushes == NULL || world == NULL)
        return false;
    for (int i = 0; i < world->brush_count; ++i)
    {
        if (!export_add_map_brush(doc, brushes, world, &world->brushes[i]))
            return false;
    }
    return true;
}

static bool export_add_map_brushes_from_source(yyjson_mut_doc *doc, yyjson_mut_val *brushes,
                                               const brush_world_runtime *world_runtime, char *error_buffer,
                                               int error_buffer_size)
{
    if (doc == NULL || brushes == NULL || world_runtime == NULL)
        return false;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
    {
        slayer3d_game_data_brush brush;
        SDL_zero(brush);
        if (!editor_brush_source_box_to_runtime_brush(world_runtime, &world_runtime->editor_source_boxes[i], &brush,
                                                      error_buffer, error_buffer_size))
        {
            return false;
        }
        const bool ok = export_add_map_brush(doc, brushes, &world_runtime->desc, &brush);
        editor_brush_source_free_runtime_brush(&brush);
        if (!ok)
            return false;
    }
    return true;
}

static bool export_add_map_actor_position(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key,
                                          slayer3d_vec3 value)
{
    return export_add_vec3(doc, obj, key, value);
}

static bool export_add_map_player_start_actor(yyjson_mut_doc *doc, yyjson_mut_val *actors,
                                              const editor_player_start_runtime *start)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *transform = yyjson_mut_obj(doc);
    yyjson_mut_val *rotation = yyjson_mut_arr(doc);
    yyjson_mut_val *properties = yyjson_mut_obj(doc);
    if (obj == NULL || transform == NULL || rotation == NULL || properties == NULL ||
        !yyjson_mut_arr_add_val(actors, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "id", start->name != NULL ? start->name : "player_start") ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "archetype", "player_start") ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "primitive", "capsule") ||
        !yyjson_mut_obj_add_val(doc, obj, "transform", transform) ||
        !export_add_map_actor_position(doc, transform, "position", start->position) ||
        !yyjson_mut_arr_add_real(doc, rotation, start->pitch) || !yyjson_mut_arr_add_real(doc, rotation, start->yaw) ||
        !yyjson_mut_arr_add_real(doc, rotation, 0.0) || !yyjson_mut_obj_add_val(doc, transform, "rotation", rotation) ||
        !yyjson_mut_obj_add_val(doc, obj, "properties", properties))
    {
        return false;
    }
    return yyjson_mut_obj_add_strcpy(doc, properties, "type", "player-character") &&
           export_add_optional_string(doc, properties, "scene", start->scene) &&
           export_add_optional_string(doc, properties, "target", start->target);
}

static bool export_add_map_player_start_actors(yyjson_mut_doc *doc, yyjson_mut_val *actors,
                                               const slayer3d_game_data_runtime *runtime)
{
    for (int i = 0; i < runtime->editor_player_start_count; ++i)
    {
        if (!export_add_map_player_start_actor(doc, actors, &runtime->editor_player_starts[i]))
            return false;
    }
    return true;
}

static const char *editor_actor_map_primitive(const editor_actor_runtime *actor)
{
    const char *role =
        actor != NULL && actor->properties != NULL ? slayer3d_properties_get_string(actor->properties, "role", "") : "";
    const char *sensor_profile = actor != NULL && actor->properties != NULL
                                     ? slayer3d_properties_get_string(actor->properties, "sensor_profile", "")
                                     : "";
    if ((role != NULL && SDL_strcmp(role, "trigger") == 0) ||
        (sensor_profile != NULL && SDL_strcmp(sensor_profile, "volume") == 0))
    {
        return "trigger";
    }
    const char *mesh = actor != NULL ? actor->mesh : NULL;
    if (mesh == NULL || mesh[0] == '\0')
        return "box";
    if (SDL_strcmp(mesh, "capsule") == 0 || SDL_strcmp(mesh, "sphere") == 0 || SDL_strcmp(mesh, "cylinder") == 0 ||
        SDL_strcmp(mesh, "rectangle") == 0 || SDL_strcmp(mesh, "billboard") == 0 || SDL_strcmp(mesh, "trigger") == 0)
    {
        return mesh;
    }
    return "box";
}

static const char *editor_actor_map_display_mode(const editor_actor_runtime *actor)
{
    const char *display_mode = actor != NULL && actor->properties != NULL
                                   ? slayer3d_properties_get_string(actor->properties, "display_mode", NULL)
                                   : NULL;
    if (display_mode != NULL && (SDL_strcmp(display_mode, "solid") == 0 || SDL_strcmp(display_mode, "wireframe") == 0))
        return display_mode;
    return NULL;
}

static bool export_add_map_editor_actor(yyjson_mut_doc *doc, yyjson_mut_val *actors, const editor_actor_runtime *actor)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *transform = yyjson_mut_obj(doc);
    const char *display_mode = editor_actor_map_display_mode(actor);
    if (obj == NULL || transform == NULL || !yyjson_mut_arr_add_val(actors, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "id", actor->name != NULL ? actor->name : "editor_actor") ||
        !export_add_optional_string(doc, obj, "archetype", actor->archetype) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "primitive", editor_actor_map_primitive(actor)) ||
        !export_add_optional_string(doc, obj, "model", actor->model) ||
        !export_add_optional_string(doc, obj, "prefab", actor->prefab) ||
        !export_add_optional_string(doc, obj, "display_mode", display_mode) ||
        !((actor->prefab != NULL && actor->prefab[0] != '\0')
              ? yyjson_mut_obj_add_bool(doc, obj, "prefab_linked", actor->prefab_linked)
              : true) ||
        !yyjson_mut_obj_add_val(doc, obj, "transform", transform) ||
        !export_add_vec3(doc, transform, "position", actor->position) ||
        !export_add_vec3(doc, transform, "rotation", actor->rotation) ||
        !export_add_vec3(doc, transform, "scale", actor->scale) || !export_add_color(doc, obj, "color", actor->color) ||
        !export_add_properties(doc, obj, "properties", actor->properties))
    {
        return false;
    }
    return true;
}

static bool export_add_map_editor_actors(yyjson_mut_doc *doc, yyjson_mut_val *actors,
                                         const slayer3d_game_data_runtime *runtime)
{
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        if (!export_add_map_editor_actor(doc, actors, &runtime->editor_actors[i]))
            return false;
    }
    return true;
}

static const slayer3d_value *editor_actor_property(const editor_actor_runtime *actor, const char *key)
{
    return actor != NULL && actor->properties != NULL ? slayer3d_properties_get_value(actor->properties, key) : NULL;
}

static const char *editor_actor_property_string(const editor_actor_runtime *actor, const char *key,
                                                const char *fallback)
{
    return actor != NULL && actor->properties != NULL ? slayer3d_properties_get_string(actor->properties, key, fallback)
                                                      : fallback;
}

static float editor_actor_property_float(const editor_actor_runtime *actor, const char *key, float fallback)
{
    return actor != NULL && actor->properties != NULL ? slayer3d_properties_get_float(actor->properties, key, fallback)
                                                      : fallback;
}

static bool editor_actor_property_bool(const editor_actor_runtime *actor, const char *key, bool fallback)
{
    return actor != NULL && actor->properties != NULL ? slayer3d_properties_get_bool(actor->properties, key, fallback)
                                                      : fallback;
}

static slayer3d_vec3 editor_actor_property_vec3(const editor_actor_runtime *actor, const char *key,
                                                slayer3d_vec3 fallback)
{
    return actor != NULL && actor->properties != NULL ? slayer3d_properties_get_vec3(actor->properties, key, fallback)
                                                      : fallback;
}

static bool editor_actor_light_exportable(const editor_actor_runtime *actor)
{
    const char *role = editor_actor_property_string(actor, "role", "");
    const char *light_type = editor_actor_property_string(actor, "light_type", "");
    return actor != NULL && ((role != NULL && (SDL_strcmp(role, "light") == 0 || SDL_strcmp(role, "baked_light") == 0 ||
                                               SDL_strcmp(role, "static_light") == 0)) ||
                             (light_type != NULL && light_type[0] != '\0'));
}

static bool export_add_map_editor_light_animation(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                                  const editor_actor_runtime *actor)
{
    const bool enabled = editor_actor_property_bool(actor, "light_animation_enabled", false);
    const char *type = editor_actor_property_string(actor, "light_animation_type", "");
    if (!enabled && (type == NULL || type[0] == '\0' || SDL_strcmp(type, "none") == 0))
        return true;

    yyjson_mut_val *animation = yyjson_mut_obj(doc);
    if (animation == NULL || !yyjson_mut_obj_add_val(doc, obj, "animation", animation) ||
        !yyjson_mut_obj_add_bool(doc, animation, "enabled", enabled) ||
        !yyjson_mut_obj_add_strcpy(doc, animation, "type", type != NULL && type[0] != '\0' ? type : "none"))
    {
        return false;
    }

    const char *preset = editor_actor_property_string(actor, "light_animation_preset", NULL);
    if (!export_add_optional_string(doc, animation, "preset", preset))
        return false;

    const float rate_hz = editor_actor_property_float(actor, "light_animation_rate_hz", -1.0f);
    const float amplitude = editor_actor_property_float(actor, "light_animation_amplitude", -1.0f);
    const float phase = editor_actor_property_float(actor, "light_animation_phase", -1.0f);
    const float min_intensity = editor_actor_property_float(actor, "light_animation_min_intensity", -1.0f);
    const float max_intensity = editor_actor_property_float(actor, "light_animation_max_intensity", -1.0f);
    const float radius = editor_actor_property_float(actor, "light_animation_radius", -1.0f);
    if (rate_hz >= 0.0f && !yyjson_mut_obj_add_real(doc, animation, "rate_hz", rate_hz))
        return false;
    if (amplitude >= 0.0f && !yyjson_mut_obj_add_real(doc, animation, "amplitude", amplitude))
        return false;
    if (phase >= 0.0f && !yyjson_mut_obj_add_real(doc, animation, "phase", phase))
        return false;
    if (min_intensity >= 0.0f && !yyjson_mut_obj_add_real(doc, animation, "min_intensity", min_intensity))
        return false;
    if (max_intensity >= 0.0f && !yyjson_mut_obj_add_real(doc, animation, "max_intensity", max_intensity))
        return false;
    if (radius > 0.0f && !yyjson_mut_obj_add_real(doc, animation, "radius", radius))
        return false;

    const slayer3d_value *axis_value = editor_actor_property(actor, "light_animation_axis");
    if (axis_value != NULL && axis_value->type == SLAYER3D_VALUE_VEC3 &&
        !export_add_vec3(doc, animation, "axis", axis_value->as_vec3))
    {
        return false;
    }
    return true;
}

static bool export_add_map_editor_light(yyjson_mut_doc *doc, yyjson_mut_val *lights, const editor_actor_runtime *actor)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *transform = yyjson_mut_obj(doc);
    char light_id[192];
    SDL_snprintf(light_id, sizeof(light_id), "%s.light", actor->name != NULL ? actor->name : "light");
    if (obj == NULL || transform == NULL || !yyjson_mut_arr_add_val(lights, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "id", light_id) ||
        !export_add_optional_string(doc, obj, "source_actor", actor->name) ||
        !yyjson_mut_obj_add_val(doc, obj, "transform", transform) ||
        !export_add_vec3(doc, transform, "position", actor->position) ||
        !export_add_vec3(doc, transform, "rotation", actor->rotation) ||
        !export_add_vec3(doc, transform, "scale", actor->scale))
    {
        return false;
    }

    const char *role = editor_actor_property_string(actor, "role", "");
    const char *kind = editor_actor_property_string(actor, "light_kind", NULL);
    if (kind == NULL || kind[0] == '\0')
    {
        kind = (role != NULL && (SDL_strcmp(role, "baked_light") == 0 || SDL_strcmp(role, "static_light") == 0))
                   ? "baked"
                   : "dynamic";
    }
    const char *type = editor_actor_property_string(actor, "light_type", "point");
    if (type == NULL || type[0] == '\0')
        type = "point";
    else if (SDL_strcmp(type, "area") == 0)
        type = "area_rect";
    else if (SDL_strcmp(type, "ambient") == 0)
        type = "directional";

    slayer3d_color color = actor->color;
    const slayer3d_value *color_value = editor_actor_property(actor, "light_color");
    if (color_value != NULL && color_value->type == SLAYER3D_VALUE_COLOR)
        color = color_value->as_color;

    const slayer3d_vec3 default_direction = slayer3d_vec3_make(0.0f, -1.0f, 0.0f);
    const slayer3d_vec3 direction = editor_actor_property_vec3(actor, "light_direction", default_direction);
    if (!yyjson_mut_obj_add_strcpy(doc, obj, "kind", kind) || !yyjson_mut_obj_add_strcpy(doc, obj, "type", type) ||
        !export_add_color(doc, obj, "color", color) ||
        !yyjson_mut_obj_add_real(doc, obj, "intensity", editor_actor_property_float(actor, "light_intensity", 1.0f)) ||
        !yyjson_mut_obj_add_real(doc, obj, "range", editor_actor_property_float(actor, "light_range", 8.0f)) ||
        !export_add_vec3(doc, obj, "direction", direction) ||
        !yyjson_mut_obj_add_bool(doc, obj, "casts_shadow", editor_actor_property_bool(actor, "casts_shadow", false)))
    {
        return false;
    }
    const float inner_angle = editor_actor_property_float(actor, "inner_angle_degrees", -1.0f);
    const float outer_angle = editor_actor_property_float(actor, "outer_angle_degrees", -1.0f);
    const float width = editor_actor_property_float(actor, "width", -1.0f);
    const float height = editor_actor_property_float(actor, "height", -1.0f);
    const float radius = editor_actor_property_float(actor, "radius", -1.0f);
    if (inner_angle >= 0.0f && !yyjson_mut_obj_add_real(doc, obj, "inner_angle_degrees", inner_angle))
        return false;
    if (outer_angle >= 0.0f && !yyjson_mut_obj_add_real(doc, obj, "outer_angle_degrees", outer_angle))
        return false;
    if (width > 0.0f && !yyjson_mut_obj_add_real(doc, obj, "width", width))
        return false;
    if (height > 0.0f && !yyjson_mut_obj_add_real(doc, obj, "height", height))
        return false;
    if (radius > 0.0f && !yyjson_mut_obj_add_real(doc, obj, "radius", radius))
        return false;
    if (!export_add_optional_string(doc, obj, "shadow_mode",
                                    editor_actor_property_string(actor, "shadow_mode", NULL)) ||
        !export_add_optional_string(doc, obj, "falloff", editor_actor_property_string(actor, "falloff", NULL)) ||
        !export_add_optional_string(doc, obj, "bake_group", editor_actor_property_string(actor, "bake_group", NULL)) ||
        !export_add_map_editor_light_animation(doc, obj, actor) ||
        !export_add_properties(doc, obj, "properties", actor->properties))
    {
        return false;
    }
    return true;
}

static bool export_add_global_directional_light(yyjson_mut_doc *doc, yyjson_mut_val *lights,
                                                const slayer3d_properties *scene_state)
{
    if (!slayer3d_properties_get_bool(scene_state, "editor.global.directional.enabled", false))
        return true;

    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *transform = yyjson_mut_obj(doc);
    const slayer3d_color color = slayer3d_properties_get_color(scene_state, "editor.global.directional.color",
                                                               (slayer3d_color){255, 238, 160, 255});
    const slayer3d_vec3 direction = slayer3d_properties_get_vec3(scene_state, "editor.global.directional.direction",
                                                                 slayer3d_vec3_make(0.35f, -0.85f, 0.25f));
    const float intensity = slayer3d_properties_get_float(scene_state, "editor.global.directional.intensity", 1.0f);
    const char *kind = slayer3d_properties_get_string(scene_state, "editor.global.directional.kind", "baked");
    if (kind == NULL || kind[0] == '\0')
        kind = "baked";

    if (obj == NULL || transform == NULL || !yyjson_mut_arr_add_val(lights, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "id", "global.directional.light") ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "kind", kind) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "type", "directional") ||
        !yyjson_mut_obj_add_val(doc, obj, "transform", transform) ||
        !export_add_vec3(doc, transform, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)) ||
        !export_add_vec3(doc, transform, "rotation", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)) ||
        !export_add_vec3(doc, transform, "scale", slayer3d_vec3_make(1.0f, 1.0f, 1.0f)) ||
        !export_add_color(doc, obj, "color", color) || !yyjson_mut_obj_add_real(doc, obj, "intensity", intensity) ||
        !export_add_vec3(doc, obj, "direction", direction) ||
        !yyjson_mut_obj_add_bool(
            doc, obj, "casts_shadow",
            slayer3d_properties_get_bool(scene_state, "editor.global.directional.casts_shadow", true)) ||
        !yyjson_mut_obj_add_strcpy(
            doc, obj, "shadow_mode",
            slayer3d_properties_get_string(scene_state, "editor.global.directional.shadow_mode", "baked")) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "falloff", "none"))
    {
        return false;
    }
    return true;
}

static bool export_add_map_lights(yyjson_mut_doc *doc, yyjson_mut_val *lights,
                                  const slayer3d_game_data_runtime *runtime)
{
    if (!export_add_global_directional_light(doc, lights, runtime != NULL ? runtime->scene_state : NULL))
        return false;
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        const editor_actor_runtime *actor = &runtime->editor_actors[i];
        if (editor_actor_light_exportable(actor) && !export_add_map_editor_light(doc, lights, actor))
            return false;
    }
    return true;
}

/* Faces and layers may reference declared image assets by id at runtime;
 * saved maps write the underlying asset path, rewritten map-relative, so
 * any map consumer can resolve the sky without the editor shell. Absolute
 * sources land under skyboxes/<directory>/<file> beside the map. */
static const char *export_sky_texture_reference(const slayer3d_game_data_runtime *runtime, const char *reference,
                                                export_asset_copies *copies, char *buffer, size_t buffer_size)
{
    if (reference == NULL || reference[0] == '\0')
        return NULL;
    slayer3d_game_data_image_asset asset;
    if (slayer3d_game_data_get_image_asset(runtime, reference, &asset) && asset.path != NULL && asset.path[0] != '\0')
        reference = asset.path;
    char fallback_dir[512] = "skyboxes";
    if (export_reference_is_absolute(reference))
    {
        /* Keep the skybox's directory name so faces from one skybox stay
         * grouped under skyboxes/<name>/ next to the map. */
        char parent[512];
        SDL_strlcpy(parent, reference, sizeof(parent));
        char *leaf_split = SDL_strrchr(parent, '/');
        if (leaf_split == NULL)
            leaf_split = SDL_strrchr(parent, '\\');
        if (leaf_split != NULL)
        {
            *leaf_split = '\0';
            SDL_snprintf(fallback_dir, sizeof(fallback_dir), "skyboxes/%s", export_reference_basename(parent));
        }
    }
    return export_map_asset_reference(reference, fallback_dir, copies, buffer, buffer_size);
}

static bool export_add_map_sky_layers(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                      const slayer3d_game_data_runtime *runtime,
                                      const slayer3d_game_data_scene_skybox *skybox, export_asset_copies *copies)
{
    yyjson_mut_val *layers = yyjson_mut_arr(doc);
    if (layers == NULL || !yyjson_mut_obj_add_val(doc, obj, "layers", layers))
        return false;
    for (int i = 0; i < skybox->layer_count; ++i)
    {
        const slayer3d_game_data_scene_sky_layer *layer = &skybox->layers[i];
        char texture_buffer[512];
        const char *texture =
            export_sky_texture_reference(runtime, layer->texture, copies, texture_buffer, sizeof(texture_buffer));
        if (texture == NULL)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Skipping sky layer with non-portable texture reference '%s' during map export",
                        layer->texture != NULL ? layer->texture : "");
            continue;
        }
        yyjson_mut_val *entry = yyjson_mut_obj(doc);
        yyjson_mut_val *scroll = yyjson_mut_arr(doc);
        if (entry == NULL || scroll == NULL || !yyjson_mut_arr_add_val(layers, entry) ||
            !yyjson_mut_obj_add_strcpy(doc, entry, "texture", texture) ||
            !yyjson_mut_obj_add_val(doc, entry, "scroll", scroll) ||
            !yyjson_mut_arr_add_real(doc, scroll, layer->scroll_x) ||
            !yyjson_mut_arr_add_real(doc, scroll, layer->scroll_y) ||
            !yyjson_mut_obj_add_real(doc, entry, "scale", layer->scale) ||
            !yyjson_mut_obj_add_real(doc, entry, "opacity", layer->opacity) ||
            !yyjson_mut_obj_add_real(doc, entry, "depth", layer->depth))
        {
            return false;
        }
        if (layer->has_tint)
        {
            yyjson_mut_val *tint = yyjson_mut_arr(doc);
            if (tint == NULL || !yyjson_mut_obj_add_val(doc, entry, "tint", tint) ||
                !yyjson_mut_arr_add_int(doc, tint, layer->tint.r) ||
                !yyjson_mut_arr_add_int(doc, tint, layer->tint.g) || !yyjson_mut_arr_add_int(doc, tint, layer->tint.b))
            {
                return false;
            }
        }
    }
    if (yyjson_mut_arr_size(layers) == 0u)
        yyjson_mut_obj_remove_key(obj, "layers");
    return true;
}

static int export_portable_sky_layer_count(const slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_scene_skybox *skybox)
{
    int count = 0;
    for (int i = 0; i < skybox->layer_count; ++i)
    {
        char probe[512];
        if (export_sky_texture_reference(runtime, skybox->layers[i].texture, NULL, probe, sizeof(probe)) != NULL)
            ++count;
    }
    return count;
}

static bool export_add_map_skybox(yyjson_mut_doc *doc, yyjson_mut_val *root, const slayer3d_game_data_runtime *runtime,
                                  export_asset_copies *copies)
{
    slayer3d_game_data_scene_skybox skybox;
    if (!slayer3d_game_data_get_active_scene_skybox(runtime, &skybox))
        return true;

    char face_buffers[6][512];
    const char *faces[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    bool faces_portable = skybox.has_faces;
    if (skybox.has_faces)
    {
        const char *authored[6] = {skybox.pos_x, skybox.neg_x, skybox.pos_y, skybox.neg_y, skybox.pos_z, skybox.neg_z};
        for (int i = 0; i < 6; ++i)
        {
            faces[i] =
                export_sky_texture_reference(runtime, authored[i], copies, face_buffers[i], sizeof(face_buffers[i]));
            if (faces[i] == NULL)
                faces_portable = false;
        }
    }
    const bool has_preset = skybox.preset != NULL && skybox.preset[0] != '\0';
    const int portable_layer_count = export_portable_sky_layer_count(runtime, &skybox);
    if (!faces_portable && !has_preset && portable_layer_count == 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Skipping skybox export: active sky has no portable preset, face, or layer references");
        return true;
    }
    if (skybox.has_faces && !faces_portable)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Skipping skybox faces with non-portable references during map export");
    }

    const char *export_mode = skybox.mode;
    if (SDL_strcmp(export_mode, "layers") == 0 && portable_layer_count == 0 && !has_preset)
        export_mode = "cubemap";

    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || !yyjson_mut_obj_add_val(doc, root, "skybox", obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "id", has_preset ? skybox.preset : "active_scene_skybox") ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "mode", export_mode) ||
        (has_preset && !yyjson_mut_obj_add_strcpy(doc, obj, "preset", skybox.preset)) ||
        !yyjson_mut_obj_add_real(doc, obj, "size", skybox.size))
    {
        return false;
    }
    if (faces_portable)
    {
        static const char *const face_keys[6] = {"pos_x", "neg_x", "pos_y", "neg_y", "pos_z", "neg_z"};
        yyjson_mut_val *faces_obj = yyjson_mut_obj(doc);
        if (faces_obj == NULL || !yyjson_mut_obj_add_val(doc, obj, "faces", faces_obj))
            return false;
        for (int i = 0; i < 6; ++i)
        {
            if (!yyjson_mut_obj_add_strcpy(doc, faces_obj, face_keys[i], faces[i]))
                return false;
        }
    }
    if (skybox.layer_count > 0 && !export_add_map_sky_layers(doc, obj, runtime, &skybox, copies))
        return false;
    return true;
}

static const char *editor_effect_default_type(const char *kind)
{
    if (kind == NULL || kind[0] == '\0')
        return "marker";
    if (SDL_strcmp(kind, "particle_emitter") == 0)
        return "particles.emitter";
    if (SDL_strcmp(kind, "fog_volume") == 0)
        return "fog.volume";
    if (SDL_strcmp(kind, "fire") == 0)
        return "fire.marker";
    if (SDL_strcmp(kind, "smoke") == 0)
        return "smoke.marker";
    return "marker";
}

static bool editor_actor_effect_exportable(const editor_actor_runtime *actor)
{
    const char *role = editor_actor_property_string(actor, "role", "");
    const char *effect_kind = editor_actor_property_string(actor, "effect_kind", "");
    const char *effect_type = editor_actor_property_string(actor, "effect_type", "");
    const char *sensor_profile = editor_actor_property_string(actor, "sensor_profile", "");
    return actor != NULL &&
           ((role != NULL && SDL_strcmp(role, "effect") == 0) || (effect_kind != NULL && effect_kind[0] != '\0') ||
            (effect_type != NULL && effect_type[0] != '\0') ||
            (sensor_profile != NULL &&
             (SDL_strcmp(sensor_profile, "particle_emitter") == 0 || SDL_strcmp(sensor_profile, "fog_volume") == 0 ||
              SDL_strcmp(sensor_profile, "fire") == 0 || SDL_strcmp(sensor_profile, "smoke") == 0)));
}

static bool export_add_optional_actor_property_string(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *json_key,
                                                      const editor_actor_runtime *actor, const char *property_key)
{
    const char *value = editor_actor_property_string(actor, property_key, NULL);
    return value == NULL || value[0] == '\0' || yyjson_mut_obj_add_strcpy(doc, obj, json_key, value);
}

static bool export_add_optional_actor_property_number(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *json_key,
                                                      const editor_actor_runtime *actor, const char *property_key)
{
    const slayer3d_value *value = editor_actor_property(actor, property_key);
    if (value == NULL)
        return true;
    if (value->type == SLAYER3D_VALUE_FLOAT)
        return yyjson_mut_obj_add_real(doc, obj, json_key, value->as_float);
    if (value->type == SLAYER3D_VALUE_INT)
        return yyjson_mut_obj_add_int(doc, obj, json_key, value->as_int);
    return true;
}

static bool export_add_optional_actor_property_int(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *json_key,
                                                   const editor_actor_runtime *actor, const char *property_key)
{
    const slayer3d_value *value = editor_actor_property(actor, property_key);
    if (value == NULL)
        return true;
    if (value->type == SLAYER3D_VALUE_INT)
        return yyjson_mut_obj_add_int(doc, obj, json_key, value->as_int);
    if (value->type == SLAYER3D_VALUE_FLOAT)
        return yyjson_mut_obj_add_int(doc, obj, json_key, (int)value->as_float);
    return true;
}

static bool export_add_optional_actor_property_bool(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *json_key,
                                                    const editor_actor_runtime *actor, const char *property_key)
{
    const slayer3d_value *value = editor_actor_property(actor, property_key);
    if (value == NULL)
        return true;
    if (value->type == SLAYER3D_VALUE_BOOL)
        return yyjson_mut_obj_add_bool(doc, obj, json_key, value->as_bool);
    return true;
}

static bool export_add_map_editor_effect(yyjson_mut_doc *doc, yyjson_mut_val *effects,
                                         const editor_actor_runtime *actor)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *transform = yyjson_mut_obj(doc);
    char effect_id[192];
    SDL_snprintf(effect_id, sizeof(effect_id), "%s.effect", actor->name != NULL ? actor->name : "effect");
    if (obj == NULL || transform == NULL || !yyjson_mut_arr_add_val(effects, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "id", effect_id) ||
        !export_add_optional_string(doc, obj, "source_actor", actor->name) ||
        !yyjson_mut_obj_add_val(doc, obj, "transform", transform) ||
        !export_add_vec3(doc, transform, "position", actor->position) ||
        !export_add_vec3(doc, transform, "rotation", actor->rotation) ||
        !export_add_vec3(doc, transform, "scale", actor->scale))
    {
        return false;
    }

    const char *kind = editor_actor_property_string(actor, "effect_kind", NULL);
    if (kind == NULL || kind[0] == '\0')
        kind = editor_actor_property_string(actor, "sensor_profile", "effect");
    if (kind == NULL || kind[0] == '\0')
        kind = "effect";
    const char *type = editor_actor_property_string(actor, "effect_type", NULL);
    if (type == NULL || type[0] == '\0')
        type = editor_effect_default_type(kind);

    slayer3d_color color = actor->color;
    const slayer3d_value *color_value = editor_actor_property(actor, "effect_color");
    if (color_value != NULL && color_value->type == SLAYER3D_VALUE_COLOR)
        color = color_value->as_color;

    if (!yyjson_mut_obj_add_strcpy(doc, obj, "kind", kind) || !yyjson_mut_obj_add_strcpy(doc, obj, "type", type) ||
        !export_add_color(doc, obj, "color", color) ||
        !export_add_optional_actor_property_string(doc, obj, "asset", actor, "effect_asset") ||
        !export_add_optional_actor_property_string(doc, obj, "texture", actor, "effect_texture") ||
        !export_add_optional_actor_property_string(doc, obj, "sprite", actor, "effect_sprite") ||
        !export_add_optional_actor_property_number(doc, obj, "radius", actor, "radius") ||
        !export_add_optional_actor_property_number(doc, obj, "duration", actor, "duration") ||
        !export_add_optional_actor_property_number(doc, obj, "density", actor, "density") ||
        !export_add_optional_actor_property_int(doc, obj, "max_particles", actor, "max_particles") ||
        !export_add_optional_actor_property_bool(doc, obj, "loop", actor, "loop") ||
        !export_add_optional_actor_property_bool(doc, obj, "emissive", actor, "emissive") ||
        !export_add_optional_actor_property_bool(doc, obj, "preview", actor, "preview") ||
        !export_add_properties(doc, obj, "properties", actor->properties))
    {
        return false;
    }
    return true;
}

static bool export_add_map_effects(yyjson_mut_doc *doc, yyjson_mut_val *effects,
                                   const slayer3d_game_data_runtime *runtime)
{
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        const editor_actor_runtime *actor = &runtime->editor_actors[i];
        if (editor_actor_effect_exportable(actor) && !export_add_map_editor_effect(doc, effects, actor))
            return false;
    }
    return true;
}

static bool export_add_map_connections(yyjson_mut_doc *doc, yyjson_mut_val *connections,
                                       const slayer3d_game_data_runtime *runtime)
{
    for (int i = 0; i < runtime->editor_connection_count; ++i)
    {
        if (!export_add_editor_connection(doc, connections, &runtime->editor_connections[i]))
            return false;
    }
    return true;
}

static bool export_add_map_prefabs(yyjson_mut_doc *doc, yyjson_mut_val *prefabs,
                                   const slayer3d_game_data_runtime *runtime)
{
    for (int i = 0; i < runtime->editor_prefab_count; ++i)
    {
        if (!export_add_editor_prefab(doc, prefabs, &runtime->editor_prefabs[i]))
            return false;
    }
    return true;
}

static bool export_editable_level_map_json_with_assets(const slayer3d_game_data_runtime *runtime,
                                                       const char *world_name, char **out_json, size_t *out_size,
                                                       export_asset_copies *copies, char *error_buffer,
                                                       int error_buffer_size)
{
    if (out_json != NULL)
        *out_json = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (runtime == NULL || out_json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "editable level map export requires runtime and output");
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "brush world '%s' not found", world_name);
        return false;
    }

    char *fragment_json = NULL;
    size_t fragment_size = 0u;
    if (!slayer3d_game_data_export_editable_level_fragment_json(runtime, world_name, &fragment_json, &fragment_size,
                                                                error_buffer, error_buffer_size))
    {
        return false;
    }

    yyjson_doc *fragment_doc = yyjson_read(fragment_json, fragment_size, 0);
    yyjson_val *fragment_root = fragment_doc != NULL ? yyjson_doc_get_root(fragment_doc) : NULL;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *metadata = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *materials = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *brushes = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *actors = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *lights = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *effects = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *connections = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *prefabs = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *editor = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *fragment_copy = doc != NULL ? yyjson_val_mut_copy(doc, fragment_root) : NULL;
    if (fragment_doc == NULL || doc == NULL || root == NULL || metadata == NULL || materials == NULL ||
        brushes == NULL || actors == NULL || lights == NULL || effects == NULL || connections == NULL ||
        prefabs == NULL || editor == NULL || fragment_copy == NULL)
    {
        yyjson_doc_free(fragment_doc);
        yyjson_mut_doc_free(doc);
        SDL_free(fragment_json);
        set_error(error_buffer, error_buffer_size, "failed to allocate Slayer3D map export document");
        return false;
    }

    yyjson_mut_doc_set_root(doc, root);
    bool ok =
        yyjson_mut_obj_add_strcpy(doc, root, "format", SLAYER3D_MAP_FORMAT_ID) &&
        yyjson_mut_obj_add_int(doc, root, "version", SLAYER3D_MAP_FORMAT_VERSION) &&
        yyjson_mut_obj_add_val(doc, root, "metadata", metadata) &&
        yyjson_mut_obj_add_strcpy(doc, metadata, "id", world_name != NULL ? world_name : "") &&
        yyjson_mut_obj_add_strcpy(doc, metadata, "name", world_name != NULL ? world_name : "Untitled Map") &&
        yyjson_mut_obj_add_strcpy(doc, root, "coordinate_system", "y_up") &&
        export_add_map_global_state(doc, root, runtime) && yyjson_mut_obj_add_val(doc, root, "materials", materials) &&
        export_add_map_materials(doc, materials, &world_runtime->desc, copies) &&
        yyjson_mut_obj_add_val(doc, root, "brushes", brushes) &&
        (world_runtime->editor_has_source_model
             ? export_add_map_brushes_from_source(doc, brushes, world_runtime, error_buffer, error_buffer_size)
             : export_add_map_brushes(doc, brushes, &world_runtime->desc)) &&
        yyjson_mut_obj_add_val(doc, root, "actors", actors) &&
        export_add_map_player_start_actors(doc, actors, runtime) &&
        export_add_map_editor_actors(doc, actors, runtime) && yyjson_mut_obj_add_val(doc, root, "lights", lights) &&
        export_add_map_lights(doc, lights, runtime) && export_add_map_skybox(doc, root, runtime, copies) &&
        yyjson_mut_obj_add_val(doc, root, "effects", effects) && export_add_map_effects(doc, effects, runtime) &&
        yyjson_mut_obj_add_val(doc, root, "connections", connections) &&
        export_add_map_connections(doc, connections, runtime) &&
        yyjson_mut_obj_add_val(doc, root, "prefabs", prefabs) && export_add_map_prefabs(doc, prefabs, runtime) &&
        yyjson_mut_obj_add_val(doc, root, "editor", editor) &&
        yyjson_mut_obj_add_strcpy(doc, editor, "source_format", "slayer3d.fragment.v0") &&
        yyjson_mut_obj_add_val(doc, editor, "editable_level_fragment", fragment_copy);

    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_doc_free(fragment_doc);
    yyjson_mut_doc_free(doc);
    SDL_free(fragment_json);
    if (json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to write Slayer3D map JSON");
        return false;
    }

    if (!slayer3d_map_validate_json(json, size, NULL, error_buffer, error_buffer_size))
    {
        free(json);
        return false;
    }

    char *copy = (char *)SDL_malloc(size + 1u);
    if (copy == NULL)
    {
        free(json);
        set_error(error_buffer, error_buffer_size, "failed to allocate Slayer3D map JSON");
        return false;
    }
    SDL_memcpy(copy, json, size + 1u);
    free(json);
    *out_json = copy;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

bool slayer3d_game_data_export_editable_level_map_json(const slayer3d_game_data_runtime *runtime,
                                                       const char *world_name, char **out_json, size_t *out_size,
                                                       char *error_buffer, int error_buffer_size)
{
    return export_editable_level_map_json_with_assets(runtime, world_name, out_json, out_size, NULL, error_buffer,
                                                      error_buffer_size);
}

static bool export_copy_asset_bytes(const slayer3d_game_data_runtime *runtime, const char *source, void **out_bytes,
                                    size_t *out_size)
{
    *out_bytes = NULL;
    *out_size = 0u;
    if (SDL_strncmp(source, "asset://", 8u) == 0)
    {
        if (runtime->assets == NULL)
            return false;
        slayer3d_asset_buffer buffer;
        SDL_zero(buffer);
        char error[256];
        error[0] = '\0';
        if (!slayer3d_asset_resolver_read_file(runtime->assets, source, &buffer, error, (int)sizeof(error)))
            return false;
        void *bytes = SDL_malloc(buffer.size > 0u ? buffer.size : 1u);
        if (bytes == NULL)
        {
            slayer3d_asset_buffer_free(&buffer);
            return false;
        }
        SDL_memcpy(bytes, buffer.data, buffer.size);
        *out_bytes = bytes;
        *out_size = buffer.size;
        slayer3d_asset_buffer_free(&buffer);
        return true;
    }
    *out_bytes = SDL_LoadFile(source, out_size);
    return *out_bytes != NULL;
}

/* Copy the referenced asset files next to the saved map so the map's
 * project-relative references resolve for any consumer. Copy failures warn
 * and continue: the map itself saved successfully. */
static int export_materialize_assets(slayer3d_game_data_runtime *runtime, const char *map_path,
                                     const export_asset_copies *copies)
{
    char *map_dir = editor_save_parent_directory(map_path);
    if (map_dir == NULL)
        return 0;

    int copied = 0;
    for (int i = 0; i < copies->count; ++i)
    {
        const export_asset_copy *entry = &copies->entries[i];
        void *bytes = NULL;
        size_t size = 0u;
        char target[1024];
        SDL_snprintf(target, sizeof(target), "%s/%s", map_dir, entry->relative);
        if (!export_copy_asset_bytes(runtime, entry->source, &bytes, &size))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Map save could not read asset '%s' for '%s'", entry->source,
                        entry->relative);
            continue;
        }
        char *target_dir = editor_save_parent_directory(target);
        bool ok = target_dir != NULL && editor_save_make_directory_recursive(target_dir);
        if (ok)
        {
            SDL_IOStream *stream = SDL_IOFromFile(target, "wb");
            ok = stream != NULL && SDL_WriteIO(stream, bytes, size) == size;
            if (stream != NULL)
                SDL_CloseIO(stream);
        }
        if (ok)
            ++copied;
        else
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Map save could not copy asset '%s'", entry->relative);
        SDL_free(target_dir);
        SDL_free(bytes);
    }
    SDL_free(map_dir);
    return copied;
}

bool slayer3d_game_data_save_editable_level_map_file(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                     const char *path, size_t *out_size, char *error_buffer,
                                                     int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0u;

    char *json = NULL;
    size_t size = 0u;
    export_asset_copies *copies = (export_asset_copies *)SDL_calloc(1u, sizeof(export_asset_copies));
    if (copies == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate map save asset list");
        return false;
    }
    if (!export_editable_level_map_json_with_assets(runtime, world_name, &json, &size, copies, error_buffer,
                                                    error_buffer_size))
    {
        SDL_free(copies);
        return false;
    }

    const bool ok = editor_save_bytes_atomic(path, json, size, "Slayer3D map", error_buffer, error_buffer_size);
    SDL_free(json);
    if (!ok)
    {
        SDL_free(copies);
        return false;
    }
    const int copied = export_materialize_assets(runtime, path, copies);
    if (copied > 0)
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Map save copied %d asset file%s next to '%s'", copied,
                    copied == 1 ? "" : "s", path);
    SDL_free(copies);
    if (!slayer3d_game_data_mark_brush_world_saved(runtime, world_name, path, error_buffer, error_buffer_size))
        return false;
    if (!slayer3d_game_data_mark_player_starts_saved(runtime, path, error_buffer, error_buffer_size))
        return false;
    if (runtime != NULL)
    {
        runtime->editor_actor_dirty = false;
        runtime->editor_prefab_dirty = false;
        runtime->editor_connection_dirty = false;
    }
    if (out_size != NULL)
        *out_size = size;
    return true;
}

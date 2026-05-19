/**
 * @file game_data_brush_editor_export.c
 * @brief Brush-world and editable-level JSON export helpers for editor tooling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <stdlib.h>

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

static bool export_add_optional_string(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, const char *value)
{
    return value == NULL || value[0] == '\0' || yyjson_mut_obj_add_strcpy(doc, obj, key, value);
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

static int editor_source_millimeters(float value)
{
    return (int)SDL_lroundf(value * 1000.0f);
}

static const char *brush_first_material_name(const slayer3d_game_data_brush_world *world,
                                             const slayer3d_game_data_brush *brush)
{
    if (brush == NULL || brush->face_count <= 0)
        return "";
    const slayer3d_game_data_brush_face *face = &brush->faces[0];
    if (face->material_name != NULL)
        return face->material_name;
    if (world != NULL && face->material_index >= 0 && face->material_index < world->material_count)
        return world->materials[face->material_index].name != NULL ? world->materials[face->material_index].name : "";
    return "";
}

static const char *brush_face_material_name(const slayer3d_game_data_brush_world *world,
                                            const slayer3d_game_data_brush_face *face)
{
    if (face == NULL)
        return "";
    if (face->material_name != NULL)
        return face->material_name;
    if (world != NULL && face->material_index >= 0 && face->material_index < world->material_count)
        return world->materials[face->material_index].name != NULL ? world->materials[face->material_index].name : "";
    return "";
}

static const char *editor_source_prefab_name(const slayer3d_game_data_brush *brush, const char *material)
{
    if (brush != NULL && brush->editor.prefab != NULL && SDL_strcmp(brush->editor.prefab, "editor.box") != 0)
        return brush->editor.prefab;
    if (material != NULL)
    {
        if (SDL_strstr(material, ".floor") != NULL)
            return "floor";
        if (SDL_strstr(material, ".wall") != NULL)
            return "wall";
        if (SDL_strstr(material, ".ceiling") != NULL)
            return "ceiling";
    }
    return brush != NULL && brush->editor.prefab != NULL ? brush->editor.prefab : "box";
}

static const char *editor_source_prefab_name_for_brush(const slayer3d_game_data_brush_world *world,
                                                       const slayer3d_game_data_brush *brush, const char *fallback)
{
    if (brush != NULL && brush->editor.prefab != NULL && SDL_strcmp(brush->editor.prefab, "editor.box") != 0)
        return brush->editor.prefab;

    int floor_count = 0;
    int wall_count = 0;
    int ceiling_count = 0;
    for (int i = 0; brush != NULL && i < brush->face_count; ++i)
    {
        const char *material = brush_face_material_name(world, &brush->faces[i]);
        if (SDL_strstr(material, ".floor") != NULL)
            floor_count++;
        if (SDL_strstr(material, ".wall") != NULL)
            wall_count++;
        if (SDL_strstr(material, ".ceiling") != NULL)
            ceiling_count++;
    }
    if (floor_count > wall_count && floor_count > ceiling_count)
        return "floor";
    if (ceiling_count > floor_count && ceiling_count > wall_count)
        return "ceiling";
    if (wall_count > 0)
        return "wall";
    return editor_source_prefab_name(brush, fallback);
}

static bool brush_face_matches_box_plane(const slayer3d_game_data_brush_face *face, slayer3d_vec3 normal,
                                         float distance)
{
    return face != NULL && SDL_fabsf(face->normal.x - normal.x) <= 0.0001f &&
           SDL_fabsf(face->normal.y - normal.y) <= 0.0001f && SDL_fabsf(face->normal.z - normal.z) <= 0.0001f &&
           SDL_fabsf(face->distance - distance) <= 0.001f;
}

static bool brush_can_export_as_structural_box(const slayer3d_game_data_brush *brush)
{
    if (brush == NULL || !brush->has_bounds || brush->face_count != 6)
        return false;

    bool found[6] = {false, false, false, false, false, false};
    for (int i = 0; i < brush->face_count; ++i)
    {
        const slayer3d_game_data_brush_face *face = &brush->faces[i];
        if (brush_face_matches_box_plane(face, slayer3d_vec3_make(1.0f, 0.0f, 0.0f), brush->bounds.max.x))
            found[0] = true;
        else if (brush_face_matches_box_plane(face, slayer3d_vec3_make(-1.0f, 0.0f, 0.0f), -brush->bounds.min.x))
            found[1] = true;
        else if (brush_face_matches_box_plane(face, slayer3d_vec3_make(0.0f, 1.0f, 0.0f), brush->bounds.max.y))
            found[2] = true;
        else if (brush_face_matches_box_plane(face, slayer3d_vec3_make(0.0f, -1.0f, 0.0f), -brush->bounds.min.y))
            found[3] = true;
        else if (brush_face_matches_box_plane(face, slayer3d_vec3_make(0.0f, 0.0f, 1.0f), brush->bounds.max.z))
            found[4] = true;
        else if (brush_face_matches_box_plane(face, slayer3d_vec3_make(0.0f, 0.0f, -1.0f), -brush->bounds.min.z))
            found[5] = true;
        else
            return false;
    }
    for (size_t i = 0; i < SDL_arraysize(found); ++i)
    {
        if (!found[i])
            return false;
    }
    return true;
}

static bool export_add_structural_box_source(yyjson_mut_doc *doc, yyjson_mut_val *boxes,
                                             const slayer3d_game_data_brush_world *world,
                                             const slayer3d_game_data_brush *brush)
{
    if (!brush_can_export_as_structural_box(brush))
        return false;

    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || !yyjson_mut_arr_add_val(boxes, obj))
        return false;

    const char *stable_id = brush->editor.stable_id != NULL ? brush->editor.stable_id : brush->name;
    const char *material = brush_first_material_name(world, brush);
    const char *prefab = editor_source_prefab_name_for_brush(world, brush, material);
    const int min_x = editor_source_millimeters(brush->bounds.min.x);
    const int min_y = editor_source_millimeters(brush->bounds.min.y);
    const int min_z = editor_source_millimeters(brush->bounds.min.z);
    const int max_x = editor_source_millimeters(brush->bounds.max.x);
    const int max_y = editor_source_millimeters(brush->bounds.max.y);
    const int max_z = editor_source_millimeters(brush->bounds.max.z);

    return yyjson_mut_obj_add_strcpy(doc, obj, "stable_id", stable_id != NULL ? stable_id : "") &&
           yyjson_mut_obj_add_strcpy(doc, obj, "name", brush->name != NULL ? brush->name : "") &&
           yyjson_mut_obj_add_strcpy(doc, obj, "kind", "box") &&
           yyjson_mut_obj_add_strcpy(doc, obj, "prefab", prefab) &&
           yyjson_mut_obj_add_strcpy(doc, obj, "material", material) &&
           export_add_vec3i_values(doc, obj, "min", min_x, min_y, min_z) &&
           export_add_vec3i_values(doc, obj, "max", max_x, max_y, max_z) &&
           export_add_brush_contents(doc, obj, brush->contents);
}

static bool export_add_editor_brush_source_world(yyjson_mut_doc *doc, yyjson_mut_val *sources,
                                                 const slayer3d_game_data_brush_world *world)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_val *boxes = yyjson_mut_arr(doc);
    if (obj == NULL || boxes == NULL || !yyjson_mut_arr_add_val(sources, obj) ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "world", world->name != NULL ? world->name : "") ||
        !yyjson_mut_obj_add_strcpy(doc, obj, "coordinate_system", "fixed_millimeters") ||
        !yyjson_mut_obj_add_real(doc, obj, "meters_per_unit", 0.001f) ||
        !yyjson_mut_obj_add_val(doc, obj, "boxes", boxes))
    {
        return false;
    }
    for (int i = 0; i < world->brush_count; ++i)
    {
        if (!export_add_structural_box_source(doc, boxes, world, &world->brushes[i]))
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

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *worlds = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *sources = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    yyjson_mut_val *starts = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    if (doc == NULL || root == NULL || worlds == NULL || sources == NULL || starts == NULL)
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
              export_add_editor_brush_source_world(doc, sources, &world_runtime->desc) &&
              yyjson_mut_obj_add_val(doc, root, "editor_player_starts", starts);
    for (int i = 0; ok && i < runtime->editor_player_start_count; ++i)
        ok = export_add_player_start(doc, starts, &runtime->editor_player_starts[i]);

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
    if (out_size != NULL)
        *out_size = size;
    return true;
}

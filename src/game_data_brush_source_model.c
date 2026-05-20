/**
 * @file game_data_brush_source_model.c
 * @brief Runtime-owned structural brush source model helpers for editor tooling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

static int source_millimeters_from_meters(float value)
{
    return (int)SDL_lroundf(value * 1000.0f);
}

static int source_units_from_meters(const brush_world_runtime *world_runtime, float value)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return (int)SDL_lroundf(value / meters_per_unit);
}

static const char *const source_box_face_keys[6] = {"px", "nx", "py", "ny", "pz", "nz"};

static bool source_vec3i(yyjson_val *object, const char *key, int out_values[3])
{
    yyjson_val *value = obj_get(object, key);
    if (out_values == NULL || !yyjson_is_arr(value) || yyjson_arr_size(value) < 3u)
        return false;
    for (int i = 0; i < 3; ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, (size_t)i);
        if (!yyjson_is_int(entry))
            return false;
        out_values[i] = (int)yyjson_get_int(entry);
    }
    return true;
}

static int source_material_index_by_name(const slayer3d_game_data_brush_world *world, const char *material_name)
{
    if (world == NULL || material_name == NULL || material_name[0] == '\0')
        return -1;
    for (int i = 0; i < world->material_count; ++i)
    {
        if (world->materials[i].name != NULL && SDL_strcmp(world->materials[i].name, material_name) == 0)
            return i;
    }
    return -1;
}

static void free_editor_brush_source_box(editor_brush_source_box_runtime *box)
{
    if (box == NULL)
        return;
    SDL_free(box->stable_id);
    SDL_free(box->name);
    SDL_free(box->prefab);
    SDL_free(box->material);
    for (size_t i = 0; i < SDL_arraysize(box->face_materials); ++i)
        SDL_free(box->face_materials[i]);
    SDL_zero(*box);
}

void free_editor_brush_source_model(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
        free_editor_brush_source_box(&world_runtime->editor_source_boxes[i]);
    SDL_free(world_runtime->editor_source_boxes);
    world_runtime->editor_source_boxes = NULL;
    world_runtime->editor_source_box_count = 0;
    world_runtime->editor_source_box_capacity = 0;
    world_runtime->editor_source_meters_per_unit = 0.001f;
    world_runtime->editor_has_source_model = false;
}

static bool copy_source_string(char **field, const char *value)
{
    if (field == NULL)
        return false;
    *field = NULL;
    if (value == NULL)
        return true;
    *field = SDL_strdup(value);
    return *field != NULL;
}

static int source_box_face_index_from_key(const char *key)
{
    for (size_t i = 0; key != NULL && i < SDL_arraysize(source_box_face_keys); ++i)
    {
        if (SDL_strcmp(key, source_box_face_keys[i]) == 0)
            return (int)i;
    }
    return -1;
}

static bool copy_source_face_materials(editor_brush_source_box_runtime *out_box, yyjson_val *face_materials,
                                       char *error_buffer, int error_buffer_size)
{
    if (out_box == NULL || face_materials == NULL)
        return true;
    if (!yyjson_is_obj(face_materials))
    {
        set_error(error_buffer, error_buffer_size, "editor brush source face_materials must be an object");
        return false;
    }

    size_t index = 0;
    size_t max = 0;
    yyjson_val *key = NULL;
    yyjson_val *value = NULL;
    yyjson_obj_foreach(face_materials, index, max, key, value)
    {
        const char *face_key = yyjson_get_str(key);
        const int face_index = source_box_face_index_from_key(face_key);
        if (face_index < 0 || !yyjson_is_str(value) || yyjson_get_str(value) == NULL ||
            yyjson_get_str(value)[0] == '\0')
        {
            set_error(error_buffer, error_buffer_size,
                      "editor brush source face_materials requires px/nx/py/ny/pz/nz non-empty string entries");
            return false;
        }
        if (!copy_source_string(&out_box->face_materials[face_index], yyjson_get_str(value)))
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate editor brush source face material");
            return false;
        }
    }
    return true;
}

static int find_editor_source_box_index_by_name(const brush_world_runtime *world_runtime, const char *brush_name)
{
    if (world_runtime == NULL || brush_name == NULL || brush_name[0] == '\0')
        return -1;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
    {
        const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[i];
        if (box->name != NULL && SDL_strcmp(box->name, brush_name) == 0)
            return i;
    }
    return -1;
}

static bool source_box_extents_valid(const editor_brush_source_box_runtime *box)
{
    return box != NULL && box->min[0] < box->max[0] && box->min[1] < box->max[1] && box->min[2] < box->max[2];
}

static void copy_source_box_coordinates(const editor_brush_source_box_runtime *box, int out_min[3], int out_max[3])
{
    if (box == NULL || out_min == NULL || out_max == NULL)
        return;
    for (int axis = 0; axis < 3; ++axis)
    {
        out_min[axis] = box->min[axis];
        out_max[axis] = box->max[axis];
    }
}

static void restore_source_box_coordinates(editor_brush_source_box_runtime *box, const int min_values[3],
                                           const int max_values[3])
{
    if (box == NULL || min_values == NULL || max_values == NULL)
        return;
    for (int axis = 0; axis < 3; ++axis)
    {
        box->min[axis] = min_values[axis];
        box->max[axis] = max_values[axis];
    }
}

static bool load_editor_brush_source_box(yyjson_val *box, editor_brush_source_box_runtime *out_box, char *error_buffer,
                                         int error_buffer_size)
{
    if (box == NULL || out_box == NULL)
        return false;

    const char *stable_id = json_string(box, "stable_id", NULL);
    const char *name = json_string(box, "name", stable_id);
    const char *prefab = json_string(box, "prefab", "box");
    const char *material = json_string(box, "material", NULL);
    SDL_zero(*out_box);
    if (stable_id == NULL || stable_id[0] == '\0' || name == NULL || name[0] == '\0' || material == NULL ||
        material[0] == '\0' || !source_vec3i(box, "min", out_box->min) || !source_vec3i(box, "max", out_box->max) ||
        out_box->min[0] >= out_box->max[0] || out_box->min[1] >= out_box->max[1] || out_box->min[2] >= out_box->max[2])
    {
        set_errorf(error_buffer, error_buffer_size, "invalid editor brush source box '%s'",
                   stable_id != NULL ? stable_id : "<unnamed>");
        return false;
    }
    if (!copy_source_string(&out_box->stable_id, stable_id) || !copy_source_string(&out_box->name, name) ||
        !copy_source_string(&out_box->prefab, prefab) || !copy_source_string(&out_box->material, material) ||
        !copy_source_face_materials(out_box, obj_get(box, "face_materials"), error_buffer, error_buffer_size))
    {
        free_editor_brush_source_box(out_box);
        if (error_buffer != NULL && error_buffer_size > 0 && error_buffer[0] == '\0')
            set_error(error_buffer, error_buffer_size, "failed to allocate editor brush source box");
        return false;
    }
    out_box->contents = brush_flags_from_json(obj_get(box, "contents"), brush_content_flag_from_string,
                                              SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID);
    return true;
}

bool load_editor_brush_source_boxes(brush_world_runtime *world_runtime, yyjson_val *boxes, float meters_per_unit,
                                    char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || !yyjson_is_arr(boxes))
    {
        set_error(error_buffer, error_buffer_size, "editor brush source requires boxes array");
        return false;
    }

    free_editor_brush_source_model(world_runtime);
    const int box_count = (int)yyjson_arr_size(boxes);
    editor_brush_source_box_runtime *source_boxes =
        box_count > 0 ? (editor_brush_source_box_runtime *)SDL_calloc((size_t)box_count, sizeof(*source_boxes)) : NULL;
    if (box_count > 0 && source_boxes == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate editor brush source boxes");
        return false;
    }

    world_runtime->editor_source_boxes = source_boxes;
    world_runtime->editor_source_box_capacity = box_count;
    world_runtime->editor_source_meters_per_unit = meters_per_unit > 0.0f ? meters_per_unit : 0.001f;
    world_runtime->editor_has_source_model = true;
    for (int i = 0; i < box_count; ++i)
    {
        if (!load_editor_brush_source_box(yyjson_arr_get(boxes, (size_t)i), &source_boxes[i], error_buffer,
                                          error_buffer_size))
        {
            free_editor_brush_source_model(world_runtime);
            return false;
        }
        world_runtime->editor_source_box_count++;
    }
    return true;
}

static bool set_source_brush_metadata_string(const char **field, const char *value)
{
    if (field == NULL)
        return false;
    SDL_free((void *)*field);
    *field = value != NULL ? SDL_strdup(value) : NULL;
    return value == NULL || *field != NULL;
}

static void free_source_runtime_brush(slayer3d_game_data_brush *brush)
{
    if (brush == NULL)
        return;
    SDL_free((void *)brush->name);
    for (int tag_index = 0; tag_index < brush->tag_count; ++tag_index)
        SDL_free((void *)brush->tags[tag_index]);
    SDL_free((void *)brush->tags);
    for (int face_index = 0; face_index < brush->face_count; ++face_index)
    {
        slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[face_index];
        free_editor_metadata(&face->editor);
    }
    SDL_free((void *)brush->faces);
    free_editor_metadata(&brush->editor);
    SDL_zero(*brush);
}

static void free_source_runtime_brushes(slayer3d_game_data_brush *brushes, int brush_count)
{
    if (brushes == NULL)
        return;
    for (int i = 0; i < brush_count; ++i)
        free_source_runtime_brush(&brushes[i]);
    SDL_free(brushes);
}

static void init_source_box_face(slayer3d_game_data_brush_face *face, slayer3d_vec3 normal, float distance,
                                 int material_index, const char *material_name)
{
    SDL_zero(*face);
    face->normal = normal;
    face->distance = distance;
    face->material_index = material_index;
    face->material_name = material_name;
    face->uv_scale[0] = 1.0f;
    face->uv_scale[1] = 1.0f;
}

static bool source_box_to_runtime_brush(const brush_world_runtime *world_runtime,
                                        const editor_brush_source_box_runtime *box, slayer3d_game_data_brush *out_brush,
                                        char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || box == NULL || out_brush == NULL)
        return false;
    const int material_index = source_material_index_by_name(&world_runtime->desc, box->material);
    int face_material_indices[6] = {-1, -1, -1, -1, -1, -1};
    const float meters_per_unit =
        world_runtime->editor_source_meters_per_unit > 0.0f ? world_runtime->editor_source_meters_per_unit : 0.001f;
    const slayer3d_vec3 min =
        slayer3d_vec3_make((float)box->min[0] * meters_per_unit, (float)box->min[1] * meters_per_unit,
                           (float)box->min[2] * meters_per_unit);
    const slayer3d_vec3 max =
        slayer3d_vec3_make((float)box->max[0] * meters_per_unit, (float)box->max[1] * meters_per_unit,
                           (float)box->max[2] * meters_per_unit);
    if (material_index < 0 || !(min.x < max.x && min.y < max.y && min.z < max.z))
    {
        set_errorf(error_buffer, error_buffer_size, "invalid editor brush source box '%s'",
                   box->stable_id != NULL ? box->stable_id : "<unnamed>");
        return false;
    }
    for (size_t i = 0; i < SDL_arraysize(face_material_indices); ++i)
    {
        const char *face_material = box->face_materials[i] != NULL ? box->face_materials[i] : box->material;
        face_material_indices[i] = source_material_index_by_name(&world_runtime->desc, face_material);
        if (face_material_indices[i] < 0)
        {
            set_errorf(error_buffer, error_buffer_size, "invalid editor brush source face material '%s'",
                       face_material != NULL ? face_material : "<null>");
            return false;
        }
    }

    SDL_zero(*out_brush);
    out_brush->name = SDL_strdup(box->name);
    out_brush->contents = box->contents != 0u ? box->contents : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    out_brush->face_count = 6;
    out_brush->faces = (slayer3d_game_data_brush_face *)SDL_calloc(6u, sizeof(*out_brush->faces));
    out_brush->has_bounds = true;
    out_brush->bounds = (slayer3d_bounding_box){min, max};
    if (out_brush->name == NULL || out_brush->faces == NULL ||
        !set_source_brush_metadata_string(&out_brush->editor.stable_id, box->stable_id) ||
        !set_source_brush_metadata_string(&out_brush->editor.prefab, box->prefab))
    {
        free_source_runtime_brush(out_brush);
        set_error(error_buffer, error_buffer_size, "failed to allocate editor source brush");
        return false;
    }

    slayer3d_game_data_brush_face *faces = (slayer3d_game_data_brush_face *)out_brush->faces;
    init_source_box_face(&faces[0], slayer3d_vec3_make(1.0f, 0.0f, 0.0f), max.x, face_material_indices[0],
                         world_runtime->desc.materials[face_material_indices[0]].name);
    init_source_box_face(&faces[1], slayer3d_vec3_make(-1.0f, 0.0f, 0.0f), -min.x, face_material_indices[1],
                         world_runtime->desc.materials[face_material_indices[1]].name);
    init_source_box_face(&faces[2], slayer3d_vec3_make(0.0f, 1.0f, 0.0f), max.y, face_material_indices[2],
                         world_runtime->desc.materials[face_material_indices[2]].name);
    init_source_box_face(&faces[3], slayer3d_vec3_make(0.0f, -1.0f, 0.0f), -min.y, face_material_indices[3],
                         world_runtime->desc.materials[face_material_indices[3]].name);
    init_source_box_face(&faces[4], slayer3d_vec3_make(0.0f, 0.0f, 1.0f), max.z, face_material_indices[4],
                         world_runtime->desc.materials[face_material_indices[4]].name);
    init_source_box_face(&faces[5], slayer3d_vec3_make(0.0f, 0.0f, -1.0f), -min.z, face_material_indices[5],
                         world_runtime->desc.materials[face_material_indices[5]].name);

    for (int i = 0; i < out_brush->face_count; ++i)
    {
        char face_stable_id[320];
        SDL_snprintf(face_stable_id, sizeof(face_stable_id), "%s.face.%s",
                     box->stable_id != NULL ? box->stable_id : box->name, source_box_face_keys[i]);
        if (!set_source_brush_metadata_string(&faces[i].editor.stable_id, face_stable_id))
        {
            free_source_runtime_brush(out_brush);
            set_error(error_buffer, error_buffer_size, "failed to allocate editor source face metadata");
            return false;
        }
    }
    return true;
}

bool editor_brush_world_rebuild_from_source(brush_world_runtime *world_runtime, char *error_buffer,
                                            int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return true;

    const int source_count = world_runtime->editor_source_box_count;
    slayer3d_game_data_brush *new_brushes =
        source_count > 0 ? (slayer3d_game_data_brush *)SDL_calloc((size_t)source_count, sizeof(*new_brushes)) : NULL;
    if (source_count > 0 && new_brushes == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate editor source runtime brushes");
        return false;
    }

    for (int i = 0; i < source_count; ++i)
    {
        if (!source_box_to_runtime_brush(world_runtime, &world_runtime->editor_source_boxes[i], &new_brushes[i],
                                         error_buffer, error_buffer_size))
        {
            free_source_runtime_brushes(new_brushes, source_count);
            return false;
        }
    }

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    slayer3d_game_data_brush *old_brushes = (slayer3d_game_data_brush *)world->brushes;
    const int old_count = world->brush_count;
    world->brushes = new_brushes;
    world->brush_count = source_count;
    if (!rebuild_brush_world_runtime_artifacts(world_runtime, error_buffer, error_buffer_size))
    {
        world->brushes = old_brushes;
        world->brush_count = old_count;
        free_source_runtime_brushes(new_brushes, source_count);
        (void)rebuild_brush_world_runtime_artifacts(world_runtime, NULL, 0);
        return false;
    }

    free_source_runtime_brushes(old_brushes, old_count);
    return true;
}

static bool brush_face_matches_source_box_plane(const slayer3d_game_data_brush_face *face, slayer3d_vec3 normal,
                                                float distance)
{
    return face != NULL && SDL_fabsf(face->normal.x - normal.x) <= 0.0001f &&
           SDL_fabsf(face->normal.y - normal.y) <= 0.0001f && SDL_fabsf(face->normal.z - normal.z) <= 0.0001f &&
           SDL_fabsf(face->distance - distance) <= 0.001f;
}

static bool brush_can_sync_as_source_box(const slayer3d_game_data_brush *brush)
{
    if (brush == NULL || !brush->has_bounds || brush->face_count != 6)
        return false;
    bool found[6] = {false, false, false, false, false, false};
    for (int i = 0; i < brush->face_count; ++i)
    {
        const slayer3d_game_data_brush_face *face = &brush->faces[i];
        if (brush_face_matches_source_box_plane(face, slayer3d_vec3_make(1.0f, 0.0f, 0.0f), brush->bounds.max.x))
            found[0] = true;
        else if (brush_face_matches_source_box_plane(face, slayer3d_vec3_make(-1.0f, 0.0f, 0.0f), -brush->bounds.min.x))
            found[1] = true;
        else if (brush_face_matches_source_box_plane(face, slayer3d_vec3_make(0.0f, 1.0f, 0.0f), brush->bounds.max.y))
            found[2] = true;
        else if (brush_face_matches_source_box_plane(face, slayer3d_vec3_make(0.0f, -1.0f, 0.0f), -brush->bounds.min.y))
            found[3] = true;
        else if (brush_face_matches_source_box_plane(face, slayer3d_vec3_make(0.0f, 0.0f, 1.0f), brush->bounds.max.z))
            found[4] = true;
        else if (brush_face_matches_source_box_plane(face, slayer3d_vec3_make(0.0f, 0.0f, -1.0f), -brush->bounds.min.z))
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

static const char *brush_material_name_for_source_box(const slayer3d_game_data_brush_world *world,
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

static const char *brush_face_material_name_for_source_box(const slayer3d_game_data_brush_world *world,
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

static const char *source_prefab_for_brush(const slayer3d_game_data_brush *brush, const char *material)
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

static bool source_box_from_runtime_brush(const slayer3d_game_data_brush_world *world,
                                          const slayer3d_game_data_brush *brush,
                                          editor_brush_source_box_runtime *out_box)
{
    if (world == NULL || brush == NULL || out_box == NULL || !brush_can_sync_as_source_box(brush))
        return false;
    const char *stable_id = brush->editor.stable_id != NULL ? brush->editor.stable_id : brush->name;
    const char *name = brush->name;
    const char *material = brush_material_name_for_source_box(world, brush);
    const char *prefab = source_prefab_for_brush(brush, material);
    SDL_zero(*out_box);
    if (!copy_source_string(&out_box->stable_id, stable_id != NULL ? stable_id : "") ||
        !copy_source_string(&out_box->name, name != NULL ? name : "") ||
        !copy_source_string(&out_box->prefab, prefab != NULL ? prefab : "box") ||
        !copy_source_string(&out_box->material, material != NULL ? material : ""))
    {
        free_editor_brush_source_box(out_box);
        return false;
    }
    out_box->min[0] = source_millimeters_from_meters(brush->bounds.min.x);
    out_box->min[1] = source_millimeters_from_meters(brush->bounds.min.y);
    out_box->min[2] = source_millimeters_from_meters(brush->bounds.min.z);
    out_box->max[0] = source_millimeters_from_meters(brush->bounds.max.x);
    out_box->max[1] = source_millimeters_from_meters(brush->bounds.max.y);
    out_box->max[2] = source_millimeters_from_meters(brush->bounds.max.z);
    out_box->contents = brush->contents != 0u ? brush->contents : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    for (int i = 0; i < brush->face_count && i < (int)SDL_arraysize(out_box->face_materials); ++i)
    {
        const char *face_material = brush_face_material_name_for_source_box(world, &brush->faces[i]);
        if (face_material != NULL && material != NULL && SDL_strcmp(face_material, material) != 0 &&
            !copy_source_string(&out_box->face_materials[i], face_material))
        {
            free_editor_brush_source_box(out_box);
            return false;
        }
    }
    return true;
}

bool editor_brush_world_sync_source_from_runtime(brush_world_runtime *world_runtime, char *error_buffer,
                                                 int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return true;

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    editor_brush_source_box_runtime *boxes =
        world->brush_count > 0
            ? (editor_brush_source_box_runtime *)SDL_calloc((size_t)world->brush_count, sizeof(*boxes))
            : NULL;
    if (world->brush_count > 0 && boxes == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate editor brush source sync");
        return false;
    }

    for (int i = 0; i < world->brush_count; ++i)
    {
        if (!source_box_from_runtime_brush(world, &world->brushes[i], &boxes[i]))
        {
            for (int j = 0; j <= i; ++j)
                free_editor_brush_source_box(&boxes[j]);
            SDL_free(boxes);
            set_errorf(error_buffer, error_buffer_size, "runtime brush '%s' cannot be represented as a source box",
                       world->brushes[i].name != NULL ? world->brushes[i].name : "<unnamed>");
            return false;
        }
    }

    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
        free_editor_brush_source_box(&world_runtime->editor_source_boxes[i]);
    SDL_free(world_runtime->editor_source_boxes);
    world_runtime->editor_source_boxes = boxes;
    world_runtime->editor_source_box_count = world->brush_count;
    world_runtime->editor_source_box_capacity = world->brush_count;
    world_runtime->editor_source_meters_per_unit = 0.001f;
    world_runtime->editor_has_source_model = true;
    return true;
}

bool editor_brush_world_insert_source_box_from_brush(brush_world_runtime *world_runtime, int box_index,
                                                     const slayer3d_game_data_brush *brush, char *error_buffer,
                                                     int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || brush == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush insertion requires a source model and brush");
        return false;
    }

    editor_brush_source_box_runtime inserted;
    if (!source_box_from_runtime_brush(&world_runtime->desc, brush, &inserted))
    {
        set_errorf(error_buffer, error_buffer_size, "brush '%s' cannot be represented as a source box",
                   brush->name != NULL ? brush->name : "<unnamed>");
        return false;
    }

    const int old_count = world_runtime->editor_source_box_count;
    const int insert_index = SDL_clamp(box_index, 0, old_count);
    editor_brush_source_box_runtime *old_boxes = world_runtime->editor_source_boxes;
    editor_brush_source_box_runtime *new_boxes =
        (editor_brush_source_box_runtime *)SDL_calloc((size_t)old_count + 1u, sizeof(*new_boxes));
    if (new_boxes == NULL)
    {
        free_editor_brush_source_box(&inserted);
        set_error(error_buffer, error_buffer_size, "failed to allocate source brush insertion");
        return false;
    }

    for (int i = 0; i < insert_index; ++i)
        new_boxes[i] = old_boxes[i];
    new_boxes[insert_index] = inserted;
    for (int i = insert_index; i < old_count; ++i)
        new_boxes[i + 1] = old_boxes[i];

    world_runtime->editor_source_boxes = new_boxes;
    world_runtime->editor_source_box_count = old_count + 1;
    world_runtime->editor_source_box_capacity = old_count + 1;
    if (!editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        world_runtime->editor_source_boxes = old_boxes;
        world_runtime->editor_source_box_count = old_count;
        world_runtime->editor_source_box_capacity = old_count;
        free_editor_brush_source_box(&new_boxes[insert_index]);
        SDL_free(new_boxes);
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }

    SDL_free(old_boxes);
    return true;
}

bool editor_brush_world_remove_source_box_at_index(brush_world_runtime *world_runtime, int box_index,
                                                   char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush removal requires a source model");
        return false;
    }
    const int old_count = world_runtime->editor_source_box_count;
    if (box_index < 0 || box_index >= old_count)
    {
        set_error(error_buffer, error_buffer_size, "source brush index out of range");
        return false;
    }

    editor_brush_source_box_runtime *old_boxes = world_runtime->editor_source_boxes;
    editor_brush_source_box_runtime removed = old_boxes[box_index];
    const int new_count = old_count - 1;
    editor_brush_source_box_runtime *new_boxes =
        new_count > 0 ? (editor_brush_source_box_runtime *)SDL_calloc((size_t)new_count, sizeof(*new_boxes)) : NULL;
    if (new_count > 0 && new_boxes == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate source brush removal");
        return false;
    }

    int write_index = 0;
    for (int read_index = 0; read_index < old_count; ++read_index)
    {
        if (read_index != box_index)
            new_boxes[write_index++] = old_boxes[read_index];
    }

    world_runtime->editor_source_boxes = new_boxes;
    world_runtime->editor_source_box_count = new_count;
    world_runtime->editor_source_box_capacity = new_count;
    if (!editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        world_runtime->editor_source_boxes = old_boxes;
        world_runtime->editor_source_box_count = old_count;
        world_runtime->editor_source_box_capacity = old_count;
        SDL_free(new_boxes);
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }

    free_editor_brush_source_box(&removed);
    SDL_free(old_boxes);
    return true;
}

bool editor_brush_world_translate_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                             slayer3d_vec3 offset, char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush translation requires a source model");
        return false;
    }

    const int source_index = find_editor_source_box_index_by_name(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    const int delta[3] = {source_units_from_meters(world_runtime, offset.x),
                          source_units_from_meters(world_runtime, offset.y),
                          source_units_from_meters(world_runtime, offset.z)};
    if (delta[0] == 0 && delta[1] == 0 && delta[2] == 0)
        return true;

    editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    int old_min[3];
    int old_max[3];
    copy_source_box_coordinates(box, old_min, old_max);
    for (int axis = 0; axis < 3; ++axis)
    {
        box->min[axis] += delta[axis];
        box->max[axis] += delta[axis];
    }

    if (!editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        restore_source_box_coordinates(box, old_min, old_max);
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }
    return true;
}

bool editor_brush_world_resize_source_box_face(brush_world_runtime *world_runtime, const char *brush_name,
                                               slayer3d_vec3 face_normal, float distance, char *error_buffer,
                                               int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush resize requires a source model");
        return false;
    }

    const int source_index = find_editor_source_box_index_by_name(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    const int delta = source_units_from_meters(world_runtime, distance);
    if (delta == 0)
        return true;

    int axis = -1;
    int side = 0;
    const slayer3d_vec3 normal = slayer3d_vec3_normalize(face_normal);
    if (SDL_fabsf(normal.x) >= SDL_fabsf(normal.y) && SDL_fabsf(normal.x) >= SDL_fabsf(normal.z))
    {
        axis = 0;
        side = normal.x >= 0.0f ? 1 : -1;
    }
    else if (SDL_fabsf(normal.y) >= SDL_fabsf(normal.z))
    {
        axis = 1;
        side = normal.y >= 0.0f ? 1 : -1;
    }
    else
    {
        axis = 2;
        side = normal.z >= 0.0f ? 1 : -1;
    }

    editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    int old_min[3];
    int old_max[3];
    copy_source_box_coordinates(box, old_min, old_max);
    if (side > 0)
        box->max[axis] += delta;
    else
        box->min[axis] -= delta;

    if (!source_box_extents_valid(box))
    {
        restore_source_box_coordinates(box, old_min, old_max);
        set_error(error_buffer, error_buffer_size, "source brush resize would create invalid geometry");
        return false;
    }

    if (!editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        restore_source_box_coordinates(box, old_min, old_max);
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }
    return true;
}

bool editor_brush_world_set_source_box_face_material(brush_world_runtime *world_runtime, const char *brush_name,
                                                     int face_index, const char *material_name, char *error_buffer,
                                                     int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || material_name == NULL ||
        material_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush paint requires a source model and material");
        return false;
    }
    if (face_index < 0 || face_index >= (int)SDL_arraysize(source_box_face_keys))
    {
        set_error(error_buffer, error_buffer_size, "source brush face index out of range");
        return false;
    }
    if (source_material_index_by_name(&world_runtime->desc, material_name) < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "source brush material '%s' not found", material_name);
        return false;
    }

    const int source_index = find_editor_source_box_index_by_name(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    char *old_face_material = box->face_materials[face_index];
    box->face_materials[face_index] = NULL;
    if (box->material != NULL && SDL_strcmp(box->material, material_name) != 0 &&
        !copy_source_string(&box->face_materials[face_index], material_name))
    {
        box->face_materials[face_index] = old_face_material;
        set_error(error_buffer, error_buffer_size, "failed to allocate source brush face material");
        return false;
    }

    if (!editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        SDL_free(box->face_materials[face_index]);
        box->face_materials[face_index] = old_face_material;
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }

    SDL_free(old_face_material);
    return true;
}

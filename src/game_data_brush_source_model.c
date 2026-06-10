/**
 * @file game_data_brush_source_model.c
 * @brief Runtime-owned structural brush source model helpers for editor tooling.
 */

#include "game_data_brush_source_model_internal.h"

#include <SDL3/SDL_stdinc.h>

#include <limits.h>
#include <stdlib.h>

int editor_brush_source_units_from_meters(const brush_world_runtime *world_runtime, float value)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return (int)SDL_lroundf(value / meters_per_unit);
}

float editor_brush_source_meters_from_units(const brush_world_runtime *world_runtime, int value)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return (float)value * meters_per_unit;
}

const char *const editor_brush_source_box_face_keys[6] = {"px", "nx", "py", "ny", "pz", "nz"};

static const int source_box_face_vertices[6][4] = {
    {1, 2, 6, 5}, {0, 4, 7, 3}, {2, 3, 7, 6}, {0, 1, 5, 4}, {4, 5, 6, 7}, {0, 3, 2, 1},
};

typedef struct source_convex_plane
{
    slayer3d_vec3 normal;
    float distance;
    int vertex_indices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int vertex_count;
    int source_face_index;
} source_convex_plane;

static bool source_box_to_runtime_brush(const brush_world_runtime *world_runtime,
                                        const editor_brush_source_box_runtime *box, slayer3d_game_data_brush *out_brush,
                                        char *error_buffer, int error_buffer_size);

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

static bool source_vec3i_value(yyjson_val *value, int out_values[3])
{
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

static int source_box_face_index_from_key(const char *key)
{
    for (size_t i = 0; key != NULL && i < SDL_arraysize(editor_brush_source_box_face_keys); ++i)
    {
        if (SDL_strcmp(key, editor_brush_source_box_face_keys[i]) == 0)
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

static int find_editor_source_box_index_by_identity(const brush_world_runtime *world_runtime,
                                                    const char *brush_identity)
{
    if (world_runtime == NULL || brush_identity == NULL || brush_identity[0] == '\0')
        return -1;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
    {
        const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[i];
        if ((box->name != NULL && SDL_strcmp(box->name, brush_identity) == 0) ||
            (box->stable_id != NULL && SDL_strcmp(box->stable_id, brush_identity) == 0))
            return i;
    }
    return -1;
}

int editor_brush_world_find_source_box_index(const brush_world_runtime *world_runtime, const char *brush_identity)
{
    return find_editor_source_box_index_by_identity(world_runtime, brush_identity);
}

static bool source_box_face_identity_matches(const editor_brush_source_box_runtime *box, int face_index,
                                             const char *face_identity)
{
    if (box == NULL || face_index < 0 || face_index >= (int)SDL_arraysize(editor_brush_source_box_face_keys) ||
        face_identity == NULL || face_identity[0] == '\0')
    {
        return false;
    }

    const char *face_key = editor_brush_source_box_face_keys[face_index];
    if (SDL_strcmp(face_identity, face_key) == 0)
        return true;

    char stable_id[320];
    if (box->stable_id != NULL && box->stable_id[0] != '\0')
    {
        SDL_snprintf(stable_id, sizeof(stable_id), "%s.face.%s", box->stable_id, face_key);
        if (SDL_strcmp(face_identity, stable_id) == 0)
            return true;
    }
    if (box->name != NULL && box->name[0] != '\0')
    {
        SDL_snprintf(stable_id, sizeof(stable_id), "%s.face.%s", box->name, face_key);
        if (SDL_strcmp(face_identity, stable_id) == 0)
            return true;
    }
    return false;
}

static int source_convex_rebuilt_face_index_for_identity(const editor_brush_source_box_runtime *box,
                                                         const char *face_identity)
{
    if (box == NULL || box->vertex_count <= 0 || face_identity == NULL || face_identity[0] == '\0')
        return -1;
    const char *ids[2] = {box->stable_id, box->name};
    for (size_t i = 0; i < SDL_arraysize(ids); ++i)
    {
        if (ids[i] == NULL || ids[i][0] == '\0')
            continue;
        char prefix[320];
        SDL_snprintf(prefix, sizeof(prefix), "%s.face.rebuilt.", ids[i]);
        const size_t prefix_len = SDL_strlen(prefix);
        if (SDL_strncmp(face_identity, prefix, prefix_len) != 0)
            continue;
        char *end = NULL;
        const long parsed = SDL_strtol(face_identity + prefix_len, &end, 10);
        if (end != NULL && *end == '\0' && parsed >= 0 && parsed < SLAYER3D_EDITOR_SOURCE_CONVEX_FACE_CAPACITY)
            return (int)parsed;
    }
    return -1;
}

static int runtime_brush_face_index_for_identity(const slayer3d_game_data_brush *brush, const char *face_identity,
                                                 int fallback_face_index)
{
    if (brush == NULL)
        return -1;
    if (face_identity != NULL && face_identity[0] != '\0')
    {
        for (int face_index = 0; face_index < brush->face_count; ++face_index)
        {
            const char *stable_id = brush->faces[face_index].editor.stable_id;
            if (stable_id != NULL && SDL_strcmp(stable_id, face_identity) == 0)
                return face_index;
        }
    }
    return fallback_face_index >= 0 && fallback_face_index < brush->face_count ? fallback_face_index : -1;
}

int editor_brush_world_source_box_face_index_for_identity(const brush_world_runtime *world_runtime,
                                                          const char *brush_identity, int fallback_face_index,
                                                          const char *face_identity)
{
    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_identity);
    if (source_index < 0)
        return -1;
    const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    for (int i = 0; i < (int)SDL_arraysize(editor_brush_source_box_face_keys); ++i)
    {
        if (source_box_face_identity_matches(box, i, face_identity))
            return i;
    }
    const int rebuilt_face_index = source_convex_rebuilt_face_index_for_identity(box, face_identity);
    if (rebuilt_face_index >= 0)
        return rebuilt_face_index;
    return fallback_face_index >= 0 && fallback_face_index < (int)SDL_arraysize(editor_brush_source_box_face_keys)
               ? fallback_face_index
               : -1;
}

static slayer3d_vec3 source_box_face_normal_for_index(int face_index)
{
    switch (face_index)
    {
    case 0:
        return slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    case 1:
        return slayer3d_vec3_make(-1.0f, 0.0f, 0.0f);
    case 2:
        return slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    case 3:
        return slayer3d_vec3_make(0.0f, -1.0f, 0.0f);
    case 4:
        return slayer3d_vec3_make(0.0f, 0.0f, 1.0f);
    case 5:
        return slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
    default:
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    }
}

bool editor_brush_world_source_box_face_normal_for_identity(const brush_world_runtime *world_runtime,
                                                            const char *brush_identity, int fallback_face_index,
                                                            const char *face_identity, int *out_face_index,
                                                            slayer3d_vec3 *out_normal)
{
    const int face_index = editor_brush_world_source_box_face_index_for_identity(world_runtime, brush_identity,
                                                                                 fallback_face_index, face_identity);
    if (out_face_index != NULL)
        *out_face_index = face_index;
    if (out_normal != NULL)
        *out_normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (face_index < 0)
        return false;

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_identity);
    if (source_index < 0)
        return false;

    slayer3d_game_data_brush brush;
    if (!source_box_to_runtime_brush(world_runtime, &world_runtime->editor_source_boxes[source_index], &brush, NULL, 0))
    {
        if (out_normal != NULL)
            *out_normal = source_box_face_normal_for_index(face_index);
        return face_index >= 0 && face_index < (int)SDL_arraysize(editor_brush_source_box_face_keys);
    }

    const int runtime_face_index = runtime_brush_face_index_for_identity(&brush, face_identity, face_index);
    const bool ok = runtime_face_index >= 0 && runtime_face_index < brush.face_count;
    if (ok && out_normal != NULL)
        *out_normal = slayer3d_vec3_normalize(brush.faces[runtime_face_index].normal);
    editor_brush_source_free_runtime_brush(&brush);
    return ok;
}

bool editor_brush_world_copy_source_box_by_identity(const brush_world_runtime *world_runtime,
                                                    const char *brush_identity,
                                                    editor_brush_source_box_runtime *out_box, int *out_index,
                                                    char *error_buffer, int error_buffer_size)
{
    if (out_box != NULL)
        SDL_zero(*out_box);
    if (out_index != NULL)
        *out_index = -1;
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush lookup requires a source model");
        return false;
    }
    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_identity);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }
    if (out_box != NULL &&
        !copy_editor_brush_source_box_runtime(&world_runtime->editor_source_boxes[source_index], out_box))
    {
        set_error(error_buffer, error_buffer_size, "failed to copy source brush");
        return false;
    }
    if (out_index != NULL)
        *out_index = source_index;
    return true;
}

static bool source_box_extents_valid(const editor_brush_source_box_runtime *box)
{
    return box != NULL && box->min[0] < box->max[0] && box->min[1] < box->max[1] && box->min[2] < box->max[2];
}

static int source_snap_units(const brush_world_runtime *world_runtime)
{
    return world_runtime != NULL && world_runtime->editor_source_snap_units > 0
               ? world_runtime->editor_source_snap_units
               : 1;
}

static bool source_coord_on_snap(int coord, int snap_units)
{
    return snap_units <= 1 || coord % snap_units == 0;
}

static bool source_box_coordinates_on_snap(const editor_brush_source_box_runtime *box, int snap_units)
{
    if (box == NULL)
        return false;
    for (int vertex = 0; vertex < box->vertex_count; ++vertex)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!source_coord_on_snap(box->vertices[vertex][axis], snap_units))
                return false;
        }
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!source_coord_on_snap(box->min[axis], snap_units) || !source_coord_on_snap(box->max[axis], snap_units))
            return false;
    }
    return true;
}

static void source_box_update_bounds_from_vertices(editor_brush_source_box_runtime *box)
{
    if (box == NULL || box->vertex_count <= 0)
        return;
    for (int axis = 0; axis < 3; ++axis)
    {
        box->min[axis] = box->vertices[0][axis];
        box->max[axis] = box->vertices[0][axis];
    }
    for (int vertex = 1; vertex < box->vertex_count; ++vertex)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            box->min[axis] = SDL_min(box->min[axis], box->vertices[vertex][axis]);
            box->max[axis] = SDL_max(box->max[axis], box->vertices[vertex][axis]);
        }
    }
}

static void translate_source_box_coordinates(editor_brush_source_box_runtime *box, const int delta[3])
{
    if (box == NULL || delta == NULL)
        return;
    for (int axis = 0; axis < 3; ++axis)
    {
        box->min[axis] += delta[axis];
        box->max[axis] += delta[axis];
    }
    for (int vertex = 0; vertex < box->vertex_count; ++vertex)
    {
        for (int axis = 0; axis < 3; ++axis)
            box->vertices[vertex][axis] += delta[axis];
    }
}

static bool source_intervals_overlap_positive(int a_min, int a_max, int b_min, int b_max)
{
    return SDL_max(a_min, b_min) < SDL_min(a_max, b_max);
}

static bool source_box_positive_volume_overlap(const editor_brush_source_box_runtime *a,
                                               const editor_brush_source_box_runtime *b)
{
    return a != NULL && b != NULL && source_intervals_overlap_positive(a->min[0], a->max[0], b->min[0], b->max[0]) &&
           source_intervals_overlap_positive(a->min[1], a->max[1], b->min[1], b->max[1]) &&
           source_intervals_overlap_positive(a->min[2], a->max[2], b->min[2], b->max[2]);
}

bool editor_brush_source_contents_are_structural(unsigned int contents)
{
    if (contents == 0u)
        contents = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    return (contents & (SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP |
                        SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP | SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY)) != 0u;
}

static void source_box_candidate_collect_warnings(const brush_world_runtime *world_runtime,
                                                  const editor_brush_source_box_runtime *candidate, int exclude_index,
                                                  editor_brush_source_prefab_result *out_result)
{
    if (world_runtime == NULL || candidate == NULL || out_result == NULL ||
        !editor_brush_source_contents_are_structural(candidate->contents))
    {
        return;
    }

    const char *first_overlap = NULL;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
    {
        if (i == exclude_index)
            continue;
        const editor_brush_source_box_runtime *existing = &world_runtime->editor_source_boxes[i];
        if (!editor_brush_source_contents_are_structural(existing->contents) ||
            !source_box_positive_volume_overlap(candidate, existing))
        {
            continue;
        }
        out_result->positive_overlap_count++;
        if (first_overlap == NULL)
            first_overlap = existing->name != NULL ? existing->name : existing->stable_id;
    }

    if (out_result->positive_overlap_count <= 0)
        return;

    if (out_result->positive_overlap_count == 1)
    {
        SDL_snprintf(out_result->warning, sizeof(out_result->warning),
                     "overlaps source brush '%s'; compiler will resolve hidden faces",
                     first_overlap != NULL ? first_overlap : "<unnamed>");
    }
    else
    {
        SDL_snprintf(out_result->warning, sizeof(out_result->warning),
                     "overlaps %d source brushes including '%s'; compiler will resolve hidden faces",
                     out_result->positive_overlap_count, first_overlap != NULL ? first_overlap : "<unnamed>");
    }
}

static bool source_optional_string_equal(const char *a, const char *b)
{
    const bool a_empty = a == NULL || a[0] == '\0';
    const bool b_empty = b == NULL || b[0] == '\0';
    if (a_empty || b_empty)
        return a_empty && b_empty;
    return SDL_strcmp(a, b) == 0;
}

static bool source_box_matches_prefab_cell(const editor_brush_source_box_runtime *a,
                                           const editor_brush_source_box_runtime *b)
{
    if (a == NULL || b == NULL || a->contents != b->contents || !source_optional_string_equal(a->prefab, b->prefab) ||
        !source_optional_string_equal(a->material, b->material))
    {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        if (a->min[axis] != b->min[axis] || a->max[axis] != b->max[axis])
            return false;
    }
    for (size_t i = 0; i < SDL_arraysize(a->face_materials); ++i)
    {
        if (!source_optional_string_equal(a->face_materials[i], b->face_materials[i]))
            return false;
    }
    return true;
}

static int find_matching_source_prefab_cell(const brush_world_runtime *world_runtime,
                                            const editor_brush_source_box_runtime *candidate)
{
    if (world_runtime == NULL || candidate == NULL)
        return -1;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
    {
        if (source_box_matches_prefab_cell(candidate, &world_runtime->editor_source_boxes[i]))
            return i;
    }
    return -1;
}

static bool source_box_candidate_valid(const brush_world_runtime *world_runtime,
                                       const editor_brush_source_box_runtime *candidate, int exclude_index,
                                       char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || candidate == NULL)
        return false;
    if (!source_box_extents_valid(candidate))
    {
        set_error(error_buffer, error_buffer_size, "source brush edit would create invalid geometry");
        return false;
    }
    const int snap_units = source_snap_units(world_runtime);
    if (!source_box_coordinates_on_snap(candidate, snap_units))
    {
        set_errorf(error_buffer, error_buffer_size, "source brush '%s' is not aligned to %d-unit source snap",
                   candidate->name != NULL ? candidate->name : "<unnamed>", snap_units);
        return false;
    }

    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
    {
        if (i == exclude_index)
            continue;
        const editor_brush_source_box_runtime *existing = &world_runtime->editor_source_boxes[i];
        if (existing->stable_id != NULL && candidate->stable_id != NULL &&
            SDL_strcmp(existing->stable_id, candidate->stable_id) == 0)
        {
            set_errorf(error_buffer, error_buffer_size, "duplicate editor brush source stable id '%s'",
                       candidate->stable_id);
            return false;
        }
        if (existing->name != NULL && candidate->name != NULL && SDL_strcmp(existing->name, candidate->name) == 0)
        {
            set_errorf(error_buffer, error_buffer_size, "duplicate editor brush source name '%s'", candidate->name);
            return false;
        }
    }
    return true;
}

bool editor_brush_world_validate_source_box_candidate(const brush_world_runtime *world_runtime,
                                                      const editor_brush_source_box_runtime *box, int exclude_index,
                                                      char *error_buffer, int error_buffer_size)
{
    return source_box_candidate_valid(world_runtime, box, exclude_index, error_buffer, error_buffer_size);
}

static bool source_box_create_below_minimum_extent(const editor_brush_source_box_runtime *box, int minimum_extent_units)
{
    if (box == NULL || minimum_extent_units <= 0)
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (box->max[axis] - box->min[axis] < minimum_extent_units)
            return true;
    }
    return false;
}

static void source_box_create_populate_result(const brush_world_runtime *world_runtime,
                                              const editor_brush_source_box_runtime *box,
                                              editor_brush_source_prefab_result *out_result)
{
    if (out_result == NULL || box == NULL)
        return;
    out_result->valid = true;
    SDL_strlcpy(out_result->brush_name,
                box->name != NULL && box->name[0] != '\0' ? box->name : (box->stable_id != NULL ? box->stable_id : ""),
                sizeof(out_result->brush_name));
    out_result->bounds = editor_brush_source_box_bounds_meters(world_runtime, box);
    for (int axis = 0; axis < 3; ++axis)
    {
        out_result->source_min[axis] = box->min[axis];
        out_result->source_max[axis] = box->max[axis];
    }
}

bool editor_brush_world_preview_source_box_create(const brush_world_runtime *world_runtime,
                                                  const editor_brush_source_box_runtime *box, int minimum_extent_units,
                                                  editor_brush_source_prefab_result *out_result, char *error_buffer,
                                                  int error_buffer_size)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || box == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source box creation requires a source-backed brush world");
        return false;
    }
    if (!source_box_extents_valid(box))
    {
        set_error(error_buffer, error_buffer_size, "source brush edit would create invalid geometry");
        return false;
    }
    if (source_box_create_below_minimum_extent(box, minimum_extent_units))
    {
        if (out_result != NULL)
        {
            out_result->valid = true;
            out_result->no_op = true;
            source_box_create_populate_result(world_runtime, box, out_result);
            SDL_strlcpy(out_result->warning, "source box create ignored tiny drag", sizeof(out_result->warning));
        }
        return true;
    }
    if (!source_box_candidate_valid(world_runtime, box, -1, error_buffer, error_buffer_size))
        return false;
    source_box_create_populate_result(world_runtime, box, out_result);
    source_box_candidate_collect_warnings(world_runtime, box, -1, out_result);
    return true;
}

bool editor_brush_world_apply_source_box_create(brush_world_runtime *world_runtime,
                                                const editor_brush_source_box_runtime *box, int minimum_extent_units,
                                                editor_brush_source_prefab_result *out_result, char *error_buffer,
                                                int error_buffer_size)
{
    editor_brush_source_prefab_result preview;
    if (!editor_brush_world_preview_source_box_create(world_runtime, box, minimum_extent_units, &preview, error_buffer,
                                                      error_buffer_size))
    {
        if (out_result != NULL)
            *out_result = preview;
        return false;
    }
    if (preview.no_op)
    {
        if (out_result != NULL)
            *out_result = preview;
        return true;
    }
    if (!editor_brush_world_insert_source_box_at_index(world_runtime, world_runtime->editor_source_box_count, box,
                                                       error_buffer, error_buffer_size))
    {
        return false;
    }
    if (out_result != NULL)
        *out_result = preview;
    return true;
}

static bool source_box_overlaps_on_other_axes(const editor_brush_source_box_runtime *a,
                                              const editor_brush_source_box_runtime *b, int axis)
{
    if (a == NULL || b == NULL || axis < 0 || axis > 2)
        return false;
    for (int other_axis = 0; other_axis < 3; ++other_axis)
    {
        if (other_axis == axis)
            continue;
        if (!source_intervals_overlap_positive(a->min[other_axis], a->max[other_axis], b->min[other_axis],
                                               b->max[other_axis]))
        {
            return false;
        }
    }
    return true;
}

static bool source_box_contact_fully_covers_face(const editor_brush_source_box_runtime *a,
                                                 const editor_brush_source_box_runtime *b, int axis)
{
    for (int other_axis = 0; other_axis < 3; ++other_axis)
    {
        if (other_axis == axis)
            continue;
        if (a->min[other_axis] != b->min[other_axis] || a->max[other_axis] != b->max[other_axis])
            return false;
    }
    return true;
}

static int source_box_contact_axis_count(const editor_brush_source_box_runtime *a,
                                         const editor_brush_source_box_runtime *b)
{
    if (a == NULL || b == NULL)
        return 0;
    int contact_axis_count = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (a->max[axis] == b->min[axis] || b->max[axis] == a->min[axis])
            ++contact_axis_count;
    }
    return contact_axis_count;
}

static int source_box_overlap_axis_count(const editor_brush_source_box_runtime *a,
                                         const editor_brush_source_box_runtime *b)
{
    if (a == NULL || b == NULL)
        return 0;
    int overlap_axis_count = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (source_intervals_overlap_positive(a->min[axis], a->max[axis], b->min[axis], b->max[axis]))
            ++overlap_axis_count;
    }
    return overlap_axis_count;
}

static void set_first_source_diagnostic_issue(slayer3d_game_data_editor_brush_source_diagnostics *diagnostics,
                                              const char *format, const editor_brush_source_box_runtime *a,
                                              const editor_brush_source_box_runtime *b, int value)
{
    if (diagnostics == NULL || diagnostics->first_issue[0] != '\0')
        return;
    SDL_snprintf(diagnostics->first_issue, sizeof(diagnostics->first_issue), format,
                 a != NULL && a->name != NULL ? a->name : "<unnamed>",
                 b != NULL && b->name != NULL ? b->name : "<unnamed>", value);
}

static void set_first_source_diagnostic_context(slayer3d_game_data_editor_brush_source_diagnostics *diagnostics,
                                                const char *kind, const editor_brush_source_box_runtime *source,
                                                const editor_brush_source_box_runtime *related_source,
                                                const char *source_face, const slayer3d_game_data_brush *runtime_brush,
                                                int runtime_brush_index, int compiled_face_index)
{
    if (diagnostics == NULL || diagnostics->first_issue_kind[0] != '\0')
        return;
    SDL_strlcpy(diagnostics->first_issue_kind, kind != NULL ? kind : "", sizeof(diagnostics->first_issue_kind));
    if (source != NULL)
    {
        SDL_strlcpy(diagnostics->first_issue_source_name, source->name != NULL ? source->name : "",
                    sizeof(diagnostics->first_issue_source_name));
        SDL_strlcpy(diagnostics->first_issue_source_stable_id, source->stable_id != NULL ? source->stable_id : "",
                    sizeof(diagnostics->first_issue_source_stable_id));
    }
    if (related_source != NULL)
    {
        SDL_strlcpy(diagnostics->first_issue_related_source_name,
                    related_source->name != NULL ? related_source->name : "",
                    sizeof(diagnostics->first_issue_related_source_name));
        SDL_strlcpy(diagnostics->first_issue_related_source_stable_id,
                    related_source->stable_id != NULL ? related_source->stable_id : "",
                    sizeof(diagnostics->first_issue_related_source_stable_id));
    }
    SDL_strlcpy(diagnostics->first_issue_source_face, source_face != NULL ? source_face : "",
                sizeof(diagnostics->first_issue_source_face));
    if (runtime_brush != NULL)
    {
        SDL_strlcpy(diagnostics->first_issue_runtime_brush_name, runtime_brush->name != NULL ? runtime_brush->name : "",
                    sizeof(diagnostics->first_issue_runtime_brush_name));
    }
    diagnostics->first_issue_runtime_brush_index = runtime_brush_index;
    diagnostics->first_issue_compiled_face_index = compiled_face_index;
}

static void set_first_source_diagnostic_text(slayer3d_game_data_editor_brush_source_diagnostics *diagnostics,
                                             const char *text)
{
    if (diagnostics == NULL || diagnostics->first_issue[0] != '\0' || text == NULL)
        return;
    SDL_strlcpy(diagnostics->first_issue, text, sizeof(diagnostics->first_issue));
}

static int find_runtime_brush_index_by_source_identity(const slayer3d_game_data_brush_world *world,
                                                       const char *brush_identity)
{
    if (world == NULL || brush_identity == NULL || brush_identity[0] == '\0')
        return -1;
    for (int i = 0; i < world->brush_count; ++i)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[i];
        if ((brush->name != NULL && SDL_strcmp(brush->name, brush_identity) == 0) ||
            (brush->editor.stable_id != NULL && SDL_strcmp(brush->editor.stable_id, brush_identity) == 0))
        {
            return i;
        }
    }
    return -1;
}

static bool runtime_brush_matches_source_box(const brush_world_runtime *world_runtime,
                                             const slayer3d_game_data_brush *brush,
                                             const editor_brush_source_box_runtime *box)
{
    if (world_runtime == NULL || brush == NULL || box == NULL || !brush->has_bounds)
        return false;
    const int min_values[3] = {editor_brush_source_units_from_meters(world_runtime, brush->bounds.min.x),
                               editor_brush_source_units_from_meters(world_runtime, brush->bounds.min.y),
                               editor_brush_source_units_from_meters(world_runtime, brush->bounds.min.z)};
    const int max_values[3] = {editor_brush_source_units_from_meters(world_runtime, brush->bounds.max.x),
                               editor_brush_source_units_from_meters(world_runtime, brush->bounds.max.y),
                               editor_brush_source_units_from_meters(world_runtime, brush->bounds.max.z)};
    for (int axis = 0; axis < 3; ++axis)
    {
        if (min_values[axis] != box->min[axis] || max_values[axis] != box->max[axis])
            return false;
    }
    if (box->stable_id != NULL && box->stable_id[0] != '\0' &&
        (brush->editor.stable_id == NULL || SDL_strcmp(brush->editor.stable_id, box->stable_id) != 0))
    {
        return false;
    }
    return true;
}

static void validate_source_runtime_brush_identity(const brush_world_runtime *world_runtime,
                                                   slayer3d_game_data_editor_brush_source_diagnostics *diagnostics)
{
    if (world_runtime == NULL || diagnostics == NULL)
        return;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    diagnostics->runtime_brush_count = world->brush_count;

    for (int source_index = 0; source_index < world_runtime->editor_source_box_count; ++source_index)
    {
        const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
        const char *identity = box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;
        const int brush_index = find_runtime_brush_index_by_source_identity(world, identity);
        if (brush_index < 0 || !runtime_brush_matches_source_box(world_runtime, &world->brushes[brush_index], box))
        {
            char message[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
            SDL_snprintf(message, sizeof(message), "source box '%s' has no matching compiled runtime brush",
                         box->name != NULL ? box->name : "<unnamed>");
            diagnostics->runtime_source_mismatch_count++;
            set_first_source_diagnostic_text(diagnostics, message);
            set_first_source_diagnostic_context(diagnostics, "runtime_source_mismatch", box, NULL, NULL, NULL, -1, -1);
        }
    }

    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        const char *identity = brush->editor.stable_id != NULL && brush->editor.stable_id[0] != '\0'
                                   ? brush->editor.stable_id
                                   : brush->name;
        const int source_index = find_editor_source_box_index_by_identity(world_runtime, identity);
        if (source_index < 0 ||
            !runtime_brush_matches_source_box(world_runtime, brush, &world_runtime->editor_source_boxes[source_index]))
        {
            char message[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
            SDL_snprintf(message, sizeof(message), "runtime brush '%s' has no matching source box",
                         brush->name != NULL ? brush->name : "<unnamed>");
            diagnostics->runtime_source_mismatch_count++;
            set_first_source_diagnostic_text(diagnostics, message);
            set_first_source_diagnostic_context(diagnostics, "runtime_source_mismatch", NULL, NULL, NULL, brush,
                                                brush_index, -1);
        }
    }
}

static void validate_source_compiled_face_identity(const brush_world_runtime *world_runtime,
                                                   slayer3d_game_data_editor_brush_source_diagnostics *diagnostics)
{
    if (world_runtime == NULL || diagnostics == NULL)
        return;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    diagnostics->compiled_face_count = world->compile_rendered_face_metadata_count;
    if (world->compile_rendered_face_metadata_count > 0 && world->compile_rendered_faces == NULL)
    {
        diagnostics->compiled_face_missing_source_count += world->compile_rendered_face_metadata_count;
        set_first_source_diagnostic_text(diagnostics, "compiled render-face metadata is missing");
        set_first_source_diagnostic_context(diagnostics, "compiled_missing_source", NULL, NULL, NULL, NULL, -1, -1);
        return;
    }
    for (int face_index = 0; face_index < world->compile_rendered_face_metadata_count; ++face_index)
    {
        const slayer3d_game_data_brush_compiled_face *compiled_face = &world->compile_rendered_faces[face_index];
        if (compiled_face->source_brush_stable_id == NULL || compiled_face->source_brush_stable_id[0] == '\0' ||
            compiled_face->source_face_stable_id == NULL || compiled_face->source_face_stable_id[0] == '\0')
        {
            diagnostics->compiled_face_missing_source_count++;
            if (diagnostics->first_issue[0] == '\0')
            {
                SDL_snprintf(diagnostics->first_issue, sizeof(diagnostics->first_issue),
                             "compiled face %d is missing source identity", face_index);
            }
            set_first_source_diagnostic_context(diagnostics, "compiled_missing_source", NULL, NULL, NULL, NULL, -1,
                                                face_index);
            continue;
        }

        const int source_index =
            find_editor_source_box_index_by_identity(world_runtime, compiled_face->source_brush_stable_id);
        const int source_face_index = source_index >= 0 ? editor_brush_world_source_box_face_index_for_identity(
                                                              world_runtime, compiled_face->source_brush_stable_id, -1,
                                                              compiled_face->source_face_stable_id)
                                                        : -1;
        const bool brush_index_valid =
            compiled_face->brush_index >= 0 && compiled_face->brush_index < world->brush_count;
        const bool face_index_valid = brush_index_valid && compiled_face->face_index >= 0 &&
                                      compiled_face->face_index < world->brushes[compiled_face->brush_index].face_count;
        const slayer3d_game_data_brush *brush = brush_index_valid ? &world->brushes[compiled_face->brush_index] : NULL;
        const slayer3d_game_data_brush_face *face = face_index_valid ? &brush->faces[compiled_face->face_index] : NULL;
        const bool brush_identity_matches =
            brush != NULL && brush->editor.stable_id != NULL &&
            SDL_strcmp(brush->editor.stable_id, compiled_face->source_brush_stable_id) == 0;
        const bool face_identity_matches =
            face != NULL && face->editor.stable_id != NULL &&
            SDL_strcmp(face->editor.stable_id, compiled_face->source_face_stable_id) == 0;
        if (source_index < 0 || source_face_index < 0 || !brush_index_valid || !face_index_valid ||
            !brush_identity_matches || !face_identity_matches)
        {
            diagnostics->compiled_face_unknown_source_count++;
            if (diagnostics->first_issue[0] == '\0')
            {
                SDL_snprintf(diagnostics->first_issue, sizeof(diagnostics->first_issue),
                             "compiled face %d source identity does not resolve", face_index);
            }
            const editor_brush_source_box_runtime *source_box =
                source_index >= 0 ? &world_runtime->editor_source_boxes[source_index] : NULL;
            set_first_source_diagnostic_context(diagnostics, "compiled_unknown_source", source_box, NULL,
                                                compiled_face->source_face_stable_id, brush, compiled_face->brush_index,
                                                face_index);
        }
    }
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

static int normalized_quarter_turns(int quarter_turns)
{
    int normalized = quarter_turns % 4;
    if (normalized < 0)
        normalized += 4;
    return normalized;
}

static void rotate_source_box_face_materials_y_once(editor_brush_source_box_runtime *box)
{
    if (box == NULL)
        return;

    char *old_px = box->face_materials[0];
    char *old_nx = box->face_materials[1];
    char *old_pz = box->face_materials[4];
    char *old_nz = box->face_materials[5];

    box->face_materials[5] = old_px;
    box->face_materials[4] = old_nx;
    box->face_materials[0] = old_pz;
    box->face_materials[1] = old_nz;
}

static bool rotate_source_coord_pair_y(int x, int z, int center_x2, int center_z2, int quarter_turns, int *out_x,
                                       int *out_z)
{
    if (out_x == NULL || out_z == NULL)
        return false;

    int dx2 = 2 * x - center_x2;
    int dz2 = 2 * z - center_z2;
    for (int turn = 0; turn < quarter_turns; ++turn)
    {
        const int next_dx2 = dz2;
        const int next_dz2 = -dx2;
        dx2 = next_dx2;
        dz2 = next_dz2;
    }

    const int rotated_x2 = center_x2 + dx2;
    const int rotated_z2 = center_z2 + dz2;
    if ((rotated_x2 % 2) != 0 || (rotated_z2 % 2) != 0)
        return false;

    *out_x = rotated_x2 / 2;
    *out_z = rotated_z2 / 2;
    return true;
}

static bool load_editor_brush_source_vertices(yyjson_val *vertices, editor_brush_source_box_runtime *out_box,
                                              char *error_buffer, int error_buffer_size)
{
    if (out_box == NULL)
        return false;
    if (vertices == NULL)
        return true;
    if (!yyjson_is_arr(vertices))
    {
        set_error(error_buffer, error_buffer_size, "editor brush source convex vertices must be an array");
        return false;
    }
    const int vertex_count = (int)yyjson_arr_size(vertices);
    if (vertex_count < 4 || vertex_count > SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
    {
        set_error(error_buffer, error_buffer_size,
                  "editor brush source convex vertices must contain 4..16 integer vec3 entries");
        return false;
    }
    for (int i = 0; i < vertex_count; ++i)
    {
        if (!source_vec3i_value(yyjson_arr_get(vertices, (size_t)i), out_box->vertices[i]))
        {
            set_error(error_buffer, error_buffer_size, "editor brush source convex vertices must be integer vec3s");
            return false;
        }
    }
    out_box->vertex_count = vertex_count;
    source_box_update_bounds_from_vertices(out_box);
    return true;
}

static bool load_editor_brush_source_box(yyjson_val *box, editor_brush_source_box_runtime *out_box, char *error_buffer,
                                         int error_buffer_size)
{
    if (box == NULL || out_box == NULL)
        return false;

    const char *stable_id = json_string(box, "stable_id", NULL);
    const char *name = json_string(box, "name", stable_id);
    const char *kind = json_string(box, "kind", "box");
    const bool is_convex = kind != NULL && SDL_strcmp(kind, "convex") == 0;
    const char *prefab = json_string(box, "prefab", is_convex ? "convex" : "box");
    const char *material = json_string(box, "material", NULL);
    SDL_zero(*out_box);
    if (stable_id == NULL || stable_id[0] == '\0' || name == NULL || name[0] == '\0' || material == NULL ||
        material[0] == '\0' || !(SDL_strcmp(kind, "box") == 0 || is_convex))
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
    if (is_convex)
    {
        if (!load_editor_brush_source_vertices(obj_get(box, "vertices"), out_box, error_buffer, error_buffer_size))
        {
            free_editor_brush_source_box(out_box);
            return false;
        }
    }
    else if (!source_vec3i(box, "min", out_box->min) || !source_vec3i(box, "max", out_box->max))
    {
        free_editor_brush_source_box(out_box);
        set_errorf(error_buffer, error_buffer_size, "invalid editor brush source box '%s'",
                   stable_id != NULL ? stable_id : "<unnamed>");
        return false;
    }
    if (out_box->min[0] >= out_box->max[0] || out_box->min[1] >= out_box->max[1] || out_box->min[2] >= out_box->max[2])
    {
        free_editor_brush_source_box(out_box);
        set_errorf(error_buffer, error_buffer_size, "invalid editor brush source box '%s'",
                   stable_id != NULL ? stable_id : "<unnamed>");
        return false;
    }
    out_box->contents = brush_flags_from_json(obj_get(box, "contents"), brush_content_flag_from_string,
                                              SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID);
    return true;
}

static bool editor_source_box_identifier_unique(const editor_brush_source_box_runtime *boxes, int box_count,
                                                const editor_brush_source_box_runtime *candidate, char *error_buffer,
                                                int error_buffer_size)
{
    if (boxes == NULL || box_count <= 0 || candidate == NULL)
        return true;
    for (int i = 0; i < box_count; ++i)
    {
        const editor_brush_source_box_runtime *box = &boxes[i];
        if (box->stable_id != NULL && candidate->stable_id != NULL &&
            SDL_strcmp(box->stable_id, candidate->stable_id) == 0)
        {
            set_errorf(error_buffer, error_buffer_size, "duplicate editor brush source stable id '%s'",
                       candidate->stable_id);
            return false;
        }
        if (box->name != NULL && candidate->name != NULL && SDL_strcmp(box->name, candidate->name) == 0)
        {
            set_errorf(error_buffer, error_buffer_size, "duplicate editor brush source name '%s'", candidate->name);
            return false;
        }
    }
    return true;
}

bool load_editor_brush_source_boxes(brush_world_runtime *world_runtime, yyjson_val *boxes, float meters_per_unit,
                                    int snap_units, char *error_buffer, int error_buffer_size)
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
    world_runtime->editor_source_snap_units = snap_units > 0 ? snap_units : 1;
    world_runtime->editor_has_source_model = true;
    for (int i = 0; i < box_count; ++i)
    {
        if (!load_editor_brush_source_box(yyjson_arr_get(boxes, (size_t)i), &source_boxes[i], error_buffer,
                                          error_buffer_size))
        {
            free_editor_brush_source_model(world_runtime);
            return false;
        }
        if (!editor_source_box_identifier_unique(source_boxes, world_runtime->editor_source_box_count, &source_boxes[i],
                                                 error_buffer, error_buffer_size))
        {
            free_editor_brush_source_box(&source_boxes[i]);
            free_editor_brush_source_model(world_runtime);
            return false;
        }
        if (!source_box_candidate_valid(world_runtime, &source_boxes[i], -1, error_buffer, error_buffer_size))
        {
            free_editor_brush_source_box(&source_boxes[i]);
            free_editor_brush_source_model(world_runtime);
            return false;
        }
        world_runtime->editor_source_box_count++;
    }
    return true;
}

bool slayer3d_game_data_validate_editor_brush_source_model(
    const slayer3d_game_data_runtime *runtime, const char *world_name, int near_gap_units,
    slayer3d_game_data_editor_brush_source_diagnostics *out_diagnostics, char *error_buffer, int error_buffer_size)
{
    if (out_diagnostics != NULL)
    {
        SDL_zero(*out_diagnostics);
        out_diagnostics->first_issue_runtime_brush_index = -1;
        out_diagnostics->first_issue_compiled_face_index = -1;
    }
    if (out_diagnostics == NULL)
    {
        set_error(error_buffer, error_buffer_size, "editor brush source diagnostics output is required");
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush world not found");
        return false;
    }
    out_diagnostics->has_source_model = world_runtime->editor_has_source_model;
    if (!world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "brush world has no editor brush source model");
        return false;
    }

    const int gap_tolerance = near_gap_units > 0 ? near_gap_units : 1;
    const int snap_units = source_snap_units(world_runtime);
    out_diagnostics->source_snap_units = snap_units;
    out_diagnostics->source_box_count = world_runtime->editor_source_box_count;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
    {
        const editor_brush_source_box_runtime *a = &world_runtime->editor_source_boxes[i];
        if (!source_box_coordinates_on_snap(a, snap_units))
        {
            out_diagnostics->off_snap_count++;
            if (out_diagnostics->first_issue[0] == '\0')
            {
                SDL_snprintf(out_diagnostics->first_issue, sizeof(out_diagnostics->first_issue),
                             "source box '%s' is not aligned to %d-unit source snap",
                             a->name != NULL ? a->name : "<unnamed>", snap_units);
            }
            set_first_source_diagnostic_context(out_diagnostics, "off_snap", a, NULL, NULL, NULL, -1, -1);
        }
        for (int j = i + 1; j < world_runtime->editor_source_box_count; ++j)
        {
            const editor_brush_source_box_runtime *b = &world_runtime->editor_source_boxes[j];
            if (!editor_brush_source_contents_are_structural(a->contents) ||
                !editor_brush_source_contents_are_structural(b->contents))
                continue;

            const int contact_axis_count = source_box_contact_axis_count(a, b);
            const int overlap_axis_count = source_box_overlap_axis_count(a, b);
            if (source_box_positive_volume_overlap(a, b))
            {
                out_diagnostics->positive_overlap_count++;
                set_first_source_diagnostic_issue(out_diagnostics,
                                                  "source boxes '%s' and '%s' overlap with positive volume", a, b, 0);
                set_first_source_diagnostic_context(out_diagnostics, "overlap", a, b, NULL, NULL, -1, -1);
                continue;
            }

            if (contact_axis_count == 2 && overlap_axis_count == 1)
            {
                out_diagnostics->edge_contact_count++;
                continue;
            }
            if (contact_axis_count == 3)
            {
                out_diagnostics->vertex_contact_count++;
                continue;
            }

            for (int axis = 0; axis < 3; ++axis)
            {
                if (!source_box_overlaps_on_other_axes(a, b, axis))
                    continue;

                int gap = 0;
                if (a->max[axis] <= b->min[axis])
                    gap = b->min[axis] - a->max[axis];
                else if (b->max[axis] <= a->min[axis])
                    gap = a->min[axis] - b->max[axis];

                if (gap == 0)
                {
                    out_diagnostics->face_contact_count++;
                    if (!source_box_contact_fully_covers_face(a, b, axis))
                        out_diagnostics->partial_face_contact_count++;
                }
                else if (gap > 0 && gap <= gap_tolerance)
                {
                    out_diagnostics->near_gap_count++;
                    set_first_source_diagnostic_issue(out_diagnostics,
                                                      "source boxes '%s' and '%s' have a %d-unit near gap", a, b, gap);
                    set_first_source_diagnostic_context(out_diagnostics, "near_gap", a, b, NULL, NULL, -1, -1);
                }
            }
        }
    }

    validate_source_runtime_brush_identity(world_runtime, out_diagnostics);
    validate_source_compiled_face_identity(world_runtime, out_diagnostics);
    out_diagnostics->structurally_valid = out_diagnostics->off_snap_count == 0 &&
                                          out_diagnostics->runtime_source_mismatch_count == 0 &&
                                          out_diagnostics->compiled_face_missing_source_count == 0 &&
                                          out_diagnostics->compiled_face_unknown_source_count == 0;
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

void editor_brush_source_free_runtime_brush(slayer3d_game_data_brush *brush)
{
    free_source_runtime_brush(brush);
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
    if (box->vertex_count > 0)
    {
        return editor_brush_world_build_source_convex_brush_from_vertices(
            world_runtime, box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name,
            &box->vertices[0][0], box->vertex_count, out_brush, error_buffer, error_buffer_size);
    }
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
                     box->stable_id != NULL ? box->stable_id : box->name, editor_brush_source_box_face_keys[i]);
        if (!set_source_brush_metadata_string(&faces[i].editor.stable_id, face_stable_id))
        {
            free_source_runtime_brush(out_brush);
            set_error(error_buffer, error_buffer_size, "failed to allocate editor source face metadata");
            return false;
        }
    }
    return true;
}

static bool source_convex_vertex_on_snap(const brush_world_runtime *world_runtime, const int vertex[3])
{
    const int snap = source_snap_units(world_runtime);
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!source_coord_on_snap(vertex[axis], snap))
            return false;
    }
    return true;
}

static slayer3d_vec3 source_convex_vertex_meters(const brush_world_runtime *world_runtime, const int vertex[3])
{
    return slayer3d_vec3_make(editor_brush_source_meters_from_units(world_runtime, vertex[0]),
                              editor_brush_source_meters_from_units(world_runtime, vertex[1]),
                              editor_brush_source_meters_from_units(world_runtime, vertex[2]));
}

static const int *source_convex_vertex_at(const int *vertices, int vertex)
{
    return &vertices[vertex * 3];
}

static bool source_convex_vertices_duplicate(const int *vertices, int vertex_count)
{
    for (int a = 0; a < vertex_count; ++a)
    {
        for (int b = a + 1; b < vertex_count; ++b)
        {
            const int *a_coord = source_convex_vertex_at(vertices, a);
            const int *b_coord = source_convex_vertex_at(vertices, b);
            if (a_coord[0] == b_coord[0] && a_coord[1] == b_coord[1] && a_coord[2] == b_coord[2])
            {
                return true;
            }
        }
    }
    return false;
}

static void source_convex_bounds_meters(const brush_world_runtime *world_runtime, const int *vertices, int vertex_count,
                                        slayer3d_bounding_box *out_bounds)
{
    if (out_bounds == NULL)
        return;
    out_bounds->min = source_convex_vertex_meters(world_runtime, source_convex_vertex_at(vertices, 0));
    out_bounds->max = out_bounds->min;
    for (int vertex = 1; vertex < vertex_count; ++vertex)
    {
        const slayer3d_vec3 point =
            source_convex_vertex_meters(world_runtime, source_convex_vertex_at(vertices, vertex));
        out_bounds->min.x = SDL_min(out_bounds->min.x, point.x);
        out_bounds->min.y = SDL_min(out_bounds->min.y, point.y);
        out_bounds->min.z = SDL_min(out_bounds->min.z, point.z);
        out_bounds->max.x = SDL_max(out_bounds->max.x, point.x);
        out_bounds->max.y = SDL_max(out_bounds->max.y, point.y);
        out_bounds->max.z = SDL_max(out_bounds->max.z, point.z);
    }
}

static bool source_convex_plane_matches(const source_convex_plane *a, slayer3d_vec3 normal, float distance)
{
    return a != NULL && SDL_fabsf(slayer3d_vec3_dot(a->normal, normal) - 1.0f) <= 0.0005f &&
           SDL_fabsf(a->distance - distance) <= 0.001f;
}

static bool source_convex_plane_recorded(const source_convex_plane *planes, int plane_count, slayer3d_vec3 normal,
                                         float distance)
{
    for (int i = 0; i < plane_count; ++i)
    {
        if (source_convex_plane_matches(&planes[i], normal, distance))
            return true;
    }
    return false;
}

static bool source_convex_face_contains_vertex(int face_index, int vertex_index)
{
    if (face_index < 0 || face_index >= (int)SDL_arraysize(source_box_face_vertices))
        return false;
    for (int i = 0; i < 4; ++i)
    {
        if (source_box_face_vertices[face_index][i] == vertex_index)
            return true;
    }
    return false;
}

static int source_convex_plane_source_face(const source_convex_plane *plane)
{
    if (plane == NULL || plane->vertex_count < 3)
        return -1;
    for (int face = 0; face < (int)SDL_arraysize(source_box_face_vertices); ++face)
    {
        bool all_on_face = true;
        for (int i = 0; i < plane->vertex_count; ++i)
        {
            if (!source_convex_face_contains_vertex(face, plane->vertex_indices[i]))
            {
                all_on_face = false;
                break;
            }
        }
        if (all_on_face)
            return face;
    }
    return -1;
}

static bool source_convex_add_plane(source_convex_plane *planes, int *plane_count, int plane_capacity,
                                    const slayer3d_vec3 points[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY],
                                    int vertex_count, int a, int b, int c)
{
    const slayer3d_vec3 ab = slayer3d_vec3_sub(points[b], points[a]);
    const slayer3d_vec3 ac = slayer3d_vec3_sub(points[c], points[a]);
    slayer3d_vec3 normal = slayer3d_vec3_cross(ab, ac);
    if (slayer3d_vec3_length_squared(normal) <= 0.000001f)
        return true;
    normal = slayer3d_vec3_normalize(normal);
    float distance = slayer3d_vec3_dot(normal, points[a]);

    int positive = 0;
    int negative = 0;
    for (int vertex = 0; vertex < vertex_count; ++vertex)
    {
        const float signed_distance = slayer3d_vec3_dot(normal, points[vertex]) - distance;
        if (signed_distance > 0.0005f)
            ++positive;
        else if (signed_distance < -0.0005f)
            ++negative;
    }
    if (positive > 0 && negative > 0)
        return true;
    if (positive > 0)
    {
        normal = slayer3d_vec3_scale(normal, -1.0f);
        distance = -distance;
    }
    if (source_convex_plane_recorded(planes, *plane_count, normal, distance))
        return true;
    if (*plane_count >= plane_capacity)
        return false;

    source_convex_plane *plane = &planes[*plane_count];
    SDL_zero(*plane);
    plane->normal = normal;
    plane->distance = distance;
    for (int vertex = 0; vertex < vertex_count; ++vertex)
    {
        if (SDL_fabsf(slayer3d_vec3_dot(normal, points[vertex]) - distance) <= 0.001f)
            plane->vertex_indices[plane->vertex_count++] = vertex;
    }
    if (plane->vertex_count < 3)
        return true;
    plane->source_face_index = source_convex_plane_source_face(plane);
    ++(*plane_count);
    return true;
}

static bool source_convex_build_planes(const brush_world_runtime *world_runtime, const int *vertices, int vertex_count,
                                       source_convex_plane *planes, int plane_capacity, int *out_plane_count,
                                       char *error_buffer, int error_buffer_size)
{
    if (out_plane_count != NULL)
        *out_plane_count = 0;
    if (world_runtime == NULL || vertices == NULL || planes == NULL || out_plane_count == NULL || vertex_count < 4 ||
        vertex_count > SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
    {
        set_error(error_buffer, error_buffer_size, "source vertex rebuild requires at least four vertices");
        return false;
    }
    if (source_convex_vertices_duplicate(vertices, vertex_count))
    {
        set_error(error_buffer, error_buffer_size, "source vertex rebuild contains duplicate vertices");
        return false;
    }

    slayer3d_vec3 points[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    for (int vertex = 0; vertex < vertex_count; ++vertex)
    {
        const int *coord = source_convex_vertex_at(vertices, vertex);
        if (!source_convex_vertex_on_snap(world_runtime, coord))
        {
            set_error(error_buffer, error_buffer_size, "source vertex rebuild contains off-grid vertices");
            return false;
        }
        points[vertex] = source_convex_vertex_meters(world_runtime, coord);
    }

    int plane_count = 0;
    for (int a = 0; a < vertex_count; ++a)
    {
        for (int b = a + 1; b < vertex_count; ++b)
        {
            for (int c = b + 1; c < vertex_count; ++c)
            {
                if (!source_convex_add_plane(planes, &plane_count, plane_capacity, points, vertex_count, a, b, c))
                {
                    set_error(error_buffer, error_buffer_size, "source vertex rebuild exceeded face capacity");
                    return false;
                }
            }
        }
    }
    if (plane_count < 4)
    {
        set_error(error_buffer, error_buffer_size, "source vertex rebuild would create zero-volume geometry");
        return false;
    }

    for (int vertex = 0; vertex < vertex_count; ++vertex)
    {
        int incident_planes = 0;
        for (int plane = 0; plane < plane_count; ++plane)
        {
            for (int i = 0; i < planes[plane].vertex_count; ++i)
            {
                if (planes[plane].vertex_indices[i] == vertex)
                {
                    ++incident_planes;
                    break;
                }
            }
        }
        if (incident_planes <= 0)
        {
            set_error(error_buffer, error_buffer_size,
                      "source vertex rebuild would discard a vertex outside the convex surface");
            return false;
        }
    }

    *out_plane_count = plane_count;
    return true;
}

static const char *source_convex_face_material(const editor_brush_source_box_runtime *box, int source_face_index)
{
    if (box == NULL)
        return "";
    if (source_face_index >= 0 && source_face_index < (int)SDL_arraysize(box->face_materials) &&
        box->face_materials[source_face_index] != NULL && box->face_materials[source_face_index][0] != '\0')
    {
        return box->face_materials[source_face_index];
    }
    return box->material != NULL ? box->material : "";
}

bool editor_brush_world_build_source_convex_brush_from_vertices(const brush_world_runtime *world_runtime,
                                                                const char *brush_identity, const int *vertices,
                                                                int vertex_count, slayer3d_game_data_brush *out_brush,
                                                                char *error_buffer, int error_buffer_size)
{
    if (out_brush != NULL)
        SDL_zero(*out_brush);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || brush_identity == NULL ||
        brush_identity[0] == '\0' || vertices == NULL || out_brush == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source vertex rebuild requires a source brush");
        return false;
    }

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_identity);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }
    const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];

    source_convex_plane planes[SLAYER3D_EDITOR_SOURCE_CONVEX_FACE_CAPACITY];
    SDL_zeroa(planes);
    int plane_count = 0;
    if (!source_convex_build_planes(world_runtime, vertices, vertex_count, planes, (int)SDL_arraysize(planes),
                                    &plane_count, error_buffer, error_buffer_size))
    {
        return false;
    }

    slayer3d_game_data_brush_face *faces =
        (slayer3d_game_data_brush_face *)SDL_calloc((size_t)plane_count, sizeof(*faces));
    if (faces == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate rebuilt source brush faces");
        return false;
    }

    out_brush->name = SDL_strdup(box->name != NULL ? box->name : "");
    out_brush->contents = box->contents != 0u ? box->contents : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    out_brush->face_count = plane_count;
    out_brush->faces = faces;
    out_brush->has_bounds = true;
    source_convex_bounds_meters(world_runtime, vertices, vertex_count, &out_brush->bounds);
    if (out_brush->name == NULL || !set_source_brush_metadata_string(&out_brush->editor.stable_id, box->stable_id) ||
        !set_source_brush_metadata_string(&out_brush->editor.prefab, box->prefab))
    {
        free_source_runtime_brush(out_brush);
        set_error(error_buffer, error_buffer_size, "failed to allocate rebuilt source brush metadata");
        return false;
    }

    for (int face_index = 0; face_index < plane_count; ++face_index)
    {
        const source_convex_plane *plane = &planes[face_index];
        const char *material_name = source_convex_face_material(box, plane->source_face_index);
        const int material_index = source_material_index_by_name(&world_runtime->desc, material_name);
        if (material_index < 0)
        {
            free_source_runtime_brush(out_brush);
            set_errorf(error_buffer, error_buffer_size, "invalid editor brush source face material '%s'",
                       material_name != NULL ? material_name : "<null>");
            return false;
        }
        init_source_box_face(&faces[face_index], plane->normal, plane->distance, material_index, material_name);

        char face_stable_id[320];
        if (plane->source_face_index >= 0)
        {
            SDL_snprintf(face_stable_id, sizeof(face_stable_id), "%s.face.%s",
                         box->stable_id != NULL ? box->stable_id : box->name,
                         editor_brush_source_box_face_keys[plane->source_face_index]);
        }
        else
        {
            SDL_snprintf(face_stable_id, sizeof(face_stable_id), "%s.face.rebuilt.%d",
                         box->stable_id != NULL ? box->stable_id : box->name, face_index);
        }
        if (!set_source_brush_metadata_string(&faces[face_index].editor.stable_id, face_stable_id))
        {
            free_source_runtime_brush(out_brush);
            set_error(error_buffer, error_buffer_size, "failed to allocate rebuilt source face metadata");
            return false;
        }
    }
    return true;
}

static void source_vertex_operation_set_failure(editor_brush_source_vertex_operation_result *result,
                                                const char *message)
{
    if (result == NULL)
        return;
    result->valid = false;
    SDL_strlcpy(result->diagnostic, message != NULL ? message : "invalid source vertex operation",
                sizeof(result->diagnostic));
}

static bool source_vertex_operation_append_unique(int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                                  int *vertex_count, const int coord[3], char *error_buffer,
                                                  int error_buffer_size)
{
    if (vertices == NULL || vertex_count == NULL || coord == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source vertex operation requires vertices");
        return false;
    }
    for (int i = 0; i < *vertex_count; ++i)
    {
        if (vertices[i][0] == coord[0] && vertices[i][1] == coord[1] && vertices[i][2] == coord[2])
            return true;
    }
    if (*vertex_count >= SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
    {
        set_error(error_buffer, error_buffer_size, "source vertex operation exceeded convex vertex capacity");
        return false;
    }
    vertices[*vertex_count][0] = coord[0];
    vertices[*vertex_count][1] = coord[1];
    vertices[*vertex_count][2] = coord[2];
    ++(*vertex_count);
    return true;
}

static bool source_vertex_operation_base_vertices(const brush_world_runtime *world_runtime, const char *brush_identity,
                                                  int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                                  int *out_vertex_count, char *error_buffer, int error_buffer_size)
{
    if (out_vertex_count != NULL)
        *out_vertex_count = 0;
    if (world_runtime == NULL || brush_identity == NULL || vertices == NULL || out_vertex_count == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source vertex operation requires a source brush");
        return false;
    }

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_identity);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    editor_brush_source_vertex_model model;
    if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, error_buffer,
                                                    error_buffer_size))
    {
        return false;
    }
    for (int i = 0; i < model.vertex_count; ++i)
    {
        if (!source_vertex_operation_append_unique(vertices, out_vertex_count, model.vertices[i].coord, error_buffer,
                                                   error_buffer_size))
        {
            return false;
        }
    }
    return true;
}

void editor_brush_world_free_source_clip_result(editor_brush_source_clip_result *result)
{
    if (result == NULL)
        return;
    for (int i = 0; i < result->output_brush_count && i < SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY; ++i)
        free_editor_brush_source_box(&result->output_brushes[i]);
    SDL_zero(*result);
}

static void source_clip_operation_set_failure(editor_brush_source_clip_result *result, const char *message)
{
    if (result == NULL)
        return;
    result->valid = false;
    SDL_strlcpy(result->diagnostic, message != NULL ? message : "invalid source clip operation",
                sizeof(result->diagnostic));
}

static float source_clip_signed_distance(const editor_brush_source_clip_desc *desc, const int coord[3])
{
    return desc != NULL && coord != NULL ? desc->normal.x * (float)coord[0] + desc->normal.y * (float)coord[1] +
                                               desc->normal.z * (float)coord[2] - desc->distance_source_units
                                         : 0.0f;
}

static bool source_clip_keep_signed_distance(float signed_distance, bool keep_front)
{
    return keep_front ? signed_distance >= -0.001f : signed_distance <= 0.001f;
}

static bool source_clip_append_intersection(int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                            int *vertex_count, const int a[3], const int b[3], float signed_a,
                                            float signed_b, char *error_buffer, int error_buffer_size)
{
    const float denominator = signed_a - signed_b;
    if (SDL_fabsf(denominator) <= 0.000001f)
        return true;
    const float t = signed_a / denominator;
    if (t < -0.001f || t > 1.001f)
        return true;

    int intersection[3] = {
        (int)SDL_lroundf((float)a[0] + ((float)b[0] - (float)a[0]) * t),
        (int)SDL_lroundf((float)a[1] + ((float)b[1] - (float)a[1]) * t),
        (int)SDL_lroundf((float)a[2] + ((float)b[2] - (float)a[2]) * t),
    };
    return source_vertex_operation_append_unique(vertices, vertex_count, intersection, error_buffer, error_buffer_size);
}

static bool source_clip_collect_side_vertices(const brush_world_runtime *world_runtime, int source_index,
                                              const editor_brush_source_clip_desc *desc, bool keep_front,
                                              int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                              int *out_vertex_count, char *error_buffer, int error_buffer_size)
{
    if (out_vertex_count != NULL)
        *out_vertex_count = 0;
    if (world_runtime == NULL || desc == NULL || vertices == NULL || out_vertex_count == NULL || source_index < 0 ||
        source_index >= world_runtime->editor_source_box_count)
    {
        set_error(error_buffer, error_buffer_size, "source clip requires a valid source brush");
        return false;
    }

    editor_brush_source_vertex_model model;
    if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, error_buffer,
                                                    error_buffer_size))
    {
        return false;
    }

    float signed_distances[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    SDL_zeroa(signed_distances);
    int kept_source_vertices = 0;
    int rejected_source_vertices = 0;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        signed_distances[i] = source_clip_signed_distance(desc, model.vertices[i].coord);
        if (source_clip_keep_signed_distance(signed_distances[i], keep_front))
        {
            ++kept_source_vertices;
            if (!source_vertex_operation_append_unique(vertices, out_vertex_count, model.vertices[i].coord,
                                                       error_buffer, error_buffer_size))
            {
                return false;
            }
        }
        else
        {
            ++rejected_source_vertices;
        }
    }

    if (kept_source_vertices == model.vertex_count)
        return true;
    if (rejected_source_vertices == model.vertex_count)
        return true;

    for (int edge = 0; edge < model.edge_count; ++edge)
    {
        const editor_brush_source_edge *source_edge = &model.edges[edge];
        const int a_index = source_edge->vertex_indices[0];
        const int b_index = source_edge->vertex_indices[1];
        if (a_index < 0 || a_index >= model.vertex_count || b_index < 0 || b_index >= model.vertex_count)
            continue;
        const float signed_a = signed_distances[a_index];
        const float signed_b = signed_distances[b_index];
        if ((signed_a > 0.001f && signed_b < -0.001f) || (signed_a < -0.001f && signed_b > 0.001f))
        {
            if (!source_clip_append_intersection(vertices, out_vertex_count, model.vertices[a_index].coord,
                                                 model.vertices[b_index].coord, signed_a, signed_b, error_buffer,
                                                 error_buffer_size))
            {
                return false;
            }
        }
    }
    return true;
}

static bool source_clip_result_identity_exists(const editor_brush_source_clip_result *result, const char *identity)
{
    if (result == NULL || identity == NULL || identity[0] == '\0')
        return false;
    for (int i = 0; i < result->output_brush_count; ++i)
    {
        const editor_brush_source_box_runtime *box = &result->output_brushes[i];
        if ((box->stable_id != NULL && SDL_strcmp(box->stable_id, identity) == 0) ||
            (box->name != NULL && SDL_strcmp(box->name, identity) == 0))
        {
            return true;
        }
    }
    return false;
}

static bool source_clip_make_unique_split_identity(const brush_world_runtime *world_runtime,
                                                   const editor_brush_source_clip_result *result, const char *base,
                                                   char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0u)
        return false;
    const char *prefix = base != NULL && base[0] != '\0' ? base : "source.clip";
    for (int i = 1; i < 10000; ++i)
    {
        SDL_snprintf(buffer, buffer_size, "%s.clip.%d", prefix, i);
        if (find_editor_source_box_index_by_identity(world_runtime, buffer) < 0 &&
            !source_clip_result_identity_exists(result, buffer))
        {
            return true;
        }
    }
    buffer[0] = '\0';
    return false;
}

static bool source_clip_set_box_identity(editor_brush_source_box_runtime *box, const char *identity, char *error_buffer,
                                         int error_buffer_size)
{
    if (box == NULL || identity == NULL || identity[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "source clip requires a brush identity");
        return false;
    }
    char *stable_id = SDL_strdup(identity);
    char *name = SDL_strdup(identity);
    if (stable_id == NULL || name == NULL)
    {
        SDL_free(stable_id);
        SDL_free(name);
        set_error(error_buffer, error_buffer_size, "failed to allocate clipped source brush identity");
        return false;
    }
    SDL_free(box->stable_id);
    SDL_free(box->name);
    box->stable_id = stable_id;
    box->name = name;
    return true;
}

static bool source_clip_output_box_from_vertices(const brush_world_runtime *world_runtime, int source_index,
                                                 const char *identity,
                                                 int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                                 int vertex_count, editor_brush_source_box_runtime *out_box,
                                                 char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || out_box == NULL || source_index < 0 ||
        source_index >= world_runtime->editor_source_box_count || vertex_count < 4)
    {
        set_error(error_buffer, error_buffer_size, "source clip would not produce a solid brush");
        return false;
    }

    const editor_brush_source_box_runtime *source_box = &world_runtime->editor_source_boxes[source_index];
    slayer3d_game_data_brush rebuilt;
    if (!editor_brush_world_build_source_convex_brush_from_vertices(
            world_runtime,
            source_box->stable_id != NULL && source_box->stable_id[0] != '\0' ? source_box->stable_id
                                                                              : source_box->name,
            &vertices[0][0], vertex_count, &rebuilt, error_buffer, error_buffer_size))
    {
        return false;
    }
    editor_brush_source_free_runtime_brush(&rebuilt);

    if (!copy_editor_brush_source_box_runtime(source_box, out_box))
    {
        set_error(error_buffer, error_buffer_size, "failed to copy clipped source brush");
        return false;
    }
    if (!source_clip_set_box_identity(out_box, identity, error_buffer, error_buffer_size))
    {
        free_editor_brush_source_box(out_box);
        return false;
    }
    char *old_prefab = out_box->prefab;
    out_box->prefab = SDL_strdup("convex");
    if (out_box->prefab == NULL)
    {
        out_box->prefab = old_prefab;
        free_editor_brush_source_box(out_box);
        set_error(error_buffer, error_buffer_size, "failed to allocate clipped source prefab metadata");
        return false;
    }
    SDL_free(old_prefab);
    out_box->vertex_count = vertex_count;
    for (int i = 0; i < vertex_count; ++i)
    {
        out_box->vertices[i][0] = vertices[i][0];
        out_box->vertices[i][1] = vertices[i][1];
        out_box->vertices[i][2] = vertices[i][2];
    }
    for (int i = vertex_count; i < SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY; ++i)
    {
        out_box->vertices[i][0] = 0;
        out_box->vertices[i][1] = 0;
        out_box->vertices[i][2] = 0;
    }
    source_box_update_bounds_from_vertices(out_box);
    if (!source_box_candidate_valid(world_runtime, out_box, source_index, error_buffer, error_buffer_size))
    {
        free_editor_brush_source_box(out_box);
        return false;
    }
    return true;
}

static bool source_clip_append_output(const brush_world_runtime *world_runtime, int source_index, const char *identity,
                                      int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3], int vertex_count,
                                      editor_brush_source_clip_result *out_result, char *error_buffer,
                                      int error_buffer_size)
{
    if (out_result == NULL || out_result->output_brush_count >= SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY)
    {
        set_error(error_buffer, error_buffer_size, "source clip exceeded output brush capacity");
        return false;
    }

    const int output_index = out_result->output_brush_count;
    if (!source_clip_output_box_from_vertices(world_runtime, source_index, identity, vertices, vertex_count,
                                              &out_result->output_brushes[output_index], error_buffer,
                                              error_buffer_size))
    {
        return false;
    }
    out_result->output_source_indices[output_index] = source_index;
    out_result->output_brush_count++;
    return true;
}

static bool source_clip_vertices_are_coplanar_with_clip(const editor_brush_source_clip_desc *desc,
                                                        int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                                        int vertex_count)
{
    if (desc == NULL || vertices == NULL || vertex_count <= 0)
        return false;
    for (int i = 0; i < vertex_count; ++i)
    {
        if (SDL_fabsf(source_clip_signed_distance(desc, vertices[i])) > 0.001f)
            return false;
    }
    return true;
}

static bool source_clip_append_side(const brush_world_runtime *world_runtime, int source_index,
                                    const editor_brush_source_clip_desc *desc, bool keep_front, const char *identity,
                                    editor_brush_source_clip_result *out_result, char *error_buffer,
                                    int error_buffer_size)
{
    int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    SDL_zeroa(vertices);
    int vertex_count = 0;
    if (!source_clip_collect_side_vertices(world_runtime, source_index, desc, keep_front, vertices, &vertex_count,
                                           error_buffer, error_buffer_size))
    {
        return false;
    }
    if (vertex_count < 4)
        return true;
    if (source_clip_vertices_are_coplanar_with_clip(desc, vertices, vertex_count))
        return true;
    return source_clip_append_output(world_runtime, source_index, identity, vertices, vertex_count, out_result,
                                     error_buffer, error_buffer_size);
}

static bool source_clip_desc_valid(const brush_world_runtime *world_runtime, const editor_brush_source_clip_desc *desc,
                                   char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || desc == NULL ||
        desc->brush_identities == NULL || desc->brush_count <= 0)
    {
        set_error(error_buffer, error_buffer_size, "source clip requires source-backed brush identities");
        return false;
    }
    const int max_outputs_per_brush = desc->keep_mode == EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH ? 2 : 1;
    if (desc->brush_count * max_outputs_per_brush > SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY)
    {
        set_error(error_buffer, error_buffer_size, "source clip selection exceeds output brush capacity");
        return false;
    }
    if (slayer3d_vec3_length_squared(desc->normal) <= 0.000001f)
    {
        set_error(error_buffer, error_buffer_size, "source clip requires a non-zero clip plane normal");
        return false;
    }
    for (int i = 0; i < desc->brush_count; ++i)
    {
        const char *identity = desc->brush_identities[i];
        if (identity == NULL || identity[0] == '\0' ||
            find_editor_source_box_index_by_identity(world_runtime, identity) < 0)
        {
            set_error(error_buffer, error_buffer_size, "source clip brush not found");
            return false;
        }
        for (int j = i + 1; j < desc->brush_count; ++j)
        {
            const char *other = desc->brush_identities[j];
            if (other != NULL && SDL_strcmp(identity, other) == 0)
            {
                set_error(error_buffer, error_buffer_size, "source clip brush identities must be unique");
                return false;
            }
        }
    }
    return true;
}

bool editor_brush_world_preview_source_clip_operation(const brush_world_runtime *world_runtime,
                                                      const editor_brush_source_clip_desc *desc,
                                                      editor_brush_source_clip_result *out_result, char *error_buffer,
                                                      int error_buffer_size)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    if (out_result == NULL || !source_clip_desc_valid(world_runtime, desc, error_buffer, error_buffer_size))
    {
        source_clip_operation_set_failure(out_result, error_buffer != NULL ? error_buffer : "invalid source clip");
        return false;
    }

    editor_brush_source_clip_desc normalized_desc = *desc;
    normalized_desc.normal = slayer3d_vec3_normalize(desc->normal);
    out_result->input_brush_count = desc->brush_count;

    for (int i = 0; i < desc->brush_count; ++i)
    {
        const int source_index = find_editor_source_box_index_by_identity(world_runtime, desc->brush_identities[i]);
        const editor_brush_source_box_runtime *source_box = &world_runtime->editor_source_boxes[source_index];
        const char *base_identity = source_box->stable_id != NULL && source_box->stable_id[0] != '\0'
                                        ? source_box->stable_id
                                        : source_box->name;
        const int output_count_before = out_result->output_brush_count;

        switch (desc->keep_mode)
        {
        case EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT:
            if (!source_clip_append_side(world_runtime, source_index, &normalized_desc, true, base_identity, out_result,
                                         error_buffer, error_buffer_size))
            {
                editor_brush_world_free_source_clip_result(out_result);
                source_clip_operation_set_failure(out_result, error_buffer);
                return false;
            }
            break;
        case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK:
            if (!source_clip_append_side(world_runtime, source_index, &normalized_desc, false, base_identity,
                                         out_result, error_buffer, error_buffer_size))
            {
                editor_brush_world_free_source_clip_result(out_result);
                source_clip_operation_set_failure(out_result, error_buffer);
                return false;
            }
            break;
        case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH: {
            if (!source_clip_append_side(world_runtime, source_index, &normalized_desc, true, base_identity, out_result,
                                         error_buffer, error_buffer_size))
            {
                editor_brush_world_free_source_clip_result(out_result);
                source_clip_operation_set_failure(out_result, error_buffer);
                return false;
            }
            char split_identity[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
            if (!source_clip_make_unique_split_identity(world_runtime, out_result, base_identity, split_identity,
                                                        sizeof(split_identity)) ||
                !source_clip_append_side(world_runtime, source_index, &normalized_desc, false, split_identity,
                                         out_result, error_buffer, error_buffer_size))
            {
                editor_brush_world_free_source_clip_result(out_result);
                source_clip_operation_set_failure(out_result, error_buffer);
                return false;
            }
            break;
        }
        default:
            editor_brush_world_free_source_clip_result(out_result);
            set_error(error_buffer, error_buffer_size, "unknown source clip keep mode");
            source_clip_operation_set_failure(out_result, "unknown source clip keep mode");
            return false;
        }

        if (out_result->output_brush_count == output_count_before)
        {
            editor_brush_world_free_source_clip_result(out_result);
            set_error(error_buffer, error_buffer_size, "source clip would remove a selected brush");
            source_clip_operation_set_failure(out_result, "source clip would remove a selected brush");
            return false;
        }
    }

    if (out_result->output_brush_count <= 0)
    {
        editor_brush_world_free_source_clip_result(out_result);
        set_error(error_buffer, error_buffer_size, "source clip would remove all selected brush geometry");
        source_clip_operation_set_failure(out_result, "source clip would remove all selected brush geometry");
        return false;
    }

    out_result->valid = true;
    SDL_snprintf(out_result->diagnostic, sizeof(out_result->diagnostic), "source clip produced %d brush%s",
                 out_result->output_brush_count, out_result->output_brush_count == 1 ? "" : "es");
    return true;
}

static bool source_clip_output_for_source_index(const editor_brush_source_clip_result *result, int source_index,
                                                int output_start_index, int *out_next_output_index)
{
    if (out_next_output_index != NULL)
        *out_next_output_index = -1;
    if (result == NULL)
        return false;
    for (int i = output_start_index; i < result->output_brush_count; ++i)
    {
        if (result->output_source_indices[i] == source_index)
        {
            if (out_next_output_index != NULL)
                *out_next_output_index = i;
            return true;
        }
    }
    return false;
}

bool editor_brush_world_apply_source_clip_operation(brush_world_runtime *world_runtime,
                                                    const editor_brush_source_clip_desc *desc,
                                                    editor_brush_source_clip_result *out_result, char *error_buffer,
                                                    int error_buffer_size)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    editor_brush_source_clip_result preview;
    SDL_zero(preview);
    if (!editor_brush_world_preview_source_clip_operation(world_runtime, desc, &preview, error_buffer,
                                                          error_buffer_size))
    {
        if (out_result != NULL)
            *out_result = preview;
        return false;
    }

    const int old_count = world_runtime->editor_source_box_count;
    bool *replace = old_count > 0 ? (bool *)SDL_calloc((size_t)old_count, sizeof(*replace)) : NULL;
    editor_brush_source_box_runtime *new_boxes = (editor_brush_source_box_runtime *)SDL_calloc(
        (size_t)(old_count - desc->brush_count + preview.output_brush_count), sizeof(*new_boxes));
    if ((old_count > 0 && replace == NULL) || new_boxes == NULL)
    {
        SDL_free(replace);
        SDL_free(new_boxes);
        editor_brush_world_free_source_clip_result(&preview);
        set_error(error_buffer, error_buffer_size, "failed to allocate source clip transaction");
        return false;
    }

    for (int i = 0; i < desc->brush_count; ++i)
    {
        const int source_index = find_editor_source_box_index_by_identity(world_runtime, desc->brush_identities[i]);
        if (source_index >= 0)
            replace[source_index] = true;
    }

    int write_index = 0;
    for (int source_index = 0; source_index < old_count; ++source_index)
    {
        if (!replace[source_index])
        {
            if (!copy_editor_brush_source_box_runtime(&world_runtime->editor_source_boxes[source_index],
                                                      &new_boxes[write_index++]))
            {
                set_error(error_buffer, error_buffer_size, "failed to copy source clip transaction");
                goto fail_before_commit;
            }
            continue;
        }

        int output_index = -1;
        int search_index = 0;
        while (source_clip_output_for_source_index(&preview, source_index, search_index, &output_index))
        {
            if (!copy_editor_brush_source_box_runtime(&preview.output_brushes[output_index], &new_boxes[write_index++]))
            {
                set_error(error_buffer, error_buffer_size, "failed to copy clipped source brush");
                goto fail_before_commit;
            }
            search_index = output_index + 1;
        }
    }

    editor_brush_source_box_runtime *old_boxes = world_runtime->editor_source_boxes;
    const int new_count = write_index;
    world_runtime->editor_source_boxes = new_boxes;
    world_runtime->editor_source_box_count = new_count;
    world_runtime->editor_source_box_capacity = new_count;
    if (!editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        world_runtime->editor_source_boxes = old_boxes;
        world_runtime->editor_source_box_count = old_count;
        world_runtime->editor_source_box_capacity = old_count;
        for (int i = 0; i < new_count; ++i)
            free_editor_brush_source_box(&new_boxes[i]);
        SDL_free(new_boxes);
        SDL_free(replace);
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        editor_brush_world_free_source_clip_result(&preview);
        return false;
    }

    for (int i = 0; i < old_count; ++i)
        free_editor_brush_source_box(&old_boxes[i]);
    SDL_free(old_boxes);
    SDL_free(replace);
    if (out_result != NULL)
        *out_result = preview;
    else
        editor_brush_world_free_source_clip_result(&preview);
    return true;

fail_before_commit:
    for (int i = 0; i < write_index; ++i)
        free_editor_brush_source_box(&new_boxes[i]);
    SDL_free(new_boxes);
    SDL_free(replace);
    editor_brush_world_free_source_clip_result(&preview);
    return false;
}

static bool source_vertex_operation_merge(const brush_world_runtime *world_runtime,
                                          const editor_brush_source_vertex_operation_desc *desc,
                                          int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                          int *vertex_count, char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || desc == NULL || vertices == NULL || vertex_count == NULL)
        return false;

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, desc->brush_identity);
    editor_brush_source_vertex_model model;
    if (source_index < 0 || !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model,
                                                                        error_buffer, error_buffer_size))
    {
        return false;
    }
    if (desc->vertex_index < 0 || desc->vertex_index >= model.vertex_count || desc->target_vertex_index < 0 ||
        desc->target_vertex_index >= model.vertex_count)
    {
        set_error(error_buffer, error_buffer_size, "source vertex merge index out of range");
        return false;
    }

    *vertex_count = 0;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        const int *coord =
            i == desc->vertex_index ? model.vertices[desc->target_vertex_index].coord : model.vertices[i].coord;
        if (!source_vertex_operation_append_unique(vertices, vertex_count, coord, error_buffer, error_buffer_size))
            return false;
    }
    return true;
}

static bool source_vertex_operation_delete(const brush_world_runtime *world_runtime,
                                           const editor_brush_source_vertex_operation_desc *desc,
                                           int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                           int *vertex_count, char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || desc == NULL || vertices == NULL || vertex_count == NULL)
        return false;

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, desc->brush_identity);
    editor_brush_source_vertex_model model;
    if (source_index < 0 || !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model,
                                                                        error_buffer, error_buffer_size))
    {
        return false;
    }
    if (desc->vertex_index < 0 || desc->vertex_index >= model.vertex_count)
    {
        set_error(error_buffer, error_buffer_size, "source vertex delete index out of range");
        return false;
    }

    *vertex_count = 0;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        if (i == desc->vertex_index)
            continue;
        if (!source_vertex_operation_append_unique(vertices, vertex_count, model.vertices[i].coord, error_buffer,
                                                   error_buffer_size))
        {
            return false;
        }
    }
    return true;
}

static bool source_vertex_operation_desc_has_index(const editor_brush_source_vertex_operation_desc *desc, int index);
static int source_vertex_operation_desc_index_offset(const editor_brush_source_vertex_operation_desc *desc, int index);

static bool source_vertex_operation_move(const brush_world_runtime *world_runtime,
                                         const editor_brush_source_vertex_operation_desc *desc,
                                         int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                         int *vertex_count, char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || desc == NULL || vertices == NULL || vertex_count == NULL)
        return false;

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, desc->brush_identity);
    editor_brush_source_vertex_model model;
    if (source_index < 0 || !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model,
                                                                        error_buffer, error_buffer_size))
    {
        return false;
    }
    if (desc->vertex_index < 0 || desc->vertex_index >= model.vertex_count)
    {
        set_error(error_buffer, error_buffer_size, "source vertex move index out of range");
        return false;
    }

    *vertex_count = 0;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        const int *coord = i == desc->vertex_index ? desc->coord : model.vertices[i].coord;
        if (!source_vertex_operation_append_unique(vertices, vertex_count, coord, error_buffer, error_buffer_size))
            return false;
    }
    return true;
}

static bool source_vertex_operation_move_many(const brush_world_runtime *world_runtime,
                                              const editor_brush_source_vertex_operation_desc *desc,
                                              int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                              int *vertex_count, char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || desc == NULL || vertices == NULL || vertex_count == NULL)
        return false;

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, desc->brush_identity);
    editor_brush_source_vertex_model model;
    if (source_index < 0 || !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model,
                                                                        error_buffer, error_buffer_size))
    {
        return false;
    }
    if (desc->vertex_index_count <= 0 || desc->vertex_index_count > SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
    {
        set_error(error_buffer, error_buffer_size, "source vertex move requires at least one vertex");
        return false;
    }
    for (int i = 0; i < desc->vertex_index_count; ++i)
    {
        const int vertex_index = desc->vertex_indices[i];
        if (vertex_index < 0 || vertex_index >= model.vertex_count)
        {
            set_error(error_buffer, error_buffer_size, "source vertex move index out of range");
            return false;
        }
        for (int j = i + 1; j < desc->vertex_index_count; ++j)
        {
            if (desc->vertex_indices[j] == vertex_index)
            {
                set_error(error_buffer, error_buffer_size, "source vertex move indices must be unique");
                return false;
            }
        }
    }

    *vertex_count = 0;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        const int move_offset = source_vertex_operation_desc_index_offset(desc, i);
        const int *coord = move_offset >= 0 ? desc->coords[move_offset] : model.vertices[i].coord;
        if (!source_vertex_operation_append_unique(vertices, vertex_count, coord, error_buffer, error_buffer_size))
            return false;
    }
    return true;
}

static bool source_vertex_operation_merge_many_to_target(const brush_world_runtime *world_runtime,
                                                         const editor_brush_source_vertex_operation_desc *desc,
                                                         int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                                         int *vertex_count, char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || desc == NULL || vertices == NULL || vertex_count == NULL)
        return false;

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, desc->brush_identity);
    editor_brush_source_vertex_model model;
    if (source_index < 0 || !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model,
                                                                        error_buffer, error_buffer_size))
    {
        return false;
    }
    if (desc->target_vertex_index < 0 || desc->target_vertex_index >= model.vertex_count)
    {
        set_error(error_buffer, error_buffer_size, "source vertex merge target index out of range");
        return false;
    }
    if (desc->vertex_index_count <= 0 || desc->vertex_index_count > SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
    {
        set_error(error_buffer, error_buffer_size, "source vertex merge requires at least one vertex");
        return false;
    }
    for (int i = 0; i < desc->vertex_index_count; ++i)
    {
        const int vertex_index = desc->vertex_indices[i];
        if (vertex_index < 0 || vertex_index >= model.vertex_count)
        {
            set_error(error_buffer, error_buffer_size, "source vertex merge index out of range");
            return false;
        }
        for (int j = i + 1; j < desc->vertex_index_count; ++j)
        {
            if (desc->vertex_indices[j] == vertex_index)
            {
                set_error(error_buffer, error_buffer_size, "source vertex merge indices must be unique");
                return false;
            }
        }
    }

    *vertex_count = 0;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        const int *coord = source_vertex_operation_desc_has_index(desc, i)
                               ? model.vertices[desc->target_vertex_index].coord
                               : model.vertices[i].coord;
        if (!source_vertex_operation_append_unique(vertices, vertex_count, coord, error_buffer, error_buffer_size))
            return false;
    }
    return true;
}

static bool source_vertex_operation_desc_has_index(const editor_brush_source_vertex_operation_desc *desc, int index)
{
    if (desc == NULL || index < 0)
        return false;
    for (int i = 0; i < desc->vertex_index_count; ++i)
    {
        if (desc->vertex_indices[i] == index)
            return true;
    }
    return false;
}

static int source_vertex_operation_desc_index_offset(const editor_brush_source_vertex_operation_desc *desc, int index)
{
    if (desc == NULL || index < 0)
        return -1;
    for (int i = 0; i < desc->vertex_index_count; ++i)
    {
        if (desc->vertex_indices[i] == index)
            return i;
    }
    return -1;
}

static bool source_vertex_operation_delete_many(const brush_world_runtime *world_runtime,
                                                const editor_brush_source_vertex_operation_desc *desc,
                                                int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                                int *vertex_count, char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || desc == NULL || vertices == NULL || vertex_count == NULL)
        return false;

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, desc->brush_identity);
    editor_brush_source_vertex_model model;
    if (source_index < 0 || !editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model,
                                                                        error_buffer, error_buffer_size))
    {
        return false;
    }
    if (desc->vertex_index_count <= 0 || desc->vertex_index_count > SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
    {
        set_error(error_buffer, error_buffer_size, "source vertex delete requires at least one vertex");
        return false;
    }
    for (int i = 0; i < desc->vertex_index_count; ++i)
    {
        const int vertex_index = desc->vertex_indices[i];
        if (vertex_index < 0 || vertex_index >= model.vertex_count)
        {
            set_error(error_buffer, error_buffer_size, "source vertex delete index out of range");
            return false;
        }
        for (int j = i + 1; j < desc->vertex_index_count; ++j)
        {
            if (desc->vertex_indices[j] == vertex_index)
            {
                set_error(error_buffer, error_buffer_size, "source vertex delete indices must be unique");
                return false;
            }
        }
    }

    *vertex_count = 0;
    for (int i = 0; i < model.vertex_count; ++i)
    {
        if (source_vertex_operation_desc_has_index(desc, i))
            continue;
        if (!source_vertex_operation_append_unique(vertices, vertex_count, model.vertices[i].coord, error_buffer,
                                                   error_buffer_size))
        {
            return false;
        }
    }
    return true;
}

static bool source_vertex_operation_add(const brush_world_runtime *world_runtime,
                                        const editor_brush_source_vertex_operation_desc *desc,
                                        int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                        int *vertex_count, char *error_buffer, int error_buffer_size)
{
    if (!source_vertex_operation_base_vertices(world_runtime, desc->brush_identity, vertices, vertex_count,
                                               error_buffer, error_buffer_size))
    {
        return false;
    }
    for (int i = 0; i < *vertex_count; ++i)
    {
        if (vertices[i][0] == desc->coord[0] && vertices[i][1] == desc->coord[1] && vertices[i][2] == desc->coord[2])
        {
            set_error(error_buffer, error_buffer_size, "source vertex add coordinate already exists");
            return false;
        }
    }
    return source_vertex_operation_append_unique(vertices, vertex_count, desc->coord, error_buffer, error_buffer_size);
}

static int source_snap_coord_to_units(int coord, int snap_units)
{
    if (snap_units <= 1)
        return coord;
    return (int)SDL_roundf((float)coord / (float)snap_units) * snap_units;
}

static bool source_vertex_operation_snap(const brush_world_runtime *world_runtime,
                                         const editor_brush_source_vertex_operation_desc *desc,
                                         int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3],
                                         int *vertex_count, int *out_changed_count, char *error_buffer,
                                         int error_buffer_size)
{
    if (out_changed_count != NULL)
        *out_changed_count = 0;
    int base_vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    SDL_zeroa(base_vertices);
    int base_vertex_count = 0;
    if (!source_vertex_operation_base_vertices(world_runtime, desc->brush_identity, base_vertices, &base_vertex_count,
                                               error_buffer, error_buffer_size))
    {
        return false;
    }

    const int snap_units = desc->snap_units > 0 ? desc->snap_units : source_snap_units(world_runtime);
    *vertex_count = 0;
    int changed_count = 0;
    for (int i = 0; i < base_vertex_count; ++i)
    {
        int snapped[3] = {base_vertices[i][0], base_vertices[i][1], base_vertices[i][2]};
        for (int axis = 0; axis < 3; ++axis)
            snapped[axis] = source_snap_coord_to_units(snapped[axis], snap_units);
        if (snapped[0] != base_vertices[i][0] || snapped[1] != base_vertices[i][1] || snapped[2] != base_vertices[i][2])
            ++changed_count;
        if (!source_vertex_operation_append_unique(vertices, vertex_count, snapped, error_buffer, error_buffer_size))
            return false;
    }
    if (out_changed_count != NULL)
        *out_changed_count = changed_count;
    return true;
}

typedef struct editor_brush_source_vertex_transaction
{
    int source_index;
    editor_brush_source_box_runtime rollback_box;
    bool rollback_captured;
    editor_brush_source_vertex_operation_result preview;
} editor_brush_source_vertex_transaction;

static void source_vertex_transaction_dispose(editor_brush_source_vertex_transaction *transaction)
{
    if (transaction == NULL)
        return;
    if (transaction->rollback_captured)
        free_editor_brush_source_box(&transaction->rollback_box);
    editor_brush_source_free_runtime_brush(&transaction->preview.brush);
    SDL_zero(*transaction);
}

bool editor_brush_world_preview_source_vertex_operation(const brush_world_runtime *world_runtime,
                                                        const editor_brush_source_vertex_operation_desc *desc,
                                                        editor_brush_source_vertex_operation_result *out_result,
                                                        char *error_buffer, int error_buffer_size)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || desc == NULL || out_result == NULL ||
        desc->brush_identity == NULL || desc->brush_identity[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "source vertex operation requires a source brush");
        source_vertex_operation_set_failure(out_result, "source vertex operation requires a source brush");
        return false;
    }

    if (find_editor_source_box_index_by_identity(world_runtime, desc->brush_identity) < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        source_vertex_operation_set_failure(out_result, "source brush not found");
        return false;
    }

    int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    SDL_zeroa(vertices);
    int vertex_count = 0;
    int changed_count = 0;
    bool ok = false;
    switch (desc->type)
    {
    case EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_ADD:
        ok = source_vertex_operation_add(world_runtime, desc, vertices, &vertex_count, error_buffer, error_buffer_size);
        changed_count = ok ? 1 : 0;
        break;
    case EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MOVE:
        ok =
            source_vertex_operation_move(world_runtime, desc, vertices, &vertex_count, error_buffer, error_buffer_size);
        changed_count = ok ? 1 : 0;
        break;
    case EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MOVE_MANY:
        ok = source_vertex_operation_move_many(world_runtime, desc, vertices, &vertex_count, error_buffer,
                                               error_buffer_size);
        changed_count = ok ? desc->vertex_index_count : 0;
        break;
    case EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_DELETE:
        ok = source_vertex_operation_delete(world_runtime, desc, vertices, &vertex_count, error_buffer,
                                            error_buffer_size);
        changed_count = ok ? 1 : 0;
        break;
    case EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_DELETE_MANY:
        ok = source_vertex_operation_delete_many(world_runtime, desc, vertices, &vertex_count, error_buffer,
                                                 error_buffer_size);
        changed_count = ok ? desc->vertex_index_count : 0;
        break;
    case EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MERGE:
        ok = source_vertex_operation_merge(world_runtime, desc, vertices, &vertex_count, error_buffer,
                                           error_buffer_size);
        changed_count = ok ? 1 : 0;
        break;
    case EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MERGE_MANY_TO_TARGET:
        ok = source_vertex_operation_merge_many_to_target(world_runtime, desc, vertices, &vertex_count, error_buffer,
                                                          error_buffer_size);
        changed_count = ok ? desc->vertex_index_count : 0;
        break;
    case EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_SNAP:
        ok = source_vertex_operation_snap(world_runtime, desc, vertices, &vertex_count, &changed_count, error_buffer,
                                          error_buffer_size);
        break;
    default:
        set_error(error_buffer, error_buffer_size, "unknown source vertex operation");
        ok = false;
        break;
    }
    if (!ok)
    {
        source_vertex_operation_set_failure(out_result, error_buffer != NULL ? error_buffer : "invalid operation");
        return false;
    }

    slayer3d_game_data_brush rebuilt;
    if (!editor_brush_world_build_source_convex_brush_from_vertices(world_runtime, desc->brush_identity,
                                                                    &vertices[0][0], vertex_count, &rebuilt,
                                                                    error_buffer, error_buffer_size))
    {
        source_vertex_operation_set_failure(out_result, error_buffer != NULL ? error_buffer : "invalid operation");
        return false;
    }

    out_result->valid = true;
    out_result->vertex_count = vertex_count;
    out_result->changed_count = changed_count;
    for (int i = 0; i < vertex_count; ++i)
    {
        out_result->vertices[i][0] = vertices[i][0];
        out_result->vertices[i][1] = vertices[i][1];
        out_result->vertices[i][2] = vertices[i][2];
    }
    out_result->face_count = rebuilt.face_count;
    out_result->brush = rebuilt;
    SDL_snprintf(out_result->diagnostic, sizeof(out_result->diagnostic), "source vertex operation valid");
    return true;
}

bool editor_brush_world_apply_source_vertex_operation(brush_world_runtime *world_runtime,
                                                      const editor_brush_source_vertex_operation_desc *desc,
                                                      editor_brush_source_vertex_operation_result *out_result,
                                                      char *error_buffer, int error_buffer_size)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || desc == NULL ||
        desc->brush_identity == NULL || desc->brush_identity[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "source vertex operation requires a source brush");
        return false;
    }

    editor_brush_source_vertex_transaction transaction;
    SDL_zero(transaction);
    if (!editor_brush_world_preview_source_vertex_operation(world_runtime, desc, &transaction.preview, error_buffer,
                                                            error_buffer_size))
    {
        if (out_result != NULL)
            *out_result = transaction.preview;
        return false;
    }

    transaction.source_index = find_editor_source_box_index_by_identity(world_runtime, desc->brush_identity);
    if (transaction.source_index < 0)
    {
        source_vertex_transaction_dispose(&transaction);
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    if (!copy_editor_brush_source_box_runtime(&world_runtime->editor_source_boxes[transaction.source_index],
                                              &transaction.rollback_box))
    {
        source_vertex_transaction_dispose(&transaction);
        set_error(error_buffer, error_buffer_size, "failed to copy source brush rollback state");
        return false;
    }
    transaction.rollback_captured = true;

    editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[transaction.source_index];
    box->vertex_count = transaction.preview.vertex_count;
    for (int i = 0; i < transaction.preview.vertex_count; ++i)
    {
        box->vertices[i][0] = transaction.preview.vertices[i][0];
        box->vertices[i][1] = transaction.preview.vertices[i][1];
        box->vertices[i][2] = transaction.preview.vertices[i][2];
    }
    source_box_update_bounds_from_vertices(box);
    if (box->prefab == NULL || SDL_strcmp(box->prefab, "box") == 0 || SDL_strcmp(box->prefab, "editor.box") == 0)
    {
        char *old_prefab = box->prefab;
        box->prefab = SDL_strdup("convex");
        if (box->prefab == NULL)
        {
            box->prefab = old_prefab;
            source_vertex_transaction_dispose(&transaction);
            set_error(error_buffer, error_buffer_size, "failed to allocate source convex prefab metadata");
            return false;
        }
        SDL_free(old_prefab);
    }

    if (!source_box_candidate_valid(world_runtime, box, transaction.source_index, error_buffer, error_buffer_size) ||
        !editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        free_editor_brush_source_box(box);
        *box = transaction.rollback_box;
        transaction.rollback_captured = false;
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        editor_brush_source_free_runtime_brush(&transaction.preview.brush);
        return false;
    }

    free_editor_brush_source_box(&transaction.rollback_box);
    transaction.rollback_captured = false;
    if (out_result != NULL)
        *out_result = transaction.preview;
    else
        editor_brush_source_free_runtime_brush(&transaction.preview.brush);
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

static const char *source_prefab_for_material(const char *material)
{
    if (material != NULL)
    {
        if (SDL_strstr(material, ".floor") != NULL)
            return "floor";
        if (SDL_strstr(material, ".wall") != NULL)
            return "wall";
        if (SDL_strstr(material, ".ceiling") != NULL)
            return "ceiling";
        if (SDL_strstr(material, ".sky") != NULL)
            return "sky";
    }
    return NULL;
}

static bool source_prefab_name_supported(const char *prefab)
{
    return prefab != NULL &&
           (SDL_strcmp(prefab, "floor") == 0 || SDL_strcmp(prefab, "wall") == 0 || SDL_strcmp(prefab, "ceiling") == 0 ||
            SDL_strcmp(prefab, "sky") == 0 || SDL_strcmp(prefab, "box") == 0 || SDL_strcmp(prefab, "editor.box") == 0);
}

static unsigned int source_prefab_default_contents(const char *prefab, unsigned int authored_contents)
{
    if (authored_contents != 0u)
        return authored_contents;
    if (prefab != NULL && SDL_strcmp(prefab, "sky") == 0)
    {
        return SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP |
               SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP;
    }
    return SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
}

static slayer3d_vec3 source_prefab_oriented_offset(slayer3d_vec3 value, const char *axis)
{
    if (axis != NULL && SDL_strcmp(axis, "x") == 0)
        return slayer3d_vec3_make(value.z, value.y, value.x);
    return value;
}

static void source_prefab_set_axis_bounds(int *out_min, int *out_max, int a, int b)
{
    if (out_min != NULL)
        *out_min = SDL_min(a, b);
    if (out_max != NULL)
        *out_max = SDL_max(a, b);
}

static bool editor_brush_world_build_source_prefab_candidate(const brush_world_runtime *world_runtime,
                                                             const editor_brush_source_prefab_desc *desc,
                                                             const char *candidate_name,
                                                             editor_brush_source_box_runtime *out_box,
                                                             char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || desc == NULL || out_box == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source prefab placement requires a source-backed brush world");
        return false;
    }

    const char *material_prefab = source_prefab_for_material(desc->material);
    const char *prefab = desc->prefab != NULL && desc->prefab[0] != '\0'
                             ? desc->prefab
                             : (material_prefab != NULL ? material_prefab : "");
    if (!source_prefab_name_supported(prefab))
    {
        set_errorf(error_buffer, error_buffer_size, "unsupported source prefab '%s'",
                   prefab != NULL && prefab[0] != '\0' ? prefab : "<empty>");
        return false;
    }
    if (desc->material == NULL || desc->material[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "source prefab placement requires a material");
        return false;
    }
    if (desc->use_grid_bounds && desc->grid_size <= 0.0f)
    {
        set_error(error_buffer, error_buffer_size, "source prefab placement requires a positive grid size");
        return false;
    }
    if (candidate_name == NULL || candidate_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "source prefab placement requires a candidate name");
        return false;
    }

    const slayer3d_vec3 recipe_min =
        desc->use_grid_bounds ? slayer3d_vec3_scale(desc->grid_min, desc->grid_size) : desc->min;
    const slayer3d_vec3 recipe_max =
        desc->use_grid_bounds ? slayer3d_vec3_scale(desc->grid_max, desc->grid_size) : desc->max;
    const slayer3d_vec3 oriented_min = source_prefab_oriented_offset(recipe_min, desc->axis);
    const slayer3d_vec3 oriented_max = source_prefab_oriented_offset(recipe_max, desc->axis);

    SDL_zero(*out_box);
    if (!copy_source_string(&out_box->stable_id, candidate_name) ||
        !copy_source_string(&out_box->name, candidate_name) || !copy_source_string(&out_box->prefab, prefab) ||
        !copy_source_string(&out_box->material, desc->material))
    {
        free_editor_brush_source_box(out_box);
        set_error(error_buffer, error_buffer_size, "failed to allocate source prefab candidate");
        return false;
    }

    source_prefab_set_axis_bounds(
        &out_box->min[0], &out_box->max[0],
        editor_brush_source_units_from_meters(world_runtime, desc->anchor.x + oriented_min.x),
        editor_brush_source_units_from_meters(world_runtime, desc->anchor.x + oriented_max.x));
    source_prefab_set_axis_bounds(
        &out_box->min[1], &out_box->max[1],
        editor_brush_source_units_from_meters(world_runtime, desc->anchor.y + oriented_min.y),
        editor_brush_source_units_from_meters(world_runtime, desc->anchor.y + oriented_max.y));
    source_prefab_set_axis_bounds(
        &out_box->min[2], &out_box->max[2],
        editor_brush_source_units_from_meters(world_runtime, desc->anchor.z + oriented_min.z),
        editor_brush_source_units_from_meters(world_runtime, desc->anchor.z + oriented_max.z));
    out_box->contents = source_prefab_default_contents(prefab, desc->contents);
    if (!source_box_extents_valid(out_box))
    {
        free_editor_brush_source_box(out_box);
        set_error(error_buffer, error_buffer_size, "source prefab placement would create invalid geometry");
        return false;
    }
    return true;
}

static const char *source_prefab_for_brush(const slayer3d_game_data_brush *brush, const char *material)
{
    if (brush != NULL && brush->editor.prefab != NULL && SDL_strcmp(brush->editor.prefab, "editor.box") != 0)
        return brush->editor.prefab;
    const char *material_prefab = source_prefab_for_material(material);
    if (material_prefab != NULL)
        return material_prefab;
    return brush != NULL && brush->editor.prefab != NULL ? brush->editor.prefab : "box";
}

bool editor_brush_source_box_from_create_desc(const brush_world_runtime *world_runtime,
                                              const slayer3d_game_data_create_box_brush_desc *desc,
                                              const char *brush_name, editor_brush_source_box_runtime *out_box)
{
    if (world_runtime == NULL || desc == NULL || brush_name == NULL || brush_name[0] == '\0' || out_box == NULL)
        return false;

    SDL_zero(*out_box);
    const char *material = desc->material_name != NULL ? desc->material_name : "";
    const char *prefab = source_prefab_for_material(material);
    if (prefab == NULL)
        prefab = "editor.box";
    if (!copy_source_string(&out_box->stable_id, brush_name) || !copy_source_string(&out_box->name, brush_name) ||
        !copy_source_string(&out_box->prefab, prefab) || !copy_source_string(&out_box->material, material))
    {
        free_editor_brush_source_box(out_box);
        return false;
    }
    out_box->min[0] = editor_brush_source_units_from_meters(world_runtime, desc->min.x);
    out_box->min[1] = editor_brush_source_units_from_meters(world_runtime, desc->min.y);
    out_box->min[2] = editor_brush_source_units_from_meters(world_runtime, desc->min.z);
    out_box->max[0] = editor_brush_source_units_from_meters(world_runtime, desc->max.x);
    out_box->max[1] = editor_brush_source_units_from_meters(world_runtime, desc->max.y);
    out_box->max[2] = editor_brush_source_units_from_meters(world_runtime, desc->max.z);
    out_box->contents = desc->contents != 0u ? desc->contents : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    return true;
}

slayer3d_bounding_box editor_brush_source_box_bounds_meters(const brush_world_runtime *world_runtime,
                                                            const editor_brush_source_box_runtime *box)
{
    slayer3d_bounding_box bounds;
    bounds.min = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    bounds.max = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (box == NULL)
        return bounds;

    bounds.min = slayer3d_vec3_make(editor_brush_source_meters_from_units(world_runtime, box->min[0]),
                                    editor_brush_source_meters_from_units(world_runtime, box->min[1]),
                                    editor_brush_source_meters_from_units(world_runtime, box->min[2]));
    bounds.max = slayer3d_vec3_make(editor_brush_source_meters_from_units(world_runtime, box->max[0]),
                                    editor_brush_source_meters_from_units(world_runtime, box->max[1]),
                                    editor_brush_source_meters_from_units(world_runtime, box->max[2]));
    return bounds;
}

bool editor_brush_world_run_source_prefab_command(brush_world_runtime *world_runtime,
                                                  const editor_brush_source_prefab_desc *desc, const char *brush_name,
                                                  const int *source_min, const int *source_max, bool apply,
                                                  editor_brush_source_prefab_result *out_result, char *error_buffer,
                                                  int error_buffer_size)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || desc == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source prefab placement requires a source-backed brush world");
        return false;
    }

    const char *material_prefab = source_prefab_for_material(desc->material);
    const char *prefab = desc->prefab != NULL && desc->prefab[0] != '\0'
                             ? desc->prefab
                             : (material_prefab != NULL ? material_prefab : "");
    if (!source_prefab_name_supported(prefab))
    {
        set_errorf(error_buffer, error_buffer_size, "unsupported source prefab '%s'",
                   prefab != NULL && prefab[0] != '\0' ? prefab : "<empty>");
        return false;
    }
    if (desc->material == NULL || desc->material[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "source prefab placement requires a material");
        return false;
    }

    char generated_name[256];
    const char *name = brush_name;
    if (name == NULL || name[0] == '\0')
    {
        if (!editor_brush_world_generate_brush_name(world_runtime, generated_name, sizeof(generated_name)))
        {
            set_error(error_buffer, error_buffer_size, "failed to generate source prefab brush name");
            return false;
        }
        name = generated_name;
    }

    editor_brush_source_box_runtime source_box;
    if (source_min != NULL && source_max != NULL)
    {
        SDL_zero(source_box);
        if (!copy_source_string(&source_box.stable_id, name) || !copy_source_string(&source_box.name, name) ||
            !copy_source_string(&source_box.prefab, prefab) ||
            !copy_source_string(&source_box.material, desc->material))
        {
            free_editor_brush_source_box(&source_box);
            set_error(error_buffer, error_buffer_size, "failed to allocate source prefab brush");
            return false;
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            source_box.min[axis] = source_min[axis];
            source_box.max[axis] = source_max[axis];
        }
        source_box.contents = source_prefab_default_contents(prefab, desc->contents);
    }
    else if (!editor_brush_world_build_source_prefab_candidate(world_runtime, desc, name, &source_box, error_buffer,
                                                               error_buffer_size))
    {
        return false;
    }

    editor_brush_source_prefab_result preview_result;
    bool ok = editor_brush_world_preview_source_box_create(world_runtime, &source_box, 0, &preview_result, error_buffer,
                                                           error_buffer_size);
    if (ok && out_result != NULL)
    {
        const int existing_index = find_matching_source_prefab_cell(world_runtime, &source_box);
        if (existing_index >= 0)
        {
            const editor_brush_source_box_runtime *existing = &world_runtime->editor_source_boxes[existing_index];
            out_result->valid = true;
            out_result->no_op = true;
            SDL_strlcpy(out_result->brush_name,
                        existing->name != NULL && existing->name[0] != '\0' ? existing->name : name,
                        sizeof(out_result->brush_name));
            out_result->bounds = editor_brush_source_box_bounds_meters(world_runtime, existing);
            for (int axis = 0; axis < 3; ++axis)
            {
                out_result->source_min[axis] = existing->min[axis];
                out_result->source_max[axis] = existing->max[axis];
            }
            SDL_snprintf(out_result->warning, sizeof(out_result->warning),
                         "source brush '%s' already occupies this prefab cell",
                         out_result->brush_name[0] != '\0' ? out_result->brush_name : "<unnamed>");
            free_editor_brush_source_box(&source_box);
            return true;
        }
        *out_result = preview_result;
    }
    if (ok && apply)
    {
        ok = editor_brush_world_apply_source_box_create(world_runtime, &source_box, 0, &preview_result, error_buffer,
                                                        error_buffer_size);
    }
    if (ok && out_result != NULL)
    {
        *out_result = preview_result;
        SDL_strlcpy(out_result->brush_name, name, sizeof(out_result->brush_name));
    }
    free_editor_brush_source_box(&source_box);
    return ok;
}

static bool source_box_from_runtime_brush(const brush_world_runtime *world_runtime,
                                          const slayer3d_game_data_brush *brush,
                                          editor_brush_source_box_runtime *out_box)
{
    if (world_runtime == NULL || brush == NULL || out_box == NULL || !brush_can_sync_as_source_box(brush))
        return false;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
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
    out_box->min[0] = editor_brush_source_units_from_meters(world_runtime, brush->bounds.min.x);
    out_box->min[1] = editor_brush_source_units_from_meters(world_runtime, brush->bounds.min.y);
    out_box->min[2] = editor_brush_source_units_from_meters(world_runtime, brush->bounds.min.z);
    out_box->max[0] = editor_brush_source_units_from_meters(world_runtime, brush->bounds.max.x);
    out_box->max[1] = editor_brush_source_units_from_meters(world_runtime, brush->bounds.max.y);
    out_box->max[2] = editor_brush_source_units_from_meters(world_runtime, brush->bounds.max.z);
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
    if (!source_box_from_runtime_brush(world_runtime, brush, &inserted))
    {
        set_errorf(error_buffer, error_buffer_size, "brush '%s' cannot be represented as a source box",
                   brush->name != NULL ? brush->name : "<unnamed>");
        return false;
    }
    const bool ok = editor_brush_world_insert_source_box_at_index(world_runtime, box_index, &inserted, error_buffer,
                                                                  error_buffer_size);
    free_editor_brush_source_box(&inserted);
    return ok;
}

bool editor_brush_world_insert_source_box_at_index(brush_world_runtime *world_runtime, int box_index,
                                                   const editor_brush_source_box_runtime *box, char *error_buffer,
                                                   int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || box == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush insertion requires a source model and brush");
        return false;
    }

    editor_brush_source_box_runtime inserted;
    if (!copy_editor_brush_source_box_runtime(box, &inserted))
    {
        set_error(error_buffer, error_buffer_size, "failed to copy source brush insertion");
        return false;
    }
    if (!source_box_candidate_valid(world_runtime, &inserted, -1, error_buffer, error_buffer_size))
    {
        free_editor_brush_source_box(&inserted);
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

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    const int delta[3] = {editor_brush_source_units_from_meters(world_runtime, offset.x),
                          editor_brush_source_units_from_meters(world_runtime, offset.y),
                          editor_brush_source_units_from_meters(world_runtime, offset.z)};
    if (delta[0] == 0 && delta[1] == 0 && delta[2] == 0)
        return true;

    editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    int old_min[3];
    int old_max[3];
    copy_source_box_coordinates(box, old_min, old_max);
    translate_source_box_coordinates(box, delta);

    if (!source_box_candidate_valid(world_runtime, box, source_index, error_buffer, error_buffer_size))
    {
        const int rollback_delta[3] = {-delta[0], -delta[1], -delta[2]};
        translate_source_box_coordinates(box, rollback_delta);
        restore_source_box_coordinates(box, old_min, old_max);
        return false;
    }

    if (!editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        const int rollback_delta[3] = {-delta[0], -delta[1], -delta[2]};
        translate_source_box_coordinates(box, rollback_delta);
        restore_source_box_coordinates(box, old_min, old_max);
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }
    return true;
}

bool editor_brush_world_rotate_source_box_y_quarter_turns(brush_world_runtime *world_runtime, const char *brush_name,
                                                          int quarter_turns, char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush rotation requires a source model");
        return false;
    }

    const int normalized_turns = normalized_quarter_turns(quarter_turns);
    if (normalized_turns == 0)
        return true;

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    int old_min[3];
    int old_max[3];
    copy_source_box_coordinates(box, old_min, old_max);

    const int center_x2 = old_min[0] + old_max[0];
    const int center_z2 = old_min[2] + old_max[2];
    int new_min_x = INT_MAX;
    int new_max_x = INT_MIN;
    int new_min_z = INT_MAX;
    int new_max_z = INT_MIN;
    const int xs[2] = {old_min[0], old_max[0]};
    const int zs[2] = {old_min[2], old_max[2]};
    for (int xi = 0; xi < 2; ++xi)
    {
        for (int zi = 0; zi < 2; ++zi)
        {
            int rotated_x = 0;
            int rotated_z = 0;
            if (!rotate_source_coord_pair_y(xs[xi], zs[zi], center_x2, center_z2, normalized_turns, &rotated_x,
                                            &rotated_z))
            {
                set_error(error_buffer, error_buffer_size,
                          "source brush rotation would leave the brush off the integer source grid");
                return false;
            }
            new_min_x = SDL_min(new_min_x, rotated_x);
            new_max_x = SDL_max(new_max_x, rotated_x);
            new_min_z = SDL_min(new_min_z, rotated_z);
            new_max_z = SDL_max(new_max_z, rotated_z);
        }
    }

    box->min[0] = new_min_x;
    box->max[0] = new_max_x;
    box->min[2] = new_min_z;
    box->max[2] = new_max_z;
    for (int turn = 0; turn < normalized_turns; ++turn)
        rotate_source_box_face_materials_y_once(box);

    if (!source_box_candidate_valid(world_runtime, box, source_index, error_buffer, error_buffer_size))
    {
        restore_source_box_coordinates(box, old_min, old_max);
        for (int turn = 0; turn < 4 - normalized_turns; ++turn)
            rotate_source_box_face_materials_y_once(box);
        return false;
    }

    if (!editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        restore_source_box_coordinates(box, old_min, old_max);
        for (int turn = 0; turn < 4 - normalized_turns; ++turn)
            rotate_source_box_face_materials_y_once(box);
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }
    return true;
}

static bool source_rotation_coord(float value, int snap_units, int *out_coord)
{
    if (out_coord == NULL || SDL_isnan(value) || SDL_isinf(value))
        return false;
    if (snap_units <= 0)
        snap_units = 1;
    const float scaled = value / (float)snap_units;
    const long rounded_scaled = SDL_lroundf(scaled);
    if (rounded_scaled > INT_MAX / snap_units || rounded_scaled < INT_MIN / snap_units)
        return false;
    const int rounded = (int)rounded_scaled * snap_units;
    *out_coord = rounded;
    return true;
}

static bool rotate_source_vertex_coord(const int coord[3], const float pivot[3], slayer3d_vec3 axis,
                                       float angle_radians, int snap_units, int out_coord[3])
{
    if (coord == NULL || pivot == NULL || out_coord == NULL)
        return false;

    const float cos_angle = SDL_cosf(angle_radians);
    const float sin_angle = SDL_sinf(angle_radians);
    const slayer3d_vec3 relative =
        slayer3d_vec3_make((float)coord[0] - pivot[0], (float)coord[1] - pivot[1], (float)coord[2] - pivot[2]);
    const slayer3d_vec3 rotated =
        slayer3d_vec3_add(slayer3d_vec3_add(slayer3d_vec3_scale(relative, cos_angle),
                                            slayer3d_vec3_scale(slayer3d_vec3_cross(axis, relative), sin_angle)),
                          slayer3d_vec3_scale(axis, slayer3d_vec3_dot(axis, relative) * (1.0f - cos_angle)));
    return source_rotation_coord(rotated.x + pivot[0], snap_units, &out_coord[0]) &&
           source_rotation_coord(rotated.y + pivot[1], snap_units, &out_coord[1]) &&
           source_rotation_coord(rotated.z + pivot[2], snap_units, &out_coord[2]);
}

bool editor_brush_world_rotate_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                          slayer3d_vec3 pivot, slayer3d_vec3 axis, float angle_radians,
                                          char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush rotation requires a source model");
        return false;
    }
    if (SDL_isnan(pivot.x) || SDL_isinf(pivot.x) || SDL_isnan(pivot.y) || SDL_isinf(pivot.y) || SDL_isnan(pivot.z) ||
        SDL_isinf(pivot.z) || SDL_isnan(axis.x) || SDL_isinf(axis.x) || SDL_isnan(axis.y) || SDL_isinf(axis.y) ||
        SDL_isnan(axis.z) || SDL_isinf(axis.z) || SDL_isnan(angle_radians) || SDL_isinf(angle_radians))
    {
        set_error(error_buffer, error_buffer_size, "source brush rotation requires finite pivot, axis, and angle");
        return false;
    }
    if (SDL_fabsf(angle_radians) <= 0.000001f)
        return true;

    const float axis_len_sq = slayer3d_vec3_length_squared(axis);
    if (axis_len_sq <= 0.000001f)
    {
        set_error(error_buffer, error_buffer_size, "source brush rotation requires a non-zero axis");
        return false;
    }
    axis = slayer3d_vec3_normalize(axis);

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    editor_brush_source_vertex_model model;
    if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, error_buffer,
                                                    error_buffer_size))
    {
        return false;
    }

    const float pivot_source[3] = {
        pivot.x / (world_runtime->editor_source_meters_per_unit > 0.0f ? world_runtime->editor_source_meters_per_unit
                                                                       : 0.001f),
        pivot.y / (world_runtime->editor_source_meters_per_unit > 0.0f ? world_runtime->editor_source_meters_per_unit
                                                                       : 0.001f),
        pivot.z / (world_runtime->editor_source_meters_per_unit > 0.0f ? world_runtime->editor_source_meters_per_unit
                                                                       : 0.001f),
    };

    int rotated_vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    SDL_zeroa(rotated_vertices);
    const int snap_units = source_snap_units(world_runtime);
    for (int vertex = 0; vertex < model.vertex_count; ++vertex)
    {
        if (!rotate_source_vertex_coord(model.vertices[vertex].coord, pivot_source, axis, angle_radians, snap_units,
                                        rotated_vertices[vertex]))
        {
            set_error(error_buffer, error_buffer_size, "source brush rotation would overflow the source grid");
            return false;
        }
    }

    slayer3d_game_data_brush rebuilt;
    if (!editor_brush_world_build_source_convex_brush_from_vertices(world_runtime, brush_name, &rotated_vertices[0][0],
                                                                    model.vertex_count, &rebuilt, error_buffer,
                                                                    error_buffer_size))
    {
        return false;
    }
    editor_brush_source_free_runtime_brush(&rebuilt);

    editor_brush_source_box_runtime snapshot;
    SDL_zero(snapshot);
    editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    if (!copy_editor_brush_source_box_runtime(box, &snapshot))
    {
        set_error(error_buffer, error_buffer_size, "failed to snapshot source brush rotation");
        return false;
    }

    char *old_prefab = box->prefab;
    box->prefab = SDL_strdup("convex");
    if (box->prefab == NULL)
    {
        box->prefab = old_prefab;
        free_editor_brush_source_box(&snapshot);
        set_error(error_buffer, error_buffer_size, "failed to allocate source brush rotation metadata");
        return false;
    }
    SDL_free(old_prefab);
    box->vertex_count = model.vertex_count;
    for (int vertex = 0; vertex < model.vertex_count; ++vertex)
    {
        box->vertices[vertex][0] = rotated_vertices[vertex][0];
        box->vertices[vertex][1] = rotated_vertices[vertex][1];
        box->vertices[vertex][2] = rotated_vertices[vertex][2];
    }
    for (int vertex = model.vertex_count; vertex < SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY; ++vertex)
    {
        box->vertices[vertex][0] = 0;
        box->vertices[vertex][1] = 0;
        box->vertices[vertex][2] = 0;
    }
    source_box_update_bounds_from_vertices(box);

    if (!source_box_candidate_valid(world_runtime, box, source_index, error_buffer, error_buffer_size) ||
        !editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        free_editor_brush_source_box(box);
        *box = snapshot;
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }

    free_editor_brush_source_box(&snapshot);
    return true;
}

static bool scale_source_vertex_coord(const int coord[3], const float anchor[3], slayer3d_vec3 factors, int snap_units,
                                      int out_coord[3])
{
    if (coord == NULL || anchor == NULL || out_coord == NULL)
        return false;
    return source_rotation_coord(anchor[0] + ((float)coord[0] - anchor[0]) * factors.x, snap_units, &out_coord[0]) &&
           source_rotation_coord(anchor[1] + ((float)coord[1] - anchor[1]) * factors.y, snap_units, &out_coord[1]) &&
           source_rotation_coord(anchor[2] + ((float)coord[2] - anchor[2]) * factors.z, snap_units, &out_coord[2]);
}

static bool mirror_source_vertex_coord(const int coord[3], const float plane_point[3], slayer3d_vec3 plane_normal,
                                       int snap_units, int out_coord[3])
{
    if (coord == NULL || plane_point == NULL || out_coord == NULL)
        return false;

    const slayer3d_vec3 point = slayer3d_vec3_make((float)coord[0], (float)coord[1], (float)coord[2]);
    const slayer3d_vec3 origin = slayer3d_vec3_make(plane_point[0], plane_point[1], plane_point[2]);
    const float distance = slayer3d_vec3_dot(slayer3d_vec3_sub(point, origin), plane_normal);
    const slayer3d_vec3 mirrored = slayer3d_vec3_sub(point, slayer3d_vec3_scale(plane_normal, 2.0f * distance));
    return source_rotation_coord(mirrored.x, snap_units, &out_coord[0]) &&
           source_rotation_coord(mirrored.y, snap_units, &out_coord[1]) &&
           source_rotation_coord(mirrored.z, snap_units, &out_coord[2]);
}

static bool editor_source_box_apply_transformed_vertices(editor_brush_source_box_runtime *box, int vertex_count,
                                                         const int *transformed_vertices, const char *operation_name,
                                                         char *error_buffer, int error_buffer_size)
{
    if (box == NULL || vertex_count <= 0 || vertex_count > SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY ||
        transformed_vertices == NULL || operation_name == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source brush transform requires valid source vertices");
        return false;
    }

    char *old_prefab = box->prefab;
    box->prefab = SDL_strdup("convex");
    if (box->prefab == NULL)
    {
        box->prefab = old_prefab;
        set_errorf(error_buffer, error_buffer_size, "failed to allocate source brush %s metadata", operation_name);
        return false;
    }
    SDL_free(old_prefab);
    box->vertex_count = vertex_count;
    for (int vertex = 0; vertex < vertex_count; ++vertex)
    {
        box->vertices[vertex][0] = transformed_vertices[vertex * 3];
        box->vertices[vertex][1] = transformed_vertices[vertex * 3 + 1];
        box->vertices[vertex][2] = transformed_vertices[vertex * 3 + 2];
    }
    for (int vertex = vertex_count; vertex < SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY; ++vertex)
    {
        box->vertices[vertex][0] = 0;
        box->vertices[vertex][1] = 0;
        box->vertices[vertex][2] = 0;
    }
    source_box_update_bounds_from_vertices(box);
    return true;
}

static bool editor_brush_world_apply_transformed_source_vertices(brush_world_runtime *world_runtime, int source_index,
                                                                 const char *brush_name,
                                                                 const editor_brush_source_vertex_model *model,
                                                                 const int *transformed_vertices,
                                                                 const char *operation_name, char *error_buffer,
                                                                 int error_buffer_size)
{
    if (world_runtime == NULL || source_index < 0 || source_index >= world_runtime->editor_source_box_count ||
        brush_name == NULL || model == NULL || transformed_vertices == NULL || operation_name == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source brush transform requires valid source vertices");
        return false;
    }

    slayer3d_game_data_brush rebuilt;
    if (!editor_brush_world_build_source_convex_brush_from_vertices(world_runtime, brush_name, transformed_vertices,
                                                                    model->vertex_count, &rebuilt, error_buffer,
                                                                    error_buffer_size))
    {
        return false;
    }
    editor_brush_source_free_runtime_brush(&rebuilt);

    editor_brush_source_box_runtime snapshot;
    SDL_zero(snapshot);
    editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    if (!copy_editor_brush_source_box_runtime(box, &snapshot))
    {
        set_errorf(error_buffer, error_buffer_size, "failed to snapshot source brush %s", operation_name);
        return false;
    }

    if (!editor_source_box_apply_transformed_vertices(box, model->vertex_count, transformed_vertices, operation_name,
                                                      error_buffer, error_buffer_size))
    {
        free_editor_brush_source_box(&snapshot);
        return false;
    }

    if (!source_box_candidate_valid(world_runtime, box, source_index, error_buffer, error_buffer_size) ||
        !editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        free_editor_brush_source_box(box);
        *box = snapshot;
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }

    free_editor_brush_source_box(&snapshot);
    return true;
}

typedef struct editor_source_box_transform_batch_item
{
    int source_index;
    int vertex_count;
    int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    bool has_snapshot;
    editor_brush_source_box_runtime snapshot;
} editor_source_box_transform_batch_item;

static void editor_restore_source_box_transform_batch(brush_world_runtime *world_runtime,
                                                      editor_source_box_transform_batch_item *items, int item_count)
{
    if (world_runtime == NULL || items == NULL)
        return;
    for (int i = 0; i < item_count; ++i)
    {
        if (!items[i].has_snapshot || items[i].source_index < 0 ||
            items[i].source_index >= world_runtime->editor_source_box_count)
        {
            continue;
        }
        free_editor_brush_source_box(&world_runtime->editor_source_boxes[items[i].source_index]);
        world_runtime->editor_source_boxes[items[i].source_index] = items[i].snapshot;
        SDL_zero(items[i].snapshot);
        items[i].has_snapshot = false;
    }
}

static void editor_free_source_box_transform_batch(editor_source_box_transform_batch_item *items, int item_count)
{
    if (items == NULL)
        return;
    for (int i = 0; i < item_count; ++i)
    {
        if (items[i].has_snapshot)
            free_editor_brush_source_box(&items[i].snapshot);
    }
    SDL_free(items);
}

bool editor_brush_world_scale_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                         slayer3d_vec3 anchor, slayer3d_vec3 factors, char *error_buffer,
                                         int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush scale requires a source model");
        return false;
    }
    if (SDL_isnan(anchor.x) || SDL_isinf(anchor.x) || SDL_isnan(anchor.y) || SDL_isinf(anchor.y) ||
        SDL_isnan(anchor.z) || SDL_isinf(anchor.z) || SDL_isnan(factors.x) || SDL_isinf(factors.x) ||
        SDL_isnan(factors.y) || SDL_isinf(factors.y) || SDL_isnan(factors.z) || SDL_isinf(factors.z))
    {
        set_error(error_buffer, error_buffer_size, "source brush scale requires finite anchor and factors");
        return false;
    }
    if (factors.x <= 0.000001f || factors.y <= 0.000001f || factors.z <= 0.000001f)
    {
        set_error(error_buffer, error_buffer_size, "source brush scale requires positive factors");
        return false;
    }
    if (SDL_fabsf(factors.x - 1.0f) <= 0.000001f && SDL_fabsf(factors.y - 1.0f) <= 0.000001f &&
        SDL_fabsf(factors.z - 1.0f) <= 0.000001f)
    {
        return true;
    }

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    editor_brush_source_vertex_model model;
    if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, error_buffer,
                                                    error_buffer_size))
    {
        return false;
    }

    const float meters_per_unit =
        world_runtime->editor_source_meters_per_unit > 0.0f ? world_runtime->editor_source_meters_per_unit : 0.001f;
    const float anchor_source[3] = {
        anchor.x / meters_per_unit,
        anchor.y / meters_per_unit,
        anchor.z / meters_per_unit,
    };

    int scaled_vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    SDL_zeroa(scaled_vertices);
    const int snap_units = source_snap_units(world_runtime);
    for (int vertex = 0; vertex < model.vertex_count; ++vertex)
    {
        if (!scale_source_vertex_coord(model.vertices[vertex].coord, anchor_source, factors, snap_units,
                                       scaled_vertices[vertex]))
        {
            set_error(error_buffer, error_buffer_size, "source brush scale would overflow the source grid");
            return false;
        }
    }

    slayer3d_game_data_brush rebuilt;
    if (!editor_brush_world_build_source_convex_brush_from_vertices(world_runtime, brush_name, &scaled_vertices[0][0],
                                                                    model.vertex_count, &rebuilt, error_buffer,
                                                                    error_buffer_size))
    {
        return false;
    }
    editor_brush_source_free_runtime_brush(&rebuilt);

    editor_brush_source_box_runtime snapshot;
    SDL_zero(snapshot);
    editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    if (!copy_editor_brush_source_box_runtime(box, &snapshot))
    {
        set_error(error_buffer, error_buffer_size, "failed to snapshot source brush scale");
        return false;
    }

    char *old_prefab = box->prefab;
    box->prefab = SDL_strdup("convex");
    if (box->prefab == NULL)
    {
        box->prefab = old_prefab;
        free_editor_brush_source_box(&snapshot);
        set_error(error_buffer, error_buffer_size, "failed to allocate source brush scale metadata");
        return false;
    }
    SDL_free(old_prefab);
    box->vertex_count = model.vertex_count;
    for (int vertex = 0; vertex < model.vertex_count; ++vertex)
    {
        box->vertices[vertex][0] = scaled_vertices[vertex][0];
        box->vertices[vertex][1] = scaled_vertices[vertex][1];
        box->vertices[vertex][2] = scaled_vertices[vertex][2];
    }
    for (int vertex = model.vertex_count; vertex < SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY; ++vertex)
    {
        box->vertices[vertex][0] = 0;
        box->vertices[vertex][1] = 0;
        box->vertices[vertex][2] = 0;
    }
    source_box_update_bounds_from_vertices(box);

    if (!source_box_candidate_valid(world_runtime, box, source_index, error_buffer, error_buffer_size) ||
        !editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        free_editor_brush_source_box(box);
        *box = snapshot;
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }

    free_editor_brush_source_box(&snapshot);
    return true;
}

bool editor_brush_world_mirror_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                          slayer3d_vec3 plane_point, slayer3d_vec3 plane_normal, char *error_buffer,
                                          int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush mirror requires a source model");
        return false;
    }
    if (SDL_isnan(plane_point.x) || SDL_isinf(plane_point.x) || SDL_isnan(plane_point.y) || SDL_isinf(plane_point.y) ||
        SDL_isnan(plane_point.z) || SDL_isinf(plane_point.z) || SDL_isnan(plane_normal.x) ||
        SDL_isinf(plane_normal.x) || SDL_isnan(plane_normal.y) || SDL_isinf(plane_normal.y) ||
        SDL_isnan(plane_normal.z) || SDL_isinf(plane_normal.z))
    {
        set_error(error_buffer, error_buffer_size, "source brush mirror requires finite plane point and normal");
        return false;
    }
    if (slayer3d_vec3_length_squared(plane_normal) <= 0.000001f)
    {
        set_error(error_buffer, error_buffer_size, "source brush mirror requires a non-zero plane normal");
        return false;
    }
    plane_normal = slayer3d_vec3_normalize(plane_normal);

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    editor_brush_source_vertex_model model;
    if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, error_buffer,
                                                    error_buffer_size))
    {
        return false;
    }

    const float meters_per_unit =
        world_runtime->editor_source_meters_per_unit > 0.0f ? world_runtime->editor_source_meters_per_unit : 0.001f;
    const float plane_point_source[3] = {
        plane_point.x / meters_per_unit,
        plane_point.y / meters_per_unit,
        plane_point.z / meters_per_unit,
    };

    int mirrored_vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    SDL_zeroa(mirrored_vertices);
    const int snap_units = source_snap_units(world_runtime);
    for (int vertex = 0; vertex < model.vertex_count; ++vertex)
    {
        if (!mirror_source_vertex_coord(model.vertices[vertex].coord, plane_point_source, plane_normal, snap_units,
                                        mirrored_vertices[vertex]))
        {
            set_error(error_buffer, error_buffer_size, "source brush mirror would overflow the source grid");
            return false;
        }
    }

    return editor_brush_world_apply_transformed_source_vertices(world_runtime, source_index, brush_name, &model,
                                                                &mirrored_vertices[0][0], "mirror", error_buffer,
                                                                error_buffer_size);
}

bool editor_brush_world_mirror_source_boxes(brush_world_runtime *world_runtime, const char *const *brush_identities,
                                            int brush_count, slayer3d_vec3 plane_point, slayer3d_vec3 plane_normal,
                                            char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush mirror requires a source model");
        return false;
    }
    if (brush_identities == NULL || brush_count <= 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush mirror requires selected brushes");
        return false;
    }
    if (SDL_isnan(plane_point.x) || SDL_isinf(plane_point.x) || SDL_isnan(plane_point.y) || SDL_isinf(plane_point.y) ||
        SDL_isnan(plane_point.z) || SDL_isinf(plane_point.z) || SDL_isnan(plane_normal.x) ||
        SDL_isinf(plane_normal.x) || SDL_isnan(plane_normal.y) || SDL_isinf(plane_normal.y) ||
        SDL_isnan(plane_normal.z) || SDL_isinf(plane_normal.z))
    {
        set_error(error_buffer, error_buffer_size, "source brush mirror requires finite plane point and normal");
        return false;
    }
    if (slayer3d_vec3_length_squared(plane_normal) <= 0.000001f)
    {
        set_error(error_buffer, error_buffer_size, "source brush mirror requires a non-zero plane normal");
        return false;
    }
    plane_normal = slayer3d_vec3_normalize(plane_normal);

    editor_source_box_transform_batch_item *items =
        (editor_source_box_transform_batch_item *)SDL_calloc((size_t)brush_count, sizeof(*items));
    if (items == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate source brush mirror batch");
        return false;
    }
    for (int i = 0; i < brush_count; ++i)
        items[i].source_index = -1;

    const float meters_per_unit =
        world_runtime->editor_source_meters_per_unit > 0.0f ? world_runtime->editor_source_meters_per_unit : 0.001f;
    const float plane_point_source[3] = {
        plane_point.x / meters_per_unit,
        plane_point.y / meters_per_unit,
        plane_point.z / meters_per_unit,
    };
    const int snap_units = source_snap_units(world_runtime);

    for (int i = 0; i < brush_count; ++i)
    {
        const char *brush_identity = brush_identities[i];
        if (brush_identity == NULL || brush_identity[0] == '\0')
        {
            set_error(error_buffer, error_buffer_size, "source brush mirror requires brush identities");
            goto fail;
        }

        const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_identity);
        if (source_index < 0)
        {
            set_error(error_buffer, error_buffer_size, "source brush not found");
            goto fail;
        }
        for (int previous = 0; previous < i; ++previous)
        {
            if (items[previous].source_index == source_index)
            {
                set_error(error_buffer, error_buffer_size, "source brush mirror batch contains duplicate brushes");
                goto fail;
            }
        }

        editor_brush_source_vertex_model model;
        if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, error_buffer,
                                                        error_buffer_size))
        {
            goto fail;
        }

        items[i].source_index = source_index;
        items[i].vertex_count = model.vertex_count;
        for (int vertex = 0; vertex < model.vertex_count; ++vertex)
        {
            if (!mirror_source_vertex_coord(model.vertices[vertex].coord, plane_point_source, plane_normal, snap_units,
                                            items[i].vertices[vertex]))
            {
                set_error(error_buffer, error_buffer_size, "source brush mirror would overflow the source grid");
                goto fail;
            }
        }

        slayer3d_game_data_brush rebuilt;
        if (!editor_brush_world_build_source_convex_brush_from_vertices(world_runtime, brush_identity,
                                                                        &items[i].vertices[0][0], model.vertex_count,
                                                                        &rebuilt, error_buffer, error_buffer_size))
        {
            goto fail;
        }
        editor_brush_source_free_runtime_brush(&rebuilt);

        if (!copy_editor_brush_source_box_runtime(&world_runtime->editor_source_boxes[source_index],
                                                  &items[i].snapshot))
        {
            set_error(error_buffer, error_buffer_size, "failed to snapshot source brush mirror");
            goto fail;
        }
        items[i].has_snapshot = true;
    }

    for (int i = 0; i < brush_count; ++i)
    {
        editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[items[i].source_index];
        if (!editor_source_box_apply_transformed_vertices(box, items[i].vertex_count, &items[i].vertices[0][0],
                                                          "mirror", error_buffer, error_buffer_size))
        {
            goto fail_restore;
        }
    }
    for (int i = 0; i < brush_count; ++i)
    {
        editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[items[i].source_index];
        if (!source_box_candidate_valid(world_runtime, box, items[i].source_index, error_buffer, error_buffer_size))
            goto fail_restore;
    }
    if (!editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
        goto fail_restore;

    editor_free_source_box_transform_batch(items, brush_count);
    return true;

fail_restore:
    editor_restore_source_box_transform_batch(world_runtime, items, brush_count);
    (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
fail:
    editor_free_source_box_transform_batch(items, brush_count);
    return false;
}

static bool shear_source_side_axis(slayer3d_vec3 side_normal, int *out_axis, float *out_sign)
{
    if (out_axis != NULL)
        *out_axis = -1;
    if (out_sign != NULL)
        *out_sign = 0.0f;
    const float ax = SDL_fabsf(side_normal.x);
    const float ay = SDL_fabsf(side_normal.y);
    const float az = SDL_fabsf(side_normal.z);
    if (ax > 0.5f && ay <= 0.0001f && az <= 0.0001f)
    {
        if (out_axis != NULL)
            *out_axis = 0;
        if (out_sign != NULL)
            *out_sign = side_normal.x < 0.0f ? -1.0f : 1.0f;
        return true;
    }
    if (ay > 0.5f && ax <= 0.0001f && az <= 0.0001f)
    {
        if (out_axis != NULL)
            *out_axis = 1;
        if (out_sign != NULL)
            *out_sign = side_normal.y < 0.0f ? -1.0f : 1.0f;
        return true;
    }
    if (az > 0.5f && ax <= 0.0001f && ay <= 0.0001f)
    {
        if (out_axis != NULL)
            *out_axis = 2;
        if (out_sign != NULL)
            *out_sign = side_normal.z < 0.0f ? -1.0f : 1.0f;
        return true;
    }
    return false;
}

static bool shear_source_vertex_coord(const int coord[3], const float bounds_min[3], const float bounds_max[3],
                                      int axis, float sign, slayer3d_vec3 delta_source, int snap_units,
                                      int out_coord[3])
{
    if (coord == NULL || bounds_min == NULL || bounds_max == NULL || out_coord == NULL || axis < 0 || axis > 2)
        return false;
    const float axis_min = bounds_min[axis];
    const float axis_max = bounds_max[axis];
    const float axis_size = axis_max - axis_min;
    if (axis_size <= 0.000001f)
        return false;
    const float coord_axis = (float)coord[axis];
    float t = sign > 0.0f ? (coord_axis - axis_min) / axis_size : (axis_max - coord_axis) / axis_size;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;

    return source_rotation_coord((float)coord[0] + delta_source.x * t, snap_units, &out_coord[0]) &&
           source_rotation_coord((float)coord[1] + delta_source.y * t, snap_units, &out_coord[1]) &&
           source_rotation_coord((float)coord[2] + delta_source.z * t, snap_units, &out_coord[2]);
}

bool editor_brush_world_shear_source_box(brush_world_runtime *world_runtime, const char *brush_name,
                                         slayer3d_bounding_box bounds, slayer3d_vec3 side_normal, slayer3d_vec3 delta,
                                         char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush shear requires a source model");
        return false;
    }
    if (SDL_isnan(bounds.min.x) || SDL_isinf(bounds.min.x) || SDL_isnan(bounds.min.y) || SDL_isinf(bounds.min.y) ||
        SDL_isnan(bounds.min.z) || SDL_isinf(bounds.min.z) || SDL_isnan(bounds.max.x) || SDL_isinf(bounds.max.x) ||
        SDL_isnan(bounds.max.y) || SDL_isinf(bounds.max.y) || SDL_isnan(bounds.max.z) || SDL_isinf(bounds.max.z) ||
        SDL_isnan(side_normal.x) || SDL_isinf(side_normal.x) || SDL_isnan(side_normal.y) || SDL_isinf(side_normal.y) ||
        SDL_isnan(side_normal.z) || SDL_isinf(side_normal.z) || SDL_isnan(delta.x) || SDL_isinf(delta.x) ||
        SDL_isnan(delta.y) || SDL_isinf(delta.y) || SDL_isnan(delta.z) || SDL_isinf(delta.z))
    {
        set_error(error_buffer, error_buffer_size, "source brush shear requires finite bounds, side, and delta");
        return false;
    }

    int axis = -1;
    float sign = 0.0f;
    if (!shear_source_side_axis(side_normal, &axis, &sign))
    {
        set_error(error_buffer, error_buffer_size, "source brush shear requires an axis-aligned side normal");
        return false;
    }
    if ((axis == 0 && bounds.max.x - bounds.min.x <= 0.000001f) ||
        (axis == 1 && bounds.max.y - bounds.min.y <= 0.000001f) ||
        (axis == 2 && bounds.max.z - bounds.min.z <= 0.000001f))
    {
        set_error(error_buffer, error_buffer_size, "source brush shear requires non-empty bounds");
        return false;
    }

    if (axis == 0)
        delta.x = 0.0f;
    else if (axis == 1)
        delta.y = 0.0f;
    else
        delta.z = 0.0f;
    if (slayer3d_vec3_length_squared(delta) <= 0.0000001f)
        return true;

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    editor_brush_source_vertex_model model;
    if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, error_buffer,
                                                    error_buffer_size))
    {
        return false;
    }

    const float meters_per_unit =
        world_runtime->editor_source_meters_per_unit > 0.0f ? world_runtime->editor_source_meters_per_unit : 0.001f;
    const float bounds_min[3] = {
        bounds.min.x / meters_per_unit,
        bounds.min.y / meters_per_unit,
        bounds.min.z / meters_per_unit,
    };
    const float bounds_max[3] = {
        bounds.max.x / meters_per_unit,
        bounds.max.y / meters_per_unit,
        bounds.max.z / meters_per_unit,
    };
    const slayer3d_vec3 delta_source =
        slayer3d_vec3_make(delta.x / meters_per_unit, delta.y / meters_per_unit, delta.z / meters_per_unit);

    int sheared_vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    SDL_zeroa(sheared_vertices);
    const int snap_units = source_snap_units(world_runtime);
    for (int vertex = 0; vertex < model.vertex_count; ++vertex)
    {
        if (!shear_source_vertex_coord(model.vertices[vertex].coord, bounds_min, bounds_max, axis, sign, delta_source,
                                       snap_units, sheared_vertices[vertex]))
        {
            set_error(error_buffer, error_buffer_size, "source brush shear would overflow the source grid");
            return false;
        }
    }

    slayer3d_game_data_brush rebuilt;
    if (!editor_brush_world_build_source_convex_brush_from_vertices(world_runtime, brush_name, &sheared_vertices[0][0],
                                                                    model.vertex_count, &rebuilt, error_buffer,
                                                                    error_buffer_size))
    {
        return false;
    }
    editor_brush_source_free_runtime_brush(&rebuilt);

    editor_brush_source_box_runtime snapshot;
    SDL_zero(snapshot);
    editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    if (!copy_editor_brush_source_box_runtime(box, &snapshot))
    {
        set_error(error_buffer, error_buffer_size, "failed to snapshot source brush shear");
        return false;
    }

    char *old_prefab = box->prefab;
    box->prefab = SDL_strdup("convex");
    if (box->prefab == NULL)
    {
        box->prefab = old_prefab;
        free_editor_brush_source_box(&snapshot);
        set_error(error_buffer, error_buffer_size, "failed to allocate source brush shear metadata");
        return false;
    }
    SDL_free(old_prefab);
    box->vertex_count = model.vertex_count;
    for (int vertex = 0; vertex < model.vertex_count; ++vertex)
    {
        box->vertices[vertex][0] = sheared_vertices[vertex][0];
        box->vertices[vertex][1] = sheared_vertices[vertex][1];
        box->vertices[vertex][2] = sheared_vertices[vertex][2];
    }
    for (int vertex = model.vertex_count; vertex < SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY; ++vertex)
    {
        box->vertices[vertex][0] = 0;
        box->vertices[vertex][1] = 0;
        box->vertices[vertex][2] = 0;
    }
    source_box_update_bounds_from_vertices(box);

    if (!source_box_candidate_valid(world_runtime, box, source_index, error_buffer, error_buffer_size) ||
        !editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
    {
        free_editor_brush_source_box(box);
        *box = snapshot;
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }

    free_editor_brush_source_box(&snapshot);
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

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    const int delta = editor_brush_source_units_from_meters(world_runtime, distance);
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

    if (!source_box_candidate_valid(world_runtime, box, source_index, error_buffer, error_buffer_size))
    {
        restore_source_box_coordinates(box, old_min, old_max);
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

static bool source_face_resize_desc(const brush_world_runtime *world_runtime, const char *brush_name,
                                    int fallback_face_index, const char *face_identity, float distance,
                                    editor_brush_source_vertex_operation_desc *out_desc,
                                    slayer3d_game_data_brush *out_brush, int *out_original_vertex_count,
                                    char *error_buffer, int error_buffer_size)
{
    if (out_desc != NULL)
        SDL_zero(*out_desc);
    if (out_brush != NULL)
        SDL_zero(*out_brush);
    if (out_original_vertex_count != NULL)
        *out_original_vertex_count = 0;
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || brush_name == NULL ||
        brush_name[0] == '\0' || out_desc == NULL || out_brush == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source-backed face resize requires a source brush");
        return false;
    }

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_name);
    if (source_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "source brush not found");
        return false;
    }

    const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    editor_brush_source_vertex_model model;
    if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, error_buffer,
                                                    error_buffer_size) ||
        !source_box_to_runtime_brush(world_runtime, box, out_brush, error_buffer, error_buffer_size))
    {
        return false;
    }

    const int source_face_index = editor_brush_world_source_box_face_index_for_identity(
        world_runtime, brush_name, fallback_face_index, face_identity);
    const int runtime_face_index = runtime_brush_face_index_for_identity(out_brush, face_identity, source_face_index);
    if (source_face_index < 0 || runtime_face_index < 0 || runtime_face_index >= out_brush->face_count)
    {
        editor_brush_source_free_runtime_brush(out_brush);
        set_error(error_buffer, error_buffer_size, "source brush face not found");
        return false;
    }
    int model_face_index = source_face_index;
    if (box->vertex_count > 0)
    {
        model_face_index = -1;
        const slayer3d_game_data_brush_face *runtime_face = &out_brush->faces[runtime_face_index];
        for (int face_index = 0; face_index < model.face_count; ++face_index)
        {
            const editor_brush_source_face_ref *face = &model.faces[face_index];
            bool matches = face->vertex_count > 0;
            for (int vertex = 0; matches && vertex < face->vertex_count; ++vertex)
            {
                const int vertex_index = face->vertex_indices[vertex];
                if (vertex_index < 0 || vertex_index >= model.vertex_count)
                {
                    matches = false;
                    break;
                }
                const slayer3d_vec3 point =
                    source_convex_vertex_meters(world_runtime, &model.vertices[vertex_index].coord[0]);
                matches = SDL_fabsf(slayer3d_vec3_dot(runtime_face->normal, point) - runtime_face->distance) <= 0.001f;
            }
            if (matches)
            {
                model_face_index = face_index;
                break;
            }
        }
    }
    if (model_face_index < 0 || model_face_index >= model.face_count)
    {
        editor_brush_source_free_runtime_brush(out_brush);
        set_error(error_buffer, error_buffer_size, "source brush face vertices not found");
        return false;
    }
    if (out_original_vertex_count != NULL)
        *out_original_vertex_count = model.vertex_count;

    const int distance_units = editor_brush_source_units_from_meters(world_runtime, distance);
    const slayer3d_vec3 normal = slayer3d_vec3_normalize(out_brush->faces[runtime_face_index].normal);
    int delta[3] = {
        (int)SDL_lroundf(normal.x * (float)distance_units),
        (int)SDL_lroundf(normal.y * (float)distance_units),
        (int)SDL_lroundf(normal.z * (float)distance_units),
    };
    if (delta[0] == 0 && delta[1] == 0 && delta[2] == 0)
    {
        editor_brush_source_free_runtime_brush(out_brush);
        set_error(error_buffer, error_buffer_size, "source brush face resize distance did not reach the source grid");
        return false;
    }

    out_desc->brush_identity = brush_name;
    out_desc->type = EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MOVE_MANY;
    const editor_brush_source_face_ref *face = &model.faces[model_face_index];
    for (int i = 0; i < face->vertex_count; ++i)
    {
        const int vertex_index = face->vertex_indices[i];
        if (vertex_index < 0 || vertex_index >= model.vertex_count ||
            out_desc->vertex_index_count >= SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY)
        {
            editor_brush_source_free_runtime_brush(out_brush);
            set_error(error_buffer, error_buffer_size, "source brush face resize vertex index out of range");
            return false;
        }
        out_desc->vertex_indices[out_desc->vertex_index_count] = vertex_index;
        for (int axis = 0; axis < 3; ++axis)
            out_desc->coords[out_desc->vertex_index_count][axis] =
                model.vertices[vertex_index].coord[axis] + delta[axis];
        out_desc->vertex_index_count++;
    }
    return true;
}

bool editor_brush_world_preview_resize_source_face(const brush_world_runtime *world_runtime, const char *brush_name,
                                                   int fallback_face_index, const char *face_identity, float distance,
                                                   editor_brush_source_vertex_operation_result *out_result,
                                                   char *error_buffer, int error_buffer_size)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    editor_brush_source_vertex_operation_desc desc;
    slayer3d_game_data_brush brush;
    int original_vertex_count = 0;
    if (!source_face_resize_desc(world_runtime, brush_name, fallback_face_index, face_identity, distance, &desc, &brush,
                                 &original_vertex_count, error_buffer, error_buffer_size))
    {
        return false;
    }
    editor_brush_source_free_runtime_brush(&brush);
    if (!editor_brush_world_preview_source_vertex_operation(world_runtime, &desc, out_result, error_buffer,
                                                            error_buffer_size))
    {
        return false;
    }
    if (out_result != NULL && out_result->valid && out_result->vertex_count != original_vertex_count)
    {
        editor_brush_source_free_runtime_brush(&out_result->brush);
        source_vertex_operation_set_failure(out_result, "source brush face resize would collapse vertices");
        set_error(error_buffer, error_buffer_size, "source brush face resize would collapse vertices");
        return false;
    }
    return true;
}

bool editor_brush_world_resize_source_face(brush_world_runtime *world_runtime, const char *brush_name,
                                           int fallback_face_index, const char *face_identity, float distance,
                                           editor_brush_source_vertex_operation_result *out_result, char *error_buffer,
                                           int error_buffer_size)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    editor_brush_source_vertex_operation_desc desc;
    slayer3d_game_data_brush brush;
    int original_vertex_count = 0;
    if (!source_face_resize_desc(world_runtime, brush_name, fallback_face_index, face_identity, distance, &desc, &brush,
                                 &original_vertex_count, error_buffer, error_buffer_size))
    {
        return false;
    }
    editor_brush_source_free_runtime_brush(&brush);
    editor_brush_source_vertex_operation_result result;
    if (!editor_brush_world_preview_source_vertex_operation(world_runtime, &desc, &result, error_buffer,
                                                            error_buffer_size))
    {
        return false;
    }
    if (result.valid && result.vertex_count != original_vertex_count)
    {
        editor_brush_source_free_runtime_brush(&result.brush);
        if (out_result != NULL)
            source_vertex_operation_set_failure(out_result, "source brush face resize would collapse vertices");
        set_error(error_buffer, error_buffer_size, "source brush face resize would collapse vertices");
        return false;
    }
    editor_brush_source_free_runtime_brush(&result.brush);
    return editor_brush_world_apply_source_vertex_operation(world_runtime, &desc, out_result, error_buffer,
                                                            error_buffer_size);
}

bool editor_brush_world_update_source_box_bounds_batch(brush_world_runtime *world_runtime,
                                                       const editor_source_box_bounds_update *updates, int update_count,
                                                       char *error_buffer, int error_buffer_size)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source-backed brush edit requires a source model");
        return false;
    }
    if (updates == NULL || update_count <= 0)
        return true;

    int *old_min = (int *)SDL_calloc((size_t)update_count * 3u, sizeof(*old_min));
    int *old_max = (int *)SDL_calloc((size_t)update_count * 3u, sizeof(*old_max));
    if (old_min == NULL || old_max == NULL)
    {
        SDL_free(old_min);
        SDL_free(old_max);
        set_error(error_buffer, error_buffer_size, "failed to allocate source brush edit rollback");
        return false;
    }

    bool ok = true;
    int applied_count = 0;
    for (int i = 0; ok && i < update_count; ++i)
    {
        const int source_index = updates[i].source_index;
        if (source_index < 0 || source_index >= world_runtime->editor_source_box_count)
        {
            set_error(error_buffer, error_buffer_size, "source brush index out of range");
            ok = false;
            break;
        }
        for (int previous = 0; previous < i; ++previous)
        {
            if (updates[previous].source_index == source_index)
            {
                set_error(error_buffer, error_buffer_size, "source brush edit contains duplicate source indices");
                ok = false;
                break;
            }
        }
        if (!ok)
            break;
        editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
        copy_source_box_coordinates(box, &old_min[i * 3], &old_max[i * 3]);
        for (int axis = 0; axis < 3; ++axis)
        {
            box->min[axis] = updates[i].min[axis];
            box->max[axis] = updates[i].max[axis];
        }
        applied_count++;
    }

    for (int i = 0; ok && i < update_count; ++i)
    {
        const int source_index = updates[i].source_index;
        if (!source_box_candidate_valid(world_runtime, &world_runtime->editor_source_boxes[source_index], source_index,
                                        error_buffer, error_buffer_size))
        {
            ok = false;
        }
    }

    if (ok && !editor_brush_world_rebuild_from_source(world_runtime, error_buffer, error_buffer_size))
        ok = false;

    if (!ok)
    {
        for (int i = 0; i < applied_count; ++i)
        {
            const int source_index = updates[i].source_index;
            if (source_index >= 0 && source_index < world_runtime->editor_source_box_count)
            {
                restore_source_box_coordinates(&world_runtime->editor_source_boxes[source_index], &old_min[i * 3],
                                               &old_max[i * 3]);
            }
        }
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
    }

    SDL_free(old_min);
    SDL_free(old_max);
    return ok;
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
    if (face_index < 0 || face_index >= (int)SDL_arraysize(editor_brush_source_box_face_keys))
    {
        set_error(error_buffer, error_buffer_size, "source brush face index out of range");
        return false;
    }
    if (source_material_index_by_name(&world_runtime->desc, material_name) < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "source brush material '%s' not found", material_name);
        return false;
    }

    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_name);
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

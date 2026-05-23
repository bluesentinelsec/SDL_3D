/**
 * @file game_data_brush_source_model.c
 * @brief Runtime-owned structural brush source model helpers for editor tooling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include <limits.h>
#include <stdlib.h>

static int source_units_from_meters(const brush_world_runtime *world_runtime, float value)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return (int)SDL_lroundf(value / meters_per_unit);
}

static float source_meters_from_units(const brush_world_runtime *world_runtime, int value)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return (float)value * meters_per_unit;
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

void free_editor_brush_source_box_runtime(editor_brush_source_box_runtime *box)
{
    free_editor_brush_source_box(box);
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
    world_runtime->editor_source_snap_units = 1;
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

bool copy_editor_brush_source_box_runtime(const editor_brush_source_box_runtime *source,
                                          editor_brush_source_box_runtime *dest)
{
    if (source == NULL || dest == NULL)
        return false;
    SDL_zero(*dest);
    if (!copy_source_string(&dest->stable_id, source->stable_id) || !copy_source_string(&dest->name, source->name) ||
        !copy_source_string(&dest->prefab, source->prefab) || !copy_source_string(&dest->material, source->material))
    {
        free_editor_brush_source_box(dest);
        return false;
    }
    for (int i = 0; i < 6; ++i)
    {
        if (!copy_source_string(&dest->face_materials[i], source->face_materials[i]))
        {
            free_editor_brush_source_box(dest);
            return false;
        }
    }
    for (int i = 0; i < 3; ++i)
    {
        dest->min[i] = source->min[i];
        dest->max[i] = source->max[i];
    }
    dest->contents = source->contents;
    return true;
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
    if (box == NULL || face_index < 0 || face_index >= (int)SDL_arraysize(source_box_face_keys) ||
        face_identity == NULL || face_identity[0] == '\0')
    {
        return false;
    }

    const char *face_key = source_box_face_keys[face_index];
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

int editor_brush_world_source_box_face_index_for_identity(const brush_world_runtime *world_runtime,
                                                          const char *brush_identity, int fallback_face_index,
                                                          const char *face_identity)
{
    const int source_index = find_editor_source_box_index_by_identity(world_runtime, brush_identity);
    if (source_index < 0)
        return -1;
    const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    for (int i = 0; i < (int)SDL_arraysize(source_box_face_keys); ++i)
    {
        if (source_box_face_identity_matches(box, i, face_identity))
            return i;
    }
    return fallback_face_index >= 0 && fallback_face_index < (int)SDL_arraysize(source_box_face_keys)
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
        *out_normal = source_box_face_normal_for_index(face_index);
    return face_index >= 0;
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
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!source_coord_on_snap(box->min[axis], snap_units) || !source_coord_on_snap(box->max[axis], snap_units))
            return false;
    }
    return true;
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

static bool source_box_contents_are_structural(unsigned int contents)
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
        !source_box_contents_are_structural(candidate->contents))
    {
        return;
    }

    const char *first_overlap = NULL;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
    {
        if (i == exclude_index)
            continue;
        const editor_brush_source_box_runtime *existing = &world_runtime->editor_source_boxes[i];
        if (!source_box_contents_are_structural(existing->contents) ||
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
    const int min_values[3] = {source_units_from_meters(world_runtime, brush->bounds.min.x),
                               source_units_from_meters(world_runtime, brush->bounds.min.y),
                               source_units_from_meters(world_runtime, brush->bounds.min.z)};
    const int max_values[3] = {source_units_from_meters(world_runtime, brush->bounds.max.x),
                               source_units_from_meters(world_runtime, brush->bounds.max.y),
                               source_units_from_meters(world_runtime, brush->bounds.max.z)};
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
            if (!source_box_contents_are_structural(a->contents) || !source_box_contents_are_structural(b->contents))
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

static int compare_source_ints(const void *a, const void *b)
{
    const int ai = *(const int *)a;
    const int bi = *(const int *)b;
    return (ai > bi) - (ai < bi);
}

static bool source_int_array_append_unique(int **values, int *count, int *capacity, int value)
{
    if (values == NULL || count == NULL || capacity == NULL)
        return false;
    for (int i = 0; i < *count; ++i)
    {
        if ((*values)[i] == value)
            return true;
    }
    if (*count >= *capacity)
    {
        const int next_capacity = *capacity > 0 ? *capacity * 2 : 16;
        int *next_values = (int *)SDL_realloc(*values, (size_t)next_capacity * sizeof(*next_values));
        if (next_values == NULL)
            return false;
        *values = next_values;
        *capacity = next_capacity;
    }
    (*values)[*count] = value;
    (*count)++;
    return true;
}

static bool source_bounds_include_box(const editor_brush_source_box_runtime *box, int bounds_min[3], int bounds_max[3])
{
    if (box == NULL || bounds_min == NULL || bounds_max == NULL)
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        bounds_min[axis] = SDL_min(bounds_min[axis], box->min[axis]);
        bounds_max[axis] = SDL_max(bounds_max[axis], box->max[axis]);
    }
    return true;
}

static int source_cell_index_for_coord(const int *edges, int edge_count, int coord)
{
    if (edges == NULL || edge_count < 2)
        return -1;
    for (int i = 0; i < edge_count - 1; ++i)
    {
        if (coord >= edges[i] && coord < edges[i + 1])
            return i;
    }
    return coord == edges[edge_count - 1] ? edge_count - 2 : -1;
}

static bool source_cell_inside_box(const int *x_edges, const int *y_edges, const int *z_edges, int x, int y, int z,
                                   const editor_brush_source_box_runtime *box)
{
    return box != NULL && x_edges[x] >= box->min[0] && x_edges[x + 1] <= box->max[0] && y_edges[y] >= box->min[1] &&
           y_edges[y + 1] <= box->max[1] && z_edges[z] >= box->min[2] && z_edges[z + 1] <= box->max[2];
}

static int source_grid_cell_index(int x, int y, int z, int dim_x, int dim_y)
{
    return (z * dim_y + y) * dim_x + x;
}

static slayer3d_vec3 source_grid_cell_center_meters(const int *x_edges, const int *y_edges, const int *z_edges, int x,
                                                    int y, int z, float meters_per_unit)
{
    const float sx = ((float)x_edges[x] + (float)x_edges[x + 1]) * 0.5f * meters_per_unit;
    const float sy = ((float)y_edges[y] + (float)y_edges[y + 1]) * 0.5f * meters_per_unit;
    const float sz = ((float)z_edges[z] + (float)z_edges[z + 1]) * 0.5f * meters_per_unit;
    return slayer3d_vec3_make(sx, sy, sz);
}

static int source_box_face_index_for_axis_side(int axis, int side)
{
    if (axis < 0 || axis > 2 || side == 0)
        return -1;
    return axis * 2 + (side > 0 ? 0 : 1);
}

static const char *source_axis_name(int axis)
{
    static const char *const names[3] = {"x", "y", "z"};
    return axis >= 0 && axis < 3 ? names[axis] : "";
}

static void source_boundary_axis_side(int x, int y, int z, int dim_x, int dim_y, int dim_z, int *out_axis,
                                      int *out_side)
{
    int axis = -1;
    int side = 0;
    if (x == 0)
    {
        axis = 0;
        side = -1;
    }
    else if (x == dim_x - 1)
    {
        axis = 0;
        side = 1;
    }
    else if (y == 0)
    {
        axis = 1;
        side = -1;
    }
    else if (y == dim_y - 1)
    {
        axis = 1;
        side = 1;
    }
    else if (z == 0)
    {
        axis = 2;
        side = -1;
    }
    else if (z == dim_z - 1)
    {
        axis = 2;
        side = 1;
    }
    if (out_axis != NULL)
        *out_axis = axis;
    if (out_side != NULL)
        *out_side = side;
}

static double source_clamp_double(double value, double min_value, double max_value)
{
    return value < min_value ? min_value : (value > max_value ? max_value : value);
}

static slayer3d_vec3 source_point_units_to_meters(const double point[3], float meters_per_unit)
{
    return slayer3d_vec3_make((float)point[0] * meters_per_unit, (float)point[1] * meters_per_unit,
                              (float)point[2] * meters_per_unit);
}

static void source_enclosure_set_candidate_source_face(
    const brush_world_runtime *world_runtime, const double leak_point_units[3], int leak_axis, int leak_side,
    slayer3d_game_data_editor_brush_enclosure_diagnostics *out_diagnostics)
{
    if (world_runtime == NULL || leak_point_units == NULL || out_diagnostics == NULL)
        return;
    const int face_index = source_box_face_index_for_axis_side(leak_axis, leak_side);
    if (face_index < 0)
        return;

    const float meters_per_unit =
        world_runtime->editor_source_meters_per_unit > 0.0f ? world_runtime->editor_source_meters_per_unit : 0.001f;
    double best_distance_sq = -1.0;
    double best_point[3] = {0.0, 0.0, 0.0};
    const editor_brush_source_box_runtime *best_box = NULL;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
    {
        const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[i];
        if (!source_box_contents_are_structural(box->contents))
            continue;
        double candidate[3] = {source_clamp_double(leak_point_units[0], (double)box->min[0], (double)box->max[0]),
                               source_clamp_double(leak_point_units[1], (double)box->min[1], (double)box->max[1]),
                               source_clamp_double(leak_point_units[2], (double)box->min[2], (double)box->max[2])};
        candidate[leak_axis] = leak_side > 0 ? box->max[leak_axis] : box->min[leak_axis];
        const double dx = (double)candidate[0] - (double)leak_point_units[0];
        const double dy = (double)candidate[1] - (double)leak_point_units[1];
        const double dz = (double)candidate[2] - (double)leak_point_units[2];
        const double distance_sq = dx * dx + dy * dy + dz * dz;
        if (best_box == NULL || distance_sq < best_distance_sq)
        {
            best_box = box;
            best_distance_sq = distance_sq;
            best_point[0] = candidate[0];
            best_point[1] = candidate[1];
            best_point[2] = candidate[2];
        }
    }
    if (best_box == NULL)
        return;

    SDL_strlcpy(out_diagnostics->candidate_source_name, best_box->name != NULL ? best_box->name : "",
                sizeof(out_diagnostics->candidate_source_name));
    SDL_strlcpy(out_diagnostics->candidate_source_stable_id, best_box->stable_id != NULL ? best_box->stable_id : "",
                sizeof(out_diagnostics->candidate_source_stable_id));
    SDL_strlcpy(out_diagnostics->candidate_source_face, source_box_face_keys[face_index],
                sizeof(out_diagnostics->candidate_source_face));
    out_diagnostics->candidate_source_point = source_point_units_to_meters(best_point, meters_per_unit);
    out_diagnostics->candidate_source_distance = (float)SDL_sqrt(best_distance_sq) * meters_per_unit;
}

bool slayer3d_game_data_validate_editor_brush_source_enclosure(
    const slayer3d_game_data_runtime *runtime, const char *world_name, const char *player_start_name, int max_cells,
    slayer3d_game_data_editor_brush_enclosure_diagnostics *out_diagnostics, char *error_buffer, int error_buffer_size)
{
    if (out_diagnostics != NULL)
        SDL_zero(*out_diagnostics);
    if (out_diagnostics == NULL)
    {
        set_error(error_buffer, error_buffer_size, "editor brush enclosure diagnostics output is required");
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
    if (world_runtime->editor_source_box_count <= 0)
    {
        set_error(error_buffer, error_buffer_size, "brush source model has no boxes");
        SDL_strlcpy(out_diagnostics->first_issue, "brush source model has no boxes",
                    sizeof(out_diagnostics->first_issue));
        return true;
    }

    const editor_player_start_runtime *player_start = find_editor_player_start(runtime, player_start_name);
    out_diagnostics->has_player_start = player_start != NULL;
    if (player_start == NULL)
    {
        set_error(error_buffer, error_buffer_size, "editor player start not found");
        SDL_strlcpy(out_diagnostics->first_issue, "editor player start not found",
                    sizeof(out_diagnostics->first_issue));
        return true;
    }

    const int cell_cap = max_cells > 0 ? max_cells : 262144;
    const int player_coord[3] = {source_units_from_meters(world_runtime, player_start->position.x),
                                 source_units_from_meters(world_runtime, player_start->position.y),
                                 source_units_from_meters(world_runtime, player_start->position.z)};
    int bounds_min[3] = {player_coord[0], player_coord[1], player_coord[2]};
    int bounds_max[3] = {player_coord[0], player_coord[1], player_coord[2]};
    out_diagnostics->source_box_count = world_runtime->editor_source_box_count;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
        source_bounds_include_box(&world_runtime->editor_source_boxes[i], bounds_min, bounds_max);

    int *axis_edges[3] = {NULL, NULL, NULL};
    int edge_counts[3] = {0, 0, 0};
    int edge_capacities[3] = {0, 0, 0};
    bool edges_ok = true;
    for (int axis = 0; axis < 3; ++axis)
    {
        edges_ok = edges_ok && source_int_array_append_unique(&axis_edges[axis], &edge_counts[axis],
                                                              &edge_capacities[axis], bounds_min[axis] - 1);
        edges_ok = edges_ok && source_int_array_append_unique(&axis_edges[axis], &edge_counts[axis],
                                                              &edge_capacities[axis], bounds_max[axis] + 1);
        edges_ok = edges_ok && source_int_array_append_unique(&axis_edges[axis], &edge_counts[axis],
                                                              &edge_capacities[axis], player_coord[axis]);
        for (int box_index = 0; box_index < world_runtime->editor_source_box_count; ++box_index)
        {
            const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[box_index];
            edges_ok = edges_ok && source_int_array_append_unique(&axis_edges[axis], &edge_counts[axis],
                                                                  &edge_capacities[axis], box->min[axis]);
            edges_ok = edges_ok && source_int_array_append_unique(&axis_edges[axis], &edge_counts[axis],
                                                                  &edge_capacities[axis], box->max[axis]);
        }
        if (edge_counts[axis] > 1)
            qsort(axis_edges[axis], (size_t)edge_counts[axis], sizeof(int), compare_source_ints);
    }
    if (!edges_ok)
    {
        for (int axis = 0; axis < 3; ++axis)
            SDL_free(axis_edges[axis]);
        set_error(error_buffer, error_buffer_size, "failed to allocate brush enclosure grid edges");
        return false;
    }

    const int dim_x = edge_counts[0] - 1;
    const int dim_y = edge_counts[1] - 1;
    const int dim_z = edge_counts[2] - 1;
    const Sint64 wide_cell_count =
        dim_x > 0 && dim_y > 0 && dim_z > 0 ? (Sint64)dim_x * (Sint64)dim_y * (Sint64)dim_z : 0;
    out_diagnostics->grid_cell_count =
        wide_cell_count > 0 && wide_cell_count <= (Sint64)INT_MAX ? (int)wide_cell_count : 0;
    if (wide_cell_count <= 0 || wide_cell_count > (Sint64)cell_cap || wide_cell_count > (Sint64)INT_MAX)
    {
        for (int axis = 0; axis < 3; ++axis)
            SDL_free(axis_edges[axis]);
        set_errorf(error_buffer, error_buffer_size, "brush enclosure grid has %lld cells, cap is %d",
                   (long long)wide_cell_count, cell_cap);
        return false;
    }
    const int cell_count = (int)wide_cell_count;

    Uint8 *solid = (Uint8 *)SDL_calloc((size_t)cell_count, sizeof(*solid));
    Uint8 *visited = (Uint8 *)SDL_calloc((size_t)cell_count, sizeof(*visited));
    int *queue = (int *)SDL_malloc((size_t)cell_count * sizeof(*queue));
    if (solid == NULL || visited == NULL || queue == NULL)
    {
        SDL_free(solid);
        SDL_free(visited);
        SDL_free(queue);
        for (int axis = 0; axis < 3; ++axis)
            SDL_free(axis_edges[axis]);
        set_error(error_buffer, error_buffer_size, "failed to allocate brush enclosure flood grid");
        return false;
    }

    for (int z = 0; z < dim_z; ++z)
    {
        for (int y = 0; y < dim_y; ++y)
        {
            for (int x = 0; x < dim_x; ++x)
            {
                const int cell_index = source_grid_cell_index(x, y, z, dim_x, dim_y);
                for (int box_index = 0; box_index < world_runtime->editor_source_box_count; ++box_index)
                {
                    const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[box_index];
                    if (!source_box_contents_are_structural(box->contents))
                        continue;
                    if (source_cell_inside_box(axis_edges[0], axis_edges[1], axis_edges[2], x, y, z, box))
                    {
                        solid[cell_index] = 1;
                        out_diagnostics->solid_cell_count++;
                        break;
                    }
                }
            }
        }
    }

    const int start_x = source_cell_index_for_coord(axis_edges[0], edge_counts[0], player_coord[0]);
    const int start_y = source_cell_index_for_coord(axis_edges[1], edge_counts[1], player_coord[1]);
    const int start_z = source_cell_index_for_coord(axis_edges[2], edge_counts[2], player_coord[2]);
    if (start_x < 0 || start_y < 0 || start_z < 0)
    {
        SDL_strlcpy(out_diagnostics->first_issue, "player start is outside source enclosure grid",
                    sizeof(out_diagnostics->first_issue));
    }
    else
    {
        const int start_cell = source_grid_cell_index(start_x, start_y, start_z, dim_x, dim_y);
        if (solid[start_cell])
        {
            SDL_strlcpy(out_diagnostics->first_issue, "player start is inside a source solid",
                        sizeof(out_diagnostics->first_issue));
        }
        else
        {
            int head = 0;
            int tail = 0;
            visited[start_cell] = 1;
            queue[tail++] = start_cell;
            static const int offsets[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            while (head < tail)
            {
                const int cell = queue[head++];
                const int x = cell % dim_x;
                const int yz = cell / dim_x;
                const int y = yz % dim_y;
                const int z = yz / dim_y;
                out_diagnostics->visited_cell_count++;
                if (x == 0 || y == 0 || z == 0 || x == dim_x - 1 || y == dim_y - 1 || z == dim_z - 1)
                {
                    out_diagnostics->open_boundary_cell_count++;
                    if (out_diagnostics->first_issue[0] == '\0')
                    {
                        SDL_strlcpy(out_diagnostics->first_issue, "player-start reachable space leaks to outside",
                                    sizeof(out_diagnostics->first_issue));
                        out_diagnostics->first_leak_point =
                            source_grid_cell_center_meters(axis_edges[0], axis_edges[1], axis_edges[2], x, y, z,
                                                           world_runtime->editor_source_meters_per_unit);
                        int leak_axis = -1;
                        int leak_side = 0;
                        source_boundary_axis_side(x, y, z, dim_x, dim_y, dim_z, &leak_axis, &leak_side);
                        SDL_strlcpy(out_diagnostics->first_leak_axis, source_axis_name(leak_axis),
                                    sizeof(out_diagnostics->first_leak_axis));
                        SDL_strlcpy(out_diagnostics->first_leak_side,
                                    leak_side > 0 ? "positive" : (leak_side < 0 ? "negative" : ""),
                                    sizeof(out_diagnostics->first_leak_side));
                        const double leak_point_units[3] = {
                            ((double)axis_edges[0][x] + (double)axis_edges[0][x + 1]) * 0.5,
                            ((double)axis_edges[1][y] + (double)axis_edges[1][y + 1]) * 0.5,
                            ((double)axis_edges[2][z] + (double)axis_edges[2][z + 1]) * 0.5,
                        };
                        source_enclosure_set_candidate_source_face(world_runtime, leak_point_units, leak_axis,
                                                                   leak_side, out_diagnostics);
                    }
                }
                for (size_t direction = 0; direction < SDL_arraysize(offsets); ++direction)
                {
                    const int nx = x + offsets[direction][0];
                    const int ny = y + offsets[direction][1];
                    const int nz = z + offsets[direction][2];
                    if (nx < 0 || ny < 0 || nz < 0 || nx >= dim_x || ny >= dim_y || nz >= dim_z)
                        continue;
                    const int next_cell = source_grid_cell_index(nx, ny, nz, dim_x, dim_y);
                    if (solid[next_cell] || visited[next_cell])
                        continue;
                    visited[next_cell] = 1;
                    queue[tail++] = next_cell;
                }
            }
        }
    }

    out_diagnostics->enclosed =
        out_diagnostics->first_issue[0] == '\0' && out_diagnostics->open_boundary_cell_count == 0;
    SDL_free(solid);
    SDL_free(visited);
    SDL_free(queue);
    for (int axis = 0; axis < 3; ++axis)
        SDL_free(axis_edges[axis]);
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

    source_prefab_set_axis_bounds(&out_box->min[0], &out_box->max[0],
                                  source_units_from_meters(world_runtime, desc->anchor.x + oriented_min.x),
                                  source_units_from_meters(world_runtime, desc->anchor.x + oriented_max.x));
    source_prefab_set_axis_bounds(&out_box->min[1], &out_box->max[1],
                                  source_units_from_meters(world_runtime, desc->anchor.y + oriented_min.y),
                                  source_units_from_meters(world_runtime, desc->anchor.y + oriented_max.y));
    source_prefab_set_axis_bounds(&out_box->min[2], &out_box->max[2],
                                  source_units_from_meters(world_runtime, desc->anchor.z + oriented_min.z),
                                  source_units_from_meters(world_runtime, desc->anchor.z + oriented_max.z));
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
    out_box->min[0] = source_units_from_meters(world_runtime, desc->min.x);
    out_box->min[1] = source_units_from_meters(world_runtime, desc->min.y);
    out_box->min[2] = source_units_from_meters(world_runtime, desc->min.z);
    out_box->max[0] = source_units_from_meters(world_runtime, desc->max.x);
    out_box->max[1] = source_units_from_meters(world_runtime, desc->max.y);
    out_box->max[2] = source_units_from_meters(world_runtime, desc->max.z);
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

    bounds.min = slayer3d_vec3_make(source_meters_from_units(world_runtime, box->min[0]),
                                    source_meters_from_units(world_runtime, box->min[1]),
                                    source_meters_from_units(world_runtime, box->min[2]));
    bounds.max = slayer3d_vec3_make(source_meters_from_units(world_runtime, box->max[0]),
                                    source_meters_from_units(world_runtime, box->max[1]),
                                    source_meters_from_units(world_runtime, box->max[2]));
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

    bool ok = editor_brush_world_validate_source_box_candidate(world_runtime, &source_box, -1, error_buffer,
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
        source_box_candidate_collect_warnings(world_runtime, &source_box, -1, out_result);
    }
    if (ok && apply)
    {
        ok = editor_brush_world_insert_source_box_at_index(world_runtime, world_runtime->editor_source_box_count,
                                                           &source_box, error_buffer, error_buffer_size);
    }
    if (ok && out_result != NULL)
    {
        out_result->valid = true;
        SDL_strlcpy(out_result->brush_name, name, sizeof(out_result->brush_name));
        out_result->bounds = editor_brush_source_box_bounds_meters(world_runtime, &source_box);
        for (int axis = 0; axis < 3; ++axis)
        {
            out_result->source_min[axis] = source_box.min[axis];
            out_result->source_max[axis] = source_box.max[axis];
        }
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
    out_box->min[0] = source_units_from_meters(world_runtime, brush->bounds.min.x);
    out_box->min[1] = source_units_from_meters(world_runtime, brush->bounds.min.y);
    out_box->min[2] = source_units_from_meters(world_runtime, brush->bounds.min.z);
    out_box->max[0] = source_units_from_meters(world_runtime, brush->bounds.max.x);
    out_box->max[1] = source_units_from_meters(world_runtime, brush->bounds.max.y);
    out_box->max[2] = source_units_from_meters(world_runtime, brush->bounds.max.z);
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

/**
 * @file game_data_brush_source_vertices.c
 * @brief Source-brush vertex, edge, and face topology helpers for editor tooling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

static const char *const source_box_vertex_labels[SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT] = {
    "nx.ny.nz", "px.ny.nz", "px.py.nz", "nx.py.nz", "nx.ny.pz", "px.ny.pz", "px.py.pz", "nx.py.pz",
};

static const int source_box_vertex_signs[SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT][3] = {
    {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1},
};

static const int source_box_edges[SLAYER3D_EDITOR_SOURCE_BOX_EDGE_COUNT][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

static const char *const source_box_edge_labels[SLAYER3D_EDITOR_SOURCE_BOX_EDGE_COUNT] = {
    "ny.nz", "px.nz", "py.nz", "nx.nz", "ny.pz", "px.pz", "py.pz", "nx.pz", "nx.ny", "px.ny", "px.py", "nx.py",
};

static const char *const source_box_face_labels[SLAYER3D_EDITOR_SOURCE_BOX_FACE_COUNT] = {"px", "nx", "py",
                                                                                          "ny", "pz", "nz"};

static const int source_box_faces[SLAYER3D_EDITOR_SOURCE_BOX_FACE_COUNT][4] = {
    {1, 2, 6, 5}, {0, 4, 7, 3}, {2, 3, 7, 6}, {0, 1, 5, 4}, {4, 5, 6, 7}, {0, 3, 2, 1},
};

static const char *source_box_identity(const editor_brush_source_box_runtime *box)
{
    if (box == NULL)
        return "<null>";
    if (box->stable_id != NULL && box->stable_id[0] != '\0')
        return box->stable_id;
    if (box->name != NULL && box->name[0] != '\0')
        return box->name;
    return "<unnamed>";
}

static int source_vertex_snap_units(const brush_world_runtime *world_runtime)
{
    return world_runtime != NULL && world_runtime->editor_source_snap_units > 0
               ? world_runtime->editor_source_snap_units
               : 1;
}

static bool source_vertex_coord_on_snap(int coord, int snap_units)
{
    return snap_units <= 1 || coord % snap_units == 0;
}

static void source_vertex_set_issue(editor_brush_source_vertex_diagnostics *diagnostics, const char *issue,
                                    const char *stable_id)
{
    if (diagnostics == NULL || diagnostics->first_issue[0] != '\0')
        return;
    SDL_snprintf(diagnostics->first_issue, sizeof(diagnostics->first_issue), "%s", issue != NULL ? issue : "");
    SDL_snprintf(diagnostics->first_issue_stable_id, sizeof(diagnostics->first_issue_stable_id), "%s",
                 stable_id != NULL ? stable_id : "");
}

static void source_vertex_publish_error(const editor_brush_source_vertex_diagnostics *diagnostics, char *error_buffer,
                                        int error_buffer_size)
{
    if (diagnostics == NULL || diagnostics->valid)
        return;
    set_error(error_buffer, error_buffer_size,
              diagnostics->first_issue[0] != '\0' ? diagnostics->first_issue : "invalid source brush vertices");
}

static void source_vertices_bounds(const int vertices[SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT][3], int out_min[3],
                                   int out_max[3])
{
    for (int axis = 0; axis < 3; ++axis)
    {
        out_min[axis] = vertices[0][axis];
        out_max[axis] = vertices[0][axis];
    }
    for (int vertex = 1; vertex < SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT; ++vertex)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            out_min[axis] = SDL_min(out_min[axis], vertices[vertex][axis]);
            out_max[axis] = SDL_max(out_max[axis], vertices[vertex][axis]);
        }
    }
}

static bool source_vertices_have_duplicate(const int vertices[SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT][3])
{
    for (int a = 0; a < SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT; ++a)
    {
        for (int b = a + 1; b < SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT; ++b)
        {
            if (vertices[a][0] == vertices[b][0] && vertices[a][1] == vertices[b][1] &&
                vertices[a][2] == vertices[b][2])
            {
                return true;
            }
        }
    }
    return false;
}

static bool source_vertex_matches_expected_corner(const int vertex[3], const int min[3], const int max[3],
                                                  const int signs[3])
{
    for (int axis = 0; axis < 3; ++axis)
    {
        const int expected = signs[axis] != 0 ? max[axis] : min[axis];
        if (vertex[axis] != expected)
            return false;
    }
    return true;
}

bool editor_brush_source_validate_box_vertex_topology(const int vertices[SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT][3],
                                                      int snap_units,
                                                      editor_brush_source_vertex_diagnostics *out_diagnostics,
                                                      char *error_buffer, int error_buffer_size)
{
    editor_brush_source_vertex_diagnostics diagnostics;
    SDL_zero(diagnostics);
    diagnostics.valid = true;
    diagnostics.brush_count = 1;
    diagnostics.vertex_count = SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT;
    diagnostics.edge_count = SLAYER3D_EDITOR_SOURCE_BOX_EDGE_COUNT;
    diagnostics.face_count = SLAYER3D_EDITOR_SOURCE_BOX_FACE_COUNT;

    if (vertices == NULL)
    {
        diagnostics.valid = false;
        diagnostics.degenerate_count = 1;
        source_vertex_set_issue(&diagnostics, "source brush vertex topology is missing", "");
        if (out_diagnostics != NULL)
            *out_diagnostics = diagnostics;
        source_vertex_publish_error(&diagnostics, error_buffer, error_buffer_size);
        return false;
    }

    if (snap_units <= 0)
        snap_units = 1;
    for (int vertex = 0; vertex < SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT; ++vertex)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!source_vertex_coord_on_snap(vertices[vertex][axis], snap_units))
            {
                diagnostics.valid = false;
                diagnostics.off_snap_count++;
                source_vertex_set_issue(&diagnostics, "source brush vertex is off grid",
                                        source_box_vertex_labels[vertex]);
            }
        }
    }

    int min[3];
    int max[3];
    source_vertices_bounds(vertices, min, max);
    for (int axis = 0; axis < 3; ++axis)
    {
        if (min[axis] >= max[axis])
        {
            diagnostics.valid = false;
            diagnostics.degenerate_count++;
            source_vertex_set_issue(&diagnostics, "source brush vertex topology has zero volume", "");
        }
    }

    if (source_vertices_have_duplicate(vertices))
    {
        diagnostics.valid = false;
        diagnostics.degenerate_count++;
        source_vertex_set_issue(&diagnostics, "source brush vertex topology contains duplicate vertices", "");
    }

    for (int vertex = 0; vertex < SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT; ++vertex)
    {
        if (!source_vertex_matches_expected_corner(vertices[vertex], min, max, source_box_vertex_signs[vertex]))
        {
            diagnostics.valid = false;
            diagnostics.concave_count++;
            source_vertex_set_issue(&diagnostics, "source brush vertex topology is not a convex canonical box",
                                    source_box_vertex_labels[vertex]);
        }
    }

    if (out_diagnostics != NULL)
        *out_diagnostics = diagnostics;
    source_vertex_publish_error(&diagnostics, error_buffer, error_buffer_size);
    return diagnostics.valid;
}

static void source_box_fill_vertices(const editor_brush_source_box_runtime *box,
                                     int vertices[SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT][3])
{
    for (int vertex = 0; vertex < SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT; ++vertex)
    {
        for (int axis = 0; axis < 3; ++axis)
            vertices[vertex][axis] = source_box_vertex_signs[vertex][axis] != 0 ? box->max[axis] : box->min[axis];
    }
}

static void source_stable_id(char *buffer, size_t buffer_size, const char *brush_id, const char *kind,
                             const char *label)
{
    SDL_snprintf(buffer, buffer_size, "%s.%s.%s", brush_id != NULL ? brush_id : "<unnamed>",
                 kind != NULL ? kind : "element", label != NULL ? label : "unknown");
}

bool editor_brush_source_box_build_vertex_model(const brush_world_runtime *world_runtime, int source_index,
                                                editor_brush_source_vertex_model *out_model, char *error_buffer,
                                                int error_buffer_size)
{
    if (out_model != NULL)
        SDL_zero(*out_model);
    if (world_runtime == NULL || out_model == NULL || source_index < 0 ||
        source_index >= world_runtime->editor_source_box_count)
    {
        set_error(error_buffer, error_buffer_size, "source brush vertex model requires a valid source index");
        return false;
    }

    const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    const char *brush_id = source_box_identity(box);
    int raw_vertices[SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT][3];
    source_box_fill_vertices(box, raw_vertices);
    if (!editor_brush_source_validate_box_vertex_topology(raw_vertices, source_vertex_snap_units(world_runtime), NULL,
                                                          error_buffer, error_buffer_size))
    {
        return false;
    }

    out_model->brush_index = source_index;
    SDL_snprintf(out_model->brush_stable_id, sizeof(out_model->brush_stable_id), "%s", brush_id);
    out_model->vertex_count = SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT;
    for (int vertex = 0; vertex < out_model->vertex_count; ++vertex)
    {
        editor_brush_source_vertex *out_vertex = &out_model->vertices[vertex];
        out_vertex->brush_index = source_index;
        out_vertex->vertex_index = vertex;
        for (int axis = 0; axis < 3; ++axis)
            out_vertex->coord[axis] = raw_vertices[vertex][axis];
        source_stable_id(out_vertex->stable_id, sizeof(out_vertex->stable_id), brush_id, "vertex",
                         source_box_vertex_labels[vertex]);
    }

    out_model->edge_count = SLAYER3D_EDITOR_SOURCE_BOX_EDGE_COUNT;
    for (int edge = 0; edge < out_model->edge_count; ++edge)
    {
        editor_brush_source_edge *out_edge = &out_model->edges[edge];
        out_edge->brush_index = source_index;
        out_edge->edge_index = edge;
        out_edge->vertex_indices[0] = source_box_edges[edge][0];
        out_edge->vertex_indices[1] = source_box_edges[edge][1];
        source_stable_id(out_edge->stable_id, sizeof(out_edge->stable_id), brush_id, "edge",
                         source_box_edge_labels[edge]);
    }

    out_model->face_count = SLAYER3D_EDITOR_SOURCE_BOX_FACE_COUNT;
    for (int face = 0; face < out_model->face_count; ++face)
    {
        editor_brush_source_face_ref *out_face = &out_model->faces[face];
        out_face->brush_index = source_index;
        out_face->face_index = face;
        out_face->vertex_count = 4;
        for (int vertex = 0; vertex < 4; ++vertex)
            out_face->vertex_indices[vertex] = source_box_faces[face][vertex];
        source_stable_id(out_face->stable_id, sizeof(out_face->stable_id), brush_id, "face",
                         source_box_face_labels[face]);
    }
    return true;
}

static int source_selection_count(const brush_world_runtime *world_runtime, const int *source_indices,
                                  int source_index_count)
{
    if (world_runtime == NULL)
        return 0;
    return source_indices == NULL || source_index_count <= 0 ? world_runtime->editor_source_box_count
                                                             : source_index_count;
}

static int source_selection_index(const int *source_indices, int source_index)
{
    return source_indices != NULL ? source_indices[source_index] : source_index;
}

bool editor_brush_world_validate_source_vertex_model(const brush_world_runtime *world_runtime,
                                                     const int *source_indices, int source_index_count,
                                                     editor_brush_source_vertex_diagnostics *out_diagnostics,
                                                     char *error_buffer, int error_buffer_size)
{
    editor_brush_source_vertex_diagnostics diagnostics;
    SDL_zero(diagnostics);
    diagnostics.valid = true;

    const int selected_count = source_selection_count(world_runtime, source_indices, source_index_count);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || selected_count < 0)
    {
        diagnostics.valid = false;
        source_vertex_set_issue(&diagnostics, "source vertex validation requires a source brush model", "");
        if (out_diagnostics != NULL)
            *out_diagnostics = diagnostics;
        source_vertex_publish_error(&diagnostics, error_buffer, error_buffer_size);
        return false;
    }

    diagnostics.brush_count = selected_count;
    for (int i = 0; i < selected_count; ++i)
    {
        editor_brush_source_vertex_model model;
        const int source_index = source_selection_index(source_indices, i);
        if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, error_buffer,
                                                        error_buffer_size))
        {
            diagnostics.valid = false;
            source_vertex_set_issue(&diagnostics, error_buffer != NULL ? error_buffer : "invalid source brush vertex",
                                    "");
            if (out_diagnostics != NULL)
                *out_diagnostics = diagnostics;
            return false;
        }
        diagnostics.vertex_count += model.vertex_count;
        diagnostics.edge_count += model.edge_count;
        diagnostics.face_count += model.face_count;
    }

    int shared_count = 0;
    (void)editor_brush_world_find_shared_source_vertices(world_runtime, source_indices, source_index_count, NULL, 0,
                                                         &shared_count, NULL, 0);
    diagnostics.shared_vertex_count = shared_count;

    if (out_diagnostics != NULL)
        *out_diagnostics = diagnostics;
    return true;
}

static bool source_vertex_coord_equal(const int a[3], const int b[3])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static bool source_shared_vertex_recorded(const int (*coords)[3], int count, const int coord[3])
{
    for (int i = 0; i < count; ++i)
    {
        if (source_vertex_coord_equal(coords[i], coord))
            return true;
    }
    return false;
}

static int source_count_matching_vertices(const editor_brush_source_vertex_model *models, int model_count,
                                          const int coord[3], int *out_first_brush, int *out_first_vertex)
{
    int count = 0;
    for (int model_index = 0; model_index < model_count; ++model_index)
    {
        const editor_brush_source_vertex_model *model = &models[model_index];
        for (int vertex = 0; vertex < model->vertex_count; ++vertex)
        {
            if (!source_vertex_coord_equal(model->vertices[vertex].coord, coord))
                continue;
            if (count == 0)
            {
                if (out_first_brush != NULL)
                    *out_first_brush = model->brush_index;
                if (out_first_vertex != NULL)
                    *out_first_vertex = vertex;
            }
            ++count;
        }
    }
    return count;
}

bool editor_brush_world_find_shared_source_vertices(const brush_world_runtime *world_runtime, const int *source_indices,
                                                    int source_index_count,
                                                    editor_brush_source_shared_vertex *out_vertices, int out_capacity,
                                                    int *out_count, char *error_buffer, int error_buffer_size)
{
    if (out_count != NULL)
        *out_count = 0;
    const int selected_count = source_selection_count(world_runtime, source_indices, source_index_count);
    if (world_runtime == NULL || selected_count < 0)
    {
        set_error(error_buffer, error_buffer_size, "shared source vertex lookup requires a brush world");
        return false;
    }
    if (selected_count == 0)
        return true;

    editor_brush_source_vertex_model *models =
        (editor_brush_source_vertex_model *)SDL_calloc((size_t)selected_count, sizeof(*models));
    int (*recorded_coords)[3] = (int (*)[3])SDL_calloc((size_t)selected_count * SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT,
                                                       sizeof(*recorded_coords));
    if (models == NULL || recorded_coords == NULL)
    {
        SDL_free(models);
        SDL_free(recorded_coords);
        set_error(error_buffer, error_buffer_size, "failed to allocate source vertex models");
        return false;
    }

    bool ok = true;
    for (int i = 0; ok && i < selected_count; ++i)
    {
        const int source_index = source_selection_index(source_indices, i);
        ok = editor_brush_source_box_build_vertex_model(world_runtime, source_index, &models[i], error_buffer,
                                                        error_buffer_size);
    }

    int shared_count = 0;
    for (int model_index = 0; ok && model_index < selected_count; ++model_index)
    {
        const editor_brush_source_vertex_model *model = &models[model_index];
        for (int vertex = 0; vertex < model->vertex_count; ++vertex)
        {
            const int *coord = model->vertices[vertex].coord;
            if (source_shared_vertex_recorded(recorded_coords, shared_count, coord))
                continue;
            int first_brush = -1;
            int first_vertex = -1;
            const int references =
                source_count_matching_vertices(models, selected_count, coord, &first_brush, &first_vertex);
            if (references <= 1)
                continue;
            if (out_vertices != NULL && shared_count >= out_capacity)
            {
                set_error(error_buffer, error_buffer_size, "shared source vertex output capacity exceeded");
                ok = false;
                break;
            }
            if (out_vertices != NULL)
            {
                editor_brush_source_shared_vertex *shared = &out_vertices[shared_count];
                shared->coord[0] = coord[0];
                shared->coord[1] = coord[1];
                shared->coord[2] = coord[2];
                shared->first_brush_index = first_brush;
                shared->first_vertex_index = first_vertex;
                shared->reference_count = references;
            }
            recorded_coords[shared_count][0] = coord[0];
            recorded_coords[shared_count][1] = coord[1];
            recorded_coords[shared_count][2] = coord[2];
            ++shared_count;
        }
    }

    if (out_count != NULL)
        *out_count = shared_count;
    SDL_free(recorded_coords);
    SDL_free(models);
    return ok;
}

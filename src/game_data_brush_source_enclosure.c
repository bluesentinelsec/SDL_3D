/**
 * @file game_data_brush_source_enclosure.c
 * @brief Editor brush source enclosure/leak diagnostics.
 */

#include "game_data_brush_source_model_internal.h"

#include <SDL3/SDL_stdinc.h>

#include <limits.h>
#include <stdlib.h>

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
        if (!editor_brush_source_contents_are_structural(box->contents))
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
    SDL_strlcpy(out_diagnostics->candidate_source_face, editor_brush_source_box_face_keys[face_index],
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
    const int player_coord[3] = {editor_brush_source_units_from_meters(world_runtime, player_start->position.x),
                                 editor_brush_source_units_from_meters(world_runtime, player_start->position.y),
                                 editor_brush_source_units_from_meters(world_runtime, player_start->position.z)};
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
                    if (!editor_brush_source_contents_are_structural(box->contents))
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

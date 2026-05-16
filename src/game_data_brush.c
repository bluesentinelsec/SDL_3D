/**
 * @file game_data_brush.c
 * @brief Authored brush-world trace and sliding helpers for game-data runtimes.
 */

#include "game_data_brush_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/collision.h"
#include "slayer3d/math.h"

static void brush_bounds_add_point(slayer3d_bounding_box *bounds, bool *has_bounds, slayer3d_vec3 point)
{
    if (bounds == NULL || has_bounds == NULL)
        return;
    if (!*has_bounds)
    {
        bounds->min = point;
        bounds->max = point;
        *has_bounds = true;
        return;
    }
    bounds->min.x = SDL_min(bounds->min.x, point.x);
    bounds->min.y = SDL_min(bounds->min.y, point.y);
    bounds->min.z = SDL_min(bounds->min.z, point.z);
    bounds->max.x = SDL_max(bounds->max.x, point.x);
    bounds->max.y = SDL_max(bounds->max.y, point.y);
    bounds->max.z = SDL_max(bounds->max.z, point.z);
}

slayer3d_bounding_box slayer3d_game_data_brush_trace_bounds(const slayer3d_game_data_brush_trace_desc *desc)
{
    slayer3d_vec3 extents = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (desc != NULL)
    {
        if (desc->shape == SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE)
            extents = slayer3d_vec3_make(SDL_max(desc->extents.x, 0.0f), SDL_max(desc->extents.x, 0.0f),
                                         SDL_max(desc->extents.x, 0.0f));
        else if (desc->shape == SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB)
            extents = slayer3d_vec3_make(SDL_max(desc->extents.x, 0.0f), SDL_max(desc->extents.y, 0.0f),
                                         SDL_max(desc->extents.z, 0.0f));
    }

    const slayer3d_vec3 start = desc != NULL ? desc->start : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const slayer3d_vec3 end = desc != NULL ? desc->end : start;
    return (slayer3d_bounding_box){
        slayer3d_vec3_make(SDL_min(start.x, end.x) - extents.x, SDL_min(start.y, end.y) - extents.y,
                           SDL_min(start.z, end.z) - extents.z),
        slayer3d_vec3_make(SDL_max(start.x, end.x) + extents.x, SDL_max(start.y, end.y) + extents.y,
                           SDL_max(start.z, end.z) + extents.z),
    };
}

static bool brush_plane_normalized(const slayer3d_game_data_brush_face *face, slayer3d_vec3 *out_normal,
                                   float *out_distance)
{
    if (face == NULL || out_normal == NULL || out_distance == NULL)
        return false;
    const float len = slayer3d_vec3_length(face->normal);
    if (len <= 0.000001f)
        return false;
    *out_normal = slayer3d_vec3_scale(face->normal, 1.0f / len);
    *out_distance = face->distance / len;
    return true;
}

static bool brush_face_basis(slayer3d_vec3 normal, slayer3d_vec3 *out_u, slayer3d_vec3 *out_v)
{
    if (out_u == NULL || out_v == NULL)
        return false;
    const slayer3d_vec3 reference =
        SDL_fabsf(normal.y) < 0.99f ? slayer3d_vec3_make(0.0f, 1.0f, 0.0f) : slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    slayer3d_vec3 u = slayer3d_vec3_cross(reference, normal);
    if (slayer3d_vec3_length_squared(u) <= 0.000001f)
        u = slayer3d_vec3_cross(slayer3d_vec3_make(0.0f, 0.0f, 1.0f), normal);
    if (slayer3d_vec3_length_squared(u) <= 0.000001f)
        return false;
    u = slayer3d_vec3_normalize(u);
    const slayer3d_vec3 v = slayer3d_vec3_normalize(slayer3d_vec3_cross(normal, u));
    *out_u = u;
    *out_v = v;
    return true;
}

static int brush_clip_polygon_to_plane(const slayer3d_vec3 *input, int input_count, slayer3d_vec3 *output,
                                       int output_capacity, slayer3d_vec3 normal, float distance)
{
    const float epsilon = 0.0005f;
    int output_count = 0;
    if (input == NULL || output == NULL || input_count <= 0 || output_capacity <= 0)
        return 0;

    slayer3d_vec3 prev = input[input_count - 1];
    float prev_d = slayer3d_vec3_dot(normal, prev) - distance;
    bool prev_inside = prev_d <= epsilon;
    for (int i = 0; i < input_count; ++i)
    {
        const slayer3d_vec3 cur = input[i];
        const float cur_d = slayer3d_vec3_dot(normal, cur) - distance;
        const bool cur_inside = cur_d <= epsilon;
        if (cur_inside != prev_inside)
        {
            const float denom = prev_d - cur_d;
            if (SDL_fabsf(denom) > 0.000001f && output_count < output_capacity)
            {
                const float t = prev_d / denom;
                output[output_count++] = slayer3d_vec3_add(
                    prev, slayer3d_vec3_scale(slayer3d_vec3_sub(cur, prev), SDL_clamp(t, 0.0f, 1.0f)));
            }
        }
        if (cur_inside && output_count < output_capacity)
            output[output_count++] = cur;
        prev = cur;
        prev_d = cur_d;
        prev_inside = cur_inside;
    }
    return output_count;
}

static int brush_compact_polygon(slayer3d_vec3 *points, int count)
{
    if (points == NULL || count <= 0)
        return 0;

    int write = 0;
    for (int i = 0; i < count; ++i)
    {
        if (write > 0 && slayer3d_vec3_length_squared(slayer3d_vec3_sub(points[i], points[write - 1])) <= 0.000001f)
            continue;
        points[write++] = points[i];
    }
    if (write > 1 && slayer3d_vec3_length_squared(slayer3d_vec3_sub(points[0], points[write - 1])) <= 0.000001f)
        --write;
    return write;
}

static int brush_build_face_polygon(const slayer3d_game_data_brush *brush, int face_index, slayer3d_vec3 *polygon,
                                    int polygon_capacity, slayer3d_vec3 *out_normal, slayer3d_vec3 *out_u,
                                    slayer3d_vec3 *out_v)
{
    slayer3d_vec3 normal;
    slayer3d_vec3 u;
    slayer3d_vec3 v;
    float distance = 0.0f;
    if (brush == NULL || polygon == NULL || polygon_capacity < 4 || face_index < 0 || face_index >= brush->face_count ||
        !brush_plane_normalized(&brush->faces[face_index], &normal, &distance) || !brush_face_basis(normal, &u, &v))
    {
        return 0;
    }

    const float extent = SDL_max(SDL_fabsf(distance) + 4096.0f, 4096.0f);
    const slayer3d_vec3 center = slayer3d_vec3_scale(normal, distance);
    polygon[0] =
        slayer3d_vec3_sub(slayer3d_vec3_sub(center, slayer3d_vec3_scale(u, extent)), slayer3d_vec3_scale(v, extent));
    polygon[1] =
        slayer3d_vec3_sub(slayer3d_vec3_add(center, slayer3d_vec3_scale(u, extent)), slayer3d_vec3_scale(v, extent));
    polygon[2] =
        slayer3d_vec3_add(slayer3d_vec3_add(center, slayer3d_vec3_scale(u, extent)), slayer3d_vec3_scale(v, extent));
    polygon[3] =
        slayer3d_vec3_add(slayer3d_vec3_sub(center, slayer3d_vec3_scale(u, extent)), slayer3d_vec3_scale(v, extent));

    int count = 4;
    slayer3d_vec3 *scratch = (slayer3d_vec3 *)SDL_malloc((size_t)polygon_capacity * sizeof(*scratch));
    if (scratch == NULL)
        return 0;
    for (int clip_index = 0; clip_index < brush->face_count && count >= 3; ++clip_index)
    {
        if (clip_index == face_index)
            continue;
        slayer3d_vec3 clip_normal;
        float clip_distance = 0.0f;
        if (!brush_plane_normalized(&brush->faces[clip_index], &clip_normal, &clip_distance))
        {
            count = 0;
            break;
        }
        count = brush_clip_polygon_to_plane(polygon, count, scratch, polygon_capacity, clip_normal, clip_distance);
        if (count > 0)
            SDL_memcpy(polygon, scratch, (size_t)count * sizeof(*polygon));
        count = brush_compact_polygon(polygon, count);
    }
    SDL_free(scratch);

    if (count >= 3)
    {
        const slayer3d_vec3 winding_normal =
            slayer3d_vec3_cross(slayer3d_vec3_sub(polygon[1], polygon[0]), slayer3d_vec3_sub(polygon[2], polygon[0]));
        if (slayer3d_vec3_dot(winding_normal, normal) < 0.0f)
        {
            for (int a = 0, b = count - 1; a < b; ++a, --b)
            {
                const slayer3d_vec3 tmp = polygon[a];
                polygon[a] = polygon[b];
                polygon[b] = tmp;
            }
        }
    }
    if (out_normal != NULL)
        *out_normal = normal;
    if (out_u != NULL)
        *out_u = u;
    if (out_v != NULL)
        *out_v = v;
    return count;
}

static bool brush_is_renderable(const slayer3d_game_data_brush *brush)
{
    if (brush == NULL)
        return false;
    const unsigned int visible_contents = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID |
                                          SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER |
                                          SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA | SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY;
    return (brush->contents & visible_contents) != 0u;
}

static Uint8 brush_color_channel(float value)
{
    if (value <= 0.0f)
        return 0;
    if (value >= 1.0f)
        return 255;
    return (Uint8)(value * 255.0f + 0.5f);
}

static slayer3d_color brush_material_color(const slayer3d_game_data_brush_material *material)
{
    if (material == NULL)
        return (slayer3d_color){255, 255, 255, 255};
    return (slayer3d_color){
        brush_color_channel(material->albedo.x),
        brush_color_channel(material->albedo.y),
        brush_color_channel(material->albedo.z),
        brush_color_channel(material->albedo.w),
    };
}

static void brush_mesh_bounds_add(slayer3d_mesh *mesh, slayer3d_vec3 p)
{
    if (mesh == NULL)
        return;
    if (!mesh->has_local_bounds)
    {
        mesh->local_bounds.min = p;
        mesh->local_bounds.max = p;
        mesh->has_local_bounds = true;
        return;
    }
    mesh->local_bounds.min.x = SDL_min(mesh->local_bounds.min.x, p.x);
    mesh->local_bounds.min.y = SDL_min(mesh->local_bounds.min.y, p.y);
    mesh->local_bounds.min.z = SDL_min(mesh->local_bounds.min.z, p.z);
    mesh->local_bounds.max.x = SDL_max(mesh->local_bounds.max.x, p.x);
    mesh->local_bounds.max.y = SDL_max(mesh->local_bounds.max.y, p.y);
    mesh->local_bounds.max.z = SDL_max(mesh->local_bounds.max.z, p.z);
}

static bool brush_float_equal(float a, float b)
{
    return SDL_fabsf(a - b) <= 0.000001f;
}

static bool brush_string_equal(const char *a, const char *b)
{
    if (a == NULL || a[0] == '\0')
        a = "";
    if (b == NULL || b[0] == '\0')
        b = "";
    return SDL_strcmp(a, b) == 0;
}

static bool brush_materials_render_equivalent(const slayer3d_game_data_brush_material *a,
                                              const slayer3d_game_data_brush_material *b)
{
    if (a == NULL || b == NULL)
        return false;
    return brush_string_equal(a->texture, b->texture) && brush_float_equal(a->albedo.x, b->albedo.x) &&
           brush_float_equal(a->albedo.y, b->albedo.y) && brush_float_equal(a->albedo.z, b->albedo.z) &&
           brush_float_equal(a->albedo.w, b->albedo.w) && brush_float_equal(a->metallic, b->metallic) &&
           brush_float_equal(a->roughness, b->roughness) && brush_float_equal(a->emissive.x, b->emissive.x) &&
           brush_float_equal(a->emissive.y, b->emissive.y) && brush_float_equal(a->emissive.z, b->emissive.z);
}

static int brush_model_build_material_batches(const slayer3d_game_data_brush_world *world, int *material_to_batch,
                                              int *batch_material_indices)
{
    if (world == NULL || material_to_batch == NULL || batch_material_indices == NULL)
        return 0;

    int batch_count = 0;
    for (int material_index = 0; material_index < world->material_count; ++material_index)
    {
        material_to_batch[material_index] = -1;
        for (int batch_index = 0; batch_index < batch_count; ++batch_index)
        {
            const int representative = batch_material_indices[batch_index];
            if (brush_materials_render_equivalent(&world->materials[material_index], &world->materials[representative]))
            {
                material_to_batch[material_index] = batch_index;
                break;
            }
        }
        if (material_to_batch[material_index] < 0)
        {
            material_to_batch[material_index] = batch_count;
            batch_material_indices[batch_count++] = material_index;
        }
    }
    return batch_count;
}

static void brush_compile_set_error(char *error_buffer, int error_buffer_size, const char *world_name,
                                    const char *brush_name, int invalid_brush_count, int degenerate_face_count)
{
    if (error_buffer == NULL || error_buffer_size <= 0)
        return;
    SDL_snprintf(error_buffer, (size_t)error_buffer_size,
                 "brush world '%s' contains invalid brush geometry: first invalid brush '%s', invalid brushes %d, "
                 "degenerate faces %d",
                 world_name != NULL ? world_name : "<unnamed>", brush_name != NULL ? brush_name : "<unnamed>",
                 invalid_brush_count, degenerate_face_count);
    error_buffer[error_buffer_size - 1] = '\0';
}

bool slayer3d_game_data_brush_world_build_acceleration_checked(slayer3d_game_data_brush_world *world,
                                                               char *error_buffer, int error_buffer_size)
{
    if (world == NULL)
        return false;

    int max_face_count = 0;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
        max_face_count = SDL_max(max_face_count, world->brushes[brush_index].face_count);
    const int polygon_capacity = SDL_max(16, max_face_count * 2 + 8);
    slayer3d_vec3 *polygon = (slayer3d_vec3 *)SDL_malloc((size_t)polygon_capacity * sizeof(*polygon));
    if (polygon == NULL)
        return world->brush_count <= 0;

    bool world_has_bounds = false;
    slayer3d_bounding_box world_bounds;
    SDL_zero(world_bounds);
    int invalid_brush_count = 0;
    int degenerate_face_count = 0;
    const char *first_invalid_brush_name = NULL;
    slayer3d_game_data_brush *brushes = (slayer3d_game_data_brush *)world->brushes;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        slayer3d_game_data_brush *brush = &brushes[brush_index];
        bool brush_has_bounds = false;
        slayer3d_bounding_box brush_bounds;
        SDL_zero(brush_bounds);
        for (int face_index = 0; face_index < brush->face_count; ++face_index)
        {
            const int count = brush_build_face_polygon(brush, face_index, polygon, polygon_capacity, NULL, NULL, NULL);
            if (count < 3)
            {
                ++degenerate_face_count;
                continue;
            }
            for (int vertex_index = 0; vertex_index < count; ++vertex_index)
                brush_bounds_add_point(&brush_bounds, &brush_has_bounds, polygon[vertex_index]);
        }
        brush->has_bounds = brush_has_bounds;
        if (brush_has_bounds)
        {
            brush->bounds = brush_bounds;
            brush_bounds_add_point(&world_bounds, &world_has_bounds, brush_bounds.min);
            brush_bounds_add_point(&world_bounds, &world_has_bounds, brush_bounds.max);
        }
        else
        {
            ++invalid_brush_count;
            if (first_invalid_brush_name == NULL)
                first_invalid_brush_name = brush->name;
        }
    }
    world->has_bounds = world_has_bounds;
    if (world_has_bounds)
        world->bounds = world_bounds;
    SDL_free(polygon);
    world->compile_invalid_brush_count = invalid_brush_count;
    world->compile_degenerate_face_count = degenerate_face_count;
    if (invalid_brush_count > 0)
    {
        brush_compile_set_error(error_buffer, error_buffer_size, world->name, first_invalid_brush_name,
                                invalid_brush_count, degenerate_face_count);
        return false;
    }
    return true;
}

bool slayer3d_game_data_brush_world_build_acceleration(slayer3d_game_data_brush_world *world)
{
    return slayer3d_game_data_brush_world_build_acceleration_checked(world, NULL, 0);
}

void slayer3d_game_data_brush_world_free_compile_chunks(slayer3d_game_data_brush_world *world)
{
    if (world == NULL)
        return;
    SDL_free((void *)world->compile_chunks);
    SDL_free((void *)world->compile_chunk_brush_indices);
    world->compile_chunks = NULL;
    world->compile_chunk_count = 0;
    world->compile_chunk_brush_indices = NULL;
    world->compile_chunk_brush_index_count = 0;
    world->compile_chunk_cell_size = 0.0f;
}

static int brush_chunk_coord(float value, float min_value, float cell_size, int dim)
{
    if (cell_size <= 0.0f || dim <= 0)
        return 0;
    return SDL_clamp((int)SDL_floorf((value - min_value) / cell_size), 0, dim - 1);
}

static int brush_chunk_index_for_bounds(const slayer3d_bounding_box *world_bounds,
                                        const slayer3d_bounding_box *brush_bounds, float cell_size, int dim_x,
                                        int dim_y, int dim_z)
{
    if (world_bounds == NULL || brush_bounds == NULL)
        return 0;
    const slayer3d_vec3 center = slayer3d_vec3_scale(slayer3d_vec3_add(brush_bounds->min, brush_bounds->max), 0.5f);
    const int x = brush_chunk_coord(center.x, world_bounds->min.x, cell_size, dim_x);
    const int y = brush_chunk_coord(center.y, world_bounds->min.y, cell_size, dim_y);
    const int z = brush_chunk_coord(center.z, world_bounds->min.z, cell_size, dim_z);
    return x + y * dim_x + z * dim_x * dim_y;
}

static void brush_chunk_add_bounds(slayer3d_game_data_brush_compile_chunk *chunk, slayer3d_bounding_box bounds)
{
    if (chunk == NULL)
        return;
    if (!chunk->has_bounds)
    {
        chunk->bounds = bounds;
        chunk->has_bounds = true;
        return;
    }
    chunk->bounds.min.x = SDL_min(chunk->bounds.min.x, bounds.min.x);
    chunk->bounds.min.y = SDL_min(chunk->bounds.min.y, bounds.min.y);
    chunk->bounds.min.z = SDL_min(chunk->bounds.min.z, bounds.min.z);
    chunk->bounds.max.x = SDL_max(chunk->bounds.max.x, bounds.max.x);
    chunk->bounds.max.y = SDL_max(chunk->bounds.max.y, bounds.max.y);
    chunk->bounds.max.z = SDL_max(chunk->bounds.max.z, bounds.max.z);
}

bool slayer3d_game_data_brush_world_build_compile_chunks(slayer3d_game_data_brush_world *world)
{
    static const int max_chunk_cells = 4096;
    static const float min_chunk_cell_size = 1.0f;
    if (world == NULL)
        return false;
    if (!world->has_bounds || world->brush_count <= 0)
    {
        slayer3d_game_data_brush_world_free_compile_chunks(world);
        return true;
    }

    const float largest_extent =
        SDL_max(world->bounds.max.x - world->bounds.min.x,
                SDL_max(world->bounds.max.y - world->bounds.min.y, world->bounds.max.z - world->bounds.min.z));
    float cell_size = SDL_max(min_chunk_cell_size, SDL_max(world->visibility_cell_size * 4.0f, largest_extent / 16.0f));
    int dim_x = 1;
    int dim_y = 1;
    int dim_z = 1;
    int cell_count = 1;
    for (;;)
    {
        dim_x = SDL_max(1, (int)SDL_ceilf((world->bounds.max.x - world->bounds.min.x) / cell_size));
        dim_y = SDL_max(1, (int)SDL_ceilf((world->bounds.max.y - world->bounds.min.y) / cell_size));
        dim_z = SDL_max(1, (int)SDL_ceilf((world->bounds.max.z - world->bounds.min.z) / cell_size));
        if (dim_x <= 0 || dim_y <= 0 || dim_z <= 0)
            return false;
        if (dim_x <= max_chunk_cells / SDL_max(dim_y, 1) / SDL_max(dim_z, 1))
        {
            cell_count = dim_x * dim_y * dim_z;
            if (cell_count > 0 && cell_count <= max_chunk_cells)
                break;
        }
        cell_size *= 2.0f;
        if (cell_size > largest_extent + min_chunk_cell_size + 1.0f)
        {
            dim_x = dim_y = dim_z = 1;
            cell_count = 1;
            break;
        }
    }

    int *cell_counts = (int *)SDL_calloc((size_t)cell_count, sizeof(*cell_counts));
    int *cell_write_offsets = (int *)SDL_calloc((size_t)cell_count, sizeof(*cell_write_offsets));
    int *cell_to_chunk = (int *)SDL_malloc((size_t)cell_count * sizeof(*cell_to_chunk));
    if (cell_counts == NULL || cell_write_offsets == NULL || cell_to_chunk == NULL)
    {
        SDL_free(cell_counts);
        SDL_free(cell_write_offsets);
        SDL_free(cell_to_chunk);
        return false;
    }
    for (int i = 0; i < cell_count; ++i)
        cell_to_chunk[i] = -1;

    int chunk_brush_count = 0;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (!brush->has_bounds)
            continue;
        const int cell = brush_chunk_index_for_bounds(&world->bounds, &brush->bounds, cell_size, dim_x, dim_y, dim_z);
        if (cell < 0 || cell >= cell_count)
            continue;
        ++cell_counts[cell];
        ++chunk_brush_count;
    }

    int chunk_count = 0;
    for (int cell = 0; cell < cell_count; ++cell)
    {
        if (cell_counts[cell] > 0)
            cell_to_chunk[cell] = chunk_count++;
    }

    slayer3d_game_data_brush_compile_chunk *chunks =
        chunk_count > 0 ? (slayer3d_game_data_brush_compile_chunk *)SDL_calloc((size_t)chunk_count, sizeof(*chunks))
                        : NULL;
    int *chunk_indices =
        chunk_brush_count > 0 ? (int *)SDL_malloc((size_t)chunk_brush_count * sizeof(*chunk_indices)) : NULL;
    if ((chunks == NULL && chunk_count > 0) || (chunk_indices == NULL && chunk_brush_count > 0))
    {
        SDL_free(cell_counts);
        SDL_free(cell_write_offsets);
        SDL_free(cell_to_chunk);
        SDL_free(chunks);
        SDL_free(chunk_indices);
        return false;
    }

    int offset = 0;
    for (int cell = 0; cell < cell_count; ++cell)
    {
        const int chunk_index = cell_to_chunk[cell];
        if (chunk_index < 0)
            continue;
        chunks[chunk_index].brush_indices = &chunk_indices[offset];
        chunks[chunk_index].brush_count = cell_counts[cell];
        cell_write_offsets[cell] = offset;
        offset += cell_counts[cell];
    }

    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (!brush->has_bounds)
            continue;
        const int cell = brush_chunk_index_for_bounds(&world->bounds, &brush->bounds, cell_size, dim_x, dim_y, dim_z);
        if (cell < 0 || cell >= cell_count || cell_to_chunk[cell] < 0)
            continue;
        const int write = cell_write_offsets[cell]++;
        chunk_indices[write] = brush_index;
        slayer3d_game_data_brush_compile_chunk *chunk = &chunks[cell_to_chunk[cell]];
        chunk->contents_mask |= brush->contents;
        brush_chunk_add_bounds(chunk, brush->bounds);
    }

    slayer3d_game_data_brush_world_free_compile_chunks(world);
    world->compile_chunks = chunks;
    world->compile_chunk_count = chunk_count;
    world->compile_chunk_brush_indices = chunk_indices;
    world->compile_chunk_brush_index_count = chunk_brush_count;
    world->compile_chunk_cell_size = cell_size;

    SDL_free(cell_counts);
    SDL_free(cell_write_offsets);
    SDL_free(cell_to_chunk);
    return true;
}

static bool brush_model_copy_materials(const slayer3d_game_data_brush_world *world, slayer3d_model *model)
{
    if (world == NULL || model == NULL || world->material_count <= 0)
        return false;
    model->materials = (slayer3d_material *)SDL_calloc((size_t)world->material_count, sizeof(*model->materials));
    if (model->materials == NULL)
        return false;
    model->material_count = world->material_count;
    for (int i = 0; i < world->material_count; ++i)
    {
        const slayer3d_game_data_brush_material *src = &world->materials[i];
        slayer3d_material *dst = &model->materials[i];
        dst->name = SDL_strdup(src->name != NULL ? src->name : "");
        dst->albedo[0] = src->albedo.x;
        dst->albedo[1] = src->albedo.y;
        dst->albedo[2] = src->albedo.z;
        dst->albedo[3] = src->albedo.w;
        dst->metallic = src->metallic;
        dst->roughness = src->roughness;
        dst->emissive[0] = src->emissive.x;
        dst->emissive[1] = src->emissive.y;
        dst->emissive[2] = src->emissive.z;
        if (src->texture != NULL)
            dst->albedo_map = SDL_strdup(src->texture);
        if (dst->name == NULL || (src->texture != NULL && dst->albedo_map == NULL))
            return false;
    }
    return true;
}

static bool brush_index_in_render_filter(int brush_index, const int *filter_indices, int filter_count)
{
    if (filter_indices == NULL || filter_count <= 0)
        return true;
    for (int i = 0; i < filter_count; ++i)
    {
        if (filter_indices[i] == brush_index)
            return true;
    }
    return false;
}

static bool brush_polygon_point_inside_convex_2d(const slayer3d_vec3 *polygon, int count, float u, float v,
                                                 slayer3d_vec3 basis_u, slayer3d_vec3 basis_v)
{
    const float epsilon = 0.001f;
    float expected_sign = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const slayer3d_vec3 a3 = polygon[i];
        const slayer3d_vec3 b3 = polygon[(i + 1) % count];
        const float ax = slayer3d_vec3_dot(a3, basis_u);
        const float ay = slayer3d_vec3_dot(a3, basis_v);
        const float bx = slayer3d_vec3_dot(b3, basis_u);
        const float by = slayer3d_vec3_dot(b3, basis_v);
        const float cross = (bx - ax) * (v - ay) - (by - ay) * (u - ax);
        if (SDL_fabsf(cross) <= epsilon)
            continue;
        const float sign = cross < 0.0f ? -1.0f : 1.0f;
        if (expected_sign == 0.0f)
        {
            expected_sign = sign;
        }
        else if (sign != expected_sign)
        {
            return false;
        }
    }
    return true;
}

static bool brush_polygon_covers_polygon(const slayer3d_vec3 *covering, int covering_count,
                                         const slayer3d_vec3 *subject, int subject_count, slayer3d_vec3 basis_u,
                                         slayer3d_vec3 basis_v)
{
    if (covering == NULL || subject == NULL || covering_count < 3 || subject_count < 3)
        return false;

    for (int i = 0; i < subject_count; ++i)
    {
        const float u = slayer3d_vec3_dot(subject[i], basis_u);
        const float v = slayer3d_vec3_dot(subject[i], basis_v);
        if (!brush_polygon_point_inside_convex_2d(covering, covering_count, u, v, basis_u, basis_v))
            return false;
    }
    return true;
}

static bool brush_material_is_opaque(const slayer3d_game_data_brush_world *world, int material_index)
{
    if (world == NULL || material_index < 0 || material_index >= world->material_count)
        return false;
    return world->materials[material_index].albedo.w >= 0.999f;
}

static bool brush_face_can_be_compile_culled(const slayer3d_game_data_brush_world *world,
                                             const slayer3d_game_data_brush *brush,
                                             const slayer3d_game_data_brush_face *face)
{
    if (world == NULL || brush == NULL || face == NULL)
        return false;
    return (brush->contents & SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID) != 0u &&
           brush_material_is_opaque(world, face->material_index);
}

static bool brush_face_hidden_by_neighbor(const slayer3d_game_data_brush_world *world, int brush_index, int face_index,
                                          const slayer3d_vec3 *polygon, int polygon_count, slayer3d_vec3 normal,
                                          slayer3d_vec3 basis_u, slayer3d_vec3 basis_v, float distance,
                                          slayer3d_vec3 *scratch, int scratch_capacity)
{
    if (world == NULL || polygon == NULL || polygon_count < 3 || scratch == NULL || brush_index < 0 ||
        brush_index >= world->brush_count)
    {
        return false;
    }

    const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
    const slayer3d_game_data_brush_face *face = &brush->faces[face_index];
    if (!brush_face_can_be_compile_culled(world, brush, face))
        return false;

    const float normal_epsilon = 0.001f;
    const float distance_epsilon = 0.001f;
    for (int other_brush_index = 0; other_brush_index < world->brush_count; ++other_brush_index)
    {
        if (other_brush_index == brush_index)
            continue;
        const slayer3d_game_data_brush *other = &world->brushes[other_brush_index];
        if ((other->contents & SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID) == 0u)
            continue;

        for (int other_face_index = 0; other_face_index < other->face_count; ++other_face_index)
        {
            const slayer3d_game_data_brush_face *other_face = &other->faces[other_face_index];
            if (!brush_material_is_opaque(world, other_face->material_index))
                continue;

            slayer3d_vec3 other_normal;
            float other_distance = 0.0f;
            if (!brush_plane_normalized(other_face, &other_normal, &other_distance))
                continue;
            if (slayer3d_vec3_dot(normal, other_normal) > -1.0f + normal_epsilon)
                continue;
            if (SDL_fabsf(distance + other_distance) > distance_epsilon)
                continue;

            const int other_count =
                brush_build_face_polygon(other, other_face_index, scratch, scratch_capacity, NULL, NULL, NULL);
            if (other_count >= 3 &&
                brush_polygon_covers_polygon(scratch, other_count, polygon, polygon_count, basis_u, basis_v))
            {
                return true;
            }
        }
    }
    return false;
}

static bool brush_world_compile_render_model_filtered(slayer3d_game_data_brush_world *world, slayer3d_model *model,
                                                      const int *filter_indices, int filter_count,
                                                      bool update_compile_counters)
{
    if (world == NULL || model == NULL)
        return false;
    SDL_zero(*model);
    if (update_compile_counters)
    {
        world->compile_face_count = 0;
        world->compile_rendered_face_count = 0;
        world->compile_culled_face_count = 0;
        world->compile_triangle_count = 0;
    }
    if (!brush_model_copy_materials(world, model))
        return false;

    int *material_to_batch = (int *)SDL_malloc((size_t)world->material_count * sizeof(*material_to_batch));
    int *batch_material_indices = (int *)SDL_malloc((size_t)world->material_count * sizeof(*batch_material_indices));
    if (material_to_batch == NULL || batch_material_indices == NULL)
    {
        SDL_free(material_to_batch);
        SDL_free(batch_material_indices);
        return false;
    }
    const int batch_count = brush_model_build_material_batches(world, material_to_batch, batch_material_indices);
    int *triangle_counts = (int *)SDL_calloc((size_t)batch_count, sizeof(*triangle_counts));
    if (triangle_counts == NULL)
    {
        SDL_free(material_to_batch);
        SDL_free(batch_material_indices);
        return false;
    }

    int max_face_count = 0;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
        max_face_count = SDL_max(max_face_count, world->brushes[brush_index].face_count);
    const int polygon_capacity = SDL_max(16, max_face_count * 2 + 8);
    slayer3d_vec3 *polygon = (slayer3d_vec3 *)SDL_malloc((size_t)polygon_capacity * sizeof(*polygon));
    slayer3d_vec3 *neighbor_polygon = (slayer3d_vec3 *)SDL_malloc((size_t)polygon_capacity * sizeof(*neighbor_polygon));
    if (polygon == NULL || neighbor_polygon == NULL)
    {
        SDL_free(polygon);
        SDL_free(neighbor_polygon);
        SDL_free(material_to_batch);
        SDL_free(batch_material_indices);
        SDL_free(triangle_counts);
        return false;
    }

    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        if (!brush_index_in_render_filter(brush_index, filter_indices, filter_count))
            continue;
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (!brush_is_renderable(brush))
            continue;
        for (int face_index = 0; face_index < brush->face_count; ++face_index)
        {
            const int material_index = brush->faces[face_index].material_index;
            if (material_index < 0 || material_index >= world->material_count)
                continue;
            slayer3d_vec3 normal;
            slayer3d_vec3 u;
            slayer3d_vec3 v;
            float distance = 0.0f;
            if (!brush_plane_normalized(&brush->faces[face_index], &normal, &distance))
                continue;
            const int count = brush_build_face_polygon(brush, face_index, polygon, polygon_capacity, &normal, &u, &v);
            if (count >= 3)
            {
                if (update_compile_counters)
                    ++world->compile_face_count;
                if (brush_face_hidden_by_neighbor(world, brush_index, face_index, polygon, count, normal, u, v,
                                                  distance, neighbor_polygon, polygon_capacity))
                {
                    if (update_compile_counters)
                        ++world->compile_culled_face_count;
                    continue;
                }
                if (update_compile_counters)
                {
                    ++world->compile_rendered_face_count;
                    world->compile_triangle_count += count - 2;
                }
                triangle_counts[material_to_batch[material_index]] += count - 2;
            }
        }
    }

    int mesh_count = 0;
    for (int batch_index = 0; batch_index < batch_count; ++batch_index)
    {
        if (triangle_counts[batch_index] > 0)
            ++mesh_count;
    }
    if (mesh_count <= 0)
    {
        SDL_free(polygon);
        SDL_free(neighbor_polygon);
        SDL_free(material_to_batch);
        SDL_free(batch_material_indices);
        SDL_free(triangle_counts);
        return true;
    }

    model->meshes = (slayer3d_mesh *)SDL_calloc((size_t)mesh_count, sizeof(*model->meshes));
    if (model->meshes == NULL)
    {
        SDL_free(polygon);
        SDL_free(neighbor_polygon);
        SDL_free(material_to_batch);
        SDL_free(batch_material_indices);
        SDL_free(triangle_counts);
        return false;
    }
    model->mesh_count = mesh_count;

    int mesh_index = 0;
    int *batch_to_mesh = (int *)SDL_malloc((size_t)batch_count * sizeof(*batch_to_mesh));
    int *vertex_offsets = (int *)SDL_calloc((size_t)batch_count, sizeof(*vertex_offsets));
    if (batch_to_mesh == NULL || vertex_offsets == NULL)
    {
        SDL_free(batch_to_mesh);
        SDL_free(vertex_offsets);
        SDL_free(polygon);
        SDL_free(neighbor_polygon);
        SDL_free(material_to_batch);
        SDL_free(batch_material_indices);
        SDL_free(triangle_counts);
        return false;
    }
    for (int batch_index = 0; batch_index < batch_count; ++batch_index)
    {
        batch_to_mesh[batch_index] = -1;
        const int triangles = triangle_counts[batch_index];
        if (triangles <= 0)
            continue;
        const int material_index = batch_material_indices[batch_index];
        slayer3d_mesh *mesh = &model->meshes[mesh_index];
        const int vertex_count = triangles * 3;
        mesh->name =
            SDL_strdup(world->materials[material_index].name != NULL ? world->materials[material_index].name : "");
        mesh->positions = (float *)SDL_calloc((size_t)vertex_count * 3u, sizeof(float));
        mesh->normals = (float *)SDL_calloc((size_t)vertex_count * 3u, sizeof(float));
        mesh->uvs = (float *)SDL_calloc((size_t)vertex_count * 2u, sizeof(float));
        mesh->colors = (float *)SDL_calloc((size_t)vertex_count * 4u, sizeof(float));
        mesh->indices = (unsigned int *)SDL_calloc((size_t)vertex_count, sizeof(unsigned int));
        mesh->vertex_count = vertex_count;
        mesh->index_count = vertex_count;
        mesh->material_index = material_index;
        if (mesh->name == NULL || mesh->positions == NULL || mesh->normals == NULL || mesh->uvs == NULL ||
            mesh->colors == NULL || mesh->indices == NULL)
        {
            slayer3d_free_model(model);
            SDL_free(batch_to_mesh);
            SDL_free(vertex_offsets);
            SDL_free(polygon);
            SDL_free(neighbor_polygon);
            SDL_free(material_to_batch);
            SDL_free(batch_material_indices);
            SDL_free(triangle_counts);
            return false;
        }
        for (int i = 0; i < vertex_count; ++i)
            mesh->indices[i] = (unsigned int)i;
        batch_to_mesh[batch_index] = mesh_index++;
    }

    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        if (!brush_index_in_render_filter(brush_index, filter_indices, filter_count))
            continue;
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (!brush_is_renderable(brush))
            continue;
        for (int face_index = 0; face_index < brush->face_count; ++face_index)
        {
            const slayer3d_game_data_brush_face *face = &brush->faces[face_index];
            const int batch_for_material = face->material_index >= 0 && face->material_index < world->material_count
                                               ? material_to_batch[face->material_index]
                                               : -1;
            const int mesh_for_material =
                batch_for_material >= 0 && batch_for_material < batch_count ? batch_to_mesh[batch_for_material] : -1;
            slayer3d_vec3 normal;
            slayer3d_vec3 u;
            slayer3d_vec3 v;
            const int count = brush_build_face_polygon(brush, face_index, polygon, polygon_capacity, &normal, &u, &v);
            if (count < 3 || mesh_for_material < 0)
                continue;
            float distance = 0.0f;
            if (!brush_plane_normalized(face, &normal, &distance) ||
                brush_face_hidden_by_neighbor(world, brush_index, face_index, polygon, count, normal, u, v, distance,
                                              neighbor_polygon, polygon_capacity))
            {
                continue;
            }
            slayer3d_mesh *mesh = &model->meshes[mesh_for_material];
            const slayer3d_game_data_brush_material *material = &world->materials[face->material_index];
            const float tex_scale = SDL_max(material->tex_scale, 0.0001f);
            const float uv_scale_x = face->uv_scale[0] != 0.0f ? face->uv_scale[0] : 1.0f;
            const float uv_scale_y = face->uv_scale[1] != 0.0f ? face->uv_scale[1] : 1.0f;
            const float uv_rotation = face->uv_rotation_degrees * SDL_PI_F / 180.0f;
            const float uv_cos = SDL_cosf(uv_rotation);
            const float uv_sin = SDL_sinf(uv_rotation);
            const slayer3d_color color = brush_material_color(material);
            const float rgba[4] = {
                (float)color.r / 255.0f,
                (float)color.g / 255.0f,
                (float)color.b / 255.0f,
                (float)color.a / 255.0f,
            };
            for (int tri = 1; tri + 1 < count; ++tri)
            {
                const slayer3d_vec3 verts[3] = {polygon[0], polygon[tri], polygon[tri + 1]};
                for (int corner = 0; corner < 3; ++corner)
                {
                    const int vertex = vertex_offsets[batch_for_material]++;
                    if (vertex >= mesh->vertex_count)
                        continue;
                    mesh->positions[vertex * 3 + 0] = verts[corner].x;
                    mesh->positions[vertex * 3 + 1] = verts[corner].y;
                    mesh->positions[vertex * 3 + 2] = verts[corner].z;
                    mesh->normals[vertex * 3 + 0] = normal.x;
                    mesh->normals[vertex * 3 + 1] = normal.y;
                    mesh->normals[vertex * 3 + 2] = normal.z;
                    const float base_u = slayer3d_vec3_dot(verts[corner], u) / tex_scale;
                    const float base_v = slayer3d_vec3_dot(verts[corner], v) / tex_scale;
                    mesh->uvs[vertex * 2 + 0] = (base_u * uv_cos - base_v * uv_sin) * uv_scale_x + face->uv_offset[0];
                    mesh->uvs[vertex * 2 + 1] = (base_u * uv_sin + base_v * uv_cos) * uv_scale_y + face->uv_offset[1];
                    mesh->colors[vertex * 4 + 0] = rgba[0];
                    mesh->colors[vertex * 4 + 1] = rgba[1];
                    mesh->colors[vertex * 4 + 2] = rgba[2];
                    mesh->colors[vertex * 4 + 3] = rgba[3];
                    brush_mesh_bounds_add(mesh, verts[corner]);
                }
            }
        }
    }

    SDL_free(batch_to_mesh);
    SDL_free(vertex_offsets);
    SDL_free(polygon);
    SDL_free(neighbor_polygon);
    SDL_free(material_to_batch);
    SDL_free(batch_material_indices);
    SDL_free(triangle_counts);
    return true;
}

bool slayer3d_game_data_brush_world_compile_render_model(slayer3d_game_data_brush_world *world, slayer3d_model *model)
{
    if (!brush_world_compile_render_model_filtered(world, model, NULL, 0, true))
        return false;
    world->render_model = model->mesh_count > 0 ? model : NULL;
    return true;
}

bool slayer3d_game_data_brush_world_compile_brush_render_models(slayer3d_game_data_brush_world *world,
                                                                slayer3d_model *models, int model_count)
{
    if (world == NULL || models == NULL || model_count < world->brush_count)
        return false;

    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        if (!brush_world_compile_render_model_filtered(world, &models[brush_index], &brush_index, 1, false))
        {
            for (int cleanup = 0; cleanup <= brush_index; ++cleanup)
                slayer3d_free_model(&models[cleanup]);
            return false;
        }
    }
    return true;
}

bool slayer3d_game_data_brush_world_compile_chunk_render_models(slayer3d_game_data_brush_world *world,
                                                                slayer3d_model *models, int model_count)
{
    if (world == NULL || models == NULL || model_count < world->compile_chunk_count)
        return false;

    for (int chunk_index = 0; chunk_index < world->compile_chunk_count; ++chunk_index)
    {
        const slayer3d_game_data_brush_compile_chunk *chunk = &world->compile_chunks[chunk_index];
        if (!brush_world_compile_render_model_filtered(world, &models[chunk_index], chunk->brush_indices,
                                                       chunk->brush_count, false))
        {
            for (int cleanup = 0; cleanup <= chunk_index; ++cleanup)
                slayer3d_free_model(&models[cleanup]);
            return false;
        }
    }
    return true;
}

slayer3d_game_data_brush_trace_result slayer3d_game_data_brush_trace_default_result(
    const slayer3d_game_data_brush_trace_desc *desc)
{
    slayer3d_game_data_brush_trace_result result;
    SDL_zero(result);
    result.fraction = 1.0f;
    result.end_position = desc != NULL ? desc->end : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    result.point = result.end_position;
    result.brush_index = -1;
    result.face_index = -1;
    return result;
}

static float brush_trace_plane_support(const slayer3d_game_data_brush_trace_desc *desc, slayer3d_vec3 normal)
{
    if (desc == NULL)
        return 0.0f;
    if (desc->shape == SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE)
        return SDL_max(desc->extents.x, 0.0f);
    if (desc->shape == SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB)
    {
        return SDL_fabsf(normal.x) * SDL_max(desc->extents.x, 0.0f) +
               SDL_fabsf(normal.y) * SDL_max(desc->extents.y, 0.0f) +
               SDL_fabsf(normal.z) * SDL_max(desc->extents.z, 0.0f);
    }
    return 0.0f;
}

bool slayer3d_game_data_brush_trace_shape_valid(const slayer3d_game_data_brush_trace_desc *desc)
{
    if (desc == NULL || desc->contents_mask == 0u)
        return false;
    switch (desc->shape)
    {
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT:
        return true;
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE:
        return desc->extents.x >= 0.0f;
    case SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB:
        return desc->extents.x >= 0.0f && desc->extents.y >= 0.0f && desc->extents.z >= 0.0f;
    default:
        return false;
    }
}

static bool brush_trace_one_brush(const slayer3d_game_data_brush *brush,
                                  const slayer3d_game_data_brush_trace_desc *desc,
                                  slayer3d_game_data_brush_trace_result *out_result)
{
    const float epsilon = 0.0005f;
    bool starts_outside = false;
    bool ends_outside = false;
    float enter_fraction = -1.0f;
    float leave_fraction = 1.0f;
    int enter_face = -1;
    slayer3d_vec3 enter_normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);

    if (brush == NULL || desc == NULL || out_result == NULL || (brush->contents & desc->contents_mask) == 0u)
        return false;

    for (int face_index = 0; face_index < brush->face_count; ++face_index)
    {
        const slayer3d_game_data_brush_face *face = &brush->faces[face_index];
        if ((face->surface_flags & SLAYER3D_GAME_DATA_BRUSH_SURFACE_NO_COLLIDE) != 0u)
            continue;

        slayer3d_vec3 normal;
        float distance = 0.0f;
        if (!brush_plane_normalized(face, &normal, &distance))
            continue;

        const float expanded_distance = distance + brush_trace_plane_support(desc, normal);
        const float start_distance = slayer3d_vec3_dot(normal, desc->start) - expanded_distance;
        const float end_distance = slayer3d_vec3_dot(normal, desc->end) - expanded_distance;

        if (start_distance > epsilon)
            starts_outside = true;
        if (end_distance > epsilon)
            ends_outside = true;

        if (start_distance > epsilon && end_distance > epsilon)
            return false;
        if (start_distance <= epsilon && end_distance <= epsilon)
            continue;

        if (start_distance > end_distance)
        {
            const float denom = start_distance - end_distance;
            if (denom > 0.000001f)
            {
                float fraction = (start_distance - epsilon) / denom;
                fraction = SDL_clamp(fraction, 0.0f, 1.0f);
                if (fraction > enter_fraction)
                {
                    enter_fraction = fraction;
                    enter_face = face_index;
                    enter_normal = normal;
                }
            }
        }
        else
        {
            const float denom = start_distance - end_distance;
            if (SDL_fabsf(denom) > 0.000001f)
            {
                float fraction = (start_distance + epsilon) / denom;
                fraction = SDL_clamp(fraction, 0.0f, 1.0f);
                if (fraction < leave_fraction)
                    leave_fraction = fraction;
            }
        }
    }

    if (!starts_outside)
    {
        out_result->hit = true;
        out_result->start_solid = true;
        out_result->all_solid = !ends_outside;
        out_result->fraction = 0.0f;
        out_result->end_position = desc->start;
        out_result->point = desc->start;
        out_result->normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        out_result->face_index = -1;
        return true;
    }

    if (enter_face >= 0 && enter_fraction >= 0.0f && enter_fraction <= leave_fraction && enter_fraction <= 1.0f)
    {
        out_result->hit = true;
        out_result->fraction = enter_fraction;
        out_result->end_position = slayer3d_vec3_lerp(desc->start, desc->end, enter_fraction);
        out_result->point = out_result->end_position;
        out_result->normal = enter_normal;
        out_result->face_index = enter_face;
        return true;
    }

    return false;
}

static void brush_trace_consider_brush(const slayer3d_game_data_brush_world *world,
                                       const slayer3d_game_data_brush_trace_desc *desc, int brush_index,
                                       const slayer3d_bounding_box *trace_bounds, bool acceleration_enabled,
                                       slayer3d_game_data_brush_trace_result *closest,
                                       slayer3d_game_data_brush_diagnostics *diagnostics)
{
    if (world == NULL || desc == NULL || closest == NULL || brush_index < 0 || brush_index >= world->brush_count)
        return;

    const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
    if (diagnostics != NULL)
        ++diagnostics->brush_count;
    if ((brush->contents & desc->contents_mask) == 0u)
    {
        if (diagnostics != NULL)
            ++diagnostics->contents_reject_count;
        return;
    }
    if (acceleration_enabled && trace_bounds != NULL && brush->has_bounds &&
        !slayer3d_check_aabb_aabb(*trace_bounds, brush->bounds))
    {
        if (diagnostics != NULL)
            ++diagnostics->bounds_reject_count;
        return;
    }
    if (diagnostics != NULL)
        ++diagnostics->collision_candidate_count;
    slayer3d_game_data_brush_trace_result candidate = slayer3d_game_data_brush_trace_default_result(desc);
    if (!brush_trace_one_brush(brush, desc, &candidate))
        return;
    if (!closest->hit || candidate.fraction < closest->fraction || (candidate.start_solid && !closest->start_solid))
    {
        *closest = candidate;
        closest->world_name = world->name;
        closest->brush_name = brush->name;
        closest->brush_index = brush_index;
        closest->contents = brush->contents;
        if (closest->face_index >= 0 && closest->face_index < brush->face_count)
        {
            closest->material_name = brush->faces[closest->face_index].material_name;
            closest->surface_flags = brush->faces[closest->face_index].surface_flags;
        }
        else
        {
            closest->material_name = NULL;
            closest->surface_flags = 0u;
        }
    }
}

static bool brush_world_trace_chunks(const slayer3d_game_data_brush_world *world,
                                     const slayer3d_game_data_brush_trace_desc *desc,
                                     const slayer3d_bounding_box *trace_bounds,
                                     slayer3d_game_data_brush_trace_result *closest,
                                     slayer3d_game_data_brush_diagnostics *diagnostics)
{
    if (world == NULL || desc == NULL || trace_bounds == NULL || closest == NULL || world->compile_chunks == NULL ||
        world->compile_chunk_count <= 0)
    {
        return false;
    }

    for (int chunk_index = 0; chunk_index < world->compile_chunk_count; ++chunk_index)
    {
        const slayer3d_game_data_brush_compile_chunk *chunk = &world->compile_chunks[chunk_index];
        if (diagnostics != NULL)
            ++diagnostics->collision_chunk_count;
        if ((chunk->contents_mask & desc->contents_mask) == 0u ||
            (chunk->has_bounds && !slayer3d_check_aabb_aabb(*trace_bounds, chunk->bounds)))
        {
            if (diagnostics != NULL)
                ++diagnostics->collision_chunk_reject_count;
            continue;
        }
        for (int i = 0; i < chunk->brush_count; ++i)
            brush_trace_consider_brush(world, desc, chunk->brush_indices[i], trace_bounds, true, closest, diagnostics);
    }
    return true;
}

bool slayer3d_game_data_brush_world_trace_local(const slayer3d_game_data_brush_world *world,
                                                const slayer3d_game_data_brush_trace_desc *desc,
                                                slayer3d_game_data_brush_trace_result *out_result)
{
    return slayer3d_game_data_brush_world_trace_local_with_diagnostics(world, desc, true, out_result, NULL);
}

bool slayer3d_game_data_brush_world_trace_local_with_diagnostics(const slayer3d_game_data_brush_world *world,
                                                                 const slayer3d_game_data_brush_trace_desc *desc,
                                                                 bool acceleration_enabled,
                                                                 slayer3d_game_data_brush_trace_result *out_result,
                                                                 slayer3d_game_data_brush_diagnostics *diagnostics)
{
    slayer3d_game_data_brush_trace_result closest = slayer3d_game_data_brush_trace_default_result(desc);
    if (world == NULL || desc == NULL || out_result == NULL || !slayer3d_game_data_brush_trace_shape_valid(desc))
        return false;
    if (diagnostics != NULL)
        ++diagnostics->trace_count;

    const slayer3d_bounding_box trace_bounds = slayer3d_game_data_brush_trace_bounds(desc);
    const bool used_chunks =
        acceleration_enabled && brush_world_trace_chunks(world, desc, &trace_bounds, &closest, diagnostics);
    for (int brush_index = 0; !used_chunks && brush_index < world->brush_count; ++brush_index)
    {
        brush_trace_consider_brush(world, desc, brush_index, &trace_bounds, acceleration_enabled, &closest,
                                   diagnostics);
    }

    *out_result = closest;
    if (diagnostics != NULL && closest.hit)
        ++diagnostics->hit_count;
    return true;
}

static slayer3d_vec3 brush_clip_velocity(slayer3d_vec3 velocity, slayer3d_vec3 normal)
{
    const float overclip = 1.001f;
    float backoff = slayer3d_vec3_dot(velocity, normal);
    backoff = backoff < 0.0f ? backoff * overclip : backoff / overclip;
    return slayer3d_vec3_sub(velocity, slayer3d_vec3_scale(normal, backoff));
}

bool slayer3d_game_data_brush_slide_with_trace(slayer3d_game_data_brush_trace_fn trace_fn, void *trace_userdata,
                                               const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                               slayer3d_game_data_brush_trace_result *out_result)
{
    enum
    {
        MAX_CLIP_PLANES = 5
    };
    if (out_result != NULL)
        *out_result = slayer3d_game_data_brush_trace_default_result(desc);
    if (trace_fn == NULL || desc == NULL || out_result == NULL || !slayer3d_game_data_brush_trace_shape_valid(desc))
        return false;

    slayer3d_vec3 position = desc->start;
    slayer3d_vec3 velocity = slayer3d_vec3_sub(desc->end, desc->start);
    slayer3d_vec3 planes[MAX_CLIP_PLANES];
    int plane_count = 0;
    float time_left = 1.0f;
    slayer3d_game_data_brush_trace_result first_hit = slayer3d_game_data_brush_trace_default_result(desc);
    const int bump_count = SDL_clamp(max_bumps, 1, 8);

    if (slayer3d_vec3_length_squared(velocity) > 0.000001f)
        planes[plane_count++] = slayer3d_vec3_normalize(velocity);

    for (int bump = 0; bump < bump_count; ++bump)
    {
        slayer3d_game_data_brush_trace_desc step = *desc;
        step.start = position;
        step.end = slayer3d_vec3_add(position, slayer3d_vec3_scale(velocity, time_left));

        slayer3d_game_data_brush_trace_result trace;
        if (!trace_fn(trace_userdata, &step, &trace))
            return false;

        if (trace.fraction > 0.0f)
            position = trace.end_position;
        if (!trace.hit)
            break;
        if (!first_hit.hit)
            first_hit = trace;
        if (trace.start_solid)
        {
            position = trace.end_position;
            break;
        }
        position = slayer3d_vec3_add(position, slayer3d_vec3_scale(trace.normal, 0.001f));

        time_left -= time_left * SDL_clamp(trace.fraction, 0.0f, 1.0f);
        if (time_left <= 0.000001f)
            break;

        bool duplicate_plane = false;
        for (int i = 0; i < plane_count; ++i)
        {
            if (slayer3d_vec3_dot(trace.normal, planes[i]) > 0.99f)
            {
                velocity = slayer3d_vec3_add(velocity, trace.normal);
                duplicate_plane = true;
                break;
            }
        }
        if (duplicate_plane)
            continue;

        if (plane_count >= MAX_CLIP_PLANES)
        {
            velocity = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
            break;
        }
        planes[plane_count++] = trace.normal;

        slayer3d_vec3 clipped_velocity = velocity;
        for (int i = 0; i < plane_count; ++i)
        {
            if (slayer3d_vec3_dot(clipped_velocity, planes[i]) >= 0.1f)
                continue;

            clipped_velocity = brush_clip_velocity(velocity, planes[i]);

            for (int j = 0; j < plane_count; ++j)
            {
                if (j == i || slayer3d_vec3_dot(clipped_velocity, planes[j]) >= 0.1f)
                    continue;

                clipped_velocity = brush_clip_velocity(clipped_velocity, planes[j]);
                if (slayer3d_vec3_dot(clipped_velocity, planes[i]) >= 0.0f)
                    continue;

                slayer3d_vec3 crease = slayer3d_vec3_cross(planes[i], planes[j]);
                if (slayer3d_vec3_length_squared(crease) <= 0.000001f)
                {
                    clipped_velocity = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
                    break;
                }
                crease = slayer3d_vec3_normalize(crease);
                clipped_velocity = slayer3d_vec3_scale(crease, slayer3d_vec3_dot(crease, velocity));

                for (int k = 0; k < plane_count; ++k)
                {
                    if (k != i && k != j && slayer3d_vec3_dot(clipped_velocity, planes[k]) < 0.1f)
                    {
                        clipped_velocity = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
                        break;
                    }
                }
                break;
            }
            break;
        }

        velocity = clipped_velocity;
        if (slayer3d_vec3_length_squared(velocity) <= 0.000001f)
            break;
    }

    if (first_hit.hit)
    {
        *out_result = first_hit;
        out_result->end_position = position;
        out_result->point = position;
    }
    else
    {
        *out_result = slayer3d_game_data_brush_trace_default_result(desc);
        out_result->end_position = position;
        out_result->point = position;
    }
    return true;
}

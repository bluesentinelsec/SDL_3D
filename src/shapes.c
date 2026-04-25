#include "sdl3d/shapes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/drawing3d.h"
#include "sdl3d/math.h"

static const float SDL3D_SHAPES_PI = 3.14159265358979323846f;

static bool sdl3d_shape_require_finite(float value, const char *label, const char *function)
{
    if (!SDL_isinf(value) && !SDL_isnan(value))
    {
        return true;
    }
    return SDL_SetError("%s requires a finite %s.", function, label);
}

static bool sdl3d_shape_require_nonnegative(float value, const char *label, const char *function)
{
    if (!sdl3d_shape_require_finite(value, label, function))
    {
        return false;
    }
    if (value < 0.0f)
    {
        return SDL_SetError("%s requires %s >= 0.", function, label);
    }
    return true;
}

static bool sdl3d_shape_require_positive(float value, const char *label, const char *function)
{
    if (!sdl3d_shape_require_finite(value, label, function))
    {
        return false;
    }
    if (!(value > 0.0f))
    {
        return SDL_SetError("%s requires %s > 0.", function, label);
    }
    return true;
}

/*
 * Build a right-handed orthonormal basis {tangent, bitangent, axis}
 * from a unit axis. `axis` must be normalized. The tangent direction is
 * arbitrary but stable; capsules only need a consistent cross-section
 * frame, not any particular texture orientation.
 */
static void sdl3d_shape_basis_from_axis(sdl3d_vec3 axis, sdl3d_vec3 *out_tangent, sdl3d_vec3 *out_bitangent)
{
    sdl3d_vec3 reference = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    if (SDL_fabsf(axis.y) > 0.9f)
    {
        reference = sdl3d_vec3_make(1.0f, 0.0f, 0.0f);
    }
    sdl3d_vec3 tangent = sdl3d_vec3_normalize(sdl3d_vec3_cross(reference, axis));
    sdl3d_vec3 bitangent = sdl3d_vec3_cross(axis, tangent);
    *out_tangent = tangent;
    *out_bitangent = bitangent;
}

static sdl3d_vec3 sdl3d_shape_circle_point(sdl3d_vec3 center, sdl3d_vec3 tangent, sdl3d_vec3 bitangent, float radius,
                                           float angle_radians)
{
    const float c = SDL_cosf(angle_radians);
    const float s = SDL_sinf(angle_radians);
    sdl3d_vec3 result = center;
    result = sdl3d_vec3_add(result, sdl3d_vec3_scale(tangent, c * radius));
    result = sdl3d_vec3_add(result, sdl3d_vec3_scale(bitangent, s * radius));
    return result;
}

bool sdl3d_draw_cube(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size, sdl3d_color color)
{
    if (!sdl3d_shape_require_nonnegative(size.x, "size.x", "sdl3d_draw_cube") ||
        !sdl3d_shape_require_nonnegative(size.y, "size.y", "sdl3d_draw_cube") ||
        !sdl3d_shape_require_nonnegative(size.z, "size.z", "sdl3d_draw_cube"))
    {
        return false;
    }

    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;

    const int vert_count = 24;
    const int idx_count = 36;

    float *positions = (float *)SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    float *normals = (float *)SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)idx_count * sizeof(unsigned int));
    if (positions == NULL || normals == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(indices);
        return SDL_OutOfMemory();
    }

    const sdl3d_vec3 c[8] = {
        {center.x - hx, center.y - hy, center.z - hz}, {center.x + hx, center.y - hy, center.z - hz},
        {center.x - hx, center.y + hy, center.z - hz}, {center.x + hx, center.y + hy, center.z - hz},
        {center.x - hx, center.y - hy, center.z + hz}, {center.x + hx, center.y - hy, center.z + hz},
        {center.x - hx, center.y + hy, center.z + hz}, {center.x + hx, center.y + hy, center.z + hz},
    };

    static const int faces[6][4] = {
        {4, 5, 7, 6}, /* +Z */
        {1, 0, 2, 3}, /* -Z */
        {5, 1, 3, 7}, /* +X */
        {0, 4, 6, 2}, /* -X */
        {6, 7, 3, 2}, /* +Y */
        {0, 1, 5, 4}, /* -Y */
    };

    static const float face_normals[6][3] = {
        {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},  {0.0f, -1.0f, 0.0f},
    };

    for (int f = 0; f < 6; ++f)
    {
        int base = f * 4;
        for (int v = 0; v < 4; ++v)
        {
            int vi = base + v;
            positions[vi * 3 + 0] = c[faces[f][v]].x;
            positions[vi * 3 + 1] = c[faces[f][v]].y;
            positions[vi * 3 + 2] = c[faces[f][v]].z;
            normals[vi * 3 + 0] = face_normals[f][0];
            normals[vi * 3 + 1] = face_normals[f][1];
            normals[vi * 3 + 2] = face_normals[f][2];
        }
        int ii = f * 6;
        unsigned int b = (unsigned int)base;
        indices[ii + 0] = b + 0;
        indices[ii + 1] = b + 1;
        indices[ii + 2] = b + 2;
        indices[ii + 3] = b + 0;
        indices[ii + 4] = b + 2;
        indices[ii + 5] = b + 3;
    }

    sdl3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;

    bool result = sdl3d_draw_mesh(context, &mesh, NULL, color);

    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool sdl3d_draw_cube_textured(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size,
                              sdl3d_vec3 rotation_axis, float rotation_angle, const sdl3d_texture2d *texture,
                              sdl3d_color tint)
{
    if (!sdl3d_shape_require_nonnegative(size.x, "size.x", "sdl3d_draw_cube_textured") ||
        !sdl3d_shape_require_nonnegative(size.y, "size.y", "sdl3d_draw_cube_textured") ||
        !sdl3d_shape_require_nonnegative(size.z, "size.z", "sdl3d_draw_cube_textured"))
    {
        return false;
    }

    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;

    /* 8 corner positions (local space, centered at origin). */
    const sdl3d_vec3 c[8] = {
        {-hx, -hy, -hz}, {+hx, -hy, -hz}, {-hx, +hy, -hz}, {+hx, +hy, -hz},
        {-hx, -hy, +hz}, {+hx, -hy, +hz}, {-hx, +hy, +hz}, {+hx, +hy, +hz},
    };

    static const int faces[6][4] = {
        {4, 5, 7, 6}, {1, 0, 2, 3}, {5, 1, 3, 7}, {0, 4, 6, 2}, {6, 7, 3, 2}, {0, 1, 5, 4},
    };
    static const float fn[6][3] = {
        {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0},
    };
    static const float face_uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

    /* Build rotation + translation matrix. */
    sdl3d_mat4 xform = sdl3d_mat4_translate(center);
    float axis_len = SDL_sqrtf(rotation_axis.x * rotation_axis.x + rotation_axis.y * rotation_axis.y +
                               rotation_axis.z * rotation_axis.z);
    if (axis_len > 0.0001f && SDL_fabsf(rotation_angle) > 0.0001f)
    {
        sdl3d_mat4 rot = sdl3d_mat4_rotate(rotation_axis, rotation_angle);
        xform = sdl3d_mat4_multiply(xform, rot);
    }

    float positions[72], normals[72], uvs[48];
    unsigned int indices[36];

    for (int f = 0; f < 6; f++)
    {
        int b = f * 4;
        for (int v = 0; v < 4; v++)
        {
            int vi = b + v;

            /* Transform position. */
            sdl3d_vec4 lp = {c[faces[f][v]].x, c[faces[f][v]].y, c[faces[f][v]].z, 1.0f};
            sdl3d_vec4 wp = sdl3d_mat4_transform_vec4(xform, lp);
            positions[vi * 3 + 0] = wp.x;
            positions[vi * 3 + 1] = wp.y;
            positions[vi * 3 + 2] = wp.z;

            /* Transform normal (rotation only, no translation). */
            sdl3d_vec4 ln = {fn[f][0], fn[f][1], fn[f][2], 0.0f};
            sdl3d_vec4 wn = sdl3d_mat4_transform_vec4(xform, ln);
            normals[vi * 3 + 0] = wn.x;
            normals[vi * 3 + 1] = wn.y;
            normals[vi * 3 + 2] = wn.z;

            uvs[vi * 2 + 0] = face_uvs[v][0];
            uvs[vi * 2 + 1] = face_uvs[v][1];
        }
        int ii = f * 6;
        unsigned int bu = (unsigned int)b;
        indices[ii + 0] = bu;
        indices[ii + 1] = bu + 1;
        indices[ii + 2] = bu + 2;
        indices[ii + 3] = bu;
        indices[ii + 4] = bu + 2;
        indices[ii + 5] = bu + 3;
    }

    sdl3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.indices = indices;
    mesh.vertex_count = 24;
    mesh.index_count = 36;
    mesh.material_index = -1;
    return sdl3d_draw_mesh(context, &mesh, texture, tint);
}

bool sdl3d_draw_cube_wires(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec3 size, sdl3d_color color)
{
    if (!sdl3d_shape_require_nonnegative(size.x, "size.x", "sdl3d_draw_cube_wires") ||
        !sdl3d_shape_require_nonnegative(size.y, "size.y", "sdl3d_draw_cube_wires") ||
        !sdl3d_shape_require_nonnegative(size.z, "size.z", "sdl3d_draw_cube_wires"))
    {
        return false;
    }

    sdl3d_bounding_box box;
    box.min = sdl3d_vec3_make(center.x - size.x * 0.5f, center.y - size.y * 0.5f, center.z - size.z * 0.5f);
    box.max = sdl3d_vec3_make(center.x + size.x * 0.5f, center.y + size.y * 0.5f, center.z + size.z * 0.5f);
    return sdl3d_draw_bounding_box(context, box, color);
}

bool sdl3d_draw_plane(sdl3d_render_context *context, sdl3d_vec3 center, sdl3d_vec2 size, sdl3d_color color)
{
    /* Subdivide into a grid so vertex fog/lighting interpolates correctly.
     * All triangles are batched into a single mesh draw call. */
    static const float CELL_SIZE = 4.0f;
    int cols, rows, vert_count, idx_count, ii;
    float hx, hz, cell_w, cell_h;
    float *positions;
    float *normals;
    unsigned int *indices;
    sdl3d_mesh mesh;
    bool result;

    if (!sdl3d_shape_require_nonnegative(size.x, "size.x", "sdl3d_draw_plane") ||
        !sdl3d_shape_require_nonnegative(size.y, "size.y", "sdl3d_draw_plane"))
    {
        return false;
    }

    cols = (int)(size.x / CELL_SIZE);
    rows = (int)(size.y / CELL_SIZE);
    if (cols < 1)
    {
        cols = 1;
    }
    if (rows < 1)
    {
        rows = 1;
    }

    vert_count = (cols + 1) * (rows + 1);
    idx_count = cols * rows * 6;

    positions = (float *)SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    normals = (float *)SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    indices = (unsigned int *)SDL_malloc((size_t)idx_count * sizeof(unsigned int));
    if (positions == NULL || normals == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(indices);
        return SDL_OutOfMemory();
    }

    hx = size.x * 0.5f;
    hz = size.y * 0.5f;
    cell_w = size.x / (float)cols;
    cell_h = size.y / (float)rows;

    for (int rz = 0; rz <= rows; ++rz)
    {
        for (int cx = 0; cx <= cols; ++cx)
        {
            int vi = rz * (cols + 1) + cx;
            positions[vi * 3 + 0] = center.x - hx + (float)cx * cell_w;
            positions[vi * 3 + 1] = center.y;
            positions[vi * 3 + 2] = center.z - hz + (float)rz * cell_h;
            normals[vi * 3 + 0] = 0.0f;
            normals[vi * 3 + 1] = 1.0f;
            normals[vi * 3 + 2] = 0.0f;
        }
    }

    ii = 0;
    for (int rz = 0; rz < rows; ++rz)
    {
        for (int cx = 0; cx < cols; ++cx)
        {
            unsigned int tl = (unsigned int)(rz * (cols + 1) + cx);
            unsigned int tr = tl + 1;
            unsigned int bl = (unsigned int)((rz + 1) * (cols + 1) + cx);
            unsigned int br = bl + 1;
            indices[ii++] = bl;
            indices[ii++] = tr;
            indices[ii++] = tl;
            indices[ii++] = bl;
            indices[ii++] = br;
            indices[ii++] = tr;
        }
    }

    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;

    result = sdl3d_draw_mesh(context, &mesh, NULL, color);

    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool sdl3d_draw_grid(sdl3d_render_context *context, int slices, float spacing, sdl3d_color color)
{
    if (slices < 1)
    {
        return SDL_SetError("sdl3d_draw_grid requires slices >= 1.");
    }
    if (!sdl3d_shape_require_positive(spacing, "spacing", "sdl3d_draw_grid"))
    {
        return false;
    }

    const float half = (float)slices * spacing * 0.5f;
    for (int i = 0; i <= slices; ++i)
    {
        const float offset = -half + (float)i * spacing;
        const sdl3d_vec3 a = sdl3d_vec3_make(-half, 0.0f, offset);
        const sdl3d_vec3 b = sdl3d_vec3_make(half, 0.0f, offset);
        const sdl3d_vec3 c = sdl3d_vec3_make(offset, 0.0f, -half);
        const sdl3d_vec3 d = sdl3d_vec3_make(offset, 0.0f, half);
        if (!sdl3d_draw_line_3d(context, a, b, color))
        {
            return false;
        }
        if (!sdl3d_draw_line_3d(context, c, d, color))
        {
            return false;
        }
    }
    return true;
}

bool sdl3d_draw_ray(sdl3d_render_context *context, sdl3d_ray ray, sdl3d_color color)
{
    const sdl3d_vec3 end = sdl3d_vec3_add(ray.position, ray.direction);
    return sdl3d_draw_line_3d(context, ray.position, end, color);
}

bool sdl3d_draw_bounding_box(sdl3d_render_context *context, sdl3d_bounding_box box, sdl3d_color color)
{
    if (!(box.min.x <= box.max.x) || !(box.min.y <= box.max.y) || !(box.min.z <= box.max.z))
    {
        return SDL_SetError("sdl3d_draw_bounding_box requires min <= max componentwise.");
    }

    const sdl3d_vec3 corners[8] = {
        {box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z}, {box.min.x, box.max.y, box.min.z},
        {box.max.x, box.max.y, box.min.z}, {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.max.z}, {box.max.x, box.max.y, box.max.z},
    };

    /* Each edge lists the two corner indices it connects. */
    static const int edges[12][2] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7}, /* X-aligned */
        {0, 2}, {1, 3}, {4, 6}, {5, 7}, /* Y-aligned */
        {0, 4}, {1, 5}, {2, 6}, {3, 7}, /* Z-aligned */
    };

    for (int i = 0; i < 12; ++i)
    {
        if (!sdl3d_draw_line_3d(context, corners[edges[i][0]], corners[edges[i][1]], color))
        {
            return false;
        }
    }
    return true;
}

static sdl3d_vec3 sdl3d_sphere_point(sdl3d_vec3 center, float radius, float theta, float phi)
{
    const float sin_t = SDL_sinf(theta);
    const float cos_t = SDL_cosf(theta);
    const float sin_p = SDL_sinf(phi);
    const float cos_p = SDL_cosf(phi);
    return sdl3d_vec3_make(center.x + radius * sin_t * cos_p, center.y + radius * cos_t,
                           center.z + radius * sin_t * sin_p);
}

bool sdl3d_draw_sphere(sdl3d_render_context *context, sdl3d_vec3 center, float radius, int rings, int slices,
                       sdl3d_color color)
{
    if (!sdl3d_shape_require_nonnegative(radius, "radius", "sdl3d_draw_sphere"))
    {
        return false;
    }
    if (rings < 2)
    {
        return SDL_SetError("sdl3d_draw_sphere requires rings >= 2.");
    }
    if (slices < 3)
    {
        return SDL_SetError("sdl3d_draw_sphere requires slices >= 3.");
    }

    const int vert_count = (rings + 1) * (slices + 1);
    const int idx_count = rings * slices * 6;

    float *positions = (float *)SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    float *normals = (float *)SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)idx_count * sizeof(unsigned int));
    if (positions == NULL || normals == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(indices);
        return SDL_OutOfMemory();
    }

    for (int i = 0; i <= rings; ++i)
    {
        const float theta = SDL3D_SHAPES_PI * (float)i / (float)rings;
        for (int j = 0; j <= slices; ++j)
        {
            const float phi = 2.0f * SDL3D_SHAPES_PI * (float)j / (float)slices;
            int vi = i * (slices + 1) + j;
            float nx = SDL_sinf(theta) * SDL_cosf(phi);
            float ny = SDL_cosf(theta);
            float nz = SDL_sinf(theta) * SDL_sinf(phi);
            positions[vi * 3 + 0] = center.x + radius * nx;
            positions[vi * 3 + 1] = center.y + radius * ny;
            positions[vi * 3 + 2] = center.z + radius * nz;
            normals[vi * 3 + 0] = nx;
            normals[vi * 3 + 1] = ny;
            normals[vi * 3 + 2] = nz;
        }
    }

    int ii = 0;
    for (int i = 0; i < rings; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            unsigned int v00 = (unsigned int)(i * (slices + 1) + j);
            unsigned int v01 = v00 + 1;
            unsigned int v10 = (unsigned int)((i + 1) * (slices + 1) + j);
            unsigned int v11 = v10 + 1;
            indices[ii++] = v00;
            indices[ii++] = v01;
            indices[ii++] = v11;
            indices[ii++] = v00;
            indices[ii++] = v11;
            indices[ii++] = v10;
        }
    }

    sdl3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;

    bool result = sdl3d_draw_mesh(context, &mesh, NULL, color);

    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool sdl3d_draw_sphere_wires(sdl3d_render_context *context, sdl3d_vec3 center, float radius, int rings, int slices,
                             sdl3d_color color)
{
    if (!sdl3d_shape_require_nonnegative(radius, "radius", "sdl3d_draw_sphere_wires"))
    {
        return false;
    }
    if (rings < 2)
    {
        return SDL_SetError("sdl3d_draw_sphere_wires requires rings >= 2.");
    }
    if (slices < 3)
    {
        return SDL_SetError("sdl3d_draw_sphere_wires requires slices >= 3.");
    }

    /* Interior latitude rings (excluding the degenerate poles). */
    for (int i = 1; i < rings; ++i)
    {
        const float theta = SDL3D_SHAPES_PI * (float)i / (float)rings;
        sdl3d_vec3 previous = sdl3d_sphere_point(center, radius, theta, 0.0f);
        for (int j = 1; j <= slices; ++j)
        {
            const float phi = 2.0f * SDL3D_SHAPES_PI * (float)j / (float)slices;
            const sdl3d_vec3 next = sdl3d_sphere_point(center, radius, theta, phi);
            if (!sdl3d_draw_line_3d(context, previous, next, color))
            {
                return false;
            }
            previous = next;
        }
    }

    /* Meridians from north to south pole. */
    for (int j = 0; j < slices; ++j)
    {
        const float phi = 2.0f * SDL3D_SHAPES_PI * (float)j / (float)slices;
        sdl3d_vec3 previous = sdl3d_sphere_point(center, radius, 0.0f, phi);
        for (int i = 1; i <= rings; ++i)
        {
            const float theta = SDL3D_SHAPES_PI * (float)i / (float)rings;
            const sdl3d_vec3 next = sdl3d_sphere_point(center, radius, theta, phi);
            if (!sdl3d_draw_line_3d(context, previous, next, color))
            {
                return false;
            }
            previous = next;
        }
    }
    return true;
}

bool sdl3d_draw_cylinder(sdl3d_render_context *context, sdl3d_vec3 center, float radius_top, float radius_bottom,
                         float height, int slices, sdl3d_color color)
{
    if (!sdl3d_shape_require_nonnegative(radius_top, "radius_top", "sdl3d_draw_cylinder") ||
        !sdl3d_shape_require_nonnegative(radius_bottom, "radius_bottom", "sdl3d_draw_cylinder") ||
        !sdl3d_shape_require_nonnegative(height, "height", "sdl3d_draw_cylinder"))
    {
        return false;
    }
    if (slices < 3)
    {
        return SDL_SetError("sdl3d_draw_cylinder requires slices >= 3.");
    }

    const float hh = height * 0.5f;
    const sdl3d_vec3 top_center = sdl3d_vec3_make(center.x, center.y + hh, center.z);
    const sdl3d_vec3 bottom_center = sdl3d_vec3_make(center.x, center.y - hh, center.z);

    const int side_verts = 2 * (slices + 1);
    const int cap_verts = (slices + 2) * 2;
    const int vert_count = side_verts + cap_verts;
    const int side_idx = slices * 6;
    const int cap_idx = slices * 3 * 2;
    const int idx_count = side_idx + cap_idx;

    float *positions = (float *)SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    float *normals = (float *)SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)idx_count * sizeof(unsigned int));
    if (positions == NULL || normals == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(indices);
        return SDL_OutOfMemory();
    }

    /* Side vertices: top ring then bottom ring, each with slices+1 verts */
    for (int j = 0; j <= slices; ++j)
    {
        const float phi = 2.0f * SDL3D_SHAPES_PI * (float)j / (float)slices;
        const float c = SDL_cosf(phi);
        const float s = SDL_sinf(phi);
        float nx = c;
        float nz = s;

        int vi_top = j;
        positions[vi_top * 3 + 0] = top_center.x + radius_top * c;
        positions[vi_top * 3 + 1] = top_center.y;
        positions[vi_top * 3 + 2] = top_center.z + radius_top * s;
        normals[vi_top * 3 + 0] = nx;
        normals[vi_top * 3 + 1] = 0.0f;
        normals[vi_top * 3 + 2] = nz;

        int vi_bot = (slices + 1) + j;
        positions[vi_bot * 3 + 0] = bottom_center.x + radius_bottom * c;
        positions[vi_bot * 3 + 1] = bottom_center.y;
        positions[vi_bot * 3 + 2] = bottom_center.z + radius_bottom * s;
        normals[vi_bot * 3 + 0] = nx;
        normals[vi_bot * 3 + 1] = 0.0f;
        normals[vi_bot * 3 + 2] = nz;
    }

    /* Side indices */
    int ii = 0;
    for (int j = 0; j < slices; ++j)
    {
        unsigned int t0 = (unsigned int)j;
        unsigned int t1 = (unsigned int)(j + 1);
        unsigned int b0 = (unsigned int)(slices + 1 + j);
        unsigned int b1 = (unsigned int)(slices + 1 + j + 1);
        indices[ii++] = b0;
        indices[ii++] = t0;
        indices[ii++] = t1;
        indices[ii++] = b0;
        indices[ii++] = t1;
        indices[ii++] = b1;
    }

    /* Top cap vertices */
    int cap_base = side_verts;
    positions[cap_base * 3 + 0] = top_center.x;
    positions[cap_base * 3 + 1] = top_center.y;
    positions[cap_base * 3 + 2] = top_center.z;
    normals[cap_base * 3 + 0] = 0.0f;
    normals[cap_base * 3 + 1] = 1.0f;
    normals[cap_base * 3 + 2] = 0.0f;
    for (int j = 0; j <= slices; ++j)
    {
        const float phi = 2.0f * SDL3D_SHAPES_PI * (float)j / (float)slices;
        int vi = cap_base + 1 + j;
        positions[vi * 3 + 0] = top_center.x + radius_top * SDL_cosf(phi);
        positions[vi * 3 + 1] = top_center.y;
        positions[vi * 3 + 2] = top_center.z + radius_top * SDL_sinf(phi);
        normals[vi * 3 + 0] = 0.0f;
        normals[vi * 3 + 1] = 1.0f;
        normals[vi * 3 + 2] = 0.0f;
    }

    /* Top cap indices: CCW from +Y means center, v_{j+1}, v_j */
    for (int j = 0; j < slices; ++j)
    {
        indices[ii++] = (unsigned int)cap_base;
        indices[ii++] = (unsigned int)(cap_base + 1 + j + 1);
        indices[ii++] = (unsigned int)(cap_base + 1 + j);
    }

    /* Bottom cap vertices */
    int bot_cap_base = cap_base + slices + 2;
    positions[bot_cap_base * 3 + 0] = bottom_center.x;
    positions[bot_cap_base * 3 + 1] = bottom_center.y;
    positions[bot_cap_base * 3 + 2] = bottom_center.z;
    normals[bot_cap_base * 3 + 0] = 0.0f;
    normals[bot_cap_base * 3 + 1] = -1.0f;
    normals[bot_cap_base * 3 + 2] = 0.0f;
    for (int j = 0; j <= slices; ++j)
    {
        const float phi = 2.0f * SDL3D_SHAPES_PI * (float)j / (float)slices;
        int vi = bot_cap_base + 1 + j;
        positions[vi * 3 + 0] = bottom_center.x + radius_bottom * SDL_cosf(phi);
        positions[vi * 3 + 1] = bottom_center.y;
        positions[vi * 3 + 2] = bottom_center.z + radius_bottom * SDL_sinf(phi);
        normals[vi * 3 + 0] = 0.0f;
        normals[vi * 3 + 1] = -1.0f;
        normals[vi * 3 + 2] = 0.0f;
    }

    /* Bottom cap indices: CCW from -Y means center, v_j, v_{j+1} */
    for (int j = 0; j < slices; ++j)
    {
        indices[ii++] = (unsigned int)bot_cap_base;
        indices[ii++] = (unsigned int)(bot_cap_base + 1 + j);
        indices[ii++] = (unsigned int)(bot_cap_base + 1 + j + 1);
    }

    sdl3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;

    bool result = sdl3d_draw_mesh(context, &mesh, NULL, color);

    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool sdl3d_draw_cylinder_wires(sdl3d_render_context *context, sdl3d_vec3 center, float radius_top, float radius_bottom,
                               float height, int slices, sdl3d_color color)
{
    if (!sdl3d_shape_require_nonnegative(radius_top, "radius_top", "sdl3d_draw_cylinder_wires") ||
        !sdl3d_shape_require_nonnegative(radius_bottom, "radius_bottom", "sdl3d_draw_cylinder_wires") ||
        !sdl3d_shape_require_nonnegative(height, "height", "sdl3d_draw_cylinder_wires"))
    {
        return false;
    }
    if (slices < 3)
    {
        return SDL_SetError("sdl3d_draw_cylinder_wires requires slices >= 3.");
    }

    const float hh = height * 0.5f;
    const sdl3d_vec3 top_center = sdl3d_vec3_make(center.x, center.y + hh, center.z);
    const sdl3d_vec3 bottom_center = sdl3d_vec3_make(center.x, center.y - hh, center.z);
    const sdl3d_vec3 tangent = sdl3d_vec3_make(1.0f, 0.0f, 0.0f);
    const sdl3d_vec3 bitangent = sdl3d_vec3_make(0.0f, 0.0f, 1.0f);

    for (int j = 0; j < slices; ++j)
    {
        const float phi1 = 2.0f * SDL3D_SHAPES_PI * (float)j / (float)slices;
        const float phi2 = 2.0f * SDL3D_SHAPES_PI * (float)(j + 1) / (float)slices;

        const sdl3d_vec3 top1 = sdl3d_shape_circle_point(top_center, tangent, bitangent, radius_top, phi1);
        const sdl3d_vec3 top2 = sdl3d_shape_circle_point(top_center, tangent, bitangent, radius_top, phi2);
        const sdl3d_vec3 bot1 = sdl3d_shape_circle_point(bottom_center, tangent, bitangent, radius_bottom, phi1);
        const sdl3d_vec3 bot2 = sdl3d_shape_circle_point(bottom_center, tangent, bitangent, radius_bottom, phi2);

        if (radius_top > 0.0f && !sdl3d_draw_line_3d(context, top1, top2, color))
        {
            return false;
        }
        if (radius_bottom > 0.0f && !sdl3d_draw_line_3d(context, bot1, bot2, color))
        {
            return false;
        }
        if (!sdl3d_draw_line_3d(context, bot1, top1, color))
        {
            return false;
        }
    }
    return true;
}

/*
 * A capsule is a tube of `rings+1` ring samples up each hemisphere with
 * a single quad band connecting the two equators (which collapses to a
 * seam when start == end). Ring index 0 is the pole near `end`; ring
 * index 2*rings+1 is the pole near `start`. Rings 0..rings live on the
 * `end` hemisphere, rings rings+1..2*rings+1 live on the `start`
 * hemisphere, and the quad band between ring `rings` and ring `rings+1`
 * forms the cylindrical side.
 */
static bool sdl3d_draw_capsule_solid(sdl3d_render_context *context, sdl3d_vec3 start, sdl3d_vec3 end, float radius,
                                     int slices, int rings, sdl3d_color color, bool wireframe)
{
    sdl3d_vec3 axis_vec = sdl3d_vec3_sub(end, start);
    const float length = sdl3d_vec3_length(axis_vec);
    sdl3d_vec3 axis;
    if (length > 0.0f)
    {
        axis = sdl3d_vec3_scale(axis_vec, 1.0f / length);
    }
    else
    {
        axis = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    }

    sdl3d_vec3 tangent;
    sdl3d_vec3 bitangent;
    sdl3d_shape_basis_from_axis(axis, &tangent, &bitangent);

    /*
     * Ring `k` sits on `end` for k in [0, rings] and on `start` for k in
     * [rings+1, 2*rings+1]. The quad band between ring `rings` and ring
     * `rings+1` forms the cylindrical side; it collapses to a seam when
     * start == end.
     */
    struct ring_sample
    {
        float radius;
        sdl3d_vec3 center;
    };
    const int total_rings = 2 * rings + 2;
    for (int i = 0; i + 1 < total_rings; ++i)
    {
        struct ring_sample samples[2] = {0};
        for (int pass = 0; pass < 2; ++pass)
        {
            const int k = i + pass;
            if (k <= rings)
            {
                const float hemisphere_t = (float)k / (float)rings; /* 0 at pole, 1 at equator */
                const float theta_h = SDL3D_SHAPES_PI * 0.5f * hemisphere_t;
                samples[pass].radius = radius * SDL_sinf(theta_h);
                samples[pass].center = sdl3d_vec3_add(end, sdl3d_vec3_scale(axis, radius * SDL_cosf(theta_h)));
            }
            else
            {
                const int kb = k - (rings + 1);                      /* 0 at start equator, rings at pole */
                const float hemisphere_t = (float)kb / (float)rings; /* 0..1 */
                const float theta_h = SDL3D_SHAPES_PI * 0.5f * hemisphere_t;
                samples[pass].radius = radius * SDL_cosf(theta_h);
                samples[pass].center = sdl3d_vec3_add(start, sdl3d_vec3_scale(axis, -radius * SDL_sinf(theta_h)));
            }
        }
        const float r_this = samples[0].radius;
        const float r_next = samples[1].radius;
        const sdl3d_vec3 center_this = samples[0].center;
        const sdl3d_vec3 center_next = samples[1].center;

        for (int j = 0; j < slices; ++j)
        {
            const float phi1 = 2.0f * SDL3D_SHAPES_PI * (float)j / (float)slices;
            const float phi2 = 2.0f * SDL3D_SHAPES_PI * (float)(j + 1) / (float)slices;

            const sdl3d_vec3 v00 = sdl3d_shape_circle_point(center_this, tangent, bitangent, r_this, phi1);
            const sdl3d_vec3 v01 = sdl3d_shape_circle_point(center_this, tangent, bitangent, r_this, phi2);
            const sdl3d_vec3 v10 = sdl3d_shape_circle_point(center_next, tangent, bitangent, r_next, phi1);
            const sdl3d_vec3 v11 = sdl3d_shape_circle_point(center_next, tangent, bitangent, r_next, phi2);

            if (wireframe)
            {
                if (r_this > 0.0f && !sdl3d_draw_line_3d(context, v00, v01, color))
                {
                    return false;
                }
                if (!sdl3d_draw_line_3d(context, v00, v10, color))
                {
                    return false;
                }
            }
            else
            {
                /*
                 * sdl3d_shape_basis_from_axis orients phi so that rotation is
                 * CCW when viewed from +axis, which is opposite to the sphere
                 * parameterization used above. Swap v01 and v10 so the
                 * outward normal matches the capsule's exterior.
                 */
                if (!sdl3d_draw_triangle_3d(context, v00, v10, v11, color))
                {
                    return false;
                }
                if (!sdl3d_draw_triangle_3d(context, v00, v11, v01, color))
                {
                    return false;
                }
            }
        }
    }

    return true;
}

bool sdl3d_draw_capsule(sdl3d_render_context *context, sdl3d_vec3 start, sdl3d_vec3 end, float radius, int slices,
                        int rings, sdl3d_color color)
{
    if (!sdl3d_shape_require_nonnegative(radius, "radius", "sdl3d_draw_capsule"))
    {
        return false;
    }
    if (slices < 3)
    {
        return SDL_SetError("sdl3d_draw_capsule requires slices >= 3.");
    }
    if (rings < 1)
    {
        return SDL_SetError("sdl3d_draw_capsule requires rings >= 1.");
    }
    return sdl3d_draw_capsule_solid(context, start, end, radius, slices, rings, color, false);
}

bool sdl3d_draw_capsule_wires(sdl3d_render_context *context, sdl3d_vec3 start, sdl3d_vec3 end, float radius, int slices,
                              int rings, sdl3d_color color)
{
    if (!sdl3d_shape_require_nonnegative(radius, "radius", "sdl3d_draw_capsule_wires"))
    {
        return false;
    }
    if (slices < 3)
    {
        return SDL_SetError("sdl3d_draw_capsule_wires requires slices >= 3.");
    }
    if (rings < 1)
    {
        return SDL_SetError("sdl3d_draw_capsule_wires requires rings >= 1.");
    }
    return sdl3d_draw_capsule_solid(context, start, end, radius, slices, rings, color, true);
}

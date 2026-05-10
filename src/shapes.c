#include "slayer3d/shapes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/drawing3d.h"
#include "slayer3d/math.h"

static const float SLAYER3D_SHAPES_PI = 3.14159265358979323846f;

static bool slayer3d_shape_require_finite(float value, const char *label, const char *function)
{
    if (!SDL_isinf(value) && !SDL_isnan(value))
    {
        return true;
    }
    return SDL_SetError("%s requires a finite %s.", function, label);
}

static bool slayer3d_shape_require_nonnegative(float value, const char *label, const char *function)
{
    if (!slayer3d_shape_require_finite(value, label, function))
    {
        return false;
    }
    if (value < 0.0f)
    {
        return SDL_SetError("%s requires %s >= 0.", function, label);
    }
    return true;
}

static bool slayer3d_shape_require_positive(float value, const char *label, const char *function)
{
    if (!slayer3d_shape_require_finite(value, label, function))
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
static void slayer3d_shape_basis_from_axis(slayer3d_vec3 axis, slayer3d_vec3 *out_tangent, slayer3d_vec3 *out_bitangent)
{
    slayer3d_vec3 reference = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    if (SDL_fabsf(axis.y) > 0.9f)
    {
        reference = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    }
    slayer3d_vec3 tangent = slayer3d_vec3_normalize(slayer3d_vec3_cross(reference, axis));
    slayer3d_vec3 bitangent = slayer3d_vec3_cross(axis, tangent);
    *out_tangent = tangent;
    *out_bitangent = bitangent;
}

static slayer3d_vec3 slayer3d_shape_circle_point(slayer3d_vec3 center, slayer3d_vec3 tangent, slayer3d_vec3 bitangent,
                                                 float radius, float angle_radians)
{
    const float c = SDL_cosf(angle_radians);
    const float s = SDL_sinf(angle_radians);
    slayer3d_vec3 result = center;
    result = slayer3d_vec3_add(result, slayer3d_vec3_scale(tangent, c * radius));
    result = slayer3d_vec3_add(result, slayer3d_vec3_scale(bitangent, s * radius));
    return result;
}

bool slayer3d_draw_cube(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                        slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_cube") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_cube") ||
        !slayer3d_shape_require_nonnegative(size.z, "size.z", "slayer3d_draw_cube"))
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

    const slayer3d_vec3 c[8] = {
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

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;

    bool result = slayer3d_draw_mesh(context, &mesh, NULL, color);

    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool slayer3d_draw_cube_textured(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                                 slayer3d_vec3 rotation_axis, float rotation_angle, const slayer3d_texture2d *texture,
                                 slayer3d_color tint)
{
    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_cube_textured") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_cube_textured") ||
        !slayer3d_shape_require_nonnegative(size.z, "size.z", "slayer3d_draw_cube_textured"))
    {
        return false;
    }

    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;

    /* 8 corner positions (local space, centered at origin). */
    const slayer3d_vec3 c[8] = {
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
    slayer3d_mat4 xform = slayer3d_mat4_translate(center);
    float axis_len = SDL_sqrtf(rotation_axis.x * rotation_axis.x + rotation_axis.y * rotation_axis.y +
                               rotation_axis.z * rotation_axis.z);
    if (axis_len > 0.0001f && SDL_fabsf(rotation_angle) > 0.0001f)
    {
        slayer3d_mat4 rot = slayer3d_mat4_rotate(rotation_axis, rotation_angle);
        xform = slayer3d_mat4_multiply(xform, rot);
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
            slayer3d_vec4 lp = {c[faces[f][v]].x, c[faces[f][v]].y, c[faces[f][v]].z, 1.0f};
            slayer3d_vec4 wp = slayer3d_mat4_transform_vec4(xform, lp);
            positions[vi * 3 + 0] = wp.x;
            positions[vi * 3 + 1] = wp.y;
            positions[vi * 3 + 2] = wp.z;

            /* Transform normal (rotation only, no translation). */
            slayer3d_vec4 ln = {fn[f][0], fn[f][1], fn[f][2], 0.0f};
            slayer3d_vec4 wn = slayer3d_mat4_transform_vec4(xform, ln);
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

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.indices = indices;
    mesh.vertex_count = 24;
    mesh.index_count = 36;
    mesh.material_index = -1;
    return slayer3d_draw_mesh(context, &mesh, texture, tint);
}

bool slayer3d_draw_cube_wires(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                              slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_cube_wires") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_cube_wires") ||
        !slayer3d_shape_require_nonnegative(size.z, "size.z", "slayer3d_draw_cube_wires"))
    {
        return false;
    }

    slayer3d_bounding_box box;
    box.min = slayer3d_vec3_make(center.x - size.x * 0.5f, center.y - size.y * 0.5f, center.z - size.z * 0.5f);
    box.max = slayer3d_vec3_make(center.x + size.x * 0.5f, center.y + size.y * 0.5f, center.z + size.z * 0.5f);
    return slayer3d_draw_bounding_box(context, box, color);
}

bool slayer3d_draw_plane(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec2 size,
                         slayer3d_color color)
{
    /* Subdivide into a grid so vertex fog/lighting interpolates correctly.
     * All triangles are batched into a single mesh draw call. */
    static const float CELL_SIZE = 4.0f;
    int cols, rows, vert_count, idx_count, ii;
    float hx, hz, cell_w, cell_h;
    float *positions;
    float *normals;
    unsigned int *indices;
    slayer3d_mesh mesh;
    bool result;

    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_plane") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_plane"))
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

    result = slayer3d_draw_mesh(context, &mesh, NULL, color);

    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool slayer3d_draw_quad(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec2 size,
                        slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_quad") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_quad"))
    {
        return false;
    }

    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float positions[12] = {
        center.x - hx, center.y - hy, center.z, center.x + hx, center.y - hy, center.z,
        center.x + hx, center.y + hy, center.z, center.x - hx, center.y + hy, center.z,
    };
    const float normals[12] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    const unsigned int indices[6] = {0, 1, 2, 0, 2, 3};
    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = (float *)positions;
    mesh.normals = (float *)normals;
    mesh.indices = (unsigned int *)indices;
    mesh.vertex_count = 4;
    mesh.index_count = 6;
    return slayer3d_draw_mesh(context, &mesh, NULL, color);
}

bool slayer3d_draw_quad_wires(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec2 size,
                              slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_quad_wires") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_quad_wires"))
    {
        return false;
    }

    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const slayer3d_vec3 p[4] = {
        slayer3d_vec3_make(center.x - hx, center.y - hy, center.z),
        slayer3d_vec3_make(center.x + hx, center.y - hy, center.z),
        slayer3d_vec3_make(center.x + hx, center.y + hy, center.z),
        slayer3d_vec3_make(center.x - hx, center.y + hy, center.z),
    };
    static const int edges[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    for (int i = 0; i < 4; ++i)
    {
        if (!slayer3d_draw_line_3d(context, p[edges[i][0]], p[edges[i][1]], color))
            return false;
    }
    return true;
}

bool slayer3d_draw_disc(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int segments,
                        slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(radius, "radius", "slayer3d_draw_disc"))
        return false;
    if (segments < 3)
        return SDL_SetError("slayer3d_draw_disc requires segments >= 3.");

    const int vert_count = segments + 1;
    const int idx_count = segments * 3;
    float *positions = (float *)SDL_malloc((size_t)vert_count * 3U * sizeof(float));
    float *normals = (float *)SDL_malloc((size_t)vert_count * 3U * sizeof(float));
    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)idx_count * sizeof(unsigned int));
    if (positions == NULL || normals == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(indices);
        return SDL_OutOfMemory();
    }

    positions[0] = center.x;
    positions[1] = center.y;
    positions[2] = center.z;
    normals[0] = 0.0f;
    normals[1] = 0.0f;
    normals[2] = 1.0f;
    for (int i = 0; i < segments; ++i)
    {
        const float a = (float)i / (float)segments * SLAYER3D_SHAPES_PI * 2.0f;
        const int vi = i + 1;
        positions[vi * 3 + 0] = center.x + SDL_cosf(a) * radius;
        positions[vi * 3 + 1] = center.y + SDL_sinf(a) * radius;
        positions[vi * 3 + 2] = center.z;
        normals[vi * 3 + 0] = 0.0f;
        normals[vi * 3 + 1] = 0.0f;
        normals[vi * 3 + 2] = 1.0f;
    }
    for (int i = 0; i < segments; ++i)
    {
        indices[i * 3 + 0] = 0U;
        indices[i * 3 + 1] = (unsigned int)(i + 1);
        indices[i * 3 + 2] = (unsigned int)((i + 1) % segments + 1);
    }

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;
    const bool result = slayer3d_draw_mesh(context, &mesh, NULL, color);
    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool slayer3d_draw_disc_wires(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int segments,
                              slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(radius, "radius", "slayer3d_draw_disc_wires"))
        return false;
    if (segments < 3)
        return SDL_SetError("slayer3d_draw_disc_wires requires segments >= 3.");
    slayer3d_vec3 previous = slayer3d_vec3_make(center.x + radius, center.y, center.z);
    for (int i = 1; i <= segments; ++i)
    {
        const float a = (float)i / (float)segments * SLAYER3D_SHAPES_PI * 2.0f;
        const slayer3d_vec3 next =
            slayer3d_vec3_make(center.x + SDL_cosf(a) * radius, center.y + SDL_sinf(a) * radius, center.z);
        if (!slayer3d_draw_line_3d(context, previous, next, color))
            return false;
        previous = next;
    }
    return true;
}

bool slayer3d_draw_grid(slayer3d_render_context *context, int slices, float spacing, slayer3d_color color)
{
    if (slices < 1)
    {
        return SDL_SetError("slayer3d_draw_grid requires slices >= 1.");
    }
    if (!slayer3d_shape_require_positive(spacing, "spacing", "slayer3d_draw_grid"))
    {
        return false;
    }

    const float half = (float)slices * spacing * 0.5f;
    for (int i = 0; i <= slices; ++i)
    {
        const float offset = -half + (float)i * spacing;
        const slayer3d_vec3 a = slayer3d_vec3_make(-half, 0.0f, offset);
        const slayer3d_vec3 b = slayer3d_vec3_make(half, 0.0f, offset);
        const slayer3d_vec3 c = slayer3d_vec3_make(offset, 0.0f, -half);
        const slayer3d_vec3 d = slayer3d_vec3_make(offset, 0.0f, half);
        if (!slayer3d_draw_line_3d(context, a, b, color))
        {
            return false;
        }
        if (!slayer3d_draw_line_3d(context, c, d, color))
        {
            return false;
        }
    }
    return true;
}

bool slayer3d_draw_ray(slayer3d_render_context *context, slayer3d_ray ray, slayer3d_color color)
{
    const slayer3d_vec3 end = slayer3d_vec3_add(ray.position, ray.direction);
    return slayer3d_draw_line_3d(context, ray.position, end, color);
}

bool slayer3d_draw_bounding_box(slayer3d_render_context *context, slayer3d_bounding_box box, slayer3d_color color)
{
    if (!(box.min.x <= box.max.x) || !(box.min.y <= box.max.y) || !(box.min.z <= box.max.z))
    {
        return SDL_SetError("slayer3d_draw_bounding_box requires min <= max componentwise.");
    }

    const slayer3d_vec3 corners[8] = {
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
        if (!slayer3d_draw_line_3d(context, corners[edges[i][0]], corners[edges[i][1]], color))
        {
            return false;
        }
    }
    return true;
}

static slayer3d_vec3 slayer3d_sphere_point(slayer3d_vec3 center, float radius, float theta, float phi)
{
    const float sin_t = SDL_sinf(theta);
    const float cos_t = SDL_cosf(theta);
    const float sin_p = SDL_sinf(phi);
    const float cos_p = SDL_cosf(phi);
    return slayer3d_vec3_make(center.x + radius * sin_t * cos_p, center.y + radius * cos_t,
                              center.z + radius * sin_t * sin_p);
}

bool slayer3d_draw_sphere_textured(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int rings,
                                   int slices, slayer3d_vec3 rotation_axis, float rotation_angle,
                                   const slayer3d_texture2d *texture, slayer3d_color tint)
{
    if (!slayer3d_shape_require_nonnegative(radius, "radius", "slayer3d_draw_sphere"))
    {
        return false;
    }
    if (rings < 2)
    {
        return SDL_SetError("slayer3d_draw_sphere requires rings >= 2.");
    }
    if (slices < 3)
    {
        return SDL_SetError("slayer3d_draw_sphere requires slices >= 3.");
    }

    const int vert_count = (rings + 1) * (slices + 1);
    const int idx_count = rings * slices * 6;

    float *positions = (float *)SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    float *normals = (float *)SDL_malloc((size_t)vert_count * 3 * sizeof(float));
    float *uvs = (float *)SDL_malloc((size_t)vert_count * 2 * sizeof(float));
    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)idx_count * sizeof(unsigned int));
    if (positions == NULL || normals == NULL || uvs == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(uvs);
        SDL_free(indices);
        return SDL_OutOfMemory();
    }

    slayer3d_mat4 xform = slayer3d_mat4_translate(center);
    float axis_len = SDL_sqrtf(rotation_axis.x * rotation_axis.x + rotation_axis.y * rotation_axis.y +
                               rotation_axis.z * rotation_axis.z);
    if (axis_len > 0.0001f && SDL_fabsf(rotation_angle) > 0.0001f)
    {
        slayer3d_mat4 rot = slayer3d_mat4_rotate(rotation_axis, rotation_angle);
        xform = slayer3d_mat4_multiply(xform, rot);
    }

    for (int i = 0; i <= rings; ++i)
    {
        const float theta = SLAYER3D_SHAPES_PI * (float)i / (float)rings;
        for (int j = 0; j <= slices; ++j)
        {
            const float phi = 2.0f * SLAYER3D_SHAPES_PI * (float)j / (float)slices;
            int vi = i * (slices + 1) + j;
            float nx = SDL_sinf(theta) * SDL_cosf(phi);
            float ny = SDL_cosf(theta);
            float nz = SDL_sinf(theta) * SDL_sinf(phi);
            slayer3d_vec4 lp = {radius * nx, radius * ny, radius * nz, 1.0f};
            slayer3d_vec4 wp = slayer3d_mat4_transform_vec4(xform, lp);
            slayer3d_vec4 ln = {nx, ny, nz, 0.0f};
            slayer3d_vec4 wn = slayer3d_mat4_transform_vec4(xform, ln);
            positions[vi * 3 + 0] = wp.x;
            positions[vi * 3 + 1] = wp.y;
            positions[vi * 3 + 2] = wp.z;
            normals[vi * 3 + 0] = wn.x;
            normals[vi * 3 + 1] = wn.y;
            normals[vi * 3 + 2] = wn.z;
            uvs[vi * 2 + 0] = (float)j / (float)slices;
            uvs[vi * 2 + 1] = 1.0f - (float)i / (float)rings;
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

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;

    bool result = slayer3d_draw_mesh(context, &mesh, texture, tint);

    SDL_free(positions);
    SDL_free(normals);
    SDL_free(uvs);
    SDL_free(indices);
    return result;
}

bool slayer3d_draw_sphere(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int rings, int slices,
                          slayer3d_color color)
{
    return slayer3d_draw_sphere_textured(context, center, radius, rings, slices, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                         0.0f, NULL, color);
}

bool slayer3d_draw_sphere_wires(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int rings,
                                int slices, slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(radius, "radius", "slayer3d_draw_sphere_wires"))
    {
        return false;
    }
    if (rings < 2)
    {
        return SDL_SetError("slayer3d_draw_sphere_wires requires rings >= 2.");
    }
    if (slices < 3)
    {
        return SDL_SetError("slayer3d_draw_sphere_wires requires slices >= 3.");
    }

    /* Interior latitude rings (excluding the degenerate poles). */
    for (int i = 1; i < rings; ++i)
    {
        const float theta = SLAYER3D_SHAPES_PI * (float)i / (float)rings;
        slayer3d_vec3 previous = slayer3d_sphere_point(center, radius, theta, 0.0f);
        for (int j = 1; j <= slices; ++j)
        {
            const float phi = 2.0f * SLAYER3D_SHAPES_PI * (float)j / (float)slices;
            const slayer3d_vec3 next = slayer3d_sphere_point(center, radius, theta, phi);
            if (!slayer3d_draw_line_3d(context, previous, next, color))
            {
                return false;
            }
            previous = next;
        }
    }

    /* Meridians from north to south pole. */
    for (int j = 0; j < slices; ++j)
    {
        const float phi = 2.0f * SLAYER3D_SHAPES_PI * (float)j / (float)slices;
        slayer3d_vec3 previous = slayer3d_sphere_point(center, radius, 0.0f, phi);
        for (int i = 1; i <= rings; ++i)
        {
            const float theta = SLAYER3D_SHAPES_PI * (float)i / (float)rings;
            const slayer3d_vec3 next = slayer3d_sphere_point(center, radius, theta, phi);
            if (!slayer3d_draw_line_3d(context, previous, next, color))
            {
                return false;
            }
            previous = next;
        }
    }
    return true;
}

bool slayer3d_draw_hemisphere(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int rings,
                              int slices, slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(radius, "radius", "slayer3d_draw_hemisphere"))
        return false;
    if (rings < 2)
        return SDL_SetError("slayer3d_draw_hemisphere requires rings >= 2.");
    if (slices < 3)
        return SDL_SetError("slayer3d_draw_hemisphere requires slices >= 3.");

    const int curved_vertices = (rings + 1) * (slices + 1);
    const int cap_center = curved_vertices;
    const int vert_count = curved_vertices + 1;
    const int idx_count = rings * slices * 6 + slices * 3;
    float *positions = (float *)SDL_malloc((size_t)vert_count * 3U * sizeof(float));
    float *normals = (float *)SDL_malloc((size_t)vert_count * 3U * sizeof(float));
    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)idx_count * sizeof(unsigned int));
    if (positions == NULL || normals == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(indices);
        return SDL_OutOfMemory();
    }

    const float y_offset = radius * 0.5f;
    for (int r = 0; r <= rings; ++r)
    {
        const float t = (float)r / (float)rings;
        const float theta = t * SLAYER3D_SHAPES_PI * 0.5f;
        const float ring_radius = SDL_sinf(theta) * radius;
        const float y = SDL_cosf(theta) * radius - y_offset;
        for (int s = 0; s <= slices; ++s)
        {
            const float phi = (float)s / (float)slices * SLAYER3D_SHAPES_PI * 2.0f;
            const float x = SDL_cosf(phi) * ring_radius;
            const float z = SDL_sinf(phi) * ring_radius;
            const int vi = r * (slices + 1) + s;
            positions[vi * 3 + 0] = center.x + x;
            positions[vi * 3 + 1] = center.y + y;
            positions[vi * 3 + 2] = center.z + z;
            const slayer3d_vec3 normal = slayer3d_vec3_normalize(slayer3d_vec3_make(x, y + y_offset, z));
            normals[vi * 3 + 0] = normal.x;
            normals[vi * 3 + 1] = normal.y;
            normals[vi * 3 + 2] = normal.z;
        }
    }

    int ii = 0;
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < slices; ++s)
        {
            const unsigned int a = (unsigned int)(r * (slices + 1) + s);
            const unsigned int b = a + 1U;
            const unsigned int c = (unsigned int)((r + 1) * (slices + 1) + s);
            const unsigned int d = c + 1U;
            indices[ii++] = a;
            indices[ii++] = c;
            indices[ii++] = b;
            indices[ii++] = b;
            indices[ii++] = c;
            indices[ii++] = d;
        }
    }

    positions[cap_center * 3 + 0] = center.x;
    positions[cap_center * 3 + 1] = center.y - y_offset;
    positions[cap_center * 3 + 2] = center.z;
    normals[cap_center * 3 + 0] = 0.0f;
    normals[cap_center * 3 + 1] = -1.0f;
    normals[cap_center * 3 + 2] = 0.0f;
    const int base_row = rings * (slices + 1);
    for (int s = 0; s < slices; ++s)
    {
        indices[ii++] = (unsigned int)cap_center;
        indices[ii++] = (unsigned int)(base_row + s + 1);
        indices[ii++] = (unsigned int)(base_row + s);
    }

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;
    const bool result = slayer3d_draw_mesh(context, &mesh, NULL, color);
    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool slayer3d_draw_hemisphere_wires(slayer3d_render_context *context, slayer3d_vec3 center, float radius, int rings,
                                    int slices, slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(radius, "radius", "slayer3d_draw_hemisphere_wires"))
        return false;
    if (rings < 2)
        return SDL_SetError("slayer3d_draw_hemisphere_wires requires rings >= 2.");
    if (slices < 3)
        return SDL_SetError("slayer3d_draw_hemisphere_wires requires slices >= 3.");

    const float y_offset = radius * 0.5f;
    for (int r = 1; r <= rings; ++r)
    {
        const float theta = (float)r / (float)rings * SLAYER3D_SHAPES_PI * 0.5f;
        const float ring_radius = SDL_sinf(theta) * radius;
        const float y = center.y + SDL_cosf(theta) * radius - y_offset;
        slayer3d_vec3 previous = slayer3d_vec3_make(center.x + ring_radius, y, center.z);
        for (int s = 1; s <= slices; ++s)
        {
            const float phi = (float)s / (float)slices * SLAYER3D_SHAPES_PI * 2.0f;
            const slayer3d_vec3 next =
                slayer3d_vec3_make(center.x + SDL_cosf(phi) * ring_radius, y, center.z + SDL_sinf(phi) * ring_radius);
            if (!slayer3d_draw_line_3d(context, previous, next, color))
                return false;
            previous = next;
        }
    }
    for (int s = 0; s < slices; ++s)
    {
        const float phi = (float)s / (float)slices * SLAYER3D_SHAPES_PI * 2.0f;
        const slayer3d_vec3 pole = slayer3d_vec3_make(center.x, center.y + radius - y_offset, center.z);
        const slayer3d_vec3 edge = slayer3d_vec3_make(center.x + SDL_cosf(phi) * radius, center.y - y_offset,
                                                      center.z + SDL_sinf(phi) * radius);
        if (!slayer3d_draw_line_3d(context, pole, edge, color))
            return false;
    }
    return true;
}

bool slayer3d_draw_cylinder(slayer3d_render_context *context, slayer3d_vec3 center, float radius_top,
                            float radius_bottom, float height, int slices, slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(radius_top, "radius_top", "slayer3d_draw_cylinder") ||
        !slayer3d_shape_require_nonnegative(radius_bottom, "radius_bottom", "slayer3d_draw_cylinder") ||
        !slayer3d_shape_require_nonnegative(height, "height", "slayer3d_draw_cylinder"))
    {
        return false;
    }
    if (slices < 3)
    {
        return SDL_SetError("slayer3d_draw_cylinder requires slices >= 3.");
    }

    const float hh = height * 0.5f;
    const slayer3d_vec3 top_center = slayer3d_vec3_make(center.x, center.y + hh, center.z);
    const slayer3d_vec3 bottom_center = slayer3d_vec3_make(center.x, center.y - hh, center.z);

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
        const float phi = 2.0f * SLAYER3D_SHAPES_PI * (float)j / (float)slices;
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
        const float phi = 2.0f * SLAYER3D_SHAPES_PI * (float)j / (float)slices;
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
        const float phi = 2.0f * SLAYER3D_SHAPES_PI * (float)j / (float)slices;
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

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;

    bool result = slayer3d_draw_mesh(context, &mesh, NULL, color);

    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool slayer3d_draw_cylinder_wires(slayer3d_render_context *context, slayer3d_vec3 center, float radius_top,
                                  float radius_bottom, float height, int slices, slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(radius_top, "radius_top", "slayer3d_draw_cylinder_wires") ||
        !slayer3d_shape_require_nonnegative(radius_bottom, "radius_bottom", "slayer3d_draw_cylinder_wires") ||
        !slayer3d_shape_require_nonnegative(height, "height", "slayer3d_draw_cylinder_wires"))
    {
        return false;
    }
    if (slices < 3)
    {
        return SDL_SetError("slayer3d_draw_cylinder_wires requires slices >= 3.");
    }

    const float hh = height * 0.5f;
    const slayer3d_vec3 top_center = slayer3d_vec3_make(center.x, center.y + hh, center.z);
    const slayer3d_vec3 bottom_center = slayer3d_vec3_make(center.x, center.y - hh, center.z);
    const slayer3d_vec3 tangent = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    const slayer3d_vec3 bitangent = slayer3d_vec3_make(0.0f, 0.0f, 1.0f);

    for (int j = 0; j < slices; ++j)
    {
        const float phi1 = 2.0f * SLAYER3D_SHAPES_PI * (float)j / (float)slices;
        const float phi2 = 2.0f * SLAYER3D_SHAPES_PI * (float)(j + 1) / (float)slices;

        const slayer3d_vec3 top1 = slayer3d_shape_circle_point(top_center, tangent, bitangent, radius_top, phi1);
        const slayer3d_vec3 top2 = slayer3d_shape_circle_point(top_center, tangent, bitangent, radius_top, phi2);
        const slayer3d_vec3 bot1 = slayer3d_shape_circle_point(bottom_center, tangent, bitangent, radius_bottom, phi1);
        const slayer3d_vec3 bot2 = slayer3d_shape_circle_point(bottom_center, tangent, bitangent, radius_bottom, phi2);

        if (radius_top > 0.0f && !slayer3d_draw_line_3d(context, top1, top2, color))
        {
            return false;
        }
        if (radius_bottom > 0.0f && !slayer3d_draw_line_3d(context, bot1, bot2, color))
        {
            return false;
        }
        if (!slayer3d_draw_line_3d(context, bot1, top1, color))
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
static bool slayer3d_draw_capsule_solid(slayer3d_render_context *context, slayer3d_vec3 start, slayer3d_vec3 end,
                                        float radius, int slices, int rings, slayer3d_color color, bool wireframe)
{
    slayer3d_vec3 axis_vec = slayer3d_vec3_sub(end, start);
    const float length = slayer3d_vec3_length(axis_vec);
    slayer3d_vec3 axis;
    if (length > 0.0f)
    {
        axis = slayer3d_vec3_scale(axis_vec, 1.0f / length);
    }
    else
    {
        axis = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    }

    slayer3d_vec3 tangent;
    slayer3d_vec3 bitangent;
    slayer3d_shape_basis_from_axis(axis, &tangent, &bitangent);

    /*
     * Ring `k` sits on `end` for k in [0, rings] and on `start` for k in
     * [rings+1, 2*rings+1]. The quad band between ring `rings` and ring
     * `rings+1` forms the cylindrical side; it collapses to a seam when
     * start == end.
     */
    struct ring_sample
    {
        float radius;
        slayer3d_vec3 center;
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
                const float theta_h = SLAYER3D_SHAPES_PI * 0.5f * hemisphere_t;
                samples[pass].radius = radius * SDL_sinf(theta_h);
                samples[pass].center = slayer3d_vec3_add(end, slayer3d_vec3_scale(axis, radius * SDL_cosf(theta_h)));
            }
            else
            {
                const int kb = k - (rings + 1);                      /* 0 at start equator, rings at pole */
                const float hemisphere_t = (float)kb / (float)rings; /* 0..1 */
                const float theta_h = SLAYER3D_SHAPES_PI * 0.5f * hemisphere_t;
                samples[pass].radius = radius * SDL_cosf(theta_h);
                samples[pass].center = slayer3d_vec3_add(start, slayer3d_vec3_scale(axis, -radius * SDL_sinf(theta_h)));
            }
        }
        const float r_this = samples[0].radius;
        const float r_next = samples[1].radius;
        const slayer3d_vec3 center_this = samples[0].center;
        const slayer3d_vec3 center_next = samples[1].center;

        for (int j = 0; j < slices; ++j)
        {
            const float phi1 = 2.0f * SLAYER3D_SHAPES_PI * (float)j / (float)slices;
            const float phi2 = 2.0f * SLAYER3D_SHAPES_PI * (float)(j + 1) / (float)slices;

            const slayer3d_vec3 v00 = slayer3d_shape_circle_point(center_this, tangent, bitangent, r_this, phi1);
            const slayer3d_vec3 v01 = slayer3d_shape_circle_point(center_this, tangent, bitangent, r_this, phi2);
            const slayer3d_vec3 v10 = slayer3d_shape_circle_point(center_next, tangent, bitangent, r_next, phi1);
            const slayer3d_vec3 v11 = slayer3d_shape_circle_point(center_next, tangent, bitangent, r_next, phi2);

            if (wireframe)
            {
                if (r_this > 0.0f && !slayer3d_draw_line_3d(context, v00, v01, color))
                {
                    return false;
                }
                if (!slayer3d_draw_line_3d(context, v00, v10, color))
                {
                    return false;
                }
            }
            else
            {
                /*
                 * slayer3d_shape_basis_from_axis orients phi so that rotation is
                 * CCW when viewed from +axis, which is opposite to the sphere
                 * parameterization used above. Swap v01 and v10 so the
                 * outward normal matches the capsule's exterior.
                 */
                if (!slayer3d_draw_triangle_3d(context, v00, v10, v11, color))
                {
                    return false;
                }
                if (!slayer3d_draw_triangle_3d(context, v00, v11, v01, color))
                {
                    return false;
                }
            }
        }
    }

    return true;
}

bool slayer3d_draw_capsule(slayer3d_render_context *context, slayer3d_vec3 start, slayer3d_vec3 end, float radius,
                           int slices, int rings, slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(radius, "radius", "slayer3d_draw_capsule"))
    {
        return false;
    }
    if (slices < 3)
    {
        return SDL_SetError("slayer3d_draw_capsule requires slices >= 3.");
    }
    if (rings < 1)
    {
        return SDL_SetError("slayer3d_draw_capsule requires rings >= 1.");
    }
    return slayer3d_draw_capsule_solid(context, start, end, radius, slices, rings, color, false);
}

bool slayer3d_draw_capsule_wires(slayer3d_render_context *context, slayer3d_vec3 start, slayer3d_vec3 end, float radius,
                                 int slices, int rings, slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(radius, "radius", "slayer3d_draw_capsule_wires"))
    {
        return false;
    }
    if (slices < 3)
    {
        return SDL_SetError("slayer3d_draw_capsule_wires requires slices >= 3.");
    }
    if (rings < 1)
    {
        return SDL_SetError("slayer3d_draw_capsule_wires requires rings >= 1.");
    }
    return slayer3d_draw_capsule_solid(context, start, end, radius, slices, rings, color, true);
}

static void slayer3d_shape_store_vertex(float *positions, float *normals, int index, slayer3d_vec3 position,
                                        slayer3d_vec3 normal)
{
    positions[index * 3 + 0] = position.x;
    positions[index * 3 + 1] = position.y;
    positions[index * 3 + 2] = position.z;
    normals[index * 3 + 0] = normal.x;
    normals[index * 3 + 1] = normal.y;
    normals[index * 3 + 2] = normal.z;
}

static slayer3d_vec3 slayer3d_shape_face_normal(slayer3d_vec3 a, slayer3d_vec3 b, slayer3d_vec3 c)
{
    return slayer3d_vec3_normalize(slayer3d_vec3_cross(slayer3d_vec3_sub(b, a), slayer3d_vec3_sub(c, a)));
}

bool slayer3d_draw_torus(slayer3d_render_context *context, slayer3d_vec3 center, float major_radius, float minor_radius,
                         int segments, int tube_segments, slayer3d_color color)
{
    if (!slayer3d_shape_require_positive(major_radius, "major_radius", "slayer3d_draw_torus") ||
        !slayer3d_shape_require_positive(minor_radius, "minor_radius", "slayer3d_draw_torus"))
    {
        return false;
    }
    if (segments < 3)
        return SDL_SetError("slayer3d_draw_torus requires segments >= 3.");
    if (tube_segments < 3)
        return SDL_SetError("slayer3d_draw_torus requires tube_segments >= 3.");

    const int verts_per_ring = tube_segments + 1;
    const int vert_count = (segments + 1) * verts_per_ring;
    const int idx_count = segments * tube_segments * 6;
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

    for (int i = 0; i <= segments; ++i)
    {
        const float u = 2.0f * SLAYER3D_SHAPES_PI * (float)i / (float)segments;
        const float cu = SDL_cosf(u);
        const float su = SDL_sinf(u);
        for (int j = 0; j <= tube_segments; ++j)
        {
            const float v = 2.0f * SLAYER3D_SHAPES_PI * (float)j / (float)tube_segments;
            const float cv = SDL_cosf(v);
            const float sv = SDL_sinf(v);
            const int vi = i * verts_per_ring + j;
            const float radial = major_radius + minor_radius * cv;
            const slayer3d_vec3 normal = slayer3d_vec3_make(cv * cu, sv, cv * su);
            const slayer3d_vec3 position =
                slayer3d_vec3_make(center.x + radial * cu, center.y + minor_radius * sv, center.z + radial * su);
            slayer3d_shape_store_vertex(positions, normals, vi, position, normal);
        }
    }

    int ii = 0;
    for (int i = 0; i < segments; ++i)
    {
        for (int j = 0; j < tube_segments; ++j)
        {
            const unsigned int v00 = (unsigned int)(i * verts_per_ring + j);
            const unsigned int v01 = v00 + 1U;
            const unsigned int v10 = (unsigned int)((i + 1) * verts_per_ring + j);
            const unsigned int v11 = v10 + 1U;
            indices[ii++] = v00;
            indices[ii++] = v11;
            indices[ii++] = v10;
            indices[ii++] = v00;
            indices[ii++] = v01;
            indices[ii++] = v11;
        }
    }

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;
    const bool result = slayer3d_draw_mesh(context, &mesh, NULL, color);
    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool slayer3d_draw_torus_wires(slayer3d_render_context *context, slayer3d_vec3 center, float major_radius,
                               float minor_radius, int segments, int tube_segments, slayer3d_color color)
{
    if (!slayer3d_shape_require_positive(major_radius, "major_radius", "slayer3d_draw_torus_wires") ||
        !slayer3d_shape_require_positive(minor_radius, "minor_radius", "slayer3d_draw_torus_wires"))
    {
        return false;
    }
    if (segments < 3)
        return SDL_SetError("slayer3d_draw_torus_wires requires segments >= 3.");
    if (tube_segments < 3)
        return SDL_SetError("slayer3d_draw_torus_wires requires tube_segments >= 3.");

    for (int i = 0; i < segments; ++i)
    {
        const float u0 = 2.0f * SLAYER3D_SHAPES_PI * (float)i / (float)segments;
        const float u1 = 2.0f * SLAYER3D_SHAPES_PI * (float)(i + 1) / (float)segments;
        const float cu0 = SDL_cosf(u0), su0 = SDL_sinf(u0);
        const float cu1 = SDL_cosf(u1), su1 = SDL_sinf(u1);
        for (int j = 0; j < tube_segments; ++j)
        {
            const float v0 = 2.0f * SLAYER3D_SHAPES_PI * (float)j / (float)tube_segments;
            const float v1 = 2.0f * SLAYER3D_SHAPES_PI * (float)(j + 1) / (float)tube_segments;
            const float cv0 = SDL_cosf(v0), sv0 = SDL_sinf(v0);
            const float cv1 = SDL_cosf(v1), sv1 = SDL_sinf(v1);
            const slayer3d_vec3 p00 =
                slayer3d_vec3_make(center.x + (major_radius + minor_radius * cv0) * cu0, center.y + minor_radius * sv0,
                                   center.z + (major_radius + minor_radius * cv0) * su0);
            const slayer3d_vec3 p01 =
                slayer3d_vec3_make(center.x + (major_radius + minor_radius * cv1) * cu0, center.y + minor_radius * sv1,
                                   center.z + (major_radius + minor_radius * cv1) * su0);
            const slayer3d_vec3 p10 =
                slayer3d_vec3_make(center.x + (major_radius + minor_radius * cv0) * cu1, center.y + minor_radius * sv0,
                                   center.z + (major_radius + minor_radius * cv0) * su1);
            if (!slayer3d_draw_line_3d(context, p00, p01, color) || !slayer3d_draw_line_3d(context, p00, p10, color))
                return false;
        }
    }
    return true;
}

bool slayer3d_draw_tube_segment(slayer3d_render_context *context, slayer3d_vec3 center, float major_radius,
                                float minor_radius, float arc_angle, int segments, int tube_segments,
                                slayer3d_color color)
{
    if (!slayer3d_shape_require_positive(major_radius, "major_radius", "slayer3d_draw_tube_segment") ||
        !slayer3d_shape_require_positive(minor_radius, "minor_radius", "slayer3d_draw_tube_segment") ||
        !slayer3d_shape_require_positive(arc_angle, "arc_angle", "slayer3d_draw_tube_segment"))
    {
        return false;
    }
    if (segments < 3)
        return SDL_SetError("slayer3d_draw_tube_segment requires segments >= 3.");
    if (tube_segments < 3)
        return SDL_SetError("slayer3d_draw_tube_segment requires tube_segments >= 3.");

    const int arc_vertices = segments + 1;
    const int tube_vertices = tube_segments + 1;
    const int vert_count = arc_vertices * tube_vertices;
    const int idx_count = segments * tube_segments * 6;
    float *positions = (float *)SDL_malloc((size_t)vert_count * 3U * sizeof(float));
    float *normals = (float *)SDL_malloc((size_t)vert_count * 3U * sizeof(float));
    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)idx_count * sizeof(unsigned int));
    if (positions == NULL || normals == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(indices);
        return SDL_OutOfMemory();
    }

    const float start_angle = -arc_angle * 0.5f;
    for (int a = 0; a <= segments; ++a)
    {
        const float u = start_angle + (float)a / (float)segments * arc_angle;
        const slayer3d_vec3 radial = slayer3d_vec3_make(SDL_cosf(u), 0.0f, SDL_sinf(u));
        const slayer3d_vec3 ring_center =
            slayer3d_vec3_make(center.x + radial.x * major_radius, center.y, center.z + radial.z * major_radius);
        for (int t = 0; t <= tube_segments; ++t)
        {
            const float v = (float)t / (float)tube_segments * SLAYER3D_SHAPES_PI * 2.0f;
            const slayer3d_vec3 normal = slayer3d_vec3_normalize(
                slayer3d_vec3_make(radial.x * SDL_cosf(v), SDL_sinf(v), radial.z * SDL_cosf(v)));
            const int vi = a * tube_vertices + t;
            positions[vi * 3 + 0] = ring_center.x + normal.x * minor_radius;
            positions[vi * 3 + 1] = ring_center.y + normal.y * minor_radius;
            positions[vi * 3 + 2] = ring_center.z + normal.z * minor_radius;
            normals[vi * 3 + 0] = normal.x;
            normals[vi * 3 + 1] = normal.y;
            normals[vi * 3 + 2] = normal.z;
        }
    }

    int ii = 0;
    for (int a = 0; a < segments; ++a)
    {
        for (int t = 0; t < tube_segments; ++t)
        {
            const unsigned int p00 = (unsigned int)(a * tube_vertices + t);
            const unsigned int p01 = p00 + 1U;
            const unsigned int p10 = (unsigned int)((a + 1) * tube_vertices + t);
            const unsigned int p11 = p10 + 1U;
            indices[ii++] = p00;
            indices[ii++] = p10;
            indices[ii++] = p01;
            indices[ii++] = p01;
            indices[ii++] = p10;
            indices[ii++] = p11;
        }
    }

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;
    const bool result = slayer3d_draw_mesh(context, &mesh, NULL, color);
    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool slayer3d_draw_tube_segment_wires(slayer3d_render_context *context, slayer3d_vec3 center, float major_radius,
                                      float minor_radius, float arc_angle, int segments, int tube_segments,
                                      slayer3d_color color)
{
    if (!slayer3d_shape_require_positive(major_radius, "major_radius", "slayer3d_draw_tube_segment_wires") ||
        !slayer3d_shape_require_positive(minor_radius, "minor_radius", "slayer3d_draw_tube_segment_wires") ||
        !slayer3d_shape_require_positive(arc_angle, "arc_angle", "slayer3d_draw_tube_segment_wires"))
    {
        return false;
    }
    if (segments < 3)
        return SDL_SetError("slayer3d_draw_tube_segment_wires requires segments >= 3.");
    if (tube_segments < 3)
        return SDL_SetError("slayer3d_draw_tube_segment_wires requires tube_segments >= 3.");

    const float start_angle = -arc_angle * 0.5f;
    for (int a = 0; a <= segments; ++a)
    {
        const float u = start_angle + (float)a / (float)segments * arc_angle;
        const slayer3d_vec3 radial = slayer3d_vec3_make(SDL_cosf(u), 0.0f, SDL_sinf(u));
        const slayer3d_vec3 ring_center =
            slayer3d_vec3_make(center.x + radial.x * major_radius, center.y, center.z + radial.z * major_radius);
        slayer3d_vec3 previous = slayer3d_vec3_make(ring_center.x + radial.x * minor_radius, ring_center.y,
                                                    ring_center.z + radial.z * minor_radius);
        for (int t = 1; t <= tube_segments; ++t)
        {
            const float v = (float)t / (float)tube_segments * SLAYER3D_SHAPES_PI * 2.0f;
            const slayer3d_vec3 normal = slayer3d_vec3_normalize(
                slayer3d_vec3_make(radial.x * SDL_cosf(v), SDL_sinf(v), radial.z * SDL_cosf(v)));
            const slayer3d_vec3 next =
                slayer3d_vec3_make(ring_center.x + normal.x * minor_radius, ring_center.y + normal.y * minor_radius,
                                   ring_center.z + normal.z * minor_radius);
            if (!slayer3d_draw_line_3d(context, previous, next, color))
                return false;
            previous = next;
        }
    }

    for (int t = 0; t < tube_segments; ++t)
    {
        const float v = (float)t / (float)tube_segments * SLAYER3D_SHAPES_PI * 2.0f;
        slayer3d_vec3 previous = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        bool has_previous = false;
        for (int a = 0; a <= segments; ++a)
        {
            const float u = start_angle + (float)a / (float)segments * arc_angle;
            const slayer3d_vec3 radial = slayer3d_vec3_make(SDL_cosf(u), 0.0f, SDL_sinf(u));
            const slayer3d_vec3 ring_center =
                slayer3d_vec3_make(center.x + radial.x * major_radius, center.y, center.z + radial.z * major_radius);
            const slayer3d_vec3 normal = slayer3d_vec3_normalize(
                slayer3d_vec3_make(radial.x * SDL_cosf(v), SDL_sinf(v), radial.z * SDL_cosf(v)));
            const slayer3d_vec3 next =
                slayer3d_vec3_make(ring_center.x + normal.x * minor_radius, ring_center.y + normal.y * minor_radius,
                                   ring_center.z + normal.z * minor_radius);
            if (has_previous && !slayer3d_draw_line_3d(context, previous, next, color))
                return false;
            previous = next;
            has_previous = true;
        }
    }
    return true;
}

static slayer3d_vec3 slayer3d_shape_clamp_vec3(slayer3d_vec3 value, slayer3d_vec3 min_value, slayer3d_vec3 max_value)
{
    return slayer3d_vec3_make(SDL_clamp(value.x, min_value.x, max_value.x),
                              SDL_clamp(value.y, min_value.y, max_value.y),
                              SDL_clamp(value.z, min_value.z, max_value.z));
}

static void slayer3d_shape_rounded_box_project(slayer3d_vec3 local, slayer3d_vec3 inner, float radius,
                                               slayer3d_vec3 *out_position, slayer3d_vec3 *out_normal)
{
    const slayer3d_vec3 clamped =
        slayer3d_shape_clamp_vec3(local, slayer3d_vec3_make(-inner.x, -inner.y, -inner.z), inner);
    slayer3d_vec3 normal = slayer3d_vec3_sub(local, clamped);
    const float len_sq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
    if (len_sq <= 0.000001f)
        normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    else
        normal = slayer3d_vec3_normalize(normal);
    *out_position = slayer3d_vec3_add(clamped, slayer3d_vec3_scale(normal, radius));
    *out_normal = normal;
}

bool slayer3d_draw_rounded_box(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size, float radius,
                               int segments, slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_rounded_box") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_rounded_box") ||
        !slayer3d_shape_require_nonnegative(size.z, "size.z", "slayer3d_draw_rounded_box") ||
        !slayer3d_shape_require_nonnegative(radius, "radius", "slayer3d_draw_rounded_box"))
    {
        return false;
    }
    if (segments < 1)
        return SDL_SetError("slayer3d_draw_rounded_box requires segments >= 1.");
    const float max_radius = SDL_min(size.x, SDL_min(size.y, size.z)) * 0.5f;
    radius = SDL_min(radius, max_radius);
    if (radius <= 0.000001f)
        return slayer3d_draw_cube(context, center, size, color);

    const int grid = SDL_max(segments + 2, 3);
    const int face_vertices = grid * grid;
    const int vert_count = face_vertices * 6;
    const int idx_count = (grid - 1) * (grid - 1) * 6 * 6;
    float *positions = (float *)SDL_malloc((size_t)vert_count * 3U * sizeof(float));
    float *normals = (float *)SDL_malloc((size_t)vert_count * 3U * sizeof(float));
    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)idx_count * sizeof(unsigned int));
    if (positions == NULL || normals == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(indices);
        return SDL_OutOfMemory();
    }

    const slayer3d_vec3 half = slayer3d_vec3_scale(size, 0.5f);
    const slayer3d_vec3 inner = slayer3d_vec3_make(SDL_max(half.x - radius, 0.0f), SDL_max(half.y - radius, 0.0f),
                                                   SDL_max(half.z - radius, 0.0f));
    int vi = 0;
    for (int face = 0; face < 6; ++face)
    {
        const int axis = face / 2;
        const float sign = (face % 2) == 0 ? -1.0f : 1.0f;
        for (int y = 0; y < grid; ++y)
        {
            const float v = -1.0f + 2.0f * (float)y / (float)(grid - 1);
            for (int x = 0; x < grid; ++x)
            {
                const float u = -1.0f + 2.0f * (float)x / (float)(grid - 1);
                slayer3d_vec3 local;
                if (axis == 0)
                    local = slayer3d_vec3_make(sign * half.x, u * half.y, v * half.z);
                else if (axis == 1)
                    local = slayer3d_vec3_make(u * half.x, sign * half.y, v * half.z);
                else
                    local = slayer3d_vec3_make(u * half.x, v * half.y, sign * half.z);
                slayer3d_vec3 projected;
                slayer3d_vec3 normal;
                slayer3d_shape_rounded_box_project(local, inner, radius, &projected, &normal);
                positions[vi * 3 + 0] = center.x + projected.x;
                positions[vi * 3 + 1] = center.y + projected.y;
                positions[vi * 3 + 2] = center.z + projected.z;
                normals[vi * 3 + 0] = normal.x;
                normals[vi * 3 + 1] = normal.y;
                normals[vi * 3 + 2] = normal.z;
                ++vi;
            }
        }
    }

    int ii = 0;
    for (int face = 0; face < 6; ++face)
    {
        const int base = face * face_vertices;
        for (int y = 0; y < grid - 1; ++y)
        {
            for (int x = 0; x < grid - 1; ++x)
            {
                const unsigned int a = (unsigned int)(base + y * grid + x);
                const unsigned int b = a + 1U;
                const unsigned int c = (unsigned int)(base + (y + 1) * grid + x);
                const unsigned int d = c + 1U;
                indices[ii++] = a;
                indices[ii++] = c;
                indices[ii++] = b;
                indices[ii++] = b;
                indices[ii++] = c;
                indices[ii++] = d;
            }
        }
    }

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = vert_count;
    mesh.index_count = idx_count;
    const bool result = slayer3d_draw_mesh(context, &mesh, NULL, color);
    SDL_free(positions);
    SDL_free(normals);
    SDL_free(indices);
    return result;
}

bool slayer3d_draw_rounded_box_wires(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                                     float radius, int segments, slayer3d_color color)
{
    (void)radius;
    (void)segments;
    return slayer3d_draw_cube_wires(context, center, size, color);
}

bool slayer3d_draw_arrow(slayer3d_render_context *context, slayer3d_vec3 center, float radius, float height,
                         int segments, slayer3d_color color)
{
    if (!slayer3d_shape_require_positive(radius, "radius", "slayer3d_draw_arrow") ||
        !slayer3d_shape_require_positive(height, "height", "slayer3d_draw_arrow"))
    {
        return false;
    }
    if (segments < 3)
        return SDL_SetError("slayer3d_draw_arrow requires segments >= 3.");
    const float shaft_height = height * 0.65f;
    const float head_height = height - shaft_height;
    const float shaft_radius = radius * 0.35f;
    const slayer3d_vec3 shaft_center =
        slayer3d_vec3_make(center.x, center.y - height * 0.5f + shaft_height * 0.5f, center.z);
    const slayer3d_vec3 head_center =
        slayer3d_vec3_make(center.x, center.y + height * 0.5f - head_height * 0.5f, center.z);
    return slayer3d_draw_cylinder(context, shaft_center, shaft_radius, shaft_radius, shaft_height, segments, color) &&
           slayer3d_draw_cylinder(context, head_center, 0.0f, radius, head_height, segments, color);
}

bool slayer3d_draw_arrow_wires(slayer3d_render_context *context, slayer3d_vec3 center, float radius, float height,
                               int segments, slayer3d_color color)
{
    if (!slayer3d_shape_require_positive(radius, "radius", "slayer3d_draw_arrow_wires") ||
        !slayer3d_shape_require_positive(height, "height", "slayer3d_draw_arrow_wires"))
    {
        return false;
    }
    if (segments < 3)
        return SDL_SetError("slayer3d_draw_arrow_wires requires segments >= 3.");
    const float shaft_height = height * 0.65f;
    const float head_height = height - shaft_height;
    const float shaft_radius = radius * 0.35f;
    const slayer3d_vec3 shaft_center =
        slayer3d_vec3_make(center.x, center.y - height * 0.5f + shaft_height * 0.5f, center.z);
    const slayer3d_vec3 head_center =
        slayer3d_vec3_make(center.x, center.y + height * 0.5f - head_height * 0.5f, center.z);
    return slayer3d_draw_cylinder_wires(context, shaft_center, shaft_radius, shaft_radius, shaft_height, segments,
                                        color) &&
           slayer3d_draw_cylinder_wires(context, head_center, 0.0f, radius, head_height, segments, color);
}

bool slayer3d_draw_pyramid(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                           slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_pyramid") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_pyramid") ||
        !slayer3d_shape_require_nonnegative(size.z, "size.z", "slayer3d_draw_pyramid"))
    {
        return false;
    }

    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;
    const slayer3d_vec3 apex = slayer3d_vec3_make(center.x, center.y + hy, center.z);
    const slayer3d_vec3 bl = slayer3d_vec3_make(center.x - hx, center.y - hy, center.z - hz);
    const slayer3d_vec3 br = slayer3d_vec3_make(center.x + hx, center.y - hy, center.z - hz);
    const slayer3d_vec3 tr = slayer3d_vec3_make(center.x + hx, center.y - hy, center.z + hz);
    const slayer3d_vec3 tl = slayer3d_vec3_make(center.x - hx, center.y - hy, center.z + hz);
    const slayer3d_vec3 faces[6][3] = {
        {bl, apex, br}, {br, apex, tr}, {tr, apex, tl}, {tl, apex, bl}, {bl, br, tr}, {bl, tr, tl},
    };
    float positions[18 * 3];
    float normals[18 * 3];
    unsigned int indices[18];
    for (int f = 0; f < 6; ++f)
    {
        const slayer3d_vec3 normal = slayer3d_shape_face_normal(faces[f][0], faces[f][1], faces[f][2]);
        for (int v = 0; v < 3; ++v)
        {
            const int vi = f * 3 + v;
            slayer3d_shape_store_vertex(positions, normals, vi, faces[f][v], normal);
            indices[vi] = (unsigned int)vi;
        }
    }

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = 18;
    mesh.index_count = 18;
    return slayer3d_draw_mesh(context, &mesh, NULL, color);
}

bool slayer3d_draw_pyramid_wires(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                                 slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_pyramid_wires") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_pyramid_wires") ||
        !slayer3d_shape_require_nonnegative(size.z, "size.z", "slayer3d_draw_pyramid_wires"))
    {
        return false;
    }
    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;
    const slayer3d_vec3 p[5] = {
        slayer3d_vec3_make(center.x - hx, center.y - hy, center.z - hz),
        slayer3d_vec3_make(center.x + hx, center.y - hy, center.z - hz),
        slayer3d_vec3_make(center.x + hx, center.y - hy, center.z + hz),
        slayer3d_vec3_make(center.x - hx, center.y - hy, center.z + hz),
        slayer3d_vec3_make(center.x, center.y + hy, center.z),
    };
    static const int edges[8][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 4}, {1, 4}, {2, 4}, {3, 4}};
    for (int i = 0; i < 8; ++i)
    {
        if (!slayer3d_draw_line_3d(context, p[edges[i][0]], p[edges[i][1]], color))
            return false;
    }
    return true;
}

bool slayer3d_draw_wedge(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                         slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_wedge") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_wedge") ||
        !slayer3d_shape_require_nonnegative(size.z, "size.z", "slayer3d_draw_wedge"))
    {
        return false;
    }
    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;
    const slayer3d_vec3 p[6] = {
        slayer3d_vec3_make(center.x - hx, center.y - hy, center.z - hz),
        slayer3d_vec3_make(center.x + hx, center.y - hy, center.z - hz),
        slayer3d_vec3_make(center.x - hx, center.y - hy, center.z + hz),
        slayer3d_vec3_make(center.x + hx, center.y - hy, center.z + hz),
        slayer3d_vec3_make(center.x - hx, center.y + hy, center.z + hz),
        slayer3d_vec3_make(center.x + hx, center.y + hy, center.z + hz),
    };
    const slayer3d_vec3 triangles[8][3] = {
        {p[0], p[3], p[1]}, {p[0], p[2], p[3]}, {p[2], p[5], p[3]}, {p[2], p[4], p[5]},
        {p[0], p[4], p[2]}, {p[0], p[1], p[5]}, {p[0], p[5], p[4]}, {p[1], p[3], p[5]},
    };
    float positions[24 * 3];
    float normals[24 * 3];
    unsigned int indices[24];
    for (int f = 0; f < 8; ++f)
    {
        const slayer3d_vec3 normal = slayer3d_shape_face_normal(triangles[f][0], triangles[f][1], triangles[f][2]);
        for (int v = 0; v < 3; ++v)
        {
            const int vi = f * 3 + v;
            slayer3d_shape_store_vertex(positions, normals, vi, triangles[f][v], normal);
            indices[vi] = (unsigned int)vi;
        }
    }

    slayer3d_mesh mesh;
    SDL_zerop(&mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.indices = indices;
    mesh.vertex_count = 24;
    mesh.index_count = 24;
    return slayer3d_draw_mesh(context, &mesh, NULL, color);
}

bool slayer3d_draw_wedge_wires(slayer3d_render_context *context, slayer3d_vec3 center, slayer3d_vec3 size,
                               slayer3d_color color)
{
    if (!slayer3d_shape_require_nonnegative(size.x, "size.x", "slayer3d_draw_wedge_wires") ||
        !slayer3d_shape_require_nonnegative(size.y, "size.y", "slayer3d_draw_wedge_wires") ||
        !slayer3d_shape_require_nonnegative(size.z, "size.z", "slayer3d_draw_wedge_wires"))
    {
        return false;
    }
    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;
    const slayer3d_vec3 p[6] = {
        slayer3d_vec3_make(center.x - hx, center.y - hy, center.z - hz),
        slayer3d_vec3_make(center.x + hx, center.y - hy, center.z - hz),
        slayer3d_vec3_make(center.x - hx, center.y - hy, center.z + hz),
        slayer3d_vec3_make(center.x + hx, center.y - hy, center.z + hz),
        slayer3d_vec3_make(center.x - hx, center.y + hy, center.z + hz),
        slayer3d_vec3_make(center.x + hx, center.y + hy, center.z + hz),
    };
    static const int edges[9][2] = {{0, 1}, {0, 2}, {1, 3}, {2, 3}, {2, 4}, {3, 5}, {4, 5}, {0, 4}, {1, 5}};
    for (int i = 0; i < 9; ++i)
    {
        if (!slayer3d_draw_line_3d(context, p[edges[i][0]], p[edges[i][1]], color))
            return false;
    }
    return true;
}

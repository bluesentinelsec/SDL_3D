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

    /*
     * Corner indices encoded as bit patterns: bit 0 = +X, bit 1 = +Y,
     * bit 2 = +Z. Every quad below lists the four corners in CCW order
     * when viewed from outside along the face normal. The faces are
     * expanded into two triangles each.
     */
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

    for (int f = 0; f < 6; ++f)
    {
        const sdl3d_vec3 a = c[faces[f][0]];
        const sdl3d_vec3 b = c[faces[f][1]];
        const sdl3d_vec3 d = c[faces[f][2]];
        const sdl3d_vec3 e = c[faces[f][3]];
        if (!sdl3d_draw_triangle_3d(context, a, b, d, color))
        {
            return false;
        }
        if (!sdl3d_draw_triangle_3d(context, a, d, e, color))
        {
            return false;
        }
    }
    return true;
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
    if (!sdl3d_shape_require_nonnegative(size.x, "size.x", "sdl3d_draw_plane") ||
        !sdl3d_shape_require_nonnegative(size.y, "size.y", "sdl3d_draw_plane"))
    {
        return false;
    }

    const float hx = size.x * 0.5f;
    const float hz = size.y * 0.5f;
    const sdl3d_vec3 v0 = sdl3d_vec3_make(center.x - hx, center.y, center.z + hz);
    const sdl3d_vec3 v1 = sdl3d_vec3_make(center.x + hx, center.y, center.z + hz);
    const sdl3d_vec3 v2 = sdl3d_vec3_make(center.x + hx, center.y, center.z - hz);
    const sdl3d_vec3 v3 = sdl3d_vec3_make(center.x - hx, center.y, center.z - hz);

    if (!sdl3d_draw_triangle_3d(context, v0, v1, v2, color))
    {
        return false;
    }
    return sdl3d_draw_triangle_3d(context, v0, v2, v3, color);
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

    for (int i = 0; i < rings; ++i)
    {
        const float theta1 = SDL3D_SHAPES_PI * (float)i / (float)rings;
        const float theta2 = SDL3D_SHAPES_PI * (float)(i + 1) / (float)rings;
        for (int j = 0; j < slices; ++j)
        {
            const float phi1 = 2.0f * SDL3D_SHAPES_PI * (float)j / (float)slices;
            const float phi2 = 2.0f * SDL3D_SHAPES_PI * (float)(j + 1) / (float)slices;

            const sdl3d_vec3 v00 = sdl3d_sphere_point(center, radius, theta1, phi1);
            const sdl3d_vec3 v01 = sdl3d_sphere_point(center, radius, theta1, phi2);
            const sdl3d_vec3 v10 = sdl3d_sphere_point(center, radius, theta2, phi1);
            const sdl3d_vec3 v11 = sdl3d_sphere_point(center, radius, theta2, phi2);

            if (!sdl3d_draw_triangle_3d(context, v00, v01, v11, color))
            {
                return false;
            }
            if (!sdl3d_draw_triangle_3d(context, v00, v11, v10, color))
            {
                return false;
            }
        }
    }
    return true;
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

        /* Side: outward-facing, CCW from outside. */
        if (!sdl3d_draw_triangle_3d(context, bot1, top1, top2, color))
        {
            return false;
        }
        if (!sdl3d_draw_triangle_3d(context, bot1, top2, bot2, color))
        {
            return false;
        }

        /* Top cap (+Y outward): CCW from +Y requires order (center, v_{j+1}, v_j). */
        if (radius_top > 0.0f)
        {
            if (!sdl3d_draw_triangle_3d(context, top_center, top2, top1, color))
            {
                return false;
            }
        }
        /* Bottom cap (-Y outward): opposite winding. */
        if (radius_bottom > 0.0f)
        {
            if (!sdl3d_draw_triangle_3d(context, bottom_center, bot1, bot2, color))
            {
                return false;
            }
        }
    }
    return true;
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

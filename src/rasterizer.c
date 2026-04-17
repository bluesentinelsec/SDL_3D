#include "rasterizer.h"

#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_stdinc.h>

/*
 * Subpixel precision: 8 bits (256 subpixel positions per pixel).
 * Sample point is pixel center at (x + 0.5, y + 0.5).
 */
static const int SDL3D_SUBPIXEL_BITS = 8;
static const int SDL3D_SUBPIXEL_UNIT = 1 << 8;       /* 256 */
static const int SDL3D_SUBPIXEL_HALF = 1 << (8 - 1); /* 128 */

/*
 * Clipping scratch capacity. A triangle clipped against 6 planes can
 * produce at most 3 + 6 = 9 vertices. Round up to 10 for safety.
 */
#define SDL3D_CLIP_MAX_VERTICES 10

/* --- Helpers -------------------------------------------------------------- */

static int sdl3d_round_subpixel(float value)
{
    /* SDL_lroundf rounds half-away-from-zero which matches our requirements. */
    return (int)SDL_lroundf(value * (float)SDL3D_SUBPIXEL_UNIT);
}

static int sdl3d_max_int(int a, int b)
{
    return (a > b) ? a : b;
}

static int sdl3d_min_int(int a, int b)
{
    return (a < b) ? a : b;
}

static void sdl3d_write_pixel(sdl3d_framebuffer *framebuffer, int x, int y, float depth, sdl3d_color color)
{
    const int index = (y * framebuffer->width) + x;

    if (depth > framebuffer->depth_pixels[index])
    {
        return;
    }

    framebuffer->depth_pixels[index] = depth;
    Uint8 *pixel = &framebuffer->color_pixels[index * 4];
    pixel[0] = color.r;
    pixel[1] = color.g;
    pixel[2] = color.b;
    pixel[3] = color.a;
}

/* --- Clip-space plane tests ---------------------------------------------- */

typedef enum sdl3d_clip_plane
{
    SDL3D_CLIP_LEFT = 0,
    SDL3D_CLIP_RIGHT = 1,
    SDL3D_CLIP_BOTTOM = 2,
    SDL3D_CLIP_TOP = 3,
    SDL3D_CLIP_NEAR = 4,
    SDL3D_CLIP_FAR = 5
} sdl3d_clip_plane;

/*
 * Signed distance from a clip-space vertex to the given plane. Positive
 * means "inside" (the half-space retained by clipping). The six planes
 * enforce: -w <= x <= w, -w <= y <= w, -w <= z <= w.
 */
static float sdl3d_clip_distance(sdl3d_vec4 v, sdl3d_clip_plane plane)
{
    switch (plane)
    {
    case SDL3D_CLIP_LEFT:
        return v.x + v.w;
    case SDL3D_CLIP_RIGHT:
        return v.w - v.x;
    case SDL3D_CLIP_BOTTOM:
        return v.y + v.w;
    case SDL3D_CLIP_TOP:
        return v.w - v.y;
    case SDL3D_CLIP_NEAR:
        return v.z + v.w;
    case SDL3D_CLIP_FAR:
        return v.w - v.z;
    default:
        SDL_assert(0 && "unreachable: unknown clip plane");
        return 0.0f;
    }
}

/*
 * Sutherland-Hodgman polygon clip against a single plane. Reads `count_in`
 * vertices from `in`, writes the clipped polygon to `out`, returns the new
 * vertex count.
 */
static int sdl3d_clip_polygon_against_plane(const sdl3d_vec4 *in, int count_in, sdl3d_vec4 *out, sdl3d_clip_plane plane)
{
    if (count_in < 3)
    {
        return 0;
    }

    int count_out = 0;
    sdl3d_vec4 prev = in[count_in - 1];
    float prev_distance = sdl3d_clip_distance(prev, plane);

    for (int i = 0; i < count_in; ++i)
    {
        const sdl3d_vec4 curr = in[i];
        const float curr_distance = sdl3d_clip_distance(curr, plane);

        const bool prev_inside = prev_distance >= 0.0f;
        const bool curr_inside = curr_distance >= 0.0f;

        if (prev_inside != curr_inside)
        {
            /*
             * The edge crosses the plane. Linear interpolation in clip
             * space on the homogeneous coordinates is geometrically
             * correct because the plane test is itself linear in those
             * coordinates.
             */
            const float t = prev_distance / (prev_distance - curr_distance);
            if (count_out < SDL3D_CLIP_MAX_VERTICES)
            {
                out[count_out++] = sdl3d_vec4_lerp(prev, curr, t);
            }
        }

        if (curr_inside)
        {
            if (count_out < SDL3D_CLIP_MAX_VERTICES)
            {
                out[count_out++] = curr;
            }
        }

        prev = curr;
        prev_distance = curr_distance;
    }

    return count_out;
}

/*
 * Clip a triangle against all six frustum planes. Writes the resulting
 * polygon into `out` (max SDL3D_CLIP_MAX_VERTICES vertices) and returns
 * the vertex count (0, 3, 4, ...).
 */
static int sdl3d_clip_triangle(sdl3d_vec4 v0, sdl3d_vec4 v1, sdl3d_vec4 v2, sdl3d_vec4 *out)
{
    sdl3d_vec4 buffer_a[SDL3D_CLIP_MAX_VERTICES];
    sdl3d_vec4 buffer_b[SDL3D_CLIP_MAX_VERTICES];

    buffer_a[0] = v0;
    buffer_a[1] = v1;
    buffer_a[2] = v2;
    int count = 3;

    sdl3d_vec4 *src = buffer_a;
    sdl3d_vec4 *dst = buffer_b;

    for (int plane = 0; plane < 6; ++plane)
    {
        count = sdl3d_clip_polygon_against_plane(src, count, dst, (sdl3d_clip_plane)plane);
        if (count == 0)
        {
            return 0;
        }
        sdl3d_vec4 *swap = src;
        src = dst;
        dst = swap;
    }

    for (int i = 0; i < count; ++i)
    {
        out[i] = src[i];
    }
    return count;
}

/*
 * Clip a line segment against all six planes using a simpler algorithm:
 * each plane produces at most one intersection, so we just trim the segment
 * at each plane. Returns false if the segment is entirely outside.
 */
static bool sdl3d_clip_line(sdl3d_vec4 *a, sdl3d_vec4 *b)
{
    for (int plane = 0; plane < 6; ++plane)
    {
        const float da = sdl3d_clip_distance(*a, (sdl3d_clip_plane)plane);
        const float db = sdl3d_clip_distance(*b, (sdl3d_clip_plane)plane);
        const bool a_inside = da >= 0.0f;
        const bool b_inside = db >= 0.0f;

        if (!a_inside && !b_inside)
        {
            return false;
        }

        if (a_inside != b_inside)
        {
            const float t = da / (da - db);
            const sdl3d_vec4 intersection = sdl3d_vec4_lerp(*a, *b, t);
            if (!a_inside)
            {
                *a = intersection;
            }
            else
            {
                *b = intersection;
            }
        }
    }
    return true;
}

/* --- Viewport transform --------------------------------------------------- */

typedef struct sdl3d_screen_vertex
{
    int x_fx;   /* fixed-point screen x, 8 subpixel bits */
    int y_fx;   /* fixed-point screen y, 8 subpixel bits */
    float z;    /* NDC depth in [-1, 1] */
    float x_px; /* floating-point screen x, for line/point raster */
    float y_px;
} sdl3d_screen_vertex;

static sdl3d_screen_vertex sdl3d_viewport_transform(sdl3d_vec4 clip, int width, int height)
{
    sdl3d_screen_vertex out;
    const float inverse_w = 1.0f / clip.w;
    const float ndc_x = clip.x * inverse_w;
    const float ndc_y = clip.y * inverse_w;
    const float ndc_z = clip.z * inverse_w;

    /* y axis flips so that NDC +Y becomes "up" on screen (pixel rows grow down). */
    const float screen_x = (ndc_x + 1.0f) * 0.5f * (float)width;
    const float screen_y = (1.0f - ndc_y) * 0.5f * (float)height;

    out.x_fx = sdl3d_round_subpixel(screen_x);
    out.y_fx = sdl3d_round_subpixel(screen_y);
    out.x_px = screen_x;
    out.y_px = screen_y;
    out.z = ndc_z;
    return out;
}

/* --- Edge function + fill rule ------------------------------------------- */

/*
 * 2D cross product in fixed-point. For subpixel precision SDL3D_SUBPIXEL_BITS
 * with coordinates up to ~2^20, products fit in int64 with headroom.
 */
static Sint64 sdl3d_edge_function(int ax, int ay, int bx, int by, int px, int py)
{
    return (Sint64)(bx - ax) * (Sint64)(py - ay) - (Sint64)(by - ay) * (Sint64)(px - ax);
}

/*
 * D3D-style top-left fill rule. Assumes winding has been normalized so that
 * the triangle has positive 2x-area. Returns the bias to add to the edge
 * function at each pixel: 0 for top-left edges (inclusive boundary), -1 for
 * others (exclusive boundary). Top edge: dy == 0 AND dx > 0. Left edge:
 * dy < 0 (in y-down screen space with positive-area winding).
 */
static int sdl3d_fill_bias(int ax, int ay, int bx, int by)
{
    const int dx = bx - ax;
    const int dy = by - ay;
    const bool is_top = (dy == 0) && (dx > 0);
    const bool is_left = (dy < 0);
    return (is_top || is_left) ? 0 : -1;
}

/* --- Triangle rasterization --------------------------------------------- */

static void sdl3d_rasterize_screen_triangle(sdl3d_framebuffer *framebuffer, sdl3d_screen_vertex a,
                                            sdl3d_screen_vertex b, sdl3d_screen_vertex c, sdl3d_color color)
{
    /* 2x signed area in fixed-point. If negative, swap winding. Zero means degenerate. */
    Sint64 area = sdl3d_edge_function(a.x_fx, a.y_fx, b.x_fx, b.y_fx, c.x_fx, c.y_fx);
    if (area == 0)
    {
        return;
    }
    if (area < 0)
    {
        sdl3d_screen_vertex tmp = b;
        b = c;
        c = tmp;
        area = -area;
    }

    /* Pixel-aligned bounding box, clipped to the framebuffer. */
    const int min_fx = sdl3d_min_int(a.x_fx, sdl3d_min_int(b.x_fx, c.x_fx));
    const int max_fx = sdl3d_max_int(a.x_fx, sdl3d_max_int(b.x_fx, c.x_fx));
    const int min_fy = sdl3d_min_int(a.y_fx, sdl3d_min_int(b.y_fx, c.y_fx));
    const int max_fy = sdl3d_max_int(a.y_fx, sdl3d_max_int(b.y_fx, c.y_fx));

    const int min_px_x = sdl3d_max_int(0, min_fx >> SDL3D_SUBPIXEL_BITS);
    const int max_px_x = sdl3d_min_int(framebuffer->width - 1, (max_fx - 1) >> SDL3D_SUBPIXEL_BITS);
    const int min_px_y = sdl3d_max_int(0, min_fy >> SDL3D_SUBPIXEL_BITS);
    const int max_px_y = sdl3d_min_int(framebuffer->height - 1, (max_fy - 1) >> SDL3D_SUBPIXEL_BITS);

    if (min_px_x > max_px_x || min_px_y > max_px_y)
    {
        return;
    }

    const int bias_ab = sdl3d_fill_bias(a.x_fx, a.y_fx, b.x_fx, b.y_fx);
    const int bias_bc = sdl3d_fill_bias(b.x_fx, b.y_fx, c.x_fx, c.y_fx);
    const int bias_ca = sdl3d_fill_bias(c.x_fx, c.y_fx, a.x_fx, a.y_fx);

    const float inverse_area = 1.0f / (float)area;

    for (int py = min_px_y; py <= max_px_y; ++py)
    {
        const int sample_y = (py << SDL3D_SUBPIXEL_BITS) + SDL3D_SUBPIXEL_HALF;
        for (int px = min_px_x; px <= max_px_x; ++px)
        {
            const int sample_x = (px << SDL3D_SUBPIXEL_BITS) + SDL3D_SUBPIXEL_HALF;

            const Sint64 w_ab = sdl3d_edge_function(a.x_fx, a.y_fx, b.x_fx, b.y_fx, sample_x, sample_y) + bias_ab;
            const Sint64 w_bc = sdl3d_edge_function(b.x_fx, b.y_fx, c.x_fx, c.y_fx, sample_x, sample_y) + bias_bc;
            const Sint64 w_ca = sdl3d_edge_function(c.x_fx, c.y_fx, a.x_fx, a.y_fx, sample_x, sample_y) + bias_ca;

            if ((w_ab | w_bc | w_ca) < 0)
            {
                continue;
            }

            /*
             * Barycentric weights. w_bc weights vertex a (opposite edge),
             * w_ca weights b, w_ab weights c. NDC z interpolates linearly
             * in screen space after perspective divide, so barycentric
             * interpolation on z is correct.
             */
            const float bary_a = (float)w_bc * inverse_area;
            const float bary_b = (float)w_ca * inverse_area;
            const float bary_c = (float)w_ab * inverse_area;
            const float depth = (bary_a * a.z) + (bary_b * b.z) + (bary_c * c.z);

            sdl3d_write_pixel(framebuffer, px, py, depth, color);
        }
    }
}

/* --- Public: triangle, line, point --------------------------------------- */

void sdl3d_rasterize_triangle(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 v0, sdl3d_vec3 v1,
                              sdl3d_vec3 v2, sdl3d_color color)
{
    if (framebuffer == NULL || framebuffer->color_pixels == NULL || framebuffer->depth_pixels == NULL)
    {
        return;
    }

    const sdl3d_vec4 clip0 = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v0, 1.0f));
    const sdl3d_vec4 clip1 = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v1, 1.0f));
    const sdl3d_vec4 clip2 = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v2, 1.0f));

    sdl3d_vec4 clipped[SDL3D_CLIP_MAX_VERTICES];
    const int clipped_count = sdl3d_clip_triangle(clip0, clip1, clip2, clipped);
    if (clipped_count < 3)
    {
        return;
    }

    sdl3d_screen_vertex screen[SDL3D_CLIP_MAX_VERTICES];
    for (int i = 0; i < clipped_count; ++i)
    {
        screen[i] = sdl3d_viewport_transform(clipped[i], framebuffer->width, framebuffer->height);
    }

    /* Fan-triangulate the clipped polygon around vertex 0. */
    for (int i = 1; i + 1 < clipped_count; ++i)
    {
        sdl3d_rasterize_screen_triangle(framebuffer, screen[0], screen[i], screen[i + 1], color);
    }
}

/* --- Line rasterization -------------------------------------------------- */

static void sdl3d_rasterize_screen_line(sdl3d_framebuffer *framebuffer, sdl3d_screen_vertex start,
                                        sdl3d_screen_vertex end, sdl3d_color color)
{
    const float dx = end.x_px - start.x_px;
    const float dy = end.y_px - start.y_px;
    const float abs_dx = SDL_fabsf(dx);
    const float abs_dy = SDL_fabsf(dy);
    const float length = (abs_dx > abs_dy) ? abs_dx : abs_dy;

    if (length <= 0.0f)
    {
        /* Degenerate: both endpoints snap to the same pixel. */
        const int px = (int)SDL_lroundf(start.x_px);
        const int py = (int)SDL_lroundf(start.y_px);
        if (px >= 0 && px < framebuffer->width && py >= 0 && py < framebuffer->height)
        {
            sdl3d_write_pixel(framebuffer, px, py, start.z, color);
        }
        return;
    }

    /*
     * DDA with per-pixel depth interpolation. Step count is ceil(length)+1
     * so we stamp the end pixel exactly once.
     */
    const int steps = (int)length + 1;
    const float inverse_steps = 1.0f / (float)steps;
    const float step_x = dx * inverse_steps;
    const float step_y = dy * inverse_steps;
    const float step_z = (end.z - start.z) * inverse_steps;

    float x = start.x_px;
    float y = start.y_px;
    float z = start.z;

    for (int i = 0; i <= steps; ++i)
    {
        const int px = (int)SDL_lroundf(x);
        const int py = (int)SDL_lroundf(y);
        if (px >= 0 && px < framebuffer->width && py >= 0 && py < framebuffer->height)
        {
            sdl3d_write_pixel(framebuffer, px, py, z, color);
        }
        x += step_x;
        y += step_y;
        z += step_z;
    }
}

void sdl3d_rasterize_line(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 start, sdl3d_vec3 end,
                          sdl3d_color color)
{
    if (framebuffer == NULL || framebuffer->color_pixels == NULL || framebuffer->depth_pixels == NULL)
    {
        return;
    }

    sdl3d_vec4 clip_start = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(start, 1.0f));
    sdl3d_vec4 clip_end = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(end, 1.0f));

    if (!sdl3d_clip_line(&clip_start, &clip_end))
    {
        return;
    }

    const sdl3d_screen_vertex screen_start =
        sdl3d_viewport_transform(clip_start, framebuffer->width, framebuffer->height);
    const sdl3d_screen_vertex screen_end = sdl3d_viewport_transform(clip_end, framebuffer->width, framebuffer->height);
    sdl3d_rasterize_screen_line(framebuffer, screen_start, screen_end, color);
}

/* --- Point rasterization ------------------------------------------------- */

void sdl3d_rasterize_point(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 position, sdl3d_color color)
{
    if (framebuffer == NULL || framebuffer->color_pixels == NULL || framebuffer->depth_pixels == NULL)
    {
        return;
    }

    const sdl3d_vec4 clip = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(position, 1.0f));

    /* Reject if outside any plane. */
    for (int plane = 0; plane < 6; ++plane)
    {
        if (sdl3d_clip_distance(clip, (sdl3d_clip_plane)plane) < 0.0f)
        {
            return;
        }
    }

    if (clip.w <= 0.0f)
    {
        return;
    }

    const sdl3d_screen_vertex sv = sdl3d_viewport_transform(clip, framebuffer->width, framebuffer->height);
    const int px = (int)SDL_lroundf(sv.x_px);
    const int py = (int)SDL_lroundf(sv.y_px);
    if (px < 0 || px >= framebuffer->width || py < 0 || py >= framebuffer->height)
    {
        return;
    }
    sdl3d_write_pixel(framebuffer, px, py, sv.z, color);
}

/* --- Framebuffer utilities ---------------------------------------------- */

void sdl3d_framebuffer_clear(sdl3d_framebuffer *framebuffer, sdl3d_color color, float depth)
{
    if (framebuffer == NULL || framebuffer->color_pixels == NULL || framebuffer->depth_pixels == NULL)
    {
        return;
    }

    const size_t pixel_count = (size_t)framebuffer->width * (size_t)framebuffer->height;
    for (size_t i = 0; i < pixel_count; ++i)
    {
        Uint8 *pixel = &framebuffer->color_pixels[i * 4];
        pixel[0] = color.r;
        pixel[1] = color.g;
        pixel[2] = color.b;
        pixel[3] = color.a;
        framebuffer->depth_pixels[i] = depth;
    }
}

bool sdl3d_framebuffer_get_pixel(const sdl3d_framebuffer *framebuffer, int x, int y, sdl3d_color *out_color)
{
    if (framebuffer == NULL || out_color == NULL)
    {
        return false;
    }
    if (x < 0 || x >= framebuffer->width || y < 0 || y >= framebuffer->height)
    {
        return false;
    }

    const int index = (y * framebuffer->width) + x;
    const Uint8 *pixel = &framebuffer->color_pixels[index * 4];
    out_color->r = pixel[0];
    out_color->g = pixel[1];
    out_color->b = pixel[2];
    out_color->a = pixel[3];
    return true;
}

bool sdl3d_framebuffer_get_depth(const sdl3d_framebuffer *framebuffer, int x, int y, float *out_depth)
{
    if (framebuffer == NULL || out_depth == NULL)
    {
        return false;
    }
    if (x < 0 || x >= framebuffer->width || y < 0 || y >= framebuffer->height)
    {
        return false;
    }
    *out_depth = framebuffer->depth_pixels[(y * framebuffer->width) + x];
    return true;
}

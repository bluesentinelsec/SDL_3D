#include "rasterizer.h"

#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_thread.h>

#include "texture_internal.h"

#include "lighting_internal.h"

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

/*
 * Fixed tile width/height for the parallel solid-triangle path. Tiles own
 * disjoint pixel rectangles, so workers never contend on a framebuffer write
 * and the output remains byte-identical to the single-threaded reference.
 */
static const int SDL3D_RASTER_TILE_SIZE = 32;

typedef void (*sdl3d_parallel_tile_function)(void *userdata, int tile_index);

typedef struct sdl3d_parallel_job
{
    sdl3d_parallel_tile_function run_tile;
    void *userdata;
    int tile_count;
    SDL_AtomicInt next_tile_index;
} sdl3d_parallel_job;

struct sdl3d_parallel_rasterizer
{
    int worker_count;
    SDL_Thread **threads;
    SDL_Semaphore *work_ready;
    SDL_Semaphore *work_complete;
    SDL_AtomicInt stop_requested;
    void *current_job;
};

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

static void sdl3d_parallel_run_job(sdl3d_parallel_job *job)
{
    if (job == NULL || job->run_tile == NULL)
    {
        return;
    }

    while (true)
    {
        const int tile_index = SDL_AddAtomicInt(&job->next_tile_index, 1);
        if (tile_index >= job->tile_count)
        {
            return;
        }

        job->run_tile(job->userdata, tile_index);
    }
}

static int sdl3d_parallel_rasterizer_thread_main(void *userdata)
{
    sdl3d_parallel_rasterizer *rasterizer = (sdl3d_parallel_rasterizer *)userdata;

    for (;;)
    {
        SDL_WaitSemaphore(rasterizer->work_ready);

        if (SDL_GetAtomicInt(&rasterizer->stop_requested) != 0)
        {
            return 0;
        }

        sdl3d_parallel_job *job = (sdl3d_parallel_job *)SDL_GetAtomicPointer(&rasterizer->current_job);
        sdl3d_parallel_run_job(job);
        SDL_SignalSemaphore(rasterizer->work_complete);
    }
}

static void sdl3d_parallel_rasterizer_execute(sdl3d_parallel_rasterizer *rasterizer, sdl3d_parallel_job *job)
{
    if (rasterizer == NULL || rasterizer->worker_count <= 0 || job == NULL || job->tile_count <= 0)
    {
        sdl3d_parallel_run_job(job);
        return;
    }

    SDL_SetAtomicInt(&job->next_tile_index, 0);
    SDL_SetAtomicPointer(&rasterizer->current_job, job);

    for (int i = 0; i < rasterizer->worker_count; ++i)
    {
        SDL_SignalSemaphore(rasterizer->work_ready);
    }

    sdl3d_parallel_run_job(job);

    for (int i = 0; i < rasterizer->worker_count; ++i)
    {
        SDL_WaitSemaphore(rasterizer->work_complete);
    }

    SDL_SetAtomicPointer(&rasterizer->current_job, NULL);
}

static bool sdl3d_scissor_contains(const sdl3d_framebuffer *framebuffer, int x, int y)
{
    if (!framebuffer->scissor_enabled)
    {
        return true;
    }

    return (x >= framebuffer->scissor_rect.x) && (y >= framebuffer->scissor_rect.y) &&
           (x < framebuffer->scissor_rect.x + framebuffer->scissor_rect.w) &&
           (y < framebuffer->scissor_rect.y + framebuffer->scissor_rect.h);
}

static void sdl3d_write_pixel(sdl3d_framebuffer *framebuffer, int x, int y, float depth, sdl3d_color color)
{
    const int index = (y * framebuffer->width) + x;

    if (!sdl3d_scissor_contains(framebuffer, x, y))
    {
        return;
    }

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

typedef struct sdl3d_triangle_bounds
{
    int min_px_x;
    int max_px_x;
    int min_px_y;
    int max_px_y;
} sdl3d_triangle_bounds;

typedef struct sdl3d_prepared_triangle
{
    sdl3d_screen_vertex a;
    sdl3d_screen_vertex b;
    sdl3d_screen_vertex c;
    Sint64 area;
    int bias_ab;
    int bias_bc;
    int bias_ca;
    float inverse_area;
    sdl3d_triangle_bounds bounds;
} sdl3d_prepared_triangle;

typedef struct sdl3d_parallel_triangle_job
{
    sdl3d_framebuffer *framebuffer;
    sdl3d_prepared_triangle triangle;
    sdl3d_color color;
    int first_tile_x;
    int first_tile_y;
    int tiles_w;
} sdl3d_parallel_triangle_job;

static void sdl3d_rasterize_screen_line(sdl3d_framebuffer *framebuffer, sdl3d_screen_vertex start,
                                        sdl3d_screen_vertex end, sdl3d_color color);

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

static Sint64 sdl3d_screen_triangle_signed_area(sdl3d_screen_vertex a, sdl3d_screen_vertex b, sdl3d_screen_vertex c)
{
    return sdl3d_edge_function(a.x_fx, a.y_fx, b.x_fx, b.y_fx, c.x_fx, c.y_fx);
}

static Sint64 sdl3d_screen_polygon_signed_area(const sdl3d_screen_vertex *vertices, int count)
{
    Sint64 area_twice = 0;

    for (int i = 0; i < count; ++i)
    {
        const sdl3d_screen_vertex current = vertices[i];
        const sdl3d_screen_vertex next = vertices[(i + 1) % count];
        area_twice += ((Sint64)current.x_fx * (Sint64)next.y_fx) - ((Sint64)next.x_fx * (Sint64)current.y_fx);
    }

    return area_twice;
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

static bool sdl3d_prepare_screen_triangle(const sdl3d_framebuffer *framebuffer, sdl3d_screen_vertex a,
                                          sdl3d_screen_vertex b, sdl3d_screen_vertex c,
                                          sdl3d_prepared_triangle *out_triangle)
{
    /* 2x signed area in fixed-point. If negative, swap winding. Zero means degenerate. */
    Sint64 area = sdl3d_screen_triangle_signed_area(a, b, c);
    if (area == 0)
    {
        return false;
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
        return false;
    }

    out_triangle->a = a;
    out_triangle->b = b;
    out_triangle->c = c;
    out_triangle->area = area;
    out_triangle->bias_ab = sdl3d_fill_bias(a.x_fx, a.y_fx, b.x_fx, b.y_fx);
    out_triangle->bias_bc = sdl3d_fill_bias(b.x_fx, b.y_fx, c.x_fx, c.y_fx);
    out_triangle->bias_ca = sdl3d_fill_bias(c.x_fx, c.y_fx, a.x_fx, a.y_fx);
    out_triangle->inverse_area = 1.0f / (float)area;
    out_triangle->bounds.min_px_x = min_px_x;
    out_triangle->bounds.max_px_x = max_px_x;
    out_triangle->bounds.min_px_y = min_px_y;
    out_triangle->bounds.max_px_y = max_px_y;
    return true;
}

static void sdl3d_rasterize_prepared_triangle_region(sdl3d_framebuffer *framebuffer,
                                                     const sdl3d_prepared_triangle *triangle, sdl3d_color color,
                                                     int min_px_x, int max_px_x, int min_px_y, int max_px_y)
{
    const sdl3d_screen_vertex a = triangle->a;
    const sdl3d_screen_vertex b = triangle->b;
    const sdl3d_screen_vertex c = triangle->c;

    for (int py = min_px_y; py <= max_px_y; ++py)
    {
        const int sample_y = (py << SDL3D_SUBPIXEL_BITS) + SDL3D_SUBPIXEL_HALF;
        for (int px = min_px_x; px <= max_px_x; ++px)
        {
            const int sample_x = (px << SDL3D_SUBPIXEL_BITS) + SDL3D_SUBPIXEL_HALF;

            const Sint64 w_ab =
                sdl3d_edge_function(a.x_fx, a.y_fx, b.x_fx, b.y_fx, sample_x, sample_y) + triangle->bias_ab;
            const Sint64 w_bc =
                sdl3d_edge_function(b.x_fx, b.y_fx, c.x_fx, c.y_fx, sample_x, sample_y) + triangle->bias_bc;
            const Sint64 w_ca =
                sdl3d_edge_function(c.x_fx, c.y_fx, a.x_fx, a.y_fx, sample_x, sample_y) + triangle->bias_ca;

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
            const float bary_a = (float)w_bc * triangle->inverse_area;
            const float bary_b = (float)w_ca * triangle->inverse_area;
            const float bary_c = (float)w_ab * triangle->inverse_area;
            const float depth = (bary_a * a.z) + (bary_b * b.z) + (bary_c * c.z);

            sdl3d_write_pixel(framebuffer, px, py, depth, color);
        }
    }
}

static void sdl3d_parallel_triangle_job_run_tile(void *userdata, int tile_index)
{
    sdl3d_parallel_triangle_job *job = (sdl3d_parallel_triangle_job *)userdata;
    const int tile_x = job->first_tile_x + (tile_index % job->tiles_w);
    const int tile_y = job->first_tile_y + (tile_index / job->tiles_w);

    const int tile_min_px_x = sdl3d_max_int(job->triangle.bounds.min_px_x, tile_x * SDL3D_RASTER_TILE_SIZE);
    const int tile_max_px_x = sdl3d_min_int(job->triangle.bounds.max_px_x, ((tile_x + 1) * SDL3D_RASTER_TILE_SIZE) - 1);
    const int tile_min_px_y = sdl3d_max_int(job->triangle.bounds.min_px_y, tile_y * SDL3D_RASTER_TILE_SIZE);
    const int tile_max_px_y = sdl3d_min_int(job->triangle.bounds.max_px_y, ((tile_y + 1) * SDL3D_RASTER_TILE_SIZE) - 1);

    if (tile_min_px_x > tile_max_px_x || tile_min_px_y > tile_max_px_y)
    {
        return;
    }

    sdl3d_rasterize_prepared_triangle_region(job->framebuffer, &job->triangle, job->color, tile_min_px_x, tile_max_px_x,
                                             tile_min_px_y, tile_max_px_y);
}

static bool sdl3d_try_rasterize_prepared_triangle_parallel(sdl3d_framebuffer *framebuffer,
                                                           const sdl3d_prepared_triangle *triangle, sdl3d_color color)
{
    if (framebuffer->parallel_rasterizer == NULL)
    {
        return false;
    }

    const int first_tile_x = triangle->bounds.min_px_x / SDL3D_RASTER_TILE_SIZE;
    const int last_tile_x = triangle->bounds.max_px_x / SDL3D_RASTER_TILE_SIZE;
    const int first_tile_y = triangle->bounds.min_px_y / SDL3D_RASTER_TILE_SIZE;
    const int last_tile_y = triangle->bounds.max_px_y / SDL3D_RASTER_TILE_SIZE;
    const int tiles_w = last_tile_x - first_tile_x + 1;
    const int tiles_h = last_tile_y - first_tile_y + 1;
    const int tile_count = tiles_w * tiles_h;

    if (tile_count <= 1)
    {
        return false;
    }

    sdl3d_parallel_triangle_job triangle_job;
    triangle_job.framebuffer = framebuffer;
    triangle_job.triangle = *triangle;
    triangle_job.color = color;
    triangle_job.first_tile_x = first_tile_x;
    triangle_job.first_tile_y = first_tile_y;
    triangle_job.tiles_w = tiles_w;

    sdl3d_parallel_job job;
    job.run_tile = sdl3d_parallel_triangle_job_run_tile;
    job.userdata = &triangle_job;
    job.tile_count = tile_count;
    SDL_SetAtomicInt(&job.next_tile_index, 0);

    sdl3d_parallel_rasterizer_execute(framebuffer->parallel_rasterizer, &job);
    return true;
}

static void sdl3d_rasterize_screen_triangle(sdl3d_framebuffer *framebuffer, sdl3d_screen_vertex a,
                                            sdl3d_screen_vertex b, sdl3d_screen_vertex c, sdl3d_color color)
{
    sdl3d_prepared_triangle triangle;
    if (!sdl3d_prepare_screen_triangle(framebuffer, a, b, c, &triangle))
    {
        return;
    }

    if (sdl3d_try_rasterize_prepared_triangle_parallel(framebuffer, &triangle, color))
    {
        return;
    }

    sdl3d_rasterize_prepared_triangle_region(framebuffer, &triangle, color, triangle.bounds.min_px_x,
                                             triangle.bounds.max_px_x, triangle.bounds.min_px_y,
                                             triangle.bounds.max_px_y);
}

/* --- Public: triangle, line, point --------------------------------------- */

void sdl3d_rasterize_triangle(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 v0, sdl3d_vec3 v1,
                              sdl3d_vec3 v2, sdl3d_color color, bool backface_culling_enabled, bool wireframe_enabled)
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

    const Sint64 polygon_area = sdl3d_screen_polygon_signed_area(screen, clipped_count);
    if (polygon_area == 0)
    {
        return;
    }

    if (backface_culling_enabled && polygon_area >= 0)
    {
        return;
    }

    if (wireframe_enabled)
    {
        for (int i = 0; i < clipped_count; ++i)
        {
            sdl3d_rasterize_screen_line(framebuffer, screen[i], screen[(i + 1) % clipped_count], color);
        }
        return;
    }

    /* Fan-triangulate the clipped polygon around vertex 0. */
    for (int i = 1; i + 1 < clipped_count; ++i)
    {
        sdl3d_rasterize_screen_triangle(framebuffer, screen[0], screen[i], screen[i + 1], color);
    }
}

/* --- Colored triangle rasterization -------------------------------------- */

/*
 * Clip-space vertex carrying per-vertex color as a float RGBA attribute.
 * Colors are in [0, 255] (matching sdl3d_color) so no separate scale is
 * needed when converting back to the framebuffer. During Sutherland-Hodgman
 * clipping the color is interpolated linearly in the clip-space edge
 * parameter `t`, which is the same parameter used for positions.
 */
typedef struct sdl3d_clip_vertex_colored
{
    sdl3d_vec4 position;
    float r;
    float g;
    float b;
    float a;
} sdl3d_clip_vertex_colored;

static sdl3d_clip_vertex_colored sdl3d_clip_vertex_colored_lerp(sdl3d_clip_vertex_colored a,
                                                                sdl3d_clip_vertex_colored b, float t)
{
    sdl3d_clip_vertex_colored out;
    out.position = sdl3d_vec4_lerp(a.position, b.position, t);
    out.r = a.r + (b.r - a.r) * t;
    out.g = a.g + (b.g - a.g) * t;
    out.b = a.b + (b.b - a.b) * t;
    out.a = a.a + (b.a - a.a) * t;
    return out;
}

static int sdl3d_clip_polygon_against_plane_colored(const sdl3d_clip_vertex_colored *in, int count_in,
                                                    sdl3d_clip_vertex_colored *out, sdl3d_clip_plane plane)
{
    if (count_in < 3)
    {
        return 0;
    }

    int count_out = 0;
    sdl3d_clip_vertex_colored prev = in[count_in - 1];
    float prev_distance = sdl3d_clip_distance(prev.position, plane);

    for (int i = 0; i < count_in; ++i)
    {
        const sdl3d_clip_vertex_colored curr = in[i];
        const float curr_distance = sdl3d_clip_distance(curr.position, plane);

        const bool prev_inside = prev_distance >= 0.0f;
        const bool curr_inside = curr_distance >= 0.0f;

        if (prev_inside != curr_inside)
        {
            const float t = prev_distance / (prev_distance - curr_distance);
            if (count_out < SDL3D_CLIP_MAX_VERTICES)
            {
                out[count_out++] = sdl3d_clip_vertex_colored_lerp(prev, curr, t);
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

static int sdl3d_clip_triangle_colored(sdl3d_clip_vertex_colored v0, sdl3d_clip_vertex_colored v1,
                                       sdl3d_clip_vertex_colored v2, sdl3d_clip_vertex_colored *out)
{
    sdl3d_clip_vertex_colored buffer_a[SDL3D_CLIP_MAX_VERTICES];
    sdl3d_clip_vertex_colored buffer_b[SDL3D_CLIP_MAX_VERTICES];

    buffer_a[0] = v0;
    buffer_a[1] = v1;
    buffer_a[2] = v2;
    int count = 3;

    sdl3d_clip_vertex_colored *src = buffer_a;
    sdl3d_clip_vertex_colored *dst = buffer_b;

    for (int plane = 0; plane < 6; ++plane)
    {
        count = sdl3d_clip_polygon_against_plane_colored(src, count, dst, (sdl3d_clip_plane)plane);
        if (count == 0)
        {
            return 0;
        }
        sdl3d_clip_vertex_colored *swap = src;
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
 * Screen vertex augmented for perspective-correct attribute interpolation.
 * `inverse_w` is 1/w at the vertex; `r_over_w` etc. are the per-vertex color
 * pre-divided by w. Linear barycentric interpolation of these quantities in
 * screen space, followed by division by the interpolated 1/w, yields the
 * perspective-correct attribute at each pixel.
 */
typedef struct sdl3d_screen_vertex_colored
{
    sdl3d_screen_vertex base;
    float inverse_w;
    float r_over_w;
    float g_over_w;
    float b_over_w;
    float a_over_w;
} sdl3d_screen_vertex_colored;

static sdl3d_screen_vertex_colored sdl3d_viewport_transform_colored(sdl3d_clip_vertex_colored v, int width, int height)
{
    sdl3d_screen_vertex_colored out;
    out.base = sdl3d_viewport_transform(v.position, width, height);
    const float inv_w = 1.0f / v.position.w;
    out.inverse_w = inv_w;
    out.r_over_w = v.r * inv_w;
    out.g_over_w = v.g * inv_w;
    out.b_over_w = v.b * inv_w;
    out.a_over_w = v.a * inv_w;
    return out;
}

typedef struct sdl3d_prepared_triangle_colored
{
    sdl3d_screen_vertex_colored a;
    sdl3d_screen_vertex_colored b;
    sdl3d_screen_vertex_colored c;
    Sint64 area;
    int bias_ab;
    int bias_bc;
    int bias_ca;
    float inverse_area;
    sdl3d_triangle_bounds bounds;
} sdl3d_prepared_triangle_colored;

typedef struct sdl3d_parallel_triangle_colored_job
{
    sdl3d_framebuffer *framebuffer;
    sdl3d_prepared_triangle_colored triangle;
    int first_tile_x;
    int first_tile_y;
    int tiles_w;
} sdl3d_parallel_triangle_colored_job;

static bool sdl3d_prepare_screen_triangle_colored(const sdl3d_framebuffer *framebuffer, sdl3d_screen_vertex_colored a,
                                                  sdl3d_screen_vertex_colored b, sdl3d_screen_vertex_colored c,
                                                  sdl3d_prepared_triangle_colored *out_triangle)
{
    Sint64 area = sdl3d_screen_triangle_signed_area(a.base, b.base, c.base);
    if (area == 0)
    {
        return false;
    }
    if (area < 0)
    {
        sdl3d_screen_vertex_colored tmp = b;
        b = c;
        c = tmp;
        area = -area;
    }

    const int min_fx = sdl3d_min_int(a.base.x_fx, sdl3d_min_int(b.base.x_fx, c.base.x_fx));
    const int max_fx = sdl3d_max_int(a.base.x_fx, sdl3d_max_int(b.base.x_fx, c.base.x_fx));
    const int min_fy = sdl3d_min_int(a.base.y_fx, sdl3d_min_int(b.base.y_fx, c.base.y_fx));
    const int max_fy = sdl3d_max_int(a.base.y_fx, sdl3d_max_int(b.base.y_fx, c.base.y_fx));

    const int min_px_x = sdl3d_max_int(0, min_fx >> SDL3D_SUBPIXEL_BITS);
    const int max_px_x = sdl3d_min_int(framebuffer->width - 1, (max_fx - 1) >> SDL3D_SUBPIXEL_BITS);
    const int min_px_y = sdl3d_max_int(0, min_fy >> SDL3D_SUBPIXEL_BITS);
    const int max_px_y = sdl3d_min_int(framebuffer->height - 1, (max_fy - 1) >> SDL3D_SUBPIXEL_BITS);

    if (min_px_x > max_px_x || min_px_y > max_px_y)
    {
        return false;
    }

    out_triangle->a = a;
    out_triangle->b = b;
    out_triangle->c = c;
    out_triangle->area = area;
    out_triangle->bias_ab = sdl3d_fill_bias(a.base.x_fx, a.base.y_fx, b.base.x_fx, b.base.y_fx);
    out_triangle->bias_bc = sdl3d_fill_bias(b.base.x_fx, b.base.y_fx, c.base.x_fx, c.base.y_fx);
    out_triangle->bias_ca = sdl3d_fill_bias(c.base.x_fx, c.base.y_fx, a.base.x_fx, a.base.y_fx);
    out_triangle->inverse_area = 1.0f / (float)area;
    out_triangle->bounds.min_px_x = min_px_x;
    out_triangle->bounds.max_px_x = max_px_x;
    out_triangle->bounds.min_px_y = min_px_y;
    out_triangle->bounds.max_px_y = max_px_y;
    return true;
}

static Uint8 sdl3d_color_channel_clamp(float value)
{
    if (value <= 0.0f)
    {
        return 0;
    }
    if (value >= 255.0f)
    {
        return 255;
    }
    return (Uint8)SDL_lroundf(value);
}

static void sdl3d_rasterize_prepared_triangle_colored_region(sdl3d_framebuffer *framebuffer,
                                                             const sdl3d_prepared_triangle_colored *triangle,
                                                             int min_px_x, int max_px_x, int min_px_y, int max_px_y)
{
    const sdl3d_screen_vertex_colored a = triangle->a;
    const sdl3d_screen_vertex_colored b = triangle->b;
    const sdl3d_screen_vertex_colored c = triangle->c;

    for (int py = min_px_y; py <= max_px_y; ++py)
    {
        const int sample_y = (py << SDL3D_SUBPIXEL_BITS) + SDL3D_SUBPIXEL_HALF;
        for (int px = min_px_x; px <= max_px_x; ++px)
        {
            const int sample_x = (px << SDL3D_SUBPIXEL_BITS) + SDL3D_SUBPIXEL_HALF;

            const Sint64 w_ab =
                sdl3d_edge_function(a.base.x_fx, a.base.y_fx, b.base.x_fx, b.base.y_fx, sample_x, sample_y) +
                triangle->bias_ab;
            const Sint64 w_bc =
                sdl3d_edge_function(b.base.x_fx, b.base.y_fx, c.base.x_fx, c.base.y_fx, sample_x, sample_y) +
                triangle->bias_bc;
            const Sint64 w_ca =
                sdl3d_edge_function(c.base.x_fx, c.base.y_fx, a.base.x_fx, a.base.y_fx, sample_x, sample_y) +
                triangle->bias_ca;

            if ((w_ab | w_bc | w_ca) < 0)
            {
                continue;
            }

            const float bary_a = (float)w_bc * triangle->inverse_area;
            const float bary_b = (float)w_ca * triangle->inverse_area;
            const float bary_c = (float)w_ab * triangle->inverse_area;
            const float depth = (bary_a * a.base.z) + (bary_b * b.base.z) + (bary_c * c.base.z);

            const float inverse_w_pixel = (bary_a * a.inverse_w) + (bary_b * b.inverse_w) + (bary_c * c.inverse_w);
            /*
             * Post-clip w is strictly positive (both near and far planes are
             * one-sided so v.w >= max(|v.z|, 0)), so dividing by the
             * barycentric blend of 1/w is safe inside a covered pixel.
             */
            const float w_pixel = 1.0f / inverse_w_pixel;
            const float r = ((bary_a * a.r_over_w) + (bary_b * b.r_over_w) + (bary_c * c.r_over_w)) * w_pixel;
            const float g = ((bary_a * a.g_over_w) + (bary_b * b.g_over_w) + (bary_c * c.g_over_w)) * w_pixel;
            const float blue = ((bary_a * a.b_over_w) + (bary_b * b.b_over_w) + (bary_c * c.b_over_w)) * w_pixel;
            const float alpha = ((bary_a * a.a_over_w) + (bary_b * b.a_over_w) + (bary_c * c.a_over_w)) * w_pixel;

            sdl3d_color color;
            color.r = sdl3d_color_channel_clamp(r);
            color.g = sdl3d_color_channel_clamp(g);
            color.b = sdl3d_color_channel_clamp(blue);
            color.a = sdl3d_color_channel_clamp(alpha);

            sdl3d_write_pixel(framebuffer, px, py, depth, color);
        }
    }
}

static void sdl3d_parallel_triangle_colored_job_run_tile(void *userdata, int tile_index)
{
    sdl3d_parallel_triangle_colored_job *job = (sdl3d_parallel_triangle_colored_job *)userdata;
    const int tile_x = job->first_tile_x + (tile_index % job->tiles_w);
    const int tile_y = job->first_tile_y + (tile_index / job->tiles_w);

    const int tile_min_px_x = sdl3d_max_int(job->triangle.bounds.min_px_x, tile_x * SDL3D_RASTER_TILE_SIZE);
    const int tile_max_px_x = sdl3d_min_int(job->triangle.bounds.max_px_x, ((tile_x + 1) * SDL3D_RASTER_TILE_SIZE) - 1);
    const int tile_min_px_y = sdl3d_max_int(job->triangle.bounds.min_px_y, tile_y * SDL3D_RASTER_TILE_SIZE);
    const int tile_max_px_y = sdl3d_min_int(job->triangle.bounds.max_px_y, ((tile_y + 1) * SDL3D_RASTER_TILE_SIZE) - 1);

    if (tile_min_px_x > tile_max_px_x || tile_min_px_y > tile_max_px_y)
    {
        return;
    }

    sdl3d_rasterize_prepared_triangle_colored_region(job->framebuffer, &job->triangle, tile_min_px_x, tile_max_px_x,
                                                     tile_min_px_y, tile_max_px_y);
}

static bool sdl3d_try_rasterize_prepared_triangle_colored_parallel(sdl3d_framebuffer *framebuffer,
                                                                   const sdl3d_prepared_triangle_colored *triangle)
{
    if (framebuffer->parallel_rasterizer == NULL)
    {
        return false;
    }

    const int first_tile_x = triangle->bounds.min_px_x / SDL3D_RASTER_TILE_SIZE;
    const int last_tile_x = triangle->bounds.max_px_x / SDL3D_RASTER_TILE_SIZE;
    const int first_tile_y = triangle->bounds.min_px_y / SDL3D_RASTER_TILE_SIZE;
    const int last_tile_y = triangle->bounds.max_px_y / SDL3D_RASTER_TILE_SIZE;
    const int tiles_w = last_tile_x - first_tile_x + 1;
    const int tiles_h = last_tile_y - first_tile_y + 1;
    const int tile_count = tiles_w * tiles_h;

    if (tile_count <= 1)
    {
        return false;
    }

    sdl3d_parallel_triangle_colored_job triangle_job;
    triangle_job.framebuffer = framebuffer;
    triangle_job.triangle = *triangle;
    triangle_job.first_tile_x = first_tile_x;
    triangle_job.first_tile_y = first_tile_y;
    triangle_job.tiles_w = tiles_w;

    sdl3d_parallel_job job;
    job.run_tile = sdl3d_parallel_triangle_colored_job_run_tile;
    job.userdata = &triangle_job;
    job.tile_count = tile_count;
    SDL_SetAtomicInt(&job.next_tile_index, 0);

    sdl3d_parallel_rasterizer_execute(framebuffer->parallel_rasterizer, &job);
    return true;
}

static void sdl3d_rasterize_screen_triangle_colored(sdl3d_framebuffer *framebuffer, sdl3d_screen_vertex_colored a,
                                                    sdl3d_screen_vertex_colored b, sdl3d_screen_vertex_colored c)
{
    sdl3d_prepared_triangle_colored triangle;
    if (!sdl3d_prepare_screen_triangle_colored(framebuffer, a, b, c, &triangle))
    {
        return;
    }

    if (sdl3d_try_rasterize_prepared_triangle_colored_parallel(framebuffer, &triangle))
    {
        return;
    }

    sdl3d_rasterize_prepared_triangle_colored_region(framebuffer, &triangle, triangle.bounds.min_px_x,
                                                     triangle.bounds.max_px_x, triangle.bounds.min_px_y,
                                                     triangle.bounds.max_px_y);
}

/*
 * Colored line rasterizer for wireframe edges of a vertex-colored triangle.
 * Linear interpolation along the screen-space segment; not perspective
 * correct (lines have no meaningful barycentric area), which is acceptable
 * for wireframe visualization.
 */
static void sdl3d_rasterize_screen_line_colored(sdl3d_framebuffer *framebuffer, sdl3d_screen_vertex_colored start,
                                                sdl3d_screen_vertex_colored end)
{
    const float dx = end.base.x_px - start.base.x_px;
    const float dy = end.base.y_px - start.base.y_px;
    const float abs_dx = SDL_fabsf(dx);
    const float abs_dy = SDL_fabsf(dy);
    const float length = (abs_dx > abs_dy) ? abs_dx : abs_dy;

    if (length <= 0.0f)
    {
        const int px = (int)SDL_lroundf(start.base.x_px);
        const int py = (int)SDL_lroundf(start.base.y_px);
        if (px >= 0 && px < framebuffer->width && py >= 0 && py < framebuffer->height)
        {
            const float w_start = 1.0f / start.inverse_w;
            sdl3d_color color;
            color.r = sdl3d_color_channel_clamp(start.r_over_w * w_start);
            color.g = sdl3d_color_channel_clamp(start.g_over_w * w_start);
            color.b = sdl3d_color_channel_clamp(start.b_over_w * w_start);
            color.a = sdl3d_color_channel_clamp(start.a_over_w * w_start);
            sdl3d_write_pixel(framebuffer, px, py, start.base.z, color);
        }
        return;
    }

    const int steps = (int)length + 1;
    const float inverse_steps = 1.0f / (float)steps;

    const float start_w = 1.0f / start.inverse_w;
    const float end_w = 1.0f / end.inverse_w;
    const float start_r = start.r_over_w * start_w;
    const float start_g = start.g_over_w * start_w;
    const float start_b = start.b_over_w * start_w;
    const float start_a = start.a_over_w * start_w;
    const float end_r = end.r_over_w * end_w;
    const float end_g = end.g_over_w * end_w;
    const float end_b = end.b_over_w * end_w;
    const float end_a = end.a_over_w * end_w;

    const float step_x = dx * inverse_steps;
    const float step_y = dy * inverse_steps;
    const float step_z = (end.base.z - start.base.z) * inverse_steps;
    const float step_r = (end_r - start_r) * inverse_steps;
    const float step_g = (end_g - start_g) * inverse_steps;
    const float step_b = (end_b - start_b) * inverse_steps;
    const float step_a = (end_a - start_a) * inverse_steps;

    float x = start.base.x_px;
    float y = start.base.y_px;
    float z = start.base.z;
    float r = start_r;
    float g = start_g;
    float blue = start_b;
    float alpha = start_a;

    for (int i = 0; i <= steps; ++i)
    {
        const int px = (int)SDL_lroundf(x);
        const int py = (int)SDL_lroundf(y);
        if (px >= 0 && px < framebuffer->width && py >= 0 && py < framebuffer->height)
        {
            sdl3d_color color;
            color.r = sdl3d_color_channel_clamp(r);
            color.g = sdl3d_color_channel_clamp(g);
            color.b = sdl3d_color_channel_clamp(blue);
            color.a = sdl3d_color_channel_clamp(alpha);
            sdl3d_write_pixel(framebuffer, px, py, z, color);
        }
        x += step_x;
        y += step_y;
        z += step_z;
        r += step_r;
        g += step_g;
        blue += step_b;
        alpha += step_a;
    }
}

void sdl3d_rasterize_triangle_colored(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 v0, sdl3d_vec3 v1,
                                      sdl3d_vec3 v2, sdl3d_color c0, sdl3d_color c1, sdl3d_color c2,
                                      bool backface_culling_enabled, bool wireframe_enabled)
{
    if (framebuffer == NULL || framebuffer->color_pixels == NULL || framebuffer->depth_pixels == NULL)
    {
        return;
    }

    sdl3d_clip_vertex_colored clip[3];
    clip[0].position = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v0, 1.0f));
    clip[0].r = (float)c0.r;
    clip[0].g = (float)c0.g;
    clip[0].b = (float)c0.b;
    clip[0].a = (float)c0.a;
    clip[1].position = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v1, 1.0f));
    clip[1].r = (float)c1.r;
    clip[1].g = (float)c1.g;
    clip[1].b = (float)c1.b;
    clip[1].a = (float)c1.a;
    clip[2].position = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v2, 1.0f));
    clip[2].r = (float)c2.r;
    clip[2].g = (float)c2.g;
    clip[2].b = (float)c2.b;
    clip[2].a = (float)c2.a;

    sdl3d_clip_vertex_colored clipped[SDL3D_CLIP_MAX_VERTICES];
    const int clipped_count = sdl3d_clip_triangle_colored(clip[0], clip[1], clip[2], clipped);
    if (clipped_count < 3)
    {
        return;
    }

    sdl3d_screen_vertex_colored screen[SDL3D_CLIP_MAX_VERTICES];
    sdl3d_screen_vertex screen_positions[SDL3D_CLIP_MAX_VERTICES];
    for (int i = 0; i < clipped_count; ++i)
    {
        screen[i] = sdl3d_viewport_transform_colored(clipped[i], framebuffer->width, framebuffer->height);
        screen_positions[i] = screen[i].base;
    }

    const Sint64 polygon_area = sdl3d_screen_polygon_signed_area(screen_positions, clipped_count);
    if (polygon_area == 0)
    {
        return;
    }

    if (backface_culling_enabled && polygon_area >= 0)
    {
        return;
    }

    if (wireframe_enabled)
    {
        for (int i = 0; i < clipped_count; ++i)
        {
            sdl3d_rasterize_screen_line_colored(framebuffer, screen[i], screen[(i + 1) % clipped_count]);
        }
        return;
    }

    for (int i = 1; i + 1 < clipped_count; ++i)
    {
        sdl3d_rasterize_screen_triangle_colored(framebuffer, screen[0], screen[i], screen[i + 1]);
    }
}

/* --- Textured triangle rasterization ------------------------------------ */

typedef struct sdl3d_clip_vertex_textured
{
    sdl3d_vec4 position;
    float u;
    float v;
    float modulate_r;
    float modulate_g;
    float modulate_b;
    float modulate_a;
} sdl3d_clip_vertex_textured;

static sdl3d_clip_vertex_textured sdl3d_clip_vertex_textured_lerp(sdl3d_clip_vertex_textured a,
                                                                  sdl3d_clip_vertex_textured b, float t)
{
    sdl3d_clip_vertex_textured out;
    out.position = sdl3d_vec4_lerp(a.position, b.position, t);
    out.u = a.u + (b.u - a.u) * t;
    out.v = a.v + (b.v - a.v) * t;
    out.modulate_r = a.modulate_r + (b.modulate_r - a.modulate_r) * t;
    out.modulate_g = a.modulate_g + (b.modulate_g - a.modulate_g) * t;
    out.modulate_b = a.modulate_b + (b.modulate_b - a.modulate_b) * t;
    out.modulate_a = a.modulate_a + (b.modulate_a - a.modulate_a) * t;
    return out;
}

static int sdl3d_clip_polygon_against_plane_textured(const sdl3d_clip_vertex_textured *in, int count_in,
                                                     sdl3d_clip_vertex_textured *out, sdl3d_clip_plane plane)
{
    if (count_in < 3)
    {
        return 0;
    }

    int count_out = 0;
    sdl3d_clip_vertex_textured prev = in[count_in - 1];
    float prev_distance = sdl3d_clip_distance(prev.position, plane);

    for (int i = 0; i < count_in; ++i)
    {
        const sdl3d_clip_vertex_textured curr = in[i];
        const float curr_distance = sdl3d_clip_distance(curr.position, plane);
        const bool prev_inside = prev_distance >= 0.0f;
        const bool curr_inside = curr_distance >= 0.0f;

        if (prev_inside != curr_inside)
        {
            const float t = prev_distance / (prev_distance - curr_distance);
            if (count_out < SDL3D_CLIP_MAX_VERTICES)
            {
                out[count_out++] = sdl3d_clip_vertex_textured_lerp(prev, curr, t);
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

static int sdl3d_clip_triangle_textured(sdl3d_clip_vertex_textured v0, sdl3d_clip_vertex_textured v1,
                                        sdl3d_clip_vertex_textured v2, sdl3d_clip_vertex_textured *out)
{
    sdl3d_clip_vertex_textured buffer_a[SDL3D_CLIP_MAX_VERTICES];
    sdl3d_clip_vertex_textured buffer_b[SDL3D_CLIP_MAX_VERTICES];

    buffer_a[0] = v0;
    buffer_a[1] = v1;
    buffer_a[2] = v2;
    int count = 3;

    sdl3d_clip_vertex_textured *src = buffer_a;
    sdl3d_clip_vertex_textured *dst = buffer_b;

    for (int plane = 0; plane < 6; ++plane)
    {
        count = sdl3d_clip_polygon_against_plane_textured(src, count, dst, (sdl3d_clip_plane)plane);
        if (count == 0)
        {
            return 0;
        }
        sdl3d_clip_vertex_textured *swap = src;
        src = dst;
        dst = swap;
    }

    for (int i = 0; i < count; ++i)
    {
        out[i] = src[i];
    }
    return count;
}

typedef struct sdl3d_screen_vertex_textured
{
    sdl3d_screen_vertex base;
    float inverse_w;
    float u_over_w;
    float v_over_w;
    float modulate_r_over_w;
    float modulate_g_over_w;
    float modulate_b_over_w;
    float modulate_a_over_w;
} sdl3d_screen_vertex_textured;

static sdl3d_screen_vertex_textured sdl3d_viewport_transform_textured(sdl3d_clip_vertex_textured v, int width,
                                                                      int height)
{
    sdl3d_screen_vertex_textured out;
    const float inverse_w = 1.0f / v.position.w;

    out.base = sdl3d_viewport_transform(v.position, width, height);
    out.inverse_w = inverse_w;
    out.u_over_w = v.u * inverse_w;
    out.v_over_w = v.v * inverse_w;
    out.modulate_r_over_w = v.modulate_r * inverse_w;
    out.modulate_g_over_w = v.modulate_g * inverse_w;
    out.modulate_b_over_w = v.modulate_b * inverse_w;
    out.modulate_a_over_w = v.modulate_a * inverse_w;
    return out;
}

typedef struct sdl3d_prepared_triangle_textured
{
    sdl3d_screen_vertex_textured a;
    sdl3d_screen_vertex_textured b;
    sdl3d_screen_vertex_textured c;
    Sint64 area;
    int bias_ab;
    int bias_bc;
    int bias_ca;
    float inverse_area;
    sdl3d_triangle_bounds bounds;
} sdl3d_prepared_triangle_textured;

typedef struct sdl3d_parallel_triangle_textured_job
{
    sdl3d_framebuffer *framebuffer;
    sdl3d_prepared_triangle_textured triangle;
    const sdl3d_texture2d *texture;
    const struct sdl3d_lighting_params *lighting_params;
    int first_tile_x;
    int first_tile_y;
    int tiles_w;
} sdl3d_parallel_triangle_textured_job;

static bool sdl3d_prepare_screen_triangle_textured(const sdl3d_framebuffer *framebuffer, sdl3d_screen_vertex_textured a,
                                                   sdl3d_screen_vertex_textured b, sdl3d_screen_vertex_textured c,
                                                   sdl3d_prepared_triangle_textured *out_triangle)
{
    Sint64 area = sdl3d_screen_triangle_signed_area(a.base, b.base, c.base);
    if (area == 0)
    {
        return false;
    }
    if (area < 0)
    {
        sdl3d_screen_vertex_textured tmp = b;
        b = c;
        c = tmp;
        area = -area;
    }

    const int min_fx = sdl3d_min_int(a.base.x_fx, sdl3d_min_int(b.base.x_fx, c.base.x_fx));
    const int max_fx = sdl3d_max_int(a.base.x_fx, sdl3d_max_int(b.base.x_fx, c.base.x_fx));
    const int min_fy = sdl3d_min_int(a.base.y_fx, sdl3d_min_int(b.base.y_fx, c.base.y_fx));
    const int max_fy = sdl3d_max_int(a.base.y_fx, sdl3d_max_int(b.base.y_fx, c.base.y_fx));

    const int min_px_x = sdl3d_max_int(0, min_fx >> SDL3D_SUBPIXEL_BITS);
    const int max_px_x = sdl3d_min_int(framebuffer->width - 1, (max_fx - 1) >> SDL3D_SUBPIXEL_BITS);
    const int min_px_y = sdl3d_max_int(0, min_fy >> SDL3D_SUBPIXEL_BITS);
    const int max_px_y = sdl3d_min_int(framebuffer->height - 1, (max_fy - 1) >> SDL3D_SUBPIXEL_BITS);

    if (min_px_x > max_px_x || min_px_y > max_px_y)
    {
        return false;
    }

    out_triangle->a = a;
    out_triangle->b = b;
    out_triangle->c = c;
    out_triangle->area = area;
    out_triangle->bias_ab = sdl3d_fill_bias(a.base.x_fx, a.base.y_fx, b.base.x_fx, b.base.y_fx);
    out_triangle->bias_bc = sdl3d_fill_bias(b.base.x_fx, b.base.y_fx, c.base.x_fx, c.base.y_fx);
    out_triangle->bias_ca = sdl3d_fill_bias(c.base.x_fx, c.base.y_fx, a.base.x_fx, a.base.y_fx);
    out_triangle->inverse_area = 1.0f / (float)area;
    out_triangle->bounds.min_px_x = min_px_x;
    out_triangle->bounds.max_px_x = max_px_x;
    out_triangle->bounds.min_px_y = min_px_y;
    out_triangle->bounds.max_px_y = max_px_y;
    return true;
}

static void sdl3d_rasterize_prepared_triangle_textured_region(sdl3d_framebuffer *framebuffer,
                                                              const sdl3d_prepared_triangle_textured *triangle,
                                                              const sdl3d_texture2d *texture,
                                                              const struct sdl3d_lighting_params *lighting_params,
                                                              int min_px_x, int max_px_x, int min_px_y, int max_px_y)
{
    const sdl3d_screen_vertex_textured a = triangle->a;
    const sdl3d_screen_vertex_textured b = triangle->b;
    const sdl3d_screen_vertex_textured c = triangle->c;

    for (int py = min_px_y; py <= max_px_y; ++py)
    {
        const int sample_y = (py << SDL3D_SUBPIXEL_BITS) + SDL3D_SUBPIXEL_HALF;
        for (int px = min_px_x; px <= max_px_x; ++px)
        {
            float texture_r = 1.0f;
            float texture_g = 1.0f;
            float texture_b = 1.0f;
            float texture_a = 1.0f;
            float output_r = 0.0f;
            float output_g = 0.0f;
            float output_b = 0.0f;
            float output_a = 0.0f;

            const int sample_x = (px << SDL3D_SUBPIXEL_BITS) + SDL3D_SUBPIXEL_HALF;
            const Sint64 w_ab =
                sdl3d_edge_function(a.base.x_fx, a.base.y_fx, b.base.x_fx, b.base.y_fx, sample_x, sample_y) +
                triangle->bias_ab;
            const Sint64 w_bc =
                sdl3d_edge_function(b.base.x_fx, b.base.y_fx, c.base.x_fx, c.base.y_fx, sample_x, sample_y) +
                triangle->bias_bc;
            const Sint64 w_ca =
                sdl3d_edge_function(c.base.x_fx, c.base.y_fx, a.base.x_fx, a.base.y_fx, sample_x, sample_y) +
                triangle->bias_ca;

            if ((w_ab | w_bc | w_ca) < 0)
            {
                continue;
            }

            const float bary_a = (float)w_bc * triangle->inverse_area;
            const float bary_b = (float)w_ca * triangle->inverse_area;
            const float bary_c = (float)w_ab * triangle->inverse_area;
            const float depth = (bary_a * a.base.z) + (bary_b * b.base.z) + (bary_c * c.base.z);
            const float inverse_w_pixel = (bary_a * a.inverse_w) + (bary_b * b.inverse_w) + (bary_c * c.inverse_w);
            const float pixel_w = 1.0f / inverse_w_pixel;
            float u;
            float v;
            const float modulate_r =
                ((bary_a * a.modulate_r_over_w) + (bary_b * b.modulate_r_over_w) + (bary_c * c.modulate_r_over_w)) *
                pixel_w;
            const float modulate_g =
                ((bary_a * a.modulate_g_over_w) + (bary_b * b.modulate_g_over_w) + (bary_c * c.modulate_g_over_w)) *
                pixel_w;
            const float modulate_b =
                ((bary_a * a.modulate_b_over_w) + (bary_b * b.modulate_b_over_w) + (bary_c * c.modulate_b_over_w)) *
                pixel_w;
            const float modulate_a =
                ((bary_a * a.modulate_a_over_w) + (bary_b * b.modulate_a_over_w) + (bary_c * c.modulate_a_over_w)) *
                pixel_w;

            if (lighting_params != NULL && lighting_params->uv_mode == SDL3D_UV_AFFINE)
            {
                const float aw = a.inverse_w > 0.0f ? 1.0f / a.inverse_w : 1.0f;
                const float bw = b.inverse_w > 0.0f ? 1.0f / b.inverse_w : 1.0f;
                const float cw = c.inverse_w > 0.0f ? 1.0f / c.inverse_w : 1.0f;
                u = bary_a * (a.u_over_w * aw) + bary_b * (b.u_over_w * bw) + bary_c * (c.u_over_w * cw);
                v = bary_a * (a.v_over_w * aw) + bary_b * (b.v_over_w * bw) + bary_c * (c.v_over_w * cw);
            }
            else
            {
                u = ((bary_a * a.u_over_w) + (bary_b * b.u_over_w) + (bary_c * c.u_over_w)) * pixel_w;
                v = ((bary_a * a.v_over_w) + (bary_b * b.v_over_w) + (bary_c * c.v_over_w)) * pixel_w;
            }

            if (texture != NULL)
            {
                sdl3d_texture_sample_rgba(texture, u, v, 0.0f, &texture_r, &texture_g, &texture_b, &texture_a);
            }

            output_r = texture_r * modulate_r;
            output_g = texture_g * modulate_g;
            output_b = texture_b * modulate_b;
            output_a = texture_a * modulate_a;

            if (output_a <= 0.0f)
            {
                continue;
            }

            if (lighting_params != NULL && lighting_params->color_quantize && lighting_params->color_depth > 0)
            {
                static const float bayer4x4[4][4] = {{0.0f / 16.0f, 8.0f / 16.0f, 2.0f / 16.0f, 10.0f / 16.0f},
                                                     {12.0f / 16.0f, 4.0f / 16.0f, 14.0f / 16.0f, 6.0f / 16.0f},
                                                     {3.0f / 16.0f, 11.0f / 16.0f, 1.0f / 16.0f, 9.0f / 16.0f},
                                                     {15.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f, 5.0f / 16.0f}};
                const float levels = (float)lighting_params->color_depth;
                const float step = 1.0f / levels;
                const float dither = (bayer4x4[py & 3][px & 3] - 0.5f) * step;
                output_r = SDL_floorf((output_r + dither) * levels + 0.5f) / levels;
                output_g = SDL_floorf((output_g + dither) * levels + 0.5f) / levels;
                output_b = SDL_floorf((output_b + dither) * levels + 0.5f) / levels;
                if (output_r < 0.0f)
                {
                    output_r = 0.0f;
                }
                if (output_g < 0.0f)
                {
                    output_g = 0.0f;
                }
                if (output_b < 0.0f)
                {
                    output_b = 0.0f;
                }
            }

            sdl3d_color color;
            color.r = sdl3d_color_channel_clamp(output_r * 255.0f);
            color.g = sdl3d_color_channel_clamp(output_g * 255.0f);
            color.b = sdl3d_color_channel_clamp(output_b * 255.0f);
            color.a = sdl3d_color_channel_clamp(output_a * 255.0f);

            sdl3d_write_pixel(framebuffer, px, py, depth, color);
        }
    }
}

static void sdl3d_parallel_triangle_textured_job_run_tile(void *userdata, int tile_index)
{
    sdl3d_parallel_triangle_textured_job *job = (sdl3d_parallel_triangle_textured_job *)userdata;
    const int tile_x = job->first_tile_x + (tile_index % job->tiles_w);
    const int tile_y = job->first_tile_y + (tile_index / job->tiles_w);

    const int tile_min_px_x = sdl3d_max_int(job->triangle.bounds.min_px_x, tile_x * SDL3D_RASTER_TILE_SIZE);
    const int tile_max_px_x = sdl3d_min_int(job->triangle.bounds.max_px_x, ((tile_x + 1) * SDL3D_RASTER_TILE_SIZE) - 1);
    const int tile_min_px_y = sdl3d_max_int(job->triangle.bounds.min_px_y, tile_y * SDL3D_RASTER_TILE_SIZE);
    const int tile_max_px_y = sdl3d_min_int(job->triangle.bounds.max_px_y, ((tile_y + 1) * SDL3D_RASTER_TILE_SIZE) - 1);

    if (tile_min_px_x > tile_max_px_x || tile_min_px_y > tile_max_px_y)
    {
        return;
    }

    sdl3d_rasterize_prepared_triangle_textured_region(job->framebuffer, &job->triangle, job->texture,
                                                      job->lighting_params, tile_min_px_x, tile_max_px_x, tile_min_px_y,
                                                      tile_max_px_y);
}

static bool sdl3d_try_rasterize_prepared_triangle_textured_parallel(sdl3d_framebuffer *framebuffer,
                                                                    const sdl3d_prepared_triangle_textured *triangle,
                                                                    const sdl3d_texture2d *texture,
                                                                    const struct sdl3d_lighting_params *lighting_params)
{
    if (framebuffer->parallel_rasterizer == NULL)
    {
        return false;
    }

    const int first_tile_x = triangle->bounds.min_px_x / SDL3D_RASTER_TILE_SIZE;
    const int last_tile_x = triangle->bounds.max_px_x / SDL3D_RASTER_TILE_SIZE;
    const int first_tile_y = triangle->bounds.min_px_y / SDL3D_RASTER_TILE_SIZE;
    const int last_tile_y = triangle->bounds.max_px_y / SDL3D_RASTER_TILE_SIZE;
    const int tiles_w = last_tile_x - first_tile_x + 1;
    const int tiles_h = last_tile_y - first_tile_y + 1;
    const int tile_count = tiles_w * tiles_h;

    if (tile_count <= 1)
    {
        return false;
    }

    sdl3d_parallel_triangle_textured_job triangle_job;
    triangle_job.framebuffer = framebuffer;
    triangle_job.triangle = *triangle;
    triangle_job.texture = texture;
    triangle_job.lighting_params = lighting_params;
    triangle_job.first_tile_x = first_tile_x;
    triangle_job.first_tile_y = first_tile_y;
    triangle_job.tiles_w = tiles_w;

    sdl3d_parallel_job job;
    job.run_tile = sdl3d_parallel_triangle_textured_job_run_tile;
    job.userdata = &triangle_job;
    job.tile_count = tile_count;
    SDL_SetAtomicInt(&job.next_tile_index, 0);

    sdl3d_parallel_rasterizer_execute(framebuffer->parallel_rasterizer, &job);
    return true;
}

static void sdl3d_rasterize_screen_triangle_textured(sdl3d_framebuffer *framebuffer, sdl3d_screen_vertex_textured a,
                                                     sdl3d_screen_vertex_textured b, sdl3d_screen_vertex_textured c,
                                                     const sdl3d_texture2d *texture,
                                                     const struct sdl3d_lighting_params *lighting_params)
{
    sdl3d_prepared_triangle_textured triangle;
    if (!sdl3d_prepare_screen_triangle_textured(framebuffer, a, b, c, &triangle))
    {
        return;
    }

    if (sdl3d_try_rasterize_prepared_triangle_textured_parallel(framebuffer, &triangle, texture, lighting_params))
    {
        return;
    }

    sdl3d_rasterize_prepared_triangle_textured_region(framebuffer, &triangle, texture, lighting_params,
                                                      triangle.bounds.min_px_x, triangle.bounds.max_px_x,
                                                      triangle.bounds.min_px_y, triangle.bounds.max_px_y);
}

void sdl3d_rasterize_triangle_textured_profiled(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 v0,
                                                sdl3d_vec3 v1, sdl3d_vec3 v2, sdl3d_vec2 uv0, sdl3d_vec2 uv1,
                                                sdl3d_vec2 uv2, sdl3d_vec4 modulate0, sdl3d_vec4 modulate1,
                                                sdl3d_vec4 modulate2, const sdl3d_texture2d *texture,
                                                const struct sdl3d_lighting_params *lighting_params,
                                                bool backface_culling_enabled, bool wireframe_enabled)
{
    if (framebuffer == NULL || framebuffer->color_pixels == NULL || framebuffer->depth_pixels == NULL)
    {
        return;
    }

    sdl3d_clip_vertex_textured clip[3];
    clip[0].position = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v0, 1.0f));
    clip[0].u = uv0.x;
    clip[0].v = uv0.y;
    clip[0].modulate_r = modulate0.x;
    clip[0].modulate_g = modulate0.y;
    clip[0].modulate_b = modulate0.z;
    clip[0].modulate_a = modulate0.w;
    clip[1].position = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v1, 1.0f));
    clip[1].u = uv1.x;
    clip[1].v = uv1.y;
    clip[1].modulate_r = modulate1.x;
    clip[1].modulate_g = modulate1.y;
    clip[1].modulate_b = modulate1.z;
    clip[1].modulate_a = modulate1.w;
    clip[2].position = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v2, 1.0f));
    clip[2].u = uv2.x;
    clip[2].v = uv2.y;
    clip[2].modulate_r = modulate2.x;
    clip[2].modulate_g = modulate2.y;
    clip[2].modulate_b = modulate2.z;
    clip[2].modulate_a = modulate2.w;

    sdl3d_clip_vertex_textured clipped[SDL3D_CLIP_MAX_VERTICES];
    const int clipped_count = sdl3d_clip_triangle_textured(clip[0], clip[1], clip[2], clipped);
    if (clipped_count < 3)
    {
        return;
    }

    sdl3d_screen_vertex_textured screen[SDL3D_CLIP_MAX_VERTICES];
    sdl3d_screen_vertex screen_positions[SDL3D_CLIP_MAX_VERTICES];
    for (int i = 0; i < clipped_count; ++i)
    {
        screen[i] = sdl3d_viewport_transform_textured(clipped[i], framebuffer->width, framebuffer->height);
        if (lighting_params != NULL && lighting_params->vertex_snap)
        {
            const int prec = lighting_params->vertex_snap_precision > 0 ? lighting_params->vertex_snap_precision : 1;
            const float snapped_x = SDL_roundf(screen[i].base.x_px / (float)prec) * (float)prec;
            const float snapped_y = SDL_roundf(screen[i].base.y_px / (float)prec) * (float)prec;
            screen[i].base.x_px = snapped_x;
            screen[i].base.y_px = snapped_y;
            screen[i].base.x_fx = sdl3d_round_subpixel(snapped_x);
            screen[i].base.y_fx = sdl3d_round_subpixel(snapped_y);
        }
        screen_positions[i] = screen[i].base;
    }

    const Sint64 polygon_area = sdl3d_screen_polygon_signed_area(screen_positions, clipped_count);
    if (polygon_area == 0)
    {
        return;
    }

    if (backface_culling_enabled && polygon_area >= 0)
    {
        return;
    }

    if (wireframe_enabled)
    {
        for (int i = 0; i < clipped_count; ++i)
        {
            sdl3d_screen_vertex_colored line_start;
            sdl3d_screen_vertex_colored line_end;
            const float start_w = 1.0f / screen[i].inverse_w;
            const float end_w = 1.0f / screen[(i + 1) % clipped_count].inverse_w;

            line_start.base = screen[i].base;
            line_start.inverse_w = screen[i].inverse_w;
            line_start.r_over_w = (screen[i].modulate_r_over_w * start_w) * screen[i].inverse_w * 255.0f;
            line_start.g_over_w = (screen[i].modulate_g_over_w * start_w) * screen[i].inverse_w * 255.0f;
            line_start.b_over_w = (screen[i].modulate_b_over_w * start_w) * screen[i].inverse_w * 255.0f;
            line_start.a_over_w = (screen[i].modulate_a_over_w * start_w) * screen[i].inverse_w * 255.0f;

            line_end.base = screen[(i + 1) % clipped_count].base;
            line_end.inverse_w = screen[(i + 1) % clipped_count].inverse_w;
            line_end.r_over_w = (screen[(i + 1) % clipped_count].modulate_r_over_w * end_w) *
                                screen[(i + 1) % clipped_count].inverse_w * 255.0f;
            line_end.g_over_w = (screen[(i + 1) % clipped_count].modulate_g_over_w * end_w) *
                                screen[(i + 1) % clipped_count].inverse_w * 255.0f;
            line_end.b_over_w = (screen[(i + 1) % clipped_count].modulate_b_over_w * end_w) *
                                screen[(i + 1) % clipped_count].inverse_w * 255.0f;
            line_end.a_over_w = (screen[(i + 1) % clipped_count].modulate_a_over_w * end_w) *
                                screen[(i + 1) % clipped_count].inverse_w * 255.0f;

            sdl3d_rasterize_screen_line_colored(framebuffer, line_start, line_end);
        }
        return;
    }

    for (int i = 1; i + 1 < clipped_count; ++i)
    {
        sdl3d_rasterize_screen_triangle_textured(framebuffer, screen[0], screen[i], screen[i + 1], texture,
                                                 lighting_params);
    }
}

void sdl3d_rasterize_triangle_textured(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 v0, sdl3d_vec3 v1,
                                       sdl3d_vec3 v2, sdl3d_vec2 uv0, sdl3d_vec2 uv1, sdl3d_vec2 uv2,
                                       sdl3d_vec4 modulate0, sdl3d_vec4 modulate1, sdl3d_vec4 modulate2,
                                       const sdl3d_texture2d *texture, bool backface_culling_enabled,
                                       bool wireframe_enabled)
{
    sdl3d_rasterize_triangle_textured_profiled(framebuffer, mvp, v0, v1, v2, uv0, uv1, uv2, modulate0, modulate1,
                                               modulate2, texture, NULL, backface_culling_enabled, wireframe_enabled);
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

/* ------------------------------------------------------------------ */
/* Lit textured triangle: normals + world positions through clipping   */
/* ------------------------------------------------------------------ */

typedef struct sdl3d_clip_vertex_lit
{
    sdl3d_vec4 position;
    float u, v;
    float mod_r, mod_g, mod_b, mod_a;
    float nx, ny, nz;
    float wx, wy, wz;
    float fog_factor;
} sdl3d_clip_vertex_lit;

static sdl3d_clip_vertex_lit sdl3d_clip_vertex_lit_lerp(sdl3d_clip_vertex_lit a, sdl3d_clip_vertex_lit b, float t)
{
    sdl3d_clip_vertex_lit out;
    out.position = sdl3d_vec4_lerp(a.position, b.position, t);
    out.u = a.u + (b.u - a.u) * t;
    out.v = a.v + (b.v - a.v) * t;
    out.mod_r = a.mod_r + (b.mod_r - a.mod_r) * t;
    out.mod_g = a.mod_g + (b.mod_g - a.mod_g) * t;
    out.mod_b = a.mod_b + (b.mod_b - a.mod_b) * t;
    out.mod_a = a.mod_a + (b.mod_a - a.mod_a) * t;
    out.nx = a.nx + (b.nx - a.nx) * t;
    out.ny = a.ny + (b.ny - a.ny) * t;
    out.nz = a.nz + (b.nz - a.nz) * t;
    out.wx = a.wx + (b.wx - a.wx) * t;
    out.wy = a.wy + (b.wy - a.wy) * t;
    out.wz = a.wz + (b.wz - a.wz) * t;
    out.fog_factor = a.fog_factor + (b.fog_factor - a.fog_factor) * t;
    return out;
}

static int sdl3d_clip_polygon_against_plane_lit(const sdl3d_clip_vertex_lit *in_verts, int in_count,
                                                sdl3d_clip_vertex_lit *out_verts, sdl3d_clip_plane plane)
{
    int out_count = 0;
    if (in_count < 1)
    {
        return 0;
    }
    sdl3d_clip_vertex_lit prev = in_verts[in_count - 1];
    float prev_dist = sdl3d_clip_distance(prev.position, plane);
    for (int i = 0; i < in_count; ++i)
    {
        sdl3d_clip_vertex_lit curr = in_verts[i];
        float curr_dist = sdl3d_clip_distance(curr.position, plane);
        if (prev_dist >= 0.0f)
        {
            if (curr_dist >= 0.0f)
            {
                out_verts[out_count++] = curr;
            }
            else
            {
                float t = prev_dist / (prev_dist - curr_dist);
                out_verts[out_count++] = sdl3d_clip_vertex_lit_lerp(prev, curr, t);
            }
        }
        else if (curr_dist >= 0.0f)
        {
            float t = prev_dist / (prev_dist - curr_dist);
            out_verts[out_count++] = sdl3d_clip_vertex_lit_lerp(prev, curr, t);
            out_verts[out_count++] = curr;
        }
        prev = curr;
        prev_dist = curr_dist;
    }
    return out_count;
}

static int sdl3d_clip_triangle_lit(sdl3d_clip_vertex_lit *verts)
{
    sdl3d_clip_vertex_lit buf_a[SDL3D_CLIP_MAX_VERTICES];
    sdl3d_clip_vertex_lit buf_b[SDL3D_CLIP_MAX_VERTICES];
    int count = 3;
    sdl3d_clip_vertex_lit *src = verts;
    sdl3d_clip_vertex_lit *dst = buf_a;
    for (int p = 0; p < 6; ++p)
    {
        count = sdl3d_clip_polygon_against_plane_lit(src, count, dst, (sdl3d_clip_plane)p);
        if (count < 3)
        {
            return 0;
        }
        src = dst;
        dst = (src == buf_a) ? buf_b : buf_a;
    }
    if (src != verts)
    {
        for (int i = 0; i < count; ++i)
        {
            verts[i] = src[i];
        }
    }
    return count;
}

typedef struct sdl3d_screen_vertex_lit
{
    sdl3d_screen_vertex base;
    float inverse_w;
    float u_over_w, v_over_w;
    float mod_r_over_w, mod_g_over_w, mod_b_over_w, mod_a_over_w;
    float nx_over_w, ny_over_w, nz_over_w;
    float wx_over_w, wy_over_w, wz_over_w;
    float fog_over_w;
} sdl3d_screen_vertex_lit;

static sdl3d_screen_vertex_lit sdl3d_viewport_transform_lit(sdl3d_clip_vertex_lit v, int width, int height)
{
    sdl3d_screen_vertex_lit out;
    float iw = 1.0f / v.position.w;
    out.base = sdl3d_viewport_transform(v.position, width, height);
    out.inverse_w = iw;
    out.u_over_w = v.u * iw;
    out.v_over_w = v.v * iw;
    out.mod_r_over_w = v.mod_r * iw;
    out.mod_g_over_w = v.mod_g * iw;
    out.mod_b_over_w = v.mod_b * iw;
    out.mod_a_over_w = v.mod_a * iw;
    out.nx_over_w = v.nx * iw;
    out.ny_over_w = v.ny * iw;
    out.nz_over_w = v.nz * iw;
    out.wx_over_w = v.wx * iw;
    out.wy_over_w = v.wy * iw;
    out.wz_over_w = v.wz * iw;
    out.fog_over_w = v.fog_factor * iw;
    return out;
}

typedef struct sdl3d_prepared_triangle_lit
{
    sdl3d_screen_vertex_lit a, b, c;
    Sint64 area;
    int bias_ab, bias_bc, bias_ca;
    float inverse_area;
    sdl3d_triangle_bounds bounds;
} sdl3d_prepared_triangle_lit;

static void sdl3d_rasterize_prepared_triangle_lit_region(sdl3d_framebuffer *framebuffer,
                                                         const sdl3d_prepared_triangle_lit *tri,
                                                         const sdl3d_texture2d *texture,
                                                         const sdl3d_lighting_params *lp, int min_px_x, int max_px_x,
                                                         int min_px_y, int max_px_y)
{
    const sdl3d_screen_vertex_lit a = tri->a;
    const sdl3d_screen_vertex_lit b = tri->b;
    const sdl3d_screen_vertex_lit c = tri->c;

    for (int py = min_px_y; py <= max_px_y; ++py)
    {
        const int sy = (py << SDL3D_SUBPIXEL_BITS) + SDL3D_SUBPIXEL_HALF;
        for (int px = min_px_x; px <= max_px_x; ++px)
        {
            const int sx = (px << SDL3D_SUBPIXEL_BITS) + SDL3D_SUBPIXEL_HALF;
            const Sint64 w_ab =
                sdl3d_edge_function(a.base.x_fx, a.base.y_fx, b.base.x_fx, b.base.y_fx, sx, sy) + tri->bias_ab;
            const Sint64 w_bc =
                sdl3d_edge_function(b.base.x_fx, b.base.y_fx, c.base.x_fx, c.base.y_fx, sx, sy) + tri->bias_bc;
            const Sint64 w_ca =
                sdl3d_edge_function(c.base.x_fx, c.base.y_fx, a.base.x_fx, a.base.y_fx, sx, sy) + tri->bias_ca;
            if ((w_ab | w_bc | w_ca) < 0)
            {
                continue;
            }

            const float ba = (float)w_bc * tri->inverse_area;
            const float bb = (float)w_ca * tri->inverse_area;
            const float bc = (float)w_ab * tri->inverse_area;
            const float depth = ba * a.base.z + bb * b.base.z + bc * c.base.z;

            /* Early depth test: skip all attribute interpolation + PBR
             * shading for pixels that would fail the depth test anyway. */
            {
                const int depth_idx = py * framebuffer->width + px;
                if (depth > framebuffer->depth_pixels[depth_idx])
                {
                    continue;
                }
            }

            {
                const float iw_px = ba * a.inverse_w + bb * b.inverse_w + bc * c.inverse_w;
                const float pw = 1.0f / iw_px;

                float u, v, mr, mg, mb, ma;

                if (lp->uv_mode == SDL3D_UV_AFFINE)
                {
                    /* Affine: linear interpolation in screen space (no /w correction). */
                    float aw = a.inverse_w > 0.0f ? 1.0f / a.inverse_w : 1.0f;
                    float bw = b.inverse_w > 0.0f ? 1.0f / b.inverse_w : 1.0f;
                    float cw = c.inverse_w > 0.0f ? 1.0f / c.inverse_w : 1.0f;
                    u = ba * (a.u_over_w * aw) + bb * (b.u_over_w * bw) + bc * (c.u_over_w * cw);
                    v = ba * (a.v_over_w * aw) + bb * (b.v_over_w * bw) + bc * (c.v_over_w * cw);
                }
                else
                {
                    u = (ba * a.u_over_w + bb * b.u_over_w + bc * c.u_over_w) * pw;
                    v = (ba * a.v_over_w + bb * b.v_over_w + bc * c.v_over_w) * pw;
                }

                mr = (ba * a.mod_r_over_w + bb * b.mod_r_over_w + bc * c.mod_r_over_w) * pw;
                mg = (ba * a.mod_g_over_w + bb * b.mod_g_over_w + bc * c.mod_g_over_w) * pw;
                mb = (ba * a.mod_b_over_w + bb * b.mod_b_over_w + bc * c.mod_b_over_w) * pw;
                ma = (ba * a.mod_a_over_w + bb * b.mod_a_over_w + bc * c.mod_a_over_w) * pw;

                {
                    float tr = 1.0f, tg = 1.0f, tb = 1.0f, ta = 1.0f;
                    float albedo_r, albedo_g, albedo_b, out_a;
                    float nx, ny, nz, n_len, wpx, wpy, wpz;
                    float lit_r, lit_g, lit_b;
                    sdl3d_color color;

                    if (texture != NULL)
                    {
                        sdl3d_texture_sample_rgba(texture, u, v, 0.0f, &tr, &tg, &tb, &ta);
                    }

                    albedo_r = tr * mr;
                    albedo_g = tg * mg;
                    albedo_b = tb * mb;
                    out_a = ta * ma;
                    if (out_a <= 0.0f)
                    {
                        continue;
                    }

                    nx = (ba * a.nx_over_w + bb * b.nx_over_w + bc * c.nx_over_w) * pw;
                    ny = (ba * a.ny_over_w + bb * b.ny_over_w + bc * c.ny_over_w) * pw;
                    nz = (ba * a.nz_over_w + bb * b.nz_over_w + bc * c.nz_over_w) * pw;
                    n_len = nx * nx + ny * ny + nz * nz;
                    if (n_len > 1e-12f)
                    {
                        float inv = 1.0f / SDL_sqrtf(n_len);
                        nx *= inv;
                        ny *= inv;
                        nz *= inv;
                    }
                    wpx = (ba * a.wx_over_w + bb * b.wx_over_w + bc * c.wx_over_w) * pw;
                    wpy = (ba * a.wy_over_w + bb * b.wy_over_w + bc * c.wy_over_w) * pw;
                    wpz = (ba * a.wz_over_w + bb * b.wz_over_w + bc * c.wz_over_w) * pw;

                    sdl3d_shade_fragment_pbr(lp, albedo_r, albedo_g, albedo_b, nx, ny, nz, wpx, wpy, wpz, &lit_r,
                                             &lit_g, &lit_b);

                    /* Fog + tonemapping.  When using a real tonemap operator
                     * (ACES/Reinhard), fog blends in linear space before the
                     * operator compresses the range.  With TONEMAP_NONE the fog
                     * color is already sRGB (matches the sky), so we gamma-encode
                     * the lit color first, then mix fog to avoid brightening it. */
                    if (lp->tonemap_mode != SDL3D_TONEMAP_NONE)
                    {
                        if (lp->fog.mode != SDL3D_FOG_NONE)
                        {
                            float fog_f;
                            if (lp->fog_eval == SDL3D_FOG_EVAL_VERTEX)
                            {
                                fog_f = (ba * a.fog_over_w + bb * b.fog_over_w + bc * c.fog_over_w) * pw;
                                if (fog_f < 0.0f)
                                {
                                    fog_f = 0.0f;
                                }
                                if (fog_f > 1.0f)
                                {
                                    fog_f = 1.0f;
                                }
                            }
                            else
                            {
                                float dx = wpx - lp->camera_pos.x;
                                float dy = wpy - lp->camera_pos.y;
                                float dz = wpz - lp->camera_pos.z;
                                float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
                                fog_f = sdl3d_compute_fog_factor(&lp->fog, dist);
                            }
                            lit_r = lit_r * (1.0f - fog_f) + lp->fog.color[0] * fog_f;
                            lit_g = lit_g * (1.0f - fog_f) + lp->fog.color[1] * fog_f;
                            lit_b = lit_b * (1.0f - fog_f) + lp->fog.color[2] * fog_f;
                        }
                        sdl3d_tonemap(lp->tonemap_mode, &lit_r, &lit_g, &lit_b);
                    }
                    else
                    {
                        sdl3d_tonemap(SDL3D_TONEMAP_NONE, &lit_r, &lit_g, &lit_b);
                        if (lp->fog.mode != SDL3D_FOG_NONE)
                        {
                            float fog_f;
                            if (lp->fog_eval == SDL3D_FOG_EVAL_VERTEX)
                            {
                                fog_f = (ba * a.fog_over_w + bb * b.fog_over_w + bc * c.fog_over_w) * pw;
                                if (fog_f < 0.0f)
                                {
                                    fog_f = 0.0f;
                                }
                                if (fog_f > 1.0f)
                                {
                                    fog_f = 1.0f;
                                }
                            }
                            else
                            {
                                float dx = wpx - lp->camera_pos.x;
                                float dy = wpy - lp->camera_pos.y;
                                float dz = wpz - lp->camera_pos.z;
                                float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
                                fog_f = sdl3d_compute_fog_factor(&lp->fog, dist);
                            }
                            lit_r = lit_r * (1.0f - fog_f) + lp->fog.color[0] * fog_f;
                            lit_g = lit_g * (1.0f - fog_f) + lp->fog.color[1] * fog_f;
                            lit_b = lit_b * (1.0f - fog_f) + lp->fog.color[2] * fog_f;
                        }
                    }

                    /* Color quantization with optional Bayer dithering. */
                    if (lp->color_quantize && lp->color_depth > 0)
                    {
                        static const float bayer4x4[4][4] = {
                            {0.0f / 16.0f, 8.0f / 16.0f, 2.0f / 16.0f, 10.0f / 16.0f},
                            {12.0f / 16.0f, 4.0f / 16.0f, 14.0f / 16.0f, 6.0f / 16.0f},
                            {3.0f / 16.0f, 11.0f / 16.0f, 1.0f / 16.0f, 9.0f / 16.0f},
                            {15.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f, 5.0f / 16.0f}};
                        float levels = (float)lp->color_depth;
                        float step = 1.0f / levels;
                        float dither = (bayer4x4[py & 3][px & 3] - 0.5f) * step;
                        lit_r = SDL_floorf((lit_r + dither) * levels + 0.5f) / levels;
                        lit_g = SDL_floorf((lit_g + dither) * levels + 0.5f) / levels;
                        lit_b = SDL_floorf((lit_b + dither) * levels + 0.5f) / levels;
                        if (lit_r < 0.0f)
                        {
                            lit_r = 0.0f;
                        }
                        if (lit_g < 0.0f)
                        {
                            lit_g = 0.0f;
                        }
                        if (lit_b < 0.0f)
                        {
                            lit_b = 0.0f;
                        }
                    }

                    color.r = sdl3d_color_channel_clamp(lit_r * 255.0f);
                    color.g = sdl3d_color_channel_clamp(lit_g * 255.0f);
                    color.b = sdl3d_color_channel_clamp(lit_b * 255.0f);
                    color.a = sdl3d_color_channel_clamp(out_a * 255.0f);
                    sdl3d_write_pixel(framebuffer, px, py, depth, color);
                }
            }
        }
    }
}

static bool sdl3d_prepare_triangle_lit(sdl3d_screen_vertex_lit sv0, sdl3d_screen_vertex_lit sv1,
                                       sdl3d_screen_vertex_lit sv2, sdl3d_framebuffer *framebuffer,
                                       sdl3d_prepared_triangle_lit *out)
{
    sdl3d_screen_vertex_lit a = sv0, b = sv1, c = sv2;
    Sint64 area = sdl3d_edge_function(a.base.x_fx, a.base.y_fx, b.base.x_fx, b.base.y_fx, c.base.x_fx, c.base.y_fx);
    int min_fx, max_fx, min_fy, max_fy;
    int min_px_x, max_px_x, min_px_y, max_px_y;

    if (area < 0)
    {
        sdl3d_screen_vertex_lit tmp = b;
        b = c;
        c = tmp;
        area = -area;
    }
    if (area == 0)
    {
        return false;
    }

    min_fx = sdl3d_min_int(a.base.x_fx, sdl3d_min_int(b.base.x_fx, c.base.x_fx));
    max_fx = sdl3d_max_int(a.base.x_fx, sdl3d_max_int(b.base.x_fx, c.base.x_fx));
    min_fy = sdl3d_min_int(a.base.y_fx, sdl3d_min_int(b.base.y_fx, c.base.y_fx));
    max_fy = sdl3d_max_int(a.base.y_fx, sdl3d_max_int(b.base.y_fx, c.base.y_fx));

    if (framebuffer->scissor_enabled)
    {
        const SDL_Rect *sr = &framebuffer->scissor_rect;
        min_fx = sdl3d_max_int(min_fx, sr->x << SDL3D_SUBPIXEL_BITS);
        max_fx = sdl3d_min_int(max_fx, ((sr->x + sr->w) << SDL3D_SUBPIXEL_BITS) - 1);
        min_fy = sdl3d_max_int(min_fy, sr->y << SDL3D_SUBPIXEL_BITS);
        max_fy = sdl3d_min_int(max_fy, ((sr->y + sr->h) << SDL3D_SUBPIXEL_BITS) - 1);
    }

    min_px_x = sdl3d_max_int(0, min_fx >> SDL3D_SUBPIXEL_BITS);
    max_px_x = sdl3d_min_int(framebuffer->width - 1, (max_fx - 1) >> SDL3D_SUBPIXEL_BITS);
    min_px_y = sdl3d_max_int(0, min_fy >> SDL3D_SUBPIXEL_BITS);
    max_px_y = sdl3d_min_int(framebuffer->height - 1, (max_fy - 1) >> SDL3D_SUBPIXEL_BITS);

    if (min_px_x > max_px_x || min_px_y > max_px_y)
    {
        return false;
    }

    out->a = a;
    out->b = b;
    out->c = c;
    out->area = area;
    out->inverse_area = 1.0f / (float)area;
    out->bias_ab = sdl3d_fill_bias(a.base.x_fx, a.base.y_fx, b.base.x_fx, b.base.y_fx);
    out->bias_bc = sdl3d_fill_bias(b.base.x_fx, b.base.y_fx, c.base.x_fx, c.base.y_fx);
    out->bias_ca = sdl3d_fill_bias(c.base.x_fx, c.base.y_fx, a.base.x_fx, a.base.y_fx);
    out->bounds.min_px_x = min_px_x;
    out->bounds.max_px_x = max_px_x;
    out->bounds.min_px_y = min_px_y;
    out->bounds.max_px_y = max_px_y;
    return true;
}

typedef struct sdl3d_parallel_triangle_lit_job
{
    sdl3d_framebuffer *framebuffer;
    sdl3d_prepared_triangle_lit triangle;
    const sdl3d_texture2d *texture;
    const sdl3d_lighting_params *lp;
    int first_tile_x;
    int first_tile_y;
    int tiles_w;
} sdl3d_parallel_triangle_lit_job;

static void sdl3d_parallel_triangle_lit_job_run_tile(void *userdata, int tile_index)
{
    sdl3d_parallel_triangle_lit_job *job = (sdl3d_parallel_triangle_lit_job *)userdata;
    const int tile_x = job->first_tile_x + (tile_index % job->tiles_w);
    const int tile_y = job->first_tile_y + (tile_index / job->tiles_w);

    const int tile_min_px_x = sdl3d_max_int(job->triangle.bounds.min_px_x, tile_x * SDL3D_RASTER_TILE_SIZE);
    const int tile_max_px_x = sdl3d_min_int(job->triangle.bounds.max_px_x, ((tile_x + 1) * SDL3D_RASTER_TILE_SIZE) - 1);
    const int tile_min_px_y = sdl3d_max_int(job->triangle.bounds.min_px_y, tile_y * SDL3D_RASTER_TILE_SIZE);
    const int tile_max_px_y = sdl3d_min_int(job->triangle.bounds.max_px_y, ((tile_y + 1) * SDL3D_RASTER_TILE_SIZE) - 1);

    if (tile_min_px_x > tile_max_px_x || tile_min_px_y > tile_max_px_y)
    {
        return;
    }

    sdl3d_rasterize_prepared_triangle_lit_region(job->framebuffer, &job->triangle, job->texture, job->lp, tile_min_px_x,
                                                 tile_max_px_x, tile_min_px_y, tile_max_px_y);
}

static bool sdl3d_try_rasterize_prepared_triangle_lit_parallel(sdl3d_framebuffer *framebuffer,
                                                               const sdl3d_prepared_triangle_lit *triangle,
                                                               const sdl3d_texture2d *texture,
                                                               const sdl3d_lighting_params *lp)
{
    int first_tile_x, last_tile_x, first_tile_y, last_tile_y;
    int tiles_w, tiles_h, tile_count;
    sdl3d_parallel_triangle_lit_job triangle_job;
    sdl3d_parallel_job job;

    if (framebuffer->parallel_rasterizer == NULL)
    {
        return false;
    }

    first_tile_x = triangle->bounds.min_px_x / SDL3D_RASTER_TILE_SIZE;
    last_tile_x = triangle->bounds.max_px_x / SDL3D_RASTER_TILE_SIZE;
    first_tile_y = triangle->bounds.min_px_y / SDL3D_RASTER_TILE_SIZE;
    last_tile_y = triangle->bounds.max_px_y / SDL3D_RASTER_TILE_SIZE;
    tiles_w = last_tile_x - first_tile_x + 1;
    tiles_h = last_tile_y - first_tile_y + 1;
    tile_count = tiles_w * tiles_h;

    if (tile_count <= 1)
    {
        return false;
    }

    triangle_job.framebuffer = framebuffer;
    triangle_job.triangle = *triangle;
    triangle_job.texture = texture;
    triangle_job.lp = lp;
    triangle_job.first_tile_x = first_tile_x;
    triangle_job.first_tile_y = first_tile_y;
    triangle_job.tiles_w = tiles_w;

    job.run_tile = sdl3d_parallel_triangle_lit_job_run_tile;
    job.userdata = &triangle_job;
    job.tile_count = tile_count;
    SDL_SetAtomicInt(&job.next_tile_index, 0);

    sdl3d_parallel_rasterizer_execute(framebuffer->parallel_rasterizer, &job);
    return true;
}

void sdl3d_rasterize_triangle_lit(sdl3d_framebuffer *framebuffer, sdl3d_mat4 mvp, sdl3d_vec3 v0, sdl3d_vec3 v1,
                                  sdl3d_vec3 v2, sdl3d_vec2 uv0, sdl3d_vec2 uv1, sdl3d_vec2 uv2, sdl3d_vec3 n0,
                                  sdl3d_vec3 n1, sdl3d_vec3 n2, sdl3d_vec3 wp0, sdl3d_vec3 wp1, sdl3d_vec3 wp2,
                                  sdl3d_vec4 modulate0, sdl3d_vec4 modulate1, sdl3d_vec4 modulate2,
                                  const sdl3d_texture2d *texture, const sdl3d_lighting_params *lighting_params,
                                  bool backface_culling_enabled, bool wireframe_enabled)
{
    sdl3d_clip_vertex_lit clip[SDL3D_CLIP_MAX_VERTICES];
    sdl3d_screen_vertex_lit screen[SDL3D_CLIP_MAX_VERTICES];
    int count;
    Sint64 signed_area;

    if (framebuffer == NULL || framebuffer->color_pixels == NULL || framebuffer->depth_pixels == NULL)
    {
        return;
    }

    /* Build clip vertices with all attributes. */
    sdl3d_vec4 p0 = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v0, 1.0f));
    sdl3d_vec4 p1 = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v1, 1.0f));
    sdl3d_vec4 p2 = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(v2, 1.0f));

    clip[0].position = p0;
    clip[0].u = uv0.x;
    clip[0].v = uv0.y;
    clip[0].mod_r = modulate0.x;
    clip[0].mod_g = modulate0.y;
    clip[0].mod_b = modulate0.z;
    clip[0].mod_a = modulate0.w;
    clip[0].nx = n0.x;
    clip[0].ny = n0.y;
    clip[0].nz = n0.z;
    clip[0].wx = wp0.x;
    clip[0].wy = wp0.y;
    clip[0].wz = wp0.z;
    clip[0].fog_factor = 0.0f;

    clip[1].position = p1;
    clip[1].u = uv1.x;
    clip[1].v = uv1.y;
    clip[1].mod_r = modulate1.x;
    clip[1].mod_g = modulate1.y;
    clip[1].mod_b = modulate1.z;
    clip[1].mod_a = modulate1.w;
    clip[1].nx = n1.x;
    clip[1].ny = n1.y;
    clip[1].nz = n1.z;
    clip[1].wx = wp1.x;
    clip[1].wy = wp1.y;
    clip[1].wz = wp1.z;
    clip[1].fog_factor = 0.0f;

    clip[2].position = p2;
    clip[2].u = uv2.x;
    clip[2].v = uv2.y;
    clip[2].mod_r = modulate2.x;
    clip[2].mod_g = modulate2.y;
    clip[2].mod_b = modulate2.z;
    clip[2].mod_a = modulate2.w;
    clip[2].nx = n2.x;
    clip[2].ny = n2.y;
    clip[2].nz = n2.z;
    clip[2].wx = wp2.x;
    clip[2].wy = wp2.y;
    clip[2].wz = wp2.z;
    clip[2].fog_factor = 0.0f;

    /* Compute per-vertex fog factors for vertex fog mode. */
    if (lighting_params != NULL && lighting_params->fog.mode != SDL3D_FOG_NONE &&
        lighting_params->fog_eval == SDL3D_FOG_EVAL_VERTEX)
    {
        sdl3d_vec3 cam = lighting_params->camera_pos;
        float d0 = SDL_sqrtf((wp0.x - cam.x) * (wp0.x - cam.x) + (wp0.y - cam.y) * (wp0.y - cam.y) +
                             (wp0.z - cam.z) * (wp0.z - cam.z));
        float d1 = SDL_sqrtf((wp1.x - cam.x) * (wp1.x - cam.x) + (wp1.y - cam.y) * (wp1.y - cam.y) +
                             (wp1.z - cam.z) * (wp1.z - cam.z));
        float d2 = SDL_sqrtf((wp2.x - cam.x) * (wp2.x - cam.x) + (wp2.y - cam.y) * (wp2.y - cam.y) +
                             (wp2.z - cam.z) * (wp2.z - cam.z));
        clip[0].fog_factor = sdl3d_compute_fog_factor(&lighting_params->fog, d0);
        clip[1].fog_factor = sdl3d_compute_fog_factor(&lighting_params->fog, d1);
        clip[2].fog_factor = sdl3d_compute_fog_factor(&lighting_params->fog, d2);
    }

    count = sdl3d_clip_triangle_lit(clip);
    if (count < 3)
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        screen[i] = sdl3d_viewport_transform_lit(clip[i], framebuffer->width, framebuffer->height);

        /* Vertex snap: quantize screen coordinates to a grid. */
        if (lighting_params != NULL && lighting_params->vertex_snap)
        {
            int prec = lighting_params->vertex_snap_precision > 0 ? lighting_params->vertex_snap_precision : 1;
            float sx = SDL_roundf(screen[i].base.x_px / (float)prec) * (float)prec;
            float sy = SDL_roundf(screen[i].base.y_px / (float)prec) * (float)prec;
            screen[i].base.x_px = sx;
            screen[i].base.y_px = sy;
            screen[i].base.x_fx = sdl3d_round_subpixel(sx);
            screen[i].base.y_fx = sdl3d_round_subpixel(sy);
        }
    }

    /* Backface culling: positive signed area = CW screen winding = backface. */
    signed_area = sdl3d_edge_function(screen[0].base.x_fx, screen[0].base.y_fx, screen[1].base.x_fx,
                                      screen[1].base.y_fx, screen[2].base.x_fx, screen[2].base.y_fx);
    if (signed_area == 0)
    {
        return;
    }
    if (backface_culling_enabled && signed_area >= 0)
    {
        return;
    }

    if (wireframe_enabled)
    {
        for (int i = 0; i < count; ++i)
        {
            int j = (i + 1) % count;
            float iw_i = screen[i].inverse_w > 0.0f ? screen[i].inverse_w : 1.0f;
            float iw_j = screen[j].inverse_w > 0.0f ? screen[j].inverse_w : 1.0f;
            sdl3d_screen_vertex_colored sa, sb;
            sa.base = screen[i].base;
            sa.inverse_w = screen[i].inverse_w;
            sa.r_over_w = screen[i].mod_r_over_w;
            sa.g_over_w = screen[i].mod_g_over_w;
            sa.b_over_w = screen[i].mod_b_over_w;
            sa.a_over_w = screen[i].mod_a_over_w;
            sb.base = screen[j].base;
            sb.inverse_w = screen[j].inverse_w;
            sb.r_over_w = screen[j].mod_r_over_w;
            sb.g_over_w = screen[j].mod_g_over_w;
            sb.b_over_w = screen[j].mod_b_over_w;
            sb.a_over_w = screen[j].mod_a_over_w;
            (void)iw_i;
            (void)iw_j;
            sdl3d_rasterize_screen_line_colored(framebuffer, sa, sb);
        }
        return;
    }

    /* Fan-triangulate and rasterize. */
    for (int i = 1; i + 1 < count; ++i)
    {
        sdl3d_prepared_triangle_lit prepared;
        if (!sdl3d_prepare_triangle_lit(screen[0], screen[i], screen[i + 1], framebuffer, &prepared))
        {
            continue;
        }
        if (!sdl3d_try_rasterize_prepared_triangle_lit_parallel(framebuffer, &prepared, texture, lighting_params))
        {
            sdl3d_rasterize_prepared_triangle_lit_region(framebuffer, &prepared, texture, lighting_params,
                                                         prepared.bounds.min_px_x, prepared.bounds.max_px_x,
                                                         prepared.bounds.min_px_y, prepared.bounds.max_px_y);
        }
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

bool sdl3d_parallel_rasterizer_create(int worker_count, sdl3d_parallel_rasterizer **out_rasterizer)
{
    if (out_rasterizer == NULL)
    {
        return SDL_InvalidParamError("out_rasterizer");
    }
    if (worker_count <= 0)
    {
        return SDL_SetError("Parallel rasterizer worker count must be positive.");
    }

    *out_rasterizer = NULL;

    sdl3d_parallel_rasterizer *rasterizer = (sdl3d_parallel_rasterizer *)SDL_calloc(1, sizeof(*rasterizer));
    if (rasterizer == NULL)
    {
        return SDL_OutOfMemory();
    }

    rasterizer->threads = (SDL_Thread **)SDL_calloc((size_t)worker_count, sizeof(*rasterizer->threads));
    if (rasterizer->threads == NULL)
    {
        SDL_free(rasterizer);
        return SDL_OutOfMemory();
    }

    rasterizer->work_ready = SDL_CreateSemaphore(0);
    if (rasterizer->work_ready == NULL)
    {
        SDL_free(rasterizer->threads);
        SDL_free(rasterizer);
        return false;
    }

    rasterizer->work_complete = SDL_CreateSemaphore(0);
    if (rasterizer->work_complete == NULL)
    {
        SDL_DestroySemaphore(rasterizer->work_ready);
        SDL_free(rasterizer->threads);
        SDL_free(rasterizer);
        return false;
    }

    rasterizer->worker_count = worker_count;
    SDL_SetAtomicInt(&rasterizer->stop_requested, 0);
    SDL_SetAtomicPointer(&rasterizer->current_job, NULL);

    for (int i = 0; i < worker_count; ++i)
    {
        char thread_name[32];
        (void)SDL_snprintf(thread_name, sizeof(thread_name), "sdl3d-rast-%d", i);
        rasterizer->threads[i] = SDL_CreateThread(sdl3d_parallel_rasterizer_thread_main, thread_name, rasterizer);
        if (rasterizer->threads[i] == NULL)
        {
            rasterizer->worker_count = i;
            sdl3d_parallel_rasterizer_destroy(rasterizer);
            return false;
        }
    }

    *out_rasterizer = rasterizer;
    return true;
}

void sdl3d_parallel_rasterizer_destroy(sdl3d_parallel_rasterizer *rasterizer)
{
    if (rasterizer == NULL)
    {
        return;
    }

    SDL_SetAtomicInt(&rasterizer->stop_requested, 1);
    for (int i = 0; i < rasterizer->worker_count; ++i)
    {
        SDL_SignalSemaphore(rasterizer->work_ready);
    }

    for (int i = 0; i < rasterizer->worker_count; ++i)
    {
        if (rasterizer->threads[i] != NULL)
        {
            SDL_WaitThread(rasterizer->threads[i], NULL);
        }
    }

    SDL_DestroySemaphore(rasterizer->work_complete);
    SDL_DestroySemaphore(rasterizer->work_ready);
    SDL_free(rasterizer->threads);
    SDL_free(rasterizer);
}

int sdl3d_parallel_rasterizer_get_worker_count(const sdl3d_parallel_rasterizer *rasterizer)
{
    if (rasterizer == NULL)
    {
        return 0;
    }

    return rasterizer->worker_count;
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

void sdl3d_framebuffer_clear_rect(sdl3d_framebuffer *framebuffer, const SDL_Rect *rect, sdl3d_color color, float depth)
{
    if (framebuffer == NULL || framebuffer->color_pixels == NULL || framebuffer->depth_pixels == NULL || rect == NULL)
    {
        return;
    }

    Sint64 min_x = rect->x;
    Sint64 min_y = rect->y;
    Sint64 max_x = (Sint64)rect->x + (Sint64)rect->w;
    Sint64 max_y = (Sint64)rect->y + (Sint64)rect->h;

    if (min_x < 0)
    {
        min_x = 0;
    }
    if (min_y < 0)
    {
        min_y = 0;
    }
    if (max_x > framebuffer->width)
    {
        max_x = framebuffer->width;
    }
    if (max_y > framebuffer->height)
    {
        max_y = framebuffer->height;
    }

    if (min_x >= max_x || min_y >= max_y)
    {
        return;
    }

    for (int y = (int)min_y; y < (int)max_y; ++y)
    {
        for (int x = (int)min_x; x < (int)max_x; ++x)
        {
            const int index = (y * framebuffer->width) + x;
            Uint8 *pixel = &framebuffer->color_pixels[index * 4];
            pixel[0] = color.r;
            pixel[1] = color.g;
            pixel[2] = color.b;
            pixel[3] = color.a;
            framebuffer->depth_pixels[index] = depth;
        }
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
